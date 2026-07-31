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

/* The body-centered position formalism.  See form_geoloc.h for the rules.
 *
 * Geoloc knows about VECTOR, because the difference of two positions is a free
 * vector and this file has to name one as its result.  Vector does not know
 * about geoloc.  That is the same arrow, one level up, as form_point.c ->
 * form_linear.h, and for the same reason: 1-D affine and 3-D affine are one
 * structure at two scales.
 */

#define _POSIX_C_SOURCE 200112L

#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <math.h>

#include "util.h"
#include "log.h"
#include "value.h"
#include "units.h"
#include "operator.h"
#include "context.h"
#include "descriptor.h"
#include "form.h"
#include "form_vector.h"
#include "form_geoloc.h"

#ifdef SPICE
#include <SpiceUsr.h>
#endif

#define REFUSE(...) (das_error(DASERR_FORM, __VA_ARGS__), dbsRefuse)

/* ************************************************************************* */
/* The formalism                                                             */

typedef struct das_form_geoloc {
	DasForm base;

	ubyte uCenterId;   /* the ORIGIN.  Never 0 on a valid form */
	ubyte uFrameId;
	ubyte uSurfId;     /* the ellipsoid, for detic and graphic */
	ubyte uSysType;
	ubyte uDirs;
} DasFormGeoLoc;

static DasForm* _geoloc_new(void)
{
	DasFormGeoLoc* pThis = (DasFormGeoLoc*)calloc(1, sizeof(DasFormGeoLoc));
	pThis->base.pVTbl = &das_form_geoloc_vtbl;
	pThis->base.nRef  = 1;
	pThis->uSysType   = DAS_VSYS_CART;
	pThis->uDirs      = VEC_DIRS3(0, 1, 2);
	return &(pThis->base);
}

DasForm* new_DasFormGeoLoc(
	ubyte uCenterId, ubyte uFrameId, ubyte uSurfId, ubyte uSysType, ubyte uDirs
){
	if(uCenterId == 0){
		das_error(DASERR_FORM,
			"A geoloc needs a center; values with no origin are free vectors "
			"and belong in <ops kind=\"vector\">"
		);
		return NULL;
	}

	DasFormGeoLoc* pThis = (DasFormGeoLoc*)_geoloc_new();
	pThis->uCenterId = uCenterId;
	pThis->uFrameId  = uFrameId;
	pThis->uSurfId   = uSurfId;
	pThis->uSysType  = uSysType;
	pThis->uDirs     = uDirs;
	return &(pThis->base);
}

#define GL_GET(FN, FIELD) \
	ubyte FN(const DasForm* pThis){ \
		if(!DasForm_isGeoLoc(pThis)){ \
			das_error(DASERR_FORM, "Not a geoloc formalism"); \
			return 0; \
		} \
		return ((const DasFormGeoLoc*)pThis)->FIELD; \
	}

GL_GET(DasFormGeoLoc_centerId, uCenterId)
GL_GET(DasFormGeoLoc_frameId,  uFrameId)
GL_GET(DasFormGeoLoc_surfId,   uSurfId)
GL_GET(DasFormGeoLoc_sysType,  uSysType)
GL_GET(DasFormGeoLoc_dirs,     uDirs)

const char* DasFormGeoLoc_slotSym(const DasForm* pThis, int iSlot)
{
	if(!DasForm_isGeoLoc(pThis)||(iSlot < 0)||(iSlot > 2)) return NULL;

	const DasFormGeoLoc* pG = (const DasFormGeoLoc*)pThis;
	return das_compsys_symbol(pG->uSysType, (pG->uDirs >> (2*iSlot)) & 0x3);
}

/* --- parameters ---------------------------------------------------------- */

static DasErrCode _geoloc_setOrder(DasFormGeoLoc* pThis, const char* sOrd)
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

static DasErrCode _geoloc_setBody(
	DasFormGeoLoc* pThis, DasCtxTbl* pTbl, const char* sBody
){
	if(pThis->uFrameId == 0){
		daslog_warn_v(
			"Ignoring body=\"%s\" on a frameless <ops kind=\"geoloc\">", sBody);
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

static DasErrCode _geoloc_setParam(
	DasForm* pBase, DasCtxTbl* pTbl, const char* sName, const char* sVal
){
	DasFormGeoLoc* pThis = (DasFormGeoLoc*)pBase;

	if(strcmp(sName, "system") == 0){
		pThis->uSysType = das_compsys_id(sVal);
		if(pThis->uSysType == 0)
			return das_error(DASERR_FORM, "Unknown component system '%s'", sVal);
		return DAS_OKAY;
	}

	if(strcmp(sName, "sysorder") == 0)
		return _geoloc_setOrder(pThis, sVal);

	/* Everything below names a stream context entry */
	if(pTbl == NULL)
		return das_error(DASERR_FORM,
			"'%s' names a stream context entry and needs a context table to "
			"resolve against", sName
		);

	/* center= is what makes this a position rather than a vector.  A body gets
	   its own context kind rather than riding as a generic <given>: SPICE work
	   asks bodies real questions (radii for an ellipsoid, an ephemeris for a
	   position), so it is a computed-on kind, which is exactly the line
	   CTX_GIVEN is on the other side of.

	   NOT the same as the frame's body=.  That says what the frame is fixed
	   to; this says what the position is measured FROM.  Cassini relative to
	   Saturn expressed in IAU_JUPITER has Jupiter for the first and Saturn for
	   the second. */
	if(strcmp(sName, "center") == 0){
		pThis->uCenterId = DasCtxTbl_intern(pTbl, CTX_BODY, sVal, NULL);
		if(pThis->uCenterId == 0)
			return das_error(DASERR_FORM,
				"Couldn't intern center body '%s' for the stream", sVal);
		return DAS_OKAY;
	}

	if(strcmp(sName, "frame") == 0){
		DasCtx* pFrame = DasCtxTbl_getByName(pTbl, CTX_FRAME, sVal);
		if(pFrame != NULL){
			pThis->uFrameId = DasCtx_id(pFrame);
			return DAS_OKAY;
		}
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
		return _geoloc_setBody(pThis, pTbl, sVal);

	return das_error(DASERR_FORM,
		"<ops kind=\"geoloc\"> has no parameter '%s'", sName
	);
}

static DasErrCode _geoloc_encode(
	const DasForm* pBase, const DasCtxTbl* pTbl, DasBuf* pBuf
){
	const DasFormGeoLoc* pThis = (const DasFormGeoLoc*)pBase;

	if(pTbl == NULL)
		return das_error(DASERR_FORM,
			"A geoloc can not be written without its context names");

	const DasCtx* pCenter = DasCtxTbl_getOfKind(pTbl, CTX_BODY, pThis->uCenterId);
	if(pCenter == NULL)
		return das_error(DASERR_FORM,
			"Center handle %hhu does not resolve", pThis->uCenterId);

	DasErrCode nRet = DasBuf_printf(pBuf,
		"<ops kind=\"geoloc\" center=\"%s\"", DasCtx_name(pCenter));
	if(nRet != DAS_OKAY) return nRet;

	if(pThis->uFrameId != 0){
		const DasCtx* pFrame = DasCtxTbl_getOfKind(pTbl, CTX_FRAME, pThis->uFrameId);
		if(pFrame == NULL)
			return das_error(DASERR_FORM,
				"Frame handle %hhu does not resolve", pThis->uFrameId);
		if((nRet = DasBuf_printf(pBuf, " frame=\"%s\"", DasCtx_name(pFrame))) != DAS_OKAY)
			return nRet;
	}

	if(pThis->uSurfId != 0){
		const DasCtx* pSurf = DasCtxTbl_getOfKind(pTbl, CTX_SURFACE, pThis->uSurfId);
		if(pSurf == NULL)
			return das_error(DASERR_FORM,
				"Surface handle %hhu does not resolve", pThis->uSurfId);
		if((nRet = DasBuf_printf(pBuf, " surface=\"%s\"", DasCtx_name(pSurf))) != DAS_OKAY)
			return nRet;
	}

	return DasBuf_printf(pBuf, " system=\"%s\" sysorder=\"%d;%d;%d\"/>\n",
		das_compsys_str(pThis->uSysType),
		 pThis->uDirs       & 0x3,
		(pThis->uDirs >> 2) & 0x3,
		(pThis->uDirs >> 4) & 0x3
	);
}

static int _geoloc_getRefs(const DasForm* pBase, ubyte* pIds, int nMax)
{
	const DasFormGeoLoc* pThis = (const DasFormGeoLoc*)pBase;

	int n = 0;
	if((pThis->uCenterId != 0)&&(n < nMax)) pIds[n++] = pThis->uCenterId;
	if((pThis->uFrameId  != 0)&&(n < nMax)) pIds[n++] = pThis->uFrameId;
	if((pThis->uSurfId   != 0)&&(n < nMax)) pIds[n++] = pThis->uSurfId;
	return n;
}

/* --- presentation -------------------------------------------------------- */

static char* _geoloc_prnRun(
	const DasForm* pForm, const ubyte* pRun, uint32_t nElems,
	das_val_type et, char* sBuf, int nLen
){
	const DasFormGeoLoc* pThis = (const DasFormGeoLoc*)pForm;

	int nUsed = snprintf(sBuf, (size_t)nLen, "%s@%hhu[",
	                     das_compsys_str(pThis->uSysType), pThis->uCenterId);

	for(uint32_t u = 0; (u < nElems)&&(nUsed < nLen - 2); ++u){
		double d = 0.0;
		if(das_value_binXform(et, pRun + u*das_vt_size(et), NULL, vtDouble,
		                      (ubyte*)&d, NULL, 0) != DAS_OKAY)
			break;
		nUsed += snprintf(sBuf + nUsed, (size_t)(nLen - nUsed), "%s%.6g",
		                  (u > 0) ? " " : "", d);
	}
	if(nUsed < nLen - 1) snprintf(sBuf + nUsed, (size_t)(nLen - nUsed), "]");
	return sBuf;
}

static bool _geoloc_pack(
	const DasForm* pBase, const das_operand* pOp, const ubyte* pRun,
	das_datum* pOut
){
	if((pOp->nIntRank != 1)||(pOp->aIntShape[0] < 1)||(pOp->aIntShape[0] > 3))
		return das_error(DASERR_FORM,
			"A position has 1 to 3 components in one level");

	return das_datum_box(
		pOut, pBase, _geoloc_prnRun, pRun, (size_t)pOp->aIntShape[0],
		pOp->vtElem, pOp->units
	);
}

static das_val_type _geoloc_datumType(const DasForm* pBase){ return vtComposite; }

int DasFormGeoLoc_values(
	const DasForm* pThis, const das_datum* pDm, double* pOut, int nMax
){
	if(!DasForm_isGeoLoc(pThis))
		return -1 * das_error(DASERR_FORM, "Not a geoloc formalism");

	const ubyte* pRun = das_datum_run(pDm);
	if(pRun == NULL)
		return -1 * das_error(DASERR_FORM, "Datum carries no component run");

	int nElems = (int)das_datum_nElems(pDm);
	das_val_type et = das_datum_elemType(pDm);

	/* No unit-vector default here, unlike form_vector.  A position with a
	   missing radial component is not "a direction at unit distance", it is an
	   incomplete position, and inventing a radius of 1 would put a spacecraft
	   one metre from the planet's centre. */
	for(int i = 0; i < nMax; ++i) pOut[i] = 0.0;

	int n = (nElems < nMax) ? nElems : nMax;
	for(int i = 0; i < n; ++i){
		if(das_value_binXform(et, pRun + i*das_vt_size(et), NULL, vtDouble,
		                      (ubyte*)(pOut + i), NULL, 0) != DAS_OKAY)
			return -1 * das_error(DASERR_FORM, "Bad component %d", i);
	}
	return n;
}

static char* _geoloc_prnIntr(
	const DasForm* pBase, const DasCtxTbl* pTbl, char* sBuf, int nLen
){
	const DasFormGeoLoc* pThis = (const DasFormGeoLoc*)pBase;

	const DasCtx* pC = (pTbl == NULL) ? NULL : DasCtxTbl_get(pTbl, pThis->uCenterId);
	const DasCtx* pF = ((pTbl == NULL)||(pThis->uFrameId == 0)) ? NULL
	                 : DasCtxTbl_getOfKind(pTbl, CTX_FRAME, pThis->uFrameId);

	snprintf(sBuf, (size_t)nLen, " geoloc(from %s in %s,%s)",
		(pC != NULL) ? DasCtx_name(pC) : "?",
		(pF != NULL) ? DasCtx_name(pF) : "no frame",
		das_compsys_str(pThis->uSysType)
	);
	return sBuf;
}

static DasForm* _geoloc_copy(const DasForm* pBase)
{
	DasFormGeoLoc* pCopy = (DasFormGeoLoc*)calloc(1, sizeof(DasFormGeoLoc));
	memcpy(pCopy, pBase, sizeof(DasFormGeoLoc));
	pCopy->base.nRef = 1;
	return &(pCopy->base);
}

static void _geoloc_release(DasForm* pBase){ free(pBase); }


/* ************************************************************************* */
/* The recipe                                                                */

typedef struct das_binop_geoloc {
	DasBinOp base;

	das_val_type vtL, vtR;
	int    nOp;
	int    nComp;

	bool   bTwoPoints;      /* difference of two positions */
	bool   bPosLeft;        /* which side carries the position, when mixed */

	ubyte  uSysL, uSysR, uSysOut;
	ubyte  aDirL[3], aDirR[3], aDirOut[3];
	double rOtherScale;     /* unit conversion for the displacement operand */
} DasBinOpGeoLoc;

static bool _gl_gather(
	const ubyte* pRun, das_val_type et, size_t uSz, int nComp,
	const ubyte* pDirs, double* pOut
){
	pOut[0] = pOut[1] = pOut[2] = 0.0;
	for(int i = 0; i < nComp; ++i){
		double d;
		if(das_value_binXform(et, pRun + i*uSz, NULL, vtDouble, (ubyte*)&d,
		                      NULL, 0) != DAS_OKAY)
			return false;
		pOut[pDirs[i]] = d;
	}
	return true;
}

static bool _geoloc_apply(
	const DasBinOp* pOp, const ubyte* pLRun, const ubyte* pRRun, ubyte* pOutRun
){
	const DasBinOpGeoLoc* pThis = (const DasBinOpGeoLoc*)pOp;

	size_t uL   = das_vt_size(pThis->vtL);
	size_t uR   = das_vt_size(pThis->vtR);
	size_t uOut = das_vt_size(pOp->vtOut);

	double aL[3], aR[3], aLc[3], aRc[3], aRes[3], aOut[3];

	if(!_gl_gather(pLRun, pThis->vtL, uL, pThis->nComp, pThis->aDirL, aL))
		return false;
	if(!_gl_gather(pRRun, pThis->vtR, uR, pThis->nComp, pThis->aDirR, aR))
		return false;

	/* Affine math is only defined on cartesian components, so both sides go
	   through cartesian and the result comes back in whichever system names
	   it: the left operand's for a difference, the POSITION's when a
	   displacement is applied. */
	if(!das_vsys_toCart(pThis->uSysL, aL, aLc)) return false;
	if(!das_vsys_toCart(pThis->uSysR, aR, aRc)) return false;

	if(pThis->bTwoPoints){
		for(int i = 0; i < 3; ++i)
			aRes[i] = aLc[i] - aRc[i]*pThis->rOtherScale;
	}
	else if(pThis->bPosLeft){
		for(int i = 0; i < 3; ++i){
			double d = aRc[i]*pThis->rOtherScale;
			aRes[i] = (pThis->nOp == D2BOP_SUB) ? (aLc[i] - d) : (aLc[i] + d);
		}
	}
	else{
		/* displacement + position; subtraction was refused at resolve */
		for(int i = 0; i < 3; ++i)
			aRes[i] = aRc[i] + aLc[i]*pThis->rOtherScale;
	}

	if(!das_vsys_fromCart(pThis->uSysOut, aRes, aOut)) return false;

	for(int i = 0; i < pThis->nComp; ++i){
		double d = aOut[pThis->aDirOut[i]];
		if(das_value_binXform(vtDouble, (const ubyte*)&d, NULL, pOp->vtOut,
		                      pOutRun + i*uOut, NULL, 0) != DAS_OKAY)
			return false;
	}
	return true;
}

static void _geoloc_binop_release(DasBinOp* pOp)
{
	DasForm_decRef(pOp->pForm);
	free(pOp);
}

static const DasBinOp_VTbl g_vtblGeoLoc = { _geoloc_apply, _geoloc_binop_release };


/* ************************************************************************* */
/* Dispatch                                                                  */

static void _geoloc_unpackDirs(ubyte uDirs, ubyte* pOut)
{
	for(int i = 0; i < 3; ++i) pOut[i] = (uDirs >> (2*i)) & 0x3;
}

static das_binop_stat _geoloc_shapeOk(
	const das_operand* pL, const das_operand* pR, int* pnComp
){
	if((pL->nIntRank != 1)||(pR->nIntRank != 1))
		return REFUSE("Affine vector math needs one internal level per side");
	if(pL->aIntShape[0] != pR->aIntShape[0])
		return REFUSE("Mismatched component counts, %zd and %zd",
		              pL->aIntShape[0], pR->aIntShape[0]);
	if(pL->aIntShape[0] != 3)
		return REFUSE("Converting between component systems needs all three "
		              "components, have %zd", pL->aIntShape[0]);
	*pnComp = 3;
	return dbsOkay;
}

static DasBinOpGeoLoc* _geoloc_recipe(
	const das_operand* pL, const das_operand* pR, int nOp, int nComp,
	ubyte uSysL, ubyte uDirL, ubyte uSysR, ubyte uDirR,
	ubyte uSysOut, ubyte uDirOut, double rScale
){
	DasBinOpGeoLoc* pRes = (DasBinOpGeoLoc*)calloc(1, sizeof(DasBinOpGeoLoc));
	pRes->base.pVTbl = &g_vtblGeoLoc;
	pRes->base.nRef  = 1;

	pRes->vtL   = pL->vtElem;
	pRes->vtR   = pR->vtElem;
	pRes->nOp   = nOp;
	pRes->nComp = nComp;
	pRes->uSysL = uSysL;  pRes->uSysR = uSysR;  pRes->uSysOut = uSysOut;
	pRes->rOtherScale = rScale;
	_geoloc_unpackDirs(uDirL,   pRes->aDirL);
	_geoloc_unpackDirs(uDirR,   pRes->aDirR);
	_geoloc_unpackDirs(uDirOut, pRes->aDirOut);

	pRes->base.nIntRank     = 1;
	pRes->base.aIntShape[0] = nComp;
	return pRes;
}

static das_binop_stat _geoloc_binOpLeft(
	const DasForm* pBase, const das_operand* pL, int nOp,
	const das_operand* pR, const DasCtxTbl* pTbl, DasBinOp** ppOut
){
	const DasFormGeoLoc* pThis = (const DasFormGeoLoc*)pBase;

	bool bRightIsPos = DasForm_isGeoLoc(pR->pForm);
	if(!bRightIsPos && !DasForm_isVector(pR->pForm)) return dbsDecline;

	int nComp = 0;
	if(_geoloc_shapeOk(pL, pR, &nComp) != dbsOkay) return dbsRefuse;

	/* --- position OP position -------------------------------------------- */
	if(bRightIsPos){
		const DasFormGeoLoc* pRg = (const DasFormGeoLoc*)pR->pForm;

		if(nOp != D2BOP_SUB)
			return REFUSE("Two positions can only be subtracted; '%s' of two "
			              "of them has no meaning", das_op_toStr(nOp, NULL));

		/* Strict identity on the ORIGIN, the spatial twin of point's strict
		   epoch rule.  Positions measured from different bodies have no
		   difference until one is re-referenced, and silently differencing
		   them would hide which body the answer is relative to. */
		if(pThis->uCenterId != pRg->uCenterId){
			const DasCtx* pA = (pTbl == NULL) ? NULL : DasCtxTbl_get(pTbl, pThis->uCenterId);
			const DasCtx* pB = (pTbl == NULL) ? NULL : DasCtxTbl_get(pTbl, pRg->uCenterId);
			return REFUSE("Positions measured from different bodies, '%s' and "
			              "'%s'; re-reference one first",
			              (pA != NULL) ? DasCtx_name(pA) : "(unbound)",
			              (pB != NULL) ? DasCtx_name(pB) : "(unbound)");
		}

		if(pThis->uFrameId != pRg->uFrameId)
			return REFUSE("Positions in different frames; rotate one first");

		if(pL->units != pR->units)
			return REFUSE("Subtracting positions needs one unit on both sides, "
			              "have %s and %s", Units_toStr(pL->units),
			              Units_toStr(pR->units));

		DasBinOpGeoLoc* pRes = _geoloc_recipe(
			pL, pR, D2BOP_SUB, nComp,
			pThis->uSysType, pThis->uDirs, pRg->uSysType, pRg->uDirs,
			pThis->uSysType, pThis->uDirs, 1.0
		);
		pRes->bTwoPoints = true;

		/* The DIFFERENCE of two positions is a free vector -- no origin, and
		   therefore no longer a geoloc.  This is the 3-D image of
		   point - point = interval. */
		pRes->base.units = pL->units;
		pRes->base.pForm = new_DasFormVector(
			pThis->uFrameId, pThis->uSysType, pThis->uDirs
		);
		pRes->base.vtOut = das_vt_merge(pR->vtElem, D2BOP_SUB, pL->vtElem);

		*ppOut = &(pRes->base);
		return dbsOkay;
	}

	/* --- position OP displacement ---------------------------------------- */
	const DasForm* pVf = pR->pForm;

	if((nOp != D2BOP_ADD)&&(nOp != D2BOP_SUB))
		return REFUSE("A position can only be moved by a displacement, '%s' is "
		              "not defined on one", das_op_toStr(nOp, NULL));

	if(pThis->uFrameId != DasFormVector_frameId(pVf))
		return REFUSE("The displacement is in a different frame than the "
		              "position; rotate one first");

	double rScale = 1.0;
	if(pL->units != pR->units){
		if(!Units_canConvert(pR->units, pL->units))
			return REFUSE("Can not move a position in %s by a displacement in "
			              "%s", Units_toStr(pL->units), Units_toStr(pR->units));
		rScale = Units_convertTo(pL->units, 1.0, pR->units);
	}

	DasBinOpGeoLoc* pRes = _geoloc_recipe(
		pL, pR, nOp, nComp,
		pThis->uSysType, pThis->uDirs,
		DasFormVector_sysType(pVf), DasFormVector_dirs(pVf),
		pThis->uSysType, pThis->uDirs, rScale
	);
	pRes->bPosLeft = true;

	/* Still a position on the same body, in the same frame and system */
	pRes->base.units = pL->units;
	pRes->base.pForm = new_DasFormGeoLoc(
		pThis->uCenterId, pThis->uFrameId, pThis->uSurfId,
		pThis->uSysType, pThis->uDirs
	);
	pRes->base.vtOut = das_vt_merge(pR->vtElem, nOp, pL->vtElem);

	*ppOut = &(pRes->base);
	return dbsOkay;
}

/* displacement + position.  Claimed HERE, not in form_vector.c, because a
   vector may not learn what a position is.  Stated explicitly rather than
   inferred from commutativity: no hook ever reorders operands, so a legal
   ordering has to be written down. */
static das_binop_stat _geoloc_binOpRight(
	const DasForm* pBase, const das_operand* pL, int nOp,
	const das_operand* pR, const DasCtxTbl* pTbl, DasBinOp** ppOut
){
	const DasFormGeoLoc* pThis = (const DasFormGeoLoc*)pBase;

	if(!DasForm_isVector(pL->pForm)) return dbsDecline;

	if(nOp == D2BOP_SUB)
		return REFUSE("A displacement minus a position is meaningless; write "
		              "position - displacement to move it backward");
	if(nOp != D2BOP_ADD)
		return REFUSE("A displacement and a position can only be added, not "
		              "'%s'", das_op_toStr(nOp, NULL));

	int nComp = 0;
	if(_geoloc_shapeOk(pL, pR, &nComp) != dbsOkay) return dbsRefuse;

	if(pThis->uFrameId != DasFormVector_frameId(pL->pForm))
		return REFUSE("The displacement is in a different frame than the "
		              "position; rotate one first");

	double rScale = 1.0;
	if(pL->units != pR->units){
		if(!Units_canConvert(pL->units, pR->units))
			return REFUSE("Can not move a position in %s by a displacement in "
			              "%s", Units_toStr(pR->units), Units_toStr(pL->units));
		rScale = Units_convertTo(pR->units, 1.0, pL->units);
	}

	DasBinOpGeoLoc* pRes = _geoloc_recipe(
		pL, pR, D2BOP_ADD, nComp,
		DasFormVector_sysType(pL->pForm), DasFormVector_dirs(pL->pForm),
		pThis->uSysType, pThis->uDirs,
		pThis->uSysType, pThis->uDirs, rScale
	);
	pRes->bPosLeft = false;

	pRes->base.units = pR->units;
	pRes->base.pForm = new_DasFormGeoLoc(
		pThis->uCenterId, pThis->uFrameId, pThis->uSurfId,
		pThis->uSysType, pThis->uDirs
	);
	pRes->base.vtOut = das_vt_merge(pR->vtElem, D2BOP_ADD, pL->vtElem);

	*ppOut = &(pRes->base);
	return dbsOkay;
}

const DasForm_VTbl das_form_geoloc_vtbl = {
	"geoloc",
	_geoloc_new,
	_geoloc_setParam,
	_geoloc_encode,
	_geoloc_getRefs,
	_geoloc_pack,
	_geoloc_datumType,
	_geoloc_prnIntr,
	_geoloc_binOpLeft,
	_geoloc_binOpRight,
	_geoloc_copy,
	_geoloc_release
};
