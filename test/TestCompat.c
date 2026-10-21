/** @file TestCompat.c Old v2.3.0 calls built through das3/das2_compat.h */

/* Author: Chris Piker <chris-piker@uiowa.edu>
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

/* Every call below is written the way a v2.3.0 program wrote it, so this
   file is the check that the compat header's adapters still map onto the
   current library.  The poison macros, which must NOT compile, are checked
   by test/das2_compat_poison_test.sh. */

#define _POSIX_C_SOURCE 200112L

#include <stdio.h>

#include <das3/das2_compat.h>
#include <das3/form_lin.h>   /* new_DasVar() needs a form, see below */

static int g_fails = 0;
#define FAIL(...) do{ printf("FAIL (line %d): ", __LINE__); printf(__VA_ARGS__); \
                      printf("\n"); ++g_fails; }while(0)

static DasErrCode onStream(StreamDesc* pSd, void* pUser)
{
	(void)pSd; (void)pUser;
	return DAS_OKAY;
}

/* ************************************************************************* */

static void test_basic(void)
{
	byte aBytes[4] = {1, 2, 3, 4};
	const ubyte* pU = aBytes;  /* byte and ubyte must stay the same type */
	if(pU[3] != 4) FAIL("byte array");

	if(DASIDX_MAX != ARYIDX_MAX) FAIL("DASIDX_MAX");
	if((DASIDX_RAGGED != -1)||(DASIDX_FUNC != -2)||(DASIDX_UNUSED != -3))
		FAIL("index flag values changed");
	if(DEGEN != DASIDX_UNUSED) FAIL("DEGEN");

	/* The globals are compound literals now, they must still act like arrays */
	ptrdiff_t aShape[DASIDX_MAX] = DASIDX_INIT_BEGIN;
	if(sizeof(g_aShapeUnused) != DASIDX_MAX*sizeof(ptrdiff_t)) FAIL("sizeof g_aShapeUnused");
	memcpy(aShape, g_aShapeUnused, sizeof(g_aShapeUnused));
	if(aShape[DASIDX_MAX - 1] != DASIDX_UNUSED) FAIL("g_aShapeUnused");
	memcpy(aShape, g_aShapeZeros, sizeof(g_aShapeZeros));
	if(aShape[3] != 0) FAIL("g_aShapeZeros");
}

static void test_values(void)
{
	das_datum dm;
	das_byteseq bs = {(const ubyte*)"abc", 3};
	if(!das_datum_byteSeq(&dm, bs, UNIT_DIMENSIONLESS)) FAIL("das_datum_byteSeq");

	das_datum_fromDbl(&dm, 2.5, UNIT_DIMENSIONLESS);
	if(das_datum_toDbl(&dm) != 2.5) FAIL("das_datum_toDbl value");

	/* The old call reported failure in-band */
	printf("INFO: An expected conversion error follows\n");
	das_datum_fromStr(&dm, "not a number");
	if(das_datum_toDbl(&dm) != DAS_FILL_VALUE) FAIL("das_datum_toDbl fill");

	if(_das_swap_float(das_swap_float(2.5f)) != 2.5f) FAIL("_das_swap_float");

	uint16_t aSwap[2] = {0x0102, 0x0304};
	_swapBufInPlace(aSwap, 2, 2);
	if((aSwap[0] != 0x0201)||(aSwap[1] != 0x0403)) FAIL("_swapBufInPlace");

	/* Not renamed, but broken in v2.3.0: it read three words past the value */
	uint32_t aU4[2] = {0x01020304, 0xA5A5A5A5};
	swapU4(aU4);
	if((aU4[0] != 0x04030201)||(aU4[1] != 0xA5A5A5A5)) FAIL("swapU4");
	if(swapFloat(das_swap_float(2.5f)) != 2.5f) FAIL("swapFloat");

	char sEsc[16];
	_das_escape_xml(sEsc, sizeof(sEsc), "a&b");
	if(strcmp(sEsc, "a&amp;b") != 0) FAIL("_das_escape_xml");

	char sJsonErr[64];
	struct das_json_parse_result_s jRes = {0};
	if(json_parse_error_info(&jRes, sJsonErr, sizeof(sJsonErr)) == NULL)
		FAIL("json_parse_error_info");
}

static void test_time(void)
{
	das_time dt;
	char sBuf[64] = {'\0'};
	if(!dt_parsetime("2026-10-21T12:34:56.789", &dt)) FAIL("dt_parsetime");
	if(strcmp(dt_isoc(sBuf, 63, &dt, 3), "2026-10-21T12:34:56.789") != 0)
		FAIL("dt_isoc: %s", sBuf);
	dt_emitt(0.0, &dt);
	if(strcmp(dt_isod(sBuf, 63, &dt, 0), "1958-001T00:00:00") != 0)
		FAIL("dt_emitt/dt_isod: %s", sBuf);
	if(dt_cmp(&dt, &dt) != 0) FAIL("dt_cmp");

	/* tt2000.h is gone from the install, the adapters keep the old calls */
	long long tt = das_utc_to_tt2K(2016.0, 12.0, 31.0, 23.0, 59.0, 60.0, 123.0, 456.0, 789.0);
	double y, mo, d, h, mi, s, ms, us, ns;
	das_tt2K_to_utc(tt, &y, &mo, &d, &h, &mi, &s, &ms, &us, &ns);
	if((y != 2016.0)||(mo != 12.0)||(d != 31.0)||(h != 23.0)||(mi != 59.0)||(s != 60.0)
	   ||(ms != 123.0)||(us != 456.0)||(ns != 789.0))
		FAIL("das_utc_to_tt2K / das_tt2K_to_utc leap second round trip");
	if(das_utc_to_tt2K(1600.0, 1.0, 1.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0) != ILLEGAL_TT2000_VALUE)
		FAIL("das_utc_to_tt2K out of range");
	if(das_tt2K_to_us2K(das_us2K_to_tt2K(1.0e12)) != 1.0e12)
		FAIL("das_us2K_to_tt2K / das_tt2K_to_us2K");
}

/* MAP_n, the old variable kinds and getDatum, plus the dataset adapters */
static void test_vars(void)
{
	das_datum dm;

	DasAry* pAry = new_DasAry("v", vtDouble, 0, NULL, RANK_1(0), UNIT_DIMENSIONLESS);
	double aVals[4] = {1.0, 2.0, 3.0, 4.0};
	DasAry_append(pAry, (const byte*)aVals, 4);

	DasGen* pGen = new_DasGenAry(pAry, MAP_2(0, DEGEN));
	DasForm* pForm = new_DasFormLinear();
	DasVar* pVar = new_DasVar(pGen, UNIT_DIMENSIONLESS, pForm);
	DasGen_decRef(pGen);
	del_DasForm(pForm);
	if(pVar == NULL){ FAIL("new_DasVar from MAP_2"); dec_DasAry(pAry); return; }

	if(DasVar_type(pVar) != D2V_ARRAY) FAIL("DasVar_type");
	if(DasVar_isComposite(pVar)) FAIL("DasVar_isComposite");
	if(DasVarAry_getArray(pVar) != pAry) FAIL("DasVarAry_getArray");
	if(ref_DasVar(pVar) < 1) FAIL("ref_DasVar");

	ptrdiff_t aLoc[DASIDX_MAX] = {2, 7};   /* j is degenerate, any value works */
	if(!DasVar_getDatum(pVar, aLoc, &dm)) FAIL("DasVar_getDatum");
	else if(das_datum_toDbl(&dm) != 3.0) FAIL("DasVar_getDatum value");

	struct das_variable* pOld = pVar; (void)pOld;
	enum var_type kind = D2V_DATUM;
	if(kind != gtConst) FAIL("D2V_DATUM");

	DasDs* pDs = new_DasDs("ds", NULL, 2);
	if(!DasDs_addAry(pDs, pAry)) FAIL("DasDs_addAry");
	DasDim* pDim = DasDs_makeDim(pDs, DASDIM_DATA, "voltage");
	if((pDim == NULL)||(strcmp(DasDim_id(pDim), "voltage") != 0))
		FAIL("DasDs_makeDim name defaults to the kind");
	if(!DasDim_addVar(pDim, DASVAR_CENTER, pVar)) FAIL("DasDim_addVar");
	if(DasDs_getDim(pDs, 0, DASDIM_DATA) != pDim) FAIL("DasDs_getDim by index");

	DasDim* pDim2 = new_DasDim("time", DASDIM_COORD, 2);
	if((pDim2 == NULL)||(strcmp(DasDim_id(pDim2), "time") != 0))
		FAIL("new_DasDim name defaults to the kind");
	if(pDim2) del_DasDim(pDim2);

	/* declared but never defined in v2.3.0, the aliases make them link */
	if((strcmp(DASVAR_MAXERR, "max_error") != 0)||(strcmp(DASVAR_MINERR, "min_error") != 0))
		FAIL("role aliases");

	del_DasDs(pDs);    /* owns the dimension and so the variable */
	dec_DasAry(pAry);
}

static void test_streams(void)
{
	StreamDescHandler fn = onStream; (void)fn;
	struct stream_descriptor* pRaw = NULL; (void)pRaw;

	StreamDesc* pSd = new_StreamDesc();
	PktDesc* pPd = StreamDesc_createPktDesc(pSd, NULL, UNIT_DIMENSIONLESS);
	if(pPd == NULL){ FAIL("StreamDesc_createPktDesc"); del_StreamDesc(pSd); return; }
	if(StreamDesc_getNPktDesc(pSd) != 1) FAIL("StreamDesc_getNPktDesc");
	int nId = DasStream_getPktId(pSd, (DasDesc*)pPd);
	if(StreamDesc_freePktDesc(pSd, nId) != DAS_OKAY) FAIL("StreamDesc_freePktDesc");
	if(StreamDesc_getNPktDesc(pSd) != 0) FAIL("StreamDesc_freePktDesc left the packet");
	del_StreamDesc(pSd);
}

static void test_creds(const char* sDir)
{
	char sFile[256];
	snprintf(sFile, 255, "%s/TestCompat.cred", sDir);

	DasCredMngr* pMgr = new_CredMngr(sFile);
	CredMngr_addUserPass(pMgr, "https://example.com", "realm", NULL, "user", "pass");
	if(!CredMngr_save(pMgr, sFile)) FAIL("CredMngr_save");
	del_CredMngr(pMgr);
	remove(sFile);
}

/* OobExcept_set() is the initializer, so each set gets its own clean */
static void test_except(void)
{
	OobExcept ex;
	struct {const char* sType; das_except_t nType;} aCase[] = {
		{DAS2_EXCEPT_NO_DATA_IN_INTERVAL, DAS_EX_NO_DATA},
		{DAS2_EXCEPT_ILLEGAL_ARGUMENT,    DAS_EX_QUERY_ERR},
		{DAS2_EXCEPT_SERVER_ERROR,        DAS_EX_SERVER_ERR},
		{EXCEPTION_UNTYPED,               DAS_EX_UNKNOWN},
		{"SomethingCustom",               DAS_EX_UNKNOWN}
	};
	for(size_t u = 0; u < sizeof(aCase)/sizeof(aCase[0]); ++u){
		OobExcept_set(&ex, aCase[u].sType, "message");
		if(ex.nType != aCase[u].nType) FAIL("OobExcept_set type '%s'", aCase[u].sType);
		OutOfBand_clean((OutOfBand*)&ex);
	}
}

/* ************************************************************************* */

int main(int argc, char** argv)
{
	das_init(argv[0], DASERR_DIS_RET, 256, DASLOG_INFO, NULL);

	/* scratch directory for the credentials file, make test passes $(BD) */
	const char* sDir = (argc > 1) ? argv[1] : ".";

	test_basic();
	test_values();
	test_time();
	test_vars();
	test_streams();
	test_creds(sDir);
	test_except();
	das_varindex_prndir(true);

	if(g_fails > 0){
		printf("ERROR: TestCompat had %d failure(s)\n", g_fails);
		return 15;
	}
	printf("INFO: All das2_compat.h adapter tests passed\n");
	return 0;
}
