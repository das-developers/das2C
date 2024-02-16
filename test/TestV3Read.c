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

const char* g_sTestFiles[] = {
	"./test/tag_test.dNt",
	"./test/ex12_sounder_xyz.d3t"
};
const int g_nTestFiles = 2;

DasErrCode onStream(StreamDesc* pSd, void* pUser){
	fputs("\n", stdout);
	char sBuf[16000] = {'\0'};
	StreamDesc_info(pSd, sBuf, 15999);
	fputs(sBuf, stdout);
	return DAS_OKAY;	
}

DasErrCode onDataset(StreamDesc* pSd, DasDs* pDs, void* pUser)
{
	char sBuf[16000] = {'\0'};
	DasDs_toStr(pDs, sBuf, 15999);
	fputs(sBuf, stdout);
	return DAS_OKAY;
}

DasErrCode onData(StreamDesc* pSd, DasDs* pDs, void* pUser)
{
	char sBuf[128] = {'\0'};
	ptrdiff_t aShape[DASIDX_MAX] = DASIDX_INIT_UNUSED;
	
	int nRank = DasDs_shape(pDs, aShape);
	das_shape_prnRng(aShape, nRank, nRank, sBuf, 127);

	printf("Dataset %s shape is now: %s\n", DasDs_id(pDs), sBuf);

	return DAS_OKAY;
}

int main(int argc, char** argv)
{
   /* Exit on errors, log info messages and above */
	das_init(argv[0], DASERR_DIS_EXIT, 0, DASLOG_INFO, NULL);

	for(int i = 0; i < g_nTestFiles; ++i){

		printf("INFO: Reading %s\n", g_sTestFiles[i]);
		FILE* pFile = fopen(g_sTestFiles[i], "r");

		DasIO* pIn = new_DasIO_cfile("TestV3Read", pFile, "r");

		StreamHandler handler;
		memset(&handler, 0, sizeof(StreamHandler));
		handler.streamDescHandler = onStream;
		handler.dsDescHandler = onDataset;
		handler.dsDataHandler = onData;
		DasIO_addProcessor(pIn, &handler);

		/* Just read it parsing packets.  Don't invoke any stream handlers to
		   do stuff with the packets */
		if(DasIO_readAll(pIn) != 0){
			printf("ERROR: Test %d failed, couldn't parse %s\n", i, g_sTestFiles[i]);
			return 64;
		}

		del_DasIO(pIn); /* Should free all memory, check with valgrind*/

		printf("INFO: %s parsed without errors\n", g_sTestFiles[i]);
	}

	return 0;
}
