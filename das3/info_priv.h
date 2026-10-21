/* Copyright (C) 2026 Chris Piker <chris-piker@uiowa.edu>
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

/* Private helpers for the info and toStr printers.  Not installed.
 *
 * nNeed is the running length the output would have with unlimited room.
 * Text goes to sBuf + nNeed while it fits, the buffer is always left NUL
 * terminated, and counting continues after it fills.  So the final nNeed is
 * exactly what an _info caller must allocate, less the NUL, and a toStr
 * function knows it was cut off when nNeed >= uLen.  A sub-printer can be
 * composed in with:
 *
 *    n += Sub_info(pSub, _prn_at(sBuf, uLen, n), _prn_rem(uLen, n));
 *
 * nNeed stays an int, as snprintf returns; uLen is a size_t, as snprintf
 * takes.  Comparisons never compute uLen - 1, which wraps when uLen is 0.
 */

#ifndef _das_info_priv_h_
#define _das_info_priv_h_

#include <stdarg.h>
#include <stddef.h>
#include <stdio.h>

/* Where the next byte goes, or NULL once full; snprintf(NULL, 0, ...) is the
   standard way to only count, and uLen may be 0 with sBuf NULL. */
static inline char* _prn_at(char* sBuf, size_t uLen, int nNeed)
{
	return ((size_t)nNeed < uLen) ? sBuf + nNeed : NULL;
}

static inline size_t _prn_rem(size_t uLen, int nNeed)
{
	return ((size_t)nNeed < uLen) ? uLen - (size_t)nNeed : 0;
}

/* Append formatted text, returns the new nNeed */
static inline int _prn_catf(char* sBuf, size_t uLen, int nNeed, const char* sFmt, ...)
{
	va_list ap;
	va_start(ap, sFmt);
	int n = vsnprintf(_prn_at(sBuf, uLen, nNeed), _prn_rem(uLen, nNeed), sFmt, ap);
	va_end(ap);
	return (n < 0) ? nNeed : nNeed + n;
}

/* Append sSrc with sIndent inserted after every newline, returns the new
   nNeed.  This is how a container nests a member's multi-line description
   without the member needing to know its depth. */
static inline int _prn_catIndent(
	char* sBuf, size_t uLen, int nNeed, const char* sSrc, const char* sIndent
){
	for(const char* p = sSrc; *p != '\0'; ++p){
		if((size_t)nNeed + 1 < uLen) sBuf[nNeed] = *p;
		++nNeed;
		if(*p == '\n'){
			for(const char* q = sIndent; *q != '\0'; ++q){
				if((size_t)nNeed + 1 < uLen) sBuf[nNeed] = *q;
				++nNeed;
			}
		}
	}
	if(uLen > 0) sBuf[((size_t)nNeed < uLen) ? (size_t)nNeed : uLen - 1] = '\0';
	return nNeed;
}

/* The toStr ending: sBuf if all nNeed bytes fit, NULL if it was cut off */
static inline char* _prn_fit(char* sBuf, size_t uLen, int nNeed)
{
	return ((nNeed >= 0)&&((size_t)nNeed < uLen)) ? sBuf : NULL;
}

/* Index tuple printer for error messages, "(i, j, k)", see array.c.  It
   returns sBuf so it can sit inline in a das_error() argument list. */
char* das_idx_prn(int nRank, ptrdiff_t* pIdx, size_t uLen, char* sBuf);

#endif /* _das_info_priv_h_ */
