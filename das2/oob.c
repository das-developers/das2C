/* Copyright (C) 2015-2017 Chris Piker <chris-piker@uiowa.edu>
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
#include <stdio.h>
#include <ctype.h>

#include <expat.h>

#include "oob.h"

/* ************************************************************************* */
/* Generic Out of Band Objects */

void OutOfBand_clean(OutOfBand* pThis)
{
	(pThis->clean)(pThis);
}

/* ************************************************************************* */
/* Stream Exceptions */

void OobExcept_clean(OutOfBand* bpThis){
	OobExcept* pThis = (OobExcept*)bpThis;
	if(pThis->sType != NULL) free(pThis->sType);
	if(pThis->sMsg != NULL) free(pThis->sMsg);
	pThis->sType = NULL; pThis->uTypeLen = 0;
	pThis->sMsg = NULL; pThis->uMsgLen = 0;
}

void OobExcept_init(OobExcept* pThis)
{
	pThis->base.pkttype = OOB_EXCEPT;
	pThis->base.clean = OobExcept_clean;
	pThis->uTypeLen = 256;
	pThis->uMsgLen = 1024;
	pThis->sType = (char*)calloc(pThis->uTypeLen, sizeof(char));
	pThis->sMsg = (char*)calloc(pThis->uMsgLen, sizeof(char));
}

void OobExcept_set(OobExcept* pThis, const char* sType, const char* sMsg)
{
	pThis->base.pkttype = OOB_EXCEPT;
	pThis->base.clean = OobExcept_clean;
	
	pThis->uTypeLen = strlen(sType) + 1;
	pThis->uMsgLen = strlen(sMsg) + 1;
	pThis->sType = (char*)calloc(pThis->uTypeLen, sizeof(char));
	pThis->sMsg = (char*)calloc(pThis->uMsgLen, sizeof(char));
	strncpy(pThis->sType, sType, pThis->uTypeLen - 1);
	strncpy(pThis->sMsg, sMsg, pThis->uMsgLen - 1);
}

typedef struct parse_stream_exception{
	OobExcept* se;
	int errorCode;
	char errorMessage[256];
}parse_stream_exception_t;

void _OobExcept_start(void *data, const char *el, const char **attr){
	int i;  		
	parse_stream_exception_t* pStack = (parse_stream_exception_t*)data;
	OobExcept* pThis = pStack->se;
	
	/* sanity check */
	if(strcmp(el, "exception") !=0 ){
		pStack->errorCode= 20;
		sprintf( pStack->errorMessage, "Logic error %s:%d", __FILE__, __LINE__ );
		return;
	}
				
	for(i = 0; attr[i]; i += 2){
		if( strcmp(attr[i], "message")==0 ){
			das_store_str(&(pThis->sMsg), &(pThis->uMsgLen), attr[i+1]);
			continue;
		}
		
		if( strcmp(attr[i], "type")==0 ){
			das_store_str(&(pThis->sType), &(pThis->uTypeLen), attr[i+1]);
			continue;
		} 
		
		pStack->errorCode= 20;
		snprintf(pStack->errorMessage, 255, "unrecognized tag in exception: %s\n",
		         attr[i]);
	 }
}

void _OobExecpt_end(void *data, const char *el) {
	
}

DasErrCode OobExcept_decode(OobExcept* se, DasBuf* pBuf)
{
	parse_stream_exception_t stack = {0};
	stack.se = se;
	
    XML_Parser p= XML_ParserCreate("UTF-8");
	 XML_SetUserData(p, (void*)&stack);
    XML_SetElementHandler(p, _OobExcept_start, _OobExecpt_end);
    if ( !p ) 
        return das_error(20, "couldn't create xml parser\n" );
    
	 int nParRet = XML_Parse( p, pBuf->pReadBeg, DasBuf_unread(pBuf), 1 );
	 XML_ParserFree(p);
    if ( !nParRet) 
        return das_error(20, "Parse error at offset %ld:\n%s\n",
            XML_GetCurrentByteIndex(p),
            XML_ErrorString(XML_GetErrorCode(p)) );
    
    if ( stack.errorCode!=0 ) 
        return das_error( stack.errorCode, stack.errorMessage );
    
    return 0;
}

DasErrCode OobExcept_encode(OobExcept* pThis, DasBuf* pBuf)
{
	for(int i = 0; i<strlen(pThis->sMsg); i++)
		if(pThis->sMsg[i] == '"') pThis->sMsg[i] = '\'';
	
	return DasBuf_printf(pBuf, "<exception type=\"%s\" message=\"%s\" />\n",
			               pThis->sType, pThis->sMsg);
}


/* ************************************************************************* */
/* Stream Comments */

void OobComment_clean(OutOfBand* bpThis){
	OobComment* pThis = (OobComment*)bpThis;
	if(pThis->sType != NULL) free(pThis->sType);
	if(pThis->sVal != NULL) free(pThis->sVal);
	if(pThis->sSrc != NULL) free(pThis->sSrc);
	pThis->sType = NULL; pThis->uTypeLen = 0;
	pThis->sVal = NULL; pThis->uValLen = 0;
	pThis->sSrc = NULL; pThis->uSrcLen = 0;
}

void OobComment_init(OobComment* pThis)
{
	pThis->base.pkttype = OOB_COMMENT;
	pThis->base.clean = OobComment_clean;
	pThis->uTypeLen = 256;
	pThis->uValLen = 1024;
	pThis->uSrcLen = 256;
	pThis->sType = (char*)calloc(pThis->uTypeLen, sizeof(char));
	pThis->sVal = (char*)calloc(pThis->uValLen, sizeof(char));
	pThis->sSrc = (char*)calloc(pThis->uSrcLen, sizeof(char));
}

typedef struct parse_stream_comment{
	OobComment* sc;
	int errorCode;
	char errorMessage[256];
}parse_stream_comment_t;


void _OobComment_start(void *data, const char *el, const char **attr) 
{	
	int i;  		
	parse_stream_comment_t* pStack = (parse_stream_comment_t*)data;
	OobComment* pThis = pStack->sc;
	
	/* sanity check */
	if(strcmp(el, "comment") !=0 ){
		pStack->errorCode= 20;
		sprintf( pStack->errorMessage, "Logic error %s:%d", __FILE__, __LINE__ );
		return;
	}
				
	for(i = 0; attr[i]; i += 2){
		if( strcmp(attr[i], "type")==0 ){
			das_store_str(&(pThis->sType), &(pThis->uTypeLen), attr[i+1]);
			continue;
		}
		if( strcmp(attr[i], "value")==0 ){
			das_store_str(&(pThis->sVal), &(pThis->uValLen), attr[i+1]);
			continue;
		}
		if( strcmp(attr[i], "source")==0 ){
			das_store_str(&(pThis->sSrc), &(pThis->uSrcLen), attr[i+1]);
			continue;
		}
		pStack->errorCode= 20;
		snprintf(pStack->errorMessage, 255, "unrecognized tag in comment: %s\n",
		         attr[i]);
	 }
}

void _OobComment_end(void *data, const char *el) {
}

DasErrCode OobComment_decode(OobComment* pSc, DasBuf* pBuf)
{
	parse_stream_comment_t stack = {0};
	stack.sc = pSc;
	stack.errorCode = 0;

	XML_Parser p= XML_ParserCreate("UTF-8");
	XML_SetUserData(p, &stack);
	XML_SetElementHandler(p, _OobComment_start, _OobComment_end);
	if ( !p ) {
		return das_error(20, "couldn't create xml parser\n" );
	}
	int nParRet = XML_Parse( p, pBuf->pReadBeg, DasBuf_unread(pBuf), 1 );
	XML_ParserFree(p);
	if (!nParRet) 
		return das_error(20, "Parse error at offset %ld:\n%s\n",
		                  XML_GetCurrentByteIndex(p),
		                  XML_ErrorString(XML_GetErrorCode(p)) );
    

	if ( stack.errorCode!=0 ) 
		return das_error(stack.errorCode, stack.errorMessage );
    
	return 0;
}

DasErrCode OobComment_encode(OobComment* pSc, DasBuf* pBuf)
{
	int nRet = 0;
	
	size_t uValLen = strlen(pSc->sVal);
	
	/* Do this all on one line if the comment value is less than 40 chars*/
   nRet = DasBuf_printf(pBuf, "<comment type=\"%s\" source=\"%s\"", 
	                    pSc->sType, pSc->sSrc);
	if(nRet != 0) return nRet;
	
	if(uValLen > 40) {
		nRet = DasBuf_printf(pBuf, "\n  ");
		if(nRet != 0) return nRet;
	}
	
	/* Replace any " characters in the message with ' */
	for(int i = 0; i<strlen(pSc->sVal); i++)
		if(pSc->sVal[i] == '"') pSc->sVal[i] = '\'';
	
	nRet = DasBuf_printf(pBuf, " value=\"%s\"", pSc->sVal);
	if(nRet != 0) return nRet;
	
	if(uValLen > 40)
		return DasBuf_printf(pBuf, "\n/>\n");
	else
		return DasBuf_printf(pBuf, " />\n");
}

/* ************************************************************************* */
/* Generic XX packet parser */
DasErrCode OutOfBand_decode(DasBuf* pBuf, OutOfBand** ppObjs, int* which)
{
	char sName[DAS_XML_NODE_NAME_LEN] = {'\0'}; 
	
	/* Eat the whitespace on either end */
	DasBuf_strip(pBuf);
	
	if(DasBuf_unread(pBuf) == 0)
		return das_error(19, "Empty out-of-Band packet in Stream");
	
	size_t uPos = DasBuf_readOffset(pBuf);
	char b; 
	if(DasBuf_read(pBuf, &b, 1) < 1) 
		return das_error(19, "%s: Error reading out-of-band packet", __func__);
	
	if(b != '<'){
		return das_error(19, "found \"%c\", expected \"<\"", b);
	}
	
	int i = 0;
	while(DasBuf_read(pBuf, &b, 1) == 1     &&
	      i < (DAS_XML_NODE_NAME_LEN - 1) && 
	      !isspace(b) && b != '\0' && b != '>' && b != '/'){
		
		sName[i] = b;
		i++;
	}
	
	DasBuf_setReadOffset(pBuf, uPos);
	
   if(strcmp(sName, "comment") == 0){
		for(i = 0; ppObjs[i] != NULL; i++){
			if(ppObjs[i]->pkttype == OOB_COMMENT){
				*which = i;
				return OobComment_decode((OobComment*)ppObjs[i], pBuf);
			}
		}
	}
	else{
		if(strcmp(sName, "exception") == 0){
			for(i = 0; ppObjs[i] != NULL; i++){
				if(ppObjs[i]->pkttype == OOB_EXCEPT){
					*which = i;
					return OobExcept_decode((OobExcept*)ppObjs[i], pBuf);
				}
			}
		}
	}
	
	*which = -1;
	return 0;
}
