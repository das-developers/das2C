/* Copyright (C) 2024   Chris Piker <chris-piker@uiowa.edu>
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
 das3_cdf: Convert incomming das2 or das3 stream to a CDF file

   Can also issue a query to download data from a server

**************************************************************************** */

#define _POSIX_C_SOURCE 200112L

#include <stdio.h>
#include <string.h>
#include <stdbool.h>
#include <assert.h>
#include <stdlib.h>


#include <cdf.h>

#include <das2/core.h>

#define PROG "das3_cdf"
#define PERR 63

/* Handle lack of const qualifier on CDFvarNum */
#define CDFvarId(id, str) CDFgetVarNum((id), (char*) (str))

#define NEW_FILE_MODE S_IRUSR|S_IWUSR|S_IRGRP|S_IWGRP|S_IROTH

#define DEF_AUTH_FILE ".dasauth"
#define DEF_TEMP_DIR  ".dastmp"

#define LOC_PATH_LEN 256

#ifdef _WIN32
#define HOME_VAR_STR "USERPROFILE"
#else
#define HOME_VAR_STR "HOME"
#endif

/* Add a littel user-flag for arrays so we know which ones to clear after 
   a batch write */
#define DASARY_REC_VARY 0x00010000

/* Handle lack of const qualifier in cdf lib that should really be there */
#define CDFvarId(id, str) CDFgetVarNum((id), (char*) (str))
#define CDFattrId(id, str) CDFgetAttrNum((id), (char*) (str))

/* ************************************************************************* */
/* Globals */

/* The default memory threshold, don't write data to disk until a dateset is
   bigger then this (except onClose) */
size_t g_nMemBufThreshold = 16777216;   /* 16 MBytes */

#define THRESH "16 MB"

typedef struct var_cdf_info{
	long iCdfId;
	long nRecsWritten;
} var_cdf_info_t;

var_cdf_info_t* g_pVarCdfInfo;
size_t g_uMaxVars = 0;
size_t g_uNextVar = 0;

var_cdf_info_t* nextVarInfo(){
	if(g_uNextVar >= g_uMaxVars){
		das_error(PERR, "At present only %zu variables are supported in a CDF"
			" but that's easy to change.", g_uMaxVars
		);
		return NULL;
	}
	var_cdf_info_t* pRet = g_pVarCdfInfo + g_uNextVar;
	++g_uNextVar;
	return pRet;
}

#define DasVar_addCdfInfo(P)  ( (P)->pUser = nextVarInfo() )
#define DasVar_cdfId(P)       ( ( (var_cdf_info_t*)((P)->pUser) )->iCdfId)
#define DasVar_cdfIdPtr(P)   &( ( (var_cdf_info_t*)((P)->pUser) )->iCdfId)
#define DasVar_cdfStart(P)    ( ( (var_cdf_info_t*)((P)->pUser) )->nRecsWritten)
#define DasVar_cdfIncStart(P, S)  ( ( (var_cdf_info_t*)((P)->pUser) )->nRecsWritten += S)


/* ************************************************************************* */
void prnHelp()
{
	printf(
"SYNOPSIS\n"
"   " PROG " - Output das v2 and v3 streams as a CDF (Common Data Format) file\n"
"\n");

	printf(
"USAGE\n"
"   " PROG " [options] [< DAS_STREAM]\n"
"\n");

	printf(
"DESCRIPTION\n"
"   By default " PROG " reads a das2 or das3 stream from standard input and\n"
"   writes a CDF file to standard output.  Unlike other das stream processors\n"
"   " PROG " is *not* a good filter.  It does not start writing ANY output\n"
"   until ALL input is consumed.  This is unavoidable as the CDF format is\n"
"   not a streaming format.  Thus a temporary file must be created whose\n"
"   bytes are then feed to standard output.  If the end result is just to\n"
"   generate a local file anyway use the '--output' option below to avoid the\n"
"   default behavior.\n"
"\n"
"   Data values are written to CDF variables and metadata are written to CDF\n"
"   attributes.  The mapping of stream properties to CDF attributes follows.\n"
"\n"
"      <stream> Properties       -> CDF Global Attributes\n"
"      <dataset> Properties      -> CDF Global Attributes (prefix as needed)\n"
"      <coord>,<data> Properties -> CDF Variable Attributes\n"
"\n"
"   During the metadata mapping, common das3 property names are converted\n"
"   to equivalent ISTP metadata names.  A few of the mappings are:\n"
"\n"
"      title       -> TITLE\n"
"      label       -> LABLAXIS (with units stripped)\n"
"      label       -> FIELDNAM\n"
"      description -> CATDESC\n"
"      summary     -> VAR_NOTES\n"
"\n"
"   Note that if a property is named 'CDF_NAME' it is not written to the CDF\n"
"   but instead changes the name of a CDF variable.\n"
"\n"
"   Other CDF attributes are also set based on the data structure type. Some\n"
"   examples are:\n"
"\n"
"      DasVar.units -> UNITS\n"
"      DasAry.fill  -> FILLVAL\n"
"      (algorithm)  -> DEPEND_N\n"
"\n"
"   Note that if the input is a legacy das2 stream, it is upgraded internally\n"
"   to a das3 stream priror to writing the CDF file.\n"
"\n");

 
	printf(
"OPTIONS\n"
"   -h,--help     Write this text to standard output and exit.\n"
"\n"
"   -t FILE,--template=FILE\n"
"                 Initialize the output CDF with an empty template CDF file first.\n"
"                 FILE is not a CDF skeleton, but could be an empty CDF generated\n"
"                 from a skeleton file. (experimental)\n"
"\n"
"   -i URL,--input=URL\n"
"                 Instead of reading from standard input, read from this URL.\n"
"                 To read from a local file prefix it with 'file://'.  Only\n"
"                 file://, http:// and https:// are supported.\n"
"\n"
"   -o DEST,--output=DEST\n"
"                 Instead of acting as a poorly performing filter, write data\n"
"                 to this location.  If DEST is a file then data will be written\n"
"                 directly to that file. If DEST is a directory then an auto-\n"
"                 generated file name will be used. This is useful when reading\n"
"                 das servers since they provide default filenames.\n"
"\n"
"   -r,--remove   Tired of libcdf refusing to overwrite a file?  Use this option\n"
"                 with '-o'\n"
"\n"
"   -s DIR,--scratch=DIR\n"
"                 Scratch space directory for writing temporary files when run\n"
"                 as a data stream filter.  Ignored if -o is given.\n"
"\n"
"   -l LEVEL,--log=LEVEL\n"
"                 Set the logging level, where LEVEL is one of 'debug', 'info',\n"
"                 'warning', 'error' in order of decreasing verbosity.  All log\n"
"                 messages go to the standard error channel, the default is 'info\n"
"\n"
"   -c FILE,--credentials=FILE\n"
"                 Set the location where server authentication tokens (if any)\n"
"                 are saved.  Defaults to %s%s%s\n"
"\n"
"   -U MEGS,--mem-use=MEGS\n"
"                 To avoid constant writes, " PROG " buffers datasets in memory\n"
"                 until they are " THRESH " or larger and then they are written\n"
"                 to disk.  Use this parameter to change the threshold.  Using\n"
"                 a large value can increase performance for large datasets.\n"
"\n", HOME_VAR_STR, DAS_DSEPS, DEF_AUTH_FILE);


	printf(
"EXAMPLES\n"
"   1. Convert a local das stream file to a CDF file:\n"
"\n"
"      cat my_data.d3b | " PROG " -o my_data.cdf\n"
"\n"
"   2. Read from a remote das server and write data to the current directory,\n"
"      auto-generating the CDF file name:\n"
"\n"
"      " PROG " -i https://college.edu/mission/inst?beg=2014&end=2015 -o ./\n"
"\n");

	printf(
"AUTHOR\n"
"   chris-piker@uiowa.edu\n"
"\n");

	printf(
"SEE ALSO\n"
"   * das3_node\n"
"   * Wiki page https://github.com/das-developers/das2C/wiki/das3_cdf\n"
"   * ISTP CDF guidelines: https://spdf.gsfc.nasa.gov/istp_guide/istp_guide.html\n"
"\n");
}

/* ************************************************************************* */

/* See a given string matches the short or long form of an argument */
static int _isArg(
	const char* sArg, const char* sShort, const char* sLong, bool* pLong 
){
	if(strcmp(sArg, sShort) == 0){
		if(pLong) *pLong = false;
		return true;
	}
	size_t uLen = strlen(sLong);  /* Include the '=' */

	if(strncmp(sArg, sLong, uLen) == 0){
		if(pLong) *pLong = true;
		return true;
	}
	return false;
}

/* Get the value of an argument if the current entry in argv matches
 * either the short or long form
 */
static bool _getArgVal(
	char* sDest, size_t uDest, char** argv, int argc, int* pArgIdx, 
	const char* sShort, const char* sLong
){
	bool bIsLong = false;

	if(! _isArg(argv[*pArgIdx], sShort, sLong, &bIsLong))
		return false;
	
	const char* sVal;
	if(bIsLong){
		size_t uLongSz = strlen(sLong);
		if(argv[*pArgIdx][uLongSz] == '\0') /* Nothing in str after prefix */
			goto NO_ARG;

		sVal = argv[*pArgIdx] + uLongSz;
	}
	else{
		if(*pArgIdx > argc-2) /* Ain't no subsequent arg */
			goto NO_ARG;

		sVal = argv[*pArgIdx+1];
		++(*pArgIdx);
	}

	strncpy(sDest, sVal, uDest - 1);
	return true;
	
NO_ARG:
	das_error(PERR, "Missing option after argument %s", argv[*pArgIdx]);
	return false;
}

typedef struct program_optitons{
	bool bRmFirst;
	char aTpltFile[256]; /* Template CDF */
	char aSource[1024];  /* Input source, http://, file:// etc. */
	char aOutFile[256];  /* Non-filter: output */
	char aTmpDir[256];   /* Filter mode: temp dir */
	char aLevel[32];    
	char aCredFile[256];
} popts_t;


#define FIELD_SZ(type, field)  (sizeof(((type*)NULL)->field)) 

int parseArgs(int argc, char** argv, popts_t* pOpts)
{
	memset(pOpts, 0, sizeof(popts_t));
	pOpts->bRmFirst = false;

	char sMemThresh[32] = {'\0'};

	/* Set a few defaults */
	snprintf(
		pOpts->aCredFile, FIELD_SZ(popts_t, aCredFile) - 1, "%s" DAS_DSEPS DEF_AUTH_FILE,
		das_userhome()
	);

	strcpy(pOpts->aLevel, "info");

	snprintf(
		pOpts->aTmpDir, FIELD_SZ(popts_t, aTmpDir) - 1, "%s" DAS_DSEPS ".cdftmp",
		das_userhome()
	);

	int i = 0;
	while(i < (argc-1)){
		++i;  /* 1st time, skip past the program name */

		if(argv[i][0] == '-'){
			if(_isArg(argv[i], "-h", "--help", NULL)){
				prnHelp();
				exit(0);
			}
			if(_isArg(argv[i], "-r", "--remove", NULL)){
				pOpts->bRmFirst = true;
				continue;
			}
			if(_getArgVal(
				sMemThresh,       32,                          argv, argc, &i, "-U", "--mem-use="
			))
				continue;
			if(_getArgVal(
				pOpts->aTpltFile, FIELD_SZ(popts_t,aTpltFile), argv, argc, &i, "-t", "--template="
			))
				continue;
			if(_getArgVal(
				pOpts->aSource,   FIELD_SZ(popts_t,aSource),   argv, argc, &i, "-i", "--input="
			))
				continue;
			if(_getArgVal(
				pOpts->aOutFile,  FIELD_SZ(popts_t,aOutFile),  argv, argc, &i, "-o", "--output="
			))
				continue;
			if(_getArgVal(
				pOpts->aTmpDir,   FIELD_SZ(popts_t,aTmpDir),   argv, argc, &i, "-s", "--scratch="
			))
				continue;
			if(_getArgVal(
				pOpts->aLevel,    FIELD_SZ(popts_t,aLevel),    argv, argc, &i, "-l", "--log="
			))
				continue;
			if(_getArgVal(
				pOpts->aCredFile, FIELD_SZ(popts_t,aCredFile), argv, argc, &i, "-c", "--credentials="
			))
				continue;
			return das_error(PERR, "Unknown command line argument %s", argv[i]);
		}
		return das_error(PERR, "Malformed command line argument %s", argv[i]);
	}

	float fMemUse;
	if(sMemThresh[0] != '\0'){
		if((sscanf(sMemThresh, "%f", &fMemUse) != 1)||(fMemUse < 1)){
			return das_error(PERR, "Invalid memory usage argument, '%s' MB", sMemThresh);
		}
		else{
			g_nMemBufThreshold = (size_t)fMemUse;
		}
	}

	return DAS_OKAY;
}

/* ************************************************************************* */

struct context {
	CDFid nCdfId;
	char* sTpltFile;  /* An empty template CDF to put data in */
	char* sWriteTo;
	/* DasTime dtBeg; */      /* Start point for initial query, if known */
	/* double rInterval; */   /* Size of original query, if known */

	uint32_t uFlushSz;   /* How big to let internal memory grow before a CDF flush */
};

/* sending CDF message to the log ****************************************** */

bool _cdfOkayish(CDFstatus iStatus){
	char sMsg[CDF_ERRTEXT_LEN+1];

	/* Wrapper should have prevented this, but in case it's called directly */
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

/* Use a macro to avoid unneccessary functions calls that slow the program */
#define _OK( SOME_CDF_FUNC ) ( ((iStatus = (SOME_CDF_FUNC) ) == CDF_OK) || (_cdfOkayish(iStatus)) )


/* ************************************************************************* */
/* Converting Das Properties to CDF properties */

/* Buffers for property conversion */

#define PROP_XFORM_SZ 65536  /* 64K */

ubyte g_propBuf[PROP_XFORM_SZ];

const char* DasProp_cdfName(const DasProp* pProp)
{
	/* Translate some of the common das property names to CDF names */
	const char* sName = DasProp_name(pProp);
	if(strcmp(sName, "label") == 0) return "FIELDNAM";
	if(strcmp(sName, "description") == 0) return "CATDESC";
	if(strcmp(sName, "title") == 0) return "TITLE";
	if(strcmp(sName, "summary") == 0) return "VAR_NOTES";
	if(strcmp(sName, "info")    == 0) return "VAR_NOTES";

	return sName;
}

/* Get the number of entries for a property.  Only global properties are
   allowed to have multiple entries.  Typically only string data are 
   allowed to have entries.  We'll interpret this to be 1 except for long
   string values.  In that case each blank line will be interpreted to 
   start a new entry.
 */
long DasProp_cdfEntries(const DasProp* pProp)
{
	if(!(DasProp_type(pProp) & DASPROP_STRING))
		return 1;

	/* Count seperators.  If sep is just '\0' then return 1. */
	char cSep = DasProp_sep(pProp);
	if(cSep == '\0')
		return 1;
	
	long nEntries = 1;

	const char* pRead = DasProp_value(pProp);
	while(*pRead != '\0'){
		if(*pRead == cSep) ++nEntries;
		++pRead;
	}
	return nEntries;
}

long DasProp_cdfType(const DasProp* pProp)
{
	ubyte uType = DasProp_type(pProp) & DASPROP_TYPE_MASK;
	switch(uType){
	case DASPROP_STRING: return CDF_UCHAR;

	/* Properties don't have fill, so an unsigned byte works */
	case DASPROP_BOOL:   return CDF_UINT1;

	case DASPROP_INT:    return CDF_INT8; /* just to be safe */
	case DASPROP_REAL:   return CDF_DOUBLE;
	case DASPROP_DATETIME: return CDF_TIME_TT2000;
	default:
		assert(false);  /* Dectects das2C lib changes */
	}
	return 0;
}

long DasProp_cdfEntLen(const DasProp* pProp, long iEntry)
{
	/* Non-strings only have one entry */
	if(!(DasProp_type(pProp) & DASPROP_STRING)){
		if(iEntry == 0)
			return DasProp_items(pProp);
		else
			return 0;
	}

	const char* pRead = DasProp_value(pProp);

	/* Strings that don't have a special separator only have one entry */
	char cSep = DasProp_sep(pProp);
	if(cSep == '\0'){
		if(iEntry == 0)
			return strlen(pRead);
		else
			return 0;
	}

	/* Get the length between separators */
	/* I am a multi-entry string, get the length form seperator
	   iEntry, until iEntry + 1 */
	int iSep = 0;
	int iLastSepPos = -1;
	int i = 0;
	while(*pRead != '\0'){
		if(*pRead == cSep){
			if(iSep == iEntry){
				return i - iLastSepPos - 1;
			}
			++iSep;
			iLastSepPos = i;
		}
		++pRead;
		++i;
	}
	return 0;
}

void* DasProp_cdfValues(const DasProp* pProp){
	/* For strings this is easy, others have to be parsed */
	if(DasProp_type(pProp) & DASPROP_STRING){
		const char* sValue = DasProp_value(pProp);
		if((sValue == NULL)||(sValue[0] == '\0'))
			sValue = " ";
		return (void*) sValue;
	}

	size_t uBufLen = 0;

	ubyte uType = DasProp_type(pProp) & DASPROP_TYPE_MASK;
	switch(uType){

	/* Properties don't have fill, so an unsigned byte works */
	case DASPROP_BOOL:
		uBufLen = PROP_XFORM_SZ;
		if(DasProp_convertBool(pProp, g_propBuf, uBufLen) != DAS_OKAY)
			return NULL;
		else
			return g_propBuf;

	case DASPROP_INT:
		uBufLen = PROP_XFORM_SZ / sizeof(int64_t);
		if(DasProp_convertInt(pProp, (int64_t*)g_propBuf, uBufLen) != DAS_OKAY)
			return NULL;
		else
			return g_propBuf;

	case DASPROP_REAL:     
		uBufLen = PROP_XFORM_SZ / sizeof(double);
		if(DasProp_convertReal(pProp, (double*)g_propBuf, uBufLen) != DAS_OKAY)
			return NULL;
		else
			return g_propBuf;
		
	case DASPROP_DATETIME:
		uBufLen = PROP_XFORM_SZ / sizeof(int64_t);
		if(DasProp_convertTt2k(pProp, (int64_t*)g_propBuf, uBufLen) != DAS_OKAY)
			return NULL;
		else
			return g_propBuf;
	default:
		assert(false); /* detect das2C library changes */
	}
	return NULL;
}

void* DasProp_cdfEntValues(const DasProp* pProp, long iEntry){
	if(!(DasProp_type(pProp) & DASPROP_STRING)){
		if(iEntry == 0)
			return DasProp_cdfValues(pProp);
		else
			return NULL;
	}

	char cSep = DasProp_sep(pProp);
	if(cSep == '\0'){
		if(iEntry == 0)
			return (void*)DasProp_value(pProp);
		else
			return NULL;
	}

	/* Get the length between separators */
	/* I am a multi-entry string, get the length form seperator
	   iEntry, until iEntry + 1 */
	const char* pRead = DasProp_value(pProp);
	const char* pEntry = NULL;
	int iSep = 0;
	int iLastSepPos = -1;
	int i = 0;
	while(*pRead != '\0'){
		if(*pRead == cSep){
			if(iSep == iEntry){
				pEntry = DasProp_value(pProp) + iLastSepPos + 1;
				return (void*)pEntry;
			}
			++iSep;
			iLastSepPos = i;
		}
		++pRead;
		++i;
	}
	return NULL;
}

DasErrCode writeGlobalProp(CDFid iCdf, const DasProp* pProp)
{	
	CDFstatus iStatus = CDF_OK; /* Also used by _OK macro */

	const char* sName = NULL;
	long iAttr = 0;

	long n = DasProp_cdfEntries(pProp);
	for(long iEntry = 0; iEntry < n; ++iEntry){

		sName = DasProp_cdfName(pProp);
	
		/* Get attribute number or make a new (why can't CDFlib use "const", 
		   is it really so hard? */
		if((iAttr = CDFgetAttrNum(iCdf, (char*)sName)) <= 0){
			if(!_OK(CDFcreateAttr(iCdf, sName, GLOBAL_SCOPE, &iAttr)))
				return PERR;
		}

		iStatus = CDFputAttrgEntry(
			iCdf, 
			iAttr,
			iEntry, 
			DasProp_cdfType(pProp),
			DasProp_cdfEntLen(pProp, iEntry),
			DasProp_cdfEntValues(pProp, iEntry)
		);
		if(!_cdfOkayish(iStatus))
			return PERR;
	}

	return DAS_OKAY;
}

DasErrCode writeVarProp(CDFid iCdf, long iVarNum, const DasProp* pProp)
{
	CDFstatus iStatus; /* Used by _OK macro */

	/* If the attribute doesn't exist, we'll need to create it first */
	long iAttr;

	const char* sName = DasProp_cdfName(pProp);

	if((iAttr = CDFattrId(iCdf, sName)) < 0){
		if(! _OK(CDFcreateAttr(iCdf,sName,VARIABLE_SCOPE,&iAttr)))
			return PERR;
	}

	if(!_OK(CDFputAttrzEntry(
		iCdf, 
		iAttr,
		iVarNum,
		DasProp_cdfType(pProp),
		(long) DasProp_items(pProp),
		DasProp_cdfValues(pProp)
	)))
		return PERR;

	return DAS_OKAY;
}

DasErrCode writeVarStrAttr(
	CDFid iCdf, long iVarNum, const char* sName, const char* sValue
){
	CDFstatus iStatus; /* Used by _OK macro */

	/* CDF doesn't like empty strings, and prefers a single space
	   instead of a zero length string */
	if((sValue == NULL)||(sValue[0] == '\0'))
		sValue = " "; 

	/* If the attribute doesn't exist, we'll need to create it first */
	long iAttr;

	if((iAttr = CDFattrId(iCdf, sName)) < 0){
		if(! _OK(CDFcreateAttr(iCdf, sName, VARIABLE_SCOPE, &iAttr )))
			return PERR;
	}

	if(! _OK(CDFputAttrzEntry(
		iCdf,
		iAttr,
		iVarNum,
		CDF_CHAR, 
		(long) strlen(sValue),
		(void*)sValue
	)))
		return PERR;
	else
		return DAS_OKAY;
}

/* ************************************************************************* */

DasErrCode onStream(StreamDesc* pSd, void* pUser){
	struct context* pCtx = (struct context*)pUser;

	daslog_info_v("Writing to: %s", pCtx->sWriteTo);

	CDFstatus iStatus = CDF_OK; /* needed by the _OK() macro */ 

	/* CDF irritating oddity, halt the file name at the dot */
	char* pDot = strrchr(pCtx->sWriteTo, '.');
	
	/* Open the file since we have something to write */
	if(pCtx->sTpltFile[0] != '\0'){
		/* Copy in skeleton and open that or... */
		if(!das_copyfile(pCtx->sTpltFile, pCtx->sWriteTo, NEW_FILE_MODE)){
			das_error(PERR, "Couldn't open copy '%s' --to--> '%s'", 
				pCtx->sTpltFile, pCtx->sWriteTo
			);
			return PERR;
		}

		if(pDot != NULL) *pDot = '\0';

		if(!_OK( CDFopenCDF(pCtx->sWriteTo, &(pCtx->nCdfId))) ){
			*pDot = '.'; /* Convert back */
			return das_error(PERR, "Couldn't open CDF file '%s'", pCtx->sWriteTo);
		}
	}
	else{
		/* Create a new file */
		if(pDot != NULL) *pDot = '\0';

		if(!_OK( CDFcreateCDF(pCtx->sWriteTo, &(pCtx->nCdfId)) ) ){
			*pDot = '.'; /* Convert back */
			return das_error(PERR, "Couldn't open CDF file '%s'", pCtx->sWriteTo);
		}
	}

	if(pDot != NULL) *pDot = '.';  /* But our damn dot back */

	/* We have the file, run in our properties */
	size_t uProps = DasDesc_length((DasDesc*)pSd);
	for(size_t u = 0; u < uProps; ++u){
		const DasProp* pProp = DasDesc_getPropByIdx((DasDesc*)pSd, u);
		if(pProp == NULL) continue;

		/* Some properties are meta-data controllers */
		if(strcmp(DasProp_name(pProp), "CDF_NAME") == 0)
			continue; 

		if(writeGlobalProp(pCtx->nCdfId, pProp) != DAS_OKAY)
			return PERR;
	}

	/* If there are any coordinate frames defined in this stream, say 
	   something about them here */
	if(StreamDesc_getNumFrames(pSd) > 0){
		daslog_error("TODO: Write stream vector frame info to CDF attributes.");
	}
	
	return DAS_OKAY;
}

/* ************************************************************************* */
/* Dependency Solver:
   
   ISTP CDFs like to associate one physical dimension with one array index, and 
   even one plot axis.  In fact it's one of thier definining limitations. Das3
   datasets do not fall into this trap, and instead de-couple array indexes from
   both physical dimensions and plotting axes. But CDFs are the "law of the land".
   So, to try and fit into ISTP constraints we have... The Dependency Solver.
   --cwp

   Guiding principles:

      1. There must be one dependency for each index (aka array dimension)

      2. Associate a different physDim with each dependency.

      3. Choose time before other physDims if it's an option.

      4. Only point variables and offsets may be selected as dependencies.

   Example, Rank 3 MARSIS Sounder Data (ignoring light time for signal bounce):

     Dimension      Role     Index Map  Type
     ---------   ---------   ---------  ----
     time        reference    0  -  -   Ary
     time        offset       -  0      Seq
     altitude    reference    0  -  -   Ary
     altitude    offset       -  -  0   Seq
     frequency   center       -  0  -   Seq

   Now sort the data from lowest used index to highest.  When two variables
   have the same sort order, decide based on the dimension name, but don't
   reuse a dimension name unless there is no other choice.

	  Dimension      Role     Index Map  Type
     ---------   ---------   ---------  ----
     time        reference    0  -  -   Ary   --> Depend 0
     altitude    reference    0  -  -   Ary
     frequency   center       -  0  -   Seq   --> Depend 1
     time        offset       -  0  -   Seq
     altitude    offset       -  -  0   Seq   --> Depend 2
     
   Next, check for variable un-rolling.  Variables can only be unrolled if:

      1. It is a depend > 0
      2. You find a reference and offset pair in the same dimension.
      3. It's partner is not already marked as a depend

	After collapse we get this:

	  Dimension      Role     Index Map  Type
     ---------   ---------   ---------  ----
     time        reference    0  -  -   Ary   --> Depend 0
     frequency   center       -  0  -   Seq   --> Depend 1
     time        offset       -  0  -   Seq
     altitude    center       0  -  0   BinOp --> Depend 2 (has Depend 0)

	After collapse, generate the corresponding CDF variables.
*/

/* Default sort order for dimensions */

struct dep_dim_weight {
	const char* sDim;
	bool bUsed;
};

struct dep_dim_weight g_weights[] = {
	{"time", false},      {"altitude", false}, 
	{"frequency", false}, {"energy", false}, 
	{"", false}
};

void _resetWeights(){
	struct dep_dim_weight* pWeight = g_weights;

	while(pWeight->sDim[0] != '\0'){
		pWeight->bUsed = false;
		++pWeight;
	}
}

typedef struct cdf_var_info {
	bool bCoord;                 /* True if this is a coordinate */
	int iDep;                    /* The dependency this satisfies (if any) */
	char sDim[DAS_MAX_ID_BUFSZ]; /* The generic name of the dim */
	DasDim* pDim;                /* the associated physical dimension */
	char sRole[DASDIM_ROLE_SZ];  /* It's role in the physical dimension */
	DasVar* pVar;                /* The actual dasvar */
	int iMaxIdx;                 /* The maximum valid external index for this var */
	ptrdiff_t aVarShape[DASIDX_MAX];    /* This var's overall dataset shape */
	char sCdfName[DAS_MAX_ID_BUFSZ];  /* The name this variable has in the CDF */
} VarInfo;

/* The CDFid of the variable is saved as the value of DasVar->pUser
   (basically an integer is saved in a pointer)
*/

VarInfo* VarInfoAry_getDepN(VarInfo* pList, size_t uLen, int iDep)
{
	for(size_t u = 0; u < uLen; ++u){
		if((pList + u)->iDep == iDep)
			return (pList + u);
	}
	return NULL;
}

VarInfo* VarInfoAry_getByRole(
	VarInfo* pList, size_t uLen, const DasDim* pDim, const char* sRole
){
	for(size_t u = 0; u < uLen; ++u){
		if(((pList+u)->pDim == pDim) && (strcmp((pList+u)->sRole, sRole) == 0))
			return (pList + u);
	}
	return NULL;
}

int _maxIndex(const ptrdiff_t* pShape){ /* Implicit length DASIDX_MAX */
	int iMaxIndex = -1;
	for(int i = 0; i < DASIDX_MAX; ++i)
		if(pShape[i] != DASIDX_UNUSED) iMaxIndex = i;
	assert(iMaxIndex >= 0);
	return iMaxIndex;
}

int _usedIndexes(const ptrdiff_t* pShape){ /* Implicit length DASIDX_MAX */
	int nUsed = 0;
	for(int i = 0; i < DASIDX_MAX; ++i)
		if(pShape[i] >= 0) ++nUsed;
	return nUsed;
}

int cdf_var_info_cmp(const void* vpVi1, const void* vpVi2)
{
	const VarInfo* pVi1 = (const VarInfo*)vpVi1;
	const VarInfo* pVi2 = (const VarInfo*)vpVi2;
	
	int nMax1 = _maxIndex(pVi1->aVarShape);
	int nMax2 = _maxIndex(pVi2->aVarShape);

	if(nMax1 != nMax2)
		return (nMax1 > nMax2) ? 1 : -1;  /* lowest max index is first */
	
	/* Have to resolve two matching at once */
	const char* sDim1 = pVi1->sDim;
	const char* sDim2 = pVi1->sDim;

	struct dep_dim_weight* pWeight = g_weights;

	while(pWeight->sDim[0] != '\0'){
		if(pWeight->bUsed){
			++pWeight;
			continue;
		}

		bool bDim1Match = (strcmp(sDim1, pWeight->sDim) == 0);
		bool bDim2Match = (strcmp(sDim2, pWeight->sDim) == 0);

		if(bDim1Match != bDim2Match){
			if(bDim1Match) return -1;  /* Put matches first */
			else return 1;
			pWeight->bUsed = true;
		}
		++pWeight;
	}

	/* Same max index and both (or neither) match a prefered axis 
	   go with the one with the fewest number of used indexes */
	int nUsed1 = _usedIndexes(pVi1->aVarShape);
	int nUsed2 = _usedIndexes(pVi2->aVarShape);

	if( nUsed1 != nUsed2)
		return (nUsed1 > nUsed2) ? 1 : -1; /* lest number of used indexes is first */

	return 0;  /* Heck I don't know at this point */
}

VarInfo* solveDepends(DasDs* pDs, size_t* pNumCoords)
{
	ptrdiff_t aDsShape[DASIDX_MAX] = DASIDX_INIT_UNUSED;
	int nDsRank = DasDs_shape(pDs, aDsShape);

	/* (1) Gather the array shapes ************* */

	size_t uD, uCoordDims = DasDs_numDims(pDs, DASDIM_COORD);
	size_t uExtra = 0;
	size_t uCoords = 0;
	for(uD = 0; uD < uCoordDims; ++uD){	
		const DasDim* pDim = DasDs_getDimByIdx(pDs, uD, DASDIM_COORD);
		uCoords += DasDim_numVars(pDim);
		if(DasDim_getVar(pDim, DASVAR_REF) &&
			DasDim_getVar(pDim, DASVAR_OFFSET) &&
			! DasDim_getVar(pDim, DASVAR_CENTER)
		)
			++uExtra; /* Make space for binary vars we might create */
	}

	VarInfo* aVarInfo = (VarInfo*) calloc(uCoords + uExtra, sizeof(VarInfo));

	size_t uInfos = 0;
	for(uD = 0; uD < uCoordDims; ++uD){
		DasDim* pDim = (DasDim*)DasDs_getDimByIdx(pDs, uD, DASDIM_COORD);
			
		size_t uVars = DasDim_numVars(pDim);
		for(size_t uV = 0; uV < uVars; ++uV){

			VarInfo* pVi = aVarInfo + uInfos;
			pVi->bCoord = true;
			pVi->iDep = -1; /* not assigned to a dim yet */
			pVi->pDim = pDim;
			pVi->pVar = (DasVar*)DasDim_getVarByIdx(pDim, uV);
			strncpy(pVi->sDim, DasDim_id(pDim), DAS_MAX_ID_BUFSZ-1);
			const char* sRole = DasDim_getRoleByIdx(pDim, uV);
			if(sRole!=NULL)
			strncpy(pVi->sRole, sRole, DASDIM_ROLE_SZ-1);

			DasVar_shape(pVi->pVar, pVi->aVarShape);
			pVi->iMaxIdx = _maxIndex(pVi->aVarShape);

			++uInfos;
		}
	}

	/* (2) sort them by least to highest max index then by named dimensions */
	_resetWeights();
	qsort(aVarInfo, uInfos, sizeof(VarInfo), cdf_var_info_cmp);


	/* (3) Assign variables as dependencies */
	int iDep = 0;
	int nAssigned = 0;
	for(size_t u = 0; u < uInfos; ++u){
		if(aVarInfo[u].iMaxIdx == iDep){
			aVarInfo[u].iDep = iDep;   /* This var is now a dependency */
			++nAssigned;
			++iDep;
		}
	}

	if(nAssigned != nDsRank){
		das_error(PERR, "Dataset not convertable to CDF.  The dataset "
			"is rank %d, but it only has %d unique coordinate variables.",
			nDsRank, nAssigned
		);
		return NULL;
	}

	/* (4) Substitute unrolled reference + offset variables */

	for(int iDep = 1; iDep < nDsRank; ++iDep){
		VarInfo* pViOff = VarInfoAry_getDepN(aVarInfo, uInfos, iDep);
		assert(pViOff != NULL);

		if(strcmp(pViOff->sRole, DASVAR_OFFSET) != 0)
			continue;

		/* We want the associated reference... */
		VarInfo* pViRef = VarInfoAry_getByRole(aVarInfo, uInfos, pViOff->pDim, DASVAR_REF);

		/* ... but skip creation if a center var already exists */
		VarInfo* pViCent = VarInfoAry_getByRole(aVarInfo, uInfos, pViOff->pDim, DASVAR_CENTER);

		if((pViRef == NULL)||(pViCent != NULL))
			continue;
		
		/* Make a new variable combining the reference and the offest and
		   substitue this in for the dependency. */
		VarInfo* pViNew = (aVarInfo + uInfos);
		pViNew->bCoord = true;
		pViNew->pVar = new_DasVarBinary(DASVAR_CENTER, pViRef->pVar, "+", pViOff->pVar);
		
		/* Give the new var to the dimension */
		DasDim_addVar(pViOff->pDim, DASVAR_CENTER, pViNew->pVar);
		pViNew->pDim = pViOff->pDim;
		
		strncpy(pViNew->sDim,  pViOff->sDim, DAS_MAX_ID_BUFSZ - 1);
		strncpy(pViNew->sRole, DASVAR_CENTER, DASDIM_ROLE_SZ - 1);

		DasVar_shape(pViNew->pVar, pViNew->aVarShape);
		pViNew->iMaxIdx = _maxIndex(pViNew->aVarShape);


		pViNew->iDep = pViOff->iDep;  /* Give dep role to new variable */
		pViOff->iDep = -1; 
		++uInfos;
	}

	*pNumCoords = uInfos;
	return aVarInfo;
}

/* ************************************************************************* */
/* Converting DasVars to CDF Vars */

long DasVar_cdfType(const DasVar* pVar)
{

	/* WARNING: Update if das_val_type_e changes */
	static const long aCdfType[] = {
		0,         /* vtUnknown  = 0 */
		CDF_UINT1, /* vtUByte    = 1 */ 
		CDF_INT1,  /* vtByte     = 2 */
		CDF_UINT2, /* vtUShort   = 3 */
		CDF_INT2,  /* vtShort    = 4 */
		CDF_UINT4, /* vtUInt     = 5 */
		CDF_INT4,  /* vtInt      = 6 */
		0,         /* vtULong    = 7 */  /* CDF doesn't have this */
		CDF_INT8,  /* vtLong     = 8 */
		CDF_REAL4, /* vtFloat    = 9 */
		CDF_REAL8, /* vtDouble   = 10 */
		CDF_TIME_TT2000, /* vtTime = 11 */
		0,         /* vtIndex    = 12 */
		CDF_UCHAR, /* vtText     = 13 */
		0,         /* vtGeoVec   = 14 */
		CDF_UINT1, /* vtByteSeq  = 15 */
	};

	/* If the units of the variable are any time units, return a type of tt2k */
	if((DasVar_units(pVar) == UNIT_TT2000)&&(DasVar_valType(pVar) == vtLong))
		return CDF_TIME_TT2000;
	else
		return aCdfType[DasVar_valType(pVar)];
}

const char* DasVar_cdfName(
	const DasDim* pDim, const DasVar* pVar, char* sBuf, size_t uBufLen
){
	assert(uBufLen > 8);

	const char* sRole = NULL;
	for(size_t u = 0; u < pDim->uVars; ++u){
		if(pDim->aVars[u] == pVar){
			sRole = pDim->aRoles[u];
			break;
		}
	}

	if(sRole == NULL){
		das_error(PERR, "Couldn't find var 0x%zx in dimension %s", pVar, DasDim_id(pDim));
		return NULL;
	}

	/* If this dim has a CDF_NAME property, then use it for the role */
	const DasProp* pOverride = DasDesc_getLocal((DasDesc*)pDim, "CDF_NAME");
	if(pOverride){
		sRole = DasProp_value(pOverride);
	}

	/* If I'm the point var, don't adorn the name with the role */
	const DasVar* pPtVar = DasDim_getPointVar(pDim);
	if(pPtVar == pVar){
		if((pDim->dtype == DASDIM_COORD)&&(strcmp(DasDim_dim(pDim), "time") == 0))
			strncpy(sBuf, "Epoch", uBufLen - 1);
		else
			snprintf(sBuf, uBufLen - 1, "%s", DasDim_id(pDim));
	}
	else
		snprintf(sBuf, uBufLen - 1, "%s_%s", DasDim_id(pDim), sRole);

	return sBuf;
}

/* Sequences pour themselves into the shape of the containing dataset
   so the dataset shape is needed here */
long DasVar_cdfNonRecDims(
	int nDsRank, ptrdiff_t* pDsShape, const DasVar* pVar, long* pNonRecDims
){
	ptrdiff_t aShape[DASIDX_MAX] = {0};

	DasVar_shape(pVar, aShape);
	long nUsed = 0;
	for(int i = 1; i < nDsRank; ++i){
		if(aShape[i] == DASIDX_RAGGED)
			return -1 * das_error(PERR, 
				"Ragged indexes in non-record indexes are not supported by CDFs"
			);
		
		if(aShape[i] != DASIDX_UNUSED){
			if(aShape[i] < 1){
				if(pDsShape[i] < 1){
					return -1 * das_error(
						PERR, "Ragged datasets with sequences are not yet supported"
					);
				}
				pNonRecDims[nUsed] = pDsShape[i];
			}
			else
				pNonRecDims[nUsed] = aShape[i];
			++nUsed;
		}
	}
	return nUsed;
}

DasErrCode makeCdfVar(
	CDFid nCdfId, DasDim* pDim, DasVar* pVar, int nDsRank, ptrdiff_t* pDsShape,
	char* sNameBuf
){
	ptrdiff_t aMin[DASIDX_MAX] = {0};
	ptrdiff_t aMax[DASIDX_MAX] = {0};

	ptrdiff_t aIntr[DASIDX_MAX] = {0};
	int nIntrRank = DasVar_intrShape(pVar, aIntr);

	long aNonRecDims[DASIDX_MAX] = {0};
	/* Sequence variables mold themselvse to the shape of the containing dataset so
	   the dataset shape has to be passed in a well */
	long nNonRecDims = DasVar_cdfNonRecDims(nDsRank, pDsShape, pVar, aNonRecDims);
	if(nNonRecDims < 0)
		return PERR;

	/* Create the varyances array.  
	 *
	 * The way CDFs were meant to be used (see sec. 2.3.11 in the CDF Users Guide)
	 * the VARY flags would would have mapped 1-to-1 to DasVar_degenerate() calls.
	 * However the people who invented the ISTP standards took a different route 
	 * with the "DEPEND_N" concept, which isn't as fexible.  In that concept, all
	 * non-varying variables were kinda expected to be 1-D, and "data" variables are
	 * expected to be cubic, So the VARY's collapse. This is unfortunate as DEPEND_N
	 * is not as flexible.   -cwp
	 
   // What the code should be ...
   
	long aVaries[DASIDX_MAX] = {
		NOVARY, NOVARY, NOVARY, NOVARY,  NOVARY, NOVARY, NOVARY, NOVARY
	};
	for(int i = 0; i < DASIDX_MAX; ++i){
		if(!DasVar_degenerate(pVar,i))
			aVaries[i] = VARY;
	}

	// ... but what it is */

	long nRecVary = NOVARY;
	long aDimVary[DASIDX_MAX - 1] = {NOVARY,NOVARY,NOVARY,NOVARY,NOVARY,NOVARY,NOVARY};

	int j = 0;
	for(int i = 0; i < DASIDX_MAX; ++i){
		if(DasVar_degenerate(pVar, i))
			continue;
		if(i == 0){ 
			nRecVary = VARY;
		}
		else{
			aDimVary[j] = VARY;
			++j;
		}
	}

	/* Attach a small var_cdf_info_t struct to the variable to track the 
	   variable ID as well as the last written record index */
	DasVar_addCdfInfo(pVar);

	/* add the variable's name */
	DasVar_cdfName(pDim, pVar, sNameBuf, DAS_MAX_ID_BUFSZ - 1);

	CDFstatus iStatus = CDFcreatezVar(
		nCdfId,                                     /* CDF File ID */
		sNameBuf,                                   /* Varible's name */
		DasVar_cdfType(pVar),                       /* CDF Data type of variable */
		(nIntrRank > 0) ? (long) aIntr[0] : 1L,     /* Character length, if needed */
		nNonRecDims,                                /* collapsed rank after index 0 */
		aNonRecDims,                                /* collapsed size in each index, after 0 */
		nRecVary,                                   /* True if varies in index 0 */
		aDimVary,                                   /* Array of varies for index > 0 */
		DasVar_cdfIdPtr(pVar)                       /* The ID of the variable created */
	);
	if(!_cdfOkayish(iStatus))
		return PERR;

	/* If the is a record varying varible and it has an array, mark that
	   array as one we'll clear after each batch of data is written */
	if(nRecVary == VARY){
		if(DasVar_type(pVar) == D2V_ARRAY){
			DasAry* pAry = DasVarAry_getArray(pVar);
			DasAry_setUsage(pAry, DasAry_getUsage(pAry) | DASARY_REC_VARY );
		}

		return DAS_OKAY;  /* Done with rec-varying variables */
	}


	/* Looks like it's not record varying, go ahead and write it now.... */
	
	/* We have a bit of a problem here.  DasVar works hard to make sure
	   you never have to care about the internal data storage and degenerate
	   indicies, but ISTP CDF *wants* to know this information (back in the
	   old days the rVariables this worked, grrr).  So what we have to do 
	   is ask for a subset that ONLY contains non-degenerate information.

	   To be ISTP compliant, use the varible's index map and "punch out"
	   overall dataset indexes that don't apply. */

	aMax[0] = 1;  /* We don't care about the 0-th index, we're not record varying */

	for(int r = 1; r < nDsRank; ++r){
		if(pDsShape[r] > 0){
			if(DasVar_degenerate(pVar, r))
				aMax[r] = 1;
			else{
				if(pDsShape[r] == DASIDX_RAGGED)
					return das_error(PERR, "CDF does not allow ragged array lengths "
						"after the zeroth index.  We could get around using by loading "
						"all data in RAM and using fill values when writing the CDF "
						"but have chosen not to do so at this time."
					);
				else
					aMax[r] = pDsShape[r];
			}
		}
		else
			aMax[r] = 1;
	}	

	/* Force all sequences and binary variables to take on concrete values */
	DasAry* pAry = DasVar_subset(pVar, nDsRank, aMin, aMax);

	ptrdiff_t aAryShape[DASIDX_MAX] = DASIDX_INIT_UNUSED;
	int nAryRank = DasAry_shape(pAry, aAryShape);

	size_t uLen = 0;
	das_val_type vt = DasAry_valType(pAry);
	const ubyte* pVals = DasAry_getIn(pAry, vt, DIM0, &uLen);

	/* Put index information into data types needed for function call */
	static const long indicies[DASIDX_MAX]  = {0,0,0,0, 0,0,0,0};
	long counts[DASIDX_MAX]                 = {0,0,0,0, 0,0,0,0};
	static const long intervals[DASIDX_MAX] = {1,1,1,1, 1,1,1,1};

	for(int r = 0; r < nAryRank; ++r){
		counts[r] = aAryShape[r];
	}

	iStatus = CDFhyperPutzVarData(
		nCdfId, /* CDF File ID */
		DasVar_cdfId(pVar),  /* Shamelessly use point as an long int storage */
		0, /* record start */
		1, /* number for records to write */
		1, /* record interval */
		indicies, /* Dimensional index start posititions */
		counts,   /* Number of intervals along each array dimension */
		intervals, /* Writing intervals along each array dimension */
		pVals
	);

	dec_DasAry(pAry);

	if(!_cdfOkayish(iStatus))
		return PERR;
	
	return DAS_OKAY;
}

DasErrCode writeVarProps(
	CDFid nCdfId, DasDim* pDim, DasVar* pVar, VarInfo* pCoords, size_t uCoords
){
	char sAttrName[64] = {'\0'};

	/* Find and set my dependencies.  The rules:
	 *
	 *   1) Start at the variable's highest used index.
	 *   2) If the varible provides a dependency, it can't have that dependency
	 */

	/* Find out if I happen to also be a coordinate */
	int iAmDep = -1;
	if(pDim->dtype == DASDIM_COORD){
		for(size_t u = 0; u < uCoords; ++u){
			if((pCoords + u)->pVar == pVar){
				iAmDep = (pCoords + u)->iDep;
				break;
			}
		}
	}

	ptrdiff_t aVarShape[DASIDX_MAX] = DASIDX_INIT_UNUSED;
	DasVar_shape(pVar, aVarShape);
	int iIdxMax = _maxIndex(aVarShape);

	for(int iIdx = iIdxMax; iIdx >= 0; --iIdx){

		/* Either not record varying, or not affected by this index */
		if(DasVar_degenerate(pVar, 0)||DasVar_degenerate(pVar, iIdx))
			continue;

		if(iIdx == iAmDep)
			continue;

		/* Find the dependency for my current index */
		for(size_t u = 0; u < uCoords; ++u){
			if((pCoords + u)->iDep == iIdx){
				snprintf(sAttrName, 15, "DEPEND_%d", iIdx);
				writeVarStrAttr(
					nCdfId, 
					DasVar_cdfId(pVar),
					sAttrName, 
					(pCoords+u)->sCdfName /* CDF name of the coordinate */
				);	
			}
		}
	}

	/* Intercept vtTime structure variables and save the units at ns */
	char sConvert[128] = {'\0'};
	const char* sUnits = " ";
	if(DasVar_valType(pVar) == vtTime)
		sUnits = "ns";
	else if (pVar->units != NULL){
		if(Units_haveCalRep(pVar->units))
			sUnits = Units_interval(pVar->units);
		else{
			sUnits = pVar->units;
			/* Convert all instances of ** to ^ follow the CDF convention */
			bool bLastStar = false;
			char* pWrite = sConvert;
			for(int i = 0; (i < strlen(sUnits))&&(i < 127); ++i){
				if(sUnits[i] == '*'){
					if(bLastStar){
						*pWrite = '^'; ++pWrite;
						bLastStar = false;
					}
					else{
						bLastStar = true;
					}
				}
				else{
					if(bLastStar){ *pWrite = '*'; ++pWrite; bLastStar = false; }
					*pWrite = sUnits[i]; ++pWrite;
				}
			}
			sUnits = sConvert;
		}
	}

	writeVarStrAttr(nCdfId, DasVar_cdfId(pVar), "UNITS", sUnits);

	if(pDim->dtype == DASDIM_COORD)
		writeVarStrAttr(nCdfId, DasVar_cdfId(pVar), "VAR_TYPE", "support_data");
	else
		writeVarStrAttr(nCdfId, DasVar_cdfId(pVar), "VAR_TYPE", "data");

	return DAS_OKAY;
}

/* ************************************************************************* */
/* This is the key function of the program:

   Property Handling:

      Dataset Props -> Global Area

			Dim Props -> Go to Variable area and are replicated for each variable
			             In addition a suffix is added to indicate the type

			             If a skeleton is supplied that has the relavent property
			             already defined, it is not changed.

			The following auto generated by the structure of the dataset items:

			   DasVar.units ->  CDFvar UNITS
			   Index Map inspection -> Triggers DEPEND_0, _1, etc.

			For the center items, if widths etc. are divided by two and stored
			as:
			    DELTA_PLUS_VAR & DELTA_MINUS_VAR

*/

DasErrCode onDataSet(StreamDesc* pSd, DasDs* pDs, void* pUser)
{
	struct context* pCtx = (struct context*)pUser;


	ptrdiff_t aDsShape[DASIDX_MAX] = DASIDX_INIT_UNUSED;
	int nDsRank = DasDs_shape(pDs, aDsShape);

	if(daslog_level() <= DASLOG_INFO){
		char sBuf[16000] = {'\0'};
		DasDs_toStr(pDs, sBuf, 15999);
		daslog_info(sBuf);
	}

	/* Send Data set properties to the global attribute space */
	size_t uProps = DasDesc_length((DasDesc*)pDs);
	for(size_t u = 0; u < uProps; ++u){
		const DasProp* pProp = DasDesc_getPropByIdx((DasDesc*)pDs, u);
		if(pProp == NULL) continue;

		if(strcmp(DasProp_name(pProp), "CDF_NAME") == 0)
			continue;

		if(writeGlobalProp(pCtx->nCdfId, pProp) != DAS_OKAY)
			return PERR;
	}

	/* Gather all coordinates and determine dependencies */
	size_t u, uCoords = 0;
	VarInfo* pCdfCoords = solveDepends(pDs, &uCoords);  /* <-- Heavy lifter */
	if(pCdfCoords == NULL)
		return PERR;
	
	/* Create variables for each of the dependencies */
	int nRet = DAS_OKAY;
	for(u = 0; u < uCoords; ++u){
		VarInfo* pVi = (pCdfCoords + u);

		if((nRet = makeCdfVar(
			pCtx->nCdfId, pVi->pDim, pVi->pVar, nDsRank, aDsShape, pVi->sCdfName
		)) != DAS_OKAY)
			return nRet;

		nRet = writeVarProps(pCtx->nCdfId, pVi->pDim, pVi->pVar, pCdfCoords, uCoords);
		if(nRet != DAS_OKAY)
			return nRet;
	}

	/* Now for the data variables... */
	size_t uDims = DasDs_numDims(pDs, DASDIM_DATA);
	char sNameBuf[DAS_MAX_ID_BUFSZ] = {'\0'};
	
	for(size_t d = 0; d < uDims; ++d)
	{
		DasDim* pDim = (DasDim*) DasDs_getDimByIdx(pDs, d, DASDIM_DATA);
		size_t uVars = DasDim_numVars(pDim)
		for(size_t v = 0; v < uVars; ++v){
			DasVar* pVar = (DasVar*) DasDim_getVarByIdx(pDim, v);

			nRet = makeCdfVar(pCtx->nCdfId, pDim, pVar, nDsRank, aDsShape, sNameBuf);
			if(nRet != DAS_OKAY)
				return nRet;

			nRet = writeVarProps(pCtx->nCdfId, pDim, pVar, pCdfCoords, uCoords);
			if(nRet != DAS_OKAY)
				return nRet;
		}
	}

	return DAS_OKAY;
}

/* ************************************************************************* */
/* Writing data to the CDF */

int64_t* g_pTimeValBuf = NULL;
size_t g_uTimeBufLen = 0;

const ubyte*  _structToTT2k(const ubyte* pData, size_t uTimes)
{
	if(g_uTimeBufLen < uTimes){
		if(g_pTimeValBuf != NULL){
			free(g_pTimeValBuf);
		}
		g_pTimeValBuf = (int64_t*)calloc(uTimes, sizeof(int64_t));
	}

	const das_time* pTimes = (const das_time*)pData;
	for(size_t u = 0; u < uTimes; ++u){
		g_pTimeValBuf[u] = dt_to_tt2k(pTimes + u);
	}
	return (const ubyte*)g_pTimeValBuf;
}

DasErrCode _writeRecVaryAry(CDFid nCdfId, DasVar* pVar, DasAry* pAry)
{
	CDFstatus iStatus; /* Used by the _OK macro */

	static const long indicies[DASIDX_MAX]  = {0,0,0,0, 0,0,0,0};
	static const long intervals[DASIDX_MAX] = {1,1,1,1, 1,1,1,1};
	long counts[DASIDX_MAX]                 = {0,0,0,0, 0,0,0,0};
	ptrdiff_t aShape[DASIDX_MAX]            = DASIDX_INIT_BEGIN;

	size_t uElSize = 0;
	size_t uElements = 0;
	size_t uTotal = 0;
	const ubyte* pData = DasAry_getAllVals(pAry, &uElSize, &uElements);
	
	if(pData == NULL)
		return PERR;

	/* Hook in data conversion.  If we see vtTime, that's a structure, and
	   it needs to be re-written to TT2K */

	if(DasAry_valType(pAry) == vtTime){
		pData = _structToTT2k(pData, uElements);
	}

	int nRank = DasAry_shape(pAry, aShape);

	uTotal = aShape[0];
	for(int r = 1; r < nRank; ++r){
		counts[r-1] = aShape[r];
		uTotal *= aShape[r];
	}

	assert(uTotal == uElements);

	if(!_OK(CDFhyperPutzVarData(
		nCdfId,
		DasVar_cdfId(pVar),
		DasVar_cdfStart(pVar), /* record start */
		(long) aShape[0],
		1,
		indicies,
		counts,
		intervals,
		pData
	)))
		return PERR;

	DasVar_cdfIncStart(pVar, aShape[0]);

	return DAS_OKAY;
}

DasErrCode putAllData(CDFid nCdfId, int nDsRank, ptrdiff_t* pDsShape, DasVar* pVar)
{
	/* Take a short cut for array variables */
	if(DasVar_type(pVar) == D2V_ARRAY){

		DasAry* pAry = DasVarAry_getArray(pVar); /* Does not copy data */
		assert(pAry != NULL);

		if(_writeRecVaryAry(nCdfId, pVar, pAry) != DAS_OKAY)
			return PERR;
	}
	else{

		/* For binaryOps and Sequences, calculate the values now.  Since none of the
		   degenerate indexes are saved to CDF "punch out" the degenerate indexes
		   with a max range of just 1 */

		ptrdiff_t aMin[DASIDX_MAX] = DASIDX_INIT_BEGIN;
		ptrdiff_t aMax[DASIDX_MAX] = DASIDX_INIT_BEGIN;

		for(int r = 0; r < nDsRank; ++r){
			if(pDsShape[r] <= 0)
				return das_error(PERR, "Ragged datasets are not yet supported");
			
			if(DasVar_degenerate(pVar, r)){
				aMax[r] = 1;
				assert(r > 0); /* Can't be degenerate in the streaming index at this point */
			}
			else
				aMax[r] = pDsShape[r];
		}

		/* A potentially long calculation.... */
		DasAry* pAry = DasVar_subset(pVar, nDsRank, aMin, aMax);

		if(_writeRecVaryAry(nCdfId, pVar, pAry) != DAS_OKAY)
			return PERR;

		dec_DasAry(pAry);  /* Delete the temporary array */		
	}
	return DAS_OKAY;
}

/* Assuming all varibles were setup above, now write a bunch of data to the CDF */
DasErrCode writeAndClearData(DasDs* pDs, struct context* pCtx)
{
	ptrdiff_t aDsShape[DASIDX_MAX] = DASIDX_INIT_UNUSED;
	int nDsRank = DasDs_shape(pDs, aDsShape);

	/* Write all the data first.  Don't clear arrays as you go because
	   binary-op variables might depend on them! */

	for(int iType = DASDIM_COORD; iType <= DASDIM_DATA; ++iType){  /* Coord & Data */

		size_t uDims = DasDs_numDims(pDs, iType);                   /* All Dimensions */
		for(size_t uD = 0; uD < uDims; ++uD){

			DasDim* pDim = (DasDim*)DasDs_getDimByIdx(pDs, uD, iType); /* All Variables */
			size_t uVars = DasDim_numVars(pDim);

			for(size_t uV = 0; uV < uVars; ++uV){
				DasVar* pVar = (DasVar*) DasDim_getVarByIdx(pDim, uV);

				if(DasVar_degenerate(pVar, 0))  /* var is not record varying */
					continue;

				if(putAllData(pCtx->nCdfId, nDsRank, aDsShape, pVar) != DAS_OKAY)
					return PERR;
			}
		}
	}

	/* Now clear all the record-varying arrays in the dataset.  Only the variables
	 * know if the arrays are record varying.  Maybe I should add a  */
	size_t uArrays = DasDs_numAry(pDs);
	for(size_t uAry = 0; uAry < uArrays; ++uAry){
		DasAry* pAry = DasDs_getAry(pDs, uAry);

		if(DasAry_getUsage(pAry) & DASARY_REC_VARY)
			DasAry_clear(pAry);
	}

	return DAS_OKAY;
}

/* ************************************************************************* */

DasErrCode onData(StreamDesc* pSd, DasDs* pDs, void* pUser)
{
	struct context* pCtx = (struct context*)pUser;

	/* Just let the data accumlate in the arrays unless we've hit
	   our memory limit, then hyperput it */

	if(daslog_level() <= DASLOG_DEBUG){

		char sBuf[128] = {'\0'};
		ptrdiff_t aShape[DASIDX_MAX] = DASIDX_INIT_UNUSED;
		
		int nRank = DasDs_shape(pDs, aShape);
		das_shape_prnRng(aShape, nRank, nRank, sBuf, 127);

		daslog_debug_v("Dataset %s shape is now: %s", DasDs_id(pDs), sBuf);
		daslog_debug_v("Dataset memory alloc:   %zu bytes", DasDs_memOwned(pDs));
		daslog_debug_v("Dataset memory used:    %zu bytes", DasDs_memUsed(pDs));
		daslog_debug_v("Dataset memory indexed: %zu bytes", DasDs_memIndexed(pDs));
	}

	if(DasDs_memUsed(pDs) > g_nMemBufThreshold)
		return writeAndClearData(pDs, pCtx);

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
DasErrCode onClose(StreamDesc* pSd, void* pUser)
{
	struct context* pCtx = (struct context*)pUser;

	/* Loop over all the datasets in the stream and make sure they are flushed */
	int nPktId = 0;
	DasDesc* pDesc = NULL;
	while((pDesc = StreamDesc_nextPktDesc(pSd, &nPktId)) != NULL){
		if(DasDesc_type(pDesc) == DATASET){
			if(writeAndClearData((DasDs*)pDesc, pCtx) != DAS_OKAY)
				return PERR;
		}
	}
	
	return DAS_OKAY;
}

/* helper ****************************************************************** */
/* autogenerate a CDF file name from just the download time */

void _addTimeStampName(char* sDest, size_t uDest)
{
	char sTmp[LOC_PATH_LEN] = {'\0'};
	das_time dt; dt_now(&dt);
	snprintf(sTmp, LOC_PATH_LEN - 1, "%s%cparsed_at_%04d-%02d-%02dT%02d-%02d-%06.3f.cdf", 
		sDest, DAS_DSEPC, dt.year, dt.month, dt.mday, dt.hour, dt.minute, dt.second
	);
	strncpy(sDest, sTmp, uDest - 1);
}

DasErrCode _addSourceName(char* sDest, size_t uDest, const char* sInFile)
{
	char* pSep = strrchr(sInFile, DAS_DSEPC);
	if(pSep != NULL)
		sInFile = pSep + 1;

	if(sInFile[0] == '\0')
		return das_error(PERR, "Input filename was empty (or was just a directory part)");

	/* If ends in some other extension, change it to .cdf */
	char* pDot = strrchr(sInFile, '.');
	char sTmp1[LOC_PATH_LEN] = {'\0'};
	if(pDot != NULL){
		strncpy(sTmp1, sInFile, LOC_PATH_LEN-1);
		pDot = strrchr(sTmp1, '.');
		*pDot = '\0';
		sInFile = sTmp1;
	}

	char sTmp2[LOC_PATH_LEN] = {'\0'};
	snprintf(sTmp2, LOC_PATH_LEN - 1, "%s%c%s.cdf", sDest, DAS_DSEPC, sInFile);

	strncpy(sDest, sTmp2, uDest - 1);

	return DAS_OKAY;
}

/* ************************************************************************* */

DasErrCode writeFileToStdout(const char* sFile)
{
	FILE* pIn = fopen(sFile, "rb");
	if(pIn == NULL)
		return das_error(PERR, "Can not read source file %s.", sFile);
	
	char buffer[65536];
	size_t uRead = 0;

	while( (uRead = fread(buffer, sizeof(char), 65536, pIn)) > 0){
		if(uRead != fwrite(buffer, sizeof(char), uRead, stdout)){
			das_error(PERR, "Error writing %s to stdout", sFile);
			fclose(pIn);
			return PERR;
		} 
	}

	fclose(pIn);
	return DAS_OKAY;
}

/* ************************************************************************* */

int main(int argc, char** argv) 
{

	DasErrCode nRet = DAS_OKAY;

	/* Exit on errors, log info messages and above */
	das_init(argv[0], DASERR_DIS_EXIT, 0, DASLOG_INFO, NULL);
	char sWriteTo[LOC_PATH_LEN] = {'\0'};

	FILE* pInFile = NULL;

	popts_t opts;
	
	if(parseArgs(argc, argv, &opts) != DAS_OKAY)
		return 13;

	daslog_setlevel(daslog_strlevel(opts.aLevel));

	/* Make room for storing the var context info */
	g_uMaxVars = 512;
	g_pVarCdfInfo = (var_cdf_info_t*) calloc(g_uMaxVars, sizeof(var_cdf_info_t));

	struct context ctx;
	memset(&ctx, 0, sizeof(struct context));
	ctx.sWriteTo = sWriteTo;
	ctx.sTpltFile = opts.aTpltFile;

	/* Figure out where we're gonna write before potentially contacting servers */
	bool bReStream = false;
	bool bAddFileName = false;

	if(opts.aOutFile[0] != '\0'){
		/* Writing to a specific final location */
		if(das_isdir(opts.aOutFile)){
			strncpy(ctx.sWriteTo, opts.aOutFile, 127);
			bAddFileName = true;
		}
		else{
			strncpy(ctx.sWriteTo, opts.aOutFile, LOC_PATH_LEN-1);
			if((nRet = das_mkdirsto(ctx.sWriteTo)) != DAS_OKAY)
				return nRet;
		}
	}
	else{
		/* Just writing to a temporary location */
		/* Temporary write and stream.  Would love to use tmpfile here, but 
		   libcdf doesn't support it */
		if(opts.aTmpDir[0] == '\0'){
			snprintf(ctx.sWriteTo, LOC_PATH_LEN-1, "%s%c%s%cd3cdf-tmp-%d.cdf", 
				das_userhome(), DAS_DSEPC, DEF_TEMP_DIR, DAS_DSEPC, getpid()
			);
		}	
		else{
			snprintf(ctx.sWriteTo, LOC_PATH_LEN-1, "%s%cd3cdf-tmp-%d.cdf", 
				opts.aTmpDir, DAS_DSEPC, getpid()
			);
		}
		bReStream = true;
		if(das_mkdirsto(ctx.sWriteTo) != DAS_OKAY){
			return das_error(PERR, "Couldn't make directories to %s", ctx.sWriteTo);	
		}
	}

	/* Build one of 4 types of stream readers */
	DasCredMngr* pCreds = NULL;
	DasHttpResp res; memset(&res, 0, sizeof(DasHttpResp));
	DasIO* pIn = NULL;

	if(opts.aSource[0] == '\0'){ /* Reading from standard input */
		pIn = new_DasIO_cfile(PROG, stdin, "r");

		/* If writing from standard input, an we need a name, just use the current time */
		if(bAddFileName)
			_addTimeStampName(ctx.sWriteTo, LOC_PATH_LEN-1);
	}
	else if(
		(strncmp(opts.aSource, "http://", 7) == 0)||
		(strncmp(opts.aSource, "https://", 8) == 0)
	){

		pCreds = new_CredMngr(opts.aCredFile);

		/* Give it a connection time out of 6 seconds */
		if(!das_http_getBody(opts.aSource, "das3_cdf", pCreds, &res, 6.0)){

			if((res.nCode == 401)||(res.nCode == 403))
				return das_error(DASERR_HTTP, "Authorization failure: %s", res.sError);
			
			if((res.nCode == 400)||(res.nCode == 404))
				return das_error(DASERR_HTTP, "Query error: %s", res.sError);

			return das_error(DASERR_HTTP, "Uncatorize error: %s", res.sError);
		}
		
		char sUrl[ FIELD_SZ(popts_t,aSource) ] = {'\0'};
		das_url_toStr(&(res.url), sUrl, sizeof(sUrl) - 1);
		
		if(strcmp(sUrl, opts.aSource) != 0)
			daslog_info_v("Redirected to %s", sUrl);

		if(DasHttpResp_useSsl(&res))
			pIn = new_DasIO_ssl("das3_cdf", res.pSsl, "r");
		else
			pIn = new_DasIO_socket("das3_cdf", res.nSockFd, "r");

		/* If we need a filename, try to get it from the response header */
		if(bAddFileName){
			if(res.sFilename)
				_addSourceName(ctx.sWriteTo, LOC_PATH_LEN-1, res.sFilename);
			else
				_addTimeStampName(ctx.sWriteTo, LOC_PATH_LEN-1);
		}
	}
	else{
		// Just a file
		const char* sInFile = opts.aSource;
		
		if(strncmp(opts.aSource, "file://", 7) == 0)
			sInFile = opts.aSource + 7;

		pInFile = fopen(sInFile, "rb");

		if(pInFile == NULL)
			return das_error(PERR, "Couldn't open file %s", sInFile);

		pIn = new_DasIO_cfile(PROG, pInFile, "rb");

		if(bAddFileName)
			_addSourceName(ctx.sWriteTo, LOC_PATH_LEN-1, sInFile);
	}

	DasIO_model(pIn, 3); /* Upgrade any das2 <packet>s to das3 <dataset>s */

	/* If remove was selected try to delete the output location */
	if( opts.bRmFirst && das_isfile(ctx.sWriteTo) ){
		if(remove(ctx.sWriteTo) != 0)
			return das_error(PERR, "Could not clear destination file %s first", ctx.sWriteTo);
	}

	/* Install our handlers */
	StreamHandler handler;
	memset(&handler, 0, sizeof(StreamHandler));
	handler.streamDescHandler = onStream;
	handler.dsDescHandler     = onDataSet;
	handler.dsDataHandler     = onData;
	handler.exceptionHandler  = onExcept;
	handler.closeHandler      = onClose;
	handler.userData          = &ctx;

	DasIO_addProcessor(pIn, &handler);
	
	nRet = DasIO_readAll(pIn);  /* <---- RUNS ALL PROCESSING -----<<< */

	if(ctx.nCdfId != 0)
		CDFclose(ctx.nCdfId);

	if(pInFile)
		fclose(pInFile);

	if(pCreds){
		del_CredMngr(pCreds);
		DasHttpResp_clear(&res);
	}

	if((nRet == DAS_OKAY)&&(bReStream)){
		nRet = writeFileToStdout(ctx.sWriteTo);
		remove(ctx.sWriteTo);
	}

	return nRet;
};