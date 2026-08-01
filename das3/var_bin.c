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

/* DasVarBin: an operation over two variables.
 *
 * FORMS COMPUTE, VARIABLES WALK.  This file is the walker, the only one.
 * It asks the operands' formalisms for a recipe ONCE, reads six facts off it,
 * and drives every index itself.  It never asks again what math made a value,
 * and the forms on the other side of that call have no generator, no array,
 * no index and no loop.
 *
 * The one variable class with NO wire element: das3 has no binary element, so a
 * stream carries reference and offset as two variables and the dimension
 * combines them.  A DasVarBin is therefore never DECODED, only built in code,
 * and serializing one means MATERIALIZING it.
 *
 * It keeps the operand VARIABLES, not just their generators
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
#include "variable.h"

/* ************************************************************************* */
/* Snapshotting a variable for the form layer                                */

bool DasVar_operand(const DasVar* pThis, das_operand* pOut)
{
	if(pThis->pForm == NULL){
		das_error(DASERR_VAR,
			"A %s has no formalism and can not take part in arithmetic",
			DasVar_element(pThis)
		);
		return false;
	}

	memset(pOut, 0, sizeof(das_operand));

	pOut->pForm = pThis->pForm;
	pOut->units = pThis->units;
	pOut->vtElem    = DasVar_elemType(pThis);      /* read live, never cached */

	pOut->nIntRank = DasVar_intrShape(pThis, pOut->aIntShape);
	return (pOut->nIntRank >= 0);
}

/* ************************************************************************* */
/* Construction: resolve the pairing once                                    */

static const DasVar_VTbl g_vtblVarBin;   /* defined at the bottom */

/* Ask the left operand, then the right.  Python's __add__ / __radd__, but the
   reason is layering rather than convenience: knowledge between formalisms is
   a one way arrow, so the better informed partner implements the hook and
   which hook it is only records which side it happened to stand on.  NEITHER
   hook reorders the operands, so an undefined pairing fails loud instead of
   silently commuting. */
static das_binop_stat _binset_resolve(
	const das_operand* pL, int nOp, const das_operand* pR, DasBinOp** ppOut
){
	const DasForm_VTbl* pLVTbl = pL->pForm->pVTbl;
	const DasForm_VTbl* pRVTbl = pR->pForm->pVTbl;

	das_binop_stat stat = dbsDecline;

	if(pLVTbl->binOpLeft != NULL)
		stat = pLVTbl->binOpLeft(pL->pForm, pL, nOp, pR, ppOut);

	/* A REFUSAL is final.  The left operand claimed the pairing and found it
	   illegal, and it has already said why in terms of the actual problem;
	   asking the right operand would only replace that with a vaguer one. */
	if(stat != dbsDecline) return stat;

	if(pRVTbl->binOpRight != NULL)
		stat = pRVTbl->binOpRight(pR->pForm, pL, nOp, pR, ppOut);

	return stat;
}

DasVarBin* new_DasVarBin(DasVar* pLeft, char cOp, DasVar* pRight)
{
	if((pLeft == NULL)||(pRight == NULL)){
		das_error(DASERR_VAR, "Null operand for a binary operation");
		return NULL;
	}

	int nOp = das_op_binary((const char[2]){cOp, '\0'});
	if((nOp == D2OP_INVALID)||!das_op_isBinary(nOp)){
		das_error(DASERR_VAR, "Unknown binary operator '%c'", cOp);
		return NULL;
	}

	das_operand opL, opR;
	if(!DasVar_operand(pLeft, &opL))  return NULL;
	if(!DasVar_operand(pRight, &opR)) return NULL;

	/* The external shapes have to agree before any math is worth resolving.
	   This is STRUCTURAL, so it is the walker's job; the frames and units are
	   semantic and belong to the form. */
	ptrdiff_t aLShape[VARIDX_MAX], aRShape[VARIDX_MAX];
	int nLRank = DasVar_shape(pLeft,  aLShape);
	int nRRank = DasVar_shape(pRight, aRShape);
	if(nLRank != nRRank){
		das_error(DASERR_VAR,
			"Can not combine a rank %d variable with a rank %d one", nLRank, nRRank
		);
		return NULL;
	}

	/* A refusal names frames straight off the operands' forms.  There is no
	   table to reach for and no detached-variable case to degrade for: a frame IS
	   its name now. */
	DasBinOp* pRecipe = NULL;
	das_binop_stat stat = _binset_resolve(&opL, nOp, &opR, &pRecipe);

	if(stat == dbsDecline){
		/* Nobody claimed it.  A refusal already spoke for itself. */
		das_error(DASERR_VAR,
			"No rule for %s %c %s: neither operand defines the pairing",
			DasForm_kindStr(pLeft->pForm), cOp, DasForm_kindStr(pRight->pForm)
		);
		return NULL;
	}
	if(stat != dbsOkay) return NULL;

	DasVarBin* pThis = (DasVarBin*)calloc(1, sizeof(DasVarBin));
	if(pThis == NULL){
		DasBinOp_decRef(pRecipe);   /* every exit past resolve must release */
		return NULL;
	}

	DasDesc_init(&(pThis->base.base), VARIABLE);
	pThis->base.pVTbl    = &g_vtblVarBin;
	pThis->base.nRef  = 1;
	pThis->base.pGen  = NULL;   /* a DasVarBin has no leaf value source; its
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
	DasVar_incRef(pLeft);
	DasVar_incRef(pRight);

	return pThis;
}

/* ************************************************************************* */
/* The walk                                                                  */

/* One item at one location: fetch both operand runs, hand them to the recipe.
   This is the ENTIRE contact surface with the formalism layer. */
static bool _binset_item(
	const DasVarBin* pThis, ptrdiff_t* pLoc, ubyte* pOutRun
){
	const DasBinOp* pOp = pThis->pRecipe;

	ubyte aLRun[DASBIN_RUN_MAX], aRRun[DASBIN_RUN_MAX];

	if(!DasVar_itemAt(pThis->pLeft,  pLoc, aLRun, sizeof(aLRun))) return false;
	if(!DasVar_itemAt(pThis->pRight, pLoc, aRRun, sizeof(aRRun))) return false;

	return DasBinOp_apply(pOp, aLRun, aRRun, pOutRun);
}

static bool _DasVarBin_get(
	const DasVar* pBase, ptrdiff_t* pLoc, das_datum* pOut
){
	const DasVarBin* pThis = (const DasVarBin*)pBase;
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
		das_error(DASERR_VAR,
			"A %s value has no single datum form; read it with DasVar_subset",
			DasForm_kindStr(pOp->pForm)
		);
		return false;
	}

	das_datum_init(pOut, aRun, pOp->vtOut, das_vt_size(pOp->vtOut), pOp->units);
	return true;
}

/* Structure comes from the operands; the recipe supplies only the ITEM shape,
   since that is the half the math can change (3;3 times 3 gives 3). */
static int _DasVarBin_shape(const DasVar* pBase, ptrdiff_t* pShape)
{
	const DasVarBin* pThis = (const DasVarBin*)pBase;

	ptrdiff_t aRight[VARIDX_MAX];
	int nRank = DasVar_shape(pThis->pLeft, pShape);
	DasVar_shape(pThis->pRight, aRight);

	/* MIN merge, the live-read semantic DasDs_lengthIn already relies on:
	   mid-stream the two operands fill different sized blocks and the extent
	   usable across both is the smaller.  A mismatch is a normal state. */
	das_varindex_merge(nRank, pShape, aRight);
	return nRank;
}

static int _DasVarBin_intrShape(const DasVar* pBase, ptrdiff_t* pShape)
{
	const DasVarBin* pThis = (const DasVarBin*)pBase;

	memcpy(pShape, pThis->pRecipe->aIntShape,
	       sizeof(ptrdiff_t) * (size_t)pThis->pRecipe->nIntRank);
	return pThis->pRecipe->nIntRank;
}

static ptrdiff_t _DasVarBin_lengthIn(
	const DasVar* pBase, int nIdx, ptrdiff_t* pLoc
){
	const DasVarBin* pThis = (const DasVarBin*)pBase;

	return das_varlength_merge(
		DasVar_lengthIn(pThis->pLeft,  nIdx, pLoc),
		DasVar_lengthIn(pThis->pRight, nIdx, pLoc)
	);
}

/* The recipe settles the result type in das_val_type terms, but the slot
   speaks das_elem_type.  The two enums agree on 0..11 by declared intent
   (see generator.h), and a composite result has no ELEMENT type of its own --
   its cells do -- so anything above the simple range is etUnknown here. */
static das_elem_type _DasVarBin_elemType(const DasVar* pBase)
{
	das_val_type vt = ((const DasVarBin*)pBase)->pRecipe->vtOut;
	if((vt < VT_MIN_SIMPLE)||(vt > VT_MAX_SIMPLE)) return etUnknown;
	return (das_elem_type)vt;
}

static bool _DasVarBin_isNumeric(const DasVar* pBase){ return true; }

/* A DasVarBin has no wire element.  Serializing one means materializing it
   first, so this answers for what it will BECOME, not what it is. */
static const char* _DasVarBin_element(const DasVar* pBase)
{
	return (((const DasVarBin*)pBase)->pRecipe->nIntRank > 0) ? "composite"
	                                                          : "scalar";
}

static char* _DasVarBin_expression(
	const DasVar* pBase, char* sBuf, int nLen, unsigned int uFlags
){
	const DasVarBin* pThis = (const DasVarBin*)pBase;

	char sLeft[128] = {'\0'}, sRight[128] = {'\0'};
	DasVar_toStr(pThis->pLeft,  sLeft,  sizeof(sLeft));
	DasVar_toStr(pThis->pRight, sRight, sizeof(sRight));

	snprintf(sBuf, (size_t)nLen, "(%s %s %s)",
		sLeft, das_op_toStr(pThis->op, NULL), sRight);
	return sBuf;
}

/* ************************************************************************* */
/* Materialize: what serializing a DasVarBin actually means                  */

DasAry* DasVar_materialize(const DasVar* pThis)
{
	ptrdiff_t aShape[VARIDX_MAX];
	int nRank = DasVar_shape(pThis, aShape);
	if(nRank < 1){
		das_error(DASERR_VAR, "Can't materialize a rank 0 variable");
		return NULL;
	}

	ptrdiff_t aMin[VARIDX_MAX], aMax[VARIDX_MAX];
	for(int i = 0; i < nRank; ++i){
		aMin[i] = 0;
		if(aShape[i] < 0){
			das_error(DASERR_NOTIMP,
				"Can't materialize index %d of a variable with no settled extent", i
			);
			return NULL;
		}
		aMax[i] = aShape[i];
	}

	/* Copy, never a view: the point is to own numbers that outlive the recipe */
	return DasVar_subsetCopy(pThis, nRank, aMin, aMax);
}

/* ************************************************************************* */
/* Lifecycle                                                                 */

static DasVar* _DasVarBin_copy(const DasVar* pBase)
{
	const DasVarBin* pThis = (const DasVarBin*)pBase;

	DasVarBin* pCopy = (DasVarBin*)calloc(1, sizeof(DasVarBin));
	memcpy(pCopy, pThis, sizeof(DasVarBin));
	pCopy->base.nRef = 1;

	/* SHARE the recipe, never re-resolve it.  Resolution is a construction
	   time decision; running it again could reach a different answer if a
	   context entry moved underneath, and two copies of one variable must not
	   disagree about what math they are. */
	DasBinOp_incRef(pCopy->pRecipe);
	DasForm_incRef(pCopy->base.pForm);
	DasVar_incRef(pCopy->pLeft);
	DasVar_incRef(pCopy->pRight);

	DasDesc_copyIn(&(pCopy->base.base), &(pThis->base.base));
	return &(pCopy->base);
}

static int _DasVarBin_decRef(DasVar* pBase)
{
	DasVarBin* pThis = (DasVarBin*)pBase;

	if(--(pThis->base.nRef) > 0) return pThis->base.nRef;

	DasVar_decRef(pThis->pLeft);
	DasVar_decRef(pThis->pRight);
	DasForm_decRef(pThis->base.pForm);
	DasBinOp_decRef(pThis->pRecipe);

	DasDesc_freeProps(&(pThis->base.base));
	free(pThis);
	return 0;
}

/* ************************************************************************* */
/* Bulk read                                                                 */

/* The generic _DasVar_subset() in variable.c hands the whole range to
   DasGen_subsetInto().  A DasVarBin has NO generator, so that path cannot
   serve it and the walk has to live here.  That is why DasVar_VTbl carries a
   subsetInto slot: array backed classes forward it to the generator, this one
   implements it. */
static int _DasVarBin_subsetInto(
	const DasVar* pBase, int nRank, const ptrdiff_t* pMin,
	const ptrdiff_t* pMax, ubyte* pBuf, size_t uBufLen
){
	const DasVarBin* pThis = (const DasVarBin*)pBase;
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
	ptrdiff_t aLoc[VARIDX_MAX];
	memcpy(aLoc, pMin, sizeof(ptrdiff_t) * (size_t)nRank);

	int    nItems = 0;
	size_t uUsed  = 0;

	while(true){
		if(uUsed + uItemSz > uBufLen){
			das_error(DASERR_VAR, "Subset buffer too small at item %d", nItems);
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

static int _DasVarBin_incRef(DasVar* pBase){ return ++(pBase->nRef); }

static const DasVar_VTbl g_vtblVarBin = {
	_DasVarBin_get,
	_DasVarBin_element,
	_DasVarBin_elemType,
	_DasVarBin_shape,
	_DasVarBin_intrShape,
	_DasVarBin_lengthIn,
	_DasVarBin_subsetInto,
	_DasVarBin_expression,
	_DasVarBin_isNumeric,
	_DasVarBin_incRef,
	_DasVarBin_decRef,
	_DasVarBin_copy
};
