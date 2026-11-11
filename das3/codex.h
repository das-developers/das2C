/* Copyright (C) 2026 Chris Piker <chris-piker@uiowa.edu>
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
 *  blobs.  Distinct from codec.h: codec = builtin packet encodings; codex =
 *  optional handlers for foreign formats embedded in a stream. */

#ifndef _das_codex_h_
#define _das_codex_h_

#include <stdbool.h>
#include <stddef.h>

#include <das3/value.h>
#include <das3/array.h>
#include <das3/buffer.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Integration sketch -- what this implies elsewhere.  All of it is deferred to
 * the first real codec (png, v3.1); for v3.0 the functions below are do-nothing
 * stubs so an undecodable blob fails loud with a named error:
 *
 *   codec_p.h: DasCodec gains  `DasCodex* pCodex;`  (NULL = builtin only)
 *   codec.c  : init_DasCodec   -> new_DasCodex()   when a mime is present + supported
 *              fini_DasCodec -> del_DasCodex(pThis->pCodex)
 *              DasCodec_decode -> DasCodex_decode() for the blob when set
 *              DasCodec_encode -> DasCodex_encode() per record when set
 *   datset_3.c : parse <packet mime="...">, and on encoding="blob" + mime +
 *              !DasCodex_isSupported(mime) fail loud -- the message now in
 *              var_ary.c moves to this decode site, where the mime is in hand.
 */

/* ------------------------------------------------------------------------- *
 * Registry lifecycle  --  DEFERRED to the first real codec (png, v3.1)
 *
 * Nothing can be registered in v3.0, so the registration/seal/unload API is
 * commented out below, kept for the design record.  When it lands, the intended
 * shape is:  daslib_init() -> das_codex_reg()xN -> das_codices_ready().  The map
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

/* Note: That sMime may have hints, a la: "image/png; gamma=2.2; interlace=adam7" */
/* DAS_API das_errcode  das_codex_reg(const char* sMime, const char* sFile); */
/* DAS_API das_errcode  das_codices_ready(void); */
/* DAS_API void        das_codices_unload(void); */

/** @addtogroup codec
 * @{
 */

/* Active v3.0 stubs -- always report "nothing registered" so an undecodable blob
 * fails loud at the reader (dataset_hdr3 / var_ary). */

/** True if a codec for this mime is registered and loaded.  v3.0: always false. */
DAS_API bool DasCodex_isSupported(const char* sMime);

/** The absolute path registered for a mime, or NULL.  v3.0: always NULL. */
DAS_API const char* DasCodex_regPath(const char* sMime);

/* ------------------------------------------------------------------------- *
 * Per-variable codec instance
 * ------------------------------------------------------------------------- */

/** @class DasCodex codex.h <das3/codex.h>
 *
 * One decode/encode context for an embedded format, made by new_DasCodex()
 * and owned by the DasCodec that made it.  Private to a single codec and
 * thread, so it may carry mutable decode and encode scratch.  Opaque; the
 * interface a codec plugin implements arrives with codec registration.
 */
typedef struct das_codex_t DasCodex;

/** Make a codec extension (codex) instance for a given mime type to a
 * specific output type.
 * 
 * @param sMime the mime type in question, for example 'image/png'.
 *   note that sMime can carry parameters.  These can be used to 
 *   configure a loaded codex, but should never be used to select
 *   paths, never allocate unbounded, and a codex that ignores the
 *   completely still works. Thes must follows the RFC-2045 section 
 *   5.1 'media-type parameters' standard.  An example:
 *       "image/png; gamma=2.2; interlace=adam7"
 * 
 * @param vtStore a declared value type. For example grayscale pixels
 *   may be output as bytes, shorts, floats, etc. this nails it down.
 *   Use DASVT_UNK to let the codex select one.
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
	const char* sMime, das_vt vtStore, int nRank, const ptrdiff_t* pShape
);

/** Get the value type for individual items read from or written to blobs.
 * @memberof DasCodex */
DAS_API das_vt DasCodex_storage(const DasCodex* pThis);

/** Write this instance's plugin .so file path into a buffer, for diagnostics.
 * @memberof DasCodex */
DAS_API das_errcode DasCodex_path(const DasCodex* pThis, char* sBuf, size_t uLen);

/** Write this instance's mime/content-type into a buffer, e.g. for re-emit.
 * @memberof DasCodex */
DAS_API das_errcode DasCodex_mime(const DasCodex* pThis, char* sBuf, size_t uLen);

/** Write the codec's own properties (image dims, colorspace, ...) into a
 * dense rank-2 ubyte array in DasDesc's packed layout
 *
 * @param pThis the codec instance
 * @param pOut receives the properties
 * @param pPropsOut receives the number of properties written
 * @memberof DasCodex */
DAS_API das_errcode DasCodex_descProps(
	const DasCodex* pThis, DasAry* pOut, size_t* pPropsOut
);

/** Decode one embedded blob, appending its samples to an array
 *
 * @param pThis the codec instance
 * @param pBlob the blob bytes
 * @param uLen the blob length in bytes
 * @param pAry receives the decoded samples
 * @param pAppended receives the number of items written
 * @memberof DasCodex */
DAS_API das_errcode DasCodex_decode(
	DasCodex* pThis, const uint8_t* pBlob, size_t uLen, DasAry* pAry,
	size_t* pAppended
);

/** Encode one record back into an embedded blob byte-run
 *
 * The caller guarantees pAddr subsumes enough items to form one object.
 *
 * @param pThis the codec instance
 * @param pItems the array holding the record's items
 * @param pAddr the location of the record in pItems
 * @param pBuf receives the blob bytes
 * @param pAppended receives the number of bytes appended to pBuf
 * @memberof DasCodex */
DAS_API das_errcode DasCodex_encode(
	DasCodex* pThis, const DasAry* pItems, const ptrdiff_t* pAddr,
	DasBuf* pBuf, size_t* pAppended
);

/** Free a codec instance and its internal state, NULL is ignored
 * @memberof DasCodex */
DAS_API void del_DasCodex(DasCodex* pThis);

/** @} */

#ifdef __cplusplus
}
#endif

#endif /* _das_codex_h_ */
