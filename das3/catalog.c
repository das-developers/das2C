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
 * moved here once a second client wanted the same root policy.
 * Policy misuse is reported with daslog_error and DASERR_CAT rather than
 * das_error, because the usual source of a bad policy is a command line
 * argument and a program should get to decide its own exit code for that. */

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

struct das_cat_mount {
	char* sUrl;
	char* sUri;
};

struct das_cat {
	DasCredMngr* pMgr;      /* owned */
	char* sAgent;           /* owned copy, or NULL for the library default */
	DasNodeCache* pCache;   /* owned, NULL until DasCat_setCache() */

	/* Root policy.  Relative paths start at sPrefix in the federated
	 * catalog, or with mounts at the root node if one was set, else at
	 * the first mounted node, or at the prefix if one was given.
	 *
	 * A root is a mount that turns off fall through: the mounts are then
	 * the whole catalog.  Keeping one list means a root and further mounts
	 * mix freely, which is how an offline test catalog is assembled from a
	 * handful of files. */
	char* sPrefix;          /* never NULL */
	bool bPrefixGiven;
	struct das_cat_mount* aMounts;  /* named nodes loaded by location */
	size_t uMounts;
	bool bIsolated;         /* set by DasCat_setRoot(): no fall through */
	size_t uRoot;           /* index of the root in aMounts when isolated */

	DasNode* pRoot;         /* the mount root, built on first use */
	DasNode** aRoots;       /* every node a constructor gave us, freed with the session */
	size_t uRoots;
	size_t uRootCap;

	char* sLastPath;        /* the caller's path to the last resolve, or NULL */
	char* sLastUri;         /* that path as the policy understood it */
};

/* ************************************************************************* */
/* Loading nodes: the native driver
 *
 * Everything below, down to the small helpers, is what node.c does not do:
 * read a file, consult the node cache, make an HTTP request, and walk a
 * catalog tree by loading one node after another.  node.c only parses
 * bytes it is handed and reads the result.  A client with its own
 * transport, a browser build in particular, uses node.h alone and never
 * links this file.  Moved here from node.c as one piece; the recursive
 * walk is replaced by DasNodeWalk in the next step.  Until then the
 * public pull constructors in node.h are implemented here. */

/* ************************************************************************* */
/* Local file nodes.
 *
 * Catalog nodes are not only served over HTTP.  Test fixtures, detached
 * catalogs under development, and 'file://' URLs handed over by other
 * tools all want a node read straight from disk.  Anything that is not an http(s) URL
 * is treated as a path; the "file://" prefix is accepted and stripped.
 * Only the path forms "file:///abs/path" and "file://rel/path" are handled,
 * host names in file URLs are not. */

static bool _DasNode_isFileUrl(const char* sUrl)
{
	if(sUrl == NULL) return false;
	if(strncmp(sUrl, "http://", 7) == 0) return false;
	if(strncmp(sUrl, "https://", 8) == 0) return false;
	return true;
}

static DasAry* _DasNode_readFile(const char* sUrl)
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

/* Only a DasCat has cache settings, nodes made through the public
 * constructors in node.h carry NULL and are never cached.  One knob, per
 * session, instead of a second process wide one to confuse it with. */

/* Cache file name for a URL: the URL minus its scheme with everything
 * outside [A-Za-z0-9.-] turned into '_', then a hash of the whole URL so
 * that two URLs which sanitize alike still get separate files.  Readable
 * names make an 'ls' of the cache directory useful when debugging a
 * catalog, a bare hash would not. */
static bool _DasNode_cachePath(
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
static DasAry* _DasNode_readCache(const DasNodeCache* pCache, const char* sPath)
{
	if((pCache == NULL)||(pCache->nMaxAgeSec <= 0)) return NULL;
	struct stat st;
	if(stat(sPath, &st) != 0) return NULL;
	if((time(NULL) - st.st_mtime) > pCache->nMaxAgeSec) return NULL;
	return _DasNode_readFile(sPath);
}

/* Written to a side file then renamed, so a reader never sees a partial
 * document when two programs walk the catalog at once.  Failures only cost
 * the cache, so they are logged at debug level and otherwise ignored. */
static void _DasNode_writeCache(const char* sPath, const char* pBytes, size_t uBytes)
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
 * sends neither.  The date is never derived from the cache file's own
 * mtime, which renewal moves forward. */
static void _DasNode_hdrPath(const char* sCache, char* sHdr, size_t uLen)
{
	snprintf(sHdr, uLen, "%s.hdr", sCache);
}

/* Request header lines for the cached copy at sCache, "" if it has none */
static void _DasNode_readValidators(const char* sCache, char* sReq, size_t uLen)
{
	sReq[0] = '\0';
	char sHdr[600];
	_DasNode_hdrPath(sCache, sHdr, sizeof(sHdr));
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
static void _DasNode_writeValidators(const char* sCache, const DasHttpResp* pRes)
{
	char sHdr[600];
	_DasNode_hdrPath(sCache, sHdr, sizeof(sHdr));
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

/* make a node from a *specific* URL, does not call das_error functions unless
 * a memory allocation error occurs because network operations are assumed to
 * fail sometimes.   Returns a new node allocated on the heap if the URL
 * contained a recognizable DasCatalog object
 */
static DasNode* _DasNode_mkNode(
	const char* sUrl, const char* sPathUri, DasCredMngr* pMgr, 
	const char* sAgent, float rConSec, const DasNodeCache* pCache
){
	/* Save the final URL, may be needed for error messages, may not match
	 * sUrl due to re-directs */
	char sFinalUrl[512] = {'\0'};

	DasAry* pBytesAry = NULL;
	char sCache[512] = {'\0'};
	bool bFromCache = false;   /* copy used as is, nothing to write back */
	bool bRenewed = false;     /* server confirmed the copy, rewrite it to move its age */
	DasHttpResp httpRes;
	memset(&httpRes, 0, sizeof(DasHttpResp));

	if(_DasNode_isFileUrl(sUrl)){
		pBytesAry = _DasNode_readFile(sUrl);
		strncpy(sFinalUrl, sUrl, sizeof(sFinalUrl) - 1);
	}
	else if(_DasNode_cachePath(pCache, sUrl, sCache, sizeof(sCache)) &&
	        ((pBytesAry = _DasNode_readCache(pCache, sCache)) != NULL)){
		daslog_debug_v("Node %s read from cache", sUrl);
		strncpy(sFinalUrl, sUrl, sizeof(sFinalUrl) - 1);
		bFromCache = true;
	}
	else{
		/* No copy, or one older than the session trusts.  With validators
		 * from the old copy the request is conditional and a 304 renews it
		 * for no body bytes.  If the server cannot be reached at all the old
		 * copy still beats nothing: a warm cache keeps working offline.  The
		 * 20 MB limit is a sanity check, no catalog node comes near it. */
		char sReqHdrs[1024] = {'\0'};
		bool bHaveOld = (sCache[0] != '\0') && das_isfile(sCache);
		if(bHaveOld) _DasNode_readValidators(sCache, sReqHdrs, sizeof(sReqHdrs));

		pBytesAry = das_http_readUrl_ex(
			sUrl, sAgent, pMgr, &httpRes, 1024*1024*20, rConSec, sReqHdrs
		);
		if(pBytesAry != NULL){
			das_url_toStr(&(httpRes.url), sFinalUrl, sizeof(sFinalUrl));
		}
		else if(httpRes.nCode == 304){
			daslog_debug_v("Node %s unchanged on the server, cached copy renewed", sUrl);
			pBytesAry = _DasNode_readFile(sCache);
			strncpy(sFinalUrl, sUrl, sizeof(sFinalUrl) - 1);
			bRenewed = true;
		}
		else if(bHaveOld){
			daslog_warn_v("%s, using the cached copy of %s",
			              httpRes.sError ? httpRes.sError : "Fetch failed", sUrl);
			pBytesAry = _DasNode_readFile(sCache);
			strncpy(sFinalUrl, sUrl, sizeof(sFinalUrl) - 1);
			bFromCache = true;
		}
		else{
			daslog_warn_v("%s", httpRes.sError ? httpRes.sError : "Fetch failed");
		}
	}
	if(pBytesAry == NULL){
		DasHttpResp_freeFields(&httpRes);
		return NULL;
	}

	size_t uBytes;
	const char* pBytes = (const char*) (DasAry_getBytesIn(pBytesAry, DIM0, &uBytes));
	DasNode* pNode = _DasNode_fromJson(pBytes, uBytes, sUrl, sFinalUrl, sPathUri);
	if(pNode != NULL){ pNode->pMgr = pMgr; pNode->sAgent = sAgent; pNode->pCache = pCache; }

	/* Only documents that parse are worth keeping, a server error page is
	 * not.  A renewal rewrites the same bytes, which is what moves the
	 * copy's age, and keeps the validators it already has. */
	if((pNode != NULL) && (sCache[0] != '\0') && !bFromCache){
		_DasNode_writeCache(sCache, pBytes, uBytes);
		if(!bRenewed) _DasNode_writeValidators(sCache, &httpRes);
	}

	DasHttpResp_freeFields(&httpRes);
	dec_DasAry(pBytesAry);   /* Done with the byte array, data in json buffer now */
	return pNode;
}

/* ************************************************************************* */
/* Sub Node Construction */


static DasNode* _DasNode_loadSubNode_dasCat(
	DasCatNode* pThis, const char* sRelPath, DasCredMngr* pMgr,
	const char* sAgent
){
	if(!DasNode_isJson((DasNode*)pThis)){
		daslog_error("XML catalogs not yet supported");
		return NULL;
	}
	const DasJdo* pDir = (DasJdo*) pThis->pContainer;

	if((pDir == NULL) || (pDir->type != das_json_type_dict)){
		daslog_error_v("Catalog container node missing for node from %s",
				         pThis->base.sURL);
		return NULL;
	}

	/* Run over all the keys and see if any of them match the front of the
	 * requested relative path.  If so try all the URLs given for full object
	 * realization before giving up */
	const das_json_dict_el* pEl = NULL;
	size_t uLen;
	const DasJdo* pChild = NULL;
	const char* sChild = NULL;
	const DasJdo* pChildUrls = NULL;
	const das_json_ary_el* pUrlEl;
	char sSubUri[560] = {'\0'};
	const char* sUrl = NULL;
	const char* sSubRelPath = NULL;
	DasNode* pNode = NULL;
	DasNode* pDecendent = NULL;
	int nUrl;

	/* Trying it implement a tree search that will auto backout and try again
	 * if a URL turns out to be broken, or contains bad data, or is the wrong
	 * child. */
	for(pEl = DasJdo_dictFirst(pDir); pEl != NULL; pEl = pEl->next){
		sChild = pEl->name->string;

		uLen = strlen(sChild);

		/* See if this child and the requested item have the same chars up to
		 * the length of the child */
		if(strncmp(sChild, sRelPath, uLen) != 0) continue;

		pChild = pEl->value;  /* If this never get's set, then we had no
									  * children with matching names */

		/* Just stop using this catalog node if it has broken syntax */
		if((pChild == NULL )||(pChild->type != das_json_type_dict)){
			daslog_error_v("Catalog error @ %s: sub item '%s' is not a dictionary",
					         pThis->base.sURL, pEl->name->string);
			return NULL;
		}

		pChildUrls = DasJdo_get(pChild, D2FRAG_URLS);
		if((pChildUrls == NULL)||(pChildUrls->type != das_json_type_ary)){
			daslog_error_v("From %s: %s element of node '%s' doesn't "
					         "have a URLS array", pThis->base.sURL, sChild,
					         pThis->sContainer);
			return NULL;
		}

		/* Loop over URLs for child */
		nUrl = -1;
		pNode = NULL;
		
		/* this is a slow time out.. */
		float rConSec = DASHTTP_TO_MIN * DASHTTP_TO_MULTI;
		
		for(pUrlEl = DasJdo_aryFirst(pChildUrls); pUrlEl != NULL; 
			 pUrlEl = pUrlEl->next){
			pNode = NULL;
			nUrl += 1;
			sUrl = DasJdo_string(pUrlEl->value);
			if(sUrl == NULL){
				daslog_error_v("From %s: %s/%s/%d element is not a string",
						         pThis->base.sURL, D2FRAG_URLS, sChild, nUrl);
				continue;
			}

			/* tell sub-item it's name */
			snprintf(sSubUri, 559, "%s%s%s", pThis->base.sPath, pThis->sPathSep, sChild);
				
			/* TODO: Split mkNode and server contact into two separate functions 
			 * so that we know if we can try again with a slow server, of if
			 * we just received bad data and shouldn't waste time reading it 
			 * again */
			pNode = _DasNode_mkNode(sUrl, sSubUri, pMgr, sAgent, rConSec, pThis->base.pCache);
			if(pNode == NULL) continue;
			
			/* If after removing the child portion from the relative path there
			 * is nothing left we're done. */
			sSubRelPath = sRelPath + uLen;

			if(sSubRelPath[0] == '\0'){ 
				DasAry_append(pThis->pSubNodes, (const ubyte*) &pNode, 1);
				DasAry_append(pThis->pSubPaths, (const ubyte*) sChild, strlen(sChild)+1);
				DasAry_markEnd(pThis->pSubPaths, DIM1);
				return pNode;
			}
				
			/* There's more, see if it's possible for this child to complete the 
			 * path. */
			if(_DasNode_subMatch(pNode, sSubRelPath)){
				
				pDecendent = DasNode_subNode((DasNode*)pNode, sSubRelPath, pMgr, sAgent);
				if(pDecendent){
					/* Worked okay, cache the child node, but return the descendant
					 * node (however far down it came from */
					DasAry_append(pThis->pSubNodes, (const ubyte*) &pNode, 1);
					DasAry_append(pThis->pSubPaths, (const ubyte*) sChild, strlen(sChild)+1);
					DasAry_markEnd(pThis->pSubPaths, DIM1);
				
					return pDecendent;
				}
			}
			else{
				/* This child can't match, stop checking this URL set*/
				_DasNode_delTree(pNode);
				pNode = NULL;
			}
		}
		
		/* Must have been a look-alike child (or some other failure), try again */
		_DasNode_delTree(pNode); 
		pNode = NULL;
	}
	

	/* A miss at one level is normal while the walk backs out and tries other
	 * URLs or children, so it is not an error here.  The caller knows whether
	 * the whole path failed and has the user's wording of it. */
	const char* sUri = DasNode_pathUri((DasNode*)pThis);
	daslog_info_v(
		"Node %s (URI '%s') has no child node that starts with %s",
		DasNode_label((DasNode*)pThis), sUri ? sUri : "", sRelPath
	);

	return NULL;
}

static DasNode* _DasNode_loadSubNode_spdfCat(
	DasCatNode* pThis, const char* sRelPath, DasCredMngr* pMgr, const char* sAgent
){
	daslog_error("SPDF catalogs are not yet supported");
	return NULL;
}

static DasNode* _DasNode_loadSubNode_spaseCat(
	DasCatNode* pThis, const char* sRelPath, DasCredMngr* pMgr, const char* sAgent
){
	daslog_error("Spase catalogs are not yet supported");
	return NULL;
}

DasNode* DasNode_subNode(
	DasNode* pThis, const char* sRelPath, DasCredMngr* pMgr, const char* sAgent
){
	/* Credentials and agent flow down from the root unless overridden here */
	if(pMgr == NULL) pMgr = pThis->pMgr;
	if(sAgent == NULL) sAgent = pThis->sAgent;

	if((sRelPath == NULL) || (sRelPath[0] == '\0')){
		das_error(DASERR_NODE, "Empty relative path, can't lookup scheme definition");
		return NULL;
	}
	if(!DasNode_isCatalog(pThis)){
		daslog_error_v("Node %s from %s is a terminating node", DasNode_label(pThis),
		               DasNode_srcUrl(pThis));
		return NULL;
	}
	DasCatNode* pCat = (DasCatNode*)pThis;

	if((pCat->sPathSep[0] != '\0')&&
	   (strncmp(sRelPath, pCat->sPathSep, strlen(pCat->sPathSep)) == 0)){
		sRelPath += strlen(pCat->sPathSep);
	}

	/* Search the local child cache first before calling on the inter-tubes */
	const char* sSubPath = NULL;
	const char* sSubRelPath = NULL;
	ptrdiff_t uChildren = DasAry_lengthIn(pCat->pSubNodes, DIM0);
	size_t uLen = 0;
	DasNode* pNode = NULL;
	DasNode* pRet = NULL;
	for(ptrdiff_t u = 0; u < uChildren; ++u){
		sSubPath = (const char*)DasAry_getBytesIn(pCat->pSubPaths, DIM1_AT(u), &uLen);

		/* From the DasAry's point of view, the item length includes the null
		 * because it has to store the null.  But from strncmp's point of view
		 * we don't count the null because the rel path might not be a terminating
		 * path (i.e. it may have components below the child */
		if(uLen == 0){
			daslog_warn_v("Node at %s has a zero-length sub-path", pThis->sURL);
			continue;
		}
		else{
			uLen -= 1;
		}

		if(strncmp(sSubPath, sRelPath, uLen) == 0){

			/* This child completes at least part of the subpath */
			pNode = *((DasNode**) DasAry_getAt(pCat->pSubNodes, vtUnknown, IDX0(u)));

			sSubRelPath = sRelPath + uLen;
			if(sSubRelPath[0] == '\0'){
				/* actually it completes all of it*/
				return pNode;
			}
			else{
				/* There's more, see if it's possible for this child to complete 
				 * the path, if not don't fail, just keep looking */
				if( ! _DasNode_subMatch(pNode, sSubRelPath)) continue;

				pRet = DasNode_subNode(pNode, sSubRelPath, pMgr, sAgent);
				if(pRet != NULL) return pRet;
			}
		}
	}

	/* I don't have this sub node: Internet! To the rescue! */

	switch(pThis->nType){
	case d2node_collection:
	case d2node_catalog:
	case d2node_srcset:
		return _DasNode_loadSubNode_dasCat(pCat, sRelPath, pMgr, sAgent);
	case d2node_spdf_cat:
		return _DasNode_loadSubNode_spdfCat(pCat, sRelPath, pMgr, sAgent);
	case d2node_spase_cat:
		return _DasNode_loadSubNode_spaseCat(pCat, sRelPath, pMgr, sAgent);
		break;
	default:
		/* Should have been ruled out by constructor, but double check since
		 * new types may be added */
		das_error(DASERR_ASSERT, "Logic error in libdas2"); return NULL;
	}

	return NULL;
}

/* ************************************************************************* */
/* Construction */

DasNode* _DasNode_newUrl(
	const char* sUrl, const char* sPathUri, DasCredMngr* pMgr, const char* sAgent,
	const DasNodeCache* pCache
){
	/* Every node has a URI; one loaded on its own is named by where it is */
	char sUri[600];
	if((sPathUri == NULL)||(sPathUri[0] == '\0')) _das_uri_local(sUrl, sUri, sizeof(sUri));
	else _das_uri_expand(sPathUri, sUri, sizeof(sUri));

	/* Since they want a specific URL, go for the max timeout */
	DasNode* pNode = _DasNode_mkNode(sUrl, sUri, pMgr, sAgent, DASHTTP_TO_MAX, pCache);
	if(pNode) pNode->bOwner = true;
	return pNode;
}

DasNode* new_DasNode_url(
	const char* sUrl, const char* sPathUri, DasCredMngr* pMgr, const char* sAgent
){
	return _DasNode_newUrl(sUrl, sPathUri, pMgr, sAgent, NULL);
}

DasNode* _DasNode_new(
	const char* sPathUri, DasCredMngr* pMgr, const char* sAgent,
	const DasNodeCache* pCache
){
	/* Find the node using the path, start at the root */
	DasNode* pTop = NULL;
	DasNode* pNode = NULL;

	/* Expanding shorthands here, in the library, means every client gets
	   the same spelling.  Names under local: belong to nodes loaded by
	   location in some session; the federated roots have never heard of
	   them and asking would only cost a round trip. */
	char sFullUri[600] = {'\0'};
	if(sPathUri != NULL){
		sPathUri = _das_uri_expand(sPathUri, sFullUri, sizeof(sFullUri));
		if(_das_uri_isLocal(sPathUri)){
			daslog_debug_v("%s is a local: name, those are not routed through the federated catalog", sPathUri);
			return NULL;
		}
	}

	/* Changing node lookup and time outs.  New strategy, try to contact a node
	   if it doesn't work within the time out, try the next one.  If all fail
		then increase the time out and try again.
	*/
	
	size_t uRoots = 0;
	const char** aRoots = das_root_urls(&uRoots);
	float rConSec = DASHTTP_TO_MIN;
	
	while((rConSec <= DASHTTP_TO_MAX )&&(pNode == NULL)){
		for(size_t i = 0; i < uRoots; ++i){
			/* Credentials should never be needed for top level nodes */
			pTop = _DasNode_mkNode(aRoots[i], NULL, NULL, sAgent, rConSec, pCache);
			if(pTop == NULL) continue;
			if(pTop->nType != d2node_catalog) continue;

			if(sPathUri == NULL){ /* They may be looking for a root node */
				pNode = pTop;
				break;
			}

			/* Depends on the fact that top node is a das catalog */
			pNode = _DasNode_loadSubNode_dasCat((DasCatNode*)pTop, sPathUri, pMgr, sAgent);
			if(pNode != NULL) break;
		}
		rConSec *= DASHTTP_TO_MULTI;
	}

	if(pNode == NULL){
		daslog_error_v("Failed to load the requested node %s from any of the "
				         "built in catalog trees", sPathUri);
		return NULL;
	}

	/* The caller gets this node, so mark it theirs before freeing the walk
	 * above it; _DasNode_delTree() then leaves it alone even when
	 * pTop == pNode */
	pNode->bOwner = true;
	pNode->pMgr = pMgr;        /* pTop was fetched without them, the kept node
	                              hands them to whatever it loads later */
	pNode->sAgent = sAgent;
	pNode->pCache = pCache;
	_DasNode_delTree(pTop);

	return pNode;
}

DasNode* new_DasNode(const char* sPathUri, DasCredMngr* pMgr, const char* sAgent)
{
	return _DasNode_new(sPathUri, pMgr, sAgent, NULL);
}


/* ************************************************************************* */
/* Small helpers */

static bool _pfx(const char* s, const char* sPrefix)
{
	return (s != NULL) && (strncmp(s, sPrefix, strlen(sPrefix)) == 0);
}

static void _setStr(char** ps, const char* s)
{
	free(*ps);
	*ps = (s != NULL) ? das_strdup(s) : NULL;
}

/* Shorthands are expanded by node.c as well; doing it here too keeps
 * DasCat_lastUri() the full form even when a resolve fails early. */
static char* _expand(const char* sIn)
{
	char sBuf[600];
	return das_strdup(_das_uri_expand(sIn, sBuf, sizeof(sBuf)));
}

bool das_isUri(const char* sPath)
{
	if((sPath == NULL) || !isalpha((unsigned char)sPath[0])) return false;
	const char* p = sPath + 1;
	while(isalnum((unsigned char)*p) || (*p == '+') || (*p == '-') || (*p == '.')) ++p;
	/* A single letter before the colon is a Windows drive, not a scheme */
	return (*p == ':') && (p > sPath + 1);
}

bool das_isUrlOrFile(const char* sPath)
{
	if(sPath == NULL) return false;
	if(_pfx(sPath, "http://")||_pfx(sPath, "https://")||_pfx(sPath, "file://")) return true;
	if((sPath[0] == '.')||(sPath[0] == '/')||(sPath[0] == '\\')) return true;
	if(isalpha((unsigned char)sPath[0]) && (sPath[1] == ':') && ((sPath[2] == '\\')||(sPath[2] == '/')))
		return true;   /* a Windows drive, catalog URIs never have their first colon at index 1 */
	size_t u = strlen(sPath);
	return (u > 5) && (strcmp(sPath + u - 5, ".json") == 0);
}

/* Take ownership of a node a constructor returned */
static DasNode* _DasCat_keep(DasCat* pThis, DasNode* pRoot)
{
	if(pRoot == NULL) return NULL;
	if(pThis->uRoots == pThis->uRootCap){
		size_t uCap = (pThis->uRootCap == 0) ? 8 : 2 * pThis->uRootCap;
		DasNode** a = (DasNode**)realloc(pThis->aRoots, uCap * sizeof(DasNode*));
		if(a == NULL){
			das_error(DASERR_CAT, "Couldn't grow the catalog root list");
			return pRoot;   /* leaks the root rather than losing the node */
		}
		pThis->aRoots = a;
		pThis->uRootCap = uCap;
	}
	pThis->aRoots[pThis->uRoots++] = pRoot;
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
	 * first.  The policy root is in aRoots like every other root. */
	for(size_t u = 0; u < pThis->uRoots; ++u) del_DasNode(pThis->aRoots[u]);
	free(pThis->aRoots);
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
	/* sDir may be our own buffer, from DasCat_cacheDir(), to change the age */
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
static bool _DasCat_frozen(const DasCat* pThis)
{
	if(pThis->uRoots == 0) return false;
	daslog_error("Catalog policy can't change after a path has been resolved");
	return true;
}

DasErrCode DasCat_setPrefix(DasCat* pThis, const char* sUri)
{
	if(_DasCat_frozen(pThis)) return DASERR_CAT;
	if((sUri == NULL)||(sUri[0] == '\0')){
		daslog_error("A path prefix can't be empty");
		return DASERR_CAT;
	}
	if(das_isUrlOrFile(sUri)){
		daslog_error_v("'%s' is a location, a path prefix is a catalog URI", sUri);
		return DASERR_CAT;
	}
	free(pThis->sPrefix);
	pThis->sPrefix = _expand(sUri);
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

const char* DasCat_prefix(const DasCat* pThis){ return pThis->sPrefix; }

bool DasCat_hasRoot(const DasCat* pThis){ return pThis->bIsolated; }

/* The mount whose URI covers sUri, or -1.  Longest match, as the mount
 * root itself resolves, though mounts rarely nest. */
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

/* Add to the mount list.  An empty URI names the node by its location. */
static DasErrCode _DasCat_addMount(DasCat* pThis, const char* sUrl, const char* sUri)
{
	struct das_cat_mount* a = (struct das_cat_mount*)realloc(
		pThis->aMounts, (pThis->uMounts + 1) * sizeof(struct das_cat_mount)
	);
	if(a == NULL) return das_error(DASERR_CAT, "Couldn't grow the mount list");
	pThis->aMounts = a;
	a[pThis->uMounts].sUrl = das_strdup(sUrl);
	if((sUri == NULL)||(sUri[0] == '\0')){
		char sBuf[600];
		a[pThis->uMounts].sUri = das_strdup(_das_uri_local(sUrl, sBuf, sizeof(sBuf)));
	}
	else{
		a[pThis->uMounts].sUri = _expand(sUri);
	}
	pThis->uMounts += 1;
	return DAS_OKAY;
}

/* The checks shared by roots and mounts */
static bool _DasCat_locationOk(const char* sUrl, const char* sUri, bool bUriNeeded)
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

DasErrCode DasCat_setRoot(DasCat* pThis, const char* sUrl, const char* sUri)
{
	if(_DasCat_frozen(pThis)) return DASERR_CAT;
	if(pThis->bIsolated){
		daslog_error("A catalog has one root, and one is already set");
		return DASERR_CAT;
	}
	if(!_DasCat_locationOk(sUrl, sUri, false)) return DASERR_CAT;
	if(_DasCat_addMount(pThis, sUrl, sUri) != DAS_OKAY) return DASERR_CAT;
	pThis->uRoot = pThis->uMounts - 1;
	pThis->bIsolated = true;
	return DAS_OKAY;
}

const char* DasCat_rootUrl(const DasCat* pThis)
{
	return pThis->bIsolated ? pThis->aMounts[pThis->uRoot].sUrl : NULL;
}

const char* DasCat_rootUri(const DasCat* pThis)
{
	return pThis->bIsolated ? pThis->aMounts[pThis->uRoot].sUri : NULL;
}

DasErrCode DasCat_mount(DasCat* pThis, const char* sUrl, const char* sUri)
{
	if(_DasCat_frozen(pThis)) return DASERR_CAT;
	if(!_DasCat_locationOk(sUrl, sUri, true)) return DASERR_CAT;
	return _DasCat_addMount(pThis, sUrl, sUri);
}

size_t DasCat_mountCount(const DasCat* pThis){ return pThis->uMounts; }

/* ************************************************************************* */
/* Resolution */

/* The mount root, loaded on first use.  With a root set the mounts are the
 * whole catalog, otherwise unclaimed paths fall through to the federated
 * roots. */
static DasNode* _DasCat_root(DasCat* pThis)
{
	if(pThis->pRoot != NULL) return pThis->pRoot;
	if(pThis->uMounts == 0) return NULL;

	const char** aUrl = (const char**)calloc(pThis->uMounts, sizeof(char*));
	const char** aUri = (const char**)calloc(pThis->uMounts, sizeof(char*));
	if((aUrl == NULL)||(aUri == NULL)){
		das_error(DASERR_CAT, "Couldn't allocate the mount root");
		free(aUrl); free(aUri);
		return NULL;
	}
	for(size_t u = 0; u < pThis->uMounts; ++u){
		aUrl[u] = pThis->aMounts[u].sUrl;
		aUri[u] = pThis->aMounts[u].sUri;
	}
	/* The mount root matches children in document order, so a mount at
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
	pThis->pRoot = _DasNode_newMountRoot(
		aUrl, aUri, pThis->uMounts, !pThis->bIsolated, pThis->pMgr, pThis->sAgent,
		pThis->pCache
	);
	free(aUrl);
	free(aUri);
	return _DasCat_keep(pThis, pThis->pRoot);
}

/* A local: name reaches only the session's own mounts, see D2URI_LOCAL */
static bool _DasCat_unrouted(const DasCat* pThis, const char* sUri)
{
	if(!_das_uri_isLocal(sUri) || (_DasCat_mountFor(pThis, sUri) >= 0)) return false;
	daslog_debug_v("%s is a local: name and nothing is mounted there", sUri);
	return true;
}

DasNode* DasCat_resolve(DasCat* pThis, const char* sPath)
{
	bool bNoPath = (sPath == NULL)||(sPath[0] == '\0');
	_setStr(&pThis->sLastPath, bNoPath ? NULL : sPath);
	free(pThis->sLastUri);
	pThis->sLastUri = NULL;

	/* A node by location, named under local: */
	if(!bNoPath && das_isUrlOrFile(sPath)){
		char sUri[600];
		pThis->sLastUri = das_strdup(_das_uri_local(sPath, sUri, sizeof(sUri)));
		return _DasCat_keep(pThis, _DasNode_newUrl(
			sPath, pThis->sLastUri, pThis->pMgr, pThis->sAgent, pThis->pCache
		));
	}

	if(pThis->uMounts > 0){
		DasNode* pRoot = _DasCat_root(pThis);
		if(pRoot == NULL) return NULL;
		const char* sTopUri = pThis->aMounts[pThis->bIsolated ? pThis->uRoot : 0].sUri;

		/* No path: the top node itself.  A full URI, or a relative path
		 * under an explicit prefix: the mount root decides whether a mount
		 * or the federated catalog owns it.  Anything else is relative to
		 * the top node. */
		if(bNoPath){
			pThis->sLastUri = das_strdup(sTopUri);
			return DasNode_subNode(pRoot, sTopUri, NULL, NULL);
		}
		if(pThis->bPrefixGiven && !das_isUri(sPath))
			pThis->sLastUri = das_string("%s%s", pThis->sPrefix, sPath);
		else
			pThis->sLastUri = _expand(sPath);
		if(das_isUri(pThis->sLastUri)){
			if(_DasCat_unrouted(pThis, pThis->sLastUri)) return NULL;
			return DasNode_subNode(pRoot, pThis->sLastUri, NULL, NULL);
		}

		DasNode* pTop = DasNode_subNode(pRoot, sTopUri, NULL, NULL);
		if(pTop == NULL){
			_setStr(&pThis->sLastUri, sTopUri);
			return NULL;
		}
		const char* sSep = DasNode_isCatalog(pTop) ? DasNode_pathSep(pTop) : "/";
		free(pThis->sLastUri);
		pThis->sLastUri = das_string("%s%s%s", sTopUri, sSep ? sSep : "/", sPath);
		return DasNode_subNode(pTop, sPath, NULL, NULL);
	}

	/* The federated catalog under the prefix */
	if(bNoPath){
		/* The prefix with its trailing separator removed names the container */
		pThis->sLastUri = das_strdup(pThis->sPrefix);
		size_t u = strlen(pThis->sLastUri);
		while((u > 0) && ((pThis->sLastUri[u-1] == '/')||(pThis->sLastUri[u-1] == ':')))
			pThis->sLastUri[--u] = '\0';
	}
	else if(das_isUri(sPath))
		pThis->sLastUri = _expand(sPath);
	else
		pThis->sLastUri = das_string("%s%s", pThis->sPrefix, sPath);

	if(_DasCat_unrouted(pThis, pThis->sLastUri)) return NULL;
	return _DasCat_keep(pThis, _DasNode_new(
		pThis->sLastUri, pThis->pMgr, pThis->sAgent, pThis->pCache
	));
}

const char* DasCat_lastUri(const DasCat* pThis){ return pThis->sLastUri; }

bool DasCat_covers(const DasCat* pThis, const char* sUri)
{
	/* local: names go nowhere but a mount; otherwise mounts fall through to
	 * the federated catalog and only a root walls paths off */
	if(_das_uri_isLocal(sUri) || pThis->bIsolated) return _DasCat_mountFor(pThis, sUri) >= 0;
	return true;
}

DasNode* DasCat_nearest(DasCat* pThis, const char** psRest)
{
	if(psRest != NULL) *psRest = NULL;
	if((pThis->sLastPath == NULL) || das_isUrlOrFile(pThis->sLastPath)) return NULL;

	/* The resolves below overwrite the last path and URI, take them back
	 * so the caller's view of the failure survives a miss here */
	char* sOrigPath = pThis->sLastPath;
	char* sOrigUri = pThis->sLastUri;
	pThis->sLastPath = NULL;
	pThis->sLastUri = NULL;

	char* sParent = das_strdup(sOrigPath);
	DasNode* pNode = NULL;
	size_t uCut = 0;
	while(true){
		size_t u = strlen(sParent);
		while((u > 0) && ((sParent[u-1] == '/')||(sParent[u-1] == ':'))) sParent[--u] = '\0';
		char* pLast = strrchr(sParent, '/');
		if((pLast == NULL)||(pLast == sParent)) break;
		*pLast = '\0';
		uCut = (size_t)(pLast - sParent);   /* sParent is a prefix copy of sOrigPath */
		DasNode* p = DasCat_resolve(pThis, sParent);
		if((p != NULL) && DasNode_isCatalog(p)){ pNode = p; break; }
	}
	free(sParent);
	free(pThis->sLastPath);
	pThis->sLastPath = sOrigPath;

	if(pNode == NULL){
		free(pThis->sLastUri);
		pThis->sLastUri = sOrigUri;
		return NULL;
	}
	free(sOrigUri);
	if(psRest != NULL){
		const char* s = sOrigPath + uCut;
		while((*s == '/')||(*s == ':')) ++s;
		*psRest = s;
	}
	return pNode;
}

/* ************************************************************************* */
/* Sources */

DasSrc* DasCat_newSrc(DasCat* pThis, const char* sPath)
{
	DasNode* pNode = DasCat_resolve(pThis, sPath);
	return (pNode == NULL) ? NULL : new_DasSrc_node(pNode);
}
