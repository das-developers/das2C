/** @file TestV3Read.c Testing basic das stream v3.0 packet parsing */

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

#define _POSIX_C_SOURCE 200112L

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <das3/core.h>

DasStream* g_pSdOut = NULL;

/* The _info printers promise snprintf semantics (issue #7): the full length
   comes back no matter the buffer size, the buffer is always NUL terminated,
   and nothing past uLen is touched.  Every fixture runs through here, so
   check the promise at the exact size and at sizes that cut the output, with
   guard bytes past the end to catch overruns. */
typedef int (*info_fn)(const void* pObj, char* sBuf, size_t uLen);

static int _infoDs(const void* p, char* s, size_t u){ return DasDs_info(p, s, u); }
static int _infoDim(const void* p, char* s, size_t u){ return DasDim_info(p, s, u); }
static int _infoSd(const void* p, char* s, size_t u){ return DasStream_info(p, s, u); }

/* The _toStr one-liners: the first line of the matching _info, sBuf when it
   fits, NULL when cut, NUL terminated and inside uLen either way. */
typedef char* (*str_fn)(const void* pObj, char* sBuf, size_t uLen);

static char* _strDs(const void* p, char* s, size_t u){ return DasDs_toStr(p, s, u); }
static char* _strDim(const void* p, char* s, size_t u){ return DasDim_toStr(p, s, u); }
static char* _strSd(const void* p, char* s, size_t u){ return DasStream_toStr(p, s, u); }

#define GUARD 16

static void _sizeFail(const char* sWhat, size_t uLen, const char* sWhy)
{
	fprintf(stderr, "ERROR: %s, uLen=%zu: %s\n", sWhat, uLen, sWhy);
	exit(13);
}

static char* checkInfo(const char* sWhat, info_fn fn, const void* pObj)
{
	int nNeed = fn(pObj, NULL, 0);
	if(nNeed < 0) _sizeFail(sWhat, 0, "negative length");
	size_t uNeed = (size_t)nNeed;

	char* sFull = (char*)malloc(uNeed + 1);
	if(fn(pObj, sFull, uNeed + 1) != nNeed)
		_sizeFail(sWhat, uNeed + 1, "length changed between calls");
	if(strlen(sFull) != uNeed)
		_sizeFail(sWhat, uNeed + 1, "returned length != strlen of output");

	size_t aCut[] = {0, 1, 2, uNeed / 2, uNeed};
	for(size_t i = 0; i < sizeof(aCut)/sizeof(size_t); ++i){
		size_t uLen = aCut[i];
		if(uLen > uNeed) continue;
		char* sCut = (char*)malloc(uLen + GUARD);
		memset(sCut, 0x5A, uLen + GUARD);

		if(fn(pObj, sCut, uLen) != nNeed)
			_sizeFail(sWhat, uLen, "cut output did not return the full length");
		if(uLen > 0){
			if(strlen(sCut) != uLen - 1)
				_sizeFail(sWhat, uLen, "cut output not NUL terminated at uLen-1");
			if(memcmp(sCut, sFull, uLen - 1) != 0)
				_sizeFail(sWhat, uLen, "cut output is not a prefix of the full one");
		}
		for(size_t j = uLen; j < uLen + GUARD; ++j)
			if(sCut[j] != 0x5A) _sizeFail(sWhat, uLen, "wrote past uLen");
		free(sCut);
	}
	return sFull;
}

static void checkToStr(const char* sWhat, str_fn fn, const void* pObj, const char* sInfo)
{
	const char* pNl = strchr(sInfo, '\n');
	size_t uLine = pNl ? (size_t)(pNl - sInfo) : strlen(sInfo);

	size_t aLen[] = {uLine + 1, uLine, 1, 0};
	for(size_t i = 0; i < sizeof(aLen)/sizeof(size_t); ++i){
		size_t uLen = aLen[i];
		char* sBuf = (char*)malloc(uLen + GUARD);
		memset(sBuf, 0x5A, uLen + GUARD);

		char* pRet = fn(pObj, sBuf, uLen);
		bool bFits = (uLen > uLine);
		if(bFits && (pRet != sBuf))
			_sizeFail(sWhat, uLen, "did not return sBuf when the line fit");
		if(!bFits && (pRet != NULL))
			_sizeFail(sWhat, uLen, "did not return NULL when the line was cut");
		if(uLen > 0){
			size_t uExpect = bFits ? uLine : uLen - 1;
			if(strlen(sBuf) != uExpect)
				_sizeFail(sWhat, uLen, "not NUL terminated where expected");
			if(memcmp(sBuf, sInfo, uExpect) != 0)
				_sizeFail(sWhat, uLen, "is not the first line of the _info output");
		}
		for(size_t j = uLen; j < uLen + GUARD; ++j)
			if(sBuf[j] != 0x5A) _sizeFail(sWhat, uLen, "wrote past uLen");
		free(sBuf);
	}
}

DasErrCode onStream(DasStream* pSd, void* pUser){
	fputs("\n", stdout);
	char* sInfo = checkInfo("DasStream_info", _infoSd, pSd);
	checkToStr("DasStream_toStr", _strSd, pSd, sInfo);
	fputs(sInfo, stdout);
	free(sInfo);
	return DAS_OKAY;	
}

DasErrCode onDataset(DasStream* pSd, int iPktId, DasDs* pDs, void* pUser)
{
	for(size_t u = 0; u < pDs->uDims; ++u){
		char* sDim = checkInfo("DasDim_info", _infoDim, pDs->lDims[u]);
		checkToStr("DasDim_toStr", _strDim, pDs->lDims[u], sDim);
		free(sDim);
	}

	char* sInfo = checkInfo("DasDs_info", _infoDs, pDs);
	checkToStr("DasDs_toStr", _strDs, pDs, sInfo);
	fputs(sInfo, stdout);
	free(sInfo);
	return DAS_OKAY;
}

static void prnShape(int iPktId, DasDs* pDs)
{
	char sBuf[128] = {'\0'};
	ptrdiff_t aShape[VARIDX_MAX] = VARIDX_INIT_UNUSED;

	int nRank = DasDs_shape(pDs, aShape);
	das_shape_info(aShape, nRank, nRank, sBuf, 127);

	printf("Dataset %d shape is now: %s\n", iPktId, sBuf);
}

/* Per-packet-id state so the shape line can be subsampled.  Some fixtures carry
   thousands of records; printing the growing shape on every callback buried
   `make test` under ~6000 near-identical lines.  Sample the first packet and
   every 1000th, then print the authoritative final shape once at stream close
   (onClose below), since the subsample almost never lands on the last record. */
#define MAX_PKT_IDS 256
static int64_t g_aDataCount[MAX_PKT_IDS] = {0};
static DasDs*  g_aLastDs[MAX_PKT_IDS]    = {NULL};

DasErrCode onData(DasStream* pSd, int iPktId, DasDs* pDs, void* pUser)
{
	if((iPktId < 0)||(iPktId >= MAX_PKT_IDS)){  /* out of range: don't subsample */
		prnShape(iPktId, pDs);
		return DAS_OKAY;
	}

	g_aLastDs[iPktId] = pDs;
	if((g_aDataCount[iPktId]++ % 1000) == 0)
		prnShape(iPktId, pDs);

	return DAS_OKAY;
}

DasErrCode onClose(DasStream* pSd, void* pUser)
{
	for(int i = 0; i < MAX_PKT_IDS; ++i){
		if(g_aLastDs[i] != NULL){
			prnShape(i, g_aLastDs[i]);
			g_aLastDs[i] = NULL;    /* reset for the next stream in the argv loop */
			g_aDataCount[i] = 0;
		}
	}
	return DAS_OKAY;
}

/* Streams that validate against the schema but hit an unimplemented das2C read
   path.  Skipped (not deleted) so they rejoin the run automatically once the
   feature lands. */
static const char* g_aXfail[][2] = {
	/* Adding {example, reason} entries here as new tests appear in the test
		tree before they are supported, otherwise I might forget to implement
		the corresponding functionality. */
	/* ex26 multi-index <sequence> (offset[j][k]) landed 2026-06-27. */
	/* ex27 native-byte blob ({N}<bytes>) landed 2026-07-10. */
	/* ex28 transports the same PNG as ex27 but asks das2C to MATERIALIZE it:
	   semantic="integer", index="*;256;280", encoding="blob" mime="image/png".
	   The {N} blob decodes fine -- what's missing is a secondary (block) decoder
	   to inflate the PNG block into the declared 256x280 integer grid.  Remove
	   once the mime/block-codec layer lands. */
	{"ex28_epop_fai_mgf_img.d3b", "no secondary PNG block decoder to inflate the blob into samples"},
	/* ex29 is the extension-contract demo: an image/jpeg blob the author declares
	   decodes to a single "pixel count" integer.  With no jpeg codec registered it
	   fails loud BY DESIGN -- that is the lesson.  A plain parse can't honor it; it
	   rejoins when a codec lands, or is exercised via embed-as-bytes (das3_text -f)
	   elsewhere. */
	{"ex29_ext_contract.d3t", "image/jpeg extension contract; no codec registered, fails loud by design"},
	{"", ""},  /* inert placeholder: keeps a valid non-empty array under -Werror */
};
static const int g_nXfail = sizeof(g_aXfail)/sizeof(g_aXfail[0]);

static const char* xfailReason(const char* sPath){
	const char* sBase = sPath;          /* strip any directory prefix */
	for(const char* p = sPath; *p; ++p)
		if((*p == '/')||(*p == '\\')) sBase = p + 1;
	for(int i = 0; i < g_nXfail; ++i)
		if(strcmp(sBase, g_aXfail[i][0]) == 0) return g_aXfail[i][1];
	return NULL;
}

int main(int argc, char** argv)
{
   /* Exit on errors, log info messages and above */
	das_init(argv[0], DASERR_DIS_EXIT, 0, DASLOG_INFO, NULL);

	/* Streams to read are given on the command line (caller globs them) */
	if(argc < 2){
		fprintf(stderr, "Usage: %s STREAM_FILE [STREAM_FILE ...]\n", argv[0]);
		return 13;
	}

	for(int i = 1; i < argc; ++i){

		const char* sXfail = xfailReason(argv[i]);
		if(sXfail != NULL){
			printf("INFO: Skipping %s (known xfail: %s)\n", argv[i], sXfail);
			continue;
		}

		printf("INFO: Reading %s\n", argv[i]);
		FILE* pFile = fopen(argv[i], "rb");
		if(pFile == NULL){
			printf("ERROR: Couldn't open %s\n", argv[i]);
			return 64;
		}

		DasIO* pIn = new_DasIO_cfile("TestV3Read", pFile, "r");
		DasIO_model(pIn, STREAM_MODEL_MIXED); /* das1/2/3 auto-detect */

		StreamHandler handler;
		memset(&handler, 0, sizeof(StreamHandler));
		handler.streamDescHandler = onStream;
		handler.dsDescHandler = onDataset;
		handler.dsDataHandler = onData;
		handler.closeHandler = onClose;
		DasIO_addProcessor(pIn, &handler);

		if(DasIO_readAll(pIn) != 0){
			printf("ERROR: Couldn't parse %s\n", argv[i]);
			return 64;
		}

		del_DasIO(pIn); /* Should free all memory, check with valgrind*/

		printf("INFO: %s parsed without errors\n", argv[i]);
	}

	return 0;
}
