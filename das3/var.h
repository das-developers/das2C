/* Copyright (C) 2026 Chris Piker <chris-piker@uiowa.edu>
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

/** @file var.h Variables, the values of a dataset.
 *
 * Start with the DasVar description just below.  The shape helpers at the
 * end of the file read, write and combine the index shapes that
 * DasVar_shape() and DasDs_shape() report.
 */

#ifndef _das_var_h_
#define _das_var_h_

#include <das3/desc.h>
#include <das3/buffer.h>
#include <das3/datum.h>
#include <das3/units.h>
#include <das3/form.h>

#ifdef __cplusplus
extern "C" {
#endif

/** @addtogroup datamodel
 * @{
 */

/** @class DasVar var.h <das3/var.h>
 *
 * A variable: one quantity of a dataset, with a value at every index position
 *
 * Variables are the most involved class in das2C, so they get a longer
 * introduction than most.
 *
 * @par Index Spaces
 *
 * A dataset (DasDs) defines an index space, and every variable in the
 * dataset has a value at each position of that space.  To say that again:
 * if a dataset has the extent
 *
 *    i=0..10, j=0..20, k=0..30
 *
 * then for every legal triple (i,j,k), every variable in the dataset will
 * hand back a value through DasVar_get().
 *
 * The value at a position may be a single number, such as a length, or a
 * set of numbers, such as the components of a vector.  Multi-component
 * values bring their own indices, which das2C writes with capital letters.
 * Say a variable provides a quaternion at each position.  Its full index
 * set is then
 *
 *    i=0..10, j=0..20, k=0..30; I=0..4
 *
 * The four values of the quaternion are a new index, independent of the
 * dataset.  To keep the two apart, the dataset's indices are called the
 * "external" indices of a variable and its own are the "internal" indices.
 * A variable may have more than one internal index.  Rotations are usually
 * written as a 3x3 matrix, so a variable of rotations could have the index
 * set
 *
 *    i=0..10, j=0..20, k=0..30; I=0..3, J=0..3
 *
 * making it rank 5 in total.  DasVar_shape() reports the external extents
 * and DasVar_intrShape() the internal ones.
 *
 * @par Index Degeneracy
 *
 * The storage behind a variable need not be as big as the dataset.  Take a
 * spectrum analyzer dataset with the physical dimensions
 *
 *    time, frequency
 *
 * and say it has 1000 time points and 40 frequency channels.  Once the
 * whole stream is read the dataset's extent is
 *
 *    i=0..1000, j=0..40
 *
 * By the rule above, the variable holding the center frequency of each
 * channel has that same shape.  But there are only 40 frequencies, and they
 * are the same at every time.  So when asked for the frequency at (i,j) the
 * variable ignores i and answers from j alone.  In library terms the
 * variable is "degenerate" in i.  Its storage is only a 40 element array, and
 * nothing that reads through the DasVar abstraction has to know that.  In
 * Python terms, variables are broadcast over the whole index space of the
 * dataset for free.  If needed, DasVar_degenerate() says which indices a
 * variable ignores.
 *
 * @par Ragged Indices
 *
 * Not every index has one fixed extent.  A radar sounder may return 1400
 * samples in one record and 1380 in the next, so the extent along j
 * depends on i.  DasVar_shape() reports such an index as VARIDX_RAGGED, and
 * DasVar_lengthIn() gives the real extent at any given position.  The
 * VARIDX_* tokens are defined in array.h.
 *
 * @par Mathematical Rules
 *
 * Every numeric variable carries a formalism, a DasForm, that says what its
 * values mean to arithmetic.  The common one is "linear", which works the
 * way you expect: the values are plain numbers and may be added, subtracted,
 * multiplied and divided.  A related one is "point", used for date-times.
 * Two date-times may be subtracted to give an interval, which is a plain
 * linear value, and an interval may be added to a date-time, but two
 * date-times cannot be added together.  July 31st 2026 plus November 2nd
 * 1800 means nothing, so the operation is undefined.  Vectors, rotations and
 * the other formalisms das2C knows are listed in form.h.  DasVar_form()
 * returns a variable's formalism.
 *
 * @par Backing Storage
 *
 * A variable is an abstraction over wherever the values actually are.  The
 * backing store may be an array, or an equation that produces a value from
 * an index, or an operation on two other variables.  Code that reads a
 * variable never needs to know which.  It matters when constructing one:
 *
 * | Backed by         | Single valued    | Multi valued        |
 * |-------------------|------------------|---------------------|
 * | An array          | new_DasVarAry()  | new_DasVarCompAry() |
 * | A linear equation | new_DasVarSeq()  | new_DasVarCompSeq() |
 * | Two other vars    | new_DasVarBin()  | new_DasVarBin()     |
 *
 * new_DasVarBin() serves both columns since the operands decide the shape
 * of the result.  new_DasVarBytesAry() makes variables of text strings or
 * binary blobs, which have no math rules at all, and new_DasVarFrom() makes
 * a second variable over the values of a first with other units or another
 * formalism.
 *
 * das2C goes to some trouble to use as little RAM as it can for a
 * variable's values, which is why the bulk read calls may hand back a view
 * onto existing storage rather than a copy.  See DasVar_subset().
 *
 * @par Reading Values
 *
 * DasVar_get() reads one value at one position into a das_datum.  It works
 * for every variable, ragged or computed, but it is not fast.  The
 * DasVar_subset() family reads a whole range of positions into a new DasAry,
 * and is how bulk readers and language bindings get their data:
 *
 * @code
 DasAry* pAry = DasVar_allVals(pVar, DasDs_rank(pDs));
 if(pAry == NULL) handle_error();
 ...
 dec_DasAry(pAry);
 @endcode
 *
 * You need not take everything.  Give DasVar_subset() a range and it
 * returns a slice, say the counts from one anode of a particle detector out
 * of a time by energy by anode variable.  There is a worked example on
 * DasVar_subset().
 *
 * @par Ownership
 *
 * Variables are reference counted.  A constructor hands back one reference.
 * DasDim_addVar() does not add another; the dimension takes over the one
 * you have and releases it when the dataset is freed.  Variables read from
 * a stream belong to their dimension and live as long as the dataset does.
 *
 * @extends DasDesc
 */
typedef struct das_var DasVar;

/** The element this variable is written as: "scalar", "composite" or "bytes"
 * @memberof DasVar */
DAS_API const char* DasVar_serialType(const DasVar* pThis);

/** The variable's units
 * @memberof DasVar */
DAS_API das_units DasVar_units(const DasVar* pThis);

/** Is this variable an operation on two others, see new_DasVarBin()
 *
 * DasVarBin_op(), DasVarBin_left() and DasVarBin_right() answer only for
 * these.
 * @memberof DasVar */
DAS_API bool DasVar_isBin(const DasVar* pThis);

/** The variable's mathematical formalism, see form.h
 *
 * Every numeric variable has one.  A variable whose stream said nothing
 * about math has the linear form, and one whose kind das2C does not know
 * has DAS_FORM_EXT.
 *
 * @returns the formalism, owned by the variable, or NULL for a string or
 *          blob variable
 * @memberof DasVar */
DAS_API DasForm* DasVar_form(const DasVar* pThis);

/** Is this variable's formalism of the given kind?
 *
 * @code
 *     if(DasVar_formIs(pVar, DAS_FORM_POS)) ...
 * @endcode
 *
 * The kind tokens, DAS_FORM_VEC, DAS_FORM_POS and so on, are in the
 * form_*.h headers.
 *
 * @returns true if the kinds match, false for NULL and for strings and
 *          blobs, which have no formalism
 * @relates DasVar
 */
#define DasVar_formIs(P,VT) (((P) != NULL)&&DasForm_isKind(DasVar_form(P), VT))

/** The symbol for one component of a multi-component value
 *
 * This is what a plot needs to label N stacked lines without knowing what
 * kind of value it holds: "x", "y", "z", or "re" and "im", and so on.
 * Components count from zero in storage order.
 *
 * @param pThis the variable
 * @param iComp the component, counting from 0
 * @returns a constant string owned by the library, or NULL.  NULL is a
 *          normal answer: plain numbers, strings and blobs have no symbols,
 *          and a two component vector has none for component 2.
 * @memberof DasVar
 */
DAS_API const char* DasVar_compSym(const DasVar* pThis, int iComp);

/** One label per component, a convenience for output formats
 *
 * Every table or file writer has to come up with a column heading for
 * each component of a vector.  This is one reasonable answer.  Labels are
 * taken from, in order of preference:
 *
 *   1. a label property with one entry per component, used as given
 *   2. a single valued label, used as a stem with each component's symbol
 *      appended, as in "B_x"
 *   3. no label at all, in which case the dimension's name is the stem
 *
 * The label property may be on the variable or on its dimension.  A
 * component with no symbol gets its index instead, so the labels are always
 * distinct.  A scalar, string or blob is one column and gets one label.
 *
 * @param pThis the variable to label
 * @param psBuf nMax pointers, each to a buffer of at least uLenEa bytes
 * @param nMax how many buffers there are.  More components than this is an
 *        error, not a truncation.
 * @param uLenEa the size of each buffer
 * @returns the number of labels written, or a negative das error code
 * @memberof DasVar
 */
DAS_API int DasVar_compLabels(
	const DasVar* pThis, char** psBuf, int nMax, size_t uLenEa
);

/** The storage type of a single number in this variable, or etUByte for a
 * string or blob
 * @memberof DasVar */
DAS_API das_elem_type DasVar_elemType(const DasVar* pThis);


/** Add a reference to a variable
 * @returns the new reference count
 * @memberof DasVar */
DAS_API int inc_DasVar(DasVar* pThis);

/** Release a reference to a variable
 *
 * When the count reaches zero the variable releases whatever backs it and
 * frees itself.  Set your pointer to NULL afterwards.
 *
 * @returns the remaining reference count
 * @memberof DasVar */
DAS_API int dec_DasVar(DasVar* pThis);

/** The current reference count of a variable
 * @memberof DasVar */
DAS_API int ref_DasVar(const DasVar* pThis);

/** Read one value at one position as a datum
 *
 * This works for every variable: ragged arrays, equations, operations over
 * other variables.  A re-gridder that walks a pixel or voxel raster lives on
 * this call.  For a range of values at once use DasVar_subset().
 *
 * @par Where the value lives
 *
 * The datum you get back holds its value in one of three places:
 *
 *   - In its own bytes.  Every scalar works this way.
 *   - In the variable's storage.  An array backed vector, string or blob is
 *     not copied; the datum points at the array.  The pointer is good only
 *     while the variable lives.
 *   - In @a work, the scratch buffer you supply.  A vector computed from an
 *     equation or an operation has no storage to point at, so it lands here.
 *
 * Reading the datum is the same in all three cases.  If you want to keep
 * the value past the next call, copy it out.
 *
 * @par Sizing the scratch
 *
 * Nobody can say in advance how big a value is at a position you have not
 * visited yet; for a ragged variable it depends on the position.  So the
 * call tells you what it needed and you grow the buffer.  Start with a
 * stack buffer, grow only when told, and keep the larger one for the rest
 * of the walk:
 *
 * @code
 ubyte        aStack[128];
 das_byte_seq work   = { aStack, sizeof(aStack) };
 ubyte*       pGrown = NULL;
 das_datum    dm;
 DasDsIter    it;

 for(init_DasDsIter(&it, pDs); !it.done; DasDsIter_next(&it)){

    int nRet = DasVar_get(pVar, it.index, work, &dm);

    if(nRet > 0){                        // this value wants nRet bytes
       ubyte* pNew = realloc(pGrown, (size_t)nRet);
       if(pNew == NULL) break;
       pGrown = pNew;  work.ptr = pGrown;  work.sz = (size_t)nRet;
       nRet = DasVar_get(pVar, it.index, work, &dm);   // fits now
    }
    if(nRet != 0) break;                 // < 0 was already logged
    ...
 }
 free(pGrown);
 @endcode
 *
 * If you only read scalars, or only array backed data, pass an empty work
 * buffer and skip all of that.  DasVar_getNeedsBuf() tells you which case
 * you are in.
 *
 * @param pThis the variable to read
 * @param pLoc the position, one entry per external index.  Entries for
 *        indices the variable ignores are not read.
 * @param work scratch space, used only when the value has no storage to
 *        point at.  Pass @c {NULL,0} when you know you do not need it.
 * @param pOut receives the value
 *
 * @returns 0 on success, the number of scratch BYTES needed when @a work is
 *          too small, or a negative das error code after logging why not.
 * @memberof DasVar */
DAS_API int DasVar_get(
	const DasVar* pThis, const ptrdiff_t* pLoc, das_byte_seq work, das_datum* pOut
);

/** Will DasVar_get() ever need a scratch buffer for this variable?
 *
 * True only for a multi-component variable with no storage behind it, such
 * as a vector computed from an equation.  Most stream readers never see a
 * true here and can skip the buffer handling.
 * @memberof DasVar */
DAS_API bool DasVar_getNeedsBuf(const DasVar* pThis);

/** The variable's extent along each external index
 *
 * @param pThis the variable
 * @param pShape receives VARIDX_MAX entries, so there is no need to
 *        pre-fill the buffer.  Each is one of:
 *
 *        - a count, 0 or more: the valid index range
 *        - VARIDX_UNUSED: the variable ignores this index
 *        - VARIDX_RAGGED: the count depends on the other indices, ask
 *          DasVar_lengthIn()
 *        - VARIDX_BORROW: the variable takes whatever extent its dataset
 *          settles on, which a sequence may do
 *
 * @returns the external rank.  Entries past it are VARIDX_UNUSED.
 * @memberof DasVar */
DAS_API int DasVar_shape(const DasVar* pThis, ptrdiff_t* pShape);

/** The internal structure of one value
 *
 * A scalar has none.  A vector has one internal index of extent 3, a 3x3
 * matrix has two, a string has one over its bytes.
 *
 * @param pThis the variable
 * @param pShape receives one entry per internal index: a count, or
 *        VARIDX_RAGGED when the extent varies with position, as it does for
 *        most strings.  Make room for VARIDX_MAX - 1 entries.
 * @returns the internal rank, 0 for a scalar, in which case pShape is left
 *          alone
 * @memberof DasVar */
DAS_API int DasVar_intrShape(const DasVar* pThis, ptrdiff_t* pShape);

/** The extent along one external index at a given position
 *
 * For a ragged variable DasVar_shape() cannot say how many values there are
 * along j, since the answer depends on i.  This call can, given i.  For a
 * rectangular variable the answer is the same everywhere.
 *
 * @param pThis the variable
 * @param nIdx the index whose extent is wanted
 * @param pLoc the position of the indices before it, nIdx entries
 * @returns the number of positions along index nIdx there, VARIDX_UNUSED if
 *          the variable ignores that index, or VARIDX_BORROW for a sequence
 *          with no extent of its own
 * @see DasAry_lengthIn()
 * @memberof DasVar */
DAS_API ptrdiff_t DasVar_lengthIn(const DasVar* pThis, int nIdx, ptrdiff_t* pLoc);

/** Does this variable ignore the given external index?
 *
 * See "Index Degeneracy" in the DasVar description.
 *
 * @param pThis the variable
 * @param iIndex the index, from 0 to VARIDX_MAX - 1
 * @returns true if changing this index cannot change the value.  Any index
 *          past the variable's rank is degenerate too.
 * @memberof DasVar */
DAS_API bool DasVar_degenerate(const DasVar* pThis, int iIndex);

/** The role this variable plays in its dimension
 *
 * "center", "offset", "reference" and so on, as given to DasDim_addVar().
 * The DASVAR_* constants name the roles the library knows.
 *
 * @returns the role name, owned by the dimension, or NULL for a variable
 *          that is not in a dimension
 * @memberof DasVar */
DAS_API const char* DasVar_role(const DasVar* pThis);

/** Read a range of positions into an array, in the data's own shape
 *
 * This is the bulk read.  Values from an equation or an operation are
 * computed and stored in a new array.  Values that already sit in an array
 * may be handed back as a VIEW onto that array rather than a copy, which is
 * cheap but comes with strings attached: the memory belongs to the variable,
 * is good only while the variable lives, and must not be written to.
 * DasAry_ownsElements() on the result tells you whether you got a view.
 * DasVar_materialize() is the same call with a copy guaranteed.
 *
 * Ragged storage stays ragged.  If a sounder sent 1400 samples in one
 * record and 1380 in the next, the result says so, and DasAry_shape() on it
 * reports VARIDX_RAGGED.  If you need a rectangle, as a CDF writer or a
 * NumPy binding does, use DasVar_subsetQube() instead.  For storage that is
 * already rectangular the two calls return the same thing.
 *
 * The result holds single numbers, not assembled values.  The components of
 * a vector are a trailing array index, and so are the bytes of a string.
 * DasVar_get() is the call that returns one whole value.
 *
 * A variable is dataset shaped, so an index it ignores is still present in
 * the result, with the dataset's extent and the value repeated along it.
 * To get just the distinct values, pin that index to a single element with
 * pMin and pMax.  DasVar_degenerate() says which indices to pin.
 *
 * @par Taking a slice
 *
 * Say a particle detector reports counts over 1000 times, 50 energy steps
 * and 16 anodes, so the variable is 1000 x 50 x 16 in (i, j, k).  To get
 * everything anode 5 saw, pin k to [5, 6) and leave i and j alone:
 *
 * @code
 ptrdiff_t aShape[VARIDX_MAX];
 DasVar_shape(pCounts, aShape);                 // {1000, 50, 16}

 ptrdiff_t aMin[3] = { 0,         0,         5 };
 ptrdiff_t aMax[3] = { aShape[0], aShape[1], 6 };   // upper bounds exclusive

 DasAry* pAnode5 = DasVar_subset(pCounts, 3, aMin, aMax, NULL);
 if(pAnode5 == NULL) handle_error();
 ...
 dec_DasAry(pAnode5);
 @endcode
 *
 * An index pinned to one element drops out of the result, so pAnode5 is a
 * 1000 x 50 array of counts over time and energy.  To walk the result one
 * value at a time use DasAry_getAt() or the iterators in iter.h.
 *
 * @param pThis the variable to read
 * @param nRank the number of entries in pMin and pMax, which must equal the
 *        variable's external rank
 * @param pMin the inclusive lower bound of each index.  NULL, together with
 *        pMax, means the whole extent.
 * @param pMax the exclusive upper bound of each index, or NULL with pMin
 * @param pShapeFrom usually NULL.  A sequence may have no extent of its
 *        own along an index (VARIDX_BORROW), so asking it for "everything"
 *        has no answer.  Name a variable here that does know its extent,
 *        typically the one the sequence is paired with, and its shape is
 *        used instead.
 * @returns a new array, to release with dec_DasAry(), or NULL after logging
 *          why not.  A range of one value is refused since that is a rank 0
 *          array; use DasVar_get() for single values.
 * @memberof DasVar */
DAS_API DasAry* DasVar_subset(
	const DasVar* pThis, int nRank, const ptrdiff_t* pMin, const ptrdiff_t* pMax,
	const DasVar* pShapeFrom
);

/** The same values as DasVar_subset(), in memory you own outright
 *
 * Never a view.  The result may be written to and outlives the variable
 * and its arrays, so this is the call for data you mean to modify, keep
 * after freeing the dataset, or hand to another thread.  Same arguments as
 * DasVar_subset().
 *
 * @returns a new array owning its memory, or NULL after logging why not
 * @memberof DasVar */
DAS_API DasAry* DasVar_materialize(
	const DasVar* pThis, int nRank, const ptrdiff_t* pMin, const ptrdiff_t* pMax,
	const DasVar* pShapeFrom
);

/** The same values as DasVar_subset(), squared off into a rectangle
 *
 * Same arguments as DasVar_subset(), and the same result when the storage
 * is already rectangular.  Where it is ragged, the longest run along each
 * ragged index within the requested range is found and every shorter run is
 * padded out to it with the array's fill value.  So the shape you get
 * depends on the range you asked for.  Variable length strings are squared
 * off the same way, and the pad reads as the end of the string.
 *
 * Variable length blobs are refused.  No byte value means "not data" in a
 * blob, so padding would destroy each item's length.  Read those with
 * DasVar_subset() or one at a time with DasVar_get().
 *
 * @returns a new rectangular array, or NULL after logging why not.  May or
 *          may not own its memory, see DasAry_ownsElements().
 * @memberof DasVar */
DAS_API DasAry* DasVar_subsetQube(
	const DasVar* pThis, int nRank, const ptrdiff_t* pMin, const ptrdiff_t* pMax,
	const DasVar* pShapeFrom
);

/** A rectangle, in memory you own outright
 *
 * DasVar_subsetQube() and DasVar_materialize() at once: something
 * rectangular that a language binding can hand to its runtime and forget
 * about.  Same arguments as DasVar_subset().
 *
 * @returns a new rectangular array owning its memory, or NULL after logging
 *          why not
 * @memberof DasVar */
DAS_API DasAry* DasVar_materializeQube(
	const DasVar* pThis, int nRank, const ptrdiff_t* pMin, const ptrdiff_t* pMax,
	const DasVar* pShapeFrom
);

/** Every value of a variable, in its own shape.  R is the variable's
 * external rank.  See DasVar_subset().
 * @relates DasVar */
#define DasVar_allVals(P, R)  DasVar_subset((P), (R), NULL, NULL, NULL)


/** Can the values be converted to doubles?  True for numbers and times,
 * false for strings and blobs.
 * @memberof DasVar */
DAS_API bool DasVar_isNumeric(const DasVar* pThis);

/** A one line description of this variable, for people
 *
 * Three '|' separated sections: where the values come from, the index
 * ranges, and the formalism.  For example:
 *
 *    b_gse[i][I] float nT | i:2442, I:3 | vector frame=GSE
 *    (2018-01-01T00:00:00 + 0.001*i s) time UTC | i:2442 | point
 *    msg[i][I] ubyte | i:99, I:* | string
 *
 * A bare word up front is the id of a backing array, with one [x] per
 * array index, lowercase for external and uppercase for internal.  A
 * parenthesized expression is a computed value.  The format may change and
 * nothing parses it.
 *
 * @param pThis the variable
 * @param sBuf the output buffer, 256 bytes is plenty
 * @param uLen its size
 * @returns sBuf, or NULL if the whole line did not fit
 * @memberof DasVar */
DAS_API char* DasVar_toStr(const DasVar* pThis, char* sBuf, size_t uLen);

/** Copy a variable
 *
 * The copy gets its own properties and formalism and shares the backing
 * array, taking a reference on it.  The two are independent afterwards; in
 * particular DasVar_setAry() on one does not affect the other.  A copy of
 * an operation shares the operands.
 *
 * @returns a new variable with one reference
 * @memberof DasVar */
DAS_API DasVar* DasVar_copy(const DasVar* pThis);

/** The type of datum DasVar_get() produces
 *
 * vtComposite for any multi-component variable, vtText or vtByteSeq for a
 * string or blob, otherwise the element type.  The components of a
 * composite datum are read with das_datum_toDoubles().
 * @memberof DasVar */
DAS_API das_val_type DasVar_valType(const DasVar* pThis);

/** The array holding this variable's values, if there is one
 *
 * @returns the array, owned by the dataset, or NULL for a variable that
 *          computes its values from an equation or an operation.  NULL is
 *          not an error.
 * @memberof DasVar */
DAS_API DasAry* DasVar_getAry(const DasVar* pThis);

/** Does an array hold this variable's values?
 * @relates DasVar */
#define DasVar_hasAry(P)  (DasVar_getAry(P) != NULL)

/** How an array backed variable's external indices map to array indices
 *
 * @param pThis an array backed variable, see DasVar_hasAry()
 * @param pMap receives VARIDX_MAX entries.  Each is the array index that
 *        external index reads, or VARIDX_UNUSED for an index the variable
 *        ignores.  Entries past the external rank are VARIDX_UNUSED too.
 * @returns the external rank, or -1 for a variable with no backing array,
 *          leaving pMap alone
 * @memberof DasVar */
DAS_API int DasVar_idxMap(const DasVar* pThis, int8_t* pMap);

/** Point an array backed variable at a different array
 *
 * The old array is released and the new one referenced.  The index map is
 * kept, so the new array must have the same rank as the old.  The element
 * type and units are taken from the new array.  This is a stream filter's
 * tool: a binary to text converter hands in a das_time array in place of a
 * TT2000 integer array and the variable becomes UTC, ready to print.
 *
 * Codecs that read or write the old array are not touched.  Re-initialize
 * those yourself.
 *
 * @param pThis an array backed variable, see DasVar_hasAry()
 * @param pNew the replacement.  Same rank as the current array, and a plain
 *        numeric or time value type.  Byte, text and blob arrays are refused.
 * @returns true on success, false after logging why not
 * @memberof DasVar */
DAS_API bool DasVar_setAry(DasVar* pThis, DasAry* pNew);


/** Create a scalar variable backed by an array
 *
 * The common case: the values are already in a DasAry, usually one added
 * to the dataset with DasDs_addAry().
 *
 * @param pAry the array; a reference is added
 * @param nExtRank the external rank, normally the dataset's rank
 * @param pIdxMap nExtRank entries, each the array index that external index
 *        reads, or VARIDX_UNUSED for an index the variable ignores.  For a
 *        scalar the map has to account for every array index.  Array
 *        indices left over are components, see new_DasVarCompAry().
 * @param units the variable's units.  A ';' separated list of per component
 *        units is refused.
 * @param pForm the formalism.  It is copied, so you still own and free the
 *        one you passed.  Required: a plain number takes
 *        new_DasFormLin(), a time axis takes new_DasFormPt().
 * @returns a new variable with one reference, or NULL after logging why not
 * @memberof DasVar */
DAS_API DasVar* new_DasVarAry(
	DasAry* pAry, int nExtRank, const int8_t* pIdxMap, das_units units,
	DasForm* pForm
);

/** Create a scalar variable computed from a linear equation
 *
 * The value at external position (i, j, ...) is
 *
 *    pMinVal + i * pInterVal[0] + j * pInterVal[1] + ...
 *
 * @param et the element type: etLong, etFloat, etDouble or etTime
 * @param pMinVal one element of type et, the value at position zero
 * @param nExtRank the external rank
 * @param pInterVal nExtRank intervals, zero for an index the value does not
 *        change along.  For etTime each is a double in seconds; otherwise
 *        each has type et.
 * @param pExtShape nExtRank extents.  A count bounds the sequence and
 *        positions outside it are errors.  VARIDX_RAGGED or VARIDX_BORROW
 *        leave the extent to the dataset, and VARIDX_UNUSED marks an index
 *        the value ignores.
 * @param units the variable's units
 * @param pForm the formalism, copied.  NULL means new_DasFormLin(), the
 *        usual case.  A time axis takes new_DasFormPt().
 * @returns a new variable with one reference, or NULL after logging why not
 * @memberof DasVar */
DAS_API DasVar* new_DasVarSeq(
	das_elem_type et, const ubyte* pMinVal, int nExtRank,
	const ubyte* pInterVal, const ptrdiff_t* pExtShape, das_units units,
	DasForm* pForm
);

/** Create a scalar variable holding one value everywhere
 *
 * @param et the element type: etLong, etFloat or etDouble
 * @param pVal one element of type et
 * @param nExtRank the external rank
 * @param pExtShape nExtRank extents, as for new_DasVarSeq()
 * @param units the variable's units
 * @param pForm the formalism, copied, never NULL
 * @returns a new variable with one reference, or NULL after logging why not
 * @memberof DasVar */
DAS_API DasVar* new_DasVarConst(
	das_elem_type et, const ubyte* pVal, int nExtRank, const ptrdiff_t* pExtShape,
	das_units units, DasForm* pForm
);

/** Create a variable whose values are an operation on two others
 *
 * The usual case is a time axis built from a reference time and an offset:
 * reference + offset.  The operands' formalisms decide whether the pairing
 * makes sense and what comes out.  Adding two date-times is refused, as are
 * mismatched units or frames, and the error says why.  The operands must
 * have the same external rank.
 *
 * The operands are referenced, not copied, so the result follows a stream
 * as it arrives.  Whether the result is a scalar or multi-component depends
 * on the operands; a rotation times a vector gives a vector.
 *
 * An operation has no header form.  Its values are read like any other
 * variable's; to write them see DasVar_encode().
 *
 * @param pLeft the left operand; a reference is added
 * @param cOp '+', '-', '*', '/' or '^'.  Which of these a pairing defines
 *        depends on the formalisms.
 * @param pRight the right operand; a reference is added
 * @returns a new variable with one reference, or NULL after logging why not
 * @memberof DasVar */
DAS_API DasVar* new_DasVarBin(DasVar* pLeft, char cOp, DasVar* pRight);

/** The operator of an operation variable
 *
 * @param pThis a variable made by new_DasVarBin(), see DasVar_isBin()
 * @returns a D2BOP_* code from oper.h, which das_op_toStr() names, or
 *          D2OP_INVALID after logging an error if pThis is not an operation
 * @memberof DasVar */
DAS_API int DasVarBin_op(const DasVar* pThis);

/** The left operand of an operation variable
 *
 * @param pThis a variable made by new_DasVarBin(), see DasVarBin_op()
 * @returns the operand, owned by the operation, or NULL after logging an
 *          error if pThis is not an operation
 * @memberof DasVar */
DAS_API DasVar* DasVarBin_left(const DasVar* pThis);

/** The right operand of an operation variable
 *
 * @param pThis a variable made by new_DasVarBin(), see DasVarBin_op()
 * @returns the operand, owned by the operation, or NULL after logging an
 *          error if pThis is not an operation
 * @memberof DasVar */
DAS_API DasVar* DasVarBin_right(const DasVar* pThis);


/** Create a multi-component variable backed by an array
 *
 * As new_DasVarAry(), with the array indices the map does not account for
 * holding the components.  Those indices have to match pIntShape; the shape
 * is taken as given rather than worked out from the array, since you may
 * know a run the storage does not show, such as a one component value with
 * no array index for it.  A mismatch is reported when a value is read.
 *
 * The formalism is checked against the internal shape: a vector form wants
 * one internal index, a rotation wants 3x3, and so on.  The plain linear
 * form accepts any shape.
 *
 * @param pAry the array; a reference is added.  Its elements must be plain
 *        numbers, a composite of das_time values is refused.
 * @param nExtRank the external rank
 * @param pIdxMap the external to array index map, see new_DasVarAry()
 * @param units the variable's units, see new_DasVarAry()
 * @param pForm the formalism, copied, never NULL
 * @param nIntRank the internal rank, 1 for a vector, 2 for a matrix
 * @param pIntShape nIntRank extents, VARIDX_RAGGED for one that varies
 * @returns a new variable with one reference, or NULL after logging why not
 * @memberof DasVar */
DAS_API DasVar* new_DasVarCompAry(
	DasAry* pAry, int nExtRank, const int8_t* pIdxMap, das_units units,
	DasForm* pForm, int nIntRank, const ptrdiff_t* pIntShape
);

/** Create a multi-component variable computed component by component
 *
 * Each component is its own linear equation, see new_DasVarSeq().  The
 * internal shape is {nComps}.
 *
 * @param et the element type: etLong, etFloat or etDouble
 * @param nComps the number of components
 * @param pMinVals nComps elements of type et, one intercept per component
 * @param nExtRank the external rank
 * @param pInterVals nComps times nExtRank intervals of type et, all of
 *        component 0 first
 * @param pExtShape nExtRank extents, as for new_DasVarSeq()
 * @param units the variable's units
 * @param pForm the formalism, copied, never NULL
 * @returns a new variable with one reference, or NULL after logging why not
 * @memberof DasVar */
DAS_API DasVar* new_DasVarCompSeq(
	das_elem_type et, int nComps, const ubyte* pMinVals, int nExtRank,
	const ubyte* pInterVals, const ptrdiff_t* pExtShape, das_units units,
	DasForm* pForm
);

/** Create a text string or binary blob variable backed by an array
 *
 * Strings and blobs have no arithmetic, so there is no formalism argument.
 * The array holds the bytes.  The one array index the map leaves out is the
 * byte number within a value.
 *
 * @param pAry the array, element type etUByte; a reference is added
 * @param nExtRank the external rank
 * @param pIdxMap the external to array index map, see new_DasVarAry()
 * @param units the variable's units, usually NULL
 * @param bSentinel true for strings, which end in a NUL in storage so that
 *        a value can be used as a char*; false for blobs, which are handed
 *        out as a pointer and a length
 * @param nExtent the length of one value in bytes, or VARIDX_RAGGED when it
 *        varies
 * @returns a new variable with one reference, or NULL after logging why not
 * @memberof DasVar */
DAS_API DasVar* new_DasVarBytesAry(
	DasAry* pAry, int nExtRank, const int8_t* pIdxMap, das_units units,
	bool bSentinel, ptrdiff_t nExtent
);

/** Create a variable over the same values as another, with new meaning
 *
 * The new variable reads pSrc's values but has its own units and formalism.
 * Use it to state something the source did not, such as the frame of a
 * vector that arrived without one.  The values are not changed, so this
 * relabels, it does not transform.
 *
 * The class, internal shape and properties come from pSrc, as for
 * DasVar_copy().  The formalism is checked against that shape.  The two
 * variables are independent afterwards.
 *
 * @param pSrc the variable whose values are reused.  An operation is
 *        refused: its meaning comes from its operands, so build another
 *        with new_DasVarBin().
 * @param units the new units, DasVar_units(pSrc) to keep them
 * @param pForm the new formalism, copied, so you still own the one you
 *        passed.  NULL keeps pSrc's.  Must be NULL for a string or blob
 *        variable.
 * @returns a new variable with one reference, or NULL after logging why not
 * @memberof DasVar */
DAS_API DasVar* new_DasVarFrom(
	const DasVar* pSrc, das_units units, const DasForm* pForm
);

/* ------------------------------------------------------------------------- *
 * Shape helpers.  The VARIDX_* tokens are defined in array.h; these read,
 * write and combine shapes written in them.
 * ------------------------------------------------------------------------- */

/** Combine two shapes into the extent both can supply
 *
 * Index by index: a ragged extent wins over everything, otherwise the
 * smaller of two counts, a count over a borrowed or unused mark, and
 * borrowed over unused.  This is how a dataset finds the range every one of
 * its variables answers in.
 *
 * Shapes from DasAry_shape() are not suitable input.  An array marks an
 * unbounded index with ARYIDX_UNBOUND, which is 0 and reads here as an
 * extent of zero.
 *
 * @param nRank the number of entries to merge, at most VARIDX_MAX
 * @param pDest a shape as DasVar_shape() reports it, updated in place
 * @param pSrc the shape to merge into it
 */
DAS_API void das_varindex_merge(int nRank, ptrdiff_t* pDest, ptrdiff_t* pSrc);

/** Combine two lengths along one index, as DasVar_lengthIn() reports them
 *
 * Two counts give the smaller.  While a stream is still arriving different
 * variables have filled different amounts, and the smaller is the range
 * both can answer in; a mismatch is normal.  A count wins over any VARIDX_*
 * mark, and among the marks ragged wins over borrowed, which wins over
 * unused.
 */
DAS_API ptrdiff_t das_varlength_merge(ptrdiff_t nLeft, ptrdiff_t nRight);

/** Parse an index= or intern= attribute value
 *
 * The text is ';' separated with one entry per index: a count, '*' for a
 * ragged extent, '^' for an extent borrowed from the dataset, and '-' for an
 * index the variable does not use.
 *
 * @param sShape the text, for example "*;3"
 * @param nMaxRank the most entries pShape can hold
 * @param bAllowFlags false to refuse '^' and '-'.  Only a variable may use
 *        them; a dataset's own index= carries neither.
 * @param sWhere context for the error message, e.g. "<composite> in dataset 30"
 * @param pShape receives a count or VARIDX_* mark per entry
 * @param pnRank receives the number of entries
 * @returns DAS_OKAY, or an error code after logging why not.  The rank is
 *          not checked against anything; that is the caller's job.
 */
DAS_API DasErrCode das_shape_fromStr(
	const char* sShape, int nMaxRank, bool bAllowFlags, const char* sWhere,
	ptrdiff_t* pShape, int* pnRank
);

/** Write a shape as an index= or intern= attribute value, the inverse of
 * das_shape_fromStr()
 *
 * @param pShape the counts and VARIDX_* marks
 * @param nRank how many entries to write
 * @param sBuf the output buffer
 * @param uLen its size, including room for the NUL
 * @returns sBuf, or NULL if the text did not fit.  A cut shape does not
 *          parse back, so check before writing a header.
 */
DAS_API char* das_shape_toStr(
	const ptrdiff_t* pShape, int nRank, char* sBuf, size_t uLen
);

/** Render a shape for people, as the _info printers do
 *
 * One entry per index in use, for example "i:2442, j:*, I:3".  External
 * indices are lettered i, j, k and internal ones I, J, K.  Every index
 * starts at 0 so only the count is given, and '*' marks a ragged or
 * borrowed extent.  VARIDX_UNUSED entries are skipped.
 *
 * @param pShape the extents, external indices first, then internal
 * @param nExtRank how many leading entries are external
 * @param nShapeLen the total number of entries
 * @param sBuf the output buffer, may be NULL when uLen is 0
 * @param uLen its size, including room for the NUL
 * @returns the length of the full text, not counting the NUL, as snprintf()
 *          does.  sBuf is NUL terminated whenever uLen > 0.
 */
DAS_API int das_shape_info(
	const ptrdiff_t* pShape, int nExtRank, int nShapeLen, char* sBuf, size_t uLen
);

/** Set which external index das_shape_info() letters "i"
 *
 * By default the first index is i and the fastest moving one is printed
 * last, as C reads an array.  A column major caller (IDL, MATLAB, Fortran)
 * passes false so that its first index is still i.  The data layout is not
 * affected.
 *
 * Process global and not thread safe: set it once at startup.
 */
DAS_API void das_varindex_prndir(bool bFastLast);

/** @} */

#ifdef __cplusplus
}
#endif

#endif /* _das_var_h_ */
