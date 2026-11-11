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
#include <das3/io.h>      /* das_stream_model */

#ifdef __cplusplus
extern "C" {
#endif

#define STREAMDESC_CMP_SZ 48
#define STREAMDESC_VER_SZ 48
#define STREAMDESC_TYPE_SZ 48

/* Child descriptors kept inline before the lists move to the heap.  Streams
   rarely have more than 10 packet IDs. */
#define DASSTREAM_LOC_DESC 16

struct das_stream {
	/* The base structure */
	DasDesc base;

	/* Attached descriptors in attach order, with each one's packet ID in the
	   parallel aIds list.  The IDs live here rather than in the descriptors
	   so a lookup scans one small array instead of touching every
	   descriptor's heap block.  Several datasets may share an ID; the last
	   one in the list is the active one, so lookups scan backwards.

	   pDescHeap/pIdHeap are NULL until the inline arrays fill, use the
	   _DasStream_descs()/_DasStream_ids() accessors in stream.c.  No pointer
	   into the struct itself is kept, so a DasStream stays safe to move. */
	size_t uDesc;
	size_t uDescCap;     /* 0 means the inline arrays */
	DasDesc** pDescHeap;
	uint16_t* pIdHeap;
	DasDesc* aDescLoc[DASSTREAM_LOC_DESC];
	uint16_t aIdLoc[DASSTREAM_LOC_DESC];

	/* One past the index of the last active descriptor DasStream_findDesc()
	   returned, 0 when unset (so a calloc'd stream starts unset).  Any attach
	   or removal resets it, which is what keeps a remembered hit active. */
	size_t uLastHit;

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

/* True if embedded formats with no registered codec are kept as opaque
   bytes instead of failing.  May change the shape of the variables it
   applies to.  The option is set on DasIO (DasIO_embedAsBytes), copied in
   when the stream is created. */
bool DasStream_getEmbedAsBytes(const DasStream* pThis);

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
das_errcode DasStream_encode2(DasStream* pThis, DasBuf* pBuf);

/** Encode a DasStream to an version 3 XML string
 * 
 * @param pThis The stream descriptor to encode
 * @param pBuf A DasBuf to receive the bytes
 * @return 0 if encoding succeeded, a non-zero error code otherwise
 * @memberof DasStream
 */
das_errcode DasStream_encode3(DasStream* pThis, DasBuf* pBuf);

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
 * @param pDesc The descriptor to detach, compared against the stored
 *        pointers before anything reads it.
 *
 * @returns DAS_OKAY on success, an error code if pDesc is not attached to
 *        this stream.
 *
 * @memberof DasStream
 */
das_errcode DasStream_rmDesc(DasStream* pThis, DasDesc* pDesc);

/** The active descriptor for a packet ID, for DasIO's per-packet lookup
 *
 * Same answer as DasStream_getDesc(), but remembers the last hit so a run
 * of packets with one ID costs one compare each, and logs nothing for an
 * out of range ID (the framing parser has already checked it).
 *
 * @returns The active descriptor or NULL.
 * @memberof DasStream
 */
DasDesc* DasStream_findDesc(DasStream* pThis, int nPktId);

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
das_errcode DasStream_cacheShapes(DasStream* pThis, bool bCache);

DasStream* new_DasStream_str(DasBuf* pBuf, das_stream_model nModel);

#ifdef __cplusplus
}
#endif

#endif /* _das_stream_p_h_ */
