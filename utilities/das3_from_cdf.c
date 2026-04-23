/* Copyright (C) 2026   Chris Piker <chris-piker@uiowa.edu>
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

/* ****************************************************************************
 das3_from_cdf: Pull data from a CDF file series and output as a das stream

   Output may be to a das2 or das3 stream, though das2 output may require
   extra slice or total arguments.

**************************************************************************** */

/* This is a das reader.  The fundamental rules of a das reader are:
 *
 * 1. ONLY Stream data are sent to standard output, all general messages and
 *    and errors *always* go to standard error.
 *
 * 2. Errors should also be sent as <execption> packets to the client to
 *    so that they can be captured by client logging mechanisms.
 *
 * 3. Always return non-zero to the shell on an error.
 *
 * 4. Not having any data in a requested query range is *not* an error 
 *
 */

/* Why Two parallel data paths?
 *
 *    CDF to DasDs to das3 is one translation (das3 is native for DasDs)
 *
 *    CDF to DasDs to das3 is two translations (das3 is not native for DasDs)
 *
 * Since each translation is imprecise, it's better to tailor each path
 * to it's direct outputs as closely as possible.  CDF doesn't have DasDim
 * equivalents.  Neither do das2 streams.  Why introduce them just to take
 * them away?
 */

/* ============================================================================
 * CLAUDE-NOTES (Claude Opus 4.7, 2026-04-19) — orientation for future work
 *
 * Structural shape: this is a *source* utility — no input stream, CDF files
 * in, das2/3 stream out.  Unlike the filter/sink exemplars (das3_spice,
 * das3_cdf) there is no StreamHandler callback set; main() drives the loop
 * directly.  Expected skeleton of main() once the CLI is settled:
 *
 *   das_init(argv[0], DASERR_DIS_EXIT, 0, DASLOG_INFO, logHandler);
 *   parseArgs(argc, argv, &opts);
 *   daslog_setlevel(...);
 *
 *   DasIO* pOut = new_DasIO_cfile(PROG, stdout, "w3");   // "w3" for das3
 *   StreamDesc* pSd = new_DasStream();
 *   // ...fill pSd global props from first CDF's global attrs...
 *   DasIO_writeDesc(pOut, (DasDesc*)pSd, 0);
 *
 *   DasUriTplt* pTplt = new_DasUriTplt();
 *   DasUriTplt_register(pTplt, das_time_uridef());
 *   DasUriTplt_pattern(pTplt, opts.sTemplate);
 *
 *   das_range r;
 *   das_range_fromUtc(&r, opts.sBegin, opts.sEnd);
 *
 *   DasUriIter iter;
 *   init_DasUriIter(&iter, pTplt, 1, &r);
 *   const char* sPath;
 *   while((sPath = DasUriIter_next(&iter)) != NULL){
 *      // open CDF, translate record-varying vars to DasDs, emit
 *   }
 *   fini_DasUriIter(&iter);
 *   DasIO_writeClose(pOut);
 *
 * First-CDF / per-file boundary handling is the biggest open design question.
 * Two options worth thinking about:
 *   A) Emit one StreamDesc + one PktDesc per distinct CDF shape/cadence,
 *      re-using the same pkt id across files when shapes match.  Clients
 *      handle gaps via the implicit time ordering.
 *   B) Emit a new PktDesc per file and rely on packet redefinition. Simpler
 *      to code, but heavier on the wire.
 * Option A matches how das3_cdf writes files back out and is probably the
 * right default.
 *
 * Variable construction patterns you'll need (see test/TestVariable.c
 * and the CLAUDE.md "Utility Authoring Patterns" section):
 *   - new_DasVarArray(pAry, SCALAR_N(...))    for stored record-varying data
 *   - new_DasVarSeq(..., &rMin, &rDelta, ...) for regularly-spaced CDF vars
 *                                              (common for frequency tables)
 *   - new_DasVarBinary(pRef, "+", pOff)       for CDF REF_TIME + OFFSET pairs
 *   - inc_DasAry(pAry) before DasDs_addAry()  if you keep using the array
 *
 * Time handling: CDF's CDF_TIME_TT2000 maps to UNIT_TT2000.  Use
 * Units_convertFromDt / Units_convertToDt — not the varargs functions.
 * The 0x80000000 00000000 fill value needs the endian-aware byte-array
 * trick already present at g_tt2kfill below.
 *
 * VAR_SPEC operations (integrate/average/slice) are rank-reduction ops
 * needed only for the -2 das2 mode.  In das3 mode they should be a no-op
 * error ("not applicable in das3 output — das3 supports the native rank").
 * Consider whether it's worth implementing them at all for v1, or whether
 * v1 should be das3-only with a TODO for the das2 reducers.
 *
 * The current skeleton has a few fixable issues — see the
 * "SKELETON TOUCH-UPS" comment block below main() for a consolidated list;
 * I've left the existing code untouched so you can review and decide.
 *
 * For a fuller brain-dump of what I know about this utility's structure
 * and the CDF-library APIs I'd expect to reach for, see
 *   utilities/das3_from_cdf_notes.md
 * ============================================================================
 */

#define _POSIX_C_SOURCE 200112L

#include <das2/core.h>
#include <cdf.h>

#ifdef _WIN32
#define strcasecmp _stricmp
#define strncasecmp _strnicmp
#else
#include <strings.h>
#endif

#include <string.h>


#define PROG "das3_from_cdf"
#define PERR (DASERR_MAX + 10)

/* TT2000 fill is more negative then LLONG_MIN, so it can't be written in C
   code directly (won't compile) so write it as raw bytes. 

   LLONG_MIN is -9223372036854775807, but we need -9223372036854775808
*/
#ifdef HOST_IS_LSB_FIRST
const ubyte g_tt2kfill[8] = {0,   0,0,0, 0,0,0,0x80};
#else
const ubyte g_tt2kfill[8] = {0x80,0,0,0, 0,0,0,   0};
#endif

/* ************************************************************************* */
/* Globals */

/* The default memory threshold, read from CDFs in blocks no bigger than 
   this (in bytes, non values) */
#define MAX_CDF_GET_BYTES 16777216;   /* 16 MBytes */

/* ************************************************************************* */

void prnHelp()
{
	printf(
		"SYNOPSIS\n"
		"   " PROG " - Read a CDF file series and output das v2 or v3 streams\n"
		"\n"
		"USAGE\n"
		"   " PROG " [options] PATTERN BEGIN END VAR_SPEC1 [VAR_SPEC2 ...] \n"
		"\n"
		"DESCRIPTION"
		"   " PROG " is a das reader.  It's purpose is to be the origin point for"
		"   a full time resolution das stream.  Stream operatior programs such as "
		"   das2_psd, das2_bin_avgsec, and das3_csv further convert output into"
		"   the desired form.  Das streams may be generated in either das2 or "
		"   das3 format.\n"
		"\n"
		"   Since das2 streams may contain at most two coordinates, high rank CDF"
		"   data variables such as sounder radar returns in time, altitude, and "
		"   frequency coordinates; or particle flux in time, direction and energy "
		"   must be reduced to a rank-2 dataset, either by slicing the variable in "
		"   a coordinate or taking the total or average of data values over a "
		"   coordinate. Optional arguments below describe the available capabilites "
		"   for rank reduction.  Das3 streams were created for high rank datasets and"
		"   thus do not have this restriction. " PROG " output defaults to das3 format "
		"   skirt these complications, but das2 streams are often more efficent to "
		"   process and may be enabled using the `-2` option defined below."
		"\n"
		"   The required parameters are listed here, options follow in thier own "
		"   section below."
		"\n"
		"   PATTERN\n"
		"      This string defines the relationship between time coordinates and\n"
		"      filenames. Each time value component in the full path is represented\n"
		"      by a time field variable.  For example file names of the form:\n"
		"\n"
		"         /project/juno/data/2025/juno-waves-survey-2025-001-v1.0.cdf\n"
		"\n"
		"      Would have the pattern:\n"
		"\n"
		"         /project/juno/data/$Y/juno-waves-survey-$Y-$j-v$v.cdfn\n"
		"\n"
		"      The time field replacetment parameters are:\n"
		"\n"
		"        $Y - A four-digit year\n"
		"        $m - A two-digit month of year\n"
		"        $j - A three-digit day of year\n"
		"        $d - A two-digit day of month\n"
		"        $H - A two-digit hour of day (0-23)\n"
		"        $M - A two-digit minute of hour\n"
		"        $S - A two-digit second of minute\n"
		"\n"
		"      Note that the patterns $j and $m,$d are mutually exclusive.  A single"
		"      path component (either directory or filename) may not contain both, "
		"      $j may be used in directories that contain files with $m,$d patterns"
		"      and vice versa."
		"\n"
		"   BEGIN END\n"
		"      The start and stop time of data to stream.  This particular reader only\n"
		"      locates data in time coordinates.  Future versions may allow for data\n"
		"      selection in other coordinates. The format is an ISO-8601 style string,\n"
		"      where the trailing time fields may be omitted if they are zero. Note that\n"
		"      the END time is an exclusive upper bound\n"
		"\n"
		"   VAR_SPEC\n"
		"      A specification for a variable to stream. Variable specifications have\n"
		"      the format:\n"
		"\n"
		"         VAR_NAME[:OPERATION:COORD_VAR[,INDEX]]"
		"\n"
		"      for example\n"
		"\n"
		"         flux:sum:direction"
		"\n"
		"      would output particle flux integrated over look directions.  Only the "
		"      variable name is needed. Some OPERATIONs require a that a coordinate "
		"      value, or index be supplied. Supported OPERATIONs are:\n"
		"\n"
		"         sum   - Sum over all values in a coordinate, units unchanged.\n"
		"\n"
		"         avg   - Sum over values in a coordinate and divide by the range.\n"
		"                 Note that this changes the output units of the variable.\n"
		"\n"
		"         slice - Only return data for a single value of a coordinate. This\n"
		"                 operation requires a coordinate value or index.\n"
		"\n"
		"         comp  - Only output a single component of a vector variable. This\n"
		"                 operation requires a coordinate value or index\n"
		"\n"
		"\n    Operations are not required if the stream type supports the number of\n"
		"      coordinates for the data value, but they may be specificed anyway to\n"
		"      network bandwidth."
		"\n"
		"      Multiple VAR_SPEC sections may be given to output multiple variables\n"
		"      at once."
		"\n"
		"OPTIONS\n"
		"   -h, --help   Write this text to standard output and exit.\n"
		"\n"
		"   -l LEVEL, --log=LEVEL\n"
		"               Set the logging level, where LEVEL is one of 'debug', 'info',\n"
		"               'warning', 'error' in order of decreasing verbosity. All log\n"
		"               messages go to the standard error channel. Defaults to 'info'.\n"
		"\n"
		"   -2, --das2  Output a das2 stream instead of a das3 stream.\n"
		"\n"
		"   -n, --no-op Don't write a stream, just check to see if the VAR_SPECs are\n"
		"               legal and print a list of files that would have been read for\n"
		"               the query.\n"
		"\n"
		"EXAMPLES\n"
		"   1. Output a das3 stream of B-field alligned oscillations:\n"
		"\n"
		"      " PROG " /data/TS2/ts2_l2_msc_bac_$Y$m$d_v$v.cdf ts2_l2_bac_fac \\\n"
		"         2026-01-01T12:00 2026-01-03T01:14 \n"
		"\n"
		"   2. Output a das2 stream of omni-directional flux from TRACERS/ACI CDFs:\n"
		"\n"
		"      " PROG " -2 /data/TS2/$Y/$m/$d/ts2_l2_ace_def_$Y$m$d_v$v.cdf \\\n"
		"         2026-01-01T12:00 2026-01-03T01:14 \\\n"
		"         ts2_l2_ace_def:integrate:ts2_l2_ace_TSCS_anode_angle"
		"\n"
		"   3. Output a das3 stream of a single frequency of MEX/MARSIS sounder data:\n"
		"\n"
		"      " PROG " /data/mex/$Y/$j/marsis_ais_$Y$j_$h.cdf 2015-04-14 2025-04-15\\\n"
		"         sounder:frequency:slice,80\n"
		"\n"
		"BUGS\n"
		"   Alternate CDF name patterns such as orbit number or spacecraft clock are\n"
		"   not yet supported.\n"
		"\n"
		"AUTHOR\n"
		"   chris-piker@uiowa.edu\n"
		"\n"
		"SEE ALSO\n"
		"   das3_cdf, das2_psd, das2_bin_avgsec\n"
		"\n"
	);
}

/* ************************************************************************* */
/* Send das_error() messages to the log (which bounces them to the client ) */

void _bounce_to_log(){
	das_error_msg* pErr = das_get_error();
	daslog_critical_v(
		"%s (reported from %s:%d, %s)", pErr->message, pErr->sFile,
		pErr->nLine, pErr->sFunc
	);
}

/* Wrap das calls in a macro that bounces errors out to the log, requires 
   a local variable of nDasStatus. */
#define DAS_EXIT( SOME_DAS_FUNC ) \
	if( (nDasStatus = (SOME_DAS_FUNC )) != DAS_OKAY) _bounce_to_log();
  

/* ************************************************************************* */
/* sending CDF message to the log (which bounces them to the client) */

bool _cdfOkayish(CDFstatus iStatus){
	char sMsg[CDF_ERRTEXT_LEN+1];

	/* Wrapper should have prevented this, but in case it's called directly */
	if(iStatus == CDF_OK)
		return true;

	CDFgetStatusText(iStatus, sMsg);

	if(iStatus < CDF_WARN){	
		daslog_error_v("from cdflib, %s", sMsg); /* Does not exit program */
		return false;
	}

	if(iStatus < CDF_OK) 
		daslog_warn_v("from cdflib, %s", sMsg);
  	
  	else if(iStatus > CDF_OK)
		daslog_info_v("from cdflib, %s", sMsg);

	return true;
}

/* Use a macro to avoid unneccessary functions calls that slow the program
   Requires a local nCdfStatus variable */
#define CDF_MAD( SOME_CDF_FUNC ) ( ((nCdfStatus = (SOME_CDF_FUNC) ) != CDF_OK) && (!_cdfOkayish(nCdfStatus)) )


/* ************************************************************************* 
 * Bounces ERRORs out to the client. Log messages of CRITical flush the
 * stream and shutdown the reader.  Current error type set higher up.
 */

/* Main sets these so that log handlers work, would have rather passed
   them in, but the log handler has no user data pointer */

static DasIO* g_pIoOut = NULL;    /* the streamer object */
static DasStream* g_pSd = NULL;    /* the output memory structure */

static except_t g_exType = DAS_EX_QUERY_ERR;

void logHandler(int nLevel, const char* sMsg, bool bPrnTime)
{
	if(nLevel < daslog_level())
		return;

	/* Always output to server logs */
	fprintf(stderr, "%s: %s\n", daslog_levelstr(nLevel), sMsg);

	/* Levels less than errors don't go out to the client */
	if(nLevel < DASLOG_ERROR)
		return;

	if(!(g_pIoOut->bSentHeader))
		DasIO_writeStreamDesc(g_pIoOut, g_pSd);

	/* Assume server error for all messages.  Early checks of missing data
	   should not use daslog_error_v() etc. to indicate no data in range. */
	OobExcept except;
	OobExcept_set(&except, g_exType, sMsg);
	if( DasIO_writeException(g_pIoOut, &except) != DAS_OKAY)
		nLevel = DASLOG_CRIT;
	
	/* Crtical items force a stream flush and app quit */
	if(nLevel >= DASLOG_CRIT){
		DasIO_close(g_pIoOut);
		del_DasIO(g_pIoOut);

		/* Leaks opts VAR_SPEC memory, but OS will clean that up */
		exit(PERR);
	}
}

/* ************************************************************************* */

#define MAX_CDF_VAR_LEN 63

/* Holds block data reads from a CDF.  Space is malloced once and 
   used over and over again. */
typedef struct var_buff_t {
	char sVar[MAX_CDF_VAR_LEN+1]; /* the name of this var */
	long nCdfType;                /* The CDF type of the data in the array */

	/* TODO: add other tracking structures as needed */

	/* The DasAry that holds buffered reads from cdf HyperGet.  When
		initializing this array always set the first index to -1 to 
		make it auto-expand.  For das3 streams, this is just a 
		reference to the DasDs array.  For das2 streams, this is the 
		only copy, but it doesn't matter because arrays are reference
		counted. */

	DasAry pAry;  /* Init as RANK_1(-1), RANK_2(-1, J), RANK_3(-1, J, K) etc. */
} VarBuf;

/* NONE - Send whole var, for das2 rank of data var must be 2 or less
   SUM  - Total all values, no unit change
   AVG  - Sum all vals, divide by coord max - coord min use Units_divide()
   SLICE - Pick out one 'plane' of data in the hypercube
   COMP - Pick out one component in a vector (must use index, no values)
*/
typedef enum varop {NONE, SUM, AVG, SLICE, COMP } varop_e;


#define MAX_CDF_VARS 63

/* TODO: Use these! Just commented out so source compiles
static VarBuf g_aVarBufs[MAX_CDF_VARS+1] = {0}; / * Leave null sentinal at end * /
static size_t g_uNextVarBuf = 0;
*/

/* Pointer Map: "O" = owns, "R" = Reference, "P" = Just Points
 *
 *  R = Reference
 *  O = Owns
 *
 *  Context: 
 *  |
 *  +-O-> VarSpec (containment)  <---------------+
 *        |                                      |
 *        +-R-> VarBuf <-------------------------|--------+
 *              |                                |        |
 *              +-R-> pAry (reference counts) <--|--+--+  |
 *                                               |  |  |  |
 *  das2 path                                    |  |  |  |
 *  ---------                                    |  |  |  |
 *  DasStream                                    |  |  |  |
 *  |                                            |  |  |  |
 *  +-O-> PktDesc                                |  |  |  |
 *        |                                      |  |  |  |
 *        +-O-> PlaneDesc                        |  |  |  |
 *              |                                |  |  |  |
 *              +-P-(via pUser) -----------------+  |  |  |
 *                                                  |  |  |
 *  das3 path                                       |  |  |
 *  ---------                                       |  |  |
 *  DasStream                                       |  |  |
 *  |                                               |  |  |
 *  +-O-> DasDs                                     |  |  |
 *        |                                         |  |  |
 *        +-R-[1]-----------------------------------+  |  |
 *        |                                            |  |
 *        +-O-> DasDim                                 |  |
 *              |                                      |  |
 *              +-O-> DasVar                           |  |
 *                    |                                |  |
 *                    +-R------------------------------+  |
 *                    |                                   |
 *                    +-P-(via pUser)---------------------+
 *     
 * NOTE: [1] DasDs_addAry steels a reference, be sure and call 
 *           int_DasAry() after giving array to DasDs
 */
typedef struct var_spec {

	/* The actual data variable to stream and it's buffer */
	VarBuf*   pData;

	/* The dependency vars, found using DEPEND_N attributes in CDF */
	VarBuf*   apCoords[DASIDX_MAX+1]; /* TODO: Use null sentenal or add count below*/

	/* The operation to perform */
	varop_e   nOp;

	/* operation argument */
	int       nIndex;    /* If -1, use dCoordVal below to find the index */
	double    dCoordVal; /* Temporary until index of value found in CDF */

	/* Processing flags needed by either the das2 or das3 processors,
	   not assigned at argument parsing time */
	uint32_t  uFlags;   

} VarSpec;

#define MAX_DATA_VARS 32
#define MAX_FILE_PTRN 255
typedef struct context_t {
	char       sLevel[12];    /* Logging level */
	int        nDas;      /* One of 2 or 3 for das2 or das3 */
	char       sPattern[MAX_FILE_PTRN + 1];  /* The filename pattern */
	const char* sBeg;     /* The start time as formatted by user */
	const char* sEnd;     /* The end time as formatted by user */
	das_range  range;     /* The time range to query */
	VarSpec    aSpecs[MAX_DATA_VARS];  /* The variables to extract points to */
	int        nSpecs;    /* Num of vars to extract */
} Context;


DasErrCode parseArgs(int argc, char** argv, Context* pCtx){
	const char* sHdr = NULL;
	char sMsg[256] = {'\0'};   /* No XML escapes, watch output! */
	char sPkt[384] = {'\0'};

	/* Step 0, look for "-2" or "--das2" to affect other output */
	pCtx->nDas = 3;
	for(int i = 1; i < argc; ++i){
		if(strcmp(argv[i], "-2")==0){ pCtx->nDas = 2; break;}
		if(strcmp(argv[i], "--das2")==0){ pCtx->nDas = 2; break;}
	}

	/* TODO: 
		Now proceed on with normal parsing with the ability to jump 
	   to the error handler below.  Anything wrong is something to
	   do with how the user invoked the program so all errors are
	   output as IllegalArgument/QueryError in here. 

		A the end of this function we either have legal looking
		data at pCtx or the the whole program has exited.

		Uncomment g_aVarBufs and g_uNextVarBuf above when this get's implimented.

	*/

	strncpy(sMsg, "Function not written yet", 255);
	goto ARG_ERROR;


	/* return DAS_OKAY; */

	/* Manual error encoding before DasIO is initialized */
ARG_ERROR:
	fprintf(stderr, "CRITICAL: %s\n", sMsg);

	if(pCtx->nDas < 3){
		sHdr = "<stream version=\"2.2\" />\n";	
		printf("[00]%06zu%s",strlen(sHdr), sHdr);
		snprintf(sPkt, 255, 
			"<exception type=\"IllegalArgument\" message=\"%s\" />\n", sMsg
		);
		printf("[xx]%06zu%s",strlen(sPkt),sPkt);
	}
	else{
		sHdr = "<stream version=\"3.0\" type=\"das-basic-stream\" />\n";
		printf("[00]%06zu%s",strlen(sHdr), sHdr);
		snprintf(sPkt, 255, 
			"<exception type=\"QueryError\">\n%s\n</exception>\n", sMsg
		);
		printf("|Ex||%zu|%s",strlen(sPkt),sPkt);
	}
	return PERR;
}

/* ************************************************************************* */
/* Setup the stream object that will contain data we want to stream
 *
 * The major task here is to find out which CDF Vars, or parts of vars
 * can coexist in the same dataset/packet structure.  If you have three
 * variables and they all link to the same Epoch, then you can output
 * a single dataset.
 *
 * If they all link to different coordinates, then they will need different
 * datasets.  Each das2 plane, or das3 variable will hold in it's user 
 * data pointer a map structure that defines how to map data out of the
 * the CDF into it's internal storage.
 *
 */

void setupDas2Stream(Context* pCtx, DasStream* g_pSd, CDFid nCdfId){
	/* Notes:
   There are only a few legal output patterns for das2 streams. See
   the plane_type_t enumeration in plane.h.

   A) X, Y               - Line plot
   B) X, Y, Y [, Y ...]  - Either a vector (Bx, By, Bz) or just multi channel
                           line data.  Das2 doesn't distinguish
	C) X, YScan           - A rank-2 CDF var (or something reduced to rank-2)
	                        where the DEPEND_1 variable is not a function of
	                        DEPEND_0.
	D) X, Y, YScan        - A rank-2 CDF var where it's DEPEND_1 intern has
	                         has it's own DEPEND_0
	E) X, Y, Z [, Z ...]  - Supported by das2 but not CDF, N/A for program.

   
   Time reference + offset note:
		For input arrays where the data are rank2 and the DEPEND_0 is a 
   	time value and the DEPEND_1 is a time offset value (typically named
   	EpochOffset or timeOffset, etc.) then you *MUST* set a special 
   	property as follows:
   
      	DasDesc_setStr(g_pSd, "renderer", "waveform");
   
   	This is really dumb, but all the clients support this kludge.  The
   	property should have been set in the Y plane at a minimum!
   */

	/* Basic Plan:

	0. Assume the CDF pointed to by nCdfId is already open

	1. Loop through all the VarSpec structures found in pOpts and 
	   determine the coordinate variable associated with each index
	   of the data variable array by looking for "DEPEND_0", "DEPEND_1",
	   "DEPEND_2" attributes.  If you find a "DEPEND_3" the CDF is just
	   not supported by this utility in das2 mode. 

	   Write thier names into VarSpec.psDepVar. Include all dependent
	   vars (coordinate) even if we slice or total over it to remove
	   that coordinate since we'll need some of it's metadata for labels.

	   Never allow slicing in DEPEND_0, which is usually named "Epoch".
	   This prog could do it but it would screw up clients.

	3. Time Unrolling (rank reduction):
		CDF Data vars with a DEPEND_0 that is time (typically named "Epoch"
		and a DEPEND_1 that is a time offset you have to unroll the 
		offset dimension by emitting successive packets where the 
		time is calculated by:

		X = Epoch[i] + Offset[j],  Y = Data[i,j]

		Note that a data var needs time unrolling by putting some flag
		in the VarSpec.uFlags member.

	2. With the VarSpec structures filled out, and an UNROLL flag set
		for one of the dimensions (if needed) loop over them to 
	   determine how many different PktDesc structs will be needed for
	   the stream.  We'll need:

	      1 for each non-repeated X coord (DEPEND_0)
	      1 for each non-repeated Y coord (DEPEND_1)

	   The best way to do this is probably to create the PktDesc objects
	   and PlaneDesc objects as you go, keeping track of already seen
	   X and Y coordinates

	3. Vector handling. Many CDF objects are vectors, you can find 
	   these using the LBR_PTR_$I attribute, where $I is the last 
	   valid index of the data variable array.  If it's 2 or 3 this 
	   is often a gemetric vector.  Geovectors are output as an:
	   <x><y><y>[<y>] packet type.  

	   Geovectors with time offsets are perfectly legal to stream
	   in das2 format because, for example:

      coord: Epoch[I]
      coord: EpochOffset[J] 
	   data: MSC_DATA[I][J][K]  (where K = 3, for X,Y,Z components)

	   After time unrolling (adding Epoch[i] + EpochOffset[j]) we get

	   coord: Epoch[I*J]
	   data:  MSC_DATA[I*J][K]

      and after component separation this becomes an:

      X, Y, Y, Y stream, where the Y's are "Bx", "By", "Bz"
	*/

	/* Function not written, auto exits */
	daslog_critical("Das2 streaming has not yet been implemented.");
}

void setupDas3Stream(Context* pCtx, DasStream* g_pSd, CDFid nCdfId){

	/* NOTE: Out of scope, do das2 path first */

	daslog_critical("Das3 streaming has not yet been implemented.");
}


void setupStream(
	Context* pCtx, DasIO* g_pIoOut, DasStream* g_pSd, CDFid nCdfId
){
	if(DasIO_getModel(g_pIoOut) == 2)
		setupDas2Stream(pCtx, g_pSd, nCdfId);
	else
		setupDas3Stream(pCtx, g_pSd, nCdfId);
}

/* ************************************************************************* */
/* Streaming data */

int streamFile(Context* pCtx, DasIO* pIoOut, DasStream* pSd, CDFid nCdfId)
{
	/* Basic Plan:
	 * 
	 * For each VarBuf in the context
	   which is *much* faster then the one record at a time functions 
	*/
	daslog_critical("Das2 streaming has not yet been implemented.");
	return 0;  /* never get's here */
}


/* ************************************************************************* */
/* It all starts here baby! */

int main(int argc, char** argv) 
{
	Context context;
	parseArgs(argc, argv, &context); /* <-- may exit program via log handler */

	/* Setup to bounce errors to client, with return on errs and custom 
	   log handler */
	das_init(argv[0], DASERR_DIS_RET, 0, daslog_strlevel(context.sLevel), logHandler);

	/* Assume at this point that any errors are user invocation errors */
	g_exType = DAS_EX_QUERY_ERR;

	/* Setup our globals first before log handlers need them */
	g_pIoOut = new_DasIO_cfile(PROG, stdout, (context.nDas == 2) ? "w" : "w3");
	g_pSd = new_DasStream();

	DasUriTplt* pTplt = new_DasUriTplt();
	DasUriTplt_register(pTplt, das_time_uridef());

	/* Start of normal processing */
	DasErrCode nDasStatus = DAS_OKAY;  /* Used by DAS_CHECK */
	CDFstatus nCdfStatus = CDF_OK; /* needed by the CDF_MAD() macro */ 

	DAS_EXIT( DasUriTplt_pattern(pTplt, context.sPattern) );
	DasUriIter iter;
	DAS_EXIT( init_DasUriIter(&iter, pTplt, 1, &(context.range)) );

	/* Main loop, lots of setup when first legal CDF file is opened */

	int nCdfsRead = 0;
	const char* sCdfFile = NULL;
	CDFid nCdfId = 0L;
	int nPktsSent = 0;
	while((sCdfFile = DasUriIter_next(&iter)) != NULL){

		if(CDF_MAD( CDFopenCDF(sCdfFile, &nCdfId)) )
			continue;  /* Skip this one */

		if(nCdfsRead == 0){
			setupStream(&context, g_pIoOut, g_pSd, nCdfId);
			DAS_EXIT( DasIO_writeDesc(g_pIoOut, (DasDesc*)g_pSd, 0) );

			/* Loop over datesets and write thier headers too */
			for(int nPktId = 1; nPktId < MAX_PKTIDS; ++nPktId){
				DasDesc* pDesc = g_pSd->descriptors[nPktId];
				if(pDesc != NULL){

					/* Note, DasDs doesn't have an DasDs_encode2() function to 
					   output itself using the das2 stream format, we'll 
					   have to add that in dataset.c, or a help file.
					   For now that's DasIO's problem, just assume it 
					   exists. */
					DAS_EXIT( DasIO_writeDesc(g_pIoOut, pDesc, nPktId) );
				}
			}
		}

		nPktsSent += streamFile(&context, g_pIoOut, g_pSd, nCdfId);
	}

	if(nPktsSent == 0){
		/* Send a no-data-in-range message if no data packets sent */
		OobExcept except;
		char sMsg[128] = {'\0'};
		snprintf(sMsg, 127, "No data in range %s to %s", context.sBeg, context.sEnd);
		OobExcept_set(&except, DAS_EX_NO_DATA, sMsg);
		DAS_EXIT( DasIO_writeException(g_pIoOut, &except) );
	}

	return 0;
};
