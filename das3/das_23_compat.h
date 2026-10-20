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

/** @file das_23_compat.h Old das2C names for code written before 2026-03-01
 *
 * Code written against das2C before the v3 development push (March 2026)
 * can replace:
 *
 *    #include <das2/core.h>
 *
 * with:
 *
 *    #include <das3/das_23_compat.h>
 *
 * and recompile.  Every name das2C has changed since then is listed here.
 * Where a macro can map the old call onto the new one it does, so old code
 * keeps building.  Where it can't, the old name expands to an identifier
 * that spells out the fix, so the compiler error points back to this file.
 * Either way this is the list of names to update when moving to das3.
 *
 * Nothing in das2C includes this header, and it is not meant for code that
 * tracks das2C directly (das2py, das2D, das2dlm, dasFlex, dasTelem ...).
 *
 * Because several old names are function-like macros with the same name as
 * the new function, include this INSTEAD of the individual das3 headers;
 * it pulls in everything it remaps first.
 */

#ifndef _das_23_compat_h_
#define _das_23_compat_h_

#include <string.h>

#include <das3/core.h>
#include <das3/generator.h>
#include <das3/form.h>
#include <das3/form_vector.h>
#include <das3/form_cplx.h>

/* ************************************************************************* */
/* Text output, 2026-10 (issue #7)
 *
 * Rules now: X_info() is a possibly multi-line summary that returns the
 * length needed, as snprintf does.  X_toStr() is one short line that returns
 * sBuf, or NULL if it was cut off.  Buffer sizes are size_t.
 */

/* DasDs_toStr() and DasDim_toStr() used to print the whole multi-line
   description, which is now DasDs_info() and DasDim_info().  The new _toStr
   functions print one line.  These restore the old output and the old
   return of sBuf; NULL is no longer returned when the buffer is short. */
#define DasDs_toStr(P, B, N)  (DasDs_info((P), (B), (size_t)(N)), (B))
#define DasDim_toStr(P, B, N) (DasDim_info((P), (B), (size_t)(N)), (B))

/* DasStream_info() returned a pointer, now the length needed */
#define DasStream_info(P, B, N) (DasStream_info((P), (B), (size_t)(N)), (B))

/* DasDesc_info() and das_shape_prnRng() returned the end of the written
   text, for appending.  B is evaluated twice. */
#define DasDesc_info(P, B, N, I) \
	(DasDesc_info((P), (B), (size_t)(N), (I)), (B) + strlen(B))
#define das_shape_prnRng(S, E, L, B, N) \
	(das_shape_info((S), (E), (L), (B), (size_t)(N)), (B) + strlen(B))

/* das_shape_toStr() returned the length, or -1 if the buffer was short */
#define das_shape_toStr(S, R, B, N) \
	((das_shape_toStr((S), (R), (B), (size_t)(N)) == NULL) ? -1 : (int)strlen(B))

/* DasEnc_toStr() returned 0 on success, an error code otherwise */
#define DasEnc_toStr(P, B, N) \
	((DasEnc_toStr((P), (B), (size_t)(N)) == NULL) ? DASERR_ENC : DAS_OKAY)

/* das_url_toStr() returned true on success; the new non-NULL return reads
   the same in a test, so no macro.

   These kept their names and char* returns but now return NULL, instead of a
   cut off sBuf, when the text doesn't fit:
      DasVar_toStr, DasForm_toStr, DasUriTplt_toStr, DasAry_toStr,
      das_datum_toStr, das_datum_toStrValOnly, Units_toLabel

   Their int length arguments are now size_t, as are those of
   DasUriTplt_render(), das_value_fmt() and DasForm_prnRun().  A formalism
   implemented outside das2C must change its prnRun vtable function to take
   size_t too. */

/* das_idx_prn() was declared in array.h but never exported, it is private */
#define das_idx_prn(...) das_idx_prn_is_private__see_das_23_compat_h

/* ************************************************************************* */
/* Time functions, 2026-10: das_time* first, as if it were "this" */

#define dt_isoc(B, N, T, F)     dt_isoc((T), (B), (size_t)(N), (F))
#define dt_isod(B, N, T, F)     dt_isod((T), (B), (size_t)(N), (F))
#define dt_dual_str(B, N, T, F) dt_dual_str((T), (B), (size_t)(N), (F))
#define dt_parsetime(S, T)      dt_parsetime((T), (S))
#define dt_emitt(TT, T)         dt_emitt((T), (TT))
#define dt_from_1958(D, MS, T)  dt_from_1958((T), (D), (MS))

/* ************************************************************************* */
/* Code to name lookups, 2026-10: _toStr / _fromStr pairs */

#define das_vsys_str      das_vsys_toStr
#define das_vsys_id       das_vsys_fromStr
#define das_cplxsys_str   das_cplxsys_toStr
#define das_cplxsys_id    das_cplxsys_fromStr
#define das_desc_type_str das_desc_type_toStr
#define PlaneType_toStr   das_plane_type_toStr
#define str2PlaneType     das_plane_type_fromStr
#define LogLvl_string     das_loglvl_toStr

/* ************************************************************************* */
/* Not yet listed: names removed between 2026-03-01 and 2026-10, mostly
   DasFrame_*, das_geovec_* and the old DasVar constructors, which the
   formalism (<ops>) and variable rewrites replaced.  To be filled in. */

#endif /* _das_23_compat_h_ */
