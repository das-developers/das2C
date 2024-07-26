/* Copyright (C) 2024 Chris Piker <chris-piker@uiowa.edu>
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

/** @file serial.h Creating, reading and writing datasets to and from buffers */

#ifndef _das_serial2_h_
#define _das_serial2_h_

#include <das2/stream.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Generate an potentially active dataset object from a packet descriptor.
 *
 * @note There are two ways to use this function:
 *       1. To create a dataset structure suitable for manual data insertion of
 *          das2 packet data (no codecs)
 *
 *       2. To create a dataset structure that parses it's own data direct from
 *          a stream without going through das2 parsing (has codecs)
 *     
 *        Since DasPlane interprets all data at doubles, setting bCodecs to
 *        false will generate attached arrays that expect doubles.  Setting bCodecs
 *        to true will generate attached arrays suitable for handling stream data
 *        "as-is".
 *
 * @param pSd - The stream descriptor, this is needed to detect for "waveform" layout
 *
 * @param pPd - The packet descriptor to inspect, it is not changed.
 *
 * @param sGroup - The group string name to assign to this dataset, may be NULL in
 *              which case pPd is inspected for a group name.
 *
 * @param bCodecs If true, define codecs for the generated dataset object so that
 *                it can be used to parse data packet payloads.  See the note above.
 *
 * @returns A new DasDs object allocated on the heap if successful, NULL otherwise
 *        and das_error is called internally.  Note that the dataset is not 
 *        attached to the stream descriptor.  It is the callers responsibility to
 *        attach it if desired.
 */
DAS_API DasDs* dasds_from_packet(
	DasStream* pSd, PktDesc* pPd, const char* sGroup, bool bCodecs
);


#ifdef __cplusplus
}
#endif

#endif /* _serial */
