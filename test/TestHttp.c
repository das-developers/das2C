/** @file TestHttp.c Unit tests for the HTTP client that need no network: the
 * URL parser, and reading a reply's headers through a transport. */

/* Author: Chris Piker <chris-piker@uiowa.edu>, via Claude Fable 5.1
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

/* DasUrlParts_fromStr() splits a URL in place.  The properties under test are that
 * every part is found whole, that the das2.2 dataset= value is only taken as
 * a whole parameter, and that anything a server request can't carry safely is
 * refused.
 *
 * The header reader is fed canned replies through a memory transport.  It
 * reads in chunks, so it always takes some of the body with the headers;
 * the property under test is that those bytes go back to the transport and
 * the body reads out whole. */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>

#include <das3/core.h>
#include <das3/http_p.h>   /* tests link the static archive, so they may */

static int g_nFail = 0;
static int g_nPass = 0;

#define CHECK(cond, ...) do{ \
	if(cond){ ++g_nPass; } \
	else{ ++g_nFail; printf("  FAIL line %d: ", __LINE__); printf(__VA_ARGS__); printf("\n"); } \
}while(0)

/* Is the part present and equal to sWant?  NULL sWant means absent. */
static bool partIs(const char* sUrl, const DasUrlParts* pParts, das_urlpart e,
                   const char* sWant)
{
	size_t uLen = 0;
	const char* p = DasUrlParts_get(pParts, sUrl, e, &uLen);
	if(sWant == NULL) return (p == NULL);
	return (p != NULL) && (uLen == strlen(sWant)) && (strncmp(p, sWant, uLen) == 0);
}

static void testParse(void)
{
	DasUrlParts parts;
	const char* sUrl;

	printf("INFO: Splitting well formed URLs\n");

	sUrl = "http://host/path?a=1";
	CHECK(DasUrlParts_fromStr(&parts, sUrl), "plain http parses");
	CHECK(partIs(sUrl, &parts, DASURLPART_SCHEME, "http"), "scheme");
	CHECK(partIs(sUrl, &parts, DASURLPART_HOST, "host"), "host");
	CHECK(partIs(sUrl, &parts, DASURLPART_PORT, "80"), "default http port");
	CHECK(partIs(sUrl, &parts, DASURLPART_PATH, "/path"), "path");
	CHECK(partIs(sUrl, &parts, DASURLPART_QUERY, "a=1"), "query");
	CHECK(partIs(sUrl, &parts, DASURLPART_DATASET, NULL), "no dataset");
	CHECK(partIs(sUrl, &parts, DASURLPART_USERINFO, NULL), "no userinfo");
	CHECK(partIs(sUrl, &parts, DASURLPART_FRAGMENT, NULL), "no fragment");

	sUrl = "https://space.physics.uiowa.edu:8443/das/stream/source/tracers/flex"
	       "?read.time.min=2026-01-01T00%3A00#top";
	CHECK(DasUrlParts_fromStr(&parts, sUrl), "https with port parses");
	CHECK(partIs(sUrl, &parts, DASURLPART_PORT, "8443"), "explicit port");
	size_t uLen = 0;
	const char* pPort = DasUrlParts_get(&parts, sUrl, DASURLPART_PORT, &uLen);
	CHECK((pPort >= sUrl) && (pPort < sUrl + strlen(sUrl)), "explicit port points into the URL");
	CHECK(partIs(sUrl, &parts, DASURLPART_PATH, "/das/stream/source/tracers/flex"), "path");
	CHECK(partIs(sUrl, &parts, DASURLPART_QUERY, "read.time.min=2026-01-01T00%3A00"),
	      "query stops at #, still encoded");
	CHECK(partIs(sUrl, &parts, DASURLPART_FRAGMENT, "top"), "fragment");

	sUrl = "https://host";
	CHECK(DasUrlParts_fromStr(&parts, sUrl), "bare host parses");
	CHECK(partIs(sUrl, &parts, DASURLPART_PORT, "443"), "default https port");
	CHECK(partIs(sUrl, &parts, DASURLPART_PATH, ""), "empty path is present, length 0");
	CHECK(partIs(sUrl, &parts, DASURLPART_QUERY, NULL), "no query");

	sUrl = "http://h/p?";
	CHECK(DasUrlParts_fromStr(&parts, sUrl) && partIs(sUrl, &parts, DASURLPART_QUERY, ""),
	      "empty query is present, length 0");

	sUrl = "http://h/x?server=dataset&dataset=Juno/WAV/Survey&start_time=1";
	CHECK(DasUrlParts_fromStr(&parts, sUrl), "das2.2 style query parses");
	CHECK(partIs(sUrl, &parts, DASURLPART_DATASET, "Juno/WAV/Survey"), "dataset stops at &");

	sUrl = "http://h/x?mydataset=nope&dataset=yes";
	CHECK(DasUrlParts_fromStr(&parts, sUrl) && partIs(sUrl, &parts, DASURLPART_DATASET, "yes"),
	      "dataset only matches a whole parameter name");

	sUrl = "http://user:p%40ss@host:81/";
	CHECK(DasUrlParts_fromStr(&parts, sUrl), "userinfo parses");
	CHECK(partIs(sUrl, &parts, DASURLPART_USERINFO, "user:p%40ss"), "userinfo");
	CHECK(partIs(sUrl, &parts, DASURLPART_HOST, "host"), "host after userinfo");
	CHECK(partIs(sUrl, &parts, DASURLPART_PORT, "81"), "port after userinfo");

	sUrl = "http://[::1]:8080/das";
	CHECK(DasUrlParts_fromStr(&parts, sUrl), "IPv6 literal parses");
	CHECK(partIs(sUrl, &parts, DASURLPART_HOST, "[::1]"), "IPv6 host keeps its brackets");
	CHECK(partIs(sUrl, &parts, DASURLPART_PORT, "8080"), "port after IPv6 literal");

	sUrl = "file:///tmp/x.d3b";
	CHECK(DasUrlParts_fromStr(&parts, sUrl), "file URL parses");
	CHECK(partIs(sUrl, &parts, DASURLPART_HOST, ""), "file URL host is empty");
	CHECK(partIs(sUrl, &parts, DASURLPART_PORT, NULL), "no default port for file:");
	CHECK(partIs(sUrl, &parts, DASURLPART_PATH, "/tmp/x.d3b"), "file URL path");

	printf("INFO: Refusing malformed URLs\n");

	CHECK(!DasUrlParts_fromStr(&parts, "host/x"), "no scheme refused");
	CHECK(!DasUrlParts_fromStr(&parts, "1http://host/"), "scheme must start with a letter");
	CHECK(!DasUrlParts_fromStr(&parts, "http://host:99999/x"), "port out of range refused");
	CHECK(!DasUrlParts_fromStr(&parts, "http://host:abc/x"), "non numeric port refused");
	CHECK(!DasUrlParts_fromStr(&parts, "http://host/a b"), "space refused");
	CHECK(!DasUrlParts_fromStr(&parts, "http://host/a\r\nX-Evil: 1"), "CR LF refused");
	CHECK(!DasUrlParts_fromStr(&parts, "http://[::1/x"), "unclosed IPv6 literal refused");
	CHECK(!DasUrlParts_fromStr(&parts, NULL), "NULL refused");

	char* sLong = (char*)malloc(70000);
	memcpy(sLong, "http://h/", 9);
	memset(sLong + 9, 'p', 70000 - 10);
	sLong[69999] = '\0';
	CHECK(!DasUrlParts_fromStr(&parts, sLong), "URL over 65534 bytes refused");
	free(sLong);
}

/* ************************************************************************* */

#define REPLY_OK \
	"HTTP/1.1 200 OK\r\n" \
	"Server: Apache\r\n" \
	"etag: \"5a6-607e10c29b347\"\r\n" \
	"Last-Modified:   Tue, 17 Oct 2023 03:23:33 GMT  \r\n" \
	"Content-Type: application/vnd.das.stream\r\n" \
	"Content-Disposition: attachment; filename=\"juno_2026-001.d3b\"\r\n" \
	"\r\n"

static void testHeaders(void)
{
	printf("INFO: Reading reply headers through a transport\n");

	const char* sBody = "|Sx||14|<stream ...>";
	char sReply[1024];
	snprintf(sReply, sizeof(sReply), "%s%s", REPLY_OK, sBody);

	DasHttpResp* pRes = _new_DasHttpResp();
	DasXport* pX = new_DasXport_mem(sReply, strlen(sReply), "r");
	CHECK(_DasHttpResp_readHdrs(pRes, pX), "canned reply read: %s",
	      DasHttpResp_error(pRes) ? DasHttpResp_error(pRes) : "");
	CHECK(DasHttpResp_code(pRes) == 200, "status code, got %d", DasHttpResp_code(pRes));

	char sBuf[64] = {'\0'};
	ptrdiff_t nRead = DasXport_read(pX, sBuf, sizeof(sBuf) - 1);
	CHECK((nRead == (ptrdiff_t)strlen(sBody)) && (strcmp(sBuf, sBody) == 0),
	      "body read out whole after the headers, got '%s'", sBuf);

	char sOut[128];
	CHECK(DasHttpResp_header(pRes, "ETag", sOut, sizeof(sOut)) &&
	      (strcmp(sOut, "\"5a6-607e10c29b347\"") == 0),
	      "ETag found without regard to case, got '%s'", sOut);
	CHECK(DasHttpResp_header(pRes, "last-modified", sOut, sizeof(sOut)) &&
	      (strcmp(sOut, "Tue, 17 Oct 2023 03:23:33 GMT") == 0),
	      "Last-Modified found and trimmed, got '%s'", sOut);
	CHECK(!DasHttpResp_header(pRes, "Content-Length", sOut, sizeof(sOut)) && (sOut[0] == '\0'),
	      "absent header reports false with an empty buffer");
	CHECK(!DasHttpResp_header(pRes, "Content", sOut, sizeof(sOut)), "a prefix of a name is not a match");
	CHECK(!DasHttpResp_header(pRes, "ETag", sOut, 8), "a value that does not fit is refused, not cut");
	CHECK((DasHttpResp_mime(pRes) != NULL) &&
	      (strcmp(DasHttpResp_mime(pRes), "application/vnd.das.stream") == 0), "media type");
	CHECK((DasHttpResp_filename(pRes) != NULL) &&
	      (strcmp(DasHttpResp_filename(pRes), "juno_2026-001.d3b") == 0),
	      "quoted file name loses both quotes, got '%s'",
	      DasHttpResp_filename(pRes) ? DasHttpResp_filename(pRes) : "(null)");
	del_DasXport(pX);

	/* A failed reply's headers are kept too */
	const char* s401 = "HTTP/1.0 401 Unauthorized\r\nWWW-Authenticate: Basic realm=\"das\"\r\n\r\n";
	pX = new_DasXport_mem((void*)s401, strlen(s401), "r");
	CHECK(_DasHttpResp_readHdrs(pRes, pX) && (DasHttpResp_code(pRes) == 401), "401 read");
	CHECK(DasHttpResp_header(pRes, "WWW-Authenticate", sOut, sizeof(sOut)) &&
	      (strcmp(sOut, "Basic realm=\"das\"") == 0), "401 headers kept, got '%s'", sOut);
	CHECK(DasHttpResp_filename(pRes) == NULL, "earlier reply's file name dropped");
	del_DasXport(pX);

	/* Not HTTP at all */
	const char* sJunk = "SSH-2.0-OpenSSH_8.7\r\n\r\n";
	pX = new_DasXport_mem((void*)sJunk, strlen(sJunk), "r");
	CHECK(!_DasHttpResp_readHdrs(pRes, pX) && (DasHttpResp_error(pRes) != NULL),
	      "non HTTP reply refused");
	del_DasXport(pX);

	/* Connection dropped part way through the headers */
	const char* sCut = "HTTP/1.1 200 OK\r\nServer: x\r\n";
	pX = new_DasXport_mem((void*)sCut, strlen(sCut), "r");
	CHECK(!_DasHttpResp_readHdrs(pRes, pX) && (DasHttpResp_error(pRes) != NULL),
	      "headers cut short refused");
	del_DasXport(pX);

	CHECK(DasHttpResp_takeBody(pRes) == NULL, "no body to take from a canned reply");
	del_DasHttpResp(pRes);
	del_DasHttpResp(NULL);

	DasHttpOpts opts;
	init_DasHttpOpts(&opts);
	CHECK((opts.uStructVer == DASHTTPOPTS_VER) && (opts.ipVersion == DASIPVER_ANY) &&
	      (opts.readSec <= 0.0) && (opts.connectSec == DASHTTP_TIMEOUT_MIN), "option defaults");

	/* A zeroed struct is refused before anything is sent, not run on defaults */
	DasHttpOpts zero;
	memset(&zero, 0, sizeof(zero));
	zero.extraHeaders = "If-None-Match: \"x\"\r\n";
	pRes = new_DasHttpResp_getEx("http://localhost/", NULL, &zero);
	CHECK((pRes != NULL) && !DasHttpResp_isOk(pRes) && (DasHttpResp_code(pRes) == -1) &&
	      (DasHttpResp_error(pRes) != NULL) && strstr(DasHttpResp_error(pRes), "init_DasHttpOpts"),
	      "zeroed options refused, got '%s'",
	      (pRes && DasHttpResp_error(pRes)) ? DasHttpResp_error(pRes) : "(null)");
	del_DasHttpResp(pRes);

	/* A program built against a later header is refused, the library can't
	   know what its extra members mean */
	DasHttpOpts later = opts;
	later.uStructVer = DASHTTPOPTS_VER + 1;
	pRes = new_DasHttpResp_getEx("http://localhost/", NULL, &later);
	CHECK((pRes != NULL) && !DasHttpResp_isOk(pRes) && (DasHttpResp_code(pRes) == -1) &&
	      (DasHttpResp_error(pRes) != NULL) && strstr(DasHttpResp_error(pRes), "newer"),
	      "later options version refused, got '%s'",
	      (pRes && DasHttpResp_error(pRes)) ? DasHttpResp_error(pRes) : "(null)");
	del_DasHttpResp(pRes);
}

/* ************************************************************************* */

int main(int argc, char** argv)
{
	(void)argc;
	daslib_init(argv[0], DASERRDISP_RET, DASLOG_ERROR, NULL);
	daslog_setLevel(DASLOG_CRITICAL);   /* the parse tests refuse on purpose throughout */

	testParse();
	testHeaders();

	printf("INFO: %d checks passed, %d failed\n", g_nPass, g_nFail);
	return (g_nFail == 0) ? 0 : 13;
}
