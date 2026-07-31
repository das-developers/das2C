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
struct das_form;   /* opaque here; datum.c never dereferences one */

/** How a composite datum says what it is.
 *
 * A callback rather than a direct call into the form layer, so datum.c never
 * includes form.h.  form.h already includes datum.h (pack() hands one back),
 * and teaching datum.c about forms would close that into a cycle.  The form
 * fills this in when it packs; datum.c calls through it and stays ignorant.
 */
typedef char* (*das_comp_prn)(
	const struct das_form* pForm, const ubyte* pRun, uint32_t nElems,
	das_val_type et, char* sBuf, int nLen
);

/** A run of numeric cells plus the formalism that says what they mean.
 *
 * Unlike das_geovec this is a VIEW: the run stays in the backing store and
 * must outlive the datum, the same way vtText and vtByteSeq already work.  A
 * 3;3 rotation will not fit in DATUM_BUF_SZ and a quaternion fills it exactly,
 * leaving no room for the form, so copying is not an option in general.
 */
typedef struct das_composite_t {

	/* FIRST, so the cast-to-your-type idiom below still works */
	const ubyte* pRun;

	/* What the run MEANS.  Never NULL: an unrecognized kind still gets a
	   DasFormGeneric, so there is no "composite with no formalism" state. */
	const struct das_form* pForm;

	das_comp_prn prn;      /* how to say it; the form supplied this */

	uint32_t     nElems;
	das_val_type et;

} das_composite;           /* 32 bytes, exactly DATUM_BUF_SZ */

typedef struct datum_t {
   ubyte bytes[DATUM_BUF_SZ]; /* 32 bytes of space */
   das_val_type vt;
   uint32_t vsize;
   das_units units;
} das_datum;

/** @} */

/** General datum initialization 
 * 
 * @param pSrc A pointer to the source location.  If the source bytes are
 *        less then DATUM_BUF_SZ they are copied internally, otherwise
 *        the pointer itself is stored in the first member of the struct.
 * 
 * @param vt The value type, one of the 15 types listed in the das_val_type
 *        enum.
 * 
 * @param vsize The value size, for anything other then vtUnknown, vtByteSeq 
 *        and vtText this parameter is ignored.
 * 
 * @param units The units associated with the value.
 * 
 * @member of das_datum
 */
DAS_API void das_datum_init(
   das_datum* pThis, const ubyte* pSrc, das_val_type vt, uint32_t vsize, 
   das_units units
);

/** Same datums have extended storage such as byte strings
 * and geovectors.  Get the fundamental element type for a datum.
 * @memberof das_datum*/
DAS_API das_val_type das_datum_elemType(const das_datum* pThis);

/** Box a run of cells as a vtComposite datum.
 *
 * @param pThis the datum to fill
 * @param pForm the formalism that gives the run meaning, never NULL
 * @param prn how to render it
 * @param pRun the cells, which must outlive the datum
 * @param nElems how many cells
 * @param et what one cell holds
 * @param units the run's units
 * @returns false on a loud error.  @memberof das_datum */
/** The formalism behind a vtComposite datum, or NULL if it is not one.
 * Opaque here on purpose; datum.c never dereferences it.  @memberof das_datum */
DAS_API const struct das_form* das_datum_form(const das_datum* pThis);

/** The cell run behind a vtComposite datum, or NULL.
 * A VIEW into the backing store: valid until that array is appended to or
 * otherwise modified.  @memberof das_datum */
DAS_API const ubyte* das_datum_run(const das_datum* pThis);

/** How many cells das_datum_run() points at; 0 if not a composite.
 * @memberof das_datum */
DAS_API size_t das_datum_nElems(const das_datum* pThis);

DAS_API bool das_datum_box(
	das_datum* pThis, const struct das_form* pForm, das_comp_prn prn,
	const ubyte* pRun, size_t nElems, das_val_type et, das_units units
);

/** Check to see if a datum has been initialized.  
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

DAS_API ptrdiff_t das_datum_shape0(const das_datum* pThis);

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

/** Similar to das_datum_toStr, but can specify a separator for vectors.
 * The separator is ignored if the element type is not multi-valued */
char* das_datum_toStrValOnlySep(
   const das_datum* pThis, char* sStr, size_t uLen, int nFracDigits, 
   const char* sSep
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
 * @returns true if the conversion was successful, false otherwise.
 *          das_error is called if the value type of the datum
 *          makes no sense in the contex of a datetime.
 * 
 * @memberof das_datum
 */
DAS_API bool das_datum_toEpoch(
	const das_datum* pThis, das_units epoch, double* pResult
);

/** Get a time datum value as a das_time
 * 
 * @param pThis a pointer to the datum to convert
 * @param pDt a pointer to a das_time to hold the result
 * 
 * @returns true if the conversion was successful, false otherwise.
 *          das_error is called if the value type of the datum
 *          makes no sense in the contex of a datetime.
 * 
 * @memberof das_datum
 */
DAS_API bool das_datum_toTime(const das_datum* pThis, das_time* pDt);


/** Get the value form a string datum
 * 
 * @memberof das_datum
 */
#define das_datum_asStr(dm) *((char**)&dm)

#ifdef __cplusplus
}
#endif

#endif	/* _das_datum_h_ */
