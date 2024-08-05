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

#define _POSIX_C_SOURCE 200112L

#include <stdbool.h>
#include <stdio.h>
#include <string.h>
#ifdef _WIN32
#define strcasecmp _stricmp
#define strncasecmp _strnicmp
#else
#include <strings.h>
#endif

#include <SpiceUsr.h>

#include <das2/core.h>
#include <das2/spice.h>

#define PROG "das3_spice"
#define PERR 63

/* ************************************************************************* */
/* Globals */

/* The maximum number of frame transforms handled by the program */
#define MAX_LOCS  6
#define MAX_ROTS 24

/* Freaking big SPICE stack variable to print frames.  Hopefully C++ spice will
   allow for heap data */
#define MAX_DEFINED_FRAMES 100

/* ************************************************************************* */
void prnHelp()
{
	printf(
"SYNOPSIS\n"
"   " PROG " - Modify das streams using SPICE kernels\n"
"\n"
"USAGE\n"
"   " PROG " [options] META_KERNEL ...\n"
"\n"
"DESCRIPTION\n"
"   " PROG " is a filter, it reads a das2 or das3 stream containing time\n"
"   coordinates on standard input, modifies values and structures using SPICE\n"
"   information, and writes a das3 stream to standard output.\n"
"\n"
"   A SPICE meta-kernel file is always required as a parameter. In addition at\n"
"   least one SPICE operation must be provided on the command line.\n"
"\n"
"   Three types of operations are supported:\n"
"\n"
"      (-L) Just list meta-kernel information\n"
"      (-E) Add spacecraft location vectors in a given frame\n"
"      (-R) Rotate vector variables into a new coordinate frame\n"
"\n"
"   The last two operations are described in successive sections below.\n"
"\n"
"   Adding Location Coordinates (Ephemerides)\n"
"   -----------------------------------------\n"
"   To add location information in a given coordinate FRAME, provide command\n"
"   line arguments of the form:\n"
"\n"
"      -E FRAME[:AXES]\n"
"\n"
"   The FRAME is the name of any SPICE frame, either built-in, or provided by\n"
"   the meta-kernel file. To list all defined frames use the '-L' option.\n"
"\n"
"   The AXES are not required. If omitted, 3-D cartesian coordinates are\n"
"   assumed. If AXES are provided, use comma separated values from one of the\n"
"   following sets:\n"
"\n"
"      x,y,z       - Cartesian coordinates\n"
"      rho,phi,z   - Cylindrical coordinates\n"
"      r,theta,phi - Spherical coordinates\n"
"\n"
"   To output less then the full set of coordinates an axes subset can be\n"
"   provided. Thus the operation:\n"
"\n"
"      -E IAU_JUPITER:r,theta\n"
"\n"
"    will only add Jupiter centered radial distance and co-latitude values to\n"
"    the stream, longitude will not be emitted.\n"
"\n"
"   The output stream will have location values added to each packet. These\n"
"   will be defined by adding additional <coord> elements to each <dataset>.\n"
"   New <coord> elements will not have a plot-axis affinity unless \"-b\", or\n"
"   it's equivalent \"--bind-axis\" is supplied. If axis affinities are\n"
"   enabled then location data are bound to plot axes as follows:\n"
"\n"
"     x,y,z - For basic cartesian frames\n"
"     ρ,φ,z - For cylindrical frames\n"
"     r,θ,φ - For spherical transforms\n"
"\n"
"   In the input stream, if a non-geometric coordinate is associated with one\n"
"   of these axes, using \"-b\" will trigger it's re-association with another\n"
"   axis. For example, if the physical dimension of \"time\" is initially\n"
"   associated with the X axis in the input stream, it would be re-assigned to\n"
"   to the T axis.\n"
"\n"
"   Rotating Coordinate and Data Vectors\n"
"   ------------------------------------\n"
"   To rotate vectors to another SPICE frame, provide command line arguments of\n"
"   the form:\n"
"\n"
"     -R OUT_FRAME[:IN_FRAME]\n"
"\n"
"   The IN_FRAME and colon are not required. If omitted " PROG " will attempt to\n"
"   rotate *all* vectors in the input stream to the given OUT_FRAME. Coordinate\n"
"   vectors added via " PROG " are not candidates for rotation, since this would\n"
"   be redundant.\n"
"\n"
"   By default, any matching input coordinate vectors or data vectors are\n"
"   rotated and the original values are *dropped* from the stream. To change\n"
"   this behavior use '-k' to \"keep\" inputs. To only rotate either <coord>\n"
"   or <data> values, use '-c' or '-d'.\n"
"\n"
"   Rotation operations will not work for input das2 streams because these do\n"
"   not have the concept of a geometric vector. (Dev note: Support could be\n"
"   added via additional options in a future version if desired.)\n"
"\n"
"OPTIONS\n"
"   -h, --help   Write this text to standard output and exit.\n"
"\n"
"   -l LEVEL, --log=LEVEL\n"
"               Set the logging level, where LEVEL is one of 'debug', 'info',\n"
"               'warning', 'error' in order of decreasing verbosity. All log\n"
"               messages go to the standard error channel. Defaults to 'info'.\n"
"\n"
"   -a IN_FRAME, --anonymous=IN_FRAME\n"
"               If the input stream has anonymous vector frames, assume they are\n"
"               in this frame.\n"
"\n"
"   -c, --coords\n" 
"               Only rotate matching coodinate vectors, ignore data vectors.\n"
"\n"
"   -d, --data  Only rotate data vectors, ignore matching coordinate vectors.\n"
"\n"
"   -k, --keep  By default, the original input vectors are not emitted on the\n"
"               output stream, but this option may be used to preserve the\n"
"               original vectors alongside the rotated items.\n"
"\n"
"   -L,--list   An information option. Just print all frames defined in the\n"
"               given meta-kernel to the standard error channel and exit.\n"
"\n"
"   -E FRAME[:AXES], --ephem=FRAME[:AXES]\n"
"               Add location data to the stream for the given SPICE frame. May\n"
"               be given more then once. See the DESCRIPTION section above for\n"
"               details.\n"
"\n"
"   -R OUT_FRAME[:IN_FRAME], --rotate=OUT_FRAME[:IN_FRAME]\n"
"               Rotate all or some input vectors to the given SPICE frame. May\n"
"               be given more then once. See the DESCRIPTION section above for\n"
"               details.\n"
"EXAMPLES\n"
"   1. Just see what frames are defined in a given metakernel:\n"
"\n"
"      das3_rotate -L my_metakernel.tm\n"
"\n"
"   2. Add IAU_JUPITER location data to Juno/Waves "
"\n"
"   3. Convert TRACERS/MAG data vectors from the any loaded coordiante system\n"
"      into the TRACERS Sun Sychronous (TSS) frame and write the results to a\n"
"      CDF file:\n"
"\n"
"      curl \"https://myserver.org/ts1/mag/bdc_roi?begin=2025-01-01&end=2025-01-02 \\\n"
"         | " PROG " tra_metakern.tm -R TSS \\\n"
"         | das3_cdf -o ./\\n"
"\n"
"AUTHOR\n"
"   chris-piker@uiowa.edu\n"
"\n"
"SEE ALSO\n"
"   * das3_cdf\n"
"   * Wiki page https://github.com/das-developers/das2C/wiki/das3_rotate\n"
"   * SPICE Frames https://naif.jpl.nasa.gov/pub/naif/toolkit_docs/C/req/frames.html\n"
"\n"
);
}

/* ************************************************************************* */
/* Argument parsing.  More details then usual */

#define SYSTEM_INVALID -1
#define SYSTEM_CAR      0  /* If theses defines change, the logic in */
#define SYSTEM_CYL      1  /* addEphemOp() needs to be updated to match */
#define SYSTEM_SPH      2

#define CAR_X      0  /* If theses defines change, the logic in */
#define CAR_Y      1  /* addEphemOp() needs to be updated to match */
#define CAR_Z      2
                      /* Coordinate names below follow ISO 31-11:1992 std */
#define CYL_RHO    0  /* Normal from axis for cylinders */
#define CYL_PHI    1  /* Angle for cylinders  */
#define CYL_Z      2  /* Along axis for cyliders */

#define SPH_R      0   /* Radial from center point for spheres */
#define SPH_THETA  1   /* Co-latitude for spheres */
#define SPH_PHI    2   /* Same usage as cylindrical, means longitude here */

#define FRAME_NAME_SZ 64

struct frame_add {
	char sFrame[FRAME_NAME_SZ]; /* If not null, output a frame */
	int iSystem;                /* Which coordinate block to output */
	bool aCoords[3][3];         /* 1st index is Vector type, 2nd is coordinate */
};

/* Assumes any components not present are 0 */
struct frame_rot {
	const char* sOutFrame; /* May not be NULL */
	const char* sInFrame;  /* May be NULL!, means try to match any */
};

typedef struct context{
	bool bListFrames;
	bool bCoordsOnly;
	bool bDataOnly;
	bool bKeepOrig;
	char aLevel[32];      /* Log level */
	char aMetaKern[256];  /* The metakernel file name */
	char aAnonFrame[32];
	struct frame_add aLocs[MAX_LOCS + 1]; /* Always terminating null struct */
	struct frame_rot aRots[MAX_ROTS + 1]; /* ditto */
	DasIO*     pOut;      /* Output IO object */
	DasStream* pSdOut;    /* Output Stream holder */
} context;


/* Parsing helpers for complicated arguments  */
/* This is meandering, try to find a shorter method -cwp */

int _addEphemOp(struct frame_add* pAdd, const char* sOp){
	char sBuf[64] = {'\0'};
	strncpy(sBuf, sOp, 63);

	char* pSep = NULL;
	char* sFrame;
	char* sCoords;
	if( (pSep = strchr(sBuf, ':')) != NULL){
		*pSep = '\0';
		sFrame = sBuf;
		sCoords = pSep + 1;
	}
	else{
		sFrame = sBuf;
		sCoords = "x,y,z";
	}

	if(sOp[0] == '\0')                /* Check frame is not null */
		goto EPHEM_PARSE_ERR;
	strncpy(pAdd->sFrame, sFrame, FRAME_NAME_SZ - 1);
	
	/* Read till hitting a separator, then store the result */
	char* pRead = sCoords;
	bool bBreakAfter = false;
	while(true){

		/* Read axis on encountering a terminator */
		if((*pRead == ',')||(*pRead == '\0')){
			if(*pRead == '\0')
				bBreakAfter = true;
			*pRead = '\0';

			/* The match and fill.  After this Z and PHI may be in the wrong place,
			 * check after the fact.*/
			if(strcasecmp(sCoords, "x") == 0)
				pAdd->aCoords[SYSTEM_CAR][CAR_X] = true;
			else if(strcasecmp(sCoords, "y") == 0)
				pAdd->aCoords[SYSTEM_CAR][CAR_Y] = true;
			else if(strcasecmp(sCoords, "z") == 0)
				pAdd->aCoords[SYSTEM_CAR][CAR_Z] = true; /* <- or cyl if other cyl stuff */
			else if(strcasecmp(sCoords, "rho") == 0)
				pAdd->aCoords[SYSTEM_CYL][CYL_RHO] = true;
			else if(strcasecmp(sCoords, "phi") == 0)
				pAdd->aCoords[SYSTEM_CYL][CYL_PHI] = true; /* <- or sphere if other sphere stuff */
			else if(strcasecmp(sCoords, "r") == 0)
				pAdd->aCoords[SYSTEM_SPH][SPH_R] = true;
			else if(strcasecmp(sCoords, "theta") == 0)
				pAdd->aCoords[SYSTEM_SPH][SPH_THETA] = true;
			else
				goto EPHEM_PARSE_ERR;

			if(bBreakAfter) break;

			++pRead;
			sCoords = pRead;
		}
		++pRead;
	}

	/* Resolve ambiguity in Z */
	if(pAdd->aCoords[SYSTEM_CAR][CAR_Z] && (
		pAdd->aCoords[SYSTEM_CYL][CYL_RHO] || pAdd->aCoords[SYSTEM_CYL][CYL_PHI]
	)){
		pAdd->aCoords[SYSTEM_CAR][CAR_Z] = false;
		pAdd->aCoords[SYSTEM_CYL][CYL_Z] = true;
	}

	/* Resolve ambiguity in Phi */
	if(pAdd->aCoords[SYSTEM_CYL][CYL_PHI] && (
		pAdd->aCoords[SYSTEM_SPH][SPH_R] || pAdd->aCoords[SYSTEM_SPH][SPH_THETA]
	)){
		pAdd->aCoords[SYSTEM_CYL][CYL_PHI] = false;
		pAdd->aCoords[SYSTEM_SPH][SPH_PHI] = true;
	}

	/* At this point, only one should ring up */
	pAdd->iSystem = SYSTEM_INVALID;
	for(int i = 0; i < 3; ++i){
		for(int j = 0; j < 3; ++j){
			if(pAdd->aCoords[i][j]){
				if(pAdd->iSystem == SYSTEM_INVALID)
					pAdd->iSystem = i;
				else
					goto EPHEM_PARSE_ERR;
			}
		}
	}

	return DAS_OKAY;

EPHEM_PARSE_ERR:
	return das_error(PERR, "Error determining requested ephemeris from '%s'", sOp);
}

/* Parsing for rotation arguments */

int _addRotateOp(struct frame_rot* pRot, char* sOp){
	
	if(sOp[0] == '\0')
		return das_error(PERR, "Missing frame name in rotation request");

	pRot->sOutFrame = sOp;

	char* pSep = NULL;
	if( (pSep = strchr(sOp, ':')) != NULL){
		*pSep = '\0';
		if(*(pSep + 1) == '\0')
			return das_error(PERR, "Input frame missing after '%s:'", sOp);
		pRot->sInFrame  = pSep + 1;
	}
	else{
		pRot->sInFrame = NULL;
	}

	return DAS_OKAY;
}

/* Main Arg parser */

int parseArgs(int argc, char** argv, context* pOpts)
{
	memset(pOpts, 0, sizeof(context));  /* <- Defaults struct values to 0 */

	strncpy(pOpts->aLevel, "info", DAS_FIELD_SZ(context,aLevel) - 1);
	int nLocs = 0;
	int nRots = 0;
	char aOpBuf[64] = {'\0'};
	int i = 0;
	int nRet = 0;
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
				pOpts->aLevel,    DAS_FIELD_SZ(context,aLevel),      argv, argc, &i, 
				"-l", "--log="
			))
				continue;
			if(dascmd_getArgVal(
				pOpts->aAnonFrame, DAS_FIELD_SZ(context,aAnonFrame), argv, argc, &i, 
				"-a", "--anonymous="
			))
				continue;
			if(dascmd_getArgVal(
				pOpts->aAnonFrame, DAS_FIELD_SZ(context,aAnonFrame), argv, argc, &i, 
				"-a", "--anonymous="
			))
				continue;

			memset(aOpBuf, 0, 64);

			if(dascmd_getArgVal(aOpBuf, 64, argv, argc, &i, "-E", "--ephem=")){
				if(nLocs >= MAX_LOCS)
					return das_error(PERR, 
						"Recompile if you want to add more then %d ephemeris sets", MAX_LOCS
					);
				if((nRet = _addEphemOp(pOpts->aLocs + nLocs, aOpBuf)) != DAS_OKAY)
					return nRet;
				++nLocs;
				continue;
			}
			if(dascmd_getArgVal(aOpBuf, 64, argv, argc, &i, "-R", "--rotate=")){
				if(nRots >= MAX_ROTS)
					return das_error(PERR, 
						"Recompile if you want more then %d rotation operations", MAX_ROTS
					);
				if((nRet = _addRotateOp(pOpts->aRots + nRots, aOpBuf) != DAS_OKAY))
					return nRet;
				++nRots;
				continue;
			}

			return das_error(PERR, "Unknown command line argument '%s'", argv[i]);
		}
		else{

			/* save the meta-kernel name */
			if(pOpts->aMetaKern[0] == '\0')
				strncpy(pOpts->aMetaKern, argv[i], 255);
			else
				return das_error(PERR, "Unknown extra fixed parameter: '%s'", argv[i]);
		} 
	} /* end arg loop */

	/* Check args */
	if(pOpts->aMetaKern[0] == '\0')
		return das_error(PERR, "Meta-kernel file was not provided");

	if((nLocs == 0)&&(nRots == 0)&&(!pOpts->bListFrames))
		return das_error(PERR, "No operations were requested, use -h for help.");
	
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

DasErrCode onStream(DasStream* pSdIn, void* pUser){
	context* pCtx = (context*)pUser;
	
	/* Make the output stream by just copying over all the top properties
	   plus any frames retained in the output */
	pCtx->pSdOut = DasStream_copy(pSdIn);

	/* Now loop over frames determining what stays */


	/* Create our new frames */


	/* Send it */
	return DasIO_writeStreamDesc(pCtx->pOut, pCtx->pSdOut); 
}


/* ************************************************************************* */

DasErrCode onDataSet(StreamDesc* pSd, int iPktId, DasDs* pDs, void* pUser)
{
	/* struct context* pCtx = (struct context*)pUser; */
	return PERR;
}

/* ************************************************************************* */

DasErrCode onData(StreamDesc* pSd, int iPktId, DasDs* pDs, void* pUser)
{
	/* struct context* pCtx = (struct context*)pUser; */
	return PERR;
}

/* ************************************************************************* */

DasErrCode onExcept(OobExcept* pExcept, void* pUser)
{
	/* struct context* pCtx = (struct context*)pUser; */

	/* If this is a no-data-in range message set the no-data flag */
	return DAS_OKAY;
}

/* ************************************************************************* */

DasErrCode onClose(StreamDesc* pSd, void* pUser)
{
	/* struct context* pCtx = (struct context*)pUser; */

	return PERR;
}
/* ************************************************************************* */

int main(int argc, char** argv) 
{
	/* Exit on errors, log info messages and above */
	das_init(argv[0], DASERR_DIS_EXIT, 0, DASLOG_INFO, NULL);


	context ctx;
	
	if(parseArgs(argc, argv, &ctx) != DAS_OKAY)
		return 13;

	daslog_setlevel(daslog_strlevel(ctx.aLevel));

	das_spice_err_setup(); /* Don't emit spice errors to stdout */

	furnsh_c(ctx.aMetaKern);
	if(failed_c()){
		das_send_spice_err(3, DAS2_EXCEPT_SERVER_ERROR);
	}

	/* Whole different path, just print stuff to stderr */
	if(ctx.bListFrames){
		prnFrames();
		return 0;
	}

	/* ... oh, they want to do actual work, okay */

	/* Input reader */
	DasIO* pIn = new_DasIO_cfile(PROG, stdin, "r");
	DasIO_model(pIn, 3);        /* <-- Read <packet>s but model <dataset>s */

	/* Output writer */
	ctx.pOut = new_DasIO_cfile(PROG, stdout, "w");

	/* Stream processor */
	StreamHandler handler;
	memset(&handler, 0, sizeof(StreamHandler));
	handler.streamDescHandler = onStream;
	handler.dsDescHandler     = onDataSet;
	handler.dsDataHandler     = onData;
	handler.exceptionHandler  = onExcept;
	handler.closeHandler      = onClose;
	handler.userData          = &ctx;

	DasIO_addProcessor(pIn, &handler);
	
	return DasIO_readAll(pIn);  /* <---- RUNS ALL PROCESSING -----<<< */
};