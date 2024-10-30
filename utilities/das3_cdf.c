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
#include <ctype.h>
#ifdef _WIN32
#define strcasecmp _stricmp
#define strncasecmp _strnicmp
#else
#include <strings.h>
#define NEW_FILE_MODE S_IRUSR|S_IWUSR|S_IRGRP|S_IWGRP|S_IROTH
#endif

#include <cdf.h>

#include <das2/core.h>

#define PROG "das3_cdf"
#define PERR 63

#define DEF_AUTH_FILE ".dasauth"
#define DEF_TEMP_DIR  ".dastmp"

#define LOC_PATH_LEN 256

#define MAX_VAR_NAME_LEN 64

#define VAR_MAP_MAX_LINE 256

#ifdef _WIN32
#define HOME_VAR_STR "USERPROFILE"
#define HELP_TEMP_DIR "%USERPROFILE%\\" DEF_TEMP_DIR
#else
#define HOME_VAR_STR "HOME"
#define HELP_TEMP_DIR "$HOME/" DEF_TEMP_DIR
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
#define DEF_FLUSH_BYTES 16777216;   /* 16 MBytes */

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
"   writes a CDF file to standard output.  Unlike most das stream processors\n"
"   " PROG " is *not* a good filter.  It does not start writing ANY output\n"
"   until ALL input is consumed.  This is unavoidable as the CDF format is\n"
"   not a streaming format.  Thus " PROG " generates a temporary file and then\n"
"   feeds that to standard output. If your purpose is to generate a local file\n"
"   anyway, use the '--output' option below to avoid creating a temporary file.\n"
"\n"
"   Data values are written to CDF variables and metadata are written to CDF\n"
"   attributes.  The mapping of stream properties to CDF attributes follows.\n"
"\n"
"      <stream> Properties       -> CDF Global Attributes\n"
"      <dataset> Properties      -> CDF Global Attributes (prefix as needed)\n"
"      <coord>,<data> Properties -> CDF Variable Attributes\n"
"\n"
"   During the metadata mapping, common das3 property names are converted\n"
"   to equivalent ISTP metadata names.  The property conversions are:\n"
"\n"
"      label                 -> LABLAXIS (with units stripped)\n"
"      title,description     -> FIELDNAM\n"
"      summary               -> CATDESC\n"
"      notes                 -> VAR_NOTES\n"
"      format                -> FORMAT\n"
"      frame                 -> REFERENCE_FRAME\n"
"      nominalMin,nominalMax -> LIMITS_NOMINAL_MIN,LIMITS_NOMINAL_MAX\n"
"      scaleMin,scaleMax     -> SCALEMIN,SCALEMAX\n"
"      scaleType             -> SCALETYP\n"
"      validMin,validMax     -> VALIDMIN,VALIDMAX\n"
"      warnMin,warnMax       -> LIMITS_WARN_MIN,LIMITS_WARN_MAX\n"
"      compLabel             -> LABL_PTR_1\n"
"\n"
"   Note that if a property is named 'cdfName' it is not written to the CDF\n"
"   but instead changes the name of a CDF variable.\n"
"\n"
"   Other CDF attributes are also set based on the data structure type. Some\n"
"   examples are:\n"
"\n"
"      DasVar.units -> UNITS\n"
"      DasAry.fill  -> FILLVAL\n"
"      (algorithm)  -> DEPEND_N\n"
"      DasFrame.dir -> LABL_PTR_1 (if compLabel missing)\n"
"\n"
"   Note that if the input is a legacy das2 stream, it is upgraded internally\n"
"   to the das3 data model priror to writing the CDF file.\n"
"\n");

 
	printf(
"OPTIONS\n"
"   -h,--help     Write this text to standard output and exit.\n"
"\n"
"   -l LEVEL,--log=LEVEL\n"
"                 Set the logging level, where LEVEL is one of 'debug', 'info',\n"
"                 'warning', 'error' in order of decreasing verbosity.  All log\n"
"                 messages go to the standard error channel, the default is 'info'.\n"
"\n"
"   -b MB,--buffer=MB\n"
"                 To avoid constant writes, " PROG " buffers datasets in memory\n"
"                 until they are " THRESH " or larger and then they are written\n"
"                 to disk.  Use this parameter to change the buffer size.  Using\n"
"                 a large value can increase performance for large datasets.  The\n"
"                 special values 'inf', 'infinite' or '∞' can be used to only\n"
"                 write record data after the stream completes.\n"
"\n"
"   -t DIR,--temp-dir=DIR\n"
"                 Directory for writing temporary files when run as a command\n"
"                 pipeline filter.  Defaults to \"%s\". Ignored if -o is given.\n"
"\n"
"   -a FILE,--auth-toks=FILE\n"
"                 Set the location where server authentication tokens (if any)\n"
"                 are saved.  Defaults to %s%s%s\n"
"\n"
"   -i URL,--input=URL\n"
"                 Instead of reading from standard input, read from a given URL.\n"
"                 To read from a local file prefix it with 'file://'.  Only\n"
"                 file://, http:// and https:// are supported in this version.\n"
"\n"
"   -m FILE,--map-vars=FILE\n"
"                 Provide a mapping from automatic variable names to CDF variables\n"
"                 The map file has one name pair per line and has pattern:\n"
"\n"
"                    OUTPUT_NAME = INPUT_PKTID INPUT_DIM INPUT_ROLE [cdfName]\n"
"\n"
"                 The value \"*\" can be used to match any input packet ID.  The \n"
"                 cdfName is optional and can be used further restrict the match.\n"
"                 Note [cdfName] represents the input cdfName (if any), the output\n"
"                 cdfName is on the left hand side. A pound symbol, '#', denotes a\n"
"                 comment that runs to the end of the line.\n"
"\n"
"   -f,--filter-vars\n"
"                 Only output \"data\" variables mentioned in the variable map file.\n"
"                 Thus a map file with identical input and output names can be used\n"
"                 to sub-select das stream inputs.  Support variables needed by the\n"
"                 \"data\" variable are always emitted.\n"
"\n"
"   -o DEST,--output=DEST\n"
"                 Instead of acting as a poorly performing filter, write data\n"
"                 to this location.  If DEST is a file then data will be written\n"
"                 directly to that file. If DEST is a directory then an auto-\n"
"                 generated file name will be used. This is useful when reading\n"
"                 das servers since they provide default filenames.\n"
"\n"
"   -n,--no-istp\n"
"                 Don't automatically add certian ITSP meta-data attributes such as\n"
"                 'Data_version' if they are missing.\n"
"\n"
"   -s FILE,--skeleton=CDF_FILE\n"
"                 Initialize the output CDF with an empty skeleton CDF file first.\n"
"                 The program \"skeletoncdf\" providid by the NASA-Goddard can be\n"
"                 used to generate a binary CDF skeleton from a text file.\n"
"\n"
"   -r,--remove   Remove the destination file before writing. By default " PROG "\n"
"                 refuses to overwrite an existing output file.  Use with '-o'.\n"
"\n"
"   -c,--clean    Automatically delete any CDFs output files that contain no\n"
"                 record varying data. Use with '-o'.\n"
"\n"
"   -u,-uncompressed\n"
"                 Disable zlib compression.  All variables are written uncompressed.\n"
"                 This is needed for any CDF files submitted to the Planetary Data\n"
"                 system. Per ISTP rules, Epoch variables are not compressed.\n"
"\n", 
HELP_TEMP_DIR, HOME_VAR_STR, DAS_DSEPS, DEF_AUTH_FILE);


	printf(
"EXAMPLES\n"
"   1. Convert a local das stream file to a CDF file.\n"
"\n"
"      $ cat my_data.d3b | " PROG " -o my_data.cdf\n"
"\n"
"   2. Read from a remote das server and write data to the current directory,\n"
"      using the server provided automatic file name in the HTTP headers.\n"
"\n"
"      $ " PROG " -i \"https://college.edu/mission/inst?beg=2014&end=2015\" -o ./\n"
"\n"
"   3. Create a PDS archive file. Compression is disabled and records are\n"
"      buffered in RAM before writing a single continuous block per variable.\n"
"\n"
"      $ cat my_pds_data.d3b | " PROG " -o my_pds_data.cdf -u -m infinite\n"
"\n"
"   4. Create and use a template CDF to add meta-data to the output while\n"
"      renaming output variables.\n"
"\n"
"      Run once to produce metadata and variable mappings:"
"      $ vim my_metadata.skt\n"
"      $ skeletoncdf my_metadata.skt   # produces an empty CDF for use below\n"
"      $ vim my_varnames.conf\n"
"\n"
"      Run as needed to produce output files:\n"
"      $ cat my_data.d2s | " PROG " -m my_varnames.conf -s my_metadata.cdf -o ./\n"
"\n"
);

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

typedef struct program_options{
	bool bRmFirst;       /* remove output before writing */
	bool bUncompressed;  /* don't compress data */
	bool bNoIstp;        /* Don't automatical add some ISTP metadata */
	bool bFilterVars;    /* Only works if a var-map file is present */
	size_t uMemThreshold; 
	char aTpltFile[256]; /* Template CDF */
	char aMapFile[256];  /* Variable name mappings */
	char aSource[1024];  /* Input source, http://, file:// etc. */
	char aOutFile[256];  /* Non-filter: output */
	char aTmpDir[256];   /* Filter mode: temp dir */
	char aLevel[32];    
	char aCredFile[256];
} popts_t;

int parseArgs(int argc, char** argv, popts_t* pOpts)
{
	memset(pOpts, 0, sizeof(popts_t));  /* <- Defaults struct values to 0 */
	pOpts->bRmFirst = false;
	pOpts->bUncompressed = false;
	pOpts->uMemThreshold = DEF_FLUSH_BYTES;

	char sMemThresh[32] = {'\0'};

	/* Set a few defaults */
	snprintf(
		pOpts->aCredFile, DAS_FIELD_SZ(popts_t, aCredFile) - 1, "%s" DAS_DSEPS DEF_AUTH_FILE,
		das_userhome()
	);

	strcpy(pOpts->aLevel, "info");

	snprintf(
		pOpts->aTmpDir, DAS_FIELD_SZ(popts_t, aTmpDir) - 1, "%s" DAS_DSEPS ".cdftmp",
		das_userhome()
	);

	int i = 0;
	while(i < (argc-1)){
		++i;  /* 1st time, skip past the program name */

		if(argv[i][0] == '-'){
			if(dascmd_isArg(argv[i], "-h", "--help", NULL)){
				prnHelp();
				exit(0);
			}
			if(dascmd_isArg(argv[i], "-r", "--remove", NULL)){
				pOpts->bRmFirst = true;
				continue;
			}
			if(dascmd_isArg(argv[i], "-n", "--no-istp", NULL)){
				pOpts->bNoIstp = true;
				continue;
			}
			if(dascmd_isArg(argv[i], "-u", "--uncompressed", NULL)){
				pOpts->bUncompressed = true;
				continue;
			}
			if(dascmd_isArg(argv[i], "-f", "--filter-vars", NULL)){
				pOpts->bFilterVars = true;
				continue;
			}
			if(dascmd_getArgVal(
				sMemThresh,       32,                          argv, argc, &i, "-b", "--buffer="
			))
				continue;
			if(dascmd_getArgVal(
				pOpts->aTpltFile, DAS_FIELD_SZ(popts_t,aTpltFile), argv, argc, &i, "-s", "--skeleton="
			))
				continue;
			if(dascmd_getArgVal(
				pOpts->aSource,   DAS_FIELD_SZ(popts_t,aSource),   argv, argc, &i, "-i", "--input="
			))
				continue;
			if(dascmd_getArgVal(
				pOpts->aOutFile,  DAS_FIELD_SZ(popts_t,aOutFile),  argv, argc, &i, "-o", "--output="
			))
				continue;
			if(dascmd_getArgVal(
				pOpts->aTmpDir,   DAS_FIELD_SZ(popts_t,aTmpDir),   argv, argc, &i, "-t", "--temp-dir="
			))
				continue;
			if(dascmd_getArgVal(
				pOpts->aMapFile,  DAS_FIELD_SZ(popts_t,aMapFile),  argv, argc, &i, "-m", "--map-vars="
			))
				continue;
			if(dascmd_getArgVal(
				pOpts->aLevel,    DAS_FIELD_SZ(popts_t,aLevel),    argv, argc, &i, "-l", "--log="
			))
				continue;
			if(dascmd_getArgVal(
				pOpts->aCredFile, DAS_FIELD_SZ(popts_t,aCredFile), argv, argc, &i, "-a", "--auth-toks="
			))
				continue;
			return das_error(PERR, "Unknown command line argument %s", argv[i]);
		}
		return das_error(PERR, "Malformed command line argument %s", argv[i]);
	}

	float fMemUse;
	if(sMemThresh[0] != '\0'){
		if((strncmp(sMemThresh, "inf", 3)==0)||(strcmp(sMemThresh, "∞") == 0)){
			pOpts->uMemThreshold = (sizeof(size_t) == 4 ? 0xFFFFFFFF : 0xFFFFFFFFFFFFFFuLL);
		}
		else{
			if((sscanf(sMemThresh, "%f", &fMemUse) != 1)||(fMemUse < 1))
				return das_error(PERR, "Invalid memory usage argument, '%s' MB", sMemThresh);
			else
				pOpts->uMemThreshold = ((size_t)fMemUse) * 1048576ull ;
		}
	}

	if(pOpts->bFilterVars && (pOpts->aMapFile[0] = '\0')){
		return das_error(PERR, "Filtering out \"data\" varibles via '-f', requires a map file, '-m'.");
	}

	return DAS_OKAY;
}

/* ************************************************************************* */
/* Variable Name Maps */

typedef struct var_name_map {
	int  nPktId;
	char sDimName[DAS_MAX_ID_BUFSZ];
	char sVarRole[DASDIM_ROLE_SZ];
	char sOldCdfName[MAX_VAR_NAME_LEN];
	char sOutName[MAX_VAR_NAME_LEN];
} var_name_map_t;

/* Load a variable name mapping, last entry is null for sentienal 
 *
 * The expected line name pattern is:
 *
 *     cdf_name = [pkt id] dimension [role]
 *
 * Only the dimension name is required.
 */
var_name_map_t* loadVarMap(const char* sFile)
{
	daslog_info_v("Reading variable name map from %s", sFile);

	FILE* pIn = fopen(sFile, "r");
	if(pIn == NULL){
		das_error(PERR, "Couldn't open variable name map file '%s'.", sFile);
		return NULL;
	}
	daslog_debug_v("Reading variable map from '%s'", sFile);

	var_name_map_t* pMap = NULL; /* Can now use the error goto */

	/* Read once for number of name mappings */
	char* pSep = NULL;
	int iLine = 0;
	int nMaps = 0;
	char aBuf[VAR_MAP_MAX_LINE] = {'\0'};
	char* pStrip = NULL;
	size_t uLen = 0;
	while(fgets(aBuf, VAR_MAP_MAX_LINE - 1, pIn) != NULL){
		++iLine;

		if(aBuf[VAR_MAP_MAX_LINE - 3] != '\0'){
			das_error(PERR, 
				"%s,line %d: Line greater then %d octets.",sFile, iLine, VAR_MAP_MAX_LINE-3
			);
			goto VAR_MAP_ERROR;
		}

		if((pStrip = das_strip(aBuf, '#')) == NULL) 
			continue;

		uLen = strlen(pStrip);
		if((uLen < 3) || ((pSep = strchr(pStrip+1, '=')) == NULL) || (pStrip[uLen - 1] == '=')){
			das_error(PERR, "%s, line %d: Syntax error, missing `=` as separator",sFile,iLine);
			goto VAR_MAP_ERROR;
		}

		++nMaps;
	}

	if(nMaps == 0){
		das_error(PERR, 
			"Variable map file '%s' doesn't have any 'CDF_NAME = DAS_NAME pairs", sFile
		);
		goto VAR_MAP_ERROR;
	}

	/* Read again to make the mapping buffer, over-allocate to get NULL map at end */
	rewind(pIn);
	pMap = (var_name_map_t*) calloc(nMaps+1, sizeof(var_name_map_t));

	int iMap = 0;
	char* pCdfName = NULL;
	char* pDasPath = NULL;
	unsigned short uDasId = 0;
	iLine = 0;
	while(fgets(aBuf, VAR_MAP_MAX_LINE - 1, pIn) != NULL){
		++iLine;

		if((pCdfName = das_strip(aBuf, '#')) == NULL)
			continue;

		pSep = strchr(pCdfName, '=');
		*pSep = '\0';
		pCdfName = das_strip(pCdfName, '\0');
		strncpy(pMap[iMap].sOutName, pCdfName, MAX_VAR_NAME_LEN-1);

		/* This can have up to four sub fields */
		pDasPath = das_strip(pSep + 1, '\0'); 

		/* See if the value starts with a digit.  If so assume it's a packet ID */
		if(pDasPath[0] == '*'){
			pMap[iMap].nPktId = 0; /* Match any packet ID */
			pSep = strchr(pDasPath + 1, ' ');
		}
		else{
			if(!isdigit(pDasPath[0]) ){
				das_error(PERR, 
					"%s, line %d: Packet ID is not `*` or an integer", sFile, iLine
				);
				goto VAR_MAP_ERROR;
			}

			if((pSep = strchr(pDasPath, ' ')) == NULL){
				das_error(PERR, 
					"%s, line %d: Packet ID not followed by a dimension name", sFile, iLine
				);
				goto VAR_MAP_ERROR;
			}
			*pSep = '\0';
			
			if( (sscanf(pDasPath, "%hu", &uDasId) != 1) ||(uDasId == 0)){
				das_error(PERR, 
					"%s, line %d: Could not convert '%s' to a packet ID (aka 16-bit integer > 0).",
					sFile, iLine
				);
				goto VAR_MAP_ERROR;
			}
			pMap[iMap].nPktId = uDasId;
		}

		pDasPath = das_strip(pSep + 1, '\0');

		/* Split off role if appended at the end, or just use 'center' */
		if((pSep = strchr(pDasPath, ' ')) == NULL){
			das_error(PERR,
				"%s, line %d: Missing variable role name.", sFile, iLine
			);
		}

		*pSep = '\0';
		pDasPath = das_strip(pDasPath, '\0');
		strncpy(pMap[iMap].sDimName, pDasPath, DAS_MAX_ID_BUFSZ-1);

		pDasPath = das_strip(pSep + 1, '\0');

		/* There may be one or two strings left */
		if((pSep = strchr(pDasPath, ' ')) == NULL){
			/* No cdf var, just a role */
			pDasPath = das_strip(pDasPath, '\0');
			strncpy(pMap[iMap].sVarRole, pDasPath, DASDIM_ROLE_SZ-1);
		}
		else{
			*pSep = '\0';
			strncpy(pMap[iMap].sVarRole, pDasPath, DASDIM_ROLE_SZ-1);
			pDasPath = das_strip(pSep + 1, '\0');
			strncpy(pMap[iMap].sOldCdfName, pDasPath, MAX_VAR_NAME_LEN-1);
		}
		
		daslog_debug_v("Var Name Map: (%d %s %s [%s]) => %s",
			pMap[iMap].nPktId,
			pMap[iMap].sDimName,
			pMap[iMap].sVarRole,
			pMap[iMap].sOldCdfName
		);
		++iMap;
	}

	fclose(pIn);
	return pMap;

VAR_MAP_ERROR:
	fclose(pIn);
	if(pMap != NULL)
		free(pMap);
	return NULL;
}

const char* _VarNameMap_newName(
	const var_name_map_t* pMap, int nPktId, const DasVar* pVar
){
	if(pMap == NULL) return NULL;

	const DasDim* pDim = (DasDim*) (((DasDesc*)pVar)->parent);
	if(pDim == NULL)
		return NULL;

	assert( ((DasDesc*)pDim)->type == PHYSDIM);

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

	/* First try for a structure match */
	const var_name_map_t* pIter = pMap;

	while((pIter != NULL)&&(pIter->sOutName[0] != '\0')){
		if((strcmp(pIter->sDimName, DasDim_id(pDim)) == 0)&&
		   (strcmp(pIter->sVarRole, sRole) == 0)&&
		   ((pIter->nPktId == 0)||(nPktId < 1)||(pIter->nPktId == nPktId))
		){

			if(pIter->sOldCdfName[0] != '\0'){
				const char* sPropVal = DasDesc_getStr((const DasDesc*)pVar, "cdfName");
				if( (sPropVal != NULL )&&( strcmp(pIter->sOldCdfName, sPropVal) == 0) ) {
					daslog_info_v("Mapping variable %s %s:%s [%s] -to-> %s",
						(pIter->nPktId == 0) ? "for all packets with" : "for matching packet IDs",
						pIter->sDimName, pIter->sVarRole, pIter->sOldCdfName, pIter->sOutName
					);
					return pIter->sOutName;
				}
			}
			else{

				if(pIter->nPktId > 0)
					daslog_info_v("For dataset ID %02d, mapping \"%s:%s\" -to-> \"%s\"",
						nPktId, pIter->sDimName, pIter->sVarRole, pIter->sOutName
					);
				else
					daslog_info_v("For all datasets, mapping dimension %s:%s -to-> %s",
						pIter->sDimName, pIter->sVarRole, pIter->sOutName
					);
				return pIter->sOutName;
			}
		}
		++pIter;
	}

	/* That was a flop. See if the variable (or it's dimension) have 
	   a cdfName property, if so use that for the matching source */
	const char* sPropVal = DasDesc_getStr((const DasDesc*)pVar, "cdfName");
	if(sPropVal == NULL) 
		return NULL;

	pIter = pMap;
	while((pIter != NULL)&&(pIter->sOutName[0] != '\0')){
		if(strcmp(pIter->sDimName, sPropVal) == 0){
			daslog_info_v("Mapping variable name %s to %s", sPropVal, pIter->sDimName);
			return pIter->sOutName;
		}
		++pIter;
	}
	daslog_debug_v("No remapping for var: %s:%s", DasDim_id(pDim), sRole);
	return NULL;
}

/* ************************************************************************* */

struct context {
	bool bCompress;
	bool bIstp;        /* output some ITSP metadata (or don't) */
	uint64_t nRecsOut;  /* Track how many record varying rows were written */
	size_t uFlushSz;   /* How big to let internal memory grow before a CDF flush */
	CDFid nCdfId;
	char* sTpltFile;  /* An empty template CDF to put data in */
	char* sWriteTo;
	var_name_map_t* pVarMap;  /* For filtering/renaming variables on write */
	bool bFilterVars;

	/* DasTime dtBeg; */      /* Start point for initial query, if known */
	/* double rInterval; */   /* Size of original query, if known */
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
#define CDF_MAD( SOME_CDF_FUNC ) ( ((iStatus = (SOME_CDF_FUNC) ) != CDF_OK) && (!_cdfOkayish(iStatus)) )


/* ************************************************************************* */
/* Converting Das Properties to CDF properties */

/* Buffers for property conversion */

#define PROP_XFORM_SZ 65536  /* 64K */

ubyte g_propBuf[PROP_XFORM_SZ];

const char* DasProp_cdfName(const DasProp* pProp)
{
	/* Translate some of the common das property names to CDF names */
	const char* sName = DasProp_name(pProp);

	if(strcmp(sName, "label"      ) == 0) return "LABLAXIS";
	if(strcmp(sName, "description") == 0) return "FIELDNAM";  /* Common das2 property */
	if(strcmp(sName, "title"      ) == 0) return "FIELDNAM";
	if(strcmp(sName, "summary"    ) == 0) return "CATDESC";
	if(strcmp(sName, "info"       ) == 0) return "VAR_NOTES";
	if(strcmp(sName, "notes"      ) == 0) return "VAR_NOTES";

	if(strcmp(sName, "frame"      ) == 0) return "REFERENCE_FRAME";

	if(strcmp(sName, "fill"       ) == 0) return "FILLVAL";
	if(strcmp(sName, "format"     ) == 0) return "FORMAT";
	if(strcmp(sName, "info"       ) == 0) return "VAR_NOTES";
	
	if(strcmp(sName, "nominalMin" ) == 0) return "LIMITS_NOMINAL_MIN";
	if(strcmp(sName, "nominalMax" ) == 0) return "LIMITS_NOMINAL_MAX";
	if(strcmp(sName, "scaleMin"   ) == 0) return "SCALEMIN";
	if(strcmp(sName, "scaleMax"   ) == 0) return "SCALEMAX";
	if(strcmp(sName, "scaleType"  ) == 0) return "SCALETYP";
	
	if(strcmp(sName, "validMin"   ) == 0) return "VALIDMIN";
	if(strcmp(sName, "validMax"   ) == 0) return "VALIDMAX";
	if(strcmp(sName, "warnMin"    ) == 0) return "LIMITS_WARN_MIN";
	if(strcmp(sName, "warnMax"    ) == 0) return "LIMITS_WARN_M";

	if(strcmp(sName, "compLabel") == 0) return NULL;  /* Eat some properties */

	return sName;
}

const char* DasProp_cdfGlobalName(const DasProp* pProp)
{
	/* Make sure some common variable property names are not used in 
	   the global section, since CDF has only one property namespace */
	const char* sName = DasProp_name(pProp);

	/* Converts some das dataset level properties to CDF global names */
	if(strcmp(sName, "summary"           ) == 0) return "TEXT";
	if(strcmp(sName, "info"              ) == 0) return "TEXT";
	if(strcmp(sName, "title"             ) == 0) return "TITLE";
	if(strcmp(sName, "label"             ) == 0) return "TITLE";

	/* Otherwise, just make sure we don't accidentally step on variable attributes */
	if(strcmp(sName, "CATDESC"           ) == 0) return "G_CATDESC";
	if(strcmp(sName, "FILLVAL"           ) == 0) return "G_FILLVAL";
	if(strcmp(sName, "FORMAT"            ) == 0) return "G_FORMAT";
	if(strcmp(sName, "VAR_NOTES"         ) == 0) return "G_VAR_NOTES";
	if(strcmp(sName, "LABLAXIS"          ) == 0) return "G_LABLAXIS";
	if(strcmp(sName, "LIMITS_NOMINAL_MIN") == 0) return "G_LIMITS_NOMINAL_MIN";
	if(strcmp(sName, "LIMITS_NOMINAL_MAX") == 0) return "G_LIMITS_NOMINAL_MAX";
	if(strcmp(sName, "SCALEMIN"          ) == 0) return "G_SCALEMIN";
	if(strcmp(sName, "SCALEMAX"          ) == 0) return "G_SCALEMAX";
	if(strcmp(sName, "SCALETYP"          ) == 0) return "G_SCALETYP";
	if(strcmp(sName, "VAR_NOTES"         ) == 0) return "G_VAR_NOTES";
	if(strcmp(sName, "FIELDNAM"          ) == 0) return "G_FIELDNAM";
	if(strcmp(sName, "VALIDMIN"          ) == 0) return "G_VALIDMIN";
	if(strcmp(sName, "VALIDMAX"          ) == 0) return "G_VALIDMAX";
	if(strcmp(sName, "LIMITS_WARN_MIN"   ) == 0) return "G_LIMITS_WARN_MIN";
	if(strcmp(sName, "LIMITS_WARN_M"     ) == 0) return "G_LIMITS_WARN_M";

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

/* Make logging output more readable */
const char* cdfTypeStr(long nCdfType){
	switch(nCdfType){
		case CDF_INT1:        return "CDF_INT1";
		case CDF_INT2:        return "CDF_INT2";
		case CDF_INT4:        return "CDF_INT4";
		case CDF_INT8:        return "CDF_INT8";
		case CDF_UINT1:       return "CDF_UINT1";
		case CDF_UINT2:       return "CDF_UINT2";
		case CDF_UINT4:       return "CDF_UINT4";
		case CDF_REAL4:       return "CDF_REAL4";
		case CDF_REAL8:       return "CDF_REAL8";
		case CDF_EPOCH:       return "CDF_EPOCH";	/* Standard style. */
		case CDF_EPOCH16:     return "CDF_EPOCH16";	/* Extended style. */
		/* One more style with leap seconds and J2000 base time. */
		case CDF_TIME_TT2000: return "CDF_TIME_TT2000";	
		case CDF_BYTE:        return "CDF_BYTE";   /* same as CDF_INT1 (signed) */
		case CDF_FLOAT:       return "CDF_FLOAT";  /* same as CDF_REAL4 */
		case CDF_DOUBLE:      return "CDF_DOUBLE"; /* same as CDF_REAL8 */
		case CDF_CHAR:        return "CDF_CHAR";  /* a "string" data type */
		case CDF_UCHAR:       return "CDF_UCHAR"; /* a "string" data type */
		default: return "CDF_UNKNOWN";
	}
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

/* Function is NOT MULTI-THREAD SAFE (not that we care here) */
void* DasProp_cdfValues(const DasProp* pProp){
	size_t uBufLen = 0;
	const char* sValue = NULL;

	ubyte uType = (DasProp_type(pProp) & DASPROP_TYPE_MASK);
	
	switch(uType){

	/* For strings this is easy, others have to be parsed */
	case DASPROP_STRING:
		sValue = DasProp_value(pProp);
		if((sValue == NULL)||(sValue[0] == '\0'))
			sValue = " ";
		return (void*) sValue;

	/* Properties don't have fill, so an unsigned byte works */
	case DASPROP_BOOL:
		uBufLen = PROP_XFORM_SZ;
		if(DasProp_convertBool(pProp, g_propBuf, uBufLen) < 1)
			return NULL;
		else
			return g_propBuf;

	case DASPROP_INT:
		uBufLen = PROP_XFORM_SZ / sizeof(int64_t);
		if(DasProp_convertInt(pProp, (int64_t*)g_propBuf, uBufLen) < 1)
			return NULL;
		else
			return g_propBuf;

	case DASPROP_REAL:     
		uBufLen = PROP_XFORM_SZ / sizeof(double);
		if(DasProp_convertReal(pProp, (double*)g_propBuf, uBufLen) < 1)
			return NULL;
		else
			return g_propBuf;
		
	case DASPROP_DATETIME:
		uBufLen = PROP_XFORM_SZ / sizeof(int64_t);
		if(DasProp_convertTt2k(pProp, (int64_t*)g_propBuf, uBufLen) < 1)
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

DasErrCode writeGlobalProp(struct context* pCtx, const DasProp* pProp)
{	
	CDFstatus iStatus = CDF_OK; /* Also used by CDF_MAD macro */

	const char* sName = NULL;
	long iAttr = 0;

	const char* sFilterOut[] = {
		"LABEL", "VAR_NOTES", "FIELDNAM", "CATDESC", NULL
	};

	long n = DasProp_cdfEntries(pProp);
	for(long iEntry = 0; iEntry < n; ++iEntry){

		sName = DasProp_cdfGlobalName(pProp);

		/* Prop filtering,
		   1. For global props that start with inst just skip past that part 
		   2. After that if a prop doesn't start with a papitol letter ignore it
		   3. Some 
		*/
		if((strncmp(sName, "inst", 4) == 0)&&(sName[4] != '\0'))
			sName += 4;

		if( (sName[0] != toupper(sName[0])) && (strncmp(sName, "spase", 5)!=0) ){
			daslog_debug_v("Ignoring lower-case property '%s' in global area.", sName);
			return DAS_OKAY;
		}

		/* Some props just don't go in the global area */
		for(int j = 0; sFilterOut[j] != NULL; ++j){
			if(strcasecmp(sFilterOut[j], sName) == 0){
				daslog_debug_v("Ignoring property %s is the global area", sName);
				return DAS_OKAY;
			}
		}
		if((strstr(sName, "ContactEmail") != NULL)||(strstr(sName, "ContactName") != NULL)){
			daslog_debug_v("Ignoring property %s is the global area", sName);
			return DAS_OKAY;
		}
	
		/* Get attribute number or make a new (why can't CDFlib use "const", 
		   is it really so hard? */
		if((iAttr = CDFattrId(pCtx->nCdfId, sName)) <= 0){
			daslog_info_v(
				"Auto global attribute %s (%s)", sName, cdfTypeStr(DasProp_cdfType(pProp))
			);
			if(CDF_MAD(CDFcreateAttr(pCtx->nCdfId, sName, GLOBAL_SCOPE, &iAttr)))
				return PERR;
		}

		iStatus = CDFputAttrgEntry(
			pCtx->nCdfId, 
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

DasErrCode writeVarProp(struct context* pCtx, long iVarNum, const DasProp* pProp)
{
	CDFstatus iStatus; /* Used by CDF_MAD macro */

	const char* sName = DasProp_cdfName(pProp);

	/* If return null, the DasProp_cdfName ate the property */
	if(sName == NULL)
		return DAS_OKAY;

	long iAttr = CDFattrId(pCtx->nCdfId, sName);
	long nScope = 0L; /* from cdf.h, this is NO_SCOPE */
	if(iAttr >= 0){
		if(CDF_MAD(CDFgetAttrScope(pCtx->nCdfId, iAttr, &nScope)))
			return PERR;
		if(nScope != VARIABLE_SCOPE){
			return das_error(PERR, 
				"CDF Limitiation: attribute name '%s' cannot be used for variables"
				" because it's already a global attribute.", sName
			);
		}
	}

	/* If the attribute doesn't exist in var scope, we'll need to create it first */
	if((iAttr < 0)){
		daslog_info_v(
			"Auto variable attribute %s (%s)", sName, cdfTypeStr(DasProp_cdfType(pProp))
		);
		if(CDF_MAD(CDFcreateAttr(pCtx->nCdfId,sName,VARIABLE_SCOPE,&iAttr)))
			return PERR;
	}

	/* Handle an asymmetry in CDF attributes */
	long nElements = DasProp_items(pProp);
	if(DasProp_cdfType(pProp) == CDF_UCHAR)
		nElements = strlen(DasProp_value(pProp));

	/* Hook in spots for debugging */
	long nType = DasProp_cdfType(pProp);
	void* pVal = DasProp_cdfValues(pProp);

	daslog_debug_v("New attribute entry for varible #%ld, %s (attrid: %ld attrtype %ld)",
		iVarNum, sName, iAttr, nType
	);

	if(CDF_MAD(CDFputAttrzEntry(
		pCtx->nCdfId, 
		iAttr,
		iVarNum,
		nType,
		nElements,
		pVal
	)))
		return PERR;

	return DAS_OKAY;
}

DasErrCode writeVarStrAttr(
	struct context* pCtx, long iVarNum, const char* sAttrName, const char* sValue
){
	CDFstatus iStatus; /* Used by CDF_MAD macro */

	/* CDF doesn't like empty strings, and prefers a single space
	   instead of a zero length string */
	if((sValue == NULL)||(sValue[0] == '\0'))
		sValue = " "; 

	/* If the attribute doesn't exist, we'll need to create it first */
	long iAttr;
	if((iAttr = CDFattrId(pCtx->nCdfId, sAttrName)) < 0){
		daslog_info_v("Auto variable attribute %s (%s)", sAttrName, cdfTypeStr(CDF_UCHAR));
		if(CDF_MAD(CDFcreateAttr(pCtx->nCdfId, sAttrName, VARIABLE_SCOPE, &iAttr )))
			return PERR;
	}

	daslog_debug_v("Writing attribute %s (attrid: %ld attrtype:%ld) for variable #%ld", 
		sAttrName, iAttr, CDF_UCHAR, iVarNum
	);

	if(CDF_MAD(CDFputAttrzEntry(
		pCtx->nCdfId,
		iAttr,
		iVarNum,
		CDF_UCHAR, 
		(long) strlen(sValue),
		(void*)sValue
	)))
		return PERR;
	else
		return DAS_OKAY;
}

DasErrCode writeVarAttr(
	struct context* pCtx, long iVarNum, const char* sAttrName, long nCdfType,
	const ubyte* pValue
){
	CDFstatus iStatus; /* Used by CDF_MAD macro */

	if(pValue == NULL)
		return das_error(PERR, "No fill value supplied");

	if((nCdfType == CDF_CHAR)||(nCdfType == CDF_UCHAR))
		return das_error(PERR, "Call writeVarStrAttr for the string attribute '%s'", sAttrName);

	/* If the attribute doesn't exist, we'll need to create it first */
	long iAttr;
	if((iAttr = CDFattrId(pCtx->nCdfId, sAttrName)) < 0){
		if(CDF_MAD(CDFcreateAttr(pCtx->nCdfId, sAttrName, VARIABLE_SCOPE, &iAttr )))
			return PERR;
	}

	daslog_debug_v("Writing attribute %s (attrid: %ld attrtype:%ld) for variable #%ld", 
		sAttrName, iAttr, nCdfType, iVarNum
	);

	if(CDF_MAD(CDFputAttrzEntry(
		pCtx->nCdfId,
		iAttr,
		iVarNum,
		nCdfType, 
		1L,
		(void*)pValue
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
#ifndef _WIN32		
		if(!das_copyfile(pCtx->sTpltFile, pCtx->sWriteTo, NEW_FILE_MODE)){
#else
		if(!das_copyfile(pCtx->sTpltFile, pCtx->sWriteTo)){
#endif
			das_error(PERR, "Couldn't open copy '%s' --to--> '%s'", 
				pCtx->sTpltFile, pCtx->sWriteTo
			);
			return PERR;
		}

		if(pDot != NULL) *pDot = '\0';

		if(CDF_MAD( CDFopenCDF(pCtx->sWriteTo, &(pCtx->nCdfId))) ){
			*pDot = '.'; /* Convert back */
			return das_error(PERR, "Couldn't open CDF file '%s'", pCtx->sWriteTo);
		}
	}
	else{
		/* Create a new file */
		if(pDot != NULL) *pDot = '\0';

		if(CDF_MAD( CDFcreateCDF(pCtx->sWriteTo, &(pCtx->nCdfId)) ) ){
			*pDot = '.'; /* Convert back */
			return das_error(PERR, "Couldn't open CDF file '%s'", pCtx->sWriteTo);
		}
	}

	if(pDot != NULL) *pDot = '.';  /* But our damn dot back */

	if(pCtx->bIstp){
		if(!DasDesc_has((DasDesc*)pSd, "Data_version"))
			DasDesc_setInt((DasDesc*)pSd, "Data_version", 1);
		if(!DasDesc_has((DasDesc*)pSd, "Generation_date")){
			das_time dt;
			dt_now(&dt);
			char sTime[32] = {'\0'};
			snprintf(sTime, 31, "%04d%02d%02d", dt.year, dt.month, dt.mday);
		}
	}

	/* We have the file, run in our properties */
	size_t uProps = DasDesc_length((DasDesc*)pSd);
	for(size_t u = 0; u < uProps; ++u){
		const DasProp* pProp = DasDesc_getPropByIdx((DasDesc*)pSd, u);
		if(pProp == NULL) continue;

		/* Some properties are meta-data controllers */
		if(strcmp(DasProp_name(pProp), "cdfName") == 0)
			continue;

		if(writeGlobalProp(pCtx, pProp) != DAS_OKAY)
			return PERR;
	}

	/* If there are any coordinate frames defined in this stream, say 
	   something about them here */
	if(StreamDesc_getNumFrames(pSd) > 0){
		daslog_error("TODO: Write stream vector frame info to CDF global attributes.");
	}
	
	return DAS_OKAY;
}

/* ************************************************************************* */
/* Dependency Solver:
   
   ISTP CDFs like to associate one physical dimension with one array index, and 
   even one plot axis.  In fact it's one of thier definining limitations. Das3
   datasets do not fall into this trap, and instead de-couple array indexes from
   both physical dimensions and plotting axes. But CDFs are the "law of the land".
   So, to try and live within ISTP constraints, we have... The Dependency Solver.
   --cwp

   Guiding principles:

      1. There must be one dependency for each index (aka array dimension)

      2. Associate a different physDim with each dependency.

      3. Choose time before other physDims if it's an option.

      4. Only point variables and point offsets may be selected as dependencies.

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
		DasDim* pDim = DasDs_getDimByIdx(pDs, uD, DASDIM_COORD);
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

	/* Skip this.  CDF likes to have lot's of little arrays marking
	   each array dimension.  It doesn't have a concept of 
	   DEPEND_1&2 (aka 2 deps satisfied at once )

	for(int iDep = 1; iDep < nDsRank; ++iDep){
		VarInfo* pViOff = VarInfoAry_getDepN(aVarInfo, uInfos, iDep);
		assert(pViOff != NULL);

		if(strcmp(pViOff->sRole, DASVAR_OFFSET) != 0)
			continue;

		/ * We want the associated reference... * /
		VarInfo* pViRef = VarInfoAry_getByRole(aVarInfo, uInfos, pViOff->pDim, DASVAR_REF);

		/ * ... but skip creation if a center var already exists * /
		VarInfo* pViCent = VarInfoAry_getByRole(aVarInfo, uInfos, pViOff->pDim, DASVAR_CENTER);

		if((pViRef == NULL)||(pViCent != NULL))
			continue;
		
		/ * Make a new variable combining the reference and the offset and
		   substitue this in for the dependency IF we aren't time. * /
		VarInfo* pViNew = (aVarInfo + uInfos);
		pViNew->bCoord = true;
		pViNew->pVar = new_DasVarBinary(DASVAR_CENTER, pViRef->pVar, "+", pViOff->pVar);
		
		/ * Give the new var to the dimension * /
		DasDim_addVar(pViOff->pDim, DASVAR_CENTER, pViNew->pVar);
		pViNew->pDim = pViOff->pDim;
		
		strncpy(pViNew->sDim,  pViOff->sDim, DAS_MAX_ID_BUFSZ - 1);
		strncpy(pViNew->sRole, DASVAR_CENTER, DASDIM_ROLE_SZ - 1);

		DasVar_shape(pViNew->pVar, pViNew->aVarShape);
		pViNew->iMaxIdx = _maxIndex(pViNew->aVarShape);


		pViNew->iDep = pViOff->iDep;  / * Give dep role to new variable * /
		pViOff->iDep = -1; 
		++uInfos;
	}

	*/

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

	/* All calendar times will be converted to TT2000 by the CDF writer */
	if(Units_haveCalRep(DasVar_units(pVar)))
		return CDF_TIME_TT2000;
	
	/* For other variables, we want the underlying type */
	return aCdfType[DasVar_elemType(pVar)];
}

/* Make a simple name for a variable */
const char* DasVar_cdfName(
	DasDim* pDim, const DasVar* pVar, char* sBuf, size_t uBufLen,
	var_name_map_t* pMap, int nPktId
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

	/* First try to get a match based off of the packet ID, dimension name and 
	   variable use */
	const char* sMatch = _VarNameMap_newName(pMap, nPktId, pVar);
	if(sMatch != NULL){
		memset(sBuf, 0, uBufLen);
		strncpy(sBuf, sMatch, uBufLen - 1);
		return sBuf;
	}

	/* If this dim has a CDF_NAME property, then use it for the role */
	const DasProp* pOverride = DasDesc_getLocal((DasDesc*)pDim, "cdfName");
	if(pOverride){

		/* TODO: 
		   The proper way to do this is to put properties on variables directly by
		   adding var_prop_t() to the DB.  Then getting the varible name would 
		   just be:

		      DasDesc_get((DasDesc*)pVar, "cdfName");

		   no role lookup or any of that stuff.

		   BUT! This would change all the queries in dastelem, it would change the
		   config.json output and it would change the codec.d.  That's a lot of 
		   breakage, so put it off until the next version.

		   For now just hack-in reference,offset handling for time and don't 
		   worry about it for other variables.
		*/
		sRole = DasProp_value(pOverride);
	}

	DasVar* pPtVar = DasDim_getPointVar(pDim);

	/* Handle time special, this is a HACK that needs to disappear! */
	if( (pDim->dtype == DASDIM_COORD)&&(strcmp(DasDim_dim(pDim), "time") == 0)){
		if((pVar == pPtVar) || (pVar == DasDim_getVar(pDim, DASVAR_REF)) )
			strncpy(sBuf, "Epoch", uBufLen - 1);
		else if(pVar == DasDim_getVar(pDim, DASVAR_OFFSET))
			strncpy(sBuf, "timeOffset", uBufLen - 1);

		return sBuf;
	}

	if(pPtVar == pVar){
		/* Check to see if this variable has a given CDF name.  Use if for the
		 * center variable only */
		const DasProp* pOverride = DasDesc_getLocal((DasDesc*)pDim, "cdfName");
		if(pOverride)
			snprintf(sBuf, uBufLen - 1, "%s", DasProp_value(pOverride));
		else
			snprintf(sBuf, uBufLen - 1, "%s", DasDim_id(pDim));
	}
	else{
		snprintf(sBuf, uBufLen - 1, "%s_%s", DasDim_id(pDim), sRole);
	}

	return sBuf;
}

/* Make a flattened namespace name for a variable.  If the variable already 
 * exists in the CDF, the sufficies are added until it's unique
 */
const char* DasVar_cdfUniqName(
	struct context* pCtx, DasDim* pDim, const DasVar* pVar, char* sBuf, size_t uBufLen
){

	CDFid nCdfId = pCtx->nCdfId;

	DasDs* pDs = (DasDs*) DasDesc_parent((DasDesc*)pDim);
	int nPktId = 0;
	if(pCtx->pVarMap != NULL)
		nPktId = DasStream_getPktId((DasStream*)DasDesc_parent((DasDesc*)pDs), (DasDesc*)pDs);

	/* Start with the short name, that may be enough */
	DasVar_cdfName(pDim, pVar, sBuf, uBufLen, pCtx->pVarMap, nPktId);

	if( CDFconfirmzVarExistence(nCdfId, sBuf) != CDF_OK )
		return sBuf;

	/* Okay that's not unique. Prepend the dataset group name and see if that
	   gets it. */
	size_t uSz = DAS_MAX_ID_BUFSZ * 2;
	char sLocal[DAS_MAX_ID_BUFSZ * 2] = {'\0'};
	snprintf(sLocal, uSz - 1, "%s_%s", sBuf, DasDs_group(pDs));

	if( CDFconfirmzVarExistence(nCdfId, sLocal) != CDF_OK ){
		strncpy(sBuf, sLocal, uBufLen - 1);
		return sBuf;
	}

	/* We'll, add the DS ID, that will force it to be unique */
	memset(sLocal, 0, uSz);
	snprintf(sLocal, uSz - 1, "%s_%s_%s", sBuf, DasDs_id(pDs), DasDs_group(pDs));
	strncpy(sBuf, sLocal, uBufLen - 1);
	return sBuf;
}


/* Sequences pour themselves into the shape of the containing dataset
   so the dataset shape is needed here.  Vectors with just a single 
   component have the internal index dropped
*/
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

	/* For vectors we need to add in the number of components */
	if(DasVar_valType(pVar) == vtGeoVec){
		ptrdiff_t aIntr[DASIDX_MAX] = {0};
		int nIntrRank = DasVar_intrShape(pVar, aIntr);
		for(int i = 0; i < nIntrRank; ++i){
			if(aIntr[i] > 1){
				pNonRecDims[nUsed] = aIntr[i];
				++nUsed;
			}
		}
	}

	return nUsed;
}

/** create a unique variable in the cdf output file 
 * 
 * @param[out] sNmaeBuf a buffer to reciver the variable name must point to at least
 *        DAS_MAX_ID_BUFSZ bytes of space
 */
DasErrCode makeCdfVar(
	struct context* pCtx, DasDim* pDim, DasVar* pVar, int nDsRank, ptrdiff_t* pDsShape,
	char* sNameBuf
){
	ptrdiff_t aMin[DASIDX_MAX] = {0};
	ptrdiff_t aMax[DASIDX_MAX] = {0};

	long aNonRecDims[DASIDX_MAX] = {0};
	/* Sequence variables mold themselvse to the shape of the containing dataset so
	   the dataset shape has to be passed in a well */
	long nNonRecDims = DasVar_cdfNonRecDims(nDsRank, pDsShape, pVar, aNonRecDims);
	if(nNonRecDims < 0)
		return PERR;

	/* Create the associated varyances array */
	long nRecVary = DasVar_degenerate(pVar, 0) ? NOVARY : VARY;
	long aDimVary[DASIDX_MAX - 1] = {NOVARY,NOVARY,NOVARY,NOVARY,NOVARY,NOVARY,NOVARY};
	for(int i = 0; i < nNonRecDims; ++i){
		if(aNonRecDims[i] > 0)
			aDimVary[i] = VARY;
	}

	/* Attach a small var_cdf_info_t struct to the variable to track the 
	   variable ID as well as the last written record index */
	DasVar_addCdfInfo(pVar);

	/* Make a name for this variable, since everything is flattened */
	DasVar_cdfUniqName(pCtx, pDim, pVar, sNameBuf, DAS_MAX_ID_BUFSZ - 1);

	/* If this var is to be interpreted as a text value, we'll need strlen */
	long nCharLen = 1L;
	if(DasVar_cdfType(pVar) == CDF_UCHAR){
		ptrdiff_t aIntr[DASIDX_MAX] = {0};
		DasVar_intrShape(pVar, aIntr);
		nCharLen = aIntr[0];
	}

	daslog_info_v("Auto variable %s", sNameBuf);

	CDFstatus iStatus = CDFcreatezVar(
		pCtx->nCdfId,                               /* CDF File ID */
		sNameBuf,                                   /* Varible's name */
		DasVar_cdfType(pVar),                       /* CDF Data type of variable */
		nCharLen,                                   /* Character length, if needed */
		nNonRecDims,                                /* collapsed rank after index 0 */
		aNonRecDims,                                /* collapsed size in each index, after 0 */
		nRecVary,                                   /* True if varies in index 0 */
		aDimVary,                                   /* Array of varies for index > 0 */
		DasVar_cdfIdPtr(pVar)                       /* The ID of the variable created */
	);
	if(!_cdfOkayish(iStatus))
		return PERR;

	/* If the data types is not TT2000 or doesn't start with 'Epoch' go ahead
	   and compress it if we're able */
	if( (pCtx->bCompress) && (DasVar_cdfType(pVar) != CDF_TIME_TT2000)
		&& ( strncasecmp(sNameBuf, "epoch", 5) != 0)
	){
		long cType = GZIP_COMPRESSION;
		long cParams[CDF_MAX_PARMS];
		cParams[0] = 6L;
		if(CDF_MAD(CDFsetzVarCompression(
			pCtx->nCdfId, DasVar_cdfId(pVar), cType, cParams
		)))
			return PERR;
	}

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
	ptrdiff_t aTmp[DASIDX_MAX] = DASIDX_INIT_UNUSED;
	int nAryRank = DasAry_shape(pAry, aAryShape);

	size_t uLen = 0;
	das_val_type vt = DasAry_valType(pAry);
	const ubyte* pVals = DasAry_getIn(pAry, vt, DIM0, &uLen);

	/* Put index information into data types needed for function call */
	static const long indicies[DASIDX_MAX]  = {0,0,0,0, 0,0,0,0};
	long counts[DASIDX_MAX]                 = {0,0,0,0, 0,0,0,0};
	static const long intervals[DASIDX_MAX] = {1,1,1,1, 1,1,1,1};

	/* shave off any length 1 indexes after the first when saving to CDF */
	int iDimOut = 0;
	for(int iDimIn = 0; iDimIn < nAryRank; ++iDimIn){
		if((iDimIn > 0) && (aAryShape[iDimIn] == 1)) continue;
		counts[iDimOut] = aAryShape[iDimIn];
		++iDimOut;
	}

	iStatus = CDFhyperPutzVarData(
		pCtx->nCdfId, /* CDF File ID */
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

/* ************************************************************************* */
/* Writing Label Properties */

DasErrCode makeCompLabels(struct context* pCtx, DasDim* pDim, DasVar* pVar)
{
	DasStream* pSd = (DasStream*) DasDesc_parent((DasDesc*)DasDesc_parent((DasDesc*)pDim));

	long iStatus; /* Used by CDF_MAD macro */
	DasErrCode nRet = DAS_OKAY;

	char psBuf[3][32] = {'\0'};
	char* ptrs[3] = {&(psBuf[0][0]), &(psBuf[1][0]), &(psBuf[2][0]) };
	int nComp = das_makeCompLabels(pVar, (char**) ptrs, 31); 
	if(nComp < 0)
		return -1 * nComp; 

	/* Find out how big the largest one is */
	int nMaxCompLen = 0;
	for(int i = 0; i < nComp; ++i){
		int nLen = strlen(psBuf[i]);
		if(nLen > nMaxCompLen) nMaxCompLen = nLen;
	}

	/* If there's only one component, short cut this branch and just make a
	   regular label */
	if(nComp == 1){
		return writeVarStrAttr(pCtx, DasVar_cdfId(pVar), "LABEL", psBuf[0]);
	}

	/* Get the primary variable's name */
	char sVarName[CDF_VAR_NAME_LEN256] = {'\0'};
	if(CDF_MAD(CDFgetzVarName(pCtx->nCdfId, DasVar_cdfId(pVar), sVarName)))
		return PERR;

	/* Make the pointer variable name */
	char sLblVarName[CDF_VAR_NAME_LEN256] = {'\0'};
	snprintf(sLblVarName, CDF_VAR_NAME_LEN256 - 1, "%s_comp_lbl", sVarName);

	long nLblVarId = 0;
	long nDimVary = VARY;
	long nNumComp = nComp; /* Store the byte in a long */
	if(CDF_MAD(CDFcreatezVar(
		pCtx->nCdfId,   /* CDF File ID */
		sLblVarName,    /* label varible's name */
		CDF_UCHAR,       /* CDF type of variable data */
		nMaxCompLen,    /* Character length */
		1,              /* We have 1 non-record dim */
		&nNumComp,      /* Number of components in first non-record dim */
		NOVARY,         /* Not a record varing variable */
		&nDimVary,      /* Varys in non-record dim 1 */
		&nLblVarId      /* Get the new var's ID */
	)))
		return PERR;

	/* Now write in the labels */
	long nDimIndices = 0;
	if(CDF_MAD(CDFsetzVarSeqPos(pCtx->nCdfId, nLblVarId, 0, &nDimIndices)))
		return PERR;

	char sCompBuf[32] = {'\0'};
	for(int i = 0; i < nComp; ++i){
		memset(sCompBuf, ' ', 32); 
		sCompBuf[31] = '\0';
		strncpy(sCompBuf, psBuf[i], strlen(psBuf[i]) > 31 ? 31 : strlen(psBuf[i]));
		if(CDF_MAD(CDFputzVarSeqData(pCtx->nCdfId, nLblVarId, sCompBuf)))
			return PERR;
	}

	nRet = writeVarStrAttr(pCtx, nLblVarId, "VAR_TYPE", "metadata");
	if(nRet != DAS_OKAY) return nRet;

	/* and make labels for the label variable */
	char sBuf[256] = {'\0'};
	snprintf(sBuf, 255, "%s component labels", sVarName);

	nRet = writeVarStrAttr(pCtx, nLblVarId, "CATDESC", sBuf);

	/* And finally, set the lable pointer for the main variable, the index it's a label
	   for is always the last one. */
	int iLblIdx = 1;
	const DasAry* pAry = DasVar_getArray(pVar);
	if(pAry == NULL)
		return das_error(PERR, "Vector variable in %s is not backed by an array", DasDim_id(pDim));
	
	iLblIdx = DasAry_rank(pAry) - 1;
	memset(sBuf, 0, 256);
	snprintf(sBuf, 32, "LABL_PTR_%d", iLblIdx);
	return writeVarStrAttr(pCtx, DasVar_cdfId(pVar), sBuf, sLblVarName);
}

/* ************************************************************************ */

DasErrCode writeVarProps(
	struct context* pCtx, DasDim* pDim, DasVar* pVar, VarInfo* pCoords, size_t uCoords
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

	/* There could be holes in the indexes used by the dataset, so 
	   collapse the deps numbers */
	int iDep = -1;
	for(int i = 0; i <= iIdxMax; ++i)
		if((aVarShape[i] > -1)&&(i != iAmDep)) ++iDep;

	for(int iIdx = iIdxMax; iIdx >= 0; --iIdx){

		/* Either not record varying, or not affected by this index */
		if(DasVar_degenerate(pVar, 0)||DasVar_degenerate(pVar, iIdx))
			continue;

		if(iIdx == iAmDep)
			continue;

		/* Find the dependency for my current index */
		for(size_t u = 0; u < uCoords; ++u){
			if((pCoords + u)->iDep == iIdx){
				snprintf(sAttrName, 15, "DEPEND_%d", iDep);
				--iDep;
				writeVarStrAttr(
					pCtx, 
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

	writeVarStrAttr(pCtx, DasVar_cdfId(pVar), "UNITS", sUnits);

	if(pDim->dtype == DASDIM_COORD)
		writeVarStrAttr(pCtx, DasVar_cdfId(pVar), "VAR_TYPE", "support_data");
	else
		writeVarStrAttr(pCtx, DasVar_cdfId(pVar), "VAR_TYPE", "data");

	/* Handle the component labels for vectors */
	DasErrCode nRet;
	if(DasVar_valType(pVar) == vtGeoVec)
		if( (nRet = makeCompLabels(pCtx, pDim, pVar)) != DAS_OKAY)
			return nRet;

	/* If I'm a point variable assign properties to me, worry about
	 * others later (this is going to be a problem) */
	if(DasDim_getPointVar(pDim) == pVar){

		size_t uProps = DasDesc_length((DasDesc*)pDim);
		for(size_t u = 0; u < uProps; ++u){
			const DasProp* pProp = DasDesc_getPropByIdx((DasDesc*)pDim, u);
			if(pProp == NULL) continue;

			if(strcmp(DasProp_name(pProp), "cdfName") == 0)
				continue;

			if(writeVarProp(pCtx, DasVar_cdfId(pVar), pProp) != DAS_OKAY)
				return PERR;
		}
	}

	/* If this is an array var, get the fill value and make a property for it 
	   but only if we're NOT a coordinate! */
	DasAry* pAry = DasVarAry_getArray(pVar);
	if((pDim->dtype == DASDIM_DATA) && (pAry != NULL)){
		const ubyte* pFill = DasAry_getFill(pAry);
		nRet = writeVarAttr(pCtx, DasVar_cdfId(pVar), "FILLVAL", DasVar_cdfType(pVar), pFill);
		if(nRet != DAS_OKAY)
			return nRet;
	}

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

DasErrCode onDataSet(StreamDesc* pSd, int iPktId, DasDs* pDs, void* pUser)
{
	struct context* pCtx = (struct context*)pUser;


	ptrdiff_t aDsShape[DASIDX_MAX] = DASIDX_INIT_UNUSED;
	int nDsRank = DasDs_shape(pDs, aDsShape);

	daslog_info_v("Creating variables for dataset %s,%s", DasDs_group(pDs), DasDs_id(pDs));

	if(daslog_level() < DASLOG_INFO){
		char sBuf[16000] = {'\0'};
		DasDs_toStr(pDs, sBuf, 15999);
		daslog_info(sBuf);
	}

	/* Send Data set properties to the global attribute space */
	size_t uProps = DasDesc_length((DasDesc*)pDs);
	for(size_t u = 0; u < uProps; ++u){
		const DasProp* pProp = DasDesc_getPropByIdx((DasDesc*)pDs, u);
		if(pProp == NULL) continue;

		if(strcmp(DasProp_name(pProp), "cdfName") == 0)
			continue;

		if(writeGlobalProp(pCtx, pProp) != DAS_OKAY)
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
			pCtx, pVi->pDim, pVi->pVar, nDsRank, aDsShape, pVi->sCdfName
		)) != DAS_OKAY)
			return nRet;

		nRet = writeVarProps(pCtx, pVi->pDim, pVi->pVar, pCdfCoords, uCoords);
		if(nRet != DAS_OKAY)
			return nRet;
	}

	/* Now for the data variables... */
	size_t uDims = DasDs_numDims(pDs, DASDIM_DATA);
	char sNameBuf[DAS_MAX_ID_BUFSZ] = {'\0'};
	
	for(size_t d = 0; d < uDims; ++d)
	{
		DasDim* pDim = (DasDim*) DasDs_getDimByIdx(pDs, d, DASDIM_DATA);
		size_t uVars = DasDim_numVars(pDim);
		for(size_t v = 0; v < uVars; ++v){
			DasVar* pVar = (DasVar*) DasDim_getVarByIdx(pDim, v);

			nRet = makeCdfVar(pCtx, pDim, pVar, nDsRank, aDsShape, sNameBuf);
			if(nRet != DAS_OKAY)
				return nRet;

			nRet = writeVarProps(pCtx, pDim, pVar, pCdfCoords, uCoords);
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

const ubyte* _structToTT2k(const ubyte* pData, size_t uTimes)
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

const ubyte* _valueToTT2k(
	const ubyte* pData, size_t uTimes, das_val_type vt, das_units units
){
	if(g_uTimeBufLen < uTimes){
		if(g_pTimeValBuf != NULL){
			free(g_pTimeValBuf);
		}
		g_pTimeValBuf = (int64_t*)calloc(uTimes, sizeof(int64_t));
	}

	/* Just handle doubles for now, that's the most common time type */
	const double* pDblSrc = NULL;
	switch(vt){
	case vtDouble:
		/* TODO: Check endianness here! */
		pDblSrc = (const double*)pData;
		for(size_t u = 0; u < uTimes; ++u){
			g_pTimeValBuf[u] = das_us2K_to_tt2K(Units_convertTo(UNIT_US2000, pDblSrc[u], units));
		}
		return (const ubyte*)g_pTimeValBuf;

	default:
		das_error(DASERR_NOTIMP, "Add conversion for epoch based from type %s", das_vt_toStr(vt));
		return NULL;
	}
}

DasErrCode _writeRecVaryAry(struct context* pCtx, DasVar* pVar, DasAry* pAry)
{
	CDFstatus iStatus; /* Used by the CDF_MAD macro */

	CDFid nCdfId = pCtx->nCdfId;

	/* It's possible that we didn't get any data, for example when
	   a header is sent, but no actual values.  If so just return okay.
	*/
	if(DasAry_size(pAry) == 0){
		daslog_debug_v("No more data to write for array %s", DasAry_id(pAry));
		return DAS_OKAY;
	}

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

	/* Hook in time conversion conversion.  If we see vtTime, that's a structure, and
	   it needs to be re-written to TT2K, if we see */

	if(DasAry_valType(pAry) == vtTime)
		pData = _structToTT2k(pData, uElements);
	else if( 
		Units_haveCalRep(DasAry_units(pAry)) && 
		(DasAry_valType(pAry) != vtLong) && 
		(DasAry_units(pAry) != UNIT_TT2000)
	)
		pData = _valueToTT2k(pData, uElements, DasAry_valType(pAry), DasAry_units(pAry));

	int nRank = DasAry_shape(pAry, aShape);

	uTotal = aShape[0];
	for(int r = 1; r < nRank; ++r){
		counts[r-1] = aShape[r];
		uTotal *= aShape[r];
	}

	assert(uTotal == uElements);

	if(CDF_MAD(CDFhyperPutzVarData(
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

	pCtx->nRecsOut += aShape[0];

	return DAS_OKAY;
}

DasErrCode putAllData(struct context* pCtx, int nDsRank, ptrdiff_t* pDsShape, DasVar* pVar)
{
	/* Take a short cut for array variables */
	if(DasVar_type(pVar) == D2V_ARRAY){

		DasAry* pAry = DasVarAry_getArray(pVar); /* Does not copy data */
		assert(pAry != NULL);

		if(_writeRecVaryAry(pCtx, pVar, pAry) != DAS_OKAY)
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

		if(_writeRecVaryAry(pCtx, pVar, pAry) != DAS_OKAY)
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
	daslog_info_v("Writing %zu records for dataset %s,%s", 
		aDsShape[0], DasDs_group(pDs), DasDs_id(pDs)
	);

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

				if(putAllData(pCtx, nDsRank, aDsShape, pVar) != DAS_OKAY)
					return PERR;
			}
		}
	}

	/* Now clear all the record-varying arrays in the dataset.  Use the flag
	 * we set earlier in the array to know if it's record varying or not */
	size_t uArrays = DasDs_numAry(pDs);
	for(size_t uAry = 0; uAry < uArrays; ++uAry){
		DasAry* pAry = DasDs_getAry(pDs, uAry);

		if(DasAry_getUsage(pAry) & DASARY_REC_VARY)
			DasAry_clear(pAry);
	}

	return DAS_OKAY;
}

/* ************************************************************************* */

DasErrCode onData(StreamDesc* pSd, int iPktId, DasDs* pDs, void* pUser)
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

	if(DasDs_memUsed(pDs) > pCtx->uFlushSz)
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

DasErrCode onComment(OobComment* pComment, void* pUser)
{
	/* Don't care about comments, for now */
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

/* helpers ****************************************************************** */

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
	ctx.bCompress = !opts.bUncompressed;
	ctx.uFlushSz = opts.uMemThreshold;
	ctx.bIstp = !opts.bNoIstp;
	ctx.pVarMap = NULL;
	ctx.bFilterVars = opts.bFilterVars;

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

	/* Load the variable name mappings, and set var filter flag */
	if(opts.aMapFile[0] != '\0'){
		if( (ctx.pVarMap = loadVarMap(opts.aMapFile)) == NULL)
			return PERR;    /* ^-sets error message for us */
	}

	/* Build one of 4 types of stream readers */
	DasCredMngr* pCreds = NULL;
	DasHttpResp res; memset(&res, 0, sizeof(DasHttpResp));
	DasIO* pIn = NULL;

	if(opts.aSource[0] == '\0'){ /* Reading from standard input */
		pIn = new_DasIO_cfile(PROG, stdin, "r");

		/* If reading from standard input, an we need a name, just use the current time */
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
		
		char sUrl[ DAS_FIELD_SZ(popts_t,aSource) ] = {'\0'};
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

	DasIO_model(pIn, 3);        /* <-- Read <packet>s but model <dataset>s */

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
	handler.commentHandler    = onComment;
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
