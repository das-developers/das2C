/** @file TestForm.c Unit tests for the DasForm formalism layer (form*.c) */

/* Author: Chris Piker <chris-piker@uiowa.edu>, via Claude Opus 5 & Fable 5.1
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

/* A formalism is Axis D: what math a variable's values obey.  
 *
 * In general, DasForm objects *compute*, their DasVar owners *walk*. So a
 * form is handed values and hands values back, and it has no generator, no 
 * array, no index and no loop.  So nothing here builds a DasVar or a DasGen
 * either.  The variable layer's tests are in TestVar.c.
 *
 * Dev Note: Keep it C99 clean: no _Static_assert, use runtime checks.
 */

#define _POSIX_C_SOURCE 200112L

#include <stdio.h>
#include <string.h>

#include <das3/core.h>
#include <das3/form.h>
#include <das3/form_linear.h>

/* A failing CHECK records the miss and lets the case keep going, so one run
   reports every broken expectation rather than only the first.  MUST is for
   the checks that cannot be survived: a null pointer the next line reads, or
   a call that leaves a struct uninitialized.  Both need an `int nErrs = 0` in
   scope, so forgetting one is a compile error rather than a silent miscount. */
#define CHECK(expr) \
	if(!(expr)){ \
		printf("ERROR: check failed at %s:%d: %s\n", __FILE__, __LINE__, #expr); \
		++nErrs; \
	}

#define MUST(expr) \
	if(!(expr)){ \
		printf("ERROR: check failed at %s:%d: %s\n", __FILE__, __LINE__, #expr); \
		return nErrs + 1; \
	}

/* Case 1: the kind table, and what an unknown kind becomes */
static int test_form_table(void)
{
	int nErrs = 0;

	const DasForm_VTbl* pPoint = das_form_lookup("point");
	MUST(pPoint != NULL);
	CHECK(strcmp(pPoint->sKind, "point") == 0);

	CHECK(das_form_lookup("linear")   != NULL);
	CHECK(das_form_lookup("vector")   != NULL);
	CHECK(das_form_lookup("geoloc")   != NULL);
	CHECK(das_form_lookup("rotation") != NULL);

	/* geovec was SPLIT into vector and geoloc; the old token must not resolve */
	CHECK(das_form_lookup("geovec") == NULL);
	CHECK(das_form_lookup("no_such_kind") == NULL);
	CHECK(das_form_lookup("") == NULL);
	CHECK(das_form_lookup(NULL) == NULL);

	/* An unrecognized kind is NOT an error: it comes back generic, carrying
	   its parameters for re-emit, which lets a stream still saying geovec
	   round-trip instead of dying. */
	const char* aGeovec[] = {"kind","geovec", "frame","TSCS", NULL};
	DasForm* pGen = new_DasForm_pairs(aGeovec);
	MUST(pGen != NULL);
	CHECK(DasForm_isKind(pGen, DAS_FORM_EXT));
	const char* sFrame = DasForm_getParam(pGen, "frame", NULL);
	MUST(sFrame != NULL);
	CHECK(strcmp(sFrame, "TSCS") == 0);
	del_DasForm(pGen);

	/* No kind= at all IS an error, and kind may legally arrive last */
	const char* aNoKind[] = {"frame","TSCS", NULL};
	CHECK(new_DasForm_pairs(aNoKind) == NULL);

	const char* aKindLast[] = {"system","spherical", "kind","vector", NULL};
	DasForm* pLast = new_DasForm_pairs(aKindLast);
	MUST(pLast != NULL);
	const char* sSys = DasForm_getParam(pLast, "system", NULL);
	MUST(sSys != NULL);
	CHECK(strcmp(sSys, "spherical") == 0);
	del_DasForm(pLast);

	/* A parameter the kind does not know is FATAL, never skipped */
	const char* aTypo[] = {"kind","vector", "sysOrder","0;1;2", NULL};
	CHECK(new_DasForm_pairs(aTypo) == NULL);

	/* Absence of <ops> is LINEAR, and that is the typed constructor's job,
	   not the wire factory's. */
	DasForm* pLin = new_DasFormLinear();
	MUST(pLin != NULL);
	CHECK(!DasForm_isKind(pLin, DAS_FORM_EXT));
	CHECK(strcmp(DasForm_kindStr(pLin), "linear") == 0);
	del_DasForm(pLin);

	return nErrs;
}

/* Case 2: a form reports its own parameters back by name, with a type.
   This is what replaced <context>: a frame is not looked up anywhere, it is
   asked for. */
static int test_form_params(void)
{
	int nErrs = 0;

	const char* aVec[] = {
		"kind","vector", "frame","TSCS", "system","spherical", "fixed","true",
		NULL
	};
	DasForm* pVec = new_DasForm_pairs(aVec);
	MUST(pVec != NULL);

	ubyte uType = 0;
	const char* sVal = DasForm_getParam(pVec, "frame", &uType);
	MUST(sVal != NULL);
	CHECK(strcmp(sVal, "TSCS") == 0);
	CHECK(uType == (DASPROP_STRING | DASPROP_SINGLE));

	sVal = DasForm_getParam(pVec, "system", &uType);
	CHECK((sVal != NULL) && (strcmp(sVal, "spherical") == 0));

	/* A decoded parameter still reads back in its WIRE spelling */
	sVal = DasForm_getParam(pVec, "fixed", &uType);
	CHECK((sVal != NULL) && (strcmp(sVal, "true") == 0));
	CHECK(uType == (DASPROP_BOOL | DASPROP_SINGLE));

	/* A miss is an answer, not an error: this is how a caller finds out
	   whether a formalism carries a frame at all. */
	CHECK(DasForm_getParam(pVec, "center", &uType) == NULL);

	/* An ellipsoidal system on a FREE vector is refused */
	const char* aDetic[] = {"kind","vector", "system","detic", NULL};
	CHECK(new_DasForm_pairs(aDetic) == NULL);

	/* validate() runs at ATTACH, not construction, so a geoloc with no body=
	   builds clean and is caught the moment it meets a shape. */
	ptrdiff_t aThree[1] = {3};
	const char* aNoBody[] = {"kind","geoloc", "system","cartesian", NULL};
	DasForm* pNoBody = new_DasForm_pairs(aNoBody);
	MUST(pNoBody != NULL);
	CHECK(DasForm_validate(pNoBody, 1, aThree) != DAS_OKAY);
	del_DasForm(pNoBody);

	/* A rotation is 9 values or 4, never 3 -- the check needing the shape */
	const char* aRot[] = {"kind","rotation", "from","A", "to","B", NULL};
	DasForm* pRot = new_DasForm_pairs(aRot);
	MUST(pRot != NULL);
	CHECK(DasForm_validate(pRot, 1, aThree) != DAS_OKAY);
	ptrdiff_t aNine[2] = {3, 3};
	CHECK(DasForm_validate(pRot, 2, aNine) == DAS_OKAY);
	del_DasForm(pRot);

	del_DasForm(pVec);

	return nErrs;
}

/* Case 3: a rotation's representation is declared, never read off the shape,
   and sysorder= places every element or none. */
static int test_form_rotation(void)
{
	int nErrs = 0;

	ptrdiff_t aFour[1]  = {4};
	ptrdiff_t aNine[2]  = {3, 3};

	/* A four element run is NOT a quaternion by itself */
	const char* aBare[] = {"kind","rotation", "from","A", "to","B", NULL};
	DasForm* pBare = new_DasForm_pairs(aBare);
	MUST(pBare != NULL);
	CHECK(DasForm_validate(pBare, 1, aFour) != DAS_OKAY);
	CHECK(strcmp(DasForm_getParam(pBare, "system", NULL), "matrix") == 0);
	CHECK(strcmp(DasForm_compSym(pBare, 0), "xx") == 0);
	CHECK(strcmp(DasForm_compSym(pBare, 5), "yz") == 0);
	CHECK(DasForm_compSym(pBare, 9) == NULL);
	del_DasForm(pBare);

	/* Scalar-last storage: symbols answer in STORAGE order, and the wire
	   spelling reads back as given.  Parameter order on the wire does not
	   matter, so sysorder= arriving before system= must work. */
	const char* aXyzw[] = {
		"kind","rotation", "from","A", "to","B",
		"sysorder","1;2;3;0", "system","quaternion", NULL
	};
	DasForm* pXyzw = new_DasForm_pairs(aXyzw);
	MUST(pXyzw != NULL);
	CHECK(DasForm_validate(pXyzw, 1, aFour) == DAS_OKAY);
	CHECK(DasForm_validate(pXyzw, 2, aNine) != DAS_OKAY);
	CHECK(strcmp(DasForm_compSym(pXyzw, 0), "x") == 0);
	CHECK(strcmp(DasForm_compSym(pXyzw, 3), "w") == 0);
	CHECK(DasForm_compSym(pXyzw, 4) == NULL);
	CHECK(strcmp(DasForm_getParam(pXyzw, "sysorder", NULL), "1;2;3;0") == 0);

	DasBuf* pBuf = new_DasBuf(256);
	MUST(pBuf != NULL);
	CHECK(pXyzw->pVTbl->encode(pXyzw, pBuf) == DAS_OKAY);
	char sOut[256] = {'\0'};
	DasBuf_read(pBuf, sOut, sizeof(sOut) - 1);
	CHECK(strstr(sOut, "system=\"quaternion\"") != NULL);
	CHECK(strstr(sOut, "sysorder=\"1;2;3;0\"") != NULL);
	del_DasBuf(pBuf);
	del_DasForm(pXyzw);

	/* A column major matrix, and its ascending twin saying nothing */
	const char* aColMaj[] = {
		"kind","rotation", "from","A", "to","B",
		"sysorder","0;3;6;1;4;7;2;5;8", NULL
	};
	DasForm* pColMaj = new_DasForm_pairs(aColMaj);
	MUST(pColMaj != NULL);
	CHECK(DasForm_validate(pColMaj, 2, aNine) == DAS_OKAY);
	CHECK(strcmp(DasForm_compSym(pColMaj, 1), "yx") == 0);
	CHECK(strcmp(DasForm_compSym(pColMaj, 2), "zx") == 0);
	del_DasForm(pColMaj);

	const char* aAsc[] = {
		"kind","rotation", "from","A", "to","B",
		"sysorder","0;1;2;3;4;5;6;7;8", NULL
	};
	DasForm* pAsc = new_DasForm_pairs(aAsc);
	MUST(pAsc != NULL);
	pBuf = new_DasBuf(256);
	MUST(pBuf != NULL);
	CHECK(pAsc->pVTbl->encode(pAsc, pBuf) == DAS_OKAY);
	memset(sOut, 0, sizeof(sOut));
	DasBuf_read(pBuf, sOut, sizeof(sOut) - 1);
	CHECK(strstr(sOut, "sysorder") == NULL);
	del_DasBuf(pBuf);
	del_DasForm(pAsc);

	/* A rotation has no defaults, so a partial order is refused at the shape
	   check, where the system's count is known.  A repeat is refused on
	   sight, whatever the system turns out to be. */
	const char* aShort[] = {
		"kind","rotation", "from","A", "to","B",
		"system","quaternion", "sysorder","1;2;3", NULL
	};
	DasForm* pShort = new_DasForm_pairs(aShort);
	MUST(pShort != NULL);
	CHECK(DasForm_validate(pShort, 1, aFour) != DAS_OKAY);
	del_DasForm(pShort);

	const char* aTwice[] = {
		"kind","rotation", "from","A", "to","B", "sysorder","0;0;2;3", NULL
	};
	CHECK(new_DasForm_pairs(aTwice) == NULL);

	const char* aEuler[] = {
		"kind","rotation", "from","A", "to","B", "system","euler", NULL
	};
	CHECK(new_DasForm_pairs(aEuler) == NULL);

	return nErrs;
}

int main(int argc, char** argv)
{
	(void)argc;

	/* Unbuffered: a crash must not discard the checks that ran before it.
	   See the same note in TestVar.c. */
	setvbuf(stdout, NULL, _IONBF, 0);

	das_init(argv[0], DASERR_DIS_RET, 0, DASLOG_ERROR, NULL);

	struct { const char* sName; int (*pFn)(void); } aCase[] = {
		{"test_form_table",    test_form_table},
		{"test_form_params",   test_form_params},
		{"test_form_rotation", test_form_rotation},
	};
	int nCase = (int)(sizeof(aCase)/sizeof(aCase[0]));

	int nBadCase = 0, nBadCheck = 0;
	printf("INFO: ======== ERROR lines below are intentional, provoked by negative checks ========\n");
	for(int i = 0; i < nCase; ++i){
		int n = aCase[i].pFn();
		if(n > 0){
			printf("ERROR: %s: %d check(s) failed\n", aCase[i].sName, n);
			++nBadCase;
			nBadCheck += n;
		}
	}
	printf("INFO: ======== end of intentional errors, the verdict follows ========\n");

	if(nBadCase > 0){
		printf("ERROR: TestForm: %d check(s) failed across %d of %d cases\n",
			nBadCheck, nBadCase, nCase);
		return 13;
	}

	printf("INFO: TestForm: all DasForm layer checks passed\n");
	return 0;
}

/* Still to write:
 *
 * 1. pack() per formalism.  Vector and geoloc pack a vtComposite datum,
 *    linear refuses at nIntRank > 0, and each answers datumType() for
 *    itself.  TestVar reaches vector pack through a composite variable;
 *    nothing reaches geoloc or rotation at all.
 * 2. binOpLeft / binOpRight directly, without a DasVarBin above them.  The
 *    affine rules are the ones worth pinning: point - point gives an
 *    interval, point + interval gives a point in both orders, point + point
 *    declines, and a unit or frame mismatch refuses.  These replace the
 *    linear and point rule tests that went with the retired das_formalism
 *    registry.
 * 3. encode(): an <ops> writer leaves a parameter off at its default
 *    (cartesian, matrix, ascending) and states it otherwise.  Case 3 covers
 *    rotation; vector and geoloc are unpinned.
 * 4. copy() gives an independent form.  Forms carry a per-variable fact (the
 *    component count validate() records), so a copy must NOT share; TestVar
 *    pins this through two variables, nothing pins it here.
 * 5. form_rot's frame-mismatch refusal and a rotation with a non ascending
 *    sysorder= applied to a vector.  The complex kind has its own file,
 *    TestCplx.c, since its arithmetic needs more room than a case here.
 */
