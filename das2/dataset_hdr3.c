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
#include "vector.h"
#include "codex.h"

#define DS_XML_MAXERR 512

#define _UNIT_BUF_SZ 127
#define _NAME_BUF_SZ 63
#define _TYPE_BUF_SZ 23
#define DASENC_SEM_LEN 32
#define _VAL_STOREAGE_SZ 12
#define _VAL_COMP_LBL_SZ ((DASFRM_NAME_SZ + 1) * 3)
#define _VAL_UNDER_SZ 64 /* Should be enough room for most variables */

#define _VAL_ENC_TYPE_SZ 8
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
	ptrdiff_t aExtShape[DASIDX_MAX];
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
	enum var_type varCategory; 
	das_val_type varItemType;  /* The type of values to store in the array */
	
	int varIntRank; /* Only 0 or 1 are handled right now. So strings and simple vectors */
	das_units varUnits;
	char varUse[DASDIM_ROLE_SZ];
	char varFrame[DASFRM_NAME_SZ];      /* per-vector frame= override; empty -> inherit dim frame */
	char valSemantic[DASENC_SEM_LEN];   /* "real", "integer", "datetime", "string", etc. */
	char valStorage[_VAL_STOREAGE_SZ];
	char varIndex[32];                  /* the raw index= string, kept for embedded* provenance */
	ubyte varCompSys;
	ubyte varCompDirs;
	int nVarComps;  /* only Non-zero if the item is a vector. 
	                   NOTE: Vectors have frames, so even if nVarComps == 1, we are
	                   are still a vector, not a scalar. */

	char varCompLbl[_VAL_COMP_LBL_SZ]; /* HACK ALERT: Temporary hack for dastelem output */

	int8_t aVarMap[DASIDX_MAX];

	DasDesc varProps; /* Temporary accumulator for variable properties */

	/* Stuff needed for sequence vars */
	ubyte aSeqMin[_VAL_SEQ_CONST_SZ];     /* big enough to hold a double */
	ubyte aSeqInter[DASIDX_MAX * _VAL_SEQ_CONST_SZ];  /* one slope per dep axis */

	/* Vector sequence: each <sequence> child is one component; gather them here and
	   transpose into geovec B / M[k] at close-var time.  Capped at 3 (geovec max). */
	int nSeqSeen;
	ubyte aSeqMinC[3][_VAL_SEQ_CONST_SZ];
	ubyte aSeqInterC[3][DASIDX_MAX * _VAL_SEQ_CONST_SZ];

	/* Stuff needed for any array var */
	DasAry* pCurAry;

	/* Stuff needed only for packet data array vars */
	char sValEncType[_VAL_ENC_TYPE_SZ];
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

	/* ... when we hit the <packet><values>or<sequence>  we'll have enough info
	   stored above to create both the variable and it associated array */

	bool bInValues;
	char cValSep;

	DasErrCode nDasErr;
	char sErrMsg[DS_XML_MAXERR];
} context_t;

/* ************************************************************************* */

static void _serial_clear_var_section(context_t* pCtx)
{
	pCtx->bInVar = false;

	pCtx->varCategory = D2V_ARRAY; /* most common kind */
	pCtx->varItemType = vtUnknown;

	pCtx->varIntRank = 0;
	pCtx->varUnits = NULL;

	/* Hopefull compiler will reduce all this to a single op-code for memory erasure */
	memset(pCtx->varUse, 0, DAS_FIELD_SZ(context_t, varUse) );
	memset(pCtx->varFrame, 0, DAS_FIELD_SZ(context_t, varFrame) );
	memset(pCtx->valSemantic, 0, DAS_FIELD_SZ(context_t, valSemantic) );
	memset(pCtx->valStorage,  0, DAS_FIELD_SZ(context_t, valStorage) );
	memset(pCtx->varIndex,    0, DAS_FIELD_SZ(context_t, varIndex) );
	pCtx->bVarBorrows = false;
	pCtx->varCompSys = 0;
	pCtx->varCompDirs = 0;
	pCtx->nVarComps = 0;
	memset(pCtx->varCompLbl,  0, DAS_FIELD_SZ(context_t, varCompLbl) );  /* HACK ALERT */
	for(int i = 0; i < DASIDX_MAX; ++i) pCtx->aVarMap[i] = DASIDX_UNUSED;
	memset(pCtx->aSeqMin, 0, DAS_FIELD_SZ(context_t, aSeqMin));
	memset(pCtx->aSeqInter, 0, DAS_FIELD_SZ(context_t, aSeqInter));
	pCtx->nSeqSeen = 0;
	memset(pCtx->aSeqMinC, 0, DAS_FIELD_SZ(context_t, aSeqMinC));
	memset(pCtx->aSeqInterC, 0, DAS_FIELD_SZ(context_t, aSeqInterC));

	pCtx->pCurAry = NULL;  /* No longer need the array */

	memset(pCtx->sValEncType, 0, DAS_FIELD_SZ(context_t, sValEncType));
	memset(pCtx->sValMime, 0, DAS_FIELD_SZ(context_t, sValMime));
	pCtx->nPktItems = 0;
	pCtx->nPktItemBytes = 0;
	memset(pCtx->sPktFillVal, 0, DAS_FIELD_SZ(context_t, sPktFillVal));
	memset(pCtx->sValTerm, 0, DAS_FIELD_SZ(context_t, sValTerm));
	memset(pCtx->sItemsTerm, 0, DAS_FIELD_SZ(context_t, sItemsTerm));
	pCtx->nTrimReq = -1;
	if(DasCodec_isValid( &(pCtx->codecHdrVals)) )
		DasCodec_deInit( &(pCtx->codecHdrVals) );
	memset(&(pCtx->codecHdrVals), 0, DAS_FIELD_SZ(context_t, codecHdrVals));
	memset(&(pCtx->aValUnderFlow), 0, DAS_FIELD_SZ(context_t, aValUnderFlow));
	pCtx->nValUnderFlowValid = 0;

	DasDesc_clearProps(&(pCtx->varProps));
}

/* ************************************************************************* */

#define _IDX_FOR_DS  false
#define _IDX_FOR_VAR true

static DasErrCode _serial_parseIndex(
	const char* sIndex, int nRank, ptrdiff_t* pMap, bool bInVar,
	const char* sElement
){
	const char* sBeg = sIndex;
	char* sEnd = NULL;
	int iExternIdx = 0;
	while((*sBeg != '\0')&&(iExternIdx < DASIDX_MAX)){
		sEnd = strchr(sBeg, ';');
		if(sEnd == NULL) 
			sEnd = strchr(sBeg, '\0');
		else
			*sEnd = '\0';
		if(sBeg == sEnd)
			return das_error(DASERR_SERIAL, "Empty index shape entry in element <%s>", sElement);

		if(sBeg[0] == '*')
			pMap[iExternIdx] = DASIDX_RAGGED;
		else if(sBeg[0] == '^'){
			/* Borrowed extent: this index varies but the var declines to size it,
			   taking the dataset's instead.  Only meaningful on a variable (the
			   dataset itself is the authority, it can't borrow from itself). */
			if(!bInVar)
				return das_error(DASERR_SERIAL,
					"A borrowed extent '^' is not allowed in element <%s>", sElement
				);
			pMap[iExternIdx] = DASIDX_BORROW;
		}
		else{
			if(sBeg[0] == '-'){
				if(!bInVar){
					return das_error(DASERR_SERIAL,
						"Unused array indexes are not allowed in element <%s>", sElement
					);
				}
				pMap[iExternIdx] = DASIDX_UNUSED;
			}
			else{
				if(sscanf(sBeg, "%td", pMap + iExternIdx)!=1){
					return das_error(DASERR_SERIAL, 
						"Could not parse index shape of %s in element <%s>",
						sIndex, sElement
					);
				}	
			}
		}
		sBeg = sEnd + 1;
		++iExternIdx;
	}

	if(iExternIdx != nRank){
		return das_error(DASERR_SERIAL,
			"The rank of this dataset is %d, but %d index ranges were specified",
			nRank, iExternIdx
		);
	}
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
	char sIndex[48] = {'\0'};
	const char* sPlot = NULL;
	for(int i = 0; psAttr[i] != NULL; i+=2){
		if(strcmp(psAttr[i],"rank")==0)       sRank=psAttr[i+1];
		else if(strcmp(psAttr[i],"name")==0)  sName=psAttr[i+1];
		else if(strcmp(psAttr[i],"plot")==0)  sPlot=psAttr[i+1];
		else if((strcmp(psAttr[i],"index")==0)&&(psAttr[i+1][0] != '\0')) 
			strncpy(sIndex, psAttr[i+1], 47);
		else
			daslog_warn_v("Unknown attribute %s in <dataset> ID %02d", psAttr[i], pCtx->nPktId);
	}
	
	char sId[12] = {'\0'};
	snprintf(sId, 11, "id%02d", pCtx->nPktId);

	int nRank = 0;
	int id = pCtx->nPktId;
	if((sRank==NULL)||(sscanf(sRank, "%d", &nRank) != 1)){
		pCtx->nDasErr = das_error(DASERR_SERIAL, "Invalid or missing rank attribute for <dataset> %02d", id);
		return;
	}
	if((nRank <= 0)||(nRank >= DASIDX_MAX)){
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

	pCtx->pDs = new_DasDs(sId, sName, nRank);

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
	const char* sFrame = NULL;
	char sAxis[48] = {'\0'};
	char sAnnot[48] = {'\0'};

	for(int i = 0; psAttr[i] != NULL; i+=2){
		if(strcmp(psAttr[i],"physDim")==0)     sPhysDim = psAttr[i+1];
		else if(strcmp(psAttr[i],"name")==0)   sName    = psAttr[i+1];
		else if(strcmp(psAttr[i],"frame")==0)  sFrame   = psAttr[i+1];
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
	
	if((sFrame != NULL)&&(sFrame[0] != '\0'))
		DasDim_setFrame(pDim, sFrame);

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
/* Helper: number of components and thier order in the packet */
DasErrCode _setComponents(context_t* pCtx, const char* sNumComp, const char* sOrder)
{
	ubyte uComp = 0;
	ubyte dirs = 0;

	if(sNumComp == NULL){
		return das_error(DASERR_SERIAL,
			"Number of components were not specified for <vector> in dataset %d",
			pCtx->nPktId
		);
	}
	if((strlen(sNumComp) > 1)||(sscanf(sNumComp, "%hhu", &uComp) != 1)||(uComp == 0)||(uComp > 3))
		return das_error(DASERR_SERIAL, "Invalid number of components '%s' for <vector> in dataset %d",
			sNumComp, pCtx->nPktId
		);

	/* sysorder is optional: absent means natural order -- packet component k lies
	   along direction k, the same result an explicit "0;1;2" would produce. */
	if(sOrder == NULL){
		for(ubyte k = 0; k < uComp; ++k)
			dirs |= ((ubyte)k) << (k*2);
		pCtx->varCompDirs = dirs;
		pCtx->nVarComps = uComp;
		pCtx->aVarMap[DasDs_rank(pCtx->pDs)] = pCtx->nVarComps;
		return DAS_OKAY;
	}

	const char* p = sOrder;

	ubyte uSeps = 0;
	while(*p != '\0'){
		if(*p == ';'){
			uSeps += 1;
			if(uSeps > 3) goto ERROR_COMP_NUM;
		}
		else{
			switch(*p){
			case '0': break;
			case '1': dirs |= 1<<(uSeps*2); break;
			case '2': dirs |= 2<<(uSeps*2); break;
			default: goto ERROR_COMP_NUM;
			}
		}
		++p;
	}

	if((uSeps+1) != uComp)
		return das_error(DASERR_SERIAL,
			"Expected %d values in 'sysorder', found %d", uComp, uSeps+1
		);

	pCtx->varCompDirs = dirs;
	pCtx->nVarComps = uComp;

	/* Set the number of internal components for the variable map too */
	pCtx->aVarMap[DasDs_rank(pCtx->pDs)] = pCtx->nVarComps;
	return DAS_OKAY;

	/* TODO: I need to increase the rank for strings too, not sure what to do there */

ERROR_COMP_NUM:
	return das_error(DASERR_SERIAL, 
		"Handling geometric vectors with more then 3 components is not implemented."
	);
}

/* *****************************************************************************
   Starting a new variable, either scalar or vector 
*/
static void _serial_onOpenVar(
	context_t* pCtx, const char* sVarElType, const char** psAttr
){
	if(pCtx->bInVar){
		pCtx->nDasErr = das_error(DASERR_SERIAL,
			"Scalars and Vectors can not be nested inside other scalars and vectors"
		);
		return;
	}

	int id = pCtx->nPktId;
	char sIndex[32] = {'\0'};
	
	/* Assume center until proven otherwise */
	strncpy(pCtx->varUse, "center", DASDIM_ROLE_SZ-1);

	const char* sNumber = NULL;
	const char* sOrder = NULL;

	for(int i = 0; psAttr[i] != NULL; i+=2){
		if(strcmp(psAttr[i], "use") == 0)
			strncpy(pCtx->varUse, psAttr[i+1], DASDIM_ROLE_SZ-1);

		/* For now allow both semantic and valType, but valType doesn't validate */
		else if(
			(strcmp(psAttr[i], "semantic") == 0)||(strcmp(psAttr[i], "valType") == 0)
		)    /* Partial value type, need pkt */
			strncpy(pCtx->valSemantic, psAttr[i+1], DASENC_SEM_LEN-1);/* encoding details to decide */
		else if(strcmp(psAttr[i], "storage") == 0)
			strncpy(pCtx->valStorage, psAttr[i+1], _VAL_STOREAGE_SZ-1);
		else if(strcmp(psAttr[i], "index") == 0){
			strncpy(sIndex, psAttr[i+1], 31);
			strncpy(pCtx->varIndex, psAttr[i+1], sizeof(pCtx->varIndex)-1);
		}
		else if(strcmp(psAttr[i], "units") == 0)
			pCtx->varUnits = Units_fromStr(psAttr[i+1]);
		else if(strcmp(psAttr[i], "components") == 0)
			sNumber = psAttr[i+1];
		else if(strcmp(psAttr[i], "sysorder") == 0)
			sOrder = psAttr[i+1];
		else if((strcmp(psAttr[i], "vecClass") == 0)||(strcmp(psAttr[i], "system") == 0)){

			if( (pCtx->varCompSys = das_compsys_id(psAttr[i+1])) == 0){
				pCtx->nDasErr = DASERR_VEC;
				return;
			}
		}
		/* A vector may carry its own frame=, overriding the dimension's frame (a dim
		   can hold vectors in different frames).  Absent, it inherits the dim frame. */
		else if(strcmp(psAttr[i], "frame") == 0)
			strncpy(pCtx->varFrame, psAttr[i+1], DASFRM_NAME_SZ - 1);

		/* Temporarily ignore values that are running around in wild */
		else
			daslog_warn_v(
				"Unknown attribute %s in <%s> for dataset ID %02d", psAttr[i], sVarElType, id
			);
	}

	bool bIsVector = (strcmp(sVarElType, "vector") == 0);
	if(bIsVector){
		if((pCtx->nDasErr = _setComponents(pCtx, sNumber, sOrder)) != DAS_OKAY)
			return;

		/* system= is optional; absent means the schema default, cartesian.  The
		   component fields reset to 0 (DAS_VSYS_UNKNOWN) per vector in
		   _serial_clear_var_section, so varCompSys==0 here reliably means "no system=
		   was given" */
		if(pCtx->varCompSys == 0)
			pCtx->varCompSys = DAS_VSYS_CART;
	}

	/* Get the mapping from dataset space to array space */ 
	ptrdiff_t aVarExtShape[DASIDX_MAX];
	int nRet = _serial_parseIndex(
		sIndex, DasDs_rank(pCtx->pDs), aVarExtShape, _IDX_FOR_VAR, sVarElType
	);
	if(nRet != DAS_OKAY){
		pCtx->nDasErr = nRet;
		return;
	}

	/* Make the var map, insure all unused index positions are set as unused */
	int j = 0;
	int nDsRank = DasDs_rank(pCtx->pDs);
	for(int i = 0; i < DASIDX_MAX; ++i){
		if((i < nDsRank) &&(aVarExtShape[i] != DASIDX_UNUSED)){
			pCtx->aVarMap[i] = j;
			j++;
		}
		else{
			pCtx->aVarMap[i] = DASIDX_UNUSED;
		}
	}

	/* Only a sequence may borrow ('^'); a stored var owns its extent.  Flag it here
	   so the array build (_serial_makeVarAry) can reject a '^' on a values/packet var. */
	pCtx->bVarBorrows = false;
	for(int i = 0; i < nDsRank; ++i)
		if(aVarExtShape[i] == DASIDX_BORROW) pCtx->bVarBorrows = true;

	/* If this is a vector, or a string/blob, mention that we have 1 internal index.
	   A string (vtText) and a blob (vtByteSeq) both carry a ragged inner byte dim,
	   so both need internal rank 1; see _serial_makeVarAry's vtText/vtByteSeq split. */
	bool bString = (strcmp(pCtx->valSemantic, "string") == 0);
	bool bBlob   = (strcmp(pCtx->valSemantic, "blob") == 0);
	if(bIsVector){
		pCtx->varIntRank = 1;
		if(bString || bBlob){
			pCtx->nDasErr = das_error(DASERR_SERIAL,
				"Vectors of strings/blobs are not supported for <%s> in dataset ID.  Max internal index is 1",
				sVarElType, pCtx->nPktId
			);
			return;
		}
	}
	else{
		if(bString || bBlob) pCtx->varIntRank = 1;
	}

	if(pCtx->varUse[0] == '\0')  /* Default to a usage of 'center' */
		strncpy(pCtx->varUse, "center", DASDIM_ROLE_SZ-1);

	if(pCtx->valSemantic[0] == '\0'){
		pCtx->nDasErr = das_error(DASERR_SERIAL,
			"Attribute 'semantic' not provided for <%s> in dataset ID %d", sVarElType, pCtx->nPktId
		);
		return;
	}
	if(pCtx->varUnits == NULL){
		pCtx->nDasErr = das_error(DASERR_SERIAL,
			"Attribute 'units' not provided for <%s> in dataset ID %d", sVarElType, pCtx->nPktId
		);
		return;
	}

	/* Calendar-representable units (an epoch: TT2000, t1970, ...) need to 
	   match with datetime semantics */
	if(Units_haveCalRep(pCtx->varUnits) && (strcmp(pCtx->valSemantic, "datetime") != 0)){
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

	pCtx->varCategory = D2V_SEQUENCE;

	for(int i = 0; psAttr[i] != NULL; i+=2){
		if(strcmp("minval", psAttr[i]) == 0)
			sMin = psAttr[i+1];
		else if(strcmp("interval", psAttr[i]) == 0)
			sInter = psAttr[i+1];
		else if(strcmp("repeat", psAttr[i]) == 0)
			pCtx->nDasErr = das_error(DASERR_NOTIMP, 
				"In <sequence> for dataset ID %d, reapeated sequence items not yet supported",
				pCtx->nPktId
			);
		else if(strcmp("repetitions", psAttr[i]) == 0)
			pCtx->nDasErr = das_error(DASERR_NOTIMP, 
				"In <sequence> for dataset ID %d, reapeated sequence items not yet supported",
				pCtx->nPktId
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
		if(strcmp(pCtx->valSemantic, "real") == 0)
			strncpy(pCtx->valStorage, "double", _VAL_STOREAGE_SZ);
		else if(strcmp(pCtx->valSemantic, "integer") == 0)
			strncpy(pCtx->valStorage, "long", _VAL_STOREAGE_SZ);
		else if(strcmp(pCtx->valSemantic, "bool") == 0)
			strncpy(pCtx->valStorage, "byte", _VAL_STOREAGE_SZ);
		else if(strcmp(pCtx->valSemantic, "datetime") == 0){
			if(pCtx->varUnits == UNIT_TT2000)
				strncpy(pCtx->valStorage, "long", _VAL_STOREAGE_SZ);
			else
				strncpy(pCtx->valStorage, "double", _VAL_STOREAGE_SZ);
		}
		else if(strcmp(pCtx->valSemantic, "string") == 0)
			strncpy(pCtx->valStorage, "utf8", _VAL_STOREAGE_SZ);
		else{
			strncpy(pCtx->valStorage, "ubyte*", _VAL_STOREAGE_SZ);
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
	   "16;0.125" for a sequence that runs over two indicies).  Pack the slopes
	   contiguously at the item stride -- the same layout new_DasVarSeq() reads --
	   and insure the slope count == the used index count, which is usually one. */
	/* Count external dependent indices only.  A vector also marks the component index
	   in aVarMap (at index DasDs_rank); that is an internal index, not a sequence
	   dependency, so bound the count to the dataset rank. */
	int nUsed = 0;
	int nDsRank = DasDs_rank(pCtx->pDs);
	for(int i = 0; i < nDsRank; ++i) if(pCtx->aVarMap[i] >= 0) ++nUsed;

	size_t uItem = das_vt_size(pCtx->varItemType);

	/* Parse the interval list.  Conservative emit is FULL-RANK: one ';'-separated
	   slot per DATASET index, positionally locked to `index` -- a `-` at every
	   degenerate position, a slope at every dependent one.  Also accepted (liberal
	   in): a SHORTHAND single slope, valid only when the sequence depends on exactly
	   ONE index.  Slopes pack into aSeqInter in DEPENDENCY order (matching how
	   new_DasVarSeq reads M[k]). */
	char aTok[DASIDX_MAX][64];
	int nTok = 0;
	const char* pBeg = sInter;
	while(*pBeg != '\0'){
		if(nTok >= DASIDX_MAX){
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
					pCtx->varItemType, aTok[i]
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
				pCtx->varItemType, aTok[k]
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

	/* Inside a <vector>, this <sequence> is one component.  Stash its parsed intercept
	   and slopes into the next component slot; _serial_onCloseVar transposes the N
	   components into geovec B / M[k] once the frame is resolved. */
	if(pCtx->nVarComps > 0){
		if(pCtx->nSeqSeen >= pCtx->nVarComps){
			pCtx->nDasErr = das_error(DASERR_SERIAL,
				"More <sequence> elements than the %d component(s) the vector declares, "
				"dataset ID %d", pCtx->nVarComps, pCtx->nPktId
			);
			return;
		}
		memcpy(pCtx->aSeqMinC[pCtx->nSeqSeen], pCtx->aSeqMin, _VAL_SEQ_CONST_SZ);
		memcpy(pCtx->aSeqInterC[pCtx->nSeqSeen], pCtx->aSeqInter, DASIDX_MAX * _VAL_SEQ_CONST_SZ);
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
	   var (values/packet), which owns its extent. */
	if(pCtx->bVarBorrows)
		return das_error(DASERR_SERIAL,
			"A borrowed extent '^' is only valid on a <sequence>; variable %s:%s in "
			"dataset ID %d is a stored var and must declare its own size",
			DasDim_id(pCtx->pCurDim), pCtx->varUse, pCtx->nPktId
		);
	char sAryId[64] = {'\0'};

	snprintf(sAryId, 63, "%s_%s", pCtx->varUse, DasDim_id(pCtx->pCurDim));

	/* Determine the array indexes from the variable indexes */
	size_t aShape[DASIDX_MAX];
	int nAryRank = 0; /* <-- current array index then bumped to the rank */
	int nDsRank = DasDs_rank(pCtx->pDs);
	for(int i = 0; i < nDsRank; ++i){
		if(pCtx->aVarMap[i] == DASIDX_UNUSED)
			continue;

		/* if(pCtx->aExtShape[pCtx->aVarMap[i]] == DASIDX_RAGGED){ */

		if(pCtx->aExtShape[i] == DASIDX_RAGGED){
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

	das_val_type vt = vtUnknown;
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

	/* Dealing with internal structure */
	uint32_t uFlags = 0;

	if(pCtx->varIntRank > 0){	
		/* Internal structure due to vectors */
		if(pCtx->nVarComps > 0){
			aShape[nAryRank] = pCtx->nVarComps;
			++nAryRank;
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
					"Unknown purpose for internal variable indicies, not a vector "
					"nor a string nor a byte-string"
				);
			}
		}
	}

	ubyte aFill[DATUM_BUF_SZ] = {0};

	if(bHandleFill){
		bool bTextFill = (strcmp(pCtx->valSemantic, "string") == 0);
		bool bBoolFill = (strcmp(pCtx->valSemantic, "bool") == 0);
		int nRet = _serial_initfill(aFill, DATUM_BUF_SZ, vt, pCtx->sPktFillVal, bTextFill, bBoolFill);
		if(nRet != DAS_OKAY)
			return nRet;
	}

	pCtx->pCurAry = new_DasAry(
		sAryId, vt, 0, aFill, nAryRank, aShape, pCtx->varUnits
	);

	if(uFlags > 0)
		DasAry_setUsage(pCtx->pCurAry, uFlags);

	/* Add it to the dataset */
	DasDs_addAry(pCtx->pDs, pCtx->pCurAry);

	return DAS_OKAY;
}

/* ************************************************************************** */
/* Save the info needed to make a packet data encoder/decoder */

static void _serial_onPacket(context_t* pCtx, const char** psAttr)
{

	if(pCtx->nDasErr != DAS_OKAY)  /* If an error condition is set, stop processing elements */
		return;

	pCtx->varCategory = D2V_ARRAY;

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
			strncpy(pCtx->sValEncType, psAttr[i+1], _VAL_ENC_TYPE_SZ-1);
			nReq |= 0x2;
			continue;
		}
		if(strcmp(psAttr[i], "mime") == 0){
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
		/* trim toggles the default-on left+rigth trim of each value (var-width utf8).
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

	/* Check to see if all needed attribtues were provided */
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
	if( ((nValTermStat & 0x1) == 0x1) && (strcmp(pCtx->valSemantic, "string") == 0)
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

	/* A blob- or base64-encoded value carrying a mime is an embedded format
	   (png, ...) that needs a codec extension to decode; the two encodings are the
	   just blob or base64, so both are undecodable without the codec.
	   A bare blob or base64 section (no mime) is raw pass-through and needs none. */
	if(((strcmp(pCtx->sValEncType, "blob") == 0)||(strcmp(pCtx->sValEncType, "base64") == 0))
	   && (pCtx->sValMime[0] != '\0') && !das_codex_supported(pCtx->sValMime)){

		/* Default: fail loud HERE, with the mime in hand, rather than at the
		   cryptic storage guard in init_DasVarAry downstream. */
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
		strncpy(pCtx->valSemantic, "blob", DASENC_SEM_LEN-1);
		memset(pCtx->valStorage, 0, DAS_FIELD_SZ(context_t, valStorage));
		memset(pCtx->sValMime,   0, DAS_FIELD_SZ(context_t, sValMime));
		for(int i = 1; i < DASIDX_MAX; ++i) pCtx->aVarMap[i] = DASIDX_UNUSED;
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
	if(pCtx->nDasErr != DAS_OKAY)  /* Error flag rasied, stop parsing */
		return;

	pCtx->varCategory = D2V_ARRAY;

	if(pCtx->bInValues){ // Can't nest values
		pCtx->nDasErr = das_error(DASERR_SERIAL, 
			"<values> element nested in dataset ID %d", pCtx->nPktId
		);
		return;
	}
	pCtx->bInValues = true;
	assert(pCtx->pCurAry == NULL);

	/* A fixed set of values can't map to a variable length index */
	int nDsRank = DasDs_rank(pCtx->pDs);
	for(int i = 0; i < nDsRank; ++i){
		if(pCtx->aVarMap[i] != DASIDX_UNUSED){
			if(pCtx->aExtShape[i] == DASIDX_RAGGED){
				pCtx->nDasErr = das_error(DASERR_SERIAL,
					"The external shape of variable %s:%s in dataset ID %02d is not "
					"consistant with the shape of the overall dataset.  A fixed set of "
					"values in index %d, can't map to a dataset with a variable length "
					"in index %d.",
					DasDim_id(pCtx->pCurDim), pCtx->varUse, pCtx->nPktId, i, i
				);
				return;
			}
		}
	}


	/* valTerm (or the alias valSep) sets the value terminator for the block; an
	   absent valTerm selects the whitespace default, same as <packet>.  Other
	   <values> attributes (repeat/repetitions/idxTerm) are not yet implemented. */
	for(int i = 0; psAttr[i] != NULL; i+=2){
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

	strncpy(pCtx->sValEncType, "utf8", _VAL_ENC_TYPE_SZ-1);

	DasErrCode nRet = _serial_makeVarAry(pCtx, NO_FILL);
	if(nRet != DAS_OKAY){
		pCtx->nDasErr = nRet;
		return;
	}

	/* Make an encoder for header values.  You have to provide cSep if you want
		to have null (fill) items in <values> blocks, but no one does this cause
		<values> are typically coordinates, and coordinates are known, ususally. */
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
	if((strcmp(sElement, "scalar") == 0)||(strcmp(sElement, "vector") == 0)){
		_serial_onOpenVar(pCtx, sElement, psAttr);
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
/* Accumlating data between element tags */

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
	   current value, read it then advace the read pointer. */
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
			int nValsRead = 0;
			nUnRead = DasCodec_decode(&(pCtx->codecHdrVals), pCtx->aValUnderFlow, nLen, -1, &nValsRead);
			if(nUnRead < 0){
				pCtx->nDasErr = -1 * nUnRead;
				strncpy(pCtx->sErrMsg, "Decoding error in header values", 511);
				return;
			}
			memset(pCtx->aValUnderFlow, 0, _VAL_TERM_SZ);
			pCtx->nValUnderFlowValid = 0;

			nLen -= n;
			sChars += n;
		}
	}

	/* Decode as many values as possible from the input */
	/* TODO:  make DasAry_putAt handle index rolling as well */
	nUnRead = DasCodec_decode(&(pCtx->codecHdrVals), (const ubyte*) sChars, nLen, -1, NULL);
	if(nUnRead < 0){
		strncpy(pCtx->sErrMsg, "Decoding error in header values", 511);
		pCtx->nDasErr = -1*nUnRead;
		return;
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
	 *   - We don't care about mappings to non-fixed external indicies
	 */
	size_t uExpect = 0;
	
	for(int iExt = 0; iExt < DASIDX_MAX; ++iExt){

		if(pCtx->aExtShape[iExt] == DASIDX_UNUSED)
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
	ptrdiff_t aShape[DASIDX_MAX] = {0};
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
void DasVarSeq_setExtents(DasVar* pBase, const ptrdiff_t* pDsShape);

static void _serial_onCloseVar(context_t* pCtx)
{
	if(pCtx->nDasErr != DAS_OKAY) /* Stop processing on error */
		return;

	/* Create the variable, for vector variables, we may need to create an
	   implicit frame as well */
	DasVar* pVar = NULL;
	
	if(pCtx->nVarComps > 0){

		/* HACK ALERT: Pick up names put in the wrong place and put them in a property instead * /
		if(pCtx->varCompLbl[0] != '\0')
			DasDesc_flexSet((DasDesc*)(pCtx->pCurDim),  
				"stringArray", 0, "compLabel", pCtx->varCompLbl, ' ', NULL, DASPROP_DAS3
			);

		/ * end HACK ALERT */

		/* Per-vector frame= overrides the dim frame (a dimension can hold vectors in
		   different frames, e.g. a reference in a geodetic frame + an offset in a
		   local frame).  If missing, the vector inherits the dim's frame. */
		const char* sFrame = (pCtx->varFrame[0] != '\0')
		                   ? pCtx->varFrame : DasDim_getFrame(pCtx->pCurDim);


		const DasFrame* pFrame = (sFrame == NULL) ? NULL : DasStream_getFrameByName(pCtx->pSd, sFrame);

		/* If my frame name is not null, but there is not defined frame in the header
		   the make one */
		if((sFrame != NULL)&&(pFrame == NULL)){

			/* If the stream had no frame section, but provided at least a frame name
			   then generate one.

			  BIG WARNING:
			     You want to use explicit frames in your streams... you really do.  

			     That is because MAGnetometer people often provide *cartesian* 
			     vectors whose orthogonal unit vectors are set by the instantaneous
			     location in a *non-cartesian* coordinate frame!  

			     In order to properly take the magnitude of a vector you have to know
			     it's component system and this may be different from the reference frame.
			     I know... wierd, right.
			 */
			int iFrame = DasStream_newFrameId(pCtx->pSd);
			if(iFrame < 0){
				pCtx->nDasErr = -1*iFrame;
				goto NO_CUR_VAR;
			}

			DasFrame* pMkFrame = DasStream_createFrame(pCtx->pSd, iFrame, sFrame);

			DasDesc_setStr((DasDesc*)pMkFrame, "title", "Autogenerated Frame");

			pFrame = pMkFrame; /* Make Frame Constant Again */
		}

		int8_t iFrame = 0;
		if(pFrame != NULL){
			iFrame = DasStream_getFrameId(pCtx->pSd, sFrame);
			if(iFrame < 0){
				pCtx->nDasErr = das_error(DASERR_SERIAL,
					"No frame named %s is defined for the stream", sFrame
				);
				goto NO_CUR_VAR;
			}
		}
		
		if(pCtx->pCurAry == NULL){
			/* Vector sequence: the components arrived as N <sequence> children (gathered
			   into aSeqMinC / aSeqInterC).  Transpose them into a geovec intercept B and
			   one geovec slope M[k] per dependent index, then build a single vtGeoVec
			   sequence.  A <sequence> inside a <vector> is always vtGeoVec, even for one
			   component. */
			if(pCtx->nSeqSeen != pCtx->nVarComps){
				pCtx->nDasErr = das_error(DASERR_SERIAL,
					"<vector> declares %d component(s) but has %d <sequence> child(ren), "
					"dataset ID %d", pCtx->nVarComps, pCtx->nSeqSeen, pCtx->nPktId
				);
				goto NO_CUR_VAR;
			}

			das_val_type et = pCtx->varItemType;
			ubyte esize = (ubyte)das_vt_size(et);
			ubyte ncomp = (ubyte)pCtx->nVarComps;

			/* dependent (external) index count -- same for every component */
			int nDeps = 0, nDsRank = DasDs_rank(pCtx->pDs);
			for(int i = 0; i < nDsRank; ++i) if(pCtx->aVarMap[i] >= 0) ++nDeps;

			/* Intercept geovec: component c's value is aSeqMinC[c]. */
			ubyte packed[24];
			das_geovec gvB;
			for(ubyte c = 0; c < ncomp; ++c)
				memcpy(packed + (size_t)c*esize, pCtx->aSeqMinC[c], esize);
			if(das_geovec_init(&gvB, packed, (ubyte)iFrame, 0, pCtx->varCompSys, et, esize,
			                   ncomp, pCtx->varCompDirs) != DAS_OKAY){
				pCtx->nDasErr = das_error(DASERR_SERIAL,
					"Could not build vector-sequence intercept, dataset ID %d", pCtx->nPktId);
				goto NO_CUR_VAR;
			}

			/* One slope geovec per dependent index: M[k]'s component c is component c's
			   k-th slope (aSeqInterC packs a component's slopes at esize stride). */
			das_geovec aM[DASIDX_MAX];
			for(int k = 0; k < nDeps; ++k){
				for(ubyte c = 0; c < ncomp; ++c)
					memcpy(packed + (size_t)c*esize, pCtx->aSeqInterC[c] + (size_t)k*esize, esize);
				if(das_geovec_init(&aM[k], packed, (ubyte)iFrame, 0, pCtx->varCompSys, et, esize,
				                   ncomp, pCtx->varCompDirs) != DAS_OKAY){
					pCtx->nDasErr = das_error(DASERR_SERIAL,
						"Could not build vector-sequence slope, dataset ID %d", pCtx->nPktId);
					goto NO_CUR_VAR;
				}
			}

			pVar = new_DasVarSeq(
				pCtx->varUse, vtGeoVec, 0, &gvB, aM,
				DasDs_rank(pCtx->pDs), pCtx->aVarMap, 1 /*nIntRank*/, pCtx->varUnits
			);
		}
		else{
			// Use the given vector class here, even if frame is a different class
			pVar = new_DasVarVecAry(
				pCtx->pCurAry, DasDs_rank(pCtx->pDs), pCtx->aVarMap, 1, /* internal rank = 1 */
				iFrame, pCtx->varCompSys, pCtx->nVarComps, pCtx->varCompDirs
			);
		}
	}
	else{
		// Scalar variables (the more common case)
		if(pCtx->pCurAry == NULL){
			pVar = new_DasVarSeq( 
				pCtx->varUse, pCtx->varItemType, 0, pCtx->aSeqMin, pCtx->aSeqInter,
				DasDs_rank(pCtx->pDs), pCtx->aVarMap, 0, pCtx->varUnits
			);
		}
		else{
			pVar = new_DasVarArray(pCtx->pCurAry, DasDs_rank(pCtx->pDs), pCtx->aVarMap, pCtx->varIntRank);
		}
	}
	DasVar_setSemantic(pVar, pCtx->valSemantic);

	/* A sequence declares its extent via `index=`; carry the dataset's declared
	   size onto it so DasDs_shape gets the size from the sequence too (not just
	   from a sibling array).  No-op for non-sequence vars. */
	DasVarSeq_setExtents(pVar, pCtx->aExtShape);

	/* If this is an array var type & it is record varying, add a packet decoder */
	if((pCtx->varCategory == D2V_ARRAY)&&(pCtx->aVarMap[0] != DASIDX_UNUSED)){

		DasCodec* pNewCodec = NULL;

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
			bool bLenPfx = (strcmp(pCtx->sValEncType, "blob") == 0)
			            || (strcmp(pCtx->sValEncType, "base64") == 0);
			int16_t nFraming = bLenPfx ? DASENC_ITEM_LEN : DASENC_ITEM_TERM;
			ubyte cSep = bLenPfx ? '\0' : pCtx->sValTerm[0];

			pNewCodec = DasDs_addStringCodec(
				pCtx->pDs, DasAry_id(pCtx->pCurAry), pCtx->valSemantic,
				pCtx->sValEncType, nFraming, cSep,
				pCtx->nPktItems, DASENC_READ
			);

		}
		else{
			pNewCodec = DasDs_addFixedCodec(
				pCtx->pDs, DasAry_id(pCtx->pCurAry), pCtx->valSemantic,
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
				if((pCtx->aVarMap[iExt] != DASIDX_UNUSED) && (pCtx->aExtShape[iExt] == DASIDX_RAGGED))
					++nExtRagged;
			}
			pNewCodec->nExtRagged = nExtRagged;

			/* If the header declared idxTerm run terminators, parse the comma-separated
			   list (with \n \t \r \0 escapes) and load it onto the read codec so the
			   decoder can bound each ragged run.  The count must match the var's
			   external ragged count exactly -- a partial hierarchy would split run
			   ownership between the tag and terminator readers.  An absent idxTerm is
			   legal for binary (count tags are found in the data) and for a SINGLE
			   ragged index, where the packet frame may bound the run of a last
			   variable; utf8 with 2+ ragged indices has no other declarable mechanism,
			   so there idxTerm is required (checked below). */
			if(pCtx->sItemsTerm[0] != '\0'){
				char aLvls[DASIDX_MAX];
				ubyte nLvls = 0;
				const char* p = pCtx->sItemsTerm;
				while((*p != '\0') && (nLvls < DASIDX_MAX)){
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
				if(nLvls != nExtRagged){
					pCtx->nDasErr = das_error(DASERR_SERIAL,
						"idxTerm=\"%s\" has %hhu terminator(s) but %s:%s has %hhu external ragged "
						"index(es) in dataset ID %02d", pCtx->sItemsTerm, nLvls,
						DasDim_id(pCtx->pCurDim), pCtx->varUse, nExtRagged, pCtx->nPktId);
					goto NO_CUR_VAR;
				}
				DasErrCode nTRet = DasCodec_setIdxTerms(pNewCodec, nLvls, aLvls);
				if(nTRet != DAS_OKAY){ pCtx->nDasErr = nTRet; goto NO_CUR_VAR; }
			}
			else if((nExtRagged >= 2) && DasCodec_isText(pNewCodec)){
				pCtx->nDasErr = das_error(DASERR_SERIAL,
					"%s:%s in dataset ID %02d is utf8 with %hhu external ragged indexes "
					"but declares no idxTerm.", DasDim_id(pCtx->pCurDim), pCtx->varUse, 
					pCtx->nPktId, nExtRagged
				);
				goto NO_CUR_VAR;
			}

			/* An explicit trim="..." overrides the codec's default (on for var-width
			   utf8; a no-op elsewhere since only _var_text_read consults it). */
			if(pCtx->nTrimReq >= 0)
				DasCodec_setTrim(pNewCodec, pCtx->nTrimReq == 1);
		}
	}

	if(pVar == NULL){
		pCtx->nDasErr = DASERR_VAR; 
		goto NO_CUR_VAR;
	}

	/* If any properties were buffered for this variable, copy them over */
	if(DasDesc_length(&(pCtx->varProps)) > 0){
		DasDesc_copyIn((DasDesc*)pVar, &(pCtx->varProps));
	}

	if(!DasDim_addVar(pCtx->pCurDim, pCtx->varUse, pVar)){
		dec_DasVar(pVar);
		pCtx->nDasErr = DASERR_DIM;
	}

	/* Set the parent pointer for the variable */
	((DasDesc*)pVar)->parent = (DasDesc*) pCtx->pCurDim;
	
NO_CUR_VAR:  /* No longer in a var, nor in an array */
	_serial_clear_var_section(pCtx);
}

/* ************************************************************************** */

/* Removing center creation, not sure why this should be done automatically */

/* 
static void _serial_onCloseDim(context_t* pCtx)
{
	/ * If this dim has a reference and an offset, but no center, add the center * /
	if(DasDim_getPointVar(pCtx->pCurDim) != NULL) return;

	DasVar* pRef = DasDim_getVar(pCtx->pCurDim, DASVAR_REF);
	DasVar* pOff = DasDim_getVar(pCtx->pCurDim, DASVAR_OFFSET);
	
	if((pRef != NULL)&&(pOff != NULL)){
		char sBuf[DAS_MAX_ID_BUFSZ] = {'\0'};
		snprintf(sBuf, DAS_MAX_ID_BUFSZ - 1, "%s_center", DasDim_dim(pCtx->pCurDim));
		DasVar* pCent = new_DasVarBinary(sBuf, pRef, "+", pOff);
		if(! DasDim_addVar(pCtx->pCurDim, DASVAR_CENTER, pCent))
			pCtx->nDasErr = DASERR_DIM;
	}
}

*/

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

	if((strcmp(sElement, "vector")==0)||(strcmp(sElement, "scalar")==0)){
		_serial_onCloseVar(pCtx);
		return;
	}
	/* Nothing to do on the other ones */
}

/* ************************************************************************** */

/** Define a das dataset and all it's constiutant parts from an XML header
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
	for(int i = 0; i < DASIDX_MAX; ++i)
		context.aExtShape[i] = DASIDX_UNUSED;

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
	   may have lost its only CONCRETE carrier.  The dataset element fixes rank +
	   declared shape, but das derives actual extent from its variables, so an
	   orphaned index would silently vanish.  A serializable dataset index must
	   resolve to a concrete size (>=0) or ragged (DASIDX_RAGGED); a merged shape of
	   DASIDX_BORROW (only a sequence generator remains, which stores no extent) or
	   DASIDX_UNUSED means the blob we flattened was the sole thing pinning that
	   index's length.  Refuse rather than emit a morphology that won't re-parse. */
	if((context.nDasErr == DAS_OKAY) && context.bFlattened && (context.pDs != NULL)){
		ptrdiff_t aDerived[DASIDX_MAX] = DASIDX_INIT_UNUSED;
		int nRank = DasDs_shape(context.pDs, aDerived);
		for(int i = 1; i < nRank; ++i){
			if((context.aExtShape[i] != DASIDX_UNUSED) && (aDerived[i] <= DASIDX_BORROW)){
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

	/* Coordinate coverage check, are indicies covered by at least one 
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
		DasAry_deInit(&(context.aPropVal)); /* Avoid memory leaks */
		DasDesc_freeProps(&(context.varProps));  /* clearProps only reset the count */
		XML_ParserFree(pParser);
		return context.pDs;
	}

ERROR:
	DasAry_deInit(&(context.aPropVal)); /* Avoid memory leaks */
	DasDesc_freeProps(&(context.varProps));
	XML_ParserFree(pParser);
	if(context.pDs)   // Happens, for example, if vector has no components
		del_DasDs(context.pDs);
	das_error(context.nDasErr, context.sErrMsg);
	return NULL;
}
