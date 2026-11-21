/* Copyright (C) 2026 Chris Piker <chris-piker@uiowa.edu>
 *
 * This file is part of das2C, the Core Das C Library.
 *
 * Das2C is free software; you can redistribute it and/or modify it under
 * the terms of the GNU Lesser General Public License version 2.1 as published
 * by the Free Software Foundation.
 *
 * Das2C is distributed in the hope that it will be useful, but WITHOUT ANY
 * WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS
 * FOR A PARTICULAR PURPOSE.  See the GNU Lesser General Public License for
 * more details.
 *
 * You should have received a copy of the GNU Lesser General Public License
 * version 2.1 along with das2C; if not, see <http://www.gnu.org/licenses/>.
 */

/* A catalog session.  Grew up as a pilot inside utilities/das_fetch.c and
 * moved here once a second client wanted the same rules for where paths
 * start.
 * Policy misuse is reported with daslog_error and DASERR_CAT rather than
 * daserrf, because the usual source of a bad policy is a command line
 * argument and a program should get to decide its exit code for that. */

#define _POSIX_C_SOURCE 200112L

#include <errno.h>
#include <stdarg.h>
#include <string.h>
#include <stdlib.h>
#include <ctype.h>
#include <stdint.h>
#include <inttypes.h>
#include <time.h>
#include <sys/stat.h>

#include "util.h"
#include "str.h"
#include "log.h"
#include "http_p.h"
#include "node_p.h"
#include "source.h"
#include "catalog.h"

/* Where this session keeps copies of nodes fetched over HTTP and how long
 * it trusts them.  nMaxAgeSec of zero or less means always ask the server,
 * see _dascat_fetch().  The walk code takes a pointer to this, NULL when
 * caching is off, see _DasCat_cache(). */
typedef struct das_node_cache {
	char sDir[256];         /* "" until chosen */
	int  nMaxAgeSec;
	bool bOn;
} DasNodeCache;

struct das_cat_mount {
	char* sUrl;
	char* sUri;
	DasNode* pTop;          /* loaded on first use, kept in aOwned; NULL
	                           again when the mount is replaced */
};

struct das_cat_t {
	DasCredMgr* pMgr;      /* owned */
	char* sAgent;           /* owned copy, or NULL for the library default */
	DasNodeCache cache;     /* settings kept while off, see DasCat_setCaching() */

	/* The seed and the mounts may change at any time.  A
	 * change only drops the session's pointer to the tree it affects: the
	 * tree stays in aOwned until del_DasCat(), since the program may hold
	 * nodes from it.  Changes are rare, so the memory that costs is not
	 * worth reference counting. */
	char* sSeedUrl;         /* owned, a file seed's location, NULL when the seed
	                           comes from the compiled in root */
	char* sSeedUri;         /* owned, the seed's name, NULL for a root */
	DasNode* pSeed;         /* a named or file seed, found on first use; for one
	                           named alone it is part of pRoot's tree */
	DasNode* pRoot;         /* the das catalog root, loaded on first use and kept
	                           while a file seed is set, so going back to the
	                           compiled in seed costs nothing; the tree below it
	                           is the session's memory of the federated catalog */
	struct das_cat_mount* aMounts;  /* named nodes loaded by location */
	size_t uMounts;

	DasNode** aOwned;       /* every tree top the session holds, freed with it */
	size_t uOwned;
	size_t uOwnedCap;
};

/* The settings the walk code reads, or NULL when caching is off */
static const DasNodeCache* _DasCat_cache(const DasCat* pThis)
{
	return pThis->cache.bOn ? &(pThis->cache) : NULL;
}

/* Why the last document a walk or a load could not use was no good.  The
 * driver fills it, and it is read when a failed lookup is explained to
 * the caller, see _DasCat_explain().  With mirrors, only the last failure
 * is kept; any of them would do to say what went wrong. */
struct dascat_fail {
	int nCause;                    /* 0, DASMISS_LOAD or DASMISS_PARSE */
	int nHttp;                     /* as das_catmiss.nHttp */
	char sUrl[DASCAT_MISS_URI_SZ];
	char sWhy[256];
};

/* Defined further down, used throughout */
static void _dascat_cpy(char* sDest, size_t uDest, const char* sSrc);
static DasNode* _dascat_loadUrl(
	const char* sUrl, const char* sPathUri, DasCredMgr* pMgr, const char* sAgent,
	const DasNodeCache* pCache, struct dascat_fail* pFail
);

/* ************************************************************************* */
/* Loading nodes: the native driver
 *
 * node.c doesn't read files or open sockets.  It parses the documents it
 * is handed and, through DasNodeWalk, says which document it needs next.
 * What follows is the driver for programs on a real machine: it reads
 * files, keeps the node cache, makes the HTTP requests, and loops a walk
 * until it stops.  A client that fetches for itself, a browser build in
 * particular, just use node.h alone and exclude this file. */

/* ************************************************************************* */
/* Local file nodes.
 *
 * Catalog nodes are not only served over HTTP.  Test fixtures, detached
 * catalogs under development, and 'file://' URLs handed over by other
 * tools all want a node read straight from disk.  Anything that is not an http(s) URL
 * is treated as a path; the "file://" prefix is accepted and stripped.
 * Only the path forms "file:///abs/path" and "file://rel/path" are handled,
 * host names in file URLs are not. */

static bool _dascat_isFileUrl(const char* sUrl)
{
	if(sUrl == NULL) return false;
	if(strncmp(sUrl, "http://", 7) == 0) return false;
	if(strncmp(sUrl, "https://", 8) == 0) return false;
	return true;
}

/* sWhy, when not NULL, receives the reason for a NULL return.  Only debug
 * logging here: a node file that is not there is reported to the caller
 * of the lookup, not to the log. */
static DasAry* _dascat_readFile(const char* sUrl, char* sWhy, size_t uWhy)
{
	const char* sPath = sUrl;
	if(strncmp(sUrl, "file://", 7) == 0) sPath += 7;

	FILE* pFile = fopen(sPath, "rb");
	if(pFile == NULL){
		if(sWhy != NULL) snprintf(sWhy, uWhy, "Couldn't open %s, %s", sPath, strerror(errno));
		daslog_debugf("Couldn't open node file %s, %s", sPath, strerror(errno));
		return NULL;
	}

	DasAry* pAry = new_DasAry("node_file", DASVT_UBYTE, 1, (const uint8_t[1]){0}, DAS_SHAPE1(0), DASUNITS_DIMENSIONLESS);
	char buf[16384];
	size_t uRead = 0;
	while((uRead = fread(buf, 1, sizeof(buf), pFile)) > 0){
		if(!DasAry_append(pAry, (const uint8_t*)buf, uRead)){
			dec_DasAry(pAry);
			fclose(pFile);
			return NULL;
		}
	}
	bool bErr = (ferror(pFile) != 0);
	fclose(pFile);
	if(bErr){
		if(sWhy != NULL) snprintf(sWhy, uWhy, "Error reading %s", sPath);
		daslog_debugf("Error reading node file %s", sPath);
		dec_DasAry(pAry);
		return NULL;
	}
	return pAry;
}

/* ************************************************************************* */
/* Node cache */

/* Only a DasCat has cache settings, nodes made through the sessionless
 * constructors in node.h aren't cached.  One knob, per session, instead
 * of a second process wide one to confuse it with. */

/* Cache file name for a URL: the URL minus its scheme with everything
 * outside [A-Za-z0-9.-] turned into '_', then a hash of the whole URL so
 * that two URLs which sanitize alike still get separate files.  Readable
 * names make an 'ls' of the cache directory useful when debugging a
 * catalog, a bare hash would not. */
static bool _dascat_cachePath(
	const DasNodeCache* pCache, const char* sUrl, char* sPath, size_t uLen
){
	if((pCache == NULL)||(pCache->sDir[0] == '\0')) return false;

	uint32_t uHash = 2166136261u;   /* FNV-1a, no need for anything stronger */
	for(const char* p = sUrl; *p != '\0'; ++p){
		uHash ^= (unsigned char)(*p);
		uHash *= 16777619u;
	}

	const char* p = sUrl;
	if(strncmp(p, "https://", 8) == 0) p += 8;
	else if(strncmp(p, "http://", 7) == 0) p += 7;

	char sName[160];
	size_t u = 0;
	for(; (*p != '\0') && (u < sizeof(sName) - 1); ++p){
		char c = *p;
		sName[u++] = (isalnum((unsigned char)c)||(c == '.')||(c == '-')) ? c : '_';
	}
	sName[u] = '\0';

	int n = snprintf(sPath, uLen, "%s%s%s_%08" PRIx32 ".json", pCache->sDir, DASFS_SEPS, sName, uHash);
	return (n > 0) && ((size_t)n < uLen);
}

/* A fresh enough cached copy, or NULL */
static DasAry* _dascat_readCache(const DasNodeCache* pCache, const char* sPath)
{
	if((pCache == NULL)||(pCache->nMaxAgeSec <= 0)) return NULL;
	struct stat st;
	if(stat(sPath, &st) != 0) return NULL;
	if((time(NULL) - st.st_mtime) > pCache->nMaxAgeSec) return NULL;
	return _dascat_readFile(sPath, NULL, 0);
}

/* Written to a side file then renamed, so a reader never sees a partial
 * document when two programs walk the catalog at once.  Failures only cost
 * the cache, so they are logged at debug level and otherwise ignored. */
static void _dascat_writeCache(const char* sPath, const char* pBytes, size_t uBytes)
{
	char sTmp[600];
	if(snprintf(sTmp, sizeof(sTmp), "%s.%ld.tmp", sPath, (long)time(NULL)) >= (int)sizeof(sTmp)) return;

	if(dasfs_mkdirsTo(sPath) != DAS_OKAY){
		daslog_debugf("Couldn't create node cache directory for %s", sPath);
		return;
	}
	FILE* pFile = fopen(sTmp, "wb");
	if(pFile == NULL){
		daslog_debugf("Couldn't write node cache file %s, %s", sTmp, strerror(errno));
		return;
	}
	bool bOk = (fwrite(pBytes, 1, uBytes, pFile) == uBytes);
	bOk = (fclose(pFile) == 0) && bOk;
	if(!bOk || (dasfs_rename(sTmp, sPath) != 0)){
		daslog_debugf("Couldn't finish node cache file %s", sPath);
		remove(sTmp);
	}
}

/* The validators a cached node came with live beside it in a .hdr side
 * file, at most two lines, the ETag and Last-Modified header values
 * verbatim.  When the copy is older than the session trusts both go back
 * as If-None-Match and If-Modified-Since, and a 304 renews the copy for
 * one round trip and no body.  Both are kept because servers differ: the
 * das2.org tree sends both, GitHub raw sends ETag only, dasflex today
 * sends neither.  The date is never derived from the cache file's 
 * mtime, which renewal moves forward. */
static void _dascat_hdrPath(const char* sCache, char* sHdr, size_t uLen)
{
	snprintf(sHdr, uLen, "%s.hdr", sCache);
}

/* Request header lines for the cached copy at sCache, "" if it has none */
static void _dascat_readValidators(const char* sCache, char* sReq, size_t uLen)
{
	sReq[0] = '\0';
	char sHdr[600];
	_dascat_hdrPath(sCache, sHdr, sizeof(sHdr));
	FILE* pFile = fopen(sHdr, "r");
	if(pFile == NULL) return;

	char sLine[1024];
	size_t u = 0;
	while(fgets(sLine, sizeof(sLine), pFile) != NULL){
		size_t uLine = strlen(sLine);
		while((uLine > 0) && ((sLine[uLine-1] == '\n')||(sLine[uLine-1] == '\r')))
			sLine[--uLine] = '\0';
		const char* sName = NULL;
		const char* sVal = NULL;
		if(strncmp(sLine, "ETag: ", 6) == 0){ sName = "If-None-Match"; sVal = sLine + 6; }
		else if(strncmp(sLine, "Last-Modified: ", 15) == 0){ sName = "If-Modified-Since"; sVal = sLine + 15; }
		if((sName == NULL)||(sVal[0] == '\0')) continue;
		int n = snprintf(sReq + u, uLen - u, "%s: %s\r\n", sName, sVal);
		if((n < 0)||((size_t)n >= uLen - u)){ sReq[u] = '\0'; break; }
		u += (size_t)n;
	}
	fclose(pFile);
}

/* Save the validators a 200 carried, or drop the side file if it carried
 * none, so a validator from an older body is never sent for a newer one */
static void _dascat_writeValidators(const char* sCache, const DasHttpResp* pRes)
{
	char sHdr[600];
	_dascat_hdrPath(sCache, sHdr, sizeof(sHdr));
	char sETag[256] = {'\0'};
	char sMod[128] = {'\0'};
	bool bETag = DasHttpResp_header(pRes, "ETag", sETag, sizeof(sETag));
	bool bMod  = DasHttpResp_header(pRes, "Last-Modified", sMod, sizeof(sMod));
	if(!bETag && !bMod){ remove(sHdr); return; }

	FILE* pFile = fopen(sHdr, "w");
	if(pFile == NULL){
		daslog_debugf("Couldn't write node cache side file %s, %s", sHdr, strerror(errno));
		return;
	}
	if(bETag) fprintf(pFile, "ETag: %s\n", sETag);
	if(bMod)  fprintf(pFile, "Last-Modified: %s\n", sMod);
	fclose(pFile);
}

/* ************************************************************************* */
/* One document, from wherever it lives */

struct dascat_fetch {
	DasAry* pBytes;        /* the document, NULL when nothing came back */
	char sFinal[512];      /* the URL after redirects, for messages */
	char sCache[512];      /* where a copy goes, "" when not cached */
	bool bFromCache;       /* copy used as is, nothing to write back */
	bool bRenewed;         /* server confirmed the copy, rewrite it to move its age */
	bool bNoAnswer;        /* no HTTP status came back: refused, timed out, no route */
	int nHttp;             /* status when nothing came back, -1 no answer, 0 not HTTP */
	char sWhy[256];        /* and why, "" when something did */
	DasHttpResp* pRes;     /* NULL for files and cache hits */
};

/* Calls daserrf only when memory runs out, network operations are
 * assumed to fail sometimes.  The 20 MB limit is a sanity check, no
 * catalog node comes near it. */
static void _dascat_fetch(
	DasCredMgr* pMgr, const char* sAgent, const DasNodeCache* pCache,
	const char* sUrl, float rConSec, struct dascat_fetch* pF
){
	memset(pF, 0, sizeof(*pF));

	if(_dascat_isFileUrl(sUrl)){
		pF->pBytes = _dascat_readFile(sUrl, pF->sWhy, sizeof(pF->sWhy));
		strncpy(pF->sFinal, sUrl, sizeof(pF->sFinal) - 1);
		return;
	}
	if(_dascat_cachePath(pCache, sUrl, pF->sCache, sizeof(pF->sCache)) &&
	   ((pF->pBytes = _dascat_readCache(pCache, pF->sCache)) != NULL)){
		daslog_debugf("Node %s read from cache", sUrl);
		strncpy(pF->sFinal, sUrl, sizeof(pF->sFinal) - 1);
		pF->bFromCache = true;
		return;
	}

	/* No copy, or one older than the session trusts.  With validators from
	 * the old copy the request is conditional and a 304 renews it for no
	 * body bytes.  If the server cannot be reached at all the old copy
	 * still beats nothing: a warm cache keeps working offline. */
	char sReqHdrs[1024] = {'\0'};
	bool bHaveOld = (pF->sCache[0] != '\0') && dasfs_isFile(pF->sCache);
	if(bHaveOld) _dascat_readValidators(pF->sCache, sReqHdrs, sizeof(sReqHdrs));

	DasHttpOpts opts;
	init_DasHttpOpts(&opts);
	opts.userAgent = sAgent;
	opts.connectSec = rConSec;
	opts.extraHeaders = sReqHdrs;
	pF->pBytes = new_DasAry_httpEx(sUrl, pMgr, &opts, 1024*1024*20, &pF->pRes);
	if(pF->pRes == NULL){   /* only when memory ran out */
		strncpy(pF->sFinal, sUrl, sizeof(pF->sFinal) - 1);
		return;
	}
	if(pF->pBytes != NULL){
		strncpy(pF->sFinal, DasHttpResp_url(pF->pRes), sizeof(pF->sFinal) - 1);
		return;
	}
	strncpy(pF->sFinal, sUrl, sizeof(pF->sFinal) - 1);
	const char* sErr = DasHttpResp_error(pF->pRes);
	if(DasHttpResp_code(pF->pRes) == 304){
		daslog_debugf("Node %s unchanged on the server, cached copy renewed", sUrl);
		pF->pBytes = _dascat_readFile(pF->sCache, NULL, 0);
		pF->bRenewed = true;
	}
	else if(bHaveOld){
		/* A lookup that works is not a failure, so this one has nowhere to
		   go but the log, and it is worth knowing the copy may be stale */
		daslog_warnf("%s, using the cached copy of %s", sErr ? sErr : "Fetch failed", sUrl);
		pF->pBytes = _dascat_readFile(pF->sCache, NULL, 0);
		pF->bFromCache = true;
	}
	else{
		/* Reported by the lookup, see struct dascat_fail */
		daslog_debugf("%s", sErr ? sErr : "Fetch failed");
		pF->nHttp = DasHttpResp_code(pF->pRes);
		pF->bNoAnswer = (pF->nHttp < 0);
		snprintf(pF->sWhy, sizeof(pF->sWhy), "%s", sErr ? sErr : "Fetch failed");
	}
}

/* Keep a copy of a document the walk accepted.  Only documents that parse
 * are worth keeping, a server error page is not.  A renewal rewrites the
 * same bytes, which is what moves the copy's age, and keeps the
 * validators it already has. */
static void _dascat_keep(const struct dascat_fetch* pF, const char* pBytes, size_t uBytes)
{
	if((pF->sCache[0] == '\0') || pF->bFromCache) return;

	/* A server that says not to keep it is obeyed, and an older copy goes
	 * too: it was kept before the server said so and would be served
	 * offline as if current.  "private" does not stop this cache, it
	 * belongs to one user. */
	if((pF->pRes != NULL) && _DasHttpResp_noStore(pF->pRes)){
		daslog_debugf("Node %s is marked no-store, not cached", pF->sFinal);
		char sHdr[600];
		_dascat_hdrPath(pF->sCache, sHdr, sizeof(sHdr));
		remove(pF->sCache);
		remove(sHdr);
		return;
	}
	_dascat_writeCache(pF->sCache, pBytes, uBytes);
	if(!pF->bRenewed && (pF->pRes != NULL)) _dascat_writeValidators(pF->sCache, pF->pRes);
}

static void _dascat_fetchDone(struct dascat_fetch* pF)
{
	del_DasHttpResp(pF->pRes);
	pF->pRes = NULL;
	if(pF->pBytes != NULL) dec_DasAry(pF->pBytes);
	pF->pBytes = NULL;
}

/* ************************************************************************* */
/* Driving a walk */

/* Note why a document was no good, for the lookup's explanation */
static void _dascat_noteFail(
	struct dascat_fail* pFail, int nCause, int nHttp, const char* sUrl, const char* sWhy
){
	if(pFail == NULL) return;
	pFail->nCause = nCause;
	pFail->nHttp = nHttp;
	_dascat_cpy(pFail->sUrl, sizeof(pFail->sUrl), sUrl);
	_dascat_cpy(pFail->sWhy, sizeof(pFail->sWhy), sWhy);
}

/* Loop a walk to its end.  pbNoAnswer is set when some request got no
 * answer at all, the one failure a longer timeout might cure.  A path the
 * catalog does not have fails through the walk the same way, and waiting
 * longer would not change that.  pFail, when not NULL, gets the last
 * document that could not be used. */
static DasNode* _dascat_drive(
	DasNodeWalk* pWalk, DasCredMgr* pMgr, const char* sAgent,
	const DasNodeCache* pCache, float rConSec, bool* pbNoAnswer,
	struct dascat_fail* pFail
){
	struct dascat_fetch f;
	while(DasNodeWalk_state(pWalk) == DASWALKSTATE_FETCH){
		const char* sUrl = DasNodeWalk_urlAt(pWalk, 0);
		_dascat_fetch(pMgr, sAgent, pCache, sUrl, rConSec, &f);
		if(f.pBytes != NULL){
			ptrdiff_t nLen = 0;
			const char* p = (const char*)DasAry_bytesIn(f.pBytes, DAS_ADDR0, &nLen);
			if(nLen < 0){
				_dascat_noteFail(pFail, DASMISS_LOAD, 0, sUrl, "unreadable document");
				DasNodeWalk_fail(pWalk, 0, NULL);
			}
			else if(DasNodeWalk_feed(pWalk, 0, p, (size_t)nLen, f.sFinal) == DAS_OKAY)
				_dascat_keep(&f, p, (size_t)nLen);
			else
				_dascat_noteFail(pFail, DASMISS_PARSE, 0, f.sFinal, "not a usable catalog node");
		}
		else{
			if(f.bNoAnswer && (pbNoAnswer != NULL)) *pbNoAnswer = true;
			_dascat_noteFail(pFail, DASMISS_LOAD, f.nHttp, sUrl, f.sWhy);
			DasNodeWalk_fail(pWalk, 0, f.sWhy[0] ? f.sWhy : NULL);
		}
		_dascat_fetchDone(&f);
	}
	return DasNodeWalk_result(pWalk);
}

/* The das catalog root, from whichever of its mirrors answers.  Each is
 * tried in turn with a short connect timeout and the round is repeated
 * with longer ones, but only when some server gave no answer, so one slow
 * mirror does not stall the load.  The root is a tree top the caller
 * owns; everything in the federated catalog is a walk from it. */
static DasNode* _dascat_loadRoot(
	DasCredMgr* pMgr, const char* sAgent, const DasNodeCache* pCache,
	struct dascat_fail* pFail
){
	float rConSec = DASHTTP_TIMEOUT_MIN;
	while(rConSec <= DASHTTP_TIMEOUT_MAX){
		DasNodeWalk* pWalk = new_DasNodeWalk_seed();
		if(pWalk == NULL) return NULL;
		bool bNoAnswer = false;
		DasNode* pRoot = _dascat_drive(pWalk, pMgr, sAgent, pCache, rConSec, &bNoAnswer, pFail);
		del_DasNodeWalk(pWalk);
		if(pRoot != NULL) return pRoot;
		if(!bNoAnswer) break;
		rConSec *= DASHTTP_TIMEOUT_MULTI;
	}
	return NULL;
}

/* One node from a URL or file, named by the caller or by its location.
 * Since they want a specific URL, go for the max timeout. */
static DasNode* _dascat_loadUrl(
	const char* sUrl, const char* sPathUri, DasCredMgr* pMgr, const char* sAgent,
	const DasNodeCache* pCache, struct dascat_fail* pFail
){
	struct dascat_fetch f;
	_dascat_fetch(pMgr, sAgent, pCache, sUrl, DASHTTP_TIMEOUT_MAX, &f);
	DasNode* pNode = NULL;
	if(f.pBytes != NULL){
		ptrdiff_t nLen = 0;
		const char* p = (const char*)DasAry_bytesIn(f.pBytes, DAS_ADDR0, &nLen);
		if(nLen >= 0)
			pNode = _dasnode_fromJson(p, (size_t)nLen, sUrl, f.sFinal, sPathUri ? sPathUri : "");
		if(pNode != NULL){
			pNode->bOwner = true;
			_dascat_keep(&f, p, (size_t)nLen);
		}
		else{
			_dascat_noteFail(pFail, DASMISS_PARSE, 0, f.sFinal, "not a usable catalog node");
		}
	}
	else{
		_dascat_noteFail(pFail, DASMISS_LOAD, f.nHttp, sUrl, f.sWhy);
	}
	_dascat_fetchDone(&f);
	return pNode;
}

/* ************************************************************************* */
/* Small helpers */

/* strncpy that always terminates and takes NULL for "" */
static void _dascat_cpy(char* sDest, size_t uDest, const char* sSrc)
{
	if(uDest == 0) return;
	snprintf(sDest, uDest, "%s", (sSrc != NULL) ? sSrc : "");
}

static void _dascat_setStr(char** ps, const char* s)
{
	free(*ps);
	*ps = ((s != NULL) && (s[0] != '\0')) ? dasstr_new(s) : NULL;
}

/* Take ownership of a tree top for the life of the session */
static DasNode* _DasCat_keep(DasCat* pThis, DasNode* pRoot)
{
	if(pRoot == NULL) return NULL;
	if(pThis->uOwned == pThis->uOwnedCap){
		size_t uCap = (pThis->uOwnedCap == 0) ? 8 : 2 * pThis->uOwnedCap;
		DasNode** a = (DasNode**)realloc(pThis->aOwned, uCap * sizeof(DasNode*));
		if(a == NULL){
			daserrf(DASERR_CAT, "Couldn't grow the session's node list");
			return pRoot;   /* leaks the node rather than losing it */
		}
		pThis->aOwned = a;
		pThis->uOwnedCap = uCap;
	}
	pThis->aOwned[pThis->uOwned++] = pRoot;
	return pRoot;
}

/* ************************************************************************* */
/* Telling the caller what happened */

/* An output always written: a success reads DAS_OKAY with empty strings */
static void _dascat_missInit(das_catmiss* pMiss)
{
	memset(pMiss, 0, sizeof(das_catmiss));
}

static void _dascat_missSet(das_catmiss* pMiss, int nCause, const char* sFmt, ...)
{
	pMiss->nCause = nCause;
	va_list args;
	va_start(args, sFmt);
	vsnprintf(pMiss->sMsg, sizeof(pMiss->sMsg), sFmt, args);
	va_end(args);
}

/* Append to the message, cutting at the end of the buffer */
static void _dascat_missCat(das_catmiss* pMiss, const char* sFmt, ...)
{
	size_t u = strlen(pMiss->sMsg);
	if(u + 1 >= sizeof(pMiss->sMsg)) return;
	va_list args;
	va_start(args, sFmt);
	vsnprintf(pMiss->sMsg + u, sizeof(pMiss->sMsg) - u, sFmt, args);
	va_end(args);
}

/* A document that did not become a node, from the failure record */
static void _dascat_missFromFail(das_catmiss* pMiss, const struct dascat_fail* pFail, const char* sWhat)
{
	pMiss->nHttp = pFail->nHttp;
	_dascat_cpy(pMiss->sUrl, sizeof(pMiss->sUrl), pFail->sUrl);
	if(pFail->nCause == DASMISS_PARSE)
		_dascat_missSet(pMiss, DASMISS_PARSE, "%s %s is not a usable catalog node", sWhat, pFail->sUrl);
	else
		_dascat_missSet(pMiss, DASMISS_LOAD, "Couldn't load %s %s: %s", sWhat, pFail->sUrl,
		                pFail->sWhy[0] ? pFail->sWhy : "no reason given");
}

/* The public loaders take NULL for "don't tell me", which means the log */
static void _dascat_missLog(const das_catmiss* pMiss)
{
	if(pMiss->nCause != DAS_OKAY) daslog_warnf("%s", pMiss->sMsg);
}

static bool _dascat_ieqn(const char* a, const char* b, size_t u)
{
	for(size_t i = 0; i < u; ++i){
		if(tolower((unsigned char)a[i]) != tolower((unsigned char)b[i])) return false;
		if(a[i] == '\0') return true;
	}
	return true;
}

/* The listed child of pNear that sRest starts with, longest first, or -1.
 * Done by listing, not by splitting sRest, because names have separators
 * in them: under the das catalog root the first name is
 * "tag:das2.org,2012".  A name counts when what follows it could not
 * continue it, so "uiowa" is not found in "uiowax/...". */
static ptrdiff_t _dascat_listed(const DasNode* pNear, const char* sRest)
{
	ptrdiff_t iBest = -1;
	size_t uBest = 0;
	size_t uKids = DasNode_numSubs(pNear);
	for(size_t u = 0; u < uKids; ++u){
		const char* sName = DasNode_subNameAt(pNear, u);
		size_t uLen = sName ? strlen(sName) : 0;
		if((uLen == 0)||(uLen <= uBest)||!_dascat_ieqn(sName, sRest, uLen)) continue;
		/* A name that ends in punctuation, as the root's "tag:das2.org,2012:"
		   does, carries its own boundary */
		char cEnd = sName[uLen - 1];
		char c = sRest[uLen];
		bool bEndsWord = isalnum((unsigned char)cEnd)||(cEnd == '_');
		if(bEndsWord && (isalnum((unsigned char)c)||(c == '_')||(c == '-'))) continue;
		iBest = (ptrdiff_t)u;
		uBest = uLen;
	}
	return iBest;
}

static DasNode* _DasCat_byUri(DasCat* pThis, const char* sUri, das_catmiss* pMiss, bool bExplain);
static bool _dascat_goesOn(const DasNode* pNode, const char* sRest);

/* After a failed walk from pFrom: say how far it got and why it stopped.
 *
 * The walk names the deepest container it searched by URI, but may have
 * let that node go, since a document that leads nowhere is dropped so
 * that a mirror can be tried.  So it is resolved again, which costs
 * nothing when it is still in memory and otherwise one cache read or
 * fetch.  An empty URI is a root, which can only be pFrom. */
static void _DasCat_explain(
	DasCat* pThis, const DasNodeWalk* pWalk, DasNode* pFrom,
	const struct dascat_fail* pFail, das_catmiss* pMiss
){
	const char* sRest = NULL;
	const char* sNear = DasNodeWalk_nearest(pWalk, &sRest);
	DasNode* pNear = NULL;
	if(sNear != NULL){
		const char* sFrom = DasNode_pathUri(pFrom);
		if(sNear[0] == '\0')
			pNear = pFrom;
		else if((sFrom != NULL) && (strcmp(sFrom, sNear) == 0))
			pNear = pFrom;     /* the program's own tree may not be under the root */
		else{
			das_catmiss again;
			pNear = _DasCat_byUri(pThis, sNear, &again, false);
		}
	}

	if((pNear == NULL)||!DasNode_isCatalog(pNear)){
		if(pFail->nCause != 0) _dascat_missFromFail(pMiss, pFail, "node");
		else _dascat_missSet(pMiss, DASMISS_NOCHILD, "Nothing in the catalog matches %s", pMiss->sUri);
		return;
	}

	pMiss->pNear = pNear;
	if(sRest == NULL) sRest = "";
	_dascat_cpy(pMiss->sRest, sizeof(pMiss->sRest), sRest);

	const char* sNearUri = DasNode_pathUri(pNear);
	char sWhere[DASCAT_MISS_URI_SZ];
	if((sNearUri != NULL) && (sNearUri[0] != '\0')) _dascat_cpy(sWhere, sizeof(sWhere), sNearUri);
	else snprintf(sWhere, sizeof(sWhere), "the root (%s)", DasNode_srcUrl(pNear));

	/* Nothing after the separator: the path ended with one */
	if(sRest[0] == '\0'){
		_dascat_missSet(pMiss, DASMISS_NOCHILD, "'%s' ends with the separator of %s, '%s'.  "
		                "A name does not include its separator", pMiss->sUri, sWhere,
		                DasNode_pathSep(pNear));
		return;
	}

	ptrdiff_t iKid = _dascat_listed(pNear, sRest);
	if(iKid < 0){
		char sChild[128];
		_dascat_cpy(sChild, sizeof(sChild), sRest);
		char* pSlash = strchr(sChild, '/');
		if(pSlash != NULL) *pSlash = '\0';
		_dascat_missSet(pMiss, DASMISS_NOCHILD, "No child named '%s' under %s", sChild, sWhere);
		size_t uKids = DasNode_numSubs(pNear);
		if(uKids > 0) _dascat_missCat(pMiss, ".  Children:");
		for(size_t u = 0; u < uKids; ++u) _dascat_missCat(pMiss, " %s", DasNode_subNameAt(pNear, u));
		return;
	}

	const char* sChild = DasNode_subNameAt(pNear, (size_t)iKid);
	if(DasNode_subTypeAt(pNear, (size_t)iKid) == DASNODETYPE_INV){
		const DasJdo* pStub = DasNode_subStubAt(pNear, (size_t)iKid);
		const char* sType = pStub ? DasJdo_str(DasJdo_subtree(pStub, DASNODE_FRAG_TYPE)) : NULL;
		_dascat_missSet(pMiss, DASMISS_UNSUPPORTED,
			"'%s' under %s is a %s node, which das2C can list but not load",
			sChild, sWhere, sType ? sType : "unknown type of"
		);
		return;
	}
	if(pFail->nCause != 0){
		pMiss->nHttp = pFail->nHttp;
		_dascat_cpy(pMiss->sUrl, sizeof(pMiss->sUrl), pFail->sUrl);
		if(pFail->nCause == DASMISS_PARSE)
			_dascat_missSet(pMiss, DASMISS_PARSE, "'%s' under %s is listed, but %s is not a usable catalog node",
			                sChild, sWhere, pFail->sUrl);
		else
			_dascat_missSet(pMiss, DASMISS_LOAD, "'%s' under %s is listed but could not be loaded: %s",
			                sChild, sWhere, pFail->sWhy[0] ? pFail->sWhy : "no reason given");
		return;
	}
	/* The child is there, but what follows its name is not its
	   separator, or it is a source with nothing under it.  It is looked up
	   again to read its separator, which costs nothing when the walk kept
	   it and otherwise one cache read or fetch. */
	size_t uChild = strlen(sChild);
	if((strncmp(sChild, sRest, uChild) == 0) && (sRest[uChild] != '\0')){
		char sKid[DASCAT_MISS_URI_SZ];
		if((sNearUri == NULL)||(sNearUri[0] == '\0')) _dascat_cpy(sKid, sizeof(sKid), sChild);
		else snprintf(sKid, sizeof(sKid), "%s%s%s", sNearUri, DasNode_pathSep(pNear), sChild);
		das_catmiss again;
		const DasNode* pKid = _DasCat_byUri(pThis, sKid, &again, false);
		if((pKid != NULL) && !DasNode_isCatalog(pKid)){
			_dascat_missSet(pMiss, DASMISS_NOCHILD, "%s is a data source, nothing is under it", sKid);
			return;
		}
		if((pKid != NULL) && !_dascat_goesOn(pKid, sRest + uChild)){
			_dascat_missSet(pMiss, DASMISS_NOCHILD, "After %s comes '%s', but its separator is '%s'",
			                sKid, sRest + uChild, DasNode_pathSep(pKid));
			return;
		}
	}

	/* It loaded and its listing does not lead on, say a mirror that is
	   behind */
	_dascat_missSet(pMiss, DASMISS_NOCHILD, "Nothing under '%s' in %s continues the path %s",
	                sChild, sWhere, sRest);
}

/* A path below a node, with this session's settings.  Children get a
 * slower timeout than roots since there is no second copy of the tree to
 * fall back to.  bExplain false skips _DasCat_explain(), for the lookups
 * it makes itself, which must not explain in turn. */
static DasNode* _DasCat_walkFrom(
	DasCat* pThis, DasNode* pFrom, const char* sRel, das_catmiss* pMiss, bool bExplain
){
	if(!DasNode_isCatalog(pFrom)){
		_dascat_missSet(pMiss, DASMISS_NOCHILD, "%s is not a container, nothing is under it",
		                DasNode_pathUri(pFrom) ? DasNode_pathUri(pFrom) : DasNode_srcUrl(pFrom));
		return NULL;
	}
	DasNodeWalk* pWalk = new_DasNodeWalk(pFrom, sRel);
	if(pWalk == NULL){
		_dascat_missSet(pMiss, DASMISS_NOCHILD, "Couldn't start a walk below %s", DasNode_srcUrl(pFrom));
		return NULL;
	}
	struct dascat_fail fail;
	memset(&fail, 0, sizeof(fail));
	DasNode* pNode = _dascat_drive(
		pWalk, pThis->pMgr, pThis->sAgent, _DasCat_cache(pThis),
		DASHTTP_TIMEOUT_MIN * DASHTTP_TIMEOUT_MULTI, NULL, &fail
	);
	if(pNode == NULL){
		if(bExplain) _DasCat_explain(pThis, pWalk, pFrom, &fail, pMiss);
		else _dascat_missSet(pMiss, DASMISS_NOCHILD, "No node at %s", pMiss->sUri);
	}
	del_DasNodeWalk(pWalk);
	return pNode;
}

/* ************************************************************************* */
/* Construction */

DasCat* new_DasCat(DasCredMgr* pMgr, const char* sAgent)
{
	DasCat* pThis = (DasCat*)calloc(1, sizeof(DasCat));
	if(pThis == NULL){
		daserrf(DASERR_CAT, "Couldn't allocate a catalog session");
		return NULL;
	}
	pThis->pMgr = pMgr;
	if((sAgent != NULL) && (sAgent[0] != '\0')) pThis->sAgent = dasstr_new(sAgent);

	/* No disk cache until the program asks, library code writes no files
	 * by default.  Nodes still stay in memory for the session's life. */
	pThis->cache.nMaxAgeSec = DASCAT_NODE_TTL_SEC;
	return pThis;
}

void del_DasCat(DasCat* pThis)
{
	if(pThis == NULL) return;

	/* Nodes borrow the credentials manager and the cache settings, so they go
	 * first.  The mounted nodes and the seeds, current and replaced, are in
	 * aOwned like every tree top. */
	for(size_t u = 0; u < pThis->uOwned; ++u) del_DasNode(pThis->aOwned[u]);
	free(pThis->aOwned);
	if(pThis->pMgr != NULL) del_DasCredMgr(pThis->pMgr);

	free(pThis->sAgent);
	free(pThis->sSeedUrl);
	free(pThis->sSeedUri);
	for(size_t u = 0; u < pThis->uMounts; ++u){
		free(pThis->aMounts[u].sUrl);
		free(pThis->aMounts[u].sUri);
	}
	free(pThis->aMounts);
	free(pThis);
}

DasCredMgr* DasCat_credMgr(const DasCat* pThis){ return pThis->pMgr; }

const char* DasCat_agent(const DasCat* pThis){ return pThis->sAgent; }

void DasCat_setAgent(DasCat* pThis, const char* sAgent)
{
	/* sAgent may be our own string, from DasCat_agent() */
	if(sAgent == pThis->sAgent) return;
	_dascat_setStr(&pThis->sAgent, sAgent);
}

/* The spot the das tools share, from dasfs_userDir() as it is now */
static das_errcode _dascat_defaultDir(DasNodeCache* pCache)
{
	int nLen = snprintf(pCache->sDir, sizeof(pCache->sDir),
		"%s" DASFS_SEPS "cache" DASFS_SEPS "nodes", dasfs_userDir()
	);
	if((nLen > 0) && (nLen < (int)sizeof(pCache->sDir))) return DAS_OKAY;

	/* Usually DAS_USERDIR or a command line, see the top of this file */
	pCache->sDir[0] = '\0';
	daslog_errorf("User directory %s too long for the node cache", dasfs_userDir());
	return DASERR_CAT;
}

das_errcode DasCat_setCaching(DasCat* pThis, bool bOn)
{
	if(bOn && (pThis->cache.sDir[0] == '\0')){
		if(_dascat_defaultDir(&(pThis->cache)) != DAS_OKAY){
			daslog_error("Node caching stays off");
			pThis->cache.bOn = false;
			return DASERR_CAT;
		}
	}
	pThis->cache.bOn = bOn;
	return DAS_OKAY;
}

bool DasCat_caching(const DasCat* pThis){ return pThis->cache.bOn; }

das_errcode DasCat_setCacheDir(DasCat* pThis, const char* sDir)
{
	if(sDir == NULL){
		/* Picked now rather than flagged for later, so the setting does
		 * not depend on when caching is turned on.  On failure the old one
		 * stays, as for a long sDir below. */
		DasNodeCache tmp = pThis->cache;
		if(_dascat_defaultDir(&tmp) != DAS_OKAY) return DASERR_CAT;
		strcpy(pThis->cache.sDir, tmp.sDir);
		return DAS_OKAY;
	}
	if((sDir[0] == '\0') || (strlen(sDir) >= sizeof(pThis->cache.sDir))){
		daslog_errorf("Node cache directory name is %s: '%s'",
			(sDir[0] == '\0') ? "empty" : "too long", sDir
		);
		return DASERR_CAT;
	}
	/* sDir may be our own buffer, from DasCat_cacheDir() */
	if(sDir != pThis->cache.sDir) strcpy(pThis->cache.sDir, sDir);
	return DAS_OKAY;
}

const char* DasCat_cacheDir(const DasCat* pThis)
{
	return (pThis->cache.sDir[0] == '\0') ? NULL : pThis->cache.sDir;
}

void DasCat_setCacheAge(DasCat* pThis, int nMaxAgeSec){ pThis->cache.nMaxAgeSec = nMaxAgeSec; }

int DasCat_cacheAge(const DasCat* pThis){ return pThis->cache.nMaxAgeSec; }

/* ************************************************************************* */
/* The seed and the mounts */

/* The longest mount name sUri starts with that is shorter than uBelow,
 * or -1.  A name only: whether sUri goes on below that node, or merely
 * starts with the same letters, takes the node's separator, see
 * _DasCat_byUri(). */
static int _DasCat_mountFor(const DasCat* pThis, const char* sUri, size_t uBelow)
{
	int nBest = -1;
	size_t uBest = 0;
	for(size_t u = 0; u < pThis->uMounts; ++u){
		size_t uLen = strlen(pThis->aMounts[u].sUri);
		if((uLen > uBest) && (uLen < uBelow) && (strncmp(sUri, pThis->aMounts[u].sUri, uLen) == 0)){
			nBest = (int)u;
			uBest = uLen;
		}
	}
	return nBest;
}

/* The mount at exactly this name, or -1 */
static int _DasCat_mountAtName(const DasCat* pThis, const char* sUri)
{
	for(size_t u = 0; u < pThis->uMounts; ++u)
		if(strcmp(pThis->aMounts[u].sUri, sUri) == 0) return (int)u;
	return -1;
}

static bool _dascat_locationOk(const char* sUrl)
{
	if(DasCat_isNodeUrl(sUrl)) return true;
	daslog_errorf("A node is loaded from a URL or a file, '%s' is neither", sUrl ? sUrl : "");
	return false;
}

static bool _dascat_nameOk(const char* sUri)
{
	if((sUri == NULL)||(sUri[0] == '\0')){
		daslog_error("A mount needs the catalog URI to put the node at, a node "
		             "to start from is a seed");
		return false;
	}
	if(DasCat_isNodeUrl(sUri)){
		daslog_errorf("'%s' is a location, a catalog URI was expected", sUri);
		return false;
	}
	return true;
}

das_errcode DasCat_mount(DasCat* pThis, const char* sUrl, const char* sUri)
{
	if(!_dascat_locationOk(sUrl)) return DASERR_CAT;
	if(!_dascat_nameOk(sUri)) return DASERR_CAT;

	char* sName = dasstr_new(sUri);
	int iMount = _DasCat_mountAtName(pThis, sName);
	if(iMount >= 0){
		free(sName);
		free(pThis->aMounts[iMount].sUrl);
		pThis->aMounts[iMount].sUrl = dasstr_new(sUrl);
		pThis->aMounts[iMount].pTop = NULL;  /* the old tree stays in aOwned */
	}
	else{
		struct das_cat_mount* a = (struct das_cat_mount*)realloc(
			pThis->aMounts, (pThis->uMounts + 1) * sizeof(struct das_cat_mount)
		);
		if(a == NULL){ free(sName); return daserrf(DASERR_CAT, "Couldn't grow the mount list"); }
		pThis->aMounts = a;
		a[pThis->uMounts].sUrl = dasstr_new(sUrl);
		a[pThis->uMounts].sUri = sName;
		a[pThis->uMounts].pTop = NULL;
		pThis->uMounts += 1;
	}
	return DAS_OKAY;
}

static void _DasCat_removeMount(DasCat* pThis, size_t u)
{
	free(pThis->aMounts[u].sUrl);
	free(pThis->aMounts[u].sUri);
	memmove(pThis->aMounts + u, pThis->aMounts + u + 1,
	        (pThis->uMounts - u - 1) * sizeof(struct das_cat_mount));
	pThis->uMounts -= 1;
}

das_errcode DasCat_unmount(DasCat* pThis, const char* sUrlOrUri)
{
	if(sUrlOrUri == NULL){
		daslog_error("Unmount needs a name or a location; the seed is not a mount, "
		             "see DasCat_setSeed()");
		return DASERR_CAT;
	}

	if(DasCat_isNodeUrl(sUrlOrUri)){
		/* Compared as given: "./a.json" and "a.json" are different
		   locations to this test, as they are to a load */
		size_t uGone = 0;
		for(size_t u = pThis->uMounts; u > 0; --u){
			if(strcmp(pThis->aMounts[u-1].sUrl, sUrlOrUri) == 0){
				_DasCat_removeMount(pThis, u - 1);
				++uGone;
			}
		}
		if(uGone > 0) return DAS_OKAY;
		if((pThis->sSeedUrl != NULL) && (strcmp(pThis->sSeedUrl, sUrlOrUri) == 0))
			daslog_errorf("%s is the seed, not a mount, see DasCat_setSeed()", sUrlOrUri);
		else
			daslog_errorf("Nothing is mounted from %s", sUrlOrUri);
		return DASERR_CAT;
	}

	int iMount = _DasCat_mountAtName(pThis, sUrlOrUri);
	if(iMount < 0){
		daslog_errorf("Nothing is mounted at %s", sUrlOrUri);
		return DASERR_CAT;
	}
	_DasCat_removeMount(pThis, (size_t)iMount);
	return DAS_OKAY;
}

size_t DasCat_numMounts(const DasCat* pThis){ return pThis->uMounts; }

bool DasCat_mountAt(
	const DasCat* pThis, size_t uIdx, const char** psUrl, const char** psUri
){
	bool bIn = (uIdx < pThis->uMounts);
	if(psUrl != NULL) *psUrl = bIn ? pThis->aMounts[uIdx].sUrl : NULL;
	if(psUri != NULL) *psUri = bIn ? pThis->aMounts[uIdx].sUri : NULL;
	return bIn;
}

das_errcode DasCat_setSeed(DasCat* pThis, const char* sUrl, const char* sUri)
{
	bool bNamed = (sUri != NULL) && (sUri[0] != '\0');
	if((sUrl != NULL) && !_dascat_locationOk(sUrl)) return DASERR_CAT;
	if(bNamed && DasCat_isNodeUrl(sUri)){
		daslog_errorf("'%s' is a location, a catalog URI was expected", sUri);
		return DASERR_CAT;
	}

	/* The old seed's tree stays in aOwned, pRoot is kept for reuse */
	free(pThis->sSeedUrl);  pThis->sSeedUrl = NULL;
	free(pThis->sSeedUri);  pThis->sSeedUri = NULL;
	pThis->pSeed = NULL;
	if(sUrl != NULL) pThis->sSeedUrl = dasstr_new(sUrl);
	if(bNamed) pThis->sSeedUri = dasstr_new(sUri);
	return DAS_OKAY;
}

const char* DasCat_seedUrl(const DasCat* pThis){ return pThis->sSeedUrl; }

const char* DasCat_seedUri(const DasCat* pThis)
{
	return (pThis->sSeedUri != NULL) ? pThis->sSeedUri : "";
}

/* Is sUri the seed's prefix, or under it?  Only asked for a named seed. */
static bool _DasCat_underSeed(const DasCat* pThis, const char* sUri)
{
	return strncmp(sUri, pThis->sSeedUri, strlen(pThis->sSeedUri)) == 0;
}

bool DasCat_covers(const DasCat* pThis, const char* sUri)
{
	/* Only a named seed walls paths off.  A root seed, the compiled in one
	 * or a file, is asked about everything: a copy of the das catalog
	 * root has its children named by URI. */
	if(pThis->sSeedUri == NULL) return true;
	if(sUri == NULL) return false;
	if(_DasCat_mountFor(pThis, sUri, SIZE_MAX) >= 0) return true;
	return _DasCat_underSeed(pThis, sUri);
}

/* ************************************************************************* */
/* Resolution */

/* A node's name does not include its own separator: the node is
 * "tag:das2.org,2012:site", not "...site:/".  The separator is only known
 * once the node is loaded, so a seed or mount named with one on the end is
 * caught here, each time it is used.  False, with pMiss saying so. */
static bool _dascat_nameFits(
	const DasNode* pNode, const char* sName, const char* sWhat, das_catmiss* pMiss
){
	if((sName == NULL)||(sName[0] == '\0')||!DasNode_isCatalog(pNode)) return true;
	const char* sSep = DasNode_pathSep(pNode);
	size_t uSep = strlen(sSep);
	size_t uName = strlen(sName);
	if((uSep == 0)||(uName <= uSep)||(strcmp(sName + uName - uSep, sSep) != 0)) return true;
	_dascat_missSet(pMiss, DASMISS_BADNAME,
		"%s is named '%s', which ends with its own separator '%s'.  A node's name "
		"does not include it: '%.*s'", sWhat, sName, sSep, (int)(uName - uSep), sName
	);
	return false;
}

/* A mount's own node, loaded once and kept.  One that fails to load is
 * tried again by the next lookup, as the seed is. */
static DasNode* _DasCat_mountTop(DasCat* pThis, size_t u, das_catmiss* pMiss)
{
	struct das_cat_mount* pMnt = pThis->aMounts + u;
	if(pMnt->pTop == NULL){
		struct dascat_fail fail;
		memset(&fail, 0, sizeof(fail));
		pMnt->pTop = _DasCat_keep(pThis, _dascat_loadUrl(
			pMnt->sUrl, pMnt->sUri, pThis->pMgr, pThis->sAgent, _DasCat_cache(pThis), &fail
		));
		if(pMnt->pTop == NULL){
			char sWhat[DASCAT_MISS_URI_SZ];
			snprintf(sWhat, sizeof(sWhat), "the node mounted at %s,", pMnt->sUri);
			_dascat_missFromFail(pMiss, &fail, sWhat);
			return NULL;
		}
	}
	return _dascat_nameFits(pMnt->pTop, pMnt->sUri, "The mount", pMiss) ? pMnt->pTop : NULL;
}

/* Is sRest, what follows a node's name in a URI, that node or something
 * below it, rather than another node whose name starts the same way?
 * Below means the node's separator comes next.  Without the check
 * "tracers-1" would claim "tracers-10", since a walk accepts a path with
 * or without its leading separator.  A null separator joins names
 * directly, so then any rest goes on below.
 *
 * A source has no separator to go by, and nothing below it.  For one, a
 * rest that starts like more of a name, a letter, digit, '_' or '-', is
 * taken for another node ("geomag2" beside "geomag"); anything else for a
 * path under it, which then fails as one, so a mounted source still
 * shadows what is under its name. */
static bool _dascat_goesOn(const DasNode* pNode, const char* sRest)
{
	if(sRest[0] == '\0') return true;
	if(!DasNode_isCatalog(pNode)){
		unsigned char c = (unsigned char)sRest[0];
		return !(isalnum(c) || (c == '_') || (c == '-'));
	}
	const char* sSep = DasNode_pathSep(pNode);
	size_t uSep = strlen(sSep);
	return (uSep == 0) || (strncmp(sRest, sSep, uSep) == 0);
}

/* The seed node, found once and kept.  A seed that fails to load is
 * tried again by the next lookup, it may have been the network.
 *
 * A seed named but given no location is the node of that name below the
 * compiled in root, found by walking the root alone: mounts are not
 * consulted, so the seed is the same node whatever is mounted.  Its tree
 * is the root's, already in aOwned.  The walk can't come back here
 * through _DasCat_explain(): the names it looks up to explain a miss are
 * shorter than the seed's name, so _DasCat_byUri() reports them outside
 * before asking for the seed. */
static DasNode* _DasCat_seedNode(DasCat* pThis, das_catmiss* pMiss)
{
	struct dascat_fail fail;
	memset(&fail, 0, sizeof(fail));

	if(pThis->sSeedUrl == NULL){
		if(pThis->pRoot == NULL)
			pThis->pRoot = _DasCat_keep(pThis, _dascat_loadRoot(
				pThis->pMgr, pThis->sAgent, _DasCat_cache(pThis), &fail
			));
		if(pThis->pRoot == NULL){
			pMiss->nHttp = fail.nHttp;
			_dascat_cpy(pMiss->sUrl, sizeof(pMiss->sUrl), fail.sUrl);
			_dascat_missSet(pMiss, (fail.nCause == DASMISS_PARSE) ? DASMISS_PARSE : DASMISS_LOAD,
			                "No mirror of the das catalog root answered");
			if(fail.sWhy[0] != '\0') _dascat_missCat(pMiss, ", the last said: %s", fail.sWhy);
			return NULL;
		}
		if(pThis->sSeedUri == NULL) return pThis->pRoot;

		if(pThis->pSeed == NULL){
			pThis->pSeed = _DasCat_walkFrom(pThis, pThis->pRoot, pThis->sSeedUri, pMiss, true);
			if(pThis->pSeed == NULL) return NULL;
		}
		return _dascat_nameFits(pThis->pSeed, pThis->sSeedUri, "The seed", pMiss) ? pThis->pSeed : NULL;
	}

	if(pThis->pSeed == NULL)
		pThis->pSeed = _DasCat_keep(pThis, _dascat_loadUrl(
			pThis->sSeedUrl, pThis->sSeedUri,
			pThis->pMgr, pThis->sAgent, _DasCat_cache(pThis), &fail
		));
	if(pThis->pSeed != NULL)
		return _dascat_nameFits(pThis->pSeed, pThis->sSeedUri, "The seed", pMiss) ? pThis->pSeed : NULL;
	_dascat_missFromFail(pMiss, &fail, "the seed");
	return NULL;
}

/* A full catalog URI, sUri, with the current seed and mounts.
 *
 * Mounts come first, longest name first.  The mount that owns sUri
 * answers for it alone, walked from its own node: a miss there is a miss,
 * it does not back out into a broader mount or the seed, which is what
 * lets a mount shadow what is under it.  A mount whose name sUri only
 * starts with, "x.ab" for a mount at "x.a" with separator ".", does not
 * own it and the next one is asked.  Telling the two apart takes the
 * mount's separator, so a mount is loaded to decide; one that won't load
 * stops the lookup, since it can't say what it holds.
 *
 * pMiss->sUri is set here, so every lookup reports the URI it ended up
 * trying. */
static DasNode* _DasCat_byUri(DasCat* pThis, const char* sUri, das_catmiss* pMiss, bool bExplain)
{
	_dascat_missInit(pMiss);
	_dascat_cpy(pMiss->sUri, sizeof(pMiss->sUri), sUri);

	size_t uBelow = SIZE_MAX;
	int iMnt;
	while((iMnt = _DasCat_mountFor(pThis, sUri, uBelow)) >= 0){
		DasNode* pTop = _DasCat_mountTop(pThis, (size_t)iMnt, pMiss);
		if(pTop == NULL) return NULL;
		uBelow = strlen(pThis->aMounts[iMnt].sUri);
		const char* sRest = sUri + uBelow;
		if(!_dascat_goesOn(pTop, sRest)) continue;
		if(sRest[0] == '\0') return pTop;
		return _DasCat_walkFrom(pThis, pTop, sRest, pMiss, bExplain);
	}

	/* A named seed stands for its node alone; the root above it exists but
	   can't be reached from this session */
	bool bNamed = (pThis->sSeedUri != NULL);
	const char* sRel = sUri;
	if(bNamed){
		if(!_DasCat_underSeed(pThis, sUri)) goto OUTSIDE;
		sRel = sUri + strlen(pThis->sSeedUri);
	}

	DasNode* pSeed = _DasCat_seedNode(pThis, pMiss);
	if(pSeed == NULL) return NULL;
	if(bNamed && !_dascat_goesOn(pSeed, sRel)){
		if(DasNode_isCatalog(pSeed))
			_dascat_missSet(pMiss, DASMISS_OUTSIDE, "'%s' is outside the seed %s and not under any "
			                "mount: after the seed's name comes '%s', but its separator is '%s'",
			                sUri, pThis->sSeedUri, sRel, DasNode_pathSep(pSeed));
		else
			_dascat_missSet(pMiss, DASMISS_OUTSIDE, "'%s' is outside the seed %s, a data source, "
			                "and not under any mount", sUri, pThis->sSeedUri);
		return NULL;
	}

	/* The seed itself is handed back as is; it may be a source, which no
	   walk can start from */
	if(sRel[0] == '\0') return pSeed;
	return _DasCat_walkFrom(pThis, pSeed, sRel, pMiss, bExplain);

OUTSIDE:
	_dascat_missSet(pMiss, DASMISS_OUTSIDE, "'%s' is outside the seed %s and not under any mount",
	                sUri[0] ? sUri : "(the root)", pThis->sSeedUri);
	return NULL;
}

/* A path from the seed as a full name.  There is one place paths start,
 * the seed, and nothing about a path's spelling is interpreted: no
 * schemes, no shorthands.  Below a root every path is a full name
 * already, the root has none of its own.  Below a named seed a path that
 * starts with the seed's name, or with a mount's, is a full name too,
 * compared as text; see _DasCat_byUri() for whether it really is under
 * that node.  Any other
 * path is joined to the seed's name with the seed's own separator, so the
 * seed is loaded to read it.  False, with pMiss saying why, if it won't
 * load or has nothing under it. */
static bool _DasCat_fullName(
	DasCat* pThis, const char* sPath, char* sOut, size_t uOut, das_catmiss* pMiss
){
	const char* sSeed = DasCat_seedUri(pThis);
	if((sSeed[0] == '\0') || _DasCat_underSeed(pThis, sPath) || (_DasCat_mountFor(pThis, sPath, SIZE_MAX) >= 0)){
		_dascat_cpy(sOut, uOut, sPath);
		return true;
	}
	_dascat_cpy(pMiss->sUri, sizeof(pMiss->sUri), sSeed);
	DasNode* pSeed = _DasCat_seedNode(pThis, pMiss);
	if(pSeed == NULL) return false;
	if(!DasNode_isCatalog(pSeed)){
		_dascat_missSet(pMiss, DASMISS_NOCHILD, "The seed %s is a data source, nothing is under it", sSeed);
		return false;
	}
	snprintf(sOut, uOut, "%s%s%s", sSeed, DasNode_pathSep(pSeed), sPath);
	return true;
}

static DasNode* _DasCat_resolve(DasCat* pThis, const char* sPath, das_catmiss* pMiss)
{
	_dascat_missInit(pMiss);
	bool bNoPath = (sPath == NULL)||(sPath[0] == '\0');

	/* A node by location: loaded on its own, the location stands in for a
	   name in messages */
	if(!bNoPath && DasCat_isNodeUrl(sPath)){
		_dascat_cpy(pMiss->sUri, sizeof(pMiss->sUri), sPath);
		struct dascat_fail fail;
		memset(&fail, 0, sizeof(fail));
		DasNode* pNode = _DasCat_keep(pThis, _dascat_loadUrl(
			sPath, NULL, pThis->pMgr, pThis->sAgent, _DasCat_cache(pThis), &fail
		));
		if(pNode == NULL) _dascat_missFromFail(pMiss, &fail, "node");
		return pNode;
	}

	/* Long enough for any name a node.c walk can hold */
	char sUri[1024];
	if(bNoPath)
		_dascat_cpy(sUri, sizeof(sUri), DasCat_seedUri(pThis));
	else if(!_DasCat_fullName(pThis, sPath, sUri, sizeof(sUri), pMiss))
		return NULL;
	return _DasCat_byUri(pThis, sUri, pMiss, true);
}

DasNode* DasCat_resolve(DasCat* pThis, const char* sPath, das_catmiss* pMiss)
{
	das_catmiss local;
	das_catmiss* p = (pMiss != NULL) ? pMiss : &local;
	DasNode* pNode = _DasCat_resolve(pThis, sPath, p);
	if(pMiss == NULL) _dascat_missLog(p);
	return pNode;
}

DasNode* DasCat_seed(DasCat* pThis, das_catmiss* pMiss)
{
	das_catmiss local;
	das_catmiss* p = (pMiss != NULL) ? pMiss : &local;
	_dascat_missInit(p);
	_dascat_cpy(p->sUri, sizeof(p->sUri), DasCat_seedUri(pThis));
	DasNode* pNode = _DasCat_seedNode(pThis, p);
	if(pMiss == NULL) _dascat_missLog(p);
	return pNode;
}

int DasCat_check(DasCat* pThis, das_catmiss* pMiss)
{
	if(pMiss != NULL) _dascat_missInit(pMiss);
	das_catmiss one;
	int nBad = 0;

	for(size_t u = 0; u <= pThis->uMounts; ++u){
		DasNode* pNode = NULL;
		if(u == 0){
			_dascat_missInit(&one);
			pNode = _DasCat_seedNode(pThis, &one);
		}
		else{
			pNode = _DasCat_byUri(pThis, pThis->aMounts[u-1].sUri, &one, true);
		}
		if(pNode != NULL) continue;
		++nBad;
		if(pMiss == NULL) _dascat_missLog(&one);
		else if(nBad == 1) *pMiss = one;
	}
	return nBad;
}

DasNode* DasCat_subNode(DasCat* pThis, DasNode* pNode, const char* sRelPath, das_catmiss* pMiss)
{
	das_catmiss local;
	das_catmiss* p = (pMiss != NULL) ? pMiss : &local;
	_dascat_missInit(p);
	if(pNode == NULL){
		/* A mistake in the program, so it is logged either way */
		daslog_error("No node to load a child of");
		_dascat_missSet(p, DASMISS_NOCHILD, "No node to load a child of");
		return NULL;
	}
	if(sRelPath == NULL) sRelPath = "";
	const char* sUri = DasNode_pathUri(pNode);
	const char* sSep = DasNode_isCatalog(pNode) ? DasNode_pathSep(pNode) : NULL;
	if((sUri == NULL)||(sUri[0] == '\0'))
		_dascat_cpy(p->sUri, sizeof(p->sUri), sRelPath);
	else if((sRelPath[0] == '\0')||(sSep == NULL)||(strncmp(sRelPath, sSep, strlen(sSep)) == 0))
		snprintf(p->sUri, sizeof(p->sUri), "%s%s", sUri, sRelPath);
	else
		snprintf(p->sUri, sizeof(p->sUri), "%s%s%s", sUri, sSep, sRelPath);

	DasNode* pSub = _DasCat_walkFrom(pThis, pNode, sRelPath, p, true);
	if(pMiss == NULL) _dascat_missLog(p);
	return pSub;
}

/* ************************************************************************* */
/* Sources */

/* Not a stream source: a container, which names what it holds, or a
 * kind of node das2C does not query */
static void _dascat_notSrc(const DasNode* pNode, const char* sUri, das_catmiss* pMiss)
{
	if(!DasNode_isCatalog(pNode)){
		_dascat_missSet(pMiss, DASMISS_UNSUPPORTED, "%s is not a data source das2C can query", sUri);
		return;
	}
	const char* aNames[16];
	size_t uKids = DasSrc_loadable(pNode, aNames, 16);
	_dascat_missSet(pMiss, DASMISS_NOTSRC, "%s is a container, not a data source", sUri);
	if(uKids > 0) _dascat_missCat(pMiss, ", its loadable children are:");
	for(size_t u = 0; (u < uKids) && (u < 16); ++u) _dascat_missCat(pMiss, " %s", aNames[u]);
}

DasSrc* new_DasSrc_cat(DasCat* pThis, const char* sPath, das_catmiss* pMiss)
{
	das_catmiss local;
	das_catmiss* p = (pMiss != NULL) ? pMiss : &local;
	DasSrc* pSrc = NULL;
	DasNode* pNode = _DasCat_resolve(pThis, sPath, p);
	if(pNode != NULL){
		if(!DasNode_isStreamSrc(pNode)){
			_dascat_notSrc(pNode, p->sUri[0] ? p->sUri : DasNode_srcUrl(pNode), p);
		}
		else if((pSrc = new_DasSrc_node(pNode)) == NULL){
			/* source.c has logged the details of what it could not read */
			_dascat_missSet(p, DASMISS_UNSUPPORTED, "Couldn't build a data source from %s",
			                DasNode_srcUrl(pNode));
		}
	}
	if(pMiss == NULL) _dascat_missLog(p);
	return pSrc;
}

/* ************************************************************************* */
/* Telling a URL or file from a catalog URI */

static bool _cat_hasPfx(const char* s, const char* sPrefix)
{
	return strncmp(s, sPrefix, strlen(sPrefix)) == 0;
}

bool DasCat_isNodeUrl(const char* sPath)
{
	if(sPath == NULL) return false;
	if(_cat_hasPfx(sPath, "http://")||_cat_hasPfx(sPath, "https://")||_cat_hasPfx(sPath, "file://")) return true;
	if((sPath[0] == '.')||(sPath[0] == '/')||(sPath[0] == '\\')) return true;
	if(isalpha((unsigned char)sPath[0]) && (sPath[1] == ':') && ((sPath[2] == '\\')||(sPath[2] == '/')))
		return true;   /* a Windows drive, catalog URIs do not have their first colon at index 1 */
	size_t u = strlen(sPath);
	return (u > 5) && (strcmp(sPath + u - 5, ".json") == 0);
}
