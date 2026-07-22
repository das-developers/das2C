/** @file TestFilter.c  Unit tests for the dataset operations that das stream
 *  filters depend on: DasDs_copy(), DasVarAry_setArray() and DasDs_replaceAry().
 *
 *  These are the "copy the dataset, re-aim some storage, change the encodings"
 *  primitives a filter such as das3_text is built from.  The interesting
 *  behavior is reference counting on the shared backing arrays, so most of the
 *  checks below are ref-count assertions made through ref_DasAry().
 */

/* Author: C. Piker, via Claude Opus 4.8
 *
 * This file contains test and example code that intends to explain an
 * interface.
 *
 * As United States courts have ruled that interfaces cannot be copyrighted,
 * the code in this individual source file, TestFilter.c, is placed into the
 * public domain and may be displayed, incorporated or otherwise re-used without
 * restriction.  It is offered to the public without any warranty including even
 * the implied warranty of merchantability or fitness for a particular purpose.
 */

#define _POSIX_C_SOURCE 200112L

#include <stdio.h>
#include <string.h>

#include <das2/core.h>

#define PROG "TestFilter"


#define CHECK(cond, ...) \
	if(!(cond)){ printf("FAIL (%s:%d): ", __func__, __LINE__); \
		printf(__VA_ARGS__); printf("\n"); return false; }

/* ************************************************************************* */
/* Helpers */

/* Pull the first dataset out of a freshly loaded stream */
static DasDs* first_ds(DasStream* pSd)
{
	int nPktId = 0;
	DasDesc* pDesc = NULL;
	while((pDesc = DasStream_nextDesc(pSd, &nPktId)) != NULL){
		if(DasDesc_type(pDesc) == DATASET)
			return (DasDs*)pDesc;
	}
	return NULL;
}

/* Find the first coordinate variable whose backing array is an integer or real
   epoch (a calendar-representable value not already stored as das_time).  This
   is the thing das3_text has to convert to a das_time before it can write
   ISO-8601, so it's what the swap helpers are exercised against. */
static DasVar* find_epoch_var(DasDs* pDs)
{
	size_t uDims = DasDs_numDims(pDs, DASDIM_COORD);
	for(size_t uD = 0; uD < uDims; ++uD){
		DasDim* pDim = DasDs_getDimByIdx(pDs, uD, DASDIM_COORD);
		size_t uVars = DasDim_numVars(pDim);
		for(size_t uV = 0; uV < uVars; ++uV){
			DasVar* pVar = DasDim_getVarByIdx(pDim, uV);
			if(DasVar_type(pVar) != D2V_ARRAY) continue;
			DasAry* pAry = DasVar_getArray(pVar);
			das_val_type vt = DasAry_valType(pAry);
			if(((vt == vtLong)||(vt == vtDouble)) && Units_haveCalRep(DasVar_units(pVar)))
				return pVar;
		}
	}
	return NULL;
}

/* Build a das_time array with the same index shape as another array */
static DasAry* like_shaped_time_ary(const char* sId, DasAry* pModel)
{
	ptrdiff_t aShape[DASIDX_MAX] = DASIDX_INIT_UNUSED;
	int nRank = DasAry_shape(pModel, aShape);

	size_t aSz[DASIDX_MAX];
	for(int i = 0; i < nRank; ++i)
		aSz[i] = (aShape[i] < 1) ? 0 : (size_t)aShape[i];
	aSz[0] = 0;  /* index 0 always grows */

	return new_DasAry(sId, vtTime, 0, NULL, nRank, aSz, UNIT_UTC);
}

/* ************************************************************************* */
/* Test 1: DasDs_copy shares arrays and mirrors structure */

static bool test_copy_structure(const char* sFile)
{
	DasStream* pSd = stream_from_path(PROG, sFile);
	CHECK(pSd != NULL, "could not load %s", sFile);
	DasDs* pSrc = first_ds(pSd);
	CHECK(pSrc != NULL, "no dataset in %s", sFile);

	DasDs* pCopy = DasDs_copy(pSrc);
	CHECK(pCopy != NULL, "DasDs_copy returned NULL");
	CHECK(pCopy != pSrc, "copy aliased the source");

	/* Same shape of structure */
	CHECK(DasDs_rank(pCopy) == DasDs_rank(pSrc), "rank mismatch");
	CHECK(DasDs_numDims(pCopy, DASDIM_COORD) == DasDs_numDims(pSrc, DASDIM_COORD),
		"coord dim count mismatch");
	CHECK(DasDs_numDims(pCopy, DASDIM_DATA) == DasDs_numDims(pSrc, DASDIM_DATA),
		"data dim count mismatch");
	CHECK(DasDs_numCodecs(pCopy) == DasDs_numCodecs(pSrc), "codec count mismatch");
	CHECK(DasDs_numAry(pCopy) == DasDs_numAry(pSrc), "array count mismatch");

	/* Arrays must be SHARED (same object), not deep copied, and each must have
	   gained exactly one reference for the copy's array-list slot plus one per
	   variable that points at it.  We can at least prove sharing + a bumped
	   count. */
	for(size_t u = 0; u < DasDs_numAry(pSrc); ++u){
		DasAry* pA = DasDs_getAry(pSrc, u);
		DasAry* pB = DasDs_getAryById(pCopy, DasAry_id(pA));
		CHECK(pB == pA, "array '%s' was copied, not shared", DasAry_id(pA));
		CHECK(ref_DasAry(pA) >= 2, "array '%s' ref not bumped by copy", DasAry_id(pA));
	}

	del_DasDs(pCopy);
	del_DasStream(pSd);
	printf("PASS: DasDs_copy structure + array sharing on %s\n", sFile);
	return true;
}

/* ************************************************************************* */
/* Test 2: mutating the copy's codec leaves the source codec alone */

static bool test_copy_independent_codecs(const char* sFile)
{
	DasStream* pSd = stream_from_path(PROG, sFile);
	CHECK(pSd != NULL, "could not load %s", sFile);
	DasDs* pSrc = first_ds(pSd);
	CHECK(pSrc != NULL, "no dataset in %s", sFile);

	CHECK(DasDs_numCodecs(pSrc) > 0, "no codecs in %s", sFile);

	DasDs* pCopy = DasDs_copy(pSrc);
	CHECK(pCopy != NULL, "DasDs_copy returned NULL");

	/* The header promises the copied codecs can be changed without touching the
	   original.  Prove they're independent memory by poking a field on the copy
	   and confirming the source's codec is unmoved. */
	int16_t nSrcWidth = DasDs_getCodec(pSrc, 0)->nBufValSz;
	DasDs_getCodec(pCopy, 0)->nBufValSz = (int16_t)(nSrcWidth + 99);

	CHECK(DasDs_getCodec(pCopy, 0)->nBufValSz == nSrcWidth + 99,
		"copy codec field did not change");
	CHECK(DasDs_getCodec(pSrc, 0)->nBufValSz == nSrcWidth,
		"mutating the copy changed the SOURCE codec (was %d, now %d)",
		nSrcWidth, DasDs_getCodec(pSrc, 0)->nBufValSz);

	del_DasDs(pCopy);
	del_DasStream(pSd);
	printf("PASS: DasDs_copy codec independence on %s\n", sFile);
	return true;
}

/* ************************************************************************* */
/* Test 3: DasVarAry_setArray re-aims a variable and re-derives its surface */

static bool test_set_array(const char* sFile)
{
	DasStream* pSd = stream_from_path(PROG, sFile);
	CHECK(pSd != NULL, "could not load %s", sFile);
	DasDs* pSrc = first_ds(pSd);
	CHECK(pSrc != NULL, "no dataset in %s", sFile);

	DasDs* pCopy = DasDs_copy(pSrc);
	CHECK(pCopy != NULL, "DasDs_copy returned NULL");

	DasVar* pVar = find_epoch_var(pCopy);
	CHECK(pVar != NULL, "no epoch coordinate var in %s", sFile);
	DasAry* pEpoch = DasVar_getArray(pVar);
	int nEpochRefBefore = ref_DasAry(pEpoch);

	DasAry* pTime = like_shaped_time_ary("test_time", pEpoch);
	CHECK(pTime != NULL, "could not build das_time array");
	CHECK(ref_DasAry(pTime) == 1, "fresh array should have one reference");

	CHECK(DasVarAry_setArray(pVar, pTime), "DasVarAry_setArray failed");

	/* Surface type, units and semantic all re-derived from the new array */
	CHECK(DasVar_valType(pVar) == vtTime, "var did not become vtTime");
	CHECK(DasVar_units(pVar) == UNIT_UTC, "var units did not become UTC");

	/* References moved: var dropped the epoch array and took the time array */
	CHECK(ref_DasAry(pEpoch) == nEpochRefBefore - 1, "epoch ref not released by var");
	CHECK(ref_DasAry(pTime) == 2, "time array ref not taken by var");

	/* Source var is untouched (still an epoch) */
	DasVar* pSrcVar = find_epoch_var(pSrc);
	CHECK(pSrcVar != NULL, "source epoch var vanished");
	CHECK(DasVar_valType(pSrcVar) != vtTime, "setArray leaked into the source dataset");

	del_DasDs(pCopy);     /* var releases pTime here */
	dec_DasAry(pTime);    /* drop our own creation reference */
	del_DasStream(pSd);
	printf("PASS: DasVarAry_setArray on %s\n", sFile);
	return true;
}

/* ************************************************************************* */
/* Test 4: DasDs_replaceAry swaps the array list + repoints vars, ref balanced */

static bool test_replace_ary(const char* sFile)
{
	DasStream* pSd = stream_from_path(PROG, sFile);
	CHECK(pSd != NULL, "could not load %s", sFile);
	DasDs* pSrc = first_ds(pSd);
	CHECK(pSrc != NULL, "no dataset in %s", sFile);

	DasDs* pCopy = DasDs_copy(pSrc);
	CHECK(pCopy != NULL, "DasDs_copy returned NULL");

	DasVar* pVar = find_epoch_var(pCopy);
	CHECK(pVar != NULL, "no epoch coordinate var in %s", sFile);
	DasAry* pEpoch = DasVar_getArray(pVar);
	char sEpochId[64];
	strncpy(sEpochId, DasAry_id(pEpoch), sizeof(sEpochId) - 1);
	sEpochId[sizeof(sEpochId)-1] = '\0';

	int nEpochBefore = ref_DasAry(pEpoch);  /* held by src list+var and copy list+var */

	DasAry* pTime = like_shaped_time_ary("iso_time", pEpoch);
	CHECK(pTime != NULL, "could not build das_time array");

	CHECK(DasDs_replaceAry(pCopy, sEpochId, pTime) == DAS_OKAY,
		"DasDs_replaceAry failed");

	/* The copy's var now reads the time array; the copy's list slot holds it */
	CHECK(DasVar_getArray(pVar) == pTime, "var not repointed to the new array");
	CHECK(DasDs_getAryById(pCopy, "iso_time") == pTime, "list slot not swapped");
	CHECK(DasVar_valType(pVar) == vtTime, "repointed var not vtTime");

	/* The copy released both of its holds on the epoch array (list + var) */
	CHECK(ref_DasAry(pEpoch) == nEpochBefore - 2,
		"epoch refs not released by replaceAry (was %d, now %d)",
		nEpochBefore, ref_DasAry(pEpoch));
	/* time array now held by us, the copy's list, and the copy's var */
	CHECK(ref_DasAry(pTime) == 3, "time array ref count wrong: %d", ref_DasAry(pTime));

	/* Source dataset is completely untouched */
	DasVar* pSrcVar = find_epoch_var(pSrc);
	CHECK(pSrcVar != NULL, "source epoch var vanished");
	CHECK(DasVar_valType(pSrcVar) != vtTime, "replaceAry leaked into the source dataset");

	del_DasDs(pCopy);   /* drops the copy's list + var references on pTime */
	CHECK(ref_DasAry(pTime) == 1, "time array should be down to our reference, is %d",
		ref_DasAry(pTime));
	dec_DasAry(pTime);  /* frees it */

	del_DasStream(pSd);
	printf("PASS: DasDs_replaceAry + ref balance on %s\n", sFile);
	return true;
}

/* ************************************************************************* */
/* Test 5: a dataset header survives encode -> re-parse.  This is the das3
   "conservative output" contract: DasDs_encodeHdr() must emit a header the
   reader accepts.  It catches encoder bugs that DasDs_copy/setArray can't -- a string
   var serialized with storage="ubyte", or a <sequence> whose minval comes out
   as a printf format. */

static bool test_header_roundtrip(const char* sFile)
{
	DasStream* pSd = stream_from_path(PROG, sFile);
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
	CHECK(DasDs_numDims(pBack, DASDIM_COORD) == DasDs_numDims(pSrc, DASDIM_COORD),
		"coord dim count changed across round-trip");
	CHECK(DasDs_numDims(pBack, DASDIM_DATA) == DasDs_numDims(pSrc, DASDIM_DATA),
		"data dim count changed across round-trip");

	del_DasDs(pBack);
	del_DasBuf(pBuf);
	del_DasStream(pSd);
	printf("PASS: header round-trip on %s\n", sFile);
	return true;
}

/* ************************************************************************* */

int main(int argc, char** argv)
{
	/* Return on error rather than exit, so a bad encode surfaces as a failed
	   check instead of killing the test run. */
	das_init(argv[0], DASERR_DIS_RET, 0, DASLOG_WARN, NULL);

	/* ex12: rich rank-3 (time + reals + sequences + inline values) */
	/* ex21: TRACERS L2 shape, TT2000 epoch coord + ragged string */
	const char* sRich  = "test/ex12_sounder_xyz.d3t";
	const char* sEpoch = "test/ex21_tracers_cdpu_status.d3b";

	if(!test_copy_structure(sRich))           return 13;
	if(!test_copy_structure(sEpoch))          return 13;
	if(!test_copy_independent_codecs(sEpoch)) return 13;
	if(!test_set_array(sEpoch))               return 13;
	if(!test_replace_ary(sEpoch))             return 13;
	if(!test_header_roundtrip(sEpoch))        return 13;  /* string var header  */
	if(!test_header_roundtrip(sRich))         return 13;  /* <sequence> + reals */

	printf("INFO: All %s checks passed.\n", PROG);
	return 0;
}
