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
 * 1. ONLY Stream data are sent to standard output, all general messages
 *    and errors *always* go to standard error.
 *
 * 2. Errors should also be sent as <exception> packets to the client so
 *    that they can be captured by client logging mechanisms.
 *
 * 3. Always return non-zero to the shell on an error.
 *
 * 4. Not having any data in a requested query range is *not* an error 
 *
 */

#define _POSIX_C_SOURCE 200112L

#include <das3/core.h>
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

/* TT2000 fill is one less than LLONG_MIN's magnitude allows in source, so
   it is spelled as bytes. */
#ifdef HOST_IS_LSB_FIRST
const ubyte g_tt2kfill[8] = {0,   0,0,0, 0,0,0,0x80};
#else
const ubyte g_tt2kfill[8] = {0x80,0,0,0, 0,0,0,   0};
#endif

/* ************************************************************************* */

void prnHelp()
{
	printf(
"SYNOPSIS\n"
"   " PROG " - Stream time series data from CDF files as a das3 stream\n"
"\n");

	printf(
"USAGE\n"
"   " PROG " [options] -r COORD,BEG,END PATTERN [VAR1[,VAR2 ...]]\n"
"   " PROG " [options] --das2 PATTERN BEGIN END [VAR1[,VAR2 ...]]\n"
"\n");

	printf(
"DESCRIPTION\n"
"   " PROG " is a das reader.  It reads one or more CDF files carrying\n"
"   ISTP style metadata and writes a das3 stream to standard output.  It is\n"
"   the origin point for a full resolution stream; reducers such as\n"
"   das3_csv, das2_bin_avgsec and das2_psd take it from there.  All log\n"
"   messages go to standard error, errors are also sent to the client as\n"
"   <exception> packets, and an empty query range is not an error.\n"
"\n"
"   Data are selected by coordinate range.  A range names a coordinate, such\n"
"   as time, and gives its bounds.  The coordinate appears twice: as fields\n"
"   in the file PATTERN that pick which files to read, and as a variable\n"
"   inside each file that picks which records to send.  Only variables that\n"
"   depend on every ranged coordinate are streamed; everything else in the\n"
"   file (calibration tables, orbit constants, and the like) is ignored.  To\n"
"   see what a file holds, and what would be skipped, use --no-op.\n"
"\n"
"   Time is the coordinate with a built-in definition.  Inside an ISTP file\n"
"   the time variable is found without help: it is the record varying\n"
"   variable of type CDF_TIME_TT2000 or CDF_EPOCH that other variables name\n"
"   in their DEPEND_0 attribute.  Files that do not follow ISTP conventions\n"
"   can name it with --coord.  This version handles time only; see the\n"
"   LIMITATIONS section for the other coordinates.\n"
"\n"
"   The parameters are:\n"
"\n"
"   PATTERN\n"
"      Either the path to a single CDF file, or a pattern that relates time\n"
"      to file names.  A single file needs no range: it bounds itself, and\n"
"      every record of its time dependent variables is sent unless a range\n"
"      is given.  Each time field in a pattern is a token of the form $F,\n"
"      where F is one of:\n"
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
"      Only with --das2.  The time range to stream, as ISO-8601 strings, taken\n"
"      from the second and third non-option arguments.  This is the argument\n"
"      order that das2 server configurations (*.dsdf files) supply, so a\n"
"      reader line of '" PROG " --das2 PATTERN' works unchanged there.  For\n"
"      the meaning of the bounds see --range.\n"
"\n"
"   VAR\n"
"      The names of the CDF data variables to stream, as many as desired,\n"
"      separated by commas or spaces.\n"
"      Naming a variable also pulls in its support variables: the DEPEND_N\n"
"      coordinates, the LABL_PTR_N label sets, and any DELTA_PLUS_VAR or\n"
"      DELTA_MINUS_VAR uncertainties.  When no VAR is given the --def-vars\n"
"      list is used.  When that is empty too, every variable with\n"
"      VAR_TYPE=data that depends on the ranged coordinates is streamed.\n"
"\n");

	printf(
"   Structure recovery\n"
"\n"
"   ISTP metadata describe a CDF variable one array index at a time, while\n"
"   das3 describes data in terms of coordinates and vector components.  The\n"
"   following rules recover the das3 structure.  Each rule is applied where\n"
"   its trigger is present and logged when it has to guess.\n"
"\n"
"      * The number of DEPEND_N attributes on a data variable is the rank of\n"
"        the output dataset, and DEPEND_N names the coordinate for index N.\n"
"\n"
"      * Data variables that share a DEPEND_0 share an output dataset.  A\n"
"        file with several time bases yields several datasets.\n"
"\n"
"      * An array index that has a LABL_PTR_N but no DEPEND_N is not a\n"
"        coordinate index, it is a set of components.  The variable is\n"
"        output as a das3 composite (a vector, a complex number, or a plain\n"
"        labeled bundle) and the dataset rank is reduced by one.\n"
"\n"
"      * A component set whose variable carries a frame attribute (see the\n"
"        property map below) is a geometric vector in that frame.  The\n"
"        coordinate system is read from the component labels, so 'B_x B_y\n"
"        B_z' is Cartesian and 'r_GEO theta_GEO phi_GEO' is spherical.\n"
"\n"
"      * A component set of two labeled 'real' and 'imaginary' (or\n"
"        'magnitude' and 'phase') is a complex number.\n"
"\n"
"      * A support variable with an OFFSET_OF attribute is the offset half\n"
"        of a reference + offset time coordinate, the usual layout for\n"
"        waveform data.  Evenly spaced offsets are sent as a sequence rather\n"
"        than as a table.\n"
"\n"
"   Metadata\n"
"\n"
"   CDF global attributes become <stream> properties and CDF variable\n"
"   attributes become properties of the <coord> or <data> element that holds\n"
"   the variable.  Attribute names are kept as-is except for the ISTP names\n"
"   that have a das3 equivalent, which are converted as follows.\n"
"\n"
"      CATDESC                -> summary\n"
"      FIELDNAM               -> title\n"
"      LABLAXIS               -> label\n"
"      VAR_NOTES              -> notes\n"
"      FILLVAL                -> (array fill value)\n"
"      FORMAT                 -> format\n"
"      COORDINATE_SYSTEM      -> frame\n"
"      SCALEMIN,SCALEMAX      -> scaleMin,scaleMax\n"
"      SCALETYP               -> scaleType\n"
"      VALIDMIN,VALIDMAX      -> validMin,validMax\n"
"      LIMITS_NOMINAL_MIN,MAX -> nominalMin,nominalMax\n"
"      LIMITS_WARN_MIN,MAX    -> warnMin,warnMax\n"
"      TEXT                   -> summary (global)\n"
"      TITLE                  -> title (global)\n"
"\n"
"   This table is the inverse of the one das3_cdf applies when writing CDFs.\n"
"   Missions that keep the same information under other attribute names can\n"
"   add to the table with --prop-map.  Note that some das3 names are\n"
"   structural rather than descriptive: 'frame' becomes part of the vector\n"
"   definition, 'label' supplies component labels, and the fill value is\n"
"   attached to the data array.\n"
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
"   -r RANGE,--range=COORD[,BEG,END]\n"
"                 Select data by coordinate range.  COORD is a coordinate\n"
"                 name, BEG and END are its bounds in the coordinate's\n"
"                 units, and END is exclusive.  Time bounds are ISO-8601\n"
"                 strings whose trailing fields may be omitted when zero, so\n"
"                 2025-08-04 is midnight on that day.  Records are compared\n"
"                 against the range by their coordinate variable, so a file\n"
"                 that overlaps the range yields only the records within it.\n"
"                 May be repeated, once per coordinate; a record must fall\n"
"                 in every range given.  COORD alone, with no bounds, selects\n"
"                 on that coordinate but takes the bounds from the file; it\n"
"                 is only legal when PATTERN has no fields for it.  A lone\n"
"                 file with no --range at all is read as --range=time.\n"
"\n"
"   --das2        Take BEGIN and END for time from the second and third\n"
"                 non-option arguments instead of from --range.  See BEGIN\n"
"                 END above.\n"
"\n"
"   -c MAP,--coord=COORD:VAR[,COORD:VAR ...]\n"
"                 Name the variable inside each file that carries a ranged\n"
"                 coordinate, overriding the built-in ISTP detection.  Needed\n"
"                 for files with no DEPEND_0 attributes, for example\n"
"                 -c time:Timestamp.  A component of a composite variable is\n"
"                 named as VAR.N with N counting from zero.\n"
"\n"
"   -d VARS,--def-vars=VAR1[,VAR2 ...]\n"
"                 The data variables to stream when none are named on the\n"
"                 command line.  Server configurations use this to pick a\n"
"                 default product while leaving the client free to ask for\n"
"                 others.\n"
"\n"
"   -n,--no-op    Do not write a stream.  Instead list the files that match\n"
"                 PATTERN for the given ranges, then for the first file list the\n"
"                 variables that would be streamed, their support variables,\n"
"                 and the variables that would be ignored and why.  Requested\n"
"                 VARs are checked against the file.\n"
"\n"
"   -p MAP,--prop-map=CDF_ATTR:DAS_PROP[,CDF_ATTR:DAS_PROP ...]\n"
"                 Extend the metadata table above.  Each entry names a CDF\n"
"                 attribute and the das3 property it should become.  This is\n"
"                 how mission conventions are supported without patching\n"
"                 " PROG ".  For example, a mission may keep the das3 <ops>\n"
"                 attribute 'frame' in COORD_FRAME, so its files are read\n"
"                 with -p COORD_FRAME:frame.  This is the mirror of the same\n"
"                 option in das3_cdf.\n"
"\n");

	printf(
"EXAMPLES\n"
"   1. Stream one hour of TRACERS 2 search coil waveforms in spacecraft\n"
"      coordinates:\n"
"\n"
"      " PROG " -p COORD_FRAME:frame -r time,2025-08-04T23:00,2025-08-05 \\\n"
"         '/data/ts2/$Y/$m/$d/ts2_l2_msc_bac_$Y$m$d_v$v.cdf' ts2_l2_bac_tscs\n"
"\n"
"   2. See what a single file holds without streaming anything:\n"
"\n"
"      " PROG " -n ts2_l2_msc_bac_20250804_v1.3.1.cdf\n"
"\n"
"   3. Convert a day of data to delimited text:\n"
"\n"
"      " PROG " -r time,2026-06-03,2026-06-04 \\\n"
"         '/data/ts2/$Y/$m/$d/ts2_l2_mag_bdc-16sps_$Y$m$d_v$v.cdf' \\\n"
"         | das3_csv > mag_20260603.csv\n"
"\n");

	printf(
"LIMITATIONS\n"
"   * Time is the only coordinate this version can range on.  Variables\n"
"     that do not depend on it are not served; read the CDF directly for\n"
"     calibration tables and other constants.  Position, orbit number and\n"
"     spacecraft clock coordinates are planned, see the source.\n"
"   * CDF_EPOCH16 time variables are not supported and are skipped.\n"
"   * File name patterns keyed on orbit number or spacecraft clock are not\n"
"     yet supported.\n"
"   * das2 stream output is not available; pipe through a das3 to das2\n"
"     converter if a das2 client must be fed.\n"
"   * There is no per-variable override yet.  A map file for renaming,\n"
"     dropping and forcing the kind of a variable, and a hand-authored\n"
"     dataset header used as a template, are planned.\n"
"\n");

	printf(
"MAINTAINER\n"
"   chris-piker@uiowa.edu\n"
"\n");

	printf(
"SEE ALSO\n"
"   * das3_cdf, das3_csv, das3_spice\n"
"   * ISTP CDF guidelines: https://spdf.gsfc.nasa.gov/istp_guide/istp_guide.html\n"
"\n");
}

/* ************************************************************************* */
/* Program options */

#define MAX_DATA_VARS 32
#define MAX_RANGES     8

typedef struct program_options {
	char aLevel[32];
	char aDefVars[1024];   /* --def-vars, comma separated */
	char aPropMap[512];    /* --prop-map, "FROM:TO,FROM:TO" */
	char aCoordMap[512];   /* --coord, "COORD:VAR,COORD:VAR" */
	bool bNoOp;
	bool bDas2Args;        /* --das2: BEGIN END are positional */
	const char* sPattern;  /* file name or URI pattern, points into argv */
	das_range   aRanges[MAX_RANGES];
	int         nRanges;
	const char* asVars[MAX_DATA_VARS];  /* point into argv */
	int         nVars;
} popts_t;

/* Argument errors happen before the stream is open, so the das3 exception
   is spelled out by hand. */
static int _argError(const char* sMsg)
{
	const char* sHdr = "<stream version=\"3.0\" type=\"das-basic-stream\" />\n";
	char sPkt[512] = {'\0'};

	fprintf(stderr, "CRITICAL: %s\n", sMsg);

	printf("|Sx||%zu|%s", strlen(sHdr), sHdr);
	snprintf(sPkt, sizeof(sPkt) - 1,
		"<exception type=\"QueryError\">\n%s\n</exception>\n", sMsg
	);
	printf("|Ex||%zu|%s", strlen(sPkt), sPkt);
	return PERR;
}

/* One --range value, "COORD,BEG,END".  Time bounds parse as UTC; anything
   else parses as a datum in whatever form das_datum_fromStr accepts. */
static int _parseRange(popts_t* pOpts, const char* sArg)
{
	char sMsg[256] = {'\0'};
	char sBuf[256] = {'\0'};
	strncpy(sBuf, sArg, sizeof(sBuf) - 1);

	char* sCoord = sBuf;
	char* sBeg = strchr(sCoord, ',');
	char* sEnd = (sBeg != NULL) ? strchr(sBeg + 1, ',') : NULL;
	if((sCoord[0] == '\0')||((sBeg != NULL)&&(sEnd == NULL))){
		snprintf(sMsg, sizeof(sMsg) - 1,
			"Expected COORD or COORD,BEG,END for --range, got '%s'", sArg
		);
		return _argError(sMsg);
	}
	if(sBeg != NULL){ *sBeg = '\0'; ++sBeg; }
	if(sEnd != NULL){ *sEnd = '\0'; ++sEnd; }

	if(pOpts->nRanges >= MAX_RANGES){
		snprintf(sMsg, sizeof(sMsg) - 1, "More than %d ranges given", MAX_RANGES);
		return _argError(sMsg);
	}
	das_range* pRng = pOpts->aRanges + pOpts->nRanges;

	if(sBeg == NULL){
		/* Bounds come from the file: an unbounded range names its coordinate
		   and nothing else.  vtUnknown datums are the "no bound" mark. */
		memset(pRng, 0, sizeof(das_range));
		strncpy(pRng->sCoord, sCoord, sizeof(pRng->sCoord) - 1);
	}
	else if(strcmp(sCoord, "time") == 0){
		if(das_range_fromUtc(pRng, sBeg, sEnd) != DAS_OKAY){
			snprintf(sMsg, sizeof(sMsg) - 1,
				"Could not parse the time range %s to %s", sBeg, sEnd
			);
			return _argError(sMsg);
		}
	}
	else{
		das_datum dmBeg, dmEnd;
		if((!das_datum_fromStr(&dmBeg, sBeg))||(!das_datum_fromStr(&dmEnd, sEnd))||
		   (das_range_fromDatum(pRng, sCoord, &dmBeg, &dmEnd) != DAS_OKAY)
		){
			snprintf(sMsg, sizeof(sMsg) - 1,
				"Could not parse the %s range %s to %s", sCoord, sBeg, sEnd
			);
			return _argError(sMsg);
		}
	}
	++(pOpts->nRanges);
	return DAS_OKAY;
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
	das_return_on_error();

	char sMsg[256] = {'\0'};
	char sRange[256] = {'\0'};
	char* sBeg = NULL;   /* --das2 positionals */
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
			if(strcmp(argv[i], "--das2") == 0){
				pOpts->bDas2Args = true;
				continue;
			}
			sRange[0] = '\0';
			if(dascmd_getArgVal(sRange, sizeof(sRange), argv, argc, &i, "-r", "--range=")){
				if(_parseRange(pOpts, sRange) != DAS_OKAY)
					return PERR;
				continue;
			}
			if(dascmd_getArgVal(
				pOpts->aCoordMap, DAS_FIELD_SZ(popts_t, aCoordMap), argv, argc, &i, "-c", "--coord="
			))
				continue;
			if(dascmd_getArgVal(
				pOpts->aLevel, DAS_FIELD_SZ(popts_t, aLevel), argv, argc, &i, "-l", "--log="
			))
				continue;
			if(dascmd_getArgVal(
				pOpts->aDefVars, DAS_FIELD_SZ(popts_t, aDefVars), argv, argc, &i, "-d", "--def-vars="
			))
				continue;
			if(dascmd_getArgVal(
				pOpts->aPropMap, DAS_FIELD_SZ(popts_t, aPropMap), argv, argc, &i, "-p", "--prop-map="
			))
				continue;
			snprintf(sMsg, sizeof(sMsg) - 1, "Unknown command line argument %s", argv[i]);
			return _argError(sMsg);
		}

		/* Positionals: PATTERN, then BEGIN END under --das2, then VARs.  The flag
		   may follow the positionals on the line, so BEGIN END are set aside
		   and only claimed after the whole line is read. */
		if(nPos == 0)
			pOpts->sPattern = argv[i];
		else if(nPos == 1)
			sBeg = argv[i];
		else if(nPos == 2)
			sEnd = argv[i];
		else{
			if(_addVars(pOpts, argv[i]) != DAS_OKAY)
				return PERR;
		}
		++nPos;
	}

	if(nPos < 1)
		return _argError("Missing arguments, expected at least PATTERN, use -h for help");

	if(pOpts->bDas2Args){
		if(nPos < 3)
			return _argError("With --das2, expected PATTERN BEGIN END, use -h for help");
		snprintf(sRange, sizeof(sRange) - 1, "time,%s,%s", sBeg, sEnd);
		if(_parseRange(pOpts, sRange) != DAS_OKAY)
			return PERR;
	}
	else{
		/* Without --das2 the 2nd and 3rd positionals were variables all along,
		   and they came before the ones already added */
		int nLater = pOpts->nVars;
		const char* asLater[MAX_DATA_VARS];
		memcpy(asLater, pOpts->asVars, nLater * sizeof(const char*));
		pOpts->nVars = 0;
		if((sBeg != NULL)&&(_addVars(pOpts, sBeg) != DAS_OKAY)) return PERR;
		if((sEnd != NULL)&&(_addVars(pOpts, sEnd) != DAS_OKAY)) return PERR;
		for(int j = 0; j < nLater; ++j){
			if(pOpts->nVars >= MAX_DATA_VARS)
				return _argError("Too many variables requested");
			pOpts->asVars[pOpts->nVars] = asLater[j];
			++(pOpts->nVars);
		}
	}

	/* A lone file bounds itself; a pattern has fields to fill in */
	bool bHasFields = (strchr(pOpts->sPattern, '$') != NULL);
	if(pOpts->nRanges == 0){
		if(bHasFields)
			return _argError("No coordinate range given for the pattern, use --range or --das2, see -h");
		strncpy(pOpts->aRanges[0].sCoord, "time", sizeof(pOpts->aRanges[0].sCoord) - 1);
		pOpts->nRanges = 1;
	}
	for(int j = 0; j < pOpts->nRanges; ++j){
		if((pOpts->aRanges[j].dBeg.vt == vtUnknown) && bHasFields){
			snprintf(sMsg, sizeof(sMsg) - 1,
				"--range=%s has no bounds, but PATTERN has fields to fill in",
				pOpts->aRanges[j].sCoord
			);
			return _argError(sMsg);
		}
	}

	return DAS_OKAY;
}

/* ************************************************************************* */
/* Log handler: everything to stderr, errors also to the client, criticals
   flush the stream and exit.  Set by main before the handler can fire. */

static DasIO* g_pIoOut = NULL;
static DasStream* g_pSd = NULL;
static das_except_t g_exType = DAS_EX_QUERY_ERR;

void logHandler(int nLevel, const char* sMsg, bool bPrnTime)
{
	if(nLevel < daslog_level())
		return;

	fprintf(stderr, "%s: %s\n", daslog_levelstr(nLevel), sMsg);

	if((nLevel < DASLOG_ERROR)||(g_pIoOut == NULL))
		return;

	if(!(g_pIoOut->bSentHeader))
		DasIO_writeStreamDesc(g_pIoOut, g_pSd);

	OobExcept except;
	OobExcept_set(&except, g_exType, sMsg);
	if( DasIO_writeException(g_pIoOut, &except) != DAS_OKAY)
		nLevel = DASLOG_CRIT;

	if(nLevel >= DASLOG_CRIT){
		DasIO_close(g_pIoOut);
		del_DasIO(g_pIoOut);
		exit(PERR);
	}
}

/* Report das_error() results through the log so the client sees them.
   Requires a local nDasStatus. */
void _bounce_to_log(){
	das_error_msg* pErr = das_get_error();
	daslog_critical_v(
		"%s (reported from %s:%d, %s)", pErr->message, pErr->sFile,
		pErr->nLine, pErr->sFunc
	);
}

#define DAS_EXIT( SOME_DAS_FUNC ) \
	if( (nDasStatus = (SOME_DAS_FUNC )) != DAS_OKAY) _bounce_to_log();

/* ************************************************************************* */
/* CDF status handling, requires a local nCdfStatus */

bool _cdfOkayish(CDFstatus iStatus){
	char sMsg[CDF_ERRTEXT_LEN+1];

	if(iStatus == CDF_OK)
		return true;

	CDFgetStatusText(iStatus, sMsg);

	if(iStatus < CDF_WARN){
		daslog_error_v("from cdflib, %s", sMsg);
		return false;
	}

	if(iStatus < CDF_OK)
		daslog_warn_v("from cdflib, %s", sMsg);
	else if(iStatus > CDF_OK)
		daslog_info_v("from cdflib, %s", sMsg);

	return true;
}

#define CDF_MAD( SOME_CDF_FUNC ) ( ((nCdfStatus = (SOME_CDF_FUNC) ) != CDF_OK) && (!_cdfOkayish(nCdfStatus)) )

/* ============================================================================
 * GENERALIZED COORDINATES  (design record, time is the only one built)
 *
 * A query is a set of coordinate ranges.  Each ranged coordinate appears in
 * two places: as fields of the file PATTERN, which pick the files, and as a
 * variable inside each file, which picks the records.  The URI layer
 * (das3/uri.h) is already coordinate-agnostic: das_range names its
 * coordinate, init_DasUriIter takes an array of them, and DasUriSegDef lets
 * a coordinate register its own path fields.  The gaps are in this file and
 * on the command line.
 *
 * The selection rule, general form: a data variable streams when every
 * ranged coordinate maps onto one of its DEPEND_N, or onto a component of
 * one.  Time is the coordinate with a built-in guess (the TT2000 or EPOCH
 * typed record varying variable named by DEPEND_0 attributes); --coord is
 * the explicit spelling of the same mapping and the only way in for files
 * without DEPEND attributes.
 *
 * Gaps, in order of how much they change the design:
 *
 * 1. Grid versus trajectory.  A position coordinate arrives in two shapes:
 *    gridded (latitude is DEPEND_0, longitude DEPEND_1, rank 2) or along a
 *    track (one record index, position a 2-component composite on it, or two
 *    scalars off the same index).  --coord must be able to name a component,
 *    VAR.N, because "iau_lat" alone cannot mean both a whole variable and a
 *    component of iau_pos.
 *
 * 2. Filtering becomes slicing.  Time filtering keeps or drops whole records
 *    and preserves the dataset shape.  Filtering a grid on longitude subsets
 *    index 1, which changes the shape and the packet item counts.
 *    DasVar_subset does the operation; it has to run before the dataset
 *    header is written, and the record loop needs a second strategy keyed on
 *    the index the coordinate lives on.
 *
 * 3. Cyclic coordinates.  Longitude wraps: 350 to 10 is twenty degrees
 *    across the meridian.  Neither das_range nor the record test knows a
 *    coordinate is cyclic, or its period.  That belongs in the coordinate
 *    definition next to the units, not on the command line.
 *
 * 4. Coordinate definitions.  das_time_uridef is compiled in.  Any other
 *    coordinate needs field widths, sign handling in file names, units, and
 *    the cyclic flag.  A small compiled-in registry is the honest first
 *    version; mission-specific token layouts stay a --prop-map style problem.
 *
 * 5. Path-only coordinates.  Orbit number, perijove, and clock partition are
 *    common file name tokens with no variable inside the file.  Under the
 *    selection rule nothing would stream.  A path-only coordinate should
 *    become a degenerate coordinate on the dataset (index "-" throughout)
 *    whose value is read from the file name; that is also how a client
 *    learns which orbit a packet came from, and a cheap cross-check that a
 *    file's Epoch agrees with its name.
 *
 * 6. Order.  Time can assume files arrive in order and records increase, and
 *    das2 clients rely on the monotonic property.  Position has no natural
 *    order and readdir order is arbitrary.  Do not claim monotonic unless the
 *    ranged coordinate is time.
 *
 * 7. What the client sees.  A 2-component position coordinate is one
 *    composite on two plot axes (axis="y;x", as ex22's space coordinate),
 *    not a vector on one axis.
 *
 * 8. Multi-field non-time coordinates in the URI layer.  A spacecraft clock
 *    partition:mod64k:mod60 is a mixed-radix tuple; a directory level with
 *    only the leading fields known covers a half-open interval of tuples,
 *    exactly as a year directory covers a year.  uri.c tests that interval
 *    for time (das3/uri.c, _in_ranges) but das_range can only hold a
 *    scalar datum, so a clock range must still be given field by field,
 *    which cannot express one ordered tuple.  The missing piece is a
 *    composite datum in das_range whose form knows the field order and the
 *    roll-over, and a form vtable that implements the comparison operators
 *    (order is already a form concern, see form_vector's sysorder; the
 *    "<" "=" ">" binops are what is unbuilt).  Time stays das_time.
 *
 * Unchanged by any of this: dataset grouping by shared DEPEND chain,
 * composite recovery from LABL_PTR, the property map, and the file iterator
 * for time.
 * ============================================================================
 */

/* ============================================================================
 * METADATA INVERSION HEURISTICS  (catalog)
 *
 * das3_cdf flattens das3's index-decoupled model into ISTP's one-physdim-per-
 * index.  We invert that.  ISTP is lossy and real producers are sloppy, so this
 * is a set of named heuristics, each triggered by specific CDF metadata.  This
 * tool is deliberately NOT fail-loud: it logs every guess it makes and proceeds.
 * "Does something, maybe not exactly right" is the hook; a reader that demands a
 * config doc up front gets dropped.  Overrides correct the guesses when
 * needed: --prop-map renames attributes for every variable; a per-variable
 * map file (rename, drop, force a kind, place a coordinate) and a
 * hand-authored dataset header used as a template, where the CDF supplies
 * only the values, are planned for the cases the heuristics cannot reach.
 *
 * Heuristics are layered as PROFILES so non-ISTP producers (ESA/SWARM) can add
 * a top layer without disturbing the core.
 *
 * ISTP-core (spec-backed, any conformant CDF):
 *
 *   depend-rank          External dataset rank = count of DEPEND_N (ISTP: the
 *                        DEPEND count must match variable dimensionality).
 *                        DEPEND_k names the coord for index k.  DEPEND_0 is
 *                        usually time but NOT always -- classify each target by
 *                        Units_haveCalRep(), never assume index 0 is time.
 *   cadence-split        >1 record-varying epoch var, each DEPEND_0 of a
 *                        disjoint set -> one dataset per DEPEND_0.  >1 dataset
 *                        per stream is expected.
 *   display-type-kind    DISPLAY_TYPE (time_series/spectrogram/stack_plot/image/
 *                        no_plot) hints the kind + what LABL_PTR means.  A HINT
 *                        only: undisciplined producers lie, so structure wins
 *                        on conflict.
 *   labelptr-internal    LABL_PTR_N with NO DEPEND_N on that axis -> the axis is
 *                        an internal component index (vector/bundle), rank-
 *                        reducing.  LABL_PTR *with* a DEPEND = channel labels on
 *                        a coordinate, nothing structural.  (Autoplot-proven.)
 *   delta-var-uncertainty DELTA_PLUS_VAR/DELTA_MINUS_VAR -> das2C min/max roles.
 *   ignore-and-metadata  VAR_TYPE: ignore_data -> skip; metadata -> labels.
 *                        Do NOT use VAR_TYPE to split coord vs data (it is loose
 *                        in real L2); group by DEPEND_0 instead.
 *
 * TRACERS layer (mission conventions -- COORD_FRAME is NOT standard ISTP):
 *
 *   vector-frame         internal axis + COORD_FRAME -> geometric vector in that
 *                        frame.  No frame -> anonymous labeled bundle.  RANK
 *                        never depends on this layer, only the frame dressing.
 *   symbol-system        component-label glyphs -> vector system by matching the
 *                        ORDERED symbol tuple (cart x,y,z / cyl rho,phi,z / sph
 *                        r,theta,phi / centric r,phi,theta / detic,graphic
 *                        phi,theta,a).  No match -> cartesian.  detic/graphic
 *                        degenerate -> warn + override with ,SYSTEM.
 *   dictkey-physdim      DICT_KEY "class>name" -> physDim class + name.
 *
 * repair layer (malformed CDFs -- do something useful anyway):
 *
 *   shape-match-repair   A DEPEND_N/LABL_PTR_N whose number does not fit the
 *                        array -> assign the referenced var to the axis whose
 *                        LENGTH matches.  (How every MATLAB mag reader loads
 *                        data.)  EFI: DEPEND_0=Frequency(257)->the 257 axis;
 *                        LABL_PTR_2(len 2)->the 2 axis.
 *   complex-pair         length-2 internal axis labeled real/imaginary (or
 *                        magnitude/phase) -> intern="2" under <ops
 *                        kind="complex">, system rectangular or polar.  No
 *                        vtComplex is involved or wanted: the pair is two
 *                        ordinary cells and the <ops> element is what makes
 *                        them one value.  See das3/form_cplx.h.
 * ============================================================================
 */


/* ************************************************************************* */
/* It all starts here baby! */

int main(int argc, char** argv)
{
	popts_t opts;
	if(parseArgs(argc, argv, &opts) != DAS_OKAY)
		return PERR;

	das_init(argv[0], DASERR_DIS_RET, 0, daslog_strlevel(opts.aLevel), logHandler);

	DasErrCode nDasStatus = DAS_OKAY;   /* for DAS_EXIT */

	DasUriTplt* pTplt = new_DasUriTplt();
	DasUriTplt_register(pTplt, das_time_uridef());
	DAS_EXIT( DasUriTplt_pattern(pTplt, opts.sPattern) );

	DasUriIter iter;
	DAS_EXIT( init_DasUriIter(&iter, pTplt, opts.nRanges, opts.aRanges) );

	if(opts.bNoOp){
		char sBeg[64] = {'\0'};
		char sEnd[64] = {'\0'};
		printf("Pattern: %s\n", opts.sPattern);
		for(int i = 0; i < opts.nRanges; ++i){
			if(opts.aRanges[i].dBeg.vt == vtUnknown){
				printf("Range: %s, bounds from the file\n", opts.aRanges[i].sCoord);
				continue;
			}
			das_datum_toStr(&(opts.aRanges[i].dBeg), sBeg, sizeof(sBeg), 6);
			das_datum_toStr(&(opts.aRanges[i].dEnd), sEnd, sizeof(sEnd), 6);
			printf("Range: %s from %s to %s\n", opts.aRanges[i].sCoord, sBeg, sEnd);
		}
		if(opts.nVars > 0){
			printf("Requested:");
			for(int i = 0; i < opts.nVars; ++i) printf(" %s", opts.asVars[i]);
			printf("\n");
		}
		printf("Files:\n");
		const char* sFile = NULL;
		int nFiles = 0;
		while((sFile = DasUriIter_next(&iter)) != NULL){
			printf("   %s\n", sFile);
			++nFiles;
		}
		if(nFiles == 0)
			printf("   (none)\n");
		printf("Variables: (listing not implemented yet)\n");
		fini_DasUriIter(&iter);
		del_DasUriTplt(pTplt);
		return 0;
	}

	/* Streaming is not built.  Say so on stderr and through the stream,
	   then exit non-zero. */
	g_pIoOut = new_DasIO_cfile(PROG, stdout, "w3");
	g_pSd = new_DasStream();
	g_exType = DAS_EX_SERVER_ERR;
	daslog_critical("Streaming has not been implemented yet");

	return PERR;   /* not reached, the log handler exits */
}
