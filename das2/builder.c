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
#include <string.h>
#include <assert.h>
#include <ctype.h>

#include "log.h"
#include "io.h"
#include "variable.h"
#include "dataset.h"
#include "builder.h"
#include "serial2.h"

/* ************************************************************************** */
/* Helpers */

/* NOTO: pPd can be NULL if we're getting an already formed dataset! */

size_t _DasDsBldr_addPair(DasDsBldr* pThis, PktDesc* pPd, DasDs* pCd)
{
	struct ds_pd_set* pNewSet = NULL;
	if(pThis->uSzPairs < (pThis->uValidPairs + 1)){
		pThis->uSzPairs *= 2;
		if(pThis->uSzPairs < 16) pThis->uSzPairs = 16;
		pNewSet = (struct ds_pd_set*) calloc(pThis->uSzPairs, sizeof(struct ds_pd_set));

		if(pThis->uValidPairs > 0)
			memcpy(pNewSet, pThis->lPairs, pThis->uValidPairs * sizeof(struct ds_pd_set));
		pThis->lPairs = pNewSet;
	}

	/* If we're matching up packet descriptors to datasets, save a copy of the
	   packet descriptor, including it's planes.  This is used to see if this
	   packet descriptor has been seen before. 
	*/

	if(pPd != NULL){
		PktDesc* pNewPd = new_PktDesc();
		PktDesc_copyPlanes(pNewPd, pPd);
		pThis->lPairs[pThis->uValidPairs].pPd = pNewPd;
	}
	else{
		pThis->lPairs[pThis->uValidPairs].pPd = NULL;	
	}
	pThis->lPairs[pThis->uValidPairs].pDs = pCd;
	pThis->uValidPairs += 1;
	return (pThis->uValidPairs - 1);
}

/* ************************************************************************** */
/* On new stream  */

DasErrCode DasDsBldr_onStreamDesc(DasStream* pSd, void* vpUd)
{
	DasDsBldr* pThis = (DasDsBldr*)vpUd;

	DasDesc_copyIn((DasDesc*) pThis->pStream, (DasDesc*)pSd);

	// Copy in the frame descriptors
	int nFrames = DasStream_getNumFrames(pSd);
	for(int i = 0; i < nFrames; ++i){
		const DasFrame* pFrame = DasStream_getFrame(pSd, i);
		DasStream_addFrame(pThis->pStream, copy_DasFrame(pFrame));
	}

	return DAS_OKAY;
}

/* ************************************************************************* */
/* On new packet type */

int _DasDsBldr_hasContainer(DasDsBldr* pThis, PktDesc* pPd)
{
	/* Find a container whose packet descriptor matches this one.  This
	 * has to have the same number of planes in the same places with the
	 * the same ytag values. */
	PlaneDesc* pPlane = NULL;
	PlaneDesc* pPlTest = NULL;

	PktDesc* pPdTest = NULL;
	ptrdiff_t iMatch = -1;
	bool bSame = false;

	size_t u,v,w;

	for(u = 0; u < pThis->uValidPairs; ++u){
		pPdTest = pThis->lPairs[u].pPd;
		if(pPdTest == NULL)  /* Happens when stream contains datasets */
			continue;

		/* Check number of planes */
		if(pPd->uPlanes != pPdTest->uPlanes) continue;

		bSame = true;
		for(v = 0; v < pPd->uPlanes; ++v){
			pPlane = pPd->planes[v];
			pPlTest = pPdTest->planes[v];

			/* Check plane type */
			if(pPlane->planeType != pPlTest->planeType){bSame = false; break;}

			/* Check item length */
			if(PlaneDesc_getNItems(pPlane) != PlaneDesc_getNItems(pPlTest)){
				bSame = false; break;
			}

			/* Check units */
			if( strcmp(pPlane->units, pPlTest->units) != 0){
				bSame = false; break;
			}

			/* Check names (careful, either of these may be null) */
			if(pPlane->sName && !(pPlTest->sName)) {bSame = false; break;}
			if(!(pPlane->sName) && pPlTest->sName) {bSame = false; break;}
			if(pPlane->sName && pPlTest->sName &&
			   strcmp(pPlane->sName, pPlTest->sName) != 0) {
				bSame = false; break;
			}

			/* For YScans, check the yTags */
			if(pPlane->planeType == YScan){
				if(pPlane->ytag_spec != pPlTest->ytag_spec){bSame=false; break;}
				switch(pPlane->ytag_spec){
					case ytags_list:
						for(w=0;w<pPlane->uItems;++w){
							if(pPlane->pYTags[w] != pPlTest->pYTags[w]){
								bSame=false; break;
							}
						}
						break;
					case ytags_series:
						if((pPlane->yTagInter != pPlTest->yTagInter)||
						   (pPlane->yTagMin != pPlTest->yTagMin) ||
							(pPlane->yTagMax != pPlTest->yTagMax)) bSame = false;

						break;
					default:
						break;
				}
				if(!bSame) break;
			}
		}

		if(bSame){ iMatch = u; break;}
	}

	return iMatch;   /* They are going to have to make a new one */
}

/* Loop through the existing pairs.  If you come across one that has the same
 * number and types of planes, and each plane has the same name as mine then
 * re-use it's group ID.  If not, take my first data plane name as the group
 * id.  */
char* _DasDsBldr_getExistingGroup(
	DasDsBldr* pThis, PktDesc* pPd, char* sGroupId, size_t uLen
){

	if(uLen < 2){das_error(DASERR_BLDR, "Really?"); 	return NULL; }

	PlaneDesc* pPlane = NULL;
	PlaneDesc* pPlTest = NULL;

	PktDesc* pPdTest = NULL;
	ptrdiff_t iMatch = -1;
	bool bSimilar = false;

	for(size_t u = 0; u < pThis->uValidPairs; ++u){
		pPdTest = pThis->lPairs[u].pPd;
		if(pPdTest == NULL) continue;  /* Happens when dataset descriptors in the stream */

		/* Check number of planes */
		if(pPd->uPlanes != pPdTest->uPlanes) continue;

		bSimilar = true;
		for(size_t v = 0; v < pPd->uPlanes; ++v){
			pPlane = pPd->planes[v];
			pPlTest = pPdTest->planes[v];

			/* Check plane type */
			if(pPlane->planeType != pPlTest->planeType){bSimilar = false; break;}

			/* Check units */
			if( strcmp(pPlane->units, pPlTest->units) != 0){
				bSimilar = false; break;
			}

			/* Check names (careful, either of these may be null) */
			if(pPlane->sName && !(pPlTest->sName)) {bSimilar = false; break;}
			if(!(pPlane->sName) && pPlTest->sName) {bSimilar = false; break;}
			if(pPlane->sName && pPlTest->sName &&
			   strcmp(pPlane->sName, pPlTest->sName) != 0) {
				bSimilar = false; break;
			}
		}

		if(bSimilar){ iMatch = u; break;}
	}

	if(iMatch != -1){
		strncpy(sGroupId, pThis->lPairs[iMatch].pDs->sGroupId, uLen-1);
		return sGroupId;
	}

	return NULL;   /* They are going to have to make something up */
}

/* Get the correlated dataset container that corresponds to the given packet
 * descriptor.  This uses duck typing.  If a packet descriptor has the same
 * number and type of planes, and each plane has the same name (and ytags if
 * applicable) then it is considered to describe the same data object.
 *
 * The index of the container is stored in the lDsMap array. */

DasErrCode DasDsBldr_onPktDesc(DasStream* pSd, PktDesc* pPd, void* vpUd)
{
	DasDsBldr* pThis = (DasDsBldr*)vpUd;

	/* The big question, are they defining a new dataset (in which case the
	 * old one needs to be kept, or are they starting an actual new one) */
	int iPktId = PktDesc_getId(pPd);
	int iPairIdx = -1;
	if(pThis->lDsMap[iPktId] != -1){
		if( (iPairIdx = _DasDsBldr_hasContainer(pThis, pPd)) != -1){
			/* Reuse old CorData */
			pThis->lDsMap[iPktId] = iPairIdx;
			return DAS_OKAY;
		}
	}

	char sGroupId[64] = {'\0'};
	const char* pGroup = _DasDsBldr_getExistingGroup(pThis, pPd, sGroupId, 64);

	DasDs* pCd = dasds_from_packet(pSd, pPd, pGroup, false);
	if(!pCd) return DASERR_BLDR;

	DasStream_addDesc(pThis->pStream, (DasDesc*)pCd, iPktId);
	size_t uIdx = _DasDsBldr_addPair(pThis, pPd, pCd);
	pThis->lDsMap[iPktId] = uIdx;

	return DAS_OKAY;
}

/* ************************************************************************* */

DasErrCode DasDsBldr_onDataSet(DasStream* pSd, int iPktId, DasDs* pDs, void* vpUd)
{
	DasDsBldr* pThis = (DasDsBldr*)vpUd;

	/* Not much to do here, we already have a valid dataset, just add it to
	   the list, but don't associate a packet descriptor */
	if( pThis->lDsMap[iPktId] != -1)
		return das_error(DASERR_BLDR, "Packet reuse not supported for DasDs descriptors");
	
	size_t uIdx = _DasDsBldr_addPair(pThis, NULL, pDs);

	DasStream_shadowPktDesc(pThis->pStream, (DasDesc*)pDs, iPktId);

	pThis->lDsMap[iPktId] = uIdx;

	return DAS_OKAY;
}

/* ************************************************************************* */

DasErrCode DasDsBldr_onPktData(PktDesc* pPd, void* vpUd)
{
	DasDsBldr* pThis = (DasDsBldr*)vpUd;
	int nPktId = PktDesc_getId(pPd);

	struct ds_pd_set set = pThis->lPairs[ pThis->lDsMap[nPktId] ];
	DasDs* pDs = set.pDs;

	/* Loop through all the arrays in the dataset object and add values */
	PlaneDesc* pPlane = NULL;
	DasAry* pAry = NULL;
	for(size_t u = 0; u < pDs->uArrays; ++u){
		pAry = pDs->lArrays[u];
		if(pAry->nSrcPktId != nPktId){
			assert(pAry->nSrcPktId < 1);
			continue;
		}
		pPlane = PktDesc_getPlane(pPd, pAry->uStartItem);
		assert(pAry->uItems == pPlane->uItems);
		DasAry_append(pAry, (const ubyte*) PlaneDesc_getValues(pPlane), pAry->uItems);
	}

	return DAS_OKAY;
}

/* ************************************************************************* */

DasErrCode DasDsBldr_onDsData(DasStream* pSd, int iPktId, DasDs* pDs, void* vpUd)
{
	/* DasIO automatically calls dasds_decode_data which calls 
	   DasCodec_decode which appends data, so nothing to do here */

	return DAS_OKAY;
}


/* ************************************************************************* */

DasErrCode DasDsBldr_onComment(OobComment* pSc, void* vpUd)
{
	/* Builder* pThis = (Builder*)vpUd; */
	return DAS_OKAY;
}


/* ************************************************************************* */

DasErrCode DasDsBldr_onException(OobExcept* pSe, void* vpUd)
{
	/* Builder* pThis = (Builder*)vpUd; */
	return DAS_OKAY;
}

DasErrCode DasDsBldr_onClose(DasStream* pSd, void* vpUd)
{
	/* Go through all the datasets and turn-off mutability, they can always 
	 * turn on mutability again if need be.
	 * This will cache the dataset size. */
	DasDsBldr* pThis = (DasDsBldr*)vpUd;
	
	for(int i = 0; i < pThis->uValidPairs; ++i)
		DasDs_setMutable(pThis->lPairs[i].pDs, false);

	/* very important, do not let the stream descriptor delete our datasets
	   take ownership of them. */
	int nPktId = 0;
	DasDesc* pDesc = NULL;
	while((pDesc = DasStream_nextDesc(pSd, &nPktId)) != NULL){
 		if(DasDesc_type(pDesc) == DATASET){

 			/* Notice the "this" pointer is ours not the original owner below */
 			DasErrCode nRet = DasStream_takePktDesc(pThis->pStream, pDesc, 0);
 			if(nRet != DAS_OKAY)
 				return nRet;
 		}
 	}
	
	return DAS_OKAY;
}

/* ************************************************************************** */
/* Constructor */

DasDsBldr* new_DasDsBldr(void)
{
	DasDsBldr* pThis = (DasDsBldr*) calloc(1, sizeof(DasDsBldr));
	pThis->pStream = new_DasStream();
	strncpy(pThis->pStream->version, DAS_30_STREAM_VER, STREAMDESC_VER_SZ - 1);

	pThis->base.userData = pThis;
	pThis->base.streamDescHandler = DasDsBldr_onStreamDesc;
	pThis->base.pktDescHandler    = DasDsBldr_onPktDesc;
	pThis->base.dsDescHandler     = DasDsBldr_onDataSet;
	pThis->base.pktDataHandler    = DasDsBldr_onPktData;
	pThis->base.dsDataHandler     = DasDsBldr_onDsData;
	pThis->base.exceptionHandler  = DasDsBldr_onException;
	pThis->base.closeHandler      = DasDsBldr_onClose;
	pThis->base.commentHandler    = DasDsBldr_onComment;
	pThis->_released = false;

	for(int i = 0; i<MAX_PKTIDS; ++i) pThis->lDsMap[i] = -1;

	/* Make room for 64 dataset pointers, can always expand if needed */
	pThis->uValidPairs = 0;
	pThis->uSzPairs = 64;
	pThis->lPairs = (struct ds_pd_set*)calloc(
		pThis->uSzPairs, sizeof(struct ds_pd_set)
	);

	return pThis;
}

void DasDsBldr_release(DasDsBldr* pThis){ 
	pThis->_released = true;
	
}

DasDs** DasDsBldr_getDataSets(DasDsBldr* pThis, size_t* pLen){
	if(pThis->uValidPairs == 0) return NULL;

	*pLen = pThis->uValidPairs;
	DasDs** pRet = (DasDs**)calloc(pThis->uValidPairs, sizeof(void*));
	for(int i = 0; i < pThis->uValidPairs; ++i) pRet[i] = pThis->lPairs[i].pDs;
	return pRet;
}

DasStream* DasDsBldr_getStream(DasDsBldr* pThis){
	return pThis->pStream;
}

DasDesc* DasDsBldr_getProps(DasDsBldr* pThis){
   /* Hope they call release if they want to keep these */
	return (DasDesc*) pThis->pStream;
}

void del_DasDsBldr(DasDsBldr* pThis){

	if(! pThis->_released){
		/* The don't want it, so delete my owned stream and everything it has */
		del_DasStream(pThis->pStream);
	}

	/* Always delete the temporary packet descriptors I made for the pair matching */
	for(size_t u = 0; u < pThis->uValidPairs; ++u){
		if(pThis->lPairs[u].pPd != NULL)
			del_PktDesc(pThis->lPairs[u].pPd);
	}

   free(pThis->lPairs);
	free(pThis);
}


/* ************************************************************************* */
/* Convenience functions */

DasDs** build_from_stdin(const char* sProgName, size_t* pSets, DasDesc** ppGlobal)
{
	daslog_info("Reading das stream from standard input");

	DasIO* pIn = new_DasIO_cfile(sProgName, stdin, "r");
	DasDsBldr* pBldr = new_DasDsBldr();
	DasIO_addProcessor(pIn, (StreamHandler*)pBldr);

	if(DasIO_readAll(pIn) != 0){
		daslog_info("Error processing standard input");
		del_DasIO(pIn);
		del_DasDsBldr(pBldr);
		return NULL;
	}
	del_DasIO(pIn);
	DasDs** lCorDs = DasDsBldr_getDataSets(pBldr, pSets);
	*ppGlobal = DasDsBldr_getProps(pBldr);
	DasDsBldr_release(pBldr);
	daslog_info_v("%zu Correlated Datasets retrieved from stdin", *pSets);
	return lCorDs;
}

DasStream* stream_from_stdin(const char* sProgName)
{
	daslog_info("Reading das stream from standard input");

	DasIO* pIn = new_DasIO_cfile(sProgName, stdin, "r");
	DasDsBldr* pBldr = new_DasDsBldr();
	DasIO_addProcessor(pIn, (StreamHandler*)pBldr);

	if(DasIO_readAll(pIn) != 0){
		daslog_info("Error processing standard input");
		del_DasIO(pIn);
		del_DasDsBldr(pBldr);
		return NULL;
	}
	del_DasIO(pIn);
	DasStream* pStream = DasDsBldr_getStream(pBldr);
	DasDsBldr_release(pBldr);
	size_t nDatasets = DasStream_getNPktDesc(pStream);  /* Our stream only contains dataset objs */
	daslog_info_v("%zu Correlated Datasets retrieved from stdin", nDatasets);
	return pStream;
}


DasStream* stream_from_path(const char* sProg, const char* sFile)
{
	daslog_info_v("Reading %s\n", sFile);
	FILE* pFile = fopen(sFile, "rb");
	if(!pFile){
		daslog_error_v("Couldn't open %s", sFile);
		return NULL;
	}
	DasIO* pIn = new_DasIO_cfile(sProg, pFile, "r");
	DasIO_model(pIn, 3);
	DasDsBldr* pBldr = new_DasDsBldr();
	DasIO_addProcessor(pIn, (StreamHandler*)pBldr);

	if(DasIO_readAll(pIn) != 0){
		daslog_error_v("Couldn't process the contents of in %s\n", sFile);
		return NULL;
	}
	
	StreamDesc* pSd = DasDsBldr_getStream(pBldr);
	size_t nDs = DasStream_getNPktDesc(pSd);
	daslog_info_v("%zu Datasets retrieved from %s", nDs, sFile);

	del_DasIO(pIn); /* <-- This autocloses the file, prop. not a good idea */
	DasDsBldr_release(pBldr); /* detach stream object */
	del_DasDsBldr(pBldr);
	
	return pSd;
}
