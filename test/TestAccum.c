/** @file TestAccum.c Unit tests accumulating stream data in ram */

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
#include <string.h>

#include <das3/core.h>
#include <das3/http.h>

const char* g_sProg = "TestAccum";

bool print_info(DasStream* pStream, int nTest)
{
	char sBuf[8192] = {'\0'};
	if(pStream == NULL) return false;
	
	DasStream_info(pStream, sBuf, 8192);
	printf("%s\n", sBuf);
	
	DasDesc* pDesc = NULL;
	while((pDesc = DasStream_nextDesc(pStream, pDesc, DASDESC_UNK))!=NULL){

		/* Check that we are a DasDs */
		if(DasDesc_type(pDesc) != DASDESC_DS){
			printf("ERROR: Non dataset desciptor %p found in a V3 model stream!\n",
				pDesc
			);
			return false;
		}

		/* Check that we belong to this stream */
		if(DasDesc_parent(pDesc) != (DasDesc*)pStream){
			printf(
				"ERROR: Test %d failed, %p is a descriptor for stream %p, "
				"not this one (%p)\n", 
				nTest, pDesc, DasDesc_parent(pDesc), pStream
			);
			return false;
		}

		DasDs_info((DasDs*)pDesc, sBuf, 8191);
		printf("%s\n", sBuf);
	}
	return true;
}

bool test_file(const char* sFile, int nTest){
	DasStream* pStream = new_DasStream_path(g_sProg, sFile);
	if(pStream == NULL){
		printf("ERROR: Test %d failed", nTest);
		return false;
	}
	if(! print_info(pStream, nTest) ) return false;
	del_DasDesc((DasDesc*)pStream);
	return true;
}

/* A legacy das2 stream that sends new packet headers under an ID already in
   use.  Read as datasets, nothing may be lost: every header is its own
   dataset, all of them stay in the stream with their records, only the last
   one under an ID is active, and the made up names don't repeat. */
bool test_reused_id(const char* sFile, size_t uExpectDs, ptrdiff_t nExpectRec, int nTest)
{
	DasStream* pStream = new_DasStream_path(g_sProg, sFile);
	if(pStream == NULL){
		printf("ERROR: Test %d failed, couldn't read %s\n", nTest, sFile);
		return false;
	}

	size_t uDs = DasStream_numDesc(pStream, DASDESC_DS);
	if(uDs != uExpectDs){
		printf("ERROR: Test %d failed, expected %zu datasets from %s, found %zu\n",
			nTest, uExpectDs, sFile, uDs);
		return false;
	}

	ptrdiff_t nRec = 0;
	size_t uActive = 0;
	ptrdiff_t aShape[VARIDX_MAX];
	DasDesc* pDesc = NULL;
	while((pDesc = DasStream_nextDesc(pStream, pDesc, DASDESC_DS)) != NULL){
		DasDs* pDs = (DasDs*)pDesc;
		DasDs_shape(pDs, aShape);
		if(aShape[0] < 1){
			printf("ERROR: Test %d failed, dataset %s kept no records\n", nTest, DasDs_name(pDs));
			return false;
		}
		nRec += aShape[0];

		int nPktId = DasStream_getPktId(pStream, pDesc);
		if(DasStream_isDescActive(pStream, pDesc)){
			uActive += 1;
			if(DasStream_getDesc(pStream, nPktId) != pDesc){
				printf("ERROR: Test %d failed, active dataset is not the one for ID %d\n", nTest, nPktId);
				return false;
			}
		}

		/* (group, name) should be unique, and here the library made the names */
		DasDesc* pOther = pDesc;
		while((pOther = DasStream_nextDesc(pStream, pOther, DASDESC_DS)) != NULL){
			if((strcmp(DasDs_name(pDs), DasDs_name((DasDs*)pOther)) == 0)&&
			   (strcmp(DasDs_group(pDs), DasDs_group((DasDs*)pOther)) == 0)){
				printf("ERROR: Test %d failed, two datasets are named (%s, %s)\n",
					nTest, DasDs_group(pDs), DasDs_name(pDs));
				return false;
			}
		}
	}

	if(nRec != nExpectRec){
		printf("ERROR: Test %d failed, expected %td records from %s, found %td\n",
			nTest, nExpectRec, sFile, nRec);
		return false;
	}
	/* The file uses a single packet ID throughout */
	if(uActive != 1){
		printf("ERROR: Test %d failed, %zu active datasets for one packet ID\n", nTest, uActive);
		return false;
	}

	printf("INFO: %zu datasets and %td records kept from %s\n", uDs, nRec, sFile);
	del_DasDesc((DasDesc*)pStream);
	return true;
}

bool test_plain_url(const char* sInitialUrl, int nTest)
{
	printf("INFO: Contacting remote HTTP URL %s\n\n", sInitialUrl);
	
	DasHttpResp* pRes = new_DasHttpResp_get(sInitialUrl, NULL, NULL, DASHTTP_TO_MIN);
	if(!DasHttpResp_ok(pRes)){
		printf("ERROR: Could not get body for URL, reason: %s\n", DasHttpResp_error(pRes));
		return 107;
	}
	char sUrl[512] = {'\0'};
	strncpy(sUrl, DasHttpResp_url(pRes), 511);
	if(strcmp(sUrl, sInitialUrl) != 0)
		printf("INFO: Redirected to %s\n\n", sUrl);
	
	DasIO* pIn = new_DasIO("TestAccum", DasHttpResp_takeBody(pRes), NULL);
	del_DasHttpResp(pRes);
	
	DasIO_setModel(pIn, STREAM_MODEL_V3);
	DasIO_setAccum(pIn, true);
	
	if(DasIO_readAll(pIn) != 0){
		printf("ERROR: Test 10 failed, couldn't process %s\n", sUrl);
		return 9;
	}
	
	DasStream* pStream = DasIO_takeStream(pIn);
	del_DasIO(pIn);
	size_t uDs = DasStream_numDesc(pStream, DASDESC_UNK);

	printf("INFO: %zu Datasets retrieved from %s\n", uDs, sUrl);

	if(! print_info(pStream, 10)) return 10;
	del_DasDesc((DasDesc*)pStream);

	return true;
}

bool test_secure_url(const char* sInitialUrl, int nTest)
{
	printf("INFO: Contacting remote HTTPS URL %s\n\n", sInitialUrl);
	
	DasHttpResp* pRes = new_DasHttpResp_get(sInitialUrl, NULL, NULL, DASHTTP_TO_MIN);
	if(!DasHttpResp_ok(pRes)){
		printf("ERROR: Could not get body for URL, reason: %s\n", DasHttpResp_error(pRes));
		return 13;
	}

	char sUrl[512] = {'\0'};
	strncpy(sUrl, DasHttpResp_url(pRes), 511);
	if(strcmp(sUrl, sInitialUrl) != 0)
		printf("INFO: Redirected to %s\n\n", sUrl);
	
	DasIO* pIn = new_DasIO("TestAccum", DasHttpResp_takeBody(pRes), NULL);
	del_DasHttpResp(pRes);
	
	DasIO_setModel(pIn, STREAM_MODEL_V3);
	DasIO_setAccum(pIn, true);
	
	if(DasIO_readAll(pIn) != 0){
		printf("ERROR: Test 11 failed, couldn't process %s\n", sUrl);
		return 13;
	}
	
	DasStream* pStream = DasIO_takeStream(pIn);
	del_DasIO(pIn);
	size_t uDs = DasStream_numDesc(pStream, DASDESC_UNK);
	
	printf("INFO: %zu Datasets retrieved from %s\n", uDs, sUrl);
	if(! print_info(pStream, 11)) return 13;
	del_DasDesc((DasDesc*)pStream);

	return true;
}

int main(int argc, char** argv)
{
	/* Exit on errors, log info messages and above */
	das_init(argv[0], DASERR_DIS_EXIT, DASLOG_INFO, NULL);
	
	int nTest = 1;
	int nErr = DASERR_MAX + nTest;
	const char* sFile = "examples/ex03_galileo_pws_lrs.d2t";
	DasStream* pStream = new_DasStream_path(g_sProg, sFile);
	if(pStream == NULL)
		return das_error(nErr, "Test %d failed", nTest);

	if(! print_info(pStream, 1) ) return nErr;

	size_t uDs = DasStream_numDesc(pStream, DASDESC_UNK);
	if((pStream == NULL)||(uDs != 1)){
		printf("ERROR: Test 1 failed, expected 1 dataset from %s, found %zu\n",
		       sFile, uDs);
		return nErr;
	}
	del_DasDesc((DasDesc*)pStream);
	
	if(!test_file("examples/ex01_polar_mfe.d2s",           2)) return 13;
	if(!test_file("examples/ex08_cassini_rpws_survey.d2t", 3)) return 13;
	if(!test_file("examples/ex09_juno_waves_survey.d2t",   4)) return 13;
	if(!test_file("examples/ex13_marsis_bmag.d2t",         5)) return 13;
	if(!test_file("examples/ex07_cassini_rpws_wbr.d2s",    6)) return 13;

	/* New tests for das3 streams */
	if(!test_file("examples/ex12_sounder_xyz.d3t",         7)) return 13;
	if(!test_file("examples/ex17_vector_noframe.d3b",      8)) return 13;
	if(!test_file("examples/ex15_vector_frame.d3t",        9)) return 13;

	/* 8 packet headers, all ID 01, and 10184 data packets (das2_ascii counts) */
	if(!test_reused_id("examples/ex06_cassini_rpws_redef.d2s", 8, 10184, 12)) return 13;

	
	printf("INFO: All local whole-stream read tests passed\n\n");
	
	/* Remote-server queries are OPT-IN via --uiowa: they hit live das2 servers, so
	   they don't belong in a hermetic build regression (bare `make test`). */
	if((argc > 1) &&(strcmp(argv[1], "--uiowa") == 0)){

		const char* sInitialUrl = "https://jupiter.physics.uiowa.edu/das/server"
		       "?server=dataset&dataset=Galileo/PWS/Survey_Electric"
		       "&start_time=2001-001&end_time=2001-002";
		if(!test_plain_url(sInitialUrl, 10)) return 13;

		sInitialUrl = "https://jupiter.physics.uiowa.edu/das/server"
		"?server=dataset&dataset=Earth/LWA-1/Ephemeris/Jupiter"
		"&end_time=2015-02-21T03:00&start_time=2015-02-21T02:00&interval=60";
		if(!test_secure_url(sInitialUrl, 11)) return 13;

		sInitialUrl = "https://tracers-dev.physics.uiowa.edu/stream"
		"?server=dataset&dataset=PreFlight/L1/ACI/FM-1/ipd_anode"
		"&start_time=2024-06-29T16:52&end_time=2024-06-29T17-08";

		if(!test_secure_url(sInitialUrl, 11)) return 13;
	}
	
	return 0;
}
