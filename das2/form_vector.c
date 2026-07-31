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

/* The free-vector formalism.  See form_vector.h for the parameter set.
 *
 * The reference resolution in _vector_setParam arrived from
 * dataset_hdr3.c's _serial_onOps, where it was sixty lines of one formalism's
 * knowledge sitting in the generic parser.
 *
 * Vector knows about LINEAR, because a vector scaled by a plain number is a
 * vector.  Linear does not know about vector.  form_geoloc.c knows about
 * THIS file, and never the reverse: a difference of two positions is one of
 * these, so geoloc must be able to name a vector as its result.
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
#include "form_linear.h"
#include "form_vector.h"

#ifdef SPICE
#include <SpiceUsr.h>
#endif

#define REFUSE(...) (das_error(DASERR_FORM, __VA_ARGS__), dbsRefuse)

#define VEC_DEG2RAD 0.017453292519943295
#define VEC_RAD2DEG 57.29577951308232

/* ************************************************************************* */
/* Coordinate systems                                                        */

/* EVERY SYSTEM'S CANONICAL ORDER IS RIGHT HANDED.  That is the rule the
   orders were chosen to satisfy, not an accident of which library was called
   first, and it is why they are not uniform:

     cartesian    x,    y,     z          x^ cross y^ = z^
     cylindrical  rho,  phi,   z          rho^ cross phi^ = z^
     spherical    r,    theta, phi        r^ cross theta^ = phi^
     centric      r,    phi,   theta      r^ cross phi^ = theta^
     (geoloc)     phi,  theta, alt        East cross North = Up

   Spherical uses COLATITUDE from +z; centric uses LATITUDE from the equator.
   Because latitude^ = -colatitude^, the angle slots must SWAP between those
   two to stay right handed -- which is exactly what the table does, and is
   the single easiest thing to get wrong in this file.

   A vector pointing along increasing values of component i, crossed with one
   along increasing i+1, therefore always points along increasing i+2.  Client
   code and plotting tools rely on that; do not "regularize" these orders.

   cspice draws the same distinctions (recsph_c vs reclat_c, which disagree
   about both the angle and which slot holds the longitude), so the SPICE
   path and the trig path below agree by construction rather than by luck.
   The authority is the table formerly at geovec.c:34-45. */

/* DEFAULT VALUE PER DIRECTION.  A das stream may send FEWER components than a
   system has, and the missing ones take these.  That is deliberate telemetry
   compression, not sloppiness: a two-axis magnetometer, or a particle detector
   sweep in a frame aligned so one component is always zero, ships what it
   measured and nothing else.  A one-component cartesian vector is legal.

   Zero everywhere except a RADIUS, which defaults to 1 so an angles-only
   vector reads as a unit direction rather than as a null vector.  Positions
   want different defaults; see form_geoloc.c. */
double das_vsys_default(ubyte uSys, int iDir)
{
	if(iDir != 0) return 0.0;

	switch(uSys){
	case DAS_VSYS_CYL:      /* rho */
	case DAS_VSYS_SPH:      /* r   */
	case DAS_VSYS_CENTRIC:  /* r   */
		return 1.0;
	}
	return 0.0;             /* cartesian x, and every non-radial slot */
}

bool das_vsys_toCartTrig(ubyte uSys, const double* pIn, double* pOut)
{
	double r, a1, a2;

	switch(uSys){
	case DAS_VSYS_CART:
		pOut[0] = pIn[0]; pOut[1] = pIn[1]; pOut[2] = pIn[2];
		return true;

	case DAS_VSYS_CYL:                       /* rho, phi, z */
		a1 = pIn[1] * VEC_DEG2RAD;
		pOut[0] = pIn[0] * cos(a1);
		pOut[1] = pIn[0] * sin(a1);
		pOut[2] = pIn[2];
		return true;

	case DAS_VSYS_SPH:                       /* r, colatitude, phi */
		r  = pIn[0];
		a1 = pIn[1] * VEC_DEG2RAD;           /* colatitude from +z */
		a2 = pIn[2] * VEC_DEG2RAD;
		pOut[0] = r * sin(a1) * cos(a2);
		pOut[1] = r * sin(a1) * sin(a2);
		pOut[2] = r * cos(a1);
		return true;

	case DAS_VSYS_CENTRIC:                   /* r, phi, latitude */
		r  = pIn[0];
		a1 = pIn[1] * VEC_DEG2RAD;
		a2 = pIn[2] * VEC_DEG2RAD;           /* latitude from the equator */
		pOut[0] = r * cos(a2) * cos(a1);
		pOut[1] = r * cos(a2) * sin(a1);
		pOut[2] = r * sin(a2);
		return true;
	}

	return das_error(DASERR_FORM,
		"No kernel-free conversion from %s to cartesian; the ellipsoidal "
		"systems need a body and belong to geoloc", das_compsys_str(uSys)
	);
}

bool das_vsys_fromCartTrig(ubyte uSys, const double* pIn, double* pOut)
{
	double x = pIn[0], y = pIn[1], z = pIn[2];
	double r;

	switch(uSys){
	case DAS_VSYS_CART:
		pOut[0] = x; pOut[1] = y; pOut[2] = z;
		return true;

	case DAS_VSYS_CYL:
		pOut[0] = sqrt(x*x + y*y);
		pOut[1] = atan2(y, x) * VEC_RAD2DEG;
		pOut[2] = z;
		return true;

	case DAS_VSYS_SPH:
		r = sqrt(x*x + y*y + z*z);
		pOut[0] = r;
		pOut[1] = (r > 0.0) ? acos(z / r) * VEC_RAD2DEG : 0.0;  /* colatitude */
		pOut[2] = atan2(y, x) * VEC_RAD2DEG;
		return true;

	case DAS_VSYS_CENTRIC:
		r = sqrt(x*x + y*y + z*z);
		pOut[0] = r;
		pOut[1] = atan2(y, x) * VEC_RAD2DEG;
		pOut[2] = (r > 0.0) ? asin(z / r) * VEC_RAD2DEG : 0.0;  /* latitude */
		return true;
	}

	return das_error(DASERR_FORM,
		"No kernel-free conversion from cartesian to %s", das_compsys_str(uSys)
	);
}

/* Prefer cspice when it is linked, so every das tool agrees to the last bit.
   The trig above stays compiled either way; see form_vector.h for why. */

bool das_vsys_toCart(ubyte uSys, const double* pIn, double* pOut)
{
#ifdef SPICE
	switch(uSys){
	case DAS_VSYS_CART:
		pOut[0] = pIn[0]; pOut[1] = pIn[1]; pOut[2] = pIn[2];
		return true;
	case DAS_VSYS_CYL:
		cylrec_c(pIn[0], pIn[1]*VEC_DEG2RAD, pIn[2], pOut);
		return true;
	case DAS_VSYS_SPH:
		sphrec_c(pIn[0], pIn[1]*VEC_DEG2RAD, pIn[2]*VEC_DEG2RAD, pOut);
		return true;
	case DAS_VSYS_CENTRIC:
		latrec_c(pIn[0], pIn[1]*VEC_DEG2RAD, pIn[2]*VEC_DEG2RAD, pOut);
		return true;
	}
#endif
	return das_vsys_toCartTrig(uSys, pIn, pOut);
}

bool das_vsys_fromCart(ubyte uSys, const double* pIn, double* pOut)
{
#ifdef SPICE
	switch(uSys){
	case DAS_VSYS_CART:
		pOut[0] = pIn[0]; pOut[1] = pIn[1]; pOut[2] = pIn[2];
		return true;
	case DAS_VSYS_CYL:
		reccyl_c(pIn, pOut, pOut+1, pOut+2);
		pOut[1] *= VEC_RAD2DEG;
		return true;
	case DAS_VSYS_SPH:
		recsph_c(pIn, pOut, pOut+1, pOut+2);
		pOut[1] *= VEC_RAD2DEG; pOut[2] *= VEC_RAD2DEG;
		return true;
	case DAS_VSYS_CENTRIC:
		reclat_c(pIn, pOut, pOut+1, pOut+2);
		pOut[1] *= VEC_RAD2DEG; pOut[2] *= VEC_RAD2DEG;
		return true;
	}
#endif
	return das_vsys_fromCartTrig(uSys, pIn, pOut);
}

/* ************************************************************************* */
/* The formalism                                                             */

typedef struct das_form_vector {
	DasForm base;

	ubyte uFrameId;    /* CTX_FRAME handle, 0 when frameless */
	ubyte uSysType;    /* DAS_VSYS_*, never an ellipsoidal one */
	ubyte uDirs;       /* VEC_DIRS3 packed, slot -> canonical direction */
} DasFormVector;

static DasForm* _vector_new(void)
{
	DasFormVector* pThis = (DasFormVector*)calloc(1, sizeof(DasFormVector));
	pThis->base.pVTbl = &das_form_vector_vtbl;
	pThis->base.nRef  = 1;
	pThis->uSysType   = DAS_VSYS_CART;
	pThis->uDirs      = VEC_DIRS3(0, 1, 2);
	return &(pThis->base);
}

DasForm* new_DasFormVector(ubyte uFrameId, ubyte uSysType, ubyte uDirs)
{
	if((uSysType == DAS_VSYS_DETIC)||(uSysType == DAS_VSYS_GRAPHIC)){
		das_error(DASERR_FORM,
			"%s is an ellipsoidal system; a free vector has no ellipsoid to "
			"be relative to.  Use <ops kind=\"geoloc\">",
			das_compsys_str(uSysType)
		);
		return NULL;
	}

	DasFormVector* pThis = (DasFormVector*)_vector_new();
	pThis->uFrameId = uFrameId;
	pThis->uSysType = uSysType;
	pThis->uDirs    = uDirs;
	return &(pThis->base);
}

#define VEC_GET(FN, FIELD) \
	ubyte FN(const DasForm* pThis){ \
		if(!DasForm_isVector(pThis)){ \
			das_error(DASERR_FORM, "Not a vector formalism"); \
			return 0; \
		} \
		return ((const DasFormVector*)pThis)->FIELD; \
	}

VEC_GET(DasFormVector_frameId, uFrameId)
VEC_GET(DasFormVector_sysType, uSysType)
VEC_GET(DasFormVector_dirs,    uDirs)

const char* DasFormVector_slotSym(const DasForm* pThis, int iSlot)
{
	if(!DasForm_isVector(pThis)||(iSlot < 0)||(iSlot > 2)) return NULL;

	const DasFormVector* pV = (const DasFormVector*)pThis;
	return das_compsys_symbol(pV->uSysType, (pV->uDirs >> (2*iSlot)) & 0x3);
}

/* --- parameters ---------------------------------------------------------- */

/* "1;0;2": storage slot i holds canonical direction sOrd[2i] */
static DasErrCode _vector_setOrder(DasFormVector* pThis, const char* sOrd)
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
static DasErrCode _vector_setBody(
	DasFormVector* pThis, DasCtxTbl* pTbl, const char* sBody
){
	if(pThis->uFrameId == 0){
		daslog_warn_v(
			"Ignoring body=\"%s\" on a frameless <ops kind=\"vector\">: a body "
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

static DasErrCode _vector_setParam(
	DasForm* pBase, DasCtxTbl* pTbl, const char* sName, const char* sVal
){
	DasFormVector* pThis = (DasFormVector*)pBase;

	if(strcmp(sName, "system") == 0){
		ubyte uSys = das_compsys_id(sVal);
		if(uSys == 0)
			return das_error(DASERR_FORM, "Unknown component system '%s'", sVal);
		if((uSys == DAS_VSYS_DETIC)||(uSys == DAS_VSYS_GRAPHIC))
			return das_error(DASERR_FORM,
				"system=\"%s\" is measured on an ellipsoid, so it describes a "
				"POSITION.  Use <ops kind=\"geoloc\"> with a center= and a "
				"surface=", sVal
			);
		pThis->uSysType = uSys;
		return DAS_OKAY;
	}

	if(strcmp(sName, "sysorder") == 0)
		return _vector_setOrder(pThis, sVal);

	/* The two parameters that mark a POSITION.  Refusing them by name, with
	   the reason, is worth more than a generic unknown-parameter message:
	   this is exactly the mistake an author porting from kind="geovec" makes. */
	if(strcmp(sName, "center") == 0)
		return das_error(DASERR_FORM,
			"center= names an ORIGIN, which makes the values positions rather "
			"than free vectors.  Use <ops kind=\"geoloc\">"
		);

	if(strcmp(sName, "surface") == 0)
		return das_error(DASERR_FORM,
			"surface= names an ellipsoid to measure against, which only a "
			"position needs.  Use <ops kind=\"geoloc\">"
		);

	if((strcmp(sName, "frame") == 0)||(strcmp(sName, "body") == 0)){
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

	if(strcmp(sName, "body") == 0)
		return _vector_setBody(pThis, pTbl, sVal);

	/* A reader that CLAIMS a kind must not skip a misspelling in it.  This is
	   where "sysOrder" gets caught instead of silently producing a wrongly
	   ordered vector. */
	return das_error(DASERR_FORM,
		"<ops kind=\"vector\"> has no parameter '%s'", sName
	);
}

/* TALKATIVE: system= and sysorder= are stated even at their defaults, since
   the schema cannot carry a default behind an anyAttribute. */
static DasErrCode _vector_encode(
	const DasForm* pBase, const DasCtxTbl* pTbl, DasBuf* pBuf
){
	const DasFormVector* pThis = (const DasFormVector*)pBase;

	DasErrCode nRet = DasBuf_puts(pBuf, "<ops kind=\"vector\"");
	if(nRet != DAS_OKAY) return nRet;

	if(pThis->uFrameId != 0){
		const DasCtx* pFrame = (pTbl == NULL) ? NULL
		                     : DasCtxTbl_getOfKind(pTbl, CTX_FRAME, pThis->uFrameId);
		if(pFrame == NULL)
			return das_error(DASERR_FORM,
				"Frame handle %hhu does not resolve; a vector can not be "
				"written without its frame's name", pThis->uFrameId
			);
		if((nRet = DasBuf_printf(pBuf, " frame=\"%s\"", DasCtx_name(pFrame))) != DAS_OKAY)
			return nRet;
	}

	return DasBuf_printf(pBuf, " system=\"%s\" sysorder=\"%d;%d;%d\"/>\n",
		das_compsys_str(pThis->uSysType),
		 pThis->uDirs       & 0x3,
		(pThis->uDirs >> 2) & 0x3,
		(pThis->uDirs >> 4) & 0x3
	);
}

static int _vector_getRefs(const DasForm* pBase, ubyte* pIds, int nMax)
{
	const DasFormVector* pThis = (const DasFormVector*)pBase;
	if((pThis->uFrameId != 0)&&(nMax > 0)){ pIds[0] = pThis->uFrameId; return 1; }
	return 0;
}

/* --- presentation -------------------------------------------------------- */

static char* _vector_prnRun(
	const DasForm* pForm, const ubyte* pRun, uint32_t nElems,
	das_val_type et, char* sBuf, int nLen
){
	const DasFormVector* pThis = (const DasFormVector*)pForm;

	int nUsed = snprintf(sBuf, (size_t)nLen, "%s[",
	                     das_compsys_str(pThis->uSysType));

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

static bool _vector_pack(
	const DasForm* pBase, const das_operand* pOp, const ubyte* pRun,
	das_datum* pOut
){
	if((pOp->nIntRank != 1)||(pOp->aIntShape[0] < 1)||(pOp->aIntShape[0] > 3))
		return das_error(DASERR_FORM,
			"A vector has 1 to 3 components in one level");

	return das_datum_box(
		pOut, pBase, _vector_prnRun, pRun, (size_t)pOp->aIntShape[0],
		pOp->vtElem, pOp->units
	);
}

static das_val_type _vector_datumType(const DasForm* pBase){ return vtComposite; }

int DasFormVector_values(
	const DasForm* pThis, const das_datum* pDm, double* pOut, int nMax
){
	if(!DasForm_isVector(pThis))
		return -1 * das_error(DASERR_FORM, "Not a vector formalism");

	const ubyte* pRun = das_datum_run(pDm);
	int nElems = (int)das_datum_nElems(pDm);
	das_val_type et = das_datum_elemType(pDm);
	if(pRun == NULL)
		return -1 * das_error(DASERR_FORM, "Datum carries no component run");

	/* An absent radial component reads as 1.0 for the curvilinear systems,
	   giving a unit vector rather than a zero one.  Inherited deliberately
	   from das_geovec_values(); it is the difference between a direction with
	   no stated magnitude and a null vector. */
	const DasFormVector* pV = (const DasFormVector*)pThis;
	if(pV->uSysType != DAS_VSYS_CART)
		for(int i = 0; i < nMax; ++i) pOut[i] = (i == 0) ? 1.0 : 0.0;
	else
		for(int i = 0; i < nMax; ++i) pOut[i] = 0.0;

	int n = (nElems < nMax) ? nElems : nMax;
	for(int i = 0; i < n; ++i){
		if(das_value_binXform(et, pRun + i*das_vt_size(et), NULL, vtDouble,
		                      (ubyte*)(pOut + i), NULL, 0) != DAS_OKAY)
			return -1 * das_error(DASERR_FORM, "Bad component %d", i);
	}
	return n;
}

static char* _vector_prnIntr(
	const DasForm* pBase, const DasCtxTbl* pTbl, char* sBuf, int nLen
){
	const DasFormVector* pThis = (const DasFormVector*)pBase;

	const DasCtx* pFrame = ((pTbl == NULL)||(pThis->uFrameId == 0)) ? NULL
	                     : DasCtxTbl_getOfKind(pTbl, CTX_FRAME, pThis->uFrameId);

	snprintf(sBuf, (size_t)nLen, " vector(%s,%s)",
		(pFrame != NULL) ? DasCtx_name(pFrame) : "no frame",
		das_compsys_str(pThis->uSysType)
	);
	return sBuf;
}

static DasForm* _vector_copy(const DasForm* pBase)
{
	DasFormVector* pCopy = (DasFormVector*)calloc(1, sizeof(DasFormVector));
	memcpy(pCopy, pBase, sizeof(DasFormVector));
	pCopy->base.nRef = 1;
	return &(pCopy->base);
}

static void _vector_release(DasForm* pBase){ free(pBase); }


/* ************************************************************************* */
/* The recipe                                                                */

typedef struct das_binop_vector {
	DasBinOp base;

	das_val_type vtL, vtR;
	int    nOp;
	int    nComp;

	bool   bScale;          /* one side is a plain number */
	bool   bVecLeft;
	double rRightScale;     /* unit conversion, addition only */

	/* Storage slot -> canonical direction, per side, so the walk can gather
	   into canonical order before converting. */
	ubyte  aDirL[3];
	ubyte  aDirR[3];
	ubyte  uSysL, uSysR, uSysOut;
	ubyte  aDirOut[3];

	/* CARTESIAN UNION PATH.  Cartesian components are independent, so two
	   partial vectors add slot-wise with no trip through anything, and the
	   result carries the UNION of the directions present.  x-only plus x-only
	   stays one component on the wire; x-only plus y-only becomes two.  That
	   keeps the stream compression the operands were written with.

	   aSrcL[i] is the source slot in the left operand for result slot i, or
	   VEC_ABSENT when that direction is missing and takes its default. */
	bool   bCartUnion;
	ubyte  aSrcL[3];
	ubyte  aSrcR[3];
} DasBinOpVector;

#define VEC_ABSENT 0xFF

static bool _vec_gather(
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

static bool _vector_apply(
	const DasBinOp* pOp, const ubyte* pLRun, const ubyte* pRRun, ubyte* pOutRun
){
	const DasBinOpVector* pThis = (const DasBinOpVector*)pOp;

	size_t uL   = das_vt_size(pThis->vtL);
	size_t uR   = das_vt_size(pThis->vtR);
	size_t uOut = das_vt_size(pOp->vtOut);

	/* --- vector times a plain number ------------------------------------ */
	if(pThis->bScale){
		const ubyte* pVec = pThis->bVecLeft ? pLRun : pRRun;
		const ubyte* pNum = pThis->bVecLeft ? pRRun : pLRun;
		das_val_type vtVec = pThis->bVecLeft ? pThis->vtL : pThis->vtR;
		das_val_type vtNum = pThis->bVecLeft ? pThis->vtR : pThis->vtL;
		size_t uVec = pThis->bVecLeft ? uL : uR;

		/* Scaling is magnitude only, so it acts on the radial component of a
		   curvilinear vector and on every component of a cartesian one.
		   Angles must NOT be scaled -- that would rotate the vector. */
		int nScale = (pThis->uSysL == DAS_VSYS_CART) ? pThis->nComp : 1;

		for(int i = 0; i < pThis->nComp; ++i){
			if(i >= nScale){
				/* an angle: carry it through untouched */
				if(das_value_binXform(vtVec, pVec + i*uVec, NULL, pOp->vtOut,
				                      pOutRun + i*uOut, NULL, 0) != DAS_OKAY)
					return false;
				continue;
			}
			DasErrCode nRet = pThis->bVecLeft
				? das_value_binop(pThis->nOp, vtVec, pVec + i*uVec,
				                  vtNum, pNum, pOp->vtOut, pOutRun + i*uOut)
				: das_value_binop(pThis->nOp, vtNum, pNum,
				                  vtVec, pVec + i*uVec, pOp->vtOut, pOutRun + i*uOut);
			if(nRet != DAS_OKAY) return false;
		}
		return true;
	}

	/* --- cartesian plus cartesian, slot-wise --------------------------- */
	if(pThis->bCartUnion){
		for(int i = 0; i < pThis->nComp; ++i){
			double rL = 0.0, rR = 0.0;   /* every cartesian default is zero */

			if((pThis->aSrcL[i] != VEC_ABSENT)&&
			   (das_value_binXform(pThis->vtL, pLRun + pThis->aSrcL[i]*uL, NULL,
			                       vtDouble, (ubyte*)&rL, NULL, 0) != DAS_OKAY))
				return false;

			if((pThis->aSrcR[i] != VEC_ABSENT)&&
			   (das_value_binXform(pThis->vtR, pRRun + pThis->aSrcR[i]*uR, NULL,
			                       vtDouble, (ubyte*)&rR, NULL, 0) != DAS_OKAY))
				return false;

			rR *= pThis->rRightScale;
			double d = (pThis->nOp == D2BOP_SUB) ? (rL - rR) : (rL + rR);

			if(das_value_binXform(vtDouble, (const ubyte*)&d, NULL, pOp->vtOut,
			                      pOutRun + i*uOut, NULL, 0) != DAS_OKAY)
				return false;
		}
		return true;
	}

	/* --- anything curvilinear: through cartesian and back --------------- */
	double aL[3], aR[3], aLc[3], aRc[3], aSum[3], aOut[3];

	if(!_vec_gather(pLRun, pThis->vtL, uL, pThis->nComp, pThis->aDirL, aL))
		return false;
	if(!_vec_gather(pRRun, pThis->vtR, uR, pThis->nComp, pThis->aDirR, aR))
		return false;

	/* Addition is only defined on cartesian components, so both sides go
	   through cartesian and the result comes back in the LEFT operand's
	   system.  Returning a representation neither operand used would be
	   surprising, and the left side names the result everywhere else. */
	if(!das_vsys_toCart(pThis->uSysL, aL, aLc)) return false;
	if(!das_vsys_toCart(pThis->uSysR, aR, aRc)) return false;

	for(int i = 0; i < 3; ++i){
		double rRight = aRc[i] * pThis->rRightScale;
		aSum[i] = (pThis->nOp == D2BOP_SUB) ? (aLc[i] - rRight)
		                                    : (aLc[i] + rRight);
	}

	if(!das_vsys_fromCart(pThis->uSysOut, aSum, aOut)) return false;

	for(int i = 0; i < pThis->nComp; ++i){
		double d = aOut[pThis->aDirOut[i]];
		if(das_value_binXform(vtDouble, (const ubyte*)&d, NULL, pOp->vtOut,
		                      pOutRun + i*uOut, NULL, 0) != DAS_OKAY)
			return false;
	}
	return true;
}

static void _vector_binop_release(DasBinOp* pOp)
{
	DasForm_decRef(pOp->pForm);
	free(pOp);
}

static const DasBinOp_VTbl g_vtblVector = { _vector_apply, _vector_binop_release };


/* ************************************************************************* */
/* Dispatch                                                                  */

static int _vector_comps(const das_operand* pOp)
{
	if((pOp->nIntRank != 1)||(pOp->aIntShape[0] < 1)||(pOp->aIntShape[0] > 3)){
		das_error(DASERR_FORM, "A vector has 1 to 3 components in one level");
		return 0;
	}
	return (int)pOp->aIntShape[0];
}

static void _vector_unpackDirs(ubyte uDirs, ubyte* pOut)
{
	for(int i = 0; i < 3; ++i) pOut[i] = (uDirs >> (2*i)) & 0x3;
}

/* Vector scaled by a plain number.  Claimed by vector in BOTH orderings,
   because linear may not learn what a vector is. */
static das_binop_stat _vector_scale(
	const das_operand* pVec, const das_operand* pNum, int nOp, bool bVecLeft,
	DasBinOp** ppOut
){
	const DasFormVector* pV = (const DasFormVector*)pVec->pForm;

	if(pNum->nIntRank != 0)
		return REFUSE("Scaling a vector needs a single number, not a run");

	if((nOp != D2BOP_MUL)&&(nOp != D2BOP_DIV))
		return REFUSE("A vector and a plain number combine under '*' or '/', "
		              "not '%s'", das_op_toStr(nOp, NULL));

	if((nOp == D2BOP_DIV)&&!bVecLeft)
		return REFUSE("Dividing a number by a vector is not defined");

	int nComp = _vector_comps(pVec);
	if(nComp == 0) return dbsRefuse;

	DasBinOpVector* pRes = (DasBinOpVector*)calloc(1, sizeof(DasBinOpVector));
	pRes->base.pVTbl = &g_vtblVector;
	pRes->base.nRef  = 1;

	pRes->vtL = bVecLeft ? pVec->vtElem : pNum->vtElem;
	pRes->vtR = bVecLeft ? pNum->vtElem : pVec->vtElem;
	pRes->nOp      = nOp;
	pRes->nComp    = nComp;
	pRes->bScale   = true;
	pRes->bVecLeft = bVecLeft;
	pRes->uSysL    = pV->uSysType;
	pRes->rRightScale = 1.0;

	pRes->base.units = (nOp == D2BOP_MUL)
		? Units_multiply(pVec->units, pNum->units)
		: Units_divide(pVec->units, pNum->units);

	/* Magnitude changes; frame, system and order do not */
	pRes->base.pForm = new_DasFormVector(pV->uFrameId, pV->uSysType, pV->uDirs);
	pRes->base.vtOut        = das_vt_merge(pNum->vtElem, nOp, pVec->vtElem);
	pRes->base.nIntRank     = 1;
	pRes->base.aIntShape[0] = nComp;

	*ppOut = &(pRes->base);
	return dbsOkay;
}

static das_binop_stat _vector_binOpLeft(
	const DasForm* pBase, const das_operand* pL, int nOp,
	const das_operand* pR, const DasCtxTbl* pTbl, DasBinOp** ppOut
){
	const DasFormVector* pThis = (const DasFormVector*)pBase;

	if(DasForm_isLinear(pR->pForm))
		return _vector_scale(pL, pR, nOp, true, ppOut);

	if(!DasForm_isVector(pR->pForm)) return dbsDecline;

	const DasFormVector* pRv = (const DasFormVector*)pR->pForm;

	/* '*' between two vectors is genuinely ambiguous -- dot, cross, or
	   elementwise are three different answers -- so it is refused rather than
	   guessed.  Named products get their own operators when they arrive. */
	if((nOp != D2BOP_ADD)&&(nOp != D2BOP_SUB))
		return REFUSE("'%s' between two vectors is ambiguous (dot? cross?); "
		              "only addition and subtraction are defined",
		              das_op_toStr(nOp, NULL));

	/* THIS is where a frame mismatch fails loud.  Two vectors in different
	   frames have no sum until one is rotated, and adding the components
	   anyway would produce a confident wrong answer. */
	if(pThis->uFrameId != pRv->uFrameId){
		const DasCtx* pA = (pTbl == NULL) ? NULL
		                 : DasCtxTbl_getOfKind(pTbl, CTX_FRAME, pThis->uFrameId);
		const DasCtx* pB = (pTbl == NULL) ? NULL
		                 : DasCtxTbl_getOfKind(pTbl, CTX_FRAME, pRv->uFrameId);
		return REFUSE("Can not combine vectors in different frames, '%s' and "
		              "'%s'; rotate one first",
		              (pA != NULL) ? DasCtx_name(pA) : "(unbound)",
		              (pB != NULL) ? DasCtx_name(pB) : "(unbound)");
	}

	int nCompL = _vector_comps(pL);
	int nCompR = _vector_comps(pR);
	if((nCompL == 0)||(nCompR == 0)) return dbsRefuse;

	/* Cartesian components are independent, so partial vectors add slot-wise
	   and the result carries the UNION of the directions present.  Anything
	   curvilinear must go through cartesian, which is undefined on a partial
	   vector, so those need all three. */
	bool bBothCart = (pThis->uSysType == DAS_VSYS_CART)&&
	                 (pRv->uSysType   == DAS_VSYS_CART);

	if(!bBothCart && ((nCompL != 3)||(nCompR != 3)))
		return REFUSE("Only cartesian vectors may be partial; converting "
		              "between component systems needs all three components, "
		              "have %d and %d", nCompL, nCompR);

	int nComp = (nCompL > nCompR) ? nCompL : nCompR;

	double rRightScale = 1.0;
	if(pL->units != pR->units){
		if(!Units_canConvert(pR->units, pL->units))
			return REFUSE("Can not combine vectors in %s and %s",
			              Units_toStr(pL->units), Units_toStr(pR->units));
		rRightScale = Units_convertTo(pL->units, 1.0, pR->units);
	}

	DasBinOpVector* pRes = (DasBinOpVector*)calloc(1, sizeof(DasBinOpVector));
	pRes->base.pVTbl = &g_vtblVector;
	pRes->base.nRef  = 1;

	pRes->vtL   = pL->vtElem;
	pRes->vtR   = pR->vtElem;
	pRes->nOp   = nOp;
	pRes->nComp = nComp;
	pRes->rRightScale = rRightScale;

	pRes->uSysL   = pThis->uSysType;
	pRes->uSysR   = pRv->uSysType;
	pRes->uSysOut = pThis->uSysType;
	_vector_unpackDirs(pThis->uDirs, pRes->aDirL);
	_vector_unpackDirs(pRv->uDirs,   pRes->aDirR);
	_vector_unpackDirs(pThis->uDirs, pRes->aDirOut);

	ubyte uOutDirs = pThis->uDirs;

	if(bBothCart){
		/* Which canonical directions does each side actually carry? */
		ubyte aFromL[3] = {VEC_ABSENT, VEC_ABSENT, VEC_ABSENT};
		ubyte aFromR[3] = {VEC_ABSENT, VEC_ABSENT, VEC_ABSENT};
		for(int i = 0; i < nCompL; ++i) aFromL[pRes->aDirL[i]] = (ubyte)i;
		for(int i = 0; i < nCompR; ++i) aFromR[pRes->aDirR[i]] = (ubyte)i;

		/* The result carries their UNION, in ascending canonical order.  A
		   direction neither side sends is simply not in the result: adding two
		   x-only vectors keeps one component on the wire rather than padding
		   out to three zeros nobody measured. */
		ubyte aOutDir[3] = {0, 0, 0};
		nComp = 0;
		for(int iDir = 0; iDir < 3; ++iDir){
			if((aFromL[iDir] == VEC_ABSENT)&&(aFromR[iDir] == VEC_ABSENT))
				continue;
			pRes->aSrcL[nComp] = aFromL[iDir];
			pRes->aSrcR[nComp] = aFromR[iDir];
			aOutDir[nComp] = (ubyte)iDir;
			++nComp;
		}
		uOutDirs = VEC_DIRS3(aOutDir[0], aOutDir[1], aOutDir[2]);
		pRes->bCartUnion = true;
		pRes->nComp = nComp;
	}

	pRes->base.units = pL->units;
	pRes->base.pForm = new_DasFormVector(
		pThis->uFrameId, pThis->uSysType, uOutDirs
	);
	pRes->base.vtOut        = das_vt_merge(
		(rRightScale != 1.0) ? vtDouble : pR->vtElem, nOp, pL->vtElem
	);
	pRes->base.nIntRank     = 1;
	pRes->base.aIntShape[0] = nComp;

	*ppOut = &(pRes->base);
	return dbsOkay;
}

/* number * vector.  Claimed HERE rather than in form_linear.c: linear sits at
   the bottom of the knowledge graph and must not learn what a vector is.  The
   operands are NOT reordered, so this hook and the left one are two separately
   stated rules, not one commuted. */
static das_binop_stat _vector_binOpRight(
	const DasForm* pBase, const das_operand* pL, int nOp,
	const das_operand* pR, const DasCtxTbl* pTbl, DasBinOp** ppOut
){
	if(!DasForm_isLinear(pL->pForm)) return dbsDecline;

	return _vector_scale(pR, pL, nOp, false, ppOut);
}

const DasForm_VTbl das_form_vector_vtbl = {
	"vector",
	_vector_new,
	_vector_setParam,
	_vector_encode,
	_vector_getRefs,
	_vector_pack,
	_vector_datumType,
	_vector_prnIntr,
	_vector_binOpLeft,
	_vector_binOpRight,
	_vector_copy,
	_vector_release
};
