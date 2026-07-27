/* generator.h -- DESIGN SKETCH, not built, no companion .c, no tests.
 *
 * Part of the DasSet design pair; included by set.h.  The whole file lives
 * inside #ifdef DAS_FUTURE so it evaporates to nothing if a real source
 * accidentally includes it.  It is a terse development plan written as C, so
 * that the wire form and the code form sit side by side.  Read the comments.
 *
 * Provenance: Claude + Dude design thread, 2026-07-26.  Fallible scaffolding.
 * Nothing here is signed off.  See co_notes/libdas_wire_model_pilot.md and
 * co_notes/libdas_type_extension_map.md for the reasoning behind it.
 */

#ifdef DAS_FUTURE

#ifndef _das_generator_h_
#define _das_generator_h_

#include <das2/value.h>

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
 * What one cell holds, plus the algebra that cell obeys on its own.  This is
 * the storage half of the split of today's das_val_type; the presentation half
 * becomes das_pres_type in set.h.
 *
 * et is a RESTRICTED SUBSET of vt, and its values are pinned to match vt so a
 * cell's vt casts straight across.  A drift check (see test/future_das_set.c)
 * asserts they have not diverged.
 *
 * vt values 12 through 15 are deliberately NOT elements a generator hands out:
 *   vtIndex   (12) is DasAry ragged-child bookkeeping, internal to the array.
 *   vtText    (13) is a presentation; its cell is really a etUByte run.
 *   vtGeoVec  (14) is assembled at presentation, never read from a packed cell.
 *   vtByteSeq (15) is a boxed fat pointer; a presentation, not a stored cell.
 *
 * Note there is no element for affine time.  A TT2000 point-time is stored as a
 * plain etLong, and the affine rule rides as a scalar formalism in set.h, not as
 * an element type.  The bits of a time and a count are identical; only the
 * composition rules differ, and rules are not storage.  The one struct time
 * that IS an element is etTime, the das_time_t broken-down calendar wart. */
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
	etTime    = 11   /* == vtTime.  das_time_t struct, broken-down-time wart */
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

	   v1 SCOPE (this settles the earlier OPEN): array, sequence, and constant
	   may produce composite runs; gtBinop and gtUnop are SCALAR-ONLY and must
	   FAIL LOUD on a composite operand, exactly as var_bin.c does today.  So one
	   eval() call is enough for v1.

	   Deferred, on purpose: real composite arithmetic (vector add, complex
	   multiply, dot/cross, matrix multiply) is NOT element-wise and can change
	   the run shape, so it does not fit a single element-wise eval().  That is
	   the "composite algebra" work, a whole session of its own, and is where
	   eval() may later split into a scalar path and a formalism-aware run path.
	   Do NOT half-build it here; refuse composite operands and move on. */
	int (*eval)(
		const DasGen* pThis, const ptrdiff_t* pExtLoc, ubyte* pRun, size_t uRunMax
	);

	/* The external shape this generator can address.  For an array this is the
	   array shape minus the internal indices.  For a sequence it is the
	   declared extent. */
	int (*extShape)(const DasGen* pThis, ptrdiff_t* pShape);

	/* External length at a partial index, for ragged external runs.  Keep the
	   MIN merge semantics that DasDs_lengthIn already relies on. */
	ptrdiff_t (*lengthIn)(const DasGen* pThis, int nIdx, ptrdiff_t* pLoc);

	int     (*incRef)(DasGen* pThis);
	int     (*decRef)(DasGen* pThis);
	DasGen* (*copy)(const DasGen* pThis);

} das_gen_vt;


struct das_generator {

	das_gen_type   kind;   /* Axis A */

	const das_gen_vt* vt;

	/* Axis B lives here, on the generator, because "what one cell holds" is a
	   fact about the source, not the presentation.  The set reads this back
	   through elemType() and never stores its own copy. */
	das_elem_type elem;

	int nRef;
};


/* The concrete generators.  Sketched as headers only.  Fields are the minimum
   each needs to answer eval(); flesh them when the interface is settled. */

typedef struct das_gen_array {
	DasGen base;
	/* the backing store and the external-to-array index map.  This is the
	   surviving core of today's DasVarAry, minus everything that decided what
	   the values MEANT. */
	void*  pAry;                /* DasAry*, opaque here to keep the sketch light */
	int8_t idxmap[DASIDX_MAX];
} DasGenAry;

typedef struct das_gen_seq {
	DasGen base;
	/* intercept plus interval, one interval per external index that varies */
	ubyte aIntercept[sizeof(double)];
	ubyte aInterval[DASIDX_MAX][sizeof(double)];
} DasGenSeq;

typedef struct das_gen_const {
	DasGen base;
	ubyte aValue[sizeof(double)];
} DasGenConst;

typedef struct das_gen_op {
	DasGen  base;
	int     nOp;                /* the operator token */
	DasGen* pLeft;              /* pLeft only, for gtUnop */
	DasGen* pRight;             /* pRight also, for gtBinop */
} DasGenOp;


#ifdef __cplusplus
}
#endif

#endif /* _das_generator_h_ */

#endif /* DAS_FUTURE */
