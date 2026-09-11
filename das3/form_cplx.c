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

/* The complex formalism.  See form_cplx.h for the rule set.
 *
 * Complex knows about LINEAR, because a real number entering complex
 * arithmetic promotes to a zero-imaginary complex and the answer is still
 * complex.  Linear does not know about complex.  That is the knowledge arrow,
 * and it is why "real * complex" is claimed here through binOpRight rather
 * than over in form_linear.c.
 *
 * The promotion is what keeps this file small.  form_vector.c needs a separate
 * scale path because a vector times a number is a different rule than a vector
 * plus a vector; here it is the same rule with a (v, 0) in front of it, so
 * there is one apply() and one resolve() covering all four operators in both
 * orders.
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
#include "value.h"
#include "units.h"
#include "operator.h"
#include "buffer.h"
#include "form.h"
#include "form_linear.h"
#include "form_cplx.h"

#define REFUSE(...) (das_error(DASERR_FORM, __VA_ARGS__), dbsRefuse)

#define CPLX_DEG2RAD 0.017453292519943295
#define CPLX_RAD2DEG 57.29577951308232

/* ************************************************************************* */
/* Component systems                                                         */

/* Terse, to match the register the vector and geoloc tables use.  Polar keeps
   words because "mag" and "phase" are what a spectrum is labelled with, and
   the math short forms (|z|, arg z) read worse on a plot axis. */
static const char* g_aCplxSym[2][2] = {
	{ "re",  "im"    },   /* DAS_VSYS_RECT  */
	{ "mag", "phase" }    /* DAS_VSYS_POLAR */
};

const char* das_cplxsys_str(ubyte uSys)
{
	switch(uSys){
	case DAS_VSYS_RECT:  return "rectangular";
	case DAS_VSYS_POLAR: return "polar";
	}
	return NULL;
}

ubyte das_cplxsys_id(const char* sSys)
{
	if(sSys == NULL) return DAS_VSYS_UNKNOWN;

	/* Prefix matching, the same courtesy das_vsys_id() extends to "cart". */
	if(strncasecmp(sSys, "rect",  4) == 0) return DAS_VSYS_RECT;
	if(strncasecmp(sSys, "polar", 5) == 0) return DAS_VSYS_POLAR;

	return DAS_VSYS_UNKNOWN;
}

const char* das_cplxsys_desc(ubyte uSys)
{
	switch(uSys){
	case DAS_VSYS_RECT:
	return "A complex number as (real, imaginary).  Both components carry the "
	       "variable's units.";
	case DAS_VSYS_POLAR:
	return "A complex number as (magnitude, phase).  The magnitude carries the "
	       "variable's units, the phase is in degrees.";
	}
	return NULL;
}

const char* das_cplxsys_symbol(ubyte uSys, int iComp)
{
	if((iComp < 0)||(iComp > 1)) return NULL;

	switch(uSys){
	case DAS_VSYS_RECT:  return g_aCplxSym[0][iComp];
	case DAS_VSYS_POLAR: return g_aCplxSym[1][iComp];
	}
	return NULL;
}

bool das_cplx_toRect(ubyte uSys, const double* pIn, double* pOut)
{
	switch(uSys){
	case DAS_VSYS_RECT:
		pOut[0] = pIn[0]; pOut[1] = pIn[1];
		return true;

	case DAS_VSYS_POLAR: {
		double rPhase = pIn[1] * CPLX_DEG2RAD;
		pOut[0] = pIn[0] * cos(rPhase);
		pOut[1] = pIn[0] * sin(rPhase);
		return true;
	}}

	return das_error_false(DASERR_FORM,
		"Component system %hhu is not a complex representation; the geometric "
		"systems have no meaning in a plane with no third direction", uSys
	);
}

bool das_cplx_fromRect(ubyte uSys, const double* pIn, double* pOut)
{
	switch(uSys){
	case DAS_VSYS_RECT:
		pOut[0] = pIn[0]; pOut[1] = pIn[1];
		return true;

	case DAS_VSYS_POLAR:
		/* hypot() rather than sqrt(x*x + y*y): it does not overflow on a
		   magnitude that fits when the squares do not, which a spectral
		   density in SI units reaches more often than one would like. */
		pOut[0] = hypot(pIn[0], pIn[1]);

		/* atan2 answers on (-180, 180].  A zero complex has no phase at all,
		   and atan2(0,0) is 0 by C99, so it reads as +0 degrees rather than
		   as an error. */
		pOut[1] = atan2(pIn[1], pIn[0]) * CPLX_RAD2DEG;
		return true;
	}

	return das_error_false(DASERR_FORM,
		"Component system %hhu is not a complex representation", uSys
	);
}

/* ************************************************************************* */
/* The formalism                                                             */

typedef struct das_form_cplx {
	DasForm base;

	ubyte uSysType;   /* DAS_VSYS_RECT or DAS_VSYS_POLAR */
} DasFormCplx;

static DasForm* _cplx_new(void)
{
	DasFormCplx* pThis = (DasFormCplx*)calloc(1, sizeof(DasFormCplx));
	pThis->base.pVTbl = &das_form_cplx_vtbl;
	pThis->uSysType   = DAS_VSYS_RECT;
	return &(pThis->base);
}

DasForm* new_DasFormCplx(ubyte uSysType)
{
	if((uSysType < DAS_VSYS_CPLX_MIN)||(uSysType > DAS_VSYS_CPLX_MAX)){
		das_error(DASERR_FORM,
			"Component system %hhu is not one a complex number can use.  The "
			"geometric systems describe directions in a frame; use <ops "
			"kind=\"vector\"> or <ops kind=\"geoloc\"> for those", uSysType
		);
		return NULL;
	}

	DasFormCplx* pThis = (DasFormCplx*)_cplx_new();
	pThis->uSysType = uSysType;
	return &(pThis->base);
}

ubyte DasFormCplx_sysType(const DasForm* pThis)
{
	if(!DasForm_isKind(pThis, DAS_FORM_CPLX)){
		das_error(DASERR_FORM, "Not a complex formalism");
		return 0;
	}
	return ((const DasFormCplx*)pThis)->uSysType;
}

/* --- parameters ---------------------------------------------------------- */

static DasErrCode _cplx_setParam(
	DasForm* pBase, const char* sName, const char* sVal
){
	DasFormCplx* pThis = (DasFormCplx*)pBase;

	if(strcmp(sName, "system") == 0){
		ubyte uSys = das_cplxsys_id(sVal);
		if(uSys == DAS_VSYS_UNKNOWN)
			return das_error(DASERR_FORM,
				"Unknown component system '%s' for kind=\"complex\"; expected "
				"rectangular or polar.  A complex number lives in a plane with "
				"no third direction and no frame, so the geometric systems do "
				"not apply to it", sVal
			);
		pThis->uSysType = uSys;
		return DAS_OKAY;
	}

	/* Refused by name, with the reason.  Component order is the mistake worth
	   catching: a producer that ships imaginary first has to say so in its
	   storage, not in a parameter, because every rule in this file reads
	   component 0 as the real (or the magnitude) and nothing renegotiates it. */
	if(strcmp(sName, "sysorder") == 0)
		return das_error(DASERR_FORM,
			"<ops kind=\"complex\"> has no sysorder=.  Component 0 is the real "
			"part (or the magnitude) and component 1 the imaginary part (or the "
			"phase); there is no other reading of a complex pair"
		);

	if(strcmp(sName, "frame") == 0)
		return das_error(DASERR_FORM,
			"frame= names a reference frame, which makes the values a geometric "
			"vector rather than a number.  Use <ops kind=\"vector\">"
		);

	/* A reader that CLAIMS a kind must not skip a misspelling in it. */
	return das_error(DASERR_FORM,
		"<ops kind=\"complex\"> has no parameter '%s'", sName
	);
}

/* TALKATIVE, unlike form_vector.c's encode which omits a cartesian system=.
   The two cases are not alike: a vector's system is nearly always cartesian
   and a missing one costs a reader nothing, while rectangular and polar hold
   the same two numbers to wildly different meaning.  Stating it always means a
   das2C-written stream can never be misread for want of one attribute. */
static DasErrCode _cplx_encode(const DasForm* pBase, DasBuf* pBuf)
{
	const DasFormCplx* pThis = (const DasFormCplx*)pBase;

	return DasBuf_printf(pBuf,
		"      <ops kind=\"complex\" system=\"%s\"/>\n",
		das_cplxsys_str(pThis->uSysType)
	);
}

static const char* _cplx_getParam(
	const DasForm* pBase, const char* sName, ubyte* pType
){
	const DasFormCplx* pThis = (const DasFormCplx*)pBase;

	if(strcmp(sName, "system") == 0){
		if(pType != NULL) *pType = DASPROP_STRING | DASPROP_SINGLE;
		return das_cplxsys_str(pThis->uSysType);
	}
	return NULL;
}

/* Exactly two components in one level.  Not "at least two" and not "an even
   count": three complex numbers making up a spectral field is a real thing and
   a real formalism, and it is not this one wearing intern="3;2".  Saying so
   here is cheaper than discovering it in the arithmetic. */
static DasErrCode _cplx_validate(
	DasForm* pBase, int nIntRank, const ptrdiff_t* pIntShape
){
	if(nIntRank != 1)
		return das_error(DASERR_FORM,
			"A complex number is one level of two components, but this variable "
			"declares %d internal levels.  A composite of complex numbers is a "
			"formalism of its own, not this one at a larger shape", nIntRank
		);

	if(pIntShape[0] != 2)
		return das_error(DASERR_FORM,
			"A complex number has exactly 2 components, not %zd", pIntShape[0]
		);

	return DAS_OKAY;
}

/* --- presentation -------------------------------------------------------- */

/* Reads as a number, not as a pair of them.  This lands in a das3_csv cell as
   well as in a log line, so it stays inside the ASCII range and carries no
   comma of its own. */
static char* _cplx_prnRun(
	const DasForm* pForm, const ubyte* pRun, uint32_t nElems,
	das_val_type et, char* sBuf, int nLen
){
	const DasFormCplx* pThis = (const DasFormCplx*)pForm;

	/* NULL hands the job back to datum.c, which prints the cells plainly.  A
	   run that is not a pair did not come from a validated complex variable,
	   and inventing a reading for it would be worse than declining. */
	if(nElems != 2) return NULL;

	double a[2] = {0.0, 0.0};
	size_t uSz = das_vt_size(et);
	for(int i = 0; i < 2; ++i){
		if(das_value_binXform(et, pRun + i*uSz, NULL, vtDouble, (ubyte*)(a + i),
		                      NULL, 0) != DAS_OKAY)
			return NULL;
	}

	if(pThis->uSysType == DAS_VSYS_POLAR)
		snprintf(sBuf, (size_t)nLen, "%.6g@%.6gdeg", a[0], a[1]);
	else
		snprintf(sBuf, (size_t)nLen, "%.6g%+.6gi", a[0], a[1]);

	return sBuf;
}

static bool _cplx_pack(
	const DasForm* pBase, const das_operand* pOp, const ubyte* pRun,
	das_datum* pOut
){
	if((pOp->nIntRank != 1)||(pOp->aIntShape[0] != 2))
		return das_error_false(DASERR_FORM,
			"A complex number has exactly 2 components in one level"
		);

	return das_datum_box(
		pOut, pBase, pRun, pOp->nIntRank, pOp->aIntShape, pOp->vtElem,
		pOp->units
	);
}

static das_val_type _cplx_datumType(const DasForm* pBase){ return vtComposite; }

/* A complex value is always two components in one level, and there is no
   sysorder= to permute them, so there is nothing to bound against. */
static const char* _cplx_compSym(const DasForm* pThis, int iComp)
{
	if(!DasForm_isKind(pThis, DAS_FORM_CPLX)||(iComp < 0)||(iComp > 1))
		return NULL;

	return das_cplxsys_symbol(((const DasFormCplx*)pThis)->uSysType, iComp);
}

static char* _cplx_prnIntr(const DasForm* pBase, char* sBuf, int nLen)
{
	const DasFormCplx* pThis = (const DasFormCplx*)pBase;

	snprintf(sBuf, (size_t)nLen, " complex(%s)",
	         das_cplxsys_str(pThis->uSysType));
	return sBuf;
}

static DasForm* _cplx_copy(const DasForm* pBase)
{
	DasFormCplx* pCopy = (DasFormCplx*)calloc(1, sizeof(DasFormCplx));
	memcpy(pCopy, pBase, sizeof(DasFormCplx));
	return &(pCopy->base);
}

static void _cplx_release(DasForm* pBase){ free(pBase); }


/* ************************************************************************* */
/* The recipe                                                                */

typedef struct das_binop_cplx {
	DasBinOp base;

	das_val_type vtL, vtR;
	int   nOp;

	/* Which side is a plain real being promoted.  Both can never be set: a
	   pairing with no complex operand never reaches this file. */
	bool  bLinL, bLinR;

	ubyte uSysL, uSysR, uSysOut;

	/* Brings the RIGHT operand into the left's units, 1.0 when none is needed.
	   Applied in rectangular, where scaling both components by k is the same
	   thing as scaling a polar magnitude by k and leaving its phase alone. */
	double rRightScale;
} DasBinOpCplx;

/* One operand into a rectangular pair.  A promoted real reads its single cell
   as the real part and leaves the imaginary at zero, which is the whole of the
   promotion rule. */
static bool _cplx_gather(
	const ubyte* pRun, das_val_type et, ubyte uSys, bool bLinear, double* pOut
){
	double aStored[2] = {0.0, 0.0};

	if(das_value_binXform(et, pRun, NULL, vtDouble, (ubyte*)(aStored + 0),
	                      NULL, 0) != DAS_OKAY)
		return false;

	if(bLinear){
		pOut[0] = aStored[0];
		pOut[1] = 0.0;
		return true;
	}

	if(das_value_binXform(et, pRun + das_vt_size(et), NULL, vtDouble,
	                      (ubyte*)(aStored + 1), NULL, 0) != DAS_OKAY)
		return false;

	return das_cplx_toRect(uSys, aStored, pOut);
}

static bool _cplx_apply(
	const DasBinOp* pOp, const ubyte* pLRun, const ubyte* pRRun, ubyte* pOutRun
){
	const DasBinOpCplx* pThis = (const DasBinOpCplx*)pOp;

	double aL[2], aR[2], aRes[2], aOut[2];

	if(!_cplx_gather(pLRun, pThis->vtL, pThis->uSysL, pThis->bLinL, aL))
		return false;
	if(!_cplx_gather(pRRun, pThis->vtR, pThis->uSysR, pThis->bLinR, aR))
		return false;

	aR[0] *= pThis->rRightScale;
	aR[1] *= pThis->rRightScale;

	switch(pThis->nOp){
	case D2BOP_ADD:
		aRes[0] = aL[0] + aR[0];
		aRes[1] = aL[1] + aR[1];
		break;

	case D2BOP_SUB:
		aRes[0] = aL[0] - aR[0];
		aRes[1] = aL[1] - aR[1];
		break;

	case D2BOP_MUL:
		aRes[0] = aL[0]*aR[0] - aL[1]*aR[1];
		aRes[1] = aL[0]*aR[1] + aL[1]*aR[0];
		break;

	case D2BOP_DIV: {
		/* Division by zero is left to IEEE, the same as every other division
		   in this library.  An error return here would abort a whole walk over
		   one bad item, which is a worse answer than an inf the caller can
		   see and filter. */
		double rDenom = aR[0]*aR[0] + aR[1]*aR[1];
		aRes[0] = (aL[0]*aR[0] + aL[1]*aR[1]) / rDenom;
		aRes[1] = (aL[1]*aR[0] - aL[0]*aR[1]) / rDenom;
		break;
	}

	default:
		/* resolve refused everything else; reaching here is a library bug */
		return das_error_false(DASERR_FORM,
			"Operator '%s' has no complex rule", das_op_toStr(pThis->nOp, NULL)
		);
	}

	if(!das_cplx_fromRect(pThis->uSysOut, aRes, aOut)) return false;

	size_t uOut = das_vt_size(pOp->vtOut);
	for(int i = 0; i < 2; ++i){
		if(das_value_binXform(vtDouble, (const ubyte*)(aOut + i), NULL,
		                      pOp->vtOut, pOutRun + i*uOut, NULL, 0) != DAS_OKAY)
			return false;
	}
	return true;
}

static void _cplx_binop_release(DasBinOp* pOp)
{
	del_DasForm(pOp->pForm);
	free(pOp);
}

static const DasBinOp_VTbl g_vtblCplx = { _cplx_apply, _cplx_binop_release };


/* ************************************************************************* */
/* Dispatch                                                                  */

/* Both hooks land here.  There is only ever one rule to resolve because the
   promotion happens first: whichever side is a plain real becomes a
   zero-imaginary complex and what is left is complex OP complex. */
static das_binop_stat _cplx_resolve(
	const das_operand* pL, int nOp, const das_operand* pR, DasBinOp** ppOut
){
	bool bLinL = !DasForm_isKind(pL->pForm, DAS_FORM_CPLX);
	bool bLinR = !DasForm_isKind(pR->pForm, DAS_FORM_CPLX);

	/* A promoted real must be a single number.  A rank-1 run of two plain
	   numbers is two numbers -- linear claims no structure at all, so reading
	   one as a complex pair would be inventing a meaning its author did not
	   write. */
	if(bLinL && (pL->nIntRank != 0))
		return REFUSE("A complex number combines with a single real, not with a "
		              "rank %d run of them", pL->nIntRank);
	if(bLinR && (pR->nIntRank != 0))
		return REFUSE("A complex number combines with a single real, not with a "
		              "rank %d run of them", pR->nIntRank);

	if(!bLinL && ((pL->nIntRank != 1)||(pL->aIntShape[0] != 2)))
		return REFUSE("A complex number has exactly 2 components in one level");
	if(!bLinR && ((pR->nIntRank != 1)||(pR->aIntShape[0] != 2)))
		return REFUSE("A complex number has exactly 2 components in one level");

	/* Every component runs through a double on the way in and out, so a
	   storage type that is not a number has nowhere to go.  Caught here rather
	   than trusted to das_vt_merge(), which has a rule for vtTime that makes
	   sense for a calendar and none at all for a phase. */
	if(!das_vt_isint(pL->vtElem)&&!das_vt_isreal(pL->vtElem))
		return REFUSE("Complex arithmetic needs numeric components, the left "
		              "operand holds %s", das_vt_toStr(pL->vtElem));
	if(!das_vt_isint(pR->vtElem)&&!das_vt_isreal(pR->vtElem))
		return REFUSE("Complex arithmetic needs numeric components, the right "
		              "operand holds %s", das_vt_toStr(pR->vtElem));

	ubyte uSysL = bLinL ? DAS_VSYS_RECT : DasFormCplx_sysType(pL->pForm);
	ubyte uSysR = bLinR ? DAS_VSYS_RECT : DasFormCplx_sysType(pR->pForm);

	/* The result wears the complex operand's representation, and the LEFT
	   one's when both are complex.  Same rule form_vector.c follows for
	   component systems: an answer in a representation neither operand used
	   would surprise, and the left side names the result everywhere else. */
	ubyte uSysOut = bLinL ? uSysR : uSysL;

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
		/* Not wrong, just unbuilt.  A complex power is a real operation with a
		   branch cut to argue about, and nobody has needed one yet. */
		return REFUSE("Operator '%s' is not implemented for complex numbers",
		              das_op_toStr(nOp, NULL));
	}

	DasBinOpCplx* pRes = (DasBinOpCplx*)calloc(1, sizeof(DasBinOpCplx));
	pRes->base.pVTbl = &g_vtblCplx;
	pRes->base.nRef  = 1;

	pRes->vtL = pL->vtElem;
	pRes->vtR = pR->vtElem;
	pRes->nOp = nOp;
	pRes->bLinL = bLinL;
	pRes->bLinR = bLinR;
	pRes->uSysL = uSysL;
	pRes->uSysR = uSysR;
	pRes->uSysOut = uSysOut;
	pRes->rRightScale = rRightScale;

	pRes->base.units = unitsOut;
	pRes->base.pForm = new_DasFormCplx(uSysOut);
	pRes->base.nIntRank     = 1;
	pRes->base.aIntShape[0] = 2;

	/* Three ways a rule promises more precision than its operands carry: a
	   polar leg goes through trigonometry, a unit conversion multiplies by a
	   real factor, and complex division divides.  Any of them and the result
	   has to be wide enough to hold what apply() computed, which is why an
	   integer pair divided by an integer pair does not come back as one. */
	bool bWide = (uSysL == DAS_VSYS_POLAR)||(uSysR == DAS_VSYS_POLAR)||
	             (uSysOut == DAS_VSYS_POLAR)||
	             (rRightScale != 1.0)||(nOp == D2BOP_DIV);

	pRes->base.vtOut = bWide
		? vtDouble
		: das_vt_merge(pR->vtElem, nOp, pL->vtElem);

	if(pRes->base.vtOut == vtUnknown){
		del_DasForm(pRes->base.pForm);
		free(pRes);
		return REFUSE("No result type for %s %s %s",
			das_vt_toStr(pL->vtElem), das_op_toStr(nOp, NULL),
			das_vt_toStr(pR->vtElem));
	}

	*ppOut = &(pRes->base);
	return dbsOkay;
}

static das_binop_stat _cplx_binOpLeft(
	const DasForm* pBase, const das_operand* pL, int nOp,
	const das_operand* pR, DasBinOp** ppOut
){
	/* Complex pairs with itself and with a plain real, and with nothing else
	   this file knows of.  A vector or a position on the right is DECLINED so
	   the better informed side gets its turn, even though neither has a rule
	   for us today; that is the mechanism working, not a gap. */
	if(!DasForm_isKind(pR->pForm, DAS_FORM_CPLX) &&
	   !DasForm_isKind(pR->pForm, DAS_FORM_LINEAR))
		return dbsDecline;

	return _cplx_resolve(pL, nOp, pR, ppOut);
}

/* real OP complex.  Claimed HERE, not in form_linear.c: linear sits at the
   bottom of the knowledge graph and must not learn what a complex number is.
   Stated as its own rule rather than inferred by commuting the left one --
   the library never reorders operands, and 2 / z is not z / 2. */
static das_binop_stat _cplx_binOpRight(
	const DasForm* pBase, const das_operand* pL, int nOp,
	const das_operand* pR, DasBinOp** ppOut
){
	if(!DasForm_isKind(pL->pForm, DAS_FORM_LINEAR)) return dbsDecline;

	return _cplx_resolve(pL, nOp, pR, ppOut);
}

const DasForm_VTbl das_form_cplx_vtbl = {
	"complex",
	_cplx_new,
	_cplx_setParam,
	_cplx_validate,
	_cplx_getParam,
	_cplx_encode,
	_cplx_pack,
	_cplx_datumType,
	_cplx_prnIntr,
	_cplx_prnRun,
	_cplx_compSym,
	_cplx_binOpLeft,
	_cplx_binOpRight,
	_cplx_copy,
	_cplx_release
};
