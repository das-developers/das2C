/* Copyright (C) 2026 Chris Piker <chris-piker@uiowa.edu>
 *
 * Author: C. Piker, via Claude Fable 5
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

/** @file set.h The DasSet layer: named value sets with formalisms.
 *
 * The successor to variable.h, which is now out of the build entirely.  What
 * remains is the mechanical rename back to DasVar once the consumers are ready.
 * XML snippets show the stream form of each type right next to the struct that
 * carries it.
 *
 * A word that recurs here: @b affine.  The point formalism marks ABSOLUTE
 * values, positions measured from an agreed origin, calendar time being the
 * canonical case.  For such values differences are meaningful but sums are
 * not: point - point = interval, point + interval = point, and point + point
 * is refused (adding two calendar dates means nothing).  Mathematics calls a
 * space with exactly these rules affine, and the word is used in that precise
 * sense throughout this layer.  Measurement theory calls the same idea an
 * interval scale, but das uses "interval" for the difference quantity itself,
 * so affine it is.
 *
 * Design record: schema/das2c_operations.md for the wire side,
 * co_notes/libdas_ops_class_spec.md for the C side.
 */

#ifndef _das_set_h_
#define _das_set_h_

#include <das2/descriptor.h>
#include <das2/buffer.h>
#include <das2/datum.h>
#include <das2/units.h>
#include <das2/generator.h>
#include <das2/form.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ========================================================================= *
 * THE FOUR AXES
 *
 * A variable, here a DasSet, is described by four axes that today's das_val_type
 * plus a semantic string tangle together.  Pulling them apart is the point of
 * this refactor.
 *
 *   Axis A, the generator (gt).  How the values are produced: array, sequence,
 *           constant, operation.  Lives in generator.h.  A set OWNS one.
 *
 *   Axis B, the element type (et).  What one cell holds.  Lives in generator.h.
 *           A pinned, restricted subset of vt.
 *
 *   Axis C, the structure.  Carried by the C CLASS, not a field, and there is
 *           one class per wire element: a bare DasSet has no internal index
 *           (<scalar>), a DasCompSet has a numeric component run
 *           (<composite>), a DasByteSet has a byte run (<bytes>).  Ask the
 *           class, never a tag.  The RICH presentation vocabulary (vector,
 *           complex, rotation...) is NOT stored: it is a view computed from
 *           C + D, because for composites the formalism fully determines it
 *           and duplicated facts drift.
 *
 *   Axis D, the formalism.  What math the values obey: none, point, geovec,
 *           complex, rotation.  A set HAS one, symmetric with the generator:
 *           pGen says how values are produced, form says what they mean to
 *           arithmetic.  On the wire this is the skippable <ops kind="..."/>
 *           element; in C it is a refcounted DasForm, one class per
 *           formalism, reached through pForm.  See form.h.
 *
 *   Semantic is NOT a carried axis.  It is a DIRECTIVE, consumed by the
 *   reader at parse time to pick a text parse target, then spent.  With the
 *   structural families now named by the wire element, semantic no longer does
 *   structural work; its vocabulary shrinks to the pure interpretation set
 *   { bool, datetime, integer, real }.  See the wire notes below.
 *
 * The word "formalism" is deliberately broader than "algebra": it houses the
 * true algebras (point, geovec, complex, rotation) and also rules that are not
 * strictly algebra in this context, such as a point spread function sampling
 * response.
 * ========================================================================= */
/* Axis D used to live here as an embedded das_formalism plus a static row
   table.  It is now DasForm in form.h: heap, refcounted, vtable dispatched,
   one file per formalism.  das_pres_type went with it -- the rich vocabulary
   (vector, complex, rotation) is what a form's datumType() answers, and
   duplicating it in an enum here is how the two drift.

   What survives of that design, in form.h:
     das_form_kind row   ->  DasForm_VTbl
     das_formalism       ->  DasForm, and its parameter store became typed
                             fields on each derived form
     das_form_rule table ->  binOpLeft / binOpRight, two vtable slots
     das_form_op         ->  D2BOP_* from operator.h, one op vocabulary
*/
/* ========================================================================= *
 * DasSet, the base.  Supersedes DasVar.
 * ========================================================================= */

typedef struct das_set DasSet;

/* ========================================================================= *
 * The scalar case.  One value per point, no internal index.  Supersedes the
 * scalar case of DasVarAry.  There is NO DasScalarSet struct: a scalar is
 * just a DasSet with no internal index, and its only formalisms so far are
 * linear or point.
 *
 * Wire, a plain linear scalar.  Linear is the default, so nothing is stated:
 *
 *   <scalar semantic="real" units="Hz" index="*">
 *     <packet numItems="1" itemBytes="8" encoding="LEreal"/>
 *   </scalar>
 *
 * Wire, a time scalar.  This is where the word "point" appears.  The affine
 * rule needs no parameters, so kind= alone carries it.  semantic="datetime"
 * still rides along as the display and calendar-units directive.  Child order is
 * properties, then ops, then generator:
 *
 *   <scalar semantic="datetime" units="TT2000" index="*">
 *     <ops kind="point"/>
 *     <packet numItems="1" itemBytes="8" encoding="LEint"/>
 *   </scalar>
 *
 * A future parameter-bearing scalar formalism (a scalar with a point spread
 * function) is no longer an open item: it is a formalism with parameters on a
 * scalar, exactly like geovec on a composite.  Same struct, same store.
 * ========================================================================= */

typedef struct DasSet_VTbl {

	/* Read one value at a full external index into a datum.  For a composite
	   this packs the whole item, a vector or a complex, into one datum.  For a
	   generic composite on a table miss it returns the run as plain numbers. */
	bool (*get)(const DasSet* pThis, ptrdiff_t* pLoc, das_datum* pOut);

	/* The wire element this class serializes as: "scalar", "composite",
	   "bytes".  Axis C lives on the class, so the serializer asks the class
	   rather than deriving structure back out of a presentation vocabulary. */
	const char* (*element)(const DasSet* pThis);

	das_elem_type (*elemType)(const DasSet* pThis);   /* asks the generator */

	int (*shape)(const DasSet* pThis, ptrdiff_t* pShape);      /* full shape */
	int (*intrShape)(const DasSet* pThis, ptrdiff_t* pShape);  /* internal only */

	char* (*expression)(const DasSet* pThis, char* sBuf, int nLen, unsigned int uFlags);

	bool (*isNumeric)(const DasSet* pThis);

	int     (*incRef)(DasSet* pThis);
	int     (*decRef)(DasSet* pThis);
	DasSet* (*copy)(const DasSet* pThis);

} DasSet_VTbl;


struct das_set {

	DasDesc base;          /* a set IS a descriptor: it has properties, a
	                          parent, and it serializes */

	const DasSet_VTbl* pVTbl;

	DasGen* pGen;          /* Axis A.  The set owns a generator but is not one.
	                          Swapping array for sequence does not change the
	                          set's type. */


	DasForm* pForm;        /* Axis D.  Heap owned and refcounted, symmetric
	                          with pGen: pGen says how values are produced,
	                          pForm says what they mean to arithmetic.  NEVER
	                          NULL for a numeric set -- an absent <ops> binds
	                          the explicit linear form and an unrecognized
	                          kind= binds DasFormGeneric, so "I do not know
	                          this math" has a vtable to refuse from.  Only
	                          DasByteSet leaves it NULL: a byte run has no
	                          auto-math and its CLASS says so. */

	/* One unit. Some composite types may need more, infact the 
	   the operations set may even demand it. */
	das_units units;

	int   nRef;
	void* pUser;
};

#define DasSet_element(P)  ((P)->pVTbl->element(P))
#define DasSet_units(P)    ((P)->units)
#define DasSet_gen(P)      ((P)->pGen)
#define DasSet_form(P)     ((P)->pForm)

/** What one cell of this set holds; forwards to the generator, where Axis B
 * actually lives.  Never cached -- DasSet_setArray() re-tags it.
 * @memberof DasSet */
#define DasSet_elemType(P) ((das_val_type)DasGen_elemType((P)->pGen))

/** Snapshot this set's facts for the formalism layer.
 *
 * A form never reaches up into a DasSet; it is handed one of these, built
 * from live facts at the moment of the call.  That is what keeps form_*.c
 * free of set.h and what makes a stale cached element type impossible.
 *
 * @param pThis the set to describe
 * @param pOut receives the snapshot
 * @returns false if the set has no formalism (a byte run), which is also the
 *          answer to "may this participate in arithmetic".
 * @memberof DasSet */
DAS_API bool DasSet_operand(const DasSet* pThis, das_operand* pOut);

/** The largest item run the binary-operation walker will stack.
 *
 * A DasBinSet holds one run from each operand plus one result on the stack per
 * item, so the runs need a fixed ceiling.  256 bytes covers everything the
 * formalisms define -- a 3;3 rotation in doubles is 72 -- and a run over it
 * fails loud rather than smashing the frame. */
#define DASBIN_RUN_MAX 256

/** Read ONE item run at one external location into a caller's buffer.
 *
 * The accessor between DasSet_get(), which assembles a whole datum, and
 * DasGen_eval(), which is a generator call the walker has no business making
 * on somebody else's operand.  Arithmetic needs the raw cells, and a nested
 * operation needs to ask its operands for them without knowing whether those
 * are array backed or computed.
 *
 * @param pThis the set to read
 * @param pLoc the external location, one index per external rank
 * @param pBuf receives the run
 * @param uBufLen the size of pBuf in BYTES
 * @returns false on a loud error, including a run that will not fit.
 * @memberof DasSet */
DAS_API bool DasSet_itemAt(
	const DasSet* pThis, ptrdiff_t* pLoc, ubyte* pBuf, size_t uBufLen
);

/** The stream context table this set can reach, or NULL when detached.
 *
 * Library internal.  Turns a context handle back into a name when serializing
 * or when explaining a refusal; the MATH never needs it, since bindings
 * compare by handle.  A detached set gets a vaguer diagnostic, never a wrong
 * answer.
 * @memberof DasSet */
const DasCtxTbl* _DasSet_ctxTbl(const DasSet* pThis);

/** Increment the reference count on a set
 *
 * @returns the new number of references to this set
 * @memberof DasSet */
DAS_API int DasSet_incRef(DasSet* pThis);

/** Decrement the reference count on a set
 *
 * If the reference count drops to zero the set drops its reference on its
 * generator (and thus, transitively, on any backing array) and then frees
 * its own memory.
 *
 * You should set any local pointers referring to this set to NULL after
 * calling DasSet_decRef as it may no longer exist.
 *
 * @returns the number of remaining references
 * @memberof DasSet */
DAS_API int DasSet_decRef(DasSet* pThis);

/** Get a value given an index
 *
 * This is the "slow boat from China" way to retrieve elements but it always
 * works, even for non-orthogonal data sets, ragged arrays and sets built on
 * operations over other sets.  This is useful when re-gridding a data set
 * onto a rectangular array such as a pixel or voxel raster.
 *
 * @param pThis the set in question
 *
 * @param pLoc The location to retrieve.  Unmapped indices are ignored.
 *
 * @param pOut pointer to a datum structure to fill in with the value.  For a
 *        composite this packs the whole item (a geovec, a complex pair) into
 *        one datum; strings and blobs pack a POINTER into backing storage.
 *
 * @return false if the indices represented by pLoc are invalid or the set's
 *         formalism has no single-datum representation, true otherwise.
 * @memberof DasSet */
DAS_API bool DasSet_get(const DasSet* pThis, ptrdiff_t* pLoc, das_datum* pOut);

/** Return the current shape of this set
 *
 * Asks the generator to inspect its backing array (or declared extents) and
 * report the current external extents in index space.
 *
 * @param pThis The set for which the shape is desired
 *
 * @param pShape a pointer to an array of size SETIDX_MAX.  Each element of
 *        the array will be filled in with one of the following:
 *
 *        * An integer from 0 to LONG_MAX to indicate the valid index range.
 *
 *        * The value SETIDX_UNUSED to indicate the given index position is
 *          ignored by this set
 *
 *        * The value SETIDX_RAGGED to indicate that the valid index range is
 *          variable and depends on the values of other indices.
 *
 *        * The value SETIDX_BORROW to indicate the set adopts whatever
 *          extent its dataset settles on for this index (sequences).
 *
 * @returns The external rank of the set
 * @memberof DasSet */
DAS_API int DasSet_shape(const DasSet* pThis, ptrdiff_t* pShape);

/** Return the internal composition of this set
 *
 * Sets may hold scalars, or items with internal structure.  An array of
 * strings has an internal index on the byte number; a geometric vector has
 * an internal index over its components.
 *
 * @param pThis The set for which the shape is desired
 *
 * @param pShape a pointer to an array of size SETIDX_MAX - 1.  Each element
 *        is filled in with either an integer from 0 to LONG_MAX (a typical
 *        geometric vector puts 3 here) or SETIDX_RAGGED to indicate the
 *        extent varies with the external indices, which is common for
 *        string data.
 *
 * @returns The rank of the internal components.  For scalars this is 0 and
 *          pShape is not touched.
 * @memberof DasSet */
DAS_API int DasSet_intrShape(const DasSet* pThis, ptrdiff_t* pShape);

/** Return the current max index value + 1 for any partial index
 *
 * This is a more general version of DasSet_shape that works for both cubic
 * arrays and ragged dimensions, or sequence values.
 *
 * @param pThis A pointer to a set
 * @param nIdx The number of location indices, which may be less than the
 *             number needed to specify an exact value
 * @param pLoc A list of values for the previous indices, each greater than
 *             or equal to 0
 * @return The number of sub-elements at this index location, or
 *         SETIDX_UNUSED if this set does not run along the given index, or
 *         SETIDX_BORROW for the dependent index of an un-bounded sequence.
 *
 * @see DasAry_lengthIn
 * @memberof DasSet */
DAS_API ptrdiff_t DasSet_lengthIn(const DasSet* pThis, int nIdx, ptrdiff_t* pLoc);

/** Does a given external index even matter to this set?
 *
 * @param pThis A pointer to a set
 *
 * @param iIndex The index in question, from 0 to SETIDX_MAX - 1
 *
 * @return true if varying this index can not cause the set's output to
 *         change, false if it can.
 * @memberof DasSet */
DAS_API bool DasSet_degenerate(const DasSet* pThis, int iIndex);

/** What part does this set play in its dimension?
 *
 * Roles live on the DasDim, in an array parallel to its set list, because a
 * role describes the RELATIONSHIP between a dim and a set rather than the set
 * itself.  This is the reverse lookup: given a set, ask its parent what it
 * was registered as.  See DasDim_addVar() and the DASVAR_* role names.
 *
 * @param pThis A pointer to a set
 *
 * @returns A constant pointer to the role name, or NULL if this set has no
 *          parent dimension.  A standalone set having no role is normal and
 *          not an error; a set whose parent cannot account for it is a
 *          corrupt dimension and is reported as one.
 * @memberof DasSet */
DAS_API const char* DasSet_role(const DasSet* pThis);

/** Materialize an external index range as a plain array
 *
 * Forces sequences, constants and computed sets to take on concrete values,
 * and squares off ragged storage using the backing array's fill value.  The
 * result is ALWAYS rectangular, so DasAry_shape() on it never reports
 * SETIDX_RAGGED.  This is what a consumer that speaks arrays rather than
 * variables (a CDF writer, a numeric binding) calls to get values out.
 *
 * The output holds ELEMENTS, not presentation values: a composite set yields
 * its components as trailing array indices, never as datums. 
 * Use DasSet_get() when a single assembled datum is what's wanted.
 *
 * For efficiency this may hand back a view onto existing storage rather than
 * a copy, in which case writing to it would corrupt the source.  When the
 * result must be independent, use DasSet_subsetCopy().
 * 
 * @note Always call dec_DasAry() on arrays returned from this function.
 *
 * @param pThis A pointer to a set
 * @param nRank The rank of the range specification, which must equal the
 *              set's external rank
 * @param pMin The inclusive lower bound for each index, nRank elements long
 * @param pMax The exclusive upper bound for each index, nRank elements long
 *
 * @returns A new DasAry holding the selected range, or NULL on error.  The
 *          array may or may not own its own memory; that is settled
 *          internally and needs no action from the caller.
 * @memberof DasSet */
DAS_API DasAry* DasSet_subset(
	const DasSet* pThis, int nRank, const ptrdiff_t* pMin, const ptrdiff_t* pMax
);

/** Materialize an external index range as an independent array
 *
 * Identical to DasSet_subset() except that the result never shares storage
 * with anything, so it may be written to and it outlives the set it came
 * from.
 *
 * @returns A new DasAry owning its own memory, or NULL on error.
 * @memberof DasSet */
DAS_API DasAry* DasSet_subsetCopy(
	const DasSet* pThis, int nRank, const ptrdiff_t* pMin, const ptrdiff_t* pMax
);

/** Are the values in this set convertible to doubles?
 *
 * A generator element can be any plain numeric type; many applications just
 * expect values convertible to doubles.  True for all numeric elements and
 * broken-down times; false for the byte-run families (strings, blobs).
 *
 * @memberof DasSet */
DAS_API bool DasSet_isNumeric(const DasSet* pThis);

/** Get a string representation of this set
 *
 * @param pThis a pointer to the set in question
 *
 * @param sBuf a buffer to hold the output, 128 bytes should be more than
 *        enough unless a deeply nested operation expression is present
 *
 * @param nLen the length of the string buffer.  This function will not
 *        write more than nLen - 1 bytes and will insure NULL termination
 * @memberof DasSet */
DAS_API char* DasSet_toStr(const DasSet* pThis, char* sBuf, int nLen);

/** Deep copy a set, but not any backing arrays
 *
 * The generator OBJECT is cloned (so a later DasSet_setArray on one copy
 * cannot re-aim the other); for operation generators the clone recurses
 * into the sub-generators.  Any reference counted objects pointed to by the
 * source (backing arrays) are incremented because they are now attached to
 * a new instance.
 *
 * @param pThis The source set
 *
 * @returns A new DasSet object allocated on the heap
 * @memberof DasSet */
DAS_API DasSet* DasSet_copy(const DasSet* pThis);

/** The das_val_type a datum from this set will carry: vtGeoVec for a geovec
 * composite, vtText/vtByteSeq for byte runs, otherwise the element type.
 * Derived, never stored.  @memberof DasSet */
DAS_API das_val_type DasSet_valType(const DasSet* pThis);

/* DasSet_vecMap() and das_makeCompLabels() are RETIRED.  Both were geovec
   knowledge in the generic set layer; answering them here would mean set.h
   including form_vector.h.  Clients use DasFormVector_slotSym() and compose
   their own labels -- see co_notes/downstream_fixups.md. */

/** The backing array if this set's generator has one, else NULL.
 * @memberof DasSet */
#define DasSet_getArray(P) DasGen_getArray((P)->pGen)

/** Point an array backed set at a different storage array
 *
 * This swaps the backing array of the set's generator, dropping the
 * reference on the old array and taking one on the new.  The index map is
 * left untouched, so the replacement must have the same rank as the array
 * it replaces.  The element type and the set's units are re-derived from
 * the new array, which is the point: handing in a das_time / UTC array in
 * place of, say, a TT2000 long array re-tags the set as UTC automatically.
 *
 * This is a stream-filter helper.  The typical use is a binary -> text
 * re-encoder that must change an epoch integer or real into a das_time so
 * the codec can emit ISO-8601.  A codec that referenced the old array must
 * be re-initialized by the caller; this call does not touch codecs.
 *
 * @param pThis An array backed set (generator kind gtArray)
 *
 * @param pNew The replacement array.  Must be the same rank as the current
 *        backing array and use a simple (non byte-run) value type.
 *
 * @returns true on success, false (with das_error set) otherwise.
 * @memberof DasSet */
DAS_API bool DasSet_setArray(DasSet* pThis, DasAry* pNew);

/** Serialize this set as its wire element: <scalar>, <composite> or <bytes>,
 * with an <ops> child when the formalism is anything but linear.
 *
 * Attributes whose default the schema can state are omitted (use="center", an
 * empty units=); <ops> parameters are written even at their defaults, because
 * the schema cannot carry a default behind an anyAttribute.  A generator kind
 * with no writer yet fails loud rather than emitting something plausible.
 * @memberof DasSet */
DAS_API DasErrCode DasSet_encode(DasSet* pThis, const char* sRole, DasBuf* pBuf);

/** Create a scalar set: one value per point, no internal index.
 *
 * @param pGen the value source; this call takes a reference
 * @param units the set's units.  A ';' units list FAILS: nothing in the
 *        library can hold per-component units yet, and silently keeping the
 *        first entry would misstate the data.
 * @param pForm the formalism; this call TAKES the reference, so a caller that
 *        wants to keep one must incRef first.  Never NULL for a scalar: an
 *        absent <ops> binds the explicit linear form, so "no formalism" is a
 *        thing only a byte run may say.
 * @returns a new set, or NULL on a loud error.  @memberof DasSet */
DAS_API DasSet* new_DasSetScalar(
	DasGen* pGen, das_units units, DasForm* pForm
);

/* ========================================================================= *
 * DasBinSet: an operation over two sets.  Supersedes DasVarBinary.
 *
 * This is the one set class with NO wire element.  das3 has no binary element:
 * a stream carries reference and offset as two separate variables and the
 * dimension combines them.  So a DasBinSet is never DECODED -- it is built in
 * code, and serializing one means MATERIALIZING it, walking the operands into
 * an array and emitting that as though it had been array-backed all along.
 *
 * It keeps the operand SETS, which is what DasVarBinary lost when it flattened
 * itself into a generator: a generator has no units and no ops, so once the
 * operands were reduced to generators nobody could ask what had been combined.
 * The generator half still exists underneath (DasGenOp holds the operand
 * GENERATORS and does the numeric walking); each layer keeps what it knows.
 *
 * FORMS COMPUTE, SETS WALK.  This is the walker, and it is the ONLY one.
 *
 * A DasBinSet asks the operands' formalisms for a recipe ONCE, at
 * construction, then reads six facts off it -- result form, units, value
 * type, internal rank and shape, and one apply function -- and drives the
 * whole walk itself.  It never asks again what math made a value.  The form
 * on the other side of that call has no generator, no array, no index and no
 * loop; it is handed one item run from each side and hands one back.
 *
 * The temptation to hand a form a generator so it can fetch its own values
 * has been considered and REFUSED.  Every formalism that exists is item
 * local, so it buys nothing today, and what it costs is this file's monopoly
 * on walking: ragged handling and bounds checking would be re-invented in
 * every form_*.c that took the offer.  When non-local math arrives
 * (interpolation, convolution, a point spread response) the recipe will
 * declare a STENCIL, this walker will gather that window and pass it in, and
 * the walker stays the only thing that walks.  A form says what it needs; it
 * never goes and gets it.
 * ========================================================================= */

typedef struct das_bin_set {

	DasSet base;         /* base.pForm and base.units are the RESOLVED result,
	                        decided once by binOpLeft/binOpRight at construction
	                        and thereafter just fields -- which is why
	                        _DasSet_subset() never asks what math made a value */

	DasSet*     pLeft;   /* references held; operand identity stays askable */
	DasSet*     pRight;
	int         op;      /* a D2BOP_* code from operator.h */

	/* The pairing, resolved ONCE at construction.  Six facts and one function
	   pointer; the walk reads them and never asks the forms again.  Shared on
	   copy, never re-resolved: two copies of one set must not disagree about
	   what math they are. */
	DasBinOp*   pRecipe;

} DasBinSet;

/** Create a set as an operation over two sets.
 *
 * The pairing is dispatched to the operands' ops objects, left first then
 * right, Python's __add__ / __radd__ model.  A pairing neither operand claims
 * FAILS LOUD (point + point, mismatched units, mismatched frames) rather than
 * being guessed at or silently commuted.
 *
 * @param cOp one of '+', '-', '*'
 * @returns a new set, or NULL on a loud error.  @memberof DasSet */
DAS_API DasBinSet* new_DasBinSet(DasSet* pLeft, char cOp, DasSet* pRight);

/** Evaluate a set into a plain array: every value, whole shape, units and all.
 *
 * The general form of what serializing a DasBinSet requires, and useful on its
 * own to any caller that wants the numbers rather than the recipe.
 *
 * @returns a new array the caller owns, or NULL on a loud error.
 * @memberof DasSet */
DAS_API DasAry* DasSet_materialize(const DasSet* pThis);


/* ========================================================================= *
 * Values with an internal index: DasCompSet (<composite>) and DasByteSet
 * (<bytes>).  Supersede DasVarVecAry and the roughly thirty scattered
 * das_val_type switch sites the scout found.
 *
 * The internal layout is pure SHAPE.  The formalism does not live in the shape.
 * A 3;3 rotation and a 3;3 plain matrix share the same layout and differ only
 * in their formalism.  TRACERS ships non-rotation matrices, so this matters.
 *
 * Dispatch inside a composite looks at Axis B first.
 *
 *   element is etUByte    ->  the byte run branch: string or blob.  Empty
 *                             formalism.  No table lookup.  <bytes> on the wire.
 *
 *   element is numeric    ->  base.form carries the row.  A hit gives a rich
 *                             datum.  A miss is the generic case: carry the
 *                             token, hand back plain numbers.
 * ========================================================================= */

/* ========================================================================= *
 * The byte run: DasByteSet, the <bytes> element.  Element type is etUByte.
 *
 * One class for one wire element.  A byte run cannot carry math and there is
 * no field saying so: the CLASS says it, which is why new_DasByteSet() takes
 * no formalism argument at all and why a byte run can never reach the binop
 * registry.  The structural family comes from the element name, so no
 * semantic is needed for structure.  string and blob are told apart by the
 * packet encoding, an explicit required triplet:
 *
 *   encoding="utf8"    text
 *   encoding="base64"  ASCII-armored binary, decode to get the bytes
 *   encoding="raw"     no transform, opaque bytes
 *
 * raw is a NAMED token, not an omitted attribute, so a forgotten encoding is a
 * loud error rather than a silent default.
 *
 * Wire:
 *
 *   <bytes index="*">
 *     <packet numItems="1" itemBytes="*" encoding="utf8" valTerm=";"/>
 *   </bytes>
 *
 *   <bytes index="*">
 *     <packet numItems="1" itemBytes="*" encoding="raw" embedded="image/png"/>
 *   </bytes>
 *
 * The primary encoding gets the bytes off the wire.  An embedded= attribute,
 * if present, is the extension-codec trigger: it names the format the bytes
 * must further be decoded FROM to realize the declared values, and a reader
 * with no handler fails loud.  A purely descriptive content label rides in a
 * property (contentType) instead.  Two independent stages on two attributes.
 *
 * The only difference between string and blob is the sentinel, so it rides as
 * one flag rather than two classes: both are the same wire element and the same
 * structure.  A string ends in a REQUIRED trailing null in the array's last
 * index, which is why it can hand out a bare char*.  A blob is stored AS GIVEN
 * with no sentinel, which is why it must always be carried as pointer plus
 * length.  Fixed versus ragged is orthogonal; both can be either.
 * ========================================================================= */

/** The numeric component run: DasCompSet, the <composite> element.
 * 
 * A composite carries semantic in its interpretation role, same as a scalar:
 * integer or real.  It is the parse target for utf8 components and redundant
 * with the encoding for binary.  The math rides on a flat <ops kind="..."/>:
 * kind= names it and every other attribute is a parameter of that kind.  The
 * element never has children, which is what lets a kind we do not recognize ride
 * through as name/value pairs.  See schema/das2c_operations.md.
 *
 * Complex needs no parameters, so kind= alone carries it.  This is the original
 * das3_from_cdf request, landed with no new semantic and no codec change:
 *
 *   <composite semantic="int" intern="2" index="*">
 *     <ops kind="complex"/>
 *     <packet numItems="2" itemBytes="1" encoding="byte"/>
 *   </composite>
 *
 * A geometric vector names a frame and its component order.  Child order reads
 * purpose to bytes: properties, ops, generator.  An <ops> writer is talkative and
 * states system= and sysorder= even at their defaults, since the schema cannot
 * carry a default behind an anyAttribute:
 *
 *   <composite semantic="real" units="m" intern="3" index="*">
 *     <properties><p name="label" type="stringArray">B_x;B_y;B_z</p></properties>
 *     <ops kind="geovec" frame="TS2_TSCS" system="cartesian" sysorder="0;1;2"/>
 *     <packet numItems="3" itemBytes="4" encoding="LEreal"/>
 *   </composite>
 *
 * A rotation names two frames.  Layout is 3;3, pure shape; the formalism, not the
 * shape, says "rotation":
 *
 *   <composite semantic="real" intern="3;3" index="*">
 *     <ops kind="rotation" from="TSCS" to="GEI2000"/>
 *     <packet numItems="9" itemBytes="8" encoding="LEreal"/>
 *   </composite>
 *
 * The word "formalism" survives in the C even though the wire says <ops>: it is
 * deliberately broader than "algebra", since a point spread function or a
 * sampling response is a rule a capable client needs but is not an algebra.
 */
typedef struct das_comp_set {

	DasSet base;           /* base.form: the formalism row, token, bindings */

	/* Internal layout exactly as declared.  "3" is a vector.  "3;3" is a
	   matrix.  Ragged levels are Flags.  Multi-level shapes read and write
	   end to end (test/ex40_rotation is the 3;3 case) */
	int       nIntRank;
	ptrdiff_t aIntShape[SETIDX_MAX];

	/* Per component labels live at this structural level, readable without
	   understanding the formalism (the skippability contract: a dumb client
	   still stacks N labeled lines).  Per component units WOULD live here
	   too; until a holder exists the ';' list fails loud, see base.units. */
	/* TODO structural label storage */

} DasCompSet;

/* The byte run: DasByteSet, the <bytes> element.  Internal rank is ALWAYS 1
   (the index is the byte number), so one extent replaces a shape array. */
typedef struct das_byte_set {

	DasSet base;           /* base.form is unused: a byte run has no math */

	ptrdiff_t nExtent;     /* item length in bytes, SETIDX_RAGGED if var-width */

	/* string, not blob: a required trailing null in the last index, which is
	   what lets a datum be a bare char*.  A blob is stored as given and must
	   always be carried as pointer plus length. */
	bool bSentinel;

} DasByteSet;

/** Create a numeric composite: a vector, a complex pair, a matrix.
 *
 * @param pGen the value source; this call takes a reference
 * @param units the set's units
 * @param pForm the formalism; this call TAKES the reference.  Never NULL
 * @param nIntRank internal rank (1 for a vector, 2 for a "3;3" matrix)
 * @param pIntShape internal extents, SETIDX_RAGGED for a ragged level
 * @returns a new set, or NULL on a loud error.  
 * @memberof DasCompSet 
 */
DAS_API DasCompSet* new_DasCompSet(
	DasGen* pGen, das_units units, DasForm* pForm,
	int nIntRank, const ptrdiff_t* pIntShape
);

/** Create a byte run: a string or a blob.
 *
 * There is no formalism argument.  A byte run has no auto-math.
 *
 * @param pGen the value source; this call takes a reference.  It must be
 *        array-backed: a byte-run datum is a POINTER into storage, and a
 *        computed run has no home to point at.
 * @param units the set's units; NULL is fine and usual
 * @param bSentinel true for a string (required trailing null), false for a blob
 * @param nExtent item length in bytes, or SETIDX_RAGGED when variable
 * @returns a new set, or NULL on a loud error.  
 * @memberof DasByteSet 
 */
DAS_API DasByteSet* new_DasByteSet(
	DasGen* pGen, das_units units, bool bSentinel, ptrdiff_t nExtent
);

/* DasSet_getFrame() is DELETED, not moved.  A frame is the geovec
   formalism's parameter, so answering it here would mean set.h including
   form_geovec.h -- generic code learning one specific formalism, the exact
   leak form_rot.h warns about.  Callers ask the form:

       #include <das2/form_geovec.h>
       ubyte uFrame = DasFormGeoVec_frameId(DasSet_form(pSet));

   which also fails honestly on a set whose formalism is not a geovec,
   where the old macro quietly returned NULL. */


#ifdef __cplusplus
}
#endif

#endif /* _das_set_h_ */
