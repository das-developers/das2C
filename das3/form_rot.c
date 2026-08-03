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

/* The rotation formalism.
 *
 * Written before form.c exists, as the top-down spec.  Rotation is the worst
 * case on every axis: it changes the item shape (3;3 times 3 gives 3), it
 * changes a context binding (the result is in a different frame), it does not
 * commute, and it has to reject a wrong partner.  So whatever this file needs
 * is what DasForm has to provide.
 *
 * Note what is NOT included: variable.h.  A form is handed das_operand
 * snapshots and never reaches up.  Nor generator.h -- forms compute,
 * variables walk.
 *
 * Wire:
 *
 *   <composite semantic="real" intern="3;3" index="*">
 *     <ops kind="rotation" from="TS2_TSCS" to="GEI2000"/>
 *     <packet numItems="9" itemBytes="8" encoding="LEreal"/>
 *   </composite>
 *
 * Defined pairings, all under D2BOP_MUL:
 *
 *   rotation * geovec    -> geovec,   the frame moves from= to to=
 *   rotation * rotation  -> rotation, R2 * R1 composes right to left
 *
 * Absent on purpose: vector * rotation.  It is not defined, no hook claims
 * it, and it therefore fails loud instead of quietly commuting into something
 * that looks like an answer.
 */

#define _POSIX_C_SOURCE 200112L

#include <string.h>
#ifndef _WIN32
#include <strings.h>
#else
#define strcasecmp _stricmp
#endif
#include <stdio.h>
#include <stdlib.h>

#include "util.h"
#include "value.h"
#include "units.h"
#include "operator.h"
#include "buffer.h"

#include "form_vector.h"
#include "form.h"
#include "form_vector.h"
#include "form_rot.h"

/* Refusing is not declining.  A decline is silent and means "ask the other
   operand"; a refusal means "this pairing is mine and it is illegal", and the
   message has to name the actual problem rather than the generic miss. */
#define REFUSE(...) (das_error(DASERR_FORM, __VA_ARGS__), dbsRefuse)

/* Local sugar over das_value_binXform(), the library's range checked cast.  A
   contraction is not element-wise, so it cannot use a shared run kernel; what
   it needs is only to get values into and out of double.  No new element API. */
static bool _toDbl(das_val_type vt, const ubyte* p, double* pOut)
{
	return das_value_binXform(
		vt, p, NULL, vtDouble, (ubyte*)pOut, NULL, 0
	) == DAS_OKAY;
}

static bool _fromDbl(das_val_type vt, double d, ubyte* pOut)
{
	return das_value_binXform(
		vtDouble, (const ubyte*)&d, NULL, vt, pOut, NULL, 0
	) == DAS_OKAY;
}

/* ************************************************************************* */
/* The formalism                                                             */

/* The LAYOUT is not stored here.  A matrix rotation is intern="3;3" and a
   quaternion is intern="4"; both are the operand's declared shape, which
   arrives in the das_operand.  Storing it twice is how the two drift. */
typedef struct das_form_rotate {
	DasForm base;

	char sFrom[DASFORM_NAME_SZ];   /* "" until bound */
	char sTo[DASFORM_NAME_SZ];
} DasFormRotate;

int DasFormRotate_layout(const das_operand* pOp)
{
	if((pOp->nIntRank == 2)&&(pOp->aIntShape[0] == 3)&&(pOp->aIntShape[1] == 3))
		return ROT_MATRIX;
	if((pOp->nIntRank == 1)&&(pOp->aIntShape[0] == 4))
		return ROT_QUAT;

	das_error(DASERR_FORM, "A rotation is intern=\"3;3\" or intern=\"4\"");
	return 0;
}

/* Bindings compare by HANDLE, so the math never needs a name.  A name is
   wanted only to serialize or to explain a refusal, and the table arrives as
   an argument from whoever is doing that. */
static const char* _rot_frameName(const char* sFrame)
{
	return ((sFrame == NULL)||(sFrame[0] == '\0')) ? "(unbound)" : sFrame;
}

/* ------------------------------------------------------------------------- */

static DasForm* _rot_new(void)
{
	DasFormRotate* pThis = (DasFormRotate*)calloc(1, sizeof(DasFormRotate));
	pThis->base.pVTbl   = &das_form_rotate_vtbl;
	pThis->base.nRef = 1;
	return &(pThis->base);
}

DasForm* new_DasFormRotate(const char* sFrom, const char* sTo)
{
	DasFormRotate* pThis = (DasFormRotate*)_rot_new();
	if(sFrom != NULL) strncpy(pThis->sFrom, sFrom, DASFORM_NAME_SZ - 1);
	if(sTo   != NULL) strncpy(pThis->sTo,   sTo,   DASFORM_NAME_SZ - 1);
	return &(pThis->base);
}

const char* DasFormRotate_from(const DasForm* pThis)
{
	if(!DasForm_isRotate(pThis)){
		das_error(DASERR_FORM, "Not a rotation formalism");
		return NULL;
	}
	return ((const DasFormRotate*)pThis)->sFrom;
}

const char* DasFormRotate_to(const DasForm* pThis)
{
	if(!DasForm_isRotate(pThis)){
		das_error(DASERR_FORM, "Not a rotation formalism");
		return NULL;
	}
	return ((const DasFormRotate*)pThis)->sTo;
}

static DasErrCode _rot_setParam(
	DasForm* pBase, const char* sName, const char* sVal
){
	DasFormRotate* pThis = (DasFormRotate*)pBase;

	char* sDest = NULL;
	if(strcmp(sName, "from") == 0)     sDest = pThis->sFrom;
	else if(strcmp(sName, "to") == 0)  sDest = pThis->sTo;
	else return das_error(DASERR_FORM,
		"<ops kind=\"rotation\"> has no parameter '%s'", sName
	);

	strncpy(sDest, sVal, DASFORM_NAME_SZ - 1);
	return DAS_OKAY;
}

static DasErrCode _rot_encode(
	const DasForm* pBase, DasBuf* pBuf
){
	const DasFormRotate* pThis = (const DasFormRotate*)pBase;

	if((pThis->sFrom[0] == '\0')||(pThis->sTo[0] == '\0'))
		return das_error(DASERR_FORM,
			"A rotation can not be written without both of its frames"
		);

	return DasBuf_printf(pBuf,
		"      <ops kind=\"rotation\" from=\"%s\" to=\"%s\"/>\n",
		_rot_frameName(pThis->sFrom), _rot_frameName(pThis->sTo)
	);
}

/* A rotation is 9 values (a 3;3 matrix) or 4 (a quaternion).  Nothing else is
   a rotation, and the shape is the only thing that tells them apart -- which
   is exactly why this check needs the variable's shape and cannot live in the
   factory. */
static DasErrCode _rot_validate(
	DasForm* pBase, int nIntRank, const ptrdiff_t* pIntShape
){
	const DasFormRotate* pThis = (const DasFormRotate*)pBase;

	if((pThis->sFrom[0] == '\0')||(pThis->sTo[0] == '\0'))
		return das_error(DASERR_FORM,
			"<ops kind=\"rotation\"> needs both from= and to=; a rotation that "
			"does not say which frames it maps between cannot be applied"
		);

	size_t uElems = 1;
	for(int i = 0; i < nIntRank; ++i){
		if(pIntShape[i] < 1) return DAS_OKAY;   /* ragged: not checkable here */
		uElems *= (size_t)pIntShape[i];
	}

	if((uElems != ROT_MATRIX)&&(uElems != ROT_QUAT))
		return das_error(DASERR_FORM,
			"A rotation is %d values as a matrix or %d as a quaternion, not %zu",
			ROT_MATRIX, ROT_QUAT, uElems
		);

	return DAS_OKAY;
}

static const char* _rot_getParam(
	const DasForm* pBase, const char* sName, ubyte* pType
)
{
	const DasFormRotate* pThis = (const DasFormRotate*)pBase;

	if(strcmp(sName, "from") == 0){
		if(pType != NULL) *pType = DASPROP_STRING | DASPROP_SINGLE;
		return (pThis->sFrom[0] == '\0') ? NULL : pThis->sFrom;
	}
	if(strcmp(sName, "to") == 0){
		if(pType != NULL) *pType = DASPROP_STRING | DASPROP_SINGLE;
		return (pThis->sTo[0] == '\0') ? NULL : pThis->sTo;
	}

	return NULL;
}

/* No new das_val_type.  A rotation datum is the generic box: a form pointer
   plus the run it describes.  If every formalism had to add a das_val_type,
   adding one would touch value.c, datum.c and thirty switch sites instead of
   nothing, and this layer would have failed here first. */
/* How a rotation datum renders.  Handed to das_datum_box so datum.c can print
   one without ever learning what a rotation is. */
static char* _rot_prnRun(
	const DasForm* pForm, const ubyte* pRun, uint32_t nElems,
	das_val_type et, char* sBuf, int nLen
){
	int nUsed = snprintf(sBuf, (size_t)nLen, "rotation[");
	for(uint32_t u = 0; (u < nElems)&&(nUsed < nLen - 2); ++u){
		double d;
		if(!_toDbl(et, pRun + u*das_vt_size(et), &d)) break;
		nUsed += snprintf(sBuf + nUsed, (size_t)(nLen - nUsed), "%s%.6g",
		                  (u > 0) ? " " : "", d);
	}
	if(nUsed < nLen - 1) snprintf(sBuf + nUsed, (size_t)(nLen - nUsed), "]");
	return sBuf;
}

static bool _rot_pack(
	const DasForm* pBase, const das_operand* pOp, const ubyte* pRun,
	das_datum* pOut
){
	int nElems = DasFormRotate_layout(pOp);
	if(nElems == 0) return false;

	/* The layout call already settled which of the two shapes this is, so the
	   operand's declared extents are not consulted again here. */
	ptrdiff_t aShape[2];
	int nRank;
	if(nElems == ROT_MATRIX){ nRank = 2; aShape[0] = 3; aShape[1] = 3; }
	else                    { nRank = 1; aShape[0] = nElems; }

	return das_datum_box(
		pOut, pBase, pRun, nRank, aShape, pOp->vtElem, pOp->units
	);
}

static das_val_type _rot_datumType(const DasForm* pBase){ return vtComposite; }

static char* _rot_prnIntr(
	const DasForm* pBase, char* sBuf, int nLen
){
	const DasFormRotate* pThis = (const DasFormRotate*)pBase;

	snprintf(sBuf, (size_t)nLen, " rotation(%s->%s)",
		_rot_frameName(pThis->sFrom), _rot_frameName(pThis->sTo)
	);
	return sBuf;
}

static DasForm* _rot_copy(const DasForm* pBase)
{
	DasFormRotate* pCopy = (DasFormRotate*)calloc(1, sizeof(DasFormRotate));
	memcpy(pCopy, pBase, sizeof(DasFormRotate));
	pCopy->base.nRef = 1;
	return &(pCopy->base);
}

static void _rot_release(DasForm* pBase){ free(pBase); }


/* ************************************************************************* */
/* Recipe: rotation * geovec -> geovec                                       */

typedef struct das_binop_rotvec {
	DasBinOp base;

	das_val_type vtRot;      /* how each side stores its cells; the result */
	das_val_type vtVec;      /* type is base.vtOut                        */

	/* storage slot -> canonical direction, for the operand vector.  The
	   result is written canonical, which is why the result form's dirs are
	   0;1;2 rather than the operand's order. */
	ubyte aVecDirs[3];
} DasBinOpRotVec;

static bool _rotvec_apply(
	const DasBinOp* pOp, const ubyte* pLRun, const ubyte* pRRun, ubyte* pOutRun
){
	const DasBinOpRotVec* pThis = (const DasBinOpRotVec*)pOp;

	size_t uRotSz = das_vt_size(pThis->vtRot);
	size_t uVecSz = das_vt_size(pThis->vtVec);
	size_t uOutSz = das_vt_size(pOp->vtOut);

	double R[9], v[3];

	for(int i = 0; i < 9; ++i)
		if(!_toDbl(pThis->vtRot, pLRun + i*uRotSz, R + i))
			return false;

	/* gather to canonical x,y,z: storage slot i holds direction aVecDirs[i] */
	for(int i = 0; i < 3; ++i)
		if(!_toDbl(pThis->vtVec, pRRun + i*uVecSz, v + pThis->aVecDirs[i]))
			return false;

	for(int r = 0; r < 3; ++r){
		double d = R[r*3 + 0]*v[0] + R[r*3 + 1]*v[1] + R[r*3 + 2]*v[2];
		if(!_fromDbl(pOp->vtOut, d, pOutRun + r*uOutSz))
			return false;
	}
	return true;
}

static void _rotvec_release(DasBinOp* pOp)
{
	DasForm_decRef(pOp->pForm);
	free(pOp);
}

static const DasBinOp_VTbl g_vtblRotVec = { _rotvec_apply, _rotvec_release };

static das_binop_stat _rot_onVec(
	const DasFormRotate* pThis, const das_operand* pRot,
	const das_operand* pVec,    DasBinOp** ppOut
){
	if(DasFormRotate_layout(pRot) != ROT_MATRIX)
		return REFUSE("Applying a quaternion to a vector is not implemented; "
		              "supply the rotation as a 3;3 matrix");

	ubyte uSys = DasFormVector_sysType(pVec->pForm);
	if(uSys != DAS_VSYS_CART)
		return REFUSE("A rotation matrix acts on cartesian components, the "
		              "vector is stored as %s", das_vsys_str(uSys));

	if((pVec->nIntRank != 1)||(pVec->aIntShape[0] != 3))
		return REFUSE("A 3;3 rotation needs a three component vector");

	/* THIS is where a frame mismatch fails loud, and it is why the recipe is
	   resolved from the formalisms rather than guessed at by the walker. */
	const char* sVecFrame = DasFormVector_frame(pVec->pForm);
	if(strcasecmp(_rot_frameName(pThis->sFrom), _rot_frameName(sVecFrame)) != 0)
		return REFUSE("This rotation starts in frame '%s', the vector is in '%s'",
			_rot_frameName(pThis->sFrom), _rot_frameName(sVecFrame)
		);

	if(!Units_canMerge(pRot->units, D2BOP_MUL, pVec->units))
		return REFUSE("Can not multiply units '%s' by '%s'",
		              Units_toStr(pRot->units), Units_toStr(pVec->units));

	if(pRot->units != UNIT_DIMENSIONLESS)
		return REFUSE("A rotation matrix is dimensionless, this one is in '%s'",
		              Units_toStr(pRot->units));

	DasBinOpRotVec* pRes = (DasBinOpRotVec*)calloc(1, sizeof(DasBinOpRotVec));
	pRes->base.pVTbl   = &g_vtblRotVec;
	pRes->base.nRef = 1;

	pRes->vtRot = pRot->vtElem;
	pRes->vtVec = pVec->vtElem;

	ubyte uDirs = DasFormVector_dirs(pVec->pForm);
	for(int i = 0; i < 3; ++i) pRes->aVecDirs[i] = (uDirs >> (2*i)) & 0x3;

	/* Everything the walker will read, settled once, right here. */
	pRes->base.units        = Units_multiply(pRot->units, pVec->units);
	pRes->base.vtOut        = das_vt_merge(pVec->vtElem, D2BOP_MUL, pRot->vtElem);
	pRes->base.nIntRank     = 1;
	pRes->base.aIntShape[0] = 3;
	pRes->base.pForm = new_DasFormVector(
		pThis->sTo, DAS_VSYS_CART, VEC_DIRS3(0, 1, 2)
	);

	*ppOut = &(pRes->base);
	return dbsOkay;
}


/* ************************************************************************* */
/* Recipe: rotation * rotation -> rotation                                   */

typedef struct das_binop_rotrot {
	DasBinOp base;
	das_val_type vtL;
	das_val_type vtR;
} DasBinOpRotRot;

static bool _rotrot_apply(
	const DasBinOp* pOp, const ubyte* pLRun, const ubyte* pRRun, ubyte* pOutRun
){
	const DasBinOpRotRot* pThis = (const DasBinOpRotRot*)pOp;

	size_t uLSz   = das_vt_size(pThis->vtL);
	size_t uRSz   = das_vt_size(pThis->vtR);
	size_t uOutSz = das_vt_size(pOp->vtOut);

	double L[9], R[9];
	for(int i = 0; i < 9; ++i){
		if(!_toDbl(pThis->vtL, pLRun + i*uLSz, L + i)) return false;
		if(!_toDbl(pThis->vtR, pRRun + i*uRSz, R + i)) return false;
	}

	for(int r = 0; r < 3; ++r){
		for(int c = 0; c < 3; ++c){
			double d = L[r*3 + 0]*R[0*3 + c]
			         + L[r*3 + 1]*R[1*3 + c]
			         + L[r*3 + 2]*R[2*3 + c];
			if(!_fromDbl(pOp->vtOut, d, pOutRun + (r*3 + c)*uOutSz))
				return false;
		}
	}
	return true;
}

static void _rotrot_release(DasBinOp* pOp)
{
	DasForm_decRef(pOp->pForm);
	free(pOp);
}

static const DasBinOp_VTbl g_vtblRotRot = { _rotrot_apply, _rotrot_release };

static das_binop_stat _rot_onRot(
	const DasFormRotate* pThis, const das_operand* pL,
	const das_operand* pR,      DasBinOp** ppOut
){
	const DasFormRotate* pRight = (const DasFormRotate*)pR->pForm;

	if((DasFormRotate_layout(pL) != ROT_MATRIX)||(DasFormRotate_layout(pR) != ROT_MATRIX))
		return REFUSE("Composing quaternions is not implemented; supply both "
		              "rotations as 3;3 matrices");

	/* R2 * R1 reads right to left: R1 runs first, so R1's destination has to
	   be R2's origin. */
	if(strcasecmp(pThis->sFrom, pRight->sTo) != 0)
		return REFUSE("Can not compose: the right rotation lands in '%s' but "
		              "the left one starts in '%s'",
			_rot_frameName(pRight->sTo),
			_rot_frameName(pThis->sFrom)
		);

	DasBinOpRotRot* pRes = (DasBinOpRotRot*)calloc(1, sizeof(DasBinOpRotRot));
	pRes->base.pVTbl   = &g_vtblRotRot;
	pRes->base.nRef = 1;

	pRes->vtL = pL->vtElem;
	pRes->vtR = pR->vtElem;

	pRes->base.units        = UNIT_DIMENSIONLESS;
	pRes->base.vtOut        = das_vt_merge(pR->vtElem, D2BOP_MUL, pL->vtElem);
	pRes->base.nIntRank     = 2;
	pRes->base.aIntShape[0] = 3;
	pRes->base.aIntShape[1] = 3;
	pRes->base.pForm = new_DasFormRotate(pRight->sFrom, pThis->sTo);

	*ppOut = &(pRes->base);
	return dbsOkay;
}


/* ************************************************************************* */
/* Dispatch                                                                  */

static das_binop_stat _rot_binOpLeft(
	const DasForm* pBase, const das_operand* pL, int nOp,
	const das_operand* pR, DasBinOp** ppOut
){
	const DasFormRotate* pThis = (const DasFormRotate*)pBase;

	/* Adding rotations is meaningless, not merely unimplemented */
	if(nOp != D2BOP_MUL) return dbsDecline;

	if(pR->pForm->pVTbl == &das_form_vector_vtbl)
		return _rot_onVec(pThis, pL, pR, ppOut);

	if(DasForm_isRotate(pR->pForm))
		return _rot_onRot(pThis, pL, pR, ppOut);

	return dbsDecline;
}

/* No binOpRight.  Nothing is defined with a rotation on the RIGHT: v * R is
   not R * v, and a NULL slot is how that stays true.  If geovec's binOpLeft
   declines and this hook does not exist, new_DasVarBin fails loud, which is
   the correct answer. */

const DasForm_VTbl das_form_rotate_vtbl = {
	"rotation",
	_rot_new,
	_rot_setParam,
	_rot_validate,
	_rot_getParam,
	_rot_encode,
	_rot_pack,
	_rot_datumType,
	_rot_prnIntr,
	_rot_prnRun,
	_rot_binOpLeft,
	NULL,             /* binOpRight */
	_rot_copy,
	_rot_release
};
