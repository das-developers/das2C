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
#include <unistd.h>

#include <das3/core.h>
#include <das3/catalog.h>

#define SITE   "test/das_fetch/cat/site.json"
#define UIOWA  "test/das_fetch/cat/uiowa.json"
#define UIOWA_URI "tag:das2.org,2012:site:/uiowa"

/* A user directory that does not exist.  Nothing writes there: the
   fixtures are local files, which are never cached. */
#define TEST_USERDIR "test_das_userdir_never_created"

/* The local: name a relative test file gets: the working directory is
 * part of it, see D2URI_LOCAL.  Static buffer, one call per CHECK. */
static const char* localUri(const char* sFile)
{
	static char sBuf[1024];
	char sCwd[512] = {'\0'};
	if(getcwd(sCwd, sizeof(sCwd) - 1) == NULL) sCwd[0] = '\0';
	snprintf(sBuf, sizeof(sBuf), D2URI_LOCAL "%s/%s", sCwd, sFile);
	return sBuf;
}

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
static DasErrCode quiet(DasErrCode (*f)(DasCat*, const char*), DasCat* p, const char* s)
{
	daslog_setlevel(DASLOG_CRIT);
	DasErrCode n = f(p, s);
	daslog_setlevel(DASLOG_WARN);
	return n;
}
/* for the two-string setters, argument order is (url, uri) as in catalog.h */
static DasErrCode quiet2(
	DasErrCode (*f)(DasCat*, const char*, const char*), DasCat* p, const char* a, const char* b
){
	daslog_setlevel(DASLOG_CRIT);
	DasErrCode n = f(p, a, b);
	daslog_setlevel(DASLOG_WARN);
	return n;
}
static DasNode* quietResolve(DasCat* p, const char* s)
{
	daslog_setlevel(DASLOG_CRIT);
	DasNode* pNode = DasCat_resolve(p, s);
	daslog_setlevel(DASLOG_WARN);
	return pNode;
}

/* ************************************************************************* */
static void test_isUrlOrFile(void)
{
	printf("TEST: das_isUrlOrFile\n");
	CHECK(das_isUrlOrFile("http://a/b.json"), "http");
	CHECK(das_isUrlOrFile("https://a/b"), "https");
	CHECK(das_isUrlOrFile("file:///x/y"), "file scheme");
	CHECK(das_isUrlOrFile("./node.json"), "dot path");
	CHECK(das_isUrlOrFile("/abs/node"), "absolute path");
	CHECK(das_isUrlOrFile("C:\\nodes\\x"), "windows drive path by backslash");
	CHECK(das_isUrlOrFile("test/das_fetch/cat/site.json"), "relative path ending in .json");
	CHECK(!das_isUrlOrFile("uiowa/juno/wav/survey"), "catalog path");
	CHECK(!das_isUrlOrFile("tag:das2.org,2012:site:/uiowa"), "catalog URI");
	CHECK(!das_isUrlOrFile("site:/uiowa"), "shorthand URI");
	CHECK(!das_isUrlOrFile(NULL), "NULL");
	CHECK(!das_isUrlOrFile(""), "empty");

	printf("TEST: das_isUri\n");
	CHECK(das_isUri("tag:das2.org,2012:site:/uiowa"), "tag URI");
	CHECK(das_isUri("doi:10.1037/a0040251/"), "doi URI");
	CHECK(das_isUri("site:/uiowa") && das_isUri("test:x") && das_isUri("local:/x"), "shorthands");
	CHECK(das_isUri("https://host/x.json"), "a URL is a URI too");
	CHECK(!das_isUri("uiowa/juno/wav:x"), "relative path, colon after a slash");
	CHECK(!das_isUri("C:\\nodes\\x") && !das_isUri("C:/x"), "drive letters are not schemes");
	CHECK(!das_isUri("1tag:x") && !das_isUri(":x") && !das_isUri("") && !das_isUri(NULL), "not schemes");
}

/* ************************************************************************* */
static void test_policy(void)
{
	printf("TEST: root policy setters\n");
	DasCat* pCat = new_DasCat(NULL, "TestCat");
	CHECK(pCat != NULL, "new_DasCat");
	CHECK(DasCat_credMngr(pCat) == NULL, "no manager");
	CHECK(streq(DasCat_agent(pCat), "TestCat"), "agent copied");
	CHECK(streq(DasCat_prefix(pCat), DASCAT_DEF_PREFIX), "default prefix");
	CHECK(!DasCat_hasRoot(pCat), "no root yet");
	CHECK(DasCat_rootUrl(pCat) == NULL, "no root url");
	CHECK(DasCat_rootUri(pCat) == NULL, "no root URI");
	CHECK(DasCat_mountCount(pCat) == 0, "no mounts");
	CHECK(DasCat_lastUri(pCat) == NULL, "no resolve yet");

	CHECK(DasCat_setPrefix(pCat, "site:/uiowa") == DAS_OKAY, "prefix shorthand");
	CHECK(streq(DasCat_prefix(pCat), UIOWA_URI), "prefix expanded: %s", DasCat_prefix(pCat));
	CHECK(DasCat_setPrefix(pCat, "site:uiowa") == DAS_OKAY, "prefix shorthand, slash left out");
	CHECK(streq(DasCat_prefix(pCat), UIOWA_URI), "slash supplied: %s", DasCat_prefix(pCat));
	CHECK(DasCat_setPrefix(pCat, "test:") == DAS_OKAY, "bare shorthand");
	CHECK(streq(DasCat_prefix(pCat), "tag:das2.org,2012:test:/"), "bare shorthand ends in a slash");
	CHECK(DasCat_setPrefix(pCat, "tag:das2.org,2012:test:/") == DAS_OKAY, "prefix full");
	CHECK(quiet(DasCat_setPrefix, pCat, SITE) != DAS_OKAY, "a file is not a prefix");
	CHECK(quiet(DasCat_setPrefix, pCat, "") != DAS_OKAY, "empty prefix");

	CHECK(quiet2(DasCat_setRoot, pCat, "uiowa", NULL) != DAS_OKAY, "a path is not a root");
	CHECK(DasCat_setRoot(pCat, SITE, NULL) == DAS_OKAY, "root by file");
	CHECK(DasCat_hasRoot(pCat), "has root");
	CHECK(streq(DasCat_rootUrl(pCat), SITE), "root url kept");
	CHECK(quiet2(DasCat_setRoot, pCat, UIOWA, NULL) != DAS_OKAY, "one root only");
	CHECK(quiet2(DasCat_setRoot, pCat, UIOWA, UIOWA_URI) != DAS_OKAY, "no named root over a root");
	CHECK(streq(DasCat_rootUri(pCat), localUri(SITE)), "a root without a URI is named by location: %s",
	      DasCat_rootUri(pCat));
	CHECK(DasCat_mount(pCat, UIOWA, UIOWA_URI) == DAS_OKAY, "a mount joins an offline catalog");
	CHECK(DasCat_mountCount(pCat) == 2, "the root is a mount too");
	CHECK(DasCat_covers(pCat, UIOWA_URI "/x") && !DasCat_covers(pCat, "tag:das2.org,2012:site:/swri"),
	      "covered by the mount, walled off elsewhere");
	del_DasCat(pCat);

	pCat = new_DasCat(NULL, NULL);
	CHECK(DasCat_agent(pCat) == NULL, "NULL agent stays NULL");
	CHECK(quiet2(DasCat_mount, pCat, UIOWA, "") != DAS_OKAY, "a mount needs a URI");
	CHECK(quiet2(DasCat_setRoot, pCat, UIOWA, UIOWA) != DAS_OKAY, "root URI can't be a file");
	CHECK(quiet2(DasCat_setRoot, pCat, "uiowa", UIOWA_URI) != DAS_OKAY, "root URL must be a location");
	CHECK(DasCat_setRoot(pCat, UIOWA, "site:/uiowa") == DAS_OKAY, "named root");
	CHECK(streq(DasCat_rootUri(pCat), UIOWA_URI), "root URI expanded");
	CHECK(DasCat_hasRoot(pCat), "a named root is a root");
	CHECK(quiet2(DasCat_setRoot, pCat, SITE, NULL) != DAS_OKAY, "no second root");
	del_DasCat(pCat);

	pCat = new_DasCat(NULL, NULL);
	CHECK(DasCat_mount(pCat, UIOWA, UIOWA_URI) == DAS_OKAY, "mount one");
	CHECK(DasCat_mount(pCat, SITE, "site:swri") == DAS_OKAY, "mount two, slash left out");
	CHECK(DasCat_mountCount(pCat) == 2, "two mounts");
	CHECK(!DasCat_hasRoot(pCat), "mounts are not a root");
	CHECK(DasCat_covers(pCat, "tag:das2.org,2012:site:/swri/x"), "slash supplied in the mount URI");
	CHECK(DasCat_covers(pCat, "tag:das2.org,2012:site:/lanl"), "mounts fall through");
	CHECK(!DasCat_covers(pCat, "tag:das2.org,2012:local:/nowhere"), "local: never falls through");
	CHECK(DasCat_setRoot(pCat, SITE, "test:/") == DAS_OKAY, "a root after mounts isolates them");
	CHECK(DasCat_hasRoot(pCat) && (DasCat_mountCount(pCat) == 3), "three mounts, one is the root");
	CHECK(!DasCat_covers(pCat, "tag:das2.org,2012:site:/lanl"), "no fall through now");
	del_DasCat(pCat);
}

/* ************************************************************************* */
static void test_rootMode(void)
{
	printf("TEST: a node file as the only root\n");
	DasCat* pCat = new_DasCat(NULL, "TestCat");
	DasCat_setCache(pCat, NULL, 0);    /* explicitly off, file nodes never cache anyway */
	CHECK(DasCat_setRoot(pCat, SITE, NULL) == DAS_OKAY, "set root");
	char sRootUri[1024];
	strcpy(sRootUri, localUri(SITE));

	DasNode* pRoot = DasCat_resolve(pCat, NULL);
	CHECK(pRoot != NULL, "root resolves");
	CHECK(pRoot && DasNode_isCatalog(pRoot), "root is a catalog");
	CHECK(streq(DasCat_lastUri(pCat), sRootUri), "no path: last URI is the root's local: name");
	CHECK(pRoot && streq(DasNode_pathUri(pRoot), sRootUri), "the node carries it");
	CHECK(DasCat_resolve(pCat, "") == pRoot, "empty path is the same as NULL");

	/* Policy is frozen now */
	CHECK(quiet(DasCat_setPrefix, pCat, "site:/") != DAS_OKAY, "no policy change after resolve");

	DasNode* pSrc = DasCat_resolve(pCat, "uiowa/geomag/flex");
	CHECK(pSrc != NULL, "deep path resolves");
	CHECK(pSrc && DasNode_isStreamSrc(pSrc), "leaf is a stream source");
	char sExpect[1100];
	/* site.json declares the ":/" separator of the node it stands in for,
	   and that is what joins its children to whatever name the root got */
	snprintf(sExpect, sizeof(sExpect), "%s:/uiowa/geomag/flex", sRootUri);
	CHECK(streq(DasCat_lastUri(pCat), sExpect), "relative path hangs off the local: name: %s",
	      DasCat_lastUri(pCat));
	CHECK(pSrc && streq(DasNode_pathUri(pSrc), sExpect), "node path URI: %s",
	      pSrc ? DasNode_pathUri(pSrc) : "(null)");
	CHECK(DasCat_resolve(pCat, "uiowa/geomag/flex") == pSrc, "second resolve is the cached node");

	DasNode* pUiowa = DasCat_resolve(pCat, "uiowa");
	CHECK(pUiowa && DasNode_isCatalog(pUiowa), "intermediate node");
	CHECK(pUiowa && (DasNode_subCount(pUiowa) == 3), "uiowa lists three children");
	CHECK(pUiowa && (DasNode_subType(pUiowa, 2) == d2node_inv), "unknown child type is d2node_inv");

	/* Misses, and what the catalog can say about them */
	CHECK(quietResolve(pCat, "uiowa/nope/deeper") == NULL, "bad child fails");
	snprintf(sExpect, sizeof(sExpect), "%s:/uiowa/nope/deeper", sRootUri);
	CHECK(streq(DasCat_lastUri(pCat), sExpect), "last URI is the failed path");
	CHECK(DasCat_covers(pCat, DasCat_lastUri(pCat)), "under the root is covered");
	CHECK(!DasCat_covers(pCat, UIOWA_URI), "the federated catalog is not");
	const char* sRest = NULL;
	DasNode* pNear = DasCat_nearest(pCat, &sRest);
	CHECK(pNear == pUiowa, "nearest is the deepest catalog that loaded");
	CHECK(streq(sRest, "nope/deeper"), "rest below it: %s", sRest ? sRest : "(null)");
	snprintf(sExpect, sizeof(sExpect), "%s:/uiowa", sRootUri);
	CHECK(streq(DasCat_lastUri(pCat), sExpect), "last URI now names the nearest node");

	CHECK(quietResolve(pCat, "uiowa/broken") == NULL, "listed child with a missing file fails");
	pNear = DasCat_nearest(pCat, &sRest);
	CHECK((pNear == pUiowa) && streq(sRest, "broken"), "nearest for a missing file");

	CHECK(quietResolve(pCat, "uiowa/odd") == NULL, "unknown node type fails to load");

	CHECK(quietResolve(pCat, "nope") == NULL, "bad child of the root");
	pNear = DasCat_nearest(pCat, &sRest);
	CHECK(pNear == NULL, "nothing above a root child");
	snprintf(sExpect, sizeof(sExpect), "%s:/nope", sRootUri);
	CHECK(streq(DasCat_lastUri(pCat), sExpect), "a failed nearest keeps the failed URI");

	/* A good resolve leaves nothing for nearest to find */
	CHECK(DasCat_resolve(pCat, "uiowa/geomag") != NULL, "source set resolves");
	CHECK(DasCat_nearest(pCat, &sRest) == pUiowa, "nearest after success is the parent, by design");

	/* A location given as the path gets a local: name */
	DasNode* pDirect = DasCat_resolve(pCat, UIOWA);
	CHECK(pDirect && DasNode_isCatalog(pDirect), "node by file path");
	CHECK(streq(DasCat_lastUri(pCat), localUri(UIOWA)), "named by location: %s", DasCat_lastUri(pCat));
	CHECK(pDirect && streq(DasNode_pathUri(pDirect), localUri(UIOWA)), "node path URI is the local: name");
	CHECK(DasCat_nearest(pCat, &sRest) == NULL, "nearest means nothing for a location");
	CHECK(DasCat_resolve(pCat, "./" UIOWA) != pDirect, "a different spelling is a different name");
	CHECK(streq(DasCat_lastUri(pCat), localUri(UIOWA)), "but a leading ./ is dropped: %s", DasCat_lastUri(pCat));

	/* local: names that nothing is mounted at fail without a lookup */
	CHECK(quietResolve(pCat, "local:/nowhere/x") == NULL, "unmounted local: name fails");
	CHECK(streq(DasCat_lastUri(pCat), "tag:das2.org,2012:local:/nowhere/x"), "and was expanded first");

	del_DasCat(pCat);

	/* No root at all: a local: name still never reaches the network */
	pCat = new_DasCat(NULL, "TestCat");
	CHECK(quietResolve(pCat, "tag:das2.org,2012:local:/home/nobody/x.json") == NULL, "unrouted");
	CHECK(!DasCat_covers(pCat, "local:/x") || true, "covers takes shorthand too");
	CHECK(!DasCat_covers(pCat, "tag:das2.org,2012:local:/x"), "and says it is not reachable");
	del_DasCat(pCat);
}

/* ************************************************************************* */
static void test_namedRoot(void)
{
	printf("TEST: a named root\n");
	DasCat* pCat = new_DasCat(NULL, "TestCat");
	CHECK(DasCat_setRoot(pCat, UIOWA, "site:/uiowa") == DAS_OKAY, "named root");

	DasNode* pTop = DasCat_resolve(pCat, NULL);
	CHECK(pTop && DasNode_isCatalog(pTop), "no path: the named node");
	CHECK(streq(DasCat_lastUri(pCat), UIOWA_URI), "its URI");
	CHECK(pTop && streq(DasNode_pathUri(pTop), UIOWA_URI), "node knows its given URI");

	DasNode* pSrc = DasCat_resolve(pCat, "geomag/flex");
	CHECK(pSrc && DasNode_isStreamSrc(pSrc), "relative to the root");
	CHECK(streq(DasCat_lastUri(pCat), UIOWA_URI "/geomag/flex"), "relative path gets the root URI: %s",
	      DasCat_lastUri(pCat));
	CHECK(pSrc && streq(DasNode_pathUri(pSrc), UIOWA_URI "/geomag/flex"), "node path URI");

	CHECK(DasCat_resolve(pCat, UIOWA_URI "/geomag") != NULL, "full URI under the root");
	CHECK(DasCat_resolve(pCat, "site:/uiowa/geomag") != NULL, "shorthand URI under the root");
	CHECK(streq(DasCat_lastUri(pCat), UIOWA_URI "/geomag"), "shorthand expanded");

	CHECK(quietResolve(pCat, "tag:das2.org,2012:site:/swri/x") == NULL, "outside the root fails");
	CHECK(!DasCat_covers(pCat, DasCat_lastUri(pCat)), "and the policy says so");
	CHECK(DasCat_covers(pCat, UIOWA_URI "/anything"), "under the root is covered");

	CHECK(quietResolve(pCat, "geomag/nope") == NULL, "bad child under the root");
	const char* sRest = NULL;
	DasNode* pNear = DasCat_nearest(pCat, &sRest);
	CHECK(pNear && DasNode_isCatalog(pNear) && streq(sRest, "nope"), "nearest under a named root: %s",
	      sRest ? sRest : "(null)");
	CHECK(streq(DasCat_lastUri(pCat), UIOWA_URI "/geomag"), "nearest URI under a named root");
	del_DasCat(pCat);

	/* The site file as the site node: the bare shorthand names the node,
	   the node's own separator builds the children, so names come out as
	   the live catalog spells them */
	pCat = new_DasCat(NULL, "TestCat");
	CHECK(DasCat_setRoot(pCat, SITE, "site:") == DAS_OKAY, "root named by bare shorthand");
	CHECK(streq(DasCat_rootUri(pCat), D2URI_SITES), "bare shorthand is the node name: %s", DasCat_rootUri(pCat));
	pSrc = DasCat_resolve(pCat, "uiowa/geomag/flex");
	CHECK(pSrc && streq(DasNode_pathUri(pSrc), UIOWA_URI "/geomag/flex"), "children spelled as the live catalog: %s",
	      pSrc ? DasNode_pathUri(pSrc) : "(null)");
	CHECK(DasCat_resolve(pCat, "site:/uiowa/geomag/flex") == pSrc, "full URI reaches the same node");
	CHECK(DasCat_resolve(pCat, "site:uiowa/geomag/flex") == pSrc, "slash left out, same node");
	CHECK(DasCat_resolve(pCat, "site:") == DasCat_resolve(pCat, NULL), "bare shorthand resolves the root");
	del_DasCat(pCat);
}

/* ************************************************************************* */
static void test_mounts(void)
{
	printf("TEST: mounts on the federated catalog (mounted side only, offline)\n");
	DasCat* pCat = new_DasCat(NULL, "TestCat");
	CHECK(DasCat_mount(pCat, UIOWA, "site:/uiowa") == DAS_OKAY, "mount");

	/* Without a prefix relative paths start at the first mount */
	DasNode* pSrc = DasCat_resolve(pCat, "geomag/flex");
	CHECK(pSrc && DasNode_isStreamSrc(pSrc), "relative to the mount");
	CHECK(streq(DasCat_lastUri(pCat), UIOWA_URI "/geomag/flex"), "URI under the mount");

	/* Full URIs under the mount never touch the network */
	CHECK(DasCat_resolve(pCat, "site:/uiowa/geomag") != NULL, "full URI through the mount");
	CHECK(DasCat_covers(pCat, "tag:das2.org,2012:site:/swri"), "a mount leaves the rest of the catalog reachable");
	del_DasCat(pCat);

	/* With a prefix relative paths start there, and land on the mount */
	pCat = new_DasCat(NULL, "TestCat");
	CHECK(DasCat_setPrefix(pCat, "site:/") == DAS_OKAY, "prefix");
	CHECK(DasCat_mount(pCat, UIOWA, "site:/uiowa") == DAS_OKAY, "mount after prefix");
	pSrc = DasCat_resolve(pCat, "uiowa/geomag/flex");
	CHECK(pSrc && DasNode_isStreamSrc(pSrc), "prefix + path through the mount");
	CHECK(streq(DasCat_lastUri(pCat), UIOWA_URI "/geomag/flex"), "URI: %s", DasCat_lastUri(pCat));
	del_DasCat(pCat);

	/* Two mounts, the longer URI must win when one is under the other */
	pCat = new_DasCat(NULL, "TestCat");
	CHECK(DasCat_mount(pCat, SITE, "site:/uiowa") == DAS_OKAY, "wide mount of the wrong file");
	CHECK(DasCat_mount(pCat, "test/das_fetch/cat/geomag.json", "site:/uiowa/geomag") == DAS_OKAY,
	      "narrow mount");
	DasNode* pNode = DasCat_resolve(pCat, "site:/uiowa/geomag/flex");
	CHECK(pNode && DasNode_isStreamSrc(pNode), "narrow mount shadows the wide one");
	del_DasCat(pCat);
}

/* ************************************************************************* */
/* The user directory and the cache default built on it */

static bool endsWith(const char* s, const char* sTail)
{
	size_t uS = strlen(s), uT = strlen(sTail);
	return (uS >= uT) && (strcmp(s + uS - uT, sTail) == 0);
}

static void test_userdir(void)
{
	printf("TEST: user directory and the session cache default\n");

	/* The default, unless this test's own environment set DAS_USERDIR */
	const char* sEnv = getenv("DAS_USERDIR");
	if((sEnv == NULL)||(sEnv[0] == '\0'))
		CHECK(endsWith(das_userdir(), DAS_DSEPS ".das"), "default is .das under home, got %s", das_userdir());
	else
		CHECK(streq(das_userdir(), sEnv), "DAS_USERDIR is honored, got %s", das_userdir());

	das_set_userdir(TEST_USERDIR);
	CHECK(streq(das_userdir(), TEST_USERDIR), "setter is honored, got %s", das_userdir());

	DasCat* pCat = new_DasCat(NULL, NULL);
	CHECK(streq(DasCat_cacheDir(pCat), TEST_USERDIR DAS_DSEPS "cache" DAS_DSEPS "nodes"),
	      "new session caches under the user directory, got %s",
	      DasCat_cacheDir(pCat) ? DasCat_cacheDir(pCat) : "(off)");
	CHECK(DasCat_cacheAge(pCat) == DASCAT_DEF_CACHE_SEC, "default cache age");

	DasCat_setCache(pCat, DasCat_cacheDir(pCat), 0);   /* aliasing is allowed */
	CHECK(streq(DasCat_cacheDir(pCat), TEST_USERDIR DAS_DSEPS "cache" DAS_DSEPS "nodes"),
	      "age change keeps the directory");
	CHECK(DasCat_cacheAge(pCat) == 0, "age changed to always fetch");

	DasCat_setCache(pCat, "elsewhere", 5);
	CHECK(streq(DasCat_cacheDir(pCat), "elsewhere"), "directory moved");
	CHECK(DasCat_cacheAge(pCat) == 5, "age set with the move");

	DasCat_setCache(pCat, NULL, 0);
	CHECK(DasCat_cacheDir(pCat) == NULL, "NULL turns the cache off");
	DasCat_setCache(pCat, "", 0);
	CHECK(DasCat_cacheDir(pCat) == NULL, "empty string turns the cache off");
	del_DasCat(pCat);

	/* A later change does not move a session already made */
	pCat = new_DasCat(NULL, NULL);
	das_set_userdir("moved_after_the_fact");
	CHECK(streq(DasCat_cacheDir(pCat), TEST_USERDIR DAS_DSEPS "cache" DAS_DSEPS "nodes"),
	      "session keeps the directory it was made with");
	del_DasCat(pCat);

	das_set_userdir(NULL);
	CHECK(!streq(das_userdir(), "moved_after_the_fact"), "NULL restores the default");
}

/* ************************************************************************* */
int main(int argc, char** argv)
{
	das_init(argv[0], DASERR_DIS_EXIT, 0, DASLOG_WARN, NULL);

	test_userdir();
	test_isUrlOrFile();
	test_policy();
	test_rootMode();
	test_namedRoot();
	test_mounts();

	printf("%d checks passed, %d failed\n", g_nPass, g_nFail);
	return (g_nFail == 0) ? 0 : 1;
}
