/* Copyright (C) 2024 Chris Piker <chris-piker@uiowa.edu>
 *
 * This file is part of das2C, the Core DAS C Library.
 * 
 * Das2C is free software; you can redistribute it and/or modify it under
 * the terms of the GNU Lesser General Public License version 2.1 as published
 * by the Free Software Foundation.
 *
 * Das2C is distributed in the hope that it will be useful, but WITHOUT ANY
 * WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS
 * FOR A PARTICULAR PURPOSE.  See the GNU Lesser General Public License for
 * more details.
 *
 * You should have received a copy of the GNU Lesser General Public License
 * version 2.1 along with Das2C; if not, see <http://www.gnu.org/licenses/>. 
 */

#define _POSIX_C_SOURCE 200112L

#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <string.h>
#include <locale.h>
#include <limits.h>

#ifndef _WIN32 
#include <unistd.h>
#include <strings.h>
#else
#define strcasecmp _stricmp
#define strncasecmp _strnicmp
#endif

#include <das2/core.h>

/* State ******************************************************************** */

int g_nTimeWidth = 24;    /* default to 'time24' */
const char* g_sTimeFmt = NULL;

int g_n8ByteWidth = 17;   /* default to 'ascii17' for 8-byte floats */
int g_n4ByteWidth = 14;   /* default to 'ascii14' for 4-byte floats */
char g_sSep[12] = {';', '\0'};

bool g_bPropOut = false;
bool g_bHeaders = true;
bool g_bIds = true;

#define PERR (DASERR_MAX + 1)

/* Help ********************************************************************* */

void prnHelp()
{
	printf(
"SYNOPSIS\n"
"   das3_csv - Transform das streams to a delimited text format\n"
"\n"
"USAGE\n"
"   das3_csv [options] < INFILE\n"
"\n"
"DESCRIPTION\n"
"   das3_csv is a filter.  It reads a das2 or das3 stream on standard input and\n"
"   writes a delimited text stream suitable for use in common spreadsheet\n"
"   programs to standard output.\n"
"\n"
"   To preserve some of the structure of a das stream while providing compatible\n"
"   output, each row starts with the dataset ID.  Dataset IDs are positive\n"
"   integers and all rows for the same dataset are tagged with the same value.\n"
"   ID zero is a special value which indicates global stream information.  For\n"
"   data streams which only output a single dataset, these IDs may be safely\n"
"   ignored and thus `-i` may be used to disable them.\n"
"\n"
"   The second column (or first if using `-i`) has one of the following strings:\n"
"\n"
"      \"header\"   - The row contains dataset header information\n"
"      \"values\"   - The row contains data values\n"
"      \"property\" - The row contians an object property\n"
"\n"
"   In general, streams may contain any number of datasets, thus the output may\n"
"   contain any number of header rows.  Header rows can be disabled via the `-n`\n"
"   option below.  Note that das streams push new object definitions onto the\n"
"   stream as they are encountered so for multi-dataset streams, new headers may\n"
"   be encountered *after* data values start.\n"
"\n"
"   Within a dataset, the number of header columns and the number of data\n"
"   columns are always the same.  \"Property\" rows do not attempt to match the\n"
"   number of columns as the surounding datasets, however object properties are\n"
"   not emitted by default.  Thus for single-dataset streams, with default\n"
"   options, the output of das3_csv is RFC-4180 compliant.\n"
"\n"
"DEFAULTS\n"
"   * All object properties are dropped, except for 'label' if available.\n"
"\n"
"   * The field delimiter character is a ';' (semicolon).\n"
"\n"
"   * Input UTF-8 values are output as-is, without conversions\n"
"\n"
"   * 32-bit floating point values are written with 6 significant digits in\n"
"     the mantissa and 2 digits in the exponent.\n"
"\n"
"   * 64-bit floating point values are written with 16 significant digits in\n"
"     the mantissa and 2 digits in the exponent.\n"
"\n"
"   * Time values are written as ISO-8601 timestamps with microsecond resolution,\n"
"     i.e. the pattern YYYY-MM-DDTHH:mm:SS.ssssss\n"
"\n"
"   * All output values are rounded normally instead of truncating fractions.\n"
"\n"
"   * All output text is encoded as UTF-8.\n"
"\n"
"OPTIONS\n"
"\n"
"   -h,--help  Show this help text\n"
"\n"
"   -l LEVEL,--log=LEVEL\n"
"              Set the logging level, where LEVEL is one of 'debug', 'info',\n"
"              'warning', 'error' in order of decreasing verbosity.  All log\n"
"              messages go to the standard error channel, the default is 'info'.\n"
"\n"
"   -p,--props Output object property rows.  Each property row is tagged with\n"
"              a 1st column containing the string \"property\".\n"
"\n"
"   -n,--no-headers\n"
"              Do not output column headers.  This makes for an under-documented\n"
"              output file, but is useful in some cases.  Using this option\n"
"              overrides `-p` if both are given.\n"
"\n"
"   -i,--no-id\n"
"              Do not output logical dataset IDs in the first column.  Das\n"
"              streams can define multiple datasets but if a data source is\n"
"              known to generate only a single dataset in each stream, then the\n"
"              ID column may be omitted without loss of clarity.\n"
"\n"
"   -d DELIM   Change the default text delimiter from ';' (semicolon) to some\n"
"              other ASCII 7-bit character.\n"
"\n"
"   -r DIGITS  Set the number of significant digits for general output.  The\n"
"              minimum resolution is 2 significant digits.\n"
"\n"
"   -s SUBSEC  Set the sub-second resolution.  Output N digits of sub-second\n"
"              resolution.  The minimum value is 0, thus time values are always\n"
"              output to at least seconds resolution.\n"
"\n"
"AUTHOR\n"
"   chris-piker@uiowa.edu\n"
"\n"
"SEE ALSO\n"
"   das2_ascii, das3_cdf\n"
);
}

/* Helpers ************************************************************* */

void _writeProps(DasDesc* pDesc, int nPktId, const char* sItem)
{
	size_t uProps = DasDesc_length(pDesc);
	for(size_t u = 0; u < uProps; ++u){
		const DasProp* pProp = DasDesc_getPropByIdx(pDesc, u);
		if(pProp == NULL) continue;

		// Write in the order: sope name, type, units, value
		// for multi-value properties, use the spreadsheet's separator, not whatever
		// the property may have been using

		if(g_bIds)
			printf("%d%s", nPktId, g_sSep);
		if(g_bHeaders)
			printf("\"property\"%s", g_sSep);

		if(DasProp_units(pProp) == UNIT_DIMENSIONLESS){
			printf("\"%s\"%s\"%s\"%s\"%s\"%s%s", 
				sItem, g_sSep, DasProp_name(pProp), g_sSep, DasProp_typeStr3(pProp), 
				g_sSep, g_sSep
			);
		}
		else{
			printf("\"%s\"%s\"%s\"%s\"%s\"%s\"%s\"%s", 
				sItem, g_sSep, DasProp_name(pProp), g_sSep, DasProp_typeStr3(pProp), 
				g_sSep, DasProp_units(pProp), g_sSep
			);
		}

		const char* sVal = DasProp_value(pProp);
		if(DasProp_items(pProp) < 2){
			printf("\"%s\"\r\n", sVal);
		}
		else{
			char cSep = DasProp_sep(pProp);
			putchar('"');
			while(*sVal != '\0'){
				if(*sVal == cSep)
					printf("\"%s\"", g_sSep);
				else
					putchar(*sVal);
				++sVal;
			}
			fputs("\"\r\n", stdout);
		}
	}
}

/* Stream Start ************************************************************* */

DasErrCode onStream(StreamDesc* pSd, void* pUser)
{
	if(g_bPropOut && g_bHeaders)
		_writeProps((DasDesc*)pSd, 0, "global");

	return DAS_OKAY;
}


/* DataSet Start ************************************************************* */

/* The first header row, pretty much gives the variable ID and units */
#define PRN_VARID 1
#define PRN_UNITS 2
void _prnVarHdrs(DasDs* pDs, int nOutput, enum dim_type dmt)
{
	const char* sCat = (dmt == DASDIM_COORD) ? "coord" : "data";

	int nRank = DasDs_rank(pDs);

	/* Loop over all variables generating headers */

	const DasDim* pDim = NULL;
	const DasVar* pVar = NULL;
	const char* sRole = NULL;
	das_units units = UNIT_DIMENSIONLESS;

	ptrdiff_t aVarShape[DASIDX_MAX] = DASIDX_INIT_UNUSED;
	
	size_t uD, uV, uDims = DasDs_numDims(pDs, dmt);
	bool bFirst = (dmt == DASDIM_COORD);

	for(uD = 0; uD < uDims; ++uD){
		pDim = DasDs_getDimByIdx(pDs, uD, dmt);

		for(uV = 0; uV < DasDim_numVars(pDim) ; ++uV){
			sRole = DasDim_getRoleByIdx(pDim, uV);
			pVar = DasDim_getVarByIdx(pDim, uV);
			units = DasVar_units(pVar);

			char sOutput[256] = {'\0'};
			switch(nOutput){
			case PRN_VARID:
				if(strcasecmp(sRole, "center") == 0)
					snprintf(sOutput, 255, "%s:%s", sCat, DasDim_id(pDim));
				else
					snprintf(sOutput, 255, "%s:%s:%s", sCat, DasDim_id(pDim), sRole);
				break;
			case PRN_UNITS: 
				if(units == UNIT_DIMENSIONLESS) sOutput[0] = '\0'; 
				else snprintf(sOutput, 255, "(%s)", Units_toStr(units)); 
				break;
			default:
				das_error(PERR, "Logic error in das3_csv");
				return;
			}

			if(bFirst){
				printf("\"%s\"", sOutput);
				bFirst = false;
			}
			else
				printf("%s\"%s\"", g_sSep, sOutput);

			// If this is a multi-valued item, add commas to the extent needed.
			// Ignore the first index, that's the stream index, it doesn't affect
			// the headers
			DasVar_shape(pVar, aVarShape);
			int nSeps = 1;
			for(int i = 1; i < nRank; ++i){
				if(aVarShape[i] >= 0)
					nSeps *= aVarShape[i];
			}

			// If this is a vector, we'll need separators for each direction
			if(DasVar_valType(pVar) == vtGeoVec){
				ubyte uComp = 0;
				DasVar_vecMap(pVar, &uComp, NULL);
				nSeps *= uComp;
			}
			nSeps -= 1;
			for(int i = 0; i < nSeps; ++i) fputs(g_sSep, stdout);

		}
	}

}

/* Helper for a helper, output a row of constant values for "DEPEND_1" */
void _prnTblHdr(const DasDs* pDs, const DasVar* pVar)
{
	das_datum dm;
	dasds_iterator iter;
	char sBuf[64] = {'\0'};

	bool bFirst = true;
	for(dasds_iter_init(&iter, pDs); !iter.done; dasds_iter_next(&iter)){

		/* Only run for the first record since this variable is defined not to
		   be record varying */
		if(iter.index[0] > 0) break;

		DasVar_get(pVar, iter.index, &dm);
		if(bFirst)
			bFirst = false;
		else
			fputs(g_sSep, stdout);

		fputs(das_datum_toStrValOnly(&dm, sBuf, 63, 6), stdout);
	}	
}

void _prnVecLblHdr(const DasDim* pDim, const DasVar* pVar)
{
	ubyte aDirs[4] = {0};
	ubyte uDirs = 0;
	DasVar_vecMap(pVar, &uDirs, aDirs);

	char psLabels[3][32] = {'\0'};
	int nLabels = das_makeCompLabels(pVar, (char**) psLabels, 32); 
	
	for(int i = 0; i < uDirs; ++i){
		if(i > 0)
			fputs(g_sSep, stdout);

		if(i < nLabels)
			printf("\"%s\"", psLabels[i]);
		else
			printf("\"component_%d\"", i);
	}
}

/* The second row exists mostly to print out values for: 
   1. Single value: the label
   2. Vector value: the labels
   3. Non-record value: the N/R values  (needs to be on a third row!)
*/
void _prnVarLblHdrs(DasDs* pDs, enum dim_type dmt)
{
	const DasDim* pDim = NULL;
	const DasVar* pVar = NULL;
	
	ptrdiff_t aVarShape[DASIDX_MAX] = DASIDX_INIT_UNUSED;
	
	size_t uD, uV, uDims = DasDs_numDims(pDs, dmt);
	bool bFirst = (dmt == DASDIM_COORD);

	for(uD = 0; uD < uDims; ++uD){
		pDim = DasDs_getDimByIdx(pDs, uD, dmt);

		for(uV = 0; uV < DasDim_numVars(pDim) ; ++uV){
			pVar = DasDim_getVarByIdx(pDim, uV);

			/* If I'm not the first item in this row, output a comma */
			if(!bFirst) 
				fputs(g_sSep, stdout);
			else
				bFirst = false;


			/* CSV isn't really meant for rank 2+ items, but try anyway.  That's why
				we have das3 in the first place.  But handle three cases:
				1. Single value -> just print the label
				2. Vector value -> print vector labels (per row)
				3. Table value  -> Print "frequencies"
			*/

			DasVar_shape(pVar, aVarShape);
			if(aVarShape[0] < 0){ // Not record varying
				_prnTblHdr(pDs, pVar);
				continue;
			}
			
			/* Okay we are record varying, so just do single label or vector label */
			if(DasVar_valType(pVar) == vtGeoVec){
				_prnVecLblHdr(pDim, pVar);
				continue;
			}

			/* nothing fancy just print regular header */
			const char* sVal = DasDesc_get((DasDesc*)pDim, "label");
			if((sVal != NULL)&&(sVal[0] != '\0'))
				printf("\"%s\"", sVal);
		}
	}
}

DasErrCode onDataSet(StreamDesc* pSd, int iPktId, DasDs* pDs, void* pUser)
{

	// Maybe emit properties
	if(g_bPropOut && g_bHeaders){
		_writeProps((DasDesc*)pDs, iPktId, DasDs_group(pDs));
		
		enum dim_type aDt[2] = {DASDIM_COORD, DASDIM_DATA};
		const DasDim* pDim;
		char sBuf[128] = {'\0'};
		for(size_t c = 0; c < 2; ++c){
			for(size_t u = 0; u < DasDs_numDims(pDs, aDt[c]); ++u){
				pDim = DasDs_getDimByIdx(pDs, u, aDt[c]);
				snprintf(sBuf, 127, "%s:%s", DasDs_group(pDs), DasDim_id(pDim));
				_writeProps((DasDesc*)pDim, iPktId, sBuf);
			}	
		}
	}

	if(!g_bHeaders)
		return DAS_OKAY;

	if(g_bIds) printf("%d%s", iPktId, g_sSep);
	if(g_bHeaders) printf("\"header\"%s", g_sSep);
	_prnVarHdrs(pDs, PRN_VARID, DASDIM_COORD);
	_prnVarHdrs(pDs, PRN_VARID, DASDIM_DATA);
	fputs("\r\n", stdout);

	if(g_bIds) printf("%d%s", iPktId, g_sSep);
	if(g_bHeaders) printf("\"header\"%s", g_sSep);
	_prnVarHdrs(pDs, PRN_UNITS, DASDIM_COORD);
	_prnVarHdrs(pDs, PRN_UNITS, DASDIM_DATA);
	fputs("\r\n", stdout);		

	if(g_bIds) printf("%d%s", iPktId, g_sSep);
	if(g_bHeaders) printf("\"header\"%s", g_sSep);
	_prnVarLblHdrs(pDs, DASDIM_COORD);
	_prnVarLblHdrs(pDs, DASDIM_DATA);
	fputs("\r\n", stdout);

	return DAS_OKAY;
}

/* Dataset update ************************************************************ */

DasErrCode onData(StreamDesc* pSd, int iPktId, DasDs* pDs, void* pUser)
{
	/* Loop over all the data for this slice and print it, then clear the 
	   dataset */

	das_datum dm;
	DasDsUniqIter iter;
	char sBuf[128] = {'\0'};

	/* for this dataset, get the list of variables that are worth printing
	   Should actually do this once and save it! */
	const DasDim* pDim = NULL;
	DasVar* pVar = NULL;
	DasVar* aVars[128] = {0};
	size_t uVars = 0;

	enum dim_type aDt[2] = {DASDIM_COORD, DASDIM_DATA};

	if(g_bIds) printf("%d%s", iPktId, g_sSep);
	if(g_bHeaders) printf("\"values\"%s", g_sSep);
	
	for(size_t c = 0; c < 2; ++c){
		for(size_t u = 0; u < DasDs_numDims(pDs, aDt[c]); ++u){
			pDim = DasDs_getDimByIdx(pDs, u, aDt[c]);
			for(size_t v = 0; v < DasDim_numVars(pDim); ++v){
				pVar = (DasVar*) DasDim_getVarByIdx(pDim, v);
				if(!DasVar_degenerate(pVar, 0)){
					aVars[uVars] = pVar;
					++uVars;
				}	
			}	
		}	
	}

	bool bFirst = true;
	for(size_t v = 0; v < uVars; ++v){
		DasDsUniqIter_init(&iter, pDs, aVars[v]);
		for(; !iter.done; DasDsUniqIter_next(&iter)){
			DasVar_get(aVars[v], iter.index, &dm);
			if(bFirst)
				bFirst = false;
			else
				fputs(g_sSep, stdout);
			fputs(das_datum_toStrValOnlySep(&dm, sBuf, 127, 6, g_sSep), stdout);
		}
	}
	fputs("\r\n", stdout);

	/* clean out the record varying stuff */
	size_t uCleared = DasDs_clearRagged0(pDs);
	daslog_debug_v("Cleared %zu bytes of dataset memory", uCleared);
	return DAS_OKAY;
}

/* Exceptions *************************************************************** */

DasErrCode onExcept(OobExcept* pExcept, void* vpSep)
{
	/* Can't do much here but quit with log message */
	fprintf(stderr, "Stream Exception: %s, %s\n", pExcept->sType, pExcept->sMsg);
	
	return DAS_OKAY;
}

/* ************************************************************************* */
DasErrCode onClose(StreamDesc* pSd, void* pUser)
{

	return DAS_OKAY;
}

/* Main ********************************************************************* */

int main( int argc, char *argv[]) {

	int i = 0;
	/* int status = 0; */
	int nGenRes = 7;
	int nSecRes = 3;
	char sTimeFmt[64] = {'\0'};
	const char* sLevel = "info";
	
	/* Exit on errors, log info messages and above */
	das_init(argv[0], DASERR_DIS_EXIT, 0, DASLOG_INFO, NULL);
	
	for(i = 1; i < argc; i++){
		if(strcmp(argv[i], "-h") == 0 || strcmp(argv[i], "--help") == 0 ){
			prnHelp();
			return 0;
		}
		if(strcmp(argv[i], "-p") == 0 || strcmp(argv[i], "--props") == 0 ){
			g_bPropOut = true;
			continue;
		}
		if(strcmp(argv[i], "-n") == 0 || strcmp(argv[i], "--no-headers") == 0 ){
			g_bHeaders = false;
			g_bPropOut = false;
			continue;
		}
		if(strcmp(argv[i], "-i") == 0 || strcmp(argv[i], "--no-ids") == 0 ){
			g_bIds = false;
			continue;
		}
		if(strcmp(argv[i], "-r") == 0){
			i++;
			if(i >= argc)
				return das_error(PERR, "Resolution parameter missing after -r\n");
			
			nGenRes = atoi(argv[i]);
			if(nGenRes < 2 || nGenRes > 18)
				return das_error(PERR, 
					"Can't format to %d significant digits, supported range is "
					"only 2 to 18 significant digits.\n", nGenRes
				);
			
			continue;
		}
		if(strcmp(argv[i], "-l") == 0 || strcmp(argv[i], "--log") == 0){
			++i;
			if(i >= argc){
				return das_error(PERR, "Log level missing after -l");
			}
			sLevel = argv[i];
			continue;
		}
		if(strcmp(argv[i], "-s") == 0){
			i++;
			if(i >= argc)
				return das_error(PERR,"Sub-seconds resolution parameter missing after -s\n");
			
			nSecRes = atoi(argv[i]);
			if(nSecRes < 0 || nSecRes > 9)
				return das_error(
					PERR, "Only 0 to 9 sub-seconds digits supported don't know how to "
					"handle %d sub-second digits.", nSecRes
				);
			continue;
		}
		if(strcmp(argv[i], "-d") == 0){
			i++;
			int j;
			for(j = 0; (j < strlen(argv[i])) && (j < 11); ++j){
				g_sSep[j] = argv[i][j];
			}
			g_sSep[j] = '\0';
			continue;
		}
		
		return das_error(PERR, "unknown parameter '%s'\n", argv[i]);
	}

	daslog_setlevel(daslog_strlevel(sLevel));
	
	if(nGenRes != 7){
		g_n4ByteWidth = nGenRes + 7;
		g_n8ByteWidth = nGenRes + 7;
	}
	
	if(nSecRes != 3){
		if(nSecRes == 0)
			g_nTimeWidth = 20;
		else
			g_nTimeWidth = 21 + nSecRes;
		
		sprintf(sTimeFmt, "%%04d-%%02d-%%02dT%%02d:%%02d:%%0%d.%df", 
				  nSecRes + 3, nSecRes);
		g_sTimeFmt = sTimeFmt;
	}
	
	StreamHandler handler;
	memset(&handler, 0, sizeof(StreamHandler));
	handler.streamDescHandler = onStream;
	handler.dsDescHandler     = onDataSet;
	handler.dsDataHandler     = onData;
	handler.exceptionHandler  = onExcept;
	handler.closeHandler      = onClose;
	/* handler.userData          = &ctx; */

	DasIO* pIn = new_DasIO_cfile("Standard Input", stdin, "r");
	DasIO_model(pIn, 3); /* Upgrade any das2 <packets>s to das3 <datasets>s */

	DasIO_addProcessor(pIn, &handler);
	
	int nRet = DasIO_readAll(pIn);
	return nRet;
}
