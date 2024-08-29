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

#define DS_XML_MAXERR 512

#define _UNIT_BUF_SZ 127
#define _NAME_BUF_SZ 63
#define _TYPE_BUF_SZ 23
#define _VAL_SEMANTIC_SZ 16
#define _VAL_STOREAGE_SZ 12
#define _VAL_COMP_LBL_SZ ((DASFRM_NAME_SZ + 1) * 3)
#define _VAL_UNDER_SZ 64 /* Should be enough room for most variables */

#define _VAL_ENC_TYPE_SZ 8
#define _VAL_FILL_SZ 48
#define _VAL_TERM_SZ 48
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
	
	bool bInPropList;
	bool bInProp;
	char sPropUnits[_UNIT_BUF_SZ+1];
	char sPropName[_NAME_BUF_SZ+1];
	char sPropType[_TYPE_BUF_SZ+1];
	DasAry aPropVal;

	/* saving attributes so they can be used when var creation is ready... */
	bool bInVar;
	enum var_type varCategory; 
	das_val_type varItemType;  /* The type of values to store in the array */
	
	int varIntRank; /* Only 0 or 1 are handled right now. Tensors & point spreads will need more */
	int nVarComp;
	das_units varUnits;
	char varUse[DASDIM_ROLE_SZ];
	char valSemantic[_VAL_SEMANTIC_SZ];   /* "real", "int", "datetime", "string", etc. */
	char valStorage[_VAL_STOREAGE_SZ];
	char varFrameType[DASFRM_TYPE_SZ];    /* For in-promptu creation of frames */
	char varCompDirs[DASFRM_MAX_DIRS][DASFRM_NAME_SZ];

	char varCompLbl[_VAL_COMP_LBL_SZ]; /* HACK ALERT: Temporary hack for dastelem output */

	int8_t aVarMap[DASIDX_MAX];

	/* Stuff needed for sequence vars */
	ubyte aSeqMin[_VAL_SEQ_CONST_SZ];     /* big enough to hold a double */
	ubyte aSeqInter[_VAL_SEQ_CONST_SZ];   

	/* Stuff needed for any array var */
	DasAry* pCurAry;

	/* Stuff needed only for packet data array vars */
	char sValEncType[_VAL_ENC_TYPE_SZ];  
	int nPktItems;
	int nPktItemBytes;
	char sPktFillVal[_VAL_FILL_SZ];

	char sValTerm[_VAL_TERM_SZ]; 
	char sItemsTerm[_VAL_TERM_SZ];

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
	pCtx->nVarComp = 0;
	pCtx->varUnits = NULL;

	/* Hopefull compiler will reduce all this to a single op-code for memory erasure */
	memset(pCtx->varUse, 0, DAS_FIELD_SZ(context_t, varUse) );
	memset(pCtx->valSemantic, 0, DAS_FIELD_SZ(context_t, valSemantic) );
	memset(pCtx->valStorage,  0, DAS_FIELD_SZ(context_t, valStorage) );
	memset(pCtx->varFrameType, 0, DAS_FIELD_SZ(context_t, varFrameType) );
	memset(pCtx->varCompDirs, 0, DAS_FIELD_SZ(context_t, varCompDirs) );
	memset(pCtx->varCompLbl,  0, DAS_FIELD_SZ(context_t, varCompLbl) );  /* HACK ALERT */
	for(int i = 0; i < DASIDX_MAX; ++i) pCtx->aVarMap[i] = DASIDX_UNUSED;
	memset(pCtx->aSeqMin, 0, DAS_FIELD_SZ(context_t, aSeqMin));
	memset(pCtx->aSeqInter, 0, DAS_FIELD_SZ(context_t, aSeqInter));
	
	pCtx->pCurAry = NULL;  /* No longer need the array */

	memset(pCtx->sValEncType, 0, DAS_FIELD_SZ(context_t, sValEncType));
	pCtx->nPktItems = 0;
	pCtx->nPktItemBytes = 0;
	memset(pCtx->sPktFillVal, 0, DAS_FIELD_SZ(context_t, sPktFillVal));
	memset(pCtx->sValTerm, 0, DAS_FIELD_SZ(context_t, sValTerm));
	memset(pCtx->sItemsTerm, 0, DAS_FIELD_SZ(context_t, sItemsTerm));
	if(DasCodec_isValid( &(pCtx->codecHdrVals)) )
		DasCodec_deInit( &(pCtx->codecHdrVals) );
	memset(&(pCtx->codecHdrVals), 0, DAS_FIELD_SZ(context_t, codecHdrVals));
	memset(&(pCtx->aValUnderFlow), 0, DAS_FIELD_SZ(context_t, aValUnderFlow));
	pCtx->nValUnderFlowValid = 0;
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
static DasErrCode _serial_initfill(ubyte* pBuf, int nBufLen, das_val_type vt, const char* sFill){
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

	strncpy(pCtx->sPropType, "string", _TYPE_BUF_SZ-1);
	for(int i = 0; psAttr[i] != NULL; i+=2){
		if(strcmp(psAttr[i],"type") == 0)
			strncpy(pCtx->sPropType, psAttr[i+1],_TYPE_BUF_SZ-1);
		else if(strcmp(psAttr[i],"name") == 0)
			strncpy(pCtx->sPropName, psAttr[i+1],_NAME_BUF_SZ-1);
		else if(strcmp(psAttr[i],"units") == 0)
			strncpy(pCtx->sPropUnits, psAttr[i+1],_UNIT_BUF_SZ-1);
		else{
			const char* sEl = (pCtx->pCurDim == NULL) ? "dataset" : (
				(pCtx->pCurDim->dtype == DASDIM_DATA) ? "data" : "coord"
			);
			char sBuf[64] = {'\0'};
			if(pCtx->pCurDim == NULL)
				snprintf(sBuf, 63, " ID %02d", pCtx->nPktId);
			else
				snprintf(sBuf, 63, " '%s' in dataset ID %02d", DasDim_id(pCtx->pCurDim), pCtx->nPktId);
			pCtx->nDasErr = das_error(DASERR_SERIAL, 
				"Unknown property attribute '%s' in properties for <%s>%s",
				psAttr[i], sEl, sBuf
			);
			return;
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

	for(int i = 0; psAttr[i] != NULL; i+=2){
		if(strcmp(psAttr[i],"physDim")==0)     sPhysDim = psAttr[i+1];
		else if(strcmp(psAttr[i],"name")==0)   sName    = psAttr[i+1];
		else if(strcmp(psAttr[i],"frame")==0)  sFrame   = psAttr[i+1];
		else if((strcmp(psAttr[i],"axis")==0) &&(psAttr[i+1][0] != '\0')) 
			strncpy(sAxis, psAttr[i+1], 47);
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
	
	if((dt == DASDIM_COORD) && (sAxis[0] == '\0')){
		pCtx->nDasErr = das_error(DASERR_SERIAL, 
			"Attribute \"axis\" missing for physical dimension %s in dataset ID %d", 
			sPhysDim, id
		);
		return;
	}

	/* We have required items, make the dim */
	DasDim* pDim = new_DasDim(sPhysDim, sName, dt, DasDs_rank(pCtx->pDs));

	/* Optional items */
	if(sAxis[0] != '\0'){
		char* sBeg = sAxis;
		char* sEnd = NULL;
		int iAxis = 0;
		while((*sBeg != '\0')&&(iAxis < DASDIM_AXES)){
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

	for(int i = 0; psAttr[i] != NULL; i+=2){
		if(strcmp(psAttr[i], "use") == 0)
			strncpy(pCtx->varUse, psAttr[i+1], DASDIM_ROLE_SZ-1);

		/* For now allow both semantic and valType, but valType doesn't validate */
		else if(
			(strcmp(psAttr[i], "semantic") == 0)||(strcmp(psAttr[i], "valType") == 0)
		)    /* Partial value type, need pkt */
			strncpy(pCtx->valSemantic, psAttr[i+1], _VAL_SEMANTIC_SZ-1);/* encoding details to decide */
		else if(strcmp(psAttr[i], "storage") == 0)
			strncpy(pCtx->valStorage, psAttr[i+1], _VAL_STOREAGE_SZ-1);
		else if(strcmp(psAttr[i], "index") == 0)
			strncpy(sIndex, psAttr[i+1], 31);
		else if(strcmp(psAttr[i], "units") == 0)
			pCtx->varUnits = Units_fromStr(psAttr[i+1]);
		else if(strcmp(psAttr[i], "vecClass") == 0){
			strncpy(pCtx->varFrameType, psAttr[i+1], DASFRM_TYPE_SZ-1);
			pCtx->varFrameType[DASFRM_TYPE_SZ-1] = '\0';
		}

		/* Temporarily ignore values that are running around in wild */
		else
			daslog_warn_v(
				"Unknown attribute %s in <%s> for dataset ID %02d", psAttr[i], sVarElType, id
			);
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

	/* If this is a vector, mention that we have 1 internal index */
	if(strcmp(sVarElType, "vector") == 0)
		pCtx->varIntRank = 1;

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

	pCtx->bInVar = true;
}

/* ************************************************************************** */

static void _serial_onComponent(context_t* pCtx, const char** psAttr)
{
	if(pCtx->nDasErr != DAS_OKAY)  /* If an error condition is set, stop processing elements */
		return;

	/* If not in a <vector> this makes no sense */
	if((!pCtx->bInVar)||(pCtx->varIntRank != 1)){
		pCtx->nDasErr = das_error(DASERR_SERIAL, "<component> elements only allowed inside <vector>'s");
		return;
	}

	const char* sDir = NULL;
	const char* sName = NULL;
	for(int i = 0; psAttr[i] != NULL; i+=2){
		if(strcmp(psAttr[i], "dir") == 0){
			sDir = psAttr[i+1];
			if(sDir[0] == '\0'){
				pCtx->nDasErr = das_error(DASERR_SERIAL, 
					"Empty direction attribute for <component> of <vector> in phyDim %s of dataset ID %d",
					DasDim_id(pCtx->pCurDim), pCtx->nPktId
				);
				return;
			}
		}

		/* HACK ALERT: Remove when dastelem is updated to new-style frames */
		else if(strcmp(psAttr[i], "name") == 0){
			sName = psAttr[i+1];
			if(sName[0] == '\0'){
				pCtx->nDasErr = das_error(DASERR_SERIAL,
					"Empty name attribute for <component> of <vector> in physDim %s of dataset ID %d",
					DasDim_id(pCtx->pCurDim), pCtx->nPktId
				);
			}
		}
		/* End HACK ALERT */
		else 
			daslog_warn_v(
				"Unknown attribute %s in <component> for dataset ID %02d", psAttr[i], pCtx->nPktId
			);
	}

	/* HACK ALERT: Just use name as a synonym for dir for now */
	if((sDir == NULL )&&(sName != NULL)){
		sDir = sName;
	}

	if((sDir != NULL)&&(sDir[0] != '\0')){ /* Hack alert, remove 'if' check once dastelem is fixed */ 

		/* HACK ALERT, reenable this once dastelem is fixed ...
		pCtx->nDasErr = das_error(DASERR_SERIAL, "Attribute 'dir' missing from <component>"
			" of <vector> in phyDim %s of dataset ID %d", DasDim_id(pCtx->pCurDim), pCtx->nPktId
		);
		*/

		/* Just save off the directions for now, make sure the same one isn't written twice */	
		for(int i = 0; i < pCtx->nVarComp; ++i){
			if(strcmp(pCtx->varCompDirs[i], sDir) == 0){
				pCtx->nDasErr = das_error(DASERR_SERIAL, 
					"<component> has a repeated direction \"%s\" in the same frame."
				);
				return;
			}
		}
		strncpy(pCtx->varCompDirs[pCtx->nVarComp], sDir, DASFRM_NAME_SZ-1);
	}

	/* HACK ALERT: Remove when dastelem is updated to new-style frames */
	if((sName != NULL)&&(sName[0] != '\0')){
		for(int i = 0; i < pCtx->nVarComp; ++i){
			if(strstr(pCtx->varCompLbl, sName) != NULL){
				pCtx->nDasErr = das_error(DASERR_SERIAL,
					"<component> has a repeated name \"%s\" in the same frame."
				);
				return;
			}
		}
		if(pCtx->nVarComp > 0)
			strncat(pCtx->varCompLbl, "|", _VAL_COMP_LBL_SZ - strlen(pCtx->varCompLbl));
		strncat(pCtx->varCompLbl, sName, _VAL_COMP_LBL_SZ - strlen(pCtx->varCompLbl));
	}
	/* end HACK ALERT */

	pCtx->nVarComp += 1;

	pCtx->aVarMap[DasDs_rank(pCtx->pDs)] = pCtx->nVarComp;

	/* TODO: I need to increase the rank for strings too, not sure what to do there */
}

/* ************************************************************************** */
/* Create a seequence item */
static void _serial_onSequence(context_t* pCtx, const char** psAttr)
{
	const char* sMin = "0";
	const char* sInter = NULL;

	if(pCtx->nDasErr != DAS_OKAY) /* Stop parsing if hit an error */
		return;

	if(pCtx->varIntRank > 0){
		pCtx->nDasErr = das_error(DASERR_NOTIMP, "Sequences not yet supported for vectors");
		return;
	}

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
		if(strcmp(pCtx->valSemantic, "real")) 
			strncpy(pCtx->valStorage, "double", _VAL_STOREAGE_SZ);
		else if(strcmp(pCtx->valSemantic, "int"))
			strncpy(pCtx->valStorage, "int", _VAL_STOREAGE_SZ);
		else if(strcmp(pCtx->valSemantic, "bool"))
			strncpy(pCtx->valStorage, "byte", _VAL_STOREAGE_SZ);
		else if(strcmp(pCtx->valSemantic, "datetime")){
			if(pCtx->varUnits == UNIT_TT2000)
				strncpy(pCtx->valStorage, "long", _VAL_STOREAGE_SZ);
			else
				strncpy(pCtx->valStorage, "double", _VAL_STOREAGE_SZ);
		}
		else if(strcmp(pCtx->valSemantic, "string"))
			strncpy(pCtx->valStorage, "utf8", _VAL_STOREAGE_SZ);
		else{
			strncpy(pCtx->valStorage, "ubyte*", _VAL_STOREAGE_SZ);
		}
	}

	/* Item type can't be set when the variable opens because we could bubble it
	 * up from the packet description */
	pCtx->varItemType = das_vt_fromStr(pCtx->valStorage);

	DasErrCode nRet; ;
	if((nRet = das_value_fromStr(pCtx->aSeqMin, _VAL_SEQ_CONST_SZ, pCtx->varItemType, sMin)) != 0){
		pCtx->nDasErr = nRet;
		return;
	}
	if((nRet = das_value_fromStr(pCtx->aSeqInter, _VAL_SEQ_CONST_SZ, pCtx->varItemType, sInter)) != 0){
		pCtx->nDasErr = nRet;
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
	char sAryId[64] = {'\0'};

	snprintf(sAryId, 63, "%s_%s", pCtx->varUse, DasDim_id(pCtx->pCurDim));

	/* Determine the array indexes from the variable indexes */
	size_t aShape[DASIDX_MAX];
	int nAryRank = 0; /* <-- current array index then bumped to the rank */
	int nDsRank = DasDs_rank(pCtx->pDs);
	for(int i = 0; i < nDsRank; ++i){
		if(pCtx->aVarMap[i] == DASIDX_UNUSED)
			continue;

		if(pCtx->aExtShape[pCtx->aVarMap[i]] == DASIDX_RAGGED){
			aShape[nAryRank] = 0;
		}
		else{
			if(pCtx->aExtShape[pCtx->aVarMap[i]] <= 0){
				return das_error(DASERR_SERIAL,
					"Invalid array map for variable %s:%s in dataset id %d",
					DasDim_id(pCtx->pCurDim), pCtx->varUse, pCtx->nPktId
				);
			}
			aShape[nAryRank] = pCtx->aExtShape[pCtx->aVarMap[i]];
		}

		++nAryRank;
	}

	/* Force first array index to "undefined" so that streaming always works */
	/* TODO: For speed we should use pre-allocated arrays, but that would 
	         required DasAry_putIn to handle index rolling, which it doesn't
	         right now */
	aShape[0] = 0;

	das_val_type vt;
	if(pCtx->valStorage[0] != '\0'){
		vt = das_vt_fromStr(pCtx->valStorage);
	}
	else{
		vt = das_vt_store_type(pCtx->sValEncType, pCtx->nPktItemBytes, pCtx->valSemantic);

		if(vt == vtUnknown){
			return das_error(DASERR_SERIAL,
				"Attribute 'storage' missing for non-string values encoded "
				"as text for variable %s:%s in dataset ID %d",
				DasDim_id(pCtx->pCurDim), pCtx->varUse, pCtx->nPktId
			);
		}
	}

	if(vt == vtUnknown){
		return das_error(DASERR_SERIAL,
			"Could not determine the value type for new array in variable "
			"%s:%s in dataset ID %d", 	DasDim_id(pCtx->pCurDim), pCtx->varUse, 
			pCtx->nPktId
		);
	}

	/* Dealing with internal structure */
	uint32_t uFlags = 0;

	if(pCtx->varIntRank > 0){	
		/* Internal structure due to vectors */
		if(pCtx->nVarComp > 1){
			aShape[nAryRank] = pCtx->nVarComp;
			++nAryRank;
		}
		else{
			/* Internal structure must be due to text or byte strings */
			if(vt == vtByteSeq){
				vt = vtByte;
				aShape[nAryRank] = 0;
				++nAryRank;
				uFlags = D2ARY_AS_SUBSEQ;
			}
			else if(vt == vtText){
				vt = vtByte;
				aShape[nAryRank] = 0;
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
		int nRet = _serial_initfill(aFill, DATUM_BUF_SZ, vt, pCtx->sPktFillVal);
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
		if(strcmp(psAttr[i], "itemBytes") == 0){
			if(psAttr[i+1][0] == '*'){
				pCtx->nPktItemBytes = -1;
				nValTermStat = 0x1;
			}
			if(sscanf(psAttr[i+1], "%d", &(pCtx->nPktItemBytes)) != 1){
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
		if(strcmp(psAttr[i], "valTerm") == 0){
			strncpy(pCtx->sValTerm, psAttr[i+1], _VAL_TERM_SZ-1);
			nValTermStat |= 0x2;
			continue;
		}
		if(strcmp(psAttr[i], "itemsTerm") == 0){
			strncpy(pCtx->sItemsTerm, psAttr[i+1], _VAL_TERM_SZ-1);
			nItemsTermStat |= 0x2;
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

	/* If the values aren't fixed length, I need a value terminator */
	if(((nValTermStat & 0x1) == 0x1 )&&(nValTermStat != 0x2)){
		pCtx->nDasErr = das_error(DASERR_SERIAL,
			"Attribute 'valTerm' missing for variable length values in <packet> for "
			"%s:%s in dataset ID %02d", DasDim_id(pCtx->pCurDim), pCtx->varUse, 
			pCtx->nPktId
		);
	}

	/* If I'm the last item set in the packet I can get away with no terminator */
	if(((nItemsTermStat & 0x1) == 0x1 )&&(nItemsTermStat != 0x2)){
		pCtx->nDasErr = das_error(DASERR_SERIAL,
			"Attribute 'itemsTerm' missing for variable number of items per "
			"packet in dataset ID %02d", pCtx->nPktId
		);
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


	for(int i = 0; psAttr[i] != NULL; i+=2){
		pCtx->nDasErr = das_error(DASERR_NOTIMP, 
			"Attributes of <values> element not yet supported in dataset ID %d", pCtx->nPktId
		);
		return;
	}

	strncpy(pCtx->sValEncType, "utf8", _VAL_ENC_TYPE_SZ-1);

	DasErrCode nRet = _serial_makeVarAry(pCtx, NO_FILL);
	if(nRet != DAS_OKAY){
		pCtx->nDasErr = nRet;
		return;
	}

	/* By default utf8 is whitespace separated, could provide a separator here... */
	nRet = DasCodec_init(
		&(pCtx->codecHdrVals), pCtx->pCurAry, pCtx->valSemantic, "utf8", 
		DASIDX_RAGGED, 0, pCtx->varUnits
	);
	if(nRet != DAS_OKAY)
		pCtx->nDasErr = nRet;
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
	if(strcmp(sElement, "component") == 0){
		_serial_onComponent(pCtx, psAttr);
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
				memcpy(pCtx->aValUnderFlow + pCtx->nValUnderFlowValid, sChars, n);
			}
		
			/* Read the underflow buffer then clear it */
			/* TODO:  make DasAry_putAt handle index rolling as well */
			int nValsRead = 0;
			nUnRead = DasCodec_decode(&(pCtx->codecHdrVals), pCtx->aValUnderFlow, nLen, -1, &nValsRead);
			if(nUnRead < 0){
				pCtx->nDasErr = -1 * nUnRead;
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
		'\0',                                                    // 
		pCtx->sPropUnits[0] == '\0' ? NULL : Units_fromStr(pCtx->sPropUnits),
		3 /* Das3 */
	);

	memset(&(pCtx->sPropType), 0, _TYPE_BUF_SZ);
	memset(&(pCtx->sPropName), 0, _NAME_BUF_SZ);
	memset(&(pCtx->sPropUnits), 0, _UNIT_BUF_SZ);
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

static void _serial_onCloseVar(context_t* pCtx)
{
	if(pCtx->nDasErr != DAS_OKAY) /* Stop processing on error */
		return;

	/* If this is a vector and we had no components, that's a problem */
	if((pCtx->varIntRank == 1)&&(pCtx->nVarComp == 0)){
		pCtx->nDasErr = das_error(DASERR_SERIAL, 
			"No components provided for vector %s of dimension %s for dataset ID %d",
			pCtx->varUse, DasDim_id(pCtx->pCurDim), pCtx->nPktId
		);
		goto NO_CUR_VAR;
	}

	/* Create the variable, for vector variables, we may need to create an
	   implicit frame as well */
	DasVar* pVar = NULL;
	DasErrCode nRet = DAS_OKAY;

	/* May not matter if variable is a scalar */
	const char* sVecClass = pCtx->varFrameType[0] == '\0' ? "cartesian" : pCtx->varFrameType;

	if(pCtx->varIntRank == 1){

		/* HACK ALERT: Pick up names put in the wrong place and put them in a property instead */
		if(pCtx->varCompLbl[0] != '\0')
			DasDesc_flexSet((DasDesc*)(pCtx->pCurDim),  
				"stringArray", 0, "compLabel", pCtx->varCompLbl, '|', NULL, DASPROP_DAS3
			);

		/* end HACK ALERT */

		if(DasDim_getFrame(pCtx->pCurDim) == NULL){

			/* HACK ALERT */
			DasDim_setFrame(pCtx->pCurDim, DASFRM_NULLNAME);
			/*
			pCtx->nDasErr = das_error(DASERR_SERIAL, 
				"<data> or <coords> holding <vectors> must have a defined coordinate frame"
			);
			goto NO_CUR_VAR;
			
			end HACK ALERT */
		}

		const DasFrame* pFrame = DasStream_getFrameByName(pCtx->pSd, DasDim_getFrame(pCtx->pCurDim));
		if(pFrame == NULL){

			/* If the stream had no frame, generate one for the given frame name.

			  BIG WARNING:
			     You want to use explicit frames in your streams... you really do.  

			     That is because MAGnetometer people often provide *cartesian* 
			     vectors whose orthogonal unit vectors are set by the instantaneous
			     location in a *non-cartesian* coordinate frame!  

			     In order to properly take the magnitude of a vector you have to know
			     it's vector class and this may be different from the vector frame.
			     I know... wierd, right.
			 */
			int iFrame = DasStream_newFrameId(pCtx->pSd);
			if(iFrame < 0){
				pCtx->nDasErr = -1*iFrame;
				goto NO_CUR_VAR;
			}

			DasFrame* pMkFrame = DasStream_createFrame(
				pCtx->pSd, iFrame, DasDim_getFrame(pCtx->pCurDim), sVecClass, 0
			);
			DasDesc_setStr((DasDesc*)pMkFrame, "title", "Autogenerated Frame");

			for(int i = 0; i < pCtx->nVarComp; ++i){
				if((nRet = DasFrame_addDir(pMkFrame, pCtx->varCompDirs[i])) != DAS_OKAY){
					pCtx->nDasErr = nRet;
					goto NO_CUR_VAR;
				}
			}
			pFrame = pMkFrame; /* Make Frame Constant Again */
		}

		int8_t iFrame =  DasStream_getFrameId(pCtx->pSd, DasDim_getFrame(pCtx->pCurDim));
		if(iFrame < 0){
			pCtx->nDasErr = das_error(DASERR_SERIAL, 
				"No frame named %s is defined for the stream", DasDim_getFrame(pCtx->pCurDim)
			);
			goto NO_CUR_VAR;
		}

		ubyte aDirs[DASFRM_MAX_DIRS];
		for(int i = 0; i < pCtx->nVarComp; ++i){
			assert(i < DASFRM_MAX_DIRS);

			aDirs[i] = DasFrame_idxByDir(pFrame, pCtx->varCompDirs[i]);
			if(aDirs[i] < 0){
				pCtx->nDasErr = das_error(DASERR_SERIAL, 
					"No direction named %s in frame %s for the stream",
					pCtx->varCompDirs[i], DasFrame_getName(pFrame)
				);
				goto NO_CUR_VAR;
			}
		}
		
		/* Get the array for this variable */
		if(!pCtx->pCurAry){
			pCtx->nDasErr = das_error(DASERR_SERIAL, "Vector sequences are not yet supported");
			goto NO_CUR_VAR;
		}

		// Use the given vector class here, even if frame is a different class
		pVar = new_DasVarVecAry(
			pCtx->pCurAry, DasDs_rank(pCtx->pDs), pCtx->aVarMap, 1, /* internal rank = 1 */
			pFrame->name, iFrame, das_str2frametype(sVecClass), pCtx->nVarComp, aDirs
		);
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
			pVar = new_DasVarArray(pCtx->pCurAry, DasDs_rank(pCtx->pDs), pCtx->aVarMap, 0);
		}
	}
	DasVar_setSemantic(pVar, pCtx->valSemantic);

	/* If this is an array var type & it is record varying, add a packet decoder */
	if((pCtx->varCategory == D2V_ARRAY)&&(pCtx->aVarMap[0] != DASIDX_UNUSED)){

		int nRet;

		if((pCtx->nPktItemBytes < 0)||(pCtx->nPktItems < 0)){
			nRet = das_error(DASERR_NOTIMP, "Setting up variable length decodings is not yet implemented");
		}
		else{
			nRet = DasDs_addFixedCodec(
				pCtx->pDs, DasAry_id(pCtx->pCurAry), pCtx->valSemantic,
				pCtx->sValEncType, pCtx->nPktItemBytes, pCtx->nPktItems
			);
		}
		if(nRet != DAS_OKAY){
			pCtx->nDasErr = nRet;
			goto NO_CUR_VAR;
		}
	}

	if(pVar == NULL){
		pCtx->nDasErr = DASERR_VAR; 
		goto NO_CUR_VAR;
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

static void _serial_onCloseDim(context_t* pCtx)
{
	/* If this dim has a reference and an offset, but no center, add the center */
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

/* ************************************************************************** */

static void _serial_xmlElementEnd(void* pUserData, const char* sElement)
{
	context_t* pCtx = (context_t*)pUserData;

	/* If I've hit an error condition, stop processing stuff */
	if(pCtx->nDasErr != DAS_OKAY)
		return;

	/* Closing properties */
	if(sElement[0] == 'p' && sElement[1] == '\0'){

		DasDesc* pDest = pCtx->pCurDim != NULL ? (DasDesc*)pCtx->pCurDim : (DasDesc*)pCtx->pDs;
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
		_serial_onCloseDim(pCtx);
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

	if((pParent == NULL)||(((DasDesc*)pParent)->type != STREAM)){
		das_error(DASERR_SERIAL, "Stream descriptor must appear before a dataset descriptor");
		return NULL;
	}

	context.pSd = (DasStream*) pParent;

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

	if((context.nDasErr == DAS_OKAY)&&(context.pDs != NULL)){
		DasAry_deInit(&(context.aPropVal)); /* Avoid memory leaks */
		return context.pDs;
	}

ERROR:
	DasAry_deInit(&(context.aPropVal)); /* Avoid memory leaks */
	XML_ParserFree(pParser);
	if(context.pDs)   // Happens, for example, if vector has no components
		del_DasDs(context.pDs);
	das_error(context.nDasErr, context.sErrMsg);
	return NULL;
}
