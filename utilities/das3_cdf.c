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
#define PROGERR 63

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
"   -i URL,--input=URL\n"
"                 Instead of reading from standard input, read from this URL.\n"
"                 To read from a local file prefix it with 'file://'.  Only\n"
"                 file://, http:// and https:// are supported.\n"
"\n"
"   -o DEST,--output=DEST\n"
"                 Instead of acting as a poorly performing filter, write data\n"
"                 to this location.  If DEST is a file then data will be written\n"
"                 directly to that file, and no temporary file will be created.\n"
"                 If DEST is a directory then an output file will be created\n"
"                 in the directory with an auto-generate filename.  This is\n"
"                 useful when reading from das servers.\n"
"\n"
"   -t DIR,--temp=DIR\n"
"                 CDF files are NOT a streaming format.  In order for " PROG "\n"
"                 to act as a filter it must actually create a file and then\n"
"                 turn around and stream it's contents to stdout. Use this \n"
"                 specify the directory where the temporary file is created.\n"
"\n"
"   -s FILE,--skeleton=FILE\n"
"                 Initialize the output CDF with a skeleton file first.\n"
"                 (experimental)\n"
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

/* Return 0 or 1 on success, -1 on failure */
static int _isArg(
	const char* sArg, const char* sShort, const char* sLong, bool* pLong 
){
	if(strcmp(sArg, sShort) == 0){
		*pLong = false;
		return true;
	}

	size_t uLen = strlen(sLong) + 1;  /* Include the '=' */

	if(strncmp(sArg, sLong, uLen) == 0){
		*pLong = true;
		return true;
	}

	return false;
}

int parseArgs(
	int argc, char** argv, char* sInFile, size_t nInFile, char* sOutFile, 
	size_t nOutFile, char* sTmpDir, size_t nTmpDir, char* sSkelFile, size_t nSkelFile
){
	int i = 0;
	bool bIsLong = false;

	while(i < (argc-1)){
		++i;  /* 1st time, skip past the program name */

		if(argv[i][0] == '-'){
			if(_isArg(argv[i], "-h", "--help", &bIsLong)){
				prnHelp();
				exit(0);
			}

			if(_isArg(argv[i], "-i", "--input", &bIsLong)){
				if(!bIsLong && (i > argc-2)) goto NO_ARG;
				else if(argv[i][8] == '\0') goto NO_ARG;
				strncpy(sInFile, bIsLong ? argv[i] + 8 : argv[i+1], nInFile - 1);
			}

			if(_isArg(argv[i], "-o", "--output", &bIsLong)){
				if(!bIsLong && (i > argc-2)) goto NO_ARG;
				else if(argv[i][9] == '\0') goto NO_ARG;
				strncpy(sOutFile, bIsLong ? argv[i] + 9 : argv[i+1], nOutFile - 1);
			}

			if(_isArg(argv[i], "-t", "--temp", &bIsLong)){
				if(!bIsLong && (i > argc-2)) goto NO_ARG;
				else if(argv[i][7] == '\0') goto NO_ARG;
				strncpy(sTmpDir, bIsLong ? argv[i] + 7 : argv[i+1], nTmpDir - 1);
			}

			if(_isArg(argv[i], "-s", "--skeleton", &bIsLong)){
				if(!bIsLong && (i > argc-2)) goto NO_ARG;
				else if(argv[i][11] == '\0') goto NO_ARG;
				strncpy(sSkelFile, bIsLong ? argv[i] + 11 : argv[i+1], nSkelFile - 1);
			}
		}

		return das_error(PROGERR, "Unknown command line argument %s", argv[i]);
	}

	return DAS_OKAY;
NO_ARG:
	return das_error(PROGERR, "Missing option after argument %s", argv[i]);
}

/* ************************************************************************* */

#define IN_FILE_SZ    1024  /* das URLs can get long */
#define OUT_FILE_SZ   256
#define TMP_DIR_SZ    256
#define SKEL_FILE_SZ  256

int main(int argc, char** argv) {

	/* Exit on errors, log info messages and above */
	das_init(argv[0], DASERR_DIS_EXIT, 0, DASLOG_INFO, NULL);

	char sInFile[IN_FILE_SZ]     = {'\0'};
	char sOutFile[OUT_FILE_SZ]   = {'\0'};
	char sTmpDir[TMP_DIR_SZ]     = {'\0'};
	char sSkelFile[SKEL_FILE_SZ] = {'\0'};
	FILE* pFile = NULL;

	DasErrCode nRet = parseArgs(
		argc, argv, sInFile, IN_FILE_SZ, sOutFile, OUT_FILE_SZ, sTmpDir, TMP_DIR_SZ, 
		sSkelFile, SKEL_FILE_SZ
	);
	if(nRet != DAS_OKAY)
		return 13;

	/* Build one of 4 types of stream readers */
	DasIO* pIn = NULL;

	if(sInFile[0] == '\0'){ /* Reading from standard input */
		pIn = new_DasIO_cfile(PROG, stdin, "r");
	}
	else if(strncmp(sInFile, "https://", 8) == 0){
		TODO;
	}
	else if(strncmp(sInFile, "http://", 7) == 0){
		TODO;
	}
	else{
		// Just a file
		const char* sTmp = sInFile;
		if(strncmp(sInFile, "file://", 7) == 0)
			pTmp = sInFile + 7;
		pInFile = fopen(sTmp, "rb");
		if(pFile == NULL)
			return das_error(PROGERR, "Couldn't open file %s", sTmp);
		pIn = new_DasIO_cfile(PROG, pInFile, "rb");
	}

	DasIO_model(pIn, 3); /* Upgrade any das2 <packet>s to das3 <dataset>s */

	if(pInFile)
		fclose(pInFile);

	return 0;
};