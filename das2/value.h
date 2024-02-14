/* Copyright (C) 2018 Chris Piker <chris-piker@uiowa.edu>
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

/** @file value.h A generic value type for use in arrays, datums and variables */

#include <stdint.h>

#include <das2/util.h>

#ifndef _das_value_h_
#define _das_value_h_

#ifdef __cplusplus
extern "C" {
#endif
	
/** @defgroup values Values
 * Basic data storage elements
 */

/** Canonical fill value (*/
#define DAS_FILL_VALUE -1e31
	
/** Conversion fill value for integer time intervals (-9.223e+18)*/
#define DAS_INT64_FILL -0x7FFFFFFFFFFFFFFFL
#define DAS_INT32_FILL -0x7FFFFFFF

	
/** @addtogroup values
 * @{
 */
 
typedef struct das_byteseq_t{
	const ubyte* ptr;
	size_t      sz;
} das_byteseq;


#define VT_MIN_SIMPLE vtUByte
#define VT_MAX_SIMPLE vtTime

/** Enumeration of types stored in Das Array (DasAry) objects
 * Note that any kind of value may be stored in a Das Array, but most of these
 * types have runtime type safty checks.
 */
typedef enum das_val_type_e {
	
   /** For generic storage, designates elements as unknown, you have to cast
	 * the array return values yourself.*/
	vtUnknown = 0,
   
   /** The basic types */

   /* VT_MIN_SIMPLE = vtUByte */

	/** Indicates array values are unsigned 8-bit integers (bytes) */
	vtUByte = 1,

   /** Indicates array values are signed 8-bit integers (signed bytes) */
   vtByte = 2;
	
   /** Indicates array values are unsigned 16-bit integers (shorts) */
	vtUShort = 3,
	
   /** Indicates array values are signed 16-bit integers (shorts)*/
	vtShort = 4,
	
   /** Indicates array values are unsigned 32-bit integers (uints) */
   vtUint = 5;

   /** Indicates array values are signed 32-bit integers (ints) */
	vtInt = 6,
	
   /** Indicates array values are unsigned 64-bit unsigned integers (ulongs) */
	vtULong = 7,

   /** Indicates array values are unsigned 64-bit integers (longs) */
   vtLong = 8,
	
   /** Indicates array values are 32-bit floating point values (floats) */
	vtFloat = 9,
	
   /** Indicates array values are 64-bit floating point values (doubles) */
	vtDouble = 10,

	/** Indicates array values are das_time_t structures */
	vtTime = 11,

   /* VT_MAX_SIMPLE = vtTime */

   /* The following type is not used by datums, but by array indexing elements 
    * that track the size and location of child dimensions */
   vtIndex = 11,
			
	/* The following two types are only used by variables and datums
	 * 
	 * When generating Datums from Arrays: 
	 * 
	 *   - If the array element type is etUnknown then vtByteSeq is used, size
	 *      is element size.
	 * 
	 *   - If the array element type is etByte and the D2ARY_AS_STRING flag is
	 *       set then vtText is used.
	 * 
	 *   - If the array element type is anything else and D2ARY_AS_SUBSEQ is
	 *     set then vtByteSeq is used, size is element size times the size of
	 *     the fastest moving index the location read.
	 */
			
	/** Indicates datum values are const char* pointers to null terminated 
	 *  UTF-8 strings */
	vtText = 12,

	/** Value are a vector struct as defined by vector.h */
	vtGeoVec = 13,

   /** Indicates values are size_t plus const ubyte* pairs, no more is
    * known about the bytes */
   vtByteSeq = 14,
   	
} das_val_type;


/** Get a storage value type given the common packet encodings
 * 
 * Storage types are values you can do calculations on.  For binary
 * encodings, these just represent the type minus any endian considerations
 * 
 * For text types that have an intended use, this returns a suitable binary
 * storage type.
 * 
 * @param sEncType The storage type one of:
 *           byte, ubyte, BEint, BEuint, BEreal, LEint, LEuint, LEreal
 *        or:
 *           utf8, none
 * 
 * @param nItemsBytes The number of bytes for each stored item
 * 
 * @param sInterp - Ignored unless the encoding is utf8, otherwise one of
 *        the values:
 *           bool, datetime, int, real, string
 * */
DAS_API das_val_type das_vt_store_type(
   const char* sEncType, int nItemBytes, const char* sInterp
);

/** Get the rank of a value type
 * 
 * Most items are scalars (rank 0), but strings and vectors are rank 1
 */
#define das_vt_rank(VT) ( ((VT==vtGeoVec)||(VT==vtText)||(VT==vtByteSeq)) ? 1:0)


/** Get the default fill value for a given element type
 * @return a pointer to the default fill value, will have to cast to the
 *          appropriate type.
 */
DAS_API const void* das_vt_fill(das_val_type vt);

/** Get the size in bytes for a given element type */
DAS_API size_t das_vt_size(das_val_type vt);

/** Get a text string representation of an element type */
DAS_API const char* das_vt_toStr(das_val_type vt);

/** Convert a text string representation */
DAS_API das_val_type das_vt_fromStr(const char* sType);


/** Get a das value from a null terminated string 
 * 
 * This function should not exit, instead erroreous parsing triggers log messages
 * 
 * @returns DAS_OKAY if parsing was successful, an error return code otherwise.
 */
DAS_API DasErrCode das_value_fromStr(
   ubyte* pBuf, int uBufLen, das_val_type vt, const char* sStr
);

/* * Given a string and it's expected interpretation, return a suitable storage type
 */
/* DAS_API das_val_type das_vt_guess_store(const char* sInterp, const char* sValue); */

/** Comparison functions look like this */
typedef int (*das_valcmp_func)(const ubyte*, const ubyte*);

/** Get the comparison function for two values of this type */
DAS_API das_valcmp_func das_vt_getcmp(das_val_type vt);

/** Compare any two value types for equality.
 *
 * If two types (vtA, vtB) are the same, memcmp is used.
 * If two types are different the following promotion rules are applied.
 *
 * 1. Strings are never equal to non strings.
 * 2. Since values have no units, times are never equal to non-times
 * 3. 
 *
 * If either side is a vtByte, vtUShort, vtShort, vtInt, or vtFloat,
 * vtDouble, both sides are promoted to double and compared.
 *
 * @returns -1 if A is less than B, 0 if equal, +1 if A is greater
 *          than B or -2 if A is not comparable to B.
 */
DAS_API int das_vt_cmpAny(
	const ubyte* pA, das_val_type vtA, const ubyte* pB, das_val_type vtB
);

/* In the future the token ID will come from the lexer, for now just make
 * something up*/
#define D2OP_PLUS 100

/** What would be the resulting type given an operation on the given value
 * type.
 * 
 * Currently the binary type combining rules are:
 * 
 * 1. Unknown combined with anything is unknown.
 * 2. Index combined with anything is unknown.
 * 3. ByteSeq combined with anything is unknown.
 * 4. Text combined with anything is unknown.
 * 
 * 5. Byte, UShort and Short math results in floats.
 * 6. Int, Long, Float and Double math results in doubles.
 *  
 * 7. If time in involved the following rules apply:
 *  
 *      Time - Time = Double
 *      Time +/- (Byte, UShort, Short, Int, Float Double) => Time
 *    
 *     All other operations invalving times are unknown
 * 
 * @param right
 * @param op An operation ID.
 * @param left
 * @return The resulting type or vtUnknown if the types cannot be 
 *         combinded via any known operations
 */
DAS_API das_val_type das_vt_merge(das_val_type right, int op, das_val_type left);


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
 *        whitespace or a '\\0' characater will still halt character
 *        accumlation.
 *
 * @param pRes The location to store the resulting integer.
 *
 * @returns @c true if the conversion succeeded, @c false otherwise.
 *
 * Will only inspect up to 64 non-whitespace characters when converting a
 * value.
 */
DAS_API bool das_strn2baseint(const char* str, int nLen, int base, int* pRes);

/* Don't think these are used anywhere
typedef struct das_real_array{
	double* values;
	size_t length;
} das_real_array;

typedef struct das_creal_array{
	const double* values;
	size_t length;
} das_creal_array;

typedef struct das_int_array{
	int* values;
	size_t length;
} das_int_array;

typedef struct das_cint_array{
	const int* values;
	size_t length;
} das_cint_array;
*/

/** Parse a comma separated list of ASCII values into a double array.
 * @param[in] s The string of comma separated values
 * @param[out] nitems a pointer to an integer which will be set to the
 *             length of the newly allocated array.
 *
 * @returns a new double array allocated on the heap.
 */
DAS_API double* das_csv2doubles(const char * s, int* nitems);


/** Print an array of doubles into a string buffer.
 * Prints an array of doubles into a string buffer with commas and spaces
 * between each entry.  Note there is no precision limit for the printing
 * so the space needed to hold the array may 24 bytes times the number
 * number of values, or more.
 *
 * @todo this function is a potential source of buffer overruns, fix it.
 *
 * @param[out] pBuf a pointer to the buffer to receive the printed values
 * @param[in]  uBufSz The length of the buffer to received the converted values
 * @param[in] pValues an array of doubles
 * @param[in] nValues the number of items to print to the array
 *
 * @returns A pointer to the supplied buffer.
 */
DAS_API char* das_doubles2csv(char* pBuf, size_t uBufSz, const double* pValues, int nValues);


/** Similar to das_doubles2csv, but for 32-bit floats */
DAS_API char* das_floats2csv(char* pBuf, size_t uBufSz, const float* pValues, int nValues);

/** @} */


#ifdef __cplusplus
}
#endif

#endif /* _das_value_h_ */

