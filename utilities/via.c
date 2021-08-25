/* Copyright (C) 2015-2017 Chris Piker <chris-piker@uiowa.edu>
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
 * version 2.1 along with libdas2; if not, see <http://www.gnu.org/licenses/>. 
 */


#define _POSIX_C_SOURCE 200112L

#include <string.h>
#include <assert.h>

#include <das2/util.h>

#include "via.h"


/* Private Functions ******************************************************* */

/* Return real index for corresponding virtual index for any location
 *  including "no man's land" , return -1 index is not valid
 */
int _Via_realIdx(const Via* pThis, int iVirt)
{
	int iReal = 0;
	if(iVirt < pThis->iVirt0){
		iReal = pThis->nSz - (pThis->iVirt0 - iVirt);
		
		/* avoid wrap to positive */
		int iMax = pThis->iVmax - pThis->iVirt0;  
		if(iReal <= iMax) return -1;
	}
	else{
		iReal = iVirt - pThis->iVirt0;
		
		/* avoid wrap to negative */
		int iMin = pThis->nSz - (pThis->iVirt0 - pThis->iVmin); 
		if(iReal >= iMin) return -1;  
	}
	return iReal;	
}

/* Realloc and copy if needed the array so that the given virtual index
   will fit, don't expand past nMaxSz */

int _Via_realloc(Via* pThis, int iVirt)
{
	int nNeeded = 0;
	int iReal = 0;
	int iMax = 0, iMin = 0;
	if(iVirt < pThis->iVirt0){ 
		iReal = pThis->nSz - (pThis->iVirt0 - iVirt);
		iMax = pThis->iVmax - pThis->iVirt0;
				
		nNeeded = iMax - iReal;
	}
	else{
		iReal = iVirt - pThis->iVirt0;
		iMin  = pThis->nSz - (pThis->iVirt0 - pThis->iVmin);
		
		nNeeded = iReal - iMin;
	}
	
	assert(nNeeded > 0);
		
	if((nNeeded + pThis->nSz) > pThis->nMaxSz){ 
		fprintf(stderr, "WARNING: Request to exceed maximum buffer setting of %zu "
				  "bytes in _Via_realloc\n", pThis->nMaxSz*sizeof(double));
		/*
		das2_error(38, 
			"in _Via_realloc, max buffer size is %d bytes, but %d are required "
			"allow for virutal index %d when virtual index 0 = %d and current "
			"min & max indexes are %d & %d.\n", pThis->nMaxSz*sizeof(double), 
			(nNeeded + pThis->nSz)*sizeof(double), iVirt, pThis->iVirt0, 
			pThis->iVmin, pThis->iVmax
		);
		*/
		           
		return -1;
	}
		
	/* If we need more than half of uMaxSz, just take all of it, otherwise 
	   just double the current memory usage */
	int nNewSz = 0;
	if( (pThis->nSz + nNeeded) > (pThis->nMaxSz / 2)){
		nNewSz = pThis->nMaxSz;
	}
	else{
		nNewSz = pThis->nSz*2;
		while( nNewSz < (nNeeded + pThis->nSz)) nNewSz *= 2;
	}
	
	double* pNewBuf = (double*)calloc(nNewSz, sizeof(double));
	if(pNewBuf == NULL){ 
		das_error(38, "Alloc failure on %d bytes", nNewSz*sizeof(double));
		return -1;
	}
	
	/* Copy over the values */
	iMax = pThis->iVmax - pThis->iVirt0;
	int nBytes = (iMax + 1)*sizeof(double);
	memcpy(pNewBuf, pThis->pBuf, nBytes);
	
	/* Upper range needs to be shifted (assuming it exists) */
	iMin = pThis->nSz - (pThis->iVirt0 - pThis->iVmin);
	
	if(iMin < pThis->nSz){
		nBytes = (pThis->nSz - iMin)*sizeof(double);
		int nShift = nNewSz - pThis->nSz;
		memcpy(pNewBuf + nShift + iMin,  pThis->pBuf + iMin,  nBytes);
	}
	
	pThis->nSz = nNewSz;
	free(pThis->pBuf);
	pThis->pBuf = pNewBuf;
	
	/* Now return the recalculated real index for iVert */
	return _Via_realIdx(pThis, iVirt);
}

/* Small helpers *********************************************************** */
int Via_minIndex(const Via* pThis){ 
	return pThis->bHasData ? pThis->iVmin : -1;
}

int Via_maxIndex(const Via* pThis){ 
	return pThis->bHasData ? pThis->iVmax : -2; 
}

int Via_length(const Via* pThis){
	return pThis->bHasData ? pThis->iVmax - pThis->iVmin + 1 : 0;
}

int Via_lastSet(const Via* pThis){
	if(!pThis->bHasData) 
		exit( 
			das_error(38, "Logic error in caller to Via_lastSet, no value has "
			           "been written for this virtual index array.")
		);
	return pThis->iVlast;
}

bool Via_valid(const Via* pThis, int iVirt){
	if(!pThis->bHasData) return false;
	return (iVirt <= pThis->iVmax) && (iVirt >= pThis->iVmin);
}

/* Constructor ************************************************************* */
Via* new_Via(int nInitial, int nMax)
{
	if(nInitial < 1 || nMax < 1) return NULL;
		
	Via* pThis = (Via*)calloc(1, sizeof(Via));
	
	pThis->pBuf = (double*)calloc(nInitial, sizeof(double));
	if(pThis->pBuf == NULL){
		das_error(32, "Alloc failure on %d bytes", nInitial*sizeof(double));
		return NULL;
	}
	
	if(pThis->pBuf == NULL) return NULL;
	pThis->nSz = nInitial;
	pThis->nMaxSz = nMax;
	pThis->bHasData = false;
	
	return pThis;
}

/* Destructor ************************************************************** */
void del_Via(Via* pThis)
{
	free(pThis->pBuf);
	free(pThis);
}

/* Clear virt table ******************************************************** */

void Via_clear(Via* pThis)
{
	pThis->bHasData = false;
	pThis->iVirt0 = pThis->iVmin = pThis->iVmax = 0;
	
	if(pThis->nSz == 1) pThis->pBuf[0] = 0.0;
	else memset(pThis->pBuf, 0, pThis->nSz * sizeof(double));
}


/* Get a value at legal index value **************************************** */
double Via_get(const Via* pThis, int iVirt)
{
	if(!pThis->bHasData)
		exit(
			das_error(38, "No valid indicies in current virtual index array")
		);
	
	  
	if((iVirt < pThis->iVmin) || (iVirt > pThis->iVmax)){ 
		/* index is in no man's land */
		exit(
			das_error(38, "Index %d outside of range %d to %d (inclusive)",
				        iVirt, pThis->iVmin, pThis->iVmax)
		);		
	}
	
	int iReal = _Via_realIdx(pThis, iVirt);
	if(iReal == -1) exit(das_error(32, "WTF?"));
	
	return *(pThis->pBuf + iReal);
}

/* Set a value at an index ************************************************* */
bool Via_set(Via* pThis, int iVirt, double rVal)
{
	if(!pThis->bHasData){
		pThis->iVirt0 = iVirt;
		pThis->iVmin = iVirt;
		pThis->iVmax = iVirt;
		pThis->pBuf[0] = rVal;
		pThis->bHasData = true;
		return true;
	}
	
	int iReal = _Via_realIdx(pThis, iVirt);
	if(iReal == -1){
		if( (iReal = _Via_realloc(pThis, iVirt)) == -1) return false;
	}
	
	/* Returned true, we know we have enough space to save the data without
	   wrapping */
	pThis->pBuf[iReal] = rVal;
	
	if((iVirt >= pThis->iVirt0) && (iVirt > pThis->iVmax)) pThis->iVmax = iVirt;
	if((iVirt < pThis->iVirt0) && (iVirt < pThis->iVmin)) pThis->iVmin = iVirt;
	
	pThis->iVlast = iVirt;
	
	return true;
}

/* Accumulate or set a value at an index *********************************** */

bool Via_add(Via* pThis, int iVirt, double rVal)
{
	if(!Via_valid(pThis, iVirt))
		return Via_set(pThis, iVirt, rVal);
	
	double rOld = Via_get(pThis, iVirt);
	return Via_set(pThis, iVirt, rOld + rVal);
}


/* UnitTest **************************************************************** */

#ifdef TEST_VIA

int main(int argc, char** argv)
{
	double rVal = 0.0;
	
	/* Exit on errors, log info messages and above */
	das_init(argv[0], DASERR_DIS_EXIT, 0, DAS_LL_INFO, NULL);
	
	Via* pVia = new_Via(10, 100);
	assert( Via_valid(pVia, 0) == false);
	assert( Via_set(pVia, -1, 1.0) );
	assert( Via_set(pVia, -5, 1.0) );
	assert( Via_set(pVia,  2, 1.0) );
	
	double rSum = 0.0;
	for(int i = Via_minIndex(pVia); i <= Via_maxIndex(pVia); ++i){
		rVal = Via_get(pVia, i);
		rSum += rVal;
	}
	
	assert( rSum == 3.0);
	
	/* Now trigger a realloc */
	assert( Via_set(pVia, -40, 1.0) );
	assert( Via_set(pVia, 30,  1.0) );
	assert( Via_add(pVia, 31,  1.0) );
	assert( Via_add(pVia, -5,  1.0) );
	
	rSum = 0.0;
	for(int i = Via_minIndex(pVia); i <= Via_maxIndex(pVia); ++i){
		rVal = Via_get(pVia, i);
		rSum += rVal;
	}
	assert( rSum == 7.0);
	
	
	/* Test for request past maximum */
	fprintf(stderr, "The next line should produce a warning...\n");
	assert( Via_set(pVia, 8000, 1.0) == false);
	fprintf(stderr, "did it work?\n");
	
	/* Test to see if clear works */
	Via_clear(pVia);
	
	assert( Via_length(pVia) == 0);
	assert( Via_add(pVia, -1, 1.0) );
	assert( Via_length(pVia) == 1);
	
	fprintf(stderr, "VirtualIndexArray class self-test passes\n");
	
	fprintf(stderr, "Will now exit with an error to test bounds checking, "
			  "this is normal\n");
	rVal = Via_get(pVia, 2000);
	return 0;
}

#endif
