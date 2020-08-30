/* Copyright (C) 2004-2018  Chris Piker  <chris-piker@uiowa.edu>
 *                          Jeremy Faden <jeremy-faden@uiowa.edu>
 *                          Edward West  <edward-west@uiowa.edu>
 *                          
 * This file is part of libdas2, the Core Das2 C Library.
 * 
 * Libdas2 is free software; you can redistribute it and/or modify it under
 * the terms of the GNU Lesser General Public License version 2.1 as published
 * by the Free Software Foundation.
 *
 * Libdas2 is distributed in the hope that it will be useful, but WITHOUT ANY
 * WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS
 * FOR A PARTICULAR PURPOSE.  See the GNU Lesser General Public License for
 * more details.
 *
 * You should have received a copy of the GNU Lesser General Public License
 * version 2.1 along with libdas2; if not, see <http://www.gnu.org/licenses/>. 
 */


#define _POSIX_C_SOURCE 200112L

/* File:   das1ToDas2.c
 * Author: Jeremy, Ed, Chris
 *
 * Created on March 19, 2004, 12:37 PM
 * 
 * Modified on October 20, 2004 (eew)
 *    -yUnits can be specified on the command line.
 *
 * Modified on Nov. ??, 2013 (cwp), added byte swapping for linux version
 *
 * Modified on Dec. 10, 2014 (cwp), updated to match refactored Das2StreamUtil
 *     library
 * 
 * Heavily Modified Jan. 16, 2014(cwp)
 *    1. Added help text
 *    2. Added support for X,Y,Z streams
 *    3. Added support for non-time X axis
 *    4. Got rid of Perl dependency
 *    5. Added coping of more meta-data from DSDF to das2 stream
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <ctype.h>

#ifdef _WIN32
#define strcasecmp _stricmp
#define popen      _popen
#define pclose     _pclose
#else
#include <strings.h>
#endif

#include <das2/core.h>


const char* g_sMyName = NULL;           /* Prog name */
double g_xBaseUs2000 = -1.0;            /* Start time in us2000 (if needed)   */

char* startTime = NULL;
char* endTime = NULL;
char* params = NULL;
const char* g_sIdlBin = NULL;           /* Location of IDL binary, if needed */

/* ************************************************************************* */
/* Is this an Ephemeris Reader or not */
bool requiresInterval(DasDesc* pDsdf)
{
	if( ! DasDesc_has(pDsdf, "form") ) return false;
	if( strcmp(DasDesc_get(pDsdf, "form" ), "x_multi_y") != 0 ) return false;
	if( ! DasDesc_has(pDsdf, "items") ) return false;
	
	return true;
}

/* ************************************************************************* */
/* Converting DSDFs to Packet Structure */

PlaneDesc* _mkXPlane(DasDesc* pSd, DasDesc* pDsdf, const char* sDsdfFile)
{
	const char* pXName = NULL;
	const char* pXUnit = NULL;
	DasEncoding* pEnc = NULL;
	das_units     xUnits = NULL;
	char sXLabel[128] = {'u','n','l','a','b','e','l','e','d','\0'};
	PlaneDesc* pX = NULL;
	
	if( (pXName = DasDesc_get(pDsdf, "x_parameter")) == NULL) pXName = "time";
	if( (pXUnit = DasDesc_get(pDsdf, "x_unit")) == NULL) pXUnit = "s";
	
	if(strcasecmp(pXName, "time") == 0){
		pEnc = new_DasEncoding(DAS2DT_HOST_REAL, 8, NULL);
		xUnits = UNIT_US2000;
		strcpy(sXLabel, "Time");
	}
	else{
		pEnc = new_DasEncoding(DAS2DT_HOST_REAL, 4, NULL);
		xUnits = Units_fromStr(DasDesc_get(pDsdf, "x_unit"));
		if(DasDesc_get(pDsdf, "x_unit") != NULL)
			snprintf(sXLabel, 128, "%s (%s)",  pXName, DasDesc_get(pDsdf, "x_unit"));
		else
			strcpy(sXLabel, pXName);
	}

	pX = new_PlaneDesc(PT_X, NULL, pEnc, xUnits);
	
	sXLabel[0] = toupper(sXLabel[0]);  /* Bill likes initial caps on labels */
	DasDesc_set(pSd, "String", "xLabel", sXLabel);
	return pX;
}

const char* _nameFromLbl( const char* pLabel, char* sName, size_t uLen){
	
	if(pLabel == NULL) return NULL;
	
	size_t uLblLen = strlen(pLabel);
	size_t v = 0;
	bool bInFmt = false;
	bool bOutUpper = true;
	for(size_t u = 0; u < uLblLen; u++ ){
		
		if(bInFmt){
			if(pLabel[u] == 'N') bOutUpper = true;
			else bOutUpper = false;
			bInFmt = false;
		}
		else{
			if(pLabel[u] == '!'){ 
				bInFmt = true;
			}
			else{
				if(bOutUpper) sName[v] = toupper(pLabel[u]);
				else sName[v] = tolower(pLabel[u]);
				v++;
				if(v == uLen - 1) break;
			}
		}
	}
	
	sName[v] = '\0';
	return sName;
}

/* The stream descriptor is used to add some labels into the stream header,
 * nothing more. */
PktDesc* createPktDesc(
	DasDesc* pSd, DasDesc* pDsdf, const char* sDsdfFile
){	
	PktDesc* pPkt = NULL;
	PlaneDesc* pPlane = NULL;
	DasEncoding* pEnc4 = new_DasEncoding(DAS2DT_HOST_REAL, 4, NULL);
	const char* sForm = NULL;
	
	das_units     yUnits = NULL;
	const char* pYName = NULL;
	char sYLabel[128] = {'u','n','l','a','b','e','l','e','d','\0'};
	
	int nItems = 0;
	char sKey[64]  = {'\0'};
	const char* pLabel = NULL;
	char sYName[128] = {'\0'};
	
	das_units     zUnits = NULL;
	const char* pZName = NULL;
	char sZLabel[128] = {'u','n','l','a','b','e','l','e','d','\0'};
	
	double* pYTags = NULL;
	size_t  uYTags = 0;
	
	/* Handle X Axis and label*/
	pPkt = new_PktDesc();
	if((pPlane = _mkXPlane(pSd, pDsdf, sDsdfFile)) == NULL) return NULL;
	PktDesc_addPlane(pPkt, pPlane);
	
	
	/* Trigger making the various Y descriptors off the form value */
	if(!DasDesc_has(pDsdf, "form")){
		das_error(112, "ERROR: form parameter not specified in %s", sDsdfFile);
		return NULL;
	}
	sForm = DasDesc_get(pDsdf, "form" );
	
	
	/* Generic Y Stuff */
	yUnits = Units_fromStr(DasDesc_get(pDsdf, "y_unit"));
	if((pYName = DasDesc_get(pDsdf, "y_parameter")) == NULL) pYName = "y";
	if(DasDesc_get(pDsdf, "y_unit") != NULL)
		snprintf(sYLabel, 128, "%s (%s)", pYName, DasDesc_get(pDsdf, "y_unit"));
	else
		strncpy(sYLabel, pYName, 128);
	sYLabel[0] = toupper(sYLabel[0]);  /* Bill likes initial caps on labels */
	DasDesc_set(pSd, "String", "yLabel", sYLabel);
	
	/* X-MULIT-Y */
	if(strcmp(sForm, "x_multi_y") == 0){
		if(DasDesc_has( pDsdf, "items" )){
			nItems = DasDesc_getInt(pDsdf, "items" );
		} 
		else{
			if(DasDesc_has(pDsdf, "ny")){
	        nItems = DasDesc_getInt( pDsdf, "ny" );
			}
			else{
				das_error(112, "[%s] ERROR: Can't determine number of Y vectors from"
						     "DSDF file %s", g_sMyName, sDsdfFile);
				return NULL;
			}
	   }
		
		if(nItems == 1){
			pPlane = new_PlaneDesc(PT_Y, pYName, DasEnc_copy(pEnc4), yUnits);
			PktDesc_addPlane(pPkt, pPlane);
		}
		else{
			for(int i = 0; i < nItems; i++){
				sprintf(sKey, "label(%d)", i);
				pLabel = DasDesc_get(pDsdf, sKey);
				
				if(_nameFromLbl(pLabel, sYName, 64) == NULL)
					snprintf(sYName, 63, "plane_%d", i );
				
				pPlane = new_PlaneDesc(PT_Y, sYName, DasEnc_copy(pEnc4), yUnits);
				
				if(pLabel != NULL)
					DasDesc_set((DasDesc*)pPlane, "String", "yLabel", pLabel);
				
				PktDesc_addPlane(pPkt, pPlane);
			}
		}
		return pPkt;
	}
	
	
	/* Generic Z Stuff */
	zUnits = Units_fromStr(DasDesc_get(pDsdf, "z_unit"));
	if((pZName = DasDesc_get(pDsdf, "z_parameter")) == NULL) pZName = "z";
	if(DasDesc_get(pDsdf, "z_unit") != NULL)
		snprintf(sZLabel, 128, "%s (%s)", pYName, DasDesc_get(pDsdf, "z_unit"));
	else
		strncpy(sZLabel, pZName, 128);
	sZLabel[0] = toupper(sZLabel[0]);  /* Bill likes initial caps on labels */
	DasDesc_set(pSd, "String", "zLabel", sZLabel);
	
	/* X-TAGGED-Y-SCAN */
	if(strcmp(sForm, "x_tagged_y_scan") == 0){	
		if(!DasDesc_has(pDsdf, "y_coordinate")){
			das_error(112, "y_coordinate missing in dsdf file %s", sDsdfFile);
			return NULL;
		}
		pYTags = dsdf_valToArray(DasDesc_get(pDsdf, "y_coordinate"), &uYTags);
		pPlane = new_PlaneDesc_yscan(pZName, DasEnc_copy(pEnc4), zUnits, uYTags, 
		                             NULL, pYTags, yUnits);
		DasDesc_set((DasDesc*)pPlane, "String", "yLabel", sYLabel);
		DasDesc_set((DasDesc*)pPlane, "String", "zLabel", sZLabel);
		PktDesc_addPlane(pPkt, pPlane);
		return pPkt;
	}
	
	/* X-Y-Z SCATTER */
	if(strcmp(sForm, "x_y_z") == 0){
		pPlane = new_PlaneDesc(PT_Y, pYName, DasEnc_copy(pEnc4), yUnits);
		PktDesc_addPlane(pPkt, pPlane);
	
		pPlane = new_PlaneDesc(PT_Z, pZName, DasEnc_copy(pEnc4), zUnits);
		PktDesc_addPlane(pPkt, pPlane);
		return pPkt;
	}

	das_error(112, "Couldn't determine the packet layout: form = '%s'", sForm);
	return NULL;
}

void addStreamProps(const DasDesc* pDsdf, StreamDesc* pSdOut)
{
	size_t uProps = DasDesc_length(pDsdf);
	const char* sVal = NULL;
	das_units unit = NULL;
	DasDesc* pSh = (DasDesc*)pSdOut;
	
	const char* psIgnore[20];
	psIgnore[0] = "form";
	psIgnore[1] = "reader";
	psIgnore[2] = "x_parameter";
	psIgnore[3] = "x_unit";
	psIgnore[4] = "y_parameter";
	psIgnore[5] = "y_unit";
	psIgnore[6] = "y_coordinate";
	psIgnore[7] = "z_parameter";
	psIgnore[8] = "z_unit";
	psIgnore[9] = "items";
	psIgnore[10] = "ny";
	psIgnore[11] = "label(0)";
	psIgnore[12] = "label(1)";
	psIgnore[13] = "label(2)";
	psIgnore[14] = "label(3)";
	psIgnore[15] = "label(4)";
	psIgnore[16] = "label(5)";
	psIgnore[17] = "format";
	psIgnore[18] = "exampleRange";
	psIgnore[19] = "exampleInterval";
	
	/* All */
	size_t u = 0;
	const char* sName = NULL;
	size_t v = 0;
	bool bIgnore = false;
	for(u = 0; u < uProps; u++){
		bIgnore = false;
		if( (sName = DasDesc_getNameByIdx(pDsdf, u)) == NULL) continue;
		for(v = 0; v < 18; v++){
			if(strcmp(sName, psIgnore[v]) == 0){
				bIgnore = true;
				break;
			}
		}
		
		if(bIgnore) continue;
		
		sVal = DasDesc_getValByIdx(pDsdf, u);
		
		if(strcmp(sName, "x_sample_width") == 0){
			if( DasDesc_get(pDsdf, "x_unit") != NULL){
				unit = Units_fromStr(DasDesc_get(pDsdf, "x_unit"));
				DasDesc_setDatum(pSh, "xTagWidth", atof(sVal), unit);
			}
			continue;
		}
		if(strcmp(sName, "y_sample_width") == 0){
			if( DasDesc_get(pDsdf, "y_unit") != NULL){
				unit = Units_fromStr(DasDesc_get(pDsdf, "y_unit"));
				DasDesc_setDatum(pSh, "yTagWidth", atof(sVal), unit);
			}
			continue;
		}
		if(strcmp(sName, "y_fill") == 0){
			DasDesc_set(pSh, "double", "yFill", sVal);
			continue;
		}
		if(strcmp(sName, "z_fill") == 0){
			DasDesc_set(pSh, "double", "zFill", sVal);
			continue;
		}
		if(strcmp(sName, "description") == 0){
			DasDesc_set(pSh, "String", "title", sVal);
			continue;
		}
		
		/* And the catch all */
		DasDesc_set(pSh, "String", sName, sVal);
	}
}

/* ************************************************************************* */
/* Drop-in fread replacement that handles byte swapping is needed, assumes
   input is in big-endian format (Das1 is defined to be big endian) 
	
	WARNING: This WILL NOT WORK for structures, but writing binary structures
	         to disk is begging for IOerrors anyway.
	*/
int read_n_swap(void* pDest, size_t nItems, size_t szItem, FILE* pFile)
{
#ifdef HOST_IS_LSB_FIRST
	size_t i = 0;
	size_t j = 0;
	unsigned char abyte;
	char* _pDest = (char*)pDest;

	nItems = fread(pDest, szItem, nItems, pFile);
	
	/* Since 1 byte items result in the inner loop not running, this is 
	   safe for single byte items, though it does waste processor time. */
	
	for(i = 0; i < nItems; i++){
		
		for(j = 0; j < (szItem/2); j++){
		
			/* Save the low byte */
			abyte = 	*(_pDest + i*szItem + j);
			
			/* Copy the high byte low */
			*(_pDest + i*szItem +j) = *(_pDest + (i+1)*szItem - 1 - j);
					
			/* Set a new high byte */
			*(_pDest + (i+1)*szItem - 1 - j) = abyte;
		}
	}
	
	return nItems;
	
#else
	/* If were on a big-endian machine, just use the native call */
	return fread(pDest, szItem, nItems, pFile);
#endif
	
}

/* ************************************************************************* */
/* Sending Das1 data */

FILE* openReader(
	const char* sRdr, const char* sBeg, const char* sEnd, const char* sParam, 
	bool bIsTCA
) {
	char* sCmd = NULL;
	
	FILE* pCmd = NULL;

	size_t uLen = strlen(sRdr) + strlen(sBeg) + strlen(sEnd) + strlen(sParam) + 4;
	sCmd = (char*)calloc(uLen, sizeof(char));

	if (!bIsTCA) 
		sprintf(sCmd, "%s %s %s %s", sRdr, sBeg, sEnd, sParam);
	else 
		sprintf(sCmd, "%s %s %s %s", sRdr, sParam, sBeg, sEnd);

	fprintf(stderr, "[%s] exec: %s\n", g_sMyName, sCmd);
	pCmd = popen(sCmd, "r");
	free(sCmd);
	
	return pCmd;
}

DasErrCode das1ToDas2(FILE* pIn, PktDesc* pPkt, DasIO* pIoOut)
{
	size_t uTotItems = 0;
	bool g_bXIsTime = false;
	size_t uPlane = 0;
	
	float* pVals = NULL;
	size_t uVal = 0;
	
	PlaneDesc* pPlane = NULL;
	size_t uItems = 0;
	size_t u = 0;
	
	int nRet = 0;
	 
	/* Items to read is sum of all plane's items + 1, get type of X axis while
	 * we're iterating */
	uTotItems = 0;
	for(uPlane = 0; uPlane < PktDesc_getNPlanes(pPkt); uPlane++){
		pPlane = PktDesc_getPlane(pPkt, uPlane);
		uTotItems += PlaneDesc_getNItems(pPlane);
		if((PlaneDesc_getType(pPlane) == PT_X) && 
		   (strcmp(PlaneDesc_getUnits(pPlane), UNIT_US2000) == 0) )
			g_bXIsTime = true;
	}

	pVals = (float*)malloc(sizeof(float) * ( uTotItems ));

	while(read_n_swap( pVals, uTotItems,  sizeof(float), pIn)){
		uVal = 0;
		
		for(uPlane = 0; uPlane < PktDesc_getNPlanes(pPkt); uPlane++){
			pPlane = PktDesc_getPlane(pPkt, uPlane);
			uItems = PlaneDesc_getNItems(pPlane);
			
			if((pPlane->planeType == PT_X)&&(g_bXIsTime)){
				PlaneDesc_setValue(pPlane, 0, g_xBaseUs2000 + pVals[uVal] * 1e6);
				uVal++;
			}
			else{
				for(u = 0; u < uItems; u++){
					PlaneDesc_setValue(pPlane, u, pVals[uVal]);
					uVal++;
				}
			}
		}
		if((nRet = DasIO_writePktData(pIoOut, pPkt)) != 0) return nRet;
	}
	
	free(pVals);
	return nRet;
}

/*****************************************************************************/
/* main, and argument helpers */

#define SZ_DSDF_NAME 256
#define SZ_PARAM     256

void prnHelp()
{
	fprintf(stderr, 
"SYNOPSIS:\n"
"   das2_from_das1 - Run a Das1 reader and convert the output to a Das2 Stream\n"
"\n"
"USAGE:\n"
"   das2_from_das1 [-I path] DSDF_FILE BEGIN END [PARAM1 PARAM2 ...]\n"
"\n"
"DESCRIPTION:\n"
"   das2_from_das1 builds a stream header and packet header by reading the\n"
"   the Das 1 set descriptor file DSDF_FILE, it then calls the reader specified\n"
"   in DSDF_FILE.  For each record emitted by the Das1 reader a corresponding\n"
"   Das 2 data packet is emitted.  The following Das 1 form types are supported:\n"
"\n"
"      x_multi_y\n"
"      x_tagged_y_scan\n"
"      x_y_z\n"
"\n"
"  It is assumed that the X parameter emitted by all Das 1 readers is a 4-byte\n"
"  big-endian time value which is an offset in seconds from the BEGIN time.\n"
"\n"
	);
	fprintf(stderr,
"  For non TCA (Ephemeris) readers the PARAM values are simply passed along as\n"
"  extra command line arguments after the BEGIN and END times to the Das 1 \n"
"  reader.  For TCA readers (which are indicated in the DSDF_FILE by the 'items'\n"
"  keyword), at least one PARAM is required and all PARAMs are passed to the\n"
"  reader before the BEGIN and END times.\n"
"\n"
"OPTIONS:\n"
"  -I path   Provide the path to the IDL binary.  IDL is only invoked if the\n"
"            y_coordinate keyword in the DSDF_FILE can't be parsed directly.\n"
"            If IDL is needed but this option is not present up-conversion\n"
"            will fail\n"
"\n"
"LIMITATIONS:\n"
"  The DSDF_FILE parser understands IDL array syntax and handles IDL \n"
"  continuation lines, but it does not implement and expression handling.  Thus\n"
"  DSDF_FILEs with findgen and other IDL functions are not supported.  Such\n"
"  statements are common for the 'y_coordiate' values and must be converted to\n"
"  simple arrays before usage by this converter.\n"
"\n"
"AUTHORS:\n"
"   jeremy-faden@uiowa.edu  (original)\n"
"   chris-piker@uiowa.edu   (current maintainer)\n"
"\n"
"SEE ALSO:\n"
"   das2_ascii, das2_to_das1\n"
"   The das 2 ICD @ http://das2.org for an introduction to the das 2 system.\n"
"\n");
}

void parseArgs(
	int argc, char **argv, char* sDsdfFile, char* sBeg, char* sEnd, char* sParams 
){
	g_sMyName = argv[0];
	
	int i;
	/* Don't look at past arg 4 for -h or --help, they may be args meant for
	   a reader */
	for(i = 0; i < argc && i < 4; i++){
		if(strcmp(argv[i], "-h") == 0 || strcmp(argv[i], "--help") == 0){
			prnHelp();
			exit(0);
		}
		if(strcmp(argv[i], "-v") == 0 || strcmp(argv[i], "--version") == 0){
			printf("$Header: https://saturn.physics.uiowa.edu/svn/das2/core/stable/libdas2_3/utilities/das2_from_das1.c 11440 2019-04-29 09:02:46Z cwp $\n");
			exit(0);
		}

	}
	
	if(argc < 4){
		fprintf(stderr, "Usage: das2_from_das1 [-I idl] DSDF_FILE START END [PARAMS]\n"
		                "Help:  das2_from_das1 -h\n");
		exit(112);
    }
	
	bool bShift = false;
	for(i = 0; i < argc && i < 4; i++){
		if(bShift){
			argv[i-2] = argv[i];
		}
		else{
			if(strcmp("-I", argv[i]) == 0){
				if(i == (argc - 1)){
					fprintf(stderr, "No IDL binary supplied after -I argument, use -h for help");
					exit(13);
				}
				g_sIdlBin = argv[i+1];
				bShift = true;
			}
		}
	}
	if(bShift) argc -= 2;
	
	strncpy(sDsdfFile, argv[1], SZ_DSDF_NAME - 1);
	strncpy(sBeg, argv[2], SZ_PARAM - 1);
	strncpy(sEnd, argv[3], SZ_PARAM - 1);
	
	/* Cram all remaining arguments (if any) into a single parameter, separate
	 * with white space characters */
	char* pWrite = sParams;
	size_t nWritten = 0;
	for(i = 4; i<argc; i++){
		if(nWritten > SZ_PARAM - 3) break;
		if(i > 4){ *pWrite = ' '; pWrite++; nWritten += 1;}
		strncpy(pWrite, argv[i], SZ_PARAM - nWritten - 1);
	}
}

/* ************************************************************************* */
int main(int argc, char** argv) 
{
	int nRet = 0;
	char sDsdfFile[SZ_DSDF_NAME] = {'\0'};
	const char* pSource = NULL;
	char sBeg[SZ_PARAM] = {'\0'};
	char* pBeg = NULL;
	char sEnd[SZ_PARAM] = {'\0'};
	char* pEnd = NULL;
	char sParams[SZ_PARAM] = {'\0'};
	char* pParams = NULL;
	DasIO* pOut = NULL;
	StreamDesc* pSdOut = NULL;
	DasDesc* pDsdf = NULL;
	PktDesc* pPdOut = NULL;
	
	const char* sRdr = NULL;
	FILE* pPipe = NULL;
	bool bReqInterval = false;
   
	/* Exit on errors, log info messages and above */
	das_init(argv[0], DASERR_DIS_EXIT, 0, DASLOG_INFO, NULL);

	parseArgs(argc, argv, sDsdfFile, sBeg, sEnd, sParams); /* May not return */
	
	/* Set the location of the IDL binary, if specified */
	if(g_sIdlBin != NULL) dsdf_setIdlBin(g_sIdlBin);
	
	if( (pDsdf = dsdf_parse( sDsdfFile )) == NULL) return 112;
	
	/* add extra requirements for Das1 dsdfs */
	if(!DasDesc_has(pDsdf, "form"))
		return das_error(112, "Keyword 'form' missing from DSDF file %s", 
				            sDsdfFile);
	
	bReqInterval = requiresInterval(pDsdf);
	
	/* Ephemeris readers require that an interval tag is first.  This is the */
	/* opposite of das2 ephemeris readers, so flop all the arguments around  */
	if(bReqInterval){
		pBeg = sEnd;
		pEnd = sParams;
		pParams = sBeg;
		if(pParams[0] == '\0')
			return das_error(112, "[%s] ERROR: Interval parameter required for "
					            "Ephemeris readers", g_sMyName);
	}
	else{
		pBeg = sBeg;
		pEnd = sEnd;
		pParams = sParams;
	}
	
   pSource = sDsdfFile;
	if(strrchr(sDsdfFile, '/')) pSource = strrchr(sDsdfFile,'/') + 1;
	pOut = new_DasIO_cfile(pSource, stdout, "w");
	pSdOut = new_StreamDesc();
	DasDesc_set( (DasDesc*)pSdOut, "Time", "start", pBeg );
	DasDesc_set( (DasDesc*)pSdOut, "Time", "end", pEnd );
	
	das_time dt = {0};
	if(!dt_parsetime(pBeg, &dt))
		return das_error(112, "[%s] ERROR: Couldn't parse %s as a date time", pBeg);
	
   g_xBaseUs2000 = Units_convertFromDt(UNIT_US2000, &dt);

   if( (pPdOut = createPktDesc((DasDesc*)pSdOut, pDsdf, sDsdfFile)) == NULL)
		return 112;
	StreamDesc_addPktDesc(pSdOut, pPdOut, 1);
	 
	/* Add in stream properties from command line and DSDF */
	DasDesc_set((DasDesc*)pSdOut, "Time", "start", pBeg);
	DasDesc_set((DasDesc*)pSdOut, "Time", "end", pEnd);
	
	addStreamProps(pDsdf, pSdOut);
	 
	DasIO_writeStreamDesc(pOut, pSdOut);
	DasIO_writePktDesc(pOut, pPdOut);
	
	if( (sRdr = DasDesc_get(pDsdf,"reader")) == NULL)
		return das_error(112, "Error in %s, 'reader' value not defined", sDsdfFile);
	
   pPipe = openReader(sRdr, pBeg, pEnd, pParams, bReqInterval);
	
   nRet = das1ToDas2(pPipe, pPdOut, pOut);
	pclose(pPipe);
	return nRet;
}
