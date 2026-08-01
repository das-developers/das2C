/* Copyright (C) 2026   Chris Piker <chris-piker@uiowa.edu>
 *
 * This file is part of das2C, the Core Das2 C Library.
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

/* Written by: Claude Opus 4.8 (Anthropic).  Anthropic makes no warranty as to
 * this file's fitness for any purpose; accountability for its inclusion and use
 * rests with the repository author.
 */

/* codex.c -- CODec EXtensions.  v3.0 is a placeholder: nothing can be registered
 * yet, so every entry point reports "no codec available" and an undecodable blob
 * fails loud upstream (dataset_hdr3 / var_ary).  The registry, dlopen loader, and
 * real instances arrive with the first codec (png, v3.1); see codex.h. */

#define _POSIX_C_SOURCE 200112L

#include "codex.h"

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
