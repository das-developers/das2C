/** @file TestProp.c Round-trip das streams to exercise property serialization */

/* Author: Claude Opus 4.8 under the direction of Chris Piker <chris-piker@uiowa.edu>
 *
 * This file contains test and example code that intends to explain an
 * interface.
 *
 * As United States courts have ruled that interfaces cannot be copyrighted,
 * the code in this individual source file, TestProp.c, is placed into the
 * public domain and may be displayed, incorporated or otherwise re-used without
 * restriction.  It is offered to the public without any without any warranty
 * including even the implied warranty of merchantability or fitness for a
 * particular purpose.
 */

#define _POSIX_C_SOURCE 200112L

#include <stdio.h>
#include <string.h>

#include <das2/core.h>

/* Read a stream and write it straight back out so the result can be diffed
   against a blessed golden file.  This exercises the *output* side of property
   handling: separator emission, the das3 whitespace canonicalization, and the
   long-stringArray line wrap -- none of which the read-only TestV3Read sees. */

DasErrCode onStream(DasStream* pSd, void* pUser){
	DasIO* pOut = (DasIO*)pUser;
	/* Re-emit in the same major version we read (output defaults to das2): set
	   both the wire version (dasver) and the internal model. */
	int nVer = (pSd->version[0] == DAS_30_STREAM_VER[0]) ? 3 : 2;
	pOut->dasver = nVer;
	DasIO_model(pOut, nVer);
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

	FILE* pFile = fopen(argv[1], "r");
	if(pFile == NULL){
		fprintf(stderr, "ERROR: Couldn't open %s\n", argv[1]);
		return 64;
	}

	DasIO* pIn = new_DasIO_cfile("TestProp", pFile, "r");
	DasIO_model(pIn, STREAM_MODEL_MIXED);

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
