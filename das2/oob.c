/* Copyright (C) 2015-2026 Chris Piker <chris-piker@uiowa.edu>
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

#include <string.h>
#include <stdio.h>
#include <ctype.h>

#include <expat.h>

#include "oob.h"

/* Maps except_t enum values to wire type strings, indexed [version][except_t].
 * version 0 = das2.2, version 1 = das3.0
 * Column order mirrors the enum: UNKNOWN, NO_DATA, SERVER_ERR, QUERY_ERR
 */
static const char* g_aExceptTypes[2][4] = {
	{"Unknown", "NoDataInInterval", "ServerError", "IllegalArgument"},
	{"Unknown", "NoMatchingData",   "ServerError", "QueryError"}
};

/* Search both version rows; returns UNKNOWN for unrecognised strings. */
static except_t _parse_except_type(const char* s)
{
	for(int iType = 0; iType < 4; ++iType)
		for(int iVer = 0; iVer < 2; ++iVer)
			if(strcmp(s, g_aExceptTypes[iVer][iType]) == 0)
				return (except_t)iType;
	return DAS_EX_UNKNOWN;
}

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
	if(pThis->sMsg != NULL) free(pThis->sMsg);
	pThis->sMsg = NULL; pThis->uMsgLen = 0;
}

void OobExcept_init(OobExcept* pThis)
{
	pThis->base.pkttype = OOB_EXCEPT;
	pThis->base.clean = OobExcept_clean;
	pThis->nType = DAS_EX_UNKNOWN;
	pThis->uMsgLen = 1024;
	pThis->sMsg = (char*)calloc(pThis->uMsgLen, sizeof(char));
}

void OobExcept_set(OobExcept* pThis, except_t nType, const char* sMsg)
{
	pThis->base.pkttype = OOB_EXCEPT;
	pThis->base.clean = OobExcept_clean;
	pThis->nType = nType;
	pThis->uMsgLen = strlen(sMsg) + 1;
	pThis->sMsg = (char*)calloc(pThis->uMsgLen, sizeof(char));
	strncpy(pThis->sMsg, sMsg, pThis->uMsgLen - 1);
}

typedef struct parse_stream_exception{
	OobExcept* se;
	int errorCode;
	char errorMessage[256];
	char sTxtBuf[4096];   /* accumulates das3 element body text */
	size_t uTxtLen;
}parse_stream_exception_t;

void _OobExcept_start(void *data, const char *el, const char **attr){
	int i;
	parse_stream_exception_t* pStack = (parse_stream_exception_t*)data;
	OobExcept* pThis = pStack->se;

	/* sanity check */
	if(strcmp(el, "exception") != 0){
		pStack->errorCode = 20;
		sprintf(pStack->errorMessage, "Logic error %s:%d", __FILE__, __LINE__);
		return;
	}

	for(i = 0; attr[i]; i += 2){
		if(strcmp(attr[i], "message") == 0){
			/* das2.2 style: message in attribute */
			das_store_str(&(pThis->sMsg), &(pThis->uMsgLen), attr[i+1]);
			continue;
		}
		if(strcmp(attr[i], "type") == 0){
			pThis->nType = _parse_except_type(attr[i+1]);
			continue;
		}
		/* das3.0 permits arbitrary extra attributes; skip silently */
	}
}

/* Accumulate element body text for das3.0 style <exception>msg</exception> */
void _OobExcept_chardata(void *data, const XML_Char *s, int len){
	parse_stream_exception_t* pStack = (parse_stream_exception_t*)data;
	size_t uRoom = sizeof(pStack->sTxtBuf) - 1 - pStack->uTxtLen;
	size_t uCopy = (size_t)len < uRoom ? (size_t)len : uRoom;
	if(uCopy > 0){
		memcpy(pStack->sTxtBuf + pStack->uTxtLen, s, uCopy);
		pStack->uTxtLen += uCopy;
		pStack->sTxtBuf[pStack->uTxtLen] = '\0';
	}
}

void _OobExcept_end(void *data, const char *el){
	parse_stream_exception_t* pStack = (parse_stream_exception_t*)data;
	OobExcept* pThis = pStack->se;

	/* das3.0: body text is the message; only use it when the das2.2
	   'message' attribute was not present (sMsg still empty) */
	if(pStack->uTxtLen > 0 && pThis->sMsg != NULL && pThis->sMsg[0] == '\0')
		das_store_str(&(pThis->sMsg), &(pThis->uMsgLen), pStack->sTxtBuf);
}

const char* OobExcept_typeStr(const OobExcept* pThis)
{
	return g_aExceptTypes[1][pThis->nType];
}

DasErrCode OobExcept_decode(OobExcept* se, DasBuf* pBuf)
{
	parse_stream_exception_t stack = {0};
	stack.se = se;

	XML_Parser p = XML_ParserCreate("UTF-8");
	if(!p)
		return das_error(20, "couldn't create xml parser\n");
	XML_SetUserData(p, (void*)&stack);
	XML_SetElementHandler(p, _OobExcept_start, _OobExcept_end);
	XML_SetCharacterDataHandler(p, _OobExcept_chardata);

	int nParRet = XML_Parse(p, pBuf->pReadBeg, DasBuf_unread(pBuf), 1);
	XML_ParserFree(p);
	if(!nParRet)
		return das_error(20, "Parse error at offset %ld:\n%s\n",
			XML_GetCurrentByteIndex(p), XML_ErrorString(XML_GetErrorCode(p)));

	if(stack.errorCode != 0)
		return das_error(stack.errorCode, stack.errorMessage);

	return DAS_OKAY;
}

/* Replace XML special chars in short identifier fields (type, source) with '_'.
   These fields should never contain markup; stomping is preferable to escaping
   because it makes the corruption visible rather than silently round-tripping. */
static void _stomp_xml_attr(char* s)
{
	for(; *s != '\0'; ++s)
		if(*s == '"' || *s == '\'' || *s == '<' || *s == '>' || *s == '&')
			*s = '_';
}

/* XML-escape a value string into a scratch buffer.  Uses the caller's 256-byte
   stack buffer for short inputs; falls back to malloc for longer ones.
   The caller must free *ppAlloc when it is non-NULL after use. */
static const char* _xml_escape_val(
    const char* src, char stk[256], char** ppAlloc
){
	*ppAlloc = NULL;
	if(strlen(src) < 128){
		das_xml_escape(stk, src, 256);
		return stk;
	}
	size_t uSz = strlen(src) * 6 + 2;
	*ppAlloc = malloc(uSz);
	if(!*ppAlloc){
		/* malloc failed: encode into stack buffer, value will be truncated */
		das_xml_escape(stk, src, 256);
		return stk;
	}
	das_xml_escape(*ppAlloc, src, uSz);
	return *ppAlloc;
}

DasErrCode OobExcept_encode(OobExcept* pThis, DasBuf* pBuf)
{
	/* das2.2 format: type and message both in attributes.
	   Type string comes from the table — no escaping needed, all entries are safe. */
	const char* sType = g_aExceptTypes[0][pThis->nType];

	char sMsgStk[256]; char* pMsgAlloc;
	const char* pMsg = _xml_escape_val(pThis->sMsg, sMsgStk, &pMsgAlloc);

	DasErrCode nRet = DasBuf_printf(pBuf,
		"<exception type=\"%s\" message=\"%s\" />\n", sType, pMsg);

	if(pMsgAlloc) free(pMsgAlloc);
	return nRet;
}

DasErrCode OobExcept_encode3(OobExcept* pThis, DasBuf* pBuf)
{
	/* das3.0 format: type in attribute, message as element body text */
	const char* sType = g_aExceptTypes[1][pThis->nType];

	char sMsgStk[256]; char* pMsgAlloc;
	const char* pMsg = _xml_escape_val(pThis->sMsg, sMsgStk, &pMsgAlloc);

	DasErrCode nRet = DasBuf_printf(pBuf,
		"<exception type=\"%s\">%s</exception>\n", sType, pMsg);

	if(pMsgAlloc) free(pMsgAlloc);
	return nRet;
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
	char sTxtBuf[4096];   /* accumulates das3 element body text */
	size_t uTxtLen;
}parse_stream_comment_t;


void _OobComment_start(void *data, const char *el, const char **attr)
{
	int i;
	parse_stream_comment_t* pStack = (parse_stream_comment_t*)data;
	OobComment* pThis = pStack->sc;

	/* sanity check */
	if(strcmp(el, "comment") != 0){
		pStack->errorCode = 20;
		sprintf(pStack->errorMessage, "Logic error %s:%d", __FILE__, __LINE__);
		return;
	}

	for(i = 0; attr[i]; i += 2){
		if(strcmp(attr[i], "type") == 0){
			das_store_str(&(pThis->sType), &(pThis->uTypeLen), attr[i+1]);
			continue;
		}
		if(strcmp(attr[i], "value") == 0){
			/* das2.2 style: value in attribute */
			das_store_str(&(pThis->sVal), &(pThis->uValLen), attr[i+1]);
			continue;
		}
		if(strcmp(attr[i], "source") == 0){
			das_store_str(&(pThis->sSrc), &(pThis->uSrcLen), attr[i+1]);
			continue;
		}
		/* das3.0 permits arbitrary extra attributes; skip silently */
	}
}

/* Accumulate element body text for das3.0 style <comment>val</comment> */
void _OobComment_chardata(void *data, const XML_Char *s, int len){
	parse_stream_comment_t* pStack = (parse_stream_comment_t*)data;
	size_t uRoom = sizeof(pStack->sTxtBuf) - 1 - pStack->uTxtLen;
	size_t uCopy = (size_t)len < uRoom ? (size_t)len : uRoom;
	if(uCopy > 0){
		memcpy(pStack->sTxtBuf + pStack->uTxtLen, s, uCopy);
		pStack->uTxtLen += uCopy;
		pStack->sTxtBuf[pStack->uTxtLen] = '\0';
	}
}

void _OobComment_end(void *data, const char *el){
	parse_stream_comment_t* pStack = (parse_stream_comment_t*)data;
	OobComment* pThis = pStack->sc;

	/* das3.0: body text is the value; only use it when the das2.2
	   'value' attribute was not present (sVal still empty) */
	if(pStack->uTxtLen > 0 && pThis->sVal != NULL && pThis->sVal[0] == '\0')
		das_store_str(&(pThis->sVal), &(pThis->uValLen), pStack->sTxtBuf);
}

DasErrCode OobComment_decode(OobComment* pSc, DasBuf* pBuf)
{
	parse_stream_comment_t stack = {0};
	stack.sc = pSc;
	stack.errorCode = 0;

	XML_Parser p = XML_ParserCreate("UTF-8");
	if(!p)
		return das_error(20, "couldn't create xml parser\n");
	XML_SetUserData(p, &stack);
	XML_SetElementHandler(p, _OobComment_start, _OobComment_end);
	XML_SetCharacterDataHandler(p, _OobComment_chardata);

	int nParRet = XML_Parse(p, pBuf->pReadBeg, DasBuf_unread(pBuf), 1);
	XML_ParserFree(p);
	if(!nParRet)
		return das_error(20, "Parse error at offset %ld:\n%s\n",
			XML_GetCurrentByteIndex(p), XML_ErrorString(XML_GetErrorCode(p)));

	if(stack.errorCode != 0)
		return das_error(stack.errorCode, stack.errorMessage);

	return DAS_OKAY;
}

DasErrCode OobComment_encode(OobComment* pSc, DasBuf* pBuf)
{
	_stomp_xml_attr(pSc->sType);
	_stomp_xml_attr(pSc->sSrc);

	char sValStk[256]; char* pValAlloc;
	const char* pVal = _xml_escape_val(pSc->sVal, sValStk, &pValAlloc);

	int nRet = 0;
	size_t uValLen = strlen(pVal);

	/* Do this all on one line if the comment value is less than 40 chars */
	nRet = DasBuf_printf(pBuf, "<comment type=\"%s\" source=\"%s\"",
		pSc->sType, pSc->sSrc);
	if(nRet != 0) goto cleanup;

	if(uValLen > 40){
		nRet = DasBuf_printf(pBuf, "\n  ");
		if(nRet != 0) goto cleanup;
	}

	nRet = DasBuf_printf(pBuf, " value=\"%s\"", pVal);
	if(nRet != 0) goto cleanup;

	nRet = DasBuf_printf(pBuf, uValLen > 40 ? "\n/>\n" : " />\n");

cleanup:
	if(pValAlloc) free(pValAlloc);
	return nRet;
}

DasErrCode OobComment_encode3(OobComment* pSc, DasBuf* pBuf)
{
	/* das3.0 format: type and source in attributes, value as element body text.
	   Omit the source attribute when the field is empty. */
	_stomp_xml_attr(pSc->sType);
	_stomp_xml_attr(pSc->sSrc);

	char sValStk[256]; char* pValAlloc;
	const char* pVal = _xml_escape_val(pSc->sVal, sValStk, &pValAlloc);

	DasErrCode nRet;
	if(pSc->sSrc[0] != '\0')
		nRet = DasBuf_printf(pBuf,
			"<comment type=\"%s\" source=\"%s\">%s</comment>\n",
			pSc->sType, pSc->sSrc, pVal);
	else
		nRet = DasBuf_printf(pBuf,
			"<comment type=\"%s\">%s</comment>\n",
			pSc->sType, pVal);

	if(pValAlloc) free(pValAlloc);
	return nRet;
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

	if(b != '<')
		return das_error(19, "found \"%c\", expected \"<\"", b);

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
	return DAS_OKAY;
}
