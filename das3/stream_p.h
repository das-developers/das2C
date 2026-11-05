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

/** @file stream_p.h INTERNAL header, subject to change without notice.
 *
 * Nothing here is API, and it is not installed.
 */

#ifndef _das_stream_p_h_
#define _das_stream_p_h_

#include <das3/stream.h>
#include <das3/desc_p.h>

#ifdef __cplusplus
extern "C" {
#endif

#define STREAMDESC_CMP_SZ 48
#define STREAMDESC_VER_SZ 48
#define STREAMDESC_TYPE_SZ 48

struct das_stream {
	/* The base structure */
	DasDesc base;

   /* TODO: Replace this with a sorted array of structures of the type

         {int id, DasDesc* pDesc}

      and use sorted insert and binary search to retrieve packet IDs 
      if greater then say 10.  For 10 or less the "small vector" 
      assumption applies and we just an embedded 10-element array 
      and loops.
   */
	DasDesc* descriptors[DAS2_MAX_PKTIDS];

	/* Common properties */
	char compression[STREAMDESC_CMP_SZ];
   char type[STREAMDESC_TYPE_SZ];
	char version[STREAMDESC_VER_SZ];

	/* Reader opt-in: recover an undecodable embedded format (a mime'd blob with no
	   registered codec extension) as opaque per-record bytes instead of failing.
	   Set from DasIO_embedAsBytes() when the stream descriptor is decoded; the
	   dataset parser reads it via DasStream_getEmbedAsBytes(). */
	bool bEmbedAsBytes;
};


/* Moved from stream.h: library plumbing, not API */

/** Packetized Stream Descriptor Factory Function
 * 
 * @param pBuf A buffer containing string data to decode.
 * 
 * @param pSd - The Stream descriptor, if it exists. May be NULL.
 * 
 * @param nPktId - The packet tag ID number corresponding to this top-level descriptor
 * 
 * @param nModel - The expected data model to parse into, one of 
 *             STREAM_MODEL_MIXED, STREAM_MODEL_V2, or STREAM_MODEL_V3
 * 
 * @returns Either a top level Descriptor object.  The specific type dependings
 *          on the data received, or NULL if the input could not be parsed.
 */
DasDesc* DasDesc_decode(
   DasBuf* pBuf, DasStream* pSd, int nPktId, int nModel
);

/** Encode a DasStream to an version 2 XML string
 * 
 * @param pThis The stream descriptor to encode
 * @param pBuf A DasBuf to receive the bytes
 * @return 0 if encoding succeeded, a non-zero error code otherwise
 * @memberof DasStream
 */
DasErrCode DasStream_encode2(DasStream* pThis, DasBuf* pBuf);

/** Encode a DasStream to an version 3 XML string
 * 
 * @param pThis The stream descriptor to encode
 * @param pBuf A DasBuf to receive the bytes
 * @return 0 if encoding succeeded, a non-zero error code otherwise
 * @memberof DasStream
 */
DasErrCode DasStream_encode3(DasStream* pThis, DasBuf* pBuf);

/** Make a deep copy of a PacketDescriptor on a new stream.
 * This function makes a deep copy of the given packet descriptor and 
 * places it on the provided stream.  Note, packet ID's are not preserved
 * in this copy.  The newly allocated PacketDescriptor may not have the same
 * packet ID as the old one.
 *
 * @param pThis the stream to get the new packet descriptor
 * @param pd The packet descriptor to clone onto the stream
 * @returns The newly created packet descriptor, or NULL if all packet IDs
 *          are in use
 * @memberof DasStream
 */
DasPkt* DasStream_clonePkt(DasStream* pThis, const DasPkt* pd);

/** Detach a packet descriptor from this stream.  
 * 
 * The stream no longer has ownership of the packet (or dataset), in fact 
 * it will no longer have any record of the descriptor of any kind so it
 * will not be deleted when the stream descriptor is deleted.
 * 
 * @param pThis The stream from which the descriptor should be detached.
 * 
 * @param pDesc if not NULL the pointer value will be compared with
 *        stored descriptor pointers.
 * 
 * @param nPktId if not 0, the ID will be compared against stored IDs.
 *        This check is not performed if pDesc is not NULL.
 * 
 * @returns DAS_OKAY on success, an error code if no descriptor with the 
 *        given conditions is attached to this stream.
 * 
 * @memberof DasStream
 */
DasErrCode DasStream_rmDesc(DasStream* pThis, DasDesc* pDesc, int nPktId);

/** Cache or un-cache the shape of every dataset in the stream
 *
 * Calls DasDs_cacheShape() on each dataset, for the usual case where a whole
 * stream has been read into memory (see DasIO_takeStream()) and none of it
 * will change again.  Caching locks nothing, see DasDs_cacheShape().
 *
 * @param pThis The stream, NULL is allowed and does nothing, so the result of
 *        a failed read can be passed straight through.
 *
 * @param bCache true to capture each dataset's current shape, false to go
 *        back to recomputing on demand.
 *
 * @returns DAS_OKAY
 *
 * @memberof DasStream
 */
DasErrCode DasStream_cacheShapes(DasStream* pThis, bool bCache);

DasStream* new_DasStream_str(DasBuf* pBuf, das_stream_model nModel);

#ifdef __cplusplus
}
#endif

#endif /* _das_stream_p_h_ */
