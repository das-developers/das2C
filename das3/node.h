/* Copyright (C) 2018 Chris Piker <chris-piker@uiowa.edu>
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

#ifndef _das_node_h_
#define _das_node_h_

#include <das3/credentials.h>
#include <das3/array.h>
#include <das3/json.h>

#ifdef __cplusplus
extern "C" {
#endif



/** @addtogroup catalog
 *
 * @par Nodes without a session
 *
 * A DasNode is one parsed catalog document: its type, URI, source URL,
 * and the document itself, with its children listed by name and type and
 * the URLs each would load from.  Nothing in this header fetches.  A node
 * is built from bytes the caller obtained, see new_DasNode_json(), and a
 * path below it is resolved by a DasNodeWalk, which says which document
 * it needs next and takes what the caller hands back.  Programs on a real
 * machine do not drive the walk themselves: a DasCat session, see
 * catalog.h, reads files, keeps a cache, makes the HTTP requests and
 * loops the walk until it stops.  A browser build drives it from
 * JavaScript with fetch().  The compiled in seed, the index of the das2
 * catalog, is loaded through the same walk interface by
 * new_DasNodeWalk_seed().
 *
 * Ownership follows general das2C rules: the node a constructor hands
 * you is yours, and del_DasNode() on it frees the whole tree loaded below
 * it.  Children a walk attaches are managed by their parent and go with
 * it; del_DasNode() ignores them.
 *
 * Catalog nodes do not know their names.  Like files on a disk, a
 * node's URI comes from the entries above it, up to the root, which has
 * none.  So a node built from bytes is named by the caller, or not at
 * all: a seed given no prefix is a root, and the nodes below it are named
 * by their path from it.
 *
 * Driving a walk by hand, which is what a browser build does:
 *
 * @code
 DasNode* pSite = new_DasNode_json(sBytes, uLen, sUrl, "tag:das2.org,2012:site:/uiowa");

 DasNodeWalk* pWalk = new_DasNodeWalk(pSite, "juno/wav/survey");
 while(DasNodeWalk_state(pWalk) == das_walk_fetch){
    const char* sNext = DasNodeWalk_url(pWalk, 0);
    if(my_fetch(sNext, &sDoc, &uDoc))
       DasNodeWalk_feed(pWalk, 0, sDoc, uDoc, NULL);
    else
       DasNodeWalk_fail(pWalk, 0, "no answer");
 }
 DasNode* pSurvey = DasNodeWalk_result(pWalk);   // NULL if the walk failed
 del_DasNodeWalk(pWalk);                         // pSurvey lives on under pSite
 @endcode
 */

/* Defines for common path_uri's */
#define D2URI_ROOT                   "tag:das2.org,2012:"
#define D2URI_SITES      D2URI_ROOT  "site"
#define D2URI_SITE_UIOWA D2URI_SITES ":/uiowa"

/* Defines for common document fragments */
#define D2FRAG_TYPE       "type"
#define D2FRAG_VERSION    "version"
#define D2FRAG_NAME       "name"
#define D2FRAG_LABEL      "label"
#define D2FRAG_TITLE      "title"
#define D2FRAG_DESC       "description"
#define D2FRAG_SUB_PATHS  "catalog"
#define D2FRAG_PATH_SEP   "separator"
#define D2FRAG_SOURCES    "sources"
#define D2FRAG_URLS       "urls"
	
/* Defines for common document string values */
#define D2CV_TYPE_CATALOG     "Catalog"
#define D2CV_TYPE_COLLECTION  "Collection"
#define D2CV_TYPE_SRCSET      "SourceSet"
#define D2CV_TYPE_STREAM      "HttpStreamSrc"
#define D2CV_TYPE_TIMEAGG     "FileTimeAgg"
#define D2CV_TYPE_SPASE       "SpaseRecord"
#define D2CV_TYPE_SPDF_MASTER "SpdfMasterCat"

/** Catalog node type */
typedef enum das_node_type_enum {
	d2node_inv = 0, d2node_catalog = 1, d2node_collection = 2, 
	d2node_srcset = 3, d2node_stream_src = 4,  d2node_file_agg = 5, 
   d2node_spdf_cat = 6, d2node_spase_cat = 7
} das_node_type_e;

/** @addtogroup catalog 
 * @{
 */

/** A das catalog node, opaque.
 *
 * Nodes are made by new_DasNode_json() and by a DasNodeWalk, read
 * through the accessors below, and freed by del_DasNode() on a tree top,
 * which takes the nodes walked to below it along.
 */
typedef struct das_node DasNode;

/** @} */

/** The compiled in seed URLs: the index of the das catalog and its mirrors
 *
 * The seed is a root, a node with no name, whose children are the top
 * level catalogs, each named by a whole URI prefix such as
 * "tag:das2.org,2012:".  new_DasNodeWalk_seed() loads it from whichever
 * of these answers, in order.
 *
 * @param  pLen Receives the number of URLs.  May be NULL.
 * @return An array of NUL terminated strings, static.
 */
DAS_API const char** das_seed_urls(size_t* pLen);

/** Build a node from the JSON text of a node document
 *
 * For programs that fetch documents themselves, a browser build in
 * particular.  Nothing is fetched and no file is read.  The result is a
 * tree top the caller owns, with no parent in memory; a container built
 * this way lists its children, see DasNode_subCount(), and names the
 * URLs each would load from, see DasNode_subStub().
 *
 * @param pBytes The document text, need not be NUL terminated.
 * @param uBytes The number of bytes in pBytes.
 * @param sUrl Where the text came from, recorded as the node's source
 *             URL, see DasNode_srcUrl().  May be NULL.
 * @param sPathUri The catalog URI of the node, shorthands expanded.  NULL
 *             or empty makes it a root: no URI, and the nodes walked to
 *             below it are named by their path from it.
 * @return A new node, to be freed with del_DasNode(), or NULL after
 *             logging a parse problem or an unrecognized node type.
 * @memberof DasNode
 */
DAS_API DasNode* new_DasNode_json(
	const char* pBytes, size_t uBytes, const char* sUrl, const char* sPathUri
);

/** Returns true this node can contain sub nodes.
 * @memberof DasNode
 */
DAS_API bool DasNode_isCatalog(const DasNode* pNode);

/** Determine if this node defines a das2 stream source
 * @memberof DasNode
 */
DAS_API bool DasNode_isStreamSrc(const DasNode* pNode);

/** Determine if this node is a SPASE record 
 * @memberof DasNode 
 */
DAS_API bool DasNode_isSpaseRec(const DasNode* pNode);

/** Determine if this node is an SPDF catalog 
 * @memberof DasNode
 */
DAS_API bool DasNode_isSpdfCat(const DasNode* pNode);

/** Get the path URI for this catalog node
 *
 * Names are composed walking down: a seed given a prefix hands it to its
 * children with its separator between, and so on.  A seed given no
 * prefix is a root, and a root is the one node with no URI; the compiled
 * in das catalog index is the usual example.
 *
 * @param pThis the catalog node
 * @return The URI, or NULL for a root
 * @memberof DasNode
 */
DAS_API const char* DasNode_pathUri(const DasNode* pThis);

/** Get the location from which this catalog node was read
 * @param pThis the catalog node
 * @return The source URL, or "" for a node built from bytes with no
 *         location given, see new_DasNode_json().  Never NULL.
 * @memberof DasNode
 */
DAS_API const char* DasNode_srcUrl(const DasNode* pThis);

/** Get the type of node
 *
 * A more specific question than the 'is' functions above, which are
 * usually the better choice so that application code works in a duck
 * typing manner.
 * @param pThis the catalog node
 * @return The node type, see das_node_type_e
 * @memberof DasNode
 */
DAS_API das_node_type_e DasNode_type(const DasNode* pThis);

/** Get the node name, the catalog's "name" field
 * 
 * @param pThis the catalog node 
 * @return A short string suitable for labeling this node in a GUI, or NULL
 *         
 * @memberof DasNode
 */
DAS_API const char* DasNode_name(const DasNode* pThis);

/** Get the node title, a one line description from the catalog's "title"
 * field, if provided
 * @returns the title or NULL
 * @memberof DasNode
 */
DAS_API const char* DasNode_title(const DasNode* pThis);

/** Get the node's longer description, the catalog's "description" field,
 * if provided
 * @returns the description or NULL
 * @memberof DasNode
 */
DAS_API const char* DasNode_summary(const DasNode* pThis);

/** Get the node's "version" field, if provided
 *
 * This is the version of the node *definition* (ex: "0.5" for catalogs,
 * "0.6" or "0.7" for HttpStreamSrc) not a data version.  Clients need it
 * because the interface layout of source nodes changed between 0.6 and 0.7.
 * @returns the version string or NULL
 * @memberof DasNode
 */
DAS_API const char* DasNode_version(const DasNode* pThis);

/** Get a short label for the node, the "label" field if present, else "name"
 * @returns the label, or NULL if the node has neither field
 * @memberof DasNode
 */
DAS_API const char* DasNode_label(const DasNode* pThis);

/** Get the separator a container node puts between its path URI and
 * the name of a child.
 *
 * @returns "/" by default, the empty string if the node's "separator" is
 *          null, or NULL if this is not a container node.
 * @memberof DasNode
 */
DAS_API const char* DasNode_pathSep(const DasNode* pThis);

/** Number of children listed by a container node.
 *
 * This inspects the listing only, nothing is downloaded.  Children of a
 * container are known by name and type before they are loaded, just like
 * directory entries.
 *
 * @returns the number of listed children, 0 for non-container nodes
 * @memberof DasNode
 */
DAS_API size_t DasNode_subCount(const DasNode* pThis);

/** Get the name of a listed child by index.  No network activity.
 * @param uIdx 0 to DasNode_subCount() - 1
 * @returns the child's name, or NULL if the index is out of range
 * @memberof DasNode
 */
DAS_API const char* DasNode_subName(const DasNode* pThis, size_t uIdx);

/** Get the listing entry (stub) for a child by index.  No network activity.
 *
 * The stub is the dictionary the container holds for the child.  It always
 * has "type" and "urls", and often "label", "title" or "description".  It
 * is not the child node.  A walk, or a session's DasCat_subNode(), loads that.
 *
 * @returns the stub dictionary, or NULL if the index is out of range
 * @memberof DasNode
 */
DAS_API const DasJdo* DasNode_subStub(const DasNode* pThis, size_t uIdx);

/** Get the node type of a listed child by index.  No network activity.
 * @returns the type, d2node_inv if the type string is unknown to this
 *          library (ex: "Das2DSDF", "WebSocSrc") or the index is bad
 * @memberof DasNode
 */
DAS_API das_node_type_e DasNode_subType(const DasNode* pThis, size_t uIdx);


/** If true the internal data for this node are in JSON format
 *
 * Always true today; the hook is for catalogs in another format.
 * @param pThis The catalog node
 * @return True if DasNode_getJdo() is usable
 * @memberof DasNode
 */
DAS_API bool DasNode_isJson(const DasNode* pThis);

/** Get a JSON document object at a fragment location in a node.
 * @param pThis The catalog node
 * @param sFragment The fragment path, for example "contacts/0/email", or
 *        NULL for the whole document
 * @return The json object at that location, or NULL if the node does not
 *         contain the given element.
 * @memberof DasNode
 */
DAS_API const DasJdo* DasNode_getJdo(const DasNode* pThis, const char* sFragment);



/* ************************************************************************* */
/* Walking */

/** @addtogroup catalog
 * @{
 */

/** A walk from a node to a descendant, opaque.
 *
 * The catalog's path resolution with the transport taken out.  A walk
 * knows which document it needs next and what to make of the one it is
 * handed; whoever drives it reads files or makes HTTP requests.
 *
 * Each step offers the candidate URLs for one node, the mirrors a catalog
 * lists for a child, in catalog order.  A sequential driver takes index
 * 0, and after a DasNodeWalk_fail() or a document that does not parse the
 * list shrinks, so index 0 is always the next thing to try.  A driver that
 * fetches mirrors in parallel feeds whichever answered.  A document that
 * parses but leads nowhere, a look-alike child or a mirror out of date,
 * is dropped silently and the next candidate offered.  When a listing is
 * exhausted the walk backs out a level and tries the level above's next
 * candidate, so a dead mirror doesn't hide a live one.
 *
 * Nothing joins the tree until the walk succeeds; then the nodes it
 * loaded are attached under their parents and a second walk of the same
 * path needs no document at all.  A walk that fails or is deleted early
 * frees what it loaded.
 *
 * The driver's loop, for a program that fetches for itself:
 *
 * @code
 DasNodeWalk* pWalk = new_DasNodeWalk(pSite, "juno/wav/survey");
 if(pWalk == NULL) handle_error();

 while(DasNodeWalk_state(pWalk) == das_walk_fetch){

    // Index 0 is always the next candidate to try, see below
    const char* sUrl = DasNodeWalk_url(pWalk, 0);

    char* pDoc = NULL;
    size_t uDoc = 0;
    if(my_fetch(sUrl, &pDoc, &uDoc)){
       // DASERR_NODE here means the bytes were not a node; the walk has
       // already moved on, so there is nothing to do but note it
       if(DasNodeWalk_feed(pWalk, 0, pDoc, uDoc, NULL) != DAS_OKAY)
          log("%s did not parse", sUrl);
       free(pDoc);
    }
    else{
       DasNodeWalk_fail(pWalk, 0, "no answer");
    }
 }

 DasNode* pSurvey = DasNodeWalk_result(pWalk);
 if(pSurvey == NULL){
    const char* sRest = NULL;
    const char* sNear = DasNodeWalk_nearest(pWalk, &sRest);
    log("nothing at %s below %s", sRest, sNear);
 }
 del_DasNodeWalk(pWalk);   // pSurvey lives on under pSite
 @endcode
 *
 * The same loop loads the compiled in seed, new_DasNodeWalk_seed(): the
 * candidates are then the seed URLs, see das_seed_urls(), and the result
 * is the root of the das catalog, a seed the caller owns and frees with
 * del_DasNode().  A lookup by full URI is a walk from it, and what it
 * loads stays under it, so a client keeps the seed for its lifetime and
 * the tree below it is its memory of the catalog.  In a browser build the
 * loop is the same again with an await on fetch() in place of my_fetch().
 */
typedef struct das_node_walk DasNodeWalk;

/** What a walk is waiting for */
typedef enum das_walk_state_enum {
	das_walk_fetch = 0,  /**< a document, see DasNodeWalk_url() */
	das_walk_done,       /**< nothing, DasNodeWalk_result() has the node */
	das_walk_failed      /**< nothing, no path led to it, see DasNodeWalk_nearest() */
} das_walk_state_e;

/** Start a walk.
 *
 * @param pFrom The container to start from, borrowed; it must outlive
 *        the walk.
 * 
 * @param sRelPath The path below pFrom, with or without the separator
 *        pFrom uses in front.  Copied.  Empty means pFrom itself and the
 *        walk is done at once.  Not expanded: a full URI is spelled out,
 *        "tag:das2.org,2012:..."
 * 
 * @returns A new walk, to be freed with del_DasNodeWalk(), already
 *        advanced to its first state.  NULL after logging why not: no
 *        start node, or one that is not a container.
 * @memberof DasNodeWalk
 */
DAS_API DasNodeWalk* new_DasNodeWalk(DasNode* pFrom, const char* sRelPath);

/** Start a walk that loads the compiled in seed.
 *
 * One step, whose candidates are the URLs from das_seed_urls() in order,
 * so a mirror that does not answer is passed over for the next.  The
 * result is the index of the das catalog, a root: it has no URI, its
 * children are the top level catalogs by URI prefix, and it is the
 * caller's to keep and to free with del_DasNode().  Everything else in
 * the federated catalog is a walk from it.
 *
 * @returns A new walk, to be freed with del_DasNodeWalk(), waiting for
 *        a document.
 * @memberof DasNodeWalk
 */
DAS_API DasNodeWalk* new_DasNodeWalk_seed(void);

/** Free a walk.  A finished walk's result stays where the walk attached
 * it, or in the caller's hands for a seed.  Nodes loaded by a walk that
 * did not finish go with it.
 * @memberof DasNodeWalk */
DAS_API void del_DasNodeWalk(DasNodeWalk* pThis);

/** @memberof DasNodeWalk */
DAS_API das_walk_state_e DasNodeWalk_state(const DasNodeWalk* pThis);

/** The number of candidate URLs on offer, 0 unless the state is
 * das_walk_fetch.  @memberof DasNodeWalk */
DAS_API size_t DasNodeWalk_urlCount(const DasNodeWalk* pThis);

/** Candidate number uIdx, borrowed and valid until the next feed or fail,
 * or NULL past the end.  @memberof DasNodeWalk */
DAS_API const char* DasNodeWalk_url(const DasNodeWalk* pThis, size_t uIdx);

/** The catalog URI of the node this step would load, for messages and
 * for a driver's cache.  "" when loading a seed, which has none.
 * @memberof DasNodeWalk */
DAS_API const char* DasNodeWalk_uri(const DasNodeWalk* pThis);

/** Hand the walk the document a candidate URL produced.
 *
 * @param uIdx Which candidate, see DasNodeWalk_url().  Candidates before
 *        it are dropped as not tried.
 * @param pBytes The document text, need not be NUL terminated.
 * @param uBytes Its length.
 * @param sFinalUrl The URL after redirects, for messages.  NULL means
 *        the candidate itself.
 * @returns DAS_OKAY when the text parsed as a node, whether or not it led
 *        anywhere, and the walk has advanced.  DASERR_NODE when it did
 *        not parse, or is not a Catalog when a seed was wanted, after
 *        logging; that candidate is dropped and the walk has advanced as
 *        after DasNodeWalk_fail().  Also DASERR_NODE, with nothing
 *        changed, when the walk was not waiting or uIdx is out of range.
 * @memberof DasNodeWalk
 */
DAS_API DasErrCode DasNodeWalk_feed(
	DasNodeWalk* pThis, size_t uIdx, const char* pBytes, size_t uBytes,
	const char* sFinalUrl
);

/** Tell the walk a candidate URL produced nothing.
 * @param uIdx Which candidate.  It and those before it are dropped.
 * @param sWhy For the debug log, may be NULL.
 * @memberof DasNodeWalk */
DAS_API void DasNodeWalk_fail(DasNodeWalk* pThis, size_t uIdx, const char* sWhy);

/** The node a finished walk reached, or NULL.  Attached under the start
 * node's tree, or for new_DasNodeWalk_seed() the seed itself, the
 * caller's to del_DasNode().
 * @memberof DasNodeWalk */
DAS_API DasNode* DasNodeWalk_result(const DasNodeWalk* pThis);

/** After a failure, the deepest container the walk searched.
 *
 * Only its URI: a node the walk loaded was not kept.  A session can load
 * it again cheaply and say "no child X under Y, which has ...".  An empty
 * string names the start node, when that is a root; it is the one
 * container the caller still holds.
 *
 * @param psRest If not NULL, receives the part of the path below that
 *        container, valid for the life of the walk.
 * @returns The URI, or NULL when the state is not das_walk_failed or no
 *        container was ever searched, as when no seed URL answered.
 * @memberof DasNodeWalk */
DAS_API const char* DasNodeWalk_nearest(const DasNodeWalk* pThis, const char** psRest);

/** @} */

/** Free a node a constructor returned, and every node loaded below it.
 *
 * A child node, one a walk attached, is ignored: it belongs to its
 * parent and is freed with it.
 * @memberof DasNode
 */
DAS_API void del_DasNode(DasNode* pNode);


#ifdef __cplusplus
}
#endif

#endif /* _das_node_h_ */
