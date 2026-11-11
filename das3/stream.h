/* Copyright (C) 2015-2026 Chris Piker <chris-piker@uiowa.edu>
 * 
 * Adapted from:
 *   Copyright (C) 2006 Jeremy Faden <jeremy-faden@uiowa.edu>
 *
 * This file is part of das2C, the Core Das C Library.
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
#include <das3/datset.h>

#ifdef __cplusplus
extern "C" {
#endif


/* Counts, so the largest ID is one less.  das3 IDs stop at 0xFEFF; the top
   256 values, 0xFF00 to 0xFFFF, are reserved and never valid on a stream. */
#define DAS2_MAX_PKTIDS 100
#define DAS3_MAX_PKTIDS 0xFF00


/** @defgroup datamodel Data Model
 * Classes and functions for storing and manipulating correlated data values
 */


/** @addtogroup datamodel
 * @{
 */

/** @class DasStream stream.h <das3/stream.h>
 *
 * Describes the stream itself, in particular the compression used, 
 * current packetDescriptors, etc.
 * 
 * This is a container for top-level stream descriptor objects.  The
 * data owner ship model for das2C is:
 * 
 * DasStream -> DasPkt -> DasPlane -> 1-row of data
 *           -> DasDs -> DasAry -> arbitrary rows of data
 *                     -> DasDim -> DasVar (Structure access to DasDs Arrays)
 *
 * Anything not owned by DasStream is considered OOB (Out Of Band) data.
 * 
 * <b>Packet IDs</b>
 *
 * Each attached descriptor is filed under a packet ID, the small integer
 * that tags its header and data packets in a serialized stream.  There is
 * one ID space for das2 packets and das3 datasets alike, and ID 0 always
 * means the stream header itself.
 *
 * The ID belongs to the stream, not to the descriptor: ask the stream with
 * DasStream_getPktId(), and the stream processor callbacks in io.h pass it
 * along with each descriptor.  A descriptor that is not attached to a
 * stream has no ID.
 *
 * A stream accepts IDs from 1 to DAS3_MAX_PKTIDS - 1 (0xFEFF).  Writing a
 * das2 stream narrows that to 1 to 99, since das2 packet tags have two
 * digits; DasIO checks this when the header or data is written, not when
 * the descriptor is attached, so a das3 stream can be read and its IDs
 * renumbered before it goes out as das2.
 *
 * <b>Several datasets under one ID</b>
 *
 * A stream may hold more than one dataset under the same ID.  The one
 * attached most recently is the @em active one: it is what
 * DasStream_getDesc() returns and where new data for that ID goes.  The
 * older ones stay attached, with their records, and are still visited by
 * DasStream_nextDesc().  Removing the active one with DasStream_freeDesc()
 * makes the next most recent one active again, like popping a stack.
 *
 * das2 packets don't work this way.  A DasPkt holds a single record, so
 * there is nothing to keep: an ID used by a DasPkt can't be shared, and
 * reusing it means freeing the old packet first.
 *
 * To tell an active dataset from an older one while walking a stream, use
 * DasStream_isDescActive().
 *
 * @extends DasDesc
 * @nosubgrouping
 * @ingroup streams
 */
typedef struct das_stream DasStream;

/** @} */

/** Creates a new blank DasStream.
 * The returned structure has no packet descriptors, no properties are defined.
 * The compression attribute is set to 'none' and the version is set to 2.2
 * 
 * @memberof DasStream
 */
DAS_API DasStream* new_DasStream(void);


/** The stream version as an enumeration, parsed from the version attribute.
 *
 * @returns STREAM_VER_UNK if the version string is not one this library
 *          knows how to read.  Never a 1.x value, das1 streams have no header.
 * @memberof DasStream
 */
DAS_API das_stream_ver DasStream_getVer(const DasStream* pThis);

/** Set the stream version
 *
 * @param pThis The stream
 * @param nVer STREAM_VER_2_2 or STREAM_VER_3_0
 * @returns DAS_OKAY, or an error code for any other version
 * @memberof DasStream
 */
DAS_API DasErrCode DasStream_setVer(DasStream* pThis, das_stream_ver nVer);

/** The stream container type as an enumeration, parsed from the type attribute.
 *
 * A missing attribute means a basic stream, the only container das 2.2 had.
 * @memberof DasStream
 */
DAS_API das_stream_type DasStream_getType(const DasStream* pThis);

/** Set the stream container type
 *
 * @param pThis The stream
 * @param nType STREAM_TYPE_BASIC or STREAM_TYPE_DOC
 * @returns DAS_OKAY, or an error code for STREAM_TYPE_UNK
 * @memberof DasStream
 */
DAS_API DasErrCode DasStream_setType(DasStream* pThis, das_stream_type nType);


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

/** Count the descriptors attached to this stream
 *
 * Counts every attached descriptor of the given type, owned or shadowed,
 * active or not (see "Several datasets under one ID" in the DasStream
 * overview).  Handy for sizing an array before a DasStream_nextDesc() walk:
 * @code
 *   size_t uDs = DasStream_numDesc(pSd, DASDESC_DS);
 *   DasDs** ppDs = (DasDs**)calloc(uDs, sizeof(DasDs*));
 * @endcode
 *
 * @param pThis The stream to query
 *
 * @param dtype DASDESC_PKT to count das2 packets, DASDESC_DS to count das3
 *        datasets, or DASDESC_UNK to count both.
 *
 * @return The number of matching descriptors.
 *
 * @memberof DasStream
 */
DAS_API size_t DasStream_numDesc(const DasStream* pThis, das_desc_type_e dtype);

/** Walk the descriptors attached to this stream
 *
 * Visits descriptors in the order they were attached, including datasets
 * that are no longer active for their ID.  Start with NULL and pass back
 * each result to get the next one:
 *
 * @code
 * DasDesc* pDesc = NULL;
 * while((pDesc = DasStream_nextDesc(pSd, pDesc, DASDESC_DS)) != NULL){
 *   DasDs* pDs = (DasDs*)pDesc;    // only datasets, the filter saw to it
 *   int nPktId = DasStream_getPktId(pSd, pDesc);
 *   // Do stuff
 * }
 * @endcode
 *
 * The previous descriptor is only compared against the stream's list, never
 * read, but it must still be attached.  To remove descriptors during a
 * walk, get the next one before freeing the current one:
 *
 * @code
 * DasDesc* pNext = DasStream_nextDesc(pSd, NULL, DASDESC_UNK);
 * while((pDesc = pNext) != NULL){
 *   pNext = DasStream_nextDesc(pSd, pDesc, DASDESC_UNK);
 *   if(want_gone(pDesc)) DasStream_freeDesc(pSd, pDesc);
 * }
 * @endcode
 *
 * @param pThis The stream to walk
 *
 * @param pPrev The descriptor returned by the previous call, or NULL to get
 *        the first one.
 *
 * @param dtype DASDESC_PKT to visit only das2 packets, DASDESC_DS to visit
 *        only das3 datasets, or DASDESC_UNK to visit both.
 *
 * @returns The next matching descriptor, or NULL at the end of the list.
 *        NULL with an error logged if pPrev is not attached to this stream.
 *
 * @memberof DasStream
 */
DAS_API DasDesc* DasStream_nextDesc(
	const DasStream* pThis, const DasDesc* pPrev, das_desc_type_e dtype
);


/** Attach a packet descriptor or dataset to this stream.
 *
 * The stream takes ownership of the descriptor.  It will be deleted when
 * the stream is deleted.
 *
 * A dataset may be attached under an ID that already has an active dataset.
 * The new one becomes active and the old one stays attached, see "Several
 * datasets under one ID" in the DasStream overview.  A das2 packet needs
 * an ID no one else is using.
 *
 * @param pThis The stream to receive the descriptor.  The descriptor will
 *        have its parent pointer set to this object.
 *
 * @param pDesc Must be a pointer to a descriptor of type DasPkt or DasDs.  If
 *        the descriptor has a parent and it's not this stream object, an error
 *        is returned.
 *
 * @param nPktId The ID to file the descriptor under, 1 to 0xFEFF.
 *
 * @return DAS_OKAY on success, or an error code if the descriptor is
 *        already attached, belongs to another stream, the ID is out of range,
 *        or the ID is held by a das2 packet (or this is a das2 packet and the
 *        ID is in use).
 *
 * @memberof DasStream
 */
DAS_API DasErrCode DasStream_addDesc(DasStream* pThis, DasDesc* pDesc, int nPktId);


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
 * so to be the lowest ID from 1 to 99 that has no descriptor at all; use
 * DasStream_getPktId() to learn which one it got.
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
DAS_API DasPkt* DasStream_createPkt(
	DasStream* pThis, DasEnc* pXEncoder, das_units xUnits 
);


/** Deepcopy a PacketDescriptor from one stream to another.
 * The copy made by this function handles recursing down to all the planes
 * and properties owned by the given packet descriptor.  Unlike the the 
 * function clonePacketDescriptor() the packet ID is preserved across the copy.
 * @param pThis the stream descriptor to get the new packet descriptor
 * @param pOther the stream descriptor who's packet descriptor is copied
 * @param nPktId the ID of the packet to copy, 1 to 99.  The copy gets the
 *        same ID in this stream.
 * @returns The newly created packet descriptor, or NULL if the source has
 *          no das2 packet with that ID, or this stream already uses it.
 * @memberof DasStream
 */
DAS_API DasPkt* DasStream_clonePktById(
	DasStream* pThis, const DasStream* pOther, int nPktId
);

/** Get the active das2 packet for an ID.
 *
 * Same as DasStream_getDesc() but only answers for das2 packets.
 *
 * @param pThis The stream to search
 * @param id The packet ID, 1 to 0xFEFF
 *
 * @returns The packet, or NULL if the ID is unused or holds a dataset.
 * @memberof DasStream
 */
DAS_API DasPkt* DasStream_getPkt(const DasStream* pThis, int id);

/** Get the active descriptor for a packet ID.
 *
 * The active descriptor is the one attached most recently under this ID,
 * see "Several datasets under one ID" in the DasStream overview.  An unused
 * ID is not an error, so this is also the way to test whether an ID is
 * free:
 * @code
 *   if(DasStream_getDesc(pSd, 7) == NULL)
 *     DasStream_addDesc(pSd, (DasDesc*)pDs, 7);
 * @endcode
 *
 * @param pThis The stream to search
 * @param id The packet ID, 1 to 0xFEFF
 *
 * @returns The active descriptor, a DasPkt or a DasDs (check DasDesc_type()),
 *        or NULL if nothing is filed under the ID.  NULL with an error
 *        logged if the ID is out of range.
 * @memberof DasStream
 */
DAS_API DasDesc* DasStream_getDesc(const DasStream* pThis, int id);

/** Get the packet ID a descriptor is filed under in this stream
 *
 * Works for active and older datasets alike.
 *
 * @param pThis The stream that is being asked to provide the ID
 *
 * @param pDesc A DasPkt or DasDs
 *
 * @returns The ID, 1 to 0xFEFF, or -1 if pDesc is not attached to this
 *        stream.
 *
 * @memberof DasStream
 */
DAS_API int DasStream_getPktId(const DasStream* pThis, const DasDesc* pDesc);

/** Is this the active descriptor for its packet ID?
 *
 * The same answer as
 * @code
 *   DasStream_getDesc(pSd, DasStream_getPktId(pSd, pDesc)) == pDesc
 * @endcode
 * in one call.  das2 packets are always active, since they never share an
 * ID.  Useful when walking a stream to skip, or flush, datasets that a
 * later one with the same ID has replaced:
 * @code
 * DasDesc* pDesc = NULL;
 * while((pDesc = DasStream_nextDesc(pSd, pDesc, DASDESC_DS)) != NULL){
 *   if(!DasStream_isDescActive(pSd, pDesc)) continue;
 *   // Only the datasets that new data would go to
 * }
 * @endcode
 *
 * @param pThis The stream to ask
 * @param pDesc A DasPkt or DasDs
 * @returns true if pDesc is attached to this stream and is the active
 *        descriptor for its ID, false otherwise.
 *
 * @memberof DasStream
 */
DAS_API bool DasStream_isDescActive(const DasStream* pThis, const DasDesc* pDesc);

/** Remove a descriptor from this stream, deleting it if this stream owns it
 *
 * The descriptor, das2 packet or das3 dataset, is deleted only when this
 * stream is its parent.  A descriptor this stream merely tracks belongs to
 * someone else and is left alone, the same rule del_DasDesc() follows.
 *
 * If pDesc was the active descriptor for its ID, the next most recently
 * attached one under that ID, if any, becomes active.
 *
 * @param pThis The stream holding the descriptor
 * @param pDesc The descriptor to remove.  Only compared against the
 *        stream's list before it is found, so a stale pointer gets an error,
 *        not a crash.
 * @returns DAS_OKAY, or an error code if pDesc is not attached to this stream
 * @memberof DasStream
 */
DAS_API DasErrCode DasStream_freeDesc(DasStream* pThis, DasDesc* pDesc);


/** Up-convert a das2 packet descriptor into a new dataset object.
 *
 * This is what DasIO uses for das2 \<packet\> headers when reading into the
 * das3 model.  It is public so a filter that reads in the
 * das2 model, to select on packet IDs and plane names, can still get the
 * standard das3 rendering of each packet and alter it before writing.
 *
 * The layout follows the das2 plane pattern: X-Y, X-Y-Z, X-YScan or events.
 * Each plane becomes one storage array, one variable and, unless planes
 * share a "source" property, one dimension named for the plane.  Plane
 * properties are copied to their dimensions with the das2 axis prefix
 * stripped.  A das2 text time plane's units name an epoch, but that is a
 * conversion hint for das2java, not the units of the stored values; such
 * planes render as
 * calendar values.
 *
 * @param pSd the stream the packet belongs to.  Needed because a das2
 *        waveform layout is announced in the stream header, not the packet.
 * @param pPd the packet descriptor to inspect.  It is not changed and is not
 *        retained, so the caller may delete it afterward.
 * @param nPktId the packet's ID, used to name the dataset "\<group\>_\<id\>" so
 *        that datasets made from different packets of one group have
 *        different names.  A packet may not be attached yet when it is
 *        converted, so the ID is given here instead of looked up.
 * @param sGroup the group, the name used in the stream header, for the new
 *        dataset.  NULL
 *        takes the packet's group property, or the lone Y plane's name, or a
 *        made up name when neither exists.
 * @param bCodecs true to define codecs matching the packet's encodings so
 *        the dataset can parse payloads as sent, or emit its header with the
 *        same \<packet\> layout.  False attaches arrays of doubles with no
 *        codecs, which suits values arriving pre-parsed through DasPlane.
 * @returns a new dataset not yet attached to any stream, or NULL on a loud
 *        error.
 * @memberof DasDs
 */
DAS_API DasDs* new_DasDs_packet(
	DasStream* pSd, DasPkt* pPd, int nPktId, const char* sGroup, bool bCodecs
);

/** The stream header text for a version, "2.2", "3.0" etc. */
DAS_API const char* das_stream_ver_toStr(das_stream_ver ver);

/** The stream header text for a container type, "das-basic-stream" etc. */
DAS_API const char* das_stream_type_toStr(das_stream_type type);

#ifdef __cplusplus
}
#endif

#endif /* _das_stream_h_ */
