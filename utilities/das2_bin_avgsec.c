/* Copyright (C) 2004       Jeremy Faden <jeremy-faden@uiowa.edu>
 *               2015-2017  Chris Piker  <chris-piker@uiowa.edu>
 *                         
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

#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <math.h>

#include <das2/core.h>

#define P_ERR 100

/* ************************************************************************* */
/* Record Keeping */

bool g_bAnnotations = true;         /* Forward or drop stream annotations */
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

/* Sum accumulation array, one for each plane of each packet type, fill values
 * aren't added to the count */
double* g_ldSum[100][MAXPLANES] = {{NULL}};

/* Counting array, one for each plane of each packet type, fill values aren't
   added to the count. */
double* g_ldCount[100][MAXPLANES] = {{NULL}};

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

	return DasIO_writeStreamDesc(g_pIoOut, g_pSdOut);
}

/*****************************************************************************/
/* Data Processing */

DasErrCode sendData(int nPktId)
{	
	/* Don't flush a packet that hasn't seen any data */
	if( ! g_lbHasBinNo[nPktId] ) return 0;
	
	PktDesc* pPdOut = StreamDesc_getPktDesc(g_pSdOut, nPktId);
	
	double value = 0.0;
	PlaneDesc* pPlane = NULL;
	for(size_t p = 0; p < PktDesc_getNPlanes(pPdOut); p++){
		pPlane = PktDesc_getPlane(pPdOut, p);
		
		for(size_t i = 0; i < PlaneDesc_getNItems(pPlane); i++){
			if(pPlane->planeType == X){
				value = g_rBinSzMicroSec*(((double)g_lnBin[nPktId]) + 0.5) + 
						  g_rStartMicroSec;
			}
			else{
				if(g_ldCount[nPktId][p][i] == 0.0)
					value = PlaneDesc_getFill(pPlane);
				else
					value = g_ldSum[nPktId][p][i] / g_ldCount[nPktId][p][i];
			}
			PlaneDesc_setValue(pPlane, i, value);
			g_ldSum[nPktId][p][i] = 0.0;
			g_ldCount[nPktId][p][i] = 0.0;
		}
		
		g_lbHasBinNo[nPktId] = false;
		g_lnBin[nPktId] = 0;
	}
	
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
		StreamDesc_freePktDesc(g_pSdOut, nPktId);
	}
	
	/* Deepcopy's the packet descriptor preserving the ID */
	PktDesc* pPdOut = StreamDesc_clonePktDescById(g_pSdOut, pSdIn, nPktId);
	
	/* Indicate that the bin number field for this packet is not valid */
	g_lbHasBinNo[nPktId] = false;
			
	/* Init the sums and counts, and make sure the output units are us2000 */
	PlaneDesc* pPlOut = NULL;
	size_t uItems = 0;
	for(size_t u = 0; u < PktDesc_getNPlanes(pPdOut); u++){
		pPlOut = PktDesc_getPlane(pPdOut, u);
		uItems = PlaneDesc_getNItems(pPlOut);
		if(pPlOut->planeType == X){
			pPlOut->units = UNIT_US2000;
			
			/* pPlOut->pEncoding = new_DasEncoding(DAS2DT_HOST_REAL, 8, NULL); */
		}
		else{
			/* Use the existing name for the upstream mention */
			
			/* This was a good idea, but it totally screws with autoplot
			   don't know what to do to fix it, with be nice to see peaks
				and averages when one zooms out and then just regular data
				otherwise but it will require a change in architecture to 
				make that happen.  -cwp 2016-05-31 */
			
			/* Desc_setPropStr((Descriptor*)pPlOut, "source", PlaneDesc_getName(pPlOut)); */
			/* Desc_setPropStr((Descriptor*)pPlOut, "operation", "BIN_AVG"); */
			
			/* Now change the name for the downstream output*/
			/* snprintf(sNewName, 127, "%s_avg", PlaneDesc_getName(pPlOut)); */
			/* PlaneDesc_setName(pPlOut, sNewName); */
		}
		
		if(g_ldSum[nPktId][u] != NULL) free(g_ldSum[nPktId][u]);
		if(g_ldCount[nPktId][u] != NULL) free(g_ldCount[nPktId][u]);
		
		g_ldSum[nPktId][u] = (double*)calloc(uItems, sizeof(double));
		g_ldCount[nPktId][u] = (double*)calloc(uItems, sizeof(double));
	}	

	return DasIO_writePktDesc(g_pIoOut, pPdOut);
}

/*****************************************************************************/
/* Packet Data Processing */

DasErrCode onPktData(PktDesc* pPdIn, void* ud)
{
	int nRet = 0;
	int nPktId = PktDesc_getId(pPdIn);
	
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
	for(size_t u = 0; u < PktDesc_getNPlanes(pPdOut); u++){
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
		for(size_t v = 0; v < PlaneDesc_getNItems(pInPlane); v++){
			if(!PlaneDesc_isFill(pInPlane, pVals[v])){
				g_ldSum[nPktId][u][v] += pVals[v];
				g_ldCount[nPktId][u][v] += 1;
			}
		}
	}
	return nRet;
}

/* ************************************************************************* */
/* Maybe copy out Exceptions and Comments */
DasErrCode onException(OobExcept* pExcept, void* vpOut)
{
	if(! g_bAnnotations) return 0;
	return DasIO_writeException(g_pIoOut, pExcept);
}

DasErrCode onComment(OobComment* pCmt, void* vpOut)
{
	if(! g_bAnnotations) return 0;
	
	return DasIO_writeComment(g_pIoOut, pCmt);
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
"   das2_bin_avgsec [-b BEGIN] BIN_SECONDS\n"
"\n"
"DESCRIPTION\n"
"   das2_bin_avgsec is a classic Unix filter, reading Das 2 Streams on standard\n"
"   input and producing a time-reduced Das 2 stream on standard output.  The\n" 
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
"OPTIONS\n"
"   -b BEGIN  Instead of starting the 0th bin at the first time value \n"
"             received, specify a starting bin.  This useful when creating\n"
"             pre-generated caches of binned data as it keeps the bin \n"
"             boundaries predictable."
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
"   jeremy-faden@uiowa.edu  (original)\n"
"   chris-piker@uiowa.edu   (current maintainer)\n"
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
	
	if(strcmp(argv[1], "-v") == 0 || strcmp(argv[1], "--version") == 0){
		printf("$Header: https://saturn.physics.uiowa.edu/svn/das2/core/stable/libdas2_3/utilities/das2_bin_avgsec.c 11232 2019-02-20 19:38:56Z cwp $\n");
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
			break;
		}
	}
	
	if(argc != 1 + iBinSzArg){
		fprintf(stderr, "Usage: das2_bin_avgsec [-b begin] BIN_SECONDS \n"
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
