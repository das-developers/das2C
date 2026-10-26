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
 * @par Working with nodes directly
 *
 * The node constructors in this header are what DasCat is built on.  Use
 * them when a program manages its own roots, for instance to hold two
 * unrelated catalogs at once, or to load a single isolated node from a URL
 * and nothing else.
 *
 * There are two basic methods of node acquisition, finding a node by
 * its path from the global root of the federated das2 catalog system:
 * (ex: tag:das2.org,2012:site:/swri/mars_express/aspera3/sciels) 
 * or by loading an explicit URL 
 * (ex: http://das2.org/catalog/das/site/uiowa/cassini/rpws/survey.json).
 * 
 * When describing the das2 catalog C-API there are two meanings for the word 
 * Root.  
 * 
 * 1) Memory Roots - A node can be the in-memory root of a tree of nodes.
 * 
 * 2) Global Root - Root can also refer to the global das2 top-level un-named
 *    node which resides at http://das2.org/catalog/index.json and its mirrors.
 * 
 * Any node loaded using new_RootNode() or new_RootNode_url() is an *in memory*
 * root node.  It might not be the root of a federated catalog system, but it
 * is still a top level memory item that manages a memory cache for its 
 * subnodes.  If new_RootNode() happens to be called with a URI of 'NULL' then
 * the in-memory root corresponds to the federated catalog root.  The reason for
 * splitting the concept of the in-memory root from the global catalog root 
 * is to provide for isolated catalogs that are not part of the global system.
 * 
 * When you instantiate a node from a random URL you have to give it a name.
 * Just like files on a disk, catalog nodes do not know their name, it is
 * derived from the name provided by the higher level catalog entries (up to
 * root, which has no name).  
 * 
 * An Example of getting a data source interface definition follows:
 * @code{.cpp}
 
 // Get the site root and safely cast the output.  Any particular point in
 // the catalog can be a root node for the purposes of sub-item queries and
 // memory management.  By calling new_RootNode() the returned item is a local
 // root object for memory management purposes.  The higher level nodes that
 // were traversed to get to the requested item are deleted from memory.
 
 DasNode* pRoot = new_RootNode("tag:das2.org,2012:site:/uiowa", NULL, NULL);
 if(!DasNode_isCatalog(pRoot)) handle_error();
 
 // Use the site catalog to get the Cassini Saturn SLS2 data source description
 
 const char* sRelPath = "cassini/ephemeris/saturn_sls2";
 DasNode* pNode = DasNode_subNode(pRoot, sRelPath, NULL, NULL);
 
 if(!DasNode_isStreamSrc(pNode)) handle_error();
 
 // Get the HTTP GET protocol definition
 
 const DasJdo* pDef = DasNode_getJdo(pNode, "protocol");
 if(pDef == NULL) handle_error();
  
 // Freeing the root node will delete all lower nodes
 
 del_RootNode(pRoot);
  
 @endcode
 * 
 * You now have a JSON object describing how the source is queried.  The
 * layout of HttpStreamSrc nodes is given by the das catalog schema, see
 * DasNode_version() for the two layouts in the wild.
 * 
 * For isolated sites, or for testing purposes catalog nodes can be loaded
 * directly.  The following example acquires a local test root and then a 
 * specific test node.  
 * 
 @code
 
 // Assume this site uses a password since it is a testing area.  Create a 
 // credentials manager and pass a basic auth hash so we aren't bothered during
 // testing.
 
 DasCredMngr* pMngr = new_CredMngr(NULL);
 das_credential cred;
 das_cred_init(&cred, "my.domain.com", "Regression Test Catalog", NULL, 
              "VVNFUk5BTUU6UEFTU1dPUkQ=");
 CredMngr_addCred(pMngr, &cred);
 
 const char* sTestUrl = "http://my.domain.com/das2/regtest.json";
 DasNode* pRoot = new_RootNode_url(sTestUrl, "regtest", pMngr, NULL);
 if(!DasNode_isCatalog(pRoot)) handle_error();
 
 // At this point sub-items can be loaded from the local root as above.  The
 // credentials manager given to the root is inherited by the lookup.
 
 DasNode* pNode = DasNode_subNode(pRoot, "MarsExpress/aspera/els", NULL, NULL);
 
 @endcode
 * 
 * 
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
 * Nodes are made by the new_RootNode*() constructors and DasNode_subNode(),
 * read through the accessors below, and freed by del_RootNode() on the
 * root that loaded them.  There is no public layout: the document, the
 * source URL and the path URI are reached with DasNode_getJdo(),
 * DasNode_srcUrl() and DasNode_pathUri().
 */
typedef struct das_node DasNode;

/** @} */

/** Return the compiled in catalog root URLs
 * @param  pLen - A pointer to a size_t to receive the number of builtin roots
 * @return A pointer to an array of null terminated strings.
 */
DAS_API const char** das_root_urls(size_t* pLen);

/** Create a new root catalog node via a path URI
 * 
 * Get a catalog node that is not attached to any parent nodes.  If the 
 * node acquired is a container type such as Root, Scheme, or Catalog then
 * it can be used to acquire further nodes.
 * 
 * This function consults the distributed das2 catalog to find and load nodes.  
 * See new_RootNode_url() for a version that only loads a specified URL.
 * 
 * @code{.cpp}
 
  DasNode* pRoot = NULL;
  
  // Get the das2 site root catalog node
  pRoot = new_RootNode(D2URI_SITES, NULL, NULL);
  
  // Get the U. Iowa site catalog top node
  pRoot = new_RootNode(D2URI_SITE_UIOWA, NULL, NULL);
  
 @endcode
 *  
 * @param sPathUri Retrieve a catalog node given a global path URI.  This a
 *               location from the top of the federated catalog system, not
 *               a file system or web path.  NULL loads the federated root
 *               itself.  The shorthands "site:/..." and "test:/..."
 *               are accepted for the two well known children of the das2
 *               root.
 * 
 * @param pMgr   A credentials manager object to consult if a password is
 *               requested.  May be set to NULL to indicate that only public 
 *               items may be requested.  Usually this is not needed as most
 *               catalog entries are public though the data sources they
 *               describe may not be.  The returned node remembers it and
 *               hands it to anything loaded through DasNode_subNode().
 * 
 * @param sAgent The user agent string you wish to send to the server.  If 
 *               NULL then the string "libdas/3" is sent.  Borrowed, not
 *               copied, so a string literal or a buffer that outlives the
 *               node.
 * 
 * @return A new DasNode object allocated on the heap, or NULL if object
 *               resolution failed.  Calling del_RootNode() for the returned
 *               pointer will delete all sub-nodes from memory as well.
 * 
 * @memberof DasNode
 */
DAS_API DasNode* new_RootNode(
	const char* sPathUri, DasCredMngr* pMgr, const char* sAgent
);

/** Create a new root catalog node via direct URL
 * 
 * Get a catalog node that is not attached to any parent nodes.  If the 
 * node acquired is a container type such as Root, Scheme, or Catalog then
 * it can be used to acquire further nodes.
 * 
 * There are two basic methods of node acquisition, set a path URI and let the
 * library find the node using the built-in global catalog location, or 
 * provide an explicit URL.
 * 
 * @code{.cpp}
 
  DasNode* pRoot = NULL;
    
  // Get a standalone local Voyager catalog root
  pRoot = new_RootNode_url("http://my.site.gov/test/local_vgr.json", "vgr", NULL, NULL);
 
  // Get a standalone data source description for Voyager PLS data
  pRoot = new_RootNode_url("http://my.site.gov/test/local_vgr/pls.json", "vgr/pls", NULL, NULL);

  // Nodes need not come from a server, a path or file:// URL reads a file
  pRoot = new_RootNode_url("./local_vgr.json", "vgr", NULL, NULL);
 
 @endcode
 *  
 * @param sUrl   Retrieve a catalog node from an explicit URL.  No searches 
 *               will be performed.  Anything that is not an http(s) URL is
 *               read as a local file, with or without a "file://" prefix.
 * 
 * @param sPathUri Since the node was not loaded by following the federated
 *               catalog system to an end point, you have to tell the node
 *               its name.  Any name works, it is saved and reported by
 *               DasNode_pathUri(), and children get it as a prefix.  May
 *               be the empty string for a root that has no name.
 * 
 * @param pMgr   A credentials manager object to consult if a password is
 *               requested.  May be set to NULL to indicate that only public 
 *               items may be requested.  Inherited by DasNode_subNode().
 * 
 * @param sAgent The user agent string you wish to send to the server.  If 
 *               NULL then the string "libdas/3" is sent.  Borrowed, not
 *               copied.
 * 
 * @return A new DasNode object allocated on the heap, or NULL if object
 *               resolution failed.  Use DasNode_type() to determine which
 *               type of catalog item was acquired.
 * 
 * @memberof DasNode
 */
DAS_API DasNode* new_RootNode_url(const char* sUrl, const char* sPathUri,
		                    DasCredMngr* pMgr, const char* sAgent);

/** Get a das2 catalog node contained item.
 * 
 * This function will search down the catalog hierarchy and find the requested
 * node, downloading any intermediate nodes as needed.  If the descendant node
 * has already been downloaded no new network activity is generated.
 * 
 * @param pThis A container node, one for which DasNode_isCatalog() is true
 * 
 * @param sRelPath  the ID of the descendant node.  This id is the portion
 *              that would be appended to this node's PathUri to make the
 *              complete URI of the final node.  sRelPath does not need to start
 *              with the sub-item separator (if one is defined for this catalog)
 *              but may at the callers option.  For example, both:
 *              ":/uiowa" and "uiowa" work for getting the uiowa subnode from
 *              the site catalog, but "/uiowa" would not since the separator
 *              defined for site is ":/".  
 *
 * @param pMgr   A credentials manager object to consult if a password is
 *               requested.  NULL means inherit the manager this node was
 *               loaded with, which is the one given to new_RootNode() or
 *               its relatives.  Pass one to override it for this lookup.
 * 
 * @param sAgent The user agent string to send.  NULL inherits the agent this
 *               node was loaded with; if none was ever given, "libdas/3".
 *
 * @return       A pointer to the child node, or NULL if no such child existed
 *               in the node cache nor could not be downloaded.
 * @memberof DasNode
 */
DAS_API DasNode* DasNode_subNode(
	DasNode* pThis, const char* sRelPath, DasCredMngr* pMgr, const char* sAgent
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
 * @param pThis the catalog node 
 * @return A pointer to the path URI string.  All catalog nodes have a path URI
 * @memberof DasNode
 */
DAS_API const char* DasNode_pathUri(const DasNode* pThis);

/** Get the location from which this catalog node was read.  
 * @param pThis the catalog node 
 * @return A pointer to the source URL string.  All catalog nodes are loaded
 *         from somewhere, das2C does not provide functions to generate
 *         node objects programmatically.
 * @memberof DasNode
 */
DAS_API const char* DasNode_srcUrl(const DasNode* pThis);

/** Get the type of node
 * This is a more specific question than the 'is' functions below, which should
 * probably be used instead so that application code can work in a "duck-typing"
 * manner.
 * @param pThis the catalog node 
 * @return The node type from the 
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

/** Get the separator a container node puts between its own path URI and
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
 * is not the child node.  Use DasNode_subNode() to load that.
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
 * @param pThis The catalog node 
 * @return True if DasNode_getJdo() usable
 */
DAS_API bool DasNode_isJson(const DasNode* pThis);

/**  Get a JSON document object at a fragment location in a node.
 * @param pThis The catalog node
 * @param sFragment The fragment path, for example "CONTACTS/TECH/0/EMAIL"
 * @return The json object at that location, or NULL if the node does not 
 *         contain the given element.
 * @memberof DasNode
 */
DAS_API const DasJdo* DasNode_getJdo(const DasNode* pThis, const char* sFragment);


/** Delete a root node freeing its memory.
 * All sub-nodes will be deleted as well.
 * @memberof DasNode
 */
DAS_API void del_RootNode(DasNode* pNode);


#ifdef __cplusplus
}
#endif

#endif /* _das_node_h_ */
