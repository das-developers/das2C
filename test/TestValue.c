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
/* dasval_accum: typed, range-checked multiply-accumulate.

   Two things are checked for every case: the return code (DAS_OKAY vs a
   DASERR_VALUE guard trip) AND that a guarded call leaves the accumulator
   UNCHANGED -- a partial write on overflow would be its own bug. */

static void test_accum_values(void)
{
	/* --- value correctness, one per numeric type (accum += step*count) --- */
	{
		uint8_t a=1, s=2;
		dasval_accum(DASVT_UBYTE, (uint8_t*)&a, (uint8_t*)&s, 3);
		if(a != 7) FAIL("DASVT_UBYTE 1+2*3 = %u", a);
	}{
		uint16_t a=10, s=5;
		dasval_accum(DASVT_USHORT, (uint8_t*)&a, (uint8_t*)&s, 4);
		if(a != 30) FAIL("DASVT_USHORT = %u", a);
	}{
		int16_t a=-5, s=3;
		dasval_accum(DASVT_SHORT, (uint8_t*)&a, (uint8_t*)&s, 4);
		if(a != 7) FAIL("DASVT_SHORT = %d", a);
	}{
		uint32_t a=100, s=7;
		dasval_accum(DASVT_UINT, (uint8_t*)&a, (uint8_t*)&s, 3);
		if(a != 121) FAIL("DASVT_UINT = %u", a);
	}{
		int32_t a=100, s=3;
		dasval_accum(DASVT_INT, (uint8_t*)&a, (uint8_t*)&s, 10);
		if(a != 130) FAIL("DASVT_INT = %d", a);
	}{
		uint64_t a=1, s=1000;
		dasval_accum(DASVT_ULONG, (uint8_t*)&a, (uint8_t*)&s, 5);
		if(a != 5001) FAIL("DASVT_ULONG = %llu", (unsigned long long)a);
	}{
		int64_t a=0, s=-7;
		dasval_accum(DASVT_LONG, (uint8_t*)&a, (uint8_t*)&s, 6);
		if(a != -42) FAIL("DASVT_LONG = %lld", (long long)a);
	}{
		float a=1.0f, s=0.25f;
		dasval_accum(DASVT_FLOAT, (uint8_t*)&a, (uint8_t*)&s, 4);
		if(a != 2.0f) FAIL("DASVT_FLOAT = %g", (double)a);
	}{
		double a=0.0, s=0.125;
		dasval_accum(DASVT_DOUBLE, (uint8_t*)&a, (uint8_t*)&s, 128);
		if(a != 16.0) FAIL("DASVT_DOUBLE 0+0.125*128 = %g", a);
	}

	/* count of 0 is a no-op; count of 1 is a plain add */
	{
		int32_t a=42, s=99;
		dasval_accum(DASVT_INT, (uint8_t*)&a, (uint8_t*)&s, 0);
		if(a != 42) FAIL("count 0 = %d", a);
	}{
		int32_t a=42, s=99;
		dasval_accum(DASVT_INT, (uint8_t*)&a, (uint8_t*)&s, 1);
		if(a != 141) FAIL("count 1 = %d", a);
	}{
		/* negative count is valid for a signed type */
		int32_t a=0, s=5;
		dasval_accum(DASVT_INT, (uint8_t*)&a, (uint8_t*)&s, -3);
		if(a != -15) FAIL("signed neg count = %d", a);
	}
}

static void test_accum_guards(void)
{
	das_errcode r;

	{	/* int8 overflow: 100 + 3*30 = 190 > 127 -> guard, unchanged */
		int8_t a=100, s=3;
		r = dasval_accum(DASVT_BYTE, (uint8_t*)&a, (uint8_t*)&s, 30);
		if(r == DAS_OKAY) FAIL("DASVT_BYTE overflow not caught");
		if(a != 100) FAIL("DASVT_BYTE changed on guard = %d", a);
	}{	/* int8 boundary: 100 + 1*27 = 127 ok */
		int8_t a=100, s=1;
		r = dasval_accum(DASVT_BYTE, (uint8_t*)&a, (uint8_t*)&s, 27);
		if(r != DAS_OKAY) FAIL("DASVT_BYTE boundary 127 rejected");
		if(a != 127) FAIL("DASVT_BYTE boundary = %d", a);
	}{	/* uint8 overflow: 200 + 1*100 = 300 -> guard, unchanged */
		uint8_t a=200, s=1;
		r = dasval_accum(DASVT_UBYTE, (uint8_t*)&a, (uint8_t*)&s, 100);
		if(r == DAS_OKAY) FAIL("DASVT_UBYTE overflow not caught");
		if(a != 200) FAIL("DASVT_UBYTE changed on guard = %u", a);
	}{	/* unsigned with negative count -> rejected, unchanged */
		uint8_t a=5, s=1;
		r = dasval_accum(DASVT_UBYTE, (uint8_t*)&a, (uint8_t*)&s, -1);
		if(r == DAS_OKAY) FAIL("unsigned neg count not caught");
		if(a != 5) FAIL("unsigned neg changed = %u", a);
	}{	/* int64 multiply-overflow: (INT64_MAX/2)*4 -> guard */
		int64_t a=0, s=INT64_MAX/2;
		r = dasval_accum(DASVT_LONG, (uint8_t*)&a, (uint8_t*)&s, 4);
		if(r == DAS_OKAY) FAIL("int64 mul overflow not caught");
		if(a != 0) FAIL("int64 changed on guard");
	}{	/* the trap edge: INT64_MIN * -1 must guard without ever evaluating
		   INT64_MIN/-1.  INT64_MIN rides in the 64-bit step so the case exists
		   where ptrdiff_t is 32 bits (wasm32); there (ptrdiff_t)INT64_MIN would
		   truncate to a count of 0 and nothing would overflow.  The mirrored
		   orientation is kept for hosts with a 64-bit count. */
		int64_t a=0, s=INT64_MIN;
		r = dasval_accum(DASVT_LONG, (uint8_t*)&a, (uint8_t*)&s, -1);
		if(r == DAS_OKAY) FAIL("int64 INT64_MIN*-1 not caught");
		if(a != 0) FAIL("int64 trap changed");
		if(sizeof(ptrdiff_t) == 8){
			a=0; s=-1;
			r = dasval_accum(DASVT_LONG, (uint8_t*)&a, (uint8_t*)&s, (ptrdiff_t)INT64_MIN);
			if(r == DAS_OKAY) FAIL("int64 -1*INT64_MIN not caught");
			if(a != 0) FAIL("int64 trap changed");
		}
	}{	/* int64 add-overflow: INT64_MAX + 1*1 -> guard */
		int64_t a=INT64_MAX, s=1;
		r = dasval_accum(DASVT_LONG, (uint8_t*)&a, (uint8_t*)&s, 1);
		if(r == DAS_OKAY) FAIL("int64 add overflow not caught");
		if(a != INT64_MAX) FAIL("int64 add changed");
	}{	/* uint64 multiply-overflow */
		uint64_t a=0, s=UINT64_MAX/2 + 1;
		r = dasval_accum(DASVT_ULONG, (uint8_t*)&a, (uint8_t*)&s, 3);
		if(r == DAS_OKAY) FAIL("uint64 mul overflow not caught");
		if(a != 0) FAIL("uint64 changed on guard");
	}{	/* float overflow -> inf -> guard, unchanged */
		float a=0, s=1e38f;
		r = dasval_accum(DASVT_FLOAT, (uint8_t*)&a, (uint8_t*)&s, 1000000);
		if(r == DAS_OKAY) FAIL("float inf overflow not caught");
		if(a != 0) FAIL("float changed on guard = %g", (double)a);
	}
}

static void test_accum_rejects(void)
{
	das_errcode r;
	int64_t a=0, s=1;

	/* DASVT_TIME is rejected: calendar math needs two value types */
	r = dasval_accum(DASVT_TIME, (uint8_t*)&a, (uint8_t*)&s, 1);
	if(r==DAS_OKAY) FAIL("DASVT_TIME not rejected");

	/* non-numeric type rejected */
	r = dasval_accum(DASVT_TEXT, (uint8_t*)&a, (uint8_t*)&s, 1);
	if(r==DAS_OKAY) FAIL("DASVT_TEXT not rejected");

	/* null pointers rejected */
	r = dasval_accum(DASVT_LONG, NULL, (uint8_t*)&s, 1);
	if(r==DAS_OKAY) FAIL("null accum not rejected");
	r = dasval_accum(DASVT_LONG, (uint8_t*)&a, NULL, 1);
	if(r==DAS_OKAY) FAIL("null step not rejected");
}

/* ************************************************************************* */
/* dasval_convert: the 10x10 numeric conversion table.

   The sweep below is a *width* test as much as a value test.  Every row is a
   macro handed a (TY_OUT, TY_IN) pair, and a pair that is wrong in either slot
   reads or writes the wrong number of bytes, so a destination sized exactly to
   its type with canary bytes behind it catches the whole class -- including
   the cases where the value still happens to land correctly. */

static int vt_isSigned(das_vt vt){
	return (vt==DASVT_BYTE)||(vt==DASVT_SHORT)||(vt==DASVT_INT)||(vt==DASVT_LONG)
	     ||(vt==DASVT_FLOAT)||(vt==DASVT_DOUBLE);
}

static void vt_store(das_vt vt, uint8_t* p, double r){
	switch(vt){
	case DASVT_UBYTE : *((uint8_t*)p)  = (uint8_t)r;  break;
	case DASVT_BYTE  : *((int8_t*)p)   = (int8_t)r;   break;
	case DASVT_USHORT: *((uint16_t*)p) = (uint16_t)r; break;
	case DASVT_SHORT : *((int16_t*)p)  = (int16_t)r;  break;
	case DASVT_UINT  : *((uint32_t*)p) = (uint32_t)r; break;
	case DASVT_INT   : *((int32_t*)p)  = (int32_t)r;  break;
	case DASVT_ULONG : *((uint64_t*)p) = (uint64_t)r; break;
	case DASVT_LONG  : *((int64_t*)p)  = (int64_t)r;  break;
	case DASVT_FLOAT : *((float*)p)    = (float)r;    break;
	default      : *((double*)p)   = r;           break;
	}
}

static double vt_load(das_vt vt, const uint8_t* p){
	switch(vt){
	case DASVT_UBYTE : return *((const uint8_t*)p);
	case DASVT_BYTE  : return *((const int8_t*)p);
	case DASVT_USHORT: return *((const uint16_t*)p);
	case DASVT_SHORT : return *((const int16_t*)p);
	case DASVT_UINT  : return *((const uint32_t*)p);
	case DASVT_INT   : return *((const int32_t*)p);
	case DASVT_ULONG : return (double)*((const uint64_t*)p);
	case DASVT_LONG  : return (double)*((const int64_t*)p);
	case DASVT_FLOAT : return *((const float*)p);
	default      : return *((const double*)p);
	}
}

static void test_binxform_table(void)
{
	static const das_vt aVt[10] = {
		DASVT_UBYTE, DASVT_BYTE, DASVT_USHORT, DASVT_SHORT, DASVT_UINT, DASVT_INT, DASVT_ULONG, DASVT_LONG,
		DASVT_FLOAT, DASVT_DOUBLE
	};
	/* magnitudes every one of the ten types holds exactly */
	static const double aVal[6] = { 0.0, 1.0, 100.0, 127.0, -1.0, -100.0 };

	for(int i = 0; i < 10; ++i){
	for(int j = 0; j < 10; ++j){
	for(int k = 0; k < 6; ++k){

		double v = aVal[k];
		if((v < 0) && (!vt_isSigned(aVt[i]) || !vt_isSigned(aVt[j]))) continue;

		uint8_t in[16] = {0};
		uint8_t out[24];
		size_t uOut = dasvt_size(aVt[j]);
		memset(out, 0x5A, sizeof(out));
		vt_store(aVt[i], in, v);

		das_errcode r = dasval_convert(aVt[i], in, NULL, aVt[j], out, NULL, 0);

		if(r != DAS_OKAY){
			FAIL("%s -> %s refused %g", dasvt_name(aVt[i]), dasvt_name(aVt[j]), v);
			continue;
		}
		if(vt_load(aVt[j], out) != v){
			FAIL("%s -> %s gave %g, wanted %g", dasvt_name(aVt[i]),
			     dasvt_name(aVt[j]), vt_load(aVt[j], out), v);
			continue;
		}
		for(size_t u = uOut; u < sizeof(out); ++u){
			if(out[u] != 0x5A){
				FAIL("%s -> %s wrote past %zu bytes", dasvt_name(aVt[i]),
				     dasvt_name(aVt[j]), uOut);
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

	if(dasval_convert(DASVT_LONG,(uint8_t*)&n,NULL,DASVT_DOUBLE,(uint8_t*)&d,NULL,0) != DAS_OKAY
	   || d != 5e9) FAIL("long -> double gave %g", d);

	if(dasval_convert(DASVT_LONG,(uint8_t*)&n,NULL,DASVT_FLOAT,(uint8_t*)&f,NULL,0) != DAS_OKAY
	   || f != 5e9f) FAIL("long -> float gave %g", (double)f);

	if(dasval_convert(DASVT_ULONG,(uint8_t*)&u,NULL,DASVT_DOUBLE,(uint8_t*)&d,NULL,0) != DAS_OKAY
	   || d != 1e10) FAIL("ulong -> double gave %g", d);

	if(dasval_convert(DASVT_ULONG,(uint8_t*)&u,NULL,DASVT_FLOAT,(uint8_t*)&f,NULL,0) != DAS_OKAY
	   || f != 1e10f) FAIL("ulong -> float gave %g", (double)f);
}

static void test_binxform_guards(void)
{
	/* Each pair carries a value the destination cannot hold.  A silent
	   truncation here is the defect: wrapping to a plausible number is worse
	   than refusing, because nothing downstream can tell. */
	static const struct { das_vt vtIn; das_vt vtOut; double val; } aOver[] = {
		{DASVT_USHORT, DASVT_BYTE,   40000.0},
		{DASVT_USHORT, DASVT_SHORT,  40000.0},
		{DASVT_UINT,   DASVT_BYTE,   40000.0},
		{DASVT_UINT,   DASVT_SHORT,  40000.0},
		{DASVT_UINT,   DASVT_INT,    3000000000.0},
		{DASVT_INT,    DASVT_UBYTE,  -1.0},
		{DASVT_INT,    DASVT_SHORT,  40000.0},
		{DASVT_LONG,   DASVT_INT,    3000000000.0},
		{DASVT_LONG,   DASVT_UINT,   -1.0},
		{DASVT_ULONG,  DASVT_LONG,   1.8e19},
		{DASVT_DOUBLE, DASVT_BYTE,   1000.0},
		{DASVT_FLOAT,  DASVT_UBYTE,  -5.0},
	};

	for(size_t k = 0; k < sizeof(aOver)/sizeof(aOver[0]); ++k){
		uint8_t in[16] = {0}, out[16] = {0};
		vt_store(aOver[k].vtIn, in, aOver[k].val);
		if(dasval_convert(aOver[k].vtIn, in, NULL, aOver[k].vtOut, out, NULL, 0)
		   == DAS_OKAY)
			FAIL("%s -> %s accepted %g", dasvt_name(aOver[k].vtIn),
			     dasvt_name(aOver[k].vtOut), aOver[k].val);
	}
}

/* ************************************************************************* */
/* dasval_binOp: the shared elementwise kernel.

   The claim under test is that promotion goes to the narrowest domain holding
   both operands, not always to double.  An int64 pair that stays exact above
   2^53 is the whole reason the kernel is not three lines of floating point. */

static void test_binop_values(void)
{
	das_errcode r;

	{	/* the exactness claim: a TT2000 tick above 2^53 survives */
		int64_t a=1500000000123456789LL, b=1LL, out=0;
		r = dasval_binOp(DASOP_ADD, DASVT_LONG,(uint8_t*)&a, DASVT_LONG,(uint8_t*)&b,
		                    DASVT_LONG,(uint8_t*)&out);
		if(r != DAS_OKAY) FAIL("int64 add refused");
		if(out != 1500000000123456790LL)
			FAIL("int64 exactness lost = %lld", (long long)out);
	}{	/* the same pair with a real output still leaves the domain correctly */
		int64_t a=100LL, b=23LL; double out=-1.0;
		r = dasval_binOp(DASOP_ADD, DASVT_LONG,(uint8_t*)&a, DASVT_LONG,(uint8_t*)&b,
		                    DASVT_DOUBLE,(uint8_t*)&out);
		if(r != DAS_OKAY) FAIL("int64 add to double refused");
		if(out != 123.0) FAIL("int64 add to double = %g", out);
	}{	/* mixed widths and signs meet in int64 */
		uint8_t a=200; int16_t b=-300; int32_t out=0;
		r = dasval_binOp(DASOP_ADD, DASVT_UBYTE,(uint8_t*)&a, DASVT_SHORT,(uint8_t*)&b,
		                    DASVT_INT,(uint8_t*)&out);
		if(r != DAS_OKAY || out != -100) FAIL("ubyte + short = %d", out);
	}{	/* the top half of DASVT_ULONG has its own domain */
		uint64_t a=18000000000000000000ULL, b=5ULL, out=0;
		r = dasval_binOp(DASOP_ADD, DASVT_ULONG,(uint8_t*)&a, DASVT_ULONG,(uint8_t*)&b,
		                    DASVT_ULONG,(uint8_t*)&out);
		if(r != DAS_OKAY || out != 18000000000000000005ULL)
			FAIL("uint64 add = %llu", (unsigned long long)out);
	}{	/* divide has no exact integer answer, so an integer pair goes real */
		int32_t a=7, b=2; double out=0.0;
		r = dasval_binOp(DASOP_DIV, DASVT_INT,(uint8_t*)&a, DASVT_INT,(uint8_t*)&b,
		                    DASVT_DOUBLE,(uint8_t*)&out);
		if(r != DAS_OKAY || out != 3.5) FAIL("int 7 / int 2 = %g", out);
	}{
		int32_t a=2, b=10; double out=0.0;
		r = dasval_binOp(DASOP_POW, DASVT_INT,(uint8_t*)&a, DASVT_INT,(uint8_t*)&b,
		                    DASVT_DOUBLE,(uint8_t*)&out);
		if(r != DAS_OKAY || out != 1024.0) FAIL("2 ** 10 = %g", out);
	}{
		double a=2.5, b=4.0, out=0.0;
		r = dasval_binOp(DASOP_MUL, DASVT_DOUBLE,(uint8_t*)&a, DASVT_DOUBLE,(uint8_t*)&b,
		                    DASVT_DOUBLE,(uint8_t*)&out);
		if(r != DAS_OKAY || out != 10.0) FAIL("2.5 * 4 = %g", out);
	}{
		float a=1.5f, b=2.25f, out=0.0f;
		r = dasval_binOp(DASOP_SUB, DASVT_FLOAT,(uint8_t*)&a, DASVT_FLOAT,(uint8_t*)&b,
		                    DASVT_FLOAT,(uint8_t*)&out);
		if(r != DAS_OKAY || out != -0.75f) FAIL("1.5 - 2.25 = %g", (double)out);
	}
}

static void test_binop_guards(void)
{
	das_errcode r;

	{	/* int64 add overflow, output untouched */
		int64_t a=INT64_MAX, b=1LL, out=0;
		r = dasval_binOp(DASOP_ADD, DASVT_LONG,(uint8_t*)&a, DASVT_LONG,(uint8_t*)&b,
		                    DASVT_LONG,(uint8_t*)&out);
		if(r == DAS_OKAY) FAIL("int64 add overflow not caught");
		if(out != 0) FAIL("int64 add wrote on guard");
	}{	/* the trap edge: INT64_MIN * -1 must guard without evaluating it */
		int64_t a=INT64_MIN, b=-1LL, out=0;
		r = dasval_binOp(DASOP_MUL, DASVT_LONG,(uint8_t*)&a, DASVT_LONG,(uint8_t*)&b,
		                    DASVT_LONG,(uint8_t*)&out);
		if(r == DAS_OKAY) FAIL("int64 INT64_MIN * -1 not caught");
	}{
		int64_t a=INT64_MIN, b=1LL, out=0;
		r = dasval_binOp(DASOP_SUB, DASVT_LONG,(uint8_t*)&a, DASVT_LONG,(uint8_t*)&b,
		                    DASVT_LONG,(uint8_t*)&out);
		if(r == DAS_OKAY) FAIL("int64 sub underflow not caught");
	}{	/* an unsigned difference below zero has nowhere to go */
		uint64_t a=1ULL, b=18000000000000000000ULL, out=0;
		r = dasval_binOp(DASOP_SUB, DASVT_ULONG,(uint8_t*)&a, DASVT_ULONG,(uint8_t*)&b,
		                    DASVT_ULONG,(uint8_t*)&out);
		if(r == DAS_OKAY) FAIL("uint64 sub below zero not caught");
	}{	/* a huge unsigned against a negative fits no integer domain, and
		   answering to 53 bits without saying so is not an option */
		uint64_t a=18000000000000000000ULL; int32_t b=-1; int64_t out=0;
		r = dasval_binOp(DASOP_ADD, DASVT_ULONG,(uint8_t*)&a, DASVT_INT,(uint8_t*)&b,
		                    DASVT_LONG,(uint8_t*)&out);
		if(r == DAS_OKAY) FAIL("unrepresentable integer pair not caught");
	}{	/* the result is fine, the declared output is too narrow for it */
		int32_t a=70000, b=70000; int16_t out=0;
		r = dasval_binOp(DASOP_MUL, DASVT_INT,(uint8_t*)&a, DASVT_INT,(uint8_t*)&b,
		                    DASVT_SHORT,(uint8_t*)&out);
		if(r == DAS_OKAY) FAIL("narrow output not caught");
	}{	/* divide by zero is an overflow, not an inf handed downstream */
		double a=1.0, b=0.0, out=0.0;
		r = dasval_binOp(DASOP_DIV, DASVT_DOUBLE,(uint8_t*)&a, DASVT_DOUBLE,(uint8_t*)&b,
		                    DASVT_DOUBLE,(uint8_t*)&out);
		if(r == DAS_OKAY) FAIL("divide by zero not caught");
	}{	/* but an operand that arrived non-finite is the caller's business */
		double a=INFINITY, b=1.0, out=0.0;
		r = dasval_binOp(DASOP_ADD, DASVT_DOUBLE,(uint8_t*)&a, DASVT_DOUBLE,(uint8_t*)&b,
		                    DASVT_DOUBLE,(uint8_t*)&out);
		if(r != DAS_OKAY) FAIL("non-finite operand refused");
		if(!isinf(out)) FAIL("non-finite operand not passed through");
	}
}

static void test_binop_rejects(void)
{
	das_errcode r;
	double a=1.0, b=2.0, out=0.0;

	/* DASVT_TIME is refused: broken down calendar math mixes two value types */
	{
		DasTime dt; DasTime_clear(&dt);
		r = dasval_binOp(DASOP_ADD, DASVT_TIME,(uint8_t*)&dt, DASVT_DOUBLE,(uint8_t*)&b,
		                    DASVT_DOUBLE,(uint8_t*)&out);
		if(r == DAS_OKAY) FAIL("DASVT_TIME not refused");
	}
	{
		const char* s = "x";
		r = dasval_binOp(DASOP_ADD, DASVT_TEXT,(uint8_t*)&s, DASVT_DOUBLE,(uint8_t*)&b,
		                    DASVT_DOUBLE,(uint8_t*)&out);
		if(r == DAS_OKAY) FAIL("DASVT_TEXT not refused");
	}

	/* a unary code, and a code in the binary range that names no operator */
	r = dasval_binOp(DASOP_SQRT, DASVT_DOUBLE,(uint8_t*)&a, DASVT_DOUBLE,(uint8_t*)&b,
	                    DASVT_DOUBLE,(uint8_t*)&out);
	if(r == DAS_OKAY) FAIL("unary op code not refused");

	r = dasval_binOp(250, DASVT_DOUBLE,(uint8_t*)&a, DASVT_DOUBLE,(uint8_t*)&b,
	                    DASVT_DOUBLE,(uint8_t*)&out);
	if(r == DAS_OKAY) FAIL("unnamed op code not refused");

	r = dasval_binOp(DASOP_ADD, DASVT_DOUBLE, NULL, DASVT_DOUBLE,(uint8_t*)&b,
	                    DASVT_DOUBLE,(uint8_t*)&out);
	if(r == DAS_OKAY) FAIL("null left operand not refused");
	r = dasval_binOp(DASOP_ADD, DASVT_DOUBLE,(uint8_t*)&a, DASVT_DOUBLE,(uint8_t*)&b,
	                    DASVT_DOUBLE, NULL);
	if(r == DAS_OKAY) FAIL("null output not refused");
}

/* ************************************************************************* */
/* ************************************************************************* */
/* dasval_fromStr: text to 64-bit integers.

   The 64-bit cases once used sscanf "%Lu"/"%Ld" on hosts without a 64-bit
   long.  This is bad, don't do it. */

static void test_fromstr_int64(void)
{
	uint8_t aBuf[8];
	int64_t n = 0;
	uint64_t u = 0;

	memset(aBuf, 0x55, sizeof(aBuf));
	if(dasval_fromStr("-9007199254740993", DASVT_LONG, aBuf, sizeof(aBuf)) != DAS_OKAY)
		FAIL("DASVT_LONG parse of -(2^53+1) rejected");
	memcpy(&n, aBuf, 8);
	if(n != -9007199254740993LL)
		FAIL("DASVT_LONG -(2^53+1) stored as %lld", (long long)n);

	memset(aBuf, 0x55, sizeof(aBuf));
	if(dasval_fromStr("-9223372036854775808", DASVT_LONG, aBuf, sizeof(aBuf)) != DAS_OKAY)
		FAIL("DASVT_LONG parse of INT64_MIN rejected");
	memcpy(&n, aBuf, 8);
	if(n != INT64_MIN)
		FAIL("DASVT_LONG INT64_MIN stored as %lld", (long long)n);

	memset(aBuf, 0x55, sizeof(aBuf));
	if(dasval_fromStr("18446744073709551615", DASVT_ULONG, aBuf, sizeof(aBuf)) != DAS_OKAY)
		FAIL("DASVT_ULONG parse of UINT64_MAX rejected");
	memcpy(&u, aBuf, 8);
	if(u != UINT64_MAX)
		FAIL("DASVT_ULONG UINT64_MAX stored as %llu", (unsigned long long)u);
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
/* dasvt_merge: result types, operands in written order (left op right).

   Until 2026-11 the arguments ran backwards (header right-first, body
   left-first, callers right-first), so Time - Double came out DASVT_UNK and
   Double - Time came out DASVT_TIME, and the small-type test was lopsided
   (DASVT_UBYTE only on the left, DASVT_BYTE only on the right).  A failed merge
   also said DASVT_UNK, which means an application's own type; it is
   DASVT_NULL, "no value type", now. */

/* ************************************************************************* */
/* The csv writers and reader.  The writers once never moved their write
   point, so each value overwrote the last and no separator was written. */

static void test_csv(void)
{
	char sBuf[128];
	const double aD[] = {1.5, -2.25, 1e-7, 123456.0};
	if((dascsv_fromDoubles(aD, 4, sBuf, sizeof(sBuf)) == NULL) ||
	   (strcmp(sBuf, "1.500000, -2.250000, 1.000000e-07, 1.234560e+05") != 0))
		FAIL("dascsv_fromDoubles gave '%s'", sBuf);

	const float aF[] = {0.5f, 3.0f};
	if((dascsv_fromFloats(aF, 2, sBuf, sizeof(sBuf)) == NULL) ||
	   (strcmp(sBuf, "0.500000, 3.000000") != 0))
		FAIL("dascsv_fromFloats gave '%s'", sBuf);

	/* Too small is an error, and what did fit stays terminated */
	if(dascsv_fromDoubles(aD, 4, sBuf, 20) != NULL)
		FAIL("dascsv_fromDoubles overflow not caught");
	if(strcmp(sBuf, "1.500000, -2.250000") != 0)
		FAIL("dascsv_fromDoubles partial output '%s'", sBuf);

	/* Reading back: what the writer made, white space, blanks, bad fields */
	int n = -1;
	double* p = dascsv_newDoubles("1.500000, -2.250000, 1.000000e-07", &n);
	if((p == NULL)||(n != 3)||(p[0] != 1.5)||(p[1] != -2.25)||(p[2] != 1e-7))
		FAIL("dascsv_newDoubles round trip, %d items", n);
	free(p);

	p = dascsv_newDoubles("  2.5 ,3", &n);
	if((p == NULL)||(n != 2)||(p[0] != 2.5)||(p[1] != 3.0))
		FAIL("dascsv_newDoubles with spaces, %d items", n);
	free(p);

	const char* aBlank[] = {"", "   "};
	for(int i = 0; i < 2; ++i){
		p = dascsv_newDoubles(aBlank[i], &n);
		if((p == NULL)||(n != 0)) FAIL("dascsv_newDoubles('%s') is not an empty list", aBlank[i]);
		free(p);
	}

	const char* aBad[] = {"1, x", "1,,2", "4,"};
	for(int i = 0; i < 3; ++i){
		n = -1;
		if((dascsv_newDoubles(aBad[i], &n) != NULL)||(n != 0))
			FAIL("dascsv_newDoubles('%s') should fail", aBad[i]);
	}
	n = -1;
	if((dascsv_newDoubles(NULL, &n) != NULL)||(n != 0))
		FAIL("dascsv_newDoubles(NULL) should fail");
}

static void test_vt_merge(void)
{
	static const struct { das_vt vtL; int nOp; das_vt vtR;
	                      das_vt vtWant; } aCase[] = {
		/* time rules, both orders */
		{DASVT_TIME,   DASOP_SUB, DASVT_TIME,   DASVT_DOUBLE },
		{DASVT_TIME,   DASOP_ADD, DASVT_TIME,   DASVT_NULL   },
		{DASVT_TIME,   DASOP_SUB, DASVT_DOUBLE, DASVT_TIME   },
		{DASVT_TIME,   DASOP_ADD, DASVT_DOUBLE, DASVT_TIME   },
		{DASVT_DOUBLE, DASOP_ADD, DASVT_TIME,   DASVT_TIME   },
		{DASVT_DOUBLE, DASOP_SUB, DASVT_TIME,   DASVT_NULL   },
		{DASVT_TIME,   DASOP_MUL, DASVT_DOUBLE, DASVT_NULL   },
		{DASVT_TIME,   DASOP_ADD, DASVT_LONG,   DASVT_TIME   },  /* any number moves a time */
		{DASVT_ULONG,  DASOP_ADD, DASVT_TIME,   DASVT_TIME   },
		{DASVT_TIME,   DASOP_ADD, DASVT_UBYTE,  DASVT_TIME   },
		/* small types are symmetric now */
		{DASVT_UBYTE,  DASOP_ADD, DASVT_UBYTE,  DASVT_FLOAT  },
		{DASVT_BYTE,   DASOP_ADD, DASVT_BYTE,   DASVT_FLOAT  },
		{DASVT_UBYTE,  DASOP_MUL, DASVT_BYTE,   DASVT_FLOAT  },
		{DASVT_SHORT,  DASOP_MUL, DASVT_FLOAT,  DASVT_FLOAT  },
		{DASVT_FLOAT,  DASOP_MUL, DASVT_INT,    DASVT_DOUBLE },
		{DASVT_INT,    DASOP_DIV, DASVT_SHORT,  DASVT_DOUBLE },
		/* nothing to combine is DASVT_NULL, "no value type" */
		{DASVT_TEXT,   DASOP_ADD, DASVT_DOUBLE, DASVT_NULL   },
		{DASVT_DOUBLE, DASOP_ADD, DASVT_NULL,   DASVT_NULL   },
		{DASVT_INDEX,  DASOP_ADD, DASVT_INT,    DASVT_NULL   },
		{DASVT_BYTESEQ,DASOP_ADD, DASVT_INT,    DASVT_NULL   },
		/* an application's own type passes through as unknown */
		{DASVT_UNK,DASOP_ADD, DASVT_DOUBLE, DASVT_UNK},
		{DASVT_TIME,   DASOP_SUB, DASVT_UNK,DASVT_UNK},
	};
	for(size_t i = 0; i < sizeof(aCase)/sizeof(aCase[0]); ++i){
		das_vt vt = dasvt_merge(aCase[i].vtL, aCase[i].nOp, aCase[i].vtR);
		if(vt != aCase[i].vtWant)
			FAIL("dasvt_merge(%s, %s, %s) = %s, expected %s",
			     dasvt_name(aCase[i].vtL), dasop_name(aCase[i].nOp, NULL),
			     dasvt_name(aCase[i].vtR), dasvt_name(vt),
			     dasvt_name(aCase[i].vtWant));
	}
}

int main(int argc, char** argv)
{
	/* DASERRDISP_RET (not _EXIT): the guard tests deliberately drive error
	   returns, and the hook drops those expected messages.  A passing run is
	   silent; we assert on the return codes, not the text. */
	DasLogHook hook = {_dropErrors, NULL};
	daslib_init(argv[0], DASERRDISP_RET, DASLOG_INFO, &hook);

	test_accum_values();
	test_accum_guards();
	test_accum_rejects();

	test_binxform_table();
	test_binxform_wide();
	test_binxform_guards();

	test_binop_values();
	test_binop_guards();
	test_binop_rejects();

	test_fromstr_int64();

	test_vt_merge();
	test_csv();

	if(g_fails > 0){
		printf("ERROR: TestValue had %d failure(s)\n", g_fails);
		return 15;
	}
	printf("INFO: All value layer tests passed\n");
	return 0;
}
