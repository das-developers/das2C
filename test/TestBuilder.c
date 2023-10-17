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


DasDs** read_stream(const char* sFile, int nTest, size_t* pLen){

	printf("INFO: Reading %s\n", sFile);
	FILE* pFile = fopen(sFile, "rb");
	if(!pFile){
		printf("ERROR: Test %d failed, couldn't open %s\n", nTest, sFile);
		return NULL;
	}
	DasIO* pIn = new_DasIO_cfile("TestBuilder", pFile, "r");
	DasDsBldr* pBldr = new_DasDsBldr();
	DasIO_addProcessor(pIn, (StreamHandler*)pBldr);
	
	if(DasIO_readAll(pIn) != 0){
		printf("ERROR: Test %d failed, couldn't process %s\n", nTest, sFile);
		return NULL;
	}
	
	DasDs** lDs = DasDsBldr_getDataSets(pBldr, pLen);	
	
	printf("INFO: %zu Datasets retrieved from %s\n", *pLen, sFile);
	
	return lDs;
}

void print_info(DasDs** lDs, size_t uDs){

	if(lDs == NULL) return;
	if(uDs < 1) return;
	
	DasDs* pDs;
	char sBuf[2048] = {'\0'};
	size_t u;
	for(u = 0; u<uDs; ++u){
		pDs = lDs[u];
		DasDs_toStr(pDs, sBuf, 2048);
		printf("%s\n", sBuf);
	}	
}


int main(int argc, char** argv)
{
	/* Exit on errors, log info messages and above */
	das_init(argv[0], DASERR_DIS_EXIT, 0, DASLOG_INFO, NULL);
	
	char sUrl[512] = {'\0'};
	size_t uCds = 0;
	const char* sFile = "test/galileo_pws_sample.d2t";
	DasDs** lDs = read_stream(sFile, 1, &uCds);
	print_info(lDs, uCds);
	
	if((lDs == NULL)||(uCds != 1)){
		printf("ERROR: Test 1 failed, expected 1 dataset from %s, found %zu\n",
		       sFile, uCds);
		return 101;
	}
	
	sFile = "test/x_multi_y.d2s";
	lDs = read_stream(sFile, 2, &uCds);
	if(lDs == NULL){
		printf("ERROR: Test 2 failed");
		return 102;
	}
	print_info(lDs, uCds);
	
	sFile = "test/cassini_rpws_sample.d2t";
	lDs = read_stream(sFile, 2, &uCds);
	if(lDs == NULL){
		printf("ERROR: Test 3 failed");
		return 103;
	}
	print_info(lDs, uCds);
	
	sFile = "test/juno_waves_sample.d2t";
	lDs = read_stream(sFile, 2, &uCds);
	if(lDs == NULL){
		printf("ERROR: Test 4 failed");
		return 104;
	}
	print_info(lDs, uCds);
	
	sFile = "test/mex_marsis_bmag.d2t";
	lDs = read_stream(sFile, 2, &uCds);
	if(lDs == NULL){
		printf("ERROR: Test 5 failed");
		return 105;
	}
	print_info(lDs, uCds);
	
	sFile = "test/cassini_rpws_wfrm_sample.d2s";
	lDs = read_stream(sFile, 2, &uCds);
	if(lDs == NULL){
		printf("ERROR: Test 6 failed");
		return 106;
	}
	print_info(lDs, uCds);
	
	
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
		printf("ERROR: Test 7 failed, couldn't process %s\n", sUrl);
		return 107;
	}
	
	lDs = DasDsBldr_getDataSets(pBldr, &uCds);	
	
	printf("INFO: %zu Datasets retrieved from %s\n", uCds, sUrl);
	print_info(lDs, uCds);
	del_DasIO(pIn);
	del_DasDsBldr(pBldr);
	
	
	sInitialUrl = "https://jupiter.physics.uiowa.edu/das/server"
	"?server=dataset&dataset=Earth/LWA-1/Ephemeris/Jupiter"
	"&end_time=2015-02-21T03:00&start_time=2015-02-21T02:00&interval=60";
	
	printf("INFO: Contacting remote HTTPS URL %s\n\n", sInitialUrl);
	
	DasHttpResp_clear(&res);
	
	if(!das_http_getBody(sInitialUrl, NULL, NULL, &res, DASHTTP_TO_MIN)){
		printf("ERROR: Could not get body for URL, reason: %s\n", res.sError);
		return 108;
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
		printf("ERROR: Test 8 failed, couldn't process %s\n", sUrl);
		return 108;
	}
	
	lDs = DasDsBldr_getDataSets(pBldr, &uCds);	
	
	printf("INFO: %zu Datasets retrieved from %s\n", uCds, sUrl);
	print_info(lDs, uCds);
	
	return 0;
}
