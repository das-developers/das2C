/** @file TestGen.c Unit tests for the DasGen value-source layer (generator.c) */

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
#include <das3/generator.h>

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

	CHECK(etUnknown == (int)vtUnknown);
	CHECK(etUByte   == (int)vtUByte);
	CHECK(etByte    == (int)vtByte);
	CHECK(etUShort  == (int)vtUShort);
	CHECK(etShort   == (int)vtShort);
	CHECK(etUInt    == (int)vtUInt);
	CHECK(etInt     == (int)vtInt);
	CHECK(etULong   == (int)vtULong);
	CHECK(etLong    == (int)vtLong);
	CHECK(etFloat   == (int)vtFloat);
	CHECK(etDouble  == (int)vtDouble);
	CHECK(etTime    == (int)vtTime);
	return nErrs;
}

/* Case 2: generator eval for constant and sequence sources */
static int test_gen_const_seq(void)
{
	int nErrs = 0;

	ptrdiff_t aShape[1] = { VARIDX_RAGGED };
	double rVal = 4.75;
	DasGen* pConst = new_DasGenConst(etDouble, (const ubyte*)&rVal, 1, aShape);
	MUST(pConst != NULL);

	ptrdiff_t aLoc[1] = { 117 };
	double rOut = 0.0;
	CHECK(DasGen_eval(pConst, aLoc, (ubyte*)&rOut, sizeof(rOut)) == 1);
	CHECK(rOut == 4.75);

	/* a 100 Hz offset sequence: 0.01 s per sample */
	double rIntercept = 0.0, rSlope = 0.01;
	DasGen* pSeq = new_DasGenSeq(
		etDouble, (const ubyte*)&rIntercept, 1, (const ubyte*)&rSlope, aShape
	);
	MUST(pSeq != NULL);
	aLoc[0] = 250;
	CHECK(DasGen_eval(pSeq, aLoc, (ubyte*)&rOut, sizeof(rOut)) == 1);
	CHECK(rOut == 2.5);

	/* a TT2000 tick sequence, whole nanoseconds */
	int64_t nIntercept = 1000000000LL, nSlope = 250000LL;
	DasGen* pTicks = new_DasGenSeq(
		etLong, (const ubyte*)&nIntercept, 1, (const ubyte*)&nSlope, aShape
	);
	MUST(pTicks != NULL);
	aLoc[0] = 4;
	int64_t nOut = 0;
	CHECK(DasGen_eval(pTicks, aLoc, (ubyte*)&nOut, sizeof(nOut)) == 1);
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
		"b_vals", vtFloat, 0, (const ubyte*)&fill, RANK_2(0, 3), UNIT_DIMENSIONLESS
	);
	MUST(pAry != NULL);

	float aVals[12];
	for(int i = 0; i < 12; ++i) aVals[i] = (float)i;
	MUST(DasAry_append(pAry, (const ubyte*)aVals, 12) != NULL);

	int8_t aMap[1] = { 0 };
	DasGen* pGen = new_DasGenAry(pAry, 1, aMap);
	MUST(pGen != NULL);
	CHECK(DasGen_elemType(pGen) == etFloat);

	/* records of 3: the item run at external index 2 is {6,7,8} */
	ptrdiff_t aLoc[1] = { 2 };
	float aRun[3] = {0.0f, 0.0f, 0.0f};
	CHECK(DasGen_eval(pGen, aLoc, (ubyte*)aRun, sizeof(aRun)) == 3);
	CHECK((aRun[0] == 6.0f)&&(aRun[1] == 7.0f)&&(aRun[2] == 8.0f));

	ptrdiff_t aShape[VARIDX_MAX];
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
 * nothing here is subtle.  Where a call does NOT follow it, the header says so
 * -- see co_notes/todo_ref_consistency.md for the ones still to convert. */
static int test_gen_ary_refs(void)
{
	int nErrs = 0;

	float fill = -1.0f;
	DasAry* pAry = new_DasAry(
		"refs", vtFloat, 0, (const ubyte*)&fill, RANK_2(0,3), UNIT_DIMENSIONLESS
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
		"refs_time", vtDouble, 0, NULL, RANK_2(0,3), UNIT_DIMENSIONLESS
	);
	MUST(pNew != NULL);
	MUST(DasGen_setArray(pGen, pNew));
	CHECK(ref_DasAry(pNew) == 2);           /* mine + the generator's */
	CHECK(ref_DasAry(pAry) == 1);           /* only mine now */
	CHECK(DasGen_elemType(pGen) == etDouble);   /* re-derived from the new store */

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

	das_init(argv[0], DASERR_DIS_RET, 0, DASLOG_ERROR, NULL);

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

/* Coverage manifest, grown case by case.
 *
 * DONE 1. Element vs value enum drift (runtime, C99 safe).  Lives here rather
 *         than in TestValue.c because the pinning only matters to code that
 *         crosses the two: generators speak et, datums speak vt.  Move it if
 *         that reasoning stops holding.
 * DONE 2. eval() for gtConst, gtSeq (double and TT2000 long) and gtArray,
 *         the last with an internal item run.
 * DONE 3. extShape() and elemType() for gtArray.
 *
 * TODO 4. The rest of the read surface, none of it touched here yet:
 *         at() (the borrowed-pointer path, and its NULL answer for every
 *         computed kind), lengthIn(), itemElems(), getFill(), elemShape().
 * TODO 5. subsetView() vs subsetInto().  subsetView is a BORROW that only
 *         gtArray can answer; every other kind returns NULL through the
 *         shared _DasGen_subsetViewNone, and that NULL is a contract, not a
 *         failure.  Assert both halves.
 * TODO 6. copy(), getArray() and setArray().  A copy clones the generator
 *         object and shares the backing array; setArray re-derives the
 *         element type, which is the whole point of the call.
 * TODO 7. new_DasGenSeqN, the multi-component sequence.  TestVar reaches it
 *         only through a composite variable (test_seq_vector), so its own
 *         eval contract has no direct test.
 * TODO 8. new_DasGenBinop.  The generator-level operation, distinct from the
 *         DasVarBin walker that sits above it.
 * TODO 9. Refusals: eval into a buffer too small, an out of range index on a
 *         bounded sequence, a rank mismatch.
 */
