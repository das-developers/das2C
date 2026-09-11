/** @file TestDs.c Unit tests for the DasDs container (das3/dataset.h).
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

#include <das3/core.h>

const char* g_sProg = "TestDs";

/* Pull dataset packet nPktId out of a freshly read stream.  Caller owns the
   returned stream (del_DasStream) on success. */
static DasDs* load_ds(const char* sFile, int nPktId, DasStream** ppSd, int nErr)
{
	DasStream* pSd = stream_from_path(g_sProg, sFile);
	if(pSd == NULL){ das_error(nErr, "Couldn't read %s", sFile); return NULL; }

	DasDesc* pDesc = DasStream_getDesc(pSd, nPktId);
	if((pDesc == NULL)||(pDesc->type != DATASET)){
		del_DasStream(pSd);
		das_error(nErr, "%s packet %d is not a dataset", sFile, nPktId);
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
		"refs", vtFloat, 0, (const ubyte*)&rFill, RANK_1(0), UNIT_DIMENSIONLESS
	);
	if(pAry == NULL) return das_error(nErr, "no array");
	if(ref_DasAry(pAry) != 1)
		return das_error(nErr, "fresh array has %d references", ref_DasAry(pAry));

	DasDs* pDs = new_DasDs("refs_ds", "refs_grp", 1);
	if(pDs == NULL) return das_error(nErr, "no dataset");

	if(DasDs_addAry(pDs, pAry) != DAS_OKAY)
		return das_error(nErr, "addAry refused");
	if(ref_DasAry(pAry) != 2)
		return das_error(nErr, "after addAry: %d references, expected 2 "
			"(mine + the dataset's)", ref_DasAry(pAry));

	/* drop mine; the dataset's hold keeps it alive */
	dec_DasAry(pAry);
	if(ref_DasAry(pAry) != 1)
		return das_error(nErr, "after my release: %d references, expected 1",
			ref_DasAry(pAry));

	del_DasDs(pDs);   /* releases the last one */

	daslog_info("Test 0 success. DasDs_addAry adds a reference.");
	return 0;
}

int main(int argc, char** argv)
{
	das_init(argv[0], DASERR_DIS_EXIT, 0, DASLOG_INFO, NULL);

	int nTest = 0;
	int nErr  = DASERR_MAX;

	if(test_addary_refs(nErr) != 0) return nErr;
	DasStream* pSd = NULL;
	ptrdiff_t aShape[VARIDX_MAX] = VARIDX_INIT_UNUSED;
	ptrdiff_t aLoc[VARIDX_MAX]   = VARIDX_INIT_BEGIN;

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
		return das_error(nErr, "Test %d: ex12 rank %d, expected 3", nTest, nRank);
	for(int i = 0; i < 3; ++i)
		if(aShape[i] != aCube[i])
			return das_error(nErr, "Test %d: ex12 shape[%d]=%td, expected %td",
				nTest, i, aShape[i], aCube[i]);

	/* The merge must agree with the shape, index by index. */
	for(int i = 0; i < 3; ++i){
		ptrdiff_t nLen = DasDs_lengthIn(pDs, i, aLoc);
		if(nLen != aCube[i])
			return das_error(nErr,
				"Test %d: DasDs_lengthIn(ex12, %d) = %td, expected %td (shape)",
				nTest, i, nLen, aCube[i]);
	}

	/* And the data dimension alone (fully mapped) must report the same. */
	DasDim* pData = DasDs_getDimByIdx(pDs, 0, DASDIM_DATA);
	if(pData == NULL)
		return das_error(nErr, "Test %d: ex12 has no data dimension", nTest);
	for(int i = 0; i < 3; ++i){
		ptrdiff_t nLen = DasDim_lengthIn(pData, i, aLoc);
		if(nLen != aCube[i])
			return das_error(nErr,
				"Test %d: DasDim_lengthIn(ex12 data, %d) = %td, expected %td",
				nTest, i, nLen, aCube[i]);
	}
	daslog_info_v("Test %d success. Cubic merge agrees with shape [3,160,80].", nTest);
	del_DasStream(pSd); pSd = NULL;

	/* ------------------------------------------------------------------ */
	/* Test 2: ragged dataset (ex19).  shape [3, *]; the inner length is a real
	   per-row count that DasDs_lengthIn must recover from the data var (and the
	   ragged time-offset array) without the reference var polluting it. */
	++nTest; ++nErr;
	pDs = load_ds("examples/ex19_cassini_ragged_wfrm.d3t", 2, &pSd, nErr);
	if(pDs == NULL) return nErr;

	for(int i = 0; i < VARIDX_MAX; ++i) aShape[i] = VARIDX_UNUSED;
	nRank = DasDs_shape(pDs, aShape);
	if(nRank != 2)
		return das_error(nErr, "Test %d: ex19 rank %d, expected 2", nTest, nRank);
	if(aShape[0] != 3)
		return das_error(nErr, "Test %d: ex19 shape[0]=%td, expected 3 records",
			nTest, aShape[0]);
	if(aShape[1] != VARIDX_RAGGED)
		return das_error(nErr, "Test %d: ex19 shape[1]=%td, expected RAGGED (%d)",
			nTest, aShape[1], VARIDX_RAGGED);

	/* Record count along index 0. */
	aLoc[0] = 0; aLoc[1] = 0;
	if(DasDs_lengthIn(pDs, 0, aLoc) != 3)
		return das_error(nErr, "Test %d: ex19 record count %td, expected 3",
			nTest, DasDs_lengthIn(pDs, 0, aLoc));

	/* Per-row sample count along the ragged index 1. */
	const ptrdiff_t aRows[3] = {2048, 1536, 2048};
	pData = DasDs_getDimByIdx(pDs, 0, DASDIM_DATA);
	if(pData == NULL)
		return das_error(nErr, "Test %d: ex19 has no data dimension", nTest);

	for(int i = 0; i < 3; ++i){
		aLoc[0] = i; aLoc[1] = 0;

		ptrdiff_t nDs = DasDs_lengthIn(pDs, 1, aLoc);
		if(nDs != aRows[i])
			return das_error(nErr,
				"Test %d: DasDs_lengthIn(ex19, 1) row %d = %td, expected %td",
				nTest, i, nDs, aRows[i]);

		ptrdiff_t nDim = DasDim_lengthIn(pData, 1, aLoc);
		if(nDim != aRows[i])
			return das_error(nErr,
				"Test %d: DasDim_lengthIn(ex19 data, 1) row %d = %td, expected %td",
				nTest, i, nDim, aRows[i]);
	}
	daslog_info_v("Test %d success. Ragged merge: rows %td, %td, %td.",
		nTest, aRows[0], aRows[1], aRows[2]);
	del_DasStream(pSd); pSd = NULL;

	daslog_info("All dataset shape/length tests passed.");
	return 0;
}
