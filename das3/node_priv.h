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
 * Loading.  Every node begins as a JSON document that _dasnode_fromJson()
 * turns into a struct of the right type.  Where the bytes come from is
 * not node.c's business: catalog.c reads files, the cache and HTTP, a
 * browser build hands them to new_DasNode_json().  The parsed DasJdo is
 * kept in pDom for the life of the node and the accessors read from it
 * rather than copying fields out.  A DasNodeWalk resolves a path below a
 * container by matching it against the names in the container dictionary
 * and asking its driver for the matching child's URL; a child that arrives
 * is remembered in the parent's pSubNodes array once the walk succeeds,
 * so a second walk of the same path needs no document.
 *
 * Ownership.  Trees are owned from the top.  A node with bOwner set was
 * handed to a caller by a constructor: it owns everything loaded below it
 * and only del_DasNode() frees it.  Callers never free a child node, it
 * belongs to its parent's pSubNodes array.  An owner need not be the top
 * of any catalog: a seed loaded from a file, or a node a program kept
 * after dropping the tree it was found in, since _DasNode_delTree() skips
 * owners.  An application holding site:/uiowa/juno/wav thus owns a one
 * node tree with no parent in memory.
 *
 * Transport.  None.  A node holds no credentials, agent string or cache
 * settings; the session that loads nodes, see catalog.c, keeps those and
 * drives a DasNodeWalk with them.
 */

struct das_node {
	das_node_type_e nType;
	char sURL[512];        /* where this node was loaded from */
	char sPath[512];       /* the node's catalog path URI, may be empty */
	bool bOwner;           /* handed out by a constructor: owns the tree below
	                          it, freed only by del_DasNode() */
	void* pDom;            /* the parsed document, a DasJdo, current layout */
	void* pShim;           /* elements added on load that the parser did not
	                          allocate, see _dasnode_upgrade().  May be NULL */
};

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



/* Build a node from JSON text, see new_DasNode_json() for the public
 * form.  sUrl is where the text came from and sFinalUrl the same after
 * redirects, both for messages.  sPathUri may be NULL.  The node is not
 * marked as owned, the caller decides that. */
DasNode* _dasnode_fromJson(
	const char* pBytes, size_t uBytes, const char* sUrl, const char* sFinalUrl,
	const char* sPathUri
);

/* True if some child listed by pBase could be the next step of sSubPath,
 * which starts with the separator pBase uses.  Nothing is loaded. */
bool _DasNode_subMatch(DasNode* pBase, const char* sSubPathWithSep);

/* Frees a node and everything below it, except nodes a constructor handed
 * to a caller: those are freed only by del_DasNode().  A child lives in
 * its parent's pSubNodes cache, freeing one alone would leave a dangling
 * entry there. */
void _DasNode_delTree(DasNode* pThis);

/* Catalog URI spellings.
 *
 * Names are composed top down: the das catalog root node has no separator, so
 * its child is "tag:das2.org,2012:site", and that node's separator
 * ":/" makes "tag:das2.org,2012:site:/uiowa".  A root has no URI, so its
 * children are named by their bare names and the levels below by the
 * path from the root.  The shorthands "site:" and "test:" follow the
 * composition rule.  Alone, one names the node:
 * "site:" is "tag:das2.org,2012:site".  With a path after it, it stands
 * for the node and its separator, and a '/' is supplied or collapsed so
 * that "site:uiowa" and "site:/uiowa" name one node and stamp one
 * requisition.  Nothing else is normalized: a full URI is copied as
 * written, since separators inside a path belong to the catalog nodes
 * and may not be '/'.  Returns sOut, empty for a NULL input. */
const char* _das_uri_expand(const char* sIn, char* sOut, size_t uOut);

/* The mount table: a synthetic Catalog node whose children are other
 * nodes placed at chosen catalog URIs.  This is the machinery behind
 * DasCat_mount() and DasCat_setSeed().
 *
 * It works the way the das catalog index does.  That node has
 * "separator":null, so its children are named by whole URI prefixes such
 * as "tag:das2.org,2012:" and a path cannot be split on a separator to
 * find one.  Instead the walk goes through the children in document order
 * and takes the FIRST child whose name is a prefix of the requested path,
 * then resolves the rest of the path below that child.
 *
 * A mount table is a small JSON document of that shape, written in memory
 * and parsed like any downloaded node.  Each child is named by the full
 * URI of a mount and its urls list holds the file or URL to load there.
 * For the two mounts (./uiowa.json at site:/uiowa) and (./wav.json at
 * site:/uiowa/juno/wav) the document is in spirit:
 *
 *   { "type":"Catalog", "separator":null, "catalog":{
 *       "tag:das2.org,2012:site:/uiowa/juno/wav": {"urls":["./wav.json"]},
 *       "tag:das2.org,2012:site:/uiowa":          {"urls":["./uiowa.json"]}
 *   }}
 *
 * Resolving site:/uiowa/juno/wav/survey matches the wav child, loads
 * wav.json and looks up "/survey" inside it.  site:/uiowa/cassini skips
 * wav and matches uiowa.  Ordering matters because the first match wins,
 * not the best match.  If uiowa were listed before wav it would also
 * claim every wav path and wav.json would never be consulted.  Hence the
 * children are written longest URI first, so a more specific mount
 * shadows a broader one.  Whether a URI no mount covers goes on to the
 * federated catalog is the session's decision, made before the table is
 * consulted at all.
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
 * @returns A new mount table, owned by the caller, whose source URL reports
 *        as "memory:mounts", to be freed with del_DasNode(), or NULL if the
 *        arguments are bad
 *        or the document could not be allocated.  Nothing is fetched at
 *        this point; the mounted nodes load on first lookup.
 */
DasNode* _dasnode_newMountTable(const char** aUrl, const char** aUri, size_t uMounts);

#endif /* _das_node_priv_h_ */
