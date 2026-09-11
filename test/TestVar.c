/** @file TestVar.c Unit tests for the DasVar layer (das3/variable.h) */

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

/* Unit tests for the DasVar layer (das3/variable.h).
 *
 * The subject is the variable interface.  A generator is FIXTURE here: these
 * cases build one because a variable needs a value source, and then ask every
 * question through DasVar_*.  The DasGen interface has its own tests in
 * TestGen.c, and an assertion about generator behavior belongs there.
 *
 * Grows case by case as the layer grows; the manifest of intended coverage is
 * at bottom.  Keep it C99 clean: no _Static_assert, use runtime checks.
 *
 */

#include <stdio.h>
#include <string.h>

#include <das3/core.h>
#include <das3/variable.h>
#include <das3/form_linear.h>
#include <das3/form_point.h>
#include <das3/form_vector.h>
#include <das3/form_vector.h>  /* core.h does not pull this in; typed vector reads */

#include "marsis_ais_data.h"

/* A failing CHECK records the miss and lets the case keep going, so one run
   reports every broken expectation rather than only the first.  MUST is for
   the checks that cannot be survived: a null pointer the next line reads, or
   a call that leaves a struct uninitialized.  Both need an `int nErrs = 0` in
   scope, so forgetting one is a compile error rather than a silent miscount. */
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

/* A plain linear scalar over an existing generator.
 *
 * Several cases below are about shape, role or extent and carry a formalism
 * only because every numeric variable has one.  For those the construction is
 * noise, so it lives here.  Where the reference rule IS the subject --
 * test_scalar_set and test_ctor_ref_contract -- it stays spelled out. */
static DasVar* _linearVar(DasGen* pGen, das_units units)
{
	DasForm* pForm = new_DasFormLinear();
	if(pForm == NULL) return NULL;
	DasVar* pVar = new_DasVar(pGen, units, pForm);
	del_DasForm(pForm);   /* the variable copied it */
	return pVar;
}

/* Case 3 (scalar slice): a set reads back the right datum */
static int test_scalar_set(void)
{
	int nErrs = 0;

	ptrdiff_t aShape[1] = { VARIDX_RAGGED };
	int64_t nIntercept = 0LL, nSlope = 7812500LL;   /* 128 Hz in TT2000 ns */
	DasGen* pGen = new_DasGenSeq(
		etLong, (const ubyte*)&nIntercept, 1, (const ubyte*)&nSlope, aShape
	);
	MUST(pGen != NULL);

	/* Make both, hand both over, drop both: the constructor adds its own
	   reference to each and the two made here are still ours to release. */
	DasForm* pForm = new_DasFormPoint();
	MUST(pForm != NULL);
	DasVar* pTime = new_DasVar(pGen, UNIT_TT2000, pForm);
	del_DasForm(pForm);
	MUST(pTime != NULL);
	/* A scalar is the base class: no internal index, and it says so twice --
	   by name and by shape.  There is no stored "kind" to disagree with. */
	CHECK(strcmp(DasVar_element(pTime), "scalar") == 0);
	ptrdiff_t aIntr[VARIDX_MAX];
	CHECK(DasVar_intrShape(pTime, aIntr) == 0);
	CHECK(DasVar_formIs(pTime, DAS_FORM_POINT));

	ptrdiff_t aLoc[1] = { 2 };
	das_datum dm = {{0},vtUnknown,0,NULL};
	MUST(DasVar_get(pTime, aLoc, DAS_BS_NULL, &dm) == 0);   /* dm is garbage if this fails */
	CHECK(dm.vt == vtLong);
	CHECK(dm.units == UNIT_TT2000);
	CHECK(*((int64_t*)&dm) == 15625000LL);

	ptrdiff_t aSetShape[VARIDX_MAX];
	CHECK(DasVar_shape(pTime, aSetShape) == 1);
	CHECK(aSetShape[0] == VARIDX_RAGGED);

	/* the generator survives the set: two owners, then one, then zero.  These
	   read DasGen only as an instrument -- the fact under test is that
	   new_DasVar takes a reference and dec_DasVar gives it back. */
	CHECK(DasGen_incRef(pGen) == 3);   /* mine + the var's + this probe */
	CHECK(DasGen_decRef(pGen) == 2);
	CHECK(dec_DasVar(pTime) == 0);
	CHECK(DasGen_decRef(pGen) == 0);
	return nErrs;
}

/* ************************************************************************* */
/* The ownership contract on the two heap objects a variable is built from.
 *
 * das2C has three conventions and they must not be guessed at:
 *
 *   ADDS a reference -- the callee calls incRef.  The count goes up, you still
 *      own the one you made, and you release it when you are done.  Every
 *      constructor does this: new_DasGenAry, new_DasVarBin, DasCodec_init,
 *      DasGen_setArray, and new_DasVar for its generator.
 *
 *   STEALS your reference -- the callee does NOT incRef.  The count is
 *      unchanged and the reference you thought you had is now the callee's.
 *      Do NOT release it.  DasDs_addAry and DasDim_addVar do this, and both
 *      say so, because the object is changing owners for good.
 *
 *   COPIES -- the callee takes a duplicate and your object is untouched.  A
 *      DasForm goes this way and is not reference counted at all: it carries a
 *      per-variable fact (how many components intern= declares), so two
 *      variables on one form would overwrite each other's answer.
 *
 * Guessing wrong gives a double free one way and a leak the other, which is
 * why this is asserted rather than described.  A generator's count is read
 * with a balanced incRef/decRef pair; a form has no count to read, so what is
 * checked instead is that the caller's object still works and is still the
 * caller's to free.
 */
static int test_ctor_ref_contract(void)
{
	int nErrs = 0;

	ptrdiff_t aShape[1] = { VARIDX_RAGGED };
	int64_t nZero = 0LL, nStep = 1000LL;
	DasGen* pGen = new_DasGenSeq(
		etLong, (const ubyte*)&nZero, 1, (const ubyte*)&nStep, aShape
	);
	MUST(pGen != NULL);
	DasForm* pForm = new_DasFormPoint();
	MUST(pForm != NULL);

	/* one reference, freshly made */
	CHECK(DasGen_incRef(pGen) == 2);   CHECK(DasGen_decRef(pGen) == 1);

	DasVar* pVar = new_DasVar(pGen, UNIT_TT2000, pForm);
	MUST(pVar != NULL);

	/* the generator: the variable added its own, mine still stands */
	CHECK(DasGen_incRef(pGen) == 3);     /* mine + the variable's + this probe */
	CHECK(DasGen_decRef(pGen) == 2);

	/* The formalism does NOT follow the generator's rule.  There is no count to
	   probe, so the assertion is behavioural: the variable took a copy, so
	   releasing the variable must leave MY form intact and usable.  If it had
	   taken a reference this would read freed memory. */
	CHECK(dec_DasVar(pVar) == 0);            /* releases its generator and its copy */
	CHECK(DasGen_decRef(pGen) == 0);         /* mine was the last */
	CHECK(DasForm_isKind(pForm, DAS_FORM_POINT));   /* mine, still standing */
	del_DasForm(pForm);                      /* and still mine to free */

	/* A refusal must leave the caller's object alone.  Under the rule above
	   that is simply correct.  Today it happens to hold only because the
	   refusal paths forget to release at all, a known leak: new_DasVar has two
	   such exits and new_DasVarComp four. */
	DasGen* pGen2 = new_DasGenSeq(
		etLong, (const ubyte*)&nZero, 1, (const ubyte*)&nStep, aShape
	);
	MUST(pGen2 != NULL);
	DasForm* pLin = new_DasFormLinear();
	MUST(pLin != NULL);
	CHECK(new_DasVar(pGen2, (das_units)"degrees;degrees;km", pLin) == NULL);
	CHECK(DasForm_isKind(pLin, DAS_FORM_LINEAR));   /* still mine to release */
	del_DasForm(pLin);
	CHECK(DasGen_decRef(pGen2) == 0);

	return nErrs;
}

/* A sequence variable in the given units, for the operand tests below */
static DasVar* _ref_seqVar(double rMin, double rDelta, das_units units)
{
	ptrdiff_t aShape[1] = { VARIDX_RAGGED };
	double aInt[VARIDX_MAX] = {0.0};
	aInt[0] = rDelta;

	DasGen* pGen = new_DasGenSeq(
		etDouble, (const ubyte*)&rMin, 1, (const ubyte*)aInt, aShape
	);
	if(pGen == NULL) return NULL;
	DasForm* pForm = new_DasFormLinear();
	DasVar* pVar = new_DasVar(pGen, units, pForm);
	del_DasForm(pForm);
	DasGen_decRef(pGen);
	return pVar;
}

/* An operation ADDS a reference to each operand and gives both back when it is
   released.  Nothing asserted this before, and it matters: dec_DasVar does
   not dispatch through the vtable, so _DasVarBin_decRef never runs and the
   base version frees the struct while pLeft, pRight and the recipe leak. */
static int test_binop_ref_contract(void)
{
	int nErrs = 0;

	DasVar* pAlt = _ref_seqVar(1000.0, -1.0, Units_fromStr("km"));
	MUST(pAlt != NULL);
	DasVar* pRng = _ref_seqVar(0.0, 0.5, Units_fromStr("km"));
	MUST(pRng != NULL);

	CHECK(inc_DasVar(pAlt) == 2);   CHECK(dec_DasVar(pAlt) == 1);
	CHECK(inc_DasVar(pRng) == 2);   CHECK(dec_DasVar(pRng) == 1);

	DasVarBin* pDiff = new_DasVarBin(pAlt, '-', pRng);
	MUST(pDiff != NULL);

	CHECK(inc_DasVar(pAlt) == 3);   /* mine + the operation's + this probe */
	CHECK(dec_DasVar(pAlt) == 2);
	CHECK(inc_DasVar(pRng) == 3);
	CHECK(dec_DasVar(pRng) == 2);

	/* releasing the operation hands both operands back */
	CHECK(dec_DasVar((DasVar*)pDiff) == 0);
	CHECK(inc_DasVar(pAlt) == 2);   CHECK(dec_DasVar(pAlt) == 1);
	CHECK(inc_DasVar(pRng) == 2);   CHECK(dec_DasVar(pRng) == 1);

	CHECK(dec_DasVar(pAlt) == 0);
	CHECK(dec_DasVar(pRng) == 0);
	return nErrs;
}

/* Case 5: the byte run branch, string sentinel vs blob ptr+len */
static int test_byte_runs(void)
{
	int nErrs = 0;

	/* fixed-width 8-char strings, null padded: RANK_2(records, 8) */
	ubyte fill = 0;
	DasAry* pAry = new_DasAry(
		"modes", vtUByte, 0, &fill, RANK_2(0, 8), UNIT_DIMENSIONLESS
	);
	MUST(pAry != NULL);
	MUST(DasAry_append(pAry, (const ubyte*)"SURVEY\0\0", 8) != NULL);
	MUST(DasAry_append(pAry, (const ubyte*)"BURST\0\0\0", 8) != NULL);

	int8_t aMap[1] = { 0 };
	DasGen* pGen = new_DasGenAry(pAry, 1, aMap);
	MUST(pGen != NULL);

	DasVarBytes* pStr = new_DasVarBytes(pGen, NULL, true /* sentinel */, 8);
	MUST(pStr != NULL);
	CHECK(strcmp(DasVar_element((DasVar*)pStr), "bytes") == 0);
	CHECK(DasVar_valType((DasVar*)pStr) == vtText);   /* sentinel => a string */
	CHECK(!DasVar_isNumeric((DasVar*)pStr));

	ptrdiff_t aLoc[1] = { 1 };
	das_datum dm = {{0},vtUnknown,0,NULL};
	MUST(DasVar_get((DasVar*)pStr, aLoc, DAS_BS_NULL, &dm) == 0);   /* sVal is read below */
	CHECK(dm.vt == vtText);
	const char* sVal = NULL;
	memcpy(&sVal, &dm, sizeof(const char*));
	MUST(sVal != NULL);
	CHECK(strcmp(sVal, "BURST") == 0);

	/* the same bytes as a blob: no sentinel promise, pointer plus length */
	DasVarBytes* pBlob = new_DasVarBytes(pGen, NULL, false /* no sentinel */, 8);
	MUST(pBlob != NULL);
	CHECK(DasVar_valType((DasVar*)pBlob) == vtByteSeq);   /* no sentinel */
	aLoc[0] = 0;
	MUST(DasVar_get((DasVar*)pBlob, aLoc, DAS_BS_NULL, &dm) == 0);   /* bs.ptr is read below */
	CHECK(dm.vt == vtByteSeq);
	das_byte_seq bs;
	memcpy(&bs, &dm, sizeof(das_byte_seq));
	CHECK(bs.sz == 8);
	MUST(bs.ptr != NULL);
	CHECK(memcmp(bs.ptr, "SURVEY\0\0", 8) == 0);

	/* A byte run cannot carry math, and that is a property of the CLASS rather
	   than a rejected argument: new_DasVarBytes takes no formalism parameter at
	   all, so there is no rule to look up and nothing to refuse. */
	CHECK(DasVar_form((DasVar*)pStr) == NULL);
	CHECK(DasVar_form((DasVar*)pBlob) == NULL);

	CHECK(dec_DasVar((DasVar*)pStr) == 0);
	CHECK(dec_DasVar((DasVar*)pBlob) == 0);
	CHECK(DasGen_decRef(pGen) == 0);
	dec_DasAry(pAry);
	return nErrs;
}

/* Case 3: a vector composite packs a vtComposite datum; a plain composite
   refuses single-datum packing loudly */
static int test_composite(void)
{
	int nErrs = 0;

	float fill = -1.0f;
	DasAry* pAry = new_DasAry(
		"b_vec", vtFloat, 0, (const ubyte*)&fill, RANK_2(0, 3), UNIT_NT
	);
	MUST(pAry != NULL);
	float aVals[6] = { 1.5f, -2.5f, 3.5f, 4.0f, 5.0f, 6.0f };
	MUST(DasAry_append(pAry, (const ubyte*)aVals, 6) != NULL);

	int8_t aMap[1] = { 0 };
	DasGen* pGen = new_DasGenAry(pAry, 1, aMap);
	MUST(pGen != NULL);

	/* The form is built complete and handed over; a var takes the reference it
	   is given.  There is no bind-after-construction step -- the form's own
	   constructor is where frame, system and component order are settled. */
	ptrdiff_t aIntShape[1] = { 3 };
	DasForm* pFormVec = new_DasFormVector(
		"TSCS", DAS_VSYS_CART, NULL
	);
	MUST(pFormVec != NULL);
	DasVarComp* pVec = new_DasVarComp(pGen, UNIT_NT, pFormVec, 1, aIntShape);
	del_DasForm(pFormVec);          /* the variable copied it */
	MUST(pVec != NULL);

	CHECK(strcmp(DasVar_element((DasVar*)pVec), "composite") == 0);
	CHECK(DasVar_isNumeric((DasVar*)pVec));
	MUST(DasVar_form((DasVar*)pVec) != NULL);
	CHECK(DasVar_formIs((DasVar*)pVec, DAS_FORM_VEC));
	CHECK(strcmp(DasFormVector_frame(DasVar_form((DasVar*)pVec)), "TSCS") == 0);

	ptrdiff_t aLoc[1] = { 1 };
	das_datum dm = {{0},vtUnknown,0,NULL};
	MUST(DasVar_get((DasVar*)pVec, aLoc, DAS_BS_NULL, &dm) == 0);   /* aComp is read below */
	CHECK(dm.vt == vtComposite);
	CHECK(dm.units == UNIT_NT);
	double aComp[3];
	MUST(das_datum_toDoubles(&dm, aComp, 3) == 3);
	CHECK((aComp[0] == 4.0)&&(aComp[1] == 5.0)&&(aComp[2] == 6.0));

	/* Anything subset() can hand out, get() can too.  A linear composite is a
	   bare numeric run with no richer meaning, and it still boxes as a datum
	   whose components read back in storage order.  A composite always HAS a
	   form -- new_DasVarComp rejects NULL -- so "plain" means linear, not
	   formless. */
	DasForm* pFormLin = new_DasFormLinear();
	MUST(pFormLin != NULL);
	DasVarComp* pPlain = new_DasVarComp(pGen, UNIT_NT, pFormLin, 1, aIntShape);
	del_DasForm(pFormLin);
	MUST(pPlain != NULL);
	CHECK(DasVar_formIs((DasVar*)pPlain, DAS_FORM_LINEAR));
	CHECK(DasVar_valType((DasVar*)pPlain) == vtComposite);
	memset(&dm, 0, sizeof(dm));
	MUST(DasVar_get((DasVar*)pPlain, aLoc, DAS_BS_NULL, &dm) == 0);
	CHECK(dm.vt == vtComposite);
	CHECK(das_datum_elemType(&dm) == vtFloat);
	aComp[0] = aComp[1] = aComp[2] = 0.0;
	MUST(das_datum_toDoubles(&dm, aComp, 3) == 3);
	CHECK((aComp[0] == 4.0)&&(aComp[1] == 5.0)&&(aComp[2] == 6.0));

	/* The same for a kind das2C has never heard of: the generic form carries
	   its parameters and the numbers are still readable. */
	const char* aOdd[] = {"kind","whatsadoodle", "flavor","strange", NULL};
	DasForm* pFormOdd = new_DasForm_pairs(aOdd);
	MUST(pFormOdd != NULL);
	DasVarComp* pOdd = new_DasVarComp(pGen, UNIT_NT, pFormOdd, 1, aIntShape);
	del_DasForm(pFormOdd);
	MUST(pOdd != NULL);
	CHECK(DasVar_formIs((DasVar*)pOdd, DAS_FORM_EXT));
	CHECK(DasVar_valType((DasVar*)pOdd) == vtComposite);
	memset(&dm, 0, sizeof(dm));
	MUST(DasVar_get((DasVar*)pOdd, aLoc, DAS_BS_NULL, &dm) == 0);
	CHECK(dm.vt == vtComposite);
	CHECK(strcmp(DasForm_getParam(das_datum_form(&dm), "flavor", NULL), "strange") == 0);
	aComp[0] = aComp[1] = aComp[2] = 0.0;
	MUST(das_datum_toDoubles(&dm, aComp, 3) == 3);
	CHECK((aComp[0] == 4.0)&&(aComp[1] == 5.0)&&(aComp[2] == 6.0));

	CHECK(dec_DasVar((DasVar*)pVec) == 0);
	CHECK(dec_DasVar((DasVar*)pPlain) == 0);
	CHECK(dec_DasVar((DasVar*)pOdd) == 0);
	CHECK(DasGen_decRef(pGen) == 0);
	dec_DasAry(pAry);
	return nErrs;
}

/* Units ruling: a ';' units list fails loud rather than misstating data */
static int test_units_list_refused(void)
{
	int nErrs = 0;

	ptrdiff_t aShape[1] = { VARIDX_RAGGED };
	double rVal = 1.0;
	DasGen* pGen = new_DasGenConst(etDouble, (const ubyte*)&rVal, 1, aShape);
	MUST(pGen != NULL);

	/* The interning layer (Units_fromStr) already refuses a ';' list, so probe
	   the variable-level guard directly with a raw string.  The form is a real
	   one on purpose: pass NULL and new_DasVar refuses for THAT reason first
	   and the units rule never gets exercised. */
	DasForm* pForm = new_DasFormLinear();
	MUST(pForm != NULL);
	DasVar* pVar = new_DasVar(pGen, (das_units)"degrees;degrees;km", pForm);
	CHECK(pVar == NULL);
	if(pVar != NULL) dec_DasVar(pVar);
	del_DasForm(pForm);               /* a refusal leaves our object alone */

	CHECK(DasGen_decRef(pGen) == 0);
	return nErrs;
}

/* ************************************************************************* */
/* Case 9: DasVar_subset / DasVar_materialize over the MARSIS sounder data.
 *
 * A rank-3 (3, 160, 80) index space built from an array set, two sequence
 * sets and two operator sets -- the shape real sounder data arrives in.
 */

/* the ionogram index space */
#define AIS_RECS  3
#define AIS_FREQS 160
#define AIS_ECHOS 80

#define DEGEN VARIDX_UNUSED

typedef struct ais_sets {
	DasAry *pAryTime, *pAryEcho, *pAryMexAlt;
	DasVar *pTime, *pEcho, *pMexAlt;   /* array backed */
	DasVar *pPulseOff, *pDelay, *pRange;  /* sequences */
	DasVar *pPulseTime, *pAppAlt;      /* operators */
} ais_sets;

/* pForm may be NULL here as a convenience meaning "the plain linear form".
   new_DasVar itself refuses NULL: a scalar always has a formalism.
   This helper RELEASES the form for you, so callers may pass one inline. */
static DasVar* _ais_aryVar(DasAry* pAry, int8_t i0, int8_t i1, int8_t i2,
                           das_units units, DasForm* pForm)
{
	int8_t aMap[3] = { i0, i1, i2 };
	DasGen* pGen = new_DasGenAry(pAry, 3, aMap);
	if(pGen == NULL){
		if(pForm != NULL) del_DasForm(pForm);
		return NULL;
	}
	if(pForm == NULL) pForm = new_DasFormLinear();
	DasVar* pVar = new_DasVar(pGen, units, pForm);
	del_DasForm(pForm);   /* the var copied it */
	DasGen_decRef(pGen);     /* likewise */
	return pVar;
}

static DasVar* _ais_seqVar(double rMin, double rDelta, int nIdx, das_units units)
{
	/* One slope per external index; only nIdx moves the value, so only nIdx is
	   an index this sequence has any opinion about.  It BORROWS its length
	   there and is UNUSED everywhere else -- a sequence cannot be internally
	   ragged, it is a rule defined everywhere, so '*' is never its own answer. */
	double aIntervals[VARIDX_MAX] = {0.0};
	aIntervals[nIdx] = rDelta;
	ptrdiff_t aShape[3] = { VARIDX_UNUSED, VARIDX_UNUSED, VARIDX_UNUSED };
	aShape[nIdx] = VARIDX_BORROW;

	DasGen* pGen = new_DasGenSeq(
		etDouble, (const ubyte*)&rMin, 3, (const ubyte*)aIntervals, aShape
	);
	if(pGen == NULL) return NULL;
	DasForm* pForm = new_DasFormLinear();
	DasVar* pVar = new_DasVar(pGen, units, pForm);
	del_DasForm(pForm);
	DasGen_decRef(pGen);
	return pVar;
}

/* Every check here is a MUST: this builds the fixture the cases then read, so
   a miss leaves a null the caller would dereference.  The struct is zeroed
   first and _ais_free tolerates nulls, so an early return still cleans up. */
static int _ais_build(ais_sets* p)
{
	int nErrs = 0;

	memset(p, 0, sizeof(ais_sets));
	const float rFill = DAS_FILL_VALUE;
	const ubyte* pFill = (const ubyte*)&rFill;

	/* index 0: one ionogram start time per record */
	p->pAryTime = new_DasAry("rec_time", vtTime, 0, NULL, RANK_1(0), UNIT_UTC);
	MUST(p->pAryTime != NULL);
	das_time dt;
	for(int i = 0; i < AIS_RECS; ++i){
		dt_parsetime(g_aTimes[i], &dt);
		MUST(DasAry_append(p->pAryTime, (const ubyte*)(&dt), 1) != NULL);
	}
	p->pTime = _ais_aryVar(
		p->pAryTime, 0, DEGEN, DEGEN, UNIT_UTC, new_DasFormPoint()
	);
	MUST(p->pTime != NULL);

	/* indices 0,1,2: the echo amplitudes themselves */
	p->pAryEcho = new_DasAry(
		"echo", vtFloat, 0, pFill, RANK_3(0, AIS_FREQS, AIS_ECHOS),
		Units_fromStr("V**2 m**-2 Hz**-1")
	);
	MUST(p->pAryEcho != NULL);
	MUST(DasAry_append(
		p->pAryEcho, (const ubyte*)g_aAmp, AIS_RECS*AIS_FREQS*AIS_ECHOS
	) != NULL);
	p->pEcho = _ais_aryVar(
		p->pAryEcho, 0, 1, 2, Units_fromStr("V**2 m**-2 Hz**-1"), NULL
	);
	MUST(p->pEcho != NULL);

	/* index 0: spacecraft altitude, the reference for apparent altitude */
	p->pAryMexAlt = new_DasAry(
		"mex_alt", vtFloat, 0, pFill, RANK_1(0), Units_fromStr("km")
	);
	MUST(p->pAryMexAlt != NULL);
	MUST(DasAry_append(
		p->pAryMexAlt, (const ubyte*)g_aMexAlt, AIS_RECS) != NULL
	);
	p->pMexAlt = _ais_aryVar(
		p->pAryMexAlt, 0, DEGEN, DEGEN, Units_fromStr("km"), NULL
	);
	MUST(p->pMexAlt != NULL);

	/* index 1: pulse repetition offsets.  index 2: echo delay and the range
	   it implies at the speed of light. */
	p->pPulseOff = _ais_seqVar(0.0, 7.86, 1, Units_fromStr("ms"));
	MUST(p->pPulseOff != NULL);
	p->pDelay = _ais_seqVar(167.443, 91.4286, 2, Units_fromStr("microsecond"));
	MUST(p->pDelay != NULL);

	const double C = 299792458.0 * 1.0e-9;   /* km per microsecond */
	p->pRange = _ais_seqVar(
		167.443 * 0.5 * C, 91.4286 * 0.5 * C, 2, Units_fromStr("km")
	);
	MUST(p->pRange != NULL);

	/* the two computed variables: a time axis and an altitude axis */
	p->pPulseTime = (DasVar*)new_DasVarBin(p->pTime, '+', p->pPulseOff);
	MUST(p->pPulseTime != NULL);
	p->pAppAlt = (DasVar*)new_DasVarBin(p->pMexAlt, '-', p->pRange);
	MUST(p->pAppAlt != NULL);

	return nErrs;
}

static void _ais_free(ais_sets* p)
{
	if(p->pAppAlt)    dec_DasVar(p->pAppAlt);
	if(p->pPulseTime) dec_DasVar(p->pPulseTime);
	if(p->pRange)     dec_DasVar(p->pRange);
	if(p->pDelay)     dec_DasVar(p->pDelay);
	if(p->pPulseOff)  dec_DasVar(p->pPulseOff);
	if(p->pMexAlt)    dec_DasVar(p->pMexAlt);
	if(p->pEcho)      dec_DasVar(p->pEcho);
	if(p->pTime)      dec_DasVar(p->pTime);
	if(p->pAryMexAlt) dec_DasAry(p->pAryMexAlt);
	if(p->pAryEcho)   dec_DasAry(p->pAryEcho);
	if(p->pAryTime)   dec_DasAry(p->pAryTime);
}

/* Subset values: every slice asserted element for element, not just shaped. */
static int test_subset_values(void)
{
	int nErrs = 0;

	ais_sets s;
	int nRet = _ais_build(&s);
	if(nRet != 0){ _ais_free(&s); return nRet; }

	const double C = 299792458.0 * 1.0e-9;
	ptrdiff_t aMin[3], aMax[3];
	size_t uVals = 0;

	/* Case 1: apparent altitude over all echoes of the first frequency.
	   app_alt = mex_alt[i] - range[k], so it varies along i and k only. */
	aMin[0]=0; aMax[0]=AIS_RECS; aMin[1]=0; aMax[1]=1; aMin[2]=0; aMax[2]=AIS_ECHOS;
	DasAry* pSlice = DasVar_subset(s.pAppAlt, 3, aMin, aMax, NULL);
	MUST(pSlice != NULL);
	ptrdiff_t aShape[VARIDX_MAX];
	CHECK(DasAry_shape(pSlice, aShape) == 2);   /* the size-1 index drops out */
	CHECK((aShape[0] == AIS_RECS)&&(aShape[1] == AIS_ECHOS));
	CHECK(DasAry_units(pSlice) == Units_fromStr("km"));
	{
		const double* pVals = DasAry_getDoublesIn(pSlice, DIM0, &uVals);
		MUST(pVals != NULL);
		CHECK(uVals == (size_t)(AIS_RECS*AIS_ECHOS));
		/* record 0, echo 0 and the last echo of record 2.  Note the temps:
		   das_within() does not parenthesize its arguments, so an expression
		   passed straight in re-associates and silently compares nonsense. */
		double rFirst = g_aMexAlt[0] - 167.443*0.5*C;
		double rLast  = g_aMexAlt[2] - (167.443 + 91.4286*(AIS_ECHOS-1))*0.5*C;
		CHECK(das_within(pVals[0], rFirst, 1e-3));
		CHECK(das_within(pVals[uVals-1], rLast, 1e-3));
	}
	dec_DasAry(pSlice);

	/* Case 2: same set pinned to the LAST echo, swept over all frequencies.
	   The frequency index is degenerate for app_alt, so every value along it
	   must repeat: this is the check the printing original never made. */
	aMin[0]=0; aMax[0]=AIS_RECS; aMin[1]=0; aMax[1]=AIS_FREQS;
	aMin[2]=AIS_ECHOS-1; aMax[2]=AIS_ECHOS;
	pSlice = DasVar_subset(s.pAppAlt, 3, aMin, aMax, NULL);
	MUST(pSlice != NULL);
	CHECK(DasAry_shape(pSlice, aShape) == 2);
	CHECK((aShape[0] == AIS_RECS)&&(aShape[1] == AIS_FREQS));
	{
		const double* pVals = DasAry_getDoublesIn(pSlice, DIM0, &uVals);
		CHECK(uVals == (size_t)(AIS_RECS*AIS_FREQS));
		for(int i = 0; i < AIS_RECS; ++i){
			double rWant = g_aMexAlt[i]
			             - (167.443 + 91.4286*(AIS_ECHOS-1))*0.5*C;
			for(int j = 0; j < AIS_FREQS; ++j)
				CHECK(das_within(pVals[i*AIS_FREQS + j], rWant, 1e-3));
		}
	}
	dec_DasAry(pSlice);

	/* Case 3: pinned on both the last frequency and the last echo, leaving a
	   rank-1 result down the record index. */
	aMin[0]=0; aMax[0]=AIS_RECS; aMin[1]=AIS_FREQS-1; aMax[1]=AIS_FREQS;
	aMin[2]=AIS_ECHOS-1; aMax[2]=AIS_ECHOS;
	pSlice = DasVar_subset(s.pAppAlt, 3, aMin, aMax, NULL);
	MUST(pSlice != NULL);
	CHECK(DasAry_shape(pSlice, aShape) == 1);
	CHECK(aShape[0] == AIS_RECS);
	dec_DasAry(pSlice);

	/* Case 4: the operator set over a time axis.  Sequences and operators are
	   forced to concrete values, which is the whole point of subset. */
	aMin[0]=0; aMax[0]=AIS_RECS; aMin[1]=0; aMax[1]=4; aMin[2]=0; aMax[2]=1;
	pSlice = DasVar_subset(s.pPulseTime, 3, aMin, aMax, NULL);
	MUST(pSlice != NULL);
	CHECK(DasAry_shape(pSlice, aShape) == 2);
	CHECK((aShape[0] == AIS_RECS)&&(aShape[1] == 4));
	dec_DasAry(pSlice);

	/* Case 5: array set, one contiguous block of echoes.  This is the shape
	   the zero-copy path is built for. */
	aMin[0]=1; aMax[0]=2; aMin[1]=1; aMax[1]=2; aMin[2]=0; aMax[2]=AIS_ECHOS;
	pSlice = DasVar_subset(s.pEcho, 3, aMin, aMax, NULL);
	MUST(pSlice != NULL);
	{
		const float* pVals = DasAry_getFloatsIn(pSlice, DIM0, &uVals);
		MUST(pVals != NULL);
		CHECK(uVals == (size_t)AIS_ECHOS);
		for(int k = 0; k < AIS_ECHOS; ++k)
			CHECK(pVals[k] == g_aAmp[1][1][k]);
	}
	dec_DasAry(pSlice);

	/* Case 6: array set, the last echo of every pulse of every record.  A
	   strided gather, not a block, so it exercises the copy path. */
	aMin[0]=0; aMax[0]=AIS_RECS; aMin[1]=0; aMax[1]=AIS_FREQS;
	aMin[2]=AIS_ECHOS-1; aMax[2]=AIS_ECHOS;
	pSlice = DasVar_subset(s.pEcho, 3, aMin, aMax, NULL);
	MUST(pSlice != NULL);
	CHECK(DasAry_shape(pSlice, aShape) == 2);
	CHECK((aShape[0] == AIS_RECS)&&(aShape[1] == AIS_FREQS));
	{
		const float* pVals = DasAry_getFloatsIn(pSlice, DIM0, &uVals);
		CHECK(uVals == (size_t)(AIS_RECS*AIS_FREQS));
		for(int i = 0; i < AIS_RECS; ++i)
			for(int j = 0; j < AIS_FREQS; ++j)
				CHECK(pVals[i*AIS_FREQS + j] == g_aAmp[i][j][AIS_ECHOS-1]);
	}
	dec_DasAry(pSlice);

	_ais_free(&s);
	return nErrs;
}

/* The refusals: bad rank, rank-0 output, and a range past the end. */
static int test_subset_refusals(void)
{
	int nErrs = 0;

	ais_sets s;
	int nRet = _ais_build(&s);
	if(nRet != 0){ _ais_free(&s); return nRet; }

	ptrdiff_t aMin[3], aMax[3];

	/* Case 7: a range specification of the wrong rank is refused by every
	   kind of set. */
	aMin[0]=0; aMax[0]=AIS_RECS;
	CHECK(DasVar_subset(s.pEcho,   1, aMin, aMax, NULL) == NULL);
	CHECK(DasVar_subset(s.pDelay,  1, aMin, aMax, NULL) == NULL);
	CHECK(DasVar_subset(s.pAppAlt, 1, aMin, aMax, NULL) == NULL);

	/* Case 8: an all-singleton range would be rank 0.  That is DasVar_get()'s
	   job, and subset says so rather than inventing a rank-1 array. */
	aMin[0]=0; aMax[0]=1; aMin[1]=0; aMax[1]=1; aMin[2]=0; aMax[2]=1;
	CHECK(DasVar_subset(s.pEcho,   3, aMin, aMax, NULL) == NULL);
	CHECK(DasVar_subset(s.pDelay,  3, aMin, aMax, NULL) == NULL);
	CHECK(DasVar_subset(s.pAppAlt, 3, aMin, aMax, NULL) == NULL);

	/* Case 9: running off the end of real storage is an error... */
	aMin[0]=0; aMax[0]=1; aMin[1]=0; aMax[1]=1000; aMin[2]=0; aMax[2]=1;
	CHECK(DasVar_subset(s.pEcho, 3, aMin, aMax, NULL) == NULL);

	/* ...but a sequence is a rule, not a store, and does not care how far it
	   is asked to run. */
	DasAry* pSlice = DasVar_subset(s.pDelay, 3, aMin, aMax, NULL);
	CHECK(pSlice != NULL);
	if(pSlice != NULL) dec_DasAry(pSlice);

	/* And an operator over an array on index 0 and a sequence on index 2 is
	   indifferent to a wild index 1, because neither operand reads it. */
	aMin[0]=0; aMax[0]=1; aMin[1]=0; aMax[1]=1000; aMin[2]=90; aMax[2]=100;
	pSlice = DasVar_subset(s.pAppAlt, 3, aMin, aMax, NULL);
	CHECK(pSlice != NULL);
	if(pSlice != NULL) dec_DasAry(pSlice);

	_ais_free(&s);
	return nErrs;
}

/* Coverage the retired tests never had: view/copy agreement, the refcount
   contract, and independence of the copy. */
/* The three ways to say "everything", and who supplies the extent.
 *
 * A NULL bound pair means the whole extent.  The variable answers for itself
 * when it can -- most sequences declare a concrete extent on the wire -- and
 * a BORROWing sequence cannot, so it either gets named bounds or is told whose
 * shape to wear.  pShapeFrom is a VARIABLE because only a variable knows which
 * of its indices are degenerate; an array has no such concept. */
static int test_subset_whole(void)
{
	int nErrs = 0;

	ais_sets s;
	int nRet = _ais_build(&s);
	if(nRet != 0){ _ais_free(&s); return nRet; }

	ptrdiff_t aShape[VARIDX_MAX];

	/* An array backed variable knows its own extent, so no bounds are needed
	   and the macro is the whole call. */
	DasAry* pAll = DasVar_allVals(s.pEcho, 3);
	MUST(pAll != NULL);
	CHECK(DasAry_shape(pAll, aShape) == 3);
	CHECK((aShape[0] == AIS_RECS)&&(aShape[1] == AIS_FREQS)&&(aShape[2] == AIS_ECHOS));
	dec_DasAry(pAll);

	/* A sequence that BORROWS has no extent of its own to give.  Refused by
	   name, with the parameter that fixes it. */
	CHECK(DasVar_allVals(s.pPulseOff, 3) == NULL);

	/* ...unless it is told whose shape to wear.  A variable is DATASET shaped,
	   so the answer is the model's full extent: pPulseOff varies only along
	   index 1, and along the other two it REPEATS with a step size of zero
	   rather than vanishing.  That is what lets every variable answer at every
	   position in the dataset (das3/variable.md, "Degeneracy alters the step
	   size").  A caller wanting only the distinct values pins the degenerate
	   indices itself, the way das3_cdf does. */
	DasAry* pWorn = DasVar_subset(s.pPulseOff, 3, NULL, NULL, s.pEcho);
	MUST(pWorn != NULL);
	CHECK(DasAry_shape(pWorn, aShape) == 3);
	CHECK((aShape[0] == AIS_RECS)&&(aShape[1] == AIS_FREQS)&&(aShape[2] == AIS_ECHOS));
	dec_DasAry(pWorn);

	/* the compact form is one restricted range away, and IS still rank 1 */
	ptrdiff_t aPin[3] = {0,0,0}, aOne[3] = {1, AIS_FREQS, 1};
	DasAry* pPinned = DasVar_subset(s.pPulseOff, 3, aPin, aOne, NULL);
	MUST(pPinned != NULL);
	CHECK(DasAry_shape(pPinned, aShape) == 1);
	CHECK(aShape[0] == AIS_FREQS);
	dec_DasAry(pPinned);

	/* materialize takes the same arguments and adds one guarantee: the result
	   is never a view, so it may be written and it outlives the variable. */
	DasAry* pOwn = DasVar_materialize(s.pEcho, 3, NULL, NULL, NULL);
	MUST(pOwn != NULL);
	CHECK(DasAry_ownsElements(pOwn));
	dec_DasAry(pOwn);

	/* subset over the same range may lend instead, and that is the difference */
	DasAry* pLent = DasVar_allVals(s.pEcho, 3);
	MUST(pLent != NULL);
	CHECK(!DasAry_ownsElements(pLent));
	dec_DasAry(pLent);

	_ais_free(&s);
	return nErrs;
}

static int test_subset_view_vs_copy(void)
{
	int nErrs = 0;

	ais_sets s;
	int nRet = _ais_build(&s);
	if(nRet != 0){ _ais_free(&s); return nRet; }

	/* A block request on an array set: the shape the zero-copy path takes. */
	ptrdiff_t aMin[3] = {1, 1, 0};
	ptrdiff_t aMax[3] = {2, 2, AIS_ECHOS};

	/* This block is contiguous in the backing store, so subset LENDS it.  The
	   same range off the computed variable has nothing to lend and allocates.
	   Both come back as a DasAry; DasAry_ownsElements() is how a caller that
	   cares which one it got finds out. */
	DasAry* pView = DasVar_subset(s.pEcho,   3, aMin, aMax, NULL);
	DasAry* pOwn  = DasVar_subset(s.pAppAlt, 3, aMin, aMax, NULL);
	MUST(pView != NULL);
	MUST(pOwn  != NULL);

	CHECK(!DasAry_ownsElements(pView));   /* lent: the echo array's own memory */
	CHECK(DasAry_ownsElements(pOwn));     /* computed: nothing to lend */

	/* Whichever path each took, the caller's duty is identical. */
	CHECK(ref_DasAry(pView) == 1);
	CHECK(ref_DasAry(pOwn)  == 1);

	size_t uView = 0;
	const float* pV = DasAry_getFloatsIn(pView, DIM0, &uView);
	MUST(pV != NULL);
	float rWas = g_aAmp[1][1][0];
	CHECK(pV[0] == rWas);

	/* An owned result may be written without disturbing anything upstream. */
	size_t uOwn = 0;
	const double* pO = DasAry_getDoublesIn(pOwn, DIM0, &uOwn);
	MUST(pO != NULL);
	((double*)pO)[0] = -12345.0;
	CHECK(DasAry_getFloatAt(s.pAryEcho, IDX2(1,1,0)) == rWas);
	dec_DasAry(pOwn);

	/* A view pins its backing store, so it stays readable afterward. */
	CHECK(pV[0] == rWas);
	dec_DasAry(pView);

	_ais_free(&s);
	return nErrs;
}

/* Ragged storage, read both ways.  A full cube fixture cannot reach either
   path, and this is the only synthetic model with real raggedness, so an
   untested path here is an untested path everywhere.

   The two axes are independent: Qube squares off with fill, plain subset keeps
   the rows the length they really are.  Both are asked the same questions here
   so the difference is the only thing that shows. */
static int test_subset_ragged(void)
{
	int nErrs = 0;

	float rFill = -99.0f;
	DasAry* pAry = new_DasAry(
		"ragged", vtFloat, 0, (const ubyte*)&rFill, RANK_2(0,0), UNIT_DIMENSIONLESS
	);
	MUST(pAry != NULL);

	/* three rows of different length: 4, 2, 5 */
	const int nRows = 3;
	const int aLens[3] = {4, 2, 5};
	float rVal = 0.0f;
	for(int i = 0; i < nRows; ++i){
		for(int j = 0; j < aLens[i]; ++j){
			rVal = (float)(100*i + j);
			CHECK(DasAry_append(pAry, (const ubyte*)&rVal, 1) != NULL);
		}
		DasAry_markEnd(pAry, DIM1);
	}

	int8_t aMap[2] = { 0, 1 };
	DasGen* pGen = new_DasGenAry(pAry, 2, aMap);
	CHECK(pGen != NULL);
	DasVar* pSet = _linearVar(pGen, UNIT_DIMENSIONLESS);
	CHECK(pSet != NULL);
	DasGen_decRef(pGen);

	/* the set reports the raggedness... */
	ptrdiff_t aSetShape[VARIDX_MAX];
	CHECK(DasVar_shape(pSet, aSetShape) == 2);
	CHECK(aSetShape[0] == nRows);
	CHECK(aSetShape[1] == VARIDX_RAGGED);

	/* ...and a QUBE of it is square, with fill standing in wherever a row ran
	   out.  Ask for 5 wide, which only row 2 actually has. */
	ptrdiff_t aMin[2] = {0, 0};
	ptrdiff_t aMax[2] = {nRows, 5};
	DasAry* pSlice = DasVar_subsetQube(pSet, 2, aMin, aMax, NULL);
	MUST(pSlice != NULL);

	ptrdiff_t aShape[VARIDX_MAX];
	CHECK(DasAry_shape(pSlice, aShape) == 2);
	CHECK((aShape[0] == nRows)&&(aShape[1] == 5));   /* never VARIDX_RAGGED */

	size_t uVals = 0;
	const float* pVals = DasAry_getFloatsIn(pSlice, DIM0, &uVals);
	MUST(pVals != NULL);
	CHECK(uVals == (size_t)(nRows*5));
	for(int i = 0; i < nRows; ++i){
		for(int j = 0; j < 5; ++j){
			float rWant = (j < aLens[i]) ? (float)(100*i + j) : rFill;
			CHECK(pVals[i*5 + j] == rWant);
		}
	}
	dec_DasAry(pSlice);

	/* Asking for less than the shortest row needs no fill at all. */
	aMax[1] = 2;
	pSlice = DasVar_subsetQube(pSet, 2, aMin, aMax, NULL);
	MUST(pSlice != NULL);
	pVals = DasAry_getFloatsIn(pSlice, DIM0, &uVals);
	CHECK(uVals == (size_t)(nRows*2));
	for(int i = 0; i < nRows; ++i)
		for(int j = 0; j < 2; ++j)
			CHECK(pVals[i*2 + j] == (float)(100*i + j));
	dec_DasAry(pSlice);

	/* No bounds at all: a Qube measures the widest row and squares off to it,
	   which is the whole reason a ragged extent is no longer a refusal. */
	pSlice = DasVar_subsetQube(pSet, 2, NULL, NULL, NULL);
	MUST(pSlice != NULL);
	CHECK(DasAry_shape(pSlice, aShape) == 2);
	CHECK((aShape[0] == nRows)&&(aShape[1] == 5));   /* 5 == max(4,2,5) */
	dec_DasAry(pSlice);

	/* The other axis.  Plain subset keeps the rows the lengths they are, so
	   the result reports RAGGED and carries no fill at all -- 11 values, not
	   the 15 a rectangle would need. */
	DasAry* pNat = DasVar_materialize(pSet, 2, NULL, NULL, NULL);
	MUST(pNat != NULL);
	CHECK(DasAry_shape(pNat, aShape) == 2);
	CHECK(aShape[0] == nRows);
	CHECK(aShape[1] == VARIDX_RAGGED);
	CHECK(DasAry_ownsElements(pNat));

	ptrdiff_t aRow[VARIDX_MAX] = VARIDX_INIT_BEGIN;
	for(int i = 0; i < nRows; ++i){
		aRow[0] = i;
		CHECK((int)DasAry_lengthIn(pNat, 1, aRow) == aLens[i]);
	}

	pVals = DasAry_getFloatsIn(pNat, DIM0, &uVals);
	MUST(pVals != NULL);
	CHECK(uVals == (size_t)(aLens[0] + aLens[1] + aLens[2]));

	size_t uAt = 0;
	for(int i = 0; i < nRows; ++i)
		for(int j = 0; j < aLens[i]; ++j, ++uAt)
			CHECK(pVals[uAt] == (float)(100*i + j));
	dec_DasAry(pNat);

	/* A full-extent subset can lend that same ragged store outright, which is
	   the cheap read the natural axis exists to make possible. */
	DasAry* pLent = DasVar_allVals(pSet, 2);
	MUST(pLent != NULL);
	CHECK(DasAry_shape(pLent, aShape) == 2);
	CHECK(aShape[1] == VARIDX_RAGGED);
	CHECK(!DasAry_ownsElements(pLent));
	dec_DasAry(pLent);

	CHECK(dec_DasVar(pSet) == 0);
	dec_DasAry(pAry);
	return nErrs;
}

/* A composite subset yields ELEMENTS with the component index trailing, not
   packed composite datums.  The retired layer disagreed with itself here:
   its striding path unpacked the vector type to the component type while its
   cell-by-cell path did not, so a ragged vector taking the slow path copied
   the wrong width.  No TRACERS data ever had ragged vectors, so nothing
   caught it. */
static int test_subset_composite(void)
{
	int nErrs = 0;

	float rFill = -1.0f;
	DasAry* pAry = new_DasAry(
		"b_vec", vtFloat, 0, (const ubyte*)&rFill, RANK_2(0,3), UNIT_NT
	);
	MUST(pAry != NULL);
	float aVals[12];
	for(int i = 0; i < 12; ++i) aVals[i] = (float)i;
	CHECK(DasAry_append(pAry, (const ubyte*)aVals, 12) != NULL);

	int8_t aMap[1] = { 0 };
	DasGen* pGen = new_DasGenAry(pAry, 1, aMap);
	CHECK(pGen != NULL);

	ptrdiff_t aIntShape[1] = { 3 };
	DasForm* pFormVec = new_DasFormVector(
		"TSCS", DAS_VSYS_CART, NULL
	);
	MUST(pFormVec != NULL);
	DasVarComp* pVec = new_DasVarComp(pGen, UNIT_NT, pFormVec, 1, aIntShape);
	del_DasForm(pFormVec);          /* the variable copied it */
	MUST(pVec != NULL);

	/* a datum is still the assembled vector... */
	ptrdiff_t aLoc[1] = { 1 };
	das_datum dm = {{0},vtUnknown,0,NULL};
	CHECK(DasVar_get((DasVar*)pVec, aLoc, DAS_BS_NULL, &dm) == 0);
	CHECK(dm.vt == vtComposite);

	/* ...but a subset is plain components, the component index trailing */
	ptrdiff_t aMin[1] = {0};
	ptrdiff_t aMax[1] = {4};
	DasAry* pSlice = DasVar_subset((DasVar*)pVec, 1, aMin, aMax, NULL);
	MUST(pSlice != NULL);

	/* The contract is that the output carries the variable's ELEMENT type,
	   whatever the backing store happens to hold, and never its presentation
	   type.  Stated against DasVar_elemType rather than the fixture's own
	   vtFloat, so switching this test to another element type does not turn
	   into a false failure. */
	CHECK(DasAry_valType(pSlice) == (das_val_type)DasVar_elemType((DasVar*)pVec));
	CHECK(DasAry_valType(pSlice) != vtComposite);
	CHECK(DasVar_valType((DasVar*)pVec) == vtComposite);  /* the view differs */

	ptrdiff_t aShape[VARIDX_MAX];
	CHECK(DasAry_shape(pSlice, aShape) == 2);
	CHECK((aShape[0] == 4)&&(aShape[1] == 3));

	size_t uVals = 0;
	const float* pVals = DasAry_getFloatsIn(pSlice, DIM0, &uVals);
	MUST(pVals != NULL);
	CHECK(uVals == 12);
	for(int i = 0; i < 12; ++i) CHECK(pVals[i] == (float)i);
	dec_DasAry(pSlice);

	CHECK(dec_DasVar((DasVar*)pVec) == 0);
	CHECK(DasGen_decRef(pGen) == 0);
	dec_DasAry(pAry);
	return nErrs;
}

/* A role belongs to the dim/set relationship, not to the set.  Two outcomes
   are legitimate and one is not: no parent means no role and that is fine, a
   parent means a role, and a parent that cannot name its own child is
   corruption rather than a quiet NULL. */
static int test_set_role(void)
{
	int nErrs = 0;

	float rFill = -1.0f;
	DasAry* pAry = new_DasAry(
		"vals", vtFloat, 0, (const ubyte*)&rFill, RANK_1(0), UNIT_NT
	);
	MUST(pAry != NULL);
	float aVals[4] = {1.0f, 2.0f, 3.0f, 4.0f};
	CHECK(DasAry_append(pAry, (const ubyte*)aVals, 4) != NULL);

	int8_t aMap[1] = { 0 };
	DasGen* pGen = new_DasGenAry(pAry, 1, aMap);
	CHECK(pGen != NULL);
	DasVar* pSet = _linearVar(pGen, UNIT_NT);
	CHECK(pSet != NULL);
	DasGen_decRef(pGen);

	/* standalone: no parent, so no role, and that is not an error */
	CHECK(DasVar_role(pSet) == NULL);

	DasDim* pDim = new_DasDim("B_mag", "B_mag", DASDIM_DATA, 1);
	CHECK(pDim != NULL);
	CHECK(DasDim_addVar(pDim, DASVAR_CENTER, pSet));

	/* adopted: the dim can now account for it */
	const char* sRole = DasVar_role(pSet);
	CHECK(sRole != NULL);
	CHECK(strcmp(sRole, DASVAR_CENTER) == 0);

	/* an unnamed role can never be stored, which is what keeps the
	   parented-implies-named promise true */
	DasGen* pGen2 = new_DasGenAry(pAry, 1, aMap);
	DasVar* pSet2 = _linearVar(pGen2, UNIT_NT);
	CHECK(pSet2 != NULL);
	DasGen_decRef(pGen2);
	CHECK(!DasDim_addVar(pDim, NULL, pSet2));
	CHECK(!DasDim_addVar(pDim, "", pSet2));
	CHECK(DasVar_role(pSet2) == NULL);   /* still standalone, still fine */

	CHECK(dec_DasVar(pSet2) == 0);
	del_DasDim(pDim);                    /* takes pSet down with it */
	dec_DasAry(pAry);
	return nErrs;
}

/* A sequence looks like an array from the outside wherever it can.  A stated
   extent is such a case: it bounds the sequence exactly as an array's length
   bounds the array.  A ragged or borrowed extent is the case where they
   genuinely cannot match, and there the sequence stays defined everywhere.

   The shape here is ex28's 'location' offset, index="-;256;280". */
static int test_seq_declared_extent(void)
{
	int nErrs = 0;

	double rMin = -128.0;
	double aInt[VARIDX_MAX] = {0.0, 1.0, 0.0};       /* moves along index 1 */

	ptrdiff_t aBound[3] = { VARIDX_UNUSED, 256, 280 };
	DasGen* pGen = new_DasGenSeq(
		etDouble, (const ubyte*)&rMin, 3, (const ubyte*)aInt, aBound
	);
	CHECK(pGen != NULL);
	DasVar* pSet = _linearVar(pGen, Units_fromStr("km"));
	CHECK(pSet != NULL);
	DasGen_decRef(pGen);

	/* the extent it reports is the extent it honors */
	ptrdiff_t aShape[VARIDX_MAX];
	CHECK(DasVar_shape(pSet, aShape) == 3);
	CHECK(aShape[1] == 256);
	CHECK(DasVar_lengthIn(pSet, 1, NULL) == 256);

	das_datum dm = {{0},vtUnknown,0,NULL};
	ptrdiff_t aLoc[3] = {0, 255, 0};
	CHECK(DasVar_get(pSet, aLoc, DAS_BS_NULL, &dm) == 0);           /* last legal index */
	CHECK(*((double*)&dm) == -128.0 + 255.0);

	aLoc[1] = 256;                                 /* one past the end */
	CHECK(DasVar_get(pSet, aLoc, DAS_BS_NULL, &dm) != 0);
	aLoc[1] = 1000;
	CHECK(DasVar_get(pSet, aLoc, DAS_BS_NULL, &dm) != 0);

	/* and a subset cannot smuggle past it either */
	ptrdiff_t aMin[3] = {0, 0, 0};
	ptrdiff_t aMax[3] = {1, 300, 1};
	CHECK(DasVar_subset(pSet, 3, aMin, aMax, NULL) == NULL);

	aMax[1] = 256;                                 /* exactly the extent */
	DasAry* pAry = DasVar_subset(pSet, 3, aMin, aMax, NULL);
	MUST(pAry != NULL);
	size_t uVals = 0;
	const double* pVals = DasAry_getDoublesIn(pAry, DIM0, &uVals);
	CHECK(uVals == 256);
	CHECK(pVals[0] == -128.0);
	CHECK(pVals[255] == -128.0 + 255.0);
	dec_DasAry(pAry);
	CHECK(dec_DasVar(pSet) == 0);

	/* An UNBOUNDED sequence is the other half of the rule: it takes its size
	   from elsewhere, so it answers anywhere it is asked. */
	ptrdiff_t aFree[3] = { VARIDX_UNUSED, VARIDX_BORROW, VARIDX_BORROW };
	pGen = new_DasGenSeq(
		etDouble, (const ubyte*)&rMin, 3, (const ubyte*)aInt, aFree
	);
	CHECK(pGen != NULL);
	pSet = _linearVar(pGen, Units_fromStr("km"));
	CHECK(pSet != NULL);
	DasGen_decRef(pGen);

	aLoc[1] = 100000;
	CHECK(DasVar_get(pSet, aLoc, DAS_BS_NULL, &dm) == 0);
	CHECK(*((double*)&dm) == -128.0 + 100000.0);

	aMax[1] = 300;
	pAry = DasVar_subset(pSet, 3, aMin, aMax, NULL);
	CHECK(pAry != NULL);
	if(pAry != NULL) dec_DasAry(pAry);

	CHECK(dec_DasVar(pSet) == 0);
	return nErrs;
}

/* ************************************************************************* */
/* Index-space characterization: what each variable class reports for shape
 * and length, and the three sequence forms.
 *
 * The expected-value tables are the point -- they are the executable form of
 * rulings that otherwise live only in prose.  Do not "simplify" a table by
 * dropping rows.
 */

static const char* idxValStr(ptrdiff_t n)
{
	switch(n){
	case VARIDX_RAGGED: return "RAGGED";
	case VARIDX_BORROW: return "BORROW";
	case VARIDX_UNUSED: return "UNUSED";
	}
	static char aBuf[4][24];
	static int iBuf = 0;
	iBuf = (iBuf + 1) % 4;
	snprintf(aBuf[iBuf], 24, "%td", n);
	return aBuf[iBuf];
}

/* DasVar_lengthIn() over the (3,160,80) AIS space.
 *
 * The point is the MERGE LATTICE, not any single answer: an array var reports
 * a concrete length on the indices it maps and UNUSED elsewhere, a sequence
 * reports BORROW along its dependent index, and a binary op merges its two
 * operands -- a real length beats a flag, BORROW beats UNUSED.  A var that
 * reported anything else for an unmapped index would pollute the per-dim
 * merge in DasDim_lengthIn(). */
static int test_lengthin_lattice(void)
{
	int nErrs = 0;

	ais_sets s;
	int nRet = _ais_build(&s);
	if(nRet != 0){ _ais_free(&s); return nRet; }

	ptrdiff_t aLoc[VARIDX_MAX] = VARIDX_INIT_BEGIN;   /* probe at the origin */

	struct { const char* sWhat; DasVar* pVar; int nIdx; ptrdiff_t nWant; } aCheck[] = {
		{"pEcho lengthIn(0) records",  s.pEcho, 0,              AIS_RECS},
		{"pEcho lengthIn(1) pulses",   s.pEcho, 1,             AIS_FREQS},
		{"pEcho lengthIn(2) samples",  s.pEcho, 2,             AIS_ECHOS},
		{"pTime lengthIn(0) records",  s.pTime, 0,              AIS_RECS},
		{"pTime lengthIn(1) unmapped", s.pTime, 1,         VARIDX_UNUSED},
		{"pTime lengthIn(2) unmapped", s.pTime, 2,         VARIDX_UNUSED},
		/* sequences: BORROW along their one dependent index, UNUSED elsewhere */
		{"pPulseOff lengthIn(0)", s.pPulseOff, 0,          VARIDX_UNUSED},
		{"pPulseOff lengthIn(1)", s.pPulseOff, 1,          VARIDX_BORROW},
		{"pPulseOff lengthIn(2)", s.pPulseOff, 2,          VARIDX_UNUSED},
		{"pDelay lengthIn(1)",    s.pDelay,    1,          VARIDX_UNUSED},
		{"pDelay lengthIn(2)",    s.pDelay,    2,          VARIDX_BORROW},
		/* binary ops merge: pPulseTime = pTime(idx0) + pPulseOff(seq idx1) */
		{"pPulseTime lengthIn(0)", s.pPulseTime, 0,             AIS_RECS},
		{"pPulseTime lengthIn(1)", s.pPulseTime, 1,        VARIDX_BORROW},
		{"pPulseTime lengthIn(2)", s.pPulseTime, 2,        VARIDX_UNUSED},
		/* pAppAlt = pMexAlt(idx0) - pRange(seq idx2) */
		{"pAppAlt lengthIn(0)",    s.pAppAlt,    0,             AIS_RECS},
		{"pAppAlt lengthIn(1)",    s.pAppAlt,    1,        VARIDX_UNUSED},
		{"pAppAlt lengthIn(2)",    s.pAppAlt,    2,        VARIDX_BORROW},
	};
	int nCheck = (int)(sizeof(aCheck)/sizeof(aCheck[0]));
	int nBad = 0;
	for(int c = 0; c < nCheck; ++c){
		ptrdiff_t nGot = DasVar_lengthIn(aCheck[c].pVar, aCheck[c].nIdx, aLoc);
		if(nGot != aCheck[c].nWant){
			printf("ERROR: %s: got %s, expected %s\n", aCheck[c].sWhat,
				idxValStr(nGot), idxValStr(aCheck[c].nWant));
			++nBad;
		}
	}
	_ais_free(&s);
	if(nBad > 0)
		printf("ERROR: %d of %d lengthIn checks wrong\n", nBad, nCheck);
	return nErrs + nBad;
}

/* DasVar_shape() over the same space.
 *
 * Parallel to the lengthIn table and NOT redundant with it: external shape
 * runs through the per-class *_shape slots feeding das_varindex_merge(), a
 * different code path from lengthIn.  The two have disagreed before. */
static int test_shape_lattice(void)
{
	int nErrs = 0;

	ais_sets s;
	int nRet = _ais_build(&s);
	if(nRet != 0){ _ais_free(&s); return nRet; }

	struct { const char* sName; DasVar* pVar; ptrdiff_t aWant[3]; } aChk[] = {
		{"pEcho     (Ary 0,1,2)", s.pEcho,   {     AIS_RECS,     AIS_FREQS,     AIS_ECHOS}},
		{"pTime     (Ary 0    )", s.pTime,   {     AIS_RECS, VARIDX_UNUSED, VARIDX_UNUSED}},
		{"pMexAlt   (Ary 0    )", s.pMexAlt, {     AIS_RECS, VARIDX_UNUSED, VARIDX_UNUSED}},
		{"pPulseOff (Seq 1    )", s.pPulseOff,{VARIDX_UNUSED, VARIDX_BORROW, VARIDX_UNUSED}},
		{"pDelay    (Seq 2    )", s.pDelay,  {VARIDX_UNUSED, VARIDX_UNUSED, VARIDX_BORROW}},
		{"pPulseTime(Bin 0+1  )", s.pPulseTime,{   AIS_RECS, VARIDX_BORROW, VARIDX_UNUSED}},
		{"pAppAlt   (Bin 0-2  )", s.pAppAlt, {     AIS_RECS, VARIDX_UNUSED, VARIDX_BORROW}},
	};
	int nChk = (int)(sizeof(aChk)/sizeof(aChk[0]));
	int nBad = 0;
	for(int p = 0; p < nChk; ++p){
		ptrdiff_t aGot[VARIDX_MAX] = VARIDX_INIT_UNUSED;
		DasVar_shape(aChk[p].pVar, aGot);
		for(int i = 0; i < 3; ++i){
			if(aGot[i] != aChk[p].aWant[i]){
				printf("ERROR: %s shape[%d]: got %s, expected %s\n", aChk[p].sName,
					i, idxValStr(aGot[i]), idxValStr(aChk[p].aWant[i]));
				++nBad;
			}
		}
	}
	_ais_free(&s);
	if(nBad > 0) printf("ERROR: %d of %d shape values wrong\n", nBad, nChk*3);
	return nErrs + nBad;
}

/* A vtTime sequence -- a regularly sampled ABSOLUTE
 * time axis.  The intercept is a das_time, the step is in seconds (the
 * Units_interval of UTC, derived and not asked for), and the values produced
 * are das_times.  Distinct from test_scalar_set(), which walks the etLong
 * TT2000-nanosecond path; this one exercises live calendar composition. */
static int test_seq_vttime(void)
{
	int nErrs = 0;

	das_time tBeg = DAS_TIME_NULL;
	dt_parsetime("2020-01-01T00:00:00", &tBeg);
	double rStep = 0.5;                          /* seconds */
	ptrdiff_t aShape[1] = { VARIDX_RAGGED };

	DasGen* pGen = new_DasGenSeq(
		etTime, (const ubyte*)&tBeg, 1, (const ubyte*)&rStep, aShape
	);
	CHECK(pGen != NULL);
	DasForm* pForm = new_DasFormPoint();
	MUST(pForm != NULL);
	DasVar* pSet = new_DasVar(pGen, UNIT_UTC, pForm);
	del_DasForm(pForm);
	MUST(pSet != NULL);
	DasGen_decRef(pGen);

	CHECK(DasVar_elemType(pSet) == etTime);

	const char* aWant[4] = {
		"2020-01-01T00:00:00.000", "2020-01-01T00:00:00.500",
		"2020-01-01T00:00:01.000", "2020-01-01T00:00:02.500"
	};
	ptrdiff_t aIdx[4] = {0, 1, 2, 5};
	for(int k = 0; k < 4; ++k){
		ptrdiff_t aLoc[VARIDX_MAX] = {aIdx[k],0,0,0,0,0,0,0};
		das_datum dm = {{0},vtUnknown,0,NULL}; char sGot[64];
		/* skip rather than MUST: a failed get leaves dm uninitialized and the
		   format below would read it, but the remaining rows still have
		   something to say */
		if(DasVar_get(pSet, aLoc, DAS_BS_NULL, &dm) != 0){
			printf("ERROR: time [%td]: DasVar_get failed\n", aIdx[k]);
			++nErrs;
			continue;
		}
		das_datum_toStrValOnly(&dm, sGot, sizeof(sGot), 3);
		if(strcmp(sGot, aWant[k]) != 0){
			printf("ERROR: time [%td]: got %s, expected %s\n",
				aIdx[k], sGot, aWant[k]);
			++nErrs;
		}
	}

	/* a subset across the lone index expands to a run of das_times */
	ptrdiff_t aMin[1] = {0}, aMax[1] = {4};
	DasAry* pAry = DasVar_subset(pSet, 1, aMin, aMax, NULL);
	MUST(pAry != NULL);
	ptrdiff_t aShp[VARIDX_MAX];
	CHECK(DasAry_shape(pAry, aShp) == 1);
	CHECK(aShp[0] == 4);
	dec_DasAry(pAry);

	CHECK(dec_DasVar(pSet) == 0);
	return nErrs;
}

/* A MULTI-INDEX sequence, offset[j][k] = 16j + 0.125k.
 *
 * The ISEE-1 rapid-sample offset shape: index="-;16;128", interval="16;0.125".
 * Two simultaneously non-zero slopes, so the value is constant along NO single
 * axis -- the case a one-slope sequence test cannot reach, and the reason
 * DasGenSeq carries an interval per external index rather than one scalar. */
static int test_seq_multi_index(void)
{
	int nErrs = 0;

	double rMin = 0.0;
	double aInt[VARIDX_MAX] = {0.0};
	aInt[1] = 16.0;                              /* one slope per dependent axis */
	aInt[2] = 0.125;
	ptrdiff_t aShape[3] = { VARIDX_UNUSED, VARIDX_BORROW, VARIDX_BORROW };

	DasGen* pGen = new_DasGenSeq(
		etDouble, (const ubyte*)&rMin, 3, (const ubyte*)aInt, aShape
	);
	CHECK(pGen != NULL);
	DasVar* pSet = _linearVar(pGen, Units_fromStr("s"));
	CHECK(pSet != NULL);
	DasGen_decRef(pGen);

	/* UNUSED on axis 0, BORROW on both dependent axes */
	ptrdiff_t aGot[VARIDX_MAX] = VARIDX_INIT_UNUSED;
	DasVar_shape(pSet, aGot);
	CHECK(aGot[0] == VARIDX_UNUSED);
	CHECK(aGot[1] == VARIDX_BORROW);
	CHECK(aGot[2] == VARIDX_BORROW);

	struct { ptrdiff_t i, j, k; double want; } aChk[5] = {
		{0,0,0, 0.0}, {0,0,1, 0.125}, {0,1,0, 16.0},
		{2,3,16, 50.0}, {5,15,127, 255.875}
	};
	for(int c = 0; c < 5; ++c){
		ptrdiff_t aLoc[VARIDX_MAX] = {aChk[c].i, aChk[c].j, aChk[c].k, 0,0,0,0,0};
		das_datum dm = {{0},vtUnknown,0,NULL};
		if(DasVar_get(pSet, aLoc, DAS_BS_NULL, &dm) != 0){
			printf("ERROR: offset[%td][%td][%td]: DasVar_get failed\n",
				aChk[c].i, aChk[c].j, aChk[c].k);
			++nErrs;
			continue;
		}
		double rGot;
		if(!das_datum_toDbl(&dm, &rGot)){
			printf("ERROR: offset[%td][%td][%td]: could not read as a double\n",
				aChk[c].i, aChk[c].j, aChk[c].k);
			++nErrs;
			continue;
		}
		if(rGot != aChk[c].want){
			printf("ERROR: offset[%td][%td][%td]: got %g, expected %g\n",
				aChk[c].i, aChk[c].j, aChk[c].k, rGot, aChk[c].want);
			++nErrs;
		}
	}

	/* a subset cube: both strides have to compose across the walk */
	ptrdiff_t aMin[3] = {0,0,0}, aMax[3] = {1,2,3};
	DasAry* pAry = DasVar_subset(pSet, 3, aMin, aMax, NULL);
	MUST(pAry != NULL);
	size_t uVals = 0;
	const double* pVals = DasAry_getDoublesIn(pAry, DIM0, &uVals);
	const double aWant[6] = {0.0, 0.125, 0.25, 16.0, 16.125, 16.25};
	MUST(pVals != NULL);
	CHECK(uVals == 6);
	for(size_t q = 0; q < 6; ++q){
		if(pVals[q] != aWant[q]){
			printf("ERROR: subset cube [%zu]: got %g, expected %g\n",
				q, pVals[q], aWant[q]);
			++nErrs;
		}
	}
	dec_DasAry(pAry);

	CHECK(dec_DasVar(pSet) == 0);
	return nErrs;
}

/* A VECTOR sequence -- the ex28 geo_loc offset grid.
 *
 *     comp0 = -128 + 1*j + 0*k        (interval "1;0")
 *     comp1 = -140 + 0*j + 1*k        (interval "0;1")
 *
 * The intercept and each slope are per-component, so this is a multi-component
 * generator (new_DasGenSeqN) wrapped in a composite var, NOT a scalar sequence.
 * The split between the numeric run and the geovec presentation is what this
 * case pins down.  Vector sequences are still unbuilt, so treat a failure here
 * as "not yet", not as a regression -- but do not delete the expected
 * values. */
static int test_seq_vector(void)
{
	int nErrs = 0;

	double aIntercepts[2] = {-128.0, -140.0};    /* per component */

	/* Slopes are component-major and PACKED at nExtRank, not at VARIDX_MAX --
	   new_DasGenSeqN documents "nComps * nExtRank slopes" and the reader packs
	   them the same way.  Declaring this [2][VARIDX_MAX] reads component 1's
	   slopes out of component 0's padding, which is silent: component 1 simply
	   never moves.  comp0 moves on axis 1, comp1 on axis 2. */
	double aInt[2][3] = {{0.0}};
	aInt[0][1] = 1.0;  aInt[0][2] = 0.0;
	aInt[1][1] = 0.0;  aInt[1][2] = 1.0;
	ptrdiff_t aExtShape[3] = { VARIDX_UNUSED, VARIDX_BORROW, VARIDX_BORROW };

	DasGen* pGen = new_DasGenSeqN(
		etDouble, 2, (const ubyte*)aIntercepts, 3, (const ubyte*)aInt, aExtShape
	);
	CHECK(pGen != NULL);

	/* ex28's offset vector: 2 cartesian components in the CASSIOPE_NEC frame,
	   sysorder "0;1".  The form is built complete and its reference handed to
	   the var. */
	ptrdiff_t aIntShape[1] = { 2 };
	DasForm* pFormVec = new_DasFormVector(
		"CASSIOPE_NEC", DAS_VSYS_CART, NULL
	);
	MUST(pFormVec != NULL);
	DasVarComp* pVec = new_DasVarComp(
		pGen, Units_fromStr("km"), pFormVec, 1, aIntShape
	);
	del_DasForm(pFormVec);          /* the variable copied it */
	MUST(pVec != NULL);
	DasGen_decRef(pGen);

	/* external shape: UNUSED on axis 0, BORROW on the two grid axes */
	ptrdiff_t aGot[VARIDX_MAX] = VARIDX_INIT_UNUSED;
	DasVar_shape((DasVar*)pVec, aGot);
	CHECK(aGot[0] == VARIDX_UNUSED);
	CHECK(aGot[1] == VARIDX_BORROW);
	CHECK(aGot[2] == VARIDX_BORROW);

	/* internal shape: rank 1, two components.  The ONLY intrShape assertion
	   in this file -- if it goes, nothing checks inner rank at all. */
	ptrdiff_t aIntr[VARIDX_MAX] = VARIDX_INIT_UNUSED;
	CHECK(DasVar_intrShape((DasVar*)pVec, aIntr) == 1);
	CHECK(aIntr[0] == 2);

	/* Component symbols stop at what the variable actually carries.  A two
	   component field is real data -- EFI sends them -- so naming a third
	   would hand a caller an E_z nobody measured. */
	CHECK(strcmp(DasVar_compSym((DasVar*)pVec, 0), "x") == 0);
	CHECK(strcmp(DasVar_compSym((DasVar*)pVec, 1), "y") == 0);
	CHECK(DasVar_compSym((DasVar*)pVec, 2) == NULL);
	CHECK(DasVar_compSym((DasVar*)pVec, -1) == NULL);

	/* One form handed to two variables of DIFFERENT component counts.  Each
	   takes its own copy, so the second construction can not reach back and
	   change what the first one answers.  While forms were reference counted
	   and shared, the second validate() overwrote the one uComps field and the
	   two-component variable started naming a "z" nobody sent. */
	{
		double aOrig[3] = {0.0, 0.0, 0.0};
		double aSlope[3][3] = {{0.0}};
		aSlope[0][1] = 1.0;  aSlope[1][2] = 1.0;  aSlope[2][1] = 1.0;
		ptrdiff_t aExt[3] = { VARIDX_UNUSED, VARIDX_BORROW, VARIDX_BORROW };
		ptrdiff_t aTwo[1] = { 2 }, aThree[1] = { 3 };

		DasForm* pShare = new_DasFormVector("CASSIOPE_NEC", DAS_VSYS_CART, NULL);
		MUST(pShare != NULL);

		DasGen* pG2 = new_DasGenSeqN(
			etDouble, 2, (const ubyte*)aOrig, 3, (const ubyte*)aSlope, aExt
		);
		DasVarComp* pV2 = new_DasVarComp(pG2, Units_fromStr("km"), pShare, 1, aTwo);
		MUST(pV2 != NULL);
		DasGen_decRef(pG2);

		DasGen* pG3 = new_DasGenSeqN(
			etDouble, 3, (const ubyte*)aOrig, 3, (const ubyte*)aSlope, aExt
		);
		DasVarComp* pV3 = new_DasVarComp(pG3, Units_fromStr("km"), pShare, 1, aThree);
		MUST(pV3 != NULL);
		DasGen_decRef(pG3);

		/* Neither variable took it, so this is the last reference */
		del_DasForm(pShare);

		CHECK(DasVar_compSym((DasVar*)pV2, 2) == NULL);            /* still two */
		CHECK(strcmp(DasVar_compSym((DasVar*)pV3, 2), "z") == 0);

		dec_DasVar((DasVar*)pV2);
		dec_DasVar((DasVar*)pV3);
	}

	/* A sequence computes its components on demand, so there is no storage to
	   point at and this is the one shape that needs the caller's scratch.
	   Assert that the library says so before we hand it any. */
	CHECK(DasVar_getNeedsBuf((DasVar*)pVec));
	CHECK(!DasVar_hasAry((DasVar*)pVec));

	/* ...and that an empty work is refused with the size it wanted, rather
	   than half-filling a datum or scribbling. */
	{
		ptrdiff_t aProbe[VARIDX_MAX] = {0,0,0,0,0,0,0,0};
		das_datum dmProbe = {{0},vtUnknown,0,NULL};
		int nWant = DasVar_get((DasVar*)pVec, aProbe, DAS_BS_NULL, &dmProbe);
		CHECK(nWant == (int)(2 * sizeof(double)));
	}

	ubyte aWork[64];
	das_byte_seq work = { aWork, sizeof(aWork) };

	struct { ptrdiff_t j, k; double w0, w1; } aChk[4] = {
		{0,0, -128.0, -140.0}, {3,0, -125.0, -140.0},
		{0,7, -128.0, -133.0}, {10,20, -118.0, -120.0}
	};
	for(int c = 0; c < 4; ++c){
		ptrdiff_t aLoc[VARIDX_MAX] = {0, aChk[c].j, aChk[c].k, 0,0,0,0,0};
		das_datum dm = {{0},vtUnknown,0,NULL};
		if(DasVar_get((DasVar*)pVec, aLoc, work, &dm) != 0){
			printf("ERROR: geo_loc[%td][%td]: DasVar_get failed\n",
				aChk[c].j, aChk[c].k);
			++nErrs;
			continue;
		}
		CHECK(dm.vt == vtComposite);
		double aComp[2] = {0.0, 0.0};
		if(das_datum_toDoubles(&dm, aComp, 2) != 2){
			printf("ERROR: geo_loc[%td][%td]: could not read 2 components\n",
				aChk[c].j, aChk[c].k);
			++nErrs;
			continue;
		}
		if((aComp[0] != aChk[c].w0)||(aComp[1] != aChk[c].w1)){
			printf("ERROR: geo_loc[%td][%td]: got (%g, %g), expected (%g, %g)\n",
				aChk[c].j, aChk[c].k, aComp[0], aComp[1], aChk[c].w0, aChk[c].w1);
			++nErrs;
		}
	}

	CHECK(dec_DasVar((DasVar*)pVec) == 0);
	return nErrs;
}

int main(int argc, char** argv)
{
	(void)argc;

	/* Unbuffered, because the report is worth most when the run dies.  Piped
	   to a file or a log, stdout is block buffered and a segfault discards
	   everything not yet flushed -- which is every check that passed or failed
	   before the crash.  The library's own errors go to stderr and survive, so
	   the symptom is a log holding the crash and none of the findings. */
	setvbuf(stdout, NULL, _IONBF, 0);

	das_init(argv[0], DASERR_DIS_RET, 0, DASLOG_ERROR, NULL);

	/* Every case runs.  A case that fails reports its own count and the run
	   continues, so one invocation shows the whole picture rather than
	   whatever happened to break first. */
	struct { const char* sName; int (*pFn)(void); } aCase[] = {
		{"test_scalar_set",          test_scalar_set},
		{"test_ctor_ref_contract",   test_ctor_ref_contract},
		{"test_binop_ref_contract",  test_binop_ref_contract},
		{"test_byte_runs",           test_byte_runs},
		{"test_composite",           test_composite},
		{"test_units_list_refused",  test_units_list_refused},
		{"test_subset_values",       test_subset_values},
		{"test_subset_refusals",     test_subset_refusals},
		{"test_subset_whole",        test_subset_whole},
		{"test_subset_view_vs_copy", test_subset_view_vs_copy},
		{"test_subset_ragged",       test_subset_ragged},
		{"test_subset_composite",    test_subset_composite},
		{"test_set_role",            test_set_role},
		{"test_seq_declared_extent", test_seq_declared_extent},
		{"test_lengthin_lattice",    test_lengthin_lattice},
		{"test_shape_lattice",       test_shape_lattice},
		{"test_seq_vttime",          test_seq_vttime},
		{"test_seq_multi_index",     test_seq_multi_index},
		{"test_seq_vector",          test_seq_vector},
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
		printf("ERROR: TestVar: %d check(s) failed across %d of %d cases\n",
			nBadCheck, nBadCase, nCase);
		return 13;
	}

	printf("INFO: TestVar: all DasVar layer checks passed\n");
	return 0;
}

/* Still to write:
 *
 * 1. DasVar_compLabels().  The three preference tiers (one label per
 *    component, a single label as stem plus symbol, the dimension name as
 *    stem) and its refusals (a ragged component count, too few buffers).
 *    das3_csv and das3_cdf are the consumers; nothing pins it directly.
 * 2. A datum read off a complex or rotation variable.  Scalar, string, blob,
 *    vector, plain linear and unknown-kind composites are read back through
 *    DasVar_get() here; complex and rotation are built and validated in
 *    TestCplx and TestForm but never carried by a variable.  TestCplx's own
 *    list names the same gap.
 * 3. Wire counts, numItems vs intern vs itemBytes.  The das3_text golden
 *    pairs pin them end to end; no case here checks the arithmetic on its
 *    own.
 */
