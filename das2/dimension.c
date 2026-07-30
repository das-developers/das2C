/* Copyright (C) 2018 - 2024 Chris Piker <chris-piker@uiowa.edu>
 *
 * This file is part of das2C, the Core Das2 C Library.
 *
 * Das2C is free software; you can redistribute it and/or modify it under
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

#define _das_dimension_c_

#include <string.h>
#ifdef _WIN32
#define strcasecmp _stricmp
#else
#include <strings.h>
#endif
#include <ctype.h>

#include "util.h"
#include "stream.h"

const char* DASVAR_CENTER  = "center";   /* The default value */
const char* DASVAR_MIN     = "min";
const char* DASVAR_MAX     = "max";
const char* DASVAR_WIDTH   = "width";
const char* DASVAR_MEAN    = "mean";
const char* DASVAR_MEDIAN  = "median";
const char* DASVAR_MODE    = "mode";
const char* DASVAR_REF     = "reference"; 
const char* DASVAR_OFFSET  = "offset"; 
const char* DASVAR_MAX_ERR = "max_error";
const char* DASVAR_MIN_ERR = "min_error";
const char* DASVAR_STD_DEV = "std_dev";
const char* DASVAR_COUNT   = "count";
const char* DASVAR_WEIGHT  = "weight";
const char* DASVAR_NORM    = "norm";

/* Held out until we got a formalism for it
 * const char* DASVAR_SPREAD = "point_spread";
 */


bool DasDim_isKnownRole(const char* sPurpose)
{
	/* Check to see if the value given for the purpose of a variable in a 
	   dimension is one we know about */
	if(strcmp(sPurpose, DASVAR_CENTER) == 0) return true;
	if(strcmp(sPurpose, DASVAR_MAX) == 0) return true;
	if(strcmp(sPurpose, DASVAR_MIN) == 0) return true;
	if(strcmp(sPurpose, DASVAR_WIDTH) == 0) return true;
	if(strcmp(sPurpose, DASVAR_MEAN) == 0) return true;
	if(strcmp(sPurpose, DASVAR_MEDIAN) == 0) return true;
	if(strcmp(sPurpose, DASVAR_MODE) == 0) return true;
	if(strcmp(sPurpose, DASVAR_REF) == 0) return true;
	if(strcmp(sPurpose, DASVAR_OFFSET) == 0) return true;
	if(strcmp(sPurpose, DASVAR_MAX_ERR) == 0) return true;
	if(strcmp(sPurpose, DASVAR_MIN_ERR) == 0) return true;
	if(strcmp(sPurpose, DASVAR_STD_DEV) == 0) return true;
	if(strcmp(sPurpose, DASVAR_COUNT) == 0) return true;
	if(strcmp(sPurpose, DASVAR_WEIGHT) == 0) return true;
	if(strcmp(sPurpose, DASVAR_NORM) == 0) return true;

	return false;
}

int DasDim_shape(const DasDim* pThis, ptrdiff_t* pShape)
{	
	int i = 0;
	for(i = 0; i < SETIDX_MAX; ++i) pShape[i] = SETIDX_UNUSED;

	ptrdiff_t aShape[SETIDX_MAX];
	
	const DasSet* pVar = NULL;
	for(i = 0; i < pThis->uVars; ++i){
		pVar = pThis->aVars[i];
		DasSet_shape(pVar, aShape);
		
		das_varindex_merge(pThis->iFirstInternal, pShape, aShape);
	}
	
	/* Count used indices below the first internal index */
	int nUsed = 0;
	for(i = 0; i < pThis->iFirstInternal; ++i) ++nUsed;
	
	/* Mask off anything at or after the first internal index */
	for(i = pThis->iFirstInternal; i < SETIDX_MAX; ++i) 
		pShape[i] = SETIDX_UNUSED;
	
	return nUsed;
}

/* If I only have degenerate variables in this index, whole dim is degenerate */
bool DasDim_degenerate(const DasDim* pThis, int iIndex)
{
	for(size_t u = 0; u < pThis->uVars; ++u){
		if(! DasSet_degenerate(pThis->aVars[u], iIndex))
			return false;
	}
	return true;
}

ptrdiff_t DasDim_lengthIn(const DasDim* pThis, int nIdx, ptrdiff_t* pLoc)
{
	int nLengthIn = SETIDX_UNUSED;
	int nVarLenIn = SETIDX_UNUSED;
	
	/* The simple function below fails if only a REFERENCE and OFFSET are
	 * specifed but not the CENTER variable */
	
	const DasSet* pVar = NULL;
	for(int i = 0; i < pThis->uVars; ++i){
		pVar = pThis->aVars[i];
		nVarLenIn = DasSet_lengthIn(pVar, nIdx, pLoc);
		
		nLengthIn = das_varlength_merge(nLengthIn, nVarLenIn);
	}
	return nLengthIn;
}

const char* das_role_fromStr(const char* sRole)
{
	if(sRole == NULL) return NULL;

	/* Substitutions go here for common to connocical names */
	if(strcmp(sRole, "average") == 0) return DASVAR_MEAN;

	/* Roles are free-form, so anything else is returned untouched.  This is a
	   canonicalizer, not a validator; see DasDim_isKnownRole(). */
	return sRole;
}

/* Display/serialization order: the value first, then what bounds it, then the
   bookkeeping a reducer needs, then the error terms.  Anything unrecognized
   sorts last, which is also where a caller's custom role lands. */
int _DasDim_varOrder(const char* sRole){
	if(strcmp(sRole, DASVAR_CENTER) == 0)    return 0;
	if(strcmp(sRole, DASVAR_MEAN) == 0)      return 1;
	if(strcmp(sRole, DASVAR_MEDIAN) == 0)    return 2;
	if(strcmp(sRole, DASVAR_MODE) == 0)      return 3;
	if(strcmp(sRole, DASVAR_MIN) == 0)       return 4;
	if(strcmp(sRole, DASVAR_MAX) == 0)       return 5;
	if(strcmp(sRole, DASVAR_REF) == 0)       return 6;
	if(strcmp(sRole, DASVAR_OFFSET) == 0)    return 7;
	if(strcmp(sRole, DASVAR_WIDTH) == 0)     return 8;
	if(strcmp(sRole, DASVAR_COUNT) == 0)     return 9;
	if(strcmp(sRole, DASVAR_WEIGHT) == 0)    return 10;
	if(strcmp(sRole, DASVAR_MAX_ERR) == 0)   return 11;
	if(strcmp(sRole, DASVAR_MIN_ERR) == 0)   return 12;
	if(strcmp(sRole, DASVAR_STD_DEV) == 0)   return 13;
	if(strcmp(sRole, DASVAR_NORM) == 0)      return 14;
	return 15;
}

char* DasDim_toStr(const DasDim* pThis, char* sBuf, int nLen)
{
	size_t u = 0;
	char* pWrite = sBuf;
	
	const char* sDimType = "Data";
	if(pThis->dtype == DASDIM_COORD) sDimType = "Coordinate";
	int nWritten = snprintf(sBuf, nLen - 1, "%s Dimension: %s (%s)",
			                  sDimType, pThis->sId, pThis->sDim);
	pWrite += nWritten; nLen -= nWritten;
	if(nLen < 40) return sBuf;

	if(pThis->axes[0][0] != '\0'){

		for(int iAxis = 0; iAxis < DASDIM_NAXES; ++iAxis){
			if(pThis->axes[iAxis][0] != '\0'){
				if(iAxis == 0){
					strcpy(pWrite, " | axis: ");
					pWrite += 9; nLen -= 9;
				}
				else{
					*pWrite = ','; ++pWrite; --nLen;
				}
				*pWrite = pThis->axes[iAxis][0]; ++pWrite; --nLen;
				if(pThis->axes[iAxis][1] != '\0'){
					*pWrite = pThis->axes[iAxis][1]; ++pWrite; --nLen;
				}
			}
		}
	}

	*pWrite = '\n'; ++pWrite; --nLen;
	
	/* Larry wanted the properties printed as well */
	char* pSubWrite = DasDesc_info((DasDesc*)pThis, pWrite, nLen, "   ");
	bool bReturn = (pSubWrite != pWrite);
	nLen -= (pSubWrite - pWrite);
	pWrite = pSubWrite;
	
	if(nLen < 4) return sBuf;
	if(bReturn){ *pWrite = '\n'; ++pWrite; --nLen; }
	
	/* Yea this is order N^2, but we never have that many variables in a dim */
	/* Do the recognized variables first */
	char sInfo[256] = {'\0'};
	for(int nOrder = 0; nOrder < 16; ++nOrder){
		
		for(u = 0; u < pThis->uVars; ++u){
			
			if(_DasDim_varOrder(pThis->aRoles[u]) == nOrder ){
			
				DasSet_toStr(pThis->aVars[u], sInfo, 255);
				nWritten = snprintf(pWrite, nLen - 1, "   Variable: %s | %s\n", 
				                 pThis->aRoles[u], sInfo);
				pWrite += nWritten; nLen -= nWritten;
				if(nLen < 1) return sBuf;
			}
		}
	}
	return sBuf;
}

/* Adding variables ******************************************************* */

bool DasDim_addVar(DasDim* pThis, const char* role, DasSet* pVar)
{
	/* All variables in a dimesnion have to have a role, which is 
	   just a non-empty string that we may or may not understand */
	if((role == NULL)||(role[0] == '\0')){
		das_error(DASERR_DIM,
			"A variable's role in dimension '%s' can't be empty", pThis->sId
		);
		return false;
	}
	if(pVar == NULL){
		das_error(DASERR_DIM, "NULL variable for role '%s'", role);
		return false;
	}

	for(size_t u = 0; u < pThis->uVars; ++u){
		if(strcasecmp(pThis->aRoles[u], role) == 0){
			das_error(DASERR_DIM, "Role %s is already defined", role);
			return false;
		}
	}
	if(pThis->uVars == DASDIM_MAXVAR){
		das_error(
			DASERR_DIM, "Maximum number of variables in a dimension %d exceeded.  "
			"The limit was chosen arbitrarily and can be changed or even removed "
			"altogether.  Contact a das2C maintainer", DASDIM_MAXVAR
		);
		return false;
	}
	
	strncpy(pThis->aRoles[pThis->uVars], role, DASDIM_ROLE_SZ-1);
	pThis->aVars[pThis->uVars] = pVar;
	pThis->uVars += 1;

	pVar->base.parent = (DasDesc*)pThis;
	return true;
}

/* Getting Variables ****************************************************** */
DasSet* DasDim_getVar(DasDim* pThis, const char* sRole)
{
	for(size_t u = 0; u < pThis->uVars; ++u){
		if(strcasecmp(pThis->aRoles[u], sRole) == 0) return pThis->aVars[u];
	}
	return NULL;
}

DasSet* DasDim_getPointVar(DasDim* pThis)
{
	/* Preference order is: 
	 * 
	 *  Center, Mean, Median, Mode, 
	 * 
	 *  If min/max provided could make auto-var for Center, that can be 
	 *  tricky if min & max are epoch times, but doable
	 * 
	 *  If reference/offset provided could make auto-var for center
	 */
	DasSet* pVar = DasDim_getVar(pThis, DASVAR_CENTER);
	if(pVar != NULL) return pVar;
	
	pVar = DasDim_getVar(pThis, DASVAR_MEAN);
	if(pVar != NULL) return pVar;
	
	pVar = DasDim_getVar(pThis, DASVAR_MEDIAN);
	if(pVar != NULL) return pVar;
	
	pVar = DasDim_getVar(pThis, DASVAR_MODE);
	
	/* We can't make new vars here since we are a constant pointer, but 
	 * a function to do this could be added...*/
	
	return pVar; /* can be null */
}


/* Removing Variables ***************************************************** */

DasSet* DasDim_popVar(DasDim* pThis, const char* role){
	
	int iRm = -1;
	void* pDest = NULL;
	void* pSrc = NULL;
	DasSet* pRet = NULL;
	
	for(size_t u = 0; u < pThis->uVars; ++u){
		if(strcmp(pThis->aRoles[u], role) == 0){
			pRet = pThis->aVars[u];
			iRm = u;
		}
	}
	
	int nShift = 0;
	if(pRet != NULL){
		/* Shift down the retained data if needed */
		nShift = (pThis->uVars - 1) - iRm;
		if(nShift > 0){
			pDest = pThis->aVars + iRm;
			pSrc = pThis->aVars + iRm + 1;
			memmove(pDest, pSrc, nShift * sizeof(void*));
			
			pDest = ((char*)pThis->aRoles) + (32 * iRm);
			pSrc = ((char*)pThis->aRoles) + (32 * (iRm + 1));
			memmove(pDest, pSrc, nShift * sizeof(char) * 32 );
		}
		
		/* always clear the last item */
		pThis->aVars[pThis->uVars - 1] = NULL;

		pDest = ((char*)pThis->aRoles) + DASDIM_ROLE_SZ * (pThis->uVars -1);
		memset(pDest, 0, DASDIM_ROLE_SZ);

		pThis->uVars -= 1;

		/* The set is no longer ours, so stop claiming to be its parent. */
		if(((DasDesc*)pRet)->parent == (DasDesc*)pThis)
			((DasDesc*)pRet)->parent = NULL;
	}

	return pRet;
}

/* Axes (the dim-level frame cascade is gone; frames bind on each set's
   geovec formalism, so multi-axis validity is a verify-level rule now) */

int DasDim_numAxes(const DasDim* pThis)
{
	int nAxes = 0;
	for(int i = 0; i < DASDIM_NAXES; ++i){
		if(pThis->axes[i][0] != '\0')
			++nAxes;
	}
	return (nAxes == 0) ? 1 : nAxes;
}

DasErrCode DasDim_setAxis(DasDim* pThis, int iAxis, const char* sAxis)
{
	if((iAxis < 0)||(iAxis >= DASDIM_NAXES))
		return das_error(DASERR_DIM, "Axis index %d is not valid for a dimension", iAxis);

	strncpy(pThis->axes[iAxis], sAxis, DASDIM_AXLEN - 1);
	return DAS_OKAY;
}

void DasDim_setAxes(DasDim* pThis, const DasDim* pOther)
{
	pThis->primary = pOther->primary;
	memcpy(pThis->axes, pOther->axes, DASDIM_NAXES*DASDIM_AXLEN);
}


/* Construction / Destruction ********************************************* */

DasDim* new_DasDim(const char* sDim, const char* sId, enum dim_type dtype, int nDsRank)
{
	DasDim* pThis = (DasDim*)calloc(1, sizeof(DasDim));
	if(pThis == NULL){
		das_error(DASERR_DIM, "Out of memory"); return NULL;
	}

	DasDesc_init((DasDesc*)pThis, PHYSDIM);
	
	pThis->dtype = dtype;
	das_assert_valid_id(sDim);
	strncpy(pThis->sDim, sDim, DAS_MAX_ID_BUFSZ-1);
	
	if(sId && sId[0] != '\0')  /* Just repeat as dim name if no name given */
		strncpy(pThis->sId, sId, DAS_MAX_ID_BUFSZ-1);
	else
		strncpy(pThis->sId, sDim, DAS_MAX_ID_BUFSZ-1);

	pThis->iFirstInternal = nDsRank;
	
	return pThis;
}


void del_DasDim(DasDim* pThis){
	size_t u;
	for(u = 0; u < pThis->uVars; ++u)
		DasSet_decRef(pThis->aVars[u]);

	DasDesc_freeProps(&(pThis->base));
	free(pThis);
}

/* ************************************************************************* */
/* Encoding as XML */

DasErrCode DasDim_encode(DasDim* pThis, DasBuf* pBuf)
{
	DasErrCode nRet = DAS_OKAY;
	char sAxis[64] = {'\0'};
	const char* sType = "data";
	if(DasDim_type(pThis) == DASDIM_COORD){
		sType = "coord";
		char* pPut = sAxis;
		size_t uLen = 0;
		int i = 0;
		for(i = 0; i < DASDIM_NAXES; ++i){
			uLen = strlen((const char*) pThis->axes[i]);
			if(uLen > 0){
				if(pPut != sAxis){ *pPut = ';'; ++pPut;}
				strncpy(pPut, (const char*) pThis->axes[i], uLen);
				pPut += uLen;
			}	
		}
	}
	DasBuf_printf(pBuf, "  <%s physDim=\"%s\" name=\"%s\"",
		sType, DasDim_dim(pThis), DasDim_id(pThis)
	);

	if((DasDim_type(pThis) == DASDIM_COORD)&&(sAxis[0] != '\0')){
		if(DasDim_isPrimeCoord(pThis))
			DasBuf_printf(pBuf, " axis=\"%s\"", sAxis);
		else
			DasBuf_printf(pBuf, " annotation=\"%s\"", sAxis);
	}
	
	DasBuf_puts(pBuf, ">\n");

	if( (nRet = DasDesc_encode3((DasDesc*)pThis, pBuf, "    ")) != 0)
		return nRet;	

	/* Loop over all vars */
	size_t uVars = DasDim_numVars(pThis);
	DasSet* pVar = NULL;
	const char* sRole = NULL;
	for(size_t u = 0; u < uVars; ++u){
		pVar = DasDim_getVarByIdx(pThis, u);
		sRole = DasDim_getRoleByIdx(pThis, u);
		nRet = DasSet_encode(pVar, sRole, pBuf);
		if(nRet != DAS_OKAY)
			return nRet;
	}

	DasBuf_printf(pBuf, "  </%s>\n", sType);
	return nRet;
}

