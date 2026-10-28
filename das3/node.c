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

/* Current upgrades provided here (2026-10-27)

   1. JSON Collection objects (v0.5) are given mime type data

   no others at this time
*/

#define _POSIX_C_SOURCE 200112L
#include <string.h>
#include <ctype.h>

#ifndef _WIN32
#include <unistd.h>
#else
#include <direct.h>
#define getcwd _getcwd
#endif

#include "util.h"
#include "log.h"
#include "array.h"
#include "node.h"

#include "json.h"   /* Very nice lib from https://github.com/sheredom/json.h
                     * customized for local use */
#include "node_priv.h"


/* Time out values for connections */


/* The compiled in seed: the index of the das catalog and its mirrors */
static const char* g_lDasDefRoots[2] = {
	"https://das2.org/catalog/index.json",
	"https://raw.githubusercontent.com/das-developers/das-cat/master/cat/index.json"
};
static int g_nDasDefRoots = 2;


const char** das_seed_urls(size_t* pLen){
	if(pLen != NULL)
		*pLen = g_nDasDefRoots;
	return g_lDasDefRoots;
}


static das_node_type_e das_node_type(const char* sType){
	if(sType == NULL) return d2node_inv;
	if(strcmp(sType, D2CV_TYPE_CATALOG) == 0)     return d2node_catalog;
	if(strcmp(sType, D2CV_TYPE_COLLECTION) == 0)  return d2node_collection;
	if(strcmp(sType, D2CV_TYPE_SRCSET) == 0)      return d2node_srcset;   // collection look alike
	if(strcmp(sType, D2CV_TYPE_STREAM) == 0)      return d2node_stream_src;
	if(strcmp(sType, D2CV_TYPE_TIMEAGG) == 0)     return d2node_file_agg;
	if(strcmp(sType, D2CV_TYPE_SPASE) == 0)       return d2node_spase_cat;
	if(strcmp(sType, D2CV_TYPE_SPDF_MASTER) == 0) return d2node_spdf_cat;
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


/* Was public until 2026, nothing outside this file ever called it */
static const DasJdo* _DasNode_getJdoType(
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
	const DasJdo* pObj = _DasNode_getJdoType(pThis, das_json_type_str, sStr);
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
/* Presenting every document in the current layout
 *
 * Past this point the library reads one document model, the current
 * catalog layout.  Each entry in the table below recognizes one older
 * shape by node type and version and rewrites the parsed DOM to match.
 * Elements the parser did not allocate come from a side block, one per
 * node, freed beside the DOM.
 *
 * If a document format other than JSON is ever read (Das2DSDF text, SPASE
 * XML), this table is where it goes: emit JSON text in the current layout
 * and run it through the parser, so nothing downstream learns a second
 * model.  None is written; this only says where. */

/* ---- 2018 Collection shim, begin.  When those catalogs are regenerated
   delete down to the end marker, plus the _DasNode_upgrade() call in
   _DasNode_fromJson() and the pShim field: a table with no entries is
   not C99, so the next translator brings the table back.  The comment
   above stays; it says where. ---- */

static bool _ieq(const char* a, const char* b)
{
	if((a == NULL)||(b == NULL)) return false;
	for(; *a && *b; ++a, ++b)
		if(tolower((unsigned char)*a) != tolower((unsigned char)*b)) return false;
	return *a == *b;
}

/* 2018 Collections, version 0.5 and earlier, describe each source by a
   convention string instead of the media types it provides.  The list
   the convention implies is appended to the entry so readers need only
   one rule.  Delete this entry when those catalogs are regenerated. */

struct provides_patch {       /* "provides": ["mime"], all in one block */
	das_json_dict_el el;
	das_json_str name;
	DasJdo aryVal;
	das_json_ary ary;
	das_json_ary_el aryEl;
	DasJdo strVal;
	das_json_str mime;
};

static const char* _impliedProvides(const DasJdo* pEntry)
{
	if((pEntry == NULL)||(pEntry->type != das_json_type_dict)) return NULL;
	if(!_ieq(DasJdo_string(DasJdo_get(pEntry, D2FRAG_TYPE)), D2CV_TYPE_STREAM)) return NULL;
	if(DasJdo_get(pEntry, "provides") != NULL) return NULL;
	const char* sConv = DasJdo_string(DasJdo_get(pEntry, "convention"));
	if(_ieq(sConv, "das2/2.2")) return "application/vnd.das2.das2stream";
	if(_ieq(sConv, "hapi/2.0")) return "application/vnd.hapi.stream";
	return NULL;
}

/* TODO: Remove this particular upgrade function once no old style collection
   catalog nodes exist in the wild */
static bool _upgradeCollection(DasJdo* pRoot, void** ppShim)
{
	const DasJdo* pSrcs = DasJdo_get(pRoot, D2FRAG_SOURCES);
	if((pSrcs == NULL)||(pSrcs->type != das_json_type_dict)) return true;   /* reported later */

	size_t uNeed = 0;
	const das_json_dict_el* pEl = NULL;
	for(pEl = DasJdo_dictFirst(pSrcs); pEl; pEl = pEl->next)
		if(_impliedProvides(pEl->value) != NULL) ++uNeed;
	if(uNeed == 0) return true;

	struct provides_patch* aPatch = (struct provides_patch*)calloc(
		uNeed, sizeof(struct provides_patch)
	);
	if(aPatch == NULL) return false;
	*ppShim = aPatch;

	size_t u = 0;
	for(pEl = DasJdo_dictFirst(pSrcs); pEl; pEl = pEl->next){
		const char* sMime = _impliedProvides(pEl->value);
		if(sMime == NULL) continue;
		struct provides_patch* p = aPatch + u++;
		p->name.string = "provides";  p->name.string_size = 8;
		p->mime.string = sMime;       p->mime.string_size = strlen(sMime);
		p->strVal.type = das_json_type_str;  p->strVal.value = &p->mime;
		p->aryEl.value = &p->strVal;
		p->ary.start = &p->aryEl;     p->ary.length = 1;
		p->aryVal.type = das_json_type_ary;  p->aryVal.value = &p->ary;
		p->el.name = &p->name;        p->el.value = &p->aryVal;

		das_json_dict* pDict = (das_json_dict*)pEl->value->value;
		if(pDict->start == NULL){
			pDict->start = &p->el;
		}
		else{
			das_json_dict_el* pLast = pDict->start;
			while(pLast->next != NULL) pLast = pLast->next;
			pLast->next = &p->el;
		}
		pDict->length += 1;
	}
	return true;
}

static const struct {
	das_node_type_e nType;
	double rThrough;                           /* versions up to this one */
	bool (*fn)(DasJdo* pRoot, void** ppShim);
} g_upgrades[] = {
	{ d2node_collection, 0.5, _upgradeCollection },
};

/* False only when memory ran out.  An unversioned document counts as the
   oldest, since versions were added after these layouts. */
static bool _DasNode_upgrade(DasJdo* pRoot, das_node_type_e nType, void** ppShim)
{
	const char* sVer = DasJdo_string(DasJdo_get(pRoot, D2FRAG_VERSION));
	double rVer = (sVer == NULL) ? 0.0 : das_strtod_c(sVer, NULL);
	for(size_t u = 0; u < sizeof(g_upgrades)/sizeof(g_upgrades[0]); ++u){
		if((g_upgrades[u].nType != nType)||(rVer > g_upgrades[u].rThrough)) continue;
		if(!g_upgrades[u].fn(pRoot, ppShim)) return false;
	}
	return true;
}
/* ---- 2018 Collection shim, end ---- */

/* Build a node from JSON text.  sUrl is where it came from and sFinalUrl
 * the same after redirects, both for messages.  sPathUri may be NULL.  The
 * node is not marked as owned; constructors decide that. */
DasNode* _DasNode_fromJson(
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

	/* Older layouts are rewritten here so the rest of the node code, and
	   everything built on it, reads only the current one. */
	void* pShim = NULL;
	if(!_DasNode_upgrade(pRoot, nType, &pShim)){
		free(pRoot);
		return NULL;
	}

	/* Initialize non-container types */
	DasNode* pBase = NULL;
	if((nType == d2node_stream_src)||(nType == d2node_file_agg)){
		pBase = (DasNode*)calloc(1, sizeof(DasNode));
		if(pBase == NULL){
			das_error(DASERR_NODE, "Couldn't allocate node");
			free(pRoot); free(pShim);
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
			free(pRoot); free(pShim);
			return NULL;
		}

		DasCatNode* pThis = (DasCatNode*)calloc(1, sizeof(DasCatNode));
		if(pThis == NULL){
			das_error(DASERR_NODE, "Couldn't allocate catalog node");
			free(pRoot); free(pShim);
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
	pBase->pShim = pShim;
	if(sPathUri != NULL) strncpy(pBase->sPath, sPathUri, 511);

	return pBase;
}

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
bool _DasNode_subMatch(DasNode* pBase, const char* sSubPathWithSep)
{
	
	if(!DasNode_isCatalog(pBase)) return false;
	
	DasCatNode* pThis = (DasCatNode*)pBase;
	
	
	const char* sSep = "/";
	const DasJdo* pObj = _DasNode_getJdoType(pBase, das_json_type_str, D2FRAG_PATH_SEP);
	if(pObj != NULL){
		sSep = ((das_json_str*)pObj->value)->string;
	}
	else{
		pObj = _DasNode_getJdoType(pBase, das_json_type_null, D2FRAG_PATH_SEP);
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

DasNode* new_DasNode_json(
	const char* pBytes, size_t uBytes, const char* sUrl, const char* sPathUri
){
	/* No name given means a root: the node has no URI and what is walked
	   to below it is named by the path from it */
	char sUri[600] = {'\0'};
	if((sPathUri != NULL) && (sPathUri[0] != '\0')) _das_uri_expand(sPathUri, sUri, sizeof(sUri));
	if(sUrl == NULL) sUrl = "";

	DasNode* pNode = _DasNode_fromJson(pBytes, uBytes, sUrl, sUrl, sUri);
	if(pNode) pNode->bOwner = true;
	return pNode;
}

/* ************************************************************************* */
/* Selective Destruction */

/* Frees a node and everything below it, except nodes a constructor handed
   to a caller: those are freed only by del_DasNode().  Not public: a child
   lives in its parent's pSubNodes cache, freeing one alone would leave a
   dangling entry there. */
void _DasNode_delTree(DasNode* pThis)
{
	if(pThis == NULL) return;
	if(pThis->bOwner) return;

	/* Cascade through the children */
	if(DasNode_isCatalog(pThis)){
		DasCatNode* pCat = (DasCatNode*)pThis;

		ptrdiff_t uNodes = DasAry_lengthIn(pCat->pSubNodes, DIM0);
		for(ptrdiff_t u = 0; u < uNodes; ++u){
			DasNode** ppSub = (DasNode**)DasAry_getAt(pCat->pSubNodes, vtUnknown, IDX0(u));
			_DasNode_delTree(*ppSub);
		}
		dec_DasAry(pCat->pSubNodes);
		dec_DasAry(pCat->pSubPaths);
	}
	/* Every node holds a JSON DOM, see _DasNode_upgrade(); the shim holds
	   whatever was added to it on load. */
	free(pThis->pDom);
	free(pThis->pShim);

	free(pThis);
}
void del_DasNode(DasNode* pThis)
{
	if(pThis == NULL) return;
	if(!pThis->bOwner) return;   /* a child, its parent frees it */

	pThis->bOwner = false;       /* so the tree pass takes this node too */
	_DasNode_delTree(pThis);
}

/* ************************************************************************* */
/* URI spellings, see node_priv.h */

static const char* g_aUriShort[] = {"site:", "test:"};

const char* _das_uri_expand(const char* sIn, char* sOut, size_t uOut)
{
	if(uOut == 0) return sOut;
	sOut[0] = '\0';
	if(sIn == NULL) return sOut;
	for(size_t u = 0; u < sizeof(g_aUriShort)/sizeof(g_aUriShort[0]); ++u){
		size_t uLen = strlen(g_aUriShort[u]);
		if(strncmp(sIn, g_aUriShort[u], uLen) != 0) continue;
		/* The shorthand alone names the node, "tag:das2.org,2012:site";
		   with more after it, it stands for the node plus its ":/" child
		   separator, as the live catalog composes names top down */
		const char* sRest = sIn + uLen;
		while(*sRest == '/') ++sRest;
		if(*sRest == '\0')
			snprintf(sOut, uOut, D2URI_ROOT "%.*s", (int)(uLen - 1), g_aUriShort[u]);
		else
			snprintf(sOut, uOut, D2URI_ROOT "%s/%s", g_aUriShort[u], sRest);
		return sOut;
	}
	strncpy(sOut, sIn, uOut - 1);
	sOut[uOut - 1] = '\0';
	return sOut;
}

/* ************************************************************************* */
/* The mount table */

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

/* read comments in node_priv.h header on this function first before editing it */
DasNode* _DasNode_newMountTable(const char** aUrl, const char** aUri, size_t uMounts)
{
	if(uMounts == 0){
		daslog_error("A mount table needs at least one mount");
		return NULL;
	}
	size_t uTotal = 0;
	for(size_t i = 0; i < uMounts; ++i){
		if((aUri[i] == NULL)||(aUri[i][0] == '\0')||(aUrl[i] == NULL)||(aUrl[i][0] == '\0')){
			daslog_error("A mount needs both a URL to load and a URI to sit at");
			return NULL;
		}
		uTotal += strlen(aUri[i]) + strlen(aUrl[i]);
	}

	/* The mount table is an ordinary Catalog node that only exists in memory.
	 * Its separator is null, like the das catalog index's, so child names
	 * are whole URI prefixes, and a lookup takes the first child whose name
	 * is a prefix of the path.
	 *
	 * Sized from the inputs since a mount list has no natural bound.  The
	 * escaping below at most doubles a string, hence the factor. */
	size_t uJson = 256 + 2 * uTotal + 64 * uMounts;
	char* sJson = (char*)calloc(uJson, 1);
	if(sJson == NULL){
		das_error(DASERR_NODE, "Couldn't allocate the mount table");
		return NULL;
	}
	size_t u = 0;
	u += snprintf(sJson + u, uJson - u,
		"{\"type\":\"Catalog\",\"version\":\"0.5\",\"name\":\"mounts\","
		"\"title\":\"Mount table\",\"separator\":null,\"catalog\":{"
	);

	/* Longest URI first, see node_priv.h.  A selection sort over an index
	 * array; mount lists are a handful of entries */
	size_t aOrder[64];
	if(uMounts > sizeof(aOrder)/sizeof(aOrder[0])){
		daslog_error("Too many mounts");
		free(sJson);
		return NULL;
	}
	for(size_t i = 0; i < uMounts; ++i) aOrder[i] = i;
	for(size_t i = 0; i + 1 < uMounts; ++i){
		size_t iMax = i;
		for(size_t j = i + 1; j < uMounts; ++j)
			if(strlen(aUri[aOrder[j]]) > strlen(aUri[aOrder[iMax]])) iMax = j;
		size_t t = aOrder[i]; aOrder[i] = aOrder[iMax]; aOrder[iMax] = t;
	}

	for(size_t i = 0; i < uMounts; ++i){
		size_t k = aOrder[i];
		if(i > 0) u += snprintf(sJson + u, uJson - u, ",");
		u += _jsonStr(sJson + u, uJson - u, aUri[k]);
		u += snprintf(sJson + u, uJson - u, ":{\"type\":\"Catalog\",\"urls\":[");
		u += _jsonStr(sJson + u, uJson - u, aUrl[k]);
		u += snprintf(sJson + u, uJson - u, "]}");
	}
	u += snprintf(sJson + u, uJson - u, "}}");
	if(u >= uJson - 1){
		daslog_error("Mount URI or URL too long");
		free(sJson);
		return NULL;
	}

	/* Nothing is fetched here, the first walk through the table does that */
	DasNode* pTable = _DasNode_fromJson(sJson, u, "memory:mounts", "memory:mounts", "");
	free(sJson);
	if(pTable != NULL) pTable->bOwner = true;
	return pTable;
}

/* ************************************************************************* */
/* Walking: resolving a path below a node, one document at a time
 *
 * The walk is the old recursive loader turned into an explicit stack, one
 * frame per path level, with the fetch taken out.  A frame searches one
 * container for the child that starts the rest of the path, first among
 * the children already loaded under it, then in its listing.  A listing
 * entry names one or more URLs; they are the candidates of a fetch step,
 * offered in catalog order.  A document that arrives is parsed and either
 * finishes the walk, becomes the parent of the next frame, or is dropped
 * because nothing under it could continue the path, in which case the
 * next candidate is offered.  When a frame runs out of candidates and
 * children the walk backs out to the frame above, which drops the node it
 * descended into and moves to its next candidate.
 *
 * First match wins at every level, as it always has: for the path "ab/x"
 * a child named "a" is tried before "ab", and the separator check in
 * _DasNode_subMatch() is what sends the walk on to "ab".  A listing entry
 * whose name is already among the loaded children is skipped, since the
 * loaded copy was tried first and a second copy would only duplicate it.
 *
 * Nothing attaches to the tree until the walk succeeds.  Then every node
 * it loaded joins its parent's child list and the walk forgets them.
 *
 * The seed walk, new_DasNodeWalk_seed(), has one frame with no parent
 * whose candidates are the compiled in seed URLs.  The node it returns is
 * a tree top the caller owns, and lookups by full URI are walks from it;
 * a session keeps it for its lifetime, see catalog.c. */

#define _WALK_DEPTH  64    /* more levels than any catalog path has */
#define _WALK_URI_SZ 600

struct walk_frame {
	DasNode* pParent;       /* container searched at this level, NULL for a seed */
	const char* sRest;      /* path below pParent, its separator stripped */
	bool bListing;          /* loaded children exhausted, now in the listing */
	ptrdiff_t iLoaded;      /* next loaded child to look at */
	const das_json_dict_el* pChild;  /* listing entry in hand, NULL before the first */
	const char* sChild;     /* its name, borrowed from the parent's document */
	const DasJdo* pUrls;    /* its urls array, NULL for a seed */
	size_t iUrl;            /* first candidate still on offer */
	size_t uUrls;           /* candidates in all, string entries only */
	DasNode* pNode;         /* loaded for candidate iUrl, not yet attached */
	char sUri[_WALK_URI_SZ]; /* the URI of the node this frame loads */
};

struct das_node_walk {
	char* sPath;            /* owned copy, frames point into it */
	DasNode* pFrom;         /* borrowed, NULL when loading a seed */
	const char** aSeedUrl;
	size_t uSeedUrl;
	struct walk_frame aFrame[_WALK_DEPTH];
	size_t uFrames;
	das_walk_state_e state;
	DasNode* pResult;
	char sNear[_WALK_URI_SZ];  /* deepest container searched, "" if none */
	const char* sNearRest;
	size_t uDeepest;
};

static void _DasNode_attach(DasNode* pParent, DasNode* pChild, const char* sName)
{
	DasCatNode* pCat = (DasCatNode*)pParent;
	DasAry_append(pCat->pSubNodes, (const ubyte*)&pChild, 1);
	DasAry_append(pCat->pSubPaths, (const ubyte*)sName, strlen(sName) + 1);
	DasAry_markEnd(pCat->pSubPaths, DIM1);
}

/* The name a loaded child was attached under, "" for a bad entry */
static const char* _DasCatNode_loadedName(const DasCatNode* pCat, ptrdiff_t i, size_t* pLen)
{
	size_t uLen = 0;
	const char* s = (const char*)DasAry_getBytesIn(pCat->pSubPaths, DIM1_AT(i), &uLen);
	if((s == NULL)||(uLen == 0)){ *pLen = 0; return ""; }
	*pLen = uLen - 1;   /* the stored NUL is not part of the name */
	return s;
}

static bool _DasCatNode_hasLoaded(const DasCatNode* pCat, const char* sName)
{
	ptrdiff_t n = DasAry_lengthIn(pCat->pSubNodes, DIM0);
	for(ptrdiff_t i = 0; i < n; ++i){
		size_t uLen = 0;
		const char* s = _DasCatNode_loadedName(pCat, i, &uLen);
		if((uLen == strlen(sName)) && (strncmp(s, sName, uLen) == 0)) return true;
	}
	return false;
}

/* Candidate number n of a frame, counting string entries only */
static const char* _walk_url(const DasNodeWalk* pThis, const struct walk_frame* pF, size_t n)
{
	if(pF->pUrls == NULL) return (n < pThis->uSeedUrl) ? pThis->aSeedUrl[n] : NULL;
	size_t k = 0;
	for(const das_json_ary_el* pEl = DasJdo_aryFirst(pF->pUrls); pEl != NULL; pEl = pEl->next){
		const char* s = DasJdo_string(pEl->value);
		if(s == NULL) continue;
		if(k++ == n) return s;
	}
	return NULL;
}

static size_t _walk_countUrls(const DasNode* pParent, const char* sChild, const DasJdo* pUrls)
{
	size_t u = 0;
	int i = 0;
	for(const das_json_ary_el* pEl = DasJdo_aryFirst(pUrls); pEl != NULL; pEl = pEl->next, ++i){
		if(DasJdo_string(pEl->value) != NULL) ++u;
		else daslog_error_v("From %s: %s/%s/%d element is not a string",
		                    pParent->sURL, D2FRAG_URLS, sChild, i);
	}
	return u;
}

/* Remember the deepest container on the path, for DasNodeWalk_nearest().
 * On the path means the rest of the path continues below it with its
 * separator, the same test _DasNode_subMatch() starts with; a look-alike
 * such as "a" for "ab/x" is not on the path.  Whether anything under it
 * matches does not matter: "uiowa" is the nearest node for
 * "uiowa/nope/deeper" even though it lists no "nope". */
static void _walk_nearSet(DasNodeWalk* pThis, size_t uDepth, const DasNode* pNode, const char* sRest)
{
	if(uDepth <= pThis->uDeepest) return;
	pThis->uDeepest = uDepth;
	strncpy(pThis->sNear, pNode->sPath, sizeof(pThis->sNear) - 1);
	pThis->sNearRest = sRest;
}

/* For a child met on the way: it is on the path only if the rest continues
 * below it with its separator */
static void _walk_near(DasNodeWalk* pThis, size_t uDepth, const DasNode* pNode, const char* sRem)
{
	const char* sSep = ((const DasCatNode*)pNode)->sPathSep;
	size_t uSep = strlen(sSep);
	if(uSep > 0){
		if(strncmp(sRem, sSep, uSep) != 0) return;
		sRem += uSep;
	}
	_walk_nearSet(pThis, uDepth, pNode, sRem);
}

static bool _walk_push(DasNodeWalk* pThis, DasNode* pParent, const char* sRest)
{
	if(pThis->uFrames == _WALK_DEPTH){
		daslog_error_v("Catalog path %s is more than %d levels deep", pThis->sPath, _WALK_DEPTH);
		return false;
	}
	struct walk_frame* pF = pThis->aFrame + pThis->uFrames;
	memset(pF, 0, sizeof(*pF));
	pF->pParent = pParent;

	/* The rest may start with the separator this container uses, "/uiowa"
	   under site: or not, "uiowa"; both name the same child */
	const char* sSep = ((DasCatNode*)pParent)->sPathSep;
	size_t uSep = strlen(sSep);
	if((uSep > 0) && (strncmp(sRest, sSep, uSep) == 0)) sRest += uSep;
	pF->sRest = sRest;
	pThis->uFrames += 1;
	_walk_nearSet(pThis, pThis->uFrames, pParent, sRest);   /* already stripped */
	return true;
}

/* Back out one level.  The frame above drops the node it descended into
   and moves to its next candidate; a frame that descended into an already
   loaded child has nothing to drop. */
static void _walk_pop(DasNodeWalk* pThis)
{
	struct walk_frame* pF = pThis->aFrame + (pThis->uFrames - 1);
	if(pF->pNode != NULL){ _DasNode_delTree(pF->pNode); pF->pNode = NULL; }
	pThis->uFrames -= 1;
	if(pThis->uFrames == 0) return;
	pF = pThis->aFrame + (pThis->uFrames - 1);
	if(pF->pNode != NULL){
		_DasNode_delTree(pF->pNode);
		pF->pNode = NULL;
		pF->iUrl += 1;
	}
}

static void _walk_finish(DasNodeWalk* pThis, DasNode* pResult)
{
	for(size_t k = 0; k < pThis->uFrames; ++k){
		struct walk_frame* pF = pThis->aFrame + k;
		if(pF->pNode == NULL) continue;
		if(pF->pParent != NULL) _DasNode_attach(pF->pParent, pF->pNode, pF->sChild);
		pF->pNode = NULL;
	}
	if(pThis->pFrom == NULL) pResult->bOwner = true;   /* a seed, the caller's */
	pThis->pResult = pResult;
	pThis->state = das_walk_done;
}

/* Move to the next state: a fetch step, done, or failed */
static void _walk_advance(DasNodeWalk* pThis)
{
	while(pThis->uFrames > 0){
		struct walk_frame* pF = pThis->aFrame + (pThis->uFrames - 1);

		/* Candidates still on offer for the child in hand */
		if((pF->pNode == NULL) && pF->bListing && (pF->iUrl < pF->uUrls)){
			pThis->state = das_walk_fetch;
			return;
		}
		if(pF->pParent == NULL){ _walk_pop(pThis); continue; }   /* every seed URL tried */

		DasCatNode* pCat = (DasCatNode*)pF->pParent;

		/* Children already loaded, so a walked path costs no document */
		if(!pF->bListing){
			ptrdiff_t n = DasAry_lengthIn(pCat->pSubNodes, DIM0);
			bool bPushed = false;
			while(pF->iLoaded < n){
				ptrdiff_t i = pF->iLoaded++;
				size_t uLen = 0;
				const char* sName = _DasCatNode_loadedName(pCat, i, &uLen);
				if(uLen == 0){
					daslog_warn_v("Node at %s has a zero-length sub-path", pF->pParent->sURL);
					continue;
				}
				if(strncmp(sName, pF->sRest, uLen) != 0) continue;
				DasNode* pKid = *((DasNode**)DasAry_getAt(pCat->pSubNodes, vtUnknown, IDX0(i)));
				const char* sRem = pF->sRest + uLen;
				if(sRem[0] == '\0'){ _walk_finish(pThis, pKid); return; }
				if(DasNode_isCatalog(pKid)) _walk_near(pThis, pThis->uFrames + 1, pKid, sRem);
				if(!_DasNode_subMatch(pKid, sRem)) continue;
				if(_walk_push(pThis, pKid, sRem)){ bPushed = true; break; }
			}
			if(bPushed) continue;
			pF->bListing = true;
			pF->pChild = NULL;
		}

		/* The listing: the next child whose name starts the rest */
		const DasJdo* pDir = (const DasJdo*)pCat->pContainer;
		const das_json_dict_el* pEl = NULL;
		if((pDir != NULL) && (pDir->type == das_json_type_dict))
			pEl = (pF->pChild == NULL) ? DasJdo_dictFirst(pDir) : pF->pChild->next;
		else
			daslog_error_v("Catalog container node missing for node from %s", pF->pParent->sURL);

		bool bOffer = false;
		for(; pEl != NULL; pEl = pEl->next){
			const char* sChild = pEl->name->string;
			size_t uLen = strlen(sChild);
			if(strncmp(sChild, pF->sRest, uLen) != 0) continue;
			if(_DasCatNode_hasLoaded(pCat, sChild)) continue;

			/* Broken syntax: stop using this listing, the level above may
			   have another copy */
			const DasJdo* pStub = pEl->value;
			if((pStub == NULL)||(pStub->type != das_json_type_dict)){
				daslog_error_v("Catalog error @ %s: sub item '%s' is not a dictionary",
				               pF->pParent->sURL, sChild);
				break;
			}
			const DasJdo* pUrls = DasJdo_get(pStub, D2FRAG_URLS);
			if((pUrls == NULL)||(pUrls->type != das_json_type_ary)){
				daslog_error_v("From %s: %s element of node '%s' doesn't have a %s array",
				               pF->pParent->sURL, pCat->sContainer, sChild, D2FRAG_URLS);
				break;
			}
			pF->pChild = pEl;
			pF->sChild = sChild;
			pF->pUrls = pUrls;
			pF->iUrl = 0;
			pF->uUrls = _walk_countUrls(pF->pParent, sChild, pUrls);
			/* A root has no URI, so its children are named by bare name and
			   the levels below by their path from the root */
			if(pF->pParent->sPath[0] == '\0')
				snprintf(pF->sUri, sizeof(pF->sUri), "%s", sChild);
			else
				snprintf(pF->sUri, sizeof(pF->sUri), "%s%s%s", pF->pParent->sPath, pCat->sPathSep, sChild);
			if(pF->uUrls == 0) continue;
			bOffer = true;
			break;
		}
		if(bOffer){ pThis->state = das_walk_fetch; return; }

		/* A miss at one level is normal while the walk backs out and tries
		   other candidates or children, so it is not an error here.  The
		   driver knows whether the whole path failed and has the user's
		   wording of it. */
		const char* sLabel = DasNode_label(pF->pParent);
		daslog_info_v("Node %s (URI '%s') has no child node that starts with %s",
		              sLabel ? sLabel : "", pF->pParent->sPath, pF->sRest);
		_walk_pop(pThis);
	}
	pThis->state = das_walk_failed;
}

static DasNodeWalk* _walk_alloc(DasNode* pFrom, const char* sRelPath)
{
	DasNodeWalk* pThis = (DasNodeWalk*)calloc(1, sizeof(DasNodeWalk));
	if(pThis == NULL){
		das_error(DASERR_NODE, "Couldn't allocate a catalog walk");
		return NULL;
	}
	pThis->pFrom = pFrom;

	/* An owned copy even when empty, das_strdup() has nothing to give for "" */
	size_t uPath = (sRelPath != NULL) ? strlen(sRelPath) : 0;
	pThis->sPath = (char*)calloc(uPath + 1, 1);
	if(pThis->sPath == NULL){
		das_error(DASERR_NODE, "Couldn't allocate a catalog walk");
		free(pThis);
		return NULL;
	}
	if(uPath > 0) memcpy(pThis->sPath, sRelPath, uPath);
	return pThis;
}

DasNodeWalk* new_DasNodeWalk(DasNode* pFrom, const char* sRelPath)
{
	if(pFrom == NULL){
		daslog_error("A walk needs a node to start from; for the compiled in seed "
		             "see new_DasNodeWalk_seed()");
		return NULL;
	}
	if(!DasNode_isCatalog(pFrom)){
		daslog_error_v("Node %s from %s is a terminating node",
		               DasNode_label(pFrom), DasNode_srcUrl(pFrom));
		return NULL;
	}
	DasNodeWalk* pThis = _walk_alloc(pFrom, sRelPath);
	if(pThis == NULL) return NULL;

	if(pThis->sPath[0] == '\0'){
		pThis->pResult = pFrom;
		pThis->state = das_walk_done;
		return pThis;
	}
	_walk_push(pThis, pFrom, pThis->sPath);   /* cannot be too deep at 0 */
	_walk_advance(pThis);
	return pThis;
}

DasNodeWalk* new_DasNodeWalk_seed(void)
{
	DasNodeWalk* pThis = _walk_alloc(NULL, NULL);
	if(pThis == NULL) return NULL;
	pThis->aSeedUrl = das_seed_urls(&pThis->uSeedUrl);

	/* One frame with no parent: its candidates are the seed URLs and the
	   document that arrives is the result */
	struct walk_frame* pF = pThis->aFrame;
	pF->sRest = pThis->sPath;
	pF->bListing = true;
	pF->uUrls = pThis->uSeedUrl;
	pThis->uFrames = 1;
	_walk_advance(pThis);
	return pThis;
}

void del_DasNodeWalk(DasNodeWalk* pThis)
{
	if(pThis == NULL) return;
	/* Frames hold unattached nodes only while the walk is under way; each
	   is a separate tree since the next frame's node is not attached to
	   it, so order does not matter */
	for(size_t k = 0; k < pThis->uFrames; ++k)
		if(pThis->aFrame[k].pNode != NULL) _DasNode_delTree(pThis->aFrame[k].pNode);
	free(pThis->sPath);
	free(pThis);
}

das_walk_state_e DasNodeWalk_state(const DasNodeWalk* pThis){ return pThis->state; }

size_t DasNodeWalk_urlCount(const DasNodeWalk* pThis)
{
	if(pThis->state != das_walk_fetch) return 0;
	const struct walk_frame* pF = pThis->aFrame + (pThis->uFrames - 1);
	return pF->uUrls - pF->iUrl;
}

const char* DasNodeWalk_url(const DasNodeWalk* pThis, size_t uIdx)
{
	if(uIdx >= DasNodeWalk_urlCount(pThis)) return NULL;
	const struct walk_frame* pF = pThis->aFrame + (pThis->uFrames - 1);
	return _walk_url(pThis, pF, pF->iUrl + uIdx);
}

const char* DasNodeWalk_uri(const DasNodeWalk* pThis)
{
	if(pThis->state != das_walk_fetch) return "";
	return pThis->aFrame[pThis->uFrames - 1].sUri;
}

DasErrCode DasNodeWalk_feed(
	DasNodeWalk* pThis, size_t uIdx, const char* pBytes, size_t uBytes,
	const char* sFinalUrl
){
	/* Misuse by the driver, logged rather than raised: a host that mixes up
	   its candidates should not take the program down with it */
	if(pThis->state != das_walk_fetch){
		daslog_error("The walk is not waiting for a document");
		return DASERR_NODE;
	}
	size_t uOffer = DasNodeWalk_urlCount(pThis);
	if(uIdx >= uOffer){
		daslog_error_v("Candidate %zu is past the %zu on offer", uIdx, uOffer);
		return DASERR_NODE;
	}

	struct walk_frame* pF = pThis->aFrame + (pThis->uFrames - 1);
	const char* sUrl = _walk_url(pThis, pF, pF->iUrl + uIdx);
	if(sFinalUrl == NULL) sFinalUrl = sUrl;

	DasNode* pNode = _DasNode_fromJson(pBytes, uBytes, sUrl, sFinalUrl, pF->sUri);
	if((pNode != NULL) && (pF->pParent == NULL) && (pNode->nType != d2node_catalog)){
		daslog_info_v("%s is not a Catalog node, skipping it as a seed", sUrl);
		_DasNode_delTree(pNode);
		pNode = NULL;
	}
	if(pNode == NULL){
		pF->iUrl += uIdx + 1;
		_walk_advance(pThis);
		return DASERR_NODE;
	}
	pF->iUrl += uIdx;   /* the one in hand; a back out moves past it */
	pF->pNode = pNode;

	const char* sRem = (pF->pParent == NULL) ? pF->sRest : pF->sRest + strlen(pF->sChild);
	if(sRem[0] == '\0'){ _walk_finish(pThis, pNode); return DAS_OKAY; }

	if(DasNode_isCatalog(pNode)) _walk_near(pThis, pThis->uFrames + 1, pNode, sRem);
	if(DasNode_isCatalog(pNode) && _DasNode_subMatch(pNode, sRem) && _walk_push(pThis, pNode, sRem)){
		_walk_advance(pThis);
		return DAS_OKAY;
	}

	/* A good document that leads nowhere: a look-alike child, or a mirror
	   whose listing is behind.  Not the driver's problem. */
	daslog_debug_v("Nothing under %s continues the path %s", sUrl, sRem);
	_DasNode_delTree(pNode);
	pF->pNode = NULL;
	pF->iUrl += 1;
	_walk_advance(pThis);
	return DAS_OKAY;
}

void DasNodeWalk_fail(DasNodeWalk* pThis, size_t uIdx, const char* sWhy)
{
	if(uIdx >= DasNodeWalk_urlCount(pThis)) return;
	struct walk_frame* pF = pThis->aFrame + (pThis->uFrames - 1);
	daslog_debug_v("%s: %s", _walk_url(pThis, pF, pF->iUrl + uIdx), sWhy ? sWhy : "not available");
	pF->iUrl += uIdx + 1;
	_walk_advance(pThis);
}

DasNode* DasNodeWalk_result(const DasNodeWalk* pThis)
{
	return (pThis->state == das_walk_done) ? pThis->pResult : NULL;
}

const char* DasNodeWalk_nearest(const DasNodeWalk* pThis, const char** psRest)
{
	if(psRest != NULL) *psRest = NULL;
	if((pThis->state != das_walk_failed)||(pThis->uDeepest == 0)) return NULL;
	if(psRest != NULL) *psRest = pThis->sNearRest;
	return pThis->sNear;
}
