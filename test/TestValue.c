/** @file TestValue.c Unit tests for the das2C value layer (value.c) */

/* Author: Chris Piker <chris-piker@uiowa.edu>, via Claude Opus 4.8
 *
 * This file is intended to demonstrate an interface.  This is free
 * and unencumbered software released into the public domain
 *
 * Anyone is free to copy, modify, publish, use, compile, sell, or
 * distribute this software, either in source code form or as a compiled
 * binary, for any purpose, commercial or non-commercial, and by any
 * means.
 *
 * In jurisdictions that recognize copyright laws, the author or authors
 * of this software dedicate any and all copyright interest in the
 * software to the public domain. We make this dedication for the benefit
 * of the public at large and to the detriment of our heirs and
 * successors. We intend this dedication to be an overt act of
 * relinquishment in perpetuity of all present and future rights to this
 * software under copyright law.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
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
#include <limits.h>
#include <math.h>

#include <das2/core.h>

static int g_fails = 0;

#define FAIL(...) do{ printf("FAIL (line %d): ", __LINE__); printf(__VA_ARGS__); \
                      printf("\n"); ++g_fails; }while(0)

/* ************************************************************************* */
/* das_value_accum: typed, range-checked multiply-accumulate.

   Two things are checked for every case: the return code (DAS_OKAY vs a
   DASERR_VALUE guard trip) AND that a guarded call leaves the accumulator
   UNCHANGED -- a partial write on overflow would be its own bug. */

static void test_accum_values(void)
{
	/* --- value correctness, one per numeric type (accum += step*count) --- */
	{
		uint8_t a=1, s=2;
		das_value_accum(vtUByte, (ubyte*)&a, (ubyte*)&s, 3);
		if(a != 7) FAIL("vtUByte 1+2*3 = %u", a);
	}{
		uint16_t a=10, s=5;
		das_value_accum(vtUShort, (ubyte*)&a, (ubyte*)&s, 4);
		if(a != 30) FAIL("vtUShort = %u", a);
	}{
		int16_t a=-5, s=3;
		das_value_accum(vtShort, (ubyte*)&a, (ubyte*)&s, 4);
		if(a != 7) FAIL("vtShort = %d", a);
	}{
		uint32_t a=100, s=7;
		das_value_accum(vtUInt, (ubyte*)&a, (ubyte*)&s, 3);
		if(a != 121) FAIL("vtUInt = %u", a);
	}{
		int32_t a=100, s=3;
		das_value_accum(vtInt, (ubyte*)&a, (ubyte*)&s, 10);
		if(a != 130) FAIL("vtInt = %d", a);
	}{
		uint64_t a=1, s=1000;
		das_value_accum(vtULong, (ubyte*)&a, (ubyte*)&s, 5);
		if(a != 5001) FAIL("vtULong = %llu", (unsigned long long)a);
	}{
		int64_t a=0, s=-7;
		das_value_accum(vtLong, (ubyte*)&a, (ubyte*)&s, 6);
		if(a != -42) FAIL("vtLong = %lld", (long long)a);
	}{
		float a=1.0f, s=0.25f;
		das_value_accum(vtFloat, (ubyte*)&a, (ubyte*)&s, 4);
		if(a != 2.0f) FAIL("vtFloat = %g", (double)a);
	}{
		double a=0.0, s=0.125;
		das_value_accum(vtDouble, (ubyte*)&a, (ubyte*)&s, 128);
		if(a != 16.0) FAIL("vtDouble 0+0.125*128 = %g", a);
	}

	/* count of 0 is a no-op; count of 1 is a plain add */
	{
		int32_t a=42, s=99;
		das_value_accum(vtInt, (ubyte*)&a, (ubyte*)&s, 0);
		if(a != 42) FAIL("count 0 = %d", a);
	}{
		int32_t a=42, s=99;
		das_value_accum(vtInt, (ubyte*)&a, (ubyte*)&s, 1);
		if(a != 141) FAIL("count 1 = %d", a);
	}{
		/* negative count is valid for a signed type */
		int32_t a=0, s=5;
		das_value_accum(vtInt, (ubyte*)&a, (ubyte*)&s, -3);
		if(a != -15) FAIL("signed neg count = %d", a);
	}
}

static void test_accum_guards(void)
{
	DasErrCode r;

	{	/* int8 overflow: 100 + 3*30 = 190 > 127 -> guard, unchanged */
		int8_t a=100, s=3;
		r = das_value_accum(vtByte, (ubyte*)&a, (ubyte*)&s, 30);
		if(r == DAS_OKAY) FAIL("vtByte overflow not caught");
		if(a != 100) FAIL("vtByte changed on guard = %d", a);
	}{	/* int8 boundary: 100 + 1*27 = 127 ok */
		int8_t a=100, s=1;
		r = das_value_accum(vtByte, (ubyte*)&a, (ubyte*)&s, 27);
		if(r != DAS_OKAY) FAIL("vtByte boundary 127 rejected");
		if(a != 127) FAIL("vtByte boundary = %d", a);
	}{	/* uint8 overflow: 200 + 1*100 = 300 -> guard, unchanged */
		uint8_t a=200, s=1;
		r = das_value_accum(vtUByte, (ubyte*)&a, (ubyte*)&s, 100);
		if(r == DAS_OKAY) FAIL("vtUByte overflow not caught");
		if(a != 200) FAIL("vtUByte changed on guard = %u", a);
	}{	/* unsigned with negative count -> rejected, unchanged */
		uint8_t a=5, s=1;
		r = das_value_accum(vtUByte, (ubyte*)&a, (ubyte*)&s, -1);
		if(r == DAS_OKAY) FAIL("unsigned neg count not caught");
		if(a != 5) FAIL("unsigned neg changed = %u", a);
	}{	/* int64 multiply-overflow: (INT64_MAX/2)*4 -> guard */
		int64_t a=0, s=INT64_MAX/2;
		r = das_value_accum(vtLong, (ubyte*)&a, (ubyte*)&s, 4);
		if(r == DAS_OKAY) FAIL("int64 mul overflow not caught");
		if(a != 0) FAIL("int64 changed on guard");
	}{	/* the trap edge: -1 * INT64_MIN must NOT evaluate INT64_MIN/-1, and must guard */
		int64_t a=0, s=-1;
		r = das_value_accum(vtLong, (ubyte*)&a, (ubyte*)&s, (ptrdiff_t)INT64_MIN);
		if(r == DAS_OKAY) FAIL("int64 -1*INT64_MIN not caught");
		if(a != 0) FAIL("int64 trap changed");
	}{	/* int64 add-overflow: INT64_MAX + 1*1 -> guard */
		int64_t a=INT64_MAX, s=1;
		r = das_value_accum(vtLong, (ubyte*)&a, (ubyte*)&s, 1);
		if(r == DAS_OKAY) FAIL("int64 add overflow not caught");
		if(a != INT64_MAX) FAIL("int64 add changed");
	}{	/* uint64 multiply-overflow */
		uint64_t a=0, s=UINT64_MAX/2 + 1;
		r = das_value_accum(vtULong, (ubyte*)&a, (ubyte*)&s, 3);
		if(r == DAS_OKAY) FAIL("uint64 mul overflow not caught");
		if(a != 0) FAIL("uint64 changed on guard");
	}{	/* float overflow -> inf -> guard, unchanged */
		float a=0, s=1e38f;
		r = das_value_accum(vtFloat, (ubyte*)&a, (ubyte*)&s, 1000000);
		if(r == DAS_OKAY) FAIL("float inf overflow not caught");
		if(a != 0) FAIL("float changed on guard = %g", (double)a);
	}
}

static void test_accum_rejects(void)
{
	DasErrCode r;
	int64_t a=0, s=1;

	/* vtTime is rejected: calendar math needs two value types */
	r = das_value_accum(vtTime, (ubyte*)&a, (ubyte*)&s, 1);
	if(r==DAS_OKAY) FAIL("vtTime not rejected");

	/* non-numeric type rejected */
	r = das_value_accum(vtText, (ubyte*)&a, (ubyte*)&s, 1);
	if(r==DAS_OKAY) FAIL("vtText not rejected");

	/* null pointers rejected */
	r = das_value_accum(vtLong, NULL, (ubyte*)&s, 1);
	if(r==DAS_OKAY) FAIL("null accum not rejected");
	r = das_value_accum(vtLong, (ubyte*)&a, NULL, 1);
	if(r==DAS_OKAY) FAIL("null step not rejected");
}

/* ************************************************************************* */
int main(int argc, char** argv)
{
	/* DASERR_DIS_RET (not _EXIT): the guard tests deliberately drive error
	   returns.  das_error() (older than log.h) writes straight to stderr unless an
	   error buffer is set, so a non-zero errBuf -> das_save_error() captures those
	   expected messages instead of printing them.  A passing run is silent; we
	   assert on the return codes, not the text. */
	das_init(argv[0], DASERR_DIS_RET, 256, DASLOG_INFO, NULL);

	test_accum_values();
	test_accum_guards();
	test_accum_rejects();

	if(g_fails > 0){
		printf("ERROR: TestValue had %d failure(s)\n", g_fails);
		return 15;
	}
	printf("INFO: All value layer tests passed\n");
	return 0;
}
