/* Copyright (C) 2026 Chris Piker <chris-piker@uiowa.edu>
 *
 * This file is part of das2C, the Core Das2 C Library.
 *
 * das2C is free software; you can redistribute it and/or modify it under
 * the terms of the GNU Lesser General Public License version 2.1 as published
 * by the Free Software Foundation.
 *
 * das2C is distributed in the hope that it will be useful, but WITHOUT ANY
 * WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS
 * FOR A PARTICULAR PURPOSE.  See the GNU Lesser General Public License for
 * more details.
 *
 * You should have received a copy of the GNU Lesser General Public License
 * version 2.1 along with das2C; if not, see <http://www.gnu.org/licenses/>.
 */

/* ## Implementation notes for uri.c
 *
 * This file is a skeleton.  The notes below record design decisions made
 * during the header phase so they are not lost before implementation begins.
 * All `#include` lines and type definitions present here will be needed when
 * real code is written.
 */

#define _das_uri_c_

#include <stdbool.h>
#include <stdint.h>
#include <string.h>
#include <stdlib.h>
#include <stdio.h>

/* POSIX directory traversal — Windows uses das2/win_dirent.h */
#ifdef _WIN32
#  include <das2/win_dirent.h>
#else
#  include <dirent.h>
#endif

#include <das2/defs.h>
#include <das2/time.h>    /* das_time, dt_parsetime, dt_tnorm, dt_compare  */
#include <das2/datum.h>   /* das_datum, das_datum_toTime, das_datum_toDbl  */
#include <das2/log.h>     /* daslog_warn_v                                  */
#include <das2/uri.h>


/* ========================================================================= */
/* ## Segment roles
 *
 * Five structural roles cover everything the parser needs to know about a
 * segment.  Which sub-field a DURI_COORD segment represents is stored as
 * sCoord + DasUriField.sLong inside the segment, filled at parse time from
 * the registered DasUriSegDef tables.  No role value is needed for the
 * scheme prefix (file://, http://, etc.) — that information lives on
 * DasUriTplt.eProto; the leading text is consumed during pattern() and
 * produces no segment.
 */

typedef enum das_uri_role_e {
	DURI_LITERAL = 0,  /* plain text                                          */
	DURI_COORD,        /* a named coordinate sub-field                        */
	DURI_WILD,         /* $x  — opaque wildcard, lexicographic-last           */
	DURI_VER,          /* $v  — version wildcard, numeric-greatest            */
} DasUriRole;


/* ========================================================================= */
/* ## Version comparison strategies */

typedef enum das_uri_ver_type_e {
	DURI_VER_SEP   = 's', /* dot-separated integers, compare component-wise  */
	DURI_VER_INT   = 'i', /* single integer, compare numerically              */
	DURI_VER_ALPHA = 'a', /* lexicographic, same as DURI_WILD                 */
} DasUriVerType;


/* ========================================================================= */
/* ## Segment struct
 *
 * The header contains only `typedef struct das_uri_seg_t DasUriSeg;`
 * (forward declaration).  The full definition lives here so that segment
 * internals remain private to the implementation.
 *
 * ### Union layout
 *
 * DURI_LITERAL needs one string.
 * DURI_COORD needs a coordinate name and a copy of the matched DasUriField.
 * DURI_WILD and DURI_VER need no storage beyond role + modifiers.
 *
 * ### Reuse of DasUriField
 *
 * DasUriField is the public sub-field descriptor from uri.h.  Embedding a
 * copy in the coord branch means the segment is fully self-describing after
 * parse time — no back-reference to the DasUriSegDef table is needed during
 * rendering or matching.
 */

#define DURI_MAX_LIT   128   /* max literal text in one segment              */

struct das_uri_seg_t {
	uint8_t      uRole;              /* one of DasUriRole                     */
	bool         bNoPad;             /* pad=none modifier                     */
	uint8_t      uVerType;           /* DasUriVerType (DURI_VER only)         */
	int          nDelta;             /* coverage-duration hint, default 1     */
	union {
		char sText[DURI_MAX_LIT];    /* DURI_LITERAL                          */
		struct {                     /* DURI_COORD                            */
			char        sCoord[32];  /* coordinate name, e.g. "time", "sclk" */
			DasUriField field;       /* copy of matched DasUriField entry     */
		} coord;
	};
};


/* ========================================================================= */
/* ## Built-in time coordinate definition
 *
 * `das_time_uridef()` returns a pointer to g_timeDef, which is backed by
 * g_aTimeFlds.  Both are file-scope statics; no allocation takes place.
 *
 * When DasUriTplt_register() copies this definition it deep-copies g_aTimeFlds
 * into the template's own heap storage, so the statics here are only the
 * source of truth — they are never modified after program startup.
 *
 * ### Datum extraction at render / match time
 *
 * The value for a time sub-field is extracted from a das_datum as follows:
 *
 * ```c
 * if(dm.vt == vtTime){
 *     // sCoord == "time", whole-coordinate datum
 *     das_time* pDt = (das_time*)(&dm);
 *     // dispatch on seg->coord.field.sLong:
 *     //   "year"   -> pDt->year
 *     //   "month"  -> pDt->month
 *     //   "mday"   -> pDt->mday
 *     //   "yday"   -> pDt->yday
 *     //   "hour"   -> pDt->hour
 *     //   "minute" -> pDt->minute
 *     //   "second" -> (int)pDt->seconds
 * } else {
 *     // sCoord == "time.year" etc., or any non-time coordinate
 *     // das_vt_isint(dm.vt) must be true (validated in init_DasUriIter)
 *     int nVal = (int)das_datum_toDbl(&dm);
 * }
 * ```
 *
 * Spacecraft-clock (SCLK) and similar compound types that do not fit any
 * existing das_val_type are handled the same way as "time.year": the caller
 * passes separate das_range entries per sub-field with das_vt_isint datums.
 */

static DasUriField g_aTimeFlds[] = {
	/* cShort  sLong        nWidth  nMin   nMax  */
	{  'Y',   "year",       4,      1678,  2262  },
	{  'm',   "month",      2,      1,     12    },
	{  'd',   "mday",       2,      1,     31    },
	{  'j',   "yday",       3,      1,     366   },
	{  'H',   "hour",       2,      0,     23    },
	{  'M',   "minute",     2,      0,     59    },
	{  'S',   "second",     2,      0,     60    },
};
#define N_TIME_FLDS  (int)(sizeof(g_aTimeFlds) / sizeof(DasUriField))

static DasUriSegDef g_timeDef = {
	"time",
	0,        /* nFields — filled by das_time_uridef() on first call          */
	NULL      /* pFields — filled by das_time_uridef() on first call          */
};

const DasUriSegDef* das_time_uridef(void)
{
	if(g_timeDef.pFields == NULL){
		g_timeDef.nFields = N_TIME_FLDS;
		g_timeDef.pFields = g_aTimeFlds;
	}
	return &g_timeDef;
}


/* ========================================================================= */
/* ## DasUriTplt_register() — deep-copy design
 *
 * Calling sequence:
 *
 * ```c
 * DasUriTplt* pTplt = new_DasUriTplt();
 * DasUriTplt_register(pTplt, das_time_uridef());
 * DasUriTplt_pattern(pTplt, "/data/$Y/$m/file_$Y$m$d_$v.cdf");
 * ```
 *
 * ### What register() must do
 *
 * 1. Grow pTplt->pDefs by one (realloc).
 * 2. memcpy the DasUriSegDef struct into the new slot.
 * 3. malloc a fresh DasUriField array of pDef->nFields entries.
 * 4. memcpy pDef->pFields into it.
 * 5. Point the new slot's pFields at the fresh copy.
 * 6. Increment pTplt->nDefs.
 *
 * ### What del_DasUriTplt() must free
 *
 * For each slot i in pTplt->pDefs:
 *   free(pTplt->pDefs[i].pFields)
 * free(pTplt->pDefs)
 * free(pTplt->pSegs)
 * free(pTplt)
 */


/* ========================================================================= */
/* ## DasUriTplt_pattern() — parser design
 *
 * Walk sTemplate left to right, consuming tokens:
 *
 * - `file://`, `http://`, `https://` prefix: set pTplt->eProto, advance past
 *   the prefix, produce no segment.
 *
 * - `$X` (single-char short token): search all registered DasUriSegDef tables
 *   for a DasUriField with cShort == X.  If found, emit DURI_COORD segment
 *   with sCoord = def->sCoord and field = matched entry.  If not found, emit
 *   daslog_warn_v() and treat as DURI_WILD.
 *
 * - `$(name)` or `$(name;mod=val;...)`: look up "name" in two ways:
 *   a. Is "name" a registered coord's sCoord?  If yes and coord has exactly
 *      one field, use that field.
 *   b. Is "name" a DasUriField.sLong in any registered coord?  If yes, use
 *      that field.
 *   c. Neither: emit warning, treat as DURI_WILD.
 *   Parse modifiers: `delta=N` -> seg.nDelta, `pad=none` -> seg.bNoPad,
 *   `type=sep|int|alpha` -> seg.uVerType (only meaningful for $v).
 *
 * - `$x`: emit DURI_WILD segment.
 * - `$v` or `$(v;type=...)`: emit DURI_VER segment; set uVerType from modifier
 *   (default DURI_VER_SEP).
 *
 * - Any other text up to the next '$' or end-of-string: emit DURI_LITERAL.
 *
 * After parsing, scan segments to set bHasWild and bLiteral on the template.
 *
 * ### Path/file boundary detection
 *
 * The scan algorithm in DasUriIter_next() needs to know which segments belong
 * to directory levels and which to the filename.  Detect this by scanning
 * pSegs for DURI_LITERAL segments containing '/'; the last '/' in the
 * rendered path marks the directory/filename split.
 */


/* ========================================================================= */
/* ## DasUriIter hidden scan state  (DasUriIter.pState)
 *
 * `DasUriIter` is stack-allocatable (fixed size in the header).  Its `pState`
 * member points to a heap-allocated `_DasUriScan` struct holding everything
 * the iterator needs between calls to DasUriIter_next().
 *
 * ### Directory stack
 *
 * Templates with directory-level coordinate fields (e.g. `/data/$Y/$m/...`)
 * require a stack of open DIR* handles, one per directory level.  When the
 * deepest level is exhausted the iterator pops and continues from the parent.
 * Maximum stack depth is bounded by the number of '/' path separators in the
 * template; DURI_MAX_DEPTH is a safe ceiling.
 *
 * ```c
 * #define DURI_MAX_DEPTH  32
 *
 * typedef struct das_uri_scan_t {
 *     DIR*   aDirs[DURI_MAX_DEPTH];             // open handles, one per level
 *     int    nDepth;                            // current depth
 *     char   aDirPath[DURI_MAX_DEPTH][DURI_MAX_PATH]; // path to each open dir
 *     char   sBestMatch[DURI_MAX_PATH];         // current best $x or $v match
 *     int    nBestVerNum;                       // numeric value for $v compare
 *     bool   bBestPending;                      // a buffered match is waiting
 * } _DasUriScan;
 * ```
 *
 * ### Scan algorithm (sketch)
 *
 * 1. `init_DasUriIter`: malloc a `_DasUriScan`, zero it, open root dir, push.
 * 2. `DasUriIter_next()`:
 *    a. If `bBestPending`: emit sBestMatch, clear flag, return.
 *    b. Read next dirent from top-of-stack.
 *    c. If subdirectory and template has a dir-level coord at this depth:
 *       decode the coord value from the entry name; if in range, open and push.
 *    d. If file and at file-name depth: match against template filename
 *       segments; filter by das_range constraints.
 *       - For $x/$v: update sBestMatch/nBestVerNum if this entry wins.
 *       - For coord segments: decode and range-check.
 *    e. If top-of-stack exhausted:
 *       - If sBestMatch is set, set bBestPending, close dir, pop.
 *       - Else close dir, pop; if stack empty set bDone = true, return NULL.
 *    f. Loop until a yield-ready entry is found or bDone is set.
 *
 * ### Version / wildcard selection
 *
 * $v and $x are resolved per-directory: all matching entries in a directory
 * are scanned and the winner (lex-last for $x, numeric-greatest for $v) is
 * buffered in sBestMatch.  It is yielded via the bBestPending mechanism when
 * the directory readdir is exhausted.
 *
 * Version collision (v1 == v01 under type=int): emit daslog_warn_v() naming
 * both files; use lexicographic last as tiebreaker.
 */


/* ========================================================================= */
/* ## Generic linked list sketch  (for future use, not part of public API)
 *
 * das_uri_free_list() returns a NULL-terminated char** for simplicity.  If a
 * richer list type is ever needed, here is a starting point.  The key feature
 * is the `listFree` destructor pointer so that generic traversal code can
 * free heterogeneous lists without knowing the element type.
 *
 * ```c
 * typedef struct das_list_node_t DasListNode;
 *
 * struct das_list_node_t {
 *     int          nType;    // caller-defined type tag
 *     DasListNode* pNext;
 *     void       (*listFree)(DasListNode* pThis);  // NULL = no-op
 * };
 * ```
 *
 * A concrete node embeds DasListNode as its first member so that a
 * `DasListNode*` can be cast to the concrete type safely:
 *
 * ```c
 * typedef struct {
 *     DasListNode node;      // MUST be first
 *     char*       sPath;
 * } DasPathNode;
 *
 * void DasPathNode_free(DasListNode* pThis){
 *     DasPathNode* p = (DasPathNode*)pThis;
 *     free(p->sPath);
 *     free(p);
 * }
 * ```
 */
