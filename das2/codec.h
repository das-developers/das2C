/* Copyright (C) 2024  Chris Piker <chris-piker@uiowa.edu>
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

/** @file codec.h Encoding/Decoding arrays to and from buffers */

#ifndef _das_codec_h_
#define _das_codec_h_

#include <stdint.h>

#include <das2/value.h>
#include <das2/array.h>
#include <das2/buffer.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Not public, only here because used in a macro */
#define DASENC_VALID    0x0001 

/** @addtogroup IO
 * @{
 */

/** Binary ragged IEEE float separators 
 * 
 * These 32-bit NaN palindromes useful as separators for ragged binary float
 * as they look like non-standard quiet NaNs when read either as big-endian
 * or little-endian values.
 * 
 * All report true from isnan(), but do not have the common NaN pattern produced
 * by the NAN macro in math.h
 */
#ifndef _das_codec_c_
extern const ubyte DAS_FLOAT_SEP[DASIDX_MAX][4];
extern const ubyte DAS_DOUBLE_SEP[DASIDX_MAX][8];
#endif

/** Reading and writing array data to buffers */
typedef struct das_codec {

   bool bResLossWarn; /* If true, the resolution loss warning has already been emitted */

	uint32_t uProc; /* Internal processing flags setup on the call to _init */

   int nAryValSz;  /* The size of each array value in internal buffer */

   int16_t nBufValSz;  /* Width of a single value in the external buffer */

	das_val_type vtBuf; /* The value type in the external buffer */

	DasAry* pAry;  /* The array for which values are encoded/decoded */

   ubyte cSep;   /* Split strings on this byte value, in addition to null */

   uint32_t uMaxString; /* If we are storing fixed strings, this is set */

   das_units timeUnits; /* If ascii times are to be stored as an integral type
                           this is needed */

   char* pOverflow;  /* If the size of a variable length value breaks */
   size_t uOverflow; /* the small vector assumption, extra space is here */

} DasCodec;


/** Has the memory for this encoder been initialized? 
 * 
 * @memberof DasCodec 
 */
#define DasCodec_isValid(pCd) (((pCd)->uProc & DASENC_VALID)==(DASENC_VALID))

/** Initialize a serial buffer decoder/encoder
 * 
 * @param pThis A pointer to the memory area to initialize
 * 
 * @param pAry A pointer to the array which either receive or supply values.
 *        Values will be encoded so that they match the value type of the
 *        array. 
 *        @warning If the basic parameters of this array, such as it's value
 *        type or rank are changed, then DasCodec_init() must be re-called.
 * 
 * @param sSemantic The purpose of the data to store in the buffer, should
 *        be one of 'bool','int','real','datatime','string'.  This determines
 *        the kinds of calculations that may be performed on the data once
 *        in memory. 
 * 
 * @param sEncType The basic encoding type of data in the buffer, one of:
 *        - byte   : 8-bit signed integer
 *        - ubyte  : 8-bit un-signed integer
 *        - BEint  : A signed integer 2+ bytes long, most significant byte first
 *        - BEuint : An un-signed integer 2+ bytes long MSB first
 *        - LEint  : Little-endian version of BEint
 *        - LEuint : Little-endian version of BEuint
 *        - BEreal : An IEEE-754 floating point value, MSB first
 *        - LEreal : An IEEE-754 floating point value, LSB first
 *        - utf8   : A string of text bytes
 * 
 * @param nSzEach the number of bytes in an item.  For variable length
 *        items (which is common with the utf8 encoding) use -1.
 * 
 * @param cSep A single byte used to mark the end of a byte sequence for
 *        string data.  By default any space character marks the end of
 *        a string.  Use 0 to ignore.
 * 
 * @param epoch If time data needs to be converted from UTC strings an epoch
 *        will be needed.  Otherwise this field can be NULL
 * 
 * @returns DAS_OKAY if an decoder/encoder for can be created for the given
 *        arguments, an error code otherwise.
 * 
 * @note For 'string' semantic data where the last index in the array is 
 *       ragged DasAry_markEnd() will be called after each string is read.
 *       Otherwise, no string larger then the last index will be written
 *       and zeros will be appended to fill out the last index when reading
 *       data.
 * 
 * @memberof DasCodec 
 */
DAS_API DasErrCode DasCodec_init(
   DasCodec* pThis, DasAry* pAry, const char* sSemantic, const char* sEncType,
   int16_t uSzEach, ubyte cSep, das_units epoch
);

/** Fix array pointer after a DasCodec memory copy
 * 
 * @param pThis A pointer to the new memory area filled via memcpy()
 * 
 * @param pAry The new array to assciate with this codec
 * 
 * @memberof DasCodec
 */
DAS_API void DasCodec_postBlit(DasCodec* pThis, DasAry* pAry);

/** Read values from a simple buffer into an array
 * 
 * Unlike the old das2 version, this encoder doesn't have a built-in number
 * of values it will always expect to read.  If no pre-determined number
 * of values is given in nExpect, then it will read until the buffer is 
 * exhausted.
 * 
 * To control the number of bytes read control nBufLen
 * 
 * @param pThis An encoder.  The pointer isn't constant because the
 *        encoder may have to allocate some memory for long, variable length
 *        text values.
 *
 * @param pBuf A pointer to the memory to read
 *
 * @param nBufLen The length of the buffer parse into the array.  Note that
 *        even for string data the function trys to read nLen bytes.  Null
 *        values do not terminate parsing but do indicate the end of an 
 *        individual utf-8 encoded item.
 *
 * @param nExpect The number of values to try and read.  Reading less then 
 *        this does not trigger an error return.  If the caller considers
 *        reading less values then expect to be an error, compare *pRead 
 *        with the number provided for nExpect.  If any number of values 
 *        can be read, set this to -1.
 * 
 * @param pValsRead A pointer to a location to hold the number of values read
 *        or NULL.  If NULL, the number of values read will not be returned
 * 
 * @returns the number of unread bytes or a negative ERR code if a data conversion
 *        error occured.
 * @memberof DasCodec 
 */
DAS_API int DasCodec_decode(
   DasCodec* pThis, const ubyte* pBuf, int nBufLen, int nExpect, int* pValsRead
);

/** Write values from an array into a buffer, does not change the array */
DAS_API int DasCodec_encode(
   DasCodec* pThis, DasBuf* pBuf, int nWrite, bool bLast
);

/** Release the reference count on the array given to this encoder/decoder 
 * 
 * @memberof DasCodec 
 */
DAS_API void DasCodec_deInit(DasCodec* pThis);

#ifdef __cplusplus
}
#endif


#endif /* _das_codec_h_ */
