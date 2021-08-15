/* Copyright (C) 2017   Chris Piker <chris-piker@uiowa.edu>
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


/******************************************************************************
 das2_psd: Convert incoming series data to a Fourier transform of the series.
	        This program changes the morphology of the stream, examples are:


Packet Remapping, Transform over X:  <y> to <yscan>

   <x> <y A> <y B>
   <x> <y A> <y B>  ===>   <x> <yscan A> <yscan B>
   <x> <y A> <y B>


Packet Remapping, Transform over X:  <yscan> to <yscan>

   <x> <yscan A> <yscan B>       <x> <yscan A> <yscan B>
   <x> <yscan A> <yscan B>  ===> <x> <yscan A> <yscan B>
	<x> <yscan A> <yscan B> 		<x> <yscan A> <yscan B>

Packet Remapping, Transform over Y:  <yscan> to <yscan>

                                  <x>    <yscan A> <yscan B>
   <x> <yscan A> <yscan B>  ===>  <x+>   <yscan A> <yscan B>
                                  <x++>  <yscan A> <yscan B>


Packet Remapping, Transform over Y with un-transformable yscan ( here yscan
   B is too short to transform, so it's dropped ):

                                            <x>    <yscan A> <yscan C>
   <x> <yscan A> <yscan B> <yscan C>  ===>  <x+>   <yscan A> <yscan C>
                                            <x++>  <yscan A> <yscan C>

See the help text in prnHelp() for mor info. -cwp

*******************************************************************************/

#define _POSIX_C_SOURCE 200112L

#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <math.h>

#include <das2/core.h>
#include "send.h"

#define PROG_NAME "das2_psd"

#define P_ERR 105

#ifndef DasIO_throw
#define DasIO_srverr(pIo, pSd, sMsg) \
  DasIO_throwException(pIo, pSd, "DAS2_SERVER_ERROR", sMsg); exit(P_ERR);
#endif

/* ************************************************************************* */
/* Globals */

StreamDesc* g_pSdOut = NULL;
DftPlan* g_pDftPlan = NULL;        /* The PSD plan */
Das2Psd* g_pPsdCalc = NULL;        /* The PSD calculator */

#define TRANSFORM_UNK   0x00
#define TRANSFORM_IN_X  0x01
#define TRANSFORM_IN_Y  0x02
#define NO_TRANSFORM    0xFF

size_t g_uDftLen = 0;
size_t g_uSlideDenom = 0;
das_datum g_cadence;
bool g_bSkip = true;
int g_nPktsOut = 0;

const double g_rEpsilon = 0.01; /* Time difference maximum jitter for DFT */
#define g_sEplion "0.01"        /* String verision of the above value */

/* We remap packet IDs starting with 1, since this code can trigger
   packet ID collapse (xscan) and packet ID expansion (yscan) */
int g_nNextPktId = 1;

/* ************************************************************************* */
/* Ancillary tracking */

/** Data accumulation structure assigned to pUser for each out going conversion
 * of one <x> or <y> plane in an x_multi_y packet.  These are needed because the
 * output packet buffer is too small to accumulate g_DftLen points since the
 * output buffer is g_DftLen/2 + 1.  These aren't needed for DFTs over time
 * offsets because they transform in one go. */
typedef struct accum {
	int nPre;       /* Number of collected pre-commit points */
	double aPre[2]; /* pre-commit buffer. Points move form here to the data
						  * buffer once they pass a jitter check */
	int iNext;      /* The index of the next point to store (also current size) */
	int nSize;      /* The size of the buffer */
	double* pData;  /* Either X or Y data */
} Accum;

Accum* new_Accum(int nSize)
{
	Accum* pThis = (Accum*) calloc(1, sizeof(Accum));
	pThis->pData = (double*) calloc(nSize, sizeof(double));
	pThis->nSize = nSize;
	/* The rest auto-init to 0 */
	return pThis;
}

void del_Accum(Accum* pThis)
{
	if(pThis == NULL) return;
	if(pThis->pData != NULL) free(pThis->pData);
	free(pThis);
}

/** Ancillary tracking structure assigned to pUser for every out-going yscan
 * plane.  Used to keep track of output data scaling (if any) as well as
 * data accumulation when needed.
 */
typedef struct aux_info {
	das_datum dmTau;
	int iMinDftOut;  /* Minimum PSD index to output, usually 0 */
	int iMaxDftOut;  /* Maximum PSD index to output, usually len/2 + 1 */
	double rYOutScale;
	double rZOutScale;
	Accum* pAccum;
}AuxInfo;

AuxInfo* new_AuxInfo(int nDftLen)
{
	AuxInfo* pThis = (AuxInfo*)calloc(1, sizeof(AuxInfo));

	/* Time between samples in X output units */
	das_datum_fromDbl(&(pThis->dmTau), 1.0, UNIT_SECONDS);
	pThis->iMinDftOut = 0;
	pThis->iMaxDftOut = nDftLen/2 + 1;
	pThis->rYOutScale = 1.0;
	pThis->rZOutScale = 1.0;
	pThis->pAccum = NULL;
	return pThis;
}

void del_AuxInfo(void* vpThis)
{
	AuxInfo* pThis = (AuxInfo*)vpThis;
	del_Accum(pThis->pAccum);
	free(vpThis);
}

/*****************************************************************************/
/* Stream Header Processing */

DasErrCode onStreamHdr(StreamDesc* pSdIn, void* vpIoOut)
{
	g_pSdOut = StreamDesc_copy(pSdIn);

	/* Remove old Y and Z labels */
	DasDesc_remove((DasDesc*)g_pSdOut, "yLabel");
	DasDesc_remove((DasDesc*)g_pSdOut, "zLabel");

	/* Override the renderer and source ID*/
	DasDesc_setStr((DasDesc*)g_pSdOut, "yScaleType", "linear");
	DasDesc_setStr((DasDesc*)g_pSdOut, "zScaleType", "log");
	DasDesc_setStr((DasDesc*)g_pSdOut, "renderer", "spectrum");
	DasDesc_vSetStr((DasDesc*)g_pSdOut, "sourceId", "das2_psd %d %d",
			           g_uDftLen, g_uSlideDenom);

	/* Set a title using replacements.  Das2 clients don't have to update the
	 * title on every load but will substitute new replacement values when they
	 * change */
	char sTitle[256] = {'\0'};
	const char* sProp = NULL;
	if( (sProp = DasDesc_get((DasDesc*)g_pSdOut, "title")) != NULL){
		strncpy(sTitle, sProp, 255);
		DasDesc_vSetStr((DasDesc*)g_pSdOut, "title",
		"%s!c%%{DFT_length} point DFT, %%{xDftOverlapInfo}", sTitle);
	}
	else{
		DasDesc_vSetStr((DasDesc*)g_pSdOut, "title",
		"!c%%{DFT_length} point DFT, %%{xDftOverlapInfo}");
	}

	if(das_datum_valid(&g_cadence)){
		DasDesc_setDatum((DasDesc*)g_pSdOut, "xTagWidth",
				            das_datum_toDbl(&g_cadence), g_cadence.units);
	}

	char sOverlap[64] = {'\0'};
	if(g_uSlideDenom == 1) strncpy(sOverlap, "No Overlap", 63);
	else snprintf(sOverlap, 63, "%zu/%zu Overlap", g_uSlideDenom - 1, g_uSlideDenom);
	DasDesc_setStr((DasDesc*)g_pSdOut, "xDftOverlapInfo", sOverlap);

	DasDesc_setInt((DasDesc*)g_pSdOut, "DFT_length", g_uDftLen);
	DasDesc_setInt((DasDesc*)g_pSdOut, "DFT_slide_denominator", g_uSlideDenom);
	DasDesc_setStr((DasDesc*)g_pSdOut, "DFT_window", "hann");

	return DasIO_writeStreamDesc((DasIO*)vpIoOut, g_pSdOut);
}

/* Get previously defined packet header that is identical to the given one,
 * it must be unique! */
PktDesc* hasMatchingPktDef(DasIO* pOut, StreamDesc* pSd, PktDesc* pPd)
{
	if(pPd == NULL){
		DasIO_srverr(pOut, pSd, "Assertion error in hasMatchingPktDef()");
	}

	PktDesc* pCheck = NULL;
	bool bMatch = false;
	PlaneDesc* pChkPlane = NULL;
	PlaneDesc* pPlane = NULL;
	int nPktId, iPlane;

	for(nPktId = 0; nPktId < 100; ++nPktId){

		if(!StreamDesc_isValidId(pSd, nPktId)) continue;
		pCheck = StreamDesc_getPktDesc(pSd, nPktId);

		if(PktDesc_getNPlanes(pCheck) != PktDesc_getNPlanes(pPd)) continue;

		bMatch = true;
		for(iPlane = 0; iPlane < PktDesc_getNPlanes(pCheck); ++iPlane){
			pChkPlane = PktDesc_getPlane(pCheck, iPlane);
			pPlane = PktDesc_getPlane(pPd, iPlane);
			if(! PlaneDesc_equivalent(pChkPlane, pPlane)){ bMatch = false; break; }
		}
		if(!bMatch) continue;

		return pCheck;
	}
	return NULL;
}

/* ************************************************************************** */
/* Helper building yscans, get the output frequency set definition.
 *
 * This is a little tricky,
 *
 * @param rDeltaF The frequency interval taken by getting the inverse of the
 *        time domain units, possibly scaled.
 *
 * @param utFreq the units of delta F.
 *
 * @param pTagMin The minimum yTag value that will be output in units of
 *        utFreq.  Not necessarily the lowest value out of the PSD calculator
 *
 * @param pMinDftIdx The index of the output value from the PSD calculator that
 *        corresponds to the minimum yTag value
 *
 * @param pLen the number of DFT points to write
 */

void _getOutFreqDef(
	const PlaneDesc* pPlane, double rDeltaF, das_units utFreq, double* pTagMin,
	int* pMinDftIdx, int* pLen
){
	double rMinFreq = NAN;
	const char* sMinFreq = NULL;

	int nRealDftLen = (g_uDftLen/2) + 1;

	if((sMinFreq = DasDesc_get((DasDesc*)pPlane, "DFT_freqTagMin")) == NULL){
		rMinFreq = 0;
	}
	else{
		rMinFreq = DasDesc_getDatum((DasDesc*)pPlane, "DFT_freqTagMin", utFreq);
		if(rMinFreq == DAS_FILL_VALUE){
			das_error(P_ERR, "Couldn't convert shift frequency datum '%s' to "
					"units of %s", sMinFreq, utFreq);
			exit(P_ERR);
		}
	}

	/* If there are no trim instructions, we're done.*/
	const char* sMinY = NULL;
	double rMinY = NAN;
	const char* sMaxY = NULL;
	double rMaxY = NAN;

	sMinY = DasDesc_get((DasDesc*)pPlane, "DFT_freqTrimMin");
	sMaxY = DasDesc_get((DasDesc*)pPlane, "DFT_freqTrimMax");

	if((sMinY == NULL)&&(sMaxY == NULL)){
		*pLen = nRealDftLen;
		*pMinDftIdx = 0.0;
		*pTagMin = rMinFreq;
		return;
	}

	/* Some trim is requested, need to calculate yTags */
	if(sMinY != NULL){
		rMinY = DasDesc_getDatum((DasDesc*)pPlane, "DFT_freqTrimMin", utFreq);
		if(rMinY == DAS_FILL_VALUE){
			das_error(P_ERR, "Couldn't convert minimum frequency trim datum '%s' to "
			           "units of %s", sMinY, utFreq);
			exit(P_ERR);
		}
	}
	if(sMaxY != NULL){
		rMaxY = DasDesc_getDatum((DasDesc*)pPlane, "DFT_freqTrimMax", utFreq);
		if(rMaxY == DAS_FILL_VALUE){
			das_error(P_ERR, "Couldn't convert maximum frequency trim datum '%s' to "
			           "units of %s", sMaxY, utFreq);
			exit(P_ERR);
		}
	}

	int nItems = 0;
	*pMinDftIdx = -1;
	double rFreq = NAN;
	for(int i = 0; i < nRealDftLen; i++){
		rFreq = rDeltaF*i + rMinFreq;
		if((sMinY != NULL)&& rFreq < rMinY) continue;
		if((sMaxY != NULL)&& rFreq >= rMaxY) continue;
		++nItems;
		if(*pMinDftIdx < 0) *pMinDftIdx = i;
	}
	*pTagMin = (*pMinDftIdx)*rDeltaF + rMinFreq;
	*pLen = nItems;
}

/*****************************************************************************/
/* Helper for onPktHdr, invert a Y plane, output units depend on 1st X plane */

void _setSource(PlaneDesc* pPldOut, const char* sSource)
{

	/* Don't cite a source if you've replaced it, just reuse it's name */
	/* Desc_setPropStr((Descriptor*)pPldOut, "source", sSource);*/

	const char* sPriorOps = DasDesc_getStr((DasDesc*)pPldOut, "operation");

	if(sPriorOps)
		DasDesc_vSetStr((DasDesc*)pPldOut, "operation", "%s, DFT", sPriorOps);
	else
		DasDesc_setStr((DasDesc*)pPldOut, "operation", "DFT");
}

PlaneDesc* mkYscanPdFromYPd(
	DasIO* pIoOut, StreamDesc* pSdOut, PlaneDesc* pPldX, PlaneDesc* pPldIn
){
	das_units yUnits = PlaneDesc_getUnits(pPldX);
	yUnits = Units_invert(Units_interval(yUnits));

	DasEncoding* pZType = DasEnc_copy( PlaneDesc_getValEncoder(pPldIn) );

	das_units zUnits = PlaneDesc_getUnits(pPldIn);
	zUnits = Units_power(zUnits, 2);
	zUnits = Units_multiply(zUnits, Units_power(yUnits, -1));


	/* The problem here, is I don't know the yTagInterval until I see data.
	 * So just set yTagInterval = 1.0 until the data buffer is full.
	 * Basically the yTags are just the data indexes for now.
	 *
	 * Also, due to trim directives we can't set the number of frequencies
	 * until we see the interval either!
	 */
	PlaneDesc* pPldOut = new_PlaneDesc_yscan_series(
		PlaneDesc_getName(pPldIn), pZType, zUnits,
		(g_uDftLen/2) + 1, 1.0, DAS_FILL_VALUE, DAS_FILL_VALUE, yUnits
	);

	/* Copy over all the old properties */
	DasDesc_copyIn((DasDesc*)pPldOut, (DasDesc*)pPldIn);

	char sLbl[128] = {'\0'};
	if(Units_toLabel(yUnits, sLbl, 127))
		DasDesc_vSetStr((DasDesc*)pPldOut, "yLabel", "Frequency (%s)", sLbl);

	if(Units_toLabel(zUnits, sLbl, 127))
		DasDesc_vSetStr((DasDesc*)pPldOut, "zLabel", "Spectral Density (%s)",
		                 sLbl);

	_setSource(pPldOut, PlaneDesc_getName(pPldIn));

	/* Code onXTransformPktData depends on the line below */
	PlaneDesc_setFill(pPldOut, PlaneDesc_getFill(pPldIn));

	DasDesc_remove((DasDesc*)pPldOut, "DFT_freqTagMin");
	DasDesc_remove((DasDesc*)pPldOut, "DFT_freqTrimMin");
	DasDesc_remove((DasDesc*)pPldOut, "DFT_freqTrimMax");

	AuxInfo* pAux = new_AuxInfo(g_uDftLen);
	pAux->pAccum = new_Accum(g_uDftLen);

	pPldOut->pUser = (void*)pAux;

	return pPldOut;
}

/*****************************************************************************/
/* Helper for mkYScanPdFromYscanPd, get's interval from tags */

double _getIntervalFromYTags(PlaneDesc* pPldIn, char* sMsg, size_t nLen){

	/* Need to allow for some variance here first let's take the avg interval */
	double yTagInterval = 0.0, yTagAvgInterval = 0.0;

	/* Guaranteed to be at least 16 pts */
	const double* pYIn = PlaneDesc_getYTags(pPldIn);
	for(int i = 1; i < PlaneDesc_getNItems(pPldIn); ++i)
		yTagAvgInterval += (pYIn[i] - pYIn[i-1]);

	yTagAvgInterval /= (PlaneDesc_getNItems(pPldIn) - 1);


	/* Get number of intervals that are off by more than 1% of the avg */
	int nOff = 0;
	for(int i = 1; i < PlaneDesc_getNItems(pPldIn); ++i){
		yTagInterval = pYIn[i] - pYIn[i-1];
		if( fabs( (yTagInterval / yTagAvgInterval) - 1.0) > 0.01) ++nOff;
	}

	if( nOff > 2){
		snprintf(sMsg, nLen,
			"More than 2 YTag interval are more than 1%% off the average value of"
			"%.5e, Dropping plane '%s' from packet type %02d.",
			yTagAvgInterval, PlaneDesc_getName(pPldIn),
			PktDesc_getId((PktDesc*)DasDesc_parent((DasDesc*)pPldIn))
		);
		return DAS_FILL_VALUE;
	}

	return yTagAvgInterval;
}

/*****************************************************************************/
/* Helper for onPktHdr, invert a Yscan plane, yUnits are inverted            */

PlaneDesc* mkYscanPdFromYscanPd(
	DasIO* pIoOut, StreamDesc* pSdOut, PlaneDesc* pPldIn
){
	char sMsg[256] = {'\0'};

	/* If the yscan has too few items, I can't transform it */
	if(PlaneDesc_getNItems(pPldIn) < g_uDftLen){
		snprintf(sMsg, 255,
			"Input das2 stream only has %zu items in plane '%s' of "
			"packet type %02d but %zu point DFT's were requested. Dropping "
			"plane from the output\n",  PlaneDesc_getNItems(pPldIn),
			PlaneDesc_getName(pPldIn),
			PktDesc_getId((PktDesc*)DasDesc_parent((DasDesc*)pPldIn)),
			g_uDftLen
		);

		if(g_bSkip) das_send_msg(2, PROG_NAME, "%s", sMsg);
		else{
			DasIO_srverr(pIoOut, pSdOut, sMsg);
		}
		return NULL;
	}

	double ySampleInterval = 1.0;
	switch(PlaneDesc_getYTagSpec(pPldIn)){

	/* If the upstream packet desc. uses yTags, find out if they are constant */
	case ytags_list:

		ySampleInterval = _getIntervalFromYTags(pPldIn, sMsg, 127);
		if(ySampleInterval == DAS_FILL_VALUE){
			if(g_bSkip){
				das_send_msg(2, PROG_NAME, "%s", sMsg);
			}
			else{
				DasIO_srverr(pIoOut, pSdOut, sMsg);
			}
			return NULL;
		}
		break;

	case ytags_series:
		PlaneDesc_getYTagSeries(pPldIn, &ySampleInterval, NULL, NULL);
		break;

	default:
		das_error(P_ERR, "Assertion failed, das2_psd has a bug.");
		exit(P_ERR);
	}

	double yTagInterval = 1.0 / (ySampleInterval * g_uDftLen);

	AuxInfo* pAux = new_AuxInfo(g_uDftLen);

	/* To scale an fft you divide by the Δf:
	 *
	 *   Δf = fs / N  =  1/(Δt N)
	 *
	 * Now it has to be scaled to hertz to be immediately recognizable by most
	 * scientist. For example if we are measuring Δt in μs then apply the
	 * conversion to seconds:
	 *
	 *   1 μs = 1e-6 s so:
	 *
	 * Δf = 1/(Δt N) = 1 / (1e-6 Δt N) = 1e6 / (Δt N)
	 *
	 * So the final scaling factor is:
	 *
	 *     N Δt Ss
	 *
	 * where S is the scaling from original units to seconds.  But since this is
	 * just the inverse of the scaling to Hertz use:
	 *
	 *     N Δt / Shz
	 */

	das_units yOrigUnits = PlaneDesc_getYTagUnits(pPldIn);
	das_units yUnits = Units_invert(yOrigUnits);

	das_datum_fromDbl(&(pAux->dmTau), ySampleInterval,yOrigUnits);

	if(Units_canConvert(yUnits, UNIT_HERTZ)){
		pAux->rYOutScale = Units_convertTo(UNIT_HERTZ, 1.0, yUnits);
		yUnits = UNIT_HERTZ;
		pAux->rZOutScale = (g_uDftLen * ySampleInterval) / pAux->rYOutScale;
		/* Now change the interval */
		yTagInterval *= pAux->rYOutScale;
	}
	else{
		pAux->rZOutScale = g_uDftLen * ySampleInterval;
	}

	DasEncoding* pZType = DasEnc_copy( PlaneDesc_getValEncoder(pPldIn) );

	das_units zUnits = PlaneDesc_getUnits(pPldIn);
	zUnits = Units_power(zUnits, 2);
	zUnits = Units_multiply(zUnits, Units_power(yUnits, -1));

	/* Waveforms can include extra handling instructions for shifting and
	 * trimming frequency values, based on the previously calculated
	 * interval figure out how many output values we're going to end
	 * up with and were they start */
	double yTagMin = NAN;
	int iDftMin = 0, nItems = -1;

	_getOutFreqDef(pPldIn, yTagInterval, yUnits, &yTagMin, &iDftMin, &nItems);
	if(nItems < 1) return NULL;
	pAux->iMinDftOut = iDftMin; pAux->iMaxDftOut = iDftMin + nItems;

	PlaneDesc* pPldOut = new_PlaneDesc_yscan_series(
		PlaneDesc_getName(pPldIn), pZType, zUnits,
		nItems, yTagInterval, yTagMin, DAS_FILL_VALUE, yUnits
	);
	pPldOut->pUser = (void*)pAux;

	/* Copy over almost all the old properties */
	DasDesc_copyIn((DasDesc*)pPldOut, (DasDesc*)pPldIn);
	DasDesc_remove((DasDesc*)pPldOut, "DFT_freqTagMin");
	DasDesc_remove((DasDesc*)pPldOut, "DFT_freqTrimMin");
	DasDesc_remove((DasDesc*)pPldOut, "DFT_freqTrimMax");

	char sLbl[128] = {'\0'};
	if(Units_toLabel(yUnits, sLbl, 127))
		DasDesc_vSetStr((DasDesc*)pPldOut, "yLabel", "Frequency (%s)", sLbl);

	if(Units_toLabel(zUnits, sLbl, 127))
		DasDesc_vSetStr((DasDesc*)pPldOut, "zLabel", "Spectral Density (%s)",
		                 sLbl);

	_setSource(pPldOut, PlaneDesc_getName(pPldIn));

	PlaneDesc_setFill(pPldOut, PlaneDesc_getFill(pPldIn));

	return pPldOut;
}

/*****************************************************************************/
/* Packet Header Processing */

DasErrCode onPktHdr(StreamDesc* pSdIn, PktDesc* pPdIn, void* vpIoOut)
{
	DasIO* pIoOut = (DasIO*)vpIoOut;

	int nPktId = PktDesc_getId(pPdIn);
	PlaneDesc* pXOut = NULL;

	/* Auto-determine the accumulation method for this packet type, save
	   the transform type in the input packet header's user data pointer */
	size_t uTransAxis = NO_TRANSFORM;

	if(PktDesc_getNPlanesOfType(pPdIn, YScan) > 0){
		uTransAxis = TRANSFORM_IN_Y;
	}
	else{
		if(PktDesc_getNPlanesOfType(pPdIn, Y) > 0){
			uTransAxis = TRANSFORM_IN_X;
		}
		else{
			DasIO_srverr(pIoOut, g_pSdOut, "Skipping over pure X boundary data "
			             "(i.e. <x><x> packets) has not been implemented.");
		}
	}

	PktDesc* pPdOut = new_PktDesc();  /* Make the new packet descriptor */

	/* Handle the X plane up before the loop to make sure it's the first plane
	   in the output packet */
	PlaneDesc* pXIn = PktDesc_getXPlane(pPdIn);
	pXOut = PlaneDesc_copy(pXIn);
	pXIn->pUser = (void*)pXOut;  /* Provisionally store ptr to new X plane */
	AuxInfo* pAux = new_AuxInfo(g_uDftLen);
	pAux->pAccum = new_Accum(g_uDftLen);
	pXOut->pUser = pAux;
	PktDesc_addPlane(pPdOut, pXOut);

	PlaneDesc* pPlaneIn = NULL;
	PlaneDesc* pPlaneOut = NULL;
	int i;
	for(i = 0; i < PktDesc_getNPlanes(pPdIn); i++){
		pPlaneIn = PktDesc_getPlane(pPdIn, i);
		pPlaneOut = NULL;

		switch(PlaneDesc_getType(pPlaneIn)){
		case X:
			/* <x><x>... packets are a problem for now */
			if(PktDesc_getNPlanesOfType(pPdOut, X) > 1)
				return das_send_srverr(2,"Multiple X-planes are not supported at this time");
			break;

		case Y:
			/* Y's embedded with <yscan> planes are just copied */
			if(uTransAxis == TRANSFORM_IN_Y)
				pPlaneOut = PlaneDesc_copy(pPlaneIn);
			else
				pPlaneOut = mkYscanPdFromYPd(pIoOut, g_pSdOut, pXIn, pPlaneIn);
			break;

		case YScan:
			pPlaneOut = mkYscanPdFromYscanPd(pIoOut, g_pSdOut, pPlaneIn);
			break;

		case Z:
			return das_send_srverr(2, "Fourier transforming X-Y-Z scatter data "
					      "would require 2-D rebinning, which is not implemented.");
		default:
			fprintf(stderr, "ERROR: WTF?");
			return P_ERR;
		}

		if(pPlaneOut != NULL){
			/* Provisionally attach output plane descriptor to input plane descriptor */
			pPlaneIn->pUser = (void*)pPlaneOut;
			PktDesc_addPlane(pPdOut, pPlaneOut);
		}
	}

	/* If the resulting packet descriptor is only left with an X plane, or if
	 * this is a Y Transform and only yscans are left, drop it */
	if((PktDesc_getNPlanes(pPdOut) < 2) ||
		((uTransAxis == TRANSFORM_IN_Y) && (PktDesc_getNPlanesOfType(pPdOut, YScan) < 1))
	  ){
		if(g_bSkip){
			das_send_msg(2, PROG_NAME, "No transformable planes in packet ID %d, "
			             "dropping packets with id %d", nPktId, nPktId);
			for(int i = 0; i < PktDesc_getNPlanes(pPdOut); i++){
				pPlaneOut = PktDesc_getPlane(pPdOut, i);
				if(pPlaneOut->pUser != NULL) del_AuxInfo(pPlaneOut->pUser);
			}
			del_PktDesc(pPdOut);  pPdOut = NULL;
			return DAS_OKAY;
		}
		else{
			das_send_srverr(2, "No transformable planes in packet ID %d, ending "
                         "stream by user request", nPktId);
			return P_ERR;
		}
	}

	/* Save the transform type in the output packet descriptor user data area*/
	pPdOut->pUser = (void*)uTransAxis;

	/* Packet ID collapse */
   /* If the DFT length is shorter than the common packet length this code has
    * the effect of collapsing the number of output packet definitions needed
    * since yscan nitems is not all over the place, but fixed at g_nDftLen. */
	PktDesc* pPdExisting = NULL;

   if( (pPdExisting = hasMatchingPktDef(pIoOut, g_pSdOut, pPdOut)) == NULL){
		/* Already saved the output packet descriptor in the user data pointer of
		 * the input packet descriptor */
		pPdIn->pUser = (void*)pPdOut;

		if( StreamDesc_addPktDesc(g_pSdOut, pPdOut, g_nNextPktId) == DAS_OKAY ){
			++g_nNextPktId;

			if(uTransAxis == TRANSFORM_IN_Y)
				return DasIO_writePktDesc(pIoOut, pPdOut);
		}
		else{
			return P_ERR;
		}
	}
	else{
		/* Nice work, but already have one of these, make sure the user data
		 * pointers of the new input packet point the old output definitions,
		 * This could wreck havoc with interleaved <x><y><y> planes, but we'll
		 * cross that bridge when we come to it. */
		pPdIn->pUser = (void*)pPdExisting;
		int iPlane = 0;
		for(i = 0; i < PktDesc_getNPlanes(pPdIn); ++i){
			pPlaneIn = PktDesc_getPlane(pPdIn, i);

			pPlaneOut = (PlaneDesc*)pPlaneIn->pUser;
			iPlane = PktDesc_getPlaneIdx(pPdOut, pPlaneOut);
			if(iPlane == -1){
				DasIO_srverr(pIoOut, g_pSdOut, "Assertion error in das2_psd");
			}
			pPlaneIn->pUser = (void*) PktDesc_getPlane(pPdExisting, iPlane);
		}

		/* Remove the packet definition we just made */
		for(i = 0; i < PktDesc_getNPlanes(pPdOut); ++i){
			pPlaneOut = PktDesc_getPlane(pPdOut, i);
			if(pPlaneOut->pUser != NULL) del_AuxInfo(pPlaneOut->pUser);
		}
		del_PktDesc(pPdOut);
	}

	return DAS_OKAY;
}

/*****************************************************************************/
/* Consistency Check */

/* If two yscans in the same packet have different sample rates then they
 * have to be put in their own packets.  This is because the output yscans
 * have an X time that is shifted based off the number of points in the DFT
 * and the sample rate and you can't have two different X times for the
 * same packet.
 *
 * Returns a pointer to a datum defining the time between samples, of null if
 * there is not constant sampling interval in all the packets
 *
 * Proper handling would be to split into multiple packets when this
 * condition occurs, for now exit with an error
 */

const das_datum* getOrigSampInterval(
	DasIO* pIoOut, StreamDesc* pSdOut, PktDesc* pPdOut
){
	int i = 0;
	double rInt = 0.0, rIntCur = 0.0;
	PlaneDesc* pPlane = NULL;
	const das_datum* pDeltaX = NULL;

	for(i = 0; i < PktDesc_getNPlanes(pPdOut); ++i){
		pPlane = PktDesc_getPlane(pPdOut, i);
		if(PlaneDesc_getType(pPlane) == YScan){
			if(rInt == 0.0){
				PlaneDesc_getYTagSeries(pPlane, &rInt, NULL, NULL);
			}
			else{
				PlaneDesc_getYTagSeries(pPlane, &rIntCur, NULL, NULL);
				if(rIntCur != rInt){
					DasIO_srverr(pIoOut, pSdOut, "Inconsistent yTag intervals in "
					"two yscan planes of the same packet");
				}
				break;
			}
			pDeltaX = & (((AuxInfo*)pPlane->pUser)->dmTau );
		}
	}
	return pDeltaX;
}

/* ************************************************************************** */
/* Packet Data Processing, X transformations
 *
 * For <x><y><y> scans look for the cadence of the signal to be consistent
 * before storing point for the DFT.
 *
 * In order to do this we define a three point algorithm called a jitter check.
 * If three points pass the jitter check the 1st point is buffered for later
 * use in a DFT.  If at any time a jitter check fails, the *entire* buffer
 * along with the first point are dumped.
 *
 * Once DFT-LEN points pass the jitter check, the tau value is calculated.
 * If it matches the tau value from a previously sent packet, then the packet
 * ID is reused and an <x><yscan> is issued.  If it matches no previous packet
 * then a new packet ID is acquired and the data are emitted.
 */

void _shiftDownXandYScans(int iStart, PktDesc* pPd){

	PlaneDesc* pX  = PktDesc_getXPlane(pPd);
	Accum* pXAccm = ((AuxInfo*)(pX->pUser))->pAccum;
	Accum* pYAccm = NULL;

	/* Reset the beginning time for this packet */
	double rBeg = PlaneDesc_getValue(pX, 0) + pXAccm->pData[iStart];
	PlaneDesc_setValue(pX, 0, rBeg);

	/* Shift down the interval data */
	int i = 0;
	for(i = iStart; i<g_uDftLen; ++i)
		pXAccm->pData[i - iStart] = pXAccm->pData[i];


	/* For each YScan, shift down the data value */
	size_t uPlane, uSz = PktDesc_getNPlanesOfType(pPd, YScan);
	PlaneDesc* pPlane = NULL;
	for(uPlane = 0; uPlane < uSz; ++uPlane){
		pPlane = PktDesc_getPlane(pPd, PktDesc_getPlaneIdxByType(pPd, YScan, uPlane) );

		pYAccm = ((AuxInfo*)(pPlane->pUser))->pAccum;
		for(i = iStart; i<g_uDftLen; i++)
			pYAccm->pData[i - iStart] = pYAccm->pData[i];
	}
	pXAccm->iNext = g_uDftLen - iStart;
}

/* Select the final output header to use.  Since X multi Y data has mode
 * changes flattened.  A single <x><y><y> input packet type can map to multiple
 * output packet types in the X direction
 *
 * Also assumes X axis accumulator has all needed points. */

DasErrCode _finalizeXTransformHdrs(PktDesc* pPdIn, PktDesc* pPdOut, DasIO* pIoOut)
{

	PlaneDesc* pXIn  = PktDesc_getXPlane(pPdIn);
	PlaneDesc* pXOut = PktDesc_getXPlane(pPdOut);
	AuxInfo* pXAux   = (AuxInfo*)pXOut->pUser;
	AuxInfo* pYAux   = NULL;
	Accum* pXAccum = pXAux->pAccum;

	double tau = (pXAccum->pData[pXAccum->iNext - 1] - pXAccum->pData[0]) /
	              pXAccum->iNext;

	das_datum_fromDbl(&(pXAux->dmTau), tau, Units_interval(PlaneDesc_getUnits(pXIn)));

	double yTagInterval = 1.0 /(tau * g_uDftLen);

	das_units yTagUnits = Units_invert(pXAux->dmTau.units);
	if(Units_canConvert(yTagUnits, UNIT_HERTZ)){
		pXAux->rYOutScale = Units_convertTo(UNIT_HERTZ, 1.0, yTagUnits);
		yTagUnits = UNIT_HERTZ;
		pXAux->rZOutScale = (g_uDftLen * tau) / pXAux->rYOutScale;

		/* Now change the interval */
		yTagInterval *= pXAux->rYOutScale;
	}
	else{
		pXAux->rZOutScale = g_uDftLen * tau;
	}
	
	PlaneDesc* pYIn = NULL;
	PlaneDesc* pYScan = NULL;
	double yTagMin = NAN;
	int iDftMin = 0, nItems = -1;
	
	for(size_t uPlane = 0; uPlane < PktDesc_getNPlanes(pPdIn); ++uPlane){
		pYScan = PktDesc_getPlane(pPdOut, uPlane);
		if(PlaneDesc_getType(pYScan) != YScan) continue;
		
		pYIn = PktDesc_getPlane(pPdIn, uPlane);
		pYAux = (AuxInfo*)pYScan->pUser;
		
		/* Waveforms can include extra handling instruction for shifting and trimming
		 * frequency values.  Each plane can have it's own instructions for this!
		 * Thus 4 simultaneous digitizers might output different ytags due to handling 
		 * instructions.
		 */
		_getOutFreqDef(pYIn, yTagInterval, yTagUnits, &yTagMin, &iDftMin, &nItems);
		if(nItems < 1){
			/* The frequency shifting basically ignored all these data points.  I
			   guess we don't output this plane, set it to blocked. */
			daslog_error_v(
				"All output dropped from input <y> plane %s due to frequency trim "
				"directives DFT_freqTrimMin and/or DFT_freqTrimMax.",
				PlaneDesc_getName(pYIn)
			);
			pYAux->iMinDftOut = -1; pYAux->iMaxDftOut = -1;
			PlaneDesc_setYTagSeries(pYScan, yTagInterval, yTagMin, DAS_FILL_VALUE);
			PlaneDesc_setYTagUnits(pYScan, yTagUnits);
			PlaneDesc_setNItems(pYScan, 1);  /* output only one item, always fill */
		}
		else{
			pYAux->iMinDftOut = iDftMin; pYAux->iMaxDftOut = iDftMin + nItems;
			PlaneDesc_setYTagSeries(pYScan, yTagInterval, yTagMin, DAS_FILL_VALUE);
			PlaneDesc_setYTagUnits(pYScan, yTagUnits);
			PlaneDesc_setNItems(pYScan, nItems);
		}
	}
	
	return DasIO_writePktDesc(pIoOut, pPdOut);
}


DasErrCode onXTransformPktData(PktDesc* pPdIn, PktDesc* pPdOut, DasIO* pIoOut)
{
	/* pre-pre check.  If all the <y> planes have fill, pretend we didn't
	 * get a point at all. */
	DasErrCode nRet = DAS_OKAY;
	size_t uSz = PktDesc_getNPlanesOfType(pPdIn, Y);
	int iPlane = 0;
	PlaneDesc* pPlaneIn = NULL;
	double rVal = 0.0;
	char sBuf[64];

	size_t uFill = 0;
	for(iPlane = 0; iPlane < uSz; ++iPlane){
		pPlaneIn = PktDesc_getPlaneByType(pPdIn, Y, iPlane);
		rVal = PlaneDesc_getValue(pPlaneIn, 0);
		if(PlaneDesc_isFill(pPlaneIn, rVal)) uFill += 1;
	}
	if(uFill == uSz)  /* if all planes are fill, ignore this packet */
		return DAS_OKAY;

	/* Save pre-commit points to prime the pump, only needed for first two
	 * packets of this type */
	PlaneDesc* pXOut = PktDesc_getXPlane(pPdOut);
	AuxInfo* pXAux   = (AuxInfo*)pXOut->pUser;
	Accum* pXAccum = pXAux->pAccum;
	PlaneDesc* pPlaneOut = NULL;
	Accum* pAccum = NULL;
	size_t uPlane;

	if(pXAccum->nPre < 2){
		uSz = PktDesc_getNPlanes(pPdIn);
		for(uPlane = 0; uPlane < uSz; ++uPlane){
			pPlaneIn = PktDesc_getPlane(pPdIn, uPlane);
			if( (pPlaneOut = (PlaneDesc*)(pPlaneIn->pUser)) == NULL) continue;

			pAccum = ((AuxInfo*)(pPlaneOut->pUser))->pAccum;
			pAccum->aPre[pAccum->nPre] = PlaneDesc_getValue(pPlaneIn, 0);
			pAccum->nPre = pAccum->nPre + 1;
		}
		return DAS_OKAY;
	}

	/* Pump is primed, check jitter commit point or dump buffer */
	das_datum datum;
	double rJitter = 0, t0 = 0, t1 = 0, t2 = 0;
	PlaneDesc* pXIn = PktDesc_getXPlane(pPdIn);

	if((pXAccum->iNext + pXAccum->nPre) < g_uDftLen){

		/* Check jitter using only the X plane, save first point of all
		 * planes if passes */
		t0 = pXAccum->aPre[0];
		t1 = pXAccum->aPre[1];
		t2 = PlaneDesc_getValue(pXIn, 0);  /* Get our FNG */

		/* The following is a reduction of |Δt₁ - Δt₀| / avg(Δt₁ , Δt₀) */
		rJitter = 2 * fabs( (t2 - 2*t1 + t0) / (t2 - t0) );

		uSz = PktDesc_getNPlanes(pPdIn);
		for(uPlane = 0; uPlane < uSz; ++uPlane){
			pPlaneIn = PktDesc_getPlane(pPdIn, uPlane);
			if( (pPlaneOut = (PlaneDesc*)(pPlaneIn->pUser)) == NULL) continue;
			pAccum = ((AuxInfo*)(pPlaneOut->pUser))->pAccum;
			rVal = PlaneDesc_getValue(pPlaneIn, 0);

			if(rJitter < g_rEpsilon){
				/* Passed jitter check, commit 1 point */
				pAccum->pData[pAccum->iNext] = pAccum->aPre[0];
				pAccum->aPre[0] = pAccum->aPre[1];
				pAccum->aPre[1] = rVal;
				pAccum->iNext += 1;

				/* Maybe commit all our points... */
				if((pXAccum->iNext + pXAccum->nPre) == g_uDftLen){
					pAccum->pData[pAccum->iNext] = pAccum->aPre[0];
					pAccum->iNext += 1;
					pAccum->pData[pAccum->iNext] = pAccum->aPre[1];
					pAccum->iNext += 1;
					pAccum->nPre = 0;
				}
			}
			else{
				/* Failed jitter check, dump accum and first commit point */
				pAccum->aPre[0] = pAccum->aPre[1];
				pAccum->aPre[1] = rVal;
				pAccum->iNext = 0;  /* Ignore all data received so far */
				
				daslog_info_v("Jitter check failure at %s", 
					das_datum_toStr(PlaneDesc_getDatum(pXIn, 0, &datum), sBuf, 63, 6)
				);
			}
		}
	}

	if(pAccum->iNext < g_uDftLen)
		return DAS_OKAY;  /* If not enough points, just return */

	/* Alright, we have usable data, now let's make some power spectral density */
	if(g_pPsdCalc == NULL){
		g_pDftPlan = new_DftPlan(g_uDftLen, true);
		g_pPsdCalc = new_Psd(g_pDftPlan, true, "hann");
	}

	/* If this packet's header has not been transmitted, finalize and transmit
	 * since we *finally* know the sample time interval and thus the frequencies
	 *
	 * This code is different from YScan -> YScan code since ALL the <y> planes
	 * have the exact same timing information, so the pXAux carries all the
	 * needed extra frequency info such as scaling, etc.
	 */
	if(!pPdOut->bSentHdr){
		/* Sets dmTau X output plane */
		if((nRet = _finalizeXTransformHdrs(pPdIn, pPdOut, pIoOut)) != DAS_OKAY)
			return nRet;  
	}

	/* Set X value to halfway across the transformed data  */
	double tau = das_datum_toDbl(&(pXAux->dmTau));
	rVal = PlaneDesc_getValue(pXIn, 0) - (g_uDftLen/2.0) * tau;
	PlaneDesc_setValue(pXOut, 0, rVal);

	/* Calculate PSD (or set to fill) for each Yscan and shift the accumulators */
	pXAccum->iNext = 0;
	int i, j;
	bool bFill = false;

	const double* pData = NULL;
	size_t uDftLen = 0;
	AuxInfo* pYAux = NULL;
	Accum* pYAccum = NULL;
	uSz = PktDesc_getNPlanesOfType(pPdOut, YScan);
	
	for(uPlane = 0; uPlane < uSz; ++uPlane){
		iPlane = PktDesc_getPlaneIdxByType(pPdOut, YScan, uPlane);
		
		pPlaneOut = PktDesc_getPlane(pPdOut, iPlane);
		pYAux = (AuxInfo*)pPlaneOut->pUser;
		
		if((pYAux->iMinDftOut == -1)||(pYAux->iMaxDftOut == -1)){
			/* See Note in: _finalizeXTransformHdrs  If these frequencies have 
			   been completely trimmed away, just output a single fill value */
			PlaneDesc_setValue(pPlaneOut, 0, PlaneDesc_getFill(pPlaneOut));
		}
		else{
			pYAccum = pYAux->pAccum;
			
			for(i = 0; i < pYAccum->iNext; ++i){               /* Fill check */
				if PlaneDesc_isFill(pPlaneOut, pAccum->pData[i]){  
					bFill = true; break;
				}
	 		}
			
			if(bFill){
				/* If any input value is fill, the whole spectrum is fill */
				for(j = 0, i = pYAux->iMinDftOut; i < pYAux->iMaxDftOut; ++i, ++j)
					PlaneDesc_setValue(pPlaneOut, j, pPlaneOut->rFill);
			}
			else{
	
				Psd_calculate(g_pPsdCalc, pYAccum->pData, NULL);
				pData = Psd_get(g_pPsdCalc, &uDftLen);
				
				/* might can change this to a memset? */
				for(j = 0, i = pYAux->iMinDftOut; i < pYAux->iMaxDftOut; ++i, ++j)
					PlaneDesc_setValue(pPlaneOut, j, pData[i]);
			}

			pYAccum->iNext = 0;
		}
	}

	/* Write the packet */
	return DasIO_writePktData(pIoOut, pPdOut);
}

/*  ************************************************************************* */
/* Packet Data Processing, Y transformations
 * For <x><yscan><yscan> there is nothing to accumulate and you may get
 * multiple outputs for as single input.
 *
 * Note that all the stream shape checking in onPktHdr() made sure that we have
 * at least 1 YScan than is long enough to transform.  Though we still have to
 * check for fill in each output packet.
 */

bool _validYScanInputInRng(PlaneDesc* pYScan, size_t uReadPt, size_t uLen)
{
	if(PlaneDesc_getNItems(pYScan) < uReadPt + uLen) return false;

	double rVal;
	for(size_t u = uReadPt; u < uReadPt + uLen; ++u){
		rVal = PlaneDesc_getValue(pYScan, u);
		if( PlaneDesc_isFill(pYScan, rVal)) return false;
	}
	return true;
}

bool _anyYscanInputInRng(PktDesc* pPdIn, size_t uReadPt, size_t uLen)
{
	size_t uPlane;
	PlaneDesc* pPlaneIn;
	PlaneDesc* pPlaneOut;
	for(uPlane = 0; uPlane < PktDesc_getNPlanes(pPdIn); ++uPlane){
		pPlaneIn = PktDesc_getPlane(pPdIn, uPlane);

		if(PlaneDesc_getType(pPlaneIn) != YScan)
			continue;  /* Not a Y scan */

		if( (pPlaneOut = (PlaneDesc*)(pPlaneIn->pUser)) == NULL)
			continue;  /* Has no defined output */

		if(! _validYScanInputInRng(pPlaneIn, uReadPt, uLen))
			continue;  /* Not all data valid in input range */

		return true;
	}
	return false;
}

DasErrCode onYTransformPktData(PktDesc* pPdIn, PktDesc* pPdOut, DasIO* pIoOut)
{
	PlaneDesc* pPlaneIn = NULL;
	PlaneDesc* pPlaneOut = NULL;

	int nYScans = 0;
	size_t uPlane, uItems, uMaxItems = 0;
	for(uPlane = 0; uPlane < PktDesc_getNPlanes(pPdIn); ++uPlane){
		if(PktDesc_getPlaneType(pPdIn, uPlane) == YScan){
			++nYScans;  /* will be used at the end of the big loop below for a
							 * fill or skip decision */
			pPlaneIn = PktDesc_getPlane(pPdIn, uPlane);
			if( (uItems = PlaneDesc_getNItems(pPlaneIn)) > uMaxItems)
				uMaxItems = uItems;
		}
	}

	/* Function below insures all yscans in this packet have the same interval */
	StreamDesc* pSdOut = (StreamDesc*)DasDesc_parent((DasDesc*)pPdOut);
	const das_datum* pTau = getOrigSampInterval(pIoOut, pSdOut, pPdOut);


	/* Data processing loop */
	size_t u, w, uReadPt = 0;
	double rDeltaT = 1.0, rVal = 0.0;
	double rZscale = 1.0;
	const double* pInData = NULL;
	const double* pOutData = NULL;
	size_t uPsdLen = 0;
	DasErrCode nRet = DAS_OKAY;
	das_units utXInter = NULL;
	AuxInfo* pAux = NULL;
	bool bSkip = false;
	double rFill = NAN;

	while(uReadPt < uMaxItems){

		if( ! _anyYscanInputInRng(pPdIn, uReadPt, g_uDftLen)){
			uReadPt += g_uDftLen/g_uSlideDenom;
			continue;  /* No useful output in range */
		}

		bSkip = false;
		for(uPlane = 0; uPlane < PktDesc_getNPlanes(pPdIn); ++uPlane){

			pPlaneIn = PktDesc_getPlane(pPdIn, uPlane);
			if( (pPlaneOut = (PlaneDesc*)(pPlaneIn->pUser)) == NULL) continue;

			/* X planes: Fold Yscan offsets into X tag time */
			if(PlaneDesc_getType(pPlaneOut) == X){
				rVal = PlaneDesc_getValue(pPlaneIn, 0);
				utXInter = Units_interval(PlaneDesc_getUnits(pPlaneOut));
				rDeltaT = Units_convertTo(utXInter, das_datum_toDbl(pTau), pTau->units);
				rVal +=  (uReadPt + g_uDftLen/2) * rDeltaT;
				PlaneDesc_setValue(pPlaneOut, 0, rVal);
			}

			/* Y Planes: Just repeat */
			if(PlaneDesc_getType(pPlaneOut) == Y){
				rVal = PlaneDesc_getValue(pPlaneIn, 0);
				PlaneDesc_setValue(pPlaneOut, 0, rVal);
			}

			/* YScan Planes: Transform YScans (or emit fill) */
			if(PlaneDesc_getType(pPlaneOut) == YScan){

				pAux = (AuxInfo*)pPlaneOut->pUser;

				if(! _validYScanInputInRng(pPlaneIn, uReadPt, g_uDftLen)){
					rVal = PlaneDesc_getFill(pPlaneOut);
					for(u = 0; u < PlaneDesc_getNItems(pPlaneOut); ++u)
						PlaneDesc_setValue(pPlaneOut, u, rVal);
				}
				else{
					if(g_pPsdCalc == NULL){
						g_pDftPlan = new_DftPlan(g_uDftLen, true);
						g_pPsdCalc = new_Psd(g_pDftPlan, true, "hann");
					 }

					pInData = PlaneDesc_getValues(pPlaneIn);
					Psd_calculate(g_pPsdCalc, pInData+uReadPt, NULL);
					pOutData = Psd_get(g_pPsdCalc, &uPsdLen);

					uItems = PlaneDesc_getNItems(pPlaneOut);
					if((pAux->iMaxDftOut - pAux->iMinDftOut) != uItems){
						das_send_srverr(2,
							"Bug in das2_psd output packet setup, items = %zu but output"
							" PSD index range is from %d up to %d (exclusive)", uItems,
							pAux->iMinDftOut, pAux->iMinDftOut
						);
						return P_ERR;
					}
					if(uItems > uPsdLen){
						das_send_srverr(2,
							"Bug in das2_psd output packet setup, items = %zu but "
							"output PSD is only has %zu amplitudes", uItems, uPsdLen
						);
						return P_ERR;
					}
					rZscale = pAux->rZOutScale;

					/* Though rare, it's possible for there to be absolutely
                  no frequency content for all output values .  This can happen
					   if the signal is DC but the DC component has been chopped.
					   In this case just remove it from the output. */
					bSkip = true;
					for(u = pAux->iMinDftOut; u < pAux->iMaxDftOut; ++u){
						if(pOutData[u] != 0.0){ bSkip = false; break; }
					}
					if(bSkip){
						if(nYScans > 1){
							rFill = PlaneDesc_getFill(pPlaneOut);
							for(w=0, u=pAux->iMinDftOut; u < pAux->iMaxDftOut; ++w, ++u)
								PlaneDesc_setValue(pPlaneOut, w, rFill);
							bSkip = false;
						}
					}
					else{
						for(w=0, u=pAux->iMinDftOut; u < pAux->iMaxDftOut; ++w, ++u)
							PlaneDesc_setValue(pPlaneOut, w, pOutData[u] * rZscale);
					}
				}
			}

			/* Z planes: Not allowed, forbidden up front */

		}

		if(!bSkip){
			if((nRet=DasIO_writePktData(pIoOut, pPdOut)) != DAS_OKAY) return nRet;
		}
		g_nPktsOut += 1;
		uReadPt += g_uDftLen/g_uSlideDenom;
	}
	return DAS_OKAY;
}

DasErrCode onPktData(PktDesc* pPdIn, void* vpIoOut)
{
	/* Remember, certain packet IDs may not be transformable and may have
	   been dropped from the output */
	if(pPdIn->pUser == NULL) return DAS_OKAY;

	DasIO* pIoOut = (DasIO*)vpIoOut;

	PktDesc* pPdOut = (PktDesc*)pPdIn->pUser;

	switch((size_t)(pPdOut->pUser)){
	case TRANSFORM_IN_X:  return onXTransformPktData(pPdIn, pPdOut, pIoOut);
	case TRANSFORM_IN_Y:  return onYTransformPktData(pPdIn, pPdOut, pIoOut);
	default:           return das_send_srverr(2, "Bug in das2_psd, onPktData()");
	}
}

/* ************************************************************************* */
/* Stream Close Handling - if nothing could be written, send no-data except  */

DasErrCode onClose(StreamDesc* pSdIn, void* vIoOut)
{
	DasIO* pIoOut = (DasIO*)vIoOut;
	OobExcept except;
	DasErrCode nRet = DAS_OKAY;

	if(g_nPktsOut == 0){
		OobExcept_set(
			&except, DAS2_EXCEPT_NO_DATA_IN_INTERVAL, "All input data segments "
			"were too short to for the requested DFT size"
		);

		nRet = DasIO_writeException(pIoOut, &except);
		OutOfBand_clean((OutOfBand*)&except);
	}

	return nRet;
}

/* ************************************************************************* */
/* Copy out Comments, trigger halts on exceptions */

DasErrCode onException(OobExcept* pExcept, void* vIoOut)
{
	DasIO* pIoOut = (DasIO*)vIoOut;

	DasErrCode nRet = DasIO_writeException(pIoOut, pExcept);
	if(nRet != 0) return nRet;
	return P_ERR;
}

DasErrCode onComment(OobComment* pCmt, void* vIoOut)
{
	DasIO* pIoOut = (DasIO*)vIoOut;
	return DasIO_writeComment(pIoOut, pCmt);
}

/* ************************************************************************* */
void prnHelp()
{
	fprintf(stderr,
"SYNOPSIS\n"
"   das2_psd - Convert time series streams into power spectral density streams\n"
"\n");

	fprintf(stderr,
"USAGE\n"
"   das2_psd [options] LENGTH SLIDE_DENOMINATOR\n"
"\n");

	fprintf(stderr,
"DESCRIPTION\n"
"   das2_psd is a classic Unix filter, reading das2 streams on standard input\n"
"   and producing a transformed stream containing packets that are LENGTH/2 +1\n"
"   y values long on the standard output.  Note that LENGTH must be an even\n"
"   number, but need not be a power of two.\n"
"\n"
"   Input data are gathered into FFT buffers in the following manner:\n"
"\n"
"     * Values with regular time cadence are read into a buffer of size\n"
"       LENGTH.  If the cadence is broken, values are discarded and\n"
"       accumulation starts over.\n"
"\n"
"     * Once the buffer has been filled, a power spectral density calculation\n"
"       is preformed on the input values and output in a <yscan> plane\n"
"\n"
"     * Values are shifted down by LENGTH/SLIDE_DENOMINATOR points and\n"
"       filling continues\n"
"\n"
"   The following table relates SLIDE_DENOMINATOR and percentage overlap for\n"
"   DFT (Discrete Fourier Transform) calculations:\n"
"\n"
"      SLIDE_DENOM      Percent Overlap\n"
"      -----------      ---------------\n"
"           1                  0%%\n"
"           2                 50%%\n"
"           3                 66%%\n"
"           4                 75%%\n"
"           5                 80%%\n"
"          ...                ...\n"
"\n"
"   The shape of the stream changes when transformed.  Though the number of\n"
"   independent packet IDs remains the same, the number of actual data packets\n"
"   in the output stream can vary dramatically from the input stream.  Stream\n"
"   morphology changes fall into three categories:\n"
"\n"
"      Case A: X with multiple Y's\n"
"      ===============================================================\n"
"      Input )  LENGTH packets with shape: <x><y><y><y>\n"
"\n"
"      Output)  1 packet with shape:       <x><yscan><yscan><yscan>\n"
"\n"
"\n"
"      Case B:  X with multiple YScans\n"
"      ===============================================================\n"
"      Input ) One packet with shape:     <x><yscan><yscan><yscan>\n"
"\n"
"      Output) 1-N packets with shape:    <x><yscan><yscan><yscan>\n"
"\n"
"\n"
"      Case C:  X with multiple Y's and YScans\n"
"      ===============================================================\n"
"      Input ) One packet with shape:     <x><y><y><yscan><yscan>\n"
"\n"
"      Output) 1-N packets with shape:    <x><y><y><yscan><yscan>\n"
"\n"
"\n"
"   In case C (mixed line-plot and table data) above, the <y> values are\n"
"   treated as <x> values and just copied to the output stream.\n"
"\n");

	fprintf(stderr,
"OPTIONS\n"
"   -h,--help     Display this text and exit.\n"
"\n"
"   -v,--version  Display source version information and exit.\n"
"\n"
"   -c \"DATUM\",--cadence=\"DATUM\"\n"
"                 The display interpolation DATUM that makes sense for waveform\n"
"                 data is often way too small for spectrograms.  For streams\n"
"                 transformed in <x> a new one of 2x the length of the DFT is\n"
"                 emitted.  Use this parameter to override the xTagWidth that\n"
"                 would normally be transmitted.\n"
"                 Note: A space is required between unit value and the unit\n"
"                 string, so this argument will need quotes.\n"
"\n"
"   -n,--no-skip  Do not skip over input packet *types* that cannot be\n"
"                 transformed, instead exit the program with an error message.\n"
"                 Individual data packets that cannot be transformed are always\n"
"                 skipped.\n"
"\n"
"   -x,--trans-x  A series of <yscan> packets can be equally spaced in either\n"
"                 the X or the Y dimension.  By default <yscan>s are assumed to\n"
"                 be waveform packets which have a regular Y spacing but\n"
"                 irregular X spacing.  Use this option to force all transforms\n"
"                 to be over the X dimension.\n"
"\n"
"  -m \"ID,DATUM\" ,--map \"ID,DATUM\"\n"
"                 For <x><y><y> set the packet ID to use when ever a particular\n"
"                 sample time is detected.  This allows for consistent packet\n"
"                 ID assignment for datasets with variable sample rates and \n"
"                 Thus simpler reduced-resolution cache sets (see \n"
"                 das2_cache_rdr for mor info).  If this option is not selected\n"
"                 packet ID are assigned in order base on the detected sample\n"
"                 rate in the input stream.\n"
"\n"
"  -j,--max-jitter FRACTION\n"
"                 Only applies to transforms over the X direction.  For\n"
"                 <x><y><y> streams each packet contains a sample time.  Due\n"
"                 to the limits of floating point time precision the sampling\n"
"                 period may appears to change between consecutive samples.\n"
"                 By default a jitter on the sample interval of less than " g_sEplion "\n"
"                 does not trigger a break in a continuous sequence of packets.\n"
"                 Jitter is calculated on each three points via:\n"
"\n"
"                    |(τ₁ - τ₀) / avg(τ₁ , τ₀)|\n"
"\n"
"                 where  τ₀ = x₁ - x₀  and   τ₁ = x₂ - x₁  for any three <x>\n"
"                 points.\n"
"\n");
	fprintf(stderr,
"LIMITATIONS\n"
"   Transforms for input <yscan> packets are always preformed in the Y dimension\n"
"   and *never* cross packet boundaries.  Thus if the LENGTH argument is larger \n"
"   the number of items in all <yscan> packets, no output is generated.\n"
"\n"
"   Transforming N <yscan> packets in the X dimension, to N <yscan>s in the 1/X\n"
"   dimension is useful, but not yet supported.\n"
"\n");
	fprintf(stderr,
"AUTHOR\n"
"   chris-piker@uiowa.edu\n"
"\n");
	fprintf(stderr,
"SEE ALSO\n"
"   * das2_bin_avg, das2_bin_avgsec, das2_ascii\n"
"\n"
"   * The Das2 ICD at http://das2.org for a general introduction\n"
"\n");
}

/* ************************************************************************* */
void prnVersion()
{
	fprintf(stderr, "SVN ID:  $Id: das2_psd.c 11341 2019-04-04 06:46:57Z cwp $\n");
	fprintf(stderr, "SVN URL: $Url$\n");
}

/* ************************************************************************* */
bool parseArgs(
	int argc, char** argv, size_t* pDftLen, size_t* pSlideDenom,
	das_datum* pCadence
){
	int i = 0;
	size_t uTmp = 0;
	const char* sArg = NULL;
	while( i < (argc - 1)){
		++i;

		if(argv[i][0] == '-'){
			if((strcmp(argv[i], "-h") == 0)||(strcmp(argv[i], "--help") == 0)){
				prnHelp();
				exit(0);
			}
			if((strcmp(argv[i], "-v") == 0)||(strcmp(argv[i], "--version") == 0)){
				prnVersion();
				exit(0);
			}

			if((strcmp(argv[i], "-c") == 0)||(strncmp(argv[i], "--cadence=", 10) == 0)){
				if(strcmp(argv[i], "-c") == 0){
					if(i > (argc - 1)){
						das_send_queryerr(2, "Missing argument for -c");
						return false;
					}
					++i;
					sArg = argv[i];
				}
				else{
					if(strlen(argv[i]) < 11){
						das_send_queryerr(2, "Missing argument for --cadence=");
						return false;
					}
					sArg = argv[i];
					sArg += 10;
				}
				if(! das_datum_fromStr(pCadence, sArg) || (das_datum_toDbl(pCadence) <= 0.0 )){
					das_send_queryerr(2, "Couldn't convert %s to a valid X-Tag cadence", sArg);
					return false;
				}
				continue;
			}
			
			if((strcmp(argv[i], "-j") == 0)||(strncmp(argv[i], "--max-jitter=", 13) == 0)){
				if(strcmp(argv[i], "-j") == 0){
					if(i > (argc - 1)){
						das_send_queryerr(2, "Missing argument for -c");
						return false;
					}
					++i;
					sArg = argv[i];
				}
				else{
					if(strlen(argv[i]) < 14){
						das_send_queryerr(2, "Missing argument for --max-jitter=");
						return false;
					}
					sArg = argv[i];
					sArg += 13;
				}				
			}

			if((strcmp(argv[i], "-n") == 0)||(strcmp(argv[i], "--no-skip") == 0)){
				g_bSkip = false;
				continue;
			}

			if((strcmp(argv[i], "-x") == 0)||(strcmp(argv[i], "--trans-x") == 0)){
				das_send_srverr(2, "Forcing X transformations is not yet supported");
				return false;
			}

		}
		else{

			if(sscanf(argv[i], "%zu", &uTmp) != 1){
				das_send_queryerr(2,"Couldn't convert '%s' to an integer", argv[i]);
				return false;
			}

			if(*pDftLen == 0){
				if(uTmp < 16){
					das_send_queryerr(2,"%d is below the minimum DFT length of 16 "
							             "points",uTmp);
					return false;
				}
				if(uTmp % 2 != 0){
					das_send_queryerr(2,"%d must be an even number",uTmp);
					return false;
				}
				*pDftLen = uTmp;
				continue;
			}

			if(*pSlideDenom == 0){
				if(uTmp < 1){
					das_send_queryerr(2,"%d is below the minimum slide fraction "
						             "denominator of 1",uTmp);
					return false;
				}
				*pSlideDenom = uTmp;

				if(*pSlideDenom > (*pDftLen - 1)){
					das_send_queryerr(2,"The given slide denominator was %d, but the "
							            "maximum overlap is to slide by one point, i.e. "
							            "denominator = %d", *pSlideDenom, (*pDftLen)-1);
					return false;

				}
				continue;
			}
		}

		das_send_queryerr(2, "Unknown command line parameter '%s'", argv[i]);
		return false;
	}
	return true;
}

/* ************************************************************************* */
int main(int argc, char** argv) {

	/* Exit on errors, log info messages and above */
	das_init(argv[0], DASERR_DIS_EXIT, 0, DASLOG_INFO, NULL);

	if(!parseArgs(argc, argv, &g_uDftLen, &g_uSlideDenom, &g_cadence)) return 13;

	/* We know our output size, generate an FFT plan */


	/* Make output writer and a stream handler structure */
	DasIO* pIoOut = new_DasIO_cfile("das2_psd", stdout, "w");

	StreamHandler* pSh = new_StreamHandler( pIoOut );

	pSh->streamDescHandler = onStreamHdr;
	pSh->pktDescHandler = onPktHdr;
	pSh->pktDataHandler = onPktData;
	pSh->closeHandler = onClose;
	pSh->commentHandler = onComment;
	pSh->exceptionHandler = onException;

	/* Make input reader, give it the stream handler structure containing
	   per-packet call-backs */
	DasIO* pIn = new_DasIO_cfile("Standard Input", stdin, "r");
	DasIO_addProcessor(pIn, pSh);

	int nRet = DasIO_readAll(pIn);

	/* Uncomment to make valgrind happy */
	/*
	PktDesc* pPdOut = NULL;
	PlaneDesc* pXOut = NULL;

	for(int i = 1; i < 100; ++i){
		if( ! StreamDesc_isValidId(g_pSdOut, i) ) continue;

		pPdOut = StreamDesc_getPktDesc(g_pSdOut, i);
		pXOut = PktDesc_getXPlane(pPdIn);
		if(pXOut->pUser) del_Accum((Accum*)(pXOut->pUser));

		StreamDesc_freePktDesc(g_pSdOut, nPktId);
	}*/

	return nRet;
}
