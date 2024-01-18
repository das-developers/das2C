#define _POSIX_C_SOURCE 200112L

#include <stdio.h>
#include <das2/core.h>

/* Test ASPERA ELS at varying resolutions 
 *
 * Build from the project root directory using:
 *
 * gcc -Wall -fpic -std=c99 -ggdb -I. test/TestEls.c \ 
 *   ./build.Linux.x86_64/libdas2.3.a \
 *   -lfftw3 -lexpat -lssl -lcrypto -lz -lm -lpthread \
 *   -o TestEls
 */
 
 
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
	das_init(argv[0], DASERR_DIS_EXIT, 0, DASLOG_INFO, NULL);
	
	const char* sUrl = 
		"https://planet.physics.uiowa.edu/das/das2Server"
		"?server=dataset"
		"&dataset=Mars_Express/ASPERA/ELS"
		"&start_time=2014-10-19T17:50:00.000Z&end_time=2014-10-19T18:50:00.000Z"
		"&resolution=60" /* 3600 would mean 1 point for the whole hour */
	;                   /* 1 point per minute is better, but kindof low res */
	
	printf("INFO: Reading %s\n", sUrl);
	
	DasHttpResp res;
	if(!das_http_getBody(sUrl, NULL, NULL, &res, DASHTTP_TO_MIN)){
		printf("ERROR: Could not get body for URL, reason: %s\n", res.sError);
		return 107;
	}
	
	DasIO* pIn = NULL;
	if(DasHttpResp_useSsl(&res))
		pIn = new_DasIO_ssl("TestBuilder", res.pSsl, "r");
	else
		pIn = new_DasIO_socket("TestBuilder", res.nSockFd, "r");
	
	DasDsBldr* pBldr = new_DasDsBldr();
	DasIO_addProcessor(pIn, (StreamHandler*)pBldr);
	
	if(DasIO_readAll(pIn) != 0){
		printf("ERROR: Test 8 failed, couldn't process %s\n", sUrl);
		return 108;
	}
	
	size_t uDs = 0;
	DasDs** lDs = DasDsBldr_getDataSets(pBldr, &uDs);	
	
	printf("INFO: %zu Datasets retrieved from %s\n", uDs, sUrl);
	print_info(lDs, uDs);
	
	return 0;
}
