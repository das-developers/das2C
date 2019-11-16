/* Copyright (C) 1997-2017 Larry Granroth <larry-granroth@uiowa.edu> 
 *                         Chris Piker <chris-piker@uiowa.edu>
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


/** @file das1.h Das 1 Compatability Utilities */

#ifndef _das1_h_
#define _das1_h_

#include <stdio.h>
#include <das2/util.h>
#include <das2/value.h>

#ifdef __cplusplus
extern "C" {
#endif

#ifdef HOST_IS_LSB_FIRST

/** Macro to byte swap buffers in place on little endian computers only */
#define swapBufIfHostLE(p, s, n) _swapBufInPlace(p, s, n)

/** Macro to return a new swapped float */
#define swapFloatIfHostLE(x) swapFloat(x)

#else

#define swapBufIfHostLE(p, s, n)

#define swapFloatIfHostLE(x) x

#endif

/** Swap whole buffers in place */
DAS_API void _swapBufInPlace(void* pMem, size_t szEach, size_t numItems);

/** Swap single floats, returns new float */
DAS_API float swapFloat(float rIn);

	
#define D1ERR     10

/** Convert most human-parseable time strings to numeric components 
 * returns 0 on success and non-zero on failure 
 */
DAS_API int parsetime (const char *string,
  int *year, int *month, int *mday, int *yday,
  int *hour, int *minute, double *second
);


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
DAS_API double ttime (
	int *year, int *month, int *mday, int *yday, int *hour,  int *minute, 
	double *second
);

/** convert double seconds since epoch to time components.
 *
 * emitt (ttime backwards) converts double precision seconds (since the
 * beginning of 1958, ignoring leap seconds) to date and time components.
 */
DAS_API void emitt (
	double tt, int *year, int *month, int *mday, int *yday,
   int *hour, int *minute, double *second
);

/** normalize date and time components
 * NOTE: yday is OUTPUT only.  To add a day to a time, increment
 *       mday as much as needed and then call tnorm.
 */
DAS_API void tnorm (
	int *year, int *month, int *mday, int *yday, int *hour, int *minute,
	double *second
);


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

/** Get the number of days since 1958-01-01 given a year and day of year */
int past_1958 (int year, int day);


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

/* generic print-message-and-exit-with-error */
void fail (const char *message);

/** Read a Tagged Das 1 packet from stdin
 *
 * @param ph 8-byte packet header
 * @param data buffer, 
 * @param max number of bytes to read
 * @returns number of bytes read 
 */
int getpkt(char *ph, byte *data, int max);

/** Read a Tagged Das 1 packet from a file object 
 *
 * @param [in] fin input file pointer
 * @param [out] ph pointer to buffer to receive the 8-byte packet header
 * @param [out] data buffer, 
 * @param [in] max number of bytes to read
 * @returns number of bytes read 
 */
int fgetpkt(FILE* fin, char* ph, byte* data, int max);


/** Write das packet to stdout
 * @param ph 8-byte packet header, ex: ":b0:78F2" 
 * @param data buffer
 * @param bytes number of bytes to write (why this isn't taken from the packet
 *        header I don't know)
 * @returns 1 on success and 0 on failure 
 */
int putpkt (const char *ph, const byte *data, const int bytes);

#ifdef __cplusplus
}
#endif

#endif /* _das1_h_ */
