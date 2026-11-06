/* Copyright (C) 2026   Chris Piker <chris-piker@uiowa.edu>
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
#include <das3/form_vec.h>   /* core.h does not pull the form headers in */
#include <das3/form_pos.h>
#include <das3/form_pt.h>
#include <das3/form_lin.h>
#include <das3/form_cplx.h>
#include "cdfmodel.h"

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
"   Data are selected by coordinate range, see Ranges below.  Only\n"
"   variables that depend on every ranged coordinate are streamed;\n"
"   everything else in the file (calibration tables, orbit constants, and\n"
"   the like) is ignored.  To see what a file holds, and what would be\n"
"   skipped, use --no-op.\n"
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
"      reader line of '" PROG " --das2 PATTERN' works unchanged there.  The\n"
"      pair means the same as --range=time,BEGIN,END, see Ranges below.\n"
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
"   Ranges\n"
"\n"
"   A range tells the program which part of the data you want.  It names a\n"
"   coordinate, usually time, and gives a beginning and an end.  The\n"
"   beginning is included and the end is not, so a request for\n"
"   2025-08-04 to 2025-08-05 is exactly one day, and a second request that\n"
"   starts at 2025-08-05 picks up where the first left off without\n"
"   repeating anything.\n"
"\n"
"   Times are written in the ISO-8601 style, largest part first.  Trailing\n"
"   parts may be left off when they are zero, so 2025-08-04 means midnight\n"
"   at the start of that day, and 2025-08-04T23:00 means 11 PM.  A day of\n"
"   the year works too: 2025-216 is the same day as 2025-08-04.  All times\n"
"   are UTC.\n"
"\n"
"   A range does two jobs.  First it picks which files to open, by reading\n"
"   the dates out of their names: a file named for a day is opened if any\n"
"   part of that day falls inside the range.  Then, within each file, it\n"
"   picks which records to send, by looking at each record's own time.  So\n"
"   a request for the last hour of a day opens one daily file and sends\n"
"   only that hour.\n"
"\n"
"   Either end may be left open by leaving it empty.  For example\n"
"   --range=time,2025-288, asks for everything from the start of day 288\n"
"   onward, and --range=time,,2025-290 asks for everything before day 290.\n"
"   When PATTERN has date fields at least one end must be given.  A range\n"
"   with no ends at all, --range=time, is allowed only for a single file,\n"
"   where it means \"all of it\".  A single file read with no --range at all\n"
"   is treated the same way.\n"
"\n"
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
"      * Data variables that share a DEPEND_0 share an output dataset. A\n"
"        file with several time bases yields several datasets.\n"
"\n"
"      * The das extension attribute plotGroup, written by das3_cdf, gives\n"
"        the dataset's group (datasets meant to be plotted together).  It\n"
"        is part of the dataset key, so data variables on one time base but\n"
"        different groups make separate datasets.\n"
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
"        than as a table.  It is found through DEPEND_N, or through\n"
"        AUX_DEPEND_N when another coordinate, such as energy, holds\n"
"        DEPEND_N for the same index.\n"
"\n"
"      * The das extension attributes COMPONENT_SYSTEM and COMPONENT_ORDER,\n"
"        written by das3_cdf, override the guesses above.  With FRAME_ORIGIN\n"
"        or opsKind=position the variable is a position rather than a free\n"
"        vector.  On a variable with no component axis they mark it as ONE\n"
"        component of a vector, for example an anode table holding only the\n"
"        theta of each look direction, which is sent as a one component\n"
"        composite.\n"
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
"      FRAME_NAME             -> frame\n"
"      COORDINATE_SYSTEM      -> frame (if there is no FRAME_NAME)\n"
"      SCALEMIN,SCALEMAX      -> scaleMin,scaleMax\n"
"      SCALETYP               -> scaleType\n"
"      AVG_TYPE               -> avgType\n"
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
"                 Select data by coordinate range, see Ranges above.  May\n"
"                 be repeated, once per coordinate; a record must fall in\n"
"                 every range given.\n"
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
"\n"
"   -V,--vars     List the variables and components of the first matching\n"
"                 file that can be named as VAR, one line each with a short\n"
"                 description from the file, and exit.  The quick way to\n"
"                 build an invocation; -n is the full diagnostic.\n"
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
"   * Time is the only coordinate this version can range on right now.\n"
"     Variables that do not depend on it are not served in this version\n"
"\n"
"   * CDF_EPOCH16 time variables are not supported and are skipped.\n"
"\n"
"   * File name patterns keyed on orbit number or spacecraft clock are not\n"
"     yet supported.\n"
"\n"
"   * Output is das3 only, see das2_from_cdf for das2 stream generation.\n"
"\n"
"   * There is no per-variable override yet.  A map file for renaming,\n"
"     dropping and forcing the kind of a variable, and a hand-authored\n"
"     dataset header used as a template, are future work.\n"
"\n");

	printf(
"MAINTAINER\n"
"   chris-piker@uiowa.edu\n"
"\n");

	printf(
"SEE ALSO\n"
"   * das2_from_cdf, das3_cdf, das3_csv, das3_spice\n"
"   * ISTP CDF guidelines: https://spdf.gsfc.nasa.gov/istp_guide/istp_guide.html\n"
"\n");
}

/* ************************************************************************* */
/* Program options */

#define MAX_RANGES     8

typedef struct program_options {
	char aLevel[32];
	char aDefVars[1024];   /* --def-vars, comma separated */
	char aPropMap[512];    /* --prop-map, "FROM:TO,FROM:TO" */
	char aCoordMap[512];   /* --coord, "COORD:VAR,COORD:VAR" */
	bool bNoOp;
	bool bVars;            /* -V: list the variables a user can name, then exit */
	bool bDas2Args;        /* --das2: BEGIN END are positional */
	const char* sPattern;  /* file name or URI pattern, points into argv */
	das_dim_range aRanges[MAX_RANGES];
	int         nRanges;
	const char* asVars[MAX_DATA_VARS];  /* point into argv */
	int         nVars;
} popts_t;

/* Argument errors happen before the stream is open; DasIO_exception_v()
   sends the stub header every das stream must start with. */
static int _argError(const char* sMsg)
{
	daslog_error_v("%s", sMsg);
	DasIO* pOut = new_DasIO(PROG, new_DasXport_stdout(), "3");
	DasIO_exception_v(pOut, DAS_EX_QUERY_ERR, "%s", sMsg);
	del_DasIO(pOut);
	return PERR;
}

/* One end of a range for messages: its value, or "(open)" when null */
static void _rangeEndStr(const das_datum* pDm, char* sBuf, size_t uLen, int nFrac)
{
	if(das_datum_isNull(pDm)) snprintf(sBuf, uLen, "(open)");
	else das_datum_toStr(pDm, sBuf, uLen, nFrac);
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
	/* Bounds come from the file when none are given: an unbounded range names
	   its coordinate and nothing else.  Null datums are the "no bound" mark. */
	das_range rng;
	das_range_init(&rng);

	if(sBeg == NULL){
		/* open at both ends */
	}
	else if(strcmp(sCoord, "time") == 0){
		if(das_range_fromUtc(&rng, sBeg, sEnd) != DAS_OKAY){
			snprintf(sMsg, sizeof(sMsg) - 1,
				"Could not parse the time range %s to %s", sBeg, sEnd
			);
			return _argError(sMsg);
		}
	}
	else{
		das_datum dmBeg, dmEnd;
		if((!das_datum_fromStr(&dmBeg, sBeg))||(!das_datum_fromStr(&dmEnd, sEnd))||
		   (das_range_fromDatum(&rng, &dmBeg, &dmEnd) != DAS_OKAY)
		){
			snprintf(sMsg, sizeof(sMsg) - 1,
				"Could not parse the %s range %s to %s", sCoord, sBeg, sEnd
			);
			return _argError(sMsg);
		}
	}

	if(das_dim_range_init(pOpts->aRanges + pOpts->nRanges, sCoord, &rng) != DAS_OKAY){
		snprintf(sMsg, sizeof(sMsg) - 1, "Coordinate name too long in --range=%s", sArg);
		return _argError(sMsg);
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
	daserr_setdisp(DASERR_DIS_RET);

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
			if(dascmd_isArg(argv[i], "-V", "--vars", NULL)){
				pOpts->bVars = true;
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
	}

	/* A lone file bounds itself; a pattern has fields to fill in */
	bool bHasFields = (strchr(pOpts->sPattern, '$') != NULL);
	if(pOpts->nRanges == 0){
		if(bHasFields)
			return _argError("No coordinate range given for the pattern, use --range or --das2, see -h");
		das_dim_range_init(pOpts->aRanges, "time", NULL);
		pOpts->nRanges = 1;
	}
	for(int j = 0; j < pOpts->nRanges; ++j){
		const das_range* pRng = &(pOpts->aRanges[j].rng);
		if(!das_range_hasBeg(pRng) && !das_range_hasEnd(pRng) && bHasFields){
			snprintf(sMsg, sizeof(sMsg) - 1,
				"--range=%s has no bounds, but PATTERN has fields to fill in",
				pOpts->aRanges[j].sDim
			);
			return _argError(sMsg);
		}
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
/* Building datasets from the inventory */

/* The das3 side of one classified dataset: its DasDs, packet id, and the
   array each reader in cdf_ds_t.aRead fills */
typedef struct ds_out {
	DasDs*  pDs;
	int     nPktId;
	DasAry* aAry[MAX_CDF_VARS];
} ds_out_t;

static ds_out_t g_aOut[MAX_CDF_DS];

/* A dataset array for a record varying variable, registered for filling */
static DasAry* _newRecAry(cdf_file_t* pFile, cdf_ds_t* pDs, ds_out_t* pOut, int iVar, const char* sId)
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
	das_val_type vt = cdf_isTimeType(pV->nType) ? vtLong : cdf_valType(pV->nType);
	ubyte aFill[16];
	DasAry* pAry = new_DasAry(sId, vt, 0, cdf_varFill(pFile, pV, aFill), nRank, aShape, cdf_varUnits(pV));
	if(pAry == NULL) return NULL;
	if(DasDs_addAry(pOut->pDs, pAry) != DAS_OKAY){ dec_DasAry(pAry); return NULL; }
	dec_DasAry(pAry);   /* the dataset holds the surviving reference */

	cdf_initReader(pFile, iVar, pDs->aRead + pDs->nRead);
	pOut->aAry[pDs->nRead] = pAry;
	++(pDs->nRead);

	DasDs_addFixedCodec(
		pOut->pDs, sId, cdf_semantic(pV), das_vt_serial_type(vt),
		(int)das_vt_size(vt), (int)nElemsPerRec, DASENC_WRITE
	);
	return pAry;
}

/* A rank-1 array holding a non record varying table, read now, sent in the
   header as <values> */
static DasAry* _newNrvAry(cdf_file_t* pFile, ds_out_t* pOut, int iVar, const char* sId)
{
	cdf_var_t* pV = pFile->aVars + iVar;
	if(pV->nDims != 1){
		das_error(PERR, "%s is a rank %ld table; only rank 1 tables are supported", pV->sName, pV->nDims);
		return NULL;
	}
	size_t aShape[1] = { 0 };   /* grows to the table length on append */
	ubyte aFill[16];
	DasAry* pAry = new_DasAry(sId, cdf_valType(pV->nType), 0, cdf_varFill(pFile, pV, aFill), 1, aShape, cdf_varUnits(pV));
	if(pAry == NULL) return NULL;

	size_t uSz = das_vt_size(cdf_valType(pV->nType));
	ubyte* pBuf = (ubyte*)malloc(uSz * pV->aDimSz[0]);
	if((pBuf == NULL)||(CDFgetzVarRecordData(pFile->id, pV->nVarNum, 0, pBuf) != CDF_OK)){
		free(pBuf); dec_DasAry(pAry);
		das_error(PERR, "Could not read table %s", pV->sName);
		return NULL;
	}
	DasAry_append(pAry, pBuf, (size_t)pV->aDimSz[0]);
	free(pBuf);
	if(DasDs_addAry(pOut->pDs, pAry) != DAS_OKAY){ dec_DasAry(pAry); return NULL; }
	dec_DasAry(pAry);
	return pAry;
}

/* The form a classified variable carries */
static DasForm* _newForm(const cdf_var_t* pV, bool bTime)
{
	if(bTime) return new_DasFormPt();
	if(strcmp(pV->sKind, "vector") == 0)
		return new_DasFormVec(
			pV->sFrame, pV->uSys, pV->aDirs
		);
	if(strcmp(pV->sKind, "position") == 0)
		return new_DasFormPos(
			pV->sOrigin, pV->sFrame[0] ? pV->sFrame : NULL,
			pV->sSurface[0] ? pV->sSurface : NULL, pV->bFixed, pV->uSys, pV->aDirs
		);
	if(strcmp(pV->sKind, "complex") == 0)
		return new_DasFormCplx(strcmp(pV->sSystem, "polar") == 0 ? DAS_VSYS_POLAR : DAS_VSYS_RECT);
	return new_DasFormLin();
}

/* Wrap an array as a variable and give it to a dimension under a role.
   pIdxMap has nRank entries.  A composite takes its internal shape from the
   variable's component axes. */
static int _addAryVar(
	cdf_file_t* pFile, ds_out_t* pOut, DasDim* pDim, const char* sRole,
	DasAry* pAry, int iVar, const int8_t* pIdxMap, bool bTime
){
	cdf_var_t* pV = pFile->aVars + iVar;
	int nRank = DasDs_rank(pOut->pDs);
	DasForm* pForm = _newForm(pV, bTime);
	if(pForm == NULL) return PERR;

	ptrdiff_t aIntShape[VARIDX_MAX];
	int nIntRank = 0;
	for(int d = 0; d < pV->nDims; ++d)
		if(pV->aIsInternal[d]) aIntShape[nIntRank++] = pV->aDimSz[d];
	if(pV->bOneComp && (nIntRank == 0))   /* no CDF dim for it, but still a composite */
		aIntShape[nIntRank++] = 1;

	DasVar* pVar = NULL;
	if(nIntRank > 0)
		pVar = new_DasVarCompAry(
			pAry, nRank, pIdxMap, cdf_varUnits(pV), pForm, nIntRank, aIntShape
		);
	else
		pVar = new_DasVarAry(pAry, nRank, pIdxMap, cdf_varUnits(pV), pForm);
	del_DasForm(pForm);   /* the variable took its own reference */
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
	cdf_file_t* pFile, ds_out_t* pOut, DasDim* pDim, const char* sRole, int iVar,
	int iExt, double rMin, double rStep
){
	cdf_var_t* pV = pFile->aVars + iVar;
	int nRank = DasDs_rank(pOut->pDs);
	das_val_type vt = cdf_valType(pV->nType);
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

	DasVar* pVar = new_DasVarSeq(
		et, bInt ? (const ubyte*)&nMin : (const ubyte*)&rMin, nRank,
		bInt ? (const ubyte*)aNStep : (const ubyte*)aRStep, aExt,
		cdf_varUnits(pV), NULL              /* NULL form: linear */
	);
	if(pVar == NULL) return PERR;
	if(!DasDim_addVar(pDim, sRole, pVar)){ dec_DasVar(pVar); return PERR; }
	return DAS_OKAY;
}

/* A dimension's physDim: the class half of DICT_KEY "class>subclass", the
   ISTP word for what the variable measures, or sFallback without one */
static void _physDim(const cdf_var_t* pV, const char* sFallback, char* sBuf, size_t uLen)
{
	if(pV->sDictKey[0] == '\0'){
		strncpy(sBuf, sFallback, uLen - 1);
		sBuf[uLen - 1] = '\0';
		return;
	}
	const char* sGt = strchr(pV->sDictKey, '>');
	size_t u = (sGt != NULL) ? (size_t)(sGt - pV->sDictKey) : strlen(pV->sDictKey);
	if(u > uLen - 1) u = uLen - 1;
	memcpy(sBuf, pV->sDictKey, u);
	sBuf[u] = '\0';
}

/* Build the DasDs for one classified dataset and register it with the
   stream under its packet id */
static int _buildDataset(cdf_file_t* pFile, int iDs, DasStream* pSd)
{
	cdf_ds_t* pDs = pFile->aDs + iDs;
	ds_out_t* pOut = g_aOut + iDs;
	cdf_var_t* pFirst = pFile->aVars + pDs->aMembers[0];
	int nRank = pDs->nRank;
	int8_t aMap[VARIDX_MAX];
	int nRet = DAS_OKAY;

	pOut->pDs = new_DasDs(pDs->sName, pDs->sGroup, nRank);
	if(pOut->pDs == NULL) return PERR;
	pOut->nPktId = iDs + 1;
	pDs->nRead = 0;

	/* which external index each DEPEND_d occupies */
	int aExtOfDep[VARIDX_MAX] = {0};
	{
		int iExt = 1;
		for(int d = 1; d <= pFirst->nDims; ++d){
			if(pFirst->asDepend[d][0] == '\0') continue;
			aExtOfDep[d] = iExt;
			++iExt;
		}
	}

	/* The time offset table, if any, runs along CDF dim iOffDim.  Through
	   DEPEND_N it IS that index's coordinate (iOff names the dim to skip
	   below); through AUX_DEPEND_N it shares the index with one. */
	int iOffDim = -1;
	bool bOffAux = false;
	int iOffVar = cdf_offsetTable(pFile, pFirst, &iOffDim, &bOffAux);
	int iOff = ((iOffVar >= 0) && !bOffAux) ? iOffDim : -1;

	/* time */
	DasDim* pDim = new_DasDim("time", "time", DASDIM_COORD, nRank);
	if(pDim == NULL) return PERR;
	DasDim_setAxis(pDim, 0, "x");
	DasDim_setPrimeCoord(pDim, true);
	cdf_var_t* pT = pFile->aVars + pDs->iTime;
	DasAry* pAry = _newRecAry(pFile, pDs, pOut, pDs->iTime, pT->sName);
	if(pAry == NULL) return PERR;
	for(int i = 0; i < VARIDX_MAX; ++i) aMap[i] = VARIDX_UNUSED;
	aMap[0] = 0;
	nRet = _addAryVar(pFile, pOut, pDim, (iOffVar >= 0) ? DASVAR_REF : DASVAR_CENTER, pAry, pDs->iTime, aMap, true);
	if(nRet != DAS_OKAY) return nRet;
	if(iOffVar >= 0){
		cdf_var_t* pO = pFile->aVars + iOffVar;
		double rMin = 0.0, rStep = 0.0;
		if(cdf_isSequence(pFile, pO, &rMin, &rStep)){
			nRet = _addSeqVar(pFile, pOut, pDim, DASVAR_OFFSET, iOffVar, aExtOfDep[iOffDim], rMin, rStep);
		}
		else{
			pAry = _newNrvAry(pFile, pOut, iOffVar, pO->sName);
			if(pAry == NULL) return PERR;
			for(int i = 0; i < VARIDX_MAX; ++i) aMap[i] = VARIDX_UNUSED;
			aMap[aExtOfDep[iOffDim]] = 0;
			nRet = _addAryVar(pFile, pOut, pDim, DASVAR_OFFSET, pAry, iOffVar, aMap, false);
		}
		if(nRet != DAS_OKAY) return nRet;

		/* The time base's attributes describe the whole dimension, the offset
		   table's describe only itself, so they go on the variable */
		DasVar* pOffVar = DasDim_getVar(pDim, DASVAR_OFFSET);
		if(pOffVar != NULL) cdf_addVarProps(pFile, pO, (DasDesc*)pOffVar);
	}
	cdf_addVarProps(pFile, pT, (DasDesc*)pDim);
	if((nRet = DasDs_addDim(pOut->pDs, pDim)) != DAS_OKAY) return nRet;

	/* the other coordinates */
	const char* asAxes[] = {"x", "y", "z", "w"};
	int iAxis = 1;
	for(int d = 1; d <= pFirst->nDims; ++d){
		if((pFirst->asDepend[d][0] == '\0')||(d == iOff)) continue;
		int iDep = cdf_varIndex(pFile, pFirst->asDepend[d]);
		if(iDep < 0) continue;   /* noted by the classifier */
		cdf_var_t* pC = pFile->aVars + iDep;

		char sPhys[64];
		_physDim(pC, pC->sName, sPhys, sizeof(sPhys));
		pDim = new_DasDim(sPhys, pC->sName, DASDIM_COORD, nRank);
		if(pDim == NULL) return PERR;
		if(iAxis < 4) DasDim_setAxis(pDim, 0, asAxes[iAxis]);
		DasDim_setPrimeCoord(pDim, true);
		++iAxis;

		/* A sequence is a plain scalar, so one component of a direction goes
		   out as a table even when its values are evenly spaced */
		double rMin = 0.0, rStep = 0.0;
		if(!pC->bRecVary && !pC->bOneComp && cdf_isSequence(pFile, pC, &rMin, &rStep)){
			nRet = _addSeqVar(pFile, pOut, pDim, DASVAR_CENTER, iDep, aExtOfDep[d], rMin, rStep);
		}
		else if(!pC->bRecVary){
			pAry = _newNrvAry(pFile, pOut, iDep, pC->sName);
			if(pAry == NULL) return PERR;
			for(int i = 0; i < VARIDX_MAX; ++i) aMap[i] = VARIDX_UNUSED;
			aMap[aExtOfDep[d]] = 0;
			nRet = _addAryVar(pFile, pOut, pDim, DASVAR_CENTER, pAry, iDep, aMap, false);
		}
		else{
			/* a record varying table: [record, N] */
			pAry = _newRecAry(pFile, pDs, pOut, iDep, pC->sName);
			if(pAry == NULL) return PERR;
			for(int i = 0; i < VARIDX_MAX; ++i) aMap[i] = VARIDX_UNUSED;
			aMap[0] = 0;
			aMap[aExtOfDep[d]] = 1;
			nRet = _addAryVar(pFile, pOut, pDim, DASVAR_CENTER, pAry, iDep, aMap, false);
		}
		if(nRet != DAS_OKAY) return nRet;
		cdf_addVarProps(pFile, pC, (DasDesc*)pDim);
		if((nRet = DasDs_addDim(pOut->pDs, pDim)) != DAS_OKAY) return nRet;
	}

	/* data: one dimension per member, uncertainties in the same dimension.

	   Annotation members first.  They become coordinates, and the header
	   lists every coordinate before any data, but packets are laid out in
	   codec creation order.  Building a location after the flux it annotates
	   wrote [flux, location] under a header that declares [location, flux]. */
	size_t uPre = strlen(pDs->sName);
	for(int iPass = 0; iPass < 2; ++iPass)
	for(int m = 0; m < pDs->nMembers; ++m){
		int iVar = pDs->aMembers[m];
		cdf_var_t* pV = pFile->aVars + iVar;
		if(pV->bAnnot != (iPass == 0)) continue;
		const char* sDim = pV->sName;
		if((uPre > 0)&&(strncmp(sDim, pDs->sName, uPre) == 0)&&(sDim[uPre] == '_')&&(sDim[uPre+1] != '\0'))
			sDim += uPre + 1;
		daslog_debug_v("dataset %s: variable %s becomes dimension %s", pDs->sName, pV->sName, sDim);
		char sPhys[64];
		_physDim(pV, sDim, sPhys, sizeof(sPhys));

		if(pV->bAnnot){
			pDim = new_DasDim(sPhys, sDim, DASDIM_COORD, nRank);
			if(pDim == NULL) return PERR;
			DasDim_setAxis(pDim, 0, "x");
			DasDim_setPrimeCoord(pDim, false);   /* annotates the time axis */
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

		pAry = _newRecAry(pFile, pDs, pOut, iVar, pV->sName);
		if(pAry == NULL) return PERR;
		nRet = _addAryVar(pFile, pOut, pDim, DASVAR_CENTER, pAry, iVar, aMap, false);
		if(nRet != DAS_OKAY) return nRet;

		const char* asDelta[2] = { pV->sDeltaPlus, pV->sDeltaMinus };
		const char* asRole[2]  = { DASVAR_MAX_ERR, DASVAR_MIN_ERR };
		for(int k = 0; k < 2; ++k){
			int iD = cdf_varIndex(pFile, asDelta[k]);
			if(iD < 0) continue;
			cdf_var_t* pD = pFile->aVars + iD;
			if(!pD->bRecVary || (pD->nDims != pV->nDims)) continue;
			memcpy(pD->aIsInternal, pV->aIsInternal, sizeof(pD->aIsInternal));
			pAry = _newRecAry(pFile, pDs, pOut, iD, pD->sName);
			if(pAry == NULL) return PERR;
			nRet = _addAryVar(pFile, pOut, pDim, asRole[k], pAry, iD, aMap, false);
			if(nRet != DAS_OKAY) return nRet;
		}

		cdf_addVarProps(pFile, pV, (DasDesc*)pDim);
		/* the common das property for an ISTP support variable sent as data */
		if(!pV->bAnnot && (strcmp(pV->sVarType, "support_data") == 0))
			DasDesc_setStr((DasDesc*)pDim, "varType", "support");
		if((nRet = DasDs_addDim(pOut->pDs, pDim)) != DAS_OKAY) return nRet;
	}

	return DasStream_addDesc(pSd, (DasDesc*)pOut->pDs, pOut->nPktId);
}

/* ************************************************************************* */
/* Streaming records */

#define MAX_BLOCK_BYTES 16777216   /* per variable, per read */

/* Send one dataset's records from the open file, in blocks, keeping only
   records whose time base falls in the range */
static int _streamDataset(
	cdf_file_t* pFile, cdf_ds_t* pDs, const ds_out_t* pOut, DasIO* pIo, const das_range* pRng,
	long* pnPkts
){
	cdf_var_t* pT = pFile->aVars + pDs->iTime;
	long nRecs = pT->nRecs;
	if(nRecs < 1) return DAS_OKAY;

	/* the range as TT2000 */
	int64_t nBeg = INT64_MIN, nEnd = INT64_MAX;   /* open ends */
	das_time dt;
	if((pRng != NULL)&&das_range_hasBeg(pRng)&&(pRng->dmBeg.vt == vtTime)){
		das_datum_toTime(&(pRng->dmBeg), &dt);
		nBeg = dt_to_tt2k(&dt);
	}
	if((pRng != NULL)&&das_range_hasEnd(pRng)&&(pRng->dmEnd.vt == vtTime)){
		das_datum_toTime(&(pRng->dmEnd), &dt);
		nEnd = dt_to_tt2k(&dt);
	}

	/* block size from the widest record */
	long nMaxBytesPerRec = 8;
	for(int r = 0; r < pDs->nRead; ++r){
		cdf_var_t* pV = pFile->aVars + pDs->aRead[r].iVar;
		long n = (long)das_vt_size(cdf_valType(pV->nType)) * pDs->aRead[r].nElemsPerRec;
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
		rec_reader_t rdT = { .iVar = pDs->iTime, .nElemsPerRec = 1, .bTime = true, .nDims = 0 };
		ubyte* pTBuf = cdf_readBlock(pFile, pT->nVarNum, &rdT, nRec0, nHere, uTSz);
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
					if(pKeep[i]) DasAry_append(pOut->aAry[r], (const ubyte*)(pTt + i), 1);
				continue;
			}
			size_t uSz = das_vt_size(cdf_valType(pV->nType));
			ubyte* pBuf = cdf_readBlock(pFile, pV->nVarNum, pR, nRec0, nHere, uSz);
			if(pBuf == NULL){ nRet = PERR; break; }
			size_t uRecBytes = uSz * pR->nElemsPerRec;
			for(long i = 0; i < nHere; ++i)
				if(pKeep[i]) DasAry_append(pOut->aAry[r], pBuf + i * uRecBytes, (size_t)pR->nElemsPerRec);
			free(pBuf);
		}
		if(nRet != DAS_OKAY) break;

		nRet = DasIO_writeData(pIo, (DasDesc*)pOut->pDs, pOut->nPktId);
		if(nRet == DAS_OKAY) *pnPkts += nKept;
		DasDs_clearRagged0(pOut->pDs);
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
	DAS_EXIT( init_DasUriIter(&iter, pTplt, opts.nRanges, opts.aRanges) );

	DAS_EXIT( cdf_parsePropMap(opts.aPropMap) );

	/* the classifier's view of the command line */
	cdf_select_t sel = {
		.sCoordMap = opts.aCoordMap, .sDefVars = opts.aDefVars,
		.asVars = opts.asVars, .nVars = opts.nVars, .pTimeRng = NULL
	};
	for(int i = 0; i < opts.nRanges; ++i)
		if(strcmp(opts.aRanges[i].sDim, "time") == 0){ sel.pTimeRng = &(opts.aRanges[i].rng); break; }

	if(opts.bVars){
		/* every time dependent variable is a candidate, so select them all */
		cdf_select_t selAll = { .sCoordMap = opts.aCoordMap, .sDefVars = "", .asVars = NULL };
		char sFile[DURI_MAX_PATH];
		char sFirst[DURI_MAX_PATH] = {'\0'};
		int nFiles = 0;
		while(DasUriIter_next(&iter, sFile, sizeof(sFile)) != NULL){
			if(nFiles == 0) strncpy(sFirst, sFile, sizeof(sFirst) - 1);
			++nFiles;
		}
		fini_DasUriIter(&iter);
		del_DasUriTplt(pTplt);
		if(nFiles == 0){ printf("No files match %s\n", opts.sPattern); return 0; }
		cdf_file_t* pFile = (cdf_file_t*)calloc(1, sizeof(cdf_file_t));
		int nRet = cdf_openAndClassify(pFile, sFirst, &selAll);
		if(nRet == DAS_OKAY){
			cdf_listVars(pFile, VARIDX_MAX, NULL);
			CDFcloseCDF(pFile->id);
		}
		free(pFile);
		return (nRet == DAS_OKAY) ? 0 : PERR;
	}

	if(opts.bNoOp){
		char sBeg[64] = {'\0'};
		char sEnd[64] = {'\0'};
		printf("Pattern: %s\n", opts.sPattern);
		for(int i = 0; i < opts.nRanges; ++i){
			const das_range* pRng = &(opts.aRanges[i].rng);
			if(!das_range_hasBeg(pRng) && !das_range_hasEnd(pRng)){
				printf("Range: %s, bounds from the file\n", opts.aRanges[i].sDim);
				continue;
			}
			_rangeEndStr(&(pRng->dmBeg), sBeg, sizeof(sBeg), 6);
			_rangeEndStr(&(pRng->dmEnd), sEnd, sizeof(sEnd), 6);
			printf("Range: %s from %s to %s\n", opts.aRanges[i].sDim, sBeg, sEnd);
		}
		if(opts.nVars > 0){
			printf("Requested:");
			for(int i = 0; i < opts.nVars; ++i) printf(" %s", opts.asVars[i]);
			printf("\n");
		}
		printf("Files:\n");
		char sFile[DURI_MAX_PATH];
		char sFirst[DURI_MAX_PATH] = {'\0'};
		int nFiles = 0;
		while(DasUriIter_next(&iter, sFile, sizeof(sFile)) != NULL){
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
		int nRet = cdf_openAndClassify(pFile, sFirst, &sel);
		if(nRet == DAS_OKAY){
			cdf_listFile(pFile, &sel);
			CDFcloseCDF(pFile->id);
		}
		free(pFile);
		return (nRet == DAS_OKAY) ? 0 : PERR;
	}

	/* Streaming.  The first readable file sets the structure; later files
	   must match it or are skipped with an error. */
	g_pIoOut = new_DasIO(PROG, new_DasXport_stdout(), "3");
	g_pSd = new_DasStream();
	DasStream_setVer(g_pSd, STREAM_VER_3_0);
	DasStream_setType(g_pSd, STREAM_TYPE_BASIC);
	g_exType = DAS_EX_SERVER_ERR;

	const das_range* pTimeRng = NULL;
	for(int i = 0; i < opts.nRanges; ++i)
		if((strcmp(opts.aRanges[i].sDim, "time") == 0)&&
		   (das_range_hasBeg(&(opts.aRanges[i].rng)) || das_range_hasEnd(&(opts.aRanges[i].rng))))
			pTimeRng = &(opts.aRanges[i].rng);

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
			/* Build every dataset, then send the headers once */
			if(cdf_addGlobalProps(pFirst, (DasDesc*)g_pSd) != DAS_OKAY) goto STREAM_ERR;
			for(int j = 0; j < pFirst->nDs; ++j)
				if(_buildDataset(pFirst, j, g_pSd) != DAS_OKAY) goto STREAM_ERR;
			DAS_EXIT( DasIO_writeDesc(g_pIoOut, (DasDesc*)g_pSd, 0) );
			for(int j = 0; j < pFirst->nDs; ++j)
				DAS_EXIT( DasIO_writeDesc(g_pIoOut, (DasDesc*)g_aOut[j].pDs, g_aOut[j].nPktId) );
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
			/* the arrays and datasets belong to the first inventory; point
			   the readers at this file's variables by number */
			for(int j = 0; j < pFirst->nDs; ++j){
				cdf_ds_t* pDs = pFirst->aDs + j;
				for(int r = 0; r < pDs->nRead; ++r){
					int iHere = cdf_varIndex(pCur, pFirst->aVars[pDs->aRead[r].iVar].sName);
					pCur->aVars[iHere].nRecs = pCur->aVars[iHere].nRecs;   /* same layout */
				}
			}
		}

		/* Stream each dataset; the readers refer to the first file's inventory
		   indices, so for later files fetch by name into the current file */
		for(int j = 0; j < pFirst->nDs; ++j){
			cdf_ds_t* pDs = pFirst->aDs + j;
			if(nFiles == 0){
				if(_streamDataset(pFirst, pDs, g_aOut + j, g_pIoOut, pTimeRng, &nPkts) != DAS_OKAY) goto STREAM_ERR;
			}
			else{
				/* remap reader indices and the time base to this file */
				cdf_ds_t dsHere = *pDs;
				dsHere.iTime = cdf_varIndex(pCur, pFirst->aVars[pDs->iTime].sName);
				for(int r = 0; r < dsHere.nRead; ++r)
					dsHere.aRead[r].iVar = cdf_varIndex(pCur, pFirst->aVars[pDs->aRead[r].iVar].sName);
				if(_streamDataset(pCur, &dsHere, g_aOut + j, g_pIoOut, pTimeRng, &nPkts) != DAS_OKAY) goto STREAM_ERR;
			}
		}

		CDFcloseCDF(pCur->id);
		pCur->id = NULL;
		++nFiles;
	}
	fini_DasUriIter(&iter);
	del_DasUriTplt(pTplt);

	if(nPkts == 0){
		if(!DasIO_headerSent(g_pIoOut))
			DAS_EXIT( DasIO_writeDesc(g_pIoOut, (DasDesc*)g_pSd, 0) );
		char sMsg[256] = {'\0'};
		if(pTimeRng != NULL){
			char sBeg[64], sEnd[64];
			_rangeEndStr(&(pTimeRng->dmBeg), sBeg, sizeof(sBeg), 3);
			_rangeEndStr(&(pTimeRng->dmEnd), sEnd, sizeof(sEnd), 3);
			snprintf(sMsg, sizeof(sMsg) - 1, "No data in range %s to %s", sBeg, sEnd);
		}
		else
			snprintf(sMsg, sizeof(sMsg) - 1, "No data in %s", opts.sPattern);
		DAS_EXIT( DasIO_exception_v(g_pIoOut, DAS_EX_NO_DATA, "%s", sMsg) );
	}

	_flushLog();
	DasIO_close(g_pIoOut);
	daslog_info_v("%ld records sent from %d file(s)", nPkts, nFiles);
	free(pFirst);
	free(pFile);
	return 0;

STREAM_ERR:
	_fatal();
	return PERR;
}
