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
#include <stdlib.h>
#include <stdio.h>
#include <ctype.h>

#include <expat.h>

#include "oob.h"
#include "str.h"
#include "oob_p.h"
#include "util_p.h"

/* Maps das_excepttype enum values to wire type strings, indexed [version][das_excepttype].
 * version 0 = das2.2, version 1 = das3.0
 * Column order mirrors the enum: UNKNOWN, NO_DATA, SERVER_ERR, QUERY_ERR
 */
static const char* g_aExceptTypes[2][4] = {
	{"Unknown", "NoDataInInterval", "ServerError", "IllegalArgument"},
	{"Unknown", "NoMatchingData",   "ServerError", "QueryError"}
};

const char* dasexcepttype_name(das_excepttype nType, int nDasVer)
{
	if((nType < DASEXCEPTTYPE_UNKNOWN)||(nType > DASEXCEPTTYPE_QUERY_ERR)) return NULL;
	if((nDasVer < 2)||(nDasVer > 3)) return NULL;
	return g_aExceptTypes[nDasVer - 2][nType];
}

/* Search both version rows, see the header for why that is safe */
das_excepttype dasexcepttype_id(const char* sType)
{
	if(sType == NULL) return DASEXCEPTTYPE_UNKNOWN;
	for(int iType = 0; iType < 4; ++iType)
		for(int iVer = 0; iVer < 2; ++iVer)
			if(strcmp(sType, g_aExceptTypes[iVer][iType]) == 0)
				return (das_excepttype)iType;
	return DASEXCEPTTYPE_UNKNOWN;
}

/* An nType outside the enum, cast in by a caller of new_DasExcept(), writes
   as Unknown rather than reading past the table */
static const char* _except_wireType(const DasExcept* pThis, int nDasVer)
{
	const char* sType = dasexcepttype_name(pThis->nType, nDasVer);
	return (sType == NULL) ? "Unknown" : sType;
}

/* ************************************************************************* */
/* Stream Exceptions */

void init_DasExcept(DasExcept* pThis)
{
	pThis->base.pkttype = OOB_EXCEPT;
	pThis->nType = DASEXCEPTTYPE_UNKNOWN;
	pThis->uMsgLen = 1024;
	pThis->sMsg = (char*)calloc(pThis->uMsgLen, sizeof(char));
}

void fini_DasExcept(DasExcept* pThis)
{
	if(pThis->sMsg != NULL) free(pThis->sMsg);
	pThis->sMsg = NULL; pThis->uMsgLen = 0;
}

DasExcept* new_DasExcept(das_excepttype nType, const char* sMsg)
{
	if(sMsg == NULL) sMsg = "";

	DasExcept* pThis = (DasExcept*)calloc(1, sizeof(DasExcept));
	if(pThis == NULL) return NULL;

	pThis->base.pkttype = OOB_EXCEPT;
	pThis->nType = nType;
	pThis->uMsgLen = strlen(sMsg) + 1;
	if((pThis->sMsg = (char*)malloc(pThis->uMsgLen)) == NULL){
		free(pThis);
		return NULL;
	}
	memcpy(pThis->sMsg, sMsg, pThis->uMsgLen);
	return pThis;
}

void del_DasExcept(DasExcept* pThis)
{
	if(pThis == NULL) return;
	fini_DasExcept(pThis);
	free(pThis);
}

das_excepttype DasExcept_type(const DasExcept* pThis){ return pThis->nType; }

const char* DasExcept_typeStr3(const DasExcept* pThis)
{
	return _except_wireType(pThis, 3);
}

const char* DasExcept_typeStr2(const DasExcept* pThis)
{
	return _except_wireType(pThis, 2);
}

const char* DasExcept_msg(const DasExcept* pThis)
{
	return (pThis->sMsg == NULL) ? "" : pThis->sMsg;
}

typedef struct parse_stream_exception{
	DasExcept* se;
	int errorCode;
	char errorMessage[256];
	char sTxtBuf[4096];   /* accumulates das3 element body text */
	size_t uTxtLen;
}parse_stream_exception_t;

static void _OobExcept_start(void *data, const char *el, const char **attr){
	int i;
	parse_stream_exception_t* pStack = (parse_stream_exception_t*)data;
	DasExcept* pThis = pStack->se;

	/* sanity check */
	if(strcmp(el, "exception") != 0){
		pStack->errorCode = 20;
		sprintf(pStack->errorMessage, "Logic error %s:%d", __FILE__, __LINE__);
		return;
	}

	/* Same object for every exception in a stream, see _OobComment_start */
	pThis->nType = DASEXCEPTTYPE_UNKNOWN;
	if(pThis->sMsg != NULL) pThis->sMsg[0] = '\0';

	for(i = 0; attr[i]; i += 2){
		if(strcmp(attr[i], "message") == 0){
			/* das2.2 style: message in attribute */
			das_store_str(&(pThis->sMsg), &(pThis->uMsgLen), attr[i+1]);
			continue;
		}
		if(strcmp(attr[i], "type") == 0){
			pThis->nType = dasexcepttype_id(attr[i+1]);
			continue;
		}
		/* das3.0 permits arbitrary extra attributes; skip silently */
	}
}

/* Accumulate element body text for das3.0 style <exception>msg</exception> */
static void _OobExcept_chardata(void *data, const XML_Char *s, int len){
	parse_stream_exception_t* pStack = (parse_stream_exception_t*)data;
	size_t uRoom = sizeof(pStack->sTxtBuf) - 1 - pStack->uTxtLen;
	size_t uCopy = (size_t)len < uRoom ? (size_t)len : uRoom;
	if(uCopy > 0){
		memcpy(pStack->sTxtBuf + pStack->uTxtLen, s, uCopy);
		pStack->uTxtLen += uCopy;
		pStack->sTxtBuf[pStack->uTxtLen] = '\0';
	}
}

static void _OobExcept_end(void *data, const char *el){
	parse_stream_exception_t* pStack = (parse_stream_exception_t*)data;
	DasExcept* pThis = pStack->se;

	/* das3.0: body text is the message; only use it when the das2.2
	   'message' attribute was not present (sMsg still empty) */
	if(pStack->uTxtLen > 0 && pThis->sMsg != NULL && pThis->sMsg[0] == '\0')
		das_store_str(&(pThis->sMsg), &(pThis->uMsgLen), pStack->sTxtBuf);
}

das_errcode OobExcept_decode(DasExcept* se, DasBuf* pBuf)
{
	parse_stream_exception_t stack = {0};
	stack.se = se;

	XML_Parser p = XML_ParserCreate("UTF-8");
	if(!p)
		return daserrf(20, "couldn't create xml parser\n");
	XML_SetUserData(p, (void*)&stack);
	XML_SetElementHandler(p, _OobExcept_start, _OobExcept_end);
	XML_SetCharacterDataHandler(p, _OobExcept_chardata);

	size_t uXml = 0;
	const char* sXml = (const char*)DasBuf_data(pBuf, &uXml);
	int nParRet = XML_Parse(p, sXml, (int)uXml, 1);
	if(!nParRet){
		/* Report before the free, the offset and code live in the parser */
		das_errcode nRet = daserrf(20, "Parse error at offset %ld:\n%s\n",
			XML_GetCurrentByteIndex(p), XML_ErrorString(XML_GetErrorCode(p)));
		XML_ParserFree(p);
		return nRet;
	}
	XML_ParserFree(p);

	if(stack.errorCode != 0)
		return daserr(stack.errorCode, stack.errorMessage);

	return DAS_OKAY;
}

/* Copy a short identifier field (type, source) with XML special chars
   replaced by '_'.  These fields should never contain markup; stomping is
   preferable to escaping because it makes the corruption visible rather than
   silently round-tripping.  It works on a copy so the encoders can take a
   const object.  Uses the caller's 256-byte stack buffer for short inputs,
   malloc for longer ones; the caller frees *ppAlloc when it is non-NULL.
   Returns NULL only if that malloc fails. */
static const char* _stomp_xml_attr(const char* src, char stk[256], char** ppAlloc)
{
	*ppAlloc = NULL;
	size_t uLen = strlen(src);
	char* pDest = stk;
	if(uLen >= 256){
		if((*ppAlloc = (char*)malloc(uLen + 1)) == NULL) return NULL;
		pDest = *ppAlloc;
	}
	for(size_t u = 0; u <= uLen; ++u){
		char c = src[u];
		pDest[u] = (c == '"' || c == '\'' || c == '<' || c == '>' || c == '&') ? '_' : c;
	}
	return pDest;
}

/* XML-escape a value string into a scratch buffer.  Uses the caller's 256-byte
   stack buffer for short inputs; falls back to malloc for longer ones.
   The caller must free *ppAlloc when it is non-NULL after use. */
static const char* _xml_escape_val(
    const char* src, char stk[256], char** ppAlloc
){
	*ppAlloc = NULL;
	if(strlen(src) < 128){
		dasxml_escape(src, stk, 256);
		return stk;
	}
	size_t uSz = strlen(src) * 6 + 2;
	*ppAlloc = malloc(uSz);
	if(!*ppAlloc){
		/* malloc failed: encode into stack buffer, value will be truncated */
		dasxml_escape(src, stk, 256);
		return stk;
	}
	dasxml_escape(src, *ppAlloc, uSz);
	return *ppAlloc;
}

das_errcode OobExcept_encode(const DasExcept* pThis, DasBuf* pBuf)
{
	/* das2.2 format: type and message both in attributes.
	   Type string comes from the table, no escaping needed, all entries are safe. */
	const char* sType = _except_wireType(pThis, 2);

	char sMsgStk[256]; char* pMsgAlloc;
	const char* pMsg = _xml_escape_val(DasExcept_msg(pThis), sMsgStk, &pMsgAlloc);

	das_errcode nRet = DasBuf_printf(pBuf,
		"<exception type=\"%s\" message=\"%s\" />\n", sType, pMsg);

	if(pMsgAlloc) free(pMsgAlloc);
	return nRet;
}

das_errcode OobExcept_encode3(const DasExcept* pThis, DasBuf* pBuf)
{
	/* das3.0 format: type in attribute, message as element body text */
	const char* sType = _except_wireType(pThis, 3);

	char sMsgStk[256]; char* pMsgAlloc;
	const char* pMsg = _xml_escape_val(DasExcept_msg(pThis), sMsgStk, &pMsgAlloc);

	das_errcode nRet = DasBuf_printf(pBuf,
		"<exception type=\"%s\">%s</exception>\n", sType, pMsg);

	if(pMsgAlloc) free(pMsgAlloc);
	return nRet;
}


/* ************************************************************************* */
/* Stream Comments */

void init_DasComment(DasComment* pThis)
{
	pThis->base.pkttype = OOB_COMMENT;
	pThis->uTypeLen = 256;
	pThis->uValLen = 1024;
	pThis->uSrcLen = 256;
	pThis->sType = (char*)calloc(pThis->uTypeLen, sizeof(char));
	pThis->sVal = (char*)calloc(pThis->uValLen, sizeof(char));
	pThis->sSrc = (char*)calloc(pThis->uSrcLen, sizeof(char));
}

void fini_DasComment(DasComment* pThis)
{
	if(pThis->sType != NULL) free(pThis->sType);
	if(pThis->sVal != NULL) free(pThis->sVal);
	if(pThis->sSrc != NULL) free(pThis->sSrc);
	pThis->sType = NULL; pThis->uTypeLen = 0;
	pThis->sVal = NULL; pThis->uValLen = 0;
	pThis->sSrc = NULL; pThis->uSrcLen = 0;
}

DasComment* new_DasComment(void)
{
	DasComment* pThis = (DasComment*)calloc(1, sizeof(DasComment));
	if(pThis == NULL) return NULL;

	init_DasComment(pThis);
	if((pThis->sType == NULL)||(pThis->sVal == NULL)||(pThis->sSrc == NULL)){
		del_DasComment(pThis);
		return NULL;
	}
	return pThis;
}

void del_DasComment(DasComment* pThis)
{
	if(pThis == NULL) return;
	fini_DasComment(pThis);
	free(pThis);
}

/* The one place the comment value names live; get and set both go
   through it.  NULL for a name not in the table. */
static char** _DasComment_slot(
	const DasComment* pThis, const char* sName, size_t** ppuLen
){
	DasComment* pMut = (DasComment*)pThis;  /* set() is the only writer */
	if(sName == NULL) return NULL;
	if(strcmp(sName, "type") == 0){
		*ppuLen = &(pMut->uTypeLen);  return &(pMut->sType);
	}
	if(strcmp(sName, "source") == 0){
		*ppuLen = &(pMut->uSrcLen);   return &(pMut->sSrc);
	}
	if(strcmp(sName, "value") == 0){
		*ppuLen = &(pMut->uValLen);   return &(pMut->sVal);
	}
	return NULL;
}

das_errcode DasComment_set(DasComment* pThis, const char* sName, const char* sVal)
{
	size_t* puLen = NULL;
	char** psSlot = _DasComment_slot(pThis, sName, &puLen);
	if(psSlot == NULL)
		return daserrf(DASERR_OOB,
			"Unknown comment value name '%s', expected type, source or value",
			sName ? sName : "(null)"
		);

	das_store_str(psSlot, puLen, (sVal == NULL) ? "" : sVal);
	return DAS_OKAY;
}

const char* DasComment_get(const DasComment* pThis, const char* sName)
{
	size_t* puLen = NULL;
	char** psSlot = _DasComment_slot(pThis, sName, &puLen);
	if(psSlot == NULL) return NULL;
	return (*psSlot == NULL) ? "" : *psSlot;
}

typedef struct parse_stream_comment{
	DasComment* sc;
	int errorCode;
	char errorMessage[256];
	char sTxtBuf[4096];   /* accumulates das3 element body text */
	size_t uTxtLen;
}parse_stream_comment_t;


static void _OobComment_start(void *data, const char *el, const char **attr)
{
	int i;
	parse_stream_comment_t* pStack = (parse_stream_comment_t*)data;
	DasComment* pThis = pStack->sc;

	/* sanity check */
	if(strcmp(el, "comment") != 0){
		pStack->errorCode = 20;
		sprintf(pStack->errorMessage, "Logic error %s:%d", __FILE__, __LINE__);
		return;
	}

	/* DasIO decodes every comment into the same object.  Clear the last one's
	   fields, otherwise the "only if sVal is empty" body text rule in
	   _OobComment_end never fires again, and a missing source attribute
	   would report the previous comment's source. */
	if(pThis->sType != NULL) pThis->sType[0] = '\0';
	if(pThis->sVal != NULL)  pThis->sVal[0] = '\0';
	if(pThis->sSrc != NULL)  pThis->sSrc[0] = '\0';

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
static void _OobComment_chardata(void *data, const XML_Char *s, int len){
	parse_stream_comment_t* pStack = (parse_stream_comment_t*)data;
	size_t uRoom = sizeof(pStack->sTxtBuf) - 1 - pStack->uTxtLen;
	size_t uCopy = (size_t)len < uRoom ? (size_t)len : uRoom;
	if(uCopy > 0){
		memcpy(pStack->sTxtBuf + pStack->uTxtLen, s, uCopy);
		pStack->uTxtLen += uCopy;
		pStack->sTxtBuf[pStack->uTxtLen] = '\0';
	}
}

static void _OobComment_end(void *data, const char *el){
	parse_stream_comment_t* pStack = (parse_stream_comment_t*)data;
	DasComment* pThis = pStack->sc;

	/* das3.0: body text is the value; only use it when the das2.2
	   'value' attribute was not present (sVal still empty) */
	if(pStack->uTxtLen > 0 && pThis->sVal != NULL && pThis->sVal[0] == '\0')
		das_store_str(&(pThis->sVal), &(pThis->uValLen), pStack->sTxtBuf);
}

das_errcode OobComment_decode(DasComment* pSc, DasBuf* pBuf)
{
	parse_stream_comment_t stack = {0};
	stack.sc = pSc;
	stack.errorCode = 0;

	XML_Parser p = XML_ParserCreate("UTF-8");
	if(!p)
		return daserrf(20, "couldn't create xml parser\n");
	XML_SetUserData(p, &stack);
	XML_SetElementHandler(p, _OobComment_start, _OobComment_end);
	XML_SetCharacterDataHandler(p, _OobComment_chardata);

	size_t uXml = 0;
	const char* sXml = (const char*)DasBuf_data(pBuf, &uXml);
	int nParRet = XML_Parse(p, sXml, (int)uXml, 1);
	if(!nParRet){
		/* Report before the free, the offset and code live in the parser */
		das_errcode nRet = daserrf(20, "Parse error at offset %ld:\n%s\n",
			XML_GetCurrentByteIndex(p), XML_ErrorString(XML_GetErrorCode(p)));
		XML_ParserFree(p);
		return nRet;
	}
	XML_ParserFree(p);

	if(stack.errorCode != 0)
		return daserr(stack.errorCode, stack.errorMessage);

	return DAS_OKAY;
}

das_errcode OobComment_encode(const DasComment* pSc, DasBuf* pBuf)
{
	char sTypeStk[256]; char* pTypeAlloc;
	char sSrcStk[256];  char* pSrcAlloc = NULL;
	char sValStk[256];  char* pValAlloc = NULL;
	int nRet = 0;

	const char* sType = _stomp_xml_attr(DasComment_get(pSc, "type"), sTypeStk, &pTypeAlloc);
	const char* sSrc  = _stomp_xml_attr(DasComment_get(pSc, "source"), sSrcStk, &pSrcAlloc);
	if((sType == NULL)||(sSrc == NULL)){
		nRet = daserrf(DASERR_OOB, "Couldn't allocate comment attribute space");
		goto cleanup;
	}

	const char* pVal = _xml_escape_val(DasComment_get(pSc, "value"), sValStk, &pValAlloc);
	size_t uValLen = strlen(pVal);

	/* Do this all on one line if the comment value is less than 40 chars */
	nRet = DasBuf_printf(pBuf, "<comment type=\"%s\" source=\"%s\"", sType, sSrc);
	if(nRet != 0) goto cleanup;

	if(uValLen > 40){
		nRet = DasBuf_printf(pBuf, "\n  ");
		if(nRet != 0) goto cleanup;
	}

	nRet = DasBuf_printf(pBuf, " value=\"%s\"", pVal);
	if(nRet != 0) goto cleanup;

	nRet = DasBuf_printf(pBuf, uValLen > 40 ? "\n/>\n" : " />\n");

cleanup:
	if(pTypeAlloc) free(pTypeAlloc);
	if(pSrcAlloc) free(pSrcAlloc);
	if(pValAlloc) free(pValAlloc);
	return nRet;
}

das_errcode OobComment_encode3(const DasComment* pSc, DasBuf* pBuf)
{
	/* das3.0 format: type and source in attributes, value as element body text.
	   Omit the source attribute when the field is empty. */
	char sTypeStk[256]; char* pTypeAlloc;
	char sSrcStk[256];  char* pSrcAlloc = NULL;
	char sValStk[256];  char* pValAlloc = NULL;
	das_errcode nRet;

	const char* sType = _stomp_xml_attr(DasComment_get(pSc, "type"), sTypeStk, &pTypeAlloc);
	const char* sSrc  = _stomp_xml_attr(DasComment_get(pSc, "source"), sSrcStk, &pSrcAlloc);
	if((sType == NULL)||(sSrc == NULL)){
		nRet = daserrf(DASERR_OOB, "Couldn't allocate comment attribute space");
		goto cleanup;
	}

	const char* pVal = _xml_escape_val(DasComment_get(pSc, "value"), sValStk, &pValAlloc);

	if(sSrc[0] != '\0')
		nRet = DasBuf_printf(pBuf,
			"<comment type=\"%s\" source=\"%s\">%s</comment>\n", sType, sSrc, pVal);
	else
		nRet = DasBuf_printf(pBuf, "<comment type=\"%s\">%s</comment>\n", sType, pVal);

cleanup:
	if(pTypeAlloc) free(pTypeAlloc);
	if(pSrcAlloc) free(pSrcAlloc);
	if(pValAlloc) free(pValAlloc);
	return nRet;
}

/* ************************************************************************* */
/* Generic XX packet parser */
das_errcode OutOfBand_decode(DasBuf* pBuf, OutOfBand** ppObjs, int* which)
{
	char sName[DASXML_NAME_LEN] = {'\0'};

	/* Eat the whitespace on either end */
	DasBuf_strip(pBuf);

	if(DasBuf_unread(pBuf) == 0)
		return daserrf(19, "Empty out-of-Band packet in Stream");

	size_t uPos = DasBuf_readOffset(pBuf);
	char b;
	if(DasBuf_read(pBuf, &b, 1) < 1)
		return daserrf(19, "%s: Error reading out-of-band packet", __func__);

	if(b != '<')
		return daserrf(19, "found \"%c\", expected \"<\"", b);

	int i = 0;
	while(DasBuf_read(pBuf, &b, 1) == 1     &&
	      i < (DASXML_NAME_LEN - 1) &&
	      !isspace((unsigned char)b) && b != '\0' && b != '>' && b != '/'){
		sName[i] = b;
		i++;
	}

	DasBuf_setReadOffset(pBuf, uPos);

	if(strcmp(sName, "comment") == 0){
		for(i = 0; ppObjs[i] != NULL; i++){
			if(ppObjs[i]->pkttype == OOB_COMMENT){
				*which = i;
				return OobComment_decode((DasComment*)ppObjs[i], pBuf);
			}
		}
	}
	else{
		if(strcmp(sName, "exception") == 0){
			for(i = 0; ppObjs[i] != NULL; i++){
				if(ppObjs[i]->pkttype == OOB_EXCEPT){
					*which = i;
					return OobExcept_decode((DasExcept*)ppObjs[i], pBuf);
				}
			}
		}
	}

	*which = -1;
	return DAS_OKAY;
}
