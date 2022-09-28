/* Copyright (C) 2017 Chris Piker <chris-piker@uiowa.edu>
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

#include <assert.h>
#include <string.h>
#include <stdarg.h>
#include <stddef.h>

#include "util.h"
#define _das_array_c_
#include "array.h"
#include "units.h"
#include "log.h"

/* ************************************************************************* */
/* Global index initialization memory */
const ptrdiff_t g_aShapeUnused[DASIDX_MAX] = DASIDX_INIT_UNUSED;
const ptrdiff_t g_aShapeZeros[DASIDX_MAX]  = DASIDX_INIT_BEGIN; 

const char g_sIdxLower[DASIDX_MAX] = {
	'i','j','k','l','m','n','p','q','r','s','t','u','v','w','x','y'
};
const char g_sIdxUpper[DASIDX_MAX] = {
	'I','J','K','L','M','N','P','Q','R','S','T','U','V','W','X','Y'
};

/* ************************************************************************* */
/* Little helpers */

char* das_idx_prn(int nRank, ptrdiff_t* pLoc, size_t uLen, char* sBuf)
{
	size_t uWrote = 0;
	if(uWrote < uLen){ sBuf[0] = '('; ++uWrote; }
	for(int d = 0; d < nRank; ++d){
		if((d > 0)&&(uWrote < uLen)){ sBuf[uWrote] = ',';  ++uWrote;}
		if(uWrote < uLen) 
			uWrote += snprintf(sBuf + uWrote, uLen - uWrote, "%td", pLoc[d]);
	}
	if(uWrote < uLen){ sBuf[uWrote] = ')'; ++uWrote; }
	return sBuf;
}

int das_rng2shape(
	int nRngRank, const ptrdiff_t* pMin, const ptrdiff_t* pMax, size_t* pShape
){
	int nShapeRank = 0;
	
	if((pMin == NULL)||(pMax == NULL)||(pShape==NULL)||(nRngRank < 1)||
		(nRngRank > DASIDX_MAX)){
		das_error(DASERR_VAR, "Invalid stride range arguments");
		return -1;
	}
	
	ptrdiff_t nSz = 0;
	for(int d = 0; d < nRngRank; ++d){
		nSz = pMax[d] - pMin[d];
		if((nSz <= 0)||(pMin[d] < 0)||(pMax[d] < 1)){
			das_error(
				DASERR_VAR, "Invalid %c slice range %zd to %zd", g_sIdxLower[d],
				pMin[d], pMax[d]
			);
			return -1;
		}
		if(nSz > 1){
			pShape[nShapeRank] = nSz;
			++nShapeRank;
		}
	}
	
	return nShapeRank;
}

/* ************************************************************************* */
/* DynaBuf functions */ 

bool DynaBuf_alloc(DynaBuf* pThis, size_t uMore)
{
	if(pThis->uSize >= (pThis->pHead - pThis->pBuf) + pThis->uValid + uMore)
		return true;
		
	size_t uAlloc = pThis->uValid + uMore;  /* The minimum to alloc */
		
	/* If it's less than twice my current number of valid items alloc that */
	if(uAlloc < (pThis->uValid * 2)) uAlloc = pThis->uValid * 2;
	
	/* If it's still less than room for 64 items, alloc that */
	if(uAlloc < 64) uAlloc = 64;
		
	/* Make sure the total allocation is an integer number of chunk sizes */
	if((pThis->uChunkSz > 0)&&(pThis->uChunkSz != uAlloc)){
		size_t uRemain = 0;
		if(pThis->uChunkSz < uAlloc)
			uRemain = uAlloc % pThis->uChunkSz;
		else
			uRemain = pThis->uChunkSz - uAlloc;
		uAlloc += uRemain;
	}
		
	/* Just using malloc below since we are going to fill this space
	 * anyway, though you might consider a debug build which uses calloc
	 * so than empty expanses of memory are noticeable */
	byte* pNew = (byte*) malloc( uAlloc * pThis->uElemSz );
	
	if(pNew == NULL){
		das_error(DASERR_ARRAY, "Couldn't allocate for %zu items of size %zu",
				     uAlloc, pThis->uElemSz );
		return false;
	}
	pThis->uSize = uAlloc;
	
	/* Maintain the old write offset */
	/* pThis->pWrite = pNew + (pThis->pWrite - pThis->pHead); */
	
	if(pThis->uValid)
		memcpy(pNew, pThis->pHead, (pThis->uElemSz) * (pThis->uValid) );
	
	if(pThis->pBuf != NULL) free(pThis->pBuf);
	
	pThis->pBuf = pNew;
	pThis->pHead = pNew;
	return true;
}

/* Add the given number of fill values.  Use memmove because the amount of
 * data written in each call goes up exponentially and memmove is freaking fast,
 * much faster than a linear write loop for large arrays */
bool DynaBuf_appendFill(DynaBuf* pThis, size_t uCount)
{
	size_t uDone = 0, uWrite = 0, uElemSz = pThis->uElemSz;
	
	if(! DynaBuf_alloc(pThis, uCount)) return false;
	
	byte* pWrite = pThis->pHead + (pThis->uValid * pThis->uElemSz);
	
	memcpy(pWrite, pThis->pFill, uElemSz);
	uDone = 1;
	
	while(uDone < uCount){
		
		if(uDone > (uCount - uDone))  
			uWrite = uCount - uDone;
		else
			uWrite = uDone;	
		
		memmove(pWrite + uDone*uElemSz, pWrite, uElemSz*uWrite);
		
		uDone += uWrite;
	}
	pThis->uValid += uCount;
	/* if(bBumpWrite) pThis->pWrite += uCount*(pThis->uElemSz); */
	return true;
}


/* uItems - make space for at least uItems worth of elements to start */
bool DynaBuf_init(
	DynaBuf* pThis, size_t uItems, das_val_type et, size_t uElemSz, 
	const byte* pFill, size_t uChunkSz, size_t uShape
){
	pThis->etype = et;
	if((uElemSz < 1)||(uElemSz > 2147483647)){
		das_error(DASERR_ARRAY, "Element size can't be %zu", uElemSz);
		return false;
	}
	pThis->uElemSz = uElemSz;
	pThis->uChunkSz = uChunkSz;
	pThis->uShape = uShape;
	pThis->bKeepMem = false;
	
	/* Alloc space for fill IF it's bigger than the size of an index_info
	 * object, just store it in the static fill buffer  */
	if(uElemSz > sizeof(das_idx_info)){
		if( (pThis->pFill = (byte*) calloc(1, uElemSz)) == NULL){
			das_error(DASERR_ARRAY, "couldn't allocate %zu bytes for fill "
					  "value.", uElemSz);
			return false;
		}
	}
	else{
		pThis->pFill = pThis->fillBuf;
	}
	memcpy(pThis->pFill, pFill, uElemSz);
	
	/* Maybe allocate room for uItems, none of which are valid */
	pThis->uValid = 0;
	pThis->uSize = 0;
	pThis->pBuf = NULL;
	pThis->pHead = NULL;
	/* pThis->pWrite = NULL; */
	if(uItems > 0) return DynaBuf_alloc(pThis, uItems);
	return true;
}

/* Opposite of init, free all memory */
void DynaBuf_release(DynaBuf* pThis)
{
	if(!pThis->bKeepMem && pThis->pBuf != NULL) free(pThis->pBuf);
	pThis->pBuf = pThis->pHead = NULL; /*pThis->pWrite = NULL; */
	pThis->uSize = pThis->uValid = pThis->uChunkSz = 0;
	if(pThis->pFill != pThis->fillBuf) free(pThis->pFill);
}


/* Add values to the array, this just pours in data without regard to record
 * boundaries, higher level append tracks shape info */
size_t DynaBuf_append(DynaBuf* pThis, const byte* pVals, size_t uCount)
{
	/* successfully do nothing*/
	if(uCount == 0) return pThis->uValid;
	
	if(pVals == NULL){
		das_error(DASERR_ARRAY, "NULL pointer for argument pVals");
		return 0;
	}
	
	if(! DynaBuf_alloc(pThis, uCount) ) /* Only alloc's if needed */
		return 0;
	
	/* now for the new stuff... */
	byte* pWrite = pThis->pHead + (pThis->uValid * pThis->uElemSz);
	memcpy(pWrite, pVals, uCount * pThis->uElemSz);
	
	pThis->uValid += uCount;
	return pThis->uValid;
}


/* ************************************************************************* */
/* Basic Info */

const char* DasAry_id(const DasAry* pThis){return pThis->sId; }

das_units DasAry_units(const DasAry* pThis){ return pThis->units; }

das_val_type DasAry_valType(const DasAry* pThis)
{
	return pThis->pBufs[pThis->nRank - 1]->etype;
}

const char* DasAry_valTypeStr(const DasAry* pThis){
	return das_vt_toStr(pThis->pBufs[pThis->nRank - 1]->etype);
}

char* DasAry_toStr(const DasAry* pThis, char* sInfo, size_t uLen)
{
	size_t uWrote; 
	uWrote = snprintf(sInfo, uLen-1, "%s %s", DasAry_valTypeStr(pThis), pThis->sId);
	size_t uSz = 0;
	for(int d = 0; d < pThis->nRank; ++d){
		if(d == 0) uSz = pThis->pIdx0->uCount;
		else uSz = pThis->pBufs[d]->uShape;
		
		if(uLen > (uWrote+1)){
			if(uSz > 0)
				uWrote += snprintf(sInfo + uWrote, uLen - (uWrote+1), "[%zu]", uSz);
			else
				uWrote += snprintf(sInfo + uWrote, uLen - (uWrote+1), "[]");
		}
	}
	sInfo[uWrote] = '\0';
	return sInfo;
}

const byte* DasAry_getFill(const DasAry* pThis){
	return pThis->pBufs[pThis->nRank - 1]->pFill;
}

bool DasAry_setFill(DasAry* pThis, das_val_type vt, const byte* pFill)
{
	if(pFill == NULL) pFill = (const byte*) das_vt_fill(DasAry_valType(pThis));
	DynaBuf* pBuf = pThis->pBufs[pThis->nRank - 1];
	
	if(vt != pBuf->etype){
		das_error(DASERR_ARRAY, "Element type mismatch");
		return false;
	}
	
	memcpy(pBuf->pFill, pFill, pBuf->uElemSz);
	return true;
}

/* ************************************************************************* */
/* Common operations used in multiple functions */

das_idx_info* _Array_LastParentFor(const DasAry* pThis, int iDim)
{
	DynaBuf* pBuf;
	das_idx_info* pParent = pThis->pIdx0;
	ptrdiff_t iOffset;
	for(int d = 0; d < iDim; ++d ){
		
		if(pParent->uCount < 1) return NULL; /* no children to qube */
		iOffset = pParent->nOffset + pParent->uCount - 1;
		
		pBuf = pThis->pBufs[d];
		if(iOffset >= pBuf->uValid){
			das_error(DASERR_ARRAY, "Invalid state for array %s", pThis->sId);
			return 0;
		}
		pParent = ((das_idx_info*)(pBuf->pHead)) + iOffset;
	}
	return pParent;
}

bool _Array_ParentAndItemAt(
	const DasAry* pThis, int nIndices, ptrdiff_t* pLoc, das_idx_info** ppParent, 
	byte** ppItem
){
	/* Get index info item at this partial index, or the item pointer for
	 * a complete index */
	int d = 0;
	byte* pItem = NULL;
	das_idx_info* pParent = pThis->pIdx0;
	ptrdiff_t iLoc, nOffset;
	DynaBuf* pBuf = NULL;
	for(d = 0; d < nIndices; ++d){
		/* Handle negative indicies */
		if(pLoc[d] < 0) iLoc = pParent->uCount + pLoc[d];
		else iLoc = pLoc[d];
				
		/* Invalid index in subsets */
		if((iLoc < 0)||(iLoc >= pParent->uCount)){ return false; }
		
		pBuf = pThis->pBufs[d];                  /* This dimension's buffer */
		nOffset = pParent->nOffset + iLoc;
		
		/* Invalid index in memory */
		if(nOffset >= pBuf->uValid){ return false; }
		
		pItem = pBuf->pHead + nOffset*pBuf->uElemSz;
		if(d < (pThis->nRank - 1)){ 
			pParent = (das_idx_info*)pItem;
			pItem = NULL;
		}
	}
	
	*ppParent = pParent;
	*ppItem = pItem;
	return true;
}

/* Upper bound in return values is *INCLUSIVE* */
bool _Array_elemOffsets(
	const DasAry* pThis, int iDim, das_idx_info* pParent, size_t* pFirstOff, 
	size_t* pLastOff
){
	DynaBuf* pDynaBuf = NULL;
	
	das_idx_info* pIiFirst = pParent;
	das_idx_info* pIiLast = pParent;
	
	size_t uFirstOff = 0;
	size_t uLastOff = 0;
	int d = iDim;
	while(true){
		uFirstOff = pIiFirst->nOffset;
		if(pIiFirst->uCount == 0) return false;  /* assumes 0 count entries mean no
														      * further children */
		uLastOff = pIiLast->nOffset + pIiLast->uCount - 1;
		
		if(d == pThis->nRank - 1) break;
	
		pDynaBuf = pThis->pBufs[d];
		pIiFirst = ((das_idx_info*)pDynaBuf->pHead) + uFirstOff;
		pIiLast = ((das_idx_info*)pDynaBuf->pHead) + uLastOff;
		++d;
	}
	*pLastOff = uLastOff;
	*pFirstOff = uFirstOff;
	return true;
}

/* ************************************************************************* */
/* Algorithm for getting flat offsets from multi-dim indices.
 * 
 *     Index      Offset
 *     -----      -------------
 *      i         p0[0].offset + i
 *      j         pI[ p0[0].offset + i].offset + j
 *      k         pJ[ pI[ p0[0].offset + i ].offset + j ].offset + k
 *     
 *      ...and so on
 */
/* Get flat index in element array or return -1 */
ptrdiff_t Array_flat(const DasAry* pThis, ptrdiff_t* pLoc)
{
	/* The general case, make fast later */
	int d;
	DynaBuf* pBuf;
	ptrdiff_t nOffset, iLoc;
	das_idx_info* pParent = pThis->pIdx0;
	for(d = 0; d < pThis->nRank; ++d){
		/* Handle negative indicies */
		if(pLoc[d] < 0) iLoc = pParent->uCount + pLoc[d];
		else iLoc = pLoc[d];
				
		/* Invalid index in subsets */
		if((iLoc < 0)||(iLoc >= pParent->uCount)) break; 
		
		pBuf = pThis->pBufs[d];
		nOffset = pParent->nOffset + iLoc;
		
		if(nOffset >= pBuf->uValid) break;      /* Invalid index in memory */
		
		if(d < (pThis->nRank - 1))              /* Go down another level */
			pParent = (das_idx_info*) (pBuf->pHead + nOffset*pBuf->uElemSz);
		else
			return nOffset;
	}
	
	return -1;
}

/* ************************************************************************* */
/* Shape, Size and general length functions  */

int DasAry_shape(const DasAry* pThis, ptrdiff_t* pShape)
{
	if(pShape == NULL){
		das_error(DASERR_ARRAY, "NULL shape pointer");
		return -1;
	}
	
	for(int d = 0; d < pThis->nRank; ++d){
		if(d == 0) pShape[d] = pThis->pIdx0->uCount;
		else{
			if(pThis->pBufs[d]->uShape != 0)
				pShape[d] = pThis->pBufs[d]->uShape;
			else
				pShape[d] = DASIDX_RAGGED;
		}
	}
	
	return pThis->nRank;
}

int DasAry_stride(
	const DasAry* pThis, ptrdiff_t* pShape, ptrdiff_t* pStride)
{
	if(pShape == NULL){
		das_error(DASERR_ARRAY, "NULL shape array pointer");
		return -1;
	}
	if(pStride == NULL){
		das_error(DASERR_ARRAY, "NULL stride array pointer");
		return -1;
	}
	
	int d;
	for(d = 0; d < pThis->nRank; ++d){
		if(d == 0) pShape[d] = pThis->pIdx0->uCount;
		else{
			if(pThis->pBufs[d]->uShape != 0)
				pShape[d] = pThis->pBufs[d]->uShape;
			else
				pShape[d] = DASIDX_RAGGED;
		}
	}
	
	/* Strides are calculated backwards */
	pStride[pThis->nRank - 1] = 1;
	for(d = pThis->nRank - 2; d > -1; --d){
		
		/* Ragged strides casacade */
		if((pStride[d+1] < 0)||(pShape[d+1] < 0))
			pStride[d] = DASIDX_RAGGED;
		else
			pStride[d] = pShape[d+1]*pStride[d+1];
	}
	
	return pThis->nRank;
}

size_t DasAry_valSize(const DasAry* pThis)
{
	return pThis->pBufs[pThis->nRank - 1]->uElemSz;
}

/* Since array elements are stored in continuous memory, get the flat index
 * of the first element and the flat index of the last.  The difference is 
 * the size.  This fails if child items are not continuous! */
size_t DasAry_size(const DasAry* pThis)
{
	size_t uFirstOff, uLastOff;
	if(_Array_elemOffsets(pThis, 0, pThis->pIdx0, &uFirstOff, &uLastOff)){
		return uLastOff - uFirstOff + 1;	
	}
	return 0;
}

size_t DasAry_lengthIn(const DasAry* pThis, int nIdx, ptrdiff_t* pLoc)
{	
	if((nIdx < 0)||(nIdx > pThis->nRank)){
		das_error(DASERR_ARRAY, "Rank %d array '%s' does not have %d indices",
				     pThis->nRank, pThis->sId, pThis->nRank, nIdx);
		return 0;
	}
	
	/* Get the index_info item at this partial index.  If null then it's
	 * a complete index and the count is always 1 */
	das_idx_info* pParent;
	byte* pItem;
	
	if(! _Array_ParentAndItemAt(pThis, nIdx, pLoc, &pParent, &pItem)){
		char sInfo[128] = {'\0'};
		char sLoc[64] = {'\0'};
		das_error(DASERR_ARRAY, "Invalid subset index %s in array %s",
				     das_idx_prn(nIdx, pLoc, 64, sLoc),
		           DasAry_toStr(pThis, sInfo, 128));
		return 0;
	}
	
	if(pParent == NULL) return 1;
	else return pParent->uCount;
}

/* ************************************************************************* */
/* Getting/Setting values from the array without changing it's size */

bool DasAry_validAt(const DasAry* pThis, ptrdiff_t* pLoc)
{
	/* The general case, make fast later */
	das_idx_info* pParent;
	byte* pItem;
	
	if(! _Array_ParentAndItemAt(pThis, pThis->nRank, pLoc, &pParent, &pItem))
		return false;
	else
		return (pItem != NULL);
}

const byte* DasAry_getAt(const DasAry* pThis, das_val_type et, ptrdiff_t* pLoc)
{	
	/* Type safety check */
	das_val_type etype = pThis->pBufs[pThis->nRank-1]->etype;
	if(etype != et){
		das_error(DASERR_ARRAY, "Elements for array '%s' are '%s' not '%s'",
				     pThis->sId, das_vt_toStr(etype), et);
		return NULL;
	}
	
	/* The general case, make fast later */
	das_idx_info* pParent;
	byte* pItem;
	
	if(! _Array_ParentAndItemAt(pThis, pThis->nRank, pLoc, &pParent, &pItem)){
		char sInfo[128] = {'\0'};
		char sLoc[64] = {'\0'};
		das_error(DASERR_ARRAY, "Invalid subset index %s in array %s",
				     das_idx_prn(pThis->nRank, pLoc, 64, sLoc),
		           DasAry_toStr(pThis, sInfo, 128));
		return NULL;
	}
	assert(pItem != NULL);
	return pItem;
}

/* if nIndices == 0, this should work just like size + get(0,0,0...) */
/* if nIndices == rank, this should work just like get(i,j,k) with a count
 * of 1 */

DAS_API byte* DasAry_getBuf(
	DasAry* pThis, das_val_type et, int nDim, ptrdiff_t* pLoc, size_t* pCount
){
	return (byte*)DasAry_getIn(pThis, et, nDim, pLoc,  pCount);
}

const byte* DasAry_getIn(
	const DasAry* pThis, das_val_type et, int nDim, ptrdiff_t* pLoc,
	size_t* pCount
){
	das_val_type etype = pThis->pBufs[pThis->nRank-1]->etype;
	if(etype != et){
		das_error(DASERR_ARRAY, "Elements for array '%s' are '%s' not '%s'",
				     pThis->sId, das_vt_toStr(etype), das_vt_toStr(et));
		*pCount = 0;
		return NULL;
	}
	if((nDim > pThis->nRank)||(nDim < 0)){
		das_error(DASERR_ARRAY, "Rank %d array '%s' does not have an index "
				     "number %d ", pThis->nRank, pThis->sId, pThis->nRank, nDim);
		return NULL;
	}
	
	/* Get index info item at this partial index, or the item pointer for
	 * a complete index */
	das_idx_info* pParent = pThis->pIdx0;
	byte* pItem = NULL;
	
	if(!_Array_ParentAndItemAt(pThis, nDim, pLoc, &pParent, &pItem)){
		char sInfo[128] = {'\0'};
		char sLoc[64] = {'\0'};
		das_error(DASERR_ARRAY, "Invalid subset index %s in array %s",
				     das_idx_prn(nDim, pLoc, 64, sLoc),
		           DasAry_toStr(pThis, sInfo, 128));
		return 0;
	}
	
	if(pItem != NULL){
		*pCount = 1;
		return pItem;
	}
	
	/* Phase two.  Get the first and last elements in the subset defined 
	 * by the index_info parent.  Return a pointer to the first one and use
	 * the distance to the last one to calculate the size
	 */
	size_t uFirstOff = 0;
	size_t uLastOff = 0;
	if(!_Array_elemOffsets(pThis, nDim, pParent, &uFirstOff, &uLastOff)){
		return NULL;
	}
	
	*pCount = uLastOff - uFirstOff + 1;
	DynaBuf* pBuf = pThis->pBufs[pThis->nRank - 1];
	return (const byte*) pBuf->pHead + (uFirstOff * pBuf->uElemSz);
}

bool DasAry_putAt(
	DasAry* pThis, ptrdiff_t* pStart, const byte* pVals, size_t uVals
){
	if(uVals == 0) return true;  /* Successfully do nothing */
	
	char sInfo[128] = {'\0'};
	char sLoc[64] = {'\0'};
		
	/* Is the write point a legal address? */
	ptrdiff_t iPut = Array_flat(pThis, pStart);
	if(iPut == -1){
		das_error(DASERR_ARRAY, "Initial write location %s not valid in %s",
				     das_idx_prn(pThis->nRank, pStart, 64, sLoc),
		           DasAry_toStr(pThis, sInfo, 128));
		return false;
	}
	
	/* Will the final location be a valid address? */
	DynaBuf* pBuf = pThis->pBufs[pThis->nRank - 1];
	if(iPut + uVals > pBuf->uValid){
		das_error(DASERR_ARRAY, "Final write location %s + %zu is not valid "
				     "in %s", das_idx_prn(pThis->nRank, pStart, 127, sLoc), uVals,
		           DasAry_toStr(pThis, sInfo, 128));
		return false;
	}
	
	void* pPut = pBuf->pHead + iPut*pBuf->uElemSz;
	
	/* Don't use memcpy here as someone may be duplicating array values via
	 * this function */
	memmove(pPut, pVals, uVals*pBuf->uElemSz);  
	return true;
}

/* ************************************************************************* */
/* Appending */

void DasAry_markEnd(DasAry* pThis, int iDim)
{
	if(pThis->pIdx0 != &(pThis->index0)){
		char sInfo[128] = {'\0'};
		das_error(DASERR_ARRAY, "Write operation attempted on sub-array %s", 
		           DasAry_toStr(pThis, sInfo, 128));
		return;
	}
	if((iDim < 0)||(iDim >= pThis->nRank)){
		char sInfo[128] = {'\0'};
		das_error(DASERR_ARRAY, "Dimension %d dosen't exist in array %s",
		           iDim, DasAry_toStr(pThis, sInfo, 128));
		return;
	}
	if(iDim == 0){ 
		das_error(DASERR_ARRAY, "Append always works. Marking the end of the "
				     "0th dimension is not allowed.");
		return;
	}
	for(int d = iDim; d < pThis->nRank; ++d) pThis->pBufs[d]->bRollParent = true;
}

/* I think the append algorithm could be made faster, smaller and more 
   elegant at some point, but not sure how to do so right now.  -cwp */

das_idx_info* _newIndexInfo(DasAry* pThis, int iDim)
{
	/* Only works for rank 2 or higher arrays.  Make a new index_info object 
	 * for the rank - 2 dimension and increment it's parent count */
	assert((iDim > -1)&&(iDim < (pThis->nRank - 1)));
	
	/* only works if each DynaBuf has at least one element, append handles this */
	DynaBuf* pMyBuf = pThis->pBufs[iDim];
	das_idx_info* pLast = (das_idx_info*)pMyBuf->pHead;
	pLast += pMyBuf->uValid - 1;
	
	das_idx_info next;
	next.uCount = 0;
	next.nOffset = pLast->nOffset + pLast->uCount;
	
	/* Get a parent that has room for a new index entry */
	das_idx_info* pParent;
	if(iDim == 0){
		pParent = pThis->pIdx0;  /* Always has room */
	}
	else{
		DynaBuf* pParentBuf = pThis->pBufs[iDim - 1];
		pParent = (das_idx_info*)pParentBuf->pHead;
		assert(pParentBuf->uValid > 0);
		pParent += pParentBuf->uValid - 1;
				  
		if(pMyBuf->bRollParent ||
			((pMyBuf->uShape != 0)&&(pParent->uCount == pMyBuf->uShape)))
			pParent = _newIndexInfo(pThis, iDim - 1);
		pMyBuf->bRollParent = false;
	}
	
	pParent->uCount += 1;
	DynaBuf_append(pMyBuf, (const byte*)&next, 1); 
	return ((das_idx_info*)(pMyBuf->pHead)) + pMyBuf->uValid - 1;
}

bool DasAry_append(DasAry* pThis, const byte* pVals, size_t uCount)
{
	if(pThis->pIdx0 != &(pThis->index0)){
		char sInfo[128] = {'\0'};
		das_error(DASERR_ARRAY, "Write operation attempted on sub-array %s ", 
		           DasAry_toStr(pThis, sInfo, 128));
		return false;
	}
	/* Appending causes creation of new parents (which may trigger
	 * creation of their own parents) for any non-ragged dimension */
	DynaBuf* pElemBuf = pThis->pBufs[pThis->nRank - 1];
	/*size_t uPreValid = pBuf->uValid;*/
	
	if(uCount == 0) return true; /* Successfully do nothing */
	
	/* First the easy part, store the new elements */
	size_t uNewValid;
	if(pVals == NULL)
		uNewValid = DynaBuf_appendFill(pElemBuf, uCount);
	else
		uNewValid = DynaBuf_append(pElemBuf, pVals, uCount);
	if(uNewValid == 0) return false;
	
	/* Get the last parent pointer in the highest index_info dimension, make
	 * sure it exists if you have to */
	das_idx_info* pParIdx = pThis->pIdx0;
	das_idx_info* pChildIdx = NULL;
	das_idx_info info;
	DynaBuf* pIdxBuf = NULL;
	for(int d = 0; d < pThis->nRank - 1; ++d){
		pIdxBuf = pThis->pBufs[d];
		
		if(pParIdx->uCount == 0){ 
			pParIdx->uCount = 1;
			info.uCount = 0;
			info.nOffset = 0;
			DynaBuf_append(pIdxBuf, (const byte*)&info, 1);
		}
		/* Cast it and let the compiler do the sizeof math for you */
		pChildIdx = (das_idx_info*)(pIdxBuf->pHead);
		pChildIdx += pParIdx->nOffset + pIdxBuf->uValid - 1;
		pParIdx = pChildIdx;
	}
	
	/* There are a couple broken symmetries here;
	 * 
	 * 1) Ragged dimensions can have an infinitly expanded count, or may be
	 *    marked as closed.
	 * 
	 * 2) Raggedness is determined by the shape parameter.  If it's zero the
	 *    dimension is ragged, except for the first dimension, it's always 
	 *    ragged this is becasue of the rule: "append always works"
	 */
	
	size_t uRoom, uAdded, uMarked = 0;
	int iParentDim = pThis->nRank - 2;
	while(uMarked < uCount){
		
		/* Does the parent have room for everything? */
		if((iParentDim == -1)||(!pElemBuf->bRollParent && (pElemBuf->uShape == 0))){
			pParIdx->uCount += uCount;
			uMarked = uCount;
		}
		else{
			/* Does the parent have room for anything? */
			if((!pElemBuf->bRollParent) && (pParIdx->uCount < pElemBuf->uShape)){
				uRoom = pElemBuf->uShape - pParIdx->uCount;
				uAdded = (uRoom > uCount) ? uCount : uRoom;
				pParIdx->uCount += uAdded;
				uMarked += uAdded;
			}
			else{
				/* Ziltch, gonna need a new parent and clean my end flag if set */
				if( (pParIdx = _newIndexInfo(pThis, iParentDim)) == NULL){
					das_error(DASERR_ARRAY, "logic error");
					return false;
				}
				pElemBuf->bRollParent = false;
			}
		}
	}
	
	return true;
}

/* ************************************************************************* */
/* Qubing */

/* Qube from the bottom up:
 * 
 * 1. You can't qube a ragged dimension, but you can qube the last element
 *    of a ragged dimension as long as it's children are fixed length.  This
 *    is typically how the function would be called.  In addition while 
 *    qubing the last child there may may too many elements to fit, so a new
 *    last child may need to be created.
 * 
 * 2. Non-ragged dimensions that hold ragged arrays are a problem.  If the
 *    count for a valid index_info can be 0 it breaks alot of algorithms.  If
 *    this function is called to qube a non-ragged dimension with ragged 
 *    children it will just fail.  Remember, the first dimension is always 
 *    implicitly ragged, so this always works for any rank 2 array.  
 * 
 * iQubeIn - The dimension we want to make rectangular.  All child dimensions
 *           for the last element here will be made rectangular as well.
 *          
 * 
 */

size_t _Array_qubeSelf(
	DasAry* pThis, int iChildDim, das_idx_info* pParent
){
	if(pThis->pIdx0 != &(pThis->index0)){
		char sInfo[128] = {'\0'};
		das_error(DASERR_ARRAY, "Write operation attempted on sub-array %s", 
		           DasAry_toStr(pThis, sInfo, 128));
		return 0;
	}
	/* One of two situations:
	 * 1. I have all the children I need, so just check all children 
	 * 2. I don't have all the children I need, so create them one at 
	 *    a time.
	 * 3. This is one of the few times I can (temporarily) have index_info
	 *    entries with count = 0
	 */
	DynaBuf* pBuf = pThis->pBufs[iChildDim];
	size_t uNeed = 0;
	
	if(pParent->uCount < pBuf->uShape)
		uNeed = pBuf->uShape - pParent->uCount;
	
	/* Make sure I have all sub elements filled */
	if(uNeed > 0){
		if(!DynaBuf_appendFill(pBuf, uNeed)) return 0;
		pParent->uCount = pBuf->uShape;
	}
	
	/* I just have normal children then I'm done */
	if(iChildDim == pThis->nRank - 1) return uNeed;
	
	/* I have index children, fill all of them *IN ORDER* */
	size_t uWrote = 0;
	das_idx_info* pChild;
	for(size_t u = 0; u < pBuf->uShape; ++u){
		pChild = ((das_idx_info*)(pBuf->pHead)) + pParent->nOffset + u;
		uWrote += _Array_qubeSelf(pThis, iChildDim + 1, pChild);
	}
	return uWrote;
}

size_t DasAry_qubeIn(DasAry* pThis, int iRecDim)
{
	
	if(iRecDim == 0){
		das_error(DASERR_ARRAY, "Dimension 0 is always automatically a qube");
		return 0;
	}	
	if(iRecDim >= pThis->nRank){
		char sAry[128];
		das_error(DASERR_ARRAY, "In array %s, dimension %d does not exist",
		           DasAry_toStr(pThis, sAry, 128));
		return 0;
	}

	/* Count back from the last index towards the first and find the highest
	 * index equal to iRecDim that is fixed length */
	
	/* Say shape(2,4)   iQubeIn = 0, iDim = 0
	 *     shape(0,4)   iQubeIn = 0, iDim = 1
	 *     shape(0,0)   iQubeIn = 0, iDim = -1 (can't qube)
	 * 
	 *     shape(2,4)   iQubeIn = 1, iDim = 1
	 *     shape(0,4)   iQubeIn = 1, iDim = 1
	 *     shape(0,0)   iQubeIn = 1, iDim -1
	 */
	int iDim, iQubeDim = -1;
	for(iDim = pThis->nRank-1; iDim > 0; --iDim){
		if(pThis->pBufs[iDim]->uShape == 0) break;
		if(iDim >= iRecDim) iQubeDim = iDim;
	}
	
	if(iQubeDim < 1) return 0;  /* Can't qube anything, vals are ragged */
	
	/* Get the last parent descriptor for elements in dimension iQubeDim */
	das_idx_info* pParent;
	if((pParent = _Array_LastParentFor(pThis, iQubeDim)) == NULL) return 0;
	
	size_t uWrote = _Array_qubeSelf(pThis, iQubeDim, pParent);
	return uWrote;
}

/* ************************************************************************* */
/* Removing */

size_t DasAry_clear(DasAry* pThis)
{
	if(pThis->pIdx0 != &(pThis->index0)){
		char sInfo[128] = {'\0'};
		das_error(DASERR_ARRAY, "Write operation attempted on sub-array %s", 
		           DasAry_toStr(pThis, sInfo, 128));
		return 0;
	}
	size_t uWasValid = pThis->pBufs[pThis->nRank - 1]->uValid;
	pThis->pIdx0->uCount = 0;
	for(int d = 0; d < pThis->nRank; ++d) pThis->pBufs[d]->uValid = 0;
	return uWasValid;
}

/*
size_t Array_rmHead(Array* pThis, size_t uRecs)
{
	size_t uRm = uRecs * pThis->lSkip[0];
	if(uRm > pThis->uValid){ 
		Array_clear(pThis);
		return 0;
	}
	
	pThis->lShape[0] -= uRecs;
	
	pThis->pHead += uRm;	
	pThis->uValid -= uRm;
	return pThis->uValid;
}

size_t Array_rmTail(Array* pThis, size_t uRecs)
{
	size_t uRm = uRecs * pThis->lSkip[0];
	if(uRm > pThis->uValid){ 
		Array_clear(pThis);
		return 0;
	}
	
	pThis->lShape[0] -= uRecs;
	
	pThis->pHead += uRm;	
	pThis->uValid -= uRm;
	return pThis->uValid;
}
*/

/* ************************************************************************* */
/* Packet tie in */
void DasAry_setSrc(DasAry* pThis, int nPktId, size_t uStartItem, size_t uItems)
{
	pThis->nSrcPktId = nPktId;
	pThis->uStartItem = uStartItem;
	pThis->uItems = uItems;
}


/* ************************************************************************* */
/* Construct, Destruct */

DasAry* new_DasAry(
	const char* id, das_val_type et, size_t sz_each, const byte* fill, 
	int rank, size_t* shape, das_units units
){
	DasAry* pThis = NULL;
	int i = 0;
	
	/* validation */
	if((id == NULL)||(id[0] == '\0')){
		das_error(DASERR_ARRAY, "id parameter empty"); return NULL;
	}
	if(!das_assert_valid_id(id)) return false;

	if(rank < 1){
		das_error(DASERR_ARRAY, "In array '%s', rank 0 (or less) arrays are"
		           " not supported.", id);  return NULL;
	}
	if(shape == NULL){
		das_error(DASERR_ARRAY, "In array '%s', shape argument is NULL ", id);
		return NULL;
	}
	if(rank > DASIDX_MAX){
		das_error(DASERR_ARRAY, "In array '%s', rank %d (or more) arrays are"
		           " not supported", id, rank /* serial number ? */);
	}
	
	/* since shape is unsigned we can't directly check for negative shape
	   values since these will appear as huge positive values, but casting to 
		ptrdiff_t does let us check on the pontential problem caused by users 
		casting size_t to (long int) or similar */
	ptrdiff_t nTest;
	for(i = 0; i < rank; ++i){ /* Check for negative shape values */
		nTest = (ptrdiff_t)shape[i];
		if(nTest < 0){
			das_error(DASERR_ARRAY, "In array %s, invalid shape value, %zu for"
					  " index %d", id, shape[i], i);
		return NULL;
		}
	}
	
	pThis = (DasAry*) calloc(1, sizeof(DasAry));
	strncpy(pThis->sId, id, 63);
	pThis->nRank = rank;
	pThis->units = units;
	
	/* Not a sub-array of any other array, owns all buffers*/
	pThis->pIdx0 = &pThis->index0;
	
	ptrdiff_t nChunk = 1.0;
	das_val_type etCur = vtIndex;
	size_t uSize = 1.0;
	size_t uElemSz = das_vt_size(vtIndex);
	size_t u;
	das_idx_info* pIdx = NULL;
	const byte* pFill = (const byte*) das_vt_fill(vtIndex);
	for(int d = 0; d < rank; ++d){
		pThis->pBufs[d] = &(pThis->bufs[d]);
		pThis->pBufs[d]->uShape = shape[d];
		
		/* Rational:  Chunk size of a dimension is equal to the multiple of
		 *            all previous chunk sizes times my own.  My chunk size is
		 *            1.0 if I'm the top index, or I'm ragged, otherwise it's my
		 *            shape times the chunk size of all previous dimensions. */
		if(d == 0) nChunk = 1.0;
		else       nChunk = shape[d] > 0 ? shape[d] * nChunk : 1.0;
		
		if(d == rank - 1){ 
			etCur = et;
			if(et == vtUnknown){
				uElemSz = sz_each;
				pFill = fill;
			}
			else {
				uElemSz = das_vt_size(et);
				if(fill != NULL) pFill = fill;
				else pFill = (const byte*) das_vt_fill(et);
				pThis->compare = das_vt_getcmp(et);
			}
		}
		
		/* Rational: The size of the array to allocate is the multiple of all
		 *           shapes up to this dimension.  If I, or any previous
		 *           dimension, is ragged, size collapses to 0 */
		uSize *= shape[d];
		
		DynaBuf_init(pThis->pBufs[d], uSize, etCur, uElemSz, pFill, nChunk, shape[d]);
		
		/* If preallocating, apply fill to all the elements */
		
		/* WARNING: If this changes, update DasConstant_subset !! */
		if(uSize > 0)
			DynaBuf_appendFill(pThis->pBufs[d], uSize); /* Used by DasConstant */
		
		/* Rational:  Initialize all index entries that point down to this array.
		 *            only matters if my size is > 0.  
		 */
		if(uSize > 0){
			for(u = 0; u < uSize; u += shape[d]){
				if(d > 0) 
					pIdx = ((das_idx_info*)(pThis->pBufs[d - 1]->pHead)) + (u/shape[d]);
				else 
					pIdx = pThis->pIdx0;
				
				pIdx->uCount = shape[d];
				pIdx->nOffset = u;
			}
		}
	}
	
	pThis->refcount = 1;
	return pThis;
}

/* syntax sugar */
DasAry* new_DasPtrAry(const char* sType, int rank, size_t* shape)
{
	void* p = NULL;
	return new_DasAry(
		sType, vtUnknown, sizeof(void*), (byte*)&p, rank, shape, 
		UNIT_DIMENSIONLESS
	);
}

byte* DasAry_disownElements(DasAry* pThis, size_t* pLen, size_t* pOffset)
{
	*pLen = 0;
	int iLast = pThis->nRank - 1;
	DynaBuf* pDb = pThis->pBufs[iLast];
	*pLen = pDb->uValid;
	
	if(*pLen == 0) return NULL;  /* If there is nothing stored, return NULL */

	/* I don't own my elements so return NULL, though the number of elements is
	   *not* zero */	
	if(!DasAry_ownsElements(pThis)) return NULL;
	
	pThis->bufs[iLast].bKeepMem = true;  /* Don't nuke memory on decrement */
	
	/* We have to return a free-able pointer to the caller.  Even 
	 * though valid memory starts at pHead, pBuf is the actual pointer
	 * that is being tracked by malloc.  So if pBuf != pHead adjust the
	 * offset occordingly */	
	
	*pOffset = pThis->bufs[iLast].pHead - pThis->bufs[iLast].pBuf;
	
	return pThis->bufs[iLast].pBuf;
}

bool DasAry_ownsElements(const DasAry* pThis)
{
	int iLast = pThis->nRank - 1;
	
	/* I am a subset array */
	if(pThis->pBufs[iLast] != &(pThis->bufs[iLast])) return false;
	
	/* Already disowned */
	if(pThis->bufs[iLast].bKeepMem) return false;
	
	return true;  /* Okay, I claim them */
}


int inc_DasAry(DasAry* pThis)
{ 
	pThis->refcount += 1; return pThis->refcount;
}

int ref_DasAry(const DasAry* pThis){ return pThis->refcount; }

void dec_DasAry(DasAry* pThis)
{	
	pThis->refcount -= 1;
	if(pThis->refcount < 1){
		if(pThis->pMemOwner != NULL){
			dec_DasAry(pThis->pMemOwner);
		}
		else{
			for(int d = 0; d < pThis->nRank; ++d){
				if(pThis->pBufs[d] == &(pThis->bufs[d])) 
					DynaBuf_release(pThis->pBufs[d]);
			}
		}
		free(pThis);
	}
}

/* Not sure about this.  Maybe using etText would just automatically have 
 * an extra ragged dimension in the array */

unsigned int DasAry_setUsage(DasAry* pThis, unsigned int uFlags){
	unsigned int uOld = pThis->uFlags;
	pThis->uFlags = uFlags;
	return uOld;
}
unsigned int DasAry_getUsage(DasAry* pThis){
	return pThis->uFlags;
}
/* ************************************************************************* */
/* Subset constructor */

DasAry* DasAry_subSetIn(
	DasAry* pThis, const char* id, int nIndices, ptrdiff_t* pLoc
){
	das_idx_info* pParent = NULL;
	byte* pItem = NULL;
	if(!_Array_ParentAndItemAt(pThis, nIndices, pLoc, &pParent, &pItem)){
		return NULL;
	}
	if((nIndices < 0)||(nIndices > pThis->nRank)){
		char sLoc[64] = {'\0'};
		char sInfo[128] = {'\0'};
		das_error(DASERR_ARRAY,
			"Invalid index %s in array %s", das_idx_prn(nIndices, pLoc, 63, sLoc),
			DasAry_toStr(pThis, sInfo, 128)
		);
		return NULL;
	}
	if(nIndices == pThis->nRank){
		char sLoc[64] = {'\0'};
		char sInfo[128] = {'\0'};
		das_error(DASERR_ARRAY, "Too many indices, specified, location %s is an"
			" element address and not a subset in array '%s' (i.e. rank 0 arrays "
			"are not supported).", das_idx_prn(nIndices, pLoc, 64, sLoc),
			DasAry_toStr(pThis, sInfo, 128)
		);
		return NULL;
	}
	DasAry* pOther = (DasAry*)calloc(1, sizeof(DasAry));
	if(id == NULL)
		snprintf(pOther->sId, DAS_MAX_ID_BUFSZ - 1, "%s_subset", pThis->sId);
	else
		strncpy(pOther->sId, id, DAS_MAX_ID_BUFSZ - 1);
	
	pOther->nRank = pThis->nRank - nIndices;
	
	pOther->pIdx0 = pParent;
	pOther->compare = pThis->compare;
	
	/* This is where strictly using parent offsets becomes really important */
	/* Don't assume that pIdx0->uOffset == 0 ! */
	for(int d = 0; d < pOther->nRank; ++d)
		pOther->pBufs[d] = pThis->pBufs[d + nIndices];
	
	pOther->pMemOwner = pThis;
	
	inc_DasAry(pThis);
	return pOther;
}
