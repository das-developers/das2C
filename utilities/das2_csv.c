/* Copyright (C) 2020 Chris Piker <chris-piker@uiowa.edu>
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

#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <string.h>
#include <locale.h>
#include <limits.h>

#ifndef _WIN32 
#include <unistd.h> 
#endif

#include <das2/core.h>

/* State ******************************************************************** */

int g_nTimeWidth = 24;    /* default to 'time24' */
const char* g_sTimeFmt = NULL;

int g_n8ByteWidth = 17;   /* default to 'ascii17' for 8-byte floats */
int g_n4ByteWidth = 14;   /* default to 'ascii14' for 4-byte floats */
char g_sSep[12] = {';', '\0'};

/* Help ********************************************************************* */

void prnHelp(FILE* pOut)
{
	fprintf(pOut,
"SYNOPSIS:\n"
"   das2_csv - Export a das2 stream to a delimited text format\n"
"\n"
"USAGE:\n"
"   das2_csv [-r DIGITS] [-s SUBSEC] [-d DELIM]\n"
"\n"
"DESCRIPTION\n"
"   das2_csv is a filter.  It reads a das2 stream on standard input and writes\n"
"   a text delimited stream suitable for use in comman spreadsheet programs to\n"
"   standard output.\n"
"\n" 
"   Each das2 packet header encountered in the das2 stream is collapsed to a\n"
"   single text header row.  Since the stream may contain any number of packet\n"
"   headers, the output may contain any number of header rows.  Users of the\n"
"   output CSV data should be on the look out for this condition if it would\n"
"   adversely impact thier downstream software.\n"
"\n"
"   By default the field delimiter character is a ';' (semicolon).\n"
"\n"
"   By default 32-bit floating points numbers are written with 7 significant\n"
"   digits in the mantissa and 2 digits in the exponent.  Any 64-bit floats\n"
"   encontered in the input stream are written with 17 significant digits in\n"
"   the mantissa and 2 digits in the exponent.  Binary time values are written\n"
"   as ISO-8601 timestamps with microsecond resolution, i.e. the pattern\n"
"   yyyy-mm-ddThh:mm:ss.ssssss\n"
"\n"
"   All output values are rounded normally instead of truncating fractions.\n"
"\n"
"   All output text is encoded as UTF-8.\n"
"\n"
"OPTIONS:\n"
"\n"
"   -h,--help\n"
"         Print this help text\n"
"\n"
"   -d DELIM   Change the default text delimiter from ';' (semicolon) to some\n"
"              other UTF-8 character.\n"
"\n"
"   -r DIGITS  Set the number of significant digits for general output.  The\n"
"              minimum resolution is 2 significant digits.\n"
"\n"
"   -s SUBSEC  Set the sub-second resolution.  Output N digits of sub-second\n"
"              resolution.  The minimum value is 0, thus time values are\n"
"              are always output to at least seconds resolution.\n"
"\n"
"AUTHORS:\n"
"   chris-piker@uiowa.edu\n"
"\n"
"SEE ALSO:\n"
"   das2_ascii, das2_binary, das2_hapi\n"
"\n"
"   and the das 2 ICD @ http://das2.org for an introduction to the das 2 system.\n"
"\n");	
}

/* Headers ****************************************************************** */

DasErrCode OnPktHdr(StreamDesc* pSdIn, PktDesc* pPdIn, void* vpSep)
{
	/* For each plane, print a header */
	for
	
}

/* Data ********************************************************************* */

DasErrCode onPktData(PktDesc* pPdIn, void* vpSep)
{
	
}


/* Exceptions *************************************************************** */

DasErrCode onException(OobExcept* pExcept, void* vpSep)
{
	/* Can't do much here but quit with log message */
	fprintf(stderr, "Stream Exception: %s, %s\n", pExcept->sType, pExcept->sMsg);
	
	return DAS_OKAY;
}

/* Main ********************************************************************* */

int main( int argc, char *argv[]) {

	int i = 0;
	int status = 0;
	int nGenRes = 7;
	int nSecRes = 3;
	char sTimeFmt[64] = {'\0'};
	
	/* Exit on errors, log info messages and above */
	das_init(argv[0], DASERR_DIS_EXIT, 0, DASLOG_INFO, NULL);
	
	for(i = 1; i < argc; i++){
		if(strcmp(argv[i], "-h") == 0 || strcmp(argv[i], "--help") == 0 ){
			prnHelp();
			return 0;
		}
		
		if(strcmp(argv[i], "-r") == 0){
			i++;
			if(i >= argc){
				fprintf(stderr, "ERROR: Resolution parameter missing after -r\n");
				return 13;
			}
			nGenRes = atoi(argv[i]);
			if(nGenRes < 2 || nGenRes > 18){
				fprintf(stderr, "ERROR: Can't format to %d significant digits, "
						  "Supported range is only 2 to 18 significant digits.\n",
						  nGenRes);
				return 13;
			}
			continue;
		}
		
		if(strcmp(argv[i], "-v") == 0 || strcmp(argv[i], "--version") == 0){
			printf("$TODO: Find a git auto substitution method$\n");
			return 0;
		}
		
		if(strcmp(argv[i], "-s") == 0){
			i++;
			if(i >= argc){
				fprintf(stderr, "ERROR: Sub-seconds resolution parameter missing after -s\n");
				return 13;
			}
			nSecRes = atoi(argv[i]);
			if(nSecRes < 0 || nSecRes > 9){
				fprintf(stderr, "ERROR: Only 0 to 9 sub-seconds digits supported "
				        "don't know how to handle %d sub-second digits.", nSecRes);
				return 13;
			}
			continue;
		}
		
		if(strcmp(argv[i], "-d") == 0){
			for(int j = 0; (j < strlen(argv[i])) && (j < 11); ++j)
				sSep[j] = argv[i][j];
			continue;
		}
		
		fprintf(stderr, "ERROR: unknown parameter '%s'\n", argv[i]);
		return 13;
	}
	
	if(nGenRes != 7){
		g_n4ByteWidth = nGenRes + 7;
		g_n8ByteWidth = nGenRes + 7;
	}
	
	if(nSecRes != 3){
		if(nSecRes == 0)
			g_nTimeWidth = 20;
		else
			g_nTimeWidth = 21 + nSecRes;
		
		sprintf(sTimeFmt, "%%04d-%%02d-%%02dT%%02d:%%02d:%%0%d.%df", 
				  nSecRes + 3, nSecRes);
		g_sTimeFmt = sTimeFmt;
	}
	
	StreamHandler* pSh = new_StreamHandler(NULL);
	
	pSh->streamDescHandler = NULL;
	pSh->pktDescHandler    = OnPktHdr;
	pSh->pktDataHandler    = onPktData;
	pSh->exceptionHandler  = onException;
	pSh->commentHandler    = NULL;
	pSh->closeHandler      = onClose;

	DasIO* pIn = new_DasIO_cfile("Standard Input", stdin, "r");
	DasIO_addProcessor(pIn, pSh);
	
	int nRet = DasIO_readAll(pIn);
	return nRet;
}
