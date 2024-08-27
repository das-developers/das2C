/* Copyright (C) 2024   Chris Piker <chris-piker@uiowa.edu>
 *
 * This file is part of das2C, the Core Das C Library.
 *
 * das2C is free software; you can redistribute it and/or modify it under
 * the terms of the GNU Lesser General Public License version 2.1 as published
 * by the Free Software Foundation.
 *
 * das2C is distributed in the hope that it will be useful, but WITHOUT ANY
 * WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS
 * FOR A PARTICULAR PURPOSE.  See the GNU Lesser General Public License for
 * more details.
 *
 * You should have received a copy of the GNU Lesser General Public License
 * version 2.1 along with das2C; if not, see <http://www.gnu.org/licenses/>.
 */

/* *************************************************************************
  
   das3_test:  Load a dasStream into memory and then do nothing with it.
               Mostly useful for seeing if a stream is parsable into the
               das3 data model.

**************************************************************************** */
 
#define _POSIX_C_SOURCE 200112L

#include <stdio.h>
#include <stdlib.h>
#include <math.h>
#include <string.h>

#ifdef _WIN32
#include <direct.h>
#define getcwd _getcwd
#else
#include <unistd.h>
#endif

#include <das2/core.h>

/* ************************************************************************* */
/* Globals */

#define PERR DASERR_MAX + 1
#define DEF_AUTH_FILE ".dasauth"
#define PROG "das3_test"

#ifndef _WIN32
#define HOME_VAR "$HOME"
#define HOME_ENV "HOME"
#else
#define HOME_VAR "%%USERPROFILE%%"
#define HOME_ENV "USERPROFILE"
#endif

/* ************************************************************************* */

void prnHelp()
{
	printf(
"SYNOPSIS\n"
"   " PROG " - Test reading a dasStream of any version into the das3 data model\n"
"\n"
"USAGE\n"
"   " PROG " [-h] INPUT\n"
"\n"
"DESCRIPTION\n"
"   " PROG " is just a test program for data source developers.  It reads a\n"
"   or URL into memory, prints memory usage statistics and exits.  If the\n"
"   stream can not be parsed the program exists with a non-zero return value.\n"
"   The only required parameter is the INPUT.  If INPUT starts with:\n"
"\n"
"     \"http://\"\n"
"     \"https://\"\n"
"\n"
"   Then an HTTP GET query is issued, otherwise INPUT is assumed to be a\n"
"   filename.\n"
"\n"
"OPTIONS\n"
"   -h, --help   Write this text to standard output and exit.\n"
"\n"
"EXAMPLE\n"
"   Test the Cassini/RPWS waveform example provide with the source distribution:\n"
"\n"
"       " PROG " test" DAS_DSEPS "cassini_rpws_wfrm_sample.d2s\n"
"\n"
"FILES\n"
"   \"" HOME_VAR DAS_DSEPS DEF_AUTH_FILE "\"\n"
"       Holds any cached credentials used to access restricted server URLs.\n"
"\n"
"SEE ALSO\n"
"   das_verify - a tool provided by das2py for validating streams.\n"
"\n"
	);
}

/* ************************************************************************* */

int main(int argc, char** argv)
{	
	/* Exit on errors, log info messages and above */
	das_init(argv[0], DASERR_DIS_EXIT, 0, DASLOG_INFO, NULL);

	for(int i = 1; i < argc; ++i){
		if((strcmp(argv[i], "-h") == 0)||(strcmp(argv[i], "--help") == 0)){
			prnHelp();
			return 0;
		}
	}
	
	const char* sInput = NULL;
	if(argc < 2){
		fprintf(stderr, "Input stream not specified, use -h for help.\n");
		return PERR;
	}
	else
		sInput = argv[1];

	/* daslog_info_v("Reading %s", sInput); */

	DasIO* pIn = NULL;
	DasCredMngr* pCreds = NULL;
	DasHttpResp res; memset(&res, 0, sizeof(DasHttpResp));

	/* If the input starts with http:// or https:// get from the network */
	if(
		(strncmp(sInput, "http://", 7) == 0)||
		(strncmp(sInput, "https://", 8) == 0)
	){
		char* sHome = getenv(HOME_ENV);
		if(sHome == NULL)
			return das_error(PERR, "Environment variable %s not set", HOME_ENV);

		char sCredFile[256] = {'\0'};
		snprintf(sCredFile, 255, "%s%s%s", sHome, DAS_DSEPS, DEF_AUTH_FILE);
		pCreds = new_CredMngr(sCredFile);

		/* Give it a connection time out of 6 seconds */
		if(!das_http_getBody(sInput, PROG, pCreds, &res, 6.0)){

			if((res.nCode == 401)||(res.nCode == 403))
				return das_error(DASERR_HTTP, "Authorization failure: %s", res.sError);
			
			if((res.nCode == 400)||(res.nCode == 404))
				return das_error(DASERR_HTTP, "Query error: %s", res.sError);

			return das_error(DASERR_HTTP, "Uncatorize error: %s", res.sError);
		}
		
		char sUrl[1024] = {'\0'};
		das_url_toStr(&(res.url), sUrl, sizeof(sUrl) - 1);
		
		if(strcmp(sUrl, sInput) != 0)
			daslog_info_v("Redirected to %s", sUrl);

		if(DasHttpResp_useSsl(&res))
			pIn = new_DasIO_ssl(PROG, res.pSsl, "r");
		else
			pIn = new_DasIO_socket(PROG, res.nSockFd, "r");
	}
	else{
		pIn = new_DasIO_file(PROG, sInput, "r");
	}

	DasDsBldr* pBldr = new_DasDsBldr();
	DasIO_addProcessor(pIn, (StreamHandler*)pBldr);
	
	if(DasIO_readAll(pIn) != 0)
		return das_error(PERR, "Couldn't process input file %s", argv[1]);
	
	DasStream* pSd = DasDsBldr_getStream(pBldr);

	/* We don't need the builder anymore, we have the streams */
	DasDsBldr_release(pBldr);  /* Let the build know that I own stream object... */

	/* The next line is causing an error at exit on windows, find out why.
	   The error does not stop the log line at the bottom from running but
	   does trigger a non-zero return to the shell... curious. */
	del_DasDsBldr(pBldr);  /* Automatically closes the file, not sure if I like this */
	del_DasIO(pIn);

	/* Print of the amount of data read for each dataset in the stream */
	DasDesc* pDesc = NULL;
	size_t uSets = 0;
	int nPktId = 0;
	char sShape[128] = {'\0'};
	ptrdiff_t aShape[DASIDX_MAX] = DASIDX_INIT_UNUSED;
		
	while((pDesc = DasStream_nextDesc(pSd, &nPktId)) != NULL){
		if(DasDesc_type(pDesc) == DATASET){
			DasDs* pDs = (DasDs*)pDesc;
			int nRank = DasDs_shape(pDs, aShape);
			das_shape_prnRng(aShape, nRank, nRank, sShape, 127);

			daslog_info_v("Dataset %s%s", DasDs_id(pDs), sShape);
			daslog_info_v("Dataset memory alloc:   %zu bytes", DasDs_memOwned(pDs));
			daslog_info_v("Dataset memory used:    %zu bytes", DasDs_memUsed(pDs));
			daslog_info_v("Dataset memory indexed: %zu bytes", DasDs_memIndexed(pDs));
		}
		++uSets;
	}

	del_DasStream(pSd); /* Done with the data */

	daslog_info_v("%u datasets sucessfully loaded and unloaded", uSets);
	return 0;
}
