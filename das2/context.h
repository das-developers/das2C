/* Copyright (C) 2024 Chris Piker <chris-piker@uiowa.edu>
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
 * version 2.1 along with Das2C; if not, see <http://www.gnu.org/licenses/>.
 */

/** @file context.h */

#ifndef _context_h_
#define _context_h_

#include <das2/descriptor.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ************************************************************************* */
/* The stream <context> entry: the general form of a stream-scoped
   definitional item.  Frames, reference surfaces, and carried <given>
   elements are all context entries.

   The governing invariant: everything das2C COMPUTES ON is a typed union
   arm below, versioned with the schema.  A generic <given> has no arm, 
   it's cargo is carried by das2C but interpreted.
 */

#define DASCTX_NAME_SZ 64   /* instance name: "TSCS", "WGS84", ...          */
#define DASCTX_KIND_SZ 32   /* "frame", "surface", or a given's type=       */

#define CTX_FRAME   1
#define CTX_SURFACE 2
#define CTX_GIVEN   3       /* carried faithfully, never computed on        */

#define DASCTX_MAX  256     /* entries 1..255; handle 0 stays "unset"       */

/** @addtogroup DM
 * @{
 */

/** A single stream-context entry
 *
 * The base descriptor holds the entry's bare <p> children (inert cargo for
 * every kind); its parent is always NULL so property lookups neither fall
 * through to the stream nor get swept by exporters walking the descriptor
 * tree.
 *
 * @extends DasDesc
 */
typedef struct das_ctx {
	DasDesc base;      /* type=CONTEXT, parent=NULL                        */

	ubyte kind;        /* CTX_FRAME | CTX_SURFACE | CTX_GIVEN              */
	ubyte id;          /* handle == index into DasStream's context array   */

	char sName[DASCTX_NAME_SZ];  /* stream reference token, ALL kinds        */
	char sKind[DASCTX_KIND_SZ];  /* stream element name; a given's type=     */

	void* pUser;       /* application hang point                           */

	/* Extended items don't use the union below and emitt as <given>s      */

	union {            /* typed arms for COMPUTED kinds only               */

		/* Emitts as <frame> */
		struct {
			char    sBody[DASCTX_NAME_SZ];
			int32_t bodyId;     /* SPICE body id after lookup, 0 = unset  */
			bool    bFixed;     /* body-fixed vs body-centered            */
		} frame;

		/* Emitts as <surface> */
		struct {
			char    sBody[DASCTX_NAME_SZ];
			int32_t extId;      /* NAIF-style surface id, 0 = unset       */
		} surface;
	} u;
} DasCtx;

/** @} */

/** Create a context entry on the heap
 *
 * For the known kinds (CTX_FRAME, CTX_SURFACE) sKind is ignored and set
 * from the kind code.  For CTX_GIVEN sKind is required and may not spell
 * a known kind -- a wildcat masquerading as a frame is refused.
 *
 * @param kind one of CTX_FRAME, CTX_SURFACE, CTX_GIVEN
 * @param id the entry's handle, 1 to DASCTX_MAX - 1
 * @param sName the instance name, required for all kinds
 * @param sKind a given's type= token; NULL for known kinds
 * @returns a heap entry the caller owns, or NULL on bad input (das_error set)
 * @memberof DasCtx
 */
DAS_API DasCtx* new_DasCtx(
	ubyte kind, ubyte id, const char* sName, const char* sKind
);

/** Free a context entry allocated by new_DasCtx / copy_DasCtx
 * @memberof DasCtx
 */
DAS_API void del_DasCtx(DasCtx* pThis);

/** Deep-copy a context entry, preserving its handle, typed arm and cargo
 * @returns a heap entry the caller owns, or NULL on error
 * @memberof DasCtx
 */
DAS_API DasCtx* copy_DasCtx(const DasCtx* pThis);

/* Read accessors.  Function-like macros so internals can shift without
   call-site churn; mutation goes through real functions that can validate. */

/** The kind code, one of CTX_FRAME, CTX_SURFACE, CTX_GIVEN
 * @memberof DasCtx
 */
#define DasCtx_kind(P) ((P)->kind)

/** The entry's handle in its stream's context array
 * @memberof DasCtx
 */
#define DasCtx_id(P) ((P)->id)

/** The instance name, the token data-plane items reference
 * @memberof DasCtx
 */
#define DasCtx_name(P) ((const char*)((P)->sName))

/** The printable kind token for an entry ("frame", "surface", or the
 * given's type=)
 * @memberof DasCtx
 */
#define DasCtx_kindStr(P) ((const char*)((P)->sKind))

/** The associated body name; "" for kinds with no body (givens)
 * @memberof DasCtx
 */
#define DasCtx_body(P) ( (const char*) ( \
	((P)->kind == CTX_FRAME)   ? (P)->u.frame.sBody   : \
	((P)->kind == CTX_SURFACE) ? (P)->u.surface.sBody : "" \
))

/** Is this a body-fixed frame?  False for every other kind
 * @memberof DasCtx
 */
#define DasCtx_isFixed(P) (((P)->kind == CTX_FRAME) && (P)->u.frame.bFixed)

/** A surface's external (NAIF-style) id; 0 = unset or not a surface
 * @memberof DasCtx
 */
#define DasCtx_extId(P) (((P)->kind == CTX_SURFACE) ? (P)->u.surface.extId : 0)

/** Print a 1-line summary of a context entry and then its properties
 * @memberof DasCtx
 */
DAS_API char* DasCtx_info(const DasCtx* pThis, char* sBuf, int nLen);

/** Encode a context entry as its stream element (<frame>, <surface> or
 * <given>) with bare <p> children -- context entries ARE property arrays,
 * so their cargo takes no <properties> wrapper.
 * @memberof DasCtx
 */
DAS_API DasErrCode DasCtx_encode(
	const DasCtx* pThis, DasBuf* pBuf, const char* sIndent
);

/** Change an entry's instance name
 * @memberof DasCtx
 */
DAS_API DasErrCode DasCtx_setName(DasCtx* pThis, const char* sName);

/** Change a frame entry's central body name (frames only, fail loud otherwise)
 * @memberof DasCtx
 */
DAS_API DasErrCode DasCtx_setBody(DasCtx* pThis, const char* sBody);

/** Set or clear a frame entry's body-fixed flag (frames only)
 * @memberof DasCtx
 */
DAS_API void DasCtx_setFixed(DasCtx* pThis, bool bFixed);


/* ************************************************************************ 
 * DasCtxTbl: the handle table.
 *
 * The entries and the handle-is-index rule, in one object.  A DasStream
 * embeds one by value and forwards to it, Formalisms (form.h) are handed
 * the table as needed.
 *
 */

typedef struct das_ctx_tbl {

	/* Entry i sits at index i.  Slot 0 is never used: handle 0 means unset */
	DasCtx* lCtx[DASCTX_MAX];
	ubyte   uCtx;              /* highest assigned handle, 0 = none */

} DasCtxTbl;

/** The highest assigned handle; entries run 1..this.  @memberof DasCtxTbl */
#define DasCtxTbl_count(P) ((P)->uCtx)

/** Release every entry and reset the table.  @memberof DasCtxTbl */
DAS_API void DasCtxTbl_clear(DasCtxTbl* pThis);

/** Add an entry, assigning the next free handle.
 *
 * Refuses a duplicate (kind, name) pair; use DasCtxTbl_intern() for
 * get-or-create.
 *
 * @param kind CTX_FRAME, CTX_SURFACE or CTX_GIVEN
 * @param sName the instance name, the wire reference token
 * @param sKind a given's type= token; NULL for the known kinds
 * @returns the new entry, or NULL on error (das_error is set)
 * @memberof DasCtxTbl */
DAS_API DasCtx* DasCtxTbl_add(
	DasCtxTbl* pThis, ubyte kind, const char* sName, const char* sKind
);

/** Fetch by handle, O(1).
 * @returns NULL for handle 0 or an unassigned handle.  @memberof DasCtxTbl */
DAS_API DasCtx* DasCtxTbl_get(const DasCtxTbl* pThis, ubyte id);

/** Fetch by handle IF the entry is of the given kind.
 *
 * The kind-checked twin of DasCtxTbl_get: NULL for an unset handle OR a kind
 * mismatch.  Also serves kind-filtered iteration over 1..DasCtxTbl_count().
 * @memberof DasCtxTbl */
DAS_API DasCtx* DasCtxTbl_getOfKind(
	const DasCtxTbl* pThis, ubyte kind, ubyte id
);

/** Find by kind and instance name, linear; parse-time use.
 * @returns NULL if no entry matches.  @memberof DasCtxTbl */
DAS_API DasCtx* DasCtxTbl_getByName(
	const DasCtxTbl* pThis, ubyte kind, const char* sName
);

/** Get-or-create, the auto-frame pattern generalized.
 *
 * The one call here that MUTATES, which is why it takes a non-const table.
 * @returns the entry's handle, or 0 on error (das_error is set)
 * @memberof DasCtxTbl */
DAS_API ubyte DasCtxTbl_intern(
	DasCtxTbl* pThis, ubyte kind, const char* sName, const char* sKind
);

/** Deep-copy another table's entries, PRESERVING handles.
 *
 * Handles must survive a copy: dataset geovecs and rotations reference
 * entries by id.  Occupied destination slots are left alone, so the first
 * definition wins.  @memberof DasCtxTbl */
DAS_API void DasCtxTbl_copy(DasCtxTbl* pDest, const DasCtxTbl* pSrc);

#ifdef __cplusplus
}
#endif

#endif /* _context_h_ */
