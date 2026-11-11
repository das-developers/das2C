/* Copyright (C) 2024 Chris Piker <chris-piker@uiowa.edu>
 *
 * This file is part of das2C, the Core Das C Library.
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

#include "iter.h"

/* ************************************************************************* */
/* Array iteration support */

void init_DasAryIter(
   DasAryIter* pThis, const DasAry* pAry, int iIdxFirst, int iIdxLast, 
   const ptrdiff_t* pAddrBeg, const ptrdiff_t* pAddrEnd
)
{
	memset(pThis, 0, sizeof(DasAryIter));

	pThis->pAry = pAry;
	pThis->rank = DasAry_shape(pThis->pAry, pThis->aShape);
	if(pThis->aShape[0] == 0){  /* Can't iterate an empty array */
		pThis->done = true;
		return;
	}

	pThis->bRagged = false;
	for(int i = 1; i < pThis->rank; ++i){
		if(pThis->aShape[i] == DASIDX_RAGGED){
			pThis->bRagged = true;
			break;
		}
	}

	pThis->iIdxFirst = iIdxFirst;
	if(pThis->iIdxFirst >= pThis->rank){
		pThis->done = true;
		return;
	}
	pThis->iIdxLast = iIdxLast;
	/* Python-style negative index from the end: -1 is the last index, -2 the
	   one before it.  (rank + iIdxLast), NOT (rank - iIdxLast). */
	if(pThis->iIdxLast < 0) pThis->iIdxLast = pThis->rank + pThis->iIdxLast;
	if((pThis->iIdxLast < 0)||(pThis->iIdxLast < pThis->iIdxFirst)){
		pThis->done = true;
		return;
	}

	/* .addr is treated as the "start" value.  We continually increment
	   the start value until it is >= the end value */

	/* Start off at all zeros, or at the specified location */
	if(pAddrBeg != NULL)
		memcpy(pThis->addr, pAddrBeg, (pThis->iIdxLast+1)*sizeof(ptrdiff_t));

	/* End at all zeros, or at the end of the array. end is an exclusive upper index */
	if(pAddrEnd != NULL)
		memcpy(pThis->addr, pAddrEnd, (pThis->iIdxLast+1)*sizeof(ptrdiff_t));
	else
		pThis->bNaturalEnd = true;

	/* If I'm ragged, get the length in the last dimension I can iterate */
	if(pThis->bRagged){
		pThis->nLenLast = DasAry_lengthIn(pAry, pThis->iIdxLast, pThis->addr);
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
static bool _DasAryIter_beforeEnd(DasAryIter* pThis)
{
	for(int iDim = pThis->iIdxFirst; iDim <= pThis->iIdxLast; ++iDim){
		if(pThis->addr[iDim] > pThis->aAddrEnd[iDim]){
			pThis->done = true;
			return false;
		}
		if(pThis->addr[iDim] < pThis->aAddrEnd[iDim])
			return true;
		
		/* not conclusive, check next lower (left most) index */
	}

	pThis->done = true;
	return false;
}

bool DasAryIter_next(DasAryIter* pThis)
{
	if(pThis->done) return false;

	if(! pThis->bRagged){

		/* Quicker function for CUBIC arrays */
		for(int iDim = pThis->iIdxLast; iDim >= pThis->iIdxFirst; --iDim){
			if(pThis->addr[iDim] < (pThis->aShape[iDim] - 1)){
				pThis->addr[iDim] += 1;
				return (pThis->bNaturalEnd ? true : _DasAryIter_beforeEnd(pThis));
			}
			else{
				pThis->addr[iDim] = 0; /* and go again, incrementing next index up */
			}
		}

		pThis->done = true;  /* must have run out if dimensions */
		return false;
	}

	/* Ahh, ragged dense arrays. Guess someone selected hard mode */
	ptrdiff_t nLenInIdx = 0;
	for(int iDim = pThis->iIdxLast; iDim >= pThis->iIdxFirst; --iDim){
		if(iDim == pThis->iIdxFirst)
			nLenInIdx = pThis->nLenLast;
		else
			nLenInIdx = DasAry_lengthIn(pThis->pAry, iDim, pThis->addr);

		if(pThis->addr[iDim] < (nLenInIdx - 1)){
			pThis->addr[iDim] += 1;
				
			/* Look ahead.  If bumping an index that's not the last, save off the
			   length of the last run that we care about */
			if(iDim < (pThis->iIdxLast - 1))
				pThis->nLenLast = DasAry_lengthIn(pThis->pAry, pThis->iIdxLast, pThis->addr);
				
			return (pThis->bNaturalEnd ? true : _DasAryIter_beforeEnd(pThis));
		}
		else{
			pThis->addr[iDim] = 0;
		}
	}
		
	pThis->done = true;
	return false;

}

/* ************************************************************************* */
/* Dataset iteration support */

void init_DasDsIter(DasDsIter* pThis, const DasDs* pDs){
	
	memset(pThis, 0, sizeof(DasDsIter));
	
	pThis->rank = DasDs_shape(pDs, pThis->aShape);
	pThis->pDs = pDs;

	/* If this is an empty dataset, we're already done */
	if(pThis->aShape[0] == 0){
		pThis->done = true;
		return;
	}
	
	pThis->bRagged = false; 
	for(int i = 1; i < pThis->rank; ++i){      /* Ignore ragged on first index */
		if(pThis->aShape[i] == DASIDX_RAGGED){
			pThis->bRagged = true;
			break;
		}
	}
	
	/* Start off index at all zeros, which memset insures above */
	
	/* If I'm ragged I'm going to need the size of the last index at the 
	 * lowest point of all previous indexes, get that. */
	if(pThis->bRagged){
		pThis->nLenLast = DasDs_lengthIn(pDs, pThis->rank - 1, pThis->addr);
		if(pThis->nLenLast < 0) pThis->done = true;
	}
}

bool DasDsIter_next(DasDsIter* pThis){

	if(pThis->done) return false;
	
	if(! pThis->bRagged){
		/* Quicker function for CUBIC datasets */
		for(int iDim = pThis->rank - 1; iDim >= 0; --iDim){
			if(pThis->addr[iDim] < (pThis->aShape[iDim] - 1)){
				pThis->addr[iDim] += 1;
				return true;
			}
			else{
				pThis->addr[iDim] = 0;
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
			nLenInIdx = pThis->nLenLast;
		else
			nLenInIdx = DasDs_lengthIn(pThis->pDs, iDim, pThis->addr);
				
		if(pThis->addr[iDim] < (nLenInIdx - 1)){
			pThis->addr[iDim] += 1;
				
			/* If bumping an index that's not the last, recompute the length
			 * of the last run */
			if(iDim < (pThis->rank - 1))
				pThis->nLenLast = DasDs_lengthIn(pThis->pDs, pThis->rank - 1, pThis->addr);
				
			return true;
		}
		else{
			pThis->addr[iDim] = 0;
		}
	}
		
	pThis->done = true;
	return false;
}

/* ************************************************************************* */

void init_DasDsUniqIter(
	DasDsUniqIter* pThis, const DasDs* pDs, const DasVar* pVar
){
	memset(pThis, 0, sizeof(DasDsUniqIter));
	
	pThis->rank = DasDs_shape(pDs, pThis->aShape);
	pThis->pDs = pDs;

	/* If this is an empty dataset, we're already done */
	if(pThis->aShape[0] == 0){
		pThis->done = true;
		return;
	}

	ptrdiff_t aVarShape[DASIDX_MAX] = DASIDX_INIT_UNUSED;
	DasVar_shape(pVar, aVarShape);
	
	/* Lock the indexes that are ignored by this variable to 0, and determine
	 * if I'm ragged in a used index */	
	pThis->bRagged = false;
	pThis->iIdxFirst = -1;
	pThis->iIdxLast  = -1;
	for(int i = 0; i < pThis->rank; ++i){
		if(aVarShape[i] == DASIDX_UNUSED){
			pThis->aLock[i] = true;
		}
		else{
			pThis->iIdxLast = i;
			if(pThis->iIdxFirst == -1) pThis->iIdxFirst = i;
		}

		if((!pThis->aLock[i])&&(i > 0)&&(pThis->aShape[i] == DASIDX_RAGGED))
			pThis->bRagged = true;
	}

	/* In the odd case of a constant, just set done right now */
	if((pThis->iIdxFirst == -1)||(pThis->iIdxFirst == -1))
		pThis->done = true;

	/* Start off index at all zeros, which memset insures above */
	
	/* If I'm ragged I'm going to need the size of the last used index
	 * at the lowest point of all previous indexes, get that. */

	if(pThis->bRagged){
		pThis->nLenLast = DasDs_lengthIn(pDs, pThis->iIdxLast, pThis->addr);
		if(pThis->nLenLast < 0) pThis->done = true;
	}
}

bool DasDsUniqIter_next(DasDsUniqIter* pThis){

	if(pThis->done) return false;
	
	if(! pThis->bRagged){
		/* Quicker function for CUBIC datasets, as long as you're 
		   at the last index of an array dimension, keep setting
		   zero and rolling previous */

		for(int iDim = pThis->iIdxLast; iDim >= pThis->iIdxFirst; --iDim){
			if(pThis->aLock[iDim]) continue;

			if(pThis->addr[iDim] < (pThis->aShape[iDim] - 1)){
				pThis->addr[iDim] += 1;
				return true;
			}
			else{
				pThis->addr[iDim] = 0;
			}
		}
	
		pThis->done = true;
		return false;	
	}
		
	/* I'm ragged so I can't use the generic shape function, but I can
	 * at least save off the length of the last index at this point
	 * and only change it when a roll occurs */
	ptrdiff_t nLenInIdx = 0;
	for(int iDim = pThis->iIdxLast; iDim >= pThis->iIdxFirst; --iDim){
			
		if(iDim == pThis->iIdxLast)
			nLenInIdx = pThis->nLenLast;
		else
			nLenInIdx = DasDs_lengthIn(pThis->pDs, iDim, pThis->addr);
				
		if(pThis->addr[iDim] < (nLenInIdx - 1)){
			pThis->addr[iDim] += 1;
				
			/* If bumping an index that's not the last, recompute the length
			 * of the last run that I care about */
			if(iDim < pThis->iIdxLast)
				pThis->nLenLast = DasDs_lengthIn(pThis->pDs, pThis->iIdxLast, pThis->addr);
				
			return true;
		}
		else{
			pThis->addr[iDim] = 0;
		}
	}
		
	pThis->done = true;
	return false;
}

/* ************************************************************************* */

void init_DasDsCubeIter(
	DasDsCubeIter* pThis, int nRank, const ptrdiff_t* pAddrBeg,
	const ptrdiff_t* pAddrEnd
){
	pThis->done = true;

	if((nRank < 1)||(nRank > DASIDX_MAX))
		daserrf(DASERR_ITER, "Invalid array rank %d", nRank);

	memcpy(pThis->aAddrBeg, pAddrBeg, sizeof(ptrdiff_t)*nRank);
	memcpy(pThis->addr, pAddrBeg, sizeof(ptrdiff_t)*nRank);
	memcpy(pThis->aAddrEnd, pAddrEnd, sizeof(ptrdiff_t)*nRank);

	pThis->rank = nRank;

	/* Check to see if any end values are greater then the corresponding begin */
	for(int i = 0; i < nRank; ++i){
		if(pAddrEnd[i] > pAddrBeg[i]){
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
		if((pThis->addr[i] + 1) < pThis->aAddrEnd[i]){
			pThis->addr[i] += 1;
			return true;
		}
		else{
			/* Need to bump next lower index, if no lower index, we're done */
			if(i != 0)
				pThis->addr[i] = pThis->aAddrBeg[i];
		}
	}
	pThis->done = true;  /* no next loop, no need for a break */
	return false;
}
