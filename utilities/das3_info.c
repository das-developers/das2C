/* Copyright (C) 2024-2026   Chris Piker <chris-piker@uiowa.edu>
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
#include <das3/creds.h>
#include <das3/http.h>

/* ************************************************************************* */
/* Globals */

#define PERR DASERR_MAX + 1
#define CRED_FILE "servers"   /* under das_userdir() */
#define PROG "das3_info"

#ifndef _WIN32
#define HELP_USERDIR "$HOME/.das"           /* for help text, das_userdir() does the lookup */
#else
#define HELP_USERDIR "%%USERPROFILE%%\\.das"
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
"   URLs that require a login prompt for a username and password.  Saved\n"
"   credentials in " HELP_USERDIR DAS_DSEPS CRED_FILE " are tried first.  The environment\n"
"   variable DAS_USERDIR replaces " HELP_USERDIR ".\n"
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
"   The das3 data model and this library are documented at\n"
"   https://das-developers.github.io/das2C/\n"
"\n"
	);
}

/* ************************************************************************* */

static DasIO* openInput(const char* sInput, float rTimeout)
{
	if((sInput == NULL)||(strcmp(sInput, "-") == 0))
		return new_DasIO(PROG, new_DasXport_stdin(), NULL);

	if((strncmp(sInput, "http://", 7) != 0)&&(strncmp(sInput, "https://", 8) != 0))
		return new_DasIO(PROG, new_DasXport_file(sInput, "r"), NULL);

	/* A URL: the credential manager only matters if the server asks */
	char sCredFile[256] = {'\0'};
	snprintf(sCredFile, sizeof(sCredFile), "%s" DAS_DSEPS CRED_FILE, das_userdir());
	DasCredMngr* pCreds = new_DasCredMngr();
	DasCredMngr_setFileStore(pCreds, sCredFile);
	DasCredMngr_load(pCreds);   /* no file yet is fine */
	DasCredMngr_setPrompt(pCreds, das_term_prompt, NULL);

	DasHttpResp* pRes = new_DasHttpResp_get(sInput, PROG, pCreds, rTimeout);
	DasIO* pIn = NULL;
	if(pRes == NULL){
		del_DasCredMngr(pCreds);
		return NULL;
	}

	if(!DasHttpResp_ok(pRes)){
		int nCode = DasHttpResp_code(pRes);
		const char* sErr = DasHttpResp_error(pRes);
		if((nCode == 401)||(nCode == 403))
			das_error(DASERR_HTTP, "Authorization failure: %s", sErr);
		else if((nCode == 400)||(nCode == 404))
			das_error(DASERR_HTTP, "Query error: %s", sErr);
		else
			das_error(DASERR_HTTP, "Uncategorized error: %s", sErr);
	}
	else{
		if(strcmp(DasHttpResp_url(pRes), sInput) != 0)
			daslog_info_v("Redirected to %s", DasHttpResp_url(pRes));
		pIn = new_DasIO(PROG, DasHttpResp_takeBody(pRes), NULL);
	}

	/* Logins are only needed while the request is made */
	del_DasHttpResp(pRes);
	del_DasCredMngr(pCreds);
	return pIn;
}

/* ************************************************************************* */

/* The quiet mode log handler */
static void _dropLog(const das_logmsg* pMsg, void* pUser){ (void)pMsg; (void)pUser; }

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
	   A hook that drops every message silences the library, and errors are
	   returned up the stack rather than printed on the way to exit(). */
	if(bQuiet){
		das_loghook hook = {_dropLog, NULL};
		das_init(argv[0], DASERR_DIS_RET, DASLOG_ERROR, &hook);
	}
	else
		das_init(argv[0], DASERR_DIS_EXIT, nLogLvl, NULL);

	DasIO* pIn = openInput(sInput, rTimeout);
	if(pIn == NULL) return PERR;

	DasIO_setModel(pIn, 3);   /* das2 streams are up-converted into das3 storage */

	/* Quiet mode only wants to know that the stream parses, so nothing is
	   kept.  Otherwise every record stays in memory for the report. */
	if(!bQuiet)
		DasIO_setAccum(pIn, true);

	if(DasIO_readAll(pIn) != 0)
		return das_error(PERR, "Couldn't process input %s", sInput ? sInput : "<stdin>");

	if(bQuiet){
		del_DasIO(pIn);
		return 0;
	}

	DasStream* pSd = DasIO_takeStream(pIn);
	del_DasIO(pIn);
	if(pSd == NULL)
		return das_error(PERR, "No stream header in %s", sInput ? sInput : "<stdin>");

	/* The info printers return the full length, as snprintf does, so grow
	   the buffer to fit whenever one comes back larger. */
	size_t uLen = 65536;
	char* sBuf = (char*)calloc(uLen, 1);

	int nNeed = DasStream_info(pSd, sBuf, uLen);
	if((nNeed >= 0)&&((size_t)nNeed >= uLen)){
		uLen = (size_t)nNeed + 1;
		sBuf = (char*)realloc(sBuf, uLen);
		DasStream_info(pSd, sBuf, uLen);
	}
	fputs(sBuf, stdout);

	DasDesc* pDesc = NULL;
	int nPktId = 0;
	while((pDesc = DasStream_nextDesc(pSd, &nPktId)) != NULL){
		if(DasDesc_type(pDesc) != DASDESC_DS) continue;
		DasDs* pDs = (DasDs*)pDesc;

		nNeed = DasDs_info(pDs, sBuf, uLen);
		if((nNeed >= 0)&&((size_t)nNeed >= uLen)){
			uLen = (size_t)nNeed + 1;
			sBuf = (char*)realloc(sBuf, uLen);
			DasDs_info(pDs, sBuf, uLen);
		}
		printf("Packet ID: %d\n%s\n", nPktId, sBuf);

		if(bMemory){
			printf("   Memory alloc:   %zu bytes\n", DasDs_memOwned(pDs));
			printf("   Memory used:    %zu bytes\n", DasDs_memUsed(pDs));
			printf("   Memory indexed: %zu bytes\n\n", DasDs_memIndexed(pDs));
		}
	}

	free(sBuf);
	del_DasDesc((DasDesc*)pSd);
	return 0;
}
