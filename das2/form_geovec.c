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

/* The geometric vector formalism.  See form_geovec.h for the parameter set.
 *
 * The reference resolution in _geovec_setParam arrived from dataset_hdr3.c's
 * _serial_onOps, where it was sixty lines of one formalism's knowledge sitting
 * in the generic parser.  Moving it is the point of the whole DasForm layer:
 * the parser now sees name/value pairs and a vtable, and never learns what a
 * frame is.
 *
 * Geovec knows about LINEAR, because a vector scaled by a plain number is a
 * vector.  Linear does not know about geovec.
 */

#define _POSIX_C_SOURCE 200112L

#include <string.h>
#include <stdio.h>
#include <stdlib.h>

#include "util.h"
#include "log.h"
#include "value.h"
#include "units.h"
#include "operator.h"
#include "context.h"
#include "descriptor.h"
#include "geovec.h"
#include "form.h"
#include "form_linear.h"
#include "form_geovec.h"

#define REFUSE(...) (das_error(DASERR_FORM, __VA_ARGS__), dbsRefuse)

/* ************************************************************************* */
/* The formalism                                                             */

/* Typed fields, not a string store.  A recognized parameter resolves at parse
   and the strings are dead the moment they are understood; only DasFormGeneric
   needs to keep text around. */
typedef struct das_form_geovec {
	DasForm base;

	ubyte uFrameId;    /* CTX_FRAME handle, 0 when frameless */
	ubyte uSurfId;     /* CTX_SURFACE handle, 0 for none */
	ubyte uSysType;    /* DAS_VSYS_* */
	ubyte uDirs;       /* VEC_DIRS3 packed, slot -> canonical direction */

	/* body= is folded into the frame's context entry, so it is NOT here.  The
	   token is kept only long enough to warn about a conflict. */
} DasFormGeoVec;

static DasForm* _geovec_new(void)
{
	DasFormGeoVec* pThis = (DasFormGeoVec*)calloc(1, sizeof(DasFormGeoVec));
	pThis->base.pVTbl = &das_form_geovec_vtbl;
	pThis->base.nRef  = 1;
	pThis->uSysType   = DAS_VSYS_CART;          /* the overwhelming default */
	pThis->uDirs      = VEC_DIRS3(0, 1, 2);     /* ascending */
	return &(pThis->base);
}

DasForm* new_DasFormGeoVec(
	ubyte uFrameId, ubyte uSurfId, ubyte uSysType, ubyte uDirs
){
	DasFormGeoVec* pThis = (DasFormGeoVec*)_geovec_new();
	pThis->uFrameId = uFrameId;
	pThis->uSurfId  = uSurfId;
	pThis->uSysType = uSysType;
	pThis->uDirs    = uDirs;
	return &(pThis->base);
}

/* --- accessors ----------------------------------------------------------- */

#define GV_GET(FN, FIELD) \
	ubyte FN(const DasForm* pThis){ \
		if(!DasForm_isGeoVec(pThis)){ \
			das_error(DASERR_FORM, "Not a geovec formalism"); \
			return 0; \
		} \
		return ((const DasFormGeoVec*)pThis)->FIELD; \
	}

GV_GET(DasFormGeoVec_frameId, uFrameId)
GV_GET(DasFormGeoVec_surfId,  uSurfId)
GV_GET(DasFormGeoVec_sysType, uSysType)
GV_GET(DasFormGeoVec_dirs,    uDirs)

const char* DasFormGeoVec_slotSym(const DasForm* pThis, int iSlot)
{
	if(!DasForm_isGeoVec(pThis)||(iSlot < 0)||(iSlot > 2)) return NULL;

	const DasFormGeoVec* pGv = (const DasFormGeoVec*)pThis;
	return das_compsys_symbol(pGv->uSysType, (pGv->uDirs >> (2*iSlot)) & 0x3);
}

/* --- parameters ---------------------------------------------------------- */

/* "1;0;2": storage slot i holds canonical direction sOrd[2i] */
static DasErrCode _geovec_setOrder(DasFormGeoVec* pThis, const char* sOrd)
{
	ubyte aDir[3] = {0, 1, 2};
	int iSlot = 0;
	for(const char* p = sOrd; (*p != '\0')&&(iSlot < 3); ++p){
		if((*p >= '0')&&(*p <= '2')){ aDir[iSlot] = (ubyte)(*p - '0'); ++iSlot; }
		else if(*p != ';')
			return das_error(DASERR_FORM, "Bad sysorder token '%s'", sOrd);
	}
	pThis->uDirs = VEC_DIRS3(aDir[0], aDir[1], aDir[2]);
	return DAS_OKAY;
}

/* body= describes the FRAME, so it lands on the frame's context entry and is
   never kept here.  Two variables in one frame then cannot disagree about the
   body, and a writer has exactly one place to emit it from. */
static DasErrCode _geovec_setBody(
	DasFormGeoVec* pThis, DasCtxTbl* pTbl, const char* sBody
){
	if(pThis->uFrameId == 0){
		daslog_warn_v(
			"Ignoring body=\"%s\" on a frameless <ops kind=\"geovec\">: a body "
			"describes a frame and there is no frame to put it on", sBody
		);
		return DAS_OKAY;
	}

	DasCtx* pFrame = DasCtxTbl_get(pTbl, pThis->uFrameId);
	const char* sHave = DasCtx_body(pFrame);

	if((sHave[0] != '\0')&&(strcmp(sHave, sBody) != 0)){
		daslog_warn_v(
			"Frame '%s' is declared with body '%s' but a variable's <ops> says "
			"'%s'; keeping the frame's", DasCtx_name(pFrame), sHave, sBody
		);
		return DAS_OKAY;
	}
	return DasCtx_setBody(pFrame, sBody);
}

static DasErrCode _geovec_setParam(
	DasForm* pBase, DasCtxTbl* pTbl, const char* sName, const char* sVal
){
	DasFormGeoVec* pThis = (DasFormGeoVec*)pBase;

	if(strcmp(sName, "system") == 0){
		pThis->uSysType = das_compsys_id(sVal);
		if(pThis->uSysType == 0)
			return das_error(DASERR_FORM, "Unknown component system '%s'", sVal);
		return DAS_OKAY;
	}

	if(strcmp(sName, "sysorder") == 0)
		return _geovec_setOrder(pThis, sVal);

	/* The reference-valued parameters need the table, so a program building a
	   geovec before it has one has to use new_DasFormGeoVec() with handles. */
	if((strcmp(sName, "frame") == 0)||(strcmp(sName, "surface") == 0)||
	   (strcmp(sName, "body") == 0)){
		if(pTbl == NULL)
			return das_error(DASERR_FORM,
				"'%s' names a stream context entry and needs a context table "
				"to resolve against", sName
			);
	}

	if(strcmp(sName, "frame") == 0){
		DasCtx* pFrame = DasCtxTbl_getByName(pTbl, CTX_FRAME, sVal);
		if(pFrame != NULL){
			pThis->uFrameId = DasCtx_id(pFrame);
			return DAS_OKAY;
		}
		/* Auto-frame: a vector may name a frame the stream never declared, and
		   that is legal.  The entry is created so the handle resolves, and
		   titled so a reader can tell it was inferred. */
		pThis->uFrameId = DasCtxTbl_intern(pTbl, CTX_FRAME, sVal, NULL);
		if(pThis->uFrameId == 0) return DASERR_FORM;

		return DasDesc_setStr(
			(DasDesc*)DasCtxTbl_get(pTbl, pThis->uFrameId), "title",
			"Autogenerated Frame"
		);
	}

	if(strcmp(sName, "surface") == 0){
		pThis->uSurfId = DasCtxTbl_intern(pTbl, CTX_SURFACE, sVal, NULL);
		if(pThis->uSurfId == 0)
			return das_error(DASERR_FORM,
				"Couldn't intern surface '%s' for the stream", sVal);
		return DAS_OKAY;
	}

	if(strcmp(sName, "body") == 0)
		return _geovec_setBody(pThis, pTbl, sVal);

	/* A reader that CLAIMS a kind must not skip a misspelling in it.  This is
	   where "sysOrder" instead of "sysorder" gets caught instead of silently
	   producing a wrongly ordered vector. */
	return das_error(DASERR_FORM,
		"<ops kind=\"geovec\"> has no parameter '%s'", sName
	);
}

/* TALKATIVE: system= and sysorder= are stated even at their defaults, because
   the schema cannot carry a default behind an anyAttribute. */
static DasErrCode _geovec_encode(
	const DasForm* pBase, const DasCtxTbl* pTbl, DasBuf* pBuf
){
	const DasFormGeoVec* pThis = (const DasFormGeoVec*)pBase;

	DasErrCode nRet = DasBuf_puts(pBuf, "<ops kind=\"geovec\"");
	if(nRet != DAS_OKAY) return nRet;

	if(pThis->uFrameId != 0){
		const DasCtx* pFrame = (pTbl == NULL) ? NULL
		                     : DasCtxTbl_getOfKind(pTbl, CTX_FRAME, pThis->uFrameId);
		if(pFrame == NULL)
			return das_error(DASERR_FORM,
				"Frame handle %hhu does not resolve; a geovec can not be "
				"written without its frame's name", pThis->uFrameId
			);
		if((nRet = DasBuf_printf(pBuf, " frame=\"%s\"", DasCtx_name(pFrame))) != DAS_OKAY)
			return nRet;
	}

	if(pThis->uSurfId != 0){
		const DasCtx* pSurf = (pTbl == NULL) ? NULL
		                    : DasCtxTbl_getOfKind(pTbl, CTX_SURFACE, pThis->uSurfId);
		if(pSurf == NULL)
			return das_error(DASERR_FORM,
				"Surface handle %hhu does not resolve", pThis->uSurfId);
		if((nRet = DasBuf_printf(pBuf, " surface=\"%s\"", DasCtx_name(pSurf))) != DAS_OKAY)
			return nRet;
	}

	nRet = DasBuf_printf(pBuf, " system=\"%s\" sysorder=\"%d;%d;%d\"/>\n",
		das_compsys_str(pThis->uSysType),
		 pThis->uDirs       & 0x3,
		(pThis->uDirs >> 2) & 0x3,
		(pThis->uDirs >> 4) & 0x3
	);
	return nRet;
}

static int _geovec_getRefs(const DasForm* pBase, ubyte* pIds, int nMax)
{
	const DasFormGeoVec* pThis = (const DasFormGeoVec*)pBase;

	int n = 0;
	if((pThis->uFrameId != 0)&&(n < nMax)) pIds[n++] = pThis->uFrameId;
	if((pThis->uSurfId  != 0)&&(n < nMax)) pIds[n++] = pThis->uSurfId;
	return n;
}

/* --- presentation -------------------------------------------------------- */

static bool _geovec_pack(
	const DasForm* pBase, const das_operand* pOp, const ubyte* pRun,
	das_datum* pOut
){
	const DasFormGeoVec* pThis = (const DasFormGeoVec*)pBase;

	if((pOp->nIntRank != 1)||(pOp->aIntShape[0] < 1)||(pOp->aIntShape[0] > 3))
		return das_error(DASERR_FORM,
			"A geovec has 1 to 3 components in one level");

	ubyte nComp = (ubyte)pOp->aIntShape[0];

	das_geovec vec;
	if(das_geovec_init(
		&vec, pRun, pThis->uFrameId, pThis->uSysType, pThis->uSurfId,
		(ubyte)pOp->vtElem, (ubyte)das_vt_size(pOp->vtElem), nComp, pThis->uDirs
	) != DAS_OKAY)
		return false;

	memcpy(pOut, &vec, sizeof(das_geovec));
	pOut->vt    = vtGeoVec;
	pOut->vsize = sizeof(das_geovec);
	pOut->units = pOp->units;
	return true;
}

/* vtGeoVec, not vtComposite.  The packed das_geovec predates the generic box
   and datum.c, value.c and das3_spice all read its layout directly, so it
   stays the fast case until those move.  A geovec datum is then the one rich
   type that is NOT boxed; everything added after this uses vtComposite. */
static das_val_type _geovec_datumType(const DasForm* pBase){ return vtGeoVec; }

static char* _geovec_prnIntr(
	const DasForm* pBase, const DasCtxTbl* pTbl, char* sBuf, int nLen
){
	const DasFormGeoVec* pThis = (const DasFormGeoVec*)pBase;

	const DasCtx* pFrame = ((pTbl == NULL)||(pThis->uFrameId == 0)) ? NULL
	                     : DasCtxTbl_getOfKind(pTbl, CTX_FRAME, pThis->uFrameId);

	snprintf(sBuf, (size_t)nLen, " geovec(%s,%s)",
		(pFrame != NULL) ? DasCtx_name(pFrame) : "no frame",
		das_compsys_str(pThis->uSysType)
	);
	return sBuf;
}

static DasForm* _geovec_copy(const DasForm* pBase)
{
	DasFormGeoVec* pCopy = (DasFormGeoVec*)calloc(1, sizeof(DasFormGeoVec));
	memcpy(pCopy, pBase, sizeof(DasFormGeoVec));
	pCopy->base.nRef = 1;
	return &(pCopy->base);
}

static void _geovec_release(DasForm* pBase){ free(pBase); }


/* ************************************************************************* */
/* The recipe                                                                */

typedef struct das_binop_geovec {
	DasBinOp base;

	das_val_type vtL, vtR;
	int    nOp;
	int    nComp;

	bool   bScale;        /* one side is a plain number, not a vector */
	bool   bVecLeft;      /* which side carries the vector, when scaling */
	double rRightScale;   /* unit conversion for the ADD case */

	/* Output slot -> input slot, per operand.  Identity when the two vectors
	   already agree on component order, which is the common case. */
	ubyte aMapL[3];
	ubyte aMapR[3];
} DasBinOpGeoVec;

static bool _geovec_apply(
	const DasBinOp* pOp, const ubyte* pLRun, const ubyte* pRRun, ubyte* pOutRun
){
	const DasBinOpGeoVec* pThis = (const DasBinOpGeoVec*)pOp;

	size_t uL   = das_vt_size(pThis->vtL);
	size_t uR   = das_vt_size(pThis->vtR);
	size_t uOut = das_vt_size(pOp->vtOut);

	/* --- vector times a plain number -------------------------------------- */
	if(pThis->bScale){
		const ubyte* pVec = pThis->bVecLeft ? pLRun : pRRun;
		const ubyte* pNum = pThis->bVecLeft ? pRRun : pLRun;
		das_val_type vtVec = pThis->bVecLeft ? pThis->vtL : pThis->vtR;
		das_val_type vtNum = pThis->bVecLeft ? pThis->vtR : pThis->vtL;
		size_t uVec = pThis->bVecLeft ? uL : uR;

		/* The scalar is ONE value applied to every component; that is not the
		   internal broadcast we refused in linear, it is what scaling means. */
		for(int i = 0; i < pThis->nComp; ++i){
			DasErrCode nRet = pThis->bVecLeft
				? das_value_binop(pThis->nOp, vtVec, pVec + i*uVec,
				                  vtNum, pNum, pOp->vtOut, pOutRun + i*uOut)
				: das_value_binop(pThis->nOp, vtNum, pNum,
				                  vtVec, pVec + i*uVec, pOp->vtOut, pOutRun + i*uOut);
			if(nRet != DAS_OKAY) return false;
		}
		return true;
	}

	/* --- vector plus or minus a vector ------------------------------------ */
	for(int i = 0; i < pThis->nComp; ++i){

		const ubyte* pL = pLRun + pThis->aMapL[i]*uL;
		const ubyte* pR = pRRun + pThis->aMapR[i]*uR;

		if(pThis->rRightScale == 1.0){
			if(das_value_binop(
				pThis->nOp, pThis->vtL, pL, pThis->vtR, pR,
				pOp->vtOut, pOutRun + i*uOut
			) != DAS_OKAY)
				return false;
			continue;
		}

		double rScaled;
		if(das_value_binXform(
			pThis->vtR, pR, NULL, vtDouble, (ubyte*)&rScaled, NULL, 0
		) != DAS_OKAY)
			return false;
		rScaled *= pThis->rRightScale;

		if(das_value_binop(
			pThis->nOp, pThis->vtL, pL, vtDouble, (const ubyte*)&rScaled,
			pOp->vtOut, pOutRun + i*uOut
		) != DAS_OKAY)
			return false;
	}
	return true;
}

static void _geovec_binop_release(DasBinOp* pOp)
{
	DasForm_decRef(pOp->pForm);
	free(pOp);
}

static const DasBinOp_VTbl g_vtblGeoVec = { _geovec_apply, _geovec_binop_release };


/* ************************************************************************* */
/* Dispatch                                                                  */

/* Adding two spherical vectors componentwise is WRONG -- angles do not add --
   and there is no silent conversion to cartesian here.  This refusal is the
   reason a vector's system travels with it rather than being assumed. */
static das_binop_stat _geovec_needCart(const DasFormGeoVec* pThis)
{
	if(pThis->uSysType != DAS_VSYS_CART)
		return REFUSE("Vector arithmetic needs cartesian components, these are "
		              "%s; convert the system first",
		              das_compsys_str(pThis->uSysType));
	return dbsOkay;
}

static int _geovec_comps(const das_operand* pOp)
{
	if((pOp->nIntRank != 1)||(pOp->aIntShape[0] < 1)||(pOp->aIntShape[0] > 3)){
		das_error(DASERR_FORM, "A geovec has 1 to 3 components in one level");
		return 0;
	}
	return (int)pOp->aIntShape[0];
}

/* Vector scaled by a plain number.  Claimed by geovec in both orderings,
   because linear may not learn what a vector is. */
static das_binop_stat _geovec_scale(
	const das_operand* pVec, const das_operand* pNum, int nOp, bool bVecLeft,
	DasBinOp** ppOut
){
	const DasFormGeoVec* pGv = (const DasFormGeoVec*)pVec->pForm;

	if(_geovec_needCart(pGv) != dbsOkay) return dbsRefuse;

	if(pNum->nIntRank != 0)
		return REFUSE("Scaling a vector needs a single number, not a run");

	if((nOp != D2BOP_MUL)&&(nOp != D2BOP_DIV))
		return REFUSE("A vector and a plain number combine under '*' or '/', "
		              "not '%s'", das_op_toStr(nOp, NULL));

	if((nOp == D2BOP_DIV)&&!bVecLeft)
		return REFUSE("Dividing a number by a vector is not defined");

	int nComp = _geovec_comps(pVec);
	if(nComp == 0) return dbsRefuse;

	DasBinOpGeoVec* pRes = (DasBinOpGeoVec*)calloc(1, sizeof(DasBinOpGeoVec));
	pRes->base.pVTbl = &g_vtblGeoVec;
	pRes->base.nRef  = 1;

	pRes->vtL = bVecLeft ? pVec->vtElem : pNum->vtElem;
	pRes->vtR = bVecLeft ? pNum->vtElem : pVec->vtElem;
	pRes->nOp      = nOp;
	pRes->nComp    = nComp;
	pRes->bScale   = true;
	pRes->bVecLeft = bVecLeft;
	pRes->rRightScale = 1.0;

	pRes->base.units = (nOp == D2BOP_MUL)
		? Units_multiply(pVec->units, pNum->units)
		: Units_divide(pVec->units, pNum->units);

	/* Scaling changes magnitude, never frame or order */
	pRes->base.pForm = new_DasFormGeoVec(
		pGv->uFrameId, pGv->uSurfId, pGv->uSysType, pGv->uDirs
	);
	pRes->base.vtOut        = das_vt_merge(pNum->vtElem, nOp, pVec->vtElem);
	pRes->base.nIntRank     = 1;
	pRes->base.aIntShape[0] = nComp;

	*ppOut = &(pRes->base);
	return dbsOkay;
}

static das_binop_stat _geovec_binOpLeft(
	const DasForm* pBase, const das_operand* pL, int nOp,
	const das_operand* pR, const DasCtxTbl* pTbl, DasBinOp** ppOut
){
	const DasFormGeoVec* pThis = (const DasFormGeoVec*)pBase;

	if(DasForm_isLinear(pR->pForm))
		return _geovec_scale(pL, pR, nOp, true, ppOut);

	if(!DasForm_isGeoVec(pR->pForm)) return dbsDecline;

	const DasFormGeoVec* pRgv = (const DasFormGeoVec*)pR->pForm;

	/* '*' between two vectors is genuinely ambiguous -- dot, cross, or
	   elementwise are three different answers -- so it is refused rather than
	   guessed.  Named products get their own operators when they arrive. */
	if((nOp != D2BOP_ADD)&&(nOp != D2BOP_SUB))
		return REFUSE("'%s' between two vectors is ambiguous (dot? cross?); "
		              "only addition and subtraction are defined",
		              das_op_toStr(nOp, NULL));

	if((_geovec_needCart(pThis) != dbsOkay)||
	   (_geovec_needCart(pRgv)  != dbsOkay)) return dbsRefuse;

	/* THIS is where a frame mismatch fails loud.  Two vectors in different
	   frames have no sum until one is rotated, and adding the components
	   anyway would produce a confident wrong answer. */
	if(pThis->uFrameId != pRgv->uFrameId){
		const DasCtx* pA = (pTbl == NULL) ? NULL
		                 : DasCtxTbl_getOfKind(pTbl, CTX_FRAME, pThis->uFrameId);
		const DasCtx* pB = (pTbl == NULL) ? NULL
		                 : DasCtxTbl_getOfKind(pTbl, CTX_FRAME, pRgv->uFrameId);
		return REFUSE("Can not combine vectors in different frames, '%s' and "
		              "'%s'; rotate one first",
		              (pA != NULL) ? DasCtx_name(pA) : "(unbound)",
		              (pB != NULL) ? DasCtx_name(pB) : "(unbound)");
	}

	if(pThis->uSurfId != pRgv->uSurfId)
		return REFUSE("Can not combine vectors on different surfaces");

	int nComp = _geovec_comps(pL);
	if((nComp == 0)||(nComp != _geovec_comps(pR)))
		return REFUSE("Vector addition needs the same component count on both "
		              "sides");

	double rRightScale = 1.0;
	if(pL->units != pR->units){
		if(!Units_canConvert(pR->units, pL->units))
			return REFUSE("Can not combine vectors in %s and %s",
			              Units_toStr(pL->units), Units_toStr(pR->units));
		rRightScale = Units_convertTo(pL->units, 1.0, pR->units);
	}

	DasBinOpGeoVec* pRes = (DasBinOpGeoVec*)calloc(1, sizeof(DasBinOpGeoVec));
	pRes->base.pVTbl = &g_vtblGeoVec;
	pRes->base.nRef  = 1;

	pRes->vtL   = pL->vtElem;
	pRes->vtR   = pR->vtElem;
	pRes->nOp   = nOp;
	pRes->nComp = nComp;
	pRes->rRightScale = rRightScale;

	/* Component ORDER need not agree.  When it already does, the maps are the
	   identity and the stored order is preserved; when it does not, both sides
	   are gathered to canonical and the result says so. */
	ubyte uOutDirs = pThis->uDirs;
	for(int i = 0; i < nComp; ++i){ pRes->aMapL[i] = i; pRes->aMapR[i] = i; }

	if(pThis->uDirs != pRgv->uDirs){
		for(int iDir = 0; iDir < nComp; ++iDir){
			for(int i = 0; i < nComp; ++i){
				if(((pThis->uDirs >> (2*i)) & 0x3) == iDir) pRes->aMapL[iDir] = i;
				if(((pRgv->uDirs  >> (2*i)) & 0x3) == iDir) pRes->aMapR[iDir] = i;
			}
		}
		uOutDirs = VEC_DIRS3(0, 1, 2);
	}

	pRes->base.units = pL->units;
	pRes->base.pForm = new_DasFormGeoVec(
		pThis->uFrameId, pThis->uSurfId, pThis->uSysType, uOutDirs
	);
	pRes->base.vtOut = das_vt_merge(
		(rRightScale != 1.0) ? vtDouble : pR->vtElem, nOp, pL->vtElem
	);
	pRes->base.nIntRank     = 1;
	pRes->base.aIntShape[0] = nComp;

	*ppOut = &(pRes->base);
	return dbsOkay;
}

/* number * vector.  Claimed HERE rather than in form_linear.c: linear sits at
   the bottom of the knowledge graph and must not learn what a vector is.  The
   operands are NOT reordered -- apply() sees them as written -- so this hook
   and the left one are two separately stated rules, not one commuted. */
static das_binop_stat _geovec_binOpRight(
	const DasForm* pBase, const das_operand* pL, int nOp,
	const das_operand* pR, const DasCtxTbl* pTbl, DasBinOp** ppOut
){
	if(!DasForm_isLinear(pL->pForm)) return dbsDecline;

	return _geovec_scale(pR, pL, nOp, false, ppOut);
}

const DasForm_VTbl das_form_geovec_vtbl = {
	"geovec",
	_geovec_new,
	_geovec_setParam,
	_geovec_encode,
	_geovec_getRefs,
	_geovec_pack,
	_geovec_datumType,
	_geovec_prnIntr,
	_geovec_binOpLeft,
	_geovec_binOpRight,
	_geovec_copy,
	_geovec_release
};
