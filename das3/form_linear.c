/* Copyright (C) 2026 Chris Piker <chris-piker@uiowa.edu>
 *
 * Author: C. Piker, via Claude Opus 5
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

/* The linear formalism: ordinary numbers.
 *
 * Linear is a REAL formalism, not an absent one.  The wire idiom is absence --
 * a <scalar> with no <ops> is linear -- but in C it binds an actual object, so
 * NULL keeps meaning exactly one thing (a byte run, no math at all) and every
 * operation dispatches through one path with no bypass for "plain" math.
 *
 * Wire: nothing.  _linear_encode writes zero bytes on purpose, so the reader
 * that produced this form and the writer that consumes it agree that the
 * default is stated by saying nothing.  <ops kind="linear"/> is legal to
 * write and reads back here, but is never emitted.
 *
 * This file has NO arithmetic of its own.  The elementwise math is
 * das_value_binop() in the value layer, shared with every other formalism
 * that needs an item-wise operation.  What linear owns is the RULES: which
 * operators are legal, what the result units are, and what the result type
 * is.
 *
 * OPERATIONS ON A RUN ARE ELEMENTWISE, requiring an exact shape match.  For
 * multiplication that is the HADAMARD PRODUCT (also called the Schur or
 * entrywise product) -- the searchable name for numpy's '*' and MATLAB's
 * '.*', as distinct from matrix multiplication.  Division and add/subtract
 * are entrywise in the same sense.
 *
 * That reading is unambiguous here because linear is the formalism that
 * claims NO structure: a 3;3 linear run is nine numbers, not a matrix.  A
 * <ops kind="matrix"> run is a matrix, and when that formalism exists A * B
 * resolves in form_matrix.c under matrix-multiply rules instead.
 */

#define _POSIX_C_SOURCE 200112L

#include <string.h>
#include <stdio.h>
#include <stdlib.h>

#include "util.h"
#include "value.h"
#include "units.h"
#include "operator.h"
#include "buffer.h"
#include "form.h"
#include "form_linear.h"

#define REFUSE(...) (das_error(DASERR_FORM, __VA_ARGS__), dbsRefuse)

/* ************************************************************************* */
/* The formalism.  No state at all beyond the base.                          */

typedef struct das_form_linear {
	DasForm base;
} DasFormLinear;

static DasForm* _linear_new(void)
{
	DasFormLinear* pThis = (DasFormLinear*)calloc(1, sizeof(DasFormLinear));
	pThis->base.pVTbl = &das_form_linear_vtbl;
	pThis->base.nRef  = 1;
	return &(pThis->base);
}

DasForm* new_DasFormLinear(void){ return _linear_new(); }

static DasErrCode _linear_setParam(
	DasForm* pBase, const char* sName, const char* sVal
){
	return das_error(DASERR_FORM,
		"<ops kind=\"linear\"> takes no parameters, got '%s'", sName
	);
}

/* Deliberately silent.  Absence IS the wire form of linear, so emitting
   anything here would make every plain scalar in every stream carry a
   redundant element. */
static DasErrCode _linear_encode(
	const DasForm* pBase, DasBuf* pBuf
){
	return DAS_OKAY;
}

/* Symmetric with _linear_setParam's refusal: linear takes no parameters, so
   there are none to hand back and every name is a miss. */
static const char* _linear_getParam(
	const DasForm* pBase, const char* sName, ubyte* pType
)
{
	return NULL;
}

static bool _linear_pack(
	const DasForm* pBase, const das_operand* pOp, const ubyte* pRun,
	das_datum* pOut
){
	/* A plain number needs no assembly: the bits ARE the value.  A linear
	   COMPOSITE (a bare numeric run with no richer meaning) has no single
	   datum form, and saying so is the honest answer. */
	if(pOp->nIntRank > 0) return false;

	das_datum_init(pOut, pRun, pOp->vtElem, 0, pOp->units);
	return true;
}

/* vtUnknown means "no datum type of my own, use the element type". */
static das_val_type _linear_datumType(const DasForm* pBase){ return vtUnknown; }

static char* _linear_prnIntr(
	const DasForm* pBase, char* sBuf, int nLen
){
	if(nLen > 0) sBuf[0] = '\0';   /* nothing to say about plain numbers */
	return sBuf;
}

static DasForm* _linear_copy(const DasForm* pBase){ return _linear_new(); }

static void _linear_release(DasForm* pBase){ free(pBase); }

/* ************************************************************************* */
/* The recipe                                                                */

typedef struct das_binop_linear {
	DasBinOp base;

	das_val_type vtL, vtR;
	int    nOp;
	size_t nElems;      /* item run length; 1 for a scalar */

	/* Brings the RIGHT operand into the left's units, 1.0 when none is
	   needed.  This lives here and nowhere else: it is linear's private
	   business, not a field every formalism pays for. */
	double rRightScale;
} DasBinOpLinear;

static bool _linear_apply(
	const DasBinOp* pOp, const ubyte* pLRun, const ubyte* pRRun, ubyte* pOutRun
){
	const DasBinOpLinear* pThis = (const DasBinOpLinear*)pOp;

	size_t uL   = das_vt_size(pThis->vtL);
	size_t uR   = das_vt_size(pThis->vtR);
	size_t uOut = das_vt_size(pOp->vtOut);

	/* Element i with element i, for as many as the shape check agreed on.  A
	   scalar is just nElems == 1, so there is one loop and no special case. */
	for(size_t u = 0; u < pThis->nElems; ++u){

		const ubyte* pRight = pRRun + u*uR;
		das_val_type vtRight = pThis->vtR;

		/* The FACTOR is loop invariant -- units are one per variable, there being
		   no holder for per-component units -- but each VALUE needs its own
		   promotion and its own temporary, hence the local.  Scaling promotes
		   to double, which is why a rule that sets a scale must declare an
		   output wide enough to hold one. */
		double rScaled;
		if(pThis->rRightScale != 1.0){
			if(das_value_binXform(
				pThis->vtR, pRight, NULL, vtDouble, (ubyte*)&rScaled, NULL, 0
			) != DAS_OKAY)
				return false;
			rScaled *= pThis->rRightScale;
			pRight  = (const ubyte*)&rScaled;
			vtRight = vtDouble;
		}

		if(das_value_binop(
			pThis->nOp, pThis->vtL, pLRun + u*uL, vtRight, pRight,
			pOp->vtOut, pOutRun + u*uOut
		) != DAS_OKAY)
			return false;
	}
	return true;
}

static void _linear_binop_release(DasBinOp* pOp)
{
	DasForm_decRef(pOp->pForm);
	free(pOp);
}

static const DasBinOp_VTbl g_vtblLinear = { _linear_apply, _linear_binop_release };

/* ************************************************************************* */
/* Dispatch                                                                  */

static das_binop_stat _linear_binOpLeft(
	const DasForm* pBase, const das_operand* pL, int nOp,
	const das_operand* pR, DasBinOp** ppOut
){
	/* Linear knows nothing about any other formalism -- it is the BOTTOM of
	   the knowledge graph, so it cannot include another form's header without
	   inverting the arrow.  A non-linear partner is therefore declined, not
	   refused, and the better informed side claims it through binOpRight. */
	if(pR->pForm->pVTbl != &das_form_linear_vtbl) return dbsDecline;

	/* Same shape, elementwise, and the SHAPE is compared rather than the
	   element count: a 3;3 and a nine component run hold the same number of
	   values and are not the same thing.

	   Positional pairing is not an assumption here, it is the whole stated
	   meaning of the operands.  Linear declares there is no frame, no
	   component system and no ordering promise -- just numbers -- so element i
	   with element i is all the correspondence that exists to honor.  Contrast
	   geovec, where real structure exists and ignoring it would be unfaithful.

	   NO BROADCAST HERE.  das2C already broadcasts, one layer up: a generator
	   maps an external index to VARIDX_UNUSED and hands back the same value
	   however that index varies.  A form never sees an external index at all,
	   only one item run at one location, so an internal scalar-against-run
	   rule would be a SECOND mechanism wearing the same word.  Exact match
	   here, degenerate indices up there.

	   MULTIPLY IS THE HADAMARD PRODUCT, elementwise like numpy's '*' rather
	   than MATLAB's.  No ambiguity with matrix multiply can arise, because
	   linear is the formalism that claims no structure: a 3;3 linear run is
	   nine numbers, not a matrix.  When <ops kind="matrix"> exists, A * B on
	   two matrices resolves in form_matrix.c under matrix-multiply rules. */
	if(pL->nIntRank != pR->nIntRank)
		return REFUSE("Elementwise math needs matching structure, have rank "
		              "%d and rank %d item runs", pL->nIntRank, pR->nIntRank);

	for(int i = 0; i < pL->nIntRank; ++i){
		if(pL->aIntShape[i] < 1)
			return REFUSE("A ragged item run has no fixed width to pair up");
		if(pL->aIntShape[i] != pR->aIntShape[i])
			return REFUSE("Elementwise math needs matching extents, differ at "
			              "internal index %d: %zd vs %zd",
			              i, pL->aIntShape[i], pR->aIntShape[i]);
	}

	double rRightScale = 1.0;
	das_units unitsOut = NULL;

	switch(nOp){
	case D2BOP_ADD:
	case D2BOP_SUB:
		if(pL->units != pR->units){
			if(!Units_canConvert(pR->units, pL->units))
				return REFUSE("Can not %s %s and %s, the units do not convert",
					(nOp == D2BOP_ADD) ? "add" : "subtract",
					Units_toStr(pL->units), Units_toStr(pR->units));
			rRightScale = Units_convertTo(pL->units, 1.0, pR->units);
		}
		unitsOut = pL->units;
		break;

	case D2BOP_MUL:
		unitsOut = Units_multiply(pL->units, pR->units);
		break;

	case D2BOP_DIV:
		unitsOut = Units_divide(pL->units, pR->units);
		break;

	default:
		/* pow and the rest are not wrong, just unbuilt.  Declining would send
		   this to the right operand, which is also linear and would also
		   decline, producing a vaguer message than the truth. */
		return REFUSE("Operator '%s' is not implemented for plain numbers",
		              das_op_toStr(nOp, NULL));
	}

	DasBinOpLinear* pRes = (DasBinOpLinear*)calloc(1, sizeof(DasBinOpLinear));
	pRes->base.pVTbl = &g_vtblLinear;
	pRes->base.nRef  = 1;

	pRes->vtL         = pL->vtElem;
	pRes->vtR         = pR->vtElem;
	pRes->nOp         = nOp;
	pRes->rRightScale = rRightScale;

	pRes->base.units = unitsOut;
	pRes->base.pForm = _linear_new();

	/* Elementwise math preserves shape; both operands agree by the check
	   above, so either one names the result. */
	pRes->base.nIntRank = pL->nIntRank;
	memcpy(pRes->base.aIntShape, pL->aIntShape,
	       sizeof(ptrdiff_t) * (size_t)pL->nIntRank);
	pRes->nElems = das_operand_elems(pL);

	/* A scaled operand arrives as a double, so the result has to be wide
	   enough to hold one; that is the rule's promise, made here. */
	pRes->base.vtOut = das_vt_merge(
		(rRightScale != 1.0) ? vtDouble : pR->vtElem, nOp, pL->vtElem
	);
	if(pRes->base.vtOut == vtUnknown){
		DasForm_decRef(pRes->base.pForm);
		free(pRes);
		return REFUSE("No result type for %s %s %s",
			das_vt_toStr(pL->vtElem), das_op_toStr(nOp, NULL),
			das_vt_toStr(pR->vtElem));
	}

	*ppOut = &(pRes->base);
	return dbsOkay;
}

/* No binOpRight.  Linear is the bottom of the knowledge graph: there is no
   formalism it knows about but does not already handle from the left.  A
   partner that wants to combine with a plain number implements the pairing in
   ITS file, which is the direction the arrow runs. */

const DasForm_VTbl das_form_linear_vtbl = {
	"linear",
	_linear_new,
	_linear_setParam,
	NULL,              /* validate -- nothing is required of this kind */
	_linear_getParam,
	_linear_encode,
	_linear_pack,
	_linear_datumType,
	_linear_prnIntr,
	_linear_binOpLeft,
	NULL,              /* binOpRight */
	_linear_copy,
	_linear_release
};
