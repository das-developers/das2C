/** @file TestIter.c Unit tests for iterating over datasets in various manners */

/* Author: Chris Piker <chris-piker@uiowa.edu>
 * 
 * This file contains test and example code and is meant to explain an
 * interface.
 * 
 * As United States courts have ruled that interfaces cannot be copyrighted,
 * the code in this individual source file, TestArrays.c, is placed into the
 * public domain and may be displayed, incorporated or otherwise re-used without
 * restriction.  It is offered to the public without any without any warranty
 * including even the implied warranty of merchantability or fitness for a
 * particular purpose.
 */

#define _POSIX_C_SOURCE 200112L

#include <string.h>

#include <das2/core.h>

const char* g_sExpectFreq =
"1.0940e-01 1.2050e-01 1.3120e-01 1.4230e-01 1.5300e-01 1.7520e-01 1.8590e-01 1.9700e-01 "
"2.0760e-01 2.1880e-01 2.2990e-01 2.4050e-01 2.5170e-01 2.7340e-01 2.8460e-01 2.9520e-01 "
"3.0630e-01 3.1700e-01 3.2810e-01 3.3920e-01 3.4990e-01 3.6100e-01 3.7170e-01 3.8280e-01 "
"3.9390e-01 4.0460e-01 4.1570e-01 4.2640e-01 4.3750e-01 4.4860e-01 4.5930e-01 4.7040e-01 "
"4.8110e-01 4.9220e-01 5.0330e-01 5.1400e-01 5.2510e-01 5.3580e-01 5.4690e-01 5.5800e-01 "
"5.6870e-01 5.7980e-01 6.0160e-01 6.2340e-01 6.4520e-01 6.6740e-01 6.8920e-01 7.1090e-01 "
"7.3270e-01 7.5450e-01 7.7630e-01 8.0840e-01 8.2030e-01 8.4210e-01 8.6390e-01 8.8570e-01 "
"9.0790e-01 9.2970e-01 9.5150e-01 9.7330e-01 9.9510e-01 1.0173e+00 1.0391e+00 1.0827e+00 "
"1.1044e+00 1.1267e+00 1.1485e+00 1.1702e+00 1.2023e+00 1.2138e+00 1.2360e+00 1.2578e+00 "
"1.2796e+00 1.3232e+00 1.3454e+00 1.3672e+00 1.3890e+00 1.4108e+00 1.4326e+00 1.4548e+00 "
"1.4766e+00 1.4984e+00 1.5202e+00 1.5420e+00 1.5642e+00 1.5860e+00 1.6078e+00 1.6295e+00 "
"1.6513e+00 1.6735e+00 1.7171e+00 1.7607e+00 1.8047e+00 1.8483e+00 1.8923e+00 1.9359e+00 "
"1.9795e+00 2.0235e+00 2.0670e+00 2.1106e+00 2.1546e+00 2.1982e+00 2.2422e+00 2.2858e+00 "
"2.3294e+00 2.3734e+00 2.4170e+00 2.4610e+00 2.5046e+00 2.5481e+00 2.5921e+00 2.6357e+00 "
"2.6797e+00 2.7233e+00 2.7669e+00 2.8109e+00 2.8545e+00 2.8985e+00 2.9421e+00 2.9856e+00 "
"3.0296e+00 3.0732e+00 3.1172e+00 3.1608e+00 3.2044e+00 3.2484e+00 3.2920e+00 3.3360e+00 "
"3.3796e+00 3.4231e+00 3.4672e+00 3.5107e+00 3.5543e+00 3.5983e+00 3.6419e+00 3.6859e+00 "
"3.7295e+00 3.7731e+00 3.8171e+00 3.8607e+00 3.9047e+00 3.9918e+00 4.0794e+00 4.1670e+00 "
"4.2546e+00 4.3422e+00 4.4293e+00 4.5169e+00 4.6045e+00 4.6921e+00 4.7797e+00 4.8668e+00 "
"4.9544e+00 5.0420e+00 5.1296e+00 5.2168e+00 5.3043e+00 5.3919e+00 5.4795e+00 5.5013e+00 "
;

int main(int argc, char** argv)
{	
	/* Exit on errors, log info messages and above */
	das_init(argv[0], DASERR_DIS_EXIT, 0, DASLOG_INFO, NULL);

	/* High-rank uniq-index interation test */
	int nTest = 1;
	int nErr = DASERR_MAX + nTest;
	DasStream* pSd = stream_from_path("TestIter", "test/ex12_sounder_xyz.d3t");
	if(pSd == NULL)
		return das_error(nErr, "Test %d failed", nTest);

	DasDesc* pDesc = DasStream_getDesc(pSd, 1);
	if(pDesc->type != DATASET)
		return das_error(nErr, "Test %d failed, descriptor isn't a dataset", nTest);
	
	DasDs* pDs = (DasDs*)pDesc;
	DasDim* pFreq = DasDs_getDim(pDs, "frequency", DASDIM_COORD);
	DasVar* pCent = DasDim_getVar(pFreq, "center");
	
	/* Print all unique frequencies, no matter the DS shape */
	das_uniq_iter iterU;
	das_datum dm;
	char sTest[20*100] = {'\0'};
	char* pWrite = sTest;
	for(das_uniq_iter_init(&iterU, pDs, pCent); !iterU.done; das_uniq_iter_next(&iterU)){
		DasVar_get(pCent, iterU.index, &dm);
		das_datum_toStrValOnly(&dm, pWrite, 32, 4);
		pWrite += strlen(pWrite);
		*pWrite = ' '; ++pWrite;
	}

	size_t uLen = strlen(sTest);
	for(size_t u = 0; u < uLen; ++u){
		if(sTest[u] != g_sExpectFreq[u])
			das_error(nErr, "Test %d failed, output change at character %zu", nTest, u);
	}
	daslog_info_v("Test %d success. Frequency sets match:\n%s\n", nTest, sTest);
	del_DasStream(pSd);


	/* Ragged dataset iteration test */
	/* TODO: Enable ragged Iterator tests! */
	/* 
	++nTest;
	++nErr;
	pSd = stream_from_path("TestIter", "test/ex19_cassini_ragged_wfrm.d3t");
	if(pSd == NULL)
		return das_error(nErr, "Test %d failed", nTest);
	
	pDesc = DasStream_getDesc(pSd, 2);
	if(pDesc->type != DATASET)
		return das_error(nErr, "Test %d failed, descriptor isn't a dataset", nTest);

	pDs = (DasDs*)pDesc;
	
	/ * Read over all the points in a variable length dataset and print
	   the points per "packet" * /
	size_t aPtsPerRow[3] = {0};

	das_iter iterR;
	for(das_iter_init(&iterR, pDs); !iterR.done; das_iter_next(&iterR)){
		aPtsPerRow[ iterR.index[0] ] += 1;
	}
	
	for(int i = 0; i < 3; ++i)
		printf("Row%d: %zu amplitudes", i, aPtsPerRow[i]);

	del_DasStream(pSd);
	*/
	return 0;
}