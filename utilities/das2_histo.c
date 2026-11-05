/* Copyright (C) 2019-2026 Chris Piker  <chris-piker@uiowa.edu>
 *                         
 * This file is part of das2C, the Core Das C Library.
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
 * version 2.1 along with das2C; if not, see <http://www.gnu.org/licenses/>. 
 */

#define _POSIX_C_SOURCE 200112L

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>

#include <das3/core.h>

/* ************************************************************************* */
/* Global mode switch */

#define P_ERR 100

#define RAW_COUNTS 0
#define FRAC_BELOW 1
#define FRAC_ABOVE 2

int g_nFrac = RAW_COUNTS;


/* ************************************************************************* */

/* Storage arrays:
 * We need one output array per output packet type.  Note that there may be
 * more output packet types then input packet types since the peaks may need
 * to be separated from the averages.  One of these is attached to each input
 * plane descriptor via the user data pointer 
 */

/* Most sensors have 12-bit outputs or less.  In some cases these states can be
 * shifted by gain or range settings giving up to about 5-bits more states.
 * Unless averaging has happened you can have more output states than input
 * states, so arrays that start at 2**17 will usually not result in an
 * additional allocation. */ 
#define LIN_ARY_MIN_SZ 0x20000 

typedef struct linear_array {
	double* data;    /* The data values */
	ptrdiff_t used;     /* The number of locations used in the array */
	ptrdiff_t size;     /* The total number of locations in the array */
	double max_val;  /* The maximum value */
} lin_ary_s;

/* Get existing index, or insertion point for new data values into the array.
 * This algorithm is cleaner if we used recursion, but tail-call optimization
 * is often not done in debug code and I don't want to trigger a stack overflow.
 * 
 */
ptrdiff_t find_val(const lin_ary_s* pLinAry, double dVal, bool* pExists)
{
	if(pLinAry->used == 0) return 0;
	
	double* aData = pLinAry->data;
	
	ptrdiff_t iHi = pLinAry->used -1;
	ptrdiff_t iLo = 0;
	ptrdiff_t iMid = -1;

	while (iLo <= iHi) { 
		iMid = iLo + (iHi - iLo)/2; 
  
		if(aData[iMid] == dVal){                   /* Found, return location */
			*pExists = true;
			return iMid;
		}
  
		if (aData[iMid] < dVal) iLo = iMid + 1;  /* Val maybe in right half */
		else iHi = iMid - 1;                    /* Val maybe in left half  */
  } 
  
	/* value not present, return insert location */
	*pExists = false;
	iMid = (iHi + iLo) / 2;
	if(aData[iMid] > dVal) return iMid;
	else return iMid + 1;
}

/** Grow the array such that index i is a valid position but don't mark
 *  that position as used */
void grow_for_idx(lin_ary_s* pAry, ptrdiff_t idx)
{
	if(idx < pAry->size) return;
	
	size_t uNewSz = pAry->size;
	if(uNewSz < LIN_ARY_MIN_SZ) uNewSz = LIN_ARY_MIN_SZ;
	double* pTmp = (double*)calloc(uNewSz, sizeof(double));
	memcpy(pTmp, pAry->data, pAry->size*sizeof(double));
	free(pAry->data);
	pAry->data = pTmp;
	pAry->size = uNewSz;
}

/** Shift down items, growing the array if needed */
bool right_shift(lin_ary_s* pAry, ptrdiff_t idx, size_t space)
{
	if(idx < 0 || space <= 0){ 
		das_error(DASERR_ASSERT, "logic error");
		return false;
	}
	if(idx >= pAry->size) grow_for_idx(pAry, idx + space);
	
	/* Careful, this is a double pointer so incrementing it by one moves it
	 * 8 bytes to the right */
	void* pDest = pAry->data + idx + space;
	void* pSrc = pAry->data + idx;
	
	/* The amount of data to move is the used - idx */
	
	/* but here we have to specify number of bytes, not number of doubles */
	memmove(pDest, pSrc, (pAry->used - idx) * sizeof(double));
	
	/* Fill in the left over bytes with zeros since all the arrays start
	 * with that value */
	memset(pSrc, 0x0, space*sizeof(double));
	return true;
}

/*****************************************************************************/

DasErrCode onStreamHdr(DasStream* pSdIn, void* vpOut)
{	
	DasIO* pOut = (DasIO*)vpOut;
	
	/* Copy over the summary, copy over the title property but add 
	   '- Histogram', don't copy anything else */
	
	DasStream* pSdOut = new_DasStream();
	
	const char* sCumlative = "";
	if(g_nFrac == FRAC_BELOW) sCumlative = "Normalized Cumulative ";
	if(g_nFrac == FRAC_ABOVE) sCumlative = "Normalized Reverse Cumulative ";
	
	DasDesc* pDesc = (DasDesc*)pSdOut;
	const char* sTitle = DasDesc_getStr((DasDesc*)pSdIn, "title");
	if(sTitle)
		DasDesc_vSetStr(pDesc, "title", "%s - %sHistogram", sTitle, sCumlative);
	else
		DasDesc_vSetStr(pDesc, "title", "%sHistogram", sCumlative);
	
	const char* sSummary = DasDesc_getStr((DasDesc*)pSdIn, "summary");
	if(sSummary) DasDesc_setStr(pDesc, "title", sSummary);
	
	DasDesc_setDouble(pDesc, "yFill", 0.0);
	DasDesc_setDouble(pDesc, "zFill", 0.0);
	
	/* Treat the output stream descriptor's user data pointer as the 
	   "next pkt ID" counter */
	DasDesc_setUser((DasDesc*)pSdOut, (void*)(intptr_t)1);
	
	/* Save the output stream descriptor as user data for the input steam desc
	 * so that we always have access to it without global vars */
	DasDesc_setUser((DasDesc*)pSdIn, pSdOut);
	
	return DasIO_writeStreamDesc(pOut, pSdOut);
}

/*****************************************************************************/
/* Setup new output structures */

DasErrCode onPktHdr(DasStream* pSdIn, DasPkt* pPdIn, void* vpOut)
{
	DasStream* pSdOut = (DasStream*)DasDesc_getUser((DasDesc*)pSdIn);
	/* The output stream's user pointer carries the next packet ID by value */
	int nNextId = (int)(intptr_t)DasDesc_getUser((DasDesc*)pSdOut);
	
	
	/* Construct one output packet header for each input PLANE */
	lin_ary_s* pVals = NULL;
	DasPlane* pPlaneIn = NULL;
	DasPlane* pXOut = NULL;
	DasPlane* pPlOut = NULL;
	DasPkt* pPktOut = NULL;
	DasEnc* pEnc = NULL;
	char sName[64] = {'\0'};
	
	size_t uPlains = DasPkt_getNPlanes(pPdIn);
	const char* sProp = NULL;
	for(int i=0; i < uPlains; ++i){
		if(nNextId > 99) return das_error(P_ERR, "Ran out of output packet IDs");
		
		pPlaneIn = DasPkt_getPlane(pPdIn, i);
		if(DasPlane_getType(pPlaneIn) == X) continue;
		
		/* make a new x-axis plane */
		pEnc = new_DasEnc(DAS2DT_HOST_REAL, 8, NULL);
		pXOut = new_DasPlane(X, NULL, pEnc, DasPlane_getUnits(pPlaneIn));
		
		/* Have it hold the output data value (X-axis) for us */
		pVals = (lin_ary_s*)calloc(1, sizeof(lin_ary_s));
		pVals->size = LIN_ARY_MIN_SZ;
		pVals->data = (double*)calloc(pVals->size, sizeof(double));
		
		DasDesc_setUser((DasDesc*)pXOut, pVals);
		
		/* Now make a counts plane, needs to be the same shape as the input, but
		 * units are now dimensionless counts.  Assume we can encode this as
		 * floats unless we're outputting raw counts and the value in any bin
		 * grows over 16,777,217 (2^24) + 1 */
		pEnc = new_DasEnc(DAS2DT_HOST_REAL, 4, NULL);
		pPlOut = DasPlane_copy(pPlaneIn);
		
		const char* sLabel = "Value Count";
		if(g_nFrac == FRAC_BELOW) sLabel = "Fraction at or below";
		if(g_nFrac == FRAC_ABOVE) sLabel = "Fraction at or above";
		
		/* Copy over the labels, use data label for X axis label */
		if(DasPlane_getType(pPlaneIn) == YScan){
			/* Y goes straight through for these */
			sProp = DasDesc_get((DasDesc*)pPlaneIn, "yLabel");
			if(sProp) DasDesc_setStr((DasDesc*)pPlOut, "yLabel", sProp);
			DasDesc_setStr((DasDesc*)pPlOut, "zLabel", sLabel);
			
			sProp = DasDesc_get((DasDesc*)pPlaneIn, "zLabel");	
		}
		else{
			DasDesc_setStr((DasDesc*)pPlOut, "yLabel", sLabel);
			
			sProp = DasDesc_get((DasDesc*)pPlaneIn, "yLabel");
		}
		if(sProp) DasDesc_setStr((DasDesc*)pXOut, "xLabel", sProp);
		
		DasPlane_setUnits(pPlOut, UNIT_DIMENSIONLESS);
		DasPlane_setValEncoder(pPlOut, pEnc);
		snprintf(sName, 63, "%s_hist", DasPlane_getName(pPlaneIn));
		DasPlane_setName(pPlOut, sName);
		
		size_t uItems = DasPlane_getNItems(pPlaneIn);
		
		/* Have it hold the output counts (data-axis) for us */
		pVals = (lin_ary_s*)calloc(1, sizeof(lin_ary_s));
		pVals->size = LIN_ARY_MIN_SZ * uItems;
		pVals->data = (double*)calloc(pVals->size, sizeof(double));
		
		DasDesc_setUser((DasDesc*)pPlOut, pVals);
		
		/* make a new packet to hold the output, save it with the input plane so
		 * we can find it */
		pPktOut = new_DasPkt();
		DasDesc_setUser((DasDesc*)pPlaneIn, pPktOut);
		
		DasPkt_addPlane(pPktOut, pXOut);
		DasPkt_addPlane(pPktOut, pPlOut);
		DasStream_addDesc(pSdOut, (DasDesc*)pPktOut, nNextId);
		++nNextId;
		DasDesc_setUser((DasDesc*)pSdOut, (void*)(intptr_t)nNextId);
	}
	return DAS_OKAY;
}

/* ************************************************************************* */
/* Binary search and store */

DasErrCode onPktData(DasPkt* pPdIn, void* vpOut)
{
	DasPlane* pPlaneIn = NULL;
	DasPkt* pPktOut = NULL;
	DasPlane* pXOut = NULL;
	DasPlane* pPlOut = NULL;
	lin_ary_s* pValAry = NULL;
	lin_ary_s* pCountAry = NULL;
	
	size_t u, uItems, uPlanes = DasPkt_getNPlanes(pPdIn);
	int j;
	double rFill = DAS_FILL_VALUE;
	double rVal = 0.0;
	for(int i=0; i < uPlanes; ++i){
		pPlaneIn = DasPkt_getPlane(pPdIn, i);
		if(DasPlane_getType(pPlaneIn) == X) continue;
		
		pPktOut = (DasPkt*) DasDesc_getUser((DasDesc*)pPlaneIn);
		pXOut = DasPkt_getPlaneByType(pPktOut, X, 0);
		pValAry = (lin_ary_s*)DasDesc_getUser((DasDesc*)pXOut);
		
		j = 0;
		do{ 
			pPlOut = DasPkt_getPlane(pPktOut, j++);
		} while(DasPlane_getType(pPlOut) == X);
		pCountAry = (lin_ary_s*) DasDesc_getUser((DasDesc*)pPlOut);
		
		rFill = DasPlane_getFill(pPlOut);
		
		/* For each value in the input plane, see if we have it in the output */
		uItems = DasPlane_getNItems(pPlaneIn);
		for(u = 0; u < uItems; ++u){
			rVal = DasPlane_getValue(pPlaneIn, u);
			if(rVal == rFill) continue;
			
			bool bHaveIt = false;
			int iX = find_val(pValAry, rVal, &bHaveIt);
		
			/* Make sure my output arrays are big enough*/
			if(iX >= pValAry->size) grow_for_idx(pValAry, iX);
			int iDat = iX * uItems + u;
			if(iDat >= pCountAry->size) grow_for_idx(pCountAry, iDat);
			
			/* Shift items to make room for new data value and counts.  We have to
			 * shift  */
			if(!bHaveIt){
				if(iX < pValAry->used){
					
					right_shift(pValAry, iX, 1);
					right_shift(pCountAry, iX*uItems, uItems);
				}
				pValAry->used += 1;
				pCountAry->used += uItems;
			}
			
			pValAry->data[iX] = rVal;
			pCountAry->data[iDat] += 1;
			
			if(pCountAry->data[iDat] > pCountAry->max_val) 
				pCountAry->max_val = pCountAry->data[iDat];
		}
	}
	return DAS_OKAY;
}

/* ************************************************************************* */
/* Pass on comments and exceptions */

DasErrCode onComment(const DasComment* pCmt, void* vpOut)
{
	DasIO* pOut = (DasIO*)vpOut;
	return DasIO_comment(pOut, pCmt);
}

/* ************************************************************************* */
/* Writing out packet data */

DasErrCode writeHisto(DasIO* pOut, DasPkt* pPktOut)
{	
	DasErrCode nRet = DAS_OKAY;
	DasPlane* pXOut = DasPkt_getXPlane(pPktOut);
	lin_ary_s* pValAry = (lin_ary_s*) DasDesc_getUser((DasDesc*)pXOut);
	DasPlane* pPlOut = NULL;
	
	int iPlane = 0;
	do{ 
		pPlOut = DasPkt_getPlane(pPktOut, iPlane++);
	} while(DasPlane_getType(pPlOut) == X);
	lin_ary_s* pCountAry = (lin_ary_s*) DasDesc_getUser((DasDesc*)pPlOut);
	
	/* Check to see if we need to change the output encoding for the data 
	 * values.  Floats can only handle integers at most 16,777,217 (2^24) + 1 */
	if((pCountAry->max_val >= 16777217) && (g_nFrac == RAW_COUNTS) ){
		DasEnc* pEnc = new_DasEnc(DAS2DT_HOST_REAL, 8, NULL);
		DasPlane_setValEncoder(pPlOut, pEnc);
	}
	if( (nRet = DasIO_writeDesc(pOut, (DasDesc*)pPktOut, 0)) != DAS_OKAY) return nRet;
	
	ptrdiff_t nItems = DasPlane_getNItems(pPlOut);
	ptrdiff_t iTotal = 0, i = 0, j = 0;
	
	/* Converting to cumulative fraction */
	if((g_nFrac == FRAC_BELOW)&&(pValAry->used > 0)){
		for(i = 1; i < pValAry->used; ++i){
			for(j = 0; j < nItems; ++j){
				pCountAry->data[i*nItems + j] += pCountAry->data[(i-1)*nItems + j];
			}
		}
	
		iTotal = nItems * (pValAry->used - 1);
		for(i = 0; i < pValAry->used; ++i){
			for(j = 0; j < nItems; ++j) {
				
				if(pCountAry->data[iTotal + j] > 0.0)
					pCountAry->data[i*nItems + j] /= pCountAry->data[iTotal + j];
			}
		}
	}
	
	/* Converting to reverse cumulative fraction */
	if((g_nFrac == FRAC_ABOVE)&&(pValAry->used > 0)){
		for(i = pValAry->used - 2; i >= 0; --i){
			for(j = 0; j < nItems; ++j){
				pCountAry->data[i*nItems + j] += pCountAry->data[(i+1)*nItems + j];
			}
		}
	
		/* Hit 0th block last since it's the divisor */
		for(i = pValAry->used - 1; i >= 0; --i){
			for(j = 0; j < nItems; ++j) {
				/* careful, weird datasets could have all fill values, which
				   would be indicated by 0th block being empty */
				if(pCountAry->data[0 + j] > 0.0)
					pCountAry->data[i*nItems + j] /= pCountAry->data[0 + j];
			}
		}		
	}
	
	for(i = 0; i < pValAry->used; ++i){
		DasPlane_setValue(pXOut, 0, pValAry->data[i]);
		DasPlane_setValues(pPlOut, pCountAry->data + i*nItems);
		if( (nRet = DasIO_writeData(pOut, (DasDesc*)pPktOut, 0)) != DAS_OKAY) return nRet;
	}
	return DAS_OKAY;
}

/* ************************************************************************* */
/* Emit early because the same packet ID is about to mean something else */

DasErrCode emitAndFreePkts(DasStream* pSdIn, DasDesc* pDescIn, void* vpOut)
{
	DasIO* pOut = (DasIO*)vpOut;

	/* Our output packets hang off das2 planes, so a DasDs in the ID slot means a
	   das3 stream we were never built to bin */
	if(DasDesc_type(pDescIn) != PACKET)
		return das_error(P_ERR,
			"das2_histo only handles das2 packet descriptors, not %s",
			das_desc_type_toStr(DasDesc_type(pDescIn))
		);

	DasPkt* pPdIn = (DasPkt*)pDescIn;

	DasErrCode nRet = DAS_OKAY;
	size_t uPlanes = DasPkt_getNPlanes(pPdIn);
	DasPkt* pPktOut = NULL;
	DasPlane* pPlaneIn = NULL;
	for(size_t u=0; u < uPlanes; ++u){
		pPlaneIn = DasPkt_getPlane(pPdIn, u);
		if(DasPlane_getType(pPlaneIn) == X) continue;
		
		pPktOut = (DasPkt*) DasDesc_getUser((DasDesc*)pPlaneIn);
		if( (nRet = writeHisto(pOut, pPktOut)) != DAS_OKAY) return nRet;
		del_DasDesc((DasDesc*)pPktOut);
	}
	return DAS_OKAY;
}


/* ************************************************************************* */
/* Emit everything remaining */

/* Flush the histograms, then pass on an exception if the input ended on one */
DasErrCode onClose(DasStream* pSdIn, const DasExcept* pExcept, void* vpOut)
{
	DasPkt* pPktIn = NULL;
	DasErrCode nRet = DAS_OKAY;
	for(int i = 1; (pSdIn != NULL)&&(i < MAX_PKTIDS); ++i){
		pPktIn = DasStream_getPkt(pSdIn, i);
		if(pPktIn == NULL) continue;
		
		if( (nRet = emitAndFreePkts(pSdIn, (DasDesc*)pPktIn, vpOut)) != DAS_OKAY) return nRet;
	}

	if(pExcept == NULL) return DAS_OKAY;
	DasIO_exception((DasIO*)vpOut, pExcept);
	return (DasExcept_type(pExcept) == DAS_EX_NO_DATA) ? DAS_OKAY : P_ERR;
}


/* ************************************************************************* */
void prnHelp(FILE* pFile)
{
	fprintf(pFile,
"SYNOPSIS\n"
"   das2_histo - Convert das2 data streams into histograms\n"
"\n"
"USAGE\n"
"   READER | das2_histo \n"
"\n"
"DESCRIPTION\n"
"   das2_histo is a classic Unix filter, reading a das2 stream on standard\n"
"   input and producing a transformed stream for the output.  The program\n"
"   drops X axis values.  Converts data values to ordered X axis values and\n"
"   replaces the data values with a count of how often that particular value\n"
"   has occurred in the input stream.\n"
"\n"
"   In cases where multiple input planes are present in a single input packet\n"
"   the output stream will have more packet types than the input stream.  This\n"
"   is necessary since data with different units should not be counted together.\n"
"\n"
"OPTIONS:\n"
"\n"
"   -b,--frac-below\n"
"         Output the cumulative fraction of points at or below a given data\n"
"         value.  By default the total count of points at a given data value\n"
"         are output.\n"
"\n"
"   -a,--frac-above\n"
"         Output the cumulative fraction of points at or above a given data\n"
"         value.  By default the total count of points at a given data value\n"
"         are output.\n"
"\n"
"   -h,--help\n"
"         Print this help text\n"
"\n"
"   -v,--version\n"
"         Print source code version control information\n"
"\n"
"AUTHOR\n"
"   chris-piker@uiowa.edu\n"
"\n"
"SEE ALSO\n"
"   das2_bin_ratesec, das2_psd\n"
"\n"
"   The das 2 ICD @ http://das2.org for an introduction to the das 2 system.\n"
"\n");
}

/* ************************************************************************* */
/* Main */

int main(int argc, char* argv[]) 
{
	int nRet = 0;
	
	/* Exit on errors, log info messages and above */
	das_init(argv[0], DASERR_DIS_EXIT, 0, DASLOG_INFO, NULL);
	
	g_nFrac = RAW_COUNTS;
	for(int i = 1; i < argc; i++){
		if(strcmp(argv[i], "-h") == 0 || strcmp(argv[i], "--help") == 0 ){
			prnHelp(stdout);
			return 0;
		}
	
		if(strcmp(argv[i], "-v") == 0 || strcmp(argv[i], "--version") == 0){
			printf("das2_histo (das2C %s)\n", das_lib_version());
			return 0;
		}
		
		if((strcmp(argv[i], "-b") == 0)|| strcmp(argv[i], "--frac-below") == 0){
			g_nFrac = FRAC_BELOW;
			continue;
		}
	
		if((strcmp(argv[i], "-a") == 0)|| strcmp(argv[i], "--frac-above") == 0){
			g_nFrac = FRAC_ABOVE;
			continue;
		}
		
		return das_error(P_ERR, "Unrecognized command line option, '%s'.  Use -h "
				           "for help.", argv[i]);
		
	}
	
	/* Create an un-compressed output I/O object */
	DasIO* pOut = new_DasIO("das2_histo", new_DasXport_stdout(), NULL);
	
	/* Create an input processor, provide the output processor as a user data
	   object so that the callbacks have access to it. */
	StreamHandler* pSh = new_StreamHandler(pOut);
	pSh->streamDescHandler = onStreamHdr;
	pSh->pktDescHandler = onPktHdr;
	pSh->pktRedefHandler = emitAndFreePkts;
	pSh->pktDataHandler = onPktData;
	pSh->closeHandler = onClose;
   pSh->commentHandler = onComment;

	DasIO* pIn = new_DasIO("Standard Input", new_DasXport_stdin(), NULL);
	DasIO_addProcessor(pIn, pSh);

	nRet = DasIO_readAll(pIn);
	del_DasIO(pIn);  /* make valgrind happy, but maybe delete this line later */
	free(pSh);       /* also to make valgrind happy */
	pIn = NULL;
	return nRet;
}
