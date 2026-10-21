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

#ifndef _das_util_priv_h_
#define _das_util_priv_h_

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

#endif /* _das_util_priv_h_ */
