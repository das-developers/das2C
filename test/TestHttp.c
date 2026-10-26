/** @file TestHttp.c Unit tests for http.h that need no network: today the
 * URL parser behind DasHttpResp_init(). */

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

/* The parser has fixed size fields.  The property under test is that a part
 * which fits is kept whole and a part which does not is refused with an
 * error message, never silently shortened.  A shortened query would reach
 * the server as a different request and a shortened dataset would match
 * the wrong saved password. */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>

#include <das3/core.h>
#include <das3/http.h>

static int g_nFail = 0;
static int g_nPass = 0;

#define CHECK(cond, ...) do{ \
	if(cond){ ++g_nPass; } \
	else{ ++g_nFail; printf("  FAIL line %d: ", __LINE__); printf(__VA_ARGS__); printf("\n"); } \
}while(0)

static bool streq(const char* a, const char* b)
{
	if((a == NULL)||(b == NULL)) return (a == b);
	return strcmp(a, b) == 0;
}

/* Parse into a cleared response.  Any error string is freed before the
 * next call so a leak checker stays quiet. */
static bool parse(DasHttpResp* pRes, const char* sUrl)
{
	if(pRes->sError){ free(pRes->sError); pRes->sError = NULL; }
	memset(pRes, 0, sizeof(DasHttpResp));
	return DasHttpResp_init(pRes, sUrl);
}

/* A URL whose named part is exactly uLen characters long */
static void mkUrl(char* sBuf, size_t uBuf, const char* sPart, size_t uLen)
{
	size_t u = 0;
	if(strcmp(sPart, "scheme") == 0){
		for(; u < uLen; ++u) sBuf[u] = 'h';
		snprintf(sBuf + u, uBuf - u, "://host/p");
		memcpy(sBuf, "http", 4);  /* still not a valid scheme, length is the point */
		return;
	}
	if(strcmp(sPart, "host") == 0){
		u = snprintf(sBuf, uBuf, "http://");
		for(size_t i = 0; i < uLen; ++i) sBuf[u++] = 'h';
		snprintf(sBuf + u, uBuf - u, "/p");
		return;
	}
	if(strcmp(sPart, "path") == 0){
		u = snprintf(sBuf, uBuf, "http://host/");
		for(size_t i = 1; i < uLen; ++i) sBuf[u++] = 'p';
		snprintf(sBuf + u, uBuf - u, "?q=1");
		return;
	}
	if(strcmp(sPart, "query") == 0){
		u = snprintf(sBuf, uBuf, "http://host/p?");
		for(size_t i = 0; i < uLen; ++i) sBuf[u++] = 'q';
		sBuf[u] = '\0';
		return;
	}
	if(strcmp(sPart, "dataset") == 0){
		u = snprintf(sBuf, uBuf, "http://host/p?server=dataset&dataset=");
		for(size_t i = 0; i < uLen; ++i) sBuf[u++] = 'd';
		snprintf(sBuf + u, uBuf - u, "&start_time=2026");
		return;
	}
}

int main(int argc, char** argv)
{
	das_init(argv[0], DASERR_DIS_EXIT, 0, DASLOG_WARN, NULL);

	DasHttpResp res;
	memset(&res, 0, sizeof(DasHttpResp));
	char sUrl[DASURL_SZ_QUERY + 256];
	char sOut[DASURL_SZ_QUERY + 256];

	printf("INFO: Parsing well formed URLs\n");

	CHECK(parse(&res, "http://host/path?a=1"), "plain http parses");
	CHECK(streq(res.url.sScheme, "http"), "scheme");
	CHECK(streq(res.url.sHost, "host"), "host");
	CHECK(streq(res.url.sPort, "80"), "default http port, got %s", res.url.sPort);
	CHECK(streq(res.url.sPath, "/path"), "path, got %s", res.url.sPath);
	CHECK(streq(res.url.sQuery, "a=1"), "query, got %s", res.url.sQuery);
	CHECK(res.url.sDataset[0] == '\0', "no dataset");
	CHECK(streq(das_url_toStr(&res.url, sOut, sizeof(sOut)), "http://host/path?a=1"),
	      "round trip drops the default port, got %s", sOut);

	CHECK(parse(&res, "https://space.physics.uiowa.edu:8443/das/stream/source/tracers/flex"
	                  "?read.time.min=2026-01-01T00%3A00"), "https with port parses");
	CHECK(streq(res.url.sPort, "8443"), "explicit port, got %s", res.url.sPort);
	CHECK(streq(res.url.sPath, "/das/stream/source/tracers/flex"), "path, got %s", res.url.sPath);
	CHECK(streq(res.url.sQuery, "read.time.min=2026-01-01T00%3A00"), "query, got %s", res.url.sQuery);
	CHECK(streq(das_url_toStr(&res.url, sOut, sizeof(sOut)),
	      "https://space.physics.uiowa.edu:8443/das/stream/source/tracers/flex?read.time.min=2026-01-01T00%3A00"),
	      "round trip keeps an explicit port, got %s", sOut);

	CHECK(parse(&res, "https://host"), "bare host parses");
	CHECK(streq(res.url.sPort, "443"), "default https port, got %s", res.url.sPort);
	CHECK(res.url.sPath[0] == '\0', "empty path");
	CHECK(res.url.sQuery[0] == '\0', "empty query");

	CHECK(parse(&res, "http://h/x?server=dataset&dataset=Juno/WAV/Survey&start_time=1"),
	      "das2.2 style query parses");
	CHECK(streq(res.url.sDataset, "Juno/WAV/Survey"), "dataset stops at &, got %s", res.url.sDataset);

	printf("INFO: Refusing malformed URLs\n");

	CHECK(!parse(&res, "ftp://host/x") && res.sError, "ftp scheme refused");
	CHECK(!parse(&res, "http://") && res.sError, "empty host refused");
	CHECK(!parse(&res, "http://host:99999/x") && res.sError, "port out of range refused");
	CHECK(!parse(&res, "http://host:abc/x") && res.sError, "non numeric port refused");
	CHECK(!parse(&res, "http://host:/x") && res.sError, "empty port refused");

	printf("INFO: Parts at the size limit fit, one byte over is refused\n");

	struct { const char* sPart; size_t uMax; } aLim[] = {
		{"host", DASURL_SZ_HOST}, {"path", DASURL_SZ_PATH},
		{"query", DASURL_SZ_QUERY}, {"dataset", DASURL_SZ_DATASET}
	};
	for(size_t i = 0; i < sizeof(aLim)/sizeof(aLim[0]); ++i){
		mkUrl(sUrl, sizeof(sUrl), aLim[i].sPart, aLim[i].uMax);
		CHECK(parse(&res, sUrl), "%s of %zu bytes parses: %s", aLim[i].sPart, aLim[i].uMax,
		      res.sError ? res.sError : "");

		mkUrl(sUrl, sizeof(sUrl), aLim[i].sPart, aLim[i].uMax + 1);
		bool bOk = parse(&res, sUrl);
		CHECK(!bOk, "%s of %zu bytes is refused", aLim[i].sPart, aLim[i].uMax + 1);
		CHECK(!bOk && res.sError && strstr(res.sError, aLim[i].sPart),
		      "refusal names the %s, got: %s", aLim[i].sPart, res.sError ? res.sError : "(null)");
	}

	/* The scheme is checked by value before anything else, so an over long
	   one is refused either way; make sure it is refused and not overrun */
	mkUrl(sUrl, sizeof(sUrl), "scheme", DASURL_SZ_SCHEME + 1);
	CHECK(!parse(&res, sUrl) && res.sError, "over long scheme refused");
	if(res.sError){ free(res.sError); res.sError = NULL; }

	printf("INFO: %d checks passed, %d failed\n", g_nPass, g_nFail);
	return (g_nFail == 0) ? 0 : 13;
}
