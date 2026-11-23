/* Copyright (C) 2026 Chris Piker <chris-piker@uiowa.edu>
 *
 * This file is part of das2C, the Core Das C Library.
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

#ifndef _das_node_p_h_
#define _das_node_p_h_

#include "node.h"

/* How a node tree works, for readers of node.c and catalog.c.
 *
 * Derived types.  struct das_node_t is the common header of every node.
 * Nodes that have children (Catalog, Collection, SourceSet and the SPASE
 * and SPDF trees) are really a DasCatNode, defined in node.c, whose first
 * member is a DasNode so that either can be handled through a DasNode*.
 * The type is checked before casting, see DasNode_isCatalog().  Terminal
 * nodes (stream sources, file aggregations) are a bare struct das_node_t.
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

struct das_node_t {
	das_nodetype nType;
	char sURL[512];        /* where this node was loaded from */
	char sPath[512];       /* the node's catalog path URI, may be empty */
	bool bOwner;           /* handed out by a constructor: owns the tree below
	                          it, freed only by del_DasNode() */
	void* pDom;            /* the parsed document, a DasJdo, current layout */
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


#endif /* _das_node_p_h_ */
