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
#include <das3/form_lin.h>
#include <das3/form_pos.h>   /* alone on purpose: must pull in form_vec.h */

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
	CHECK(das_form_lookup("position") != NULL);
	CHECK(das_form_lookup("rotation") != NULL);

	/* Retired tokens must not resolve, there are no shims: geovec was SPLIT
	   into vector and position, and geoloc was renamed to position. */
	CHECK(das_form_lookup("geovec") == NULL);
	CHECK(das_form_lookup("geoloc") == NULL);
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
	/* The token that arrived, not the shared generic vtable's empty one */
	CHECK(strcmp(DasForm_kind(pGen), "geovec") == 0);
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
	CHECK(strcmp(DasForm_kind(pLin), "linear") == 0);
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
		"kind","vector", "frame","TSCS", "system","spherical", NULL
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
	const char* aPos[] = {
		"kind","position", "body","EARTH", "frame","IAU_EARTH", "fixed","true", NULL
	};
	DasForm* pPos = new_DasForm_pairs(aPos);
	MUST(pPos != NULL);
	sVal = DasForm_getParam(pPos, "fixed", &uType);
	CHECK((sVal != NULL) && (strcmp(sVal, "true") == 0));
	CHECK(uType == (DASPROP_BOOL | DASPROP_SINGLE));
	del_DasForm(pPos);

	/* A miss is an answer, not an error: this is how a caller finds out
	   whether a formalism carries a frame at all. */
	CHECK(DasForm_getParam(pVec, "body", &uType) == NULL);

	/* An ellipsoidal system on a FREE vector is refused */
	const char* aDetic[] = {"kind","vector", "system","detic", NULL};
	CHECK(new_DasForm_pairs(aDetic) == NULL);

	/* So is an origin.  Pre-3.0 streams sent body= on vectors to name the
	   frame's center; that now belongs to the frame definition. */
	const char* aVecBody[] = {"kind","vector", "frame","TSCS", "body","TS1", NULL};
	CHECK(new_DasForm_pairs(aVecBody) == NULL);

	/* And so is fixed=, which describes the frame, not the vector */
	const char* aVecFixed[] = {"kind","vector", "frame","TSCS", "fixed","true", NULL};
	CHECK(new_DasForm_pairs(aVecFixed) == NULL);

	/* validate() runs at ATTACH, not construction, so a position with no body=
	   builds clean and is caught the moment it meets a shape. */
	ptrdiff_t aThree[1] = {3};
	const char* aNoBody[] = {"kind","position", "system","cartesian", NULL};
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

/* Case 4: one component-system vocabulary for vectors and positions.  The
   lookups answer for all six; refusing the ellipsoidal two is the vector
   formalism's job, and it must do so from BOTH entry points. */
static int test_form_systems(void)
{
	int nErrs = 0;

	CHECK(das_vsys_fromStr("detic") == DAS_VSYS_DETIC);
	CHECK(das_vsys_fromStr("graphic") == DAS_VSYS_GRAPHIC);
	CHECK(das_vsys_fromStr("cart") == DAS_VSYS_CART);
	CHECK(das_vsys_fromStr("no_such_system") == DAS_VSYS_UNKNOWN);

	CHECK(strcmp(das_vsys_toStr(DAS_VSYS_GRAPHIC), "graphic") == 0);
	CHECK(das_vsys_toStr(DAS_VSYS_MAX + 1) == NULL);   /* complex's codes */
	CHECK(das_vsys_desc(DAS_VSYS_DETIC) != NULL);

	/* Graphic is latitude first, see form_vec.c for why */
	CHECK(strcmp(das_vsys_symbol(DAS_VSYS_GRAPHIC, 0), "φ") == 0);
	CHECK(strcmp(das_vsys_symbol(DAS_VSYS_DETIC, 2), "h") == 0);
	CHECK(das_vsys_symbol(DAS_VSYS_MAX + 1, 0) == NULL);
	CHECK(das_vsys_index(DAS_VSYS_DETIC, "h") == 2);

	CHECK(strcmp(das_vsys_name(DAS_VSYS_GRAPHIC, 1), "lambda") == 0);
	CHECK(das_vsys_nameIndex(DAS_VSYS_SPH, "theta") == 1);
	CHECK(das_vsys_nameIndex(DAS_VSYS_SPH, "θ") == 1);

	CHECK(das_vsys_isEllipsoidal(DAS_VSYS_DETIC));
	CHECK(!das_vsys_isEllipsoidal(DAS_VSYS_CENTRIC));

	/* No radius in the ellipsoidal systems, so no slot defaults to 1 */
	CHECK(das_vsys_default(DAS_VSYS_SPH, 0) == 1.0);
	CHECK(das_vsys_default(DAS_VSYS_DETIC, 0) == 0.0);

	/* The constructor path; the wire path is in case 2 */
	CHECK(new_DasFormVector(NULL, DAS_VSYS_DETIC, NULL) == NULL);
	DasForm* pPos = new_DasFormPos("EARTH", "IAU_EARTH", "WGS84", true,
	                               DAS_VSYS_GRAPHIC, NULL);
	MUST(pPos != NULL);
	CHECK(DasFormPos_sysType(pPos) == DAS_VSYS_GRAPHIC);
	del_DasForm(pPos);

	return nErrs;
}

/* Case 5: every kind enumerates its parameters as typed properties, so a
   binding can serialize one it has never heard of (das2C issue #38). */

/* The i'th parameter's name, value and type all match, or not */
static bool _paramEq(const DasProp* pA, const DasProp* pB)
{
	return (strcmp(DasProp_name(pA), DasProp_name(pB)) == 0) &&
	       (strcmp(DasProp_value(pA), DasProp_value(pB)) == 0) &&
	       (DasProp_type(pA) == DasProp_type(pB));
}

static int test_form_paramAt(void)
{
	int nErrs = 0;
	das_form_param_buf buf;
	const DasProp* pProp = NULL;

	/* Defaults are reported: nothing but a frame on the wire, three answers */
	const char* aVec[] = {"kind","vector", "frame","TSCS", NULL};
	DasForm* pVec = new_DasForm_pairs(aVec);
	MUST(pVec != NULL);
	ptrdiff_t aTwo[1] = {2};
	MUST(DasForm_validate(pVec, 1, aTwo) == DAS_OKAY);
	CHECK(DasForm_numParams(pVec) == 3);

	MUST((pProp = DasForm_paramAt(pVec, 0, &buf)) != NULL);
	CHECK(strcmp(DasProp_name(pProp), "frame") == 0);
	CHECK(strcmp(DasProp_value(pProp), "TSCS") == 0);
	CHECK(DasProp_type(pProp) == (DASPROP_STRING | DASPROP_SINGLE));

	MUST((pProp = DasForm_paramAt(pVec, 1, &buf)) != NULL);
	CHECK(strcmp(DasProp_name(pProp), "system") == 0);
	CHECK(strcmp(DasProp_value(pProp), "cartesian") == 0);

	/* sysorder is an integer set sized to the VARIABLE's two components, read
	   without the caller knowing what separates the items */
	MUST((pProp = DasForm_paramAt(pVec, 2, &buf)) != NULL);
	CHECK(strcmp(DasProp_name(pProp), "sysorder") == 0);
	CHECK(DasProp_type(pProp) == (DASPROP_INT | DASPROP_SET));
	int64_t aOrd[9] = {0};
	CHECK(DasProp_convertInt(pProp, aOrd, 9) == 2);
	CHECK((aOrd[0] == 0) && (aOrd[1] == 1));

	CHECK(DasForm_paramAt(pVec, 3, &buf) == NULL);
	CHECK(DasForm_paramAt(pVec, -1, &buf) == NULL);
	CHECK(DasForm_paramAt(pVec, 0, NULL) == NULL);
	del_DasForm(pVec);

	/* A frameless vector simply has one fewer, nothing reported empty */
	const char* aBare[] = {"kind","vector", NULL};
	DasForm* pBare = new_DasForm_pairs(aBare);
	MUST(pBare != NULL);
	CHECK(DasForm_numParams(pBare) == 2);
	MUST((pProp = DasForm_paramAt(pBare, 0, &buf)) != NULL);
	CHECK(strcmp(DasProp_name(pProp), "system") == 0);
	del_DasForm(pBare);

	/* A permuted order given to the constructor, not the wire.  Only the wire
	   path used to write the spelling, so this one reported 0;1;2. */
	ubyte aDirs[3] = {1, 0, 2};
	DasForm* pCon = new_DasFormVector("TSCS", DAS_VSYS_CART, aDirs);
	MUST(pCon != NULL);
	CHECK(strcmp(DasForm_getParam(pCon, "sysorder", NULL), "1;0;2") == 0);
	MUST(DasForm_validate(pCon, 1, aTwo) == DAS_OKAY);
	CHECK(strcmp(DasForm_getParam(pCon, "sysorder", NULL), "1;0") == 0);
	del_DasForm(pCon);

	/* fixed= is always answered and is a real boolean */
	const char* aPos[] = {"kind","position", "body","EARTH", "frame","IAU_EARTH", NULL};
	DasForm* pPos = new_DasForm_pairs(aPos);
	MUST(pPos != NULL);
	bool bSawFixed = false;
	for(int i = 0; (pProp = DasForm_paramAt(pPos, i, &buf)) != NULL; ++i){
		if(strcmp(DasProp_name(pProp), "fixed") != 0) continue;
		bSawFixed = true;
		CHECK(DasProp_type(pProp) == (DASPROP_BOOL | DASPROP_SINGLE));
		uint8_t uFixed = 1;
		CHECK(DasProp_convertBool(pProp, &uFixed, 1) == 1);
		CHECK(uFixed == 0);
	}
	CHECK(bSawFixed);
	del_DasForm(pPos);

	/* An unvalidated matrix rotation still knows its nine slot default */
	const char* aRot[] = {"kind","rotation", "from","A", "to","B", NULL};
	DasForm* pRot = new_DasForm_pairs(aRot);
	MUST(pRot != NULL);
	CHECK(DasForm_numParams(pRot) == 4);
	MUST((pProp = DasForm_paramAt(pRot, 3, &buf)) != NULL);
	CHECK(DasProp_convertInt(pProp, aOrd, 9) == 9);
	CHECK(aOrd[8] == 8);
	del_DasForm(pRot);

	/* A kind das2C does not know: arrival order, and STRING for everything
	   since the library is not the authority on what the values mean */
	const char* aTensor[] = {"kind","tensor", "frame","GSE", "symmetry","full", NULL};
	DasForm* pTen = new_DasForm_pairs(aTensor);
	MUST(pTen != NULL);
	CHECK(DasForm_numParams(pTen) == 2);
	MUST((pProp = DasForm_paramAt(pTen, 1, &buf)) != NULL);
	CHECK(strcmp(DasProp_name(pProp), "symmetry") == 0);
	CHECK(strcmp(DasProp_value(pProp), "full") == 0);
	CHECK(DasProp_type(pProp) == (DASPROP_STRING | DASPROP_SINGLE));
	del_DasForm(pTen);

	/* Kinds with nothing to say, and no form at all */
	const char* aPt[] = {"kind","point", NULL};
	DasForm* pPt = new_DasForm_pairs(aPt);
	MUST(pPt != NULL);
	CHECK(DasForm_numParams(pPt) == 0);
	CHECK(DasForm_paramAt(pPt, 0, &buf) == NULL);
	del_DasForm(pPt);
	CHECK(DasForm_numParams(NULL) == 0);
	CHECK(DasForm_paramAt(NULL, 0, &buf) == NULL);

	return nErrs;
}

/* Split one encode() result back into expat-style pairs, in place.  Good
   enough for what das2C writes: name="value" with no quotes inside. */
static int _encodedPairs(char* sXml, const char** psAttr, int nMax)
{
	int n = 0;
	char* p = strstr(sXml, "<ops");
	if(p == NULL) return 0;
	p += 4;
	while(n < nMax - 2){
		while(*p == ' ') ++p;
		char* pEq = strchr(p, '=');
		if((pEq == NULL)||(pEq[1] != '"')) break;
		char* pEnd = strchr(pEq + 2, '"');
		if(pEnd == NULL) break;
		*pEq = '\0';  *pEnd = '\0';
		psAttr[n++] = p;
		psAttr[n++] = pEq + 2;
		p = pEnd + 1;
	}
	psAttr[n] = NULL;
	return n;
}

/* The <ops> lines from examples/, written back out and read again.  The
   text is NOT expected to match: a stream may state a default or order its
   attributes as it likes, and encode() leaves defaults off.  What must
   survive is the meaning, i.e. the same typed parameters both times. */
static int test_form_paramTrip(void)
{
	int nErrs = 0;

	struct {
		const char* aAttr[16];
		int nRank;
		ptrdiff_t aShape[2];
	} aCase[] = {
		{{"kind","vector", "frame","CASSIOPE_NEC", "system","cartesian", "sysorder","0;1", NULL}, 1, {2, 0}},
		{{"kind","vector", "frame","TS2_ACI_REDUCED", "system","spherical", "sysorder","1", NULL}, 1, {1, 0}},
		{{"kind","vector", "frame","TSCS", NULL}, 1, {3, 0}},
		{{"kind","position", "body","Earth", "frame","IAU_EARTH", "fixed","true", "system","detic",
		  "surface","WGS84", "sysorder","1;0;2", NULL}, 1, {3, 0}},
		{{"kind","position", "body","EARTH", "frame","IAU_EARTH", "fixed","true", "system","spherical", NULL}, 1, {3, 0}},
		{{"kind","rotation", "from","TSCS", "to","GEI2000", NULL}, 2, {3, 3}},
		{{"kind","rotation", "from","TSCS", "to","GEI2000", "system","quaternion", "sysorder","1;2;3;0", NULL}, 1, {4, 0}},
		{{"kind","complex", "system","polar", NULL}, 1, {2, 0}},
		{{"kind","tensor", "frame","GSE", "symmetry","full", NULL}, 2, {3, 3}},
		{{"kind","point", NULL}, 0, {0, 0}},
	};
	int nCase = (int)(sizeof(aCase)/sizeof(aCase[0]));

	for(int c = 0; c < nCase; ++c){
		DasForm* pIn = new_DasForm_pairs(aCase[c].aAttr);
		MUST(pIn != NULL);
		CHECK(DasForm_validate(pIn, aCase[c].nRank, aCase[c].aShape) == DAS_OKAY);

		DasBuf* pBuf = new_DasBuf(512);
		MUST(pBuf != NULL);
		CHECK(pIn->pVTbl->encode(pIn, pBuf) == DAS_OKAY);
		char sXml[512] = {'\0'};
		DasBuf_read(pBuf, sXml, sizeof(sXml) - 1);
		del_DasBuf(pBuf);

		const char* aOut[32];
		_encodedPairs(sXml, aOut, 32);
		DasForm* pOut = new_DasForm_pairs(aOut);
		MUST(pOut != NULL);
		CHECK(DasForm_validate(pOut, aCase[c].nRank, aCase[c].aShape) == DAS_OKAY);

		int nIn = DasForm_numParams(pIn);
		if(nIn != DasForm_numParams(pOut)){
			printf("ERROR: case %d (%s): %d parameters in, %d back\n", c,
				aCase[c].aAttr[1], nIn, DasForm_numParams(pOut));
			++nErrs;
		}
		das_form_param_buf bufIn, bufOut;
		for(int i = 0; i < nIn; ++i){
			const DasProp* pA = DasForm_paramAt(pIn, i, &bufIn);
			const DasProp* pB = DasForm_paramAt(pOut, i, &bufOut);
			if((pA == NULL)||(pB == NULL)||!_paramEq(pA, pB)){
				printf("ERROR: case %d (%s) parameter %d: %s=%s back as %s=%s\n",
					c, aCase[c].aAttr[1], i,
					pA ? DasProp_name(pA) : "(none)", pA ? DasProp_value(pA) : "",
					pB ? DasProp_name(pB) : "(none)", pB ? DasProp_value(pB) : "");
				++nErrs;
			}
		}
		del_DasForm(pOut);
		del_DasForm(pIn);
	}
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
		{"test_form_systems",  test_form_systems},
		{"test_form_paramAt",  test_form_paramAt},
		{"test_form_paramTrip", test_form_paramTrip},
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
 * 1. pack() per formalism.  Vector and position pack a vtComposite datum,
 *    linear refuses at nIntRank > 0, and each answers datumType() for
 *    itself.  TestVar reaches vector pack through a composite variable;
 *    nothing reaches position or rotation at all.
 * 2. binOpLeft / binOpRight directly, without a DasVarBin above them.  The
 *    affine rules are the ones worth pinning: point - point gives an
 *    interval, point + interval gives a point in both orders, point + point
 *    declines, and a unit or frame mismatch refuses.  These replace the
 *    linear and point rule tests that went with the retired das_formalism
 *    registry.
 * 3. encode(): an <ops> writer leaves a parameter off at its default
 *    (cartesian, matrix, ascending) and states it otherwise.  Case 3 covers
 *    rotation; vector and position are unpinned.
 * 4. copy() gives an independent form.  Forms carry a per-variable fact (the
 *    component count validate() records), so a copy must NOT share; TestVar
 *    pins this through two variables, nothing pins it here.
 * 5. form_rot's frame-mismatch refusal and a rotation with a non ascending
 *    sysorder= applied to a vector.  The complex kind has its own file,
 *    TestCplx.c, since its arithmetic needs more room than a case here.
 */
