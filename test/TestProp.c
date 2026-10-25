/** @file TestProp.c Round-trip das streams to exercise property serialization */

/* Author: Claude Opus 4.8 under the direction of Chris Piker <chris-piker@uiowa.edu>
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
#include <string.h>

#include <das3/core.h>

/* Read a stream and write it straight back out so the result can be diffed
   against a blessed golden file.  This exercises the *output* side of property
   handling: separator emission, the das3 whitespace canonicalization, and the
   long-stringArray line wrap -- none of which the read-only TestV3Read sees. */

DasErrCode onStream(DasStream* pSd, void* pUser){
	DasIO* pOut = (DasIO*)pUser;
	/* Re-emit in the same version we read (output defaults to das2): set
	   both the wire version and the internal model. */
	das_stream_ver nVer = DasStream_getVer(pSd);
	if(nVer == STREAM_VER_UNK)
		return das_error(DASERR_IO, "Unknown stream version '%s'", pSd->version);
	DasErrCode nRet;
	if((nRet = DasIO_setOutVer(pOut, nVer)) != DAS_OKAY) return nRet;
	nRet = DasIO_setModel(
		pOut, (nVer == STREAM_VER_3_0) ? STREAM_MODEL_V3 : STREAM_MODEL_V2
	);
	if(nRet != DAS_OKAY) return nRet;
	return DasIO_writeStreamDesc(pOut, pSd);
}

DasErrCode onDataset(DasStream* pSd, int iPktId, DasDs* pDs, void* pUser){
	return DasIO_writeDesc((DasIO*)pUser, (DasDesc*)pDs, iPktId);
}

DasErrCode onData(DasStream* pSd, int iPktId, DasDs* pDs, void* pUser){
	return DasIO_writeData((DasIO*)pUser, (DasDesc*)pDs, iPktId);
}

int main(int argc, char** argv)
{
	das_init(argv[0], DASERR_DIS_EXIT, 0, DASLOG_WARN, NULL);

	if(argc < 2){
		fprintf(stderr, "Usage: %s STREAM_FILE  (re-emits the stream on stdout)\n", argv[0]);
		return 13;
	}

	FILE* pFile = fopen(argv[1], "rb");
	if(pFile == NULL){
		fprintf(stderr, "ERROR: Couldn't open %s\n", argv[1]);
		return 64;
	}

	DasIO* pIn = new_DasIO_cfile("TestProp", pFile, "r");
	DasIO_setModel(pIn, STREAM_MODEL_MIXED);

	DasIO* pOut = new_DasIO_cfile("TestProp", stdout, "w");

	StreamHandler handler;
	memset(&handler, 0, sizeof(StreamHandler));
	handler.streamDescHandler = onStream;
	handler.dsDescHandler = onDataset;
	handler.dsDataHandler = onData;
	handler.userData = pOut;
	DasIO_addProcessor(pIn, &handler);

	if(DasIO_readAll(pIn) != 0){
		fprintf(stderr, "ERROR: Couldn't parse %s\n", argv[1]);
		return 64;
	}

	DasIO_close(pOut);   /* flush the re-emitted stream */
	del_DasIO(pIn);
	del_DasIO(pOut);

	return 0;
}
