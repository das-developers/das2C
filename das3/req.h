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

/** @file req.h Requisitions: what a client wants from a data source */

#ifndef _das_req_h_
#define _das_req_h_

#include <das3/util.h>

#ifdef __cplusplus
extern "C" {
#endif

/** @addtogroup catalog
 * @{
 */

/** @defgroup req Requisitions
 *
 * A requisition is the list of settings a client wants from a data
 * source.  It is the form an application fills in, and it is deliberately
 * simple.  A DasReq holds strings naming entries in the interface section
 * of a data source node and nothing else: no hidden pointers, no parsed
 * values and no protocol parameters.  Everything that interprets a
 * requisition is a function of the DasSrc it is handed to, see
 * DasSrc_check() and DasStreamSrc_url() in source.h.
 *
 * That split lets a requisition outlive the interface it was built from.
 * It can be saved to a file one day, reloaded the next, and used to query
 * the data source again.
 *
 * @par Entries and verdicts
 *
 * Each entry is a key, a value and a verdict.  Keys are the dotted names
 * described under @ref src_keys.  Shorter spellings are not interpreted
 * by the requisition; they are kept as entered and the DasSrc resolves
 * them.  Verdicts start out unchecked.  DasSrc_check() validates every
 * entry against a source, marking each one ok or bad and attaching a
 * reason to the bad ones.
 *
 * Sources create requisitions.  DasSrc_newReq() hands back one seeded
 * with a worked example, or a blank one listing every required key with
 * an empty value so that a form knows what must be filled.  Either way
 * the requisition is stamped with the source URI, and a later
 * DasSrc_check() refuses to validate it against a different source.  A
 * requisition made with new_DasReq() has no stamp and is validated by
 * whatever source it is handed to.
 *
 * @par Raw parameters
 *
 * A requisition may also carry raw protocol parameters: name and value
 * pairs sent to the server exactly as given.  These bypass the interface
 * entirely and are for people who already know the server.  The source
 * applies them after everything else, so they win.  This is not the
 * intended use of a requisition, only an escape hatch.
 *
 * @par Saved requisitions
 *
 * DasReq_write() and new_DasReq_file() round trip a requisition through a
 * small JSON document, the same shape as an example in a source node with
 * a `source` block in front:
 *
 * @code
 * { "source": { "path": "tag:das2.org,2012:site:/uiowa/juno/wav/survey",
 *               "url": "https://jupiter.physics.uiowa.edu/.../flex.json",
 *               "version": "0.7" },
 *   "label": "Perijove 16, 30 second bins",
 *   "settings": { "coords/time/props/min": "2018-10-02T11:00",
 *                 "coords/time/props/max": "2018-10-04T11:00",
 *                 "coords/time/props/res": "30" },
 *   "params": { "extra": "1" } }
 * @endcode
 *
 * The path is the source's identity and is what a later check compares;
 * the URL records where that copy of the node came from.  The version is
 * the node format version.
 *
 * @todo Issue #42 tracks a content version.  Until the catalog spec adds
 * one, a stale requisition shows up as unknown keys at check time.
 */

/** How an entry fared the last time a source looked at it */
typedef enum das_req_verdict_enum {
	das_req_unchecked = 0,  /**< not validated since it was set */
	das_req_ok,             /**< the key resolved and the value is well formed */
	das_req_bad             /**< see DasReq_why() */
} das_req_verdict_e;

/** A requisition.  Create with new_DasReq() or DasSrc_newReq(), free with
 * del_DasReq().  Not thread safe for writers. */
typedef struct das_req DasReq;

/** An empty requisition with no source stamp */
DAS_API DasReq* new_DasReq(void);

/** A requisition read from the JSON text written by DasReq_text().
 * @returns a new requisition, or NULL after logging what was wrong with
 *        the text. */
DAS_API DasReq* new_DasReq_text(const char* sJson, size_t uLen);

/** A requisition read from a file written by DasReq_write(), or NULL
 * after logging why not. */
DAS_API DasReq* new_DasReq_file(const char* sPath);

/** A deep copy, verdicts included */
DAS_API DasReq* copy_DasReq(const DasReq* pThis);

DAS_API void del_DasReq(DasReq* pThis);

/* ************************************************************************* */
/* The stamp and the label */

/** Record which source this requisition is for.
 *
 * DasSrc_newReq() does this for you.  Call it yourself when adopting a
 * requisition built from loose strings so that later checks hold it to
 * one source.
 *
 * @param sPath The source's catalog URI, see DasSrc_path().  "" removes
 *        the stamp.
 * @param sUrl Where the source document was loaded from, kept for
 *        diagnostics; it plays no part in matching.
 * @param sVersion The node format version, or "".
 * @memberof DasReq
 */
DAS_API void DasReq_setSrc(
	DasReq* pThis, const char* sPath, const char* sUrl, const char* sVersion
);

/** The catalog URI of the stamped source, "" when unstamped.
 * @memberof DasReq */
DAS_API const char* DasReq_srcPath(const DasReq* pThis);

/** The URL of the stamped source, "" when unstamped.  @memberof DasReq */
DAS_API const char* DasReq_srcUrl(const DasReq* pThis);

/** The node format version of the stamped source, "" when unknown.
 * @memberof DasReq */
DAS_API const char* DasReq_srcVersion(const DasReq* pThis);

/** A short description, the example label when seeded from one, else
 * "".  @memberof DasReq */
DAS_API const char* DasReq_label(const DasReq* pThis);

/** @memberof DasReq */
DAS_API void DasReq_setLabel(DasReq* pThis, const char* sLabel);

/* ************************************************************************* */
/* Entries */

/** Set a value.
 *
 * The key and value are stored as given.  Nothing is resolved or checked
 * here, there is no source to ask; the entry's verdict becomes unchecked
 * and the next DasSrc_check() validates it.  Setting a key that is already
 * present replaces its value.  Keys compare case insensitively.
 *
 * @param sKey Any spelling from @ref src_keys, or a coordinate name
 *        alone for the range forms described there.
 * @param sVal The value.  "" is allowed and means the key is wanted but
 *        not yet filled in, which is how a blank requisition lists what
 *        a source requires.  NULL is treated as "".
 * @returns DAS_OKAY, or DASERR_SRC if memory ran out or the key is empty.
 * @memberof DasReq
 */
DAS_API DasErrCode DasReq_set(DasReq* pThis, const char* sKey, const char* sVal);

/** The value stored under a key, exactly as set, or NULL when the key is
 * not present.  Only keys set directly are found, the derived entries a
 * check adds are reached by index.  @memberof DasReq */
DAS_API const char* DasReq_value(const DasReq* pThis, const char* sKey);

/** Drop an entry.  @returns true if there was one.  @memberof DasReq */
DAS_API bool DasReq_remove(DasReq* pThis, const char* sKey);

/** Drop every entry and raw parameter, keeping the stamp and label.
 * @memberof DasReq */
DAS_API void DasReq_clear(DasReq* pThis);

/** The number of entries, including any a check derived, see
 * DasReq_derived().  @memberof DasReq */
DAS_API size_t DasReq_count(const DasReq* pThis);

/** Entry number uIdx's key, or NULL past the end.  @memberof DasReq */
DAS_API const char* DasReq_key(const DasReq* pThis, size_t uIdx);

/** Entry number uIdx's value, or NULL past the end.  @memberof DasReq */
DAS_API const char* DasReq_val(const DasReq* pThis, size_t uIdx);

/** @memberof DasReq */
DAS_API das_req_verdict_e DasReq_verdict(const DasReq* pThis, size_t uIdx);

/** Why entry number uIdx was rejected, "" unless its verdict is
 * das_req_bad.  The text names the key and is meant to be shown as is.
 * @memberof DasReq */
DAS_API const char* DasReq_why(const DasReq* pThis, size_t uIdx);

/** True for entries a check added rather than the client.
 *
 * A coordinate range given as one key, `time=2026-01-02,2026-01-03`,
 * expands to entries for the minimum and maximum, and a required key the
 * client never set is added with an empty value and a bad verdict so a
 * form can mark it.  Derived entries are rebuilt on every check and
 * vanish on the next set; a panel usually skips them when drawing and a
 * client reading values back should too.
 * @memberof DasReq
 */
DAS_API bool DasReq_derived(const DasReq* pThis, size_t uIdx);

/** Index of the entry for a key, or -1.  @memberof DasReq */
DAS_API int DasReq_find(const DasReq* pThis, const char* sKey);

/* ************************************************************************* */
/* Raw parameters */

/** Name a protocol parameter directly, see the group description.
 * @returns DAS_OKAY, or DASERR_SRC if memory ran out.  @memberof DasReq */
DAS_API DasErrCode DasReq_setRaw(DasReq* pThis, const char* sName, const char* sVal);

/** @memberof DasReq */
DAS_API size_t DasReq_rawCount(const DasReq* pThis);

/** Raw parameter number uIdx.
 * @param psVal Receives the value, may be NULL.
 * @returns The name, or NULL past the end.  @memberof DasReq */
DAS_API const char* DasReq_raw(const DasReq* pThis, size_t uIdx, const char** psVal);

/* ************************************************************************* */
/* Saving */

/** The requisition as JSON text, see the group description.
 *
 * Derived entries and verdicts are not written; they belong to a check,
 * not to the requisition.
 *
 * @returns A heap string to be released with free(), or NULL if memory
 *        ran out.
 * @memberof DasReq
 */
DAS_API char* DasReq_text(const DasReq* pThis);

/** Write DasReq_text() to a file, replacing any existing file.
 * @returns DAS_OKAY, or DASERR_SRC after logging why not.  @memberof DasReq */
DAS_API DasErrCode DasReq_write(const DasReq* pThis, const char* sPath);

/** @} */
/** @} */

#ifdef __cplusplus
}
#endif

#endif /* _das_req_h_ */
