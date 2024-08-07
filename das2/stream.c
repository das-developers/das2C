/* Copyright (C) 2015-2024 Chris Piker <chris-piker@uiowa.edu>
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

#include "serial2.h"
#include "serial3.h"
#include "stream.h"
#include "log.h"

/* ************************************************************************** */
/* Private defs from other modules */

DasDs* dasds_from_packet(
	DasStream* pSd, PktDesc* pPd, const char* sGroup, bool bCodecs
);


/* ************************************************************************** */
/* Construction */

DasStream* new_DasStream() 
{
	DasStream* pThis;
    
	pThis = (DasStream*) calloc(1, sizeof( DasStream ) );
	DasDesc_init((DasDesc*)pThis, STREAM);
	 
	pThis->bDescriptorSent = false;
    
	strncpy(pThis->compression, "none", STREAMDESC_CMP_SZ - 1);

	strncpy(pThis->version, DAS_22_STREAM_VER, STREAMDESC_VER_SZ - 1);
   
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
	strncpy(pOut->type, pThis->type, STREAMDESC_VER_SZ - 1);
	
	pOut->pUser = pThis->pUser;  /* Should this be copied ? */
	DasDesc_copyIn((DasDesc*)pOut, (DasDesc*)pThis);

	return pOut;
}

void del_DasStream(DasStream* pThis){
	DasDesc_freeProps(&(pThis->base));

	for(size_t u = 0; u < MAX_FRAMES; ++u){
		if(pThis->frames[u] != NULL){
			free(pThis->frames[u]);
			pThis->frames[u] = NULL;
		}
	}

	// Only delete the items I own! 

	for(size_t u = 1; u < MAX_PKTIDS; u++){
		DasDesc* pDesc = pThis->descriptors[u];
		if(pDesc == NULL)
			continue;
		if(pDesc->type == PACKET){
			if(pDesc->parent == (DasDesc*) pThis)
				del_PktDesc((PktDesc*)pDesc);
		}
		else{
			assert(pDesc->type == DATASET);
			if(pDesc->parent == (DasDesc*) pThis)
				del_DasDs((DasDs*)pDesc);
		}
	}
	free(pThis);
}

/* ************************************************************************** */
char* DasStream_info(const DasStream* pThis, char* sBuf, int nLen)
{
	if(nLen < 30)
		return sBuf;
	char* pWrite = sBuf;

	char sComp[32] = {'\0'};
	if(strcmp(pThis->compression, "none") != 0)
		snprintf(sComp, 31, "(%s compression)", pThis->compression);

	int nWritten = snprintf(pWrite, nLen - 1, "Stream: das v%s%s\n", pThis->version, sComp);
	pWrite += nWritten; nLen -= nWritten;

	if(DasDesc_length((DasDesc*)pThis) > 0){
		char* pSubWrite = DasDesc_info((DasDesc*)pThis, pWrite, nLen, "   ");
		nLen -= (pSubWrite - pWrite);
		pWrite = pSubWrite;
	}
	else{
		nWritten = snprintf(pWrite, nLen - 1, "   (no global properties)\n");
		pWrite += nWritten; nLen -= nWritten;
	}

	/* Now print info on each defined frame */
	for(int i = 0; i < MAX_FRAMES; ++i){
		if(pThis->frames[i] != NULL){
			char* pSubWrite = DasFrame_info(pThis->frames[i], pWrite, nLen);
			nLen -= (pSubWrite - pWrite);
			pWrite = pSubWrite;
		}
	}
	if(nLen < 2) return pWrite;
	*pWrite = '\n'; ++pWrite; --nLen;
	return pWrite;
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

size_t DasStream_getNPktDesc(const DasStream* pThis)
{
	size_t nRet = 0;
	for(size_t u = 1; u < MAX_PKTIDS; u++) if(pThis->descriptors[u]) nRet++;
	return nRet;
}

int DasStream_nextPktId(DasStream* pThis)
{
	for (int i = 1; i < MAX_PKTIDS; ++i){ /* 00 is reserved for stream descriptor */
		if(pThis->descriptors[i] == NULL) 
			return i;
	}
	return -1 * das_error(DASERR_STREAM, "Ran out of Packet IDs only 99 allowed!" );
}

PktDesc* DasStream_createPktDesc(
	DasStream* pThis, DasEncoding* pXEncoder, das_units xUnits 
){
	PktDesc* pPkt;

	pPkt= new_PktDesc();
	pPkt->id= DasStream_nextPktId(pThis);
	pPkt->base.parent=(DasDesc*)pThis;
	 
	PlaneDesc* pX = new_PlaneDesc(X, "", pXEncoder, xUnits);
	PktDesc_addPlane(pPkt, pX);
	pThis->descriptors[pPkt->id] = (DasDesc*) pPkt;
	
	return pPkt;
}

DasErrCode DasStream_freeSubDesc(DasStream* pThis, int nPktId)
{
	if(!DasStream_isValidId(pThis, nPktId))
		return das_error(DASERR_STREAM, "%s: stream contains no descriptor for packets "
		                  "with id %d", __func__, nPktId);

	DasDesc* pDesc = pThis->descriptors[nPktId];
	if(pDesc->type == PACKET)
		del_PktDesc((PktDesc*)pDesc);
	else
		del_DasDs((DasDs*)pDesc);
	pThis->descriptors[nPktId]= NULL;

	for(size_t u = 0; (u < MAX_FRAMES) && (pThis->frames[u] != NULL); ++u)
		del_DasFrame(pThis->frames[u]);
	
	return DAS_OKAY;
}

PktDesc* DasStream_getPktDesc(const DasStream* pThis, int nPacketId)
{
	if(nPacketId < 1 || nPacketId > (MAX_PKTIDS-1)){
		das_error(DASERR_STREAM, 
			"Illegal Packet ID %d in getPacketDescriptor", nPacketId
		);
		return NULL;
	}
	DasDesc* pDesc = pThis->descriptors[nPacketId];
	return (pDesc == NULL)||(pDesc->type != PACKET) ? NULL : (PktDesc*)pDesc;
}

int DasStream_getPktId(DasStream* pThis, const DasDesc* pDesc)
{
	/* Linear search for now, but small vector assumption may no always hold
	   in the future */

	/* 0 is never a container ID */
	for(int i = 1; i < MAX_PKTIDS; ++i){
		if((pThis->descriptors[i] != NULL)&&(pThis->descriptors[i] == pDesc))
			return i;
	}
	return -1;
}


DasDesc* DasStream_nextPktDesc(const DasStream* pThis, int* pPrevPktId)
{
	int nBeg = *pPrevPktId + 1;
	if(nBeg < 1){
		das_error(DASERR_STREAM, "Illegal descriptor value %d", nBeg);
		return NULL;
	}
	for(int i = nBeg; i < MAX_PKTIDS; ++i){
		if(pThis->descriptors[i] != NULL){
			*pPrevPktId = i;
			return pThis->descriptors[i];
		}
	}
	return NULL;
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

PktDesc* DasStream_clonePktDesc(DasStream* pThis, const PktDesc* pPdIn)
{
	PktDesc* pPdOut;

	pPdOut= (PktDesc*)calloc(1, sizeof(PktDesc));
	pPdOut->base.type = pPdIn->base.type;
	 
	DasDesc_copyIn((DasDesc*)pPdOut, (DasDesc*)pPdIn);
	 
	int id = DasStream_nextPktId( pThis );
	pThis->descriptors[id] = (DasDesc*)pPdOut;

	pPdOut->id = id;
	 
	PktDesc_copyPlanes(pPdOut, pPdIn);  /* Realloc's the data buffer */

	return pPdOut;
}

bool DasStream_isValidId(const DasStream* pThis, int nPktId)
{
	if(nPktId > 0 && nPktId < MAX_PKTIDS){
		if(pThis->descriptors[nPktId] != NULL) return true;
	}
	return false;
}

PktDesc* DasStream_clonePktDescById(
	DasStream* pThis, const DasStream* pOther, int nPacketId
){
	PktDesc* pIn, *pOut;

	pIn = DasStream_getPktDesc(pOther, nPacketId);

	if(pThis->descriptors[pIn->id] != NULL){
		das_error(DASERR_STREAM, "ERROR: Stream descriptor already has a packet "
		                "descriptor with id %d", nPacketId); 
		return NULL;
	}
	
	pOut = new_PktDesc();
	pOut->base.parent = (DasDesc*)pThis;
	
	DasDesc_copyIn((DasDesc*)pOut, (DasDesc*)pIn);
	
	pOut->id = pIn->id;
	pThis->descriptors[pIn->id] = (DasDesc*)pOut;
	
	PktDesc_copyPlanes(pOut, pIn); /* Realloc's the data buffer */
		
	/* Do not copy user data */
	
	return pOut;
}

DasErrCode DasStream_addPktDesc(DasStream* pThis, DasDesc* pDesc, int nPktId)
{
	/* Only accept either das2 packet descriptors or das3 datasets */
	if((pDesc->type != PACKET)&&(pDesc->type != DATASET))
		return das_error(DASERR_STREAM, "Unexpected packet desciptor type");

	if((pDesc->parent != NULL)&&(pDesc->parent != (DasDesc*)pThis)){

		/* Hint to random developer: If you are here because you wanted to copy 
		 * another stream's packet descriptor onto this stream use one of 
		 * DasStream_clonePktDesc() or DasStream_clonePktDescById() instead. 
		 *
		 * If you're here because you wanted to track another packet descriptor but 
		 * not own it, use DasStream_shadowPktDesc() below.
		 */
		return das_error(DASERR_STREAM, "Packet Descriptor already belongs to different "
				                "stream");
	}
	
	/* Check uniqueness */
	if(pDesc->parent == (DasDesc*)pThis){
		for(int i = 0; i < MAX_PKTIDS; ++i)
			if(pThis->descriptors[i] != NULL)
				if(pThis->descriptors[i]->type == PACKET)
					if(PktDesc_equalFormat((PktDesc*)pDesc, (PktDesc*)(pThis->descriptors[i])))
						return das_error(DASERR_STREAM, 
							"Packet Descriptor is already part of the stream"
						);
	}
	
	if(nPktId < 1 || nPktId > 99)
		return das_error(DASERR_STREAM, "Illegal packet id: %02d", nPktId);
	
	if(pThis->descriptors[nPktId] != NULL) 
		return das_error(DASERR_STREAM, "DasStream already has a packet descriptor with ID"
				" %02d", nPktId);
	
	pThis->descriptors[nPktId] = pDesc;

	/* If this is an old-sckool packet descriptor, set it's ID */
	if(pDesc->type == PACKET)
		((PktDesc*)pDesc)->id = nPktId;

	pDesc->parent = (DasDesc*)pThis;
	return 0;
}

DasErrCode DasStream_shadowPktDesc(DasStream* pThis, DasDesc* pDesc, int nPktId)
{
	/* Only accept either das2 packet descriptors or das3 datasets */
	if((pDesc->type != PACKET)&&(pDesc->type != DATASET))
		return das_error(DASERR_STREAM, "Unexpected packet desciptor type");

	if((pDesc->parent == (DasDesc*) pThis))
		return das_error(DASERR_STREAM, "Can't shadow my own packet descriptors");
	
	/* Check uniqueness */
	if(pDesc->parent == (DasDesc*)pThis){
		for(int i = 0; i < MAX_PKTIDS; ++i)
			if(pThis->descriptors[i] != NULL)
				if(pThis->descriptors[i]->type == PACKET)
					if(PktDesc_equalFormat((PktDesc*)pDesc, (PktDesc*)(pThis->descriptors[i])))
						return das_error(DASERR_STREAM, 
							"Packet Descriptor is already part of the stream"
						);
	}
	
	if(nPktId < 1 || nPktId > 99)
		return das_error(DASERR_STREAM, "Illegal packet id: %02d", nPktId);
	
	if(pThis->descriptors[nPktId] != NULL) 
		return das_error(DASERR_STREAM, "DasStream already has a packet descriptor with ID"
				" %02d", nPktId);
	
	pThis->descriptors[nPktId] = pDesc;

	return 0;
}

DasErrCode DasStream_ownPktDesc(DasStream* pThis, DasDesc* pDesc, int nPktId)
{
	// Make sure I'm already tracking it.

	// Try lookup by address
	if(pDesc != NULL){

		// More unnecessary loops, need to erradicate this way of tracking owned objects!
		// I inherited it, but it's way past it sale date.  --cwp
		for(int i = 0; i < MAX_PKTIDS; ++i){
			if(pThis->descriptors[i] == pDesc){
				pDesc->parent = (DasDesc*) pThis;
				return 0;
			}
		}
		return das_error(DASERR_STREAM, "Could not find packet descriptor in tracking array");
	}

	if(nPktId < 1 || nPktId > 99)
		return das_error(DASERR_STREAM, "Illegal packet id: %02d", nPktId);

	if(pThis->descriptors[nPktId] == NULL)
		return das_error(DASERR_STREAM, "Packet ID slot %02d points to nothing", nPktId);

	pThis->descriptors[nPktId]->parent = (DasDesc*) pThis;

	return 0;
}

DasErrCode DasStream_rmPktDesc(DasStream* pThis, DasDesc* pDesc, int nPktId)
{
	/* This is essentially 2 functions, but we don't have function overloading in C */

	if(pDesc != NULL){

		if((pDesc->type != PACKET)&&(pDesc->type != DATASET))
			return das_error(DASERR_STREAM, "Unexpected packet desciptor type");

		if((pDesc->parent != (DasDesc*)pThis))
			return das_error(DASERR_STREAM, "Descriptor dosen't belong to this stream");

		for(int i = 0; i < MAX_PKTIDS; ++i){
			if(pThis->descriptors[i] == pDesc){
				pThis->descriptors[i] = NULL;  /* Detach both ways */
				pDesc->parent = NULL;
				return DAS_OKAY;
			}
		}

		return das_error(DASERR_STREAM, "Descriptor is not part of this stream");
	}

	if(nPktId < 1 || nPktId > 99)
		return das_error(DASERR_STREAM, "Illegal packet id: %02d", nPktId);

	if(pThis->descriptors[nPktId] == NULL)
		return das_error(DASERR_STREAM, "Stream has not descriptor for packet id: %02d", nPktId);
	
	pDesc = pThis->descriptors[nPktId];
	pDesc->parent = NULL;
	pThis->descriptors[nPktId] = NULL;

	return DAS_OKAY;
}


/* ************************************************************************* */
/* Frame wrappers */

/* Takes ownership */
int DasStream_addFrame(DasStream* pThis, DasFrame* pFrame)
{

	if(pFrame == NULL)
		return -1 * das_error(DASERR_STREAM, "Null vector frame pointers not allowed");

	// Find a slot for it.
	int nIdx = 0;
	while((pThis->frames[nIdx] != 0) && (nIdx < (MAX_FRAMES))){ 
		if(strcmp(DasFrame_getName(pFrame),DasFrame_getName(pThis->frames[nIdx]))==0)
		{
			return -1 * das_error(DASERR_STREAM,
				"A vector direction frame named '%s' already exist for this stream",
				DasFrame_getName(pFrame)		
			);
		}
		++nIdx;
	}
	
	if(pThis->frames[nIdx] != NULL){
		return -1 * das_error(DASERR_STREAM, 
			"Adding more then %d frame definitions will require a recompile",
			MAX_FRAMES
		);
	}

	if(pFrame != NULL){
		pThis->frames[nIdx] = pFrame;
	}

	return nIdx;	
}

DasFrame* DasStream_createFrame(
   DasStream* pThis, ubyte id, const char* sName, const char* sType, ubyte uType
){
	DasFrame* pFrame;
	if((sType == NULL)||(sType[0] == '\0'))
		pFrame = new_DasFrame2((DasDesc*)pThis, id, sName, uType);
	else
		pFrame = new_DasFrame((DasDesc*)pThis, id, sName, sType);

	int nRet = DasStream_addFrame(pThis, pFrame);

	if(nRet < 0)
		return NULL;
	else
		return pFrame;
}

int8_t DasStream_getFrameId(const DasStream* pThis, const char* sFrame){

	for(int i = 0; i < MAX_FRAMES; ++i){
		if(pThis->frames[i] == NULL)
			continue;
		if(strcmp(pThis->frames[i]->name, sFrame) == 0)
			return pThis->frames[i]->id;
	}
	return -1 * DASERR_STREAM;
}

int DasStream_newFrameId(const DasStream* pThis){

	/* Since MAX_FRAMES is small and the size of the frame ID field 
	   is small, we can get away with a double loop */
	for(ubyte i = 1; i < 256; ++i){
		bool bUsed = false;
		for(int j = 0; j < MAX_FRAMES; ++j){
			if((pThis->frames[j] != NULL) && (pThis->frames[j]->id == i)){
				bUsed = true;
				break;
			}
		}
		if(!bUsed) return (int)i;
	}

	return -1 * DASERR_STREAM;
}

int8_t DasStream_getNumFrames(const DasStream* pThis)
{
	/* Return the ID of the last defined frame */
	int8_t iLastGood = -1;
	for(int8_t i = 0; i < MAX_FRAMES; ++i){
		if(pThis->frames[i] != NULL)
			iLastGood = i;
	}
	return iLastGood + 1;
}

const DasFrame* DasStream_getFrame(const DasStream* pThis, int idx)
{
	return (idx < 0 || idx > MAX_FRAMES) ? NULL : pThis->frames[idx];
}

const DasFrame* DasStream_getFrameByName(
   const DasStream* pThis, const char* sFrame
){
	for(size_t u = 0; (u < MAX_FRAMES) && (pThis->frames[u] != NULL); ++u){
		if(strcmp(sFrame, DasFrame_getName(pThis->frames[u])) == 0)
			return pThis->frames[u];
	}
	return NULL;
}

const DasFrame* DasStream_getFrameById(const DasStream* pThis, ubyte id)
{
	for(size_t u = 0; (u < MAX_FRAMES) && (pThis->frames[u] != NULL); ++u){
		if(pThis->frames[u]->id == id)
			return pThis->frames[u];
	}
	return NULL;
}


/* ************************************************************************* */
/* Serializing */

#define _UNIT_BUF_SZ 127
#define _NAME_BUF_SZ 63
#define _TYPE_BUF_SZ 23

typedef struct parse_stream_desc{
	DasStream* pStream;
	DasFrame*   pFrame;  // Only non-null when in a <frame> tag
	DasErrCode nRet;
	bool bInProp;
	bool bV3Okay;
	char sPropUnits[_UNIT_BUF_SZ+1];
	char sPropName[_NAME_BUF_SZ+1];
	char sPropType[_TYPE_BUF_SZ+1];
	DasAry aPropVal;
}parse_stream_desc_t;

/* Formerly nested function "start" in parseStreamDescriptor */
void parseDasStream_start(void* data, const char* el, const char** attr)
{
	int i;
	parse_stream_desc_t* pPsd = (parse_stream_desc_t*)data;

	if(pPsd->nRet != DAS_OKAY) /* Processing halt */
		return;

	DasStream* pSd = pPsd->pStream;
	char sType[64] = {'\0'};
	char sName[64] = {'\0'};
	const char* pColon = NULL;
	bool bInertial = false;

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
				if((strcmp(pSd->version, DAS_22_STREAM_VER) > 0) && 
					(strcmp(pSd->version, DAS_30_STREAM_VER) > 0)
				){
					fprintf(stderr, "Warning: Stream is version %s, expected %s "
						"or %s.  Some features might not be supported\n", pSd->version, 
						DAS_22_STREAM_VER, DAS_30_STREAM_VER
					);
				}
				continue;
			}
			
			fprintf(stderr, "ignoring attribute of stream tag: %s\n", attr[i]);
			continue;
		} 
		
		if(strcmp( el, "properties" ) == 0){
			DasDesc* pCurDesc = pPsd->pFrame ? (DasDesc*)(pPsd->pFrame) : (DasDesc*)(pPsd->pStream);

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
			
			continue;
		}

		// elements dir and frame have name in common
		if((strcmp(el, "dir") == 0)||(strcmp(el, "frame") == 0)){
			if(!(pPsd->bV3Okay)){
				pPsd->nRet = das_error(DASERR_STREAM,
					"Element <%s> is invalid in dasStream v2 headers", el
				);
				return;
			}
			if(strcmp(attr[i], "name") == 0){
				memset(sName, 0, 64); strncpy(sName, attr[i+1], 63);
				continue;
			}
		}

		if(strcmp(el, "frame") == 0){
			if(!(pPsd->bV3Okay)){
				pPsd->nRet = das_error(DASERR_STREAM,
					"Element <%s> is invalid in das2 stream headers", el
				);
				return;
			}
			if(strcmp(attr[i], "type") == 0){
				memset(sType, 0, 64); strncpy(sType, attr[i+1], 63);
				continue;
			}

			/* Frames just have names, not IDs, those are assigned internally ...
			if(strcmp(attr[i], "id") == 0){
				if((sscanf(attr[i+1], "%hhd", &nFrameId) != 1)||(nFrameId == 0)){
					pPsd->nRet = das_error(DASERR_STREAM, "Invalid frame ID, %hhd", nFrameId);
				}
				continue;
			}
			*/
			if(strcmp(attr[i], "inertial") == 0){
				if((attr[i+1][0] == 't')||(attr[i+1][0] == 'T')||(strcasecmp(attr[i+1], "true") == 0))
					bInertial = true;
				continue;
			}
		}
		
		pPsd->nRet = das_error(DASERR_STREAM, 
			"Invalid element <%s> or attribute \"%s=\" under <stream> section", 
			el, attr[i]
		);
		break;
	}

	if(strcmp(el, "frame") == 0){
		if(!(pPsd->bV3Okay)){
			pPsd->nRet = das_error(DASERR_STREAM,
				"Element <%s> is invalid in das2 stream headers", el
			);
			return;
		}

		int8_t uFrameId = DasStream_newFrameId(pSd);
		pPsd->pFrame = DasStream_createFrame(pSd, uFrameId, sName, sType, 0);
		if(!pPsd->pFrame){
			pPsd->nRet = das_error(DASERR_STREAM, "Frame definition failed in <stream> header");
		}
		if(bInertial)
			pPsd->pFrame->flags |= DASFRM_INERTIAL;
		return;
	}

	if(strcmp(el, "dir") == 0){
		if(!(pPsd->bV3Okay)){
			pPsd->nRet = das_error(DASERR_STREAM,
				"Element <%s> is invalid in dasStream v2 headers", el
			);
			return;
		}

		if(pPsd->pFrame == NULL){
			pPsd->nRet = das_error(DASERR_STREAM, "<dir> element encountered outside a <stream>");
		}
		else{
			int nTmp = DasFrame_addDir(pPsd->pFrame, sName);
			if(nTmp > 0)
				pPsd->nRet = nTmp;
		}
		return;
	}
}

void parseDasStream_chardata(void* data, const char* sChars, int len)
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
void parseDasStream_end(void* data, const char* el)
{
	parse_stream_desc_t* pPsd = (parse_stream_desc_t*)data;

	if(pPsd->nRet != DAS_OKAY) /* Processing halt */
		return;

	if(strcmp(el, "frame") == 0){
		// Close the frame
		pPsd->pFrame = NULL;  
		return;
	}

	if(strcmp(el, "p") != 0)
		return;

	pPsd->bInProp = false;

	DasAry* pAry = &(pPsd->aPropVal);
	ubyte uTerm = 0;
	DasAry_append(pAry, &uTerm, 1);  // Null terminate the value string
	size_t uValLen = 0;
	const char* sValue = DasAry_getCharsIn(pAry, DIM0, &uValLen);

	// Set attribute for stream itself or for coordinate frame depending
	// on if a frame element is current in process.
	DasDesc* pDest = pPsd->pFrame ? (DasDesc*)pPsd->pFrame : (DasDesc*)pPsd->pStream;

	DasDesc_flexSet(
		pDest, 
		pPsd->sPropType[0] == '\0' ? "string" : pPsd->sPropType,
		0,
		pPsd->sPropName,
		sValue,
		'\0',
		pPsd->sPropUnits[0] == '\0' ? NULL : Units_fromStr(pPsd->sPropUnits),
		3 /* Das3 */
	);

	memset(&(pPsd->sPropType), 0, _TYPE_BUF_SZ);
	memset(&(pPsd->sPropName), 0, _NAME_BUF_SZ);
	memset(&(pPsd->sPropUnits), 0, _UNIT_BUF_SZ);
	DasAry_clear(pAry);
}

DasStream* new_DasStream_str(DasBuf* pBuf, int nModel)
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
	DasAry_init(&(psd.aPropVal), "streamprops", vtUByte, 0, NULL, RANK_1(0), NULL);
	
	XML_SetUserData(p, (void*) &psd);
	XML_SetElementHandler(p, parseDasStream_start, parseDasStream_end);
	XML_SetCharacterDataHandler(p, parseDasStream_chardata);
	
	int nParRet = XML_Parse(p, pBuf->pReadBeg, DasBuf_unread(pBuf), true);
	XML_ParserFree(p);
	
	if(!nParRet){
		das_error(DASERR_STREAM, "Parse error at line %d:\n%s\n",
		           XML_GetCurrentLineNumber(p), XML_ErrorString(XML_GetErrorCode(p))
		);
		del_DasStream(pThis);           // Don't leak on fail
		DasAry_deInit(&(psd.aPropVal));
		return NULL;
	}
	if(psd.nRet != 0){
		del_DasStream(pThis);           // Don't leak on fail
		DasAry_deInit(&(psd.aPropVal));
		return NULL;
	}
	
	return pThis;
}

DasErrCode DasStream_encode2(DasStream* pThis, DasBuf* pBuf)
{

	/* Save off the encoding request format */
	strncpy(pThis->version, DAS_22_STREAM_VER, STREAMDESC_VER_SZ-1);

	DasErrCode nRet = 0;
	if((nRet = DasBuf_printf(pBuf, "<stream ")) !=0 ) return nRet;
	
	if(pThis->compression[0] != '\0') {
		nRet = DasBuf_printf(pBuf, "compression=\"%s\" ", pThis->compression);
		if(nRet != 0) return nRet;	
	}

	if( (nRet = DasBuf_printf(pBuf, "version=\"%s\" >\n", pThis->version)) != 0)
		return nRet;
	
	if( (nRet = DasDesc_encode2((DasDesc*)pThis, pBuf, "  ")) != 0) return nRet;

	for(int i = 0; i < MAX_FRAMES; ++i){
		if(pThis->frames[i] != NULL){
			daslog_warn("dasStream v2 doesn't support vector frames, one or "
				"more frame definitions dropped"
			);
			break;
		}
	}
		
	return DasBuf_printf(pBuf, "</stream>\n");
}

DasErrCode DasStream_encode3(DasStream* pThis, DasBuf* pBuf)
{
	/* Save off the encoding request format */
	strncpy(pThis->version, DAS_30_STREAM_VER, STREAMDESC_VER_SZ-1);

	DasErrCode nRet = 0;
	if((nRet = DasBuf_printf(pBuf, "<stream ")) !=0 ) return nRet;
	
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
	
	for(int i = 0; i < MAX_FRAMES; ++i){
		if(pThis->frames[i] != NULL){
			if( (nRet = DasFrame_encode(pThis->frames[i], pBuf, "  ", 3)) != 0)
				return nRet;
		}
	}
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
		while( i < 256 /*safty check*/ && b != '\0' && b != '>' ){
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
		return (DasDesc*) new_DasStream_str(pBuf, nModel);
   }
	
   if(strcmp(sName, "packet") == 0){
   	PktDesc* pPkt = new_PktDesc_xml(pBuf, (DasDesc*)pSd, nPktId);

   	/* Up convert to a dataset if this is supposed to be a das3 stream.
   	   Note: The stream descriptor is needed to check for a "waveform" renderer.
   	         One of the spur-of-the-moment bad ideas in the v2 format. */
   	if(nModel == STREAM_MODEL_V3)
   		return (DasDesc*) dasds_from_packet(pSd, pPkt, NULL, true /* = add codecs */);
   	else
   		return (DasDesc*) pPkt;
   }

	if(strcmp(sName, "dataset") == 0){
		if((nModel != STREAM_MODEL_MIXED)&&(nModel != STREAM_MODEL_V3)){
   		das_error(DASERR_STREAM, "das3 <dateset> element found, expected das2 headers");
   		return NULL;
   	}
		return (DasDesc*) dasds_from_xmlheader(pBuf, pSd, nPktId);
	}
	
	das_error(DASERR_STREAM, "Unknown top-level descriptor object: %s", sName);
	return NULL;
}
