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

/* Handle transformations for X center points.  These typically arrive in
 * multiple packets.  Thus we have to buffer data until enough are found for
 * a single transfrom.
 *
 * Also, since each time point is independent different sampling rates are
 * flattened.  Thus one input packet type can correspond to multiple autput
 * packet types.
 */

const double g_rEpsilon = 0.001; /* Time difference maximum jitter for DFT */

typedef struct xcenter_aux_info {
	
	
};



/** Data accumulation structure assigned to pUser for each out going conversion
 * of one <x> or <y> plane in an x_multi_y packet.  These are needed because the
 * output packet buffer is too small to accumulate g_DftLen points since the
 * output buffer is g_DftLen/2 + 1.  These aren't needed for DFTs over time
 * offsets because they transform in one go. */
typedef struct accum {
	int nPre;      /* Number of collected pre-commit points */
	double aPre;   /* pre-commit buffer. Points move form here to the data
						 * buffer once they pass a jitter check */
	int iNext;     /* The index of the next point to store (also current size) */
	int nSize;     /* The size of the buffer */
	double* pData; /* Either X or Y data */
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

/* ************************************************************************** */
/* Packet Data Processing, X transformations */

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
 * output packet types.
 * 
 * Also assumes X axis accumulator has all needed points. */
PktDesc* _finalizeXTransformHdrs(PktDesc* pPdIn, DasIO* pIoOut)
{
	
	double rSampleRate = pAccum->pData[g_uDftLen - 1] - pAccum->pData[0];
	
	das_datum_fromDbl(&(pXAux->dmTau), pXAccum->pData[1] - pXAccum->pData[0],
			                Units_interval( PlaneDesc_getUnits(pXIn)) );

	tau = das_datum_toDbl(&(pXAux->dmTau));
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

	/* Waveforms can include extra handling instructions for shifting and
	* trimming frequency values.  Based on the previously calculated
	* interval, figure out how many output values we're going to end
	* up with and were they start */
	double yTagMin = NAN;
	int iDftMin, nItems = -1;
	_getOutFreqDef(pXIn, yTagInterval, yTagUnits, &yTagMin, &iDftMin,
	                       &nItems);
	pXAux->iMinDftOut = iDftMin;
	pXAux->iMaxDftOut = iDftMin + nItems;

	for(uPlane = 0; uPlane < PktDesc_getNPlanes(pPdIn); ++uPlane){
		pPlaneOut = PktDesc_getPlane(pPdOut, uPlane);
		if(PlaneDesc_getType(pPlaneOut) != YScan) continue;
		pPlaneOut->yTagInter = yTagInterval;
		PlaneDesc_setYTagUnits(pPlaneOut, yTagUnits);
		PlaneDesc_setNItems(pPlaneOut, nItems);
		PlaneDesc_setYTagSeries(pPlaneOut, yTagInterval, yTagMin, DAS_FILL_VALUE);
	}

	return DasIO_writePktDesc(pIoOut, pPdOut);
}

/* For <x><y><y> scans look for the cadence of the signal to be consistent 
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

DasErrCode onXTransformPktData(PktDesc* pPdIn, PktDesc* pPdOut, DasIO* pIoOut)
{
	/* pre-pre check.  If all the <y> planes have fill, pretend we didn't
	 * get a point at all. */
	size_t uSz = PktDesc_getNPlanesOfType(pPdIn, Y);
	int iPlane = 0;
	PlaneDesc* pPlaneIn = NULL;
	double rVal;
	
	size_t uFill = 0;
	for(iPlane = 0; iPlane < uSz; ++iPlane){
		pPlaneIn = PktDesc_getPlaneByType(pPdIn, Y, iPlane);
		rVal = PlaneDesc_getValue(pPlaneIn, 0);
		if(PlaneDesc_isFill(pPlaneIn, rVal)) uFill += 1;
	}
	if(uFill = uSz)
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
			pAccum->aPre[pXAccum->nPre] = PlaneDesc_getValue(pPlaneIn, 0);
			pAccum->nPre = pXAccum->nPre + 1;
		}
		pXAccum->nPre += 1;
		return DAS_OKAY;
	}
	
	/* Pump is primed, check jitter commit point or dump buffer */
	double rJitter = 0, t0 = 0, t1 = 0, t2 = 0;
	PlaneDesc* pXIn = PktDesc_getXPlane(pPdIn);
		
	if((pXAccum->iNext + pXAccum->nPre) < g_uDftLen){
		
		/* Check jitter using only the X plane, save first point of all
		 * planes if passes */
		t0 = pXAccum->aPre[0];
		t1 = pXAccum->aPre[1];
		t2 = PlaneDesc_getValue(pXIn, 0);  /* Get our FNG */
		
		/* The following is a reduction of |Δt₁ - Δt₀| / avg(Δt₁ , Δt₀) */
		rJitter = 2 * fabs(t2 - 2*t1 + t0) / t2 + t0;
		
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
		_finalizeXTransformHdrs(pPdIn, pPdOut, pIoOut);
	}

	/* Set X value to halfway across the transformed data  */
	tau = das_datum_toDbl(&(pXAux->dmTau));
	rVal = PlaneDesc_getValue(pXIn, 0) - (g_uDftLen/2.0) * tau;
	PlaneDesc_setValue(pXOut, 0, rVal);

	/* Calculate PSD (or set to fill) for each Yscan and shift the accumulators */
	pXAccum->iNext = 0;
	int j;
	const double* pData = NULL;
	size_t uDftLen = 0;
	uSz = PktDesc_getNPlanesOfType(pPdOut, YScan);
	for(uPlane = 0; uPlane < uSz; ++uPlane){
		iPlane = PktDesc_getPlaneIdxByType(pPdOut, YScan, uPlane);
		pPlaneOut = PktDesc_getPlane(pPdOut, iPlane);
		pYAux = (AuxInfo*)pPlaneOut->pUser;
		pYAccum = pYAux->pAccum;

		Psd_calculate(g_pPsdCalc, pYAccum->pData, NULL);
		pData = Psd_get(g_pPsdCalc, &uDftLen);

		for(j = 0, i = pXAux->iMinDftOut; i < pXAux->iMaxDftOut; ++i, ++j)
			PlaneDesc_setValue(pPlaneOut, j, pData[i]);

		pYAccum->iNext = 0;
	}
	
	/* Do the fft's on any planes that don't have fill */
	AuxInfo pAux = NULL;
	uSz = PktDesc_getNPlanes(pPdOut);
	bool bFill = false;
	int i = 0;
	for(uPlane = 0; uPlane < uSz; ++uPlane){
		pPlaneOut = PktDesc_getPlane(pPdOut, uPlane);
		if(PlaneDesc_getType(pPlaneOut) != YScan) continue;
		
		pAux = (AuxInfo*)pPlaneOut->pUser;
		pAccum = pAux->pAccum;

		/* Fill check */
		for(i = 0; i < pAccum->iNext; ++i){
			rVal = pAccum->pData[i];
			if PlaneDesc_isFill(pPlaneOut, rVal){ 
				bFill = true; break; 
			}
 		}
		if(bFill){
			/* If any input value is fill, the whole output is fill */
			for(i = 0; i < pAccum->iNext; ++i)
				PlaneDesc_setValue(pPlaneOut, i, pPlaneOut->rFill);
		}
		
	}

	/* Write the packet */
	return DasIO_writePktData(pIoOut, pPdOut);
}