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

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <das2/core.h>
#include <das2/uri.h>

#define PERR 63


/* ========================================================================= */
/* Test 1 — Wildcard and version selection
 *
 * Verifies that $x (lexicographic-last) and $v (numeric-greatest) selection
 * work correctly when multiple files match a template at the same time step.
 *
 * Setup: a small synthetic directory tree (created under /tmp at test time)
 * that contains multiple versions of the same file.  The iterator should
 * yield only the highest-version file for each time step and skip lower
 * versions entirely.
 *
 * TODO (before implementation begins): create the temp directory tree here
 * and assert on the paths returned by DasUriIter_next().
 */

int test_wildver(void)
{
	printf("INFO: test_wildver: version/wildcard selection ... ");

	/* Template: a data directory with per-day files that have version suffixes.
	 * e.g. /tmp/testuri/2025/001/data_2025001_v01.cdf
	 *      /tmp/testuri/2025/001/data_2025001_v02.cdf   <- should win
	 *      /tmp/testuri/2025/002/data_2025002_v1.cdf
	 *      /tmp/testuri/2025/002/data_2025002_v01.cdf   <- collision: warn + lex last
	 */
	const char* sTplt = "/tmp/testuri/$Y/$j/data_$Y$j_$(v;type=int).cdf";

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

	/* TODO: create /tmp/testuri tree, iterate, assert on returned paths */

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

int test_time(void)
{
	printf("INFO: test_time: standard time template ... ");

	const char* sTplt = "/data/project/$Y/$j/instrument_$Y$j_v$v.cdf";

	DasUriTplt* pTplt = new_DasUriTplt();
	if(pTplt == NULL){ printf("FAIL\n"); return 1; }

	if(DasUriTplt_register(pTplt, das_time_uridef()) != DAS_OKAY){
		del_DasUriTplt(pTplt); printf("FAIL\n"); return 1;
	}

	if(DasUriTplt_pattern(pTplt, sTplt) != DAS_OKAY){
		del_DasUriTplt(pTplt); printf("FAIL\n"); return 1;
	}

	/* Verify render produces the expected path for a known time */
	das_range rRender;
	strcpy(rRender.sCoord, "time");
	das_datum_fromStr(&rRender.dBeg, "2025-10-15");
	das_datum_fromStr(&rRender.dEnd, "2025-10-16");  /* unused by render */

	char sBuf[DURI_MAX_PATH] = {'\0'};
	char* sRendered = DasUriTplt_render(pTplt, 1, &rRender, sBuf, DURI_MAX_PATH);
	if(sRendered == NULL){
		del_DasUriTplt(pTplt); printf("FAIL\n"); return 1;
	}
	/* 2025-10-15 is day-of-year 288 */
	const char* sExpected = "/data/project/2025/288/instrument_2025288_v*.cdf";
	if(strcmp(sBuf, sExpected) != 0){
		printf("FAIL\n  render: got '%s'\n  expected '%s'\n", sBuf, sExpected);
		del_DasUriTplt(pTplt);
		return 1;
	}

	/* TODO: create /tmp/testuri time-tree, run iterator, assert on paths */
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
 * Voyager SCLK format:  P_MMMMM:SS:LLL
 *   P       partition   1 digit,   0–9
 *   MMMMM   mod64k      5 digits,  0–65535
 *   SS      mod60       2 digits,  0–59
 *   LLL     line        3 digits,  1–800
 *
 * Example filename:  vgr2_pw_1_12345_30_400.dat
 * Example template:  /vgr/$P/pw/vgr2_pw_$P_$M_$S_$L.dat
 *
 * Query: all files for partition 1, mod64k 12000–13000.
 */

int test_sclk(void)
{
	printf("INFO: test_sclk: user-defined SCLK coordinate ... ");

	/* Define the Voyager SCLK coordinate on the stack.  DasUriTplt_register()
	 * will deep-copy everything before this function returns. */
	DasUriField aSclkFlds[] = {
		/* cShort  sLong         nWidth  nMin  nMax   */
		{  'P',   "partition",   1,      0,    9      },
		{  'M',   "mod64k",      5,      0,    65535  },
		{  'S',   "mod60",       2,      0,    59     },
		{  'L',   "line",        3,      1,    800    },
	};
	DasUriSegDef sclkDef = {
		"sclk",
		(int)(sizeof(aSclkFlds) / sizeof(DasUriField)),
		aSclkFlds
	};

	const char* sTplt = "/vgr/$P/pw/vgr2_pw_$P_$M_$S_$L.dat";

	DasUriTplt* pTplt = new_DasUriTplt();
	if(pTplt == NULL){ printf("FAIL\n"); return 1; }

	/* Register only the SCLK def — no time def — to verify the engine does
	 * not assume time is always present. */
	if(DasUriTplt_register(pTplt, &sclkDef) != DAS_OKAY){
		del_DasUriTplt(pTplt); printf("FAIL\n"); return 1;
	}

	if(DasUriTplt_pattern(pTplt, sTplt) != DAS_OKAY){
		del_DasUriTplt(pTplt); printf("FAIL\n"); return 1;
	}

	/* Query: partition 1, mod64k 12000–13000, all mod60 and line values */
	das_range aRanges[2];

	strcpy(aRanges[0].sCoord, "sclk.partition");
	das_datum_fromDbl(&aRanges[0].dBeg, 1.0, UNIT_DIMENSIONLESS);
	das_datum_fromDbl(&aRanges[0].dEnd, 2.0, UNIT_DIMENSIONLESS);  /* exclusive */

	strcpy(aRanges[1].sCoord, "sclk.mod64k");
	das_datum_fromDbl(&aRanges[1].dBeg, 12000.0, UNIT_DIMENSIONLESS);
	das_datum_fromDbl(&aRanges[1].dEnd, 13001.0, UNIT_DIMENSIONLESS);

	DasUriIter iter;
	if(init_DasUriIter(&iter, pTplt, 2, aRanges) != DAS_OKAY){
		del_DasUriTplt(pTplt); printf("FAIL\n"); return 1;
	}

	/* TODO: create /tmp/testuri SCLK-tree, iterate, assert on paths */
	int nFound = 0;
	const char* sPath;
	while((sPath = DasUriIter_next(&iter)) != NULL){
		printf("\n       %s", sPath);
		++nFound;
	}
	(void)nFound;
	fini_DasUriIter(&iter);
	del_DasUriTplt(pTplt);

	printf("\n       (stack DasUriSegDef survived register; iteration assertions TODO)\n");
	printf("INFO: test_sclk: PASS (parse only; iteration assertions pending)\n");
	return 0;
}


/* ========================================================================= */

int main(int argc, char** argv)
{
	das_init(argv[0], DASERR_DIS_RET, 0, DASLOG_INFO, NULL);

	int nFail = 0;
	nFail += test_wildver();
	nFail += test_time();
	nFail += test_sclk();

	if(nFail == 0)
		printf("INFO: All TestUri tests passed\n");
	else
		fprintf(stderr, "ERROR: %d TestUri test(s) FAILED\n", nFail);

	return nFail ? 1 : 0;
}
