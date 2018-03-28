/** @file buffer.h Utility to assist with encode and decode operations */

#ifndef _das2_buffer_h_
#define _das2_buffer_h_

#include <stdlib.h>
#include <stdbool.h>
#include <stdio.h>

#include "util.h"

#ifdef	__cplusplus
extern "C" {
#endif
	
/** Little buffer class to handle accumulating string data.
 * 
 * DasBuf objects maintain a data buffer with a current write point, a current
 * read point and an end read point.  As data are written to the buffer the
 * write point is incremented as well as the end-of-read point.  This structure 
 * is handy when multiple functions need to contribute encoded data to a single 
 * memory buffer, or when multiple functions need to read from a buffer without
 * memory re-allocations or placing null values to stop parsing.
 * 
 * It is hoped that the use of this class cuts down on alot of data copies and
 * sub-string allocations.
 */
typedef struct das_buffer{
	char* sBuf;
	size_t uLen;
	char* pWrite;
	const char* pReadBeg;
	const char* pReadEnd;
	size_t uWrap;
} DasBuf;

/** Create a new Read-Write buffer on the heap
 * Allocates a new char buffer of the indicated size, call del_DasBuffer() when
 * finished
 * @param uLen The length of the raw buffer to allocate
 * @return a new ::DasBuf allocated on the heap.
 *
 * @memberof DasBuf
 */
DasBuf* new_DasBuf(size_t uLen);

/** Initialize a read-write buffer that points to an external byte array.
 * The write point is reset to the beginning and function zero's all data.  The
 * read point is also set to the beginning.
 * 
 * @param pThis the buffer initialize
 * @param sBuf an pre-allocated character buffer to receive new data
 * @param uLen the length of the pre-allocated buffer
 * 
 * @memberof DasBuf
 */
ErrorCode DasBuf_initReadWrite(DasBuf* pThis, char* sBuf, size_t uLen);

/** Initialize a read-only buffer than points to an external byte array.
 * 
 * This function re-sets the read point for the buffer.
 * 
 * @param pThis the buffer initialize
 * @param sBuf an pre-allocated character buffer to receive new data
 * @param uLen the length of the pre-allocated buffer
 */
ErrorCode DasBuf_initReadOnly(DasBuf* pThis, const char* sBuf, size_t uLen);

/** Re-initialize a buffer including read and write points
 * This version can be a little quicker than init_DasBuffer() because it only
 * zero's out the bytes that were written, not the entire buffer.
 * 
 * @memberof DasBuf
 */
void DasBuf_reinit(DasBuf* pThis);

/** Free a buffer object along with it's backing store.  
 * Don't use this if the ::DasBuffer::sBuf member points to data on the stack
 * if so, your program will crash.
 * 
 * @param pThis The buffer to free.  It's good practice to set this pointer
 *         to NULL after this  function is called
 * 
 * @memberof DasBuf
 */
void del_DasBuf(DasBuf* pThis);

/** Add a string to the buffer 
 * @param pThis the buffer to receive the bytes
 * @param sStr the null-terminated string to write
 * @returns 0 if the operation succeeded, a positive error code otherwise.
 * @memberof DasBuf
 */
ErrorCode DasBuf_puts(DasBuf* pThis, const char* sStr);

/** Write formatted strings to the buffer 
 * @param pThis the buffer to receive the bytes
 * @param sFmt an sprintf style format string
 * @returns 0 if the operation succeeded, a positive error code otherwise.
 * @memberof DasBuf
 */
ErrorCode DasBuf_printf(DasBuf* pThis, const char* sFmt, ...);
	
/** Add generic data to the buffer 
 * @param pThis the buffer to receive the bytes
 * @param pData a pointer to the bytes to write
 * @param uLen the number of bytes to write
 * @returns 0 if the operation succeeded, a positive error code otherwise.
 * @memberof DasBuf
 */
ErrorCode DasBuf_write(DasBuf* pThis, const void* pData, size_t uLen);

/**  Write wrapped utf-8 text to the buffer 
 * 
 * With the exception of explicit newline characters, this function uses white
 * space only to separate words.  Words are not split thus new-lines start at
 * word boundaries. 
 * 
 * The mentality of the function is to produce horizontal "paragraphs" of 
 * text that are space indented.
 * 
 * @param pThis the buffer to receive the text
 * @param nIndent1 the start column for the first line of text
 * @param nIndent the start column for subsequent lines
 * @param nWrap the wrap column, using 80 is recommended
 * @param fmt A printf style format string, may contain utf-8 characters.
 * @returns 0 if the operation succeeded, a positive error code otherwise.  
 */
ErrorCode DasBuf_paragraph(
	DasBuf* pThis, int nIndent1, int nIndent, int nWrap, const char* fmt, ...
);

/** Add generic data to the buffer from a file
 * @returns Then number of bytes actually read, or a negative error code if there
 *          was a problem reading from the file.
 */
int DasBuf_writeFrom(DasBuf* pThis, FILE* pIn, size_t uLen);
	
/** Get the size of the data in the buffer.
 * @returns the number of bytes written to the buffer
 * @memberof DasBuf
 */
size_t DasBuf_written(const DasBuf* pThis);

/** Get the remaining write space in the buffer.
 * 
 * @param pThis The buffer
 * @return The number of bytes that may be still be written to the buffer.
 */
size_t DasBuf_writeSpace(const DasBuf* pThis);

/** Get the number of bytes remaining from the read begin point to the read end
 * point.
 * Normally this returns the difference between the read point and the 
 * write point but some operations such as DasBuf_strip() reduce the read
 * end point below the write point.
 * 
 * @returns Read length remaining.
 * @memberof DasBuf
 */
size_t DasBuf_unread(const DasBuf* pThis);

/** Adjust read points so that the data starts and ends on non-space values.
 * This is handy if the buffer contains string data.
 * 
 * @warning If any new bytes are added after the buffer has been stripped then
 * the right read point will be reset to the end of valid data.
 * 
 * @returns The number of bytes left to read after moving the read boundaries.
 *          The return value is the same as what you would get by calling
 *          DasBuf_remaining() immediately after this function.
 * @memberof DasBuf
 */
size_t DasBuf_strip(DasBuf* pThis);

/** Read bytes from a buffer 
 * Copies bytes into a buffer and increments the read point.  As soon as the
 * read point hits the end of valid data no more bytes are copied.
 * 
 * @returns The number of bytes copied out of the buffer.
 * @memberof DasBuf
 */
size_t DasBuf_read(DasBuf* pThis, char* pOut, size_t uOut);

/** Get the offset of the read position
 * 
 * @param pThis - The buffer to query
 * @returns The difference between the read point and the base of the buffer
 */
size_t DasBuf_readOffset(const DasBuf* pThis);

/** Set the offset of the read position
 * 
 * @param pThis - The buffer in question
 * @param uPos - The new read offset from be beginning of the buffer
 * @returns 0 on success an positive error code if uPos makes no sense for the
 *          buffers current state
 */
ErrorCode DasBuf_setReadOffset(DasBuf* pThis, size_t uPos);

#ifdef	__cplusplus
}
#endif

#endif	/* _das2_buffer_h_ */

