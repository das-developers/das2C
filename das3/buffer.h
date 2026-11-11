/* Copyright (C) 2015-2017 Chris Piker <chris-piker@uiowa.edu>
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

/** @file buffer.h
 * Byte buffers with separate read and write points, see DasBuf
 */

#ifndef _das_buffer_h_
#define _das_buffer_h_

#include <stdlib.h>
#include <stdbool.h>
#include <stdio.h>

#include "util.h"

#ifdef	__cplusplus
extern "C" {
#endif
	
/** @addtogroup IO
 * @{
 */
	
/** @class DasBuf buffer.h <das3/buffer.h>
 *
 * A block of memory, written at one end and read from the other.
 *
 * Each buffer tracks a write point and a read point.  Writes append at the
 * write point; reads consume from the read point.  Several functions can
 * add to one buffer in turn, encoding a packet piece by piece for example,
 * and several can read from it without copying the bytes or inserting NUL
 * terminators to stop a parser.
 *
 * Create an empty buffer with new_DasBuf(), or with new_DasBuf_elastic() if
 * it should grow when writes run out of room.  To use existing memory, such
 * as a packet from a socket library or a JavaScript ArrayBuffer under
 * WebAssembly, wrap it with new_DasBuf_wrap() rather than copying it in:
 *
 * @code
 *   DasBuf* pBuf = new_DasBuf_wrap(pBytes, uLen, "r");   // pBytes is not copied
 *   DasDs_decodeData(pDs, pBuf);
 *   del_DasBuf(pBuf);                                    // pBytes is not freed
 * @endcode
 *
 * Zero-copy access is also available.  DasBuf_prepare() and DasBuf_commit()
 * let callers write directly into the buffer, and DasBuf_data() and
 * DasBuf_consume() let them read in place.  The ordinary DasBuf_write(),
 * DasBuf_printf() and DasBuf_read() functions copy data from the input
 * source.
 *
 * Pointers obtained this way are only good while nothing writes to the
 * buffer, since an elastic buffer may move its storage to grow.  Code that
 * receives a buffer from elsewhere and wants to hold on to such a pointer
 * can ask with DasBuf_isPinned().
 *
 * DasBuf is not thread safe; do not share one between threads without
 * locking.
 */
typedef struct das_buffer DasBuf;

/** @} */

/** Create an empty buffer
 *
 * @param uLen Size of the buffer in bytes.  It does not grow, see
 *        new_DasBuf_elastic() for one that does.
 * @returns A new zero filled buffer to be freed with del_DasBuf(), or NULL
 *          if allocation fails
 *
 * @memberof DasBuf
 */
DAS_API DasBuf* new_DasBuf(size_t uLen);

/** Create an empty buffer that grows as needed, up to a limit
 *
 * Writes that don't fit enlarge the buffer instead of failing, until it
 * would pass uMax; after that they fail just as they would on a full
 * new_DasBuf() buffer.  The buffer never shrinks, so one that is reused for
 * similar sized chunks of data soon stops growing:
 *
 * @code
 *   // Starts at 16 KiB, may reach 16 MiB
 *   DasBuf* pBuf = new_DasBuf_elastic(16384, 16777216);
 * @endcode
 *
 * Growing can move the storage, so pointers from DasBuf_data() and
 * DasBuf_prepare() go stale on any write.  DasBuf_isPinned() is false for
 * these buffers.  DasBuf_capacity() and DasBuf_writeSpace() report the
 * current allocation, not the limit.
 *
 * @param uLen Initial size of the buffer in bytes
 * @param uMax Largest size the buffer may grow to, at least uLen
 * @returns A new zero filled buffer to be freed with del_DasBuf(), or NULL
 *          if allocation fails or uMax is less than uLen
 *
 * @memberof DasBuf
 */
DAS_API DasBuf* new_DasBuf_elastic(size_t uLen, size_t uMax);

/** Create a buffer over caller-owned memory
 *
 * The memory is neither copied nor freed by del_DasBuf(), and must remain
 * valid until the buffer is deleted.  Creating the buffer never modifies
 * it, in either mode.
 *
 * In "r" mode the contents may be read, but writes are refused.
 *
 * @code
 *   DasBuf* pBuf = new_DasBuf_wrap((void*)pConstBytes, uLen, "r");
 * @endcode
 *
 * In "w" mode the memory is treated as empty write space.  Existing
 * contents are not cleared; call DasBuf_zero() first if the result will be
 * read as a C string.
 *
 * This is the DasBuf analog of fmemopen().
 *
 * @param pData Caller-owned memory
 * @param uLen  Size of pData in bytes
 * @param sMode "r" to read the bytes, "w" to write over them
 * @returns A new buffer, or NULL if sMode is invalid or allocation fails
 *
 * @memberof DasBuf
 */
DAS_API DasBuf* new_DasBuf_wrap(void* pData, size_t uLen, const char* sMode);

/** Reset a buffer for reuse
 *
 * Moves the read and write points back to the start and zeroes the bytes
 * written so far, which is sufficient for a buffer filled only through
 * DasBuf writes.  For a read-only buffer only the read point is reset, so
 * it can be read again from the beginning.
 *
 * @memberof DasBuf
 */
DAS_API void DasBuf_reinit(DasBuf* pThis);

/** Zero the entire buffer and reset it
 *
 * Like DasBuf_reinit(), but clears all DasBuf_capacity() bytes rather than
 * only those written.  Use it on a "w" wrap of memory with prior contents;
 * for a buffer filled only through DasBuf writes, DasBuf_reinit() is
 * equivalent and cheaper.
 *
 * @returns DAS_OKAY, or DASERR_BUF for a read-only buffer, whose memory
 *          may not be writable
 * @memberof DasBuf
 */
DAS_API das_errcode DasBuf_zero(DasBuf* pThis);

/** Free a buffer
 *
 * Storage allocated by new_DasBuf() is freed; memory passed to
 * new_DasBuf_wrap() is not.
 *
 * @param pThis The buffer to free.  May be NULL, in which case nothing is
 *        done.
 *
 * @memberof DasBuf
 */
DAS_API void del_DasBuf(DasBuf* pThis);

/** Get the total size of the buffer in bytes
 *
 * @returns The size given to new_DasBuf() or new_DasBuf_wrap(), or the
 *          current size of an elastic buffer
 * @memberof DasBuf
 */
DAS_API size_t DasBuf_capacity(const DasBuf* pThis);

/** Does the buffer's storage stay put?
 *
 * A pinned buffer keeps the same memory for its whole life, so a pointer
 * from DasBuf_data() or DasBuf_prepare() keeps pointing into it, though the
 * bytes there may be overwritten.  Buffers created from new_DasBuf() and
 * new_DasBuf_wrap() are pinned. Calling new_DasBuf_elastic() creates an
 * elastic buffer that allocates as necessary.  Writing to an elastic 
 * buffer may move everything.
 *
 * If a buffer is not pinned, don't keep a pointer into it across a write, 
 * keep an offset instead. To do so, save DasBuf_written() before writing,
 * then add it to a fresh DasBuf_data() pointer afterwards.
 *
 * @code
 *   size_t uStart = DasBuf_written(pBuf);   // survives growth, a pointer doesn't
 *   DasBuf_printf(pBuf, "%.3f", rVal);
 *   size_t uLen;
 *   const ubyte* pAll = DasBuf_data(pBuf, &uLen);
 *   const char* sVal = (const char*)pAll + uStart;  // assumes nothing consumed yet
 * @endcode
 *
 * @memberof DasBuf
 */
DAS_API bool DasBuf_isPinned(const DasBuf* pThis);

/** Is the buffer closed to writing?
 *
 * Only new_DasBuf_wrap() with mode "r" makes a read-only buffer.  DasBuf 
 * modes are fixed after construction.  It is an error to write to a read
 * only buffer.  Use this function to determine if the buffer is read only.
 *
 * @memberof DasBuf
 */
DAS_API bool DasBuf_isReadOnly(const DasBuf* pThis);

/** Add a string to the buffer 
 * @param pThis the buffer to receive the bytes
 * @param sStr the null-terminated string to write
 * @returns 0 if the operation succeeded, a positive error code otherwise.
 * @memberof DasBuf
 */
DAS_API das_errcode DasBuf_puts(DasBuf* pThis, const char* sStr);

/** Write formatted strings to the buffer 
 * @param pThis the buffer to receive the bytes
 * @param sFmt an sprintf style format string
 * @returns 0 if the operation succeeded, a positive error code otherwise.
 * @memberof DasBuf
 */
DAS_API das_errcode DasBuf_printf(DasBuf* pThis, const char* sFmt, ...);

/** Write text to the buffer, word wrapped
 *
 * Runs of white space between words become one space, or a line break plus
 * sIndent when the next word would pass nWidth.  Newlines in sTxt are kept.
 * A word longer than the line is written whole on its own line.  The first
 * line is indented too.
 *
 * @todo: Widths count bytes, not characters, so lines holding multi-byte 
 * UTF-8 characters wrap early this needs to be fixed.  
 * 
 * Words are never split, so neither are characters.
 *
 * @param pThis the buffer to receive the text
 * @param nWidth the line length in bytes, at least 20 more than sIndent's
 * @param sIndent prefix for every line, may be NULL.  Only the first 80 bytes
 *        are used.
 * @param sTxt the null-terminated text to write
 * @returns 0 if the operation succeeded, a positive error code otherwise,
 *        including when the buffer fills part way through.
 * @memberof DasBuf
 */
DAS_API das_errcode DasBuf_wrapWrite(
	DasBuf* pThis, int nWidth, const char* sIndent, const char* sTxt
);
	
/** Add generic data to the buffer 
 * @param pThis the buffer to receive the bytes
 * @param pData a pointer to the bytes to write
 * @param uLen the number of bytes to write
 * @returns DAS_OKAY if the operation succeeded, a positive error code otherwise.
 * @memberof DasBuf
 */
DAS_API das_errcode DasBuf_write(DasBuf* pThis, const void* pData, size_t uLen);

/** Get a pointer for writing directly into the buffer
 *
 * The zero-copy counterpart to DasBuf_write().  
 * 
 * The usage pattern is to: 
 *   1. Call DasBuf_prepare() to say how many bytes you mean to write
 *   2. Call DasBuf_commit() to say how many bytes you wrote. 
 * Uncommitted bytes are not readable:
 *
 * @code
 *   size_t uSpace;
 *   ubyte* pSpace = DasBuf_prepare(pBuf, 4096, &uSpace);
 *   if(pSpace != NULL){
 *       size_t uGot = fill(pSpace, uSpace);   // may fill less than uSpace
 *       DasBuf_commit(pBuf, uGot);
 *   }
 * @endcode
 *
 * @note Calling DasBuf_prepare() can grow an elastic buffer. Because of
 * this, the backing storage my move.  Direct pointers into the buffer 
 * should be considered invalid after this function is called just as if
 * the buffer were reinitialized.
 * 
 * Calling commit does not change the backing storage.
 *
 * @param pThis the buffer
 * 
 * @param uWant the number of bytes the caller needs room for.  Use 0 to
 *        take whatever space is free now without growing the buffer.
 * 
 * @param pSpace Receives the number of bytes available, which may be more
 *        than uWant, or 0 on failure
 * 
 * @returns Pointer to the first free byte, or NULL if the buffer is
 *        read-only, or can't provide uWant bytes (or any, when uWant is 0).
 *        Only the read-only case calls das_error(). A buffer without room
 *        returns NULL, but does not call das_error.
 *
 * @memberof DasBuf
 */
DAS_API ubyte* DasBuf_prepare(DasBuf* pThis, size_t uWant, size_t* pSpace);

/** Add bytes written through DasBuf_prepare() to the buffer contents
 *
 * As with DasBuf_write(), the end of the readable data moves to the new
 * write point, so any earlier DasBuf_strip() is undone.
 *
 * @param pThis the buffer
 * @param uLen number of bytes written
 * @returns DAS_OKAY, or DASERR_BUF if uLen is more than the prepared space,
 *        in which case nothing changes
 *
 * @memberof DasBuf
 */
DAS_API das_errcode DasBuf_commit(DasBuf* pThis, size_t uLen);

/** Get the size of the data in the buffer.
 * @returns the number of bytes written to the buffer
 * @memberof DasBuf
 */
DAS_API size_t DasBuf_written(const DasBuf* pThis);

/** Get the remaining write space in the buffer.
 * 
 * @param pThis The buffer
 * @return The number of bytes that may be still be written to the buffer.
 */
DAS_API size_t DasBuf_writeSpace(const DasBuf* pThis);

/** Get the number of bytes remaining from the read begin point to the read end
 * point.
 * Normally this returns the difference between the read point and the 
 * write point but some operations such as DasBuf_strip() reduce the read
 * end point below the write point.
 * 
 * @returns Read length remaining.
 * @memberof DasBuf
 */
DAS_API size_t DasBuf_unread(const DasBuf* pThis);

/** Adjust read points so that the data starts and ends on non-space values.
 * This is handy if the buffer contains string data.
 * 
 * @warning If any new bytes are added after the buffer has been stripped then
 * the right read point will be reset to the end of valid data.
 * 
 * @returns The number of bytes left to read after moving the read boundaries.
 *          The return value is the same as what you would get by calling
 *          DasBuf_unread() immediately after this function.
 * @memberof DasBuf
 */
DAS_API size_t DasBuf_strip(DasBuf* pThis);

/** Read bytes from a buffer 
 * Copies bytes out of a buffer and increments the read point.  As soon as the
 * read point hits the end of valid data no more bytes are copied.
 * 
 * @returns The number of bytes copied out of the buffer.
 * @memberof DasBuf
 */
DAS_API size_t DasBuf_read(DasBuf* pThis, char* pOut, size_t uOut);

/** Get a pointer to the unread bytes without copying them
 *
 * The zero-copy counterpart to DasBuf_read().  The read point is not
 * moved; once some or all of the bytes have been processed, advance past
 * them with DasBuf_consume():
 *
 * @code
 *   size_t uLen;
 *   const ubyte* pData = DasBuf_data(pBuf, &uLen);
 *   if(pData != NULL){
 *       size_t uUsed = parse(pData, uLen);   // may use less than uLen
 *       DasBuf_consume(pBuf, uUsed);
 *   }
 * @endcode
 *
 * The pointer remains valid until the next call that writes to or resets
 * the buffer.
 *
 * @param pThis the buffer
 *
 * @param pLen Receives the number of unread bytes, 0 when there are none
 *
 * @returns A pointer to the first unread byte, or NULL if there are no
 *        unread bytes
 *
 * @memberof DasBuf
 */
DAS_API const ubyte* DasBuf_data(const DasBuf* pThis, size_t* pLen);

/** Advance the read point past bytes obtained from DasBuf_data()
 *
 * @param pThis the buffer
 * @param uLen number of unread bytes to skip
 * @returns DAS_OKAY, or DASERR_BUF if uLen is more than DasBuf_unread(),
 *        in which case the read point does not move
 *
 * @memberof DasBuf
 */
DAS_API das_errcode DasBuf_consume(DasBuf* pThis, size_t uLen);

/** Peak at bytes from a buffer
 * Copies bytes out of a buffer but does *not* increment the read point.
 * 
 * @returns The number of bytes copied out of the buffer.
 * @memberof DasBuf
 */
DAS_API size_t DasBuf_peek(const DasBuf* pThis, char* pOut, size_t uOut);

/** Peak the last byte in the buffer
 * @returns the last byte in the buffer as an int, or -1 if there are
 *          no bytes in the buffer.
 * @memberof DasBuf
 */
DAS_API int DasBuf_last(const DasBuf* pThis);

/** Return a pointer to the start of the current line and advance the read
 * point to the start of the next line.
 * 
 * The main use is for reading lines of character data, though any delimited
 * byte stream can be read with this function.
 * 
 * @param pThis The DasBuf to read
 * @param sDelim the line delimiter, typically this is just a single character
 *         string, but any string may be considered the line delimiter
 * @param uDelimLen the length of the record deliminator in bytes
 * @param pLen A pointer to a location to receive the line length, excluding
 *         the delimiter.  The saved value will be zero for empty lines.
 * @return A pointer to the start of the current line, or NULL if no further 
 *          lines are present in the buffer.
 */
DAS_API const char* DasBuf_readRec(
	DasBuf* pThis, const char* sDelim, size_t uDelimLen, size_t* pLen
);


/** Get the offset of the read position
 * 
 * @param pThis - The buffer to query
 * @returns The difference between the read point and the base of the buffer
 */
DAS_API size_t DasBuf_readOffset(const DasBuf* pThis);

/** Set the offset of the read position
 * 
 * @param pThis - The buffer in question
 * @param uPos - The new read offset from be beginning of the buffer
 * @returns 0 on success an positive error code if uPos makes no sense for the
 *          buffers current state
 */
DAS_API das_errcode DasBuf_setReadOffset(DasBuf* pThis, size_t uPos);

#ifdef	__cplusplus
}
#endif

#endif	/* _das_buffer_h_ */

