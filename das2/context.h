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

#ifndef _frame_h_
#define _frame_h_

#include <das2/descriptor.h>

#ifdef __cplusplus
extern "C" {
#endif

#define DASFRM_NAME_SZ  64
#define DASFRM_CNAME_SZ 12
#define DASFRM_BODY_SZ  64  /* Direction name size */
#define DASFRM_FIXED 0x00000010
#define DASFRM_NULLNAME ""

/** @addtogroup DM 
 * @{
 */

/** Stores the definitions of a coordinate frame
 * 
 * These are little more then a basic definition to allow new das3 vector
 * objects to be manipulated in a somewhat reasonable manner.  Two vectors that 
 * have the same frame can be subject to cross-products and other useful
 * manipulations.  If the do not share a coordinate system then some out-of-band
 * transformation will be needed.
 */
typedef struct frame_descriptor{

	/** The base class 
	 * A common property to store is the suffexes for the principle coordinate
	 * axes,  For eample in the East, North, Up system these would be "E","N","U" 
	 */
	DasDesc base;

	/* Required properties */
	ubyte id;  /* The frame ID, used in vectors, quaternions etc. */
	           /* WARNING: If this is changed to something bigger, like a ushort,
	                       go remove the double loop from DasStream_getFrameId! */

	char name[DASFRM_NAME_SZ];
	char body[DASFRM_NAME_SZ];

	int32_t bodyId;  /* A place to store the spice body ID after lookup, 0 = unset */
	uint32_t flags;  /* Usually contains the type */

	/** User data pointer
	 * 
	 * The stream -> frame  hierarchy provides a goood organizational structure
	 * for application data, especially applications that filter streams.  It
	 * is initialized to NULL when a variable is created but otherwise the
	 * library dosen't deal with it.
	 */
	void* pUser;

} DasFrame;

/** @} */

/** Create a new empty frame definition
 * @param pParent
 *
 * @param id The internal stream ID used to tag geovectors (das_geovec) in this
 *        frame.  Has no external meeting. Must be in the range 1 to 255
 *        inclusive.
 *
 * @param sName The name of the frame.  Stream creators are encouraged to
 *        use external name systems for this, such as SPICE.
 *
 * @returns A bare frame, carrying only a name.  The name is a dictionary key
 *        into an external system (typically SPICE) which already holds the body
 *        definition, so a name alone is all most readers need.  Streams that
 *        want to pin down the central body or fix the frame to it must declare
 *        a full <frame> section; see DasFrame_setBody() and DasFrame_fixed().
 *
 * @memberof DasFrame
 */
DAS_API DasFrame* new_DasFrame(DasDesc* pParent, ubyte id, const char* sName);

/** Create a deepcopy of a DasFrame descriptor and all it's properties */
DAS_API DasFrame* copy_DasFrame(const DasFrame* pThis);

/** Print a 1-line summary of a frame and then it's properties 
 * 
 * @memberof DasFrame
 */
DAS_API char* DasFrame_info(const DasFrame* pThis, char* sBuf, int nLen);

/** Change the frame name 
 * @memberof DasFrame
 */
DAS_API DasErrCode DasFrame_setName(DasFrame* pThis, const char* sName);

/** Change the frame central body name
 * @memberof DasFrame
 */
DAS_API DasErrCode DasFrame_setBody(DasFrame* pThis, const char* sBody);

/** Get the internal (stream only) ID of a frame
 * 
 * @memberof DasFrame
 */
#define DasFrame_id(p) ((p)->id)


DAS_API void DasFrame_fixed(DasFrame* pThis, bool bFixed);

#define DasFrame_isFixed(P) (P->flags & DASFRM_FIXED)

/** Get the frame name
 * @memberof DasFrame
 */
#define DasFrame_getName(P) (P->name)

/** Get the central body for the frame
 * @memberof DasFrame
 */
#define DasFrame_getBody(P) ((const char*)(P->body))

/** Encode a frame definition into a buffer
 * 
 * @param pThis The vector frame to encode
 * @param pBuf A buffer object to receive the XML data
 * @param sIndent An indent level for the frame
 * @param nDasVer expects 3 or higher
 * @return 0 if the operation succeeded, a non-zero return code otherwise.
 * @memberof DasDesc
 */
DAS_API DasErrCode DasFrame_encode(
   const DasFrame* pThis, DasBuf* pBuf, const char* sIndent, int nDasVer
);


/** Free a frame definition that was allocated on the heap
 *
 * @memberof DasFrame
 */
DAS_API void del_DasFrame(DasFrame* pThis);

/* ************************************************************************* */
/* The stream <context> entry: the general form of a stream-scoped
   definitional item.  Frames, reference surfaces, and carried <given>
   elements are all context entries.  DasFrame above is the legacy face of
   the frame kind and will become a facade over its entry.

   The governing invariant: everything das2C COMPUTES ON is a typed union
   arm below, versioned with the schema.  A <given> has no arm -- the C
   type system enforces that its cargo is carried, never interpreted. */

#define DASCTX_NAME_SZ 64   /* instance name: "TSCS", "WGS84", ...          */
#define DASCTX_KIND_SZ 32   /* "frame", "surface", or a given's type=       */

#define CTX_FRAME   1
#define CTX_SURFACE 2
#define CTX_GIVEN   3       /* carried faithfully, never computed on        */

#define DASCTX_MAX  256     /* entries 1..255; handle 0 stays "unset"       */

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

	char sName[DASCTX_NAME_SZ];  /* wire reference token, ALL kinds        */
	char sKind[DASCTX_KIND_SZ];  /* wire element name; a given's type=     */

	void* pUser;       /* application hang point, as DasFrame.pUser        */

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

#ifdef __cplusplus
}
#endif

#endif /* _frame_h_ */
