/* Copyright (C) 2004-2017 Jeremy Faden <jeremy-faden@uiowa.edu>
 *                         Chris Piker <chris-piker@uiowa.edu>
 *
 * This file is part of libdas2, the Core Das2 C Library.
 * 
 * Libdas2 is free software; you can redistribute it and/or modify it under
 * the terms of the GNU Lesser General Public License version 2.1 as published
 * by the Free Software Foundation.
 *
 * Libdas2 is distributed in the hope that it will be useful, but WITHOUT ANY
 * WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS
 * FOR A PARTICULAR PURPOSE.  See the GNU Lesser General Public License for
 * more details.
 *
 * You should have received a copy of the GNU Lesser General Public License
 * version 2.1 along with libdas2; if not, see <http://www.gnu.org/licenses/>. 
 */

/** @file stream.h Objects representing a Das2 Stream as a whole */

#ifndef _das_stream_h_
#define _das_stream_h_

#include <stdbool.h>
#include <das2/packet.h>
#include <das2/dataset.h>

#ifdef __cplusplus
extern "C" {
#endif

#define STREAM_MODEL_MIXED -1
#define STREAM_MODEL_V2     2
#define STREAM_MODEL_V3     3

#define STREAMDESC_CMP_SZ 48
#define STREAMDESC_VER_SZ 48
#define STREAMDESC_TYPE_SZ 48

#define MAX_PKTIDS 100
#define MAX_FRAMES 12

/** @defgroup streams Streams 
 * Classes for handling interleaved self-describing data streams
 */

/** Describes the stream itself, in particular the compression used, 
 * current packetDescriptors, etc.
 * 
 * This is a container for top-level stream descriptor objects.  The
 * data owner ship model for das2C is:
 * 
 * StreamDesc -> PktDesc -> PlaneDesc -> 1-row of data
 *            -> DasDs -> DasAry -> arbitrary rows of data
 *                     -> DasDim -> DasVar (Structure access to DasDs Arrays)
 *
 * Anything not owned by StreamDesc is considered OOB (Out Of Band) data.
 * 
 * All top level descriptors my be accessed by an integer ID.  There is one
 * ID space for all desciptors, not a separate one for datasets (das3) 
 * versus packets (das2)
 * 
 * ID 0 is reserved for the stream descriptor itself.
 * 
 * Desciptor IDs:
 *   The lookup ID is the same value used as the header & data IDs in the
 *   stream. The legal packet ID range depends on the stream serilazation
 *   method.  For the das2 format, the valid range is 0 to 99.
 * 
 *   For the das3 format, packet ID's must be positive and fit in an integer
 *   so the maximum is about 2.1 billion. 
 *
 *   Note that das v2 Streams can re-use packet ID's.  So the PacketDescriptor
 *   at, for example, ID 2 may be completely different from one invocation
 *   of a stream handler callback to another.
 * 
 *   Since das v3 streams have no packet length limitations, ID reuse is
 *   not permitted on a single stream.
 *  
 * @extends DasDesc
 * @nosubgrouping
 * @ingroup streams
 */
typedef struct stream_descriptor{
	/** The base structure */
	DasDesc base;

   /* TODO: Replace this with a sorted array of structures of the type

         {int id, DasDesc* pDesc}

      and use sorted insert and binary search to retrieve packet IDs 
      if greater then say 10.  For 10 or less the "small vector" 
      assumption applies and we just an embedded 10-element array 
      and loops.
   */
	DasDesc* descriptors[MAX_PKTIDS];

   /** List of defined coordinate frames */
   DasFrame* frames[MAX_FRAMES];

	/* Common properties */
	char compression[STREAMDESC_CMP_SZ];
   char type[STREAMDESC_TYPE_SZ];
	char version[STREAMDESC_VER_SZ];
	bool bDescriptorSent;
	  
	/** User data pointer.
	 * The stream->packet->plane hierarchy provides a good organizational
	 * structure for application data, especially for applications whose
	 * purpose is to filter streams.  This pointer can be used to hold
	 * a reference to information that is not serialized.  It is initialized
	 * to NULL when a PacketDescriptor is created otherwise the library
	 * doesn't deal with it in any other way. */
	 void* pUser;  
} StreamDesc;


/** Creates a new blank StreamDesc.
 * The returned structure has no packet descriptors, no properties are defined.
 * The compression attribute is set to 'none' and the version is set to 2.2
 * 
 * @memberof StreamDesc
 */
DAS_API StreamDesc* new_StreamDesc(void);

DAS_API StreamDesc* new_StreamDesc_str(DasBuf* pBuf, int nModel);

/** Print a short description of the stream to a string buffer,
 *  This is not a serialization, just an overview 
 * 
 * @memberof StreamDesc
 */
DAS_API char* StreamDesc_info(const StreamDesc* pSd, char* sBuf, int nLen);

/** Creates a deep-copy of an existing StreamDesc object.
 * 
 * An existing stream descriptor, probably one initialized automatically by
 * reading standard input, can be used as a template for generating a second
 * stream descriptor. This is a deep copy, all owned objects are copied as well
 * and may be changed with out affecting the source object or it components.
 *
 * @param pThis The stream descriptor to copy 
 * @return A new stream descriptor allocated on the heap with all associated
 *         packet descriptors attached and also allocated on the heap
 * 
 * @memberof StreamDesc
 */
DAS_API StreamDesc* StreamDesc_copy(const StreamDesc* pThis);


/** Delete a stream descriptor and all it's sub objects
 * 
 * @param pThis The stream descriptor to erase, the pointer should be set
 *        to NULL by the caller.
 */
DAS_API void del_StreamDesc(StreamDesc* pThis);

/** Get the number of packet descriptors defined for this stream
 * 
 * @warning It is possible to have a non-contiguous set of Packet IDs.  Unless
 *          the application insures by some mechanism that packet IDs are not
 *          skipped when calling functions like StreamDesc_addPktDesc() then
 *          the results of this function will not useful for iteration.
 * 
 * @param pThis The stream descriptor to query
 * 
 * @return Then number of packet descriptors attached to this stream
 *         descriptor.  For better performance the caller should reused the 
 *         return value as all possible packet ID's are tested to see home many
 *         are defined.
 */
DAS_API size_t StreamDesc_getNPktDesc(const StreamDesc* pThis);

/** Attach a packet descriptor to this stream.
 * 
 * The stream takes ownership of the packet descriptor.  It will be deleted when
 * the stream is deleted.
 * 
 * @param pThis The stream to receive the packet descriptor.  The PkdDesc object
 *        will have it's parent pointer set to this object.
 * 
 * @param pDesc Must be a pointer to a descriptor of type PktDesc or DasDs.  If
 *        the descriptor has a parent and it's not this stream object, an error
 *        will be thrown.
 * 
 * @param nPktId The ID for the new packet descriptor.
 * @return 0 on success or a positive error code on failure.
 * @memberof StreamDesc
 */
DAS_API DasErrCode StreamDesc_addPktDesc(StreamDesc* pThis, DasDesc* pDesc, int nPktId);


/** Indicates if the xtags on the stream are monotonic, in which
 * case there might be optimal ways of processing the stream.
 * @memberof StreamDesc
 */
DAS_API void StreamDesc_setMonotonic(StreamDesc* pThis, bool isMonotonic );

/** Adds metadata into the property set of the StreamDesc.  These include
 * the creation time, the source Id, the process id, the command line, and
 * hostname. 
 * @memberof StreamDesc
 */
DAS_API void StreamDesc_addStdProps(StreamDesc* pThis);

/** Adds the command line into the property set of the StreamDesc.
 * This can be useful when debugging.
 * @memberof StreamDesc
 */
DAS_API void StreamDesc_addCmdLineProp(StreamDesc* pThis, int argc, char* argv[] );

/** Creates a descriptor structure that for a stream packet type.  
 *
 * Initially this descriptor will only have xtags, but additional data planes
 * are added.  The packet ID for the new descriptor is automatically assigned
 * so to be the lowest legal ID not currently in use.
 * 
 * @param pThis The stream descriptor object that will receive the new packet 
 *        type.
 *
 * @param xUnits is a UnitType (currently char *) that describes the data.
 *        Generally this is used to identify times (e.g.UNIT_MJ1958,UNIT_US2000)
 *        or is UNIT_DIMENSIONLESS, but other UnitTypes are defined (e.g. 
 *        UNIT_HERTZ, UNIT_DB).  
 *
 * @param pXEncoder The encoder for X-plane values on this stream. The
 *         StreamDesc object takes ownership of the encoder's memory. 
 *
 * @returns A pointer to new PacketDescriptor object allocated on the heap.  
 *        This pointer is also stored in the 
 *        StreamDesc::packetDescriptors member variable of @a pThis.
 *
 * @memberof StreamDesc
 */
DAS_API PktDesc* StreamDesc_createPktDesc(
	StreamDesc* pThis, DasEncoding* pXEncoder, das_units xUnits 
);

/** Define a new vector direction frame for the stream.
 * @see new_DasFrame for arguments 
 * 
 * @returns The newly created frame, or null on a failure.
 *          Note that each coordinate frame in the same stream must have
 *          a different name
 * 
 * @memberof StreamDesc
 */
DAS_API DasFrame* StreamDesc_createFrame(
   StreamDesc* pThis, ubyte id, const char* sName, const char* sType
);

/** Get the next open frame ID 
 * 
 * @returns then next valid frame ID or a negative DasErrCode if no more frames are allowed
 */
DAS_API int StreamDesc_nextFrameId(const StreamDesc* pThis);

/** Make a deep copy of a PacketDescriptor on a new stream.
 * This function makes a deep copy of the given packet descriptor and 
 * places it on the provided stream.  Note, packet ID's are not preserved
 * in this copy.  The newly allocated PacketDescriptor may not have the same
 * packet ID as the old one.
 *
 * @param pThis the stream to get the new packet descriptor
 * @param pd The packet descriptor to clone onto the stream
 * @returns The newly created packet descriptor
 * @memberof StreamDesc
 */
DAS_API PktDesc* StreamDesc_clonePktDesc(StreamDesc* pThis, const PktDesc* pd);
													 
/** Deepcopy a PacketDescriptor from one stream to another.
 * The copy made by this function handles recursing down to all the planes
 * and properties owned by the given packet descriptor.  Unlike the the 
 * function clonePacketDescriptor() the packet ID is preserved across the copy.
 * @param pThis the stream descriptor to get the new packet descriptor
 * @param pOther the stream descriptor who's packet descriptor is copied
 * @param nPktId the id of the packet to copy, a value in the range of 0 to 99
 *        inclusive.
 * @returns The newly created packet descriptor, or NULL if there was no
 *          packet descriptor with that ID in the source.
 * @memberof StreamDesc
 */
DAS_API PktDesc* StreamDesc_clonePktDescById(
	StreamDesc* pThis, const StreamDesc* pOther, int nPktId
);

/** Check to see if an packet ID has been defined for the stream 
 * 
 * @param pThis The stream to check
 * @param nPktId The ID in question
 * @return true if a packet of that type is defined on the stream false
 *         otherwise
 */
DAS_API bool StreamDesc_isValidId(const StreamDesc* pThis, int nPktId);
													 
/** Get the packet descriptor associated with an ID.
 *
 * @param pThis The stream object which contains the packet descriptors.
 * @param id The numeric packet ID, a value from 1 to 99 inclusive.
 *
 * @returns NULL if there is no packet descriptor associated with the
 *          given Packet ID
 * @memberof StreamDesc
 */
DAS_API PktDesc* StreamDesc_getPktDesc(const StreamDesc* pThis, int id);


/** Get a frame pointer by it's index
 * 
 * @param pThis The stream object which contains the frame definitions
 * 
 * @param id The numeric frame index, is not used outside the stream
 *           descriptor itself
 * 
 * @returns NULL if there is no frame at the given index
 * @memberof StreamDesc
 */
DAS_API const DasFrame* StreamDesc_getFrame(const StreamDesc* pThis, int idx);

/** Return the number of frames defined in the stream */
DAS_API int8_t StreamDesc_getNumFrames(const StreamDesc* pThis);

/** Get a frame index given it's name 
 * 
 * @returns negative DasErrCode if there's no frame for the given name
 */
DAS_API int8_t StreamDesc_getFrameId(const StreamDesc* pThis, const char* sFrame);


/** Get a frame pointer by it's name 
 * 
 * @param sFrame the name of a frame pointer
 * @returns NULL if there is no frame by that name
 * 
 * @memberof StreamDesc 
 */
DAS_API const DasFrame* StreamDesc_getFrameByName(
   const StreamDesc* pThis, const char* sFrame
);

/** Get a frame pointer by it's id
 * 
 * @param id the numeric ID of a frame as stored in das_vector
 * @returns NULL if there is no frame by that name
 * 
 * @memberof StreamDesc 
 */
const DasFrame* StreamDesc_getFrameById(const StreamDesc* pThis, ubyte id);

/** Free any resources associated with this PacketDescriptor,
 * and release it's id number for use with a new PacketDescriptor.
  * @memberof StreamDesc
 */
DAS_API DasErrCode StreamDesc_freeDesc(StreamDesc* pThis, int nPktId);

/** An I/O function that makes sense to use for either operation 
 * @memberof StreamDesc
 */
DAS_API int StreamDesc_getOffset(StreamDesc* pThis);

/** Encode a StreamDesc to an XML string
 * 
 * @param pThis The stream descriptor to encode
 * @param pBuf A DasBuffer item to receive the bytes
 * @return 0 if encoding succeeded, a non-zero error code otherwise
 * @memberof StreamDesc
 */
DAS_API DasErrCode StreamDesc_encode(StreamDesc* pThis, DasBuf* pBuf);

/** Packtized Stream Descriptor Factory Function
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
DAS_API DasDesc* DasDesc_decode(
   DasBuf* pBuf, StreamDesc* pSd, int nPktId, int nModel
);

#ifdef __cplusplus
}
#endif

#endif /* _das_stream_h_ */
