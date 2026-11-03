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

struct das_pkt {
    DasDesc base;
	 
    int id;
	 
	 size_t uPlanes;
    DasPlane* planes[MAXPLANES];
    
	 /* Set to true when encode is called, make sure data doesn't go 
	  * out the door unless the descriptor is sent first */
	 bool bSentHdr;
};

#ifdef __cplusplus
}
#endif

#endif /* _das_packet_p_h_ */
