/** @file TestCat.c Unit tests for DasCat, the catalog session, over local
 * node files.  No network. */

/* Author: Chris Piker <chris-piker@uiowa.edu>, via Claude Fable 5.1
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

/* Runs from the top of the source tree, the fixture node files in
 * test/das_fetch/cat/ refer to each other by paths from there.  The tree
 * is site -> uiowa -> geomag -> flex, with two children under uiowa that
 * can be listed but not loaded. */

#define _POSIX_C_SOURCE 200112L

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>

#ifndef _WIN32
#include <unistd.h>
#endif

#include <das3/core.h>
#include <das3/catalog.h>

#define SITE   "test/das_fetch/cat/site.json"
#define DAS2   "test/das_fetch/cat/das.json"
#define UIOWA  "test/das_fetch/cat/uiowa.json"
#define UIOWA_URI "tag:das2.org,2012:site:/uiowa"

/* A user directory that does not exist.  Nothing writes there: the
   fixtures are local files, which are never cached. */
#define TEST_USERDIR "test_das_userdir_never_created"


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

/* Refusals log at error level by design; keep expected ones out of the
 * test output */
static das_errcode quiet(das_errcode (*f)(DasCat*, const char*), DasCat* p, const char* s)
{
	daslog_setLevel(DASLOG_CRITICAL);
	das_errcode n = f(p, s);
	daslog_setLevel(DASLOG_WARN);
	return n;
}
/* for the two-string setters, argument order is (url, uri) as in catalog.h */
static das_errcode quiet2(
	das_errcode (*f)(DasCat*, const char*, const char*), DasCat* p, const char* a, const char* b
){
	daslog_setLevel(DASLOG_CRITICAL);
	das_errcode n = f(p, a, b);
	daslog_setLevel(DASLOG_WARN);
	return n;
}

/* ************************************************************************* */
static void test_isUrlOrFile(void)
{
	printf("TEST: DasCat_isNodeUrl\n");
	CHECK(DasCat_isNodeUrl("http://a/b.json"), "http");
	CHECK(DasCat_isNodeUrl("https://a/b"), "https");
	CHECK(DasCat_isNodeUrl("file:///x/y"), "file scheme");
	CHECK(DasCat_isNodeUrl("./node.json"), "dot path");
	CHECK(DasCat_isNodeUrl("/abs/node"), "absolute path");
	CHECK(DasCat_isNodeUrl("C:\\nodes\\x"), "windows drive path by backslash");
	CHECK(DasCat_isNodeUrl("test/das_fetch/cat/site.json"), "relative path ending in .json");
	CHECK(!DasCat_isNodeUrl("uiowa/juno/wav/survey"), "catalog path");
	CHECK(!DasCat_isNodeUrl("tag:das2.org,2012:site:/uiowa"), "catalog URI");
	CHECK(!DasCat_isNodeUrl("tag:das2.org,2012:site:/uiowa"), "a catalog name");
	CHECK(!DasCat_isNodeUrl(NULL), "NULL");
	CHECK(!DasCat_isNodeUrl(""), "empty");

	printf("TEST: dasurl_hasScheme\n");
	CHECK(dasurl_hasScheme("tag:das2.org,2012:site:/uiowa"), "tag URI");
	CHECK(dasurl_hasScheme("doi:10.1037/a0040251/"), "doi URI");
	CHECK(dasurl_hasScheme("tag:das2.org,2012:site:/uiowa") && dasurl_hasScheme("test:x"), "any scheme");
	CHECK(dasurl_hasScheme("https://host/x.json"), "a URL is a URI too");
	CHECK(!dasurl_hasScheme("uiowa/juno/wav:x"), "relative path, colon after a slash");
	CHECK(!dasurl_hasScheme("C:\\nodes\\x") && !dasurl_hasScheme("C:/x"), "drive letters are not schemes");
	CHECK(!dasurl_hasScheme("1tag:x") && !dasurl_hasScheme(":x") && !dasurl_hasScheme("") && !dasurl_hasScheme(NULL), "not schemes");
}

/* ************************************************************************* */
static void test_policy(void)
{
	printf("TEST: seed and mount setters\n");
	DasCat* pCat = new_DasCat(NULL, "TestCat");
	CHECK(pCat != NULL, "new_DasCat");
	CHECK(DasCat_credMgr(pCat) == NULL, "no manager");
	CHECK(streq(DasCat_agent(pCat), "TestCat"), "agent copied");
	CHECK(streq(DasCat_seedUri(pCat), ""), "the compiled in seed is a root, paths are full names");
	CHECK(DasCat_seedUrl(pCat) == NULL, "the compiled in seed has no location of its own");
	CHECK(DasCat_numMounts(pCat) == 0, "no mounts");

	CHECK(quiet2(DasCat_setSeed, pCat, "uiowa", NULL) != DAS_OKAY, "a path is not a seed");
	CHECK(DasCat_setSeed(pCat, SITE, NULL) == DAS_OKAY, "seed by file");
	CHECK(streq(DasCat_seedUrl(pCat), SITE), "its location");
	CHECK(streq(DasCat_seedUri(pCat), ""), "a root seed: paths are names below it");
	CHECK(DasCat_numMounts(pCat) == 0, "the seed is not a mount");
	CHECK(DasCat_setSeed(pCat, UIOWA, NULL) == DAS_OKAY, "a second seed replaces the first");
	CHECK(streq(DasCat_seedUrl(pCat), UIOWA), "new location");
	CHECK(DasCat_mount(pCat, UIOWA, UIOWA_URI) == DAS_OKAY, "a mount joins an offline catalog");
	CHECK(DasCat_numMounts(pCat) == 1, "one mount");
	CHECK(DasCat_setSeed(pCat, NULL, DASNODE_URI_SITES) == DAS_OKAY, "a node of the das catalog, by name alone");
	CHECK(streq(DasCat_seedUri(pCat), DASNODE_URI_SITES), "its name");
	CHECK(DasCat_seedUrl(pCat) == NULL, "found through the root, no location of its own");
	CHECK(DasCat_covers(pCat, DASNODE_URI_UIOWA), "names under it are covered");
	CHECK(!DasCat_covers(pCat, "tag:cdaweb.gsfc.nasa.gov,2025:"), "the rest of the catalog is not");
	CHECK(quiet2(DasCat_setSeed, pCat, NULL, "./site.json") != DAS_OKAY, "a location is not a name");
	CHECK(DasCat_setSeed(pCat, NULL, "") == DAS_OKAY, "an empty name is no name");
	CHECK(streq(DasCat_seedUri(pCat), ""), "the compiled in root");
	CHECK(DasCat_setSeed(pCat, NULL, DASNODE_URI_SITES) == DAS_OKAY, "named again");
	CHECK(DasCat_setSeed(pCat, NULL, NULL) == DAS_OKAY, "back to the compiled in seed");
	CHECK(DasCat_seedUrl(pCat) == NULL, "no location");
	CHECK(streq(DasCat_seedUri(pCat), ""), "a root again");
	CHECK(DasCat_numMounts(pCat) == 1, "mounts are kept across seed changes");
	del_DasCat(pCat);

	pCat = new_DasCat(NULL, NULL);
	CHECK(DasCat_agent(pCat) == NULL, "NULL agent stays NULL");
	CHECK(quiet2(DasCat_mount, pCat, UIOWA, "") != DAS_OKAY, "a mount name can't be empty");
	CHECK(quiet2(DasCat_mount, pCat, UIOWA, NULL) != DAS_OKAY, "or NULL, a start point is a seed");
	CHECK(quiet2(DasCat_mount, pCat, "uiowa", UIOWA_URI) != DAS_OKAY, "a mount URL must be a location");
	CHECK(quiet2(DasCat_mount, pCat, UIOWA, UIOWA) != DAS_OKAY, "a mount name can't be a file");
	CHECK(quiet2(DasCat_setSeed, pCat, UIOWA, UIOWA) != DAS_OKAY, "seed name can't be a file");
	CHECK(DasCat_setSeed(pCat, UIOWA, UIOWA_URI) == DAS_OKAY, "seed with a name");
	CHECK(streq(DasCat_seedUri(pCat), UIOWA_URI), "where paths start");
	CHECK(DasCat_numMounts(pCat) == 0, "a named seed is not a mount");
	CHECK(DasCat_setSeed(pCat, SITE, "site") == DAS_OKAY, "a name is any string");
	CHECK(streq(DasCat_seedUri(pCat), "site"), "and is not expanded: %s", DasCat_seedUri(pCat));

	/* A name is taken as written; one that ends with the node's separator
	   is found out when the node loads, and every lookup through it says so */
	das_catmiss m;
	CHECK(DasCat_setSeed(pCat, UIOWA, "tag:das2.org,2012:site:/uiowa/") == DAS_OKAY, "with a trailing separator");
	CHECK(streq(DasCat_seedUri(pCat), UIOWA_URI "/"), "kept as written: %s", DasCat_seedUri(pCat));
	CHECK((DasCat_resolve(pCat, "geomag", &m) == NULL) && (m.nCause == DASMISS_BADNAME),
	      "relative lookups fail: %d %s", m.nCause, m.sMsg);
	CHECK(strstr(m.sMsg, "'" UIOWA_URI "'") != NULL, "naming the right name: %s", m.sMsg);
	CHECK((DasCat_seed(pCat, &m) == NULL) && (m.nCause == DASMISS_BADNAME), "and the seed itself");
	CHECK((DasCat_check(pCat, &m) == 1) && (m.nCause == DASMISS_BADNAME), "check() finds it");
	CHECK(DasCat_setSeed(pCat, SITE, "tag:das2.org,2012:site:/") == DAS_OKAY, "the site node, named with its separator");
	CHECK((DasCat_resolve(pCat, "uiowa", &m) == NULL) && (m.nCause == DASMISS_BADNAME) &&
	      strstr(m.sMsg, "':/'"), "found out by its own separator: %s", m.sMsg);
	const char* sUrl = NULL;
	const char* sUri = NULL;
	CHECK(!DasCat_mountAt(pCat, 0, &sUrl, &sUri) && (sUrl == NULL) && (sUri == NULL),
	      "past the end: false, and the outputs are written anyway");
	del_DasCat(pCat);

	pCat = new_DasCat(NULL, NULL);
	CHECK(DasCat_mount(pCat, UIOWA, UIOWA_URI) == DAS_OKAY, "mount one");
	CHECK(DasCat_mount(pCat, SITE, "tag:das2.org,2012:site:/swri") == DAS_OKAY, "mount two");
	CHECK(DasCat_numMounts(pCat) == 2, "two mounts");
	CHECK(DasCat_mountAt(pCat, 0, &sUrl, &sUri) && streq(sUrl, UIOWA) && streq(sUri, UIOWA_URI),
	      "listed in the order mounted");
	CHECK(DasCat_covers(pCat, "tag:das2.org,2012:site:/swri/x"), "under a mount");
	CHECK(DasCat_covers(pCat, "tag:das2.org,2012:site:/lanl"), "mounts fall through to the seed");
	CHECK(DasCat_setSeed(pCat, SITE, "tag:das2.org,2012:test") == DAS_OKAY, "a named seed after mounts");
	CHECK(DasCat_numMounts(pCat) == 2, "still two mounts");
	CHECK(!DasCat_covers(pCat, "tag:das2.org,2012:site:/lanl"), "outside the seed and the mounts");
	CHECK(DasCat_covers(pCat, UIOWA_URI "/x"), "the earlier mounts are still there");
	CHECK(DasCat_covers(pCat, "tag:das2.org,2012:test:/x"), "and under the seed");
	CHECK(DasCat_setSeed(pCat, SITE, NULL) == DAS_OKAY, "a root seed");
	CHECK(DasCat_covers(pCat, "tag:das2.org,2012:site:/lanl"), "is asked about everything");

	DasCat_setAgent(pCat, "later/2.0");
	CHECK(streq(DasCat_agent(pCat), "later/2.0"), "agent changed after construction");
	DasCat_setAgent(pCat, DasCat_agent(pCat));
	CHECK(streq(DasCat_agent(pCat), "later/2.0"), "own string back is fine");
	DasCat_setAgent(pCat, "");
	CHECK(DasCat_agent(pCat) == NULL, "empty agent is the library default");
	del_DasCat(pCat);
}

/* ************************************************************************* */
static void test_seed(void)
{
	printf("TEST: a node file as a root seed\n");
	DasCat* pCat = new_DasCat(NULL, "TestCat");
	DasCat_setCaching(pCat, false);    /* explicitly off, file nodes are not cached anyway */
	CHECK(DasCat_setSeed(pCat, SITE, NULL) == DAS_OKAY, "set seed");

	das_catmiss m;
	memset(&m, 0x55, sizeof(m));       /* garbage in, so a missed field shows */
	DasNode* pRoot = DasCat_resolve(pCat, NULL, &m);
	CHECK(pRoot != NULL, "root resolves");
	CHECK(pRoot && DasNode_isCatalog(pRoot), "root is a catalog");
	CHECK((m.nCause == DAS_OKAY) && streq(m.sUri, "") && (m.pNear == NULL) &&
	      streq(m.sMsg, "") && streq(m.sUrl, "") && streq(m.sRest, "") && (m.nHttp == 0),
	      "a success writes every field: URI empty, a root has none");
	CHECK(pRoot && (DasNode_pathUri(pRoot) == NULL), "the node has no URI");
	CHECK(DasCat_seed(pCat, &m) == pRoot, "the seed accessor is the same node");
	CHECK((m.nCause == DAS_OKAY) && streq(m.sUri, ""), "and says so");
	CHECK(DasCat_resolve(pCat, "", NULL) == pRoot, "empty path is the same as NULL");

	/* Mounts may change after a resolve */
	CHECK(DasCat_mount(pCat, UIOWA, "zz") == DAS_OKAY, "mount after resolve");
	CHECK(DasCat_unmount(pCat, "zz") == DAS_OKAY, "and unmount");

	DasNode* pSrc = DasCat_resolve(pCat, "uiowa/geomag/flex", &m);
	CHECK(pSrc != NULL, "deep path resolves");
	CHECK(pSrc && DasNode_isStreamSrc(pSrc), "leaf is a stream source");
	/* Below a root, names are the path from it: the root's ":/"
	   separator is not used since there is nothing to join to */
	CHECK(streq(m.sUri, "uiowa/geomag/flex"), "relative path is the name below a root: %s", m.sUri);
	CHECK(pSrc && streq(DasNode_pathUri(pSrc), "uiowa/geomag/flex"), "node path URI: %s",
	      pSrc ? DasNode_pathUri(pSrc) : "(null)");
	CHECK(DasCat_resolve(pCat, "uiowa/geomag/flex", NULL) == pSrc, "second resolve is the cached node");

	DasNode* pUiowa = DasCat_resolve(pCat, "uiowa", NULL);
	CHECK(pUiowa && DasNode_isCatalog(pUiowa), "intermediate node");
	CHECK(pUiowa && (DasNode_numSubs(pUiowa) == 3), "uiowa lists three children");
	CHECK(pUiowa && (DasNode_subTypeAt(pUiowa, 2) == DASNODETYPE_INV), "unknown child type is DASNODETYPE_INV");

	/* Misses, and what the session says about them */
	CHECK(DasCat_resolve(pCat, "uiowa/nope/deeper", &m) == NULL, "bad child fails");
	CHECK(m.nCause == DASMISS_NOCHILD, "a name that is not listed: %d", m.nCause);
	CHECK(streq(m.sUri, "uiowa/nope/deeper"), "the URI tried: %s", m.sUri);
	CHECK(m.pNear == pUiowa, "nearest is the deepest catalog that loaded");
	CHECK(streq(m.sRest, "nope/deeper"), "rest below it: %s", m.sRest);
	CHECK(strstr(m.sMsg, "'nope'") && strstr(m.sMsg, "geomag broken odd"),
	      "the message names the child and what is there: %s", m.sMsg);
	CHECK(DasCat_covers(pCat, m.sUri), "under a root is covered");

	CHECK(DasCat_resolve(pCat, "uiowa/broken", &m) == NULL, "listed child with a missing file fails");
	CHECK((m.nCause == DASMISS_LOAD) && (m.pNear == pUiowa) && streq(m.sRest, "broken"),
	      "a listed child that won't load: %d %s", m.nCause, m.sMsg);
	CHECK((m.sUrl[0] != '\0') && (m.nHttp == 0), "names the file, no HTTP: %s", m.sUrl);
	CHECK(strstr(m.sMsg, "listed but could not be loaded") != NULL, "says so: %s", m.sMsg);

	CHECK(DasCat_resolve(pCat, "uiowa/odd", &m) == NULL, "unknown node type fails to load");
	CHECK(m.nCause == DASMISS_UNSUPPORTED, "listed with a type das2C can't load: %s", m.sMsg);

	CHECK(DasCat_resolve(pCat, "nope", &m) == NULL, "bad child of the root");
	CHECK((m.pNear == pRoot) && streq(m.sRest, "nope"), "nearest for a root child is the root itself");
	CHECK(strstr(m.sMsg, "the root") != NULL, "a root is named as one: %s", m.sMsg);

	/* A good resolve after a bad one leaves nothing behind */
	CHECK(DasCat_resolve(pCat, "uiowa/geomag", &m) != NULL, "source set resolves");
	CHECK((m.nCause == DAS_OKAY) && (m.pNear == NULL) && streq(m.sMsg, ""), "success clears the miss");

	/* A child of a node in hand */
	CHECK(DasCat_subNode(pCat, pUiowa, "geomag", &m) != NULL, "subNode finds a child");
	CHECK(streq(m.sUri, "uiowa/geomag"), "and reports its URI: %s", m.sUri);
	CHECK(DasCat_subNode(pCat, pUiowa, "nope", &m) == NULL, "subNode miss");
	CHECK((m.nCause == DASMISS_NOCHILD) && (m.pNear == pUiowa), "with the node it started from");

	/* A location given as the path is a node of its own */
	DasNode* pDirect = DasCat_resolve(pCat, UIOWA, &m);
	CHECK(pDirect && DasNode_isCatalog(pDirect), "node by file path");
	CHECK(streq(m.sUri, UIOWA), "the location stands in for a name: %s", m.sUri);
	CHECK(pDirect && (DasNode_pathUri(pDirect) == NULL), "a node loaded by location has no URI");
	CHECK(DasCat_resolve(pCat, "./" UIOWA, NULL) != pDirect, "a different spelling is a different load");
	CHECK(DasCat_resolve(pCat, "test/das_fetch/cat/missing.json", &m) == NULL, "a missing file by location");
	CHECK((m.nCause == DASMISS_LOAD) && (m.pNear == NULL), "is a load failure with no nearest");

	/* Sources */
	das_catmiss ms;
	DasSrc* pDs = new_DasSrc_cat(pCat, "uiowa/geomag/flex", &ms);
	CHECK((pDs != NULL) && (ms.nCause == DAS_OKAY), "a source by path");
	del_DasSrc(pDs);
	CHECK(new_DasSrc_cat(pCat, "uiowa/geomag", &ms) == NULL, "a container is not a source");
	CHECK((ms.nCause == DASMISS_NOTSRC) && strstr(ms.sMsg, "flex"), "and names what it holds: %s", ms.sMsg);
	CHECK(new_DasSrc_cat(pCat, "uiowa/nope", &ms) == NULL, "a miss is a miss for sources too");
	CHECK(ms.nCause == DASMISS_NOCHILD, "the resolve's reason comes through");

	/* No struct: the reason goes to the log instead */
	daslog_setLevel(DASLOG_CRITICAL);
	CHECK(DasCat_resolve(pCat, "uiowa/nope", NULL) == NULL, "NULL miss is fine");
	CHECK(new_DasSrc_cat(pCat, "uiowa/geomag", NULL) == NULL, "for sources too");
	daslog_setLevel(DASLOG_WARN);

	del_DasCat(pCat);
}

/* ************************************************************************* */
static void test_prefixedSeed(void)
{
	printf("TEST: a seed with a name\n");
	DasCat* pCat = new_DasCat(NULL, "TestCat");
	CHECK(DasCat_setSeed(pCat, UIOWA, "tag:das2.org,2012:site:/uiowa") == DAS_OKAY, "seed with a name");

	das_catmiss m;
	DasNode* pTop = DasCat_resolve(pCat, NULL, &m);
	CHECK(pTop && DasNode_isCatalog(pTop), "no path: the named node");
	CHECK(streq(m.sUri, UIOWA_URI), "its URI");
	CHECK(pTop && streq(DasNode_pathUri(pTop), UIOWA_URI), "node knows its given URI");

	DasNode* pSrc = DasCat_resolve(pCat, "geomag/flex", &m);
	CHECK(pSrc && DasNode_isStreamSrc(pSrc), "relative to the seed");
	CHECK(streq(m.sUri, UIOWA_URI "/geomag/flex"), "the prefix names a node, its separator joins: %s", m.sUri);
	CHECK(pSrc && streq(DasNode_pathUri(pSrc), UIOWA_URI "/geomag/flex"), "node path URI");

	CHECK(DasCat_resolve(pCat, UIOWA_URI "/geomag", &m) != NULL, "full name under the seed");
	CHECK(streq(m.sUri, UIOWA_URI "/geomag"), "taken as it is");

	/* Only the seed's own name marks a full name; any other string is a
	   path below the seed, whatever it looks like */
	CHECK(DasCat_resolve(pCat, "tag:das2.org,2012:site:/swri/x", &m) == NULL, "another site's name");
	CHECK((m.nCause == DASMISS_NOCHILD) && streq(m.sUri, UIOWA_URI "/tag:das2.org,2012:site:/swri/x"),
	      "is a path below the seed: %s", m.sUri);
	CHECK(DasCat_resolve(pCat, UIOWA_URI "x/y", &m) == NULL, "a look-alike of the seed's name");
	CHECK(m.nCause == DASMISS_OUTSIDE, "is outside it: %s", m.sMsg);
	CHECK(!DasCat_covers(pCat, "tag:das2.org,2012:site:/swri/x"), "covers() goes by names alone");
	CHECK(DasCat_covers(pCat, UIOWA_URI "/anything"), "under the seed is covered");

	CHECK(DasCat_resolve(pCat, "geomag/nope", &m) == NULL, "bad child under the seed");
	CHECK(m.pNear && DasNode_isCatalog(m.pNear) && streq(m.sRest, "nope"), "nearest under a named seed: %s",
	      m.sRest);
	CHECK(m.pNear && streq(DasNode_pathUri(m.pNear), UIOWA_URI "/geomag"), "nearest is named");
	del_DasCat(pCat);

	/* The site file as the site node: the node's separator builds the
	   children, so names come out as the live catalog spells them */
	pCat = new_DasCat(NULL, "TestCat");
	CHECK(DasCat_setSeed(pCat, SITE, DASNODE_URI_SITES) == DAS_OKAY, "seed named as the site node");
	pSrc = DasCat_resolve(pCat, "uiowa/geomag/flex", NULL);
	CHECK(pSrc && streq(DasNode_pathUri(pSrc), UIOWA_URI "/geomag/flex"), "children spelled as the live catalog: %s",
	      pSrc ? DasNode_pathUri(pSrc) : "(null)");
	CHECK(DasCat_resolve(pCat, "tag:das2.org,2012:site:/uiowa/geomag/flex", NULL) == pSrc, "full URI reaches the same node");
	das_catmiss m2;
	CHECK((DasCat_resolve(pCat, DASNODE_URI_SITES ":uiowa/geomag/flex", &m2) == NULL) &&
	      (m2.nCause == DASMISS_OUTSIDE), "slash left out is another name: %s", m2.sMsg);
	CHECK(strstr(m2.sMsg, "its separator is ':/'") != NULL, "and the message says which: %s", m2.sMsg);
	CHECK(DasCat_resolve(pCat, DASNODE_URI_SITES, NULL) == DasCat_resolve(pCat, NULL, NULL),
	      "the seed's name resolves the seed");
	del_DasCat(pCat);

	/* das.json as the seed, named as the das2 node: its separator is null,
	   so "site:/uiowa" below it is tag:das2.org,2012:site:/uiowa, the
	   short form das3py writes, and full names work too */
	pCat = new_DasCat(NULL, "TestCat");
	CHECK(DasCat_setSeed(pCat, DAS2, DASNODE_URI_ROOT) == DAS_OKAY, "the das2 node as seed");
	pSrc = DasCat_resolve(pCat, "site:/uiowa/geomag/flex", &m2);
	CHECK(pSrc && streq(DasNode_pathUri(pSrc), UIOWA_URI "/geomag/flex"), "short form: %s", m2.sMsg);
	CHECK(DasCat_resolve(pCat, UIOWA_URI "/geomag/flex", NULL) == pSrc, "full name, same node");
	del_DasCat(pCat);
}

/* ************************************************************************* */
static void test_mountUnderSeed(void)
{
	printf("TEST: mounts in a root's name space\n");
	const char* sGeomag = "test/das_fetch/geomag_flex_0.7.json";
	das_catmiss m;

	/* A root: names are paths from it, and a mount at one of them is
	   consulted before the listing, however the path is spelled */
	DasCat* pCat = new_DasCat(NULL, "TestCat");
	CHECK(DasCat_setSeed(pCat, SITE, NULL) == DAS_OKAY, "root seed");
	CHECK(DasCat_mount(pCat, sGeomag, "uiowa/broken") == DAS_OKAY, "mount at a path from the root");
	DasNode* pNode = DasCat_resolve(pCat, "uiowa/broken", &m);
	CHECK(pNode && DasNode_isStreamSrc(pNode), "the mount shadows a listed child whose file is missing");
	CHECK(streq(m.sUri, "uiowa/broken"), "named by the path: %s", m.sUri);
	CHECK(pNode && streq(DasNode_pathUri(pNode), "uiowa/broken"), "the node carries the mount name");
	CHECK(DasCat_resolve(pCat, "uiowa/broken", NULL) == pNode, "and is kept");
	DasNode* pFlex = DasCat_resolve(pCat, "uiowa/geomag/flex", NULL);
	CHECK(pFlex && DasNode_isStreamSrc(pFlex) && (pFlex != pNode), "paths off the mount still walk the root");
	CHECK(DasCat_covers(pCat, "uiowa/broken/deeper"), "under the mount is covered");
	del_DasCat(pCat);

	/* A named seed: the relative spelling and the full URI reach the
	   same mount */
	pCat = new_DasCat(NULL, "TestCat");
	CHECK(DasCat_setSeed(pCat, UIOWA, "tag:das2.org,2012:site:/uiowa") == DAS_OKAY, "named seed");
	CHECK(DasCat_mount(pCat, sGeomag, "tag:das2.org,2012:site:/uiowa/broken") == DAS_OKAY, "mount under it");
	pNode = DasCat_resolve(pCat, "broken", &m);
	CHECK(pNode && DasNode_isStreamSrc(pNode), "relative path through the mount");
	CHECK(streq(m.sUri, UIOWA_URI "/broken"), "composed name: %s", m.sUri);
	CHECK(DasCat_resolve(pCat, UIOWA_URI "/broken", NULL) == pNode, "full URI is the same node");
	del_DasCat(pCat);
}

/* ************************************************************************* */
static void test_mounts(void)
{
	printf("TEST: mounts on the federated catalog (mounted side only, offline)\n");
	das_catmiss m;
	DasCat* pCat = new_DasCat(NULL, "TestCat");
	CHECK(DasCat_mount(pCat, UIOWA, "tag:das2.org,2012:site:/uiowa") == DAS_OKAY, "mount");

	/* With the compiled in seed, a root, paths are full names.  Paths
	   relative to a node are DasCat_subNode()'s job, joined by that
	   node's own separator */
	DasNode* pTop = DasCat_resolve(pCat, UIOWA_URI, &m);
	CHECK(pTop && DasNode_isCatalog(pTop), "the mount by its name: %s", m.sMsg);
	DasNode* pSrc = pTop ? DasCat_subNode(pCat, pTop, "geomag/flex", &m) : NULL;
	CHECK(pSrc && DasNode_isStreamSrc(pSrc), "relative to it");
	CHECK(streq(m.sUri, UIOWA_URI "/geomag/flex"), "URI under the mount: %s", m.sUri);

	/* A name ending with a separator names nothing */
	CHECK((DasCat_resolve(pCat, UIOWA_URI "/", &m) == NULL) && (m.nCause == DASMISS_NOCHILD),
	      "names nothing: %d %s", m.nCause, m.sMsg);
	CHECK(strstr(m.sMsg, "ends with the separator") != NULL, "and says why: %s", m.sMsg);

	/* Full URIs under the mount don't touch the network */
	CHECK(DasCat_resolve(pCat, "tag:das2.org,2012:site:/uiowa/geomag", NULL) != NULL, "full URI through the mount");
	CHECK(DasCat_covers(pCat, "tag:das2.org,2012:site:/swri"), "a mount leaves the rest of the catalog reachable");
	del_DasCat(pCat);

	/* Two mounts, the longer URI must win when one is under the other */
	pCat = new_DasCat(NULL, "TestCat");
	CHECK(DasCat_mount(pCat, SITE, "tag:das2.org,2012:site:/uiowa") == DAS_OKAY, "wide mount of the wrong file");
	CHECK(DasCat_mount(pCat, "test/das_fetch/cat/geomag.json", "tag:das2.org,2012:site:/uiowa/geomag") == DAS_OKAY,
	      "narrow mount");
	DasNode* pNode = DasCat_resolve(pCat, "tag:das2.org,2012:site:/uiowa/geomag/flex", NULL);
	CHECK(pNode && DasNode_isStreamSrc(pNode), "narrow mount shadows the wide one");
	del_DasCat(pCat);
}

/* ************************************************************************* */
static void test_change(void)
{
	printf("TEST: changing the seed and mounts after lookups\n");
	das_catmiss m;
	DasCat* pCat = new_DasCat(NULL, "TestCat");
	CHECK(DasCat_setSeed(pCat, SITE, NULL) == DAS_OKAY, "a file as a root seed");
	DasNode* pFlex = DasCat_resolve(pCat, "uiowa/geomag/flex", &m);
	CHECK(pFlex && DasNode_isStreamSrc(pFlex), "resolves below it");

	/* The same branch as a named seed: the root above it can't be reached */
	CHECK(DasCat_setSeed(pCat, UIOWA, "tag:das2.org,2012:site:/uiowa") == DAS_OKAY, "replace the seed after a lookup");
	CHECK(DasNode_isStreamSrc(pFlex) && streq(DasNode_pathUri(pFlex), "uiowa/geomag/flex"),
	      "a node from before the change is still good");
	DasNode* pNode = DasCat_resolve(pCat, "geomag/flex", &m);
	CHECK(pNode && (pNode != pFlex) && streq(m.sUri, UIOWA_URI "/geomag/flex"),
	      "relative paths start at the new seed: %s", m.sUri);
	CHECK(DasCat_resolve(pCat, "tag:das2.org,2012:site:/swri", &m) == NULL, "another name, not mounted");
	CHECK(m.nCause == DASMISS_NOCHILD, "is a path below the seed: %s", m.sMsg);
	CHECK(strstr(m.sMsg, UIOWA_URI) != NULL, "naming the seed: %s", m.sMsg);
	CHECK(DasCat_seed(pCat, &m) == DasCat_resolve(pCat, NULL, NULL), "seed() is the named node");
	CHECK(streq(m.sUri, UIOWA_URI), "by its name: %s", m.sUri);

	/* A mount outside the seed's name makes that branch reachable, and
	   does not move where relative paths start */
	CHECK(DasCat_mount(pCat, SITE, "tag:das2.org,2012:site:/swri") == DAS_OKAY, "mount beside the seed");
	CHECK(DasCat_resolve(pCat, "tag:das2.org,2012:site:/swri", &m) != NULL, "now reachable");
	CHECK(DasCat_resolve(pCat, "geomag/flex", &m) == pNode, "relative paths are unmoved");

	/* Replaced at the same name, then removed by name */
	CHECK(DasCat_mount(pCat, UIOWA, "tag:das2.org,2012:site:/swri") == DAS_OKAY, "remount at the same name");
	const char* sUrl = NULL;
	CHECK((DasCat_numMounts(pCat) == 1) && DasCat_mountAt(pCat, 0, &sUrl, NULL) && streq(sUrl, UIOWA),
	      "replaced, not added");
	CHECK(DasCat_unmount(pCat, "tag:das2.org,2012:site:/swri") == DAS_OKAY, "unmount by name");
	CHECK(DasCat_numMounts(pCat) == 0, "gone");
	CHECK(DasCat_resolve(pCat, "tag:das2.org,2012:site:/swri", &m) == NULL, "outside again");
	CHECK(quiet(DasCat_unmount, pCat, "tag:das2.org,2012:site:/swri") != DAS_OKAY, "not mounted is a program error");
	CHECK(quiet(DasCat_unmount, pCat, NULL) != DAS_OKAY, "the seed is not a mount");
	CHECK(quiet(DasCat_unmount, pCat, UIOWA) != DAS_OKAY, "not even by its location");
	CHECK(streq(DasCat_seedUrl(pCat), UIOWA), "and is still there");

	/* By location: every mount from it */
	CHECK(DasCat_mount(pCat, UIOWA, "tag:das2.org,2012:site:/a") == DAS_OKAY, "mount a");
	CHECK(DasCat_mount(pCat, UIOWA, "tag:das2.org,2012:site:/b") == DAS_OKAY, "mount b, same file");
	CHECK(DasCat_mount(pCat, SITE, "tag:das2.org,2012:site:/c") == DAS_OKAY, "mount c, another file");
	CHECK(quiet(DasCat_unmount, pCat, DASNODE_URI_SITES) != DAS_OKAY, "the site node itself is not mounted");
	CHECK(DasCat_numMounts(pCat) == 3, "a name matches exactly, site:/ is not a parent of the three");
	CHECK(DasCat_unmount(pCat, UIOWA) == DAS_OKAY, "unmount by location");
	const char* sUri = NULL;
	CHECK((DasCat_numMounts(pCat) == 1) && DasCat_mountAt(pCat, 0, NULL, &sUri) &&
	      streq(sUri, "tag:das2.org,2012:site:/c"), "only the other file's mount is left: %s", sUri ? sUri : "");
	CHECK(streq(DasCat_seedUrl(pCat), UIOWA), "the seed from that file stays");

	CHECK(DasCat_setSeed(pCat, NULL, NULL) == DAS_OKAY, "back to the compiled in seed");
	CHECK(streq(DasCat_seedUri(pCat), ""), "a root again, paths are full names");
	del_DasCat(pCat);
}

/* ************************************************************************* */
/* Fixtures from test/das_walk/: root lists a and ab, look-alikes with "/"
 * separators, and src, a source; dots lists a and ab again under ".". */
#define W_ROOT "test/das_walk/root.json"
#define W_A    "test/das_walk/a.json"
#define W_AB   "test/das_walk/ab.json"
#define W_SRC  "test/das_walk/src.json"
#define W_DOTS "test/das_walk/dots.json"
#define W_LONG "test/das_walk/longsep.json"
#define DOI    "doi:12457:tracers.tracers-1"

static const char* srcOf(const DasNode* p){ return p ? DasNode_srcUrl(p) : "(none)"; }

static void test_boundary(void)
{
	printf("TEST: a name is under a node only when the node's separator follows\n");
	das_catmiss m;
	DasNode* pNode;

	/* The seed: a name that only starts like the seed's is outside it */
	DasCat* pCat = new_DasCat(NULL, "TestCat");
	CHECK(DasCat_setSeed(pCat, W_DOTS, DOI) == DAS_OKAY, "a seed named by a DOI");
	pNode = DasCat_resolve(pCat, DOI ".a/b", &m);
	CHECK(streq(srcOf(pNode), W_SRC), "through the seed's '.' then a child's '/': %s", m.sMsg);
	pNode = DasCat_resolve(pCat, "ab/x", &m);
	CHECK(pNode && streq(m.sUri, DOI ".ab/x"), "relative, joined with the seed's separator: %s", m.sUri);
	pNode = DasCat_resolve(pCat, DOI "a/b", &m);
	CHECK(!pNode && (m.nCause == DASMISS_OUTSIDE), "the separator left out names something else: %s", m.sMsg);
	pNode = DasCat_resolve(pCat, DOI "0", &m);
	CHECK(!pNode && (m.nCause == DASMISS_OUTSIDE), "a longer name is not under it: %s", m.sMsg);
	CHECK(DasCat_covers(pCat, DOI "0"), "covers() goes by the name alone, it loads nothing");
	del_DasCat(pCat);

	/* A mount's look-alike is asked of the seed */
	pCat = new_DasCat(NULL, "TestCat");
	DasCat_setSeed(pCat, W_ROOT, NULL);
	DasCat_mount(pCat, W_A, "a");
	pNode = DasCat_resolve(pCat, "a/b", &m);
	CHECK(streq(srcOf(pNode), W_SRC), "under the mount: %s", m.sMsg);
	pNode = DasCat_resolve(pCat, "ab/x", &m);
	CHECK(streq(srcOf(pNode), W_SRC), "a name that only starts like the mount's goes to the seed: %s", m.sMsg);
	del_DasCat(pCat);

	/* An inner mount answers alone for what is under it, whatever the
	   outer one has loaded */
	pCat = new_DasCat(NULL, "TestCat");
	DasCat_setSeed(pCat, W_ROOT, NULL);
	DasCat_mount(pCat, W_ROOT, "m:r");
	DasCat_mount(pCat, W_AB, "m:r/a");
	CHECK(DasCat_resolve(pCat, "m:r/src", NULL) != NULL, "the outer mount, loaded first");
	pNode = DasCat_resolve(pCat, "m:r/a", &m);
	CHECK(streq(srcOf(pNode), W_AB), "the inner mount by name, not the outer's child: %s", srcOf(pNode));
	pNode = DasCat_resolve(pCat, "m:r/a/b", &m);
	CHECK(!pNode && (m.nCause == DASMISS_NOCHILD), "the outer's a/b is shadowed: %s", m.sMsg);
	CHECK(DasCat_resolve(pCat, "m:r/a/x", NULL) != NULL, "the inner tree's own child");
	pNode = DasCat_resolve(pCat, "m:r/ab/x", &m);
	CHECK(pNode != NULL, "the inner mount's look-alike goes to the outer: %s", m.sMsg);
	del_DasCat(pCat);

	/* A source has no separator: more of a name passes it by, anything
	   else is under it, where nothing is */
	pCat = new_DasCat(NULL, "TestCat");
	DasCat_setSeed(pCat, W_ROOT, NULL);
	DasCat_mount(pCat, W_SRC, "a");
	pNode = DasCat_resolve(pCat, "a", &m);
	CHECK(pNode && DasNode_isStreamSrc(pNode), "a source mounted over a catalog");
	pNode = DasCat_resolve(pCat, "a/b", &m);
	CHECK(!pNode && (m.nCause == DASMISS_NOCHILD), "nothing under it, the catalog's a/b is shadowed: %s",
	      m.sMsg);
	pNode = DasCat_resolve(pCat, "ab/x", &m);
	CHECK(streq(srcOf(pNode), W_SRC), "more of a name passes it by: %s", m.sMsg);
	del_DasCat(pCat);

	/* A mount name is taken as written, a separator on the end included,
	   and is found out when the node loads */
	pCat = new_DasCat(NULL, "TestCat");
	DasCat_setSeed(pCat, W_ROOT, NULL);
	CHECK(DasCat_mount(pCat, W_AB, "mx:x/") == DAS_OKAY, "mount with a trailing slash");
	const char* sUri = NULL;
	CHECK(DasCat_mountAt(pCat, 0, NULL, &sUri) && streq(sUri, "mx:x/"), "kept as written: %s", sUri);
	pNode = DasCat_resolve(pCat, "mx:x/x", &m);
	CHECK(!pNode && (m.nCause == DASMISS_BADNAME), "lookups under it say so: %d %s", m.nCause, m.sMsg);
	CHECK((DasCat_check(pCat, &m) == 1) && (m.nCause == DASMISS_BADNAME), "check() finds it: %s", m.sMsg);
	CHECK(quiet(DasCat_unmount, pCat, "mx:x") != DAS_OKAY, "unmounted only by the name given");
	CHECK(DasCat_unmount(pCat, "mx:x/") == DAS_OKAY, "which works");
	del_DasCat(pCat);

	/* A separator longer than 7 characters is refused, not cut short */
	pCat = new_DasCat(NULL, "TestCat");
	CHECK(DasCat_setSeed(pCat, W_LONG, "m:long") == DAS_OKAY, "a seed with a long separator");
	daslog_setLevel(DASLOG_CRITICAL);
	pNode = DasCat_seed(pCat, &m);
	daslog_setLevel(DASLOG_WARN);
	CHECK(!pNode && (m.nCause == DASMISS_PARSE), "is not a usable node: %d %s", m.nCause, m.sMsg);
	del_DasCat(pCat);
}

/* ************************************************************************* */
static void test_check(void)
{
	printf("TEST: DasCat_check and a seed that won't load\n");
	das_catmiss m;
	DasCat* pCat = new_DasCat(NULL, "TestCat");
	DasCat_setSeed(pCat, SITE, NULL);
	DasCat_mount(pCat, UIOWA, "tag:das2.org,2012:site:/uiowa");
	CHECK(DasCat_check(pCat, &m) == 0, "a good seed and a good mount");
	CHECK(m.nCause == DAS_OKAY, "nothing to report");

	DasCat_mount(pCat, "test/das_fetch/cat/missing.json", "tag:das2.org,2012:site:/zz");
	CHECK(DasCat_check(pCat, &m) == 1, "one bad mount");
	CHECK((m.nCause == DASMISS_LOAD) && strstr(m.sUrl, "missing.json"), "is the first problem: %s", m.sMsg);
	daslog_setLevel(DASLOG_CRITICAL);
	CHECK(DasCat_check(pCat, NULL) == 1, "NULL logs instead");
	daslog_setLevel(DASLOG_WARN);

	DasCat_setSeed(pCat, "test/das_fetch/cat/missing.json", NULL);
	CHECK(DasCat_check(pCat, &m) == 2, "a bad seed counts too");
	CHECK(m.nCause == DASMISS_LOAD, "and is found first: %s", m.sMsg);
	CHECK(strstr(m.sMsg, "seed") != NULL, "naming it the seed: %s", m.sMsg);
	CHECK(DasCat_seed(pCat, &m) == NULL, "seed() fails the same way");
	CHECK((m.nCause == DASMISS_LOAD) && strstr(m.sUrl, "missing.json"), "naming the file: %s", m.sUrl);
	CHECK(DasCat_resolve(pCat, "uiowa/geomag", &m) == NULL, "a path below a bad seed");
	CHECK(m.nCause == DASMISS_LOAD, "fails for the seed's reason: %s", m.sMsg);
	CHECK(DasCat_resolve(pCat, "tag:das2.org,2012:site:/uiowa/geomag", &m) != NULL, "a mount still works");
	del_DasCat(pCat);
}

/* ************************************************************************* */
/* The user directory and the opt-in cache built on it */

static bool endsWith(const char* s, const char* sTail)
{
	size_t uS = strlen(s), uT = strlen(sTail);
	return (uS >= uT) && (strcmp(s + uS - uT, sTail) == 0);
}

static void test_userdir(void)
{
	printf("TEST: user directory and the opt-in node cache\n");

	/* The default, unless this test's environment set DAS_USERDIR */
	const char* sEnv = getenv("DAS_USERDIR");
	if((sEnv == NULL)||(sEnv[0] == '\0'))
		CHECK(endsWith(dasfs_userDir(), DASFS_SEPS ".das"), "default is .das under home, got %s", dasfs_userDir());
	else
		CHECK(streq(dasfs_userDir(), sEnv), "DAS_USERDIR is honored, got %s", dasfs_userDir());

	dasfs_setUserDir(TEST_USERDIR);
	CHECK(streq(dasfs_userDir(), TEST_USERDIR), "setter is honored, got %s", dasfs_userDir());

#define DEF_DIR TEST_USERDIR DASFS_SEPS "cache" DASFS_SEPS "nodes"
	DasCat* pCat = new_DasCat(NULL, NULL);
	CHECK(!DasCat_caching(pCat), "new session writes no files");
	CHECK(DasCat_cacheDir(pCat) == NULL, "and has no directory chosen, got %s",
	      DasCat_cacheDir(pCat) ? DasCat_cacheDir(pCat) : "(none)");
	CHECK(DasCat_cacheAge(pCat) == DASCAT_NODE_TTL_SEC, "default age before anything is set");
	CHECK(DasCat_setCaching(pCat, false) == DAS_OKAY, "turning off what is off is fine");

	CHECK(DasCat_setCaching(pCat, true) == DAS_OKAY, "one call turns caching on");
	CHECK(DasCat_caching(pCat), "and it reads back on");
	CHECK(streq(DasCat_cacheDir(pCat), DEF_DIR), "default directory under the user directory, got %s",
	      DasCat_cacheDir(pCat) ? DasCat_cacheDir(pCat) : "(none)");

	DasCat_setCacheAge(pCat, 0);
	CHECK((DasCat_cacheAge(pCat) == 0) && streq(DasCat_cacheDir(pCat), DEF_DIR),
	      "age changes alone");

	CHECK(DasCat_setCacheDir(pCat, "elsewhere") == DAS_OKAY, "directory moved while on");
	CHECK(streq(DasCat_cacheDir(pCat), "elsewhere") && DasCat_caching(pCat) &&
	      (DasCat_cacheAge(pCat) == 0), "move keeps caching on and the age");
	CHECK(DasCat_setCacheDir(pCat, DasCat_cacheDir(pCat)) == DAS_OKAY, "own buffer back is fine");

	DasCat_setCaching(pCat, false);
	CHECK(!DasCat_caching(pCat) && streq(DasCat_cacheDir(pCat), "elsewhere"),
	      "off keeps the directory, it is not the off signal");
	DasCat_setCaching(pCat, true);
	CHECK(streq(DasCat_cacheDir(pCat), "elsewhere"), "back on where it left off");

	CHECK(DasCat_setCacheDir(pCat, NULL) == DAS_OKAY, "NULL goes back to the default");
	CHECK(streq(DasCat_cacheDir(pCat), DEF_DIR), "default picked now, got %s", DasCat_cacheDir(pCat));

	/* Logged and returned, not raised: this test runs in DASERRDISP_EXIT */
	char sLong[400];
	memset(sLong, 'd', sizeof(sLong) - 1);
	sLong[sizeof(sLong) - 1] = '\0';
	daslog_setLevel(DASLOG_CRITICAL);
	CHECK(DasCat_setCacheDir(pCat, sLong) == DASERR_CAT, "too long a directory is refused");
	CHECK(DasCat_setCacheDir(pCat, "") == DASERR_CAT, "an empty directory is refused");
	daslog_setLevel(DASLOG_WARN);
	CHECK(streq(DasCat_cacheDir(pCat), DEF_DIR) && DasCat_caching(pCat),
	      "a refused directory leaves the setting as it was");
	del_DasCat(pCat);

	/* Directory chosen first, then turned on: the default is not picked */
	pCat = new_DasCat(NULL, NULL);
	DasCat_setCacheDir(pCat, "mine");
	DasCat_setCaching(pCat, true);
	CHECK(streq(DasCat_cacheDir(pCat), "mine"), "a chosen directory wins at turn on");
	del_DasCat(pCat);

	/* A user directory too long for the default: caching stays off */
	dasfs_setUserDir(sLong);
	pCat = new_DasCat(NULL, NULL);
	daslog_setLevel(DASLOG_CRITICAL);
	CHECK(DasCat_setCaching(pCat, true) == DASERR_CAT, "default too long is refused");
	daslog_setLevel(DASLOG_WARN);
	CHECK(!DasCat_caching(pCat) && (DasCat_cacheDir(pCat) == NULL), "and caching stays off");
	del_DasCat(pCat);
	dasfs_setUserDir(TEST_USERDIR);

	/* A later change does not move a session already turned on */
	pCat = new_DasCat(NULL, NULL);
	DasCat_setCaching(pCat, true);
	dasfs_setUserDir("moved_after_the_fact");
	CHECK(streq(DasCat_cacheDir(pCat), DEF_DIR), "session keeps the directory it chose");
	del_DasCat(pCat);
#undef DEF_DIR

	dasfs_setUserDir(NULL);
	CHECK(!streq(dasfs_userDir(), "moved_after_the_fact"), "NULL restores the default");
}

/* ************************************************************************* */
int main(int argc, char** argv)
{
	daslib_init(argv[0], DASERRDISP_EXIT, DASLOG_WARN, NULL);

	test_userdir();
	test_isUrlOrFile();
	test_policy();
	test_seed();
	test_prefixedSeed();
	test_mountUnderSeed();
	test_mounts();
	test_change();
	test_boundary();
	test_check();

	printf("%d checks passed, %d failed\n", g_nPass, g_nFail);
	return (g_nFail == 0) ? 0 : 1;
}
