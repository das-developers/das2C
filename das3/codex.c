/* Copyright (C) 2026   Chris Piker <chris-piker@uiowa.edu>
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

/* codex.c -- CODec EXtensions.  v3.0 is a placeholder: nothing can be registered
 * yet, so every entry point reports "no codec available" and an undecodable blob
 * fails loud upstream (dataset_hdr3 / var_ary).  The registry, dlopen loader, and
 * real instances arrive with the first codec (png, v3.1); see codex.h. */

#define _POSIX_C_SOURCE 200112L

#include "codex_p.h"

/* No codec is ever registered in v3.0. */
bool das_codex_supported(const char* sMime)
{
	return false;
}

/* No registry, so no path to report. */
const char* das_codex_path(const char* sMime)
{
	return NULL;
}

/* Never an instance to make yet; the caller treats NULL as "no extension" and
 * fails loud with the mime in hand. */
DasCodex* new_DasCodex(
	const char* sMime, das_val_type vtStore, int nRank, const ptrdiff_t* pShape
){
	return NULL;
}

/* ------------------------------------------------------------------------- */
/* Member dispatch.  No instance can exist in v3.0, these are the interface */

das_val_type DasCodex_storage(const DasCodex* pThis)
{
	return pThis->storage(pThis);
}

das_errcode DasCodex_path(const DasCodex* pThis, char* sBuf, size_t uLen)
{
	return pThis->path(pThis, sBuf, uLen);
}

das_errcode DasCodex_mime(const DasCodex* pThis, char* sBuf, size_t uLen)
{
	return pThis->mime(pThis, sBuf, uLen);
}

das_errcode DasCodex_properties(
	const DasCodex* pThis, DasAry* pOut, size_t* pPropsOut
){
	return pThis->properties(pThis, pOut, pPropsOut);
}

das_errcode DasCodex_decode(
	DasCodex* pThis, const ubyte* pBlob, size_t uLen, DasAry* pAry,
	size_t* pAppended
){
	return pThis->decode(pThis, pBlob, uLen, pAry, pAppended);
}

das_errcode DasCodex_encode(
	DasCodex* pThis, const DasAry* pItems, const ptrdiff_t* pLoc,
	DasBuf* pBuf, size_t* pAppended
){
	return pThis->encode(pThis, pItems, pLoc, pBuf, pAppended);
}

void del_DasCodex(DasCodex* pThis)
{
	if(pThis != NULL) pThis->del(pThis);
}
