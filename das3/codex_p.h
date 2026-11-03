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

/* INTERNAL header, not installed.  The codec extension instance: a state
 * pointer and the table of functions a codex implements.  codex.h's
 * DasCodex_* functions dispatch through it. */

#ifndef _das_codex_p_h_
#define _das_codex_p_h_

#include <das3/codex.h>

#ifdef __cplusplus
extern "C" {
#endif

struct das_codex {

	void* pState;   /* instance-private state, freed by del */

	/* Introspection (const). */
	das_val_type (*storage)(const struct das_codex* pThis);
	DasErrCode   (*path)(const struct das_codex* pThis, char* sBuf, size_t uLen);
	DasErrCode   (*mime)(const struct das_codex* pThis, char* sBuf, size_t uLen);

	/* Write the codec's own properties (e.g. image dims, colorspace) into a
	   dense rank-2 ubyte array in DasDesc's packed layout; *pPropsOut = count. */
	DasErrCode (*properties)(
		const struct das_codex* pThis, DasAry* pOut, size_t* pPropsOut
	);

	/* One blob -> samples appended to pAry; *pAppended = items written. */
	DasErrCode (*decode)(
		struct das_codex* pThis, const ubyte* pBlob, size_t uLen,
		DasAry* pAry, size_t* pAppended
	);

	/* One record (at pLoc) -> blob bytes appended to pBuf; *pAppended = bytes.
	   Caller guarantees pLoc subsumes enough items to form one object. */
	DasErrCode (*encode)(
		struct das_codex* pThis, const DasAry* pItems, const ptrdiff_t* pLoc,
		DasBuf* pBuf, size_t* pAppended
	);

	void (*del)(struct das_codex* pThis);  /* free pState + the instance */

};

#ifdef __cplusplus
}
#endif

#endif /* _das_codex_p_h_ */
