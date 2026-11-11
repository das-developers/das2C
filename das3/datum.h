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
#include <das3/time.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Sized for the largest value that lives inline: a DasTime.  
   A composite objects point to external data, and the object needed
   to interpret them */
#define DASDATUM_BUF_SZ 32
	
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
 * DasDatum_isLocal() answers.  Locality is a property of the *value type*,
 * not of the size: a local datum owns its bytes outright and may be copied
 * and kept for as long as its holder likes.  A non-local datum boxes a
 * pointer to memory somebody else owns -- an array, a decode buffer -- and is
 * good only while that memory is.  A DASVT_TEXT datum is smaller than a local
 * DASVT_TIME one and is still a reference.
 *
 * Any interface that keeps a datum past the call it arrived on must therefore
 * check DasDatum_isLocal() and refuse what it cannot hold.
 *
 * @par Null datums
 *
 * A datum of type DASVT_NULL is null: no value at all, not even a typed one.
 * DasDatum_isNull() tests for it and DasDatum_setNull() makes one.  Null
 * prints as an empty string, and an empty string parses back to null.
 *
 * A zeroed block of memory is a legal null datum, so a structure holding
 * datums may be cleared with memset() or "= {0}".  Datums the library makes
 * carry DASUNITS_DIMENSIONLESS rather than a NULL units pointer.
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
 * DasDatum dm;
 * DasDatum_fromStr(&dm, "2017-01-02T12:14");
 * 
 * int year = ((DasTime*)&dm)->year;
 * 
 * DasDatum_fromStr(&dm, "2.145 meters");
 * double length = *((double*)&dm);
 * 
 * @endcode
 * 
 * The datum class is made to work with Variable, to provide a single
 * "value" of a variable, however these values may contain internal
 * structure.  Two prime examples are geometric vectors and strings.
 */
struct das_form_t;   /* opaque here; datum.c never dereferences one */

/* The DASVT_COMP box -- its struct and its bit layout -- is private to
   datum.c.  Unlike a DasTime or a double, which a caller may reasonably cast
   a datum pointer to, a composite is not a value you read by looking at it:
   the extents live in two places depending on size, and the run belongs to
   whoever supplied it.  Use the accessors below.  Nothing is lost by hiding
   it and the representation stays free to change. */


typedef struct das_datum_t {
   uint8_t bytes[DASDATUM_BUF_SZ]; /* 32 bytes of space */
   das_vt vt;
   uint32_t vsize;
   DasUnits units;
} DasDatum;

/** @} */

/** General datum initialization 
 * 
 * @param pThis The datum to initialize
 * 
 * @param pSrc A pointer to the source location.  If the source bytes are
 *        less then DASDATUM_BUF_SZ they are copied internally, otherwise
 *        the pointer itself is stored in the first member of the struct.
 * 
 * @param vt The value type, one of the 15 types listed in the das_vt
 *        enum.
 * 
 * @param vsize The value size, for anything other then DASVT_UNK, DASVT_BYTESEQ 
 *        and DASVT_TEXT this parameter is ignored.
 * 
 * @param units The units associated with the value.
 * 
 * @memberof DasDatum
 */
DAS_API void DasDatum_set(
   DasDatum* pThis, const uint8_t* pSrc, das_vt vt, uint32_t vsize, 
   DasUnits units
);

/** Make a null datum.
 *
 * The bytes are zeroed, the type is DASVT_NULL, vsize is 0 and the units are
 * DASUNITS_DIMENSIONLESS.  A zeroed datum is also null; this is the form the
 * library writes.
 *
 * @param pThis the datum to set
 * @memberof DasDatum
 */
DAS_API void DasDatum_setNull(DasDatum* pThis);

/** Is this the null datum?
 *
 * Only the type is checked, so zeroed memory is null.  Builds without
 * NDEBUG also log "inconsistent null datum" at debug level for a DASVT_NULL
 * datum with non-zero bytes or vsize; with NDEBUG defined there is no check
 * beyond the type.
 *
 * @param pThis the datum to test
 * @return true if pThis has type DASVT_NULL
 * @memberof DasDatum
 */
DAS_API bool DasDatum_isNull(const DasDatum* pThis);

/** Some datums have extended storage such as byte strings and composite
 * runs.  Get the fundamental element type for a datum.
 * @memberof DasDatum*/
DAS_API das_vt DasDatum_et(const DasDatum* pThis);

/** Box a caller-owned run as a DASVT_COMP datum.
 *
 * pRun must outlive the datum; nothing is copied.  The extents are stored
 * inline when they fit, in which case pShape need not survive the call --
 * DasDatum_shape() is the only thing that knows which way they went.
 *
 * @param pThis the datum to fill
 * @param pForm the formalism that gives the run meaning, never NULL
 * @param pRun the elements, which must outlive the datum
 * @param nRank the run's internal rank
 * @param pShape the extents, nRank of them
 * @param et what one element holds
 * @param units the run's units
 * @returns false on a loud error.  @memberof DasDatum */
DAS_API bool DasDatum_wrapElems(
	DasDatum* pThis, const struct das_form_t* pForm, const uint8_t* pRun,
	int nRank, const ptrdiff_t* pShape, das_vt et, DasUnits units
);

/** The formalism behind a DASVT_COMP datum, or NULL if it is not one.
 * Opaque here on purpose; datum.c never dereferences it.  @memberof DasDatum */
DAS_API const struct das_form_t* DasDatum_form(const DasDatum* pThis);

/** The element run behind a DASVT_COMP datum, or NULL.
 * A view into caller-owned storage, valid only while that storage is.
 * @memberof DasDatum */
DAS_API const uint8_t* DasDatum_elems(const DasDatum* pThis);

/** The shape of one datum's value: rank, and the extents at that rank.
 *
 * Total over every datum type, so a caller may ask without first knowing what
 * it holds:
 *
 *   a simple value  rank 0, nothing written
 *   DASVT_TEXT          rank 1, the character count, exactly what strlen reports
 *   DASVT_BYTESEQ       rank 1, the byte count
 *   DASVT_COMP     the internal rank, and the extents at each level
 *
 * A DASVT_TEXT extent counts characters and not the terminator: the NUL that
 * DASARY_AS_STRING puts in an array is das2C's storage choice, not part of the
 * encoded value, so the backing array reports one more than this does.
 *
 * A composite stores small extents one way and large or ragged ones another;
 * this is the only thing that knows which, so the two cannot drift apart.
 *
 * @param pShape receives the extents, room for at least DASIDX_MAX, or NULL
 *        for a caller that only wants the rank
 * @returns the rank, 0 for a scalar.  @memberof DasDatum */
DAS_API int DasDatum_shape(const DasDatum* pThis, ptrdiff_t* pShape);

/** Total elements in the run, the product of the extents.
 * @returns the count, 1 for a scalar, 0 if any extent is ragged.
 * @memberof DasDatum */
DAS_API size_t DasDatum_numElems(const DasDatum* pThis);

/** The bytes a caller must supply to hold one item of this datum's value.
 *
 * The question a loop asks before it starts: can my stack array hold what is
 * coming, or do I need to allocate?  Answered from a datum a caller already
 * has, so it costs no second call into the variable layer.
 *
 * @returns the byte count, 0 for a value that needs no caller storage (any
 *          datum that is not a composite).  @memberof DasDatum */
DAS_API size_t DasDatum_elemsBytes(const DasDatum* pThis);

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
 * @relates DasDatum
 */
#define DasDatum_isValid(p) ((p)->vsize > 0)

/** Does this datum own its bytes, or is it a reference?
 *
 * True means the value sits in the datum's own storage: copy it, keep it,
 * park it in a structure that outlives the caller.  False means the datum
 * boxes a pointer and is only good while the pointed-at memory is.
 *
 * @param p Constant pointer to the datum to check.
 * @relates DasDatum
 */
#define DasDatum_isLocal(p) \
   ( ((p)->vt >= DASVT_MIN_LOCAL) && ((p)->vt <= DASVT_MAX_LOCAL) )


/** Initialize a numeric datum from a value and units string.  
 *
 * Note that this function will not initialize text datums.  This is because
 * text datums only carry a const char* pointer, but not the string itself.
 * Call DasDatum_wrapStr() to make text datums.
 *
 * An empty string, or one of only whitespace, makes the null datum and
 * counts as parseable.
 *
 * @param pThis pointer to the datum structure to initialize
 * @param sStr the value plus it's units.
 * @return true if the string was parseable as a datum, false otherwise.
 * @memberof DasDatum
 */
DAS_API bool DasDatum_fromStr(DasDatum* pThis, const char* sStr);

/** Create a datum from a double value and units
 * 
 * This is the most commonly used type of datum, followed by time datums
 * 
 * @param pThis
 * @param value
 * @param units
 * @return Always returns true.
 * @memberof DasDatum
 */
DAS_API bool DasDatum_fromDouble(DasDatum* pThis, double value, DasUnits units);

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
 * DasUnits city = DasUnits_fromStr("city");
 * DasDatum locations[4];
 * for(int i = 0; i < 4; ++i)
 *     DasDatum_wrapStr(locations + i, cities[i], city);
 * @endcode
 * @memberof DasDatum
 */
DAS_API bool DasDatum_wrapStr(DasDatum* pThis, const char* sStr, DasUnits units);


/** Wrap an external unknown type pointer as a datum.
 *
 * This is for special user defined data types unknown to das2C.  The type
 * of the datum will be DASVT_BYTESEQ (a byte sequence)
 * @memberof DasDatum
 */
DAS_API bool DasDatum_wrapBytes(
	DasDatum* pThis, DasCByteSeq seq, DasUnits units
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
 * @memberof DasDatum
 */
DAS_API char* DasDatum_toStr(
	const DasDatum* pThis, int nFracDigits, char* sStr, size_t uLen
);

/** Same as DasDatum_toStr, but never print the units
 * 
 * @see DasDatum_toStr
 * @memberof DasDatum
 */
DAS_API char* DasDatum_toValStr(
	const DasDatum* pThis, int nFracDigits, char* sStr, size_t uLen
);

/** Similar to DasDatum_toStr, but can specify a separator for vectors.
 * The separator is ignored if the element type is not multi-valued */
DAS_API char* DasDatum_toValStrSep(
   const DasDatum* pThis, int nFracDigits, const char* sSep, char* sStr, 
   size_t uLen
);


/** Get a datum value as a double
 *
 * Numeric items are converted to doubles, *with resolution loss* for
 * 64-bit integers. Text is parsed. A DASVT_TIME is refused since a structure
 * can't be returned. For DASVT_TIME types use DasDatum_toPoint() instead.
 *
 * @param pThis the datum to read
 * @param pOut receives the value, untouched on failure
 * @returns true on success, false on error.  daserrf is called for a
 *          null datum and for value types with no numeric reading.
 * @memberof DasDatum
 */
DAS_API bool DasDatum_toDouble(const DasDatum* pThis, double* pOut);

/** Read a datum's components as doubles, in storage order.
 *
 * The plural of DasDatum_toDouble().  A scalar is the one-component case and
 * writes pOut[0].
 *
 * Output is in storage order.  Some DASVT_COMP datums carry a math formalism
 * (@see DasForm) and these may have an expected order for elements, but this
 * function does not know or make use of DasForm pointers, even when they are
 * available in the datum.
 *
 * @param pThis the datum to read
 * @param pOut receives up to nMax components. slots from the return value up
 *        to nMax are not touched.
 * @param nMax the room in pOut
 * @returns the count written, or a negative das error code.
 * @memberof DasDatum
 */
DAS_API int DasDatum_toDoubles(
	const DasDatum* pThis, double* pOut, int nMax
);


/** Get a time datum value as a double at a given epoch an scale
 *
 * @param pThis pointer to the value to convert
 * @param units The desired time base units, expects one of DASUNITS_US2000 and 
 *              friends.  Do not use DASUNITS_UTC, that's only ment for broken
 *              down time values.
 * @param pResult Pointer to location to store the converted value.
 *
 * @returns true if the conversion was successful, false otherwise.
 *          daserrf is called if the value type of the datum
 *          makes no sense in the context of a datetime.
 * 
 * @memberof DasDatum
 */
DAS_API bool DasDatum_toPoint(
	const DasDatum* pThis, DasUnits epoch, double* pResult
);

/** Get a time datum value as a DasTime
 * 
 * @param pThis a pointer to the datum to convert
 * @param pDt a pointer to a DasTime to hold the result
 * 
 * @returns true if the conversion was successful, false otherwise.
 *          daserrf is called if the value type of the datum
 *          makes no sense in the context of a datetime.
 * 
 * @memberof DasDatum
 */
DAS_API bool DasDatum_toTime(const DasDatum* pThis, DasTime* pDt);

/** Make a time datum from a DasTime, the inverse of DasDatum_toTime()
 *
 * The DasTime is copied as is, with no text round trip, so seconds keep
 * full precision.  year, month and mday are used and yday is recomputed, so
 * the datum renders in either calendar form.  A leap second @b stays 60.x;
 * plain DasTime_normalize() would roll it into the next minute.  Units are DASUNITS_UTC.
 *
 * @param pThis the datum to set
 * @param pDt the broken down time to copy
 *
 * @returns true, or false with pThis left null if pDt is NULL, in which
 *          case daserrf is called.
 *
 * @memberof DasDatum
 */
DAS_API bool DasDatum_fromTime(DasDatum* pThis, const DasTime* pDt);



/* ************************************************************************* */
/* Ranges */

/** @addtogroup values
 * @{
 */

/** A half-open interval of values, [begin, end).
 *
 * A range is just its two ends.  What the values are values OF, a time
 * coordinate, a spacecraft clock field, a property, is for the holder of the
 * range to know.  DasDimRng, below, pairs a range with a dimension name.
 *
 * Unlike a bare DasDatum, a range may be kept: both datums own their
 * bytes, which DasRng_fromDatum() enforces.  Copy one with plain (=).
 *
 * @par Open ends
 *
 * A null end (see DasDatum_isNull()) is no bound on that side: a null begin
 * runs from minus infinity, a null end to plus infinity.  Each end stands
 * on its own, so a range may be bounded, half-bounded or open at both ends.
 * A zeroed DasRng is open at both ends, as is one from DasRng_setOpen().
 *
 * Test the ends with DasRng_hasBeg() and DasRng_hasEnd() before
 * reading either one.
 *
 * @code
 *   DasRng r;
 *   DasRng_fromIso(&r, "2025-288", "2025-290");   // two days
 *   DasRng_fromIso(&r, "2025-288", "");           // 2025-288 onward
 *   DasRng_setOpen(&r);                              // everything
 * @endcode
 */
typedef struct das_rng_t {
	DasDatum begin;    /* inclusive range begin, null if open              */
	DasDatum end;      /* exclusive range end, null if open                */
} DasRng;

/** @} */


/** Initialize a DasRng that is open at both ends.
 *
 * @param pRng  Storage to initialise; all fields are overwritten.
 * @memberof DasRng
 */
DAS_API void DasRng_setOpen(DasRng* pRng);


/** Does this range have a lower bound?
 * @param pRng  The range to test.
 * @return false if begin is null, true otherwise.
 * @memberof DasRng
 */
DAS_API bool DasRng_hasBeg(const DasRng* pRng);


/** Does this range have an upper bound?
 * @param pRng  The range to test.
 * @return false if end is null, true otherwise.
 * @memberof DasRng
 */
DAS_API bool DasRng_hasEnd(const DasRng* pRng);


/** Initialize a DasRng of times from ISO-8601 UTC strings.
 *
 * Any format DasDatum_fromStr() takes works here: "2025-10-01", "2025-288",
 * "2025-10-15T06:00".  An empty string leaves that end open.
 *
 * @param pRng  Storage to initialise; all fields are overwritten.
 * @param sBeg  Range begin (inclusive), or "" for no lower bound.
 * @param sEnd  Range end (exclusive), or "" for no upper bound.
 * @return DAS_OKAY on success, a positive error code on parse failure, or
 *         if a non-empty string parses to something other than a time.
 * @memberof DasRng
 */
DAS_API das_errcode DasRng_fromIso(
	DasRng* pRng, const char* sBeg, const char* sEnd
);


/** Initialize a DasRng of times from DasTime structs.
 *
 * Each end is normalized as DasDatum_fromTime() describes.
 *
 * @param pRng  Storage to initialise; all fields are overwritten.
 * @param dtBeg Inclusive range begin; copied.  NULL for no lower bound.
 * @param dtEnd Exclusive range end; copied.  NULL for no upper bound.
 * @memberof DasRng
 */
DAS_API void DasRng_fromTime(
	DasRng* pRng, const DasTime* dtBeg, const DasTime* dtEnd
);


/** Initialize a DasRng of integers.
 *
 * The values are stored as dimensionless doubles, exact up to 2^53.
 *
 * @param pRng    Storage to initialise; all fields are overwritten.
 * @param nBeg    Inclusive range begin.
 * @param nEnd    Exclusive range end.  How nBeg > nEnd is read is up to
 *                the consumer; uri.h takes it as a rollover.
 *
 * Both ends are bounded.  For an open end, start from DasRng_setOpen() or
 * use DasRng_fromDatum() with a null datum.
 *
 * @memberof DasRng
 */
DAS_API void DasRng_fromInt(DasRng* pRng, int64_t nBeg, int64_t nEnd);


/** Initialize a DasRng from pre-built DasDatum values.
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
 * @memberof DasRng
 */
DAS_API das_errcode DasRng_fromDatum(
	DasRng* pRng, const DasDatum* dmBeg, const DasDatum* dmEnd
);

/* ************************************************************************* */
/* Ranges on a named dimension */

/** @addtogroup values
 * @{
 */

/** A DasRng and the dimension it constrains.
 *
 * physDim names a physical dimension, the id of a DasDim such as "time".
 * 
 * There are no restrictions on the name, so some components append a value 
 * sub field using a "." separator, such as "time.year".  In general "." is 
 * reserved for sub-fields in das2C as needed.
 *
 * An open range can still record it's associated dimension (and optional
 * value field) but it constrains nothing.
 * 
 * Fields are ordered so that a pointer to a DasDimRng, properly cast,
 * is a pointer to a DasRng.
 * 
 * @see DasDim for more on physical dimensions in the data model.
 *
 * @code
 *   DasRng rng;
 *   DasRng_fromIso(&rng, "2025-288", "2025-290");
 *
 *   DasDimRng dr;
 *   DasDimRng_set(&dr, "time", &rng);
 * @endcode
 */
typedef struct das_dimrng_t {
	DasRng range;          /* the bounds                                      */
	char   physDim[64];    /* dimension or field: "time", "sclk.mod64k"       */
} DasDimRng;

/** @} */


/** Name a range's dimension.
 *
 * @param pThis  Storage to initialise; all fields are overwritten.
 * @param sDim   Dimension or field name.  Stored lower case; must fit in 63
 *               chars.
 * @param pRng   The bounds, copied.  NULL for a range open at both ends.
 * @return DAS_OKAY on success, a positive error code if sDim is too long.
 *
 * @memberof DasDimRng
 */
DAS_API das_errcode DasDimRng_set(
	DasDimRng* pThis, const char* sDim, const DasRng* pRng
);

#ifdef __cplusplus
}
#endif

#endif	/* _das_datum_h_ */
