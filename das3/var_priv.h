/* Copyright (C) 2026 Chris Piker <chris-piker@uiowa.edu>
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

/** @file var_priv.h INTERNAL header, subject to change without notice.
 *
 * Nothing here is API.  These declarations exist because the pieces of the
 * variable layer are split across translation units, not because anything
 * outside das2C should call them.  das3/core.h does not include this file and
 * will not, so it is not like form_point.h and the rest of the formalism
 * headers, which are a supported opt-in.
 *
 * Anything in here may be renamed, resignatured or deleted between releases
 * without a deprecation cycle.  Build against it and you are on your own.
 *
 * The application-facing half of a binary variable -- DasVarBin,
 * new_DasVarBin() and DasVar_materialize() -- is in variable.h, where it
 * stays.
 */

#ifndef _das_var_priv_h_
#define _das_var_priv_h_

#include <das3/form.h>       /* das_operand */
#include <das3/variable.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Naming inside this table follows what a caller can reach.
 *
 * A slot with a variable.h entry dispatching straight to it is a public
 * virtual member in all but syntax -- were this C++ there would be no explicit
 * table and no question -- so its implementations carry no underscore:
 * DasVarBytes_get, DasVarComp_intrShape, DasVarGen_lengthIn.  Changing one of
 * those changes behavior a user of DasVar sees.
 *
 * itemElems, subsetView and subsetInto have no such entry.  They exist for
 * _DasVar_subset() to divide the work, so their implementations keep the
 * underscore: _DasVarGen_subsetInto, _DasVarBin_itemElems.  Nothing outside
 * this layer can call them by any route. */
typedef struct DasVar_VTbl {

	/* Read one value at a full external index into a datum.
	 *
	 * work is caller scratch, used ONLY when there is no storage to point at.
	 * Returns 0 on success, the bytes of scratch needed if work is too small,
	 * and a negative error code otherwise.  See DasVar_get(). */
	int (*get)(
		const DasVar* pThis, ptrdiff_t* pLoc, das_byte_seq work, das_datum* pOut
	);

	/* The wire element this class serializes as: "scalar", "composite",
	   "bytes".  Axis C lives on the class, so the serializer asks the class
	   rather than deriving structure back out of a presentation vocabulary. */
	const char* (*element)(const DasVar* pThis);

	das_elem_type (*elemType)(const DasVar* pThis);   /* asks the generator */

	int (*shape)(const DasVar* pThis, ptrdiff_t* pShape);      /* full shape */
	int (*intrShape)(const DasVar* pThis, ptrdiff_t* pShape);  /* internal only */

	/* How many items exist along external index nIdx at the location pLoc.
	 *
	 * The question ragged data forces: record 5 may hold 1400 frequency bins
	 * and record 6 only 1380, so a shape cannot answer it and the caller has
	 * to name a place.
	 *
	 * A vtable slot rather than one shared implementation because the classes
	 * answer differently.  Anything built on a generator just asks it.  A
	 * DasVarBin has no generator -- its values are computed from two operands
	 * -- so it asks both and takes the SMALLER real length.
	 *
	 * That minimum is a rule about live reads, not a safety margin.  Two
	 * variables fill in different-sized blocks as a stream arrives, so at any
	 * instant the range over which BOTH actually have data is the shorter one.
	 * Operands disagreeing about length is a normal mid-stream state, not an
	 * error.  das_varlength_merge() is where that lives.
	 */
	ptrdiff_t (*lengthIn)(const DasVar* pThis, int nIdx, ptrdiff_t* pLoc);

	/* Elements per item: 1 for a scalar, 3 for a vector, 9 for a 3;3 matrix,
	 * 0 when the run is ragged and has no fixed width.
	 *
	 * A slot because the answer has two sources.  Anything built on a
	 * generator asks it; a DasVarBin has none and takes the count from the
	 * recipe, since the math can change the item shape (3;3 times 3 gives 3). */
	size_t (*itemElems)(const DasVar* pThis);

	/* Hand back a VIEW onto existing storage for an external range, or NULL.
	 *
	 * NULL is a complete answer meaning "not me, allocate and call subsetInto"
	 * -- exactly what _DasGen_subsetViewNone says one layer down for every
	 * computed generator.  Only array-backed storage can lend memory. */
	DasAry* (*subsetView)(
		const DasVar* pThis, int nExtRank, const ptrdiff_t* pMin,
		const ptrdiff_t* pMax
	);

	/* Materialize an external range into a caller-supplied buffer.
	 *
	 * A slot for the same reason: the shared implementation reads bytes out of
	 * a generator, and a computed variable has none to read from.  It walks
	 * its two operands instead and applies the recipe as it goes. */
	int (*subsetInto)(
		const DasVar* pThis, int nExtRank, const ptrdiff_t* pMin,
		const ptrdiff_t* pMax, ubyte* pBuf, size_t uBufLen
	);

	/* The first section of DasVar_toStr(): where the values come from.  A
	   bare array id with its index map, or a parenthesized expression for a
	   computed value.  Element type, units, ranges and formalism are appended
	   by the base, since every class answers those the same way. */
	char* (*prnGen)(const DasVar* pThis, char* sBuf, int nLen);

	bool (*isNumeric)(const DasVar* pThis);

	int     (*incRef)(DasVar* pThis);
	int     (*decRef)(DasVar* pThis);
	DasVar* (*copy)(const DasVar* pThis);

} DasVar_VTbl;

/* Snapshot this variable's facts for the formalism layer.
 *
 * A form never reaches up into a DasVar; it is handed one of these, built from
 * live facts at the moment of the call.  That is what keeps form_*.c free of
 * variable.h and what makes a stale cached element type impossible.
 *
 * Only the variable layer builds one of these.  A formalism RECEIVES a
 * das_operand in binOpLeft/binOpRight and never has cause to make one, which
 * is why this is not public even though das_operand is.
 *
 * @param pThis the variable to describe
 * @param pOut receives the snapshot
 * @returns false if the variable has no formalism (a byte run), which is also
 *          the answer to "may this participate in arithmetic".
 */
bool _DasVar_operand(const DasVar* pThis, das_operand* pOut);

/* Bytes in ONE item of this variable, or 0 when the run is ragged and the
 * width is a property of the location rather than the variable. */
size_t _DasVar_itemBytes(const DasVar* pThis);

/* Scratch bytes DasVar_get() needs to produce one item, 0 when every run
 * involved can be pointed at in place.  Recurses through operation operands,
 * since a nested operation needs room for its own result too. */
size_t _DasVar_runScratch(const DasVar* pThis);

/* One item run at one location.
 *
 * Hands back a pointer to the run and the bytes in it.  That pointer is the
 * variable's OWN storage when it has any -- das2C lends rather than copies --
 * and pScratch otherwise.  Returns NULL having set *pNeed when the scratch is
 * too small, or NULL with *pNeed 0 on a loud error. */
const ubyte* _DasVar_runAt(
	const DasVar* pThis, ptrdiff_t* pLoc, ubyte* pScratch, size_t uScratch,
	size_t* pBytes, size_t* pNeed
);

/* One item run of a binary operation, the class with no generator to read
 * from.  _DasVar_runAt() dispatches here so a nested operation reads its
 * operands through the same call. */
const ubyte* _DasVarBin_runAt(
	const DasVar* pBase, ptrdiff_t* pLoc, ubyte* pScratch, size_t uScratch,
	size_t* pBytes, size_t* pNeed
);

#ifdef __cplusplus
}
#endif

#endif /* _das_var_priv_h_ */
