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

/* Module start-up and shut-down hooks.  Not installed.
 *
 * das_init() and das_finish() in util.c are the only callers, so the hooks
 * are declared next to them rather than in each module's public header.  The
 * defining .c files include this too, so the compiler checks each definition
 * against its declaration.
 *
 * None of these are thread safe, they run once from das_init() before any
 * worker threads exist.
 */

#ifndef _das_util_p_h_
#define _das_util_p_h_

#include <stdbool.h>

/* units.c, registers the built-in unit names */
bool das_units_init(const char* sProgName);

/* dft.c, sets up the shared FFTW planning data */
bool das_dft_init(const char* sProgName);

/* http.c, SSL library set-up and tear-down */
bool das_http_init(const char* sProgName);
void das_http_finish(void);

/* tt2000.c, loads the leap second table before any conversion runs.  Set
   CDF_LEAPSECONDSTABLE to read an external table; without it the built-in
   table is used, which means a library update for each new leap second.
   das_tt2k_reinit() in tt2000.h reloads it later. */
bool das_tt2K_init(const char* sProgName);


/* Moved from util.h: library plumbing, not API */

/** Used for co-operative locking of time-limited error disposition changes.
 * 
 * Acquire this lock before your critical section, then release it.
 * All code that want's to toggle the error disposition should use this,
 * but it's not enforceable, except by code review.
 * 
 * YOU MUST BE SURE YOUR FUNCTION CAN'T EXIT BEFORE THE LOCK IS RELEASED!
 */
void das_errdisp_get_lock(void);

/** Used for co-operative locking of time-limited error disposition changes.
 * 
 * Release this lock before your critical section, then release it.
 * All code that want's to toggle the error disposition should use this,
 * but it's not enforceable, except by code review.
 */
void das_errdisp_release_lock(void);

/** Store string in a buffer that is reallocated if need be
 *
 * @param psDest a pointer to the storage location
 * @param puLen a pointer to the size of the storage location
 * @param sSrc the source string to store.
 */
void das_store_str(char** psDest, size_t* puLen, const char* sSrc);

/** Copy string as an XML token
 * 
 * Copy a string ignoring leading and traily spaces.  Internal whitespace
 * characters are converted to regular spaces and collapsed
 *
 * @param dest The buffer to receive string data, may be UTF-8 if desired
 *             The output string is always null terminated if n > 1
 *
 * @param src the source string, may be UTF-8 if desired
 *
 * @param n the size of the dest buffer
 * 
 * @returns the equivalent of strlen(dest).
 */
size_t das_tokncpy(char* dest, const char* src, size_t n);

/** A memset that handles multi-byte items  
 *              
 * Uses memcpy because the amount of data written in each call goes up
 * exponentially and memcpy is freaking fast, much faster than a linear
 * write loop for large arrays.
 * 
 * @param pDest The destination area must not overlap with pSrc
 * @param pSrc  A location for an individual element to repeat in pDest
 * @param uElemSz The size in bytes of a single element
 * @param uCount The number of elements to repeat in pDest
 * @return The input pDest pointer.  There is no provision for a NULL return
 *         as this function should not fail since the memory is pre-allocated
 *         by the caller
 * 
 */
uint8_t* das_memset(
	uint8_t* pDest, const uint8_t* pSrc, size_t uElemSz, size_t uCount
);


/* Julian Day at January 1, 1958, 12:00:00 UT */
#define DAS_MJ1958_EPOCH 2436205   /* the integer julian day for the MJ1958 scale */


#endif /* _das_util_p_h_ */
