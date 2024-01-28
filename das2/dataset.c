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
#ifndef _WIN32
#include <strings.h>
#else
#define strcasecmp _stricmp
#endif

#include "util.h"
#include "dataset.h"
#include "utf8.h"

/* ************************************************************************* */
/* Dataset Inspection Functions */

const char* DasDs_id(const DasDs* pThis){return pThis->sId;}

const char* DasDs_group(const DasDs* pThis){return pThis->sGroupId;}

int Dataset_rank(const DasDs* pThis){return pThis->nRank;}

size_t DasDs_numDims(const DasDs* pThis, enum dim_type vt){
	size_t uVars = 0;
	for(size_t u = 0; u < pThis->uDims; ++u){
		if(pThis->lDims[u]->dtype == vt) ++uVars;
	}
	return uVars;
}

const DasDim* DasDs_getDim(const DasDs* pThis, size_t idx, enum dim_type vt)
{
	size_t uTypeIdx = 0;
	for(size_t u = 0; u < pThis->uDims; ++u){
		if(pThis->lDims[u]->dtype == vt){
			if(uTypeIdx == idx) return pThis->lDims[u];
			else ++uTypeIdx;
		}
	}
	return NULL;
}

const DasDim* DasDs_getDimById(const DasDs* pThis, const char* sId)
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
				"%s of dataset %s, is rank %d, must be at most rank %d for consistancy", 
				pDim->sId, pThis->sId, nDimRank, pThis->nRank
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

/* ************************************************************************* */
/* Post-Construction sub-item addition */

bool DasDs_addAry(DasDs* pThis, DasAry* pAry)
{
	
	/* In python ABI language, this function steals a reference to the 
	 * array.  */
	
	if(pThis->uSzArrays < (pThis->uArrays + 1)){
		DasAry** pNew = NULL;
		size_t uNew = pThis->uSzArrays * 2;
		if(uNew < 16) uNew = 16;
		if( (pNew = (DasAry**)calloc(uNew, sizeof(void*))) == NULL) return false;


		if(pThis->uArrays > 0)
			memcpy(pNew, pThis->lArrays, pThis->uArrays * sizeof(void*));
		pThis->lArrays = pNew;
		pThis->uSzArrays = uNew;
	}
	pThis->lArrays[pThis->uArrays] = pAry;
	pThis->uArrays += 1;
	return true;
}


bool DasDs_addDim(DasDs* pThis, DasDim* pDim)
{
	/* Since function maps mask off any un-used indices and since
	 * Variables can have internal structure beyond those needed for
	 * correlation, and since vars can be completly static! I guess
	 * there is nothing to check here other than that all dependend
	 * variables in the span are also owened by this dataset and that
	 * all arrays are also owned by this dataset.
	 */
	size_t v = 0;
	
	if(pDim->dtype == DASDIM_UNK){
		das_error(DASERR_DS, "Can't add a dimension of type ANY to dataset %s", pThis->sId);
		return false;
	}
	
	/* Make sure that I don't already have a varible with this name */
	for(v = 0; v < pThis->uDims; ++v){
		if(strcmp(pThis->lDims[v]->sId, pDim->sId) == 0){
			das_error(
				DASERR_DS, "A dimension named %s already exists in dataset %s",
				pDim->sId, pThis->sId
			);
			return false;
		}
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
					"is not part of dataset %s", pDim->sId, pDim->aCoords[u]->sId, 
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
		if(uNew < 16) uNew = 16;
		DasDim** pNew = (DasDim**) calloc(uNew, sizeof(void*));
		if(pThis->uSzDims > 0)
			memcpy(pNew, pThis->lDims, pThis->uDims * sizeof(void*));
		pThis->lDims = pNew;
		pThis->uSzDims = uNew;
	}

	pThis->lDims[pThis->uDims] = pDim;
	pThis->uDims += 1;
	
	pDim->base.parent = &(pThis->base);

	return true;
}

DasDim* DasDs_makeDim(DasDs* pThis, enum dim_type dType, const char* sId)
{
	DasDim* pDim = new_DasDim(sId, dType, pThis->nRank);
	if(! DasDs_addDim(pThis, pDim)){
		del_DasDim(pDim);
		return NULL;
	}
	return pDim;
}

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
	
	char* pSubWrite;
	pSubWrite = das_shape_prnRng(aShape, pThis->nRank, pThis->nRank, pWrite, nLen);
	nLen -= pSubWrite - pWrite;
	pWrite = pSubWrite;
	if(nLen < 20) return sBuf;
	
	*pWrite = '\n'; ++pWrite; --nLen;
	/*nWritten = snprintf(pWrite, nLen - 1, " | contains ...\n");
	pWrite += nWritten; nLen -= nWritten;*/
	
	/* Larry wanted the properties printed as well */
	char sInfo[128] = {'\0'};
	size_t u = 0;
	const char* sName = NULL;
	const char* sType = NULL;
	const char* sVal = NULL;
	const DasDesc* pBase = (const DasDesc*)pThis;
	size_t uProps = DasDesc_length(pBase);
	for(u = 0; u < uProps; ++u){
		sName = DasDesc_getNameByIdx(pBase, u);
		sType = DasDesc_getTypeByIdx(pBase, u);
		sVal = DasDesc_getValByIdx(pBase, u);
		strncpy(sInfo, sVal, 48); sInfo[48] = '\0';
		nWritten = snprintf(pWrite, nLen - 1, "   Property: %s | %s | %s\n",
			                 sType, sName, sVal);
		pWrite += nWritten; nLen -= nWritten;
		if(nLen < 4) return sBuf;
	}
	/* *pWrite = '\n'; ++pWrite; --nLen; */
	nWritten = snprintf(pWrite, nLen-1, "\n   ");
	pWrite += nWritten; nLen -= nWritten;
	
	/* Do data first... */
	for(u = 0; u < pThis->uDims; ++u){
		
		if(pThis->lDims[u]->dtype != DASDIM_DATA) continue;
		
		pRead = DasDim_toStr(pThis->lDims[u], sDimBuf, 1023);
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
/* Construction, Destruction */

void del_DasDs(DasDs* pThis){
	size_t u;
	
	/* Delete the vars first so that their functions can decrement the
	 * array reference counts */
	if(pThis->lDims != NULL){
		for(u = 0; u < pThis->uDims; ++u)
			del_DasDim(pThis->lDims[u]);
		free(pThis->lDims);
	}
	
	/* Now drop the reference count on our arrays */
	if(pThis->lArrays != NULL){
		for(u = 0; u < pThis->uArrays; ++u)
			dec_DasAry(pThis->lArrays[u]);
	}

	DasDesc_freeProps(&(pThis->base));
	free(pThis);
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
	if(nRank > 16){
		das_error(DASERR_DS, "Datasets above rank 16 are not currently "
		           "supported, but can be if needed.");
		return NULL;
	}

	DasDs* pThis = (DasDs*)calloc(1, sizeof(DasDs));

	DasDesc_init((DasDesc*)pThis, DATASET);
	
	/* Make sure not to break in middle of utf-8 sequence, start to replace
	 * all the strncpy's in das2 libs with this for safety */
	u8_strncpy(pThis->sId, sId, 64);
	if(sGroupId != NULL) u8_strncpy(pThis->sGroupId, sGroupId, 64);

	pThis->nRank = nRank;
	
	/* All datasets start out as dynamic (or else how would you build one? */
	pThis->_dynamic = true;

	return pThis;
}

/* ************************************************************************* */
/* XML Serialization */

#define ELT_NONE 0

#define ELT_DS   1  // Opens on <dataset> -> becomes DasDs
                    //  * on open: create DasDs, set DasDs ptr, set Prop Dest ptr
                                      
#define ELT_PDIM 2  // Opens on <coord> or <data> -> becomes DasDim
                    //  * on open: create DasDim, set cur DasDim ptr, take prop dest ptr
                    //  * on close: set prop dest ptr back to the ds.

#define ELT_PSET 3  // Opens on <properties> (no actions)
                    //  (could parse for das2 style props, but don't those are
                    //   invalide XML and should not be encouraged)

#define ELT_PROP 4  // Opens on <p> -> becomes DasProp
                    //  * on open: save attribs to parser vars
                    //  * on data: append char_data buffer
                    //  * on close: add prop to current prop dest

#define ELT_VAR  5  // Opens on <scalar>, <vector> -> becomes DasVar
                    //  * on open: save attribs to parser vars
                    //  * on close: unset current var ptr

// When opening a vector, set the number of internal dimensions as 1.
// then 

#define ELT_VSEQ 6  // Atomic on <sequence> -> part of DasVar
                    //  * on open: 

#define ELT_VVAL 7
#define ELT_VPKT 8

typedef struct ds_xml_parser {
	int eltCur;
	DasDs* pDs;
	DasDim* pDim;
	DasVar* pVar;


} ds_xml_parser_t;

DAS_API DasDs* new_DasDs_xml(DasBuf* pBuf, DasDesc* pParent, int nPktId)
{
	

	return NULL;
}