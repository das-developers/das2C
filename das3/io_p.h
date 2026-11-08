/* Copyright (C) 2026 Chris Piker <chris-piker@uiowa.edu>
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

/* INTERNAL header, subject to change without notice.
 *
 * DasIO's packet size and ID limits.  They live here, not in io.c, so
 * util.c's daslimit_get() table reports the values this build enforces.
 */

#ifndef _das_io_p_h_
#define _das_io_p_h_

#include <das3/stream.h>   /* DAS2_MAX_PKTIDS */

/* The reusable serializing buffer (pDb) is elastic: it starts at
   DASIO_INIT_PKT_SZ and grows to the largest packet seen, read or written,
   but never past DASIO_MAX_PKT_SZ.  The das3 spec bounds packet IDs, not
   packet sizes; DASIO_MAX_PKT_SZ is this particular library's limit

   Builds for small machines can lower both from the make command line.
*/
#ifndef DASIO_INIT_PKT_SZ
#define DASIO_INIT_PKT_SZ 16384   /* holds most stream headers outright */
#endif
#ifndef DASIO_MAX_PKT_SZ
#define DASIO_MAX_PKT_SZ 16777216
#endif

/* sanity check limits */
#if (DASIO_MAX_PKT_SZ > 2147483647) || (DASIO_INIT_PKT_SZ > DASIO_MAX_PKT_SZ) || (DASIO_INIT_PKT_SZ < 128)
#error "Need 32 <= DASIO_INIT_PKT_SZ <= DASIO_MAX_PKT_SZ <= 2^31 - 1"
#endif

/* das2 length fields are six digits */
#define DAS2_LEN_MAX 999999

/* das3 IDs still index DasStream's fixed table (stream_p.h) until the
   descriptor storage changes (redesign_stream_framing.md item 2), then
   this becomes DAS3_MAX_PKTIDS - 1 */
#define DAS3_PKTID_MAX (DAS2_MAX_PKTIDS - 1)

#endif /* _das_io_p_h_ */
