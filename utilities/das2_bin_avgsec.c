/* Copyright (C) 2015-2025  Chris Piker  <chris-piker@uiowa.edu>
 *               2004       Jeremy Faden <jeremy-faden@uiowa.edu>
 *                         
 *
 * This file is part of das2C, the Core Das2 C Library.
 * 
 * das2C is free software; you can redistribute it and/or modify it under
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

#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <math.h>
#include <assert.h>

#include <das2/core.h>

#define P_ERR 100

/* ************************************************************************* */
/* Record Keeping */

bool g_bProgress = true;         /* Forward or drop stream annotations */
DasIO* g_pIoOut = NULL;             /* The output writer */
StreamDesc* g_pSdOut = NULL;        /* The output stream descriptor */
	
/* The us2000 time of the first packet received, used to calculate 
   which bin number any other packets may be in */
double g_rStartMicroSec = 0.0;
	
/* Has a single data packet been seen on the stream yet?  The first packet's
   time sets bin num = 0 */
bool g_bHasStartTime = false;

double g_rBinSzMicroSec = 0.0;   /* Output bin size in micro-seconds */
bool   g_lbHasBinNo[100] = {false};
long   g_lnBin[100] = {0};       /* The current X-axis 'bin number' by pkt id */

/* Keep track of where the original data planes stop */
size_t g_uOrigPlanes[100] = {0};

bool g_bRangeOut = false;        /* True if min/max planes should be output */
bool g_bStdDevOut = false;       /* True if stddev planes should be output */

/* Keep track of the max plane for each original plane */
size_t g_uMaxIndex[100][MAXPLANES] = {{0}};

/* Keep track of the min plane for each original plane */
size_t g_uMinIndex[100][MAXPLANES] = {{0}};

/* Keep track of the Std. Dev. plane for each origin plane */
size_t g_uStdDevIndex[100][MAXPLANES] = {{0}};

/* Sum accumulation array, one for each plane of each packet type, fill values
 * aren't added to the count */
double* g_ldSum[100][MAXPLANES] = {{NULL}};

/* Counting array, one for each plane of each packet type, fill values aren't
   added to the count. */
double* g_ldCount[100][MAXPLANES] = {{NULL}};

/* Min max arrays, one for each variable of each physical dimension */
double* g_ldMin[100][MAXPLANES] = {{NULL}};
double* g_ldMax[100][MAXPLANES] = {{NULL}};

/* Accumulation arrays, if needed */
DasAry* g_lpAccum[100][MAXPLANES] = {{NULL}};



/* ************************************************************************* */
/* Maybe copy out Exceptions and Comments */

DasErrCode onException(OobExcept* pExcept, void* vpOut)
{
	return DasIO_writeException(g_pIoOut, pExcept);
}

DasErrCode onComment(OobComment* pCmt, void* vpOut)
{
	if(! g_bProgress && (
		(strcmp(pCmt->sType, "taskProgress") == 0) || (strcmp(pCmt->sType, "taskSize") == 0)
	))
		return DAS_OKAY;
	
	return DasIO_writeComment(g_pIoOut, pCmt);
}


/*****************************************************************************/
/* Stream Header Processing */

DasErrCode onStreamHdr(StreamDesc* pSdIn, void* v)
{
	g_pSdOut = StreamDesc_copy(pSdIn);
	double rBinSzSec = g_rBinSzMicroSec*1e-6;
	double rCacheRes = rBinSzSec;
	char sResInfo[64] = {'\0'};
	
	/* Update the output xTagWidth if it's less than the resolution */
	if( DasDesc_has((DasDesc*)g_pSdOut, "xTagWidth" ) ) {
		double rInWidth = DasDesc_getDatum((DasDesc*)g_pSdOut, "xTagWidth", UNIT_SECONDS);
		if(rInWidth < rBinSzSec)
			DasDesc_setDatum((DasDesc*)g_pSdOut, "xTagWidth", rBinSzSec, UNIT_SECONDS);
		else
			rCacheRes = rInWidth;
	}
	else{
		DasDesc_setDatum((DasDesc*)g_pSdOut, "xTagWidth", rBinSzSec, UNIT_SECONDS);
	}
	
	/* Set the xCacheResolution, remember this should be no higher than the 
	 * xTagWidth */
	DasDesc_setDatum((DasDesc*)g_pSdOut, "xCacheResolution", rCacheRes, UNIT_SECONDS);
	
	if(rCacheRes < 1.0){  snprintf(sResInfo, 63, " (%.0f ms Averages)", rCacheRes*1000); }
	else{
		if(rCacheRes < 60.0){ snprintf(sResInfo, 63, " (%.1f s Averages)", rCacheRes); }
		else{
			if(rCacheRes < 3600.0){ 
				snprintf(sResInfo, 63, " (%.1f minute Averages)", rCacheRes/60.0); 
			}
			else{
				if(rCacheRes < 86400.0)
					snprintf(sResInfo, 63, " (%.1f hour Averages)", rCacheRes/3600.0); 
				else
					snprintf(sResInfo, 63, " (%.3g day Averages)", rCacheRes/86400);
			}
		}
	}
	DasDesc_setStr((DasDesc*)g_pSdOut, "xCacheResInfo", sResInfo);

	/* CDF Item:  If the Data_type property is present, change it from
	   H0>High Resolution data to K0>Key Parameter */
	if(DasDesc_has((DasDesc*)g_pSdOut, "Data_type"))
		DasDesc_setStr((DasDesc*)g_pSdOut, "Data_type", "K0>Key Parameter");

	return DasIO_writeStreamDesc(g_pIoOut, g_pSdOut);
}

/* ************************************************************************* */
/* Determine if a waveform <yscan> should be collapsed to a single point 
   This is true if:
     1. This is a yscan plane
     2. We are set to waveform renderer
     3. The bin size is greater then the size of the packet offsets
*/

bool shouldCollapse(PlaneDesc* pPlane){

	if(PlaneDesc_getType(pPlane) != YScan)
		return false;

	/* Property cascade can bring this in from the stream header */
	const char* sRend = DasDesc_getStr((DasDesc*)pPlane, "renderer");
	if(sRend == NULL) return false;
	if(strcmp("waveform", sRend) != 0) return false;

	das_units units = PlaneDesc_getYTagUnits(pPlane);
	if(! Units_canConvert(units, UNIT_SECONDS)) return false;


	// Get the max and min offset
	size_t uItems = PlaneDesc_getNItems(pPlane);
	double dMin = 0.0, dMax = 0.0;
	const double* pTags = PlaneDesc_getOrMakeYTags(pPlane);
	for(size_t u = 0; u < uItems; ++u){
		if(u == 0){
			dMin = pTags[u]; dMax = pTags[u];
		}
		else{
			if(dMin > pTags[u]) dMin = pTags[u];
			if(dMax < pTags[u]) dMax = pTags[u];
		}
	}

	das_units yunits = PlaneDesc_getYTagUnits(pPlane);
	double dRange = Units_convertTo(UNIT_MICROSECONDS, dMax - dMin, yunits);
	
	return (dRange <= g_rBinSzMicroSec);
}


/*****************************************************************************/
/* Data Processing */

/* Just store a 1 or a 0 in the location that's normally meant to hold a ptr.
   
   We store this value with the input plane descriptor so that we know where
   put min/max values when pulling data from the input so that on *output* 
   the stats arrays only have the same number of items as the output descriptor.
*/
   
#define SET_COLLAPSE(P) (P->pUser = (void*)1)
#define SET_NO_COLLAPSE(P) (P->pUser = (void*)0)
#define COLLAPSE(P) ((size_t)(P->pUser) == 1)


DasErrCode sendData(int nPktId)
{	
	/* Don't flush a packet that hasn't seen any data */
	if( ! g_lbHasBinNo[nPktId] ) 
		return 0;
	
	PktDesc* pPdOut = StreamDesc_getPktDesc(g_pSdOut, nPktId);
	
	double dTmp    = 0.0;
	double average = 0.0;
	double minVal = 0.0;
	double maxVal = 0.0;
	double sdVal = 0.0;
	DasAry* pAcc = NULL;
	const double* pAccVals = NULL;
	size_t uAccAllVals = 0;
	size_t uAccPkts = 0;
	size_t uAccOffsets = 0;
	PlaneDesc* pPlane = NULL;
	PlaneDesc* pMin   = NULL;
	PlaneDesc* pMax   = NULL;
	PlaneDesc* pStdDev = NULL;
	bool bRangeOut = g_bRangeOut; /* Avoid L1 cache miss in loop */
	bool bStdDevOut = g_bStdDevOut;
	
	for(size_t u = 0; u < g_uOrigPlanes[nPktId]; u++){

		pPlane = PktDesc_getPlane(pPdOut, u);
		
		if(pPlane->planeType == X){
			average = g_rBinSzMicroSec*(((double)g_lnBin[nPktId]) + 0.5) + 
			        g_rStartMicroSec;
			PlaneDesc_setValue(pPlane, u, average);
			continue;
		}

		size_t uItems = PlaneDesc_getNItems(pPlane);
		for(size_t v = 0; v < uItems; v++){
				
			if(g_ldCount[nPktId][u][v] == 0.0){
				average = PlaneDesc_getFill(pPlane);
				minVal = average;
				maxVal = average;
				sdVal  = average;
			}
			else{
				average = g_ldSum[nPktId][u][v] / g_ldCount[nPktId][u][v];

				if(bRangeOut){
					minVal = g_ldMin[nPktId][u][v];
					maxVal = g_ldMax[nPktId][u][v];
				}

				if(bStdDevOut){
					/* Run through all accumulated values for this "frequency" and get SD 
					 * -or-
					 * run through all accumulated values for all "offsets" and get SD */
					pAcc = g_lpAccum[nPktId][u];
					size_t uSzEa = 0;
					pAccVals = (double*)DasAry_getAllVals(pAcc, &uSzEa, &uAccAllVals);
					uAccPkts = DasAry_lengthIn(pAcc, DIM0);
					uAccOffsets = DasAry_lengthIn(pAcc, DIM1_AT(0));

					if(uAccPkts*uAccOffsets != uAccAllVals){
						return das_error(P_ERR, "Expected %zu*%zu = %zu accumlated values, have %zu",
							uAccPkts, uAccOffsets, uAccPkts*uAccOffsets, uAccAllVals
						);
					}

					sdVal = 0;
					if(uAccAllVals > 1){  /* SD of 1 value is == 0 */

						/* If these data are rank 2 but output is only rank 1, iteration
						   is not "per frequency" but over all offsets */
						if((uAccOffsets > 1) && (uItems == 1)){

							size_t uNonFill = 0;
							for(size_t uPkt = 0; uPkt < uAccPkts; ++uPkt){
								for(size_t uOff = 0; uOff < uAccOffsets; ++uOff){
									dTmp = pAccVals[uAccOffsets*uPkt + uOff]; /* I know I can stride, it's das2*/
									if(! PlaneDesc_isFill(pPlane, dTmp)){
										dTmp = (dTmp - average);
										sdVal += dTmp*dTmp;
										++uNonFill;
									}
								}
							}
							if(uNonFill > 1)
								sdVal /= (uNonFill - 1);		
						}
						else{
							for(size_t uPkt = 0; uPkt < uAccPkts; ++uPkt){
								dTmp = (pAccVals[uAccOffsets*uPkt + v] - average);
								sdVal += dTmp*dTmp;
							}
							sdVal /= (uAccPkts - 1);		
						}

						sdVal = sqrt(sdVal);
					}
				}
			}
			
			PlaneDesc_setValue(pPlane, v, average);
				
			if(bRangeOut){
				pMin = PktDesc_getPlane(pPdOut, g_uMinIndex[nPktId][u]);
				PlaneDesc_setValue(pMin, v, minVal);
				pMax = PktDesc_getPlane(pPdOut, g_uMaxIndex[nPktId][u]);
				PlaneDesc_setValue(pMax, v, maxVal);
						
				g_ldMin[nPktId][u][v]   = 0.0;
				g_ldMax[nPktId][u][v]   = 0.0;
			}

			if(bStdDevOut){
				pStdDev = PktDesc_getPlane(pPdOut, g_uStdDevIndex[nPktId][u]);
				PlaneDesc_setValue(pStdDev, v, sdVal);
			}
		
			g_ldSum[nPktId][u][v]   = 0.0;
			g_ldCount[nPktId][u][v] = 0.0;
		}

		if(g_lpAccum[nPktId][u] != NULL)
			DasAry_clear(g_lpAccum[nPktId][u]);
	}

	g_lbHasBinNo[nPktId] = false;
	g_lnBin[nPktId] = 0;
	
	return DasIO_writePktData(g_pIoOut, pPdOut);
}

/*****************************************************************************/
/* Packet Header Processing */

DasErrCode onPktHdr(StreamDesc* pSdIn, PktDesc* pPdIn, void* v)
{
	int nPktId = PktDesc_getId(pPdIn);
	/* char sNewName[128] = {'\0'}; */
	
	/* If this output packet ID already exists, kick out any data associated
	   with it and delete it */
	if(StreamDesc_isValidId(g_pSdOut, nPktId)){
		sendData(nPktId);
		StreamDesc_freeDesc(g_pSdOut, nPktId);
	}
	
	/* Deepcopy's the packet descriptor preserving the ID */
	PktDesc* pPdOut = StreamDesc_clonePktDescById(g_pSdOut, pSdIn, nPktId);
	
	/* Indicate that the bin number field for this packet is not valid */
	g_lbHasBinNo[nPktId] = false;
	
	g_uOrigPlanes[nPktId] = PktDesc_getNPlanes(pPdIn);
	
	/* Check to see if we have room for min/max planes, we almost always
	 * do because it's rare to have over 25 planes in the input */
	int nMaxInPlanes = 99;
	if(g_bRangeOut && ! g_bStdDevOut) nMaxInPlanes = 33;
	if(!g_bRangeOut && g_bStdDevOut)  nMaxInPlanes = 49;
	if(g_bRangeOut && g_bStdDevOut)   nMaxInPlanes = 24;
	if(g_uOrigPlanes[nPktId] >= nMaxInPlanes ){
		OobExcept oob;
		OobExcept_init(&oob);
		strncpy(oob.sType, DAS2_EXCEPT_SERVER_ERROR, oob.uTypeLen - 1);
		snprintf(oob.sMsg, oob.uMsgLen - 1,
			"Only 99 output planes supported. Due to requested extra planes"
			" (if any) only %d input planes are supported.", nMaxInPlanes
		);
		return onException(&oob, NULL);
	}
	
			
	/* Init the sums and counts, and make sure the output units are us2000 */
	PlaneDesc* pPlIn  = NULL;
	PlaneDesc* pPlOut = NULL;
	PlaneDesc* pPlNew = NULL;
	PlaneDesc* pMinPlane = NULL;
	PlaneDesc* pMaxPlane = NULL;
	PlaneDesc* pStdDevPlane = NULL;
	size_t uItems = 0;
	char sNewVar[128] = {'\0'};
	
	for(size_t u = 0; u < g_uOrigPlanes[nPktId]; u++){

		pPlOut = PktDesc_getPlane(pPdOut, u);
		pPlIn  = PktDesc_getPlane(pPdIn,  u);

		/* No data are stored for X planes, all information about the location of
		   points in a bin is dumped.  We could keep this if needed */
		if(pPlOut->planeType == X){
			pPlOut->units = UNIT_US2000;
			continue;
		}

		if(shouldCollapse(pPlOut)){

			pPlNew = new_PlaneDesc(
				Y, PlaneDesc_getName(pPlOut), 
				DasEnc_copy( PlaneDesc_getValEncoder(pPlOut) ),
				PlaneDesc_getUnits(pPlOut)
			);
			PlaneDesc_setFill(pPlNew, PlaneDesc_getFill(pPlOut));
			DasDesc_copyIn((DasDesc*)pPlNew, (DasDesc*)pPlOut);

			PktDesc_replaceAt(pPdOut, u, pPlNew);
			del_PlaneDesc(pPlOut);
			pPlOut = pPlNew;
			SET_COLLAPSE(pPlIn);
		}
		else{
			SET_NO_COLLAPSE(pPlIn);	
		}

		uItems = PlaneDesc_getNItems(pPlOut);
			
		/* Min */
		if(g_bRangeOut){
			pMinPlane = PlaneDesc_copy(pPlOut);
			snprintf(sNewVar, 127, "%s.min", PlaneDesc_getName(pPlOut));
			PlaneDesc_setName(pMinPlane, sNewVar);
			DasDesc_setStr((DasDesc*)pMinPlane, "source", PlaneDesc_getName(pPlOut));
			DasDesc_setStr((DasDesc*)pMinPlane, "operation", "BIN_MIN");
			g_uMinIndex[nPktId][u] = PktDesc_addPlane(pPdOut, pMinPlane);


			/* The "out" plane is derived as well, it's the averages */
			DasDesc_setStr((DasDesc*)pPlOut, "source", PlaneDesc_getName(pPlOut));
			DasDesc_setStr((DasDesc*)pPlOut, "operation", "BIN_AVG");

			/* Max */
			pMaxPlane = PlaneDesc_copy(pPlOut);
			snprintf(sNewVar, 127, "%s.max", PlaneDesc_getName(pPlOut));
			PlaneDesc_setName(pMaxPlane, sNewVar);
			DasDesc_setStr((DasDesc*)pMaxPlane, "source", PlaneDesc_getName(pPlOut));
			DasDesc_setStr((DasDesc*)pMaxPlane, "operation", "BIN_MAX");
			g_uMaxIndex[nPktId][u] = PktDesc_addPlane(pPdOut, pMaxPlane);
		}

		/* stdddev */
		if(g_bStdDevOut){
			pStdDevPlane = PlaneDesc_copy(pPlOut);
			snprintf(sNewVar, 127, "%s.stddev", PlaneDesc_getName(pPlOut));
			PlaneDesc_setName(pStdDevPlane, sNewVar);
			DasDesc_setStr((DasDesc*)pStdDevPlane, "source", PlaneDesc_getName(pPlOut));
			DasDesc_setStr((DasDesc*)pStdDevPlane, "operation", "BIN_STDDEV");
			g_uStdDevIndex[nPktId][u] = PktDesc_addPlane(pPdOut, pStdDevPlane);
		}
		
		if(g_ldSum[nPktId][u] != NULL) free(g_ldSum[nPktId][u]);
		if(g_ldCount[nPktId][u] != NULL) free(g_ldCount[nPktId][u]);
		
		if(g_bRangeOut){
			if(g_ldMin[nPktId][u] != NULL) free(g_ldMin[nPktId][u]);
			if(g_ldMax[nPktId][u] != NULL) free(g_ldMax[nPktId][u]);
		}
		if(g_bStdDevOut){
			if(g_lpAccum[nPktId][u] != NULL)
				dec_DasAry(g_lpAccum[nPktId][u]);
		}
		
		g_ldSum[nPktId][u] = (double*)calloc(uItems, sizeof(double));
		g_ldCount[nPktId][u] = (double*)calloc(uItems, sizeof(double));
		
		if(g_bRangeOut){
			g_ldMin[nPktId][u] = (double*)calloc(uItems, sizeof(double));
			g_ldMax[nPktId][u] = (double*)calloc(uItems, sizeof(double));
		}
		if(g_bStdDevOut){
			double dFill = PlaneDesc_getFill(pPlOut);
			g_lpAccum[nPktId][u] = new_DasAry(
				PlaneDesc_getName(pPlOut), vtDouble, 0, (const ubyte*) &dFill,
				RANK_2(0, uItems), PlaneDesc_getUnits(pPlOut)
			);
		}
	}	

	return DasIO_writePktDesc(g_pIoOut, pPdOut);
}

/*****************************************************************************/
/* Packet Data Processing */

DasErrCode onPktData(PktDesc* pPdIn, void* ud)
{
	int nRet = 0;
	int nPktId = PktDesc_getId(pPdIn);
	bool bRange = g_bRangeOut; /* Avoid L1 cache miss in tight loop */
	
	/* Check to see if this is the first time this packet type has
	   been seen  (Note: output packet ID's mirror the input) */
	PlaneDesc* pX = PktDesc_getXPlane(pPdIn);
	double rCurTime = PlaneDesc_getValue(pX, 0);
	rCurTime = Units_convertTo(UNIT_US2000, rCurTime, PlaneDesc_getUnits(pX));
	
	/* Could be the first data packet of the entire stream */
	if(! g_bHasStartTime){
		g_rStartMicroSec = rCurTime;
		g_bHasStartTime = true;
	}
	
	long nCurBin = (rCurTime - g_rStartMicroSec)/g_rBinSzMicroSec;
	if(g_lbHasBinNo[nPktId]){
		if(nCurBin != g_lnBin[nPktId])
			if( (nRet = sendData(nPktId)) != 0) return nRet;
	}
	
	g_lnBin[nPktId] = nCurBin;
	g_lbHasBinNo[nPktId] = true;
	
	PktDesc* pPdOut = StreamDesc_getPktDesc(g_pSdOut, nPktId);
	PlaneDesc* pOutPlane = NULL;
	PlaneDesc* pInPlane = NULL;
	const double* pVals = NULL;
	int nXPlanes = 0;
	for(size_t u = 0; u < PktDesc_getNPlanes(pPdIn); u++){
		pOutPlane = PktDesc_getPlane(pPdOut, u);
		
		if(pOutPlane->planeType == X){ 
			nXPlanes += 1;
			if(nXPlanes > 1) 
				return das_error(P_ERR, "das2_bin_avgsec reducer can't handle "
						            "packets with more than one X plane.");
			continue;
		}
		
		pInPlane = PktDesc_getPlane(pPdIn, u);
		pVals = PlaneDesc_getValues(pInPlane);
		size_t nVals = PlaneDesc_getNItems(pInPlane);
		size_t vOut = 0;
		for(size_t v = 0; v < nVals; v++){
			
			if(PlaneDesc_isFill(pInPlane, pVals[v]))
				continue;

			vOut = COLLAPSE(pInPlane) ? 0 : v;
			
			// the sum / count calculation would fail for TT2000 long integers.
			// but this calculation is ignored for X planes.
			g_ldSum[nPktId][u][vOut] += pVals[v];
			g_ldCount[nPktId][u][vOut] += 1;
			if(bRange){
					
				if(g_ldCount[nPktId][u][vOut] == 1){
					g_ldMin[nPktId][u][vOut] = pVals[v];
					g_ldMax[nPktId][u][vOut] = pVals[v];
				}
				else{
					if(pVals[v] < g_ldMin[nPktId][u][vOut])
						g_ldMin[nPktId][u][vOut] = pVals[v];
					
					if(pVals[v] > g_ldMax[nPktId][u][vOut])
						g_ldMax[nPktId][u][vOut] = pVals[v];
				}
			}
		}

		/* They want to accumlate data for some reason, so do it. */
		if(g_lpAccum[nPktId][u] != NULL){
			// Push all packet points to the accumlate buffer as long
			// as one of them is not fill
			for(size_t v = 0; v < nVals; v++){
				if(PlaneDesc_isFill(pInPlane, pVals[v]))
					continue;
				DasAry_append(g_lpAccum[nPktId][u], (const ubyte*) pVals, nVals);
				break;
			}
		}
	}
	return nRet;
}

/* ************************************************************************* */
/* Stream Close Handling */
DasErrCode onClose(StreamDesc* pSdIn, void* ud){
	
	int nPktId;
	DasErrCode nRet = 0;
	for (nPktId = 1; nPktId < 100; nPktId++){
		if(StreamDesc_isValidId(g_pSdOut, nPktId)){
			if( (nRet = sendData(nPktId) ) != 0) return nRet;
		}
	}
	return 0;
}

/*****************************************************************************/
void prnHelp()
{
	fprintf(stderr, 
"SYNOPSIS\n"
"   das2_bin_avgsec - Reduces the size of Das2 streams by averaging over time.\n"
"\n"
"USAGE\n"
"   das2_bin_avgsec [-r] [-b BEGIN] BIN_SECONDS\n"
"\n"
"DESCRIPTION\n"
"   das2_bin_avgsec is a classic Unix filter, reading das2 streams on standard\n"
"   input and producing a time-reduced das2 stream on standard output.  The\n" 
"   program averages <y> and <yscan> data values over time, but does not\n"
"   preform rebinning across packet types.  Only values with the same packet\n"
"   ID and the same plane name are averaged.  Within <yscan> planes, only\n"
"   Z-values with the same Y coordinate are combined.\n"
"\n"
"   It is assumed that <x> plane values are time points.  For this reducer\n"
"   only the following <x> unit values are allowed:\n"
"\n"
"      * us2000 - Microseconds since midnight, January 1st 2000\n"
"      * t2000  - Seconds since midnight, January 1st 2000\n"
"      * mj1958 - Days since midnight January 1st 1958\n"
"      * t1970  - Seconds since midnight, January 1st 1970\n"
"\n"
"   All time values, regardless of scale, epoch, or representation in the\n"
"   input stream are handled as 8-byte IEEE floating point numbers internally.\n"
"   ASCII times are converted internally to us2000 values.\n"
"\n"
"   The BIN_SECONDS parameter provides the number of seconds over which to \n"
"   average <y> and <yscan> plane values.  Up to total of 99 <y> and <yscan> \n"
"   planes may exist in each packet type, and up to 99 packet types may exist\n"
"   in the input stream.  This is a plane limit, not a limit on the total\n"
"   number of data vectors.  <yscan> planes may contain an arbitrary number\n"
"   of vectors.  The output stream has the same number of packet types and \n"
"   planes as the input stream, but presumably with many fewer time points.\n"
"\n"
"   Waveform Table Warning!\n"
"   -----------------------\n"
"   If an input <yscan> plane for represents offsets from a reference time\n"
"   then the size of the <yscan> can change!  In fact if the new bin size\n"
"   is smaller then the full range of all offsets in a <yscan> then then\n"
"   it will be replacen in the output by a single <y> plane instead.\n"
"\n"
"OPTIONS\n"
"   -h        Generate this message.\n"
"\n"
"   -b BEGIN  Instead of starting the 0th bin at the first time value \n"
"             received, specify a starting bin.  This useful when creating\n"
"             pre-generated caches of binned data as it keeps the bin \n"
"             boundaries predictable.\n"
"\n"
"   -r        Generate two new variables in each physical data dimension \n"
"             (not coordinate dimension) that provide the RANGE of the data.\n"
"             One of the new variables contains the minumum value for each\n"
"             bin, and the other the minimum value for each bin.\n"
"\n"
"   -s        Generate a new variable in each physical data dimension\n"
"             that contains the standard deviation of values in the each bin.\n"
"\n"
"   -p        Drop stream progress messages.  This is useful when caching\n"
"             reduced resolution streams.\n"
"\n"
"DAS2 PROPERTIES\n"
"   das2_bin_avgsec sets the following <stream> properties on output:\n"
"\n"
"      xCacheResolution - Set to a Datum that represents the binning period\n"
"\n"
"      xCacheResInfo - Set to human readable string representing the binning\n"
"         period.  Readers may wish to use macro substitution to place this\n"
"         string in labels and titles.\n"   
"\n"
"LIMITATIONS"
"   This is a 1-dimensional averager, <x>, <y>, <z> scatter data are not\n"
"   handled by this reducer.\n"
"\n"
"AUTHORS\n"
"   chris-piker@uiowa.edu   (2015 revised)\n"
"   jeremy-faden@uiowa.edu  (original)\n"
"\n"
"SEE ALSO\n"
"   das2_bin_avg, das2_bin_peakavgsec, das2_ascii, das2_cache_rdr\n"
"\n"
"   The das 2 ICD @ http://das2.org for an introduction to the das 2 system.\n"
"\n");
}


/* ************************************************************************** */
/* This is just like binAverage, except that the arg is a float in seconds    */
 
int main(int argc, char *argv[])
{
	int iBinSzArg = 1;
	double rBinSize;
	int nRet = 0;
	g_bRangeOut = false;
	
	/* Exit on errors, log info messages and above */
	das_init(argv[0], DASERR_DIS_EXIT, 0, DASLOG_INFO, NULL);
	
	if(argc < 2){
		fprintf(stderr, "Usage das2_bin_avgsec BIN_SIZE_SECS\n\nIssue -h"
              " to output the help page.\n");
		return 4;
	}
	  
	if(strcmp(argv[1], "-h") == 0 || strcmp(argv[1], "--help") == 0){
		prnHelp();
		return 0;
	}
	
	das_time dt = {0};
	for(int i = 1; i < argc; i++){
		if(strcmp(argv[i], "-b") == 0){
			if(i+1 == argc)
				return das_error(P_ERR, "Begin bin position missing after -b");
			iBinSzArg += 2;
			if(! dt_parsetime(argv[i+1], &dt))
				return das_error(P_ERR, "Couldn't convert %s to a date-time", 
						            argv[i+1]);
			g_rStartMicroSec = Units_convertFromDt(UNIT_US2000, &dt);
			g_bHasStartTime = true;
			continue;
		}
		if(strcmp(argv[i], "-r") == 0){
			g_bRangeOut = true;
			iBinSzArg += 1;
			continue;
		}
		if(strcmp(argv[i], "-s") == 0){
			g_bStdDevOut = true;
			iBinSzArg += 1;
			continue;
		}
		if(strcmp(argv[i], "-p") == 0){
			g_bProgress = false;
			iBinSzArg += 1;
			continue;
		}
	}
	
	if(argc != 1 + iBinSzArg){
		fprintf(stderr, "Usage: das2_bin_avgsec [-r] [-b begin] BIN_SECONDS \n"
		        "Issue the command %s -h for more info.\n\n", argv[0]);
		return P_ERR;
	}
	
	/* Handle bin size argument */
	sscanf(argv[iBinSzArg], "%lf", &rBinSize);
	if(rBinSize == 0.0){
		fprintf(stderr, "Output bin size must be bigger than 0 seconds!");
		return P_ERR;
	}
	g_rBinSzMicroSec = rBinSize * 1.0e6;  /* Convert to microseconds */
	
	
	g_pIoOut = new_DasIO_cfile("das2_bin_avgsec", stdout, "w");

	StreamHandler* pSh = new_StreamHandler(NULL);
	pSh->streamDescHandler = onStreamHdr;
	pSh->pktDescHandler = onPktHdr;
	pSh->pktDataHandler = onPktData;
	pSh->closeHandler = onClose;
   pSh->commentHandler = onComment;
	pSh->exceptionHandler = onException;
	
	DasIO* pIn = new_DasIO_cfile("Standard Input", stdin, "r");
	DasIO_addProcessor(pIn, pSh);
	 
	nRet = DasIO_readAll(pIn);
	del_DasIO(pIn);  /* make valgrind happy, but maybe delete this line later */
	free(pSh);       /* also to make valgrind happy */
	pIn = NULL;
	return nRet;
}
