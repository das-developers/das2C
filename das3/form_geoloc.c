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
#include "form_vector.h"
#include "form_geoloc.h"

#ifdef SPICE
#include <SpiceUsr.h>
#endif

#define REFUSE(...) (das_error(DASERR_FORM, __VA_ARGS__), dbsRefuse)

/* ************************************************************************* */
/* The formalism                                                             */

/* ************************************************************************* */
/* Component systems: this file's two, plus a superset view over vector's     */

/* These two do NOT share a component order, and the reason is the right-handed
   rule the whole pair of files is built on (see form_vector.c):

     detic     lambda, phi,    h     East cross North = Up
     graphic   phi,    lambda, h     North cross West = Up

   Detic longitude runs EAST, graphic runs WEST.  Held in a common
   (lon, lat, h) order graphic would be West cross North = Down, a left handed
   triad, so graphic puts latitude first instead.  Do not "regularize" the two
   into one order; what comes out is a mirror-image position that looks
   perfectly reasonable on a plot.

   Graphic is west-positive here by definition, not by derivation.  cspice
   decides the sense from the sign of the body's prime-meridian rate and lets a
   kernel override it per body (BODY<id>_PGR_POSITIVE_LON), so an east-positive
   planetographic system is numerically identical to detic -- same reference
   ellipsoid, same latitude definition -- and is spelled "detic".  Carrying a
   second token for it would be two names for one geometry. */
static const char* g_aGeoSym[2][3] = {
	{ "λ",  "φ",  "h"},   /* DAS_VSYS_DETIC   */
	{ "φ",  "λ",  "h"}    /* DAS_VSYS_GRAPHIC */
};

const char* das_geosys_str(ubyte uSys)
{
	switch(uSys & DAS_VSYS_TYPE_MASK){
	case DAS_VSYS_DETIC:   return "detic";
	case DAS_VSYS_GRAPHIC: return "graphic";
	}
	return das_vsys_str(uSys);
}

ubyte das_geosys_id(const char* sSys)
{
	if(sSys == NULL) return DAS_VSYS_UNKNOWN;

	if(strncasecmp(sSys, "detic", 5) == 0) return DAS_VSYS_DETIC;
	if(strncasecmp(sSys, "graph", 5) == 0) return DAS_VSYS_GRAPHIC;

	return das_vsys_id(sSys);
}

const char* das_geosys_desc(ubyte uSys)
{
	switch(uSys & DAS_VSYS_TYPE_MASK){
	case DAS_VSYS_DETIC:
	return "An ellipsoidal coordinate system defined with respect to a "
	       "reference surface. Normals from the surface do not intersect "
	       "the origin except at the equator and poles.  The full "
	       "component set is (λ, φ, h) where 'λ' is the EASTWARD longitude, "
	       "'φ' is the latitude of the surface normal through the point and "
	       "'h' is the distance outside the ellipsoid along that normal. "
	       "All of 'λ', 'φ' and 'h' are assumed to be 0 if absent.";

	case DAS_VSYS_GRAPHIC:
	return "An ellipsoidal coordinate system differing from detic only in "
	       "the sense of the longitude, which runs WESTWARD.  The component "
	       "order differs as well: the full set is (φ, λ, h), latitude "
	       "FIRST, because a westward longitude in (λ, φ, h) order would "
	       "give a left handed triad.  'φ' is the latitude of the surface "
	       "normal through the point, 'λ' is the westward longitude and 'h' "
	       "is the distance outside the ellipsoid along that normal. "
	       "All of 'φ', 'λ' and 'h' are assumed to be 0 if absent.";
	}
	return das_vsys_desc(uSys);
}

const char* das_geosys_symbol(ubyte uSys, int iDir)
{
	int nSys = uSys & DAS_VSYS_TYPE_MASK;

	if((nSys == DAS_VSYS_DETIC)||(nSys == DAS_VSYS_GRAPHIC)){
		if((iDir < 0)||(iDir > 2)) return NULL;
		return g_aGeoSym[nSys - DAS_VSYS_DETIC][iDir];
	}
	return das_vsys_symbol(uSys, iDir);
}

int8_t das_geosys_index(ubyte uSys, const char* sSymbol)
{
	if(sSymbol == NULL) return -1;

	for(int8_t i = 0; i < 3; ++i){
		const char* sHave = das_geosys_symbol(uSys, i);
		if((sHave != NULL)&&(strcasecmp(sSymbol, sHave) == 0)) return i;
	}
	return -1;
}

/* ************************************************************************* */

typedef struct das_form_geoloc {
	DasForm base;

	/* The ORIGIN, spelled body= on the wire because the thing at the center of
	   a position IS a body.  Never "" on a valid form. */
	char sBody[DASFORM_NAME_SZ];
	char sFrame[DASFORM_NAME_SZ];
	char sSurface[DASFORM_NAME_SZ]; /* the ellipsoid, for detic and graphic */
	char sSysOrder[16];
	bool bFixed;
	ubyte uSysType;
	ubyte aDirs[3];

	/* Slots this variable actually uses; see the same field on DasFormVector.
	   0 until validate() is handed the internal shape. */
	ubyte uComps;
} DasFormGeoLoc;

static DasForm* _geoloc_new(void)
{
	DasFormGeoLoc* pThis = (DasFormGeoLoc*)calloc(1, sizeof(DasFormGeoLoc));
	pThis->base.pVTbl = &das_form_geoloc_vtbl;
	pThis->uSysType   = DAS_VSYS_CART;
	pThis->aDirs[0] = 0; pThis->aDirs[1] = 1; pThis->aDirs[2] = 2;
	return &(pThis->base);
}

DasForm* new_DasFormGeoLoc(
	const char* sBody, const char* sFrame, const char* sSurface,
	ubyte uSysType, const ubyte* pDirs
){
	if((sBody == NULL)||(sBody[0] == '\0')){
		das_error(DASERR_FORM,
			"A geoloc needs a center; values with no origin are free vectors "
			"and belong in <ops kind=\"vector\">"
		);
		return NULL;
	}

	DasFormGeoLoc* pThis = (DasFormGeoLoc*)_geoloc_new();
	strncpy(pThis->sBody, sBody, DASFORM_NAME_SZ - 1);
	if(sFrame   != NULL) strncpy(pThis->sFrame,   sFrame,   DASFORM_NAME_SZ - 1);
	if(sSurface != NULL) strncpy(pThis->sSurface, sSurface, DASFORM_NAME_SZ - 1);
	pThis->uSysType  = uSysType;
	if(pDirs != NULL)
		for(int i = 0; i < 3; ++i) pThis->aDirs[i] = pDirs[i];
	return &(pThis->base);
}

#define GL_GET(FN, FIELD) \
	ubyte FN(const DasForm* pThis){ \
		if(!DasForm_isKind(pThis, DAS_FORM_GEOLOC)){ \
			das_error(DASERR_FORM, "Not a geoloc formalism"); \
			return 0; \
		} \
		return ((const DasFormGeoLoc*)pThis)->FIELD; \
	}

#define GL_GETSTR(FN, FIELD) \
	const char* FN(const DasForm* pThis){ \
		if(!DasForm_isKind(pThis, DAS_FORM_GEOLOC)){ \
			das_error(DASERR_FORM, "Not a geoloc formalism"); \
			return NULL; \
		} \
		const char* s = ((const DasFormGeoLoc*)pThis)->FIELD; \
		return (s[0] == '\0') ? NULL : s; \
	}

GL_GETSTR(DasFormGeoLoc_body,    sBody)
GL_GETSTR(DasFormGeoLoc_frame,   sFrame)
GL_GETSTR(DasFormGeoLoc_surface, sSurface)
GL_GET(DasFormGeoLoc_sysType,  uSysType)

const ubyte* DasFormGeoLoc_dirs(const DasForm* pThis)
{
	if(!DasForm_isKind(pThis, DAS_FORM_GEOLOC)){
		das_error(DASERR_FORM, "Not a geoloc formalism");
		return NULL;
	}
	return ((const DasFormGeoLoc*)pThis)->aDirs;
}

static const char* _geoloc_compSym(const DasForm* pThis, int iComp)
{
	if(!DasForm_isKind(pThis, DAS_FORM_GEOLOC)||(iComp < 0)) return NULL;

	/* Bounded by uComps, for the reason form_vector.c gives. */
	const DasFormGeoLoc* pG = (const DasFormGeoLoc*)pThis;
	if(iComp >= pG->uComps) return NULL;

	return das_geosys_symbol(pG->uSysType, pG->aDirs[iComp]);
}

/* --- parameters ---------------------------------------------------------- */

static DasErrCode _geoloc_setOrder(DasFormGeoLoc* pThis, const char* sOrd)
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

static DasErrCode _geoloc_setParam(
	DasForm* pBase, const char* sName, const char* sVal
){
	DasFormGeoLoc* pThis = (DasFormGeoLoc*)pBase;

	if(strcmp(sName, "system") == 0){
		pThis->uSysType = das_geosys_id(sVal);
		if(pThis->uSysType == 0)
			return das_error(DASERR_FORM, "Unknown component system '%s'", sVal);
		return DAS_OKAY;
	}

	if(strcmp(sName, "sysorder") == 0)
		return _geoloc_setOrder(pThis, sVal);

	/* body= is what makes this a position rather than a vector: it names the
	   ORIGIN the values are measured from.  A frame also has a body -- what it
	   is fixed to -- and the two differ for Cassini-relative-to-Saturn in
	   IAU_JUPITER.  A position only ever needs the origin, so this file spends
	   the name on that and lets a reader ask SPICE what a frame is fixed to. */
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
		"<ops kind=\"geoloc\"> has no parameter '%s'", sName
	);
}

static DasErrCode _geoloc_encode(
	const DasForm* pBase, DasBuf* pBuf
){
	const DasFormGeoLoc* pThis = (const DasFormGeoLoc*)pBase;

	if(pThis->sBody[0] == '\0')
		return das_error(DASERR_FORM,
			"A geoloc can not be written without its body");

	DasErrCode nRet = DasBuf_printf(pBuf,
		"      <ops kind=\"geoloc\" body=\"%s\"", pThis->sBody);
	if(nRet != DAS_OKAY) return nRet;

	if(pThis->sFrame[0] != '\0'){
		if((nRet = DasBuf_printf(pBuf, " frame=\"%s\"", pThis->sFrame)) != DAS_OKAY)
			return nRet;
	}
	if(pThis->sSurface[0] != '\0'){
		if((nRet = DasBuf_printf(pBuf, " surface=\"%s\"", pThis->sSurface)) != DAS_OKAY)
			return nRet;
	}
	/* body= is NOT repeated here.  A vector's body is optional and gets a
	   conditional block; a geoloc's is mandatory and already went out with the
	   element name above.  Writing it twice is a duplicate XML attribute, which
	   expat rejects -- das3_text was emitting geoloc streams it could not read
	   back. */
	if(pThis->bFixed){
		if((nRet = DasBuf_puts(pBuf, " fixed=\"true\"")) != DAS_OKAY) return nRet;
	}

	/* cartesian is the default, so naming it says nothing.  Same reasoning
	   as the ascending sysorder below: emit a parameter only when it
	   differs from what a reader would assume. */
	if(pThis->uSysType != DAS_VSYS_CART){
		if((nRet = DasBuf_printf(pBuf, " system=\"%s\"",
		                         das_geosys_str(pThis->uSysType))) != DAS_OKAY)
			return nRet;
	}

	if((nRet = _das_form_prnOrder(pBuf, pThis->aDirs, pThis->uComps)) != DAS_OKAY)
		return nRet;

	return DasBuf_puts(pBuf, "/>\n");
}

/* A NULL from DasFormVector_frame() means frameless; "" means the same
   thing on this side, so normalize before comparing. */
static bool _geoloc_frameNe(const char* sMine, const char* sTheirs)
{
	if(sMine   == NULL) sMine   = "";
	if(sTheirs == NULL) sTheirs = "";
	return (strcasecmp(sMine, sTheirs) != 0);
}

/* body= is the ONE parameter a geoloc cannot do without: it names the origin,
   and a position with no origin is a free vector.  new_DasFormGeoLoc() refuses
   an empty one, but the wire path never goes through that constructor, so
   before this slot existed a stream could declare <ops kind="geoloc"
   system="detic"/> and get an originless position that failed much later, at
   write time, or not at all. */
static DasErrCode _geoloc_validate(
	DasForm* pBase, int nIntRank, const ptrdiff_t* pIntShape
){
	DasFormGeoLoc* pThis = (DasFormGeoLoc*)pBase;

	if(pThis->sBody[0] == '\0')
		return das_error(DASERR_FORM,
			"<ops kind=\"geoloc\"> has no body=.  A position is measured FROM "
			"somewhere; values with no origin are free vectors and belong in "
			"<ops kind=\"vector\">"
		);

	/* An ellipsoidal system measures against a reference surface, so naming
	   one is not decoration. */
	if(das_geosys_isEllipsoidal(pThis->uSysType)&&(pThis->sSurface[0] == '\0'))
		return das_error(DASERR_FORM,
			"system=\"%s\" is measured on an ellipsoid, so it needs a surface=",
			das_geosys_str(pThis->uSysType)
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
	return DAS_OKAY;
}

static const char* _geoloc_getParam(
	const DasForm* pBase, const char* sName, ubyte* pType
)
{
	const DasFormGeoLoc* pThis = (const DasFormGeoLoc*)pBase;

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
		return das_geosys_str(pThis->uSysType);
	}
	if(strcmp(sName, "sysorder") == 0){
		if(pType != NULL) *pType = DASPROP_STRING | DASPROP_SINGLE;
		return (pThis->sSysOrder[0] == '\0') ? "0;1;2" : pThis->sSysOrder;
	}
	if(strcmp(sName, "fixed") == 0){
		if(pType != NULL) *pType = DASPROP_BOOL | DASPROP_SINGLE;
		return pThis->bFixed ? "true" : "false";
	}

	return NULL;
}

/* --- presentation -------------------------------------------------------- */

static char* _geoloc_prnRun(
	const DasForm* pForm, const ubyte* pRun, uint32_t nElems,
	das_val_type et, char* sBuf, int nLen
){
	const DasFormGeoLoc* pThis = (const DasFormGeoLoc*)pForm;

	int nUsed = snprintf(sBuf, (size_t)nLen, "%s@%s[",
	                     das_geosys_str(pThis->uSysType), pThis->sBody);

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
		return das_error_false(DASERR_FORM,
			"A position has 1 to 3 components in one level"
		);

	return das_datum_box(
		pOut, pBase, pRun, pOp->nIntRank, pOp->aIntShape, pOp->vtElem,
		pOp->units
	);
}

static das_val_type _geoloc_datumType(const DasForm* pBase){ return vtComposite; }


static DasForm* _geoloc_copy(const DasForm* pBase)
{
	DasFormGeoLoc* pCopy = (DasFormGeoLoc*)calloc(1, sizeof(DasFormGeoLoc));
	memcpy(pCopy, pBase, sizeof(DasFormGeoLoc));
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
	del_DasForm(pOp->pForm);
	free(pOp);
}

static const DasBinOp_VTbl g_vtblGeoLoc = { _geoloc_apply, _geoloc_binop_release };


/* ************************************************************************* */
/* Dispatch                                                                  */

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
	ubyte uSysL, const ubyte* pDirL, ubyte uSysR, const ubyte* pDirR,
	ubyte uSysOut, const ubyte* pDirOut, double rScale
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
	for(int i = 0; i < 3; ++i){
		pRes->aDirL[i]   = pDirL[i];
		pRes->aDirR[i]   = pDirR[i];
		pRes->aDirOut[i] = pDirOut[i];
	}

	pRes->base.nIntRank     = 1;
	pRes->base.aIntShape[0] = nComp;
	return pRes;
}

static das_binop_stat _geoloc_binOpLeft(
	const DasForm* pBase, const das_operand* pL, int nOp,
	const das_operand* pR, DasBinOp** ppOut
){
	const DasFormGeoLoc* pThis = (const DasFormGeoLoc*)pBase;

	bool bRightIsPos = DasForm_isKind(pR->pForm, DAS_FORM_GEOLOC);
	if(!bRightIsPos && !DasForm_isKind(pR->pForm, DAS_FORM_VEC))
		return dbsDecline;

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

		DasBinOpGeoLoc* pRes = _geoloc_recipe(
			pL, pR, D2BOP_SUB, nComp,
			pThis->uSysType, pThis->aDirs, pRg->uSysType, pRg->aDirs,
			pThis->uSysType, pThis->aDirs, 1.0
		);
		pRes->bTwoPoints = true;

		/* The DIFFERENCE of two positions is a free vector -- no origin, and
		   therefore no longer a geoloc.  This is the 3-D image of
		   point - point = interval. */
		pRes->base.units = pL->units;
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

	if(_geoloc_frameNe(pThis->sFrame, DasFormVector_frame(pVf)))
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
		pThis->uSysType, pThis->aDirs,
		DasFormVector_sysType(pVf), DasFormVector_dirs(pVf),
		pThis->uSysType, pThis->aDirs, rScale
	);
	pRes->bPosLeft = true;

	/* Still a position on the same body, in the same frame and system */
	pRes->base.units = pL->units;
	pRes->base.pForm = new_DasFormGeoLoc(
		pThis->sBody, pThis->sFrame, pThis->sSurface,
		pThis->uSysType, pThis->aDirs
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
	const das_operand* pR, DasBinOp** ppOut
){
	const DasFormGeoLoc* pThis = (const DasFormGeoLoc*)pBase;

	if(!DasForm_isKind(pL->pForm, DAS_FORM_VEC)) return dbsDecline;

	if(nOp == D2BOP_SUB)
		return REFUSE("A displacement minus a position is meaningless; write "
		              "position - displacement to move it backward");
	if(nOp != D2BOP_ADD)
		return REFUSE("A displacement and a position can only be added, not "
		              "'%s'", das_op_toStr(nOp, NULL));

	int nComp = 0;
	if(_geoloc_shapeOk(pL, pR, &nComp) != dbsOkay) return dbsRefuse;

	if(_geoloc_frameNe(pThis->sFrame, DasFormVector_frame(pL->pForm)))
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
		pThis->uSysType, pThis->aDirs,
		pThis->uSysType, pThis->aDirs, rScale
	);
	pRes->bPosLeft = false;

	pRes->base.units = pR->units;
	pRes->base.pForm = new_DasFormGeoLoc(
		pThis->sBody, pThis->sFrame, pThis->sSurface,
		pThis->uSysType, pThis->aDirs
	);
	pRes->base.vtOut = das_vt_merge(pR->vtElem, D2BOP_ADD, pL->vtElem);

	*ppOut = &(pRes->base);
	return dbsOkay;
}

const DasForm_VTbl das_form_geoloc_vtbl = {
	"geoloc",
	_geoloc_new,
	_geoloc_setParam,
	_geoloc_validate,
	_geoloc_getParam,
	_geoloc_encode,
	_geoloc_pack,
	_geoloc_datumType,
	_geoloc_prnRun,
	_geoloc_compSym,
	_geoloc_binOpLeft,
	_geoloc_binOpRight,
	_geoloc_copy,
	_geoloc_release
};
