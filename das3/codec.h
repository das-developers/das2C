/* Copyright (C) 2024  Chris Piker <chris-piker@uiowa.edu>
 *
 * This file is part of das2C, the Core Das2 C Library.
 * 
 * das2C is free software; you can redistribute it and/or modify it under
 * the terms of the GNU Lesser General Public License version 2.1 as published
 * by the Free Software Foundation.
 *
 * das2C is distributed in the hope that it will be useful, but WITHOUT ANY
 * WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS
 * FOR A PARTICULAR PURPOSE.  See the GNU Lesser General Public License for
 * more details.
 *
 * You should have received a copy of the GNU Lesser General Public License
 * version 2.1 along with das2C; if not, see <http://www.gnu.org/licenses/>. 
 */

/** @file codec.h Encoding/Decoding arrays to and from buffers */

#ifndef _das_codec_h_
#define _das_codec_h_

#include <stdint.h>

#include <das3/value.h>
#include <das3/array.h>
#include <das3/buffer.h>


#ifdef __cplusplus
extern "C" {
#endif


/** @addtogroup IO
 * @{
 */


/** Reading and writing array data to buffers
 *
 * Codecs live inside a dataset.  Get one from DasDs_getCodec(),
 * DasDs_getCodecFor() or the DasDs_add*Codec() calls; the dataset owns it.
 * Change one with DasDs_updateCodec(), which keeps the codec's array inside
 * its dataset.
 */
typedef struct das_codec DasCodec;

#define DASENC_READ  true
#define DASENC_WRITE false

#define DASENC_ITEM_TERM -9  /* Packet items are terminated by a special byte sequence 
                               aka: "Hi There!|Go Away.|" */

#define DASENC_ITEM_LEN -1 /* Packet items have explicit byte lengths in packets
                              aka:  "|9|Hi There!|8|Go Away." */







/** Attach the per-index run terminators after codec creation.
 *
 * The value terminator (valTerm) is set when the codec is created and lives at
 * sSepSet[0]; this call fills sSepSet[1..] outer-most first and updates nSep.
 * The list is all-or-nothing: either one terminator per external RAGGED index,
 * or one per EVERY external index down to the inner-most ragged one (the
 * decorated form, where fixed extents inside the run structure get a formally
 * redundant terminator for readability).
 * 
 * Since internal/external is a DasVar concept and not an encoding concept
 * the caller has to set this up.
 *
 * @param pThis the codec to configure
 * @param nLevels the number of run-terminator levels (1..VARIDX_MAX-1)
 * @param sLevels the level bytes, outer-most first, nLevels long
 * @returns DAS_OKAY, or a das error code on a bad or colliding terminator
 *
 * @memberof DasCodec
 */
DAS_API DasErrCode DasCodec_setIdxTerms(DasCodec* pThis, ubyte nLevels, const char* sLevels);

/** Is this codec setup and a reader from external buffers or a write to them ?
 *
 * @memberof DasCodec
 */
DAS_API bool DasCodec_isReader(const DasCodec* pThis);

/** Does this codec emit (or read) text, i.e. does it use utf8 terminator framing?
 * True for encoding="utf8" and for binary values re-encoded as text; false for
 * intrinsic binary and for {N}-framed blob/base64.  The ragged run terminators
 * (idxTerm) apply only when this is true.
 *
 * @memberof DasCodec
 */
DAS_API bool DasCodec_isText(const DasCodec* pThis);

/** The encoding of values in the external buffer
 *
 * @returns one of the canonical DAS_ENC_* strings from value.h, for example
 *        "utf8" or "LEreal", the same token as the <packet encoding="...">
 *        attribute, or NULL for an uninitialized codec
 * @memberof DasCodec
 */
DAS_API const char* DasCodec_encoding(const DasCodec* pThis);

/** The intended meaning of the values, which steers text parsing
 *
 * @returns one of the canonical DAS_SEM_* strings, for example "real" or
 *        "datetime", or NULL for an uninitialized codec
 * @memberof DasCodec
 */
DAS_API const char* DasCodec_semantic(const DasCodec* pThis);

/** The width of one value in the external buffer
 *
 * @returns the byte count, the same as <packet itemBytes="...">, or a
 *        negative DASENC_ITEM_* flag for values whose width is set in the
 *        packet
 * @memberof DasCodec
 */
DAS_API int DasCodec_itemBytes(const DasCodec* pThis);

/** The array this codec fills on read or empties on write
 *
 * @returns the array, owned by the codec's dataset
 * @memberof DasCodec
 */
DAS_API DasAry* DasCodec_getAry(const DasCodec* pThis);

/** The number of external ragged indices whose runs this codec closes
 *
 * Zero for a codec with a fixed item count.  When it is non-zero,
 * DasCodec_raggedIndices() says which indices they are.
 * @memberof DasCodec
 */
DAS_API int DasCodec_numRagged(const DasCodec* pThis);

/** Locate the external ragged indices this codec's runs close.
 *
 * Fills aRagIdx[0..N-1] (N = DasCodec_numRagged()) with the position of each external
 * ragged index ('j', 'k', ...) in the backing array's index order, outer-most
 * first.
 *
 * @param pThis the codec; DasCodec_numRagged() must not be 0
 * @param aRagIdx receives the index positions; must hold VARIDX_MAX entries
 * @returns the number of ragged levels (>= 1), or a negative das error code
 * @memberof DasCodec
 */
DAS_API int DasCodec_raggedIndices(const DasCodec* pThis, int* aRagIdx);

#ifdef __cplusplus
}
#endif


#endif /* _das_codec_h_ */
