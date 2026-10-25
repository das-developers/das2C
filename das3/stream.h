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
#include <das3/packet.h>
#include <das3/dataset.h>

#ifdef __cplusplus
extern "C" {
#endif


#define STREAMDESC_CMP_SZ 48
#define STREAMDESC_VER_SZ 48
#define STREAMDESC_TYPE_SZ 48

#define MAX_PKTIDS 100


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
 * ID space for all descriptors, not a separate one for datasets (das3) 
 * versus packets (das2)
 * 
 * ID 0 is reserved for the stream descriptor itself.
 * 
 * Descriptor IDs:
 *   The lookup ID is the same value used as the header & data IDs in the
 *   stream. The legal packet ID range depends on the stream serialization
 *   method.  For the das2 format, the valid range is 1 to 99.
 * 
 *   For the das3 format, packet ID's must be positive and fit into a 
 *   short unsigned integer, so the valid range is 1 to 65,535.
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

	/* Common properties */
	char compression[STREAMDESC_CMP_SZ];
   char type[STREAMDESC_TYPE_SZ];
	char version[STREAMDESC_VER_SZ];
	bool bDescriptorSent;

	/* Reader opt-in: recover an undecodable embedded format (a mime'd blob with no
	   registered codec extension) as opaque per-record bytes instead of failing.
	   Set from DasIO_embedAsBytes() when the stream descriptor is decoded; the
	   dataset parser reads it via DasStream_getEmbedAsBytes(). */
	bool bEmbedAsBytes;
	  
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

/** True if embedded formats with no registered codec should be preserved
 * as opaque bytes rather than erroring out.
 *
 * Note that this may cause the shape of any applicable variables 
 * to change.
 * 
 * @memberof DasStream */
#define DasStream_getEmbedAsBytes(P) ((P)->bEmbedAsBytes)

/** Creates a new blank DasStream.
 * The returned structure has no packet descriptors, no properties are defined.
 * The compression attribute is set to 'none' and the version is set to 2.2
 * 
 * @memberof DasStream
 */
DAS_API DasStream* new_DasStream(void);

DAS_API DasStream* new_DasStream_str(DasBuf* pBuf, das_stream_model nModel);

/** The stream version as an enumeration, parsed from the version attribute.
 *
 * @returns STREAM_VER_UNK if the version string is not one this library
 *          knows how to read.  Never a 1.x value, das1 streams have no header.
 * @memberof DasStream
 */
DAS_API das_stream_ver DasStream_getVer(const DasStream* pThis);

/** The stream container type as an enumeration, parsed from the type attribute.
 *
 * A missing attribute means a basic stream, the only container das 2.2 had.
 * @memberof DasStream
 */
DAS_API das_stream_type DasStream_getType(const DasStream* pThis);


/** Print a short description of the stream to a string buffer.
 *
 * This is not a serialization, just an overview of the stream header and
 * its properties.  Same calling convention as DasDs_info().
 *
 * @param pSd The stream to describe.
 *
 * @param sBuf The output buffer, may be NULL if uLen is 0.
 *
 * @param uLen The size of sBuf in bytes, including room for the terminating
 *        NUL.  Output that does not fit is cut off, but sBuf is always NUL
 *        terminated when uLen > 0 and nothing past sBuf[uLen-1] is touched.
 *
 * @returns The length of the full description, not counting the NUL, as
 *        snprintf() does.  If this is uLen or more the output was cut off.
 *        To size a buffer, call once with (NULL, 0) and allocate the
 *        return value plus one.
 *
 * @memberof DasStream
 */
DAS_API int DasStream_info(const DasStream* pSd, char* sBuf, size_t uLen);

/** Print a one line summary of the stream header
 *
 * The first line of the _info() output, for example:
 *
 *    Stream: das-basic-stream v3.0
 *
 * Short enough to use inline as a printf %s argument.
 *
 * @param pThis The object to describe.
 * @param sBuf  The output buffer.
 * @param uLen  The size of sBuf, including room for the NUL.
 * @returns sBuf, or NULL if the whole line did not fit.  sBuf is always NUL
 *        terminated when uLen > 0.
 *
 * @memberof DasStream
 */
DAS_API char* DasStream_toStr(const DasStream* pThis, char* sBuf, size_t uLen);

/** Creates a deep-copy of an existing DasStream object.
 * 
 * An existing stream descriptor, probably one initialized automatically by
 * reading standard input, can be used as a template for generating a second
 * stream descriptor. This is a deep copy for properties, but that's it.  Any
 * owned frames and packet definitions are not copied. 
 * 
 * The stream encoding is saved, though this should probably be moved out
 * to a separate serializers.
 *
 * @param pThis The stream descriptor to copy 
 * 
 * @return A new stream descriptor allocated on the heap containing the
 *         same properties as the original, but none of the child 
 *         descriptors.  Those must be added manually.
 * 
 * @memberof DasStream
 */
DAS_API DasStream* DasStream_copy(const DasStream* pThis);

/** Delete a stream descriptor and all it's sub objects
 * 
 * @param pThis The stream descriptor to erase, the pointer should be set
 *        to NULL by the caller.
 * 
 * @memberof DasStream
 */
DAS_API void del_DasStream(DasStream* pThis);

/** Get the number of descriptors attached to this stream
 * 
 * Counts every attached descriptor, das2 packets and das3 datasets alike,
 * owned or shadowed.
 * 
 * @warning It is possible to have a non-contiguous set of Packet IDs.  Unless
 *          the application insures by some mechanism that packet IDs are not
 *          skipped when calling functions like DasStream_addDesc() then
 *          the results of this function will not useful for iteration.
 * 
 * @param pThis The stream descriptor to query
 * 
 * @return The number of descriptors attached to this stream
 *         descriptor.  For better performance the caller should reused the 
 *         return value as all possible packet ID's are tested to see home many
 *         are defined.
 * 
 * @memberof DasStream
 */
DAS_API size_t DasStream_numDesc(const DasStream* pThis);

/** Iterate over packet descriptors 
 * 
 * Here's one way to use this function in a loop:
 * 
 * @code
 * int nPktId = 0;
 * DasDesc* pDesc = NULL;
 * while((pDesc = DasStream_nextDesc(pSd, &nPktId)) != NULL){
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
DAS_API DasDesc* DasStream_nextDesc(const DasStream* pThis, int* pPrevPktId);

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
DAS_API DasErrCode DasStream_addDesc(DasStream* pThis, DasDesc* pDesc, int nPktId);

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
DAS_API DasErrCode DasStream_rmDesc(DasStream* pThis, DasDesc* pDesc, int nPktId);

/** Indicates if the xtags on the stream are monotonic, in which
 * case there might be optimal ways of processing the stream.
 * @memberof DasStream
 */
DAS_API void DasStream_setMonotonic(DasStream* pThis, bool isMonotonic );

/** Adds metadata into the property set of the DasStream.  These include
 * the creation time, the source Id, the process id, the command line, and
 * hostname. 
 * @memberof DasStream
 */
DAS_API void DasStream_addStdProps(DasStream* pThis);

/** Adds the command line into the property set of the DasStream.
 * This can be useful when debugging.
 * @memberof DasStream
 */
DAS_API void DasStream_addCmdLineProp(DasStream* pThis, int argc, char* argv[] );

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
 *        NULL if all packet IDs are in use, then pXEncoder is not taken.
 *
 * @memberof DasStream
 */
DAS_API PktDesc* DasStream_createPktDesc(
	DasStream* pThis, DasEncoding* pXEncoder, das_units xUnits 
);



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
DAS_API PktDesc* DasStream_clonePktDesc(DasStream* pThis, const PktDesc* pd);

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

/** Check to see if an packet ID has been defined for the stream 
 * 
 * @param pThis The stream to check
 * @param nPktId The ID in question
 * @return true if a packet of that type is defined on the stream false
 *         otherwise
 */
DAS_API bool DasStream_isValidId(const DasStream* pThis, int nPktId);

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

/** Get any descriptor associated with a packet ID.
 * 
 * @param pThis The stream object which contains the packet descriptors.
 * @param id The numeric packet ID, currently a value from 1 to 99 inclusive
 * @todo: Increase packet ID spice to ushort (1 to 2^16)
 * @returns NULL if there is no descriptor associated with the
 *          given Packet ID.  It is up to the caller to determine the 
 *          descriptor type
 * @memberof DasStream
 */
DAS_API DasDesc* DasStream_getDesc(const DasStream* pThis, int id);

/** If a data container is owned by this stream object, return it's packet ID
 * 
 * @param pThis The stream that is being asked to provide the ID
 * 
 * @param pDesc The owned object descriptor
 * 
 * @returns A number between 1 and MAX_PKTIDS, or -1 if the object is not
 *        owned by this stream.  The returned object may be a PktDesc or 
 *        a DasDs.
 * 
 * @memberof DasStream
 */
DAS_API int DasStream_getPktId(const DasStream* pThis, const DasDesc* pDesc);

/** Drop the descriptor for a packet ID, deleting it if this stream owns it
 * 
 * The ID slot is always cleared.  The descriptor, das2 packet or das3
 * dataset, is deleted only when this stream is its parent.  A descriptor
 * this stream merely tracks belongs to someone else and is left alone, the
 * same rule del_DasStream() follows.
 * 
 * @param pThis The stream holding the descriptor
 * @param nPktId The packet ID to vacate, 1 to 99
 * @returns DAS_OKAY, or an error code if no descriptor has that ID
 * @memberof DasStream
 */
DAS_API DasErrCode DasStream_freeDesc(DasStream* pThis, int nPktId);

/** Encode a DasStream to an version 2 XML string
 * 
 * @param pThis The stream descriptor to encode
 * @param pBuf A DasBuffer item to receive the bytes
 * @return 0 if encoding succeeded, a non-zero error code otherwise
 * @memberof DasStream
 */
DAS_API DasErrCode DasStream_encode2(DasStream* pThis, DasBuf* pBuf);

/** Encode a DasStream to an version 3 XML string
 * 
 * @param pThis The stream descriptor to encode
 * @param pBuf A DasBuffer item to receive the bytes
 * @return 0 if encoding succeeded, a non-zero error code otherwise
 * @memberof DasStream
 */
DAS_API DasErrCode DasStream_encode3(DasStream* pThis, DasBuf* pBuf);

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
DAS_API DasDesc* DasDesc_decode(
   DasBuf* pBuf, DasStream* pSd, int nPktId, int nModel
);

/** Up-convert a das2 packet descriptor into a new dataset object.
 *
 * This is the factory DasDesc_decode() uses for das2 <packet> headers when
 * reading into the das3 model.  It is public so a filter that reads in the
 * das2 model, to select on packet IDs and plane names, can still get the
 * standard das3 rendering of each packet and alter it before writing.
 *
 * The layout follows the das2 plane pattern: X-Y, X-Y-Z, X-YScan or events.
 * Each plane becomes one storage array, one variable and, unless planes
 * share a "source" property, one dimension named for the plane.  Plane
 * properties are copied to their dimensions with the das2 axis prefix
 * stripped.  A das2 text time plane's units name an epoch, but that is a
 * conversion hint for das2java, not the wire units; such planes render as
 * calendar values.
 *
 * @param pSd the stream the packet belongs to.  Needed because a das2
 *        waveform layout is announced in the stream header, not the packet.
 * @param pPd the packet descriptor to inspect.  It is not changed and is not
 *        retained, so the caller may delete it afterward.
 * @param sGroup the group, which is the wire name, for the new dataset.  NULL
 *        takes the packet's group property, or the lone Y plane's name, or a
 *        made up name when neither exists.
 * @param bCodecs true to define codecs matching the packet's encodings so
 *        the dataset can parse payloads as sent, or emit its header with the
 *        same <packet> layout.  False attaches arrays of doubles with no
 *        codecs, which suits values arriving pre-parsed through PlaneDesc.
 * @returns a new dataset not yet attached to any stream, or NULL on a loud
 *        error.
 * @memberof DasDs
 */
DAS_API DasDs* new_DasDs_packet(
	DasStream* pSd, PktDesc* pPd, const char* sGroup, bool bCodecs
);


#ifdef __cplusplus
}
#endif

#endif /* _das_stream_h_ */
