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

/* Unit tests for descriptor property storage (das3/desc.h) and for
 * splitting property values into items (das3/prop.h).
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
#include <stdlib.h>
#include <string.h>

#include <das3/core.h>
#include <das3/desc_p.h>   /* tests link the static archive, DasDesc_init on the stack */
#include <das3/prop_p.h>   /* DasProp_size, for the slot invariant */

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

/* Every live property must fit its slot by its own reckoning.  DasDesc_setProp
   copies DasProp_size() bytes out of the source slot, so a size larger than
   the slot reads past it.  Valgrind misses this unless the slot happens to be
   last in the array's allocation, so check the invariant directly. */
static int _check_slots(DasDesc* pDesc)
{
	size_t uProps = DasAry_lengthIn(&(pDesc->properties), DIM0);
	for(size_t u = 0; u < uProps; ++u){
		size_t uSlot = 0;
		const DasProp* pProp = (const DasProp*)DasAry_getBuf(
			&(pDesc->properties), vtUByte, DIM1_AT(u), &uSlot
		);
		if(DasProp_isValid(pProp))
			CHECK(DasProp_size(pProp) <= uSlot);
	}
	return 0;
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
	DasDesc_init(&desc, DASDESC_UNK, NULL);

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
	DasDesc_init(&src, DASDESC_UNK, NULL);
	CHECK(DasDesc_setStr(&src, "q", "a replacement much longer than 'other'") == DAS_OKAY);
	CHECK(DasDesc_setStr(&src, "r", "new") == DAS_OKAY);
	DasDesc_copyIn(&desc, &src);
	CHECK(DasDesc_getStr(&desc, "q") != NULL);
	CHECK(strcmp(DasDesc_getStr(&desc, "q"), "a replacement much longer than 'other'") == 0);
	CHECK(strcmp(DasDesc_getStr(&desc, "r"), "new") == 0);
	CHECK(DasDesc_getStr(&desc, "p") == NULL);   /* still removed */
	CHECK(_check_slots(&src) == 0);    /* "r" = "new" is smaller than a DasProp */
	CHECK(_check_slots(&desc) == 0);

	DasDesc_freeProps(&src);
	DasDesc_freeProps(&desc);
	return 0;
}

/* The size limits land exactly on the bit fields in the flags word: the name
   plus its null must fit the 7 bit value offset and the whole name+value
   must fit the 17 bit total.  One past either limit is refused, the limits
   themselves round trip intact. */
static int test_limits(void)
{
	DasDesc desc;
	memset(&desc, 0, sizeof(desc));
	DasDesc_init(&desc, DASDESC_UNK, NULL);

	enum { NMAX = 126, VMAX = 130943 };
	char sName[NMAX + 2];
	memset(sName, 'n', NMAX + 1);
	sName[NMAX + 1] = '\0';
	char* sVal = (char*)malloc(VMAX + 2);
	CHECK(sVal != NULL);
	memset(sVal, 'v', VMAX + 1);
	sVal[VMAX + 1] = '\0';

	CHECK(DasDesc_setStr(&desc, sName, "x") != DAS_OKAY);   /* name one too long */
	sName[NMAX] = '\0';
	CHECK(DasDesc_setStr(&desc, "big", sVal) != DAS_OKAY);  /* value one too long */
	sVal[VMAX] = '\0';

	/* No standard allows a nameless property, not even loose das1 */
	for(int nStd = DASPROP_DAS1; nStd <= DASPROP_DAS3; ++nStd)
		CHECK(DasDesc_flexSet(&desc, "string", 0, "", "x", 0, NULL, nStd) != DAS_OKAY);
	CHECK(DasDesc_getProp(&desc, "") == NULL);

	CHECK(DasDesc_setStr(&desc, sName, sVal) == DAS_OKAY);
	const DasProp* pProp = DasDesc_getProp(&desc, sName);
	CHECK(pProp != NULL);
	CHECK(strcmp(DasProp_name(pProp), sName) == 0);
	CHECK(strlen(DasProp_value(pProp)) == VMAX);
	/* das_units is a pointer: 4 bytes on wasm32, 8 on 64-bit hosts */
	CHECK(DasProp_size(pProp) == sizeof(uint64_t) + sizeof(das_units) + NMAX + 1 + VMAX + 1);

	/* The published limits are the ones just tested */
	CHECK(daslimit_get("max_prop_name_bytes") != NULL);
	CHECK(daslimit_get("max_prop_name_bytes")->nLimit == NMAX);
	CHECK(daslimit_get("max_prop_value_bytes") != NULL);
	CHECK(daslimit_get("max_prop_value_bytes")->nLimit == VMAX);

	free(sVal);
	DasDesc_freeProps(&desc);
	return 0;
}

/* The limit table: every entry reachable by the walk is also found by name,
   names are unique and fields are filled in.  Unknown names and pointers
   that are not from the table give NULL. */
static int test_daslimit(void)
{
	size_t uCount = 0;
	const das_limit* pLimit;
	for(pLimit = daslimit_getNext(NULL); pLimit != NULL; pLimit = daslimit_getNext(pLimit)){
		CHECK(pLimit->sName != NULL && pLimit->sName[0] != '\0');
		CHECK(pLimit->nLimit > 0);
		CHECK(daslimit_get(pLimit->sName) == pLimit);   /* also proves uniqueness */
		CHECK(++uCount < 1000);                           /* the walk ends */
	}
	CHECK(uCount >= 8);

	CHECK(daslimit_get(NULL) == NULL);
	CHECK(daslimit_get("no_such_limit") == NULL);
	CHECK(daslimit_get("max_dataset_rank")->nLimit == VARIDX_MAX);

	/* A copy is not an entry, stepping from it must not walk off */
	das_limit copy = *daslimit_getNext(NULL);
	CHECK(daslimit_getNext(&copy) == NULL);
	return 0;
}

/* A refused set must change nothing: no new slot, and a value being replaced
   survives.  The property array can't give a slot back, so the checks have
   to run before a slot is taken.  The first version took the slot first:
   a refused replace lost the old value, and every refusal left a dead slot
   behind (0xFF filled at the time, which read as a live, unterminated name). */
static int test_refused(void)
{
	DasDesc desc;
	memset(&desc, 0, sizeof(desc));
	DasDesc_init(&desc, DASDESC_UNK, NULL);

	CHECK(DasDesc_setStr(&desc, "p", "short") == DAS_OKAY);
	size_t uSlots = DasAry_lengthIn(&(desc.properties), DIM0);   /* raw slots, live or not */

	/* Replace that would outgrow the slot: not a range, so refused */
	CHECK(DasDesc_flexSet(&desc, "realrange", 0, "p",
		"a value much longer than short, and not a range", 0, NULL, DASPROP_DAS2
	) != DAS_OKAY);
	CHECK(DasDesc_getStr(&desc, "p") != NULL);
	CHECK(strcmp(DasDesc_getStr(&desc, "p"), "short") == 0);
	CHECK(DasAry_lengthIn(&(desc.properties), DIM0) == uSlots);

	/* Replace that would fit in place, also refused */
	CHECK(DasDesc_flexSet(&desc, "realrange", 0, "p", "x", 0, NULL, DASPROP_DAS2) != DAS_OKAY);
	CHECK(strcmp(DasDesc_getStr(&desc, "p"), "short") == 0);
	CHECK(DasAry_lengthIn(&(desc.properties), DIM0) == uSlots);

	/* New names refused for several reasons, none may take a slot */
	CHECK(DasDesc_flexSet(&desc, "string", 0, "bad name", "x", 0, NULL, DASPROP_DAS2) != DAS_OKAY);
	CHECK(DasDesc_flexSet(&desc, "nosuchtype", 0, "q", "x", 0, NULL, DASPROP_DAS2) != DAS_OKAY);
	CHECK(DasDesc_flexSet(&desc, NULL, 0, "q", "x", 0, NULL, DASPROP_DAS3) != DAS_OKAY);
	CHECK(DasDesc_flexSet(&desc, "string", 0, NULL, "x", 0, NULL, DASPROP_DAS2) != DAS_OKAY);
	CHECK(DasAry_lengthIn(&(desc.properties), DIM0) == uSlots);
	CHECK(DasDesc_getProp(&desc, "q") == NULL);

	/* Later sets still work and still find the right slots */
	CHECK(DasDesc_setStr(&desc, "q", "fine") == DAS_OKAY);
	CHECK(strcmp(DasDesc_getStr(&desc, "p"), "short") == 0);
	CHECK(strcmp(DasDesc_getStr(&desc, "q"), "fine") == 0);
	CHECK(_check_slots(&desc) == 0);
	DasDesc_freeProps(&desc);

	/* Both constructors give property storage a zero fill, so any slot that
	   is ever taken without being written reads as an invalid, nameless
	   property instead of a live one with no terminator. */
	DasDesc* pHeap = new_DasDesc();
	DasDesc* aDesc[2] = {&desc, pHeap};
	memset(&desc, 0, sizeof(desc));
	DasDesc_init(&desc, DASDESC_UNK, NULL);
	for(int i = 0; i < 2; ++i){
		CHECK(DasAry_append(&(aDesc[i]->properties), NULL, 32) != NULL);
		size_t uLen = 0;
		const ubyte* pSlot = DasAry_getBuf(
			&(aDesc[i]->properties), vtUByte, DIM1_AT(0), &uLen
		);
		CHECK(uLen == 32);
		for(size_t u = 0; u < uLen; ++u) CHECK(pSlot[u] == 0);
	}
	DasDesc_freeProps(&desc);
	del_DasDesc(pHeap);
	return 0;
}

/* Values and names read from a descriptor and set back into the same one.
   Taking a new slot may move the property storage, and the first version
   then copied from freed memory; das2_from_cdf sets ySummary from yLabel
   this way.  Enough sets to force several moves. */
static int test_alias(void)
{
	DasDesc desc;
	memset(&desc, 0, sizeof(desc));
	DasDesc_init(&desc, DASDESC_UNK, NULL);

	char sBig[2000];
	memset(sBig, 'a', sizeof(sBig) - 1);
	sBig[sizeof(sBig) - 1] = '\0';
	CHECK(DasDesc_setStr(&desc, "a", sBig) == DAS_OKAY);

	char sName[16];
	for(int i = 0; i < 64; ++i){
		snprintf(sName, sizeof(sName), "b%d", i);
		CHECK(DasDesc_setStr(&desc, sName, DasDesc_getStr(&desc, "a")) == DAS_OKAY);
	}
	CHECK(strcmp(DasDesc_getStr(&desc, "b63"), sBig) == 0);

	/* Name from the storage, value that outgrows its slot */
	const char* sOwnName = DasDesc_getNameByIdx(&desc, 1);   /* "b0" */
	CHECK(DasDesc_setStr(&desc, sOwnName, sBig + 1000) == DAS_OKAY);
	CHECK(strcmp(DasDesc_getStr(&desc, "b0"), sBig + 1000) == 0);

	/* A property set onto itself, and onto a descriptor that has it */
	const DasProp* pProp = DasDesc_getProp(&desc, "a");
	CHECK(DasDesc_setProp(&desc, pProp) == DAS_OKAY);
	CHECK(strcmp(DasDesc_getStr(&desc, "a"), sBig) == 0);

	CHECK(_check_slots(&desc) == 0);
	DasDesc_freeProps(&desc);
	return 0;
}

/* DasDesc_length() and the by-index getters count live properties only.
   Replaced and removed values stay behind as retired slots, which an index
   walk must never see, and equality must not depend on them. */
static int test_live_index(void)
{
	DasDesc desc;
	memset(&desc, 0, sizeof(desc));
	DasDesc_init(&desc, DASDESC_UNK, NULL);

	CHECK(DasDesc_setStr(&desc, "a", "1") == DAS_OKAY);
	CHECK(DasDesc_setStr(&desc, "b", "2") == DAS_OKAY);
	CHECK(DasDesc_setStr(&desc, "c", "3") == DAS_OKAY);
	CHECK(DasDesc_length(&desc) == 3);

	/* Grow "a" past its slot (retires one) and remove "b" (retires another) */
	CHECK(DasDesc_setStr(&desc, "a", "a value longer than the first slot") == DAS_OKAY);
	CHECK(DasDesc_remove(&desc, "b"));
	CHECK(DasDesc_length(&desc) == 2);

	/* Slot order is c, then the new a */
	CHECK(strcmp(DasDesc_getNameByIdx(&desc, 0), "c") == 0);
	CHECK(strcmp(DasDesc_getValByIdx(&desc, 0), "3") == 0);
	CHECK(strcmp(DasDesc_getNameByIdx(&desc, 1), "a") == 0);
	CHECK(strcmp(DasDesc_getValByIdx(&desc, 1), "a value longer than the first slot") == 0);
	CHECK(DasDesc_getTypeByIdx(&desc, 1) != NULL);
	CHECK(DasDesc_getTypeByIdx3(&desc, 1) != NULL);
	CHECK(DasDesc_getPropByIdx(&desc, 1) == DasDesc_getProp(&desc, "a"));
	CHECK(DasDesc_getNameByIdx(&desc, 2) == NULL);
	CHECK(DasDesc_getValByIdx(&desc, 2) == NULL);
	CHECK(DasDesc_getTypeByIdx(&desc, 2) == NULL);
	CHECK(DasDesc_getPropByIdx(&desc, 2) == NULL);

	/* Same live properties with no history compare equal, both ways */
	DasDesc other;
	memset(&other, 0, sizeof(other));
	DasDesc_init(&other, DASDESC_UNK, NULL);
	CHECK(DasDesc_setStr(&other, "a", "a value longer than the first slot") == DAS_OKAY);
	CHECK(DasDesc_setStr(&other, "c", "3") == DAS_OKAY);
	CHECK(DasDesc_equals(&desc, &other));
	CHECK(DasDesc_equals(&other, &desc));
	CHECK(DasDesc_setStr(&other, "d", "4") == DAS_OKAY);
	CHECK(!DasDesc_equals(&desc, &other));
	DasDesc_freeProps(&other);

	/* Remove everything: nothing to iterate, nothing to encode */
	CHECK(DasDesc_remove(&desc, "a"));
	CHECK(DasDesc_remove(&desc, "c"));
	CHECK(DasDesc_length(&desc) == 0);
	CHECK(DasDesc_getNameByIdx(&desc, 0) == NULL);

	/* Clearing resets the count along with the slots */
	DasDesc_clearProps(&desc);
	CHECK(DasDesc_length(&desc) == 0);
	CHECK(DasDesc_setStr(&desc, "z", "26") == DAS_OKAY);
	CHECK(DasDesc_length(&desc) == 1);
	CHECK(strcmp(DasDesc_getNameByIdx(&desc, 0), "z") == 0);

	DasDesc_freeProps(&desc);
	return 0;
}

/* One x plane and one two-channel yscan with a y-tag list.  Both packets
   built here get the same default y-tag encoding, so the compare in
   DasPkt_equalFormat() gets as far as the y-tag arrays. */
static DasPkt* _mk_scan_pkt(void)
{
	static const double aTags[2] = {1.0, 2.0};
	DasPkt* pPkt = new_DasPkt();
	DasPkt_addPlane(pPkt, new_DasPlane(
		DASPLANE_X, "", new_DasEnc(DAS2DT_LE_REAL, 8, NULL), UNIT_US2000
	));
	DasPkt_addPlane(pPkt, new_DasPlane_yscan(
		"amp", new_DasEnc(DAS2DT_LE_REAL, 4, NULL), UNIT_DIMENSIONLESS, 2,
		NULL, aTags, UNIT_HERTZ
	));
	return pPkt;
}

/* DasPlane_setYTagSeq() turns a list yscan into an interval one by freeing
   the y-tag array but keeps the y encoding.  Compared against a plain list
   plane it passes the encoding check and leaves one y-tag pointer NULL,
   which DasPkt_equalFormat() once read through in either argument order. */
static int test_pkt_ytag_style(void)
{
	DasPkt* pList = _mk_scan_pkt();
	DasPkt* pSeq  = _mk_scan_pkt();
	CHECK(DasPkt_equalFormat(pList, pSeq));

	DasPlane_setYTagSeq(DasPkt_getPlane(pSeq, 1), 1.0, 0.0, DAS_FILL_VALUE);
	CHECK(!DasPkt_equalFormat(pList, pSeq));
	CHECK(!DasPkt_equalFormat(pSeq, pList));

	del_DasDesc((DasDesc*)pList);
	del_DasDesc((DasDesc*)pSeq);
	return 0;
}

/* A stream files descriptors by packet ID, 1 to 0xFEFF.  Datasets may share
   an ID, the last one attached is active and freeing it brings the one
   before back; das2 packets never share.  pNever is never attached, so it
   stands in for a stale pointer without comparing a freed one. */
static int test_stream_ids(void)
{
	DasStream* pSd = new_DasStream();
	DasDs* pA = new_DasDs("a", "g", 1);
	DasDs* pB = new_DasDs("b", "g", 1);
	DasDs* pNever = new_DasDs("never", "g", 1);
	DasPkt* pP = new_DasPkt();

	/* IDs past the das2 range are fine on a stream */
	CHECK(DasStream_addDesc(pSd, (DasDesc*)pA, 800) == DAS_OKAY);
	CHECK(DasStream_getDesc(pSd, 800) == (DasDesc*)pA);
	CHECK(DasStream_getPktId(pSd, (DasDesc*)pA) == 800);

	/* A second dataset on the same ID becomes active, the first stays */
	CHECK(DasStream_addDesc(pSd, (DasDesc*)pB, 800) == DAS_OKAY);
	CHECK(DasStream_getDesc(pSd, 800) == (DasDesc*)pB);
	CHECK(DasStream_isDescActive(pSd, (DasDesc*)pB));
	CHECK(!DasStream_isDescActive(pSd, (DasDesc*)pA));
	CHECK(DasStream_getPktId(pSd, (DasDesc*)pA) == 800);
	CHECK(DasStream_numDesc(pSd, DASDESC_DS) == 2);
	CHECK(DasStream_addDesc(pSd, (DasDesc*)pA, 801) != DAS_OKAY);  /* already attached */

	/* das2 packets never share an ID, in either direction */
	CHECK(DasStream_addDesc(pSd, (DasDesc*)pP, 800) != DAS_OKAY);
	CHECK(DasStream_addDesc(pSd, (DasDesc*)pP, 7) == DAS_OKAY);
	CHECK(DasStream_addDesc(pSd, (DasDesc*)pNever, 7) != DAS_OKAY);
	CHECK(DasStream_getPkt(pSd, 7) == pP);
	CHECK(DasStream_getPkt(pSd, 800) == NULL);
	CHECK(DasStream_isDescActive(pSd, (DasDesc*)pP));

	/* Walk in attach order, with and without a type filter */
	CHECK(DasStream_nextDesc(pSd, NULL, DASDESC_UNK) == (DasDesc*)pA);
	CHECK(DasStream_nextDesc(pSd, (DasDesc*)pA, DASDESC_UNK) == (DasDesc*)pB);
	CHECK(DasStream_nextDesc(pSd, (DasDesc*)pB, DASDESC_UNK) == (DasDesc*)pP);
	CHECK(DasStream_nextDesc(pSd, (DasDesc*)pP, DASDESC_UNK) == NULL);
	CHECK(DasStream_nextDesc(pSd, NULL, DASDESC_PKT) == (DasDesc*)pP);
	CHECK(DasStream_nextDesc(pSd, (DasDesc*)pA, DASDESC_DS) == (DasDesc*)pB);
	CHECK(DasStream_nextDesc(pSd, (DasDesc*)pB, DASDESC_DS) == NULL);
	CHECK(DasStream_numDesc(pSd, DASDESC_PKT) == 1);
	CHECK(DasStream_numDesc(pSd, DASDESC_UNK) == 3);

	/* Freeing the active one pops back to the previous */
	CHECK(DasStream_freeDesc(pSd, (DasDesc*)pB) == DAS_OKAY);
	CHECK(DasStream_getDesc(pSd, 800) == (DasDesc*)pA);
	CHECK(DasStream_isDescActive(pSd, (DasDesc*)pA));

	/* Unattached pointers are errors, not crashes */
	CHECK(DasStream_freeDesc(pSd, (DasDesc*)pNever) != DAS_OKAY);
	CHECK(DasStream_nextDesc(pSd, (DasDesc*)pNever, DASDESC_UNK) == NULL);
	CHECK(DasStream_getPktId(pSd, (DasDesc*)pNever) == -1);
	CHECK(!DasStream_isDescActive(pSd, (DasDesc*)pNever));

	/* Range: an unused legal ID is a quiet NULL, an illegal one an error */
	CHECK(DasStream_getDesc(pSd, DAS3_MAX_PKTIDS - 1) == NULL);
	CHECK(DasStream_getDesc(pSd, 0) == NULL);
	CHECK(DasStream_getDesc(pSd, DAS3_MAX_PKTIDS) == NULL);
	CHECK(DasStream_addDesc(pSd, (DasDesc*)pNever, DAS3_MAX_PKTIDS) != DAS_OKAY);
	CHECK(DasStream_addDesc(pSd, (DasDesc*)pNever, 0) != DAS_OKAY);

	/* Grow past the inline list, nothing moves */
	char sId[16];
	for(int i = 0; i < 40; ++i){
		snprintf(sId, sizeof(sId), "grow%d", i);
		CHECK(DasStream_addDesc(pSd, (DasDesc*)new_DasDs(sId, "grow", 1), 1000 + i) == DAS_OKAY);
	}
	CHECK(DasStream_numDesc(pSd, DASDESC_DS) == 41);
	CHECK(DasStream_getPktId(pSd, DasStream_getDesc(pSd, 1039)) == 1039);
	CHECK(DasStream_getDesc(pSd, 800) == (DasDesc*)pA);
	CHECK(DasStream_getPkt(pSd, 7) == pP);

	/* createPkt takes the lowest das2 ID nobody uses */
	DasPkt* pNew = DasStream_createPkt(pSd, new_DasEnc(DAS2DT_LE_REAL, 8, NULL), UNIT_US2000);
	CHECK(pNew != NULL);
	CHECK(DasStream_getPktId(pSd, (DasDesc*)pNew) == 1);

	del_DasDesc((DasDesc*)pNever);
	del_DasDesc((DasDesc*)pSd);
	return 0;
}

/* The writer names every packet ID.  0 belongs to the stream header alone,
   a das2 stream's two digit tags stop at 99, das3 at 0xFEFF, and the ID
   written need not be the one the packet has in its own stream. */
static int test_write_ids(void)
{
	static char aBuf2[4096];
	static char aBuf3[4096];

	DasIO* pOut = new_DasIO("TestDesc", new_DasXport_mem(aBuf2, sizeof(aBuf2), "w"), NULL);
	DasStream* pSd = new_DasStream();     /* version 2.2 */
	CHECK(DasIO_writeDesc(pOut, (DasDesc*)pSd, 5) != DAS_OKAY);
	CHECK(DasIO_writeDesc(pOut, (DasDesc*)pSd, 0) == DAS_OKAY);

	DasPkt* pPkt = DasStream_createPkt(pSd, new_DasEnc(DAS2DT_LE_REAL, 8, NULL), UNIT_US2000);
	CHECK(pPkt != NULL);
	CHECK(DasIO_writeDesc(pOut, (DasDesc*)pPkt, 0) != DAS_OKAY);
	CHECK(DasIO_writeDesc(pOut, (DasDesc*)pPkt, 100) != DAS_OKAY);
	CHECK(DasIO_writeDesc(pOut, (DasDesc*)pPkt, 1) == DAS_OKAY);
	CHECK(DasIO_writeData(pOut, (DasDesc*)pPkt, 0) != DAS_OKAY);
	CHECK(DasIO_writeData(pOut, (DasDesc*)pPkt, 100) != DAS_OKAY);
	CHECK(DasIO_writeData(pOut, (DasDesc*)pPkt, 1) == DAS_OKAY);
	CHECK(strstr(aBuf2, "[01]") != NULL);
	CHECK(strstr(aBuf2, ":01:") != NULL);
	del_DasIO(pOut);
	del_DasDesc((DasDesc*)pSd);

	/* The output version is the writer's choice, not the stream's */
	pOut = new_DasIO("TestDesc", new_DasXport_mem(aBuf3, sizeof(aBuf3), "w"), "3");
	pSd = new_DasStream();
	CHECK(DasIO_writeDesc(pOut, (DasDesc*)pSd, 0) == DAS_OKAY);
	pPkt = DasStream_createPkt(pSd, new_DasEnc(DAS2DT_LE_REAL, 8, NULL), UNIT_US2000);
	CHECK(pPkt != NULL);
	CHECK(DasIO_writeDesc(pOut, (DasDesc*)pPkt, DAS3_MAX_PKTIDS) != DAS_OKAY);
	CHECK(DasIO_writeDesc(pOut, (DasDesc*)pPkt, DAS3_MAX_PKTIDS - 1) == DAS_OKAY);
	CHECK(DasIO_writeData(pOut, (DasDesc*)pPkt, DAS3_MAX_PKTIDS - 1) == DAS_OKAY);
	CHECK(strstr(aBuf3, "|Hx|65279|") != NULL);
	CHECK(strstr(aBuf3, "|Pd|65279|") != NULL);
	del_DasIO(pOut);
	del_DasDesc((DasDesc*)pSd);
	return 0;
}

/* Whether a header was sent is a fact about an output, not about a
   descriptor.  Data may only follow its own header on the same output: not
   a header that went to some other output or under some other ID, not once
   another descriptor's header has taken the ID over, and for das2 not after
   a plane was changed.  The answers must hold up when more IDs are in play
   than DasIO tracks without allocating (16). */
static int test_write_order(void)
{
	static char aBufA[65536];
	static char aBufB[4096];

	DasIO* pA = new_DasIO("TestDesc", new_DasXport_mem(aBufA, sizeof(aBufA), "w"), "3");
	DasIO* pB = new_DasIO("TestDesc", new_DasXport_mem(aBufB, sizeof(aBufB), "w"), "3");
	DasStream* pSd = new_DasStream();
	CHECK(DasIO_writeDesc(pA, (DasDesc*)pSd, 0) == DAS_OKAY);
	CHECK(DasIO_writeDesc(pB, (DasDesc*)pSd, 0) == DAS_OKAY);

	DasDesc* p1 = (DasDesc*)DasStream_createPkt(pSd, new_DasEnc(DAS2DT_LE_REAL, 8, NULL), UNIT_US2000);
	DasDesc* p2 = (DasDesc*)DasStream_createPkt(pSd, new_DasEnc(DAS2DT_LE_REAL, 4, NULL), UNIT_US2000);
	CHECK((p1 != NULL)&&(p2 != NULL));

	/* No header at all, then a header on A that B never saw */
	CHECK(DasIO_writeData(pA, p1, 1) != DAS_OKAY);
	CHECK(DasIO_writeDesc(pA, p1, 1) == DAS_OKAY);
	CHECK(DasIO_writeData(pA, p1, 1) == DAS_OKAY);
	CHECK(DasIO_writeData(pB, p1, 1) != DAS_OKAY);

	/* It went out as ID 1, not ID 2 */
	CHECK(DasIO_writeData(pA, p1, 2) != DAS_OKAY);

	/* Another header takes the ID over, and the first can take it back */
	CHECK(DasIO_writeDesc(pA, p2, 1) == DAS_OKAY);
	CHECK(DasIO_writeData(pA, p1, 1) != DAS_OKAY);
	CHECK(DasIO_writeData(pA, p2, 1) == DAS_OKAY);
	CHECK(DasIO_writeDesc(pA, p1, 1) == DAS_OKAY);
	CHECK(DasIO_writeData(pA, p2, 1) != DAS_OKAY);
	CHECK(DasIO_writeData(pA, p1, 1) == DAS_OKAY);

	/* A changed plane makes the header that was sent stale */
	DasPlane_setFill(DasPkt_getXPlane((DasPkt*)p1), -1.0e31);
	CHECK(DasIO_writeData(pA, p1, 1) != DAS_OKAY);
	CHECK(DasIO_writeDesc(pA, p1, 1) == DAS_OKAY);
	CHECK(DasIO_writeData(pA, p1, 1) == DAS_OKAY);

	/* Past the inline list: every ID still remembers its own header */
	for(int nId = 2; nId <= 40; ++nId)
		CHECK(DasIO_writeDesc(pA, p2, nId) == DAS_OKAY);
	for(int nId = 2; nId <= 40; ++nId){
		CHECK(DasIO_writeData(pA, p2, nId) == DAS_OKAY);
		CHECK(DasIO_writeData(pA, p1, nId) != DAS_OKAY);
	}
	CHECK(DasIO_writeData(pA, p1, 1) == DAS_OKAY);
	CHECK(DasIO_writeData(pA, p2, 41) != DAS_OKAY);

	del_DasIO(pA);
	del_DasIO(pB);
	del_DasDesc((DasDesc*)pSd);
	return 0;
}

/* DasProp_escapeSize() sizes the buffer DasProp_xmlValue() fills, so the
   two must agree exactly.  They once disagreed on '&': a bare one counted
   zero bytes, one starting an entity was skipped as if already escaped, and
   a later entity could cancel an earlier quote's need for escaping. */
static int test_xml_escape(void)
{
	static const char* aCase[][2] = {
		{"plain text",          NULL},
		{"a \"quote\"",         "a &quot;quote&quot;"},
		{"it's <b> & </b>",     "it&apos;s &lt;b&gt; &amp; &lt;/b&gt;"},
		{"trailing &",          "trailing &amp;"},
		{"&lt; stays literal",  "&amp;lt; stays literal"},
		{"\" then &amp;",       "&quot; then &amp;amp;"},
		{"ends with &gt;",      "ends with &amp;gt;"}
	};
	DasDesc desc;
	memset(&desc, 0, sizeof(desc));
	DasDesc_init(&desc, DASDESC_UNK, NULL);

	char sBuf[128];
	for(size_t i = 0; i < sizeof(aCase)/sizeof(aCase[0]); ++i){
		const DasProp* pProp = _set(&desc, DASPROP_STRING|DASPROP_SINGLE, aCase[i][0], '\0', DASPROP_DAS3);
		CHECK(pProp != NULL);
		size_t uSz = DasProp_escapeSize(pProp);
		if(aCase[i][1] == NULL){
			CHECK(uSz == 0);
			continue;
		}
		CHECK(uSz == strlen(aCase[i][1]) + 1);

		/* Exactly the size asked for, garbage filled, must come back terminated */
		memset(sBuf, 'X', sizeof(sBuf));
		const char* sOut = DasProp_xmlValue(pProp, sBuf, uSz);
		CHECK(sOut != NULL);
		CHECK(strcmp(sOut, aCase[i][1]) == 0);

		/* One byte short is refused, not overrun */
		CHECK(DasProp_xmlValue(pProp, sBuf, uSz - 1) == NULL);
	}
	DasDesc_freeProps(&desc);
	return 0;
}

/* Array properties written from numbers come back as the same numbers.  The
   csv writers under them once wrote every value over the one before, so a
   set stored only its last value. */
static int test_double_ary(void)
{
	DasDesc desc;
	memset(&desc, 0, sizeof(desc));
	DasDesc_init(&desc, DASDESC_UNK, NULL);

	double aIn[] = {1.0, 2.5, 4.0};
	CHECK(DasDesc_setDoubleArray(&desc, "lst", 3, aIn) == DAS_OKAY);
	int n = 0;
	double* pOut = DasDesc_getDoubleAry(&desc, "lst", &n);
	CHECK((pOut != NULL) && (n == 3));
	CHECK((pOut[0] == 1.0) && (pOut[1] == 2.5) && (pOut[2] == 4.0));
	free(pOut);

	float aFin[] = {0.25f, 8.0f};
	CHECK(DasDesc_setFloatAry(&desc, "flst", 2, aFin) == DAS_OKAY);
	pOut = DasDesc_getDoubleAry(&desc, "flst", &n);
	CHECK((pOut != NULL) && (n == 2));
	CHECK((pOut[0] == 0.25) && (pOut[1] == 8.0));
	free(pOut);

	/* A missing property is a das_error, NULL with a zero count here */
	n = 7;
	CHECK(DasDesc_getDoubleAry(&desc, "nope", &n) == NULL);
	CHECK(n == 0);

	DasDesc_freeProps(&desc);
	return 0;
}

int main(int argc, char** argv)
{
	(void)argc;
	setvbuf(stdout, NULL, _IONBF, 0);
	das_init(argv[0], DASERR_DIS_RET, DASLOG_ERROR, NULL);

	/* DasDesc_init() assumes zeroed storage, the way the library's calloc'd
	   objects arrive; stack garbage in the property array header crashes the
	   first set. */
	DasDesc desc;
	memset(&desc, 0, sizeof(desc));
	DasDesc_init(&desc, DASDESC_UNK, NULL);

	int nRet = 0;
	printf("INFO: ======== ERROR lines below are intentional, provoked by negative checks ========\n");
	if((nRet = test_single(&desc)) != 0) return nRet;
	if((nRet = test_range(&desc)) != 0)  return nRet;
	if((nRet = test_set(&desc)) != 0)    return nRet;
	if((nRet = test_overwrite()) != 0)   return nRet;
	if((nRet = test_limits()) != 0)      return nRet;
	if((nRet = test_refused()) != 0)     return nRet;
	if((nRet = test_alias()) != 0)       return nRet;
	if((nRet = test_live_index()) != 0)  return nRet;
	if((nRet = test_daslimit()) != 0)    return nRet;
	if((nRet = test_pkt_ytag_style()) != 0) return nRet;
	if((nRet = test_stream_ids()) != 0)  return nRet;
	if((nRet = test_write_ids()) != 0)   return nRet;
	if((nRet = test_write_order()) != 0) return nRet;
	if((nRet = test_xml_escape()) != 0)  return nRet;
	if((nRet = test_double_ary()) != 0)  return nRet;
	printf("INFO: ======== end of intentional errors, the verdict follows ========\n");

	DasDesc_freeProps(&desc);
	printf("INFO: TestDesc: all descriptor property checks passed\n");
	return 0;
}
