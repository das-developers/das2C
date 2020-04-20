/* Copyright (C) 2017-2020 Chris Piker <chris-piker@uiowa.edu>
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

size_t DasVar_valSize(const DasVar* pThis){return pThis->vsize;}

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
	const unsigned int uFlags = D2V_EXP_RANGE | D2V_EXP_UNITS | D2V_EXP_SUBEX;
	pThis->expression(pThis, sBuf, nLen, uFlags);
	return sBeg;
}

byte* DasVar_copy(
	const DasVar* pThis, const ptrdiff_t* pMin, const ptrdiff_t* pMax, 
	 int* pRank, ptrdiff_t* pShape
){
	return pThis->copy(pThis, pMin, pMax, pRank, pShape);
}

bool DasVar_isNumeric(const DasVar* pThis)
{
	return pThis->isNumeric(pThis);
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

/* Helper for all variable copy functions.  Determines if the slice
   arguments are valid and if so allocates memory for the output */

byte* _DasVar_getSliceMem(
	int nVarRank, const ptrdiff_t* pMin, const ptrdiff_t* pMax,
	size_t elSz, ptrdiff_t* pShape, int* pRank
){
	if((pMin == NULL)||(pMax == NULL)||(elSz < 1)||(pShape==NULL)||
		(nVarRank < 1)){
		das_error(DASERR_VAR, "Invalid slice copy argument");
		return NULL;
	}
	
	memcpy(pShape, g_aShapeUnused, DASIDX_MAX*sizeof(ptrdiff_t));
	
	/* Save the output shape of the buffer, and its rank */
	*pRank = 0;
	ptrdiff_t nSz = 0;
	int d = 0;
	for(d = 0; d < nVarRank; ++d){		
		nSz = pMax[d] - pMin[d];
		if((nSz <= 0)||(pMin[d] < 0)||(pMax[d] < 1)){
			das_error(
				DASERR_VAR, "Invalid %c slice range %zd to %zd", letter_idx[d],
				pMin[d], pMax[d]
			);
			return NULL;
		}
			
		if(nSz > 1){
			pShape[*pRank] = nSz;
			*pRank = *pRank + 1;
		}
	}

	
	/* Allocate output memory */
	int64_t nItems = 1;
	
	for(d = 0; d < nVarRank; ++d) 
		nItems *= (pMax[d] - pMin[d]);
	
	char sSlice[512] = {'\0'};
	char* pWrite = sSlice;
	if(nItems <= 0){
		for(d = 0; d < nVarRank; ++d){
			if(d > 0)
				pWrite += snprintf(
					pWrite, 511 - (pWrite - sSlice), ", %c:%zd..%zd", letter_idx[d],
					pMin[d], pMax[d]
				);
			else
				pWrite += snprintf(
					pWrite, 511 - (pWrite - sSlice), "%c:%zd..%zd", letter_idx[d],
					pMin[d], pMax[d]
				);
		}
		das_error(DASERR_VAR, "Invalid slice request ( %s)", sSlice);
		return NULL;
	}
	
	byte* pOut = (byte*)calloc(nItems, elSz);
	
	if(pOut == NULL){
		das_error(DASERR_VAR, "Failed to calloc %zd bytes", elSz*nItems);
		return NULL;
	}
	
	return pOut;
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
	free(pBase);
	return 0;
}

/* Doesn't even look at the index */
bool DasConstant_get(const DasVar* pBase, ptrdiff_t* pLoc, das_datum* pDatum)
{
	const DasConstant* pThis = (const DasConstant*)pBase;
	memcpy(pDatum, pThis->constant, pBase->vsize);
	pDatum->vt = pBase->vt;
	pDatum->vsize = pBase->vsize;
	pDatum->units = pBase->units;
	return true;
}

bool DasConstant_isNumeric(const DasVar* pBase)
{
	return ((pBase->vt == vtFloat  ) || (pBase->vt == vtDouble ) ||
	        (pBase->vt == vtInt    ) || (pBase->vt == vtLong   ) || 
	        (pBase->vt == vtUShort ) || (pBase->vt == vtShort  ) ||
	        (pBase->vt == vtTime   ) || (pBase->vt == vtByte   )    );
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

int DasConstant_shape(const DasVar* pBase, ptrdiff_t* pShape)
{
	for(int i = 0; i < DASIDX_MAX; ++i) pShape[i] = DASIDX_FUNC;
	return 0;
}

ptrdiff_t DasConstant_lengthIn(const DasVar* pBase, int nIdx , ptrdiff_t* pLoc)
{
	return DASIDX_FUNC;
}


bool DasConstant_isFill(const DasVar* pBase, const byte* pCheck, das_val_type vt)
{
	return false;
}

byte* DasConstant_copy(
	const DasVar* pBase, const ptrdiff_t* pMin, const ptrdiff_t* pMax,
	int* pRank, ptrdiff_t* pShape
){
	byte* pBuf = _DasVar_getSliceMem(
		pBase->iFirstInternal, pMin, pMax, pBase->vsize, pShape, pRank
	);
	if(pBuf == NULL) return NULL;
	
	/* initialize to minimum value*/
	size_t uCount = 1;
	for(int i = 0; i < *pRank; ++i) uCount *= pShape[i];
	
	/* Just copy the full amount in one go */
	DasConstant* pThis = (DasConstant*)pBase;
	return das_memset(pBuf, pThis->constant, pBase->vsize, uCount);
}


DasVar* new_DasConstant(
	das_val_type vt, size_t sz, const void* val, int nDsRank, das_units units
){
	DasConstant* pThis = (DasConstant*)calloc(1, sizeof(DasConstant));
	
	pThis->base.vartype = D2V_DATUM;
	pThis->base.units = units;
	pThis->base.vt = vt;
	if(vt == vtUnknown) pThis->base.vsize = sz;
	else pThis->base.vsize = das_vt_size(vt);
	
	pThis->base.iFirstInternal = nDsRank;
	pThis->base.decRef     = dec_DasConstant;
	pThis->base.isNumeric  = DasConstant_isNumeric;
	pThis->base.expression = DasConstant_expression;
	pThis->base.incRef     = inc_DasVar;
	pThis->base.nRef       = 1;
	pThis->base.get        = DasConstant_get;
	pThis->base.shape      = DasConstant_shape;
	pThis->base.lengthIn   = DasConstant_lengthIn;
	pThis->base.isFill     = DasConstant_isFill;
	pThis->base.copy       = DasConstant_copy;
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

bool DasVarAry_isNumeric(const DasVar* pBase)
{
	/* Put most common ones first for faster checks */
	if((pBase->vt == vtFloat  ) || (pBase->vt == vtDouble ) ||
	   (pBase->vt == vtInt    ) || (pBase->vt == vtLong   ) || 
	   (pBase->vt == vtUShort ) || (pBase->vt == vtShort  ) ||
	   (pBase->vt == vtTime   ) ) return true;
	
	/* All the rest but vtByte are not numeric */
	if(pBase->vt == vtByte){
		const DasVarArray* pThis = (const DasVarArray*) pBase;
		return ! (DasAry_getUsage(pThis->pAry) & D2ARY_AS_SUBSEQ);
	}
	
	return false;
}

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
	ptrdiff_t pAryLoc[16] = {0};
	
	
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


bool _DasVarAry_canStride(
	const DasVarArray* pThis, const ptrdiff_t* pMin, const ptrdiff_t* pMax
){
	/* You can't have more than ane increment (of a ragged rage)
	 * So say J is ragged, and you only want one I then that's okay.
	 * If you want more than one I then the stride equation no longer
	 * works. 
	 */
	ptrdiff_t shape[DASIDX_MAX] = DASIDX_INIT_UNUSED;
	DasAry_shape(pThis->pAry, shape);
	
	/* the more than one of a ragged range test */
	int d = 0;
	int iFirstUsed = -1;
	ptrdiff_t nSzFirstUsed = 0;
	int iFirstRagged = -1;
	int iLoc;
	
	int nVarRank = pThis->base.iFirstInternal;
	
	for(d = 0; d < nVarRank; ++d){
		if(pThis->idxmap[d] == DASIDX_UNUSED) continue;
		
		iLoc = pThis->idxmap[d];              /* the real index */
		if(iFirstUsed == -1){
			iFirstUsed = iLoc;
			nSzFirstUsed = pMax[d] - pMin[d];
			continue;
		}
		
		if((shape[iLoc] == DASIDX_RAGGED)&&(iFirstRagged == -1)){
			iFirstRagged = iLoc;
			break;
		}
	}
	
	/* first ragged only set after first used */	
	return (iFirstRagged == -1) || (nSzFirstUsed == 1);
}

byte* _DasVarAry_strideSlice(
	const DasVarArray* pThis, const ptrdiff_t* pMin, const ptrdiff_t* pMax, 
	ptrdiff_t* pShape, int* pRank
){	
	int nRank = pThis->base.iFirstInternal;
	size_t elSz = pThis->base.vsize;
	
	byte* pBuf = _DasVar_getSliceMem(nRank, pMin, pMax, elSz, pShape, pRank);
	if(pBuf == NULL) return NULL;
	
	/* Get the base starting point pointer */	
	ptrdiff_t base_idx[DASIDX_MAX] = {0};
	int d = 0;
	int iLoc = 0;
	for(d = 0; d < nRank; ++d){
		iLoc = pThis->idxmap[d];
		if(iLoc == DASIDX_UNUSED) continue;
		base_idx[iLoc] = pMin[d];
	}
	size_t uRemain = 0;
	const byte* pBase;
	pBase = DasAry_getIn(pThis->pAry, pThis->base.vt, nRank, base_idx, &uRemain);
	
	/* make a variable stride from the array stride */
	ptrdiff_t ary_stride[DASIDX_MAX];
	ptrdiff_t var_stride[DASIDX_MAX] = {0};
	DasAry_stride(pThis->pAry, ary_stride);
	
	for(d = 0; d < nRank; d++){
		/* If only 1 value is chosen for this index there is no striding */
		if((pMax[d] - pMin[d]) == 1) continue;
		iLoc = pThis->idxmap[d];
		if(iLoc == DASIDX_UNUSED) continue;
				
		var_stride[d] = ary_stride[iLoc];
	}
	
	/* Stride over the array copying values */
	ptrdiff_t idx_cur[DASIDX_MAX];
	memcpy(idx_cur, pMin, nRank * sizeof(ptrdiff_t));
	const byte* pRead = pBase;
	byte* pWrite = pBuf;
	
	/* Copy the data.  Unroll the loop up to dimension 4.  Unchecked there
	 * are *all* kinds of security errors here:
	 *
	 * 1. We could write off the end of the buffer
	 * 2. We could read outside array memory. 
	 *
	 */
	switch(nRank){
	case 1:
		while(idx_cur[0] < pMax[0]){
			pRead = pBase; 
			pRead += idx_cur[0]*var_stride[0];
			memcpy(pWrite, pRead, elSz);
			idx_cur[0] += 1;
			pWrite += elSz;
		}
		break;
	
	case 2:
		while(idx_cur[0] < pMax[0]){
			pRead = pBase;
			pRead += idx_cur[0]*var_stride[0];
			pRead += idx_cur[1]*var_stride[1];
			
			memcpy(pWrite, pRead, elSz);

			idx_cur[1] += 1;
			if(idx_cur[1] == pMax[1]){
				idx_cur[1] = pMin[1];
				idx_cur[0] += 1;
			}
			pWrite += elSz;
		}
		break;
	
	case 3:
		while(idx_cur[0] < pMax[0]){
			pRead = pBase;
			pRead += idx_cur[0]*var_stride[0];
			pRead += idx_cur[1]*var_stride[1];
			pRead += idx_cur[2]*var_stride[2];
			
			memcpy(pWrite, pRead, elSz);

			idx_cur[2] += 1;
			if(idx_cur[2] == pMax[2]){
				idx_cur[2] = pMin[2];
				idx_cur[1] += 1;
				if(idx_cur[1] == pMax[1]){
					idx_cur[1] = pMin[1];
					idx_cur[0] += 1;
				}
			}
			pWrite += elSz;
		}
		break;
		
	case 4:
		while(idx_cur[0] < pMax[0]){
			pRead = pBase;
			pRead += idx_cur[0]*var_stride[0];
			pRead += idx_cur[1]*var_stride[1];
			pRead += idx_cur[2]*var_stride[2];
			pRead += idx_cur[3]*var_stride[3];
			
			memcpy(pWrite, pRead, elSz);

			idx_cur[3] += 1;
			if(idx_cur[3] == pMax[3]){
				idx_cur[3] = pMin[3];
				idx_cur[2] += 1;
				if(idx_cur[2] == pMax[2]){
					idx_cur[2] = pMin[2];
					idx_cur[1] += 1;
					if(idx_cur[1] == pMax[1]){
						idx_cur[1] = pMin[1];
						idx_cur[0] += 1;
					}					
				}
			}
			pWrite += elSz;
		}
		break;
		
	default:
		/* all higher dimensions, now we need inner loops */
		while(idx_cur[0] < pMax[0]){
			pRead = pBase;
			for(d = 0; d < nRank; ++d) pRead += idx_cur[d]*var_stride[d];
		
			memcpy(pWrite, pRead, elSz);
			
			/* Roll index */
			for(d = nRank - 1; d > -1; --d){
				idx_cur[d] += 1;
				if((d > 0) && (idx_cur[d] == pMax[d]))  
					idx_cur[d] = pMin[d];
				else	
					break;  /* Stop rolling */
			}
			
			pWrite += elSz;
		}	
		break;		
	}
	
	return pBuf;
}


/* NOTES in: variable.md. */
byte* DasVarAry_copy(
	const DasVar* pBase, const ptrdiff_t* pMin, const ptrdiff_t* pMax,
	int* pRank, ptrdiff_t* pShape
){
	const DasVarArray* pThis = (DasVarArray*)pBase;
	
	memcpy(pShape, g_aShapeUnused, DASIDX_MAX*sizeof(ptrdiff_t));
	
	/* See if the trival copy is requested, this is just all the array
	   memory. This happens when pMin = zeros and pMax = shape of
		the underlying array and the index map is the direct map i.e.
		[0, 1, 2, ...]  */
	
	byte* pRet = NULL;
	ptrdiff_t ary_shape[DASIDX_MAX] = DASIDX_INIT_UNUSED;
	int nAryRank = DasAry_shape(pThis->pAry, ary_shape);
	int nVarRank = pBase->iFirstInternal;
	
	bool bTrival = (nVarRank == nAryRank);
	int d = 0;
	size_t uValues = 0;
	size_t uValSz  = 0;
	if(bTrival){
		for(d = 0; d < nVarRank; ++d){
			if((pMin[d] != 0)||(pMax[d] != ary_shape[d])||(pThis->idxmap[d] != d)){
				bTrival = false;
				break;
			}
		}
		if(bTrival){
			uValues = DasAry_size(pThis->pAry);
			uValSz  = DasAry_valSize(pThis->pAry);
			pRet = (byte*)calloc(uValues, uValSz);
			if(pRet == NULL) {
				das_error(DASERR_VAR, 
					"Couldn't allocate array of %zu elements of size %zu",
					uValues, uValSz
				);
				return NULL;
			}
						    
			*pRank = nVarRank;
			memcpy(pShape, pMax, nVarRank*sizeof(ptrdiff_t));
			memcpy(pRet, pBase, uValues * uValSz);
			return pRet;
		}
	}
	
	/* Given the requested slice, the index map and the raggedness of the
	   underlying array, can I still use the stride equation for fast offsets?
	 */
	bool bCanStride = _DasVarAry_canStride(pThis, pMin, pMax);

	if(bCanStride){
		pRet = _DasVarAry_strideSlice(pThis, pMin, pMax, pShape, pRank);
	}
	else{
		das_error(DASERR_VAR, "Ragged copy out not yet implemented");
		/* pRet = _DasVarAry_raggedSlice(pThis, pMin, pMax, pShape, pRank); */
	}
	
	return pRet;
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
	
	if((pBase->units != UNIT_DIMENSIONLESS) && (uFlags & D2V_EXP_UNITS)){
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
	pThis->base.isNumeric  = DasVarAry_isNumeric;
	pThis->base.expression = DasVarAry_expression;
	pThis->base.incRef     = inc_DasVar;
	pThis->base.get        = DasVarAry_get;
	pThis->base.shape      = DasVarAry_shape;
	pThis->base.lengthIn   = DasVarAry_lengthIn;
	pThis->base.isFill     = DasVarAry_isFill;
	pThis->base.copy       = DasVarAry_copy;
	
	
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
	char sBuf[128] = {'\0'};
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
				           DasAry_toStr(pAry, sBuf, 127));
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
	int iDep;      /* The one and only index I depend on */
	char sId[DAS_MAX_ID_BUFSZ];  /* Since we can't just use our array ID */
	
	byte B[sizeof(das_time)];  /* Intercept */
	byte* pB;
	
	byte M[sizeof(das_time)];  /* Slope */
	byte* pM;
	
} DasVarSeq;

int dec_DasVarSeq(DasVar* pBase){
	pBase->nRef -= 1;
	if(pBase->nRef > 0) return pBase->nRef;
	free(pBase);
	return 0;
}

bool DasVarSeq_get(const DasVar* pBase, ptrdiff_t* pLoc, das_datum* pDatum)
{
	const DasVarSeq* pThis = (const DasVarSeq*)pBase;
	
	if(pDatum == NULL){
		das_error(DASERR_VAR, "NULL datum pointer");
		return false;
	}
	
	/* Can't use negative indexes with a sequence because it doesn't know
	   how big it is! */
	
	if(pLoc[pThis->iDep] < 0){
		das_error(DASERR_VAR, "Negative indexes undefined for sequences");
		return false;
	}
	
	pDatum->vt = pBase->vt;
	pDatum->vsize = pBase->vsize;
	pDatum->units = pBase->units;
	
	size_t u = (size_t)(pLoc[pThis->iDep]);
			  
	/* casting to smaller types is well defined for unsigned values only 
	 * according to the C standards that I can't read because they cost money.
	 * (why have a standard and hide it behind a paywall?) */
			  
	switch(pThis->base.vt){
	case vtByte: 
		*((byte*)pDatum) = *(pThis->pM) * ((byte)u) + *(pThis->pB);
		return true;
	case vtUShort:
		*((uint16_t*)pDatum) = *( (uint16_t*)pThis->pM) * ((uint16_t)u) + 
		                       *( (uint16_t*)pThis->pB);
		return true;
	case vtShort:
		if(u > 32767){
			das_error(DASERR_VAR, "Range error, max index for vtShort sequence is 32,767");
			return false;
		}
		*((int16_t*)pDatum) = *( (int16_t*)pThis->pM) * ((int16_t)u) + 
		                      *( (int16_t*)pThis->pB);
		return true;
	case vtInt:
		if(u > 2147483647){
			das_error(DASERR_VAR, "Range error max index for vtInt sequence is 2,147,483,647");
			return false;
		}
		*((int32_t*)pDatum) = *( (int32_t*)pThis->pM) * ((int32_t)u) + 
		                      *( (int32_t*)pThis->pB);
		return true;
	case vtLong:
		*((int64_t*)pDatum) = *( (int64_t*)pThis->pM) * ((int64_t)u) + 
		                      *( (int64_t*)pThis->pB);
		return true;
	case vtFloat:
		*((float*)pDatum) = *( (float*)pThis->pM) * u + *( (float*)pThis->pB);
		return true;
	case vtDouble:
		*((double*)pDatum) = *( (double*)pThis->pM) * u + *( (double*)pThis->pB);
		return true;
	case vtTime:
		/* Here assume that the intercept is a dastime, then add the interval.
		 * The constructor saves the interval in seconds using the units value */
		*((das_time*)pDatum) = *((das_time*)(pThis->pB));
		((das_time*)pDatum)->second += *( (double*)pThis->pM ) * u;
		dt_tnorm( (das_time*)pDatum );
		return true;
	default:
		das_error(DASERR_VAR, "Unknown data type %d", pThis->base.vt);
		return false;	
	}
}

bool DasVarSeq_isNumeric(const DasVar* pBase)
{
	return true;   /* Text based sequences have not been implemented */
}

/* Returns the pointer an the next write point of the string */
char* DasVarSeq_expression(
	const DasVar* pBase, char* sBuf, int nLen, unsigned int uFlags
){
	if(nLen < 3) return sBuf;
	
	/* Output is: 
	 *   B + A * i  units    (most sequences) 
	 *   B + A * i s  UTC (time sequences)
	 */
	
	memset(sBuf, 0, nLen);  /* Insure null termination whereever I stop writing */
	
	const DasVarSeq* pThis = (const DasVarSeq*)pBase;
	
	int nWrote = strlen(pThis->sId);
	nWrote = nWrote > (nLen - 1) ? nLen - 1 : nWrote;
	char* pWrite = sBuf;
	strncpy(sBuf, pThis->sId, nWrote);
	
	pWrite = sBuf + nWrote;  nLen -= nWrote;
	if(nLen < 4) return pWrite;
	int i;
	
	*pWrite = '['; ++pWrite; --nLen;
	*pWrite = letter_idx[pThis->iDep]; ++pWrite; --nLen;
	*pWrite = ']'; ++pWrite; --nLen;
	
	/* Print units if desired */
	if(uFlags & D2V_EXP_UNITS){
		char* pNewWrite = _DasVar_prnUnits(pBase, pWrite, nLen);
		nLen -= (pNewWrite - pWrite);
		pWrite = pNewWrite;
	}
	
	/* The rest is range printing... */
	if(! (uFlags & D2V_EXP_RANGE)) return pWrite;
	
	if(nLen < 3) return pWrite;
	strncpy(pWrite, " | ", 3);
	pWrite += 3;
	nLen -= 3;
	
	das_datum dm;
	dm.units = pThis->base.units;
	dm.vt    = pThis->base.vt;
	dm.vsize = pThis->base.vsize;
	
	das_time dt;
	int nFracDigit = 5;
	if(pThis->base.vt == vtTime){
		dt = *((das_time*)(pThis->pB));
		*((das_time*)&dm) = dt;
		if(dt.second == 0.0) nFracDigit = 0;
		das_datum_toStrValOnly(&dm, pWrite, nLen, nFracDigit);
	}
	else{
		for(i = 0; i < dm.vsize; ++i) dm.bytes[i] = pThis->pB[i];
		das_datum_toStrValOnly(&dm, pWrite, nLen, 5);
	}
	
	nWrote = strlen(pWrite);
	nLen -= nWrote;
	pWrite += nWrote;
	
	if(nLen < 3) return pWrite;
	strncpy(pWrite, " + ", 3);
	pWrite += 3; nLen -= 3;
	
	if(nLen < 7) return pWrite;
	
	if(pThis->base.vt == vtTime){
		das_datum_fromDbl(&dm, *((double*)pThis->pM), UNIT_SECONDS);
	}
	else{
		for(i = 0; i < dm.vsize; ++i) dm.bytes[i] = pThis->pM[i];
	}
	
	das_datum_toStrValOnly(&dm, pWrite, nLen, 5);
	nWrote = strlen(pWrite);
	nLen -= nWrote;
	pWrite += nWrote;
	
	if(nLen < 3) return pWrite;
	*pWrite = '*'; ++pWrite; --nLen;
	*pWrite = letter_idx[pThis->iDep];  ++pWrite; --nLen;
	
	if(pBase->units == UNIT_DIMENSIONLESS) return pWrite;
	if( (uFlags & D2V_EXP_UNITS) == 0) return pWrite;
	
	if(nLen < 3) return pWrite;
	
	*pWrite = ' '; pWrite += 1;
	nLen -= 1;
	
	return pWrite;
}

int DasVarSeq_shape(const DasVar* pBase, ptrdiff_t* pShape){
	DasVarSeq* pThis = (DasVarSeq*)pBase;
	
	for(int i = 0; i < DASIDX_MAX; ++i)
		pShape[i] = pThis->iDep == i ? DASIDX_FUNC : DASIDX_UNUSED;
	return 0;
}

ptrdiff_t DasVarSeq_lengthIn(const DasVar* pBase, int nIdx, ptrdiff_t* pLoc)
{
	/* The location works on the directed graph assumption.  Since simple
	 * sequences are homogenous in index space (i.e. not ragged) then we
	 * only actually care about the number of indices specified.  If it's
	 * not equal to our dependent index then it's immaterial what pLoc is.
	 */
	DasVarSeq* pThis = (DasVarSeq*)pBase;
	
	if(nIdx == (pThis->iDep + 1)) return DASIDX_FUNC;
	else return DASIDX_UNUSED;
}


bool DasVarSeq_isFill(const DasVar* pBase, const byte* pCheck, das_val_type vt)
{
	return false;
}

/* NOTES in: variable.md. */
byte* DasVarSeq_copy(
	const DasVar* pBase, const ptrdiff_t* pMin, const ptrdiff_t* pMax,
	int* pRank, ptrdiff_t* pShape
){
	byte* pBuf = _DasVar_getSliceMem(
		pBase->iFirstInternal, pMin, pMax, pBase->vsize, pShape, pRank
	);
	if(pBuf == NULL) return NULL;
	
	/* We are expanding a 1-D item.  If my dependent index is not the last one
	 * then each value will be copied multiple times.  If my dependent index
	 * is not the first one, then each complete set will be copied multiple
	 * times.  
	 * 
	 * Since this is only dependent on a single axis, there is only a need
	 * for a loop in that one axis.
	 */
	DasVarSeq* pThis = (DasVarSeq*)pBase;
	size_t uMin = pMin[pThis->iDep];
	size_t uMax = pMax[pThis->iDep];
	
	size_t u, uSzElm = pBase->vsize;
	
	size_t uRepEach = 1;
	for(int d = pThis->iDep + 1; d < pBase->iFirstInternal; ++d) 
		uRepEach *= (pMax[d] - pMin[d]);
	
	size_t uBlkBytes = (pMax[pThis->iDep] - pMin[pThis->iDep]) * uRepEach * uSzElm;
	
	size_t uRepBlk = 1; 
	for(int d = 0; d < pThis->iDep; ++d) 
		uRepBlk *= (pMax[d] - pMin[d]);
	
	byte value[sizeof(das_time)];
	byte* pVal = value;
	byte* pWrite = pBuf;
	size_t uWriteInc = 0;
	
	/* I know it's messier, but put the switch on the outside so we don't hit 
	 * it on each loop iteration */
	uWriteInc = uRepEach * uSzElm;
	
	switch(pThis->base.vt){
	case vtByte:	
		for(u = uMin; u < uMax; ++u){
			/* The Calc */
			*pVal = *(pThis->pM) * ((byte)u) + *(pThis->pB); 
			
			das_memset(pWrite, pVal, uSzElm, uRepEach);
			pWrite += uWriteInc;
		}
		break;
	
	case vtUShort:
		for(u = uMin; u < uMax; ++u){
			 /* The Calc */
			*((uint16_t*)pVal) = *( (uint16_t*)pThis->pM) * ((uint16_t)u) + 
		                        *( (uint16_t*)pThis->pB);
			
			das_memset(pWrite, pVal, uSzElm, uRepEach);
			pWrite += uWriteInc;
		}
		break;
	
	case vtShort:
		for(u = uMin; u < uMax; ++u){
			if(u > 32767){
				das_error(DASERR_VAR, "Range error, max index for vtShort sequence is 32,767");
				free(pBuf);
				return false;
			}
			/* The Calc */
			*((int16_t*)pVal) = *( (int16_t*)pThis->pM) * ((int16_t)u) + 
			                    *( (int16_t*)pThis->pB);
			das_memset(pWrite, pVal, uSzElm, uRepEach);
			pWrite += uWriteInc;
		}
		break;
		
	case vtInt:
		for(u = uMin; u < uMax; ++u){
			if(u > 2147483647){
				das_error(DASERR_VAR, "Range error max index for vtInt sequence is 2,147,483,647");
				free(pBuf);
				return false;
			}
			/* The Calc */
			*((int32_t*)pVal) = *( (int32_t*)pThis->pM) * ((int32_t)u) + 
				                 *( (int32_t*)pThis->pB);
			das_memset(pWrite, pVal, uSzElm, uRepEach);
			pWrite += uWriteInc;
		}
		break;
		
	case vtLong:
		for(u = uMin; u < uMax; ++u){
			/* The Calc */
			*((int64_t*)pVal) = *( (int64_t*)pThis->pM) * ((int64_t)u) + 
		                       *( (int64_t*)pThis->pB);
			das_memset(pWrite, pVal, uSzElm, uRepEach);
			pWrite += uWriteInc;
		}
		break;
		
	case vtFloat:
		for(u = uMin; u < uMax; ++u){
			/* The Calc */
			*((float*)pVal) = *( (float*)pThis->pM) * u + *( (float*)pThis->pB);
			das_memset(pWrite, pVal, uSzElm, uRepEach);
			pWrite += uWriteInc;
		}
		break;
		
	case vtDouble:
		for(u = uMin; u < uMax; ++u){
			/* The Calc */
			*((double*)pVal) = *( (double*)pThis->pM) * u + *( (double*)pThis->pB);
			das_memset(pWrite, pVal, uSzElm, uRepEach);
			pWrite += uWriteInc;
		}
		break;
		
	case vtTime:
		for(u = uMin; u < uMax; ++u){
			
			/* The Calc */
			*((das_time*)pVal) = *((das_time*)(pThis->pB));
			((das_time*)pVal)->second += *( (double*)pThis->pM ) * u;
			dt_tnorm( (das_time*)pVal );
			
			das_memset(pWrite, pVal, uSzElm, uRepEach);
			pWrite += uWriteInc;
		}
		break;
	
	default:
		das_error(DASERR_VAR, "Unknown data type %d", pBase->vt);
		free(pBuf);
		return false;
	}
	
	/* Now replicate the whole blocks if needed */
	if(uRepBlk > 1)
		das_memset(pWrite, pBuf, uBlkBytes, uRepBlk);
	
	return pBuf;
}

DasVar* new_DasVarSeq(
	const char* sId, das_val_type vt, size_t vSz, const void* pMin, 
	const void* pInterval, int nDsRank, int8_t* pMap, das_units units
){
	if((sId == NULL)||((vt == vtUnknown)&&(vSz == 0))||(pMin == NULL)||
	   (pInterval == NULL)||(pMap == NULL)||(nDsRank < 1)){
		das_error(DASERR_VAR, "Invalid argument");
		return NULL;
	}
	
	if(vt == vtText){
		das_error(DASERR_VAR, "Text based sequences are not implemented");
		return NULL;
	}
	
	DasVarSeq* pThis = (DasVarSeq*)calloc(1, sizeof(DasVarSeq));
	
	if(!das_assert_valid_id(sId)) return NULL;
	strncpy(pThis->sId, sId, DAS_MAX_ID_BUFSZ - 1);
	
	pThis->base.vartype    = D2V_SEQUENCE;
	pThis->base.vt         = vt;
	if(vt == vtUnknown)
		pThis->base.vsize = vSz;
	else 
		pThis->base.vsize = das_vt_size(vt);
	
	pThis->base.iFirstInternal = nDsRank;
	pThis->base.nRef       = 1;
	pThis->base.units      = units;
	pThis->base.decRef     = dec_DasVarSeq;
	pThis->base.isNumeric  = DasVarSeq_isNumeric;
	pThis->base.expression = DasVarSeq_expression;
	pThis->base.incRef     = inc_DasVar;
	pThis->base.get        = DasVarSeq_get;
	pThis->base.shape      = DasVarSeq_shape;
	pThis->base.lengthIn   = DasVarSeq_lengthIn;
	pThis->base.isFill     = DasVarSeq_isFill;
	pThis->base.copy       = DasVarSeq_copy;

	
	pThis->iDep = -1;
	for(int i = 0; i < nDsRank; ++i){
		if(pMap[i] == 0){
			if(pThis->iDep != -1){
				das_error(DASERR_VAR, "Simple sequence can only depend on one axis");
				free(pThis);
				return NULL;
			}
			pThis->iDep = i;
		}
	}
	if(pThis->iDep < 0){
		das_error(DASERR_VAR, "Invalid dependent axis map");
		free(pThis);
		return NULL;
	}
	
	pThis->pB = pThis->B;
	pThis->pM = pThis->M;
	double rScale;
	
	switch(vt){
	case vtByte: 
		*(pThis->pB) = *((byte*)pMin);  *(pThis->pM) = *((byte*)pInterval);
		break;
	case vtUShort:
		*((uint16_t*)(pThis->pB)) = *((uint16_t*)pMin);  
		*((uint16_t*)(pThis->pM)) = *((uint16_t*)pInterval);
		break;
	case vtShort:
		*((int16_t*)(pThis->pB)) = *((int16_t*)pMin);  
		*((int16_t*)(pThis->pM)) = *((int16_t*)pInterval);
		break;
	case vtInt:
		*((int32_t*)(pThis->pB)) = *((int32_t*)pMin);  
		*((int32_t*)(pThis->pM)) = *((int32_t*)pInterval);
		break;
	case vtLong:
		*((int64_t*)(pThis->pB)) = *((int64_t*)pMin);  
		*((int64_t*)(pThis->pM)) = *((int64_t*)pInterval);
		break;
	case vtFloat:
		*((float*)(pThis->pB)) = *((float*)pMin);  
		*((float*)(pThis->pM)) = *((float*)pInterval);
		break;
	case vtDouble:
		*((double*)(pThis->pB)) = *((double*)pMin);  
		*((double*)(pThis->pM)) = *((double*)pInterval);
		break;
	case vtTime:
		/* Use the units to get the conversion factor for the slope to seconds */
		/* The save the units as UTC */
		rScale = Units_convertTo(UNIT_SECONDS, *((double*)pInterval), units);
		*((double*)(pThis->pM)) = rScale * *((double*)pInterval);
		pThis->base.units = UNIT_UTC;
		
		*((das_time*)pThis->pB) = *((das_time*)pMin);
		break;
	default:
		das_error(DASERR_VAR, "Value type %d not yet supported for sequences", vt);
		free(pThis);
		return NULL;
	}
	return (DasVar*)pThis;
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
	
	char sId[DAS_MAX_ID_BUFSZ];  /* The combination has it's own name, 
										   * may be empty for anoymous combinations */
	
	DasVar* pRight;      /* right hand sub-variable pointer */
	DasVar* pLeft;       /* left hand sub-variable pointer  */
	int     nOp;         /* operator for unary and binary operations */
	double  rRightScale; /* Scaling factor for right hand values */
} DasVarBinary;

bool DasVarBinary_isNumeric(const DasVar* pBase)
{
	/* Put most common ones first for faster checks */
	if((pBase->vt == vtFloat  ) || (pBase->vt == vtDouble ) ||
	   (pBase->vt == vtInt    ) || (pBase->vt == vtLong   ) || 
	   (pBase->vt == vtUShort ) || (pBase->vt == vtShort  ) ||
	   (pBase->vt == vtTime   ) ) return true;
	
	/* All the rest but vtByte are not numeric */
	if(pBase->vt != vtByte) return false;
	
	const DasVarBinary* pThis = (const DasVarBinary*) pBase;
		
	return (DasVar_isNumeric(pThis->pLeft) && 
			 DasVar_isNumeric(pThis->pRight)    );
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

/* Our expressions looks like this:  
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
	int nWrite = 0;
	int d = 0;
	ptrdiff_t aShape[DASIDX_MAX] = DASIDX_INIT_UNUSED;
	
	/* Write our named info if not anoymous */
	if(pThis->sId[0] != '\0'){
		nWrite = strlen(pThis->sId);
		nWrite = nWrite > (nLen - 1) ? nLen - 1 : nWrite;
		strncpy(sBuf, pThis->sId, nWrite);
	
		pWrite = sBuf + nWrite;  nLen -= nWrite;
		
		DasVarBinary_shape(pBase, aShape);
		for(d = 0; d < pBase->iFirstInternal; ++d){
			if(aShape[d] == DASIDX_UNUSED) continue;
			
			if(nLen < 3) return pWrite;
			*pWrite = '[';  ++pWrite; --nLen;
			*pWrite = letter_idx[d]; ++pWrite; --nLen;
			*pWrite = ']';  ++pWrite; --nLen;
		}
	}
	
	/* Add in the sub-expression if requested (or if we're anonymous) */
	char* pSubWrite = NULL;
	int nTmp = 0;
	char sScale[32] = {'\0'};
	if((uFlags & D2V_EXP_SUBEX)||(pThis->sId[0] == '\0')){
	
		if(nLen < 4) return pWrite;
		
		*pWrite = ' '; ++pWrite; *pWrite = '('; ++pWrite;
		nLen -= 2;
	
		pSubWrite = pThis->pLeft->expression(pThis->pLeft, pWrite, nLen, 0);
		nTmp = pSubWrite - pWrite;
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
		
		if(pThis->rRightScale != 1.0){
			snprintf(sScale, 31, "%.6e", pThis->rRightScale);
			/* Should pop off strings of zeros after the decimal pt here */
			nTmp = strlen(sScale);
			if(nTmp > (nLen - 2)) goto DAS_VAR_BIN_EXP_PUNT;
			strncpy(pWrite, sScale, nTmp);
			pWrite += nTmp;
			nLen -= nTmp;
			*pWrite = '*'; ++pWrite; --nLen;
		}
	
		pSubWrite = pThis->pRight->expression(pThis->pRight, pWrite, nLen, 0);
		nTmp = pSubWrite - pWrite;
		nLen -= nTmp;
		pWrite = pSubWrite;
		if((nTmp == 0)||(nLen < 3)) goto DAS_VAR_BIN_EXP_PUNT;
	
		*pWrite = ')'; ++pWrite; --nLen;
	}
	
	if(uFlags & D2V_EXP_UNITS){
		if(pBase->units != UNIT_DIMENSIONLESS){
			pSubWrite = _DasVar_prnUnits(pBase, pWrite, nLen);
			nLen -= pSubWrite - pWrite;
			pWrite = pSubWrite;
		}
	}
	
	if(uFlags & D2V_EXP_RANGE){
		pWrite = _DasVar_prnRange(&(pThis->base), pWrite, nLen);
	}
	
	return pWrite;
	
	DAS_VAR_BIN_EXP_PUNT:
	sBuf[0] = '\0';
	return sBuf;
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
		pDatum->vsize = sizeof(float);
		pDatum->vt = vtFloat;
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
			pDatum->vsize = sizeof(das_time);
			pDatum->vt = vtTime;
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

		pDatum->vsize = sizeof(double);
		pDatum->vt = vtDouble;
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
		pDatum->vsize = sizeof(das_time);
		pDatum->vt = vtTime;
		break;
		
	default:
		das_error(DASERR_ASSERT, "Logic mismatch between das_vt_merge and DasVarBinary_get");
		return false;
	}
	
	return true;
}

byte* DasVarBinary_copy(
	const DasVar* pBase, const ptrdiff_t* pMin, const ptrdiff_t* pMax,
	int* pRank, ptrdiff_t* pShape
){
	byte* pBuf = _DasVar_getSliceMem(
		pBase->iFirstInternal, pMin, pMax, pBase->vsize, pShape, pRank
	);
	if(pBuf == NULL) return NULL;
	
	/* Going to take the slow boat from China on this one.  Just repeatedly
	 * invoke the get function */
	
	ptrdiff_t pIdx[DASIDX_MAX] = DASIDX_INIT_UNUSED;
	memcpy(pIdx, pMin,  pBase->iFirstInternal * sizeof(ptrdiff_t));
	
	byte* pWrite = pBuf;
	das_datum dm;
	int d = 0;
	while(pIdx[0] < pMax[0]){
		
		if(!DasVarBinary_get(pBase, pIdx, &dm)){
			free(pBuf);
			return NULL;
		}
		
		memcpy(pWrite, &dm, dm.vsize);
		
		/* Roll the index */
		for(d = pBase->iFirstInternal - 1; d > -1; --d){
			pIdx[d] += 1;
			if((d > 0) && (pIdx[d] == pMax[d]))
				pIdx[d] = pMin[d];   /* next higher index will roll on loop iter */
			else
				break;  /* Stop rolling */
		}
		
		pWrite += dm.vsize;
	}
	
	return pBuf;
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

DasVar* new_DasVarBinary_tok(
	const char* sId, DasVar* pLeft, int op, DasVar* pRight
){
	if(pLeft == NULL){
		das_error(DASERR_VAR, "Left side variable NULL in binary var definition");
		return NULL;
	}
	if(pRight == NULL){
		das_error(DASERR_VAR, "Right side variable NULL in binary var definition");
		return NULL;
	}	
	
	if(!Units_canMerge(pLeft->units, op, pRight->units)){
		das_error(DASERR_VAR, 
			"Units of '%s' can not be combined with units '%s' using operation '%s'",
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
	
	if(sId != NULL){
		if(!das_assert_valid_id(sId)) return NULL;
	}
	
	DasVarBinary* pThis = (DasVarBinary*)calloc(1, sizeof(DasVarBinary));
	
	pThis->base.vartype    = D2V_BINARY_OP;
	pThis->base.vt         = vt;
	pThis->base.vsize      = das_vt_size(vt);
	pThis->base.nRef       = 1;
	pThis->base.decRef     = dec_DasVarBinary;
	pThis->base.isNumeric  = DasVarBinary_isNumeric;
	pThis->base.expression = DasVarBinary_expression;
	pThis->base.incRef     = inc_DasVar;
	pThis->base.get        = DasVarBinary_get;
	pThis->base.shape      = DasVarBinary_shape;
	pThis->base.lengthIn   = DasVarBinary_lengthIn;
	pThis->base.iFirstInternal = pRight->iFirstInternal;
	pThis->base.isFill     = DasVarBinary_isFill;
	pThis->base.copy       = DasVarBinary_copy;
	
	if(sId != NULL) strncpy(pThis->sId, sId, 63);
	
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

DasVar* new_DasVarBinary(
	const char* sId, DasVar* pLeft, const char* sOp, DasVar* pRight)
{
	int nOp = das_op_binary(sOp);
	if(nOp == 0) return NULL;
	return new_DasVarBinary_tok(sId, pLeft, nOp, pRight);
}







