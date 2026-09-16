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

/* Except for this message, which is mine, this file was written entirely by
   AI. In this case Claude Fable 5.1. Compared to the code in das3_cdf, which
   was initially written by me over three weeks of focused attention, this
   program was completed in 2 days of casual conversations, and the help text
   is more fluid to boot! We really are in a new world. The Enterprise computer
   is real now.
   --cwp 2026-09-15
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
#include <das3/form_vector.h>   /* core.h does not pull the form headers in */
#include <das3/form_geoloc.h>
#include <das3/form_point.h>
#include <das3/form_linear.h>
#include <das3/form_cplx.h>
#include <cdf.h>

#ifdef _WIN32
#define strcasecmp _stricmp
#define strncasecmp _strnicmp
#else
#include <strings.h>
#endif

#include <string.h>
#include <stdarg.h>
#include <inttypes.h>
#include <ctype.h>

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
"      separated by commas or spaces.  One component of a variable with a\n"
"      component axis is selected as VAR.COMPONENT, where COMPONENT is the\n"
"      label as written in the file (B_x), the bare direction symbol (x),\n"
"      or the component number counting from zero.  The result is still a\n"
"      composite carrying the frame, with one component present.\n"
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
"      VAR_TYPE=support_data  -> varType=support (data elements only)\n"
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

	if(nLevel < DASLOG_ERROR)
		return;

	if(g_pIoOut != NULL){
		if(!(g_pIoOut->bSentHeader))
			DasIO_writeStreamDesc(g_pIoOut, g_pSd);

		OobExcept except;
		OobExcept_set(&except, g_exType, sMsg);
		if( DasIO_writeException(g_pIoOut, &except) != DAS_OKAY)
			nLevel = DASLOG_CRIT;
	}

	/* Critical items end the program, with or without a stream to flush */
	if(nLevel >= DASLOG_CRIT){
		if(g_pIoOut != NULL){
			DasIO_close(g_pIoOut);
			del_DasIO(g_pIoOut);
		}
		exit(PERR);
	}
}

/* Report das_error() results through the log so the client sees them.
   Requires a local nDasStatus. */
void _bounce_to_log(){
	das_error_msg* pErr = das_get_error();
	if((pErr == NULL)||(pErr->message == NULL)){
		daslog_critical("das library error, no message available");
		return;
	}
	daslog_critical_v(
		"%s (reported from %s:%d, %s)", pErr->message, pErr->sFile,
		pErr->nLine, pErr->sFunc
	);
	das_error_free(pErr);
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
/* CDF attribute access */

/* A variable attribute as a string.  Numeric attributes are formatted; a
   missing attribute leaves sBuf empty and returns false. */
static bool _varAttrStr(CDFid id, long iVar, const char* sAttr, char* sBuf, size_t uLen)
{
	sBuf[0] = '\0';
	long iAttr = CDFgetAttrNum(id, (char*)sAttr);
	if(iAttr < 0) return false;
	if(CDFconfirmzEntryExistence(id, iAttr, iVar) != CDF_OK) return false;

	long nType = 0, nElems = 0;
	if(CDFgetAttrzEntryDataType(id, iAttr, iVar, &nType) != CDF_OK) return false;
	if(CDFgetAttrzEntryNumElements(id, iAttr, iVar, &nElems) != CDF_OK) return false;

	if((nType == CDF_CHAR)||(nType == CDF_UCHAR)){
		char* sTmp = (char*)calloc(nElems + 1, 1);
		if(sTmp == NULL) return false;
		if(CDFgetAttrzEntry(id, iAttr, iVar, sTmp) != CDF_OK){ free(sTmp); return false; }
		strncpy(sBuf, sTmp, uLen - 1);
		free(sTmp);
		return true;
	}

	/* Numeric: print the first element */
	ubyte aVal[64] = {0};
	if(nElems * 8 > (long)sizeof(aVal)) return false;
	if(CDFgetAttrzEntry(id, iAttr, iVar, aVal) != CDF_OK) return false;
	switch(nType){
	case CDF_INT1:  snprintf(sBuf, uLen - 1, "%d",  *((int8_t*)aVal));   break;
	case CDF_UINT1: snprintf(sBuf, uLen - 1, "%u",  *((uint8_t*)aVal));  break;
	case CDF_INT2:  snprintf(sBuf, uLen - 1, "%d",  *((int16_t*)aVal));  break;
	case CDF_UINT2: snprintf(sBuf, uLen - 1, "%u",  *((uint16_t*)aVal)); break;
	case CDF_INT4:  snprintf(sBuf, uLen - 1, "%d",  *((int32_t*)aVal));  break;
	case CDF_UINT4: snprintf(sBuf, uLen - 1, "%u",  *((uint32_t*)aVal)); break;
	case CDF_INT8:
	case CDF_TIME_TT2000:
		snprintf(sBuf, uLen - 1, "%" PRId64, *((int64_t*)aVal)); break;
	case CDF_REAL4:
	case CDF_FLOAT:  snprintf(sBuf, uLen - 1, "%g", *((float*)aVal));  break;
	case CDF_REAL8:
	case CDF_DOUBLE:
	case CDF_EPOCH:  snprintf(sBuf, uLen - 1, "%g", *((double*)aVal)); break;
	default: return false;
	}
	return true;
}

/* Names of the variable scoped attributes present on one variable, for the
   advice printer.  Returns the count written. */
static int _varAttrNames(CDFid id, long iVar, char* sBuf, size_t uLen)
{
	long nAttrs = 0;
	if(CDFgetNumAttributes(id, &nAttrs) != CDF_OK) return 0;
	sBuf[0] = '\0';
	int nFound = 0;
	char sName[CDF_ATTR_NAME_LEN256 + 1];
	for(long i = 0; i < nAttrs; ++i){
		long nScope = 0;
		if(CDFgetAttrScope(id, i, &nScope) != CDF_OK) continue;
		if(nScope != VARIABLE_SCOPE) continue;
		if(CDFconfirmzEntryExistence(id, i, iVar) != CDF_OK) continue;
		if(CDFgetAttrName(id, i, sName) != CDF_OK) continue;
		size_t uHave = strlen(sBuf);
		if(uHave + strlen(sName) + 2 >= uLen) break;
		if(nFound > 0) strcat(sBuf, " ");
		strcat(sBuf, sName);
		++nFound;
	}
	return nFound;
}

/* ************************************************************************* */
/* The property map: CDF attribute name to das3 key, "FROM:TO,FROM:TO".  The
   built-in table is the inverse of das3_cdf's; --prop-map entries are
   consulted first. */

typedef struct prop_map_ent { char sFrom[64]; char sTo[64]; } prop_map_t;

#define MAX_PROP_MAP 32
static prop_map_t g_aPropMap[MAX_PROP_MAP];
static int g_nPropMap = 0;

static const prop_map_t g_aBuiltinMap[] = {
	{"CATDESC",            "summary"},
	{"FIELDNAM",           "title"},
	{"LABLAXIS",           "label"},
	{"VAR_NOTES",          "notes"},
	{"FILLVAL",            "fill"},
	{"FORMAT",             "format"},
	{"COORDINATE_SYSTEM",  "frame"},
	{"SCALEMIN",           "scaleMin"},
	{"SCALEMAX",           "scaleMax"},
	{"SCALETYP",           "scaleType"},
	{"VALIDMIN",           "validMin"},
	{"VALIDMAX",           "validMax"},
	{"LIMITS_NOMINAL_MIN", "nominalMin"},
	{"LIMITS_NOMINAL_MAX", "nominalMax"},
	{"LIMITS_WARN_MIN",    "warnMin"},
	{"LIMITS_WARN_MAX",    "warnMax"},
	{"", ""}
};

static int _parsePropMap(const char* sMap)
{
	g_nPropMap = 0;
	if((sMap == NULL)||(sMap[0] == '\0')) return DAS_OKAY;

	char sBuf[512];
	strncpy(sBuf, sMap, sizeof(sBuf) - 1);
	sBuf[sizeof(sBuf) - 1] = '\0';

	char* sTok = sBuf;
	while((sTok != NULL)&&(*sTok != '\0')){
		char* sNext = strchr(sTok, ',');
		if(sNext != NULL){ *sNext = '\0'; ++sNext; }
		char* sSep = strchr(sTok, ':');
		if((sSep == NULL)||(sSep == sTok)||(sSep[1] == '\0'))
			return das_error(PERR, "Expected CDF_ATTR:DAS_PROP in --prop-map, got '%s'", sTok);
		if(g_nPropMap >= MAX_PROP_MAP)
			return das_error(PERR, "More than %d --prop-map entries", MAX_PROP_MAP);
		*sSep = '\0';
		strncpy(g_aPropMap[g_nPropMap].sFrom, sTok, 63);
		strncpy(g_aPropMap[g_nPropMap].sTo, sSep + 1, 63);
		++g_nPropMap;
		sTok = sNext;
	}
	return DAS_OKAY;
}

/* The value of the CDF attribute that maps to a das3 key, on one variable.
   The user's map wins over the built-in table.  False if no attribute
   maps there or none present. */
static bool _varKeyValue(
	CDFid id, long iVar, const char* sKey, char* sBuf, size_t uLen, char* sFromAttr
){
	for(int i = 0; i < g_nPropMap; ++i){
		if(strcmp(g_aPropMap[i].sTo, sKey) != 0) continue;
		if(_varAttrStr(id, iVar, g_aPropMap[i].sFrom, sBuf, uLen)){
			if(sFromAttr) strcpy(sFromAttr, g_aPropMap[i].sFrom);
			return true;
		}
	}
	for(int i = 0; g_aBuiltinMap[i].sFrom[0] != '\0'; ++i){
		if(strcmp(g_aBuiltinMap[i].sTo, sKey) != 0) continue;
		if(_varAttrStr(id, iVar, g_aBuiltinMap[i].sFrom, sBuf, uLen)){
			if(sFromAttr) strcpy(sFromAttr, g_aBuiltinMap[i].sFrom);
			return true;
		}
	}
	return false;
}

/* ************************************************************************* */
/* File inventory: one record per zVariable, filled from the CDF and then
   annotated by the classifier.  Both the dry run listing and the stream
   builder work from this. */

#define CVAR_NAME_SZ (CDF_VAR_NAME_LEN256 + 1)
#define MAX_CDF_VARS 256
#define MAX_CDF_DS   32
#define MAX_COMPS    16      /* labels kept per component set */
#define ADVICE_SZ    4096

typedef enum var_role {
	ROLE_UNK = 0,
	ROLE_TIME,      /* a time base */
	ROLE_DATA,      /* streams as a <data> (or annotation <coord>) */
	ROLE_COORD,     /* a DEPEND_N table of a streamed variable */
	ROLE_OFFSET,    /* an OFFSET_OF table for a time base */
	ROLE_LABEL,     /* a LABL_PTR_N target */
	ROLE_DELTA,     /* a DELTA_PLUS/MINUS_VAR target */
	ROLE_IGNORE
} var_role_e;

typedef struct cdf_var {
	char sName[CVAR_NAME_SZ];
	long nVarNum;
	long nType;
	long nElems;                 /* chars per value for CDF_CHAR */
	long nDims;                  /* not counting the record dimension */
	long aDimSz[CDF_MAX_DIMS];
	long bRecVary;
	long nRecs;

	/* ISTP attributes the heuristics read, empty when absent */
	char sVarType[32];
	char asDepend[VARIDX_MAX][CVAR_NAME_SZ];    /* [0] = DEPEND_0 */
	char asLablPtr[VARIDX_MAX][CVAR_NAME_SZ];   /* [1] = LABL_PTR_1 */
	char sOffsetOf[CVAR_NAME_SZ];
	char sDeltaPlus[CVAR_NAME_SZ];
	char sDeltaMinus[CVAR_NAME_SZ];
	char sDictKey[128];
	char sUnits[64];
	char sFrame[64];
	char sFrameAttr[64];         /* which attribute supplied sFrame */

	/* classification */
	var_role_e role;
	char sWhy[160];              /* why ignored, or how used */
	bool bTimeDep;               /* DEPEND_0 is a time base */
	bool bSelected;              /* streams */
	int  iTime;                  /* index of the time base, -1 */
	int  iUsedBy;                /* support: first variable served, -1 */
	int  iDs;                    /* dataset index, -1 */

	/* structure of a selected variable */
	int  nExtRank;               /* record index + DEPEND_N dims */
	int  aIsInternal[CDF_MAX_DIMS];  /* per CDF dim: 1 = component axis */
	int  nComps;                 /* product of internal dims, 1 for a scalar */
	char sKind[32];              /* scalar, vector, complex, bundle, matrix */
	char sSystem[16];            /* cartesian, spherical, ... rectangular, polar */
	char sSyms[64];              /* "x,y,z" */
	char aLabels[MAX_COMPS][32]; /* component labels as read */
	int  nLabels;
	ubyte uSys;                  /* DAS_VSYS_* when sKind is vector */
	ubyte aDirs[MAX_COMPS];      /* canonical direction of each component */
	int  iCompDim;               /* CDF dim of the component axis, -1 if none */
	int  iCompSel;               /* selected component on it, -1 = all */
	char sCompSel[32];           /* how the user spelled it */
	bool bFolded;                /* joined a higher rank dataset, degenerate on its extra indices */
	bool bAnnot;                 /* folded as a coordinate annotation, not data */
} cdf_var_t;

/* One record varying array of a built dataset and the CDF variable that
   fills it, block by block */
typedef struct rec_reader {
	int     iVar;
	DasAry* pAry;
	long    nElemsPerRec;
	bool    bTime;               /* EPOCH values convert to TT2000 on the way in */
	long    nDims;               /* the hyperget window, fixed by the first file */
	long    aStart[CDF_MAX_DIMS];
	long    aCount[CDF_MAX_DIMS];
} rec_reader_t;

typedef struct cdf_ds {
	char sName[64];
	char sGroup[64];
	int  nRank;
	int  iTime;
	char sChain[VARIDX_MAX * CVAR_NAME_SZ];  /* DEPEND names joined, the key */
	int  aMembers[MAX_CDF_VARS];
	int  nMembers;

	/* the built dataset, filled by _buildDataset */
	DasDs* pDs;
	int    nPktId;
	rec_reader_t aRead[MAX_CDF_VARS];
	int    nRead;
} cdf_ds_t;

typedef struct cdf_file {
	CDFid id;
	char sPath[DURI_MAX_PATH];
	char sSource[128];           /* Logical_source, or a short TITLE, or "" */
	cdf_var_t aVars[MAX_CDF_VARS];
	int nVars;
	cdf_ds_t aDs[MAX_CDF_DS];
	int nDs;
	char sAdvice[ADVICE_SZ];
} cdf_file_t;

/* das identifiers: [A-Za-z0-9_], no leading digit, at most 63 characters.
   Anything else becomes an underscore. */
static void _dasId(char* sId)
{
	for(size_t u = 0; sId[u] != '\0'; ++u){
		char c = sId[u];
		bool bOk = ((c >= '0')&&(c <= '9'))||((c >= 'A')&&(c <= 'Z'))||((c >= 'a')&&(c <= 'z'))||(c == '_');
		if(!bOk) sId[u] = '_';
		if(u >= 63){ sId[u] = '\0'; break; }
	}
	if((sId[0] >= '0')&&(sId[0] <= '9')){
		memmove(sId + 1, sId, strlen(sId) + 1);
		sId[0] = '_';
		sId[63] = '\0';
	}
}

static int _varIndex(const cdf_file_t* pFile, const char* sName)
{
	if((sName == NULL)||(sName[0] == '\0')) return -1;
	for(int i = 0; i < pFile->nVars; ++i)
		if(strcmp(pFile->aVars[i].sName, sName) == 0) return i;
	return -1;
}

static void _advise(cdf_file_t* pFile, const char* sFmt, ...) _das_fmt_check(2, 3);

static void _advise(cdf_file_t* pFile, const char* sFmt, ...)
{
	char sMsg[1024];
	va_list ap;
	va_start(ap, sFmt);
	vsnprintf(sMsg, sizeof(sMsg), sFmt, ap);
	va_end(ap);

	/* word wrap to 80 columns, hanging indent under the bullet */
	size_t uHave = strlen(pFile->sAdvice);
	const char* sWord = sMsg;
	int nCol = 0;
	const char* sLead = "* ";
	while(*sWord != '\0'){
		while(*sWord == ' ') ++sWord;
		if(*sWord == '\0') break;
		const char* sEnd = sWord;
		while((*sEnd != '\0')&&(*sEnd != ' ')) ++sEnd;
		int nLen = (int)(sEnd - sWord);
		if(nCol == 0){
			if(uHave + 6 >= ADVICE_SZ) return;
			strcpy(pFile->sAdvice + uHave, sLead); uHave += 2; nCol = 2;
			sLead = "  ";
		}
		else if(nCol + 1 + nLen > 79){
			if(uHave + 7 >= ADVICE_SZ) return;
			pFile->sAdvice[uHave++] = '\n';
			strcpy(pFile->sAdvice + uHave, sLead); uHave += 2; nCol = 2;
		}
		else{
			pFile->sAdvice[uHave++] = ' '; ++nCol;
		}
		if(uHave + nLen + 2 >= ADVICE_SZ) return;
		memcpy(pFile->sAdvice + uHave, sWord, nLen);
		uHave += nLen; nCol += nLen;
		pFile->sAdvice[uHave] = '\0';
		sWord = sEnd;
	}
	pFile->sAdvice[uHave++] = '\n';
	pFile->sAdvice[uHave] = '\0';
}

static bool _isTimeType(long nType)
{
	return (nType == CDF_TIME_TT2000)||(nType == CDF_EPOCH);
}

static const char* _cdfTypeName(long nType)
{
	switch(nType){
	case CDF_INT1: return "int1";     case CDF_UINT1: return "uint1";
	case CDF_INT2: return "int2";     case CDF_UINT2: return "uint2";
	case CDF_INT4: return "int4";     case CDF_UINT4: return "uint4";
	case CDF_INT8: return "int8";
	case CDF_REAL4: case CDF_FLOAT:   return "float";
	case CDF_REAL8: case CDF_DOUBLE:  return "double";
	case CDF_CHAR:  case CDF_UCHAR:   return "char";
	case CDF_EPOCH:        return "EPOCH";
	case CDF_EPOCH16:      return "EPOCH16";
	case CDF_TIME_TT2000:  return "TT2000";
	default: return "?";
	}
}

/* A global attribute as a string, entry 0 */
static bool _globalAttrStr(CDFid id, const char* sAttr, char* sBuf, size_t uLen)
{
	sBuf[0] = '\0';
	long iAttr = CDFgetAttrNum(id, (char*)sAttr);
	if(iAttr < 0) return false;
	if(CDFconfirmgEntryExistence(id, iAttr, 0) != CDF_OK) return false;
	long nType = 0, nElems = 0;
	if(CDFgetAttrgEntryDataType(id, iAttr, 0, &nType) != CDF_OK) return false;
	if((nType != CDF_CHAR)&&(nType != CDF_UCHAR)) return false;
	if(CDFgetAttrgEntryNumElements(id, iAttr, 0, &nElems) != CDF_OK) return false;
	char* sTmp = (char*)calloc(nElems + 1, 1);
	if(sTmp == NULL) return false;
	if(CDFgetAttrgEntry(id, iAttr, 0, sTmp) != CDF_OK){ free(sTmp); return false; }
	strncpy(sBuf, sTmp, uLen - 1);
	free(sTmp);
	return true;
}

/* Read every zVariable's shape and the attributes the classifier needs */
static int _inventory(cdf_file_t* pFile)
{
	CDFstatus nCdfStatus = CDF_OK;
	long nVars = 0;

	/* A name for the file's contents, for datasets that carry no DICT_KEY.
	   Logical_source is the ISTP answer; a short TITLE is the L1 answer. */
	if(!_globalAttrStr(pFile->id, "Logical_source", pFile->sSource, sizeof(pFile->sSource))){
		char sTitle[128];
		if(_globalAttrStr(pFile->id, "TITLE", sTitle, sizeof(sTitle)) &&
		   (strlen(sTitle) <= 24) && (strchr(sTitle, ' ') == NULL))
			strncpy(pFile->sSource, sTitle, sizeof(pFile->sSource) - 1);
	}

	if(CDF_MAD( CDFgetNumzVars(pFile->id, &nVars) ))
		return PERR;
	if(nVars > MAX_CDF_VARS)
		return das_error(PERR, "%s has %ld variables, the limit is %d", pFile->sPath, nVars, MAX_CDF_VARS);

	char sAttr[32];
	for(long iVar = 0; iVar < nVars; ++iVar){
		cdf_var_t* pV = pFile->aVars + pFile->nVars;
		memset(pV, 0, sizeof(cdf_var_t));
		pV->nVarNum = iVar;
		pV->iTime = -1; pV->iUsedBy = -1; pV->iDs = -1;
		pV->iCompDim = -1; pV->iCompSel = -1;

		long aDimVary[CDF_MAX_DIMS];
		if(CDF_MAD( CDFinquirezVar(
			pFile->id, iVar, pV->sName, &(pV->nType), &(pV->nElems), &(pV->nDims),
			pV->aDimSz, &(pV->bRecVary), aDimVary
		)))
			return PERR;
		if(CDF_MAD( CDFgetzVarNumRecsWritten(pFile->id, iVar, &(pV->nRecs)) ))
			return PERR;

		_varAttrStr(pFile->id, iVar, "VAR_TYPE", pV->sVarType, sizeof(pV->sVarType));
		for(int i = 0; i < VARIDX_MAX; ++i){
			snprintf(sAttr, sizeof(sAttr), "DEPEND_%d", i);
			_varAttrStr(pFile->id, iVar, sAttr, pV->asDepend[i], CVAR_NAME_SZ);
			if(i > 0){
				snprintf(sAttr, sizeof(sAttr), "LABL_PTR_%d", i);
				_varAttrStr(pFile->id, iVar, sAttr, pV->asLablPtr[i], CVAR_NAME_SZ);
			}
		}
		_varAttrStr(pFile->id, iVar, "OFFSET_OF",       pV->sOffsetOf,   CVAR_NAME_SZ);
		_varAttrStr(pFile->id, iVar, "DELTA_PLUS_VAR",  pV->sDeltaPlus,  CVAR_NAME_SZ);
		_varAttrStr(pFile->id, iVar, "DELTA_MINUS_VAR", pV->sDeltaMinus, CVAR_NAME_SZ);
		_varAttrStr(pFile->id, iVar, "DICT_KEY",        pV->sDictKey,    sizeof(pV->sDictKey));
		_varAttrStr(pFile->id, iVar, "UNITS",           pV->sUnits,      sizeof(pV->sUnits));
		for(long j = (long)strlen(pV->sUnits) - 1; (j >= 0)&&(pV->sUnits[j] == ' '); --j) pV->sUnits[j] = '\0';
		_varKeyValue(pFile->id, iVar, "frame", pV->sFrame, sizeof(pV->sFrame), pV->sFrameAttr);

		++(pFile->nVars);
	}
	return DAS_OKAY;
}

/* ************************************************************************* */
/* Component labels and the system they spell */

/* Read an NRV CDF_CHAR label variable into pV->aLabels */
static int _readLabels(cdf_file_t* pFile, cdf_var_t* pV, int iLbl)
{
	cdf_var_t* pL = pFile->aVars + iLbl;
	if((pL->nType != CDF_CHAR)&&(pL->nType != CDF_UCHAR))
		return 0;
	long nLabels = (pL->nDims > 0) ? pL->aDimSz[0] : 1;
	for(int i = 1; i < pL->nDims; ++i) nLabels *= pL->aDimSz[i];
	if(nLabels > MAX_COMPS) nLabels = MAX_COMPS;

	char* pBuf = (char*)calloc(nLabels * pL->nElems + 1, 1);
	if(pBuf == NULL) return 0;
	if(CDFgetzVarRecordData(pFile->id, pL->nVarNum, 0, pBuf) != CDF_OK){
		free(pBuf);
		return 0;
	}
	pV->nLabels = 0;
	for(long i = 0; i < nLabels; ++i){
		char* sDest = pV->aLabels[pV->nLabels];
		long nCopy = (pL->nElems < 31) ? pL->nElems : 31;
		memcpy(sDest, pBuf + i * pL->nElems, nCopy);
		sDest[nCopy] = '\0';
		/* labels are space or NUL padded to the element width */
		for(long j = (long)strlen(sDest) - 1; (j >= 0)&&(sDest[j] == ' '); --j) sDest[j] = '\0';
		while(*sDest == ' ') memmove(sDest, sDest + 1, strlen(sDest));
		++(pV->nLabels);
	}
	free(pBuf);
	return pV->nLabels;
}

/* ASCII spellings of the canonical symbols, which the library keeps as
   Greek glyphs.  Compared case-insensitively. */
static const struct { const char* sAscii; const char* sGlyph; } g_aSymAlias[] = {
	{"theta",  "\xCE\xB8"},   /* small theta  */
	{"phi",    "\xCF\x86"},   /* small phi    */
	{"rho",    "\xCF\x81"},   /* small rho    */
	{"lambda", "\xCE\xBB"},   /* small lambda */
	{"lat",    "\xCE\xBB"},
	{"lon",    "\xCF\x86"},
	{"long",   "\xCF\x86"},
	{"alt",    "h"},
	{NULL, NULL}
};

/* Does one label token name direction iDir of system uSys? */
static bool _tokenIsDir(ubyte uSys, int iDir, const char* sTok)
{
	const char* sSym = (uSys >= DAS_VSYS_DETIC) ? das_geosys_symbol(uSys, iDir)
	                                            : das_vsys_symbol(uSys, iDir);
	if(sSym == NULL) return false;
	if(strcasecmp(sTok, sSym) == 0) return true;
	for(int i = 0; g_aSymAlias[i].sAscii != NULL; ++i)
		if((strcasecmp(sTok, g_aSymAlias[i].sAscii) == 0)&&(strcmp(g_aSymAlias[i].sGlyph, sSym) == 0))
			return true;
	return false;
}

/* Does a whole label, affixes and all, name direction iDir?  Tokens split
   on '_' are tried whole ("r_IAU_EARTH"), then a bare trailing or leading
   glyph on the first token ("Bx", "xB"). */
static bool _labelIsDir(ubyte uSys, int iDir, const char* sLabel)
{
	char sBuf[32];
	strncpy(sBuf, sLabel, sizeof(sBuf) - 1);
	sBuf[sizeof(sBuf) - 1] = '\0';

	char* sTok = sBuf;
	char* sFirst = NULL;
	while(sTok != NULL){
		char* sNext = strchr(sTok, '_');
		if(sNext != NULL){ *sNext = '\0'; ++sNext; }
		if(sFirst == NULL) sFirst = sTok;
		if(_tokenIsDir(uSys, iDir, sTok)) return true;
		sTok = sNext;
	}
	size_t uLen = (sFirst != NULL) ? strlen(sFirst) : 0;
	if(uLen > 1){
		char sOne[2] = { sFirst[uLen - 1], '\0' };
		if(_tokenIsDir(uSys, iDir, sOne)) return true;
		sOne[0] = sFirst[0];
		if(_tokenIsDir(uSys, iDir, sOne)) return true;
	}
	return false;
}

/* Which system the ordered labels spell, DAS_VSYS_UNKNOWN if none */
static ubyte _labelsSystem(const cdf_var_t* pV)
{
	if((pV->nLabels < 1)||(pV->nLabels > 3)) return DAS_VSYS_UNKNOWN;
	const ubyte aTry[] = {
		DAS_VSYS_CART, DAS_VSYS_CYL, DAS_VSYS_SPH, DAS_VSYS_CENTRIC,
		DAS_VSYS_DETIC, DAS_VSYS_GRAPHIC
	};
	for(size_t u = 0; u < sizeof(aTry); ++u){
		bool bAll = true;
		for(int i = 0; i < pV->nLabels; ++i)
			if(!_labelIsDir(aTry[u], i, pV->aLabels[i])){ bAll = false; break; }
		if(bAll) return aTry[u];
	}
	return DAS_VSYS_UNKNOWN;
}

static const char* _sysName(ubyte uSys)
{
	if(uSys >= DAS_VSYS_DETIC) return das_geosys_str(uSys);
	return das_vsys_str(uSys);
}

/* printf's %-Ns pads by bytes; glyphs need padding by display width */
static void _padTo(const char* sStr, int nWidth)
{
	int nHave = (int)u8_strwidth(sStr);
	fputs(sStr, stdout);
	for(int i = nHave; i < nWidth; ++i) fputc(' ', stdout);
}

/* ************************************************************************* */
/* Classification */

/* Is a support variable a coordinate: something that could label a plot
   axis as an alternative reading of the primary coordinate?  A position
   (frame, or a spherical or geodetic label set, or a loc/pos name with a
   distance unit), a time by DICT_KEY, or anything --coord names. */
static bool _isCoordLike(const cdf_var_t* pV, const popts_t* pOpts)
{
	if(pV->sFrame[0] != '\0') return true;
	if((pV->uSys == DAS_VSYS_SPH)||(pV->uSys == DAS_VSYS_CENTRIC)||
	   (pV->uSys == DAS_VSYS_DETIC)||(pV->uSys == DAS_VSYS_GRAPHIC)) return true;
	if((strncmp(pV->sDictKey, "time>", 5) == 0)||(strncmp(pV->sDictKey, "position>", 9) == 0)) return true;

	char sLow[CVAR_NAME_SZ];
	size_t u = 0;
	for(; pV->sName[u] != '\0'; ++u) sLow[u] = (char)tolower((unsigned char)pV->sName[u]);
	sLow[u] = '\0';
	if((strstr(sLow, "loc") != NULL)||(strstr(sLow, "pos") != NULL)){
		das_units units = (pV->sUnits[0] != '\0') ? Units_fromStr(pV->sUnits) : NULL;
		if((units != NULL)&&Units_canConvert(units, UNIT_KM)) return true;
	}

	/* named on the command line as a coordinate variable */
	if(pOpts->aCoordMap[0] != '\0'){
		const char* p = pOpts->aCoordMap;
		while((p = strchr(p, ':')) != NULL){
			++p;
			size_t n = strcspn(p, ",");
			if((n == strlen(pV->sName))&&(strncmp(p, pV->sName, n) == 0)) return true;
			p += n;
		}
	}
	return false;
}

/* Records of a time base that fall in the range, or -1 if unbounded */
static long _recsInRange(cdf_file_t* pFile, cdf_var_t* pT, const das_range* pRng)
{
	if((pRng == NULL)||(pRng->dBeg.vt != vtTime)) return -1;
	if((pT->nRecs < 1)||(pT->nDims != 0)) return -1;

	das_time dtBeg, dtEnd;
	das_datum_toTime(&(pRng->dBeg), &dtBeg);
	das_datum_toTime(&(pRng->dEnd), &dtEnd);

	size_t uSz = (pT->nType == CDF_EPOCH) ? sizeof(double) : sizeof(int64_t);
	ubyte* pBuf = (ubyte*)malloc(uSz * pT->nRecs);
	if(pBuf == NULL) return -1;
	if(CDFgetzVarAllRecordsByVarID(pFile->id, pT->nVarNum, pBuf) != CDF_OK){
		free(pBuf);
		return -1;
	}

	long nIn = 0;
	if(pT->nType == CDF_EPOCH){
		double rBeg = computeEPOCH(dtBeg.year, dtBeg.month, dtBeg.mday, dtBeg.hour,
			dtBeg.minute, (long)dtBeg.second, (long)((dtBeg.second - (long)dtBeg.second)*1000));
		double rEnd = computeEPOCH(dtEnd.year, dtEnd.month, dtEnd.mday, dtEnd.hour,
			dtEnd.minute, (long)dtEnd.second, (long)((dtEnd.second - (long)dtEnd.second)*1000));
		const double* pVal = (const double*)pBuf;
		for(long i = 0; i < pT->nRecs; ++i)
			if((pVal[i] >= rBeg)&&(pVal[i] < rEnd)) ++nIn;
	}
	else{
		int64_t nBeg = dt_to_tt2k(&dtBeg);
		int64_t nEnd = dt_to_tt2k(&dtEnd);
		const int64_t* pVal = (const int64_t*)pBuf;
		for(long i = 0; i < pT->nRecs; ++i)
			if((pVal[i] >= nBeg)&&(pVal[i] < nEnd)) ++nIn;
	}
	free(pBuf);
	return nIn;
}

/* Common prefix of the VAR_TYPE=data member names (all members when there
   are none), cut back to the last '_'.  No common prefix: the dataset takes
   the group name. */
static void _dsNameFromMembers(cdf_file_t* pFile, cdf_ds_t* pDs)
{
	const char* sFirst = NULL;
	size_t uPre = 0;
	int nUsed = 0;
	for(int nPass = 0; (nPass < 2)&&(nUsed == 0); ++nPass){
		for(int i = 0; i < pDs->nMembers; ++i){
			const cdf_var_t* pV = pFile->aVars + pDs->aMembers[i];
			if((nPass == 0)&&(strcmp(pV->sVarType, "data") != 0)) continue;
			if(sFirst == NULL){ sFirst = pV->sName; uPre = strlen(sFirst); }
			else{
				size_t u = 0;
				while((u < uPre)&&(pV->sName[u] != '\0')&&(pV->sName[u] == sFirst[u])) ++u;
				uPre = u;
			}
			++nUsed;
		}
	}
	if(nUsed > 1){
		/* trim a partial token */
		while((uPre > 0)&&(sFirst[uPre - 1] != '_')) --uPre;
		while((uPre > 0)&&(sFirst[uPre - 1] == '_')) --uPre;
	}
	if((sFirst == NULL)||(uPre == 0)){
		strncpy(pDs->sName, pDs->sGroup, sizeof(pDs->sName) - 1);
		return;
	}
	if(uPre > sizeof(pDs->sName) - 1) uPre = sizeof(pDs->sName) - 1;
	memcpy(pDs->sName, sFirst, uPre);
	pDs->sName[uPre] = '\0';
}

/* Decide what each variable is and how the selected ones are shaped.
   Every guess lands in pFile->sAdvice as well as the log. */
static int _classify(cdf_file_t* pFile, const popts_t* pOpts)
{
	/* (1) time bases: typed, or named by --coord time:VAR */
	char sCoordTime[CVAR_NAME_SZ] = {'\0'};
	if(pOpts->aCoordMap[0] != '\0'){
		const char* p = strstr(pOpts->aCoordMap, "time:");
		if((p != NULL)&&((p == pOpts->aCoordMap)||(p[-1] == ','))){
			p += 5;
			size_t u = 0;
			while((p[u] != '\0')&&(p[u] != ',')&&(u < CVAR_NAME_SZ - 1)){ sCoordTime[u] = p[u]; ++u; }
			sCoordTime[u] = '\0';
		}
	}
	if((sCoordTime[0] != '\0')&&(_varIndex(pFile, sCoordTime) < 0))
		return das_error(PERR, "--coord names time variable '%s', which is not in %s",
			sCoordTime, pFile->sPath);

	int nTimeBases = 0;
	for(int i = 0; i < pFile->nVars; ++i){
		cdf_var_t* pV = pFile->aVars + i;
		bool bNamed = (sCoordTime[0] != '\0')&&(strcmp(pV->sName, sCoordTime) == 0);
		if(pV->nType == CDF_EPOCH16){
			pV->role = ROLE_IGNORE;
			strcpy(pV->sWhy, "CDF_EPOCH16 is not supported, convert the file with cdfconvert");
			_advise(pFile, "%s is CDF_EPOCH16; run the file through cdfconvert -epoch2tt2000 to use it", pV->sName);
			continue;
		}
		if(bNamed || (pV->bRecVary && _isTimeType(pV->nType) && (pV->nDims == 0))){
			pV->role = ROLE_TIME;
			++nTimeBases;
			if(bNamed && !_isTimeType(pV->nType))
				_advise(pFile, "%s was named as the time variable but is type %s, not TT2000 or EPOCH; "
					"values will be read as %s", pV->sName, _cdfTypeName(pV->nType), pV->sUnits);
		}
	}
	if(nTimeBases == 0)
		_advise(pFile, "no time variable found: nothing has type TT2000 or EPOCH.  Name one with "
			"-c time:VARIABLE");
	else{
		/* files with no DEPEND attributes at all (ESA style) */
		int nOrphans = 0, iBase = -1;
		for(int i = 0; i < pFile->nVars; ++i)
			if(pFile->aVars[i].role == ROLE_TIME){ iBase = i; break; }
		for(int i = 0; i < pFile->nVars; ++i){
			cdf_var_t* pV = pFile->aVars + i;
			if((pV->role == ROLE_UNK)&&pV->bRecVary&&(pV->asDepend[0][0] == '\0')&&
			   (pV->nRecs == pFile->aVars[iBase].nRecs))
				++nOrphans;
		}
		if((nOrphans > 0)&&(sCoordTime[0] == '\0'))
			_advise(pFile, "%d record varying variables have no DEPEND_0 but the same record count "
				"as %s; name the time base with -c time:%s to stream them on it",
				nOrphans, pFile->aVars[iBase].sName, pFile->aVars[iBase].sName);
		else if(nOrphans > 0)
			_advise(pFile, "%d record varying variables have no DEPEND_0; they are taken to "
				"depend on %s because the record counts match", nOrphans, sCoordTime);
	}

	/* (2) time dependence */
	for(int i = 0; i < pFile->nVars; ++i){
		cdf_var_t* pV = pFile->aVars + i;
		if(pV->role == ROLE_TIME || pV->role == ROLE_IGNORE) continue;

		if(strcmp(pV->sVarType, "ignore_data") == 0){
			pV->role = ROLE_IGNORE; strcpy(pV->sWhy, "VAR_TYPE ignore_data"); continue;
		}
		if(strcmp(pV->sVarType, "metadata") == 0){
			pV->role = ROLE_IGNORE; strcpy(pV->sWhy, "metadata not referenced by a streamed variable");
			continue;   /* may be promoted to a label set below */
		}
		if(!pV->bRecVary){
			pV->role = ROLE_IGNORE; strcpy(pV->sWhy, "not record varying, not referenced by a streamed variable");
			continue;   /* may be promoted to a coordinate or offset table below */
		}
		if((pV->nType == CDF_CHAR)||(pV->nType == CDF_UCHAR)){
			pV->role = ROLE_IGNORE; strcpy(pV->sWhy, "text values are not streamed yet");
			continue;
		}
		if(pV->asDepend[0][0] == '\0'){
			/* --coord named the base: a variable with no DEPEND_0 and the same
			   record count rides on it (files with no ISTP attributes) */
			int iNamed = (sCoordTime[0] != '\0') ? _varIndex(pFile, sCoordTime) : -1;
			if((iNamed >= 0)&&(pV->nRecs == pFile->aVars[iNamed].nRecs)){
				strncpy(pV->asDepend[0], sCoordTime, CVAR_NAME_SZ - 1);
			}
			else{
				pV->role = ROLE_IGNORE; strcpy(pV->sWhy, "no DEPEND_0");
				continue;
			}
		}
		int iDep0 = _varIndex(pFile, pV->asDepend[0]);
		if(iDep0 < 0){
			pV->role = ROLE_IGNORE;
			snprintf(pV->sWhy, sizeof(pV->sWhy), "DEPEND_0 %s is not in the file", pV->asDepend[0]);
			_advise(pFile, "%s names DEPEND_0 %s, which does not exist", pV->sName, pV->asDepend[0]);
			continue;
		}
		if(pFile->aVars[iDep0].role != ROLE_TIME){
			pV->role = ROLE_IGNORE;
			snprintf(pV->sWhy, sizeof(pV->sWhy), "DEPEND_0 %s is not a time variable", pV->asDepend[0]);
			continue;
		}
		pV->bTimeDep = true;
		pV->iTime = iDep0;
		pV->role = ROLE_DATA;
	}

	/* (3) selection: named, else --def-vars, else every time dependent
	       data or support variable */
	char sDefVars[1024];
	const char* asNames[MAX_DATA_VARS];
	int nNames = 0;
	if(pOpts->nVars > 0){
		for(int i = 0; i < pOpts->nVars; ++i) asNames[nNames++] = pOpts->asVars[i];
	}
	else if(pOpts->aDefVars[0] != '\0'){
		strncpy(sDefVars, pOpts->aDefVars, sizeof(sDefVars) - 1);
		sDefVars[sizeof(sDefVars) - 1] = '\0';
		char* sTok = sDefVars;
		while((sTok != NULL)&&(nNames < MAX_DATA_VARS)){
			char* sNext = strchr(sTok, ',');
			if(sNext != NULL){ *sNext = '\0'; ++sNext; }
			if(*sTok != '\0') asNames[nNames++] = sTok;
			sTok = sNext;
		}
	}

	int nSelected = 0;
	if(nNames > 0){
		int nBad = 0;
		for(int n = 0; n < nNames; ++n){
			/* VAR.COMPONENT selects one component of a composite */
			char sVar[CVAR_NAME_SZ + 32];
			strncpy(sVar, asNames[n], sizeof(sVar) - 1);
			sVar[sizeof(sVar) - 1] = '\0';
			char* sComp = strrchr(sVar, '.');
			if((sComp != NULL)&&(_varIndex(pFile, sVar) < 0)){ *sComp = '\0'; ++sComp; }
			else sComp = NULL;
			int i = _varIndex(pFile, sVar);
			if(i < 0){
				daslog_error_v("Requested variable '%s' is not in %s", asNames[n], pFile->sPath);
				++nBad; continue;
			}
			cdf_var_t* pV = pFile->aVars + i;
			if(sComp != NULL) strncpy(pV->sCompSel, sComp, sizeof(pV->sCompSel) - 1);
			if(!pV->bTimeDep){
				daslog_error_v("Requested variable '%s' cannot be streamed: %s", asNames[n],
					(pV->role == ROLE_TIME) ? "it is a time base, name a variable that depends on it" : pV->sWhy);
				++nBad; continue;
			}
			pV->bSelected = true;
			++nSelected;
		}
		if(nBad > 0) return PERR;
	}
	else{
		for(int i = 0; i < pFile->nVars; ++i){
			cdf_var_t* pV = pFile->aVars + i;
			if(!pV->bTimeDep) continue;
			if((strcmp(pV->sVarType, "data") == 0)||(strcmp(pV->sVarType, "support_data") == 0)||
			   (pV->sVarType[0] == '\0')){
				pV->bSelected = true;
				++nSelected;
			}
		}
	}

	/* (4) support pull, and the per variable structure */
	for(int i = 0; i < pFile->nVars; ++i){
		cdf_var_t* pV = pFile->aVars + i;
		if(!pV->bSelected) continue;

		pV->nExtRank = 1;
		pV->nComps = 1;
		for(int d = 1; d <= pV->nDims; ++d){
			const char* sDep = pV->asDepend[d];
			const char* sLbl = pV->asLablPtr[d];
			if(sDep[0] != '\0'){
				int iDep = _varIndex(pFile, sDep);
				if(iDep < 0){
					_advise(pFile, "%s names DEPEND_%d %s, which does not exist; the index is "
						"treated as a plain coordinate with no values", pV->sName, d, sDep);
				}
				else{
					cdf_var_t* pD = pFile->aVars + iDep;
					if(pD->role == ROLE_IGNORE || pD->role == ROLE_UNK){
						bool bOffset = (pD->sOffsetOf[0] != '\0')&&
							(strcmp(pD->sOffsetOf, pV->asDepend[0]) == 0);
						pD->role = bOffset ? ROLE_OFFSET : ROLE_COORD;
						snprintf(pD->sWhy, sizeof(pD->sWhy), "%s of %s",
							bOffset ? "time offset table" : "coordinate", pV->sName);
						pD->iUsedBy = i;
					}
				}
				++(pV->nExtRank);
			}
			else if(sLbl[0] != '\0'){
				pV->aIsInternal[d-1] = 1;
				pV->nComps *= (int)pV->aDimSz[d-1];
				if(pV->iCompDim < 0) pV->iCompDim = d - 1;
				int iLbl = _varIndex(pFile, sLbl);
				if(iLbl < 0){
					_advise(pFile, "%s names LABL_PTR_%d %s, which does not exist; components are unlabeled",
						pV->sName, d, sLbl);
				}
				else{
					cdf_var_t* pL = pFile->aVars + iLbl;
					if(pL->role == ROLE_IGNORE || pL->role == ROLE_UNK){
						pL->role = ROLE_LABEL;
						snprintf(pL->sWhy, sizeof(pL->sWhy), "component labels of %s", pV->sName);
						pL->iUsedBy = i;
					}
					if(pV->nLabels == 0) _readLabels(pFile, pV, iLbl);
				}
			}
			else{
				/* Neither: an ISTP violation.  Treat the index as components and
				   look for a LABL_PTR_N with N out of range whose label count
				   matches this index (shape-match repair, the EFI case). */
				pV->aIsInternal[d-1] = 1;
				pV->nComps *= (int)pV->aDimSz[d-1];
				int iFix = -1, nFix = 0;
				for(int k = pV->nDims + 1; (k < VARIDX_MAX)&&(iFix < 0); ++k){
					if(pV->asLablPtr[k][0] == '\0') continue;
					int iLbl = _varIndex(pFile, pV->asLablPtr[k]);
					if((iLbl >= 0)&&(pFile->aVars[iLbl].nDims == 1)&&
					   (pFile->aVars[iLbl].aDimSz[0] == pV->aDimSz[d-1])){
						iFix = iLbl; nFix = k;
					}
				}
				if(pV->iCompDim < 0) pV->iCompDim = d - 1;
				if(iFix >= 0){
					cdf_var_t* pL = pFile->aVars + iFix;
					if(pL->role == ROLE_IGNORE || pL->role == ROLE_UNK){
						pL->role = ROLE_LABEL;
						snprintf(pL->sWhy, sizeof(pL->sWhy), "component labels of %s (by length)", pV->sName);
						pL->iUsedBy = i;
					}
					if(pV->nLabels == 0) _readLabels(pFile, pV, iFix);
					_advise(pFile, "%s has %ld dimensions but names LABL_PTR_%d; its %ld labels are "
						"assumed to belong to index %d because the lengths match", pV->sName,
						pV->nDims, nFix, pL->aDimSz[0], d);
				}
				else{
					_advise(pFile, "%s index %d (size %ld) has no DEPEND_%d or LABL_PTR_%d; it is treated "
						"as an unlabeled component axis", pV->sName, d, pV->aDimSz[d-1], d, d);
				}
			}
		}

		const char* asDelta[2] = { pV->sDeltaPlus, pV->sDeltaMinus };
		for(int k = 0; k < 2; ++k){
			if(asDelta[k][0] == '\0') continue;
			int iD = _varIndex(pFile, asDelta[k]);
			if(iD < 0){
				_advise(pFile, "%s names %s %s, which does not exist", pV->sName,
					(k == 0) ? "DELTA_PLUS_VAR" : "DELTA_MINUS_VAR", asDelta[k]);
				continue;
			}
			cdf_var_t* pD = pFile->aVars + iD;
			if(!pD->bSelected && (pD->role != ROLE_TIME)){
				pD->role = ROLE_DELTA;
				snprintf(pD->sWhy, sizeof(pD->sWhy), "uncertainty of %s", pV->sName);
				pD->iUsedBy = i;
			}
		}

		/* kind */
		int nIntDims = 0;
		for(int d = 0; d < pV->nDims; ++d) nIntDims += pV->aIsInternal[d];
		if(nIntDims == 0){
			strcpy(pV->sKind, "scalar");
		}
		else if(nIntDims >= 2){
			snprintf(pV->sKind, sizeof(pV->sKind), "matrix");
			_advise(pFile, "%s has %d component axes (%d elements); it is sent as a plain "
				"composite, name it a rotation in a map file when that exists", pV->sName, nIntDims, pV->nComps);
		}
		else if((pV->nLabels == 2)&&(
			((strcasecmp(pV->aLabels[0], "real") == 0)&&(strncasecmp(pV->aLabels[1], "imag", 4) == 0)) ||
			((strncasecmp(pV->aLabels[0], "mag", 3) == 0)&&(strcasecmp(pV->aLabels[1], "phase") == 0))
		)){
			strcpy(pV->sKind, "complex");
			strcpy(pV->sSystem, (strcasecmp(pV->aLabels[0], "real") == 0) ? "rectangular" : "polar");
			snprintf(pV->sSyms, sizeof(pV->sSyms), "%s,%s", pV->aLabels[0], pV->aLabels[1]);
		}
		else{
			ubyte uSys = _labelsSystem(pV);
			pV->uSys = uSys;
			if(uSys != DAS_VSYS_UNKNOWN){
				/* which canonical direction each label is; the form needs the
				   list because a file may carry fewer than three */
				for(int c = 0; c < pV->nLabels; ++c)
					for(int iDir = 0; iDir < 3; ++iDir)
						if(_labelIsDir(uSys, iDir, pV->aLabels[c])){ pV->aDirs[c] = (ubyte)iDir; break; }
				strncpy(pV->sSystem, _sysName(uSys), sizeof(pV->sSystem) - 1);
				pV->sSyms[0] = '\0';
				for(int c = 0; c < pV->nComps && c < 3; ++c){
					const char* sSym = (uSys >= DAS_VSYS_DETIC) ? das_geosys_symbol(uSys, c) : das_vsys_symbol(uSys, c);
					if(c > 0) strcat(pV->sSyms, ",");
					strncat(pV->sSyms, sSym, sizeof(pV->sSyms) - strlen(pV->sSyms) - 1);
				}
				if(pV->sFrame[0] != '\0'){
					strcpy(pV->sKind, "vector");
				}
				else{
					strcpy(pV->sKind, "composite");
					char sAttrs[512];
					_varAttrNames(pFile->id, pV->nVarNum, sAttrs, sizeof(sAttrs));
					_advise(pFile, "%s looks like a %s vector (%s) but no attribute maps to 'frame'. "
						"If one of its attributes names the frame, add it with -p ATTR:frame; "
						"otherwise the frame can be set per variable in a map file once that "
						"exists.  Attributes present: %s", pV->sName, pV->sSystem, pV->sSyms, sAttrs);
				}
			}
			else{
				strcpy(pV->sKind, "composite");
				pV->sSyms[0] = '\0';
				for(int c = 0; c < pV->nLabels && c < 6; ++c){
					if(c > 0) strcat(pV->sSyms, ",");
					strncat(pV->sSyms, pV->aLabels[c], sizeof(pV->sSyms) - strlen(pV->sSyms) - 1);
				}
				if(pV->nLabels > 0 && pV->nLabels <= 3)
					_advise(pFile, "%s has %d labeled components (%s) that spell no known system; "
						"it is sent as a plain composite", pV->sName, pV->nLabels, pV->sSyms);
			}
		}
	}

	/* (4b) component selection: one component of a composite keeps its form
	   with a one-entry direction list; the read window narrows to it */
	for(int i = 0; i < pFile->nVars; ++i){
		cdf_var_t* pV = pFile->aVars + i;
		if(!pV->bSelected || (pV->sCompSel[0] == '\0')) continue;
		if(pV->iCompDim < 0)
			return das_error(PERR, "%s has no components, '%s.%s' selects nothing",
				pV->sName, pV->sName, pV->sCompSel);
		if(strcmp(pV->sKind, "complex") == 0)
			return das_error(PERR, "%s is a complex number; its parts cannot be sent separately",
				pV->sName);
		int nAlong = (int)pV->aDimSz[pV->iCompDim];
		int iSel = -1;
		for(int c = 0; (c < pV->nLabels)&&(iSel < 0); ++c)       /* the label as written */
			if(strcmp(pV->aLabels[c], pV->sCompSel) == 0) iSel = c;
		if((iSel < 0)&&(pV->uSys != DAS_VSYS_UNKNOWN)){           /* or the bare symbol */
			for(int c = 0; (c < pV->nLabels)&&(iSel < 0); ++c)
				if(_tokenIsDir(pV->uSys, pV->aDirs[c], pV->sCompSel)) iSel = c;
		}
		if(iSel < 0){                                             /* or a number */
			char* pEnd = NULL;
			long n = strtol(pV->sCompSel, &pEnd, 10);
			if((*pEnd == '\0')&&(pEnd != pV->sCompSel)&&(n >= 0)&&(n < nAlong)) iSel = (int)n;
		}
		if(iSel < 0)
			return das_error(PERR, "%s has no component '%s'", pV->sName, pV->sCompSel);
		if(pV->nDims - 1 > pV->iCompDim){
			/* only the first component axis is selectable for now */
			for(int d = pV->iCompDim + 1; d < pV->nDims; ++d)
				if(pV->aIsInternal[d])
					return das_error(PERR, "%s has more than one component axis, select on the "
						"first only", pV->sName);
		}
		pV->iCompSel = iSel;
		pV->nComps /= nAlong;
		pV->aDimSz[pV->iCompDim] = 1;
		if(pV->nLabels > iSel){
			char sOne[32]; strcpy(sOne, pV->aLabels[iSel]);
			strcpy(pV->aLabels[0], sOne);
			pV->aDirs[0] = pV->aDirs[iSel];
			pV->nLabels = 1;
		}
		if(pV->uSys != DAS_VSYS_UNKNOWN){
			snprintf(pV->sSyms, sizeof(pV->sSyms), "%s",
				(pV->uSys >= DAS_VSYS_DETIC) ? das_geosys_symbol(pV->uSys, pV->aDirs[0])
				                             : das_vsys_symbol(pV->uSys, pV->aDirs[0]));
		}
		else
			strncpy(pV->sSyms, pV->aLabels[0], sizeof(pV->sSyms) - 1);
		daslog_info_v("%s: sending component %d (%s) only", pV->sName, iSel, pV->aLabels[0]);
	}

	/* (5) datasets: selected variables sharing a DEPEND chain */
	for(int i = 0; i < pFile->nVars; ++i){
		cdf_var_t* pV = pFile->aVars + i;
		if(!pV->bSelected) continue;

		char sChain[VARIDX_MAX * CVAR_NAME_SZ] = {'\0'};
		for(int d = 0; d <= pV->nDims; ++d){
			if(pV->asDepend[d][0] == '\0') continue;
			if(sChain[0] != '\0') strcat(sChain, ";");
			strcat(sChain, pV->asDepend[d]);
		}
		int iDs = -1;
		for(int j = 0; j < pFile->nDs; ++j)
			if(strcmp(pFile->aDs[j].sChain, sChain) == 0){ iDs = j; break; }
		if(iDs < 0){
			if(pFile->nDs >= MAX_CDF_DS)
				return das_error(PERR, "More than %d datasets in %s", MAX_CDF_DS, pFile->sPath);
			iDs = pFile->nDs++;
			cdf_ds_t* pDs = pFile->aDs + iDs;
			memset(pDs, 0, sizeof(cdf_ds_t));
			strcpy(pDs->sChain, sChain);
			pDs->nRank = pV->nExtRank;
			pDs->iTime = pV->iTime;
			/* group from the DICT_KEY class of the first member */
			if(pV->sDictKey[0] != '\0'){
				const char* sGt = strchr(pV->sDictKey, '>');
				size_t u = (sGt != NULL) ? (size_t)(sGt - pV->sDictKey) : strlen(pV->sDictKey);
				if(u > sizeof(pDs->sGroup) - 1) u = sizeof(pDs->sGroup) - 1;
				memcpy(pDs->sGroup, pV->sDictKey, u);
				pDs->sGroup[u] = '\0';
			}
		}
		pV->iDs = iDs;
		pFile->aDs[iDs].aMembers[pFile->aDs[iDs].nMembers++] = i;
	}
	/* (6) fold: a support variable whose DEPEND chain is a proper prefix of
	   another dataset's chain joins that dataset, degenerate on the extra
	   indices, as a coordinate annotation when it is coordinate-like and as
	   data otherwise.  Its own dataset goes away when emptied. */
	for(int j = 0; j < pFile->nDs; ++j){
		cdf_ds_t* pSrc = pFile->aDs + j;
		size_t uLen = strlen(pSrc->sChain);
		int iBest = -1, nBestRank = pSrc->nRank;
		for(int k = 0; k < pFile->nDs; ++k){
			if(k == j) continue;
			cdf_ds_t* pDst = pFile->aDs + k;
			if((strncmp(pDst->sChain, pSrc->sChain, uLen) != 0)||(pDst->sChain[uLen] != ';')) continue;
			if(pDst->nRank > nBestRank){ iBest = k; nBestRank = pDst->nRank; }
		}
		if(iBest < 0) continue;
		cdf_ds_t* pDst = pFile->aDs + iBest;
		int nKept = 0;
		for(int m = 0; m < pSrc->nMembers; ++m){
			cdf_var_t* pV = pFile->aVars + pSrc->aMembers[m];
			if(strcmp(pV->sVarType, "support_data") != 0){
				pSrc->aMembers[nKept++] = pSrc->aMembers[m];
				continue;
			}
			pV->bFolded = true;
			pV->bAnnot = _isCoordLike(pV, pOpts);
			pV->iDs = iBest;
			pDst->aMembers[pDst->nMembers++] = pSrc->aMembers[m];
		}
		pSrc->nMembers = nKept;
	}
	/* drop emptied datasets */
	{
		int nOut = 0;
		for(int j = 0; j < pFile->nDs; ++j){
			if(pFile->aDs[j].nMembers == 0) continue;
			if(nOut != j){
				pFile->aDs[nOut] = pFile->aDs[j];
				for(int m = 0; m < pFile->aDs[nOut].nMembers; ++m)
					pFile->aVars[pFile->aDs[nOut].aMembers[m]].iDs = nOut;
			}
			++nOut;
		}
		pFile->nDs = nOut;
	}

	/* Group names.  Never a coordinate name: a dataset of support variables
	   only is "support", the rest fall from DICT_KEY to the file's source
	   name to a default. */
	int nSupportDs = 0;
	for(int j = 0; j < pFile->nDs; ++j){
		cdf_ds_t* pDs = pFile->aDs + j;
		bool bAllSupport = true;   /* no VAR_TYPE at all counts as data */
		for(int m = 0; m < pDs->nMembers; ++m)
			if(strcmp(pFile->aVars[pDs->aMembers[m]].sVarType, "support_data") != 0){ bAllSupport = false; break; }
		if(bAllSupport){
			++nSupportDs;
			if(nSupportDs == 1) strcpy(pDs->sGroup, "support");
			else snprintf(pDs->sGroup, sizeof(pDs->sGroup), "support_%d", nSupportDs);
		}
		else if(pDs->sGroup[0] == '\0'){
			if(pFile->sSource[0] != '\0') strncpy(pDs->sGroup, pFile->sSource, sizeof(pDs->sGroup) - 1);
			else strcpy(pDs->sGroup, "def_group");
		}
		_dsNameFromMembers(pFile, pDs);
		_dasId(pDs->sGroup);
		_dasId(pDs->sName);
	}

	if(nSelected == 0)
		_advise(pFile, "nothing selected to stream: no variable depends on a time base");

	return DAS_OKAY;
}

/* ************************************************************************* */
/* The dry run listing.  One line per model level: dataset, dimension,
   variable, then the variable's components, ops and source, in the wire
   vocabulary so the listing reads as a preview of the header. */

/* das3 storage word for a CDF type */
static const char* _storageName(long nType)
{
	switch(nType){
	case CDF_INT1:  return "byte";    case CDF_UINT1: return "ubyte";
	case CDF_INT2:  return "short";   case CDF_UINT2: return "ushort";
	case CDF_INT4:  return "int";     case CDF_UINT4: return "uint";
	case CDF_INT8:  case CDF_TIME_TT2000: return "long";
	case CDF_REAL4: case CDF_FLOAT:   return "float";
	case CDF_REAL8: case CDF_DOUBLE:  case CDF_EPOCH: return "double";
	case CDF_CHAR:  case CDF_UCHAR:   return "utf8";
	default: return "?";
	}
}

static const char* _unitsName(const cdf_var_t* pV)
{
	if(pV->nType == CDF_TIME_TT2000) return "TT2000";
	return pV->sUnits[0] ? pV->sUnits : "";
}

/* Is a non record varying rank-1 numeric table an arithmetic sequence?
   Values are read whole; these tables are small by nature. */
static bool _isSequence(cdf_file_t* pFile, const cdf_var_t* pV, double* pMin, double* pStep)
{
	if(pV->bRecVary || (pV->nDims != 1) || (pV->aDimSz[0] < 2)) return false;
	if(pV->aDimSz[0] > 1048576) return false;
	long nSz = 0;
	if(CDFgetDataTypeSize(pV->nType, &nSz) != CDF_OK) return false;
	long nVals = pV->aDimSz[0];
	ubyte* pBuf = (ubyte*)malloc(nSz * nVals);
	if(pBuf == NULL) return false;
	if(CDFgetzVarRecordData(pFile->id, pV->nVarNum, 0, pBuf) != CDF_OK){ free(pBuf); return false; }

	bool bOk = true;
	double rPrev = 0.0, rStep = 0.0;
	for(long i = 0; (i < nVals) && bOk; ++i){
		double r = 0.0;
		const ubyte* p = pBuf + i * nSz;
		switch(pV->nType){
		case CDF_INT1:  r = *((const int8_t*)p);   break;
		case CDF_UINT1: r = *((const uint8_t*)p);  break;
		case CDF_INT2:  r = *((const int16_t*)p);  break;
		case CDF_UINT2: r = *((const uint16_t*)p); break;
		case CDF_INT4:  r = *((const int32_t*)p);  break;
		case CDF_UINT4: r = *((const uint32_t*)p); break;
		case CDF_INT8:  r = (double)*((const int64_t*)p); break;
		case CDF_REAL4: case CDF_FLOAT:  r = *((const float*)p);  break;
		case CDF_REAL8: case CDF_DOUBLE: r = *((const double*)p); break;
		default: bOk = false; continue;
		}
		if(i == 0){ *pMin = r; }
		else if(i == 1){ rStep = r - rPrev; if(rStep == 0.0) bOk = false; }
		else{
			double rDiff = r - rPrev;
			double rTol = (rStep < 0 ? -rStep : rStep) * 1e-6;
			if(rTol < 1e-12) rTol = 1e-12;
			if((rDiff - rStep > rTol)||(rStep - rDiff > rTol)) bOk = false;
		}
		rPrev = r;
	}
	free(pBuf);
	if(bOk) *pStep = rStep;
	return bOk;
}

/* External index string: the record index then each non-component dim,
   then '-' for every dataset index the variable does not reach */
static void _idxStr(const cdf_var_t* pV, int nDsRank, char* sBuf, size_t uLen)
{
	strncpy(sBuf, "*", uLen - 1);
	int nHave = 1;
	for(int d = 0; d < pV->nDims; ++d){
		if(pV->aIsInternal[d]) continue;
		char sOne[24];
		snprintf(sOne, sizeof(sOne), ";%ld", pV->aDimSz[d]);
		strncat(sBuf, sOne, uLen - strlen(sBuf) - 1);
		++nHave;
	}
	for(; nHave < nDsRank; ++nHave)
		strncat(sBuf, ";-", uLen - strlen(sBuf) - 1);
}

static void _internStr(const cdf_var_t* pV, char* sBuf, size_t uLen)
{
	sBuf[0] = '\0';
	for(int d = 0; d < pV->nDims; ++d){
		if(!pV->aIsInternal[d]) continue;
		char sOne[24];
		snprintf(sOne, sizeof(sOne), "%s%ld", sBuf[0] ? ";" : "", pV->aDimSz[d]);
		strncat(sBuf, sOne, uLen - strlen(sBuf) - 1);
	}
}

static void _section(const char* sTitle)
{
	printf("\n%s\n", sTitle);
	for(const char* p = sTitle; *p; ++p) fputc('-', stdout);
	fputc('\n', stdout);
}

/* The variable line and its sub-lines */
static void _printVar(cdf_file_t* pFile, const cdf_var_t* pV, const char* sRole, const char* sIdx, int nIndent)
{
	char sIntern[32];
	_internStr(pV, sIntern, sizeof(sIntern));
	printf("%*s%-7s %s  %s", nIndent, "", sRole, pV->sName, sIntern[0] ? "composite" : "scalar");
	printf("  storage=%s", _storageName(pV->nType));
	if(_unitsName(pV)[0]) printf("  units=%s", _unitsName(pV));
	printf("  index=%s", sIdx);
	if(sIntern[0]) printf("  intern=%s", sIntern);   /* storage order: external then internal */
	printf("\n");

	nIndent += 2;
	if(pV->nLabels > 0){
		printf("%*sCOMPS ", nIndent, "");
		for(int c = 0; c < pV->nLabels; ++c) printf("%s%s", c ? "," : "", pV->aLabels[c]);
		/* which variable the labels came from */
		for(int d = 1; d <= pV->nDims; ++d)
			if(pV->aIsInternal[d-1] && pV->asLablPtr[d][0]){ printf(" (%s)", pV->asLablPtr[d]); break; }
		for(int k = pV->nDims + 1; k < VARIDX_MAX; ++k)
			if(pV->asLablPtr[k][0]){ printf(" (%s)", pV->asLablPtr[k]); break; }
		printf("\n");
	}
	if(strcmp(pV->sKind, "vector") == 0)
		printf("%*sOPS vector system=%s frame=%s\n", nIndent, "", pV->sSystem, pV->sFrame);
	else if(strcmp(pV->sKind, "complex") == 0)
		printf("%*sOPS complex system=%s\n", nIndent, "", pV->sSystem);
	else if(pV->nType == CDF_TIME_TT2000 || pV->nType == CDF_EPOCH)
		printf("%*sOPS point\n", nIndent, "");

	double rMin = 0.0, rStep = 0.0;
	if(_isSequence(pFile, pV, &rMin, &rStep)){
		/* the letter of the one index the table runs along */
		char cIdx = 'i';
		int iPos = 0;
		for(const char* p = sIdx; *p != '\0'; ++p){
			if(*p == ';'){ ++iPos; continue; }
			if((*p >= '0')&&(*p <= '9')){ cIdx = (char)('i' + iPos); break; }
		}
		printf("%*sSRC sequence (%.15g + %.15g*%c)\n", nIndent, "", rMin, rStep, cIdx);
	}
	else
		printf("%*sSRC array (%s)\n", nIndent, "", pV->sName);
}

static void _listFile(cdf_file_t* pFile, const popts_t* pOpts)
{
	const char* sBase = strrchr(pFile->sPath, '/');
	sBase = (sBase != NULL) ? sBase + 1 : pFile->sPath;

	const das_range* pTimeRng = NULL;
	for(int i = 0; i < pOpts->nRanges; ++i)
		if(strcmp(pOpts->aRanges[i].sCoord, "time") == 0) pTimeRng = pOpts->aRanges + i;

	char sTitle[DURI_MAX_PATH + 32];
	snprintf(sTitle, sizeof(sTitle), "Time bases in %s", sBase);
	_section(sTitle);
	for(int i = 0; i < pFile->nVars; ++i){
		cdf_var_t* pV = pFile->aVars + i;
		if(pV->role != ROLE_TIME) continue;
		long nIn = _recsInRange(pFile, pV, pTimeRng);
		printf("%s  storage=%s  units=%s  records=%ld", pV->sName, _storageName(pV->nType),
			_unitsName(pV), pV->nRecs);
		if(nIn >= 0) printf("  in_range=%ld", nIn);
		printf("\n");
	}

	_section("Datasets");
	if(pFile->nDs == 0) printf("(none)\n");
	char sIdx[64];
	for(int j = 0; j < pFile->nDs; ++j){
		cdf_ds_t* pDs = pFile->aDs + j;
		cdf_var_t* pFirst = pFile->aVars + pDs->aMembers[0];
		_idxStr(pFirst, pDs->nRank, sIdx, sizeof(sIdx));
		if(j > 0) printf("\n");
		printf("DS %s  group=%s  rank=%d  index=%s\n", pDs->sName, pDs->sGroup, pDs->nRank, sIdx);

		/* the time coordinate: center, or reference plus offset */
		cdf_var_t* pT = pFile->aVars + pDs->iTime;
		int iOff = -1;
		for(int d = 1; d <= pFirst->nDims; ++d){
			int iDep = _varIndex(pFile, pFirst->asDepend[d]);
			if((iDep >= 0)&&(pFile->aVars[iDep].role == ROLE_OFFSET)){ iOff = iDep; break; }
		}
		printf("  COORD time  axis=x\n");
		if(iOff < 0){
			strcpy(sIdx, "*");
			for(int r = 1; r < pDs->nRank; ++r) strcat(sIdx, ";-");
			_printVar(pFile, pT, "CENTER", sIdx, 4);
		}
		else{
			strcpy(sIdx, "*");
			for(int r = 1; r < pDs->nRank; ++r) strcat(sIdx, ";-");
			_printVar(pFile, pT, "REF", sIdx, 4);
			snprintf(sIdx, sizeof(sIdx), "-;%ld", pFile->aVars[iOff].aDimSz[0]);
			_printVar(pFile, pFile->aVars + iOff, "OFFSET", sIdx, 4);
		}

		/* the other coordinates, one dimension each */
		const char* asAxes[] = {"x", "y", "z", "w"};
		int iAxis = 1;
		int iExt = 1;
		for(int d = 1; d <= pFirst->nDims; ++d){
			if(pFirst->asDepend[d][0] == '\0') continue;
			int iDep = _varIndex(pFile, pFirst->asDepend[d]);
			if(iDep == iOff){ ++iExt; continue; }
			if(iAxis < 4) printf("\n  COORD %s  axis=%s\n", pFirst->asDepend[d], asAxes[iAxis]);
			else          printf("\n  COORD %s\n", pFirst->asDepend[d]);
			++iAxis;
			if(iDep < 0){
				printf("    CENTER %s  (not in the file)\n", pFirst->asDepend[d]);
				++iExt;
				continue;
			}
			cdf_var_t* pD = pFile->aVars + iDep;
			/* shape: the record index if it varies, then this index */
			strcpy(sIdx, pD->bRecVary ? "*" : "-");
			for(int r = 1; r < pDs->nRank; ++r){
				char sOne[24];
				snprintf(sOne, sizeof(sOne), ";%s", (r == iExt) ? "^" : "-");
				if(r == iExt) snprintf(sOne, sizeof(sOne), ";%ld", pD->nDims > 0 ? pD->aDimSz[pD->nDims - 1] : 1);
				strcat(sIdx, sOne);
			}
			_printVar(pFile, pD, "CENTER", sIdx, 4);
			++iExt;
		}

		/* folded coordinate annotations first, then data dimensions, one per
		   member, with any uncertainty variables */
		size_t uPre = strlen(pDs->sName);
		for(int m = 0; m < pDs->nMembers; ++m){
			cdf_var_t* pV = pFile->aVars + pDs->aMembers[m];
			if(!pV->bAnnot) continue;
			printf("\n  COORD %s  annotation=x\n", pV->sName);
			_idxStr(pV, pDs->nRank, sIdx, sizeof(sIdx));
			_printVar(pFile, pV, "CENTER", sIdx, 4);
		}
		for(int m = 0; m < pDs->nMembers; ++m){
			cdf_var_t* pV = pFile->aVars + pDs->aMembers[m];
			if(pV->bAnnot) continue;
			const char* sDim = pV->sName;
			if((strncmp(sDim, pDs->sName, uPre) == 0)&&(sDim[uPre] == '_')&&(sDim[uPre+1] != '\0'))
				sDim += uPre + 1;
			char sPhys[64];
			if(pV->sDictKey[0] != '\0'){
				const char* sGt = strchr(pV->sDictKey, '>');
				size_t u = (sGt != NULL) ? (size_t)(sGt - pV->sDictKey) : strlen(pV->sDictKey);
				if(u > sizeof(sPhys) - 1) u = sizeof(sPhys) - 1;
				memcpy(sPhys, pV->sDictKey, u); sPhys[u] = '\0';
			}
			else
				strncpy(sPhys, sDim, sizeof(sPhys) - 1);
			printf("\n  DATA %s  physDim=%s\n", sDim, sPhys);
			_idxStr(pV, pDs->nRank, sIdx, sizeof(sIdx));
			_printVar(pFile, pV, "CENTER", sIdx, 4);
			int iPlus  = _varIndex(pFile, pV->sDeltaPlus);
			int iMinus = _varIndex(pFile, pV->sDeltaMinus);
			if(iPlus >= 0)  _printVar(pFile, pFile->aVars + iPlus,  "MAX_ERR", sIdx, 4);
			if(iMinus >= 0) _printVar(pFile, pFile->aVars + iMinus, "MIN_ERR", sIdx, 4);
		}
	}

	bool bAny = false;
	for(int i = 0; i < pFile->nVars; ++i){
		cdf_var_t* pV = pFile->aVars + i;
		if(!pV->bTimeDep || pV->bSelected) continue;
		if(!bAny){ _section("Available, name to stream"); bAny = true; }
		printf("%s  %s  on=%s", pV->sName, pV->sVarType[0] ? pV->sVarType : "data", pV->asDepend[0]);
		if(pV->sUnits[0]) printf("  units=%s", pV->sUnits);
		printf("\n");
	}

	bAny = false;
	for(int i = 0; i < pFile->nVars; ++i){
		cdf_var_t* pV = pFile->aVars + i;
		if(pV->role != ROLE_IGNORE) continue;
		if(!bAny){ _section("Ignored"); bAny = true; }
		printf("%s  %s\n", pV->sName, pV->sWhy);
	}

	if(pFile->sAdvice[0] != '\0'){
		_section("Notes");
		fputs(pFile->sAdvice, stdout);
	}
}

/* Open a CDF, inventory and classify it.  Returns DAS_OKAY with the file
   open, or an error with it closed. */
static int _openAndClassify(cdf_file_t* pFile, const char* sPath, const popts_t* pOpts)
{
	CDFstatus nCdfStatus = CDF_OK;
	memset(pFile, 0, sizeof(cdf_file_t));
	strncpy(pFile->sPath, sPath, sizeof(pFile->sPath) - 1);

	if(CDF_MAD( CDFopenCDF((char*)sPath, &(pFile->id)) ))
		return PERR;
	CDFsetReadOnlyMode(pFile->id, READONLYon);

	int nRet = _inventory(pFile);
	if(nRet == DAS_OKAY) nRet = _classify(pFile, pOpts);
	if(nRet != DAS_OKAY){
		CDFcloseCDF(pFile->id);
		pFile->id = NULL;
	}
	return nRet;
}


/* ************************************************************************* */
/* Building datasets from the inventory */

static das_val_type _cdfVt(long nType)
{
	switch(nType){
	case CDF_INT1:  return vtByte;    case CDF_UINT1: return vtUByte;
	case CDF_INT2:  return vtShort;   case CDF_UINT2: return vtUShort;
	case CDF_INT4:  return vtInt;     case CDF_UINT4: return vtUInt;
	case CDF_INT8:  case CDF_TIME_TT2000: return vtLong;
	case CDF_REAL4: case CDF_FLOAT:   return vtFloat;
	case CDF_REAL8: case CDF_DOUBLE:  case CDF_EPOCH: return vtDouble;
	default: return vtUnknown;
	}
}

static const char* _semantic(const cdf_var_t* pV)
{
	if(_isTimeType(pV->nType)) return "datetime";
	das_val_type vt = _cdfVt(pV->nType);
	return das_vt_isreal(vt) ? "real" : "int";
}

/* The fill for a variable's array: FILLVAL when present and of the same
   type, else the das default for the type.  Time bases are always stored
   as TT2000 longs. */
static const ubyte* _varFill(cdf_file_t* pFile, const cdf_var_t* pV, ubyte* pBuf)
{
	if(_isTimeType(pV->nType)) return g_tt2kfill;

	long iAttr = CDFgetAttrNum(pFile->id, (char*)"FILLVAL");
	if((iAttr >= 0)&&(CDFconfirmzEntryExistence(pFile->id, iAttr, pV->nVarNum) == CDF_OK)){
		long nType = 0, nElems = 0;
		if((CDFgetAttrzEntryDataType(pFile->id, iAttr, pV->nVarNum, &nType) == CDF_OK)&&
		   (CDFgetAttrzEntryNumElements(pFile->id, iAttr, pV->nVarNum, &nElems) == CDF_OK)&&
		   (_cdfVt(nType) == _cdfVt(pV->nType))&&(nElems == 1)){
			if(CDFgetAttrzEntry(pFile->id, iAttr, pV->nVarNum, pBuf) == CDF_OK)
				return pBuf;
		}
	}
	return (const ubyte*)das_vt_fill(_cdfVt(pV->nType));
}

/* Units for a variable.  Time bases are TT2000; empty and placeholder
   strings are dimensionless; anything the parser rejects is logged and
   made dimensionless rather than stopping the stream. */
static das_units _varUnits(const cdf_var_t* pV)
{
	if(_isTimeType(pV->nType)) return UNIT_TT2000;
	const char* s = pV->sUnits;
	if((s[0] == '\0')||(strcmp(s, "null") == 0)||(strcmp(s, "<<TBD>>") == 0)||(strcmp(s, "e") == 0))
		return UNIT_DIMENSIONLESS;
	das_units units = Units_fromStr(s);
	if(units == NULL){
		daslog_warn_v("Units '%s' on %s could not be parsed, sent as dimensionless", s, pV->sName);
		return UNIT_DIMENSIONLESS;
	}
	return units;
}

/* Attribute names the classifier consumed, which are not repeated as
   properties.  Anything DEPEND_/LABL_PTR_ prefixed is also skipped. */
static bool _attrConsumed(const cdf_var_t* pV, const char* sAttr)
{
	static const char* asSkip[] = {
		"VAR_TYPE", "FILLVAL", "UNITS", "OFFSET_OF", "DELTA_PLUS_VAR", "DELTA_MINUS_VAR",
		"DICT_KEY", NULL
	};
	for(int i = 0; asSkip[i] != NULL; ++i)
		if(strcmp(sAttr, asSkip[i]) == 0) return true;
	if((strncmp(sAttr, "DEPEND_", 7) == 0)||(strncmp(sAttr, "LABL_PTR_", 9) == 0)) return true;
	if((pV->sFrameAttr[0] != '\0')&&(strcmp(sAttr, pV->sFrameAttr) == 0)) return true;
	return false;
}

/* das3 property name for a CDF variable attribute: the user's map, then
   the built-in table, then the name as-is */
static const char* _propName(const char* sAttr)
{
	for(int i = 0; i < g_nPropMap; ++i)
		if(strcmp(g_aPropMap[i].sFrom, sAttr) == 0) return g_aPropMap[i].sTo;
	for(int i = 0; g_aBuiltinMap[i].sFrom[0] != '\0'; ++i)
		if(strcmp(g_aBuiltinMap[i].sFrom, sAttr) == 0) return g_aBuiltinMap[i].sTo;
	return sAttr;
}

/* Copy a variable's attributes onto a descriptor as properties */
static int _addVarProps(cdf_file_t* pFile, const cdf_var_t* pV, DasDesc* pDest)
{
	long nAttrs = 0;
	if(CDFgetNumAttributes(pFile->id, &nAttrs) != CDF_OK) return DAS_OKAY;
	char sName[CDF_ATTR_NAME_LEN256 + 1];
	char sVal[4096];
	for(long i = 0; i < nAttrs; ++i){
		long nScope = 0;
		if(CDFgetAttrScope(pFile->id, i, &nScope) != CDF_OK) continue;
		if(nScope != VARIABLE_SCOPE) continue;
		if(CDFconfirmzEntryExistence(pFile->id, i, pV->nVarNum) != CDF_OK) continue;
		if(CDFgetAttrName(pFile->id, i, sName) != CDF_OK) continue;
		if(_attrConsumed(pV, sName)) continue;

		long nType = 0;
		if(CDFgetAttrzEntryDataType(pFile->id, i, pV->nVarNum, &nType) != CDF_OK) continue;
		if(!_varAttrStr(pFile->id, pV->nVarNum, sName, sVal, sizeof(sVal))) continue;
		const char* sProp = _propName(sName);
		/* the frame is a form parameter, never a property */
		if(strcmp(sProp, "frame") == 0) continue;

		if((nType == CDF_CHAR)||(nType == CDF_UCHAR))
			DasDesc_setStr(pDest, sProp, sVal);
		else if(_isTimeType(nType))
			DasDesc_setStr(pDest, sProp, sVal);
		else if(das_vt_isreal(_cdfVt(nType)))
			DasDesc_setDouble(pDest, sProp, atof(sVal));
		else
			DasDesc_setInt(pDest, sProp, atoi(sVal));
	}
	return DAS_OKAY;
}

/* A dataset array for a record varying variable, registered for filling */
static DasAry* _newRecAry(cdf_file_t* pFile, cdf_ds_t* pDs, int iVar, const char* sId)
{
	cdf_var_t* pV = pFile->aVars + iVar;
	size_t aShape[VARIDX_MAX] = {0};
	int nRank = 1 + (int)pV->nDims;
	if(nRank > VARIDX_MAX){
		das_error(PERR, "%s has %ld dimensions, the limit is %d", pV->sName, pV->nDims, VARIDX_MAX - 1);
		return NULL;
	}
	long nElemsPerRec = 1;
	for(int d = 0; d < pV->nDims; ++d){ aShape[d+1] = (size_t)pV->aDimSz[d]; nElemsPerRec *= pV->aDimSz[d]; }

	/* time bases are stored as TT2000 whatever the CDF type; EPOCH values
	   are converted as they are read */
	das_val_type vt = _isTimeType(pV->nType) ? vtLong : _cdfVt(pV->nType);
	ubyte aFill[16];
	DasAry* pAry = new_DasAry(sId, vt, 0, _varFill(pFile, pV, aFill), nRank, aShape, _varUnits(pV));
	if(pAry == NULL) return NULL;
	if(DasDs_addAry(pDs->pDs, pAry) != DAS_OKAY){ dec_DasAry(pAry); return NULL; }
	dec_DasAry(pAry);   /* the dataset holds the surviving reference */

	rec_reader_t* pR = pDs->aRead + pDs->nRead;
	pR->iVar = iVar;
	pR->pAry = pAry;
	pR->nElemsPerRec = nElemsPerRec;
	pR->bTime = _isTimeType(pV->nType);
	pR->nDims = pV->nDims;
	for(int d = 0; d < pV->nDims; ++d){
		pR->aStart[d] = ((d == pV->iCompDim)&&(pV->iCompSel >= 0)) ? pV->iCompSel : 0;
		pR->aCount[d] = pV->aDimSz[d];   /* already 1 on a selected component axis */
	}
	++(pDs->nRead);

	DasDs_addFixedCodec(
		pDs->pDs, sId, _semantic(pV), das_vt_serial_type(vt),
		(int)das_vt_size(vt), (int)nElemsPerRec, DASENC_WRITE
	);
	return pAry;
}

/* A rank-1 array holding a non record varying table, read now, sent in the
   header as <values> */
static DasAry* _newNrvAry(cdf_file_t* pFile, cdf_ds_t* pDs, int iVar, const char* sId)
{
	cdf_var_t* pV = pFile->aVars + iVar;
	if(pV->nDims != 1){
		das_error(PERR, "%s is a rank %ld table; only rank 1 tables are supported", pV->sName, pV->nDims);
		return NULL;
	}
	size_t aShape[1] = { 0 };   /* grows to the table length on append */
	ubyte aFill[16];
	DasAry* pAry = new_DasAry(sId, _cdfVt(pV->nType), 0, _varFill(pFile, pV, aFill), 1, aShape, _varUnits(pV));
	if(pAry == NULL) return NULL;

	size_t uSz = das_vt_size(_cdfVt(pV->nType));
	ubyte* pBuf = (ubyte*)malloc(uSz * pV->aDimSz[0]);
	if((pBuf == NULL)||(CDFgetzVarRecordData(pFile->id, pV->nVarNum, 0, pBuf) != CDF_OK)){
		free(pBuf); dec_DasAry(pAry);
		das_error(PERR, "Could not read table %s", pV->sName);
		return NULL;
	}
	DasAry_append(pAry, pBuf, (size_t)pV->aDimSz[0]);
	free(pBuf);
	if(DasDs_addAry(pDs->pDs, pAry) != DAS_OKAY){ dec_DasAry(pAry); return NULL; }
	dec_DasAry(pAry);
	return pAry;
}

/* The form a classified variable carries */
static DasForm* _newForm(const cdf_var_t* pV, bool bTime)
{
	if(bTime) return new_DasFormPoint();
	if(strcmp(pV->sKind, "vector") == 0) return new_DasFormVector(pV->sFrame, pV->uSys, pV->aDirs);
	if(strcmp(pV->sKind, "complex") == 0)
		return new_DasFormCplx(strcmp(pV->sSystem, "polar") == 0 ? DAS_VSYS_POLAR : DAS_VSYS_RECT);
	return new_DasFormLinear();
}

/* Wrap an array as a variable and give it to a dimension under a role.
   pIdxMap has nRank entries.  A composite takes its internal shape from the
   variable's component axes. */
static int _addAryVar(
	cdf_file_t* pFile, cdf_ds_t* pDs, DasDim* pDim, const char* sRole,
	DasAry* pAry, int iVar, const int8_t* pIdxMap, bool bTime
){
	cdf_var_t* pV = pFile->aVars + iVar;
	int nRank = DasDs_rank(pDs->pDs);
	DasGen* pGen = new_DasGenAry(pAry, nRank, pIdxMap);
	DasForm* pForm = _newForm(pV, bTime);
	if((pGen == NULL)||(pForm == NULL)){
		DasGen_decRef(pGen); del_DasForm(pForm);
		return PERR;
	}

	DasVar* pVar = NULL;
	ptrdiff_t aIntShape[VARIDX_MAX];
	int nIntRank = 0;
	for(int d = 0; d < pV->nDims; ++d)
		if(pV->aIsInternal[d]) aIntShape[nIntRank++] = pV->aDimSz[d];
	if(nIntRank > 0)
		pVar = (DasVar*)new_DasVarComp(pGen, _varUnits(pV), pForm, nIntRank, aIntShape);
	else
		pVar = new_DasVar(pGen, _varUnits(pV), pForm);
	DasGen_decRef(pGen);   /* both constructors added their own references */
	del_DasForm(pForm);
	if(pVar == NULL) return PERR;

	if(pV->nLabels > 0){
		char sLabels[MAX_COMPS * 33] = {'\0'};
		for(int c = 0; c < pV->nLabels; ++c){
			if(c > 0) strcat(sLabels, ";");
			strcat(sLabels, pV->aLabels[c]);
		}
		DasDesc_flexSet((DasDesc*)pVar, "stringArray", 0, "label", sLabels, ';', NULL, 3);
	}

	if(!DasDim_addVar(pDim, sRole, pVar)){   /* addVar takes the reference */
		dec_DasVar(pVar);
		return PERR;
	}
	return DAS_OKAY;
}

/* A sequence variable for an arithmetic offset or coordinate table that
   runs along external index iExt */
static int _addSeqVar(
	cdf_file_t* pFile, cdf_ds_t* pDs, DasDim* pDim, const char* sRole, int iVar,
	int iExt, double rMin, double rStep
){
	cdf_var_t* pV = pFile->aVars + iVar;
	int nRank = DasDs_rank(pDs->pDs);
	das_val_type vt = _cdfVt(pV->nType);
	bool bInt = das_vt_isint(vt);
	das_elem_type et = bInt ? etLong : etDouble;

	int64_t nMin = (int64_t)rMin;
	int64_t aNStep[VARIDX_MAX] = {0};
	double  aRStep[VARIDX_MAX] = {0.0};
	ptrdiff_t aExt[VARIDX_MAX];
	for(int i = 0; i < nRank; ++i) aExt[i] = VARIDX_UNUSED;
	aExt[iExt] = pV->aDimSz[0];
	aNStep[iExt] = (int64_t)rStep;
	aRStep[iExt] = rStep;

	DasGen* pGen = new_DasGenSeq(
		et, bInt ? (const ubyte*)&nMin : (const ubyte*)&rMin, nRank,
		bInt ? (const ubyte*)aNStep : (const ubyte*)aRStep, aExt
	);
	DasForm* pForm = new_DasFormLinear();
	if((pGen == NULL)||(pForm == NULL)){ DasGen_decRef(pGen); del_DasForm(pForm); return PERR; }
	DasVar* pVar = new_DasVar(pGen, _varUnits(pV), pForm);
	DasGen_decRef(pGen);
	del_DasForm(pForm);
	if(pVar == NULL) return PERR;
	if(!DasDim_addVar(pDim, sRole, pVar)){ dec_DasVar(pVar); return PERR; }
	return DAS_OKAY;
}

/* Build the DasDs for one classified dataset and register it with the
   stream under its packet id */
static int _buildDataset(cdf_file_t* pFile, int iDs, DasStream* pSd)
{
	cdf_ds_t* pDs = pFile->aDs + iDs;
	cdf_var_t* pFirst = pFile->aVars + pDs->aMembers[0];
	int nRank = pDs->nRank;
	int8_t aMap[VARIDX_MAX];
	int nRet = DAS_OKAY;

	pDs->pDs = new_DasDs(pDs->sName, pDs->sGroup, nRank);
	if(pDs->pDs == NULL) return PERR;
	pDs->nPktId = iDs + 1;
	pDs->nRead = 0;

	/* which external index each DEPEND_d occupies, and the offset if any */
	int aExtOfDep[VARIDX_MAX] = {0};
	int iOff = -1;
	{
		int iExt = 1;
		for(int d = 1; d <= pFirst->nDims; ++d){
			if(pFirst->asDepend[d][0] == '\0') continue;
			aExtOfDep[d] = iExt;
			int iDep = _varIndex(pFile, pFirst->asDepend[d]);
			if((iDep >= 0)&&(pFile->aVars[iDep].role == ROLE_OFFSET)) iOff = d;
			++iExt;
		}
	}

	/* time */
	DasDim* pDim = new_DasDim("time", "time", DASDIM_COORD, nRank);
	if(pDim == NULL) return PERR;
	DasDim_setAxis(pDim, 0, "x");
	DasDim_primeCoord(pDim, true);
	cdf_var_t* pT = pFile->aVars + pDs->iTime;
	DasAry* pAry = _newRecAry(pFile, pDs, pDs->iTime, pT->sName);
	if(pAry == NULL) return PERR;
	for(int i = 0; i < VARIDX_MAX; ++i) aMap[i] = VARIDX_UNUSED;
	aMap[0] = 0;
	nRet = _addAryVar(pFile, pDs, pDim, (iOff > 0) ? DASVAR_REF : DASVAR_CENTER, pAry, pDs->iTime, aMap, true);
	if(nRet != DAS_OKAY) return nRet;
	if(iOff > 0){
		int iDep = _varIndex(pFile, pFirst->asDepend[iOff]);
		cdf_var_t* pO = pFile->aVars + iDep;
		double rMin = 0.0, rStep = 0.0;
		if(_isSequence(pFile, pO, &rMin, &rStep)){
			nRet = _addSeqVar(pFile, pDs, pDim, DASVAR_OFFSET, iDep, aExtOfDep[iOff], rMin, rStep);
		}
		else{
			pAry = _newNrvAry(pFile, pDs, iDep, pO->sName);
			if(pAry == NULL) return PERR;
			for(int i = 0; i < VARIDX_MAX; ++i) aMap[i] = VARIDX_UNUSED;
			aMap[aExtOfDep[iOff]] = 0;
			nRet = _addAryVar(pFile, pDs, pDim, DASVAR_OFFSET, pAry, iDep, aMap, false);
		}
		if(nRet != DAS_OKAY) return nRet;
	}
	_addVarProps(pFile, pT, (DasDesc*)pDim);
	if((nRet = DasDs_addDim(pDs->pDs, pDim)) != DAS_OKAY) return nRet;

	/* the other coordinates */
	const char* asAxes[] = {"x", "y", "z", "w"};
	int iAxis = 1;
	for(int d = 1; d <= pFirst->nDims; ++d){
		if((pFirst->asDepend[d][0] == '\0')||(d == iOff)) continue;
		int iDep = _varIndex(pFile, pFirst->asDepend[d]);
		if(iDep < 0) continue;   /* noted by the classifier */
		cdf_var_t* pC = pFile->aVars + iDep;

		pDim = new_DasDim(pC->sName, pC->sName, DASDIM_COORD, nRank);
		if(pDim == NULL) return PERR;
		if(iAxis < 4) DasDim_setAxis(pDim, 0, asAxes[iAxis]);
		DasDim_primeCoord(pDim, true);
		++iAxis;

		double rMin = 0.0, rStep = 0.0;
		if(!pC->bRecVary && _isSequence(pFile, pC, &rMin, &rStep)){
			nRet = _addSeqVar(pFile, pDs, pDim, DASVAR_CENTER, iDep, aExtOfDep[d], rMin, rStep);
		}
		else if(!pC->bRecVary){
			pAry = _newNrvAry(pFile, pDs, iDep, pC->sName);
			if(pAry == NULL) return PERR;
			for(int i = 0; i < VARIDX_MAX; ++i) aMap[i] = VARIDX_UNUSED;
			aMap[aExtOfDep[d]] = 0;
			nRet = _addAryVar(pFile, pDs, pDim, DASVAR_CENTER, pAry, iDep, aMap, false);
		}
		else{
			/* a record varying table: [record, N] */
			pAry = _newRecAry(pFile, pDs, iDep, pC->sName);
			if(pAry == NULL) return PERR;
			for(int i = 0; i < VARIDX_MAX; ++i) aMap[i] = VARIDX_UNUSED;
			aMap[0] = 0;
			aMap[aExtOfDep[d]] = 1;
			nRet = _addAryVar(pFile, pDs, pDim, DASVAR_CENTER, pAry, iDep, aMap, false);
		}
		if(nRet != DAS_OKAY) return nRet;
		_addVarProps(pFile, pC, (DasDesc*)pDim);
		if((nRet = DasDs_addDim(pDs->pDs, pDim)) != DAS_OKAY) return nRet;
	}

	/* data: one dimension per member, uncertainties in the same dimension */
	size_t uPre = strlen(pDs->sName);
	for(int m = 0; m < pDs->nMembers; ++m){
		int iVar = pDs->aMembers[m];
		cdf_var_t* pV = pFile->aVars + iVar;
		const char* sDim = pV->sName;
		if((uPre > 0)&&(strncmp(sDim, pDs->sName, uPre) == 0)&&(sDim[uPre] == '_')&&(sDim[uPre+1] != '\0'))
			sDim += uPre + 1;
		daslog_debug_v("dataset %s: variable %s becomes dimension %s", pDs->sName, pV->sName, sDim);
		char sPhys[64];
		if(pV->sDictKey[0] != '\0'){
			const char* sGt = strchr(pV->sDictKey, '>');
			size_t u = (sGt != NULL) ? (size_t)(sGt - pV->sDictKey) : strlen(pV->sDictKey);
			if(u > sizeof(sPhys) - 1) u = sizeof(sPhys) - 1;
			memcpy(sPhys, pV->sDictKey, u); sPhys[u] = '\0';
		}
		else
			strncpy(sPhys, sDim, sizeof(sPhys) - 1);

		if(pV->bAnnot){
			pDim = new_DasDim(sPhys, sDim, DASDIM_COORD, nRank);
			if(pDim == NULL) return PERR;
			DasDim_setAxis(pDim, 0, "x");
			DasDim_primeCoord(pDim, false);   /* annotates the time axis */
		}
		else{
			pDim = new_DasDim(sPhys, sDim, DASDIM_DATA, nRank);
			if(pDim == NULL) return PERR;
		}

		/* external CDF dims map to their external index, internal ones are
		   the item run; a folded member has no dims for the trailing indices */
		for(int i = 0; i < VARIDX_MAX; ++i) aMap[i] = VARIDX_UNUSED;
		aMap[0] = 0;
		for(int d = 1; d <= pV->nDims; ++d)
			if(!pV->aIsInternal[d-1]) aMap[aExtOfDep[d]] = (int8_t)d;

		pAry = _newRecAry(pFile, pDs, iVar, pV->sName);
		if(pAry == NULL) return PERR;
		nRet = _addAryVar(pFile, pDs, pDim, DASVAR_CENTER, pAry, iVar, aMap, false);
		if(nRet != DAS_OKAY) return nRet;

		const char* asDelta[2] = { pV->sDeltaPlus, pV->sDeltaMinus };
		const char* asRole[2]  = { DASVAR_MAX_ERR, DASVAR_MIN_ERR };
		for(int k = 0; k < 2; ++k){
			int iD = _varIndex(pFile, asDelta[k]);
			if(iD < 0) continue;
			cdf_var_t* pD = pFile->aVars + iD;
			if(!pD->bRecVary || (pD->nDims != pV->nDims)) continue;
			memcpy(pD->aIsInternal, pV->aIsInternal, sizeof(pD->aIsInternal));
			pAry = _newRecAry(pFile, pDs, iD, pD->sName);
			if(pAry == NULL) return PERR;
			nRet = _addAryVar(pFile, pDs, pDim, asRole[k], pAry, iD, aMap, false);
			if(nRet != DAS_OKAY) return nRet;
		}

		_addVarProps(pFile, pV, (DasDesc*)pDim);
		/* the common das property for an ISTP support variable sent as data */
		if(!pV->bAnnot && (strcmp(pV->sVarType, "support_data") == 0))
			DasDesc_setStr((DasDesc*)pDim, "varType", "support");
		if((nRet = DasDs_addDim(pDs->pDs, pDim)) != DAS_OKAY) return nRet;
	}

	return DasStream_addDesc(pSd, (DasDesc*)pDs->pDs, pDs->nPktId);
}

/* Global attributes become stream properties: the inverse of das3_cdf's
   global name table, then names as-is.  Multi-entry attributes join with
   newlines. */
static int _addGlobalProps(cdf_file_t* pFile, DasStream* pSd)
{
	long nAttrs = 0;
	if(CDFgetNumAttributes(pFile->id, &nAttrs) != CDF_OK) return DAS_OKAY;
	char sName[CDF_ATTR_NAME_LEN256 + 1];
	for(long i = 0; i < nAttrs; ++i){
		long nScope = 0;
		if(CDFgetAttrScope(pFile->id, i, &nScope) != CDF_OK) continue;
		if(nScope != GLOBAL_SCOPE) continue;
		if(CDFgetAttrName(pFile->id, i, sName) != CDF_OK) continue;

		long nMax = -1;
		if(CDFgetAttrMaxgEntry(pFile->id, i, &nMax) != CDF_OK) continue;

		/* Each entry is read on its own.  For a multi-entry attribute the
		   entry's internal whitespace collapses to single spaces, so a typed
		   in paragraph is one item that re-flows downstream and the entry
		   count survives a trip back through das3_cdf; a lone entry is kept
		   as written. */
		char sVal[8192] = {'\0'};
		char sEnt[4096];
		size_t uHave = 0;
		int nEntries = 0;
		for(long e = 0; e <= nMax; ++e){
			if(CDFconfirmgEntryExistence(pFile->id, i, e) != CDF_OK) continue;
			long nType = 0, nElems = 0;
			if(CDFgetAttrgEntryDataType(pFile->id, i, e, &nType) != CDF_OK) continue;
			if((nType != CDF_CHAR)&&(nType != CDF_UCHAR)) continue;
			if(CDFgetAttrgEntryNumElements(pFile->id, i, e, &nElems) != CDF_OK) continue;
			if(nElems >= (long)sizeof(sEnt)) nElems = sizeof(sEnt) - 1;
			memset(sEnt, 0, sizeof(sEnt));
			if(CDFgetAttrgEntry(pFile->id, i, e, sEnt) != CDF_OK) continue;
			sEnt[nElems] = '\0';
			++nEntries;

			if(nMax > 0){
				/* collapse whitespace runs to one space, trim both ends */
				char* pW = sEnt;
				bool bSpace = true;   /* trims leading */
				for(const char* pR = sEnt; *pR != '\0'; ++pR){
					if((*pR == ' ')||(*pR == '\t')||(*pR == '\n')||(*pR == '\r')){
						if(!bSpace) *pW++ = ' ';
						bSpace = true;
					}
					else{ *pW++ = *pR; bSpace = false; }
				}
				while((pW > sEnt)&&(pW[-1] == ' ')) --pW;
				*pW = '\0';
				if(sEnt[0] == '\0') continue;   /* an all-blank entry has nothing to say */
			}
			size_t uLen = strlen(sEnt);
			if(uHave + uLen + 2 >= sizeof(sVal)) break;
			if(uHave > 0) sVal[uHave++] = '\n';
			memcpy(sVal + uHave, sEnt, uLen + 1);
			uHave += uLen;
		}
		if(uHave == 0) continue;

		const char* sProp = sName;
		if(strcmp(sName, "TEXT") == 0) sProp = "summary";
		else if(strcmp(sName, "TITLE") == 0) sProp = "title";
		else if(strncmp(sName, "G_", 2) == 0) sProp = sName + 2;

		if(nMax < 1)
			DasDesc_setStr((DasDesc*)pSd, sProp, sVal);
		else
			DasDesc_flexSet((DasDesc*)pSd, "stringArray", 0, sProp, sVal, '\n', NULL, 3);
	}
	return DAS_OKAY;
}

/* ************************************************************************* */
/* Streaming records */

#define MAX_BLOCK_BYTES 16777216   /* per variable, per read */

/* Structure signature: what a later file must match to reuse the datasets */
static void _structSig(const cdf_file_t* pFile, char* sBuf, size_t uLen)
{
	sBuf[0] = '\0';
	for(int i = 0; i < pFile->nVars; ++i){
		const cdf_var_t* pV = pFile->aVars + i;
		if(!pV->bSelected && (pV->role != ROLE_TIME) && (pV->role != ROLE_COORD)&&(pV->role != ROLE_OFFSET))
			continue;
		char sOne[CVAR_NAME_SZ + 64];
		snprintf(sOne, sizeof(sOne), "%s:%ld:%ld", pV->sName, pV->nType, pV->nDims);
		for(int d = 0; d < pV->nDims; ++d){
			char sD[24]; snprintf(sD, sizeof(sD), ":%ld", pV->aDimSz[d]);
			strncat(sOne, sD, sizeof(sOne) - strlen(sOne) - 1);
		}
		strncat(sOne, ";", sizeof(sOne) - strlen(sOne) - 1);
		if(strlen(sBuf) + strlen(sOne) >= uLen) return;
		strcat(sBuf, sOne);
	}
}

/* Read a block of records of one variable into a malloc'ed buffer, through
   the reader's window (whole dims, or one component of the component axis) */
static ubyte* _readBlock(
	cdf_file_t* pFile, long nVarNum, const rec_reader_t* pR, long nRec0, long nRecs, size_t uElemSz
){
	ubyte* pBuf = (ubyte*)malloc(uElemSz * pR->nElemsPerRec * nRecs);
	if(pBuf == NULL) return NULL;
	long aItv[CDF_MAX_DIMS];
	for(int d = 0; d < pR->nDims; ++d) aItv[d] = 1;
	CDFstatus nCdfStatus = CDF_OK;
	if(CDF_MAD( CDFhyperGetzVarData(pFile->id, nVarNum, nRec0, nRecs, 1L,
		(long*)pR->aStart, (long*)pR->aCount, aItv, pBuf) )){
		free(pBuf);
		return NULL;
	}
	return pBuf;
}

/* Send one dataset's records from the open file, in blocks, keeping only
   records whose time base falls in the range */
static int _streamDataset(
	cdf_file_t* pFile, cdf_ds_t* pDs, DasIO* pIo, const das_range* pRng, long* pnPkts
){
	cdf_var_t* pT = pFile->aVars + pDs->iTime;
	long nRecs = pT->nRecs;
	if(nRecs < 1) return DAS_OKAY;

	/* the range as TT2000 */
	int64_t nBeg = INT64_MIN, nEnd = INT64_MAX;
	if((pRng != NULL)&&(pRng->dBeg.vt == vtTime)){
		das_time dtBeg, dtEnd;
		das_datum_toTime(&(pRng->dBeg), &dtBeg);
		das_datum_toTime(&(pRng->dEnd), &dtEnd);
		nBeg = dt_to_tt2k(&dtBeg);
		nEnd = dt_to_tt2k(&dtEnd);
	}

	/* block size from the widest record */
	long nMaxBytesPerRec = 8;
	for(int r = 0; r < pDs->nRead; ++r){
		cdf_var_t* pV = pFile->aVars + pDs->aRead[r].iVar;
		long n = (long)das_vt_size(_cdfVt(pV->nType)) * pDs->aRead[r].nElemsPerRec;
		if(n > nMaxBytesPerRec) nMaxBytesPerRec = n;
	}
	long nBlock = MAX_BLOCK_BYTES / nMaxBytesPerRec;
	if(nBlock < 1) nBlock = 1;

	bool* pKeep = (bool*)malloc(nBlock);
	int64_t* pTt = (int64_t*)malloc(nBlock * sizeof(int64_t));
	if((pKeep == NULL)||(pTt == NULL)){ free(pKeep); free(pTt); return PERR; }

	int nRet = DAS_OKAY;
	for(long nRec0 = 0; (nRec0 < nRecs)&&(nRet == DAS_OKAY); nRec0 += nBlock){
		long nHere = ((nRecs - nRec0) < nBlock) ? (nRecs - nRec0) : nBlock;

		/* the time base first, to decide what is kept */
		size_t uTSz = (pT->nType == CDF_EPOCH) ? sizeof(double) : sizeof(int64_t);
		rec_reader_t rdT = { .iVar = pDs->iTime, .pAry = NULL, .nElemsPerRec = 1, .bTime = true, .nDims = 0 };
		ubyte* pTBuf = _readBlock(pFile, pT->nVarNum, &rdT, nRec0, nHere, uTSz);
		if(pTBuf == NULL){ nRet = PERR; break; }
		long nKept = 0;
		for(long i = 0; i < nHere; ++i){
			int64_t nVal;
			if(pT->nType == CDF_EPOCH)
				nVal = CDF_TT2000_from_UTC_EPOCH(((const double*)pTBuf)[i]);
			else
				nVal = ((const int64_t*)pTBuf)[i];
			pTt[i] = nVal;
			pKeep[i] = (nVal >= nBeg)&&(nVal < nEnd);
			if(pKeep[i]) ++nKept;
		}
		free(pTBuf);
		if(nKept == 0) continue;

		for(int r = 0; (r < pDs->nRead)&&(nRet == DAS_OKAY); ++r){
			rec_reader_t* pR = pDs->aRead + r;
			cdf_var_t* pV = pFile->aVars + pR->iVar;
			if(pR->bTime){
				for(long i = 0; i < nHere; ++i)
					if(pKeep[i]) DasAry_append(pR->pAry, (const ubyte*)(pTt + i), 1);
				continue;
			}
			size_t uSz = das_vt_size(_cdfVt(pV->nType));
			ubyte* pBuf = _readBlock(pFile, pV->nVarNum, pR, nRec0, nHere, uSz);
			if(pBuf == NULL){ nRet = PERR; break; }
			size_t uRecBytes = uSz * pR->nElemsPerRec;
			for(long i = 0; i < nHere; ++i)
				if(pKeep[i]) DasAry_append(pR->pAry, pBuf + i * uRecBytes, (size_t)pR->nElemsPerRec);
			free(pBuf);
		}
		if(nRet != DAS_OKAY) break;

		nRet = DasIO_writeData(pIo, (DasDesc*)pDs->pDs, pDs->nPktId);
		if(nRet == DAS_OKAY) *pnPkts += nKept;
		DasDs_clearRagged0(pDs->pDs);
	}
	free(pKeep);
	free(pTt);
	return nRet;
}

/* ************************************************************************* */
/* It all starts here baby! */

int main(int argc, char** argv)
{
	popts_t opts;
	if(parseArgs(argc, argv, &opts) != DAS_OKAY)
		return PERR;

	/* The error buffer feeds _bounce_to_log; without one das_get_error has
	   nothing to copy */
	das_init(argv[0], DASERR_DIS_RET, 1024, daslog_strlevel(opts.aLevel), logHandler);

	DasErrCode nDasStatus = DAS_OKAY;   /* for DAS_EXIT */

	DasUriTplt* pTplt = new_DasUriTplt();
	DasUriTplt_register(pTplt, das_time_uridef());
	DAS_EXIT( DasUriTplt_pattern(pTplt, opts.sPattern) );

	DasUriIter iter;
	DAS_EXIT( init_DasUriIter(&iter, pTplt, opts.nRanges, opts.aRanges) );

	DAS_EXIT( _parsePropMap(opts.aPropMap) );

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
		char sFirst[DURI_MAX_PATH] = {'\0'};
		int nFiles = 0;
		while((sFile = DasUriIter_next(&iter)) != NULL){
			printf("   %s\n", sFile);
			if(nFiles == 0) strncpy(sFirst, sFile, sizeof(sFirst) - 1);
			++nFiles;
		}
		if(nFiles == 0)
			printf("   (none)\n");
		fini_DasUriIter(&iter);
		del_DasUriTplt(pTplt);
		if(nFiles == 0) return 0;

		/* Inspect the first file only, the rest are assumed to match */
		cdf_file_t* pFile = (cdf_file_t*)calloc(1, sizeof(cdf_file_t));
		int nRet = _openAndClassify(pFile, sFirst, &opts);
		if(nRet == DAS_OKAY){
			_listFile(pFile, &opts);
			CDFcloseCDF(pFile->id);
		}
		free(pFile);
		return (nRet == DAS_OKAY) ? 0 : PERR;
	}

	/* Streaming.  The first readable file sets the structure; later files
	   must match it or are skipped with an error. */
	g_pIoOut = new_DasIO_cfile(PROG, stdout, "w3");
	g_pSd = new_DasStream();
	strncpy(g_pSd->version, DAS_30_STREAM_VER, STREAMDESC_VER_SZ - 1);
	strncpy(g_pSd->type, "das-basic-stream", STREAMDESC_TYPE_SZ - 1);
	g_exType = DAS_EX_SERVER_ERR;

	const das_range* pTimeRng = NULL;
	for(int i = 0; i < opts.nRanges; ++i)
		if((strcmp(opts.aRanges[i].sCoord, "time") == 0)&&(opts.aRanges[i].dBeg.vt == vtTime))
			pTimeRng = opts.aRanges + i;

	cdf_file_t* pFirst = (cdf_file_t*)calloc(1, sizeof(cdf_file_t));
	cdf_file_t* pFile = (cdf_file_t*)calloc(1, sizeof(cdf_file_t));
	char sSigFirst[MAX_CDF_VARS * 64] = {'\0'};
	char sSig[MAX_CDF_VARS * 64] = {'\0'};
	long nPkts = 0;
	int nFiles = 0;
	const char* sPath = NULL;
	while((sPath = DasUriIter_next(&iter)) != NULL){
		cdf_file_t* pCur = (nFiles == 0) ? pFirst : pFile;
		if(_openAndClassify(pCur, sPath, &opts) != DAS_OKAY){
			daslog_error_v("Skipping %s", sPath);
			continue;
		}

		if(nFiles == 0){
			/* Build every dataset, then send the headers once */
			if(_addGlobalProps(pFirst, g_pSd) != DAS_OKAY) goto STREAM_ERR;
			for(int j = 0; j < pFirst->nDs; ++j)
				if(_buildDataset(pFirst, j, g_pSd) != DAS_OKAY) goto STREAM_ERR;
			DAS_EXIT( DasIO_writeDesc(g_pIoOut, (DasDesc*)g_pSd, 0) );
			for(int j = 0; j < pFirst->nDs; ++j)
				DAS_EXIT( DasIO_writeDesc(g_pIoOut, (DasDesc*)pFirst->aDs[j].pDs, pFirst->aDs[j].nPktId) );
			_structSig(pFirst, sSigFirst, sizeof(sSigFirst));
		}
		else{
			_structSig(pCur, sSig, sizeof(sSig));
			if(strcmp(sSig, sSigFirst) != 0){
				daslog_error_v("%s does not have the same variables as %s, skipping it",
					sPath, pFirst->sPath);
				CDFcloseCDF(pCur->id);
				continue;
			}
			/* the arrays and datasets belong to the first inventory; point
			   the readers at this file's variables by number */
			for(int j = 0; j < pFirst->nDs; ++j){
				cdf_ds_t* pDs = pFirst->aDs + j;
				for(int r = 0; r < pDs->nRead; ++r){
					int iHere = _varIndex(pCur, pFirst->aVars[pDs->aRead[r].iVar].sName);
					pCur->aVars[iHere].nRecs = pCur->aVars[iHere].nRecs;   /* same layout */
				}
			}
		}

		/* Stream each dataset; the readers refer to the first file's inventory
		   indices, so for later files fetch by name into the current file */
		for(int j = 0; j < pFirst->nDs; ++j){
			cdf_ds_t* pDs = pFirst->aDs + j;
			if(nFiles == 0){
				if(_streamDataset(pFirst, pDs, g_pIoOut, pTimeRng, &nPkts) != DAS_OKAY) goto STREAM_ERR;
			}
			else{
				/* remap reader indices and the time base to this file */
				cdf_ds_t dsHere = *pDs;
				dsHere.iTime = _varIndex(pCur, pFirst->aVars[pDs->iTime].sName);
				for(int r = 0; r < dsHere.nRead; ++r)
					dsHere.aRead[r].iVar = _varIndex(pCur, pFirst->aVars[pDs->aRead[r].iVar].sName);
				if(_streamDataset(pCur, &dsHere, g_pIoOut, pTimeRng, &nPkts) != DAS_OKAY) goto STREAM_ERR;
			}
		}

		CDFcloseCDF(pCur->id);
		pCur->id = NULL;
		++nFiles;
	}
	fini_DasUriIter(&iter);
	del_DasUriTplt(pTplt);

	if(nPkts == 0){
		if(!(g_pIoOut->bSentHeader))
			DAS_EXIT( DasIO_writeDesc(g_pIoOut, (DasDesc*)g_pSd, 0) );
		OobExcept except;
		char sMsg[256] = {'\0'};
		if(pTimeRng != NULL){
			char sBeg[64], sEnd[64];
			das_datum_toStr(&(pTimeRng->dBeg), sBeg, sizeof(sBeg), 3);
			das_datum_toStr(&(pTimeRng->dEnd), sEnd, sizeof(sEnd), 3);
			snprintf(sMsg, sizeof(sMsg) - 1, "No data in range %s to %s", sBeg, sEnd);
		}
		else
			snprintf(sMsg, sizeof(sMsg) - 1, "No data in %s", opts.sPattern);
		OobExcept_set(&except, DAS_EX_NO_DATA, sMsg);
		DAS_EXIT( DasIO_writeException(g_pIoOut, &except) );
	}

	DasIO_close(g_pIoOut);
	daslog_info_v("%ld records sent from %d file(s)", nPkts, nFiles);
	free(pFirst);
	free(pFile);
	return 0;

STREAM_ERR:
	_bounce_to_log();   /* exits through the log handler */
	return PERR;
}
