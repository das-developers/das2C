#define _POSIX_C_SOURCE 200112L

#include <string.h>
#include <stdio.h>
#include <stdarg.h>

#include <stdint.h>   /* C99 */
#include <stdbool.h>  /* C99 */

#include "send.h"


static const char* _g_sEscChar = "\"'<>&";
static const int _g_nEscChar = 5;
static const char* _g_sReplace[5] = {
	"&quot;", "&apos;", "&lt;", "&gt;","&amp;"
};

void _das_escape_xml(char* sDest, size_t uOutLen, const char* sSrc)
{
	size_t uIn = 0;
	size_t uOut = 0;
	size_t uTok = 0;
	bool bEsc = false;
	size_t uTokLen = 0;
	
	memset(sDest, 0, sizeof(char)*uOutLen);
	
	/* Leave room for the trailing NULL */
	while((sSrc[uIn] != '\0')&&(uIn < uOutLen-1)){
		
		bEsc = false;
		
		/* Loop over all replacement chars */
		for(uTok = 0; uTok<_g_nEscChar; ++uTok){
			
			if(sSrc[uIn] == _g_sEscChar[uTok]){
				
				uTokLen = strlen(_g_sReplace[uTok]);
				
				if(uOut + uTokLen < uOutLen - 1)
					strcpy(sDest+uOut, _g_sReplace[uTok]);
				
				uOut += uTokLen;
				bEsc = true;
				break;
			}	
		}
		
		if(!bEsc){ *(sDest+uOut) = sSrc[uIn]; ++uOut; }
		
		++uIn;
	}
}

void das_send_stub(int nDasVer)
{
	if(nDasVer < 2) return;
	const char* shdr = "<stream version=\"2.2\"></stream>\n";
	printf("[00]%06zu%s", strlen(shdr), shdr);
}

int das_send_nodata(int nDasVer, const char* sFmt, ...)
{
	va_list argp;
	char sMsg[1024] = {'\0'};
	char sMsgEsc[1024] = {'\0'};
	char sOut[1152] = {'\0'};
	
	va_start(argp, sFmt);
	vsnprintf(sMsg, 1023, sFmt, argp);
	va_end(argp);
		
	fprintf(stderr, "INFO: No Data in interval %s\n", sMsg);
	
	if(nDasVer == 2){
		_das_escape_xml(sMsgEsc, 1023, sMsg);
		snprintf(sOut, 1151, "<exception type=\"NoDataInInterval\"\n"
		                     "           message=\"%s\" />\n", sMsgEsc);
	
		printf("[xx]%06zu%s", strlen(sOut), sOut);
	}
	return 0;
}

int das_send_queryerr(int nDasVer, const char* sFmt, ...)
{
	va_list argp;
	char sMsg[1024] = {'\0'};
	char sMsgEsc[1024] = {'\0'};
	char sOut[1152] = {'\0'};
	
	va_start(argp, sFmt);
	vsnprintf(sMsg, 1023, sFmt, argp);
	va_end(argp);
	
	fprintf(stderr, "ERROR: Query Error, %s\n", sMsg);
	
	if(nDasVer == 2){
		_das_escape_xml(sMsgEsc, 1023, sMsg);
		snprintf(sOut, 1151, "<exception type=\"IllegalArgument\"\n"
		                     "           message=\"%s\" />\n", sMsgEsc);
		
		printf("[xx]%06zu%s", strlen(sOut), sOut);
	}
	return 47;
}

int das_vsend_queryerr(int nDasVer, const char* sFmt, va_list argp)
{
	char sMsg[1024] = {'\0'};
	char sMsgEsc[1024] = {'\0'};
	char sOut[1152] = {'\0'};
	
	va_list _argp;
	
	va_copy(argp, _argp);
	vsnprintf(sMsg, 1023, sFmt, _argp);
	va_end(_argp);
	
	fprintf(stderr, "ERROR: Query Error, %s\n", sMsg);
	
	if(nDasVer == 2){
		_das_escape_xml(sMsgEsc, 1023, sMsg);
		snprintf(sOut, 1151, "<exception type=\"IllegalArgument\"\n"
		                     "           message=\"%s\" />\n", sMsgEsc);
		
		printf("[xx]%06zu%s", strlen(sOut), sOut);
	}
	return 47;
}

int das_send_srverr(int nDasVer, const char* sFmt, ...)
{
	va_list argp;
	char sMsg[1024] = {'\0'};
	char sMsgEsc[1024] = {'\0'};
	char sOut[1152] = {'\0'};
	
	va_start(argp, sFmt);
	vsnprintf(sMsg, 1023, sFmt, argp);
	va_end(argp);

	fprintf(stderr, "ERROR: %s\n", sMsg);
	if(nDasVer == 2){
		_das_escape_xml(sMsgEsc, 1023, sMsg);
		snprintf(sOut, 1151, "<exception type=\"ServerError\"\n"
		                     "           message=\"%s\" />\n", sMsgEsc);
		
		printf("[xx]%06zu%s", strlen(sOut), sOut);
	}
	return 48;
}

void das_send_msg(int nDasVer, const char* sSource, const char* sFmt, ...)
{
	va_list argp;
	char sMsg[1024] = {'\0'};
	char sMsgEsc[1024] = {'\0'};
	char sSrcEsc[1024] = {'\0'};
	char sOut[1152] = {'\0'};
	
	va_start(argp, sFmt);
	vsnprintf(sMsg, 1023, sFmt, argp);
	va_end(argp);

	fprintf(stderr, "INFO: (%s) %s\n", sSource, sMsg);
	
	if(nDasVer == 2){
		_das_escape_xml(sMsgEsc, 1023, sMsg);
		_das_escape_xml(sSrcEsc, 1023, sSource);
		snprintf(sOut, 1151, "<comment type=\"log:info\"\n"
		                     "         source=\"%s\"\n"
		                     "         value=\"%s\" />\n", sSrcEsc, sMsgEsc);
		
		printf("[xx]%06zu%s", strlen(sOut), sOut);
	}
}

/* This is not multi-thread safe, but neither is intermixing writing two
   das2 streams to the same output channel so this is probably not an 
	issue 
*/
static float g_fProgBeg = 0;
static float g_fProgEnd = 1.0;
static int g_nLastProg = 0;

void das_send_progbeg(int nDasVer, const char* sSrc, double fBeg, double fEnd)
{
	if(nDasVer < 2) return;
		
	g_fProgBeg = fBeg;
	g_fProgEnd = fEnd;
	g_nLastProg = 0;
		
	char sMsg[1024] = {'\0'};
	snprintf(sMsg, 1023, "<comment type=\"taskSize\" value=\"100\" source=\"%s\" />\n",
	         sSrc);
	printf("[xx]%06zu%s", strlen(sMsg), sMsg);
}

void das_send_progup(
	int nDasVer, const char* sSrc, double fCurrent
){
	if(nDasVer < 2) return;
	
	int nProg = 0;
	double fRange = g_fProgEnd - g_fProgBeg;
	if(fRange > 0.0){
		nProg = (int)( ((fCurrent - g_fProgBeg)/ fRange) * 100.0);
	}
	if(nProg <= g_nLastProg) return;
	
	char sMsg[1024] = {'\0'};
	snprintf(sMsg, 1023, "<comment type=\"taskProgress\" value=\"%d\" source=\"%s\" />\n",
	         nProg, sSrc);
	printf("[xx]%06zu%s", strlen(sMsg), sMsg);
	
	g_nLastProg = nProg;
}	

/* ************************************************************************* */
/* Das1 helper for little endian machines */

float _das_swap_float(float rIn){
	const uint8_t* pIn  = (const uint8_t*)(&rIn);
	pIn += 3;
	
	float rOut;
	uint8_t* pOut = (uint8_t*)(&rOut);
	
	for(int i = 0; i < 4; i++){
		*pOut = *pIn; 
		pIn--; 
		pOut++;
	}
	return rOut;
}
