/* Copyright (C) 2017-2024 Chris Piker <chris-piker@uiowa.edu>
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

#include <ctype.h>
#include <stdbool.h>
#include <string.h>
#ifndef _WIN32
#include <strings.h>
#else
#define strcasecmp _stricmp
#endif
#include <assert.h>

#include "util.h"
#include "dataset.h"
#include "utf8.h"
#include "log.h"

/* ************************************************************************* */
/* Dataset Inspection Functions */

size_t DasDs_numDims(const DasDs* pThis, enum dim_type vt){
	size_t uDims = 0;
	for(size_t u = 0; u < pThis->uDims; ++u){
		if(pThis->lDims[u]->dtype == vt) ++uDims;
	}
	return uDims;
}

DasDim* DasDs_getDim(DasDs* pThis, const char* sId, enum dim_type dmt)
{
	const char* sDimId = NULL;
	for(size_t u = 0; u < pThis->uDims; ++u){
		if(pThis->lDims[u]->dtype != dmt)
			continue;

		sDimId = DasDim_id(pThis->lDims[u]);
		if(strcasecmp(sId, sDimId) == 0) 
			return pThis->lDims[u];
	}
	return NULL;
}

DasDim* DasDs_getDimByIdx(DasDs* pThis, size_t idx, enum dim_type dmt)
{
	size_t uTypeIdx = 0;
	for(size_t u = 0; u < pThis->uDims; ++u){
		if(pThis->lDims[u]->dtype == dmt){
			if(uTypeIdx == idx) return pThis->lDims[u];
			else ++uTypeIdx;
		}
	}
	return NULL;
}

DasDim* DasDs_getDimById(DasDs* pThis, const char* sId)
{
	const char* sDimId = NULL;
	for(size_t u = 0; u < pThis->uDims; ++u){
		sDimId = DasDim_id(pThis->lDims[u]);
		if(strcasecmp(sId, sDimId) == 0) return pThis->lDims[u];
	}
	return NULL;
	
}

void DasDs_setMutable(DasDs* pThis, bool bChangeAllowed)
{	
	/* On a transition from mutable to un-mutable, cache the shape */
	if(pThis->_dynamic && !bChangeAllowed) DasDs_shape(pThis, pThis->_shape);
	
	pThis->_dynamic = bChangeAllowed;
}


int DasDs_shape(const DasDs* pThis, ptrdiff_t* pShape)
{
	/* If static, just return value captured at _setMutable(false) call */
	if(! pThis->_dynamic ){
		memcpy(pShape, pThis->_shape, sizeof(ptrdiff_t)*DASIDX_MAX);
		return pThis->nRank;
	}
	
	for(int i = 0; i < pThis->nRank; ++i) pShape[i] = DASIDX_UNUSED;

	/* Find out my current shape.  Ask all the dimensions thier shape.
	 * Since this can be an instantaneous question during data flow, respond 
	 * back with the smallest set (union) of all the dimension's shapes */

	DasDim* pDim = NULL;
	int nDimRank = 0;
	size_t uDim = 0;
	ptrdiff_t aShape[DASIDX_MAX];
	
	for(uDim = 0; uDim < pThis->uDims; ++uDim){
		pDim = pThis->lDims[uDim];
		nDimRank = DasDim_shape(pDim, aShape);
		
		if(nDimRank > pThis->nRank){
			das_error(
				DASERR_DS, "Dimension rank consistancy check failure.  Dimension "
				"%s (%s) of dataset %s, is rank %d, must be at most rank %d for consistancy", 
				pDim->sId, pDim->sDim, pThis->sId, nDimRank, pThis->nRank
				);
			return 0;
		}
		
		das_varindex_merge(pThis->nRank, pShape, aShape);
	}
	return pThis->nRank;
}

ptrdiff_t DasDs_lengthIn(const DasDs* pThis, int nIdx, ptrdiff_t* pLoc)
{
	
	int nLengthIn = DASIDX_UNUSED;
	int nVarLenIn = DASIDX_UNUSED;
	
	/* The simple function below fails if only a REFERENCE and OFFSET are
	 * specifed but not the CENTER variable */
	
	const DasDim* pDim = NULL;
	for(int i = 0; i < pThis->uDims; ++i){
		pDim = pThis->lDims[i];
		nVarLenIn = DasDim_lengthIn(pDim, nIdx, pLoc);
		
		nLengthIn = das_varlength_merge(nLengthIn, nVarLenIn);
	}
	return nLengthIn;
}

bool DasDs_cubicCoords(const DasDs* pThis,  const DasDim** pCoords)
{
	ptrdiff_t aDsShape[DASIDX_MAX] = DASIDX_INIT_UNUSED;
	int nRank = DasDs_shape(pThis, aDsShape);

	for(int i = 0; i < nRank; ++i){  /* For each index... */

		bool bGotIt = false;
		for(int c = 0; c < pThis->uDims; ++c){  /* for each coordinate */

			const DasDim* pCoord = pThis->lDims[c];

			if(pCoord->dtype != DASDIM_COORD) continue;

			bool bUsed = false;           /* See if already used for lower index*/
			for(int j = 0; j < i; ++j){
				if(pCoords[j] == pCoord)
					bUsed = true;
			}
			if(bUsed) continue;

			if(DasDim_degenerate(pCoord, i)) 
				continue;  /* Doesn't depend on this idx */

			/* make sure it depends only on this index */
			bool bOnlyMe = true;
			for(int j = 0; j < nRank; ++j){
				if(j == i) continue;

				if(! DasDim_degenerate(pCoord, j)){
					bOnlyMe = false;
					break;
				}
			}
			if(!bOnlyMe)
				continue;
			
			pCoords[i] = pCoord;
			bGotIt = true;
			break;
		}

		if(!bGotIt) return false;
	}
	return true;
}

/* ************************************************************************* */
/* Post-Construction sub-item addition */

DasErrCode DasDs_addAry(DasDs* pThis, DasAry* pAry)
{
	
	/* In python ABI language, this function steals a reference to the 
	 * array.  */
	
	if(pThis->uSzArrays < (pThis->uArrays + 1)){
		DasAry** pNew = NULL;
		size_t uNew = pThis->uSzArrays * 2;
		if( (pNew = (DasAry**)calloc(uNew, sizeof(void*))) == NULL) return DASERR_DS;

		if(pThis->uArrays > 0)
			memcpy(pNew, pThis->lArrays, pThis->uArrays * sizeof(void*));

		/* Only the heap buffer is mine to release, aArrays is inside me */
		if(pThis->lArrays != pThis->aArrays)
			free(pThis->lArrays);

		pThis->lArrays = pNew;
		pThis->uSzArrays = uNew;
	}
	pThis->lArrays[pThis->uArrays] = pAry;
	pThis->uArrays += 1;
	return DAS_OKAY;
}

DasAry* DasDs_getAryById(DasDs* pThis, const char* sAryId)
{
	for(size_t u = 0; u < pThis->uArrays; ++u){
		if(strcmp(pThis->lArrays[u]->sId, sAryId) == 0){
			return pThis->lArrays[u];
		}
	}
	return NULL;
}

size_t DasDs_memOwned(const DasDs* pThis)
{
	size_t uSize = 0;
	for(size_t u = 0; u < pThis->uArrays; ++u){
		uSize += DasAry_memOwned(pThis->lArrays[u]);
	}
	return uSize;
}


size_t DasDs_memUsed(const DasDs* pThis)
{
	size_t uSize = 0;
	for(size_t u = 0; u < pThis->uArrays; ++u){
		uSize += DasAry_memUsed(pThis->lArrays[u]);
	}
	return uSize;
}

size_t DasDs_memIndexed(const DasDs* pThis)
{
	size_t uSize = 0;
	for(size_t u = 0; u < pThis->uArrays; ++u){
		uSize += DasAry_memIndexed(pThis->lArrays[u]);
	}
	return uSize;	
}



DasErrCode DasDs_addDim(DasDs* pThis, DasDim* pDim)
{
	/* Since function maps mask off any un-used indices and since
	 * Variables can have internal structure beyond those needed for
	 * correlation, and since vars can be completly static! I guess
	 * there is nothing to check here other than that all dependend
	 * variables in the span are also owened by this dataset and that
	 * all arrays are also owned by this dataset.
	 */
	size_t v = 0;
	
	if(pDim->dtype == DASDIM_UNK)
		return das_error(DASERR_DS, "Can't add a dimension of type ANY to dataset %s", pThis->sId);
		
	
	/* Make sure that I don't already have a dimesion with this name */
	for(v = 0; v < pThis->uDims; ++v){
		if(strcmp(pThis->lDims[v]->sId, pDim->sId) == 0)
			return das_error(DASERR_DS, 
				"A dimension named %s already exists in dataset %s", pDim->sId, pThis->sId
			);
	}
	
	/* Going to remove direct coordinate references for now */
	
	/* 
	bool bInSet = false;
	size_t u = 0; 
	if(pDim->dtype == DATA_DIM){
		bInSet = false;
		for(u = 0; u < pDim->uCoords; ++u){
			for(v = 0; v < pThis->uDims; ++v){
				if(pDim->aCoords[u] == pThis->lDims[v]){
					bInSet = true;
					break;
				}
			}
			if(!bInSet){
				das_error(
					D2ERR_DS, "Data dimension %s depends on coordinate %s which "
					"is not part of dataset %s", pDim->sDim, pDim->aCoords[u]->sId, 
					pThis->sId
				);
				return false;
			}
		}
	}
	 */
	
	/* Deep drill, make sure any pointed to arrays are owned by this dataset... 
	 * 
	 * Actually, punt.  Someone could make a slice dataset with new variables
	 * with reduced function maps that have arrays that belong to someone else.
	 * and that should be okay 
	 */
	
	if(pThis->uSzDims < (pThis->uDims + 1)){
		size_t uNew = pThis->uSzDims * 2;
		DasDim** pNew = (DasDim**) calloc(uNew, sizeof(void*));
		if(pNew == NULL)
			return das_error(DASERR_DS, "Couldn't allocate space for %zu dimensions", uNew);

		if(pThis->uDims > 0)
			memcpy(pNew, pThis->lDims, pThis->uDims * sizeof(void*));

		/* Only the heap buffer is mine to release, aDims is inside me */
		if(pThis->lDims != pThis->aDims)
			free(pThis->lDims);

		pThis->lDims = pNew;
		pThis->uSzDims = uNew;
	}

	pThis->lDims[pThis->uDims] = pDim;
	pThis->uDims += 1;
	
	pDim->base.parent = &(pThis->base);

	return DAS_OKAY;
}

DasDim* DasDs_makeDim(
    DasDs* pThis, enum dim_type dType, const char* sDim, const char* sId
){
	DasDim* pDim = new_DasDim(sDim, sId, dType, pThis->nRank);
	if(DasDs_addDim(pThis, pDim) != DAS_OKAY){
		del_DasDim(pDim);
		return NULL;
	}
	return pDim;
}

/* ************************************************************************* */
/* Codec handling */

/* Most only be triggered at the transition to large, or garbage will be copied in */
DasErrCode _DasDs_codecsGoLarge(DasDs* pThis)
{
	/* Copy over the current codecs */
	size_t uNewSz = DASDS_LOC_ENC_SZ * 2;

	DasCodec* pNewCodecs = (DasCodec*) calloc(uNewSz, sizeof(DasCodec));
	int* pNewItems       = (int*) calloc(uNewSz, sizeof(int));

	/* Commit nothing until both land, else a half-grown dataset would have
	   lCodecs on the heap and lItems still in the small vector */
	if((pNewCodecs == NULL)||(pNewItems == NULL)){
		if(pNewCodecs != NULL) free(pNewCodecs);
		if(pNewItems  != NULL) free(pNewItems);
		return das_error(DASERR_DS, "Couldn't allocate space for %zu codecs", uNewSz);
	}

	memcpy(pNewCodecs, pThis->aCodecs, DASDS_LOC_ENC_SZ*sizeof(DasCodec));
	memcpy(pNewItems,  pThis->aItems,  DASDS_LOC_ENC_SZ*sizeof(int));

	pThis->lCodecs = pNewCodecs;
	pThis->lItems  = pNewItems;

	pThis->uSzCodecs = uNewSz;
	return DAS_OKAY;
}

DasErrCode _DasDs_codecsGoLarger(DasDs* pThis)
{
	/* We're already using dynamic codec array, now go even bigger */
	size_t uNewSz = pThis->uSzCodecs * 2;

	DasCodec* pNewCodecs = (DasCodec*) calloc(uNewSz, sizeof(DasCodec));
	int* pNewItems       = (int*) calloc(uNewSz, sizeof(int));

	/* Backing out here leaves the old buffers live and the dataset usable */
	if((pNewCodecs == NULL)||(pNewItems == NULL)){
		if(pNewCodecs != NULL) free(pNewCodecs);
		if(pNewItems  != NULL) free(pNewItems);
		return das_error(DASERR_DS, "Couldn't allocate space for %zu codecs", uNewSz);
	}

	memcpy(pNewCodecs, pThis->lCodecs, pThis->uSzCodecs*sizeof(DasCodec));
	memcpy(pNewItems,  pThis->lItems,  pThis->uSzCodecs*sizeof(int));

	free(pThis->lCodecs);
	free(pThis->lItems);

	pThis->lCodecs = pNewCodecs;
	pThis->lItems  = pNewItems;

	pThis->uSzCodecs = uNewSz;
	return DAS_OKAY;
}

/* Make room for one more codec.  Callers can't tell small-vector from heap and
   shouldn't have to. */
static DasErrCode _DasDs_codecsMakeRoom(DasDs* pThis)
{
	DasErrCode nRet = DAS_OKAY;

	/* Go dynamic? */
	if(pThis->uCodecs == DASDS_LOC_ENC_SZ)
		nRet = _DasDs_codecsGoLarge(pThis);

	/* Go even bigger? */
	if((nRet == DAS_OKAY)&&(pThis->uCodecs == pThis->uSzCodecs))
		nRet = _DasDs_codecsGoLarger(pThis);

	return nRet;
}

/* TODO: Got some copy-pasta in the next three functions, DRY the code out. 
         -cwp 2025-04-03
*/

DasCodec* DasDs_addFixedCodec(
	DasDs* pThis, const char* sAryId, const char* sSemantic, 
	const char* sEncType, int nItemBytes, int nNumItems, bool bRead
){

	if(_DasDs_codecsMakeRoom(pThis) != DAS_OKAY)
		return NULL;

	/* Find the array with this ID */
	DasAry* pAry = DasDs_getAryById(pThis, sAryId);
	if(pAry == NULL){
		das_error(DASERR_DS, "An array with id '%s' was not found", sAryId);
		return NULL;
	}

	DasCodec* pCodec = pThis->lCodecs + pThis->uCodecs;

	DasErrCode nRet = DasCodec_init(
		bRead, pCodec, pAry, sSemantic, sEncType, nItemBytes, 0, pAry->units, NULL
	);

	if(nRet != DAS_OKAY)
		return NULL;

	pThis->lItems[pThis->uCodecs] = nNumItems;
	pThis->uCodecs += 1;
	
	return pCodec;
}

DAS_API DasCodec* DasDs_addStringCodec(
    DasDs* pThis, const char* sAryId, const char* sSemantic, 
    const char* sEncType, int nItemTerm, ubyte uTerm, int nNumItems,
    bool bRead
){
	if(_DasDs_codecsMakeRoom(pThis) != DAS_OKAY)
		return NULL;

	/* Find the array with this ID */
	DasAry* pAry = DasDs_getAryById(pThis, sAryId);
	if(pAry == NULL){
		das_error(DASERR_DS, "An array with id '%s' was not found", sAryId);
		return NULL;
	}

	DasCodec* pCodec = pThis->lCodecs + pThis->uCodecs;

	DasErrCode nRet = DasCodec_init(
		bRead, pCodec, pAry, sSemantic, sEncType, nItemTerm, uTerm, pAry->units, NULL
	);

	if(nRet != DAS_OKAY)
		return NULL;

	pThis->lItems[pThis->uCodecs] = nNumItems;
	pThis->uCodecs += 1;
	
	return pCodec;
}

DasCodec* DasDs_addCodecFrom(
	DasDs* pThis, const char* sAryId, const DasCodec* pOther, int nNumItems,
	bool bRead
){
	if(_DasDs_codecsMakeRoom(pThis) != DAS_OKAY)
		return NULL;

	/* Find the array with this ID */
	const char* _sFindAry = sAryId;
	if((_sFindAry == NULL)||(_sFindAry[0] == '\0'))
		_sFindAry = DasAry_id(pOther->pAry);

	/* Look for the array in this dataset, not the other one */
	DasAry* pAry = DasDs_getAryById(pThis, _sFindAry);
	if(pAry == NULL){
		das_error(DASERR_DS, "An array with id '%s' was not found", _sFindAry);
		return NULL;
	}
	
	DasCodec* pDest = pThis->lCodecs + pThis->uCodecs;

	/* TODO: Using internal knowledge of DasCodec here, rework with external
	         functions only! */
	memcpy(pDest, pOther, sizeof(DasCodec));
	DasCodec_postBlit(pDest, pAry);  /* inc's the pAry reference internally */
	if(DasCodec_isReader(pDest) != bRead)
		DasCodec_update(bRead, pDest, NULL, 0, '\0', NULL, NULL);

	pThis->lItems[pThis->uCodecs] = nNumItems;
	pThis->uCodecs += 1;
	
	return pDest;
}

int DasDs_recBytes(const DasDs* pThis)
{
	int nBytesPerPkt = 0;
	for(size_t u = 0; u < pThis->uCodecs; ++u){
		int nValsExpect = DasDs_pktItems(pThis, u);

		if(nValsExpect < 1) /* Return -1 to indicate variable length packets */
			return -1;

		int16_t nValSz = DasDs_getCodec(pThis, u)->nBufValSz;
		nBytesPerPkt += nValSz*nValsExpect;
	}
	return nBytesPerPkt;
}

DasCodec* DasDs_getCodecFor(
	const DasDs* pThis, const char* sAryId, int* pItems
){
	size_t u;
	const DasAry* pFind = NULL;
	for(u = 0; u < pThis->uArrays; ++u){
		if(strcmp(pThis->lArrays[u]->sId, sAryId) == 0){
			pFind = pThis->lArrays[u];
			break;
		}
	}
	if(pFind == NULL){
		das_error(DASERR_DS, "No array with ID %s present in this dataset", sAryId);
		return NULL;
	}

	DasCodec* pCodec = NULL;
	for( u = 0; u < pThis->uCodecs; u++){
		pCodec = &(pThis->lCodecs[u]);
		if(pCodec->pAry == pFind){
			*pItems = pThis->lItems[u];
			return pCodec;
		}
	}

	/* Some arrays don't have codecs, maybe because app code didn't 
	   create one yet, or it's a set of header only values */
	daslog_debug_v(
		"No codec for array '%s' in dataset '%s (%s)', must be a header only array", 
		sAryId, pThis->sId, pThis->sGroupId
	);
	return NULL;
}

/* ************************************************************************* */

char* DasDs_toStr(const DasDs* pThis, char* sBuf, int nLen)
{
	char sDimBuf[1024] = {'\0'};
	int nWritten = 0;  /* Not necessarily the actual number of bytes written
	                    * but good enough to know if we should exit due to
	                    * running out of buffer space */
	char* pWrite = sBuf;
	const char* pRead = NULL;
	
	nWritten = snprintf(pWrite, nLen - 1, 
		"Dataset: '%s' from group '%s'",
		pThis->sId, pThis->sGroupId
	);
	nLen -= nWritten;
	pWrite += nWritten;
	
	if(nLen < 4) return sBuf;
	
	ptrdiff_t aShape[DASIDX_MAX];
	DasDs_shape(pThis, aShape);
	
	char* pSubWrite = das_shape_prnRng(aShape, pThis->nRank, pThis->nRank, pWrite, nLen);
	nLen -= (pSubWrite - pWrite);
	pWrite = pSubWrite;
	if(nLen < 20) return sBuf;
	
	*pWrite = '\n'; ++pWrite; --nLen;
	/*nWritten = snprintf(pWrite, nLen - 1, " | contains ...\n");
	pWrite += nWritten; nLen -= nWritten;*/
	
	pSubWrite = DasDesc_info((DasDesc*)pThis, pWrite, nLen, "   ");
	nLen -= (pSubWrite - pWrite);
	pWrite = pSubWrite;

	/* *pWrite = '\n'; ++pWrite; --nLen; */
	nWritten = snprintf(pWrite, nLen-1, "\n   ");
	pWrite += nWritten; nLen -= nWritten;
	
	/* Do data first... */
	for(uint32_t u = 0; u < pThis->uDims; ++u){
		
		if(pThis->lDims[u]->dtype != DASDIM_DATA) continue;
		
		pRead = DasDim_toStr(pThis->lDims[u], sDimBuf, 1023);

		/* If we don't have enough room to fit this variables description 
		   quit now and return NULL.  Upstream can use this to allocate
		   more space */
		if(nLen < (int)(strlen(pRead) * 1.1))
			return NULL;

		while((nLen > 8)&&(*pRead != '\0')){
			if(*pRead == '\n'){ 
				*pWrite = *pRead; ++pWrite;
				*pWrite = ' ';    ++pWrite; 
				*pWrite = ' ';    ++pWrite;
				*pWrite = ' ';    ++pWrite; nLen -= 4;
			}
			else{
				*pWrite = *pRead; ++pWrite; --nLen;
			}
			++pRead;
		}
		nWritten = snprintf(pWrite, nLen-1, "\n   ");
		pWrite += nWritten; nLen -= nWritten;
	}
	if(nLen < 5) return sBuf;
	
	/*
	if((pWrite > sBuf+3)&&(*(pWrite - 3) = ' ')){ 
		pWrite -= 3;
		nLen += 3;
	}
	*/
	
	/*nWritten = snprintf(pWrite, nLen- 1, "\n   ");
	nLen -= nWritten;
	pWrite += nWritten;
	if(nLen < 4) return sBuf;*/
	
	/* Now the coordinates... */
	for(size_t u = 0; u < pThis->uDims; ++u){
		
		if(pThis->lDims[u]->dtype != DASDIM_COORD) continue;
		
		pRead = DasDim_toStr(pThis->lDims[u], sDimBuf, 1023);

		/* If we don't have enough room to fit this variables description 
		   quit now and return NULL.  Upstream can use this to allocate
		   more space */
		if(nLen < (int)(strlen(pRead) * 1.1))
			return NULL;
		
		while((nLen > 8)&&(*pRead != '\0')){
			if(*pRead == '\n'){ 
				*pWrite = *pRead; ++pWrite;
				*pWrite = ' '; ++pWrite; 
				*pWrite = ' '; ++pWrite;
				*pWrite = ' '; ++pWrite; nLen -= 4;
			}
			else{
				*pWrite = *pRead; ++pWrite; --nLen;
			}
			++pRead;
		}
		nWritten = snprintf(pWrite, nLen-1, "\n   ");
		pWrite += nWritten; nLen -= nWritten;
	}
	
	/* Finally the arrays */
	/** TODO: add array info printing */
	
	return sBuf;
}

/* ************************************************************************* */
/* Construction, Destruction, Clearing */

void del_DasDs(DasDs* pThis){
	size_t u;
	
	/* Delete the vars first so that their functions can decrement the
	 * array reference counts */
	for(u = 0; u < pThis->uDims; ++u)
		del_DasDim(pThis->lDims[u]);

	/* If I had to go large on dims, free that */
	if(pThis->lDims != pThis->aDims)
		free(pThis->lDims);

	/* Each codec holds a reference on its array (DasCodec_init / addCodecFrom),
	   plus maybe an overflow buffer and codec-extension state.  Release them all;
	   this must cover the small-vector case too, only the heap backing store below
	   is conditionally freed. */
	for(u = 0; u < pThis->uCodecs; ++u)
		DasCodec_deInit(pThis->lCodecs + u);

	/* If I had to go large on codecs, free those */
	if(pThis->uCodecs >= DASDS_LOC_ENC_SZ){
		assert(pThis->lCodecs != pThis->aCodecs);
		assert(pThis->lItems != pThis->aItems);
		assert(pThis->lCodecs != NULL);
		assert(pThis->lItems != NULL);
		free(pThis->lCodecs);
		free(pThis->lItems);
	}
	
	/* Now drop the reference count on our arrays */
	for(u = 0; u < pThis->uArrays; ++u)
		dec_DasAry(pThis->lArrays[u]);

	/* If I had to go large on arrays, free that */
	if(pThis->lArrays != pThis->aArrays)
		free(pThis->lArrays);

	DasDesc_freeProps(&(pThis->base));
	free(pThis);
}

size_t DasDs_clearRagged0(DasDs* pThis)
{
	size_t uBytesCleared = 0;
	DasDim* pDim = NULL;
	DasVar* pVar = NULL;
	DasAry* pAry = NULL;

	for(size_t d = 0; d < pThis->uDims; ++d){
		pDim = pThis->lDims[d];
		for(size_t v = 0; v < pDim->uVars; ++v){
			pVar = pDim->aVars[v];
			if(!DasVar_degenerate(pVar, 0)){
				if( (pAry = DasVar_getArray(pVar)) != NULL){
					uBytesCleared += DasAry_clear(pAry) * DasAry_valSize(pAry);
				}
			}
		}
	}

	return uBytesCleared;
}

DasDs* new_DasDs(
	const char* sId, const char* sGroupId, int nRank
){
	if(!das_assert_valid_id(sId)) return NULL;
	if((sGroupId != NULL) && (!das_assert_valid_id(sGroupId))) return NULL;
	if(nRank < 1){
		das_error(DASERR_DS, "Datasets below rank 1 are not supported");
		return NULL;
	}
	if(nRank > DASIDX_MAX ){
		das_error(DASERR_DS, "Datasets above rank %d are not currently "
		           "supported, but can be if needed.", DASERR_DS);
		return NULL;
	}

	DasDs* pThis = (DasDs*)calloc(1, sizeof(DasDs));
	if(pThis == NULL){
		das_error(DASERR_DS, "Couldn't allocate %zu bytes for a new dataset", sizeof(DasDs));
		return NULL;
	}

	DasDesc_init((DasDesc*)pThis, DATASET);
	
	/* Make sure not to break in middle of utf-8 sequence, start to replace
	 * all the strncpy's in das2 libs with this for safety */
	u8_strncpy(pThis->sId, sId, 64);
	if(sGroupId != NULL) u8_strncpy(pThis->sGroupId, sGroupId, 64);

	pThis->nRank = nRank;
	
	/* All datasets start out as dynamic (or else how would you build one? */
	pThis->_dynamic = true;

	pThis->uSzCodecs = DASDS_LOC_ENC_SZ; /* build in, small vec array */
	pThis->uSzArrays = DASDS_LOC_ARY_SZ;
	pThis->uSzDims   = DASDS_LOC_DIM_SZ;

	/* Point at my internal storage to start */
	pThis->lCodecs = pThis->aCodecs;
	pThis->lItems = pThis->aItems;
	pThis->lArrays = pThis->aArrays;
	pThis->lDims = pThis->aDims;

	return pThis;
}

/* ************************************************************************* */
/* Shallow-on-arrays deep-on-structure copy, the stream-filter work horse */

DasDs* DasDs_copy(const DasDs* pThis)
{
	/* A non-const handle is needed for the by-index dim accessor, but nothing
	   below mutates the source. */
	DasDs* pSrc = (DasDs*)pThis;

	DasDs* pOut = new_DasDs(pThis->sId, pThis->sGroupId, pThis->nRank);
	if(pOut == NULL)
		return NULL;

	DasDesc_copyIn((DasDesc*)pOut, (const DasDesc*)pThis);
	pOut->_dynamic = pThis->_dynamic;

	/* 1. Share the storage arrays.  DasDs_addAry() steals a reference, so bump
	      the count first to leave the source dataset's hold intact.  We do NOT
	      copy the array contents: reads that fill the source array are seen by
	      both datasets, which is the whole point of a copy used as a filter
	      stage (see the header doc). */
	for(size_t u = 0; u < pThis->uArrays; ++u){
		inc_DasAry(pThis->lArrays[u]);
		if(DasDs_addAry(pOut, pThis->lArrays[u]) != DAS_OKAY)
			goto FAIL;
	}

	/* 2. Copy the dimensions and their variables.  copy_DasVar() shares (and
	      ref-counts) the same backing array a variable already points at, so the
	      copied variables read from the shared storage added above. */
	for(int iType = DASDIM_COORD; iType <= DASDIM_DATA; ++iType){
		size_t uDims = DasDs_numDims(pThis, iType);
		for(size_t uD = 0; uD < uDims; ++uD){
			DasDim* pDimIn = DasDs_getDimByIdx(pSrc, uD, iType);

			DasDim* pDimOut = DasDs_makeDim(
				pOut, iType, DasDim_dim(pDimIn), DasDim_id(pDimIn)
			);
			if(pDimOut == NULL)
				goto FAIL;

			DasDesc_copyIn((DasDesc*)pDimOut, (DasDesc*)pDimIn);

			if(DasDim_getFrame(pDimIn) != NULL)
				DasDim_setFrame(pDimOut, DasDim_getFrame(pDimIn));

			DasDim_setAxes(pDimOut, pDimIn);

			size_t uVars = DasDim_numVars(pDimIn);
			for(size_t uV = 0; uV < uVars; ++uV){
				DasVar* pVarOut = copy_DasVar(DasDim_getVarByIdx(pDimIn, uV));
				if(pVarOut == NULL)
					goto FAIL;

				if(!DasDim_addVar(pDimOut, DasDim_getRoleByIdx(pDimIn, uV), pVarOut)){
					dec_DasVar(pVarOut);
					goto FAIL;
				}
			}
		}
	}

	/* 3. Copy the codecs.  addCodecFrom() re-points each one at the matching
	      array in pOut (found by id) and preserves its read/write mode, so the
	      copy serializes exactly as the original would. */
	for(size_t u = 0; u < pThis->uCodecs; ++u){
		const DasCodec* pCdIn = DasDs_getCodec(pThis, u);
		if(DasDs_addCodecFrom(
			pOut, DasAry_id(pCdIn->pAry), pCdIn, pThis->lItems[u],
			DasCodec_isReader(pCdIn)
		) == NULL)
			goto FAIL;
	}

	return pOut;

FAIL:
	del_DasDs(pOut);
	return NULL;
}

DasErrCode DasDs_replaceAry(DasDs* pThis, const char* sOldId, DasAry* pNew)
{
	/* Locate the slot holding the array to retire */
	size_t uSlot = 0;
	DasAry* pOld = NULL;
	for(uSlot = 0; uSlot < pThis->uArrays; ++uSlot){
		if(strcmp(DasAry_id(pThis->lArrays[uSlot]), sOldId) == 0){
			pOld = pThis->lArrays[uSlot];
			break;
		}
	}
	if(pOld == NULL)
		return das_error(DASERR_DS, "No array with id '%s' to replace", sOldId);

	/* Re-point every array variable that reads from the old array.  This drops
	   each variable's reference on pOld and takes one on pNew. */
	for(int iType = DASDIM_COORD; iType <= DASDIM_DATA; ++iType){
		size_t uDims = DasDs_numDims(pThis, iType);
		for(size_t uD = 0; uD < uDims; ++uD){
			DasDim* pDim = DasDs_getDimByIdx(pThis, uD, iType);
			size_t uVars = DasDim_numVars(pDim);
			for(size_t uV = 0; uV < uVars; ++uV){
				DasVar* pVar = DasDim_getVarByIdx(pDim, uV);
				if((DasVar_type(pVar) == D2V_ARRAY) && (DasVar_getArray(pVar) == pOld)){
					if(!DasVarAry_setArray(pVar, pNew))
						return DASERR_DS;
				}
			}
		}
	}

	/* Swap the array list entry: take the list's reference on pNew, drop its
	   reference on pOld.  Any codec still aimed at pOld must be re-initialized by
	   the caller; a codec holds its own ref on its array (DasCodec_init, released
	   in del_DasDs), so pOld survives here until that codec is re-init'd. */
	inc_DasAry(pNew);
	pThis->lArrays[uSlot] = pNew;
	dec_DasAry(pOld);

	return DAS_OKAY;
}

/* ************************************************************************* */
/* Sending/Reading dataset decriptions to XML */

/* Non API function declairation */

/* From dimension.c */
DasErrCode DasDim_encode(DasDim* pThis, DasBuf* pBuf);

/* new_DasDs_xml() is in dataset_hdr3.c to keep this file from getting so big */


/** Encode the descriptive header for a dataset 
 * 
 * This will encode a description of a das dastaset suitable for reloading
 * via new_DasDs_xml().  All variables that are degenerate in the
 * first index will have thier data written into the header itself.  All
 * other variables will have <packet> elements which specify how data 
 * will be written when dasds_encode_data() is called.
 * 
 * @param pDs A pointer to a dataset object
 * @param pBuf A pointer to a DasBuf object to recieve the serialized header.
 * @returns DAS_OKAY if the operation succeeded, a positive error value
 *        otherwise
 */
DasErrCode DasDs_encode(DasDs* pThis, DasBuf* pBuf)
{
	DasErrCode nRet = DAS_OKAY;
	ptrdiff_t aShape[DASIDX_MAX] = DASIDX_INIT_UNUSED;
	int nRank = DasDs_shape(pThis, aShape);

	DasBuf_printf(pBuf, "\n<dataset name=\"%s\" rank=\"%d\" index=\"", DasDs_group(pThis), nRank);
	for(int i = 0; i < nRank; ++i){
		if(i > 0) DasBuf_puts(pBuf, ";");
		if((i==0)||(aShape[i] == DASIDX_RAGGED)) DasBuf_puts(pBuf, "*");
		else DasBuf_printf(pBuf, "%td", aShape[i]);
	}
	DasBuf_puts(pBuf, "\" >\n");

	if( (nRet = DasDesc_encode3((DasDesc*)pThis, pBuf, "  ")) != 0)
		return nRet;

	/* Now for the coordinates then data */
	for(int iType = DASDIM_COORD; iType <= DASDIM_DATA; ++iType){
		size_t uDims = DasDs_numDims(pThis, iType);                   /* All Dimensions */
		for(size_t uD = 0; uD < uDims; ++uD){
			DasDim* pDim = (DasDim*)DasDs_getDimByIdx(pThis, uD, iType); /* All Variables */
			if( (nRet = DasDim_encode(pDim, pBuf)) != 0)
				return nRet;
		}
	}
	
	DasBuf_puts(pBuf, "</dataset>\n");
	pThis->bSentHdr = true;
	return DAS_OKAY;
}

/* ************************************************************************** */
/* Decoding a data packet, using a dataset created by one of the constructors */

/* Parse a ragged run tag "[<idx>|N]" at the front of pBuf.  A variable item-count
   run that is not the last block in a packet is bounded by this tag (the count of
   the external ragged index it closes).  Returns N (>= 0) and sets *pnTagBytes to
   the tag's byte width, or a negative das_error code on a malformed tag.  The idx
   letter is informational for a single ragged level, so it is read past but not
   yet cross-checked against the index position. */
static int _read_run_tag(const ubyte* pBuf, int nBufLen, int* pnTagBytes)
{
	if((nBufLen < 4) || (pBuf[0] != '['))
		return -1 * das_error(DASERR_SERIAL,
			"Expected a ragged run tag '[<idx>|N]' to bound a variable item-count run"
		);

	int iBar = -1, iClose = -1;
	int nScan = (nBufLen < 32) ? nBufLen : 32;   /* a count tag is short */
	for(int i = 1; i < nScan; ++i){
		if(pBuf[i] == '|'){ if(iBar < 0) iBar = i; }
		else if(pBuf[i] == ']'){ iClose = i; break; }
	}
	if((iBar < 1) || (iClose <= iBar))
		return -1 * das_error(DASERR_SERIAL, "Malformed ragged run tag");

	int nCount = 0;
	for(int i = iBar + 1; i < iClose; ++i){
		if((pBuf[i] < '0') || (pBuf[i] > '9'))
			return -1 * das_error(DASERR_SERIAL, "Non-digit in ragged run tag count");
		nCount = nCount * 10 + (pBuf[i] - '0');
	}
	*pnTagBytes = iClose + 1;
	return nCount;
}

/* Recursively decode a variable-count ragged run bounded by "[idx|N]" run tags.
   aRagDim[0..nLvls-1] are the array's ragged dimension positions, outermost first.
   The run tag at the INNERMOST ragged level counts ATOMS (handed to the codec, which
   also auto-rolls any fixed inner dim such as a vector's components); at an OUTER
   level it counts the number of CHILD runs to recurse into.  DasAry_markEnd closes
   each level's dimension as its run finishes, so nested raggedness of any depth (up
   to DASIDX_MAX) is handled -- e.g. a rank-3 ionogram is [j|Nj] then, per pulse,
   [k|Nk]<atoms>.  Returns bytes consumed from pRaw, or a negative das_error code. */
static int _decode_ragged_run(
	DasCodec* pCodec, const ubyte* pRaw, int nBufLen,
	const int* aRagDim, int iLvl, int nLvls
){
	int nTagBytes = 0;
	int nCount = _read_run_tag(pRaw, nBufLen, &nTagBytes);
	if(nCount < 0)
		return nCount;   /* already a negative das_error code */

	/* A zero-length run means an element with no children.  DasAry -- and the whole
	   DasDs/DasVar/iterator/builder/utility/das2py chain built on it -- has assumed
	   >= 1 element per sub-run for its entire life (the one exception, cubic-slice
	   auto-fill, is a read-side view, not a stored state).  Allowing it is an
	   invariant change needing a full audit, so until then fail loud rather than
	   strand an unmaterializable element.  See notes/notimp_inventory.md and the
	   test/notimp_zero_subrun.d3b probe. */
	if(nCount == 0)
		return -1 * das_error(DASERR_NOTIMP,
			"Zero-length ragged run (count 0 at array index %d) is not supported for "
			"array %s: sub-runs must have >= 1 element (see notes/notimp_inventory.md)",
			aRagDim[iLvl], DasAry_id(pCodec->pAry)
		);

	int nUsed = nTagBytes;

	if(iLvl == (nLvls - 1)){
		/* Innermost ragged level: nCount atoms straight to the codec. */
		int nValsRead = 0;
		int nSubLen = nBufLen - nUsed;
		int nUnread = DasCodec_decode(pCodec, pRaw + nUsed, nSubLen, nCount, &nValsRead);
		if(nUnread < 0)
			return nUnread;   /* negative das_error from the codec */
		if(nCount != nValsRead)
			return -1 * das_error(DASERR_SERIAL,
				"Expected %d atoms in a ragged run for array %s but decoded %d",
				nCount, DasAry_id(pCodec->pAry), nValsRead
			);
		nUsed += (nSubLen - nUnread);
	}
	else{
		/* Outer ragged level: nCount child runs, each its own [idx|N]. */
		for(int n = 0; n < nCount; ++n){
			int nSub = _decode_ragged_run(
				pCodec, pRaw + nUsed, nBufLen - nUsed, aRagDim, iLvl + 1, nLvls
			);
			if(nSub < 0)
				return nSub;
			nUsed += nSub;
		}
	}

	DasAry_markEnd(pCodec->pAry, aRagDim[iLvl]);
	return nUsed;
}

/* Decode data from a buffer into dataset memory
 * 
 * @param pDs A pointer to a das dataset object that has defined encoders
 *        and arrays.  This can be created via new_DasDs_xml()
 * 
 * @param pBuf The buffer to read.  Reading will start with the read point
 *        and will run until the end of the packet.  Since reading from the
 *        buffer advances the read point, the caller can determine how many
 *        bytes were read.
 * 
 * @returns DAS_OKAY if reading was successful or a error code if not.
 */

DasErrCode DasDs_decodeData(DasDs* pThis, DasBuf* pBuf)
{
	if(DasDs_numCodecs(pThis) == 0){
		return das_error(DASERR_SERIAL, 
			"No decoders are defined for dataset %02d in group %s", DasDs_id(pThis), DasDs_group(pThis)
		);
	}

	int nUnReadBytes = 0;
	int nSzEncs = (int)DasDs_numCodecs(pThis);
	for(int i = 0; i < nSzEncs; ++i){
		DasCodec* pCodec = DasDs_getCodec(pThis, i);
		size_t uBufLen = 0;
		const ubyte* pRaw = DasBuf_direct(pBuf, &uBufLen);

		if(pRaw == NULL){
			return das_error(DASERR_SERIAL,
				"Packet buffer is empty, there are no bytes to decode"
			);
		}
		if(uBufLen > 0x7fffffff)
			return das_error(DASERR_SERIAL, "Packet buffer > signed integer half range, what are you doing?");
		int nBufLen = (int)uBufLen;
		
		/* Encoder returns the number of bytes it didn't read.  Assuming we are
		   doing things right, the last return from the last encoder call will 
		   be 0, AKA nothing will be unread in the packet.
		 */
		int nValsRead = 0;
		int nValsExpect = DasDs_pktItems(pThis, i);

		/* A variable item-count run (numItems="*") needs its ragged external index
		   located so we know which index markEnd closes. Can be skipped if the 
		   last run in the packet. The run-tag counts ATOMS (like numItems); the codec
		   reads them flat and the array's fixed inner dims auto-roll them into vectors
		   etc. (a run of 3 three-vectors is [j|9]), so no component scaling here. 
		   There is an asymetry in what counts as an atom.  A string or a blob is
		   an ATOM that has an internal index.  For vtGeoVect it has an internal
		   index, but it's components are not Atoms. Confusing, but this was 
		   done because the "atoms" in the vector case are individually parsed,
		   not like blobs and strings. */
		/* NOT the last block: variable-count runs are bounded by "[idx|N]" run tags,
		   nested for multi-level raggedness.  Recurse over the ragged dims --
		   _decode_ragged_run does the codec calls and every markEnd -- then advance
		   the read point and move on to the next codec. */
		if((nValsExpect < 1) && (i < (nSzEncs - 1))){
			ptrdiff_t aShape[DASIDX_MAX];
			int nAryRank = DasAry_shape(pCodec->pAry, aShape);
			int aRagDim[DASIDX_MAX];
			int nLvls = 0;
			for(int d = 1; d < nAryRank; ++d){ if(aShape[d] < 0) aRagDim[nLvls++] = d; }
			if(nLvls < 1)
				return das_error(DASERR_SERIAL,
					"Variable item count for array %s but found no ragged index",
					DasAry_id(pCodec->pAry)
				);

			int nUsed = _decode_ragged_run(pCodec, pRaw, nBufLen, aRagDim, 0, nLvls);
			if(nUsed < 0)
				return -1 * nUsed;

			size_t uCurOffset = DasBuf_readOffset(pBuf);
			DasBuf_setReadOffset(pBuf, uCurOffset + nUsed);
			nUnReadBytes = nBufLen - nUsed;
			continue;
		}

		/* Fixed count, or a variable count as the LAST block (end-of-packet bounds
		   it): read straight through and mark the one ragged index afterward.
		   (Multi-level ragged as a last block would need a read-to-buffer-end walk;
		   no fixture yet, so it is not handled here.) */
		const ubyte* pDecBuf = pRaw;
		int nDecLen = nBufLen;
		int nCodecExpect = nValsExpect;
		int iRagged = -1;

		if(nValsExpect < 1){
			ptrdiff_t aShape[DASIDX_MAX];
			int nAryRank = DasAry_shape(pCodec->pAry, aShape);
			for(int d = 1; d < nAryRank; ++d){ if(aShape[d] < 0){ iRagged = d; break; } }
			if(iRagged < 0)
				return das_error(DASERR_SERIAL,
					"Variable item count for array %s but found no ragged index",
					DasAry_id(pCodec->pAry)
				);
		}

		nUnReadBytes = DasCodec_decode(pCodec, pDecBuf, nDecLen, nCodecExpect, &nValsRead);
		if(nUnReadBytes < 0)
			return -1 * nUnReadBytes;

		if(nCodecExpect > 0){
			if(nCodecExpect != nValsRead)
				return das_error(DASERR_SERIAL,
					"Expected to parse %d values from a packet for array %s in dataset %s "
					"but received %d.", nCodecExpect, DasAry_id(pCodec->pAry), DasDs_id(pThis),
					nValsRead
				);
		}

		/* Since we used direct (aka raw) access, we have to manually adjust the
		   read point of the buffer */
		int nReadBytes = nBufLen - nUnReadBytes;
		assert(nReadBytes > -1);
		size_t uCurOffset = DasBuf_readOffset(pBuf);
		DasBuf_setReadOffset(pBuf, uCurOffset + nReadBytes);

		/* A variable item-count run closes one ragged record here: mark the end of
		   the RAGGED index (iRagged), not blindly the last array index -- for a
		   vector the last index is the fixed component axis, which auto-rolls; the
		   ragged external index is the one that must be told.  Fixed inner shapes
		   auto-roll once full; ragged ones (uShape 0) must be told.
		   (Per-value internal markEnd for ragged strings is the codec's DASENC_WRAP
		   job -- a different axis; reconcile when variable-count strings land.) */
		if((nValsExpect < 1) && (iRagged >= 1))
			DasAry_markEnd(pCodec->pAry, iRagged);
	}

	if(nUnReadBytes > 0){
		daslog_warn_v("%d unread bytes at the end of the packet for dataset %s", 
			nUnReadBytes, DasDs_id(pThis)
		);
	}

	return DAS_OKAY;
}

/* ************************************************************************* */
/* Encode data for a dataset */

/* Encode one major index's worth of packet data for a dataset 
 * 
 * This function can be call repeatedly in a loop, with a negative return
 * value indicating the normal completion of the loop.
 * 
 * @param pThis A pointer to a dataset object
 * 
 * @param pBuf A pointer to a DasBuf object to recieve the serialized data
 *        for one increment of the major index of the dataset.
 * 
 * @returns DAS_OKAY to indicate data was serialized for the given index.
 *          A positive error code if there was a problem sending data.
 */

DasErrCode DasDs_encodeData(DasDs* pThis, DasBuf* pBuf, ptrdiff_t iIdx0)
{
	
	if(DasDs_numCodecs(pThis) == 0){
		return das_error(DASERR_SERIAL, 
			"No decoders are defined for dataset %02d in group %s", DasDs_id(pThis), DasDs_group(pThis)
		);
	}

	int nSzEncs = (int)DasDs_numCodecs(pThis);
	int nValsExpect = 0;
	int nValsWrote = 0;
	bool bLast = false;
	for(int i = 0; i < nSzEncs; ++i){
		DasCodec* pCodec = DasDs_getCodec(pThis, i);
		
		/* Encoder returns the number of values it wrote */
		nValsWrote = 0;
		nValsExpect = DasDs_pktItems(pThis, i);

		if((nValsExpect < 1)&&(i < (nSzEncs - 1)))
			return das_error(DASERR_NOTIMP, 
				"To handle parsing ragged non-text arrays that's not at the end of "
				"a packet, add searching for binary sentinals to DasCodec_decode"
			);
		

		bLast = (i == ((nSzEncs) - 1)); /* Last encoder can be and uses \n for val sep */
		/* Pass the NAMED flag, not the raw bool: DASENC_PKT_LAST is 0x02, so passing
		   bLast==1 set bit 0x01 and the last-item newline never fired -- that's why
		   das3_text packets ran together instead of one-record-per-line (das2_ascii's
		   readable behavior).  The header <values> path already passes the flag. */
		nValsWrote = DasCodec_encode(
			pCodec, pBuf, DIM1_AT(iIdx0), nValsExpect, bLast ? DASENC_PKT_LAST : 0
		);
		if(nValsWrote < 0){
			return -1*nValsWrote;  /* negative indicates error condition */
		}
		
		if(nValsExpect > 0){
			if(nValsExpect != nValsWrote)
				return das_error(DASERR_SERIAL, 
					"Expected to write %d values to a packet for array %s in dataset %s "
					"but wrote %d instead.", nValsExpect, DasAry_id(pCodec->pAry), 
					DasDs_id(pThis), nValsWrote
				);
		}
		else{
			/* Even for variable number of items, we expect to write something */
			if(nValsWrote == 0){
				return das_error(DASERR_SERIAL, "No values written for array %s in dataset %s",
					DasAry_id(pCodec->pAry), DasDs_id(pThis)
				);
			}	
		}
	}

	return DAS_OKAY;
}