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

/** @file generator.h Value sources for the DasSet layer.
 *
 * Part of the DasSet redesign pair; included by set.h.  Grows beside
 * variable.h until it can do everything variable.h does, then variable.h is
 * removed.  Join and succeed.  Design record:
 * co_notes/libdas_wire_model_pilot.md, co_notes/libdas_set_sketch_notes.md.
 */

#ifndef _das_generator_h_
#define _das_generator_h_

#include <das2/value.h>
#include <das2/array.h>

#ifdef __cplusplus
extern "C" {
#endif

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
 * This supersedes the GENERATION half of today's DasVar subtypes.  The array
 * half of DasVarAry becomes DasGenAry.  DasVarSeq becomes DasGenSeq.
 * DasVarConstant becomes DasGenConst.  DasVarBinary and DasVarUnary become
 * DasGenOp.  The INTERPRETATION half of those old types, what a vtGeoVec means
 * or how a string reads out, does NOT come here.  It goes to set.h.
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

typedef struct das_gen_vt {

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

	/* The external shape this generator can address, DASIDX_RAGGED /
	   DASIDX_BORROW / DASIDX_UNUSED vocabulary.  For an array this is the
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

	void (*destroy)(DasGen* pThis);   /* called by DasGen_decRef at zero */

} das_gen_vt;


struct das_generator {

	das_gen_type   kind;   /* Axis A */

	const das_gen_vt* vt;

	/* Axis B lives here, on the generator, because "what one cell holds" is a
	   fact about the source, not the presentation.  The set reads this back
	   through DasGen_elemType and never stores its own copy. */
	das_elem_type elem;

	int nRef;
};

/** Promote any plain numeric element to double (false for struct times).
 * @memberof DasGen */
DAS_API bool das_elem_asDouble(das_elem_type et, const ubyte* p, double* pOut);

#define DasGen_type(P)     ((P)->kind)
#define DasGen_elemType(P) ((P)->elem)

DAS_API int DasGen_incRef(DasGen* pThis);

/** Drop a reference; the generator destroys itself at zero.
 * @returns the remaining count. @memberof DasGen */
DAS_API int DasGen_decRef(DasGen* pThis);

DAS_API int DasGen_eval(
	const DasGen* pThis, const ptrdiff_t* pExtLoc, ubyte* pRun, size_t uRunMax
);
DAS_API int DasGen_extShape(const DasGen* pThis, ptrdiff_t* pShape);
DAS_API ptrdiff_t DasGen_lengthIn(const DasGen* pThis, int nIdx, ptrdiff_t* pLoc);
DAS_API const ubyte* DasGen_at(
	const DasGen* pThis, const ptrdiff_t* pExtLoc, size_t* pCount
);

/** Clone the generator object; backing arrays are shared by reference.
 * @memberof DasGen */
DAS_API DasGen* DasGen_copy(const DasGen* pThis);

/** The backing array, or NULL for computed sources.  @memberof DasGen */
DAS_API DasAry* DasGen_getArray(const DasGen* pThis);

/** Re-point an array-backed generator at replacement storage of the same
 * element type (reference counts adjusted).  @memberof DasGen */
DAS_API bool DasGen_setArray(DasGen* pThis, DasAry* pNew);


/* The concrete generators. ------------------------------------------------ */

typedef struct das_gen_array {
	DasGen base;

	DasAry* pAry;               /* referenced (incRef held), owned by the dataset */

	/* external index -> array index; DASIDX_UNUSED marks a degenerate
	   external index.  Array indices past the mapped ones are the item run. */
	int  nExtRank;
	int8_t idxmap[DASIDX_MAX];

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
	ubyte aInterval[DASGEN_SEQ_MAXCOMP][DASIDX_MAX][sizeof(das_time)];

	int       nExtRank;                 /* declared extent, since a sequence  */
	ptrdiff_t aExtShape[DASIDX_MAX];    /* has no backing store to derive one */
} DasGenSeq;

typedef struct das_gen_const {
	DasGen base;
	ubyte aValue[sizeof(double)];

	int       nExtRank;
	ptrdiff_t aExtShape[DASIDX_MAX];
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
 *        array index.  DASIDX_UNUSED marks a degenerate external index.
 *        Not every external index needs to be mapped, and array indices
 *        not consumed by the map are the item run (the internal shape).
 *
 * @returns a new generator, or NULL on a loud error.  @memberof DasGen */
DAS_API DasGen* new_DasGenAry(DasAry* pAry, int nExtRank, const int8_t* pIdxMap);

/** A computed intercept + interval value source.
 *
 * @param et element type; etLong, etFloat, etDouble and etTime.  An etTime
 *        sequence takes a das_time intercept and DOUBLE slopes in seconds.
 * @param pIntercept one element, the value at index zero
 * @param nExtRank number of external indices
 * @param pIntervals nExtRank slope slots at the SLOPE element's stride
 *        (double for etTime, the element size otherwise); zero for an index
 *        this sequence does not vary in
 * @param pExtShape nExtRank declared extents (DASIDX_RAGGED / DASIDX_BORROW
 *        allowed)
 * @returns a new generator, or NULL on a loud error.  @memberof DasGen */
DAS_API DasGen* new_DasGenSeq(
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
DAS_API DasGen* new_DasGenSeqN(
	das_elem_type et, int nComps, const ubyte* pIntercepts, int nExtRank,
	const ubyte* pIntervals, const ptrdiff_t* pExtShape
);

/** One value, everywhere.  @memberof DasGen */
DAS_API DasGen* new_DasGenConst(
	das_elem_type et, const ubyte* pVal, int nExtRank, const ptrdiff_t* pExtShape
);

/** A binary operation over two child generators.  Normally reached through
 * new_DasSetBinaryOp, which supplies the apply function from the binop
 * registry after resolving units and formalisms.  Takes a reference on both
 * children.  @memberof DasGen */
DAS_API DasGen* new_DasGenBinop(
	das_gen_applyfn apply, das_elem_type etOut, DasGen* pLeft, DasGen* pRight,
	double rRightScale
);


#ifdef __cplusplus
}
#endif

#endif /* _das_generator_h_ */
