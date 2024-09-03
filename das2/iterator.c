/* Copyright (C) 2024 Chris Piker <chris-piker@uiowa.edu>
 *
 * This file is part of das2C, the core das C Library.
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

#include "iterator.h"

/* ************************************************************************* */
/* Array iteration support */

void DasAryIter_init(
   DasAryIter* pThis, const DasAry* pAry, int iDimMin, int iDimMax, 
   ptrdiff_t* pLocBeg,  ptrdiff_t* pLocEnd
)
{
	memset(pThis, 0, sizeof(DasAryIter));

	pThis->pAry = pAry;
	pThis->rank = DasAry_shape(pThis->pAry, pThis->shape);
	if(pThis->shape[0] == 0){  /* Can't iterate an empty array */
		pThis->done = true;
		return;
	}

	pThis->ragged = false;
	for(int i = 1; i < pThis->rank; ++i){
		if(pThis->shape[i] == DASIDX_RAGGED){
			pThis->ragged = true;
			break;
		}
	}

	pThis->dim_min = iDimMin;
	if(pThis->dim_min >= pThis->rank){
		pThis->done = true;
		return;
	}
	pThis->dim_max = iDimMax;
	if(pThis->dim_max < 0) pThis->dim_max = pThis->rank - pThis->dim_max;
	if((pThis->dim_max < 0)||(pThis->dim_max < pThis->dim_min)){
		pThis->done = true;
		return;
	}

	/* .index is treated as the "start" value.  We continually increment
	   the start value until it is >= the end value */

	/* Start off at all zeros, or at the specified location */
	if(pLocBeg != NULL)
		memcpy(pThis->index, pLocBeg, (pThis->dim_max+1)*sizeof(ptrdiff_t));

	/* End at all zeros, or at the end of the array. end is an exclusive upper index */
	if(pLocEnd != NULL)
		memcpy(pThis->index, pLocEnd, (pThis->dim_max+1)*sizeof(ptrdiff_t));
	else
		pThis->bNaturalEnd = true;

	/* If I'm ragged, get the length in the last dimension I can iterate */
	if(pThis->ragged){
		pThis->nLenLast = DasAry_lengthIn(pAry, pThis->dim_max, pThis->index);
	}
}

/* Helper, is index before end? Depends on normalized indexes */
/* The way to visualize this is text highlight select with rows and columns
  
  Consider the text, where ^ points out first and last values

    0123456789012345678901234567890123456789012
  0 asnt asnteoh tanehu asuh ansteohu asnehuhu
                      ^
  1 asnotuna otah tohuh  
                 ^
  Beg = 0,16
  End = 1,13

  if I > 1, I'm done.
  if I == 1 && J >= 13 I'm done. 

*/
bool _DasAryIter_beforeEnd(DasAryIter* pThis)
{
	for(int iDim = pThis->dim_min; iDim <= pThis->dim_max; ++iDim){
		if(pThis->index[iDim] > pThis->end_idx[iDim]){
			pThis->done = true;
			return false;
		}
		if(pThis->index[iDim] < pThis->end_idx[iDim])
			return true;
		
		/* not conclusive, check next lower (left most) index */
	}

	pThis->done = true;
	return false;
}

bool DasAryIter_next(DasAryIter* pThis)
{
	if(pThis->done) return false;

	if(! pThis->ragged){

		/* Quicker function for CUBIC arrays */
		for(int iDim = pThis->dim_max; iDim >= pThis->dim_min; --iDim){
			if(pThis->index[iDim] < (pThis->shape[iDim] - 1)){
				pThis->index[iDim] += 1;
				return (pThis->bNaturalEnd ? true : _DasAryIter_beforeEnd(pThis));
			}
			else{
				pThis->index[iDim] = 0; /* and go again, incrementing next index up */
			}
		}

		pThis->done = true;  /* must have run out if dimensions */
		return false;
	}

	/* Ahh, ragged dense arrays. Guess someone selected hard mode */
	ptrdiff_t nLenInIdx = 0;
	for(int iDim = pThis->dim_max; iDim >= pThis->dim_min; --iDim){
		if(iDim == pThis->dim_min)
			nLenInIdx = pThis->nLenLast;
		else
			nLenInIdx = DasAry_lengthIn(pThis->pAry, iDim, pThis->index);

		if(pThis->index[iDim] < (nLenInIdx - 1)){
			pThis->index[iDim] += 1;
				
			/* Look ahead.  If bumping an index that's not the last, save off the
			   length of the last run that we care about */
			if(iDim < (pThis->dim_max - 1))
				pThis->nLenLast = DasAry_lengthIn(pThis->pAry, pThis->dim_max, pThis->index);
				
			return (pThis->bNaturalEnd ? true : _DasAryIter_beforeEnd(pThis));
		}
		else{
			pThis->index[iDim] = 0;
		}
	}
		
	pThis->done = true;
	return false;

}

/* ************************************************************************* */
/* Dataset iteration support */

void DasDsIter_init(DasDsIter* pThis, const DasDs* pDs){
	
	memset(pThis, 0, sizeof(DasDsIter));
	
	pThis->rank = DasDs_shape(pDs, pThis->shape);
	pThis->pDs = pDs;

	/* If this is an empty dataset, we're already done */
	if(pThis->shape[0] == 0){
		pThis->done = true;
		return;
	}
	
	pThis->ragged = false; 
	for(int i = 1; i < pThis->rank; ++i){      /* Ignore ragged on first index */
		if(pThis->shape[i] == DASIDX_RAGGED){
			pThis->ragged = true;
			break;
		}
	}
	
	/* Start off index at all zeros, which memset insures above */
	
	/* If I'm ragged I'm going to need the size of the last index at the 
	 * lowest point of all previous indexes, get that. */
	if(pThis->ragged){
		pThis->nLenIn = DasDs_lengthIn(pDs, pThis->rank - 1, pThis->index);
		if(pThis->nLenIn < 0) pThis->done = true;
	}
}

bool DasDsIter_next(DasDsIter* pThis){

	if(pThis->done) return false;
	
	if(! pThis->ragged){
		/* Quicker function for CUBIC datasets */
		for(int iDim = pThis->rank - 1; iDim >= 0; --iDim){
			if(pThis->index[iDim] < (pThis->shape[iDim] - 1)){
				pThis->index[iDim] += 1;
				return true;
			}
			else{
				pThis->index[iDim] = 0;
			}
		}
	
		pThis->done = true;
		return false;	
	}
		
	/* I'm ragged so I can't use the generic shape function, but I can
	 * at least save off the length of the last index at this point
	 * and only change it when a roll occurs */
	ptrdiff_t nLenInIdx = 0;
	for(int iDim = pThis->rank - 1; iDim >= 0; --iDim){
			
		if(iDim == (pThis->rank - 1))
			nLenInIdx = pThis->nLenIn;
		else
			nLenInIdx = DasDs_lengthIn(pThis->pDs, iDim, pThis->index);
				
		if(pThis->index[iDim] < (nLenInIdx - 1)){
			pThis->index[iDim] += 1;
				
			/* If bumping an index that's not the last, recompute the length
			 * of the last run */
			if(iDim < (pThis->rank - 1))
				pThis->nLenIn = DasDs_lengthIn(pThis->pDs, pThis->rank - 1, pThis->index);
				
			return true;
		}
		else{
			pThis->index[iDim] = 0;
		}
	}
		
	pThis->done = true;
	return false;
}

/* ************************************************************************* */

void DasDsUniqIter_init(
	DasDsUniqIter* pThis, const DasDs* pDs, const DasVar* pVar
){
	memset(pThis, 0, sizeof(DasDsUniqIter));
	
	pThis->rank = DasDs_shape(pDs, pThis->shape);
	pThis->pDs = pDs;

	/* If this is an empty dataset, we're already done */
	if(pThis->shape[0] == 0){
		pThis->done = true;
		return;
	}

	ptrdiff_t aVarShape[DASIDX_MAX] = DASIDX_INIT_UNUSED;
	DasVar_shape(pVar, aVarShape);
	
	/* Lock the indexes that are ignored by this variable to 0, and determine
	 * if I'm ragged in a used index */	
	pThis->ragged = false;
	pThis->first = -1;
	pThis->last  = -1;
	for(int i = 0; i < pThis->rank; ++i){
		if(aVarShape[i] == DASIDX_UNUSED){
			pThis->lock[i] = true;
		}
		else{
			pThis->last = i;
			if(pThis->first == -1) pThis->first = i;
		}

		if((!pThis->lock[i])&&(i > 0)&&(pThis->shape[i] == DASIDX_RAGGED))
			pThis->ragged = true;
	}

	/* In the odd case of a constant, just set done right now */
	if((pThis->first == -1)||(pThis->first == -1))
		pThis->done = true;

	/* Start off index at all zeros, which memset insures above */
	
	/* If I'm ragged I'm going to need the size of the last used index
	 * at the lowest point of all previous indexes, get that. */

	if(pThis->ragged){
		pThis->nLenIn = DasDs_lengthIn(pDs, pThis->last, pThis->index);
		if(pThis->nLenIn < 0) pThis->done = true;
	}
}

bool DasDsUniqIter_next(DasDsUniqIter* pThis){

	if(pThis->done) return false;
	
	if(! pThis->ragged){
		/* Quicker function for CUBIC datasets, as long as you're 
		   at the last index of an array dimension, keep setting
		   zero and rolling previous */

		for(int iDim = pThis->last; iDim >= pThis->first; --iDim){
			if(pThis->lock[iDim]) continue;

			if(pThis->index[iDim] < (pThis->shape[iDim] - 1)){
				pThis->index[iDim] += 1;
				return true;
			}
			else{
				pThis->index[iDim] = 0;
			}
		}
	
		pThis->done = true;
		return false;	
	}
		
	/* I'm ragged so I can't use the generic shape function, but I can
	 * at least save off the length of the last index at this point
	 * and only change it when a roll occurs */
	ptrdiff_t nLenInIdx = 0;
	for(int iDim = pThis->last; iDim >= pThis->first; --iDim){
			
		if(iDim == pThis->last)
			nLenInIdx = pThis->nLenIn;
		else
			nLenInIdx = DasDs_lengthIn(pThis->pDs, iDim, pThis->index);
				
		if(pThis->index[iDim] < (nLenInIdx - 1)){
			pThis->index[iDim] += 1;
				
			/* If bumping an index that's not the last, recompute the length
			 * of the last run that I care about */
			if(iDim < pThis->last)
				pThis->nLenIn = DasDs_lengthIn(pThis->pDs, pThis->last, pThis->index);
				
			return true;
		}
		else{
			pThis->index[iDim] = 0;
		}
	}
		
	pThis->done = true;
	return false;
}

/* ************************************************************************* */

void DasDsCubeIter_init(
	DasDsCubeIter* pThis, int nRank, ptrdiff_t* pMin, ptrdiff_t* pMax 
){
	pThis->done = true;

	if((nRank < 1)||(nRank > DASIDX_MAX))
		das_error(DASERR_ITER, "Invalid array rank %d", nRank);

	memcpy(pThis->idxmin, pMin, sizeof(ptrdiff_t)*nRank);
	memcpy(pThis->index, pMin, sizeof(ptrdiff_t)*nRank);
	memcpy(pThis->idxmax, pMax, sizeof(ptrdiff_t)*nRank);

	pThis->rank = nRank;

	/* Check to see if any max values are greater then the corresponding min */
	for(int i = 0; i < nRank; ++i){
		if(pMax[i] > pMin[i]){
			pThis->done = false;
			return;
		}
	}
}

bool DasDsCubeIter_next(DasDsCubeIter* pThis)
{
	if(pThis->done) return false;

	for(int i = (pThis->rank - 1); i > -1; --i){

		/* Increment this index, if you can */
		if((pThis->index[i] + 1) < pThis->idxmax[i]){
			pThis->index[i] += 1;
			return true;
		}
		else{
			/* Need to bump next lower index, if no lower index, we're done */
			if(i != 0)
				pThis->index[i] = pThis->idxmin[i];
		}
	}
	pThis->done = true;  /* no next loop, no need for a break */
	return false;
}
