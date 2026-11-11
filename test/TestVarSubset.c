/** @file TestVarSubset.c Bulk reads off a DasVar, over real stream fixtures */

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

/* The four bulk-read entry points, exercised against streams rather than
 * synthetic arrays.
 *
 *     new_DasAry_subset()           natural shape, may lend
 *     new_DasAry_materialize()      natural shape, owns
 *     new_DasAry_subsetQube()       squared off,   may lend
 *     new_DasAry_materializeQube()  squared off,   owns
 *
 * Split from TestVar.c on purpose, and not only for size.  TestVar builds its
 * own generators, which is right when the subject is a constructor or a
 * vtable slot.  It is the wrong tool for raggedness: a hand-built array can be
 * made to have any shape at all, including shapes no reader would ever
 * produce, so it proves nothing about what das2C actually does with a stream.
 * Everything here loads a fixture and asks the same questions a client would.
 *
 * Every case below pins
 * something that was found broken, and the comment on each says which.
 */

#include <stdio.h>
#include <string.h>

#include <das3/core.h>
#include <das3/var.h>

const char* g_sProg = "TestVarSubset";

/* Same contract as TestVar.c: CHECK records and continues, MUST bails when the
   next line would read through a null.  Both need `int nErrs = 0` in scope. */
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

/* Several cases below assert that a call REFUSES, and das2C refuses loudly --
   which would leave a fully passing run printing ERROR lines that mean the
   opposite of what a reader assumes.  For exactly the length of the expected
   refusal a hook drops daserrf() messages; anything else still prints.  A
   genuine break still lands as a failed CHECK. */
static void _dropErrors(const DasLogMsg* pMsg, void* pUser)
{
	(void)pUser;
	if(pMsg->errCode == DAS_OKAY) daslog_stderr(pMsg, NULL);
}

#define REFUSES(expr) do{ \
	DasLogHook _hook = {_dropErrors, NULL}; \
	DasLogHook _prev = daslog_setHook(&_hook); \
	CHECK(expr); \
	daslog_setHook(&_prev); \
}while(0)

/* Fixtures live beside the source tree, so these run from the repo root the
   way every other stream test does. */
static DasStream* _open(const char* sFile, int nPktId, DasDs** ppDs)
{
	DasStream* pSd = new_DasStream_file(g_sProg, sFile);
	if(pSd == NULL){ printf("ERROR: couldn't read %s\n", sFile); return NULL; }

	DasDesc* pDesc = DasStream_desc(pSd, nPktId);
	if((pDesc == NULL)||(DasDesc_type(pDesc) != DASDESCTYPE_DS)){
		printf("ERROR: %s packet %d is not a dataset\n", sFile, nPktId);
		del_DasDesc((DasDesc*)pSd);
		return NULL;
	}
	*ppDs = (DasDs*)pDesc;
	return pSd;
}

static DasVar* _pointVar(DasDs* pDs, const char* sDim)
{
	DasDim* pDim = DasDs_dim(pDs, sDim);
	if(pDim == NULL) return NULL;
	return DasDim_pointVar(pDim);
}

/* ************************************************************************* */
/* ex35: ragged runs OF ragged strings -- both indices ragged at once.
 *
 * The case the natural/Qube split was built for.  Rows hold 3, 3, 1 and 4
 * strings; the strings are 8, 8, 9 / 8, 9, 1 / 10 / 10, 8, 10, 8 bytes
 * (NUL included).  So a Qube is [4, 4, 10] and a natural read is 89 values.
 * Before the split this refused outright: "ragged item runs have no fixed
 * width to subset". */
static int test_ex35_both_axes(void)
{
	int nErrs = 0;
	DasDs* pDs = NULL;
	DasStream* pSd = _open("examples/ex35_strings_rank2.d3t", 34, &pDs);
	MUST(pSd != NULL);

	DasVar* pV = _pointVar(pDs, "status");
	MUST(pV != NULL);

	ptrdiff_t aShape[DASIDX_MAX];
	CHECK(DasVar_shape(pV, aShape) == 2);

	/* -- squared off -- */
	DasAry* pQ = new_DasAry_materializeQube(pV, 2, NULL, NULL, NULL);
	MUST(pQ != NULL);
	CHECK(DasAry_shape(pQ, aShape) == 3);
	CHECK((aShape[0] == 4)&&(aShape[1] == 4)&&(aShape[2] == 10));
	CHECK(DasAry_size(pQ) == 160);
	CHECK(DasAry_ownsData(pQ));

	/* Contents, not just shape.  A right-shaped array of wrong bytes is the
	   classic way a padding walk passes its own test. */
	size_t uElSz = 0, uEls = 0;
	const uint8_t* pAll = DasAry_data(pQ, &uElSz, &uEls);
	MUST(pAll != NULL);
	CHECK(uEls == 160);
	CHECK(strcmp((const char*)(pAll +  0*10), "NOMINAL")   == 0);
	CHECK(strcmp((const char*)(pAll +  2*10), "DROP OUT")  == 0);
	CHECK(strcmp((const char*)(pAll +  3*10), "")          == 0);  /* pad row */
	CHECK(strcmp((const char*)(pAll +  6*10), "")          == 0);  /* real empty */
	CHECK(strcmp((const char*)(pAll +  8*10), "SAFE MODE") == 0);
	CHECK(strcmp((const char*)(pAll + 12*10), "CALIBRATE") == 0);
	CHECK(strcmp((const char*)(pAll + 15*10), "WARNING")   == 0);

	/* Every pad byte past a string's NUL is fill, all the way out. */
	for(int k = 8; k < 10; ++k) CHECK(pAll[0*10 + k] == 0);
	dec_DasAry(pQ);

	/* -- natural: the same data with nothing invented -- */
	DasAry* pN = new_DasAry_materialize(pV, 2, NULL, NULL, NULL);
	MUST(pN != NULL);
	CHECK(DasAry_shape(pN, aShape) == 3);
	CHECK(aShape[0] == 4);
	CHECK(aShape[1] == DASIDX_RAGGED);
	CHECK(aShape[2] == DASIDX_RAGGED);
	CHECK(DasAry_size(pN) == 89);          /* 25 + 18 + 10 + 36 */
	CHECK(DasAry_ownsData(pN));

	const int aRunCt[4] = {3, 3, 1, 4};
	const int aLen0[3]  = {8, 8, 9};
	ptrdiff_t aAddr[DASIDX_MAX] = DASIDX_INIT_BEGIN;
	for(int i = 0; i < 4; ++i){
		aAddr[0] = i;
		CHECK((int)DasAry_lengthIn(pN, 1, aAddr) == aRunCt[i]);
	}
	aAddr[0] = 0;
	for(int j = 0; j < 3; ++j){
		aAddr[1] = j;
		CHECK((int)DasAry_lengthIn(pN, 2, aAddr) == aLen0[j]);
	}

	/* the empty string in record 1 slot 2 is one byte, its NUL -- it survived
	   the decoder and it has to survive the copy */
	aAddr[0] = 1; aAddr[1] = 2;
	CHECK((int)DasAry_lengthIn(pN, 2, aAddr) == 1);
	dec_DasAry(pN);

	del_DasDesc((DasDesc*)pSd);
	return nErrs;
}

/* ************************************************************************* */
/* ex36: two ragged external indices AND a ragged item, backing array rank 4.
 *
 * Deeper than ex35 and the reason the walks recurse rather than special-casing
 * rank 2.  Groups per record are 2, 2, 1, 2; slots per group run 2,1 / 1,2 /
 * 1 / 2,1; the widest message is 8 bytes.  So [4, 2, 2, 8] squared, 57
 * natural. */
static int test_ex36_multilevel(void)
{
	int nErrs = 0;
	DasDs* pDs = NULL;
	DasStream* pSd = _open("examples/ex36_events_rank3.d3t", 36, &pDs);
	MUST(pSd != NULL);

	DasVar* pV = _pointVar(pDs, "msgs");
	MUST(pV != NULL);

	ptrdiff_t aShape[DASIDX_MAX];
	CHECK(DasVar_shape(pV, aShape) == 3);

	DasAry* pQ = new_DasAry_materializeQube(pV, 3, NULL, NULL, NULL);
	MUST(pQ != NULL);
	CHECK(DasAry_shape(pQ, aShape) == 4);
	CHECK((aShape[0]==4)&&(aShape[1]==2)&&(aShape[2]==2)&&(aShape[3]==8));
	CHECK(DasAry_size(pQ) == 128);

	size_t uElSz = 0, uEls = 0;
	const uint8_t* pAll = DasAry_data(pQ, &uElSz, &uEls);
	MUST(pAll != NULL);
	CHECK(strcmp((const char*)(pAll + 0*8), "NOMINAL") == 0);
	CHECK(strcmp((const char*)(pAll + 1*8), "LOW PWR") == 0);
	CHECK(strcmp((const char*)(pAll + 2*8), "SAFE")    == 0);
	CHECK(strcmp((const char*)(pAll + 3*8), "")        == 0);  /* pad */
	CHECK(strcmp((const char*)(pAll + 6*8), "WARM UP") == 0);
	CHECK(strcmp((const char*)(pAll + 7*8), "OK")      == 0);
	dec_DasAry(pQ);

	DasAry* pN = new_DasAry_materialize(pV, 3, NULL, NULL, NULL);
	MUST(pN != NULL);
	CHECK(DasAry_shape(pN, aShape) == 4);
	CHECK(DasAry_size(pN) == 57);
	CHECK(aShape[1] == DASIDX_RAGGED);
	CHECK(aShape[2] == DASIDX_RAGGED);
	CHECK(aShape[3] == DASIDX_RAGGED);

	const int aGrpCt[4] = {2, 2, 1, 2};
	ptrdiff_t aAddr[DASIDX_MAX] = DASIDX_INIT_BEGIN;
	for(int i = 0; i < 4; ++i){
		aAddr[0] = i;
		CHECK((int)DasAry_lengthIn(pN, 1, aAddr) == aGrpCt[i]);
	}
	aAddr[0] = 0; aAddr[1] = 0;
	CHECK((int)DasAry_lengthIn(pN, 2, aAddr) == 2);
	aAddr[1] = 1;
	CHECK((int)DasAry_lengthIn(pN, 2, aAddr) == 1);
	dec_DasAry(pN);

	del_DasDesc((DasDesc*)pSd);
	return nErrs;
}

/* ************************************************************************* */
/* ex27: a variable-length BLOB can never be squared off.
 *
 * Dude's ruling, 2026-08-03.  Padding a STRING is honest because the run is
 * NUL terminated and the pad reads as empty; padding a blob destroys each
 * item's length and no byte value can mean "not data" (ex27's fill is 0xff, a
 * perfectly legal image byte).  The discriminator is DASARY_FILL_TERM, which
 * ex35's strings carry and these blobs do not.
 *
 * The natural read still works, and that is the point of refusing: the data
 * stays reachable, it just keeps its real byte counts. */
static int test_ex27_blob_refuses_qube(void)
{
	int nErrs = 0;
	DasDs* pDs = NULL;
	DasStream* pSd = _open("examples/ex27_epop_fai_mgf_blob.d3t", 1, &pDs);
	MUST(pSd != NULL);

	DasVar* pV = _pointVar(pDs, "FAI_image");
	MUST(pV != NULL);

	daserr_setDisp(DASERRDISP_RET);

	REFUSES(new_DasAry_subsetQube(pV, 1, NULL, NULL, NULL) == NULL);
	REFUSES(new_DasAry_materializeQube(pV, 1, NULL, NULL, NULL) == NULL);

	DasAry* pN = new_DasAry_materialize(pV, 1, NULL, NULL, NULL);
	MUST(pN != NULL);

	ptrdiff_t aShape[DASIDX_MAX];
	CHECK(DasAry_shape(pN, aShape) == 2);
	CHECK(aShape[0] == 7);
	CHECK(aShape[1] == DASIDX_RAGGED);

	/* real PNG sizes, unpadded, with the magic intact */
	const int aBytes[7] = {29233, 28777, 27840, 27264, 26584, 26387, 26038};
	ptrdiff_t aAddr[DASIDX_MAX] = DASIDX_INIT_BEGIN;
	size_t uTotal = 0;
	for(int i = 0; i < 7; ++i){
		aAddr[0] = i;
		CHECK((int)DasAry_lengthIn(pN, 1, aAddr) == aBytes[i]);
		uTotal += (size_t)aBytes[i];

		size_t uCt = 0;
		const uint8_t* pRun = DasAry_valsIn(pN, DASVT_UBYTE, 1, aAddr, &uCt);
		MUST(pRun != NULL);
		CHECK(memcmp(pRun, "\x89PNG", 4) == 0);
	}
	CHECK(DasAry_size(pN) == uTotal);
	dec_DasAry(pN);

	del_DasDesc((DasDesc*)pSd);
	return nErrs;
}

/* ************************************************************************* */
/* ex39: the widest run must be measured over the WHOLE extent.
 *
 * This is a regression test for a bug that shipped green.  The Qube discovery
 * walk was clipping to the REQUESTING variable's bounds, and `sample` is
 * degenerate on indices 0 and 1, so those collapsed to extent 1 and the walk
 * measured "the widest run inside record 0, sensor 0" -- 2 -- and called it
 * the answer for the whole dataset.  The true maximum is 5.
 *
 * ex19 could not catch this: its longest row happens to BE record 0, so a
 * clipped walk returns the right number for the wrong reason.  Pick a fixture
 * whose maximum is not in the first record. */
static int test_qube_measures_whole_extent(void)
{
	int nErrs = 0;
	DasDs* pDs = NULL;
	DasStream* pSd = _open("examples/ex39_sandwich.d3t", 39, &pDs);
	MUST(pSd != NULL);

	DasVar* pSample = _pointVar(pDs, "sample");
	DasVar* pAmp    = _pointVar(pDs, "amp");
	MUST((pSample != NULL)&&(pAmp != NULL));

	/* sample lengths are [2,4,3] [1,2,5] [3,1,1] [2,2,2] -- max 5, and the 5
	   is in record 1.  A walk that stops early reports 2, 4 or 3.  The result
	   is dataset shaped, so sample repeats along the two indices it does not
	   vary on; the number under test is the LAST extent. */
	DasAry* pA = new_DasAry_subsetQube(pSample, 3, NULL, NULL, pAmp);
	MUST(pA != NULL);

	ptrdiff_t aShape[DASIDX_MAX];
	CHECK(DasAry_shape(pA, aShape) == 3);
	CHECK((aShape[0] == 4)&&(aShape[1] == 3));
	CHECK(aShape[2] == 5);                       /* not 2, which record 0 alone gives */

	size_t uLen = 0;
	const float* pVals = DasAry_floatsIn(pA, DAS_ADDR0, &uLen);
	MUST(pVals != NULL);
	CHECK(uLen == 60);
	for(int i = 0; i < 4; ++i)
		for(int j = 0; j < 3; ++j)
			for(int k = 0; k < 5; ++k)
				CHECK(pVals[i*15 + j*5 + k] == (float)k);
	dec_DasAry(pA);

	/* the data variable itself squares off to the same maximum */
	DasAry* pQ = new_DasAry_subsetQube(pAmp, 3, NULL, NULL, NULL);
	MUST(pQ != NULL);
	CHECK(DasAry_shape(pQ, aShape) == 3);
	CHECK((aShape[0]==4)&&(aShape[1]==3)&&(aShape[2]==5));
	CHECK(DasAry_size(pQ) == 60);
	dec_DasAry(pQ);

	del_DasDesc((DasDesc*)pSd);
	return nErrs;
}

/* ************************************************************************* */
/* ex39: the hard rail, and what is NOT covered by it.
 *
 * Dude, 2026-08-03: "you can never borrow outside of the dataset index range,
 * that's the hard rail."  ex39 declares <dataset index="*;3;*"> with a
 * <scalar index="-;^;-"> sequence, so index 1 IS 3 and the parser stamps it.
 * Before that, `sensor` was unreadable by all four entries.
 *
 * Index 2 is the control: the dataset declares `*` there, so `sample` keeps
 * its `^` and still needs a model or a named range.  A number resolves, a
 * raggedness does not. */
static int test_ex39_borrow_hard_rail(void)
{
	int nErrs = 0;
	DasDs* pDs = NULL;
	DasStream* pSd = _open("examples/ex39_sandwich.d3t", 39, &pDs);
	MUST(pSd != NULL);

	DasVar* pSensor = _pointVar(pDs, "sensor");
	DasVar* pSample = _pointVar(pDs, "sample");
	MUST((pSensor != NULL)&&(pSample != NULL));

	ptrdiff_t aShape[DASIDX_MAX];
	CHECK(DasVar_shape(pSensor, aShape) == 3);
	CHECK(aShape[1] == 3);                  /* resolved, not DASIDX_BORROW */
	CHECK(DasVar_shape(pSample, aShape) == 3);
	CHECK(aShape[2] == DASIDX_BORROW);      /* dataset is ragged there */

	daserr_setDisp(DASERRDISP_RET);

	/* the unresolvable one says so rather than guessing */
	REFUSES(new_DasAry_subsetQube(pSample, 3, NULL, NULL, NULL) == NULL);

	del_DasDesc((DasDesc*)pSd);
	return nErrs;
}

/* ************************************************************************* */
/* ex39: a variable is DATASET shaped -- degeneracy repeats, it does not vanish.
 *
 * das3/var.md, "Degeneracy alters the step size": a degenerate index gets
 * a step size of zero and reading out all the values for the overall dataset
 * iterates it at full extent.  Collapsing is something the CALLER asks for by
 * slicing, which is what das3_cdf does after DasVar_isDegenerate().
 *
 * The whole-extent call and the explicit dataset-range call must agree
 * exactly; a period where they did not was a regression introduced by the
 * NULL/NULL convenience inventing extent 1. */
static int test_degeneracy_repeats(void)
{
	int nErrs = 0;
	DasDs* pDs = NULL;
	DasStream* pSd = _open("examples/ex39_sandwich.d3t", 39, &pDs);
	MUST(pSd != NULL);

	DasVar* pSensor = _pointVar(pDs, "sensor");
	MUST(pSensor != NULL);
	CHECK(DasVar_isDegenerate(pSensor, 0));
	CHECK(!DasVar_isDegenerate(pSensor, 1));
	CHECK(DasVar_isDegenerate(pSensor, 2));

	ptrdiff_t aShape[DASIDX_MAX];

	/* no bounds: the variable reaches up to its dataset for the extents it
	   does not have itself */
	DasAry* pAll = new_DasAry_materializeQube(pSensor, 3, NULL, NULL, NULL);
	MUST(pAll != NULL);
	CHECK(DasAry_shape(pAll, aShape) == 3);
	CHECK((aShape[0]==4)&&(aShape[1]==3)&&(aShape[2]==5));
	CHECK(DasAry_size(pAll) == 60);

	size_t uLen = 0;
	const float* pVals = DasAry_floatsIn(pAll, DAS_ADDR0, &uLen);
	MUST(pVals != NULL);
	CHECK(uLen == 60);
	/* value depends on index 1 only: 5 copies of 0, then 1, then 2, per record */
	for(int i = 0; i < 4; ++i)
		for(int j = 0; j < 3; ++j)
			for(int k = 0; k < 5; ++k)
				CHECK(pVals[i*15 + j*5 + k] == (float)j);

	/* naming the same range explicitly must give the identical answer */
	ptrdiff_t aMin[3] = {0,0,0}, aMax[3] = {4,3,5};
	DasAry* pSame = new_DasAry_materializeQube(pSensor, 3, aMin, aMax, NULL);
	MUST(pSame != NULL);
	size_t uLen2 = 0;
	const float* pV2 = DasAry_floatsIn(pSame, DAS_ADDR0, &uLen2);
	MUST(pV2 != NULL);
	CHECK(uLen2 == uLen);
	CHECK(memcmp(pVals, pV2, uLen*sizeof(float)) == 0);
	dec_DasAry(pSame);
	dec_DasAry(pAll);

	/* and the compact form is one restricted range away */
	aMax[0] = 1; aMax[2] = 1;
	DasAry* pPin = new_DasAry_materializeQube(pSensor, 3, aMin, aMax, NULL);
	MUST(pPin != NULL);
	CHECK(DasAry_shape(pPin, aShape) == 1);
	CHECK(aShape[0] == 3);
	dec_DasAry(pPin);

	del_DasDesc((DasDesc*)pSd);
	return nErrs;
}

/* ************************************************************************* */
/* ex19: the Cassini call, in the form clients are meant to write it.
 *
 * Dude's convention, 2026-08-03 -- the caller knows which variable actually
 * spans the dataset, so it names it, and a reference-plus-offset time
 * materializes over it:
 *
 *     pAmpV    = DasDim_var(DasDs_physDim(pDs, "amplitude"), "center");
 *     pTimeV   = new_DasVarBin(ref, '+', offset);
 *     pAryTime = new_DasAry_materialize(pTimeV, DasDs_rank(pDs), NULL, NULL, pAmpV);
 *
 * The rule that makes it work: bounds come from the model, values from this
 * variable.  A ref+offset time has no extent of its own along the sample
 * index; the waveform does.  Rows are 2048 / 1536 / 2048. */
static int test_cassini_model_driven(void)
{
	int nErrs = 0;
	DasDs* pDs = NULL;
	DasStream* pSd = _open("examples/ex19_cassini_ragged_wfrm.d3t", 2, &pDs);
	MUST(pSd != NULL);

	DasDim* pTimeD = DasDs_dim(pDs, "time");
	MUST(pTimeD != NULL);
	DasVar* pAmpV = _pointVar(pDs, "Ex");
	MUST(pAmpV != NULL);

	DasVar* pBin = new_DasVarBin(
		DasDim_var(pTimeD, DASDIM_ROLE_REF), '+',
		DasDim_var(pTimeD, DASDIM_ROLE_OFFSET)
	);
	MUST(pBin != NULL);
	DasVar* pTimeV = pBin;

	const ptrdiff_t aRows[3] = {2048, 1536, 2048};
	ptrdiff_t aShape[DASIDX_MAX];

	DasAry* pAryTime = new_DasAry_materialize(
		pTimeV, DasDs_rank(pDs), NULL, NULL, pAmpV
	);
	MUST(pAryTime != NULL);
	CHECK(DasAry_shape(pAryTime, aShape) == 2);
	CHECK(aShape[0] == 3);
	CHECK(aShape[1] == DASIDX_RAGGED);
	CHECK(DasAry_size(pAryTime) == 5632);      /* 2048 + 1536 + 2048, no fill */
	CHECK(DasAry_ownsData(pAryTime));

	ptrdiff_t aAddr[DASIDX_MAX] = DASIDX_INIT_BEGIN;
	for(int i = 0; i < 3; ++i){
		aAddr[0] = i;
		CHECK(DasAry_lengthIn(pAryTime, 1, aAddr) == (size_t)aRows[i]);
	}

	/* the offset really was added: last sample of row 0 is 2047 * 0.01 s past
	   the reference, which lands on 03:03:34.668 */
	ptrdiff_t aLast[DASIDX_MAX] = {0, 2047, 0,0,0,0,0,0};
	const uint8_t* pT = DasAry_valAt(pAryTime, DASVT_TIME, 2, aLast);
	MUST(pT != NULL);
	char sBuf[64];
	DasTime_toIsoC((const DasTime*)pT, 3, sBuf, sizeof(sBuf));
	CHECK(strcmp(sBuf, "2004-03-28T03:03:34.668") == 0);
	dec_DasAry(pAryTime);

	/* the same call squared off pads row 1 out to the widest */
	DasAry* pQ = new_DasAry_materializeQube(
		pTimeV, DasDs_rank(pDs), NULL, NULL, pAmpV
	);
	MUST(pQ != NULL);
	CHECK(DasAry_shape(pQ, aShape) == 2);
	CHECK((aShape[0] == 3)&&(aShape[1] == 2048));
	CHECK(DasAry_size(pQ) == 6144);
	dec_DasAry(pQ);

	dec_DasVar(pTimeV);
	del_DasDesc((DasDesc*)pSd);
	return nErrs;
}

/* ************************************************************************* */
/* The ownership axis, which is independent of the shape axis.
 *
 * subset may hand back a view onto the stream's own storage; materialize never
 * does.  Both are legal answers to the same question, and a caller that needs
 * independence should be able to ASK rather than test-and-copy -- which is the
 * dance das2dlm still hand-writes at das2c_data.c:550. */
static int test_lend_vs_own(void)
{
	int nErrs = 0;
	DasDs* pDs = NULL;
	DasStream* pSd = _open("examples/ex35_strings_rank2.d3t", 34, &pDs);
	MUST(pSd != NULL);

	DasVar* pV = _pointVar(pDs, "status");
	MUST(pV != NULL);

	/* whole extent, natural shape: nothing to build, so the store is lent */
	DasAry* pLent = new_DasAry_allVals(pV, 2);
	MUST(pLent != NULL);
	CHECK(!DasAry_ownsData(pLent));
	dec_DasAry(pLent);

	DasAry* pOwn = new_DasAry_materialize(pV, 2, NULL, NULL, NULL);
	MUST(pOwn != NULL);
	CHECK(DasAry_ownsData(pOwn));

	/* an owned result outlives the stream it came from */
	ptrdiff_t aShape[DASIDX_MAX];
	int nRank = DasAry_shape(pOwn, aShape);
	del_DasDesc((DasDesc*)pSd);

	CHECK(DasAry_shape(pOwn, aShape) == nRank);
	CHECK(DasAry_size(pOwn) == 89);
	size_t uCt = 0;
	ptrdiff_t aAddr[DASIDX_MAX] = DASIDX_INIT_BEGIN;
	const uint8_t* pRun = DasAry_valsIn(pOwn, DASVT_UBYTE, 2, aAddr, &uCt);
	MUST(pRun != NULL);
	CHECK(strcmp((const char*)pRun, "NOMINAL") == 0);
	dec_DasAry(pOwn);

	return nErrs;
}

/* ************************************************************************* */

int main(int argc, char** argv)
{
	(void)argc;
	setvbuf(stdout, NULL, _IONBF, 0);

	daslib_init(argv[0], DASERRDISP_RET, DASLOG_ERROR, NULL);

	struct { const char* sName; int (*pFn)(void); } aCase[] = {
		{"test_ex35_both_axes",             test_ex35_both_axes},
		{"test_ex36_multilevel",            test_ex36_multilevel},
		{"test_ex27_blob_refuses_qube",     test_ex27_blob_refuses_qube},
		{"test_qube_measures_whole_extent", test_qube_measures_whole_extent},
		{"test_ex39_borrow_hard_rail",      test_ex39_borrow_hard_rail},
		{"test_degeneracy_repeats",         test_degeneracy_repeats},
		{"test_cassini_model_driven",       test_cassini_model_driven},
		{"test_lend_vs_own",                test_lend_vs_own},
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
		printf("ERROR: TestVarSubset: %d check(s) failed across %d of %d cases\n",
			nBadCheck, nBadCase, nCase);
		return 13;
	}

	printf("INFO: TestVarSubset: all bulk-read checks passed\n");
	return 0;
}
