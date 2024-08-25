/** @file TestArrays.c Unit tests for dynamic array handling */

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

const char* g_sProg = "TestBuilder";

bool print_info(DasStream* pStream, int nTest)
{
	char sBuf[8192] = {'\0'};
	if(pStream == NULL) return false;
	
	printf("%s\n", DasStream_info(pStream, sBuf, 8191));
	
	int nPktId = 0;
	DasDesc* pDesc = NULL;
	while((pDesc = DasStream_nextDesc(pStream, &nPktId))!=NULL){

		/* Check that we are a DasDs */
		if(DasDesc_type(pDesc) != DATASET){
			printf("ERROR: Non dataset desciptor %p found after builder operation!\n",
				pDesc
			);
			return false;
		}

		/* Check that we belong to this stream */
		if(pDesc->parent != (DasDesc*)pStream){
			printf(
				"ERROR: Test %d failed, %p is a descriptor for stream %p, "
				"not this one (%p)\n", 
				nTest, pDesc, pDesc->parent, pStream
			);
			return false;
		}

		DasDs_toStr((DasDs*)pDesc, sBuf, 8191);
		printf("%s\n", sBuf);
	}
	return true;
}

bool test_file(const char* sFile, int nTest){
	DasStream* pStream = stream_from_path(g_sProg, sFile);
	if(pStream == NULL){
		printf("ERROR: Test %d failed", nTest);
		return false;
	}
	if(! print_info(pStream, nTest) ) return false;
	del_StreamDesc(pStream);
	return true;
}


int main(int argc, char** argv)
{
	/* Exit on errors, log info messages and above */
	das_init(argv[0], DASERR_DIS_EXIT, 0, DASLOG_INFO, NULL);
	
	int nTest = 1;
	int nErr = DASERR_MAX + nTest;
	const char* sFile = "test/galileo_pws_sample.d2t";
	DasStream* pStream = stream_from_path(g_sProg, sFile);
	if(pStream == NULL)
		return das_error(nErr, "Test %d failed", nTest);

	if(! print_info(pStream, 1) ) return nErr;

	size_t uDs = DasStream_getNPktDesc(pStream);
	if((pStream == NULL)||(uDs != 1)){
		printf("ERROR: Test 1 failed, expected 1 dataset from %s, found %zu\n",
		       sFile, uDs);
		return nErr;
	}
	
	if(!test_file("test/x_multi_y.d2s",                2)) return 13;
	if(!test_file("test/cassini_rpws_sample.d2t",      3)) return 13;
	if(!test_file("test/juno_waves_sample.d2t",        4)) return 13;
	if(!test_file("test/mex_marsis_bmag.d2t",          5)) return 13;
	if(!test_file("test/cassini_rpws_wfrm_sample.d2s", 6)) return 13;

	/* New tests for das3 streams */
	if(!test_file("test/ex12_sounder_xyz.d3t",         7)) return 13;
	if(!test_file("test/ex15_vector_frame.d3b",        8)) return 13;
	if(!test_file("test/ex17_vector_noframe.d3b",      9)) return 13;
	
	printf("INFO: All local builder operation tests passed\n\n");
	
	/* Now for the big one, try to get a dataset from a remote server, this may
	 * fail, it's up to the test runner to decide if the failure is okay */
	const char* sInitialUrl = "https://jupiter.physics.uiowa.edu/das/server"
	       "?server=dataset&dataset=Galileo/PWS/Survey_Electric"
	       "&start_time=2001-001&end_time=2001-002";
	
	printf("INFO: Contacting remote HTTP URL %s\n\n", sInitialUrl);
	
	DasHttpResp res;
	if(!das_http_getBody(sInitialUrl, NULL, NULL, &res, DASHTTP_TO_MIN)){
		printf("ERROR: Could not get body for URL, reason: %s\n", res.sError);
		return 107;
	}
	char sUrl[512] = {'\0'};
	das_url_toStr(&(res.url), sUrl, 511);
	if(strcmp(sUrl, sInitialUrl) != 0)
		printf("INFO: Redirected to %s\n\n", sUrl);
	
	DasIO* pIn = NULL;
	if(DasHttpResp_useSsl(&res))
		pIn = new_DasIO_ssl("TestBuilder", res.pSsl, "r");
	else
		pIn = new_DasIO_socket("TestBuilder", res.nSockFd, "r");
	
	DasDsBldr* pBldr = new_DasDsBldr();
	DasIO_addProcessor(pIn, (StreamHandler*)pBldr);
	
	if(DasIO_readAll(pIn) != 0){
		printf("ERROR: Test 10 failed, couldn't process %s\n", sUrl);
		return 9;
	}
	
	pStream = DasDsBldr_getStream(pBldr);	
	uDs = DasStream_getNPktDesc(pStream);

	printf("INFO: %zu Datasets retrieved from %s\n", uDs, sUrl);
	DasDsBldr_release(pBldr); /* Avoid double delete with print_info function */
	if(! print_info(pStream, 10)) return 10;
	del_DasIO(pIn);
	del_DasDsBldr(pBldr);
	
	sInitialUrl = "https://jupiter.physics.uiowa.edu/das/server"
	"?server=dataset&dataset=Earth/LWA-1/Ephemeris/Jupiter"
	"&end_time=2015-02-21T03:00&start_time=2015-02-21T02:00&interval=60";
	
	printf("INFO: Contacting remote HTTPS URL %s\n\n", sInitialUrl);
	
	DasHttpResp_clear(&res);
	
	if(!das_http_getBody(sInitialUrl, NULL, NULL, &res, DASHTTP_TO_MIN)){
		printf("ERROR: Could not get body for URL, reason: %s\n", res.sError);
		return 13;
	}
	das_url_toStr(&(res.url), sUrl, 511);
	if(strcmp(sUrl, sInitialUrl) != 0)
		printf("INFO: Redirected to %s\n\n", sUrl);
	
	pIn = NULL;
	if(DasHttpResp_useSsl(&res))
		pIn = new_DasIO_ssl("TestBuilder", res.pSsl, "r");
	else
		pIn = new_DasIO_socket("TestBuilder", res.nSockFd, "r");
	
	pBldr = new_DasDsBldr();
	DasIO_addProcessor(pIn, (StreamHandler*)pBldr);
	
	if(DasIO_readAll(pIn) != 0){
		printf("ERROR: Test 11 failed, couldn't process %s\n", sUrl);
		return 13;
	}
	
	pStream = DasDsBldr_getStream(pBldr);
	uDs = DasStream_getNPktDesc(pStream);
	
	printf("INFO: %zu Datasets retrieved from %s\n", uDs, sUrl);
	DasDsBldr_release(pBldr); /* Avoid double delete with print_info function */
	if(! print_info(pStream, 11)) return 13;
	del_DasIO(pIn);
	del_DasDsBldr(pBldr);
	
	return 0;
}
