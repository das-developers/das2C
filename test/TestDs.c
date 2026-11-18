/** @file TestDs.c Unit tests for the DasDs container (das3/datset.h).
 *
 * TestVar.c proves the per-variable lengthIn() answers in isolation.  This
 * test closes the stack at the top: it asserts DasDs_lengthIn() and
 * DasDim_lengthIn() -- the merge layers that combine every variable in a
 * dimension / dataset and feed the iterators -- on real built datasets.
 *
 * Two fixtures, two geometries:
 *   ex12  cubic   rank 3, shape [3, 160, 80]   (the merge must AGREE with shape)
 *   ex19  ragged  rank 2, shape [3, *]          rows 2048 / 1536 / 2048
 *
 * Before the var_ary.c / var_seq.c lengthIn fixes a non-mapping variable
 * reported a bogus length here and the MIN-merge dragged the answer off; this
 * test would have gone red on both fixtures. */

/* Author: Chris Piker <chris-piker@uiowa.edu>
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

#include <string.h>

#include <das3/core.h>
#include <das3/datset_p.h>  /* DASDS_LOC_ENC_SZ, the small vector size */

const char* g_sProg = "TestDs";

/* Pull dataset packet nPktId out of a freshly read stream.  Caller owns the
   returned stream (del_DasDesc) on success. */
static DasDs* load_ds(const char* sFile, int nPktId, DasStream** ppSd, int nErr)
{
	DasStream* pSd = new_DasStream_file(g_sProg, sFile);
	if(pSd == NULL){ daserrf(nErr, "Couldn't read %s", sFile); return NULL; }

	DasDesc* pDesc = DasStream_desc(pSd, nPktId);
	if((pDesc == NULL)||(DasDesc_type(pDesc) != DASDESCTYPE_DS)){
		del_DasDesc((DasDesc*)pSd);
		daserrf(nErr, "%s packet %d is not a dataset", sFile, nPktId);
		return NULL;
	}
	*ppSd = pSd;
	return (DasDs*)pDesc;
}

/* DasDs_addAry ADDS a reference: the dataset takes its own and the caller
   still owns the one it made.  Pinned here because the alternative -- stealing
   the caller's reference -- is indistinguishable at the call site and fails as
   a double free rather than a leak. */
static int test_addary_refs(int nErr)
{
	float rFill = -1.0f;
	DasAry* pAry = new_DasAry(
		"refs", DASVT_FLOAT, 0, (const uint8_t*)&rFill, DAS_SHAPE1(0), DASUNITS_DIMENSIONLESS
	);
	if(pAry == NULL) return daserrf(nErr, "no array");
	if(ref_DasAry(pAry) != 1)
		return daserrf(nErr, "fresh array has %d references", ref_DasAry(pAry));

	DasDs* pDs = new_DasDs("refs_ds", "refs_grp", 1);
	if(pDs == NULL) return daserrf(nErr, "no dataset");

	if(DasDs_addAry(pDs, pAry) != DAS_OKAY)
		return daserrf(nErr, "addAry refused");
	if(ref_DasAry(pAry) != 2)
		return daserrf(nErr, "after addAry: %d references, expected 2 "
			"(mine + the dataset's)", ref_DasAry(pAry));

	/* drop mine; the dataset's hold keeps it alive */
	dec_DasAry(pAry);
	if(ref_DasAry(pAry) != 1)
		return daserrf(nErr, "after my release: %d references, expected 1",
			ref_DasAry(pAry));

	del_DasDesc((DasDesc*)pDs);   /* releases the last one */

	daslog_info("Test 0 success. DasDs_addAry adds a reference.");
	return 0;
}

/* Codecs live in a small vector inside the dataset until one more than
   DASDS_LOC_ENC_SZ is added, then move to the heap and double from there.
   Teardown once keyed off the count and freed the small vector at exactly
   DASDS_LOC_ENC_SZ codecs.  Each count is built and torn down whole: the
   ones either side of each move to a new block are the cases. */
static int test_codec_counts(int nErr)
{
	const size_t aCounts[] = {
		DASDS_LOC_ENC_SZ - 1, DASDS_LOC_ENC_SZ, DASDS_LOC_ENC_SZ + 1,
		DASDS_LOC_ENC_SZ*2,   DASDS_LOC_ENC_SZ*2 + 1
	};
	float rFill = -1.0f;
	char sId[32];

	for(size_t c = 0; c < sizeof(aCounts)/sizeof(size_t); ++c){
		DasDs* pDs = new_DasDs("codec_ds", "codec_grp", 1);
		if(pDs == NULL) return daserrf(nErr, "no dataset");

		for(size_t u = 0; u < aCounts[c]; ++u){
			snprintf(sId, sizeof(sId), "ary_%zu", u);
			DasAry* pAry = new_DasAry(
				sId, DASVT_FLOAT, 0, (const uint8_t*)&rFill, DAS_SHAPE1(0),
				DASUNITS_DIMENSIONLESS
			);
			if(pAry == NULL) return daserrf(nErr, "no array");
			if(DasDs_addAry(pDs, pAry) != DAS_OKAY)
				return daserrf(nErr, "addAry refused %s", sId);
			dec_DasAry(pAry);

			if(DasDs_makeFixedCodec(
				pDs, sId, "real", "LEreal", 4, 1, DASCODEC_WRITE
			) == NULL)
				return daserrf(nErr, "codec %zu of %zu refused", u + 1, aCounts[c]);
		}
		if(DasDs_numCodecs(pDs) != aCounts[c])
			return daserrf(nErr, "%zu codecs, expected %zu", DasDs_numCodecs(pDs),
				aCounts[c]);

		/* Every codec still names its own array after the moves */
		for(size_t u = 0; u < aCounts[c]; ++u){
			snprintf(sId, sizeof(sId), "ary_%zu", u);
			const DasCodec* pCd = DasDs_codecAt(pDs, u);
			if((pCd == NULL)||(strcmp(DasAry_id(DasCodec_ary(pCd)), sId) != 0))
				return daserrf(nErr, "codec %zu lost its array at count %zu", u,
					aCounts[c]);
		}

		del_DasDesc((DasDesc*)pDs);
	}

	daslog_info("Test 0b success. Codec storage grows and frees at every boundary.");
	return 0;
}

/* DasDs_codecFor() tells three answers apart: a codec, a header only array
   with none, and no such array.  The last is an error, so this test runs
   with errors returned rather than exiting. */
static int test_codec_for(int nErr)
{
	das_errdisp nWasDisp = daserr_disp();
	daserr_setDisp(DASERRDISP_RET);

	float rFill = -1.0f;
	DasDs* pDs = new_DasDs("codec_for", "codec_grp", 1);
	if(pDs == NULL) return daserrf(nErr, "no dataset");

	const char* aIds[2] = {"in_pkts", "in_hdr"};
	for(int i = 0; i < 2; ++i){
		DasAry* pAry = new_DasAry(
			aIds[i], DASVT_FLOAT, 0, (const uint8_t*)&rFill, DAS_SHAPE1(0),
			DASUNITS_DIMENSIONLESS
		);
		if((pAry == NULL)||(DasDs_addAry(pDs, pAry) != DAS_OKAY))
			return daserrf(nErr, "couldn't add array %s", aIds[i]);
		dec_DasAry(pAry);
	}
	if(DasDs_makeFixedCodec(pDs, "in_pkts", "real", "LEreal", 4, 3, DASCODEC_WRITE) == NULL)
		return daserrf(nErr, "codec refused");

	DasCodec* pCodec = NULL;
	int nItems = -9;
	int nRet = DasDs_codecFor(pDs, "in_pkts", &pCodec, &nItems);
	if((nRet != 1)||(pCodec != DasDs_codecAt(pDs, 0))||(nItems != 3))
		return daserrf(nErr, "codecFor packet array gave %d, items %d", nRet, nItems);

	pCodec = (DasCodec*)&rFill;  /* anything but NULL, it must be overwritten */
	nItems = -9;
	nRet = DasDs_codecFor(pDs, "in_hdr", &pCodec, &nItems);
	if((nRet != 0)||(pCodec != NULL)||(nItems != 0))
		return daserrf(nErr, "codecFor header array gave %d, items %d", nRet, nItems);

	daslog_info("======== The ERROR line below is intentional ========");
	nRet = DasDs_codecFor(pDs, "nope", &pCodec, &nItems);
	if((nRet != -DASERR_DS)||(pCodec != NULL)||(nItems != 0))
		return daserrf(nErr, "codecFor missing array gave %d", nRet);

	del_DasDesc((DasDesc*)pDs);
	daserr_setDisp(nWasDisp);
	daslog_info("Test 0c success. codecFor separates codec, header only and missing.");
	return 0;
}

int main(int argc, char** argv)
{
	daslib_init(argv[0], DASERRDISP_EXIT, DASLOG_INFO, NULL);

	int nTest = 0;
	int nErr  = DASERR_MAX;

	if(test_addary_refs(nErr) != 0) return nErr;
	if(test_codec_counts(nErr) != 0) return nErr;
	if(test_codec_for(nErr) != 0) return nErr;
	DasStream* pSd = NULL;
	ptrdiff_t aShape[DASIDX_MAX] = DASIDX_INIT_UNUSED;
	ptrdiff_t aAddr[DASIDX_MAX]   = DASIDX_INIT_BEGIN;

	/* ------------------------------------------------------------------ */
	/* Test 1: cubic dataset (ex12).  For a cube the merge MUST equal the
	   shape at every index -- that is the cleanest possible cross-check, and
	   it exercises vars that map only one index (frequency, altitude offset)
	   alongside the fully-mapped data var. */
	++nTest; ++nErr;
	DasDs* pDs = load_ds("examples/ex12_sounder_xyz.d3t", 1, &pSd, nErr);
	if(pDs == NULL) return nErr;

	int nRank = DasDs_shape(pDs, aShape);
	const ptrdiff_t aCube[3] = {3, 160, 80};
	if(nRank != 3)
		return daserrf(nErr, "Test %d: ex12 rank %d, expected 3", nTest, nRank);
	for(int i = 0; i < 3; ++i)
		if(aShape[i] != aCube[i])
			return daserrf(nErr, "Test %d: ex12 shape[%d]=%td, expected %td",
				nTest, i, aShape[i], aCube[i]);

	/* The merge must agree with the shape, index by index. */
	for(int i = 0; i < 3; ++i){
		ptrdiff_t nLen = DasDs_lengthIn(pDs, i, aAddr);
		if(nLen != aCube[i])
			return daserrf(nErr,
				"Test %d: DasDs_lengthIn(ex12, %d) = %td, expected %td (shape)",
				nTest, i, nLen, aCube[i]);
	}

	/* And the data dimension alone (fully mapped) must report the same. */
	DasDim* pData = DasDs_dimAt(pDs, 0, DASDIMTYPE_DATA);
	if(pData == NULL)
		return daserrf(nErr, "Test %d: ex12 has no data dimension", nTest);
	for(int i = 0; i < 3; ++i){
		ptrdiff_t nLen = DasDim_lengthIn(pData, i, aAddr);
		if(nLen != aCube[i])
			return daserrf(nErr,
				"Test %d: DasDim_lengthIn(ex12 data, %d) = %td, expected %td",
				nTest, i, nLen, aCube[i]);
	}
	daslog_infof("Test %d success. Cubic merge agrees with shape [3,160,80].", nTest);
	del_DasDesc((DasDesc*)pSd); pSd = NULL;

	/* ------------------------------------------------------------------ */
	/* Test 2: ragged dataset (ex19).  shape [3, *]; the inner length is a real
	   per-row count that DasDs_lengthIn must recover from the data var (and the
	   ragged time-offset array) without the reference var polluting it. */
	++nTest; ++nErr;
	pDs = load_ds("examples/ex19_cassini_ragged_wfrm.d3t", 2, &pSd, nErr);
	if(pDs == NULL) return nErr;

	for(int i = 0; i < DASIDX_MAX; ++i) aShape[i] = DASIDX_UNUSED;
	nRank = DasDs_shape(pDs, aShape);
	if(nRank != 2)
		return daserrf(nErr, "Test %d: ex19 rank %d, expected 2", nTest, nRank);
	if(aShape[0] != 3)
		return daserrf(nErr, "Test %d: ex19 shape[0]=%td, expected 3 records",
			nTest, aShape[0]);
	if(aShape[1] != DASIDX_RAGGED)
		return daserrf(nErr, "Test %d: ex19 shape[1]=%td, expected RAGGED (%d)",
			nTest, aShape[1], DASIDX_RAGGED);

	/* Record count along index 0. */
	aAddr[0] = 0; aAddr[1] = 0;
	if(DasDs_lengthIn(pDs, 0, aAddr) != 3)
		return daserrf(nErr, "Test %d: ex19 record count %td, expected 3",
			nTest, DasDs_lengthIn(pDs, 0, aAddr));

	/* Per-row sample count along the ragged index 1. */
	const ptrdiff_t aRows[3] = {2048, 1536, 2048};
	pData = DasDs_dimAt(pDs, 0, DASDIMTYPE_DATA);
	if(pData == NULL)
		return daserrf(nErr, "Test %d: ex19 has no data dimension", nTest);

	for(int i = 0; i < 3; ++i){
		aAddr[0] = i; aAddr[1] = 0;

		ptrdiff_t nDs = DasDs_lengthIn(pDs, 1, aAddr);
		if(nDs != aRows[i])
			return daserrf(nErr,
				"Test %d: DasDs_lengthIn(ex19, 1) row %d = %td, expected %td",
				nTest, i, nDs, aRows[i]);

		ptrdiff_t nDim = DasDim_lengthIn(pData, 1, aAddr);
		if(nDim != aRows[i])
			return daserrf(nErr,
				"Test %d: DasDim_lengthIn(ex19 data, 1) row %d = %td, expected %td",
				nTest, i, nDim, aRows[i]);
	}
	daslog_infof("Test %d success. Ragged merge: rows %td, %td, %td.",
		nTest, aRows[0], aRows[1], aRows[2]);
	del_DasDesc((DasDesc*)pSd); pSd = NULL;

	daslog_info("All dataset shape/length tests passed.");
	return 0;
}
