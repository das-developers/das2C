#define _POSIX_C_SOURCE 200112L

#include <string.h>
#include <stdio.h>

#include <SpiceUsr.h>

#include "spice.h"
#include "send.h"
#include "util.h"   /* das_xml_escape */

void das_spice_err_setup(void)
{
	errprt_c("SET", 4, "ALL");   /* Want all error text */
	erract_c("SET", 7, "RETURN");/* Just return on error, we'll check for it */
	errdev_c("SET", 4, "NULL");  /* Don't go printing to the screen on error */
}

static void strfort2c(char* s, int nLen)
{
	char* p = s + nLen - 1;
	while(p >= s){
		if((*p != ' ')&&(*p != '\0')) break;
		if(*p == ' ') *p = '\0';
		p--;
	}
}

static char g_sMsgBuf[2048] = {'\0'};

const char* das_get_spice_error(void)
{
	memset(g_sMsgBuf, 0, 2048);
	getmsg_c("SHORT", 40, (char*)g_sMsgBuf);
	
	/* Convert from fortran right-space padding to C-null termination */
	strfort2c(g_sMsgBuf, 41);
	
	int iNext = strlen(g_sMsgBuf);
	g_sMsgBuf[iNext] = ' ';
	iNext++;
	getmsg_c("LONG", 2047 - iNext, g_sMsgBuf+iNext);
	
	strfort2c(g_sMsgBuf, 1842);
	
	return g_sMsgBuf;
}	


int das_send_spice_err(int nDasVer, das_except_t nType)
{
	char sMsg[1842] = {'\0'};
	char sMsgEsc[2048] = {'\0'};
	char sOut[3072] = {'\0'};
	int iNext = 0; 
	
	getmsg_c("SHORT", 40, (char*)sMsg);
	
	/* Convert from fortran right-space padding to C-null termination */
	strfort2c(sMsg, 41);
	
	iNext = strlen(sMsg);
	sMsg[iNext] = ' ';
	iNext++;
	getmsg_c("LONG", 1841 - iNext, ((char*)sMsg)+iNext);
	
	strfort2c(sMsg, 1842);
		
	fprintf(stderr, "ERROR: %s\n", sMsg);

	
	switch(nDasVer){ 
	case 1:
		// Das 1 has no concept of in-stream error reports
		break;
	case 2:
		das_xml_escape(sMsgEsc, sMsg, sizeof(sMsgEsc));
		snprintf(sOut, 3071,
			"<exception type=\"%s\"\n"
			"           message=\"%s\" />\n", das_except_toStr(nType, 2), sMsgEsc
		);
		printf("[XX]%06zu%s", strlen(sOut), sOut);
		break;

	case 3:
		das_xml_escape(sMsgEsc, sMsg, sizeof(sMsgEsc));
		snprintf(sOut, 3071,
			"<exception type=\"%s\">\n"
			"%s\n"
			"</exception>\n", das_except_toStr(nType, 3), sMsgEsc
		);
		printf("|Ex||%zu|%s", strlen(sOut), sOut);
		break;

	default:
		das_error(DASERR_SPICE, "Unknown stream version %d", nDasVer);	
		break;
	}

	return DASERR_SPICE;
}

int das_print_spice_error(const char* sProgName)
{
	char sMsg[1842] = {'\0'};
	int iNext = 0; 
	
	getmsg_c("SHORT", 40, (char*)sMsg);
	
	/* Convert from fortran right-space padding to C-null termination */
	strfort2c(sMsg, 41);
	
	iNext = strlen(sMsg);
	sMsg[iNext] = ' ';
	iNext++;
	getmsg_c("LONG", 1841 - iNext, ((char*)sMsg)+iNext);
	
	strfort2c(sMsg, 1842);
		
	if(sProgName) fprintf(stderr, "ERROR (%s): %s\n", sProgName, sMsg);
	else fprintf(stderr, "ERROR: %s\n", sMsg);
	
	return DASERR_SPICE;	
}
