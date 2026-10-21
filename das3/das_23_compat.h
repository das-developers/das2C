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
/* Plain renames that used to be aliased in the regular headers, 2026-10 */

/* StreamDesc became DasStream.  Packet descriptors are now one kind of
   DasDesc a stream holds (datasets are the other), so the *PktDesc names
   that work on either kind lost the "Pkt". */
#define StreamDesc                  DasStream
#define new_StreamDesc              new_DasStream
#define new_StreamDesc_str          new_DasStream_str
#define del_StreamDesc              del_DasStream
#define StreamDesc_info             DasStream_info
#define StreamDesc_copy             DasStream_copy
#define StreamDesc_getNPktDesc      DasStream_numDesc
#define StreamDesc_nextPktDesc      DasStream_nextDesc
#define StreamDesc_addPktDesc       DasStream_addDesc
#define StreamDesc_setMonotonic     DasStream_setMonotonic
#define StreamDesc_addStdProps      DasStream_addStdProps
#define StreamDesc_addCmdLineProp   DasStream_addCmdLineProp
#define StreamDesc_createPktDesc    DasStream_createPktDesc
#define StreamDesc_clonePktDesc     DasStream_clonePktDesc
#define StreamDesc_clonePktDescById DasStream_clonePktDescById
#define StreamDesc_isValidId        DasStream_isValidId
#define StreamDesc_getPktDesc       DasStream_getPktDesc
#define StreamDesc_freeDesc         DasStream_freeDesc
#define DasStream_rmPktDesc         DasStream_rmDesc
#define DasStream_getNPktDesc       DasStream_numDesc
#define DasStream_shadowPktDesc     DasStream_shadowDesc
#define DasStream_takePktDesc       DasStream_takeDesc
#define DasStream_freeDatDesc       DasStream_freeDesc

/* Declared since the earliest das2C but never implemented, any use was
   already a link error */
#define StreamDesc_getOffset(...) StreamDesc_getOffset_never_existed
#define DasStream_getOffset(...)  DasStream_getOffset_never_existed

/* The un-numbered encoders wrote das2 headers, das3 ones are _encode3 */
#define StreamDesc_encode           DasStream_encode2
#define DasDesc_encode              DasDesc_encode2

#define dasds_iterator              DasDsIter
#define dasds_iter_init             DasDsIter_init
#define dasds_iter_next             DasDsIter_next
#define das_cube_iter               DasDsCubeIter
#define das_cube_iter_init          DasDsCubeIter_init
#define das_cube_iter_next          DasDsCubeIter_next

#define dt_cmp                      dt_compare

/* ************************************************************************* */
/* Deprecated dataset builder functions, removed 2026-10 */

/* All three handed back pieces of the stream the builder assembles.  Take
   the whole stream instead with DasDsBldr_getStream(); its descriptors are
   all DasDs objects, walk them with DasStream_nextDesc().  The stream itself
   is the old "global properties" descriptor.  stream_from_stdin() replaces
   build_from_stdin(). */
#define DasDsBldr_getDataSets(...) \
	DasDsBldr_getDataSets_removed__use_DasDsBldr_getStream
#define DasDsBldr_getProps(...) \
	DasDsBldr_getProps_removed__use_DasDsBldr_getStream
#define build_from_stdin(...) \
	build_from_stdin_removed__use_stream_from_stdin

/* ************************************************************************* */
/* Not yet listed: names removed between 2026-03-01 and 2026-10, mostly
   DasFrame_*, das_geovec_* and the old DasVar constructors, which the
   formalism (<ops>) and variable rewrites replaced.  To be filled in. */

#endif /* _das_23_compat_h_ */
