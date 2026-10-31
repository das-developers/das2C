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

/* The body-centered position formalism.  See form_pos.h for the rules.
 *
 * Position knows about VECTOR, since the difference of two positions is a free
 * vector and this file has to name one as its result.  Vector does not know
 * about position.  That is the same arrow, one level up, as form_pt.c ->
 * form_lin.h, and for the same reason: 1-D affine and 3-D affine are one
 * structure at two scales.
 */

#define _POSIX_C_SOURCE 200112L

#include <string.h>
#ifndef _WIN32
#include <strings.h>
#else
#define strcasecmp  _stricmp
#define strncasecmp _strnicmp
#endif
#include <stdio.h>
#include <stdlib.h>
#include <math.h>

#include "util.h"
#include "log.h"
#include "value.h"
#include "units.h"
#include "operator.h"

#include "buffer.h"
#include "descriptor.h"
#include "form.h"
#include "form_vec.h"
#include "form_pos.h"
#include "info_priv.h"
#include "form_priv.h"

#define REFUSE(...) (das_error(DASERR_FORM, __VA_ARGS__), dbsRefuse)

/* ************************************************************************* */
/* The formalism                                                             */

typedef struct das_form_pos {
	DasForm base;

	/* The ORIGIN, spelled body= on the wire because the thing at the center of
	   a position IS a body.  Never "" on a valid form. */
	char sBody[DASFORM_NAME_SZ];
	char sFrame[DASFORM_NAME_SZ];
	char sSurface[DASFORM_NAME_SZ]; /* the ellipsoid, for detic and graphic */
	char sSysOrder[16];   /* "" only before validate() with no order given */
	bool bFixed;
	ubyte uSysType;
	ubyte aDirs[3];

	/* Slots this variable actually uses; see the same field on DasFormVector.
	   0 until validate() is handed the internal shape. */
	ubyte uComps;
} DasFormPos;

static DasForm* _dasformpos_new(void)
{
	DasFormPos* pThis = (DasFormPos*)calloc(1, sizeof(DasFormPos));
	pThis->base.pVTbl = &das_form_pos_vtbl;
	pThis->uSysType   = DAS_VSYS_CART;
	pThis->aDirs[0] = 0; pThis->aDirs[1] = 1; pThis->aDirs[2] = 2;
	return &(pThis->base);
}

DasForm* new_DasFormPos(
	const char* sBody, const char* sFrame, const char* sSurface,
	bool bFixed, ubyte uSysType, const ubyte* pDirs
){
	if((sBody == NULL)||(sBody[0] == '\0')){
		das_error(DASERR_FORM,
			"A position needs a body; values with no origin are free vectors "
			"and belong in <ops kind=\"vector\">"
		);
		return NULL;
	}

	DasFormPos* pThis = (DasFormPos*)_dasformpos_new();
	strncpy(pThis->sBody, sBody, DASFORM_NAME_SZ - 1);
	if(sFrame   != NULL) strncpy(pThis->sFrame,   sFrame,   DASFORM_NAME_SZ - 1);
	if(sSurface != NULL) strncpy(pThis->sSurface, sSurface, DASFORM_NAME_SZ - 1);
	pThis->bFixed    = bFixed;
	pThis->uSysType  = uSysType;
	if(pDirs != NULL){
		for(int i = 0; i < 3; ++i) pThis->aDirs[i] = pDirs[i];
		_dasform_fmtOrder(pThis->sSysOrder, sizeof(pThis->sSysOrder), pThis->aDirs, 3);
	}
	return &(pThis->base);
}

#define POS_GET(FN, FIELD) \
	ubyte FN(const DasForm* pThis){ \
		if(!DasForm_isKind(pThis, DAS_FORM_POS)){ \
			das_error(DASERR_FORM, "Not a position formalism"); \
			return 0; \
		} \
		return ((const DasFormPos*)pThis)->FIELD; \
	}

#define POS_GETSTR(FN, FIELD) \
	const char* FN(const DasForm* pThis){ \
		if(!DasForm_isKind(pThis, DAS_FORM_POS)){ \
			das_error(DASERR_FORM, "Not a position formalism"); \
			return NULL; \
		} \
		const char* s = ((const DasFormPos*)pThis)->FIELD; \
		return (s[0] == '\0') ? NULL : s; \
	}

POS_GETSTR(DasFormPos_body,    sBody)
POS_GETSTR(DasFormPos_frame,   sFrame)
POS_GETSTR(DasFormPos_surface, sSurface)
POS_GET(DasFormPos_sysType,  uSysType)

bool DasFormPos_fixed(const DasForm* pThis)
{
	if(!DasForm_isKind(pThis, DAS_FORM_POS)){
		das_error(DASERR_FORM, "Not a position formalism");
		return false;
	}
	return ((const DasFormPos*)pThis)->bFixed;
}

const ubyte* DasFormPos_dirs(const DasForm* pThis)
{
	if(!DasForm_isKind(pThis, DAS_FORM_POS)){
		das_error(DASERR_FORM, "Not a position formalism");
		return NULL;
	}
	return ((const DasFormPos*)pThis)->aDirs;
}

static const char* _DasFormPos_compSym(const DasForm* pThis, int iComp)
{
	if(!DasForm_isKind(pThis, DAS_FORM_POS)||(iComp < 0)) return NULL;

	/* Bounded by uComps, for the reason form_vec.c gives. */
	const DasFormPos* pG = (const DasFormPos*)pThis;
	if(iComp >= pG->uComps) return NULL;

	return das_vsys_symbol(pG->uSysType, pG->aDirs[iComp]);
}

/* --- parameters ---------------------------------------------------------- */

static DasErrCode _DasFormPos_setOrder(DasFormPos* pThis, const char* sOrd)
{
	ubyte aDir[3] = {0, 1, 2};
	int iComp = 0;
	for(const char* p = sOrd; (*p != '\0')&&(iComp < 3); ++p){
		if((*p >= '0')&&(*p <= '2')){ aDir[iComp] = (ubyte)(*p - '0'); ++iComp; }
		else if(*p != ';')
			return das_error(DASERR_FORM, "Bad sysorder token '%s'", sOrd);
	}
	for(int i = 0; i < 3; ++i) pThis->aDirs[i] = aDir[i];
	strncpy(pThis->sSysOrder, sOrd, sizeof(pThis->sSysOrder) - 1);
	return DAS_OKAY;
}

static DasErrCode _DasFormPos_setParam(
	DasForm* pBase, const char* sName, const char* sVal
){
	DasFormPos* pThis = (DasFormPos*)pBase;

	if(strcmp(sName, "system") == 0){
		pThis->uSysType = das_vsys_fromStr(sVal);
		if(pThis->uSysType == 0)
			return das_error(DASERR_FORM, "Unknown component system '%s'", sVal);
		return DAS_OKAY;
	}

	if(strcmp(sName, "sysorder") == 0)
		return _DasFormPos_setOrder(pThis, sVal);

	/* body= is what makes this a position rather than a vector: it names the
	   ORIGIN the values are measured from.  The frame only says which way the
	   axes point and may be attached to some other body (Titan relative to
	   IAU_SATURN); fixed= is what relates the two. */
	if(strcmp(sName, "body") == 0){
		strncpy(pThis->sBody, sVal, DASFORM_NAME_SZ - 1);
		return DAS_OKAY;
	}

	if(strcmp(sName, "frame") == 0){
		strncpy(pThis->sFrame, sVal, DASFORM_NAME_SZ - 1);
		return DAS_OKAY;
	}

	if(strcmp(sName, "surface") == 0){
		strncpy(pThis->sSurface, sVal, DASFORM_NAME_SZ - 1);
		return DAS_OKAY;
	}

	if(strcmp(sName, "fixed") == 0)
		return das_str2bool(sVal, &(pThis->bFixed)) ? DAS_OKAY : DASERR_FORM;

	return das_error(DASERR_FORM,
		"<ops kind=\"position\"> has no parameter '%s'", sName
	);
}

static DasErrCode _DasFormPos_encode(
	const DasForm* pBase, DasBuf* pBuf
){
	const DasFormPos* pThis = (const DasFormPos*)pBase;

	if(pThis->sBody[0] == '\0')
		return das_error(DASERR_FORM,
			"A position can not be written without its body");

	DasErrCode nRet = DasBuf_printf(pBuf,
		"      <ops kind=\"position\" body=\"%s\"", pThis->sBody);
	if(nRet != DAS_OKAY) return nRet;

	if(pThis->sFrame[0] != '\0'){
		if((nRet = DasBuf_printf(pBuf, " frame=\"%s\"", pThis->sFrame)) != DAS_OKAY)
			return nRet;
	}
	if(pThis->sSurface[0] != '\0'){
		if((nRet = DasBuf_printf(pBuf, " surface=\"%s\"", pThis->sSurface)) != DAS_OKAY)
			return nRet;
	}
	if(pThis->bFixed){
		if((nRet = DasBuf_puts(pBuf, " fixed=\"true\"")) != DAS_OKAY) return nRet;
	}

	/* cartesian is the default, so naming it says nothing.  Same reasoning
	   as the ascending sysorder below: emit a parameter only when it
	   differs from what a reader would assume. */
	if(pThis->uSysType != DAS_VSYS_CART){
		if((nRet = DasBuf_printf(pBuf, " system=\"%s\"",
		                         das_vsys_toStr(pThis->uSysType))) != DAS_OKAY)
			return nRet;
	}

	if((nRet = _dasform_prnOrder(pBuf, pThis->aDirs, pThis->uComps)) != DAS_OKAY)
		return nRet;

	return DasBuf_puts(pBuf, "/>\n");
}

/* A NULL from DasFormVector_frame() means frameless; "" means the same
   thing on this side, so normalize before comparing. */
static bool _pos_frameNe(const char* sMine, const char* sTheirs)
{
	if(sMine   == NULL) sMine   = "";
	if(sTheirs == NULL) sTheirs = "";
	return (strcasecmp(sMine, sTheirs) != 0);
}

/* body= is the ONE parameter a position cannot do without: it names the origin,
   and a position with no origin is a free vector.  new_DasFormPos() refuses
   an empty one, but the wire path never goes through that constructor, so
   before this slot existed a stream could declare <ops kind="position"
   system="detic"/> and get an originless position that failed much later, at
   write time, or not at all. */
static DasErrCode _DasFormPos_validate(
	DasForm* pBase, int nIntRank, const ptrdiff_t* pIntShape
){
	DasFormPos* pThis = (DasFormPos*)pBase;

	if(pThis->sBody[0] == '\0')
		return das_error(DASERR_FORM,
			"<ops kind=\"position\"> has no body=.  A position is measured FROM "
			"somewhere; values with no origin are free vectors and belong in "
			"<ops kind=\"vector\">"
		);

	/* An ellipsoidal system measures against a reference surface, so naming
	   one is not decoration. */
	if(das_vsys_isEllipsoidal(pThis->uSysType)&&(pThis->sSurface[0] == '\0'))
		return das_error(DASERR_FORM,
			"system=\"%s\" is measured on an ellipsoid, so it needs a surface=",
			das_vsys_toStr(pThis->uSysType)
		);

	if(nIntRank != 1)
		return das_error(DASERR_FORM,
			"A position is one level of components, but this variable declares "
			"%d internal levels", nIntRank
		);

	if((pIntShape[0] < 1)||(pIntShape[0] > 3))
		return das_error(DASERR_FORM,
			"A position has 1 to 3 components, not %zd", pIntShape[0]
		);

	pThis->uComps = (ubyte)pIntShape[0];

	/* Respell to the real count, for the reasons _DasFormVector_validate gives */
	_dasform_fmtOrder(pThis->sSysOrder, sizeof(pThis->sSysOrder), pThis->aDirs, pThis->uComps);
	return DAS_OKAY;
}

static const char* _DasFormPos_getParam(
	const DasForm* pBase, const char* sName, ubyte* pType
)
{
	const DasFormPos* pThis = (const DasFormPos*)pBase;

	if(strcmp(sName, "frame") == 0){
		if(pType != NULL) *pType = DASPROP_STRING | DASPROP_SINGLE;
		return (pThis->sFrame[0] == '\0') ? NULL : pThis->sFrame;
	}
	if(strcmp(sName, "surface") == 0){
		if(pType != NULL) *pType = DASPROP_STRING | DASPROP_SINGLE;
		return (pThis->sSurface[0] == '\0') ? NULL : pThis->sSurface;
	}
	if(strcmp(sName, "body") == 0){
		if(pType != NULL) *pType = DASPROP_STRING | DASPROP_SINGLE;
		return (pThis->sBody[0] == '\0') ? NULL : pThis->sBody;
	}
	if(strcmp(sName, "system") == 0){
		if(pType != NULL) *pType = DASPROP_STRING | DASPROP_SINGLE;
		return das_vsys_toStr(pThis->uSysType);
	}
	if(strcmp(sName, "sysorder") == 0){
		if(pType != NULL) *pType = DASPROP_INT | DASPROP_SET;
		return (pThis->sSysOrder[0] == '\0') ? "0;1;2" : pThis->sSysOrder;
	}
	if(strcmp(sName, "fixed") == 0){
		if(pType != NULL) *pType = DASPROP_BOOL | DASPROP_SINGLE;
		return pThis->bFixed ? "true" : "false";
	}

	return NULL;
}

static const char* _DasFormPos_paramName(const DasForm* pBase, int iParam)
{
	static const char* aNames[] = {
		"body", "frame", "surface", "fixed", "system", "sysorder"
	};
	if((iParam < 0)||(iParam >= 6)) return NULL;
	return aNames[iParam];
}

/* --- presentation -------------------------------------------------------- */

static char* _DasFormPos_prnRun(
	const DasForm* pForm, const ubyte* pRun, uint32_t nElems,
	das_val_type et, char* sBuf, size_t uLen
){
	const DasFormPos* pThis = (const DasFormPos*)pForm;

	int n = _prn_catf(sBuf, uLen, 0, "%s@%s[",
	                  das_vsys_toStr(pThis->uSysType), pThis->sBody);

	for(uint32_t u = 0; u < nElems; ++u){
		double d = 0.0;
		if(das_value_binXform(et, pRun + u*das_vt_size(et), NULL, vtDouble,
		                      (ubyte*)&d, NULL, 0) != DAS_OKAY)
			break;
		n = _prn_catf(sBuf, uLen, n, "%s%.6g", (u > 0) ? " " : "", d);
	}
	_prn_catf(sBuf, uLen, n, "]");
	return sBuf;
}

static bool _DasFormPos_pack(
	const DasForm* pBase, const das_operand* pOp, const ubyte* pRun,
	das_datum* pOut
){
	if((pOp->nIntRank != 1)||(pOp->aIntShape[0] < 1)||(pOp->aIntShape[0] > 3))
		return das_error_false(DASERR_FORM,
			"A position has 1 to 3 components in one level"
		);

	return das_datum_box(
		pOut, pBase, pRun, pOp->nIntRank, pOp->aIntShape, pOp->vtElem,
		pOp->units
	);
}

static das_val_type _DasFormPos_datumType(const DasForm* pBase){ return vtComposite; }


static DasForm* _DasFormPos_copy(const DasForm* pBase)
{
	DasFormPos* pCopy = (DasFormPos*)calloc(1, sizeof(DasFormPos));
	memcpy(pCopy, pBase, sizeof(DasFormPos));
	return &(pCopy->base);
}

static void _DasFormPos_release(DasForm* pBase){ free(pBase); }


/* ************************************************************************* */
/* The recipe                                                                */

typedef struct das_binop_pos {
	DasBinOp base;

	das_val_type vtL, vtR;
	int    nOp;
	int    nComp;

	bool   bTwoPoints;      /* difference of two positions */
	bool   bPosLeft;        /* which side carries the position, when mixed */

	ubyte  uSysL, uSysR, uSysOut;
	ubyte  aDirL[3], aDirR[3], aDirOut[3];
	double rOtherScale;     /* unit conversion for the displacement operand */
} DasBinOpPos;

static bool _pos_gather(
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

static bool _DasBinOpPos_apply(
	const DasBinOp* pOp, const ubyte* pLRun, const ubyte* pRRun, ubyte* pOutRun
){
	const DasBinOpPos* pThis = (const DasBinOpPos*)pOp;

	size_t uL   = das_vt_size(pThis->vtL);
	size_t uR   = das_vt_size(pThis->vtR);
	size_t uOut = das_vt_size(pOp->vtOut);

	double aL[3], aR[3], aLc[3], aRc[3], aRes[3], aOut[3];

	if(!_pos_gather(pLRun, pThis->vtL, uL, pThis->nComp, pThis->aDirL, aL))
		return false;
	if(!_pos_gather(pRRun, pThis->vtR, uR, pThis->nComp, pThis->aDirR, aR))
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

static void _DasBinOpPos_release(DasBinOp* pOp)
{
	del_DasForm(pOp->pForm);
	free(pOp);
}

static const DasBinOp_VTbl g_vtblPos = { _DasBinOpPos_apply, _DasBinOpPos_release };


/* ************************************************************************* */
/* Dispatch                                                                  */

static das_binop_stat _pos_shapeOk(
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

static DasBinOpPos* _dasbinoppos_new(
	const das_operand* pL, const das_operand* pR, int nOp, int nComp,
	ubyte uSysL, const ubyte* pDirL, ubyte uSysR, const ubyte* pDirR,
	ubyte uSysOut, const ubyte* pDirOut, double rScale
){
	DasBinOpPos* pRes = (DasBinOpPos*)calloc(1, sizeof(DasBinOpPos));
	pRes->base.pVTbl = &g_vtblPos;
	pRes->base.nRef  = 1;

	pRes->vtL   = pL->vtElem;
	pRes->vtR   = pR->vtElem;
	pRes->nOp   = nOp;
	pRes->nComp = nComp;
	pRes->uSysL = uSysL;  pRes->uSysR = uSysR;  pRes->uSysOut = uSysOut;
	pRes->rOtherScale = rScale;
	for(int i = 0; i < 3; ++i){
		pRes->aDirL[i]   = pDirL[i];
		pRes->aDirR[i]   = pDirR[i];
		pRes->aDirOut[i] = pDirOut[i];
	}

	pRes->base.nIntRank     = 1;
	pRes->base.aIntShape[0] = nComp;
	return pRes;
}

static das_binop_stat _DasFormPos_binOpLeft(
	const DasForm* pBase, const das_operand* pL, int nOp,
	const das_operand* pR, DasBinOp** ppOut
){
	const DasFormPos* pThis = (const DasFormPos*)pBase;

	bool bRightIsPos = DasForm_isKind(pR->pForm, DAS_FORM_POS);
	if(!bRightIsPos && !DasForm_isKind(pR->pForm, DAS_FORM_VEC))
		return dbsDecline;

	int nComp = 0;
	if(_pos_shapeOk(pL, pR, &nComp) != dbsOkay) return dbsRefuse;

	/* --- position OP position -------------------------------------------- */
	if(bRightIsPos){
		const DasFormPos* pRg = (const DasFormPos*)pR->pForm;

		if(nOp != D2BOP_SUB)
			return REFUSE("Two positions can only be subtracted; '%s' of two "
			              "of them has no meaning", das_op_toStr(nOp, NULL));

		/* Strict identity on the ORIGIN, the spatial twin of point's strict
		   epoch rule.  Positions measured from different bodies have no
		   difference until one is re-referenced, and silently differencing
		   them would hide which body the answer is relative to. */
		if(strcasecmp(pThis->sBody, pRg->sBody) != 0){
			const char* pA = pThis->sBody;
			const char* pB = pRg->sBody;
			return REFUSE("Positions measured from different bodies, '%s' and "
			              "'%s'; re-reference one first",
			              (pA[0] != '\0') ? pA : "(unbound)",
			              (pB[0] != '\0') ? pB : "(unbound)");
		}

		if(strcasecmp(pThis->sFrame, pRg->sFrame) != 0)
			return REFUSE("Positions in different frames; rotate one first");

		if(pL->units != pR->units)
			return REFUSE("Subtracting positions needs one unit on both sides, "
			              "have %s and %s", Units_toStr(pL->units),
			              Units_toStr(pR->units));

		DasBinOpPos* pRes = _dasbinoppos_new(
			pL, pR, D2BOP_SUB, nComp,
			pThis->uSysType, pThis->aDirs, pRg->uSysType, pRg->aDirs,
			pThis->uSysType, pThis->aDirs, 1.0
		);
		pRes->bTwoPoints = true;

		/* The DIFFERENCE of two positions is a free vector -- no origin, and
		   therefore no longer a position.  This is the 3-D image of
		   point - point = interval. */
		pRes->base.units = pL->units;
		/* No origin, so no fixed= either: that related the frame to an origin */
		pRes->base.pForm = new_DasFormVector(
			pThis->sFrame, pThis->uSysType, pThis->aDirs
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

	if(_pos_frameNe(pThis->sFrame, DasFormVector_frame(pVf)))
		return REFUSE("The displacement is in a different frame than the "
		              "position; rotate one first");

	double rScale = 1.0;
	if(pL->units != pR->units){
		if(!Units_canConvert(pR->units, pL->units))
			return REFUSE("Can not move a position in %s by a displacement in "
			              "%s", Units_toStr(pL->units), Units_toStr(pR->units));
		rScale = Units_convertTo(pL->units, 1.0, pR->units);
	}

	DasBinOpPos* pRes = _dasbinoppos_new(
		pL, pR, nOp, nComp,
		pThis->uSysType, pThis->aDirs,
		DasFormVector_sysType(pVf), DasFormVector_dirs(pVf),
		pThis->uSysType, pThis->aDirs, rScale
	);
	pRes->bPosLeft = true;

	/* Still a position on the same body, in the same frame and system */
	pRes->base.units = pL->units;
	pRes->base.pForm = new_DasFormPos(
		pThis->sBody, pThis->sFrame, pThis->sSurface,
		pThis->bFixed, pThis->uSysType, pThis->aDirs
	);
	pRes->base.vtOut = das_vt_merge(pR->vtElem, nOp, pL->vtElem);

	*ppOut = &(pRes->base);
	return dbsOkay;
}

/* displacement + position.  Claimed HERE, not in form_vec.c, because a
   vector may not learn what a position is.  Stated explicitly rather than
   inferred from commutativity: no hook ever reorders operands, so a legal
   ordering has to be written down. */
static das_binop_stat _DasFormPos_binOpRight(
	const DasForm* pBase, const das_operand* pL, int nOp,
	const das_operand* pR, DasBinOp** ppOut
){
	const DasFormPos* pThis = (const DasFormPos*)pBase;

	if(!DasForm_isKind(pL->pForm, DAS_FORM_VEC)) return dbsDecline;

	if(nOp == D2BOP_SUB)
		return REFUSE("A displacement minus a position is meaningless; write "
		              "position - displacement to move it backward");
	if(nOp != D2BOP_ADD)
		return REFUSE("A displacement and a position can only be added, not "
		              "'%s'", das_op_toStr(nOp, NULL));

	int nComp = 0;
	if(_pos_shapeOk(pL, pR, &nComp) != dbsOkay) return dbsRefuse;

	if(_pos_frameNe(pThis->sFrame, DasFormVector_frame(pL->pForm)))
		return REFUSE("The displacement is in a different frame than the "
		              "position; rotate one first");

	double rScale = 1.0;
	if(pL->units != pR->units){
		if(!Units_canConvert(pL->units, pR->units))
			return REFUSE("Can not move a position in %s by a displacement in "
			              "%s", Units_toStr(pR->units), Units_toStr(pL->units));
		rScale = Units_convertTo(pR->units, 1.0, pL->units);
	}

	DasBinOpPos* pRes = _dasbinoppos_new(
		pL, pR, D2BOP_ADD, nComp,
		DasFormVector_sysType(pL->pForm), DasFormVector_dirs(pL->pForm),
		pThis->uSysType, pThis->aDirs,
		pThis->uSysType, pThis->aDirs, rScale
	);
	pRes->bPosLeft = false;

	pRes->base.units = pR->units;
	pRes->base.pForm = new_DasFormPos(
		pThis->sBody, pThis->sFrame, pThis->sSurface,
		pThis->bFixed, pThis->uSysType, pThis->aDirs
	);
	pRes->base.vtOut = das_vt_merge(pR->vtElem, D2BOP_ADD, pL->vtElem);

	*ppOut = &(pRes->base);
	return dbsOkay;
}

const DasForm_VTbl das_form_pos_vtbl = {
	"position",
	_dasformpos_new,
	_DasFormPos_setParam,
	_DasFormPos_validate,
	_DasFormPos_getParam,
	_DasFormPos_paramName,
	_DasFormPos_encode,
	_DasFormPos_pack,
	_DasFormPos_datumType,
	_DasFormPos_prnRun,
	_DasFormPos_compSym,
	_DasFormPos_binOpLeft,
	_DasFormPos_binOpRight,
	_DasFormPos_copy,
	_DasFormPos_release
};
