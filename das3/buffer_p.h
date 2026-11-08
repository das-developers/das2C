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

/* The DasBuf layout, for lib code managing DasBuf structures on the stack.
 *
 * Code outside buffer.c should still use member functions. The layout is
 * here so that DasPlane_setTimeValue() and others can wrap a local array
 * without a malloc on each call, not for direct field access.  
 *
 * Seriously, use the functions. The compiler will optimize out function
 * calls anyway when it's safe to do so.
 */

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

	/* True if sBuf came from new_DasBuf() and del_DasBuf() frees it. */
	bool bOwned;

	/* Growth limit of an elastic buffer, 0 for a fixed buffer.  Only owned
	   storage can grow, so uMax != 0 implies bOwned. */
	size_t uMax;
};

/* Initialize a stack DasBuf object. 
 * Does *not* zero pData on startup since the caller may just want to 
 * provide a nice interface over read-only memory. */
DasErrCode _init_DasBuf_wrap(DasBuf* pThis, void* pData, size_t uLen, const char* sMode);

#ifdef __cplusplus
}
#endif

#endif /* _das_buffer_p_h_ */
