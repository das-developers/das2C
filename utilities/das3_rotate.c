/* Copyright (C) 2024   Chris Piker <chris-piker@uiowa.edu>
 *
 * This file is part of das2C, the Core Das2 C Library.
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
  
   das3_rotate: Change coordinate frames for vector variables

**************************************************************************** */

#include <string.h>

#include <SpiceUsr.h>

#include <das2/core.h>
#include <das2/spice.h>

#define PROG "das3_rotate"
#define PERR 63

/* ************************************************************************* */
/* Globals */

/* The maximum number of frame transforms handled by the program */
#define MAX_XFORMS 24

/* Freak big stack variable to print frames.  Hopefully C++ spice will allow
   for heap data */
#define MAX_DEFINED_FRAMES 100

/* ************************************************************************* */
void prnHelp()
{
	printf(
"SYNOPSIS\n"
"   " PROG " - Rotate vectors in das3 stream to new coordinate frames\n"
"\n"
"USAGE\n"
"   " PROG " [options] META_KERNEL [IN_FRAME1:]OUT_FRAME1 [IN_FRAME1:OUT_FRAME2]\n"
"\n"
"DESCRIPTION\n"
"   " PROG " is a filter, it reads das3 streams on standard input attempts to\n"
"   and vector variables in the given INPUT_FRAME into the given OUTPUT_FRAME.\n"
"   Rotation matricies are provide by the CSpice library driven by the given\n"
"   META_KERNEL file.  The transformed stream is written to standard output.\n"
"   Since das2 streams do not have the concept of a geometric vector, das3\n"
"   streams are expected as input.\n"
"\n"
"   Transforms are specified by the set:\n"
"\n"
"      INPUT_FRAME \":\" OUTPUT_FRAME\n"
"\n"
"   without spaces!  If the input frame is not given, all input vector frames\n"
"   automatically match the transform rule.  The program assumes that `:` is\n"
"   not a legal character in a vector frame name."
"\n"
"OPTIONS\n"
"   -h,--help   Write this text to standard output and exit.\n"
"\n"
"   -l LEVEL,--log=LEVEL\n"
"               Set the logging level, where LEVEL is one of 'debug', 'info',\n"
"               'warning', 'error' in order of decreasing verbosity.  All log\n"
"               messages go to the standard error channel, the default is'info'.\n"
"\n"
"   -a FRAME,--anonymous FRAME\n"
"               If the input stream has anonymous vector frames, assume they are\n"
"               in this frame.\n"
"\n"
"   -c,--coords Only rotate matching coodinate vectors, ignore data vectors.\n"
"\n"
"   -d,--data   Only rotate data vectors, ignore matching coordinate vectors.\n"
"\n"
"   -k,--keep   By default original input vectors are not emitted on the output\n"
"               stream, but the command line argument `-k` may be used to\n"
"               preserve the original vectors alongside the rotated items.\n"
"\n"
"   -L,--list   An information option.  Just print all frames defined in the\n"
"               given metakernel to the standard error channel and exit\n"
"\n"
"EXAMPLES\n"
"   1. Just see what frames are defined in a given metakernel:\n"
"\n"
"      das3_rotate -L my_metakernel.tm\n"
"\n"
"   2. Convert MAG data vectors from the any loaded coordiante system into the TSS\n"
"      TSS frame and write the results to a CDF file:\n"
"\n"
"      das_get site:uiowa/tracers/l1/mag/bdc_roi time:2024-01-01,2024-01-02 \\\n"
"              | das3_rotate tra_metakern.tm TSS \\\n"
"              | das3_cdf -o ./\\n"
"\n"
"AUTHOR\n"
"   chris-piker@uiowa.edu\n"
"\n"
"SEE ALSO\n"
"   * das_get, das3_cdf\n"
"   * Wiki page https://github.com/das-developers/das2C/wiki/das3_rotate\n"
"   * SPICE Frames https://naif.jpl.nasa.gov/pub/naif/toolkit_docs/C/req/frames.html\n"
"\n");

}

/* ************************************************************************* */

struct frame_xform {
	const char* sOutFrame; /* May not be NULL */
	const char* sInFrame;  /* May be NULL!, means try to match any */
};

typedef struct program_options{
	bool bListFrames;
	bool bCoordsOnly;
	bool bDataOnly;
	bool bKeepOrig;
	char aLevel[32];      /* Log level */
	char aMetaKern[256];  /* The metakernel file name */
	char aAnonFrame[32];
	struct frame_xform aXForms[MAX_XFORMS + 1]; /* Always terminating null struct */
} popts_t;

int parseArgs(int argc, char** argv, popts_t* pOpts)
{
	memset(pOpts, 0, sizeof(popts_t));  /* <- Defaults struct values to 0 */

	strncpy(pOpts->aLevel, "info", DAS_FIELD_SZ(popts_t,aLevel) - 1);
	int iXForms = 0;
	int i = 0;
	while(i < (argc-1)){
		++i;  /* Increments an handily skips past the program name */

		if(argv[i][0] == '-'){
			if(dascmd_isArg(argv[i], "-h", "--help", NULL)){
				prnHelp();
				exit(0);
			}
			if(dascmd_isArg(argv[i], "-c", "--coords", NULL)){
				pOpts->bCoordsOnly = true;
				continue;
			}
			if(dascmd_isArg(argv[i], "-d", "--data", NULL)){
				pOpts->bDataOnly = true;
				continue;
			}
			if(dascmd_isArg(argv[i], "-k", "--keep", NULL)){
				pOpts->bKeepOrig = true;
				continue;
			}
			if(dascmd_isArg(argv[i], "-L", "--list", NULL)){
				pOpts->bListFrames = true;
				continue;
			}
			if(dascmd_getArgVal(
				pOpts->aLevel,    DAS_FIELD_SZ(popts_t,aLevel),      argv, argc, &i, 
				"-l", "--log="
			))
				continue;
			if(dascmd_getArgVal(
				pOpts->aAnonFrame, DAS_FIELD_SZ(popts_t,aAnonFrame), argv, argc, &i, 
				"-a", "--anonymous="
			))
				continue;
			if(dascmd_getArgVal(
				pOpts->aAnonFrame, DAS_FIELD_SZ(popts_t,aAnonFrame), argv, argc, &i, 
				"-a", "--anonymous="
			))
				continue;

			return das_error(PERR, "Unknown command line argument '%s'", argv[i]);
		}
		else{
			if(pOpts->aMetaKern[0] == '\0'){
				strncpy(pOpts->aMetaKern, argv[i], 255);
			}
			else{
				if(iXForms >= MAX_XFORMS){
					return das_error(PERR, 
						"Maximum number of frame transformations exceeded (%d)", MAX_XFORMS
					);
				}
				pOpts->aXForms[iXForms].sOutFrame = argv[i];

				char* pSep = strchr(argv[i], ':');
				if(pSep != NULL){
					if(*(pSep+1) == '\0')
						return das_error(PERR, "Output frame missing after ':' in '%s'", argv[i]);
				
					*pSep = '\0';
					pOpts->aXForms[iXForms].sInFrame = pSep + 1;
				}
			}
		} 
	} /* end arg loop */

	/* Check args */
	if(pOpts->aMetaKern[0] == '\0')
		return das_error(PERR, "Meta-kernel file was not provided");

	if(!(pOpts->bListFrames)){
		if(pOpts->aXForms[0].sOutFrame == NULL)
			return das_error(PERR, "No frame transformations were given");
	}

	return DAS_OKAY;
}

/* ************************************************************************* */
/* Handy end-user tool, print frames defined in a kernel */
void prnFrames(){

	SpiceInt nFrmType[] = {
		SPICE_FRMTYP_PCK, SPICE_FRMTYP_CK, SPICE_FRMTYP_TK, SPICE_FRMTYP_DYN,
		SPICE_FRMTYP_SWTCH, 0
	};

	const char* sFrmType[] = {
		"PCK-based frames", "CK-based frames", "Text Kernel frames", 
		"Dynamic frames", "Switch frames", NULL
	};

	SPICEINT_CELL (cells, MAX_DEFINED_FRAMES);

	SpiceChar sFrame[ 34 ];

	for(int i = 0; sFrmType[i] != NULL; ++i){
	
		kplfrm_c(nFrmType[i], &cells);
		fprintf(stderr, "There are %d %s frames defined:\n", (int)card_c(&cells), sFrmType[i]);

		for(int j = 0; j < card_c(&cells); ++j){
			SpiceInt nFrm = ((SpiceInt*)cells.data)[j];
			frmnam_c(nFrm, 33, sFrame);
			fprintf(stderr, "   %12d   %s\n", nFrm, sFrame);
		}
		putc('\n', stderr);
	}
}

/* ************************************************************************* */

int main(int argc, char** argv) 
{
	/* Exit on errors, log info messages and above */
	das_init(argv[0], DASERR_DIS_EXIT, 0, DASLOG_INFO, NULL);


	popts_t opts;
	
	if(parseArgs(argc, argv, &opts) != DAS_OKAY)
		return 13;

	daslog_setlevel(daslog_strlevel(opts.aLevel));

	das_spice_err_setup(); /* Don't emit spice errors to stdout */

	furnsh_c(opts.aMetaKern);
	if(failed_c()){
		das_send_spice_err(3, DAS2_EXCEPT_SERVER_ERROR);
	}

	/* Whole different path, just print stuff to stderr */
	if(opts.bListFrames){
		prnFrames();
		return 0;
	}

	das_error(DASERR_NOTIMP, "Actual program body not yet impliment, just the -L option");
	return PERR;
};