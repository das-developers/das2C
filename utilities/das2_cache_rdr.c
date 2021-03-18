#define _POSIX_C_SOURCE 200112L

#include <locale.h>
#include <stdio.h>
#include <stdbool.h>
#include <string.h>
#include <limits.h>
#include <ctype.h>
#include <strings.h>
#include <math.h>

#include <das2/core.h>
#include <das2/das1.h>

#define P_ERR 92

typedef struct cache_tree{
	double rBinSec;      /* Conversion of binUnits to seconds */
	int nBinSize;        /* Number of binUnits in a single bin */
	das_units binUnits;   /* Typically seconds, but may millisec or other */
	char sStoreMeth[32]; /**/
	char sSubDir[32];    /**/
} cache_tree_t;

#define MAX_TREES 60

#define _QDEF(x) #x
#define QDEF(x) _QDEF(x)


/* ************************************************************************* */
/* Read the cache layout file, order from highest resolution to lowest       */
/* resolution.  Native resolution is listed with nBinSize == 0               */

int _bigBinsFirst(const void* vp1, const void* vp2)
{
	const cache_tree_t* pTree1 = (const cache_tree_t*)vp1;
	const cache_tree_t* pTree2 = (const cache_tree_t*)vp2;
	
	if(pTree1->rBinSec > pTree2->rBinSec) return -1;
	if(pTree1->rBinSec < pTree2->rBinSec) return 1;
	return 0;
}

int readStoreMeth(
	const char* sDsdf, const char* sNormParam, cache_tree_t* pTrees, size_t* pNum
){
	DasDesc* pDsdf = NULL;
	cache_tree_t* pCurTree = NULL;
	size_t uProps = 0;
	size_t u = 0;
	const char* sKey = NULL;
	const char* sTmp = NULL;
	char sVal[256] = {'\0'};
	int i = 0; 
	bool bLookStart = false;
	char* pChar = NULL;
   const char* sItemAry[4] = {NULL};
	int j = 0;
	char sBuf[256] = {'\0'};
	
	if( (pDsdf = dsdf_parse(sDsdf)) == NULL)
		return das_error(P_ERR, "Couldn't prase DSDF file %s", sDsdf);
	
	/* Make one cache tree for each cacheLevel keyword has our paramset */
	
	*pNum = 0;
	pCurTree = pTrees;
	
	uProps = DasDesc_length(pDsdf);
	for(u = 0; u<uProps; u++){
		if( (sKey = DasDesc_getNameByIdx(pDsdf, u)) == NULL) continue;
		if(strstr(sKey, "cacheLevel") == NULL) continue;
		
		if( (sTmp = DasDesc_getValByIdx(pDsdf, u)) == NULL) continue;
		strncpy(sVal, sTmp, 255);
		
		
		/* Break on | chars */
		sItemAry[0]=NULL; sItemAry[1]=NULL; sItemAry[2]=NULL; sItemAry[3]=NULL;
		i = 0;
		bLookStart = true;
		pChar = sVal;
		while(*pChar != '\0' && i < 3){
			if(bLookStart){
				
				if(*pChar == '|')
					return das_error(P_ERR, "Syntax error in %s, keyword %s", sDsdf, sKey);
				
				if(!isspace(*pChar)){	
					sItemAry[i] = pChar;
					i++;
					bLookStart = false;
				}
			}
			
			else{  /* Looking for End */
				if(*pChar == '|'){
					*pChar = '\0';
					bLookStart = true;
				}
			}
			pChar++;
		}
		
		/* Move over the 2nd two items and split the first on the first space */
		sItemAry[3] = sItemAry[2];
		sItemAry[2] = sItemAry[1];
		sItemAry[1] = NULL;

		if( strncasecmp(sItemAry[0], "intrinsic", 9) != 0 ){
			
			pChar = strchr(sItemAry[0], ' ');
			if(pChar == NULL) pChar = strchr(sItemAry[0], '\t');
			if(pChar == NULL) 
				return das_error(P_ERR, "Units missing in dsdf file %s, keyword %s.", sDsdf, sKey);
			while(isspace(*pChar)){
				*pChar = '\0';
				pChar++;
			}
			if(!isalpha(*pChar))
				return das_error(P_ERR, "Units missing in dsdf file %s, keyword %s.", sDsdf, sKey);
			
			sItemAry[1] = pChar;			
		}
		
		/* right-trim the 4 items */
		for(i = 0; i<3; i++){
			if(sItemAry[i] == NULL) continue;
			int nLen = strlen(sItemAry[i]);
			for(j = nLen - 1; j > 0; j--){
				if( isspace(sItemAry[i][j])) ((char*)(sItemAry[i]))[j] = '\0';
				else break;
			}
		}
		
		if(sItemAry[3] == NULL) sItemAry[3] = "_noparam";
		else sItemAry[3] = dsdf_valToNormParam(sItemAry[3], sBuf, 255);
		
		/* Check param match before assiging anything */
		if(strcmp(sItemAry[3], sNormParam) != 0) continue;
		
		
		/* BIN SIZE */
		if(strncasecmp(sItemAry[0], "intrinsic", 9) == 0){
			pCurTree->nBinSize = 0;
		}
		else{
			if((index(sItemAry[0], '.') != NULL)||(index(sItemAry[0], '-') != NULL))
				return das_error(P_ERR, "Illegal BIN size, %s, must be a positive "
				                  "integer", sItemAry[0]);
			
			if(!das_str2int(sItemAry[0], &(pCurTree->nBinSize)) || pCurTree->nBinSize <= 0)
				return das_error(P_ERR, "Can't convert %s to positive integer "
				                  "binsize", sItemAry[0]);
		}
		
		
		/* UNITS */
		pCurTree->binUnits = NULL;
		if(sItemAry[1] != NULL){
			if( (pCurTree->binUnits = Units_fromStr(sItemAry[1])) == NULL)
				return das_error(P_ERR, "Can't convert %s to known units", sItemAry[1]);
		}
		
		/* Convert units to seconds */
		if(pCurTree->binUnits != NULL){
			pCurTree->rBinSec = Units_convertTo(UNIT_SECONDS, pCurTree->nBinSize, pCurTree->binUnits);
		}
		else{
			pCurTree->rBinSec = pCurTree->nBinSize;
			pCurTree->binUnits = UNIT_SECONDS;
		}
									
		
		/* PERIOD */		
		strncpy(pCurTree->sStoreMeth, sItemAry[2], 31); pCurTree->sStoreMeth[31] = '\0';
		
		/* MK-SubDir */
		if(pCurTree->nBinSize == 0){
			strncpy(pCurTree->sSubDir, "intrinsic", 31);
		}
		else{
			snprintf(pCurTree->sSubDir, 31, "bin-%d%s", pCurTree->nBinSize, pCurTree->binUnits);
		}
		
		*pNum += 1;
		pCurTree++;
	}
	
	if(*pNum < 1)
		return das_error(P_ERR, "No cache trees were defined in %s", sDsdf);
	
	/* Sort largest bin size to smallest bin size */
	qsort(pTrees, *pNum, sizeof(cache_tree_t), _bigBinsFirst); 
	return 0;
}

/* ************************************************************************* */
/* Is this cache blocking method time based? */

bool isTimeBased(cache_tree_t* pTrees, size_t uTrees)
{
	for(size_t u = 0; u < uTrees; u++){
		if(pTrees[u].binUnits == NULL){
			if(strcmp(pTrees[u].sStoreMeth, "persecond") == 0) return true;
			if(strcmp(pTrees[u].sStoreMeth, "perminute") == 0) return true;
			if(strcmp(pTrees[u].sStoreMeth, "hourly") == 0) return true;
			if(strcmp(pTrees[u].sStoreMeth, "daily") == 0) return true;
			if(strcmp(pTrees[u].sStoreMeth, "monthly") == 0) return true;
			if(strcmp(pTrees[u].sStoreMeth, "yearly") == 0) return true;
		}
		else{		
			if(Units_canConvert(pTrees[u].binUnits, UNIT_SECONDS)) return true;
		}
	}
	return false;
}

/* ************************************************************************* */
/* Always send something, only call this if no packets have been sent */

typedef struct handler_data {
	bool bHdrSent;
	int nPktsSent;
	bool bXIsTime;
	double rBeg;
	double rEnd;
	DasIO* pOut;
	StreamDesc* pSdOut;
	cache_tree_t* pTree;
} handler_data_t;

void sendNoData(handler_data_t* pHDat){
	if(!pHDat->bHdrSent) DasIO_writeStreamDesc(pHDat->pOut, pHDat->pSdOut);

	OobExcept se = {{0, NULL},NULL,0,NULL,0};
	char sMsg[1024] = {'\0'};
	char sLevel[128] = {'\0'};
	if(pHDat->pTree->nBinSize == 0)
		sprintf(sLevel, "native resolution");
	else
		snprintf(sLevel, 127, "%d %s resolution", pHDat->pTree->nBinSize, 
		        pHDat->pTree->binUnits);
	
	/* Turn off exit-now style error handling in the das2 lib*/
	das_return_on_error();
	
	das_time dtBeg = {0};  char sBeg[64] = {'\0'};
	das_time dtEnd = {0};  char sEnd[64] = {'\0'};
	Units_convertToDt(&dtBeg, pHDat->rBeg, UNIT_US2000);
	Units_convertToDt(&dtEnd, pHDat->rEnd, UNIT_US2000);
	
	if(pHDat->bXIsTime){
		snprintf(
			sMsg, 1023, "No data in the interval %s to %s at %s", 
			dt_isoc(sBeg, 63, &dtBeg, 6), dt_isoc(sEnd, 63, &dtEnd, 6), sLevel
		);
	}
	else{
		snprintf(sMsg, 1023, "No data in the interval %.3e to %.3e at cache level %d %s", 
			      pHDat->rBeg, pHDat->rEnd, pHDat->pTree->nBinSize, sLevel);
	}
	
	OobExcept_set(&se, DAS2_EXCEPT_NO_DATA_IN_INTERVAL, sMsg);
	
	/* Turn on exit-now style error handling */
	das_exit_on_error();
	
	DasIO_writeException(pHDat->pOut, &se);
}

/* ************************************************************************* */
/* File List Helpers */
 
int cmpArrays(int nLen, int* lOne, int* lTwo){
	for(int i = 0; i < nLen; i++){
		if(lOne[i] - lTwo[i] != 0) return lOne[i] - lTwo[i];
	}
	return 0;
}

DasErrCode timeFilePath(
	char* sPath, const char* sBinRoot, int nTmCmp, int* lTm, const char* sResToken
){
	
	switch(nTmCmp){
	case 1:  /* Yearly */
		snprintf(sPath, PATH_MAX-1, "%s/%04d_%s.d2s", sBinRoot, lTm[0], sResToken);
		break;
	case 2:  /* Monthly */
		snprintf(sPath, PATH_MAX-1, "%s/%04d/%04d-%02d_%s.d2s", sBinRoot,
			      lTm[0], lTm[0], lTm[1], sResToken);
		break;
	case 3:  /* Daily */
		snprintf(sPath, PATH_MAX-1, "%s/%04d/%02d/%04d-%02d-%02d_%s.d2s", sBinRoot,
			      lTm[0], lTm[1], lTm[0], lTm[1], lTm[2], sResToken);
		break;
	case 4:  /* Hourly files in daily dirs */
		snprintf(sPath, PATH_MAX-1, "%s/%04d/%02d/%02d/%04d-%02d-%02dT%02d_%s.d2s", 
		         sBinRoot, lTm[0], lTm[1], lTm[2], lTm[0], lTm[1], lTm[2], lTm[3],
               sResToken);
		break;
	case 5: /* per minute - goes in hourly directories */
		snprintf(sPath, PATH_MAX-1, "%s/%04d/%02d/%02d/%02d/%04d-%02d-%02dT%02d-%02d_%s.d2s", 
					sBinRoot, lTm[0], lTm[1], lTm[2], lTm[3], lTm[0], lTm[1], lTm[2], lTm[3], lTm[4],
               sResToken);
		break;
	case 6: /* per second, goes in 1-minute directories */
		snprintf(sPath, PATH_MAX-1, 
				   "%s/%04d/%02d/%02d/%02d/%02d/%04d-%02d-%02dT%02d-%02d-%02d_%s.d2s", 
					sBinRoot, lTm[0], lTm[1], lTm[2], lTm[3], lTm[4], lTm[0], lTm[1], lTm[2], 
				   lTm[3], lTm[4], lTm[5], sResToken);
		break;
	default:
		return das_error(P_ERR, "Expected to create a yearly, monthly, daily or hourly, "
		                  "per-minute or per-second cache path");
	}
	
	return DAS_OKAY;
}


/* ************************************************************************* */
/* Getting Time based File Lists */

/* Function should only be called when rBeg and rEnd are in us2000 units. */

char** timeBinFileList(
	const char* sRoot, cache_tree_t* pTree, double rBeg, double rEnd, 
	int nTmComp, size_t* puFiles
){

	das_time dtBeg = {0};
	das_time dtEnd = {0};
	
	/* fprintf(stderr, "HELLO1\n"); */
	
	/* Time arrays:  0 = yr, 1 = month, 2 = day, 3 = hr, 4 = min, 5 = sec */
	int lBeg[6] = {0}; 
	int lCur[6] = {0};
	int lEnd[6] = {0};
	int i = 0;
	char sPath[PATH_MAX] = {'\0'};
	int nDoy = 0; double rSec = 0.0;
	
   if(nTmComp > 6){
		fprintf(stderr, "Finest file chunk-size is 'per-second' (6 time components). "
				  "Reading finer cache blocks requires a source code update.\n");
		return NULL;
	}
	if(nTmComp < 1){
		fprintf(stderr, "Largest cache block size supported is yearly (1 time "
		        "component).  Reading larger cache blocks requires a source code "
		        "update.\n");
		return NULL;
	}
	
	Units_convertToDt(&dtBeg, rBeg, UNIT_US2000);
	Units_convertToDt(&dtEnd, rEnd, UNIT_US2000);
	lBeg[0] = dtBeg.year;    lEnd[0] = dtEnd.year;
	lBeg[1] = dtBeg.month;   lEnd[1] = dtEnd.month;
	lBeg[2] = dtBeg.mday;    lEnd[2] = dtEnd.mday;
	lBeg[3] = dtBeg.hour;    lEnd[3] = dtEnd.hour;
	lBeg[4] = dtBeg.minute;  lEnd[4] = dtEnd.minute;
	if(nTmComp > 4){
		lBeg[5] = (int) floor(dtBeg.second);
		lEnd[5] = (int) ceil(dtEnd.second);
	}
	
	/* Bump up end point if at least one of the finer time components are 
	   non-zero */
	for(i = nTmComp; i<6; ++i){
		if(lEnd[i] > 0){ 
			lEnd[nTmComp - 1] += 1;  
			break;
		}
	}
	for(i = nTmComp; i<6; ++i)	lEnd[i] = i <3 ? 1 : 0;
	rSec = lEnd[5];
	tnorm(lEnd, lEnd+1, lEnd+2, &nDoy, lEnd+3, lEnd+4, &rSec);
	lEnd[5] = (int) ceil(rSec);
	

	/* Loop 1st time to get the size of the array */
	*puFiles = 0;
	for(i = 0; i < nTmComp; i++) lCur[i] = lBeg[i];
	
	while(cmpArrays(nTmComp, (int*)lCur, (int*)lEnd) < 0){
		
		timeFilePath(sPath, sRoot, nTmComp, lCur, pTree->sSubDir);
		/* fprintf(stderr, "Looking for: %s\n", sPath); */
		if(das_isfile(sPath)) *puFiles += 1;
		
		lCur[nTmComp - 1] += 1;                /* Bump last time component by 1 */
		
		rSec = lCur[5];
		tnorm(lCur, lCur+1, lCur+2, &nDoy, lCur+3, lCur+4, &rSec);
		lCur[5] = (int) ceil(rSec);
	}
	
	if(*puFiles == 0) return NULL;
	
	char** ppFiles = (char**)calloc(*puFiles, sizeof(char*));
	for(size_t u = 0; u < *puFiles; u++) 
		ppFiles[u] = (char*)calloc(PATH_MAX, sizeof(char));

	/* Loop second time to make path records, careful, filesystem could be changing */
	size_t uMax = *puFiles;
	*puFiles = 0;
	for(i = 0; i < nTmComp; i++) lCur[i] = lBeg[i];
	
	while( (cmpArrays(nTmComp, (int*)lCur, (int*)lEnd) < 0) && (*puFiles < uMax)){
		
		timeFilePath(ppFiles[*puFiles], sRoot, nTmComp, lCur, pTree->sSubDir);
		
		if(das_isfile(ppFiles[*puFiles]))
			*puFiles += 1;
		else
			ppFiles[*puFiles][0] = '\0';
		
		lCur[nTmComp - 1] += 1;
		rSec = lCur[5];
		tnorm(lCur, lCur+1, lCur+2, &nDoy, lCur+3, lCur+4, &rSec);
		lCur[5] = (int) ceil(rSec);
	}
	
	return ppFiles;
}

/* ************************************************************************* */
/* Getting generic binned parameter lists */

/* Get GCC to shut it's cakehole about the unused parameters below */
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-parameter"

char** generalBinFileList(
	const char* sRoot, cache_tree_t* pTree, double rBeg, double rEnd,
	size_t* puFiles
){
	das_error(P_ERR, "Getting general 1-D binned data (no-time based) is not "
	           "yet implemented.");
	return NULL;
}

#pragma GCC diagnostic pop

/* ************************************************************************* */
/* Writing output data */

DasErrCode onStreamHdr(StreamDesc* pSdIn, void* vpHDat)
{
	DasErrCode nRet = 0;
	handler_data_t* pHDat = (handler_data_t*)vpHDat;
	
	char sRng[128] = {'\0'};
	const char* pRes = NULL;
	char sRes[128] = {'\0'};
	DasDesc* pSdOut = (DasDesc*)(pHDat->pSdOut);
	
	if(!pHDat->bHdrSent){
		/* Save our xCacheRange and xCacheResolution properties */
		strncpy(sRng, DasDesc_get(pSdOut, "xCacheRange"), 127);
		if( (pRes = DasDesc_get(pSdOut, "xCacheResolution")) != NULL)
			strncpy(sRes, pRes, 127);
		
		/* Copy everything */
		DasDesc_copyIn(pSdOut, (DasDesc*)pSdIn);
		
		/* Put 'em back */
		DasDesc_set(pSdOut, "DatumRange", "xCacheRange", sRng);
		if(pRes != NULL)
			DasDesc_set(pSdOut, "Datum", "xCacheResolution", sRes);
				
		if( (nRet = DasIO_writeStreamDesc(pHDat->pOut, pHDat->pSdOut)) != 0) 
			return nRet;
		pHDat->bHdrSent = true;
	}
	return nRet;
}

DasErrCode onPktHdr(StreamDesc* pSdIn, PktDesc* pPdIn, void* vpHDat)
{
	handler_data_t* pHDat = (handler_data_t*)vpHDat;
	StreamDesc* pSdOut = pHDat->pSdOut;
	PktDesc* pPdOut = NULL;
	int nPktId = pPdIn->id;
	
	if(StreamDesc_isValidId(pSdOut, nPktId)){
		/* Drop repeated packet descriptions */
		pPdOut = StreamDesc_getPktDesc(pSdOut, nPktId);
		if( PktDesc_equalFormat(pPdIn, pPdOut) ){
			pPdIn->bSentHdr = true;  /* Make input descriptor think it's written */
			return 0;
		}
		
		/* Not a repeat, but a reuse of the same ID, this is going to 
		   squash the stream efficiency */
		StreamDesc_freePktDesc(pSdOut, nPktId);
	}
	
	/* Attach the packet definitions to the output stream descriptor that */
	/* way we know which packet types are being re-defined and those that */
	/* are just being re-encountered */
	if( (pPdOut = StreamDesc_clonePktDescById(pSdOut, pSdIn, nPktId)) == NULL)
		return P_ERR;
	
	pPdIn->bSentHdr = true;  /* Make input descriptor think it's written */
	return DasIO_writePktDesc(pHDat->pOut, pPdOut);	
}

DasErrCode onPktData(PktDesc* pPktIn, void* vpHDat){
	handler_data_t* pHDat = (handler_data_t*)vpHDat;
	
	/* See if the packet is in bounds */
	PlaneDesc* pPlane = PktDesc_getXPlane(pPktIn);
	double rX = PlaneDesc_getValue(pPlane, 0);
	if(pHDat->bXIsTime){
		if( strcmp(PlaneDesc_getUnits(pPlane), UNIT_US2000) != 0)
			rX = Units_convertTo(UNIT_US2000, rX, PlaneDesc_getUnits(pPlane));
	}
	if(rX < pHDat->rBeg) return 0;
	if(rX >= pHDat->rEnd) return 0;
	
	int nRet = DasIO_writePktData(pHDat->pOut, pPktIn);
	if(nRet == 0){
		pHDat->nPktsSent += 1;
		return 0;
	}
	return nRet;
}

/* ************************************************************************* */
/* Callback to ignore no data messages, we'll count and make our own         */

/* Like I said.  GCC, just freaking chill okay  */
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-parameter"
DasErrCode onIgnoreNoData(OobExcept* pExcept, void* vpHDat)
{
	return DAS_OKAY;
}
#pragma GCC diagnostic pop

/* ************************************************************************* */
int readCache(
	const char* sParamRoot, cache_tree_t* pTree, double rBeg, double rEnd,
	bool bXIsTime, const char* sBeg, const char* sEnd
){
	handler_data_t hdat = {0};
	hdat.bHdrSent = false;
	hdat.bXIsTime = bXIsTime;
	hdat.nPktsSent = 0;
	hdat.pOut = NULL;
	hdat.pSdOut = NULL;
	hdat.pTree = pTree;
	hdat.rBeg = rBeg;
	hdat.rEnd = rEnd;
	
	char** pFileList = NULL;
	size_t uFiles = 0;
	int nInterval = 0;
	
	char sCacheLvlDir[PATH_MAX];
	
	snprintf(sCacheLvlDir, PATH_MAX, "%s/%s", sParamRoot, pTree->sSubDir);
	
	if(strcmp(pTree->sStoreMeth, "daily") == 0){
		pFileList = timeBinFileList(sCacheLvlDir, pTree, rBeg, rEnd, 3, &uFiles);
		goto have_file_list;
	}
	if(strcmp(pTree->sStoreMeth, "hourly") == 0){
		pFileList = timeBinFileList(sCacheLvlDir, pTree, rBeg, rEnd, 4, &uFiles);
		goto have_file_list;
	}
	if (strcmp(pTree->sStoreMeth, "monthly") == 0){
		pFileList = timeBinFileList(sCacheLvlDir, pTree, rBeg, rEnd, 2, &uFiles);
		goto have_file_list;
	}
	if (strcmp(pTree->sStoreMeth, "yearly") == 0){
		pFileList = timeBinFileList(sCacheLvlDir, pTree, rBeg, rEnd, 1, &uFiles);
		goto have_file_list;
	}
	if (strcmp(pTree->sStoreMeth, "perminute") == 0){
		pFileList = timeBinFileList(sCacheLvlDir, pTree, rBeg, rEnd, 5, &uFiles);
		goto have_file_list;
	}
	if (strcmp(pTree->sStoreMeth, "persecond") == 0){
		pFileList = timeBinFileList(sCacheLvlDir, pTree, rBeg, rEnd, 6, &uFiles);
		goto have_file_list;
	}

	
	if(!das_str2int(pTree->sStoreMeth, &nInterval)){
		return das_error(P_ERR, "Can't convert storage interval '%s' to "
				            "an integer", pTree->sStoreMeth);
		pFileList = generalBinFileList(sCacheLvlDir, pTree, rBeg, rEnd, &uFiles);
	}
	
have_file_list:
	
	/* Always output something */
	hdat.pOut = new_DasIO_cfile("das2_cache_rdr", stdout, "w");
	
	/* My output header, in case no data are read some minimal cache information
	   needs to be transmitted.  An nBinSize of 0 means intrinsic resolution so
		in that case xCacheResolution is not transmitted */
	hdat.pSdOut = new_StreamDesc();
	
	if(bXIsTime){
		char sVal[128] = {'\0'};
		snprintf(sVal, 128, "%s to %s UTC", sBeg, sEnd);
		DasDesc_set((DasDesc*)hdat.pSdOut, "DatumRange", "xCacheRange", sVal);
	}
	else{
		DasDesc_setDatumRng((DasDesc*)hdat.pSdOut, "xCacheRange", rBeg, rEnd,
				                 pTree->binUnits);
	}
	
	if(hdat.pTree->nBinSize > 0){
		DasDesc_setDatum((DasDesc*)hdat.pSdOut, "xCacheResolution",
				            pTree->nBinSize, pTree->binUnits);	
	}
	
	if(pFileList == NULL){
		sendNoData(&hdat);
		DasIO_close(hdat.pOut); hdat.pOut = NULL;
		return 0;
	}
	
	StreamHandler* pSh = new_StreamHandler(&hdat);
	pSh->streamDescHandler = onStreamHdr;
	pSh->pktDescHandler = onPktHdr;
	pSh->pktDataHandler = onPktData;
	pSh->exceptionHandler = onIgnoreNoData;
			
	int nRet = 0;
	DasIO* pIn = NULL;
	for(size_t u = 0; u<uFiles; u++){
		if(pIn != NULL){
			del_DasIO(pIn);
			pIn = NULL;
		}
		
		fprintf(stderr, "   Reading: %s\n", pFileList[u]);
		if( (pIn = new_DasIO_file("das2_cache_rdr", pFileList[u], "r")) == NULL)
				continue;
		
		DasIO_addProcessor(pIn, pSh);	
		if( (nRet = DasIO_readAll(pIn)) != 0) break;
	}
	
	if(pIn != NULL) del_DasIO(pIn);
	
	if(nRet == 0 && hdat.nPktsSent == 0) 
		sendNoData(&hdat);
	
	return nRet;
}

/* ************************************************************************* */
void prnHelp()
{
	fprintf(stderr, 
"SYNOPSIS\n"
"   das2_cache_rdr - Reads a das2 stream cache set\n"
"\n"
"USAGE\n"
"   das2_cache_rdr [options] DSDF CACHE_DIR PARAMSET RES BEG END\n"
"\n"
"DESCRIPTION\n"
"   das2_cache_rdr selects pre-generated das2 stream files from a hierarchical\n"
"   data cache of pre-binned data.  The program expects cache control\n"
"   directives to have the format defined in Section 3 the Das2-ICD version\n"
"   2.2.1 or higher.  The cache tree layout is expected to conform to Section\n"
"   4 of the PyServer User's Reference, version 0.3 or higher.\n"
"\n"
"PARAMETRS\n"
"   DATASET   The location of the DSDF file that defines the dataset.  Cache\n"
"             resolution information is read from this file.  Only cache\n"
"             levels who's normalize parameter string matches the PARAMSET\n"
"             argument below are used as a data source.\n"
"\n"
"   CACHE_DIR The root directory of the cache tree for a given dataset.\n"
"             Typically this directory is formed by combining some cache\n"
"             root directory with the DSDF relative path.\n"
"\n"
"   PARAMSET  A normalized string representing the parameters set to the reader.\n"
"             The assumption is that when readers are called with different \n"
"             parameter sets the output dataset changes.  Each different param\n"
"             set is a different set of cache files.  The string '_noparam' \n"
"             can be used to indicate that no parameters were given to the \n"
"             reader when the cache files were generated.\n"
"\n"
"   BEG       The starting value of the lookup parameter.\n"
"\n"
"   END       The ending value of the lookup parameter.\n"
"\n"
"   RES       A floating point number providing the resolution requested in \n"
"             seconds.  Bins may be defined using other time units in the DSDF\n"
"             but the command line parameter to the cache reader is always\n"
"             seconds (for now). The largest bin size that does not exceed this\n"
"             value will be selected as the dataset.  The string 'intrinsic' can\n"
"             be used to select the best resolution available.  Also a RES of 0\n"
"             may be given to select intrinsic resolution as well.\n"
"\n"
"OPTIONS\n"
"  -h,--help  Show this help text and exit\n"
"\n"
"  -p AMOUNT, --pad=AMOUNT\n"
"             Pad output range by an AMOUNT.  If BEG and END are UTC time values\n"
"             then the range is extended by AMOUNT seconds on each side.  If\n"
"             BEG and END are not UTC times, then the range is extended by\n"
"             AMOUNT X plane units.  This option is useful for datasets than\n"
"             still should output something even when client programs request\n"
"             an x-range that is so small it falls between points (ex. Fce lines\n"
"             for Whistler plots).\n"
"\n"
"FILES:\n"
"   TODO:  Explain the cache layout\n"
"\n"
"AUTHOR\n"
"   Chris Piker <chris-piker@uiowa.edu>\n"
"\n"
"SEE ALSO\n"
"   * das2_bin_avgsec, das2_bin_peakavgsec\n"
"   * The Das2 ICD @ http://das2.org for a general introduction to the Das 2 system.\n"
"   * The Das2 PyServer user's guide, also at http://das2.org\n"
"\n"		
	);
}

/* ************************************************************************* */

int main(int argc, char* argv[]){
	int nRet = 0;
	double rPad = 0.0;
	char* pArg = NULL;
	
	/* Exit on errors, log info messages and above */
	das_init(argv[0], DASERR_DIS_EXIT, 0, DASLOG_INFO, NULL);
	
	int iParam = 1;
	for(int i = 1; i < argc; i++){
		if(strcmp(argv[i], "-h") == 0 || strcmp(argv[i], "--help") == 0){
			prnHelp();
			return 0;
		}
		if(strcmp(argv[i], "-p") == 0){
			if(argc <= i+1){
				fprintf(stderr, "Error argument missing for pad (-p) option\n");
				return 13;
			}
			i++;
			if(! das_str2double(argv[i], &rPad) || (rPad < 0.0)){
				fprintf(stderr, "Couldn't convert %s to a positive real value\n", argv[i]);
				return 13;
			}
			iParam += 2;
		}
		if(strncmp(argv[i], "--pad=", 6) == 0){
			pArg = argv[i];
			if(! das_str2double(pArg + 6, &rPad) || (rPad < 0.0)){
				fprintf(stderr, "Couldn't convert %s to a positive real value\n", pArg+6);
				return 13;
			}
			++iParam;
		}
	}
	
	if((argc - iParam) != 6){
		fprintf(stderr, "Unexpected number of command line arguments %d %d\n"
				  "Usage: das2_cache_rdr DSDF_FILE CACHE_ROOT NORM_PARAM "
				  "BEG END RES\nIssue the command %s -h for more info.\n\n", argc, iParam,
				   argv[0]);
		return 13;
	}
	
	const char* sDsdf = argv[iParam];
	const char* sCacheRoot = argv[iParam + 1];
	const char* sNormParam = argv[iParam + 2];
	char* sBeg = argv[iParam + 3];
	char* sEnd = argv[iParam + 4];
	const char* sRes = argv[iParam + 5];
	char sParamRoot[PATH_MAX] = {'\0'};
	
	/* Knock the damn Z off the end of the time strings, no one uses local time
	   for anything important anymore */
	int iTmp = strlen(sBeg) - 1;
	if(sBeg[iTmp] == 'Z') sBeg[iTmp] = '\0';
	iTmp = strlen(sEnd) - 1;
	if(sEnd[iTmp] == 'Z') sEnd[iTmp] = '\0';
						
	if(!das_isfile(sDsdf))
		return das_error(P_ERR, "Missing Data Source Description File (DSDF): %s", sDsdf);
	
	if(!das_isdir(sCacheRoot))
		return das_error(P_ERR, "Cache directory %s doesn't exist", sCacheRoot);

	cache_tree_t pTrees[MAX_TREES+1];  /* Native should always be first */
	size_t uTrees = MAX_TREES;
	
	/* These come out with the lowest resolution (highest interval value) first*/
	if((nRet = readStoreMeth(sDsdf, sNormParam, pTrees, &uTrees)) != 0) return nRet;
	
	bool bXisTime = isTimeBased(pTrees, uTrees);
	
	double rRes = 0.0, rBeg = 0.0, rEnd = 0.0;
	das_time dtBeg = {0}, dtEnd = {0};
	if(bXisTime){
		dt_parsetime(sBeg, &dtBeg);
		rBeg = Units_convertFromDt(UNIT_US2000, &dtBeg);
		if(isDas2Fill(rBeg)) return P_ERR;
		dt_parsetime(sEnd, &dtEnd);
		rEnd = Units_convertFromDt(UNIT_US2000, &dtEnd);
		if(isDas2Fill(rEnd)) return P_ERR;
		rBeg -= rPad * 1.0e+6;
		rEnd += rPad * 1.0e+6;
	}
	else{
		if(!das_str2double(sBeg, &rBeg))
			return das_error(P_ERR, "Can't convert begin point %s to a double value", sBeg);
		if(!das_str2double(sEnd, &rEnd))
			return das_error(P_ERR, "Can't convert end point %s to a double value", sEnd);
		rBeg -= rPad;
		rEnd += rPad;
	}
	if(rEnd <= rBeg)
		return das_error(P_ERR, "Begin point %s is <= to the ending point %s", sBeg, sEnd);
	
	if(!das_str2double(sRes, &rRes))
		return das_error(P_ERR, "Can't convert resolution %s to a double value", sRes);
	if(rRes < 0.0)
		return das_error(P_ERR, "Resolution value %s is <= 0", sRes);
	
	/* Pick the directory tree to use */
	cache_tree_t* pUseTree = NULL;
	for(size_t u = 0; u<uTrees; u++){
		if(rRes >= pTrees[u].rBinSec){
			pUseTree = pTrees + u;
			break;
		}
	}
	
	if(pUseTree == NULL)
		return das_error(P_ERR, "Can't find a cache tree in %s with a resolution "
				            "lower than %f", sCacheRoot, rRes);
	snprintf(sParamRoot, PATH_MAX-1, "%s/%s", sCacheRoot, sNormParam);
	return readCache(sParamRoot, pUseTree, rBeg, rEnd, bXisTime, sBeg, sEnd);
}
