/* Copyright (C) 2004-2017 Jeremy Faden <jeremy-faden@uiowa.edu>
 *                         Chris Piker <chris-piker@uiowa.edu>
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
#include <time.h>
#include <limits.h>
#include <stdio.h>
#ifdef __unix
#include <sys/types.h>
#include <unistd.h>
#endif
#include <ctype.h>

#include <expat.h>

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
	for(size_t u = 1; pThis->pktDesc[u] != NULL; u++){
		del_PktDesc(pThis->pktDesc[u]);
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
	for(size_t u = 1; u < MAX_PKTIDS; u++) if(pThis->pktDesc[u]) nRet++;
	return nRet;
}

int StreamDesc_nextPktId(StreamDesc* pThis)
{
    int i;
    for ( i=1; i<MAX_PKTIDS; i++ ) { /* 00 is reserved for stream descriptor */
        if ( pThis->pktDesc[i]==NULL ) return i;
    }
    return -1 * das_error(19, "Ran out of Packet IDs only 99 allowed!" );
}

PktDesc* StreamDesc_createPktDesc(StreamDesc* pThis, DasEncoding* pXEncoder, 
		                            das_units xUnits )
{
    PktDesc* pPkt;

    pPkt= new_PktDesc();
    pPkt->id= StreamDesc_nextPktId(pThis);
	pPkt->base.parent=(DasDesc*)pThis;
	 
	PlaneDesc* pX = new_PlaneDesc(X, "", pXEncoder, xUnits);
	PktDesc_addPlane(pPkt, pX);
    pThis->pktDesc[pPkt->id]= pPkt;
	
    return pPkt;
}

DasErrCode StreamDesc_freePktDesc(StreamDesc* pThis, int nPktId)
{
	if(!StreamDesc_isValidId(pThis, nPktId))
		return das_error(19, "%s: stream contains no descriptor for packets "
		                  "with id %d", __func__, nPktId);
	del_PktDesc(pThis->pktDesc[nPktId]);
	pThis->pktDesc[nPktId]= NULL;
	return 0;
}

PktDesc* StreamDesc_getPktDesc(const StreamDesc* pThis, int nPacketId)
{
	if(nPacketId < 1 || nPacketId > 99)
		das_error(19, "ERROR: Illegal Packet ID %d in getPacketDescriptor",
				          nPacketId);
	return pThis->pktDesc[nPacketId];
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
    pThis->pktDesc[id]= pPdOut;

    pPdOut->id= id;
	 
	 PktDesc_copyPlanes(pPdOut, pPdIn);  /* Realloc's the data buffer */

    return pPdOut;
}

bool StreamDesc_isValidId(const StreamDesc* pThis, int nPktId)
{
	if(nPktId > 0 && nPktId < MAX_PKTIDS){
		if(pThis->pktDesc[nPktId] != NULL) return true;
	}
	return false;
}

PktDesc* StreamDesc_clonePktDescById(
	StreamDesc* pThis, const StreamDesc* pOther, int nPacketId
){
	PktDesc* pIn, *pOut;

	pIn = StreamDesc_getPktDesc(pOther, nPacketId);

	if(pThis->pktDesc[pIn->id] != NULL){
		das_error(19, "ERROR: Stream descriptor already has a packet "
		                "descriptor with id %d", nPacketId); 
		return NULL;
	}
	
	pOut = new_PktDesc();
	pOut->base.parent = (DasDesc*)pThis;
	
	DasDesc_copyIn((DasDesc*)pOut, (DasDesc*)pIn);
	
	pOut->id = pIn->id;
	pThis->pktDesc[pIn->id] = pOut;
	
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
		return das_error(19, "Packet Descriptor already belongs to different "
				                "stream");
	
	if(pPd->base.parent == (DasDesc*)pThis) 
		return das_error(19, "Packet Descriptor is already part of the stream");
	
	if(nPktId < 1 || nPktId > 99)
		return das_error(19, "Illegal packet id in addPktDesc: %02d", nPktId);
	
	if(pThis->pktDesc[nPktId] != NULL) 
		return das_error(19, "StreamDesc already has a packet descriptor with ID"
				" %02d", nPktId);
	
	pThis->pktDesc[nPktId] = pPd;
	pPd->id = nPktId;
	pPd->base.parent = (DasDesc*)pThis;
	return 0;
}

/* ************************************************************************* */
/* Serializing */

typedef struct parse_stream_desc{
	StreamDesc* pDesc;
	DasErrCode nRet;
}parse_stream_desc_t;

/* Formerly nested function "start" in parseStreamDescriptor */
void parseStreamDesc_start( void *data, const char *el, const char **attr)
{
	int i;
	parse_stream_desc_t* pPsd = (parse_stream_desc_t*)data;
	StreamDesc* pSd = pPsd->pDesc;
	char sType[64] = {'\0'};
	char sName[64] = {'\0'};
	const char* pColon = NULL;
	  
	for(i=0; attr[i]; i+=2) {
		if (strcmp(el, "stream")==0) {
			if ( strcmp(attr[i], "compression")== 0 ) {
				strncpy( pSd->compression, (char*)attr[i+1], STREAMDESC_CMP_SZ-1);
				continue;
			}
			
			if ( strcmp(attr[i], "version")== 0 ) {
				strncpy( pSd->version, (char*)attr[i+1], STREAMDESC_VER_SZ-1);
				if(strcmp(pSd->version, DAS_22_STREAM_VER) > 0){
					fprintf(stderr, "Warning: Stream is version %s, expected %s, "
							  "some features might not be supported", pSd->version, 
							DAS_22_STREAM_VER);
				}
				continue;
			}
			
			fprintf( stderr, "ignoring attribute of stream tag: %s\n", attr[i]);
			continue;
		} 
		
		if(strcmp( el, "properties" ) == 0){
			if( (pColon = strchr(attr[i], ':')) != NULL){
				memset(sType, '\0', 64);
				strncpy(sType, attr[i], pColon - attr[i]);
				strncpy(sName, pColon+1, 63);
				DasDesc_set( (DasDesc*)pSd, sType, sName, attr[i+1] );
			}
			else{
				DasDesc_set( (DasDesc*)pSd, "String", attr[i], attr[i+1]);
			}
			
			continue;
		} 
		
		pPsd->nRet = das_error(19, "Invalid element <%s> in <stream> section", el);
		break;
	}
}

/* Formerly nested function "end" in parseStreamDescriptor */
void parseStreamDesc_end(void *data, const char *el) {
}

StreamDesc* new_StreamDesc_str(DasBuf* pBuf)
{
	StreamDesc* pThis = new_StreamDesc();
	parse_stream_desc_t psd = {pThis, 0};
	
	XML_Parser p = XML_ParserCreate("UTF-8");
	if(!p){
		das_error(19, "couldn't create xml parser\n");
		return NULL;
	}
	XML_SetUserData(p, (void*) &psd);
	XML_SetElementHandler(p, parseStreamDesc_start, parseStreamDesc_end);
	
	int nParRet = XML_Parse(p, pBuf->pReadBeg, DasBuf_unread(pBuf), true);
	XML_ParserFree(p);
	
	if(!nParRet){
		das_error(19, "Parse error at line %d:\n%s\n",
		           XML_GetCurrentLineNumber(p), XML_ErrorString(XML_GetErrorCode(p))
		);
		return NULL;
	}
	if(psd.nRet != 0)
		return NULL;
	else
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
	
	if( (nRet = DasBuf_printf(pBuf, "version=\"%s\"", pThis->version)) != 0)
		return nRet;
	
	if( (nRet = DasBuf_printf(pBuf, " >\n" ) ) != 0) return nRet;
	
	nRet = DasDesc_encode((DasDesc*)pThis, pBuf, "  ");
	if(nRet != 0) return nRet;
	
	return DasBuf_printf(pBuf, "</stream>\n");
}

/* Factory function */
DasDesc* Das2Desc_decode(DasBuf* pBuf)
{
	char sName[DAS_XML_NODE_NAME_LEN] = {'\0'}; 
	
	/* Eat the whitespace on either end */
	DasBuf_strip(pBuf);
	
	if(DasBuf_unread(pBuf) == 0){
		das_error(19, "Empty Descriptor Header in Stream");
		return NULL;
	}
	
	size_t uPos = DasBuf_readOffset(pBuf);
	char b; 
	DasBuf_read(pBuf, &b, 1);

	if(b != '<'){
		das_error(19, "found \"%c\", expected \"<\"", b);
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
			das_error(19, "Error finding the end of the XML prolog, was the"
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
	
	DasBuf_setReadOffset(pBuf, uPos);
	
   if(strcmp(sName, "stream") == 0)
		return (DasDesc*) new_StreamDesc_str(pBuf);
	
   if(strcmp(sName, "packet") == 0)
		return (DasDesc*) new_PktDesc_xml(pBuf, NULL, 0);
	
	das_error(19, "Unknown top-level descriptor object: %s", sName);
	return NULL;
}

