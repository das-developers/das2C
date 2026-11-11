/* Copyright (C) 2004-2026 Chris Piker <chris-piker@uiowa.edu>
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

/** @file str.h String handling: copies, parsing, csv lists and encodings */

#ifndef _das_str_h_
#define _das_str_h_

#include <stdarg.h>
#include <stddef.h>

#include <das3/defs.h>

#ifdef __cplusplus
extern "C" {
#endif

/** @addtogroup utilities
 * @{
 */

/* ************************************************************************* */
/* Copies and formatting */

/** Copy a string into a new buffer allocated on the heap
 *
 * @param sIn the string to copy
 * @return a pointer to the newly allocated buffer containing the same
 *          characters as the input string or NULL if the input length was
 *          zero
 */
DAS_API char* das_strdup(const char* sIn);

/** Allocate a new string on the heap and format it
 *
 * Except for using das_error on a failure, this is a copy of the
 * code out of man 3 printf on Linux.
 *
 * @returns A pointer to the newly allocated and formatted string on
 *          the heap, or NULL if the function failed and the das2 error
 *          disposition allows for continuation after a failure
 */
DAS_API char* das_string(const char* fmt, ...);

/** Store a formatted string in a newly allocated buffer
 *
 * This version is suitable for calling from variable argument functions.
 *
 * Except for using das_error on a failure, this is a copy of the
 * code out of man 3 printf on Linux.
 *
 * @param fmt - a printf format string
 * @param ap A va_list list, see vfprintf or stdarg.h for details
 *
 * @returns A pointer to the newly allocated and formatted string on
 *          the heap, or NULL if the function failed and the das2 error
 *          disposition allows for continuation after a failure
 */
DAS_API char* das_vstring(const char* fmt, va_list ap);

/* ************************************************************************* */
/* Cleanup */

/** Strip whitespace from a string
 * 
 * @param sLine - Pointer to a writable null terminated string, or a
 *          NULL pointer.
 * 
 * @param cComment - Treat the character if it was a null character
 *          terminating the string.  Replace it with an actual null if
 *          found. Use '\0' to avoid stripping "comments".
 * 
 * @returns NULL if the string consisted only of whitespace, or was empty
 *          or was a NULL pointer. 
 *          Otherwise a pointer to the first non-whitespace portion is 
 *          returned.  A null character will be written to the string
 *          at the start of trailing whitespace.
 */
DAS_API char* das_strip(char* sLine, char cComment);

/* ************************************************************************* */
/* Text to numbers */

/** Convert a string value to a 8-byte float, similar to strtod(3).
 *
 * @param str the string to convert.  Conversion stops at the first improper
 *        character.  Whitespace and leading 0's are ignored in the input.
 *
 * @param pRes The location to store the resulting 8-byte float.
 *
 * @returns @c true if the conversion succeeded, @c false otherwise.  Among
 *        other reason, conversion will fail if the resulting value won't fit
 *        in a 8 byte float.
 */
DAS_API bool das_str2double(const char* str, double* pRes);

/** Convert the initial portion of a string to an integer with explicit 
 * over/underflow checks
 *
 * @param str the string to convert.  Conversion stops at the first improper
 *        character.  Whitespace and leading 0's are ignored in the input.
 *        The number is assumed to be in base 10, unless the first non-whitespace
 *        characters after the optional '+' or '-' sign are '0x'.
 *
 * @param pRes The location to store the resulting integer.
 *
 * @returns @c true if the conversion succeeded, @c false otherwise.
 */
DAS_API bool das_str2int(const char* str, int* pRes);

/** Convert a string value to a boolean value.
 *
 * @param str the string to convert.  The following values are accepted as
 *        representing true:  'true' (any case), 'yes' (any case), 'T', 'Y',
 *        '1'.  The following values are accepted as representing false:
 *        'false' (any case), 'no', (any case), 'F', 'N', '0'.  Anything else
 *        results in no conversion.
 * @param pRes the location to store the resulting boolean value
 * @return true if the string could be converted to a boolean, false otherwise.
 */
DAS_API bool das_str2bool(const char* str, bool* pRes);

/** Convert a string to an integer with explicit base and overflow
 * checking.
 *
 * @param str the string to convert.  Conversion stops at the first improper
 *        character.  Whitespace and leading 0's are ignored in the input.
 *        No assumptions are made about the base of the string.  So anything
 *        that is not a proper character is the given base is causes an
 *        error return.
 *
 * @param base an integer from 1 to 60 inclusive.
 *
 * @param pRes The location to store the resulting integer.
 *
 * @returns @c true if the conversion succeeded, @c false otherwise.
 */
DAS_API bool das_str2baseint(const char* str, int base, int* pRes);

/** Convert an explicit length string to an integer with explicit base with
 * over/underflow checks.
 *
 * @param str the string to convert.  Conversion stops at the first improper
 *        character.  Whitespace and leading 0's are ignored in the input.
 *        No assumptions are made about the base of the string.  So anything
 *        that is not a proper character is the given base is causes an
 *        error return.
 *
 * @param base an integer from 1 to 60 inclusive.
 *
 * @param nLen only look at up to this many characters of input.  Encountering
 *        whitespace or a '\\0' character will still halt character
 *        accumulation.
 *
 * @param pRes The location to store the resulting integer.
 *
 * @returns @c true if the conversion succeeded, @c false otherwise.
 *
 * Will only inspect up to 64 non-whitespace characters when converting a
 * value.
 */
DAS_API bool das_strn2baseint(const char* str, int nLen, int base, int* pRes);

/** A C locale string to double converter 
 * 
 * This is essentially the same as the C-lib function strtod, except it always
 * evaluates strings in the "C" locale no matter the current locale of the 
 * program.  It does not alter the current program's locale.
 * 
 * @param nptr    The starting point of the string to convert
 * @param endptr  Where to store a pointer to the last item converted
 * @return        The converted value 
 * 
 * Notes: Errno is set as normal for the underlying strtod implementation.
 */
DAS_API double das_strtod_c(const char *nptr, char **endptr);

/** Small helper for printing reals in less space 
 * 
 * Since we don't know how precise a double precision value is, it's common
 * to want to print these to 14 digits (or similar).  Many times all those 
 * extra digits are just zeros.  Use this function to shorten real values 
 * that after they have been written to a buffer.
 */
DAS_API void das_value_trimReal(char* sVal);

/* ************************************************************************* */
/* Comma separated lists */

/** Parse a comma separated list of ASCII values into a double array.
 *
 * Fields are read in the C locale, '.' is the decimal point whatever the
 * program's locale.  White space around a field is fine; an empty field or
 * one that is not a number is an error.
 *
 * @param[in] sCsv The string of comma separated values
 * @param[out] pNumItems a pointer to an integer which will be set to the
 *             length of the newly allocated array.
 *
 * @returns a new double array allocated on the heap  
 *          Not NULL on success: blank text is an empty list, a zero count
 *          and a one slot array.  
 *          NULL, with *pNumItems = 0, after das_error() for NULL
 *          text or a field that is not a number.
 */
DAS_API double* das_csv2doubles(const char* sCsv, int* pNumItems);

/** Print an array of doubles into a string buffer.
 * Prints an array of doubles into a string buffer with commas and spaces
 * between each entry.  Note there is no precision limit for the printing
 * so the space needed to hold the array may 24 bytes times the number
 * number of values, or more.  Nothing is written past pBuf[uBufSz - 1].
 *
 * @param[in] pValues an array of doubles
 * @param[in] nValues the number of items to print to the array
 * @param[out] pBuf a pointer to the buffer to receive the printed values
 * @param[in]  uBufSz The length of the buffer to received the converted values
 *
 * @returns A pointer to the supplied buffer, or NULL after das_error() if
 *          the values do not all fit.
 */
DAS_API char* das_doubles2csv(const double* pValues, int nValues, char* pBuf, size_t uBufSz);

/** Similar to das_doubles2csv, but for 32-bit floats */
DAS_API char* das_floats2csv(const float* pValues, int nValues, char* pBuf, size_t uBufSz);

/* ************************************************************************* */
/* Escapes and encodings */

/** Translate unsafe characters for XML string output 
 *
 * The characters [ " ' < > & ] are translated to entities, everything else,
 * white space included, is copied as is.  So long as dest is at least one
 * byte long the result is null terminated.  Output that doesn't fit is
 * dropped from the first source character that won't fit whole, so neither
 * an entity nor a multi-byte UTF-8 character is ever cut in half.
 *
 * @param src  The data source
 * @param dest The buffer to receive the output
 * @param uOutLen the size of the dest buffer.  
 *
 * @returns the parameter dest cast to a constant pointer.
 */
DAS_API const char* das_xml_escape(const char* src, char* dest, size_t uOutLen);

/** Percent encode a string for use in a URL query.
 *
 * Everything but the RFC 3986 unreserved characters, letters, digits and
 * "-._~", is encoded, including the ':' in time strings.  Servers decode
 * before use so over-encoding is harmless, and it is what most encoders
 * do.  Works like snprintf: the output is always NUL terminated when uOut
 * is at least 1, and the return value says how much room was needed.
 *
 * @param sIn The text to encode.
 * @param sOut Receives the encoding.  Three times the input length plus
 *        one always suffices.
 * @param uOut The size of sOut.
 * @returns The length of the full encoding, not counting the terminator.
 *        If this is uOut or more the output was cut short.
 */
DAS_API size_t das_url_encode(const char* sIn, char* sOut, size_t uOut);

/** Encode binary data as base64 (RFC 4648) characters in a new buffer.
 *
 * (Credit: stackoverflow user ryyst)
 * @param data A pointer to the data to encode
 * @param input_length The number of input bytes to encode
 * @param output_length a pointer to location to receive the encoded length
 *        (4 * ceil(input_length/3), '=' padded), not counting the terminator
 *
 * @returns A pointer to a newly allocated, NUL terminated string, or NULL
 *          if calloc failed.  Caller frees.
 */
DAS_API char* das_b64_encode(
   const unsigned char* data, size_t input_length, size_t* output_length
);

/** Decode base64 (RFC 4648) into a caller-provided buffer, no allocation.
 *
 * @param data The base64 characters (length a multiple of 4, '=' padded)
 * @param input_length The number of base64 characters
 * @param out The caller's output buffer
 * @param out_cap The capacity of out
 * @param output_length receives the number of decoded bytes written. Decoded
 *        output is at most 3/4 of input_length, so:
 *           out_cap >= 3*input_length/4
 *        always suffices.
 * @returns DAS_OKAY, or a negative das error code on malformed input or
 *          a short output buffer.
 */
DAS_API int das_b64_decode_buf(
   const char* data, size_t input_length, unsigned char* out, size_t out_cap,
   size_t* output_length
);

/** Decode base64 (RFC 4648) into a newly allocated buffer, symmetric with
 * das_b64_encode.  Caller frees.  Returns NULL on malformed input or alloc
 * failure (with *output_length set to 0).  
 *
 * @note For repeated decoding, use the no-alloc function das_b64_decode_buf().
 */
DAS_API unsigned char* das_b64_decode(
   const char* data, size_t input_length, size_t* output_length
);

/** @} */

#ifdef __cplusplus
}
#endif

#endif /* _das_str_h_ */
