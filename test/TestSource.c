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

/* Sources, requisitions and URLs from the three saved source nodes under
 * test/das_fetch, entirely offline.  Run from the repository top.
 *
 * geomag is the current layout with examples and mirrors, hsk the current
 * layout at scale (73 variables), juno_survey the 2018 layout with long
 * property names, flag packed options and protocol level examples.  The
 * URL cases are the ones utilities/das_fetch.c has always been held to,
 * now asked of the library directly. */

#define _POSIX_C_SOURCE 200112L

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>

#include <das3/core.h>
#include <das3/source.h>
#include <das3/http.h>
#include <das3/catalog.h>
#include "../das3/src_priv.h"

#define GEOMAG "test/das_fetch/geomag_flex_0.7.json"
#define HSK    "test/das_fetch/hsk_flex_0.7.json"
#define JUNO   "test/das_fetch/juno_survey_0.6.json"

#define GB "http://127.0.0.1:8765/data/geomag.d2s"
#define HB "https://space.physics.uiowa.edu/das/stream/source/tracers/tracers-2/l1a/cdpu/hsk/flex"
#define JB "http://127.0.0.1:8765/data/juno_survey.d2s?server=dataset&dataset=Juno/WAV/Survey"

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

static char* readFile(const char* sFile, size_t* puLen)
{
	FILE* pIn = fopen(sFile, "rb");
	if(pIn == NULL){ printf("  Can't open %s, run from the repo top\n", sFile); return NULL; }
	fseek(pIn, 0, SEEK_END);
	long nLen = ftell(pIn);
	fseek(pIn, 0, SEEK_SET);
	char* sBuf = (char*)malloc(nLen + 1);
	*puLen = fread(sBuf, 1, nLen, pIn);
	fclose(pIn);
	sBuf[*puLen] = '\0';
	return sBuf;
}

static DasSrc* loadSrc(const char* sFile, const char* sPath)
{
	size_t uLen = 0;
	char* sDoc = readFile(sFile, &uLen);
	if(sDoc == NULL) return NULL;
	DasSrc* pSrc = new_DasSrc_json(sDoc, uLen, sPath, sFile);
	free(sDoc);
	return pSrc;
}

static const DasSrcProp* byKey(const DasSrc* pSrc, const char* sKey)
{
	for(size_t u = 0; u < DasSrc_propCount(pSrc); ++u)
		if(streq(DasSrcProp_key(DasSrc_prop(pSrc, u)), sKey)) return DasSrc_prop(pSrc, u);
	return NULL;
}

static const DasSrcCtrl* ctrlByKey(const DasSrc* pSrc, const char* sKey)
{
	const DasSrcProp* p = byKey(pSrc, sKey);
	return p ? DasSrcProp_ctrl(p) : NULL;
}

static size_t ctrlCount(const DasSrc* pSrc, int nSec)
{
	size_t n = 0;
	for(size_t u = 0; u < DasSrc_propCount(pSrc); ++u){
		const DasSrcProp* p = DasSrc_prop(pSrc, u);
		if((nSec >= 0) && (DasSrcProp_section(p) != (das_src_sec_e)nSec)) continue;
		if(DasSrcProp_ctrl(p) != NULL) ++n;
	}
	return n;
}

static DasErrCode chk(const DasSrcCtrl* pC, const char* sVal)
{
	return DasSrcCtrl_check(pC, sVal, NULL, 0);
}

/* A KEY=VAL list to a URL, the das_fetch_test.sh shape.  nExample seeds the
   requisition, -1 for none.  Errors are expected in some cases, so the log
   is quiet for the duration. */
static bool urlFor(
	const DasSrc* pSrc, int nExample, const char** aKeyVal, const char** aRaw,
	char* sUrl, size_t uLen
){
	DasReq* pReq = (nExample >= 0) ? DasSrc_newReq(pSrc, nExample) : new_DasReq();
	if(pReq == NULL) return false;
	for(size_t u = 0; aKeyVal && aKeyVal[u]; ++u){
		const char* pEq = strchr(aKeyVal[u], '=');
		char sKey[128];
		if(pEq){
			size_t n = (size_t)(pEq - aKeyVal[u]);
			memcpy(sKey, aKeyVal[u], n); sKey[n] = '\0';
			DasReq_set(pReq, sKey, pEq + 1);
		}
		else{
			DasReq_set(pReq, aKeyVal[u], "true");
		}
	}
	for(size_t u = 0; aRaw && aRaw[u]; u += 2) DasReq_setRaw(pReq, aRaw[u], aRaw[u+1]);
	daslog_setlevel(DASLOG_CRIT);
	size_t n = DasStreamSrc_url(pSrc, pReq, 0, sUrl, uLen);
	daslog_setlevel(DASLOG_WARN);
	del_DasReq(pReq);
	return n > 0;
}

#define URL_IS(src, ex, keys, raw, want) do{ \
	char _s[2048]; const char* _k[] = keys; const char* _r[] = raw; \
	bool _ok = urlFor(src, ex, _k, _r, _s, sizeof(_s)); \
	CHECK(_ok && streq(_s, want), "\n    want %s\n    got  %s", want, _ok ? _s : "(no url)"); \
}while(0)

#define URL_FAILS(src, ex, keys) do{ \
	char _s[2048]; const char* _k[] = keys; \
	CHECK(!urlFor(src, ex, _k, NULL, _s, sizeof(_s)), "expected failure, got %s", _s); \
}while(0)

#define K(...) {__VA_ARGS__, NULL}
#define NONE {NULL}

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
	const char* sDoc = "{\"st\":\"x\",\"nm\":86.4,\"in\":42,\"tr\":true,\"fa\":false,"
	                   "\"nl\":null,\"ar\":[1],\"di\":{},\"x\":\"one\",\"ls\":[[7]]}";
	DasJdo* pDom = das_json_parse(sDoc, strlen(sDoc));
	CHECK(pDom != NULL, "parse");
	char sBuf[32];
	CHECK(_das_json_scalar(DasJdo_get(pDom, "x"), sBuf, 32) && streq(sBuf, "one"), "one character key");
	CHECK(_das_json_scalar(DasJdo_get(pDom, "ls/0/0"), sBuf, 32) && streq(sBuf, "7"), "index then index");
	CHECK(DasJdo_get(pDom, "12/x") == NULL, "index past the end");
	CHECK(_das_json_scalar(DasJdo_get(pDom, "st"), sBuf, 32) && streq(sBuf, "x"), "string");
	CHECK(_das_json_scalar(DasJdo_get(pDom, "nm"), sBuf, 32) && streq(sBuf, "86.4"), "number text kept");
	CHECK(_das_json_scalar(DasJdo_get(pDom, "in"), sBuf, 32) && streq(sBuf, "42"), "integer");
	CHECK(_das_json_scalar(DasJdo_get(pDom, "tr"), sBuf, 32) && streq(sBuf, "true"), "true");
	CHECK(_das_json_scalar(DasJdo_get(pDom, "fa"), sBuf, 32) && streq(sBuf, "false"), "false");
	CHECK(!_das_json_scalar(DasJdo_get(pDom, "nl"), sBuf, 32), "null is not a scalar");
	CHECK(!_das_json_scalar(DasJdo_get(pDom, "ar"), sBuf, 32), "array is not a scalar");
	CHECK(!_das_json_scalar(NULL, sBuf, 32), "NULL is not a scalar");
	CHECK(_das_json_scalar(DasJdo_get(pDom, "nm"), sBuf, 3) && streq(sBuf, "86"), "number cut to fit");
	free(pDom);
}

static void test_helpers(void)
{
	printf("URL encoding and time spans:\n");
	char sBuf[64];
	CHECK(das_url_encode("2026-01-02T14:30", sBuf, sizeof(sBuf)) == 18, "encoded length");
	CHECK(streq(sBuf, "2026-01-02T14%3A30"), "colon encoded, got %s", sBuf);
	CHECK(das_url_encode("a b&c", sBuf, sizeof(sBuf)) == 9, "space and ampersand");
	CHECK(streq(sBuf, "a%20b%26c"), "got %s", sBuf);
	CHECK(das_url_encode("abc", sBuf, 2) == 3, "needed length when short");
	CHECK(streq(sBuf, "a"), "cut short and terminated, got '%s'", sBuf);
	CHECK(das_url_encode("abc", NULL, 0) == 3, "measure only");
	CHECK(das_url_encode("", sBuf, sizeof(sBuf)) == 0 && streq(sBuf, ""), "empty");

	das_time dtB, dtE;
	char sB[40], sE[40];
#define SPAN_IS(str, b, e) do{ \
	bool _ok = dt_parsespan(str, &dtB, &dtE); \
	if(_ok){ dt_isoc(&dtB, sB, 40, 0); dt_isoc(&dtE, sE, 40, 0); } \
	CHECK(_ok && streq(sB, b) && streq(sE, e), "%s -> %s %s, want %s %s", str, \
	      _ok ? sB : "(refused)", _ok ? sE : "", b, e); \
}while(0)
	SPAN_IS("2026", "2026-01-01T00:00:00", "2027-01-01T00:00:00");
	SPAN_IS("2026-02", "2026-02-01T00:00:00", "2026-03-01T00:00:00");
	SPAN_IS("2026-01-02", "2026-01-02T00:00:00", "2026-01-03T00:00:00");
	SPAN_IS("2026-12-31", "2026-12-31T00:00:00", "2027-01-01T00:00:00");
	SPAN_IS("2026-01-02T14", "2026-01-02T14:00:00", "2026-01-02T15:00:00");
	SPAN_IS("2026-01-02T23", "2026-01-02T23:00:00", "2026-01-03T00:00:00");
	SPAN_IS("2026-01-02T14:30", "2026-01-02T14:30:00", "2026-01-02T14:31:00");
	SPAN_IS("2026-01-02T14:30:15Z", "2026-01-02T14:30:15", "2026-01-02T14:30:16");
	SPAN_IS("2026-002", "2026-01-02T00:00:00", "2026-01-03T00:00:00");
	SPAN_IS("2026-002T06", "2026-01-02T06:00:00", "2026-01-02T07:00:00");
	CHECK(!dt_parsespan("2026-01-02T14:30:15.5", NULL, NULL), "fractions have no span");
	CHECK(!dt_parsespan("T14", NULL, NULL), "a time needs a date");
	CHECK(!dt_parsespan("now", NULL, NULL), "now is not a span");
	CHECK(!dt_parsespan("", NULL, NULL), "empty");
	CHECK(!dt_parsespan("2026-01-02,2026-01-03", NULL, NULL), "a range is not a span");
	CHECK(dt_parsespan("2026-01-02", NULL, NULL), "outputs are optional");
}

static void test_geomag(void)
{
	printf("geomag, current layout:\n");
	DasSrc* pSrc = loadSrc(GEOMAG, "tag:das2.org,2012:site:/uiowa/tracers/tracers-2/spice/geomagcoords/flex");
	CHECK(pSrc != NULL, "load");
	if(pSrc == NULL) return;

	CHECK(DasSrc_kind(pSrc) == das_src_kind_stream, "stream source");
	CHECK(streq(DasSrc_version(pSrc), "0.7"), "version");
	CHECK(streq(DasSrc_url(pSrc), GEOMAG), "url is where it came from");
	CHECK(!DasSrc_needsAuth(pSrc), "no auth");
	CHECK(DasStreamSrc_urlCount(pSrc) == 2, "two mirrors");
	CHECK(streq(DasStreamSrc_baseUrl(pSrc, 1), "http://127.0.0.1:8765/data/geomag_alt.d2s"), "second mirror");
	CHECK(DasStreamSrc_baseUrl(pSrc, 2) == NULL, "past the mirrors");

	/* time min/max/inter, lshell enabled, then 13 format knobs */
	CHECK(ctrlCount(pSrc, -1) == 17, "17 controls, got %zu", ctrlCount(pSrc, -1));
	CHECK(DasSrc_propCount(pSrc) > 17, "fixed props are kept too, %zu total", DasSrc_propCount(pSrc));

	/* Groups */
	CHECK(DasSrc_groupCount(pSrc, das_src_sec_coord) == 1, "one coordinate");
	CHECK(DasSrc_groupCount(pSrc, das_src_sec_data) == 1, "one variable");
	CHECK(DasSrc_groupCount(pSrc, das_src_sec_format) == 3, "three formats, got %zu",
	      DasSrc_groupCount(pSrc, das_src_sec_format));
	CHECK(DasSrc_groupCount(pSrc, das_src_sec_option) == 0, "no options");
	CHECK(DasSrc_group(pSrc, das_src_sec_option, 0) == NULL, "no option group");

	const DasSrcGroup* pG = DasSrc_findGroup(pSrc, das_src_sec_coord, "time");
	CHECK(pG != NULL, "time group");
	if(pG){
		CHECK(streq(DasSrcGroup_label(pG), "Time"), "label");
		CHECK(DasSrcGroup_enable(pG) == NULL, "coordinates have no gate");
		CHECK(DasSrcGroup_propCount(pG) == 4, "min max units inter, got %zu", DasSrcGroup_propCount(pG));
		CHECK(DasSrcGroup_ctrlCount(pG) == 3, "three of them settable");
		const DasSrcProp* pU = DasSrcGroup_findProp(pG, "units");
		CHECK(pU && (DasSrcProp_ctrl(pU) == NULL), "units is a fixed property");
		CHECK(pU && streq(DasSrcProp_value(pU), "UTC"), "with the value UTC");
		CHECK(DasSrcGroup_findProp(pG, "minimum") == DasSrcGroup_findProp(pG, "min"), "alias in findProp");
		CHECK(DasSrcGroup_prop(pG, 4) == NULL, "past the end");
	}

	pG = DasSrc_findGroup(pSrc, das_src_sec_format, "csv");
	CHECK(pG != NULL, "csv group");
	if(pG){
		const DasSrcProp* pOn = DasSrcGroup_enable(pG);
		CHECK(pOn && streq(DasSrcProp_key(pOn), "formats.csv.enabled"), "the gate");
		CHECK(streq(DasSrcGroup_xorGroup(pG), "format"), "formats are exclusive");
		CHECK(DasSrcGroup_mimeCount(pG) == 1 && streq(DasSrcGroup_mime(pG, 0), "text/csv"), "mime");
	}
	pG = DasSrc_findGroup(pSrc, das_src_sec_data, "lshell");
	CHECK(pG && DasSrcGroup_enable(pG) && (DasSrcGroup_xorGroup(pG) == NULL), "variable gate, not exclusive");

	/* Properties and controls */
	const DasSrcProp* p = byKey(pSrc, "coords.time.min");
	CHECK(p != NULL, "coords.time.min");
	if(p){
		const DasSrcCtrl* pC = DasSrcProp_ctrl(p);
		CHECK(pC != NULL, "has a control");
		CHECK(DasSrcCtrl_prop(pC) == p, "control points back");
		CHECK(streq(DasSrcGroup_name(DasSrcProp_group(p)), "time"), "group");
		CHECK(streq(DasSrcProp_name(p), "min"), "name");
		CHECK(streq(DasSrcCtrl_param(pC), "read.time.min"), "param via dotted name");
		CHECK(DasSrcCtrl_required(pC), "required from the protocol parameter");
		CHECK(DasSrcProp_type(p) == das_srcprop_time, "type from the parameter");
		CHECK(streq(DasSrcProp_typeName(p), "isotime"), "type name from the parameter");
		CHECK(streq(DasSrcProp_units(p), "UTC"), "UTC for a time");
		CHECK(DasSrcProp_value(p) == NULL, "null value is no default");
		CHECK(chk(pC, "2026-01-01") == DAS_OKAY, "ISO date passes");
		CHECK(chk(pC, "2026-01-01T12:00:00") == DAS_OKAY, "ISO time passes");
		CHECK(chk(pC, "now") == DAS_OKAY, "now passes");
		char sWhy[256];
		CHECK(DasSrcCtrl_check(pC, "yesterday-ish", sWhy, sizeof(sWhy)) == DASERR_SRC, "garbage fails");
		CHECK(strstr(sWhy, "coords.time.min") != NULL, "reason names the key: %s", sWhy);
	}

	const DasSrcCtrl* pC = ctrlByKey(pSrc, "coords.time.inter");
	CHECK(pC != NULL, "coords.time.inter");
	if(pC){
		CHECK(DasSrcProp_type(DasSrcCtrl_prop(pC)) == das_srcprop_real, "real from the parameter");
		CHECK(streq(DasSrcProp_units(DasSrcCtrl_prop(pC)), "s"), "units string on the property");
		CHECK(chk(pC, "60") == DAS_OKAY, "60 is real");
		CHECK(chk(pC, "6e1") == DAS_OKAY, "6e1 is real");
		CHECK(chk(pC, "sixty") == DASERR_SRC, "sixty is not");
	}

	pC = ctrlByKey(pSrc, "data.lshell.enabled");
	CHECK(pC != NULL, "data.lshell.enabled");
	if(pC){
		CHECK(DasSrcProp_type(DasSrcCtrl_prop(pC)) == das_srcprop_bool, "boolean");
		CHECK(streq(DasSrcCtrl_param(pC), "read.data"), "flag set parameter");
		CHECK(!DasSrcCtrl_required(pC), "not required");
		CHECK(chk(pC, "yes") == DAS_OKAY, "yes");
		CHECK(chk(pC, "OFF") == DAS_OKAY, "OFF");
		CHECK(chk(pC, "maybe") == DASERR_SRC, "maybe");
	}

	char sBuf[64];
	p = byKey(pSrc, "formats.csv.enabled");
	CHECK(p != NULL, "formats.csv.enabled");
	if(p){
		CHECK(streq(DasSrcCtrl_param(DasSrcProp_ctrl(p)), "format.type"), "format selector param");
		const char* sTitle = DasSrcProp_title(p, sBuf, sizeof(sBuf));
		CHECK(strncmp(sTitle, "Output as ", 10) == 0, "composed format title, got '%s'", sTitle);
	}

	pC = ctrlByKey(pSrc, "formats.csv.delimiter");
	CHECK(pC != NULL, "formats.csv.delimiter");
	if(pC){
		CHECK(DasSrcProp_type(DasSrcCtrl_prop(pC)) == das_srcprop_enum, "delimiter is an enum");
		CHECK(DasSrcCtrl_enumCount(pC) == 4, "four delimiters, got %zu", DasSrcCtrl_enumCount(pC));
		CHECK(DasSrcCtrl_enum(pC, 4) == NULL, "past the end");
		CHECK(streq(DasSrcCtrl_enum(pC, 0), "Comma"), "node spelling kept");
		CHECK(chk(pC, "tab") == DAS_OKAY, "enum match is case insensitive");
		CHECK(chk(pC, "colon") == DASERR_SRC, "colon is not offered");
	}

	pC = ctrlByKey(pSrc, "formats.csv.fracSecs");
	CHECK(pC != NULL, "formats.csv.fracSecs");
	if(pC){
		const char* sLo = NULL; const char* sHi = NULL;
		CHECK(DasSrcProp_type(DasSrcCtrl_prop(pC)) == das_srcprop_int, "integer from the parameter");
		CHECK(DasSrcCtrl_range(pC, &sLo, &sHi), "range from the parameter");
		CHECK(streq(sLo, "0") && streq(sHi, "9"), "0 to 9, got %s to %s", sLo, sHi);
		CHECK(chk(pC, "12") == DAS_OKAY, "12 passes, ranges are not enforced");
		CHECK(chk(pC, "3.5") == DASERR_SRC, "3.5 is not an integer");
	}

	/* Lookup by typed key */
	char sWhy[256];
	CHECK(DasSrc_findProp(pSrc, "time.min", sWhy, sizeof(sWhy)) == byKey(pSrc, "coords.time.min"), "time.min");
	CHECK(DasSrc_findProp(pSrc, "coord.time.minimum", NULL, 0) == byKey(pSrc, "coords.time.min"), "long form");
	CHECK(DasSrc_findProp(pSrc, "lshell", NULL, 0) == byKey(pSrc, "data.lshell.enabled"), "group toggle");
	CHECK(DasSrc_findProp(pSrc, "csv", NULL, 0) == byKey(pSrc, "formats.csv.enabled"), "format toggle");
	CHECK(DasSrc_findProp(pSrc, "serial", NULL, 0) != NULL, "unique bare prop");
	CHECK(DasSrc_findProp(pSrc, "fracSecs", sWhy, sizeof(sWhy)) == NULL, "ambiguous bare prop");
	CHECK(strstr(sWhy, "formats.das.fracSecs") && strstr(sWhy, "formats.csv.fracSecs"), "names both: %s", sWhy);
	CHECK(DasSrc_findProp(pSrc, "nope", sWhy, sizeof(sWhy)) == NULL && sWhy[0], "unknown key has a reason");
	CHECK(DasSrc_findProp(pSrc, "time.units", NULL, 0) != NULL, "fixed props are found too");

	/* Examples and requisitions */
	CHECK(DasSrc_exampleCount(pSrc) == 1, "one example");
	CHECK(streq(DasSrc_exampleLabel(pSrc, 0), "First day of 2026"), "label");
	CHECK(DasSrc_exampleLabel(pSrc, 1) == NULL, "past the examples");
	daslog_setlevel(DASLOG_CRIT);
	CHECK(DasSrc_newReq(pSrc, 9) == NULL, "no example 9");
	daslog_setlevel(DASLOG_WARN);

	DasReq* pReq = DasSrc_newReq(pSrc, 0);
	CHECK(pReq != NULL, "example req");
	if(pReq){
		CHECK(streq(DasReq_label(pReq), "First day of 2026"), "req carries the label");
		CHECK(streq(DasReq_srcPath(pReq), DasSrc_path(pSrc)), "stamped with the path");
		CHECK(streq(DasReq_value(pReq, "coords.time.min"), "2026-01-01"), "example setting in key form");
		CHECK(DasReq_count(pReq) == 3, "three settings, got %zu", DasReq_count(pReq));
		size_t uBad = 9;
		CHECK(DasSrc_check(pSrc, pReq, &uBad) == DAS_OKAY && uBad == 0, "example checks clean");
		CHECK(DasReq_verdict(pReq, 0) == das_req_ok, "verdict ok");
		del_DasReq(pReq);
	}

	pReq = DasSrc_newReq(pSrc, DASSRC_BLANK_REQ);
	CHECK(pReq != NULL, "blank req");
	if(pReq){
		CHECK(DasReq_count(pReq) == 3, "lists the three required keys, got %zu", DasReq_count(pReq));
		CHECK(streq(DasReq_value(pReq, "coords.time.inter"), ""), "with empty values");
		size_t uBad = 0;
		CHECK(DasSrc_check(pSrc, pReq, &uBad) == DASERR_SRC && uBad == 3, "all three bad, got %zu", uBad);
		CHECK(strstr(DasReq_why(pReq, 0), "required") != NULL, "says required: %s", DasReq_why(pReq, 0));
		CHECK(DasReq_set(pReq, "time", "2026-04-01,2026-04-01T01:30") == DAS_OKAY, "range");
		CHECK(DasSrc_check(pSrc, pReq, &uBad) == DASERR_SRC && uBad == 1, "inter still bad, got %zu", uBad);
		size_t uDerived = 0;
		for(size_t u = 0; u < DasReq_count(pReq); ++u) if(DasReq_derived(pReq, u)) ++uDerived;
		CHECK(uDerived == 2, "min and max derived, got %zu", uDerived);
		DasReq_set(pReq, "inter", "60");
		CHECK(DasSrc_check(pSrc, pReq, &uBad) == DAS_OKAY, "now clean");
		char sUrl[2048];
		CHECK(DasStreamSrc_url(pSrc, pReq, 0, sUrl, sizeof(sUrl)) > 0, "builds");
		CHECK(streq(sUrl, GB "?read.time.min=2026-04-01&read.time.max=2026-04-01T01%3A30&read.time.inter=60"),
		      "got %s", sUrl);
		size_t uNeed = DasStreamSrc_url(pSrc, pReq, 0, sUrl, 20);
		CHECK(uNeed > 20 && strlen(sUrl) == 19, "snprintf style on a short buffer, need %zu", uNeed);
		CHECK(DasStreamSrc_url(pSrc, pReq, 1, sUrl, sizeof(sUrl)) > 0 &&
		      strncmp(sUrl, "http://127.0.0.1:8765/data/geomag_alt.d2s?", 42) == 0, "second mirror used");
		daslog_setlevel(DASLOG_CRIT);
		CHECK(DasStreamSrc_url(pSrc, pReq, 2, sUrl, sizeof(sUrl)) == 0, "no third mirror");
		daslog_setlevel(DASLOG_WARN);
		del_DasReq(pReq);
	}

	/* The URL cases das_fetch has always been held to */
	URL_IS(pSrc, -1, K("time=2026-04-01,2026-04-01T01:30,60", "lshell"), NONE,
		GB "?read.time.min=2026-04-01&read.time.max=2026-04-01T01%3A30&read.time.inter=60&read.data=lshell");
	URL_IS(pSrc, -1, K("coord.time.minimum=2026-02-01", "time.maximum=2026-02-02", "inter=60"), NONE,
		GB "?read.time.min=2026-02-01&read.time.max=2026-02-02&read.time.inter=60");
	URL_IS(pSrc, 0, NONE, NONE,
		GB "?read.time.min=2026-01-01&read.time.max=2026-01-02&read.time.inter=60");
	URL_IS(pSrc, 0, K("time.inter=30"), NONE,
		GB "?read.time.min=2026-01-01&read.time.max=2026-01-02&read.time.inter=30");
	URL_IS(pSrc, 0, K("lshell=no"), NONE,
		GB "?read.time.min=2026-01-01&read.time.max=2026-01-02&read.time.inter=60");
	URL_IS(pSrc, 0, K("csv", "delimiter=Tab", "csv.headers=false", "serial=text"), NONE,
		GB "?read.time.min=2026-01-01&read.time.max=2026-01-02&read.time.inter=60&format.serial=text&format.type=csv&format.headers=1&format.delim=tab");
	URL_IS(pSrc, 0, K("data.lshell", "coord.time.inter=30"), NONE,
		GB "?read.time.min=2026-01-01&read.time.max=2026-01-02&read.time.inter=30&read.data=lshell");
	const char* aRaw[] = {"read.time.inter", "5", "extra", "1", NULL};
	{
		char sUrl[2048]; const char* aK[] = {NULL};
		CHECK(urlFor(pSrc, 0, aK, aRaw, sUrl, sizeof(sUrl)) &&
		      streq(sUrl, GB "?read.time.min=2026-01-01&read.time.max=2026-01-02&read.time.inter=5&extra=1"),
		      "raw parameters win and add, got %s", sUrl);
	}
	/* the span form */
	URL_IS(pSrc, -1, K("time=2026-01-02T14", "inter=60"), NONE,
		GB "?read.time.min=2026-01-02T14%3A00%3A00&read.time.max=2026-01-02T15%3A00%3A00&read.time.inter=60");

	URL_FAILS(pSrc, -1, K("time=2026-04-01,2026-04-01T01:30"));   /* required inter missing */
	URL_FAILS(pSrc, 0, K("foo=1"));                                /* no such key */
	URL_FAILS(pSrc, 0, K("fracSecs=4"));                           /* ambiguous */
	URL_FAILS(pSrc, 0, K("formats.xml"));                          /* no such format */
	URL_FAILS(pSrc, 0, K("lshell=maybe"));                         /* not a boolean */
	URL_FAILS(pSrc, 0, K("delimiter=Colon"));                      /* not in enum */
	URL_FAILS(pSrc, 0, K("version=das3"));                         /* geomag offers only das2 */
	URL_FAILS(pSrc, 0, K("data.nope"));                            /* no such variable */
	URL_FAILS(pSrc, 0, K("time.units=TAI"));                       /* fixed property */
	URL_FAILS(pSrc, 0, K("time=2026-01-02,2026-01-03,4,5"));       /* too many range items */

	/* mime follows the format choice */
	pReq = DasSrc_newReq(pSrc, 0);
	CHECK(streq(DasSrc_mime(pSrc, NULL), "application/vnd.das.stream") ||
	      strstr(DasSrc_mime(pSrc, NULL), "das") != NULL, "default is a das stream: %s", DasSrc_mime(pSrc, NULL));
	DasReq_set(pReq, "csv", "true");
	CHECK(streq(DasSrc_mime(pSrc, pReq), "text/csv"), "csv chosen: %s", DasSrc_mime(pSrc, pReq));
	del_DasReq(pReq);

	/* A requisition for some other source is refused */
	pReq = new_DasReq();
	DasReq_setSrc(pReq, "tag:das2.org,2012:site:/elsewhere", "", "");
	DasReq_set(pReq, "time", "2026-01-02");
	daslog_setlevel(DASLOG_CRIT);
	CHECK(DasSrc_check(pSrc, pReq, NULL) == DASERR_SRC, "stamp mismatch refused");
	daslog_setlevel(DASLOG_WARN);
	CHECK(DasReq_verdict(pReq, 0) == das_req_unchecked, "and nothing graded");

	/* The same URI from another mirror, or a local copy, is the same source */
	DasReq_set(pReq, "inter", "60");
	DasReq_setSrc(pReq, DasSrc_path(pSrc), "file:///somewhere/else/geomag.json", "");
	CHECK(DasSrc_check(pSrc, pReq, NULL) == DAS_OKAY, "the URL is not identity");
	DasReq_setSrc(pReq, "", "file:///somewhere/else/geomag.json", "");
	CHECK(DasSrc_check(pSrc, pReq, NULL) == DAS_OKAY, "URL only is unstamped");
	del_DasReq(pReq);

	/* A source built with a shorthand path stamps the full URI */
	size_t uLen = 0;
	char* sDoc = readFile(GEOMAG, &uLen);
	DasSrc* pShort = sDoc ? new_DasSrc_json(sDoc, uLen, "site:uiowa/tracers/tracers-2/spice/geomagcoords/flex", NULL) : NULL;
	free(sDoc);
	CHECK(pShort && streq(DasSrc_path(pShort), DasSrc_path(pSrc)), "shorthand expanded, slash supplied: %s",
	      pShort ? DasSrc_path(pShort) : "(null)");
	if(pShort){
		pReq = DasSrc_newReq(pShort, 0);
		CHECK(pReq && (DasSrc_check(pSrc, pReq, NULL) == DAS_OKAY), "its requisitions grade here");
		del_DasReq(pReq);
		del_DasSrc(pShort);
	}

	del_DasSrc(pSrc);
}

static void test_hsk(void)
{
	printf("hsk, current layout at scale:\n");
	DasSrc* pSrc = loadSrc(HSK, NULL);
	CHECK(pSrc != NULL, "load");
	if(pSrc == NULL) return;

	CHECK(ctrlCount(pSrc, -1) == 90, "3 + 73 + 14 controls, got %zu", ctrlCount(pSrc, -1));
	CHECK(ctrlCount(pSrc, das_src_sec_data) == 73, "73 variable toggles");
	CHECK(DasSrc_groupCount(pSrc, das_src_sec_data) == 73, "73 variable groups");
	CHECK(DasSrc_needsAuth(pSrc), "restricted");
	CHECK(DasSrc_contactCount(pSrc) >= 1, "has a contact");
	const char* sEmail = NULL;
	const char* sName = DasSrc_contact(pSrc, 0, &sEmail);
	CHECK(sName && sName[0] && sEmail, "contact name %s", sName ? sName : "(none)");
	CHECK(DasSrc_contact(pSrc, 99, NULL) == NULL, "past the contacts");
	CHECK(streq(DasSrc_path(pSrc), ""), "no path given, so no URI: %s", DasSrc_path(pSrc));

	/* Every property has a key, every control a parameter, all titled */
	bool bAllKeyed = true, bAllParam = true, bAllTitled = true;
	char sBuf[64];
	for(size_t u = 0; u < DasSrc_propCount(pSrc); ++u){
		const DasSrcProp* p = DasSrc_prop(pSrc, u);
		if(DasSrcProp_key(p)[0] == '\0') bAllKeyed = false;
		if(DasSrcProp_ctrl(p) && (DasSrcCtrl_param(DasSrcProp_ctrl(p)) == NULL)) bAllParam = false;
		if(DasSrcProp_title(p, sBuf, sizeof(sBuf)) == NULL) bAllTitled = false;
	}
	CHECK(bAllKeyed && bAllParam && bAllTitled, "all keyed, parametered, titled");

	/* groups are index ranges into one array, so a group's props are adjacent */
	const DasSrcGroup* pG = DasSrc_group(pSrc, das_src_sec_data, 5);
	CHECK(pG && (DasSrcGroup_prop(pG, 0) == DasSrcProp_group(DasSrcGroup_prop(pG, 0))->pSrc->aProp + pG->uFirst),
	      "group indexing");

	const DasSrcCtrl* pC = ctrlByKey(pSrc, "coords.time.min");
	CHECK(pC != NULL, "coords.time.min");
	if(pC){
		const char* sLo = NULL; const char* sHi = NULL;
		CHECK(DasSrcCtrl_range(pC, &sLo, &sHi), "coordinate validRange reaches min");
		CHECK(sLo && (strncmp(sLo, "2025-", 5) == 0), "range low is the mission start, got %s", sLo);
		CHECK(streq(DasSrcProp_title(DasSrcCtrl_prop(pC), sBuf, sizeof(sBuf)), "Minimum time value to stream"),
		      "node title wins over the composed one");
		CHECK(streq(DasSrcProp_units(DasSrcCtrl_prop(pC)), "UTC"), "UTC from the group units");
	}
	pC = ctrlByKey(pSrc, "coords.time.res");
	CHECK(pC && !DasSrcCtrl_range(pC, NULL, NULL) && !DasSrcCtrl_required(pC), "res: no range, optional");
	const DasSrcProp* p = byKey(pSrc, "formats.csv.headers");
	CHECK(p && streq(DasSrcProp_value(p), "true") && (DasSrcProp_type(p) == das_srcprop_bool), "headers default true");

	/* format groups: das is on by default, mime from it */
	pG = DasSrc_findGroup(pSrc, das_src_sec_format, "das");
	CHECK(pG && DasSrcGroup_mimeCount(pG) == 3, "three das mimes");
	CHECK(streq(DasSrc_mime(pSrc, NULL), "application/vnd.das2.das2stream"), "default mime: %s", DasSrc_mime(pSrc, NULL));

	URL_IS(pSrc, 0, NONE, NONE,
		HB "?read.time.min=2026-01-06T14%3A12&read.time.max=2026-01-06T14%3A24");
	URL_IS(pSrc, 0, K("version=das2", "nmi_errors", "irq3_errors", "data.hsk_task_errors"), NONE,
		HB "?read.time.min=2026-01-06T14%3A12&read.time.max=2026-01-06T14%3A24&read.data=nmi_errors%2Cirq3_errors%2Chsk_task_errors&format.version=2");
	URL_IS(pSrc, 1, NONE, NONE,
		HB "?read.time.min=2026-01-05T14%3A24&read.time.max=2026-01-06T14%3A24&format.type=cdf");
	URL_IS(pSrc, 0, K("formats.csv", "das.version=das2", "time.res=10"), NONE,
		HB "?read.time.min=2026-01-06T14%3A12&read.time.max=2026-01-06T14%3A24&bin.time.max=10&format.version=2&format.type=csv");

	DasReq* pReq = DasSrc_newReq(pSrc, 1);
	CHECK(pReq && streq(DasSrc_mime(pSrc, pReq), "application/x-cdf"), "cdf example means a cdf: %s",
	      pReq ? DasSrc_mime(pSrc, pReq) : "");
	del_DasReq(pReq);

	del_DasSrc(pSrc);
}

static void test_juno(void)
{
	printf("juno_survey, 2018 layout:\n");
	DasSrc* pSrc = loadSrc(JUNO, "tag:das2.org,2012:site:/uiowa/juno/wav/survey/das2");
	CHECK(pSrc != NULL, "load");
	if(pSrc == NULL) return;

	CHECK(ctrlCount(pSrc, das_src_sec_coord) == 3, "three time controls");
	CHECK(ctrlCount(pSrc, das_src_sec_option) >= 8, "flag options found");
	CHECK(ctrlCount(pSrc, das_src_sec_data) == 0, "nothing settable in data");
	CHECK(DasSrc_groupCount(pSrc, das_src_sec_data) > 0, "but the variables are listed");
	CHECK(DasSrc_groupCount(pSrc, das_src_sec_option) == 1, "one options group");
	CHECK(DasSrc_findGroup(pSrc, das_src_sec_option, "") != NULL, "named by the empty string");

	/* the fixed frequency axis is visible before any download */
	const DasSrcGroup* pG = DasSrc_findGroup(pSrc, das_src_sec_coord, "frequency");
	CHECK(pG != NULL, "frequency group");
	if(pG){
		CHECK(DasSrcGroup_ctrlCount(pG) == 0, "nothing to set");
		const DasSrcProp* pU = DasSrcGroup_findProp(pG, "units");
		CHECK(pU && (DasSrcProp_ctrl(pU) == NULL) && DasSrcProp_value(pU)[0], "units %s",
		      pU ? DasSrcProp_value(pU) : "(none)");
	}
	/* time first even though the node lists frequency first */
	CHECK(streq(DasSrcGroup_name(DasSrc_group(pSrc, das_src_sec_coord, 0)), "time"), "time first");

	/* Keys use the node's long spelling, matching folds it */
	const DasSrcProp* p = byKey(pSrc, "coords.time.minimum");
	CHECK(p != NULL, "coords.time.minimum keyed as spelled");
	CHECK(byKey(pSrc, "coords.time.min") == NULL, "not rewritten to min");
	if(p){
		const DasSrcCtrl* pC = DasSrcProp_ctrl(p);
		CHECK(pC && streq(DasSrcCtrl_param(pC), "start_time"), "param");
		CHECK(pC && DasSrcCtrl_required(pC), "required from the set block");
		CHECK(streq(DasSrcProp_value(p), "2018-10-02T11:00"), "default kept");
		const char* sLo = NULL; const char* sHi = NULL;
		CHECK(pC && DasSrcCtrl_range(pC, &sLo, &sHi) && streq(sHi, "now"), "set range to now");
		CHECK(DasSrcProp_type(p) == das_srcprop_time, "type from the old parameter dict");
		CHECK(streq(DasSrcProp_units(p), "UTC"), "UTC from the old units dict");
		char sBuf[64];
		CHECK(streq(DasSrcProp_title(p, sBuf, sizeof(sBuf)), "Minimum Time value to output"),
		      "composed from the group name, got '%s'", DasSrcProp_title(p, sBuf, sizeof(sBuf)));
	}
	p = byKey(pSrc, "coords.time.resolution");
	CHECK(p && streq(DasSrcProp_value(p), "86.4"), "number default in source notation");
	CHECK(p && streq(DasSrcProp_units(p), "s"), "units string on the property");

	p = byKey(pSrc, "options.lfrl");
	CHECK(p != NULL, "options.lfrl");
	if(p){
		CHECK(streq(DasSrcGroup_name(DasSrcProp_group(p)), ""), "options have no group name");
		CHECK(DasSrcProp_type(p) == das_srcprop_bool, "flag toggle is boolean");
		CHECK(streq(DasSrcCtrl_param(DasSrcProp_ctrl(p)), "params"), "packed parameter");
		CHECK(streq(DasSrcProp_value(p), "false"), "default false");
	}
	p = byKey(pSrc, "options.das2_text");
	CHECK(p && (DasSrcProp_type(p) == das_srcprop_bool), "boolean from a boolean default");

	/* 2018 examples come out in interface terms */
	CHECK(DasSrc_exampleCount(pSrc) >= 8, "examples, got %zu", DasSrc_exampleCount(pSrc));
	CHECK(streq(DasSrc_exampleLabel(pSrc, 0), "Perijove 16"), "label from the title: %s", DasSrc_exampleLabel(pSrc, 0));
	DasReq* pReq = DasSrc_newReq(pSrc, 0);
	CHECK(pReq != NULL, "example 0 req");
	if(pReq){
		CHECK(streq(DasReq_value(pReq, "coords.time.minimum"), "2018-10-02T11:00"), "start_time mapped back");
		CHECK(streq(DasReq_value(pReq, "coords.time.resolution"), "86.4"), "resolution mapped back");
		CHECK(DasReq_rawCount(pReq) == 0, "nothing left raw, %zu", DasReq_rawCount(pReq));
		del_DasReq(pReq);
	}
	pReq = DasSrc_newReq(pSrc, 7);
	CHECK(pReq != NULL, "example 7 req");
	if(pReq){
		CHECK(streq(DasReq_value(pReq, "options.b"), "true"), "params=B became the b toggle");
		CHECK(DasReq_rawCount(pReq) == 0, "nothing left raw");
		del_DasReq(pReq);
	}

	URL_IS(pSrc, -1, K("time=2021-02-21,2021-02-22", "b", "lfrl", "das2_text"), NONE,
		JB "&end_time=2021-02-22&start_time=2021-02-21&params=B%20LFRL&ascii=true");
	URL_IS(pSrc, -1, K("time=2021-02-21,2021-02-22,0", "ascii", "option.xmas=true"), NONE,
		JB "&end_time=2021-02-22&start_time=2021-02-21&resolution=0&params=ASCII%20XMAS");
	/* example 7 with the resolution changed.  The parameter order is the
	   interface order now that the example is settings, not raw text. */
	URL_IS(pSrc, 7, K("resolution=10"), NONE,
		JB "&end_time=2016-177T00%3A00&start_time=2016-176T00%3A00&resolution=10&params=B");

	del_DasSrc(pSrc);
}

static void test_req(void)
{
	printf("Requisitions without a source:\n");
	DasReq* pReq = new_DasReq();
	CHECK(pReq != NULL, "new");
	CHECK(DasReq_count(pReq) == 0 && streq(DasReq_srcPath(pReq), ""), "empty and unstamped");
	daslog_setlevel(DASLOG_CRIT);
	CHECK(DasReq_set(pReq, "", "x") == DASERR_SRC, "empty key refused");
	daslog_setlevel(DASLOG_WARN);
	CHECK(DasReq_set(pReq, "Time", "2026-01-02") == DAS_OKAY, "set");
	CHECK(DasReq_set(pReq, "time", "2026-01-03") == DAS_OKAY, "replace, case insensitive");
	CHECK(DasReq_count(pReq) == 1 && streq(DasReq_value(pReq, "TIME"), "2026-01-03"), "one entry");
	CHECK(DasReq_set(pReq, "res", NULL) == DAS_OKAY && streq(DasReq_value(pReq, "res"), ""), "NULL is empty");
	CHECK(DasReq_find(pReq, "res") == 1 && DasReq_find(pReq, "nope") == -1, "find");
	CHECK(DasReq_verdict(pReq, 0) == das_req_unchecked && streq(DasReq_why(pReq, 0), ""), "unchecked");
	CHECK(DasReq_setRaw(pReq, "extra", "1") == DAS_OKAY && DasReq_rawCount(pReq) == 1, "raw");
	const char* sVal = NULL;
	CHECK(streq(DasReq_raw(pReq, 0, &sVal), "extra") && streq(sVal, "1"), "raw read back");
	CHECK(DasReq_remove(pReq, "res") && !DasReq_remove(pReq, "res"), "remove once");
	DasReq_setLabel(pReq, "a \"quoted\" label\n");
	DasReq_setSrc(pReq, "site:/x", "http://x/y.json", "0.7");

	/* round trip through text */
	char* sText = DasReq_text(pReq);
	CHECK(sText != NULL && strstr(sText, "\\\"quoted\\\"") != NULL, "escaped label");
	DasReq* pBack = new_DasReq_text(sText, strlen(sText));
	CHECK(pBack != NULL, "parses back");
	if(pBack){
		CHECK(streq(DasReq_label(pBack), "a \"quoted\" label\n"), "label survives");
		CHECK(streq(DasReq_srcPath(pBack), "site:/x") && streq(DasReq_srcVersion(pBack), "0.7"), "stamp survives");
		CHECK(streq(DasReq_value(pBack, "time"), "2026-01-03"), "setting survives");
		CHECK(DasReq_rawCount(pBack) == 1, "raw survives");
		del_DasReq(pBack);
	}
	free(sText);

	/* the example settings shape reads too */
	const char* sEx = "{\"settings\":{\"coords/time/props/min\":\"2026-01-01\",\"coords/time/props/max\":\"2026-01-02\"},"
	                  "\"label\":\"ex\"}";
	pBack = new_DasReq_text(sEx, strlen(sEx));
	CHECK(pBack && streq(DasReq_value(pBack, "coords.time.min"), "2026-01-01"), "slash paths fold to keys");
	del_DasReq(pBack);

	daslog_setlevel(DASLOG_CRIT);
	CHECK(new_DasReq_text("[1,2]", 5) == NULL, "not an object");
	CHECK(new_DasReq_file("test/das_fetch/no_such_file.json") == NULL, "missing file");
	daslog_setlevel(DASLOG_WARN);

	/* file round trip and a copy.  ./tmp is git ignored, so a fresh
	   checkout, CI for one, does not have it */
	das_mkdirsto("tmp/TestSource_req.json");
	CHECK(DasReq_write(pReq, "tmp/TestSource_req.json") == DAS_OKAY, "write");
	pBack = new_DasReq_file("tmp/TestSource_req.json");
	CHECK(pBack && streq(DasReq_value(pBack, "time"), "2026-01-03"), "read back from file");
	del_DasReq(pBack);
	remove("tmp/TestSource_req.json");

	DasReq* pCopy = copy_DasReq(pReq);
	CHECK(pCopy && DasReq_count(pCopy) == 1 && DasReq_rawCount(pCopy) == 1, "copy");
	del_DasReq(pCopy);

	DasReq_clear(pReq);
	CHECK(DasReq_count(pReq) == 0 && DasReq_rawCount(pReq) == 0 && streq(DasReq_srcPath(pReq), "site:/x"),
	      "clear keeps the stamp");
	del_DasReq(pReq);
}

static void test_node(void)
{
	printf("From a node tree:\n");
	DasCat* pCat = new_DasCat(NULL, "TestSource");
	DasCat_setSeed(pCat, GEOMAG, "tag:das2.org,2012:site:/uiowa/geomag/flex");
	DasNode* pRoot = DasCat_seed(pCat);
	CHECK(pRoot != NULL, "node loads");
	if(pRoot == NULL){ del_DasCat(pCat); return; }
	DasSrc* pSrc = new_DasSrc_node(pRoot);
	CHECK(pSrc != NULL, "source from node");
	del_DasCat(pCat);   /* takes the node; the source must not care */
	if(pSrc){
		CHECK(streq(DasSrc_path(pSrc), "tag:das2.org,2012:site:/uiowa/geomag/flex"), "path from the node");
		CHECK(ctrlCount(pSrc, -1) == 17, "same interface after the tree is gone");
		URL_IS(pSrc, 0, NONE, NONE,
			GB "?read.time.min=2026-01-01&read.time.max=2026-01-02&read.time.inter=60");
		del_DasSrc(pSrc);
	}

	daslog_setlevel(DASLOG_CRIT);
	const char* sNotSrc = "{\"type\":\"Catalog\",\"catalog\":{}}";
	CHECK(new_DasSrc_json(sNotSrc, strlen(sNotSrc), NULL, NULL) == NULL, "a catalog is not a source");
	CHECK(new_DasSrc_json("not json", 8, NULL, NULL) == NULL, "garbage is not a source");
	daslog_setlevel(DASLOG_WARN);

	/* a set block with no param is a fixed property with a warning, the
	   rest still builds */
	const char* sDoc =
		"{\"type\":\"HttpStreamSrc\",\"protocol\":{\"baseUrls\":[\"http://x/y\"],\"httpParams\":{}},"
		"\"interface\":{\"options\":{"
		"  \"good\":{\"value\":false,\"set\":{\"param\":\"g\",\"value\":true}},"
		"  \"broken\":{\"value\":false,\"set\":{\"value\":true}},"
		"  \"doc_only\":{\"value\":\"x\"}"
		"}}}";
	daslog_setlevel(DASLOG_ERROR);
	DasSrc* pSrc2 = new_DasSrc_json(sDoc, strlen(sDoc), NULL, NULL);
	daslog_setlevel(DASLOG_WARN);
	CHECK(pSrc2 != NULL, "builds");
	if(pSrc2){
		CHECK(streq(DasSrc_path(pSrc2), ""), "nameless and placeless means no URI: %s", DasSrc_path(pSrc2));
		CHECK(DasSrc_propCount(pSrc2) == 3 && ctrlCount(pSrc2, -1) == 1, "three props, one control");
		CHECK(streq(DasSrcProp_key(DasSrc_prop(pSrc2, 0)), "options.good"), "the good one");
		CHECK(DasSrcProp_type(DasSrc_prop(pSrc2, 0)) == das_srcprop_bool, "boolean by default value");
		CHECK(streq(DasSrcProp_title(DasSrc_prop(pSrc2, 0), NULL, 0), ""), "untitled option is empty");
		CHECK(streq(DasSrc_mime(pSrc2, NULL), ""), "no mime stated");
		URL_IS(pSrc2, -1, K("good"), NONE, "http://x/y?g=true");
		URL_IS(pSrc2, -1, NONE, NONE, "http://x/y");
		del_DasSrc(pSrc2);
	}
}

/* Choosing among a container's sources by what the client reads */
static void test_pick(void)
{
	printf("Loadable sources under a container:\n");
	const char* aNames[4];
	DasCat* pCat = new_DasCat(NULL, "TestSource");
	DasCat_setSeed(pCat, "test/das_fetch/cat/twin.json", "site:/uiowa/twin");
	DasNode* pTwin = DasCat_seed(pCat);
	CHECK(pTwin != NULL, "twin loads");
	if(pTwin == NULL){ del_DasCat(pCat); return; }
	CHECK(dassrc_loadable(pTwin, aNames, 4) == 2 && streq(aNames[0], "flex") && streq(aNames[1], "das3"),
	      "two loadable children in catalog order, the DSDF is skipped");
	CHECK(dassrc_loadable(pTwin, NULL, 0) == 2, "counting only");
	daslog_setlevel(DASLOG_CRIT);
	CHECK(new_DasSrc_node(pTwin) == NULL, "a container is not a source, no child is picked");
	daslog_setlevel(DASLOG_WARN);

	DasNode* pSub = DasCat_subNode(pCat, pTwin, "das3");
	CHECK(pSub != NULL, "the caller's choice loads through the tree");
	CHECK(dassrc_loadable(pSub, NULL, 0) == 0, "a source has no loadable children");
	DasSrc* pSrc = (pSub == NULL) ? NULL : new_DasSrc_node(pSub);
	CHECK(pSrc != NULL && streq(DasSrc_path(pSrc), "tag:das2.org,2012:site:/uiowa/twin/das3"), "source from the chosen child");
	del_DasSrc(pSrc);
	del_DasCat(pCat);

	pCat = new_DasCat(NULL, "TestSource");
	DasCat_setSeed(pCat, "test/das_fetch/cat/old.json", "site:/uiowa/old");
	DasNode* pOld = DasCat_seed(pCat);
	CHECK(pOld != NULL, "old loads");
	if(pOld == NULL){ del_DasCat(pCat); return; }
	CHECK(dassrc_loadable(pOld, aNames, 4) == 2, "a 2018 Collection lists both HttpStreamSrc children");

	/* Presented in the current layout: the provides list each convention
	   implies is there, the convention is untouched, and the DOM still
	   serializes with the additions in place */
	CHECK(streq(DasNode_subName(pOld, 0), "das2") && streq(DasNode_subName(pOld, 1), "hapi"), "fixture order");
	const char* sMime = DasJdo_string(DasJdo_get(DasNode_subStub(pOld, 0), "provides/0"));
	CHECK(sMime != NULL && streq(sMime, "application/vnd.das2.das2stream"), "das2/2.2 presents as a das2 stream");
	sMime = DasJdo_string(DasJdo_get(DasNode_subStub(pOld, 1), "provides/0"));
	CHECK(sMime != NULL && streq(sMime, "application/vnd.hapi.stream"), "hapi/2.0 presents as a hapi stream");
	CHECK(DasJdo_get(DasNode_subStub(pOld, 1), "convention") != NULL, "the convention stays");
	size_t uDoc = 0;
	char* sDoc = (char*)DasJdo_writeMinified(DasNode_getJdo(pOld, NULL), &uDoc);
	CHECK(sDoc != NULL && strstr(sDoc, "\"provides\":[\"application/vnd.hapi.stream\"]") != NULL, "the added list serializes");
	free(sDoc);
	del_DasCat(pCat);
}

int main(int argc, char** argv)
{
	das_init(argv[0], DASERR_DIS_EXIT, 0, DASLOG_WARN, NULL);
	test_grammar();
	test_scalar();
	test_helpers();
	test_geomag();
	test_hsk();
	test_juno();
	test_req();
	test_node();
	test_pick();
	printf("%d checks passed, %d failed\n", g_nPass, g_nFail);
	return g_nFail ? 1 : 0;
}
