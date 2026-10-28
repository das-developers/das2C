/** @file TestWalk.c Unit tests for DasNodeWalk, driven by hand from
 * fixture files.  No network, no DasCat. */

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
 * test/das_walk/ refer to each other by paths from there.  This program
 * is the walk's driver, which is what a browser build is: it reads the
 * file a walk asks for and feeds it back, or reports that it could not.
 * Because the driver is in the test, the fallback paths that only a
 * misbehaving server would exercise are exercised here on purpose: a
 * missing mirror, a look-alike child, a document that is not JSON, a
 * child with nowhere to load from. */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>

#include <das3/core.h>
#include <das3/node.h>

static int g_nPass = 0;
static int g_nFail = 0;

#define CHECK(COND, ...) do{ \
	if(COND){ ++g_nPass; } \
	else{ ++g_nFail; printf("  FAIL %s:%d: ", __FILE__, __LINE__); printf(__VA_ARGS__); putchar('\n'); } \
}while(0)

static bool streq(const char* a, const char* b){ return (a != NULL) && (b != NULL) && (strcmp(a, b) == 0); }

#define ROOT_URL "test/das_walk/root.json"
#define ROOT_URI "tag:das2.org,2012:site:/walk"

/* ************************************************************************* */
/* The driver: files in, documents out */

static char* readFile(const char* sPath, size_t* pLen)
{
	FILE* pFile = fopen(sPath, "rb");
	if(pFile == NULL) return NULL;
	char* pBuf = NULL;
	size_t uCap = 0, uLen = 0;
	char sChunk[4096];
	size_t uRead;
	while((uRead = fread(sChunk, 1, sizeof(sChunk), pFile)) > 0){
		if(uLen + uRead > uCap){
			uCap = (uLen + uRead) * 2;
			pBuf = (char*)realloc(pBuf, uCap);
		}
		memcpy(pBuf + uLen, sChunk, uRead);
		uLen += uRead;
	}
	fclose(pFile);
	*pLen = uLen;
	return pBuf;
}

/* Records every URL asked for, in order, so a test can say what a walk
 * should have tried */
#define MAX_LOG 16
struct drive_log {
	const char* aUrl[MAX_LOG];
	size_t uUrls;
};

/* Answer one step with candidate uIdx: feed the file or report it missing */
static DasErrCode step(DasNodeWalk* pWalk, size_t uIdx, struct drive_log* pLog)
{
	const char* sUrl = DasNodeWalk_url(pWalk, uIdx);
	if((pLog != NULL) && (pLog->uUrls < MAX_LOG)) pLog->aUrl[pLog->uUrls++] = sUrl;
	size_t uLen = 0;
	char* pDoc = readFile(sUrl, &uLen);
	if(pDoc == NULL){
		DasNodeWalk_fail(pWalk, uIdx, "no such file");
		return DASERR_NOTIMP;   /* any code that is not DAS_OKAY or DASERR_NODE */
	}
	DasErrCode rc = DasNodeWalk_feed(pWalk, uIdx, pDoc, uLen, NULL);
	free(pDoc);
	return rc;
}

static DasNode* drive(DasNodeWalk* pWalk, struct drive_log* pLog)
{
	if(pLog != NULL) pLog->uUrls = 0;
	while(DasNodeWalk_state(pWalk) == das_walk_fetch) step(pWalk, 0, pLog);
	return DasNodeWalk_result(pWalk);
}

static bool logIs(const struct drive_log* pLog, size_t u, ...)
{
	if(pLog->uUrls != u) return false;
	va_list ap;
	va_start(ap, u);
	bool bOk = true;
	for(size_t i = 0; i < u; ++i){
		const char* s = va_arg(ap, const char*);
		if(!streq(pLog->aUrl[i], s)) bOk = false;
	}
	va_end(ap);
	return bOk;
}

static void showLog(const struct drive_log* pLog)
{
	printf("    asked for:");
	for(size_t i = 0; i < pLog->uUrls; ++i) printf(" %s", pLog->aUrl[i]);
	putchar('\n');
}

static DasNode* loadRoot(void)
{
	size_t uLen = 0;
	char* pDoc = readFile(ROOT_URL, &uLen);
	DasNode* pRoot = (pDoc == NULL) ? NULL : new_DasNode_json(pDoc, uLen, ROOT_URL, "site:/walk");
	free(pDoc);
	return pRoot;
}

/* ************************************************************************* */
static void test_json(void)
{
	printf("TEST: nodes from bytes\n");
	DasNode* pRoot = loadRoot();
	CHECK(pRoot != NULL, "root loads from bytes");
	if(pRoot == NULL) return;
	CHECK(DasNode_isCatalog(pRoot), "it is a catalog");
	CHECK(streq(DasNode_pathUri(pRoot), ROOT_URI), "shorthand expanded: %s", DasNode_pathUri(pRoot));
	CHECK(streq(DasNode_srcUrl(pRoot), ROOT_URL), "source URL kept");
	CHECK(DasNode_subCount(pRoot) == 5, "five children listed, none loaded");
	CHECK(DasNode_subType(pRoot, 4) == d2node_stream_src, "last child is a stream source");
	del_DasNode(pRoot);

	daslog_setlevel(DASLOG_CRIT);
	CHECK(new_DasNode_json("not json", 8, "x", NULL) == NULL, "garbage is not a node");
	CHECK(new_DasNode_json("{\"type\":\"Nope\"}", 15, "x", NULL) == NULL, "unknown type is not a node");
	daslog_setlevel(DASLOG_WARN);

	const char* sDoc = "{\"type\":\"HttpStreamSrc\",\"protocol\":{}}";
	DasNode* pNode = new_DasNode_json(sDoc, strlen(sDoc), NULL, NULL);
	CHECK(pNode && (DasNode_pathUri(pNode) == NULL), "nameless means no URI, a root");
	CHECK(pNode && streq(DasNode_srcUrl(pNode), ""), "no URL is the empty string");
	del_DasNode(pNode);
	pNode = new_DasNode_json(sDoc, strlen(sDoc), "http://host/x.json", NULL);
	CHECK(pNode && (DasNode_pathUri(pNode) == NULL), "a location is not a name either");
	CHECK(pNode && streq(DasNode_srcUrl(pNode), "http://host/x.json"), "but it is kept as the source URL");
	del_DasNode(pNode);
}

/* ************************************************************************* */
static void test_steps(void)
{
	printf("TEST: one step at a time\n");
	DasNode* pRoot = loadRoot();
	if(pRoot == NULL) return;

	DasNodeWalk* pWalk = new_DasNodeWalk(pRoot, "src");
	CHECK(pWalk != NULL, "walk starts");
	if(pWalk == NULL){ del_DasNode(pRoot); return; }
	CHECK(DasNodeWalk_state(pWalk) == das_walk_fetch, "a document is wanted");
	CHECK(DasNodeWalk_urlCount(pWalk) == 1, "one candidate");
	CHECK(streq(DasNodeWalk_url(pWalk, 0), "test/das_walk/src.json"), "the child's URL");
	CHECK(DasNodeWalk_url(pWalk, 1) == NULL, "past the end is NULL");
	CHECK(streq(DasNodeWalk_uri(pWalk), ROOT_URI "/src"), "the child's URI: %s", DasNodeWalk_uri(pWalk));
	CHECK(DasNodeWalk_result(pWalk) == NULL, "no result yet");
	CHECK(DasNodeWalk_nearest(pWalk, NULL) == NULL, "nothing nearest while running");

	daslog_setlevel(DASLOG_CRIT);
	CHECK(DasNodeWalk_feed(pWalk, 1, "{}", 2, NULL) != DAS_OKAY, "feeding a candidate not on offer is refused");
	daslog_setlevel(DASLOG_WARN);
	CHECK(DasNodeWalk_state(pWalk) == das_walk_fetch, "and changes nothing");

	CHECK(step(pWalk, 0, NULL) == DAS_OKAY, "the source document parses");
	CHECK(DasNodeWalk_state(pWalk) == das_walk_done, "walk is done");
	DasNode* pSrc = DasNodeWalk_result(pWalk);
	CHECK(pSrc != NULL, "result");
	CHECK(pSrc && DasNode_isStreamSrc(pSrc), "it is a stream source");
	CHECK(pSrc && streq(DasNode_pathUri(pSrc), ROOT_URI "/src"), "named by the walk");
	CHECK(pSrc && streq(DasNode_srcUrl(pSrc), "test/das_walk/src.json"), "loaded from the candidate");
	CHECK(DasNodeWalk_urlCount(pWalk) == 0, "nothing on offer when done");
	daslog_setlevel(DASLOG_CRIT);
	CHECK(DasNodeWalk_feed(pWalk, 0, "{}", 2, NULL) != DAS_OKAY, "a finished walk takes nothing");
	daslog_setlevel(DASLOG_WARN);
	del_DasNodeWalk(pWalk);

	/* The result was attached: the same path needs no document now */
	pWalk = new_DasNodeWalk(pRoot, "src");
	CHECK(pWalk && (DasNodeWalk_state(pWalk) == das_walk_done), "second walk is done at once");
	CHECK(pWalk && (DasNodeWalk_result(pWalk) == pSrc), "and finds the same node");
	del_DasNodeWalk(pWalk);
	pWalk = new_DasNodeWalk(pRoot, "/src");
	CHECK(pWalk && (DasNodeWalk_result(pWalk) == pSrc), "leading separator is accepted");
	del_DasNodeWalk(pWalk);
	pWalk = new_DasNodeWalk(pRoot, "");
	CHECK(pWalk && (DasNodeWalk_result(pWalk) == pRoot), "empty path is the start node");
	del_DasNodeWalk(pWalk);

	daslog_setlevel(DASLOG_CRIT);
	CHECK(new_DasNodeWalk(pSrc, "deeper") == NULL, "no walk below a source");
	daslog_setlevel(DASLOG_WARN);

	del_DasNode(pSrc);   /* a child: ignored, its parent owns it */
	CHECK(streq(DasNode_srcUrl(pSrc), "test/das_walk/src.json"), "a child is not deleted separately");
	del_DasNode(pRoot);
}

/* ************************************************************************* */
static void test_fallback(void)
{
	printf("TEST: mirrors, look-alikes and dead ends\n");
	struct drive_log log;
	DasNode* pRoot = loadRoot();
	if(pRoot == NULL) return;

	/* The first mirror of "a" does not exist */
	DasNodeWalk* pWalk = new_DasNodeWalk(pRoot, "a/b");
	CHECK(pWalk && (DasNodeWalk_urlCount(pWalk) == 2), "two mirrors on offer");
	DasNode* pB = drive(pWalk, &log);
	CHECK(pB != NULL, "a/b resolves through the second mirror");
	CHECK(logIs(&log, 3, "test/das_walk/missing.json", "test/das_walk/a.json", "test/das_walk/src.json"),
	      "missing mirror, good mirror, leaf");
	if(g_nFail) showLog(&log);
	CHECK(pB && streq(DasNode_pathUri(pB), ROOT_URI "/a/b"), "URI composed through two levels: %s",
	      pB ? DasNode_pathUri(pB) : "(null)");
	del_DasNodeWalk(pWalk);

	/* Both levels attached: "a" costs nothing now, and so does a/b */
	pWalk = new_DasNodeWalk(pRoot, "a");
	CHECK(pWalk && (DasNodeWalk_state(pWalk) == das_walk_done), "the intermediate node was kept");
	DasNode* pA = pWalk ? DasNodeWalk_result(pWalk) : NULL;
	del_DasNodeWalk(pWalk);
	pWalk = new_DasNodeWalk(pRoot, "a/b");
	CHECK(pWalk && (DasNodeWalk_result(pWalk) == pB), "and so was the leaf");
	del_DasNodeWalk(pWalk);
	pWalk = new_DasNodeWalk(pA, "b");
	CHECK(pWalk && (DasNodeWalk_result(pWalk) == pB), "a walk may start at the intermediate node");
	del_DasNodeWalk(pWalk);

	/* "a" is a prefix of "ab", so it is tried first and rejected by the
	   separator rule; the loaded copy of "a" is consulted and the listing
	   entry for it skipped, so no mirror of "a" is asked for again */
	pWalk = new_DasNodeWalk(pRoot, "ab/x");
	DasNode* pX = drive(pWalk, &log);
	CHECK(pX != NULL, "ab/x resolves");
	CHECK(logIs(&log, 2, "test/das_walk/ab.json", "test/das_walk/src.json"), "look-alike skipped via the loaded copy");
	if(g_nFail) showLog(&log);
	del_DasNodeWalk(pWalk);
	del_DasNode(pRoot);

	/* The same on a fresh tree: "a" has to be fetched to be rejected, and
	   since it led nowhere it is not kept */
	pRoot = loadRoot();
	pWalk = new_DasNodeWalk(pRoot, "ab/x");
	pX = drive(pWalk, &log);
	CHECK(pX && streq(DasNode_pathUri(pX), ROOT_URI "/ab/x"), "ab/x on a fresh tree");
	CHECK(logIs(&log, 4, "test/das_walk/missing.json", "test/das_walk/a.json", "test/das_walk/ab.json",
	      "test/das_walk/src.json"), "a was tried first, through both mirrors, then ab");
	if(g_nFail) showLog(&log);
	del_DasNodeWalk(pWalk);
	pWalk = new_DasNodeWalk(pRoot, "a");
	CHECK(pWalk && (DasNodeWalk_state(pWalk) == das_walk_fetch), "a dead end is not kept");
	del_DasNodeWalk(pWalk);

	/* A document that is not JSON: feed says so, the walk moves on */
	pWalk = new_DasNodeWalk(pRoot, "junk/x");
	daslog_setlevel(DASLOG_CRIT);
	DasErrCode rc = step(pWalk, 0, NULL);
	daslog_setlevel(DASLOG_WARN);
	CHECK(rc == DASERR_NODE, "junk is reported to the driver");
	CHECK(DasNodeWalk_state(pWalk) == das_walk_failed, "and nothing else to try");
	const char* sRest = NULL;
	CHECK(streq(DasNodeWalk_nearest(pWalk, &sRest), ROOT_URI) && streq(sRest, "junk/x"),
	      "nearest is the root with the whole path below it");
	del_DasNodeWalk(pWalk);

	/* Nothing listed, or listed with no URLs: failed before any step */
	daslog_setlevel(DASLOG_CRIT);
	pWalk = new_DasNodeWalk(pRoot, "nothere");
	CHECK(pWalk && (DasNodeWalk_state(pWalk) == das_walk_failed), "unknown child fails at once");
	CHECK(pWalk && (DasNodeWalk_urlCount(pWalk) == 0), "nothing on offer");
	del_DasNodeWalk(pWalk);
	pWalk = new_DasNodeWalk(pRoot, "nourls/x");
	CHECK(pWalk && (DasNodeWalk_state(pWalk) == das_walk_failed), "a child with no URLs fails at once");
	del_DasNodeWalk(pWalk);
	daslog_setlevel(DASLOG_WARN);

	/* A driver that fetched the mirrors in parallel feeds the one that
	   answered, skipping the ones before it */
	pWalk = new_DasNodeWalk(pRoot, "a/b");
	CHECK(pWalk && (DasNodeWalk_urlCount(pWalk) == 2), "two mirrors again on a fresh tree");
	CHECK(step(pWalk, 1, NULL) == DAS_OKAY, "the second mirror fed directly");
	CHECK(DasNodeWalk_state(pWalk) == das_walk_fetch, "on to the leaf");
	CHECK(streq(DasNodeWalk_uri(pWalk), ROOT_URI "/a/b"), "leaf URI");
	/* Abandon it here: the node loaded for "a" belongs to the walk and
	   must go with it, see valgrind */
	del_DasNodeWalk(pWalk);
	pWalk = new_DasNodeWalk(pRoot, "a");
	CHECK(pWalk && (DasNodeWalk_state(pWalk) == das_walk_fetch), "an abandoned walk attached nothing");
	del_DasNodeWalk(pWalk);

	del_DasNode(pRoot);
}

/* ************************************************************************* */
static void test_federated(void)
{
	printf("TEST: the compiled in seed\n");
	size_t uRoots = 0;
	const char** aRoots = das_seed_urls(&uRoots);

	daslog_setlevel(DASLOG_CRIT);
	CHECK(new_DasNodeWalk(NULL, "src") == NULL, "a walk needs a start node");
	daslog_setlevel(DASLOG_WARN);

	DasNodeWalk* pWalk = new_DasNodeWalk_seed();
	CHECK(pWalk != NULL, "seed walk starts");
	if(pWalk == NULL) return;
	CHECK(DasNodeWalk_state(pWalk) == das_walk_fetch, "a seed document is wanted");
	CHECK(DasNodeWalk_urlCount(pWalk) == uRoots, "every seed URL is a candidate");
	CHECK(streq(DasNodeWalk_url(pWalk, 0), aRoots[0]), "in order");
	CHECK(streq(DasNodeWalk_uri(pWalk), ""), "a seed has no URI");

	DasNodeWalk_fail(pWalk, 0, "no network in a unit test");
	CHECK(DasNodeWalk_urlCount(pWalk) == uRoots - 1, "one mirror fewer");
	CHECK(streq(DasNodeWalk_url(pWalk, 0), aRoots[1]), "the next mirror is first now");

	/* A seed must be a Catalog; a source document is dropped */
	size_t uLen = 0;
	char* pDoc = readFile("test/das_walk/src.json", &uLen);
	daslog_setlevel(DASLOG_CRIT);
	DasErrCode rc = DasNodeWalk_feed(pWalk, 0, pDoc, uLen, NULL);
	daslog_setlevel(DASLOG_WARN);
	free(pDoc);
	CHECK(rc == DASERR_NODE, "a source is not a seed");
	CHECK(DasNodeWalk_state(pWalk) == das_walk_failed, "no mirrors left");
	CHECK(DasNodeWalk_nearest(pWalk, NULL) == NULL, "nothing nearest when no seed loaded");
	del_DasNodeWalk(pWalk);

	/* A stand in seed that lists the fixture tree as the site: node, so
	   its children are site:/a, site:/src and so on.  The seed is the
	   caller's; everything else is a walk from it and stays under it. */
	pWalk = new_DasNodeWalk_seed();
	pDoc = readFile("test/das_walk/fedroot.json", &uLen);
	CHECK(DasNodeWalk_feed(pWalk, 0, pDoc, uLen, "https://mirror/index.json") == DAS_OKAY, "a stand in seed");
	free(pDoc);
	CHECK(DasNodeWalk_state(pWalk) == das_walk_done, "the seed is the result");
	DasNode* pRoot = DasNodeWalk_result(pWalk);
	del_DasNodeWalk(pWalk);
	CHECK(pRoot && DasNode_isCatalog(pRoot), "the seed outlives the walk");
	CHECK(pRoot && (DasNode_pathUri(pRoot) == NULL), "a seed with no prefix is a root, the one node with no URI");
	CHECK(pRoot && streq(DasNode_srcUrl(pRoot), aRoots[0]), "loaded from the first mirror, this walk never failed one");
	if(pRoot == NULL) return;

	pWalk = new_DasNodeWalk(pRoot, "tag:das2.org,2012:site:/src");
	CHECK(pWalk && (DasNodeWalk_state(pWalk) == das_walk_fetch), "a full URI walks from the seed");
	CHECK(pWalk && streq(DasNodeWalk_uri(pWalk), "tag:das2.org,2012:site:"), "first level named by the seed's prefix child: %s",
	      pWalk ? DasNodeWalk_uri(pWalk) : "(null)");
	CHECK(pWalk && streq(DasNodeWalk_url(pWalk, 0), ROOT_URL), "the fixture root stands in for site");
	DasNode* pSrc = pWalk ? drive(pWalk, NULL) : NULL;
	CHECK(pSrc && streq(DasNode_pathUri(pSrc), "tag:das2.org,2012:site:/src"), "full URI: %s",
	      pSrc ? DasNode_pathUri(pSrc) : "(null)");
	del_DasNodeWalk(pWalk);

	/* A sibling shares the levels already walked */
	struct drive_log log;
	pWalk = new_DasNodeWalk(pRoot, "tag:das2.org,2012:site:/a/b");
	DasNode* pB = pWalk ? drive(pWalk, &log) : NULL;
	CHECK(pB != NULL, "a sibling path resolves");
	CHECK(logIs(&log, 3, "test/das_walk/missing.json", "test/das_walk/a.json", "test/das_walk/src.json"),
	      "the site level was not fetched again");
	if(g_nFail) showLog(&log);
	del_DasNodeWalk(pWalk);

	del_DasNode(pRoot);   /* takes site, a, b and src with it */
}

/* ************************************************************************* */
int main(int argc, char** argv)
{
	das_init(argv[0], DASERR_DIS_EXIT, 0, DASLOG_WARN, NULL);

	test_json();
	test_steps();
	test_fallback();
	test_federated();

	printf("%d checks passed, %d failed\n", g_nPass, g_nFail);
	return (g_nFail == 0) ? 0 : 1;
}
