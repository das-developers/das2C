/* Copyright (C) 2015-2026 Chris Piker <chris-piker@uiowa.edu>
 *               2004-2006 Jeremy Faden <jeremy-faden@uiowa.edu>
 *
 * This file is part of das2C, the Core Das C Library.
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
 * version 2.1 along with das2C; if not, see <http://www.gnu.org/licenses/>. 
 */


#define _POSIX_C_SOURCE 200112L

#include <assert.h>
#include <string.h>
#include <time.h>
#include <limits.h>
#include <stdio.h>
#ifdef __unix
#include <sys/types.h>
#include <unistd.h>
#endif
#include <ctype.h>
#ifndef _WIN32
#include <strings.h>
#else
#define strcasecmp _stricmp
#endif

#include <expat.h>

#include "stream.h"
#include "log.h"
#include "info_p.h"
#include "stream_p.h"
#include "packet_p.h"
#include "datset_p.h"
#include "prop_p.h"

static void _del_DasStream(DasDesc* pDesc);  /* del_DasDesc() target */

/* ************************************************************************** */
/* Version and type identification */

das_stream_ver DasStream_getVer(const DasStream* pThis)
{
	if(strcmp(pThis->version, das_stream_ver_toStr(STREAM_VER_2_2)) == 0)
		return STREAM_VER_2_2;
	if(strcmp(pThis->version, das_stream_ver_toStr(STREAM_VER_3_0)) == 0)
		return STREAM_VER_3_0;
	return STREAM_VER_UNK;
}

das_stream_type DasStream_getType(const DasStream* pThis)
{
	/* No attribute is how das 2.2 spells "basic", and encode3 fills it in the
	   same way, so the empty string is BASIC rather than UNKNOWN */
	if(pThis->type[0] == '\0')
		return STREAM_TYPE_BASIC;
	if(strcmp(pThis->type, das_stream_type_toStr(STREAM_TYPE_BASIC)) == 0)
		return STREAM_TYPE_BASIC;
	if(strcmp(pThis->type, das_stream_type_toStr(STREAM_TYPE_DOC)) == 0)
		return STREAM_TYPE_DOC;
	return STREAM_TYPE_UNK;
}

/* Only what DasStream_getVer() recognizes, so a set always reads back */
DasErrCode DasStream_setVer(DasStream* pThis, das_stream_ver nVer)
{
	if((nVer != STREAM_VER_2_2)&&(nVer != STREAM_VER_3_0))
		return das_error(DASERR_STREAM, "Stream version %s can't be set, "
			"only %s and %s are supported", das_stream_ver_toStr(nVer),
			das_stream_ver_toStr(STREAM_VER_2_2), das_stream_ver_toStr(STREAM_VER_3_0)
		);
	memset(pThis->version, 0, STREAMDESC_VER_SZ);
	strncpy(pThis->version, das_stream_ver_toStr(nVer), STREAMDESC_VER_SZ - 1);
	return DAS_OKAY;
}

DasErrCode DasStream_setType(DasStream* pThis, das_stream_type nType)
{
	if((nType != STREAM_TYPE_BASIC)&&(nType != STREAM_TYPE_DOC))
		return das_error(DASERR_STREAM, "Unknown stream type %d", (int)nType);
	memset(pThis->type, 0, STREAMDESC_TYPE_SZ);
	strncpy(pThis->type, das_stream_type_toStr(nType), STREAMDESC_TYPE_SZ - 1);
	return DAS_OKAY;
}

bool DasStream_getEmbedAsBytes(const DasStream* pThis){ return pThis->bEmbedAsBytes; }

/* ************************************************************************** */
/* Construction */

DasStream* new_DasStream() 
{
	DasStream* pThis;
    
	pThis = (DasStream*) calloc(1, sizeof( DasStream ) );
	DasDesc_init((DasDesc*)pThis, DASDESC_STREAM, _del_DasStream);
	 
	strncpy(pThis->compression, "none", STREAMDESC_CMP_SZ - 1);

	strncpy(pThis->version, das_stream_ver_toStr(STREAM_VER_2_2), STREAMDESC_VER_SZ - 1);
   
    return pThis;
}

/* Deep copy a stream descriptor */
DasStream* DasStream_copy(const DasStream* pThis) 
{
	/* Since this is a deep copy do it by explicit assignment, less likely to
	 * make a mistake that way */
	DasStream* pOut = new_DasStream();
	strncpy(pOut->compression, pThis->compression, STREAMDESC_CMP_SZ - 1);
	strncpy(pOut->type, pThis->type, STREAMDESC_TYPE_SZ - 1);
	strncpy(pOut->version, pThis->version, STREAMDESC_VER_SZ - 1);
	
	DasDesc_copyIn((DasDesc*)pOut, (DasDesc*)pThis);


	return pOut;
}

/* The two lists live inline until they outgrow DASSTREAM_LOC_DESC */
static DasDesc** _DasStream_descs(const DasStream* pThis)
{
	return pThis->pDescHeap ? pThis->pDescHeap : (DasDesc**)pThis->aDescLoc;
}

static uint16_t* _DasStream_ids(const DasStream* pThis)
{
	return pThis->pIdHeap ? pThis->pIdHeap : (uint16_t*)pThis->aIdLoc;
}

static void _del_DasStream(DasDesc* pDesc){
	DasStream* pThis = (DasStream*)pDesc;
	DasDesc_freeProps(&(pThis->base));

	/* Only delete the items I own!  */
	DasDesc** pDescs = _DasStream_descs(pThis);
	for(size_t u = 0; u < pThis->uDesc; u++){
		if(pDescs[u]->parent == (DasDesc*) pThis)
			del_DasDesc(pDescs[u]);
	}
	if(pThis->pDescHeap) free(pThis->pDescHeap);
	if(pThis->pIdHeap) free(pThis->pIdHeap);
	free(pThis);
}

/* ************************************************************************** */
/* The first line of DasStream_info(), and all of DasStream_toStr() */
static int _DasStream_catTitle(const DasStream* pThis, char* sBuf, size_t uLen, int n)
{
	char sComp[32] = {'\0'};
	if(strcmp(pThis->compression, "none") != 0)
		snprintf(sComp, 31, "(%s compression)", pThis->compression);

	/* das2 streams have no type token, das3 streams always do */
	return _prn_catf(sBuf, uLen, n, "Stream: %s v%s%s",
		(pThis->type[0] != '\0') ? pThis->type : "das", pThis->version, sComp
	);
}

char* DasStream_toStr(const DasStream* pThis, char* sBuf, size_t uLen)
{
	if(uLen > 0) sBuf[0] = '\0';
	return _prn_fit(sBuf, uLen, _DasStream_catTitle(pThis, sBuf, uLen, 0));
}

int DasStream_info(const DasStream* pThis, char* sBuf, size_t uLen)
{
	if(uLen > 0) sBuf[0] = '\0';

	int n = _DasStream_catTitle(pThis, sBuf, uLen, 0);
	n = _prn_catf(sBuf, uLen, n, "\n");

	if(DasDesc_length((DasDesc*)pThis) > 0)
		n += DasDesc_info(
			(DasDesc*)pThis, _prn_at(sBuf, uLen, n), _prn_rem(uLen, n), "   "
		);
	else
		n = _prn_catf(sBuf, uLen, n, "   (no global properties)\n");

	return _prn_catf(sBuf, uLen, n, "\n");
}

/* ************************************************************************** */
/* Adding/Removing detail */

void DasStream_addStdProps(DasStream* pThis)
{
	time_t tCur;
	struct tm* pCur = NULL;	
	char sTime[128] = {'\0'};
	/* char * hostname; */
        
	time(&tCur);
	pCur = gmtime(&tCur);
	snprintf(sTime, 127, "%04d-%02d-%02dT%02d:%02d:%02d", pCur->tm_year+1900, pCur->tm_mon,
			   pCur->tm_mday, pCur->tm_hour, pCur->tm_min, pCur->tm_sec);

	DasDesc_setStr((DasDesc*)pThis, "creationTime", sTime);
#ifdef __unix
	DasDesc_setInt( (DasDesc*)pThis, "pid", getpid() );
#endif
    
	/* This is not in the posix C99 standard, going to exclude it for now -cwp
	hostname= (char*)malloc(256);
	gethostname( hostname, 256 );
	setProperty( (Descriptor)pThis, "hostname", hostname );
	*/
}

void DasStream_setMonotonic(DasStream* pThis, bool isMonotonic )
{
	if(isMonotonic)
		DasDesc_setBool( (DasDesc*)pThis, "monotonicXTags", true );
	else
		DasDesc_setBool( (DasDesc*)pThis, "monotonicXTags", false );
}

/* ************************************************************************** */
/* Child list plumbing.  Linear scans throughout: streams rarely carry more
   than 10 descriptors, and the ID scan reads a packed uint16 array, 32 IDs
   per cache line, without touching the descriptors themselves. */

static bool _DasStream_idOkay(int nPktId)
{
	return (nPktId > 0)&&(nPktId < DAS3_MAX_PKTIDS);
}

/* Position of a descriptor in the list, -1 if not attached.  Compares
   pointers only, so a stale pointer is safe to ask about. */
static ptrdiff_t _DasStream_index(const DasStream* pThis, const DasDesc* pDesc)
{
	DasDesc** pDescs = _DasStream_descs(pThis);
	for(size_t u = 0; u < pThis->uDesc; u++)
		if(pDescs[u] == pDesc) return (ptrdiff_t)u;
	return -1;
}

/* Position of the active descriptor for an ID, -1 if none.  The active one
   is the last attached, hence the backwards scan. */
static ptrdiff_t _DasStream_active(const DasStream* pThis, int nPktId)
{
	const uint16_t* pIds = _DasStream_ids(pThis);
	for(size_t u = pThis->uDesc; u > 0; u--)
		if(pIds[u-1] == nPktId) return (ptrdiff_t)(u-1);
	return -1;
}

/* Make room for one more entry, moving to the heap when the inline arrays
   fill.  Rare, so plain malloc and copy rather than two reallocs that could
   fail separately. */
static DasErrCode _DasStream_reserve(DasStream* pThis)
{
	size_t uCap = (pThis->uDescCap == 0) ? DASSTREAM_LOC_DESC : pThis->uDescCap;
	if(pThis->uDesc < uCap) return DAS_OKAY;

	size_t uNew = uCap * 2;
	DasDesc** pNewDescs = (DasDesc**)malloc(uNew * sizeof(DasDesc*));
	uint16_t* pNewIds = (uint16_t*)malloc(uNew * sizeof(uint16_t));
	if((pNewDescs == NULL)||(pNewIds == NULL)){
		free(pNewDescs);
		free(pNewIds);
		return das_error(DASERR_STREAM, "Couldn't grow the descriptor list to %zu", uNew);
	}
	memcpy(pNewDescs, _DasStream_descs(pThis), pThis->uDesc * sizeof(DasDesc*));
	memcpy(pNewIds, _DasStream_ids(pThis), pThis->uDesc * sizeof(uint16_t));
	if(pThis->pDescHeap) free(pThis->pDescHeap);
	if(pThis->pIdHeap) free(pThis->pIdHeap);
	pThis->pDescHeap = pNewDescs;
	pThis->pIdHeap = pNewIds;
	pThis->uDescCap = uNew;
	return DAS_OKAY;
}

/* Attach with no checks, callers have done them */
static DasErrCode _DasStream_append(DasStream* pThis, DasDesc* pDesc, int nPktId)
{
	DasErrCode nRet = _DasStream_reserve(pThis);
	if(nRet != DAS_OKAY) return nRet;

	_DasStream_descs(pThis)[pThis->uDesc] = pDesc;
	_DasStream_ids(pThis)[pThis->uDesc] = (uint16_t)nPktId;
	pThis->uDesc += 1;
	pThis->uLastHit = 0;
	pDesc->parent = (DasDesc*)pThis;
	return DAS_OKAY;
}

/* Detach the entry at a position, keeping attach order for the rest */
static void _DasStream_removeAt(DasStream* pThis, size_t uIdx)
{
	DasDesc** pDescs = _DasStream_descs(pThis);
	uint16_t* pIds = _DasStream_ids(pThis);
	size_t uAfter = pThis->uDesc - uIdx - 1;
	memmove(pDescs + uIdx, pDescs + uIdx + 1, uAfter * sizeof(DasDesc*));
	memmove(pIds + uIdx, pIds + uIdx + 1, uAfter * sizeof(uint16_t));
	pThis->uDesc -= 1;
	pThis->uLastHit = 0;
}

size_t DasStream_numDesc(const DasStream* pThis, das_desc_type_e dtype)
{
	DasDesc** pDescs = _DasStream_descs(pThis);
	size_t nRet = 0;
	for(size_t u = 0; u < pThis->uDesc; u++)
		if((dtype == DASDESC_UNK)||(pDescs[u]->type == dtype)) nRet++;
	return nRet;
}

/* das2 packets get the lowest ID that no descriptor uses at all, active or
   not, since a DasPkt never shares its ID */
static int DasStream_nextPktId(DasStream* pThis)
{
	for(int i = 1; i < DAS2_MAX_PKTIDS; ++i){ /* 00 is reserved for stream descriptor */
		const uint16_t* pIds = _DasStream_ids(pThis);
		bool bUsed = false;
		for(size_t u = 0; u < pThis->uDesc; u++)
			if(pIds[u] == i){ bUsed = true; break; }
		if(!bUsed) return i;
	}
	return -1 * das_error(DASERR_STREAM, "Ran out of Packet IDs only 99 allowed!" );
}

static bool _DasStream_idUsed(const DasStream* pThis, int nPktId)
{
	return (_DasStream_active(pThis, nPktId) >= 0);
}

DasPkt* DasStream_createPkt(
	DasStream* pThis, DasEnc* pXEncoder, das_units xUnits 
){
	DasPkt* pPkt;

	/* Before allocating anything, a full stream leaves the encoder with the
	   caller */
	int nId = DasStream_nextPktId(pThis);
	if(nId < 1) return NULL;

	pPkt= new_DasPkt();
	 
	DasPlane* pX = new_DasPlane(DASPLANE_X, "", pXEncoder, xUnits);
	DasPkt_addPlane(pPkt, pX);
	if(_DasStream_append(pThis, (DasDesc*)pPkt, nId) != DAS_OKAY){
		del_DasDesc((DasDesc*)pPkt);
		return NULL;
	}
	return pPkt;
}

DasErrCode DasStream_freeDesc(DasStream* pThis, DasDesc* pDesc)
{
	ptrdiff_t iIdx = _DasStream_index(pThis, pDesc);
	if(iIdx < 0)
		return das_error(DASERR_STREAM, "%s: descriptor is not attached to this stream",
		                 __func__);

	_DasStream_removeAt(pThis, (size_t)iIdx);

	/* Shadowed descriptors belong to another stream, as in _del_DasStream() */
	if(pDesc->parent != (DasDesc*)pThis)
		return DAS_OKAY;

	del_DasDesc(pDesc);
	return DAS_OKAY;
}

DasPkt* DasStream_getPkt(const DasStream* pThis, int nPacketId)
{
	DasDesc* pDesc = DasStream_getDesc(pThis, nPacketId);
	return (pDesc == NULL)||(pDesc->type != DASDESC_PKT) ? NULL : (DasPkt*)pDesc;
}

DasDesc* DasStream_getDesc(const DasStream* pThis, int nPacketId)
{
	if(!_DasStream_idOkay(nPacketId)){
		das_error(DASERR_STREAM, "Illegal packet ID %d, expected 1 to %d",
			nPacketId, DAS3_MAX_PKTIDS - 1
		);
		return NULL;
	}
	ptrdiff_t iIdx = _DasStream_active(pThis, nPacketId);
	return (iIdx < 0) ? NULL : _DasStream_descs(pThis)[iIdx];
}

DasDesc* DasStream_findDesc(DasStream* pThis, int nPktId)
{
	/* Data packets usually arrive in runs of one ID, so check last time's
	   answer first.  Any attach or removal clears it, so it is still active. */
	if(pThis->uLastHit > 0){
		size_t uHit = pThis->uLastHit - 1;
		if(_DasStream_ids(pThis)[uHit] == nPktId)
			return _DasStream_descs(pThis)[uHit];
	}

	ptrdiff_t iIdx = _DasStream_active(pThis, nPktId);
	if(iIdx < 0) return NULL;
	pThis->uLastHit = (size_t)iIdx + 1;
	return _DasStream_descs(pThis)[iIdx];
}

int DasStream_getPktId(const DasStream* pThis, const DasDesc* pDesc)
{
	ptrdiff_t iIdx = _DasStream_index(pThis, pDesc);
	return (iIdx < 0) ? -1 : (int)(_DasStream_ids(pThis)[iIdx]);
}

bool DasStream_isDescActive(const DasStream* pThis, const DasDesc* pDesc)
{
	ptrdiff_t iIdx = _DasStream_index(pThis, pDesc);
	if(iIdx < 0) return false;
	return _DasStream_active(pThis, _DasStream_ids(pThis)[iIdx]) == iIdx;
}

DasDesc* DasStream_nextDesc(
	const DasStream* pThis, const DasDesc* pPrev, das_desc_type_e dtype
){
	size_t uBeg = 0;
	if(pPrev != NULL){
		ptrdiff_t iIdx = _DasStream_index(pThis, pPrev);
		if(iIdx < 0){
			das_error(DASERR_STREAM, "Previous descriptor is not attached to this "
				"stream, fetch the next one before removing the current one");
			return NULL;
		}
		uBeg = (size_t)iIdx + 1;
	}

	DasDesc** pDescs = _DasStream_descs(pThis);
	for(size_t u = uBeg; u < pThis->uDesc; u++)
		if((dtype == DASDESC_UNK)||(pDescs[u]->type == dtype))
			return pDescs[u];
	return NULL;
}

DasErrCode DasStream_cacheShapes(DasStream* pThis, bool bCache)
{
	if(pThis == NULL) return DAS_OKAY;

	DasDesc* pDesc = NULL;
	while((pDesc = DasStream_nextDesc(pThis, pDesc, DASDESC_DS)) != NULL)
		DasDs_cacheShape((DasDs*)pDesc, bCache);
	return DAS_OKAY;
}


void DasStream_addCmdLineProp(DasStream* pThis, int argc, char * argv[] ) 
{
	/* Save up to 1023 bytes of command line info */
	char sCmd[1024] = {'\0'};
	int nSpace = 1023;
	int nLen = 0;
   char* pWrite = sCmd;
	int i = 0;
	while((i < argc) && (nSpace > 0)){
		
		if(i > 0){
			*pWrite = ' '; ++pWrite, --nSpace;
			if(nSpace <= 0) break;
		}
		
		nLen = strlen(argv[i]);
		strncpy(pWrite, argv[i], nSpace);
		pWrite += nLen; nSpace -= nLen;
		
		++i;
	}
   
   DasDesc_setStr( (DasDesc*)pThis, "commandLine", sCmd);
}

/* ************************************************************************* */
/* Copying stream objects */

DasPkt* DasStream_clonePkt(DasStream* pThis, const DasPkt* pPdIn)
{
	DasPkt* pPdOut;

	int id = DasStream_nextPktId( pThis );
	if(id < 1) return NULL;

	pPdOut = new_DasPkt();
	pPdOut->base.parent = (DasDesc*)pThis;   /* owned, _del_DasStream frees it */
	 
	DasDesc_copyIn((DasDesc*)pPdOut, (DasDesc*)pPdIn);
	 
	if(_DasStream_append(pThis, (DasDesc*)pPdOut, id) != DAS_OKAY){
		del_DasDesc((DasDesc*)pPdOut);
		return NULL;
	}
	 
	DasPkt_copyPlanes(pPdOut, pPdIn);  /* Realloc's the data buffer */

	return pPdOut;
}

DasPkt* DasStream_clonePktById(
	DasStream* pThis, const DasStream* pOther, int nPacketId
){
	DasPkt* pIn, *pOut;

	pIn = DasStream_getPkt(pOther, nPacketId);
	if(pIn == NULL) return NULL;   /* Empty slot, or a dataset not a packet */

	if(_DasStream_idUsed(pThis, nPacketId)){
		das_error(DASERR_STREAM, "ERROR: Stream descriptor already has a packet "
		                "descriptor with id %d", nPacketId); 
		return NULL;
	}
	
	pOut = new_DasPkt();
	pOut->base.parent = (DasDesc*)pThis;
	
	DasDesc_copyIn((DasDesc*)pOut, (DasDesc*)pIn);
	
	if(_DasStream_append(pThis, (DasDesc*)pOut, nPacketId) != DAS_OKAY){
		del_DasDesc((DasDesc*)pOut);
		return NULL;
	}
	
	DasPkt_copyPlanes(pOut, pIn); /* Realloc's the data buffer */
		
	/* Do not copy user data */
	
	return pOut;
}

DasErrCode DasStream_addDesc(DasStream* pThis, DasDesc* pDesc, int nPktId)
{
	/* Only accept either das2 packet descriptors or das3 datasets */
	if((pDesc->type != DASDESC_PKT)&&(pDesc->type != DASDESC_DS))
		return das_error(DASERR_STREAM, "Unexpected packet desciptor type");

	if((pDesc->parent != NULL)&&(pDesc->parent != (DasDesc*)pThis)){

		/* Hint to random developer: If you are here because you wanted to copy 
		 * another stream's packet descriptor onto this stream use one of 
		 * DasStream_clonePkt() or DasStream_clonePktById() instead. 
		 */
		return das_error(DASERR_STREAM, "Packet Descriptor already belongs to different "
				                "stream");
	}
	
	/* The same object under a second ID */
	ptrdiff_t iIdx = _DasStream_index(pThis, pDesc);
	if(iIdx >= 0)
		return das_error(DASERR_STREAM,
			"Descriptor is already attached to this stream as ID %02d",
			(int)(_DasStream_ids(pThis)[iIdx])
		);

	if(!_DasStream_idOkay(nPktId))
		return das_error(DASERR_STREAM, "Illegal packet id %d, expected 1 to %d",
			nPktId, DAS3_MAX_PKTIDS - 1
		);

	/* A das2 header identical to one already live under another ID is rejected.  
	   An early version of the das2 shim (much of it survives in
	   das2_from_tagged_das1.c) handed out a fresh packet ID for every header it
	   emitted, including re-sends of one already defined. Each re-sent header
	   is a separate object, so only a content compare catches it.

	   Note that redefining a packet ID never gets here, io.c frees the old
	   descriptor first.  Datasets are not compared, DasPkt_equalFormat() only
	   understands das2 packets. Maybe some das3 stream will commit this same
	   sin someday, but it hasn't happened so far.  -cwp 2026-10-21 */
	DasDesc** pDescs = _DasStream_descs(pThis);
	if((pDesc->type == DASDESC_PKT)&&(pDesc->parent == (DasDesc*)pThis)){
		for(size_t u = 0; u < pThis->uDesc; ++u)
			if(pDescs[u]->type == DASDESC_PKT)
				if(DasPkt_equalFormat((DasPkt*)pDesc, (DasPkt*)(pDescs[u])))
					return das_error(DASERR_STREAM,
						"Packet header for ID %02d repeats the header for ID %02d",
						nPktId, (int)(_DasStream_ids(pThis)[u])
					);
	}

	/* das2 packets hold one record, so there is nothing to keep by stacking
	   them, and datasets can't stack on top of one either.  Datasets stack on
	   datasets, the newest becomes active. */
	ptrdiff_t iActive = _DasStream_active(pThis, nPktId);
	if(iActive >= 0){
		if(pDesc->type == DASDESC_PKT)
			return das_error(DASERR_STREAM, "Packet ID %02d is already in use", nPktId);
		if(pDescs[iActive]->type == DASDESC_PKT)
			return das_error(DASERR_STREAM,
				"Packet ID %02d is held by a das2 packet, a dataset can't share it", nPktId
			);
	}

	return _DasStream_append(pThis, pDesc, nPktId);
}

DasErrCode DasStream_rmDesc(DasStream* pThis, DasDesc* pDesc)
{
	if((pDesc->type != DASDESC_PKT)&&(pDesc->type != DASDESC_DS))
		return das_error(DASERR_STREAM, "Unexpected packet desciptor type");

	if((pDesc->parent != (DasDesc*)pThis))
		return das_error(DASERR_STREAM, "Descriptor dosen't belong to this stream");

	ptrdiff_t iIdx = _DasStream_index(pThis, pDesc);
	if(iIdx < 0)
		return das_error(DASERR_STREAM, "Descriptor is not part of this stream");

	_DasStream_removeAt(pThis, (size_t)iIdx);
	pDesc->parent = NULL;   /* Detach both ways */
	return DAS_OKAY;
}

/* ************************************************************************* */
/* Frame wrappers */

/* ************************************************************************* */
/* Context entries */

/* ************************************************************************* */
/* Serializing */

/* Declaration of important non-API functions */

/* from desc.c */


#define _UNIT_BUF_SZ 127
#define _NAME_BUF_SZ 63
#define _TYPE_BUF_SZ 23

typedef struct parse_stream_desc{
	DasStream* pStream;
	DasErrCode nRet;
	bool bInProp;
	bool bV3Okay;
	char sPropUnits[_UNIT_BUF_SZ+1];
	char sPropName[_NAME_BUF_SZ+1];
	char sPropType[_TYPE_BUF_SZ+1];
	char sPropSep[8];   /* value separator for set/array props (raw wire form) */
	DasAry aPropVal;
}parse_stream_desc_t;

/* Formerly nested function "start" in parseStreamDescriptor */
static void parseDasStream_start(void* data, const char* el, const char** attr)
{
	int i;
	parse_stream_desc_t* pPsd = (parse_stream_desc_t*)data;

	if(pPsd->nRet != DAS_OKAY) /* Processing halt */
		return;

	DasStream* pSd = pPsd->pStream;
	char sType[64] = {'\0'};
	char sName[64] = {'\0'};
	const char* pColon = NULL;

	pPsd->bInProp = (strcmp(el, "p") == 0);
	  
	for(i=0; attr[i]; i+=2) {
		if (strcmp(el, "stream")==0) {
			if ( strcmp(attr[i], "compression")== 0 ) {
				strncpy( pSd->compression, (char*)attr[i+1], STREAMDESC_CMP_SZ-1);
				continue;
			}

			if(strcmp(attr[i],"type")==0){
				strncpy( pSd->type, (char*)attr[i+1], STREAMDESC_TYPE_SZ-1);
				continue;
			}
			
			if ( strcmp(attr[i], "version")== 0 ) {
				strncpy( pSd->version, (char*)attr[i+1], STREAMDESC_VER_SZ-1);
				if(DasStream_getVer(pSd) == STREAM_VER_UNK){
					fprintf(stderr, "Warning: Stream is version %s, expected %s "
						"or %s.  Some features might not be supported\n", pSd->version, 
						das_stream_ver_toStr(STREAM_VER_2_2),
						das_stream_ver_toStr(STREAM_VER_3_0)
					);
				}
				continue;
			}
			
			fprintf(stderr, "ignoring attribute of stream tag: %s\n", attr[i]);
			continue;
		} 
		
		if(strcmp( el, "properties" ) == 0){
			DasDesc* pCurDesc = (DasDesc*)(pPsd->pStream);

			if( (pColon = strchr(attr[i], ':')) != NULL){
				memset(sType, '\0', 64);
				strncpy(sType, attr[i], pColon - attr[i]);
				strncpy(sName, pColon+1, 63);
				DasDesc_set( pCurDesc, sType, sName, attr[i+1] );
			}
			else{
				DasDesc_set( pCurDesc, "String", attr[i], attr[i+1]);
			}
			continue;
		}

		if(strcmp(el, "p") == 0){
			if(!(pPsd->bV3Okay)){
				pPsd->nRet = das_error(DASERR_STREAM,
					"Element <p> is invalid in das2 stream headers"
				);
				return;
			}
			if(strcmp(attr[i], "name") == 0)
				strncpy(pPsd->sPropName, attr[i+1], _NAME_BUF_SZ);
			else if(strcmp(attr[i], "type") == 0)
				strncpy(pPsd->sPropType, attr[i+1], _TYPE_BUF_SZ);
			else if (strcmp(attr[i], "units") == 0)
				strncpy(pPsd->sPropUnits, attr[i+1], _UNIT_BUF_SZ);
			/* term is the schema spelling for the stringArray item terminator; sep is
			   accepted as a compatibility alias (matches _serial_onProp). */
			else if ((strcmp(attr[i], "term") == 0)||(strcmp(attr[i], "sep") == 0))
				strncpy(pPsd->sPropSep, attr[i+1], sizeof(pPsd->sPropSep)-1);

			continue;
		}

		pPsd->nRet = das_error(DASERR_STREAM,
			"Invalid element <%s> or attribute \"%s=\" under <stream> section",
			el, attr[i]
		);
		break;
	}

	if(pPsd->nRet != DAS_OKAY)
		return;

}

static void parseDasStream_chardata(void* data, const char* sChars, int len)
{
	parse_stream_desc_t* pPsd = (parse_stream_desc_t*)data;

	if(pPsd->nRet != DAS_OKAY) /* Processing halt */
		return;

	if(!pPsd->bInProp)
		return;

	DasAry* pAry = &(pPsd->aPropVal);

	DasAry_append(pAry, (ubyte*) sChars, len);
}

/* Formerly nested function "end" in parseDasStreamriptor */
static void parseDasStream_end(void* data, const char* el)
{
	parse_stream_desc_t* pPsd = (parse_stream_desc_t*)data;

	if(pPsd->nRet != DAS_OKAY) /* Processing halt */
		return;

	if(strcmp(el, "p") != 0)
		return;

	pPsd->bInProp = false;

	DasAry* pAry = &(pPsd->aPropVal);
	ubyte uTerm = 0;
	DasAry_append(pAry, &uTerm, 1);  /*  Null terminate the value string */
	size_t uValLen = 0;
	const char* sValue = DasAry_getCharsIn(pAry, DIM0, &uValLen);

	DasDesc* pDest = (DasDesc*)pPsd->pStream;

	/* Property units are input too.  Text that doesn't parse leaves the value
	   unitless, Units_fromStr() said why; the value text is untouched.  "" is
	   dimensionless. */
	das_units propUnits = Units_fromStr(pPsd->sPropUnits);
	if(propUnits == NULL) propUnits = UNIT_DIMENSIONLESS;

	DasDesc_flexSet(
		pDest,
		pPsd->sPropType[0] == '\0' ? "string" : pPsd->sPropType,
		0,
		pPsd->sPropName,
		sValue,
		dasprop_unescapeSep(pPsd->sPropSep),
		propUnits,
		3 /* Das3 */
	);

	memset(&(pPsd->sPropType), 0, _TYPE_BUF_SZ);
	memset(&(pPsd->sPropName), 0, _NAME_BUF_SZ);
	memset(&(pPsd->sPropUnits), 0, _UNIT_BUF_SZ);
	pPsd->sPropSep[0] = '\0';
	DasAry_clear(pAry);
}

DasStream* new_DasStream_str(DasBuf* pBuf, das_stream_model nModel)
{
	DasStream* pThis = new_DasStream();

	/*DasStream* pDesc;
	DasErrCode nRet;
	char sPropUnits[_UNIT_BUF_SZ+1];
	char sPropName[_NAME_BUF_SZ+1];
	char sPropType[_TYPE_BUF_SZ+1];
	DasAry aPropVal;
	*/

	parse_stream_desc_t psd;
	memset(&psd, 0, sizeof(psd));
	psd.pStream = pThis;
	psd.bV3Okay = (nModel == STREAM_MODEL_MIXED) || (nModel == STREAM_MODEL_V3);
	
	XML_Parser p = XML_ParserCreate("UTF-8");
	if(!p){
		das_error(DASERR_STREAM, "couldn't create xml parser\n");
		return NULL;
	}

	/* Make a 1-D dynamic array to hold the current property value so that
	   it has no up-front limits, but doesn't require a huge heap buffer 
	  for what are typically very small strings.*/
	init_DasAry(&(psd.aPropVal), "streamprops", vtUByte, 0, (const ubyte[1]){0}, RANK_1(0), NULL);
	
	XML_SetUserData(p, (void*) &psd);
	XML_SetElementHandler(p, parseDasStream_start, parseDasStream_end);
	XML_SetCharacterDataHandler(p, parseDasStream_chardata);
	
	size_t uXml = 0;
	const char* sXml = (const char*)DasBuf_data(pBuf, &uXml);
	int nParRet = XML_Parse(p, sXml, (int)uXml, true);

	if(!nParRet){
		/* Report before the free: the line number and error code live in the parser */
		das_error(DASERR_STREAM, "Parse error at line %lu:\n%s\n",
		           (unsigned long)XML_GetCurrentLineNumber(p), XML_ErrorString(XML_GetErrorCode(p))
		);
		XML_ParserFree(p);
		del_DasDesc((DasDesc*)pThis);           /* Don't leak on fail */
		fini_DasAry(&(psd.aPropVal));
		return NULL;
	}
	XML_ParserFree(p);
	if(psd.nRet != 0){
		del_DasDesc((DasDesc*)pThis);           /* Don't leak on fail */
		fini_DasAry(&(psd.aPropVal));
		return NULL;
	}

	fini_DasAry(&(psd.aPropVal));    /* ... nor on success */
	return pThis;
}

DasErrCode DasStream_encode2(DasStream* pThis, DasBuf* pBuf)
{

	/* Save off the encoding request format */
	strncpy(pThis->version, das_stream_ver_toStr(STREAM_VER_2_2), STREAMDESC_VER_SZ-1);

	DasErrCode nRet = 0;
	if((nRet = DasBuf_printf(pBuf, "<stream ")) !=0 ) return nRet;
	
	if(pThis->compression[0] != '\0') {
		nRet = DasBuf_printf(pBuf, "compression=\"%s\" ", pThis->compression);
		if(nRet != 0) return nRet;	
	}

	if( (nRet = DasBuf_printf(pBuf, "version=\"%s\" >\n", pThis->version)) != 0)
		return nRet;
	
	if( (nRet = DasDesc_encode2((DasDesc*)pThis, pBuf, "  ")) != 0) return nRet;

	return DasBuf_printf(pBuf, "</stream>\n");
}

DasErrCode DasStream_encode3(DasStream* pThis, DasBuf* pBuf)
{
	/* Save off the encoding request format.  A stream read from a das2
	   header has no type token, go with the non-namespace stream */
	strncpy(pThis->version, das_stream_ver_toStr(STREAM_VER_3_0), STREAMDESC_VER_SZ-1);
	if(pThis->type[0] == '\0')
		strncpy(pThis->type, das_stream_type_toStr(STREAM_TYPE_BASIC), STREAMDESC_TYPE_SZ-1);

	DasErrCode nRet = 0;
	if((nRet = DasBuf_printf(pBuf, "\n<stream ")) !=0 ) return nRet;
	
	if((pThis->compression[0] != '\0') && (strcmp(pThis->compression, "none") != 0)){
		nRet = DasBuf_printf(pBuf, "compression=\"%s\" ", pThis->compression);
		if(nRet != 0) return nRet;	
	}

	if( (nRet = DasBuf_printf(pBuf, 
		"type=\"%s\" version=\"%s\" >\n", pThis->type, pThis->version)
	) != 0)
		return nRet;	
	
	if( (nRet = DasDesc_encode3((DasDesc*)pThis, pBuf, "  ")) != 0)
		return nRet;
	
	return DasBuf_printf(pBuf, "</stream>\n");
}

/* Factory function */
DasDesc* DasDesc_decode(
	DasBuf* pBuf, DasStream* pSd, int nPktId, int nModel
){
	char sName[DAS_XML_NODE_NAME_LEN] = {'\0'}; 
	
	/* Eat the whitespace on either end */
	DasBuf_strip(pBuf);
	
	if(DasBuf_unread(pBuf) == 0){
		das_error(DASERR_STREAM, "Empty Descriptor Header in Stream");
		return NULL;
	}
	
	size_t uPos = DasBuf_readOffset(pBuf);
	char b; 
	DasBuf_read(pBuf, &b, 1);

	if(b != '<'){
		das_error(DASERR_STREAM, "found \"%c\", expected \"<\"", b);
		return NULL;
	}
	
	int i = 0;
	DasBuf_read(pBuf, &b, 1);
	
	/* Skip past the "I'm XML" header, if present */
	if(b == '?'){
		while( i < 256 /*safety check*/ && b != '\0' && b != '>' ){
			DasBuf_read(pBuf, &b, 1);
			i++;
		}
		if(b == '\0' || i == 256){
			das_error(DASERR_STREAM, "Error finding the end of the XML prolog, was the"
					     "entire prolog more that 255 characters long?");
			return NULL;
		}
				
		uPos = DasBuf_readOffset(pBuf);
		DasBuf_read(pBuf, &b, 1);
		
		/* Eat any whitespace after the prolog */
		while(isspace(b)){ 
			DasBuf_read(pBuf, &b, 1);
			uPos++;
		}
		
		/* If the current character is <, eat that too, but don't bump the */
		/* read position of the whole buffer */
		if(b == '<') DasBuf_read(pBuf, &b, 1);
		
		i = 0; /* Get ready for using i below */
	}
	
	while(i < (DAS_XML_NODE_NAME_LEN - 1) && 
			!isspace(b) && b != '\0' && b != '>' && b != '/'){
		sName[i] = b;
		DasBuf_read(pBuf, &b, 1);
		i++;
	}
	
	/* TODO: Use read with buf save point in DasIO for faster tag parsing */

	DasBuf_setReadOffset(pBuf, uPos); /* <-- the key call, back up the buffer */
	
   if(strcmp(sName, "stream") == 0){
   	/* The descriptor keeps what the wire said, a das2 header has no type
   	   token; the das3 encoder supplies one on the way out */
   	return (DasDesc*) new_DasStream_str(pBuf, nModel);
   }
	
   if(strcmp(sName, "packet") == 0){   	
   	DasPkt* pPkt = new_DasPkt_xml(pBuf, (DasDesc*)pSd, nPktId);

   	/* Up convert to a dataset if this is supposed to be a das3 stream.
   	   Note: The stream descriptor is needed to check for a "waveform" renderer.
   	         One of the spur-of-the-moment bad ideas in the v2 format. */
   	if(nModel == STREAM_MODEL_V3){
   		DasDs* pDs = new_DasDs_packet(pSd, pPkt, nPktId, NULL, true /* = add codecs */);
   		del_DasDesc((DasDesc*)pPkt);   /* transient scaffold: the DasDs copied all it needs */
   		return (DasDesc*) pDs;
   	}
   	else
   		return (DasDesc*) pPkt;
   }

	if(strcmp(sName, "dataset") == 0){
		if((nModel != STREAM_MODEL_MIXED)&&(nModel != STREAM_MODEL_V3)){
   		das_error(DASERR_STREAM, "das3 <dateset> element found, expected das2 headers");
   		return NULL;
   	}
		return (DasDesc*) new_DasDs_xml(pBuf, (DasDesc*)pSd, nPktId);
	}
	
	das_error(DASERR_STREAM, "Unknown top-level descriptor object: %s", sName);
	return NULL;
}


