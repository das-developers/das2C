/* Copyright (C) 2026   Chris Piker <chris-piker@uiowa.edu>
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

/* Written by: Claude Opus 4.8 (Anthropic).  Anthropic makes no warranty as to
 * this file's fitness for any purpose; accountability for its inclusion and use
 * rests with the repository author.
 */

/** @file codex.h  CODec EXtensions.  Decode/encode embedded formats (png, gzip,
 *  jpeg, ...) that das2C's builtin codec (codec.h) hands through as opaque
 *  blobs.  Distinct from codec.h: codec = builtin wire encodings; codex =
 *  optional handlers for foreign formats embedded in a stream. */

#ifndef _das_codex_h_
#define _das_codex_h_

#include <stdbool.h>
#include <stddef.h>

#include <das2/value.h>
#include <das2/array.h>
#include <das2/buffer.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Integration sketch -- what this implies elsewhere.  All of it is deferred to
 * the first real codec (png, v3.1); for v3.0 the functions below are do-nothing
 * stubs so an undecodable blob fails loud with a named error:
 *
 *   codec.h  : DasCodec gains  `DasCodex* pCodex;`  (NULL = builtin only)
 *   codec.c  : DasCodec_init   -> new_DasCodex()   when a mime is present + supported
 *              DasCodec_deInit -> del_DasCodex(pThis->pCodex)
 *              DasCodec_decode -> DasCodex_decode() for the blob when set
 *              DasCodec_encode -> DasCodex_encode() per record when set
 *   dataset_hdr3.c : parse <packet mime="...">, and on encoding="blob" + mime +
 *              !das_codex_supported(mime) fail loud -- the message now in
 *              var_ary.c moves to this decode site, where the mime is in hand.
 */

/* ------------------------------------------------------------------------- *
 * Registry lifecycle  --  DEFERRED to the first real codec (png, v3.1)
 *
 * Nothing can be registered in v3.0, so the registration/seal/unload API is
 * commented out below, kept for the design record.  When it lands, the intended
 * shape is:  das_init() -> das_codex_reg()xN -> das_codices_ready().  The map
 * seals at ready() and is immutable after (so post-ready lookups need no lock),
 * and new_DasCodex succeeds only post-seal.  Apps that use no codecs never call
 * any of it.
 *
 * SECURITY (future contract): the registered sFile MUST be an application-
 * controlled absolute path and must NEVER derive from stream content -- a stream
 * that can name the .so to load is a code-execution hole.  das_codex_reg will
 * reject non-absolute paths, and the seal means nothing a stream triggers can
 * register a codec mid-read; but the library cannot verify a path's provenance,
 * so that stays the caller's contract.
 * ------------------------------------------------------------------------- */

/* DAS_API DasErrCode  das_codex_reg(const char* sMime, const char* sFile); */
/* DAS_API DasErrCode  das_codices_ready(void); */
/* DAS_API void        das_codices_unload(void); */

/* Active v3.0 stubs -- always report "nothing registered" so an undecodable blob
 * fails loud at the reader (dataset_hdr3 / var_ary). */

/** True if a codec for this mime is registered and loaded.  v3.0: always false. */
DAS_API bool das_codex_supported(const char* sMime);

/** The absolute path registered for a mime, or NULL.  v3.0: always NULL. */
DAS_API const char* das_codex_path(const char* sMime);

/* ------------------------------------------------------------------------- *
 * Per-variable codec instance
 *
 * Reach every member through the DasCodex_* / del_DasCodex macros below, never
 * `pThis->member(...)`.  The macros are the public API and keep call sites
 * reading like ordinary das2C functions (same idiom as DasVar_get, etc.).
 * ------------------------------------------------------------------------- */

/** One decode/encode context, made by new_DasCodex and owned by the DasCodec
 *  that made it.  Private to a single codec/thread, so it may carry mutable
 *  decode/encode scratch. */
typedef struct das_codex {

	void* pState;   /* instance-private state, freed by del */

	/* Introspection (const). */
	das_val_type (*storage)(const struct das_codex* pThis);
	DasErrCode   (*path)(const struct das_codex* pThis, char* sBuf, size_t uLen);
	DasErrCode   (*mime)(const struct das_codex* pThis, char* sBuf, size_t uLen);

	/* Write the codec's own properties (e.g. image dims, colorspace) into a
	   dense rank-2 ubyte array in DasDesc's packed layout; *pPropsOut = count. */
	DasErrCode (*properties)(
		const struct das_codex* pThis, DasAry* pOut, size_t* pPropsOut
	);

	/* One blob -> samples appended to pAry; *pAppended = items written. */
	DasErrCode (*decode)(
		struct das_codex* pThis, const ubyte* pBlob, size_t uLen,
		DasAry* pAry, size_t* pAppended
	);

	/* One record (at pLoc) -> blob bytes appended to pBuf; *pAppended = bytes.
	   Caller guarantees pLoc subsumes enough items to form one object. */
	DasErrCode (*encode)(
		struct das_codex* pThis, const DasAry* pItems, const ptrdiff_t* pLoc,
		DasBuf* pBuf, size_t* pAppended
	);

	void (*del)(struct das_codex* pThis);  /* free pState + the instance */

} DasCodex;

/** Make a codec extension (codex) instance for a given mime type to a
 * specific output type.
 * 
 * @param sMime the mime type in question, for example 'image/png'
 * 
 * @param vtStore a declared value type. For example grayscale pixels
 *   may be output as bytes, shorts, floats, etc. this nails it down.
 *   Use vtUnknown to let the codex select one.
 * 
 * @param nRank the number of index dimensions in the full stream output
 *   this is each blob's shape plus an over all stream index.
 * 
 * @param pShape the expected output storage shape in index space.
 * 
 * @returns  Returns NULL if the mime is unsupported. There is no internal
 *   registry of Codex instances, the caller must deallocate via del_DasCodex()
 * 
 * @see das_codex_reg() and das_codices_ready() which are not yet implemented.
 * 
 * @memberof DasCodex
 */
DAS_API DasCodex* new_DasCodex(
	const char* sMime, das_val_type vtStore, int nRank, const ptrdiff_t* pShape
);

/* Member access -- the public surface.  Arguments are evaluated more than once
 * (as with the other das2C accessor macros); pass plain pointers. */


/** Get the value type for individual items read from or written to blobs.
 * @memberof DasCodex
 */
#define DasCodex_storage(p)                 ((p)->storage(p))
/** Write this instance's plugin .so file path into a buffer, for diagnostics.
 * @memberof DasCodex
 */
#define DasCodex_path(p, sBuf, uLen)        ((p)->path((p), (sBuf), (uLen)))

/** Write this instance's mime/content-type into a buffer, e.g. for re-emit.
 * @memberof DasCodex
 */
#define DasCodex_mime(p, sBuf, uLen)        ((p)->mime((p), (sBuf), (uLen)))

/** Append the codec's own properties (image dims, colorspace, ...) to an array.
 * @memberof DasCodex
 */
#define DasCodex_properties(p, pOut, pN)    ((p)->properties((p), (pOut), (pN)))

/** Decode one embedded blob, appending its samples to an array.
 * @memberof DasCodex
 */
#define DasCodex_decode(p, pBlob, uLen, pAry, pN) \
	((p)->decode((p), (pBlob), (uLen), (pAry), (pN)))

/** Encode one record back into an embedded blob byte-run.
 * @memberof DasCodex
 */
#define DasCodex_encode(p, pItems, pLoc, pBuf, pN) \
	((p)->encode((p), (pItems), (pLoc), (pBuf), (pN)))

/** Free a codec instance and its internal state.
 * @memberof DasCodex
 */
#define del_DasCodex(p)                     ((p)->del(p))

#ifdef __cplusplus
}
#endif

#endif /* _das_codex_h_ */
