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
#include <das3/source.h>

#ifdef __cplusplus
extern "C" {
#endif

/** @defgroup catalog Catalogs
 * Finding data sources: the federated das catalog, catalog URIs, and the
 * objects that walk it.
 *
 * A das catalog is a tree of small JSON documents, mostly on web servers,
 * leading from a well known root down to data source definitions.  Each
 * document is a node.  Container nodes (Catalog, Collection, SourceSet)
 * list their children by name along with the URLs each child can be
 * loaded from.  Leaf nodes (HttpStreamSrc, FileTimeAgg) describe how to
 * get data.  The tree is federated: the root at das2.org names a few top
 * level trees, and any node may have children maintained by a different
 * organization.  A path is resolved one network request per level, and
 * only the levels on the way to the node asked for are ever loaded.
 *
 * @par Names
 *
 * Every node has a globally unique URI, composed by walking down from the
 * root: each container appends its separator and the child's name to its
 * own URI.  A node does not know its name, the walk gives it one.  The
 * result looks like a file path with a URI scheme in front:
 *
 *   tag:das2.org,2012:site:/uiowa/juno/wav/survey
 *
 * The RFC 4151 "tag" scheme is the usual one, though others such as "doi"
 * work just as well.  URLs are not used as names because they change
 * whenever an organization reshuffles its servers, while a name must not.
 * For that reason a name may not start with a locator scheme, "http",
 * "https", "ftp" and so on; the library reads those as locations, see
 * das_isUrlOrFile().
 *
 * In daily use the prefix "tag:das2.org,2012:site:/" is noise, so the
 * library also accepts the relative form "uiowa/juno/wav/survey" and the
 * shorthand "site:/uiowa/juno/wav/survey".
 *
 * Since names come from the walk, a local file can be declared to be any
 * node.  For testing, the file
 *
 *    ./survey.json
 *
 * can be mounted as
 *
 *    tag:das2.org,2012:site:/uiowa/juno/wav/survey
 *
 * and walking proceeds from there as if the file had been fetched from
 * the catalog, see DasCat_mount() and DasCat_setRoot().  A file loaded
 * with no name at all is named after its location under D2URI_LOCAL.
 *
 * @par Reading a node
 *
 * Open a catalog session, resolve a path and read the node:
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
 * Programs that want to query a data source rather than inspect its node
 * use DasCat_newSrc(), which resolves the path and builds a DasSrc, see
 * source.h.
 *
 * Two objects do the work.  DasCat, in das3/catalog.h, is a session: it
 * holds the root policy (where relative paths start, see DasCat_setRoot()
 * and DasCat_mount()), the credentials and user agent every load uses,
 * and the memory of every node it loads.  DasNode, in das3/node.h, is one
 * node: its type, URI, source URL and parsed document, with
 * DasNode_subNode() to step down to a child.  Most programs only ever
 * construct a DasCat.  The node constructors in node.h are the lower
 * level interface for programs that manage roots themselves; see
 * "Working with nodes directly" in node.h.
 */

/** @addtogroup catalog
 * @{
 */

/** The catalog URI prefix relative paths start from unless told otherwise */
#define DASCAT_DEF_PREFIX "tag:das2.org,2012:site:/"

/** How long, in seconds, a new session trusts a cached catalog node */
#define DASCAT_DEF_CACHE_SEC 3600

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
 *     "./node.json".  Such a node is named after its location under
 *     D2URI_LOCAL, since every node has a URI.
 *
 * Where relative paths start is the root policy, and the catalog tree is
 * navigated like a file system, so the policy borrows its words:
 *
 *   - By default relative paths start at DASCAT_DEF_PREFIX in the
 *     federated catalog.  DasCat_setPrefix() moves that.
 *   - DasCat_mount() puts a node, loaded by location, at a catalog URI
 *     on top of the federated catalog.  Paths under the URI resolve
 *     through the mounted node, everything else through the compiled in
 *     roots as usual.  Several nodes may be mounted.
 *   - DasCat_setRoot() mounts a node and makes the mounts the whole
 *     catalog: relative paths start at that node and nothing outside
 *     the mounts can be reached.  This is how an offline catalog is
 *     assembled from a few files.
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

/** Is this string a full URI rather than a relative catalog path?
 *
 * True when it starts with a URI scheme, a letter followed by letters,
 * digits, '+', '-' or '.' and then a colon, as in "tag:", "doi:" and the
 * shorthands "site:" and "test:".  A single letter before the colon is a
 * Windows drive, not a scheme.  Note that URLs are URIs too; test with
 * das_isUrlOrFile() first when the difference matters.
 *
 * @memberof DasCat
 */
DAS_API bool das_isUri(const char* sPath);

/** Create a catalog session
 * 
 * The returned catalog it's node cache enabled right out of the gate,
 * to turn it off see DasCat_setCache()
 *
 * @param pMgr Credentials manager consulted when a node or data source asks
 *        for a login.  May be NULL for public access only.  Ownership
 *        passes to the catalog, del_DasCat() frees it.
 * @param sAgent User agent string sent with every request, copied.  NULL
 *        sends the library default.
 * @returns a new catalog, free with del_DasCat().
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

/** Change where, and for how long, this session keeps copies of catalog
 * nodes fetched over HTTP
 *
 * A new session already caches: copies go under das_userdir() in
 * cache/nodes and are trusted for DASCAT_DEF_CACHE_SEC.  Walking a catalog
 * costs one request per level, so this is what makes the second run of a
 * program fast.  A copy older than that is not thrown away: the server is
 * asked whether it changed, using the ETag or Last-Modified it came with,
 * and only a changed node is downloaded again.  A server that cannot be
 * reached leaves the old copy in use, with a warning.  Nodes read from
 * local files are not cached, and neither are nodes made through the
 * constructors in node.h
 *
 * @param sDir Directory for the copies, created on first use.  NULL or an
 *        empty string turns caching off for this session.  Passing
 *        DasCat_cacheDir() is allowed, and can be used to change the 
 *        default node expiration time.
 * @param nMaxAgeSec Copies older than this many seconds are checked with
 *        the server before use.  Zero or less means check every time,
 *        which costs one round trip per level and no body bytes when the
 *        server supports validators, and a full fetch when it does not.
 * @memberof DasCat
 */
DAS_API void DasCat_setCache(DasCat* pThis, const char* sDir, int nMaxAgeSec);

/** The directory this session caches nodes in, or NULL if caching is off
 * @memberof DasCat
 */
DAS_API const char* DasCat_cacheDir(const DasCat* pThis);

/** How many seconds this session trusts a cached node, see DasCat_setCache()
 * @memberof DasCat
 */
DAS_API int DasCat_cacheAge(const DasCat* pThis);

/** Where relative paths start in the federated catalog
 *
 * The shorthands "site:" and "test:" are expanded; given alone they name
 * the container and its ":/" child separator is supplied, so "site:" is
 * the same prefix as "tag:das2.org,2012:site:/".  A URL or file is
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

/** Mount a node and make the mounts the whole catalog
 *
 * Relative paths then start at this node, full URIs resolve only when a
 * mount covers them, and everything else fails, which DasCat_covers() can
 * tell apart from an ordinary miss.  Further DasCat_mount() calls, before
 * or after, add to the offline catalog.  Refused once a root is set.
 *
 * The URI declares what the node is: a development server's copy of a
 * catalog branch stands for the branch's federated name, so requisitions
 * made against it are good against the live catalog too.  Without a URI
 * the node is named after its location under D2URI_LOCAL.  That is fine
 * for a one off file, but a requisition stamped with a local: name will
 * not match the catalog node the file was copied from; give the real URI
 * when it has one.
 *
 * @param sUrl A URL or file path that new_RootNode_url() accepts
 * @param sUri The catalog URI the node has, shorthands accepted, or NULL
 *        to name it by location
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
 * prefix.  With a root set, see DasCat_setRoot(), the mount joins the
 * offline catalog instead.
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

/** True if a root is set, so paths reach only the mounted nodes
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
 * False when a root is set and no mount covers sUri, and for a local:
 * name, see D2URI_LOCAL, that no mount covers; those are never looked up
 * in the federated catalog.  A failed resolve of a URI this returns false
 * for is a policy miss, not a catalog miss, and the message to the user
 * should say so.
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

/** Resolve a path and build the data source it leads to.
 *
 * Catalog paths usually end at a Collection or SourceSet whose children
 * are the actual sources, often one per server interface, and users
 * rarely type the child's name.  This resolves the path as
 * DasCat_resolve() does and then picks: the node itself if it is
 * already a stream source, else the child named "flex", which is the
 * current server interface, else the first stream source child in the
 * container's order.  The source is built from that node with
 * new_DasSrc_node().
 *
 * @param sPath A catalog path in any form DasCat_resolve() accepts,
 *        including a URL or file name.
 * @returns A new source, to be freed with del_DasSrc() and independent
 *        of this session afterwards, or NULL after logging why not.  As
 *        with a failed resolve, DasCat_lastUri() and DasCat_nearest()
 *        say how far the path got.
 * @memberof DasCat
 */
DAS_API DasSrc* DasCat_newSrc(DasCat* pThis, const char* sPath);

/** @} */

#ifdef __cplusplus
}
#endif

#endif /* _das_catalog_h_ */
