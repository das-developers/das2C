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
  
	das3_spice: Add SPICE location data and rotate vectors in SPICE frames

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
#include <assert.h>

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
"      (-I) Just list meta-kernel information\n"
"      (-L) Add spacecraft location vectors in a given frame\n"
"      (-R) Rotate vector variables into a new coordinate frame\n"
"\n"
"   The last two operations are described in successive sections below.\n"
"\n"
"   Adding Location Coordinates (Ephemerides)\n"
"   -----------------------------------------\n"
"   To add location information in a given coordinate FRAME, provide command\n"
"   line arguments of the form:\n"
"\n"
"      -L [BODY:]OUT_FRAME[,SYSTEM]\n"
"\n"
"   The BODY is the object whose location is desired.  If omitted " PROG "\n"
"   will look for the \"naifHostId\", or failing that \"instrumentHost\" in\n"
"   the stream properties. If neither of those are present, " PROG " exits\n"
"   with an error.\n"
"\n"
"   The OUT_FRAME is the name of any SPICE frame, either built-in, or provided\n"
"   by the meta-kernel file.  To list all defined frames use the '-L' option.\n"
"\n"
"   The coordinate SYSTEM is not required. If omitted, cartesian coordinates\n"
"   are assumed. The following list of coordinate systems are supported:\n"
"\n"
"      (cart)esian     - Cartesian   x, y, z (the default)\n"
"      (cyl)drical     - ISO 31-11   ρ, ϕ, z\n"
"      (sph)erical     - ISO 31-11   r, θ, ϕ  (θ = colat, North pole @ 0°)\n"
"      planeto(centric)- Spherical   r, ϕ, θ' (θ' = lat, +lon to East)\n"
"      planeto(detic)  - Ellipsoidal ϕ, θ',r' (θ' = lat, +lon to East, r' = alt)\n"
"      planeto(graphic)- Ellipsoidal ϕ, θ',r' (θ' = lat, +lon to West, r' = alt)\n"
"\n"
"   Full names can be used, but just the portion in parenthesis is sufficient.\n"
"\n"
"   The output stream will have location values added to each packet. These\n"
"   will be defined by adding additional <coord> elements to each <dataset>.\n"
/* "   New <coord> elements will not have a plot-axis affinity unless \"-b\", or\n"
"   it's equivalent \"--bind-axis\", is supplied.\n"
*/
"\n"
"   Though multiple location systems may be added to a stream, the *order* of\n" 
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
"   Rotation operations will not work for das2 streams because these do not\n"
"   have the concept of a geometric vector.  Run das2 streams through das3_vec\n"
"   first to define input vectors from sets of scalers.\n"
"\n"
"   Angle Units\n"
"   -----------\n"
"   To avoid confusion, all angles are *always* output in decimal degrees. These\n"
"   are easiest to check by eye, and doesn't involve multiple fields such as\n"
"   arc-minutes and arc-seconds."
"\n"
"RARE OPTIONS\n"
"   -s SECONDS, --shift-et=SECONDS\n"
"               Shift ephemeris times by floating point SECONDS prior to any\n"
"               SPICE function calls.  Useful for mission simulations and other\n"
"               ground test data."
"\n"
"OPTIONS\n"
"   -h, --help   Write this text to standard output and exit.\n"
"\n"
"   -l LEVEL, --log=LEVEL\n"
"               Set the logging level, where LEVEL is one of 'debug', 'info',\n"
"               'warning', 'error' in order of decreasing verbosity. All log\n"
"               messages go to the standard error channel. Defaults to 'info'.\n"
"\n"
"   -a IN_FRAME, --anon-frame=IN_FRAME\n"
"               If the input stream has anonymous vector frames, assume they are\n"
"               in this frame.\n"
"\n"
"   -b MB, --buffer=MB\n"
"               Normally " PROG " writes one output packet for each input\n"
"               packet. For better performance, use this option to batch process\n"
"               up to MB megabytes of data before each write. The special values\n"
"               'inf', 'infinite' or '∞' can be used to only write packets after\n"
"               the input stream completes.\n"
"\n"
"   -c, --coords\n" 
"               Only rotate matching coordinate vectors, ignore data vectors.\n"
"\n"
"   -d, --data  Only rotate data vectors, ignore matching coordinate vectors.\n"
"\n"
"   -k, --keep  By default, the original input vectors are not emitted on the\n"
"               output stream, but this option may be used to preserve the\n"
"               original vectors alongside the rotated items.\n"
"\n"
"   -p [TYPE:]NAME=VALUE, --prop [TYPE:]NAME=VALUE\n"
"               Add property NAME to the output stream header of the given TYPE\n"
"               with the given VALUE.  If TYPE is missing, it defaults to\n"
"               \"string\".  See the dasStream 3.0 definition document for\n"
"               details.\n"
"\n"
"   -I, --info  An information option. Just print all frames defined in the\n"
"               given meta-kernel to the standard error channel and exit.\n"
"\n"
"   -L [BODY:]OUT_FRAME[,SYSTEM], --locate=BODY:OUT_FRAME[,SYSTEM]\n"
"               Add location data to the stream for the given BODY in the\n"
"               given SPICE frame. BODY may be an integer SPICE body ID code\n"
"               or a text string, and is usually a spacecraft name such as\n"
"               Cassini.  The option may be given more then once. Each instance\n"
"               adds a new coordinate vector variable to the stream.  See the\n"
"               DESCRIPTION section above for details.\n"
"\n"
"   -R [IN_FRAME:]OUT_FRAME[,SYSTEM], --rotate=[IN_FRAME:]OUT_FRAME[,SYSTEM]\n"
"               Rotate all or some input vectors to the given SPICE frame. May\n"
"               be given more then once. See the DESCRIPTION section above for\n"
"               details.\n"
"\n"
"EXAMPLES\n"
"   1. Just see what frames are defined in a given metakernel:\n"
"\n"
"      " PROG " -I my_metakernel.tm\n"
"\n"
"   2. Add IAU_JUPITER planetocentric coordinates to Juno/Waves streams:\n"
"\n"
"      das_reader | " PROG " juno_metakern.tm -L JUNO:IAU_JUPITER,centric\n"
"\n"
"   3. Convert TRACERS/MAG data vectors from the any loaded coordiante system\n"
"      into the TRACERS Sun Sychronous (TSS) frame and write the results to a\n"
"      CDF file:\n"
"\n"
"      das_reader | " PROG " tra_metakern.tm -R TSS | das3_cdf -o ./\n"
"\n"
"AUTHOR\n"
"   chris-piker@uiowa.edu\n"
"\n"
"SEE ALSO\n"
"   das3_vec, das3_cdf\n"
"   das2C Wiki page: https://github.com/das-developers/das2C/wiki/das3_spice\n"
"   SPICE Frames Overview:\n"
"      https://naif.jpl.nasa.gov/pub/naif/toolkit_docs/Tutorials/pdf/individual_docs/17_frames_and_coordinate_systems.pdf"
"   SPICE Frames required reading:\n"
"      https://naif.jpl.nasa.gov/pub/naif/toolkit_docs/C/req/frames.html\n"
"\n"
);
}

/* ************************************************************************* */
/* Pointer Map:  "O" = owns, "R" = reference, "P" = Just Points

  pIn (DasIO) - Input generator, triggers callbacks.
  |	
  +-P-> pCtx (Context [1]) - "Global" program options, on stack
  |     |                                                  
  |     +-P-> pOut (DasIO) Output generator, Owns NOTHING! 
  |     |                                                  
  |     +-O-> pSdOut (DasStream) - Output Stream
  |
  +-O-> pSdIn (DasStream) - Input stream
		  |
		  +-O-> pDsIn (DasDs) - Input dataset
				  |
			 [2] +-R-> pAry (DasAry [2]) - Input array <----+---+  <---+
				  |                                          |   |      |
				  +-O-> pCodec (DasCodec) - Input decoder -R-+   |      |
				  |                                              |      |
				  +-O-> pDim (DasDim) - Input Var Group          |      |
				  |     |                                        |      |
				  |     +-O-> pVar (DasVar) - Input Variable -R--+  <---|----+
				  |                                                     |    |
				  |                                                     |    |
				  +-P*-> pDsOut (DasDs) - Output Dataset (*via pUser)   |    |
					  |                                                  |    |
					  |   +- pAry (pass through [3]) --------------------+    |
					  |   |                                                   |
					  +-R-+                                                   |
					  |   |                                                   |
					  |   +- pAry (DasAry [2]) - Input array <----+---+       |
					  |                                           |   |       |
				 [4] +-O--> pCodec (DasCodec) - Input decoder -R-+   |       |
					  |                                               |       |
					  +-O--> pDim (DasDim) - Input Var Group          |       |
					  |      |                                        |       |
					  |      +-O-> pVar (DasVar) - Input Variable -R--+  <----+  
					  |                                                       |
					  +-O-> pCalcs -R-----------------------------------------+

Notes:
  1. pCtx is connected to pIn via an owned stream processor object not shown.

  2. DasAry objects are reference counted. Nobody owns them exclusively and 
	  memory clean runs up when the last reference disappears.

  3. No memcpy is needed to move input values to output for items that are
	  just copied through.  The upstream array is reference directly.

  4. Output datasets own a separate set of codecs, this way the output format
	  can be different from the input without duplicating the backing arrays.

																			... end pointer map */
/* ************************************************************************* */

/* ************************************************************************* */
/* Context object to track processsing state.  Initially there is only one
 * instance of this, but for each dataset encountered the original is duplicated
 */

#define XFORM_NAME_SZ  64
#define XFORM_LOC      0x01
#define XFORM_ROT      0x02
#define XFORM_VALID    0x10
#define XFORM_IN_HDR   0x20 /* Set if frame for xform out in stream header */

/* Owned by App Context.  Generated at Command Line parsing */
typedef struct xform_request {
	uint32_t uFlags;
	char aBody[DASFRM_NAME_SZ];     /* Usually a spacecraft name, but could be a moon etc.*/
	SpiceInt nBodyId;               /* The body's (usually spacecraft's) spice ID */
	char aInFrame[DASFRM_NAME_SZ];  /* Only used for rotations */
	char aOutFrame[DASFRM_NAME_SZ]; /* Explict name such as IAU_JUPITER */
	SpiceInt nOutCenter;            /* Spice ID code for the central body of the out frame */
	char aOutCenter[DASFRM_NAME_SZ];/* Name for central body of the output frame */
	ubyte uOutSystem;               /* See definitions in DasFrame.h */
	ubyte uOutDasId;                /* ID used with dasStream to link vectors & frames */
	
	/* The coordinates to output.  The order is:
	  x,y,z - For cartesian coords
	  ρ,φ,z - For cylindrical cords
	  r,θ,φ - For spherical coords */
	bool aOutCoords[3];            /* Default to all three for now */

} XReq;

/* Owned by output DasVar structs. Generated on new Dataset Definition */
typedef struct xform_calc {
	XReq request;     /* What they want done */
	DasVar* pTime;    /* The input time source for calculations */
	DasVar* pVarIn;   /* The input vector source for rotations  */
	DasVar* pVarOut;  /* The output destination                 */

} XCalc;

#define ANON_FRAME_SZ 32

typedef struct context{
	bool bListFrames;
	
	/* Conversion flags */
	bool bCoordsOnly;     /* Only convert/add coordinate items */
	bool bDataOnly;       /* Only convert data items */
	bool bKeepOrig;       /* Keep original vectiors in datasets */
	bool bHasMatchAny;    /* We're trying to match any in-frame */
	bool bWantsLocs;      /* We want to output at least one location */

	char aLevel[32];      /* Log level */
	char aMetaKern[256];  /* The metakernel file name */
	char aAnonFrame[ANON_FRAME_SZ];  /* Unnamed frames are assumed to be this one */
	ubyte uAnonDasId;      /* Id assigned in the output stream for the anon frame */
	SpiceInt nAnonCenter; /* Center body ID for the anonymous frame */
	char aAnonCenter[DASFRM_NAME_SZ]; /* Center body name for the anonymous frame */
	size_t uFlushSz;

	double rEphemShift;   /* Lesser used option */

	DasIO*     pOut;      /* Output IO object */
	DasStream* pSdOut;    /* Output Stream holder */
	int nXReq;
	XReq aXReq[MAX_XFORMS + 1]; /* Always terminating null struct */
} Context;

/* Remove?

/ * Define purpose of coord indexes for readability * /
#define CAR_X      0 
#define CAR_Y      1 
#define CAR_Z      2
							 / * Coordinate names below follow ISO 31-11:1992 std * /
#define CYL_RHO    0  / * Normal from axis for cylinders * /
#define CYL_PHI    1  / * longitude angle * /
#define CYL_Z      2  / * Along axis for cyliders * /

#define SPH_R      0   / * Radial from center point for spheres * /
#define SPH_THETA  1   / * latitude angle, direction changes * /
#define SPH_PHI    2   / * longitude angle, direction changes * /
*/

char g_sShortErr[42] = {'\0'};
char g_sLongErr[42] = {'\0'};

#define CHECK_SPICE if(failed_c()) { \
	return das_error(PERR, "%s", das_get_spice_error()); \
}

/* ************************************************************************* */
/* Argument parsing.  More details then usual */

/* Parse strings of the form [input:]output[,system] */

int _addOp(uint32_t uOp, XReq* pReq, const char* sOp){
	char sBuf[64] = {'\0'};
	strncpy(sBuf, sOp, 63);
	char* pRead = sBuf;
	char* pSep = NULL;
	pReq->uFlags = (uOp & XFORM_ROT) ? XFORM_ROT : XFORM_LOC;

	/* Input frame if defined */
	if((pSep = strchr(pRead, ':')) != NULL){
		*pSep = '\0';
		if(pRead[0] == '\0') goto ADDOP_ERR;  /* nothing before the colon */

		if(uOp & XFORM_LOC){
			strncpy(pReq->aBody, pRead, DASFRM_NAME_SZ - 1);	
		}
		else{
			strncpy(pReq->aInFrame, pRead, DASFRM_NAME_SZ - 1);
		}
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
		if(strstr(pSep, "cart")) pReq->uOutSystem = DAS_VSYS_CART;
		else if (strstr(pSep, "cyl")) pReq->uOutSystem = DAS_VSYS_CYL;
		else if (strstr(pSep, "sph")) pReq->uOutSystem = DAS_VSYS_SPH;
		else if (strstr(pSep, "cent")) pReq->uOutSystem = DAS_VSYS_CENTRIC;
		else if (strstr(pSep, "detic")) pReq->uOutSystem = DAS_VSYS_DETIC;
		else if (strstr(pSep, "graph")) pReq->uOutSystem = DAS_VSYS_GRAPHIC;
		else goto ADDOP_ERR;

		/* Check for valid out coord system, not sure what rotating vectors into 
		 * an ellipsoidal system even means, would right angles not apply anymore
		 * between vector components ? */
		if((pReq->uFlags & XFORM_ROT)&&
			((pReq->uOutSystem == DAS_VSYS_DETIC) || (pReq->uOutSystem = DAS_VSYS_GRAPHIC))
		)
			return das_error(PERR, "Vector rotations to '%s' non-orthonormal coordinates not supported", pSep);
	}
	else{
		pReq->uOutSystem = DAS_VSYS_CART;
	}

	strncpy(pReq->aOutFrame, pRead, DASFRM_NAME_SZ - 1);
	return DAS_OKAY;

ADDOP_ERR:
	return das_error(PERR, "Error parsing operation directive '%s'. Use -h for help.", sOp);
}


/* Main Arg parser */

int parseArgs(int argc, char** argv, Context* pCtx)
{
	memset(pCtx, 0, sizeof(Context));  /* <- Defaults all context values to 0 */

	char sMemThresh[32] = {'\0'};

	strncpy(pCtx->aLevel, "info", DAS_FIELD_SZ(Context,aLevel) - 1);
	pCtx->nXReq = 0;
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
			if(dascmd_isArg(argv[i], "-I", "--info", NULL)){
				pCtx->bListFrames = true;
				continue;
			}
			if(dascmd_getArgVal(
				sMemThresh,  32, argv, argc, &i, "-b", "--buffer="
			))
				continue;
			if(dascmd_getArgVal(
				pCtx->aLevel, DAS_FIELD_SZ(Context,aLevel), argv, argc, &i, "-l", "--log="
			))
				continue;
			if(dascmd_getArgVal(
				pCtx->aAnonFrame, DAS_FIELD_SZ(Context,aAnonFrame), argv, argc, &i, "-a", "--anon-frame"
			))
				continue;

			memset(aOpBuf, 0, 64);

			if(dascmd_getArgVal(aOpBuf, 64, argv, argc, &i, "-L", "--locate=")){
				if(pCtx->nXReq >= MAX_XFORMS)
					return das_error(PERR, 
						"Recompile if you want to preform more than %d spice operations", MAX_XFORMS
					);
				if((nRet = _addOp(XFORM_LOC, pCtx->aXReq + pCtx->nXReq, aOpBuf)) != DAS_OKAY)
					return nRet;
				++(pCtx->nXReq);
				pCtx->bWantsLocs = true;
				continue;
			}
			if(dascmd_getArgVal(aOpBuf, 64, argv, argc, &i, "-R", "--rotate=")){
				if(pCtx->nXReq >= MAX_XFORMS)
					return das_error(PERR, 
						"Recompile if you want to preform more than %d spice operations", MAX_XFORMS
					);
				if((nRet = _addOp(XFORM_ROT, pCtx->aXReq + pCtx->nXReq, aOpBuf) != DAS_OKAY))
					return nRet;

				/* See if I have a "match any" */
				if(pCtx->aXReq[pCtx->nXReq].aInFrame[0] == '\0')
					pCtx->bHasMatchAny = true;
				++(pCtx->nXReq);
				continue;
			}
			if(dascmd_getArgVal(aOpBuf, 64, argv, argc, &i, "-s", "--shift-et=")){
				if(sscanf(aOpBuf, "%lf", &(pCtx->rEphemShift)) != 1){
					return das_error(PERR, 
						"Error converting %s to a floating point seconds time.", aOpBuf
					);
				}
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

	if((pCtx->nXReq == 0)&&(!pCtx->bListFrames))
		return das_error(PERR, "No operations were requested, use -h for help.");

	/* Convert the memory threshold if given */
	float fMemUse;
	if(sMemThresh[0] != '\0'){
		if((strncmp(sMemThresh, "inf", 3)==0)||(strcmp(sMemThresh, "∞") == 0)){
			pCtx->uFlushSz = (sizeof(size_t) == 4 ? 0xFFFFFFFF : 0xFFFFFFFFFFFFFFuLL);
		}
		else{
			if((sscanf(sMemThresh, "%f", &fMemUse) != 1)||(fMemUse < 1))
				return das_error(PERR, "Invalid memory usage argument, '%s' MB", sMemThresh);
			else
				pCtx->uFlushSz = ((size_t)fMemUse) * 1048576ull ;
		}
	}
	
	return DAS_OKAY;
}

/* ************************************************************************* */
/* Get body centers for frames */

DasErrCode addSpiceIDs(Context* pCtx)
{
	SpiceInt nBodyId;     /* Typically a spacecraft name, could be ground station 
									 or even a moon. */
	SpiceInt nFrameId;    /* Global ID for the frame */
	SpiceInt nCentId;     /* Id of the central body for the frame */
	SpiceInt nFrmTypeId;  /* The type of frame */
	SpiceInt nFrmClassId; /* ID for the frame within it's class */
	SpiceBoolean bFound;  /* SPICETRUE if it was found */

	for(XReq* pReq = pCtx->aXReq; pReq->aOutFrame[0] != '\0'; ++pReq){

		if((pReq->uFlags & XFORM_LOC) == 0)
			continue;

		if(pReq->aBody[0] == '\0')
			continue;

		/* Get spacecraft or other orbiting thing's spice ID */
		bods2c_c(pReq->aBody, &nBodyId, &bFound);
		CHECK_SPICE
		if(bFound){
			pReq->nBodyId = nBodyId;
			daslog_debug_v("Body '%s' recognized as NAIF ID %d.", pReq->aBody, pReq->nBodyId);
		}
		else
			return das_error(PERR, "Body '%s' not recognized by spice.\n" 
				"HINT:  You may need to specify it as a SPICE ID code or via it's "
				"abbreviation instead of by name.", pReq->aBody
			);

		namfrm_c(pReq->aOutFrame, &nFrameId);
		if(nFrameId == 0){
			return das_error(PERR, 
				"Cannot get frame ID, insufficent data for frame %s",
				pReq->aOutFrame
			);
		}

		frinfo_c(nFrameId, &nCentId, &nFrmTypeId, &nFrmClassId, &bFound);
		if(! bFound)
			return das_error(PERR, 
				"Cannot get central body, insufficent data for frame %s",
				pReq->aOutFrame
			);
		
		pReq->nOutCenter = nCentId;
		bodc2n_c(nCentId, DASFRM_NAME_SZ -1, pReq->aOutCenter, &bFound);
		if(! bFound)
			return das_error(PERR,
				"Cannot get central body name for frame %s", pReq->aOutFrame
			);
	}

	/* if we're defining a frame for anonymous vectors get it's info */
	if(pCtx->aAnonFrame[0] != '\0'){
		namfrm_c(pCtx->aAnonFrame, &nFrameId);
		if(nFrameId == 0)
			return das_error(PERR, "Cannot get frame ID, insufficent data for frame %s", pCtx->aAnonFrame);
		
		frinfo_c(nFrameId, &nCentId, &nFrmTypeId, &nFrmClassId, &bFound);
		if(!bFound)
			return das_error(PERR, "Cannot get central body, insufficent data for frame %s", pCtx->aAnonFrame);
		pCtx->nAnonCenter = nCentId;
		
		bodc2n_c(nCentId, DASFRM_NAME_SZ -1, pCtx->aAnonCenter, &bFound);
		if(!bFound)
			return das_error(PERR, "Cannot get central body name for frame %s", pCtx->aAnonFrame);
	}

	return DAS_OKAY;
}

/* ************************************************************************* */

DasErrCode onStream(DasStream* pSdIn, void* pUser){
	Context* pCtx = (Context*)pUser;
	
	/* Make the output stream by just copying over all the top properties
		plus any frames retained in the output */
	DasStream* pSdOut = DasStream_copy(pSdIn);

	/* Now loop over frames copying over anything that stays */
	int nFrames = DasStream_getNumFrames(pSdIn);
	bool bKeep = false;
	XReq* pReq = NULL;

	for(int i = 0; i < nFrames; ++i){
		const DasFrame* pFrame = DasStream_getFrame(pSdIn, i);
		
		bKeep = pCtx->bKeepOrig;
		if(!bKeep){
			/* Is this one of the frames we add? Could already be defined I guess */
			for(pReq = &(pCtx->aXReq[0]); pReq->aOutFrame[0] != '\0'; ++pReq){
				if(strcmp(DasFrame_getName(pFrame), pReq->aOutFrame) == 0){
					bKeep = true;
					pReq->uFlags |= XFORM_IN_HDR;
				}
			}
		}

		/* if(bKeep) */
		/* Always carry over the frame definitions, even if they aren't used */
		DasStream_addFrame(pSdOut, copy_DasFrame(pFrame));
	}

	/* Create our new frames */
	for(pReq = &(pCtx->aXReq[0]); pReq->aOutFrame[0] != '\0'; ++pReq){

		if(pReq->uFlags & XFORM_IN_HDR) continue;

		int iFrame = DasStream_newFrameId(pSdOut);
		if(iFrame < 0)
			return das_error(PERR, 
				"Out of coord-frame definition space, recompile with MAX_FRAMES > %d", MAX_FRAMES
			);
		pReq->uOutDasId = (ubyte)iFrame;

		DasFrame* pNewFrame = DasStream_createFrame(
			pSdOut, pReq->uOutDasId, pReq->aOutFrame, pReq->aOutCenter
		);
		if(pNewFrame == NULL)
			return das_error(PERR, "Couldn't create frame definition for %s", pReq->aOutFrame);

		pReq->uFlags |= XFORM_IN_HDR;
	}

	/* ... and the anonymous input frame */
	if(pCtx->aAnonFrame[0] != '\0'){
		const DasFrame* pConstFrame = DasStream_getFrameByName(pSdOut, pCtx->aAnonFrame);
		if(pConstFrame == NULL){
			int nAnonDasId = DasStream_newFrameId(pSdOut);
			if(nAnonDasId < 1)
				return (nAnonDasId * -1);

			pCtx->uAnonDasId = (ubyte)nAnonDasId;

			/* Get spice information for the anonymous frame */

			DasFrame* pNewFrame = DasStream_createFrame( /* assume cartesian for now */
				pSdOut, pCtx->uAnonDasId, pCtx->aAnonFrame, pCtx->aAnonCenter
			);
			if(pNewFrame == NULL)
				return das_error(PERR, "Couldn't create frame definition for %s", pCtx->aAnonFrame);
		}
	}

	/* Pick up the name of the instrument host while we are here */
	SpiceInt nBodyId = 0;
	SpiceBoolean bFound = SPICEFALSE;
	const char* sHost = DasDesc_get((DasDesc*)pSdIn, "naifHostId");
	if(sHost == NULL)
		sHost = DasDesc_get((DasDesc*)pSdIn, "instrumentHost");
	
	if(sHost != NULL)
		bods2c_c(sHost, &nBodyId, &bFound);
	
	for(XReq* pReq = pCtx->aXReq; pReq->aOutFrame[0] != '\0'; ++pReq){

		if((pReq->uFlags & XFORM_LOC) == 0)
			continue;

		if(pReq->aBody[0] == '\0'){
			/* Didn't find an instrument host name in the stream header if no
				object-in-need-of-location-data was mentioned on the command line
				the calculation will fail */
			if(bFound == SPICETRUE){
				strncpy(pReq->aBody, sHost, DASFRM_NAME_SZ - 1);
				pReq->nBodyId = nBodyId;
			}
			else{
				return das_error(PERR, "No target body name found for %s locations "
					"in the stream header and none specified on the command line "
					"either.  Use -h for help.", pReq->aOutFrame
				);
			}
		}
	}

	/* Save off metakernel and add in the time shift if there is one */
	DasDesc_setStr((DasDesc*)pSdOut, "meta_kernel", pCtx->aMetaKern);
	if(pCtx->rEphemShift != 0)
		DasDesc_setDouble((DasDesc*)pSdOut, "ephem_time_shift", pCtx->rEphemShift);

	/* Send it */
	pCtx->pSdOut = pSdOut;
	return DasIO_writeDesc(pCtx->pOut, (DasDesc*)pCtx->pSdOut, 0);
}


/* ************************************************************************* */
/* Header Generation */

const ubyte aStdDirs[3] = {0,1,2};  /* Use std dirs for components */

/* Does this rotational transform affect this input dimension? */
bool _matchRotDim(const DasDim* pDim, const XReq* pReq, const char* sAnonFrame)
{
	size_t uVars = DasDim_numVars(pDim);
	const char* sFrame = NULL;
	const DasVar* pVar = NULL;
	for(size_t uV = 0; uV < uVars; ++uV){
		pVar = DasDim_getVarByIdx(pDim, uV);
		if(DasVar_valType(pVar) != vtGeoVec) continue;

		if(pReq->aInFrame[0] == '\0')
			return true;

		/* If I have a default frame, treat no-name frames as this one */
		sFrame = DasVar_getFrameName(pVar);
		if((sFrame == NULL)&&(sAnonFrame != NULL))
			sFrame = sAnonFrame;

		if(strcasecmp(sFrame, pReq->aInFrame) == 0)
			return true;
	}

	return false;
}

const ubyte g_uStdDirs = VEC_DIRS3(0,1,2); /* encodes 3 small integers in a byte */
#ifdef HOST_IS_LSB_FIRST
const char* g_sFloatEnc = "LEreal";
#else
const char* g_sFloatEnc = "BEreal";
#endif


/* Add record dependent location vectors to the output dataset */

DasErrCode _addLocation(XCalc* pCalc, DasDs* pDsOut, const char* sAnnoteAxis)
{
	int nDsRank = DasDs_rank(pDsOut);
	const XReq* pReq = &(pCalc->request);

	/* The new storage array */
	char sId[64] = {'\0'};
	snprintf(sId, 63, "loc_%s", pReq->aOutFrame);
	DasAry* pAryOut = new_DasAry(sId, vtFloat, 0, NULL, RANK_2(0,3), UNIT_KM);
	DasDs_addAry(pDsOut, pAryOut);

	/* The new codec for output */
	DasDs_addFixedCodec(
		pDsOut, DasAry_id(pAryOut), "real", g_sFloatEnc, das_vt_size(vtFloat), 3,
		DASENC_WRITE
	);
		
	/* The new variable to interface to the array */
	int8_t aVarMap[DASIDX_MAX] = DASIDX_INIT_UNUSED;
	aVarMap[0] = 0;

	DasVar* pVarOut = new_DasVarVecAry(
		pAryOut,              /* Vectors are backed by this new array */
		nDsRank,              /* Our external rank is the same the dataset */
		aVarMap,              /* We depend only on the first dataset index */
		1,                    /* But, we have one internal index */
		pReq->uOutDasId,      /* We are associated with this reference frame */
		pReq->uOutSystem,     /* and it uses this coordinate system */
		3,                    /* we encode 3 components of this system */
		g_uStdDirs            /* and they are in the standard order */
	);

	pCalc->pVarOut = pVarOut;  /* State where data will go */

	DasDim* pDimOut = new_DasDim("location", sId, DASDIM_COORD, nDsRank);
	DasDim_setFrame(pDimOut, pReq->aOutFrame);
	DasDim_addVar(pDimOut, DASVAR_CENTER, pVarOut);
	DasDim_setAxis(pDimOut, 0, sAnnoteAxis);
	DasDim_primeCoord(pDimOut, false); /* It's just an annotation */

	return DasDs_addDim(pDsOut, pDimOut);
}

/* Add record dependent, or record independent rotation vector variable */

DasErrCode _addRotation(XCalc* pCalc, const char* sAnonFrame, DasDs* pDsOut)
{
	ptrdiff_t aDsShape[DASIDX_MAX] = DASIDX_INIT_UNUSED;
	int nDsRank = DasDs_shape(pDsOut, aDsShape);
	XReq* pReq = &(pCalc->request);

	/* If this rotation has no specified input frame, then set our frame as
	   the input frame.  If we don't have one, fall back to the anonymous
	   frame, if specified */
	if(pReq->aInFrame[0] == '\0'){
		
		const char* sFrame = DasVar_getFrameName(pCalc->pVarIn);
		if(sFrame == NULL){
			if((sAnonFrame == NULL)||(sAnonFrame[0] == '\0')){
				return das_error(PERR, 
					"Can not add rotation operation, input vector has no frame "
					"and no anonymous frame is set.  Use -h for help."
				);
			}
			else{
				sFrame = sAnonFrame;
			}
		}
		strncpy(pReq->aInFrame, sFrame, DASFRM_NAME_SZ - 1);
	}

	/* The shape the storage array is just the same as the input, with all
		unused indexes collapsed.  A three-way compare is used in order to
		support function variables, which take the same shape as thier 
		container */
	ptrdiff_t aVarShape[DASIDX_MAX] = DASIDX_INIT_UNUSED;
	DasVar_shape(pCalc->pVarIn, aVarShape);

	int nAryRank = 0;
	size_t aAryShape[DASIDX_MAX] = {0};
	int i,j;
	int nItems = 1;
	int8_t aVarMap[DASIDX_MAX] = DASIDX_INIT_UNUSED;
	for(i = 0, j = 0; i < nDsRank; ++i){
		if(!DasVar_degenerate(pCalc->pVarIn, i)) aVarMap[i] = (int8_t)i;

		/* Even if upstream doesn't use the record index, pTime does, so we do too */
		if((i > 0) && (aVarMap[i] == DASIDX_UNUSED)) continue;

		aAryShape[j] = (aDsShape[i] == DASIDX_RAGGED) ? 0 : aDsShape[i];

		/* The items per record are not affected by the record index */
		if(i > 0) nItems *= aAryShape[j];

		++nAryRank;
	}
	aAryShape[nAryRank] = 3;  /* Add 1 internal dimension */
	nItems *= 3;
	++nAryRank;
	if(nItems < 0) nItems = -1; /* if any dim ragged, just make codec ragged */

	char sId[64] = {'\0'};
	const DasDim* pDimIn = (const DasDim*)DasDesc_parent((const DasDesc*)(pCalc->pVarIn));
	const char* sDimIn = DasDim_dim(pDimIn);

	/* If the input array is a double, move up to doubles for the output */
	const DasAry* pAryIn = DasVar_getArray(pCalc->pVarIn);
	das_val_type vtElOut = ((pAryIn != NULL)&&(DasAry_valType(pAryIn) == vtDouble)) ? 
		vtDouble : vtFloat;

	snprintf(sId, 63, "%s_%s", sDimIn, pReq->aOutFrame);
	DasAry* pAryOut = new_DasAry(
		sId, vtDouble, 0, NULL, nAryRank, aAryShape, DasVar_units(pCalc->pVarIn)
	);
	DasDs_addAry(pDsOut, pAryOut);

	/* Now add a codec for this array, assumes time is record varying */
	if(nItems > 0){
		DasDs_addFixedCodec(
			pDsOut, DasAry_id(pAryOut), "real", g_sFloatEnc, das_vt_size(vtElOut), nItems,
			DASENC_WRITE
		);
	}
	else{
		/* DAS_FLOAT_SEP : contains binary patterns almost never seen in floating
			point serialization.  See codec.c */
		DasDs_addRaggedCodec(
			pDsOut, DasAry_id(pAryOut), "real", g_sFloatEnc, das_vt_size(vtElOut), 
			nAryRank, das_vt_size(vtElOut), &(DAS_FLOAT_SEP[0][0]), DASENC_WRITE
		);
	}
	
	/* The new variable to provide access to the array. */ 
	aVarMap[0] = 0;   /* <-- Even if upstream isn't record varying, we are */

	DasVar* pVarOut = new_DasVarVecAry(
		pAryOut,              /* Vectors are backed by this new array */
		nDsRank,              /* Our external rank is the same the dataset */
		aVarMap,              /* We have the same index mapping as the upstream var */
		1,                    /* and we also have 1 internal index */
		pReq->uOutDasId,      /* We are associated with this coordinate frame */
		pReq->uOutSystem,     /* and it uses this coordinate system */
		3,                    /* we encode 3 components of this system */
		g_uStdDirs            /* and they are in the standard order */
	);

	pCalc->pVarOut = pVarOut;  /* attach the output location */

	/* We will have the same basic property as upstream */

	DasDim* pDimOut = new_DasDim(sDimIn, sId, DasDim_type(pDimIn), nDsRank);
	DasDim_setFrame(pDimOut, pReq->aOutFrame);
	DasDim_addVar(pDimOut, DASVAR_CENTER, pVarOut);

	/* Copy over the properties, and change a few */
	DasDesc_copyIn((DasDesc*)pDimOut, (const DasDesc*)pDimIn);
	DasDesc_setStr((DasDesc*)pDimOut, "frame", pReq->aOutFrame);
	char sBuf[128] = {'\0'};
	snprintf(sBuf, 127, "%s values rotated into %s", DasDim_id(pDimIn), pReq->aOutFrame);
	DasDesc_setStr((DasDesc*)pDimOut, "summary", sBuf);

	return DasDs_addDim(pDsOut, pDimOut);
}

/* Check to see if this input dim alone will provide rotation data
 *
 *  Match requirements:
 *
 *  3. Dim has a vector frame name
 *  4. rotateany is set, or frame matchs one of the requested rotations
 */
bool _isSufficentRotSrc(const Context* pCtx, DasDim* pDim)
{
	/* 1. Not sole source of components if not a vector variable */
	DasVar* pVar = DasDim_getPointVar(pDim);
	if(pVar == NULL) 
		return false;

	if(DasVar_valType(pVar) != vtGeoVec)
		return false;

	/* 2. Var is not source of rotations on this dim type are blocked by user */
	if(pCtx->bDataOnly && (DasDim_type(pDim) == DASDIM_COORD)) return false;
	if(pCtx->bCoordsOnly && (DasDim_type(pDim) == DASDIM_DATA)) return false;

	/* 3. Var is not a source of rotations if we can't figure out the vector frame */
	const char* sFrame = DasVar_getFrameName(pVar);
	if(sFrame == NULL){ 
		if(pCtx->aAnonFrame[0] == '\0')
			return false; /* No default frame name either */
		else
			sFrame = pCtx->aAnonFrame;
	}

	/* 4. If "rotate any" our input frame name doesn't have to match cmd args */
	if(pCtx->bHasMatchAny)
		return true;

	/* 5. Okay, not "rotate any". Does our frame name mach any input? */
	for(size_t iReq = 0; iReq < pCtx->nXReq; ++iReq){	
		if(strcasecmp(sFrame, pCtx->aXReq[iReq].aInFrame) == 0){
			return true;
		}
	}
	return false; 
}

/* TODO: This is dumb, the encoder shouldn't make unknown frames, the 
         whole concept is useless, *but* the CDF program might need them
         and I'm in a crunch.

         After the meeting update dataset_hdr3.c to not generate anonymous
         frames */

bool _hadAnonFrame(DasVar* pVar){
	if( DasVar_valType(pVar) != vtGeoVec) return false;
	int nFrameId = DasVar_getFrame(pVar);
	if( nFrameId == 0) return true;

	DasStream* pSd = (DasStream*) ((DasDesc*)pVar)->parent->parent->parent;

	const DasFrame* pFrame = DasStream_getFrameById(pSd, nFrameId);

	if(strcmp(DasFrame_getName(pFrame), DASFRM_NULLNAME) == 0)
		return true;

	return false;
}


/* For each new upstream dataset, define a downstream dataset */

DasErrCode onDataSet(DasStream* pSdIn, int iPktId, DasDs* pDsIn, void* pUser)
{
	Context* pCtx = (struct context*)pUser;
	DasStream* pSdOut = pCtx->pSdOut;

	ptrdiff_t aDsShape[DASIDX_MAX] = DASIDX_INIT_UNUSED;   /* see if room for vectors */
	int nRank = DasDs_shape(pDsIn, aDsShape);
	if(nRank == DASIDX_MAX){
		return das_error(PERR, "Can't add vectors to rank %d datasets. No index "
			"slots are left over for the internal vector index.", DASIDX_MAX
		);
	}

	DasDs* pDsOut = new_DasDs(DasDs_id(pDsIn), DasDs_group(pDsIn), DasDs_rank(pDsIn));
	DasDesc_copyIn((DasDesc*)pDsOut, (DasDesc*)pDsIn);
	DasStream_addDesc(pSdOut, (DasDesc*)pDsOut, iPktId);

	pDsIn->pUser = pDsOut;  /* Attach upstream DS so we can find it easily */

	XCalc* pCalcs = (XCalc*) calloc(MAX_XFORMS+1, sizeof(XCalc));
	pDsOut->pUser = pCalcs;
	int nCalcs = 0;

	int nRet = DAS_OKAY;

	/* TODO: Handle header only conversions that aren't a function of record index
	 *       These exist if:
	 *
	 *       1. The transform is a rotation
	 *       2. The variable is degenerate in index 0
	 *       3. The output frame is a fixed offest from the input frame 
	 *          (class 4 in spice lingo)
	 */

	/* Except for the rare case of fixed offset frame rotations, all spice conversions
		are going to need ephemeris time from some source.  Look ahead an see if
		we have input time data. */

	DasDim* pTimeDim = DasDs_getDim(pDsIn, "time", DASDIM_COORD);
	DasVar* pTimeVar = NULL;
	const char* sTimeAx = NULL;
	if(pTimeDim != NULL){
		sTimeAx = DasDim_getAxis(pTimeDim, 0);
		if(sTimeAx == NULL) sTimeAx = "x";

		if((pTimeVar = DasDim_getVar(pTimeDim, DASVAR_REF)) == NULL)
			pTimeVar = DasDim_getPointVar(pTimeDim);
	}
	if(pTimeVar == NULL){
		return das_error(PERR, "No time coordinate present in input dataset %s", DasDs_id(pDsIn));
	}

	if(! Units_haveCalRep(DasVar_units(pTimeVar))){
		return das_error(PERR,
			"Time point variable in datest '%s', in group '%s' is in units "
			"of '%s', which can't be converted to UTC.", DasDs_id(pDsIn),
			DasDs_group(pDsIn), Units_toStr(DasVar_units(pTimeVar))
		);
	}

	/* Per-dimension operations:
		Loop over all input dimensions.  Copy over ones that should be retained. 
		Generating new ones and thier calculation structures as we go. */

	for(int iType = DASDIM_COORD; iType <= DASDIM_DATA; ++iType){
		size_t uDims = DasDs_numDims(pDsIn, iType);                   /* All Dimensions */
		for(size_t uD = 0; uD < uDims; ++uD){

			DasDim* pDimIn = (DasDim*)DasDs_getDimByIdx(pDsIn, uD, iType); /* All Variables */
			size_t uVars = DasDim_numVars(pDimIn);

	
			/* 1) Check to see if this input dim alone will provide rotation data */
			bool bRotate = _isSufficentRotSrc(pCtx, pDimIn);
		

			/* 2) Carry over most inputs, some rotations don't carry over */
			if(!bRotate || pCtx->bKeepOrig){

				DasDim* pDimOut = DasDs_makeDim(pDsOut, iType, DasDim_dim(pDimIn), DasDim_id(pDimIn));
				DasDesc_copyIn((DasDesc*)pDimOut, (DasDesc*)pDimIn);

				if(DasDim_getFrame(pDimIn))
					DasDim_setFrame(pDimOut, DasDim_getFrame(pDimIn));
				
				DasDim_setAxes(pDimOut, pDimIn);

				/* Now carry over the variables */
				uVars = DasDim_numVars(pDimIn);
				for(size_t uV = 0; uV < uVars; ++uV){
					DasVar* pVarIn = (DasVar*) DasDim_getVarByIdx(pDimIn, uV);

					DasVar* pVarOut = copy_DasVar(pVarIn); /* <-- stays attached to old array, good */

					const char* sRoleIn = DasDim_getRoleByIdx(pDimIn, uV);
					DasDim_addVar(pDimOut, sRoleIn, pVarOut);

					/* If requested, assign a frame vector variables without one */
					if((pCtx->uAnonDasId != 0)&&_hadAnonFrame(pVarIn)){
						DasVar_setFrame(pVarOut, pCtx->uAnonDasId);

						/* TODO: This is dumb! refactor the frame ID mentality */
						DasDim_setFrame(pDimOut, 
							DasFrame_getName( StreamDesc_getFrameById(pSdOut,pCtx->uAnonDasId) )
						);
					}

					/* If this var has an array, we'll need our own array and codec */
					if(DasVar_type(pVarIn) == D2V_ARRAY){
						DasAry* pAry = DasVar_getArray(pVarIn);

						/* Increment the reference, since DasDs_addAry() steals a reference */
						inc_DasAry(pAry);
						DasDs_addAry(pDsOut, pAry);
						int nItems;
						const DasCodec* pCodec = DasDs_getCodecFor(pDsIn, DasAry_id(pAry), &nItems);
						
						/* If there's no codec for the input array, we don't need
							to worry about it because these are header only values */
						if(pCodec != NULL){
							DasCodec* pCodecOut = DasDs_addFixedCodecFrom(
								pDsOut, NULL, pCodec, nItems, DASENC_WRITE
							);
							if(!pCodecOut) return PERR;

							/* Tweek the output codec here.  If the array vt is time, add two
							   characters to the output size since we don't transmit day of
							   year times anymore */
							if(DasAry_valType(pAry) == vtTime)
								pCodecOut->nBufValSz += 2;
						}

						/* Further codec customizations ??? */
					}
				}
			}

			/* 3) Make output calculation dims based on this dimension */
			for(size_t uC = 0; uC < pCtx->nXReq; ++uC){

				XReq* pReq = &(pCtx->aXReq[uC]);

				/* Does this dim + xfrom request result in any new outputs? */
				if(pReq->uFlags & XFORM_LOC){
					/* need time coordinates for locations */
					if((iType!=DASDIM_COORD)||(strcasecmp(DasDim_dim(pDimIn),"time") != 0))
						continue;
				}
				else{
					/* Assume it's a rotation, that's the only other item */
					if(!bRotate || ! _matchRotDim(pDimIn, pReq, pCtx->aAnonFrame))
						continue;
				}

				if(nCalcs >= MAX_XFORMS) goto ERR_MAX_XFORMS;

				/* Setup the calc inputs for this specific request + input combo */
				XCalc* pCalc = ((XCalc*)pDsOut->pUser) + nCalcs;
				memset(pCalc, 0, sizeof(XCalc));
				memcpy(&(pCalc->request), &(pCtx->aXReq[uC]), sizeof(XReq));

				pCalc->pTime = pTimeVar;
				if(pReq->uFlags & XFORM_LOC){
					pCalc->pVarIn = NULL;
					nRet = _addLocation(pCalc, pDsOut, sTimeAx);
				}
				else{
					pCalc->pVarIn = DasDim_getPointVar(pDimIn);
					nRet = _addRotation(pCalc, pCtx->aAnonFrame, pDsOut);
				}	
				
				if(nRet != DAS_OKAY)
					return PERR;

				/* The null we have at the end allows for one over, but that's an error */
				if(nCalcs >= MAX_XFORMS) goto ERR_MAX_XFORMS;
				++nCalcs;
			}
		}
	}

	/* Attach the output dataset to the input dataset so that we can
		find it when processing a packet at a time */
	pDsIn->pUser = pDsOut;
	return DasIO_writeDesc(pCtx->pOut, (DasDesc*)pDsOut, iPktId);

ERR_MAX_XFORMS:
	return das_error(PERR, 
		"Only %d SPICE calculations/dataset supported, recompile to change the "
		"limit.", MAX_XFORMS
	);
}

/* ************************************************************************ */
/* Data output */

double _dm2et(const das_datum* pInput, double rTimeShift)
{
	double rEt = 0.0;
	if(pInput->vt == vtTime){
		rEt = Units_convertFromDt(UNIT_ET2000, (const das_time*)pInput);
#ifndef NDEBUG
		/* Check utc conversions */
		char sBuf[32] = {'\0'};
		dt_isoc(sBuf, 31, (const das_time*)pInput, 9);
		double rCheck;
		utc2et_c(sBuf, &rCheck);
		if(fabs(rCheck - rEt) > 0.001){
			das_error(PERR, "Debug check on spice time conversions failed");
			return -1*60*60*24*50.0;
		}
#endif
	}
	else
		rEt = Units_convertTo(UNIT_ET2000, das_datum_toDbl(pInput), pInput->units);

	return rEt;
}

/* We want the input dataset here because we need to know how many provided
	values we're going to get */
DasErrCode _writeLocation(DasDs* pDsIn, XCalc* pCalc, double rTimeShift)
{
	double rEt, rLt;  /* ephemeris time, light time */ 
	SpiceDouble aRecOut[3];
	SpiceDouble aTmp[3];
	SpiceInt    nTmp;
	float aOutput[3];
	
	DasAry* pAryOut = DasVar_getArray(pCalc->pVarOut);
	if(pAryOut == NULL)
		return das_error(PERR, "Output variable definition logic error");

	XReq* pReq  = &(pCalc->request);
	ubyte uSysOut = pReq->uOutSystem;

	SpiceDouble radOut = 0.0;      /* Potential constants for ellipsoidal coords */
	SpiceDouble flatOut = 0.0;
	if((uSysOut == DAS_VSYS_DETIC)||(uSysOut == DAS_VSYS_GRAPHIC)){
		bodvcd_c(pReq->nBodyId, "RADII", 3, &nTmp, aTmp);
		radOut = aTmp[0];
		flatOut = (radOut - aTmp[0]) / radOut;
	}

	DasDsUniqIter iter;        /* Produces unique indexes for given DS and Var */
	DasDsUniqIter_init(&iter, pDsIn, pCalc->pVarOut);
	das_datum dm;
	for(; !iter.done; DasDsUniqIter_next(&iter)){

		DasVar_get(pCalc->pTime, iter.index, &dm);
		rEt = _dm2et(&dm, rTimeShift);

		spkezp_c(
			pReq->nBodyId, rEt, pReq->aOutFrame, "NONE", pReq->nOutCenter, aRecOut, &rLt
		);

		if(uSysOut != DAS_VSYS_CART){  /* Convert output coord sys if needed */
			switch(uSysOut){
			case DAS_VSYS_CYL: /* ρ,ϕ,z */
				reccyl_c(aRecOut, aTmp, aTmp+1, aTmp+2); 
				aTmp[1] *= dpr_c(); /* Always output degrees */
				break;
			case DAS_VSYS_SPH:   /* r,θ,ϕ */
				recsph_c(aRecOut, aTmp, aTmp+1, aTmp+2);
				aTmp[1] *= dpr_c(); aTmp[2] *= dpr_c(); 
				break;
			case DAS_VSYS_CENTRIC:     /* radius (r), long (ϕ), lat (θ) */
				reclat_c(aRecOut, aTmp, aTmp+1, aTmp+2);
				aTmp[1] *= dpr_c(); aTmp[2] *= dpr_c(); 
				break;
			case DAS_VSYS_DETIC:       /* long (ϕ), lat (θ), alt (r') */
				recgeo_c(aRecOut, radOut, flatOut, aTmp, aTmp+1, aTmp+2);
				aTmp[0] *= dpr_c(); aTmp[1] *= dpr_c(); 
				break;
			case DAS_VSYS_GRAPHIC:     
				recgeo_c(aRecOut, radOut, flatOut, aTmp, aTmp+1, aTmp+2);

				/* now get complementary angle for logitude, 2*pi - east long = west long */
				/* except sun, moon and earth (really?) */
				if((pReq->nBodyId != 3)&&(pReq->nBodyId != 10)&&(pReq->nBodyId != 301))
					aTmp[0] = (2 * pi_c()) - aTmp[0];

				aTmp[0] *= dpr_c(); aTmp[1] *= dpr_c(); 
				break;
			default: assert(0); return PERR; /* any others ? */
			}
			aRecOut[0] = aTmp[0]; aRecOut[1] = aTmp[1]; aRecOut[2] = aTmp[2]; 
		}

		aOutput[0] = aRecOut[0]; aOutput[1] = aRecOut[1]; aOutput[2] = aRecOut[2];
		DasAry_append(pAryOut, (ubyte*) aOutput, 3);
		/* DasAry_markEnd() See TODO note below on updates for rolling ragged arrays */
	}
	CHECK_SPICE;

	return DAS_OKAY;
}

/* TODO: Rolling ragged arrays.
	
	To do this there needs to be some concept of which dimensions are
	at an end point.  This would return something like:
 
	  iter.atEnd  A value from 0 to DASIDX_MAX that gives the number
					  of demensions that have just ended.
 
	  iter.idxEnd An array of dimensions that are done.
 
	This would only be for variables/datasets that are ragged.
 
	if(iter.atEnd > 0){
		for(iDsIdx = 0; iDsIdx < iter.atEnd; ++iDsIdx){
		
		  // Something to back map DS dims to array DIMS 
		  if( (iAryIdx = DasVar_reverseMap(iDsIdx)) > -1)
			  DasAry_markEnd(iAryIdx);
		}
	}
 */

DasErrCode _writeRotation(DasDs* pDsIn, XCalc* pCalc, double rTimeShift)
{
	double rEt;  /* ephemeris time */ 
	double mRot[3][3];
	SpiceDouble aRecIn[3];
	SpiceDouble aRecOut[3];
	SpiceDouble aTmp[3];
	float aOutput[3];
	ubyte uSysOut = 0;
	das_geovec* pVecIn = NULL;

	DasAry* pAryOut = DasVar_getArray(pCalc->pVarOut);
	if(pAryOut == NULL)
		return das_error(PERR, "Output variable definition logic error");

	XReq* pReq  = &(pCalc->request);
	DasVar* pVarIn = pCalc->pVarIn;
	DasVar* pVarOut = pCalc->pVarOut;
	uSysOut = pReq->uOutSystem;

	DasDsUniqIter iter;        /* Produces unique indexes for given DS and Var */
	DasDsUniqIter_init(&iter, pDsIn, pVarOut);

	das_datum dm;
	for(; !iter.done; DasDsUniqIter_next(&iter)){

		DasVar_get(pCalc->pTime, iter.index, &dm);
		
		rEt = _dm2et(&dm, rTimeShift);

		pxform_c(pReq->aInFrame, pReq->aOutFrame, rEt, mRot);   /* Get rot matrix */

		DasVar_get(pVarIn, iter.index, &dm);

		pVecIn = (das_geovec*)&(dm);                /* datums store payload first */
		assert(pVecIn);

		memset(aRecIn, 0, 3*sizeof(SpiceDouble));  /* zero any missing components */
		for(ubyte u = 0; u < pVecIn->ncomp; ++u)
			das_geovec_values(pVecIn, aRecIn);

		if(pVecIn->systype != DAS_VSYS_CART){   /* convert non-cart input coords */
			switch(pVecIn->systype){
			
			case DAS_VSYS_CYL:          /* Assume degrees, but need to check */
				aRecIn[1] *= rpd_c();
				cylrec_c(aTmp[0], aTmp[1], aTmp[2], aRecIn); 
				break;
			case DAS_VSYS_SPH:
				aRecIn[1] *= rpd_c(); aRecIn[2] *= rpd_c();
				sphrec_c(aTmp[0], aTmp[1], aTmp[2], aRecIn); 
				break;
			case DAS_VSYS_CENTRIC:
				aRecIn[1] *= rpd_c(); aRecIn[2] *= rpd_c(); 
				latrec_c(aTmp[0], aTmp[1], aTmp[2], aRecIn);
				break;
			default: break;
			}
		}
				
		mxv_c(mRot, aRecIn, aRecOut);
	
		/* Convert non-cart output coords, always output degrees */
		if(uSysOut != DAS_VSYS_CART){        
			switch(uSysOut){
			case DAS_VSYS_CYL:  /* ρ,ϕ,z */
				reccyl_c(aRecOut, aTmp, aTmp+1, aTmp+2);
				aTmp[1] *= dpr_c();   
				break;
			case DAS_VSYS_SPH:   /* r,θ,ϕ */
				recsph_c(aRecOut, aTmp, aTmp+1, aTmp+2);
				aTmp[1] *= dpr_c(); aTmp[2] *= dpr_c(); 
				break;
			case DAS_VSYS_CENTRIC:     /* radius (r), long (ϕ), lat (θ) */
				reclat_c(aRecOut, aTmp, aTmp+1, aTmp+2);
				aTmp[1] *= dpr_c(); aTmp[2] *= dpr_c(); 
				break;
			default:  assert(0); break;     /* non-orthonormal stopped by _addOp() */
			}
			aRecOut[0] = aTmp[0]; aRecOut[1] = aTmp[1]; aRecOut[2] = aTmp[2]; 
		}

		aOutput[0] = aRecOut[0]; aOutput[1] = aRecOut[1]; aOutput[2] = aRecOut[2];
		DasAry_append(pAryOut, (ubyte*) aOutput, 3);
		/* DasAry_markEnd()          See TODO note above on rolling ragged arrays */
	}
	CHECK_SPICE;

	return DAS_OKAY;
}

DasErrCode writeAndClearDs(Context* pCtx, int iPktId, DasDs* pDsIn)
{
	DasDs* pDsOut = (DasDs*) pDsIn->pUser;
	XCalc* pCalc = (XCalc*) pDsOut->pUser;
	DasErrCode nRet = DAS_OKAY;

	while(pCalc->pVarOut != NULL){

		if((pCalc->request.uFlags & XFORM_LOC) != 0)
			nRet = _writeLocation(pDsIn, pCalc, pCtx->rEphemShift);
		else
			nRet = _writeRotation(pDsIn, pCalc, pCtx->rEphemShift);

		if(nRet != DAS_OKAY)
			return nRet;

		++pCalc;
	}

	/* Write output data, clear everything that is record varying */
	if( (nRet = DasIO_writeData(pCtx->pOut, (DasDesc*)pDsOut, iPktId)) != DAS_OKAY)
		return nRet;

	DasDs_clearRagged0(pDsOut);
	DasDs_clearRagged0(pDsIn);
	return DAS_OKAY;
}

DasErrCode onData(StreamDesc* pSd, int iPktId, DasDs* pDsIn, void* pUser)
{
	Context* pCtx = (struct context*)pUser;

	/* Use buffering for better performance */
	if((pCtx->uFlushSz == 0)||(DasDs_memUsed(pDsIn) > pCtx->uFlushSz))
		return writeAndClearDs(pCtx, iPktId, pDsIn);

	return DAS_OKAY;
}

/* ************************************************************************* */

DasErrCode onExcept(OobExcept* pExcept, void* pUser)
{
	/* struct context* pCtx = (struct context*)pUser; */

	/* If this is a no-data-in range message set the no-data flag */
	return DAS_OKAY;
}

/* ************************************************************************* */

DasErrCode onClose(StreamDesc* pSdIn, void* pUser)
{
	struct context* pCtx = (struct context*)pUser;
	
	/* Loop over all the datasets in the stream and make sure they are flushed */
	int nPktId = 0;
	DasDesc* pDescIn = NULL;
	DasDs* pDs = NULL;
	DasErrCode nRet;
	ptrdiff_t aShape[DASIDX_MAX] = DASIDX_INIT_UNUSED;
	while((pDescIn = DasStream_nextDesc(pSdIn, &nPktId)) != NULL){
		if(DasDesc_type(pDescIn) == DATASET){

			/* If we have any data, then write it */
			pDs = (DasDs*)pDescIn;
			DasDs_shape(pDs, aShape);
			if(aShape[0] > 0){
				if((nRet = writeAndClearDs(pCtx, nPktId, (DasDs*)pDescIn)) != DAS_OKAY)
					return nRet;
			}
		}
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

	Context ctx; /* The "what am I doing" common block */
	
	if(parseArgs(argc, argv, &ctx) != DAS_OKAY)
		return 13;

	daslog_setlevel(daslog_strlevel(ctx.aLevel));

	das_spice_err_setup(); /* Don't emit spice errors to stdout */

	furnsh_c(ctx.aMetaKern);
	if(failed_c()){
		das_send_spice_err(3, DAS2_EXCEPT_SERVER_ERROR);
	}

	if(addSpiceIDs(&ctx) != DAS_OKAY) /* load body centers for frames */
		return PERR;

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