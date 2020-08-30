/* Copyright (C) 2017-2020  Chris Piker <chris-piker@uiowa.edu>
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

#define _POSIX_C_SOURCE 200112L

#include <das2/core.h>
#include "psd_xoffset.h"


/* ************************************************************************* */
/* Internal Handler Object for Xoffset processing */

typedef struct xoffset_handler{
	PktHandler base;
	das_datum dmTau;  /* all xscans in same packet must have the same deltaT */
	DftInfo dft;      /* and so all xscans have the same dft scaling info    */
} XOffHndlr;




/** Ancillary tracking structure assigned to pUser for every out-going yscan
 * plane.  Used to keep track of output data scaling (if any) as well as
 * data accumulation when needed.
 */
typedef struct aux_info {
	das_datum dmTau;
	int iMinDftOut;  
	int iMaxDftOut;  
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


/* Make Output packet descriptor.
 * Make output packet descriptor given an input packet descriptor that contains
 * XScans */

PktDesc* onXOffsetPktHdr(PktDesc* pPdIn, DasIO* pOut, StreamDesc* pSdOut)
{
	
	


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
			if(uTransAxis == TRANSFORM_IN_X)
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

DasErrCode onXScanPktData(PktDesc* pPdIn, PktDesc* pPdOut, DasIO* pIoOut)
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
					Das2Psd* pCalc = psdCalc();

					pInData = PlaneDesc_getValues(pPlaneIn);
					Psd_calculate(pCalc, pInData+uReadPt, NULL);
					pOutData = Psd_get(pCalc, &uPsdLen);

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
