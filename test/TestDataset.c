/** @file TestDataset.c Unit tests for dataset-level shape and length queries.
 *
 * TestVariable.c proves the per-variable lengthIn() answers in isolation.  This
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
 * This file contains test and example code that intends to explain an
 * interface.
 *
 * As United States courts have ruled that interfaces cannot be copyrighted,
 * the code in this individual source file, TestDataset.c, is placed into the
 * public domain and may be displayed, incorporated or otherwise re-used without
 * restriction.  It is offered to the public without any without any warranty
 * including even the implied warranty of merchantability or fitness for a
 * particular purpose.
 */

#define _POSIX_C_SOURCE 200112L

#include <das2/core.h>

const char* g_sProg = "TestDataset";

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

int main(int argc, char** argv)
{
	das_init(argv[0], DASERR_DIS_EXIT, 0, DASLOG_INFO, NULL);

	int nTest = 0;
	int nErr  = DASERR_MAX;
	DasStream* pSd = NULL;
	ptrdiff_t aShape[DASIDX_MAX] = DASIDX_INIT_UNUSED;
	ptrdiff_t aLoc[DASIDX_MAX]   = DASIDX_INIT_BEGIN;

	/* ------------------------------------------------------------------ */
	/* Test 1: cubic dataset (ex12).  For a cube the merge MUST equal the
	   shape at every index -- that is the cleanest possible cross-check, and
	   it exercises vars that map only one index (frequency, altitude offset)
	   alongside the fully-mapped data var. */
	++nTest; ++nErr;
	DasDs* pDs = load_ds("test/ex12_sounder_xyz.d3t", 1, &pSd, nErr);
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
	pDs = load_ds("test/ex19_cassini_ragged_wfrm.d3t", 2, &pSd, nErr);
	if(pDs == NULL) return nErr;

	for(int i = 0; i < DASIDX_MAX; ++i) aShape[i] = DASIDX_UNUSED;
	nRank = DasDs_shape(pDs, aShape);
	if(nRank != 2)
		return das_error(nErr, "Test %d: ex19 rank %d, expected 2", nTest, nRank);
	if(aShape[0] != 3)
		return das_error(nErr, "Test %d: ex19 shape[0]=%td, expected 3 records",
			nTest, aShape[0]);
	if(aShape[1] != DASIDX_RAGGED)
		return das_error(nErr, "Test %d: ex19 shape[1]=%td, expected RAGGED (%d)",
			nTest, aShape[1], DASIDX_RAGGED);

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
