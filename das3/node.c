/* Copyright (C) 2018 Chris Piker <chris-piker@uiowa.edu>
 *
 * This file is part of libdas2, the Core Das2 C Library.
 *
 * Libdas2 is free software; you can redistribute it and/or modify it under
 * the terms of the GNU Lesser General Public License version 2.1 as published
 * by the Free Software Foundation.
 *
 * Libdas2 is distributed in the hope that it will be useful, but WITHOUT ANY
 * WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS
 * FOR A PARTICULAR PURPOSE.  See the GNU Lesser General Public License for
 * more details.
 *
 * You should have received a copy of the GNU Lesser General Public License
 * version 2.1 along with libdas2; if not, see <http://www.gnu.org/licenses/>.
 */

#define _POSIX_C_SOURCE 200112L
#include <pthread.h>
#include <errno.h>
#include <string.h>
#include <ctype.h>
#include <stdint.h>
#include <inttypes.h>
#include <time.h>
#include <sys/stat.h>

#ifndef _WIN32
#include <sys/socket.h>
#else
#include <winsock2.h>
#endif

#include <openssl/ssl.h>

#include "util.h"
#include "log.h"
#include "http.h"
#include "array.h"
#include "node.h"

#include "json.h"   /* Very nice lib from https://github.com/sheredom/json.h
                     * customized for local use */
#include "das3/node.h"


/* Time out values for connections */


/* Built-in catalog roots.  */
static const char* g_lDasDefRoots[2] = {
	"https://das2.org/catalog/index.json",
	"https://raw.githubusercontent.com/das-developers/das-cat/master/cat/index.json"
};
static int g_nDasDefRoots = 2;


const char** das_root_urls(size_t* pLen){
	if(pLen != NULL)
		*pLen = g_nDasDefRoots;
	return g_lDasDefRoots;
}


#define D2C_LOCAL_SUB_SZ 32

/* Catalog nodes have extra arrays to cache nodes */
#define _D2C_SEP_SZ 8
typedef struct das_catalog {
	DasNode base;

	DasAry* pSubNodes;

	DasAry* pSubPaths;

	/* The json object or xml element tag which contains the sub elements */
	void* pContainer;

	/* The path separator, defaults to '/' unless overridden by the object */
	char  sPathSep[_D2C_SEP_SZ];

	/* The name of the element which contains sub elements */
	char sContainer[64];

} DasCatNode;


static das_node_type_e das_node_type(const char* sType){
	if(sType == NULL) return d2node_inv;
	if(strcmp(sType, D2CV_TYPE_CATALOG) == 0)     return d2node_catalog;
	if(strcmp(sType, D2CV_TYPE_COLLECTION) == 0)  return d2node_collection;
	if(strcmp(sType, D2CV_TYPE_SRCSET) == 0)      return d2node_srcset;   // collection look alike
	if(strcmp(sType, D2CV_TYPE_STREAM) == 0)      return d2node_stream_src;
	if(strcmp(sType, D2CV_TYPE_TIMEAGG) == 0)     return d2node_file_agg;
	if(strcmp(sType, D2CV_TYPE_SPASE) == 0)       return d2node_spase_cat;
	if(strcmp(sType, D2Cv_TYPE_SPDF_MASTER) == 0) return d2node_spdf_cat;
	return d2node_inv;
}

/* ************************************************************************* */
/* Information function */

bool DasNode_isCatalog(const DasNode* pThis)
{
	return (
		(pThis->nType == d2node_catalog)    ||(pThis->nType == d2node_spdf_cat)
		||(pThis->nType == d2node_spase_cat)||(pThis->nType == d2node_collection)
		||(pThis->nType == d2node_srcset)
	);
}

/* These four were declared in node.h since 2018 but never defined, so any
   program that called them failed to link.  das_fetch is the first. */
bool DasNode_isStreamSrc(const DasNode* pThis){
	return (pThis != NULL) && (pThis->nType == d2node_stream_src);
}

bool DasNode_isSpaseRec(const DasNode* pThis){
	return (pThis != NULL) && (pThis->nType == d2node_spase_cat);
}

bool DasNode_isSpdfCat(const DasNode* pThis){
	return (pThis != NULL) && (pThis->nType == d2node_spdf_cat);
}

das_node_type_e DasNode_type(const DasNode* pThis){
	return (pThis != NULL) ? pThis->nType : d2node_inv;
}

bool DasNode_isJson(const DasNode* pThis){
	return (
		(pThis->nType == d2node_catalog)||(pThis->nType == d2node_stream_src)
		||(pThis->nType == d2node_file_agg)||(pThis->nType == d2node_collection)
		||(pThis->nType == d2node_srcset)
	);
}

const DasJdo* DasNode_getJdo(const DasNode* pThis, const char* sFragment)
{
	if(!DasNode_isJson(pThis)){
		daslog_error("Node data is not in JSON format");
		return NULL;
	}

	if((sFragment == NULL)||(strlen(sFragment) == 0))
		return (DasJdo*)pThis->pDom;

	return DasJdo_get((DasJdo*)pThis->pDom, sFragment);
}


const DasJdo* DasNode_getJdoType(
	const DasNode* pThis, enum das_json_type_e type, const char* sFragment
){
	const DasJdo* pObj = DasNode_getJdo(pThis, sFragment);
	if((pObj == NULL) || (pObj->type != type)){
		return NULL;
	}
	return pObj;
}

const char* DasNode_pathUri(const DasNode* pThis)
{
	return (pThis->sPath[0] == '\0' ? NULL : pThis->sPath);
}

/* helper, git a string value from the root of the dom */
static const char* DasNode_rootStr(const DasNode* pThis, const char* sStr)
{
	if(!DasNode_isJson(pThis)){
		daslog_error("Non-JSON nodes not supported at this time");
		return NULL;
	}

	/* Most root strings (title, description, version) are optional, so a
	   miss is the caller's business, not an error.  Callers that require a
	   field check for NULL and complain themselves. */
	const DasJdo* pObj = DasNode_getJdoType(pThis, das_json_type_str, sStr);
	if(pObj == NULL)
		return NULL;
	return ((das_json_str*)pObj->value)->string;
}


const char* DasNode_name(const DasNode* pThis){
	return DasNode_rootStr(pThis, D2FRAG_NAME);
}

const char* DasNode_label(const DasNode* pThis){
	const char* pLabel = DasNode_rootStr(pThis, D2FRAG_LABEL);
	if(pLabel != NULL)
		return pLabel;

	return DasNode_rootStr(pThis, D2FRAG_NAME);
}

const char* DasNode_title(const DasNode* pThis){
	return DasNode_rootStr(pThis, D2FRAG_TITLE);
}

const char* DasNode_summary(const DasNode* pThis){
	return DasNode_rootStr(pThis, D2FRAG_DESC);
}

const char* DasNode_srcUrl(const DasNode* pThis){ return pThis->sURL;}

const char* DasNode_version(const DasNode* pThis){
	return DasNode_rootStr(pThis, D2FRAG_VERSION);
}

const char* DasNode_pathSep(const DasNode* pThis){
	if(!DasNode_isCatalog(pThis)) return NULL;
	return ((const DasCatNode*)pThis)->sPathSep;
}

/* ************************************************************************* */
/* Child listings.  These read the container dictionary that was downloaded
 * with the node, so they never touch the network.  Linear walks of a linked
 * list, fine for the few dozen entries real catalogs have. */

size_t DasNode_subCount(const DasNode* pThis)
{
	if(!DasNode_isCatalog(pThis)) return 0;
	const DasJdo* pDir = (const DasJdo*) ((const DasCatNode*)pThis)->pContainer;
	if((pDir == NULL)||(pDir->type != das_json_type_dict)) return 0;
	return ((const das_json_dict*)pDir->value)->length;
}

static const das_json_dict_el* _DasNode_subEl(const DasNode* pThis, size_t uIdx)
{
	if(!DasNode_isCatalog(pThis)) return NULL;
	const DasJdo* pDir = (const DasJdo*) ((const DasCatNode*)pThis)->pContainer;
	if((pDir == NULL)||(pDir->type != das_json_type_dict)) return NULL;

	const das_json_dict_el* pEl = DasJdo_dictFirst(pDir);
	for(size_t u = 0; (u < uIdx) && (pEl != NULL); ++u) pEl = pEl->next;
	return pEl;
}

const char* DasNode_subName(const DasNode* pThis, size_t uIdx)
{
	const das_json_dict_el* pEl = _DasNode_subEl(pThis, uIdx);
	return pEl ? pEl->name->string : NULL;
}

const DasJdo* DasNode_subStub(const DasNode* pThis, size_t uIdx)
{
	const das_json_dict_el* pEl = _DasNode_subEl(pThis, uIdx);
	if((pEl == NULL)||(pEl->value == NULL)||(pEl->value->type != das_json_type_dict))
		return NULL;
	return pEl->value;
}

das_node_type_e DasNode_subType(const DasNode* pThis, size_t uIdx)
{
	const DasJdo* pStub = DasNode_subStub(pThis, uIdx);
	if(pStub == NULL) return d2node_inv;
	return das_node_type( DasJdo_string( DasJdo_get(pStub, D2FRAG_TYPE) ) );
}

/* ************************************************************************* */
/* Local file nodes.
 *
 * Catalog nodes are not only served over HTTP.  Test fixtures, detached
 * catalogs under development, and das2py's documented 'file://' URLs all
 * want a node read straight from disk.  Anything that is not an http(s) URL
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

static char g_sNodeCacheDir[256] = {'\0'};
static int  g_nNodeCacheAge = 0;

void DasNode_setCache(const char* sDir, int nMaxAgeSec)
{
	if((sDir == NULL)||(sDir[0] == '\0')){
		g_sNodeCacheDir[0] = '\0';
		return;
	}
	strncpy(g_sNodeCacheDir, sDir, sizeof(g_sNodeCacheDir) - 1);
	g_sNodeCacheDir[sizeof(g_sNodeCacheDir) - 1] = '\0';
	g_nNodeCacheAge = nMaxAgeSec;
}

/* Cache file name for a URL: the URL minus its scheme with everything
 * outside [A-Za-z0-9.-] turned into '_', then a hash of the whole URL so
 * that two URLs which sanitize alike still get separate files.  Readable
 * names make an 'ls' of the cache directory useful when debugging a
 * catalog, a bare hash would not. */
static bool _DasNode_cachePath(const char* sUrl, char* sPath, size_t uLen)
{
	if(g_sNodeCacheDir[0] == '\0') return false;

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

	int n = snprintf(sPath, uLen, "%s%s%s_%08" PRIx32 ".json", g_sNodeCacheDir, DAS_DSEPS, sName, uHash);
	return (n > 0) && ((size_t)n < uLen);
}

/* A fresh enough cached copy, or NULL */
static DasAry* _DasNode_readCache(const char* sPath)
{
	if(g_nNodeCacheAge <= 0) return NULL;
	struct stat st;
	if(stat(sPath, &st) != 0) return NULL;
	if((time(NULL) - st.st_mtime) > g_nNodeCacheAge) return NULL;
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

/* ************************************************************************* */
/* Direct Node Construction */

/* Build a node from JSON text.  sUrl is where it came from and sFinalUrl
 * the same after redirects, both for messages.  sPathUri may be NULL. */
static DasNode* _DasNode_fromJson(
	const char* pBytes, size_t uBytes, const char* sUrl, const char* sFinalUrl,
	const char* sPathUri
){
	if(uBytes < 2){
		daslog_error_v("String data from %s too small to be a valid JSON document", sUrl);
		return NULL;
	}

	struct das_json_parse_result_s jsonRes;
	DasJdo* pRoot;

	pRoot = das_json_parse_ex(pBytes, uBytes, das_jparse_flags_allow_json5,
			                    NULL, NULL, &jsonRes);

	if(pRoot == NULL){
		char sTmp[256] = {'\0'};
		daslog_error_v("Error parsing JSON data for URL %s: %s", sFinalUrl,
		               das_json_parse_error_info(&jsonRes, sTmp, 255));
		return NULL;
	}

	const char* sType = DasJdo_string( DasJdo_get(pRoot, D2FRAG_TYPE) );
	if(sType == NULL){
		daslog_error_v("Error in catalog object from %s, '%s' element missing"
				         "or not a string at root level.", sFinalUrl, D2FRAG_TYPE);
		free(pRoot);
		return NULL;
	}

	das_node_type_e nType = das_node_type( sType );
	if( nType == d2node_inv){
		daslog_error_v("Error in catalog object from %s, 'TYPE' value %s is "
				         "unknown", sFinalUrl, sType);
		free(pRoot);
		return NULL;
	}

	/* Initialize non-container types */
	DasNode* pBase = NULL;
	if((nType == d2node_stream_src)||(nType == d2node_file_agg)){
		pBase = (DasNode*)calloc(1, sizeof(DasNode));
		if(pBase == NULL){
			das_error(DASERR_NODE, "Couldn't allocate node");
			free(pRoot);
			return NULL;
		}
	}
	else{
		/* This code only knows the container type 'catalog' from Catalog
		 * and 'sources' from collection.
		 * The SpdfMasterCat has the container name 'datasite' but we don't
		 * have code to parse those yet.*/
		const char* sContainer = D2FRAG_SUB_PATHS;
		if(nType == d2node_collection) sContainer = D2FRAG_SOURCES;
		const DasJdo* pVal;
		if( ((pVal = DasJdo_get(pRoot, sContainer)) == NULL) ||
		    (pVal->type != das_json_type_dict) ){
			daslog_error_v("Error in catalog object from %s, missing dictionary "
					         "element '%s'", sFinalUrl, sContainer);
			free(pRoot);
			return NULL;
		}

		DasCatNode* pThis = (DasCatNode*)calloc(1, sizeof(DasCatNode));
		if(pThis == NULL){
			das_error(DASERR_NODE, "Couldn't allocate catalog node");
			free(pRoot);
			return NULL;
		}
		pBase = (DasNode*)pThis;

		const ubyte* pFill = NULL;
		pThis->pSubNodes = new_DasAry(
			"nodes", vtUnknown, sizeof(ubyte*), (const ubyte*)&pFill,
			RANK_1(0), UNIT_DIMENSIONLESS
		);
		pThis->pSubPaths = new_DasAry(
			"paths", vtUByte, 0, NULL, RANK_2(0,0), UNIT_DIMENSIONLESS
		);
		DasAry_setUsage(pThis->pSubPaths, D2ARY_AS_STRING);

		pThis->pContainer = (DasJdo*)pVal;   /* Save the location of the sub-element tag */
		strncpy(pThis->sContainer, sContainer, 63);

		/* See if a path sep object is present */
		pThis->sPathSep[0] = '/';
		const DasJdo* pSep = DasJdo_get(pRoot, D2FRAG_PATH_SEP);
		if(pSep != NULL){
			if(pSep->type == das_json_type_str)
				strncpy(pThis->sPathSep, DasJdo_string(pSep), 7);
			if(pSep->type == das_json_type_null)
				pThis->sPathSep[0] = '\0';  /* Empty string */
		}
	}

	/* Initialize common stuff */
	pBase->nType = nType;
	strncpy(pBase->sURL, sUrl, 511);
	pBase->pDom = pRoot;
	if(sPathUri != NULL) strncpy(pBase->sPath, sPathUri, 511);

	return pBase;
}

/* make a node from a *specific* URL, does not call das_error functions unless
 * a memory allocation error occurs because network operations are assumed to
 * fail sometimes.   Returns a new node allocated on the heap if the URL
 * contained a recognizable DasCatalog object
 */
static DasNode* _DasNode_mkNode(
	const char* sUrl, const char* sPathUri, DasCredMngr* pMgr, 
	const char* sAgent, float rConSec
){
	/* Save the final URL, may be needed for error messages, may not match
	 * sUrl due to re-directs */
	char sFinalUrl[512] = {'\0'};

	DasAry* pBytesAry = NULL;
	char sCache[512] = {'\0'};
	bool bFromCache = false;
	if(_DasNode_isFileUrl(sUrl)){
		pBytesAry = _DasNode_readFile(sUrl);
		strncpy(sFinalUrl, sUrl, sizeof(sFinalUrl) - 1);
	}
	else if(_DasNode_cachePath(sUrl, sCache, sizeof(sCache)) &&
	        ((pBytesAry = _DasNode_readCache(sCache)) != NULL)){
		daslog_debug_v("Node %s read from cache", sUrl);
		strncpy(sFinalUrl, sUrl, sizeof(sFinalUrl) - 1);
		bFromCache = true;
	}
	else{
		/* Read JSON bytes into the array, put a sanity limit of 20 MB on the
		 * read, no catalogs should ever get anywhere near that size, but you
		 * don't know */
		DasHttpResp httpRes;
		pBytesAry = das_http_readUrl(
			sUrl, sAgent, pMgr, &httpRes, 1024*1024*20, rConSec
		);
		if(pBytesAry == NULL){
			daslog_warn_v("%s", httpRes.sError);
			DasHttpResp_freeFields(&httpRes);
			return NULL;
		}
		das_url_toStr(&(httpRes.url), sFinalUrl, sizeof(sFinalUrl));
		DasHttpResp_freeFields(&httpRes); /* Done with the headers etc. */
	}
	if(pBytesAry == NULL) return NULL;

	size_t uBytes;
	const char* pBytes = (const char*) (DasAry_getBytesIn(pBytesAry, DIM0, &uBytes));
	DasNode* pNode = _DasNode_fromJson(pBytes, uBytes, sUrl, sFinalUrl, sPathUri);
	if(pNode != NULL){ pNode->pMgr = pMgr; pNode->sAgent = sAgent; }

	/* Only documents that parse are worth keeping, a server error page is not */
	if((pNode != NULL) && (sCache[0] != '\0') && !bFromCache)
		_DasNode_writeCache(sCache, pBytes, uBytes);

	dec_DasAry(pBytesAry);   /* Done with the byte array, data in json buffer now */
	return pNode;
}

/* ************************************************************************* */
/* Sub Node Construction */

DasNode* DasNode_subNode(
	DasNode* pThis, const char* sRelPath, DasCredMngr* pMgr, const char* sAgent
);

static void del_NonRootNode(DasNode* pThis);


/* Helper for loadSubNode and subNode
 * 
 * We have a fundamental difficulty in looking for sub nodes.  Since a catalog
 * can't know it's child's separator string (with out loading it), there is an
 * ambiguity between a child that starts with a same name as another child.  
 * For example:
 * 
 *   ...cassini/survey/das2
 *   ...cassini/survey_keyparams/das2
 * 
 * Since '_' could be the separator string for child 'survey', the path
 * ..."cassini/survey_keyparams/das2" could (at the cassini level) match this
 * node layout:
 * 
 *   ...cassini -> survey -> keyparams -> das2
 * 
 * or this layout.
 * 
 *   ...cassini -> survey -> das2
 * 
 * Only by checking children of children or by reading the child's directory
 * separator string can the ambiguity be resolved.  The problem is present 
 * for both cached nodes and internet resource nodes.
 * 
 * To resolve this we're going to add a double check to the child reading loop.
 * After the child is loaded see if it's impossible to match the rest of the
 * string, if so, keep looking.
 * 
 * There's more to the path, see if it's impossible for this node to complete
 * the path, (ex: _keyparams/das2) so:
 * 
 *  1. make sure the sub-node is a catalog type.  (must be if we're
 *     not done), if not keep looking.
 * 
 *  2. See if the first part of the remaining string matches the
 *     separator, if not keep looking
 * 
 *  3. See if the child has a child that starts with the rest of
 *     the string minus the separator, if not ... keep looking.
 * 
 * 
 * @param pBase - A DasNode
 * @param sSubPath - The sub path *WITH THE SEPARATOR ATTACHED* 
 * 
 */
static bool _checkSubMatch(DasNode* pBase, const char* sSubPathWithSep)
{
	
	if(!DasNode_isCatalog(pBase)) return false;
	
	DasCatNode* pThis = (DasCatNode*)pBase;
	
	
	const char* sSep = "/";
	const DasJdo* pObj = DasNode_getJdoType(pBase, das_json_type_str, D2FRAG_PATH_SEP);
	if(pObj != NULL){
		sSep = ((das_json_str*)pObj->value)->string;
	}
	else{
		pObj = DasNode_getJdoType(pBase, das_json_type_null, D2FRAG_PATH_SEP);
		if(pObj != NULL) sSep = "";
	}
		
	size_t uSepLen = strlen(sSep);
	
	if(strncmp(sSep, sSubPathWithSep, uSepLen) != 0) return false;
	
	/* Okay, check my children... */
	if(!DasNode_isJson(pBase)){
		daslog_error("XML catalogs not yet supported");
		return false;
	}
	const DasJdo* pDir = (DasJdo*) pThis->pContainer;

	if((pDir == NULL) || (pDir->type != das_json_type_dict)){
		daslog_error_v("Catalog container node missing for node from %s",
				         pThis->base.sURL);
		return false;
	}
	
	const das_json_dict_el* pEl = NULL;
	const char* sChild;
	size_t uChildLen;
	for(pEl = DasJdo_dictFirst(pDir); pEl != NULL; pEl = pEl->next){
		sChild = pEl->name->string;
		uChildLen = strlen(sChild);
		if(strncmp(sChild, sSubPathWithSep + uSepLen, uChildLen) == 0) return true;
	}
	return false;
}


/* sRelPath is the desired sub path from the point of this object.  If it's
 * NULL, that means you just wanted what you already have (pThis) so there's
 * no point in calling this function */
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
			pNode = _DasNode_mkNode(sUrl, sSubUri, pMgr, sAgent, rConSec);
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
			if(_checkSubMatch(pNode, sSubRelPath)){
				
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
				del_NonRootNode(pNode);
				pNode = NULL;
			}
		}
		
		/* Must have been a look-alike child (or some other failure), try again */
		del_NonRootNode(pNode); 
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
				if( ! _checkSubMatch(pNode, sSubRelPath)) continue;

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
/* Selective Destruction */

/* only deletes non-root nodes and any contained non-root nodes.  Not public:
   a non-root node lives in its parent's pSubNodes cache, freeing one alone
   would leave a dangling entry there.  Callers only ever own roots. */
static void del_NonRootNode(DasNode* pThis)
{
	if(pThis == NULL) return;
	if(pThis->bIsRoot) return;

	/* Cascade deletes for non-root sub items */
	if(DasNode_isCatalog(pThis)){
		DasCatNode* pCat = (DasCatNode*)pThis;

		ptrdiff_t uNodes = DasAry_lengthIn(pCat->pSubNodes, DIM0);
		for(ptrdiff_t u = 0; u < uNodes; ++u){
			DasNode** ppSub = (DasNode**)DasAry_getAt(pCat->pSubNodes, vtUnknown, IDX0(u));
			del_NonRootNode(*ppSub);
		}
		dec_DasAry(pCat->pSubNodes);
		dec_DasAry(pCat->pSubPaths);
	}
	/* Now del self */
	if(!DasNode_isJson(pThis))
		das_error(DASERR_NODE, "Memory Leak! Handling for non-JSON nodes has "
				     "not been implemented");
	else
		free(pThis->pDom);

	free(pThis);
}
/* only deletes root nodes and any contained non-root nodes */
void del_RootNode(DasNode* pThis)
{
	if(pThis == NULL) return;
	if(!pThis->bIsRoot) return;

	pThis->bIsRoot = false;
	del_NonRootNode(pThis);
}

/* ************************************************************************* */
/* Root Node Construction */

DasNode* new_RootNode_url(
	const char* sUrl, const char* sPathUri, DasCredMngr* pMgr, const char* sAgent
){
	/* Since they want a specific URL, go for the max timeout */
	DasNode* pNode = _DasNode_mkNode(sUrl, sPathUri, pMgr, sAgent, DASHTTP_TO_MAX);
	if(pNode) pNode->bIsRoot = true;
	return pNode;
}

/* Append a JSON string literal, escaping the two characters that matter */
static size_t _jsonStr(char* sOut, size_t uOut, const char* sIn)
{
	size_t u = 0;
	if(u < uOut - 1) sOut[u++] = '"';
	for(; (*sIn != '\0') && (u < uOut - 3); ++sIn){
		if((*sIn == '"')||(*sIn == '\\')) sOut[u++] = '\\';
		sOut[u++] = *sIn;
	}
	if(u < uOut - 1) sOut[u++] = '"';
	sOut[u] = '\0';
	return u;
}

DasNode* new_RootNode_seed(
	const char* sUri, const char* sUrl, bool bFallThrough, DasCredMngr* pMgr,
	const char* sAgent
){
	if((sUri == NULL)||(sUri[0] == '\0')||(sUrl == NULL)||(sUrl[0] == '\0')){
		daslog_error("A catalog seed needs both a URI and a URL");
		return NULL;
	}

	/* The seed root is an ordinary Catalog node that only exists in memory.
	 * Its separator is null, like the federated root's, so child names are
	 * whole URI prefixes.  The seed comes first, then, if asked for, a child
	 * with the empty name whose URLs are the federated roots.  The empty name
	 * is a prefix of every path, so anything the seed does not claim falls
	 * through to the normal catalog, while everything under the seed URI is
	 * shadowed.  Without that child the seed is all there is. */
	char sJson[4096];
	size_t u = 0;
	u += snprintf(sJson + u, sizeof(sJson) - u,
		"{\"type\":\"Catalog\",\"version\":\"0.5\",\"name\":\"seed\","
		"\"title\":\"Seeded catalog root\",\"separator\":null,\"catalog\":{"
	);
	u += _jsonStr(sJson + u, sizeof(sJson) - u, sUri);
	u += snprintf(sJson + u, sizeof(sJson) - u, ":{\"type\":\"Catalog\",\"urls\":[");
	u += _jsonStr(sJson + u, sizeof(sJson) - u, sUrl);
	u += snprintf(sJson + u, sizeof(sJson) - u, "]}");
	if(bFallThrough){
		u += snprintf(sJson + u, sizeof(sJson) - u, ",\"\":{\"type\":\"Catalog\",\"urls\":[");
		for(int i = 0; i < g_nDasDefRoots; ++i){
			if(i > 0) u += snprintf(sJson + u, sizeof(sJson) - u, ",");
			u += _jsonStr(sJson + u, sizeof(sJson) - u, g_lDasDefRoots[i]);
		}
		u += snprintf(sJson + u, sizeof(sJson) - u, "]}");
	}
	u += snprintf(sJson + u, sizeof(sJson) - u, "}}");
	if(u >= sizeof(sJson) - 1){
		daslog_error("Catalog seed URI or URL too long");
		return NULL;
	}

	/* Nothing is fetched here, the first subNode call does that and takes
	   the manager and agent from the root */
	DasNode* pRoot = _DasNode_fromJson(sJson, u, "memory:seed", "memory:seed", "");
	if(pRoot != NULL){
		pRoot->bIsRoot = true;
		pRoot->pMgr = pMgr;
		pRoot->sAgent = sAgent;
	}
	return pRoot;
}

DasNode* new_RootNode(
	const char* sPathUri, DasCredMngr* pMgr, const char* sAgent
){
	/* Find the node using the path, start at the root */
	DasNode* pTop = NULL;
	DasNode* pNode = NULL;

	/* das2py accepts "site:/..." and "test:/..." as shorthand for the two
	   well known children of the das2 root.  Do the same here so every
	   client gets it, and so the two libraries agree on what a path means. */
	char sFullUri[600] = {'\0'};
	if((sPathUri != NULL) && (
		(strncmp(sPathUri, "site:", 5) == 0) || (strncmp(sPathUri, "test:", 5) == 0)
	)){
		snprintf(sFullUri, sizeof(sFullUri) - 1, "%s%s", D2URI_ROOT, sPathUri);
		sPathUri = sFullUri;
	}

	/* Changing node lookup and time outs.  New strategy, try to contact a node
	   if it doesn't work within the time out, try the next one.  If all fail
		then increase the time out and try again.
	*/
	
	float rConSec = DASHTTP_TO_MIN;
	
	while((rConSec <= DASHTTP_TO_MAX )&&(pNode == NULL)){
		for(int i = 0; i < g_nDasDefRoots; ++i){
			/* Credentials should never be needed for top level nodes */
			pTop = _DasNode_mkNode(g_lDasDefRoots[i], NULL, NULL, sAgent, rConSec);
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

	/* I'm keeping this node, so set it as a root item so that it will not be
	 * deleted without an explicit call to del_RootNode, then delete the rest,
	 * pTop will not be deleted if pTop == pNode */
	pNode->bIsRoot = true;
	pNode->pMgr = pMgr;        /* pTop was fetched without them, the kept node
	                              hands them to whatever it loads later */
	pNode->sAgent = sAgent;
	del_NonRootNode(pTop);

	return pNode;
}

