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

/* DasBinSet: an operation over two sets.  Supersedes DasVarBinary.
 *
 * FORMS COMPUTE, SETS WALK.  This file is the walker and it is the only one.
 * It asks the operands' formalisms for a recipe ONCE, reads six facts off it,
 * and drives every index itself.  It never asks again what math made a value,
 * and the forms on the other side of that call have no generator, no array,
 * no index and no loop.
 *
 * The one set class with NO wire element: das3 has no binary element, so a
 * stream carries reference and offset as two variables and the dimension
 * combines them.  A DasBinSet is therefore never DECODED, only built in code,
 * and serializing one means MATERIALIZING it.
 *
 * It keeps the operand SETS, which DasVarBinary lost when it flattened itself
 * into a generator: a generator has no units and no formalism, so once the
 * operands were reduced to generators nobody could ask what had been
 * combined.
 */

#define _POSIX_C_SOURCE 200112L

#include <string.h>
#include <stdio.h>
#include <stdlib.h>

#include "util.h"
#include "value.h"
#include "units.h"
#include "operator.h"
#include "array.h"
#include "form.h"
#include "set.h"

/* ************************************************************************* */
/* Snapshotting a set for the form layer                                     */

bool DasSet_operand(const DasSet* pThis, das_operand* pOut)
{
	if(pThis->pForm == NULL){
		das_error(DASERR_SET,
			"A %s has no formalism and can not take part in arithmetic",
			DasSet_element(pThis)
		);
		return false;
	}

	memset(pOut, 0, sizeof(das_operand));

	pOut->pForm = pThis->pForm;
	pOut->units = pThis->units;
	pOut->vtElem    = DasSet_elemType(pThis);      /* read live, never cached */

	pOut->nIntRank = DasSet_intrShape(pThis, pOut->aIntShape);
	return (pOut->nIntRank >= 0);
}

/* ************************************************************************* */
/* Construction: resolve the pairing once                                    */

static const DasSet_VTbl g_vtblBinSet;   /* defined at the bottom */

/* Ask the left operand, then the right.  Python's __add__ / __radd__, but the
   reason is layering rather than convenience: knowledge between formalisms is
   a one way arrow, so the better informed partner implements the hook and
   which hook it is only records which side it happened to stand on.  NEITHER
   hook reorders the operands, so an undefined pairing fails loud instead of
   silently commuting. */
static das_binop_stat _binset_resolve(
	const das_operand* pL, int nOp, const das_operand* pR,
	const DasCtxTbl* pTbl, DasBinOp** ppOut
){
	const DasForm_VTbl* pLVTbl = pL->pForm->pVTbl;
	const DasForm_VTbl* pRVTbl = pR->pForm->pVTbl;

	das_binop_stat stat = dbsDecline;

	if(pLVTbl->binOpLeft != NULL)
		stat = pLVTbl->binOpLeft(pL->pForm, pL, nOp, pR, pTbl, ppOut);

	/* A REFUSAL is final.  The left operand claimed the pairing and found it
	   illegal, and it has already said why in terms of the actual problem;
	   asking the right operand would only replace that with a vaguer one. */
	if(stat != dbsDecline) return stat;

	if(pRVTbl->binOpRight != NULL)
		stat = pRVTbl->binOpRight(pR->pForm, pL, nOp, pR, pTbl, ppOut);

	return stat;
}

DasBinSet* new_DasBinSet(DasSet* pLeft, char cOp, DasSet* pRight)
{
	if((pLeft == NULL)||(pRight == NULL)){
		das_error(DASERR_SET, "Null operand for a binary operation");
		return NULL;
	}

	int nOp = das_op_binary((const char[2]){cOp, '\0'});
	if((nOp == D2OP_INVALID)||!das_op_isBinary(nOp)){
		das_error(DASERR_SET, "Unknown binary operator '%c'", cOp);
		return NULL;
	}

	das_operand opL, opR;
	if(!DasSet_operand(pLeft, &opL))  return NULL;
	if(!DasSet_operand(pRight, &opR)) return NULL;

	/* The external shapes have to agree before any math is worth resolving.
	   This is STRUCTURAL, so it is the walker's job; the frames and units are
	   semantic and belong to the form. */
	ptrdiff_t aLShape[SETIDX_MAX], aRShape[SETIDX_MAX];
	int nLRank = DasSet_shape(pLeft,  aLShape);
	int nRRank = DasSet_shape(pRight, aRShape);
	if(nLRank != nRRank){
		das_error(DASERR_SET,
			"Can not combine a rank %d set with a rank %d one", nLRank, nRRank
		);
		return NULL;
	}

	/* Naming a frame in a refusal needs the table; the MATH never does, since
	   bindings compare by handle.  A detached set gets a vaguer message, not
	   a wrong answer. */
	const DasCtxTbl* pTbl = _DasSet_ctxTbl(pLeft);
	if(pTbl == NULL) pTbl = _DasSet_ctxTbl(pRight);

	DasBinOp* pRecipe = NULL;
	das_binop_stat stat = _binset_resolve(&opL, nOp, &opR, pTbl, &pRecipe);

	if(stat == dbsDecline){
		/* Nobody claimed it.  A refusal already spoke for itself. */
		das_error(DASERR_SET,
			"No rule for %s %c %s: neither operand defines the pairing",
			DasForm_kindStr(pLeft->pForm), cOp, DasForm_kindStr(pRight->pForm)
		);
		return NULL;
	}
	if(stat != dbsOkay) return NULL;

	DasBinSet* pThis = (DasBinSet*)calloc(1, sizeof(DasBinSet));
	if(pThis == NULL){
		DasBinOp_decRef(pRecipe);   /* every exit past resolve must release */
		return NULL;
	}

	DasDesc_init(&(pThis->base.base), VARIABLE);
	pThis->base.pVTbl    = &g_vtblBinSet;
	pThis->base.nRef  = 1;
	pThis->base.pGen  = NULL;   /* a DasBinSet has no leaf value source; its
	                               values ARE the two operands plus a rule */

	/* The recipe decided these once.  From here they are ordinary fields and
	   nothing downstream asks what math produced them. */
	pThis->base.units = pRecipe->units;
	pThis->base.pForm = pRecipe->pForm;
	DasForm_incRef(pThis->base.pForm);

	pThis->pRecipe = pRecipe;   /* resolve left us the reference */
	pThis->op      = nOp;
	pThis->pLeft   = pLeft;
	pThis->pRight  = pRight;
	DasSet_incRef(pLeft);
	DasSet_incRef(pRight);

	return pThis;
}

/* ************************************************************************* */
/* The walk                                                                  */

/* One item at one location: fetch both operand runs, hand them to the recipe.
   This is the ENTIRE contact surface with the formalism layer. */
static bool _binset_item(
	const DasBinSet* pThis, ptrdiff_t* pLoc, ubyte* pOutRun
){
	const DasBinOp* pOp = pThis->pRecipe;

	ubyte aLRun[DASBIN_RUN_MAX], aRRun[DASBIN_RUN_MAX];

	if(!DasSet_itemAt(pThis->pLeft,  pLoc, aLRun, sizeof(aLRun))) return false;
	if(!DasSet_itemAt(pThis->pRight, pLoc, aRRun, sizeof(aRRun))) return false;

	return DasBinOp_apply(pOp, aLRun, aRRun, pOutRun);
}

static bool _DasBinSet_get(
	const DasSet* pBase, ptrdiff_t* pLoc, das_datum* pOut
){
	const DasBinSet* pThis = (const DasBinSet*)pBase;
	const DasBinOp* pOp = pThis->pRecipe;

	ubyte aRun[DASBIN_RUN_MAX];
	if(!_binset_item(pThis, pLoc, aRun)) return false;

	/* Packing is the form's job: it is the only thing that knows whether nine
	   doubles are a rotation or a matrix.  A form with no pack slot has no
	   single datum representation, which is an answer and not a failure. */
	if(pOp->pForm->pVTbl->pack != NULL){
		das_operand op;
		op.pForm    = pOp->pForm;
		op.vtElem   = pOp->vtOut;
		op.units    = pOp->units;
		op.nIntRank = pOp->nIntRank;
		memcpy(op.aIntShape, pOp->aIntShape, sizeof(op.aIntShape));

		return pOp->pForm->pVTbl->pack(pOp->pForm, &op, aRun, pOut);
	}

	if(pOp->nIntRank > 0){
		das_error(DASERR_SET,
			"A %s value has no single datum form; read it with DasSet_subset",
			DasForm_kindStr(pOp->pForm)
		);
		return false;
	}

	das_datum_init(pOut, aRun, pOp->vtOut, das_vt_size(pOp->vtOut), pOp->units);
	return true;
}

/* Structure comes from the operands; the recipe supplies only the ITEM shape,
   since that is the half the math can change (3;3 times 3 gives 3). */
static int _DasBinSet_shape(const DasSet* pBase, ptrdiff_t* pShape)
{
	const DasBinSet* pThis = (const DasBinSet*)pBase;

	ptrdiff_t aRight[SETIDX_MAX];
	int nRank = DasSet_shape(pThis->pLeft, pShape);
	DasSet_shape(pThis->pRight, aRight);

	/* MIN merge, the live-read semantic DasDs_lengthIn already relies on:
	   mid-stream the two operands fill different sized blocks and the extent
	   usable across both is the smaller.  A mismatch is a normal state. */
	das_varindex_merge(nRank, pShape, aRight);
	return nRank;
}

static int _DasBinSet_intrShape(const DasSet* pBase, ptrdiff_t* pShape)
{
	const DasBinSet* pThis = (const DasBinSet*)pBase;

	memcpy(pShape, pThis->pRecipe->aIntShape,
	       sizeof(ptrdiff_t) * (size_t)pThis->pRecipe->nIntRank);
	return pThis->pRecipe->nIntRank;
}

static ptrdiff_t _DasBinSet_lengthIn(
	const DasSet* pBase, int nIdx, ptrdiff_t* pLoc
){
	const DasBinSet* pThis = (const DasBinSet*)pBase;

	return das_varlength_merge(
		DasSet_lengthIn(pThis->pLeft,  nIdx, pLoc),
		DasSet_lengthIn(pThis->pRight, nIdx, pLoc)
	);
}

static das_val_type _DasBinSet_elemType(const DasSet* pBase)
{
	return ((const DasBinSet*)pBase)->pRecipe->vtOut;
}

static bool _DasBinSet_isNumeric(const DasSet* pBase){ return true; }

/* A DasBinSet has no wire element.  Serializing one means materializing it
   first, so this answers for what it will BECOME, not what it is. */
static const char* _DasBinSet_element(const DasSet* pBase)
{
	return (((const DasBinSet*)pBase)->pRecipe->nIntRank > 0) ? "composite"
	                                                          : "scalar";
}

static char* _DasBinSet_expression(
	const DasSet* pBase, char* sBuf, int nLen, unsigned int uFlags
){
	const DasBinSet* pThis = (const DasBinSet*)pBase;

	char sLeft[128] = {'\0'}, sRight[128] = {'\0'};
	DasSet_toStr(pThis->pLeft,  sLeft,  sizeof(sLeft));
	DasSet_toStr(pThis->pRight, sRight, sizeof(sRight));

	snprintf(sBuf, (size_t)nLen, "(%s %s %s)",
		sLeft, das_op_toStr(pThis->op, NULL), sRight);
	return sBuf;
}

/* ************************************************************************* */
/* Materialize: what serializing a DasBinSet actually means                  */

DasAry* DasSet_materialize(const DasSet* pThis)
{
	ptrdiff_t aShape[SETIDX_MAX];
	int nRank = DasSet_shape(pThis, aShape);
	if(nRank < 1){
		das_error(DASERR_SET, "Can't materialize a rank 0 set");
		return NULL;
	}

	ptrdiff_t aMin[SETIDX_MAX], aMax[SETIDX_MAX];
	for(int i = 0; i < nRank; ++i){
		aMin[i] = 0;
		if(aShape[i] < 0){
			das_error(DASERR_NOTIMP,
				"Can't materialize index %d of a set with no settled extent", i
			);
			return NULL;
		}
		aMax[i] = aShape[i];
	}

	/* Copy, never a view: the point is to own numbers that outlive the recipe */
	return DasSet_subsetCopy(pThis, nRank, aMin, aMax);
}

/* ************************************************************************* */
/* Lifecycle                                                                 */

static DasSet* _DasBinSet_copy(const DasSet* pBase)
{
	const DasBinSet* pThis = (const DasBinSet*)pBase;

	DasBinSet* pCopy = (DasBinSet*)calloc(1, sizeof(DasBinSet));
	memcpy(pCopy, pThis, sizeof(DasBinSet));
	pCopy->base.nRef = 1;

	/* SHARE the recipe, never re-resolve it.  Resolution is a construction
	   time decision; running it again could reach a different answer if a
	   context entry moved underneath, and two copies of one set must not
	   disagree about what math they are. */
	DasBinOp_incRef(pCopy->pRecipe);
	DasForm_incRef(pCopy->base.pForm);
	DasSet_incRef(pCopy->pLeft);
	DasSet_incRef(pCopy->pRight);

	DasDesc_copyIn(&(pCopy->base.base), &(pThis->base.base));
	return &(pCopy->base);
}

static int _DasBinSet_decRef(DasSet* pBase)
{
	DasBinSet* pThis = (DasBinSet*)pBase;

	if(--(pThis->base.nRef) > 0) return pThis->base.nRef;

	DasSet_decRef(pThis->pLeft);
	DasSet_decRef(pThis->pRight);
	DasForm_decRef(pThis->base.pForm);
	DasBinOp_decRef(pThis->pRecipe);

	DasDesc_freeProps(&(pThis->base.base));
	free(pThis);
	return 0;
}

/* ************************************************************************* */
/* Bulk read                                                                 */

/* The generic _DasSet_subset() in set.c hands the whole range to
   DasGen_subsetInto().  A DasBinSet has NO generator, so that path cannot
   serve it and the walk has to live here.  That is why DasSet_VTbl carries a
   subsetInto slot: array backed classes forward it to the generator, this one
   implements it. */
static int _DasBinSet_subsetInto(
	const DasSet* pBase, int nRank, const ptrdiff_t* pMin,
	const ptrdiff_t* pMax, ubyte* pBuf, size_t uBufLen
){
	const DasBinSet* pThis = (const DasBinSet*)pBase;
	const DasBinOp*  pOp   = pThis->pRecipe;

	size_t uItemElems = 1;
	for(int i = 0; i < pOp->nIntRank; ++i){
		if(pOp->aIntShape[i] < 1){
			das_error(DASERR_NOTIMP,
				"Ragged item runs have no fixed width to subset"
			);
			return -1;
		}
		uItemElems *= (size_t)pOp->aIntShape[i];
	}
	size_t uItemSz = uItemElems * das_vt_size(pOp->vtOut);

	/* Row major, last external index fastest, matching DasGen_subsetInto. */
	ptrdiff_t aLoc[SETIDX_MAX];
	memcpy(aLoc, pMin, sizeof(ptrdiff_t) * (size_t)nRank);

	int    nItems = 0;
	size_t uUsed  = 0;

	while(true){
		if(uUsed + uItemSz > uBufLen){
			das_error(DASERR_SET, "Subset buffer too small at item %d", nItems);
			return -1;
		}
		if(!_binset_item(pThis, aLoc, pBuf + uUsed)) return -1;
		uUsed += uItemSz;
		++nItems;

		/* odometer */
		int d = nRank - 1;
		while(d >= 0){
			if(++aLoc[d] < pMax[d]) break;
			aLoc[d] = pMin[d];
			--d;
		}
		if(d < 0) break;
	}

	return nItems;
}

static int _DasBinSet_incRef(DasSet* pBase){ return ++(pBase->nRef); }

static const DasSet_VTbl g_vtblBinSet = {
	_DasBinSet_get,
	_DasBinSet_element,
	_DasBinSet_elemType,
	_DasBinSet_shape,
	_DasBinSet_intrShape,
	_DasBinSet_lengthIn,
	_DasBinSet_subsetInto,
	_DasBinSet_expression,
	_DasBinSet_isNumeric,
	_DasBinSet_incRef,
	_DasBinSet_decRef,
	_DasBinSet_copy
};
