/* Copyright (C) 2026 Chris Piker <chris-piker@uiowa.edu>
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

/** @file xport.h
 * Plain bytes from files, pipes and network connections, see DasXport
 */

#ifndef _das_xport_h_
#define _das_xport_h_

#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

#include <das3/defs.h>

#ifdef __cplusplus
extern "C" {
#endif

/** @addtogroup IO
 * @{
 */

/** @class DasXport xport.h <das3/xport.h>
 *
 * A source or sink of plain bytes: a file, a pipe, a network connection,
 * or memory.
 *
 * A transport is what lets one piece of code read from anywhere.  The same
 * loop handles a local file and the body of an https download:
 *
 * @code
 *   DasXport* pSrc;
 *   if(bRemote){
 *       DasHttpResp* pRes = new_DasHttpResp_get(sUrl, "my_prog", pMgr, DASHTTP_TIMEOUT_MIN);
 *       pSrc = DasHttpResp_takeBody(pRes);    // NULL if the request failed
 *       del_DasHttpResp(pRes);
 *   }
 *   else
 *       pSrc = new_DasXport_file(sPath, "r");
 *
 *   char aBuf[16384];
 *   ptrdiff_t nRead;
 *   while((nRead = DasXport_read(pSrc, aBuf, sizeof(aBuf))) > 0)
 *       consume(aBuf, nRead);
 *   if(nRead < 0)
 *       fprintf(stderr, "%s\n", DasXport_error(pSrc));
 *   del_DasXport(pSrc);
 * @endcode
 *
 * To parse a das stream instead, give the transport to new_DasIO().
 * To pass the bytes straight through to another transport, use
 * DasXport_copyTo().
 *
 * A transport hides what is underneath it.  There is no way to get the
 * socket, the TLS session or the FILE* back out, and none is needed:
 * closing the transport shuts all of it down properly.  That is also what
 * makes it safe to pass across a DLL boundary, even when the program links
 * a different TLS library than das2C does.
 *
 * Read and write failures do not call daserrf().  They return -1 and
 * DasXport_error() says why, so the program decides whether a dropped
 * connection is fatal.  Constructors report bad arguments through
 * daserrf() like every other constructor.
 *
 * DasXport is not thread safe; do not share one between threads without
 * locking.
 */
typedef struct das_xport_t DasXport;

/** Open a file as a transport
 *
 * Files are opened in binary mode on every platform.  The transport owns
 * the file and closes it with del_DasXport() or DasXport_close().
 *
 * @param sPath The file to open
 * @param sMode "r" to read it, "w" to create or truncate it for writing
 * @returns A new transport, or NULL if the file could not be opened
 * @memberof DasXport
 */
DAS_API DasXport* new_DasXport_file(const char* sPath, const char* sMode);

/** Use an open C stream as a transport
 *
 * The stream is borrowed.  Closing or deleting the transport flushes it
 * but does not close it, so this works for stdin and stdout as well as for
 * files the program will keep using.  The stream is switched to binary
 * mode, as new_DasIO_cfile() does; that only matters on Windows, where text
 * mode would alter the bytes.
 *
 * @param pFile An open stream
 * @param sMode "r" or "w", matching how pFile was opened
 * @returns A new transport, or NULL on a bad argument
 * @memberof DasXport
 */
DAS_API DasXport* new_DasXport_cfile(FILE* pFile, const char* sMode);

/** Read standard input as a transport
 *
 * Shorthand for new_DasXport_cfile(stdin, "r"), the first line of most
 * filter programs:
 *
 * @code
 *   DasIO* pIn = new_DasIO(PROG, new_DasXport_stdin(), NULL);
 * @endcode
 *
 * stdin is borrowed and switched to binary mode.
 *
 * @returns A new transport open for reading, or NULL if allocation fails
 * @memberof DasXport
 */
DAS_API DasXport* new_DasXport_stdin(void);

/** Write standard output as a transport
 *
 * Shorthand for new_DasXport_cfile(stdout, "w").  stdout is borrowed and
 * switched to binary mode; closing the transport flushes it.
 *
 * @returns A new transport open for writing, or NULL if allocation fails
 * @memberof DasXport
 */
DAS_API DasXport* new_DasXport_stdout(void);

/** Use a connected socket as a transport
 *
 * The socket must be a connected stream socket, TCP or a Unix domain
 * socket.  Datagram sockets (UDP) do not work for das streams, a lost
 * datagram leaves the reader out of step with the packets.  Neither do
 * serial ports, pipes or FIFOs, which are not sockets; fdopen() those and
 * use new_DasXport_cfile().
 *
 * The socket is borrowed.  Closing or deleting the transport leaves it
 * open for its owner, who shuts it down.
 *
 * Not available in builds without networking, such as WebAssembly.
 *
 * @param nSock The socket.  intptr_t so a Windows SOCKET fits; on POSIX
 *        systems pass the int file descriptor as is.
 * @param sMode "r", "w", or "rw" for both
 * @returns A new transport, or NULL on a bad argument
 * @memberof DasXport
 */
DAS_API DasXport* new_DasXport_sock(intptr_t nSock, const char* sMode);

/** Read the output of a command as a transport
 *
 * The command runs through the shell, /bin/sh on POSIX systems, and its
 * standard output becomes the transport's input.  The transport owns the
 * pipe.  A command that exits with a non-zero status is logged as a
 * warning when the transport closes; what it wrote before that has already
 * been read.
 *
 * @param sCmd The command line to run
 * @returns A new transport open for reading, or NULL if the command could
 *          not be started
 * @memberof DasXport
 */
DAS_API DasXport* new_DasXport_cmd(const char* sCmd);

/** Use memory you already have as a transport
 *
 * The memory is borrowed and must stay put until the transport is deleted.
 * A reader returns the uLen bytes and then the end of input.  A writer
 * fills the memory from the start; a write that would go past uLen fails,
 * nothing is cut short.
 *
 * @param pMem The memory
 * @param uLen Its size in bytes
 * @param sMode "r" or "w"
 * @returns A new transport, or NULL on a bad argument
 * @memberof DasXport
 */
DAS_API DasXport* new_DasXport_mem(void* pMem, size_t uLen, const char* sMode);

/** Read up to uLen bytes
 *
 * Like read(2), this may return fewer bytes than asked for when that is all
 * that has arrived so far; that is not the end of the input.  Keep reading
 * until it returns 0.
 *
 * @param pThis The transport
 * @param pOut  Receives the bytes
 * @param uLen  Size of pOut
 * @returns The number of bytes read, 0 at the end of the input, or -1 if
 *          the read failed, see DasXport_error()
 * @memberof DasXport
 */
DAS_API ptrdiff_t DasXport_read(DasXport* pThis, void* pOut, size_t uLen);

/** Write all uLen bytes
 *
 * Unlike write(2), this does not return early with part of the data sent;
 * it writes everything or fails.
 *
 * @param pThis The transport
 * @param pIn   The bytes to send
 * @param uLen  How many
 * @returns uLen, or -1 if the write failed, see DasXport_error().  A
 *          transport opened only for reading always fails.
 * @memberof DasXport
 */
DAS_API ptrdiff_t DasXport_write(DasXport* pThis, const void* pIn, size_t uLen);

/** Progress callback for DasXport_copyTo()
 *
 * Follows the DasIOProc callback convention: a non-zero return stops the copy
 * and becomes the return value of DasXport_copyTo().  Use a code of your
 * own rather than DASERR_IO, otherwise a deliberate stop can't be told from
 * a read or write failure.
 *
 * @param nBytes The number of bytes copied so far
 * @param pUser  The opaque pointer handed to DasXport_copyTo()
 * @returns DAS_OKAY to keep copying, anything else to stop
 */
typedef das_errcode (*dasfn_onProgress)(int64_t nBytes, void* pUser);

/** Copy everything that remains in one transport to another
 *
 * Only one chunk is held in memory at a time, so inputs of any size may be
 * copied.  Neither transport is closed.  When the copy fails, the
 * transport that failed is the one with a DasXport_error() message.
 *
 * @code
 *   DasXport* pOut = new_DasXport_cfile(stdout, "w");
 *   DasXport_copyTo(pSrc, pOut, NULL, NULL, NULL);
 *   if(DasXport_close(pOut) != DAS_OKAY)   // a full disk shows up here
 *       fprintf(stderr, "%s\n", DasXport_error(pOut));
 * @endcode
 *
 * @param pThis      The source
 * @param pDest      The destination
 * @param onProgress If not NULL, called after each chunk is written.  A
 *                   non-zero return stops the copy.
 * @param pUser      Passed through to onProgress untouched
 * @param pCopied    If not NULL, receives the number of bytes copied.  Set
 *                   even on failure or an early stop.
 * @returns DAS_OKAY at the end of the source, DASERR_IO if a read or write
 *          failed, or whatever onProgress returned if it stopped the copy
 * @memberof DasXport
 */
DAS_API das_errcode DasXport_copyTo(
	DasXport* pThis, DasXport* pDest, dasfn_onProgress onProgress, void* pUser,
	int64_t* pCopied
);

/** Finish with a transport, reporting whether that went well
 *
 * Flushes anything not yet written and releases the connection, file or
 * pipe.  For a writer this is where the last bytes go out, so check the
 * return value.  A second call does nothing and returns DAS_OKAY.
 *
 * @returns DAS_OKAY, or DASERR_IO if the final flush or close failed, see
 *          DasXport_error()
 * @memberof DasXport
 */
DAS_API das_errcode DasXport_close(DasXport* pThis);

/** Get the reason the transport failed
 *
 * @returns A message suitable for showing a user, or NULL if nothing has
 *          failed.  Only the first failure is kept, later ones are usually
 *          its fallout.  Owned by the transport.
 * @memberof DasXport
 */
DAS_API const char* DasXport_error(const DasXport* pThis);

/** Close a transport, if that has not been done, and free it
 *
 * @param pThis The transport.  May be NULL, in which case nothing is done.
 * @memberof DasXport
 */
DAS_API void del_DasXport(DasXport* pThis);

/** @} */

#ifdef __cplusplus
}
#endif

#endif /* _das_xport_h_ */
