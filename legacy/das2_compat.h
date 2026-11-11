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
 *    #include "das2_compat.h"
 *
 * and recompile.  This header and the das1, send and cli files it pulls in
 * are not part of libdas3; copy them from das2C's legacy directory into the
 * program's own source tree.  The one exception is <das2/spice.h>, which becomes
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

/* v2.3.0's core.h included these, today's does not.  utf8.h, vendored
   public domain code, is no longer installed: a program that called the
   u8_* functions copies das3/utf8.h and das3/utf8.c from das2C into its own
   source tree. */
#include <das3/plane.h>
#include <das3/packet.h>
#include <das3/dsdf.h>
#include <das3/dft.h>
#include <das3/creds.h>
#include <das3/http.h>
#include <das3/node.h>

/* The rest of the v2.3.0 installed headers, which core.h never included */
#include "das1.h"
#include "send.h"
#include "cli.h"
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

/* These kept their names and char* returns but now return NULL, instead of a
   cut off sBuf, when the text doesn't fit:
      DasVar_toStr, DasAry_toStr, das_datum_toStr, das_datum_toStrValOnly,
      Units_toLabel

   Their int length arguments are now size_t, and the two datum calls take
   nFracDigits before the buffer, (pThis, nFracDigits, sStr, uLen).

   das_doubles2csv() gained a buffer length and now takes its values first,
   (pValues, nValues, pBuf, uBufSz).  No macro, there is no honest default;
   the compiler's "too few arguments" marks each call. */

#define das_datum_toStr(P, S, N, F)        das_datum_toStr((P), (F), (S), (N))
#define das_datum_toStrValOnly(P, S, N, F) das_datum_toStrValOnly((P), (F), (S), (N))

/* das_idx_prn() was declared in array.h but never exported, it is private */
#define das_idx_prn(...) das_idx_prn_is_private__see_das2_compat_h

/* ************************************************************************* */
/* Time functions, 2026-10: das_time* first, as if it were "this".
   2026-11: then inputs, then the output buffer and its size last. */

#define dt_isoc(B, N, T, F)     dt_isoc((T), (F), (B), (size_t)(N))
#define dt_isod(B, N, T, F)     dt_isod((T), (F), (B), (size_t)(N))
#define dt_dual_str(B, N, T, F) dt_dual_str((T), (F), (B), (size_t)(N))

/* All three are das_time*, so an old order call would compile and test the
   wrong time.  v2.3.0 was (begin, end, test). */
#define dt_in_range(B, E, T)    dt_in_range((T), (B), (E))

/* Inputs first: the value, then its units, then the units wanted */
#define Units_convertTo(TO, V, FROM) Units_convertTo((V), (FROM), (TO))
#define Units_convertFromDt(U, T)    Units_convertFromDt((T), (U))
#define dt_parsetime(S, T)      dt_parsetime((T), (S))
#define dt_emitt(TT, T)         dt_emitt((T), (TT))
#define dt_from_1958(D, MS, T)  dt_from_1958((T), (D), (MS))

#define dt_cmp                  dt_compare

/* dt_copy() is gone: das_time is a plain value, copy it with = */
#define dt_copy(D, S)           ((void)(*(D) = *(S)))

/* ************************************************************************* */
/* Code to name lookups, 2026-10: _toStr / _fromStr pairs */

#define PlaneType_toStr   das_plane_type_toStr
#define str2PlaneType     das_plane_type_fromStr
#define LogLvl_string(...) LogLvl_string_removed__log_levels_are_comment_types_like_log_info

/* ************************************************************************* */
/* Enumerations, 2026-11: prefixed so they no longer collide with user code */

/* The old constants come back as an anonymous enum, not as macros, so a local
   variable named X or STREAM still shadows them as it did in v2.3.0.  The
   enum tags can only be renamed by macro, so any other identifier spelled
   dim_type, plane_type, ytag_spec or DescriptorType is renamed with them;
   harmless, since every use is renamed the same way. */
#define DescriptorType das_desc_type   /* enum tag */
typedef das_desc_type_e desc_type_t;
enum {
	UNK_DESC = DASDESC_UNK, STREAM = DASDESC_STREAM, PLANE = DASDESC_PLANE,
	PACKET = DASDESC_PKT, DATASET = DASDESC_DS, VARIABLE = DASDESC_VAR
};

#define plane_type das_plane_type      /* enum tag */
typedef das_plane_type_e plane_type_t;
enum {
	Invalid = DASPLANE_INV, X = DASPLANE_X, Y = DASPLANE_Y, Z = DASPLANE_Z,
	YScan = DASPLANE_YSCAN
};

#define ytag_spec das_ytag_type        /* enum tag */
typedef das_ytag_type_e ytag_spec_t;
enum {
	ytags_none = DASPLANE_YTAGS_NONE, ytags_list = DASPLANE_YTAGS_LIST,
	ytags_series = DASPLANE_YTAGS_SEQ
};

#define dim_type das_dim_type          /* enum tag, as in enum dim_type */

/* DasAry usage flags, the D2 prefix went with the das2 name */
#define D2ARY_AS_SUBSEQ  DASARY_AS_SUBSEQ
#define D2ARY_FILL_TERM  DASARY_FILL_TERM
#define D2ARY_AS_STRING  DASARY_AS_STRING

/* Julian Day Number of 1958-01-01, the day MJ1958 counts from.  Library
   internal now; the value is fixed, so it is restated rather than aliased. */
#define EPOCH 2436205

#define jday das_julian_day

/* das2 and das3 packet ID limits differ; the old name was the das2 one */
#define MAX_PKTIDS DAS2_MAX_PKTIDS

/* Catalog node types (v2.3.0 had no source set; the values moved) */
#define das_node_type_enum das_node_type   /* enum tag */
enum {
	d2node_inv = DASNODE_INV, d2node_catalog = DASNODE_CATALOG,
	d2node_collection = DASNODE_COLLECTION, d2node_stream_src = DASNODE_STREAM_SRC,
	d2node_file_agg = DASNODE_FILE_AGG, d2node_spdf_cat = DASNODE_SPDF_CAT,
	d2node_spase_cat = DASNODE_SPASE_CAT
};

/* Catalog document keys and type strings, the D2 prefix went too */
#define D2URI_ROOT           DASNODE_URI_ROOT
#define D2URI_SITES          DASNODE_URI_SITES
#define D2URI_SITE_UIOWA     DASNODE_URI_UIOWA
#define D2FRAG_TYPE          DASNODE_FRAG_TYPE
#define D2FRAG_NAME          DASNODE_FRAG_NAME
#define D2FRAG_TITLE         DASNODE_FRAG_TITLE
#define D2FRAG_DESC          DASNODE_FRAG_DESC
#define D2FRAG_SUB_PATHS     DASNODE_FRAG_SUB_PATHS
#define D2FRAG_PATH_SEP      DASNODE_FRAG_PATH_SEP
#define D2FRAG_SOURCES       DASNODE_FRAG_SOURCES
#define D2FRAG_URLS          DASNODE_FRAG_URLS
#define D2CV_TYPE_CATALOG    DASNODE_CATALOG_S
#define D2CV_TYPE_COLLECTION DASNODE_COLLECTION_S
#define D2CV_TYPE_STREAM     DASNODE_STREAM_SRC_S
#define D2CV_TYPE_TIMEAGG    DASNODE_FILE_AGG_S
#define D2CV_TYPE_SPASE      DASNODE_SPASE_CAT_S

/* ************************************************************************* */
/* Packets and planes */

/* The das2 packet and plane descriptors follow the object naming rules now:
   PktDesc became DasPkt and PlaneDesc became DasPlane, methods too. */
#define PktDesc                    DasPkt
#define PlaneDesc                  DasPlane
#define packet_descriptor          das_pkt   /* struct tag */
#define plane_descriptor           das_plane   /* struct tag */
#define PktDesc_addPlane           DasPkt_addPlane
#define PktDesc_copyPlanes(...)    PktDesc_copyPlanes_is_private__use_DasStream_clonePktById
#define PktDesc_decodeData(...)    PktDesc_decodeData_is_private__streams_are_read_and_written_by_DasIO
#define PktDesc_encode             DasPkt_encode
#define PktDesc_encodeData         DasPkt_encodeData
#define PktDesc_equalFormat        DasPkt_equalFormat
/* A packet no longer keeps its ID, its stream does.  The v2.3.0 calls that
   relied on a packet knowing its own ID ask the packet's parent stream. */
static inline int _das2_compat_pktId(const DasPkt* pPkt)
{
	const DasDesc* pSd = DasDesc_parent((const DasDesc*)pPkt);
	if(pSd == NULL) return -1;
	return DasStream_getPktId((const DasStream*)pSd, (const DasDesc*)pPkt);
}
#define PktDesc_getId(P)           _das2_compat_pktId(P)
#define PktDesc_getNPlanes         DasPkt_getNPlanes
#define PktDesc_getNPlanesOfType   DasPkt_getNPlanesOfType
#define PktDesc_getPlane           DasPkt_getPlane
#define PktDesc_getPlaneByName     DasPkt_getPlaneByName
#define PktDesc_getPlaneByType     DasPkt_getPlaneByType
#define PktDesc_getPlaneIdx        DasPkt_getPlaneIdx
#define PktDesc_getPlaneIdxByName  DasPkt_getPlaneIdxByName
#define PktDesc_getPlaneIdxByType  DasPkt_getPlaneIdxByType
#define PktDesc_getPlaneType       DasPkt_getPlaneType
#define PktDesc_getXPlane          DasPkt_getXPlane
#define PktDesc_recBytes           DasPkt_recBytes
#define PktDesc_setValue           DasPkt_setValue
#define PktDesc_setValues          DasPkt_setValues
#define PktDesc_validate(...)      PktDesc_validate_is_private__streams_are_read_and_written_by_DasIO
#define PlaneDesc_copy             DasPlane_copy
#define PlaneDesc_decode           DasPlane_decode
#define PlaneDesc_decodeData(...)  PlaneDesc_decodeData_is_private__streams_are_read_and_written_by_DasIO
#define PlaneDesc_encode(...)      PlaneDesc_encode_is_private__streams_are_read_and_written_by_DasIO
#define PlaneDesc_encodeData(...)  PlaneDesc_encodeData_is_private__streams_are_read_and_written_by_DasIO
#define PlaneDesc_equivalent       DasPlane_equivalent
#define PlaneDesc_getDatum         DasPlane_getDatum
#define PlaneDesc_getFill          DasPlane_getFill
#define PlaneDesc_getNItems        DasPlane_getNItems
#define PlaneDesc_getName          DasPlane_getName
#define PlaneDesc_getOrMakeYTags   DasPlane_getOrMakeYTags
#define PlaneDesc_getType          DasPlane_getType
#define PlaneDesc_getUnits         DasPlane_getUnits
#define PlaneDesc_getValEncoder    DasPlane_getValEncoder
#define PlaneDesc_getValue         DasPlane_getValue
#define PlaneDesc_getValues        DasPlane_getValues
#define PlaneDesc_getYTagInterval  DasPlane_getYTagInterval
#define PlaneDesc_getYTagSeries    DasPlane_getYTagSeq
#define PlaneDesc_getYTagSpec      DasPlane_getYTagType
#define PlaneDesc_getYTagUnits     DasPlane_getYTagUnits
#define PlaneDesc_getYTags         DasPlane_getYTags
#define PlaneDesc_isFill           DasPlane_isFill
#define PlaneDesc_setFill          DasPlane_setFill
#define PlaneDesc_setNItems        DasPlane_setNItems
#define PlaneDesc_setName          DasPlane_setName
#define PlaneDesc_setTimeValue     DasPlane_setTimeValue
#define PlaneDesc_setUnits         DasPlane_setUnits
#define PlaneDesc_setValEncoder    DasPlane_setValEncoder
#define PlaneDesc_setValue         DasPlane_setValue
#define PlaneDesc_setValues        DasPlane_setValues
#define PlaneDesc_setYTagSeries    DasPlane_setYTagSeq
#define PlaneDesc_setYTagUnits     DasPlane_setYTagUnits
#define PlaneDesc_setYTags         DasPlane_setYTags
#define new_PktDesc                new_DasPkt
#define new_PktDesc_xml(...)       new_PktDesc_xml_is_private__streams_are_read_and_written_by_DasIO
#define new_PlaneDesc              new_DasPlane
#define new_PlaneDesc_empty(...)   new_PlaneDesc_empty_is_private__use_new_DasPlane
#define new_PlaneDesc_pairs(...)   new_PlaneDesc_pairs_is_private__streams_are_read_and_written_by_DasIO
#define new_PlaneDesc_yscan        new_DasPlane_yscan
#define new_PlaneDesc_yscan_series new_DasPlane_yscan_seq

/* ************************************************************************* */
/* Streams */

/* StreamDesc became DasStream.  Packet descriptors are now one kind of
   DasDesc a stream holds (datasets are the other), so the *PktDesc names
   that work on either kind lost the "Pkt". */
/* The stream version got its own setter (DasIO_setOutVer) and the model setter
   took the "set" prefix to match.  The version string macros became
   das_stream_ver_toStr() so the one list of versions lives in stream.h. */
#define DasIO_model                 DasIO_setModel
#define DAS_22_STREAM_VER           das_stream_ver_toStr(STREAM_VER_2_2)
#define DAS_30_STREAM_VER           das_stream_ver_toStr(STREAM_VER_3_0)

/* The packet-only writers merged into the ones that take any top-level
   descriptor, and the caller now names the ID; these use the one the
   packet has in its stream, as the old calls did. */
#define DasIO_writePktDesc(IO, P)   DasIO_writeDesc((IO), (DasDesc*)(P), _das2_compat_pktId(P))
#define DasIO_writePktData(IO, P)   DasIO_writeData((IO), (DasDesc*)(P), _das2_compat_pktId(P))

/* Removal is by descriptor now, and an unused or out of range ID is a
   quiet false, as the old isValidId gave */
static inline bool _das2_compat_isValidId(const DasStream* pSd, int nPktId)
{
	if((nPktId < 1)||(nPktId >= DAS3_MAX_PKTIDS)) return false;
	return DasStream_getDesc(pSd, nPktId) != NULL;
}
static inline DasErrCode _das2_compat_freePktDesc(DasStream* pSd, int nPktId)
{
	DasDesc* pDesc = _das2_compat_isValidId(pSd, nPktId) ? DasStream_getDesc(pSd, nPktId) : NULL;
	if(pDesc == NULL)
		return das_error(DASERR_STREAM, "No descriptor for packet ID %d", nPktId);
	return DasStream_freeDesc(pSd, pDesc);
}

#define stream_descriptor           das_stream   /* struct tag */
#define StreamDesc                  DasStream
#define StreamDescHandler           das_stream_desc_f
#define new_StreamDesc              new_DasStream
#define new_StreamDesc_str(...)     new_StreamDesc_str_is_private__streams_are_read_and_written_by_DasIO
#define StreamDesc_copy             DasStream_copy
#define StreamDesc_getNPktDesc(S)   DasStream_numDesc((S), DASDESC_UNK)
#define StreamDesc_addPktDesc       DasStream_addDesc
#define StreamDesc_freePktDesc(S, ID) _das2_compat_freePktDesc((S), (ID))
#define StreamDesc_setMonotonic     DasStream_setMonotonic
#define StreamDesc_addStdProps      DasStream_addStdProps
#define StreamDesc_addCmdLineProp   DasStream_addCmdLineProp
#define StreamDesc_createPktDesc    DasStream_createPkt
#define StreamDesc_clonePktDesc(...) StreamDesc_clonePktDesc_is_private__use_DasStream_clonePktById
#define StreamDesc_clonePktDescById DasStream_clonePktById
#define StreamDesc_isValidId(S, ID)  _das2_compat_isValidId((S), (ID))
#define StreamDesc_getPktDesc       DasStream_getPkt

/* DasStream is opaque, its version and type strings are set by enumeration */
#define STREAMDESC_CMP_SZ  STREAMDESC_CMP_SZ_removed__DasStream_is_opaque
#define STREAMDESC_VER_SZ  STREAMDESC_VER_SZ_removed__use_DasStream_setVer

/* The un-numbered encoders wrote das2 headers, das3 ones are _encode3 */
#define StreamDesc_encode(...)      StreamDesc_encode_is_private__streams_are_read_and_written_by_DasIO
#define DasDesc_encode(...)         DasDesc_encode_is_private__streams_are_read_and_written_by_DasIO

/* Declared since the earliest das2C but never implemented, any use was
   already a link error */
#define StreamDesc_getOffset(...)   StreamDesc_getOffset_never_existed

/* The factory now takes the stream, the packet ID and the data model it
   should parse into */
#define Das2Desc_decode(...)        Das2Desc_decode_removed__streams_are_read_and_written_by_DasIO

/* Descriptors are opaque: constructors set up the base, del_DasDesc() frees
   any descriptor through its type's own destructor */
#define DasDesc_init(...)      DasDesc_init_removed__descriptors_come_from_their_classes
#define DasDesc_freeProps(...) DasDesc_freeProps_removed__use_del_DasDesc
#define new_Descriptor(...)    new_Descriptor_removed__descriptors_come_from_their_classes

#define del_StreamDesc(P)      del_DasDesc((DasDesc*)(P))
#define del_PktDesc(P)         del_DasDesc((DasDesc*)(P))
#define del_PlaneDesc(P)       del_DasDesc((DasDesc*)(P))
#define del_DasDs(P)           del_DasDesc((DasDesc*)(P))
#define del_DasDim(P)          del_DasDesc((DasDesc*)(P))

/* <packet group=> is not in the das2 schema; das2 plane names become das3
   dataset groups, see DasDs_group() */
#define PktDesc_getGroup(...)  PktDesc_getGroup_removed__see_DasDs_group
#define PktDesc_setGroup(...)  PktDesc_setGroup_removed__see_DasDs_group

/* Packet IDs belong to a stream, so copies are made through it */
#define copy_PktDesc(...)  copy_PktDesc_removed__use_DasStream_clonePktById

/* Properties are no longer a fixed size table, DasDesc_length() counts them */
#define DAS_XML_MAXPROPS  DAS_XML_MAXPROPS_removed__use_DasDesc_length
#define DasDesc_hasAnyProps(...) DasDesc_hasAnyProps_removed__use_DasDesc_length

/* DasDesc_get() was the same call as DasDesc_getStr() */
#define DasDesc_get             DasDesc_getStr

/* ************************************************************************* */
/* Datasets, dimensions and the builder */

#define dasds_iterator   DasDsIter
#define dasds_iter_init  init_DasDsIter
#define dasds_iter_next  DasDsIter_next

/* DasDs_getDim() now looks dimensions up by name, the index lookup kept its
   behavior under a new name */
#define DasDs_getDim  DasDs_getDimByIdx

/* Dimensions gained a name within their kind, so several can share a kind
   ("Search_Coil" and "DC_MAG" are both "magnetic_field").  NULL makes the
   name the same as the kind, the old behavior. */
#define new_DasDim(S, T, R)      new_DasDim((S), NULL, (T), (R))
#define DasDs_makeDim(D, T, S)   DasDs_makeDim((D), (T), (S), NULL)

/* Both return the name= attribute of a das3 header, and are now called
   that.  "id" was easy to take for the packet ID. */
#define DasDs_id   DasDs_name
#define DasDim_id  DasDim_name
#define DasDs_getDimById  DasDs_getDimByName

/* "Mutable" only ever switched a shape cache, nothing was locked, and the
   cache is the library's own business now. */
#define DasDs_setMutable(...)  DasDs_setMutable_removed__shape_caching_is_internal

/* Read a struct member that is gone.  The caller is the one who sets the
   shape cache, so there is nothing left to ask the dataset. */
#define DasDs_mutable(...)  DasDs_mutable_removed__shape_caching_is_internal

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

/* The dataset builder is gone.  A reader keeps the stream it builds until
   del_DasIO(), so read with DasIO_setModel(pIn, STREAM_MODEL_V3) and
   DasIO_setAccum(pIn, true), then DasIO_takeStream() after DasIO_readAll().
   The V3 stream's descriptors are all DasDs objects, walk them with
   DasStream_nextDesc(); the stream itself is the old "global properties"
   descriptor.  new_DasStream_path() and new_DasStream_stdin() do all of it
   in one call, new_DasStream_stdin() replaces build_from_stdin(). */
#define DasDsBldr  DasDsBldr_removed__use_DasIO_takeStream
#define new_DasDsBldr(...)  new_DasDsBldr_removed__use_DasIO_takeStream
#define del_DasDsBldr(...)  del_DasDsBldr_removed__use_DasIO_takeStream
#define DasDsBldr_release(...)  DasDsBldr_release_removed__use_DasIO_takeStream
#define DasDsBldr_getDataSets(...) \
	DasDsBldr_getDataSets_removed__use_DasIO_takeStream
#define DasDsBldr_getProps(...) \
	DasDsBldr_getProps_removed__use_DasIO_takeStream
#define build_from_stdin(...) \
	build_from_stdin_removed__use_new_DasStream_stdin

/* The one-call readers are DasStream constructors */
#define stream_from_stdin   new_DasStream_stdin
#define stream_from_path    new_DasStream_path

/* An array of pointers is a plain DasAry of unknown type, pointer sized,
   with a NULL fill (the compound literal is that fill value) */
#define new_DasPtrAry(T, R, S) \
	new_DasAry((T), vtUnknown, sizeof(void*), (const ubyte*)&(void*){NULL}, \
	           (R), (S), UNIT_DIMENSIONLESS)

/* Declared in v2.3.0, never implemented, any use was already a link error */
#define DasAry_cmp(...)     DasAry_cmp_never_existed
#define DasBuf_paragraph(...) DasBuf_paragraph_never_existed

/* DasAry_setSrc() only fed the builder's das2 packet matching */
#define DasAry_setSrc(...)  DasAry_setSrc_removed__no_replacement

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

#define das_vt_cmpAny  das_value_cmpAny

/* A placeholder operator token, never produced by any parser */
#define D2OP_PLUS      D2OP_PLUS_removed__no_replacement

/* ************************************************************************* */
/* Variables, 2026-08 (the generator / formalism rewrite)
 *
 * A variable is now two things an application sees, the values and a form
 * saying what they mean, joined by a typed constructor:
 *
 *    pForm = new_DasFormLin()                     (scalars, form_lin.h)
 *    pVar  = new_DasVarAry(pAry, MAP_2(0, DEGEN), units, pForm)
 *    del_DasForm(pForm);
 *
 * new_DasVarAry() takes the same (rank, map) pair new_DasVarArray() did, so
 * the MAP_n helpers below still fit it.  Units are passed in, they are no
 * longer taken from the array.  new_DasVarSeq() and new_DasVarConst() replace
 * the sequence and constant constructors; new_DasVarCompAry() builds a
 * variable with internal indices and new_DasVarBytesAry() a string.  The value
 * source underneath (the generator) is library internal.
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

/* The value source kinds are library internal now, so the old enum is
   answered from the variable alone.  Values differ from v2.3, D2V_DATUM was
   0.  A constant is indistinguishable from a sequence from outside the
   library, so D2V_DATUM is poisoned rather than left as a value DasVar_type()
   can never return. */
typedef enum var_type {
	D2V_ARRAY = 1, D2V_SEQUENCE = 2, D2V_UNARY_OP = 4, D2V_BINARY_OP = 5
} var_type;
#define D2V_DATUM           D2V_DATUM_removed__constants_report_as_D2V_SEQUENCE

/* A binary operation has no value source of its own.  P is evaluated twice. */
#define DasVar_type(P) \
	(DasVar_isBin(P) ? D2V_BINARY_OP : (DasVar_hasAry(P) ? D2V_ARRAY : D2V_SEQUENCE))

/* The old meaning, an operator tree.  NOT a test for a DasVarComp, which is
   what "composite" means now. */
#define DasVar_isComposite(P)  DasVar_isBin(P)

#define DasVarAry_getArray  DasVar_getAry

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
	new_DasVarArray_removed__use_new_DasVarAry_which_also_takes_units_and_a_form
#define new_DasConstant(...) \
	new_DasConstant_removed__use_new_DasVarConst
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
	DasVar_isFill_removed__compare_with_DasAry_getFill_of_DasVar_getAry

/* Declared but never implemented, any use was already a link error */
#define DasVar_id(...)          DasVar_id_never_existed
#define DasVar_orthoginal(...)  DasVar_orthoginal_never_existed__use_DasVar_degenerate
#define new_DasVarEval(...)     new_DasVarEval_never_existed__use_DasVar_materialize

/* ************************************************************************* */
/* Fourier transforms and power spectra: the classes follow the object naming
   rules now and are opaque.  Read the input length with DasDft_length() or
   DasPsd_length(), not a struct member. */

#define DftPlan             DasDftPlan
#define dft_plan            das_dft_plan   /* struct tag */
#define new_DftPlan         new_DasDftPlan
#define del_DftPlan         del_DasDftPlan

#define Das2Dft             DasDft
#define new_Dft             new_DasDft
#define del_Dft             del_DasDft
#define Dft_calculate       DasDft_calculate
#define Dft_getReal         DasDft_getReal
#define Dft_getImg          DasDft_getImg
#define Dft_getMagnitude    DasDft_getMagnitude

#define Das2Psd             DasPsd
#define new_Psd             new_DasPsd
#define del_Das2Psd         del_DasPsd
#define Psd_calculate       DasPsd_calculate
#define Psd_calculate_f     DasPsd_calculate_f
#define Psd_powerRatio      DasPsd_powerRatio
#define Psd_get             DasPsd_get

/* ************************************************************************* */
/* Credentials */

/* The credentials manager was redone for 3.0: records and secrets are set
   separately, lookups copy into caller buffers, and stores are configured
   rather than named on each save.  The old calls are retired, not aliased. */
#define new_CredMngr(...)          new_CredMngr_removed__use_new_DasCredMngr_then_DasCredMngr_setFileStore
#define del_CredMngr(...)          del_CredMngr_removed__use_del_DasCredMngr
#define CredMngr_addCred(...)      CredMngr_addCred_removed__use_DasCredMngr_addCred_and_DasCredMngr_setSecret
#define CredMngr_getCred(...)      CredMngr_getCred_removed__use_DasCredMngr_getCred_and_DasCredMngr_getSecret
#define CredMngr_addUserPass(...)  CredMngr_addUserPass_removed__use_DasCredMngr_addCred_and_DasCredMngr_setUserPass
#define CredMngr_getHttpAuth(...)  CredMngr_getHttpAuth_is_private__the_http_functions_call_it
#define CredMngr_authFailed(...)   CredMngr_authFailed_is_private__the_http_functions_call_it
#define CredMngr_setPrompt(...)    CredMngr_setPrompt_removed__use_DasCredMngr_setPrompt
#define CredMngr_load(...)         CredMngr_load_removed__use_DasCredMngr_setFileStore_and_DasCredMngr_load
#define CredMngr_save(...)         CredMngr_save_removed__use_DasCredMngr_setFileStore_and_DasCredMngr_save

#define DasCredMngr_addUserPass(...)  DasCredMngr_addUserPass_removed__use_DasCredMngr_addCred_and_DasCredMngr_setUserPass
#define DasCredMngr_getHttpAuth(...)  DasCredMngr_getHttpAuth_is_private__the_http_functions_call_it
#define DasCredMngr_authFailed(...)   DasCredMngr_authFailed_is_private__the_http_functions_call_it

#define das_credential    das_credential_removed__use_das_cred_which_holds_no_secret
#define DASCRED_HASH_SZ   DASCRED_HASH_SZ_removed__secrets_vary_in_length_see_DASCRED_SECRET_SZ
#define DASCMGR_FILE_SZ   DASCMGR_FILE_SZ_removed__see_DASCRED_STORE_SZ
#define DASCMGR_MSG_SZ    DASCMGR_MSG_SZ_removed__no_longer_public

/* ************************************************************************* */
/* Buffers */

/* DasBuf only holds memory now.  Files and connections are read by DasIO
   and the http functions; bytes already in hand go in with DasBuf_write(). */
#define DasBuf_writeFrom(...)      DasBuf_writeFrom_removed__fread_then_DasBuf_write
#define DasBuf_writeFromSock(...)  DasBuf_writeFromSock_removed__recv_then_DasBuf_write
#define DasBuf_writeFromSSL(...)   DasBuf_writeFromSSL_removed__read_bodies_with_the_http_functions

/* DasBuf is opaque, so there are no stack buffers to initialize */
#define DasBuf_initReadOnly(...)   DasBuf_initReadOnly_removed__use_new_DasBuf_wrap_mode_r
#define DasBuf_initReadWrite(...)  DasBuf_initReadWrite_removed__use_new_DasBuf_wrap_mode_w_then_DasBuf_zero

/* ************************************************************************* */
/* Reading and writing streams: every byte source is a DasXport */

/* One DasIO constructor takes a transport, see new_DasIO().  The transport's
   direction is the first letter of the old mode, so M is evaluated twice.
   One difference: a FILE* given to new_DasIO_cfile() is now borrowed, the
   caller closes it after del_DasIO() as with any FILE it opened.  The same
   goes for a socket.  new_DasIO_str() output is no longer NUL terminated. */
#define new_DasIO_cfile(P, F, M) \
	new_DasIO((P), new_DasXport_cfile((F), (((M)[0] == 'w') ? "w" : "r")), (M))
#define new_DasIO_file(P, F, M) \
	new_DasIO((P), new_DasXport_file((F), (((M)[0] == 'w') ? "w" : "r")), (M))
#define new_DasIO_socket(P, S, M) \
	new_DasIO((P), new_DasXport_sock((S), (((M)[0] == 'w') ? "w" : "r")), (M))
#define new_DasIO_cmd(P, C) new_DasIO((P), new_DasXport_cmd(C), "r")
#define new_DasIO_str(P, B, L, M) \
	new_DasIO((P), new_DasXport_mem((B), (L), (((M)[0] == 'w') ? "w" : "r")), (M))

/* An SSL* never crosses the library boundary now, the body of an https
   request comes out of the response as a transport */
#define new_DasIO_ssl(...)  new_DasIO_ssl_removed__use_new_DasIO_with_DasHttpResp_takeBody

/* ************************************************************************* */
/* HTTP: the response is an object, made by the request */

#define das_http_getBody(...)        das_http_getBody_removed__use_new_DasHttpResp_get_and_DasHttpResp_ok
#define DasHttpResp_clear(...)       DasHttpResp_clear_removed__responses_come_from_new_DasHttpResp_get
#define DasHttpResp_freeFields(...)  DasHttpResp_freeFields_removed__use_del_DasHttpResp
#define DasHttpResp_init(...)        DasHttpResp_init_removed__use_das_url_parse
#define DasHttpResp_useSsl(...)      DasHttpResp_useSsl_removed__take_the_body_with_DasHttpResp_takeBody
#define das_url_toStr(...)           das_url_toStr_removed__use_DasHttpResp_url
#define das_ssl_getErr(...)          das_ssl_getErr_is_private__TLS_stays_inside_the_library

/* URLs have no fixed size parts now, das_url_parse() splits one in place */
#define das_url            das_url_removed__see_das_url_parse
#define DASURL_SZ_SCHEME   DASURL_SZ_SCHEME_removed__urls_have_no_part_limits
#define DASURL_SZ_HOST     DASURL_SZ_HOST_removed__urls_have_no_part_limits
#define DASURL_SZ_PATH     DASURL_SZ_PATH_removed__urls_have_no_part_limits
#define DASURL_SZ_QUERY    DASURL_SZ_QUERY_removed__urls_have_no_part_limits
#define DASURL_SZ_DATASET  DASURL_SZ_DATASET_removed__urls_have_no_part_limits
#define DASURL_SZ_PORT     DASURL_SZ_PORT_removed__urls_have_no_part_limits

/* ************************************************************************* */
/* das2 encodings: the integer categories were parsed but never read or written,
   das3 streams carry integers through DasCodec */

#define DAS2DT_BE_INT   DAS2DT_BE_INT_removed__use_das3_DasCodec
#define DAS2DT_LE_INT   DAS2DT_LE_INT_removed__use_das3_DasCodec
#define DAS2DT_BE_UINT  DAS2DT_BE_UINT_removed__use_das3_DasCodec
#define DAS2DT_LE_UINT  DAS2DT_LE_UINT_removed__use_das3_DasCodec

/* The encoding class follows the object naming rules now, and has a real
   destructor where it used to be freed directly */
#define DasEncoding          DasEnc
#define new_DasEncoding      new_DasEnc
#define new_DasEncoding_str  new_DasEnc_str
#define del_DasEncoding      del_DasEnc

/* The fill helpers: DAS_FILL_VALUE is the value, das_isfill() the test */
#define getDas2Fill()        DAS_FILL_VALUE
#define isDas2Fill           das_isfill

/* ************************************************************************* */
/* Das1 output */

/* Public names don't start with an underscore */
#define _das_swap_float das_swap_float
#define _swapBufInPlace das_swap_buf

/* The swap helpers are named like das_swap_float() */
#define das_swapBufInPlace  das_swap_buf
#define swapU4              das_swap_u4

/* das_xml_escape() takes its source first now.  A const char* source in the
   old place would only warn, so the adapter is required, not a courtesy. */
#define das_xml_escape(D, S, L)  das_xml_escape((S), (D), (L))

/* Was a second copy of das_xml_escape() with the arguments in another order.
   Written in v2.3.0 order, the adapter above puts it right. */
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
/* Exceptions and comments: opaque, and the exception type is a das_except_e,
 * not a string
 *
 * The old type strings are kept and mapped with das_except_fromStr(), which
 * also knows the das3 names; other strings map to DAS_EX_UNKNOWN.
 * Exceptions are written with each stream version's own names,
 * das_except_toStr() has the table.
 *
 * OobExcept and OobComment are now DasExcept and DasComment, heap objects
 * with no public members: read them with DasExcept_type(), DasExcept_msg()
 * and DasComment_get(p, "type" | "source" | "value").  Make one to send with
 * new_DasExcept() or new_DasComment() and DasComment_set().  Exception and
 * comment handlers take a const pointer now; no macro can adapt a callback,
 * so those signatures need editing.
 */

#define DAS2_EXCEPT_NO_DATA_IN_INTERVAL "NoDataInInterval"
#define DAS2_EXCEPT_ILLEGAL_ARGUMENT    "IllegalArgument"
#define DAS2_EXCEPT_SERVER_ERROR        "ServerError"
#define EXCEPTION_UNTYPED ""

#define OobExcept   DasExcept
#define OobComment  DasComment
#define OutOfBand   OutOfBand_is_private__use_DasExcept_or_DasComment
#define oob_t       oob_t_is_private__use_DasExcept_or_DasComment
#define OOB_EXCEPT  OOB_EXCEPT_is_private__use_DasExcept
#define OOB_COMMENT OOB_COMMENT_is_private__use_DasComment

#define OutOfBand_clean(...)  OutOfBand_clean_removed__use_del_DasExcept_or_del_DasComment
#define OobExcept_init(...)   OobExcept_init_removed__use_new_DasExcept
#define OobExcept_set(...)    OobExcept_set_removed__use_new_DasExcept
#define OobComment_init(...)  OobComment_init_removed__use_new_DasComment
#define OobExcept_decode(...)  OobExcept_decode_is_private__streams_are_read_and_written_by_DasIO
#define OobExcept_encode(...)  OobExcept_encode_is_private__streams_are_read_and_written_by_DasIO
#define OobComment_decode(...) OobComment_decode_is_private__streams_are_read_and_written_by_DasIO
#define OobComment_encode(...) OobComment_encode_is_private__streams_are_read_and_written_by_DasIO
#define OutOfBand_decode(...)  OutOfBand_decode_is_private__streams_are_read_and_written_by_DasIO

/* An exception ends the stream, and all of these become DasIO_exception_v().
   It sends a stub header when none went out, and closes the DasIO without
   freeing it; DasIO_throwException also freed it, so the adapter does too.
   The return values are the old ones.  DasIO_writeException did not close
   the stream, DasIO_exception does.  Exception handlers are gone: the close
   handler gets the exception, see das_close_f in io.h. */
#define DasIO_throwException(IO, S, T, M) \
	((void)(S), DasIO_exception_v((IO), das_except_fromStr(T), "%s", (M)), del_DasIO(IO))
#define DasIO_serverExcept(IO, ...) \
	(DasIO_exception_v((IO), DAS_EX_SERVER_ERR, __VA_ARGS__), 11)
#define DasIO_queryExcept(IO, ...) \
	(DasIO_exception_v((IO), DAS_EX_QUERY_ERR, __VA_ARGS__), 11)
#define DasIO_closeNoData(IO, ...) \
	(DasIO_exception_v((IO), DAS_EX_NO_DATA, __VA_ARGS__), 0)
#define DasIO_writeException DasIO_exception
#define DasIO_writeComment   DasIO_comment

/* Errors are log messages.  das_error() sends its text to the log hook, the
   library keeps no copy, and a program that wants the text saves it in its
   hook, see log.h.  das_init() takes the hook and no buffer size:
   das_init(sProg, nDis, nLevel, pHook). */
#define das_get_error(...)    das_get_error_removed__save_errors_in_your_log_hook
#define das_error_free(...)   das_error_free_removed__the_library_keeps_no_error_copies
#define das_save_error(...)   das_save_error_removed__save_errors_in_your_log_hook
#define das_print_error(...)  das_print_error_removed__the_default_log_hook_prints
#define das_error_msg         das_error_msg_removed__see_das_logmsg_in_log_h
#define das_abort_on_error(...)  das_abort_on_error_removed__break_on_daserr_func_in_a_debugger
#define das_exit_on_error(...)   das_exit_on_error_removed__use_daserr_setdisp_DASERR_DIS_EXIT
#define das_return_on_error(...) das_return_on_error_removed__use_daserr_setdisp_DASERR_DIS_RET
#define DASERR_DIS_ABORT      DASERR_DIS_ABORT_removed__break_on_daserr_func_in_a_debugger
#define das_error_disposition daserr_disp
#define das_error_func        daserr_func
#define das_error_func_fixed  daserr_func_fixed

/* Log handlers take a das_logmsg and a pUser pointer, set as a pair */
#define daslog_sethandler(...) daslog_sethandler_removed__use_daslog_sethook
#define das_log_handler_t     das_log_handler_t_removed__use_das_loghandler_f
#define daslog_set_showline(...) daslog_set_showline_removed__use_daslog_showline_bool
#define DASLOG_NOTHING        DASLOG_NOTHING_removed__DASLOG_CRIT_is_the_top_level

/* To-stream logging: a log line is a comment whose type names the level,
   DasIO_comment_v(pOut, "log:info", ...).  Readers filter, not writers. */
#define DasIO_sendLog(...)    DasIO_sendLog_removed__use_DasIO_comment_v
#define DasIO_setLogLvl(...)  DasIO_setLogLvl_removed__readers_filter_by_comment_type
#define DasIO_getLogLvl(...)  DasIO_getLogLvl_never_existed
#define LOGLVL_FINEST   LOGLVL_FINEST_removed__use_DasIO_comment_v
#define LOGLVL_FINER    LOGLVL_FINER_removed__use_DasIO_comment_v
#define LOGLVL_FINE     LOGLVL_FINE_removed__use_DasIO_comment_v
#define LOGLVL_CONFIG   LOGLVL_CONFIG_removed__use_DasIO_comment_v
#define LOGLVL_INFO     LOGLVL_INFO_removed__use_DasIO_comment_v
#define LOGLVL_WARNING  LOGLVL_WARNING_removed__use_DasIO_comment_v
#define LOGLVL_ERROR    LOGLVL_ERROR_removed__use_DasIO_comment_v

/* das_send_spice_err() took the type string too.  This header can't include
   <das3/spice.h>, which needs the CSPICE headers, and a function-like macro
   would mangle that file's prototype if it came second, so the adapter is
   only defined when spice.h was included FIRST. */
#ifdef _das2_spice_H_
#define das_send_spice_err(V, T) das_send_spice_err((V), das_except_fromStr(T))
#endif

#endif /* _das2_compat_h_ */
