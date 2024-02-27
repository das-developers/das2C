/* Copyright (C) 2017 Chris Piker <chris-piker@uiowa.edu>
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

/** @file  datum.h */

#ifndef _das_datum_h_
#define _das_datum_h_

#include <das2/value.h>
#include <das2/units.h>

#ifdef __cplusplus
extern "C" {
#endif

#define DATUM_BUF_SZ 32 // big enough to hold a das_vector and das_time
	
/** @addtogroup values
 * @{
 */

/** An atomic data processing unit, and it's units.
 * 
 * Datum objects can be created as stack variables.  For any object up to 
 * DATUM_BUF_SZ all memory is internal and the plain old C equals (=) 
 * operator can be used to assign the contents of one datum to another.
 * 
 * For larger objects an external constant pointer is used to denote the 
 * value.  So copy by = still works.  Use das_datum_islocal() to determine
 * if the datum value is contained in local memory or if it's an external
 * reference.
 * 
 * Datums have thier byte array stored first in their structure so it is
 * possible to cast pointers to datums as pointers to their type if the
 * type is known.  For example:
 * 
 * @code 
 * das_datum dm;
 * das_datum_fromStr(&dm, "2017-01-02T12:14");
 * 
 * int year = ((*das_time)(&dm))->year;
 * 
 * // This works
 * das_datum_Double(&dm, "2.145 meters");
 * double length = *((*double)dm);
 * 
 * @endcode
 * 
 * The datum class is made to work with Variable, to provide a single
 * "value" of a variable, however these values may contain internal
 * structure.  Two prime examples are geometric vectors and strings.
 */
typedef struct datum_t {
   ubyte bytes[DATUM_BUF_SZ]; /* 32 bytes of space */
   das_val_type vt;
   uint32_t vsize;
   das_units units;
} das_datum;

/** @} */

/** Check to seef if a datum has been initialized.  
 * 
 * Note that a datum containing a fill value is still a valid datum for the
 * purposes of this macro.
 *
 * @param p Constant pointer to the datum to check.
 %
 * @effect An expression that evaluates to logical true if pThis->vsize > 0, 
 *         logical false expression otherwise
 * 
 * @memberof das_datum
 */
#define das_datum_valid(p) ((p)->vsize > 0)

ptrdiff_t das_datum_shape0(const das_datum* pThis);

/** Initialize a numeric datum from a value and units string.  
 *
 * Note that this function will not initialize text datums.  This is because
 * text datums only carry a const char* pointer, but not the string itself.
 * Call Datum_wrapStr() to make text datums.
 * 
 * @param pThis pointer to the datum structure to initialize
 * @param sStr the value plus it's units. 
 * @return true if the string was parseable as a datum, false otherwise.
 * @memberof das_datum
 */
DAS_API bool das_datum_fromStr(das_datum* pThis, const char* sStr);

/** Create a datum from a double value and units
 * 
 * This is the most commonly used type of datum, followed by time datums
 * 
 * @param pThis
 * @param value
 * @param units
 * @return Always returns true.
 * @memberof das_datum
 */
DAS_API bool das_datum_fromDbl(das_datum* pThis, double value, das_units units);

/** Wrap an external string as a datum.
 *
 * This is useful for events lists as well as non-numeric coordinate points.
 * For example:
 * @code
 *
 * static const char** cites[] = {
 *    "Iowa City", "Coralville", "North Liberty", "Cedar Rapids"
 * };
 *
 * Make a datum array representing a few cities in Eastern Iowa with units
 * of "city".
 *
 * datum locations[4];
 * for(int i = 0; i < 4; ++i)
 *     das_datum_initStr(locations + i, "city", cities[i]);
 *
 * @endcode
 * @memberof das_datum
 */
DAS_API bool das_datum_wrapStr(das_datum* pTHis, const char* sStr, das_units units);


/** Wrap an external unknown type pointer as a datum.
 *
 * This is for special user defined data types unknown to das2C.  The type
 * of the datum will be vtByteSeq (a byte sequence)
 * @memberof das_datum
 */
DAS_API bool das_datum_byteSeq(
	das_datum* pThis, das_byteseq seq, das_units units
);

/** Write a UTF-8 string representation of a datum to a buffer
 *
 * Time values are printed as ISO-8601 time strings, all floating point values
 * are printed using a generic exponential notation.  String datums are simply
 * printed, and byteseq datums are printed as hex-digits.
 * 
 * @param pThis The datum to write
 * 
 * @param sStr The buffer to write the reprenestation to
 * 
 * @param uLen The amount of space available for writing
 * 
 * @param nFracDigits Number of digits after the decimal place to print.
 *        for multi-part values, such a calendar times this refers to the
 *        number of digits after the decimal point for the last component
 *        only.
 *        Use -1 to get default fractional digits which are 5 for a float,
 *        9 for a double and millisec precision for times.
 * 
 * @return The write point for adding more text to the buffer.  To see 
 *         how much text was written subtract the initial buffer (sBuf) from
 *         this return value.
 * @memberof das_datum
 */
DAS_API char* das_datum_toStr(
	const das_datum* pThis, char* sStr, size_t uLen, int nFracDigits
);

/** Same as das_datum_toStr, but never print the units
 * 
 * @see das_datum_toStr
 * @memberof das_datum
 */
DAS_API char* das_datum_toStrValOnly(
	const das_datum* pThis, char* sStr, size_t uLen, int nFracDigits
);


/** Get a datum value as a double
 * 
 * This function throws an error if the given datum is not convertable
 * as a double value
 * 
 * @param pThis
 * @return The double value
 * @memberof das_datum
 */
DAS_API double das_datum_toDbl(const das_datum* pThis);


/** Get a time datum value as a double at a given epoch an scale
 *
 * @param pThis pointer to the value to convert
 * @param units The desired time base units, expects one of UNIT_US2000 and 
 *              friends.  Do not use UNIT_UTC, that's only ment for broken
 *              down time values.
 * @param pResult Pointer to location to store the converted value.
 *
 * @returns true if the conversion was successful, false otherwise
 *          and das_error is called.
 * @memberof das_datum
 */
DAS_API bool das_datum_toEpoch(
	const das_datum* pThis, das_units epoch, double* pResult
);

/** Get the value form a string datum
 * 
 * @memberof das_datum
 */
#define das_datum_asStr(dm) *((char**)&dm)

#ifdef __cplusplus
}
#endif

#endif	/* _das_datum_h_ */
