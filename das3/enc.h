/* Copyright (C) 2015-2026 Chris Piker <chris-piker@uiowa.edu>
 *
 * This file is part of das2C, the Core Das C Library.
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


/** @file enc.h  Defines storage and access methods for values in a 
 * das stream
 */

#ifndef _das_enc_h_
#define _das_enc_h_

#include <das3/util.h>
#include <das3/units.h>
#include <das3/buffer.h>

#ifdef __cplusplus
extern "C" {
#endif


/* Most Significant byte First (big-endian) IEEE-754 reals */
#define DAS2DT_BE_REAL 0x0001

/* Most Significant byte last (little-endian) IEEE-754 reals */
#define DAS2DT_LE_REAL 0x0002

#ifdef HOST_IS_LSB_FIRST
#define DAS2DT_HOST_REAL 0x0002
#else
#define DAS2DT_HOST_REAL 0x0001
#endif

/* A real number formatted in some number of characters.
 * Conventionally there are a whitespace characters to improve readability,
 * but this is not required.  The formatted number should be parsable by 
 * scanf in C, readf in IDL, or Double.parseDouble in java.
 */
#define DAS2DT_ASCII 0x0003

/* A date-time formatted as an ASCII ISO-8601 string.
 * Actually any time that is parseable by the parsetime routine will work
 * Generally if a human can read it, it's parseable. For example, 
 * YYYY-MM-DDThh:mm:ss.mmmZ.
 */
#define DAS2DT_TIME 0x0004

#define DASENC_FMT_LEN  64
#define DASENC_TYPE_LEN 32


/** @addtogroup codec
 * @{
 */

/** Reading and writing values to buffers
 *
 * Values in a Das Stream have can be encoded in a variety of ways.  Values
 * can be big-endian reals, little-endian floats, ASCII strings, etc.  In
 * to it's basic category values can be represented on the stream using a 
 * variable number of bytes.   It is the job of this class to handle 
 * encoding and decoding values from a Das2 Stream.  
 * 
 * This class handles syntax not semantics.  The type of measurement represented
 * by a value depends on it's ::das_units  Any double value can be output as in 
 * any given encoding.  The functions for this class are happy to output
 * amplitudes at time strings.  It's up to other classes to see that proper
 * encodings are used. 
 * 
 * Encodings are made by new_DasEnc(), new_DasEnc_str() or DasEnc_copy()
 * and freed with del_DasEnc().
 *
 * @see ::das_units
 * @see ::DasPlane
 * @class DasEnc
 */
typedef struct das_encoding DasEnc;

/** The encoding category: DAS2DT_BE_REAL, DAS2DT_LE_REAL, DAS2DT_ASCII or
 * DAS2DT_TIME
 * @memberof DasEnc */
DAS_API unsigned int DasEnc_cat(const DasEnc* pThis);

/** The width in bytes of each encoded value
 *
 * REAL encodings are 4 or 8 bytes wide.  ASCII and TIME widths count one
 * byte for the separator that follows each value, see DasEnc_write().
 * @memberof DasEnc */
DAS_API unsigned int DasEnc_width(const DasEnc* pThis);


/** Make a new data encoder/decoder
 * 
 * @param nCat The basic encoding category one of:
 *        - DAS2DT_BE_REAL
 *        - DAS2DT_LE_REAL
 *        - DAS2DT_HOST_REAL
 *        - DAS2DT_ASCII
 *        - DAS2DT_TIME
 *
 * @param nWidth The width in bytes of each encoded value, ASCII and TIME
 *        types can have arbitrary widths, REAL can be 4 or 8.  For integers
 *        or half-floats use a das3 stream and DasCodec instead.
 *
 * @param sFmt If not NULL then this sprintf style string will be use to 
 *        format the values for output.  If the encoding is just used for
 *        input then a format string is not required.  If the encoding is
 *        used for output and no format string has been set the library 
 *        will assign a reasonable default.
 *
 * @returns A new DasEnc structure allocated on the heap, free it with
 *          del_DasEnc().
 * @memberof DasEnc
 */
DAS_API DasEnc* new_DasEnc(int nCat, int nWidth, const char* sFmt);

/** Free an encoding made by new_DasEnc(), new_DasEnc_str() or DasEnc_copy()
 *
 * @param pThis the encoding, NULL is ignored
 * @memberof DasEnc
 */
DAS_API void del_DasEnc(DasEnc* pThis);

/** Are this encoding's values text, ASCII numbers or time strings
 *
 * @memberof DasEnc
 */
DAS_API bool DasEnc_isUtf8(const DasEnc* pThis);

/** Create a new encoding based on the encoding type string.
 *
 * @param sType the @e type string from a Das2 Stream plane definition.
 *        This should be one of, <b>sun_real8</b>, <b>little_endian_real8</b>,
 *        <b>sun_real4</b>, <b>little_endian_real4</b>, <b>ascii</b>XX, or
 *        <b>time</b>XX.  Here <b>XX</b> is a field width.
 * 
 * @returns a new DasEnc allocated on the heap, free it with del_DasEnc().
 *
 * @memberof DasEnc
 */
DAS_API DasEnc* new_DasEnc_str(const char* sType);


/** Copy an encoding to the heap, free the copy with del_DasEnc()
 * @memberof DasEnc */
DAS_API DasEnc* DasEnc_copy(const DasEnc* pThis);


/** Set the output format to be used when converting internal binary
 * values to ASCII strings.
 *
 * ASCII values are written to Das2 streams with a single space character
 * between each formatted value.  The last value of the the last plane is
 * followed by a new-line character instead of a space character.  Using this
 * function will change the value format, but will not alter the separator
 * character or the end-of-line characters.  (Sorry, no CSV output formats.)
 *
 * Use of this function is not required.  ASCII values will receive a
 * default format.
 *
 * @param pThis The output plane in question
 *
 * @param sValFmt a printf style format string.  Typical strings for general
 *        data values would be: '%9.2e', '%+13.6e'.  In general strings 
 *        such as '%13.3f' should @b not be used as these aren't guaranteed
 *        to have a fix output width and your value strings may be truncated.
 *
 * @param nFmtWidth the number of output characters indicated by this format.
 *        This is just the width of the formatted value, not including any
 *        field separators.
 *
 * @memberof DasEnc
 */
DAS_API void DasEnc_setAsciiFormat(DasEnc* pThis, const char* sValFmt, 
                           int nFmtWidth);


/** Set the output format to be used when converting binary time values to
 * to ASCII strings.
 *
 * ASCII Time values are always encoded for output using the following C-call:
 *
 * @code
 * sprintf(buf, sValFmt, year, month, dayofmonth, hour, min, sec);
 * @endcode
 *
 * The seconds field is double precision.  Note that you do <b>not</b> have to
 * convert all the fields in your output.  In this case the output time will
 * be a truncated time.
 *		 
 * ASCII values are written to Das2 streams with a single space character
 * between each formatted value.  The last value of the the last plane is
 * followed by a new-line character instead of a space character.  Using this
 * function will change the value format, but will not alter the separator
 * character or the end-of-line characters.  Sorry, no CSV output formats.
 * 
 * Use of this function is not required.  ASCII Time values will receive a
 * default format.
 *
 * @param pThis The output plane in question
 *
 * @param sTimeFmt a printf style format string.  An example strings would be:
 *        '%04d-%02d-%02dT%02d:%02d%02.0fZ' for seconds resolution with the
 *        Zulu time character 'Z' appended.
 *
 * @param nFmtWidth the number of output characters indicated by this format.
 *        This is just the width of the formatted value, not including any
 *        field separators.
 * @memberof DasEnc
 */
DAS_API void DasEnc_setTimeFormat(
	DasEnc* pThis, const char* sTimeFmt, int nFmtWidth
);


/* More explicit indication of a big-endian 8-byte number */
#define DAS2DT_BE_REAL_8 0x0801

/* little-endian (least significant byte first) 8-byte real */
#define DAS2DT_LE_REAL_8 0x0802

/* 8-byte real number, in host byte order */
#ifdef HOST_IS_LSB_FIRST
#define DAS2DT_DOUBLE 0x0802
#else
#define DAS2DT_DOUBLE 0x0801
#endif

/* More explicit indication of a big-endian 4-byte number */
#define DAS2DT_BE_REAL_4 0x0401

/* little-endian (least significant byte first) 4-byte real */
#define DAS2DT_LE_REAL_4 0x0402


/** 4-byte real number, in host byte order */
#ifdef HOST_IS_LSB_FIRST
#define DAS2DT_FLOAT 0x0402
#else
#define DAS2DT_FLOAT 0x0401
#endif

/* Legacy specific width encoding */
#define DAS2DT_ASCII_10 0x0A03
#define DAS2DT_ASCII_24 0x1804 
#define DAS2DT_ASCII_14 0x0E04

#define DAS2DT_TIME_25 0x1904   
#define DAS2DT_TIME_28 0x1c04   


/** Get a string representation of the data type. 
 * 
 * This provides a string representation of the data type that is suitable for
 * use as the type parameter of a plane in a Das2 stream packet header.  All 
 * Das2 Tools use the same string representations for encodings.
 * 
 * @param[in] pThis the Das2 encoding to represent as a string.
 * 
 * @param[out] sType a buffer to receive this encoding's type string, 20 bytes
 *             always suffices.
 * @param[in] uLen the size of sType, including room for the NUL.
 * 
 * @returns sType, or NULL if the whole name did not fit or the encoding
 *          category is unknown (the latter after a das_error() call).
 *
 * @memberof DasEnc
 */
DAS_API char* DasEnc_toStr(const DasEnc* pThis, char* sType, size_t uLen);


/** Encode and write a value onto a string.
 *
 * Writes a value onto a stream without any separators.  Note that the ASCII
 * types write one fewer bytes than their DasEnc_width() would
 * indicate.  The last byte is left for the caller to use as a separator of 
 * their choosing.
 *
 * @param pThis the DasEnc object to handle the translation
 * @param pBuf a write buffer to receive the encoded bytes
 * @param value The value to write
 * @param units All values are held internally by the library as doubles.  In
 *        the instance that the output type for this encoding is a time string
 *        this parameter will be used to determine the epoch and scale for 
 *        @a value.  Otherwise it is ignored.
 *
 * @returns 0 on success, a positive error value on an error.
 * @memberof DasEnc
 */
DAS_API das_errcode DasEnc_write(DasEnc* pThis, DasBuf* pBuf, double value,
                       das_units units);


/** Read and Decode a value from a string.
 *
 * Reads a value from a stream consuming and ignoring separators.
 *
 * @param[in] pThis the DasEnc object to handle the translation
 *
 * @param[in] pBuf The buffer to read from.  This buffer should have at 
 *        least DasEnc_width() more valid bytes in the buffer from the
 *        current read location.
 *
 * @param[in] units All values are held internally by the library as doubles.  
 *        this means that times must be converted to some number of steps
 *        from a 0 time.  The units value provides this information.  It is
 *        not consulted for non-time fields.
 *
 * @param[out] pOut The decoded value.
 * @returns 0 on success or a positive error code if there is a problem.
 * @memberof DasEnc
 */
DAS_API das_errcode DasEnc_read(
	const DasEnc* pThis, DasBuf* pBuf, das_units units, double* pOut
);

/** @} */

#ifdef __cplusplus
}
#endif

#endif /* _das_enc_h_ */
