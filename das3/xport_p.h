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

/* Transports: where DasIO and the HTTP client get bytes and send them.
 *
 * Everything that knows what a FILE*, socket or SSL* is lives in xport.c.
 * DasIO, http.c and the HTTP body readers go through the vtable and never
 * branch on the kind; a new transport (webdav, a wasm fetch() backend,
 * websockets) is a new kind here, not a new case in each of them.  This is
 * also what keeps SSL* off the public API: a DasHttpResp or DasIO holds a
 * DasXport*, never the OpenSSL object.
 *
 * Compression is a kind too.  An inflate or deflate transport wraps another
 * and owns it, so zlib is written once and works over every transport
 * instead of repeating the transport dispatch in each zlib function.  It
 * serves the stream level compression="deflate" attribute, which has seen
 * little use; the same filter would serve HTTP Content-Encoding if that is
 * ever wanted.
 *
 * The public face is xport.h: the opaque type, read, write, copy, close and
 * two file constructors.  The vtable, the other kinds and the pushback stay
 * here.  Caller supplied read/write callbacks, a public kind, are additive
 * and can follow.
 *
 * Failure policy: the sub classes never call das_error() for I/O failures.  
 * They return -1 and leave a message in the base, the caller decides whether
 * a dropped connection is fatal.  Constructors do call das_error() for bad
 * arguments and allocation failures, like every other constructor. */

#ifndef _das_xport_p_h_
#define _das_xport_p_h_

#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>
#include <stdio.h>

#include "buffer.h"
#include "xport.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Big enough for an OpenSSL error queue dump or a strerror() with context */
#define DASXPORT_ERR_SZ 256

#define DASXPORT_RD 0x01
#define DASXPORT_WR 0x02

/* Each slot takes the base pointer, the kind casts it on the first line.
 *
 * read   >0 bytes, 0 end of input, -1 failure with the message set.  Short
 *        reads are allowed; _DasXport_readFull() loops for exact counts.
 *        Pushed back bytes are handled by the base, kinds never see them.
 * write  uLen or -1.  The kind loops over partial writes itself.
 * close  Flush, then release what the kind holds: SSL_shutdown, SSL_free
 *        and the socket for ssl; pclose for cmd; fclose for an owned FILE*,
 *        fflush only for one that is not; for a filter, finish the zlib
 *        stream and close the inner transport.  Called at most once.
 * free   Optional, kind-specific heap other than the object itself.
 */
typedef struct das_xport_vt {
	const char* sKind;    /* "file", "cmd", "mem", "sock", "ssl", "inflate" ... */
	ptrdiff_t  (*read) (DasXport* pBase, void* pOut, size_t uLen);
	ptrdiff_t  (*write)(DasXport* pBase, const void* pIn, size_t uLen);
	DasErrCode (*close)(DasXport* pBase);
	void       (*free) (DasXport* pBase);
} das_xport_vt;

/* The base, first member of every kind */
struct das_xport {
	const das_xport_vt* pVt;
	unsigned int uFlags;     /* DASXPORT_RD and/or DASXPORT_WR */
	bool     bClosed;
	bool     bFailed;        /* sErr holds the first failure */
	int64_t  nMoved;         /* bytes read or written through the vtable,
	                            DasIO's offset and copy-body counts */

	/* Pushback.  Bytes a reader took but did not use, returned first by the
	   next read.  http.c reads past the end of the headers and hands the
	   start of the body back here; DasIO does the same before swapping in an
	   inflate filter.  Allocated on first use. */
	ubyte*   pBack;
	size_t   uBackLen;
	size_t   uBackPos;

	char     sErr[DASXPORT_ERR_SZ];
};

/* ************************************************************************* */
/* For library callers, beside the public calls in xport.h.  DasXport_read()
 * returns pushed back bytes before reading anything new. */

/* Loop over short reads until uLen bytes, end of input, or failure.
 * Returns the count, short only at the end of input, or -1. */
ptrdiff_t _DasXport_readFull(DasXport* pThis, void* pOut, size_t uLen);

/* Return bytes to the front of the input, the next read gets them first.
 * They need not be the bytes last read, a filter may hand back what it
 * took from its inner transport.  DASERR_IO for a write-only transport. */
DasErrCode _DasXport_unread(DasXport* pThis, const void* pIn, size_t uLen);

/* Read uLen bytes straight into a DasBuf, via DasBuf_prepare() and
 * DasBuf_commit().  Loops like _DasXport_readFull(), since DasIO reads
 * exact lengths; the count is short only at the end of input.  uLen larger
 * than the free space is reported through das_error(), it is a caller bug,
 * not an I/O error.  Returns the count or -1. */
ptrdiff_t _DasXport_fill(DasXport* pThis, DasBuf* pBuf, size_t uLen);

/* Write everything unread in pBuf, consuming it.  Returns the count or -1 */
ptrdiff_t _DasXport_drain(DasXport* pThis, DasBuf* pBuf);


/* ************************************************************************* */
/* For kinds */

/* Fill in the base after allocating the derived struct */
void _DasXport_init(DasXport* pBase, const das_xport_vt* pVt, unsigned int uFlags);

/* Record a failure, the first one sticks.  Always returns -1 so a slot can
 * end with `return _DasXport_fail(pBase, ...);` */
ptrdiff_t _DasXport_fail(DasXport* pBase, const char* sFmt, ...);


/* ************************************************************************* */
/* Kinds.  sMode is "r" or "w", and "rw" for the two network kinds, which
 * carry a request out and the reply back on one connection.  All return
 * NULL after das_error() on bad arguments or no memory; I/O failures come
 * later, through read and write. */

/* The owning forms of two public kinds.  bOwn decides whether close is
 * fclose() / closesocket() or leaves the handle alone.  The public
 * constructors borrow; new_DasXport_file() opens its FILE and passes true,
 * and http.c passes true for the sockets it connects.  new_DasXport_cmd() and
 * new_DasXport_mem() have no private form, a pipe is always owned and memory
 * always borrowed. */
DasXport* _new_DasXport_cfile(FILE* pFile, const char* sMode, bool bOwn);

#ifndef DAS_NO_NETWORK

/* OpenSSL's own tag for SSL, so this header needs no OpenSSL includes */
struct ssl_st;

/* -1 is the invalid socket on both platforms, since INVALID_SOCKET is ~0.
 * It is cast to the OS type only at the recv/send/close calls. */
DasXport* _new_DasXport_sock(intptr_t nSock, const char* sMode, bool bOwn);

/* Takes over the session and its socket.  http.c keeps the SSL_CTX. */
DasXport* _new_DasXport_ssl(struct ssl_st* pSsl, const char* sMode);

#endif

/* Filters.  They own pInner from here on; closing the filter finishes the
 * zlib stream and then closes pInner.  DasIO swaps its pointer when a
 * stream header turns compression on:
 *
 *    pThis->pX = _new_DasXport_inflate(pThis->pX);
 *
 * Bytes already pushed back on pInner are read before anything new, so a
 * reader that went past the header just unreads them before the swap. */
DasXport* _new_DasXport_inflate(DasXport* pInner);
DasXport* _new_DasXport_deflate(DasXport* pInner);

#ifdef __cplusplus
}
#endif

#endif /* _das_xport_p_h_ */
