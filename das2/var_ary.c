/* Copyright (C) 2017-2024 Chris Piker <chris-piker@uiowa.edu>
 *
 * This file is part of das2C, the Core Das2 C Library.
 *
 * Das2C is free software; you can redistribute it and/or modify it under
 * the terms of the GNU Lesser General Public License version 2.1 as published
 * by the Free Software Foundation.
 *
 * Das2C is distributed in the hope that it will be useful, but WITHOUT ANY
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

#include "frame.h"
#include "vector.h"
#include "stream.h"
#include "log.h"

/* ************************************************************************* */
/* Protected functions from the base class */

char* _DasVar_prnUnits(const DasVar* pThis, char* sBuf, int nLen);
char* _DasVar_prnRange(const DasVar* pThis, char* sBuf, int nLen);
char* _DasVar_prnType(const DasVar* pThis, char* pWrite, int nLen);
char* _DasVar_prnIntr(
	const DasVar* pThis, const char* sFrame, ubyte* pFrmDirs, ubyte nFrmDirs, 
	char* sBuf, int nBufLen
);
void _DasVar_copyTo(const DasVar* pThis, DasVar* pOther);

DasStream* _DasVar_getStream(const DasVar* pThis);

/* ************************************************************************* */
/* Array mapping functions */

enum var_subtype {D2V_STDARY=1, D2V_GEOVEC=2};

typedef struct das_var_array{
	DasVar base;
	
	/* Array pointer and index map to support lookup variables */
	DasAry* pAry; /* A pointer to the array containing the values */
	int idxmap[DASIDX_MAX];      /* i,j,k data set space to array space */

	enum var_subtype varsubtype;  /* Var sub type */
	
} DasVarAry;

/* Derived version, add's vector frame directions and a template for 
   returning values */

typedef struct das_var_vecary{
	DasVarAry base;
	das_geovec tplt;

} DasVarVecAry;


void _DasVarAry_copyTo(const DasVarAry* pThis, DasVarAry* pOther)
{
	_DasVar_copyTo((const DasVar*)pThis, (DasVar*)pOther);

	/* Add in my stuff, but don't copy the bulk data array, just bump
	   the reference count */
	if(pThis->pAry != NULL){
		pOther->pAry = pThis->pAry;
		inc_DasAry( pOther->pAry );
	}

	memcpy(pOther->idxmap, pThis->idxmap, DASIDX_MAX*sizeof(int));
	pOther->varsubtype = pThis->varsubtype;
}

DasVar* copy_DasVarAry(const DasVar* pBase){
	/* Why no run-time type checking here? 
		This function is only visible inside this module, and it's assigned 
		to a virtual function pointer.  Unless a caller explicitly changes
		a virtual function pointer in the base class, then the type will
		match the function. */
	assert(pBase->vartype == D2V_ARRAY);

	/* Can't do direct memcpy, this would end up with two independent copies of
	   
	      DasVarVecAry.base.base.base.properties.pIdx0

	   pointing to the same das_idx_info structure.  Handle it as a maunal copy
	*/

	DasVarAry* pRet = calloc(1, sizeof(DasVarAry));  /* Make something of my size */
	_DasVarAry_copyTo((const DasVarAry*)pBase, pRet);       /* Initialize it */

	return (DasVar*)pRet;
}

das_val_type DasVarAry_elemType(const DasVar* pBase){
	DasVarAry* pThis = (DasVarAry*)pBase;
	return DasAry_valType(pThis->pAry);
}


bool DasVarAry_degenerate(const DasVar* pBase, int iIndex)
{
	DasVarAry* pThis = (DasVarAry*)pBase;

	if((iIndex >= 0)&&(iIndex < DASIDX_MAX)){
		if(pThis->idxmap[iIndex] != DASIDX_UNUSED)
			return false;
	}
	return true;
}

bool DasVarAry_isNumeric(const DasVar* pBase)
{
	/* Put most common ones first for faster checks */
	if((pBase->vt == vtFloat  ) || (pBase->vt == vtDouble ) ||
		(pBase->vt == vtInt    ) || (pBase->vt == vtUInt   ) || 
		(pBase->vt == vtLong   ) || (pBase->vt == vtULong  ) || 
		(pBase->vt == vtUShort ) || (pBase->vt == vtShort  ) ||
		(pBase->vt == vtByte) /* signed bytes considered numeric */
	) return true;
	
	/* All the rest but vtUByte are not numeric */
	if(pBase->vt == vtUByte){
		const DasVarAry* pThis = (const DasVarAry*) pBase;
		return ! (DasAry_getUsage(pThis->pAry) & D2ARY_AS_SUBSEQ);
	}
	
	return false;
}

DasAry* DasVar_getArray(DasVar* pBase)
{
	if( pBase->vartype != D2V_ARRAY) return NULL;
	DasVarAry* pThis = (DasVarAry*)pBase;
	return pThis->pAry;
}

/* Public function, call from the top level 
 */ 
int DasVarAry_shape(const DasVar* pBase, ptrdiff_t* pShape)
{
	if(pShape == NULL){
		das_error(DASERR_VAR, "null shape pointer, can't output shape values");
		return -1;
	}
	
	const DasVarAry* pThis = (const DasVarAry*)pBase;
	
	/* Force a memory error right up front if they've provided a variable too
	 * short to hold the answer */
	for(int i = 0; i < DASIDX_MAX; ++i) pShape[i] = DASIDX_UNUSED;
	
	/* Must be an array function */
	ptrdiff_t aShape[DASIDX_MAX] = DASIDX_INIT_UNUSED;
	int nAryRank = DasAry_shape(pThis->pAry, aShape);
	int iAryIdx = -1;
	int nRank = 0;
	
	for(int iVarIdx = 0; iVarIdx < pBase->nExtRank; ++iVarIdx){
		if(pThis->idxmap[iVarIdx] == DASIDX_UNUSED)
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

int DasVarAry_intrShape(const DasVar* pBase, ptrdiff_t* pShape)
{
	
	assert(pBase->vartype == D2V_ARRAY);
	DasVarAry* pThis = (DasVarAry*)pBase;

	int i;
	for(i = 0; i < DASIDX_MAX; ++i)
		pShape[i] = DASIDX_UNUSED;

	ptrdiff_t aShape[DASIDX_MAX] = DASIDX_INIT_UNUSED;
	int nAryRank = DasAry_shape(pThis->pAry, aShape);

	if(pBase->nIntRank > 0){
		/* Just copy out the last nIntRank indicies of the array 
			because all internal indicies are dense. */
		int j = 0;
		for(i = (nAryRank - pBase->nIntRank); i < nAryRank; ++i){
			pShape[j] = aShape[i];
			++j;
		}
	}

	return pBase->nIntRank;
}

/* This one is tough.  What is my shape in a particular index given all 
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
	DasVarAry* pThis = (DasVarAry*)pBase;
	
	/* Map the location, it should provide a partial map */
	ptrdiff_t aAryLoc[DASIDX_MAX] = DASIDX_INIT_UNUSED;  /* we have to resolve all these
																			* to a positive number before
																			 * asking the array for its 
																			* size */
	int i = 0;
	int nIndexes = 0;
	/* nIdx is the number of indexes they want to "lock down", if they
	   don't want to lock down any then nIdx is 0 */
	for(i = 0; i <= nIdx; ++i){
		
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
	const DasVarAry* pThis = (const DasVarAry*)pBase;
	
	/* Ignore indices you don't understand, that's what makes this work */
	/* I need a way to make this code fast, maybe the loop should be unrolled? */
	ptrdiff_t pAryLoc[DASIDX_MAX] = DASIDX_INIT_BEGIN;
	
	int nDim = 0;
	for(int i = 0; i < pBase->nExtRank; ++i){
		if(pThis->idxmap[i] >= 0){ /* all the wierd flags are less than 0 */
			pAryLoc[ pThis->idxmap[i] ] = pLoc[i];
			++nDim;
		}
	}

	das_val_type vtAry = DasAry_valType(pThis->pAry);
	
	/* If my last index >= first internal, use getIn*/
	if(pBase->nIntRank == 0){
		const ubyte* ptr = DasAry_getAt(pThis->pAry, pBase->vt, pAryLoc);
		if(pBase->vsize > DATUM_BUF_SZ) return false;
		assert(pBase->vsize <= DATUM_BUF_SZ);
		memcpy(pDatum, ptr, pBase->vsize);
		pDatum->vt = vtAry;
		pDatum->vsize = das_vt_size(vtAry);
		pDatum->units  = pBase->units;
	}
	else if(pBase->nIntRank == 1){
		size_t uCount = 1;
		const ubyte* ptr = DasAry_getIn(pThis->pAry, vtUByte, nDim, pAryLoc, &uCount);
		if(ptr == NULL) return false;

		if(vtAry == vtUByte){   /* Make a datum */

			if(pBase->vt == vtText){
				pDatum->vt = vtText;
				pDatum->vsize = das_vt_size(vtText);
				pDatum->units = pBase->units;
				memcpy(pDatum, &ptr, sizeof(const ubyte*));
			}
			else{
				das_byteseq bs;
				pDatum->vt = vtByteSeq;
				pDatum->vsize = sizeof(das_byteseq);
				bs.ptr = ptr;
				bs.sz  = uCount;
				memcpy(pDatum, &bs, sizeof(das_byteseq));
			}
		}
		else{
			das_error(DASERR_VAR, 
				"Don't know how represent value type %s using a single datum. "
				"(Hint: did you mean to make a GeoVector ?)", das_vt_toStr(vtAry)
			);
			return false;
		}
	}
	else{
		das_error(DASERR_VAR, "Handling for internal types larger then rank 1 not implemented");
		return false;
	}
	return true;
}

bool DasVarAry_isFill(const DasVar* pBase, const ubyte* pCheck, das_val_type vt)
{
	const DasVarAry* pThis = (const DasVarAry*)pBase;
	const ubyte* pFill = DasAry_getFill(pThis->pAry);
	
	return (das_vt_cmpAny(pFill, pBase->vt, pCheck, vt) == 0);
}

int dec_DasVarAry(DasVar* pBase){
	pBase->nRef -= 1;
	if(pBase->nRef > 0) return pBase->nRef;
	
	DasVarAry* pThis = (DasVarAry*)pBase;
	dec_DasAry(pThis->pAry);
	free(pThis);
	return 0;
}


bool _DasVarAry_canStride(
	const DasVarAry* pThis, const ptrdiff_t* pMin, const ptrdiff_t* pMax
){
	/* You can't have more than one increment (of a ragged range)
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
	
	int nVarRank = pThis->base.nExtRank;
	
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

/* This is a handler for both DasVarAry and DasVarVecAry */
/* NOTES in: variable.md. */
DasAry* _DasVarAry_strideSubset(
	const DasVarAry* pThis, const ptrdiff_t* pMin, const ptrdiff_t* pMax,
	bool* pContinue
){ 
	*pContinue = true; /* We assume */
	/* If can't stride, this is pointless */
	if(!_DasVarAry_canStride(pThis, pMin, pMax))
		return NULL;
	
	int nVarRank = pThis->base.nExtRank;
	size_t elSz = pThis->base.vsize;
	das_val_type vtEl = pThis->base.vt;
	if(pThis->base.vt == vtGeoVec){
		vtEl = ((DasVarVecAry*)pThis)->tplt.et;
	}

	
	/* Allocate the output array and get a pointer to the memory */
	size_t aSliceShape[DASIDX_MAX] = DASIDX_INIT_BEGIN;
	int nSliceRank = das_rng2shape(nVarRank, pMin, pMax, aSliceShape);
	
	char sName[DAS_MAX_ID_BUFSZ] = {'\0'};
	snprintf(sName, DAS_MAX_ID_BUFSZ - 1, "%s_subset", DasAry_id(pThis->pAry));
	DasAry* pSlice = new_DasAry(
		sName, vtEl, pThis->base.vsize, DasAry_getFill(pThis->pAry),
		nSliceRank, aSliceShape, pThis->base.units
	);
	
	size_t uWriteBufLen = 0;
	ubyte* pWriteBuf = DasAry_getBuf(pSlice, vtEl, DIM0, &uWriteBufLen);
	
	/* Get the base starting point pointer */ 
	ptrdiff_t base_idx[DASIDX_MAX] = {0};
	int d = 0;
	int iLoc = 0;
	for(d = 0; d < nVarRank; ++d){
		iLoc = pThis->idxmap[d];
		if(iLoc == DASIDX_UNUSED) continue;
		base_idx[iLoc] = pMin[d];
	}
	size_t uRemain = 0;
	const ubyte* pBaseRead = DasAry_getIn(
		pThis->pAry, vtEl, DasAry_rank(pThis->pAry), base_idx, &uRemain
	);
	if(pBaseRead == NULL){
		*pContinue = false;
		return NULL;
	}
	
	/* make a variable stride from the array stride, note that the var_stride
	 * may be degenerate and have offset changes of 0 */
	ptrdiff_t ary_shape[DASIDX_MAX];
	ptrdiff_t ary_stride[DASIDX_MAX];
	ptrdiff_t var_stride[DASIDX_MAX] = {0};
	if(DasAry_stride(pThis->pAry, ary_shape, ary_stride) < 1){
		*pContinue = false;
		return NULL;
	}
	/* Multiply the strides by the element size, we're going to work in bytes */
	for(d = 0; d < DasAry_rank(pThis->pAry); ++d) ary_stride[d] *= elSz;
	
	for(d = 0; d < nVarRank; ++d){
		/* If only 1 value is chosen for this index there is no striding */
		if((pMax[d] - pMin[d]) == 1) continue;
		iLoc = pThis->idxmap[d];
		if(iLoc == DASIDX_UNUSED) continue;
				
		var_stride[d] = ary_stride[iLoc];
	}
	
	/* Sanity check, are the var strides > 0 */
#ifndef NDEBUG
	for(d = 0; d < nVarRank; ++d){
		assert(var_stride[d] >= 0);
	}
#endif
	
	/* Stride over the array copying values */
	ptrdiff_t idx_cur[DASIDX_MAX];
	memcpy(idx_cur, pMin, nVarRank * sizeof(ptrdiff_t));
	const ubyte* pRead = pBaseRead;
	ubyte* pWrite = pWriteBuf;
	
	/* Copy the data.  Unroll the loop up to dimension 4.  Unchecked there
	 * are *all* kinds of security concerns here:
	 *
	 * 1. We could write off the end of the buffer
	 * 2. We could read outside array memory. 
	 *
	 */
	switch(nVarRank){
	case 1:
		while(idx_cur[0] < pMax[0]){
			pRead = pBaseRead; 
			pRead += idx_cur[0]*var_stride[0];
			memcpy(pWrite, pRead, elSz);
			idx_cur[0] += 1;
			pWrite += elSz;
		}
		break;
	
	case 2:
		while(idx_cur[0] < pMax[0]){
			pRead = pBaseRead;
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
			pRead = pBaseRead;
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
			pRead = pBaseRead;
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
			pRead = pBaseRead;
			for(d = 0; d < nVarRank; ++d) pRead += idx_cur[d]*var_stride[d];
		
			memcpy(pWrite, pRead, elSz);
			
			/* Roll index */
			for(d = nVarRank - 1; d > -1; --d){
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
	
	return pSlice;
}

/* See if we can use the DasAry_SubSetIn function to make a subset without 
 * allocating memory or copying any data */
DasAry* _DasVarAry_directSubset(
	const DasVarAry* pThis, const ptrdiff_t* pMin, const ptrdiff_t* pMax, 
	bool* pContinue
){
	*pContinue = true;  /* We assume */
	
	/* Map the requested range to the array range */
	ptrdiff_t aAryMin[DASIDX_MAX];
	ptrdiff_t aAryMax[DASIDX_MAX];
	ptrdiff_t nSz;
	int iDim;
	for(iDim = 0; iDim < pThis->base.nExtRank; ++iDim){
		nSz = pMax[iDim] - pMin[iDim];
		if(pThis->idxmap[iDim] == DASIDX_UNUSED){
			if(nSz != 1) 
				return NULL;
		}
		else{
			aAryMin[ pThis->idxmap[iDim] ] = pMin[iDim];
			aAryMax[ pThis->idxmap[iDim] ] = pMax[iDim];
		}
	}
	
	ptrdiff_t aAryShape[DASIDX_MAX];
	int nAryRank = DasAry_shape(pThis->pAry, aAryShape);

	/* If we have internal indicies, use the full range for them */
	/* if( ((DasVar*)pThis)->nIntRank > 0){
		assert( ((DasVar*)pThis)->nIntRank == 1); / * No high internal ranks yet * /
		assert( aAryShape[nAryRank - 1] > 0);     / * No var-len internal yet * /

		aAryMin[ aAryShape[nAryRank - 1] ] = 0;
		aAryMax[ aAryShape[nAryRank - 1] ] = aAryShape[nAryRank - 1];
	}
	*/

	/* Look over the array range and make sure it points to a single subset */
	ptrdiff_t aLoc[DASIDX_MAX];
	int nLocSz = 0;
	int iBegFullRng = -1;
	for(iDim = 0; iDim < (nAryRank - ((DasVar*)pThis)->nIntRank); ++iDim){
			
		/* Sanity check */
		if((aAryMin[iDim] < 0)||(aAryMax[iDim] > aAryShape[iDim])){
			das_error(DASERR_VAR, "Invalid subset request");
			*pContinue = false;
			return NULL;
		}
			
		if((aAryMax[iDim] - aAryMin[iDim]) == 1){ 
			/* Going full range locks, can't go back to single items after */
			if(iBegFullRng != -1)
				return NULL;
				
			++nLocSz;
			aLoc[iDim] = aAryMin[iDim];
		}
		else{
			/* Has to be 1 or full range */
			if((aAryMin[iDim] == 0)&&(aAryMax[iDim] == aAryShape[iDim])){
				if(iBegFullRng == -1) iBegFullRng = iDim;
			}
			else{
				/* Fractional range zzziitt, gonna have to copy the data */
				return NULL;
			}
		}
	}
	
	/* Can just make a subset IF nLocSz less than nAryRank */
	if(nLocSz < nAryRank){
		DasAry* pSubSet = DasAry_subSetIn(pThis->pAry, NULL, nLocSz, aLoc);
		return pSubSet;
	}
	
	return NULL;
}

DasAry* _DasVarAry_slowSubset(
	const DasVarAry* pThis, const ptrdiff_t* pMin, const ptrdiff_t* pMax
){
	/* This is the easiest subset code to write but it is also the slowest */
	
	/* Allocate the output array and get a pointer to the memory */
	size_t aSliceShape[DASIDX_MAX] = DASIDX_INIT_BEGIN;
	int nVarRank = pThis->base.nExtRank;
	das_val_type vtEl = pThis->base.vt;
	size_t uSzEl = pThis->base.vsize;
	const ubyte* pFill = DasAry_getFill(pThis->pAry);
	
	int nSliceRank = das_rng2shape(nVarRank, pMin, pMax, aSliceShape);
	
	char sName[DAS_MAX_ID_BUFSZ] = {'\0'};
	snprintf(sName, DAS_MAX_ID_BUFSZ - 1, "%s_subset", DasAry_id(pThis->pAry));
	DasAry* pSlice = new_DasAry(
		sName, vtEl, uSzEl, pFill, nSliceRank, aSliceShape, pThis->base.units
	);
	
	size_t uBufSz = 0;
	ubyte* pBase = DasAry_getBuf(pSlice, vtEl, DIM0, &uBufSz);
	
	ptrdiff_t var_idx[DASIDX_MAX];
	memcpy(var_idx, pMin, nVarRank*sizeof(ptrdiff_t));
	ptrdiff_t read_idx[DASIDX_MAX];  /* Right pad for internal indexes */
	
	const ubyte* pValue = NULL;
	int d;
	ubyte* pWrite = pBase;
	while(var_idx[0] < pMax[0]){
		
		/* Get the real read and the real write locations */
		for(d = 0; d < nVarRank; ++d){
			if(pThis->idxmap[d] != DASIDX_UNUSED){
				read_idx[ pThis->idxmap[d] ] = var_idx[d];
			}
		}
		
		/* If this is an invalid location just use fill.  This is how we take
		 * slices of ragged arrays */
		if(!DasAry_validAt(pThis->pAry, read_idx))
			pValue = pFill;
		else
			pValue = DasAry_getAt(pThis->pAry, vtEl, read_idx);
		
		memcpy(pWrite, pValue, uSzEl);
			
		/* Roll index var index. */
		for(d = nVarRank - 1; d > -1; --d){
			var_idx[d] += 1;
			if((d > 0) && (var_idx[d] == pMax[d]))  
				var_idx[d] = pMin[d];
			else  
				break;  /* Stop rolling */
		}
		pWrite += uSzEl;
	}  
	
	return pSlice;
}

/* subset algorithm router */

DasAry* DasVarAry_subset(
	const DasVar* pBase, int nRank, const ptrdiff_t* pMin, const ptrdiff_t* pMax
){
	const DasVarAry* pThis = (DasVarAry*)pBase;
	
	if(nRank != pBase->nExtRank){
		das_error(
			DASERR_VAR, "External variable is rank %d, but subset specification "
			"is rank %d", pBase->nExtRank, nRank
		);
		return NULL;
	}
	
	size_t aSliceShape[DASIDX_MAX] = DASIDX_INIT_BEGIN;
	int nSliceRank = das_rng2shape(nRank, pMin, pMax, aSliceShape);
	if(nSliceRank < 0) return NULL;
	if(nSliceRank == 0){ 
		das_error(DASERR_VAR, "Can't output a rank 0 array, use DasVar_get() "
					"for single items");
		return NULL;
	}
	
	/* Try to get the subset in order from fastest method to slowest */
	
	bool bCont = true;
	
	DasAry* pRet = _DasVarAry_directSubset(pThis, pMin, pMax, &bCont);
	if(pRet != NULL) return pRet;
	if(!bCont) return NULL;
	
	pRet = _DasVarAry_strideSubset(pThis, pMin, pMax, &bCont);
	if(pRet != NULL) return pRet;
	if(!bCont) return NULL;
	
	return _DasVarAry_slowSubset(pThis, pMin, pMax);
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

// Combined expression printer for both regular & vector arrays
char* _DasVarAry_intrExpress(
	const DasVar* pBase, char* sBuf, int nLen, unsigned int uExFlags,
	const char* sFrame, ubyte dirs, ubyte nDirs
){

	ubyte pDirs[4] = {0};
	if(nDirs > 0) pDirs[0] = dirs & 0x3;
	if(nDirs > 1) pDirs[1] = (dirs >> 2) & 0x3;
	if(nDirs > 2) pDirs[2] = (dirs >> 4) & 0x3;

	if(nLen < 2) return sBuf;  /* No where to write and remain null terminated */
	memset(sBuf, 0, nLen);  /* Insure null termination whereever I stop writing */
	
	const DasVarAry* pThis = (const DasVarAry*)pBase; 

	int nWrite = strlen(pThis->pAry->sId);
	nWrite = nWrite > (nLen - 1) ? nLen - 1 : nWrite;
	char* pWrite = sBuf;
	strncpy(sBuf, pThis->pAry->sId, nWrite);
	
	pWrite = sBuf + nWrite;  nLen -= nWrite;
	if(nLen < 2) return pWrite;

	int nRank = 0;
	for(int i = 0; i < pBase->nExtRank; i++){
		if(pThis->idxmap[i] != DASIDX_UNUSED) ++nRank;
	}
	
	if(nLen < (nRank*3 + 1)) return pWrite;
	
	for(int i = 0; i < pBase->nExtRank; i++){
		if(pThis->idxmap[i] != DASIDX_UNUSED){ 
			*pWrite = '['; ++pWrite; --nLen;
			*pWrite = g_sIdxLower[i]; ++pWrite; --nLen;
			*pWrite = ']'; ++pWrite; --nLen;
		}
	}

	// i now holds the first internal dimension
	
	char* pSubWrite = pWrite;
	
	if((pBase->units != UNIT_DIMENSIONLESS) && (uExFlags & D2V_EXP_UNITS)){
		pSubWrite = _DasVar_prnUnits((DasVar*)pThis, pWrite, nLen);
		nLen -= (pSubWrite - pWrite);
		pWrite = pSubWrite;
	}
	
	if(uExFlags & D2V_EXP_RANGE){
		pSubWrite = _DasVar_prnRange(&(pThis->base), pWrite, nLen);
		nLen -= (pSubWrite - pWrite);
		pWrite = pSubWrite;
	}

	// Print internal object info if there is any
	if((uExFlags & D2V_EXP_INTR) && (das_vt_rank(pBase->vt) > 0)){
		pSubWrite = _DasVar_prnIntr(pBase, sFrame, pDirs, nDirs, pWrite, nLen);
		nLen -= (pSubWrite - pWrite);
		pWrite = pSubWrite;  
	}

	if(uExFlags & D2V_EXP_TYPE)
		return _DasVar_prnType((DasVar*)pThis, pWrite, nLen);
	else
		return pWrite;
}

char* DasVarAry_expression(
	const DasVar* pBase, char* sBuf, int nLen, unsigned int uFlags
){
	return _DasVarAry_intrExpress(pBase, sBuf, nLen, uFlags, NULL, 0, 0);
}

DasErrCode init_DasVarAry(
	DasVarAry* pThis, DasAry* pAry, int nExtRank, int8_t* pExtMap, int nIntRank
){
	if((nExtRank == 0)||(nExtRank > (DASIDX_MAX-1))){
		das_error(DASERR_VAR, "Invalid start of internal indices: %d", nExtRank);
		return false;
	}
	
	pThis->base.vartype    = D2V_ARRAY;
	pThis->base.nRef       = 1;
	pThis->base.copy       = copy_DasVarAry;
	pThis->base.decRef     = dec_DasVarAry;
	pThis->base.isNumeric  = DasVarAry_isNumeric;
	pThis->base.expression = DasVarAry_expression;
	pThis->base.incRef     = inc_DasVar;
	pThis->base.get        = DasVarAry_get;
	pThis->base.shape      = DasVarAry_shape;
	pThis->base.intrShape  = DasVarAry_intrShape;
	pThis->base.lengthIn   = DasVarAry_lengthIn;
	pThis->base.isFill     = DasVarAry_isFill;
	pThis->base.subset     = DasVarAry_subset;
	pThis->base.nExtRank   = nExtRank;
	pThis->base.nIntRank   = nIntRank;
	pThis->base.degenerate = DasVarAry_degenerate;
	pThis->base.elemType   = DasVarAry_elemType;
	
	/* Extra stuff for array variables */
	if(pAry == NULL)
		return das_error(DASERR_VAR, "Null array pointer\n");
	
	pThis->pAry = pAry;
	pThis->varsubtype = D2V_STDARY;
	
	/* Connection between variable units and array units broken here, this
	 * is intentional, but be aware of it! */
	pThis->base.units = pAry->units;
	
	int nValid = 0;
	char sBuf[128] = {'\0'};
	pThis->base.nExtRank = nExtRank;
	for(int i = 0; i < DASIDX_MAX; ++i)
		pThis->idxmap[i] = DASIDX_UNUSED;
	
	size_t u;
	for(u = 0; u < nExtRank; ++u){ 
		pThis->idxmap[u] = pExtMap[u];
		
		/* Make sure that the map has the same number of non empty indexes */
		/* as the rank of the array */
		if(pExtMap[u] >= 0){
			++nValid;
			if(pExtMap[u] >= pAry->nRank){
				return das_error(DASERR_VAR, 
					"Variable dimension %zu maps to non-existant dimension %zu in "
					"array %s", u, pExtMap[u], DasAry_toStr(pAry, sBuf, 127)
				);
			}
		}
	}

	/* Now make sure that we have enough extra array indicies for the internal
		structure */
	if((nValid + nIntRank) != DasAry_rank(pAry))
		return das_error(DASERR_VAR,
			"Backing array is rank %d. Expected %d external plus "
			"%d internal indicies.", DasAry_rank(pAry), nExtRank, nIntRank
		);

	/* Here's the score. We're putting a template on top of simple das arrays
	 * that allows composite datums such as strings and GeoVec to be stored with
	 * dense packing.  
	 * 
	 * vtUByte w/string -> vtText and needs one internal index
	 * vtGeoVec needs one internal index and it's equal to the number of components
	 *          It also needs the value type set to the index vector type
	 * vtByteSeq needs one internal index, and it's ragged.
	 * vtPixel Has a number of channels (RGBA) and a size of each channel (8,16 bits)
	 */
	das_val_type vtAry = DasAry_valType(pAry);

	if(nIntRank > 1)
		return das_error(DASERR_VAR, 
			"Internal rank = %d, ranks > 1 are not yet supported", nIntRank
		);

	/* Make sure that the last index < the first internal for scalar types,
		and that last index == first internal for rank 1 types */
	if((vtAry == vtUByte)||(vtAry == vtByte)){
		if((pAry->uFlags & D2ARY_AS_STRING) == D2ARY_AS_STRING){
			if(nIntRank != 1)
				return das_error(DASERR_VAR, "Dense text needs an internal rank of 1");
			pThis->base.vt = vtText;
		}
		else{
			if(nIntRank > 0)
				pThis->base.vt = vtByteSeq;
			else
				pThis->base.vt = vtUByte;
		}
	}
	else {
		if((vtAry < VT_MIN_SIMPLE)||(vtAry > VT_MAX_SIMPLE))
			return das_error(DASERR_VAR, 
				"Only simple types understood by DasVarAry, not vt = %d", vtAry
			);
		pThis->base.vt = vtAry;
	}
	
	pThis->base.vsize = das_vt_size(pThis->base.vt);
	if(Units_haveCalRep(pThis->base.units))
		strncpy(pThis->base.semantic, DAS_SEM_DATE, D2V_MAX_SEM_LEN-1); 
	else
		strncpy(pThis->base.semantic, das_sem_default(pThis->base.vt), D2V_MAX_SEM_LEN-1); 

	inc_DasAry(pAry);    /* Increment the reference count for this array */
	return DAS_OKAY;
}

DasVar* new_DasVarAry(DasAry* pAry, int nExtRank, int8_t* pExtMap, int nIntIdx)
{
	/* DasVarAry does not point outside of it's stack */
	DasVarAry* pThis = (DasVarAry*)calloc(1, sizeof(DasVarAry));
	DasDesc_init((DasDesc*)pThis, VARIABLE);

	if(init_DasVarAry(pThis, pAry, nExtRank, pExtMap, nIntIdx) != DAS_OKAY){
		/* Don't decrement the array ownership on failure because it wasn't
			incremented, free */
		free(pThis);
		return NULL;
	}
	return (DasVar*)pThis;
}

/* ************************************************************************* */
/* A specific array var, internal structure is a geometric vector            */


/*                                  ^    */
/*                                  |    */
/* See structure definition above --+    */

DasVar* copy_DasVarVecAry(const DasVar* pAncestor)
{
	const DasVarAry* pBase = (DasVarAry*)pAncestor;

	assert(pAncestor->vartype == D2V_ARRAY); /* Okay to not be present in release code */
	assert(pBase->varsubtype == D2V_GEOVEC);

	DasVarVecAry* pRet = calloc(1, sizeof(DasVarVecAry)); /* Make something my size */
	_DasVarAry_copyTo(pBase, (DasVarAry*)pRet);           /* Fill it with base class stuff */

	DasVarVecAry* pThis = (DasVarVecAry*)pBase;           /* Now add my stuff */
	pRet->tplt = pThis->tplt;
	
	return (DasVar*)pRet;
}

int DasVarAry_getFrame(const DasVar* pBase)
{
	if(pBase->vartype != D2V_ARRAY) 
		return 0;

	if( ((const DasVarAry*)pBase)->varsubtype != D2V_GEOVEC)
		return 0;

	return ((const DasVarVecAry*)pBase)->tplt.frame;
}


bool DasVarAry_setFrame(DasVar* pBase, ubyte nFrameId)
{
	if(pBase->vartype != D2V_ARRAY) 
		return false;

	if( ((const DasVarAry*)pBase)->varsubtype != D2V_GEOVEC)
		return false;	

	DasVarVecAry* pThis = ((DasVarVecAry*)pBase);

	/* If 0, this template is a frame-less vector */
	pThis->tplt.frame = nFrameId;

	return true;
}

const char* DasVarAry_getFrameName(const DasVar* pBase)
{
	if(pBase->vartype != D2V_ARRAY) 
		return NULL;

	if( ((const DasVarAry*)pBase)->varsubtype != D2V_GEOVEC)
		return NULL;

	const DasVarVecAry* pThis = (const DasVarVecAry*)pBase;
	if(pThis->tplt.frame == 0) return NULL;

	DasStream* pStream = _DasVar_getStream(pBase);
	if(pStream == NULL) return NULL;

	const DasFrame* pFrame = DasStream_getFrameById(pStream, pThis->tplt.frame);
	if(pFrame == NULL) return NULL;

	return DasFrame_getName(pFrame);
}

ubyte DasVarAry_vecMap(const DasVar* pBase, ubyte* nDirs, ubyte* pDirs)
{
	if(pBase->vartype != D2V_ARRAY) 
		return 0;

	if( ((const DasVarAry*)pBase)->varsubtype != D2V_GEOVEC)
		return 0;

	*nDirs = 0;

	das_geovec gv = ((const DasVarVecAry*)pBase)->tplt;
	if(pDirs != NULL){
		if(gv.ncomp > 0)
			pDirs[0] = gv.dirs & 0x3;
		if(gv.ncomp > 1)
			pDirs[1] = (gv.dirs >> 2)&0x3;
		if(gv.ncomp > 2)
			pDirs[2] = (gv.dirs >> 4)&0x3;
	}

	*nDirs = gv.ncomp;

	return gv.systype;
}

char* DasVarVecAry_expression(
	const DasVar* pBase, char* sBuf, int nLen, unsigned int uFlags
){
	DasVarVecAry* pThis = (DasVarVecAry*)pBase;

	const char* sFrame = "unknown";
	DasStream* pStream = _DasVar_getStream(pBase);
	if(pStream != NULL){
		const DasFrame* pFrame = DasStream_getFrameById(pStream, pThis->tplt.frame);
		if(pFrame != NULL){
			sFrame = DasFrame_getName(pFrame);
		}
	}

	return _DasVarAry_intrExpress(
		pBase, sBuf, nLen, uFlags, sFrame, pThis->tplt.dirs, pThis->tplt.ncomp
	);
}

bool DasVarVecAry_get(const DasVar* pAncestor, ptrdiff_t* pLoc, das_datum* pDm)
{
	const DasVarAry*  pBase = (const DasVarAry*)pAncestor;
	const DasVarVecAry* pThis = (const DasVarVecAry*)pAncestor;
	
	/* Ignore indices you don't understand, that's what makes this work */
	/* I need a way to make this code fast, maybe the loop should be unrolled? */
	ptrdiff_t pAryLoc[DASIDX_MAX] = DASIDX_INIT_BEGIN;
	
	int nDim = 0;
	for(int i = 0; i < pAncestor->nExtRank; ++i){
		if(pBase->idxmap[i] >= 0){ /* all the wierd flags are less than 0 */
			pAryLoc[ pBase->idxmap[i] ] = pLoc[i];
			++nDim;
		}
	}

	if(pAncestor->nIntRank != 1){
		das_error(DASERR_VAR, "Logic error in vector access");
		return false;
	}
	
	size_t uCount = 1;
	const ubyte* ptr = DasAry_getIn(
		pBase->pAry, pThis->tplt.et, nDim, pAryLoc, &uCount
	);

	memcpy(pDm, &(pThis->tplt), sizeof(das_geovec));
	das_geovec* pVec = (das_geovec*)pDm;

	memcpy(pDm, ptr, pVec->esize * pVec->ncomp);
	pDm->units = pAncestor->units;
	pDm->vsize = sizeof(das_geovec);
	pDm->vt    = vtGeoVec;

	return true;
}

DasVar* new_DasVarVecAry(
	DasAry* pAry, int nExtRank, int8_t* pExtMap, int nIntRank,
	ubyte nFrameId, ubyte uSysType, ubyte nComp, ubyte dirs
){
	
	// Handle the base class
	DasVarVecAry* pThis = (DasVarVecAry*) calloc(1, sizeof(DasVarVecAry));
	DasDesc_init((DasDesc*)pThis, VARIABLE);

	DasVarAry* pBase = (DasVarAry*)pThis;
	DasVar* pAncestor = (DasVar*)pThis;

	if(init_DasVarAry(pBase, pAry, nExtRank, pExtMap, nIntRank) != DAS_OKAY){
		/* Don't decrement the array ownership on failure because it wasn't
			incremented, free */
		free(pThis);
		return NULL;
	}

	/* Add in our changes */
	pAncestor->copy        = copy_DasVarVecAry;
	pAncestor->get         = DasVarVecAry_get;
	pAncestor->expression  = DasVarVecAry_expression;
	
	ubyte nodata[24] = {0};

	DasErrCode nRet =  das_geovec_init(&(pThis->tplt), nodata, 
		nFrameId, 0, uSysType, pAncestor->vt, das_vt_size(pAncestor->vt), 
		nComp, dirs
	);

	/* Now switch our value type to geovec */
	pAncestor->vt = vtGeoVec;
	pBase->varsubtype = D2V_GEOVEC;

	if(nRet != DAS_OKAY){
		free(pThis);
		return NULL;
	}

	return (DasVar*) pThis;
}

/* Combined encoder for both vectors and non-vectors */
DasErrCode DasVarAry_encode(DasVar* pBase, const char* sRole, DasBuf* pBuf)
{
	/* If this were a public function we'd need to check the pointers here */
	const DasDim* pDim = (const DasDim*) ((DasDesc*)pBase)->parent;
	const DasDs* pDs = (const DasDs*) ((DasDesc*)pDim)->parent;
	
	DasVarAry* pThis = (DasVarAry*)pBase;

	/* 1. Figure out my shape in index space */

	ptrdiff_t aExtShape[DASIDX_MAX] = DASIDX_INIT_UNUSED;
	DasVarAry_shape(pBase, aExtShape);
	int nItems = 1;
	char sIndex[128] = {'\0'};
	char* pWrite = sIndex;
	for(int i = 0; i < pBase->nExtRank; ++i){
		if(pWrite - sIndex > 117)
			continue;
		if(i > 0){ *pWrite = ';'; ++pWrite;}
		if(aExtShape[i] == DASIDX_UNUSED){ 
			*pWrite = '-'; ++pWrite;
		}
		else{ 
			if(i == 0){ *pWrite = '*'; ++pWrite;}
			else{ 
				pWrite += snprintf(pWrite, 11, "%td", aExtShape[i]); 
				nItems *= aExtShape[i];  /* <-- items per packet */
			}
		}
	}

	/* 2. Get a Codec, either the one predefined for packet values, or make a new
	      one for header values */

	DasAry* pAry = DasVar_getArray(pBase);
	int nItemsPerWrite = 0;
	const DasCodec* pCodec = DasDs_getCodecFor(pDs, DasAry_id(pAry), &nItemsPerWrite);
	das_units units = pThis->base.units;

	/* No encoder, must be header values, so make one here */
	DasCodec codecHdr;
	if(pCodec == NULL){
		/* Make sure we're not a function of index 0 since we don't have a
		   fixed codec */
		if(aExtShape[0] != DASIDX_UNUSED){
			return das_error(DASERR_VAR, "No codec provided for %s/%s/%s/%s packet data!",
				DasDs_id(pDs), DasDim_typeName(pDim), DasDim_id(pDim), sRole
			);
		}

		DasCodec_init(
			DASENC_WRITE, &codecHdr, pAry, pBase->semantic, "utf8", DASIDX_RAGGED, 0, 
			units, NULL
		);
		pCodec = &codecHdr;
	}

	das_val_type vtAry = DasAry_valType(pAry);
	das_val_type vtExt = pCodec->vtBuf;


	/* 3. Define the variable in the output header, if vector add components */

	const char* sStorage = vtAry == vtTime ? "struct" : das_vt_toStr(vtAry);
	const char* sType = (pThis->varsubtype == D2V_GEOVEC) ? "vector" : "scalar";

	char aComponents[32] = {'\0'};
	DasVarVecAry* pDerived = (DasVarVecAry*)pThis;
	das_geovec gvec = pDerived->tplt;
	if(pThis->varsubtype == D2V_GEOVEC){
		snprintf(aComponents, 32, "components=\"%hhu\" ", gvec.ncomp);
	}
	
	DasBuf_printf(pBuf, 
		"    <%s %suse=\"%s\" semantic=\"%s\" storage=\"%s\" index=\"%s\" units=\"%s\"",
		sType, aComponents, sRole, pBase->semantic, sStorage, sIndex, units
	);

	if(pThis->varsubtype == D2V_GEOVEC){

		DasBuf_printf(pBuf, " system=\"%s\" ", das_compsys_str(gvec.systype));
		if(das_geovec_hasRefSurf(&gvec)){
			DasBuf_printf(pBuf, " surface=\"%hhu\"", das_geovec_surfId(&gvec));
		}

		DasBuf_puts(pBuf, "sysorder=\"");
		for(int i = 0; i < gvec.ncomp; ++i){
			if(i> 0)DasBuf_puts(pBuf, ";");
			DasBuf_printf(pBuf, "%d", das_geovec_dir(&gvec, i));
		}
		DasBuf_puts(pBuf, "\">\n");

		nItems *= gvec.ncomp; /* More items per packet if we have vectors */

		/* Make sure we have a summary of this vector system */
		/*
		if(DasDesc_getLocal((DasDesc*)pBase, "notes") == NULL){
			const char* sTmp = das_compsys_desc(gvec.systype);
			if(sTmp != NULL)
				DasDesc_setStr((DasDesc*)pBase, "notes", sTmp);
		}
		*/
	}
	else{
		DasBuf_puts(pBuf, ">\n");
	}

	/* 4. Write any properties, make sure a generic summary is included */
	if(DasDesc_length((DasDesc*)pBase) > 0){
		int nRet = DasDesc_encode3((DasDesc*)pBase, pBuf, "      ");
		if(nRet != DAS_OKAY) return nRet;
	}

	/* 5. Write values, or how to read values */

	if(aExtShape[0] == DASIDX_UNUSED){
		DasBuf_puts(pBuf, "      <values>\n");
		int nWrite = (int)DasAry_size(pAry);
		int nVals = DasCodec_encode(&codecHdr, pBuf, DIM0, nWrite, DASENC_IN_HDR|DASENC_PKT_LAST);
		if(nVals < 0){
			return das_error(DASERR_VAR, "Error encoding data for %s/%s/%s/%s",
				DasDs_id(pDs), DasDim_typeName(pDim), DasDim_id(pDim), sRole
			);
		}
		DasBuf_puts(pBuf, "      </values>\n");
		DasCodec_deInit(&codecHdr);
	}
	else{
		char sFill[64] = {'\0'};
		das_datum dmFill;
		das_datum_init(&dmFill, DasAry_getFill(pAry), vtAry, das_vt_size(vtAry), units);
		das_datum_toStrValOnly(&dmFill, sFill, 63, 6);
			
   	DasBuf_printf(pBuf, 
			"      <packet numItems=\"%td\" itemBytes=\"%d\" encoding=\"%s\" fill=\"%s\" />\n",
			nItems, pCodec->nBufValSz, das_vt_serial_type(vtExt), sFill
		);
	}

	DasBuf_printf(pBuf, "    </%s>\n", sType);

	return DAS_OKAY;
}

/* ************************************************************************* */
/* Helper utility for component labels, these are really important for some
 * CDF readers, so always try to make it happen
 */

int das_makeCompLabels(const DasVar* pVar, char** psBuf, size_t uLenEa)
{
	/* If you have a label property, use it.  
	   If this is a scaler, try the dim's 'label'
	      if that doesn't work just use the physdim
	   If this is a vector try the dimensions 'compLabel' property
	      if that doesn't work get the phys dim and append the connonical
	      direction symbols.
	*/
	const DasDesc* pDesc = (DasDesc*)pVar;
	const DasDesc* pDim  = DasDesc_parent(pDesc);
	const DasProp* pProp = DasDesc_getLocal(pDesc, "label");

	if(uLenEa < 2)
		return -1 * das_error(DASERR_VAR, "uLenEa too small in das_makeCompLabels");

	if((DasVar_type(pVar) == D2V_ARRAY)&& (((const DasVarAry*)pVar)->varsubtype == D2V_GEOVEC)){

		das_geovec tplt = ((const DasVarVecAry*)pVar)->tplt;

		int nComp = tplt.ncomp;

		/* Vector version here */
		if(pProp == NULL)
			pProp = DasDesc_getLocal(pDim, "compLabel");
		
		if(pProp != NULL){
			int nItems = DasProp_extractItems(pProp, psBuf, 3, uLenEa);
			if(nItems == nComp)
				return nComp;
			else
				daslog_warn_v("Expected %d values in the component label %s, found %d instead",
					nComp, DasProp_value(pProp), nItems
				);
		}

		/* Handle this differently from coordinates versus data.  For data components 
		   you usually care about the dimension your measuring and then the direction
		   symbol.  For coordinates, you usually care about your frame of reference
		   and then the symbol.  Swap between Frame and Measurment here */ 
		
		if(DasDim_type((DasDim*)pDim) == DASDIM_DATA){
			const char* sDim = DasDim_dim((DasDim*)pDim);
			for(int i = 0; i < nComp; ++i){
				const char* sSym = das_geovec_compSym(&tplt, i);
				snprintf(psBuf[i], uLenEa - 1, "%s_%s", sDim, sSym);
			}
		}
		else{
			const char* sFrame = DasDim_getFrame((DasDim*)pDim);
			for(int i = 0; i < nComp; ++i){
				const char* sSym = das_geovec_compSym(&tplt, i);
				if(sFrame)
					snprintf(psBuf[i], uLenEa - 1, "%s_%s", sSym, sFrame);
				else
					strncpy(psBuf[i], sSym, uLenEa - 1);
			}
		}
		return nComp;
	}

	/* scalar version here */
	if(pProp == NULL)
		pProp = DasDesc_getLocal(pDim, "label");
	
	if(pProp != NULL)
		strncpy(psBuf[0], DasProp_value(pProp), uLenEa-1);
	else
		strncpy(psBuf[0], DasDim_dim((DasDim*)pDim), uLenEa-1);  /* Re-use the dim name */

	return 1;
}