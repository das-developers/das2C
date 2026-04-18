/** @file TestUri.c  Unit tests for URI template parsing and file iteration */

/* Author: Chris Piker <chris-piker@uiowa.edu>
 *
 * This file contains test and example code and is meant to explain an
 * interface.
 *
 * As United States courts have ruled that interfaces cannot be copyrighted,
 * the code in this individual source file, TestUri.c, is placed into the
 * public domain and may be displayed, incorporated or otherwise re-used without
 * restriction.  It is offered to the public without any warranty including
 * even the implied warranty of merchantability or fitness for a particular
 * purpose.
 */

#define _POSIX_C_SOURCE 200112L

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>

#include <das2/core.h>

#define PERR 63


/* ========================================================================= */
/* Filesystem helpers */

/* Create all directory components of sPath (like mkdir -p).  EEXIST is ok. */
static int makedirs(const char* sPath)
{
	char sBuf[DURI_MAX_PATH];
	strncpy(sBuf, sPath, sizeof(sBuf) - 1);
	sBuf[sizeof(sBuf) - 1] = '\0';
	for(char* p = sBuf + 1; *p; ++p) {
		if(*p == '/') {
			*p = '\0';
			if(mkdir(sBuf, 0755) != 0 && errno != EEXIST) return -1;
			*p = '/';
		}
	}
	if(mkdir(sBuf, 0755) != 0 && errno != EEXIST) return -1;
	return 0;
}

/* Create an empty file.  Parent directory must already exist. */
static int touch(const char* sPath)
{
	FILE* f = fopen(sPath, "wb");
	if(f == NULL) return -1;
	fclose(f);
	return 0;
}

/* Build sBase/sSub, create any missing parent directories, then touch the file.
 * Returns 0 on success, -1 on failure (error printed to stderr). */
static int mkfile(const char* sBase, const char* sSub)
{
	char sPath[DURI_MAX_PATH];
	char sDir[DURI_MAX_PATH];

	snprintf(sPath, sizeof(sPath), "%s/%s", sBase, sSub);

	strncpy(sDir, sPath, sizeof(sDir) - 1);
	sDir[sizeof(sDir) - 1] = '\0';
	char* pSlash = strrchr(sDir, '/');
	if(pSlash) *pSlash = '\0';

	if(makedirs(sDir) != 0) {
		fprintf(stderr, "ERROR: makedirs(%s): %s\n", sDir, strerror(errno));
		return -1;
	}
	if(touch(sPath) != 0) {
		fprintf(stderr, "ERROR: touch(%s): %s\n", sPath, strerror(errno));
		return -1;
	}
	return 0;
}


/* Create every file in a NULL-terminated list of paths relative to sBase. */
static int make_files(const char* sBase, const char** psFiles)
{
	for(const char** pp = psFiles; *pp != NULL; ++pp)
		if(mkfile(sBase, *pp) != 0) return -1;
	return 0;
}


/* ========================================================================= */
/* Test 1 — Wildcard and version selection
 *
 * Verifies that $x (lexicographic-last) and $v (numeric-greatest) selection
 * work correctly when multiple files match a template at the same time step.
 *
 * Setup: a small synthetic directory tree (created under sBase/wildver at
 * test time) that contains multiple versions of the same file.  The iterator
 * should yield only the highest-version file for each time step and skip lower
 * versions entirely.
 *
 * TODO (before implementation begins): assert on the paths returned by
 * DasUriIter_next() once uri.c is implemented.
 */

/* per-day files with version suffixes; v02 should beat v01, v1/v01 collision */
static const char* g_wildver_files[] = {
	"wildver/2025/001/data_2025001_v01.cdf",
	"wildver/2025/001/data_2025001_v02.cdf",   /* should win */
	"wildver/2025/002/data_2025002_v1.cdf",
	"wildver/2025/002/data_2025002_v01.cdf",   /* collision: warn + lex last */
	NULL
};

int test_wildver(const char* sBase)
{
	printf("INFO: test_wildver: version/wildcard selection\n");

	if(make_files(sBase, g_wildver_files) != 0) { printf("FAIL\n"); return 1; }

	char sTplt[DURI_MAX_PATH];
	snprintf(sTplt, sizeof(sTplt),
	         "%s/wildver/$Y/$j/data_$Y$j_$(v;type=int).cdf", sBase);

	DasUriTplt* pTplt = new_DasUriTplt();
	if(pTplt == NULL){ printf("FAIL\n"); return 1; }

	if(DasUriTplt_register(pTplt, das_time_uridef()) != DAS_OKAY){
		del_DasUriTplt(pTplt); printf("FAIL\n"); return 1;
	}

	if(DasUriTplt_pattern(pTplt, sTplt) != DAS_OKAY){
		del_DasUriTplt(pTplt); printf("FAIL\n"); return 1;
	}

	/* Exercise DasUriTplt_toStr: the reconstructed pattern should round-trip
	 * back to a string close to the original (scheme-stripped, file:// absent). */
	char sDesc[DURI_MAX_PATH];
	printf("\n       pattern: %s",
	       DasUriTplt_toStr(pTplt, sDesc, sizeof(sDesc)));

	das_range rTime;
	if(das_range_fromUtc(&rTime, "2025-001", "2025-003") != DAS_OKAY){
		del_DasUriTplt(pTplt); printf("FAIL\n"); return 1;
	}

	DasUriIter iter;
	if(init_DasUriIter(&iter, pTplt, 1, &rTime) != DAS_OKAY){
		del_DasUriTplt(pTplt); printf("FAIL\n"); return 1;
	}

	int nFound = 0;
	const char* sPath;
	while((sPath = DasUriIter_next(&iter)) != NULL){
		printf("\n       %s", sPath);
		++nFound;
	}
	fini_DasUriIter(&iter);
	del_DasUriTplt(pTplt);

	/* TODO: assert nFound == 2, assert specific paths */
	printf("\n       (found %d path(s), full assertions TODO)\n", nFound);
	printf("INFO: test_wildver: PASS (parse only; iteration assertions pending)\n");
	return 0;
}


/* ========================================================================= */
/* Test 2 — Standard time-based lookup
 *
 * The primary use case: a classic archive of daily CDF files organised by
 * year and day-of-year.  The iterator should yield exactly the files whose
 * encoded date falls within the requested range.
 *
 * Also exercises DasUriTplt_render() to verify that a known time value
 * produces the expected path string before any filesystem access is needed.
 */

static const char* g_time_files[] = {
	"time/2025/288/instrument_2025288_v01.cdf",
	"time/2025/289/instrument_2025289_v01.cdf",
	NULL
};

int test_time(const char* sBase)
{
	printf("INFO: test_time: standard time template ... ");

	if(make_files(sBase, g_time_files) != 0){ printf("FAIL\n"); return 1; }

	char sTplt[DURI_MAX_PATH];
	snprintf(sTplt, sizeof(sTplt),
	         "%s/time/$Y/$j/instrument_$Y$j_v$v.cdf", sBase);

	DasUriTplt* pTplt = new_DasUriTplt();
	if(pTplt == NULL){ printf("FAIL\n"); return 1; }

	if(DasUriTplt_register(pTplt, das_time_uridef()) != DAS_OKAY){
		del_DasUriTplt(pTplt); printf("FAIL\n"); return 1;
	}

	if(DasUriTplt_pattern(pTplt, sTplt) != DAS_OKAY){
		del_DasUriTplt(pTplt); printf("FAIL\n"); return 1;
	}

	/* Verify render produces the expected path for a known time.
	 * 2025-10-15 is day-of-year 288. */
	das_range rRender;
	if(das_range_fromUtc(&rRender, "2025-10-15", "2025-10-16") != DAS_OKAY){
		del_DasUriTplt(pTplt); printf("FAIL\n"); return 1;
	}

	char sBuf[DURI_MAX_PATH] = {'\0'};
	char* sRendered = DasUriTplt_render(pTplt, 1, &rRender, sBuf, DURI_MAX_PATH);
	if(sRendered == NULL){
		del_DasUriTplt(pTplt); printf("FAIL\n"); return 1;
	}

	char sExpected[DURI_MAX_PATH];
	snprintf(sExpected, sizeof(sExpected),
	         "%s/time/2025/288/instrument_2025288_v*.cdf", sBase);

	if(strcmp(sBuf, sExpected) != 0){
		printf("FAIL\n  render: got '%s'\n  expected '%s'\n", sBuf, sExpected);
		del_DasUriTplt(pTplt);
		return 1;
	}

	/* TODO: run iterator over days 288-289, assert nFound == 2 */
	das_range rQuery;
	if(das_range_fromUtc(&rQuery, "2025-288", "2025-290") != DAS_OKAY){
		del_DasUriTplt(pTplt); printf("FAIL\n"); return 1;
	}

	DasUriIter iter;
	if(init_DasUriIter(&iter, pTplt, 1, &rQuery) != DAS_OKAY){
		del_DasUriTplt(pTplt); printf("FAIL\n"); return 1;
	}
	fini_DasUriIter(&iter);

	del_DasUriTplt(pTplt);
	printf("PASS (render verified; iteration assertions TODO)\n");
	return 0;
}


/* ========================================================================= */
/* Test 3 — User-defined coordinate: Voyager spacecraft clock (SCLK)
 *
 * Demonstrates that the template parser is not special-cased for time.
 * A completely user-defined coordinate type with its own short tokens and
 * rollover ranges is registered and used to parse a template and filter files.
 *
 * Voyager SCLK format:  P_MMMMM:SS[:LLL]
 *   P       partition   1–2 digits, 0–10  (not always zero-padded)
 *   MMMMM   mod64k      5 digits,  0–65535
 *   SS      mod60       2 digits,  0–59
 *   LLL     line        3 digits,  1–800  (field exists in the SCLK spec
 *                                          but not all archives encode it)
 *
 * Real PDS3 archive layout (VGPW_1001):
 *   <volume>/P<P>/V1P<P>_<seq>/C<MMMMM><SS>.DAT
 *   <seq> is a serial sub-volume sequence number with no physical meaning;
 *   $x in the template matches it (lexicographically last sub-volume wins).
 *
 * Sub-test A: partition 1, mod64k 12000–13000           (expected 3 hits)
 * Sub-test B: cross-partition 9/63986.00 to 10/05943.03 (expected 7 hits)
 *
 * In a real application the user selected UTC time range would be converted
 * to a spacecraft clock using spice, then the clock values would be used to
 * find the files in range.
 */

static const char* g_sclk_files[] = {
	/* out-of-range: path uses "DATA/" prefix that the template doesn't match */
	"VGPW_1001/DATA/P9/V1P9_040/C6356641.DAT",
	"VGPW_1001/DATA/P9/V1P9_040/C6377611.DAT",
	"VGPW_1001/DATA/P9/V1P9_040/C6398541.DAT",

	/* sub-test B — in range: 9/63986.00 to 10/05943.03
	   (2023-10-17T04:32:03.884 to 2024-06-22T22:57:18.879 via vgr_l6_sclk2scet) */
	"VGPW_1001/P9/V1P9_040/C6419511.DAT",    /* 9/64195.11 */
	"VGPW_1001/P9/V1P9_040/C6440441.DAT",    /* 9/64404.41 */
	"VGPW_1001/P9/V1P9_040/C6461411.DAT",    /* 9/64614.11 */
	"VGPW_1001/P10/V1P10_001/C0594146.DAT",  /* 10/05941.46 */
	"VGPW_1001/P10/V1P10_001/C0594147.DAT",  /* 10/05941.47 */
	"VGPW_1001/P10/V1P10_001/C0594301.DAT",  /* 10/05943.01 */
	"VGPW_1001/P10/V1P10_001/C0594302.DAT",  /* 10/05943.02  <- last in range */

	/* sub-test B — out of range: beyond 10/05943.03 */
	"VGPW_1001/P10/V1P10_001/C0594416.DAT",  /* 10/05944.16 */
	"VGPW_1001/P10/V1P10_001/C0594417.DAT",  /* 10/05944.17 */
	"VGPW_1001/P10/V1P10_001/C0601957.DAT",  /* 10/06019.57 */

	/* sub-test A — in range: partition 1, mod64k 12000–13000 */
	"VGPW_1001/P1/V1P1_004/C1200030.DAT",    /* 1/12000.30 */
	"VGPW_1001/P1/V1P1_004/C1250015.DAT",    /* 1/12500.15 */
	"VGPW_1001/P1/V1P1_005/C1300000.DAT",    /* 1/13000.00 */

	/* sub-test A — out of range: mod64k above upper bound */
	"VGPW_1001/P1/V1P1_005/C1999959.DAT",    /* 1/19999.59 */

	/* sub-test A — out of range: wrong partition */
	"VGPW_1001/P2/V1P2_001/C1200030.DAT",    /* 2/12000.30 */
	NULL
};

int test_sclk(const char* sBase)
{
	if(make_files(sBase, g_sclk_files) != 0){
		printf("ERROR: test_sclk: make_files failed\n");
		return 1;
	}

	/* Define the Voyager SCLK coordinate on the stack.  DasUriTplt_register()
	 * will deep-copy everything before this function returns. */
	DasUriField aSclkFlds[] = {
		/* cShort  sLong        nWidth  nMin  nMax  */
		{  'P',   "partition",  0,      0,    10    },  /* variable width: P9, P10 */
		{  'M',   "mod64k",     5,      0,    65535 },
		{  'S',   "mod60",      2,      0,    59    },
	};
	DasUriSegDef sclkDef = {
		"sclk",
		(int)(sizeof(aSclkFlds) / sizeof(DasUriField)),
		aSclkFlds
	};

	char sTplt[DURI_MAX_PATH];
	snprintf(sTplt, sizeof(sTplt),
	         "%s/VGPW_1001/P$P/V1P$P_$x/C$M$S.DAT", sBase);

	DasUriTplt* pTplt = new_DasUriTplt();
	if(pTplt == NULL){ printf("ERROR: test_sclk: new_DasUriTplt\n"); return 1; }

	/* Register only the SCLK def — no time def — to verify the engine does
	 * not assume time is always present. */
	if(DasUriTplt_register(pTplt, &sclkDef) != DAS_OKAY){
		del_DasUriTplt(pTplt);
		printf("ERROR: test_sclk: DasUriTplt_register\n");
		return 1;
	}

	if(DasUriTplt_pattern(pTplt, sTplt) != DAS_OKAY){
		del_DasUriTplt(pTplt);
		printf("ERROR: test_sclk: DasUriTplt_pattern\n");
		return 1;
	}

	/* ------------------------------------------------------------------ */
	/* Sub-test A: partition 1, mod64k 12000–13000, all mod60 values      */

	printf("INFO: test_sclk A: partition 1, mod64k 12000-13000 ... ");

	das_range aA[2];
	das_range_fromInt(&aA[0], "sclk.partition", 1, 2);
	das_range_fromInt(&aA[1], "sclk.mod64k", 12000, 13001);

	DasUriIter iterA;
	if(init_DasUriIter(&iterA, pTplt, 2, aA) != DAS_OKAY){
		del_DasUriTplt(pTplt); printf("FAIL\n"); return 1;
	}
	int nFoundA = 0;
	const char* sPath;
	while((sPath = DasUriIter_next(&iterA)) != NULL){
		printf("\n       %s", sPath);
		++nFoundA;
	}
	fini_DasUriIter(&iterA);

	/* TODO: assert nFoundA == 3 once iteration is implemented */
	printf("\n       (found %d, expected 3 — assertions TODO)\n", nFoundA);
	printf("INFO: test_sclk A: PASS (parse only; iteration assertions pending)\n");

	/* ------------------------------------------------------------------ */
	/* Sub-test B: cross-partition boundary 9/63986.00 to 10/05943.03     *
	 *                                                                     *
	 * dBeg > dEnd on mod64k signals a rollover crossing:                  *
	 *   partition 9  → mod64k in [63986, 65536)                           *
	 *   partition 10 → mod64k in [0, 5944)                                *
	 * Expected 7 hits (3 from P9, 4 from P10).                           */

	printf("INFO: test_sclk B: cross-partition 9/63986.00 to 10/05943.03 ... ");

	das_range aB[2];
	das_range_fromInt(&aB[0], "sclk.partition", 9, 11);
	das_range_fromInt(&aB[1], "sclk.mod64k", 63986, 5944);  /* nBeg > nEnd: rollover P9→P10 */

	DasUriIter iterB;
	if(init_DasUriIter(&iterB, pTplt, 2, aB) != DAS_OKAY){
		del_DasUriTplt(pTplt); printf("FAIL\n"); return 1;
	}
	int nFoundB = 0;
	while((sPath = DasUriIter_next(&iterB)) != NULL){
		printf("\n       %s", sPath);
		++nFoundB;
	}
	fini_DasUriIter(&iterB);

	/* TODO: assert nFoundB == 7 once iteration is implemented */
	printf("\n       (found %d, expected 7 — assertions TODO)\n", nFoundB);
	printf("INFO: test_sclk B: PASS (parse only; iteration assertions pending)\n");

	del_DasUriTplt(pTplt);
	return 0;
}


/* ========================================================================= */
/* Test 4 — Semver version selection (TRACERS-style filenames)
 *
 * Verifies that $v under the default DURI_VER_SEP rule compares versions
 * component-wise as integers, so "1.10.0" beats "1.9.0".  A plain
 * lexicographic compare would pick "1.9.0" and silently drop the newer file,
 * so this test is the guard-rail for that class of regression.
 *
 * Template:  tracers/$Y/$j/trcr_l1_mag_$Y$j_v$v.cdf   (default type=sep)
 *
 * Fixture (day 107 and 108 each have one 1.x and one 0.x contender):
 *   ts2_l2_mag_bac_2026107_v1.9.0.cdf     <-- loses
 *   ts2_l2_mag_bac_2026107_v1.10.0.cdf    <-- wins (1.10 > 1.9)
 *   ts2_l2_mag_bac_2026108_v0.9.1.cdf     <-- loses
 *   ts2_l2_mag_bac_2026108_v0.10.0.cdf    <-- wins (0.10 > 0.9.1)
 *
 * Expected: exactly 2 paths returned, and both winners are the .10.0 files.
 */

static const char* g_semver_files[] = {
	"L2/2026/107/ts2_l2_mag_bac_2026107_v1.9.0.cdf",
	"L2/2026/107/ts2_l2_mag_bac_2026107_v1.10.0.cdf",   /* should win */
	"L2/2026/108/ts2_l2_mag_bac_2026108_v0.9.1.cdf",
	"L2/2026/108/ts2_l2_mag_bac_2026108_v0.10.0.cdf",   /* should win */
	NULL
};

int test_semver(const char* sBase)
{
	printf("INFO: test_semver: TRACERS-style semver version selection ... ");

	if(make_files(sBase, g_semver_files) != 0){ printf("FAIL\n"); return 1; }

	char sTplt[DURI_MAX_PATH];
	snprintf(sTplt, sizeof(sTplt),
	         "%s/L2/$Y/$j/ts2_l2_mag_bac_$Y$j_v$v.cdf", sBase);

	DasUriTplt* pTplt = new_DasUriTplt();
	if(pTplt == NULL){ printf("FAIL (new_DasUriTplt)\n"); return 1; }

	if(DasUriTplt_register(pTplt, das_time_uridef()) != DAS_OKAY){
		del_DasUriTplt(pTplt);
		printf("FAIL (register)\n"); return 1;
	}

	if(DasUriTplt_pattern(pTplt, sTplt) != DAS_OKAY){
		del_DasUriTplt(pTplt);
		printf("FAIL (pattern)\n"); return 1;
	}

	das_range rTime;
	if(das_range_fromUtc(&rTime, "2026-107", "2026-109") != DAS_OKAY){
		del_DasUriTplt(pTplt);
		printf("FAIL (range)\n"); return 1;
	}

	DasUriIter iter;
	if(init_DasUriIter(&iter, pTplt, 1, &rTime) != DAS_OKAY){
		del_DasUriTplt(pTplt);
		printf("FAIL (init_DasUriIter)\n"); return 1;
	}

	/* Copy each returned path — the iterator owns the buffer, and the next
	 * next() call can overwrite it before we get to assert. */
	char aFound[4][DURI_MAX_PATH];
	int nFound = 0;
	const char* sPath;
	while((sPath = DasUriIter_next(&iter)) != NULL){
		if(nFound < 4){
			strncpy(aFound[nFound], sPath, DURI_MAX_PATH - 1);
			aFound[nFound][DURI_MAX_PATH - 1] = '\0';
		}
		++nFound;
	}
	fini_DasUriIter(&iter);
	del_DasUriTplt(pTplt);

	if(nFound != 2){
		printf("FAIL: found %d path(s), expected 2\n", nFound);
		for(int i = 0; i < nFound && i < 4; ++i)
			printf("       %s\n", aFound[i]);
		return 1;
	}

	/* Verify the specific winners — iterator yields in readdir order per
	 * directory but directories are walked in chronological order, so day 107
	 * comes before day 108.  However readdir order within a day is not
	 * guaranteed; accept either 1.10.0 or 0.10.0 in either slot and check
	 * both are present. */
	bool bHave107 = false, bHave108 = false;
	for(int i = 0; i < 2; ++i){
		if(strstr(aFound[i], "2026107_v1.10.0.cdf") != NULL) bHave107 = true;
		if(strstr(aFound[i], "2026108_v0.10.0.cdf") != NULL) bHave108 = true;
		/* Verify the losers were NOT selected */
		if(strstr(aFound[i], "v1.9.0.cdf") != NULL){
			printf("FAIL: iterator picked v1.9.0 over v1.10.0 "
			       "(lex compare instead of semver)\n       %s\n", aFound[i]);
			return 1;
		}
		if(strstr(aFound[i], "v0.9.1.cdf") != NULL){
			printf("FAIL: iterator picked v0.9.1 over v0.10.0 "
			       "(lex compare instead of semver)\n       %s\n", aFound[i]);
			return 1;
		}
	}
	if(!bHave107){
		printf("FAIL: missing day-107 winner v1.10.0.cdf\n");
		for(int i = 0; i < 2; ++i) printf("       %s\n", aFound[i]);
		return 1;
	}
	if(!bHave108){
		printf("FAIL: missing day-108 winner v0.10.0.cdf\n");
		for(int i = 0; i < 2; ++i) printf("       %s\n", aFound[i]);
		return 1;
	}

	printf("PASS\n");
	for(int i = 0; i < 2; ++i) printf("       %s\n", aFound[i]);
	return 0;
}


/* ========================================================================= */
/* Test 5 — das_uri_list() single-block convenience function
 *
 * Verifies that das_uri_list() returns the right count of paths and that
 * the returned block can be released with a single free() call (no leak).
 * Reuses the TRACERS semver fixture created by test_semver().
 */

int test_uri_list(const char* sBase)
{
	printf("INFO: test_uri_list: convenience list builder ... ");

	char sTplt[DURI_MAX_PATH];
	snprintf(sTplt, sizeof(sTplt),
	         "%s/L2/$Y/$j/ts2_l2_mag_bac_$Y$j_v$v.cdf", sBase);

	das_range rTime;
	if(das_range_fromUtc(&rTime, "2026-107", "2026-109") != DAS_OKAY){
		printf("FAIL (range)\n"); return 1;
	}

	size_t nCount = 0;
	char** ppList = das_uri_list(sTplt, das_time_uridef(), 1, &rTime, &nCount);

	if(ppList == NULL || nCount != 2){
		printf("FAIL: got %zu path(s), expected 2\n", nCount);
		free(ppList);
		return 1;
	}

	/* Verify the winners are the .10.0 files. */
	bool bHave107 = false, bHave108 = false;
	for(size_t i = 0; i < nCount; ++i){
		if(strstr(ppList[i], "2026107_v1.10.0.cdf") != NULL) bHave107 = true;
		if(strstr(ppList[i], "2026108_v0.10.0.cdf") != NULL) bHave108 = true;
	}

	free(ppList);  /* single block — one free releases everything */

	if(!bHave107 || !bHave108){
		printf("FAIL: missing expected winner(s)\n");
		return 1;
	}

	printf("PASS (%zu paths, freed with single free())\n", nCount);
	return 0;
}


/* ========================================================================= */
/* Test 6 — Long-form time tokens
 *
 * Verifies that $(time.year), $(time.yday), etc. resolve to the same internal
 * segments as their short-form equivalents $Y, $j.  Uses a separate fixture
 * directory so the result is independent of test_time.
 *
 * Round-trip note: DasUriTplt_toStr() will emit $Y/$j/... rather than
 * $(time.year)/$(time.yday)/... because every time field carries a cShort.
 * That is the expected behaviour — both spellings parse to the same segment.
 */

static const char* g_longform_files[] = {
	"longform/2025/288/inst_2025288_v01.cdf",
	"longform/2025/289/inst_2025289_v01.cdf",
	NULL
};

int test_longform_time(const char* sBase)
{
	printf("INFO: test_longform_time: $(time.year)/$(time.yday) qualified tokens\n");

	if(make_files(sBase, g_longform_files) != 0){ printf("FAIL\n"); return 1; }

	char sTplt[DURI_MAX_PATH];
	snprintf(sTplt, sizeof(sTplt),
	         "%s/longform/$(time.year)/$(time.yday)"
	         "/inst_$(time.year)$(time.yday)_v$v.cdf", sBase);

	DasUriTplt* pTplt = new_DasUriTplt();
	if(pTplt == NULL){ printf("FAIL (new_DasUriTplt)\n"); return 1; }

	if(DasUriTplt_register(pTplt, das_time_uridef()) != DAS_OKAY){
		del_DasUriTplt(pTplt); printf("FAIL (register)\n"); return 1;
	}
	if(DasUriTplt_pattern(pTplt, sTplt) != DAS_OKAY){
		del_DasUriTplt(pTplt); printf("FAIL (pattern)\n"); return 1;
	}

	/* toStr emits short tokens since all time fields have cShort */
	char sDesc[DURI_MAX_PATH];
	printf("       toStr: %s\n", DasUriTplt_toStr(pTplt, sDesc, sizeof(sDesc)));

	das_range rTime;
	if(das_range_fromUtc(&rTime, "2025-288", "2025-290") != DAS_OKAY){
		del_DasUriTplt(pTplt); printf("FAIL (range)\n"); return 1;
	}

	DasUriIter iter;
	if(init_DasUriIter(&iter, pTplt, 1, &rTime) != DAS_OKAY){
		del_DasUriTplt(pTplt); printf("FAIL (init_DasUriIter)\n"); return 1;
	}

	char aFound[4][DURI_MAX_PATH];
	int nFound = 0;
	const char* sPath;
	while((sPath = DasUriIter_next(&iter)) != NULL){
		if(nFound < 4){
			strncpy(aFound[nFound], sPath, DURI_MAX_PATH - 1);
			aFound[nFound][DURI_MAX_PATH - 1] = '\0';
		}
		++nFound;
	}
	fini_DasUriIter(&iter);
	del_DasUriTplt(pTplt);

	if(nFound != 2){
		printf("FAIL: found %d path(s), expected 2\n", nFound); return 1;
	}
	printf("PASS (%d paths)\n", nFound);
	for(int i = 0; i < nFound; ++i) printf("       %s\n", aFound[i]);
	return 0;
}


/* ========================================================================= */
/* Test 7 — User-defined orbit coordinate: qualified form and scalar shorthand
 *
 * A single-field user coordinate ("orbit", field "number") is registered
 * without a cShort, so it can only appear in $() token form.  Two sub-tests
 * verify that both spellings parse to the same internal segment and yield the
 * same set of files:
 *
 *   Sub-test A: $(orbit.number)  — qualified form (primary)
 *   Sub-test B: $(orbit)         — scalar shorthand (valid: coord has one field)
 *
 * toStr() round-trip: because cShort is absent, both templates reconstruct as
 * $(orbit.number), not $(orbit).  That is the expected behaviour — the qualified
 * form is always emitted when no short token is available.
 *
 * Fixture: five Juno-style files, orbits 0050-0054.
 * Query:   orbit.number in [51, 54)  →  expected 3 files.
 *
 * Note: orbit numbers are not time.  This test intentionally registers no time
 * coordinate to confirm the engine makes no assumptions about what a coordinate
 * means.
 */

static const char* g_orbit_files[] = {
	"juno/orb0050/juno_orbit0050_fgm.cdf",   /* out of range (< 51)  */
	"juno/orb0051/juno_orbit0051_fgm.cdf",   /* in range             */
	"juno/orb0052/juno_orbit0052_fgm.cdf",   /* in range             */
	"juno/orb0053/juno_orbit0053_fgm.cdf",   /* in range             */
	"juno/orb0054/juno_orbit0054_fgm.cdf",   /* out of range (>= 54) */
	NULL
};

/* Run one orbit iterator sub-test and return the found count, or -1 on error. */
static int _run_orbit_iter(
	const DasUriTplt* pTplt, const das_range* pRng, const char* sLabel
){
	DasUriIter iter;
	if(init_DasUriIter(&iter, pTplt, 1, pRng) != DAS_OKAY){
		printf("FAIL (%s init_DasUriIter)\n", sLabel); return -1;
	}
	int nFound = 0;
	const char* sPath;
	while((sPath = DasUriIter_next(&iter)) != NULL){
		printf("       %s\n", sPath);
		++nFound;
	}
	fini_DasUriIter(&iter);
	return nFound;
}

int test_orbit(const char* sBase)
{
	if(make_files(sBase, g_orbit_files) != 0){
		printf("ERROR: test_orbit: make_files failed\n"); return 1;
	}

	/* Orbit coordinate: one field, no short token — must use $() form. */
	DasUriField aOrbFlds[] = {
		/* cShort  sLong     nWidth  nMin  nMax  */
		{  '\0',  "number",  4,      0,    99999 },
	};
	DasUriSegDef orbDef = {
		"orbit",
		(int)(sizeof(aOrbFlds) / sizeof(DasUriField)),
		aOrbFlds
	};

	das_range rOrbit;
	das_range_fromInt(&rOrbit, "orbit.number", 51, 54);

	char sDesc[DURI_MAX_PATH];
	char sTplt[DURI_MAX_PATH];

	/* ------------------------------------------------------------------ */
	/* Sub-test A: qualified form $(orbit.number)                          */

	printf("INFO: test_orbit A: $(orbit.number) qualified form\n");

	snprintf(sTplt, sizeof(sTplt),
	         "%s/juno/orb$(orbit.number)/juno_orbit$(orbit.number)_fgm.cdf",
	         sBase);

	DasUriTplt* pTplt = new_DasUriTplt();
	if(pTplt == NULL){ printf("FAIL (new_DasUriTplt)\n"); return 1; }

	if(DasUriTplt_register(pTplt, &orbDef) != DAS_OKAY){
		del_DasUriTplt(pTplt); printf("FAIL (register A)\n"); return 1;
	}
	if(DasUriTplt_pattern(pTplt, sTplt) != DAS_OKAY){
		del_DasUriTplt(pTplt); printf("FAIL (pattern A)\n"); return 1;
	}

	/* toStr emits $(orbit.number) — no cShort to fall back to */
	printf("       toStr: %s\n", DasUriTplt_toStr(pTplt, sDesc, sizeof(sDesc)));

	int nFoundA = _run_orbit_iter(pTplt, &rOrbit, "A");
	del_DasUriTplt(pTplt);
	if(nFoundA < 0) return 1;
	if(nFoundA != 3){
		printf("FAIL: orbit A: found %d, expected 3\n", nFoundA); return 1;
	}
	printf("PASS (%d paths)\n", nFoundA);

	/* ------------------------------------------------------------------ */
	/* Sub-test B: scalar shorthand $(orbit) — valid because orbit has     *
	 * exactly one sub-field.  toStr still emits $(orbit.number).          */

	printf("INFO: test_orbit B: $(orbit) scalar shorthand\n");

	snprintf(sTplt, sizeof(sTplt),
	         "%s/juno/orb$(orbit)/juno_orbit$(orbit)_fgm.cdf", sBase);

	pTplt = new_DasUriTplt();
	if(pTplt == NULL){ printf("FAIL (new_DasUriTplt)\n"); return 1; }

	if(DasUriTplt_register(pTplt, &orbDef) != DAS_OKAY){
		del_DasUriTplt(pTplt); printf("FAIL (register B)\n"); return 1;
	}
	if(DasUriTplt_pattern(pTplt, sTplt) != DAS_OKAY){
		del_DasUriTplt(pTplt); printf("FAIL (pattern B)\n"); return 1;
	}

	printf("       toStr: %s\n", DasUriTplt_toStr(pTplt, sDesc, sizeof(sDesc)));

	int nFoundB = _run_orbit_iter(pTplt, &rOrbit, "B");
	del_DasUriTplt(pTplt);
	if(nFoundB < 0) return 1;
	if(nFoundB != 3){
		printf("FAIL: orbit B: found %d, expected 3\n", nFoundB); return 1;
	}
	printf("PASS (%d paths)\n", nFoundB);

	return 0;
}


/* ========================================================================= */
/* Test 8 — Mixed coordinates: time and orbit in one template
 *
 * Verifies that two DasUriSegDef objects (time + orbit) can be registered on
 * the same template and that both coordinate filters are applied together when
 * scanning directories.
 *
 * Three sub-tests share the same six-file fixture:
 *
 *   Sub-test A: both time and orbit ranges applied       → 2 paths
 *   Sub-test B: time range only, orbit treated as $x    → 3 paths
 *   Sub-test C: orbit range only, date treated as $x    → 3 paths
 *
 * Fixture:
 *
 * Directory               File              day  orbit  Sub-A  Sub-B  Sub-C
 * ----------------------  ----------------  ---  -----  -----  -----  -----
 * 2020001_ORBIT_42/       WAV_2020001_V01   001  42     orb<   in     orb<
 * 2020002_ORBIT_43/       WAV_2020002_V01   002  43     V02↑   V02↑   V02↑
 * 2020002_ORBIT_43/       WAV_2020002_V02   002  43     win    win    win
 * 2020003_ORBIT_44/       WAV_2020003_V01   003  44     in     in     in
 * 2020004_ORBIT_45/       WAV_2020004_V01   004  45     orb≥   day≥   in
 * 2020006_ORBIT_47/       WAV_2020006_V01   006  47     day>   day>   orb>
 *
 * Sub-A time "2020-001"→"2020-005", orbit [43,45): days 001–004 pass time,
 *      orbits 43–44 pass orbit → 2 paths (day 002 V02, day 003)
 * Sub-B time "2020-001"→"2020-004": exclusive upper → days 001–003 → 3 paths
 *      (orbit is $x wildcard, not filtered)
 * Sub-C orbit [43,46): orbits 43–45 → 3 paths (date is $x wildcard)
 *      Template: $x_ORBIT_$O/WAV_$x.CSV — single $x at file level spans
 *      the entire "date_E_Vnn" fragment.  Lex-last comparison picks
 *      "2020002_E_V02" over "2020002_E_V01" for orbit 43.  (A $v at the
 *      same file level as $x is now rejected as a duplicate wild token.)
 */

static const char* g_time_orbit_files[] = {
	"juno/data/2020001_ORBIT_42/WAV_2020001_E_V01.CSV",  /* orbit out of range */
	"juno/data/2020002_ORBIT_43/WAV_2020002_E_V01.CSV",  /* loses to V02       */
	"juno/data/2020002_ORBIT_43/WAV_2020002_E_V02.CSV",  /* version winner     */
	"juno/data/2020003_ORBIT_44/WAV_2020003_E_V01.CSV",  /* only version, wins */
	"juno/data/2020004_ORBIT_45/WAV_2020004_E_V01.CSV",  /* orbit out of range */
	"juno/data/2020006_ORBIT_47/WAV_2020006_E_V01.CSV",  /* time out of range  */
	NULL
};

int test_time_orbit(const char* sBase)
{
	printf("INFO: test_time_orbit: time + orbit coords in one template\n");

	if(make_files(sBase, g_time_orbit_files) != 0){ printf("FAIL\n"); return 1; }

	/* Orbit coordinate: $O short token does not conflict with time's
	 * $Y/$m/$d/$j/$H/$M/$S.  Width 4 matches the four-digit fixture filenames. */
	DasUriField aOrbFlds[] = {
		/* cShort  sLong     nWidth  nMin  nMax */
		{  'O',   "number",  2,      0,    99   },
	};
	DasUriSegDef orbDef = {
		"orbit",
		(int)(sizeof(aOrbFlds) / sizeof(DasUriField)),
		aOrbFlds
	};

	char sTplt[DURI_MAX_PATH];
	snprintf(sTplt, sizeof(sTplt),
	         "%s/juno/data/$Y$j_ORBIT_$O/WAV_$Y$j_E_V$v.CSV", sBase);

	DasUriTplt* pTplt = new_DasUriTplt();
	if(pTplt == NULL){ printf("FAIL (new_DasUriTplt)\n"); return 1; }

	/* Both defs must be registered before pattern() is called. */
	if(DasUriTplt_register(pTplt, das_time_uridef()) != DAS_OKAY){
		del_DasUriTplt(pTplt); printf("FAIL (register time)\n"); return 1;
	}
	if(DasUriTplt_register(pTplt, &orbDef) != DAS_OKAY){
		del_DasUriTplt(pTplt); printf("FAIL (register orbit)\n"); return 1;
	}
	if(DasUriTplt_pattern(pTplt, sTplt) != DAS_OKAY){
		del_DasUriTplt(pTplt); printf("FAIL (pattern)\n"); return 1;
	}

	char sDesc[DURI_MAX_PATH];
	printf("       toStr: %s\n", DasUriTplt_toStr(pTplt, sDesc, sizeof(sDesc)));

	das_range aRanges[2];
	if(das_range_fromUtc(&aRanges[0], "2020-001", "2020-005") != DAS_OKAY){
		del_DasUriTplt(pTplt); printf("FAIL (time range)\n"); return 1;
	}
	das_range_fromInt(&aRanges[1], "orbit.number", 43, 45);

	DasUriIter iter;
	if(init_DasUriIter(&iter, pTplt, 2, aRanges) != DAS_OKAY){
		del_DasUriTplt(pTplt); printf("FAIL (init_DasUriIter)\n"); return 1;
	}

	char aFound[4][DURI_MAX_PATH];
	int nFound = 0;
	const char* sPath;
	while((sPath = DasUriIter_next(&iter)) != NULL){
		printf("       %s\n", sPath);
		if(nFound < 4){
			strncpy(aFound[nFound], sPath, DURI_MAX_PATH - 1);
			aFound[nFound][DURI_MAX_PATH - 1] = '\0';
		}
		++nFound;
	}
	fini_DasUriIter(&iter);
	del_DasUriTplt(pTplt);

	if(nFound != 2){
		printf("FAIL: found %d path(s), expected 2\n", nFound); return 1;
	}

	bool bHave002 = false, bHave003 = false;
	for(int i = 0; i < nFound; ++i){
		if(strstr(aFound[i], "WAV_2020002_E_V02.CSV") != NULL) bHave002 = true;
		if(strstr(aFound[i], "WAV_2020003_E_V01.CSV") != NULL) bHave003 = true;
		if(strstr(aFound[i], "WAV_2020002_E_V01.CSV") != NULL){
			printf("FAIL: picked V01 over V02 for day 002\n       %s\n", aFound[i]);
			return 1;
		}
	}
	if(!bHave002){
		printf("FAIL: missing day-002 winner WAV_2020002_E_V02.CSV\n"); return 1;
	}
	if(!bHave003){
		printf("FAIL: missing day-003 file WAV_2020003_E_V01.CSV\n"); return 1;
	}

	printf("PASS sub-A (%d paths)\n", nFound);

	/* ------------------------------------------------------------------ */
	/* Sub-test B: time range only — orbit treated as $x (wildcard)       *
	 *                                                                     *
	 * Query "what days fall in a time window, regardless of orbit?"       *
	 * Template uses $x for the orbit portion of the directory name so the *
	 * iterator descends into all matching day directories.  $v at the     *
	 * file level still selects the latest version per directory.          *
	 *                                                                     *
	 * Upper bound "2020-004" is exclusive (no sub-day content) →          *
	 * days 001, 002, 003 pass; day 004 is the exclusive endpoint.        */

	snprintf(sTplt, sizeof(sTplt),
	         "%s/juno/data/$Y$j_ORBIT_$x/WAV_$Y$j_E_V$v.CSV", sBase);

	DasUriTplt* pTpltB = new_DasUriTplt();
	if(pTpltB == NULL){ printf("FAIL sub-B (new)\n"); return 1; }
	if(DasUriTplt_register(pTpltB, das_time_uridef()) != DAS_OKAY){
		del_DasUriTplt(pTpltB); printf("FAIL sub-B (register)\n"); return 1;
	}
	if(DasUriTplt_pattern(pTpltB, sTplt) != DAS_OKAY){
		del_DasUriTplt(pTpltB); printf("FAIL sub-B (pattern)\n"); return 1;
	}

	das_range rTimeB;
	if(das_range_fromUtc(&rTimeB, "2020-001", "2020-004") != DAS_OKAY){
		del_DasUriTplt(pTpltB); printf("FAIL sub-B (range)\n"); return 1;
	}

	DasUriIter iterB;
	if(init_DasUriIter(&iterB, pTpltB, 1, &rTimeB) != DAS_OKAY){
		del_DasUriTplt(pTpltB); printf("FAIL sub-B (init)\n"); return 1;
	}
	int nFoundB = 0;
	while((sPath = DasUriIter_next(&iterB)) != NULL){
		printf("       %s\n", sPath);
		++nFoundB;
	}
	fini_DasUriIter(&iterB);
	del_DasUriTplt(pTpltB);

	if(nFoundB != 3){
		printf("FAIL sub-B: found %d, expected 3\n", nFoundB); return 1;
	}
	printf("PASS sub-B (%d paths)\n", nFoundB);

	/* ------------------------------------------------------------------ */
	/* Sub-test C: orbit range only — date treated as $x (wildcard)       *
	 *                                                                     *
	 * Query "give me everything in a set of orbits, regardless of date?"  *
	 * Template uses $x for the date portion; only the orbit coord is      *
	 * registered.  Orbit range [43, 46) → orbits 43, 44, 45.            *
	 *                                                                     *
	 * Template uses a single $x at file level spanning the whole          *
	 * "date_E_Vnn" fragment.  Two $x or a $x+$v at the same level would  *
	 * now be rejected at pattern() time.  Lex-last on the full $x span   *
	 * picks "2020002_E_V02" over "2020002_E_V01" for orbit 43.           */

	snprintf(sTplt, sizeof(sTplt),
	         "%s/juno/data/$x_ORBIT_$O/WAV_$x.CSV", sBase);

	DasUriTplt* pTpltC = new_DasUriTplt();
	if(pTpltC == NULL){ printf("FAIL sub-C (new)\n"); return 1; }
	if(DasUriTplt_register(pTpltC, &orbDef) != DAS_OKAY){
		del_DasUriTplt(pTpltC); printf("FAIL sub-C (register)\n"); return 1;
	}
	if(DasUriTplt_pattern(pTpltC, sTplt) != DAS_OKAY){
		del_DasUriTplt(pTpltC); printf("FAIL sub-C (pattern)\n"); return 1;
	}

	das_range rOrbC;
	das_range_fromInt(&rOrbC, "orbit.number", 43, 46);

	DasUriIter iterC;
	if(init_DasUriIter(&iterC, pTpltC, 1, &rOrbC) != DAS_OKAY){
		del_DasUriTplt(pTpltC); printf("FAIL sub-C (init)\n"); return 1;
	}
	int nFoundC = 0;
	while((sPath = DasUriIter_next(&iterC)) != NULL){
		printf("       %s\n", sPath);
		++nFoundC;
	}
	fini_DasUriIter(&iterC);
	del_DasUriTplt(pTpltC);

	if(nFoundC != 3){
		printf("FAIL sub-C: found %d, expected 3\n", nFoundC); return 1;
	}
	printf("PASS sub-C (%d paths)\n", nFoundC);
	return 0;
}


/* ========================================================================= */
/* Test 9 — Adjacent variable-width coord fields must be rejected
 *
 * Two coord fields both with nWidth == 0 side-by-side in a template are
 * ambiguous: the match routine has no way to know where one field ends and
 * the next begins.  DasUriTplt_pattern() must return a non-zero error code
 * before the iterator is ever constructed.
 *
 * Sub-test A: $A$B (no delimiter)  → pattern() must FAIL
 * Sub-test B: $A_$B (literal '_')  → pattern() must SUCCEED
 * Sub-test C: $A$(amb.beta) mixed  → pattern() must FAIL (same rule)
 *
 * Uses a throwaway "amb" coordinate with two variable-width fields.
 * No filesystem fixture is needed — the check is purely at parse time.
 */

int test_adjacent_varwidth(const char* sBase)
{
	printf("INFO: test_adjacent_varwidth: adjacent variable-width fields\n");

	DasUriField aAmbFlds[] = {
		/* cShort  sLong    nWidth  nMin   nMax  */
		{  'A',   "alpha",  0,      0,     9999  },  /* variable width */
		{  'B',   "beta",   0,      0,     9999  },  /* variable width */
	};
	DasUriSegDef ambDef = {
		"amb",
		(int)(sizeof(aAmbFlds) / sizeof(DasUriField)),
		aAmbFlds
	};

	char sTplt[DURI_MAX_PATH];
	DasUriTplt* pTplt;

	/* Sub-test A: $A$B — must be rejected */
	pTplt = new_DasUriTplt();
	if(pTplt == NULL){ printf("FAIL (new)\n"); return 1; }
	if(DasUriTplt_register(pTplt, &ambDef) != DAS_OKAY){
		del_DasUriTplt(pTplt); printf("FAIL (register A)\n"); return 1;
	}
	snprintf(sTplt, sizeof(sTplt), "%s/ambt/$A$B.dat", sBase);
	if(DasUriTplt_pattern(pTplt, sTplt) == DAS_OKAY){
		del_DasUriTplt(pTplt);
		printf("FAIL sub-A: pattern() accepted adjacent variable-width $A$B\n");
		return 1;
	}
	del_DasUriTplt(pTplt);
	printf("PASS sub-A: $A$B correctly rejected\n");

	/* Sub-test B: $A_$B — literal '_' delimiter, must succeed */
	pTplt = new_DasUriTplt();
	if(pTplt == NULL){ printf("FAIL (new)\n"); return 1; }
	if(DasUriTplt_register(pTplt, &ambDef) != DAS_OKAY){
		del_DasUriTplt(pTplt); printf("FAIL (register B)\n"); return 1;
	}
	snprintf(sTplt, sizeof(sTplt), "%s/ambt/$A_$B.dat", sBase);
	if(DasUriTplt_pattern(pTplt, sTplt) != DAS_OKAY){
		del_DasUriTplt(pTplt);
		printf("FAIL sub-B: pattern() rejected valid $A_$B template\n");
		return 1;
	}
	del_DasUriTplt(pTplt);
	printf("PASS sub-B: $A_$B accepted (literal delimiter present)\n");

	/* Sub-test C: $A$(amb.beta) using mixed short/long form — same rule applies */
	pTplt = new_DasUriTplt();
	if(pTplt == NULL){ printf("FAIL (new)\n"); return 1; }
	if(DasUriTplt_register(pTplt, &ambDef) != DAS_OKAY){
		del_DasUriTplt(pTplt); printf("FAIL (register C)\n"); return 1;
	}
	snprintf(sTplt, sizeof(sTplt), "%s/ambt/$A$(amb.beta).dat", sBase);
	if(DasUriTplt_pattern(pTplt, sTplt) == DAS_OKAY){
		del_DasUriTplt(pTplt);
		printf("FAIL sub-C: pattern() accepted adjacent $A$(amb.beta)\n");
		return 1;
	}
	del_DasUriTplt(pTplt);
	printf("PASS sub-C: $A$(amb.beta) correctly rejected\n");

	printf("INFO: test_adjacent_varwidth: PASS\n");
	return 0;
}


/* ========================================================================= */
/* Test 10 — Duplicate $x / $v within a single path component must be rejected
 *
 * _match_entry uses look-ahead to delimit the wild span and records only the
 * first $x or $v token; a second one at the same level would be silently
 * ignored, producing misleading best-match behaviour.  DasUriTplt_pattern()
 * (via _decompose_levels) must reject such templates before an iterator is
 * ever constructed.
 *
 * Sub-test A: two $x in one filename       → must FAIL
 * Sub-test B: $x and $v in one filename    → must FAIL
 * Sub-test C: $x in dir, $v in filename    → must SUCCEED (one per level)
 */

int test_duplicate_wild(const char* sBase)
{
	printf("INFO: test_duplicate_wild: duplicate $x/$v in one path component\n");

	char sTplt[DURI_MAX_PATH];
	DasUriTplt* pTplt;

	/* Sub-test A: $x_jedi_$Y$j_$x.cdf — two $x at file level → must fail */
	pTplt = new_DasUriTplt();
	if(pTplt == NULL){ printf("FAIL (new A)\n"); return 1; }
	if(DasUriTplt_register(pTplt, das_time_uridef()) != DAS_OKAY){
		del_DasUriTplt(pTplt); printf("FAIL (register A)\n"); return 1;
	}
	snprintf(sTplt, sizeof(sTplt), "%s/dw/$x_jedi_$Y$j_$x.cdf", sBase);
	if(DasUriTplt_pattern(pTplt, sTplt) == DAS_OKAY){
		del_DasUriTplt(pTplt);
		printf("FAIL sub-A: two $x in one filename was accepted\n"); return 1;
	}
	del_DasUriTplt(pTplt);
	printf("PASS sub-A: $x_jedi_$Y$j_$x.cdf correctly rejected\n");

	/* Sub-test B: $x_$v.cdf — $x and $v at same file level → must fail */
	pTplt = new_DasUriTplt();
	if(pTplt == NULL){ printf("FAIL (new B)\n"); return 1; }
	snprintf(sTplt, sizeof(sTplt), "%s/dw/$x_$v.cdf", sBase);
	if(DasUriTplt_pattern(pTplt, sTplt) == DAS_OKAY){
		del_DasUriTplt(pTplt);
		printf("FAIL sub-B: $x and $v in one filename was accepted\n"); return 1;
	}
	del_DasUriTplt(pTplt);
	printf("PASS sub-B: $x_$v.cdf correctly rejected\n");

	/* Sub-test C: $x/$Y$j_$v.cdf — $x in dir level, $v in file level → must succeed */
	pTplt = new_DasUriTplt();
	if(pTplt == NULL){ printf("FAIL (new C)\n"); return 1; }
	if(DasUriTplt_register(pTplt, das_time_uridef()) != DAS_OKAY){
		del_DasUriTplt(pTplt); printf("FAIL (register C)\n"); return 1;
	}
	snprintf(sTplt, sizeof(sTplt), "%s/dw/$x/$Y$j_$v.cdf", sBase);
	if(DasUriTplt_pattern(pTplt, sTplt) != DAS_OKAY){
		del_DasUriTplt(pTplt);
		printf("FAIL sub-C: valid $x dir + $v file was rejected\n"); return 1;
	}
	del_DasUriTplt(pTplt);
	printf("PASS sub-C: $x (dir) + $v (file) accepted — one wild per level\n");

	printf("INFO: test_duplicate_wild: PASS\n");
	return 0;
}


/* ========================================================================= */
/* Test 11 — Upper-bound round-up semantics for vtTime ranges
 *
 * The upper-bound field value is included when dtHi has non-zero precision
 * *below* that field's granularity; otherwise it is the exclusive endpoint.
 *
 * In practical terms: "2025-01-01" means 2025-01-01T00:00:00.000 — no data
 * from day 1 is in the range, so day 1 is excluded.  "2025-01-01T06:00" extends
 * *into* day 1, so day 1 is included.
 *
 * Three boundary levels tested, each with an off/on pair (N vs N+1 files):
 *
 *   A/B  yday  — "2025-063"        vs "2025-063T06:00"    (2 vs 3 files)
 *   C/D  mday  — "2025-01-05"      vs "2025-01-05T09:30"  (2 vs 3 files)
 *   E/F  month — "2025-03-01T00"   vs "2025-03-01T12:00"  (1 vs 2 files)
 *
 * Sub-tests E/F specifically exercise the bSubMon gate: mday=1 is the minimum
 * mday value (months are 1-indexed), so "2025-03-01" lands exactly at the first
 * moment of March and excludes the entire month unless a sub-day field is set.
 *
 * SCLK uses vtInt (das_range_fromInt) where the caller controls nEnd directly;
 * no new code path exists for SCLK — test_sclk() already covers that boundary.
 */

/* Fixtures for each boundary level live in separate subdirectories to avoid
 * collisions with other tests' files. */
static const char* g_ru_yday_files[] = {
	"ru_yday/2025060.dat",   /* before lower bound       */
	"ru_yday/2025061.dat",   /* lower bound: always in   */
	"ru_yday/2025062.dat",   /* middle: always in        */
	"ru_yday/2025063.dat",   /* upper boundary: on/off   */
	"ru_yday/2025064.dat",   /* above upper: always out  */
	NULL
};

static const char* g_ru_mday_files[] = {
	"ru_mday/2025-01-02.dat",  /* before lower bound      */
	"ru_mday/2025-01-03.dat",  /* lower bound: always in  */
	"ru_mday/2025-01-04.dat",  /* middle: always in       */
	"ru_mday/2025-01-05.dat",  /* upper boundary: on/off  */
	"ru_mday/2025-01-06.dat",  /* above upper: always out */
	NULL
};

static const char* g_ru_mon_files[] = {
	"ru_mon/202501.dat",  /* before lower bound      */
	"ru_mon/202502.dat",  /* lower bound: always in  */
	"ru_mon/202503.dat",  /* upper boundary: on/off  */
	"ru_mon/202504.dat",  /* above upper: always out */
	NULL
};

/* Helper: register time, parse pattern, run iterator with one UTC range,
 * assert count.  Returns 0 on pass, 1 on fail. */
static int _time_iter_count(
	const char* sLabel,
	const char* sTplt,
	const char* sBeg, const char* sEnd,
	int nExpect
){
	DasUriTplt* pTplt = new_DasUriTplt();
	if(pTplt == NULL){ printf("FAIL: %s (new)\n", sLabel); return 1; }
	if(DasUriTplt_register(pTplt, das_time_uridef()) != DAS_OKAY){
		del_DasUriTplt(pTplt); printf("FAIL: %s (register)\n", sLabel); return 1;
	}
	if(DasUriTplt_pattern(pTplt, sTplt) != DAS_OKAY){
		del_DasUriTplt(pTplt); printf("FAIL: %s (pattern)\n", sLabel); return 1;
	}
	das_range rng;
	if(das_range_fromUtc(&rng, sBeg, sEnd) != DAS_OKAY){
		del_DasUriTplt(pTplt); printf("FAIL: %s (range)\n", sLabel); return 1;
	}
	DasUriIter iter;
	if(init_DasUriIter(&iter, pTplt, 1, &rng) != DAS_OKAY){
		del_DasUriTplt(pTplt); printf("FAIL: %s (init)\n", sLabel); return 1;
	}
	int nFound = 0;
	const char* sPath;
	while((sPath = DasUriIter_next(&iter)) != NULL) ++nFound;
	fini_DasUriIter(&iter);
	del_DasUriTplt(pTplt);
	if(nFound != nExpect){
		printf("FAIL: %s: found %d, expected %d\n", sLabel, nFound, nExpect);
		return 1;
	}
	printf("PASS: %s: found %d\n", sLabel, nFound);
	return 0;
}

int test_roundup_utc(const char* sBase)
{
	printf("INFO: test_roundup_utc: upper-bound round-up semantics\n");

	char sTplt[DURI_MAX_PATH];
	int nFail = 0;

	/* Group 1: yday boundary — sub-day content determines whether day 063 passes */
	if(make_files(sBase, g_ru_yday_files) != 0){ printf("FAIL\n"); return 1; }
	snprintf(sTplt, sizeof(sTplt), "%s/ru_yday/$Y$j.dat", sBase);
	nFail += _time_iter_count(
		"yday A (exact boundary, excluded)",
		sTplt, "2025-061", "2025-063", 2);
	nFail += _time_iter_count(
		"yday B (hour=6 → boundary included)",
		sTplt, "2025-061", "2025-063T06:00", 3);

	/* Group 2: mday boundary, within a single month so uniform bounds are exact */
	if(make_files(sBase, g_ru_mday_files) != 0){ printf("FAIL\n"); return 1; }
	snprintf(sTplt, sizeof(sTplt), "%s/ru_mday/$Y-$m-$d.dat", sBase);
	nFail += _time_iter_count(
		"mday C (exact boundary, excluded)",
		sTplt, "2025-01-03", "2025-01-05", 2);
	nFail += _time_iter_count(
		"mday D (hour=9 min=30 → boundary included)",
		sTplt, "2025-01-03", "2025-01-05T09:30", 3);

	/* Group 3: month boundary — mday=1 is the minimum; sub-day precision forces
	 * inclusion of the boundary month through the bSubMon gate */
	if(make_files(sBase, g_ru_mon_files) != 0){ printf("FAIL\n"); return 1; }
	snprintf(sTplt, sizeof(sTplt), "%s/ru_mon/$Y$m.dat", sBase);
	nFail += _time_iter_count(
		"month E (mday=1 no sub-day, March excluded)",
		sTplt, "2025-02-01", "2025-03-01", 1);
	nFail += _time_iter_count(
		"month F (mday=1 hour=12 → March included)",
		sTplt, "2025-02-01", "2025-03-01T12:00", 2);

	if(nFail == 0) printf("INFO: test_roundup_utc: PASS\n");
	return nFail;
}


/* ========================================================================= */
/* Test 12 — Multi-year time range: sub-year field context awareness
 *
 * When a vtTime query spans multiple calendar years, sub-year field bounds
 * must be applied relative to the actual year being examined.  Without the
 * fix, yday bounds [364, 2] (from "2025-364" → "2026-003") evaluate against
 * every file uniformly: yday=364 fails 364 <= 2, yday=365 also fails —
 * all of 2025 is silently dropped (false negatives).
 *
 * Fix: in the lo year apply only the lower sub-year bound; in the hi year
 * apply only the upper; in any intermediate year apply none.
 *
 * Fixture (template: $Y/$j.dat):
 *
 *   multiyr/2025/363.dat  — below lo bound (363 < 364)         → out
 *   multiyr/2025/364.dat  — lo year, lower bound               → IN
 *   multiyr/2025/365.dat  — lo year, above lower bound         → IN
 *   multiyr/2026/001.dat  — hi year, below upper bound         → IN
 *   multiyr/2026/002.dat  — hi year, at upper bound (incl.)    → IN
 *   multiyr/2026/003.dat  — hi year, exclusive endpoint        → out
 *   multiyr/2026/004.dat  — hi year, above upper bound         → out
 *
 * Query "2025-364" → "2026-003" → 4 files expected.
 */

static const char* g_multiyr_files[] = {
	"multiyr/2025/363.dat",
	"multiyr/2025/364.dat",
	"multiyr/2025/365.dat",
	"multiyr/2026/001.dat",
	"multiyr/2026/002.dat",
	"multiyr/2026/003.dat",
	"multiyr/2026/004.dat",
	NULL
};

int test_multiyr_time(const char* sBase)
{
	printf("INFO: test_multiyr_time: sub-year field context across year boundary\n");

	if(make_files(sBase, g_multiyr_files) != 0){ printf("FAIL\n"); return 1; }

	char sTplt[DURI_MAX_PATH];
	snprintf(sTplt, sizeof(sTplt), "%s/multiyr/$Y/$j.dat", sBase);

	return _time_iter_count(
		"multi-year yday (2025-364 to 2026-003)",
		sTplt, "2025-364", "2026-003", 4
	);
}


/* ========================================================================= */

int main(int argc, char** argv)
{
	das_init(argv[0], DASERR_DIS_RET, 0, DASLOG_INFO, NULL);

	if(argc != 2){
		fprintf(stderr, "Usage: %s <test-dir>\n", argv[0]);
		return 1;
	}
	const char* sBase = argv[1];

	int nFail = 0;
	nFail += test_wildver(sBase);
	nFail += test_time(sBase);
	nFail += test_sclk(sBase);
	nFail += test_semver(sBase);
	nFail += test_uri_list(sBase);
	nFail += test_longform_time(sBase);
	nFail += test_orbit(sBase);
	nFail += test_time_orbit(sBase);
	nFail += test_adjacent_varwidth(sBase);
	nFail += test_duplicate_wild(sBase);
	nFail += test_roundup_utc(sBase);
	nFail += test_multiyr_time(sBase);

	if(nFail == 0)
		printf("INFO: All TestUri tests passed\n");
	else
		fprintf(stderr, "ERROR: %d TestUri test(s) FAILED\n", nFail);

	return nFail ? 1 : 0;
}
