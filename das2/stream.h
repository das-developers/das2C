/* Copyright (C) 2015-2024 Chris Piker <chris-piker@uiowa.edu>
 * 
 * Adapted from:
 *   Copyright (C) 2006 Jeremy Faden <jeremy-faden@uiowa.edu>
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

/** @file stream.h Objects representing a single das stream */

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
#define MAX_FRAMES 12  /* <-- if drastically increased, update _newFrameId() */


/** @defgroup DM Data Model
 * Classes and functions for storing and manipulating correlated data values
 */


/** @addtogroup DM
 * @{
 */

/** Describes the stream itself, in particular the compression used, 
 * current packetDescriptors, etc.
 * 
 * This is a container for top-level stream descriptor objects.  The
 * data owner ship model for das2C is:
 * 
 * DasStream -> PktDesc -> PlaneDesc -> 1-row of data
 *           -> DasDs -> DasAry -> arbitrary rows of data
 *                     -> DasDim -> DasVar (Structure access to DasDs Arrays)
 *
 * Anything not owned by DasStream is considered OOB (Out Of Band) data.
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
typedef struct das_stream{
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
} DasStream;

/** @} */

/** Compatability macro
 * For backwards compatability, all functions with the name pattern 
 * *DasStream* are also have a macro alias to *StreamDesc*.
 */
#define StreamDesc  DasStream

/** Creates a new blank StreamDesc.
 * The returned structure has no packet descriptors, no properties are defined.
 * The compression attribute is set to 'none' and the version is set to 2.2
 * 
 * @memberof DasStream
 */
DAS_API DasStream* new_DasStream(void);

#define new_StreamDesc new_DasStream

DAS_API DasStream* new_DasStream_str(DasBuf* pBuf, int nModel);

#define new_StreamDesc_str new_DasStream_str

/** Print a short description of the stream to a string buffer,
 *  This is not a serialization, just an overview 
 * 
 * @memberof DasStream
 */
DAS_API char* DasStream_info(const DasStream* pSd, char* sBuf, int nLen);

#define StreamDesc_info DasStream_info

/** Creates a deep-copy of an existing DasStream object.
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
 * @memberof DasStream
 */
DAS_API DasStream* DasStream_copy(const DasStream* pThis);

#define StreamDesc_copy DasStream_copy

/** Delete a stream descriptor and all it's sub objects
 * 
 * @param pThis The stream descriptor to erase, the pointer should be set
 *        to NULL by the caller.
 * 
 * @memberof DasStream
 */
DAS_API void del_DasStream(DasStream* pThis);

#define del_StreamDesc del_DasStream

/** Get the number of packet descriptors defined for this stream
 * 
 * @warning It is possible to have a non-contiguous set of Packet IDs.  Unless
 *          the application insures by some mechanism that packet IDs are not
 *          skipped when calling functions like DasStream_addPktDesc() then
 *          the results of this function will not useful for iteration.
 * 
 * @param pThis The stream descriptor to query
 * 
 * @return Then number of packet descriptors attached to this stream
 *         descriptor.  For better performance the caller should reused the 
 *         return value as all possible packet ID's are tested to see home many
 *         are defined.
 * 
 * @memberof DasStream
 */
DAS_API size_t DasStream_getNPktDesc(const DasStream* pThis);

#define StreamDesc_getNPktDesc DasStream_getNPktDesc

/** Iterate over packet descriptiors 
 * 
 * Here's one way to use this function in a loop:
 * 
 * @code
 * int nPktId = 0;
 * DasDesc* pDesc = NULL;
 * while((pDesc = DasStream_nextPktDesc(pSd, &nPktId)) != NULL){
 *   // Do stuff
 *   // call DasDesc_type() to further sub-divide actions
 * }
 * @endcode
 * 
 * @param pThis A stream descriptor structure
 * 
 * @param pPrevPktId A pointer to the ID of a previous packet descriptor.
 *        Will be incremented to the next valid packet ID
 * 
 * @returns The packet descriptor for the next valid packet ID, or NULL if
 *        there was no next valid packet descriptor.
 * 
 * @memberof DasStream
 */
DAS_API DasDesc* DasStream_nextPktDesc(const DasStream* pThis, int* pPrevPktId);

#define StreamDesc_nextPktDesc DasStream_nextPktDesc

/** Attach a packet descriptor to this stream.
 * 
 * The stream takes ownership of the packet (or dataset) descriptor.  It will
 * be deleted when the stream is deleted.
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
 * @memberof DasStream
 */
DAS_API DasErrCode DasStream_addPktDesc(DasStream* pThis, DasDesc* pDesc, int nPktId);

#define StreamDesc_addPktDesc DasStream_addPktDesc

/** Loosly attach a dataset (DasDs or PktDesc) to this stream
 * 
 * The stream does *not* take ownership of the dataset (or PktDesc) desciptor.
 * It will not be deleted when the stream is deleted unless DasStream_ownPktDesc()
 * is called later.
 * 
 * @param pThis the stream to track the packet desciptor.
 * 
 * @param pDesc a pointer to either a PktDesc or a DasDs.
 * 
 * @param nPktId The ID to assigne to the packet for this stream object
 * 
 * @return 0 on success or a positive error code on failure.
 * @memberof DasStream
 */
DAS_API DasErrCode DasStream_shadowPktDesc(DasStream* pThis, DasDesc* pDesc, int nPktId);

/** Take ownership of a dataset (DasDs or PktDesc) 
 * 
 * @param pThis the stream to track the packet desciptor.
 * 
 * @param pDesc a pointer to either a PktDesc or a DasDs, may be NULL if nPktId is set.
 * 
 * @param nPktId The ID under which to find the descriptor.  Only used if pDesc is NULL.
 * 
 * @return 0 on success or a positive error code on failure.
 * @memberof DasStream
 */
DAS_API DasErrCode DasStream_ownPktDesc(DasStream* pThis, DasDesc* pDesc, int nPktId);

/** Detach a packet descriptor from this stream.  
 * 
 * The stream no longer has ownership of the packet (or dataset), in fact 
 * it will no longer have any record of the descriptor of any kind so it
 * will not be deleted when the stream descriptor is deleted.
 * 
 * @param pThis The stream from which the descriptor should be detached.
 * 
 * @param pDesc, if not NULL the pointer value will be compared with
 *        stored descriptor pointers.
 * 
 * @param nPktId, if not 0, the ID will be compared against stored IDs.
 *        This check is not performed if pDesc is not NULL.
 * 
 * @returns DAS_OKAY on success, an error code if no descriptor with the 
 *        given conditions is attached to this stream.
 * 
 * @memberof DasStream
 */
DAS_API DasErrCode DasStream_rmPktDesc(DasStream* pThis, DasDesc* pDesc, int nPktId);

/** Indicates if the xtags on the stream are monotonic, in which
 * case there might be optimal ways of processing the stream.
 * @memberof DasStream
 */
DAS_API void DasStream_setMonotonic(DasStream* pThis, bool isMonotonic );

#define StreamDesc_setMonotonic DasStream_setMonotonic

/** Adds metadata into the property set of the DasStream.  These include
 * the creation time, the source Id, the process id, the command line, and
 * hostname. 
 * @memberof DasStream
 */
DAS_API void DasStream_addStdProps(DasStream* pThis);

#define StreamDesc_addStdProps DasStream_addStdProps

/** Adds the command line into the property set of the DasStream.
 * This can be useful when debugging.
 * @memberof DasStream
 */
DAS_API void DasStream_addCmdLineProp(DasStream* pThis, int argc, char* argv[] );

#define StreamDesc_addCmdLineProp DasStream_addCmdLineProp

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
 *         DasStream object takes ownership of the encoder's memory. 
 *
 * @returns A pointer to new PacketDescriptor object allocated on the heap.  
 *        This pointer is also stored in the 
 *        DasStream::packetDescriptors member variable of @a pThis.
 *
 * @memberof DasStream
 */
DAS_API PktDesc* DasStream_createPktDesc(
	DasStream* pThis, DasEncoding* pXEncoder, das_units xUnits 
);

#define StreamDesc_createPktDesc DasStream_createPktDesc


/** Give a vector frame object to the stream, the stream object takes ownership
 * 
 * @param pThis A valid DasStream pointer
 * @param pFrame The vector frame to defined for datasets in this stream.  The
 *           DasStream takes ownership of the frame, use copy_DasFrame() to make
 *           a copy first if you don't own the frame object
 * @returns The index of the frame definition in the stream's frame array.
 */
DAS_API int DasStream_addFrame(DasStream* pThis, DasFrame* pFrame);

/** Define a new vector direction frame for the stream.
 * @see new_DasFrame for arguments 
 * 
 * @returns The newly created frame, or null on a failure.
 *          Note that each coordinate frame in the same stream must have
 *          a different name
 * 
 * @memberof DasStream
 */
DAS_API DasFrame* DasStream_createFrame(
   DasStream* pThis, ubyte id, const char* sName, const char* sType
);

#define StreamDesc_createFrame DasStream_createFrame

/** Get an open frame ID 
 * 
 * @returns A frame ID that is not currently in use by any frame in 
 *          the stream.  Return a negative DasErrCode if no more
 *          frames are allowed in the stream
 */
DAS_API int DasStream_newFrameId(const DasStream* pThis);

#define StreamDesc_newFrameId DasStream_nextFrameId

/** Make a deep copy of a PacketDescriptor on a new stream.
 * This function makes a deep copy of the given packet descriptor and 
 * places it on the provided stream.  Note, packet ID's are not preserved
 * in this copy.  The newly allocated PacketDescriptor may not have the same
 * packet ID as the old one.
 *
 * @param pThis the stream to get the new packet descriptor
 * @param pd The packet descriptor to clone onto the stream
 * @returns The newly created packet descriptor
 * @memberof DasStream
 */
DAS_API PktDesc* DasStream_clonePktDesc(DasStream* pThis, const PktDesc* pd);

#define StreamDesc_clonePktDesc DasStream_clonePktDesc
													 
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
 * @memberof DasStream
 */
DAS_API PktDesc* DasStream_clonePktDescById(
	DasStream* pThis, const DasStream* pOther, int nPktId
);

#define StreamDesc_clonePktDescById DasStream_clonePktDescById

/** Check to see if an packet ID has been defined for the stream 
 * 
 * @param pThis The stream to check
 * @param nPktId The ID in question
 * @return true if a packet of that type is defined on the stream false
 *         otherwise
 */
DAS_API bool DasStream_isValidId(const DasStream* pThis, int nPktId);

#define StreamDesc_isValidId DasStream_isValidId
													 
/** Get the packet descriptor associated with an ID.
 *
 * @param pThis The stream object which contains the packet descriptors.
 * @param id The numeric packet ID, a value from 1 to 99 inclusive.
 *
 * @returns NULL if there is no packet descriptor associated with the
 *          given Packet ID
 * @memberof DasStream
 */
DAS_API PktDesc* DasStream_getPktDesc(const DasStream* pThis, int id);

#define StreamDesc_getPktDesc DasStream_getPktDesc


/** Get a frame pointer by it's index
 * 
 * @param pThis The stream object which contains the frame definitions
 * 
 * @param id The numeric frame index, is not used outside the stream
 *           descriptor itself
 * 
 * @returns NULL if there is no frame at the given index
 * @memberof DasStream
 */
DAS_API const DasFrame* DasStream_getFrame(const DasStream* pThis, int idx);

#define StreamDesc_getFrame DasStream_getFrame

/** Return the number of frames defined in the stream */
DAS_API int8_t DasStream_getNumFrames(const DasStream* pThis);

#define StreamDesc_getNumFrames DasStream_getNumFrames

/** Get a frame index given it's name 
 * 
 * @returns negative DasErrCode if there's no frame for the given name
 */
DAS_API int8_t DasStream_getFrameId(const DasStream* pThis, const char* sFrame);

#define StreamDesc_getFrameId DasStream_getFrameId

/** Get a frame pointer by it's name 
 * 
 * @param sFrame the name of a frame pointer
 * @returns NULL if there is no frame by that name
 * 
 * @memberof DasStream 
 */
DAS_API const DasFrame* DasStream_getFrameByName(
   const DasStream* pThis, const char* sFrame
);

#define StreamDesc_getFrameByName DasStream_getFrameByName

/** Get a frame pointer by it's id
 * 
 * @param id the numeric ID of a frame as stored in das_vector
 * @returns NULL if there is no frame by that name
 * 
 * @memberof DasStream 
 */
const DasFrame* DasStream_getFrameById(const DasStream* pThis, ubyte id);

#define StreamDesc_getFrameById DasStream_getFrameById

/** Free any resources associated with this PacketDescriptor,
 * and release it's id number for use with a new PacketDescriptor.
  * @memberof DasStream
 */
DAS_API DasErrCode DasStream_freeSubDesc(DasStream* pThis, int nPktId);

#define StreamDesc_freeDesc DasStream_freeSubDesc

/** An I/O function that makes sense to use for either operation 
 * @memberof DasStream
 */
DAS_API int DasStream_getOffset(DasStream* pThis);

#define StreamDesc_getOffset DasStream_getOffset

/** Encode a DasStream to an XML string
 * 
 * @param pThis The stream descriptor to encode
 * @param pBuf A DasBuffer item to receive the bytes
 * @return 0 if encoding succeeded, a non-zero error code otherwise
 * @memberof DasStream
 */
DAS_API DasErrCode DasStream_encode(DasStream* pThis, DasBuf* pBuf);

#define StreamDesc_encode DasStream_encode

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
   DasBuf* pBuf, DasStream* pSd, int nPktId, int nModel
);


#ifdef __cplusplus
}
#endif

#endif /* _das_stream_h_ */
