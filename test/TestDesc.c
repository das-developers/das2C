/* Author: C. Piker
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

/* Unit tests for descriptor property storage (das3/descriptor.h) and for
 * splitting property values into items (das3/property.h).
 *
 * Item splitting: DasProp_items() and DasProp_extractItems() must agree,
 * because callers size their output buffers by the first and fill them with
 * the second.  The das2js metadata snapshot tripped over the one place they
 * disagreed: a single-valued property whose value is the empty string
 * counted as one item but extracted as none.  (It came from a das2 stream
 * property of " " that the das3 up-conversion trimmed to "".)
 *
 * Storage: a property that outgrows its slot is appended afresh and the old
 * slot is retired in place, so the SAME NAME can occupy several slots.  The
 * checks below overwrite one name with growing and shrinking values and
 * insist the readers always answer with the latest, which is where the
 * first version of this file found a stale read and a lost copyIn.
 *
 * TestProp.c is a stream round-trip, not a unit test, so these live here.
 */

#include <stdio.h>
#include <string.h>

#include <das3/core.h>

#define CHECK(expr) \
	if(!(expr)){ \
		printf("ERROR: check failed at %s:%d: %s\n", __FILE__, __LINE__, #expr); \
		return 13; \
	}

/* Set one property on a scratch descriptor and hand back a pointer to it. */
static const DasProp* _set(
	DasDesc* pDesc, ubyte uType, const char* sVal, char cSep, int nStandard
){
	if(DasDesc_flexSet(pDesc, NULL, uType, "p", sVal, cSep, NULL, nStandard) != DAS_OKAY)
		return NULL;
	return DasDesc_getProp(pDesc, "p");
}

/* Pull every item through the public extractor into a fixed table */
#define MAX_ITEMS 8
#define ITEM_LEN 16
static char g_aItems[MAX_ITEMS][ITEM_LEN];
static char* g_psItems[MAX_ITEMS];

static int _extract(const DasProp* pProp)
{
	memset(g_aItems, 0, sizeof(g_aItems));
	for(int i = 0; i < MAX_ITEMS; ++i) g_psItems[i] = g_aItems[i];
	return DasProp_extractItems(pProp, g_psItems, MAX_ITEMS, ITEM_LEN);
}

static int test_single(DasDesc* pDesc)
{
	const DasProp* pProp = NULL;

	/* The defect case: an empty single value is one empty item, not zero */
	pProp = _set(pDesc, DASPROP_STRING|DASPROP_SINGLE, "", '\0', DASPROP_DAS3);
	CHECK(pProp != NULL);
	CHECK(DasProp_items(pProp) == 1);
	CHECK(_extract(pProp) == 1);
	CHECK(g_aItems[0][0] == '\0');

	/* das3 trims single values, so whitespace-only collapses to the same case */
	pProp = _set(pDesc, DASPROP_STRING|DASPROP_SINGLE, "  ", '\0', DASPROP_DAS3);
	CHECK(pProp != NULL);
	CHECK(strlen(DasProp_value(pProp)) == 0);
	CHECK(DasProp_items(pProp) == 1);
	CHECK(_extract(pProp) == 1);

	/* das2 keeps the value verbatim; still one item */
	pProp = _set(pDesc, DASPROP_STRING|DASPROP_SINGLE, " ", '\0', DASPROP_DAS2);
	CHECK(pProp != NULL);
	CHECK(strcmp(DasProp_value(pProp), " ") == 0);
	CHECK(DasProp_items(pProp) == 1);
	CHECK(_extract(pProp) == 1);
	CHECK(strcmp(g_aItems[0], " ") == 0);

	/* The ordinary case, and the item is the whole value, separators included */
	pProp = _set(pDesc, DASPROP_STRING|DASPROP_SINGLE, "a; b", '\0', DASPROP_DAS3);
	CHECK(pProp != NULL);
	CHECK(DasProp_items(pProp) == 1);
	CHECK(_extract(pProp) == 1);
	CHECK(strcmp(g_aItems[0], "a; b") == 0);

	/* An empty integer is a bad integer, not a silent zero-length result */
	int64_t nVal = 0;
	pProp = _set(pDesc, DASPROP_INT|DASPROP_SINGLE, "", '\0', DASPROP_DAS3);
	CHECK(pProp != NULL);
	CHECK(DasProp_convertInt(pProp, &nVal, 1) < 0);
	pProp = _set(pDesc, DASPROP_INT|DASPROP_SINGLE, "42", '\0', DASPROP_DAS3);
	CHECK(pProp != NULL);
	CHECK(DasProp_convertInt(pProp, &nVal, 1) == 1);
	CHECK(nVal == 42);
	return 0;
}

static int test_range(DasDesc* pDesc)
{
	const DasProp* pProp = _set(
		pDesc, DASPROP_REAL|DASPROP_RANGE, "1.5 to 2.5", '\0', DASPROP_DAS3
	);
	CHECK(pProp != NULL);
	CHECK(DasProp_items(pProp) == 2);
	CHECK(_extract(pProp) == 2);
	CHECK(strcmp(g_aItems[0], "1.5") == 0);
	CHECK(strcmp(g_aItems[1], "2.5") == 0);
	return 0;
}

static int test_set(DasDesc* pDesc)
{
	const DasProp* pProp = NULL;

	/* Terminator semantics: a trailing separator adds no phantom item ... */
	pProp = _set(pDesc, DASPROP_STRING|DASPROP_SET, "Bx;By;Bz;", ';', DASPROP_DAS3);
	CHECK(pProp != NULL);
	CHECK(DasProp_items(pProp) == 3);
	CHECK(_extract(pProp) == 3);
	CHECK(strcmp(g_aItems[2], "Bz") == 0);

	/* ... an unterminated last item still counts ... */
	pProp = _set(pDesc, DASPROP_STRING|DASPROP_SET, "Bx;By;Bz", ';', DASPROP_DAS3);
	CHECK(pProp != NULL);
	CHECK(DasProp_items(pProp) == 3);
	CHECK(_extract(pProp) == 3);

	/* ... and an empty item between two separators is a real (empty) item */
	pProp = _set(pDesc, DASPROP_STRING|DASPROP_SET, "a;;c", ';', DASPROP_DAS3);
	CHECK(pProp != NULL);
	CHECK(DasProp_items(pProp) == 3);
	CHECK(_extract(pProp) == 3);
	CHECK(g_aItems[1][0] == '\0');
	CHECK(strcmp(g_aItems[2], "c") == 0);

	/* An empty set has no items at all, whichever separator it was given */
	pProp = _set(pDesc, DASPROP_STRING|DASPROP_SET, "", ';', DASPROP_DAS3);
	CHECK(pProp != NULL);
	CHECK(DasProp_items(pProp) == 0);
	CHECK(_extract(pProp) == 0);
	pProp = _set(pDesc, DASPROP_STRING|DASPROP_SET, "", '\0', DASPROP_DAS3);
	CHECK(pProp != NULL);
	CHECK(DasProp_items(pProp) == 0);
	CHECK(_extract(pProp) == 0);

	/* Whitespace splitting: runs of blanks of any kind are one separator and
	   the ends are ignored, whether the separator was given as a space ... */
	pProp = _set(pDesc, DASPROP_INT|DASPROP_SET, "  1 2\t\t3  ", ' ', DASPROP_DAS3);
	CHECK(pProp != NULL);
	CHECK(DasProp_items(pProp) == 3);
	int64_t aVals[4] = {0};
	CHECK(DasProp_convertInt(pProp, aVals, 4) == 3);
	CHECK((aVals[0] == 1) && (aVals[1] == 2) && (aVals[2] == 3));

	/* ... or left for the library to pick (it finds the tab first) */
	pProp = _set(pDesc, DASPROP_STRING|DASPROP_SET, "x\ty  z", '\0', DASPROP_DAS3);
	CHECK(pProp != NULL);
	CHECK(DasProp_items(pProp) == 3);
	CHECK(_extract(pProp) == 3);
	CHECK(strcmp(g_aItems[0], "x") == 0);
	CHECK(strcmp(g_aItems[1], "y") == 0);
	CHECK(strcmp(g_aItems[2], "z") == 0);
	return 0;
}

/* Overwrite one name with values of different sizes and read it back each
   time.  Growth retires a slot and appends; a later shrink must not revive
   the retired slot in front of the live one. */
static int test_overwrite(void)
{
	DasDesc desc;
	memset(&desc, 0, sizeof(desc));
	DasDesc_init(&desc, UNK_DESC);

	const char* psVals[] = {
		"", "a longer value that needs a bigger slot", "x",
		"an even longer value, longer than the slot the second one took", "",
		"medium length value"
	};
	for(size_t u = 0; u < sizeof(psVals)/sizeof(psVals[0]); ++u){
		CHECK(DasDesc_setStr(&desc, "p", psVals[u]) == DAS_OKAY);
		CHECK(DasDesc_getStr(&desc, "p") != NULL);
		CHECK(strcmp(DasDesc_getStr(&desc, "p"), psVals[u]) == 0);
	}

	/* A second name must not be confused with the retired slots of the first */
	CHECK(DasDesc_setStr(&desc, "q", "other") == DAS_OKAY);
	CHECK(strcmp(DasDesc_getStr(&desc, "p"), "medium length value") == 0);
	CHECK(strcmp(DasDesc_getStr(&desc, "q"), "other") == 0);

	/* Removal retires the live slot, not some earlier retired one */
	CHECK(DasDesc_remove(&desc, "p"));
	CHECK(DasDesc_getStr(&desc, "p") == NULL);
	CHECK(!DasDesc_remove(&desc, "p"));
	CHECK(strcmp(DasDesc_getStr(&desc, "q"), "other") == 0);

	/* copyIn with a value that outgrows the slot it replaces must keep it */
	DasDesc src;
	memset(&src, 0, sizeof(src));
	DasDesc_init(&src, UNK_DESC);
	CHECK(DasDesc_setStr(&src, "q", "a replacement much longer than 'other'") == DAS_OKAY);
	CHECK(DasDesc_setStr(&src, "r", "new") == DAS_OKAY);
	DasDesc_copyIn(&desc, &src);
	CHECK(DasDesc_getStr(&desc, "q") != NULL);
	CHECK(strcmp(DasDesc_getStr(&desc, "q"), "a replacement much longer than 'other'") == 0);
	CHECK(strcmp(DasDesc_getStr(&desc, "r"), "new") == 0);
	CHECK(DasDesc_getStr(&desc, "p") == NULL);   /* still removed */

	DasDesc_freeProps(&src);
	DasDesc_freeProps(&desc);
	return 0;
}

int main(int argc, char** argv)
{
	(void)argc;
	setvbuf(stdout, NULL, _IONBF, 0);
	das_init(argv[0], DASERR_DIS_RET, 0, DASLOG_ERROR, NULL);

	/* DasDesc_init() assumes zeroed storage, the way the library's calloc'd
	   objects arrive; stack garbage in the property array header crashes the
	   first set. */
	DasDesc desc;
	memset(&desc, 0, sizeof(desc));
	DasDesc_init(&desc, UNK_DESC);

	int nRet = 0;
	printf("INFO: ======== ERROR lines below are intentional, provoked by negative checks ========\n");
	if((nRet = test_single(&desc)) != 0) return nRet;
	if((nRet = test_range(&desc)) != 0)  return nRet;
	if((nRet = test_set(&desc)) != 0)    return nRet;
	if((nRet = test_overwrite()) != 0)   return nRet;
	printf("INFO: ======== end of intentional errors, the verdict follows ========\n");

	DasDesc_freeProps(&desc);
	printf("INFO: TestDesc: all descriptor property checks passed\n");
	return 0;
}
