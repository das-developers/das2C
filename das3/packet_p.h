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

/** @file packet_p.h INTERNAL header, subject to change without notice.
 *
 * Nothing here is API, and it is not installed.
 */

#ifndef _das_packet_p_h_
#define _das_packet_p_h_

#include <das3/packet.h>
#include <das3/plane_p.h>

#ifdef __cplusplus
extern "C" {
#endif

struct das_pkt_t {
    DasDesc base;
	 
	 size_t uPlanes;
    DasPlane* planes[DASPKT_MAX_PLANES];
    
	 /* Bumped each time a plane is altered by adding new planes.
	  * DasIO notes this value with each header it writes and so
	  * it can see if the packet has changed between data writes,
	  * which would be a data integrity violation. */
	 uint32_t uHdrRev;
};


/* Moved from packet.h: library plumbing, not API */

/** Decode 1 packet's worth of data from a buffer.
 * @param pThis the packet descriptor to handle decoding
 * @param pBuf The buffer with the data, data will be read from the current 
 *        read point.
 * @returns 0 if successful, or a positive integer if not.
 * @memberof DasPkt
 */
das_errcode DasPkt_decodeData(DasPkt* pThis, DasBuf* pBuf);

/** Check to see if a legal plane layout is present.
 * 
 * Since not all checks for a legal packet layout can be made while the 
 * sub-objects are being added to the packet descriptor, this function is
 * provided to check the layout after adding all planes to a packet.
 * 
 * @see DasPkt_addPlane()
 * @param pThis The packet descriptor to check
 * @returns true if this packet descriptor has a legal set of planes, false 
 * otherwise.
 * @memberof DasPkt
 */
bool DasPkt_validate(DasPkt* pThis );

/** Copy in all planes from another a packet descriptor
 *
 * Deepcopy's the plane descriptors in pOther and attaches the newly
 * allocated planes to this DasPkt object.  This packet descriptor must
 * not already have any planes defined or this function will fail.
 *
 * @param pThis the destination for the newly allocated PlaneDescriptors
 * @param pOther the source of the PlaneDescriptors
 * @returns 0 on success or a positive error number if there is a problem
 *
 * @memberof DasPkt
 */
das_errcode DasPkt_copyPlanes(DasPkt* pThis,  const DasPkt* pOther);

/** Create a DasPkt from XML data
 * 
 * @param pBuf The buffer to read.  Reading will start with the read point and
 *        will run until DasBuf_remaining() is 0 or the end tag is found, which
 *        ever comes first.
 * @param pParent The parent packet descriptor, this may be NULL
 * @param nPktId The ID the header arrived under, used only in error
 *        messages.  The packet does not keep it, its stream does.
 * @return A pointer to a new DasPkt allocated on the heap, or NULL on an error.
 * @memberof DasPkt
 */
DasPkt* new_DasPkt_xml(DasBuf* pBuf, DasDesc* pParent, int nPktId);

#ifdef __cplusplus
}
#endif

#endif /* _das_packet_p_h_ */
