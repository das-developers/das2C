/* Copyright (C) 2026 Chris Piker <chris-piker@uiowa.edu>
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

/** @file catalog.h A catalog session: roots, credentials and node memory
 * in one object, with mountable sub-trees */

#ifndef _das_catalog_h_
#define _das_catalog_h_

#include <das3/node.h>

#ifdef __cplusplus
extern "C" {
#endif

/** @defgroup catalog Catalogs
 * Finding data sources: the federated das catalog, catalog paths, and the
 * objects that walk it.
 *
 * A das catalog is a tree of small JSON documents, mostly on web servers,
 * leading from a well known root down to data source definitions.  Each
 * document is a node.  Container nodes (Catalog, Collection, SourceSet)
 * list their children by name along with the URLs each child can be loaded
 * from.  Leaf nodes (HttpStreamSrc, FileTimeAgg) describe how to get data.
 * The tree is federated: the root at das2.org names a few top level trees
 * and each site maintains its own branch.  A path is resolved one network
 * request per level, and only the levels on the way to the node asked for
 * are ever loaded.
 *
 * Nodes are named by catalog path, which looks like a file path with a URI
 * scheme in front:
 *
 *   tag:das2.org,2012:site:/uiowa/juno/wav/survey
 *
 * In most programs the "tag:das2.org,2012:site:/" prefix is noise, so the
 * library also accepts the relative form "uiowa/juno/wav/survey" and the
 * shorthand "site:/uiowa/juno/wav/survey".  A node can also be
 * named by location, the URL or file it is stored at, which is how test
 * catalogs and development servers are reached.
 *
 * To get a data source definition, open a catalog session, resolve a path
 * and read the node:
 *
 * @code
 DasCat* pCat = new_DasCat(NULL, "my_program/1.0");

 DasNode* pNode = DasCat_resolve(pCat, "uiowa/juno/wav/survey");
 if((pNode == NULL) || !DasNode_isStreamSrc(pNode)) handle_error();

 // The query interface of the source, laid out by the das catalog schema
 const DasJdo* pProto = DasNode_getJdo(pNode, "protocol");
 ...
 del_DasCat(pCat);   // frees every node the session loaded
 @endcode
 *
 * Two objects do the work.  DasCat, in das3/catalog.h, is a session: it
 * holds the root policy (where relative paths start, see DasCat_setRoot()
 * and DasCat_mount()), the credentials and user agent every load uses, and
 * the memory of every node it loads.  DasNode, in das3/node.h, is one node:
 * its type, path URI, source URL and parsed document, with DasNode_subNode()
 * to step down to a child.  Most programs only ever construct a DasCat.
 * The node constructors in node.h are the lower level interface for
 * programs that manage roots themselves; see "Working with nodes directly"
 * below.
 */

/** @addtogroup catalog
 * @{
 */

/** The catalog URI prefix relative paths start from unless told otherwise */
#define DASCAT_DEF_PREFIX "tag:das2.org,2012:site:/"

/** A catalog session, opaque.
 *
 * A DasCat turns catalog paths into nodes.  It remembers where relative
 * paths start, which credentials and user agent to send, and every node it
 * has loaded, so a program, a MATLAB extension or a Python module can say
 * "resolve this path" and get a node back without managing roots itself.
 *
 * Paths given to DasCat_resolve() are catalog paths, not web paths, in any
 * of these forms:
 *
 *   - relative to the prefix: "uiowa/juno/wav/survey"
 *   - the shorthands "site:/uiowa/..." and "test:/..."
 *   - a full catalog URI: "tag:das2.org,2012:site:/uiowa/..."
 *   - a node by location, a URL or a file: "https://host/node.json",
 *     "./node.json"
 *
 * Where relative paths start is the root policy, and the catalog tree is
 * navigated like a file system, so the policy borrows its words:
 *
 *   - By default relative paths start at DASCAT_DEF_PREFIX in the
 *     federated catalog.  DasCat_setPrefix() moves that.
 *   - DasCat_setRoot() makes one node, loaded by location, the whole
 *     catalog.  Optionally the node is given its catalog URI, so paths
 *     under that URI resolve through it and DasNode_pathUri() reports
 *     full URIs.
 *   - DasCat_mount() puts a node, loaded by location, at a catalog URI
 *     on top of the federated catalog.  Paths under the URI resolve
 *     through the mounted node, everything else through the compiled in
 *     roots as usual.  Several nodes may be mounted.
 *
 * Policy is set before the first resolve.  Nodes returned by
 * DasCat_resolve() are owned by the catalog and live until del_DasCat().
 *
 * @code
 DasCat* pCat = new_DasCat(NULL, "my_program");
 DasNode* pNode = DasCat_resolve(pCat, "uiowa/juno/wav/survey");
 if(pNode == NULL){
    const char* sRest = NULL;
    DasNode* pNear = DasCat_nearest(pCat, &sRest);
    // pNear is the deepest catalog that did load, sRest what it lacks
 }
 del_DasCat(pCat);
 @endcode
 */
typedef struct das_cat DasCat;

/** Is this string a node location rather than a catalog path?
 *
 * True for http://, https:// and file:// URLs, for anything starting with
 * '.', '/', '\' or a drive letter, and for anything ending in ".json".  All else
 * is taken to be a catalog path or URI.  This is the rule DasCat_resolve()
 * uses, published so command line parsers can agree with it.
 *
 * @memberof DasCat
 */
DAS_API bool das_isUrlOrFile(const char* sPath);

/** Create a catalog session
 *
 * @param pMgr Credentials manager consulted when a node or data source asks
 *        for a login.  May be NULL for public access only.  Ownership
 *        passes to the catalog, del_DasCat() frees it.
 * @param sAgent User agent string sent with every request, copied.  NULL
 *        sends the library default.
 * @returns a new catalog, free with del_DasCat()
 * @memberof DasCat
 */
DAS_API DasCat* new_DasCat(DasCredMngr* pMgr, const char* sAgent);

/** Free a catalog session, every node it loaded, and its credentials manager
 * @memberof DasCat
 */
DAS_API void del_DasCat(DasCat* pThis);

/** The credentials manager, borrowed, for data requests made by the caller
 * @returns the manager given to new_DasCat(), or NULL
 * @memberof DasCat
 */
DAS_API DasCredMngr* DasCat_credMngr(const DasCat* pThis);

/** The user agent string, borrowed
 * @returns the string given to new_DasCat(), or NULL
 * @memberof DasCat
 */
DAS_API const char* DasCat_agent(const DasCat* pThis);

/** Keep local copies of catalog nodes this session fetches over HTTP
 *
 * The per session form of DasNode_setCache().  Until called, nodes loaded
 * through this catalog follow the process wide setting.  Nodes read from
 * local files are never cached.
 *
 * @param sDir Directory for the copies, created on first use.  NULL or an
 *        empty string turns caching off for this session, whatever the
 *        process default says.
 * @param nMaxAgeSec Copies older than this many seconds are fetched again.
 *        Zero or less means always fetch, but still save the result.
 * @memberof DasCat
 */
DAS_API void DasCat_setCache(DasCat* pThis, const char* sDir, int nMaxAgeSec);

/** Where relative paths start in the federated catalog
 *
 * The shorthands "site:" and "test:" are expanded.  A URL or file is
 * refused, those are roots, see DasCat_setRoot().
 *
 * @param sUri A catalog URI prefix, typically ending in the separator the
 *        container uses, such as "tag:das2.org,2012:test:/"
 * @returns DAS_OKAY, or DASERR_CAT with the reason logged
 * @memberof DasCat
 */
DAS_API DasErrCode DasCat_setPrefix(DasCat* pThis, const char* sUri);

/** The current prefix, DASCAT_DEF_PREFIX unless DasCat_setPrefix() was called
 * @memberof DasCat
 */
DAS_API const char* DasCat_prefix(const DasCat* pThis);

/** Make one node, loaded by location, the whole catalog
 *
 * Relative paths then start at that node and nothing outside it can be
 * reached.  Refused if a root is already set or anything is mounted.
 *
 * Without a URI the root has no catalog name: DasNode_pathUri() on nodes
 * under it gives paths relative to the root, and a full URI given to
 * DasCat_resolve() is just another relative path.  With a URI the root is
 * declared to be the node of that name, as a development server's copy of
 * a catalog branch stands for the branch's federated name.  Then full URIs
 * under it resolve through it, nodes report full URIs, and URIs outside it
 * fail, which DasCat_covers() can tell apart from an ordinary miss.
 *
 * @param sUrl A URL or file path that new_RootNode_url() accepts
 * @param sUri The catalog URI the node has, shorthands accepted, or NULL
 *        for a nameless root
 * @returns DAS_OKAY, or DASERR_CAT with the reason logged
 * @memberof DasCat
 */
DAS_API DasErrCode DasCat_setRoot(DasCat* pThis, const char* sUrl, const char* sUri);

/** The root location given to DasCat_setRoot(), or NULL if none is set
 * @memberof DasCat
 */
DAS_API const char* DasCat_rootUrl(const DasCat* pThis);

/** The root's catalog URI, or NULL if the root is nameless or not set
 * @memberof DasCat
 */
DAS_API const char* DasCat_rootUri(const DasCat* pThis);

/** Put a node, loaded by location, at a catalog URI over the federated catalog
 *
 * Paths under sUri load from sUrl and shadow whatever the federated
 * catalog says for them, everything else resolves through the compiled in
 * roots as usual.  May be called more than once; when one mount point is
 * under another the longer URI wins.  Relative paths start at the first
 * mount unless DasCat_setPrefix() was called, then they start at the
 * prefix.  Refused if a root is set.
 *
 * @param sUrl Where to load the node from, a URL or a file
 * @param sUri The catalog URI to mount it at, shorthands accepted
 * @returns DAS_OKAY, or DASERR_CAT with the reason logged
 * @memberof DasCat
 */
DAS_API DasErrCode DasCat_mount(DasCat* pThis, const char* sUrl, const char* sUri);

/** Number of nodes mounted with DasCat_mount()
 * @memberof DasCat
 */
DAS_API size_t DasCat_mountCount(const DasCat* pThis);

/** True if a root is set, so paths no longer reach the whole federated catalog
 * @memberof DasCat
 */
DAS_API bool DasCat_hasRoot(const DasCat* pThis);

/** Resolve a path to a node under the root policy
 *
 * Network access happens here, one request per catalog level not yet
 * loaded.  Nodes are kept, a second resolve of the same path costs nothing.
 *
 * @param sPath A catalog path in any of the forms listed for DasCat.  NULL
 *        or empty means the root itself: the prefix container, the root
 *        node, or the first mounted node.
 * @returns the node, owned by the catalog, or NULL if the path did not
 *        resolve.  DasCat_lastUri() then gives the URI that was tried and
 *        DasCat_nearest() the deepest node that did load.
 * @memberof DasCat
 */
DAS_API DasNode* DasCat_resolve(DasCat* pThis, const char* sPath);

/** The full catalog URI of the last path given to DasCat_resolve(), as the
 * policy understood it, resolved or not.  For a path by location this is
 * the location.
 * @returns the URI, or NULL before the first resolve
 * @memberof DasCat
 */
DAS_API const char* DasCat_lastUri(const DasCat* pThis);

/** Can this URI be reached under the current policy?
 *
 * False only when a named root is set and sUri is not under its URI.  A
 * failed resolve of a URI this returns false for is a policy miss, not a
 * catalog miss, and the message to the user should say so.
 * @memberof DasCat
 */
DAS_API bool DasCat_covers(const DasCat* pThis, const char* sUri);

/** After a failed resolve, the deepest catalog node on the path that did load
 *
 * Strips the last path component and resolves again, repeating until a
 * container node loads or the path is exhausted.  Costs network requests
 * only for levels not already loaded.  DasCat_lastUri() afterwards names
 * the node returned, so a caller can say "no child X under URI Y" and list
 * what Y does have with DasNode_subName().
 *
 * @param psRest If not NULL, receives a pointer to the part of the failed
 *        path below the node returned, such as "survey/das2", valid until
 *        the next resolve.  NULL when nothing is returned.
 * @returns the node, owned by the catalog, or NULL if no part of the path
 *        resolves, the last resolve succeeded, or the path was a location.
 * @memberof DasCat
 */
DAS_API DasNode* DasCat_nearest(DasCat* pThis, const char** psRest);

/** @} */

#ifdef __cplusplus
}
#endif

#endif /* _das_catalog_h_ */
