/* Copyright (C) 1997 Larry Granroth <larry-granroth@uiowa.edu> 
 *               2012-2026 Chris Piker <chris-piker@uiowa.edu>
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


/** @file time.h Das Time Utilities */

/** @addtogroup values
 * @{
 */

#ifndef _das_time_h_
#define _das_time_h_

#include <das3/defs.h>
#include <das3/units.h>   /* the Units_ time conversions take DasUnits */

#ifdef __cplusplus
extern "C" {
#endif

/** Basic date-time structure used throughout the Das1 & Das2 utilities 
 *
 * In all das routines, times are assumed to be UTC.  Since we are
 * dealing with spacecraft far from Earth, local time zones are of no
 * consideration in almost all cases.
 * 
 * On 64-bit machines sizeof(DasTime) == 24+8 = 32 bytes.
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
    *  warning in DasTime_normalize() */
   int yday; 
   
   /** Hour of day, range is 0 to 23 */
   int hour;
   
   /** Minute of the hour, range 0 to 59 */
   int minute; 
   
   /** Second of the minute.  Normalization functions, DasTime_toSec1958() or dt_emit()
    * assume that seconds > 60.0 are not normalized.  Thus datetimes returned
    * from TT2000 conversion should not be passed to DasTime_toSec1958() or DasTime_fromSec1958().
    */
   double second;
   
} DasTime;

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
    *  warning in DasTime_normalize() * /
   int16_t yday; 
   
   / ** Second of the minute, range 0.0 to 60.0 - epsilon.  
    * Note, there is no provision for leap seconds in the library.  All
    * minutes are assumed to have 60 seconds.
    * /
   double second;
   
} DasTime;
*/

#define DASTIME_NULL {0, 0, 0, 0, 0, 0, 0.0}


/** Zero out all values in a DasTime structure
 *
 * Note, the resulting DasTime is an *invalid* time, not a zero point.
 * @memberof DasTime
 */
DAS_API void DasTime_clear(DasTime* pDt);


/** Convert most human-parseable time strings to numeric components 
 *
 * @param pDt   The DasTime structure to initialize
 * @param sTime The string to convert to a numeric time
 * @returns true on success and false on failure 
 *
 * @memberof DasTime
 */
DAS_API bool DasTime_fromStr(DasTime* pDt, const char* sTime);

/** Parse a time string as the span it denotes.
 *
 * A time written to limited precision names an interval, not an instant:
 * "2026-01-02" is the whole of January 2nd, "2026-01-02T14" the hour from
 * 14:00 to 15:00, "2026" the year.  This reads the string the way
 * DasTime_fromStr() does and also sets pEnd to the start of the next unit
 * of the least significant field given.  Day of year forms, "2026-002",
 * step the day.  A string with a fraction of a second, "T14:30:15.5",
 * denotes no natural span and is refused, as is anything DasTime_fromStr()
 * refuses.
 *
 * @param sTime An ISO 8601 date or date and time, year through whole
 *        seconds, with or without a trailing Z.
 * @param pBeg Receives the start of the span.  May be NULL.
 * @param pEnd Receives the end of the span, exclusive.  May be NULL.
 * @returns true if the string was understood, false otherwise with both
 *        outputs untouched.
 */
DAS_API bool DasTime_strToSpan(const char* sTime, DasTime* pBeg, DasTime* pEnd);


/** Initialize a DasTime to the current UTC time.
 * 
 * Note: UTC is not your local time zone.
 * @memberof DasTime
 */
DAS_API bool DasTime_now(DasTime* pDt);
 

/** Get a das time given days since 1958 and optional milliseconds of 
 * day.  This format is common for many older spacecraft missions 
 *
 * @param pDt The DasTime structure to initialize
 * @param daysSince1958 Whole days since 1958-01-01
 * @param msOfDay Milliseconds into the day
 *
 * @memberof DasTime
 */
DAS_API void DasTime_fromDays1958(
	DasTime* pDt, unsigned short int daysSince1958, unsigned int msOfDay
);

/** Convert a das time to integer nanoseconds since 1970-01-01
 *  
 * 
 * @param dt a das time
 * @memberof DasTime
 */
DAS_API int64_t DasTime_toNs1970(const DasTime* dt);

/** Test for time within a time range
 * The the standard exclusive upper bound test.
 *
 * @param test The test time
 * @param begin The beginning time point for the range
 * @param end  The ending time point for the range
 *
 * @returns true if begin <= test and test < end, false otherwise
 * @memberof DasTime
 */
DAS_API bool DasTime_isInRange(
	const DasTime* test, const DasTime* begin, const DasTime* end
);

/** Simple helper to set values in a das time. 
 * 
 * Warning: This function does not cal tnorm, so you *can* use it to set
 *          invalid das times
 *
 * @memberof DasTime
 */
DAS_API void DasTime_set(
	DasTime* pDt, int year, int month, int mday, int yday, int hour, 
	int minute, double second
);

/** Compare to dastime structures.
 * Since we can't overload the numerical comparison operators in C, you
 * you get this function
 *
 * @param pA a pointer to a DasTime structure
 * @param pB a pointer to a DasTime structure
 *
 * @return an integer less than 0 if *pA is less that *pB, 0 if 
 *         *pA is equal to *pB and greater than 0 if *pA is greater
 *         than *pB.
 * @memberof DasTime
 */
DAS_API int DasTime_compare(const DasTime* pA, const DasTime* pB);


/** Get the difference of two DasTime structures in seconds.
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
 * @memberof DasTime
 */
DAS_API double DasTime_diff(const DasTime* pA, const DasTime* pB);

/** Print an ISOC standard time string given a DasTime structure
 * 
 * The output has the format:
 *
 *   yyyy-mm-ddThh:mm:ss[.sssss]
 *
 * Where the number of fractional seconds digits to print is variable and
 * may be set to 0
 *
 * @param pDt the dastime to print
 * @param nFracSec the number of fractional seconds digits in the output,
 *        0 to 9, larger values are treated as 9
 * @param sBuf the buffer to hold the output
 * @param uLen the length of the output buffer, including the NUL
 * @returns sBuf, or NULL if the whole string did not fit.  sBuf is always
 *        NUL terminated when uLen > 0.
 * @memberof DasTime
 */
DAS_API char* DasTime_toIsoC(const DasTime* pDt, int nFracSec, char* sBuf, size_t uLen);

/** Print an ISOD standard time string given a DasTime structure
 * 
 * The output has the format:
 *
 *   yyyy-dddThh:mm:ss[.sssss]
 *
 * Where the number of fractional seconds digits to print is variable and
 * may be set to 0
 *
 * @param pDt the dastime to print
 * @param nFracSec the number of fractional seconds digits in the output,
 *        0 to 9, larger values are treated as 9
 * @param sBuf the buffer to hold the output
 * @param uLen the length of the output buffer, including the NUL
 * @returns sBuf, or NULL if the whole string did not fit.  sBuf is always
 *        NUL terminated when uLen > 0.
 * @memberof DasTime
 */
DAS_API char* DasTime_toIsoD(const DasTime* pDt, int nFracSec, char* sBuf, size_t uLen);


/** Print time a string that provides both day of month and day of year given a
 *  DasTime structure
 * 
 * The output has the format:
 *
 *   yyyy-mm-dd (ddd) hh:mm:ss[.sssss]
 *
 * Where the number of fractional seconds digits to print is variable and
 * may be set to 0
 *
 * @param pDt the dastime to print
 * @param nFracSec the number of fractional seconds digits in the output,
 *        0 to 9, larger values are treated as 9
 * @param sBuf the buffer to hold the output
 * @param uLen the length of the output buffer, including the NUL
 * @returns sBuf, or NULL if the whole string did not fit.  sBuf is always
 *        NUL terminated when uLen > 0.
 * @memberof DasTime
 */
DAS_API char* DasTime_toIsoDual(const DasTime* pDt, int nFracSec, char* sBuf, size_t uLen);

				  
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
 * @memberof DasTime
 */
DAS_API double DasTime_toSec1958(const DasTime* dt);

/** convert double seconds since epoch to time components.
 *
 * emitt (ttime backwards) converts double precision seconds (since the
 * beginning of 1958, ignoring leap seconds) to date and time components.
 *
 * @param pDt The DasTime structure to initialize
 * @param tt  Seconds since 1958-01-01, ignoring leap seconds
 *
 * @memberof DasTime
 */
DAS_API void DasTime_fromSec1958(DasTime* pDt, double tt);

/* Returned by DasTime_toTt2k() for fields outside the TT2000 range or with
   negative members.  Also defined in tt2000.c, which is not installed; the
   two must stay the same value. */
#ifndef DASTIME_TT2K_ILLEGAL
#define DASTIME_TT2K_ILLEGAL (-9223372036854775805LL)
#endif

/** Convert a time structure to a TT2000 time 
 * 
 * @returns nanoseconds since 2000-01-01T11:58:55.816 (TT2000), or
 *          DASTIME_TT2K_ILLEGAL if the fields can't be converted.  No error
 *          is raised, compare the result.
 *
 * @memberof DasTime
 */
DAS_API int64_t DasTime_toTt2k(const DasTime* dt);

/** Convert a TT2000 time to a time structure
 *
 * During a leap second the result's second field is 60.x, which
 * DasTime_toTt2k() and DasTime_toIsoC() handle but DasTime_normalize() does not!
 *
 * @memberof DasTime
 */
DAS_API void DasTime_fromTt2k(DasTime* dt, int64_t nTime);

/** Convert broken out UTC fields to TT2000, without floating point seconds
 *
 * For sources that already hold integer sub-second counts.  A DasTime keeps
 * seconds as a double, so going through DasTime_toTt2k() would pack them into a
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
DAS_API das_errcode DasTime_utcToTt2k(
	int year, int month, int mday, int hour, int minute, int second,
	int32_t nanosec, int64_t* pTT
);

/** Break a TT2000 time into UTC fields, without floating point seconds
 *
 * The inverse of DasTime_utcToTt2k().  During a leap second the second field
 * is 60.  Any output pointer may be NULL to skip that field.
 *
 * @param nTT nanoseconds since 2000-01-01T11:58:55.816 (TT2000)
 * @param pYear, pMonth, pMday, pHour, pMinute, pSecond receive the calendar
 *        fields, month and mday 1 based
 * @param pNanosec receives nanoseconds into the second, 0 to 999999999
 */
DAS_API void DasTime_tt2kToUtc(
	int64_t nTT, int* pYear, int* pMonth, int* pMday, int* pHour,
	int* pMinute, int* pSecond, int32_t* pNanosec
);

/** Normalize date and time components
 * 
 *  Call this function after manipulating time structure values directly
 *  to insure that any overflow or underflow from various fields are
 *  carried over into to more significant fields.  After calling this function
 *  a DasTime structure is again normalized into a valid date-time.
 *
 * @warning The DasTime.yday member is OUTPUT only.  To add a day to
 *  a time, increment mday as much as needed and then call tnorm.
 *
 * @warning Not leap second aware.  A second of 60.x, as DasTime_fromTt2k() and
 *  DasTime_fromPoint() return during a leap second, is rolled up into the
 *  next minute.  Don't normalize a DasTime that may hold one.
 * @memberof DasTime
 */
DAS_API void DasTime_normalize(DasTime* dt);

/** Get the Julian Day Number of a calendar date
 *
 * The Julian Day Number is the integer count of days used in astronomy,
 * so 2000-01-01 gives 2451545.  Dates use the Gregorian calendar, extended
 * backwards before its adoption in 1582, for years 1 and later.  Historical
 * dates recorded in the Julian calendar need converting first.
 *
 * The day is counted straight through, so month 1 with a day of year works
 * the same way it does for DasTime.
 *
 * @param year  The full year, such as 2026
 * @param month 1 to 12
 * @param day   Day of month, or day of year when month is 1
 * @returns The Julian Day Number
 */
DAS_API int DasTime_utcToJdn (int year, int month, int day);

/** Convert a value in time offset units to a calendar representation
 *
 * @param[out] pDt a pointer to a DasTime structure to receive the broken 
 *            down time.
 * 
 * @param[in] value the double value representing time from the epoch in some
 *            scale.
 *
 * @param[in] epoch_units Unit string
 */
DAS_API das_errcode DasTime_fromPoint(DasTime* pDt, double value, DasUnits epoch_units);

/** Convert a calendar representation of a time to value in time offset units
 * 
 * @param pDt the calendar time object from which to derive the value
 * @param epoch_units The units associated with the return value
 * @return the value as a floating point offset from the epoch associated with 
 *         epoch_units, or DASVAL_FILL on an error
 */
DAS_API double DasTime_toPoint(const DasTime* pDt, DasUnits epoch_units);

/** Get seconds since midnight for some value of an epoch time unit
 * @param rVal the value of the epoch time
 * @param epoch_units so type of epoch time unit.
 * @returns the number of floating point second since midnight
 */
DAS_API double DasTime_pointToSod( double rVal, DasUnits epoch_units );

/* Get the Julian day for the Datum (double,unit) */
DAS_API int DasTime_pointToJdn( double timeDouble, DasUnits epoch_units );

#ifdef __cplusplus
}
#endif

#endif /* _das_time_h_ */
