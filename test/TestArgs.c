/** @file TestArgs.c Unit tests for the dasarg_ command line helpers */

/* Author: Chris Piker <chris-piker@uiowa.edu>, via Claude Opus 5.5
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

#include <stdio.h>
#include <string.h>
#include <stdbool.h>

#include <das3/core.h>

static int g_nFail = 0;
static int g_nPass = 0;

#define CHECK(cond, ...) do{ \
	if(cond){ ++g_nPass; } \
	else{ ++g_nFail; printf("  FAIL line %d: ", __LINE__); printf(__VA_ARGS__); printf("\n"); } \
}while(0)

static bool streq(const char* a, const char* b)
{
	if((a == NULL)||(b == NULL)) return (a == b);
	return strcmp(a, b) == 0;
}

#define ARGC(a) ((int)(sizeof(a)/sizeof(a[0])))

/* ************************************************************************* */
static void test_isOpt(void)
{
	printf("TEST: dasarg_isOpt\n");
	bool bLong = true;
	CHECK(dasarg_isOpt("-c", "-c", "--clean", &bLong) && !bLong, "short flag");
	CHECK(dasarg_isOpt("--clean", "-c", "--clean", &bLong) && bLong, "long flag");
	CHECK(!dasarg_isOpt("--cleanup", "-c", "--clean", NULL), "a flag is not a prefix");
	CHECK(!dasarg_isOpt("--clea", "-c", "--clean", NULL), "nor a shorter word");
	CHECK(!dasarg_isOpt("-cx", "-c", "--clean", NULL), "short forms match whole");
	CHECK(dasarg_isOpt("--log=info", "-l", "--log=", &bLong) && bLong, "joined value");
	CHECK(dasarg_isOpt("--log", "-l", "--log=", &bLong) && bLong, "value to follow");
	CHECK(!dasarg_isOpt("--logx", "-l", "--log=", NULL), "a longer word is not the option");
	CHECK(!dasarg_isOpt("--lo", "-l", "--log=", NULL), "nor a shorter one");
	CHECK(!dasarg_isOpt("info", "-l", "--log=", NULL), "a value is not an option");
}

/* ************************************************************************* */
static void test_optVal(void)
{
	printf("TEST: dasarg_optVal\n");
	const char* sVal = "junk";
	int i;

	char* a1[] = {"prog", "-l", "info", "--log=debug", "--log", "warn", "--log=", "-l"};
	i = 1;
	CHECK(dasarg_optVal(a1, ARGC(a1), "-l", "--log=", &i, &sVal) && streq(sVal, "info") && (i == 2),
	      "short form takes the next argument");
	i = 3;
	CHECK(dasarg_optVal(a1, ARGC(a1), "-l", "--log=", &i, &sVal) && streq(sVal, "debug") && (i == 3),
	      "joined long form stays put");
	CHECK(sVal == a1[3] + 6, "points into argv, no copy");
	i = 4;
	CHECK(dasarg_optVal(a1, ARGC(a1), "-l", "--log=", &i, &sVal) && streq(sVal, "warn") && (i == 5),
	      "spaced long form takes the next argument");
	i = 6;
	CHECK(dasarg_optVal(a1, ARGC(a1), "-l", "--log=", &i, &sVal) && (sVal == NULL) && (i == 6),
	      "'--log=' matches with no value");
	i = 7;
	CHECK(dasarg_optVal(a1, ARGC(a1), "-l", "--log=", &i, &sVal) && (sVal == NULL) && (i == 7),
	      "last on the line, no value");
	i = 2;
	sVal = "junk";
	CHECK(!dasarg_optVal(a1, ARGC(a1), "-l", "--log=", &i, &sVal) && (sVal == NULL) && (i == 2),
	      "not the option: false, NULL value, index untouched");

	/* A value may look like an option; it is taken as given, getopt does
	   the same for a required argument */
	char* a2[] = {"prog", "--seed", "-r"};
	i = 1;
	CHECK(dasarg_optVal(a2, ARGC(a2), "-s", "--seed=", &i, &sVal) && streq(sVal, "-r") && (i == 2),
	      "the next argument is the value whatever it looks like");

	/* Values with '=' in them, the mount form */
	char* a3[] = {"prog", "--mount=a.json=tag:x", "-m", "b.json=tag:y"};
	i = 1;
	CHECK(dasarg_optVal(a3, ARGC(a3), "-m", "--mount=", &i, &sVal) && streq(sVal, "a.json=tag:x"),
	      "only the first '=' ends the name");
	i = 2;
	CHECK(dasarg_optVal(a3, ARGC(a3), "-m", "--mount=", &i, &sVal) && streq(sVal, "b.json=tag:y"),
	      "short form with '=' in the value");
}

/* ************************************************************************* */
static void test_getOptVal(void)
{
	printf("TEST: dasarg_getOptVal\n");
	daslog_setLevel(DASLOG_CRITICAL);  /* the refusals below are expected */
	char sBuf[8];
	int i;

	char* a1[] = {"prog", "--log", "info", "-l", "debug", "--log=critical", "-o", "toolongvalue", "--log"};
	memset(sBuf, 'x', sizeof(sBuf));
	i = 1;
	CHECK(dasarg_getOptVal(a1, ARGC(a1), "-l", "--log=", &i, sBuf, sizeof(sBuf)) &&
	      streq(sBuf, "info") && (i == 2), "spaced long form, copied and terminated");
	i = 3;
	CHECK(dasarg_getOptVal(a1, ARGC(a1), "-l", "--log=", &i, sBuf, sizeof(sBuf)) &&
	      streq(sBuf, "debug") && (i == 4), "short form");
	i = 5;
	memset(sBuf, 'x', sizeof(sBuf));
	CHECK(dasarg_getOptVal(a1, ARGC(a1), "-l", "--log=", &i, sBuf, sizeof(sBuf)) == false,
	      "'critical' does not fit in 8 bytes");
	CHECK(sBuf[0] == '\0', "not cut, left empty");
	CHECK(i == 5, "index left on the option");

	i = 6;
	CHECK(dasarg_getOptVal(a1, ARGC(a1), "-o", "--output=", &i, sBuf, sizeof(sBuf)) == false,
	      "nor does 'toolongvalue', short form");
	CHECK(i == 6, "index back on the option");

	i = 8;
	CHECK(dasarg_getOptVal(a1, ARGC(a1), "-l", "--log=", &i, sBuf, sizeof(sBuf)) == false,
	      "missing value at the end");

	i = 2;
	strcpy(sBuf, "keep");
	CHECK(!dasarg_getOptVal(a1, ARGC(a1), "-l", "--log=", &i, sBuf, sizeof(sBuf)) && streq(sBuf, "keep"),
	      "not the option: the buffer is untouched, an earlier value survives");
	daslog_setLevel(DASLOG_WARN);
}

/* ************************************************************************* */
int main(int argc, char** argv)
{
	/* The refusals in test_getOptVal are reported with daserrf(), so
	   return them rather than exit */
	daslib_init(argv[0], DASERRDISP_RET, DASLOG_WARN, NULL);

	test_isOpt();
	test_optVal();
	test_getOptVal();

	printf("%d checks passed, %d failed\n", g_nPass, g_nFail);
	return (g_nFail == 0) ? 0 : 13;
}
