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
#include <stdlib.h>

#include <das2/core.h>

#define PROG "das3_cdf"
#define PERR 63

/* Handle lack of const qualifier on CDFvarNum */
#define CDFvarId(id, str) CDFgetVarNum((id), (char*) (str))

#define NEW_FILE_MODE S_IRUSR|S_IWUSR|S_IRGRP|S_IWGRP|S_IROTH

#define DEF_AUTH_FILE ".dasauth"
#define DEF_TEMP_DIR  ".dastmp"

#define LOC_PATH_LEN 256

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
"   Other CDF attributes are also set based on the data structure type. Some "
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
"                 are saved.  Defaults to " HOME_STR DAS_DSEPS DEF_AUTH_FILE "\n"
"\n");

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
		*pLong = false;
		return true;
	}
	size_t uLen = strlen(sLong);  /* Include the '=' */

	if(strncmp(sArg, sLong, uLen) == 0){
		*pLong = true;
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
		
	else if(argv[*pArgIdx][11] == '\0') 
		goto NO_ARG;

	const char* sVal;
	if(bIsLong){
		size_t uLongSz = strlen(sLong)
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
	das_error(PERR, "Missing option after argument %s", argv[i]);
	return false;
}

typedef struct program_optitons{
	char aTpltFile[256]; /* Template CDF */
	char aSource[1024];  /* Input source, http://, file:// etc. */
	char aOutFile[256];  /* Non-filter: output */
	char aTmpDir[256];   /* Filter mode: temp dir */
	char aLevel[32];    
	char aCredFile[256];    
} popts_t;

int parseArgs(int argc, char** argv, popts_t* pOpts)
{
	memset(pOpts, 0, sizeof(popts_t));

	/* Set a few defaults */
	snprintf(
		pOpts->aCredFile, sizeof(popts_t.aCreds) - 1, "%s" DAS_DSEPS DEF_AUTH_FILE,
		das_userhome()
	);

	strcpy(pOpts->sLevel, "info");

	snprintf(
		pOpts->aTmpDir, sizeof(popts_t.aTmpDir) - 1, "%s" DAS_DSEPS ".cdftmp",
		das_userhome()
	);

	int i = 0;
	while(i < (argc-1)){
		++i;  /* 1st time, skip past the program name */

		if(argv[i][0] == '-'){
			if(_isArg(argv[i], "-h", "--help", &bIsLong)){
				prnHelp();
				exit(0);
			}
			if(_getArg(pOpts->aTpltFile, sizeof(popts_t.aTpltFile), argv, argc, &i, "-t", "--template="))
				continue;
			if(_getArg(pOpts->aSource,   sizeof(popts_t.aSource),   argv, argc, &i, "-i", "--input="))
				continue;
			if(_getArg(pOpts->aWriteTo,  sizeof(popts_t.aWriteTo),  argv, argc, &i, "-o", "--output="))
				continue;
			if(_getArg(pOpts->aTmpDir,   sizeof(popts_t.aTmpDir),   argv, argc, &i, "-s", "--scratch="))
				continue;
			if(_getArg(pOpts->aLevel,    sizeof(popts_t.aLevel),    argv, argc, &i, "-l", "--log="))
				continue;
			if(_getArg(pOpts->aCredFile, sizeof(popts_t.aCredFile), argv, argc, &i, "-c", "--credentials="))
				continue;
		}
		return das_error(PERR, "Unknown command line argument %s", argv[i]);
	}
	return DAS_OKAY;
}

/* ************************************************************************* */

struct context {
	CDFid nCdfId;
	char sCdfStatus[CDF_STATUSTEXT_LEN+1];
	const char* sTpltFile;  /* An empty template CDF to put data in */
	const char* sWriteTo;
	/* DasTime dtBeg; */      /* Start point for initial query, if known */
	/* double rInterval; */   /* Size of original query, if known */

	uint32_t uFlushSz;   /* How big to let internal memory grow before a CDF flush */
};

/* sending CDF message to the log ****************************************** */

bool _cdfOkayish(CDFstatus iStatus){
	char[CDF_ERRTEXT_LEN+1] sMsg;

	/* Wrapper should have prevented this, but in case it's called directly */
	if(iStatus == CDF_OK)
		return true;

	CDFgetStatusText(iStatus, sMsg);

	if(status < CDF_WARN){	
		daslog_error_v("from cdflib, %s", sMsg);
		return false;
	}

	if(status < CDF_OK) 
		daslog_warn_v("from cdflib, %s", sMsg);
  	
  	else if(status > CDF_OK)
		daslog_info_v("from cdflib, %s", sMsg);

	return true;
}

/* Use a macro to avoid unneccessary functions calls that slow the program */
#define _OK( SOME_CDF_FUNC ) ( ((status = (SOME_CDF_FUNC) ) == CDF_OK) || (_cdfOkayish(status)) )


/* ************************************************************************* */
/* Converting Das Properties to CDF properties */

/* Buffers for property conversion */

#define PROP_XFORM_SZ 65536  /* 64K */

const ubyte g_propBuf[PROP_XFORM_SZ];

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
	cSep = DasProp_sep(pProp);
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
		assert(false, "das2C library change detected");
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

	/* Strings that don't have a special separator only have one entry */
	char cSep = DasProp_value(pProp);
	if(cSep == '\0'){
		if(iEntry == 0)
			return strlen(pRead);
		else
			return 0;
	}

	/* Get the length between separators */
	/* I am a multi-entry string, get the length form seperator
	   iEntry, until iEntry + 1 */
	const char* pRead = DasProp_value(pProp);
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
	if(DasProp_type(pProp) & DASPROP_STRING)
		return DasProp_value(pProp);

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
		if(DasProp_convertInt(pProp, g_propBuf, uBufLen) != DAS_OKAY)
			return NULL;
		else
			return g_propBuf;

	case DASPROP_REAL:     
		uBufLen = PROP_XFORM_SZ / sizeof(double);
		if(DasProp_convertReal(pProp, g_propBuf, uBufLen) != DAS_OKAY)
			return NULL;
		else
			return g_propBuf;
		
	case DASPROP_DATETIME:
		uBufLen = PROP_XFORM_SZ / sizeof(int64_t);
		if(DasProp_convertTt2k(pProp, g_propBuf, uBufLen) != DAS_OKAY)
			return NULL;
		else
			return g_propBuf;
	default:
		assert(false, "das2C library change detected");
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
			return DasProp_value(pProp);
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
				return pEntry;
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
	CDFstatus status = CDF_OK;

	long n = DasProp_cdfEntries(pProp);
	for(long iEntry = 0; iEntry < n; ++iEntry){

		status = CDFputAttrgEntry(
			iCdf, 
			CDFgetAttrNum(iCdf, DasProp_cdfName(pProp)),
			iEntry, 
			DasProp_cdfType(pProp),
			DasProp_cdfEntLen(pProp, iEntry),
			DasProp_cdfEntValues(pProp, iEntry)
		);
		if(!_cdfOkayish(status))
			return PERR
	}

	return DAS_OKAY;
}

DasErrCode writeVarProp(CDFid iCdf, long iVarNum, const DasProp* pProp)
{
	status = CDFput/AttrzEntry(
		iCdf, 
		CDFgetAttrNum(iCdf, DasProp_cdfName(pProp)),
		iVarNum,
		DasProp_cdfType(pProp),
		(long) DasProp_items(pProp),
		DasProp_cdfValues(pProp, iEntry)
	);
	if(!_cdfOkayish(status))
		return PERR;

	return DAS_OKAY;
}

/* ************************************************************************* */

DasErrCode onStream(StreamDesc* pSd, void* pUser){
	struct context pCtx* = (struct context pCtx*)pUser;

	CDFstatus nCdfRet = CDF_OK;

	/* CDF oddity, halt the file name at the dot */
	char* pDot = strrchr(pCtx->sOutFile, '.')
	if(pDot != NULL) *pDot = '\0';

	/* Open the file since we have something to write */
	if(pCtx->sTpltFile){
		/* Copy in skeleton and open that or... */
		if(!das_copyfile(sSkelCdf, pCtx->sOutFile, NEW_FILE_MODE)){
			das_error(PERR, "Couldn't open copy '%s' --to--> '%s'", sSkelCdf, sDest);
			return PERR;
		}
		if(CDF_OK != CDFopenCDF(sDest, &(pCtx->nCdfId))){
			const char* sTmp = "";
			if(pDot != NULL) *pDot = '.';
			else sTmp = ".cdf";
			return das_error(PERR, "Couldn't open CDF file '%s%s'", pCtx->sOutFile, sTmp);
		}
	}
	else{
		/* Create a new file */
		if( CDF_OK != CDFcreateCDF(pCtx->sOutFile, &(pCtx->nCdfId)) ){
			const char* sTmp = "";
			if(pDot != NULL) *pDot = '.';
			else sTmp = ".cdf";
			return das_error(PERR, "Couldn't open CDF file '%s%S'", pCtx->sOutFile, sTmp)
		}
	}

	if(pDot != NULL) *pDot = '.';  /* But our dot back damnit */

	/* We have the file, run in our properties */
	size_t uProps = DasDesc_lengthIn((DasDesc*)pSd);
	for(size_t u = 0; u < uProps; ++u){
		const DasProp* pProp = DasDesc_getPropByIdx((DasDesc*)pSd, u);
		if(pProp == NULL) continue;

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
DasErrCode onDataSet(StreamDesc* pSd, DasDs* dd, void* pUser)
{
	struct context pCtx* = (struct context pCtx*)pUser;

	/* Inspect the dataset and create any associated CDF variables */
	/* For data that has values in the header, write the values to CDF now */
	return PERR;
}

/* ************************************************************************* */
DasErrCode onData(StreamDesc* pSd, DasDs* dd, void* pUser)
{
	struct context pCtx* = (struct context pCtx*)pUser;

	/* Just let the data accumlate in the arrays unless we've hit
	   our memory limit, then hyperput it */

	return PERR;
}

/* ************************************************************************* */
DasErrCode onExcept(OobExcept* pExcept, void* pUser)
{
	struct context pCtx* = (struct context pCtx*)pUser;

	/* If this is a no-data-in range message set the no-data flag */
	return PERR;
}

/* ************************************************************************* */
DasErrCode onClose(StreamDesc* pSd, void* pUser)
{
	struct context pCtx* = (struct context pCtx*)pUser;

	/* Flush all data to the CDF */
	return PERR;
}

/* helper ****************************************************************** */
/* autogenerate a CDF file name from just the download time */

void _addTimeStampName(char* sDest, size_t uDest)
{
	char sTmp[LOC_PATH_LEN] = {'\0'};
	das_time dt; dt_now(&dt);
	snprintf(sTmp, LOC_PATH_LEN - 1, "%s%cparsed_at_%04d-%02d-%02dT%02d-%02d-%06.3f.cdf", 
		sWriteTo, DAS_DSEPC, dt.year, dt.month, dt.mday, dt.hour, dt.minute, dt.second
	);
	strncpy(sWriteTo, sTmp, uDest - 1);
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
	snprintf(sTmp2, LOC_PATH_LEN - 1, "%s%c%s.cdf", sInFile);

	strncpy(sWriteTo, sTmp2, uDest - 1);

	return DAS_OKAY;
}

/* ************************************************************************* */

DasErrCode writeFileToStdout(const char* sFile)
{
	FILE* pIn = fopen(sFile, "rb");
	if(pIn == NULL)
		return das_error(PERR, "Can not read source file %s.", sFile);
	
	char buffer[65536]
	size_t uRead = 0;

	while( (uRead = fread(buffer, sizeof(char), 65536, pIn)) > 0){
		if(uRead != fwrite(buffer, sizeof(char), uRead, stdout)){
			das_error(PERR, "Error writing %s to stdout", sFile);
			fclose(pIn);
			fclose(pOut);
			return PERR;
		} 
	}

	fclose(pIn);

	return DAS_OKAY;
}

/* ************************************************************************* */

int main(int argc, char** argv) {

	/* Exit on errors, log info messages and above */
	das_init(argv[0], DASERR_DIS_EXIT, 0, DASLOG_INFO, NULL);
	char sWriteTo[LOC_PATH_LEN] = {'\0'};

	FILE* pFile = NULL;

	my_opts opts;
	
	if(parseArgs(argc, argv, &opts) != DAS_OKAY)
		return 13;

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
			strncpy(ctx.sWriteTo, 128, opts.aOutFile);
			bAddFileName = true;
		}
		else{
			strncpy(ctx.sWriteTo, LOC_PATH_LEN-1, opts.aOutFile);
			if(!das_mkdirsto(ctx.sWriteTo))
				return das_error(PERR, "Couldn't make directories to %s", ctx.sWriteTo);
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
				opts.sTmpDir, DAS_SEPC, getpid()
			);
		}
		bReStream = true;
		if(!das_mkdirsto(ctx.sWriteTo)){
			return das_error(PERR, "Couldn't make directories to %s", ctx.sWriteTo);	
		}
	}

	/* Build one of 4 types of stream readers */
	DasCredMngr* pCreds;
	DasHttpResp res;
	DasIO* pIn = NULL;

	if(sInFile[0] == '\0'){ /* Reading from standard input */
		pIn = new_DasIO_cfile(PROG, stdin, "r");

		/* If writing from standard input, an we need a name, just use the current time */
		if(bAddFileName)
			_addTimeStampName(ctx.sWriteTo, LOC_PATH_LEN-1);
	}
	else if((strncmp(sInFile, "http://", 7) == 0)||(strncmp(sInFile, "https://", 8) == 0)){

		pCreds = new_CredMngr(credFileName(sCredFile));

		/* Give it a connection time out of 6 seconds */
		if(!das_http_getBody(sInFile, "das3_cdf", pCreds, &res, 6.0)){

			if((res.nCode == 401)||(res.nCode == 403))
				return das_error(DASERR_HTTP, "Authorization failure: %s", res.sError);
			
			if((res.nCode == 400)||(res.nCode == 404))
				return das_error(DASERR_HTTP, "Query error: %s", res.sError);

			return das_error(DASERR_HTTP, "Uncatorize error: %s", res.sError);
		}
		
		das_url_toStr(&(res.url), sInFile, IN_FILE_SZ - 1);
		if(strcmp(sUrl, sInFile) != 0)
			daslog_info_v("Redirected to %s", sUrl);

		if(DasHttpResp_useSsl(&res))
			pIn = new_DasIO_ssl("das3_cdf", res.pSsl, "r");
		else
			pIn = new_DasIO_socket("das3_cdf", res.nSockFd, "r");

		/* If we need a filename, try to get it from the response header */
		if(bAddFileName){
			if(res.sFilename)
				_addSourceName(ctx.sWriteTo, LOC_PATH_LEN-1, res.sFileName);
			else
				_addTimeStampName(ctx.sWriteTo, LOC_PATH_LEN-1);
		}
	}
	else{
		// Just a file
		const char* sTmp = sInFile;
		if(strncmp(sInFile, "file://", 7) == 0)
			pTmp = sInFile + 7;
		pInFile = fopen(sTmp, "rb");
		if(pFile == NULL)
			return das_error(PERR, "Couldn't open file %s", sTmp);

		pIn = new_DasIO_cfile(PROG, pInFile, "rb");

		if(bAddFileName)
			_addSourceName(ctx.sWriteTo, LOC_PATH_LEN-1, sInFile);
	}

	DasIO_model(pIn, 3); /* Upgrade any das2 <packet>s to das3 <dataset>s */

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