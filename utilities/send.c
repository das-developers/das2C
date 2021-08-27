/* Copyright (C) 2015-2017  Chris Piker <chris-piker@uiowa.edu>
 *
 * This file is part of libdas2, the Core Das2 C Library.
 * 
 * Libdas2 is free software; you can redistribute it and/or modify it under
 * the terms of the GNU Lesser General Public License version 2.1 as published
 * by the Free Software Foundation.
 *
 * Libdas2 is distributed in the hope that it will be useful, but WITHOUT ANY
 * WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS
 * FOR A PARTICULAR PURPOSE.  See the GNU Lesser General Public License for
 * more details.
 *
 * You should have received a copy of the GNU Lesser General Public License
 * version 2.1 along with libdas2; if not, see <http://www.gnu.org/licenses/>. 
 */


#define _POSIX_C_SOURCE 200112L

#include <string.h>
#include <stdio.h>
#include <stdarg.h>

#include <stdint.h>   /* C99 */
#include <stdbool.h>  /* C99 */

#include <das2/util.h>

#include "send.h"

void das_send_stub(int nDasVer)
{
	if(nDasVer == 2)
		printf("<stream version=\"2.2\"></stream>\n");
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
		das_xml_escape(sMsgEsc, sMsg, 1023);
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
		das_xml_escape(sMsgEsc, sMsg, 1023);
		snprintf(sOut, 1151, "<exception type=\"IllegalArgument\"\n"
		                     "           message=\"%s\" />\n", sMsgEsc);
		
		printf("[xx]%06zu%s", strlen(sOut), sOut);
	}
	return 0;
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
		das_xml_escape(sMsgEsc, sMsg, 1023);
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
		das_xml_escape(sMsgEsc, sMsg, 1023);
		das_xml_escape(sSrcEsc, sSource, 1023);
		snprintf(sOut, 1151, "<comment type=\"log:info\"\n"
		                     "         source=\"%s\"\n"
		                     "         value=\"%s\" />\n", sSrcEsc, sMsgEsc);
		
		printf("[xx]%06zu%s", strlen(sOut), sOut);
	}
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
