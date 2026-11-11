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


/** @file dft.h Provides a wrapper around FFTW for memory management and
 * normalization.
 */
 
/** @defgroup DFT Discrete Fourier Transforms
 * Discrete Fourier transforms and power spectral density estimation
 * 
 * This module provides an amplitude preserving 1-D Fourier transform and power
 * preserving Power Spectral Density estimator.  One key concept for this 
 * module is FFT plans can not be created and destroyed while transforms are
 * running.  On Linux the FFTW3 library is used to provide fast transform
 * capability, the library to use for Windows is yet to be determined.  
 * 
 * On linux if the file /etc/fftw/wisdom exists it will be loaded during the
 * call to daslib_init().  Pre-planning FFT operations can significantly 
 * increase the speed of new_DasDftPlan() calls
 * 
 * The following example uses the pthread library to demonstrate running four
 * simultaneous transforms.
 * 
 * @code
 * 
 * #define SEC_IN_DAY 86400
 * #define FREQUENCIES (SEC_IN_DAY/2 + 1)
 * 
 * // I/O structure for worker threads
 * struct thread_data {
 *   DasDftPlan* pPlan;
 *   double* pInput;
 *   double* pOutput;
 * };
 * 
 * void* doTransform(void* vpThreadData){
 *   struct thread_data* pTd = (struct thread_data*)vpThreadData;
 * 
 *   // DFT objects should be created on a per-thread basis
 *   DasDft* pDft = new_DasDft(pTd->pPlan, "HANN");
 *   
 *   // Run the transform saving the complex output to DFT obj memory
 *   DasDft_calculate(pDft, pTd->pInput, NULL);
 *
 *   // Copy magnitude out to the location designated be the main thread
 *   size_t uLen = 0;
 *   const double* pMag = DasDft_amplitude(pDft, &uLen);
 *   assert(uLen == FREQUENCIES);
 *   memcpy(pTd->pOutput, pMag, uLen*sizeof(double));
 * 
 *   del_DasDft(pDft);
 *   return NULL;
 * }
 * 
 * int main(int argc, char** argv){
 *   
 *   // Initialize das2C
 *   daslib_init(argv[0], DASERRDISP_EXIT, DASLOG_INFO, NULL);
 * 
 *   double* timeseries = (double*) calloc(SEC_IN_DAY*4, sizeof(double));
 *   
 *   // Do something to fill time array ...
 *  
 *   double* spectra = (double*) calloc(FREQUENCIES*4, sizeof(double));
 * 
 *   // Make a plan for calculating the DFTs
 *   DasDftPlan* pPlan = new_DasDftPlan(SEC_IN_DAY, true);  // true = forward
 * 
 *   // Now calculate 4 spectra at the same time
 *   struct thread_data aTd[4];
 *   pthread_t threads[4];
 *   for(int i = 0; i < 4; ++i){
 *      aTd[i].pPlan = pPlan;
 *      aTd[i].pInput = timeseries + SEC_IN_DAY*i;
 *      aTd[i].pOutput = spectra + FREQUENCIES*i;
 *      pthread_create(threads+i, NULL, doTransform, aTd+i);
 *   }
 *   
 *   for(int i = 0; i < 4; ++i) pthread_join(threads[i], NULL);
 *   
 *   del_DasDftPlan(pPlan);
 *   
 *   // Do something with all 4 spectra ...
 *   return 0;
 * }
 * 
 * @endcode
 * 
 */


#ifndef _das_dft_h_
#define _das_dft_h_
		 
#ifdef __cplusplus
extern "C" {
#endif

#include <das3/defs.h>


/** @addtogroup DFT
 * @{
 */

/** @class DasDftPlan dft.h <das3/dft.h>
 *
 * An structure containing a set of global planning data for DFTs 
 * to be preformed. */
typedef struct das_dftplan_t DasDftPlan;

/** Create a new shareable DFT plan on the heap 
 * 
 * DFT plan creation is thread safe in that this function will block until it 
 * can obtain a global plan lock while the new plan is created.  This function
 * will block if any DFTs are currently being calculated or if another thread is 
 * in the process of creating or deleting a new plan.
 * 
 * @param uLen The length of the 1-D complex signal to analyze
 * @param bForward Whether to do a forward DFT or revers DFT
 * @returns A new DasDftPlan allocated on the heap suitable for use in multiple
 *          simultaneous calls to DasDft_calculate().
 * @memberof DasDftPlan
 */
DAS_API DasDftPlan* new_DasDftPlan(size_t uLen, bool bForward);


/** Delete a shareable DFT plan from the heap
 * 
 * DFT plan destruction is thread safe in that this function will block until it 
 * can obtain a global plan lock while the plan is being deleted.  This function
 * will block if any DFTs are currently being calculated or if another thread is 
 * in the process of creating or deleting a plan.
 * 
 * @warning For the love of all that's holy don't run this function if any of
 * your exec threads are still executing.  I don't mean the actual 
 * dft_execute function but the whole damn thread!
 * 
 * @param pThis
 * @memberof DasDftPlan
 */
DAS_API bool del_DasDftPlan(DasDftPlan* pThis);

/** @class DasDft dft.h <das3/dft.h>
 * An amplitude preserving Discrete Fourier Transform converter
 *
 * On POSIX systems this code uses pthreads and fftw to handle simultaneous
 * FFTs.  Create one per thread; any number of them may share a DasDftPlan.  See
 * the example at the top of this file.
 */
typedef struct das_dft_t DasDft;

/** Create a new DFT calculator
 * 
 * This function allocates re-usable storage for Fourier transform output,
 * but in order to preform calculations a re-usable plan object must be 
 * provided
 * 
 * @param pPlan - A Transform plan object.  The reference count of DFTs 
 *                using this plan will be incremented.
 * 
 * @param sWindow - A named window to apply to the data.  If NULL then 
 *               no window will be used.
 * 
 * @return A new DasDft object allocated on the heap.
 * @memberof DasDft
 */
DAS_API DasDft* new_DasDft(DasDftPlan* pPlan, const char* sWindow);

/** Free a DFT (Discrete Fourier Transform) calculator
 * 
 * @param pThis the DFT calculator to free, the caller should set the object
 *        pointer to NULL after this call.  Calling this also deletes the 
 *        reference count for the associated DasDftPlan object
 * 
 * @memberof DasDft
 */
DAS_API void del_DasDft(DasDft* pThis);

/** The length of the input vectors DasDft_calculate() expects
 *
 * @memberof DasDft
 */
DAS_API size_t DasDft_length(const DasDft* pThis);

/** Calculate a discrete Fourier transform
 * 
 * Using the calculation plan setup in the constructor, calculate a discrete
 * Fourier transform.  When this function is called internal storage of any
 * previous DFT calculations (if any) are over written.
 *
 * @param pThis The DFT object which will hold the result memory
 * 
 * @param pReal An input vector of with the same length as the plan object
 *        provided to the constructor
 * 
 * @param pImg The imaginary (or quadrature phase) input vector with the
 *        same length as pRead.  For a purely real signal this vector is
 *        NULL. 
 * 
 * @memberof DasDft
 * @return 0 (DAS_OKAY) if the calculation was successful, a non-zero error code
 *           otherwise
 */
DAS_API das_errcode DasDft_calculate(
	DasDft* pThis, const double* pReal, const double* pImg
);

/** Return the real component after a calculation
 * 
 * @param pThis
 * @param pLen
 * @return 
 * @memberof DasDft
 */
DAS_API const double* DasDft_real(DasDft* pThis, size_t* pLen);

/** Return the imaginary component after a calculation
 * 
 * @param pThis
 * @param pLen
 * @return 
 * @memberof DasDft
 */
DAS_API const double* DasDft_imag(DasDft* pThis, size_t* pLen);

/** Get the amplitude magnitude vector from a calculation
 * 
 * Scale the stored DFT so that it preserves amplitude, and get the magnitude.
 * For real-valued inputs (complex pointer = 0) the 'positive' and 'negative'
 * frequencies are combined.  For complex input vectors this is not the case
 * since all DFT output amplitudes are unique.  Stated another way, for complex
 * input signals components above the Nyquist frequency have meaningful
 * information.
 * 
 * @param pThis The DFT calculator object which has previously been called to
 *        calculate a result.
 * 
 * @param pLen The vector length.  In general this is *NOT* the same as the
 *        input time series length.  For real-value input signals (complex
 *        input is NULL), this is N/2 + 1.  For complex input signals this is N.
 * 
 * @return A pointer to an internal holding bin for the real signal magnitude
 *         values.  
 * 
 * @warning If DasDft_calculate() is called again, the return pointer can be
 *          invalidated.  If a permanent result is needed after subsequent
 *          DasDft_calculate() calls, copy these data to another buffer.
 * @memberof DasDft
 */
DAS_API const double* DasDft_amplitude(DasDft* pThis, size_t* pLen);

/** @class DasPsd dft.h <das3/dft.h>
 * A power spectral density estimator (periodogram)
 *
 * This is a wrapper around the FFTW (Fastest Fourier Transform in the West)
 * library to handle memory management, normalization and windowing.
 */
typedef struct das_psd_t DasPsd;

/** Create a new Power Spectral Density Calculator
 * 
 * This estimator uses the equations given in Numerical Recipes in C, section
 * 13.4, but not any of the actual Numerical Recipes source code.
 * 
 * @param pPlan - A Transform plan object.  The reference count of DFTs 
 *                using this plan will be incremented.
 * 
 * @param bCenter If true, input values will be centered on the Mean value.  
 *        This shifts-out the DC component from the input.
 * 
 * @param sWindow A named window to use for the data.  Possible values are:
 *        "hann" - Use a hann window as defined at 
 *                 http://en.wikipedia.org/wiki/Hann_function
 *        NULL  - Use a square window. (i.e. 'multiply' all data by 1.0)  
 * 
 * @return A new Power Spectral Density estimator allocated on the heap
 * @memberof DasPsd
 */
DAS_API DasPsd* new_DasPsd(DasDftPlan* pPlan,  bool bCenter, const char* sWindow);

/** Free a Power Spectral Density calculator
 * 
 * @param pThis the PSD calculator to free, the caller should set the object
 *        pointer to NULL after this call.  Calling this also deletes the 
 *        reference count for the associated DasDftPlan object
 *
 * @memberof DasPsd
 */
DAS_API void del_DasPsd(DasPsd* pThis);

/** The length of the input vectors DasPsd_calculate() expects
 *
 * @memberof DasPsd
 */
DAS_API size_t DasPsd_length(const DasPsd* pThis);


/** Calculate a Power Spectral Density (periodogram)
 * 
 * Using the calculation plan setup in the constructor, calculate a discrete
 * Fourier transform.  When this function is called internal storage of any
 * previous DFT calculations (if any) are over written.
 *
 * @param pThis The PSD calculator object
 * 
 * @param pReal An input vector of with the same length as the plan object
 *        provided to the constructor
 * 
 * @param pImg The imaginary (or quadrature phase) input vector with the
 *        same length as pRead.  For a purely real signal this vector is
 *        NULL. 
 * 
 * @return 0 (DAS_OKAY) if the calculation was successful, a non-zero error code
 *           otherwise
 * @memberof DasPsd
 */
DAS_API das_errcode DasPsd_calculate(
	DasPsd* pThis, const double* pReal, const double* pImg
);

/** The floating point array input analog of DasPsd_calculate()
 * 
 * Internal calculations are still handled in double precision.
 * @memberof DasPsd
 */
DAS_API das_errcode DasPsd_calcFloat(
	DasPsd* pThis, const float* pReal, const float* pImg
);

/** Provide a comparison of the input power and the output power.
 * 
 * During the DasPsd_calculate() call the average magnitude of the input vector
 * is saved along with the average magnitude of the output vector (divided by
 * the Window summed and squared).  These two measures of power should always
 * be close to each other when using a hann window.  When using a NULL window
 * they should be almost identical, to within rounding error.  The two measures
 * are:
 * 
 * <pre>
 *              N-1
 *          1  ----   2      2
 *  Pin =  --- \    r    +  i
 *          N  /     n       n
 *             ----
 *              n=0
 *
 *                N-1
 *           1   ----   2      2
 *  Pout =  ---  \    R    +  I
 *          Wss  /     k       k
 *               ----
 *                k=0
 * </pre>
 * 
 * where Wss collapses to N**2 when a NULL (square) window is used.  The reason
 * that the Pout has an extra factor of N in the denominator is due to the
 * following identity for the discrete Fourier transform (Parseval's theorem):
 * <pre>
 * 
 *     N-1                   N-1
 *    ----   2    2      1  ----  2    2
 *    \     r  + i   =  --- \    R  + I
 *    /      n    n      N  /     n    n
 *    ----                  ----
 *     n=0                   k=0
 * 
 * </pre>
 * Where r and i are the real and imaginary input amplitudes, and R and I are
 * the DFT real and imaginary output values.
 * 
 * @param pThis A PSD calculator for which DasPsd_calculate() has been called
 * 
 * @param pInput A pointer to store the input power.  If NULL, the input power
 *               will no be saved separately.
 * 
 * @param pOutput A pointer to store the output power.  If NULL, the output power
 *               will no be saved separately.
 * 
 * @return The ratio of Power Out divided by Power In. (Gain).
 * @memberof DasPsd
 */
DAS_API double DasPsd_powerRatio(
	const DasPsd* pThis, double* pInput, double* pOutput
);


/** Get the power spectral density from a calculation
 * 
 * For real-valued inputs (complex pointer = 0) the 'positive' and 'negative'
 * frequencies are combined.  For complex input vectors this is not the case
 * since all DFT output amplitudes are unique.  Stated another way, for complex
 * input signals components above the Nyquist frequency have meaningful
 * information.
 * 
 * @param pThis The PSD calculator object which has previously been called to
 *        calculate a result.
 * 
 * @param pLen The vector length.  In general this is *NOT* the same as the
 *        input time series length.  For real-value input signals (complex
 *        input is NULL), this is N/2 + 1.  For complex input signals this is N.
 * 
 * @return A pointer to an internal holding bin for the PSD values.
 *
 * @warning If DasPsd_calculate() is called again, the return pointer can be
 *          invalidated.  If a permanent result is needed after subsequent
 *          DasPsd_calculate() calls, copy these data to another buffer.
 *
 * @memberof DasPsd
 */
DAS_API const double* DasPsd_power(const DasPsd* pThis, size_t* pLen);

#ifdef __cplusplus
}
#endif

/** @} */
#endif
