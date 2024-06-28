/* Copyright (C) 2024 Chris Piker <chris-piker@uiowa.edu>
 *
 * This file is part of das2C, the Core DAS C Library.
 * 
 * Das2C is free software; you can redistribute it and/or modify it under
 * the terms of the GNU Lesser General Public License version 2.1 as published
 * by the Free Software Foundation.
 *
 * Das2C is distributed in the hope that it will be useful, but WITHOUT ANY
 * WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS
 * FOR A PARTICULAR PURPOSE.  See the GNU Lesser General Public License for
 * more details.
 *
 * You should have received a copy of the GNU Lesser General Public License
 * version 2.1 along with Das2C; if not, see <http://www.gnu.org/licenses/>. 
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

bool g_bPropOut = false;

#define PERR (DASERR_MAX + 1)

/* Help ********************************************************************* */

void prnHelp()
{
	printf(
"SYNOPSIS:\n"
"   das3_csv - Export das streams to a delimited text format\n"
"\n"
"USAGE:\n"
"   das3_csv [-p] [-r DIGITS] [-s SUBSEC] [-d DELIM] < INFILE\n"
"\n"
"DESCRIPTION\n"
"   das3_csv is a filter.  It reads a das2 or das3 stream on standard input and\n"
"   writes a delimited text stream suitable for use in comman spreadsheet\n"
"   handler to standard output.\n"
"\n" 
"   Each dataset encountered in the input stream is collapsed to a single text\n"
"   header row for rank-1 data and two header rows for rank 2 or greater.  Since\n"
"   the stream may contain any number of datasets, the output may contain any\n"
"   number of header rows.  Users of the output should be on the look out for\n"
"   this condition if it would adversely adversely impact thier downstream\n"
"   software.\n"
"\n"
"   Each output row will contain the minimum number of field separators needed\n"
"   to distinguish the fields in that particular row.  No attempt is made to\n"
"   output the same number of field separators for all rows. Thus das3_csv is\n"
"   *not* strictly RFC-4180 compliant.\n"
"\n"
"DEFAULTS:\n"
"   * All object properties are dropped, except those used in header rows.\n"
"\n"
"   * The field delimiter character is a ';' (semicolon).\n"
"\n"
"   * Input UTF-8 values are output as-is, without conversions\n"
"\n"
"   * 32-bit floating point values are written with 6 significant digits in\n"
"     the mantissa and 2 digits in the exponent.\n"
"\n"
"   * 64-bit floating point values are written with 16 significant digits in\n"
"     the mantissa and 2 digits in the exponent.\n"
"\n"
"   * Time values are written as ISO-8601 timestamps with microsecond resolution,\n"
"     i.e. the pattern YYYY-MM-DDTHH:mm:SS.ssssss\n"
"\n"
"   * All output values are rounded normally instead of truncating fractions.\n"
"\n"
"   * All output text is encoded as UTF-8.\n"
"\n"
"OPTIONS:\n"
"\n"
"   -h,--help  Show this help text\n"
"\n"
"   -l LEVEL,--log=LEVEL\n"
"              Set the logging level, where LEVEL is one of 'debug', 'info',\n"
"              'warning', 'error' in order of decreasing verbosity.  All log\n"
"              messages go to the standard error channel, the default is 'info'.\n"
"\n"
"   -p,--props Output object property rows.  Each property row is tagged with\n"
"              a 1st column containing the string '#property#'\n"
"\n"
"   -d DELIM   Change the default text delimiter from ';' (semicolon) to some\n"
"              other ASCII 7-bit character.\n"
"\n"
"   -r DIGITS  Set the number of significant digits for general output.  The\n"
"              minimum resolution is 2 significant digits.\n"
"\n"
"   -s SUBSEC  Set the sub-second resolution.  Output N digits of sub-second\n"
"              resolution.  The minimum value is 0, thus time values are\n"
"              are always output to at least seconds resolution.\n"
"\n"
"AUTHOR:\n"
"   chris-piker@uiowa.edu\n"
"\n"
"SEE ALSO:\n"
"   das2_ascii, das3_cdf\n"
);
}

/* Stream Start ************************************************************* */

DasErrCode onStream(StreamDesc* pSd, void* pUser)
{
	if(!g_bPropOut) return DAS_OKAY;

	size_t uProps = DasDesc_length((DasDesc*)pSd);
	for(size_t u = 0; u < uProps; ++u){
		const DasProp* pProp = DasDesc_getPropByIdx((DasDesc*)pSd, u);
		if(pProp == NULL) continue;

		// Write in the order: sope name, type, units, value
		// for multi-value properties, use the spreadsheet's separator, not whatever
		// the property may have been using

		printf("\"#property\",\"%s\",\"%s\",\"%s\"", DasProp_name(pProp),
			DasProp_typeStr3(pProp), DasProp_units(pProp)
		);

		const char* sVal = DasProp_value(pProp);
		if(DasProp_items(pProp) < 2){
			printf("%s\r\n", sVal);
		}
		else{
			char cSep = DasProp_sep(pProp);
			putchar('"');
			while(*sVal != '\0'){
				if(*sVal == cSep)
					printf("\"%s\"", g_sSep);
				else
					putchar(*sVal);
			}
			puts("\"\r\n");
		}
	}
	return DAS_OKAY;
}


/* DataSet Start ************************************************************* */

DasErrCode onDataSet(StreamDesc* pSd, int iPktId, DasDs* pDs, void* pUser)
{
	return DAS_OKAY;
}

/* Dataset update ************************************************************ */

DasErrCode onData(StreamDesc* pSd, int iPktId, DasDs* pDs, void* pUser)
{
	return DAS_OKAY;
}

/* Exceptions *************************************************************** */

DasErrCode onExcept(OobExcept* pExcept, void* vpSep)
{
	/* Can't do much here but quit with log message */
	fprintf(stderr, "Stream Exception: %s, %s\n", pExcept->sType, pExcept->sMsg);
	
	return DAS_OKAY;
}

/* ************************************************************************* */
DasErrCode onClose(StreamDesc* pSd, void* pUser)
{

	return DAS_OKAY;
}

/* Main ********************************************************************* */

int main( int argc, char *argv[]) {

	int i = 0;
	/* int status = 0; */
	int nGenRes = 7;
	int nSecRes = 3;
	char sTimeFmt[64] = {'\0'};
	const char* sLevel = "info";
	
	/* Exit on errors, log info messages and above */
	das_init(argv[0], DASERR_DIS_EXIT, 0, DASLOG_INFO, NULL);
	
	for(i = 1; i < argc; i++){
		if(strcmp(argv[i], "-h") == 0 || strcmp(argv[i], "--help") == 0 ){
			prnHelp();
			return 0;
		}
		
		if(strcmp(argv[i], "-r") == 0){
			i++;
			if(i >= argc)
				return das_error(PERR, "Resolution parameter missing after -r\n");
			
			nGenRes = atoi(argv[i]);
			if(nGenRes < 2 || nGenRes > 18)
				return das_error(PERR, 
					"Can't format to %d significant digits, supported range is "
					"only 2 to 18 significant digits.\n", nGenRes
				);
			
			continue;
		}
		if(strcmp(argv[i], "-l") == 0 || strcmp(argv[i], "--log") == 0){
			sLevel = argv[i];
			continue;
		}
		
		if(strcmp(argv[i], "-s") == 0){
			i++;
			if(i >= argc)
				return das_error(PERR,"Sub-seconds resolution parameter missing after -s\n");
			
			nSecRes = atoi(argv[i]);
			if(nSecRes < 0 || nSecRes > 9)
				return das_error(
					PERR, "Only 0 to 9 sub-seconds digits supported don't know how to "
					"handle %d sub-second digits.", nSecRes
				);
			continue;
		}
		
		if(strcmp(argv[i], "-d") == 0){
			int j;
			for(j = 0; (j < strlen(argv[i])) && (j < 11); ++j){
				g_sSep[j] = argv[i][j];
			}
			g_sSep[j] = '\0';
			continue;
		}
		
		return das_error(PERR, "unknown parameter '%s'\n", argv[i]);
	}

	daslog_setlevel(daslog_strlevel(sLevel));
	
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
	
	StreamHandler handler;
	memset(&handler, 0, sizeof(StreamHandler));
	handler.streamDescHandler = onStream;
	handler.dsDescHandler     = onDataSet;
	handler.dsDataHandler     = onData;
	handler.exceptionHandler  = onExcept;
	handler.closeHandler      = onClose;
	/* handler.userData          = &ctx; */

	DasIO* pIn = new_DasIO_cfile("Standard Input", stdin, "r");
	DasIO_model(pIn, 3); /* Upgrade any das2 <packets>s to das3 <datasets>s */

	DasIO_addProcessor(pIn, &handler);
	
	int nRet = DasIO_readAll(pIn);
	return nRet;
}
