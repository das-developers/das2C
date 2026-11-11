/** @file TestFilter.c  Unit tests for the dataset operations that das stream
 *  filters depend on: new_DasDs_copy(), DasGen_setArray() and DasDs_replaceAry().
 *
 *  These are the "copy the dataset, re-aim some storage, change the encodings"
 *  primitives a filter such as das3_text is built from.  The interesting
 *  behavior is reference counting on the shared backing arrays, so most of the
 *  checks below are ref-count assertions made through ref_DasAry().
 */

/* Author: C. Piker, via Claude Opus 4.8
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

#define _POSIX_C_SOURCE 200112L

#include <stdio.h>
#include <string.h>

#include <das3/core.h>
#include <das3/var_p.h>   /* tests link the static archive, so they may */
#include <das3/codec_p.h> /* white-box: pokes a codec field to prove copies are independent */

#define PROG "TestFilter"


#define CHECK(cond, ...) \
	if(!(cond)){ printf("FAIL (%s:%d): ", __func__, __LINE__); \
		printf(__VA_ARGS__); printf("\n"); return false; }

/* ************************************************************************* */
/* Helpers */

/* Pull the first dataset out of a freshly loaded stream */
static DasDs* first_ds(DasStream* pSd)
{
	return (DasDs*)DasStream_nextDesc(pSd, NULL, DASDESCTYPE_DS);
}

/* Find the first coordinate variable whose backing array is an integer or real
   epoch (a calendar-representable value not already stored as DasTime).  This
   is the thing das3_text has to convert to a DasTime before it can write
   ISO-8601, so it's what the swap helpers are exercised against. */
static DasVar* find_epoch_var(DasDs* pDs)
{
	size_t uDims = DasDs_numDims(pDs, DASDIMTYPE_COORD);
	for(size_t uD = 0; uD < uDims; ++uD){
		DasDim* pDim = DasDs_dimAt(pDs, uD, DASDIMTYPE_COORD);
		size_t uVars = DasDim_numVars(pDim);
		for(size_t uV = 0; uV < uVars; ++uV){
			DasVar* pVar = DasDim_varAt(pDim, uV);
			if(DasGen_type(DasVar_gen(pVar)) != gtArray) continue;
			DasAry* pAry = DasVar_ary(pVar);
			das_vt vt = DasAry_vt(pAry);
			if(((vt == DASVT_LONG)||(vt == DASVT_DOUBLE)) && DasUnits_isTimePoint(DasVar_units(pVar)))
				return pVar;
		}
	}
	return NULL;
}

/* Build a DasTime array with the same index shape as another array */
static DasAry* like_shaped_time_ary(const char* sId, DasAry* pModel)
{
	ptrdiff_t aShape[DASIDX_MAX] = DASIDX_INIT_UNUSED;
	int nRank = DasAry_shape(pModel, aShape);

	size_t aSz[DASIDX_MAX];
	for(int i = 0; i < nRank; ++i)
		aSz[i] = (aShape[i] < 1) ? 0 : (size_t)aShape[i];
	aSz[0] = 0;  /* index 0 always grows */

	return new_DasAry(sId, DASVT_TIME, 0, NULL, nRank, aSz, DASUNITS_UTC);
}

/* ************************************************************************* */
/* Test 1: new_DasDs_copy shares arrays and mirrors structure */

static bool test_copy_structure(const char* sFile)
{
	DasStream* pSd = new_DasStream_file(PROG, sFile);
	CHECK(pSd != NULL, "could not load %s", sFile);
	DasDs* pSrc = first_ds(pSd);
	CHECK(pSrc != NULL, "no dataset in %s", sFile);

	DasDs* pCopy = new_DasDs_copy(pSrc);
	CHECK(pCopy != NULL, "new_DasDs_copy returned NULL");
	CHECK(pCopy != pSrc, "copy aliased the source");

	/* Same shape of structure */
	CHECK(DasDs_rank(pCopy) == DasDs_rank(pSrc), "rank mismatch");
	CHECK(DasDs_numDims(pCopy, DASDIMTYPE_COORD) == DasDs_numDims(pSrc, DASDIMTYPE_COORD),
		"coord dim count mismatch");
	CHECK(DasDs_numDims(pCopy, DASDIMTYPE_DATA) == DasDs_numDims(pSrc, DASDIMTYPE_DATA),
		"data dim count mismatch");
	CHECK(DasDs_numCodecs(pCopy) == DasDs_numCodecs(pSrc), "codec count mismatch");
	CHECK(DasDs_numArys(pCopy) == DasDs_numArys(pSrc), "array count mismatch");

	/* Arrays must be SHARED (same object), not deep copied, and each must have
	   gained exactly one reference for the copy's array-list slot plus one per
	   variable that points at it.  We can at least prove sharing + a bumped
	   count. */
	for(size_t u = 0; u < DasDs_numArys(pSrc); ++u){
		DasAry* pA = DasDs_aryAt(pSrc, u);
		DasAry* pB = DasDs_ary(pCopy, DasAry_id(pA));
		CHECK(pB == pA, "array '%s' was copied, not shared", DasAry_id(pA));
		CHECK(ref_DasAry(pA) >= 2, "array '%s' ref not bumped by copy", DasAry_id(pA));
	}

	del_DasDesc((DasDesc*)pCopy);
	del_DasDesc((DasDesc*)pSd);
	printf("PASS: new_DasDs_copy structure + array sharing on %s\n", sFile);
	return true;
}

/* ************************************************************************* */
/* Test 2: mutating the copy's codec leaves the source codec alone */

static bool test_copy_independent_codecs(const char* sFile)
{
	DasStream* pSd = new_DasStream_file(PROG, sFile);
	CHECK(pSd != NULL, "could not load %s", sFile);
	DasDs* pSrc = first_ds(pSd);
	CHECK(pSrc != NULL, "no dataset in %s", sFile);

	CHECK(DasDs_numCodecs(pSrc) > 0, "no codecs in %s", sFile);

	DasDs* pCopy = new_DasDs_copy(pSrc);
	CHECK(pCopy != NULL, "new_DasDs_copy returned NULL");

	/* The header promises the copied codecs can be changed without touching the
	   original.  Prove they're independent memory by poking a field on the copy
	   and confirming the source's codec is unmoved. */
	int16_t nSrcWidth = DasDs_codecAt(pSrc, 0)->nBufValSz;
	DasDs_codecAt(pCopy, 0)->nBufValSz = (int16_t)(nSrcWidth + 99);

	CHECK(DasDs_codecAt(pCopy, 0)->nBufValSz == nSrcWidth + 99,
		"copy codec field did not change");
	CHECK(DasDs_codecAt(pSrc, 0)->nBufValSz == nSrcWidth,
		"mutating the copy changed the SOURCE codec (was %d, now %d)",
		nSrcWidth, DasDs_codecAt(pSrc, 0)->nBufValSz);

	del_DasDesc((DasDesc*)pCopy);
	del_DasDesc((DasDesc*)pSd);
	printf("PASS: new_DasDs_copy codec independence on %s\n", sFile);
	return true;
}

/* ************************************************************************* */
/* Test 3: DasGen_setArray re-aims a variable and re-derives its surface */

static bool test_set_array(const char* sFile)
{
	DasStream* pSd = new_DasStream_file(PROG, sFile);
	CHECK(pSd != NULL, "could not load %s", sFile);
	DasDs* pSrc = first_ds(pSd);
	CHECK(pSrc != NULL, "no dataset in %s", sFile);

	DasDs* pCopy = new_DasDs_copy(pSrc);
	CHECK(pCopy != NULL, "new_DasDs_copy returned NULL");

	DasVar* pVar = find_epoch_var(pCopy);
	CHECK(pVar != NULL, "no epoch coordinate var in %s", sFile);
	DasAry* pEpoch = DasVar_ary(pVar);
	int nEpochRefBefore = ref_DasAry(pEpoch);

	DasAry* pTime = like_shaped_time_ary("test_time", pEpoch);
	CHECK(pTime != NULL, "could not build DasTime array");
	CHECK(ref_DasAry(pTime) == 1, "fresh array should have one reference");

	CHECK(DasVar_setAry(pVar, pTime), "DasVarAry_setArray failed");

	/* Surface type, units and semantic all re-derived from the new array */
	CHECK(DasVar_vt(pVar) == DASVT_TIME, "var did not become DASVT_TIME");
	CHECK(DasVar_units(pVar) == DASUNITS_UTC, "var units did not become UTC");

	/* References moved: var dropped the epoch array and took the time array */
	CHECK(ref_DasAry(pEpoch) == nEpochRefBefore - 1, "epoch ref not released by var");
	CHECK(ref_DasAry(pTime) == 2, "time array ref not taken by var");

	/* Source var is untouched (still an epoch) */
	DasVar* pSrcVar = find_epoch_var(pSrc);
	CHECK(pSrcVar != NULL, "source epoch var vanished");
	CHECK(DasVar_vt(pSrcVar) != DASVT_TIME, "setArray leaked into the source dataset");

	del_DasDesc((DasDesc*)pCopy);     /* var releases pTime here */
	dec_DasAry(pTime);    /* drop our own creation reference */
	del_DasDesc((DasDesc*)pSd);
	printf("PASS: DasGen_setArray on %s\n", sFile);
	return true;
}

/* ************************************************************************* */
/* Test 4: DasDs_replaceAry swaps the array list + repoints vars, ref balanced */

static bool test_replace_ary(const char* sFile)
{
	DasStream* pSd = new_DasStream_file(PROG, sFile);
	CHECK(pSd != NULL, "could not load %s", sFile);
	DasDs* pSrc = first_ds(pSd);
	CHECK(pSrc != NULL, "no dataset in %s", sFile);

	DasDs* pCopy = new_DasDs_copy(pSrc);
	CHECK(pCopy != NULL, "new_DasDs_copy returned NULL");

	DasVar* pVar = find_epoch_var(pCopy);
	CHECK(pVar != NULL, "no epoch coordinate var in %s", sFile);
	DasAry* pEpoch = DasVar_ary(pVar);
	char sEpochId[64];
	strncpy(sEpochId, DasAry_id(pEpoch), sizeof(sEpochId) - 1);
	sEpochId[sizeof(sEpochId)-1] = '\0';

	int nEpochBefore = ref_DasAry(pEpoch);  /* held by src list+var and copy list+var */

	DasAry* pTime = like_shaped_time_ary("iso_time", pEpoch);
	CHECK(pTime != NULL, "could not build DasTime array");

	CHECK(DasDs_replaceAry(pCopy, sEpochId, pTime) == DAS_OKAY,
		"DasDs_replaceAry failed");

	/* The copy's var now reads the time array; the copy's list slot holds it */
	CHECK(DasVar_ary(pVar) == pTime, "var not repointed to the new array");
	CHECK(DasDs_ary(pCopy, "iso_time") == pTime, "list slot not swapped");
	CHECK(DasVar_vt(pVar) == DASVT_TIME, "repointed var not DASVT_TIME");

	/* The copy released both of its holds on the epoch array (list + var) */
	CHECK(ref_DasAry(pEpoch) == nEpochBefore - 2,
		"epoch refs not released by replaceAry (was %d, now %d)",
		nEpochBefore, ref_DasAry(pEpoch));
	/* time array now held by us, the copy's list, and the copy's var */
	CHECK(ref_DasAry(pTime) == 3, "time array ref count wrong: %d", ref_DasAry(pTime));

	/* Source dataset is completely untouched */
	DasVar* pSrcVar = find_epoch_var(pSrc);
	CHECK(pSrcVar != NULL, "source epoch var vanished");
	CHECK(DasVar_vt(pSrcVar) != DASVT_TIME, "replaceAry leaked into the source dataset");

	del_DasDesc((DasDesc*)pCopy);   /* drops the copy's list + var references on pTime */
	CHECK(ref_DasAry(pTime) == 1, "time array should be down to our reference, is %d",
		ref_DasAry(pTime));
	dec_DasAry(pTime);  /* frees it */

	del_DasDesc((DasDesc*)pSd);
	printf("PASS: DasDs_replaceAry + ref balance on %s\n", sFile);
	return true;
}

/* ************************************************************************* */
/* Test 4b: array ids name exactly one array, and a codec is re-aimed through
   its dataset by id, so it can only ever serve an array that dataset holds. */

static bool test_update_codec(const char* sFile)
{
	DasStream* pSd = new_DasStream_file(PROG, sFile);
	CHECK(pSd != NULL, "could not load %s", sFile);
	DasDs* pSrc = first_ds(pSd);
	CHECK(pSrc != NULL, "no dataset in %s", sFile);
	DasDs* pCopy = new_DasDs_copy(pSrc);
	CHECK(pCopy != NULL, "new_DasDs_copy returned NULL");

	DasVar* pVar = find_epoch_var(pCopy);
	CHECK(pVar != NULL, "no epoch coordinate var in %s", sFile);
	DasAry* pEpoch = DasVar_ary(pVar);
	char sEpochId[64];
	strncpy(sEpochId, DasAry_id(pEpoch), sizeof(sEpochId) - 1);
	sEpochId[sizeof(sEpochId)-1] = '\0';

	/* Unique ids: the same array twice, or a second array under a taken id */
	CHECK(DasDs_addAry(pCopy, pEpoch) != DAS_OKAY, "same array listed twice");
	DasAry* pTwin = like_shaped_time_ary(sEpochId, pEpoch);
	CHECK(pTwin != NULL, "could not build twin array");
	CHECK(DasDs_addAry(pCopy, pTwin) != DAS_OKAY, "duplicate array id accepted");
	CHECK(DasDs_replaceAry(pCopy, sEpochId, pTwin) == DAS_OKAY,
		"replace with the SAME id is a swap, not a collision");
	CHECK(DasDs_replaceAry(pCopy, sEpochId, pEpoch) == DAS_OKAY, "swap back failed");
	dec_DasAry(pTwin);

	/* Find the codec serving the epoch array */
	size_t uCd = DasDs_numCodecs(pCopy);
	for(size_t u = 0; u < DasDs_numCodecs(pCopy); ++u)
		if(DasCodec_ary(DasDs_codecAt(pCopy, u)) == pEpoch){ uCd = u; break; }
	CHECK(uCd < DasDs_numCodecs(pCopy), "no codec for the epoch array");

	DasAry* pTime = like_shaped_time_ary("iso_time", pEpoch);
	CHECK(pTime != NULL, "could not build DasTime array");
	CHECK(DasDs_replaceAry(pCopy, sEpochId, pTime) == DAS_OKAY, "replaceAry failed");
	CHECK(DasCodec_ary(DasDs_codecAt(pCopy, uCd)) == pEpoch,
		"replaceAry is documented to leave codecs alone");

	/* Refusals: a bad index, and an id this dataset doesn't hold (the epoch
	   array still exists, in the source dataset, but not in this list) */
	CHECK(DasDs_updateCodec(pCopy, DasDs_numCodecs(pCopy), DASCODEC_WRITE, NULL,
		NULL, 0, '\0', NULL, NULL) != DAS_OKAY, "out of range codec accepted");
	CHECK(DasDs_updateCodec(pCopy, uCd, DASCODEC_WRITE, sEpochId, "utf8", 30, ' ',
		NULL, NULL) != DAS_OKAY, "codec aimed at an array outside its dataset");

	int nTimeBefore = ref_DasAry(pTime);
	CHECK(DasDs_updateCodec(pCopy, uCd, DASCODEC_WRITE, "iso_time", "utf8", 30, ' ',
		NULL, NULL) == DAS_OKAY, "updateCodec by id failed");
	const DasCodec* pCd = DasDs_codecAt(pCopy, uCd);
	CHECK(DasCodec_ary(pCd) == pTime, "codec not re-aimed");
	CHECK(DasCodec_isText(pCd) && !DasCodec_isReader(pCd), "codec not a text writer");
	CHECK(ref_DasAry(pTime) == nTimeBefore + 1, "codec did not take a reference");

	del_DasDesc((DasDesc*)pCopy);   /* list, var and codec references go */
	CHECK(ref_DasAry(pTime) == 1, "time array should be down to our reference, is %d",
		ref_DasAry(pTime));
	dec_DasAry(pTime);

	del_DasDesc((DasDesc*)pSd);
	printf("PASS: unique array ids + DasDs_updateCodec on %s\n", sFile);
	return true;
}

/* ************************************************************************* */
/* Test 5: a dataset header survives encode -> re-parse.  This is the das3
   "conservative output" contract: DasDs_encodeHdr() must emit a header the
   reader accepts.  It catches encoder bugs that new_DasDs_copy/setArray can't -- a string
   var serialized with storage="ubyte", or a <sequence> whose minval comes out
   as a printf format. */

static bool test_header_roundtrip(const char* sFile)
{
	DasStream* pSd = new_DasStream_file(PROG, sFile);
	CHECK(pSd != NULL, "could not load %s", sFile);
	DasDs* pSrc = first_ds(pSd);
	CHECK(pSrc != NULL, "no dataset in %s", sFile);

	DasBuf* pBuf = new_DasBuf(256*1024);
	CHECK(pBuf != NULL, "could not allocate buffer");
	CHECK(DasDs_encodeHdr(pSrc, pBuf) == DAS_OKAY, "DasDs_encodeHdr failed for %s", sFile);

	DasDs* pBack = new_DasDs_xml(pBuf, (DasDesc*)pSd, 1);
	CHECK(pBack != NULL,
		"re-parse of the encoded header failed -- encoder emitted invalid das3 for %s",
		sFile);

	CHECK(DasDs_rank(pBack) == DasDs_rank(pSrc), "rank changed across round-trip");
	CHECK(DasDs_numDims(pBack, DASDIMTYPE_COORD) == DasDs_numDims(pSrc, DASDIMTYPE_COORD),
		"coord dim count changed across round-trip");
	CHECK(DasDs_numDims(pBack, DASDIMTYPE_DATA) == DasDs_numDims(pSrc, DASDIMTYPE_DATA),
		"data dim count changed across round-trip");

	del_DasDesc((DasDesc*)pBack);
	del_DasBuf(pBuf);
	del_DasDesc((DasDesc*)pSd);
	printf("PASS: header round-trip on %s\n", sFile);
	return true;
}

/* ************************************************************************* */

int main(int argc, char** argv)
{
	/* Return on error rather than exit, so a bad encode surfaces as a failed
	   check instead of killing the test run. */
	daslib_init(argv[0], DASERRDISP_RET, DASLOG_WARN, NULL);

	/* ex12: rich rank-3 (time + reals + sequences + inline values) */
	/* ex21: TRACERS L2 shape, TT2000 epoch coord + ragged string */
	const char* sRich  = "examples/ex12_sounder_xyz.d3t";
	const char* sEpoch = "examples/ex21_tracers_cdpu_status.d3b";

	if(!test_copy_structure(sRich))           return 13;
	if(!test_copy_structure(sEpoch))          return 13;
	if(!test_copy_independent_codecs(sEpoch)) return 13;
	if(!test_set_array(sEpoch))               return 13;
	if(!test_replace_ary(sEpoch))             return 13;
	if(!test_update_codec(sEpoch))            return 13;
	if(!test_header_roundtrip(sEpoch))        return 13;  /* string var header  */
	if(!test_header_roundtrip(sRich))         return 13;  /* <sequence> + reals */

	printf("INFO: All %s checks passed.\n", PROG);
	return 0;
}
