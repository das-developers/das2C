/** @file dft.h Provides a wrapper around FFTW for memory management and
 * normalization.
 */

#ifndef _das2_dft_h_
#define _das2_dft_h_

/** An amplitude preserving Discrete Fourier Transform converter 
 * 
 * This is small wrapper around fftw to handle memory management, 
 * normalization, and windowing
 */
typedef struct das2_dft_t{

	/* FFTW variables */
	void* vpPlan;
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
	
} Das2Dft;

/** Create a new DFT calculator
 * 
 * @param uLen - The length of the data vectors that will be supplied to the
 *               calculate function
 * @param sWindow - A named window to apply to the data.  If NULL then 
 *               no window will be used.
 * @param bForward - If true calculate a forward DFT, if false calculate an
 *               inverse DFT.  (The difference between the two is the index
 *                order.)
 * 
 * @return A new Das2Dft object allocated on the heap.
 * @memberof Das2Dft
 */
Das2Dft* new_Dft(size_t uLen, const char* sWindow, bool bForward);

/** Free a DFT (Discrete Fourier Transform) calculator
 * 
 * @param pThis the DFT calculator to free, the caller should set the object
 *        pointer to NULL after this call.
 * @memberof Das2Dft
 */
void del_Dft(Das2Dft* pThis);

/** Calculate a discrete Fourier transform
 * 
 * Using the calculation plan setup in the constructor, calculate a discrete
 * Fourier transform.  When this function is called internal storage of any
 * previous DFT calculations (if any) are over written.
 *
 * @param pThis The DFT object
 * 
 * @param pReal A "time domain" input vector of length uLen
 * 
 * @param pImg The imaginary (or quadrature phase) input vector of length
 *             uLen.  For a purely real signal this vector is NULL. 
 * 
 * @param uLen The number of reals in the input signal.  If this value changes
 *             between successive calls to this function for the same Dft object
 *             then you're code will take a performance hit.
 * 
 * @memberof Das2Dft
 * @return 0 (DAS_OKAY) if the calculation was successful, a non-zero error code
 *           otherwise
 */
ErrorCode Dft_calculate(
	Das2Dft* pThis, const double* pReal, const double* pImg, size_t uLen
);

/** Calculate an inverse discrete Fourier transform
 * 
 * Using the calculation plan setup in the constructor, calculate a discrete
 * Fourier transform.  When this function is called internal storage of any
 * previous DFT calculations (if any) are over written.
 *
 * @param pThis The DFT object
 * 
 * @param pReal A "time domain" input vector of length uLen
 * 
 * @param pImg The imaginary (or quadrature phase) input vector of length
 *             uLen.  For a purely real signal this vector is NULL. 
 * 
 * @param uLen The number of reals in the input signal.  If this value changes
 *             between successive calls to this function for the same Dft object
 *             then you're code will take a performance hit.
 * 
 * @memberof Das2Dft
 * @return 0 (DAS_OKAY) if the calculation was successful, a non-zero error code
 *           otherwise
 */
ErrorCode Dft_cal_inv(
	Das2Dft* pThis, const double* pReal, const double* pImg, size_t uLen
);

/** Return the real component after a calculation
 * 
 * @param pThis
 * @param pLen
 * @return 
 */
const double* Dft_getReal(Das2Dft* pThis, size_t* pLen);

/** Return the imaginary component after a calculation
 * 
 * @param pThis
 * @param pLen
 * @return 
 * @memberof Das2Dft
 */
const double* Dft_getImg(Das2Dft* pThis, size_t* pLen);

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
 *        input is NULL, this is N/2 + 1.  For complex input signals this is N.
 * 
 * @return A pointer to an internal holding bin for the real signal magnitude
 *         values.  
 * 
 * @warning If Dft_calculate() is called again, the return pointer can be
 *          invalidated.  If a permanent result is needed after subsequent
 *          Dft_calculate() calls, copy these data to another buffer.
 * @memberof Das2Dft
 */
const double* Dft_getMagnitude(Das2Dft* pThis, size_t* pLen);

/** A power spectral density estimator (periodogram)
 * 
 * This is a wrapper around the FFTW (Fastest Fourier Transform in the West) 
 * library to handle memory management, normalization and windowing.
 */
typedef struct das2_psd_t{
	
	/* FFTW variables */
	void* vpPlan;
	void* vpIn;
	void* vpOut;
	
	/* Input vector information */
	size_t uLen;
	bool bRealOnly;
	
	/* Center data about average first */
	bool bCenter;
	
	/* Holder for up conversion arrays, helps Psd_calculate_f*/
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
	
} Das2Psd;

/** Create a new Power Spectral Density Calculator
 * 
 * This estimator uses the equations given in Numerical Recipes in C, section
 * 13.4, but not any of the actual Numerical Recipes source code.
 * 
 * @param uLen The length of the input complex series
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
 * @memberof Das2Psd
 */
Das2Psd* new_Psd(size_t uLen,  bool bCenter, const char* sWindow);

/** Free a Power Spectral Density calculator
 * 
 * @param pThis 
 * @memberof Das2Psd
 */
void del_Das2Psd(Das2Psd* pThis);


/** Calculate a Power Spectral Density (periodogram)
 * 
 * Using the calculation plan setup in the constructor, calculate a discrete
 * Fourier transform.  When this function is called internal storage of any
 * previous DFT calculations (if any) are over written.
 *
 * @param pThis The PSD calculator object
 * 
 * @param pReal A "time domain" input vector of length uLen
 * 
 * @param pImg The imaginary (or quadrature phase) input vector of length
 *             uLen.  For a purely real signal this vector is NULL. 
 * 
 * @param uLen The number of reals in the input signal.  If this value changes
 *             between successive calls to this function for the same PSD object
 *             then you're code will take a performance hit.
 * 
 * @return 0 (DAS_OKAY) if the calculation was successful, a non-zero error code
 *           otherwise
 * @memberof Das2Psd
 */
ErrorCode Psd_calculate(
	Das2Psd* pThis, const double* pReal, const double* pImg, size_t uLen
);

/** The floating point array input analog of Psd_calaculate()
 * 
 * Internal calculations are still handled in double precision.
 * @memberof Das2Psd
 */
ErrorCode Psd_calculate_f(
	Das2Psd* pThis, const float* pReal, const float* pImg, size_t uLen
);

/** Provide a comparison of the input power and the output power.
 * 
 * During the Psd_calculate() call the average magnitude of the input vector
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
 * @param pThis A PSD calculator for which Psd_calculate has been called
 * 
 * @param pInput A pointer to store the input power.  If NULL, the input power
 *               will no be saved separately.
 * 
 * @param pOutput A pointer to store the output power.  If NULL, the output power
 *               will no be saved separately.
 * 
 * @return The ratio of Power Out divided by Power In. (Gain).
 * @memberof Das2Psd
 */
double Psd_powerRatio(const Das2Psd* pThis, double* pInput, double* pOutput);


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
 *        input is NULL, this is N/2 + 1.  For complex input signals this is N.
 * 
 * @return A pointer to an internal holding bin for the real signal magnitude
 *         values. 
 *
 * @warning If Psd_calculate() is called again, the return pointer can be
 *          invalidated.  If a permanent result is needed after subsequent
 *          Psd_calculate() calls, copy these data to another buffer.
 *
 * @memberof Das2Psd
 */
const double* Psd_get(const Das2Psd* pThis, size_t* pLen);

#endif
