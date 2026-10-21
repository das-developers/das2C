/* Copyright (C) 1997-2017 Larry Granroth <larry-granroth@uiowa.edu> 
 *                         Chris Piker <chris-piker@uiowa.edu>
 *                         
 * This file is part of das2C, the Core Das2 C Library.
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


/** @file time.h Das Time Utilities */

/** @addtogroup values
 * @{
 */

#ifndef _das_time_h_
#define _das_time_h_

#include <das3/defs.h>

#ifdef __cplusplus
extern "C" {
#endif

/** Basic date-time structure used throughout the Das1 & Das2 utilities 
 *
 * In all das routines, times are assumed to be UTC.  Since we are
 * dealing with spacecraft far from Earth, local time zones are of no
 * consideration in almost all cases.
 * 
 * On 64-bit machines sizeof(das_time) == 24+8 = 32 bytes.
 */
typedef struct das_time_t{

   /** Calendar year number, cannot hold years before 1 AD */
   int year; 
   
   /** Calendar month number, 1 = January */
   int month; 
   
   /** Calendar Day of month, starts at 1 */
   int mday; 
   
   /** Integer Day of year, Jan. 1st = 1.  
    *  This field is <b>output only</b> for most Das1 functions see the
    *  warning in dt_tnorm() */
   int yday; 
   
   /** Hour of day, range is 0 to 23 */
   int hour;
   
   /** Minute of the hour, range 0 to 59 */
   int minute; 
   
   /** Second of the minute.  Normalization functions, dt_ttime() or dt_emit()
    * assume that seconds > 60.0 are not normalized.  Thus datetimes returned
    * from TT2000 conversion should not be passed to dt_ttime() or dt_emitt().
    */
   double second;
   
} das_time;

/** @} */


/* A version of above that would only use 16 bytes instead of 32 ....
   to enable and test later 
   
typedef struct das_time_t{
   / ** Calendar month number, 1 = January * /
   int8_t month; 
   
   / ** Calendar Day of month, starts at 1 * /
   int8_t mday; 

   / ** Hour of day, range is 0 to 23 * /
   int8_t hour;
   
   / ** Minute of the hour, range 0 to 59 * /
   int8_t minute; 

   / ** Calendar year number, cannot hold years before 1 AD * /
   int16_t year; 

   / ** Integer Day of year, Jan. 1st = 1.  
    *  This field is <b>output only</b> for most Das1 functions see the
    *  warning in dt_tnorm() * /
   int16_t yday; 
   
   / ** Second of the minute, range 0.0 to 60.0 - epsilon.  
    * Note, there is no provision for leap seconds in the library.  All
    * minutes are assumed to have 60 seconds.
    * /
   double second;
   
} das_time;
*/

#define DAS_TIME_NULL {0, 0, 0, 0, 0, 0, 0.0}


/** Zero out all values in a das_time structure
 *
 * Note, the resulting das_time is an *invalid* time, not a zero point.
 * @memberof das_time
 */
DAS_API void dt_null(das_time* pDt);


/** Convert most human-parseable time strings to numeric components 
 *
 * @param pDt   The das_time structure to initialize
 * @param sTime The string to convert to a numeric time
 * @returns true on success and false on failure 
 *
 * @memberof das_time
 */
DAS_API bool dt_parsetime(das_time* pDt, const char* sTime);


/** Initialize a das_time to the current UTC time.
 * 
 * Note: UTC is not your local time zone.
 * @memberof das_time
 */
DAS_API bool dt_now(das_time* pDt);
 

/** Get a das time given days since 1958 and optional milliseconds of 
 * day.  This format is common for many older spacecraft missions 
 *
 * @param pDt The das_time structure to initialize
 * @param daysSince1958 Whole days since 1958-01-01
 * @param msOfDay Milliseconds into the day
 *
 * @memberof das_time
 */
DAS_API void dt_from_1958(
	das_time* pDt, unsigned short int daysSince1958, unsigned int msOfDay
);

/** Convert a das time to integer nanoseconds since 1970-01-01
 *  
 * 
 * @param dt a das time
 * @param pDays days since Jan. 1st 1970 at midnight
 * @param pFrac fraction of a day.
 * @memberof das_time
 */
DAS_API int64_t dt_nano_1970(const das_time* dt);

/** Test for time within a time range
 * The the standard exclusive upper bound test.
 *
 * @param begin The beginning time point for the range
 * @param end  The ending time point for the range
 * @param test The test time
 *
 * @returns true if begin <= test and test < end, false otherwise
 * @memberof das_time
 */
DAS_API bool dt_in_range(
	const das_time* begin, const das_time* end, const das_time* test
);

/** Simple helper to copy values from one das time to another 
 *
 * @memberof das_time
 */
DAS_API void dt_copy(das_time* pDest, const das_time* pSrc);

/** Simple helper to set values in a das time. 
 * 
 * Warning: This function does not cal tnorm, so you *can* use it to set
 *          invalid das times
 *
 * @memberof das_time
 */
DAS_API void dt_set(
	das_time* pDt, int year, int month, int mday, int yday, int hour, 
	int minute, double second
);

/** Compare to dastime structures.
 * Since we can't overload the numerical comparison operators in C, you
 * you get this function
 *
 * @param pA a pointer to a das_time structure
 * @param pB a pointer to a das_time structure
 *
 * @return an integer less than 0 if *pA is less that *pB, 0 if 
 *         *pA is equal to *pB and greater than 0 if *pA is greater
 *         than *pB.
 * @memberof das_time
 */
DAS_API int dt_compare(const das_time* pA, const das_time* pB);


/** Get the difference of two das_time structures in seconds.
 *
 * Handle time subtractions in a way that is sensitive to small differences. 
 * Thus, do not go out to tnorm and back.
 * 
 * Time difference in seconds is returned.  This method should be valid
 * as long as you are using the gregorian calendar, but doesn't account
 * for leap seconds.
 *
 * Credit: http://stackoverflow.com/questions/12862226/the-implementation-of-calculating-the-number-of-days-between-2-dates
 *
 * @returns Time A - Time B in seconds.
 * @memberof das_time
 */
DAS_API double dt_diff(const das_time* pA, const das_time* pB);

/** Print an ISOC standard time string given a das_time structure
 * 
 * The output has the format:
 *
 *   yyyy-mm-ddThh:mm:ss[.sssss]
 *
 * Where the number of fractional seconds digits to print is variable and
 * may be set to 0
 *
 * @param pDt the dastime to print
 * @param sBuf the buffer to hold the output
 * @param uLen the length of the output buffer, including the NUL
 * @param nFracSec the number of fractional seconds digits in the output,
 *        0 to 9, larger values are treated as 9
 * @returns sBuf, or NULL if the whole string did not fit.  sBuf is always
 *        NUL terminated when uLen > 0.
 * @memberof das_time
 */
DAS_API char* dt_isoc(const das_time* pDt, char* sBuf, size_t uLen, int nFracSec);

/** Print an ISOD standard time string given a das_time structure
 * 
 * The output has the format:
 *
 *   yyyy-dddThh:mm:ss[.sssss]
 *
 * Where the number of fractional seconds digits to print is variable and
 * may be set to 0
 *
 * @param pDt the dastime to print
 * @param sBuf the buffer to hold the output
 * @param uLen the length of the output buffer, including the NUL
 * @param nFracSec the number of fractional seconds digits in the output,
 *        0 to 9, larger values are treated as 9
 * @returns sBuf, or NULL if the whole string did not fit.  sBuf is always
 *        NUL terminated when uLen > 0.
 * @memberof das_time
 */
DAS_API char* dt_isod(const das_time* pDt, char* sBuf, size_t uLen, int nFracSec);


/** Print time a string that provides both day of month and day of year given a
 *  das_time structure
 * 
 * The output has the format:
 *
 *   yyyy-mm-dd (ddd) hh:mm:ss[.sssss]
 *
 * Where the number of fractional seconds digits to print is variable and
 * may be set to 0
 *
 * @param pDt the dastime to print
 * @param sBuf the buffer to hold the output
 * @param uLen the length of the output buffer, including the NUL
 * @param nFracSec the number of fractional seconds digits in the output,
 *        0 to 9, larger values are treated as 9
 * @returns sBuf, or NULL if the whole string did not fit.  sBuf is always
 *        NUL terminated when uLen > 0.
 * @memberof das_time
 */
DAS_API char* dt_dual_str(const das_time* pDt, char* sBuf, size_t uLen, int nFracSec);

		
/* Julian Day at January 1, 1958, 12:00:00 UT */
#define EPOCH 2436205

				  
/** Convert time components to double seconds since January 1st 1958
 *
 * converts time components to a double precision floating point value
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
 * @memberof das_time
 */
DAS_API double dt_ttime(const das_time* dt);

/** convert double seconds since epoch to time components.
 *
 * emitt (ttime backwards) converts double precision seconds (since the
 * beginning of 1958, ignoring leap seconds) to date and time components.
 *
 * @param pDt The das_time structure to initialize
 * @param tt  Seconds since 1958-01-01, ignoring leap seconds
 *
 * @memberof das_time
 */
DAS_API void dt_emitt(das_time* pDt, double tt);

/* Returned by dt_to_tt2k() for fields outside the TT2000 range or with
   negative members.  Also defined in tt2000.c, which is not installed; the
   two must stay the same value. */
#ifndef ILLEGAL_TT2000_VALUE
#define ILLEGAL_TT2000_VALUE (-9223372036854775805LL)
#endif

/** Convert a time structure to a TT2000 time 
 * 
 * @returns nanoseconds since 2000-01-01T11:58:55.816 (TT2000), or
 *          ILLEGAL_TT2000_VALUE if the fields can't be converted.  No error
 *          is raised, compare the result.
 *
 * @memberof das_time
 */
DAS_API int64_t dt_to_tt2k(const das_time* dt);

/** Convert a TT2000 time to a time structure
 *
 * During a leap second the result's second field is 60.x, which
 * dt_to_tt2k() and dt_isoc() handle but dt_tnorm() does not!
 *
 * @memberof das_time
 */
DAS_API void dt_from_tt2k(das_time* dt, int64_t nTime);

/** Convert broken out UTC fields to TT2000, without floating point seconds
 *
 * For sources that already hold integer sub-second counts.  A das_time keeps
 * seconds as a double, so going through dt_to_tt2k() would pack them into a
 * double just to split them again.
 *
 * nanosec may pass a second and carries leap second aware: second 59 plus
 * 1500000000 ns at 2016-12-31T23:59 is the leap second 23:59:60.5.  Month 1
 * with mday as the day of year also works.
 *
 * @param year   four digit year, 1707 to 2292 (the TT2000 range)
 * @param month  1 to 12
 * @param mday   day of month, or day of year when month is 1
 * @param hour   0 to 23
 * @param minute 0 to 59
 * @param second 0 to 59, or 60 during a leap second
 * @param nanosec nanoseconds into the second, a caller with separate milli,
 *        micro and nano counts passes ms*1000000 + us*1000 + ns
 * @param pTT    receives the TT2000 value
 *
 * @returns DAS_OKAY, or DASERR_TIME for negative fields or a time outside
 *        the TT2000 range, in which case pTT is not written.
 */
DAS_API DasErrCode tt2k_from_parts(
	int year, int month, int mday, int hour, int minute, int second,
	int32_t nanosec, int64_t* pTT
);

/** Break a TT2000 time into UTC fields, without floating point seconds
 *
 * The inverse of tt2k_from_parts().  During a leap second the second field
 * is 60.  Any output pointer may be NULL to skip that field.
 *
 * @param nTT nanoseconds since 2000-01-01T11:58:55.816 (TT2000)
 * @param pYear, pMonth, pMday, pHour, pMinute, pSecond receive the calendar
 *        fields, month and mday 1 based
 * @param pNanosec receives nanoseconds into the second, 0 to 999999999
 */
DAS_API void tt2k_to_parts(
	int64_t nTT, int* pYear, int* pMonth, int* pMday, int* pHour,
	int* pMinute, int* pSecond, int32_t* pNanosec
);

/** Normalize date and time components
 * 
 *  Call this function after manipulating time structure values directly
 *  to insure that any overflow or underflow from various fields are
 *  carried over into to more significant fields.  After calling this function
 *  a das_time structure is again normalized into a valid date-time.
 *
 * @warning The das_time.yday member is OUTPUT only.  To add a day to
 *  a time, increment mday as much as needed and then call tnorm.
 *
 * @warning Not leap second aware.  A second of 60.x, as dt_from_tt2k() and
 *  Units_convertToDt() return during a leap second, is rolled up into the
 *  next minute.  Don't normalize a das_time that may hold one.
 * @memberof das_time
 */
DAS_API void dt_tnorm(das_time* dt);

/* return Julian Day number given year, month, and day, not exported no purpose */
int jday (int year, int month, int day);

#ifdef __cplusplus
}
#endif

#endif /* _das_time_h_ */
