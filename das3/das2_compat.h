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

/** @file das2_compat.h Old das2C names for code written against v2.3.0
 *
 * Code written against das2C v2.3.0 (2023-11-06) or earlier can replace
 * every das2C include:
 *
 *    #include <das2/core.h>
 *    #include <das2/das1.h>
 *    #include <das2/send.h>   ... and the rest
 *
 * with the one line:
 *
 *    #include <das3/das2_compat.h>
 *
 * and recompile.  The one exception is <das2/spice.h>, which becomes
 * <das3/spice.h> since it needs the CSPICE headers.  Include it BEFORE this
 * header so das_send_spice_err() is adapted too.
 *
 * Every public name from v2.3.0 that das2C has since removed
 * or changed is listed here.  Where a macro can map the old call onto the new
 * one it does, so old code keeps building.  Where it can't, the old name
 * expands to an identifier that spells out the fix, so the compiler error
 * points back to this file.  Either way this is the list of names to update
 * when moving to das3.
 *
 * Why v2.3.0: it is the last stable release.  Everything after it was a
 * v3.0-preN prerelease, and names that came and went during that work (the
 * stream frames, das_geovec, the v3.0-pre variable constructors ...) are not
 * covered.  Code that tracked the prereleases was tracking development and
 * is fixed by hand.
 *
 * Not every old program will build unchanged, the variable layer in
 * particular was rewritten, but the errors this header produces say what to
 * use instead.
 *
 * Nothing in das2C includes this header, and it is not meant for code that
 * tracks das2C directly (das2py, das2D, das2dlm, dasFlex, dasTelem ...).
 *
 * Because several old names are function-like macros with the same name as
 * the new function, include this INSTEAD of the individual das3 headers;
 * it pulls in everything it remaps first.
 */

#ifndef _das2_compat_h_
#define _das2_compat_h_

#include <string.h>

#include <das3/core.h>
#include <das3/generator.h>

/* v2.3.0's core.h included these, today's does not */
#include <das3/utf8.h>
#include <das3/plane.h>
#include <das3/packet.h>
#include <das3/dsdf.h>
#include <das3/builder.h>
#include <das3/dft.h>
#include <das3/credentials.h>
#include <das3/http.h>
#include <das3/node.h>

/* The rest of the v2.3.0 installed headers, which core.h never included */
#include <das3/das1.h>
#include <das3/send.h>
#include <das3/cli.h>
#include <das3/json.h>

/* ************************************************************************* */
/* Basic types */

/* Only ubyte is left.  Both are uint8_t, so the old byte* signatures of
   DasAry_*, getpkt() and friends take a ubyte* unchanged. */
typedef ubyte byte;

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

/* das_shape_prnRng() returned the end of the written text, for appending.
   B is evaluated twice. */
#define das_shape_prnRng(S, E, L, B, N) \
	(das_shape_info((S), (E), (L), (B), (size_t)(N)), (B) + strlen(B))

/* DasEnc_toStr() returned 0 on success, an error code otherwise */
#define DasEnc_toStr(P, B, N) \
	((DasEnc_toStr((P), (B), (size_t)(N)) == NULL) ? DASERR_ENC : DAS_OKAY)

/* das_url_toStr() returned true on success; the new non-NULL return reads
   the same in a test, so no macro.

   These kept their names and char* returns but now return NULL, instead of a
   cut off sBuf, when the text doesn't fit:
      DasVar_toStr, DasAry_toStr, das_datum_toStr, das_datum_toStrValOnly,
      Units_toLabel

   Their int length arguments are now size_t.

   das_doubles2csv() gained a buffer length after pBuf.  No macro, there is no
   honest default; the compiler's "too few arguments" marks each call. */

/* das_idx_prn() was declared in array.h but never exported, it is private */
#define das_idx_prn(...) das_idx_prn_is_private__see_das2_compat_h

/* ************************************************************************* */
/* Time functions, 2026-10: das_time* first, as if it were "this" */

#define dt_isoc(B, N, T, F)     dt_isoc((T), (B), (size_t)(N), (F))
#define dt_isod(B, N, T, F)     dt_isod((T), (B), (size_t)(N), (F))
#define dt_dual_str(B, N, T, F) dt_dual_str((T), (B), (size_t)(N), (F))
#define dt_parsetime(S, T)      dt_parsetime((T), (S))
#define dt_emitt(TT, T)         dt_emitt((T), (TT))
#define dt_from_1958(D, MS, T)  dt_from_1958((T), (D), (MS))

#define dt_cmp                  dt_compare

/* ************************************************************************* */
/* Code to name lookups, 2026-10: _toStr / _fromStr pairs */

#define PlaneType_toStr   das_plane_type_toStr
#define str2PlaneType     das_plane_type_fromStr
#define LogLvl_string     das_loglvl_toStr

/* ************************************************************************* */
/* Streams */

/* StreamDesc became DasStream.  Packet descriptors are now one kind of
   DasDesc a stream holds (datasets are the other), so the *PktDesc names
   that work on either kind lost the "Pkt". */
#define stream_descriptor           das_stream   /* struct tag */
#define StreamDesc                  DasStream
#define StreamDescHandler           DasStreamHandler
#define new_StreamDesc              new_DasStream
#define new_StreamDesc_str          new_DasStream_str
#define del_StreamDesc              del_DasStream
#define StreamDesc_copy             DasStream_copy
#define StreamDesc_getNPktDesc      DasStream_numDesc
#define StreamDesc_addPktDesc       DasStream_addDesc
#define StreamDesc_freePktDesc      DasStream_freeDesc
#define StreamDesc_setMonotonic     DasStream_setMonotonic
#define StreamDesc_addStdProps      DasStream_addStdProps
#define StreamDesc_addCmdLineProp   DasStream_addCmdLineProp
#define StreamDesc_createPktDesc    DasStream_createPktDesc
#define StreamDesc_clonePktDesc     DasStream_clonePktDesc
#define StreamDesc_clonePktDescById DasStream_clonePktDescById
#define StreamDesc_isValidId        DasStream_isValidId
#define StreamDesc_getPktDesc       DasStream_getPktDesc

/* The un-numbered encoders wrote das2 headers, das3 ones are _encode3 */
#define StreamDesc_encode           DasStream_encode2
#define DasDesc_encode              DasDesc_encode2

/* Declared since the earliest das2C but never implemented, any use was
   already a link error */
#define StreamDesc_getOffset(...)   StreamDesc_getOffset_never_existed

/* The factory now takes the stream, the packet ID and the data model it
   should parse into */
#define Das2Desc_decode(...)        Das2Desc_decode_removed__use_DasDesc_decode

/* <packet group=> is not in the das2 schema; das2 plane names become das3
   dataset groups, see DasDs_group() */
#define PktDesc_getGroup(...)  PktDesc_getGroup_removed__see_DasDs_group
#define PktDesc_setGroup(...)  PktDesc_setGroup_removed__see_DasDs_group

/* Properties are no longer a fixed size table, DasDesc_length() counts them */
#define DAS_XML_MAXPROPS  DAS_XML_MAXPROPS_removed__use_DasDesc_length

/* ************************************************************************* */
/* Datasets, dimensions and the builder */

#define dasds_iterator   DasDsIter
#define dasds_iter_init  DasDsIter_init
#define dasds_iter_next  DasDsIter_next

/* DasDs_getDim() now looks dimensions up by name, the index lookup kept its
   behavior under a new name */
#define DasDs_getDim  DasDs_getDimByIdx

/* Dimensions gained a name within their kind, so several can share a kind
   ("Search_Coil" and "DC_MAG" are both "magnetic_field").  NULL makes the
   name the same as the kind, the old behavior. */
#define new_DasDim(S, T, R)      new_DasDim((S), NULL, (T), (R))
#define DasDs_makeDim(D, T, S)   DasDs_makeDim((D), (T), (S), NULL)

/* DasDs_addAry() returned true on success, now DAS_OKAY (0) */
#define DasDs_addAry(D, A)  (DasDs_addAry((D), (A)) == DAS_OKAY)

/* DasDim_getVar() and DasDim_getPointVar() now take and return non-const
   pointers; passing a const DasDim* warns.  No macro. */

/* Property merging moved to the descriptor base, which copies all of them
   and returns nothing; the per-axis filtering of the DasDim version is gone */
#define DasDs_copyInProps(...)   DasDs_copyInProps_removed__use_DasDesc_copyIn
#define DasDim_copyInProps(...)  DasDim_copyInProps_removed__use_DasDesc_copyIn

/* Declared under these names, defined only under the new ones */
#define DASVAR_MAXERR  DASVAR_MAX_ERR
#define DASVAR_MINERR  DASVAR_MIN_ERR

/* "uncertainty" named no defined quantity */
#define DASVAR_UNCERT  DASVAR_UNCERT_removed__use_DASVAR_STD_DEV_or_MIN_ERR_MAX_ERR
#define DASVAR_SPREAD  DASVAR_SPREAD_withheld__no_point_spread_formalism_yet

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
/* Index vocabulary: array extents are ARYIDX_*, dataset index space is
   VARIDX_*.  Same values, EXCEPT the maximum rank: DASIDX_MAX was 16 in
   v2.3.0 and is 8 now, and the _INIT_ lists shrank to match.  Arrays sized
   DASIDX_MAX and filled from DASIDX_INIT_* still agree with each other. */

#define DASIDX_MAX          ARYIDX_MAX
#define DASIDX_RAGGED       VARIDX_RAGGED
#define DASIDX_FUNC         VARIDX_BORROW   /* renamed DASIDX_BORROW, then this */
#define DASIDX_UNUSED       VARIDX_UNUSED
#define DASIDX_INIT_UNUSED  VARIDX_INIT_UNUSED
#define DASIDX_INIT_BEGIN   VARIDX_INIT_BEGIN
#define DEGEN               VARIDX_UNUSED

/* The globals are gone, these compound literals still work as memcpy()
   sources and in sizeof */
#define g_aShapeUnused ((const ptrdiff_t[ARYIDX_MAX])VARIDX_INIT_UNUSED)
#define g_aShapeZeros  ((const ptrdiff_t[ARYIDX_MAX])VARIDX_INIT_BEGIN)

/* ************************************************************************* */
/* Values and datums */

#define das_byteseq_t  das_const_byte_seq_t
#define das_byteseq    das_cbyte_seq

/* das_datum_toDbl() returned the value, DAS_FILL_VALUE on failure */
static inline double _das2_compat_datum_toDbl(const das_datum* pDm)
{
	double rVal;
	return das_datum_toDbl(pDm, &rVal) ? rVal : DAS_FILL_VALUE;
}
#define das_datum_toDbl(P) _das2_compat_datum_toDbl(P)

/* ************************************************************************* */
/* Variables, 2026-08 (the generator / formalism rewrite)
 *
 * A variable is now three parts: a generator for the values, a form saying
 * what they mean, and the variable that joins them:
 *
 *    pGen  = new_DasGenAry(pAry, MAP_2(0, DEGEN))  (or new_DasGenSeq, ...Const)
 *    pForm = new_DasFormLinear()                   (scalars, form_lin.h)
 *    pVar  = new_DasVar(pGen, units, pForm)
 *    DasGen_decRef(pGen);  del_DasForm(pForm);
 *
 * new_DasGenAry() takes the same (rank, map) pair new_DasVarArray() did, so
 * the MAP_n helpers below still fit it.  Units are passed in, they are no
 * longer taken from the array.  Use new_DasVarComp() for variables with
 * internal indices and new_DasVarBytes() for strings.
 */

#define MAP_0                  0, (NULL)
#define MAP_1(I)               1, (int8_t[1]){I}
#define MAP_2(I,J)             2, (int8_t[2]){I,J}
#define MAP_3(I,J,K)           3, (int8_t[3]){I,J,K}
#define MAP_4(I,J,K,L)         4, (int8_t[4]){I,J,K,L}
#define MAP_5(I,J,K,L,M)       5, (int8_t[5]){I,J,K,L,M}
#define MAP_6(I,J,K,L,M,N)     6, (int8_t[6]){I,J,K,L,M,N}
#define MAP_7(I,J,K,L,M,N,O)   7, (int8_t[7]){I,J,K,L,M,N,O}

#define das_variable        das_var   /* struct tag, the fields all changed */

#define var_type            das_gen_type_e
#define D2V_DATUM           gtConst   /* values differ, D2V_DATUM was 0 */
#define D2V_SEQUENCE        gtSeq
#define D2V_ARRAY           gtArray
#define D2V_UNARY_OP        gtUnop
#define D2V_BINARY_OP       gtBinop

/* A binary operation has no generator.  P is evaluated twice. */
#define DasVar_type(P) \
	(DasVar_gen(P) ? DasGen_type(DasVar_gen(P)) : gtBinop)

/* The old meaning, an operator tree.  NOT a test for a DasVarComp, which is
   what "composite" means now. */
#define DasVar_isComposite(P)  (DasVar_gen(P) == NULL)

#define DasVarAry_getArray  DasVar_getAry
#define ref_DasVar(P)       ((P)->nRef)

/* Returned true on success.  A variable that needs caller scratch
   (DasVar_getNeedsBuf) fails here, call DasVar_get() directly for those. */
#define DasVar_getDatum(P, L, D) \
	(DasVar_get((P), (L), (das_byte_seq){NULL, 0}, (D)) == 0)

/* DasVar_subset() took a fifth argument, pShapeFrom; old calls add NULL to
   wear the variable's own extents.  No macro, DasVar_allVals() expands to
   the five argument form. */

#define D2V_EXP_UNITS  D2V_EXP_removed__DasVar_toStr_takes_no_flags
#define D2V_EXP_RANGE  D2V_EXP_removed__DasVar_toStr_takes_no_flags
#define D2V_EXP_SUBEX  D2V_EXP_removed__DasVar_toStr_takes_no_flags

#define new_DasVarArray(...) \
	new_DasVarArray_removed__use_new_DasGenAry_plus_new_DasVar
#define new_DasVarSeq(...) \
	new_DasVarSeq_removed__use_new_DasGenSeq_plus_new_DasVar
#define new_DasConstant(...) \
	new_DasConstant_removed__use_new_DasGenConst_plus_new_DasVar
#define new_DasVarUnary(...) \
	new_DasVarUnary_removed__no_unary_vars_in_v3
#define new_DasVarUnary_tok(...) \
	new_DasVarUnary_removed__no_unary_vars_in_v3

/* Not mapped onto new_DasVarBin() on purpose: the old call took over the
   caller's operand references, the new one takes its own, so old callers
   would leak.  The id argument is gone and only + - * are accepted. */
#define new_DasVarBinary(...) \
	new_DasVarBinary_removed__use_new_DasVarBin

#define DasVar_valSize(...) \
	DasVar_valSize_removed__use_das_vt_size_of_elemType
#define DasVar_isFill(...) \
	DasVar_isFill_removed__compare_with_DasGen_getFill

/* Declared but never implemented, any use was already a link error */
#define DasVar_id(...)          DasVar_id_never_existed
#define DasVar_orthoginal(...)  DasVar_orthoginal_never_existed__use_DasVar_degenerate
#define new_DasVarEval(...)     new_DasVarEval_never_existed__use_DasVar_materialize

/* ************************************************************************* */
/* Credentials */

/* Gained a key argument (unused, NULL) and returns the rows saved or -1 */
#define CredMngr_save(P, F)  (CredMngr_save((P), NULL, (F)) >= 0)

/* ************************************************************************* */
/* das2 encodings: the integer categories were parsed but never read or written,
   das3 streams carry integers through DasCodec */

#define DAS2DT_BE_INT   DAS2DT_BE_INT_removed__use_das3_DasCodec
#define DAS2DT_LE_INT   DAS2DT_LE_INT_removed__use_das3_DasCodec
#define DAS2DT_BE_UINT  DAS2DT_BE_UINT_removed__use_das3_DasCodec
#define DAS2DT_LE_UINT  DAS2DT_LE_UINT_removed__use_das3_DasCodec

/* ************************************************************************* */
/* Das1 output */

/* Public names don't start with an underscore */
#define _das_swap_float das_swap_float
#define _swapBufInPlace das_swapBufInPlace

/* Was a second copy of das_xml_escape() with the arguments in another order */
#define _das_escape_xml(D, L, S) ((void)das_xml_escape((D), (S), (L)))

/* ************************************************************************* */
/* JSON */

/* The json.h names all carry das_ now, this one was missed */
#define json_parse_error_info das_json_parse_error_info

/* utf8.h's character class helpers were generic names, now static in utf8.c */
#define octal_digit(...) octal_digit_is_private__use_isdigit
#define hex_digit(...)   hex_digit_is_private__use_isxdigit

/* ************************************************************************* */
/* TT2000: tt2000.h is no longer installed.  Its CDF style variadic calls took
   every field as a double; time.h has integer versions, tt2k_from_parts() and
   tt2k_to_parts().  These adapters keep the v2.3.0 behavior, including a
   silent ILLEGAL_TT2000_VALUE for bad fields. */

#define DAS_TT2000_JD_ORIGIN 2451545.0

static inline long long _das2_compat_utc_to_tt2K(
	double yr, double mt, double dy, double hr, double mn, double sc,
	double ms, double us, double ns
){
	das_time dt = {0};
	dt.year = (int)yr;  dt.month = (int)mt;  dt.mday = (int)dy;
	dt.hour = (int)hr;  dt.minute = (int)mn;
	dt.second = sc + ms*1.0e-3 + us*1.0e-6 + ns*1.0e-9;
	return dt_to_tt2k(&dt);
}
#define das_utc_to_tt2K(Y, M, D, h, m, s, ms, us, ns) \
	_das2_compat_utc_to_tt2K((Y), (M), (D), (h), (m), (s), (ms), (us), (ns))

static inline void _das2_compat_tt2K_to_utc(
	long long tt, double* yr, double* mt, double* dy, double* hr, double* mn,
	double* sc, double* ms, double* us, double* ns
){
	int y, m, d, h, n, s;
	int32_t nNano;
	tt2k_to_parts(tt, &y, &m, &d, &h, &n, &s, &nNano);
	*yr = y;  *mt = m;  *dy = d;  *hr = h;  *mn = n;  *sc = s;
	*ms = nNano / 1000000;  *us = (nNano / 1000) % 1000;  *ns = nNano % 1000;
}
#define das_tt2K_to_utc(T, Y, M, D, h, m, s, ms, us, ns) \
	_das2_compat_tt2K_to_utc((T), (Y), (M), (D), (h), (m), (s), (ms), (us), (ns))

#define das_tt2K_to_us2K(T) Units_convertTo(UNIT_US2000, (T), UNIT_TT2000)
#define das_us2K_to_tt2K(U) Units_convertTo(UNIT_TT2000, (U), UNIT_US2000)

#define das_tt2k_reinit(...) das_tt2k_reinit_is_private__see_das2_compat_h

/* ************************************************************************* */
/* Exceptions, 2026-04: the type is a das_except_t, not a string
 *
 * The old type strings are kept and mapped with das_except_fromStr(), which
 * also knows the das3 names; other strings map to DAS_EX_UNKNOWN.  The
 * OobExcept sType and uTypeLen members are gone, read nType or
 * OobExcept_typeStr().  Exceptions are written with each stream version's
 * own names, das_except_toStr() has the table.
 */

#define DAS2_EXCEPT_NO_DATA_IN_INTERVAL "NoDataInInterval"
#define DAS2_EXCEPT_ILLEGAL_ARGUMENT    "IllegalArgument"
#define DAS2_EXCEPT_SERVER_ERROR        "ServerError"
#define EXCEPTION_UNTYPED ""

#define OobExcept_set(P, T, M) \
	OobExcept_set((P), das_except_fromStr(T), (M))
#define DasIO_throwException(IO, S, T, M) \
	DasIO_throwException((IO), (S), das_except_fromStr(T), (M))

/* das_send_spice_err() took the type string too.  This header can't include
   <das3/spice.h>, which needs the CSPICE headers, and a function-like macro
   would mangle that file's prototype if it came second, so the adapter is
   only defined when spice.h was included FIRST. */
#ifdef _das2_spice_H_
#define das_send_spice_err(V, T) das_send_spice_err((V), das_except_fromStr(T))
#endif

#endif /* _das2_compat_h_ */
