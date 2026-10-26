/* Copyright (C) 2026 Chris Piker <chris-piker@uiowa.edu>
 *
 * This file is part of das2C, the Core Das2 C Library.
 *
 * Das2C is free software; you can redistribute it and/or modify it under
 * the terms of the GNU Lesser General Public License version 2.1 as published
 * by the Free Software Foundation.
 *
 * Das2C is distributed in the hope that it will be useful, but WITHOUT ANY
 * WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS
 * FOR A PARTICULAR PURPOSE.  See the GNU Lesser General Public License for
 * more details.
 *
 * You should have received a copy of the GNU Lesser General Public License
 * version 2.1 along with das2C; if not, see <http://www.gnu.org/licenses/>.
 */

/* Source settings parsed straight from the three saved source nodes under
 * test/das_fetch, no DasNode involved.  Run from the repository top.
 *
 * The private parser is driven directly because that is the seam the
 * source node load (step 6) will call; the public accessors are then
 * checked against what the fixtures say.  geomag is the current layout
 * with examples, hsk is the current layout at scale (73 variables), and
 * juno_survey is the 2018 layout with long property names and flag packed
 * options. */

#define _POSIX_C_SOURCE 200112L

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>

#include <das3/core.h>
#include <das3/settable.h>
#include "../das3/sett_priv.h"

#define GEOMAG "test/das_fetch/geomag_flex_0.7.json"
#define HSK    "test/das_fetch/hsk_flex_0.7.json"
#define JUNO   "test/das_fetch/juno_survey_0.6.json"

static int g_nFail = 0;
static int g_nPass = 0;

#define CHECK(cond, ...) do{ \
	if(cond){ ++g_nPass; } \
	else{ ++g_nFail; printf("  FAIL line %d: ", __LINE__); printf(__VA_ARGS__); printf("\n"); } \
}while(0)

static bool streq(const char* a, const char* b)
{
	if((a == NULL)||(b == NULL)) return (a == b);
	return strcmp(a, b) == 0;
}

/* Validation failures log at error level by design; keep the test output
   readable while provoking them */
static DasErrCode quietCheck(const DasSettable* p, const char* sVal)
{
	daslog_setlevel(DASLOG_CRIT);
	DasErrCode n = DasSettable_check(p, sVal);
	daslog_setlevel(DASLOG_WARN);
	return n;
}

static DasJdo* loadJson(const char* sFile)
{
	FILE* pIn = fopen(sFile, "rb");
	if(pIn == NULL){ printf("  Can't open %s, run from the repo top\n", sFile); return NULL; }
	fseek(pIn, 0, SEEK_END);
	long nLen = ftell(pIn);
	fseek(pIn, 0, SEEK_SET);
	char* sBuf = (char*)malloc(nLen + 1);
	size_t uRead = fread(sBuf, 1, nLen, pIn);
	fclose(pIn);
	sBuf[uRead] = '\0';
	DasJdo* pDom = das_json_parse(sBuf, uRead);
	free(sBuf);
	return pDom;
}

/* The settable list for a saved node, through the same two dicts the node
   load will pass */
static DasSettable* parseNode(const DasJdo* pDom, size_t* puCount)
{
	const DasJdo* pParams = DasJdo_get(pDom, "protocol/httpParams");
	if(pParams == NULL) pParams = DasJdo_get(pDom, "protocol/http_params");
	return _DasSettable_parse(DasJdo_get(pDom, "interface"), pParams, puCount);
}

static const DasSettable* byKey(const DasSettable* aList, size_t uCount, const char* sKey)
{
	for(size_t u = 0; u < uCount; ++u)
		if(streq(DasSettable_key(aList + u), sKey)) return aList + u;
	return NULL;
}

/* ************************************************************************* */

static void test_grammar(void)
{
	printf("Key grammar:\n");
	das_src_sec_e sec;
	CHECK(_das_src_secByName("coord", &sec) && (sec == das_src_sec_coord), "coord");
	CHECK(_das_src_secByName("Coordinates", &sec) && (sec == das_src_sec_coord), "Coordinates");
	CHECK(_das_src_secByName("fmt", &sec) && (sec == das_src_sec_format), "fmt");
	CHECK(_das_src_secByName("opts", &sec) && (sec == das_src_sec_option), "opts");
	CHECK(!_das_src_secByName("time", &sec), "time is not a section");
	CHECK(!_das_src_secByName("", &sec), "empty is not a section");

	CHECK(streq(das_src_sec_name(das_src_sec_coord), "coords"), "coords name");
	CHECK(streq(das_src_sec_name(das_src_sec_format), "formats"), "formats name");
	CHECK(streq(das_src_sec_name(das_src_sec_size), ""), "sentinel has no name");

	CHECK(streq(_das_src_propName("Minimum"), "min"), "Minimum -> min");
	CHECK(streq(_das_src_propName("inter"), "inter"), "inter stays");
	CHECK(streq(_das_src_propName("enable"), "enabled"), "enable -> enabled");
	CHECK(streq(_das_src_propName("delim"), "delim"), "unknown passes through");
	CHECK(_das_src_propEq("maximum", "MAX"), "maximum == MAX");
	CHECK(!_das_src_propEq("res", "inter"), "res != inter");

	char aParts[3][DAS_MAX_ID_BUFSZ];
	CHECK(_das_src_splitKey("coords.time.min", aParts) == 3, "three parts");
	CHECK(streq(aParts[1], "time") && streq(aParts[2], "min"), "parts content");
	CHECK(_das_src_splitKey("lshell", aParts) == 1, "one part");
	CHECK(_das_src_splitKey("a.b.c.d", aParts) == 0, "four parts is malformed");
	CHECK(_das_src_splitKey("a..c", aParts) == 0, "empty part is malformed");
	CHECK(_das_src_splitKey("", aParts) == 0, "empty key is malformed");
	CHECK(_das_src_splitKey(NULL, aParts) == 0, "NULL key is malformed");
}

static void test_scalar(void)
{
	printf("JSON scalars:\n");
	/* Two letter keys: DasJdo_get() does not resolve one character names */
	const char* sDoc = "{\"st\":\"x\",\"nm\":86.4,\"in\":42,\"tr\":true,\"fa\":false,"
	                   "\"nl\":null,\"ar\":[1],\"di\":{}}";
	DasJdo* pDom = das_json_parse(sDoc, strlen(sDoc));
	CHECK(pDom != NULL, "parse");
	char sBuf[32];
	CHECK(_das_json_scalar(DasJdo_get(pDom, "st"), sBuf, 32) && streq(sBuf, "x"), "string");
	CHECK(_das_json_scalar(DasJdo_get(pDom, "nm"), sBuf, 32) && streq(sBuf, "86.4"), "number text kept");
	CHECK(_das_json_scalar(DasJdo_get(pDom, "in"), sBuf, 32) && streq(sBuf, "42"), "integer");
	CHECK(_das_json_scalar(DasJdo_get(pDom, "tr"), sBuf, 32) && streq(sBuf, "true"), "true");
	CHECK(_das_json_scalar(DasJdo_get(pDom, "fa"), sBuf, 32) && streq(sBuf, "false"), "false");
	CHECK(!_das_json_scalar(DasJdo_get(pDom, "nl"), sBuf, 32), "null is not a scalar");
	CHECK(!_das_json_scalar(DasJdo_get(pDom, "ar"), sBuf, 32), "array is not a scalar");
	CHECK(!_das_json_scalar(DasJdo_get(pDom, "di"), sBuf, 32), "dict is not a scalar");
	CHECK(!_das_json_scalar(NULL, sBuf, 32), "NULL is not a scalar");
	CHECK(_das_json_scalar(DasJdo_get(pDom, "nm"), sBuf, 3) && streq(sBuf, "86"), "number cut to fit");
	free(pDom);
}

static void test_geomag(void)
{
	printf("geomag, current layout:\n");
	DasJdo* pDom = loadJson(GEOMAG);
	CHECK(pDom != NULL, "load");
	if(pDom == NULL) return;

	size_t uCount = 0;
	DasSettable* aList = parseNode(pDom, &uCount);
	CHECK(aList != NULL, "parse");

	/* time min/max/inter, lshell enabled, then 13 format knobs */
	CHECK(uCount == 17, "17 settables, got %zu", uCount);

	/* Order is section then document order */
	CHECK((uCount > 4) && (DasSettable_section(aList) == das_src_sec_coord), "coords first");
	CHECK((uCount > 4) && (DasSettable_section(aList + 3) == das_src_sec_data), "data second");
	CHECK((uCount > 4) && (DasSettable_section(aList + 4) == das_src_sec_format), "formats third");

	const DasSettable* p = byKey(aList, uCount, "coords.time.min");
	CHECK(p != NULL, "coords.time.min");
	if(p){
		CHECK(streq(DasSettable_group(p), "time"), "group");
		CHECK(streq(DasSettable_prop(p), "min"), "prop");
		CHECK(streq(DasSettable_param(p), "read.time.min"), "param via dotted name");
		CHECK(DasSettable_required(p), "required from the protocol parameter");
		CHECK(DasSettable_type(p) == das_settable_time, "type from the parameter");
		CHECK(streq(DasSettable_typeName(p), "isotime"), "type name from the parameter");
		CHECK(streq(DasSettable_units(p), "UTC"), "UTC for a time");
		CHECK(DasSettable_defVal(p) == NULL, "null value is no default");
		CHECK(quietCheck(p, "2026-01-01") == DAS_OKAY, "ISO date passes");
		CHECK(quietCheck(p, "2026-01-01T12:00:00") == DAS_OKAY, "ISO time passes");
		CHECK(quietCheck(p, "now") == DAS_OKAY, "now passes");
		CHECK(quietCheck(p, "yesterday-ish") == DASERR_SRC, "garbage fails");
	}

	p = byKey(aList, uCount, "coords.time.inter");
	CHECK(p != NULL, "coords.time.inter");
	if(p){
		CHECK(DasSettable_type(p) == das_settable_real, "real from the parameter");
		CHECK(streq(DasSettable_units(p), "s"), "units string on the property");
		CHECK(quietCheck(p, "60") == DAS_OKAY, "60 is real");
		CHECK(quietCheck(p, "6e1") == DAS_OKAY, "6e1 is real");
		CHECK(quietCheck(p, "sixty") == DASERR_SRC, "sixty is not");
	}

	p = byKey(aList, uCount, "data.lshell.enabled");
	CHECK(p != NULL, "data.lshell.enabled");
	if(p){
		CHECK(DasSettable_type(p) == das_settable_bool, "boolean");
		CHECK(streq(DasSettable_param(p), "read.data"), "flag set parameter");
		CHECK(!DasSettable_required(p), "not required");
		CHECK(quietCheck(p, "yes") == DAS_OKAY, "yes");
		CHECK(quietCheck(p, "OFF") == DAS_OKAY, "OFF");
		CHECK(quietCheck(p, "maybe") == DASERR_SRC, "maybe");
	}

	char sBuf[64];
	p = byKey(aList, uCount, "formats.csv.enabled");
	CHECK(p != NULL, "formats.csv.enabled");
	if(p){
		CHECK(streq(DasSettable_param(p), "format.type"), "format selector param");
		const char* sTitle = DasSettable_title(p, sBuf, sizeof(sBuf));
		CHECK(strncmp(sTitle, "Output as ", 10) == 0, "composed format title, got '%s'", sTitle);
	}

	p = byKey(aList, uCount, "formats.csv.delimiter");
	CHECK(p != NULL, "formats.csv.delimiter");
	if(p){
		CHECK(DasSettable_type(p) == das_settable_enum, "delimiter is an enum, got %d", DasSettable_type(p));
		CHECK(DasSettable_enumCount(p) == 4, "four delimiters, got %zu", DasSettable_enumCount(p));
		CHECK(DasSettable_enum(p, 4) == NULL, "past the end");
		CHECK(streq(DasSettable_enum(p, 0), "Comma"), "node spelling kept");
		CHECK(quietCheck(p, DasSettable_enum(p, 0)) == DAS_OKAY, "first choice passes");
		CHECK(quietCheck(p, "tab") == DAS_OKAY, "enum match is case insensitive");
		CHECK(quietCheck(p, "colon") == DASERR_SRC, "colon is not offered");
	}

	p = byKey(aList, uCount, "formats.csv.fracSecs");
	CHECK(p != NULL, "formats.csv.fracSecs");
	if(p){
		const char* sLo = NULL; const char* sHi = NULL;
		CHECK(DasSettable_type(p) == das_settable_int, "integer from the parameter");
		CHECK(streq(DasSettable_param(p), "format.secfrac"), "param");
		CHECK(DasSettable_range(p, &sLo, &sHi), "range from the parameter");
		CHECK(streq(sLo, "0") && streq(sHi, "9"), "0 to 9, got %s to %s", sLo, sHi);
		CHECK(quietCheck(p, "3") == DAS_OKAY, "3");
		CHECK(quietCheck(p, "12") == DAS_OKAY, "12 passes, ranges are not enforced");
		CHECK(quietCheck(p, "3.5") == DASERR_SRC, "3.5 is not an integer");
	}

	_DasSettable_freeList(aList, uCount);
	free(pDom);
}

static void test_hsk(void)
{
	printf("hsk, current layout at scale:\n");
	DasJdo* pDom = loadJson(HSK);
	CHECK(pDom != NULL, "load");
	if(pDom == NULL) return;

	size_t uCount = 0;
	DasSettable* aList = parseNode(pDom, &uCount);
	CHECK(aList != NULL, "parse");
	CHECK(uCount == 90, "3 + 73 + 14 settables, got %zu", uCount);

	size_t uData = 0;
	for(size_t u = 0; u < uCount; ++u)
		if(DasSettable_section(aList + u) == das_src_sec_data) ++uData;
	CHECK(uData == 73, "73 variable toggles, got %zu", uData);

	/* Every entry has a key, a parameter, and a printable title */
	bool bAllKeyed = true, bAllParam = true, bAllTitled = true;
	char sBuf[64];
	for(size_t u = 0; u < uCount; ++u){
		if(DasSettable_key(aList + u)[0] == '\0') bAllKeyed = false;
		if(DasSettable_param(aList + u) == NULL) bAllParam = false;
		if(DasSettable_title(aList + u, sBuf, sizeof(sBuf)) == NULL) bAllTitled = false;
	}
	CHECK(bAllKeyed, "all keyed");
	CHECK(bAllParam, "all have a parameter");
	CHECK(bAllTitled, "all titled");

	const DasSettable* p = byKey(aList, uCount, "coords.time.min");
	CHECK(p != NULL, "coords.time.min");
	if(p){
		const char* sLo = NULL; const char* sHi = NULL;
		CHECK(DasSettable_range(p, &sLo, &sHi), "coordinate validRange reaches min");
		CHECK(sLo && (strncmp(sLo, "2025-", 5) == 0), "range low is the mission start, got %s", sLo);
		CHECK(streq(DasSettable_title(p, sBuf, sizeof(sBuf)), "Minimum time value to stream"),
		      "node title wins over the composed one");
		CHECK(streq(DasSettable_units(p), "UTC"), "UTC from the group units");
	}
	p = byKey(aList, uCount, "coords.time.res");
	CHECK(p != NULL, "coords.time.res");
	if(p){
		CHECK(!DasSettable_range(p, NULL, NULL), "res has no range");
		CHECK(streq(DasSettable_units(p), "s"), "seconds");
		CHECK(!DasSettable_required(p), "optional");
	}
	p = byKey(aList, uCount, "formats.csv.headers");
	CHECK(p != NULL, "formats.csv.headers");
	if(p){
		CHECK(streq(DasSettable_defVal(p), "true"), "default true");
		CHECK(DasSettable_type(p) == das_settable_bool, "boolean");
	}

	_DasSettable_freeList(aList, uCount);
	free(pDom);
}

static void test_juno(void)
{
	printf("juno_survey, 2018 layout:\n");
	DasJdo* pDom = loadJson(JUNO);
	CHECK(pDom != NULL, "load");
	if(pDom == NULL) return;

	size_t uCount = 0;
	DasSettable* aList = parseNode(pDom, &uCount);
	CHECK(aList != NULL, "parse");

	/* time minimum/maximum/resolution and the options block */
	size_t uCoord = 0, uOpt = 0;
	for(size_t u = 0; u < uCount; ++u){
		if(DasSettable_section(aList + u) == das_src_sec_coord) ++uCoord;
		if(DasSettable_section(aList + u) == das_src_sec_option) ++uOpt;
	}
	CHECK(uCoord == 3, "three time settables, got %zu", uCoord);
	CHECK(uOpt >= 8, "flag options found, got %zu", uOpt);
	CHECK(uCount == uCoord + uOpt, "nothing settable in data, got %zu", uCount);

	/* Keys use the node's long spelling, matching folds it */
	const DasSettable* p = byKey(aList, uCount, "coords.time.minimum");
	CHECK(p != NULL, "coords.time.minimum keyed as spelled");
	CHECK(byKey(aList, uCount, "coords.time.min") == NULL, "not rewritten to min");
	if(p){
		CHECK(_das_src_propEq(DasSettable_prop(p), "min"), "propEq folds it");
		CHECK(streq(DasSettable_param(p), "start_time"), "param");
		CHECK(DasSettable_required(p), "required from the set block");
		CHECK(streq(DasSettable_defVal(p), "2018-10-02T11:00"), "default kept");
		const char* sLo = NULL; const char* sHi = NULL;
		CHECK(DasSettable_range(p, &sLo, &sHi) && streq(sHi, "now"), "set range to now");
		CHECK(DasSettable_type(p) == das_settable_time, "type from the old parameter dict");
		CHECK(streq(DasSettable_typeName(p), "isotime"), "type name from the parameter");
		CHECK(quietCheck(p, "2018-10-02T11:00") == DAS_OKAY, "the default passes its own check");
		CHECK(streq(DasSettable_units(p), "UTC"), "UTC from the old units dict");
		char sBuf[64];
		CHECK(streq(DasSettable_title(p, sBuf, sizeof(sBuf)), "Minimum Time value to output"),
		      "composed from the group name, got '%s'", DasSettable_title(p, sBuf, sizeof(sBuf)));
	}
	p = byKey(aList, uCount, "coords.time.resolution");
	CHECK(p != NULL, "coords.time.resolution");
	if(p){
		CHECK(streq(DasSettable_defVal(p), "86.4"), "number default in source notation");
		CHECK(streq(DasSettable_units(p), "s"), "units string on the property");
	}

	/* Options: two part keys, empty group, flag toggles are booleans */
	p = byKey(aList, uCount, "options.lfrl");
	CHECK(p != NULL, "options.lfrl");
	if(p){
		CHECK(streq(DasSettable_group(p), ""), "options have no group");
		CHECK(DasSettable_type(p) == das_settable_bool, "flag toggle is boolean");
		CHECK(streq(DasSettable_param(p), "params"), "packed parameter");
		CHECK(streq(DasSettable_defVal(p), "false"), "default false");
		char sBuf[64];
		CHECK(strlen(DasSettable_title(p, sBuf, sizeof(sBuf))) > 10, "node title");
	}
	p = byKey(aList, uCount, "options.das2_text");
	CHECK(p != NULL, "options.das2_text");
	if(p) CHECK(DasSettable_type(p) == das_settable_bool, "boolean from a boolean default");

	_DasSettable_freeList(aList, uCount);
	free(pDom);
}

static void test_edges(void)
{
	printf("Edge cases:\n");
	size_t uCount = 99;
	DasSettable* aList = _DasSettable_parse(NULL, NULL, &uCount);
	CHECK((aList != NULL) && (uCount == 0), "NULL interface is an empty list");
	_DasSettable_freeList(aList, uCount);

	/* A set block with no param is dropped with a warning, the rest stays */
	const char* sDoc =
		"{\"interface\":{\"options\":{"
		"  \"good\":{\"value\":false,\"set\":{\"param\":\"g\",\"value\":true}},"
		"  \"broken\":{\"value\":false,\"set\":{\"value\":true}},"
		"  \"doc_only\":{\"value\":\"x\"}"
		"}}}";
	DasJdo* pDom = das_json_parse(sDoc, strlen(sDoc));
	CHECK(pDom != NULL, "parse edge doc");
	daslog_setlevel(DASLOG_ERROR);
	aList = _DasSettable_parse(DasJdo_get(pDom, "interface"), NULL, &uCount);
	daslog_setlevel(DASLOG_WARN);
	CHECK(uCount == 1, "one settable survives, got %zu", uCount);
	CHECK((uCount == 1) && streq(DasSettable_key(aList), "options.good"), "the good one");
	CHECK((uCount == 1) && (DasSettable_type(aList) == das_settable_bool), "boolean by default value");
	CHECK((uCount == 1) && streq(DasSettable_title(aList, NULL, 0), ""), "untitled option is empty");
	_DasSettable_freeList(aList, uCount);
	free(pDom);
	_DasSettable_freeList(NULL, 0);
}

int main(int argc, char** argv)
{
	das_init(argv[0], DASERR_DIS_EXIT, 0, DASLOG_WARN, NULL);

	test_grammar();
	test_scalar();
	test_geomag();
	test_hsk();
	test_juno();
	test_edges();

	printf("TestSettable: %d checks passed, %d failed\n", g_nPass, g_nFail);
	return (g_nFail == 0) ? 0 : 1;
}
