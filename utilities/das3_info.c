/* Copyright (C) 2024-2026   Chris Piker <chris-piker@uiowa.edu>
 *
 * Author: C. Piker
 * Updates via Claude Fable 5.1
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

   das3_info:  Load a das stream into memory and describe what arrived: the
               stream properties, then each dataset with its dimensions,
               variables and index ranges.  With -q nothing is printed and
               the exit status alone says whether the stream parsed.

**************************************************************************** */

#define _POSIX_C_SOURCE 200112L

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <das3/core.h>

/* ************************************************************************* */
/* Globals */

#define PERR DASERR_MAX + 1
#define DEF_AUTH_FILE ".dasauth"
#define PROG "das3_info"

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
"   " PROG " - Describe the datasets in a das stream\n"
"\n"
"USAGE\n"
"   " PROG " [options] [INPUT]\n"
"\n"
"DESCRIPTION\n"
"   " PROG " reads a das stream of any version into the das3 data model and\n"
"   prints what it holds: the stream properties, then one block per dataset\n"
"   giving its index ranges, properties, dimensions and variables.  Each\n"
"   variable line says where the values come from (a backing array with its\n"
"   index map, or a computed expression), the element type and units, the\n"
"   item count along each index, and the formalism with its parameters.\n"
"\n"
"   INPUT is a file name, or a URL if it starts with \"http://\" or\n"
"   \"https://\", in which case an HTTP GET is issued.  If INPUT is absent\n"
"   or is \"-\" the stream is read from standard input.\n"
"\n"
"   The report goes to standard output, log messages to standard error.\n"
"\n"
"OPTIONS\n"
"   -h, --help   Write this text to standard output and exit.\n"
"\n"
"   -q           Quiet.  Print nothing at all, just exit 0 if the stream\n"
"                parsed and non-zero otherwise.\n"
"\n"
"   -m           Also print memory statistics for each dataset.\n"
"\n"
"   -t SECONDS   Connection timeout for URL inputs, default is 6.\n"
"\n"
"   -l LEVEL     Log level, one of trace, debug, info, warning, error or\n"
"                critical.  The default is warning.\n"
"\n"
"AUTHENTICATION\n"
"   URLs that require a login are handled by prompting for credentials,\n"
"   which are cached for the session in " HOME_VAR "/" DEF_AUTH_FILE ".\n"
"\n"
"EXAMPLES\n"
"   Describe the Cassini/RPWS waveform example provided with the source:\n"
"\n"
"       " PROG " examples" DAS_DSEPS "ex07_cassini_rpws_wbr.d2s\n"
"\n"
"   Check that a reader's output parses, without looking at it:\n"
"\n"
"       ./my_reader 2024-01-01 2024-01-02 | " PROG " -q && echo OK\n"
"\n"
"SEE ALSO\n"
"   das3_text, das3_csv\n"
"\n"
"   The das3 data model is described at https://das2.org/das3\n"
"\n"
	);
}

/* ************************************************************************* */

static DasIO* openInput(const char* sInput, float rTimeout)
{
	if((sInput == NULL)||(strcmp(sInput, "-") == 0))
		return new_DasIO_cfile(PROG, stdin, "r");

	if((strncmp(sInput, "http://", 7) != 0)&&(strncmp(sInput, "https://", 8) != 0))
		return new_DasIO_file(PROG, sInput, "r");

	/* A URL: the credential manager only matters if the server asks */
	char* sHome = getenv(HOME_ENV);
	if(sHome == NULL){
		das_error(PERR, "Environment variable %s not set", HOME_ENV);
		return NULL;
	}
	char sCredFile[256] = {'\0'};
	snprintf(sCredFile, 255, "%s%s%s", sHome, DAS_DSEPS, DEF_AUTH_FILE);
	DasCredMngr* pCreds = new_CredMngr(sCredFile);

	DasHttpResp res;
	memset(&res, 0, sizeof(DasHttpResp));

	if(!das_http_getBody(sInput, PROG, pCreds, &res, rTimeout)){
		if((res.nCode == 401)||(res.nCode == 403))
			das_error(DASERR_HTTP, "Authorization failure: %s", res.sError);
		else if((res.nCode == 400)||(res.nCode == 404))
			das_error(DASERR_HTTP, "Query error: %s", res.sError);
		else
			das_error(DASERR_HTTP, "Uncategorized error: %s", res.sError);
		return NULL;
	}

	char sUrl[1024] = {'\0'};
	das_url_toStr(&(res.url), sUrl, sizeof(sUrl) - 1);
	if(strcmp(sUrl, sInput) != 0)
		daslog_info_v("Redirected to %s", sUrl);

	if(DasHttpResp_useSsl(&res))
		return new_DasIO_ssl(PROG, res.pSsl, "r");
	else
		return new_DasIO_socket(PROG, res.nSockFd, "r");
}

/* ************************************************************************* */

int main(int argc, char** argv)
{
	const char* sInput = NULL;
	bool  bQuiet   = false;
	bool  bMemory  = false;
	float rTimeout = 6.0;
	int   nLogLvl  = DASLOG_WARN;

	for(int i = 1; i < argc; ++i){
		if((strcmp(argv[i], "-h") == 0)||(strcmp(argv[i], "--help") == 0)){
			prnHelp();
			return 0;
		}
		else if(strcmp(argv[i], "-q") == 0) bQuiet  = true;
		else if(strcmp(argv[i], "-m") == 0) bMemory = true;
		else if(strcmp(argv[i], "-t") == 0){
			if(++i == argc){ fprintf(stderr, "ERROR: -t needs a value\n"); return PERR; }
			rTimeout = (float)atof(argv[i]);
		}
		else if(strcmp(argv[i], "-l") == 0){
			if(++i == argc){ fprintf(stderr, "ERROR: -l needs a value\n"); return PERR; }
			nLogLvl = daslog_strlevel(argv[i]);
		}
		else if((argv[i][0] == '-')&&(argv[i][1] != '\0')){
			fprintf(stderr, "ERROR: unknown option '%s', use -h for help\n", argv[i]);
			return PERR;
		}
		else{
			if(sInput != NULL){
				fprintf(stderr, "ERROR: only one INPUT may be given, use -h for help\n");
				return PERR;
			}
			sInput = argv[i];
		}
	}

	/* Quiet means quiet: not even error text, the exit status is the report.
	   Errors are saved to a buffer nobody reads and returned up the stack
	   rather than printed on the way to exit(). */
	if(bQuiet)
		das_init(argv[0], DASERR_DIS_RET, 1024, DASLOG_NOTHING, NULL);
	else
		das_init(argv[0], DASERR_DIS_EXIT, 0, nLogLvl, NULL);

	DasIO* pIn = openInput(sInput, rTimeout);
	if(pIn == NULL) return PERR;

	DasIO_model(pIn, 3);   /* das2 streams are up-converted into das3 storage */

	/* Quiet mode only wants to know that every packet decodes, so the
	   builder stays out and a handler with no data hook goes in: DasIO
	   clears each dataset after every packet when nobody asks to keep it. */
	StreamHandler quiet;
	memset(&quiet, 0, sizeof(StreamHandler));
	DasDsBldr* pBldr = NULL;
	if(bQuiet){
		DasIO_addProcessor(pIn, &quiet);
	}
	else{
		pBldr = new_DasDsBldr();
		DasIO_addProcessor(pIn, (StreamHandler*)pBldr);
	}

	if(DasIO_readAll(pIn) != 0)
		return das_error(PERR, "Couldn't process input %s", sInput ? sInput : "<stdin>");

	if(bQuiet){
		del_DasIO(pIn);
		return 0;
	}

	DasStream* pSd = DasDsBldr_getStream(pBldr);
	DasDsBldr_release(pBldr);
	del_DasDsBldr(pBldr);
	del_DasIO(pIn);

	/* DasDs_toStr() returns NULL when it runs out of room, so grow until it
	   fits.  64 KB is plenty for a stream with a few small datasets. */
	size_t uLen = 65536;
	char* sBuf = (char*)calloc(uLen, 1);

	DasStream_info(pSd, sBuf, (int)uLen - 1);
	fputs(sBuf, stdout);

	DasDesc* pDesc = NULL;
	int nPktId = 0;
	while((pDesc = DasStream_nextDesc(pSd, &nPktId)) != NULL){
		if(DasDesc_type(pDesc) != DATASET) continue;
		DasDs* pDs = (DasDs*)pDesc;

		while(DasDs_toStr(pDs, sBuf, (int)uLen - 1) == NULL){
			uLen *= 2;
			sBuf = (char*)realloc(sBuf, uLen);
		}
		printf("Packet ID: %d\n%s\n", nPktId, sBuf);

		if(bMemory){
			printf("   Memory alloc:   %zu bytes\n", DasDs_memOwned(pDs));
			printf("   Memory used:    %zu bytes\n", DasDs_memUsed(pDs));
			printf("   Memory indexed: %zu bytes\n\n", DasDs_memIndexed(pDs));
		}
	}

	free(sBuf);
	del_DasStream(pSd);
	return 0;
}
