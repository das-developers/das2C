/** @file TestXport.c The private transport layer under DasIO and the HTTP client */

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

/* Usage: TestXport SCRATCH_DIR
 *
 * Covers the base (short reads, readFull, pushback, failure latching, close)
 * through the memory kind, then each kind that can run without a server: a
 * C file both owned and borrowed, a sub-command, a socket pair, and the zlib
 * filters, including the hand back of bytes that follow a zlib stream.  The
 * TLS kind needs a live server and is covered by the HTTP tests.
 *
 * Some checks provoke errors on purpose; the library logs those to stderr. */

#define _POSIX_C_SOURCE 200112L

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifndef _WIN32
#include <sys/socket.h>
#include <unistd.h>
#endif

#include <das3/core.h>
#include <das3/xport_p.h>   /* tests link the static archive, so they may */

static int g_fails = 0;
#define FAIL(...) do{ printf("FAIL (line %d): ", __LINE__); printf(__VA_ARGS__); \
                      printf("\n"); ++g_fails; }while(0)
#define CHECK(C, ...) do{ if(!(C)) FAIL(__VA_ARGS__); }while(0)

static const char g_sText[] = "0123456789abcdefghij";  /* 20 bytes */

/* ************************************************************************* */
static void testBase(void)
{
	char aBuf[64] = {'\0'};
	DasXport* pX = new_DasXport_mem((void*)g_sText, 20, "r");
	CHECK(pX != NULL, "mem reader made");
	if(pX == NULL) return;

	CHECK(DasXport_read(pX, aBuf, 4) == 4 && memcmp(aBuf, "0123", 4) == 0, "short read");

	/* Pushback comes out first, newest in front, then the stream resumes */
	CHECK(_DasXport_unread(pX, "23", 2) == DAS_OKAY, "unread");
	CHECK(_DasXport_unread(pX, "XY", 2) == DAS_OKAY, "second unread");
	CHECK(_DasXport_readFull(pX, aBuf, 8) == 8 && memcmp(aBuf, "XY234567", 8) == 0,
	      "pushback first, then the stream: got %.8s", aBuf);
	CHECK(pX->nMoved == 8, "nMoved counts stream bytes once, got %lld",
	      (long long)pX->nMoved);

	CHECK(_DasXport_readFull(pX, aBuf, 64) == 12, "readFull short only at the end");
	CHECK(DasXport_read(pX, aBuf, 8) == 0, "end of input is 0");
	CHECK(DasXport_error(pX) == NULL, "no error at a clean end");

	CHECK(DasXport_write(pX, "a", 1) == -1 && DasXport_error(pX) != NULL,
	      "write to a reader fails with a message");

	/* Pushback survives a close, the stream does not */
	_DasXport_unread(pX, "Q", 1);
	CHECK(DasXport_close(pX) == DAS_OKAY, "close");
	CHECK(DasXport_close(pX) == DAS_OKAY, "second close is harmless");
	CHECK(DasXport_read(pX, aBuf, 8) == 1 && aBuf[0] == 'Q', "pushback after close");
	CHECK(DasXport_read(pX, aBuf, 8) == -1, "stream read after close fails");
	del_DasXport(pX);
	del_DasXport(NULL);

	/* A writer that runs out of room fails and stays failed */
	char aOut[8] = {'\0'};
	pX = new_DasXport_mem(aOut, sizeof(aOut), "w");
	CHECK(DasXport_write(pX, "abcde", 5) == 5, "write fits");
	CHECK(DasXport_write(pX, "fghij", 5) == -1, "write past the end fails");
	const char* sErr = DasXport_error(pX);
	CHECK(sErr != NULL && strstr(sErr, "full") != NULL, "first error is kept: %s",
	      sErr ? sErr : "(null)");
	CHECK(DasXport_write(pX, "f", 1) == -1, "failure latches");
	CHECK(memcmp(aOut, "abcde", 5) == 0 && aOut[5] == '\0', "nothing partial written");
	del_DasXport(pX);

	CHECK(new_DasXport_mem(aOut, 8, "rw") == NULL, "rw refused for memory");
}

/* ************************************************************************* */
static das_errcode _stopCopy(int64_t nBytes, void* pUser)
{
	(void)nBytes; (void)pUser;
	return 77;
}

static void testBuf(void)
{
	DasBuf* pBuf = new_DasBuf(16);
	DasXport* pIn = new_DasXport_mem((void*)g_sText, 20, "r");

	CHECK(_DasXport_fill(pIn, pBuf, 10) == 10 && DasBuf_written(pBuf) == 10, "fill exact");
	CHECK(_DasXport_fill(pIn, pBuf, 10) == -1, "fill past the buffer refused");
	CHECK(DasBuf_written(pBuf) == 10, "refused fill commits nothing");

	char aOut[32] = {'\0'};
	DasXport* pOut = new_DasXport_mem(aOut, sizeof(aOut), "w");
	CHECK(_DasXport_drain(pOut, pBuf) == 10, "drain");
	CHECK(DasBuf_unread(pBuf) == 0 && memcmp(aOut, "0123456789", 10) == 0,
	      "drain consumes and writes in order");
	CHECK(_DasXport_drain(pOut, pBuf) == 0, "drain of an empty buffer");

	del_DasXport(pIn);
	del_DasXport(pOut);
	del_DasBuf(pBuf);

	/* Copying, and stopping a copy part way */
	char aCopy[32] = {'\0'};
	int64_t nCopied = -1;
	pIn = new_DasXport_mem((void*)g_sText, 20, "r");
	pOut = new_DasXport_mem(aCopy, sizeof(aCopy), "w");
	CHECK(DasXport_copyTo(pIn, pOut, NULL, NULL, &nCopied) == DAS_OKAY, "copyTo");
	CHECK(nCopied == 20 && memcmp(aCopy, g_sText, 20) == 0, "copyTo moved every byte");
	del_DasXport(pIn);
	del_DasXport(pOut);

	char aSmall[8];
	pIn = new_DasXport_mem((void*)g_sText, 20, "r");
	pOut = new_DasXport_mem(aSmall, sizeof(aSmall), "w");
	CHECK(DasXport_copyTo(pIn, pOut, NULL, NULL, NULL) == DASERR_IO, "copyTo into too little room");
	CHECK(DasXport_error(pOut) != NULL && DasXport_error(pIn) == NULL,
	      "the failed end is the one with the message");
	del_DasXport(pIn);
	del_DasXport(pOut);

	pIn = new_DasXport_mem((void*)g_sText, 20, "r");
	pOut = new_DasXport_mem(aCopy, sizeof(aCopy), "w");
	CHECK(DasXport_copyTo(pIn, pOut, _stopCopy, NULL, &nCopied) == 77 && nCopied == 20,
	      "progress callback stops the copy with its own code");
	del_DasXport(pIn);
	del_DasXport(pOut);
}

/* ************************************************************************* */
/* Elastic buffers grow on write up to a limit; DasIO's packet buffer is one */
/* Counts DasBuf errors, to tell a reported refusal from a quiet one */
static void _countBufErr(const DasLogMsg* pMsg, void* pUser)
{
	if(pMsg->errCode == DASERR_BUF) ++(*(int*)pUser);
}

static void testElastic(void)
{
	CHECK(new_DasBuf_elastic(0, 10) == NULL, "elastic needs a starting size");
	CHECK(new_DasBuf_elastic(20, 10) == NULL, "elastic limit below its start refused");

	DasBuf* pFix = new_DasBuf(16);
	char aMem[8];
	DasBuf* pWrap = new_DasBuf_wrap(aMem, sizeof(aMem), "w");
	DasBuf* pBuf = new_DasBuf_elastic(16, 64);
	CHECK(DasBuf_isPinned(pFix) && DasBuf_isPinned(pWrap) && !DasBuf_isPinned(pBuf),
	      "only elastic buffers are unpinned");
	del_DasBuf(pWrap);

	/* Growth keeps what was written and where reading had got to */
	CHECK(DasBuf_write(pBuf, "abc", 3) == DAS_OKAY, "small write");
	char aOne[2] = {'\0'};
	CHECK(DasBuf_read(pBuf, aOne, 1) == 1 && aOne[0] == 'a', "read one byte");
	CHECK(DasBuf_write(pBuf, g_sText, 20) == DAS_OKAY &&
	      DasBuf_write(pBuf, g_sText, 17) == DAS_OKAY, "writes past the start size");
	CHECK(DasBuf_capacity(pBuf) == 64, "grew by doubling to %zu", DasBuf_capacity(pBuf));
	size_t uLen = 0;
	const uint8_t* pData = DasBuf_data(pBuf, &uLen);
	CHECK(uLen == 39 && memcmp(pData, "bc", 2) == 0 && memcmp(pData + 2, g_sText, 20) == 0,
	      "contents and read point survive growth");
	CHECK(pData[uLen] == 0, "space past the data is zeroed after growth");

	/* The limit holds and a refused write changes nothing */
	char aBig[25];
	memset(aBig, 'z', sizeof(aBig));
	CHECK(DasBuf_write(pBuf, aBig, 25) != DAS_OKAY, "write past the limit refused");
	CHECK(DasBuf_written(pBuf) == 40 && DasBuf_capacity(pBuf) == 64,
	      "refused write leaves the buffer alone");
	del_DasBuf(pBuf);

	/* printf measures, grows, formats again */
	pBuf = new_DasBuf_elastic(8, 128);
	CHECK(DasBuf_printf(pBuf, "%s%s%s", g_sText, g_sText, g_sText) == DAS_OKAY,
	      "printf longer than the start size");
	pData = DasBuf_data(pBuf, &uLen);
	CHECK(uLen == 60 && memcmp(pData + 40, g_sText, 20) == 0, "printf output intact");
	CHECK(DasBuf_printf(pBuf, "%s%s%s%s", g_sText, g_sText, g_sText, g_sText) != DAS_OKAY,
	      "printf past the limit refused");
	CHECK(DasBuf_written(pBuf) == 60, "refused printf commits nothing");
	del_DasBuf(pBuf);

	/* prepare asks for room up front, and commit can't move anything */
	size_t uSpace = 0;
	pBuf = new_DasBuf_elastic(16, 128);
	uint8_t* pDest = DasBuf_prepare(pBuf, 100, &uSpace);
	CHECK(pDest != NULL && uSpace >= 100 && DasBuf_capacity(pBuf) == 128,
	      "prepare grows to fit, %zu bytes of space", uSpace);
	memset(pDest, 'x', 100);
	CHECK(DasBuf_commit(pBuf, 100) == DAS_OKAY && DasBuf_written(pBuf) == 100, "commit");
	CHECK(DasBuf_prepare(pBuf, 29, &uSpace) == NULL && uSpace == 0,
	      "prepare past the limit fails quietly");
	CHECK(DasBuf_prepare(pBuf, 0, &uSpace) != NULL && uSpace == 28,
	      "prepare of 0 takes what is free");
	del_DasBuf(pBuf);

	/* Read-only is a caller bug and is reported once, "no room" is quiet */
	int nErrs = 0;
	DasLogHook count = {_countBufErr, &nErrs};
	DasLogHook prev = daslog_setHook(&count);

	DasBuf* pRo = new_DasBuf_wrap((void*)g_sText, 20, "r");
	CHECK(DasBuf_isReadOnly(pRo) && !DasBuf_isReadOnly(pFix), "read-only flag");
	CHECK(DasBuf_prepare(pRo, 0, &uSpace) == NULL && uSpace == 0,
	      "read-only buffer refuses prepare");
	CHECK(nErrs == 1, "read-only prepare reports it, %d errors", nErrs);
	DasXport* pRoIn = new_DasXport_mem((void*)g_sText, 20, "r");
	CHECK(_DasXport_fill(pRoIn, pRo, 20) == -1, "fill into a read-only buffer refused");
	CHECK(nErrs == 2, "and reports it once, %d errors", nErrs - 1);
	del_DasXport(pRoIn);
	del_DasBuf(pRo);

	nErrs = 0;
	CHECK(DasBuf_prepare(pFix, 17, &uSpace) == NULL && nErrs == 0,
	      "no room is quiet, %d errors", nErrs);
	daslog_setHook(&prev);

	CHECK(DasBuf_prepare(pFix, 17, &uSpace) == NULL && uSpace == 0,
	      "pinned buffer can't prepare more than it has");
	CHECK(DasBuf_prepare(pFix, 16, &uSpace) != NULL && uSpace == 16,
	      "pinned buffer prepares what it has");
	del_DasBuf(pFix);

	/* The transport fill path grows an elastic buffer to the read size */
	pBuf = new_DasBuf_elastic(4, 64);
	DasXport* pIn = new_DasXport_mem((void*)g_sText, 20, "r");
	CHECK(_DasXport_fill(pIn, pBuf, 20) == 20 && DasBuf_written(pBuf) == 20,
	      "fill grows the buffer");
	del_DasXport(pIn);
	del_DasBuf(pBuf);
}

/* ************************************************************************* */
static void testFile(const char* sDir)
{
	char sPath[512];
	snprintf(sPath, sizeof(sPath), "%s/TestXport.tmp", sDir);

	FILE* pFile = fopen(sPath, "wb");
	if(pFile == NULL){ FAIL("can't create %s", sPath); return; }
	DasXport* pX = _new_DasXport_cfile(pFile, "w", true);
	CHECK(DasXport_write(pX, g_sText, 20) == 20, "file write");
	CHECK(DasXport_close(pX) == DAS_OKAY, "owned file closes clean");
	del_DasXport(pX);

	/* Borrowed: close flushes but the FILE stays open for its owner */
	pFile = fopen(sPath, "rb");
	pX = _new_DasXport_cfile(pFile, "r", false);
	char aBuf[32] = {'\0'};
	CHECK(_DasXport_readFull(pX, aBuf, 32) == 20 && memcmp(aBuf, g_sText, 20) == 0,
	      "file read back");
	del_DasXport(pX);
	CHECK(fseek(pFile, 0, SEEK_SET) == 0 && fgetc(pFile) == '0',
	      "borrowed FILE still usable after delete");
	fclose(pFile);
	remove(sPath);

	CHECK(_new_DasXport_cfile(stdin, "rw", false) == NULL, "rw refused for files");
}

/* ************************************************************************* */
#ifndef _WIN32
/* Catches the sub-command exit warning, the one transport message above
   debug level */
static char g_sLastWarn[256] = {'\0'};
static void _catchLog(const DasLogMsg* pMsg, void* pUser)
{
	(void)pUser;
	if(pMsg->level == DASLOG_WARN)
		snprintf(g_sLastWarn, sizeof(g_sLastWarn), "%s", pMsg->msg);
}

static void testCmd(void)
{
	DasXport* pX = new_DasXport_cmd("printf 'from a pipe'");
	char aBuf[32] = {'\0'};
	CHECK(_DasXport_readFull(pX, aBuf, 31) == 11 && strcmp(aBuf, "from a pipe") == 0,
	      "sub-command output: %s", aBuf);
	CHECK(DasXport_write(pX, "x", 1) == -1, "sub-command is read only");
	CHECK(DasXport_close(pX) == DAS_OKAY, "pclose");
	del_DasXport(pX);

	/* A failing command is not a transport failure, but it is logged */
	int nLevel = daslog_level();
	daslog_setLevel(DASLOG_WARN);
	DasLogHook hook = {_catchLog, NULL};
	DasLogHook prev = daslog_setHook(&hook);
	pX = new_DasXport_cmd("printf partial; exit 3");
	CHECK(_DasXport_readFull(pX, aBuf, 31) == 7, "output before the failure is read");
	CHECK(DasXport_close(pX) == DAS_OKAY, "non-zero exit still closes clean");
	CHECK(strstr(g_sLastWarn, "status 3") != NULL, "exit status logged: '%s'", g_sLastWarn);
	del_DasXport(pX);
	daslog_setHook(&prev);
	daslog_setLevel(nLevel);
}

static void testSock(void)
{
	int aFd[2];
	if(socketpair(AF_UNIX, SOCK_STREAM, 0, aFd) != 0){ FAIL("socketpair"); return; }

	DasXport* pA = _new_DasXport_sock(aFd[0], "rw", true);
	DasXport* pB = _new_DasXport_sock(aFd[1], "rw", false);
	char aBuf[32] = {'\0'};

	CHECK(DasXport_write(pA, "ping", 4) == 4, "socket write");
	CHECK(_DasXport_readFull(pB, aBuf, 4) == 4 && memcmp(aBuf, "ping", 4) == 0, "socket read");
	CHECK(DasXport_write(pB, "pong", 4) == 4, "socket write back, rw");
	CHECK(_DasXport_readFull(pA, aBuf, 4) == 4 && memcmp(aBuf, "pong", 4) == 0,
	      "socket read back, rw");

	del_DasXport(pA);   /* owned, shuts down and closes aFd[0] */
	CHECK(DasXport_read(pB, aBuf, 4) == 0, "peer close is end of input");
	del_DasXport(pB);   /* borrowed, aFd[1] stays open */
	CHECK(close(aFd[1]) == 0, "borrowed socket left open");
}
#endif

/* ************************************************************************* */
/* A DasIO takes its direction from the transport, the mode only has to say
   it for one that goes both ways */
static void testIoMode(void)
{
	DasIO* pIo = new_DasIO("TestXport", new_DasXport_mem((void*)g_sText, 20, "r"), NULL);
	CHECK(pIo != NULL, "reader inferred from a read transport");
	del_DasIO(pIo);

	char aOut[64];
	pIo = new_DasIO("TestXport", new_DasXport_mem(aOut, sizeof(aOut), "w"), "3");
	CHECK(pIo != NULL && DasIO_outVer(pIo) == DASSTREAMVER_3_0,
	      "writer inferred, version from the mode");
	del_DasIO(pIo);

	pIo = new_DasIO("TestXport", new_DasXport_mem(aOut, sizeof(aOut), "w"), NULL);
	CHECK(pIo != NULL && DasIO_outVer(pIo) == DASSTREAMVER_2_2, "writer default is das2.2");
	del_DasIO(pIo);

	CHECK(new_DasIO("TestXport", new_DasXport_mem((void*)g_sText, 20, "r"), "w") == NULL,
	      "mode disagreeing with the transport refused");
	CHECK(new_DasIO("TestXport", new_DasXport_mem((void*)g_sText, 20, "r"), "c") == NULL,
	      "compression refused for a reader");
	CHECK(new_DasIO("TestXport", new_DasXport_mem((void*)g_sText, 20, "r"), "rx") == NULL,
	      "unknown mode letter refused");
	pIo = new_DasIO("TestXport", new_DasXport_mem((void*)g_sText, 20, "r"), "rb");
	CHECK(pIo != NULL, "old fopen style 'b' accepted");
	del_DasIO(pIo);
	CHECK(new_DasIO("TestXport", NULL, NULL) == NULL, "no transport, no DasIO");

#ifndef _WIN32
	int aFd[2];
	if(socketpair(AF_UNIX, SOCK_STREAM, 0, aFd) != 0){ FAIL("socketpair"); return; }
	CHECK(new_DasIO("TestXport", new_DasXport_sock(aFd[0], "rw"), NULL) == NULL,
	      "two way transport needs r or w");
	pIo = new_DasIO("TestXport", new_DasXport_sock(aFd[0], "rw"), "r");
	CHECK(pIo != NULL, "two way transport with r");
	del_DasIO(pIo);
	CHECK(close(aFd[0]) == 0 && close(aFd[1]) == 0, "borrowed sockets left open");
#endif
}

/* ************************************************************************* */
static void testZlib(void)
{
	/* Compressible input bigger than one zlib chunk, written in odd pieces */
	size_t uIn = 100000;
	char* pIn = (char*)malloc(uIn);
	for(size_t u = 0; u < uIn; ++u) pIn[u] = g_sText[(u * 7) % 20];

	size_t uRoom = uIn + 64;
	uint8_t* pZip = (uint8_t*)calloc(uRoom, 1);
	DasXport* pMem = new_DasXport_mem(pZip, uRoom, "w");
	DasXport* pDef = _new_DasXport_deflate(pMem);
	for(size_t u = 0; u < uIn; u += 777){
		size_t uTake = (uIn - u < 777) ? uIn - u : 777;
		if(DasXport_write(pDef, pIn + u, uTake) != (ptrdiff_t)uTake){
			FAIL("deflate write at %zu", u); break;
		}
	}
	CHECK(DasXport_close(pDef) == DAS_OKAY, "deflate close finishes the stream");
	size_t uZip = (size_t)pMem->nMoved;
	CHECK(uZip > 0 && uZip < uIn / 4, "compressed %zu to %zu bytes", uIn, uZip);
	del_DasXport(pDef);  /* frees pMem too */

	/* Bytes after the zlib stream belong to whoever reads next */
	memcpy(pZip + uZip, "TAIL", 4);
	DasXport* pRaw = new_DasXport_mem(pZip, uZip + 4, "r");
	DasXport* pInf = _new_DasXport_inflate(pRaw);
	char* pOut = (char*)calloc(uIn + 1, 1);
	ptrdiff_t nOut = _DasXport_readFull(pInf, pOut, uIn + 1);
	CHECK(nOut == (ptrdiff_t)uIn && memcmp(pOut, pIn, uIn) == 0,
	      "inflate round trip, %td of %zu bytes", nOut, uIn);
	char aTail[8] = {'\0'};
	CHECK(DasXport_read(pRaw, aTail, 8) == 4 && memcmp(aTail, "TAIL", 4) == 0,
	      "trailing bytes handed back to the inner transport");
	del_DasXport(pInf);  /* frees pRaw too */

	/* A cut off stream is an error, not a quiet short read */
	pRaw = new_DasXport_mem(pZip, uZip / 2, "r");
	pInf = _new_DasXport_inflate(pRaw);
	nOut = _DasXport_readFull(pInf, pOut, uIn);
	CHECK(nOut == -1 && DasXport_error(pInf) != NULL, "truncated stream fails");
	del_DasXport(pInf);

	CHECK(_new_DasXport_inflate(NULL) == NULL, "inflate needs an inner transport");

	free(pOut);
	free(pZip);
	free(pIn);
}

/* ************************************************************************* */
int main(int argc, char** argv)
{
	daslib_init(argv[0], DASERRDISP_RET, DASLOG_ERROR, NULL);

	if(argc < 2){
		fprintf(stderr, "Usage: %s SCRATCH_DIR\n", argv[0]);
		return 13;
	}

	testBase();
	testBuf();
	testElastic();
	testFile(argv[1]);
#ifndef _WIN32
	testCmd();
	testSock();
#endif
	testIoMode();
	testZlib();

	if(g_fails > 0){
		printf("ERROR: %d transport checks failed\n", g_fails);
		return 13;
	}
	printf("INFO: All transport checks passed\n");
	return 0;
}
