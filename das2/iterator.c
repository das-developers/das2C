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

void dasds_iter_init(dasds_iterator* pIter, const DasDs* pDs){
	
	memset(pIter, 0, sizeof(dasds_iterator));
	
	pIter->rank = DasDs_shape(pDs, pIter->shape);
	pIter->pDs = pDs;
	
	pIter->ragged = false;
	for(int i = 0; i < pIter->rank; ++i){
		if((pIter->shape[i] == DASIDX_RAGGED)||(pIter->shape[i] == DASIDX_RAGGED)){
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

bool dasds_iter_next(dasds_iterator* pIter){
	
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