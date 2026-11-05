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

/* The HTTP client's side doors.  The DasHttpResp layout stays in http.c.
 *
 * A response holds its connection as a DasXport, see xport_p.h.  That is
 * what keeps SSL* and socket numbers off the public API: nothing outside
 * http.c and xport.c ever sees either one. */

#ifndef _das_http_p_h_
#define _das_http_p_h_

#include "http.h"
#include "xport_p.h"

#ifdef __cplusplus
extern "C" {
#endif

/* An empty response, for TestHttp to read canned replies into.  The real
 * ones come from new_DasHttpResp_get(). */
DasHttpResp* _new_DasHttpResp(void);

/* Read a reply's status line and headers from a transport.
 *
 * Sets the code, keeps the header block for DasHttpResp_header(), and pulls
 * out the media type and file name.  Reading stops exactly at the blank line:
 * whatever arrived past it is the start of the body and is pushed back onto
 * pX, see _DasXport_unread().  This lets the reply be read in chunks instead
 * of peeking, which a transport can't do in general.
 *
 * Returns false, with the error set, on a transport failure, a reply that is
 * not HTTP, or headers too big to keep.  TestHttp feeds it canned replies
 * through a memory transport. */
bool _DasHttpResp_readHdrs(DasHttpResp* pThis, DasXport* pX);

#ifdef __cplusplus
}
#endif

#endif /* _das_http_p_h_ */
