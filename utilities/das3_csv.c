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

#include <das3/core.h>

/* Components of the widest composite this program will label.  A 3;3;3 tensor
   is 27 and a 3;3;3;3 is 81; a component label is a plot legend entry, not
   prose. */
#define DASCSV_MAX_COMP 81
#define DASCSV_LBL_SZ  64

/* State ******************************************************************** */

int g_nTimeWidth = 24;    /* default to 'time24' */
const char* g_sTimeFmt = NULL;

int g_nGenRes = 7;        /* Default to 7 sig digits */
int g_nSecRes = 3;        /* default to milliseconds */
int g_n8ByteWidth = 17;   /* default to 'ascii17' for 8-byte floats */
int g_n4ByteWidth = 14;   /* default to 'ascii14' for 4-byte floats */
char g_sSep[12] = {';', '\0'};

bool g_bPropOut = false;
bool g_bHeaders = true;
bool g_bIds = true;
bool g_bCenter = true;    /* reference + offset -> one computed column */

#define PERR (DASERR_MAX + 1)

/* Help ********************************************************************* */

void prnHelp()
{
	printf(
"SYNOPSIS\n"
"   das3_csv - Flatten das streams to rank-1 delimited text\n"
"\n"
"USAGE\n"
"   das3_csv [options] < INFILE\n"
"\n"
"DESCRIPTION\n"
"   das3_csv is a filter. It reads a das2 or das3 stream on standard input and\n"
"   writes delimited text, suitable for spreadsheet programs and data-frame\n"
"   libraries, to standard output.\n"
"\n"
"   A das stream carries structure a flat table cannot: datasets of any rank,\n"
"   records of varying length, vectors and matrices, a 2-D time axis given as\n"
"   a reference plus offsets. Das3_csv flattens all of it. Every row is\n"
"   one location in a dataset's index space and every column holds one value.\n"
"\n"
"   A dataset of rank 2 or higher (a waveform capture, a sensor grid) writes\n"
"   one row per inner sample, and values that belong to an outer index, such\n"
"   as a record's start time, repeat as needed. Composite values such as\n"
"   vectors or matrices occupy one column per component.\n"
"\n"
"   A coordinate given as a reference plus an offset is written as computed\n"
"   centers (but see `-r`). A byte sequence, such as an embedded image, is\n"
"   written as one base64 column.\n"
"\n"
"   What survives of the stream's structure is enough bookkeeping to take the\n"
"   file apart again. Each row starts with the dataset ID. Dataset IDs are\n"
"   positive integers and all rows for the same dataset are tagged with the\n"
"   same value. ID zero is a special value which indicates global stream\n"
"   information. For data streams which only output a single dataset, these\n"
"   IDs may be safely ignored and thus `-i` may be used to disable them.\n"
"\n"
"   The second column (or first if using `-i`) has one of the following strings:\n"
"\n"
"      \"header\"   - The row contains dataset header information\n"
"      \"values\"   - The row contains data values\n"
"      \"property\" - The row contains an object property\n"
"\n"
"   In general, streams may contain any number of datasets, thus the output may\n"
"   contain any number of header rows. Header rows can be disabled via the `-n`\n"
"   option below. Note that das streams push new object definitions onto the\n"
"   stream as they are encountered so for multi-dataset streams, new headers may\n"
"   be encountered *after* data values start.\n"
"\n"
"   Within a dataset there are three header rows: the variable each column\n"
"   belongs to, its units, and a label for the column. The header rows and the\n"
"   value rows always have the same number of columns. \"Property\" rows do not\n"
"   attempt to match the number of columns as the surrounding datasets, however\n"
"   object properties are not emitted by default. Thus for single-dataset\n"
"   streams, with default options, the output of das3_csv is RFC-4180\n"
"   compliant.\n"
"\n"
"DEFAULTS\n"
"   * All object properties are dropped, except for 'label' if available.\n"
"\n"
"   * The field delimiter character is a ';' (semicolon).\n"
"\n"
"   * Input UTF-8 values are output as-is, without conversions\n"
"\n"
"   * Floating point values are written in exponential form with 7 digits after\n"
"     the decimal point (8 significant digits) and 2 digits in the exponent,\n"
"     whatever their storage width.  Integers are written as integers.\n"
"\n"
"   * Time values are written as ISO-8601 timestamps with millisecond\n"
"     resolution, i.e. the pattern YYYY-MM-DDTHH:mm:SS.sss\n"
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
"   -c,--no-center\n"
"              Do not compute center values for reference and offset pairs,\n"
"              but instead report each in separate columns.\n"
"\n"
"   -d DELIM   Change the default text delimiter from ';' (semicolon) to some\n"
"              other ASCII 7-bit character.\n"
"\n"
"   -r DIGITS  Set the number of digits after the decimal point for floating\n"
"              point output, from 2 to 18.\n"
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


/* Columns ******************************************************************* */

/* One row per location in the dataset's index space and one column per
   value.  A variable that varies along any index is a column set: one column
   for a scalar, one per component for a composite.  Values that belong to an
   outer index repeat down their run, which is what lets a ragged dataset be
   written at all; wide rows would need a width no record agrees on.  A
   variable that varies along no index is a constant, not a column; it shows
   in the property rows when those are requested. */

#define DASCSV_MAX_COLS 128

/* Set as pUser on a variable this program built rather than read */
static int g_nSynth = 0;
#define DASCSV_SYNTH ((void*)&g_nSynth)

typedef struct csv_cols {
	DasVar* aVars[DASCSV_MAX_COLS];
	int     nVars;
} CsvCols;

static bool _isColumn(const DasDs* pDs, const DasVar* pVar)
{
	int nRank = DasDs_rank(pDs);
	for(int i = 0; i < nRank; ++i)
		if(!DasVar_degenerate(pVar, i)) return true;
	return false;
}

/* An axis given as reference plus offset is presented as one computed center
   column, the sum, in place of its two parts.  The stream stays the faithful
   record of how the axis was made; a CSV reader wants the axis.  The dimension
   owns the new variable and frees it with the dataset. */
static DasErrCode _addCenter(DasDim* pDim)
{
	if(!g_bCenter) return DAS_OKAY;
	if(DasDim_getVar(pDim, DASVAR_CENTER) != NULL) return DAS_OKAY;

	DasVar* pRef = DasDim_getVar(pDim, DASVAR_REF);
	DasVar* pOff = DasDim_getVar(pDim, DASVAR_OFFSET);
	if((pRef == NULL)||(pOff == NULL)) return DAS_OKAY;

	DasVarBin* pSum = new_DasVarBin(pRef, '+', pOff);
	if(pSum == NULL)
		return das_error(PERR,
			"Could not combine the reference and offset of dimension '%s'",
			DasDim_id(pDim)
		);
	pSum->base.pUser = DASCSV_SYNTH;

	if(!DasDim_addVar(pDim, DASVAR_CENTER, (DasVar*)pSum)){
		dec_DasVar((DasVar*)pSum);
		return das_error(PERR,
			"Could not attach a center to dimension '%s'", DasDim_id(pDim)
		);
	}
	return DAS_OKAY;
}

static DasErrCode _gatherCols(DasDs* pDs, CsvCols* pCols)
{
	pCols->nVars = 0;
	enum dim_type aDt[2] = {DASDIM_COORD, DASDIM_DATA};

	for(int c = 0; c < 2; ++c){
		size_t uDims = DasDs_numDims(pDs, aDt[c]);
		for(size_t u = 0; u < uDims; ++u){
			DasDim* pDim = DasDs_getDimByIdx(pDs, u, aDt[c]);

			DasErrCode nRet = _addCenter(pDim);
			if(nRet != DAS_OKAY) return nRet;

			const DasVar* pCenter = DasDim_getVar(pDim, DASVAR_CENTER);
			bool bSynth = (pCenter != NULL)&&(pCenter->pUser == DASCSV_SYNTH);

			for(size_t v = 0; v < DasDim_numVars(pDim); ++v){
				DasVar* pVar = DasDim_getVarByIdx(pDim, v);
				const char* sRole = DasDim_getRoleByIdx(pDim, v);

				/* the parts of a computed center are not printed again */
				if(bSynth && ((strcasecmp(sRole, DASVAR_REF) == 0)||
				              (strcasecmp(sRole, DASVAR_OFFSET) == 0)))
					continue;

				if(!_isColumn(pDs, pVar)) continue;

				if(pCols->nVars == DASCSV_MAX_COLS)
					return das_error(PERR,
						"More than %d variables to print in dataset '%s'",
						DASCSV_MAX_COLS, DasDs_id(pDs)
					);
				pCols->aVars[pCols->nVars] = pVar;
				pCols->nVars += 1;
			}
		}
	}
	return DAS_OKAY;
}

/* The column list is built once, when the dataset header arrives, and rides
   on the dataset until the stream closes. */
static CsvCols* _colsOf(DasDs* pDs)
{
	if(pDs->pUser != NULL) return (CsvCols*)pDs->pUser;

	CsvCols* pCols = (CsvCols*)calloc(1, sizeof(CsvCols));
	if(_gatherCols(pDs, pCols) != DAS_OKAY){
		free(pCols);
		return NULL;
	}
	pDs->pUser = pCols;
	return pCols;
}

/* Columns a variable occupies: one per component, one for a scalar */
static int _numCols(const DasVar* pVar)
{
	if(DasVar_valType(pVar) != vtComposite) return 1;

	ptrdiff_t aIntr[VARIDX_MAX] = VARIDX_INIT_UNUSED;
	int nRank = DasVar_intrShape(pVar, aIntr);
	int nCols = 1;
	for(int i = 0; i < nRank; ++i)
		if(aIntr[i] > 0) nCols *= (int)aIntr[i];
	return nCols;
}

/* Header rows *************************************************************** */

#define PRN_VARID 1
#define PRN_UNITS 2
#define PRN_LABEL 3

/* One header row.  Every row has the same column count as the value rows
   because all of them are laid out from the same column list. */
static DasErrCode _prnHdrRow(const CsvCols* pCols, int nOutput)
{
	char aLbl[DASCSV_MAX_COMP][DASCSV_LBL_SZ];
	char* psLbl[DASCSV_MAX_COMP];
	for(int i = 0; i < DASCSV_MAX_COMP; ++i) psLbl[i] = aLbl[i];

	for(int c = 0; c < pCols->nVars; ++c){
		const DasVar* pVar = pCols->aVars[c];
		const DasDim* pDim = (const DasDim*)DasDesc_parent((const DasDesc*)pVar);
		int nCols = _numCols(pVar);

		if(c > 0) fputs(g_sSep, stdout);

		char sOut[256] = {'\0'};
		switch(nOutput){
		case PRN_VARID: {
			const char* sCat  = (DasDim_type(pDim) == DASDIM_COORD) ? "coord" : "data";
			const char* sRole = DasVar_role(pVar);
			if(strcasecmp(sRole, DASVAR_CENTER) == 0)
				snprintf(sOut, 255, "%s:%s", sCat, DasDim_id(pDim));
			else
				snprintf(sOut, 255, "%s:%s:%s", sCat, DasDim_id(pDim), sRole);
			printf("\"%s\"", sOut);
			for(int i = 1; i < nCols; ++i) fputs(g_sSep, stdout);
			break;
		}
		case PRN_UNITS: {
			das_units units = DasVar_units(pVar);
			if(units != UNIT_DIMENSIONLESS)
				snprintf(sOut, 255, "(%s)", Units_toStr(units));
			printf("\"%s\"", sOut);
			for(int i = 1; i < nCols; ++i) fputs(g_sSep, stdout);
			break;
		}
		case PRN_LABEL: {
			/* The library's default labelling, the same das3_cdf uses, so a
			   stream labels alike in both outputs */
			int nLbls = DasVar_compLabels(pVar, psLbl, DASCSV_MAX_COMP, DASCSV_LBL_SZ);
			if(nLbls < 0) return -1 * nLbls;

			/* A role other than center is part of the name, or a reference and
			   an offset in one dimension would share a label */
			const char* sRole = DasVar_role(pVar);
			bool bRole = (strcasecmp(sRole, DASVAR_CENTER) != 0);
			for(int i = 0; i < nLbls; ++i){
				if(i > 0) fputs(g_sSep, stdout);
				if(bRole) printf("\"%s_%s\"", aLbl[i], sRole);
				else      printf("\"%s\"", aLbl[i]);
			}
			break;
		}
		default:
			return das_error(PERR, "Logic error in das3_csv");
		}
	}
	fputs("\r\n", stdout);
	return DAS_OKAY;
}

/* Value cells *************************************************************** */

/* Formats a datum for the value rows.  The one departure from the library
   formatter is calendar time: das3_csv writes times as UTC by default; the
   library value formatter stays faithful to storage, so the epoch->das_time
   conversion is done here. */
static const char* _csv_datumStr(
	das_datum* pDm, char* sBuf, size_t uLen, int nFracDigits, const char* sSep
){
	das_datum dmT;
	if((pDm->vt != vtTime) && Units_haveCalRep(pDm->units) && (pDm->units != UNIT_UTC)){
		if((pDm->vt == vtLong) && (pDm->units == UNIT_TT2000))
			dt_from_tt2k((das_time*)&dmT, *((uint64_t*)pDm));
		else{
			double rEpoch;
			if(!das_datum_toDbl(pDm, &rEpoch)) return NULL;
			Units_convertToDt((das_time*)&dmT, rEpoch, pDm->units);
		}
		dmT.vt    = vtTime;
		dmT.vsize = sizeof(das_time);
		dmT.units = UNIT_UTC;
		pDm = &dmT;
	}
	return das_datum_toStrValOnlySep(pDm, sBuf, uLen, nFracDigits, sSep);
}

/* One datum as the cells it occupies.  A composite is one cell per element in
   storage order, the same order the label row used.  A byte sequence is one
   base64 cell: the alphabet holds no delimiter or quote, so an image or a raw
   blob stays a legal field, and it is the encoding das3 text streams already
   use for the same bytes. */
static DasErrCode _prnCells(das_datum* pDm, int nSigDig)
{
	char sBuf[128] = {'\0'};
	const char* sVal = NULL;

	if(pDm->vt == vtComposite){
		size_t uElems = das_datum_nElems(pDm);
		das_val_type et = das_datum_elemType(pDm);
		size_t uElSz = das_vt_size(et);
		const ubyte* pRun = das_datum_run(pDm);
		if((pRun == NULL)||(uElSz == 0))
			return das_error(PERR, "Composite value carries no readable run");

		for(size_t v = 0; v < uElems; ++v){
			das_datum dmCell;
			das_datum_init(&dmCell, pRun + v*uElSz, et, (uint32_t)uElSz, pDm->units);
			if(v > 0) fputs(g_sSep, stdout);
			if((sVal = _csv_datumStr(&dmCell, sBuf, 127, nSigDig, g_sSep)) == NULL)
				return das_error(PERR, "Could not format a composite element");
			fputs(sVal, stdout);
		}
		return DAS_OKAY;
	}

	if(pDm->vt == vtByteSeq){
		const das_cbyte_seq* pBs = (const das_cbyte_seq*)pDm;
		size_t uOut = 0;
		char* sB64 = das_b64_encode(pBs->ptr, pBs->sz, &uOut);
		if(sB64 != NULL){
			fwrite(sB64, 1, uOut, stdout);   /* not null terminated */
			free(sB64);
		}
		return DAS_OKAY;
	}

	if((sVal = _csv_datumStr(pDm, sBuf, 127, nSigDig, g_sSep)) == NULL)
		return das_error(PERR, "Could not format a value");
	fputs(sVal, stdout);
	return DAS_OKAY;
}

/* Scratch for values that have to be computed rather than pointed at, grown
   to whatever DasVar_get() asks for and kept for the run */
static ubyte* g_pWork = NULL;
static size_t g_uWork = 0;

static DasErrCode _getAt(const DasVar* pVar, ptrdiff_t* pLoc, das_datum* pOut)
{
	das_byte_seq work = { g_pWork, g_uWork };
	int nRet = DasVar_get(pVar, pLoc, work, pOut);
	if(nRet > 0){
		ubyte* pNew = (ubyte*)realloc(g_pWork, (size_t)nRet);
		if(pNew == NULL)
			return das_error(PERR, "Could not allocate %d bytes of scratch", nRet);
		g_pWork = pNew;
		g_uWork = (size_t)nRet;
		work.ptr = g_pWork;
		work.sz  = g_uWork;
		nRet = DasVar_get(pVar, pLoc, work, pOut);
	}
	if(nRet != 0)
		return das_error(PERR, "Failure to get item at valid index!");
	return DAS_OKAY;
}

/* DataSet Start ************************************************************* */

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

	/* Settle the columns now, headers or not, so the value rows never have
	   to decide layout on their own */
	CsvCols* pCols = _colsOf(pDs);
	if(pCols == NULL) return PERR;

	if(!g_bHeaders)
		return DAS_OKAY;

	DasErrCode nRet = DAS_OKAY;
	int aRows[3] = {PRN_VARID, PRN_UNITS, PRN_LABEL};
	for(int r = 0; r < 3; ++r){
		if(g_bIds) printf("%d%s", iPktId, g_sSep);
		printf("\"header\"%s", g_sSep);
		if((nRet = _prnHdrRow(pCols, aRows[r])) != DAS_OKAY) return nRet;
	}
	return DAS_OKAY;
}

/* Dataset update ************************************************************ */

DasErrCode onData(StreamDesc* pSd, int iPktId, DasDs* pDs, void* pUser)
{
	/* Walk every location the buffered records cover, print a row for each,
	   then let the record varying storage go */

	CsvCols* pCols = _colsOf(pDs);
	if(pCols == NULL) return PERR;

	das_datum dm;
	DasDsIter iter;
	DasErrCode nRet = DAS_OKAY;

	for(DasDsIter_init(&iter, pDs); !iter.done; DasDsIter_next(&iter)){
		if(g_bIds) printf("%d%s", iPktId, g_sSep);
		if(g_bHeaders) printf("\"values\"%s", g_sSep);

		for(int c = 0; c < pCols->nVars; ++c){
			if(c > 0) fputs(g_sSep, stdout);

			memset(&dm, 0, sizeof(dm));
			if((nRet = _getAt(pCols->aVars[c], iter.index, &dm)) != DAS_OKAY)
				return nRet;

			int nSigDig = Units_haveCalRep(dm.units) ? g_nSecRes : g_nGenRes;
			if((nRet = _prnCells(&dm, nSigDig)) != DAS_OKAY)
				return nRet;
		}
		fputs("\r\n", stdout);
	}

	/* clean out the record varying stuff */
	size_t uCleared = DasDs_clearRagged0(pDs);
	daslog_debug_v("Cleared %zu bytes of dataset memory", uCleared);

	return DAS_OKAY;
}

/* Exceptions *************************************************************** */

DasErrCode onExcept(OobExcept* pExcept, void* vpSep)
{
	/* Can't do much here but quit with log message */
	fprintf(stderr, "Stream Exception: %s, %s\n", OobExcept_typeStr(pExcept), pExcept->sMsg);
	
	return DAS_OKAY;
}

/* ************************************************************************* */
DasErrCode onClose(StreamDesc* pSd, void* pUser)
{
	/* The column lists ride on the datasets; the datasets go with the stream */
	int nId = 0;
	DasDesc* pDesc = NULL;
	while((pDesc = DasStream_nextDesc(pSd, &nId)) != NULL){
		if(DasDesc_type(pDesc) != DATASET) continue;
		DasDs* pDs = (DasDs*)pDesc;
		free(pDs->pUser);
		pDs->pUser = NULL;
	}
	free(g_pWork);
	g_pWork = NULL;
	g_uWork = 0;
	return DAS_OKAY;
}

/* Main ********************************************************************* */

int main( int argc, char *argv[]) {

	int i = 0;
	/* int status = 0; */
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
		if(strcmp(argv[i], "-c") == 0 || strcmp(argv[i], "--no-center") == 0 ){
			g_bCenter = false;
			continue;
		}
		if(strcmp(argv[i], "-r") == 0){
			i++;
			if(i >= argc)
				return das_error(PERR, "Resolution parameter missing after -r\n");
			
			g_nGenRes = atoi(argv[i]);
			if(g_nGenRes < 2 || g_nGenRes > 18)
				return das_error(PERR, 
					"Can't format to %d significant digits, supported range is "
					"only 2 to 18 significant digits.\n", g_nGenRes
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
			
			g_nSecRes = atoi(argv[i]);
			if(g_nSecRes < 0 || g_nSecRes > 9)
				return das_error(
					PERR, "Only 0 to 9 sub-seconds digits supported don't know how to "
					"handle %d sub-second digits.", g_nSecRes
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
	
	if(g_nGenRes != 7){
		g_n4ByteWidth = g_nGenRes + 7;
		g_n8ByteWidth = g_nGenRes + 7;
	}
	
	if(g_nSecRes != 3){
		if(g_nSecRes == 0)
			g_nTimeWidth = 20;
		else
			g_nTimeWidth = 21 + g_nSecRes;
		
		sprintf(sTimeFmt, "%%04d-%%02d-%%02dT%%02d:%%02d:%%0%d.%df", 
				  g_nSecRes + 3, g_nSecRes);
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
	del_DasIO(pIn);     /* and the stream it read, datasets included */
	return nRet;
}
