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

#include <string.h>
#include <stdlib.h>
#include <ctype.h>

#include "util.h"
#include "log.h"
#include "node_priv.h"
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
	 * catalog, unless a root is set, then at that node, or with mounts, at
	 * the first mounted node, or at the prefix if one was given. */
	char* sPrefix;          /* never NULL */
	bool bPrefixGiven;
	struct das_cat_mount root;      /* the whole catalog; sUrl NULL if unset,
	                                   sUri NULL for a nameless root */
	struct das_cat_mount* aMounts;  /* over the federated roots */
	size_t uMounts;

	DasNode* pRoot;         /* memory root of the root and mount modes */
	DasNode** aRoots;       /* every memory root made, freed with the session */
	size_t uRoots;
	size_t uRootCap;

	char* sLastPath;        /* the caller's path to the last resolve, or NULL */
	char* sLastUri;         /* that path as the policy understood it */
};

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

/* site:/x and test:/x are shorthand for the two well known children
 * of the das2 root.  new_RootNode() expands them too; doing it here as well
 * keeps DasCat_lastUri() the full form. */
static char* _expand(const char* sIn)
{
	if(_pfx(sIn, "site:")||_pfx(sIn, "test:"))
		return das_string(D2URI_ROOT "%s", sIn);
	return das_strdup(sIn);
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

/* Take ownership of a memory root */
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
	for(size_t u = 0; u < pThis->uRoots; ++u) del_RootNode(pThis->aRoots[u]);
	free(pThis->aRoots);
	if(pThis->pMgr != NULL) del_CredMngr(pThis->pMgr);
	free(pThis->pCache);

	free(pThis->sAgent);
	free(pThis->sPrefix);
	free(pThis->root.sUrl);
	free(pThis->root.sUri);
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
	pThis->bPrefixGiven = true;
	return DAS_OKAY;
}

const char* DasCat_prefix(const DasCat* pThis){ return pThis->sPrefix; }

bool DasCat_hasRoot(const DasCat* pThis){ return pThis->root.sUrl != NULL; }

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
	if(DasCat_hasRoot(pThis)){
		daslog_error("A catalog has one root, and one is already set");
		return DASERR_CAT;
	}
	if(pThis->uMounts > 0){
		daslog_error("A root replaces the federated catalog, which the mounts build on; use one or the other");
		return DASERR_CAT;
	}
	if(!_DasCat_locationOk(sUrl, sUri, false)) return DASERR_CAT;
	_setStr(&pThis->root.sUrl, sUrl);
	free(pThis->root.sUri);
	pThis->root.sUri = ((sUri != NULL) && (sUri[0] != '\0')) ? _expand(sUri) : NULL;
	return DAS_OKAY;
}

const char* DasCat_rootUrl(const DasCat* pThis){ return pThis->root.sUrl; }

const char* DasCat_rootUri(const DasCat* pThis){ return pThis->root.sUri; }

DasErrCode DasCat_mount(DasCat* pThis, const char* sUrl, const char* sUri)
{
	if(_DasCat_frozen(pThis)) return DASERR_CAT;
	if(DasCat_hasRoot(pThis)){
		daslog_error("A mount builds on the federated catalog, which a root replaces; use one or the other");
		return DASERR_CAT;
	}
	if(!_DasCat_locationOk(sUrl, sUri, true)) return DASERR_CAT;

	struct das_cat_mount* a = (struct das_cat_mount*)realloc(
		pThis->aMounts, (pThis->uMounts + 1) * sizeof(struct das_cat_mount)
	);
	if(a == NULL) return das_error(DASERR_CAT, "Couldn't grow the mount list");
	pThis->aMounts = a;
	a[pThis->uMounts].sUrl = das_strdup(sUrl);
	a[pThis->uMounts].sUri = _expand(sUri);
	pThis->uMounts += 1;
	return DAS_OKAY;
}

size_t DasCat_mountCount(const DasCat* pThis){ return pThis->uMounts; }

/* ************************************************************************* */
/* Resolution */

/* Root node of the root and mount modes, loaded on first use.  A named
 * root is a mount root with one mount and no fall through. */
static DasNode* _DasCat_root(DasCat* pThis)
{
	if(pThis->pRoot != NULL) return pThis->pRoot;

	if(pThis->root.sUrl != NULL){
		if(pThis->root.sUri == NULL){
			pThis->pRoot = _DasNode_newRootUrl(
				pThis->root.sUrl, "", pThis->pMgr, pThis->sAgent, pThis->pCache
			);
		}
		else{
			const char* aUrl[1] = { pThis->root.sUrl };
			const char* aUri[1] = { pThis->root.sUri };
			pThis->pRoot = _DasNode_newMountRoot(
				aUrl, aUri, 1, false, pThis->pMgr, pThis->sAgent, pThis->pCache
			);
		}
	}
	else if(pThis->uMounts > 0){
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
		pThis->pRoot = _DasNode_newMountRoot(
			aUrl, aUri, pThis->uMounts, true, pThis->pMgr, pThis->sAgent, pThis->pCache
		);
		free(aUrl);
		free(aUri);
	}
	return _DasCat_keep(pThis, pThis->pRoot);
}

DasNode* DasCat_resolve(DasCat* pThis, const char* sPath)
{
	bool bNoPath = (sPath == NULL)||(sPath[0] == '\0');
	_setStr(&pThis->sLastPath, bNoPath ? NULL : sPath);
	free(pThis->sLastUri);
	pThis->sLastUri = NULL;

	/* A node by location, named by itself */
	if(!bNoPath && das_isUrlOrFile(sPath)){
		pThis->sLastUri = das_strdup(sPath);
		return _DasCat_keep(pThis, _DasNode_newRootUrl(
			sPath, sPath, pThis->pMgr, pThis->sAgent, pThis->pCache
		));
	}

	const char* sRootUri = pThis->root.sUri;
	bool bMounted = (pThis->uMounts > 0);
	if((sRootUri != NULL) || bMounted){
		DasNode* pRoot = _DasCat_root(pThis);
		if(pRoot == NULL) return NULL;
		const char* sTopUri = bMounted ? pThis->aMounts[0].sUri : sRootUri;

		/* No path: the named node itself.  A full URI, or a relative path
		 * under an explicit prefix when the catalog is mounted over: the
		 * mount root decides whether a mount or the federated catalog owns
		 * it.  Anything else is relative to the named node. */
		if(bNoPath){
			pThis->sLastUri = das_strdup(sTopUri);
			return DasNode_subNode(pRoot, sTopUri, NULL, NULL);
		}
		if(bMounted && pThis->bPrefixGiven)
			pThis->sLastUri = das_string("%s%s", pThis->sPrefix, sPath);
		else
			pThis->sLastUri = _expand(sPath);
		if(_pfx(pThis->sLastUri, "tag:")||_pfx(pThis->sLastUri, sTopUri))
			return DasNode_subNode(pRoot, pThis->sLastUri, NULL, NULL);

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

	if(pThis->root.sUrl != NULL){
		DasNode* pRoot = _DasCat_root(pThis);
		if(pRoot == NULL) return NULL;
		if(bNoPath){
			pThis->sLastUri = das_strdup(pThis->root.sUrl);
			return pRoot;
		}
		pThis->sLastUri = das_strdup(sPath);
		return DasNode_subNode(pRoot, sPath, NULL, NULL);
	}

	/* The federated catalog under the prefix */
	if(bNoPath){
		/* The prefix with its trailing separator removed names the container */
		pThis->sLastUri = das_strdup(pThis->sPrefix);
		size_t u = strlen(pThis->sLastUri);
		while((u > 0) && ((pThis->sLastUri[u-1] == '/')||(pThis->sLastUri[u-1] == ':')))
			pThis->sLastUri[--u] = '\0';
	}
	else if(_pfx(sPath, "tag:")||_pfx(sPath, "site:")||_pfx(sPath, "test:"))
		pThis->sLastUri = _expand(sPath);
	else
		pThis->sLastUri = das_string("%s%s", pThis->sPrefix, sPath);

	return _DasCat_keep(pThis, _DasNode_newRoot(
		pThis->sLastUri, pThis->pMgr, pThis->sAgent, pThis->pCache
	));
}

const char* DasCat_lastUri(const DasCat* pThis){ return pThis->sLastUri; }

bool DasCat_covers(const DasCat* pThis, const char* sUri)
{
	/* Mounts fall through to the federated catalog and a nameless root has
	 * no URIs to be outside of, only a named root walls paths off */
	if(pThis->root.sUri == NULL) return true;
	return _pfx(sUri, pThis->root.sUri);
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
