/** @file TestV3Read.c Testing basic das stream v3.0 packet parsing */

/* Author: Chris Piker <chris-piker@uiowa.edu>
 * 
 * This file contains test and example code that intends to explain an
 * interface.
 * 
 * As United States courts have ruled that interfaces cannot be copyrighted,
 * the code in this individual source file, TestBuilder.c, is placed into the
 * public domain and may be displayed, incorporated or otherwise re-used without
 * restriction.  It is offered to the public without any without any warranty
 * including even the implied warranty of merchantability or fitness for a
 * particular purpose. 
 */

#define _POSIX_C_SOURCE 200112L

#include <stdio.h>
#include <string.h>

#include <das2/core.h>

StreamDesc* g_pSdOut = NULL;

DasErrCode onStream(StreamDesc* pSd, void* pUser){
	fputs("\n", stdout);
	char sBuf[16000] = {'\0'};
	StreamDesc_info(pSd, sBuf, 15999);
	fputs(sBuf, stdout);
	return DAS_OKAY;	
}

DasErrCode onDataset(StreamDesc* pSd, int iPktId, DasDs* pDs, void* pUser)
{
	char sBuf[16000] = {'\0'};
	DasDs_toStr(pDs, sBuf, 15999);
	fputs(sBuf, stdout);
	return DAS_OKAY;
}

DasErrCode onData(StreamDesc* pSd, int iPktId, DasDs* pDs, void* pUser)
{
	char sBuf[128] = {'\0'};
	ptrdiff_t aShape[DASIDX_MAX] = DASIDX_INIT_UNUSED;
	
	int nRank = DasDs_shape(pDs, aShape);
	das_shape_prnRng(aShape, nRank, nRank, sBuf, 127);

	printf("Dataset %d shape is now: %s\n", iPktId, sBuf);

	return DAS_OKAY;
}

/* Streams that validate against the schema but hit an unimplemented das2C read
   path.  Skipped (not deleted) so they rejoin the run automatically once the
   feature lands. */
static const char* g_aXfail[][2] = {
	/* Adding {example, reason} entries here as new tests appear in the test
		tree before they are supported, otherwise I might forget to implement
		the corresponding functionality. */
	{"ex26_isee_rapid_rank3.d3b",
	 "multi-index <sequence> (offset[j][k], index=\"-;16;128\") not yet supported"},
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
		FILE* pFile = fopen(argv[i], "r");
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
