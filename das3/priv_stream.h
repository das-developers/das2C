/* Copyright (C) 2026 Chris Piker <chris-piker@uiowa.edu>
 *
 * This file is part of das2C, the Core Das2 C Library.
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

/** @file priv_stream.h INTERNAL header, subject to change without notice.
 *
 * Nothing here is API, and it is not installed.  The dataset builder borrows
 * descriptors from the reader's stream while the stream is open and takes
 * them over when it closes; these two calls are how.  Kept out of stream.h
 * so v3.0 doesn't freeze a two-stage ownership hand-off that only the
 * builder needs.  If a stream filter turns out to want it, promote it then,
 * adding a public function is a compatible change.
 */

#ifndef _das_priv_stream_h_
#define _das_priv_stream_h_

#include <das3/stream.h>

#ifdef __cplusplus
extern "C" {
#endif

/** Loosely attach a dataset (DasDs or PktDesc) to this stream
 * 
 * The stream does *not* take ownership of the dataset (or PktDesc) descriptor.
 * It will not be deleted when the stream is deleted unless DasStream_takeDesc()
 * is called later.
 * 
 * @param pThis the stream to track the packet descriptor.
 * 
 * @param pDesc a pointer to either a PktDesc or a DasDs.
 * 
 * @param nPktId The ID to assigne to the packet for this stream object
 * 
 * @return 0 on success or a positive error code on failure.
 * @memberof DasStream
 */
DasErrCode DasStream_shadowDesc(DasStream* pThis, DasDesc* pDesc, int nPktId);

/** Take ownership of a dataset (DasDs or PktDesc) 
 * 
 * The dataset must already be tracked if this stream object, typically by calling
 * DasStream_shadowDesc() earlier on.
 * 
 * Taken together, DasStream_shadowDesc() and DasStream_takeDesc() have the
 * same effect as DasStream_addDesc(), but split the operation into two
 * stages
 * 
 * @param pThis the stream to track the packet descriptor.
 * @param pDesc a pointer to either a PktDesc or a DasDs, may be NULL if nPktId is set.
 * @param nPktId The ID under which to find the descriptor.  Only used if pDesc is NULL.
 * 
 * @return 0 on success or a positive error code on failure.
 * @memberof DasStream
 */
DasErrCode DasStream_takeDesc(DasStream* pThis, DasDesc* pDesc, int nPktId);

#ifdef __cplusplus
}
#endif

#endif /* _das_priv_stream_h_ */
