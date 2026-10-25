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

/* Node internals shared by node.c and catalog.c.  Not installed.
 *
 * DasNode is opaque to applications so that the layout below can change
 * after the v3.0 tag without breaking them.  C has no package-private, so
 * the two files that need the fields include this header and everything
 * else goes through the accessors in node.h.  Functions declared here have
 * no DAS_API: the library is built with hidden visibility, so they are
 * reachable inside libdas3 and nowhere else.
 */

#ifndef _das_node_priv_h_
#define _das_node_priv_h_

#include "node.h"

/* How a node tree works, for readers of node.c and catalog.c.
 *
 * Derived types.  struct das_node is the common header of every node.
 * Nodes that have children (Catalog, Collection, SourceSet and the SPASE
 * and SPDF trees) are really a DasCatNode, defined in node.c, whose first
 * member is a DasNode so that either can be handled through a DasNode*.
 * The type is checked before casting, see DasNode_isCatalog().  Terminal
 * nodes (stream sources, file aggregations) are a bare struct das_node.
 *
 * Loading.  Every node begins as a JSON document that _DasNode_mkNode()
 * reads from a file, from the cache, or over HTTP, and _DasNode_fromJson()
 * turns into a struct of the right type.  The parsed DasJdo is kept in pDom
 * for the life of the node and the accessors read from it rather than
 * copying fields out.  DasNode_subNode() matches a relative path against
 * the names in the parent's container dictionary, loads the matching
 * child's URL and remembers the child in the parent's pSubNodes array, so
 * a second lookup of the same path costs nothing.
 *
 * Ownership.  Trees are owned from the top.  A node with bIsRoot set is a
 * memory root and del_RootNode() on it frees everything loaded below it.
 * Callers never free a non-root node, it belongs to its parent's pSubNodes
 * array.  A memory root need not be a catalog root: _DasNode_newRoot()
 * walks down from the federated roots to the requested node, marks that
 * one as a root, then deletes the nodes above it (del_NonRootNode() skips
 * roots).  An application holding site:/uiowa/juno/wav thus owns a one
 * node tree with no parent in memory.
 *
 * Inherited settings.  pMgr, sAgent and pCache are borrowed from whoever
 * made the root and copied into each child as it loads, so a DasCat's
 * credentials, agent string and cache follow a whole walk without being
 * passed at every call.  None are owned by the node; the caller keeps them
 * alive for as long as the tree exists.
 */

/* Keeps track of downloaded nodes and their age.
 *
 * A manager object such as DasCat owns node caches, DasNode just uses the cache.
  */
typedef struct das_node_cache {
	char sDir[256];    /* empty means no caching */
	int  nMaxAgeSec;   /* zero or less means always fetch, but still save */
} DasNodeCache;

struct das_node {
	das_node_type_e nType;
	char sURL[512];        /* where this node was loaded from */
	char sPath[512];       /* the node's catalog path URI, may be empty */
	bool bIsRoot;          /* a memory root, owns everything loaded below it */
	void* pDom;            /* the parsed document, a DasJdo for JSON nodes */
	DasCredMngr* pMgr;     /* borrowed from the loader, inherited by children */
	const char* sAgent;    /* borrowed, usually a string literal */
	const DasNodeCache* pCache;  /* borrowed, inherited by children */
};

/* Root constructors.  The public new_RootNode() and new_RootNode_url() in
 * node.h are thin wrappers that pass NULL for pCache and so use the process
 * default from DasNode_setCache().  A DasCat passes its own settings. */

/* Load the node at a catalog path URI by walking down from the built in
 * federated roots, see das_root_urls().
 *
 * Each root URL is tried in turn with a short connect timeout and the whole
 * list is retried with longer ones before giving up, so one slow mirror
 * does not stall a walk.  The node found is marked as a memory root and
 * every node above it is freed, see Ownership above.  Credentials are not
 * offered to the top level roots, only to the nodes below them, which is
 * where restricted catalogs live.
 *
 * @param sPathUri The full catalog URI of the node wanted, for example
 *        "tag:das2.org,2012:site:/uiowa/juno/wav".  The "site:" and "test:"
 *        shorthands are expanded to full tag URIs first.  NULL returns the
 *        federated root node itself.
 *
 * @param pMgr The credentials manager offered to nodes below the top level,
 *        and inherited by every node loaded later from the result.  May be
 *        NULL if no restricted catalogs will be walked.  Borrowed.
 *
 * @param sAgent The HTTP user agent string sent with every request, also
 *        inherited.  Borrowed, usually a string literal.
 *
 * @param pCache Where fetched node documents are saved and how long they
 *        are trusted, inherited by the whole tree.  NULL selects the process
 *        default from DasNode_setCache().  Borrowed.
 *
 * @returns A new memory root, to be freed with del_RootNode(), or NULL if no
 *        federated root could be reached or none of them lead to sPathUri.
 *        Failures are logged, not raised through das_error().
 */
DasNode* _DasNode_newRoot(
	const char* sPathUri, DasCredMngr* pMgr, const char* sAgent,
	const DasNodeCache* pCache
);

/* Load one node directly from a URL or file path and make it a memory root.
 *
 * The maximum connect timeout is used since there is no alternate URL to
 * fall back on.
 *
 * @param sUrl An http://, https:// or file:// URL, or a bare file system
 *        path, of a node document.  Local files are read directly and never
 *        cached.
 *
 * @param sPathUri The catalog URI to record for the node, which the loader
 *        has no way to know from the URL alone.  It may be any name the
 *        caller likes, or "" when no URI applies, as for a nameless DasCat
 *        root.  Children extend it using the node's own separator.
 *
 * @param pMgr Credentials for this fetch and for every node loaded later
 *        from the result.  May be NULL.  Borrowed.
 *
 * @param sAgent The HTTP user agent string, inherited by children.
 *        Borrowed, usually a string literal.
 *
 * @param pCache Cache settings for the tree, NULL for the process default.
 *        Borrowed.
 *
 * @returns A new memory root, to be freed with del_RootNode(), or NULL if
 *        the document could not be read or is not a recognized node type.
 *        Failures are logged, not raised through das_error().
 */
DasNode* _DasNode_newRootUrl(
	const char* sUrl, const char* sPathUri, DasCredMngr* pMgr, const char* sAgent,
	const DasNodeCache* pCache
);

/* A synthetic Catalog node whose children are other nodes placed at chosen
 * catalog URIs.  This is the machinery behind DasCat_mount() and the named
 * form of DasCat_setRoot().
 *
 * It works the way the federated root catalog at das2.org does.  That node
 * has "separator":null, so its children are named by whole URI prefixes
 * such as "tag:das2.org,2012:" and node.c cannot split a path on a
 * separator to find one.  Instead _DasNode_subNode() walks the children in
 * document order and takes the FIRST child whose name is a prefix of the
 * requested path, then resolves the rest of the path below that child.
 *
 * A mount root is a small JSON document of that shape, written in memory
 * and parsed like any downloaded node.  Each child is named by the full URI
 * of a mount and its urls list holds the file or URL to load there.  For
 * the two mounts (./uiowa.json at site:/uiowa) and (./wav.json at
 * site:/uiowa/juno/wav) with fall-through, the document is in spirit:
 *
 *   { "type":"Catalog", "separator":null, "catalog":{
 *       "tag:das2.org,2012:site:/uiowa/juno/wav": {"urls":["./wav.json"]},
 *       "tag:das2.org,2012:site:/uiowa":          {"urls":["./uiowa.json"]},
 *       "": {"urls":["https://das2.org/catalog/index.json", ...]}
 *   }}
 *
 * Resolving site:/uiowa/juno/wav/survey matches the wav child, loads
 * wav.json and looks up "/survey" inside it.  site:/uiowa/cassini skips
 * wav and matches uiowa.  A CDAWeb URI matches neither mount, but the
 * empty string is a prefix of every path, so the last child wins and the
 * federated roots take over.
 *
 * Ordering matters because the first match wins, not the best match.  If
 * uiowa were listed before wav it would also claim every wav path and
 * wav.json would never be consulted.  Hence the children are written
 * longest URI first, so a more specific mount shadows a broader one.
 *
 * With bFallThrough the empty-named child is appended and any path no mount
 * claims resolves through the normal catalog.  Without it the mounts are
 * the whole catalog and everything else fails to resolve.  A named root is
 * exactly that with a single mount, which is how DasCat_setRoot() builds
 * one.
 *
 * @param aUrl The URL or file path to load for each mount, in the same order
 *        as aUri.  None may be NULL or empty.
 *
 * @param aUri The full catalog URI each mount sits at, for example
 *        "tag:das2.org,2012:site:/uiowa".  None may be NULL or empty, and
 *        the "site:" shorthand is NOT expanded here, callers such as DasCat
 *        do that first.  Order does not matter, the children are sorted.
 *
 * @param uMounts The number of entries in aUrl and aUri, at least 1 and at
 *        most 64, the size of the sort buffer in node.c.
 *
 * @param bFallThrough If true, paths no mount claims resolve through the
 *        built in federated roots.  If false the mounts are the whole
 *        catalog.
 *
 * @param pMgr Credentials offered when the mounted nodes and their children
 *        are loaded.  May be NULL.  Borrowed.
 *
 * @param sAgent The HTTP user agent string, inherited by children.
 *        Borrowed, usually a string literal.
 *
 * @param pCache Cache settings for the tree, NULL for the process default.
 *        Borrowed.
 *
 * @returns A new memory root whose source URL reports as "memory:mounts",
 *        to be freed with del_RootNode(), or NULL if the arguments are bad
 *        or the document could not be allocated.  Nothing is fetched at
 *        this point; the mounted nodes load on first lookup.
 */
DasNode* _DasNode_newMountRoot(
	const char** aUrl, const char** aUri, size_t uMounts,
	bool bFallThrough, DasCredMngr* pMgr, const char* sAgent,
	const DasNodeCache* pCache
);

#endif /* _das_node_priv_h_ */
