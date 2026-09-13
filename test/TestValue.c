/** @file TestValue.c Unit tests for the das2C value layer (value.c) */

/* Author: Chris Piker <chris-piker@uiowa.edu>, via Claude Opus 4.8
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
#include <limits.h>
#include <math.h>

#include <das3/core.h>

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
/* das_value_binXform: the 10x10 numeric conversion table.

   The sweep below is a *width* test as much as a value test.  Every row is a
   macro handed a (TY_OUT, TY_IN) pair, and a pair that is wrong in either slot
   reads or writes the wrong number of bytes, so a destination sized exactly to
   its type with canary bytes behind it catches the whole class -- including
   the cases where the value still happens to land correctly. */

static int vt_isSigned(das_val_type vt){
	return (vt==vtByte)||(vt==vtShort)||(vt==vtInt)||(vt==vtLong)
	     ||(vt==vtFloat)||(vt==vtDouble);
}

static void vt_store(das_val_type vt, ubyte* p, double r){
	switch(vt){
	case vtUByte : *((uint8_t*)p)  = (uint8_t)r;  break;
	case vtByte  : *((int8_t*)p)   = (int8_t)r;   break;
	case vtUShort: *((uint16_t*)p) = (uint16_t)r; break;
	case vtShort : *((int16_t*)p)  = (int16_t)r;  break;
	case vtUInt  : *((uint32_t*)p) = (uint32_t)r; break;
	case vtInt   : *((int32_t*)p)  = (int32_t)r;  break;
	case vtULong : *((uint64_t*)p) = (uint64_t)r; break;
	case vtLong  : *((int64_t*)p)  = (int64_t)r;  break;
	case vtFloat : *((float*)p)    = (float)r;    break;
	default      : *((double*)p)   = r;           break;
	}
}

static double vt_load(das_val_type vt, const ubyte* p){
	switch(vt){
	case vtUByte : return *((const uint8_t*)p);
	case vtByte  : return *((const int8_t*)p);
	case vtUShort: return *((const uint16_t*)p);
	case vtShort : return *((const int16_t*)p);
	case vtUInt  : return *((const uint32_t*)p);
	case vtInt   : return *((const int32_t*)p);
	case vtULong : return (double)*((const uint64_t*)p);
	case vtLong  : return (double)*((const int64_t*)p);
	case vtFloat : return *((const float*)p);
	default      : return *((const double*)p);
	}
}

static void test_binxform_table(void)
{
	static const das_val_type aVt[10] = {
		vtUByte, vtByte, vtUShort, vtShort, vtUInt, vtInt, vtULong, vtLong,
		vtFloat, vtDouble
	};
	/* magnitudes every one of the ten types holds exactly */
	static const double aVal[6] = { 0.0, 1.0, 100.0, 127.0, -1.0, -100.0 };

	for(int i = 0; i < 10; ++i){
	for(int j = 0; j < 10; ++j){
	for(int k = 0; k < 6; ++k){

		double v = aVal[k];
		if((v < 0) && (!vt_isSigned(aVt[i]) || !vt_isSigned(aVt[j]))) continue;

		ubyte in[16] = {0};
		ubyte out[24];
		size_t uOut = das_vt_size(aVt[j]);
		memset(out, 0x5A, sizeof(out));
		vt_store(aVt[i], in, v);

		DasErrCode r = das_value_binXform(aVt[i], in, NULL, aVt[j], out, NULL, 0);

		if(r != DAS_OKAY){
			FAIL("%s -> %s refused %g", das_vt_toStr(aVt[i]), das_vt_toStr(aVt[j]), v);
			continue;
		}
		if(vt_load(aVt[j], out) != v){
			FAIL("%s -> %s gave %g, wanted %g", das_vt_toStr(aVt[i]),
			     das_vt_toStr(aVt[j]), vt_load(aVt[j], out), v);
			continue;
		}
		for(size_t u = uOut; u < sizeof(out); ++u){
			if(out[u] != 0x5A){
				FAIL("%s -> %s wrote past %zu bytes", das_vt_toStr(aVt[i]),
				     das_vt_toStr(aVt[j]), uOut);
				break;
			}
		}
	}}}
}

static void test_binxform_wide(void)
{
	/* Above what a 32-bit read of a 64-bit input would see, so a row that
	   names the wrong input type answers with the low word instead. */
	int64_t n = 5000000000LL;
	uint64_t u = 10000000000ULL;
	double d;
	float f;

	if(das_value_binXform(vtLong,(ubyte*)&n,NULL,vtDouble,(ubyte*)&d,NULL,0) != DAS_OKAY
	   || d != 5e9) FAIL("long -> double gave %g", d);

	if(das_value_binXform(vtLong,(ubyte*)&n,NULL,vtFloat,(ubyte*)&f,NULL,0) != DAS_OKAY
	   || f != 5e9f) FAIL("long -> float gave %g", (double)f);

	if(das_value_binXform(vtULong,(ubyte*)&u,NULL,vtDouble,(ubyte*)&d,NULL,0) != DAS_OKAY
	   || d != 1e10) FAIL("ulong -> double gave %g", d);

	if(das_value_binXform(vtULong,(ubyte*)&u,NULL,vtFloat,(ubyte*)&f,NULL,0) != DAS_OKAY
	   || f != 1e10f) FAIL("ulong -> float gave %g", (double)f);
}

static void test_binxform_guards(void)
{
	/* Each pair carries a value the destination cannot hold.  A silent
	   truncation here is the defect: wrapping to a plausible number is worse
	   than refusing, because nothing downstream can tell. */
	static const struct { das_val_type vtIn; das_val_type vtOut; double val; } aOver[] = {
		{vtUShort, vtByte,   40000.0},
		{vtUShort, vtShort,  40000.0},
		{vtUInt,   vtByte,   40000.0},
		{vtUInt,   vtShort,  40000.0},
		{vtUInt,   vtInt,    3000000000.0},
		{vtInt,    vtUByte,  -1.0},
		{vtInt,    vtShort,  40000.0},
		{vtLong,   vtInt,    3000000000.0},
		{vtLong,   vtUInt,   -1.0},
		{vtULong,  vtLong,   1.8e19},
		{vtDouble, vtByte,   1000.0},
		{vtFloat,  vtUByte,  -5.0},
	};

	for(size_t k = 0; k < sizeof(aOver)/sizeof(aOver[0]); ++k){
		ubyte in[16] = {0}, out[16] = {0};
		vt_store(aOver[k].vtIn, in, aOver[k].val);
		if(das_value_binXform(aOver[k].vtIn, in, NULL, aOver[k].vtOut, out, NULL, 0)
		   == DAS_OKAY)
			FAIL("%s -> %s accepted %g", das_vt_toStr(aOver[k].vtIn),
			     das_vt_toStr(aOver[k].vtOut), aOver[k].val);
	}
}

/* ************************************************************************* */
/* das_value_binop: the shared elementwise kernel.

   The claim under test is that promotion goes to the narrowest domain holding
   both operands, not always to double.  An int64 pair that stays exact above
   2^53 is the whole reason the kernel is not three lines of floating point. */

static void test_binop_values(void)
{
	DasErrCode r;

	{	/* the exactness claim: a TT2000 tick above 2^53 survives */
		int64_t a=1500000000123456789LL, b=1LL, out=0;
		r = das_value_binop(D2BOP_ADD, vtLong,(ubyte*)&a, vtLong,(ubyte*)&b,
		                    vtLong,(ubyte*)&out);
		if(r != DAS_OKAY) FAIL("int64 add refused");
		if(out != 1500000000123456790LL)
			FAIL("int64 exactness lost = %lld", (long long)out);
	}{	/* the same pair with a real output still leaves the domain correctly */
		int64_t a=100LL, b=23LL; double out=-1.0;
		r = das_value_binop(D2BOP_ADD, vtLong,(ubyte*)&a, vtLong,(ubyte*)&b,
		                    vtDouble,(ubyte*)&out);
		if(r != DAS_OKAY) FAIL("int64 add to double refused");
		if(out != 123.0) FAIL("int64 add to double = %g", out);
	}{	/* mixed widths and signs meet in int64 */
		uint8_t a=200; int16_t b=-300; int32_t out=0;
		r = das_value_binop(D2BOP_ADD, vtUByte,(ubyte*)&a, vtShort,(ubyte*)&b,
		                    vtInt,(ubyte*)&out);
		if(r != DAS_OKAY || out != -100) FAIL("ubyte + short = %d", out);
	}{	/* the top half of vtULong has its own domain */
		uint64_t a=18000000000000000000ULL, b=5ULL, out=0;
		r = das_value_binop(D2BOP_ADD, vtULong,(ubyte*)&a, vtULong,(ubyte*)&b,
		                    vtULong,(ubyte*)&out);
		if(r != DAS_OKAY || out != 18000000000000000005ULL)
			FAIL("uint64 add = %llu", (unsigned long long)out);
	}{	/* divide has no exact integer answer, so an integer pair goes real */
		int32_t a=7, b=2; double out=0.0;
		r = das_value_binop(D2BOP_DIV, vtInt,(ubyte*)&a, vtInt,(ubyte*)&b,
		                    vtDouble,(ubyte*)&out);
		if(r != DAS_OKAY || out != 3.5) FAIL("int 7 / int 2 = %g", out);
	}{
		int32_t a=2, b=10; double out=0.0;
		r = das_value_binop(D2BOP_POW, vtInt,(ubyte*)&a, vtInt,(ubyte*)&b,
		                    vtDouble,(ubyte*)&out);
		if(r != DAS_OKAY || out != 1024.0) FAIL("2 ** 10 = %g", out);
	}{
		double a=2.5, b=4.0, out=0.0;
		r = das_value_binop(D2BOP_MUL, vtDouble,(ubyte*)&a, vtDouble,(ubyte*)&b,
		                    vtDouble,(ubyte*)&out);
		if(r != DAS_OKAY || out != 10.0) FAIL("2.5 * 4 = %g", out);
	}{
		float a=1.5f, b=2.25f, out=0.0f;
		r = das_value_binop(D2BOP_SUB, vtFloat,(ubyte*)&a, vtFloat,(ubyte*)&b,
		                    vtFloat,(ubyte*)&out);
		if(r != DAS_OKAY || out != -0.75f) FAIL("1.5 - 2.25 = %g", (double)out);
	}
}

static void test_binop_guards(void)
{
	DasErrCode r;

	{	/* int64 add overflow, output untouched */
		int64_t a=INT64_MAX, b=1LL, out=0;
		r = das_value_binop(D2BOP_ADD, vtLong,(ubyte*)&a, vtLong,(ubyte*)&b,
		                    vtLong,(ubyte*)&out);
		if(r == DAS_OKAY) FAIL("int64 add overflow not caught");
		if(out != 0) FAIL("int64 add wrote on guard");
	}{	/* the trap edge: INT64_MIN * -1 must guard without evaluating it */
		int64_t a=INT64_MIN, b=-1LL, out=0;
		r = das_value_binop(D2BOP_MUL, vtLong,(ubyte*)&a, vtLong,(ubyte*)&b,
		                    vtLong,(ubyte*)&out);
		if(r == DAS_OKAY) FAIL("int64 INT64_MIN * -1 not caught");
	}{
		int64_t a=INT64_MIN, b=1LL, out=0;
		r = das_value_binop(D2BOP_SUB, vtLong,(ubyte*)&a, vtLong,(ubyte*)&b,
		                    vtLong,(ubyte*)&out);
		if(r == DAS_OKAY) FAIL("int64 sub underflow not caught");
	}{	/* an unsigned difference below zero has nowhere to go */
		uint64_t a=1ULL, b=18000000000000000000ULL, out=0;
		r = das_value_binop(D2BOP_SUB, vtULong,(ubyte*)&a, vtULong,(ubyte*)&b,
		                    vtULong,(ubyte*)&out);
		if(r == DAS_OKAY) FAIL("uint64 sub below zero not caught");
	}{	/* a huge unsigned against a negative fits no integer domain, and
		   answering to 53 bits without saying so is not an option */
		uint64_t a=18000000000000000000ULL; int32_t b=-1; int64_t out=0;
		r = das_value_binop(D2BOP_ADD, vtULong,(ubyte*)&a, vtInt,(ubyte*)&b,
		                    vtLong,(ubyte*)&out);
		if(r == DAS_OKAY) FAIL("unrepresentable integer pair not caught");
	}{	/* the result is fine, the declared output is too narrow for it */
		int32_t a=70000, b=70000; int16_t out=0;
		r = das_value_binop(D2BOP_MUL, vtInt,(ubyte*)&a, vtInt,(ubyte*)&b,
		                    vtShort,(ubyte*)&out);
		if(r == DAS_OKAY) FAIL("narrow output not caught");
	}{	/* divide by zero is an overflow, not an inf handed downstream */
		double a=1.0, b=0.0, out=0.0;
		r = das_value_binop(D2BOP_DIV, vtDouble,(ubyte*)&a, vtDouble,(ubyte*)&b,
		                    vtDouble,(ubyte*)&out);
		if(r == DAS_OKAY) FAIL("divide by zero not caught");
	}{	/* but an operand that arrived non-finite is the caller's business */
		double a=INFINITY, b=1.0, out=0.0;
		r = das_value_binop(D2BOP_ADD, vtDouble,(ubyte*)&a, vtDouble,(ubyte*)&b,
		                    vtDouble,(ubyte*)&out);
		if(r != DAS_OKAY) FAIL("non-finite operand refused");
		if(!isinf(out)) FAIL("non-finite operand not passed through");
	}
}

static void test_binop_rejects(void)
{
	DasErrCode r;
	double a=1.0, b=2.0, out=0.0;

	/* vtTime is refused: broken down calendar math mixes two value types */
	{
		das_time dt; dt_null(&dt);
		r = das_value_binop(D2BOP_ADD, vtTime,(ubyte*)&dt, vtDouble,(ubyte*)&b,
		                    vtDouble,(ubyte*)&out);
		if(r == DAS_OKAY) FAIL("vtTime not refused");
	}
	{
		const char* s = "x";
		r = das_value_binop(D2BOP_ADD, vtText,(ubyte*)&s, vtDouble,(ubyte*)&b,
		                    vtDouble,(ubyte*)&out);
		if(r == DAS_OKAY) FAIL("vtText not refused");
	}

	/* a unary code, and a code in the binary range that names no operator */
	r = das_value_binop(D2UOP_SQRT, vtDouble,(ubyte*)&a, vtDouble,(ubyte*)&b,
	                    vtDouble,(ubyte*)&out);
	if(r == DAS_OKAY) FAIL("unary op code not refused");

	r = das_value_binop(250, vtDouble,(ubyte*)&a, vtDouble,(ubyte*)&b,
	                    vtDouble,(ubyte*)&out);
	if(r == DAS_OKAY) FAIL("unnamed op code not refused");

	r = das_value_binop(D2BOP_ADD, vtDouble, NULL, vtDouble,(ubyte*)&b,
	                    vtDouble,(ubyte*)&out);
	if(r == DAS_OKAY) FAIL("null left operand not refused");
	r = das_value_binop(D2BOP_ADD, vtDouble,(ubyte*)&a, vtDouble,(ubyte*)&b,
	                    vtDouble, NULL);
	if(r == DAS_OKAY) FAIL("null output not refused");
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

	test_binxform_table();
	test_binxform_wide();
	test_binxform_guards();

	test_binop_values();
	test_binop_guards();
	test_binop_rejects();

	if(g_fails > 0){
		printf("ERROR: TestValue had %d failure(s)\n", g_fails);
		return 15;
	}
	printf("INFO: All value layer tests passed\n");
	return 0;
}
