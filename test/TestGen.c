/** @file TestGen.c Unit tests for the DasGen value-source layer (gen.c) */

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

/* A generator is Axis A: where a variable's values come from.  It has no
 * units, no formalism and no role in a dimension, and these tests hold it to
 * that: nothing here builds a DasVar.  The variable layer's own tests are in
 * TestVar.c and they treat a generator as fixture, never as the subject.
 *
 * Keep it C99 clean: no _Static_assert, use runtime checks.
 */

#define _POSIX_C_SOURCE 200112L

#include <stdio.h>
#include <string.h>

#include <das3/core.h>
#include <das3/gen_p.h>   /* tests link the static archive, so they may */

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

/* Case 1: et is a pinned subset of vt; assert they have not drifted. */
static int test_elem_drift(void)
{
	int nErrs = 0;

	CHECK(DASET_NULL    == (int)DASVT_NULL);
	CHECK(DASET_UBYTE   == (int)DASVT_UBYTE);
	CHECK(DASET_BYTE    == (int)DASVT_BYTE);
	CHECK(DASET_USHORT  == (int)DASVT_USHORT);
	CHECK(DASET_SHORT   == (int)DASVT_SHORT);
	CHECK(DASET_UINT    == (int)DASVT_UINT);
	CHECK(DASET_INT     == (int)DASVT_INT);
	CHECK(DASET_ULONG   == (int)DASVT_ULONG);
	CHECK(DASET_LONG    == (int)DASVT_LONG);
	CHECK(DASET_FLOAT   == (int)DASVT_FLOAT);
	CHECK(DASET_DOUBLE  == (int)DASVT_DOUBLE);
	CHECK(DASET_TIME    == (int)DASVT_TIME);
	return nErrs;
}

/* Case 2: generator eval for constant and sequence sources */
static int test_gen_const_seq(void)
{
	int nErrs = 0;

	ptrdiff_t aShape[1] = { DASIDX_RAGGED };
	double rVal = 4.75;
	DasGen* pConst = new_DasGenConst(DASET_DOUBLE, (const uint8_t*)&rVal, 1, aShape);
	MUST(pConst != NULL);

	ptrdiff_t aAddr[1] = { 117 };
	double rOut = 0.0;
	CHECK(DasGen_eval(pConst, aAddr, (uint8_t*)&rOut, sizeof(rOut)) == 1);
	CHECK(rOut == 4.75);

	/* a 100 Hz offset sequence: 0.01 s per sample */
	double rIntercept = 0.0, rSlope = 0.01;
	DasGen* pSeq = new_DasGenSeq(
		DASET_DOUBLE, (const uint8_t*)&rIntercept, 1, (const uint8_t*)&rSlope, aShape
	);
	MUST(pSeq != NULL);
	aAddr[0] = 250;
	CHECK(DasGen_eval(pSeq, aAddr, (uint8_t*)&rOut, sizeof(rOut)) == 1);
	CHECK(rOut == 2.5);

	/* a TT2000 tick sequence, whole nanoseconds */
	int64_t nIntercept = 1000000000LL, nSlope = 250000LL;
	DasGen* pTicks = new_DasGenSeq(
		DASET_LONG, (const uint8_t*)&nIntercept, 1, (const uint8_t*)&nSlope, aShape
	);
	MUST(pTicks != NULL);
	aAddr[0] = 4;
	int64_t nOut = 0;
	CHECK(DasGen_eval(pTicks, aAddr, (uint8_t*)&nOut, sizeof(nOut)) == 1);
	CHECK(nOut == 1001000000LL);

	CHECK(DasGen_decRef(pConst) == 0);
	CHECK(DasGen_decRef(pSeq) == 0);
	CHECK(DasGen_decRef(pTicks) == 0);
	return nErrs;
}

/* Case 3: generator eval over a backing array, including an internal run */
static int test_gen_array(void)
{
	int nErrs = 0;

	float fill = -1.0f;
	DasAry* pAry = new_DasAry(
		"b_vals", DASVT_FLOAT, 0, (const uint8_t*)&fill, DAS_SHAPE2(0, 3), DASUNITS_DIMENSIONLESS
	);
	MUST(pAry != NULL);

	float aVals[12];
	for(int i = 0; i < 12; ++i) aVals[i] = (float)i;
	MUST(DasAry_append(pAry, (const uint8_t*)aVals, 12) != NULL);

	int8_t aMap[1] = { 0 };
	DasGen* pGen = new_DasGenAry(pAry, 1, aMap);
	MUST(pGen != NULL);
	CHECK(DasGen_elemType(pGen) == DASET_FLOAT);

	/* records of 3: the item run at external index 2 is {6,7,8} */
	ptrdiff_t aAddr[1] = { 2 };
	float aRun[3] = {0.0f, 0.0f, 0.0f};
	CHECK(DasGen_eval(pGen, aAddr, (uint8_t*)aRun, sizeof(aRun)) == 3);
	CHECK((aRun[0] == 6.0f)&&(aRun[1] == 7.0f)&&(aRun[2] == 8.0f));

	ptrdiff_t aShape[DASIDX_MAX];
	CHECK(DasGen_extShape(pGen, aShape) == 1);
	CHECK(aShape[0] == 4);

	CHECK(DasGen_decRef(pGen) == 0);
	dec_DasAry(pAry);
	return nErrs;
}

/* Case 4: what a generator does to its backing array's reference count.
 *
 * A generator ADDS a reference: it calls inc_DasAry and the caller still owns
 * the one it made.  That is the rule for every das2C call that keeps a pointer
 * to a reference counted object, so these counts are worth stating even though
 * nothing here is subtle.  Where a call does NOT follow it, the header says
 * so. */
static int test_gen_ary_refs(void)
{
	int nErrs = 0;

	float fill = -1.0f;
	DasAry* pAry = new_DasAry(
		"refs", DASVT_FLOAT, 0, (const uint8_t*)&fill, DAS_SHAPE2(0,3), DASUNITS_DIMENSIONLESS
	);
	MUST(pAry != NULL);
	CHECK(ref_DasAry(pAry) == 1);           /* just mine */

	int8_t aMap[1] = { 0 };
	DasGen* pGen = new_DasGenAry(pAry, 1, aMap);
	MUST(pGen != NULL);
	CHECK(ref_DasAry(pAry) == 2);           /* mine + the generator's */

	/* A copy clones the generator OBJECT and shares the storage, which is what
	   lets a later setArray on one owner leave the other pointing where it was. */
	DasGen* pCopy = DasGen_copy(pGen);
	MUST(pCopy != NULL);
	CHECK(pCopy != pGen);
	CHECK(ref_DasAry(pAry) == 3);
	CHECK(DasGen_decRef(pCopy) == 0);
	CHECK(ref_DasAry(pAry) == 2);           /* the copy gave its reference back */

	/* setArray adds one to the replacement and drops one from the original */
	DasAry* pNew = new_DasAry(
		"refs_time", DASVT_DOUBLE, 0, NULL, DAS_SHAPE2(0,3), DASUNITS_DIMENSIONLESS
	);
	MUST(pNew != NULL);
	MUST(DasGen_setArray(pGen, pNew));
	CHECK(ref_DasAry(pNew) == 2);           /* mine + the generator's */
	CHECK(ref_DasAry(pAry) == 1);           /* only mine now */
	CHECK(DasGen_elemType(pGen) == DASET_DOUBLE);   /* re-derived from the new store */

	CHECK(DasGen_decRef(pGen) == 0);
	CHECK(ref_DasAry(pNew) == 1);           /* the generator gave it back */

	dec_DasAry(pNew);
	dec_DasAry(pAry);
	return nErrs;
}

int main(int argc, char** argv)
{
	(void)argc;

	/* Unbuffered: a crash must not discard the checks that ran before it.
	   See the same note in TestVar.c. */
	setvbuf(stdout, NULL, _IONBF, 0);

	daslib_init(argv[0], DASERRDISP_RET, DASLOG_ERROR, NULL);

	struct { const char* sName; int (*pFn)(void); } aCase[] = {
		{"test_elem_drift",    test_elem_drift},
		{"test_gen_const_seq", test_gen_const_seq},
		{"test_gen_array",     test_gen_array},
		{"test_gen_ary_refs",  test_gen_ary_refs},
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
		printf("ERROR: TestGen: %d check(s) failed across %d of %d cases\n",
			nBadCheck, nBadCase, nCase);
		return 13;
	}

	printf("INFO: TestGen: all DasGen layer checks passed\n");
	return 0;
}

/* Still to write:
 *
 * 1. The rest of the read surface, none of it touched here yet: at() (the
 *    borrowed-pointer path, and its NULL answer for every computed kind),
 *    lengthIn(), itemElems(), getFill(), getArray().
 * 2. subsetView() vs subsetInto().  subsetView is a BORROW that only gtArray
 *    can answer; every other kind returns NULL through the shared
 *    _DasGen_subsetViewNone, and that NULL is a contract, not a failure.
 *    Assert both halves.
 * 3. new_DasGenSeqN, the multi-component sequence.  TestVar reaches it only
 *    through a composite variable (test_seq_vector), so its own eval
 *    contract has no direct test.
 * 4. new_DasGenBinop.  The generator-level operation, distinct from the
 *    DasVarBin walker that sits above it.
 * 5. Refusals: eval into a buffer too small, an out of range index on a
 *    bounded sequence, a rank mismatch.
 */
