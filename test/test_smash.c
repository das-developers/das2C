#define _POSIX_C_SOURCE 200112L

#include <das2/core.h>


/* Test program for DasDs_toStr which was smashing the stack at one point
   (2025-02-18) when datasets with many variables were downloaded.
	
	To Build:
	gcc -Wall -ggdb -I . test/test_smash.c -L ./build.ubuntu24 -ldas3.0 -lexpat -lssl -lcrypto -lfftw3 -lz -lm -o test/test_smash

	To Run:
	./test/test_smash
*/	

int main(int argc, char** argv)
{	
	/* Exit on errors, log info messages and above */
	das_init(argv[0], DASERR_DIS_EXIT, 0, DASLOG_INFO, NULL);
	
	const char* sUrl = 
		"http://localhost/das/server?server=dataset"
		"&dataset=preflight/l1/aci/emu-3/hsk&start_time=2023-08-08T15:04"
		"&end_time=2023-08-08T15:15&resolution=0.660000";
			
	const char* sUserAgent = NULL;
	float rConSec = DASHTTP_TO_MIN * DASHTTP_TO_MULTI;

	DasHttpResp res;
	bool bOkay = das_http_getBody(sUrl, sUserAgent, NULL, &res, rConSec);
	if(!bOkay)
		return 7;
	
	DasIO* pIn = new_DasIO_socket("das2py", res.nSockFd, "r");
	
	DasIO_model(pIn, -1);  /* Allow all stream versions */

	DasDsBldr* pBldr = new_DasDsBldr();
	DasIO_addProcessor(pIn, (StreamHandler*)pBldr);

	int nRet = DasIO_readAll(pIn);
	if(nRet != DAS_OKAY)
		return 14;
	
	DasStream* pStream = DasDsBldr_getStream(pBldr);
	
	char sInfo[4096] = {'\0'};
	DasDesc* pDesc = NULL;
	DasDs* pDs = NULL;
	int nPktId = 0;
	while((pDesc = DasStream_nextDesc(pStream, &nPktId)) != NULL){
		
		if(DasDesc_type(pDesc) != DATASET)
			continue;
		
		pDs = (DasDs*)pDesc;
		DasDs_toStr(pDs, sInfo, 4095);
	}
	
	return 0;
}
