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

/* The DasBuf layout, for library code that keeps a buffer on the stack.
 *
 * Code outside buffer.c should still go through the methods; the layout is
 * here so a per-value path like DasPlane_setTimeValue() can wrap a local
 * array without a malloc each call, not so the fields can be poked. */

#ifndef _das_buffer_p_h_
#define _das_buffer_p_h_

#include "buffer.h"

#ifdef __cplusplus
extern "C" {
#endif

struct das_buffer {
	char* sBuf;
	size_t uLen;
	char* pWrite;            /* NULL marks a read-only buffer */
	const char* pReadBeg;
	const char* pReadEnd;    /* below pWrite after DasBuf_strip() */
	size_t uWrap;

	/* sBuf came from new_DasBuf() and del_DasBuf() frees it.  Kept apart
	   from pWrite: a writable buffer over caller memory owns nothing, and
	   inferring ownership from pWrite once freed such memory. */
	bool bOwned;
};

/* new_DasBuf_wrap() into a struct the caller already has, usually a local.
 * Same modes and same promise not to touch pData when setting up.  Any
 * prior contents of pThis are ignored.  The result owns nothing and is
 * never passed to del_DasBuf(). */
DasErrCode _DasBuf_initWrap(DasBuf* pThis, void* pData, size_t uLen, const char* sMode);

#ifdef __cplusplus
}
#endif

#endif /* _das_buffer_p_h_ */
