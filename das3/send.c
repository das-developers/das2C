#define _POSIX_C_SOURCE 200112L

#include <string.h>
#include <stdio.h>
#include <stdarg.h>

#include <stdint.h>   /* C99 */
#include <stdbool.h>  /* C99 */

#ifdef _WIN32
#include <io.h>     /* _setmode, _fileno */
#include <fcntl.h>  /* _O_BINARY */
#endif

#include "util.h"    /* das_xml_escape */
#include "oob.h"     /* das_except_toStr */
#include "send.h"


/* One emitter per out-of-band kind, per stream version.

   Das2.2 puts the text in attributes while das3.0 puts them in
   the  element body. das1 streams have no out-of-band messages */

/* Packet lengths count LF line ends, so stdout has to be binary, see
   new_DasIO_cfile() */
static void _stdout_binary(void)
{
#ifdef _WIN32
	static bool bBinary = false;
	if(!bBinary){
		fflush(stdout);
		_setmode(_fileno(stdout), _O_BINARY);
		bBinary = true;
	}
#endif
}

static void _send_except(int nDasVer, das_except_t nType, const char* sMsg)
{
	if((nDasVer < 2)||(nDasVer > 3)) return;
	_stdout_binary();

	char sMsgEsc[1024] = {'\0'};
	char sOut[1152] = {'\0'};
	das_xml_escape(sMsgEsc, sMsg, sizeof(sMsgEsc));

	if(nDasVer == 2){
		snprintf(sOut, 1151, "<exception type=\"%s\"\n"
		                     "           message=\"%s\" />\n",
		         das_except_toStr(nType, 2), sMsgEsc);
		printf("[XX]%06zu%s", strlen(sOut), sOut);
	}
	else{
		snprintf(sOut, 1151, "<exception type=\"%s\">%s</exception>\n",
		         das_except_toStr(nType, 3), sMsgEsc);
		printf("|Ex||%zu|%s", strlen(sOut), sOut);
	}
}

static void _send_comment(
	int nDasVer, const char* sType, const char* sSrc, const char* sVal
){
	if((nDasVer < 2)||(nDasVer > 3)) return;
	_stdout_binary();

	char sSrcEsc[1024] = {'\0'};
	char sValEsc[1024] = {'\0'};
	char sOut[2176] = {'\0'};
	das_xml_escape(sSrcEsc, sSrc, sizeof(sSrcEsc));
	das_xml_escape(sValEsc, sVal, sizeof(sValEsc));

	if(nDasVer == 2){
		snprintf(sOut, 2175, "<comment type=\"%s\"\n"
		                     "         source=\"%s\"\n"
		                     "         value=\"%s\" />\n", sType, sSrcEsc, sValEsc);
		printf("[XX]%06zu%s", strlen(sOut), sOut);
	}
	else{
		snprintf(sOut, 2175, "<comment type=\"%s\" source=\"%s\">%s</comment>\n",
		         sType, sSrcEsc, sValEsc);
		printf("|Cx||%zu|%s", strlen(sOut), sOut);
	}
}

void das_send_stub(int nDasVer)
{
	const char* sHdr = NULL;
	_stdout_binary();
	switch(nDasVer){
	case 2:
		sHdr = "<stream version=\"2.2\"></stream>\n";
		printf("[00]%06zu%s", strlen(sHdr), sHdr);
		break;
	case 3:
		sHdr = "<stream version=\"3.0\" type=\"das-basic-stream\" />\n";
		printf("|Sx||%zu|%s", strlen(sHdr), sHdr);
		break;
	default:
		break;
	}
}

int das_send_nodata(int nDasVer, const char* sFmt, ...)
{
	va_list argp;
	char sMsg[1024] = {'\0'};

	va_start(argp, sFmt);
	vsnprintf(sMsg, 1023, sFmt, argp);
	va_end(argp);

	fprintf(stderr, "INFO: No Data in interval %s\n", sMsg);
	_send_except(nDasVer, DAS_EX_NO_DATA, sMsg);
	return 0;
}

int das_send_queryerr(int nDasVer, const char* sFmt, ...)
{
	va_list argp;
	va_start(argp, sFmt);
	int nRet = das_vsend_queryerr(nDasVer, sFmt, argp);
	va_end(argp);
	return nRet;
}

int das_vsend_queryerr(int nDasVer, const char* sFmt, va_list argp)
{
	char sMsg[1024] = {'\0'};

	va_list _argp;
	va_copy(_argp, argp);
	vsnprintf(sMsg, 1023, sFmt, _argp);
	va_end(_argp);

	fprintf(stderr, "ERROR: Query Error, %s\n", sMsg);
	_send_except(nDasVer, DAS_EX_QUERY_ERR, sMsg);
	return 47;
}

int das_send_srverr(int nDasVer, const char* sFmt, ...)
{
	va_list argp;
	char sMsg[1024] = {'\0'};

	va_start(argp, sFmt);
	vsnprintf(sMsg, 1023, sFmt, argp);
	va_end(argp);

	fprintf(stderr, "ERROR: %s\n", sMsg);
	_send_except(nDasVer, DAS_EX_SERVER_ERR, sMsg);
	return 48;
}

void das_send_msg(int nDasVer, const char* sSource, const char* sFmt, ...)
{
	va_list argp;
	char sMsg[1024] = {'\0'};

	va_start(argp, sFmt);
	vsnprintf(sMsg, 1023, sFmt, argp);
	va_end(argp);

	fprintf(stderr, "INFO: (%s) %s\n", sSource, sMsg);
	_send_comment(nDasVer, "log:info", sSource, sMsg);
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

	_send_comment(nDasVer, "taskSize", sSrc, "100");
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

	char sProg[16] = {'\0'};
	snprintf(sProg, 15, "%d", nProg);
	_send_comment(nDasVer, "taskProgress", sSrc, sProg);

	g_nLastProg = nProg;
}	

