#define _POSIX_C_SOURCE 200112L

#include <string.h>
#include <strings.h>
#include <math.h>

#include <fftw3.h>

#include "util.h"
#include "dft.h"

#define MAGNITUDE(r, i) sqrt( (r)*(r) + (i)*(i) )
#define SQUARE(r, i) ( (r)*(r) + (i)*(i) )

#ifndef M_PI
#define M_PI 3.1415926535897931
#endif

/* ************************************************************************* */
/* Construction/Re-Initialization/Destruction DFT object */

ErrorCode reinit_Dft(
	Das2Dft* pThis, size_t uLen, const char* sWindow
){
	if(pThis->vpPlan) fftw_destroy_plan( (fftw_plan)pThis->vpPlan);
	if(pThis->vpIn) fftw_free( pThis->vpIn);
	if(pThis->vpOut) fftw_free( pThis->vpOut);
	if(pThis->pWnd) free(pThis->pWnd);
	if(pThis->sWindow) free(pThis->sWindow);
	if(pThis->pMag) free(pThis->pMag); 
	
	pThis->uMagLen = 0;
	pThis->pMag = NULL;
	pThis->bNewMag = false;
	
	for(size_t u = 0; u<2; u++){
		if(pThis->pCmpOut[u]) free(pThis->pCmpOut[u]);
		pThis->pCmpOut[u] = NULL;
		pThis->uCmpLen[u] = 0;
		pThis->bNewCmp[u] = false;
	}
	
	if((uLen == 0) ||(uLen % 2 != 0))
		return das2_error(DAS2ERR_DFT, "Can't handle odd length DFTs or DFTs "
				            "less than 2 points long.");
	pThis->uLen = uLen;
	
	pThis->vpIn = fftw_malloc(uLen*sizeof(fftw_complex));
	pThis->vpOut = fftw_malloc(uLen*sizeof(fftw_complex));
	
	int nSign = FFTW_FORWARD;
	if(!(pThis->bForward)) nSign = FFTW_BACKWARD;
	
	pThis->vpPlan = fftw_plan_dft_1d(
		uLen, (fftw_complex*)pThis->vpIn, (fftw_complex*)pThis->vpOut,
		nSign, FFTW_MEASURE
	);
	
	pThis->bRealOnly = false;
	
	pThis->pWnd = (double*)calloc(uLen, sizeof(double));
	size_t u;
	if(sWindow == NULL){
		for(u = 0; u<uLen; u++) pThis->pWnd[u] = 1.0;
		return DAS_OKAY;
	}
	
	pThis->sWindow = (char*)calloc(strlen(sWindow) + 1, sizeof(char));
	strcpy(pThis->sWindow, sWindow);
	
	if(strncasecmp(sWindow, "HANN", 4) == 0){
		
		/* Using a constant of 2 to make the area under the curve = 1.0 */
		for(u = 0; u<uLen; u++)
			pThis->pWnd[u] = 2.0*(1.0 - cos((2.0*M_PI*u)/(uLen - 1)));
		
		return DAS_OKAY;
	}
	
	return das2_error(DAS2ERR_DFT, "Unknown window function: '%s'", sWindow);
}

Das2Dft* new_Dft(size_t uLen, const char* sWindow, bool bForward)
{	
	Das2Dft* pThis = (Das2Dft*)calloc(1, sizeof(Das2Dft));
	pThis->bForward = bForward;
	
	ErrorCode err = reinit_Dft(pThis, uLen, sWindow);
	if(err != DAS_OKAY){
		del_Dft(pThis);
		return NULL;
	}
	
	return pThis;
}

void del_Dft(Das2Dft* pThis){
	if(pThis->vpPlan) fftw_destroy_plan( (fftw_plan)pThis->vpPlan);
	if(pThis->vpIn) fftw_free( pThis->vpIn);
	if(pThis->vpOut) fftw_free( pThis->vpOut);
	if(pThis->pWnd) free(pThis->pWnd);
	if(pThis->sWindow) free(pThis->sWindow);
	if(pThis->pMag) free(pThis->pMag);
	if(pThis->pCmpOut[0]) free(pThis->pCmpOut[0]);
	if(pThis->pCmpOut[1]) free(pThis->pCmpOut[1]);
	free(pThis);
}


/* ************************************************************************* */
/* Calculate a DFT */

ErrorCode Dft_calculate(
	Das2Dft* pThis, const double* pReal, const double* pImg, size_t uLen
){
	ErrorCode err;
	if(uLen != pThis->uLen)
		if((err=reinit_Dft(pThis, uLen, pThis->sWindow)) != DAS_OKAY) return err;
	
	fftw_plan     pPlan  = (fftw_plan)   pThis->vpPlan;
	fftw_complex* pIn  = (fftw_complex*) pThis->vpIn;
	
	for(size_t u = 0; u < uLen; u++){
		pIn[u][0] = pReal[u] * pThis->pWnd[u];
		if(pImg)
			pIn[u][1] = pImg[u] * pThis->pWnd[u];
		else
			pIn[u][1] = 0.0;
	}
	
	fftw_execute(pPlan);

	pThis->bNewMag = true;
	pThis->bNewCmp[0] = true; pThis->bNewCmp[1] = true;
	pThis->bRealOnly = (pImg == NULL);

	return DAS_OKAY;
}

/* ************************************************************************* */
/* Retrieving Results */

const double* _Dft_getComp(Das2Dft* pThis, size_t* pLen, size_t uCmp)
{
	if(pThis->vpOut == NULL) 
		*pLen = 0;
	else 
		*pLen = pThis->uLen;
	
	if(!pThis->bNewCmp[uCmp]) return pThis->pCmpOut[uCmp];
	
	if(pThis->uCmpLen[uCmp] != pThis->uLen){
		if(pThis->pCmpOut[uCmp]) free(pThis->pCmpOut[uCmp]);
		pThis->pCmpOut[uCmp] = (double*)calloc(pThis->uLen, sizeof(double));
	}
	
	fftw_complex* pOut = (fftw_complex*) pThis->vpOut;
	
	for(size_t u = 0; u<pThis->uLen; u++)
		pThis->pCmpOut[uCmp][u] = pOut[u][uCmp];
	
	return pThis->pCmpOut[uCmp];
}

const double* Dft_getReal(Das2Dft* pThis, size_t* pLen)
{
	return _Dft_getComp(pThis, pLen, 0);
}

const double* Dft_getImg(Das2Dft* pThis, size_t* pLen)
{
	return _Dft_getComp(pThis, pLen, 1);
}

const double* Dft_getMagnitude(Das2Dft* pThis, size_t* pLen)
{
	if(!pThis->bNewMag){
		*pLen = pThis->uMagLen;
		return pThis->pMag;
	}
	
	fftw_complex* pOut = (fftw_complex*) pThis->vpOut;
	
	if(pThis->bRealOnly){
	
		if(pThis->uMagLen != (pThis->uLen/2 + 1)){
			pThis->uMagLen = pThis->uLen/2 + 1;
			if(pThis->pMag) free(pThis->pMag);
			pThis->pMag = (double*)calloc(pThis->uMagLen, sizeof(double));
		}
	
		size_t u, uPos, uNeg, uNyquist = pThis->uLen/2;
		
		if(pThis->bNewMag){
			pThis->pMag[0] = MAGNITUDE(pOut[0][0], pOut[0][1]) / pThis->uLen;
			pThis->pMag[uNyquist] = MAGNITUDE(pOut[uNyquist][0], pOut[uNyquist][1]) /
					                  pThis->uLen;
		
			for(u = 1; u< uNyquist; u++){
				uPos = u;
				uNeg = pThis->uLen - u;
				pThis->pMag[u] = MAGNITUDE(pOut[uPos][0], pOut[uPos][1]) + 
			                    MAGNITUDE(pOut[uNeg][0], pOut[uNeg][1]);
				pThis->pMag[u] /= pThis->uLen;
			}
		}
	}
	else{
		das2_error(DAS2ERR_NOTIMP, "Magnitude calculation for complex input not "
				     "yet implemented");
		return NULL;
	}
	
	*pLen = pThis->uMagLen;
	return pThis->pMag;
}

/* ***************************** PSD Objects ******************************* */


/* ************************************************************************* */
/* Construction/Re-Initialization/Destruction PSD object */

ErrorCode reinit_Psd(
	Das2Psd* pThis, size_t uLen, bool bCenter, const char* sWindow
){
	if(pThis->vpPlan) fftw_destroy_plan( (fftw_plan)pThis->vpPlan);
	if(pThis->vpIn) fftw_free( pThis->vpIn);
	if(pThis->vpOut) fftw_free( pThis->vpOut);
	if(pThis->pWnd) free(pThis->pWnd);
	if(pThis->sWindow) free(pThis->sWindow);
	if(pThis->pMag) free(pThis->pMag); 
	
	pThis->uMagLen = 0;
	pThis->pMag = NULL;
	
	pThis->rPwrIn = 0.0;
	pThis->rPwrOut = 0.0;
	
	if((uLen == 0) ||(uLen % 2 != 0))
		return das2_error(DAS2ERR_DFT, "Can't handle odd length DFTs or DFTs "
				            "less than 2 points long.");
	pThis->uLen = uLen;
	
	pThis->vpIn = fftw_malloc(uLen*sizeof(fftw_complex));
	pThis->vpOut = fftw_malloc(uLen*sizeof(fftw_complex));
	
	pThis->vpPlan = fftw_plan_dft_1d(
		uLen, (fftw_complex*)pThis->vpIn, (fftw_complex*)pThis->vpOut,
		FFTW_FORWARD, FFTW_MEASURE
	);
	
	pThis->bRealOnly = false;
	pThis->bCenter = bCenter;
	
	pThis->pWnd = (double*)calloc(uLen, sizeof(double));
	pThis->rWndSqSum = 0.0;
	size_t u;
	if(sWindow == NULL){
		for(u = 0; u<uLen; u++) pThis->pWnd[u] = 1.0;
		pThis->rWndSqSum = ((double)pThis->uLen) * ((double)pThis->uLen);
		pThis->sWindow = NULL;
		return DAS_OKAY;
	}
	
	pThis->sWindow = (char*)calloc(strlen(sWindow) + 1, sizeof(char));
	strcpy(pThis->sWindow, sWindow);
	
	/* Strict Hann Function Definition used here: */
	/*   http://en.wikipedia.org/wiki/Hann_function */
	/* Window Square Sum (Wss) used here: */
	/*   Numerical Recipes in C, equation 13.4.11 */
	
	if(strncasecmp(sWindow, "HANN", 4) == 0){
		
		for(u = 0; u<uLen; u++){
			pThis->pWnd[u] = 0.5*(1.0 - cos((2.0*M_PI*u)/(uLen - 1)));
			pThis->rWndSqSum += pThis->pWnd[u] * pThis->pWnd[u];
		}
		pThis->rWndSqSum *= pThis->uLen;
		return DAS_OKAY;
	}
	
	return das2_error(DAS2ERR_DFT, "Unknown window function: '%s'", sWindow);
}

Das2Psd* new_Psd(size_t uLen, bool bCenter, const char* sWindow)
{
	Das2Psd* pThis = (Das2Psd*)calloc(1, sizeof(Das2Psd));
	
	ErrorCode err = reinit_Psd(pThis, uLen, bCenter, sWindow);
	if(err != DAS_OKAY){
		del_Das2Psd(pThis);
		return NULL;
	}
	
	return pThis;
}


void del_Das2Psd(Das2Psd* pThis)
{
	if(pThis->vpPlan) fftw_destroy_plan( (fftw_plan)pThis->vpPlan);
	if(pThis->vpIn) fftw_free( pThis->vpIn);
	if(pThis->vpOut) fftw_free( pThis->vpOut);
	if(pThis->pWnd) free(pThis->pWnd);
	if(pThis->sWindow) free(pThis->sWindow);
	if(pThis->pMag) free(pThis->pMag); 
	if(pThis->pUpConvReal) free(pThis->pUpConvReal);
	if(pThis->pUpConvImg) free(pThis->pUpConvImg);
	free(pThis);
}


ErrorCode Psd_calculate_f(
	Das2Psd* pThis, const float* pfReal, const float* pfImg, size_t uLen
){
	if(pThis->uUpConvLen != uLen){
		if(pThis->pUpConvReal) free(pThis->pUpConvReal);
		pThis->pUpConvReal = (double*)calloc(uLen, sizeof(double));
		if(pThis->pUpConvImg) free(pThis->pUpConvImg);
		pThis->pUpConvImg = (double*)calloc(uLen, sizeof(double));
	}
	
	for(size_t u = 0; u < uLen; u++){
		pThis->pUpConvReal[u] = pfReal[u];
		if(pfImg) pThis->pUpConvImg[u] = pfImg[u];
	}
	
	if(pfImg != NULL)
		return Psd_calculate(pThis, pThis->pUpConvReal, pThis->pUpConvImg, uLen);
	else
		return Psd_calculate(pThis, pThis->pUpConvReal, NULL, uLen);
}

ErrorCode Psd_calculate(
	Das2Psd* pThis, const double* pReal, const double* pImg, size_t uLen
){
	ErrorCode err;
	if(uLen != pThis->uLen){
		if((err=reinit_Psd(pThis, uLen, pThis->bCenter, pThis->sWindow)) != DAS_OKAY) 
			return err;
	}
	
	fftw_plan     pPlan  = (fftw_plan)   pThis->vpPlan;
	fftw_complex* pIn  = (fftw_complex*) pThis->vpIn;
	
	pThis->bRealOnly = (pImg == NULL);
	
	/* Shift Out the DC component, if desired */
	double rRealAvg = 0.0;
	double rImgAvg = 0.0;
	size_t u = 0;
	if(pThis->bCenter){
		for(u=0; u<pThis->uLen; u++){
			rRealAvg += pReal[u];
			if(pImg) rImgAvg += pImg[u];
		}
		rRealAvg /= pThis->uLen;
		rImgAvg /= pThis->uLen;
	}
	
	/* Apply the window, calculate input power, and load to FFTW input */
	pThis->rPwrIn = 0.0;
	for(u=0; u<pThis->uLen; u++){
		pIn[u][0] = (pReal[u] - rRealAvg) * pThis->pWnd[u];
		if(pImg)
			pIn[u][1] = (pImg[u] - rImgAvg) * pThis->pWnd[u];
		else
			pIn[u][1] = 0.0;
		
		if(pImg)
			pThis->rPwrIn += SQUARE((pReal[u] - rRealAvg), (pImg[u] - rImgAvg));
		else
			pThis->rPwrIn += SQUARE((pReal[u] - rRealAvg), 0.0);
	}
	pThis->rPwrIn /= pThis->uLen;
	
	/* Get the DFT */
	fftw_execute(pPlan);

	/* Calc the power spectral density and the output power */
	size_t uMagLen = 0;
	if(pThis->bRealOnly)
		uMagLen = (pThis->uLen/2) + 1;
	else
		uMagLen = pThis->uLen;
	
	if(pThis->uMagLen != uMagLen){
		if(pThis->pMag) free(pThis->pMag);
		pThis->uMagLen = uMagLen;
		pThis->pMag = (double*)calloc(pThis->uMagLen, sizeof(double));
	}
	
	fftw_complex* pOut  = (fftw_complex*) pThis->vpOut;
	pThis->rPwrOut = 0.0;
	if(pThis->bRealOnly){
		size_t N = pThis->uLen;
		size_t No2 = N / 2;
		pThis->pMag[0] = SQUARE(pOut[0][0], pOut[0][1]) / pThis->rWndSqSum;
		pThis->rPwrOut += pThis->pMag[0];
		
		pThis->pMag[No2] = SQUARE(pOut[No2][0], pOut[No2][1]) / pThis->rWndSqSum;
		pThis->rPwrOut += pThis->pMag[No2];
		
		for(u = 1; u<No2; u++){
			pThis->pMag[u] = (SQUARE(pOut[u][0], pOut[u][1])  + 
					            SQUARE(pOut[N-u][0], pOut[N-u][1]) ) / 
					            pThis->rWndSqSum;
			pThis->rPwrOut += pThis->pMag[u];
		}
	}
	else{
		for(u = 0; u<pThis->uLen; u++){
			pThis->pMag[u] = SQUARE(pOut[u][0], pOut[u][1]) / pThis->rWndSqSum;
			pThis->rPwrOut += pThis->pMag[u];
		}
	}
	
	return DAS_OKAY;
}

double Psd_powerRatio(const Das2Psd* pThis, double* pInput, double* pOutput)
{
	if(pInput) *pInput = pThis->rPwrIn;
	if(pOutput) *pOutput = pThis->rPwrOut;
	return (pThis->rPwrOut) / (pThis->rPwrIn);
}


const double* Psd_get(const Das2Psd* pThis, size_t* pLen)
{
	*pLen = pThis->uMagLen;
	return pThis->pMag;
}
