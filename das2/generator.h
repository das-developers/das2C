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

/* Note this is a library-internal file, not stable public API is defined here */

/** @file generator.h Value sources for the DasSet layer.
 *
 * Part of the DasSet redesign pair; included by set.h.  variable.h is out of
 * the build now, so what remains is the mechanical rename back to DasVar.  Also
 * holds the model's index vocabulary and the shape-string grammar, both below.
 * Design record: co_notes/libdas_ops_class_spec.md.
 */

#ifndef _das_generator_h_
#define _das_generator_h_

#include <das2/value.h>
#include <das2/array.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ------------------------------------------------------------------------- *
 * The MODEL's index vocabulary.
 *
 * An array declares extents that are either a real count or unbounded, and it
 * says so with unsigned values (see ARYIDX_UNBOUND in array.h).  From here up
 * the stack an extent can also be BORROWED from the container or entirely
 * UNUSED by this variable, so the model needs signed values and three distinct
 * negative markers.  This is where those first mean anything, which is why they
 * live here rather than one layer down.
 *
 * The flags walk DOWNWARD from SETIDX_RAGGED on purpose.  das_varlength_merge()
 * implements the precedence lattice
 *
 *      Ragged > Number > Borrow > Unused
 *
 * with a plain max() over negative values, so the VALUES have to descend in
 * precedence order for it to be right.  Deriving each from the one above makes
 * that unbreakable instead of merely documented; you cannot reorder them
 * without rewriting the chain.
 * ------------------------------------------------------------------------- */

/** Max index count for the model.  Same number as ARYIDX_MAX and always will be,
 * since every model index eventually lands in one array's index list; a separate
 * spelling because it is a separate audience. */
#define SETIDX_MAX     ARYIDX_MAX

/* SETIDX_RAGGED lives in array.h: DasAry_shape() has to write it. */
#define SETIDX_BORROW  (SETIDX_RAGGED - 1)  /* no intrinsic extent, take the container's */
#define SETIDX_UNUSED  (SETIDX_BORROW - 1)  /* a change in this index changes nothing here */

/** Every flag is <= this, so (n > SETIDX_MAXFLG) reads as "is a real extent" */
#define SETIDX_MAXFLG  SETIDX_RAGGED

#define SETIDX_INIT_UNUSED {-3,-3,-3,-3,-3,-3,-3,-3}
#define SETIDX_INIT_BEGIN  { 0, 0, 0, 0, 0, 0, 0, 0}

/** Merge two shapes in the model's index space, taking the lattice
 *
 *      Ragged > Number > Borrow > Unused
 *
 * with two real numbers going to the smaller.  pDest is updated in place.
 *
 * NOT for array storage shapes: an array marks an unset extent with
 * ARYIDX_UNBOUND (0), which this reads as a real extent of zero.
 */
DAS_API void das_varindex_merge(int nRank, ptrdiff_t* pDest, ptrdiff_t* pSrc);

/** Merge the length of one index position, same lattice as above.  Two real
 * lengths give the SMALLER on purpose: mid-stream, variables fill different
 * sized blocks and the extent usable across all of them is the minimum
 * currently populated.  A mismatch is a normal live-read state, not an error.
 */
DAS_API ptrdiff_t das_varlength_merge(ptrdiff_t nLeft, ptrdiff_t nRight);

/** Parse a shape string in the model's index vocabulary.
 *
 * The grammar is ';'-separated tokens: a positive count, '*' ragged, '^' an
 * extent borrowed from the container, '-' an index this thing does not use.
 * The same grammar serves index= and intern=.
 *
 * @param nMaxRank the most entries pShape can take
 * @param bAllowFlags false to refuse '^' and '-'.  A dataset IS the container,
 *        so it can neither borrow an extent nor decline an index; a variable
 *        may do both.
 * @param sWhere a short context for diagnostics, e.g. "<composite> in dataset 30"
 * @returns DAS_OKAY, or an error code with das_error already called.  Rank is
 *          NOT checked against any expected value; that is the caller's policy.
 */
DAS_API DasErrCode das_shape_fromStr(
	const char* sShape, int nMaxRank, bool bAllowFlags, ptrdiff_t* pShape,
	int* pnRank, const char* sWhere
);

/** Emit a shape string, the exact inverse of das_shape_fromStr().
 * @returns the length written, or -1 if the buffer is too small.
 */
DAS_API int das_shape_toStr(
	const ptrdiff_t* pShape, int nRank, char* sBuf, int nLen
);

/* ------------------------------------------------------------------------- *
 * Axis A: the generator (gt).
 *
 * A generator is a value source and nothing more.  It answers one question:
 * what raw elements sit at a given external index?  It does not know what
 * those elements mean.  It does not know it is a vector, a complex, or a
 * timestamp.  That meaning is the set's job, one layer up in set.h.
 *
 * This is the axis that varies independently of the object type.  The same
 * geometric vector can be backed by an array on Monday and a sequence on
 * Tuesday, and it is the same vector both days.  So a set OWNS a generator by
 * composition.  A set is not a generator.
 *
 * This superseded the GENERATION half of the old DasVar subtypes: the array
 * half of DasVarAry became DasGenAry, DasVarSeq became DasGenSeq,
 * DasVarConstant became DasGenConst, and DasVarBinary/DasVarUnary became
 * DasGenOp.  The INTERPRETATION half of those old types, what a vtGeoVec means
 * or how a string reads out, did NOT come here.  That went to set.h.
 * ------------------------------------------------------------------------- */

/* Axis B: element type (et).
 *
 * What one cell holds.  This is the storage half of the split of today's
 * das_val_type; the presentation half becomes das_pres_type in set.h.
 *
 * et is a RESTRICTED SUBSET of vt, and its values are pinned to match vt so a
 * cell's vt casts straight across.  A drift check (test/future_das_set.c)
 * asserts they have not diverged.
 *
 * vt values 12 through 15 are deliberately NOT elements a generator hands out:
 *   vtIndex   (12) is DasAry ragged-child bookkeeping, internal to the array.
 *   vtText    (13) is a presentation; its cell is really a etUByte run.
 *   vtGeoVec  (14) is assembled at presentation, never read from a packed cell.
 *   vtByteSeq (15) is a boxed fat pointer; a presentation, not a stored cell.
 *
 * Note there is no element for affine time.  A TT2000 point-time is stored as a
 * plain etLong, and the affine rule rides as a formalism in set.h, not as an
 * element type.  The bits of a time and a count are identical; only the
 * composition rules differ, and rules are not storage.  The one struct time
 * that IS an element is etTime, the das_time broken-down calendar wart. */
typedef enum das_elem_type_e {
	etUnknown = 0,   /* == vtUnknown */
	etUByte   = 1,   /* == vtUByte, also the byte-run cell of a <bytes> */
	etByte    = 2,   /* == vtByte  */
	etUShort  = 3,   /* == vtUShort */
	etShort   = 4,   /* == vtShort */
	etUInt    = 5,   /* == vtUInt  */
	etInt     = 6,   /* == vtInt   */
	etULong   = 7,   /* == vtULong */
	etLong    = 8,   /* == vtLong.  a TT2000 point-time is stored here */
	etFloat   = 9,   /* == vtFloat */
	etDouble  = 10,  /* == vtDouble */
	etTime    = 11   /* == vtTime.  das_time struct, broken-down-time wart */
} das_elem_type;


/* Axis A: generator type (gt).  How the values are produced. */
typedef enum das_gen_type_e {
	gtArray = 1,   /* a lookup into a backing DasAry */
	gtSeq,         /* an intercept plus an interval, computed on demand */
	gtConst,       /* one value, everywhere */
	gtUnop,        /* a unary operation on one child generator */
	gtBinop        /* a binary operation on two child generators */
} das_gen_type;


typedef struct das_generator DasGen;

typedef struct DasGen_VTbl {

	/* Fill the caller's buffer with the internal run of raw elements this
	   generator produces at one external index.  The run length is the set's
	   item count: one element for a scalar, ncomp elements for a composite.
	   Returns the count written, or a negative das error code.

	   v1 SCOPE: array, sequence, and constant may produce composite runs;
	   gtBinop and gtUnop are SCALAR-ONLY and must FAIL LOUD on a composite
	   operand, exactly as var_bin.c does today.  So one eval() call is enough.

	   Deferred, on purpose: real composite arithmetic (vector add, complex
	   multiply, dot/cross, matrix multiply) is NOT element-wise and can change
	   the run shape.  That is the "composite algebra" work, dispatched through
	   the binop registry in set.h when its rules are registered.  Do NOT
	   half-build it here; refuse composite operands and move on. */
	int (*eval)(
		const DasGen* pThis, const ptrdiff_t* pExtLoc, ubyte* pRun, size_t uRunMax
	);

	/* The external shape this generator can address, SETIDX_RAGGED /
	   SETIDX_BORROW / SETIDX_UNUSED vocabulary.  For an array this is the
	   array shape minus the internal indices.  For a sequence it is the
	   declared extent. */
	int (*extShape)(const DasGen* pThis, ptrdiff_t* pShape);

	/* External length at a partial index, for ragged external runs.  Keep the
	   MIN merge semantics that DasDs_lengthIn already relies on. */
	ptrdiff_t (*lengthIn)(const DasGen* pThis, int nIdx, ptrdiff_t* pLoc);

	/* A pointer to the item run IN PLACE, for values that are views rather
	   than copies: a string datum carries a pointer into the backing array,
	   a blob carries pointer plus length, and both can exceed any fixed
	   buffer.  Only a backed generator can answer (gtArray); computed
	   sources return NULL, a computed string having no home to point at.
	   *pCount receives the run element count (ragged aware). */
	const ubyte* (*at)(
		const DasGen* pThis, const ptrdiff_t* pExtLoc, size_t* pCount
	);

	/* Hand back a rectangular DasAry covering the external range [pMin,pMax)
	   WITHOUT copying, when the generator's storage already has that shape.
	   Only an array-backed generator can ever answer; everything else leaves
	   this NULL.  Returning NULL means "not me, allocate and call subsetInto"
	   -- the same not-my-job answer at() gives.

	   The returned array holds one reference for the caller and saves its
	   memory owner, exactly as new_DasAry() would 
	   @see DasAry_subSetIn). 
	*/
	DasAry* (*subsetView)(
		const DasGen* pThis, int nExtRank, const ptrdiff_t* pMin,
		const ptrdiff_t* pMax
	);

	/* Write every item run in [pMin,pMax) into the caller's buffer in
	   row-major order (last external index fastest).

	   Ragged index ranges are padded out with fill values as needed.
	   So a subset of any variable, ragged or not is always rectangular.
	   This property is useful for writing CDFs and other formats that
	   will not accept variable length records.

	   Returns item runs written, or a negative das error code. */
	int (*subsetInto)(
		const DasGen* pThis, int nExtRank, const ptrdiff_t* pMin,
		const ptrdiff_t* pMax, ubyte* pBuf, size_t uBufLen
	);

	void (*destroy)(DasGen* pThis);   /* called by DasGen_decRef at zero */

} DasGen_VTbl;


struct das_generator {

	das_gen_type   kind;   /* Axis A */

	const DasGen_VTbl* pVTbl;

	/* Axis B lives here, on the generator, because "what one cell holds" is a
	   fact about the source, not the presentation.  The set reads this back
	   through DasGen_elemType and never stores its own copy. */
	das_elem_type elem;

	int nRef;
};

/* Nothing below is tagged DAS_API.  A generator is reached through the DasSet
   that owns it, so in a language with the notion these would be package
   private: visible across the library, not part of its published surface.
   Test programs link the static library and so still see them. */

/** Promote any plain numeric element to double.
 * @param et the element type held at p
 * @param p the element to read
 * @param pOut receives the promoted value
 * @returns false for a struct time, which has no single double form.
 * @memberof DasGen
 */
bool das_elem_asDouble(das_elem_type et, const ubyte* p, double* pOut);

/** Axis A, how this generator produces values (gtArray, gtSeq, ...).
 * @memberof DasGen */
#define DasGen_type(P)     ((P)->kind)

/** Axis B, what one cell of this generator holds.
 * @memberof DasGen */
#define DasGen_elemType(P) ((P)->elem)

/** Claim a reference, so the generator outlives the caller's use of it.
 * @param pThis the generator to hold
 * @returns the new reference count.
 * @memberof DasGen
 */
int DasGen_incRef(DasGen* pThis);

/** Drop a reference; the generator destroys itself at zero.
 * @param pThis the generator to release
 * @returns the remaining count.
 * @memberof DasGen
 */
int DasGen_decRef(DasGen* pThis);

/** Read the item run produced at one external location.
 *
 * The run is one element for a scalar source and the component count for a
 * composite one.  A bounded source refuses a location outside its extent
 * rather than inventing a value there.
 *
 * @param pThis the generator to read
 * @param pExtLoc the external location, one index per external rank
 * @param pRun receives the elements
 * @param uRunMax the size of pRun in BYTES
 * @returns the element count written, or a negative das error code.
 * @memberof DasGen
 */
int DasGen_eval(
	const DasGen* pThis, const ptrdiff_t* pExtLoc, ubyte* pRun, size_t uRunMax
);

/** The external shape this generator can address.
 *
 * Speaks the SETIDX_RAGGED / SETIDX_BORROW / SETIDX_UNUSED vocabulary.  For an
 * array this is the backing shape minus the internal indices; for a computed
 * source it is the extent it was declared with.
 *
 * @param pThis the generator to measure
 * @param pShape receives one entry per external index
 * @returns the external rank.
 * @memberof DasGen
 */
int DasGen_extShape(const DasGen* pThis, ptrdiff_t* pShape);

/** The length along one index at a partial location, for ragged sources.
 *
 * @param pThis the generator to measure
 * @param nIdx the index to report on
 * @param pLoc values for the indices before nIdx
 * @returns the length there, or SETIDX_UNUSED if this generator does not run
 *          along nIdx.  Keeps the MIN merge semantics DasDs_lengthIn needs.
 * @memberof DasGen
 */
ptrdiff_t DasGen_lengthIn(const DasGen* pThis, int nIdx, ptrdiff_t* pLoc);

/** Point at an item run in place, rather than copying it out.
 *
 * For values that are views rather than copies, such as a string or a blob
 * that can exceed any fixed buffer.  Only an array-backed generator can
 * answer; a computed string has no home to point at.
 *
 * @param pThis the generator to read
 * @param pExtLoc the external location
 * @param pCount receives the run's element count, ragged aware
 * @returns a pointer into the backing store, or NULL if this generator has
 *          nothing to point at.
 * @memberof DasGen
 */
const ubyte* DasGen_at(
	const DasGen* pThis, const ptrdiff_t* pExtLoc, size_t* pCount
);

/** Hand back an external index range without copying it, if that is possible.
 *
 * @param pThis the generator to read
 * @param nExtRank the rank of the range specification
 * @param pMin inclusive lower bound per index
 * @param pMax exclusive upper bound per index
 * @returns an array sharing the generator's storage and holding one reference
 *          for the caller, or NULL when no view is possible and the caller
 *          should allocate and call DasGen_subsetInto() instead.
 * @memberof DasGen
 */
DasAry* DasGen_subsetView(
	const DasGen* pThis, int nExtRank, const ptrdiff_t* pMin, const ptrdiff_t* pMax
);

/** Copy an external index range into the caller's buffer.
 *
 * Written row-major, last external index fastest.  Ragged storage is squared
 * off with the fill value, so the result is always rectangular.
 *
 * @param pThis the generator to read
 * @param nExtRank the rank of the range specification
 * @param pMin inclusive lower bound per index
 * @param pMax exclusive upper bound per index
 * @param pBuf receives the values
 * @param uBufLen the size of pBuf in BYTES
 * @returns the number of item runs written, or a negative das error code.
 * @memberof DasGen
 */
int DasGen_subsetInto(
	const DasGen* pThis, int nExtRank, const ptrdiff_t* pMin,
	const ptrdiff_t* pMax, ubyte* pBuf, size_t uBufLen
);

/** The value standing in for data this generator can't produce.
 *
 * @param pThis the generator to ask
 * @returns the fill value, or NULL when the generator has none.  Array-backed
 *          generators answer from the backing array.  A computed source has no
 *          stored fill and needs none: where it is bounded it refuses an out of
 *          range index outright, and where it is unbounded there is nothing it
 *          can miss.
 * @memberof DasGen
 */
const ubyte* DasGen_getFill(const DasGen* pThis);

/** How many elements make up one item run.
 *
 * @param pThis the generator to measure
 * @returns 1 for a scalar, the component count for a composite, or 0 when the
 *          run length varies by location.  A varying run has no fixed width to
 *          copy, so it is readable only through DasGen_at().
 * @memberof DasGen
 */
size_t DasGen_itemElems(const DasGen* pThis);

/** The internal shape of one item run: the structure behind DasGen_itemElems.
 *
 * A constant fact, like the element type -- every location this generator
 * serves hands back the same internal shape -- so ask once and keep it out of
 * the fast path.  This is what tells a 3;3 rotation from a 9 component vector;
 * the element COUNT alone cannot, which is why a declared intern= has nothing
 * to check itself against until this exists.
 *
 * The generator reports STRUCTURE, never meaning.  What a 3;3 run signifies is
 * the formalism's business, one layer up, and knowledge runs that way only.
 *
 * @param pThis the generator to measure
 * @param pShape receives one extent per internal index, SETIDX_RAGGED for a
 *        level whose extent varies by location
 * @returns the internal rank, 0 for a scalar source (pShape untouched), or a
 *          negative das error code when the source has no derivable internal
 *          shape.  Only a backed generator can derive one; a sequence or a
 *          constant has no storage to read it from and is told its shape
 *          rather than knowing it.
 * @memberof DasGen
 */
int DasGen_elemShape(const DasGen* pThis, ptrdiff_t* pShape);

/** Clone the generator object.
 *
 * The clone is a separate object holding its own reference on any backing
 * array, so re-aiming one owner's generator does not disturb another's.
 *
 * @param pThis the generator to clone
 * @returns a new generator with one reference, or NULL on error.
 * @memberof DasGen
 */
DasGen* DasGen_copy(const DasGen* pThis);

/** The backing array behind this generator.
 *
 * @param pThis the generator to ask
 * @returns the array, or NULL for a computed source.  The reference belongs to
 *          the generator; claim your own with inc_DasAry() to outlive it.
 * @memberof DasGen
 */
DasAry* DasGen_getArray(const DasGen* pThis);

/** Re-point an array-backed generator at replacement storage.
 *
 * The replacement must have the same index structure, since the generator's
 * index map is preserved.  Simple value types only; reference counts on both
 * the old and new arrays are adjusted.
 *
 * @param pThis the generator to re-aim
 * @param pNew the replacement storage
 * @returns true on success, false if refused.
 * @memberof DasGen
 */
bool DasGen_setArray(DasGen* pThis, DasAry* pNew);


/* The concrete generators. ------------------------------------------------ */

typedef struct das_gen_array {
	DasGen base;

	DasAry* pAry;               /* referenced (incRef held), owned by the dataset */

	/* external index -> array index; SETIDX_UNUSED marks a degenerate
	   external index.  Array indices past the mapped ones are the item run. */
	int  nExtRank;
	int8_t idxmap[SETIDX_MAX];

	/* elements in one item run, product of the unmapped (internal) array
	   extents.  v1 handles cubic internal shapes; a ragged INTERNAL extent
	   fails loud at construction until the run walk migrates. */
	size_t uItemElems;
} DasGenAry;

#define DASGEN_SEQ_MAXCOMP 3   /* per-component sequences cap at the geovec max */

typedef struct das_gen_seq {
	DasGen base;

	/* One intercept per component, one interval per (component, external
	   index).  Slots are sized for the largest element (a broken-down time).
	   For etTime the intercept is a das_time and the slopes are DOUBLES in
	   seconds (the affine rule at the storage layer). */
	int   nComps;               /* 1 for a scalar sequence */
	ubyte aIntercept[DASGEN_SEQ_MAXCOMP][sizeof(das_time)];
	ubyte aInterval[DASGEN_SEQ_MAXCOMP][SETIDX_MAX][sizeof(das_time)];

	int       nExtRank;                 /* declared extent, since a sequence  */
	ptrdiff_t aExtShape[SETIDX_MAX];    /* has no backing store to derive one */
} DasGenSeq;

typedef struct das_gen_const {
	DasGen base;
	ubyte aValue[sizeof(double)];

	int       nExtRank;
	ptrdiff_t aExtShape[SETIDX_MAX];
} DasGenConst;

/* gtUnop / gtBinop.  The generator stays formalism-ignorant: it holds a bare
   bytes-in bytes-out apply function handed down by the set layer, which got
   it from the binop registry.  Scalar-only in v1: children must produce
   one-element runs, fail loud otherwise. */
typedef bool (*das_gen_applyfn)(
	das_elem_type etL, das_elem_type etR,
	const ubyte* pL, const ubyte* pR, ubyte* pOut
);

typedef struct das_gen_op {
	DasGen  base;               /* base.elem is the RESULT element type */
	das_gen_applyfn apply;
	DasGen* pLeft;
	DasGen* pRight;             /* NULL for gtUnop (none exist yet) */
	double  rRightScale;        /* brings right values into left scale; 1.0
	                               when none needed.  Scaling promotes the
	                               right operand to double before apply. */
} DasGenOp;


/** Wrap a DasAry as a value source
 *
 * The generator is backed by an array though the array indices do not have
 * to match the external indices.  For example an array of frequencies for
 * a time, frequency spectrogram might only have a single index [i], but
 * the set could access these as index [i][j] where j for the set maps to i
 * for the array and i for the set is ignored.
 *
 * @param pAry The array which contains the values.  A reference is taken;
 *        the dataset keeps ownership.
 *
 * @param nExtRank The external rank, which should match the enclosing
 *        dataset, though some indices can be marked as degenerate and are
 *        thus not mapped to the backing array.
 *
 * @param pIdxMap The mapping of external indices to DasAry indices.  The
 *        offset into this array is the external index; the value is the
 *        array index.  SETIDX_UNUSED marks a degenerate external index.
 *        Not every external index needs to be mapped, and array indices
 *        not consumed by the map are the item run (the internal shape).
 *
 * @returns a new generator, or NULL on a loud error.  @memberof DasGen */
DasGen* new_DasGenAry(DasAry* pAry, int nExtRank, const int8_t* pIdxMap);

/** A computed intercept + interval value source.
 *
 * @param et element type; etLong, etFloat, etDouble and etTime.  An etTime
 *        sequence takes a das_time intercept and DOUBLE slopes in seconds.
 * @param pIntercept one element, the value at index zero
 * @param nExtRank number of external indices
 * @param pIntervals nExtRank slope slots at the SLOPE element's stride
 *        (double for etTime, the element size otherwise); zero for an index
 *        this sequence does not vary in
 * @param pExtShape nExtRank declared extents (SETIDX_RAGGED / SETIDX_BORROW
 *        allowed)
 * @returns a new generator, or NULL on a loud error.  @memberof DasGen */
DasGen* new_DasGenSeq(
	das_elem_type et, const ubyte* pIntercept, int nExtRank,
	const ubyte* pIntervals, const ptrdiff_t* pExtShape
);

/** A per-component sequence source: the item run at external index I is
 * nComps elements, component c computed from its own intercept + slopes.
 * This backs a composite whose wire form is one <sequence> per component;
 * the geovec-ness (or any other math) lives in the SET's formalism, this
 * generator just emits runs.
 *
 * @param pIntercepts nComps elements at the element stride
 * @param pIntervals nComps * nExtRank slopes, component-major at the slope
 *        stride (see new_DasGenSeq)
 * @returns a new generator, or NULL on a loud error.  @memberof DasGen */
DasGen* new_DasGenSeqN(
	das_elem_type et, int nComps, const ubyte* pIntercepts, int nExtRank,
	const ubyte* pIntervals, const ptrdiff_t* pExtShape
);

/** One value, everywhere.
 *
 * @param et element type of the value
 * @param pVal the one element every location produces
 * @param nExtRank number of external indices
 * @param pExtShape nExtRank declared extents.  A stated length bounds the
 *        constant exactly as it bounds a sequence: outside it there is no
 *        value to hand back.
 * @returns a new generator, or NULL on a loud error.
 * @memberof DasGen
 */
DasGen* new_DasGenConst(
	das_elem_type et, const ubyte* pVal, int nExtRank, const ptrdiff_t* pExtShape
);

/** A binary operation over two child generators.
 *
 * Normally reached through new_DasSetBinaryOp(), which supplies the apply
 * function from the binop registry after resolving units and formalisms.
 *
 * @param apply the item-wise operation, scalar-only in v1
 * @param etOut the RESULT element type, which need not match either child
 * @param pLeft the left operand; a reference is taken
 * @param pRight the right operand; a reference is taken
 * @param rRightScale brings right values into the left's scale, 1.0 when no
 *        scaling is needed.  Scaling promotes the right operand to double.
 * @returns a new generator, or NULL on a loud error.
 * @memberof DasGen
 */
DasGen* new_DasGenBinop(
	das_gen_applyfn apply, das_elem_type etOut, DasGen* pLeft, DasGen* pRight,
	double rRightScale
);


#ifdef __cplusplus
}
#endif

#endif /* _das_generator_h_ */
