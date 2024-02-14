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

#include <expat.h>

#include "serial.h"
#include "stream.h"


/* ************************************************************************** */
/* Construction */

StreamDesc* new_StreamDesc() 
{
	StreamDesc* pThis;
    
	pThis = ( StreamDesc* ) calloc(1, sizeof( StreamDesc ) );
	DasDesc_init((DasDesc*)pThis, STREAM);
	 
	pThis->bDescriptorSent = false;
    
	strncpy(pThis->compression, "none", STREAMDESC_CMP_SZ - 1);
	strncpy(pThis->version, DAS_22_STREAM_VER, STREAMDESC_VER_SZ - 1);
   
    return pThis;
}

/* Deep copy a stream descriptor */
StreamDesc* StreamDesc_copy(const StreamDesc* pThis) 
{
	/* Since this is a deep copy do it by explicit assignment, less likely to
	 * make a mistake that way */
	StreamDesc* pOut = new_StreamDesc();
	strncpy(pOut->compression, pThis->compression, 47);
	
	pOut->pUser = pThis->pUser;  /* Should this be copied ? */
	DasDesc_copyIn((DasDesc*)pOut, (DasDesc*)pThis);

	return pOut;
}

void del_StreamDesc(StreamDesc* pThis){
	DasDesc_freeProps(&(pThis->base));
	for(size_t u = 1; u < MAX_PKTIDS; u++){
		DasDesc* pDesc = pThis->descriptors[u];
		if(pDesc == NULL)
			continue;
		if(pDesc->type == PACKET){
			del_PktDesc((PktDesc*)pDesc);
		}
		else{
			assert(pDesc->type == DATASET);
			del_DasDs((DasDs*)pDesc);
		}
	}
	free(pThis);
}

/* ************************************************************************** */
/* Adding/Removing detail */

void StreamDesc_addStdProps(StreamDesc* pThis)
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

void StreamDesc_setMonotonic(StreamDesc* pThis, bool isMonotonic )
{
	if(isMonotonic)
		DasDesc_setBool( (DasDesc*)pThis, "monotonicXTags", true );
	else
		DasDesc_setBool( (DasDesc*)pThis, "monotonicXTags", false );
}

size_t StreamDesc_getNPktDesc(const StreamDesc* pThis)
{
	size_t nRet = 0;
	for(size_t u = 1; u < MAX_PKTIDS; u++) if(pThis->descriptors[u]) nRet++;
	return nRet;
}

int StreamDesc_nextPktId(StreamDesc* pThis)
{
	for (int i = 1; i < MAX_PKTIDS; ++i){ /* 00 is reserved for stream descriptor */
		if(pThis->descriptors[i] == NULL) 
			return i;
	}
	return -1 * das_error(DASERR_STREAM, "Ran out of Packet IDs only 99 allowed!" );
}

PktDesc* StreamDesc_createPktDesc(
	StreamDesc* pThis, DasEncoding* pXEncoder, das_units xUnits 
){
	PktDesc* pPkt;

	pPkt= new_PktDesc();
	pPkt->id= StreamDesc_nextPktId(pThis);
	pPkt->base.parent=(DasDesc*)pThis;
	 
	PlaneDesc* pX = new_PlaneDesc(X, "", pXEncoder, xUnits);
	PktDesc_addPlane(pPkt, pX);
	pThis->descriptors[pPkt->id] = (DasDesc*) pPkt;
	
	return pPkt;
}

DasErrCode StreamDesc_freeDesc(StreamDesc* pThis, int nPktId)
{
	if(!StreamDesc_isValidId(pThis, nPktId))
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

PktDesc* StreamDesc_getPktDesc(const StreamDesc* pThis, int nPacketId)
{
	if(nPacketId < 1 || nPacketId > 99){
		das_error(DASERR_STREAM, 
			"Illegal Packet ID %d in getPacketDescriptor", nPacketId
		);
		return NULL;
	}
	DasDesc* pDesc = pThis->descriptors[nPacketId];
	return (pDesc == NULL)||(pDesc->type != PACKET) ? NULL : (PktDesc*)pDesc;
}


void StreamDesc_addCmdLineProp(StreamDesc* pThis, int argc, char * argv[] ) 
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

PktDesc* StreamDesc_clonePktDesc(StreamDesc* pThis, const PktDesc* pPdIn)
{
	PktDesc* pPdOut;
	pPdOut= (PktDesc*)calloc(1, sizeof(PktDesc));
	pPdOut->base.type = pPdIn->base.type;
	 
	DasDesc_copyIn((DasDesc*)pPdOut, (DasDesc*)pPdIn);
	 
	int id = StreamDesc_nextPktId( pThis );
	pThis->descriptors[id] = (DasDesc*)pPdOut;

	pPdOut->id = id;
	 
	PktDesc_copyPlanes(pPdOut, pPdIn);  /* Realloc's the data buffer */

	return pPdOut;
}

bool StreamDesc_isValidId(const StreamDesc* pThis, int nPktId)
{
	if(nPktId > 0 && nPktId < MAX_PKTIDS){
		if(pThis->descriptors[nPktId] != NULL) return true;
	}
	return false;
}

PktDesc* StreamDesc_clonePktDescById(
	StreamDesc* pThis, const StreamDesc* pOther, int nPacketId
){
	PktDesc* pIn, *pOut;

	pIn = StreamDesc_getPktDesc(pOther, nPacketId);

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

DasErrCode StreamDesc_addPktDesc(StreamDesc* pThis, PktDesc* pPd, int nPktId)
{
	if((pPd->base.parent != NULL)&&(pPd->base.parent != (DasDesc*)pThis))
		/* Hint to random developer: If you are here because you wanted to copy 
		 * another stream's packet descriptor onto this stream use one of 
		 * StreamDesc_clonePktDesc() or StreamDesc_clonePktDescById() instead. */
		return das_error(DASERR_STREAM, "Packet Descriptor already belongs to different "
				                "stream");
	
	if(pPd->base.parent == (DasDesc*)pThis) 
		return das_error(DASERR_STREAM, "Packet Descriptor is already part of the stream");
	
	if(nPktId < 1 || nPktId > 99)
		return das_error(DASERR_STREAM, "Illegal packet id in addPktDesc: %02d", nPktId);
	
	if(pThis->descriptors[nPktId] != NULL) 
		return das_error(DASERR_STREAM, "StreamDesc already has a packet descriptor with ID"
				" %02d", nPktId);
	
	pThis->descriptors[nPktId] = (DasDesc*)pPd;
	pPd->id = nPktId;
	pPd->base.parent = (DasDesc*)pThis;
	return 0;
}

/* ************************************************************************* */
/* Frame wrappers */

DasFrame* StreamDesc_createFrame(
   StreamDesc* pThis, ubyte id, const char* sName, const char* sType
){
	// Find a slot for it.
	size_t uIdx = 0;
	while((pThis->frames[uIdx] != 0) && (uIdx < (MAX_FRAMES-1))){ 
		if(strcmp(sName, DasFrame_getName(pThis->frames[uIdx])) == 0){
			das_error(DASERR_STREAM,
				"A vector direction frame named '%s' already exist for this stream",
				sName
			);
			return NULL;
		}
		++uIdx;
	}
	
	if(pThis->frames[uIdx] != NULL){
		das_error(DASERR_STREAM, 
			"Adding more then %d frame definitions will require a recompile",
			MAX_FRAMES
		);
		return NULL;
	}

	DasFrame* pFrame = new_DasFrame((DasDesc*)pThis, id, sName, sType);
	if(pFrame != NULL){
		pThis->frames[uIdx] = pFrame;
	}

	return pFrame;
}

int StreamDesc_nextFrameId(const StreamDesc* pThis){
	for(int i = 0; i < MAX_FRAMES; ++i){
		if(pThis->frames[i] == NULL)
			return i;
	}
	return -1;
}

const DasFrame* StreamDesc_getFrame(const StreamDesc* pThis, int idx)
{
	return (idx < 0 || idx > MAX_FRAMES) ? NULL : pThis->frames[idx];
}

const DasFrame* StreamDesc_getFrameByName(
   const StreamDesc* pThis, const char* sFrame
){
	for(size_t u = 0; (u < MAX_FRAMES) && (pThis->frames[u] != NULL); ++u){
		if(strcmp(sFrame, DasFrame_getName(pThis->frames[u])) == 0)
			return pThis->frames[u];
	}
	return NULL;
}

const DasFrame* StreamDesc_getFrameById(const StreamDesc* pThis, ubyte id)
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
	StreamDesc* pStream;
	DasFrame*   pFrame;  // Only non-null when in a <frame> tag
	DasErrCode nRet;
	bool bInProp;
	char sPropUnits[_UNIT_BUF_SZ+1];
	char sPropName[_NAME_BUF_SZ+1];
	char sPropType[_TYPE_BUF_SZ+1];
	DasAry aPropVal;
}parse_stream_desc_t;

/* Formerly nested function "start" in parseStreamDescriptor */
void parseStreamDesc_start(void* data, const char* el, const char** attr)
{
	int i;
	parse_stream_desc_t* pPsd = (parse_stream_desc_t*)data;
	StreamDesc* pSd = pPsd->pStream;
	char sType[64] = {'\0'};
	char sName[64] = {'\0'};
	ubyte nFrameId = 0;
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
			if(strcmp(attr[i], "name") == 0){
				memset(sName, 0, 64); strncpy(sName, attr[i+1], 63);
				continue;
			}
		}

		if(strcmp(el, "frame") == 0){
			if(strcmp(attr[i], "type") == 0){
				memset(sType, 0, 64); strncpy(sType, attr[i+1], 63);
				continue;
			}
			if(strcmp(attr[i], "id") == 0){
				if((sscanf(attr[i+1], "%hhd", &nFrameId) != 1)||(nFrameId == 0)){
					pPsd->nRet = das_error(DASERR_STREAM, "Invalid frame ID, %hhd", nFrameId);
				}
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
		pPsd->pFrame = StreamDesc_createFrame(pSd, nFrameId, sName, sType);
		if(!pPsd->pFrame){
			pPsd->nRet = das_error(DASERR_STREAM, "Frame definition failed in <stream> header");
		}
		return;
	}

	if(strcmp(el, "dir") == 0){
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

void parseStreamDesc_chardata(void* data, const char* sChars, int len)
{
	parse_stream_desc_t* pPsd = (parse_stream_desc_t*)data;

	if(!pPsd->bInProp)
		return;

	DasAry* pAry = &(pPsd->aPropVal);

	DasAry_append(pAry, (ubyte*) sChars, len);
}

/* Formerly nested function "end" in parseStreamDescriptor */
void parseStreamDesc_end(void* data, const char* el)
{
	parse_stream_desc_t* pPsd = (parse_stream_desc_t*)data;

	if(strcmp(el, "frame") == 0){
		// Close the frame
		pPsd->pFrame = NULL;  
		return;
	}

	if(strcmp(el, "p") != 0)
		return;

	pPsd->bInProp = false;

	DasAry* pAry = &(pPsd->aPropVal);
	DasAry_append(pAry, NULL, 1);  // Null terminate the value string
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

StreamDesc* new_StreamDesc_str(DasBuf* pBuf)
{
	StreamDesc* pThis = new_StreamDesc();

	/*StreamDesc* pDesc;
	DasErrCode nRet;
	char sPropUnits[_UNIT_BUF_SZ+1];
	char sPropName[_NAME_BUF_SZ+1];
	char sPropType[_TYPE_BUF_SZ+1];
	DasAry aPropVal;
	*/

	parse_stream_desc_t psd;
	memset(&psd, 0, sizeof(psd));
	psd.pStream = pThis;

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
	XML_SetElementHandler(p, parseStreamDesc_start, parseStreamDesc_end);
	XML_SetCharacterDataHandler(p, parseStreamDesc_chardata);
	
	int nParRet = XML_Parse(p, pBuf->pReadBeg, DasBuf_unread(pBuf), true);
	XML_ParserFree(p);
	
	if(!nParRet){
		das_error(DASERR_STREAM, "Parse error at line %d:\n%s\n",
		           XML_GetCurrentLineNumber(p), XML_ErrorString(XML_GetErrorCode(p))
		);
		del_StreamDesc(pThis);           // Don't leak on fail
		DasAry_deInit(&(psd.aPropVal));
		return NULL;
	}
	if(psd.nRet != 0){
		del_StreamDesc(pThis);           // Don't leak on fail
		DasAry_deInit(&(psd.aPropVal));
		return NULL;
	}
	
	return pThis;
}

DasErrCode StreamDesc_encode(StreamDesc* pThis, DasBuf* pBuf)
{
	DasErrCode nRet = 0;
	if((nRet = DasBuf_printf(pBuf, "<stream ")) !=0 ) return nRet;
	
	if(pThis->compression[0] != '\0') {
		nRet = DasBuf_printf(pBuf, "compression=\"%s\" ", pThis->compression);
		if(nRet != 0) return nRet;	
	}

	if(strcmp(pThis->version, DAS_22_STREAM_VER) != 0){
		if( (nRet = DasBuf_printf(pBuf, "type=\"%s\"", pThis->type)) != 0)
			return nRet;	
	}
	
	if( (nRet = DasBuf_printf(pBuf, "version=\"%s\"", pThis->version)) != 0)
		return nRet;
	
	if( (nRet = DasBuf_printf(pBuf, " >\n" ) ) != 0) return nRet;
	
	nRet = DasDesc_encode((DasDesc*)pThis, pBuf, "  ");
	if(nRet != 0) return nRet;
	
	return DasBuf_printf(pBuf, "</stream>\n");
}

/* Factory function */
DasDesc* DasDesc_decode(DasBuf* pBuf, StreamDesc* pSd, int nPktId)
{
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
	
   if(strcmp(sName, "stream") == 0)
		return (DasDesc*) new_StreamDesc_str(pBuf);
	
   if(strcmp(sName, "packet") == 0)
		return (DasDesc*) new_PktDesc_xml(pBuf, pSd, nPktId);

	if(strcmp(sName, "dataset") == 0)
		return (DasDesc*) dasds_from_xmlheader(3, pBuf, pSd, nPktId);
	
	das_error(DASERR_STREAM, "Unknown top-level descriptor object: %s", sName);
	return NULL;
}
