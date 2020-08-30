#include <das2/core.h>

#include <stdio.h>
#include <string.h>

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

int main(int argc, char** argv){

	char sUrl[512] = {'\0'};
	size_t uCds = 0;
	DasDs** lDs = NULL;
	
	/* Exit on errors, log info messages and above */
	das_init("webasm", DASERR_DIS_EXIT, 0, DASLOG_INFO, NULL);
	
	const char* sInitialUrl = "http://planet.physics.uiowa.edu/das/das2Server"
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
	
	return 0;
}
