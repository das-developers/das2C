/** @file das1.h Das 1 Utilities 
 * Basic Das utilities.  Many of these are utilized by the Das2 utilities.
 */

#ifndef _das_h_

#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <stdio.h>

/* Get compile time byte order, results in faster code that avoids 
 * runtime checks.  For some newer chips this may not work as the
 * processor can be switched from big endian to little endian at runtime.
 *
 * At the end of the day either HOST_IS_LSB_FIRST will be defined, or it won't.
 * If this macro is defined then the host computer stores the least significant
 * byte of a word in the lowest address, i.e. it's a little endian machine.  If
 * this macro is not defined then the host computer stores the list significant
 * byte of a word in the highest address, i.e. it a big endian machine.
 */
 
#ifdef __linux

#include <endian.h>
#if __BYTE_ORDER == __LITTLE_ENDIAN
#define HOST_IS_LSB_FIRST
#else
#undef HOST_IS_LSB_FIRST
#endif

#endif /* End Linux Section */

#ifdef __sun
#include <sys/isa_defs.h>
#ifdef _LITTLE_ENDIAN
#define HOST_IS_LSB_FIRST
#else
#undef HOST_IS_LSB_FIRST
#endif
#endif /* End Sun Section */


#ifdef __APPLE__
#if __LITTLE_ENDIAN__ == 1
#define HOST_IS_LSB_FIRST
#else
#undef HOST_IS_LSB_FIRST
#endif
#endif

#ifdef WIN32
/** This computer is a little endian machine, macro is not present on big
 * endian machines. 
 */
#define HOST_IS_LSB_FIRST
#endif /* End Windows section */


#ifdef HOST_IS_LSB_FIRST

/** Macro to byte swap buffers in place on little endian computers only */
#define swapBufIfHostLE(p, s, n) _swapBufInPlace(p, s, n)

/** Macro to return a new swapped float */
#define swapFloatIfHostLE(x) _swapFloat(x)

#else

#define swapBufIfHostLE(p, s, n)

#define swapFloatIfHostLE(x) x

#endif

/** Swap whole buffers in place */
void _swapBufInPlace(void* pMem, size_t szEach, size_t numItems);

/** Swap single floats, returns new float */
float swapFloat(float rIn);


#define DAS1ERR     11

/** Basic date-time structure used throughout the Das1 & Das2 utilities 
 *
 * In all das rountines, times are assumed to be UTC.  Since we are
 * dealing with spacecraft far from Earth, local time zones are of no
 * consideration in almost all cases.
 */
typedef struct das_time{

	/** Calendar year number, cannot hold years before 1 AD */
	int year; 
	
	/** Calendar month number, 1 = January */
	int month; 
	
	/** Calender Day of month, starts at 1 */
	int mday; 
	
	/** Integer Day of year, Jan. 1st = 1.  
	 *  This field is <b>output only</b> for most Das1 functions see the
	 *  warning in dt_tnorm() */
	int yday; 
	
	/** Hour of day, range is 0 to 23 */
   int hour;
	
	/** Minute of the hour, range 0 to 59 */
	int minute; 
	
	/** Second of the minute, range 0.0 to 60.0 - epsilon.  
	 * Note, there is no provision for leap seconds in the library.  All
	 * minutes are assumed to have 60 seconds.
	 */
	double second;
	
} das_time_t;


/** Zero out all values in a das_time_t structrue
 *
 * Note, the resulting das_time is an *invalid* time, not a zero point.
 */
void dt_null(das_time_t* pDt);

/** Convert most human-parseable time strings to numeric components 
 * returns 0 on success and non-zero on failure 
 */
int parsetime (const char *string,
  int *year, int *month, int *mday, int *yday,
  int *hour, int *minute, double *second);


/** Convert most human-parseable time strings to numeric components 
 *
 * @param string - the string to convert to a numeric time
 * @param dt     - a pointer to the das_time structure to initialize
 * @returns true on success and false on failure 
 *
 * @memberof das_time_t
 */
bool dt_parsetime(const char* string, das_time_t* dt);


/** Initialize a das_time to the current UTC time.
 * 
 * Note: UTC is not your local time zone.
 */
bool dt_now(das_time_t* pDt);
 

/** Get a das time given days since 1958 and optional milliseconds of 
 * day.  This format is common for many older spacecraft missions 
 * @memberof das_time_t
 */
void dt_from_1958(
	unsigned short int daysSince1958, unsigned int msOfDay, das_time_t* dt
);

/** Get the number of days since 1958-01-01 given a year and day of year */
int past_1958 (int year, int day);

/** Test for time within a time range
 * The the standard exclusive upper bound test.
 *
 * @param begin The beginning time point for the range
 * @param end  The ending time point for the range
 * @param test The test time
 *
 * @returns true if begin <= test and test < end, false otherwise
 * @memberof das_time_t
 */
bool dt_in_range(
	const das_time_t* begin, const das_time_t* end, const das_time_t* test
);

/** Simple helper to copy values from one das time to another 
 *
 * @memberof das_time_t
 */
void dt_copy(das_time_t* pDest, const das_time_t* pSrc);

/** Simple helper to set values in a das time. 
 * 
 * Warning: This function does not cal tnorm, so you *can* use it to set
 *          invalid das times
 *
 * @memberof das_time_t
 */
void dt_set(
	das_time_t* pDt, int year, int month, int mday, int yday, int hour, 
	int minute, double second
);

/** Compare to dastime structures.
 * Since we can't overload the numerical comparison operators in C, you
 * you get this function
 *
 * @param pA a pointer to a das_time_t structure
 * @param pB a pointer to a das_time_t structure
 *
 * @return an integer less than 0 if *pA is less that *pB, 0 if 
 *         *pA is equal to *pB and greater than 0 if *pA is greater
 *         than *pB.
 * @memberof das_time_t
 */
int dt_compare(const das_time_t* pA, const das_time_t* pB);

#define dt_cmp dt_compare

/** Get the difference of two das_time_t structures in seconds.
 *
 * Handle time subtractions in a way that is sensitive to small differences. 
 * Thus, do not go out to tnorm and back.
 * 
 * Time difference in seconds is returned.  This method should be valid
 * as long as you are using the gegorian calendar, but doesn't account
 * for leap seconds.
 *
 * Credit: http://stackoverflow.com/questions/12862226/the-implementation-of-calculating-the-number-of-days-between-2-dates
 *
 * @memberof das_time_t
 */
double dt_diff(const das_time_t* pA, const das_time_t* pB);

/** Print an ISOC standard time string given a das_time_t structure
 * 
 * The output has the format:
 *
 *   yyyy-mm-ddThh:mm:ss[.sssss]
 *
 * Where the number of fractional seconds digits to print is variable and
 * may be set to 0
 *
 * @param sBuf the buffer to hold the output
 * @param nLen the length of the output buffer
 * @param pDt the dastime to print
 * @param nFracSec the number of fractional seconds digits in the output
 *        must be a number from 0 to 15 inclusive
 * @memberof das_time_t
 */
char* dt_isoc(char* sBuf, size_t nLen, const das_time_t* pDt, int nFracSec);

/** Print an ISOD standard time string given a das_time_t structure
 * 
 * The output has the format:
 *
 *   yyyy-dddThh:mm:ss[.sssss]
 *
 * Where the number of fractional seconds digits to print is variable and
 * may be set to 0
 *
 * @param sBuf the buffer to hold the output
 * @param nLen the length of the output buffer
 * @param pDt the dastime to print
 * @param nFracSec the number of fractional seconds digits in the output
 *        must be a number from 0 to 15 inclusive
 * @memberof das_time_t
 */
char* dt_isod(char* sBuf, size_t nLen, const das_time_t* pDt, int nFracSec);


/** Print time a string that provides both day of month and day of year given a
 *  das_time_t structure
 * 
 * The output has the format:
 *
 *   yyyy-mm-dd (ddd) hh:mm:ss[.sssss]
 *
 * Where the number of fractional seconds digits to print is variable and
 * may be set to 0
 *
 * @param sBuf the buffer to hold the output
 * @param nLen the length of the output buffer
 * @param pDt the dastime to print
 * @param nFracSec the number of fractional seconds digits in the output
 *        must be a number from 0 to 15 inclusive
 * @memberof das_time_t
 */
char* dt_dual_str(char* sBuf, size_t nLen, const das_time_t* pDt, int nFracSec);

		
/* Julian Day at January 1, 1958, 12:00:00 UT */
#define EPOCH 2436205


/** Convert time components to double seconds since epoch 
 *
 * Converts time components to a double precision floating point value
 * (seconds since the beginning of 1958, ignoring leap seconds) and normalize
 * inputs.  Note that this floating point value should only be used for
 * "internal" purposes.  (There's no need to propagate yet another time
 * system, plus I want to be able to change/fix these values.)
 * 
 * There is no accommodation for calendar adjustments, for example the
 * transition from Julian to Gregorian calendar, so I wouldn't recommend
 * using these routines for times prior to the 1800's.  Sun IEEE 64-bit
 * floating point preserves millisecond accuracy past the year 3000.
 * For various applications, it may be wise to round to nearest millisecond
 * (or microsecond, etc.) after the value is returned.
 *
 * @note that day-of-year (yday) is an output-only parameter for all
 * of these functions.  To use day-of-year as input, set month to 1
 * and pass day-of-year in mday instead.
 *
 * @warning This function can change it's input values!  The time will
 *          be normalized this could change the input time.
 */
double ttime (
	int *year, int *month, int *mday, int *yday, int *hour,  int *minute, 
	double *second
);
				  
/** Convert time components to double seconds since January 1st 1958
 *
 * converts time components to a double precision floating point value
 * (seconds since the beginning of 1958, ignoring leap seconds) and normalize
 * inputs.  Note that this floating point value should only be used for
 * "internal" purposes.  (There's no need to propagate yet another time
 * system, plus I want to be able to change/fix these values.)
 * 
 * There is no accomodation for calendar adjustments, for example the
 * transition from Julian to Gregorian calendar, so I wouldn't recommend
 * using these routines for times prior to the 1800's.  Sun IEEE 64-bit
 * floating point preserves millisecond accuracy past the year 3000.
 * For various applications, it may be wise to round to nearest millisecond
 * (or microsecond, etc.) after the value is returned.
 *
 * @memberof das_time_t
 */
double dt_ttime(const das_time_t* dt);


/** convert double seconds since epoch to time components.
 *
 * emitt (ttime backwards) converts double precision seconds (since the
 * beginning of 1958, ignoring leap seconds) to date and time components.
 */
void emitt (double tt, int *year, int *month, int *mday, int *yday,
            int *hour, int *minute, double *second);

/** convert double seconds since epoch to time components.
 *
 * emitt (ttime backwards) converts double precision seconds (since the
 * beginning of 1958, ignoring leap seconds) to date and time components.
 * @memberof das_time_t
 */
void dt_emitt (double tt, das_time_t* dt);


/** normalize date and time components
 * NOTE: yday is OUTPUT only.  To add a day to a time, increment
 *       mday as much as needed and then call tnorm.
 */
void tnorm (int *year, int *month, int *mday, int *yday,
            int *hour, int *minute, double *second);

/** Normalize date and time components
 * 
 *  Call this function after manipulating time structure values directly
 *  to insure that any overflow or underflow from various fields are
 *  caried over into to more significant fields.  After calling this function
 *  a das_time sturcture is again normalized into a valid date-time.
 *
 * @warning The das_time_t.yday member is OUTPUT only.  To add a day to
 *  a time, increment mday as much as needed and then call tnorm.
 * @memberof das_time_t
 */
void dt_tnorm(das_time_t* dt);

/** Return a year and day of year given the number of days past 1958 
 *
 * This function is useful for years 1958 to 2096, for years greater
 * than 2096 it runs off the end of an internal buffer.
 * 
 * @param [out] pYear a pointer to an integer to receive the 4 digit year
 *              number
 * @param [out] pDoy a pointer to an integer to recieve the day of year
 *              number (1 = Jan. 1st)
 *
 * @param [in] days_since_1958 The number of days since Jan. 1st, 1958
 */
void yrdy1958(int* pYear, int* pDoy, int days_since_1958);

/** Return the hours, minutes and seconds of a day given then number of 
 * milliseconds since the start of the day
 *
 * @param [out] pHour a pointer to an integer to receive the hour of 
 *              the day (midnight = 0)
 *
 * @param [out] pMin a pointer to an integer to receive the minute of the
 *              hour.
 *
 * @param [out] pSec a pointer to a float to receive the seconds of the
 *              minute.  Result is (of course) accurate to milliseconds.
 *
 * @param [in] ms_of_day the milliseconds of day value.
 */
void ms2hms(int* pHour, int* pMin, float* pSec, double ms_of_day);


/* return Julian Day number given year, month, and day */
int jday (int year, int month, int day);

/* generic print-message-and-exit-with-error */
void fail (const char *message);

/** Read a Tagged Das 1 packet from stdin
 *
 * @param ph 8-byte packet header
 * @param data buffer, 
 * @param max number of bytes to read
 * @returns number of bytes read 
 */
int getpkt(char *ph, void *data, int max);

/** Read a Tagged Das 1 packet from a file object 
 *
 * @param [in] fin input file pointer
 * @param [out] ph pointer to buffer to receive the 8-byte packet header
 * @param [out] data buffer, 
 * @param [in] max number of bytes to read
 * @returns number of bytes read 
 */
int fgetpkt(FILE* fin, char* ph, void* data, int max);


/** Write das packet to stdout
 * @param ph 8-byte packet header, ex: ":b0:78F2" 
 * @param data buffer
 * @param bytes number of bytes to write (why this isn't taken from the packet
 *        header I don't know)
 * @returns 1 on success and 0 on failure 
 */
int putpkt (const char *ph, const void *data, int bytes);

#define _das_h_
#endif
