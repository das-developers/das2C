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
/* The iteration support */

void das_iter_init(das_iter* pIter, const DasDs* pDs){
	
	memset(pIter, 0, sizeof(das_iter));
	
	pIter->rank = DasDs_shape(pDs, pIter->shape);
	pIter->pDs = pDs;

	/* If this is an empty dataset, we're already done */
	if(pIter->shape[0] == 0){
		pIter->done = true;
		return;
	}
	
	pIter->ragged = false; 
	for(int i = 1; i < pIter->rank; ++i){      /* Ignore ragged on first index */
		if(pIter->shape[i] == DASIDX_RAGGED){
			pIter->ragged = true;
			break;
		}
	}
	
	/* Start off index at all zeros, which memset insures above */
	
	/* If I'm ragged I'm going to need the size of the last index at the 
	 * lowest point of all previous indexes, get that. */
	if(pIter->ragged){
		pIter->nLenIn = DasDs_lengthIn(pDs, pIter->rank - 1, pIter->index);
		if(pIter->nLenIn < 0) pIter->done = true;
	}
}

bool das_iter_next(das_iter* pIter){

	if(pIter->done) return false;
	
	if(! pIter->ragged){
		/* Quicker function for CUBIC datasets */
		for(int iDim = pIter->rank - 1; iDim >= 0; --iDim){
			if(pIter->index[iDim] < (pIter->shape[iDim] - 1)){
				pIter->index[iDim] += 1;
				return true;
			}
			else{
				pIter->index[iDim] = 0;
			}
		}
	
		pIter->done = true;
		return false;	
	}
		
	/* I'm ragged so I can't use the generic shape function, but I can
	 * at least save off the length of the last index at this point
	 * and only change it when a roll occurs */
	ptrdiff_t nLenInIdx = 0;
	for(int iDim = pIter->rank - 1; iDim >= 0; --iDim){
			
		if(iDim == (pIter->rank - 1))
			nLenInIdx = pIter->nLenIn;
		else
			nLenInIdx = DasDs_lengthIn(pIter->pDs, iDim, pIter->index);
				
		if(pIter->index[iDim] < (nLenInIdx - 1)){
			pIter->index[iDim] += 1;
				
			/* If bumping an index that's not the last, recompute the length
			 * of the last run */
			if(iDim < (pIter->rank - 1))
				pIter->nLenIn = DasDs_lengthIn(pIter->pDs, pIter->rank - 1, pIter->index);
				
			return true;
		}
		else{
			pIter->index[iDim] = 0;
		}
	}
		
	pIter->done = true;
	return false;
}

/* ************************************************************************* */

void das_uniq_iter_init(
	das_uniq_iter* pIter, const DasDs* pDs, const DasVar* pVar
){
	memset(pIter, 0, sizeof(das_uniq_iter));
	
	pIter->rank = DasDs_shape(pDs, pIter->shape);
	pIter->pDs = pDs;

	/* If this is an empty dataset, we're already done */
	if(pIter->shape[0] == 0){
		pIter->done = true;
		return;
	}

	ptrdiff_t aVarShape[DASIDX_MAX] = DASIDX_INIT_UNUSED;
	DasVar_shape(pVar, aVarShape);
	
	/* Lock the indexes that are ignored by this variable to 0, and determine
	 * if I'm ragged in a used index */	
	pIter->ragged = false;
	pIter->first = -1;
	pIter->last  = -1;
	for(int i = 0; i < pIter->rank; ++i){
		if(aVarShape[i] == DASIDX_UNUSED){
			pIter->lock[i] = true;
		}
		else{
			pIter->last = i;
			if(pIter->first == -1) pIter->first = i;
		}

		if((!pIter->lock[i])&&(i > 0)&&(pIter->shape[i] == DASIDX_RAGGED))
			pIter->ragged = true;
	}

	/* In the odd case of a constant, just set done right now */
	if((pIter->first == -1)||(pIter->first == -1))
		pIter->done = true;

	/* Start off index at all zeros, which memset insures above */
	
	/* If I'm ragged I'm going to need the size of the last used index
	 * at the lowest point of all previous indexes, get that. */

	if(pIter->ragged){
		pIter->nLenIn = DasDs_lengthIn(pDs, pIter->last, pIter->index);
		if(pIter->nLenIn < 0) pIter->done = true;
	}
}

bool das_uniq_iter_next(das_uniq_iter* pIter){

	if(pIter->done) return false;
	
	if(! pIter->ragged){
		/* Quicker function for CUBIC datasets, as long as you're 
		   at the last index of an array dimension, keep setting
		   zero and rolling previous */

		for(int iDim = pIter->last; iDim >= pIter->first; --iDim){
			if(pIter->lock[iDim]) continue;

			if(pIter->index[iDim] < (pIter->shape[iDim] - 1)){
				pIter->index[iDim] += 1;
				return true;
			}
			else{
				pIter->index[iDim] = 0;
			}
		}
	
		pIter->done = true;
		return false;	
	}
		
	/* I'm ragged so I can't use the generic shape function, but I can
	 * at least save off the length of the last index at this point
	 * and only change it when a roll occurs */
	ptrdiff_t nLenInIdx = 0;
	for(int iDim = pIter->last; iDim >= pIter->first; --iDim){
			
		if(iDim == pIter->last)
			nLenInIdx = pIter->nLenIn;
		else
			nLenInIdx = DasDs_lengthIn(pIter->pDs, iDim, pIter->index);
				
		if(pIter->index[iDim] < (nLenInIdx - 1)){
			pIter->index[iDim] += 1;
				
			/* If bumping an index that's not the last, recompute the length
			 * of the last run that I care about */
			if(iDim < pIter->last)
				pIter->nLenIn = DasDs_lengthIn(pIter->pDs, pIter->last, pIter->index);
				
			return true;
		}
		else{
			pIter->index[iDim] = 0;
		}
	}
		
	pIter->done = true;
	return false;
}

/* ************************************************************************* */

void das_cube_iter_init(
	das_cube_iter* pIter, int nRank, ptrdiff_t* pMin, ptrdiff_t* pMax 
){
	pIter->done = true;

	if((nRank < 1)||(nRank > DASIDX_MAX))
		das_error(DASERR_ITER, "Invalid array rank %d", nRank);

	memcpy(pIter->idxmin, pMin, sizeof(ptrdiff_t)*nRank);
	memcpy(pIter->index, pMin, sizeof(ptrdiff_t)*nRank);
	memcpy(pIter->idxmax, pMax, sizeof(ptrdiff_t)*nRank);

	pIter->rank = nRank;

	/* Check to see if any max values are greater then the corresponding min */
	for(int i = 0; i < nRank; ++i){
		if(pMax[i] > pMin[i]){
			pIter->done = false;
			return;
		}
	}
}

bool das_cube_iter_next(das_cube_iter* pIter)
{
	if(pIter->done) return false;

	for(int i = (pIter->rank - 1); i > -1; --i){

		/* Increment this index, if you can */
		if((pIter->index[i] + 1) < pIter->idxmax[i]){
			pIter->index[i] += 1;
			return true;
		}
		else{
			/* Need to bump next lower index, if no lower index, we're done */
			if(i != 0)
				pIter->index[i] = pIter->idxmin[i];
		}
	}
	pIter->done = true;  /* no next loop, no need for a break */
	return false;
}
