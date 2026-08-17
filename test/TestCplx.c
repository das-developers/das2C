/** @file TestCplx.c Unit tests for the complex formalism (form_cplx.c) */

/* Author: Chris Piker <chris-piker@uiowa.edu>, via Claude Opus 5
 *
 * This file is intended to demonstrate an interface.  This is free
 * and unencumbered software released into the public domain
 *
 * Anyone is free to copy, modify, publish, use, compile, sell, or
 * distribute this file, either in source code form or as a compiled
 * binary, for any purpose, commercial or non-commercial, and by any
 * means.
 *
 * In jurisdictions that recognize copyright laws, the author or authors
 * of this file dedicate any and all copyright interest in this file to
 * the public domain. We make this dedication for the benefit of the
 * public at large and to the detriment of our heirs and successors. We
 * intend this dedication to be an overt act of relinquishment in
 * perpetuity of all present and future rights to this file under
 * copyright law.
 *
 * THIS FILE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.
 *
 * For more information, please refer to <http://unlicense.org/>
 */

/* The complex kind is the first formalism whose arithmetic is not a
 * decorated version of the elementwise rule, so this file mostly checks that
 * a complex product is a complex product: (a+bi)(c+di) and not (ac, bd).
 *
 * Nothing here builds a DasVar.  A form is handed values and hands values
 * back, so a das_operand filled in by hand is the whole of what the layer
 * needs, and _resolve() below is a copy of var_bin.c's dispatch so the
 * decline/refuse protocol under test is the one that actually ships.
 *
 * Dev Note: Keep it C99 clean: no _Static_assert, use runtime checks.
 */

#define _POSIX_C_SOURCE 200112L

#include <stdio.h>
#include <string.h>
#include <math.h>

#include <das3/core.h>
#include <das3/form.h>
#include <das3/form_cplx.h>
#include <das3/form_linear.h>
#include <das3/form_vector.h>

#define CHECK(expr) \
	if(!(expr)){ \
		printf("ERROR: check failed at %s:%d: %s\n", __FILE__, __LINE__, #expr); \
		++nErrs; \
	}

#define MUST(expr) \
	if(!(expr)){ \
		printf("ERROR: check failed at %s:%d: %s\n", __FILE__, __LINE__, #expr); \
		return nErrs + 1; \
	}

/* Complex arithmetic runs through trigonometry on the polar legs, so an
   exact compare would be testing the FPU and not the rules. */
#define CLOSE(have, want) (fabs((have) - (want)) < 1e-9)

/* var_bin.c's _binset_resolve, repeated so this file exercises the real
   protocol: left hook first, a refusal is final, a decline moves on. */
static das_binop_stat _resolve(
	const das_operand* pL, int nOp, const das_operand* pR, DasBinOp** ppOut
){
	das_binop_stat stat = dbsDecline;

	if(pL->pForm->pVTbl->binOpLeft != NULL)
		stat = pL->pForm->pVTbl->binOpLeft(pL->pForm, pL, nOp, pR, ppOut);

	if(stat != dbsDecline) return stat;

	if(pR->pForm->pVTbl->binOpRight != NULL)
		stat = pR->pForm->pVTbl->binOpRight(pR->pForm, pL, nOp, pR, ppOut);

	return stat;
}

static void _cplxOperand(das_operand* pOp, DasForm* pForm, das_units units)
{
	memset(pOp, 0, sizeof(das_operand));
	pOp->pForm       = pForm;
	pOp->vtElem      = vtDouble;
	pOp->units       = units;
	pOp->nIntRank    = 1;
	pOp->aIntShape[0] = 2;
}

static void _realOperand(das_operand* pOp, DasForm* pForm, das_units units)
{
	memset(pOp, 0, sizeof(das_operand));
	pOp->pForm    = pForm;
	pOp->vtElem   = vtDouble;
	pOp->units    = units;
	pOp->nIntRank = 0;
}

/* ************************************************************************* */
/* Case 1: the wire face -- kind, parameters, shape                          */

static int test_cplx_wire(void)
{
	int nErrs = 0;

	const DasForm_VTbl* pVTbl = das_form_lookup("complex");
	MUST(pVTbl != NULL);
	CHECK(strcmp(pVTbl->sKind, "complex") == 0);

	/* Rectangular is the default, so kind= alone is a whole element */
	const char* aBare[] = {"kind","complex", NULL};
	DasForm* pBare = new_DasForm_pairs(aBare);
	MUST(pBare != NULL);
	CHECK(DasForm_isKind(pBare, DAS_FORM_CPLX));
	CHECK(!DasForm_isKind(pBare, DAS_FORM_EXT));
	CHECK(DasFormCplx_sysType(pBare) == DAS_VSYS_RECT);

	/* getParam hands back the WIRE spelling of a decoded value */
	const char* sSys = DasForm_getParam(pBare, "system", NULL);
	MUST(sSys != NULL);
	CHECK(strcmp(sSys, "rectangular") == 0);
	CHECK(DasForm_getParam(pBare, "frame", NULL) == NULL);

	/* Exactly two components in one level, and nothing else */
	ptrdiff_t aTwo[1]  = {2};
	ptrdiff_t aThree[1] = {3};
	ptrdiff_t aPairs[2] = {3, 2};
	CHECK(DasForm_validate(pBare, 1, aTwo)   == DAS_OKAY);
	CHECK(DasForm_validate(pBare, 1, aThree) != DAS_OKAY);
	CHECK(DasForm_validate(pBare, 2, aPairs) != DAS_OKAY);
	CHECK(DasForm_validate(pBare, 0, NULL)   != DAS_OKAY);
	DasForm_decRef(pBare);

	/* Abbreviations are long-standing wire practice */
	const char* aPolar[] = {"kind","complex", "system","polar", NULL};
	DasForm* pPolar = new_DasForm_pairs(aPolar);
	MUST(pPolar != NULL);
	CHECK(DasFormCplx_sysType(pPolar) == DAS_VSYS_POLAR);
	DasForm_decRef(pPolar);

	const char* aRect[] = {"kind","complex", "system","rect", NULL};
	DasForm* pRect = new_DasForm_pairs(aRect);
	MUST(pRect != NULL);
	CHECK(DasFormCplx_sysType(pRect) == DAS_VSYS_RECT);
	DasForm_decRef(pRect);

	/* A parameter this kind does not define is FATAL, never skipped: a
	   silently ignored system= would ship polar data read as rectangular. */
	const char* aTypo[] = {"kind","complex", "sytem","polar", NULL};
	CHECK(new_DasForm_pairs(aTypo) == NULL);

	const char* aGeo[] = {"kind","complex", "system","cartesian", NULL};
	CHECK(new_DasForm_pairs(aGeo) == NULL);

	const char* aOrder[] = {"kind","complex", "sysorder","1;0", NULL};
	CHECK(new_DasForm_pairs(aOrder) == NULL);

	const char* aFrame[] = {"kind","complex", "frame","TSCS", NULL};
	CHECK(new_DasForm_pairs(aFrame) == NULL);

	/* The typed constructor refuses a geometric code by range */
	CHECK(new_DasFormCplx(DAS_VSYS_CART) == NULL);
	CHECK(new_DasFormCplx(DAS_VSYS_UNKNOWN) == NULL);

	/* encode() is TALKATIVE about system=, since rectangular and polar hold
	   the same two numbers to entirely different meaning */
	DasBuf* pBuf = new_DasBuf(256);
	MUST(pBuf != NULL);
	DasForm* pEnc = new_DasFormCplx(DAS_VSYS_POLAR);
	MUST(pEnc != NULL);
	CHECK(pEnc->pVTbl->encode(pEnc, pBuf) == DAS_OKAY);

	char sOut[256] = {'\0'};
	DasBuf_read(pBuf, sOut, sizeof(sOut) - 1);
	CHECK(strstr(sOut, "kind=\"complex\"") != NULL);
	CHECK(strstr(sOut, "system=\"polar\"") != NULL);
	DasForm_decRef(pEnc);
	del_DasBuf(pBuf);

	return nErrs;
}

/* ************************************************************************* */
/* Case 2: the product that is not component-wise                            */

static int test_cplx_mul(void)
{
	int nErrs = 0;

	/* Two DIFFERENT units on purpose: a product and a ratio of the same one
	   runs into a units-layer wart (V/m over V/m renders as "V**0 m**0"
	   rather than reducing) that has nothing to do with complex math. */
	das_units unitsL = Units_fromStr("V/m");
	das_units unitsR = Units_fromStr("s");
	DasForm* pL = new_DasFormCplx(DAS_VSYS_RECT);
	DasForm* pR = new_DasFormCplx(DAS_VSYS_RECT);
	MUST(pL != NULL); MUST(pR != NULL);

	das_operand opL, opR;
	_cplxOperand(&opL, pL, unitsL);
	_cplxOperand(&opR, pR, unitsR);

	DasBinOp* pOp = NULL;
	MUST(_resolve(&opL, D2BOP_MUL, &opR, &pOp) == dbsOkay);
	MUST(pOp != NULL);

	CHECK(pOp->nIntRank == 1);
	CHECK(pOp->aIntShape[0] == 2);
	CHECK(DasForm_isKind(pOp->pForm, DAS_FORM_CPLX));
	CHECK(pOp->units == Units_multiply(unitsL, unitsR));

	/* (1 + 2i)(3 + 4i) = -5 + 10i.  Component-wise would give (3, 8), which
	   is the answer this whole formalism exists to prevent. */
	double aL[2] = {1.0, 2.0}, aR[2] = {3.0, 4.0}, aOut[2] = {0.0, 0.0};
	MUST(DasBinOp_apply(pOp, (const ubyte*)aL, (const ubyte*)aR, (ubyte*)aOut));
	CHECK(CLOSE(aOut[0], -5.0));
	CHECK(CLOSE(aOut[1], 10.0));

	/* i * i = -1, the identity that says the rule is right */
	double aI[2] = {0.0, 1.0};
	MUST(DasBinOp_apply(pOp, (const ubyte*)aI, (const ubyte*)aI, (ubyte*)aOut));
	CHECK(CLOSE(aOut[0], -1.0));
	CHECK(CLOSE(aOut[1],  0.0));

	DasBinOp_decRef(pOp);

	/* Division inverts it: (-5 + 10i) / (3 + 4i) = 1 + 2i */
	pOp = NULL;
	MUST(_resolve(&opL, D2BOP_DIV, &opR, &pOp) == dbsOkay);
	MUST(pOp != NULL);
	CHECK(pOp->units == Units_divide(unitsL, unitsR));

	double aProd[2] = {-5.0, 10.0};
	MUST(DasBinOp_apply(pOp, (const ubyte*)aProd, (const ubyte*)aR, (ubyte*)aOut));
	CHECK(CLOSE(aOut[0], 1.0));
	CHECK(CLOSE(aOut[1], 2.0));
	DasBinOp_decRef(pOp);

	DasForm_decRef(pL);
	DasForm_decRef(pR);
	return nErrs;
}

/* ************************************************************************* */
/* Case 3: sums, and the unit conversion that rides with them                */

static int test_cplx_addsub(void)
{
	int nErrs = 0;

	das_units units = Units_fromStr("V/m");
	DasForm* pL = new_DasFormCplx(DAS_VSYS_RECT);
	DasForm* pR = new_DasFormCplx(DAS_VSYS_RECT);
	MUST(pL != NULL); MUST(pR != NULL);

	das_operand opL, opR;
	_cplxOperand(&opL, pL, units);
	_cplxOperand(&opR, pR, units);

	DasBinOp* pOp = NULL;
	MUST(_resolve(&opL, D2BOP_ADD, &opR, &pOp) == dbsOkay);
	CHECK(pOp->units == units);

	double aL[2] = {1.0, 2.0}, aR[2] = {3.0, 4.0}, aOut[2] = {0.0, 0.0};
	MUST(DasBinOp_apply(pOp, (const ubyte*)aL, (const ubyte*)aR, (ubyte*)aOut));
	CHECK(CLOSE(aOut[0], 4.0));
	CHECK(CLOSE(aOut[1], 6.0));
	DasBinOp_decRef(pOp);

	pOp = NULL;
	MUST(_resolve(&opL, D2BOP_SUB, &opR, &pOp) == dbsOkay);
	MUST(DasBinOp_apply(pOp, (const ubyte*)aL, (const ubyte*)aR, (ubyte*)aOut));
	CHECK(CLOSE(aOut[0], -2.0));
	CHECK(CLOSE(aOut[1], -2.0));
	DasBinOp_decRef(pOp);

	/* A convertible right operand is scaled, and the scale reaches BOTH
	   components: half a complex number is not half of its real part. */
	das_operand opMilli;
	DasForm* pMilli = new_DasFormCplx(DAS_VSYS_RECT);
	MUST(pMilli != NULL);
	_cplxOperand(&opMilli, pMilli, Units_fromStr("mV/m"));

	pOp = NULL;
	MUST(_resolve(&opL, D2BOP_ADD, &opMilli, &pOp) == dbsOkay);
	CHECK(pOp->units == units);

	double aMilli[2] = {1000.0, 2000.0};
	MUST(DasBinOp_apply(pOp, (const ubyte*)aL, (const ubyte*)aMilli, (ubyte*)aOut));
	CHECK(CLOSE(aOut[0], 2.0));
	CHECK(CLOSE(aOut[1], 4.0));
	DasBinOp_decRef(pOp);

	/* Units that do not convert are REFUSED, not quietly added */
	das_operand opSec;
	DasForm* pSec = new_DasFormCplx(DAS_VSYS_RECT);
	MUST(pSec != NULL);
	_cplxOperand(&opSec, pSec, Units_fromStr("s"));

	pOp = NULL;
	CHECK(_resolve(&opL, D2BOP_ADD, &opSec, &pOp) == dbsRefuse);

	DasForm_decRef(pSec);
	DasForm_decRef(pMilli);
	DasForm_decRef(pL);
	DasForm_decRef(pR);
	return nErrs;
}

/* ************************************************************************* */
/* Case 4: a real number promotes to (v, 0), in both orders                   */

static int test_cplx_real(void)
{
	int nErrs = 0;

	das_units units = Units_fromStr("V/m");
	DasForm* pCplx = new_DasFormCplx(DAS_VSYS_RECT);
	DasForm* pLin  = new_DasFormLinear();
	MUST(pCplx != NULL); MUST(pLin != NULL);

	das_operand opCplx, opReal;
	_cplxOperand(&opCplx, pCplx, units);
	_realOperand(&opReal, pLin, UNIT_DIMENSIONLESS);

	double aCplx[2] = {1.0, 2.0};
	double rReal = 3.0;
	double aOut[2] = {0.0, 0.0};

	/* complex * real, claimed by the left hook */
	DasBinOp* pOp = NULL;
	MUST(_resolve(&opCplx, D2BOP_MUL, &opReal, &pOp) == dbsOkay);
	CHECK(DasForm_isKind(pOp->pForm, DAS_FORM_CPLX));
	MUST(DasBinOp_apply(pOp, (const ubyte*)aCplx, (const ubyte*)&rReal, (ubyte*)aOut));
	CHECK(CLOSE(aOut[0], 3.0));
	CHECK(CLOSE(aOut[1], 6.0));
	DasBinOp_decRef(pOp);

	/* real * complex.  Linear declines (it may not learn what a complex is)
	   and complex claims it from the right, which is the arrow working. */
	pOp = NULL;
	MUST(_resolve(&opReal, D2BOP_MUL, &opCplx, &pOp) == dbsOkay);
	CHECK(DasForm_isKind(pOp->pForm, DAS_FORM_CPLX));
	MUST(DasBinOp_apply(pOp, (const ubyte*)&rReal, (const ubyte*)aCplx, (ubyte*)aOut));
	CHECK(CLOSE(aOut[0], 3.0));
	CHECK(CLOSE(aOut[1], 6.0));
	DasBinOp_decRef(pOp);

	/* real / complex is NOT complex / real, which is why the two hooks are
	   two stated rules and never one commuted: 4 / (1 + i) = 2 - 2i */
	das_operand opFour;
	_realOperand(&opFour, pLin, UNIT_DIMENSIONLESS);
	double rFour = 4.0, aOne[2] = {1.0, 1.0};

	pOp = NULL;
	MUST(_resolve(&opFour, D2BOP_DIV, &opCplx, &pOp) == dbsOkay);
	MUST(DasBinOp_apply(pOp, (const ubyte*)&rFour, (const ubyte*)aOne, (ubyte*)aOut));
	CHECK(CLOSE(aOut[0],  2.0));
	CHECK(CLOSE(aOut[1], -2.0));
	DasBinOp_decRef(pOp);

	/* A real ADDED to a complex moves the real part and leaves the imaginary
	   where it was, which is the promotion doing its whole job. */
	das_operand opVolts;
	_realOperand(&opVolts, pLin, units);

	pOp = NULL;
	MUST(_resolve(&opCplx, D2BOP_ADD, &opVolts, &pOp) == dbsOkay);
	CHECK(pOp->units == units);
	MUST(DasBinOp_apply(pOp, (const ubyte*)aCplx, (const ubyte*)&rReal, (ubyte*)aOut));
	CHECK(CLOSE(aOut[0], 4.0));
	CHECK(CLOSE(aOut[1], 2.0));
	DasBinOp_decRef(pOp);

	DasForm_decRef(pLin);
	DasForm_decRef(pCplx);
	return nErrs;
}

/* ************************************************************************* */
/* Case 5: polar, and the representation the result comes back in            */

static int test_cplx_polar(void)
{
	int nErrs = 0;

	das_units units = Units_fromStr("V/m");
	DasForm* pP1 = new_DasFormCplx(DAS_VSYS_POLAR);
	DasForm* pP2 = new_DasFormCplx(DAS_VSYS_POLAR);
	MUST(pP1 != NULL); MUST(pP2 != NULL);

	das_operand opP1, opP2;
	_cplxOperand(&opP1, pP1, units);
	_cplxOperand(&opP2, pP2, units);

	/* Magnitudes multiply and phases add.  Checked through the rectangular
	   kernel rather than special cased, so this also pins the round trip. */
	DasBinOp* pOp = NULL;
	MUST(_resolve(&opP1, D2BOP_MUL, &opP2, &pOp) == dbsOkay);
	CHECK(DasFormCplx_sysType(pOp->pForm) == DAS_VSYS_POLAR);

	double aA[2] = {2.0, 30.0}, aB[2] = {3.0, 45.0}, aOut[2] = {0.0, 0.0};
	MUST(DasBinOp_apply(pOp, (const ubyte*)aA, (const ubyte*)aB, (ubyte*)aOut));
	CHECK(CLOSE(aOut[0], 6.0));
	CHECK(CLOSE(aOut[1], 75.0));
	DasBinOp_decRef(pOp);

	/* Two unit vectors 90 degrees apart sum to sqrt(2) at 45 */
	pOp = NULL;
	MUST(_resolve(&opP1, D2BOP_ADD, &opP2, &pOp) == dbsOkay);
	double aE[2] = {1.0, 0.0}, aN[2] = {1.0, 90.0};
	MUST(DasBinOp_apply(pOp, (const ubyte*)aE, (const ubyte*)aN, (ubyte*)aOut));
	CHECK(CLOSE(aOut[0], sqrt(2.0)));
	CHECK(CLOSE(aOut[1], 45.0));
	DasBinOp_decRef(pOp);

	/* Mixed representations are legal and the LEFT operand names the result,
	   the same rule form_vector.c follows for component systems. */
	DasForm* pRect = new_DasFormCplx(DAS_VSYS_RECT);
	MUST(pRect != NULL);
	das_operand opRect;
	_cplxOperand(&opRect, pRect, units);

	pOp = NULL;
	MUST(_resolve(&opRect, D2BOP_MUL, &opP1, &pOp) == dbsOkay);
	CHECK(DasFormCplx_sysType(pOp->pForm) == DAS_VSYS_RECT);

	/* (0 + 1i) * (1 at 90 deg) = i * i = -1 */
	double aImag[2] = {0.0, 1.0};
	MUST(DasBinOp_apply(pOp, (const ubyte*)aImag, (const ubyte*)aN, (ubyte*)aOut));
	CHECK(CLOSE(aOut[0], -1.0));
	CHECK(CLOSE(aOut[1],  0.0));
	DasBinOp_decRef(pOp);

	pOp = NULL;
	MUST(_resolve(&opP1, D2BOP_MUL, &opRect, &pOp) == dbsOkay);
	CHECK(DasFormCplx_sysType(pOp->pForm) == DAS_VSYS_POLAR);
	DasBinOp_decRef(pOp);

	/* The conversion helpers on their own, since a caller may reach them
	   without ever building a recipe */
	double aRectVal[2] = {3.0, 4.0}, aPolarVal[2], aBack[2];
	MUST(das_cplx_fromRect(DAS_VSYS_POLAR, aRectVal, aPolarVal));
	CHECK(CLOSE(aPolarVal[0], 5.0));
	CHECK(CLOSE(aPolarVal[1], 53.13010235415598));
	MUST(das_cplx_toRect(DAS_VSYS_POLAR, aPolarVal, aBack));
	CHECK(CLOSE(aBack[0], 3.0));
	CHECK(CLOSE(aBack[1], 4.0));

	/* A geometric code is not a complex representation */
	CHECK(!das_cplx_toRect(DAS_VSYS_SPH, aRectVal, aBack));
	CHECK(das_cplxsys_id("spherical") == DAS_VSYS_UNKNOWN);
	CHECK(das_cplxsys_str(DAS_VSYS_CART) == NULL);

	DasForm_decRef(pRect);
	DasForm_decRef(pP2);
	DasForm_decRef(pP1);
	return nErrs;
}

/* ************************************************************************* */
/* Case 6: what gets refused, and what gets declined                         */

static int test_cplx_refuse(void)
{
	int nErrs = 0;

	das_units units = Units_fromStr("V/m");
	DasForm* pCplx = new_DasFormCplx(DAS_VSYS_RECT);
	DasForm* pLin  = new_DasFormLinear();
	MUST(pCplx != NULL); MUST(pLin != NULL);

	das_operand opCplx, opOther;
	_cplxOperand(&opCplx, pCplx, units);

	DasBinOp* pOp = NULL;

	/* Not implemented is a REFUSAL with a reason, not a decline: sending this
	   to the other operand would only produce a vaguer message. */
	CHECK(_resolve(&opCplx, D2BOP_POW, &opCplx, &pOp) == dbsRefuse);

	/* A rank-1 run of two plain numbers is two numbers.  Linear claims no
	   structure, so reading one as a complex pair would invent a meaning. */
	_realOperand(&opOther, pLin, UNIT_DIMENSIONLESS);
	opOther.nIntRank = 1;
	opOther.aIntShape[0] = 2;
	pOp = NULL;
	CHECK(_resolve(&opCplx, D2BOP_MUL, &opOther, &pOp) == dbsRefuse);

	/* A complex operand that is not a pair */
	_cplxOperand(&opOther, pCplx, units);
	opOther.aIntShape[0] = 3;
	pOp = NULL;
	CHECK(_resolve(&opCplx, D2BOP_MUL, &opOther, &pOp) == dbsRefuse);

	/* Components have to be numbers.  Caught here rather than trusted to
	   das_vt_merge(), which has a rule for vtTime that suits a calendar and
	   makes no sense at all for a phase. */
	_cplxOperand(&opOther, pCplx, units);
	opOther.vtElem = vtTime;
	pOp = NULL;
	CHECK(_resolve(&opCplx, D2BOP_ADD, &opOther, &pOp) == dbsRefuse);

	/* A vector is DECLINED, not refused: neither formalism has a rule for the
	   other, and the mechanism reporting "no rule" is the correct outcome. */
	DasForm* pVec = new_DasFormVector("TSCS", DAS_VSYS_CART, VEC_DIRS3(0,1,2));
	MUST(pVec != NULL);
	memset(&opOther, 0, sizeof(das_operand));
	opOther.pForm = pVec;
	opOther.vtElem = vtDouble;
	opOther.units = units;
	opOther.nIntRank = 1;
	opOther.aIntShape[0] = 3;

	pOp = NULL;
	CHECK(_resolve(&opCplx, D2BOP_ADD, &opOther, &pOp) == dbsDecline);
	CHECK(_resolve(&opOther, D2BOP_ADD, &opCplx, &pOp) == dbsDecline);

	DasForm_decRef(pVec);
	DasForm_decRef(pLin);
	DasForm_decRef(pCplx);
	return nErrs;
}

/* ************************************************************************* */

int main(int argc, char** argv)
{
	(void)argc;

	/* Unbuffered: a crash must not discard the checks that ran before it. */
	setvbuf(stdout, NULL, _IONBF, 0);

	das_init(argv[0], DASERR_DIS_RET, 0, DASLOG_ERROR, NULL);

	struct { const char* sName; int (*pFn)(void); } aCase[] = {
		{"test_cplx_wire",   test_cplx_wire},
		{"test_cplx_mul",    test_cplx_mul},
		{"test_cplx_addsub", test_cplx_addsub},
		{"test_cplx_real",   test_cplx_real},
		{"test_cplx_polar",  test_cplx_polar},
		{"test_cplx_refuse", test_cplx_refuse},
	};
	int nCase = (int)(sizeof(aCase)/sizeof(aCase[0]));

	int nBadCase = 0, nBadCheck = 0;
	for(int i = 0; i < nCase; ++i){
		int n = aCase[i].pFn();
		if(n > 0){
			printf("ERROR: %s: %d check(s) failed\n", aCase[i].sName, n);
			++nBadCase;
			nBadCheck += n;
		}
	}

	if(nBadCase > 0){
		printf("ERROR: TestCplx: %d check(s) failed across %d of %d cases\n",
			nBadCheck, nBadCase, nCase);
		return 13;
	}

	printf("INFO: TestCplx: all complex formalism checks passed\n");
	return 0;
}

/* Coverage manifest.
 *
 * DONE 1. The wire face: kind lookup, the system= parameter and its
 *         abbreviations, a misspelled parameter refused, a geometric system
 *         refused, validate() against every wrong shape, talkative encode.
 * DONE 2. The product that is not component-wise, i*i = -1, and division as
 *         its inverse.
 * DONE 3. Sums, the unit scale reaching both components, a unit mismatch.
 * DONE 4. The real promotion in both orders, and real / complex proving the
 *         two hooks are two rules rather than one commuted.
 * DONE 5. Polar: magnitudes multiply and phases add, a sum through the
 *         rectangular kernel, mixed representations, the helpers alone.
 * DONE 6. Refusals and the one DECLINE that matters.
 *
 * TODO 7. pack() into a vtComposite datum and the prnRun rendering, once
 *         something builds a complex variable to pack from.  TestVar is where
 *         that belongs, next to the vector pack it already reaches.
 * TODO 8. A complex variable read from a stream, which waits on das3_from_cdf
 *         writing one.
 */
