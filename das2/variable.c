/* Copyright (C) 2017-2018 Chris Piker <chris-piker@uiowa.edu>
 *
 * This file is part of libdas2, the Core Das2 C Library.
 *
 * Libdas2 is free software; you can redistribute it and/or modify it under
 * the terms of the GNU Lesser General Public License version 2.1 as published
 * by the Free Software Foundation.
 *
 * Libdas2 is distributed in the hope that it will be useful, but WITHOUT ANY
 * WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS
 * FOR A PARTICULAR PURPOSE.  See the GNU Lesser General Public License for
 * more details.
 *
 * You should have received a copy of the GNU Lesser General Public License
 * version 2.1 along with libdas2; if not, see <http://www.gnu.org/licenses/>.
 */

#define _POSIX_C_SOURCE 200112L

#include <ctype.h>
#include <stdbool.h>
#include <string.h>
#include <math.h>

#include "util.h"
#include "variable.h"
#include "array.h"
#include "operator.h"
#include "variable.h"


/* ************************************************************************* */
/* Set index printing direction... NOT thread safe */
bool g_bFastIdxLast = false;

DAS_API void das_varindex_prndir(bool bFastLast)
{
	g_bFastIdxLast = bFastLast;
}

/* ************************************************************************* */
/* Helpers */

void das_varindex_merge(int nRank, ptrdiff_t* pDest, ptrdiff_t* pSrc)
{
	
	for(size_t u = 0; u < nRank && u < DASIDX_MAX; ++u){
		
		/* Here's the order of shape merge precidence
		 * 
		 * Ragged > Number > Seq > Unused
		 * 
		 *    | R | N | S | U  
		 *  --+---+---+---+---  
		 *  R | R | R | R | R
		 *  --+---+---+---+---  
		 *  N | R |low| N | N
		 *  --+---+---+---+---   
		 *  F | R | N | S | S
		 *  --+---+---+---+---   
		 *  S | R | N | S | U
		 *  --+---+---+---+---  
		 */ 
		
		/* If either is ragged, the result is ragged */
		if((pDest[u] == DASIDX_RAGGED) || (pSrc[u] == DASIDX_RAGGED)){ 
			pDest[u] = DASIDX_RAGGED;
			continue;
		}
		
		/* If either is a number, the result is the smallest number */
		if((pDest[u] >= 0) || (pSrc[u] >= 0)){
			if((pDest[u] >= 0) && (pSrc[u] >= 0))
				pDest[u] = (pDest[u] < pSrc[u]) ? pDest[u] : pSrc[u];
			
			else
				/* Take item that is a number, cause one of them must be */
				pDest[u] = (pDest[u] < pSrc[u]) ? pSrc[u] : pDest[u];
			
			continue;
		}
		
		/* All that's left at this point is to be a function or unused */
		if((pDest[u] == DASIDX_FUNC)||(pSrc[u] == DASIDX_FUNC)){
			pDest[u] = DASIDX_FUNC;
			continue;
		}
		
		/* default to unused requires no action */
	}
}

ptrdiff_t das_varlength_merge(ptrdiff_t nLeft, ptrdiff_t nRight)
{
	if((nLeft >= 0)&&(nRight >= 0)) return nLeft < nRight ? nLeft : nRight;
	
	/* Reflect at 0 since FUNC beats UNUSED, and a real index beats anything
	 * that's just a flag */
	
	return nLeft > nRight ? nLeft : nRight;
}

/* ************************************************************************* */
/* Base class functions */

int inc_DasVar(DasVar* pThis){ pThis->nRef += 1; return pThis->nRef;}

int ref_DasVar(const DasVar* pThis){ return pThis->nRef;}

enum var_type DasVar_type(const DasVar* pThis){ return pThis->vartype; }

das_val_type DasVar_valType(const DasVar* pThis){ return pThis->vt;}

das_units DasVar_units(const DasVar* pThis){ return pThis->units;}

bool DasVar_getDatum(const DasVar* pThis, ptrdiff_t* pLoc, das_datum* pDatum)
{
	return pThis->get(pThis, pLoc, pDatum);
}

bool DasVar_isFill(const DasVar* pThis, const byte* pCheck, das_val_type vt)
{
	return pThis->isFill(pThis, pCheck, vt);	
}


bool DasVar_isComposite(const DasVar* pVar){
	return (pVar->vartype == D2V_BINARY_OP || pVar->vartype == D2V_UNARY_OP);
}

int DasVar_shape(const DasVar* pThis, ptrdiff_t* pShape)
{
	return pThis->shape(pThis, pShape);
}

ptrdiff_t DasVar_lengthIn(const DasVar* pThis, int nIdx, ptrdiff_t* pLoc)
{
	return pThis->lengthIn(pThis, nIdx, pLoc);
}

char* DasVar_toStr(const DasVar* pThis, char* sBuf, int nLen)
{
	char* sBeg = sBuf;
	pThis->expression(pThis, sBuf, nLen, D2V_EXP_RANGE | D2V_EXP_UNITS);
	return sBeg;
}

char* _DasVar_prnUnits(const DasVar* pThis, char* sBuf, int nLen)
{
	if(pThis->units == UNIT_DIMENSIONLESS) return sBuf;
	if(nLen < 3) return sBuf;
	
	memset(sBuf, 0, nLen);  /* Insure null termination where ever I stop writing */
	
	char* pWrite = sBuf;
	
	/* if( nLen > 0 ){ *pWrite = '{'; --nLen; ++pWrite;} */
	if( nLen > 0 ){ *pWrite = ' '; --nLen; ++pWrite;}
	
	const char* sUnits = Units_toStr(pThis->units);
	int nStr = strlen(sUnits);
	int nWrite = nStr < nLen ? nStr : nLen;
	strncpy(pWrite, sUnits, nWrite);
	pWrite += nWrite;
	nLen -= nWrite;
	
	/* if( nLen > 0 ){ *pWrite = '}'; --nLen; ++pWrite;} */
	
	return pWrite;
}

static const char letter_idx[16] = {
	/* 'I','J','K','L','M','N','P','Q','R','S','T','U','V','W','X','Y' */
	   'i','j','k','l','m','n','p','q','r','s','t','u','v','w','x','y'
};
	
/*	
iFirst = 3

i = 0, 1, 2
    I  J  K

i = 2, 1, 0    iFirst - i - 1 = 0 1 2
    I  J  K
	 
iLetter = i (last fastest)
iLetter = iFirst - i - 1   (first fastest)

*/

char* das_shape_prnRng(
	ptrdiff_t* pShape, int iFirstInternal, int nShapeLen, char* sBuf, int nBufLen
){

	memset(sBuf, 0, nBufLen);  /* Insure null termination where ever I stop writing */
	
	int nUsed = 0;
	for(int i = 0; i < iFirstInternal; ++i)
		if(pShape[i] != DASIDX_UNUSED) ++nUsed;
	
	if(nUsed == 0) return sBuf;
	
	/* If don't have the minimum min of bytes to print the range don't do it */
	if(nBufLen < (3 + nUsed*6 + (nUsed - 1)*2)) return sBuf;
	
	char* pWrite = sBuf;
	strncpy(pWrite, " |", 2); 
	nBufLen -= 2;
	pWrite += 2;
	
	char sEnd[32] = {'\0'};
	int nNeedLen = 0;
	bool bAnyWritten = false;
	
	int i = 0;
	int iEnd = iFirstInternal;
	int iLetter = 0;
	if(!g_bFastIdxLast){
		i = iFirstInternal - 1;
		iEnd = -1;
	}
	
	while(i != iEnd){
		
		if(pShape[i] == DASIDX_UNUSED){ 
			nNeedLen = 4 + ( bAnyWritten ? 1 : 0);
			if(nBufLen < (nNeedLen + 1)){
				sBuf[0] = '\0'; return sBuf;
			}
			
			if(bAnyWritten)
				snprintf(pWrite, nBufLen - 1, ", %c:-", letter_idx[iLetter]);
			else
				snprintf(pWrite, nBufLen - 1, " %c:-", letter_idx[iLetter]);
		}
		else{
			if((pShape[i] == DASIDX_RAGGED)||(pShape[i] == DASIDX_FUNC)){
				sEnd[0] = '*'; sEnd[1] = '\0';
			}
			else{
				snprintf(sEnd, 31, "%zd", pShape[i]);
			}
		
			nNeedLen = 6 + strlen(sEnd) + ( bAnyWritten ? 1 : 0);
			if(nBufLen < (nNeedLen + 1)){ 
				/* If I've run out of room close off the string at the original 
				 * write point and exit */
				sBuf[0] = '\0';
				return sBuf;
			}
		
			if(bAnyWritten)
				snprintf(pWrite, nBufLen - 1, ", %c:0..%s", letter_idx[iLetter], sEnd);
			else
				snprintf(pWrite, nBufLen - 1, " %c:0..%s", letter_idx[iLetter], sEnd);
		}
		
		pWrite += nNeedLen;
		nBufLen -= nNeedLen;
		bAnyWritten = true;
		
		if(g_bFastIdxLast) ++i;
		else               --i;
		
		 ++iLetter;  /* always report in order of I,J,K */
	}
	
	return pWrite;
}


/* Range expressions look like " | i:0..60, j:0..1442 "  */
char* _DasVar_prnRange(const DasVar* pThis, char* sBuf, int nLen)
{
	ptrdiff_t aShape[DASIDX_MAX];
	pThis->shape(pThis, aShape);
	
	int iInternal = pThis->iFirstInternal;
	return das_shape_prnRng(aShape, iInternal, iInternal, sBuf, nLen);
}

/* ************************************************************************* */
/* Constants */

typedef struct das_var_const{
	DasVar base;
	
	/* Buffer and possible pointer for constant value 'variables' */
	byte constant[sizeof(das_time)];
} DasConstant;


int dec_DasConstant(DasVar* pBase){
	pBase->nRef -= 1;
	if(pBase->nRef > 0) return pBase->nRef;
	DasConstant* pThis = (DasConstant*)pBase;
	free(pThis);
	return 0;
}

/* Doesn't even look at the index */
bool DasConstant_get(const DasVar* pBase, ptrdiff_t* pIdx, das_datum* pDatum)
{
	const DasConstant* pThis = (const DasConstant*)pBase;
	memcpy(pDatum, pThis->constant, pBase->vsize);
	pDatum->vt = pBase->vt;
	pDatum->vsize = pBase->vsize;
	pDatum->units = pBase->units;
	return true;
}


/* Returns the pointer an the next write point of the string */
char* DasConstant_expression(
	const DasVar* pBase, char* sBuf, int nLen, unsigned int uFlags
){
	if(nLen < 3) return sBuf;
	
	memset(sBuf, 0, nLen);  /* Insure null termination where ever I stop writing */
	
	const DasConstant* pThis = (const DasConstant*)pBase;
	das_datum dm;
	DasConstant_get(pBase, NULL, &dm);
			  
	das_datum_toStrValOnly(&dm, sBuf, nLen, -1);
	int nWrote = strlen(sBuf);
	nLen -= nWrote;
	char* pWrite = sBuf + nWrote;
	
	if(pBase->units == UNIT_DIMENSIONLESS) return pWrite;
	if( (uFlags & D2V_EXP_UNITS) == 0) return pWrite;
	
	return _DasVar_prnUnits((DasVar*)pThis, pWrite, nLen);
}

int DasConstant_shape(const DasVar* pBase, ptrdiff_t* pShape){
	for(int i = 0; i < DASIDX_MAX; ++i) pShape[i] = DASIDX_UNUSED;
	return 0;
}

ptrdiff_t DasConstant_lengthIn(const DasVar* pVar, int nIdx, ptrdiff_t* pLoc)
{
	return DASIDX_FUNC;
}

DasVar* new_DasConstant(
	das_val_type vt, size_t sz, const void* val, das_units units
){
	DasConstant* pThis = (DasConstant*)calloc(1, sizeof(DasConstant));
	
	pThis->base.vartype = D2V_DATUM;
	pThis->base.units = units;
	pThis->base.vt = vt;
	if(vt == vtUnknown) pThis->base.vsize = sz;
	else pThis->base.vsize = das_vt_size(vt);
	
	pThis->base.decRef     = dec_DasConstant;
	pThis->base.expression = DasConstant_expression;
	pThis->base.incRef     = inc_DasVar;
	pThis->base.nRef       = 1;
	pThis->base.get        = DasConstant_get;
	pThis->base.shape      = DasConstant_shape;
	pThis->base.lengthIn   = DasConstant_lengthIn;
	pThis->base.iFirstInternal = 0;        /* No external shape for constants */
	
	/* Copy in the value */
	if(vt == vtText){
		memcpy(pThis->constant, &val, sizeof(const char*));
	}
	else{
		memcpy(pThis->constant, &val, pThis->base.vsize);
	}
	
	return (DasVar*)pThis;
}

/* ************************************************************************* */
/* Array mapping functions */

typedef struct das_var_array{
	DasVar base;
	
	/* Array pointer and index map to support lookup variables */
	DasAry* pAry; /* A pointer to the array containing the values */
	int idxmap[16];      /* i,j,k data set space to array space */
	int idxconst[16];    /* i,j,k constant indicies for slices */
	
} DasVarArray;


DasAry* DasVarAry_getArray(DasVar* pThis)
{
	if( pThis->vartype != D2V_ARRAY) return NULL;
	DasVarArray* pReallyThis = (DasVarArray*)pThis;
	return pReallyThis->pAry;
}

/* Public function, call from the top level 
 */ 
int DasVarAry_shape(const DasVar* pBase, ptrdiff_t* pShape)
{
	if(pShape == NULL){
		das_error(DASERR_VAR, "null shape pointer, can't output shape values");
		return -1;
	}
	
	const DasVarArray* pThis = (const DasVarArray*)pBase;
	
	/* Force a memory error right up front if they've provided a variable too
	 * short to hold the answer */
	for(int i = 0; i < DASIDX_MAX; ++i) pShape[i] = DASIDX_UNUSED;
	
	/* Must be an array function */
	ptrdiff_t aShape[DASIDX_MAX] = DASIDX_INIT_UNUSED;
	int nAryRank = DasAry_shape(pThis->pAry, aShape);
	int iAryIdx = -1;
	int nRank = 0;
	
	for(int iVarIdx = 0; iVarIdx < pBase->iFirstInternal; ++iVarIdx){
		if((pThis->idxconst[iVarIdx] != DASIDX_UNUSED) || 
			(pThis->idxmap[iVarIdx] == DASIDX_UNUSED))
			continue;
		
		iAryIdx = pThis->idxmap[iVarIdx];
		if(iAryIdx >= nAryRank){
			das_error(DASERR_VAR, "Invalid index map detected, max array index"
			           " is %d, lookup index is %d", nAryRank - 1, iAryIdx);
			return -1;
		}
		
		/* Any particular array point may be marked as ragged and that's okay */
		pShape[iVarIdx] = aShape[iAryIdx];
		++nRank;
	}
	return nRank;
}

/* This one is tough what is my shape in a particular index given all 
 * other indexes.  This is different from the array version in that 
 * 
 * 1: I might not even depend on the previous indexes
 * 
 * 2. Indexes further to the right might affect the range of indexes to
 *    the left
 * 
 * Just code the mapping and see what happens  Let's see what this looks
 * like for ragged arrays
 * 
 *                 j
 *          time   0    1    2    3    4    5    6    7    8
 *       +---------------------------------------------------
 *  freq |       25.1 50.2 75.3  100  126  151  176  201  226
 *   i  0|  2000   X    X    X    X    X    X    X    X 
 *      1|  2001   X    X    X    X    X    X    X    X    X 
 *      2|  2002   X    X    X    X    X    X 
 *      3|  2003   X    X    X    X    X    X    X 
 *      4|  2004   X    X    X    X    X    X    X    X    X 
 *      5|  2005   X    X  
 *      6|  2006   X    X    X    X    X    X    X    X
 *      7|  2007   X    X    X    X    X    X    X  
 *      8|  2008   X    X    X    X    X    X    X    X 
 *      9|  2009   X    X    X    X    X    X    X    X
 *     10|  2010   X    X    X    X    X    X    X    X    X 
 *     11|  2011   X    X    X    X    X    X
 * 
 *  amp  len_in_j @ i = 0 : 7
 *  freq len_in_j @ i = 0 : 7
 *  time len_in_j @ i = 0 : 1 ? 7 ? ==> - (no dependence)
 * 
 * 
 *  amp len_in_i @ j = 3 : 10 ?  The transpose of the above amplitude array 
 *                               is not a valid array.  So assuming low to 
 *                               high packing len_in_i @ j is an invalid value.
 */
ptrdiff_t DasVarAry_lengthIn(const DasVar* pBase, int nIdx, ptrdiff_t* pLoc)
{
	DasVarArray* pThis = (DasVarArray*)pBase;
	
	/* Map the location, it should provide a partial map */
	ptrdiff_t aAryLoc[16] = DASIDX_INIT_UNUSED;  /* we have to resolve all these
															   * to a positive number before
															   * asking the array for its 
	                                             * size */
	int i = 0;
	int nIndexes = 0;
	for(i = 0; i < nIdx; ++i){
		
		if(pLoc[i] < 0){
			das_error(DASERR_VAR, "Location index must not contain negative values");
			return DASIDX_UNUSED;
		}
		
		if(pThis->idxmap[i] >= 0){
			++nIndexes;
			aAryLoc[ pThis->idxmap[i] ] = pLoc[i];
		}
	}
	
	/* Sequences would return D2IDX_FUNC here instead */
	if(nIndexes == 0) return DASIDX_UNUSED; 
	
	/* Make sure the front of the array is packed */
	for(i = 0; i < nIndexes; ++i){
		if(aAryLoc[i] < 0){
			das_error(DASERR_VAR, "Unexpected index map result, review this code");
			return DASIDX_UNUSED;
		}
	}
	
	return DasAry_lengthIn(pThis->pAry, nIndexes, aAryLoc);
}

bool DasVarAry_get(const DasVar* pBase, ptrdiff_t* pLoc, das_datum* pDatum)
{
	const DasVarArray* pThis = (const DasVarArray*)pBase;
	
	/* Ignore indices you don't understand, that's what makes this work */
	/* I need a way to make this code fast, maybe the loop should be unrolled? */
	ptrdiff_t pAryLoc[16] = {'\0'};
	
	
	for(int i = 0; i < pBase->iFirstInternal; ++i){
		if(pThis->idxmap[i] >= 0) /* all the wierd flags are less than 0 */
			pAryLoc[ pThis->idxmap[i] ] = pLoc[i];
	}
	
	/* No way to know if this is a full or partial lookup unless we 
	 * check it.  Do we want to check it? Not for now */
	const void* ptr = DasAry_getAt(pThis->pAry, pBase->vt, pAryLoc);
	if(ptr == NULL) return false;
	memcpy(pDatum, ptr, pBase->vsize);
	pDatum->vt = pBase->vt;
	pDatum->vsize = pBase->vsize;
	pDatum->units = pBase->units;
	return true;
}

bool DasVarAry_isFill(const DasVar* pBase, const byte* pCheck, das_val_type vt)
{
	const DasVarArray* pThis = (const DasVarArray*)pBase;
	const byte* pFill = DasAry_getFill(pThis->pAry);
	
	return (das_vt_cmpAny(pFill, pBase->vt, pCheck, vt) == 0);
}

int dec_DasVarAry(DasVar* pBase){
	pBase->nRef -= 1;
	if(pBase->nRef > 0) return pBase->nRef;
	
	DasVarArray* pThis = (DasVarArray*)pBase;
	dec_DasAry(pThis->pAry);
	free(pThis);
	return 0;
}

/* It is certianly possible to implement an "evaluate_at" function for 
 * variables.  It would look something like the following.  Not going to do this
 * now because we just need to get data into python and other environments.  
 * 
 * But it does provide a nice diagnostic tool, so maybe it's worth considering
 * 
 * General:
 *  app_alt[i][j] => (sqrt(altitude[i]) - (delay_time[j] * 3.14567e-00)) / 42.0) V**2 m**-2 Hz**-1 | i:0..60, j:0..1442
 * 
 * Evaluate at i = 14:
 *  app_alt @ i=14 => (1240 - (delay_time[j] * 3.14567e-00) / 42.0)) | j:0..1442
 *
 * Evaluate at slice j = 346:
 *  app_alt @ j=346 => (sqrt(altitude[i]) - 80.45) | i:0..60
 * 
 * Evaluate above again for i = 14:
 *  app_alt @ i=14,j=346 => (14.4765e+01) V**2 m**2 Hz**-1
 * 
 * After faltten: nothing fixed:
 *  app_alt => app_alt[i][j] V**2 m**-2 Hz**-1 | i:0..60, j:0..1442
 * 
 */

char* DasVarAry_expression(
	const DasVar* pBase, char* sBuf, int nLen, unsigned int uFlags
){
	if(nLen < 2) return sBuf;  /* No where to write and remain null terminated */
	memset(sBuf, 0, nLen);  /* Insure null termination whereever I stop writing */
	
	const DasVarArray* pThis = (const DasVarArray*)pBase;	

	int nWrite = strlen(pThis->pAry->sId);
	nWrite = nWrite > (nLen - 1) ? nLen - 1 : nWrite;
	char* pWrite = sBuf;
	strncpy(sBuf, pThis->pAry->sId, nWrite);
	
	pWrite = sBuf + nWrite;  nLen -= nWrite;
	if(nLen < 2) return pWrite;
	
	int i, nRank = 0;
	for(i = 0; i < pBase->iFirstInternal; i++){
		if(pThis->idxmap[i] != DASIDX_UNUSED) ++nRank;
	}
	
	if(nLen < (nRank*3 + 1)) return pWrite;
	
	for(i = 0; i < pBase->iFirstInternal; i++){
		if(pThis->idxmap[i] != DASIDX_UNUSED){ 
			*pWrite = '['; ++pWrite; --nLen;
			*pWrite = letter_idx[i]; ++pWrite; --nLen;
			*pWrite = ']'; ++pWrite; --nLen;
		}
	}
	
	char* pSubWrite = pWrite;
	
	if((pBase->units != UNIT_DIMENSIONLESS) &&
		((uFlags & D2V_EXP_UNITS) == D2V_EXP_UNITS) ){
		pSubWrite = _DasVar_prnUnits((DasVar*)pThis, pWrite, nLen);
		nLen -= (pSubWrite - pWrite);
		pWrite = pSubWrite;
	}
	
	if(uFlags & D2V_EXP_RANGE){
		pWrite = _DasVar_prnRange(&(pThis->base), pWrite, nLen);
	}
	
	return pWrite;
}

DasVar* new_DasVarArray(DasAry* pAry, int iInternal, int8_t* pMap)
{
	
	if((iInternal == 0)||(iInternal > 15)){
		das_error(DASERR_VAR, "Invalid start of internal indices: %d", iInternal);
		return NULL;
	}
	
	DasVarArray* pThis = (DasVarArray*)calloc(1, sizeof(DasVarArray));
	
	pThis->base.vartype    = D2V_ARRAY;
	pThis->base.vt    = DasAry_valType(pAry);
	pThis->base.vsize    = DasAry_valSize(pAry);
	pThis->base.nRef       = 1;
	pThis->base.decRef     = dec_DasVarAry;
	pThis->base.expression = DasVarAry_expression;
	pThis->base.incRef     = inc_DasVar;
	pThis->base.get        = DasVarAry_get;
	pThis->base.shape      = DasVarAry_shape;
	pThis->base.lengthIn   = DasVarAry_lengthIn;
	pThis->base.isFill     = DasVarAry_isFill;
	
	
	/* Extra stuff for array variables */
	if(pAry == NULL){
		das_error(DASERR_VAR, "Null array pointer\n");
		return false;
	}
	
	pThis->pAry = pAry;
	
	/* Connection between variable units and array units broken here, this
	 * is intentional, but be aware of it! */
	pThis->base.units = pAry->units;
	
	int nValid = 0;
	char sBuf[64] = {'\0'};
	pThis->base.iFirstInternal = iInternal;
	for(int i = 0; i < DASIDX_MAX; ++i){
		pThis->idxmap[i] = DASIDX_UNUSED;
		pThis->idxconst[i] = DASIDX_UNUSED;
	}
	for(size_t u = 0; (u < iInternal) && (u < DASIDX_MAX); ++u){ 
		pThis->idxmap[u] = pMap[u];/* Run roles is a defined order so that the same things always appear
		 * in the same places */
		/* pThis->idxconst[u] = D2IDX_UNUSED; */
		
		/* Make sure that the map has the same number of non empty indexes */
		/* as the rank of the array */
		if(pMap[u] >= 0){
			++nValid;
			if(pMap[u] >= pAry->nRank){
				das_error(DASERR_VAR, "Variable dimension %zu maps to non-existant "
				           "dimension %zu in array %s", u, pMap[u],
				           DasAry_toStr(pAry, sBuf, 63));
				free(pThis);
				return NULL;
			}
		}
		
	}
	
	if(nValid == 0){
		das_error(DASERR_VAR, "Coordinate values are independent of dataset "
		           "indices");
		free(pThis);
		return NULL;
	}
	if(nValid != pAry->nRank){
		das_error(DASERR_VAR, "Variable index map does not have the same number "
			"of valid indices as the array dimension.  While partial array "
			"mapping may be useful, it's not supported for now.");
		free(pThis);
		return NULL;
	}
	
	inc_DasAry(pAry);    /* Increment the reference count for this array */
	return &(pThis->base);
}

/* ************************************************************************* */
/* Sequences derived from direct operation on indices */

typedef struct das_var_seq{
	DasVar base;
	int idxmap[DASIDX_MAX];
	int idxconst[DASIDX_MAX];
	char* sPseudoAryName[32];
	
	byte initial[sizeof(das_time)];
	byte* pInitial;
	
	byte interval[sizeof(das_time)];
	byte* pInterval;
	
} DasVarSeq;


DasVar* new_DasVarSeq(
	const char* sId, das_val_type vt, size_t vSz, const void* pMin, 
	const void* pInterval, int8_t* pMap, das_units units
){
	return NULL;
}

/* ************************************************************************* */
/* Unary Functions on other Variables */

typedef struct das_var_op{
	DasVar base;
	
	/* right hand sub-variable pointer for binary operations */
	const DasVar* pLeft;     
	
	/* Right hand sub-variable pointer for unary and binary operations */
	const DasVar* pRight;
	
	/* operator for unary and binary operations */
	int     nOp;
} DasVarUnary;

/*
DasVar* new_DasVarUnary(const char* sOp, const DasVar* left)
{
	
}



DasVar* new_DasVarUnary_tok(int nOp, const DasVar* left)
{
	/ * TODO: write this once the expression lexer exist * /
	return NULL;
}
*/

/* ************************************************************************* */
/* Binary functions on other Variables */

typedef struct das_var_binary{
	DasVar base;
	
	DasVar* pRight;      /* right hand sub-variable pointer */
	DasVar* pLeft;       /* left hand sub-variable pointer  */
	int     nOp;         /* operator for unary and binary operations */
	double  rRightScale; /* Scaling factor for right hand values */
} DasVarBinary;

/* Our expressions look like this:  
 * 
 * ( sub_exp_left operator [scale *] sub_exp_right )[units][range]
 */
char* DasVarBinary_expression(
	const DasVar* pBase, char* sBuf, int nLen, unsigned int uFlags
){	
	if(nLen < 12) return sBuf; /* No where to write */
	memset(sBuf, 0, nLen); /* Insure null termination whereever I stop writing */
	
	const DasVarBinary* pThis = (const DasVarBinary*)pBase;
	
	char* pWrite = sBuf;
	*pWrite = '('; pWrite++; *pWrite = ' '; pWrite++; nLen -= 2;
	
	char* pSubWrite = pThis->pLeft->expression(pThis->pLeft, pWrite, nLen, 0);
	int nTmp = pSubWrite - pWrite;
	nLen -= nTmp;
	pWrite = pSubWrite;
	if((nTmp == 0)||(nLen < 6)) goto DAS_VAR_BIN_EXP_PUNT;
		
	*pWrite = ' '; ++pWrite; --nLen;
	
	/* Print the operator, we know this is an in-between operator */
	nTmp = strlen(das_op_toStr(pThis->nOp, NULL));
	if(nTmp > (nLen - 3)) goto DAS_VAR_BIN_EXP_PUNT;
	
	strncpy(pWrite, das_op_toStr(pThis->nOp, NULL), nTmp);
	nLen -= nTmp;
	pWrite += nTmp;
	*pWrite = ' '; ++pWrite, --nLen;
		
	char sScale[32] = {'\0'};
	if(pThis->rRightScale != 1.0){
		snprintf(sScale, 31, "%.6e", pThis->rRightScale);
		/* Should pop off strings of zeros after the decimal pt here */
		nTmp = strlen(sScale);
		if(nTmp > (nLen - 4)) goto DAS_VAR_BIN_EXP_PUNT;
		strncpy(pWrite, sScale, nTmp);
		pWrite += nTmp;
		nLen -= nTmp;
		*pWrite = ' '; ++pWrite;
		*pWrite = '*'; ++pWrite;
		*pWrite = ' '; ++pWrite; nLen -= 3;
	}
	
	pSubWrite = pThis->pRight->expression(pThis->pRight, pWrite, nLen, 0);
	nTmp = pSubWrite - pWrite;
	nLen -= nTmp;
	pWrite = pSubWrite;
	if((nTmp == 0)||(nLen < 3)) goto DAS_VAR_BIN_EXP_PUNT;
	
	*pWrite = ' '; ++pWrite; *pWrite = ')'; ++pWrite; nLen -= 2;
	
	if((pBase->units != UNIT_DIMENSIONLESS) &&
		((uFlags & D2V_EXP_UNITS) == D2V_EXP_UNITS) ){
		pSubWrite = _DasVar_prnUnits(pBase, pWrite, nLen);
		nLen -= pSubWrite - pWrite;
		pWrite = pSubWrite;
	}
	
	if(uFlags & D2V_EXP_RANGE){
		pWrite = _DasVar_prnRange(&(pThis->base), pWrite, nLen);
	}
	
	return pWrite;
	
	DAS_VAR_BIN_EXP_PUNT:
	sBuf[0] = '\0';
	return sBuf;
}

int DasVarBinary_shape(const DasVar* pBase, ptrdiff_t* pShape)
{
	int i = 0;
	
	if(pShape == NULL){
		das_error(DASERR_VAR, "null shape pointer, can't output shape values");
		return -1;
	}
	
	DasVarBinary* pThis = (DasVarBinary*)pBase;
	
	/* Fill in shape with left variable */
	pThis->pLeft->shape(pThis->pLeft, pShape);
	
	ptrdiff_t aRight[DASIDX_MAX] = DASIDX_INIT_UNUSED;
		
	pThis->pRight->shape(pThis->pRight, aRight);
	das_varindex_merge(pBase->iFirstInternal, pShape, aRight);
	
	int nRank = 0;
	for(i = 0; i < pBase->iFirstInternal; ++i) 
		if(pShape[i] != DASIDX_UNUSED) 
			++nRank;
	
	return nRank;
}

ptrdiff_t DasVarBinary_lengthIn(const DasVar* pBase, int nIdx, ptrdiff_t* pLoc)
{
	DasVarBinary* pThis = (DasVarBinary*)pBase;
	
	ptrdiff_t nLeft = pThis->pLeft->lengthIn(pThis->pLeft, nIdx, pLoc);
	ptrdiff_t nRight = pThis->pRight->lengthIn(pThis->pRight, nIdx, pLoc);
	
	return das_varlength_merge(nLeft, nRight);
}


bool DasVarBinary_get(const DasVar* pBase, ptrdiff_t* pIdx, das_datum* pDatum)
{
	DasVarBinary* pThis = (DasVarBinary*)pBase;
	
	float fTmp;
	double dTmp;
	if(! pThis->pLeft->get(pThis->pLeft, pIdx, pDatum)) return false;
	das_datum dmRight;
	if(! pThis->pRight->get(pThis->pRight, pIdx, &dmRight)) return false;
	
	if(pThis->rRightScale != 1.0){
		switch(dmRight.vt){
			case vtByte:   dTmp = *((uint8_t*)&dmRight);  break;
			case vtShort:  dTmp = *((int16_t*)&dmRight);  break;
			case vtUShort: dTmp = *((uint16_t*)&dmRight); break;
			case vtInt:    dTmp = *((int*)&dmRight);      break;
			case vtLong:   dTmp = *((long*)&dmRight);     break;
			case vtFloat:  dTmp = *((float*)&dmRight);    break;
			case vtDouble: dTmp = *((double*)&dmRight);   break;
			default:
				das_error(DASERR_VAR, "Can't multiply types %s and %s", 
				          das_vt_toStr(dmRight.vt), das_vt_toStr(vtDouble));
				return false;
		}
		*((double*)(&dmRight)) = pThis->rRightScale * dTmp;
		dmRight.vt = vtDouble;
		dmRight.vsize = sizeof(double);
	}
	
	/* Promote left and right datums to the output type if needed */
	/* Note that for time output, only the left value is promoted if needed */
	switch(pBase->vt){
		
	/* Float promotions and calculation */
	case vtFloat:
		switch(pDatum->vt){
		case vtByte:   fTmp = *((uint8_t*)pDatum);  *((float*)pDatum) = fTmp; break;
		case vtShort:  fTmp = *((int16_t*)pDatum);  *((float*)pDatum) = fTmp; break;
		case vtUShort: fTmp = *((uint16_t*)pDatum); *((float*)pDatum) = fTmp; break;
		case vtFloat: break; /* nothing to do */
		default:
			das_error(DASERR_ASSERT, "Logic mismatch between das_vt_merge and DasVarBinary_get");
			return false;
		}
		
		switch(dmRight.vt){
		case vtByte:   fTmp = *((uint8_t*)&dmRight);  *((float*)&dmRight) = fTmp; break;
		case vtShort:  fTmp = *((int16_t*)&dmRight);  *((float*)&dmRight) = fTmp; break;
		case vtUShort: fTmp = *((uint16_t*)&dmRight); *((float*)&dmRight) = fTmp; break;
		case vtFloat:  break; /* nothing to do */
		default:
			das_error(DASERR_ASSERT, "Logic mismatch between das_vt_merge and DasVarBinary_get");
			return false;
		}
		
		switch(pThis->nOp){
		case D2BOP_ADD: *((float*)pDatum) += *((float*)&dmRight); break;
		case D2BOP_SUB: *((float*)pDatum) -= *((float*)&dmRight); break;
		case D2BOP_MUL: *((float*)pDatum) *= *((float*)&dmRight); break;
		case D2BOP_DIV: *((float*)pDatum) /= *((float*)&dmRight); break;
		case D2BOP_POW: *((float*)pDatum) = powf(*((float*)pDatum), *((float*)&dmRight)); break;
		default:
			das_error(DASERR_NOTIMP, "Binary operation not yet implemented ");
		}
		
		break;
	
	/* Double promotions and calculation */
	case vtDouble:
			
		switch(pDatum->vt){
		case vtByte:   dTmp = *((uint8_t*)pDatum);  *((double*)pDatum) = dTmp; break;
		case vtShort:  dTmp = *((int16_t*)pDatum);  *((double*)pDatum) = dTmp; break;
		case vtUShort: dTmp = *((uint16_t*)pDatum); *((double*)pDatum) = dTmp; break;
		case vtInt:    dTmp = *((int32_t*)pDatum);  *((double*)pDatum) = dTmp; break;
		case vtLong:   dTmp = *((int64_t*)pDatum);  *((double*)pDatum) = dTmp; break;
		case vtFloat:  dTmp = *((float*)pDatum);    *((double*)pDatum) = dTmp; break;
		case vtDouble: break; /* Nothing to do */
		case vtTime:
			/* The only way the left input is a time and my output is a double is 
			 * if I'm subtracting two times. Just go ahead and do than now and
			 * avoid messing up code below */
			if(dmRight.vt != vtTime){
				das_error(DASERR_ASSERT, "Logic mismatch between das_vt_merge and DasVarBinary_get");
				return false;
			}
			dTmp = dt_diff((das_time*)pDatum, (das_time*)&dmRight);
			*((double*)pDatum) = dTmp;
			return true;
			break;
		default:
			das_error(DASERR_ASSERT, "Logic mismatch between das_vt_merge and DasVarBinary_get");
			return false;
		}
		
		switch(dmRight.vt){
		case vtByte:   dTmp = *((uint8_t*)&dmRight);  *((double*)&dmRight) = dTmp; break;
		case vtShort:  dTmp = *((int16_t*)&dmRight);  *((double*)&dmRight) = dTmp; break;
		case vtUShort: dTmp = *((uint16_t*)&dmRight); *((double*)&dmRight) = dTmp; break;
		case vtInt:    dTmp = *((int32_t*)&dmRight);  *((double*)&dmRight) = dTmp; break;
		case vtLong:   dTmp = *((int64_t*)&dmRight);  *((double*)&dmRight) = dTmp; break;
		case vtFloat:  dTmp = *((float*)&dmRight);    *((double*)&dmRight) = dTmp; break;
		case vtDouble: break; /* Nothing to do */
		default:
			das_error(DASERR_ASSERT, "Logic mismatch between das_vt_merge and DasVarBinary_get");
			return false;
		}
		
		switch(pThis->nOp){
		case D2BOP_ADD: *((double*)pDatum) += *((double*)&dmRight); break;
		case D2BOP_SUB: *((double*)pDatum) -= *((double*)&dmRight); break;
		case D2BOP_MUL: *((double*)pDatum) *= *((double*)&dmRight); break;
		case D2BOP_DIV: *((double*)pDatum) /= *((double*)&dmRight); break;
		case D2BOP_POW: *((double*)pDatum) = pow(*((double*)&dmRight), *((double*)pDatum)); break;
		default:
			das_error(DASERR_NOTIMP, "Binary operation not yet implemented ");
		}

		break;
	
	/* If output is a time then the left side better be a time and the operation
	 * is to add to the seconds field and then normalize */
	case vtTime:
		if(pDatum->vt != vtTime){
			das_error(DASERR_ASSERT, "Logic mismatch between das_vt_merge and DasVarBinary_get");
			return false;
		}
		
		switch(dmRight.vt){
		case vtByte:   dTmp = *((uint8_t*)pDatum); break;
		case vtShort:  dTmp = *((int16_t*)pDatum); break;
		case vtUShort: dTmp = *((uint16_t*)pDatum);break;
		case vtInt:    dTmp = *((int32_t*)pDatum); break;
		case vtLong:   dTmp = *((int64_t*)pDatum); break;
		case vtFloat:  dTmp = *((float*)pDatum);   break;
		case vtDouble: dTmp = *((double*)pDatum);  break;
		default:
			das_error(DASERR_ASSERT, "Logic mismatch between das_vt_merge and DasVarBinary_get");
			return false;
		}
		
		switch(pThis->nOp){
		case D2BOP_ADD:  ((das_time*)pDatum)->second += dTmp; break;
		case D2BOP_SUB:  ((das_time*)pDatum)->second -= dTmp; break;
		default:
			das_error(DASERR_ASSERT, "Logic mismatch between das_vt_merge and DasVarBinary_get");
			return false;
		}
		dt_tnorm((das_time*)pDatum);
		break;
		
	default:
		das_error(DASERR_ASSERT, "Logic mismatch between das_vt_merge and DasVarBinary_get");
		return false;
	}
	
	return true;
}

/* Fill propogates, if either item is fill, the result is fill */
bool DasVarBinary_isFill(const DasVar* pBase, const byte* pCheck, das_val_type vt)
{
	DasVarBinary* pThis = (DasVarBinary*)pBase;
	
	bool bEither = (pThis->pLeft->isFill(pThis->pLeft, pCheck, vt) ||
	                pThis->pRight->isFill(pThis->pRight, pCheck, vt) );
	return bEither;
}

int dec_DasVarBinary(DasVar* pBase){
	pBase->nRef -= 1;
	if(pBase->nRef > 0) return pBase->nRef;
	
	DasVarBinary* pThis = (DasVarBinary*)pBase;
	pThis->pLeft->decRef(pThis->pLeft);
	pThis->pRight->decRef(pThis->pRight);
	free(pThis);
	return 0;
}

DasVar* new_DasVarBinary_tok(DasVar* pLeft, int op, DasVar* pRight)
{
	if(op != D2BOP_ADD){
		das_error(DASERR_NOTIMP, "Only the addition operator is implemented at this time");
		return NULL;
	}
	
	if(!Units_canMerge(pLeft->units, op, pRight->units)){
		das_error(DASERR_VAR, 
			"Units of '%s' can not be combined with units '%s' using operation %s",
			Units_toStr(pRight->units), Units_toStr(pLeft->units), das_op_toStr(op, NULL)
		);
		return NULL;
	}
	
	if(pLeft->iFirstInternal != pRight->iFirstInternal){
		das_error(DASERR_VAR,
			"Sub variables appear to be from different datasets, on with %d "
			"indices, the other with %d.", pLeft->iFirstInternal,
			pRight->iFirstInternal
		);
		return NULL;
	}
	
	das_val_type vt = das_vt_merge(pLeft->vt, op, pRight->vt);
	if(vt == vtUnknown){ 
		das_error(DASERR_VAR, "Don't know how to merge types %s and %s under "
				  "operation %s", das_vt_toStr(pLeft->vt), 
				  das_vt_toStr(pRight->vt), das_op_toStr(op, NULL));
		return NULL;
	}
	
	DasVarBinary* pThis = (DasVarBinary*)calloc(1, sizeof(DasVarBinary));
	
	pThis->base.vartype    = D2V_BINARY_OP;
	pThis->base.vt         = vt;
	pThis->base.nRef       = 1;
	pThis->base.decRef     = dec_DasVarBinary;
	pThis->base.expression = DasVarBinary_expression;
	pThis->base.incRef     = inc_DasVar;
	pThis->base.get        = DasVarBinary_get;
	pThis->base.shape      = DasVarBinary_shape;
	pThis->base.lengthIn   = DasVarBinary_lengthIn;
	pThis->base.iFirstInternal = pRight->iFirstInternal;
	pThis->base.isFill     = DasVarBinary_isFill;
			  
			  
	/* Extra items for this derived class including any conversion factors that
	 * must be applied to the pLeft hand values so that they are in the same
	 * units as the pRight */
	pThis->nOp = op;
	pThis->pLeft = pLeft;
	pThis->pRight = pRight;
	
	/* Save any conversion factors that must be applied to the pRight hand 
	 * values so that they are in the same units as the pLeft hand value */
	if(Units_haveCalRep(pLeft->units)){
		das_units left_interval = Units_interval(pLeft->units);
		
		if(Units_haveCalRep(pRight->units)){
			das_units right_interval = Units_interval(pRight->units);
			pThis->rRightScale = Units_convertTo(right_interval, 1.0, left_interval);
			pThis->base.units = left_interval;
		}
		else{
			pThis->rRightScale = Units_convertTo(left_interval, 1.0, pRight->units);
			pThis->base.units = pLeft->units;
		}
	}
	else{
		/* Just regular numbers.  Scale if adding subtracting, merge units if
		 * multiply divide */
		switch(op){
		case D2BOP_ADD:
		case D2BOP_SUB:
			pThis->rRightScale = Units_convertTo(pRight->units, 1.0, pLeft->units);
			pThis->base.units = pLeft->units;
			break;
		
		case D2BOP_MUL:
			pThis->base.units = Units_multiply(pRight->units, pLeft->units);
			pThis->rRightScale = 1.0;
			break;
		
		case D2BOP_DIV:
			pThis->base.units = Units_divide(pRight->units, pLeft->units);
			pThis->rRightScale = 1.0;
			break;
		
		default:
			das_error(DASERR_VAR, 
				"I don't know how to combine units '%s' and '%s' under the operation"
				" '%s'", Units_toStr(pRight->units), Units_toStr(pLeft->units), 
				das_op_toStr(op, NULL)
			);
			free(pThis);
			return NULL;
			break;	 
		}
	}
	
	/* If we're going to scale the pRight value, then it's type will convert 
	 * to double.  That might change our output type */
	vt = das_vt_merge(pLeft->vt, op, vtDouble);
	if(vt == vtUnknown){
		free(pThis);
		das_error(DASERR_VAR, "Scaling converts vartype %s to %s, "
		   "Don't know how to merge types %s and %s under "
		   "operation %s", das_vt_toStr(pLeft->vt), das_vt_toStr(vtDouble),
		   das_vt_toStr(pLeft->vt), das_vt_toStr(vtDouble), 
		   das_op_toStr(op, NULL)
		);
		return NULL;
	}
	
	pRight->incRef(pRight);
	pLeft->incRef(pLeft);
	
	return &(pThis->base);
}

DasVar* new_DasVarBinary(DasVar* pLeft, const char* sOp, DasVar* pRight)
{
	int nOp = das_op_binary(sOp);
	if(nOp == 0) return NULL;
	return new_DasVarBinary_tok(pLeft, nOp, pRight);
}

/* ************************************************************************** */
/* Improperly implemented base class functions done this way to avoid         */
/* breaking binary compatability for libdas2.3.so users since we are now      */
/* declared stable, should add method pointer to struct variable              */

bool DasVar_isNumeric(const DasVar* pThis)
{
	/* Put most common ones first for faster checks */
	if((pThis->vt == vtFloat  ) || (pThis->vt == vtDouble ) ||
	   (pThis->vt == vtInt    ) || (pThis->vt == vtLong   ) || 
	   (pThis->vt == vtUShort ) || (pThis->vt == vtShort  ) ||
	   (pThis->vt == vtTime   ) ) return true;
	
	/* All the rest but vtByte are not numeric */
	if(pThis->vt != vtByte) return false;
	
	/* The special case, vtByte */
	const DasVarArray* pVarArray = NULL;
	const DasVarBinary* pVarBinOp = NULL;
	
	switch(pThis->vartype){
	case D2V_DATUM: return true; /* constants only hold a single byte */
	
	case D2V_ARRAY:
		pVarArray = (const DasVarArray*) pThis;   /* bad programmer, don't     */
		                                          /* upcast in baseclass funcs */
		return ! (DasAry_getUsage(pVarArray->pAry) & D2ARY_AS_SUBSEQ);
		
	case D2V_BINARY_OP:
		pVarBinOp = (const DasVarBinary*) pThis;    /* bad idea */
		
		return (DasVar_isNumeric(pVarBinOp->pLeft) && 
				  DasVar_isNumeric(pVarBinOp->pRight)    );
		
	default:
		das_error(DASERR_VAR, "Sequence and unary operation variables not yet "
		                      "implemented");
	}
	return false;  /* not yet implemented... */
}




