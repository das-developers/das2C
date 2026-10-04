/* Copyright (C) 2026 Chris Piker <chris-piker@uiowa.edu>
 *
 * Author: C. Piker, via Claude Fable 5.1
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

/* The CDF file model: inventory, classification and the dry run listing,
   shared by the from_cdf programs.  See cdfmodel.h. */

#define _POSIX_C_SOURCE 200112L

#include <string.h>
#include <stdarg.h>
#include <inttypes.h>
#include <ctype.h>

#ifdef _WIN32
#define strcasecmp _stricmp
#define strncasecmp _strnicmp
#else
#include <strings.h>
#endif

#include <das3/form_vector.h>   /* core.h does not pull the form headers in */
#include <das3/form_geoloc.h>

#include "cdfmodel.h"

/* TT2000 fill is one less than LLONG_MIN's magnitude allows in source, so
   it is spelled as bytes. */
#ifdef HOST_IS_LSB_FIRST
static const ubyte g_tt2kfill[8] = {0,   0,0,0, 0,0,0,0x80};
#else
static const ubyte g_tt2kfill[8] = {0x80,0,0,0, 0,0,0,   0};
#endif

/* ************************************************************************* */
/* CDF status handling */

bool cdf_okayish(CDFstatus iStatus){
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
bool cdf_varAttrStr(CDFid id, long iVar, const char* sAttr, char* sBuf, size_t uLen)
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

int cdf_parsePropMap(const char* sMap)
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
		if(cdf_varAttrStr(id, iVar, g_aPropMap[i].sFrom, sBuf, uLen)){
			if(sFromAttr) strcpy(sFromAttr, g_aPropMap[i].sFrom);
			return true;
		}
	}
	for(int i = 0; g_aBuiltinMap[i].sFrom[0] != '\0'; ++i){
		if(strcmp(g_aBuiltinMap[i].sTo, sKey) != 0) continue;
		if(cdf_varAttrStr(id, iVar, g_aBuiltinMap[i].sFrom, sBuf, uLen)){
			if(sFromAttr) strcpy(sFromAttr, g_aBuiltinMap[i].sFrom);
			return true;
		}
	}
	return false;
}


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

int cdf_varIndex(const cdf_file_t* pFile, const char* sName)
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

bool cdf_isTimeType(long nType)
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

		cdf_varAttrStr(pFile->id, iVar, "VAR_TYPE", pV->sVarType, sizeof(pV->sVarType));
		for(int i = 0; i < VARIDX_MAX; ++i){
			snprintf(sAttr, sizeof(sAttr), "DEPEND_%d", i);
			cdf_varAttrStr(pFile->id, iVar, sAttr, pV->asDepend[i], CVAR_NAME_SZ);
			if(i > 0){
				snprintf(sAttr, sizeof(sAttr), "LABL_PTR_%d", i);
				cdf_varAttrStr(pFile->id, iVar, sAttr, pV->asLablPtr[i], CVAR_NAME_SZ);
			}
		}
		cdf_varAttrStr(pFile->id, iVar, "OFFSET_OF",       pV->sOffsetOf,   CVAR_NAME_SZ);
		cdf_varAttrStr(pFile->id, iVar, "DELTA_PLUS_VAR",  pV->sDeltaPlus,  CVAR_NAME_SZ);
		cdf_varAttrStr(pFile->id, iVar, "DELTA_MINUS_VAR", pV->sDeltaMinus, CVAR_NAME_SZ);
		cdf_varAttrStr(pFile->id, iVar, "DICT_KEY",        pV->sDictKey,    sizeof(pV->sDictKey));
		cdf_varAttrStr(pFile->id, iVar, "UNITS",           pV->sUnits,      sizeof(pV->sUnits));
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
static bool _isCoordLike(const cdf_var_t* pV, const cdf_select_t* pSel)
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
	if(pSel->sCoordMap[0] != '\0'){
		const char* p = pSel->sCoordMap;
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
static int _classify(cdf_file_t* pFile, const cdf_select_t* pSel)
{
	/* (1) time bases: typed, or named by --coord time:VAR */
	char sCoordTime[CVAR_NAME_SZ] = {'\0'};
	if(pSel->sCoordMap[0] != '\0'){
		const char* p = strstr(pSel->sCoordMap, "time:");
		if((p != NULL)&&((p == pSel->sCoordMap)||(p[-1] == ','))){
			p += 5;
			size_t u = 0;
			while((p[u] != '\0')&&(p[u] != ',')&&(u < CVAR_NAME_SZ - 1)){ sCoordTime[u] = p[u]; ++u; }
			sCoordTime[u] = '\0';
		}
	}
	if((sCoordTime[0] != '\0')&&(cdf_varIndex(pFile, sCoordTime) < 0))
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
		if(bNamed || (pV->bRecVary && cdf_isTimeType(pV->nType) && (pV->nDims == 0))){
			pV->role = ROLE_TIME;
			++nTimeBases;
			if(bNamed && !cdf_isTimeType(pV->nType))
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
			int iNamed = (sCoordTime[0] != '\0') ? cdf_varIndex(pFile, sCoordTime) : -1;
			if((iNamed >= 0)&&(pV->nRecs == pFile->aVars[iNamed].nRecs)){
				strncpy(pV->asDepend[0], sCoordTime, CVAR_NAME_SZ - 1);
			}
			else{
				pV->role = ROLE_IGNORE; strcpy(pV->sWhy, "no DEPEND_0");
				continue;
			}
		}
		int iDep0 = cdf_varIndex(pFile, pV->asDepend[0]);
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
	if(pSel->nVars > 0){
		for(int i = 0; i < pSel->nVars; ++i) asNames[nNames++] = pSel->asVars[i];
	}
	else if(pSel->sDefVars[0] != '\0'){
		strncpy(sDefVars, pSel->sDefVars, sizeof(sDefVars) - 1);
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
			if((sComp != NULL)&&(cdf_varIndex(pFile, sVar) < 0)){ *sComp = '\0'; ++sComp; }
			else sComp = NULL;
			int i = cdf_varIndex(pFile, sVar);
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
			if(pSel->bDataOnly && (strcmp(pV->sVarType, "data") != 0)) continue;
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
				int iDep = cdf_varIndex(pFile, sDep);
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
				int iLbl = cdf_varIndex(pFile, sLbl);
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
					int iLbl = cdf_varIndex(pFile, pV->asLablPtr[k]);
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
			int iD = cdf_varIndex(pFile, asDelta[k]);
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
			pV->bAnnot = _isCoordLike(pV, pSel);
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
bool cdf_isSequence(cdf_file_t* pFile, const cdf_var_t* pV, double* pMin, double* pStep)
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
	if(cdf_isSequence(pFile, pV, &rMin, &rStep)){
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

void cdf_listFile(cdf_file_t* pFile, const cdf_select_t* pSel)
{
	const char* sBase = strrchr(pFile->sPath, '/');
	sBase = (sBase != NULL) ? sBase + 1 : pFile->sPath;

	const das_range* pTimeRng = pSel->pTimeRng;

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
			int iDep = cdf_varIndex(pFile, pFirst->asDepend[d]);
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
			int iDep = cdf_varIndex(pFile, pFirst->asDepend[d]);
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
			int iPlus  = cdf_varIndex(pFile, pV->sDeltaPlus);
			int iMinus = cdf_varIndex(pFile, pV->sDeltaMinus);
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


/* The one line description of a variable: CATDESC, else FIELDNAM, else
   LABLAXIS, whitespace collapsed and cut to the ISTP line width */
static void _oneLiner(const cdf_file_t* pFile, const cdf_var_t* pV, char* sBuf, size_t uLen)
{
	char sRaw[2048] = {'\0'};
	if(!cdf_varAttrStr(pFile->id, pV->nVarNum, "CATDESC", sRaw, sizeof(sRaw)) || (sRaw[0] == '\0'))
		if(!cdf_varAttrStr(pFile->id, pV->nVarNum, "FIELDNAM", sRaw, sizeof(sRaw)) || (sRaw[0] == '\0'))
			cdf_varAttrStr(pFile->id, pV->nVarNum, "LABLAXIS", sRaw, sizeof(sRaw));

	size_t u = 0;
	bool bSpace = true;   /* trims leading */
	for(const char* p = sRaw; (*p != '\0') && (u + 1 < uLen); ++p){
		if((*p == ' ')||(*p == '\t')||(*p == '\n')||(*p == '\r')){
			if(!bSpace) sBuf[u++] = ' ';
			bSpace = true;
		}
		else{ sBuf[u++] = *p; bSpace = false; }
	}
	while((u > 0)&&(sBuf[u-1] == ' ')) --u;
	sBuf[u] = '\0';
	if(u >= 80) strcpy(sBuf + 77, "...");
}

/* One listing entry: the name, the components in VAR.COMPONENT spelling,
   the one liner */
static void _listVar(const cdf_file_t* pFile, const cdf_var_t* pV)
{
	char sDesc[84];
	printf("* %s", pV->sName);
	if(pV->nComps > 1){
		printf(" (");
		for(int c = 0; c < pV->nComps; ++c){
			if(c < pV->nLabels) printf("%s.%s", c ? " " : "", pV->aLabels[c]);
			else                printf("%s.%d", c ? " " : "", c);
		}
		printf(")");
	}
	_oneLiner(pFile, pV, sDesc, sizeof(sDesc));
	if(sDesc[0] != '\0') printf("  \"%s\"", sDesc);
	printf("\n");
}

/* Data variables first, then support, each in file order */
static bool _listPass(const cdf_var_t* pV, int nPass)
{
	bool bSupport = (strcmp(pV->sVarType, "support_data") == 0);
	return (nPass == 0) ? !bSupport : bSupport;
}

void cdf_listVars(cdf_file_t* pFile, int nMaxRank, const char* sDeeperTitle)
{
	const char* sBase = strrchr(pFile->sPath, '/');
	sBase = (sBase != NULL) ? sBase + 1 : pFile->sPath;
	printf("Streamable variables and components in %s\n\n", sBase);

	int nListed = 0, nDeeper = 0;
	for(int nPass = 0; nPass < 2; ++nPass){
		for(int i = 0; i < pFile->nVars; ++i){
			const cdf_var_t* pV = pFile->aVars + i;
			if(!pV->bSelected || !_listPass(pV, nPass)) continue;
			if(pV->nExtRank > nMaxRank){ ++nDeeper; continue; }
			_listVar(pFile, pV);
			++nListed;
		}
	}
	if(nListed == 0) printf("(none)\n");

	if((nDeeper > 0) && (sDeeperTitle != NULL)){
		printf("\n%s\n\n", sDeeperTitle);
		for(int nPass = 0; nPass < 2; ++nPass){
			for(int i = 0; i < pFile->nVars; ++i){
				const cdf_var_t* pV = pFile->aVars + i;
				if(pV->bSelected && _listPass(pV, nPass) && (pV->nExtRank > nMaxRank)) _listVar(pFile, pV);
			}
		}
	}

	printf("\nOther variables are present but cannot be streamed; -n lists them and why.\n");
}

/* Open a CDF, inventory and classify it.  Returns DAS_OKAY with the file
   open, or an error with it closed. */
int cdf_openAndClassify(cdf_file_t* pFile, const char* sPath, const cdf_select_t* pSel)
{
	CDFstatus nCdfStatus = CDF_OK;
	memset(pFile, 0, sizeof(cdf_file_t));
	strncpy(pFile->sPath, sPath, sizeof(pFile->sPath) - 1);

	if(CDF_MAD( CDFopenCDF((char*)sPath, &(pFile->id)) ))
		return PERR;
	CDFsetReadOnlyMode(pFile->id, READONLYon);
	pFile->nMajority = ROW_MAJOR;
	CDFgetMajority(pFile->id, &(pFile->nMajority));
	if(pFile->nMajority == COLUMN_MAJOR)
		daslog_debug_v("%s is column major; multi dimensional records are transposed on read", sPath);

	int nRet = _inventory(pFile);
	if(nRet == DAS_OKAY) nRet = _classify(pFile, pSel);
	if(nRet != DAS_OKAY){
		CDFcloseCDF(pFile->id);
		pFile->id = NULL;
	}
	return nRet;
}


/* ************************************************************************* */
/* Translation of model terms to das terms */

das_val_type cdf_valType(long nType)
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

const char* cdf_semantic(const cdf_var_t* pV)
{
	if(cdf_isTimeType(pV->nType)) return "datetime";
	das_val_type vt = cdf_valType(pV->nType);
	return das_vt_isreal(vt) ? "real" : "int";
}

/* The fill for a variable's array: FILLVAL when present and of the same
   type, else the das default for the type.  Time bases are always stored
   as TT2000 longs. */
const ubyte* cdf_varFill(cdf_file_t* pFile, const cdf_var_t* pV, ubyte* pBuf)
{
	if(cdf_isTimeType(pV->nType)) return g_tt2kfill;

	long iAttr = CDFgetAttrNum(pFile->id, (char*)"FILLVAL");
	if((iAttr >= 0)&&(CDFconfirmzEntryExistence(pFile->id, iAttr, pV->nVarNum) == CDF_OK)){
		long nType = 0, nElems = 0;
		if((CDFgetAttrzEntryDataType(pFile->id, iAttr, pV->nVarNum, &nType) == CDF_OK)&&
		   (CDFgetAttrzEntryNumElements(pFile->id, iAttr, pV->nVarNum, &nElems) == CDF_OK)&&
		   (cdf_valType(nType) == cdf_valType(pV->nType))&&(nElems == 1)){
			if(CDFgetAttrzEntry(pFile->id, iAttr, pV->nVarNum, pBuf) == CDF_OK)
				return pBuf;
		}
	}
	return (const ubyte*)das_vt_fill(cdf_valType(pV->nType));
}

/* Units for a variable.  Time bases are TT2000; empty and placeholder
   strings are dimensionless; anything the parser rejects is logged and
   made dimensionless rather than stopping the stream. */
das_units cdf_varUnits(const cdf_var_t* pV)
{
	if(cdf_isTimeType(pV->nType)) return UNIT_TT2000;
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
bool cdf_attrConsumed(const cdf_var_t* pV, const char* sAttr)
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
const char* cdf_propName(const char* sAttr)
{
	for(int i = 0; i < g_nPropMap; ++i)
		if(strcmp(g_aPropMap[i].sFrom, sAttr) == 0) return g_aPropMap[i].sTo;
	for(int i = 0; g_aBuiltinMap[i].sFrom[0] != '\0'; ++i)
		if(strcmp(g_aBuiltinMap[i].sFrom, sAttr) == 0) return g_aBuiltinMap[i].sTo;
	return sAttr;
}

/* Copy a variable's attributes onto a descriptor as properties */
int cdf_addVarProps(cdf_file_t* pFile, const cdf_var_t* pV, DasDesc* pDest)
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
		if(cdf_attrConsumed(pV, sName)) continue;

		long nType = 0;
		if(CDFgetAttrzEntryDataType(pFile->id, i, pV->nVarNum, &nType) != CDF_OK) continue;
		if(!cdf_varAttrStr(pFile->id, pV->nVarNum, sName, sVal, sizeof(sVal))) continue;
		const char* sProp = cdf_propName(sName);
		/* the frame is a form parameter, never a property */
		if(strcmp(sProp, "frame") == 0) continue;

		if((nType == CDF_CHAR)||(nType == CDF_UCHAR))
			DasDesc_setStr(pDest, sProp, sVal);
		else if(cdf_isTimeType(nType))
			DasDesc_setStr(pDest, sProp, sVal);
		else if(das_vt_isreal(cdf_valType(nType)))
			DasDesc_setDouble(pDest, sProp, atof(sVal));
		else
			DasDesc_setInt(pDest, sProp, atoi(sVal));
	}
	return DAS_OKAY;
}


/* Global attributes become properties of the descriptor given: the inverse of das3_cdf's
   global name table, then names as-is.  Multi-entry attributes join with
   newlines. */
int cdf_addGlobalProps(cdf_file_t* pFile, DasDesc* pDest)
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
			DasDesc_setStr(pDest, sProp, sVal);
		else
			DasDesc_flexSet(pDest, "stringArray", 0, sProp, sVal, '\n', NULL, 3);
	}
	return DAS_OKAY;
}


/* Structure signature: what a later file must match to reuse the datasets */
void cdf_structSig(const cdf_file_t* pFile, char* sBuf, size_t uLen)
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

/* Set a reader's window for a variable: whole dims, or one component of the
   component axis.  Returns the elements read per record. */
long cdf_initReader(const cdf_file_t* pFile, int iVar, rec_reader_t* pR)
{
	const cdf_var_t* pV = pFile->aVars + iVar;
	long nElemsPerRec = 1;
	for(int d = 0; d < pV->nDims; ++d) nElemsPerRec *= pV->aDimSz[d];

	pR->iVar = iVar;
	pR->nElemsPerRec = nElemsPerRec;
	pR->bTime = cdf_isTimeType(pV->nType);
	pR->nDims = pV->nDims;
	for(int d = 0; d < pV->nDims; ++d){
		pR->aStart[d] = ((d == pV->iCompDim)&&(pV->iCompSel >= 0)) ? pV->iCompSel : 0;
		pR->aCount[d] = pV->aDimSz[d];   /* already 1 on a selected component axis */
	}
	return nElemsPerRec;
}

/* Read a block of records of one variable into a malloc'ed buffer, through
   the reader's window (whole dims, or one component of the component axis) */
ubyte* cdf_readBlock(
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

	/* The library hands records back in the file's majority.  A column major
	   record has its FIRST dim fastest; callers index row major, so transpose
	   each record when more than one dim has extent. */
	int nWide = 0;
	for(int d = 0; d < pR->nDims; ++d) if(pR->aCount[d] > 1) ++nWide;
	if((pFile->nMajority == COLUMN_MAJOR) && (nWide > 1)){
		size_t uRec = uElemSz * pR->nElemsPerRec;
		ubyte* pTmp = (ubyte*)malloc(uRec);
		if(pTmp == NULL){ free(pBuf); return NULL; }
		long aColStride[CDF_MAX_DIMS];   /* source: first dim fastest */
		aColStride[0] = 1;
		for(int d = 1; d < pR->nDims; ++d) aColStride[d] = aColStride[d-1] * pR->aCount[d-1];
		long aIdx[CDF_MAX_DIMS];
		for(long r = 0; r < nRecs; ++r){
			ubyte* pSrc = pBuf + r * uRec;
			memset(aIdx, 0, sizeof(aIdx));
			for(long i = 0; i < pR->nElemsPerRec; ++i){
				long iCol = 0;
				for(int d = 0; d < pR->nDims; ++d) iCol += aIdx[d] * aColStride[d];
				memcpy(pTmp + i * uElemSz, pSrc + iCol * uElemSz, uElemSz);
				/* advance the row major index, last dim fastest */
				for(int d = pR->nDims - 1; d >= 0; --d){
					if(++aIdx[d] < pR->aCount[d]) break;
					aIdx[d] = 0;
				}
			}
			memcpy(pSrc, pTmp, uRec);
		}
		free(pTmp);
	}
	return pBuf;
}

