/* Copyright (C) 2026 Chris Piker <chris-piker@uiowa.edu>
 *
 * Author: C. Piker, via Claude Fable 5.1
 *
 * This file is part of das2C, the Core Das2 C Library.
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
 * das_error, because the usual source of a bad policy is a command line
 * argument and a program should get to decide its exit code for that. */

#define _POSIX_C_SOURCE 200112L

#include <errno.h>
#include <string.h>
#include <stdlib.h>
#include <ctype.h>
#include <stdint.h>
#include <inttypes.h>
#include <time.h>
#include <sys/stat.h>

#include "util.h"
#include "log.h"
#include "http.h"
#include "node_priv.h"
#include "source.h"
#include "catalog.h"

/* Where this session keeps copies of nodes fetched over HTTP and how long
 * it trusts them.  Empty sDir means no caching; nMaxAgeSec of zero or less
 * means always ask the server, see _dascat_fetch(). */
typedef struct das_node_cache {
	char sDir[256];
	int  nMaxAgeSec;
} DasNodeCache;

struct das_cat_mount {
	char* sUrl;
	char* sUri;
};

struct das_cat {
	DasCredMngr* pMgr;      /* owned */
	char* sAgent;           /* owned copy, or NULL for the library default */
	DasNodeCache* pCache;   /* owned, NULL until DasCat_setCache() */

	/* Where paths start.  Relative paths get sPrefix in front, or with a
	 * seed set, the seed's prefix and separator; the two are one knob
	 * with two setters, and only one may be used.  Mounts are consulted
	 * before the federated catalog for any URI they cover, opportunistic
	 * grafts rather than a policy.  A seed is a mount that turns the
	 * federated catalog off: the mounts are then the whole catalog, which
	 * is how an offline catalog is assembled from a handful of files. */
	char* sPrefix;          /* never NULL */
	bool bPrefixGiven;      /* DasCat_setPrefix() was called */
	struct das_cat_mount* aMounts;  /* named nodes loaded by location */
	size_t uMounts;
	bool bSeed;             /* set by DasCat_setSeed(): the seed and the mounts are the whole catalog */
	char* sSeedUrl;         /* owned, where the seed loads from */
	char* sSeedUri;         /* owned, its prefix, "" for a root */
	DasNode* pSeed;         /* the seed node, loaded on first use */

	DasNode* pTable;        /* the mount table, built on first use */
	DasNode* pDas2;         /* the compiled in seed, loaded on first use; the
	                           tree below it is the session's memory of the
	                           federated catalog */
	DasNode** aOwned;       /* every tree top the session holds, freed with it */
	size_t uOwned;
	size_t uOwnedCap;

	char* sLastPath;        /* the caller's path to the last resolve, or NULL */
	char* sLastUri;         /* that path as the policy understood it */
	char* sNear;            /* after a failed walk, the deepest container it searched */
	char* sNearRest;        /* and the path below it, see DasCat_nearest() */
	DasNode* pNearNode;     /* that container when it is a root and so has no URI to
	                           resolve again: the walk's start node, borrowed */
};

/* Defined further down, used throughout */
static char* _dascat_strdupz(const char* s);
static DasNode* _dascat_loadUrl(
	const char* sUrl, const char* sPathUri, DasCredMngr* pMgr, const char* sAgent,
	const DasNodeCache* pCache
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

static DasAry* _dascat_readFile(const char* sUrl)
{
	const char* sPath = sUrl;
	if(strncmp(sUrl, "file://", 7) == 0) sPath += 7;

	FILE* pFile = fopen(sPath, "rb");
	if(pFile == NULL){
		daslog_warn_v("Couldn't open node file %s, %s", sPath, strerror(errno));
		return NULL;
	}

	DasAry* pAry = new_DasAry("node_file", vtUByte, 1, NULL, RANK_1(0), UNIT_DIMENSIONLESS);
	char buf[16384];
	size_t uRead = 0;
	while((uRead = fread(buf, 1, sizeof(buf), pFile)) > 0){
		if(!DasAry_append(pAry, (const ubyte*)buf, uRead)){
			dec_DasAry(pAry);
			fclose(pFile);
			return NULL;
		}
	}
	bool bErr = (ferror(pFile) != 0);
	fclose(pFile);
	if(bErr){
		daslog_warn_v("Error reading node file %s", sPath);
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

	int n = snprintf(sPath, uLen, "%s%s%s_%08" PRIx32 ".json", pCache->sDir, DAS_DSEPS, sName, uHash);
	return (n > 0) && ((size_t)n < uLen);
}

/* A fresh enough cached copy, or NULL */
static DasAry* _dascat_readCache(const DasNodeCache* pCache, const char* sPath)
{
	if((pCache == NULL)||(pCache->nMaxAgeSec <= 0)) return NULL;
	struct stat st;
	if(stat(sPath, &st) != 0) return NULL;
	if((time(NULL) - st.st_mtime) > pCache->nMaxAgeSec) return NULL;
	return _dascat_readFile(sPath);
}

/* Written to a side file then renamed, so a reader never sees a partial
 * document when two programs walk the catalog at once.  Failures only cost
 * the cache, so they are logged at debug level and otherwise ignored. */
static void _dascat_writeCache(const char* sPath, const char* pBytes, size_t uBytes)
{
	char sTmp[600];
	if(snprintf(sTmp, sizeof(sTmp), "%s.%ld.tmp", sPath, (long)time(NULL)) >= (int)sizeof(sTmp)) return;

	if(das_mkdirsto(sPath) != DAS_OKAY){
		daslog_debug_v("Couldn't create node cache directory for %s", sPath);
		return;
	}
	FILE* pFile = fopen(sTmp, "wb");
	if(pFile == NULL){
		daslog_debug_v("Couldn't write node cache file %s, %s", sTmp, strerror(errno));
		return;
	}
	bool bOk = (fwrite(pBytes, 1, uBytes, pFile) == uBytes);
	bOk = (fclose(pFile) == 0) && bOk;
	if(!bOk || (das_rename(sTmp, sPath) != 0)){
		daslog_debug_v("Couldn't finish node cache file %s", sPath);
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
		daslog_debug_v("Couldn't write node cache side file %s, %s", sHdr, strerror(errno));
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
	DasHttpResp res;
};

/* Calls das_error only when memory runs out, network operations are
 * assumed to fail sometimes.  The 20 MB limit is a sanity check, no
 * catalog node comes near it. */
static void _dascat_fetch(
	DasCredMngr* pMgr, const char* sAgent, const DasNodeCache* pCache,
	const char* sUrl, float rConSec, struct dascat_fetch* pF
){
	memset(pF, 0, sizeof(*pF));
	pF->res.nCode = -1;

	if(_dascat_isFileUrl(sUrl)){
		pF->pBytes = _dascat_readFile(sUrl);
		strncpy(pF->sFinal, sUrl, sizeof(pF->sFinal) - 1);
		return;
	}
	if(_dascat_cachePath(pCache, sUrl, pF->sCache, sizeof(pF->sCache)) &&
	   ((pF->pBytes = _dascat_readCache(pCache, pF->sCache)) != NULL)){
		daslog_debug_v("Node %s read from cache", sUrl);
		strncpy(pF->sFinal, sUrl, sizeof(pF->sFinal) - 1);
		pF->bFromCache = true;
		return;
	}

	/* No copy, or one older than the session trusts.  With validators from
	 * the old copy the request is conditional and a 304 renews it for no
	 * body bytes.  If the server cannot be reached at all the old copy
	 * still beats nothing: a warm cache keeps working offline. */
	char sReqHdrs[1024] = {'\0'};
	bool bHaveOld = (pF->sCache[0] != '\0') && das_isfile(pF->sCache);
	if(bHaveOld) _dascat_readValidators(pF->sCache, sReqHdrs, sizeof(sReqHdrs));

	pF->pBytes = das_http_readUrl_ex(
		sUrl, sAgent, pMgr, &pF->res, 1024*1024*20, rConSec, sReqHdrs
	);
	if(pF->pBytes != NULL){
		das_url_toStr(&(pF->res.url), pF->sFinal, sizeof(pF->sFinal));
		return;
	}
	strncpy(pF->sFinal, sUrl, sizeof(pF->sFinal) - 1);
	if(pF->res.nCode == 304){
		daslog_debug_v("Node %s unchanged on the server, cached copy renewed", sUrl);
		pF->pBytes = _dascat_readFile(pF->sCache);
		pF->bRenewed = true;
	}
	else if(bHaveOld){
		daslog_warn_v("%s, using the cached copy of %s",
		              pF->res.sError ? pF->res.sError : "Fetch failed", sUrl);
		pF->pBytes = _dascat_readFile(pF->sCache);
		pF->bFromCache = true;
	}
	else{
		daslog_warn_v("%s", pF->res.sError ? pF->res.sError : "Fetch failed");
		pF->bNoAnswer = (pF->res.nCode < 0);
	}
}

/* Keep a copy of a document the walk accepted.  Only documents that parse
 * are worth keeping, a server error page is not.  A renewal rewrites the
 * same bytes, which is what moves the copy's age, and keeps the
 * validators it already has. */
static void _dascat_keep(const struct dascat_fetch* pF, const char* pBytes, size_t uBytes)
{
	if((pF->sCache[0] == '\0') || pF->bFromCache) return;
	_dascat_writeCache(pF->sCache, pBytes, uBytes);
	if(!pF->bRenewed) _dascat_writeValidators(pF->sCache, &pF->res);
}

static void _dascat_fetchDone(struct dascat_fetch* pF)
{
	DasHttpResp_freeFields(&pF->res);
	if(pF->pBytes != NULL) dec_DasAry(pF->pBytes);
	pF->pBytes = NULL;
}

/* ************************************************************************* */
/* Driving a walk */

/* Loop a walk to its end.  pbNoAnswer is set when some request got no
 * answer at all, the one failure a longer timeout might cure.  A path the
 * catalog does not have fails through the walk the same way, and waiting
 * longer would not change that. */
static DasNode* _dascat_drive(
	DasNodeWalk* pWalk, DasCredMngr* pMgr, const char* sAgent,
	const DasNodeCache* pCache, float rConSec, bool* pbNoAnswer
){
	struct dascat_fetch f;
	while(DasNodeWalk_state(pWalk) == das_walk_fetch){
		const char* sUrl = DasNodeWalk_url(pWalk, 0);
		_dascat_fetch(pMgr, sAgent, pCache, sUrl, rConSec, &f);
		if(f.pBytes != NULL){
			size_t uLen = 0;
			const char* p = (const char*)DasAry_getBytesIn(f.pBytes, DIM0, &uLen);
			if(DasNodeWalk_feed(pWalk, 0, p, uLen, f.sFinal) == DAS_OKAY)
				_dascat_keep(&f, p, uLen);
		}
		else{
			if(f.bNoAnswer && (pbNoAnswer != NULL)) *pbNoAnswer = true;
			DasNodeWalk_fail(pWalk, 0, f.res.sError);
		}
		_dascat_fetchDone(&f);
	}
	return DasNodeWalk_result(pWalk);
}

/* Remember what a failed walk can say, see DasCat_nearest() */
static void _DasCat_setNear(DasCat* pThis, const DasNodeWalk* pWalk, DasNode* pFrom)
{
	free(pThis->sNear);     pThis->sNear = NULL;
	free(pThis->sNearRest); pThis->sNearRest = NULL;
	pThis->pNearNode = NULL;
	const char* sRest = NULL;
	const char* sNear = DasNodeWalk_nearest(pWalk, &sRest);
	if(sNear == NULL) return;
	/* An empty URI is a root, which cannot be resolved again by name; it
	   can only be the node the walk started from.  The mount table is a
	   root too, but nothing under it that failed is worth pointing at. */
	if(sNear[0] == '\0'){
		if(pFrom == pThis->pTable) return;
		pThis->pNearNode = pFrom;
	}
	else{
		pThis->sNear = das_strdup(sNear);
	}
	pThis->sNearRest = _dascat_strdupz(sRest);
}

/* A path below a node, with this session's settings.  Children get a
 * slower timeout than roots since there is no second copy of the tree to
 * fall back to. */
static DasNode* _DasCat_walkFrom(DasCat* pThis, DasNode* pFrom, const char* sRel)
{
	DasNodeWalk* pWalk = new_DasNodeWalk(pFrom, sRel);
	if(pWalk == NULL) return NULL;
	DasNode* pNode = _dascat_drive(
		pWalk, pThis->pMgr, pThis->sAgent, pThis->pCache, DASHTTP_TO_MIN * DASHTTP_TO_MULTI, NULL
	);
	_DasCat_setNear(pThis, pWalk, pFrom);
	del_DasNodeWalk(pWalk);
	return pNode;
}

/* The compiled in seed, from whichever of its mirrors answers.  Each is
 * tried in turn with a short connect timeout and the round is repeated
 * with longer ones, but only when some server gave no answer, so one slow
 * mirror does not stall the load.  The seed is a tree top the caller
 * owns; everything in the federated catalog is a walk from it. */
static DasNode* _dascat_loadSeed(DasCredMngr* pMgr, const char* sAgent, const DasNodeCache* pCache)
{
	float rConSec = DASHTTP_TO_MIN;
	while(rConSec <= DASHTTP_TO_MAX){
		DasNodeWalk* pWalk = new_DasNodeWalk_seed();
		if(pWalk == NULL) return NULL;
		bool bNoAnswer = false;
		DasNode* pRoot = _dascat_drive(pWalk, pMgr, sAgent, pCache, rConSec, &bNoAnswer);
		del_DasNodeWalk(pWalk);
		if(pRoot != NULL) return pRoot;
		if(!bNoAnswer) break;
		rConSec *= DASHTTP_TO_MULTI;
	}
	daslog_error("No mirror of the das catalog index answered");
	return NULL;
}

static DasNode* _DasCat_keep(DasCat* pThis, DasNode* pRoot);   /* below, with the session */

/* The seed given to DasCat_setSeed(), loaded once and kept */
static DasNode* _DasCat_seedNode(DasCat* pThis)
{
	if(pThis->pSeed == NULL)
		pThis->pSeed = _DasCat_keep(pThis, _dascat_loadUrl(
			pThis->sSeedUrl, pThis->sSeedUri, pThis->pMgr, pThis->sAgent, pThis->pCache
		));
	return pThis->pSeed;
}

/* The session's copy of the compiled in seed, loaded once and kept, as
 * das2py has always done.  Every federated lookup is then a walk from it
 * and the levels walked through stay loaded for the next one. */
static DasNode* _DasCat_das2(DasCat* pThis)
{
	if(pThis->pDas2 == NULL)
		pThis->pDas2 = _DasCat_keep(pThis, _dascat_loadSeed(pThis->pMgr, pThis->sAgent, pThis->pCache));
	return pThis->pDas2;
}

/* One node from a URL or file, named by the caller or by its location.
 * Since they want a specific URL, go for the max timeout. */
static DasNode* _dascat_loadUrl(
	const char* sUrl, const char* sPathUri, DasCredMngr* pMgr, const char* sAgent,
	const DasNodeCache* pCache
){
	char sUri[600] = {'\0'};
	if((sPathUri != NULL) && (sPathUri[0] != '\0')) _das_uri_expand(sPathUri, sUri, sizeof(sUri));

	struct dascat_fetch f;
	_dascat_fetch(pMgr, sAgent, pCache, sUrl, DASHTTP_TO_MAX, &f);
	DasNode* pNode = NULL;
	if(f.pBytes != NULL){
		size_t uLen = 0;
		const char* p = (const char*)DasAry_getBytesIn(f.pBytes, DIM0, &uLen);
		pNode = _dasnode_fromJson(p, uLen, sUrl, f.sFinal, sUri);
		if(pNode != NULL){
			pNode->bOwner = true;
			_dascat_keep(&f, p, uLen);
		}
	}
	_dascat_fetchDone(&f);
	return pNode;
}

/* ************************************************************************* */
/* Small helpers */

static void _dascat_setStr(char** ps, const char* s)
{
	free(*ps);
	*ps = (s != NULL) ? das_strdup(s) : NULL;
}

/* Shorthands are expanded by node.c as well; doing it here too keeps
 * DasCat_lastUri() the full form even when a resolve fails early. */
static char* _dascat_expandUri(const char* sIn)
{
	char sBuf[600];
	return das_strdup(_das_uri_expand(sIn, sBuf, sizeof(sBuf)));
}

/* Take ownership of a tree top for the life of the session */
static DasNode* _DasCat_keep(DasCat* pThis, DasNode* pRoot)
{
	if(pRoot == NULL) return NULL;
	if(pThis->uOwned == pThis->uOwnedCap){
		size_t uCap = (pThis->uOwnedCap == 0) ? 8 : 2 * pThis->uOwnedCap;
		DasNode** a = (DasNode**)realloc(pThis->aOwned, uCap * sizeof(DasNode*));
		if(a == NULL){
			das_error(DASERR_CAT, "Couldn't grow the session's node list");
			return pRoot;   /* leaks the node rather than losing it */
		}
		pThis->aOwned = a;
		pThis->uOwnedCap = uCap;
	}
	pThis->aOwned[pThis->uOwned++] = pRoot;
	return pRoot;
}

/* ************************************************************************* */
/* Construction */

DasCat* new_DasCat(DasCredMngr* pMgr, const char* sAgent)
{
	DasCat* pThis = (DasCat*)calloc(1, sizeof(DasCat));
	if(pThis == NULL){
		das_error(DASERR_CAT, "Couldn't allocate a catalog session");
		return NULL;
	}
	pThis->pMgr = pMgr;
	if((sAgent != NULL) && (sAgent[0] != '\0')) pThis->sAgent = das_strdup(sAgent);
	pThis->sPrefix = das_strdup(DASCAT_DEF_PREFIX);

	/* Caching is on from the start.  A session that does not want it says
	 * so; one that forgets gets fast catalog walks and at worst an hour old
	 * node, which is the better default for the GUIs and scripts built on
	 * this. */
	char sDir[sizeof(((DasNodeCache*)0)->sDir)];
	int nLen = snprintf(sDir, sizeof(sDir), "%s" DAS_DSEPS "cache" DAS_DSEPS "nodes", das_userdir());
	if((nLen > 0) && (nLen < (int)sizeof(sDir)))
		DasCat_setCache(pThis, sDir, DASCAT_DEF_CACHE_SEC);
	else
		daslog_warn_v("User directory %s too long for the node cache, caching is off", das_userdir());
	return pThis;
}

void del_DasCat(DasCat* pThis)
{
	if(pThis == NULL) return;

	/* Nodes borrow the credentials manager and the cache settings, so they go
	 * first.  The mount table and the seeds are in aOwned like every tree top. */
	for(size_t u = 0; u < pThis->uOwned; ++u) del_DasNode(pThis->aOwned[u]);
	free(pThis->aOwned);
	if(pThis->pMgr != NULL) del_CredMngr(pThis->pMgr);
	free(pThis->pCache);

	free(pThis->sAgent);
	free(pThis->sPrefix);
	for(size_t u = 0; u < pThis->uMounts; ++u){
		free(pThis->aMounts[u].sUrl);
		free(pThis->aMounts[u].sUri);
	}
	free(pThis->aMounts);
	free(pThis->sLastPath);
	free(pThis->sLastUri);
	free(pThis->sNear);
	free(pThis->sNearRest);
	free(pThis->sSeedUrl);
	free(pThis->sSeedUri);
	free(pThis);
}

DasCredMngr* DasCat_credMngr(const DasCat* pThis){ return pThis->pMgr; }

const char* DasCat_agent(const DasCat* pThis){ return pThis->sAgent; }

void DasCat_setCache(DasCat* pThis, const char* sDir, int nMaxAgeSec)
{
	if(pThis->pCache == NULL)
		pThis->pCache = (DasNodeCache*)calloc(1, sizeof(DasNodeCache));
	if(pThis->pCache == NULL){
		das_error(DASERR_CAT, "Couldn't allocate the node cache settings");
		return;
	}
	if((sDir == NULL)||(sDir[0] == '\0')){
		pThis->pCache->sDir[0] = '\0';
		return;
	}
	if(strlen(sDir) >= sizeof(pThis->pCache->sDir)){
		daslog_warn_v("Node cache directory name too long, caching is off: %s", sDir);
		pThis->pCache->sDir[0] = '\0';
		return;
	}
	/* sDir may be our buffer, from DasCat_cacheDir(), to change the age */
	if(sDir != pThis->pCache->sDir) strcpy(pThis->pCache->sDir, sDir);
	pThis->pCache->nMaxAgeSec = nMaxAgeSec;
}

const char* DasCat_cacheDir(const DasCat* pThis)
{
	if((pThis->pCache == NULL)||(pThis->pCache->sDir[0] == '\0')) return NULL;
	return pThis->pCache->sDir;
}

int DasCat_cacheAge(const DasCat* pThis)
{
	return (pThis->pCache == NULL) ? 0 : pThis->pCache->nMaxAgeSec;
}

/* ************************************************************************* */
/* Root policy */

/* Roots are built on first use, so the policy is frozen by then */
/* das_strdup() gives NULL for "", and these fields promise a string */
static char* _dascat_strdupz(const char* s)
{
	return ((s == NULL)||(s[0] == '\0')) ? (char*)calloc(1, 1) : das_strdup(s);
}

static bool _DasCat_frozen(const DasCat* pThis)
{
	if(pThis->uOwned == 0) return false;
	daslog_error("Catalog policy can't change after a path has been resolved");
	return true;
}

DasErrCode DasCat_setPrefix(DasCat* pThis, const char* sUri)
{
	if(_DasCat_frozen(pThis)) return DASERR_CAT;
	if(pThis->bSeed){
		daslog_error_v("Relative paths start at the seed, %s", pThis->sSeedUri[0] ? pThis->sSeedUri : "a root");
		return DASERR_CAT;
	}
	if((sUri == NULL)||(sUri[0] == '\0')){
		daslog_error("A path prefix can't be empty");
		return DASERR_CAT;
	}
	if(das_isUrlOrFile(sUri)){
		daslog_error_v("'%s' is a location, a path prefix is a catalog URI", sUri);
		return DASERR_CAT;
	}
	free(pThis->sPrefix);
	pThis->sPrefix = _dascat_expandUri(sUri);
	/* A prefix is joined to relative paths by plain concatenation, so it
	   must end in the container's separator.  A bare shorthand names the
	   container; its separator is known, supply it. */
	if((strcmp(pThis->sPrefix, D2URI_SITES) == 0)||(strcmp(pThis->sPrefix, D2URI_ROOT "test") == 0)){
		char* s = das_string("%s:/", pThis->sPrefix);
		free(pThis->sPrefix);
		pThis->sPrefix = s;
	}
	pThis->bPrefixGiven = true;
	return DAS_OKAY;
}

const char* DasCat_prefix(const DasCat* pThis)
{
	return pThis->bSeed ? pThis->sSeedUri : pThis->sPrefix;
}

bool DasCat_hasSeed(const DasCat* pThis){ return pThis->bSeed; }

/* The mount whose URI covers sUri, or -1.  Longest match, as the mount
 * table itself resolves, though mounts rarely nest. */
static int _DasCat_mountFor(const DasCat* pThis, const char* sUri)
{
	int nBest = -1;
	size_t uBest = 0;
	for(size_t u = 0; u < pThis->uMounts; ++u){
		size_t uLen = strlen(pThis->aMounts[u].sUri);
		if((uLen > uBest) && (strncmp(sUri, pThis->aMounts[u].sUri, uLen) == 0)){
			nBest = (int)u;
			uBest = uLen;
		}
	}
	return nBest;
}

/* Add to the mount list, the URI already checked */
static DasErrCode _DasCat_addMount(DasCat* pThis, const char* sUrl, const char* sUri)
{
	struct das_cat_mount* a = (struct das_cat_mount*)realloc(
		pThis->aMounts, (pThis->uMounts + 1) * sizeof(struct das_cat_mount)
	);
	if(a == NULL) return das_error(DASERR_CAT, "Couldn't grow the mount list");
	pThis->aMounts = a;
	a[pThis->uMounts].sUrl = das_strdup(sUrl);
	a[pThis->uMounts].sUri = _dascat_expandUri(sUri);
	pThis->uMounts += 1;
	return DAS_OKAY;
}

/* The checks shared by seeds and mounts */
static bool _dascat_locationOk(const char* sUrl, const char* sUri, bool bUriNeeded)
{
	if(!das_isUrlOrFile(sUrl)){
		daslog_error_v("A node is loaded from a URL or a file, '%s' is neither", sUrl ? sUrl : "");
		return false;
	}
	if((sUri == NULL)||(sUri[0] == '\0')){
		if(!bUriNeeded) return true;
		daslog_error("A mount needs the catalog URI to put the node at");
		return false;
	}
	if(das_isUrlOrFile(sUri)){
		daslog_error_v("'%s' is a location, a catalog URI was expected", sUri);
		return false;
	}
	return true;
}

DasErrCode DasCat_setSeed(DasCat* pThis, const char* sUrl, const char* sUri)
{
	if(_DasCat_frozen(pThis)) return DASERR_CAT;
	if(pThis->bSeed){
		daslog_error("A session has one seed, and one is already set");
		return DASERR_CAT;
	}
	if(pThis->bPrefixGiven)
		daslog_info_v("Relative paths started at %s, now they start at the seed", pThis->sPrefix);
	if(!_dascat_locationOk(sUrl, sUri, false)) return DASERR_CAT;
	pThis->sSeedUrl = das_strdup(sUrl);
	pThis->sSeedUri = ((sUri != NULL) && (sUri[0] != '\0')) ? _dascat_expandUri(sUri) : (char*)calloc(1, 1);
	pThis->bSeed = true;
	return DAS_OKAY;
}

const char* DasCat_seedUrl(const DasCat* pThis){ return pThis->sSeedUrl; }

DasErrCode DasCat_mount(DasCat* pThis, const char* sUrl, const char* sUri)
{
	if(_DasCat_frozen(pThis)) return DASERR_CAT;
	if(!_dascat_locationOk(sUrl, sUri, true)) return DASERR_CAT;
	return _DasCat_addMount(pThis, sUrl, sUri);
}

size_t DasCat_mountCount(const DasCat* pThis){ return pThis->uMounts; }

/* ************************************************************************* */
/* Resolution */

/* The mount table, built on first use: a synthetic node whose children
 * are the mounts by URI, longest URI first so a mount under another
 * shadows it.  It never falls through to the federated catalog; that is
 * decided here, see _DasCat_byUri(). */
static DasNode* _DasCat_table(DasCat* pThis)
{
	if(pThis->pTable != NULL) return pThis->pTable;
	if(pThis->uMounts == 0) return NULL;

	const char** aUrl = (const char**)calloc(pThis->uMounts, sizeof(char*));
	const char** aUri = (const char**)calloc(pThis->uMounts, sizeof(char*));
	if((aUrl == NULL)||(aUri == NULL)){
		das_error(DASERR_CAT, "Couldn't allocate the mount table");
		free((void*)aUrl); free((void*)aUri);  /* quiet msvc */
		return NULL;
	}
	for(size_t u = 0; u < pThis->uMounts; ++u){
		aUrl[u] = pThis->aMounts[u].sUrl;
		aUri[u] = pThis->aMounts[u].sUri;
	}
	/* The table matches children in document order, so a mount at
	   site:/uiowa listed before one at site:/uiowa/juno/wav would shadow
	   it.  Longest URI first makes the order mounts were given in
	   irrelevant, as it should be: lists in a catalog are never
	   preferences.  Insertion sort, there are a handful at most. */
	for(size_t i = 1; i < pThis->uMounts; ++i){
		for(size_t j = i; (j > 0) && (strlen(aUri[j]) > strlen(aUri[j-1])); --j){
			const char* s = aUri[j]; aUri[j] = aUri[j-1]; aUri[j-1] = s;
			s = aUrl[j]; aUrl[j] = aUrl[j-1]; aUrl[j-1] = s;
		}
	}
	pThis->pTable = _dasnode_newMountTable(aUrl, aUri, pThis->uMounts);
	free((void*)aUrl);  /* quiet msvc */
	free((void*)aUri);  /* quiet msvc */
	return _DasCat_keep(pThis, pThis->pTable);
}

/* A full catalog URI under the current policy, sLastUri already set.
 * Mounts are opportunistic: a URI one covers is walked from the mount
 * table, anything else from the seed, or from the compiled in seed when
 * none is set.  Shared by DasCat_resolve() and DasCat_nearest(), which
 * has a URI in hand and must not go through the location test. */
static DasNode* _DasCat_byUri(DasCat* pThis, const char* sUri)
{
	if(_DasCat_mountFor(pThis, sUri) >= 0){
		DasNode* pTable = _DasCat_table(pThis);
		if(pTable == NULL) return NULL;
		return _DasCat_walkFrom(pThis, pTable, sUri);
	}
	if(pThis->bSeed){
		if(!DasCat_covers(pThis, sUri)){
			daslog_debug_v("%s is outside the seed %s", sUri, pThis->sSeedUri[0] ? pThis->sSeedUri : "(a root)");
			return NULL;
		}
		DasNode* pSeed = _DasCat_seedNode(pThis);
		if(pSeed == NULL) return NULL;
		/* The seed itself is asked for by its prefix; it may be a source,
		   which no walk can start from, so it is handed back directly */
		const char* sRest = sUri + strlen(pThis->sSeedUri);
		if(sRest[0] == '\0') return pSeed;
		return _DasCat_walkFrom(pThis, pSeed, sRest);
	}
	DasNode* pDas2 = _DasCat_das2(pThis);
	if(pDas2 == NULL) return NULL;
	return _DasCat_walkFrom(pThis, pDas2, sUri);
}

DasNode* DasCat_resolve(DasCat* pThis, const char* sPath)
{
	bool bNoPath = (sPath == NULL)||(sPath[0] == '\0');
	_dascat_setStr(&pThis->sLastPath, bNoPath ? NULL : sPath);
	free(pThis->sLastUri);     pThis->sLastUri = NULL;
	free(pThis->sNear);        pThis->sNear = NULL;
	free(pThis->sNearRest);    pThis->sNearRest = NULL;

	/* A node by location: a root, the location stands in for a
	   name in messages */
	if(!bNoPath && das_isUrlOrFile(sPath)){
		pThis->sLastUri = das_strdup(sPath);
		return _DasCat_keep(pThis, _dascat_loadUrl(
			sPath, NULL, pThis->pMgr, pThis->sAgent, pThis->pCache
		));
	}

	/* With a seed, relative paths start at it; a full URI goes through the
	 * mounts or nowhere */
	if(pThis->bSeed){
		if(bNoPath){
			pThis->sLastUri = _dascat_strdupz(pThis->sSeedUri);
			return _DasCat_seedNode(pThis);
		}
		if(das_isUri(sPath)){
			pThis->sLastUri = _dascat_expandUri(sPath);
			return _DasCat_byUri(pThis, pThis->sLastUri);
		}
		DasNode* pSeed = _DasCat_seedNode(pThis);
		if(pSeed == NULL){
			pThis->sLastUri = _dascat_strdupz(pThis->sSeedUri);
			return NULL;
		}
		/* Names below a root are the path from it; below a prefix the
		   seed's separator joins them.  The full name then goes through
		   the same door as a URI, so a mount under the seed is consulted
		   whichever way the path was spelled. */
		if(pThis->sSeedUri[0] == '\0'){
			pThis->sLastUri = das_strdup(sPath);
		}
		else{
			const char* sSep = DasNode_isCatalog(pSeed) ? DasNode_pathSep(pSeed) : "/";
			pThis->sLastUri = das_string("%s%s%s", pThis->sSeedUri, sSep ? sSep : "/", sPath);
		}
		return _DasCat_byUri(pThis, pThis->sLastUri);
	}

	/* Otherwise relative paths start at the prefix, mounts or not */
	if(bNoPath){
		/* The prefix with its trailing separator removed names the container */
		pThis->sLastUri = das_strdup(pThis->sPrefix);
		size_t u = strlen(pThis->sLastUri);
		while((u > 0) && ((pThis->sLastUri[u-1] == '/')||(pThis->sLastUri[u-1] == ':')))
			pThis->sLastUri[--u] = '\0';
	}
	else if(das_isUri(sPath))
		pThis->sLastUri = _dascat_expandUri(sPath);
	else
		pThis->sLastUri = das_string("%s%s", pThis->sPrefix, sPath);

	return _DasCat_byUri(pThis, pThis->sLastUri);
}

const char* DasCat_lastUri(const DasCat* pThis){ return pThis->sLastUri; }

bool DasCat_covers(const DasCat* pThis, const char* sUri)
{
	/* Mounts fall through to the federated catalog; only a seed walls paths
	 * off.  Names below a root seed are paths from it, so anything with a
	 * scheme in front is outside it. */
	if(!pThis->bSeed) return true;
	if(_DasCat_mountFor(pThis, sUri) >= 0) return true;
	if(pThis->sSeedUri[0] == '\0') return !das_isUri(sUri);
	return strncmp(sUri, pThis->sSeedUri, strlen(pThis->sSeedUri)) == 0;
}

DasNode* DasCat_nearest(DasCat* pThis, const char** psRest)
{
	if(psRest != NULL) *psRest = NULL;
	if(pThis->pNearNode != NULL){
		/* A root: nothing to resolve again, and no URI to report, so the
		   last path and URI say so with an empty string */
		DasNode* pNode = pThis->pNearNode;
		pThis->pNearNode = NULL;
		_dascat_setStr(&pThis->sLastPath, NULL);
		free(pThis->sLastUri);
		pThis->sLastUri = (char*)calloc(1, 1);
		if(psRest != NULL) *psRest = pThis->sNearRest;
		return pNode;
	}
	if(pThis->sNear == NULL) return NULL;

	/* The walk that failed said how far it got.  Loading that container
	 * again is cheap, loaded children or the node cache, and it has the
	 * documented effect on the last path and URI: afterwards they name the
	 * nearest node.  The walk's memory is taken first, the resolve clears
	 * it. */
	char* sNear = pThis->sNear;          pThis->sNear = NULL;
	char* sRest = pThis->sNearRest;      pThis->sNearRest = NULL;
	char* sOrigPath = pThis->sLastPath;  pThis->sLastPath = das_strdup(sNear);
	char* sOrigUri = pThis->sLastUri;    pThis->sLastUri = sNear;

	DasNode* pNode = _DasCat_byUri(pThis, pThis->sLastUri);
	if(pNode == NULL){
		/* It loaded a moment ago, so a server went away between calls;
		 * keep the caller's view of the original failure */
		free(sRest);
		free(pThis->sLastPath);  pThis->sLastPath = sOrigPath;
		free(pThis->sLastUri);   pThis->sLastUri = sOrigUri;
		return NULL;
	}
	free(sOrigPath);
	free(sOrigUri);
	free(pThis->sNearRest);
	pThis->sNearRest = sRest;   /* the caller's pointer, valid until the next resolve */
	if(psRest != NULL) *psRest = sRest;
	return pNode;
}

DasNode* DasCat_seed(DasCat* pThis)
{
	return pThis->bSeed ? _DasCat_seedNode(pThis) : _DasCat_das2(pThis);
}

DasNode* DasCat_subNode(DasCat* pThis, DasNode* pNode, const char* sRelPath)
{
	if(pNode == NULL){
		daslog_error("No node to load a child of");
		return NULL;
	}
	return _DasCat_walkFrom(pThis, pNode, sRelPath);
}

/* ************************************************************************* */
/* Sources */

DasSrc* DasCat_newSrc(DasCat* pThis, const char* sPath)
{
	DasNode* pNode = DasCat_resolve(pThis, sPath);
	return (pNode == NULL) ? NULL : new_DasSrc_node(pNode);
}
