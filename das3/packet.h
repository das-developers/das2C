/* Copyright (C) 2004-2017 Jeremy Faden <jeremy-faden@uiowa.edu> 
 *                         Chris Piker <chris-piker@uiowa.edu>
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


/** @file packet.h */

#ifndef _das_packet_h_
#define _das_packet_h_

#include <das3/plane.h>

#ifdef __cplusplus
extern "C" {
#endif

/** maximum planes allowed in a packet */
#define MAXPLANES 100

/** @class DasPkt packet.h <das3/packet.h>
 *
 * Holds information for a single packet type in a Das2 stream.
 *
 * A Das2 Stream may consist of up to 99 different types of \e packets. 
 * In the following Das2 Stream snippet two different types of packets
 * are defined:
 * @code
 * [00]000234<stream>
 *   <properties xMonotonic="true" xLabel="Time (s)" yLabel="Frequency (s!U-1!N)"
 *               zLabel="Electric Field (V m!U-1!N)" title="Voyager 1 PWS SA"
 *               Datum:xTagWidth="256.0 s" double:zFill="0.0" />
 * </stream>
 * @endcode
 * @code
 * [01]000201<packet>
 *   <x type="time24" units="us2000" ></x>
 *   <yscan name="averages" nitems="5" type="ascii9" yUnits="Hz" zUnits="V/m" 
 *          yTags="10.0,17.8,31.1,56.2,100.0">
 *     <properties zSummary="Average value within the interval"/>
 *   </yscan>
 * </packet>
 * @endcode
 * @code
 * [02]000201<packet>
 *   <x type="time24" units="us2000" ></x>
 *   <yscan name="peaks" nitems="5" type="ascii9" yUnits="Hz" zUnits="V/m" 
 *          yTags="10.0,17.8,31.1,56.2,100.0">
 *     <properties zSummary="Peak value within the interval"/>
 *   </yscan>
 * </packet>
 * @endcode
 * @code
 * :01:2012-01-01T12:56:22.792 1.91e-06 8.92e-07 7.80e-07 6.04e-07 2.43e-07
 * :02:2012-01-01T12:56:22.792 3.12e-06 4.10e-06 2.47e-06 1.42e-06 9.36e-07 
 * :01:2012-01-01T13:00:38.792 1.98e-06 4.63e-07 7.64e-07 7.56e-07 5.09e-07 
 * :02:2012-01-01T13:00:38.792 2.91e-06 1.46e-06 2.97e-06 1.42e-06 1.55e-06 
 * @endcode
 *
 * Each @b \<packet\> element above is a serialized PacketDescriptor.  This
 * structure and it's associated functions are responsible for:
 *
 *    -# Holding the definition of a single packet type within the stream
 *    -# Writing data values onto the stream
 *    -# Serializing data values from a stream
 *
 * To help them do their jobs, PacketDescriptors hold and array of 
 * PlaneDescriptors, as well a byte field containing up to one data packet's
 * worth of bytes.
 *
 * <h3>Creating Packet Descriptors</h3>
 *
 * Packet Descriptors are part of a Das2 Stream.  To define a new packet type
 * call:
 * @code
 *  createPacketDescriptor(DasStream* sd, DataType xDataType, UnitType xUnits)
 * @endcode
 *
 * or a similar function.  This particular version creates a PacketDescriptor 
 * that only has an \<x\> plane.  Using the following to add \<y\> and/or \<yscan\> 
 * planes to the packet.
 *
 * @code
 *   addPlaneY()
 *   addPlaneYScan()
 * @endcode 
 *
 * Optionally additions properties such as labels may be added to each \<y\> or 
 * \<yscan\> plane using:
 *
 * @code
 *   setPropertyString()
 * @endcode
 *
 * and related functions.
 *
 * When reading Das2 Streams PacketDescriptors are created automatically 
 * as the input is read by the processStream() method of the ::StreamHandler.
 *
 * <h3>Emitting Packet Data</h3>
 *  
 * The PacketDiscriptor has a 1-Packet wide buffer.  This buffer is used to
 * build up the output data for a single packet.  To set the value for the
 * various planes use:
 *
 * @code
 * setDataPacketDouble()       // For <x> and <y> planes
 * setDataPacketYScanDouble()  // For <yscan> planes
 * @endcode
 *
 * and related functions.  Once that job is complete, transmit the data 
 * packet using:
 *
 * @code
 * sendPacket()
 * @endcode
 *
 * The details of encoding the data according the format stored in the
 * packet descriptor are handled by the library.
 *
 * <h3>Reading Data</h3>
 *
 * @extends DasDesc
 * @nosubgrouping
 * @ingroup streams
 */
typedef struct das_pkt DasPkt;


/** Creates a packet descriptor with the default settings.
 * 
 * @return A pointer to a new DasPkt allocated on the heap, or NULL on an error.
 * @memberof DasPkt
 */
DAS_API DasPkt* new_DasPkt(void);


/** Check for packet descriptor format equality
 * 
 * This function checks to see if two packet descriptors define the same data
 * note that the DasStream parent of each need not be the same, nor are the
 * descriptors required to have the same current data values.
 * 
 * @param pPd1
 * @param pPd2
 * @return true if both packet descriptors provide the same definition, false
 *         otherwise
 */
DAS_API bool DasPkt_equalFormat(const DasPkt* pPd1, const DasPkt* pPd2);

/** Get the packet ID for this packet.
 * Each packet type within a Das 2 Stream has a unique ID form 1 to 99 
 * inclusive.  Note that ID's <b>can be reused</b>!  So processing code
 * must be on the lookout for re-definition packets.
 * @return the packet id, a number between 1 and 99 or -1 if there is on packet
 *         id assigned.
 * @memberof DasPkt
 */
DAS_API int DasPkt_getId(const DasPkt* pThis);


/** Get the size of data records defined by a packet descriptor
 * 
 * @param pThis The packet descriptor to query
 * @return The size in bytes of a single packet's worth of data.
 * @memberof DasPkt
 */
DAS_API size_t DasPkt_recBytes(const DasPkt* pThis);


/** Get the number of planes in this type of packet
 * @param pThis the packet descriptor in question
 * @returns The number of planes defined in this packet.
 * @memberof DasPkt
 */
DAS_API size_t DasPkt_getNPlanes(const DasPkt* pThis);

/** Get the number of planes of a particular type in a packet
 * 
 * @param pThis The packet descriptor to check
 * @param pt the plane type to check
 * @return The number of planes of a particular type in this packet descriptor
 * @memberof DasPkt
 */
DAS_API size_t DasPkt_getNPlanesOfType(const DasPkt* pThis, plane_type_t pt);

/** Add a plane to a packet
 * 
 * All data in a das2 stream are sent via packets, each packet type has 1-100
 * planes.  A plane can be a single column of numbers, which has one value
 * per data packet, or in the case of \<yscan\> planes they can be more like
 * sub-tables which have many values per packet.  The newly added plane will 
 * have this DasPkt assigned as it's parent.
 * 
 * Planes have types.  A Packet must have at least 1 \<x\> plane.  In general it
 * may have as many \<y\> \<yscan\> planes as it likes up to the plane limit. 
 * For packets with \<z\> planes, no \<yscan\> planes are allowed and one and 
 * only one \<y\> plane must be preset.  This function enforces the packet 
 * formation rules.  In summary the following patterns are legal.
 *  - X [X]
 *  - X [X] Y [Y Y ...]
 *  - X [X] YScan [YScan YScan ...]
 *  - X [X] Y [Y Y ...] YScan [YScan YScan ...] (see note)
 *  - X [X] Z [Z Z ...]
 * 
 * where [] indicates optional planes.  Note: Y and YScan planes can be
 * interleaved in any order.
 * 
 * This function adds the plane after all existing planes.  Thus the index
 * of any existing planes is not altered.
 * 
 * @param pThis The packet descriptor to receive the new plane definition
 * @param pPlane The plane to add
 * @return On success the index of the new plane is returned, or -1 on an error
 *
 * @memberof DasPkt
 */
DAS_API int DasPkt_addPlane(DasPkt* pThis, DasPlane* pPlane);


/** Determine the type of plane by index
 *
 * @param pThis the packet descriptor to query
 * @param iPlane the index in question.  Valid values for this parameter are
 *        0 to MAXPLANES - 1
 * @returns If the index corresponds to a data plane, then one of the enum
 *          values is return:
 *          <ul><li>X</li>
 *          <li>Y</li>
 *          <li>YSCAN</li>
 *          <li>Z</li></ul>
 *          is returned, otherwise:
 *          <ul><li>Invalid</li></ul>
 *          is returned
 *
 * @memberof DasPkt
 */
DAS_API plane_type_t DasPkt_getPlaneType(const DasPkt* pThis, int iPlane);


/** returns the PlaneDescriptor for plane number @a iplane
 * This can be used to query properties of the plane, such as units and
 * the name.
 *
 * @param pThis The packet descriptor to query
 * @param iplane The index of the plane to retrieve.  The 0th plane is
 *         an \<x\> plane if one is present in the stream.
 *
 * @memberof DasPkt
 */
DAS_API DasPlane* DasPkt_getPlane(DasPkt* pThis, int iplane);


/** Replace a plane descriptor at a specific index
 * 
 * @param pThis The packet descriptor to hold the new plane
 * @param iPlane The index of the plane to replace.  The 0th plane is
 *         an \<x\> plane if one is present in the stream.
 * @param pNew The new packet descriptor, if NULL this function serves to
 *       remove plane descriptors
 * @returns The plane that was removed and detached from the packet
 * @memberof DasPkt
 */
DAS_API DasPlane* DasPkt_replaceAt(DasPkt* pThis, int iPlane, DasPlane* pNew);


/** Get the plane number within this packet description
 * @param pThis the packet descriptor to query
 * @param pPlane a pointer to the plane who's index is required
 * @returns a number from 0 to 99 if successful or -1 if the plane pointed to
 *          by pPlane is not part of this packet description
 */
DAS_API int DasPkt_getPlaneIdx(DasPkt* pThis, DasPlane* pPlane);

/** Get a Plane Descriptor for the plane with the name @a name
 *
 * @returns A pointer to the plane, or NULL if no plane with the 
 *          given name is present in the packet descriptor
 */
DAS_API DasPlane* DasPkt_getPlaneByName(DasPkt* pThis, const char* name);

/** Get the Ith plane of a given type
 * 
 * @param pThis The packet descriptor to query
 * @param ptype The plane type, one X, Y, Z, YScan
 * @param iRelIndex The number of the plane of a given type to find.  The
 *        lowest index will be 0 for a given type.
 * @return A pointer to the plane, or NULL if there are not at least iRelIndex + 1
 *          planes of the given type in the packet
 */
DAS_API DasPlane* DasPkt_getPlaneByType(
	DasPkt* pThis, plane_type_t ptype, int iRelIndex);

/** Gets the Nth plane of a given type. 
 *
 * This is useful for iterating over all planes of a given type.  
 *
 * @param pThis A packet descriptor structure pointer.
 * 
 * @param ptype The plane type, one X, Y, Z, YScan
 *
 * @param iRelIndex The number of the plane of a given type to find.  The
 *        lowest index will be 0 for a given type.
 *
 * @return The absolute plane index for the Nth plane of a given type. If
 *        No planes of the given type are present, or iIndex is out of range
 *        -1 is returned.
 * 
 * @memberof DasPkt
 */
DAS_API int DasPkt_getPlaneIdxByType(
	const DasPkt* pThis, plane_type_t ptype, int iRelIndex
);

/** returns the plane number for the named plane.  
 * @param pThis The packet descriptor to query.
 * @param name The name in of the plane to find
 * @param planeType if not set to 0, then PlaneType must also match. 
 * Note that typically the 0th plane is typically the \<x\> plane.
 * @memberof DasPkt
 */
DAS_API int DasPkt_getPlaneIdxByName(
	DasPkt* pThis, const char* name, plane_type_t planeType
);

/** returns the PlaneDescriptor for the 1st X Tag plane.  
 * @memberof DasPkt
 */
DAS_API DasPlane* DasPkt_getXPlane(DasPkt* pThis);

/** Serialize a packet descriptor as XML data
 * 
 * @param pThis The packet descriptor to store as string data
 * @param pBuf A buffer object to received the string data
 * @return 0 if successful, or a positive integer if not.
 * 
 * @memberof DasPkt
 */
DAS_API DasErrCode DasPkt_encode(const DasPkt* pThis, DasBuf* pBuf);

/** Serialize a packet's current data
 * In addition to holding the format information for Das2 Stream packets
 * DasPkt objects also hold a a single packet's worth of data.  Use this
 * function to encode the current data values for output.
 * 
 * @param pThis The packet descriptor that has been loaded with data
 * @param pBuf A buffer to receive the encoded bytes
 * @returns 0 if successful, or a positive integer if not.
 * 
 * @memberof DasPkt
 */
DAS_API DasErrCode DasPkt_encodeData(const DasPkt* pThis, DasBuf* pBuf);


/** Convenience function for setting a single value in a plane
 * This is just a shortcut for:
 * @code
 * DasPlane* pPlane = DasPkt_getPlane(pPkt, uPlane);
 * DasPlane_setValue(pPlane, uItem, value);
 * @endcode
 * @param pThis The packet descriptor in question.
 * @param uPlane The index of the plane to receive the values
 * @param uItem The index of the item in the plane to be set to the new value
 * @param val The new value for this item in this plane.
 * @return 0 on success or a positive error code if there is a problem
 * @memberof DasPkt
 */
DAS_API DasErrCode DasPkt_setValue(
	DasPkt* pThis, size_t uPlane, size_t uItem, double val
);

/** Convenience function for setting an array of values in a plane
 * This is just a shortcut for:
 * @code
 * DasPlane* pPlane = DasPkt_getPlane(pPkt, uPlane);
 * DasPlane_setValues(pPlane, pVals);
 * @endcode
 * @param pThis The packet descriptor in question.
 * @param uPlane The index of the plane to receive the values
 * @param pVals The array of values to set.  The array is assumed to be the
 *        same length as the number of items in plane @a uPlane
 * @return 0 on success or a positive error code if there is a problem
 * @memberof DasPkt
 */
DAS_API DasErrCode DasPkt_setValues(
	DasPkt* pThis, size_t uPlane, const double* pVals
);

#ifdef __cplusplus
}
#endif

#endif /* _das_packet_h_ */
