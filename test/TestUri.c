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
	printf("INFO: test_wildver: version/wildcard selection ... ");

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

	das_range rTime;
	strcpy(rTime.sCoord, "time");
	das_datum_fromStr(&rTime.dBeg, "2025-001");
	das_datum_fromStr(&rTime.dEnd, "2025-003");

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
	strcpy(rRender.sCoord, "time");
	das_datum_fromStr(&rRender.dBeg, "2025-10-15");
	das_datum_fromStr(&rRender.dEnd, "2025-10-16");  /* unused by render */

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
	strcpy(rQuery.sCoord, "time");
	das_datum_fromStr(&rQuery.dBeg, "2025-288");
	das_datum_fromStr(&rQuery.dEnd, "2025-290");

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
	strcpy(aA[0].sCoord, "sclk.partition");
	das_datum_fromDbl(&aA[0].dBeg, 1.0,     UNIT_DIMENSIONLESS);
	das_datum_fromDbl(&aA[0].dEnd, 2.0,     UNIT_DIMENSIONLESS);  /* exclusive */
	strcpy(aA[1].sCoord, "sclk.mod64k");
	das_datum_fromDbl(&aA[1].dBeg, 12000.0, UNIT_DIMENSIONLESS);
	das_datum_fromDbl(&aA[1].dEnd, 13001.0, UNIT_DIMENSIONLESS);

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
	strcpy(aB[0].sCoord, "sclk.partition");
	das_datum_fromDbl(&aB[0].dBeg, 9.0,     UNIT_DIMENSIONLESS);
	das_datum_fromDbl(&aB[0].dEnd, 11.0,    UNIT_DIMENSIONLESS);  /* exclusive */
	strcpy(aB[1].sCoord, "sclk.mod64k");
	das_datum_fromDbl(&aB[1].dBeg, 63986.0, UNIT_DIMENSIONLESS);  /* start in P9  */
	das_datum_fromDbl(&aB[1].dEnd,  5944.0, UNIT_DIMENSIONLESS);  /* end in P10 (exclusive) */

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

	if(nFail == 0)
		printf("INFO: All TestUri tests passed\n");
	else
		fprintf(stderr, "ERROR: %d TestUri test(s) FAILED\n", nFail);

	return nFail ? 1 : 0;
}
