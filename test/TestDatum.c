/** @file TestDatum.c Unit tests for the das_datum layer (datum.c) */

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
#include <das3/form_vector.h>   /* core.h stops at the generic layer */

static int g_fails = 0;
#define FAIL(...) do{ printf("FAIL (line %d): ", __LINE__); printf(__VA_ARGS__); \
                      printf("\n"); ++g_fails; }while(0)

/* ************************************************************************* */
/* das_datum_shape: total over every datum type.

   A function that only answers for some of its inputs is a trap, so each type
   is checked here even where the answer is trivially 0. */

static void test_shape_total(void)
{
	ptrdiff_t aShape[VARIDX_MAX];
	das_datum dm;

	/* a simple value is rank 0 and writes nothing */
	das_datum_fromDbl(&dm, 3.0, UNIT_DIMENSIONLESS);
	if(das_datum_shape(&dm, aShape) != 0) FAIL("double rank");
	if(das_datum_nElems(&dm) != 1)        FAIL("double nElems");
	if(das_datum_runBytes(&dm) != 0)      FAIL("a scalar needs no caller storage");

	/* text is rank 1 and reports exactly what strlen would.  The NUL that
	   D2ARY_AS_STRING puts in an array is das2C's storage choice, so the
	   backing array reports one MORE than this does. */
	das_datum_wrapStr(&dm, "hello", UNIT_DIMENSIONLESS);
	if(das_datum_shape(&dm, aShape) != 1) FAIL("text rank");
	if(aShape[0] != 5) FAIL("text extent = %td, wanted strlen 5", aShape[0]);

	das_datum_wrapStr(&dm, "", UNIT_DIMENSIONLESS);
	if(das_datum_shape(&dm, aShape) != 1) FAIL("empty text rank");
	if(aShape[0] != 0) FAIL("empty text extent = %td", aShape[0]);

	/* a byte sequence carries its own length */
	ubyte aRaw[24];
	das_cbyte_seq bs; bs.ptr = aRaw; bs.sz = 24;
	das_datum_byteSeq(&dm, bs, UNIT_DIMENSIONLESS);
	if(das_datum_shape(&dm, aShape) != 1) FAIL("byteseq rank");
	if(aShape[0] != 24) FAIL("byteseq extent = %td", aShape[0]);

	/* rank only, for a caller that has no array to spare */
	if(das_datum_shape(&dm, NULL) != 1) FAIL("NULL pShape rejected");
}

/* ************************************************************************* */
/* das_datum_wrapStr and das_datum_init: two argument orders the compiler
   cannot check for you.

   das_units is a const char*, so wrapStr's (datum, sStr, units) and
   (datum, units, sStr) are the SAME C signature -- a swap between header and
   implementation compiles clean and silently trades the two.  Only a value
   check catches it. */

static void test_datum_ctors(void)
{
	ptrdiff_t aShape[VARIDX_MAX];
	das_datum dm;

	das_datum_wrapStr(&dm, "meters", UNIT_DIMENSIONLESS);
	if(dm.units != UNIT_DIMENSIONLESS)
		FAIL("wrapStr took its value as the units");
	das_datum_shape(&dm, aShape);
	if(aShape[0] != 6) FAIL("wrapStr stored the units as its value");

	/* das_datum_init must copy a byteseq's pointer AND its length; copying
	   only the pointer leaves a size nothing downstream can check */
	ubyte aRaw[24];
	das_cbyte_seq bs; bs.ptr = aRaw; bs.sz = 24;
	das_datum_init(&dm, (const ubyte*)&bs, vtByteSeq, sizeof(das_cbyte_seq),
	               UNIT_DIMENSIONLESS);
	if(das_datum_shape(&dm, aShape) != 1) FAIL("init byteseq rank");
	if(aShape[0] != 24) FAIL("init truncated the byteseq to its pointer");
}

/* ************************************************************************* */
/* das_datum_islocal: does the datum own its bytes?

   The predicate any interface must check before keeping a datum past the call
   it arrived on. */

static void test_islocal(void)
{
	das_datum dm;

	das_datum_fromDbl(&dm, 1.0, UNIT_DIMENSIONLESS);
	if(!das_datum_islocal(&dm)) FAIL("a double is local");

	das_datum_wrapStr(&dm, "x", UNIT_DIMENSIONLESS);
	if(das_datum_islocal(&dm)) FAIL("text is a reference, not local");

	ubyte aRaw[4];
	das_cbyte_seq bs; bs.ptr = aRaw; bs.sz = 4;
	das_datum_byteSeq(&dm, bs, UNIT_DIMENSIONLESS);
	if(das_datum_islocal(&dm)) FAIL("a byteseq is a reference, not local");

	/* and the enforcement it was written for: a das_range outlives the call
	   that fills it, so it may only hold datums that own their bytes */
	das_range rng;
	das_datum dmA, dmB;
	das_datum_fromDbl(&dmA, 1.0, UNIT_SECONDS);
	das_datum_fromDbl(&dmB, 2.0, UNIT_SECONDS);
	if(das_range_fromDatum(&rng, "time", &dmA, &dmB) != DAS_OKAY)
		FAIL("a local pair was refused");

	das_datum_wrapStr(&dmB, "not mine", UNIT_DIMENSIONLESS);
	if(das_range_fromDatum(&rng, "time", &dmA, &dmB) == DAS_OKAY)
		FAIL("a range retained a reference datum");
}

/* ************************************************************************* */
/* das_datum_box: the vtComposite box.

   Extents ride inside uInfo when they fit and behind pShape when they do not.
   das_datum_shape() is the only thing that knows which, so every case below
   is read back through it. */

static void test_composite_box(void)
{
	ptrdiff_t aShape[VARIDX_MAX];
	float aVals[16];
	for(int i = 0; i < 16; ++i) aVals[i] = (float)i;

	DasForm* pForm = new_DasFormVector("TSCS", DAS_VSYS_CART, NULL);
	if(pForm == NULL){ FAIL("no vector form"); return; }

	das_datum dm;

	/* rank 1, inline */
	ptrdiff_t a3[1] = {3};
	if(!das_datum_box(&dm, pForm, (const ubyte*)aVals, 1, a3, vtFloat, UNIT_NT))
		FAIL("box rank 1");
	if(dm.vt != vtComposite)               FAIL("box did not set vtComposite");
	if(das_datum_shape(&dm, aShape) != 1)  FAIL("rank 1 read back");
	if(aShape[0] != 3)                     FAIL("extent = %td", aShape[0]);
	if(das_datum_nElems(&dm) != 3)         FAIL("rank 1 nElems");
	if(das_datum_runBytes(&dm) != 3*sizeof(float)) FAIL("rank 1 runBytes");
	if(das_datum_form(&dm) != pForm)       FAIL("form did not survive");
	if(das_datum_run(&dm) != (const ubyte*)aVals) FAIL("run did not survive");
	if(das_datum_elemType(&dm) != vtFloat) FAIL("cell type did not survive");
	if(das_datum_islocal(&dm))             FAIL("a composite is a view");

	/* rank 2, still inline: 3;3 and 9 must not read alike */
	ptrdiff_t a33[2] = {3,3};
	if(!das_datum_box(&dm, pForm, (const ubyte*)aVals, 2, a33, vtFloat, UNIT_NT))
		FAIL("box rank 2");
	if(das_datum_shape(&dm, aShape) != 2)  FAIL("rank 2 read back");
	if((aShape[0] != 3)||(aShape[1] != 3)) FAIL("3;3 extents");
	if(das_datum_nElems(&dm) != 9)         FAIL("rank 2 nElems");

	/* rank above 3 spills behind pShape, which only the accessor knows */
	ptrdiff_t a4[4] = {2,2,2,2};
	if(!das_datum_box(&dm, pForm, (const ubyte*)aVals, 4, a4, vtFloat, UNIT_NT))
		FAIL("box rank 4");
	if(das_datum_shape(&dm, aShape) != 4)  FAIL("rank 4 read back");
	if((aShape[0] != 2)||(aShape[3] != 2)) FAIL("rank 4 extents");
	if(das_datum_nElems(&dm) != 16)        FAIL("rank 4 nElems");

	/* an extent past what 16 bits holds spills as well */
	ptrdiff_t aBig[1] = {70000};
	if(!das_datum_box(&dm, pForm, (const ubyte*)aVals, 1, aBig, vtFloat, UNIT_NT))
		FAIL("box wide extent");
	if(das_datum_shape(&dm, aShape) != 1)  FAIL("wide extent rank");
	if(aShape[0] != 70000) FAIL("wide extent = %td, truncated to 16 bits?", aShape[0]);

	/* a ragged extent must not be truncated into the inline field either */
	ptrdiff_t aRag[1] = {VARIDX_RAGGED};
	if(!das_datum_box(&dm, pForm, (const ubyte*)aVals, 1, aRag, vtFloat, UNIT_NT))
		FAIL("box ragged");
	if(das_datum_shape(&dm, aShape) != 1)  FAIL("ragged rank");
	if(aShape[0] != VARIDX_RAGGED) FAIL("ragged extent = %td", aShape[0]);
	if(das_datum_nElems(&dm) != 0) FAIL("a ragged run has no fixed count");

	/* refusals */
	if(das_datum_box(&dm, NULL, (const ubyte*)aVals, 1, a3, vtFloat, UNIT_NT))
		FAIL("a composite with no formalism was accepted");
	if(das_datum_box(&dm, pForm, NULL, 1, a3, vtFloat, UNIT_NT))
		FAIL("a composite with no run was accepted");
	if(das_datum_box(&dm, pForm, (const ubyte*)aVals, 1, NULL, vtFloat, UNIT_NT))
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
		"b_vec", vtFloat, 0, (const ubyte*)&fill, RANK_2(0,3), UNIT_NT
	);
	if(pAry == NULL){ FAIL("no array"); return; }
	float aVals[6] = { 1.5f, -2.5f, 3.5f, 4.0f, 5.0f, 6.0f };
	DasAry_append(pAry, (const ubyte*)aVals, 6);

	int8_t aMap[1] = { 0 };
	DasGen* pGen = new_DasGenAry(pAry, 1, aMap);
	ptrdiff_t aIntShape[1] = { 3 };
	DasForm* pForm = new_DasFormVector("TSCS", DAS_VSYS_CART, NULL);
	DasVarComp* pVec = new_DasVarComp(pGen, UNIT_NT, pForm, 1, aIntShape);
	del_DasForm(pForm);   /* the variable copied it */
	if(pVec == NULL){ FAIL("no composite variable"); return; }

	ptrdiff_t aLoc[1] = { 1 };
	das_datum dm;
	if(DasVar_get((DasVar*)pVec, aLoc, DAS_BS_NULL, &dm) != 0){ FAIL("get refused"); return; }

	/* The run must be the ARRAY's memory, not a dead stack frame: a composite
	   datum is a view, so a run that does not outlive the call is a dangling
	   pointer the caller cannot detect. */
	size_t uElSz = 0, uEls = 0;
	const ubyte* pBeg = DasAry_getAllVals(pAry, &uElSz, &uEls);
	const ubyte* pRun = das_datum_run(&dm);
	if((pRun < pBeg)||(pRun >= pBeg + uElSz*uEls))
		FAIL("the boxed run points outside the backing array");

	double aComp[3];
	if(das_datum_toDoubles(&dm, aComp, 3) != 3)
		FAIL("could not read components");
	if((aComp[0] != 4.0)||(aComp[1] != 5.0)||(aComp[2] != 6.0))
		FAIL("components = %g %g %g", aComp[0], aComp[1], aComp[2]);

	/* the formalism renders it; a form with no opinion gets bracketed cells */
	char sBuf[128] = {'\0'};
	das_datum_toStr(&dm, sBuf, sizeof(sBuf), -1);
	if(strstr(sBuf, "4") == NULL) FAIL("printed as '%s'", sBuf);

	dec_DasAry(pAry);
}

/* ************************************************************************* */
int main(int argc, char** argv)
{
	/* DASERR_DIS_RET, not _EXIT: the refusal cases below deliberately drive
	   error returns, and a non-zero errBuf captures the expected messages
	   instead of printing them.  A passing run is silent. */
	das_init(argv[0], DASERR_DIS_RET, 256, DASLOG_INFO, NULL);

	test_shape_total();
	test_datum_ctors();
	test_islocal();
	test_composite_box();
	test_composite_read();

	if(g_fails > 0){
		printf("ERROR: TestDatum had %d failure(s)\n", g_fails);
		return 15;
	}
	printf("INFO: All datum layer tests passed\n");
	return 0;
}
