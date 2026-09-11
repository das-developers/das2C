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

/* The point formalism: absolute positions on a scale with an agreed origin.
 * See form_point.h for the rule set and the word "affine".
 *
 * Point knows about LINEAR, because an interval is a plain number and a point
 * minus a point produces one.  Linear does not know about point.  That is the
 * knowledge arrow, and it is why the interval + point ordering is claimed here
 * by binOpRight rather than over in form_linear.c.
 */

#define _POSIX_C_SOURCE 200112L

#include <string.h>
#include <stdio.h>
#include <stdlib.h>

#include "util.h"
#include "value.h"
#include "units.h"
#include "time.h"
#include "operator.h"
#include "buffer.h"
#include "form.h"
#include "form_linear.h"
#include "form_point.h"

#define REFUSE(...) (das_error(DASERR_FORM, __VA_ARGS__), dbsRefuse)

/* ************************************************************************* */
/* The formalism.  No state; the affine rule needs no parameters.            */

typedef struct das_form_point {
	DasForm base;
} DasFormPoint;

static DasForm* _point_new(void)
{
	DasFormPoint* pThis = (DasFormPoint*)calloc(1, sizeof(DasFormPoint));
	pThis->base.pVTbl = &das_form_point_vtbl;
	return &(pThis->base);
}

DasForm* new_DasFormPoint(void){ return _point_new(); }

static DasErrCode _point_setParam(
	DasForm* pBase, const char* sName, const char* sVal
){
	return das_error(DASERR_FORM,
		"<ops kind=\"point\"> takes no parameters, got '%s'", sName
	);
}

static DasErrCode _point_encode(
	const DasForm* pBase, DasBuf* pBuf
){
	return DasBuf_puts(pBuf, "      <ops kind=\"point\"/>\n");
}

/* A point's origin rides in units (TT2000, US2000), not in a parameter, so
   like linear it has nothing to hand back. */
static const char* _point_getParam(
	const DasForm* pBase, const char* sName, ubyte* pType
)
{
	return NULL;
}

static bool _point_pack(
	const DasForm* pBase, const das_operand* pOp, const ubyte* pRun,
	das_datum* pOut
){
	/* A run of positions boxes as a plain composite, same as a linear run:
	   the affine rule changes what may be DONE with the elements, not how
	   they read. The scalar case is just the element itself. */
	if(pOp->nIntRank > 0)
		return das_datum_box(
			pOut, pBase, pRun, pOp->nIntRank, pOp->aIntShape, pOp->vtElem,
			pOp->units
		);

	das_datum_init(pOut, pRun, pOp->vtElem, 0, pOp->units);
	return true;
}

static das_val_type _point_datumType(const DasForm* pBase){ return vtUnknown; }

static char* _point_prnIntr(
	const DasForm* pBase, char* sBuf, int nLen
){
	snprintf(sBuf, (size_t)nLen, " point");
	return sBuf;
}

static DasForm* _point_copy(const DasForm* pBase){ return _point_new(); }

static void _point_release(DasForm* pBase){ free(pBase); }


/* ************************************************************************* */
/* The recipe                                                                */

typedef struct das_binop_affine {
	DasBinOp base;

	das_val_type vtL, vtR;
	int    nOp;             /* D2BOP_ADD or D2BOP_SUB, as WRITTEN */

	/* Which side carries the point.  Needed because neither hook ever
	   reorders operands: apply() receives them in the order the caller wrote
	   them, and "interval + point" has to add the left to the right while
	   "point + interval" adds the right to the left. */
	bool bPointLeft;

	/* Both operands are positions, so this is the difference case and there is
	   no interval to scale.  Recorded at resolve rather than re-derived here:
	   the walker must never have to work out what math it is running. */
	bool bTwoPoints;

	size_t nElems;          /* item run length; 1 for a scalar position */

	/* Brings the INTERVAL operand into the epoch's own interval units, 1.0
	   when it already is.  A point operand is never scaled: converting an
	   absolute position means moving its origin, which is a different
	   operation than rescaling a span. */
	double rIntervalScale;
} DasBinOpAffine;

/* Read one value as a double, through the library's range checked cast. */
static bool _toDbl(das_val_type vt, const ubyte* p, double* pOut)
{
	return das_value_binXform(
		vt, p, NULL, vtDouble, (ubyte*)pOut, NULL, 0
	) == DAS_OKAY;
}

static bool _affine_apply(
	const DasBinOp* pOp, const ubyte* pLRun, const ubyte* pRRun, ubyte* pOutRun
){
	const DasBinOpAffine* pThis = (const DasBinOpAffine*)pOp;

	das_val_type vtPoint = pThis->bPointLeft ? pThis->vtL : pThis->vtR;
	das_val_type vtOther = pThis->bPointLeft ? pThis->vtR : pThis->vtL;

	size_t uL   = das_vt_size(pThis->vtL);
	size_t uR   = das_vt_size(pThis->vtR);
	size_t uOut = das_vt_size(pOp->vtOut);

	/* Element i with element i.  A scalar position is just nElems == 1, so
	   there is one loop and no special case for the common shape. */
	for(size_t u = 0; u < pThis->nElems; ++u){

		const ubyte* pL   = pLRun   + u*uL;
		const ubyte* pR   = pRRun   + u*uR;
		ubyte*       pOut = pOutRun + u*uOut;

		const ubyte* pPoint = pThis->bPointLeft ? pL : pR;
		const ubyte* pOther = pThis->bPointLeft ? pR : pL;

		/* --- point - point, the difference of two positions ------------ */
		if(pThis->bTwoPoints){
			if(pThis->vtL == vtTime){
				/* dt_diff is the only thing that knows how to walk a calendar
				   to get a span, and it answers in seconds. */
				*((double*)pOut) = dt_diff(
					(const das_time*)pL, (const das_time*)pR
				);
			}
			else if(das_value_binop(
				D2BOP_SUB, pThis->vtL, pL, pThis->vtR, pR, pOp->vtOut, pOut
			) != DAS_OKAY)
				return false;
			continue;
		}

		/* --- a broken-down time moved by a span ------------------------ */
		if(vtPoint == vtTime){
			double rSpan;
			if(!_toDbl(vtOther, pOther, &rSpan)) return false;
			rSpan *= pThis->rIntervalScale;

			/* point - interval moves backward.  interval - point never gets
			   here; resolve refused it. */
			if(pThis->nOp == D2BOP_SUB) rSpan = -rSpan;

			das_time dt = *((const das_time*)pPoint);
			dt.second += rSpan;
			dt_tnorm(&dt);
			memcpy(pOut, &dt, sizeof(das_time));
			continue;
		}

		/* --- ordinary arithmetic on the ticks -------------------------- */
		if(pThis->rIntervalScale == 1.0){
			if(das_value_binop(
				pThis->nOp, pThis->vtL, pL, pThis->vtR, pR, pOp->vtOut, pOut
			) != DAS_OKAY)
				return false;
			continue;
		}

		double rScaled;
		if(!_toDbl(vtOther, pOther, &rScaled)) return false;
		rScaled *= pThis->rIntervalScale;

		/* Operand ORDER is preserved: the scaled interval goes back on the
		   side it came in on, because no hook ever reorders operands. */
		DasErrCode nRet = pThis->bPointLeft
			? das_value_binop(pThis->nOp, vtPoint, pPoint,
			                  vtDouble, (const ubyte*)&rScaled, pOp->vtOut, pOut)
			: das_value_binop(pThis->nOp, vtDouble, (const ubyte*)&rScaled,
			                  vtPoint, pPoint, pOp->vtOut, pOut);
		if(nRet != DAS_OKAY) return false;
	}
	return true;
}

static void _affine_release(DasBinOp* pOp)
{
	del_DasForm(pOp->pForm);
	free(pOp);
}

static const DasBinOp_VTbl g_vtblAffine = { _affine_apply, _affine_release };

static DasBinOpAffine* _affine_new(
	const das_operand* pL, const das_operand* pR, int nOp, bool bPointLeft,
	bool bTwoPoints, double rScale, das_units unitsOut, DasForm* pFormOut,
	das_val_type vtOut
){
	DasBinOpAffine* pRes = (DasBinOpAffine*)calloc(1, sizeof(DasBinOpAffine));
	pRes->base.pVTbl = &g_vtblAffine;
	pRes->base.nRef  = 1;

	pRes->vtL = pL->vtElem;
	pRes->vtR = pR->vtElem;
	pRes->nOp = nOp;
	pRes->bPointLeft     = bPointLeft;
	pRes->bTwoPoints     = bTwoPoints;
	pRes->rIntervalScale = rScale;

	pRes->base.units    = unitsOut;
	pRes->base.pForm    = pFormOut;
	pRes->base.vtOut = vtOut;

	/* Affine math preserves shape: a displacement has as many components as
	   the positions it runs between.  Both operands agree by _point_sameShape,
	   so either names the result. */
	pRes->base.nIntRank = pL->nIntRank;
	memcpy(pRes->base.aIntShape, pL->aIntShape,
	       sizeof(ptrdiff_t) * (size_t)pL->nIntRank);
	pRes->nElems = das_operand_elems(pL);
	return pRes;
}

/* A span between two points is a double when a calendar struct is involved
   (dt_diff answers in seconds) and otherwise promotes like ordinary math. */
static das_val_type _span_type(das_val_type vtL, das_val_type vtR)
{
	if((vtL == vtTime)||(vtR == vtTime)) return vtDouble;
	return das_vt_merge(vtR, D2BOP_SUB, vtL);
}

/* ************************************************************************* */
/* Dispatch                                                                  */

/* An affine space is a set of POINTS plus a vector space of TRANSLATIONS, and
   nothing in that definition mentions dimension.  A 3-run of positions is an
   affine point in three dimensions and its difference is a 3-run displacement,
   which in this library's vocabulary is a linear run: a free vector carrying
   no frame and claiming no dot or cross product.  Attaching a frame= is what
   would promote it to a geovec.

   So the only structural requirement is that the two operands have the SAME
   shape, compared rank-and-extent rather than by element count for the same
   reason linear does it: a 3;3 and a nine-run are not the same thing. */
static das_binop_stat _point_sameShape(
	const das_operand* pL, const das_operand* pR
){
	if(pL->nIntRank != pR->nIntRank)
		return REFUSE("Affine math needs matching structure, have rank %d and "
		              "rank %d item runs", pL->nIntRank, pR->nIntRank);

	for(int i = 0; i < pL->nIntRank; ++i){
		if(pL->aIntShape[i] < 1)
			return REFUSE("A ragged item run has no fixed width to pair up");
		if(pL->aIntShape[i] != pR->aIntShape[i])
			return REFUSE("Affine math needs matching extents, differ at "
			              "internal index %d: %zd vs %zd",
			              i, pL->aIntShape[i], pR->aIntShape[i]);
	}
	return dbsOkay;
}

static das_binop_stat _point_binOpLeft(
	const DasForm* pBase, const das_operand* pL, int nOp,
	const das_operand* pR, DasBinOp** ppOut
){
	bool bRightIsPoint = DasForm_isKind(pR->pForm, DAS_FORM_POINT);

	/* Not our partner at all: only linear and point pair with a point. */
	if(!bRightIsPoint && !DasForm_isKind(pR->pForm, DAS_FORM_LINEAR))
		return dbsDecline;

	if(_point_sameShape(pL, pR) != dbsOkay) return dbsRefuse;

	/* --- point OP point ------------------------------------------------- */
	if(bRightIsPoint){
		if(nOp != D2BOP_SUB)
			return REFUSE("Two absolute positions can only be subtracted; "
			              "'%s' of two of them has no meaning",
			              das_op_toStr(nOp, NULL));

		/* Strict identity on the epoch.  Two positions measured from
		   DIFFERENT origins have no difference until one is re-referenced,
		   and silently converting would hide which origin the answer is on. */
		if(pL->units != pR->units)
			return REFUSE("Subtracting positions needs one origin on both "
			              "sides, have %s and %s (convert first)",
			              Units_toStr(pL->units), Units_toStr(pR->units));

		*ppOut = &(_affine_new(
			pL, pR, D2BOP_SUB, true, true, 1.0,
			Units_interval(pL->units),        /* the difference is a span */
			new_DasFormLinear(),              /* and a span is plain math */
			_span_type(pL->vtElem, pR->vtElem)
		)->base);
		return dbsOkay;
	}

	/* --- point OP interval ---------------------------------------------- */
	if((nOp != D2BOP_ADD)&&(nOp != D2BOP_SUB))
		return REFUSE("An absolute position can only be moved by an interval, "
		              "'%s' is not defined on one", das_op_toStr(nOp, NULL));

	das_units uNeed = Units_interval(pL->units);
	double rScale = 1.0;
	if(pR->units != uNeed){
		if(!Units_canConvert(pR->units, uNeed))
			return REFUSE("Moving a position on epoch %s needs %s (or "
			              "convertible) on the right, have %s",
			              Units_toStr(pL->units), Units_toStr(uNeed),
			              Units_toStr(pR->units));
		rScale = Units_convertTo(uNeed, 1.0, pR->units);
	}

	*ppOut = &(_affine_new(
		pL, pR, nOp, true, false, rScale,
		pL->units,                  /* still a position on the same origin */
		_point_new(),
		pL->vtElem                  /* and it keeps the point's storage */
	)->base);
	return dbsOkay;
}

/* interval + point.  Claimed HERE, not in form_linear.c, because linear sits
   at the bottom of the knowledge graph and must not learn what a point is.
   Stated explicitly rather than inferred from commutativity: the library
   never reorders operands, so a legal ordering has to be written down. */
static das_binop_stat _point_binOpRight(
	const DasForm* pBase, const das_operand* pL, int nOp,
	const das_operand* pR, DasBinOp** ppOut
){
	if(!DasForm_isKind(pL->pForm, DAS_FORM_LINEAR)) return dbsDecline;

	if(_point_sameShape(pL, pR) != dbsOkay) return dbsRefuse;

	if(nOp == D2BOP_SUB)
		return REFUSE("An interval minus a position is meaningless; write "
		              "position - interval to move it backward");

	if(nOp != D2BOP_ADD)
		return REFUSE("An interval and a position can only be added, not "
		              "'%s'", das_op_toStr(nOp, NULL));

	das_units uNeed = Units_interval(pR->units);
	double rScale = 1.0;
	if(pL->units != uNeed){
		if(!Units_canConvert(pL->units, uNeed))
			return REFUSE("Moving a position on epoch %s needs %s (or "
			              "convertible) on the left, have %s",
			              Units_toStr(pR->units), Units_toStr(uNeed),
			              Units_toStr(pL->units));
		rScale = Units_convertTo(uNeed, 1.0, pL->units);
	}

	*ppOut = &(_affine_new(
		pL, pR, D2BOP_ADD, false, false, rScale,
		pR->units, _point_new(), pR->vtElem
	)->base);
	return dbsOkay;
}

const DasForm_VTbl das_form_point_vtbl = {
	"point",
	_point_new,
	_point_setParam,
	NULL,              /* validate -- nothing is required of this kind */
	_point_getParam,
	_point_encode,
	_point_pack,
	_point_datumType,
	_point_prnIntr,
	NULL,              /* prnRun -- plain numbers, no rendering of its own */
	NULL,              /* compSym -- one value on a scale, no components */
	_point_binOpLeft,
	_point_binOpRight,
	_point_copy,
	_point_release
};
