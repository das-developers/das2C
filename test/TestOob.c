/** @file TestOob.c Exception type names and the DasIO out-of-band writers */

/* Author: Chris Piker <chris-piker@uiowa.edu>
 *
 * This file is intended to demonstrate an interface.  This is free
 * and unencumbered software released into the public domain
 *
 * Anyone is free to copy, modify, publish, use, compile, sell, or
 * distribute this file, either in source code form or as a compiled
 * binary, for any purpose, commercial or non-commercial, and by any
 * means.
 *
 * In jurisdictions that recognize copyright laws, the author or authors
 * of this file dedicate any and all copyright interest in this file to
 * the public domain. We make this dedication for the benefit of the
 * public at large and to the detriment of our heirs and successors. We
 * intend this dedication to be an overt act of relinquishment in
 * perpetuity of all present and future rights to this file under
 * copyright law.
 *
 * THIS FILE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.
 *
 * For more information, please refer to <http://unlicense.org/>
 */

/* Checks das_except_toStr() / _fromStr() against the table,
 * das_xml_escape() truncation, and a DasIO round trip of DasIO_comment_v() /
 * DasIO_exception_v().  The legacy send.h writers are checked by
 * legacy/TestSend.c.
 */

#define _POSIX_C_SOURCE 200112L

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <das3/core.h>

static int g_fails = 0;
#define FAIL(...) do{ printf("FAIL (line %d): ", __LINE__); printf(__VA_ARGS__); \
                      printf("\n"); ++g_fails; }while(0)

/* Markup in a message must be escaped by the writer and restored by the
   reader.  Each message differs, so a reader that keeps the previous packet's
   text shows up as a mismatch. */
#define MSG_INFO   "info a<b & c>d"
#define MSG_QUERY  "query a<b & c>d"
#define MSG_SERVER "server a<b & c>d"
#define MSG_NODATA "nodata a<b & c>d"

/* ************************************************************************* */

static void test_lookup(void)
{
	struct {das_except_e nType; const char* s2; const char* s3;} aRow[] = {
		{DAS_EX_UNKNOWN,    "Unknown",          "Unknown"},
		{DAS_EX_NO_DATA,    "NoDataInInterval", "NoMatchingData"},
		{DAS_EX_SERVER_ERR, "ServerError",      "ServerError"},
		{DAS_EX_QUERY_ERR,  "IllegalArgument",  "QueryError"}
	};
	for(size_t u = 0; u < sizeof(aRow)/sizeof(aRow[0]); ++u){
		const char* s2 = das_except_toStr(aRow[u].nType, 2);
		const char* s3 = das_except_toStr(aRow[u].nType, 3);
		if((s2 == NULL)||(strcmp(s2, aRow[u].s2) != 0)) FAIL("das2 name for %d", aRow[u].nType);
		if((s3 == NULL)||(strcmp(s3, aRow[u].s3) != 0)) FAIL("das3 name for %d", aRow[u].nType);

		/* either spelling parses back, no version needed */
		if(das_except_fromStr(aRow[u].s2) != aRow[u].nType) FAIL("fromStr %s", aRow[u].s2);
		if(das_except_fromStr(aRow[u].s3) != aRow[u].nType) FAIL("fromStr %s", aRow[u].s3);
	}

	if(das_except_toStr(DAS_EX_SERVER_ERR, 1) != NULL) FAIL("das1 has no exceptions");
	if(das_except_toStr(DAS_EX_SERVER_ERR, 4) != NULL) FAIL("no das4 yet");
	if(das_except_toStr((das_except_e)99, 3) != NULL)  FAIL("type out of range");

	/* streams may carry their own types, those are not errors */
	if(das_except_fromStr("SomethingCustom") != DAS_EX_UNKNOWN) FAIL("custom type");
	if(das_except_fromStr(NULL) != DAS_EX_UNKNOWN) FAIL("NULL type");
}

/* ************************************************************************* */
/* The writers escape into fixed buffers, so what happens at the end matters */

static void test_escape(void)
{
	char sBuf[16];

	/* each '&' grows to five bytes, three fit exactly with the NUL */
	das_xml_escape(sBuf, "&&&&&&&&a", sizeof(sBuf));
	if(strcmp(sBuf, "&amp;&amp;&amp;") != 0) FAIL("escape fill, got '%s'", sBuf);

	/* an entity that won't fit whole is left out, not cut */
	das_xml_escape(sBuf, "ab&", 5);
	if(strcmp(sBuf, "ab") != 0) FAIL("escape cut entity, got '%s'", sBuf);

	/* nor is a two byte character, e-acute here */
	das_xml_escape(sBuf, "ab\xc3\xa9", 4);
	if(strcmp(sBuf, "ab") != 0) FAIL("escape cut utf-8, got '%s'", sBuf);

	das_xml_escape(sBuf, "a\"b'c<d>e", sizeof(sBuf));
	if(strcmp(sBuf, "a&quot;b&apos;c") != 0) FAIL("escape set, got '%s'", sBuf);
}

/* ************************************************************************* */
/* Reader side */

#define MAX_SEEN 16

typedef struct seen {
	int nClose;
	bool bHadSd;          /* the close handler got a stream */
	int nExcept;
	das_except_e aExcept[MAX_SEEN];
	char aExceptMsg[MAX_SEEN][64];
	int nComment;
	bool bCmtSd;          /* every comment came with a stream */
	char aCommentType[MAX_SEEN][32];
	char aCommentVal[MAX_SEEN][64];
} seen_t;

/* The only place a reader sees an exception, and the last call it gets */
static DasErrCode onClose(DasStream* pSd, const DasExcept* pEx, void* pUser)
{
	seen_t* pSeen = (seen_t*)pUser;
	++(pSeen->nClose);
	pSeen->bHadSd = (pSd != NULL);
	if(pEx != NULL){
		if(pSeen->nExcept < MAX_SEEN){
			pSeen->aExcept[pSeen->nExcept] = DasExcept_type(pEx);
			snprintf(pSeen->aExceptMsg[pSeen->nExcept], 63, "%s", DasExcept_msg(pEx));
		}
		++(pSeen->nExcept);
	}
	return DAS_OKAY;   /* the default would fail the read on a server error */
}

static DasErrCode onComment(DasStream* pSd, const DasComment* pCmt, void* pUser)
{
	seen_t* pSeen = (seen_t*)pUser;
	pSeen->bCmtSd = (pSeen->nComment == 0 || pSeen->bCmtSd) && (pSd != NULL);
	if(pSeen->nComment < MAX_SEEN){
		snprintf(pSeen->aCommentType[pSeen->nComment], 31, "%s", DasComment_get(pCmt, "type"));
		snprintf(pSeen->aCommentVal[pSeen->nComment], 63, "%s", DasComment_get(pCmt, "value"));
	}
	++(pSeen->nComment);
	return DAS_OKAY;
}

/* Read with a chosen processor set: 'n' none, 'm' a comment-only monitor,
   'h' the full handler, "hm" both.  Only the outcome is wanted. */
static DasErrCode _readWith(const char* sFile, const char* sProcs, seen_t* pSeen)
{
	memset(pSeen, 0, sizeof(seen_t));
	DasIO* pIn = new_DasIO("TestOob", new_DasXport_file(sFile, "r"), NULL);
	if(pIn == NULL){ FAIL("can't open %s", sFile); return 15; }
	DasIO_setModel(pIn, STREAM_MODEL_MIXED);

	das_stream_proc handler, monitor;
	das_stream_proc_init(&handler, pSeen);
	handler.onClose = onClose;
	handler.onComment = onComment;
	das_stream_proc_init(&monitor, pSeen);
	monitor.onComment = onComment;
	if(strchr(sProcs, 'h')) DasIO_addProcessor(pIn, &handler);
	if(strchr(sProcs, 'm')) DasIO_addProcessor(pIn, &monitor);

	DasErrCode nRet = DasIO_readAll(pIn);
	del_DasIO(pIn);
	return nRet;
}

static DasErrCode _read(const char* sFile, seen_t* pSeen)
{
	memset(pSeen, 0, sizeof(seen_t));

	DasIO* pIn = new_DasIO("TestOob", new_DasXport_file(sFile, "r"), NULL);
	if(pIn == NULL){ FAIL("can't open %s", sFile); return 15; }
	DasIO_setModel(pIn, STREAM_MODEL_MIXED);  /* das3 headers carry <p> */

	das_stream_proc handler;
	das_stream_proc_init(&handler, pSeen);
	handler.onClose = onClose;
	handler.onComment = onComment;
	DasIO_addProcessor(pIn, &handler);

	DasErrCode nRet = DasIO_readAll(pIn);
	del_DasIO(pIn);
	return nRet;
}

/* Some writers wrap the body text in newlines, compare without them */
static bool _sameText(const char* sGot, const char* sWant)
{
	while((*sGot == '\n')||(*sGot == ' ')) ++sGot;
	size_t uLen = strlen(sGot);
	while((uLen > 0)&&((sGot[uLen-1] == '\n')||(sGot[uLen-1] == ' '))) --uLen;
	return (uLen == strlen(sWant))&&(strncmp(sGot, sWant, uLen) == 0);
}

/* ************************************************************************* */
/* The DasIO writers, through a file and back */

static void test_dasio(int nVer)
{
	char sFile[64];
	snprintf(sFile, sizeof(sFile), "TestOob_tmp.d%ds", nVer);
	const char* sMode = (nVer == 2) ? "2" : "3";   /* the file decides direction */
	seen_t seen;

	/* An exception before any header sends a stub header, and ends the
	   stream: nothing may be written after it */
	DasIO* pOut = new_DasIO("TestOob", new_DasXport_file(sFile, "w"), sMode);
	if(pOut == NULL){ FAIL("can't write %s", sFile); return; }
	if(DasIO_exception_v(pOut, DAS_EX_SERVER_ERR, "%s %d", MSG_SERVER, 42) != DAS_OKAY)
		FAIL("das%d: DasIO_exception_v", nVer);
	if(DasIO_comment_v(pOut, "log:info", "too late") == DAS_OKAY)
		FAIL("das%d: a comment after the exception was accepted", nVer);
	del_DasIO(pOut);

	if(_read(sFile, &seen) != 0) FAIL("das%d: reading back the stub stream", nVer);
	if((seen.nClose != 1)||(!seen.bHadSd))
		FAIL("das%d: stub stream closed %d times, stream %s", nVer, seen.nClose,
			seen.bHadSd ? "present" : "missing");
	if((seen.nExcept != 1)||(seen.aExcept[0] != DAS_EX_SERVER_ERR)||
		(!_sameText(seen.aExceptMsg[0], MSG_SERVER " 42")))
		FAIL("das%d: stub stream exception '%s'", nVer, seen.aExceptMsg[0]);
	if(seen.nComment != 0) FAIL("das%d: stub stream has %d comments", nVer, seen.nComment);

	/* The library default for onClose runs only when no processor has one.
	   A server error then fails the read, with or without processors, but a
	   passive sibling must not fail a read another processor handled. */
	if(_readWith(sFile, "n", &seen) == DAS_OKAY)
		FAIL("das%d: unhandled exception passed with no processors", nVer);
	if(_readWith(sFile, "m", &seen) == DAS_OKAY)
		FAIL("das%d: unhandled exception passed with only a monitor", nVer);
	if(_readWith(sFile, "hm", &seen) != DAS_OKAY)
		FAIL("das%d: a monitor failed a read whose exception was handled", nVer);

	/* A comment, then "no data", then a second close that must be harmless */
	pOut = new_DasIO("TestOob", new_DasXport_file(sFile, "w"), sMode);
	if(pOut == NULL){ FAIL("can't write %s", sFile); return; }
	DasStream* pSd = new_DasStream();
	DasIO_writeStreamDesc(pOut, pSd);
	del_DasDesc((DasDesc*)pSd);
	if(DasIO_comment_v(pOut, "log:info", "%s", MSG_INFO) != DAS_OKAY)
		FAIL("das%d: DasIO_comment_v", nVer);
	if(DasIO_exception_v(pOut, DAS_EX_NO_DATA, "%s", MSG_NODATA) != DAS_OKAY)
		FAIL("das%d: DasIO_exception_v no data", nVer);
	DasIO_close(pOut);
	del_DasIO(pOut);

	if(_read(sFile, &seen) != 0) FAIL("das%d: reading back the no data stream", nVer);
	if((seen.nComment != 1)||(strcmp(seen.aCommentType[0], "log:info") != 0)||
		(!_sameText(seen.aCommentVal[0], MSG_INFO)))
		FAIL("das%d: comment '%s'", nVer, seen.aCommentVal[0]);
	if(!seen.bCmtSd) FAIL("das%d: comment after the header arrived without a stream", nVer);
	if((seen.nExcept != 1)||(seen.aExcept[0] != DAS_EX_NO_DATA)||
		(!_sameText(seen.aExceptMsg[0], MSG_NODATA)))
		FAIL("das%d: no data exception '%s'", nVer, seen.aExceptMsg[0]);

	remove(sFile);
}

/* ************************************************************************* */

int main(int argc, char** argv)
{
	das_init(argv[0], DASERR_DIS_RET, 256, DASLOG_INFO, NULL);

	test_lookup();
	test_escape();

	/* A processor that never went through das_stream_proc_init() is refused,
	   uVersion zero would otherwise be read as some layout */
	das_stream_proc zero;
	memset(&zero, 0, sizeof(zero));
	DasIO* pIo = new_DasIO_feed("TestOob", "r");
	if(DasIO_addProcessor(pIo, &zero) >= 0)
		FAIL("a zeroed das_stream_proc was accepted");
	del_DasIO(pIo);

	printf("INFO: ======== ERROR lines below are intentional, provoked by negative checks ========\n");
	fflush(stdout);
	test_dasio(2);
	test_dasio(3);
	printf("INFO: ======== end of intentional errors, the verdict follows ========\n");
	if(g_fails > 0){
		printf("ERROR: TestOob had %d failure(s)\n", g_fails);
		return 15;
	}
	printf("INFO: All exception name lookups, escapes and DasIO round trips passed\n");
	return 0;
}
