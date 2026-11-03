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

/* INTERNAL header, not installed.  The parts of oob.h only the library
 * calls. */

#ifndef _das_oob_p_h_
#define _das_oob_p_h_

#include <das3/oob.h>

#ifdef __cplusplus
extern "C" {
#endif


/* Moved from oob.h: library plumbing, not API */

/** Initialize a comment object form string data
 * 
 * @param pThis
 * @param sbuf
 * @return 
 * @memberof StreamComment
 */
DasErrCode OobComment_decode(OobComment* pThis, DasBuf* sbuf);

/** Serialize a comment into a buffer in das2.2 format.
 *
 * Writes: @c \<comment type="T" source="S" value="V" /\>
 *
 * @param pThis The comment to save
 * @param pBuf The buffer to receive the data
 * @return DAS_OKAY on success, a positive error code otherwise
 * @memberof OobComment
 */
DasErrCode OobComment_encode(OobComment* pThis, DasBuf* pBuf);

/** Serialize a comment into a buffer in das3.0 format.
 *
 * Writes: @c \<comment type="T" source="S"\>V\</comment\>
 * The @c source attribute is omitted when the source field is empty.
 *
 * @param pThis The comment to save
 * @param pBuf The buffer to receive the data
 * @return DAS_OKAY on success, a positive error code otherwise
 * @memberof OobComment
 */
DasErrCode OobComment_encode3(OobComment* pThis, DasBuf* pBuf);

/** Initialize an Exception Structure
 * This only needs to be called once, the same structure will be reused each
 * time OutOfBand_decode() is called.  Memory is not re-allocated for each call,
 * it only expands as needed.
 * 
 * @param pThis A pointer to the stream comment object to initialize
 * @memberof OobComment
 */
void OobComment_init(OobComment* pThis);

/** Parse text data into a stream exception
 * @memberof OobExcept
 */
DasErrCode OobExcept_decode(OobExcept* pThis, DasBuf* str);

/** Serialize an exception into a buffer in das2.2 format.
 *
 * Writes: @c \<exception type="T" message="M" /\>
 *
 * @param pThis The exception to encode
 * @param pBuf The buffer to receive the bytes
 * @return DAS_OKAY on success, a positive error code on failure.
 * @memberof OobExcept
 */
DasErrCode OobExcept_encode(OobExcept* pThis, DasBuf* pBuf);

/** Serialize an exception into a buffer in das3.0 format.
 *
 * Writes: @c \<exception type="T"\>M\</exception\>
 *
 * @param pThis The exception to encode
 * @param pBuf The buffer to receive the bytes
 * @return DAS_OKAY on success, a positive error code on failure.
 * @memberof OobExcept
 */
DasErrCode OobExcept_encode3(OobExcept* pThis, DasBuf* pBuf);

/** Factory function to produce out of band objects from general data
 *
 * Unlike Header packets which are read in-frequently, out of band objects may
 * occur frequently in the input stream.  To avoid alot of memory allocations
 * This factory function takes an array of pointers to out of band objects.
 *
 * If one of the given OOB's in the input array corresponds to the parsed object
 * then it is initialize with the values in the buffer.   If the out of band
 * object is a proper XML item but is not understood by this function it is 
 * just ignored and @b which will be set to -1
 * 
 * @param[in] pBuf a readable buffer containing up to one out of band object
 * @param[in] ppObjs a NULL terminated array of out of band objects to possibly
 *            populate with data
 * @param[out] which A pointer to an integer.  The integer will be set to 
 *            -1 if the object was not parseable or if no structure was 
 *            provided in ppObjs to hold the parsed item.
 * 
 * @returns 0 on success or a positive error code if there is a problem.
 * 
 * @memberof OutOfBand
 */
DasErrCode OutOfBand_decode(DasBuf* pBuf, OutOfBand** ppObjs, int* which);

#ifdef __cplusplus
}
#endif

#endif /* _das_oob_p_h_ */
