/** @file TestSend.c Read back what the legacy send.h writers produce */

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


/* Two modes:
 *
 *    TestSend send 2|3      write a stub stream holding every send.h message
 *    TestSend read FILE     read it back with the library's own parser
 *
 * make test_legacy pipes send into a file and reads it back for both
 * versions.  The send.h writers build their packets by hand, so the only
 * proof they are right is that DasIO accepts them, with the right type names,
 * and that the markup characters in the messages survive the trip.  send
 * writes all three exceptions in a row; an exception ends a stream, so the
 * reader must stop at the first and never see the other two.
 */

#define _POSIX_C_SOURCE 200112L

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <das3/core.h>
#include "send.h"

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
/* Writer side, stdout is the stream */

static int write_stream(int nDasVer)
{
	das_send_stub(nDasVer);
	das_send_msg(nDasVer, "TestSend", "%s", MSG_INFO);
	das_send_progbeg(nDasVer, "TestSend", 0.0, 10.0);
	das_send_progup(nDasVer, "TestSend", 5.0);
	das_send_queryerr(nDasVer, "%s", MSG_QUERY);
	das_send_srverr(nDasVer, "%s", MSG_SERVER);
	das_send_nodata(nDasVer, "%s", MSG_NODATA);
	return 0;
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
static das_errcode onClose(DasStream* pSd, const DasExcept* pEx, void* pUser)
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

static das_errcode onComment(DasStream* pSd, const DasComment* pCmt, void* pUser)
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
static das_errcode _read(const char* sFile, seen_t* pSeen)
{
	memset(pSeen, 0, sizeof(seen_t));

	DasIO* pIn = new_DasIO("TestSend", new_DasXport_file(sFile, "r"), NULL);
	if(pIn == NULL){ FAIL("can't open %s", sFile); return 15; }
	DasIO_setModel(pIn, STREAM_MODEL_MIXED);  /* das3 headers carry <p> */

	das_stream_proc handler;
	das_stream_proc_init(&handler, pSeen);
	handler.onClose = onClose;
	handler.onComment = onComment;
	DasIO_addProcessor(pIn, &handler);

	das_errcode nRet = DasIO_readAll(pIn);
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

static int read_stream(const char* sFile)
{
	seen_t seen;
	if(_read(sFile, &seen) != 0) FAIL("reading %s", sFile);

	if(seen.nClose != 1) FAIL("%s: close handler ran %d times", sFile, seen.nClose);
	if(seen.nExcept != 1) FAIL("%s: %d exceptions, expected the first only", sFile, seen.nExcept);
	if(seen.nExcept > 0){
		if(seen.aExcept[0] != DAS_EX_QUERY_ERR)
			FAIL("%s: exception is type %d, expected %d", sFile, seen.aExcept[0], DAS_EX_QUERY_ERR);
		if(!_sameText(seen.aExceptMsg[0], MSG_QUERY))
			FAIL("%s: exception message '%s'", sFile, seen.aExceptMsg[0]);
	}

	const char* aWantCmt[3] = {"log:info", "taskSize", "taskProgress"};
	const char* aWantVal[3] = {MSG_INFO, "100", "50"};
	if(seen.nComment != 3) FAIL("%s: %d comments, expected 3", sFile, seen.nComment);
	for(int i = 0; (i < 3)&&(i < seen.nComment); ++i){
		if(strcmp(seen.aCommentType[i], aWantCmt[i]) != 0)
			FAIL("%s: comment %d is '%s', expected '%s'", sFile, i, seen.aCommentType[i], aWantCmt[i]);
		if(!_sameText(seen.aCommentVal[i], aWantVal[i]))
			FAIL("%s: comment %d value '%s'", sFile, i, seen.aCommentVal[i]);
	}

	if(g_fails > 0){
		printf("ERROR: TestSend read of %s had %d failure(s)\n", sFile, g_fails);
		return 15;
	}
	printf("INFO: send.h output in %s reads back correctly\n", sFile);
	return 0;
}

/* The refusals tested here are deliberate, so their das_error() messages
   are dropped; everything else prints as usual.  The return codes are what
   the checks look at. */
static void _dropErrors(const das_logmsg* pMsg, void* pUser)
{
	(void)pUser;
	if(pMsg->nErr == DAS_OKAY) daslog_stderr(pMsg, NULL);
}

/* ************************************************************************* */

int main(int argc, char** argv)
{
	das_loghook hook = {_dropErrors, NULL};
	das_init(argv[0], DASERR_DIS_RET, DASLOG_INFO, &hook);

	if((argc == 3)&&(strcmp(argv[1], "send") == 0))
		return write_stream(atoi(argv[2]));

	if((argc == 3)&&(strcmp(argv[1], "read") == 0))
		return read_stream(argv[2]);

	fprintf(stderr, "Usage: TestSend send 2|3  or  TestSend read FILE\n");
	return 15;
}
