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

#ifdef _WIN32
#define strcasecmp _stricmp
#define strncasecmp _strnicmp
#else
#include <strings.h>
#endif

#include <cdf.h>
#include <das2/core.h>

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

/* The default memory threshold, read from CDFs in blocks around this big */
#define DEF_PULL_BYTES 16777216;   /* 16 MBytes */

static g_sHdrSent = false; /* True if a stream header has been sent */
DasIO* g_pIoOut = NULL;    /* the streamer object */
DasStream g_pSd = NULL;    /* the output memory structure */

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

/* ************************************************************************* 
 * Bounces ERRORs out to the client, outputs other messages to server only
 * Log messages of CRITical flush the stream and shutdown the reader 
 */

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
		DasIO_writeStreamDesc(g_pIoOut, g_pSd)

	/* Assume server error for all messages.  Early checks of missing data
	   should not use daslog_error_v() etc. to indicate no data in range. */
	OobExcept except;
	OobExcept_set(&except, DAS_EX_SERVER_ERR, sMsg);
	DasIO_writeException(g_pIoOut, "%s", sMsg);
	
	/* Crtical items force a stream flush and app quit */
	if(nLevel < DASLOG_CRIT){
		DasIO_close(g_pIoOut);
		del_DasIO(g_pIoOut);

		/* Leaks opts VAR_SPEC memory, but OS will clean that up */
		exit();
	}
}

/* ************************************************************************* */

/* NONE - Send whole var,
   SUM  - Total all values, no unit change
   AVG  - Sum all vals, divide by coord max - coord min use Units_divide()
   SLICE - Pick out one 'plane' of data in the hypercube
   COMP - Pick out one component in a vector (must use index, no values)
*/
typedef enum varop {NONE, SUM, AVG, SLICE, COMP } varop_t;

typedef struct var_spec {
	char[64]     sDataVar;
	enum varop_t nOp;
	char[64]     sCoordVar;
	int          nIndex;    /* If -1, use dCoordVal below to find the index */
	double       dCoordVal; /* Temporary until index of value found in CDF */
} VarSpec;

typedef struct opts {
	char[12]   sLevel;    /* Logging level */
	int        nDas;      /* One of 2 or 3 for das2 or das3 */
	char[256]  sPattern;  /* The filename pattern */
	das_range  range;     /* The time range to query */
	var_spec*  pSpecs;    /* The variables to extract */
	int        nSpecs;    /* Num of vars to extract */
} Opts;


DasErrCode parseArgs(int argc, char** argv, Opts* pOpts){
	const char* sHdr = NULL;
	char* sMsg[256] = {'\0'};   /* No XML escapes, watch output! */
	char* sPkt[384] = {'\0'};

	/* Step 0, look for "-2" or "--das2" to affect other output */
	pOpts->nDas = 3;
	for(int i = 1; i < argc; ++i){
		if(strcmp(argv[i], "-2")==0){ pOpts->nDas = 2; break;}
		if(strcmp(argv[i], "--das2"==0)){ pOpts->nDas = 2; break;}
	}

	/* TODO: 
		Now proceed on with normal parsing with the ability to jump 
	   to the error handler below.  Anything wrong is something to
	   do with how the user invoked the program so all errors are
	   output as IllegalArgument/QueryError 
	*/


	return DAS_OKAY;

	/* Manual error encoding before DasIO is initialized */
ARG_ERROR:
	fprintf(stderr, "CRITICAL: %s\n", sMsg);

	if(pOpts->nDas < 3){
		sHdr = "<stream version=\"2.2\" />\n";	
		printf("[00]%06zu%s",strlen(sHdr), sHdr);
		snprintf(sPkt, 255, 
			"<exception type=\"IllegalArgument\" message=\"%s\" />\n", sMsg
		)
		printf("[xx]%06zu",strlen(sPkt),sPkt);
	}
	else{
		sHdr = "<stream version=\"3.0\" type=\"das-basic-stream\" />\n";
		printf("[00]%06zu%s",strlen(sHdr), sHdr);
		snprintf(sPkt, 255, 
			"<exception type=\"QueryError\">\n%s\n</exception>\n", sMsg
		)
		printf("|Ex||%zu|",strlen(sPkt),sPkt);
	}
	return PERR;
}

/* ************************************************************************* */
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
 */

int main(int argc, char** argv) 
{

	DasErrCode nRet = DAS_OKAY;

	popts_t opts;
	parseArgs(argc, argv, &opts); /* <-- may exit program via log handler */

	/* Install our own handler for log messages, we'll handle program
	 * exit so that problems get sent to the remote client */
	das_init(argv[0], DASERR_DIS_EXIT, 0, daslog_strlevel(opts.oLevel), logHandler);

	/* Setup our globals first before log handlers need them */
	g_pIoOut = new_DasIO_cfile(PROG, stdout, (opts.nDas == 2) ? "w" : "w3");
	g_pSd = new_DasStream();

	/* 1. Parse the filename pattern */
	DasUriTplt* pTplt = new_DasUriTplt();
	DasUriTplt_register(pTplt, das_time_uridef());
	nRet = DasUriTplt_pattern(pTplt, opts.sPattern);
	if(nRet != DAS_OKAY)
		goto EXIT_PROG;

	/* 2. Get the time range */
	


	DasUriIter iter;
	if(init_DasUriIter(&iter, pTplt, ))

	/* 2. Open the first filename in range for the pattern */
	

	return 0;

EXIT_PROG:
	DasIO_close(g_pIoOut);
	del_DasIO(g_pIoOut);
	del_DasStream(g_pSd);
	/* Leaks opts VAR_SPEC memory, but OS will clean that up */
	return nRet;
};

/* ============================================================================
 * CLAUDE-NOTES: SKELETON TOUCH-UPS (for Dude's review)
 *
 * Small items I noticed while reading the skeleton — all compile-blockers
 * or logic bugs that would need to be fixed before the program runs.  Left
 * untouched in the source above so you can choose the fix.
 *
 *   7. Help-text typos / polish:
 *        - "It's purpose"     -> "Its purpose"       (line ~144)
 *        - "operatior"        -> "operator"          (line ~145)
 *        - "capabilites"      -> "capabilities"      (line ~155)
 *        - "skirt these"      -> "to skirt these"    (line ~158)
 *        - "efficent"         -> "efficient"         (line ~158)
 *        - "replacetment"     -> "replacement"       (line ~175)
 *        - "$v.cdfn"          -> "$v.cdf"            (line ~173)
 *        - "alligned"         -> "aligned"           (line ~241)
 *        - Example 2 uses  `:integrate:ts2_l2_ace_TSCS_anode_angle`
 *          with colons, but the VAR_SPEC grammar above specifies commas
 *          between the operation and the coord-var name.  One or the
 *          other is wrong — pick a separator and make them match.
 *
 *   8. The `--no-op` / `-n` option promises "print a list of files that
 *      would have been read".  That maps cleanly onto DasUriIter — just
 *      print sPath instead of opening/streaming.  Useful diagnostic for
 *      template debugging and probably worth implementing first.
 *
 *   9. `-2` / `--das2` with a rank-3+ CDF variable and no VAR_SPEC
 *      reduction is an error the CLI parser should catch early, before
 *      any files are opened.
 * ============================================================================
 */