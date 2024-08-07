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
#define MAX_XFORMS 24

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
"      -E OUT_FRAME[,SYSTEM]\n"
"\n"
"   The FRAME is the name of any SPICE frame, either built-in, or provided by\n"
"   the meta-kernel file. To list all defined frames use the '-L' option.\n"
"\n"
"   The coordinate SYSTEM is not required. If omitted, cartesian coordinates\n"
"   are assumed. The following list of coordinate systems are supported:\n"
"\n"
"      (cart)esian      - Cartesian coords x,y,z (the default)\n"
"      (cyl)drical      - ISO 31-11 coords ρ,ϕ,z\n"
"      (sph)erical      - ISO 31-11 coords r,θ,ϕ (colatitude, north pole at 0°)\n"
"      planeto(centric) - Spherical coords, north pole at +90°, +long. to east\n"
"      planeto(detic)   - Ellipsoidal coords, same angles as planetocentric\n"
"      planeto(graphic) - Ellipsoidal coords, positive longitude to the west\n"
"\n"
"   Full names can be used, but just the portion in parenthesis is sufficent.\n"
"\n"
"   The output stream will have location values added to each packet. These\n"
"   will be defined by adding additional <coord> elements to each <dataset>.\n"
"   New <coord> elements will not have a plot-axis affinity unless \"-b\", or\n"
"   it's equivalent \"--bind-axis\" is supplied.\n"
"\n"
"   Though multilpe location systems may be added to a stream, the *order* of\n" 
"   the arguments matter. The first one will be defined as the primary \"space\"\n"
"   dimension and will recive an axis affinity, others will not.\n"
"\n"
"   Rotating Coordinate and Data Vectors\n"
"   ------------------------------------\n"
"   To rotate vectors to another SPICE frame, provide command line arguments of\n"
"   the form:\n"
"\n"
"     -R [IN_FRAME:]OUT_FRAME[,SYSTEM]\n"
"\n"
"   The IN_FRAME and colon are not required. If omitted " PROG " will attempt to\n"
"   rotate *all* vectors in the input stream to the given OUT_FRAME. Coordinate\n"
"   vectors added via " PROG " are not candidates for rotation, since this would\n"
"   be redundant.\n"
"\n"
"   The SYSTEM section defines the vector components to emit.  SYSTEM can be\n"
"   one of:\n"
"\n"
"       (cart)esian\n"
"       (cyl)indrical\n"
"       (sph)erical\n"
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
"   -p NAME[:TYPE]=VALUE, --prop NAME[:TYPE]=VALUE\n"
"               Add property NAME to the output stream header of the given TYPE\n"
"               with the given VALUE.  If TYPE is missing, it defaults to\n"
"               \"string\".  See the dasStream v3 definition document for\n"
"               details.\n"
"\n"
"   -L,--list   An information option. Just print all frames defined in the\n"
"               given meta-kernel to the standard error channel and exit.\n"
"\n"
"   -E OUT_FRAME[,SYSTEM], --ephem=OUT_FRAME[,SYSTEM]\n"
"               Add location data to the stream for the given SPICE frame. May\n"
"               be given more then once. See the DESCRIPTION section above for\n"
"               details.\n"
"\n"
"   -R [IN_FRAME:]OUT_FRAME[,SYSTEM], --rotate=[IN_FRAME:]OUT_FRAME[,SYSTEM]\n"
"               Rotate all or some input vectors to the given SPICE frame. May\n"
"               be given more then once. See the DESCRIPTION section above for\n"
"               details.\n"
"EXAMPLES\n"
"   1. Just see what frames are defined in a given metakernel:\n"
"\n"
"      " PROG " -L my_metakernel.tm\n"
"\n"
"   2. Add IAU_JUPITER radius and co-latitudes to Juno/Waves streams:\n"
"\n"
"      my_waves_das_reader | " PROG " juno_metakern.tm -E IAU_JUPITER,spherical\n"
"\n"
"   3. Convert TRACERS/MAG data vectors from the any loaded coordiante system\n"
"      into the TRACERS Sun Sychronous (TSS) frame and write the results to a\n"
"      CDF file:\n"
"\n"
"      my_tracers_das_reader | " PROG " tra_metakern.tm -R TSS | das3_cdf -o ./\n"
"\n"
"AUTHOR\n"
"   chris-piker@uiowa.edu\n"
"\n"
"SEE ALSO\n"
"   * das3_cdf\n"
"\n"
"   * das2C Wiki page: https://github.com/das-developers/das2C/wiki/das3_spice\n"
"\n"
"   * SPICE Frames Overview:\n"
"     https://naif.jpl.nasa.gov/pub/naif/toolkit_docs/Tutorials/pdf/individual_docs/17_frames_and_coordinate_systems.pdf"
"\n"
"   * SPICE Frames required reading:\n"
"     https://naif.jpl.nasa.gov/pub/naif/toolkit_docs/C/req/frames.html\n"
"\n"
);
}

/* ************************************************************************* */
/* Argument parsing.  More details then usual */

#define CAR_X      0  /* If theses defines change, the logic in */
#define CAR_Y      1  /* addEphemOp() needs to be updated to match */
#define CAR_Z      2
                      /* Coordinate names below follow ISO 31-11:1992 std */
#define CYL_RHO    0  /* Normal from axis for cylinders */
#define CYL_PHI    1  /* longitude angle */
#define CYL_Z      2  /* Along axis for cyliders */

#define SPH_R      0   /* Radial from center point for spheres */
#define SPH_THETA  1   /* latitude angle, direction changes */
#define SPH_PHI    2   /* longitude angle, direction changes */

#define XFORM_NAME_SZ  64
#define XFORM_ADD      0x01
#define XFORM_ROT      0x02
#define XFORM_VALID    0x10
#define XFORM_IN_HDR   0x20

typedef struct frame_xform {
	uint32_t uFlags;
	ubyte uOutSystem; /* See definitions in DasFrame.h */
	char sOutFrame[DASFRM_NAME_SZ];
	/* The coordinates to output.  The order is:
     x,y,z - For cartesian coords
     ρ,φ,z - For cylindrical cords
     r,θ,φ - For spherical coords */
	bool aOutCoords[3];  /* Default to all three for now */
	char sInFrame[DASFRM_NAME_SZ]; /* Only used for rotations */
} frame_xform_s;

typedef struct context{
	bool bListFrames;
	bool bCoordsOnly;
	bool bDataOnly;
	bool bKeepOrig;
	char aLevel[32];      /* Log level */
	char aMetaKern[256];  /* The metakernel file name */
	char aAnonFrame[32];
	DasIO*     pOut;      /* Output IO object */
	DasStream* pSdOut;    /* Output Stream holder */
	frame_xform_s aXForm[MAX_XFORMS + 1]; /* Always terminating null struct */
} context_s;

/* Parsing helpers for complicated arguments  */
/* This is meandering, try to find a shorter method -cwp */

#define SYSTEM_INVALID -1
#define SYSTEM_CAR      0  /* If theses defines change, the logic in */
#define SYSTEM_CYL      1  /* addEphemOp() needs to be updated to match */
#define SYSTEM_SPH      2

/* Parse strings of the form [input:]output[,system] */

int _addOp(uint32_t uOp, frame_xform_s* pXForm, const char* sOp){
	char sBuf[64] = {'\0'};
	strncpy(sBuf, sOp, 63);
	char* pRead = sBuf;
	char* pSep = NULL;
	pXForm->uFlags = (uOp & XFORM_ROT) ? XFORM_ROT : XFORM_ADD;
	
	/* Input frame if defined */
	if((pSep = strchr(pRead, ':')) != NULL){
		if( (uOp & XFORM_ROT) == 0){
			return das_error(PERR, 
				"Operation requires no input coordinates, '%s', so ':' is not needed.",
				sOp
			);
		}
		*pSep = '\0';
		if(pRead[0] == '\0') goto ADDOP_ERR;  /* nothing before the colon */
		strncpy(pXForm->sInFrame, pRead, DASFRM_NAME_SZ - 1);
		pRead = pSep + 1;
	}

	if(pRead[0] == '\0') goto ADDOP_ERR;  /* No output frame */

	/* Output coord system if defined */
	if((pSep = strchr(pRead, ',')) != NULL){
		*pSep = '\0';
		++pSep;
		if(*pSep == '\0') goto ADDOP_ERR;  /* Nothing after the comma */
		if(*pRead == '\0') goto ADDOP_ERR; /* Nothing before the comma */

		/* These are allowed outputs for both coords and rotations */
		/* NOTE: The coord systems: Polar, Surface, etc. are just other coordinte 
		         systems with some components locked to 0 */
		if(strstr(pSep, "cart")) pXForm->uOutSystem = DASFRM_CARTESIAN;
		else if (strstr(pSep, "cyl")) pXForm->uOutSystem = DASFRM_CYLINDRICAL;
		else if (strstr(pSep, "sph")) pXForm->uOutSystem = DASFRM_SPHERICAL;
		else if (strstr(pSep, "cent")) pXForm->uOutSystem = DASFRM_CENTRIC;
		else if (strstr(pSep, "detic")) pXForm->uOutSystem = DASFRM_DETIC;
		else if (strstr(pSep, "graph")) pXForm->uOutSystem = DASFRM_GRAPHIC;
		else goto ADDOP_ERR;

		/* Check for valid out coord system, not sure what rotating vectors into 
		 * an ellipsoidal system even means, would right angles not apply anymore
		 * between vector components ? */
		if((pXForm->uFlags & XFORM_ROT)&&((pXForm->uOutSystem == DASFRM_CENTRIC) ||
		   (pXForm->uOutSystem == DASFRM_DETIC) || (pXForm->uOutSystem = DASFRM_GRAPHIC)
		))
			return das_error(PERR, "Vector rotations to '%s' coordinates not supported", pSep);
	}
	else{
		pXForm->uOutSystem = DASFRM_CARTESIAN;
	}

	strncpy(pXForm->sOutFrame, pRead, DASFRM_NAME_SZ - 1);
	return DAS_OKAY;

ADDOP_ERR:
	return das_error(PERR, "Error parsing operation directive '%s'. Use -h for help.", sOp);
}


/* Main Arg parser */

int parseArgs(int argc, char** argv, context_s* pCtx)
{
	memset(pCtx, 0, sizeof(context_s));  /* <- Defaults all context values to 0 */

	strncpy(pCtx->aLevel, "info", DAS_FIELD_SZ(context_s,aLevel) - 1);
	int nXForms = 0;
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
				pCtx->bCoordsOnly = true;
				continue;
			}
			if(dascmd_isArg(argv[i], "-d", "--data", NULL)){
				pCtx->bDataOnly = true;
				continue;
			}
			if(dascmd_isArg(argv[i], "-k", "--keep", NULL)){
				pCtx->bKeepOrig = true;
				continue;
			}
			if(dascmd_isArg(argv[i], "-L", "--list", NULL)){
				pCtx->bListFrames = true;
				continue;
			}
			if(dascmd_getArgVal(
				pCtx->aLevel,    DAS_FIELD_SZ(context_s,aLevel),      argv, argc, &i, 
				"-l", "--log="
			))
				continue;
			if(dascmd_getArgVal(
				pCtx->aAnonFrame, DAS_FIELD_SZ(context_s,aAnonFrame), argv, argc, &i, 
				"-a", "--anonymous="
			))
				continue;

			memset(aOpBuf, 0, 64);

			if(dascmd_getArgVal(aOpBuf, 64, argv, argc, &i, "-E", "--ephem=")){
				if(nXForms >= MAX_XFORMS)
					return das_error(PERR, 
						"Recompile if you want to preform more than %d spice operations", MAX_XFORMS
					);
				if((nRet = _addOp(XFORM_ADD, pCtx->aXForm + nXForms, aOpBuf)) != DAS_OKAY)
					return nRet;
				++nXForms;
				continue;
			}
			if(dascmd_getArgVal(aOpBuf, 64, argv, argc, &i, "-R", "--rotate=")){
				if(nXForms >= MAX_XFORMS)
					return das_error(PERR, 
						"Recompile if you want to preform more than %d spice operations", MAX_XFORMS
					);
				if((nRet = _addOp(XFORM_ROT, pCtx->aXForm + nXForms, aOpBuf) != DAS_OKAY))
					return nRet;
				++nXForms;
				continue;
			}

			return das_error(PERR, "Unknown command line argument '%s'", argv[i]);
		}
		else{

			/* save the meta-kernel name */
			if(pCtx->aMetaKern[0] == '\0')
				strncpy(pCtx->aMetaKern, argv[i], 255);
			else
				return das_error(PERR, "Unknown extra fixed parameter: '%s'", argv[i]);
		} 
	} /* end arg loop */

	/* Check args */
	if(pCtx->aMetaKern[0] == '\0')
		return das_error(PERR, "Meta-kernel file was not provided");

	if((nXForms == 0)&&(!pCtx->bListFrames))
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
	context_s* pCtx = (context_s*)pUser;
	
	/* Make the output stream by just copying over all the top properties
	   plus any frames retained in the output */
	DasStream* pSdOut = DasStream_copy(pSdIn);
	int nRet = DAS_OKAY;

	/* Now loop over frames copying over anything that stays */
	int nFrames = DasStream_getNumFrames(pSdIn);
	bool bKeep = false;
	frame_xform_s* pXForm = NULL;

	for(int i = 0; i < nFrames; ++i){
		const DasFrame* pFrame = DasStream_getFrame(pSdIn, i);
		
		bKeep = pCtx->bKeepOrig;
		if(!bKeep){
			/* Is this one of the frames we add? Could already be defined I guess */
			for(pXForm = &(pCtx->aXForm[0]); pXForm->sOutFrame[0] != '\0'; ++pXForm){
				if(strcmp(DasFrame_getName(pFrame), pXForm->sOutFrame) == 0){
					bKeep = true;
					pXForm->uFlags |= XFORM_IN_HDR;
				}
			}
		}

		if(bKeep)
			DasStream_addFrame(pSdOut, copy_DasFrame(pFrame));
	}

	/* Create our new frames */
	for(pXForm = &(pCtx->aXForm[0]); pXForm->sOutFrame[0] != '\0'; ++pXForm){

		if(pXForm->uFlags & XFORM_IN_HDR) continue;

		int iFrame = DasStream_newFrameId(pSdOut);
		if(iFrame < 0)
			return das_error(PERR, 
				"Out of coord-frame definition space, recompile with MAX_FRAMES > %d", MAX_FRAMES
			);
		DasFrame* pNewFrame = DasStream_createFrame(
			pSdOut, iFrame, pXForm->sOutFrame, NULL, pXForm->uOutSystem
		);
		if(pNewFrame == NULL)
			return das_error(PERR, "Couldn't create frame definition for %s", pXForm->sOutFrame);

		/* Just use default directions for this app */
		if( (nRet = DasFrame_setDefDirs(pNewFrame)) != DAS_OKAY) return nRet;

		pXForm->uFlags |= XFORM_IN_HDR;
	}

	/* Send it */
	pCtx->pSdOut = pSdOut;
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

	context_s ctx; /* The "what am I doing" common block */
	
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
	ctx.pOut = new_DasIO_cfile(PROG, stdout, "w3"); /* 3 = das3 */

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