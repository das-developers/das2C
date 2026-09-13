/* Copyright (C) 2017-2024 Chris Piker <chris-piker@uiowa.edu>
 *
 * This file is part of das2C, the Core Das2 C Library.
 *
 * das2C is free software; you can redistribute it and/or modify it under
 * the terms of the GNU Lesser General Public License version 2.1 as published
 * by the Free Software Foundation.
 *
 * das2C is distributed in the hope that it will be useful, but WITHOUT ANY
 * WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS
 * FOR A PARTICULAR PURPOSE.  See the GNU Lesser General Public License for
 * more details.
 *
 * You should have received a copy of the GNU Lesser General Public License
 * version 2.1 along with das2C; if not, see <http://www.gnu.org/licenses/>.
 */

#define _POSIX_C_SOURCE 200112L

#include <string.h>
#include <assert.h>
#include <expat.h>
#include <ctype.h>

#include "stream.h"
#include "dataset.h"
#include "log.h"
#include "form_linear.h"
#include "form_point.h"
#include "codex.h"

#define DS_XML_MAXERR 512

#define _UNIT_BUF_SZ 127
#define _NAME_BUF_SZ 63
#define _TYPE_BUF_SZ 23
#define _VAL_STOREAGE_SZ 12
#define _VAL_COMP_LBL_SZ ((DASFORM_NAME_SZ + 1) * 3)
#define _VAL_UNDER_SZ 64 /* Should be enough room for most variables */

#define _VAL_FILL_SZ 48
#define _VAL_TERM_SZ 48
#define _VAL_MIME_SZ 64
#define _VAL_SEQ_CONST_SZ sizeof(das_time)


/* ****************************************************************************
   Our processing state structure, these are required for expat
*/
typedef struct serial_xml_context {
	StreamDesc* pSd;
	int nPktId;
	
	DasDs* pDs;
	ptrdiff_t aExtShape[VARIDX_MAX];
	DasDim* pCurDim;

	bool bEmbedAsBytes;  /* stream opt-in: flatten undecodable embedded formats */
	bool bFlattened;     /* set once a flatten happened, arms the post-parse gate */
	bool bVarBorrows;    /* current var declared a '^' (borrowed extent) -- seq only */
	
	bool bInPropList;
	bool bInProp;
	char sPropUnits[_UNIT_BUF_SZ+1];
	char sPropName[_NAME_BUF_SZ+1];
	char sPropType[_TYPE_BUF_SZ+1];
	char sPropSep[8];   /* value separator for set/array props (raw wire form) */
	DasAry aPropVal;

	/* saving attributes so they can be used when var creation is ready... */
	bool bInVar;
	/* PARSE state for the element in progress -- this is the reader's own
	   vocabulary, not the object model's.  VS_STRING parks a <bytes> until
	   the packet encoding refines string vs blob. */
	enum var_struct { VS_NONE=0, VS_SCALAR, VS_STRING, VS_BLOB, VS_COMPOSITE }
		varFam;
	das_gen_type   varGenKind; /* gtArray unless a <sequence> child arrives */
	DasForm* pVarForm;   /* built at <ops>, ownership passes to the variable */
	bool bInOps;         /* true only WHILE inside the element */
	bool bOpsSeen;       /* an <ops> arrived for this variable at all */
	das_val_type varItemType;  /* The type of values to store in the array */
	
	int varIntRank; /* Internal index count: 0 for a scalar, 1 for a byte run, one
	                   per intern= level for a composite */
	das_units varUnits;
	char varUse[DASDIM_ROLE_SZ];
	const char* valSemantic;   /* canonical DAS_SEM_* singleton, or NULL if not yet set */
	char valStorage[_VAL_STOREAGE_SZ];
	char varIndex[32];                  /* the raw index= string, kept for embedded* provenance */

	/* intern= carries a SHAPE, not a count: "3" is a vector, "3;3" a matrix.  The
	   level list and the values-per-item product are BOTH needed downstream (the
	   array wants one dimension per level, the codec and the <sequence> check want
	   the product) so both are kept.  Conflating them into one int is what held
	   composites to a single internal level. */
	int       nVarIntRank;              /* intern= level count; 0 when not a composite */
	ptrdiff_t aVarIntShape[VARIDX_MAX]; /* one extent per level */
	int       nVarComps;                /* product of the levels: values per item */

	int8_t aVarMap[VARIDX_MAX];
	ptrdiff_t aVarExtShape[VARIDX_MAX]; /* the var's own declared extents */

	DasDesc varProps; /* Temporary accumulator for variable properties */

	/* Stuff needed for sequence vars */
	ubyte aSeqMin[_VAL_SEQ_CONST_SZ];     /* big enough to hold a double */
	ubyte aSeqInter[VARIDX_MAX * _VAL_SEQ_CONST_SZ];  /* one slope per dep axis */

	/* Composite sequence: each <sequence> child is one component, gathered here
	   until close-var builds the generator.  Scratch grows to the component
	   count of the widest composite seen and lives as long as the parse. */
	int    nSeqSeen;
	int    nSeqCap;                     /* components the scratch can hold */
	ubyte* pSeqMinC;                    /* nSeqCap * _VAL_SEQ_CONST_SZ */
	ubyte* pSeqInterC;                  /* nSeqCap * VARIDX_MAX * _VAL_SEQ_CONST_SZ */

	/* Stuff needed for any array var */
	DasAry* pCurAry;

	/* Stuff needed only for packet data array vars */
	const char* sValEncType;   /* canonical DAS_ENC_* singleton, or NULL if not yet set */
	char sValMime[_VAL_MIME_SZ];  /* <packet mime="..."> for an embedded (codex) blob; "" if none */
	int nPktItems;
	int nPktItemBytes;
	char sPktFillVal[_VAL_FILL_SZ];

	char sValTerm[_VAL_TERM_SZ];
	char sItemsTerm[_VAL_TERM_SZ];
	signed char nTrimReq;  /* <packet trim="...">: -1 unset (default on for var-width
	                          utf8), 0 explicit false, 1 explicit true */

	/* Stuff needed only for embedded values array vars */
	DasCodec codecHdrVals;
	ubyte aValUnderFlow[_VAL_UNDER_SZ];
	int nValUnderFlowValid;

	/* The codec's own tally of values handed back across every character-data
	   callback for one <values> element.  Counted independently of the array
	   shape so the two can be cross checked when the element closes. */
	size_t uHdrValsRead;

	/* ... when we hit the <packet><values>or<sequence>  we'll have enough info
	   stored above to create both the variable and it associated array */

	bool bInValues;
	char cValSep;

	DasErrCode nDasErr;
	char sErrMsg[DS_XML_MAXERR];
} context_t;

/* ************************************************************************* */

/* A variable's parse state has two boundaries and they do different jobs.
 *
 * _serial_var_init() runs at the <scalar>/<vector>/<bytes> START tag and states
 * every start-of-variable value.  It has to be the way IN, not the way out
 *
 * It is idempotent, can re-call on a parse parse that died between the two tags.
 */
static void _serial_var_init(context_t* pCtx)
{
	pCtx->varFam = VS_NONE;
	pCtx->varGenKind = gtArray; /* most common kind */

	/* LINEAR, not NULL.  Absence of <ops> means ordinary numbers; NULL would
	   mean "no math at all", which only a byte run gets to say.  Binding it
	   explicitly is what keeps every operation on one dispatch path. */
	pCtx->pVarForm = new_DasFormLinear();
	pCtx->bInOps   = false;
	pCtx->bOpsSeen = false;
	pCtx->varItemType = vtUnknown;

	pCtx->varIntRank = 0;
	pCtx->varUnits = NULL;

	/* Hopefull compiler will reduce all this to a single op-code for memory erasure */
	memset(pCtx->varUse, 0, DAS_FIELD_SZ(context_t, varUse) );
	pCtx->valSemantic = NULL;
	memset(pCtx->valStorage,  0, DAS_FIELD_SZ(context_t, valStorage) );
	memset(pCtx->varIndex,    0, DAS_FIELD_SZ(context_t, varIndex) );
	pCtx->bVarBorrows = false;
	pCtx->nVarComps = 0;
	pCtx->nVarIntRank = 0;
	for(int i = 0; i < VARIDX_MAX; ++i){
		pCtx->aVarMap[i] = VARIDX_UNUSED;
		pCtx->aVarExtShape[i] = VARIDX_UNUSED;
		pCtx->aVarIntShape[i] = VARIDX_UNUSED;
	}
	memset(pCtx->aSeqMin, 0, DAS_FIELD_SZ(context_t, aSeqMin));
	memset(pCtx->aSeqInter, 0, DAS_FIELD_SZ(context_t, aSeqInter));
	pCtx->nSeqSeen = 0;   /* the component scratch is overwritten, not cleared */

	pCtx->sValEncType = NULL;
	memset(pCtx->sValMime, 0, DAS_FIELD_SZ(context_t, sValMime));
	pCtx->nPktItems = 0;
	pCtx->nPktItemBytes = 0;
	memset(pCtx->sPktFillVal, 0, DAS_FIELD_SZ(context_t, sPktFillVal));
	memset(pCtx->sValTerm, 0, DAS_FIELD_SZ(context_t, sValTerm));
	memset(pCtx->sItemsTerm, 0, DAS_FIELD_SZ(context_t, sItemsTerm));
	pCtx->nTrimReq = -1;   /* -1 is UNSET; 0 would mean trim="false" */
	memset(&(pCtx->aValUnderFlow), 0, DAS_FIELD_SZ(context_t, aValUnderFlow));
	pCtx->nValUnderFlowValid = 0;
}

/* the other sided over the variable parse init/deinit pair */
static void _serial_var_deinit(context_t* pCtx)
{
	pCtx->bInVar = false;  /* a <p> now routes to the dim, not the variable */

	pCtx->pCurAry = NULL;  /* borrowed; the dataset owns the array */

	/* A form that never reached a variable -- an error exit before close-var, or a
	   variable with no generator -- dies here.  Set to NULL on transfer, so a
	   non-NULL here always means unowned. */
	if(pCtx->pVarForm != NULL){
		del_DasForm(pCtx->pVarForm);
		pCtx->pVarForm = NULL;
	}

	/* The header-values codec is the one thing here that holds heap of its own,
	   and this is its only release point. */
	if(DasCodec_isValid( &(pCtx->codecHdrVals)) )
		DasCodec_deInit( &(pCtx->codecHdrVals) );
	memset(&(pCtx->codecHdrVals), 0, DAS_FIELD_SZ(context_t, codecHdrVals));

	DasDesc_clearProps(&(pCtx->varProps));
}

/* ************************************************************************* */

#define _IDX_FOR_DS  false
#define _IDX_FOR_VAR true

static DasErrCode _serial_parseIndex(
	const char* sIndex, int nRank, ptrdiff_t* pMap, bool bInVar,
	const char* sElement
){
	int nGot = 0;
	DasErrCode nRet = das_shape_fromStr(
		sIndex, VARIDX_MAX, bInVar, pMap, &nGot, sElement
	);
	if(nRet != DAS_OKAY)
		return nRet;

	/* Rank is policy, not grammar: a variable's index= has to line up with the
	   dataset it lives in. */
	if(nGot != nRank)
		return das_error(DASERR_SERIAL,
			"The rank of this dataset is %d, but %d index ranges were specified",
			nRank, nGot
		);
	return DAS_OKAY;
}

/* intern= uses the same grammar as index=, with tighter policy: no flag tokens
   (an internal level cannot be borrowed or unused) 

   TODO: Ragged unsupported here in the dataset.c decoder.
      The codec derives its internal ragged count from the byte-run flags `{N}`
      and it only counts at most one. `{N;M}` is not yet supported in the binary
      packet stream.

   pnProduct receives the values-per-item, which is what the codec and the
   <sequence> child count want. */
static DasErrCode _serial_parseIntern(
	const char* sIntern, int nMaxRank, ptrdiff_t* pShape, int* pnRank,
	int* pnProduct, int nPktId
){
	char sWhere[64];
	snprintf(sWhere, sizeof(sWhere), "intern= in dataset ID %02d", nPktId);

	DasErrCode nRet = das_shape_fromStr(
		sIntern, nMaxRank, false /* no ^ or - inside an item */, pShape, pnRank,
		sWhere
	);
	if(nRet != DAS_OKAY)
		return nRet;

	long long nProd = 1;
	for(int i = 0; i < *pnRank; ++i){
		if(pShape[i] == VARIDX_RAGGED)
			return das_error(DASERR_NOTIMP,
				"intern=\"%s\" in dataset ID %02d: ragged internal levels are "
				"not handled yet, every level needs a fixed extent", sIntern, nPktId
			);
		if(pShape[i] < 1)
			return das_error(DASERR_SERIAL,
				"Bad internal extent in intern=\"%s\", dataset ID %02d, levels "
				"are positive counts", sIntern, nPktId
			);
		nProd *= (long long)pShape[i];
		if(nProd > 0x7FFFFFFF)
			return das_error(DASERR_SERIAL,
				"intern=\"%s\" in dataset ID %02d implies more values per item "
				"than can be counted", sIntern, nPktId
			);
	}

	*pnProduct = (int)nProd;
	return DAS_OKAY;
}

/* ****************************************************************************
   Given a fill value as a string, make a fill value an a storable,
   If the fill value string is NULL, just return a default fill from
   value.c 
*/
static DasErrCode _serial_initfill(
	ubyte* pBuf, int nBufLen, das_val_type vt, const char* sFill, bool bText, bool bBool
){
	/* A boolean's fill must end up as a NUMBER in the array, but a user sets it to
	   match what they SEE in the stream.  For a utf8 bool that's the glyph '*' (the
	   display form of fill) -- which can't be parsed as a number -- so interpret it
	   (and the empty default) as the storage fill sentinel.  A numeric fill (e.g.
	   "255" on a binary ubyte bool, where the user sees bytes, not glyphs) parses
	   straight in. */
	if(bBool){
		/* The fill glyphs ('*' canonical, '?' or '-') and the empty default denote
		   the storage fill sentinel.  A numeric fill (e.g. "255" on a binary ubyte
		   bool, where the user sees bytes, not glyphs) parses straight in. */
		bool bGlyph = (sFill != NULL) && (sFill[0] != '\0') && (sFill[1] == '\0') &&
		              ((sFill[0] == '*')||(sFill[0] == '?')||(sFill[0] == '-'));
		if((sFill == NULL)||(sFill[0] == '\0')||bGlyph){
			if(nBufLen < (int)das_vt_size(vt))
				return das_error(DASERR_SERIAL, "Logic error fill value buffer too small");
			memcpy(pBuf, das_vt_fill(vt), das_vt_size(vt));
			return DAS_OKAY;
		}
		/* A lone, non-glyph character is a bad fill -- give a clear message rather
		   than the opaque "could not parse" from the numeric path below. */
		if(sFill[1] == '\0')
			return das_error(DASERR_SERIAL,
				"Invalid boolean fill '%s': use a fill glyph ('*', '?' or '-') or a "
				"numeric sentinel (e.g. \"255\")", sFill);
		return das_value_fromStr(pBuf, nBufLen, vt, sFill);
	}

	/* A string array stores chars, so its fill is a literal byte, NOT a parsed
	   number.  fill=" " is the pad byte 0x20 (the natural fixed-length string pad),
	   and fill="" (the only legal variable-length string fill) leaves the empty/
	   null byte.  The numeric path below is wrong for both: it would default an
	   empty fill to das_vt_fill(vtUByte)==255, and it parse-FAILS on a non-digit
	   like ' ' (sscanf "%hhu"). */
	if(bText){
		if(nBufLen < 1)
			return das_error(DASERR_SERIAL, "Logic error fill value buffer too small");
		/* A string fill is a single per-element pad char, auto-repeated across the
		   field width by DasAry, so only the first character is used.  If the author
		   supplied more, say so -- they put bytes in the stream we're dropping.
		   (Header parsing is not the hot path; warn freely here.) */
		if((sFill != NULL) && (strlen(sFill) > 1))
			daslog_warn_v(
				"Extra fill characters ignored; using '%c' as the repeated string "
				"pad (fill=\"%s\")", sFill[0], sFill
			);
		pBuf[0] = ((sFill != NULL) && (sFill[0] != '\0')) ? (ubyte)sFill[0] : 0;
		return DAS_OKAY;
	}

	if((sFill == NULL)||(sFill[0] == '\0')){
		if(nBufLen >= das_vt_size(vt)){
			memcpy(pBuf, das_vt_fill(vt), das_vt_size(vt));
			return DAS_OKAY;
		}
		else{
			return das_error(DASERR_SERIAL, "Logic error fill value buffer too small");
		}
	}

	// Ugh, now we have to parse the damn thing.
	return das_value_fromStr(pBuf, nBufLen, vt, sFill);
}

/* ****************************************************************************
   Create an empty dataset of known index shape */

static void _serial_onOpenDs(context_t* pCtx, const char** psAttr)
{
	const char* sRank = NULL;
	const char* sName = NULL;
	const char* sGroup = NULL;
	char sIndex[48] = {'\0'};
	const char* sPlot = NULL;
	for(int i = 0; psAttr[i] != NULL; i+=2){
		if(strcmp(psAttr[i],"rank")==0)       sRank=psAttr[i+1];
		else if(strcmp(psAttr[i],"name")==0)  sName=psAttr[i+1];
		else if(strcmp(psAttr[i],"group")==0) sGroup=psAttr[i+1];
		else if(strcmp(psAttr[i],"plot")==0)  sPlot=psAttr[i+1];
		else if((strcmp(psAttr[i],"index")==0)&&(psAttr[i+1][0] != '\0')) 
			strncpy(sIndex, psAttr[i+1], 47);
		else
			daslog_warn_v("Unknown attribute %s in <dataset> ID %02d", psAttr[i], pCtx->nPktId);
	}

	int nRank = 0;
	int id = pCtx->nPktId;
	if((sRank==NULL)||(sscanf(sRank, "%d", &nRank) != 1)){
		pCtx->nDasErr = das_error(DASERR_SERIAL, "Invalid or missing rank attribute for <dataset> %02d", id);
		return;
	}
	if((nRank <= 0)||(nRank >= VARIDX_MAX)){
		pCtx->nDasErr = das_error(DASERR_SERIAL, "Invalid rank (%d) for dataset ID %02d", id);
		return;
	}
	if((sName == NULL)||(strlen(sName) < 1)){
		pCtx->nDasErr = das_error(DASERR_SERIAL, "Missing name attribute for dataset %02d", id);
		return;
	}

	// Save off the expected overall dataset shape
	if(sIndex[0] == '\0'){
		pCtx->nDasErr = das_error(DASERR_SERIAL, "Missing index attribute for dataset %02d", id);
		return;
	}

	DasErrCode nRet;
	if((nRet = _serial_parseIndex(sIndex, nRank, pCtx->aExtShape, _IDX_FOR_DS, "dataset")) != DAS_OKAY){
		pCtx->nDasErr = nRet;
		return;
	}

	/* name= is the dataset's unique id; group= is display-join intent and
	   defaults to the name (a dataset self-groups).  */
	pCtx->pDs = new_DasDs(sName, ((sGroup!=NULL)&&(sGroup[0]!='\0')) ? sGroup : sName, nRank);

	if((sPlot!=NULL)&&(sPlot[0]!='\0'))
		DasDesc_setStr((DasDesc*)(pCtx->pDs), "plot", sPlot);
	return;

}

static void _serial_onOpenProp(context_t* pCtx, const char** psAttr){

	if(pCtx->nDasErr != DAS_OKAY)
		return;

	pCtx->bInProp = true;

	pCtx->sPropSep[0] = '\0';
	strncpy(pCtx->sPropType, "string", _TYPE_BUF_SZ-1);
	for(int i = 0; psAttr[i] != NULL; i+=2){
		if(strcmp(psAttr[i],"type") == 0)
			strncpy(pCtx->sPropType, psAttr[i+1],_TYPE_BUF_SZ-1);
		else if(strcmp(psAttr[i],"name") == 0)
			strncpy(pCtx->sPropName, psAttr[i+1],_NAME_BUF_SZ-1);
		else if(strcmp(psAttr[i],"units") == 0)
			strncpy(pCtx->sPropUnits, psAttr[i+1],_UNIT_BUF_SZ-1);
		/* term is the schema spelling for the stringArray item terminator; sep is
		   accepted as a compatibility alias. */
		else if((strcmp(psAttr[i],"term") == 0)||(strcmp(psAttr[i],"sep") == 0))
			strncpy(pCtx->sPropSep, psAttr[i+1], sizeof(pCtx->sPropSep)-1);
		/* Liberal on input: warn, don't reject, on attributes we don't model */
		else{
			const char* sEl = (pCtx->pCurDim == NULL) ? "dataset" : (
				(pCtx->pCurDim->dtype == DASDIM_DATA) ? "data" : "coord"
			);
			char sBuf[64] = {'\0'};
			if(pCtx->pCurDim == NULL)
				snprintf(sBuf, 63, " ID %02d", pCtx->nPktId);
			else
				snprintf(sBuf, 63, " '%s' in dataset ID %02d", DasDim_id(pCtx->pCurDim), pCtx->nPktId);
			daslog_warn_v(
				"Ignoring unknown property attribute '%s' in properties for <%s>%s",
				psAttr[i], sEl, sBuf
			);
		}
	}
}

/* ****************************************************************************
   Making a dimension inside a dataset 
*/
static void _serial_onOpenDim(
	context_t* pCtx, const char* sDimType, const char** psAttr
){

	if(pCtx->nDasErr != DAS_OKAY)
		return;

	enum dim_type dt = DASDIM_UNK;

	int id = pCtx->nPktId;

	if(strcmp(sDimType, "coord") == 0) dt = DASDIM_COORD;
	else if (strcmp(sDimType, "data") == 0) dt = DASDIM_DATA;
	else{
		pCtx->nDasErr = das_error(DASERR_SERIAL, "Unknown physical dimension type '%s'", sDimType);
		return;
	}

	const char* sName = NULL;
	const char* sPhysDim = NULL;
	char sAxis[48] = {'\0'};
	char sAnnot[48] = {'\0'};

	for(int i = 0; psAttr[i] != NULL; i+=2){
		if(strcmp(psAttr[i],"physDim")==0)     sPhysDim = psAttr[i+1];
		else if(strcmp(psAttr[i],"name")==0)   sName    = psAttr[i+1];
		else if(strcmp(psAttr[i],"axis")==0){ 
			if(psAttr[i+1][0] != '\0') strncpy(sAxis, psAttr[i+1], 47);
		}
		else if(strcmp(psAttr[i],"annotation")==0){
			if(psAttr[i+1][0] != '\0') strncpy(sAnnot, psAttr[i+1], 47);
		} 
		else
			daslog_warn_v(
				"Unknown attribute %s in <%s> for dataset ID %02d", psAttr[i], sDimType, id
			);
	}

	/* freak about about missing items */
	if(sPhysDim == NULL){
		pCtx->nDasErr = das_error(DASERR_SERIAL, 
			"Attribute \"physDim\" missing for %s groups in dataset ID %d", 
			sDimType, id
		);
		return;
	}

	/* Assign name to missing physDims */
	if(sPhysDim[0] == '\0')
		sPhysDim = "none";
	
	/* 
	if((dt == DASDIM_COORD) && (sAxis[0] == '\0') && (sAnnot[0] == '\0')){
		pCtx->nDasErr = das_error(DASERR_SERIAL, 
			"Both \"axis\" and \"annotation\" missing for coordinate dimension %s in dataset ID %d", 
			sPhysDim, id
		);
		return;
	}
	*/

	/* We have required items, make the dim */
	DasDim* pDim = new_DasDim(sPhysDim, sName, dt, DasDs_rank(pCtx->pDs));

	/* Optional items */
	if(sAxis[0] != '\0'){
		char* sBeg = sAxis;
		char* sEnd = NULL;
		int iAxis = 0;
		while((*sBeg != '\0')&&(iAxis < DASDIM_NAXES)){
			sEnd = strchr(sBeg, ';');
			if(sEnd == NULL) 
				sEnd = strchr(sBeg, '\0');
			else
				*sEnd = '\0';
			if(sBeg == sEnd){
				pCtx->nDasErr = das_error(DASERR_SERIAL, 
					"Empty axis entry in '%s' for element <%s>",sAxis, sDimType
				);
				return;
			}

			pDim->axes[iAxis][0] = sBeg[0];
			if(sEnd - sBeg > 1)
				pDim->axes[iAxis][1] = sBeg[1];

			sBeg = sEnd + 1;
			++iAxis;
		}
		DasDim_primeCoord(pDim, true);
	}
	else if(sAnnot[0] != '\0'){
		strncpy(pDim->axes[0], sAnnot, DASDIM_AXLEN-1);
		DasDim_primeCoord(pDim, false);
	}
	
	DasErrCode nRet = 0;
	if((nRet = DasDs_addDim(pCtx->pDs, pDim)) != DAS_OKAY){
		pCtx->nDasErr = nRet;
		del_DasDim(pDim);
		return;
	}

	((DasDesc*)pDim)->parent = (DasDesc*)pCtx->pDs;
	pCtx->pCurDim = pDim;
}

/* ***************************************************************************** */
/* The <ops> element: kind= names the math, every other attribute is a parameter
   of that kind.  The element hase no children, only attributes which is what
   lets an unrecognized kind ride through as name/value pairs.  See
   schema/das2c_operations.md for the kinds das2C acts on.

   Context refs resolve here, while the stream header is at hand; the auto-intern
   path keeps undeclared frame and surface tokens round-tripping.

   BIG WARNING (inherited from the vector era, still true):
      You want explicit frames in your streams... you really do.  MAGnetometer
      people often provide *cartesian* vectors whose orthogonal unit vectors
      are set by the instantaneous location in a *non-cartesian* frame.  To
      take a magnitude you must know the component system, and it may differ
      from the reference frame. */
static void _serial_onOps(context_t* pCtx, const char** psAttr)
{
	if(pCtx->nDasErr != DAS_OKAY) return;
	if(!pCtx->bInVar){
		pCtx->nDasErr = das_error(DASERR_SERIAL,
			"<ops> outside a variable in dataset ID %02d", pCtx->nPktId);
		return;
	}

	/* Build a formalism from XML attribute pairs, only returns null if 
		a known kind has bad inputs. */
	DasForm* pForm = new_DasForm_pairs(psAttr);
	if(pForm == NULL){
		pCtx->nDasErr = DASERR_SERIAL;
		return;
	}

	del_DasForm(pCtx->pVarForm);
	pCtx->pVarForm = pForm;

	pCtx->bInOps   = true;
	pCtx->bOpsSeen = true;
}

/* *****************************************************************************
   Starting a new variable: scalar, byte run, or composite
*/
static void _serial_onOpenVar(
	context_t* pCtx, const char* sVarElType, const char** psAttr
){
	if(pCtx->bInVar){
		pCtx->nDasErr = das_error(DASERR_SERIAL,
			"Variables can not be nested inside other variables"
		);
		return;
	}

	/* After the nesting guard (which reads the OLD bInVar) and before any
	   attribute below is stored */
	_serial_var_init(pCtx);

	int id = pCtx->nPktId;
	char sIndex[32] = {'\0'};
	const char* sIntern = NULL;

	/* Assume center until proven otherwise */
	strncpy(pCtx->varUse, "center", DASDIM_ROLE_SZ-1);

	/* The structure IS the element name.  <bytes> parks as VS_STRING; the
	   packet encoding refines it (utf8 keeps string, raw/base64 -> blob). */
	if(strcmp(sVarElType, "scalar") == 0)      pCtx->varFam = VS_SCALAR;
	else if(strcmp(sVarElType, "bytes") == 0)  pCtx->varFam = VS_STRING;
	else                                       pCtx->varFam = VS_COMPOSITE;

	bool bBytes = (pCtx->varFam == VS_STRING);

	for(int i = 0; psAttr[i] != NULL; i+=2){
		/* Canonicalize the role here, so the legacy "average" spelling never
		   reaches the data model (same treatment as semantic just below) */
		if(strcmp(psAttr[i], "use") == 0)
			strncpy(pCtx->varUse, das_role_fromStr(psAttr[i+1]), DASDIM_ROLE_SZ-1);

		/* semantic and valType (a legacy alias) both name the value semantic.
		   Canonicalize to the DAS_SEM_* singleton here  */
		else if(
			(strcmp(psAttr[i], "semantic") == 0)||(strcmp(psAttr[i], "valType") == 0)
		){
			if(bBytes){
				pCtx->nDasErr = das_error(DASERR_SERIAL,
					"A <bytes> run carries no semantic; the element name is the "
					"structural family and the encoding tells string from blob, "
					"dataset ID %d", id
				);
				return;
			}
			pCtx->valSemantic = das_sem_fromStr(psAttr[i+1]);
			if((pCtx->valSemantic == NULL)||(pCtx->valSemantic == DAS_SEM_TEXT)||
			   (pCtx->valSemantic == DAS_SEM_BLOB)){
				pCtx->nDasErr = das_error(DASERR_SERIAL,
					"Unknown semantic '%s' in <%s> for dataset ID %d; expected one "
					"of bool, datetime, integer, real (string and blob are the "
					"<bytes> element now)",
					psAttr[i+1], sVarElType, pCtx->nPktId
				);
				return;
			}
		}
		else if(strcmp(psAttr[i], "storage") == 0)
			strncpy(pCtx->valStorage, psAttr[i+1], _VAL_STOREAGE_SZ-1);
		else if(strcmp(psAttr[i], "index") == 0){
			strncpy(sIndex, psAttr[i+1], 31);
			strncpy(pCtx->varIndex, psAttr[i+1], sizeof(pCtx->varIndex)-1);
		}
		else if(strcmp(psAttr[i], "units") == 0)
			pCtx->varUnits = Units_fromStr(psAttr[i+1]);
		else if(strcmp(psAttr[i], "intern") == 0)
			sIntern = psAttr[i+1];

		/* Temporarily ignore values that are running around in wild */
		else
			daslog_warn_v(
				"Unknown attribute %s in <%s> for dataset ID %02d", psAttr[i], sVarElType, id
			);
	}

	if(pCtx->varFam == VS_COMPOSITE){
		if((sIntern == NULL)||(sIntern[0] == '\0')){
			pCtx->nDasErr = das_error(DASERR_SERIAL,
				"<composite> missing its intern attribute in dataset ID %d", id
			);
			return;
		}
		/* At least one external index has to survive, so cap the levels one below
		   the shared limit here; the real budget is checked against the actual
		   external count in _serial_makeVarAry. */
		pCtx->nDasErr = _serial_parseIntern(
			sIntern, VARIDX_MAX - 1, pCtx->aVarIntShape, &(pCtx->nVarIntRank),
			&(pCtx->nVarComps), id
		);
		if(pCtx->nDasErr != DAS_OKAY)
			return;
	}
	else if(sIntern != NULL){
		pCtx->nDasErr = das_error(DASERR_SERIAL,
			"Attribute intern is only valid on <composite>, dataset ID %d", id
		);
		return;
	}

	/* Get the mapping from dataset space to array space */
	ptrdiff_t aVarExtShape[VARIDX_MAX];
	int nRet = _serial_parseIndex(
		sIndex, DasDs_rank(pCtx->pDs), aVarExtShape, _IDX_FOR_VAR, sVarElType
	);
	if(nRet != DAS_OKAY){
		pCtx->nDasErr = nRet;
		return;
	}

	int nDsRank = DasDs_rank(pCtx->pDs);

	/* Validate the var's declared extents against the dataset header, fail if
	   an inconsistency is detected.  Only concrete-vs-concrete is checked; the
	   '-'/'^'/'*' sentinels defer to the dataset by design. */
	for(int i = 0; i < nDsRank; ++i){
		if((pCtx->aExtShape[i] >= 0) && (aVarExtShape[i] >= 0)
		   && (aVarExtShape[i] != pCtx->aExtShape[i])){
			pCtx->nDasErr = das_error(DASERR_SERIAL,
				"Index mismatch in <%s> of dimension '%s' (dataset ID %d): the variable "
				"declares extent %td at index %d where the dataset header declares %td",
				sVarElType, DasDim_id(pCtx->pCurDim), pCtx->nPktId,
				aVarExtShape[i], i, pCtx->aExtShape[i]
			);
			return;
		}
	}

	/* Make the var map, insure all unused index positions are set as unused */
	int j = 0;
	for(int i = 0; i < VARIDX_MAX; ++i){
		if((i < nDsRank) &&(aVarExtShape[i] != VARIDX_UNUSED)){
			pCtx->aVarMap[i] = j;
			j++;
		}
		else{
			pCtx->aVarMap[i] = VARIDX_UNUSED;
		}
		pCtx->aVarExtShape[i] = (i < nDsRank) ? aVarExtShape[i] : VARIDX_UNUSED;
	}

	/* Only a sequence may borrow ('^'); a stored var owns its extent.  Flag it here
	   so the array build (_serial_makeVarAry) can reject a '^' on a values/packet var. */
	pCtx->bVarBorrows = false;
	for(int i = 0; i < nDsRank; ++i)
		if(aVarExtShape[i] == VARIDX_BORROW) pCtx->bVarBorrows = true;

	/* A composite carries one internal index per intern= level; a byte run carries
	   exactly one (the byte number); a scalar carries none. */
	if(pCtx->varFam == VS_SCALAR)
		pCtx->varIntRank = 0;
	else if(pCtx->varFam == VS_COMPOSITE)
		pCtx->varIntRank = pCtx->nVarIntRank;
	else
		pCtx->varIntRank = 1;

	if(pCtx->varUse[0] == '\0')  /* Default to a usage of 'center' */
		strncpy(pCtx->varUse, "center", DASDIM_ROLE_SZ-1);

	if((pCtx->valSemantic == NULL)&&(!bBytes)){
		pCtx->nDasErr = das_error(DASERR_SERIAL,
			"Attribute 'semantic' not provided for <%s> in dataset ID %d", sVarElType, pCtx->nPktId
		);
		return;
	}
	/* Absent units means dimensionless */
	if(pCtx->varUnits == NULL)
		pCtx->varUnits = UNIT_DIMENSIONLESS;

	/* Calendar-representable units (an epoch: TT2000, t1970, ...) need to 
	   match with datetime semantics */
	if(Units_haveCalRep(pCtx->varUnits) && (pCtx->valSemantic != DAS_SEM_DATE)){
		pCtx->nDasErr = das_error(DASERR_SERIAL,
			"Contradictory <%s> in dataset ID %d: units '%s' are a calendar epoch "
			"but semantic is '%s', expected 'datetime'", sVarElType, pCtx->nPktId,
			Units_toStr(pCtx->varUnits), pCtx->valSemantic
		);
		return;
	}

	pCtx->bInVar = true;
}


/* ************************************************************************** */
/* Create a sequence item */
static void _serial_onSequence(context_t* pCtx, const char** psAttr)
{
	const char* sMin = "0";
	const char* sInter = NULL;

	if(pCtx->nDasErr != DAS_OKAY) /* Stop parsing if hit an error */
		return;

	pCtx->varGenKind = gtSeq;

	for(int i = 0; psAttr[i] != NULL; i+=2){
		if(strcmp("minval", psAttr[i]) == 0)
			sMin = psAttr[i+1];
		else if(strcmp("interval", psAttr[i]) == 0)
			sInter = psAttr[i+1];
		else if((strcmp("repeat", psAttr[i]) == 0)||(strcmp("repetitions", psAttr[i]) == 0))
			/* Removed from the v3.0 schema 2026-07-21; deferred to v3.1 (see
			   notes/das3_roadmap.md#future).  Error out because ignoring it would
			   silently change the sequence's values. */
			pCtx->nDasErr = das_error(DASERR_SERIAL,
				"In <sequence> for dataset ID %d, '%s' is not part of das v3.0 "
				"(deferred to v3.1)", pCtx->nPktId, psAttr[i]
			);
		else
			daslog_warn_v(
				"Unknown attribute %s in <sequence> for dataset ID %02d", psAttr[i], pCtx->nPktId
			);
	}

	if((sInter == NULL)||(sInter[0] == '\0')){
		pCtx->nDasErr = das_error(DASERR_SERIAL, "Interval not provided for <sequence> in "
			"dataset ID %d", pCtx->nPktId
		);
		return;
	}

	/* For sequences, pick a storage type if none given */
	if(pCtx->valStorage[0] == '\0'){
		/* Pick a default based on the semantic */
		if(pCtx->valSemantic == DAS_SEM_REAL)
			strncpy(pCtx->valStorage, "double", _VAL_STOREAGE_SZ);
		else if(pCtx->valSemantic == DAS_SEM_INT)
			strncpy(pCtx->valStorage, "long", _VAL_STOREAGE_SZ);
		else if(pCtx->valSemantic == DAS_SEM_BOOL)
			strncpy(pCtx->valStorage, "byte", _VAL_STOREAGE_SZ);
		else if(pCtx->valSemantic == DAS_SEM_DATE){
			/* UTC takes the broken-down struct so that minval can be an ISO
			   string; epoch units are plain numbers of their own kind */
			if(pCtx->varUnits == UNIT_TT2000)
				strncpy(pCtx->valStorage, "long", _VAL_STOREAGE_SZ);
			else if(pCtx->varUnits == UNIT_UTC)
				strncpy(pCtx->valStorage, "struct", _VAL_STOREAGE_SZ);
			else
				strncpy(pCtx->valStorage, "double", _VAL_STOREAGE_SZ);
		}
		else{
			pCtx->nDasErr = das_error(DASERR_SERIAL,
				"A <sequence> needs a numeric semantic, dataset ID %d", pCtx->nPktId
			);
			return;
		}
	}

	/* Item type can't be set when the variable opens because we could bubble it
	 * up from the packet description */
	pCtx->varItemType = das_vt_fromStr(pCtx->valStorage);

	DasErrCode nRet;
	if((nRet = das_value_fromStr(pCtx->aSeqMin, _VAL_SEQ_CONST_SZ, pCtx->varItemType, sMin)) != 0){
		das_error(DASERR_SERIAL, "Could not convert sequence minval string '%s' to a value", sMin);
		pCtx->nDasErr = nRet;
		return;
	}

	/* The interval is a ';'-separated list, one slope per dependent index (e.g.
	   "16;0.125" for a sequence that runs over two indices).  Pack the slopes
	   contiguously at the item stride, the layout DasGenSeq expects,
	   and insure the slope count == the used index count, which is usually one. */
	/* Count external dependent indices only.  A vector also marks the component index
	   in aVarMap (at index DasDs_rank); that is an internal index, not a sequence
	   dependency, so bound the count to the dataset rank. */
	int nUsed = 0;
	int nDsRank = DasDs_rank(pCtx->pDs);
	for(int i = 0; i < nDsRank; ++i) if(pCtx->aVarMap[i] >= 0) ++nUsed;

	/* etTime slopes are stored as DOUBLES in seconds (the affine rule at the
	   storage layer); every other element keeps its own stride */
	das_val_type vtSlope = (pCtx->varItemType == vtTime) ? vtDouble : pCtx->varItemType;
	size_t uItem = das_vt_size(vtSlope);

	/* Parse the interval list.  Conservative emit is FULL-RANK: one ';'-separated
	   slot per DATASET index, positionally locked to `index` -- a `-` at every
	   degenerate position, a slope at every dependent one.  Also accepted (liberal
	   in): a SHORTHAND single slope, valid only when the sequence depends on exactly
	   ONE index.  Slopes pack into aSeqInter in DEPENDENCY order (matching how
	   DasGenSeq reads M[k]). */
	char aTok[VARIDX_MAX][64];
	int nTok = 0;
	const char* pBeg = sInter;
	while(*pBeg != '\0'){
		if(nTok >= VARIDX_MAX){
			pCtx->nDasErr = das_error(DASERR_SERIAL,
				"Too many interval components in <sequence> for dataset ID %d", pCtx->nPktId
			);
			return;
		}
		const char* pEnd = pBeg;
		while((*pEnd != '\0')&&(*pEnd != ';')) ++pEnd;
		int nComp = (int)(pEnd - pBeg);
		if(nComp >= (int)sizeof(aTok[0])) nComp = (int)sizeof(aTok[0]) - 1;
		memcpy(aTok[nTok], pBeg, nComp);
		aTok[nTok][nComp] = '\0';
		++nTok;
		pBeg = (*pEnd == ';') ? pEnd + 1 : pEnd;
	}

	if(nTok == nDsRank){
		/* Full-rank, `-`-aligned to `index`: a slope at each dependent position, `-`
		   at each degenerate one.  A number where index says `-` (or vice versa) is a
		   contradiction, not absorbed. */
		int k = 0;
		for(int i = 0; i < nDsRank; ++i){
			bool bDep  = (pCtx->aVarMap[i] >= 0);
			bool bDash = (strcmp(aTok[i], "-") == 0);
			if(bDep == bDash){
				pCtx->nDasErr = das_error(DASERR_SERIAL,
					"<sequence> interval is not aligned with index at position %d: a "
					"slope must sit at each dependent index and '-' at each degenerate "
					"one, dataset ID %d", i, pCtx->nPktId
				);
				return;
			}
			if(bDep){
				if((nRet = das_value_fromStr(
					pCtx->aSeqInter + (size_t)k * uItem, _VAL_SEQ_CONST_SZ,
					vtSlope, aTok[i]
				)) != 0){
					das_error(DASERR_SERIAL,
						"Could not convert sequence interval slope '%s' to a value", aTok[i]
					);
					pCtx->nDasErr = nRet;
					return;
				}
				++k;
			}
		}
	}
	else if(nTok == nUsed){
		/* Compact (liberal in, being phased out for full-rank): one slope per
		   DEPENDENT index in dependency order, no `-`.  Covers the single-slope
		   shorthand (nUsed == 1) and pre-full-rank multi-index streams during the
		   transition. */
		for(int k = 0; k < nTok; ++k){
			if(strcmp(aTok[k], "-") == 0){
				pCtx->nDasErr = das_error(DASERR_SERIAL,
					"'-' is not allowed in a compact <sequence> interval; use the "
					"full-rank ('-'-aligned to index) form, dataset ID %d", pCtx->nPktId
				);
				return;
			}
			if((nRet = das_value_fromStr(
				pCtx->aSeqInter + (size_t)k * uItem, _VAL_SEQ_CONST_SZ,
				vtSlope, aTok[k]
			)) != 0){
				das_error(DASERR_SERIAL,
					"Could not convert sequence interval slope '%s' to a value", aTok[k]
				);
				pCtx->nDasErr = nRet;
				return;
			}
		}
	}
	else{
		pCtx->nDasErr = das_error(DASERR_SERIAL,
			"<sequence> interval must be full-rank ('-'-aligned to index, %d slots) or "
			"compact (one slope per dependent index, %d slots); got %d slot(s), "
			"dataset ID %d", nDsRank, nUsed, nTok, pCtx->nPktId
		);
		return;
	}

	/* Inside a <composite>, this <sequence> is one component.  Stash its parsed
	   intercept and slopes into the next component slot; onCloseVar hands the
	   set to new_DasGenSeqN.  The scratch is sized by the composite, since there
	   must be one <sequence> per internal cell. */
	if(pCtx->nVarComps > 0){
		if(pCtx->nSeqSeen >= pCtx->nVarComps){
			pCtx->nDasErr = das_error(DASERR_SERIAL,
				"More <sequence> elements than the %d component(s) the composite "
				"declares, dataset ID %d", pCtx->nVarComps, pCtx->nPktId
			);
			return;
		}
		if(pCtx->nSeqCap < pCtx->nVarComps){
			free(pCtx->pSeqMinC);
			free(pCtx->pSeqInterC);
			pCtx->pSeqMinC   = (ubyte*)calloc(pCtx->nVarComps, _VAL_SEQ_CONST_SZ);
			pCtx->pSeqInterC = (ubyte*)calloc(pCtx->nVarComps, VARIDX_MAX * _VAL_SEQ_CONST_SZ);
			pCtx->nSeqCap = pCtx->nVarComps;
		}
		memcpy(pCtx->pSeqMinC + (size_t)pCtx->nSeqSeen * _VAL_SEQ_CONST_SZ,
		       pCtx->aSeqMin, _VAL_SEQ_CONST_SZ);
		memcpy(pCtx->pSeqInterC + (size_t)pCtx->nSeqSeen * VARIDX_MAX * _VAL_SEQ_CONST_SZ,
		       pCtx->aSeqInter, VARIDX_MAX * _VAL_SEQ_CONST_SZ);
		++pCtx->nSeqSeen;
	}
}

/* ************************************************************************** */
/* Assuming enough info about the variable is setup, make an array */

#define NO_FILL  false
#define SET_FILL true

static DasErrCode _serial_makeVarAry(context_t* pCtx, bool bHandleFill)
{
	/* Okay, make an array to hold the values since this is packet data */
	assert(pCtx->pCurAry == NULL);

	/* A '^' (borrowed extent) is only valid on a <sequence>; this builds a STORED
	   var (values/packet), which owns its extent.  Guard verified 2026-07-24. */
	if(pCtx->bVarBorrows)
		return das_error(DASERR_SERIAL,
			"A borrowed extent '^' is only valid on a <sequence>; variable %s:%s in "
			"dataset ID %d is a stored var and must declare its own size",
			DasDim_id(pCtx->pCurDim), pCtx->varUse, pCtx->nPktId
		);
	char sAryId[64] = {'\0'};

	snprintf(sAryId, 63, "%s_%s", pCtx->varUse, DasDim_id(pCtx->pCurDim));

	/* Determine the array indexes from the variable indexes */
	size_t aShape[VARIDX_MAX];
	int nAryRank = 0; /* <-- current array index then bumped to the rank */
	int nDsRank = DasDs_rank(pCtx->pDs);
	for(int i = 0; i < nDsRank; ++i){
		if(pCtx->aVarMap[i] == VARIDX_UNUSED)
			continue;

		/* if(pCtx->aExtShape[pCtx->aVarMap[i]] == VARIDX_RAGGED){ */

		if(pCtx->aExtShape[i] == VARIDX_RAGGED){
			aShape[ pCtx->aVarMap[i] ] = 0;
		}
		else{
			/* if(pCtx->aExtShape[pCtx->aVarMap[i]] <= 0){ */

			if(pCtx->aExtShape[i] <= 0){
				return das_error(DASERR_SERIAL,
					"Invalid array map for variable %s:%s in dataset id %d",
					DasDim_id(pCtx->pCurDim), pCtx->varUse, pCtx->nPktId
				);
			}
			/* aShape[nAryRank] = pCtx->aExtShape[pCtx->aVarMap[i]]; */
			aShape[ pCtx->aVarMap[i] ] = pCtx->aExtShape[i];
		}

		++nAryRank;
	}

	/* Force first array index to "undefined" so that streaming always works */
	/* TODO: For speed we should use pre-allocated arrays, but that would 
	         required DasAry_putIn to handle index rolling, which it doesn't
	         right now */
	aShape[0] = 0;

	/* The <bytes> family refines here, when the packet encoding is known:
	   utf8 keeps string, raw/base64 is a blob */
	if((pCtx->varFam == VS_STRING)&&(pCtx->sValEncType != NULL)&&
	   (pCtx->sValEncType != DAS_ENC_UTF8))
		pCtx->varFam = VS_BLOB;

	das_val_type vt = vtUnknown;
	if(pCtx->varFam == VS_STRING)      vt = vtText;
	else if(pCtx->varFam == VS_BLOB)   vt = vtByteSeq;
	else{
		if(pCtx->valStorage[0] != '\0'){
			vt = das_vt_fromStr(pCtx->valStorage);
		}
		/* that didn't work, try using the val semantic + encoding */
		if(vt == vtUnknown){
			vt = das_vt_store_type(pCtx->sValEncType, pCtx->nPktItemBytes, pCtx->valSemantic);

			if(vt == vtUnknown){
				return das_error(DASERR_SERIAL,
					"Attribute 'storage' missing for non-string values encoded "
					"as text for variable %s:%s in dataset ID %d",
					DasDim_id(pCtx->pCurDim), pCtx->varUse, pCtx->nPktId
				);
			}
		}
	}

	/* Dealing with internal structure */
	uint32_t uFlags = 0;

	if(pCtx->varIntRank > 0){

		/* DasAry keeps ONE index list capped at VARIDX_MAX, shared between the
		   external and internal halves (Dude's ruling 2026-07-29).  Check before
		   writing: a deep intern= on a high rank dataset would otherwise run off
		   the end of aShape. */
		if((nAryRank + pCtx->varIntRank) > VARIDX_MAX)
			return das_error(DASERR_SERIAL,
				"Variable %s:%s in dataset ID %d needs %d external plus %d internal "
				"indices; DasAry shares a limit of %d between them",
				DasDim_id(pCtx->pCurDim), pCtx->varUse, pCtx->nPktId, nAryRank,
				pCtx->varIntRank, VARIDX_MAX
			);

		/* Internal structure due to composites: one array dimension per level, so
		   intern="3;3" adds two.  The values arrive row major, which is the order
		   DasAry_putIn fills them. */
		if(pCtx->varFam == VS_COMPOSITE){
			for(int i = 0; i < pCtx->nVarIntRank; ++i){
				aShape[nAryRank] = (size_t)pCtx->aVarIntShape[i];
				++nAryRank;
			}
		}
		else{
			/* Internal structure must be due to text or byte strings */
			if(vt == vtByteSeq){
				vt = vtUByte;
				aShape[nAryRank] = 0;
				++nAryRank;
				uFlags = D2ARY_AS_SUBSEQ;
			}
			else if(vt == vtText){
				vt = vtUByte;
				/* A variable-length string (itemBytes="*") gets a RAGGED char dim and
				   the codec markEnds each value (DASENC_WRAP).  A FIXED-length string
				   (itemBytes="N") instead gets a FIXED char dim of N+1 -- N data chars
				   plus the NUL the AS_STRING codec appends -- so the array auto-wraps
				   every value and no WRAP/markEnd is needed (nor wanted: the fixed-text
				   decode path asserts WRAP is clear). */
				aShape[nAryRank] = (pCtx->nPktItemBytes > 0) ? (pCtx->nPktItemBytes + 1) : 0;
				++nAryRank;
				uFlags = D2ARY_AS_STRING;
			}
			else{
				return das_error(DASERR_SERIAL,
					"Unknown purpose for internal variable indices, not a vector "
					"nor a string nor a byte-string"
				);
			}
		}
	}

	ubyte aFill[DATUM_BUF_SZ] = {0};

	if(bHandleFill){
		bool bTextFill = (pCtx->varFam == VS_STRING);
		bool bBoolFill = (pCtx->valSemantic == DAS_SEM_BOOL);
		int nRet = _serial_initfill(aFill, DATUM_BUF_SZ, vt, pCtx->sPktFillVal, bTextFill, bBoolFill);
		if(nRet != DAS_OKAY)
			return nRet;
	}

	pCtx->pCurAry = new_DasAry(
		sAryId, vt, 0, aFill, nAryRank, aShape, pCtx->varUnits
	);

	if(uFlags > 0)
		DasAry_setUsage(pCtx->pCurAry, uFlags);

	/* Add it to the dataset.  The dataset adds its own reference, so the one
	   made above is released here. pCurAry doesn't own the object anymore */
	DasErrCode nRet = DasDs_addAry(pCtx->pDs, pCtx->pCurAry);
	dec_DasAry(pCtx->pCurAry);
	if(nRet != DAS_OKAY){
		pCtx->pCurAry = NULL;   /* nothing holds it now */
		return nRet;
	}

	return DAS_OKAY;
}

/* ************************************************************************** */
/* Save the info needed to make a packet data encoder/decoder */

/* numItems vs itemBytes, the distinction that trips everyone (true today,
 * independent of any future composite types): numItems counts user-facing
 * elements in the packet field (the lines a client would plot); itemBytes is the
 * width of one element.  An 11-byte fixed string is numItems=1 itemBytes=11 (one
 * value, 11 wide), not numItems=11.  A 3-component vector is numItems=3
 * itemBytes=8 (three values).  Blobs follow the string rule: one value of fixed
 * or variable width; a blob is just a string you read in a hex editor instead of
 * a text editor, so text vs blob is semantic, not structural. */
static void _serial_onPacket(context_t* pCtx, const char** psAttr)
{

	if(pCtx->nDasErr != DAS_OKAY)  /* If an error condition is set, stop processing elements */
		return;

	pCtx->varGenKind = gtArray;

	/* Only work with fixed types for now */
	int nReq = 0x0;      /* 1 = has num items, 2 = has encoding, 4 = has item bytes */

	int nValTermStat = 0x0;     /* 0x1 = needs a terminator       */
	int nItemsTermStat = 0x0;   /* 0x2 = has the terminator array */

	for(int i = 0; psAttr[i] != NULL; i+=2){

		if(strcmp(psAttr[i], "numItems") == 0){
			if(psAttr[i+1][0] == '*'){
				pCtx->nPktItems = -1;
				nItemsTermStat = 0x1;
			}
			else{
				if(sscanf(psAttr[i+1], "%d", &(pCtx->nPktItems)) != 1){
					pCtx->nDasErr = das_error(DASERR_SERIAL, 
						"Error parsing 'numItems=\"%s\"' in <packet> for dataset ID %02d",
						psAttr[i+1], pCtx->nPktId 
					);
					return;
				}
			}
			nReq |= 0x1;
			continue;
		}
		if(strcmp(psAttr[i], "encoding")==0){
			pCtx->sValEncType = das_enc_fromStr(psAttr[i+1]);
			if(pCtx->sValEncType == NULL){
				pCtx->nDasErr = das_error(DASERR_SERIAL,
					"Unknown encoding '%s' in <packet> for dataset ID %d; expected one of "
					"byte, ubyte, BEint, BEuint, BEreal, LEint, LEuint, LEreal, utf8, blob, base64",
					psAttr[i+1], pCtx->nPktId
				);
				return;
			}
			nReq |= 0x2;
			continue;
		}
		/* embedded= is the extension-codec trigger (MIME-valued); "mime" was
		   its pre-release spelling, accepted inbound */
		if((strcmp(psAttr[i], "embedded") == 0)||(strcmp(psAttr[i], "mime") == 0)){
			strncpy(pCtx->sValMime, psAttr[i+1], _VAL_MIME_SZ-1);
			continue;
		}
		if(strcmp(psAttr[i], "itemBytes") == 0){
			if(psAttr[i+1][0] == '*'){
				pCtx->nPktItemBytes = -1;
				nValTermStat = 0x1;
			}
			else if(sscanf(psAttr[i+1], "%d", &(pCtx->nPktItemBytes)) != 1){
				pCtx->nDasErr = das_error(DASERR_SERIAL, 
					"Error parsing 'itemBytes=\"%s\"' in <packet> for dataset ID %02d",
					psAttr[i+1], pCtx->nPktId 
				);
				return;
			}
			nReq |= 0x4;
			continue;
		}
		if(strcmp(psAttr[i], "fill") == 0){
			strncpy(pCtx->sPktFillVal, psAttr[i+1], _VAL_FILL_SZ-1);
			continue;
		}
		/* valTerm is the schema spelling for the value terminator (and the long-time
		   TRACERS name); valSep is accepted as a compatibility alias.  Liberal input:
		   the reader takes either, the writer emits valTerm. */
		if((strcmp(psAttr[i], "valTerm") == 0)||(strcmp(psAttr[i], "valSep") == 0)){
			if(strlen(psAttr[i+1]) != 1){
				pCtx->nDasErr = das_error(DASERR_SERIAL,
					"Error parsing '%s=\"%s\" in <packet> for dataset ID %02d."
					" Expected a 1-byte long string", psAttr[i], psAttr[i+1], pCtx->nPktId
				);
				return;
			}
			strncpy(pCtx->sValTerm, psAttr[i+1], _VAL_TERM_SZ-1);
			nValTermStat |= 0x2;
			continue;
		}
		/* idxTerm is the schema spelling for the per-index run terminator; idxSep and
		   itemsTerm are accepted as compatibility aliases. */
		if((strcmp(psAttr[i], "idxTerm") == 0)||(strcmp(psAttr[i], "idxSep") == 0)
		   ||(strcmp(psAttr[i], "itemsTerm") == 0)){
			strncpy(pCtx->sItemsTerm, psAttr[i+1], _VAL_TERM_SZ-1);
			nItemsTermStat |= 0x2;
			continue;
		}
		/* trim toggles the default-on left+right trim of each value (var-width utf8).
		   Liberal read (das_str2bool: first letter, T/t/1/Y -> true, F/f/0/N ->
		   false); a value it can't read is a mis-authored stream. */
		if(strcmp(psAttr[i], "trim") == 0){
			bool bTrim;
			if(!das_str2bool(psAttr[i+1], &bTrim)){
				pCtx->nDasErr = das_error(DASERR_SERIAL,
					"Error parsing 'trim=\"%s\"' in <packet> for dataset ID %02d; expected"
					" a boolean (true/false)", psAttr[i+1], pCtx->nPktId
				);
				return;
			}
			pCtx->nTrimReq = bTrim ? 1 : 0;
			continue;
		}
	}

	/* Check to see if all needed attributes were provided */
	if(nReq != 0x7){
		pCtx->nDasErr = das_error(DASERR_SERIAL,
			"Error parsing <packet> for dataset ID %02d, one of the required attributes"
			" 'encoding', 'numItems', or 'itemBytes' is missing.", pCtx->nPktId
		);
	}

	/* A variable-length value (itemBytes="*") with no explicit valSep defaults to
	   N whitespace chars as one separator(DASENC_EAT_SPACE, set in DasCodec_init).
	   An explicit non-space valSep (e.g. ';') instead enables empty-item semantics;
	   see _var_text_read. */

	/* A variable-length string represents a missing value structurally -- the empty
	   run between separators -- so its only valid fill is empty (fill=""). Fixed-length
	   strings can have fill, so long as the fill value is as long as given length  */
	if( ((nValTermStat & 0x1) == 0x1) && (pCtx->varFam == VS_STRING)
	    && (pCtx->sPktFillVal[0] != '\0') ){
		pCtx->nDasErr = das_error(DASERR_SERIAL,
			"A variable-length string (itemBytes=\"*\") may only have an empty fill; "
			"fill=\"%s\" is not allowed in <packet> for %s:%s in dataset ID %02d",
			pCtx->sPktFillVal, DasDim_id(pCtx->pCurDim), pCtx->varUse, pCtx->nPktId
		);
	}

	/* A variable item-count run with no idxSep is legal when it is the LAST run in
	   the packet -- end-of-packet is its natural boundary.  Being a SAX parser we
	   can't tell here whether this run is last, so don't reject it.  A variable
	   run that turns out NOT to be last is caught at decode time in
	   DasDs_decodeData (the i < nSzEncs-1 guard). */
	(void)nItemsTermStat;

	/* A raw- or base64-encoded value carrying embedded= is an embedded format
	   (png, ...) that needs a codec extension to decode; the two encodings are
	   just raw or base64, so both are undecodable without the codec.  A bare
	   raw or base64 section (no embedded=) is pass-through and needs none. */
	if(((pCtx->sValEncType == DAS_ENC_BLOB)||(pCtx->sValEncType == DAS_ENC_BASE64))
	   && (pCtx->sValMime[0] != '\0') && !das_codex_supported(pCtx->sValMime)){

		/* Default: fail loud HERE, with the mime in hand, rather than at the
		   cryptic storage guard downstream in the generator. */
		if(!pCtx->bEmbedAsBytes){
			pCtx->nDasErr = das_error(DASERR_SERIAL,
				"Variable %s:%s in dataset ID %02d is an embedded '%s'; no codec "
				"extension is registered to decode it (pass the reader's "
				"embed-as-bytes option to recover it as opaque bytes)",
				DasDim_id(pCtx->pCurDim), pCtx->varUse, pCtx->nPktId, pCtx->sValMime
			);
			return;
		}

		/* Opt-in recovery: keep only what one blob-per-item can carry.
		   A numItems="1" blob is one rank-1 item per record, so it can depend only
		   on the stream index. 
		   TODO: handle a multi-blob (multi item) packet */
		if(pCtx->nPktItems != 1){
			pCtx->nDasErr = das_error(DASERR_SERIAL,
				"Cannot flatten embedded '%s' in variable %s:%s (dataset ID %02d): "
				"only numItems=\"1\" embedded formats are supported",
				pCtx->sValMime, DasDim_id(pCtx->pCurDim), pCtx->varUse, pCtx->nPktId
			);
			return;
		}

		/* Record the declared decoded morphology as provenance BEFORE we overwrite
		   it -- these are preserved, not invented, so storage is stamped only when
		   the stream actually declared it. */
		DasDesc_setStr(&(pCtx->varProps), "embeddedMime",     pCtx->sValMime);
		DasDesc_setStr(&(pCtx->varProps), "embeddedIndex",    pCtx->varIndex);
		DasDesc_setStr(&(pCtx->varProps), "embeddedSemantic", pCtx->valSemantic);
		if(pCtx->valStorage[0] != '\0')
			DasDesc_setStr(&(pCtx->varProps), "embeddedStorage", pCtx->valStorage);

		daslog_warn_v(
			"Flattening embedded '%s' in variable %s:%s to opaque bytes; index "
			"morphology (%s) is not preserved",
			pCtx->sValMime, DasDim_id(pCtx->pCurDim), pCtx->varUse, pCtx->varIndex
		);

		/* Rewrite the value section to a degenerate per-record byte blob:
		   1) semantic "blob" + cleared storage -> vtByteSeq
		   2) clear the mime -> downstream treats it as a bare pass-through blob
		   3) collapse the inner index map so the var depends on the stream index
		     only (for now index 0 keeps its mapping, all inner indices go unused)
		   4) varIntRank 1 for the ragged internal byte dimension 
		*/
		pCtx->varFam = VS_BLOB;
		memset(pCtx->valStorage, 0, DAS_FIELD_SZ(context_t, valStorage));
		memset(pCtx->sValMime,   0, DAS_FIELD_SZ(context_t, sValMime));
		for(int i = 1; i < VARIDX_MAX; ++i) pCtx->aVarMap[i] = VARIDX_UNUSED;
		pCtx->varIntRank = 1;
		pCtx->bFlattened = true;
	}

	DasErrCode nRet = _serial_makeVarAry(pCtx, SET_FILL);
	if(nRet != DAS_OKAY)
		pCtx->nDasErr = nRet;
}

/* ************************************************************************** */

static void _serial_onOpenVals(context_t* pCtx, const char** psAttr)
{
	if(pCtx->nDasErr != DAS_OKAY)  /* Error flag raised, stop parsing */
		return;

	pCtx->varGenKind = gtArray;

	if(pCtx->bInValues){ // Can't nest values
		pCtx->nDasErr = das_error(DASERR_SERIAL, 
			"<values> element nested in dataset ID %d", pCtx->nPktId
		);
		return;
	}
	pCtx->bInValues = true;
	pCtx->uHdrValsRead = 0;
	assert(pCtx->pCurAry == NULL);

	/* A fixed set of values can't map to a variable length index */
	int nDsRank = DasDs_rank(pCtx->pDs);
	for(int i = 0; i < nDsRank; ++i){
		if(pCtx->aVarMap[i] != VARIDX_UNUSED){
			if(pCtx->aExtShape[i] == VARIDX_RAGGED){
				pCtx->nDasErr = das_error(DASERR_SERIAL,
					"The external shape of variable %s:%s in dataset ID %02d is not "
					"consistent with the shape of the overall dataset.  A fixed set of "
					"values in index %d, can't map to a dataset with a variable length "
					"in index %d.",
					DasDim_id(pCtx->pCurDim), pCtx->varUse, pCtx->nPktId, i, i
				);
				return;
			}
		}
	}


	/* valTerm (or the alias valSep) sets the value terminator for the block; an
	   absent valTerm selects the whitespace default, same as <packet>.  There is
	   no idxTerm here: values extents are fixed and fully declared by index=
	   (the ragged-map check above), so content lines up by count alone.
	   repeat/repetitions were removed from the v3.0 schema 2026-07-21, deferred
	   to v3.1. */
	for(int i = 0; psAttr[i] != NULL; i+=2){
		if((strcmp(psAttr[i], "repeat") == 0)||(strcmp(psAttr[i], "repetitions") == 0)){
			pCtx->nDasErr = das_error(DASERR_SERIAL,
				"In <values> for dataset ID %d, '%s' is not part of das-basic-stream v3.0 ", 
				pCtx->nPktId, psAttr[i]
			);
			return;
		}
		if((strcmp(psAttr[i], "idxTerm") == 0)||(strcmp(psAttr[i], "idxSep") == 0)){
			pCtx->nDasErr = das_error(DASERR_SERIAL,
				"In <values> for dataset ID %d, idxTerm is not das-basic-stream v3.0 "
				"<values> attribute ", pCtx->nPktId
			);
			return;
		}
		if((strcmp(psAttr[i], "valTerm") == 0)||(strcmp(psAttr[i], "valSep") == 0)){
			if(strlen(psAttr[i+1]) != 1){
				pCtx->nDasErr = das_error(DASERR_SERIAL,
					"Error parsing '%s=\"%s\"' in <values> for dataset ID %02d."
					" Expected a 1-byte long string", psAttr[i], psAttr[i+1], pCtx->nPktId
				);
				return;
			}
			strncpy(pCtx->sValTerm, psAttr[i+1], _VAL_TERM_SZ-1);
			continue;
		}
		pCtx->nDasErr = das_error(DASERR_NOTIMP,
			"Attribute '%s' of <values> element not yet supported in dataset ID %d",
			psAttr[i], pCtx->nPktId
		);
		return;
	}

	pCtx->sValEncType = DAS_ENC_UTF8;

	DasErrCode nRet = _serial_makeVarAry(pCtx, NO_FILL);
	if(nRet != DAS_OKAY){
		pCtx->nDasErr = nRet;
		return;
	}

	/* Make an encoder for header values.  You have to provide cSep if you want
		to have null (fill) items in <values> blocks, but no one does this cause
		<values> are typically coordinates, and coordinates are known, usually. */
	nRet = DasCodec_init(
		DASENC_READ, &(pCtx->codecHdrVals), pCtx->pCurAry, pCtx->valSemantic, "utf8",
		DASENC_ITEM_TERM, pCtx->sValTerm[0], pCtx->varUnits, NULL
	);
	if(nRet != DAS_OKAY)
		pCtx->nDasErr = nRet;

	DasCodec_eatSpace(&(pCtx->codecHdrVals), true);
}

/* ************************************************************************** */
/* Switch to various element initialization functions */
static void _serial_xmlElementBeg(void* pUserData, const char* sElement, const char** psAttr)
{
	context_t* pCtx = (context_t*)pUserData;

	if(pCtx->nDasErr != DAS_OKAY)  /* If an error condition is set, stop processing elements */
		return;

	if(strcmp(sElement, "dataset") == 0){
		if(pCtx->pDs != NULL){
			pCtx->nDasErr = das_error(DASERR_SERIAL, "Only one dataset definition allowed per header packet");
			return;
		}
		_serial_onOpenDs(pCtx, psAttr);
		return;
	}

	if(pCtx->pDs == NULL)          /* If the dataset is not defined, nothing can be linked in */
		return;
	
	if((strcmp(sElement, "coord")==0)||(strcmp(sElement, "data")==0)){
		_serial_onOpenDim(pCtx, sElement, psAttr);
		return;
	}
	if(strcmp(sElement, "properties") == 0){
		pCtx->bInPropList = true;
		return;
	}
	if(strcmp(sElement, "p") == 0){
		if(pCtx->bInPropList)
			_serial_onOpenProp(pCtx, psAttr);
		return;
	}
	if((strcmp(sElement, "scalar") == 0)||(strcmp(sElement, "bytes") == 0)||
	   (strcmp(sElement, "composite") == 0)){
		_serial_onOpenVar(pCtx, sElement, psAttr);
		return;
	}
	if(strcmp(sElement, "ops") == 0){
		_serial_onOps(pCtx, psAttr);
		return;
	}
	if(pCtx->bInOps){
		/* <ops> is flat by construction: parameters are attributes, and nesting is
		   what an unrecognized kind could not carry through. */
		pCtx->nDasErr = das_error(DASERR_SERIAL,
			"<ops> takes no child elements, found <%s> in dataset ID %02d",
			sElement, pCtx->nPktId
		);
		return;
	}
	/* dasTelem v0.6 emits <vector> and those streams are live -2026-08-01 */
	if(strcmp(sElement, "vector") == 0){
		pCtx->nDasErr = das_error(DASERR_SERIAL,
			"<vector> is a das2 dialect; a variable's math rides on a flat "
			"<ops kind=\"vector\" frame=\"...\"/> instead.  Regenerate the stream "
			"(dasTelem v0.6 and earlier produce this), dataset ID %02d",
			pCtx->nPktId
		);
		return;
	}
	if(strcmp(sElement, "values") == 0){
		_serial_onOpenVals(pCtx, psAttr);  // Sets value type, starts an array
		return;
	}
	if(strcmp(sElement, "sequence") == 0){
		_serial_onSequence(pCtx, psAttr); // sets value type, saves sequence constants
		return;
	}
	if(strcmp(sElement, "packet") == 0){
		_serial_onPacket(pCtx, psAttr);   // Setss value type, starts an array
		return;
	}

	pCtx->nDasErr = das_error(DASERR_SERIAL, 
		"Unsupported element %s in the definition for dataset ID %02d.",
		sElement, pCtx->nPktId
	);
	return;
}

/* ************************************************************************** */
/* Accumulating data between element tags */

static void _serial_xmlCharData(void* pUserData, const char* sChars, int nLen)
{
	context_t* pCtx = (context_t*)pUserData;

	if(pCtx->nDasErr != DAS_OKAY) /* Error, stop processing */
		return;

	if(pCtx->bInProp){
		/* TODO: Add stripping of beginning and ending whitespace,
		 * possibly at the line level for long properties 
		 */
		
		DasAry* pAry = &(pCtx->aPropVal);
		DasAry_append(pAry, (ubyte*) sChars, nLen);
		return;
	}

	/* The only other character data should come from embedded values */
	if(!pCtx->bInValues)
		return;

	int nUnRead = 0;

	/* If I have underflow from the previous read, complete the one value 
	   and append it.  The previous buffer must have ended before a separator
	   or else we wouldn't be in an underflow condition.  Finish out the 
	   current value, read it then advance the read pointer. */
	if(pCtx->nValUnderFlowValid > 0){
		const char* p = sChars;
		int n = 0;
		while(!isspace(*p) && (*p != '\0') && (n < nLen)){
			++p; ++n;
		}

		if(n > 0){
			if(pCtx->nValUnderFlowValid + n >= (_VAL_UNDER_SZ-1)){
				pCtx->nDasErr = das_error(DASERR_SERIAL, 
					"Parse error: Underflow buffer can't hold %d + %d bytes", 
					pCtx->nValUnderFlowValid, n
				);
				return;
			}
			
			memcpy(pCtx->aValUnderFlow + pCtx->nValUnderFlowValid, sChars, n);
		
			/* Read the underflow buffer then clear it */
			/* TODO:  make DasAry_putAt handle index rolling as well
			 *  (I think this is done now?)
			 */
			/* Decode only what the underflow buffer holds: the bytes saved from
			   the previous chunk plus the n just spliced on.  nLen is the size
			   of the *incoming* chunk and is unrelated to this 64 byte buffer. */
			int nValsRead = 0;
			int nUnderFlowLen = pCtx->nValUnderFlowValid + n;
			nUnRead = DasCodec_decode(
				&(pCtx->codecHdrVals), pCtx->aValUnderFlow, nUnderFlowLen, -1, &nValsRead
			);
			if(nUnRead < 0){
				pCtx->nDasErr = -1 * nUnRead;
				strncpy(pCtx->sErrMsg, "Decoding error in header values", 511);
				return;
			}
			pCtx->uHdrValsRead += nValsRead;
			memset(pCtx->aValUnderFlow, 0, _VAL_UNDER_SZ);
			pCtx->nValUnderFlowValid = 0;

			nLen -= n;
			sChars += n;
		}
	}

	/* Decode as many values as possible from the input */
	/* TODO:  make DasAry_putAt handle index rolling as well */
	int nChunkVals = 0;
	nUnRead = DasCodec_decode(
		&(pCtx->codecHdrVals), (const ubyte*) sChars, nLen, -1, &nChunkVals
	);
	if(nUnRead < 0){
		strncpy(pCtx->sErrMsg, "Decoding error in header values", 511);
		pCtx->nDasErr = -1*nUnRead;
		return;
	}
	pCtx->uHdrValsRead += nChunkVals;

	/* Consuming text without producing a value means the codec could not find a
	   token where one clearly exists.  Left unchecked this only surfaces as a
	   whole-element count shortfall once </values> closes, with nothing to say
	   which text caused it. */
	if((nChunkVals == 0) && ((nLen - nUnRead) > 0)){
		int nSeen = nLen - nUnRead;
		bool bBlank = true;
		for(int i = 0; i < nSeen; ++i){
			if(!isspace((unsigned char)sChars[i])){ bBlank = false; break; }
		}
		if(!bBlank){
			pCtx->nDasErr = das_error(DASERR_SERIAL,
				"Parse error: %d bytes of header values consumed without decoding a "
				"value, near '%.32s'", nSeen, sChars
			);
			return;
		}
	}

	/* Copy unread bytes into the underflow buffer */
	if(nUnRead > 0){
		if(nUnRead > _VAL_UNDER_SZ){
			pCtx->nDasErr = das_error(DASERR_SERIAL, 
				"Parse error: Unread bytes of character data (%d) too large to "
				"fit in underflow buffer (%d)", nUnRead, _VAL_UNDER_SZ
			);
			return;
		}

		memcpy(pCtx->aValUnderFlow, sChars + nLen - nUnRead, nUnRead);
		pCtx->nValUnderFlowValid = nUnRead;
	}
}

/* ************************************************************************** */
/* Closing out properties */

static void _serial_onCloseProp(context_t* pCtx, DasDesc* pDest){
	
	if(pCtx->nDasErr != DAS_OKAY)
		return;

	DasAry* pAry = &(pCtx->aPropVal);
	ubyte uTerm = 0;
	DasAry_append(pAry, &uTerm, 1);  // Null terminate the value string
	size_t uValLen = 0;
	const char* sValue = DasAry_getCharsIn(pAry, DIM0, &uValLen);

	if(pDest == NULL){
		pCtx->nDasErr = das_error(DASERR_SERIAL, "Property element at improper location");
		return;
	}

	DasDesc_flexSet(
		pDest,                                                   // descriptor
		pCtx->sPropType[0] == '\0' ? "string" : pCtx->sPropType, // prop type (string)
		0,                                                       // prop type (code)
		pCtx->sPropName,                                         // prop name
		sValue,                                                  // prop value
		dasprop_unescapeSep(pCtx->sPropSep),                     // wire sep, else guess
		pCtx->sPropUnits[0] == '\0' ? NULL : Units_fromStr(pCtx->sPropUnits),
		3 /* Das3 */
	);

	memset(&(pCtx->sPropType), 0, _TYPE_BUF_SZ);
	memset(&(pCtx->sPropName), 0, _NAME_BUF_SZ);
	memset(&(pCtx->sPropUnits), 0, _UNIT_BUF_SZ);
	pCtx->sPropSep[0] = '\0';
	DasAry_clear(pAry);
}

/* ************************************************************************** */

static void _serial_onCloseVals(context_t* pCtx){
	if(pCtx->nDasErr != DAS_OKAY)
		return;

	pCtx->bInValues = false;

	/* Cross check dataset size against the array size, make sure they match. */

	/* Look over external dimensions.  The var map is hard to keep straight.  
    *
	 *   - The index you on while looping over the var map is the external
	 *     index.
	 *
	 *   - The value in the map is what array index maps to the external 
	 *     index
	 *
	 *   - We don't care about mappings to non-fixed external indices
	 */
	size_t uExpect = 0;
	
	for(int iExt = 0; iExt < VARIDX_MAX; ++iExt){

		if(pCtx->aExtShape[iExt] == VARIDX_UNUSED)
			break;

		if(pCtx->aExtShape[iExt] < 1) continue; /* this external index is variable length */

		if(pCtx->aVarMap[iExt] < 0) continue; /* Array doesn't map to this external index */

		/* Array does map to this external index, get the number of items in this external
		   index */

		if(uExpect == 0) 
			uExpect = pCtx->aExtShape[ iExt ];
		else 
			uExpect *= pCtx->aExtShape[ iExt ];
	}

	/* Now get the array size in any non-internal dimensions */
	ptrdiff_t aShape[VARIDX_MAX] = {0};
	int nExtAryRank = DasAry_shape(pCtx->pCurAry, aShape) - pCtx->varIntRank;

	size_t uHave = 0;
	for(int i = 0; i < nExtAryRank; ++i){
		if(aShape[i] > 0){
			if(uHave == 0)
				uHave = aShape[i];
			else
				uHave *= aShape[i];
		}
	}

	/* uHave comes from the array shape, uHdrValsRead from the codec's own tally.
	   They are arrived at independently, so a disagreement is an internal fault
	   (a dropped index roll, say) and not a malformed stream.  Say which. */
	if(uHave != pCtx->uHdrValsRead){
		pCtx->nDasErr = das_error(DASERR_SERIAL,
			"Internal error: the codec reported %zu header values for variable %s:%s in "
			"dataset ID %02d but the array holds %zu",
			pCtx->uHdrValsRead, DasDim_id(pCtx->pCurDim), pCtx->varUse, pCtx->nPktId, uHave
		);
		return;
	}

	if(uHave != uExpect){
		pCtx->nDasErr = das_error(DASERR_SERIAL,
			"Expected %zu header values for variable %s:%s in dataset ID %02d, read %zu",
			uExpect, DasDim_id(pCtx->pCurDim), pCtx->varUse, pCtx->nPktId, uHave
		);
		return;
	}
	
	/* Looks good from here boss */
}

/* ************************************************************************** */

/* var_seq.c helper: stamp a just-built sequence's per-index extent from the
   dataset's declared shape (no public prototype, declared locally). */
static void _serial_onCloseVar(context_t* pCtx)
{
	if(pCtx->nDasErr != DAS_OKAY) /* Stop processing on error */
		return;

	DasGen* pGen = NULL;
	DasVar* pVar = NULL;
	int nDsRank = DasDs_rank(pCtx->pDs);

	/* datetime scalars are affine whether or not the stream said so; binding
	   the point row explicitly matches the schema's SHOULD */
	if((pCtx->varFam == VS_SCALAR)&&(pCtx->valSemantic == DAS_SEM_DATE)&&
	   !pCtx->bOpsSeen){
		del_DasForm(pCtx->pVarForm);
		pCtx->pVarForm = new_DasFormPoint();
	}

	if(pCtx->varGenKind == gtSeq){
		/* the sequence's declared extents, with the dataset header filling in
		   numbers where the variable deferred */
		ptrdiff_t aExt[VARIDX_MAX];
		for(int i = 0; i < nDsRank; ++i){
			aExt[i] = pCtx->aVarExtShape[i];

			/* A sequence has no storage, so '*' cannot mean ragged on one --
			   there is nothing there to be ragged.  It means what '^' means:
			   the container decides.  Read it that way and say so out loud. */
			if(aExt[i] == VARIDX_RAGGED){
				daslog_info_v(
					"Dataset ID %d, %s:%s index %d: '*' on a sequence reads as "
					"'^', the container's extent", pCtx->nPktId,
					DasDim_id(pCtx->pCurDim), pCtx->varUse, i
				);
				aExt[i] = VARIDX_BORROW;
			}

			/* The hard rail: nothing borrows outside the dataset's own index
			   range.  Wherever the dataset named a number, that number IS the
			   extent.  Only a ragged dataset index leaves the question open,
			   and then it takes a shape model to answer at read time. */
			if((aExt[i] == VARIDX_BORROW)&&(pCtx->aExtShape[i] >= 0))
				aExt[i] = pCtx->aExtShape[i];
		}

		das_val_type vtEl = pCtx->varItemType;
		size_t uElem  = das_vt_size(vtEl);
		size_t uSlope = (vtEl == vtTime) ? sizeof(double) : uElem;

		int nComps = (pCtx->nVarComps > 0) ? pCtx->nVarComps : 1;
		if((pCtx->nVarComps > 0)&&(pCtx->nSeqSeen != pCtx->nVarComps)){
			pCtx->nDasErr = das_error(DASERR_SERIAL,
				"<composite> declares %d component(s) but has %d <sequence> "
				"child(ren), dataset ID %d", pCtx->nVarComps, pCtx->nSeqSeen,
				pCtx->nPktId
			);
			goto NO_CUR_VAR;
		}
		/* expand dependency-ordered slopes into per-external-index slots */
		ubyte* aIcpt  = (ubyte*)calloc(nComps, sizeof(das_time));
		ubyte* aSlope = (ubyte*)calloc((size_t)nComps * VARIDX_MAX, sizeof(das_time));
		for(int c = 0; c < nComps; ++c){
			const ubyte* pMin = (pCtx->nVarComps > 0) ?
				pCtx->pSeqMinC + (size_t)c * _VAL_SEQ_CONST_SZ : pCtx->aSeqMin;
			const ubyte* pInt = (pCtx->nVarComps > 0) ?
				pCtx->pSeqInterC + (size_t)c * VARIDX_MAX * _VAL_SEQ_CONST_SZ : pCtx->aSeqInter;
			memcpy(aIcpt + (size_t)c*uElem, pMin, uElem);
			int k = 0;
			for(int i = 0; i < nDsRank; ++i){
				if(pCtx->aVarMap[i] < 0) continue;
				memcpy(
					aSlope + ((size_t)c*(size_t)nDsRank + (size_t)i)*uSlope,
					pInt + (size_t)k*uSlope, uSlope
				);
				++k;
			}
		}
		pGen = new_DasGenSeqN(
			(das_elem_type)vtEl, nComps, aIcpt, nDsRank, aSlope, aExt
		);
		free(aIcpt);
		free(aSlope);
	}
	else{
		if(pCtx->pCurAry == NULL){
			pCtx->nDasErr = das_error(DASERR_SERIAL,
				"Variable %s:%s in dataset ID %d has no value source",
				DasDim_id(pCtx->pCurDim), pCtx->varUse, pCtx->nPktId
			);
			goto NO_CUR_VAR;
		}
		pGen = new_DasGenAry(pCtx->pCurAry, nDsRank, pCtx->aVarMap);
	}
	if(pGen == NULL){
		pCtx->nDasErr = DASERR_SERIAL;
		goto NO_CUR_VAR;
	}

	/* One class per wire element */
	if(pCtx->varFam == VS_SCALAR){
		pVar = new_DasVar(pGen, pCtx->varUnits, pCtx->pVarForm);
	}
	else if(pCtx->varFam == VS_COMPOSITE){
		pVar = (DasVar*)new_DasVarComp(
			pGen, pCtx->varUnits, pCtx->pVarForm, pCtx->nVarIntRank,
			pCtx->aVarIntShape
		);
	}
	else{
		/* a byte run's one internal index is the byte number */
		pVar = (DasVar*)new_DasVarBytes(
			pGen, pCtx->varUnits, (pCtx->varFam == VS_STRING),
			(pCtx->nPktItemBytes > 0) ? (ptrdiff_t)pCtx->nPktItemBytes
			                          : VARIDX_RAGGED
		);
	}
	DasGen_decRef(pGen);   /* the variable holds the surviving reference */
	pGen = NULL;
	if(pVar == NULL){
		pCtx->nDasErr = DASERR_VAR;
		goto NO_CUR_VAR;
	}

	/* The form was built here, so it is released here. 
	   Variables make copies of forms as needed.  */
	del_DasForm(pCtx->pVarForm);
	pCtx->pVarForm = NULL;

	/* If this is an array var type & it is record varying, add a packet decoder */
	if((pCtx->varGenKind == gtArray)&&(pCtx->pCurAry != NULL)&&
	   (pCtx->aVarMap[0] != VARIDX_UNUSED)){

		DasCodec* pNewCodec = NULL;

		/* the codec still speaks semantic internally; byte runs derive theirs
		   from the structural family (the wire no longer says it) */
		const char* sCodecSem = pCtx->valSemantic;
		if(pCtx->varFam == VS_STRING)    sCodecSem = DAS_SEM_TEXT;
		else if(pCtx->varFam == VS_BLOB) sCodecSem = DAS_SEM_BLOB;

		/* The item COUNT (nPktItems, -1 when variable) is passed straight through to
		   codec registration; the item-BYTES axis picks fixed vs string codec.  A
		   variable count is realized at decode time (DasDs_decodeData calls markEnd
		   to close each ragged record). */
		if(pCtx->nPktItemBytes < 0){

			/* itemBytes="*" is framed two ways: 
			   1. DASENC_ITEM_LEN: native "blob" bytes or base64 encoded carry an
			      in-packet length tag '{N}'.
			   2. DASENC_ITEM_TERM: utf8 is terminator-framed.  Note that cSep is
			      meaningless for the length-prefixed forms, so pass NUL. 
			*/
			bool bLenPfx = (pCtx->sValEncType == DAS_ENC_BLOB)
			            || (pCtx->sValEncType == DAS_ENC_BASE64);
			int16_t nFraming = bLenPfx ? DASENC_ITEM_LEN : DASENC_ITEM_TERM;
			ubyte cSep = bLenPfx ? '\0' : pCtx->sValTerm[0];

			pNewCodec = DasDs_addStringCodec(
				pCtx->pDs, DasAry_id(pCtx->pCurAry), sCodecSem,
				pCtx->sValEncType, nFraming, cSep,
				pCtx->nPktItems, DASENC_READ
			);

		}
		else{
			pNewCodec = DasDs_addFixedCodec(
				pCtx->pDs, DasAry_id(pCtx->pCurAry), sCodecSem,
				pCtx->sValEncType, pCtx->nPktItemBytes, pCtx->nPktItems,
				DASENC_READ
			);
		}
		if(pNewCodec == NULL){
			pCtx->nDasErr = DASERR_SERIAL;
			goto NO_CUR_VAR;
		}

		/* Determine the number of terminators we need. This is the number of non stream
			external indices we have for numItems="*" utf8 encoded data. -- see DasCodec. */
		{
			int nDsRank = DasDs_rank(pCtx->pDs);
			ubyte nExtRagged = 0;
			for(int iExt = 1; iExt < nDsRank; ++iExt){
				if((pCtx->aVarMap[iExt] != VARIDX_UNUSED) && (pCtx->aExtShape[iExt] == VARIDX_RAGGED))
					++nExtRagged;
			}
			pNewCodec->nExtRagged = nExtRagged;

			/* The run walk spans every external index down to the inner-most ragged
			   one; a fixed extent inside the span (the "*;3;*" sandwich) closes by
			   its declared count. */
			int nSpan = 0;
			if(nExtRagged > 0){
				int aRagIdx[VARIDX_MAX];
				int nChk = DasCodec_raggedIndices(pNewCodec, aRagIdx);
				if(nChk < 0){
					pCtx->nDasErr = -1 * nChk;
					goto NO_CUR_VAR;
				}
				nSpan = aRagIdx[nChk - 1];
			}

			/* If the header declared idxTerm run terminators, parse the comma-separated
			   list (with \n \t \r \0 escapes) and load it onto the read codec so the
			   decoder can bound each ragged run. 

			   The list is all-or-nothing: one terminator per ragged index, or one per 
			   for all index runs, even fixed length cases. 

			   An absent idxTerm is legal for binary (index run tags handle the job),
			   and for a ragged rank-2 variable at the last position because the 
			   packet edge ends the run and rolls the next higher index.
			*/
			if(pCtx->sItemsTerm[0] != '\0'){
				char aLvls[VARIDX_MAX];
				ubyte nLvls = 0;
				const char* p = pCtx->sItemsTerm;
				while((*p != '\0') && (nLvls < VARIDX_MAX)){
					char c = *p;
					if(*p == '\\'){
						switch(p[1]){
						case 'n': c = '\n'; break;  case 't': c = '\t'; break;
						case 'r': c = '\r'; break;  case '0': c = '\0'; break;
						default:
							pCtx->nDasErr = das_error(DASERR_SERIAL,
								"Bad escape in idxTerm=\"%s\" for dataset ID %02d",
								pCtx->sItemsTerm, pCtx->nPktId);
							goto NO_CUR_VAR;
						}
						p += 2;
					}
					else ++p;
					aLvls[nLvls++] = c;
					if(*p == ',') ++p;
				}
				if((nLvls != nExtRagged) && (nLvls != nSpan)){
					pCtx->nDasErr = das_error(DASERR_SERIAL,
						"idxTerm=\"%s\" has %hhu terminator(s) but %s:%s needs %hhu (one per "
						"ragged index) or %d (one per index down to the inner-most ragged) "
						"in dataset ID %02d", pCtx->sItemsTerm, nLvls,
						DasDim_id(pCtx->pCurDim), pCtx->varUse, nExtRagged, nSpan,
						pCtx->nPktId);
					goto NO_CUR_VAR;
				}
				DasErrCode nTRet = DasCodec_setIdxTerms(pNewCodec, nLvls, aLvls);
				if(nTRet != DAS_OKAY){ pCtx->nDasErr = nTRet; goto NO_CUR_VAR; }
			}
			else if((nSpan >= 2) && DasCodec_isText(pNewCodec)){
				pCtx->nDasErr = das_error(DASERR_SERIAL,
					"%s:%s in dataset ID %02d is utf8 with a variable-count run structure "
					"%d indexes deep but declares no idxTerm.", DasDim_id(pCtx->pCurDim),
					pCtx->varUse, pCtx->nPktId, nSpan
				);
				goto NO_CUR_VAR;
			}

			/* An explicit trim="..." overrides the codec's default (on for var-width
			   utf8; a no-op elsewhere since only _var_text_read consults it). */
			if(pCtx->nTrimReq >= 0)
				DasCodec_setTrim(pNewCodec, pCtx->nTrimReq == 1);
		}
	}

	/* If any properties were buffered for this variable, copy them over */
	if(DasDesc_length(&(pCtx->varProps)) > 0){
		DasDesc_copyIn((DasDesc*)pVar, &(pCtx->varProps));
	}

	if(!DasDim_addVar(pCtx->pCurDim, pCtx->varUse, pVar)){
		dec_DasVar(pVar);
		pCtx->nDasErr = DASERR_DIM;
		goto NO_CUR_VAR;
	}

	/* Set the parent pointer for the variable */
	((DasDesc*)pVar)->parent = (DasDesc*) pCtx->pCurDim;
	
NO_CUR_VAR:  /* No longer in a var, nor in an array */
	_serial_var_deinit(pCtx);
}

/* ************************************************************************** */

static void _serial_xmlElementEnd(void* pUserData, const char* sElement)
{
	context_t* pCtx = (context_t*)pUserData;

	/* If I've hit an error condition, stop processing stuff */
	if(pCtx->nDasErr != DAS_OKAY)
		return;

	/* Closing properties.  Attach the property to the current:
     Var - If there is one
     Dim - If there is one
     Dataset
	*/
	if(sElement[0] == 'p' && sElement[1] == '\0'){

		DasDesc* pDest;
		if(pCtx->bInVar)
			pDest = &(pCtx->varProps);
		else 
			pDest = pCtx->pCurDim != NULL ? (DasDesc*)pCtx->pCurDim : (DasDesc*)pCtx->pDs;
		_serial_onCloseProp(pCtx, pDest);
		pCtx->bInProp = false;
		return;
	}

	/* Closing property blocks */
	if(strcmp(sElement, "properties") == 0){
		pCtx->bInPropList = false;
		return;
	}

	/* Closing values, not much to do here as values are converted to
	   array entries as character data are read */
	if(strcmp(sElement, "values") == 0){
		_serial_onCloseVals(pCtx);
		return;
	}

	if((strcmp(sElement, "coord")==0)||(strcmp(sElement, "data")==0)){
		/* _serial_onCloseDim(pCtx); */
		pCtx->pCurDim = NULL;
		return;
	}

	if((strcmp(sElement, "scalar")==0)||(strcmp(sElement, "bytes")==0)||
	   (strcmp(sElement, "composite")==0)){
		_serial_onCloseVar(pCtx);
		return;
	}
	if(strcmp(sElement, "ops") == 0){
		pCtx->bInOps = false;
		return;
	}
	/* Nothing to do on the other ones */
}

/* ************************************************************************** */

/** Define a das dataset and all it's constituent parts from an XML header
 * 
 * @param pBuf The buffer to read.  Reading will start with the read point
 *             and will run until DasBuf_remaining() is 0 or the end tag
 *             is found, which ever comes first.
 * 
 * @param pParent The parent descriptor for this data set. This is assumed
 *             to be an object which can hold vector frame definitions.
 * 
 * @param nPktId  The packet's ID within it's parent's array.  My be 0 if
 *             and only if pParent is NULL
 * 
 * @returns A pointer to a new DasDs and all if it's children allocated 
 *          on the heap, or NULL on an error.
 */

DasDs* new_DasDs_xml(DasBuf* pBuf, DasDesc* pParent, int nPktId)
{
	context_t context = {0};  // All object's initially null
	DasDesc_init(&(context.varProps), UNK_DESC); // Variable properties at zero

	if((pParent == NULL)||(((DasDesc*)pParent)->type != STREAM)){
		das_error(DASERR_SERIAL, "Stream descriptor must appear before a dataset descriptor");
		return NULL;
	}

	context.pSd = (DasStream*) pParent;
	context.bEmbedAsBytes = DasStream_getEmbedAsBytes(context.pSd);

	context.nPktId = nPktId;
	for(int i = 0; i < VARIDX_MAX; ++i)
		context.aExtShape[i] = VARIDX_UNUSED;

	context.nDasErr = DAS_OKAY;

	XML_Parser pParser = XML_ParserCreate("UTF-8");
	if(pParser == NULL){
		das_error(DASERR_SERIAL, "Couldn't create XML parser\n" );
		return NULL;
	}

	/* Make a 1-D array to hold the current property value during string accumulation */
	DasAry_init(&(context.aPropVal), "streamprops", vtUByte, 0, NULL, RANK_1(0), NULL);

	XML_SetUserData(pParser, (void*)&context);
	XML_SetElementHandler(pParser, _serial_xmlElementBeg, _serial_xmlElementEnd);
	XML_SetCharacterDataHandler(pParser, _serial_xmlCharData);

	if(XML_Parse(pParser, pBuf->pReadBeg, DasBuf_unread(pBuf), true) == XML_STATUS_ERROR)
	{
		das_error(DASERR_PKT, "Parse error at line %d: %s\n",
			XML_GetCurrentLineNumber(pParser),
			XML_ErrorString(XML_GetErrorCode(pParser))
		);
		goto ERROR;
	}

	/* If a flatten degenerated a variable's inner indices, a declared dataset index
	   may have lost its only CONCRETE carrier.  

	   The dataset element supplies the rank (and shape) but das derives actual extent
	   from its variables, so an orphaned index would silently vanish.  

	   A serializable dataset index must resolve to a concrete size (>=0) or ragged 
	   (VARIDX_RAGGED); a merged shape of VARIDX_BORROW (only a sequence generator
	   remains, which stores no extent) or VARIDX_UNUSED means the blob we flattened
	   was the sole thing pinning that index's length.

	   Refuse rather than emit a morphology that won't re-parse.
	*/
	if((context.nDasErr == DAS_OKAY) && context.bFlattened && (context.pDs != NULL)){
		ptrdiff_t aDerived[VARIDX_MAX] = VARIDX_INIT_UNUSED;
		int nRank = DasDs_shape(context.pDs, aDerived);
		for(int i = 1; i < nRank; ++i){
			if((context.aExtShape[i] != VARIDX_UNUSED) && (aDerived[i] <= VARIDX_BORROW)){
				context.nDasErr = das_error(DASERR_SERIAL,
					"Flattening an embedded format orphaned index %d of dataset '%s': "
					"no remaining variable pins its extent (only a sequence generator "
					"is left), so this stream's morphology cannot be recovered as "
					"flattened bytes", i, DasDs_id(context.pDs)
				);
				break;
			}
		}
	}

	/* Coordinate coverage check, are indices covered by at least one 
	   coordinate variable? */
	if((context.nDasErr == DAS_OKAY)&&(context.pDs != NULL)){
		int nRank = DasDs_rank(context.pDs);
		size_t nCoord = DasDs_numDims(context.pDs, DASDIM_COORD);
		for(int iIdx = 0; iIdx < nRank; ++iIdx){
			bool bCovered = false;
			for(size_t iC = 0; (iC < nCoord) && !bCovered; ++iC){
				DasDim* pCoord = DasDs_getDimByIdx(context.pDs, iC, DASDIM_COORD);
				if((pCoord != NULL) && !DasDim_degenerate(pCoord, iIdx))
					bCovered = true;
			}
			if(!bCovered)
				daslog_warn_v(
					"Coordinate coverage gap: no coordinate variable in dataset "
					"'%s' varies along index `%c` (%d)", DasDs_id(context.pDs), 
					g_sIdxLower[iIdx], iIdx
			);
		}
	}

	if((context.nDasErr == DAS_OKAY)&&(context.pDs != NULL)){
		_serial_var_deinit(&context);  /* no-op unless a var never saw its end tag */
		free(context.pSeqMinC);
		free(context.pSeqInterC);
		DasAry_deInit(&(context.aPropVal)); /* Avoid memory leaks */
		DasDesc_freeProps(&(context.varProps));  /* clearProps only reset the count */
		XML_ParserFree(pParser);
		return context.pDs;
	}

ERROR:
	_serial_var_deinit(&context);  /* a parse that died mid-variable still holds a codec */
	free(context.pSeqMinC);
	free(context.pSeqInterC);
	DasAry_deInit(&(context.aPropVal)); /* Avoid memory leaks */
	DasDesc_freeProps(&(context.varProps));
	XML_ParserFree(pParser);
	if(context.pDs)   // Happens, for example, if vector has no components
		del_DasDs(context.pDs);
	das_error(context.nDasErr, context.sErrMsg);
	return NULL;
}
