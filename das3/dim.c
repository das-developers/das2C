/* Copyright (C) 2018 - 2024 Chris Piker <chris-piker@uiowa.edu>
 *
 * This file is part of das2C, the Core Das C Library.
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

#include <string.h>
#ifdef _WIN32
#define strcasecmp _stricmp
#else
#include <strings.h>
#endif
#include <ctype.h>

#include "util.h"
#include "log.h"
#include "info_p.h"
#include "stream.h"
#include "dim_p.h"
#include "var_p.h"

static void _del_DasDim(DasDesc* pDesc);  /* del_DasDesc() target */

const char* DASDIM_ROLE_CENTER  = "center";   /* The default value */
const char* DASDIM_ROLE_MIN     = "min";
const char* DASDIM_ROLE_MAX     = "max";
const char* DASDIM_ROLE_WIDTH   = "width";
const char* DASDIM_ROLE_MEAN    = "mean";
const char* DASDIM_ROLE_MEDIAN  = "median";
const char* DASDIM_ROLE_MODE    = "mode";
const char* DASDIM_ROLE_REF     = "reference"; 
const char* DASDIM_ROLE_OFFSET  = "offset"; 
const char* DASDIM_ROLE_MAX_ERR = "max_error";
const char* DASDIM_ROLE_MIN_ERR = "min_error";
const char* DASDIM_ROLE_STD_DEV = "std_dev";
const char* DASDIM_ROLE_COUNT   = "count";
const char* DASDIM_ROLE_WEIGHT  = "weight";
const char* DASDIM_ROLE_NORM    = "norm";

/* Held out until we got a formalism for it
 * const char* DASDIM_ROLE_SPREAD = "point_spread";
 */


bool DasDim_isKnownRole(const char* sPurpose)
{
	/* Check to see if the value given for the purpose of a variable in a 
	   dimension is one we know about */
	if(strcmp(sPurpose, DASDIM_ROLE_CENTER) == 0) return true;
	if(strcmp(sPurpose, DASDIM_ROLE_MAX) == 0) return true;
	if(strcmp(sPurpose, DASDIM_ROLE_MIN) == 0) return true;
	if(strcmp(sPurpose, DASDIM_ROLE_WIDTH) == 0) return true;
	if(strcmp(sPurpose, DASDIM_ROLE_MEAN) == 0) return true;
	if(strcmp(sPurpose, DASDIM_ROLE_MEDIAN) == 0) return true;
	if(strcmp(sPurpose, DASDIM_ROLE_MODE) == 0) return true;
	if(strcmp(sPurpose, DASDIM_ROLE_REF) == 0) return true;
	if(strcmp(sPurpose, DASDIM_ROLE_OFFSET) == 0) return true;
	if(strcmp(sPurpose, DASDIM_ROLE_MAX_ERR) == 0) return true;
	if(strcmp(sPurpose, DASDIM_ROLE_MIN_ERR) == 0) return true;
	if(strcmp(sPurpose, DASDIM_ROLE_STD_DEV) == 0) return true;
	if(strcmp(sPurpose, DASDIM_ROLE_COUNT) == 0) return true;
	if(strcmp(sPurpose, DASDIM_ROLE_WEIGHT) == 0) return true;
	if(strcmp(sPurpose, DASDIM_ROLE_NORM) == 0) return true;

	return false;
}

int DasDim_shape(const DasDim* pThis, ptrdiff_t* pShape)
{	
	int i = 0;
	for(i = 0; i < DASIDX_MAX; ++i) pShape[i] = DASIDX_UNUSED;

	ptrdiff_t aShape[DASIDX_MAX];
	
	const DasVar* pVar = NULL;
	for(i = 0; i < pThis->uVars; ++i){
		pVar = pThis->aVars[i];
		DasVar_shape(pVar, aShape);
		
		dasshape_merge(pThis->iFirstInternal, pShape, aShape);
	}
	
	/* Count used indices below the first internal index */
	int nUsed = 0;
	for(i = 0; i < pThis->iFirstInternal; ++i) ++nUsed;
	
	/* Mask off anything at or after the first internal index */
	for(i = pThis->iFirstInternal; i < DASIDX_MAX; ++i) 
		pShape[i] = DASIDX_UNUSED;
	
	return nUsed;
}

/* If I only have degenerate variables in this index, whole dim is degenerate */
bool DasDim_isDegenerate(const DasDim* pThis, int iIndex)
{
	for(size_t u = 0; u < pThis->uVars; ++u){
		if(! DasVar_isDegenerate(pThis->aVars[u], iIndex))
			return false;
	}
	return true;
}

ptrdiff_t DasDim_lengthIn(const DasDim* pThis, int nIdx, ptrdiff_t* pAddr)
{
	int nLengthIn = DASIDX_UNUSED;
	int nVarLenIn = DASIDX_UNUSED;
	
	/* The simple function below fails if only a REFERENCE and OFFSET are
	 * specified but not the CENTER variable */
	
	const DasVar* pVar = NULL;
	for(int i = 0; i < pThis->uVars; ++i){
		pVar = pThis->aVars[i];
		nVarLenIn = DasVar_lengthIn(pVar, nIdx, pAddr);
		
		nLengthIn = dasshape_mergeLen(nLengthIn, nVarLenIn);
	}
	return nLengthIn;
}

const char* DasDim_roleFromStr(const char* sRole)
{
	if(sRole == NULL) return NULL;

	/* Substitutions go here for common to canonical names */
	if(strcmp(sRole, "average") == 0) return DASDIM_ROLE_MEAN;

	/* Roles are free-form, so anything else is returned untouched.  This is a
	   canonicalizer, not a validator; see DasDim_isKnownRole(). */
	return sRole;
}

/* Display/serialization order: the value first, then what bounds it, then the
   bookkeeping a reducer needs, then the error terms.  Anything unrecognized
   sorts last, which is also where a caller's custom role lands. */
static int _DasDim_varOrder(const char* sRole){
	if(strcmp(sRole, DASDIM_ROLE_CENTER) == 0)    return 0;
	if(strcmp(sRole, DASDIM_ROLE_MEAN) == 0)      return 1;
	if(strcmp(sRole, DASDIM_ROLE_MEDIAN) == 0)    return 2;
	if(strcmp(sRole, DASDIM_ROLE_MODE) == 0)      return 3;
	if(strcmp(sRole, DASDIM_ROLE_MIN) == 0)       return 4;
	if(strcmp(sRole, DASDIM_ROLE_MAX) == 0)       return 5;
	if(strcmp(sRole, DASDIM_ROLE_REF) == 0)       return 6;
	if(strcmp(sRole, DASDIM_ROLE_OFFSET) == 0)    return 7;
	if(strcmp(sRole, DASDIM_ROLE_WIDTH) == 0)     return 8;
	if(strcmp(sRole, DASDIM_ROLE_COUNT) == 0)     return 9;
	if(strcmp(sRole, DASDIM_ROLE_WEIGHT) == 0)    return 10;
	if(strcmp(sRole, DASDIM_ROLE_MAX_ERR) == 0)   return 11;
	if(strcmp(sRole, DASDIM_ROLE_MIN_ERR) == 0)   return 12;
	if(strcmp(sRole, DASDIM_ROLE_STD_DEV) == 0)   return 13;
	if(strcmp(sRole, DASDIM_ROLE_NORM) == 0)      return 14;
	return 15;
}

/* The first line of DasDim_info(), and all of DasDim_toStr() */
static int _DasDim_catTitle(const DasDim* pThis, char* sBuf, size_t uLen, int n)
{
	const char* sDimType = "Data";
	if(pThis->dtype == DASDIMTYPE_COORD) sDimType = "Coordinate";
	n = _prn_catf(sBuf, uLen, n, "%s Dimension: %s (%s)",
	                  sDimType, pThis->sId, pThis->sDim);

	if(pThis->axes[0][0] != '\0'){

		for(int iAxis = 0; iAxis < DASDIM_NAXES; ++iAxis){
			if(pThis->axes[iAxis][0] == '\0') continue;

			if(iAxis == 0){
				/* the same slot serves both; as with the encoder, only a
				   coordinate can be a secondary (annotation) axis */
				const char* sLabel = " | axis: ";
				if((pThis->dtype == DASDIMTYPE_COORD)&&(!pThis->primary))
					sLabel = " | annotation: ";
				n = _prn_catf(sBuf, uLen, n, "%s", sLabel);
			}
			else{
				n = _prn_catf(sBuf, uLen, n, ",");
			}
			/* axes are one or two letters and not NUL terminated at 2 */
			n = _prn_catf(sBuf, uLen, n, "%.2s", pThis->axes[iAxis]);
		}
	}

	return n;
}

char* DasDim_toStr(const DasDim* pThis, char* sBuf, size_t uLen)
{
	if(uLen > 0) sBuf[0] = '\0';
	return _prn_fit(sBuf, uLen, _DasDim_catTitle(pThis, sBuf, uLen, 0));
}

int DasDim_info(const DasDim* pThis, char* sBuf, size_t uLen)
{
	if(uLen > 0) sBuf[0] = '\0';
	int n = _DasDim_catTitle(pThis, sBuf, uLen, 0);
	n = _prn_catf(sBuf, uLen, n, "\n");
	
	/* Larry wanted the properties printed as well */
	int nProps = DasDesc_info(
		(DasDesc*)pThis, "   ", _prn_at(sBuf, uLen, n), _prn_rem(uLen, n)
	);
	n += nProps;
	if(nProps > 0) n = _prn_catf(sBuf, uLen, n, "\n");
	
	/* Yea this is order N^2, but we never have that many variables in a dim */
	/* Do the recognized variables first */

	/* DasVar_toStr() clips to its buffer rather than sizing, this is big
	   enough for any one-line variable summary we produce */
	char sInfo[2048] = {'\0'};
	for(int nOrder = 0; nOrder < 16; ++nOrder){
		
		for(size_t u = 0; u < pThis->uVars; ++u){
			
			if(_DasDim_varOrder(pThis->aRoles[u]) == nOrder ){
			
				DasVar_toStr(pThis->aVars[u], sInfo, sizeof(sInfo));
				n = _prn_catf(sBuf, uLen, n, "   Var: %-9s | %s\n",
				              pThis->aRoles[u], sInfo);
			}
		}
	}
	return n;
}

/* Adding variables ******************************************************* */

bool DasDim_addVar(DasDim* pThis, const char* role, DasVar* pVar)
{
	/* All variables in a dimension have to have a role, which is 
	   just a non-empty string that we may or may not understand */
	if((role == NULL)||(role[0] == '\0')){
		daserrf(DASERR_DIM,
			"A variable's role in dimension '%s' can't be empty", pThis->sId
		);
		return false;
	}
	if(pVar == NULL){
		daserrf(DASERR_DIM, "NULL variable for role '%s'", role);
		return false;
	}

	for(size_t u = 0; u < pThis->uVars; ++u){
		if(strcasecmp(pThis->aRoles[u], role) == 0){
			daserrf(DASERR_DIM, "Role %s is already defined", role);
			return false;
		}
	}
	if(pThis->uVars == DASDIM_MAXVAR){
		daserrf(
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
DasVar* DasDim_var(DasDim* pThis, const char* sRole)
{
	for(size_t u = 0; u < pThis->uVars; ++u){
		if(strcasecmp(pThis->aRoles[u], sRole) == 0) return pThis->aVars[u];
	}
	return NULL;
}

DasVar* DasDim_pointVar(DasDim* pThis)
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
	DasVar* pVar = DasDim_var(pThis, DASDIM_ROLE_CENTER);
	if(pVar != NULL) return pVar;
	
	pVar = DasDim_var(pThis, DASDIM_ROLE_MEAN);
	if(pVar != NULL) return pVar;
	
	pVar = DasDim_var(pThis, DASDIM_ROLE_MEDIAN);
	if(pVar != NULL) return pVar;
	
	pVar = DasDim_var(pThis, DASDIM_ROLE_MODE);
	
	/* We can't make new vars here since we are a constant pointer, but 
	 * a function to do this could be added...*/
	
	return pVar; /* can be null */
}


/* Removing Variables ***************************************************** */

DasVar* DasDim_popVar(DasDim* pThis, const char* role){
	
	int iRm = -1;
	void* pDest = NULL;
	void* pSrc = NULL;
	DasVar* pRet = NULL;
	
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

const char* DasDim_physDim(const DasDim* pThis){ return pThis->sDim; }

const char* DasDim_name(const DasDim* pThis){ return pThis->sId; }

das_dimtype DasDim_type(const DasDim* pThis){ return pThis->dtype; }

/* DASDIMTYPE_UNK reports as "data", as it always has */
const char* DasDim_typeName(const DasDim* pThis)
{
	return (pThis->dtype == DASDIMTYPE_COORD) ? "coord" : "data";
}

void DasDim_setPrimeCoord(DasDim* pThis, bool bPrimary){ pThis->primary = bPrimary; }

bool DasDim_isPrimeCoord(const DasDim* pThis){ return pThis->primary; }

const char* DasDim_axisSymAt(const DasDim* pThis, int iAxis)
{
	if((iAxis < 0)||(iAxis >= DASDIM_NAXES)) return NULL;
	return (pThis->axes[iAxis][0] != '\0') ? pThis->axes[iAxis] : NULL;
}

size_t DasDim_numVars(const DasDim* pThis){ return pThis->uVars; }

DasVar* DasDim_varAt(const DasDim* pThis, size_t uIdx)
{
	return (uIdx < pThis->uVars) ? pThis->aVars[uIdx] : NULL;
}

const char* DasDim_roleAt(const DasDim* pThis, size_t uIdx)
{
	return (uIdx < pThis->uVars) ? pThis->aRoles[uIdx] : NULL;
}

int DasDim_numAxes(const DasDim* pThis)
{
	int nAxes = 0;
	for(int i = 0; i < DASDIM_NAXES; ++i){
		if(pThis->axes[i][0] != '\0')
			++nAxes;
	}
	return nAxes;
}

das_errcode DasDim_setAxisSym(DasDim* pThis, int iAxis, const char* sAxis)
{
	if((iAxis < 0)||(iAxis >= DASDIM_NAXES))
		return daserrf(DASERR_DIM, "Axis index %d is not valid for a dimension", iAxis);

	/* Keeps the axes packed from 0, DasDim_numAxes() is a loop bound */
	if(iAxis > DasDim_numAxes(pThis))
		return daserrf(DASERR_DIM,
			"Set axis %d of dimension %s before axis %d", iAxis - 1, pThis->sId, iAxis
		);
	if((sAxis == NULL)||(sAxis[0] == '\0'))
		return daserrf(DASERR_DIM, "Empty axis name for dimension %s", pThis->sId);

	strncpy(pThis->axes[iAxis], sAxis, DASDIM_AXIS_SZ - 1);
	return DAS_OKAY;
}

void DasDim_setAxesFrom(DasDim* pThis, const DasDim* pOther)
{
	pThis->primary = pOther->primary;
	memcpy(pThis->axes, pOther->axes, DASDIM_NAXES*DASDIM_AXIS_SZ);
}


/* Construction / Destruction ********************************************* */

DasDim* new_DasDim(const char* sDim, const char* sId, das_dimtype dtype, int nDsRank)
{
	DasDim* pThis = (DasDim*)calloc(1, sizeof(DasDim));
	if(pThis == NULL){
		daserrf(DASERR_DIM, "Out of memory"); return NULL;
	}

	DasDesc_init((DasDesc*)pThis, DASDESCTYPE_DIM, _del_DasDim);
	
	pThis->dtype = dtype;
	dasid_check(sDim);
	strncpy(pThis->sDim, sDim, DASID_SZ-1);
	
	if(sId && sId[0] != '\0')  /* Just repeat as dim name if no name given */
		strncpy(pThis->sId, sId, DASID_SZ-1);
	else
		strncpy(pThis->sId, sDim, DASID_SZ-1);

	pThis->iFirstInternal = nDsRank;
	
	return pThis;
}


static void _del_DasDim(DasDesc* pDesc){
	DasDim* pThis = (DasDim*)pDesc;
	size_t u;
	for(u = 0; u < pThis->uVars; ++u)
		dec_DasVar(pThis->aVars[u]);

	DasDesc_freeProps(&(pThis->base));
	free(pThis);
}

/* ************************************************************************* */
/* Encoding as XML */

das_errcode DasDim_encode(DasDim* pThis, DasBuf* pBuf)
{
	das_errcode nRet = DAS_OKAY;
	char sAxis[64] = {'\0'};
	const char* sType = "data";
	if(DasDim_type(pThis) == DASDIMTYPE_COORD){
		sType = "coord";
		char* pPut = sAxis;
		size_t uLen = 0;
		int i = 0;
		for(i = 0; i < DASDIM_NAXES; ++i){
			uLen = strlen((const char*) pThis->axes[i]);
			if(uLen > 0){
				if(pPut != sAxis){ *pPut = ';'; ++pPut;}
				memcpy(pPut, (const char*) pThis->axes[i], uLen);
				pPut += uLen;
			}	
		}
	}
	DasBuf_printf(pBuf, "  <%s physDim=\"%s\" name=\"%s\"",
		sType, DasDim_physDim(pThis), DasDim_name(pThis)
	);

	if((DasDim_type(pThis) == DASDIMTYPE_COORD)&&(sAxis[0] != '\0')){
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
	DasVar* pVar = NULL;
	const char* sRole = NULL;
	for(size_t u = 0; u < uVars; ++u){
		pVar = DasDim_varAt(pThis, u);
		sRole = DasDim_roleAt(pThis, u);

		/* A computed variable has no wire form, this is typically not a
		   problem, just remind the end user of that fact. */
		if(DasVar_gen(pVar) == NULL){
			daslog_infof(
				"Dropping computed variable %s:%s from output.", pThis->sId, sRole
			);
			continue;
		}

		nRet = DasVar_encode(pVar, sRole, pBuf);
		if(nRet != DAS_OKAY)
			return nRet;
	}

	DasBuf_printf(pBuf, "  </%s>\n", sType);
	return nRet;
}

