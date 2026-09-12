/* Author: C. Piker, via Claude Opus 5
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

/* Unit tests for DasDim (das3/dimension.h).
 *
 * A dimension is mostly a small keyed container, so most of what is worth
 * testing is its BOOKKEEPING: the role names it holds beside each set, and
 * whether that bookkeeping stays consistent with the parent pointers the
 * sets carry.  Those two halves are written in different places and can come
 * apart, which is the class of bug these cases are aimed at.
 *
 * The value-carrying behavior (shape merging, iteration) is exercised
 * against real streams by TestDs and TestV3Read; this file stays on the
 * container semantics that no stream fixture would notice were broken.
 */

#include <stdio.h>
#include <string.h>

#include <das3/core.h>
#include <das3/variable.h>
#include <das3/form_linear.h>   /* core.h does not pull the formalisms in */

#define CHECK(expr) \
	if(!(expr)){ \
		printf("ERROR: check failed at %s:%d: %s\n", __FILE__, __LINE__, #expr); \
		return 13; \
	}

/* Both DasDim_getRoleByIdx() and DasVar_role() can legitimately answer NULL,
   and strcmp() may not be handed one -- gcc's -Werror=nonnull catches it,
   which is the same shape as the das3_cdf NULL-into-strcmp fixed this pass. */
static bool _roleIs(const char* sRole, const char* sWant)
{
	return (sRole != NULL) && (strcmp(sRole, sWant) == 0);
}

/* A minimal set to hang on a dimension; the values do not matter here. */
static DasVar* _mkSet(DasAry* pAry)
{
	int8_t aMap[1] = { 0 };
	DasGen* pGen = new_DasGenAry(pAry, 1, aMap);
	if(pGen == NULL) return NULL;

	/* Both heap arguments are ours until we drop them: the constructor adds
	   its own reference to each.  A formalism is required -- a numeric
	   variable always has one, and linear is what an absent <ops> means. */
	DasForm* pForm = new_DasFormLinear();
	DasVar* pVar = new_DasVar(pGen, UNIT_NT, pForm);
	del_DasForm(pForm);
	DasGen_decRef(pGen);
	return pVar;
}

static DasAry* _mkAry(const char* sId)
{
	float rFill = -1.0f;
	DasAry* pAry = new_DasAry(sId, vtFloat, 0, (const ubyte*)&rFill, RANK_1(0), UNIT_NT);
	if(pAry == NULL) return NULL;
	float aVals[4] = {1.0f, 2.0f, 3.0f, 4.0f};
	DasAry_append(pAry, (const ubyte*)aVals, 4);
	return pAry;
}

/* Construction and the plain accessors */
static int test_dim_basics(void)
{
	DasDim* pDim = new_DasDim("B_GSM", "B_GSM_avg", DASDIM_DATA, 1);
	CHECK(pDim != NULL);
	CHECK(strcmp(DasDim_id(pDim), "B_GSM_avg") == 0);
	CHECK(DasDim_numVars(pDim) == 0);
	CHECK(DasDim_getVar(pDim, DASVAR_CENTER) == NULL);
	CHECK(DasDim_getPointVar(pDim) == NULL);   /* nothing to point at yet */
	del_DasDim(pDim);
	return 0;
}

/* What may and may not be registered.  Every refusal here is what keeps
   "a set with a parent dimension has a role in it" true, which DasVar_role()
   relies on. */
static int test_dim_addvar_guards(void)
{
	DasAry* pAry = _mkAry("vals");
	CHECK(pAry != NULL);
	DasDim* pDim = new_DasDim("B_mag", "B_mag", DASDIM_DATA, 1);
	CHECK(pDim != NULL);

	DasVar* pA = _mkSet(pAry);  CHECK(pA != NULL);
	DasVar* pB = _mkSet(pAry);  CHECK(pB != NULL);

	/* an unnamed role would produce a parented set with no role */
	CHECK(!DasDim_addVar(pDim, NULL, pA));
	CHECK(!DasDim_addVar(pDim, "", pA));
	CHECK(!DasDim_addVar(pDim, DASVAR_CENTER, NULL));
	CHECK(DasDim_numVars(pDim) == 0);
	CHECK(DasVar_role(pA) == NULL);   /* untouched, still standalone */

	CHECK(DasDim_addVar(pDim, DASVAR_CENTER, pA));
	CHECK(DasDim_numVars(pDim) == 1);

	/* roles are unique, and the comparison is case insensitive, so a second
	   spelling of the same role is still the same role */
	CHECK(!DasDim_addVar(pDim, DASVAR_CENTER, pB));
	CHECK(!DasDim_addVar(pDim, "CeNtEr", pB));
	CHECK(DasDim_numVars(pDim) == 1);

	CHECK(DasDim_getVar(pDim, DASVAR_CENTER) == pA);
	CHECK(_roleIs(DasDim_getRoleByIdx(pDim, 0), DASVAR_CENTER));
	CHECK(DasDim_getRoleByIdx(pDim, 1) == NULL);   /* past the end */

	CHECK(dec_DasVar(pB) == 0);   /* never adopted, so mine to release */
	del_DasDim(pDim);                /* takes pA down with it */
	dec_DasAry(pAry);
	return 0;
}

/* The point variable is a preference walk, not a lookup: center first, then
   the three statistics that can stand in for it. */
static int test_dim_pointvar_order(void)
{
	DasAry* pAry = _mkAry("vals");
	CHECK(pAry != NULL);

	/* mode alone answers */
	DasDim* pDim = new_DasDim("B", "B", DASDIM_DATA, 1);
	DasVar* pMode = _mkSet(pAry);
	CHECK(DasDim_addVar(pDim, DASVAR_MODE, pMode));
	CHECK(DasDim_getPointVar(pDim) == pMode);

	/* median outranks mode */
	DasVar* pMedian = _mkSet(pAry);
	CHECK(DasDim_addVar(pDim, DASVAR_MEDIAN, pMedian));
	CHECK(DasDim_getPointVar(pDim) == pMedian);

	/* mean outranks median */
	DasVar* pMean = _mkSet(pAry);
	CHECK(DasDim_addVar(pDim, DASVAR_MEAN, pMean));
	CHECK(DasDim_getPointVar(pDim) == pMean);

	/* and center outranks everything */
	DasVar* pCenter = _mkSet(pAry);
	CHECK(DasDim_addVar(pDim, DASVAR_CENTER, pCenter));
	CHECK(DasDim_getPointVar(pDim) == pCenter);

	/* a dimension of bounds alone has no point variable: min/max could imply
	   one, but the library does not invent it here */
	DasDim* pBounds = new_DasDim("range", "range", DASDIM_COORD, 1);
	DasVar* pMin = _mkSet(pAry);
	DasVar* pMax = _mkSet(pAry);
	CHECK(DasDim_addVar(pBounds, DASVAR_MIN, pMin));
	CHECK(DasDim_addVar(pBounds, DASVAR_MAX, pMax));
	CHECK(DasDim_getPointVar(pBounds) == NULL);

	del_DasDim(pBounds);
	del_DasDim(pDim);
	dec_DasAry(pAry);
	return 0;
}

/* Removal has to undo BOTH halves of registration: the dim's list and the
   set's parent pointer.  Leaving the parent behind creates a set that claims
   a dimension which no longer accounts for it. */
static int test_dim_popvar(void)
{
	DasAry* pAry = _mkAry("vals");
	CHECK(pAry != NULL);
	DasDim* pDim = new_DasDim("time", "time", DASDIM_COORD, 1);
	CHECK(pDim != NULL);

	DasVar* pRef    = _mkSet(pAry);
	DasVar* pCenter = _mkSet(pAry);
	DasVar* pOffset = _mkSet(pAry);
	CHECK(DasDim_addVar(pDim, DASVAR_REF, pRef));
	CHECK(DasDim_addVar(pDim, DASVAR_CENTER, pCenter));
	CHECK(DasDim_addVar(pDim, DASVAR_OFFSET, pOffset));
	CHECK(DasDim_numVars(pDim) == 3);

	/* popping a role that isn't there changes nothing */
	CHECK(DasDim_popVar(pDim, DASVAR_WIDTH) == NULL);
	CHECK(DasDim_numVars(pDim) == 3);

	/* pop from the MIDDLE, which is the case that has to shift the arrays */
	CHECK(DasDim_popVar(pDim, DASVAR_CENTER) == pCenter);
	CHECK(DasDim_numVars(pDim) == 2);
	CHECK(DasDim_getVar(pDim, DASVAR_CENTER) == NULL);

	/* the survivors keep their pairing: this is what a bad shift breaks */
	CHECK(DasDim_getVar(pDim, DASVAR_REF) == pRef);
	CHECK(DasDim_getVar(pDim, DASVAR_OFFSET) == pOffset);
	CHECK(_roleIs(DasDim_getRoleByIdx(pDim, 0), DASVAR_REF));
	CHECK(_roleIs(DasDim_getRoleByIdx(pDim, 1), DASVAR_OFFSET));
	CHECK(DasDim_getRoleByIdx(pDim, 2) == NULL);
	CHECK(_roleIs(DasVar_role(pRef), DASVAR_REF));
	CHECK(_roleIs(DasVar_role(pOffset), DASVAR_OFFSET));

	/* The popped set is standalone again, NOT parented-but-roleless.  Assert
	   the parent pointer directly: DasVar_role() answers NULL for BOTH states
	   (it reports the corrupt one and then returns NULL too), so asking it
	   alone cannot tell a released set from a stranded one. */
	CHECK(DasDesc_parent((DasDesc*)pCenter) == NULL);
	CHECK(DasVar_role(pCenter) == NULL);

	/* and it can be re-registered, which is what the das2.2 up-convert does
	   when it turns an old center variable into a reference variable */
	CHECK(DasDim_addVar(pDim, DASVAR_MEAN, pCenter));
	CHECK(_roleIs(DasVar_role(pCenter), DASVAR_MEAN));

	del_DasDim(pDim);
	dec_DasAry(pAry);
	return 0;
}

/* The role vocabulary.  Roles are free strings; this only reports which ones
   the library will act on rather than merely carry. */
static int test_dim_known_roles(void)
{
	CHECK(DasDim_isKnownRole(DASVAR_CENTER));
	CHECK(DasDim_isKnownRole(DASVAR_MIN));
	CHECK(DasDim_isKnownRole(DASVAR_MAX));
	CHECK(DasDim_isKnownRole(DASVAR_WIDTH));
	CHECK(DasDim_isKnownRole(DASVAR_MEAN));
	CHECK(DasDim_isKnownRole(DASVAR_MEDIAN));
	CHECK(DasDim_isKnownRole(DASVAR_MODE));
	CHECK(DasDim_isKnownRole(DASVAR_REF));
	CHECK(DasDim_isKnownRole(DASVAR_OFFSET));
	CHECK(DasDim_isKnownRole(DASVAR_MAX_ERR));
	CHECK(DasDim_isKnownRole(DASVAR_MIN_ERR));
	CHECK(DasDim_isKnownRole(DASVAR_STD_DEV));
	CHECK(DasDim_isKnownRole(DASVAR_COUNT));
	CHECK(DasDim_isKnownRole(DASVAR_WEIGHT));
	CHECK(DasDim_isKnownRole(DASVAR_NORM));

	CHECK(!DasDim_isKnownRole("not_a_role"));
	CHECK(!DasDim_isKnownRole(""));

	/* Case sensitive: the DASVAR_* values are all lower case, and addVar's
	   duplicate test is case INSENSITIVE, so the two disagree on purpose
	   until someone rules otherwise. */
	CHECK(!DasDim_isKnownRole("Center"));

	/* An unknown role is legal, just inert.  This is the free-form half of
	   the contract, and it is what makes the check worth having. */
	DasAry* pAry = _mkAry("vals");
	CHECK(pAry != NULL);
	DasDim* pDim = new_DasDim("odd", "odd", DASDIM_DATA, 1);
	DasVar* pCustom = _mkSet(pAry);
	CHECK(DasDim_addVar(pDim, "my_own_role", pCustom));
	CHECK(_roleIs(DasVar_role(pCustom), "my_own_role"));
	CHECK(DasDim_getVar(pDim, "my_own_role") == pCustom);
	CHECK(DasDim_getPointVar(pDim) == NULL);   /* stored, never chosen */
	del_DasDim(pDim);
	dec_DasAry(pAry);

	/* Liberal in, canonical out: the legacy wire spelling becomes the name
	   the library uses, and anything else passes through untouched. */
	CHECK(_roleIs(das_role_fromStr("average"), DASVAR_MEAN));
	CHECK(_roleIs(das_role_fromStr(DASVAR_MEAN), DASVAR_MEAN));
	CHECK(_roleIs(das_role_fromStr("my_own_role"), "my_own_role"));
	CHECK(das_role_fromStr(NULL) == NULL);

	/* Retired names must not creep back in: 'uncertainty' left the library
	   because it names no defined quantity, and 'point_spread' is withheld
	   until a formalism can interpret it. */
	CHECK(!DasDim_isKnownRole("uncertainty"));
	CHECK(!DasDim_isKnownRole("point_spread"));
	return 0;
}

int main(int argc, char** argv)
{
	(void)argc;
	/* Unbuffered, so the library's stderr lines land between the banners */
	setvbuf(stdout, NULL, _IONBF, 0);

	das_init(argv[0], DASERR_DIS_RET, 0, DASLOG_ERROR, NULL);

	int nRet = 0;
	printf("INFO: ======== ERROR lines below are intentional, provoked by negative checks ========\n");
	if((nRet = test_dim_basics()) != 0)          return nRet;
	if((nRet = test_dim_addvar_guards()) != 0)   return nRet;
	if((nRet = test_dim_pointvar_order()) != 0)  return nRet;
	if((nRet = test_dim_popvar()) != 0)          return nRet;
	if((nRet = test_dim_known_roles()) != 0)     return nRet;
	printf("INFO: ======== end of intentional errors, the verdict follows ========\n");

	printf("INFO: TestDim: all DasDim container checks passed\n");
	return 0;
}

/* Still to write:
 *
 * 1. DasDim_shape / lengthIn / degenerate merges.  TestDs covers lengthIn
 *    over ex12 and ex19 only; nothing pins the iFirstInternal masking rule
 *    in DasDim_shape directly, and ex40/ex41 now carry the internal rank a
 *    case would need.
 * 2. DasDim_encode round trip at the unit level.  The das3_text golden pairs
 *    exercise it end to end, but nothing here checks one dimension's output
 *    against its input.  While there: the function has no declaration in
 *    dimension.h (dataset.c carries a local prototype) and its DasBuf_printf
 *    returns go unchecked.
 * 3. DASDIM_MAXVAR overflow.  Needs 16 sets to reach, so it wants a loop
 *    rather than the hand-built dimensions above.
 */
