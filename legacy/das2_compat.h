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
typedef uint8_t byte;

/* The error code type is snake case now, as every integer type is */
typedef das_errcode DasErrCode;

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
	(dasshape_info((S), (E), (L), (B), (size_t)(N)), (B) + strlen(B))

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

#define das_datum_toStr(P, S, N, F)        DasDatum_toStr((P), (F), (S), (N))
#define das_datum_toStrValOnly(P, S, N, F) DasDatum_toValStr((P), (F), (S), (N))

/* das_idx_prn() was declared in array.h but never exported, it is private */
#define das_idx_prn(...) das_idx_prn_is_private__see_das2_compat_h

/* ************************************************************************* */
/* Time functions, 2026-10: das_time* first, as if it were "this".
   2026-11: then inputs, then the output buffer and its size last. */

#define dt_isoc(B, N, T, F)     DasTime_toIsoC((T), (F), (B), (size_t)(N))
#define dt_isod(B, N, T, F)     DasTime_toIsoD((T), (F), (B), (size_t)(N))
#define dt_dual_str(B, N, T, F) DasTime_toIsoDual((T), (F), (B), (size_t)(N))

/* All three are das_time*, so an old order call would compile and test the
   wrong time.  v2.3.0 was (begin, end, test). */
#define dt_in_range(B, E, T)    DasTime_isInRange((T), (B), (E))

/* Inputs first: the value, then its units, then the units wanted */
#define Units_convertTo(TO, V, FROM) DasUnits_convert((V), (FROM), (TO))
#define Units_convertFromDt(U, T)    DasTime_toPoint((T), (U))
#define dt_parsetime(S, T)      DasTime_fromStr((T), (S))
#define dt_emitt(TT, T)         DasTime_fromSec1958((T), (TT))
#define dt_from_1958(D, MS, T)  DasTime_fromDays1958((T), (D), (MS))

#define dt_cmp                  DasTime_compare

/* dt_copy() is gone: das_time is a plain value, copy it with = */
#define dt_copy(D, S)           ((void)(*(D) = *(S)))

/* ************************************************************************* */
/* Code to name lookups, 2026-10: _toStr / _fromStr pairs */

#define PlaneType_toStr   dasplanetype_name
#define str2PlaneType     dasplanetype_id
#define LogLvl_string(...) LogLvl_string_removed__log_levels_are_comment_types_like_log_info

/* ************************************************************************* */
/* Enumerations, 2026-11: prefixed so they no longer collide with user code */

/* The old constants come back as an anonymous enum, not as macros, so a local
   variable named X or STREAM still shadows them as it did in v2.3.0.  The
   enum tags can only be renamed by macro, so any other identifier spelled
   dim_type, plane_type, ytag_spec or DescriptorType is renamed with them;
   harmless, since every use is renamed the same way. */
#define DescriptorType das_desctype_e   /* enum tag */
typedef das_desctype desc_type_t;
enum {
	UNK_DESC = DASDESCTYPE_UNK, STREAM = DASDESCTYPE_STREAM, PLANE = DASDESCTYPE_PLANE,
	PACKET = DASDESCTYPE_PKT, DATASET = DASDESCTYPE_DS, VARIABLE = DASDESCTYPE_VAR
};

#define plane_type das_planetype_e      /* enum tag */
typedef das_planetype plane_type_t;
enum {
	Invalid = DASPLANETYPE_INV, X = DASPLANETYPE_X, Y = DASPLANETYPE_Y, Z = DASPLANETYPE_Z,
	YScan = DASPLANETYPE_YSCAN
};

#define ytag_spec das_ytagtype_e        /* enum tag */
typedef das_ytagtype ytag_spec_t;
enum {
	ytags_none = DASYTAGTYPE_NONE, ytags_list = DASYTAGTYPE_LIST,
	ytags_series = DASYTAGTYPE_SEQ
};

#define dim_type das_dimtype_e          /* enum tag, as in enum dim_type */

/* DasAry usage flags, the D2 prefix went with the das2 name */
#define D2ARY_AS_SUBSEQ  DASARY_AS_SUBSEQ
#define D2ARY_FILL_TERM  DASARY_FILL_TERM
#define D2ARY_AS_STRING  DASARY_AS_STRING

/* Julian Day Number of 1958-01-01, the day MJ1958 counts from.  Library
   internal now; the value is fixed, so it is restated rather than aliased. */
#define EPOCH 2436205

#define jday DasTime_utcToJdn

/* das2 and das3 packet ID limits differ; the old name was the das2 one */
#define MAX_PKTIDS DASSTREAM_V2_ENDPKTID

/* Catalog node types (v2.3.0 had no source set; the values moved) */
#define das_node_type_enum das_nodetype_e   /* enum tag */
enum {
	d2node_inv = DASNODETYPE_INV, d2node_catalog = DASNODETYPE_CATALOG,
	d2node_collection = DASNODETYPE_COLLECTION, d2node_stream_src = DASNODETYPE_STREAM_SRC,
	d2node_file_agg = DASNODETYPE_FILE_AGG, d2node_spdf_cat = DASNODETYPE_SPDF_CAT,
	d2node_spase_cat = DASNODETYPE_SPASE_CAT
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
#define packet_descriptor          das_pkt_t   /* struct tag */
#define plane_descriptor           das_plane_t   /* struct tag */
#define PktDesc_addPlane           DasPkt_addPlane
#define PktDesc_copyPlanes(...)    PktDesc_copyPlanes_is_private__use_DasStream_clonePkt
#define PktDesc_decodeData(...)    PktDesc_decodeData_is_private__streams_are_read_and_written_by_DasIO
#define PktDesc_encode             DasPkt_encodeHdr
#define PktDesc_encodeData         DasPkt_encodeData
#define PktDesc_equalFormat        DasPkt_isSameFmt
/* A packet no longer keeps its ID, its stream does.  The v2.3.0 calls that
   relied on a packet knowing its own ID ask the packet's parent stream. */
static inline int _das2_compat_pktId(const DasPkt* pPkt)
{
	const DasDesc* pSd = DasDesc_parent((const DasDesc*)pPkt);
	if(pSd == NULL) return -1;
	return DasStream_pktId((const DasStream*)pSd, (const DasDesc*)pPkt);
}
#define PktDesc_getId(P)           _das2_compat_pktId(P)
#define PktDesc_getNPlanes         DasPkt_numPlanes
#define PktDesc_getNPlanesOfType   DasPkt_numPlanesOfType
#define PktDesc_getPlane           DasPkt_planeAt
#define PktDesc_getPlaneByName     DasPkt_planeByName
#define PktDesc_getPlaneByType     DasPkt_planeOfType
#define PktDesc_getPlaneIdx        DasPkt_planeIdx
#define PktDesc_getPlaneIdxByName  DasPkt_planeIdxByName
#define PktDesc_getPlaneIdxByType  DasPkt_planeIdxOfType
#define PktDesc_getPlaneType       DasPkt_planeTypeAt
#define PktDesc_getXPlane          DasPkt_xPlane
#define PktDesc_recBytes           DasPkt_recBytes
#define PktDesc_setValue           DasPkt_setValAt
#define PktDesc_setValues          DasPkt_setVals
#define PktDesc_validate(...)      PktDesc_validate_is_private__streams_are_read_and_written_by_DasIO
#define PlaneDesc_copy             new_DasPlane_copy
#define PlaneDesc_decode           DasPlane_decode
#define PlaneDesc_decodeData(...)  PlaneDesc_decodeData_is_private__streams_are_read_and_written_by_DasIO
#define PlaneDesc_encode(...)      PlaneDesc_encode_is_private__streams_are_read_and_written_by_DasIO
#define PlaneDesc_encodeData(...)  PlaneDesc_encodeData_is_private__streams_are_read_and_written_by_DasIO
#define PlaneDesc_equivalent       DasPlane_isEquivalent
#define PlaneDesc_getDatum         DasPlane_datumAt
#define PlaneDesc_getFill          DasPlane_fill
#define PlaneDesc_getNItems        DasPlane_numItems
#define PlaneDesc_getName          DasPlane_name
#define PlaneDesc_getOrMakeYTags   DasPlane_makeYTags
#define PlaneDesc_getType          DasPlane_type
#define PlaneDesc_getUnits         DasPlane_units
#define PlaneDesc_getValEncoder    DasPlane_valEnc
#define PlaneDesc_getValue         DasPlane_valAt
#define PlaneDesc_getValues        DasPlane_vals
#define PlaneDesc_getYTagInterval  DasPlane_yTagInter
#define PlaneDesc_getYTagSeries    DasPlane_yTagSeq
#define PlaneDesc_getYTagSpec      DasPlane_yTagType
#define PlaneDesc_getYTagUnits     DasPlane_yTagUnits
#define PlaneDesc_getYTags         DasPlane_yTags
#define PlaneDesc_isFill           DasPlane_isFill
#define PlaneDesc_setFill          DasPlane_setFill
#define PlaneDesc_setNItems        DasPlane_setNumItems
#define PlaneDesc_setName          DasPlane_setName
#define PlaneDesc_setTimeValue     DasPlane_setTimeAt
#define PlaneDesc_setUnits         DasPlane_setUnits
#define PlaneDesc_setValEncoder    DasPlane_setValEnc
#define PlaneDesc_setValue         DasPlane_setValAt
#define PlaneDesc_setValues        DasPlane_setVals
#define PlaneDesc_setYTagSeries    DasPlane_setYTagSeq
#define PlaneDesc_setYTagUnits     DasPlane_setYTagUnits
#define PlaneDesc_setYTags         DasPlane_setYTags
#define new_PktDesc                new_DasPkt
#define new_PktDesc_xml(...)       new_PktDesc_xml_is_private__streams_are_read_and_written_by_DasIO
#define new_PlaneDesc              new_DasPlane
#define new_PlaneDesc_empty(...)   new_PlaneDesc_empty_is_private__use_new_DasPlane
#define new_PlaneDesc_pairs(...)   new_PlaneDesc_pairs_is_private__streams_are_read_and_written_by_DasIO
#define new_PlaneDesc_yscan        new_DasPlane_yscan
#define new_PlaneDesc_yscan_series new_DasPlane_yscanSeq

/* ************************************************************************* */
/* Streams */

/* StreamDesc became DasStream.  Packet descriptors are now one kind of
   DasDesc a stream holds (datasets are the other), so the *PktDesc names
   that work on either kind lost the "Pkt". */
/* The stream version got its own setter (DasIO_setOutVer) and the model setter
   took the "set" prefix to match.  DasIO_model() is the model GETTER now, so
   a v2.3.0 call that set the model no longer compiles: use DasIO_setModel().
   The version string macros became DasStream_verToStr() so the one list of
   versions lives in stream.h. */
#define DAS_22_STREAM_VER           DasStream_verToStr(DASSTREAMVER_2_2)
#define DAS_30_STREAM_VER           DasStream_verToStr(DASSTREAMVER_3_0)

/* The packet-only writers merged into the ones that take any top-level
   descriptor, and the caller now names the ID; these use the one the
   packet has in its stream, as the old calls did. */
#define DasIO_writePktDesc(IO, P)   DasIO_writeDesc((IO), (DasDesc*)(P), _das2_compat_pktId(P))
#define DasIO_writePktData(IO, P)   DasIO_writeData((IO), (DasDesc*)(P), _das2_compat_pktId(P))

/* Removal is by descriptor now, and an unused or out of range ID is a
   quiet false, as the old isValidId gave */
static inline bool _das2_compat_isValidId(const DasStream* pSd, int nPktId)
{
	if((nPktId < 1)||(nPktId >= DASSTREAM_V3_ENDPKTID)) return false;
	return DasStream_desc(pSd, nPktId) != NULL;
}
static inline das_errcode _das2_compat_freePktDesc(DasStream* pSd, int nPktId)
{
	DasDesc* pDesc = _das2_compat_isValidId(pSd, nPktId) ? DasStream_desc(pSd, nPktId) : NULL;
	if(pDesc == NULL)
		return daserrf(DASERR_STREAM, "No descriptor for packet ID %d", nPktId);
	return DasStream_freeDesc(pSd, pDesc);
}

#define stream_descriptor           das_stream_t   /* struct tag */
#define StreamDesc                  DasStream
#define StreamDescHandler           dasfn_onStream
#define new_StreamDesc              new_DasStream
#define new_StreamDesc_str(...)     new_StreamDesc_str_is_private__streams_are_read_and_written_by_DasIO
#define StreamDesc_copy             new_DasStream_copy
#define StreamDesc_getNPktDesc(S)   DasStream_numDesc((S), DASDESCTYPE_UNK)
#define StreamDesc_addPktDesc       DasStream_addDesc
#define StreamDesc_freePktDesc(S, ID) _das2_compat_freePktDesc((S), (ID))
#define StreamDesc_setMonotonic     DasStream_setMonotonic
#define StreamDesc_addStdProps      DasStream_addStdProps
#define StreamDesc_addCmdLineProp   DasStream_addArgvProp
#define StreamDesc_createPktDesc    DasStream_createPkt
#define StreamDesc_clonePktDesc(...) StreamDesc_clonePktDesc_is_private__use_DasStream_clonePkt
#define StreamDesc_clonePktDescById DasStream_clonePkt
#define StreamDesc_isValidId(S, ID)  _das2_compat_isValidId((S), (ID))
#define StreamDesc_getPktDesc       DasStream_pkt

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
#define copy_PktDesc(...)  copy_PktDesc_removed__use_DasStream_clonePkt

/* Properties are no longer a fixed size table, DasDesc_length() counts them */
#define DAS_XML_MAXPROPS  DAS_XML_MAXPROPS_removed__use_DasDesc_numProps
#define DasDesc_hasAnyProps(...) DasDesc_hasAnyProps_removed__use_DasDesc_numProps

/* DasDesc_get() was the same call as DasDesc_getStr() */
#define DasDesc_get             DasDesc_val

/* ************************************************************************* */
/* Datasets, dimensions and the builder */

#define dasds_iterator   DasDsIter
#define dasds_iter_init  init_DasDsIter
#define dasds_iter_next  DasDsIter_next

/* DasDs_getDim() now looks dimensions up by name, the index lookup kept its
   behavior under a new name */
#define DasDs_getDim  DasDs_dimAt

/* Dimensions gained a name within their kind, so several can share a kind
   ("Search_Coil" and "DC_MAG" are both "magnetic_field").  NULL makes the
   name the same as the kind, the old behavior. */
#define new_DasDim(S, T, R)      new_DasDim((S), NULL, (T), (R))
#define DasDs_makeDim(D, T, S)   DasDs_makeDim((D), (T), (S), NULL)

/* Both return the name= attribute of a das3 header, and are now called
   that.  "id" was easy to take for the packet ID. */
#define DasDs_id   DasDs_name
#define DasDim_id  DasDim_name
#define DasDs_getDimById  DasDs_dim

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
#define DASVAR_MAXERR  DASDIM_ROLE_MAX_ERR
#define DASVAR_MINERR  DASDIM_ROLE_MIN_ERR

/* "uncertainty" named no defined quantity */
#define DASVAR_UNCERT  DASVAR_UNCERT_removed__use_DASDIM_ROLE_STD_DEV_or_MIN_ERR_MAX_ERR
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
#define stream_from_path    new_DasStream_file

/* An array of pointers is a plain DasAry of unknown type, pointer sized,
   with a NULL fill (the compound literal is that fill value) */
#define new_DasPtrAry(T, R, S) \
	new_DasAry((T), DASVT_UNK, sizeof(void*), (const uint8_t*)&(void*){NULL}, \
	           (R), (S), DASUNITS_DIMENSIONLESS)

/* Declared in v2.3.0, never implemented, any use was already a link error */
#define DasAry_cmp(...)     DasAry_cmp_never_existed
#define DasBuf_paragraph(...) DasBuf_paragraph_never_existed

/* DasAry_setSrc() only fed the builder's das2 packet matching */
#define DasAry_setSrc(...)  DasAry_setSrc_removed__no_replacement

/* ************************************************************************* */
/* Index vocabulary: the DASIDX_* names are v2.3.0's and keep their values,
   EXCEPT the maximum rank: DASIDX_MAX was 16 in v2.3.0 and is 8 now, and
   the _INIT_ lists shrank to match.  Arrays sized DASIDX_MAX and filled
   from DASIDX_INIT_* still agree with each other. */

#define DASIDX_FUNC         DASIDX_BORROW   /* renamed DASIDX_BORROW, then this */
#define DEGEN               DASIDX_UNUSED

/* The globals are gone, these compound literals still work as memcpy()
   sources and in sizeof */
#define g_aShapeUnused ((const ptrdiff_t[DASIDX_MAX])DASIDX_INIT_UNUSED)
#define g_aShapeZeros  ((const ptrdiff_t[DASIDX_MAX])DASIDX_INIT_BEGIN)

/* ************************************************************************* */
/* Values and datums */

/* v2.3.0's const byte sequence struct tag, das_byteseq_t, is the non-const
   tag now (same layout).  Name it das_cbyteseq_t, or use DasCByteSeq. */
#define das_byteseq    DasCByteSeq

/* das_datum_toDbl() returned the value, DAS_FILL_VALUE on failure */
static inline double _das2_compat_datum_toDbl(const DasDatum* pDm)
{
	double rVal;
	return DasDatum_toDouble(pDm, &rVal) ? rVal : DASVAL_FILL;
}
#define das_datum_toDbl(P) _das2_compat_datum_toDbl(P)

#define das_vt_cmpAny  dasval_cmpAny

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

#define das_variable        das_var_t   /* struct tag, the fields all changed */

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

#define DasVarAry_getArray  DasVar_ary

/* Returned true on success.  A variable that needs caller scratch
   (DasVar_getNeedsBuf) fails here, call DasVar_get() directly for those. */
#define DasVar_getDatum(P, L, D) \
	(DasVar_datumAt((P), (L), (DasByteSeq){NULL, 0}, (D)) == 0)

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
	DasVar_valSize_removed__use_dasvt_size_of_elemType
#define DasVar_isFill(...) \
	DasVar_isFill_removed__compare_with_DasAry_fill_of_DasVar_ary

/* Declared but never implemented, any use was already a link error */
#define DasVar_id(...)          DasVar_id_never_existed
#define DasVar_orthoginal(...)  DasVar_orthoginal_never_existed__use_DasVar_isDegenerate
#define new_DasVarEval(...)     new_DasVarEval_never_existed__use_new_DasAry_materialize

/* ************************************************************************* */
/* Fourier transforms and power spectra: the classes follow the object naming
   rules now and are opaque.  Read the input length with DasDft_length() or
   DasPsd_length(), not a struct member. */

#define DftPlan             DasDftPlan
#define dft_plan            das_dftplan_t   /* struct tag */
#define new_DftPlan         new_DasDftPlan
#define del_DftPlan         del_DasDftPlan

#define Das2Dft             DasDft
#define new_Dft             new_DasDft
#define del_Dft             del_DasDft
#define Dft_calculate       DasDft_calculate
#define Dft_getReal         DasDft_real
#define Dft_getImg          DasDft_imag
#define Dft_getMagnitude    DasDft_amplitude

#define Das2Psd             DasPsd
#define new_Psd             new_DasPsd
#define del_Das2Psd         del_DasPsd
#define Psd_calculate       DasPsd_calculate
#define Psd_calculate_f     DasPsd_calcFloat
#define Psd_powerRatio      DasPsd_powerRatio
#define Psd_get             DasPsd_power

/* ************************************************************************* */
/* Credentials */

/* The credentials manager was redone for 3.0: records and secrets are set
   separately, lookups copy into caller buffers, and stores are configured
   rather than named on each save.  The old calls are retired, not aliased. */
#define new_CredMngr(...)          new_CredMngr_removed__use_new_DasCredMgr_then_DasCredMgr_setStore
#define del_CredMngr(...)          del_CredMngr_removed__use_del_DasCredMgr
#define CredMngr_addCred(...)      CredMngr_addCred_removed__use_DasCredMgr_addCred_and_DasCredMgr_setSecret
#define CredMngr_getCred(...)      CredMngr_getCred_removed__use_DasCredMgr_findCred_and_DasCredMgr_secret
#define CredMngr_addUserPass(...)  CredMngr_addUserPass_removed__use_DasCredMgr_addCred_and_DasCredMgr_setUserPass
#define CredMngr_getHttpAuth(...)  CredMngr_getHttpAuth_is_private__the_http_functions_call_it
#define CredMngr_authFailed(...)   CredMngr_authFailed_is_private__the_http_functions_call_it
#define CredMngr_setPrompt(...)    CredMngr_setPrompt_removed__use_DasCredMgr_setPrompt
#define CredMngr_load(...)         CredMngr_load_removed__use_DasCredMgr_setStore_and_DasCredMgr_load
#define CredMngr_save(...)         CredMngr_save_removed__use_DasCredMgr_setStore_and_DasCredMgr_save

#define DasCredMngr_addUserPass(...)  DasCredMngr_addUserPass_removed__use_DasCredMgr_addCred_and_DasCredMgr_setUserPass
#define DasCredMngr_getHttpAuth(...)  DasCredMngr_getHttpAuth_is_private__the_http_functions_call_it
#define DasCredMngr_authFailed(...)   DasCredMngr_authFailed_is_private__the_http_functions_call_it

#define das_credential    das_credential_removed__use_DasCred_which_holds_no_secret
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

#define das_http_getBody(...)        das_http_getBody_removed__use_new_DasHttpResp_get_and_DasHttpResp_isOk
#define DasHttpResp_clear(...)       DasHttpResp_clear_removed__responses_come_from_new_DasHttpResp_get
#define DasHttpResp_freeFields(...)  DasHttpResp_freeFields_removed__use_del_DasHttpResp
#define DasHttpResp_init(...)        DasHttpResp_init_removed__use_DasUrlParts_fromStr
#define DasHttpResp_useSsl(...)      DasHttpResp_useSsl_removed__take_the_body_with_DasHttpResp_takeBody
#define das_url_toStr(...)           das_url_toStr_removed__use_DasHttpResp_url
#define das_ssl_getErr(...)          das_ssl_getErr_is_private__TLS_stays_inside_the_library

/* URLs have no fixed size parts now, das_url_parse() splits one in place */
#define das_url            das_url_removed__see_DasUrlParts_fromStr
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
#define getDas2Fill()        DASVAL_FILL
#define isDas2Fill           dasval_isFill

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
#define das_xml_escape(D, S, L)  dasxml_escape((S), (D), (L))

/* Was a second copy of das_xml_escape() with the arguments in another order.
   Written in v2.3.0 order, the adapter above puts it right. */
#define _das_escape_xml(D, L, S) ((void)das_xml_escape((D), (S), (L)))

/* ************************************************************************* */
/* JSON */

/* The json.h names all carry das_ now, this one was missed */
#define json_parse_error_info DasJparseResult_info

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
	DasTime dt = {0};
	dt.year = (int)yr;  dt.month = (int)mt;  dt.mday = (int)dy;
	dt.hour = (int)hr;  dt.minute = (int)mn;
	dt.second = sc + ms*1.0e-3 + us*1.0e-6 + ns*1.0e-9;
	return DasTime_toTt2k(&dt);
}
#define das_utc_to_tt2K(Y, M, D, h, m, s, ms, us, ns) \
	_das2_compat_utc_to_tt2K((Y), (M), (D), (h), (m), (s), (ms), (us), (ns))

static inline void _das2_compat_tt2K_to_utc(
	long long tt, double* yr, double* mt, double* dy, double* hr, double* mn,
	double* sc, double* ms, double* us, double* ns
){
	int y, m, d, h, n, s;
	int32_t nNano;
	DasTime_tt2kToUtc(tt, &y, &m, &d, &h, &n, &s, &nNano);
	*yr = y;  *mt = m;  *dy = d;  *hr = h;  *mn = n;  *sc = s;
	*ms = nNano / 1000000;  *us = (nNano / 1000) % 1000;  *ns = nNano % 1000;
}
#define das_tt2K_to_utc(T, Y, M, D, h, m, s, ms, us, ns) \
	_das2_compat_tt2K_to_utc((T), (Y), (M), (D), (h), (m), (s), (ms), (us), (ns))

#define das_tt2K_to_us2K(T) Units_convertTo(DASUNITS_US2000, (T), DASUNITS_TT2000)
#define das_us2K_to_tt2K(U) Units_convertTo(DASUNITS_TT2000, (U), DASUNITS_US2000)

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
	((void)(S), DasIO_exceptionf((IO), dasexcepttype_id(T), "%s", (M)), del_DasIO(IO))
#define DasIO_serverExcept(IO, ...) \
	(DasIO_exceptionf((IO), DASEXCEPTTYPE_SERVER_ERR, __VA_ARGS__), 11)
#define DasIO_queryExcept(IO, ...) \
	(DasIO_exceptionf((IO), DASEXCEPTTYPE_QUERY_ERR, __VA_ARGS__), 11)
#define DasIO_closeNoData(IO, ...) \
	(DasIO_exceptionf((IO), DASEXCEPTTYPE_NO_DATA, __VA_ARGS__), 0)
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
#define das_error_msg         das_error_msg_removed__see_DasLogMsg_in_log_h
#define das_abort_on_error(...)  das_abort_on_error_removed__break_on_daserr_funcf_in_a_debugger
#define das_exit_on_error(...)   das_exit_on_error_removed__use_daserr_setDisp_DASERRDISP_EXIT
#define das_return_on_error(...) das_return_on_error_removed__use_daserr_setDisp_DASERRDISP_RET
#define DASERR_DIS_ABORT      DASERR_DIS_ABORT_removed__break_on_daserr_funcf_in_a_debugger
#define das_error_disposition daserr_disp
#define das_error_func        daserr_funcf
#define das_error_func_fixed  daserr_func

/* Log handlers take a das_logmsg and a pUser pointer, set as a pair */
#define daslog_sethandler(...) daslog_sethandler_removed__use_daslog_setHook
#define das_log_handler_t     das_log_handler_t_removed__use_dasfn_onLog
#define daslog_set_showline(...) daslog_set_showline_removed__use_daslog_showLine_bool
#define DASLOG_NOTHING        DASLOG_NOTHING_removed__DASLOG_CRITICAL_is_the_top_level

/* To-stream logging: a log line is a comment whose type names the level,
   DasIO_comment_v(pOut, "log:info", ...).  Readers filter, not writers. */
#define DasIO_sendLog(...)    DasIO_sendLog_removed__use_DasIO_commentf
#define DasIO_setLogLvl(...)  DasIO_setLogLvl_removed__readers_filter_by_comment_type
#define DasIO_getLogLvl(...)  DasIO_getLogLvl_never_existed
#define LOGLVL_FINEST   LOGLVL_FINEST_removed__use_DasIO_commentf
#define LOGLVL_FINER    LOGLVL_FINER_removed__use_DasIO_commentf
#define LOGLVL_FINE     LOGLVL_FINE_removed__use_DasIO_commentf
#define LOGLVL_CONFIG   LOGLVL_CONFIG_removed__use_DasIO_commentf
#define LOGLVL_INFO     LOGLVL_INFO_removed__use_DasIO_commentf
#define LOGLVL_WARNING  LOGLVL_WARNING_removed__use_DasIO_commentf
#define LOGLVL_ERROR    LOGLVL_ERROR_removed__use_DasIO_commentf

/* ************************************************************************* */
/* The v3.0 names, 2026-11 (co_notes/api_rename.csv)
 *
 * Every public name settled into one shape: classes are CamelCase with
 * Class_method() calls, plain types and integer types are lower case, and
 * each constant carries its owner's name as its prefix.  Old names that
 * kept their meaning map straight across.  A few are not mapped, the old
 * call fails to compile instead:
 *
 *   - Renamed struct members.  A macro can't alias a member, the compiler
 *     names the member it no longer finds.
 *   - dataset, the old struct tag of DasDs.  A #define of an ordinary word
 *     would rename every variable spelled that way.  Write DasDs.
 *   - Names v3.0 reuses with another meaning, DasIO_model and
 *     das_byteseq_t, see their notes above.
 */

/* units.h */
#define das_units                DasUnits
#define UNIT_US2000              DASUNITS_US2000
#define UNIT_MJ1958              DASUNITS_MJ1958
#define UNIT_T2000               DASUNITS_T2000
#define UNIT_T1970               DASUNITS_T1970
#define UNIT_NS1970              DASUNITS_NS1970
#define UNIT_UTC                 DASUNITS_UTC
#define UNIT_TT2000              DASUNITS_TT2000
#define UNIT_SECONDS             DASUNITS_SECONDS
#define UNIT_HOURS               DASUNITS_HOURS
#define UNIT_DAYS                DASUNITS_DAYS
#define UNIT_MILLISECONDS        DASUNITS_MILLISECONDS
#define UNIT_MICROSECONDS        DASUNITS_MICROSECONDS
#define UNIT_NANOSECONDS         DASUNITS_NANOSECONDS
#define UNIT_HERTZ               DASUNITS_HERTZ
#define UNIT_KILO_HERTZ          DASUNITS_KILO_HERTZ
#define UNIT_MEGA_HERTZ          DASUNITS_MEGA_HERTZ
#define UNIT_E_SPECDENS          DASUNITS_E_SPECDENS
#define UNIT_B_SPECDENS          DASUNITS_B_SPECDENS
#define UNIT_NT                  DASUNITS_NT
#define UNIT_NUMBER_DENS         DASUNITS_NUMBER_DENS
#define UNIT_DB                  DASUNITS_DB
#define UNIT_KM                  DASUNITS_KM
#define UNIT_EV                  DASUNITS_EV
#define UNIT_DEGREES             DASUNITS_DEGREES
#define UNIT_DIMENSIONLESS       DASUNITS_DIMENSIONLESS
#define Units_fromStr            DasUnits_fromStr
#define Units_toStr              DasUnits_toStr
#define Units_toLabel            DasUnits_toLbl
#define Units_invert             DasUnits_invert
#define Units_multiply           DasUnits_mul
#define Units_divide             DasUnits_div
#define Units_power              DasUnits_pow
#define Units_root               DasUnits_root
#define Units_interval           DasUnits_toInterval
#define Units_reduce             DasUnits_reduce
#define Units_canConvert         DasUnits_canConvert
#define Units_canMerge           DasUnits_canMerge
#define Units_haveCalRep         DasUnits_isTimePoint

/* time.h */
#define Units_convertToDt        DasTime_fromPoint
#define Units_secondsSinceMidnight DasTime_pointToSod
#define Units_getJulianDay       DasTime_pointToJdn
#define das_time                 DasTime
#define DAS_TIME_NULL            DASTIME_NULL
#define dt_null                  DasTime_clear
#define dt_now                   DasTime_now
#define dt_nano_1970             DasTime_toNs1970
#define dt_ttime                 DasTime_toSec1958
#define dt_set                   DasTime_set
#define dt_compare               DasTime_compare
#define dt_diff                  DasTime_diff
#define dt_tnorm                 DasTime_normalize

/* datum.h */
#define das_datum                DasDatum
#define datum_t                  das_datum_t
#define das_datum_valid          DasDatum_isValid
#define das_datum_fromStr        DasDatum_fromStr
#define das_datum_fromDbl        DasDatum_fromDouble
#define das_datum_wrapStr        DasDatum_wrapStr
#define das_datum_byteSeq        DasDatum_wrapBytes
#define das_datum_toEpoch        DasDatum_toPoint

/* value.h */
#define DAS_FILL_VALUE           DASVAL_FILL
#define DAS_INT64_FILL           DASVAL_FILL_INT64
#define DAS_INT32_FILL           DASVAL_FILL_INT32
#define das_val_type             das_vt
#define das_val_type_e           das_vt_e
#define vtByte                   DASVT_BYTE
#define vtUShort                 DASVT_USHORT
#define vtShort                  DASVT_SHORT
#define vtInt                    DASVT_INT
#define vtLong                   DASVT_LONG
#define vtFloat                  DASVT_FLOAT
#define vtDouble                 DASVT_DOUBLE
#define vtTime                   DASVT_TIME
#define vtIndex                  DASVT_INDEX
#define vtText                   DASVT_TEXT
#define vtByteSeq                DASVT_BYTESEQ
#define vtUnknown                DASVT_UNK
#define das_vt_toStr             dasvt_name
#define das_vt_size              dasvt_size
#define das_vt_fill              dasvt_fill
#define das_valcmp_func          dasfn_valCmp
#define das_vt_getcmp            dasvt_cmpFunc
#define das_vt_merge             dasvt_merge
#define das_within               dasval_within

/* str.h */
#define das_str2double           dasstr_toDouble
#define das_str2int              dasstr_toInt
#define das_str2bool             dasstr_toBool
#define das_str2baseint          dasstr_toBaseInt
#define das_strn2baseint         dasstr_toBaseIntN
#define das_csv2doubles          dascsv_newDoubles
#define das_doubles2csv          dascsv_fromDoubles
#define das_string               dasstr_newf
#define das_vstring              dasstr_newvf
#define das_strdup               dasstr_new
#define das_strtod_c             dasstr_toDoubleCLocale

/* defs.h */
#define HOST_IS_LSB_FIRST        DASHOST_LSB_FIRST

/* log.h */
#define DASLOG_CRIT              DASLOG_CRITICAL
#define daslog_setlevel          daslog_setLevel
#define daslog_strlevel          daslog_levelFromStr
/* v2.3.0 had no function name argument */
#define daslog(L, F, N, ...) daslog_funcf((L), (F), NULL, (N), __VA_ARGS__)
#define daslog_trace_v           daslog_tracef
#define daslog_debug_v           daslog_debugf
#define daslog_info_v            daslog_infof
#define daslog_warn_v            daslog_warnf
#define daslog_error_v           daslog_errorf
#define daslog_critical_v        daslog_criticalf

/* util.h */
#define DASERR_DIS_EXIT          DASERRDISP_EXIT
#define DASERR_DIS_RET           DASERRDISP_RET
#define das_init                 daslib_init
#define das_lib_version          daslib_version
#define das_finish               daslib_finish
#define das_error                daserrf
#define DAS_XML_BUF_LEN          DASXML_BUF_LEN
#define DAS_XML_NODE_NAME_LEN    DASXML_NAME_LEN
#define das_isdir                dasfs_isDir
#define das_isfile               dasfs_isFile
#define das_copyfile             dasfs_copyFile
#define das_dirlist              dasfs_listDir

/* array.h */
#define DAS_MAX_ID_BUFSZ         DASID_SZ
#define das_assert_valid_id      dasid_check
#define RANK_1                   DAS_SHAPE1
#define RANK_2                   DAS_SHAPE2
#define RANK_3                   DAS_SHAPE3
#define RANK_4                   DAS_SHAPE4
#define RANK_5                   DAS_SHAPE5
#define RANK_6                   DAS_SHAPE6
#define RANK_7                   DAS_SHAPE7
#define RANK_8                   DAS_SHAPE8
#define DIM0                     DAS_ADDR0
#define DIM1_AT                  DAS_ADDR1
#define DIM2_AT                  DAS_ADDR2
#define DIM3_AT                  DAS_ADDR3
#define DIM4_AT                  DAS_ADDR4
#define DIM5_AT                  DAS_ADDR5
#define DIM6_AT                  DAS_ADDR6
#define DIM7_AT                  DAS_ADDR7
#define DIM1                     DAS_IDX1
#define DIM2                     DAS_IDX2
#define DIM3                     DAS_IDX3
#define DIM4                     DAS_IDX4
#define DIM5                     DAS_IDX5
#define DIM6                     DAS_IDX6
#define DIM7                     DAS_IDX7
#define das_array                das_ary_t
#define DasAry_disownElements    DasAry_disownData
#define DasAry_ownsElements      DasAry_ownsData
#define DasAry_getUsage          DasAry_usage
#define DasAry_valType           DasAry_vt
#define DasAry_valTypeStr        DasAry_vtName
#define DasAry_getFill           DasAry_fill
#define DasAry_validAt           DasAry_isValidAt
#define DasAry_getAt             DasAry_valAt
#define DasAry_putAt             DasAry_setValsAt
#define DasAry_getIn             DasAry_valsIn
#define DasAry_getBuf            DasAry_dataIn
#define DasAry_subSetIn          new_DasAry_slice
#define DasAry_getFloatAt        DasAry_floatAt
#define DasAry_getDoubleAt       DasAry_doubleAt
#define DasAry_getByteAt         DasAry_byteAt
#define DasAry_getUShortAt       DasAry_ushortAt
#define DasAry_getShortAt        DasAry_shortAt
#define DasAry_getIntAt          DasAry_intAt
#define DasAry_getLongAt         DasAry_longAt
#define DasAry_getTimeAt         DasAry_timeAt
#define DasAry_getTextAt         DasAry_textAt
#define DasAry_getPtrAt          DasAry_ptrAt
#define DasAry_getFloatsIn       DasAry_floatsIn
#define DasAry_getDoublesIn      DasAry_doublesIn
#define DasAry_getCharsIn        DasAry_charsIn
#define DasAry_getBytesIn        DasAry_bytesIn
#define DasAry_getUShortsIn      DasAry_ushortsIn
#define DasAry_getShortsIn       DasAry_shortsIn
#define DasAry_getIntsIn         DasAry_intsIn
#define DasAry_getLongsIn        DasAry_longsIn
#define DasAry_getTimesIn        DasAry_timesIn
#define DasAry_getTextIn         DasAry_textIn

/* io.h */
#define das_io_struct            das_io_t
#define DasIO_addProcessor       DasIO_addProc
#define DasIO_setTaskSize        DasIO_setProgressMax
#define DasIO_setTaskProgress    DasIO_setProgress

/* iter.h */
#define dasds_iterator_t         das_dsiter_t

/* oper.h */
#define D2OP_INVALID             DASOP_INVALID
#define D2UOP_SIGN               DASOP_SIGN
#define D2UOP_SQRT               DASOP_SQRT
#define D2UOP_CURT               DASOP_CURT
#define D2UOP_LOG10              DASOP_LOG10
#define D2UOP_LN                 DASOP_LN
#define D2UOP_COS                DASOP_COS
#define D2UOP_SIN                DASOP_SIN
#define D2UOP_TAN                DASOP_TAN
#define D2UOP_SQUARE             DASOP_SQUARE
#define D2UOP_CUBE               DASOP_CUBE
#define D2UOP_INV                DASOP_INV
#define D2UOP_INVSQ              DASOP_INVSQ
#define D2UOP_INVCUBE            DASOP_INVCUBE
#define D2BOP_ADD                DASOP_ADD
#define D2BOP_SUB                DASOP_SUB
#define D2BOP_MUL                DASOP_MUL
#define D2BOP_DIV                DASOP_DIV
#define D2BOP_POW                DASOP_POW
#define D2OP_BEFORE              DASOP_POS_BEFORE
#define D2OP_BETWEEN             DASOP_POS_BETWEEN
#define D2OP_AFTER               DASOP_POS_AFTER
#define das_op_unary             dasop_unary
#define das_op_binary            dasop_binary
#define das_op_toStr             dasop_name
#define das_op_isBinary          dasop_isBinary
#define das_op_isUnary           dasop_isUnary

/* dsdf.h */
#define dsdf_parse               new_DasDesc_dsdf
#define dsdf_valToArray          dasdsdf_newDoubles
#define dsdf_setIdlBin           dasdsdf_setIdlBin
#define dsdf_valToNormParam      dasdsdf_normParam

/* http.h */
#define DASHTTP_TO_MIN           DASHTTP_TIMEOUT_MIN
#define DASHTTP_TO_MULTI         DASHTTP_TIMEOUT_MULTI
#define DASHTTP_TO_MAX           DASHTTP_TIMEOUT_MAX
#define das_http_readUrl         new_DasAry_http

/* json.h */
#define das_json_obj_s           das_jdo_t
#define das_json_parse           new_DasJdo_parse
#define das_json_parse_ex        new_DasJdo_parseEx
#define DasJdo_get               DasJdo_subtree
#define DasJdo_string            DasJdo_str
#define DasJdo_writeMinified     DasJdo_newMiniStr
#define DasJdo_writePretty       DasJdo_newPrettyStr
#define das_json_type_e          das_jsontype_e
#define das_json_type_str        DASJSONTYPE_STR
#define das_json_type_num        DASJSONTYPE_NUM
#define das_json_type_dict       DASJSONTYPE_DICT
#define das_json_type_ary        DASJSONTYPE_ARY
#define das_json_type_true       DASJSONTYPE_TRUE
#define das_json_type_false      DASJSONTYPE_FALSE
#define das_json_type_null       DASJSONTYPE_NULL
#define das_json_parse_flags_e   das_jparse_e
#define das_jparse_flags_default DASJPARSE_DEFAULT
#define das_jparse_flags_allow_trailing_comma DASJPARSE_ALLOW_TRAILING_COMMA
#define das_jparse_flags_allow_unquoted_keys DASJPARSE_ALLOW_UNQUOTED_KEYS
#define das_jparse_flags_allow_global_object DASJPARSE_ALLOW_GLOBAL_OBJECT
#define das_jparse_flags_allow_equals_in_object DASJPARSE_ALLOW_EQUALS_IN_OBJECT
#define das_jparse_flags_allow_no_commas DASJPARSE_ALLOW_NO_COMMAS
#define das_jparse_flags_allow_c_style_comments DASJPARSE_ALLOW_C_STYLE_COMMENTS
#define das_jparse_flags_deprecated DASJPARSE_DEPRECATED
#define das_jparse_flags_allow_location_information DASJPARSE_ALLOW_LOCATION_INFORMATION
#define das_jparse_flags_allow_single_quoted_strings DASJPARSE_ALLOW_SINGLE_QUOTED_STRINGS
#define das_jparse_flags_allow_hexadecimal_numbers DASJPARSE_ALLOW_HEXADECIMAL_NUMBERS
#define das_jparse_flags_allow_leading_plus_sign DASJPARSE_ALLOW_LEADING_PLUS_SIGN
#define das_jparse_flags_allow_leading_or_trailing_decimal_point DASJPARSE_ALLOW_LEADING_OR_TRAILING_DECIMAL_POINT
#define das_jparse_flags_allow_inf_and_nan DASJPARSE_ALLOW_INF_AND_NAN
#define das_jparse_flags_allow_multi_line_strings DASJPARSE_ALLOW_MULTI_LINE_STRINGS
#define das_jparse_flags_allow_simplified_json DASJPARSE_ALLOW_SIMPLIFIED_JSON
#define das_jparse_flags_allow_json5 DASJPARSE_ALLOW_JSON5
#define das_jparse_error_e       das_jparseerr_e
#define das_jparse_error_none    DASJPARSEERR_NONE
#define das_jparse_error_expected_comma_or_closing_bracket DASJPARSEERR_EXPECTED_COMMA_OR_CLOSING_BRACKET
#define das_jparse_error_expected_colon DASJPARSEERR_EXPECTED_COLON
#define das_jparse_error_expected_opening_quote DASJPARSEERR_EXPECTED_OPENING_QUOTE
#define das_jparse_error_invalid_string_escape_sequence DASJPARSEERR_INVALID_STRING_ESCAPE_SEQUENCE
#define das_jparse_error_invalid_number_format DASJPARSEERR_INVALID_NUMBER_FORMAT
#define das_jparse_error_invalid_value DASJPARSEERR_INVALID_VALUE
#define das_jparse_error_premature_end_of_buffer DASJPARSEERR_PREMATURE_END_OF_BUFFER
#define das_jparse_error_invalid_string DASJPARSEERR_INVALID_STRING
#define das_jparse_error_allocator_failed DASJPARSEERR_ALLOCATOR_FAILED
#define das_jparse_error_unexpected_trailing_characters DASJPARSEERR_UNEXPECTED_TRAILING_CHARACTERS
#define das_jparse_error_unknown DASJPARSEERR_UNKNOWN
#define das_json_parse_result_s  das_jparseresult_t
#define das_json_str             DasJsonStr
#define das_json_str_s           das_jsonstr_t
#define das_json_str_ex          DasJsonStrEx
#define das_json_str_ex_s        das_jsonstrex_t
#define das_json_num             DasJsonNum
#define das_json_num_s           das_jsonnum_t
#define das_json_dict_el         DasJsonDictEl
#define das_json_dict_el_s       das_jsondictel_t
#define das_json_dict            DasJsonDict
#define das_json_dict_s          das_jsondict_t
#define das_json_ary_el          DasJsonAryEl
#define das_json_ary_el_s        das_jsonaryel_t
#define das_json_ary             DasJsonAry
#define das_json_ary_s           das_jsonary_t
#define das_json_val_ex          DasJsonValEx
#define das_json_val_ex_s        das_jsonvalex_t

/* buffer.h */
#define das_buffer               das_buf_t
#define DasBuf_reinit            DasBuf_reset

/* enc.h */
#define das_encoding             das_enc_t
#define DASENC_FMT_LEN           DASENC_FMT_SZ
#define DASENC_TYPE_LEN          DASENC_TYPE_SZ
#define DasEnc_copy              new_DasEnc_copy
#define DasEnc_setAsciiFormat    DasEnc_setAsciiFmt
#define DasEnc_setTimeFormat     DasEnc_setTimeFmt

/* desc.h */
#define das_descriptor           das_desc_t
#define DasDesc_equals           DasDesc_isEqual
#define DasDesc_length           DasDesc_numProps
#define DasDesc_getNameByIdx     DasDesc_propNameAt
#define DasDesc_getValByIdx      DasDesc_valAt
#define DasDesc_getTypeByIdx     DasDesc_propTypeAt
#define DasDesc_getType          DasDesc_propType
#define DasDesc_remove           DasDesc_rmProp
#define DasDesc_set              DasDesc_setProp
#define DasDesc_getStr           DasDesc_val
#define DasDesc_setStr           DasDesc_setVal
#define DasDesc_vSetStr          DasDesc_setValf
#define DasDesc_getInt           DasDesc_valInt
#define DasDesc_setInt           DasDesc_setValInt
#define DasDesc_getBool          DasDesc_valBool
#define DasDesc_setBool          DasDesc_setValBool
#define DasDesc_getDouble        DasDesc_valDouble
#define DasDesc_setDouble        DasDesc_setValDouble
#define DasDesc_getDatum         DasDesc_valDatum
#define DasDesc_setDatum         DasDesc_setValDatum
#define DasDesc_getDoubleAry     DasDesc_newValDoubles
#define DasDesc_setDoubleArray   DasDesc_setValDoubles
#define DasDesc_setFloatAry      DasDesc_setValFloats
#define DasDesc_getStrAry        DasDesc_valStrs
#define DasDesc_getArray         DasDesc_valStrsSep
#define DasDesc_setDatumRng      DasDesc_setValDatumRng
#define DasDesc_getStrRng        DasDesc_valRng

/* dim.h */
#define DASDIM_UNK               DASDIMTYPE_UNK
#define DASDIM_COORD             DASDIMTYPE_COORD
#define DASDIM_DATA              DASDIMTYPE_DATA
#define DASVAR_CENTER            DASDIM_ROLE_CENTER
#define DASVAR_MIN               DASDIM_ROLE_MIN
#define DASVAR_MAX               DASDIM_ROLE_MAX
#define DASVAR_WIDTH             DASDIM_ROLE_WIDTH
#define DASVAR_MEAN              DASDIM_ROLE_MEAN
#define DASVAR_MEDIAN            DASDIM_ROLE_MEDIAN
#define DASVAR_MODE              DASDIM_ROLE_MODE
#define DASVAR_REF               DASDIM_ROLE_REF
#define DASVAR_OFFSET            DASDIM_ROLE_OFFSET
#define DASVAR_STD_DEV           DASDIM_ROLE_STD_DEV
#define DASVAR_WEIGHT            DASDIM_ROLE_WEIGHT
#define das_dim                  das_dim_t
#define DasDim_getVar            DasDim_var
#define DasDim_getPointVar       DasDim_pointVar

/* packet.h */
#define MAXPLANES                DASPKT_MAX_PLANES

/* var.h */
#define DasVar_valType           DasVar_vt
#define DasVar_subset            new_DasAry_subset
#define das_varindex_merge       dasshape_merge
#define das_varlength_merge      dasshape_mergeLen
#define das_varindex_prndir      dasshape_setPrnDir
#define das_rng2shape            dasshape_fromRng

/* creds.h */
#define das_cred_init            init_DasCred
#define das_prompt               dasfn_prompt
#define DasCredMngr              DasCredMgr
#define das_credmngr             das_credmgr_t

/* node.h */
#define das_node_type_e          das_nodetype
#define das_node                 das_node_t
#define DasNode_getJdo           DasNode_jdo

/* spice.h */
#define das_spice_err_setup      dasspice_catchErrors
#define das_print_spice_error    dasspice_prnError

/* IDXn(...) built a bare index list.  The At calls take a count and a list
   now, both from DAS_ADDRn(...), and THE DIGIT SHIFTS UP ONE: the old digit
   named the last index reached, the new one counts the values, so
   IDX1(9, 151) becomes DAS_ADDR2(9, 151). */
#define IDX0(...) IDX0_removed__use_DAS_ADDR1
#define IDX1(...) IDX1_removed__use_DAS_ADDR2
#define IDX2(...) IDX2_removed__use_DAS_ADDR3
#define IDX3(...) IDX3_removed__use_DAS_ADDR4
#define IDX4(...) IDX4_removed__use_DAS_ADDR5
#define IDX5(...) IDX5_removed__use_DAS_ADDR6
#define IDX6(...) IDX6_removed__use_DAS_ADDR7
#define IDX7(...) IDX7_removed__use_DAS_ADDR8

/* Index name strings, used only by the library's own messages */
#define g_sIdxLower  g_sIdxLower_is_private
#define g_sIdxUpper  g_sIdxUpper_is_private

/* RNG_n(...) were never used, no replacement */
#define RNG_1(...) RNG_1_removed__no_replacement
#define RNG_2(...) RNG_2_removed__no_replacement
#define RNG_3(...) RNG_3_removed__no_replacement
#define RNG_4(...) RNG_4_removed__no_replacement
#define RNG_5(...) RNG_5_removed__no_replacement
#define RNG_6(...) RNG_6_removed__no_replacement
#define RNG_7(...) RNG_7_removed__no_replacement

/* das_send_spice_err() took the type string too.  This header can't include
   <das3/spice.h>, which needs the CSPICE headers, and a function-like macro
   would mangle that file's prototype if it came second, so the adapter is
   only defined when spice.h was included FIRST. */
#ifdef _das2_spice_H_
#define das_send_spice_err(V, T) das_send_spice_err((V), dasexcepttype_id(T))
#endif

#endif /* _das2_compat_h_ */
