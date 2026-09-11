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

/** @file variable.h The DasVar layer: named values with formalisms.
 *
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
 * The wire side of all this is written up in schema/das2c_operations.md.
 */

#ifndef _das_var_h_
#define _das_var_h_

#include <das3/descriptor.h>
#include <das3/buffer.h>
#include <das3/datum.h>
#include <das3/units.h>
#include <das3/generator.h>
#include <das3/form.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ========================================================================= *
 *
 * A variable, here a DasVar, is the top level interface for data values.  It
 * pulls together multiple concepts:
 *
 * 1. Datum: Found at each full external index of a variable is a datum.  The
 *    value can come in multiple forms, from simple integers, to rotation
 *    matrices, to strings.  DasVar_get() provides datums, though it's not
 *    a fast function and there are better ways to retrieve data.
 *
 * 2. Elements: Each datum is composed of one or more elements.  For simple
 *    variables a datum is just a single floating point number.  For utf-8
 *    or byte strings, it is a byte.
 *
 * 3. Formalisms: Some datums obey a given set of math rules.  Each one is a
 *    vector, or a complex number, or an affine point.  Formalisms (DasForm)
 *    provide binary operations on their values and on related forms.  To get
 *    a variable's formalism call DasVar_form().
 *
 * 4. Generators: (DasGen) provides elements.  Generators don't know what math
 *    rules apply, they operate below that level and either create values via
 *    binary operations, provide them from a backing array store, or by simple
 *    rules based on the provided index.  Application code typically does not
 *    interact with this level, but every variable has a generator.
 *
 * There are four variable classes, but that is an implementation detail as
 * interactions with each one are typically via virtual functions.  The only
 * time application code needs to concern itself with the type of a variable
 * is when constructing DasVar derived objects of their own.  The constructors
 * are:
 *
 * new_DasVar()
 *   Creates a variable that has no internal indices, but may have a math
 *   form.  Example values are calendar times, which follow the affine
 *   (point) formalism.
 *
 * new_DasVarComp()
 *   Creates a variable with internal indices and an optional math formalism.
 *   Example values are geodetic locations.  These have three elements for
 *   each value and follow the "geoloc" formalism.
 *
 * new_DasVarBin()
 *   Creates a variable that is a binary operation on two other variables,
 *   such as providing a 2-D Qube array of time values given a reference
 *   array and an offset time array.
 *
 * new_DasVarBytes()
 *   Creates a variable with a single internal index for each value.  These
 *   hold strings or binary blobs of bytes and may not have a math formalism.
 *
 * ========================================================================= */

/* ========================================================================= *
 * DasVar, the base.
 * ========================================================================= */

typedef struct das_var DasVar;

/* ========================================================================= *
 * The scalar case.  One value per point, no internal index.  There is NO
 * DasVarScalar struct: a scalar IS the base DasVar, and its only formalisms so
 * far are linear or point.
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
 * scalar, exactly like vector on a composite.  Same struct, same store.
 * ========================================================================= */

/* The dispatch table is in var_priv.h: nothing outside das2C implements one,
   and there is no registration path if it wanted to. */
struct DasVar_VTbl;



struct das_var {

	DasDesc base;          /* a variable IS a descriptor: it has properties, a
	                          parent, and it serializes */

	const struct DasVar_VTbl* pVTbl;

	DasGen* pGen;          /* The variable owns a generator, is not one.
	                          Swapping array for sequence does not change the
	                          variable's type. */


	DasForm* pForm;        /* Heap owned and refcounted, symmetric
	                          with pGen: pGen says how values are produced,
	                          pForm says what they mean to arithmetic.  NEVER
	                          NULL for a numeric variable -- an absent <ops> binds
	                          the explicit linear form and an unrecognized
	                          kind= binds DasFormGeneric, so "I do not know
	                          this math" has a vtable to refuse from.  Only
	                          DasVarBytes leaves it NULL: a byte run has no
	                          auto-math and its CLASS says so. */

	/* One unit. Some DasForm objects may need more, in which
	   case they can provide storage for them */
	das_units units;

	int   nRef;
	void* pUser;
};

/** The wire element this variable serializes as: "scalar", "composite" or
 * "bytes".  Structure is carried by the C class, so the serializer asks the
 * class rather than deriving it back out of a presentation vocabulary.
 * @memberof DasVar */
DAS_API const char* DasVar_element(const DasVar* pThis);

#define DasVar_units(P)    ((P)->units)
#define DasVar_gen(P)      ((P)->pGen)
#define DasVar_form(P)     ((P)->pForm)

/** Is this variable's formalism the given kind?
 *
 * A shortcut for DasForm_isKind() so that code working with variables need not
 * pull out the formalism first.  Each formalism header defines a macro for its
 * virtual table, use those here.  For example
 * @code
 *     if(DasVar_formIs(pVar, DAS_FORM_GEOLOC)) ...
 * @endcode
 *
 * @returns true if the formalisms match.  A null variable returns false, as do
 *          strings and blobs, which carry no formalism at all.
 * @memberof DasVar
 */
#define DasVar_formIs(P,VT) (((P) != NULL)&&DasForm_isKind(DasVar_form(P), VT))

/** The canonical symbol for one component of this variable's value.
 *
 * What a client needs to label N stacked component lines without knowing what
 * kind of value it is holding: "x", "λ", "re", "xy" and so on.  Components
 * count from zero in storage order, so a form's sysorder= has already been
 * applied.  Use DasVar_intrShape() for how many there are.
 *
 * @param pThis the variable to ask
 * @param iComp the component, in storage order
 * @returns a constant symbol owned by the library, or NULL.  NULL is a normal
 *          answer and not an error: plain numbers, strings and blobs have no
 *          symbols at all, and a value carrying fewer components than its
 *          system defines has none past the count it actually holds.  A two
 *          component vector answers for 0 and 1 and NULL for 2.
 * @memberof DasVar
 */
DAS_API const char* DasVar_compSym(const DasVar* pThis, int iComp);

/** One label per component, a convenience for output formats.
 *
 * Nothing in the library calls this and no application has to.  Labelling the
 * components of a composite is a chore every output format faces, so this is
 * one default answer for the apps that would rather not invent their own.
 * The order of preference is:
 *
 *   1. a label property holding one entry per component, used as given
 *   2. a single valued label, used as a stem with each component's symbol
 *      appended, as in "B_x"
 *   3. no label at all, in which case the dimension's name is the stem
 *
 * The label property is read the inheriting way, so one on the dimension
 * covers every variable under it.  A component with no symbol gets its index
 * instead, since N identical labels would leave a reader unable to tell the
 * components apart.
 *
 * @param pThis the variable to label
 * @param psBuf nMax pointers, each to a buffer of at least uLenEa bytes
 * @param nMax how many buffers there are; more components than this is an
 *        error rather than a truncation
 * @param uLenEa the size of each buffer
 * @returns the count written, 1 for a scalar, or a negative das error code.
 * @memberof DasVar
 */
DAS_API int DasVar_compLabels(
	const DasVar* pThis, char** psBuf, int nMax, size_t uLenEa
);

/** What one cell of this variable holds.  Never cached -- DasVar_setAry()
 * re-tags it.
 *
 * Dispatched, because the answer is not always the generator's: a binary
 * operation has no generator and takes its cell type from the recipe that
 * resolved it.
 *
 * das_elem_type, NOT das_val_type.  A variable's cells are storage, and
 * calling storage by the presentation vocabulary's name is a cast that only
 * happens to work because the two enums agree on 0..11.
 * @memberof DasVar */
DAS_API das_elem_type DasVar_elemType(const DasVar* pThis);


/** Increment the reference count on a variable
 *
 * @returns the new number of references to this variable
 * @memberof DasVar */
DAS_API int inc_DasVar(DasVar* pThis);

/** Decrement the reference count on a variable
 *
 * If the reference count drops to zero the variable drops its reference on its
 * generator (and thus, transitively, on any backing array) and then frees
 * its own memory.
 *
 * You should set any local pointers referring to this variable to NULL after
 * calling dec_DasVar as it may no longer exist.
 *
 * @returns the number of remaining references
 * @memberof DasVar */
DAS_API int dec_DasVar(DasVar* pThis);

/** Get one value, at one location, as a datum.
 *
 * The one-at-a-time read.  It always works -- non-orthogonal datasets, ragged
 * arrays, sequences, operations over other variables -- which is what a
 * re-gridder onto a pixel or voxel raster needs.
 *
 * @par Where the value lives
 * das2C lends storage rather than copying it, so a datum from this call keeps
 * its value in one of three places:
 *   - inline, in the datum's own bytes.  Every scalar, including a scalar
 *     computed by an operation.  das_datum_islocal() reports true.
 *   - in the VARIABLE's storage, pointed at in place.  An array backed
 *     composite, string or blob.  Zero copy, and valid only while the
 *     variable lives.
 *   - in @a work, the scratch you supply.  A composite with nothing to point
 *     at: a vector sequence, or a composite operation.
 *
 * Reading the datum does not care which.  Keeping the VALUE past the next call
 * does: copy it out.
 *
 * @par Sizing the scratch
 * There is no call that answers "how big a buffer" in advance, because for a
 * ragged run the answer is a property of the LOCATION, not of the variable --
 * item N+1 may dwarf item N.  So this call reports what it needed and the
 * caller grows.  Start with a stack buffer, grow only when told, and keep the
 * larger one bound for the rest of the walk:
 *
 * @code
 * ubyte       aStack[128];
 * das_byte_seq work   = { aStack, sizeof(aStack) };
 * ubyte*      pGrown = NULL;
 * das_datum   dm;
 *
 * for(DasDsIter_init(&it, pDs); !it.done; DasDsIter_next(&it)){
 *
 *    int nRet = DasVar_get(pVar, it.index, work, &dm);
 *
 *    if(nRet > 0){                        // this item wants nRet bytes
 *       ubyte* pNew = realloc(pGrown, (size_t)nRet);
 *       if(pNew == NULL) break;
 *       pGrown = pNew;  work.ptr = pGrown;  work.sz = (size_t)nRet;
 *       nRet = DasVar_get(pVar, it.index, work, &dm);   // fits now
 *    }
 *    if(nRet != 0) break;                 // < 0 was already reported
 *    ...
 * }
 * free(pGrown);
 * @endcode
 *
 * A caller that reads only scalars or only array backed data passes an empty
 * work and never allocates.  DasVar_getNeedsBuf() says which case you are in
 * without reading anything.
 *
 * The retry is guaranteed to fit so long as the backing store does not change
 * underneath, which is the same condition that lets threads read one variable
 * without locking.  Do not loop on it.
 *
 * @param pThis the variable in question
 * @param pLoc the location to retrieve; unmapped indices are ignored
 * @param work caller scratch, used only when there is nothing to point at.
 *        Pass @c {NULL,0} if you know you do not need it.
 * @param pOut receives the value
 *
 * @returns 0 on success, the BYTES of scratch needed when @a work is too
 *          small, or a negative das error code on a loud failure (a bad
 *          location, or a formalism with no single-datum representation).
 * @memberof DasVar */
DAS_API int DasVar_get(
	const DasVar* pThis, ptrdiff_t* pLoc, das_byte_seq work, das_datum* pOut
);

/** Will DasVar_get() need scratch from the caller for this variable?
 *
 * false when every datum it produces keeps its value inline or points into
 * storage this variable already owns.  true only for a composite with nothing
 * to point at -- a vector sequence, or a composite operation.
 *
 * A convenience, never a requirement: DasVar_get() reports what it needs, so a
 * caller may always ask forgiveness instead.  What this buys is skipping the
 * buffer machinery altogether when the answer is no, which is the usual case
 * for a stream reader.
 * @memberof DasVar */
DAS_API bool DasVar_getNeedsBuf(const DasVar* pThis);

/** Return the current shape of this variable
 *
 * Asks the generator to inspect its backing array (or declared extents) and
 * report the current external extents in index space.
 *
 * @param pThis The variable for which the shape is desired
 *
 * @param pShape a pointer to an array of size VARIDX_MAX.  All VARIDX_MAX
 *        elements are written, not just the ones this variable uses, so the
 *        caller need not pre-fill the buffer.  Each element receives one of
 *        the following:
 *
 *        * An integer from 0 to LONG_MAX to indicate the valid index range.
 *
 *        * The value VARIDX_UNUSED to indicate the given index position is
 *          ignored by this variable
 *
 *        * The value VARIDX_RAGGED to indicate that the valid index range is
 *          variable and depends on the values of other indices.
 *
 *        * The value VARIDX_BORROW to indicate the variable adopts whatever
 *          extent its dataset settles on for this index (sequences).
 *
 * @returns The external rank of the variable
 * @memberof DasVar */
DAS_API int DasVar_shape(const DasVar* pThis, ptrdiff_t* pShape);

/** Return the internal composition of this variable
 *
 * Variables may hold scalars, or items with internal structure.  An array of
 * strings has an internal index on the byte number; a geometric vector has
 * an internal index over its components.
 *
 * @param pThis The variable for which the shape is desired
 *
 * @param pShape a pointer to an array of size VARIDX_MAX - 1.  Each element
 *        is filled in with either an integer from 0 to LONG_MAX (a typical
 *        geometric vector puts 3 here) or VARIDX_RAGGED to indicate the
 *        extent varies with the external indices, which is common for
 *        string data.
 *
 * @returns The rank of the internal components.  For scalars this is 0 and
 *          pShape is not touched.
 * @memberof DasVar */
DAS_API int DasVar_intrShape(const DasVar* pThis, ptrdiff_t* pShape);

/** Return the current max index value + 1 for any partial index
 *
 * This is a more general version of DasVar_shape that works for both cubic
 * arrays and ragged dimensions, or sequence values.
 *
 * @param pThis A pointer to a variable
 * @param nIdx The number of location indices, which may be less than the
 *             number needed to specify an exact value
 * @param pLoc A list of values for the previous indices, each greater than
 *             or equal to 0
 * @return The number of sub-elements at this index location, or
 *         VARIDX_UNUSED if this variable does not run along the given index, or
 *         VARIDX_BORROW for the dependent index of an un-bounded sequence.
 *
 * @see DasAry_lengthIn
 * @memberof DasVar */
DAS_API ptrdiff_t DasVar_lengthIn(const DasVar* pThis, int nIdx, ptrdiff_t* pLoc);

/** Does a given external index even matter to this variable?
 *
 * @param pThis A pointer to a variable
 *
 * @param iIndex The index in question, from 0 to VARIDX_MAX - 1
 *
 * @return true if varying this index can not cause the variable's output to
 *         change, false if it can.
 * @memberof DasVar */
DAS_API bool DasVar_degenerate(const DasVar* pThis, int iIndex);

/** What part does this variable play in its dimension?
 *
 * Roles live on the DasDim, in an array parallel to its variable list,
 * because a role describes the RELATIONSHIP between a dim and a variable
 * rather than the variable itself.  This is the reverse lookup: given a
 * variable, ask its parent what it
 * was registered as.  See DasDim_addVar() and the DASVAR_* role names.
 *
 * @param pThis A pointer to a variable
 *
 * @returns A constant pointer to the role name, or NULL if this variable has no
 *          parent dimension.  A standalone variable having no role is normal
 *          and not an error; a variable whose parent cannot account for it is a
 *          corrupt dimension and is reported as one.
 * @memberof DasVar */
DAS_API const char* DasVar_role(const DasVar* pThis);

/** Read an external index range as a plain array, in the data's own shape
 *
 * Forces sequences, constants and computed variables to take on concrete
 * values.  Ragged storage stays ragged -- there is no *Qube* in the name -- so
 * DasAry_shape() on the result may report VARIDX_RAGGED, and the per-run
 * lengths are exactly the ones the stream carried.  Reach for this when the
 * real structure is the point: a Cassini waveform whose rows are genuinely
 * 2048 / 1536 / 2048 samples long says so, instead of padding two of them.
 *
 * A consumer that must have a rectangle -- a CDF writer, a numpy or IDL
 * binding, anything that indexes by stride -- wants DasVar_subsetQube()
 * instead.  For storage that is already square the two are the same call over
 * the same bytes, so the choice only matters where the data is ragged.
 *
 * The output holds ELEMENTS, not presentation values: a composite variable
 * yields its components as trailing array indices, never as datums.
 * Use DasVar_get() when a single assembled datum is what's wanted.
 *
 * For efficiency this may hand back a VIEW onto existing storage rather than a
 * copy, in which case writing to it would corrupt the source.  Ask which you
 * got with DasAry_ownsElements(): true means the memory is yours to write and
 * to keep (DasAry_disownElements() will hand it over), false means you are
 * looking at the variable's own store and must copy before changing anything.
 *
 * @note Always call dec_DasAry() on arrays returned from this function.
 *
 * @param pThis A pointer to a variable
 * @param nRank The rank of the range specification, which must equal the
 *              variable's external rank
 * @param pMin The inclusive lower bound for each index, nRank elements long.
 *             May be NULL together with pMax, meaning the whole extent.
 * @param pMax The exclusive upper bound for each index, nRank elements long.
 *             May be NULL together with pMin.
 *
 * @param pShapeFrom Whose extents to wear when this variable cannot supply its
 *        own -- a sequence that BORROWS its length is the case that needs it.
 *        NULL means "use my own shape", which is the usual answer: most
 *        sequences declare a concrete extent on the wire (see
 *        examples/ex12_sounder_xyz.d3t, index="-;-;80").
 *
 *        This is a VARIABLE and not an array on purpose.  An array has no
 *        concept of a degenerate index; a variable does, and it maps dataset
 *        index space onto array index space through its own index map.  Two
 *        variables over one dataset can therefore need different array ranks
 *        for the same dataset extent, so only a variable can answer.
 *
 *        Usually unnecessary: a variable that has a parent dimension reaches up
 *        to its dataset for the same answer.  Name a model for a variable that
 *        stands alone, or to wear extents other than its own container's.
 *
 * @note A variable is DATASET shaped.  An index it does not vary along is not
 *       absent -- it repeats, with a step size of zero, which is what lets
 *       every variable in a dataset answer at every position (see
 *       das3/variable.md, "Degeneracy alters the step size").  So the whole
 *       extent of a rank 3 dataset is rank 3 even for a variable that varies
 *       along one index, and the repeated values are really there in the
 *       result.  A caller wanting only the distinct values says so with a
 *       restricted pMin/pMax -- pin the degenerate indices to one element, the
 *       way das3_cdf does after asking DasVar_degenerate().
 *
 * @returns A new DasAry holding the selected range, or NULL on error.  The
 *          array may or may not own its own memory; ask DasAry_ownsElements()
 *          if it matters, or call DasVar_materialize() to be certain.
 * @memberof DasVar */
DAS_API DasAry* DasVar_subset(
	const DasVar* pThis, int nRank, const ptrdiff_t* pMin, const ptrdiff_t* pMax,
	const DasVar* pShapeFrom
);

/** The same values, in memory the caller owns outright.
 *
 * Identical arguments to DasVar_subset(), and the same answer for anything
 * computed -- a sequence or an operation has no storage to lend, so subset
 * was already allocating.  The difference is the GUARANTEE: this never hands
 * back a view, so the result may be written to and outlives the variable and
 * its arrays.  Ask for it when you mean to free the dataset, modify the
 * values, or carry them to another thread; a caller can test what subset gave
 * it, but testing does not get you what you needed.
 *
 * @returns A new DasAry owning its own memory, or NULL on error.
 * @memberof DasVar */
DAS_API DasAry* DasVar_materialize(
	const DasVar* pThis, int nRank, const ptrdiff_t* pMin, const ptrdiff_t* pMax,
	const DasVar* pShapeFrom
);

/** The same values, squared off into a rectangle.
 *
 * Identical arguments to DasVar_subset(), and identical output whenever the
 * storage is already square -- including the same zero-copy view, which is the
 * path a CDF writer lives on.  Where the data is RAGGED this walks the
 * requested range, takes the widest extent found along each ragged index, and
 * pads every short run out to it with the backing array's fill value.  A
 * ragged item run (a variable-width string) is squared the same way, one index
 * deeper, and lands as a trailing array index.
 *
 * So DasAry_shape() on the result never reports VARIDX_RAGGED, and the shape
 * is a property of the requested RANGE: ask for fewer records and the
 * rectangle may be narrower.
 *
 * Padding is only honest where the pad byte is a sentinel.  For text it is --
 * the runs are fill terminated, so a padded slot reads as an empty string.
 * For a variable-length BLOB no byte value can mean "not data", and the
 * per-item length would be destroyed, so this REFUSES rather than corrupt it;
 * read those with DasVar_subset() or one item at a time with DasVar_get().
 * The test is D2ARY_FILL_TERM on the backing array.
 *
 * @returns A new DasAry holding the selected range as a rectangle, or NULL on
 *          error.  May or may not own its memory; see DasAry_ownsElements().
 * @memberof DasVar */
DAS_API DasAry* DasVar_subsetQube(
	const DasVar* pThis, int nRank, const ptrdiff_t* pMin, const ptrdiff_t* pMax,
	const DasVar* pShapeFrom
);

/** A rectangle, in memory the caller owns outright.
 *
 * DasVar_subsetQube()'s shape guarantee and DasVar_materialize()'s ownership
 * guarantee at once, which together are what a language binding actually
 * needs: something rectangular that it can hand to the runtime and then forget
 * about.  The one call replaces the test-and-take dance of asking
 * DasVar_subset(), checking DasAry_ownsElements() and copying by hand when the
 * answer is false.
 *
 * @returns A new rectangular DasAry owning its own memory, or NULL on error.
 * @memberof DasVar */
DAS_API DasAry* DasVar_materializeQube(
	const DasVar* pThis, int nRank, const ptrdiff_t* pMin, const ptrdiff_t* pMax,
	const DasVar* pShapeFrom
);

/** Every value this variable has, in its own shape.
 *
 * The common case: no bounds, no model.  "get" is deliberately absent from
 * this name -- DasVar_get() returns one datum and DasVar_getAry() returns the
 * backing store, so a third DasVar_get* returning a computed array would be
 * one letter and one wrong guess away from either.  @memberof DasVar */
#define DasVar_allVals(P, R)  DasVar_subset((P), (R), NULL, NULL, NULL)


/** Are the values in this variable convertible to doubles?
 *
 * A generator element can be any plain numeric type; many applications just
 * expect values convertible to doubles.  True for all numeric elements and
 * broken-down times; false for the byte-run families (strings, blobs).
 *
 * @memberof DasVar */
DAS_API bool DasVar_isNumeric(const DasVar* pThis);

/** Get a string representation of this variable
 *
 * @param pThis a pointer to the variable in question
 *
 * @param sBuf a buffer to hold the output, 128 bytes should be more than
 *        enough unless a deeply nested operation expression is present
 *
 * @param nLen the length of the string buffer.  This function will not
 *        write more than nLen - 1 bytes and will insure NULL termination
 * @memberof DasVar */
DAS_API char* DasVar_toStr(const DasVar* pThis, char* sBuf, int nLen);

/** Deep copy a variable, but not any backing arrays
 *
 * The generator OBJECT is cloned (so a later DasVar_setAry on one copy
 * cannot re-aim the other); for operation generators the clone recurses
 * into the sub-generators.  Any reference counted objects pointed to by the
 * source (backing arrays) are incremented because they are now attached to
 * a new instance.
 *
 * @param pThis The source variable
 *
 * @returns A new DasVar object allocated on the heap
 * @memberof DasVar */
DAS_API DasVar* DasVar_copy(const DasVar* pThis);

/** The das_val_type a datum from this variable carries: whatever the form's
 * datumType() answers for a composite, vtText/vtByteSeq for byte runs,
 * otherwise the element type.
 * Derived, never stored.  @memberof DasVar */
DAS_API das_val_type DasVar_valType(const DasVar* pThis);

/* DasVar_vecMap() and das_makeCompLabels() are RETIRED.  Both were vector
   knowledge in the generic variable layer; answering them here would mean
   variable.h including form_vector.h.  Clients use DasVar_compSym() above and
   compose their own labels. */

/** The backing array behind this variable, or NULL if there is none.
 *
 * NULL is a complete answer, not a failure: a sequence computes its values
 * and a binary operation has no leaf value source at all.  So this doubles as
 * the fast-path test -- a caller that can bulk-copy a whole array asks once
 * and gets both the verdict and the pointer, rather than testing a kind and
 * then fetching.
 *
 * @returns the array, or NULL for any variable that computes its values.
 * @memberof DasVar */
DAS_API DasAry* DasVar_getAry(const DasVar* pThis);

/** Is there a backing store under this variable?
 *
 * The predicate form of DasVar_getAry(), for the many call sites that only
 * want the yes/no.  false means every value is computed on demand.
 * @memberof DasVar */
#define DasVar_hasAry(P)  (DasVar_getAry(P) != NULL)

/** Point an array backed variable at a different storage array
 *
 * This swaps the backing array of the variable's generator, dropping the
 * reference on the old array and taking one on the new.  The index map is
 * left untouched, so the replacement must have the same rank as the array
 * it replaces.  The element type and the variable's units are re-derived from
 * the new array, which is the point: handing in a das_time / UTC array in
 * place of, say, a TT2000 long array re-tags the variable as UTC automatically.
 *
 * This is a stream-filter helper.  The typical use is a binary -> text
 * re-encoder that must change an epoch integer or real into a das_time so
 * the codec can emit ISO-8601.  A codec that referenced the old array must
 * be re-initialized by the caller; this call does not touch codecs.
 *
 * @param pThis An array backed variable (generator kind gtArray)
 *
 * @param pNew The replacement array.  Must be the same rank as the current
 *        backing array and use a simple (non byte-run) value type.
 *
 * @returns true on success, false (with das_error set) otherwise.
 * @memberof DasVar */
DAS_API bool DasVar_setAry(DasVar* pThis, DasAry* pNew);

/** Serialize this variable as its wire element: <scalar>, <composite> or
 * <bytes>, with an <ops> child when the formalism is anything but linear.
 *
 * Attributes whose default the schema can state are omitted (use="center", an
 * empty units=); <ops> parameters are written even at their defaults, because
 * the schema cannot carry a default behind an anyAttribute.  A generator kind
 * with no writer yet fails loud rather than emitting something plausible.
 * @memberof DasVar */
DAS_API DasErrCode DasVar_encode(DasVar* pThis, const char* sRole, DasBuf* pBuf);





/** Create a scalar variable: one value per point, no internal index.
 *
 * Both heap arguments follow the same rule: this call ADDS a reference to each
 * and the caller still owns the ones it made.  Release them when done, exactly
 * as you would after new_DasGenAry() or new_DasVarBin().  This is the rule for
 * every das2C constructor; the calls that STEAL a reference instead --
 * DasDs_addAry() and DasDim_addVar(), where an object changes owners for good
 * -- say so in their own docs.  A refusal below leaves both references alone.
 *
 * @param pGen the value source; this call adds a reference
 * @param units the variable's units.  A ';' units list FAILS: nothing in the
 *        library can hold per-component units yet, and silently keeping the
 *        first entry would misstate the data.
 * @param pForm the formalism; this call adds a reference.  Never NULL for a
 *        scalar: an absent <ops> binds the explicit linear form, so "no
 *        formalism" is a thing only a byte run may say.
 * @returns a new variable, or NULL on a loud error.  @memberof DasVar */
DAS_API DasVar* new_DasVar(
	DasGen* pGen, das_units units, DasForm* pForm
);

/* ========================================================================= *
 * DasVarBin: an operation over two variables.
 *
 * This is the one variable class with NO wire element.  das3 has no binary
 * element: a stream carries reference and offset as two separate variables
 * and the dimension combines them.  So a DasVarBin is never DECODED -- it is
 * built in code, and serializing one means MATERIALIZING it: walking the
 * operands into an array and emitting that as though it had been array-backed
 * all along.
 *
 * It keeps the operand VARIABLES, not just their generators.  A generator has
 * no units and no ops, so operands reduced to generators leave nobody able to
 * ask what was combined.  The generator half still exists underneath (DasGenOp
 * holds the operand GENERATORS and does the numeric walking); each layer keeps
 * what it knows.
 *
 * The pairing resolves ONCE, at construction: what math combined the two is
 * settled then and never asked again, so units, form and value type on the
 * result are ordinary fields from that point on.  See form.h for the rule
 * that puts the walk here rather than in the formalisms.
 * ========================================================================= */

typedef struct das_var_bin {

	DasVar base;         /* base.pForm and base.units are the RESOLVED result,
	                        decided once by binOpLeft/binOpRight at construction
	                        and thereafter just fields -- which is why
	                        _DasVar_subset() never asks what math made a value */

	DasVar*     pLeft;   /* references held; operand identity stays askable */
	DasVar*     pRight;
	int         op;      /* a D2BOP_* code from operator.h */

	/* The pairing, resolved ONCE at construction.  Six facts and one function
	   pointer; the walk reads them and never asks the forms again.  Shared on
	   copy, never re-resolved: two copies of one variable must not disagree about
	   what math they are. */
	DasBinOp*   pRecipe;

} DasVarBin;

/** Create a variable as an operation over two variables.
 *
 * The pairing is dispatched to the operands' ops objects, left first then
 * right, Python's __add__ / __radd__ model.  A pairing neither operand claims
 * FAILS LOUD (point + point, mismatched units, mismatched frames) rather than
 * being guessed at or silently commuted.
 *
 * @param cOp one of '+', '-', '*'
 * @returns a new variable, or NULL on a loud error.  @memberof DasVar */
DAS_API DasVarBin* new_DasVarBin(DasVar* pLeft, char cOp, DasVar* pRight);



/* ========================================================================= *
 * Values with an internal index: DasVarComp (<composite>) and DasVarBytes
 * (<bytes>).
 *
 * The internal layout is pure SHAPE.  The formalism does not live in the shape.
 * A 3;3 rotation and a 3;3 plain matrix share the same layout and differ only
 * in their formalism.  TRACERS ships non-rotation matrices, so this matters.
 *
 *   element is etUByte    ->  the byte run branch: string or blob.  Empty
 *                             formalism.  No table lookup.  <bytes> on the wire.
 *
 *   element is numeric    ->  base.form carries the row.  A hit gives a rich
 *                             datum.  A miss is the generic case: carry the
 *                             token, hand back plain numbers.
 * ========================================================================= */

/* ========================================================================= *
 * The byte run: DasVarBytes, the <bytes> element.  Element type is etUByte.
 *
 * One class for one wire element.  A byte run cannot carry math and there is
 * no field saying so: the CLASS says it, which is why new_DasVarBytes() takes
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

/** The numeric component run: DasVarComp, the <composite> element.
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
 *     <ops kind="vector" frame="TS2_TSCS" system="cartesian" sysorder="0;1;2"/>
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
typedef struct das_var_comp {

	DasVar base;           /* base.form: the formalism row, token, bindings */

	/* Internal layout exactly as declared.  "3" is a vector.  "3;3" is a
	   matrix.  Ragged levels are Flags.  Multi-level shapes read and write
	   end to end (examples/ex40_rotation is the 3;3 case) */
	int       nIntRank;
	ptrdiff_t aIntShape[VARIDX_MAX];

	/* Per component labels live at this structural level, readable without
	   understanding the formalism (the skippability contract: a dumb client
	   still stacks N labeled lines).  Per component units WOULD live here
	   too; until a holder exists the ';' list fails loud, see base.units. */
	/* TODO structural label storage */

} DasVarComp;

/* The byte run: DasVarBytes, the <bytes> element.  Internal rank is ALWAYS 1
   (the index is the byte number), so one extent replaces a shape array. */
typedef struct das_var_bytes {

	DasVar base;           /* base.form is unused: a byte run has no math */

	ptrdiff_t nExtent;     /* item length in bytes, VARIDX_RAGGED if var-width */

	/* string, not blob: a required trailing null in the last index, which is
	   what lets a datum be a bare char*.  A blob is stored as given and must
	   always be carried as pointer plus length. */
	bool bSentinel;

} DasVarBytes;

/** Create a numeric composite: a vector, a complex pair, a matrix.
 *
 * @param pGen the value source; this call adds a reference
 * @param units the variable's units
 * @param pForm the formalism; this call adds a reference.  Never NULL.  See
 *        new_DasVar() for the reference rule the whole layer shares.
 * @param nIntRank internal rank (1 for a vector, 2 for a "3;3" matrix)
 * @param pIntShape internal extents, VARIDX_RAGGED for a ragged level
 * @returns a new variable, or NULL on a loud error.  
 * @memberof DasVarComp 
 */
DAS_API DasVarComp* new_DasVarComp(
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
 * @param units the variable's units; NULL is fine and usual
 * @param bSentinel true for a string (required trailing null), false for a blob
 * @param nExtent item length in bytes, or VARIDX_RAGGED when variable
 * @returns a new variable, or NULL on a loud error.  
 * @memberof DasVarBytes 
 */
DAS_API DasVarBytes* new_DasVarBytes(
	DasGen* pGen, das_units units, bool bSentinel, ptrdiff_t nExtent
);

#ifdef __cplusplus
}
#endif

#endif /* _das_var_h_ */
