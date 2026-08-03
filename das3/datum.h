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

#include <das3/value.h>
#include <das3/units.h>

#ifdef __cplusplus
extern "C" {
#endif

#define DATUM_BUF_SZ 32 // big enough to hold a das_vector and das_time
	
/** @addtogroup values
 * @{
 */

/** An atomic data processing unit, and it's units.
 * 
 * Datum objects are stack objects: a template a caller fills in as shorthand
 * for moving a value around.  They are not meant to be retained -- not parked
 * in a long lived structure, not handed back from the function that filled
 * one in.  The plain old C equals (=) operator copies one, which is the point.
 *
 * Whether that rule can be relaxed for a particular datum is exactly what
 * das_datum_islocal() answers.  Locality is a property of the *value type*,
 * not of the size: a local datum owns its bytes outright and may be copied
 * and kept for as long as its holder likes.  A non-local datum boxes a
 * pointer to memory somebody else owns -- an array, a decode buffer -- and is
 * good only while that memory is.  A vtText datum is smaller than a local
 * vtTime one and is still a reference.
 *
 * Any interface that keeps a datum past the call it arrived on must therefore
 * check das_datum_islocal() and refuse what it cannot hold.
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

/* The vtComposite box -- its struct and its bit layout -- is private to
   datum.c.  Unlike a das_time or a double, which a caller may reasonably cast
   a datum pointer to, a composite is not a value you read by looking at it:
   the extents live in two places depending on size, and the run belongs to
   whoever supplied it.  Use the accessors below.  Nothing is lost by hiding
   it and the representation stays free to change. */


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

/** Some datums have extended storage such as byte strings and composite
 * runs.  Get the fundamental element type for a datum.
 * @memberof das_datum*/
DAS_API das_val_type das_datum_elemType(const das_datum* pThis);

/** Box a caller-owned run as a vtComposite datum.
 *
 * pRun must outlive the datum; nothing is copied.  The extents are stored
 * inline when they fit, in which case pShape need not survive the call --
 * das_datum_shape() is the only thing that knows which way they went.
 *
 * @param pThis the datum to fill
 * @param pForm the formalism that gives the run meaning, never NULL
 * @param pRun the cells, which must outlive the datum
 * @param nRank the run's internal rank
 * @param pShape the extents, nRank of them
 * @param et what one cell holds
 * @param units the run's units
 * @returns false on a loud error.  @memberof das_datum */
DAS_API bool das_datum_box(
	das_datum* pThis, const struct das_form* pForm, const ubyte* pRun,
	int nRank, const ptrdiff_t* pShape, das_val_type et, das_units units
);

/** The formalism behind a vtComposite datum, or NULL if it is not one.
 * Opaque here on purpose; datum.c never dereferences it.  @memberof das_datum */
DAS_API const struct das_form* das_datum_form(const das_datum* pThis);

/** The cell run behind a vtComposite datum, or NULL.
 * A view into caller-owned storage, valid only while that storage is.
 * @memberof das_datum */
DAS_API const ubyte* das_datum_run(const das_datum* pThis);

/** The shape of one datum's value: rank, and the extents at that rank.
 *
 * Total over every datum type, so a caller may ask without first knowing what
 * it holds:
 *
 *   a simple value  rank 0, nothing written
 *   vtText          rank 1, the character count, exactly what strlen reports
 *   vtByteSeq       rank 1, the byte count
 *   vtComposite     the internal rank, and the extents at each level
 *
 * A vtText extent counts characters and not the terminator: the NUL that
 * D2ARY_AS_STRING puts in an array is das2C's storage choice, not part of the
 * encoded value, so the backing array reports one more than this does.
 *
 * A composite stores small extents one way and large or ragged ones another;
 * this is the only thing that knows which, so the two cannot drift apart.
 *
 * @param pShape receives the extents, room for at least VARIDX_MAX, or NULL
 *        for a caller that only wants the rank
 * @returns the rank, 0 for a scalar.  @memberof das_datum */
DAS_API int das_datum_shape(const das_datum* pThis, ptrdiff_t* pShape);

/** Total cells in the run, the product of the extents.
 * @returns the count, 1 for a scalar, 0 if any extent is ragged.
 * @memberof das_datum */
DAS_API size_t das_datum_nElems(const das_datum* pThis);

/** The bytes a caller must supply to hold one item of this datum's value.
 *
 * The question a loop asks before it starts: can my stack array hold what is
 * coming, or do I need to allocate?  Answered from a datum a caller already
 * has, so it costs no second call into the variable layer.
 *
 * @returns the byte count, 0 for a value that needs no caller storage (a
 *          scalar, or a composite viewing an array).  @memberof das_datum */
DAS_API size_t das_datum_runBytes(const das_datum* pThis);

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

/** Does this datum own its bytes, or is it a reference?
 *
 * True means the value sits in the datum's own storage: copy it, keep it,
 * park it in a structure that outlives the caller.  False means the datum
 * boxes a pointer and is only good while the pointed-at memory is.
 *
 * Which types get boxed locally is datum.h's choice, not a law of the value
 * layer.  The simple types are the bulk of it; vtIndex joins them because a
 * das_idx_info (array.h) is two plain integers and so reaches outside itself
 * no more than a double does.  value.h keeps them adjacent as
 * VT_MIN_LOCAL..VT_MAX_LOCAL so this stays one comparison pair.
 *
 * @param p Constant pointer to the datum to check.
 * @memberof das_datum
 */
#define das_datum_islocal(p) \
   ( ((p)->vt >= VT_MIN_LOCAL) && ((p)->vt <= VT_MAX_LOCAL) )


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
	das_datum* pThis, das_cbyte_seq seq, das_units units
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
