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
	DasDesc* descriptors[MAX_PKTIDS];

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

#ifdef __cplusplus
}
#endif

#endif /* _das_stream_p_h_ */
