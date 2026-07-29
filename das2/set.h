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
 * The successor to variable.h.  It grows here beside the current code; when
 * it can do everything variable.h and friends do, they are removed.  No rip
 * and replace.  Join and succeed.  XML snippets show the stream form of each
 * type right next to the struct that carries it.
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
 * Design record: co_notes/libdas_wire_model_pilot.md,
 * co_notes/libdas_type_extension_map.md, co_notes/libdas_set_sketch_notes.md.
 */

#ifndef _das_set_h_
#define _das_set_h_

#include <das2/descriptor.h>
#include <das2/buffer.h>
#include <das2/datum.h>
#include <das2/units.h>
#include <das2/generator.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Naming convention (Dude's): heap objects with constructors and reference
   counts take Java-style CamelCase (DasSet, DasGen, DasIntrSet).  Stack,
   embedded, and static things take snake_case (das_elem_type, das_form_kind,
   das_set_vt), matching das_time and das_geovec.  A formalism table row and a
   vtable are static, not heap, so they are snake_case. */

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
 *   Axis C, the structure.  Carried by the C CLASS, not a field: a bare
 *           DasSet has no internal index (a scalar); a DasIntrSet has one,
 *           and its das_intrset_class says what the internal run holds
 *           (string, blob, or plain numbers).  The RICH presentation
 *           vocabulary (vector, complex, rotation...) is NOT stored: it is
 *           a view computed from C + D, because for composites the
 *           formalism fully determines it and duplicated facts drift.
 *           (Historical note: this axis began life as a four-class list,
 *           DasSetScalar/Str/Blob/Comp, was briefly one stored family
 *           field, and now rests at two classes plus a content enum --
 *           the machinery boundary was the honest cut all along.)
 *
 *   Axis D, the formalism.  What math the values obey: none, point, geovec,
 *           complex, rotation.  A set HAS one, symmetric with the generator:
 *           pGen says how values are produced, form says what they mean to
 *           arithmetic.  On the wire this is the skippable <formalism>
 *           element; in C it is an embedded das_formalism binding the set to
 *           a static kind row plus that row's context bindings.
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

/* The DERIVED presentation vocabulary: what one word tells a consumer this
   set hands out.  Computed by presType() from the class + content +
   formalism, never stored (for composites the formalism fully determines
   it). */
typedef enum das_pres_type_e {
	prUnknown = 0,
	prScalar,     /* one value per point, no internal index.  <scalar> */
	prString,     /* a ubyte run, sentinel REQUIRED, hands out a bare char* */
	prBlob,       /* a ubyte run, no sentinel, always carried as ptr + len */
	prVector,     /* a numeric run, geometric vector formalism */
	prComplex,    /* a numeric run of two, complex formalism */
	prRotation,   /* a numeric run, 3;3 or a quaternion, rotation formalism */
	prMatrix,     /* a numeric run, r;c, a plain matrix, no rotation promise */
	prImage,      /* a numeric run on a two dimensional grid, plus planes */
	prGeneric     /* a numeric run whose formalism is known only as a string */
} das_pres_type;


/* ========================================================================= *
 * Axis D: the formalism.  One mechanism for scalars and composites alike.
 *
 * A das_form_kind is one row of the formalism table: a compiled-in registry,
 * one row per known formalism.  Adding point, geovec, complex, rotation,
 * matrix, image is adding rows, not classes.  The row is where the old
 * per-type knowledge in var_ary.c and value.c collapses.
 *
 * A das_formalism is the per-set INSTANCE: which row (if any), the wire token
 * as it arrived, and the row's context bindings.  It is embedded in DasSet by
 * value; the rows are static.  Contrast the generator: pGen is heap-owned,
 * refcounted, swappable.  The formalism is a bound classification, not an
 * owned machine.
 * ========================================================================= */

typedef struct das_set DasSet;

#define DASFORM_MAX_BINDS 10

typedef struct das_form_kind {

	const char* sToken;         /* the wire type= value: "point", "geovec",
	                               "complex", ...  The linear row's token is
	                               "linear" though the wire idiom is absence;
	                               das_formalism_init maps absent to that row */

	das_elem_type elemWanted;   /* numeric for every current row */

	/* Read one item's internal run and stamp a datum template: a geovec, a
	   complex pair, a quaternion.  For a scalar row (point) the run is one
	   element.  A function pointer in a data row, not a subclass. */
	bool (*pack)(const DasSet* pThis, const ubyte* pRun, das_datum* pOut);

	/* Print the internal structure for expression(). */
	char* (*prnIntr)(const DasSet* pThis, char* sBuf, int nLen);

	/* True if a client that does not understand this formalism may still stack
	   the components as separate lines.  True for geovec, complex, rotation,
	   matrix, image.  The byte run branch never reaches the table. */
	bool bUserFacing;

} das_form_kind;


typedef struct das_formalism {

	/* The formalism table row, or NULL.  Linear IS a row: the unstated
	   formalism ordinary numbers obey, bound explicitly when no <formalism>
	   arrives, so every operation dispatches through one registry with no
	   bypass path for "plain" math.  NULL therefore means exactly one thing:
	   the token missed the table, the generic case.  The C library is then
	   its own dumb client: it knows the elements are numeric, carries the
	   layout it was told, exposes plain numbers, and REFUSES arithmetic
	   (rules it does not know are rules it must not guess). */
	const das_form_kind* pKind;

	/* The wire type= token exactly as it arrived.  Always set when a
	   <formalism> was present, even on a table hit, so a generic reader can
	   name the formalism it is skipping.  Empty means none arrived. */
	char sToken[32];

	/* The binding store, role to value: a geovec's frame, system, sysorder
	   and surface, a rotation's from and to.  It lives HERE because this is
	   where the wire puts it (attributes of the <formalism> sub-element), not
	   on the set body.  DasSet_getFrame() is a typed accessor over this
	   store, so common geovec code never sees the generality.  With the
	   dim-level frame cascade dropped (2026-07-26) there is no inherit step:
	   the bindings on the variable are the whole truth.  Context REFS (frame,
	   surface) and plain params (system, sysorder) share the store; which
	   roles are refs is the row's knowledge. */
	struct {
		char sRole[12];
		char sVal[48];
	} aBind[DASFORM_MAX_BINDS];
	int nBinds;

} das_formalism;

/** Add one role -> value binding to a formalism instance.  @memberof DasSet */
DAS_API DasErrCode das_formalism_bind(
	struct das_formalism* pThis, const char* sRole, const char* sVal
);

/** Get a binding by role, or NULL if the role is unbound.  @memberof DasSet */
DAS_API const char* das_formalism_getBind(
	const struct das_formalism* pThis, const char* sRole
);


/* --- The binop registry: cross-formalism arithmetic ----------------------
 *
 * Binary operations between formalisms are PAIRWISE facts, so they do not
 * belong on the rows (a row owning a "mulRight that switches on partner"
 * smears N x N knowledge across every row).  They live in a second, sparse
 * table keyed by the ORDERED triple (op, kind left, kind right).  Ordered on
 * purpose: rotation * vector is defined, vector * rotation is not, and a
 * missing key FAILS LOUD rather than auto-commuting.
 *
 * A rule takes and returns full formalisms, bindings included, because the
 * math acts on bindings too: a rotation from=TSCS to=GEI applied to a vector
 * bound to TSCS yields a vector bound to GEI, and applied to anything else it
 * refuses.  Binding checks are the rule's job; that is where a frame-mismatch
 * fail-loud belongs.
 *
 * Population plan: v1 registers the linear ring (add/sub/mul over plain
 * numbers, the rules every scalar always had) plus the point affine rules,
 * so the whole dispatch path is exercised by math we already ship.  The
 * composite rules (geovec add/dot/cross, rotation compose/apply, complex,
 * matrix) come in their own session; each is one new row here, no existing
 * code touched.  Until then every composite pair misses, which IS the settled
 * v1 rule: gtBinop/gtUnop are scalar-only and fail loud on composites.
 *
 * A generic formalism (pKind NULL) can never match a rule slot, so unknown
 * math is refused by construction.  A "scalar scales anything" wildcard, when
 * composite rules land, will use a dedicated sentinel, never NULL.
 */

typedef enum das_form_op_e { dfoAdd, dfoSub, dfoMul } das_form_op;

typedef struct das_form_rule {
	das_form_op op;
	const das_form_kind* pLeft;
	const das_form_kind* pRight;

	/* Check bindings and units, name the result.  Returns false (loudly) when
	   the pairing is illegal: wrong frame, inconvertible units, wrong shapes.
	   pRightScale receives the factor that brings right-hand values into the
	   left side's scale (1.0 when none is needed); a rule that sets it other
	   than 1.0 promises the result element is wide enough for the scaled
	   value (scaling promotes to double, matching the retired variable
	   layer). */
	bool (*resolve)(const das_formalism* pL, const das_formalism* pR,
	                das_units uL, das_units uR,
	                das_formalism* pOut, das_units* pUnitsOut,
	                double* pRightScale);

	/* The result element type for a given operand pairing.  A rule owns this
	   because it is math, not storage: time minus time is a double span even
	   though both operands are struct times. */
	das_elem_type (*outElem)(das_elem_type etL, das_elem_type etR);

	/* Item-wise evaluation: both operand runs in, result run out.  Item-wise
	   is enough even for shape-changing math (a dot product shrinks the
	   INTERNAL shape only), so DasGenOp's single-call eval survives.  Both
	   element types ride along because a rule is registered per formalism
	   pair, not per storage width, and the pair may mix widths (a broken-down
	   time plus a double span). */
	bool (*apply)(
		das_elem_type etL, das_elem_type etR,
		const ubyte* pLRun, const ubyte* pRRun, ubyte* pOutRun
	);
} das_form_rule;

/** Look up a formalism table row by its wire type= token.
 * @returns the row, or NULL: unknown tokens are the generic case, not an
 *          error.  @memberof DasSet */
DAS_API const das_form_kind* das_form_lookup(const char* sToken);

/** The explicit linear row, the formalism no <formalism> element implies.
 * @memberof DasSet */
DAS_API const das_form_kind* das_form_linear(void);

/* Internal function for merging set and dimension shapes.
 *
 * Combining index rules:
 *
 *    '*' + '-'    = '*'
 *    '*' + Number = Number      ('*' means undefined length, represented by)
 *    '-' + Number = Number      ('-' means no dependency,    negative nums )
 *    Big Number + Small Number = Small Number
 */
DAS_API void das_varindex_merge(int nRank, ptrdiff_t* pDest, ptrdiff_t* pSrc);

/* Internal function for merging length in a particular dimension. */
DAS_API ptrdiff_t das_varlength_merge(ptrdiff_t nLeft, ptrdiff_t nRight);

/** Set index printing direction.
 *
 * Switch printing of set index order in _toStr() calls.  Does not affect
 * the internal layout of the data.  The default print order is "Fastest
 * index last."
 *
 * WARNING: This function is NOT thread safe.
 */
DAS_API void das_varindex_prndir(bool bFastLast);

/** Look up a binop rule by the ORDERED (op, left kind, right kind) triple.
 * @returns the rule, or NULL: a miss means the pairing is undefined and the
 *          caller must refuse the operation, loudly.  A generic formalism's
 *          NULL kind matches nothing, so unknown math misses by
 *          construction.  @memberof DasSet */
DAS_API const das_form_rule* das_form_findRule(
	das_form_op op, const das_form_kind* pLeft, const das_form_kind* pRight
);

/** Bind a formalism instance from a wire token.  "" or NULL binds the
 * explicit linear row (the wire's unstated default made concrete); a table
 * miss keeps the token and leaves pKind NULL, the generic case.
 * @memberof DasSet */
DAS_API DasErrCode das_formalism_init(struct das_formalism* pThis, const char* sToken);

/* The inaugural registry content: the linear ring plus the point pilot.
 *
 *   { dfoAdd/dfoSub/dfoMul, linear, linear }  ->  linear (the common ops)
 *   { dfoSub, point,  point  }  ->  interval  (units via Units_interval)
 *   { dfoAdd, point,  linear }  ->  point     (interval units required)
 *   { dfoAdd, linear, point  }  ->  point     (registered, addition of an
 *                                              interval commutes; stated
 *                                              explicitly, never inferred)
 *   { dfoAdd, point,  point  }  ->  REFUSED   (a lookup miss, not a rule)
 *
 * Time lives here: a datetime is etLong TT2000 ticks with the point row.
 * point also fits any zero-referenced axis, a position measured from a
 * chosen origin. */


/* ========================================================================= *
 * DasSet, the base.  Supersedes DasVar.
 * ========================================================================= */

/* ========================================================================= *
 * The scalar case.  One value per point, no internal index.  Supersedes the
 * scalar case of DasVarAry.  There is NO DasScalarSet struct: with the
 * formalism hoisted into the base, a scalar is just a DasSet whose pres is
 * prScalar, and its only formalisms so far are none (linear) or point.
 *
 * Wire, a plain linear scalar.  Linear is the default, so nothing is stated:
 *
 *   <scalar semantic="real" units="Hz" index="*">
 *     <packet numItems="1" itemBytes="8" encoding="LEreal"/>
 *   </scalar>
 *
 * Wire, a time scalar.  This is where the word "point" appears.  The affine
 * rule is a <formalism> whose type alone suffices, so it has no sub-element.
 * semantic="datetime" still rides along as the display and calendar-units
 * directive.  Child order is properties, then formalism, then generator:
 *
 *   <scalar semantic="datetime" units="TT2000" index="*">
 *     <formalism type="point"/>
 *     <packet numItems="1" itemBytes="8" encoding="LEint"/>
 *   </scalar>
 *
 * A future binding-bearing scalar formalism (a scalar with a point spread
 * function) is no longer an open item: it is a formalism with bindings on a
 * scalar, exactly like geovec on a composite.  Same struct, same store.
 * ========================================================================= */

typedef struct das_set_vt {

	/* Read one value at a full external index into a datum.  For a composite
	   this packs the whole item, a vector or a complex, into one datum.  For a
	   generic composite on a table miss it returns the run as plain numbers. */
	bool (*get)(const DasSet* pThis, ptrdiff_t* pLoc, das_datum* pOut);

	das_elem_type (*elemType)(const DasSet* pThis);   /* asks the generator */
	das_pres_type (*presType)(const DasSet* pThis);

	int (*shape)(const DasSet* pThis, ptrdiff_t* pShape);      /* full shape */
	int (*intrShape)(const DasSet* pThis, ptrdiff_t* pShape);  /* internal only */

	char* (*expression)(const DasSet* pThis, char* sBuf, int nLen, unsigned int uFlags);

	bool (*isNumeric)(const DasSet* pThis);

	int     (*incRef)(DasSet* pThis);
	int     (*decRef)(DasSet* pThis);
	DasSet* (*copy)(const DasSet* pThis);

} das_set_vt;


struct das_set {

	DasDesc base;          /* a set IS a descriptor: it has properties, a
	                          parent, and it serializes */

	const das_set_vt* vt;

	DasGen* pGen;          /* Axis A.  The set owns a generator but is not one.
	                          Swapping array for sequence does not change the
	                          set's type. */


	das_formalism form;    /* Axis D.  Embedded by value.  A plain number
	                          binds the explicit LINEAR row; only <bytes>
	                          is truly empty (pKind NULL, no token): a byte
	                          run has no auto-math to bind. */

	/* One unit.  The wire allows a semicolon list of exactly intern-many
	   (degrees;degrees;km for a geodetic position) but das_geovec holds one
	   units set and the sizeof(das_time) >= sizeof(das_geovec) rule leaves no
	   room for more, so v1 FAILS LOUD on a ';' list the holder cannot carry
	   (Dude's ruling 2026-07-27; a scope cut, stated, not silent).  On an
	   angular-system geovec the single unit is the altitude/radial unit and
	   the angles are always degrees. */
	das_units units;

	int   nRef;
	void* pUser;
};

#define DasSet_presType(P) ((P)->vt->presType(P))
#define DasSet_units(P)    ((P)->units)
#define DasSet_gen(P)      ((P)->pGen)

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
 * @param pShape a pointer to an array of size DASIDX_MAX.  Each element of
 *        the array will be filled in with one of the following:
 *
 *        * An integer from 0 to LONG_MAX to indicate the valid index range.
 *
 *        * The value DASIDX_UNUSED to indicate the given index position is
 *          ignored by this set
 *
 *        * The value DASIDX_RAGGED to indicate that the valid index range is
 *          variable and depends on the values of other indices.
 *
 *        * The value DASIDX_BORROW to indicate the set adopts whatever
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
 * @param pShape a pointer to an array of size DASIDX_MAX - 1.  Each element
 *        is filled in with either an integer from 0 to LONG_MAX (a typical
 *        geometric vector puts 3 here) or DASIDX_RAGGED to indicate the
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
 *         DASIDX_UNUSED if this set does not run along the given index, or
 *         DASIDX_BORROW for the dependent index of an un-bounded sequence.
 *
 * @see DasAry_lengthIn
 * @memberof DasSet */
DAS_API ptrdiff_t DasSet_lengthIn(const DasSet* pThis, int nIdx, ptrdiff_t* pLoc);

/** Does a given external index even matter to this set?
 *
 * @param pThis A pointer to a set
 *
 * @param iIndex The index in question, from 0 to DASIDX_MAX - 1
 *
 * @return true if varying this index can not cause the set's output to
 *         change, false if it can.
 * @memberof DasSet */
DAS_API bool DasSet_degenerate(const DasSet* pThis, int iIndex);

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

/** Get the component directions for a geometric vector composite
 *
 * Geometric vectors are defined in terms of a reference frame and a
 * coordinate system.  Each coordinate system type has a canonical set of
 * vector (or angle) definitions in a right-handed order.  A composite may
 * not have its components in that order and might not carry all of them.
 *
 * Use this function to see how vector data maps into its coordinate
 * system.  The component map provides the match-ups as depicted:
 * <pre>
 *    +-------+-------+-------+
 *    | dir0  | dir1  | dir2<-|--- Value is the coordsys canonical index
 *    +-------+-------+-------+
 *    ^
 *    |
 *    +-- Slot index corresponds to the storage order of the components
 * </pre>
 *
 * @param pThis A composite set carrying the geovec formalism
 *
 * @param pNumDirs A pointer to a location to receive the component count
 *
 * @param pDirs A pointer to at least 3 bytes to receive the component
 *        direction map, or NULL if only the count is wanted
 *
 * @returns The component count, or 0 when the set is not a geovec.
 * @memberof DasSet */
DAS_API ubyte DasSet_vecMap(const DasSet* pThis, ubyte* pNumDirs, ubyte* pDirs);

/** Component (or scalar) display labels, one string per user-facing line.
 * @memberof DasSet */
DAS_API int das_makeCompLabels(const DasSet* pVar, char** psBuf, size_t uLenEa);

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

/** Serialize this set as its wire element.  NOT IMPLEMENTED until the writer
 * phase; fails loud so nothing silently emits old-dialect headers.
 * @memberof DasSet */
DAS_API DasErrCode DasSet_encode(DasSet* pThis, const char* sRole, DasBuf* pBuf);

/** Create a scalar set: one value per point, no internal index.
 *
 * @param pGen the value source; this call takes a reference
 * @param units the set's units.  A ';' units list FAILS: nothing in the
 *        library can hold per-component units yet, and silently keeping the
 *        first entry would misstate the data.
 * @param sFormToken the wire formalism type=, "" or NULL for plain linear,
 *        "point" for the affine rule.  Unknown tokens are kept generically.
 * @returns a new set, or NULL on a loud error.  @memberof DasSet */
DAS_API DasSet* new_DasSetScalar(
	DasGen* pGen, das_units units, const char* sFormToken
);

/** Create a scalar set as an operation over two scalar sets.
 *
 * The pairing is dispatched through the binop registry: the operands'
 * formalisms and units are resolved to a result formalism and units, or the
 * whole construction FAILS LOUD (point + point, mismatched units, any
 * composite operand).  This supersedes new_DasVarBinary; v1 keeps its
 * scalar-only rule.
 *
 * @param cOp one of '+', '-', '*'
 * @returns a new set, or NULL on a loud error.  @memberof DasSet */
DAS_API DasSet* new_DasSetBinaryOp(DasSet* pLeft, char cOp, DasSet* pRight);


/* ========================================================================= *
 * DasIntrSet.  A value with an internal index.  Supersedes DasVarVecAry and the
 * roughly thirty scattered das_val_type switch sites the scout found.
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

/* Axis C's content half: what a DasIntrSet's internal run holds.  icString
   and icBlob are the two faces of <bytes>, split by the sentinel rule (utf8
   vs raw/base64 encoding); icNumeric is <composite>.  A bare DasSet (a
   <scalar>) needs no entry: having no internal index IS its structure. */
typedef enum das_intrset_class_e {
	icUnknown = 0,
	icString,     /* byte run, sentinel REQUIRED, hands out a bare char* */
	icBlob,       /* byte run, no sentinel, always carried as ptr + len  */
	icNumeric     /* component run of plain numbers; math via <formalism> */
} das_intrset_class;

/* ========================================================================= *
 * The byte run branch: string and blob.  Element is etUByte.
 *
 * These are DasIntrSet in C, because a byte run IS an internal index, but they
 * are ONE wire element, <bytes>, because their bytes are not user facing lines.
 * The structural family comes from the element name, so no semantic is needed
 * for structure.  A byte run's formalism is EMPTY (a byte run has no
 * auto-math), matching <bytes> carrying no <formalism> on the wire.  string
 * and blob are told apart by the packet encoding, an explicit required
 * triplet:
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
 * The only difference between string and blob is the sentinel.  A string ends
 * in a REQUIRED trailing null in the array's last index, which is why it can
 * hand out a bare char*.  A blob is stored AS GIVEN with no sentinel, which is
 * why it must always be carried as pointer plus length.  Fixed versus ragged is
 * orthogonal; both can be either.
 *
 * OPEN: whether the byte run branch is just DasIntrSet with a etUByte element
 * and a small sentinel flag, or earns a thin DasStrSet subclass.  The scout
 * showed string and blob take a distinct codec path, ITEMLEN and WRAP, while
 * numeric composites ride the plain path.  Decide against real code.
 * ========================================================================= */

/* typedef struct das_str_set { DasIntrSet base; ... } DasStrSet;  back pocket */

/* --- The numeric composite wire forms ---
 *
 * A composite carries semantic in its interpretation role, same as a scalar:
 * integer or real.  It is the parse target for utf8 components and redundant
 * with the encoding for binary.  The formalism is always a <formalism type="...">
 * whose type names the math.  The wire attribute is type= to match <stream type=>
 * and <p type=>; the C lookup key is das_form_kind.sToken (a separate audience).
 * The sub-element is OPTIONAL, present only when the formalism needs bindings or
 * params a classification string cannot carry.  The type token and the
 * sub-element name match, and the type doubles as the label a client shows when
 * it cannot parse the sub-element.
 *
 * Complex needs no context, so type alone suffices and no sub-element is spent.
 * This is the original das3_from_cdf request, landed with no new semantic and no
 * codec change:
 *
 *   <composite semantic="int" intern="2" index="*">
 *     <formalism type="complex"/>
 *     <packet numItems="2" itemBytes="1" encoding="byte"/>
 *   </composite>
 *
 * A geometric vector needs a frame, so it carries a sub-element for the frame,
 * body, and component order, plus a refs summary a dumb client reads at the skip
 * boundary.  Child order reads purpose to bytes: properties, formalism, generator:
 *
 *   <composite semantic="real" units="m" intern="3" index="*">
 *     <properties><p name="label" type="stringArray">B_x;B_y;B_z</p></properties>
 *     <formalism type="geovec" refs="TS2_TSCS">
 *       <geovec frame="TS2_TSCS" body="-64678" sysorder="0;1;2"/>
 *     </formalism>
 *     <packet numItems="3" itemBytes="4" encoding="LEreal"/>
 *   </composite>
 *
 * A rotation carries two bindings.  Layout is 3;3, pure shape; the formalism,
 * not the shape, says "rotation":
 *
 *   <composite semantic="real" intern="3;3" index="*">
 *     <formalism type="rotation" refs="TSCS;GEI2000">
 *       <rotation from="TSCS" to="GEI2000"/>
 *     </formalism>
 *     <packet numItems="9" itemBytes="8" encoding="LEreal"/>
 *   </composite>
 *
 * The name <formalism> rather than <algebra> is on purpose: a point spread
 * function on an image, or a sampling response, is a rule a capable client
 * needs but is not an algebra.  Same container, same skip boundary.
 */


typedef struct das_intr_set {

	DasSet base;           /* base.form: the formalism row, token, bindings */

	das_intrset_class content;   /* what the internal run holds */

	/* Internal layout exactly as declared.  "3" is a vector.  "3;3" is a
	   matrix.  "4;*" is a ragged multi level composite.  Ragged levels are
	   negative.  This is the axis that first exercises internal rank greater
	   than one, which today's var_ary.c refuses. */
	int       nIntRank;
	ptrdiff_t aIntShape[DASIDX_MAX];

	/* Per component labels live at this structural level, readable without
	   understanding the formalism (the skippability contract: a dumb client
	   still stacks N labeled lines).  Per component units WOULD live here
	   too; until a holder exists the ';' list fails loud, see base.units. */
	/* TODO structural label storage */

} DasIntrSet;

/** Create a set whose values have an internal index: a numeric composite,
 * a string, or a blob.
 *
 * @param ic icNumeric, icString or icBlob.  The byte-run classes require an
 *        etUByte generator and take NO formalism (a byte run has no
 *        auto-math); icNumeric requires a numeric element.
 * @param pGen the value source; this call takes a reference.  For byte runs
 *        the generator must be array-backed: a string datum is a POINTER
 *        into storage and a computed string has no home to point at.
 * @param units the set's units; NULL/empty is fine for strings and blobs
 * @param sFormToken the formalism type= token, NULL for none/linear
 * @param nIntRank internal rank (1 for vectors, strings, blobs)
 * @param pIntShape internal extents, DASIDX_RAGGED for a ragged level
 * @returns a new set, or NULL on a loud error.  @memberof DasSet */
DAS_API DasIntrSet* new_DasIntrSet(
	das_intrset_class ic, DasGen* pGen, das_units units, const char* sFormToken,
	int nIntRank, const ptrdiff_t* pIntShape
);

/** What the internal run of a DasIntrSet holds.  @memberof DasSet */
#define DasIntrSet_class(P) ((P)->content)

/** Typed accessor for the most common binding.  @memberof DasSet */
#define DasSet_getFrame(P) das_formalism_getBind(&((P)->form), "frame")



/* ========================================================================= *
 * Far corners, one line each.  Sketched shallow on purpose.
 * ========================================================================= *
 *
 * prMatrix   a numeric composite, r;c layout, formalism token "matrix", no
 *            rotation promise.  A formalism table row.
 *
 * prImage    a numeric composite on a two dimensional grid plus planes.  A
 *            point spread function attaches as a <given> context ref, per the
 *            note now in context.h, carried through <formalism>.  v3.1
 *            territory.  Note that a MEASURED image is often just a <scalar>
 *            field over a 2-D external grid, block-encoded (png, jpeg) over that
 *            grid; prImage is for display images with planes and colorspace,
 *            not measurement grids.
 *
 * prGeneric  already handled above as the table miss.  It is the base
 *            DasIntrSet behavior, not a separate type.
 */

#ifdef __cplusplus
}
#endif

#endif /* _das_set_h_ */
