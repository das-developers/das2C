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
#include "das2/node.h"


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


das_node_type_e das_node_type(const char* sType){
	if(sType == NULL) return d2node_inv;
	if(strcmp(sType, D2CV_TYPE_CATALOG) == 0) return d2node_catalog;
	if(strcmp(sType, D2CV_TYPE_COLLECTION) == 0) return d2node_collection;
	if(strcmp(sType, D2CV_TYPE_STREAM) == 0) return d2node_stream_src;
	if(strcmp(sType, D2CV_TYPE_TIMEAGG) == 0) return d2node_file_agg;
	if(strcmp(sType, D2CV_TYPE_SPASE) == 0) return d2node_spase_cat;
	if(strcmp(sType, D2Cv_TYPE_SPDF_MASTER) == 0) return d2node_spdf_cat;
	return d2node_inv;
}

/* ************************************************************************* */
/* Information function */

bool DasNode_isCatalog(const DasNode* pThis)
{
	return ((pThis->nType == d2node_catalog) ||
			  (pThis->nType == d2node_spdf_cat) ||
			  (pThis->nType == d2node_spase_cat) ||
			  (pThis->nType == d2node_collection));
}

bool DasNode_isJson(const DasNode* pThis){
	return ((pThis->nType == d2node_catalog)||
			  (pThis->nType == d2node_stream_src)||
			  (pThis->nType == d2node_file_agg) ||
			  (pThis->nType == d2node_collection));
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
const char* DasNode_rootStr(const DasNode* pThis, const char* sStr)
{
	if(!DasNode_isJson(pThis)){
		daslog_error("Non-JSON nodes not supported at this time");
		return NULL;
	}

	const DasJdo* pObj = DasNode_getJdoType(pThis, das_json_type_str, sStr);
	if(pObj == NULL){
		daslog_error_v("Error in node from %s, '%s' missing or not a string",
				         pThis->sURL, sStr);
		return NULL;
	}
	return ((das_json_str*)pObj->value)->string;
}


const char* DasNode_name(const DasNode* pThis){
	return DasNode_rootStr(pThis, D2FRAG_NAME);
}

const char* DasNode_title(const DasNode* pThis){
	return DasNode_rootStr(pThis, D2FRAG_TITLE);
}

const char* DasNode_summary(const DasNode* pThis){
	return DasNode_rootStr(pThis, D2FRAG_DESC);
}

const char* DasNode_srcUrl(const DasNode* pThis){ return pThis->sURL;}

/* ************************************************************************* */
/* Direct Node Construction */

/* make a node from a *specific* URL, does not call das_error functions unless
 * a memory allocation error occurs because network operations are assumed to
 * fail sometimes.   Returns a new node allocated on the heap if the URL
 * contained a recognizable DasCatalog object
 */
DasNode* _DasNode_mkNode(
	const char* sUrl, const char* sPathUri, DasCredMngr* pMgr, 
	const char* sAgent, float rConSec
){
	/* Read JSON bytes into the array, put a sanity limit of 20 MB on the read,
	 * no catalogs should ever get anywhere near that size, but you don't know */
	DasHttpResp httpRes;
	DasAry* pBytesAry = das_http_readUrl(
		sUrl, sAgent, pMgr, &httpRes, 1024*1024*20, rConSec
	);
	if(pBytesAry == NULL){ 
		daslog_warn(httpRes.sError);
		return NULL;
	}

	/* Save the final URL, may be needed for error messages, may not match
	 * sUrl due to re-directs */
	char sFinalUrl[256] = {'\0'};
	das_url_toStr(&(httpRes.url), sFinalUrl, 511);


	/* Parse the JSON data */
	size_t uBytes;
	const char* pBytes = (const char*) (DasAry_getBytesIn(pBytesAry, DIM0, &uBytes));
	if(uBytes < 2){
		daslog_error_v("String data from %s too small to be a valid JSON document", sUrl);
		return NULL;
	}

	struct das_json_parse_result_s jsonRes;
	DasJdo* pRoot;

	pRoot = das_json_parse_ex(pBytes, uBytes, das_jparse_flags_allow_json5,
			                    NULL, NULL, &jsonRes);

	dec_DasAry(pBytesAry);   // Done with the byte array, data in json buffer now
	DasHttpResp_freeFields(&httpRes); // Done with the headers etc.

	if(pRoot == NULL){
		char sTmp[256] = {'\0'};
		daslog_error_v("Error parsing JSON data for URL %s: %s", sFinalUrl,
		               json_parse_error_info(&jsonRes, sTmp, 255));
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

		const byte* pFill = NULL;
		pThis->pSubNodes = new_DasAry(
			"nodes", vtUnknown, sizeof(byte*), (const byte*)&pFill,
			RANK_1(0), UNIT_DIMENSIONLESS
		);
		pThis->pSubPaths = new_DasAry(
			"paths", vtByte, 0, NULL, RANK_2(0,0), UNIT_DIMENSIONLESS
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

/* ************************************************************************* */
/* Sub Node Construction */

DasNode* DasNode_subNode(
	DasNode* pThis, const char* sRelPath, DasCredMngr* pMgr, const char* sAgent
);

void del_NonRootNode(DasNode* pThis);


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
 * Since '_' could be the separater string for child 'survey', the path
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
bool _checkSubMatch(DasNode* pBase, const char* sSubPathWithSep)
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
DasNode* _DasNode_loadSubNode_dasCat(
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
					         pThis->base.sURL, pEl->name);
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
			 * we just recieved bad data and shouldn't waste time reading it 
			 * again */
			pNode = _DasNode_mkNode(sUrl, sSubUri, pMgr, sAgent, rConSec);
			if(pNode == NULL) continue;
			
			/* If after removing the child portion from the relative path there
			 * is nothing left we're done. */
			sSubRelPath = sRelPath + uLen;

			if(sSubRelPath[0] == '\0'){ 
				DasAry_append(pThis->pSubNodes, (const byte*) &pNode, 1);
				DasAry_append(pThis->pSubPaths, (const byte*) sChild, strlen(sChild)+1);
				DasAry_markEnd(pThis->pSubPaths, DIM1);
				return pNode;
			}
				
			/* There's more, see if it's possible for this child to complete the 
			 * path. */
			if(_checkSubMatch(pNode, sSubRelPath)){
				
				pDecendent = DasNode_subNode((DasNode*)pNode, sSubRelPath, pMgr, sAgent);
				if(pDecendent){
					/* Worked okay, cache the child node, but return the decendent
					 * node (however far down it came from */
					DasAry_append(pThis->pSubNodes, (const byte*) &pNode, 1);
					DasAry_append(pThis->pSubPaths, (const byte*) sChild, strlen(sChild)+1);
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
	

	daslog_error_v(
		"Node %s (URI '%s') has no child node that starts with %s",
		DasNode_name((DasNode*)pThis), DasNode_pathUri((DasNode*)pThis), sRelPath
	);

	return NULL;
}

DasNode* _DasNode_loadSubNode_spdfCat(
	DasCatNode* pThis, const char* sRelPath, DasCredMngr* pMgr, const char* sAgent
){
	daslog_error("SPDF catalogs are not yet supported");
	return NULL;
}

DasNode* _DasNode_loadSubNode_spaseCat(
	DasCatNode* pThis, const char* sRelPath, DasCredMngr* pMgr, const char* sAgent
){
	daslog_error("Spase catalogs are not yet supported");
	return NULL;
}

DasNode* DasNode_subNode(
	DasNode* pThis, const char* sRelPath, DasCredMngr* pMgr, const char* sAgent
){
	if((sRelPath == NULL) || (sRelPath[0] == '\0')){
		das_error(DASERR_NODE, "Empty relative path, can't lookup scheme definition");
		return NULL;
	}
	if(!DasNode_isCatalog(pThis)){
		daslog_error_v("Node %s from %s is a terminating node", DasNode_name(pThis));
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
			daslog_warn_v("Node at %s has a zero-length sub-path", pNode->sURL);
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

/* only deletes non-root nodes and any contained non-root nodes */
void del_NonRootNode(DasNode* pThis)
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

DasNode* new_RootNode(
	const char* sPathUri, DasCredMngr* pMgr, const char* sAgent
){
	/* Find the node using the path, start at the root */
	DasNode* pTop = NULL;
	DasNode* pNode = NULL;

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
	del_NonRootNode(pTop);

	return pNode;
}

