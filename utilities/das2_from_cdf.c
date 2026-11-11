/* Copyright (C) 2026 Chris Piker <chris-piker@uiowa.edu>
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

/* ****************************************************************************
 das2_from_cdf: Pull data from a CDF file series and output a das2 stream

 Why this is its own program.  We considered giving libdas3 the ability to
 write das2 streams from its DasDs data model and decided against it.  That
 would add long term complexity to the library on behalf of a format the
 old stream object model (DasPkt, DasPlane) already writes, and there are
 no plans to remove that model.  A tenet of the Unix philosophy is that a
 program should do one thing well.  Turning a pile of CDFs into a das2
 stream is this program's one job.  das3_from_cdf does the same for das3,
 and the CDF file model used by both is in cdfmodel.h.

**************************************************************************** */

/* This is a das reader.  The fundamental rules of a das reader are:
 *
 * 1. ONLY Stream data are sent to standard output, all general messages
 *    and errors *always* go to standard error.
 *
 * 2. Errors should also be sent as <exception> packets to the client so
 *    that they can be captured by client logging mechanisms.
 *
 * 3. Always return non-zero to the shell on an error.
 *
 * 4. Not having any data in a requested query range is *not* an error
 */

#define _POSIX_C_SOURCE 200112L

#include <das3/core.h>
#include "cdfmodel.h"

#include <string.h>
#include <stdarg.h>
#include <inttypes.h>
#include <ctype.h>

#define PROG "das2_from_cdf"

/* ************************************************************************* */

void prnHelp()
{
	printf(
"SYNOPSIS\n"
"   " PROG " - Stream time series data from CDF files as a das2 stream\n"
"\n"
"USAGE\n"
"   " PROG " [options] PATTERN BEGIN END [VAR1[,VAR2 ...]]\n"
"   " PROG " -V FILE\n"
"\n"
"DESCRIPTION\n"
"   " PROG " is a das2 reader.  It reads one or more CDF files carrying\n"
"   ISTP style metadata and writes a das 2.2 stream to standard output, the\n"
"   format Autoplot and other das2 clients read.  It is the origin point\n"
"   for a full resolution stream; reducers such as das2_bin_avgsec and\n"
"   das2_psd take it from there.  All log messages go to standard error,\n"
"   errors are also sent to the client as exception packets, and an empty\n"
"   query range is not an error.  das3_from_cdf is the companion program\n"
"   for das3 output; the two read files the same way.\n"
"\n"
"   The parameters are:\n"
"\n"
"   PATTERN\n"
"      Either the path to a single CDF file, or a pattern that relates time\n"
"      to file names.  Each time field in a pattern is a token of the form\n"
"      $F, where F is one of:\n"
"\n"
"         $Y - four digit year         $H - two digit hour of day (0-23)\n"
"         $m - two digit month         $M - two digit minute of hour\n"
"         $d - two digit day of month  $S - two digit second of minute\n"
"         $j - three digit day of year $v - a file version, see below\n"
"\n"
"      For example the pattern:\n"
"\n"
"         /data/ts2/$Y/$m/$d/ts2_l2_msc_bac_$Y$m$d_v$v.cdf\n"
"\n"
"      matches files such as:\n"
"\n"
"         /data/ts2/2025/08/04/ts2_l2_msc_bac_20250804_v1.3.1.cdf\n"
"\n"
"      Patterns are matched against the directory tree, so only files that\n"
"      exist are read.  When several files differ only in the $v field, the\n"
"      one with the greatest version is read and the others are ignored.\n"
"      Note that $j is exclusive with $m and $d within a single path\n"
"      component.  Quote the pattern to protect it from the shell.\n"
"\n"
"   BEGIN END\n"
"      The time range to stream as ISO-8601 strings, END exclusive.\n"
"      Trailing fields may be omitted when zero, so 2025-08-04 is midnight\n"
"      on that day.  This is the argument order das2 server configurations\n"
"      (*.dsdf files) supply, so a reader line of '" PROG " PATTERN' works\n"
"      unchanged there.  Records are compared against the range by their\n"
"      time variable, so a file that overlaps the range yields only the\n"
"      records within it.  With -V or -n, BEGIN END may be left off when\n"
"      PATTERN is a single file.\n"
"\n"
"   VAR\n"
"      The names of the CDF data variables to stream, as many as desired,\n"
"      separated by commas or spaces.  One component of a variable with a\n"
"      component axis is selected as VAR.COMPONENT, where COMPONENT is the\n"
"      label as written in the file (B_x), the bare direction symbol (x),\n"
"      or the component number counting from zero.  -V lists the names a\n"
"      file offers.  When no VAR is given the --def-vars list is used.  When\n"
"      that is empty too, every variable with VAR_TYPE=data that depends on\n"
"      the time variable is streamed.\n"
"\n"
"   What a das2 stream can hold\n"
"\n"
"   A das2 packet is a time value followed by scalar values and arrays, all\n"
"   sharing that one time.  The variables selected must therefore share a\n"
"   DEPEND_0 time variable and the same DEPEND chain; files with several\n"
"   time bases need VARs named to pick one.  Within that, the layout of the\n"
"   packet follows from the variables' shapes:\n"
"\n"
"      * Variables with only a record dimension become <y> planes, one\n"
"        per scalar and one per component of a vector, named VAR_LABEL.\n"
"\n"
"      * Variables with one more dimension that has a DEPEND_1 table, or\n"
"        an OFFSET_OF time offset table, become <yscan> planes with the\n"
"        table as yTags.  Evenly spaced time offsets are sent as a yTag\n"
"        series and the stream is marked as waveform data, the layout\n"
"        Autoplot reads for search coil and electric field waveforms.\n"
"\n"
"      * A vector variable with a time offset dimension, a record holding\n"
"        consecutive vector samples, does not fit a <yscan>, which holds\n"
"        one scalar.  It is unrolled: one packet per sample with the time\n"
"        of the sample in <x> and one <y> per component.  Name\n"
"        VAR.COMPONENT to get the <yscan> form for a single component\n"
"        instead.\n"
"\n"
"   Anything else is refused with a message naming the obstacle: more than\n"
"   one extra dimension, a DEPEND_1 table that varies by record, a vector\n"
"   over a dimension that is not a time offset, text values.  Uncertainty\n"
"   variables (DELTA_PLUS_VAR, DELTA_MINUS_VAR) have no das2 spelling and\n"
"   are not sent.  Times are sent as double precision microseconds since\n"
"   2000-01-01, data values as little endian reals of the source width.\n"
"\n"
"   Metadata\n"
"\n"
"   CDF global attributes become <stream> properties and CDF variable\n"
"   attributes become properties of the plane that holds the variable,\n"
"   prefixed with the plane's axis in the das2 manner (yLabel, zSummary).\n"
"   Attribute names are kept as-is except for the ISTP names that have a\n"
"   das equivalent, which are converted as follows.\n"
"\n"
"      CATDESC                -> summary\n"
"      FIELDNAM               -> title\n"
"      LABLAXIS               -> label\n"
"      VAR_NOTES              -> notes\n"
"      FILLVAL                -> (fill values are replaced by the das2 fill)\n"
"      FORMAT                 -> format\n"
"      SCALEMIN,SCALEMAX      -> scaleMin,scaleMax\n"
"      SCALETYP               -> scaleType\n"
"      VALIDMIN,VALIDMAX      -> validMin,validMax\n"
"      LIMITS_NOMINAL_MIN,MAX -> nominalMin,nominalMax\n"
"      LIMITS_WARN_MIN,MAX    -> warnMin,warnMax\n"
"      TEXT                   -> summary (global)\n"
"      TITLE                  -> title (global)\n"
"\n"
"   Missions that keep the same information under other attribute names can\n"
"   add to the table with --prop-map.\n"
"\n");

	printf(
"OPTIONS\n"
"   -h,--help     Write this text to standard output and exit.\n"
"\n"
"   -l LEVEL,--log=LEVEL\n"
"                 Set the logging level, where LEVEL is one of 'critical',\n"
"                 'error', 'warning', 'info', 'debug' in order of increasing\n"
"                 verbosity.  All log messages go to the standard error\n"
"                 channel.  Defaults to 'info'.\n"
"\n"
"   -c MAP,--coord=time:VAR\n"
"                 Name the variable inside each file that carries time,\n"
"                 overriding the built-in ISTP detection.  Needed for files\n"
"                 with no DEPEND_0 attributes, for example -c time:Timestamp.\n"
"\n"
"   -d VARS,--def-vars=VAR1[,VAR2 ...]\n"
"                 The data variables to stream when none are named on the\n"
"                 command line.  Server configurations use this to pick a\n"
"                 default product while leaving the client free to ask for\n"
"                 others.\n"
"\n"
"   -n,--no-op    Do not write a stream.  Instead list the files that match\n"
"                 PATTERN for the time range, then for the first file list\n"
"                 the variables that would be streamed, the variables that\n"
"                 would be ignored and why, and the das2 packet header that\n"
"                 would be sent.  Requested VARs are checked against the\n"
"                 file.\n"
"\n"
"   -p MAP,--prop-map=CDF_ATTR:DAS_PROP[,CDF_ATTR:DAS_PROP ...]\n"
"                 Extend the metadata table above.  Each entry names a CDF\n"
"                 attribute and the das property it should become.  This is\n"
"                 the same option das3_from_cdf and das3_cdf take.\n"
"\n"
"   -V,--vars     List the variables and components of the first matching\n"
"                 file that can be named as VAR, one line each with a short\n"
"                 description from the file, and exit.  Variables das2\n"
"                 cannot carry are listed apart.  The quick way to build an\n"
"                 invocation; -n is the full diagnostic.\n"
"\n");

	printf(
"EXAMPLES\n"
"   1. Stream one hour of TRACERS 2 search coil waveforms, x component, in\n"
"      spacecraft coordinates, as the <yscan> waveform layout:\n"
"\n"
"      " PROG " '/data/ts2/$Y/$m/$d/ts2_l2_msc_bac_$Y$m$d_v$v.cdf' \\\n"
"         2025-08-04T23:00 2025-08-05 ts2_l2_bac_tscs.B_x\n"
"\n"
"   2. See what a single file offers, then how one choice would be sent:\n"
"\n"
"      " PROG " -V ts2_l2_msc_bac_20250804_v1.3.1.cdf\n"
"      " PROG " -n ts2_l2_msc_bac_20250804_v1.3.1.cdf ts2_l2_bac_tscs.B_x\n"
"\n"
"   3. A day of 16 sample per second magnetometer vectors as text:\n"
"\n"
"      " PROG " '/data/ts2/$Y/$m/$d/ts2_l2_mag_bdc-16sps_$Y$m$d_v$v.cdf' \\\n"
"         2026-06-03 2026-06-04 ts2_l2_mag_16sps_tss_deltab | das2_ascii -r 4\n"
"\n");

	printf(
"LIMITATIONS\n"
"   * Time is the only coordinate this program can range on.  Variables\n"
"     that do not depend on it are not served; read the CDF directly for\n"
"     calibration tables and other constants.\n"
"   * CDF_EPOCH16 time variables are not supported and are skipped.\n"
"   * One packet type per stream.  Variables on different time bases or\n"
"     with different DEPEND chains cannot be sent together.\n"
"   * There is no per-variable override yet.  A map file for renaming,\n"
"     dropping and placing variables is planned.\n"
"\n");

	printf(
"MAINTAINER\n"
"   chris-piker@uiowa.edu\n"
"\n");

	printf(
"SEE ALSO\n"
"   * das3_from_cdf, das2_ascii, das2_bin_avgsec\n"
"   * ISTP CDF guidelines: https://spdf.gsfc.nasa.gov/istp_guide/istp_guide.html\n"
"\n");
}

/* ************************************************************************* */
/* Program options */

typedef struct program_options {
	char aLevel[32];
	char aDefVars[1024];   /* --def-vars, comma separated */
	char aPropMap[512];    /* --prop-map, "FROM:TO,FROM:TO" */
	char aCoordMap[512];   /* --coord, "time:VAR" */
	bool bNoOp;
	bool bVars;            /* -V: list the variables a user can name, then exit */
	const char* sPattern;  /* file name or URI pattern, points into argv */
	das_range   range;     /* the time range */
	const char* asVars[MAX_DATA_VARS];  /* point into argv */
	int         nVars;
} popts_t;

/* Argument errors happen before the stream is open; DasIO_exception_v()
   sends the stub header every das stream must start with. */
static int _argError(const char* sMsg)
{
	daslog_error_v("%s", sMsg);
	DasIO* pOut = new_DasIO(PROG, new_DasXport_stdout(), "2");
	DasIO_exception_v(pOut, DAS_EX_QUERY_ERR, "%s", sMsg);
	del_DasIO(pOut);
	return PERR;
}

/* Variable names arrive one per argument or comma joined in one argument,
   depending on which server built the command line.  Commas are cut in
   place; argv is ours to edit. */
static int _addVars(popts_t* pOpts, char* sArg)
{
	char* sTok = sArg;
	while(sTok != NULL){
		char* sNext = strchr(sTok, ',');
		if(sNext != NULL){ *sNext = '\0'; ++sNext; }
		if(*sTok != '\0'){
			if(pOpts->nVars >= MAX_DATA_VARS){
				char sMsg[128];
				snprintf(sMsg, sizeof(sMsg) - 1, "More than %d variables requested", MAX_DATA_VARS);
				return _argError(sMsg);
			}
			pOpts->asVars[pOpts->nVars] = sTok;
			++(pOpts->nVars);
		}
		sTok = sNext;
	}
	return DAS_OKAY;
}

int parseArgs(int argc, char** argv, popts_t* pOpts)
{
	memset(pOpts, 0, sizeof(popts_t));
	strcpy(pOpts->aLevel, "info");

	/* das_init has not run: keep library parse failures from exiting before
	   the client gets its exception packet */
	daserr_setdisp(DASERR_DIS_RET);

	char sMsg[256] = {'\0'};
	char* sBeg = NULL;
	char* sEnd = NULL;
	int nPos = 0;
	int i = 0;
	while(i < (argc-1)){
		++i;

		if(argv[i][0] == '-'){
			if(dascmd_isArg(argv[i], "-h", "--help", NULL)){
				prnHelp();
				exit(0);
			}
			if(dascmd_isArg(argv[i], "-n", "--no-op", NULL)){
				pOpts->bNoOp = true;
				continue;
			}
			if(dascmd_isArg(argv[i], "-V", "--vars", NULL)){
				pOpts->bVars = true;
				continue;
			}
			if(dascmd_getArgVal(
				argv, argc, "-c", "--coord=", &i, pOpts->aCoordMap, DAS_FIELD_SZ(popts_t, aCoordMap)
			))
				continue;
			if(dascmd_getArgVal(
				argv, argc, "-l", "--log=", &i, pOpts->aLevel, DAS_FIELD_SZ(popts_t, aLevel)
			))
				continue;
			if(dascmd_getArgVal(
				argv, argc, "-d", "--def-vars=", &i, pOpts->aDefVars, DAS_FIELD_SZ(popts_t, aDefVars)
			))
				continue;
			if(dascmd_getArgVal(
				argv, argc, "-p", "--prop-map=", &i, pOpts->aPropMap, DAS_FIELD_SZ(popts_t, aPropMap)
			))
				continue;
			snprintf(sMsg, sizeof(sMsg) - 1, "Unknown command line argument %s", argv[i]);
			return _argError(sMsg);
		}

		/* Positionals: PATTERN BEGIN END, then VARs */
		if(nPos == 0)      pOpts->sPattern = argv[i];
		else if(nPos == 1) sBeg = argv[i];
		else if(nPos == 2) sEnd = argv[i];
		else if(_addVars(pOpts, argv[i]) != DAS_OKAY) return PERR;
		++nPos;
	}

	if(nPos < 1)
		return _argError("Expected PATTERN BEGIN END, use -h for help");

	/* The diagnostics can inspect a lone file without a time range, in
	   which case the positionals after PATTERN are all VARs.  A pattern has
	   fields to fill in and a stream needs its bounds, so they need the
	   range. */
	bool bDiag = (pOpts->bVars || pOpts->bNoOp) && (strchr(pOpts->sPattern, '$') == NULL);
	/* das2 servers always send both bounds, so a half-open range is refused */
	bool bHaveRange = (nPos >= 3) && (das_range_fromUtc(&(pOpts->range), sBeg, sEnd) == DAS_OKAY)
		&& das_range_hasBeg(&(pOpts->range)) && das_range_hasEnd(&(pOpts->range));
	if(bHaveRange)
		return DAS_OKAY;
	if(!bDiag){
		if(nPos < 3)
			return _argError("Expected PATTERN BEGIN END, use -h for help");
		snprintf(sMsg, sizeof(sMsg) - 1, "Could not parse the time range %s to %s", sBeg, sEnd);
		return _argError(sMsg);
	}

	/* unbounded: dmBeg stays null; the 2nd and 3rd positionals were
	   variables all along and came before the ones already added */
	das_range_init(&(pOpts->range));
	int nLater = pOpts->nVars;
	const char* asLater[MAX_DATA_VARS];
	memcpy((void*)asLater, pOpts->asVars, nLater * sizeof(const char*));  /* quiet msvc */
	pOpts->nVars = 0;
	if((sBeg != NULL)&&(_addVars(pOpts, sBeg) != DAS_OKAY)) return PERR;
	if((sEnd != NULL)&&(_addVars(pOpts, sEnd) != DAS_OKAY)) return PERR;
	for(int j = 0; j < nLater; ++j){
		if(pOpts->nVars >= MAX_DATA_VARS)
			return _argError("Too many variables requested");
		pOpts->asVars[pOpts->nVars] = asLater[j];
		++(pOpts->nVars);
	}
	return DAS_OKAY;
}

/* ************************************************************************* */
/* Logging.  Everything goes to stderr, errors also reach the client: as
   "log:error" comments while the stream carries on, or as the exception
   that ends it.  A log handler may not call back into das2C, so the handler
   only keeps the text and main writes it into the stream at safe points,
   see _flushLog() and _fatal().

   The library level is held at ERROR or below so the handler always hears
   about errors, even with -l crit; the user's level only decides what is
   printed and sent as comments. */

static DasIO* g_pIoOut = NULL;
static DasStream* g_pSd = NULL;
static das_except_e g_exType = DAS_EX_QUERY_ERR;

static int  g_nUserLevel = DASLOG_INFO;

#define LOG_QUEUE 8
static char g_aQueue[LOG_QUEUE][DASLOG_MSG_SZ];
static int  g_nQueued  = 0;
static int  g_nDropped = 0;
static char g_sLastErr[DASLOG_MSG_SZ] = {'\0'};

static void logHandler(const das_logmsg* pMsg, void* pUser)
{
	(void)pUser;
	
	/* The last error is kept whatever the level, it becomes the exception */
	if(pMsg->nLevel >= DASLOG_ERROR)
		snprintf(g_sLastErr, DASLOG_MSG_SZ, "%s", pMsg->sMsg);
	
	if(pMsg->nLevel < g_nUserLevel) return;
	daslog_stderr(pMsg, NULL);
	
	if(pMsg->nLevel < DASLOG_ERROR) return;
	if(g_nQueued < LOG_QUEUE)
		snprintf(g_aQueue[g_nQueued++], DASLOG_MSG_SZ, "%s", pMsg->sMsg);
	else
		++g_nDropped;
}

static void _sendHeader(void)
{
	if(!DasIO_headerSent(g_pIoOut))
		DasIO_writeStreamDesc(g_pIoOut, g_pSd);
}

/* Send queued errors to the client as comments, the stream carries on.
   Before the stream is open they have only been to stderr. */
static void _flushLog(void)
{
	if(g_pIoOut != NULL){
		if(g_nQueued > 0) _sendHeader();
		for(int i = 0; i < g_nQueued; ++i){
			if(DasIO_comment_v(g_pIoOut, "log:error", "%s", g_aQueue[i]) != DAS_OKAY){
				/* No way left to tell the client anything */
				del_DasIO(g_pIoOut);
				exit(PERR);
			}
		}
		if(g_nDropped > 0)
			DasIO_comment_v(g_pIoOut, "log:error", "%d more errors, see stderr",
			                g_nDropped);
	}
	g_nQueued = 0;
	g_nDropped = 0;
}

/* End the program on an error.  The last error is the exception that ends
   the stream, any earlier ones go first as comments. */
static void _fatal(void)
{
	const char* sMsg = (g_sLastErr[0] != '\0') ? g_sLastErr :
	                   "das library error, no message available";
	
	if(g_pIoOut != NULL){
		if((g_nQueued > 0)&&(strcmp(g_aQueue[g_nQueued - 1], g_sLastErr) == 0))
			--g_nQueued;
		_flushLog();
		_sendHeader();
		DasIO_exception_v(g_pIoOut, g_exType, "%s", sMsg);
		DasIO_close(g_pIoOut);
		del_DasIO(g_pIoOut);
		g_pIoOut = NULL;
	}
	exit(PERR);
}

/* Requires a local nDasStatus */
#define DAS_EXIT( SOME_DAS_FUNC ) do{ \
	if( (nDasStatus = (SOME_DAS_FUNC )) != DAS_OKAY) _fatal(); else _flushLog(); \
}while(0)

/* ************************************************************************* */
/* The packet layout.  Decided once from the first file's classification,
   then reused for every file that matches its structure. */

typedef enum layout { LAYOUT_Y = 1, LAYOUT_YSCAN, LAYOUT_UNROLL } layout_e;

/* One CDF variable feeding one or more planes */
typedef struct var_src {
	char       sName[CVAR_NAME_SZ];
	long       nType;
	int        nComps;             /* planes made from it, 1 for a scalar or yscan */
	int        iCompDim;           /* CDF dim of the components, -1 */
	int        nDims;              /* CDF dims in the read window */
	long       aCount[CDF_MAX_DIMS];
	double     rFill;              /* the file's fill, replaced on the way out */
	DasPlane* aPlanes[MAX_COMPS]; /* owned by the packet descriptor */
} var_src_t;

typedef struct pkt_layout {
	layout_e   mode;
	DasPkt*   pPkt;
	DasPlane* pX;
	char       sTime[CVAR_NAME_SZ];  /* the time base */
	long       nTimeType;
	int        nSrcs;
	var_src_t  aSrcs[MAX_CDF_VARS];

	/* yscan and unroll: the second index */
	long       nItems;
	int        iItemDim;             /* CDF dim of the second index on the data vars */
	bool       bTimeOffset;          /* the table is a time offset of the record time */
	double*    pOffSec;              /* nItems offsets in seconds, unroll only */
} pkt_layout_t;

static pkt_layout_t g_layout;

/* The dimension name a member streams under: the variable name less the
   dataset's common prefix, as das3_from_cdf does it */
static const char* _dimName(const cdf_ds_t* pDs, const cdf_var_t* pV)
{
	size_t uPre = strlen(pDs->sName);
	const char* sDim = pV->sName;
	if((uPre > 0)&&(strncmp(sDim, pDs->sName, uPre) == 0)&&(sDim[uPre] == '_')&&(sDim[uPre+1] != '\0'))
		sDim += uPre + 1;
	return sDim;
}

/* The fill value of a variable as a double */
static double _fillAsDouble(cdf_file_t* pFile, const cdf_var_t* pV)
{
	ubyte aBuf[16];
	const ubyte* p = cdf_varFill(pFile, pV, aBuf);
	switch(cdf_valType(pV->nType)){
	case vtByte:   return *((const int8_t*)p);
	case vtUByte:  return *((const uint8_t*)p);
	case vtShort:  return *((const int16_t*)p);
	case vtUShort: return *((const uint16_t*)p);
	case vtInt:    return *((const int32_t*)p);
	case vtUInt:   return *((const uint32_t*)p);
	case vtLong:   return (double)*((const int64_t*)p);
	case vtFloat:  return *((const float*)p);
	case vtDouble: return *((const double*)p);
	default:       return DAS_FILL_VALUE;
	}
}

/* One element of a raw CDF buffer as a double */
static double _elemAsDouble(long nType, const ubyte* p)
{
	switch(cdf_valType(nType)){
	case vtByte:   return *((const int8_t*)p);
	case vtUByte:  return *((const uint8_t*)p);
	case vtShort:  return *((const int16_t*)p);
	case vtUShort: return *((const uint16_t*)p);
	case vtInt:    return *((const int32_t*)p);
	case vtUInt:   return *((const uint32_t*)p);
	case vtLong:   return (double)*((const int64_t*)p);
	case vtFloat:  return *((const float*)p);
	case vtDouble: return *((const double*)p);
	default:       return DAS_FILL_VALUE;
	}
}

/* TT2000 to the das2 wire time, microseconds since 2000 */
static double _tt2kToUs2k(int64_t nTt)
{
	das_time dt;
	dt_from_tt2k(&dt, nTt);
	return Units_convertFromDt(&dt, UNIT_US2000);
}

/* Data values go out as little endian reals of the source width */
static DasEnc* _valueEncoding(long nType)
{
	size_t uSz = das_vt_size(cdf_valType(nType));
	return new_DasEnc(DAS2DT_LE_REAL, (uSz > 4) ? 8 : 4, NULL);
}

/* Copy a variable's attributes onto a plane as das2 properties.  Known das
   keys take the plane's axis prefix (label -> yLabel); other attributes
   keep their names. */
static void _addPlaneProps(cdf_file_t* pFile, const cdf_var_t* pV, DasPlane* pPlane, char cAxis)
{
	static const char* asPrefixed[] = {
		"label", "summary", "notes", "format", "scaleMin", "scaleMax", "scaleType",
		"validMin", "validMax", "nominalMin", "nominalMax", "warnMin", "warnMax", NULL
	};
	long nAttrs = 0;
	if(CDFgetNumAttributes(pFile->id, &nAttrs) != CDF_OK) return;
	char sName[CDF_ATTR_NAME_LEN256 + 1];
	char sVal[4096];
	char sProp[80];
	for(long i = 0; i < nAttrs; ++i){
		long nScope = 0;
		if(CDFgetAttrScope(pFile->id, i, &nScope) != CDF_OK) continue;
		if(nScope != VARIABLE_SCOPE) continue;
		if(CDFconfirmzEntryExistence(pFile->id, i, pV->nVarNum) != CDF_OK) continue;
		if(CDFgetAttrName(pFile->id, i, sName) != CDF_OK) continue;
		if(cdf_attrConsumed(pV, sName)) continue;
		if(strcmp(sName, "FIELDNAM") == 0) continue;   /* the plane name carries it */

		long nType = 0;
		if(CDFgetAttrzEntryDataType(pFile->id, i, pV->nVarNum, &nType) != CDF_OK) continue;
		if(!cdf_varAttrStr(pFile->id, pV->nVarNum, sName, sVal, sizeof(sVal))) continue;

		const char* sKey = cdf_propName(sName);
		if(strcmp(sKey, "frame") == 0) continue;
		bool bPrefix = false;
		for(int k = 0; asPrefixed[k] != NULL; ++k)
			if(strcmp(sKey, asPrefixed[k]) == 0){ bPrefix = true; break; }
		if(bPrefix){
			snprintf(sProp, sizeof(sProp), "%c%c%s", cAxis, toupper((unsigned char)sKey[0]), sKey + 1);
			sKey = sProp;
		}

		if((nType == CDF_CHAR)||(nType == CDF_UCHAR))
			DasDesc_setStr((DasDesc*)pPlane, sKey, sVal);
		else if(cdf_isTimeType(nType))
			DasDesc_set((DasDesc*)pPlane, "datetime", sKey, sVal);
		/* As text, the way cdf_addVarProps() does it: atoi() truncated every
		   CDF_INT8 (and any CDF_UINT4 past INT_MAX) to 32 bits */
		else if(das_vt_isreal(cdf_valType(nType)))
			DasDesc_set((DasDesc*)pPlane, "real", sKey, sVal);
		else
			DasDesc_set((DasDesc*)pPlane, "integer", sKey, sVal);
	}
}

/* The second index of a rank 2 dataset: which CDF dim it is on the first
   member and which variable holds its values (-1 for none) */
static int _secondIndex(cdf_file_t* pFile, const cdf_var_t* pFirst, int* pDim)
{
	for(int d = 1; d <= pFirst->nDims; ++d){
		if(pFirst->asDepend[d][0] == '\0') continue;
		*pDim = d - 1;
		return cdf_varIndex(pFile, pFirst->asDepend[d]);
	}
	*pDim = -1;
	return -1;
}

/* Read a non record varying rank 1 table as doubles, in its own units */
static double* _readTable(cdf_file_t* pFile, const cdf_var_t* pV)
{
	size_t uSz = das_vt_size(cdf_valType(pV->nType));
	ubyte* pRaw = (ubyte*)malloc(uSz * pV->aDimSz[0]);
	double* pOut = (double*)malloc(sizeof(double) * pV->aDimSz[0]);
	if((pRaw == NULL)||(pOut == NULL)||(CDFgetzVarRecordData(pFile->id, pV->nVarNum, 0, pRaw) != CDF_OK)){
		free(pRaw); free(pOut);
		return NULL;
	}
	for(long i = 0; i < pV->aDimSz[0]; ++i) pOut[i] = _elemAsDouble(pV->nType, pRaw + i * uSz);
	free(pRaw);
	return pOut;
}

/* Decide the layout for the one dataset and build its packet descriptor.
   Refusals are das errors naming the obstacle. */
static int _planLayout(cdf_file_t* pFile, pkt_layout_t* pL)
{
	memset(pL, 0, sizeof(pkt_layout_t));

	if(pFile->nDs != 1){
		char sList[2048] = {'\0'};
		for(int j = 0; j < pFile->nDs; ++j){
			const cdf_ds_t* pDs = pFile->aDs + j;
			size_t u = strlen(sList);
			snprintf(sList + u, sizeof(sList) - u, "%s  %s:", (j > 0) ? "\n" : "", pDs->sName);
			for(int m = 0; m < pDs->nMembers; ++m){
				u = strlen(sList);
				snprintf(sList + u, sizeof(sList) - u, " %s", pFile->aVars[pDs->aMembers[m]].sName);
			}
		}
		if(pFile->nDs == 0)
			return das_error(
				PERR, "Nothing to stream from %s: no data variable depends on a time base",
				pFile->sPath
			);
		return das_error(
			PERR, "A das2 stream holds one packet type but %s has %d time dependent "
			"datasets; name the variables of one of them:\n%s", 
			pFile->sPath, pFile->nDs, sList
		);
	}

	cdf_ds_t* pDs = pFile->aDs;
	cdf_var_t* pFirst = pFile->aVars + pDs->aMembers[0];
	cdf_var_t* pT = pFile->aVars + pDs->iTime;
	strncpy(pL->sTime, pT->sName, CVAR_NAME_SZ - 1);
	pL->nTimeType = pT->nType;

	if(pDs->nRank > 2)
		return das_error(
			PERR, "%s depends on %d coordinates; a das2 packet holds time and at most "
			"one more", pFirst->sName, pDs->nRank
		);

	/* the second index and its table */
	cdf_var_t* pC = NULL;
	int iItemDim = -1;
	if(pDs->nRank == 2){
		int iC = _secondIndex(pFile, pFirst, &iItemDim);
		if(iC >= 0) pC = pFile->aVars + iC;
		if((pC != NULL) && pC->bRecVary)
			return das_error(PERR, "%s varies by record; das2 yTags are fixed for the stream, "
				"so %s cannot be sent as a <yscan>", pC->sName, pFirst->sName);
		if((pC != NULL) && (pC->nDims != 1))
			return das_error(PERR, "%s is a rank %ld table; only rank 1 tables become yTags",
				pC->sName, pC->nDims);
		pL->nItems = pFirst->aDimSz[iItemDim];
		pL->iItemDim = iItemDim;
		pL->bTimeOffset = (pC != NULL) && (pC->role == ROLE_OFFSET);
	}

	/* any multi component member on a rank 2 dataset forces the unroll */
	bool bMulti = false;
	for(int m = 0; m < pDs->nMembers; ++m)
		if(pFile->aVars[pDs->aMembers[m]].nComps > 1) bMulti = true;

	if(pDs->nRank == 1)
		pL->mode = LAYOUT_Y;
	else if(!bMulti)
		pL->mode = LAYOUT_YSCAN;
	else if(pL->bTimeOffset)
		pL->mode = LAYOUT_UNROLL;
	else
		return das_error(PERR, "%s has components along one index and %s along another; a das2 "
			"<yscan> holds one scalar.  Name one component, as %s.%s, to send it as a <yscan>",
			pFirst->sName, (pC != NULL) ? pC->sName : "an unlabeled table", pFirst->sName,
			(pFirst->nLabels > 0) ? pFirst->aLabels[0] : "0");

	/* the second index table: a series when evenly spaced, else a list */
	double* pTable = NULL;
	bool bSeries = false;
	double rMin = 0.0, rStep = 0.0;
	if(pC != NULL){
		bSeries = cdf_isSequence(pFile, pC, &rMin, &rStep);
		pTable = bSeries ? (double*)malloc(sizeof(double) * pL->nItems) : _readTable(pFile, pC);
		if(pTable == NULL) return das_error(PERR, "Could not read table %s", pC->sName);
		if(bSeries) for(long i = 0; i < pL->nItems; ++i) pTable[i] = rMin + rStep * i;
	}
	das_units unitsC = (pC != NULL) ? cdf_varUnits(pC) : UNIT_DIMENSIONLESS;
	if(pL->bTimeOffset){
		if(!Units_canConvert(unitsC, UNIT_SECONDS)){
			free(pTable);
			return das_error(PERR, "%s is an OFFSET_OF time table but its units, %s, are not a "
				"time interval", pC->sName, Units_toStr(unitsC));
		}
		for(long i = 0; i < pL->nItems; ++i) pTable[i] = Units_convertTo(pTable[i], unitsC, UNIT_SECONDS);
		rMin  = Units_convertTo(rMin, unitsC, UNIT_SECONDS);
		rStep = Units_convertTo(rStep, unitsC, UNIT_SECONDS);
		unitsC = UNIT_SECONDS;
	}

	/* the packet: time first */
	pL->pPkt = new_DasPkt();
	pL->pX = new_DasPlane(DASPLANE_X, "", new_DasEnc(DAS2DT_LE_REAL, 8, NULL), UNIT_US2000);
	DasPkt_addPlane(pL->pPkt, pL->pX);

	bool bDeltas = false;
	for(int m = 0; m < pDs->nMembers; ++m){
		cdf_var_t* pV = pFile->aVars + pDs->aMembers[m];
		var_src_t* pS = pL->aSrcs + pL->nSrcs;
		strncpy(pS->sName, pV->sName, CVAR_NAME_SZ - 1);
		pS->nType = pV->nType;
		pS->iCompDim = pV->iCompDim;
		pS->nDims = (int)pV->nDims;
		for(int d = 0; d < pV->nDims; ++d) pS->aCount[d] = pV->aDimSz[d];
		pS->rFill = _fillAsDouble(pFile, pV);
		if((pV->sDeltaPlus[0] != '\0')||(pV->sDeltaMinus[0] != '\0')) bDeltas = true;

		const char* sDim = _dimName(pDs, pV);
		das_units units = cdf_varUnits(pV);
		char sPlane[CVAR_NAME_SZ + 40];

		if(pL->mode == LAYOUT_YSCAN){
			pS->nComps = 1;
			DasPlane* pP = NULL;
			if(bSeries)
				pP = new_DasPlane_yscan_seq(sDim, _valueEncoding(pV->nType), units, pL->nItems,
					rStep, rMin, DAS_FILL_VALUE, unitsC);
			else if(pTable != NULL)
				pP = new_DasPlane_yscan(sDim, _valueEncoding(pV->nType), units, pL->nItems, NULL, pTable, unitsC);
			else
				pP = new_DasPlane_yscan(sDim, _valueEncoding(pV->nType), units, pL->nItems, NULL, NULL, UNIT_DIMENSIONLESS);
			if(pP == NULL){ free(pTable); return PERR; }
			_addPlaneProps(pFile, pV, pP, 'z');
			if(pC != NULL){
				char sLbl[256];
				if(cdf_varAttrStr(pFile->id, pC->nVarNum, "LABLAXIS", sLbl, sizeof(sLbl)) && (sLbl[0] != '\0'))
					DasDesc_setStr((DasDesc*)pP, "yLabel", sLbl);
			}
			DasDesc_setDouble((DasDesc*)pP, "zFill", DAS_FILL_VALUE);
			DasPkt_addPlane(pL->pPkt, pP);
			pS->aPlanes[0] = pP;
		}
		else{
			pS->nComps = pV->nComps;
			if(pS->nComps > MAX_COMPS){ free(pTable); return das_error(PERR, "%s has more than %d components", pV->sName, MAX_COMPS); }
			for(int c = 0; c < pS->nComps; ++c){
				if(pS->nComps == 1)
					snprintf(sPlane, sizeof(sPlane), "%s", sDim);
				else if(c < pV->nLabels)
					snprintf(sPlane, sizeof(sPlane), "%s_%s", sDim, pV->aLabels[c]);
				else
					snprintf(sPlane, sizeof(sPlane), "%s_%d", sDim, c);
				DasPlane* pP = new_DasPlane(DASPLANE_Y, sPlane, _valueEncoding(pV->nType), units);
				if(pP == NULL){ free(pTable); return PERR; }
				_addPlaneProps(pFile, pV, pP, 'y');
				/* a component's label is its own, the variable's label describes the set */
				if((pS->nComps > 1)&&(c < pV->nLabels)){
					if(DasDesc_has((DasDesc*)pP, "yLabel"))
						DasDesc_setStr((DasDesc*)pP, "ySummary", DasDesc_getStr((DasDesc*)pP, "yLabel"));
					DasDesc_setStr((DasDesc*)pP, "yLabel", pV->aLabels[c]);
					if(pV->sFrame[0] != '\0') DasDesc_setStr((DasDesc*)pP, "yFrame", pV->sFrame);
				}
				DasDesc_setDouble((DasDesc*)pP, "yFill", DAS_FILL_VALUE);
				DasPkt_addPlane(pL->pPkt, pP);
				pS->aPlanes[c] = pP;
			}
		}
		++(pL->nSrcs);
	}
	if(bDeltas)
		daslog_warn("DELTA_PLUS_VAR and DELTA_MINUS_VAR uncertainties have no das2 spelling and are not sent");

	if(pL->mode == LAYOUT_UNROLL){
		pL->pOffSec = pTable;
		daslog_info_v("%s: %ld samples per record are sent as separate packets", pFirst->sName, pL->nItems);
	}
	else
		free(pTable);

	return DAS_OKAY;
}

/* Stream level properties the layout implies */
static void _addStreamHints(const pkt_layout_t* pL, DasStream* pSd)
{
	if((pL->mode == LAYOUT_YSCAN) && pL->bTimeOffset)
		DasDesc_setStr((DasDesc*)pSd, "renderer", "waveform");
	DasDesc_setStr((DasDesc*)pSd, "xLabel", "Time (UTC)");
}

/* ************************************************************************* */
/* Streaming records */

#define MAX_BLOCK_BYTES 16777216   /* per variable, per read */

/* Fill in the file replaced by the das2 fill */
static inline double _outVal(double r, double rFill)
{
	return (r == rFill) ? DAS_FILL_VALUE : r;
}

/* Send the one dataset's records from an open file, in blocks, keeping
   records whose time base falls in the range */
static int _streamFile(cdf_file_t* pFile, pkt_layout_t* pL, DasIO* pIo, const das_range* pRng, long* pnPkts)
{
	int iT = cdf_varIndex(pFile, pL->sTime);
	if(iT < 0) return das_error(PERR, "%s has no %s", pFile->sPath, pL->sTime);
	cdf_var_t* pT = pFile->aVars + iT;
	long nRecs = pT->nRecs;
	if(nRecs < 1) return DAS_OKAY;

	das_time dtBeg, dtEnd;
	das_datum_toTime(&(pRng->dmBeg), &dtBeg);
	das_datum_toTime(&(pRng->dmEnd), &dtEnd);
	int64_t nBeg = dt_to_tt2k(&dtBeg);
	int64_t nEnd = dt_to_tt2k(&dtEnd);

	/* readers for this file, by name */
	rec_reader_t aRd[MAX_CDF_VARS];
	long nMaxBytesPerRec = 8;
	for(int s = 0; s < pL->nSrcs; ++s){
		int iV = cdf_varIndex(pFile, pL->aSrcs[s].sName);
		if(iV < 0) return das_error(PERR, "%s has no %s", pFile->sPath, pL->aSrcs[s].sName);
		long n = cdf_initReader(pFile, iV, aRd + s) * (long)das_vt_size(cdf_valType(pL->aSrcs[s].nType));
		if(n > nMaxBytesPerRec) nMaxBytesPerRec = n;
	}
	long nBlock = MAX_BLOCK_BYTES / nMaxBytesPerRec;
	if(nBlock < 1) nBlock = 1;

	bool* pKeep = (bool*)malloc(nBlock);
	double* pUs = (double*)malloc(nBlock * sizeof(double));
	ubyte** apBuf = (ubyte**)calloc(pL->nSrcs, sizeof(ubyte*));
	if((pKeep == NULL)||(pUs == NULL)||(apBuf == NULL)){ free(pKeep); free(pUs); free(apBuf); return PERR; }

	int nRet = DAS_OKAY;
	for(long nRec0 = 0; (nRec0 < nRecs)&&(nRet == DAS_OKAY); nRec0 += nBlock){
		long nHere = ((nRecs - nRec0) < nBlock) ? (nRecs - nRec0) : nBlock;

		/* the time base first, to decide what is kept */
		size_t uTSz = (pT->nType == CDF_EPOCH) ? sizeof(double) : sizeof(int64_t);
		rec_reader_t rdT = { .iVar = iT, .nElemsPerRec = 1, .bTime = true, .nDims = 0 };
		ubyte* pTBuf = cdf_readBlock(pFile, pT->nVarNum, &rdT, nRec0, nHere, uTSz);
		if(pTBuf == NULL){ nRet = PERR; break; }
		long nKept = 0;
		for(long i = 0; i < nHere; ++i){
			int64_t nVal = (pT->nType == CDF_EPOCH) ?
				CDF_TT2000_from_UTC_EPOCH(((const double*)pTBuf)[i]) : ((const int64_t*)pTBuf)[i];
			pKeep[i] = (nVal >= nBeg)&&(nVal < nEnd);
			if(pKeep[i]){ pUs[i] = _tt2kToUs2k(nVal); ++nKept; }
		}
		if(nRec0 == 0)
			daslog_debug_v("range %" PRId64 " to %" PRId64 " TT2000, first record %" PRId64 ", %ld of %ld kept in block 0",
				nBeg, nEnd, (int64_t)((pT->nType == CDF_EPOCH) ? CDF_TT2000_from_UTC_EPOCH(((const double*)pTBuf)[0]) : ((const int64_t*)pTBuf)[0]), nKept, nHere);
		free(pTBuf);
		if(nKept == 0) continue;

		for(int s = 0; (s < pL->nSrcs)&&(nRet == DAS_OKAY); ++s){
			size_t uSz = das_vt_size(cdf_valType(pL->aSrcs[s].nType));
			int iV = aRd[s].iVar;
			apBuf[s] = cdf_readBlock(pFile, pFile->aVars[iV].nVarNum, aRd + s, nRec0, nHere, uSz);
			if(apBuf[s] == NULL) nRet = PERR;
		}

		for(long i = 0; (i < nHere)&&(nRet == DAS_OKAY); ++i){
			if(!pKeep[i]) continue;

			if(pL->mode != LAYOUT_UNROLL){
				DasPlane_setValue(pL->pX, 0, pUs[i]);
				for(int s = 0; s < pL->nSrcs; ++s){
					var_src_t* pS = pL->aSrcs + s;
					size_t uSz = das_vt_size(cdf_valType(pS->nType));
					const ubyte* pRec = apBuf[s] + i * uSz * aRd[s].nElemsPerRec;
					if(pL->mode == LAYOUT_YSCAN){
						/* the one scalar per item; a selected component rides at
						   offset 0 of its (width 1) axis */
						for(long k = 0; k < pL->nItems; ++k)
							DasPlane_setValue(pS->aPlanes[0], k,
								_outVal(_elemAsDouble(pS->nType, pRec + k * uSz), pS->rFill));
					}
					else{
						for(int c = 0; c < pS->nComps; ++c)
							DasPlane_setValue(pS->aPlanes[c], 0,
								_outVal(_elemAsDouble(pS->nType, pRec + c * uSz), pS->rFill));
					}
				}
				nRet = DasIO_writeData(pIo, (DasDesc*)pL->pPkt, 1);   /* the only packet, see main */
				if(nRet == DAS_OKAY) ++(*pnPkts);
			}
			else{
				/* one packet per sample; element (k, c) or (c, k) by which CDF
				   dim carries the components */
				for(long k = 0; (k < pL->nItems)&&(nRet == DAS_OKAY); ++k){
					DasPlane_setValue(pL->pX, 0, pUs[i] + pL->pOffSec[k] * 1e6);
					for(int s = 0; s < pL->nSrcs; ++s){
						var_src_t* pS = pL->aSrcs + s;
						size_t uSz = das_vt_size(cdf_valType(pS->nType));
						const ubyte* pRec = apBuf[s] + i * uSz * aRd[s].nElemsPerRec;
						bool bCompLast = (pS->iCompDim > pL->iItemDim);
						long nInner = bCompLast ? pS->aCount[pS->iCompDim] : pL->nItems;
						for(int c = 0; c < pS->nComps; ++c){
							long iEl = bCompLast ? (k * nInner + c) : (c * nInner + k);
							DasPlane_setValue(pS->aPlanes[c], 0,
								_outVal(_elemAsDouble(pS->nType, pRec + iEl * uSz), pS->rFill));
						}
					}
					nRet = DasIO_writeData(pIo, (DasDesc*)pL->pPkt, 1);   /* the only packet, see main */
					if(nRet == DAS_OKAY) ++(*pnPkts);
				}
			}
		}
		for(int s = 0; s < pL->nSrcs; ++s){ free(apBuf[s]); apBuf[s] = NULL; }
	}
	free(pKeep);
	free(pUs);
	free(apBuf);
	return nRet;
}

/* ************************************************************************* */

/* Walk the matching files, printing them when asked, and return how many
   there were with the first one's path in sFirst */
static int _firstFile(DasUriIter* pIter, char* sFirst, size_t uLen, bool bPrint)
{
	char sFile[DURI_MAX_PATH];
	int nFiles = 0;
	if(bPrint) printf("Files:\n");
	while(DasUriIter_next(pIter, sFile, sizeof(sFile)) != NULL){
		if(bPrint) printf("   %s\n", sFile);
		if(nFiles == 0) strncpy(sFirst, sFile, uLen - 1);
		++nFiles;
	}
	if(bPrint && (nFiles == 0)) printf("   (none)\n");
	return nFiles;
}

int main(int argc, char** argv)
{
	popts_t opts;
	if(parseArgs(argc, argv, &opts) != DAS_OKAY)
		return PERR;

	/* The library level stays at ERROR or below, see logHandler() */
	g_nUserLevel = daslog_strlevel(opts.aLevel);
	if(g_nUserLevel < 0) g_nUserLevel = DASLOG_INFO;
	das_loghook hook = {logHandler, NULL};
	das_init(argv[0], DASERR_DIS_RET,
	         (g_nUserLevel > DASLOG_ERROR) ? DASLOG_ERROR : g_nUserLevel, &hook);

	DasErrCode nDasStatus = DAS_OKAY;   /* for DAS_EXIT */

	DasUriTplt* pTplt = new_DasUriTplt();
	DasUriTplt_register(pTplt, das_time_uridef());
	DAS_EXIT( DasUriTplt_pattern(pTplt, opts.sPattern) );

	DasUriIter iter;
	das_dim_range drTime;
	das_dim_range_init(&drTime, "time", &(opts.range));
	DAS_EXIT( init_DasUriIter(&iter, pTplt, 1, &drTime) );

	DAS_EXIT( cdf_parsePropMap(opts.aPropMap) );

	bool bBounded = das_range_hasBeg(&(opts.range)) && das_range_hasEnd(&(opts.range));
	cdf_select_t sel = {
		.sCoordMap = opts.aCoordMap, .sDefVars = opts.aDefVars,
		.asVars = opts.asVars, .nVars = opts.nVars,
		.pTimeRng = bBounded ? &(opts.range) : NULL, .bDataOnly = true
	};

	if(opts.bVars){
		/* every time dependent variable is a candidate, so select them all */
		cdf_select_t selAll = { .sCoordMap = opts.aCoordMap, .sDefVars = "", .asVars = NULL };
		char sFirst[DURI_MAX_PATH] = {'\0'};
		int nFiles = _firstFile(&iter, sFirst, sizeof(sFirst), false);
		fini_DasUriIter(&iter);
		del_DasUriTplt(pTplt);
		if(nFiles == 0){ printf("No files match %s\n", opts.sPattern); return 0; }
		cdf_file_t* pFile = (cdf_file_t*)calloc(1, sizeof(cdf_file_t));
		int nRet = cdf_openAndClassify(pFile, sFirst, &selAll);
		if(nRet == DAS_OKAY){
			cdf_listVars(pFile, 2, "Not compatible with das2, try das3_from_cdf");
			CDFcloseCDF(pFile->id);
		}
		free(pFile);
		return (nRet == DAS_OKAY) ? 0 : PERR;
	}

	if(opts.bNoOp){
		printf("Pattern: %s\n", opts.sPattern);
		if(bBounded){
			char sBeg[64] = {'\0'};
			char sEnd[64] = {'\0'};
			das_datum_toStr(&(opts.range.dmBeg), 6, sBeg, sizeof(sBeg));
			das_datum_toStr(&(opts.range.dmEnd), 6, sEnd, sizeof(sEnd));
			printf("Range: time from %s to %s\n", sBeg, sEnd);
		}
		else
			printf("Range: time, bounds from the file\n");
		if(opts.nVars > 0){
			printf("Requested:");
			for(int i = 0; i < opts.nVars; ++i) printf(" %s", opts.asVars[i]);
			printf("\n");
		}
		char sFirst[DURI_MAX_PATH] = {'\0'};
		int nFiles = _firstFile(&iter, sFirst, sizeof(sFirst), true);
		fini_DasUriIter(&iter);
		del_DasUriTplt(pTplt);
		if(nFiles == 0) return 0;

		/* Inspect the first file only, the rest are assumed to match */
		cdf_file_t* pFile = (cdf_file_t*)calloc(1, sizeof(cdf_file_t));
		int nRet = cdf_openAndClassify(pFile, sFirst, &sel);
		if(nRet == DAS_OKAY){
			cdf_listFile(pFile, &sel);
			printf("\ndas2 packet header\n------------------\n");
			g_sLastErr[0] = '\0';   /* so a refusal below reports its own reason */
			if(_planLayout(pFile, &g_layout) == DAS_OKAY){
				DasBuf* pBuf = new_DasBuf(65536);
				size_t uHdr = 0;
				const ubyte* pHdr = NULL;
				if((DasPkt_encode(g_layout.pPkt, pBuf) == DAS_OKAY)&&
				   ((pHdr = DasBuf_data(pBuf, &uHdr)) != NULL))
					fwrite(pHdr, 1, uHdr, stdout);
				del_DasBuf(pBuf);
				if(g_layout.mode == LAYOUT_UNROLL)
					printf("(one packet per sample, %ld per record)\n", g_layout.nItems);
			}
			else{
				printf("(refused) %s\n", (g_sLastErr[0] != '\0') ? g_sLastErr : "see the log");
			}
			CDFcloseCDF(pFile->id);
		}
		free(pFile);
		return (nRet == DAS_OKAY) ? 0 : PERR;
	}

	/* Streaming.  The first readable file sets the layout; later files must
	   match its structure or are skipped with an error. */
	g_pIoOut = new_DasIO(PROG, new_DasXport_stdout(), NULL);
	g_pSd = new_DasStream();
	g_exType = DAS_EX_SERVER_ERR;

	cdf_file_t* pFirst = (cdf_file_t*)calloc(1, sizeof(cdf_file_t));
	cdf_file_t* pFile = (cdf_file_t*)calloc(1, sizeof(cdf_file_t));
	char sSigFirst[MAX_CDF_VARS * 64] = {'\0'};
	char sSig[MAX_CDF_VARS * 64] = {'\0'};
	long nPkts = 0;
	int nFiles = 0;
	char sPath[DURI_MAX_PATH];
	while(DasUriIter_next(&iter, sPath, sizeof(sPath)) != NULL){
		cdf_file_t* pCur = (nFiles == 0) ? pFirst : pFile;
		if(cdf_openAndClassify(pCur, sPath, &sel) != DAS_OKAY){
			daslog_error_v("Skipping %s", sPath);
			_flushLog();
			continue;
		}

		if(nFiles == 0){
			if(cdf_addGlobalProps(pFirst, (DasDesc*)g_pSd) != DAS_OKAY) goto STREAM_ERR;
			if(_planLayout(pFirst, &g_layout) != DAS_OKAY) goto STREAM_ERR;
			_addStreamHints(&g_layout, g_pSd);
			DAS_EXIT( DasStream_addDesc(g_pSd, (DasDesc*)g_layout.pPkt, 1) );
			DAS_EXIT( DasIO_writeStreamDesc(g_pIoOut, g_pSd) );
			DAS_EXIT( DasIO_writeDesc(g_pIoOut, (DasDesc*)g_layout.pPkt, 1) );
			cdf_structSig(pFirst, sSigFirst, sizeof(sSigFirst));
		}
		else{
			cdf_structSig(pCur, sSig, sizeof(sSig));
			if(strcmp(sSig, sSigFirst) != 0){
				daslog_error_v("%s does not have the same variables as %s, skipping it",
					sPath, pFirst->sPath);
				_flushLog();
				CDFcloseCDF(pCur->id);
				continue;
			}
		}

		if(_streamFile(pCur, &g_layout, g_pIoOut, &(opts.range), &nPkts) != DAS_OKAY) goto STREAM_ERR;

		CDFcloseCDF(pCur->id);
		pCur->id = NULL;
		++nFiles;
	}
	fini_DasUriIter(&iter);
	del_DasUriTplt(pTplt);

	if(nPkts == 0){
		if(!DasIO_headerSent(g_pIoOut))
			DAS_EXIT( DasIO_writeStreamDesc(g_pIoOut, g_pSd) );
		char sMsg[256] = {'\0'};
		char sBeg[64], sEnd[64];
		das_datum_toStr(&(opts.range.dmBeg), 3, sBeg, sizeof(sBeg));
		das_datum_toStr(&(opts.range.dmEnd), 3, sEnd, sizeof(sEnd));
		snprintf(sMsg, sizeof(sMsg) - 1, "No data in range %s to %s", sBeg, sEnd);
		DAS_EXIT( DasIO_exception_v(g_pIoOut, DAS_EX_NO_DATA, "%s", sMsg) );
	}

	_flushLog();
	DasIO_close(g_pIoOut);
	daslog_info_v("%ld packets sent from %d file(s)", nPkts, nFiles);
	free(g_layout.pOffSec);
	free(pFirst);
	free(pFile);
	return 0;

STREAM_ERR:
	_fatal();
	return PERR;
}
