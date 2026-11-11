/** @file TestDatum.c Unit tests for the DasDatum layer (datum.c) */

/* Author: Chris Piker <chris-piker@uiowa.edu>, via Claude Opus 5
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

#define _POSIX_C_SOURCE 200112L

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>

#include <das3/core.h>
#include <das3/uri.h>
#include <das3/form_vec.h>   /* core.h stops at the generic layer */
#include <das3/var_p.h>   /* tests link the static archive, so they may */

static int g_fails = 0;
#define FAIL(...) do{ printf("FAIL (line %d): ", __LINE__); printf(__VA_ARGS__); \
                      printf("\n"); ++g_fails; }while(0)

/* ************************************************************************* */
/* DasDatum_shape: total over every datum type.

   A function that only answers for some of its inputs is a trap, so each type
   is checked here even where the answer is trivially 0. */

static void test_shape_total(void)
{
	ptrdiff_t aShape[DASIDX_MAX];
	DasDatum dm;

	/* a simple value is rank 0 and writes nothing */
	DasDatum_fromDouble(&dm, 3.0, DASUNITS_DIMENSIONLESS);
	if(DasDatum_shape(&dm, aShape) != 0) FAIL("double rank");
	if(DasDatum_numElems(&dm) != 1)        FAIL("double nElems");
	if(DasDatum_elemsBytes(&dm) != 0)      FAIL("a scalar needs no caller storage");

	/* text is rank 1 and reports exactly what strlen would.  The NUL that
	   DASARY_AS_STRING puts in an array is das2C's storage choice, so the
	   backing array reports one MORE than this does. */
	DasDatum_wrapStr(&dm, "hello", DASUNITS_DIMENSIONLESS);
	if(DasDatum_shape(&dm, aShape) != 1) FAIL("text rank");
	if(aShape[0] != 5) FAIL("text extent = %td, wanted strlen 5", aShape[0]);

	DasDatum_wrapStr(&dm, "", DASUNITS_DIMENSIONLESS);
	if(DasDatum_shape(&dm, aShape) != 1) FAIL("empty text rank");
	if(aShape[0] != 0) FAIL("empty text extent = %td", aShape[0]);

	/* a byte sequence carries its own length */
	uint8_t aRaw[24];
	DasCByteSeq bs; bs.data = aRaw; bs.size = 24;
	DasDatum_wrapBytes(&dm, bs, DASUNITS_DIMENSIONLESS);
	if(DasDatum_shape(&dm, aShape) != 1) FAIL("byteseq rank");
	if(aShape[0] != 24) FAIL("byteseq extent = %td", aShape[0]);

	/* rank only, for a caller that has no array to spare */
	if(DasDatum_shape(&dm, NULL) != 1) FAIL("NULL pShape rejected");
}

/* ************************************************************************* */
/* DasDatum_wrapStr and DasDatum_set: two argument orders the compiler
   cannot check for you.

   DasUnits is a const char*, so wrapStr's (datum, sStr, units) and
   (datum, units, sStr) are the SAME C signature -- a swap between header and
   implementation compiles clean and silently trades the two.  Only a value
   check catches it. */

static void test_datum_ctors(void)
{
	ptrdiff_t aShape[DASIDX_MAX];
	DasDatum dm;

	DasDatum_wrapStr(&dm, "meters", DASUNITS_DIMENSIONLESS);
	if(dm.units != DASUNITS_DIMENSIONLESS)
		FAIL("wrapStr took its value as the units");
	DasDatum_shape(&dm, aShape);
	if(aShape[0] != 6) FAIL("wrapStr stored the units as its value");

	/* DasDatum_set must copy a byteseq's pointer AND its length; copying
	   only the pointer leaves a size nothing downstream can check */
	uint8_t aRaw[24];
	DasCByteSeq bs; bs.data = aRaw; bs.size = 24;
	DasDatum_set(&dm, (const uint8_t*)&bs, DASVT_BYTESEQ, sizeof(DasCByteSeq),
	               DASUNITS_DIMENSIONLESS);
	if(DasDatum_shape(&dm, aShape) != 1) FAIL("init byteseq rank");
	if(aShape[0] != 24) FAIL("init truncated the byteseq to its pointer");

	/* Hex rendering: two digits per byte, every byte, the separator between. */
	uint8_t aPng[4] = {0x89, 0x50, 0x4E, 0x0A};
	bs.data = aPng; bs.size = 4;
	DasDatum_wrapBytes(&dm, bs, DASUNITS_DIMENSIONLESS);
	char sHex[32] = {'\0'};
	DasDatum_toValStrSep(&dm, -1, ", ", sHex, sizeof(sHex));
	if(strcmp(sHex, "89, 50, 4E, 0A") != 0) FAIL("hex bytes rendered as '%s'", sHex);
	DasDatum_toValStrSep(&dm, -1, ";", sHex, 8);
	if(strcmp(sHex, "89;50") != 0) FAIL("short buffer cut mid-byte: '%s'", sHex);
}

/* ************************************************************************* */
/* DasDatum_isLocal: does the datum own its bytes?

   The predicate any interface must check before keeping a datum past the call
   it arrived on. */

static void test_islocal(void)
{
	DasDatum dm;

	DasDatum_fromDouble(&dm, 1.0, DASUNITS_DIMENSIONLESS);
	if(!DasDatum_isLocal(&dm)) FAIL("a double is local");

	DasDatum_wrapStr(&dm, "x", DASUNITS_DIMENSIONLESS);
	if(DasDatum_isLocal(&dm)) FAIL("text is a reference, not local");

	uint8_t aRaw[4];
	DasCByteSeq bs; bs.data = aRaw; bs.size = 4;
	DasDatum_wrapBytes(&dm, bs, DASUNITS_DIMENSIONLESS);
	if(DasDatum_isLocal(&dm)) FAIL("a byteseq is a reference, not local");

	/* and the enforcement it was written for: a DasRng outlives the call
	   that fills it, so it may only hold datums that own their bytes */
	DasRng rng;
	DasDatum dmA, dmB;
	DasDatum_fromDouble(&dmA, 1.0, DASUNITS_SECONDS);
	DasDatum_fromDouble(&dmB, 2.0, DASUNITS_SECONDS);
	if(DasRng_fromDatum(&rng, &dmA, &dmB) != DAS_OKAY)
		FAIL("a local pair was refused");

	DasDatum_wrapStr(&dmB, "not mine", DASUNITS_DIMENSIONLESS);
	if(DasRng_fromDatum(&rng, &dmA, &dmB) == DAS_OKAY)
		FAIL("a range retained a reference datum");
}

/* ************************************************************************* */
/* The null datum: DASVT_NULL, printed as "" and parsed back from "" */

static void test_null(void)
{
	DasDatum dm;
	memset(&dm, 0, sizeof(dm));
	if(!DasDatum_isNull(&dm)) FAIL("zeroed memory is a legal null");

	DasDatum_fromDouble(&dm, 1.0, DASUNITS_SECONDS);
	DasDatum_setNull(&dm);
	if(!DasDatum_isNull(&dm))               FAIL("initNull did not make a null");
	if(dm.vsize != 0)                        FAIL("null vsize = %u", dm.vsize);
	if(dm.units != DASUNITS_DIMENSIONLESS)       FAIL("null units are not dimensionless");

	DasDatum_fromDouble(&dm, 0.0, DASUNITS_DIMENSIONLESS);
	if(DasDatum_isNull(&dm)) FAIL("a zero double is a value, not null");

	char sBuf[32];
	memset(sBuf, 'x', sizeof(sBuf));
	DasDatum_setNull(&dm);
	if(DasDatum_toStr(&dm, -1, sBuf, sizeof(sBuf)) == NULL) FAIL("null did not print");
	if(sBuf[0] != '\0') FAIL("null printed as '%s'", sBuf);

	DasDatum_fromDouble(&dm, 1.0, DASUNITS_SECONDS);
	if(!DasDatum_fromStr(&dm, ""))   FAIL("\"\" is parseable");
	if(!DasDatum_isNull(&dm))        FAIL("\"\" did not parse to null");
	DasDatum_fromDouble(&dm, 1.0, DASUNITS_SECONDS);
	if(!DasDatum_fromStr(&dm, " \t")) FAIL("whitespace is parseable");
	if(!DasDatum_isNull(&dm))         FAIL("whitespace did not parse to null");

	/* Null owns nothing, so it is local; DASVT_UNK boxes a pointer, so it is
	   neither null nor local */
	if(!DasDatum_isLocal(&dm)) FAIL("null is local");
	void* pSrc = &dm;
	DasDatum_set(&dm, (const uint8_t*)&pSrc, DASVT_UNK, sizeof(void*), DASUNITS_DIMENSIONLESS);
	if(DasDatum_isNull(&dm))   FAIL("a DASVT_UNK datum is not null");
	if(DasDatum_isLocal(&dm))  FAIL("a DASVT_UNK datum is a reference, not local");

	/* init with DASVT_NULL ignores the source, whatever it points at */
	double rSrc = 5.0;
	DasDatum_set(&dm, (const uint8_t*)&rSrc, DASVT_NULL, 8, DASUNITS_SECONDS);
	if(!DasDatum_isNull(&dm)) FAIL("init with DASVT_NULL is not null");

	/* null has no value to convert; each refuses through daserrf, which
	   DASERRDISP_RET turns into a false return here */
	double rOut = 7.0;
	DasTime dt;
	if(DasDatum_toDouble(&dm, &rOut)) FAIL("null converted to a double");
	if(rOut != 7.0)                 FAIL("toDbl wrote through on null");
	if(DasDatum_toTime(&dm, &dt))  FAIL("null converted to a time");
	if(DasDatum_toPoint(&dm, DASUNITS_T2000, &rOut)) FAIL("null converted to an epoch");
}

/* ************************************************************************* */
/* DasDatum_fromTime: the inverse of toTime, yday recomputed, leap second kept */

static void test_from_time(void)
{
	DasDatum dm;
	DasTime dtIn = {0}, dtOut = {0};
	dtIn.year = 2016; dtIn.month = 12; dtIn.mday = 31;
	dtIn.hour = 23;   dtIn.minute = 59; dtIn.second = 60.5;

	if(!DasDatum_fromTime(&dm, &dtIn)) FAIL("fromTime refused a leap second");
	if(dm.vt != DASVT_TIME)                 FAIL("fromTime type is %d", dm.vt);
	if(dm.units != DASUNITS_UTC)            FAIL("fromTime units are not UTC");
	if(!DasDatum_toTime(&dm, &dtOut))  FAIL("toTime refused a fromTime datum");
	if(dtOut.yday != 366)               FAIL("yday = %d, wanted 366", dtOut.yday);
	if((dtOut.minute != 59)||(dtOut.second != 60.5))
		FAIL("leap second rolled to %02d:%06.3f", dtOut.minute, dtOut.second);

	if(DasDatum_fromTime(&dm, NULL)) FAIL("fromTime took a NULL DasTime");
	if(!DasDatum_isNull(&dm))        FAIL("fromTime NULL did not leave a null");
}

/* ************************************************************************* */
/* DasRng open ends: a null end is no bound on that side */

static void test_range_open(void)
{
	DasRng rng;

	memset(&rng, 0, sizeof(rng));
	if(DasRng_hasBeg(&rng) || DasRng_hasEnd(&rng))
		FAIL("a zeroed range is open at both ends");

	DasRng_fromInt(&rng, 1, 2);
	DasRng_setOpen(&rng);
	if(DasRng_hasBeg(&rng) || DasRng_hasEnd(&rng))
		FAIL("init left an end bounded");
	if(rng.begin.units != DASUNITS_DIMENSIONLESS)    FAIL("init made improper nulls");

	/* text: "" is open, anything else must be a time */
	if(DasRng_fromIso(&rng, "2025-288", "") != DAS_OKAY) FAIL("fromUtc refused an open end");
	if(!DasRng_hasBeg(&rng) || DasRng_hasEnd(&rng))   FAIL("fromUtc \"\" end is not open");
	if(rng.begin.vt != DASVT_TIME)                                FAIL("fromUtc begin is not a time");
	if(DasRng_fromIso(&rng, "", "2025-290") != DAS_OKAY) FAIL("fromUtc refused an open begin");
	if(DasRng_hasBeg(&rng) || !DasRng_hasEnd(&rng))   FAIL("fromUtc \"\" begin is not open");
	if(DasRng_fromIso(&rng, "", "") != DAS_OKAY)         FAIL("fromUtc refused all of time");
	if(DasRng_fromIso(&rng, "2025-288", "dogs") == DAS_OKAY) FAIL("fromUtc took 'dogs'");
	if(DasRng_fromIso(&rng, "2.5", "2025-290") == DAS_OKAY)  FAIL("fromUtc took a number");

	/* DasTime: NULL is open */
	DasTime dt = {0};
	dt.year = 2025; dt.month = 10; dt.mday = 15;
	DasRng_fromTime(&rng, &dt, NULL);
	if(!DasRng_hasBeg(&rng) || DasRng_hasEnd(&rng))  FAIL("fromTime NULL end is not open");
	if(((DasTime*)&rng.begin)->yday != 288)                FAIL("fromTime did not normalize yday");

	/* datums: NULL pointer and null datum are both open */
	DasDatum dmBeg, dmNull;
	DasDatum_fromDouble(&dmBeg, 3.0, DASUNITS_DIMENSIONLESS);
	DasDatum_setNull(&dmNull);
	if(DasRng_fromDatum(&rng, &dmBeg, &dmNull) != DAS_OKAY)
		FAIL("fromDatum refused a null end");
	if(!DasRng_hasBeg(&rng) || DasRng_hasEnd(&rng)) FAIL("fromDatum null end is not open");
	if(DasRng_fromDatum(&rng, NULL, &dmBeg) != DAS_OKAY)
		FAIL("fromDatum refused a NULL begin");
	if(DasRng_hasBeg(&rng) || !DasRng_hasEnd(&rng)) FAIL("fromDatum NULL begin is not open");

	/* fromInt is always bounded */
	DasRng_fromInt(&rng, 5, 2);
	if(!DasRng_hasBeg(&rng) || !DasRng_hasEnd(&rng)) FAIL("fromInt left an end open");
}

/* ************************************************************************* */
/* DasDimRng: a range plus the dimension it constrains */

static void test_dim_range(void)
{
	DasRng rng;
	DasRng_fromInt(&rng, 3, 7);

	DasDimRng dr;
	if(DasDimRng_set(&dr, "SCLK.Mod64k", &rng) != DAS_OKAY) FAIL("init refused a name");
	if(strcmp(dr.physDim, "sclk.mod64k") != 0)  FAIL("init kept the case: %s", dr.physDim);
	if(!DasRng_hasBeg(&dr.range) || !DasRng_hasEnd(&dr.range)) FAIL("init lost the bounds");

	/* the range comes first, so the cast works */
	if((const DasRng*)&dr != &dr.range) FAIL("DasDimRng does not start with its range");

	if(DasDimRng_set(&dr, "time", NULL) != DAS_OKAY) FAIL("init refused NULL bounds");
	if(DasRng_hasBeg(&dr.range) || DasRng_hasEnd(&dr.range)) FAIL("NULL bounds are not open");

	/* 31 + '.' + 31 fits, 79 does not */
	char sQual[64];
	memset(sQual, 'a', 63); sQual[31] = '.'; sQual[63] = '\0';
	if(DasDimRng_set(&dr, sQual, NULL) != DAS_OKAY) FAIL("a 63 char dim.field was refused");
	char sLong[80];
	memset(sLong, 'x', sizeof(sLong) - 1); sLong[sizeof(sLong) - 1] = '\0';
	if(DasDimRng_set(&dr, sLong, NULL) == DAS_OKAY) FAIL("init took a 79 char name");
}

/* ************************************************************************* */
/* DasDatum_wrapElems: the DASVT_COMP box.

   Extents ride inside uInfo when they fit and behind pShape when they do not.
   DasDatum_shape() is the only thing that knows which, so every case below
   is read back through it. */

static void test_composite_box(void)
{
	ptrdiff_t aShape[DASIDX_MAX];
	float aVals[16];
	for(int i = 0; i < 16; ++i) aVals[i] = (float)i;

	DasForm* pForm = new_DasFormVec("TSCS", DASVECSYS_CART, NULL);
	if(pForm == NULL){ FAIL("no vector form"); return; }

	DasDatum dm;

	/* rank 1, inline */
	ptrdiff_t a3[1] = {3};
	if(!DasDatum_wrapElems(&dm, pForm, (const uint8_t*)aVals, 1, a3, DASVT_FLOAT, DASUNITS_NT))
		FAIL("box rank 1");
	if(dm.vt != DASVT_COMP)               FAIL("box did not set DASVT_COMP");
	if(DasDatum_shape(&dm, aShape) != 1)  FAIL("rank 1 read back");
	if(aShape[0] != 3)                     FAIL("extent = %td", aShape[0]);
	if(DasDatum_numElems(&dm) != 3)         FAIL("rank 1 nElems");
	if(DasDatum_elemsBytes(&dm) != 3*sizeof(float)) FAIL("rank 1 runBytes");
	if(DasDatum_form(&dm) != pForm)       FAIL("form did not survive");
	if(DasDatum_elems(&dm) != (const uint8_t*)aVals) FAIL("run did not survive");
	if(DasDatum_et(&dm) != DASVT_FLOAT) FAIL("cell type did not survive");
	if(DasDatum_isLocal(&dm))             FAIL("a composite is a view");

	/* rank 2, still inline: 3;3 and 9 must not read alike */
	ptrdiff_t a33[2] = {3,3};
	if(!DasDatum_wrapElems(&dm, pForm, (const uint8_t*)aVals, 2, a33, DASVT_FLOAT, DASUNITS_NT))
		FAIL("box rank 2");
	if(DasDatum_shape(&dm, aShape) != 2)  FAIL("rank 2 read back");
	if((aShape[0] != 3)||(aShape[1] != 3)) FAIL("3;3 extents");
	if(DasDatum_numElems(&dm) != 9)         FAIL("rank 2 nElems");

	/* rank above 3 spills behind pShape, which only the accessor knows */
	ptrdiff_t a4[4] = {2,2,2,2};
	if(!DasDatum_wrapElems(&dm, pForm, (const uint8_t*)aVals, 4, a4, DASVT_FLOAT, DASUNITS_NT))
		FAIL("box rank 4");
	if(DasDatum_shape(&dm, aShape) != 4)  FAIL("rank 4 read back");
	if((aShape[0] != 2)||(aShape[3] != 2)) FAIL("rank 4 extents");
	if(DasDatum_numElems(&dm) != 16)        FAIL("rank 4 nElems");

	/* an extent past what 16 bits holds spills as well */
	ptrdiff_t aBig[1] = {70000};
	if(!DasDatum_wrapElems(&dm, pForm, (const uint8_t*)aVals, 1, aBig, DASVT_FLOAT, DASUNITS_NT))
		FAIL("box wide extent");
	if(DasDatum_shape(&dm, aShape) != 1)  FAIL("wide extent rank");
	if(aShape[0] != 70000) FAIL("wide extent = %td, truncated to 16 bits?", aShape[0]);

	/* a ragged extent must not be truncated into the inline field either */
	ptrdiff_t aRag[1] = {DASIDX_RAGGED};
	if(!DasDatum_wrapElems(&dm, pForm, (const uint8_t*)aVals, 1, aRag, DASVT_FLOAT, DASUNITS_NT))
		FAIL("box ragged");
	if(DasDatum_shape(&dm, aShape) != 1)  FAIL("ragged rank");
	if(aShape[0] != DASIDX_RAGGED) FAIL("ragged extent = %td", aShape[0]);
	if(DasDatum_numElems(&dm) != 0) FAIL("a ragged run has no fixed count");

	/* refusals */
	if(DasDatum_wrapElems(&dm, NULL, (const uint8_t*)aVals, 1, a3, DASVT_FLOAT, DASUNITS_NT))
		FAIL("a composite with no formalism was accepted");
	if(DasDatum_wrapElems(&dm, pForm, NULL, 1, a3, DASVT_FLOAT, DASUNITS_NT))
		FAIL("a composite with no run was accepted");
	if(DasDatum_wrapElems(&dm, pForm, (const uint8_t*)aVals, 1, NULL, DASVT_FLOAT, DASUNITS_NT))
		FAIL("a rank 1 composite with no extents was accepted");

	del_DasForm(pForm);
}

/* ************************************************************************* */
/* Reading a composite back through the variable layer, which is the only way
   the box is produced in real use. */

static void test_composite_read(void)
{
	float fill = -1.0f;
	DasAry* pAry = new_DasAry(
		"b_vec", DASVT_FLOAT, 0, (const uint8_t*)&fill, DAS_SHAPE2(0,3), DASUNITS_NT
	);
	if(pAry == NULL){ FAIL("no array"); return; }
	float aVals[6] = { 1.5f, -2.5f, 3.5f, 4.0f, 5.0f, 6.0f };
	DasAry_append(pAry, (const uint8_t*)aVals, 6);

	int8_t aMap[1] = { 0 };
	DasGen* pGen = new_DasGenAry(pAry, 1, aMap);
	ptrdiff_t aIntShape[1] = { 3 };
	DasForm* pForm = new_DasFormVec("TSCS", DASVECSYS_CART, NULL);
	DasVar* pVec = _new_DasVarComp_gen(pGen, DASUNITS_NT, pForm, 1, aIntShape);
	del_DasForm(pForm);   /* the variable copied it */
	if(pVec == NULL){ FAIL("no composite variable"); return; }

	ptrdiff_t aAddr[1] = { 1 };
	DasDatum dm;
	if(DasVar_datumAt(pVec, aAddr, (DasByteSeq){0}, &dm) != 0){ FAIL("get refused"); return; }

	/* The run must be the ARRAY's memory, not a dead stack frame: a composite
	   datum is a view, so a run that does not outlive the call is a dangling
	   pointer the caller cannot detect. */
	size_t uElSz = 0, uEls = 0;
	const uint8_t* pBeg = DasAry_data(pAry, &uElSz, &uEls);
	const uint8_t* pRun = DasDatum_elems(&dm);
	if((pRun < pBeg)||(pRun >= pBeg + uElSz*uEls))
		FAIL("the boxed run points outside the backing array");

	double aComp[3];
	if(DasDatum_toDoubles(&dm, aComp, 3) != 3)
		FAIL("could not read components");
	if((aComp[0] != 4.0)||(aComp[1] != 5.0)||(aComp[2] != 6.0))
		FAIL("components = %g %g %g", aComp[0], aComp[1], aComp[2]);

	/* the formalism renders it; a form with no opinion gets bracketed cells */
	char sBuf[128] = {'\0'};
	DasDatum_toStr(&dm, -1, sBuf, sizeof(sBuf));
	if(strstr(sBuf, "4") == NULL) FAIL("printed as '%s'", sBuf);

	dec_DasAry(pAry);
}

/* The refusals tested here are deliberate, so their daserrf() messages
   are dropped; everything else prints as usual.  The return codes are what
   the checks look at. */
static void _dropErrors(const DasLogMsg* pMsg, void* pUser)
{
	(void)pUser;
	if(pMsg->errCode == DAS_OKAY) daslog_stderr(pMsg, NULL);
}

/* ************************************************************************* */
int main(int argc, char** argv)
{
	/* DASERRDISP_RET, not _EXIT: the refusal cases below deliberately drive
	   error returns, and the hook drops the expected messages.  A passing run
	   is silent. */
	DasLogHook hook = {_dropErrors, NULL};
	daslib_init(argv[0], DASERRDISP_RET, DASLOG_INFO, &hook);

	test_shape_total();
	test_datum_ctors();
	test_islocal();
	test_null();
	test_from_time();
	test_range_open();
	test_dim_range();
	test_composite_box();
	test_composite_read();

	if(g_fails > 0){
		printf("ERROR: TestDatum had %d failure(s)\n", g_fails);
		return 15;
	}
	printf("INFO: All datum layer tests passed\n");
	return 0;
}
