/* Copyright (C) 2015-2026 Chris Piker <chris-piker@uiowa.edu>
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
/* #define _XOPEN_SOURCE 500 */ /* Trying to get pthread_mutexattr_settype */

#include <pthread.h>
#include <string.h>
#include <math.h>

#ifdef _WIN32
#define strcasecmp _stricmp
#define strncasecmp _strnicmp
#else
#include <strings.h>
#endif

#include <fftw3.h>

#include "log.h"
#include "util.h"
#include "dft.h"
#include "util_p.h"

#define MAGNITUDE(r, i) sqrt( (r)*(r) + (i)*(i) )
#define SQUARE(r, i) ( (r)*(r) + (i)*(i) )

#ifndef M_PI
#define M_PI 3.1415926535897931
#endif

/* No wisdom on wasm.*  Wisdom only fits the FFTW build and CPU it was
   measured on, so a native file is no use to a wasm libfftw3.  There's no
   cycle counter to measure with either, and the browser module has no
   filesystem to read one from. 

   *some may beg to differ
*/
#ifndef __EMSCRIPTEN__
#ifndef WISDOM_FILE
#error WISDOM_FILE should be defined to the system fftw wisdom location
#endif

#define _QDEF(x) #x
#define QDEF(x) _QDEF(x)
#define DEF_WISDOM QDEF(WISDOM_FILE)
#endif

/* Nothing to measure with on wasm either, and worse, FFTW picks
   clock_gettime(CLOCK_SGI_CYCLE) for its tick counter which no
   runtime supports and the ticks never advance. */
#ifdef __EMSCRIPTEN__
#define DAS_FFTW_PLANNER FFTW_ESTIMATE
#else
#define DAS_FFTW_PLANNER FFTW_MEASURE
#endif

static int g_nExecCount = 0;
static pthread_mutex_t g_mtxExecCount = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t g_cndExecCountDec = PTHREAD_COND_INITIALIZER;

bool das_dft_init(const char* sProgName){
#ifndef __EMSCRIPTEN__
	/* import the system wide FFTW wisdom */
	bool bHaveWisdomFile = false;
	FILE* pWis = NULL;
	int nLoadWis = 0;
	
	if(dasfs_isFile(DEF_WISDOM)){
		if( (pWis = fopen(DEF_WISDOM, "rb")) != NULL) {
			
			bHaveWisdomFile = true;
			nLoadWis = fftw_import_wisdom_from_file(pWis);
			fclose(pWis);
			
			if(nLoadWis != 1){
				daslog_debugf("(%s) fftw3 is suspicious of the wisdom file "
				              DEF_WISDOM " and refused to use it.", sProgName);
			}
		}
		else{
			daslog_warnf("(%s) Couldn't read fftw wisdom file: " DEF_WISDOM,
			              sProgName);
		}
	}
	
	if(!bHaveWisdomFile){
		/* Default das2 log level is info, so this works before the call to 
		 * lower it */
		daslog_debugf(
			"(%s) FFTW wisdom file not found. Hint: you can speed up das2C DFT"
			" functions by running fftw-wisdom and saving the the results in "
			DEF_WISDOM, sProgName
		);
	}
#endif

	pthread_mutexattr_t attr;
	pthread_mutexattr_init(&attr);
#ifndef NDEBUG
	pthread_mutexattr_settype(&attr, PTHREAD_MUTEX_ERRORCHECK);
#endif
	if( pthread_mutex_init(&g_mtxExecCount, &attr) != 0) return false;
	return true;
}


/* ************************************************************************* */
/* Real DFT plans we are using are a super-set of the one in the header      */

struct das_dftplan_t{
	/* Intentionally held as a void pointer so that application code
	 * doesn't need to have access to the fftw3.h file */
	void* vpPlan;
		
	/* I think the planner needs memory as well */
	void* vpIn;
	void* vpOut;
	
	/* Save the length and the direction */
	size_t uLen;
	bool bForward;
	
	/* Save a count of how many DFTs are using this plan, used to prevent
	 * plan deletion */
	int nCount;
	

	pthread_mutex_t mtxCount;
	pthread_cond_t cndCountDec;

};


/* ************************************************************************* */
/* Plan functions and the corresponding thread locks */

DasDftPlan* new_DasDftPlan(size_t uLen, bool bForward)
{
	/* Don't let the DFT exec count change while we are in the planner */
	pthread_mutex_lock(&g_mtxExecCount);
	
	while(g_nExecCount > 0){
		/* ... unless we need to wait for it to decrease */
		/* the cond wait function will release the count mutex and this thread
		 * will not wake up again until another thread calls 
		 * pthread_cond_broadcast or pthread_cond_signal */
		pthread_cond_wait(&g_cndExecCountDec, &g_mtxExecCount);
	}
	
	DasDftPlan* pThis = (DasDftPlan*)calloc(1, sizeof(DasDftPlan));
	
	pthread_mutexattr_t attr;
	pthread_mutexattr_init(&attr);
#ifndef NDEBUG
	pthread_mutexattr_settype(&attr, PTHREAD_MUTEX_ERRORCHECK);
#endif
	pthread_mutex_init(&(pThis->mtxCount), &attr);
	pthread_cond_init(&(pThis->cndCountDec), NULL);
	
	pThis->vpIn = fftw_malloc(uLen*sizeof(fftw_complex));
	pThis->vpOut = fftw_malloc(uLen*sizeof(fftw_complex));
	
	int nSign = FFTW_FORWARD;
	if(!bForward) nSign = FFTW_BACKWARD;
	
	pThis->vpPlan = fftw_plan_dft_1d(
		uLen, (fftw_complex*)pThis->vpIn, (fftw_complex*)pThis->vpOut,
		nSign, DAS_FFTW_PLANNER
	);
	
	pThis->uLen = uLen;
	pThis->bForward = bForward;
	
	pthread_mutex_unlock(&g_mtxExecCount);
	return pThis;
}

bool del_DasDftPlan(DasDftPlan* pThis)
{	
	/* Don't let the DFT exec count change while we are in the planner... */
	pthread_mutex_lock(&g_mtxExecCount);
	
	while(g_nExecCount > 0){
		/* ... unless we need to wait for it to decrease */
		pthread_cond_wait(&g_cndExecCountDec, &g_mtxExecCount);
		
		/* Don't delete this plan if someone is using it */
		while(pThis->nCount > 0){
			pthread_cond_wait(&(pThis->cndCountDec), &(pThis->mtxCount));
		}
	}
	
	fftw_destroy_plan( (fftw_plan)pThis->vpPlan);
	pthread_mutex_unlock(&(pThis->mtxCount));
	pthread_mutex_unlock(&g_mtxExecCount);
	free(pThis);
	return true;
}

/* ************************************************************************* */
/* Construction/Destruction DFT object */

struct das_dft_t {

	/* The plan, the only variable changed in the plan is the usage count */
	DasDftPlan* pPlan;

	/* FFTW variables */
	void* vpIn;
	void* vpOut;

	/* Input vector length */
	size_t uLen;

	/* Input vector is real only*/
	bool bRealOnly;

	/* DFT Direction */
	bool bForward;

	/* Holder for the window function and name*/
	char* sWindow;
	double* pWnd;

	/* Holder for the magnitude result */
	bool bNewMag;
	double* pMag;
	size_t uMagLen;

	/* Holder for continuous real and imaginary results */
	bool bNewCmp[2];   /* fftw convention: 0 = reals, 1 = img */
	double* pCmpOut[2];
	size_t uCmpLen[2];
};

DasDft* new_DasDft(DasDftPlan* pPlan, const char* sWindow)
{	
	pthread_mutex_lock(&(pPlan->mtxCount));
	pPlan->nCount++;
	pthread_mutex_unlock(&(pPlan->mtxCount));
	
	if((pPlan->uLen == 0) ||((pPlan->uLen) % 2 != 0)){
		daserrf(DASERR_DFT, "Can't handle odd length DFTs or DFTs "
		           "less than 2 points long.");
		pthread_mutex_lock(&(pPlan->mtxCount));
		pPlan->nCount--;
		pthread_mutex_unlock(&(pPlan->mtxCount));
		return NULL;
	}
	
	/* Window can be NULL, or "HANN" */
	if(sWindow != NULL && (strncasecmp(sWindow, "HANN", 4) != 0)){
		daserrf(DASERR_DFT, "Unknown window function: '%s'", sWindow);
		pthread_mutex_lock(&(pPlan->mtxCount));
		pPlan->nCount--;
		pthread_mutex_unlock(&(pPlan->mtxCount));
		return NULL;
	}
	
	DasDft* pThis = (DasDft*)calloc(1, sizeof(DasDft));
	
	pThis->pPlan = pPlan;
	pThis->uMagLen = 0;
	pThis->pMag = NULL;
	pThis->bNewMag = false;
	
	for(size_t u = 0; u<2; u++){
		pThis->pCmpOut[u] = NULL;
		pThis->uCmpLen[u] = 0;
		pThis->bNewCmp[u] = false;
	}
	
	pThis->uLen = pPlan->uLen;
	
	pThis->vpIn = fftw_malloc((pThis->uLen)*sizeof(fftw_complex));
	pThis->vpOut = fftw_malloc((pThis->uLen)*sizeof(fftw_complex));
	
	pThis->bRealOnly = false;
	
	pThis->pWnd = (double*)calloc(pThis->uLen, sizeof(double));
	size_t u;
	if(sWindow == NULL){
		for(u = 0; u < pThis->uLen; u++) pThis->pWnd[u] = 1.0;
	}
	else{
		size_t uWinLen = strlen(sWindow);
		pThis->sWindow = (char*)calloc(uWinLen + 1, sizeof(char));
		memcpy(pThis->sWindow, sWindow, uWinLen);
	
		if(strncasecmp(sWindow, "HANN", 4) == 0){
		
			/* Using a constant of 2 to make the area under the curve = 1.0 */
			for(u = 0; u < pThis->uLen; u++)
				pThis->pWnd[u] = 2.0*(1.0 - cos((2.0*M_PI*u)/(pThis->uLen - 1)));
		}
		
		/* Other windows here... */
	}
	
	return pThis;
}

void del_DasDft(DasDft* pThis){
	
	/* In case another thread is waiting to delete a plan, let it know that
	   you no longer need the plan */
	pthread_mutex_lock(&(pThis->pPlan->mtxCount));
	pThis->pPlan->nCount -= 1;
	pthread_cond_broadcast(&(pThis->pPlan->cndCountDec));
	pthread_mutex_unlock(&(pThis->pPlan->mtxCount));
	
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

das_errcode DasDft_calculate(
	DasDft* pThis, const double* pReal, const double* pImg
){
	
	/* Block waiting to increment the exec count */
	pthread_mutex_lock(&g_mtxExecCount);
	g_nExecCount += 1;
	pthread_mutex_unlock(&g_mtxExecCount);
	
	if(pThis->pPlan->uLen != pThis->uLen){
		pthread_mutex_lock(&g_mtxExecCount);
		g_nExecCount -= 1;
		pthread_mutex_unlock(&g_mtxExecCount);
		return daserrf(DASERR_DFT, 
			"Some one changed the plan while it was in use! "
			"Plan/DFT length mismatch, attempting to calculate a %zu length DFT "
			"with a %zu length plan", pThis->uLen, pThis->pPlan->uLen
		);
	}
	
	fftw_plan     pFftwPlan  = (fftw_plan)   pThis->pPlan->vpPlan;
	fftw_complex* pIn  = (fftw_complex*) pThis->vpIn;
	fftw_complex* pOut = (fftw_complex*) pThis->vpOut;
	
	for(size_t u = 0; u < pThis->uLen; u++){
		pIn[u][0] = pReal[u] * pThis->pWnd[u];
		if(pImg)
			pIn[u][1] = pImg[u] * pThis->pWnd[u];
		else
			pIn[u][1] = 0.0;
	}
	
	fftw_execute_dft(pFftwPlan, pIn, pOut);

	pThis->bNewMag = true;
	pThis->bNewCmp[0] = true; pThis->bNewCmp[1] = true;
	pThis->bRealOnly = (pImg == NULL);

	/* Block waiting to decrement the exec count, and signal anyone waiting 
	 * to hear that the count has decreased */
	pthread_mutex_lock(&g_mtxExecCount);
	g_nExecCount -= 1;
	pthread_cond_broadcast(&g_cndExecCountDec);
	pthread_mutex_unlock(&g_mtxExecCount);
	
	return DAS_OKAY;
}

/* ************************************************************************* */
/* Retrieving Results */

size_t DasDft_length(const DasDft* pThis){ return pThis->uLen; }

static const double* _DasDft_getComp(DasDft* pThis, size_t* pLen, size_t uCmp)
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

const double* DasDft_real(DasDft* pThis, size_t* pLen)
{
	return _DasDft_getComp(pThis, pLen, 0);
}

const double* DasDft_imag(DasDft* pThis, size_t* pLen)
{
	return _DasDft_getComp(pThis, pLen, 1);
}

const double* DasDft_amplitude(DasDft* pThis, size_t* pLen)
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
		daserrf(DASERR_NOTIMP, "Magnitude calculation for complex input not "
				     "yet implemented");
		return NULL;
	}
	
	*pLen = pThis->uMagLen;
	return pThis->pMag;
}

/* ***************************** PSD Objects ******************************* */


/* ************************************************************************* */
/* Construction/Re-Initialization/Destruction PSD object */

struct das_psd_t {

	/* The plan, the only variable changed in the plan is the usage count */
	DasDftPlan* pPlan;

	/* FFTW variables */
	void* vpIn;
	void* vpOut;

	/* Input vector information */
	size_t uLen;
	bool bRealOnly;

	/* Center data about average first */
	bool bCenter;

	/* Holder for up conversion arrays, helps DasPsd_calcFloat */
	size_t uUpConvLen;
	double* pUpConvReal;
	double* pUpConvImg;

	/* Holder for the window function and name */
	char* sWindow;
	double* pWnd;
	double rWndSqSum;

	/* Holder for the PSD result */
	double* pMag;
	size_t uMagLen;

	/* Total Energy calculations */
	double rPwrIn;
	double rPwrOut;
};

DasPsd* new_DasPsd(DasDftPlan* pPlan, bool bCenter, const char* sWindow)
{
	
	pthread_mutex_lock(&(pPlan->mtxCount));
	pPlan->nCount++;
	pthread_mutex_unlock(&(pPlan->mtxCount));

	if((pPlan->uLen == 0) ||((pPlan->uLen) % 2 != 0)){
		daserrf(DASERR_DFT, "Can't handle odd length DFTs or DFTs "
		           "less than 2 points long.");
		pthread_mutex_lock(&(pPlan->mtxCount));
		pPlan->nCount--;
		pthread_mutex_unlock(&(pPlan->mtxCount));
		return NULL;
	}

	DasPsd* pThis = (DasPsd*)calloc(1, sizeof(DasPsd));
	
	pThis->pPlan = pPlan;
	pThis->uMagLen = 0;
	pThis->pMag = NULL;
	
	pThis->rPwrIn = 0.0;
	pThis->rPwrOut = 0.0;
	
	pThis->uLen = pPlan->uLen;
	
	pThis->vpIn = fftw_malloc((pThis->uLen)*sizeof(fftw_complex));
	pThis->vpOut = fftw_malloc((pThis->uLen)*sizeof(fftw_complex));
		
	pThis->bRealOnly = false;
	pThis->bCenter = bCenter;
	
	pThis->pWnd = (double*)calloc((pThis->uLen), sizeof(double));
	pThis->rWndSqSum = 0.0;
	size_t u;
	if(sWindow == NULL){
		for(u = 0; u<(pThis->uLen); u++) pThis->pWnd[u] = 1.0;
		pThis->rWndSqSum = ((double)pThis->uLen) * ((double)pThis->uLen);
		pThis->sWindow = NULL;
	}
	else{
		pThis->sWindow = (char*)calloc(strlen(sWindow) + 1, sizeof(char));
		strcpy(pThis->sWindow, sWindow);
	
		/* Strict Hann Function Definition used here: */
		/*   http://en.wikipedia.org/wiki/Hann_function */
		/* Window Square Sum (Wss) used here: */
		/*   Numerical Recipes in C, equation 13.4.11 */
	
		if(strncasecmp(sWindow, "HANN", 4) == 0){
		
			for(u = 0; u<(pThis->uLen); u++){
				pThis->pWnd[u] = 0.5*(1.0 - cos((2.0*M_PI*u)/((pThis->uLen) - 1)));
				pThis->rWndSqSum += pThis->pWnd[u] * pThis->pWnd[u];
			}
			pThis->rWndSqSum *= pThis->uLen;
		}
		
		/* Other windows here in the future */
	}
	
	return pThis;
}


void del_DasPsd(DasPsd* pThis)
{
	/* In case another thread is waiting to delete a plan, let it know that
	   you no longer need the plan */
	pthread_mutex_lock(&(pThis->pPlan->mtxCount));
	pThis->pPlan->nCount -= 1;
	pthread_cond_broadcast(&(pThis->pPlan->cndCountDec));
	pthread_mutex_unlock(&(pThis->pPlan->mtxCount));

	if(pThis->vpIn) fftw_free( pThis->vpIn);
	if(pThis->vpOut) fftw_free( pThis->vpOut);
	if(pThis->pWnd) free(pThis->pWnd);
	if(pThis->sWindow) free(pThis->sWindow);
	if(pThis->pMag) free(pThis->pMag); 
	if(pThis->pUpConvReal) free(pThis->pUpConvReal);
	if(pThis->pUpConvImg) free(pThis->pUpConvImg);
	free(pThis);
}


das_errcode DasPsd_calcFloat(
	DasPsd* pThis, const float* pfReal, const float* pfImg
){
	
	if(pThis->uUpConvLen != pThis->uLen){
		pThis->pUpConvReal = (double*)calloc((pThis->uLen), sizeof(double));
		pThis->pUpConvImg = (double*)calloc((pThis->uLen), sizeof(double));
		pThis->uUpConvLen = pThis->uLen;
	}
	
	for(size_t u = 0; u < (pThis->uLen); u++){
		pThis->pUpConvReal[u] = pfReal[u];
		if(pfImg) pThis->pUpConvImg[u] = pfImg[u];
	}
	
	if(pfImg != NULL)
		return DasPsd_calculate(pThis, pThis->pUpConvReal, pThis->pUpConvImg);
	else
		return DasPsd_calculate(pThis, pThis->pUpConvReal, NULL);
}

das_errcode DasPsd_calculate(
	DasPsd* pThis, const double* pReal, const double* pImg
){
	
	/* Block waiting to increment the exec count */
	pthread_mutex_lock(&g_mtxExecCount);
	g_nExecCount += 1;
	pthread_mutex_unlock(&g_mtxExecCount);

	if(pThis->pPlan->uLen != pThis->uLen){
		pthread_mutex_lock(&g_mtxExecCount);
		g_nExecCount -= 1;
		pthread_mutex_unlock(&g_mtxExecCount);
		return daserrf(DASERR_DFT, 
			"Some one changed the plan while it was in use! "
			"Plan/DFT length mismatch, attempting to calculate a %zu length DFT "
			"with a %zu length plan", pThis->uLen, pThis->pPlan->uLen
		);
	}
	
	fftw_plan     pFftwPlan  = (fftw_plan)   pThis->pPlan->vpPlan;
	fftw_complex* pIn  = (fftw_complex*) pThis->vpIn;
	fftw_complex* pOut = (fftw_complex*) pThis->vpOut;
	
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
	fftw_execute_dft(pFftwPlan, pIn, pOut);

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
	
	pthread_mutex_lock(&g_mtxExecCount);
	g_nExecCount -= 1;
	pthread_mutex_unlock(&g_mtxExecCount);
	
	return DAS_OKAY;
}

size_t DasPsd_length(const DasPsd* pThis){ return pThis->uLen; }

double DasPsd_powerRatio(const DasPsd* pThis, double* pInput, double* pOutput)
{
	if(pInput) *pInput = pThis->rPwrIn;
	if(pOutput) *pOutput = pThis->rPwrOut;
	return (pThis->rPwrOut) / (pThis->rPwrIn);
}


const double* DasPsd_power(const DasPsd* pThis, size_t* pLen)
{
	*pLen = pThis->uMagLen;
	return pThis->pMag;
}
