/* Copyright (C) 2017 Chris Piker <chris-piker@uiowa.edu>
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

/** @file  datum.h */

#ifndef _das_datum_h_
#define _das_datum_h_

#include <das3/value.h>
#include <das3/units.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Sized for the largest value that lives inline: a das_time.  
   A composite objects point to external data, and the object needed
   to interpret them */
#define DATUM_BUF_SZ 32
	
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
 * @par Null datums
 *
 * A datum of type vtNull is null: no value at all, not even a typed one.
 * das_datum_isNull() tests for it and das_datum_initNull() makes one.  Null
 * prints as an empty string, and an empty string parses back to null.
 *
 * A zeroed block of memory is a legal null datum, so a structure holding
 * datums may be cleared with memset() or "= {0}".  Datums the library makes
 * carry UNIT_DIMENSIONLESS rather than a NULL units pointer.
 *
 * Null is not fill.  A fill value is a typed placeholder that code reading
 * values blindly can carry through a tight loop without a test.  Whether a
 * value is fill is for the code that knows the variable to decide.
 *
 * Datums have their byte array stored first in their structure so it is
 * possible to cast pointers to datums as pointers to their type if the
 * type is known.  For example:
 * 
 * @code 
 * das_datum dm;
 * das_datum_fromStr(&dm, "2017-01-02T12:14");
 * 
 * int year = ((das_time*)&dm)->year;
 * 
 * das_datum_fromStr(&dm, "2.145 meters");
 * double length = *((double*)&dm);
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
 * @param pThis The datum to initialize
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
 * @memberof das_datum
 */
DAS_API void das_datum_init(
   das_datum* pThis, const ubyte* pSrc, das_val_type vt, uint32_t vsize, 
   das_units units
);

/** Make a null datum.
 *
 * The bytes are zeroed, the type is vtNull, vsize is 0 and the units are
 * UNIT_DIMENSIONLESS.  A zeroed datum is also null; this is the form the
 * library writes.
 *
 * @param pThis the datum to set
 * @memberof das_datum
 */
DAS_API void das_datum_initNull(das_datum* pThis);

/** Is this the null datum?
 *
 * Only the type is checked, so zeroed memory is null.  Builds without
 * NDEBUG also log "inconsistent null datum" at debug level for a vtNull
 * datum with non-zero bytes or vsize; with NDEBUG defined there is no check
 * beyond the type.
 *
 * @param pThis the datum to test
 * @return true if pThis has type vtNull
 * @memberof das_datum
 */
DAS_API bool das_datum_isNull(const das_datum* pThis);

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
 * @param pRun the elements, which must outlive the datum
 * @param nRank the run's internal rank
 * @param pShape the extents, nRank of them
 * @param et what one element holds
 * @param units the run's units
 * @returns false on a loud error.  @memberof das_datum */
DAS_API bool das_datum_box(
	das_datum* pThis, const struct das_form* pForm, const ubyte* pRun,
	int nRank, const ptrdiff_t* pShape, das_val_type et, das_units units
);

/** The formalism behind a vtComposite datum, or NULL if it is not one.
 * Opaque here on purpose; datum.c never dereferences it.  @memberof das_datum */
DAS_API const struct das_form* das_datum_form(const das_datum* pThis);

/** The element run behind a vtComposite datum, or NULL.
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
 * DASARY_AS_STRING puts in an array is das2C's storage choice, not part of the
 * encoded value, so the backing array reports one more than this does.
 *
 * A composite stores small extents one way and large or ragged ones another;
 * this is the only thing that knows which, so the two cannot drift apart.
 *
 * @param pShape receives the extents, room for at least VARIDX_MAX, or NULL
 *        for a caller that only wants the rank
 * @returns the rank, 0 for a scalar.  @memberof das_datum */
DAS_API int das_datum_shape(const das_datum* pThis, ptrdiff_t* pShape);

/** Total elements in the run, the product of the extents.
 * @returns the count, 1 for a scalar, 0 if any extent is ragged.
 * @memberof das_datum */
DAS_API size_t das_datum_nElems(const das_datum* pThis);

/** The bytes a caller must supply to hold one item of this datum's value.
 *
 * The question a loop asks before it starts: can my stack array hold what is
 * coming, or do I need to allocate?  Answered from a datum a caller already
 * has, so it costs no second call into the variable layer.
 *
 * @returns the byte count, 0 for a value that needs no caller storage (any
 *          datum that is not a composite).  @memberof das_datum */
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
 * @relates das_datum
 */
#define das_datum_valid(p) ((p)->vsize > 0)

/** Does this datum own its bytes, or is it a reference?
 *
 * True means the value sits in the datum's own storage: copy it, keep it,
 * park it in a structure that outlives the caller.  False means the datum
 * boxes a pointer and is only good while the pointed-at memory is.
 *
 * @param p Constant pointer to the datum to check.
 * @relates das_datum
 */
#define das_datum_islocal(p) \
   ( ((p)->vt >= VT_MIN_LOCAL) && ((p)->vt <= VT_MAX_LOCAL) )


/** Initialize a numeric datum from a value and units string.  
 *
 * Note that this function will not initialize text datums.  This is because
 * text datums only carry a const char* pointer, but not the string itself.
 * Call das_datum_wrapStr() to make text datums.
 *
 * An empty string, or one of only whitespace, makes the null datum and
 * counts as parseable.
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
 * The string is not copied, so it must outlive the datum.  For example, a
 * datum array of a few cities in Eastern Iowa with units of "city":
 * @code
 * static const char* cities[] = {
 *    "Iowa City", "Coralville", "North Liberty", "Cedar Rapids"
 * };
 *
 * das_units city = Units_fromStr("city");
 * das_datum locations[4];
 * for(int i = 0; i < 4; ++i)
 *     das_datum_wrapStr(locations + i, cities[i], city);
 * @endcode
 * @memberof das_datum
 */
DAS_API bool das_datum_wrapStr(das_datum* pThis, const char* sStr, das_units units);


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
 * printed, and byteseq datums are printed as hex-digits.  The null datum
 * prints as an empty string, units and all.
 *
 * @param pThis The datum to write
 * 
 * @param nFracDigits Number of digits after the decimal place to print.
 *        for multi-part values, such a calendar times this refers to the
 *        number of digits after the decimal point for the last component
 *        only.
 *        Use -1 to get default fractional digits which are 5 for a float,
 *        9 for a double and millisec precision for times.
 * 
 * @param sStr The buffer to write the representation to
 * 
 * @param uLen The amount of space available for writing
 * 
 * @return sStr, or NULL if the whole value did not fit.  sStr is always NUL
 *         terminated when uLen > 0.
 * @memberof das_datum
 */
DAS_API char* das_datum_toStr(
	const das_datum* pThis, int nFracDigits, char* sStr, size_t uLen
);

/** Same as das_datum_toStr, but never print the units
 * 
 * @see das_datum_toStr
 * @memberof das_datum
 */
DAS_API char* das_datum_toStrValOnly(
	const das_datum* pThis, int nFracDigits, char* sStr, size_t uLen
);

/** Similar to das_datum_toStr, but can specify a separator for vectors.
 * The separator is ignored if the element type is not multi-valued */
DAS_API char* das_datum_toStrValOnlySep(
   const das_datum* pThis, int nFracDigits, const char* sSep, char* sStr, 
   size_t uLen
);


/** Get a datum value as a double
 *
 * Numeric items are converted to doubles, *with resolution loss* for
 * 64-bit integers. Text is parsed. A vtTime is refused since a structure
 * can't be returned. For vtTime types use das_datum_toEpoch() instead.
 *
 * @param pThis the datum to read
 * @param pOut receives the value, untouched on failure
 * @returns true on success, false on error.  das_error is called for a
 *          null datum and for value types with no numeric reading.
 * @memberof das_datum
 */
DAS_API bool das_datum_toDbl(const das_datum* pThis, double* pOut);

/** Read a datum's components as doubles, in storage order.
 *
 * The plural of das_datum_toDbl().  A scalar is the one-component case and
 * writes pOut[0].
 *
 * Output is in storage order.  Some vtComposite datums carry a math formalism
 * (@see DasForm) and these may have an expected order for elements, but this
 * function does not know or make use of DasForm pointers, even when they are
 * available in the datum.
 *
 * @param pThis the datum to read
 * @param pOut receives up to nMax components. slots from the return value up
 *        to nMax are not touched.
 * @param nMax the room in pOut
 * @returns the count written, or a negative das error code.
 * @memberof das_datum
 */
DAS_API int das_datum_toDoubles(
	const das_datum* pThis, double* pOut, int nMax
);


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
 *          makes no sense in the context of a datetime.
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
 *          makes no sense in the context of a datetime.
 * 
 * @memberof das_datum
 */
DAS_API bool das_datum_toTime(const das_datum* pThis, das_time* pDt);

/** Make a time datum from a das_time, the inverse of das_datum_toTime()
 *
 * The das_time is copied as is, with no text round trip, so seconds keep
 * full precision.  year, month and mday are used and yday is recomputed, so
 * the datum renders in either calendar form.  A leap second @b stays 60.x;
 * plain dt_tnorm() would roll it into the next minute.  Units are UNIT_UTC.
 *
 * @param pThis the datum to set
 * @param pDt the broken down time to copy
 *
 * @returns true, or false with pThis left null if pDt is NULL, in which
 *          case das_error is called.
 *
 * @memberof das_datum
 */
DAS_API bool das_datum_fromTime(das_datum* pThis, const das_time* pDt);



/* ************************************************************************* */
/* Ranges */

/** @addtogroup values
 * @{
 */

/** A half-open interval of values, [dmBeg, dmEnd).
 *
 * A range is just its two ends.  What the values are values OF, a time
 * coordinate, a spacecraft clock field, a property, is for the holder of the
 * range to know.  das_dim_range, below, pairs a range with a dimension name.
 *
 * Unlike a bare das_datum, a range may be kept: both datums own their
 * bytes, which das_range_fromDatum() enforces.  Copy one with plain (=).
 *
 * @par Open ends
 *
 * A null end (see das_datum_isNull()) is no bound on that side: a null dmBeg
 * runs from minus infinity, a null dmEnd to plus infinity.  Each end stands
 * on its own, so a range may be bounded, half-bounded or open at both ends.
 * A zeroed das_range is open at both ends, as is one from das_range_init().
 *
 * Test the ends with das_range_hasBeg() and das_range_hasEnd() before
 * reading either one.
 *
 * @code
 *   das_range r;
 *   das_range_fromUtc(&r, "2025-288", "2025-290");   // two days
 *   das_range_fromUtc(&r, "2025-288", "");           // 2025-288 onward
 *   das_range_init(&r);                              // everything
 * @endcode
 */
typedef struct das_range_t {
	das_datum dmBeg;      /* inclusive range begin, null if open              */
	das_datum dmEnd;      /* exclusive range end, null if open                */
} das_range;

/** @} */


/** Initialize a das_range that is open at both ends.
 *
 * @param pRng  Storage to initialise; all fields are overwritten.
 * @memberof das_range
 */
DAS_API void das_range_init(das_range* pRng);


/** Does this range have a lower bound?
 * @param pRng  The range to test.
 * @return false if dmBeg is null, true otherwise.
 * @memberof das_range
 */
DAS_API bool das_range_hasBeg(const das_range* pRng);


/** Does this range have an upper bound?
 * @param pRng  The range to test.
 * @return false if dmEnd is null, true otherwise.
 * @memberof das_range
 */
DAS_API bool das_range_hasEnd(const das_range* pRng);


/** Initialize a das_range of times from ISO-8601 UTC strings.
 *
 * Any format das_datum_fromStr() takes works here: "2025-10-01", "2025-288",
 * "2025-10-15T06:00".  An empty string leaves that end open.
 *
 * @param pRng  Storage to initialise; all fields are overwritten.
 * @param sBeg  Range begin (inclusive), or "" for no lower bound.
 * @param sEnd  Range end (exclusive), or "" for no upper bound.
 * @return DAS_OKAY on success, a positive error code on parse failure, or
 *         if a non-empty string parses to something other than a time.
 * @memberof das_range
 */
DAS_API DasErrCode das_range_fromUtc(
	das_range* pRng, const char* sBeg, const char* sEnd
);


/** Initialize a das_range of times from das_time structs.
 *
 * Each end is normalized as das_datum_fromTime() describes.
 *
 * @param pRng  Storage to initialise; all fields are overwritten.
 * @param dtBeg Inclusive range begin; copied.  NULL for no lower bound.
 * @param dtEnd Exclusive range end; copied.  NULL for no upper bound.
 * @memberof das_range
 */
DAS_API void das_range_fromTime(
	das_range* pRng, const das_time* dtBeg, const das_time* dtEnd
);


/** Initialize a das_range of integers.
 *
 * The values are stored as dimensionless doubles, exact up to 2^53.
 *
 * @param pRng    Storage to initialise; all fields are overwritten.
 * @param nBeg    Inclusive range begin.
 * @param nEnd    Exclusive range end.  How nBeg > nEnd is read is up to
 *                the consumer; uri.h takes it as a rollover.
 *
 * Both ends are bounded.  For an open end, start from das_range_init() or
 * use das_range_fromDatum() with a null datum.
 *
 * @memberof das_range
 */
DAS_API void das_range_fromInt(das_range* pRng, int64_t nBeg, int64_t nEnd);


/** Initialize a das_range from pre-built das_datum values.
 *
 * The datums are copied, so they must own their bytes: a datum that refers
 * to outside memory is refused.  A null datum, or a NULL pointer, leaves
 * that end open.
 *
 * @param pRng    Storage to initialise; all fields are overwritten.
 * @param dmBeg   Inclusive begin datum, null or NULL for no lower bound.
 * @param dmEnd   Exclusive end datum, null or NULL for no upper bound.
 * @return DAS_OKAY on success, a positive error code if a datum does not
 *         own its bytes.
 * @memberof das_range
 */
DAS_API DasErrCode das_range_fromDatum(
	das_range* pRng, const das_datum* dmBeg, const das_datum* dmEnd
);

/* ************************************************************************* */
/* Ranges on a named dimension */

/** @addtogroup values
 * @{
 */

/** A das_range and the dimension it constrains.
 *
 * sDim names a physical dimension, the id of a DasDim such as "time".
 * 
 * There are no restrictions on the name, so some components append a value 
 * sub field using a "." separator, such as "time.year".  In general "." is 
 * reserved for sub-fields in das2C as needed.
 *
 * An open range can still record it's associated dimension (and optional
 * value field) but it constrains nothing.
 * 
 * Fields are ordered so that a pointer to a das_dim_range, properly cast,
 * is a pointer to a das_range.
 * 
 * @see DasDim for more on physical dimensions in the data model.
 *
 * @code
 *   das_range rng;
 *   das_range_fromUtc(&rng, "2025-288", "2025-290");
 *
 *   das_dim_range dr;
 *   das_dim_range_init(&dr, "time", &rng);
 * @endcode
 */
typedef struct das_dim_range_t {
	das_range rng;          /* the bounds                                      */
	char      sDim[64];     /* dimension or field: "time", "sclk.mod64k"       */
} das_dim_range;

/** @} */


/** Name a range's dimension.
 *
 * @param pThis  Storage to initialise; all fields are overwritten.
 * @param sDim   Dimension or field name.  Stored lower case; must fit in 63
 *               chars.
 * @param pRng   The bounds, copied.  NULL for a range open at both ends.
 * @return DAS_OKAY on success, a positive error code if sDim is too long.
 *
 * @memberof das_dim_range
 */
DAS_API DasErrCode das_dim_range_init(
	das_dim_range* pThis, const char* sDim, const das_range* pRng
);

#ifdef __cplusplus
}
#endif

#endif	/* _das_datum_h_ */
