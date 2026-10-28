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
	/* Every node has a URI; one built from bytes with no name is named by
	   where the bytes came from, or by a stand in when even that is unknown */
	char sUri[600];
	if(sUrl == NULL) sUrl = "memory:node";
	if((sPathUri == NULL)||(sPathUri[0] == '\0')) _das_uri_local(sUrl, sUri, sizeof(sUri));
	else _das_uri_expand(sPathUri, sUri, sizeof(sUri));

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

static const char* g_aUriShort[] = {"site:", "test:", "local:"};

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

const char* _das_uri_local(const char* sLoc, char* sOut, size_t uOut)
{
	const char* sTail = sLoc;
	const char* sLead = "";
	if(strncmp(sLoc, "file://", 7) == 0) sTail += 7;
	else if(strncmp(sLoc, "http://", 7) == 0){ sTail += 7; sLead = "/"; }
	else if(strncmp(sLoc, "https://", 8) == 0){ sTail += 8; sLead = "/"; }

	/* A bare relative path: the working directory is part of the name,
	 * since "./wav.json" means a different file from every other directory.
	 * One leading "./" is dropped as pure noise. */
	bool bDrive = isalpha((unsigned char)sTail[0]) && (sTail[1] == ':');
	if((sTail == sLoc) && (sTail[0] != '/') && (sTail[0] != '\\') && !bDrive){
		if((sTail[0] == '.') && ((sTail[1] == '/')||(sTail[1] == '\\'))) sTail += 2;
		char sCwd[512] = {'\0'};
		if(getcwd(sCwd, sizeof(sCwd) - 1) == NULL) sCwd[0] = '\0';
		snprintf(sOut, uOut, D2URI_LOCAL "%s%s%s", (sCwd[0] == '/') ? "" : "/", sCwd, DAS_DSEPS);
		size_t u = strlen(sOut);
		if(u < uOut) snprintf(sOut + u, uOut - u, "%s", sTail);
		return sOut;
	}
	snprintf(sOut, uOut, D2URI_LOCAL "%s%s", (bDrive ? "/" : sLead), sTail);
	return sOut;
}

bool _das_uri_isLocal(const char* sUri)
{
	return (sUri != NULL) && (strncmp(sUri, D2URI_LOCAL, sizeof(D2URI_LOCAL) - 1) == 0);
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

/* read comments in node_priv.h header on this function first before edititing it */
DasNode* _DasNode_newMountRoot(
	const char** aUrl, const char** aUri, size_t uMounts,
	bool bFallThrough, DasCredMngr* pMgr, const char* sAgent,
	const DasNodeCache* pCache
){
	if(uMounts == 0){
		daslog_error("A mount root needs at least one mount");
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

	/* The mount root is an ordinary Catalog node that only exists in memory.
	 * Its separator is null, like the federated root's, so child names are
	 * whole URI prefixes.  The mounts come first, then, if asked for, a child
	 * with the empty name whose URLs are the federated roots.  The empty name
	 * is a prefix of every path, so anything no mount claims falls through
	 * to the normal catalog, while everything under a mount URI is shadowed.
	 * Without that child the mounts are all there is.
	 *
	 * Sized from the inputs since a mount list has no natural bound.  The
	 * escaping below at most doubles a string, hence the factor. */
	size_t uJson = 256 + 2 * uTotal + 64 * uMounts;
	for(int i = 0; i < g_nDasDefRoots; ++i) uJson += 2 * strlen(g_lDasDefRoots[i]) + 4;
	char* sJson = (char*)calloc(uJson, 1);
	if(sJson == NULL){
		das_error(DASERR_NODE, "Couldn't allocate mount root");
		return NULL;
	}
	size_t u = 0;
	u += snprintf(sJson + u, uJson - u,
		"{\"type\":\"Catalog\",\"version\":\"0.5\",\"name\":\"mounts\","
		"\"title\":\"Mount root\",\"separator\":null,\"catalog\":{"
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
	if(bFallThrough){
		u += snprintf(sJson + u, uJson - u, ",\"\":{\"type\":\"Catalog\",\"urls\":[");
		for(int i = 0; i < g_nDasDefRoots; ++i){
			if(i > 0) u += snprintf(sJson + u, uJson - u, ",");
			u += _jsonStr(sJson + u, uJson - u, g_lDasDefRoots[i]);
		}
		u += snprintf(sJson + u, uJson - u, "]}");
	}
	u += snprintf(sJson + u, uJson - u, "}}");
	if(u >= uJson - 1){
		daslog_error("Mount URI or URL too long");
		free(sJson);
		return NULL;
	}

	/* Nothing is fetched here, the first subNode call does that and takes
	   the manager, agent and cache from the root */
	DasNode* pRoot = _DasNode_fromJson(sJson, u, "memory:mounts", "memory:mounts", "");
	free(sJson);
	if(pRoot != NULL){
		pRoot->bOwner = true;
		pRoot->pMgr = pMgr;
		pRoot->sAgent = sAgent;
		pRoot->pCache = pCache;
	}
	return pRoot;
}

