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


/* ========================================================================= */
/* Stubs — replace one by one during implementation                          */

DasUriTplt* new_DasUriTplt(void)
{
	return (DasUriTplt*) calloc(1, sizeof(DasUriTplt));
}

DasErrCode DasUriTplt_register(DasUriTplt* pThis, const DasUriSegDef* pDef)
{
	/* duplicate coordinate name is an error */
	for(int i = 0; i < pThis->nDefs; ++i){
		if(strcmp(pThis->pDefs[i].sCoord, pDef->sCoord) == 0)
			return das_error(DASERR_URI,
				"coordinate '%s' is already registered", pDef->sCoord);
	}

	/* grow the def array by one slot */
	DasUriSegDef* pNew = (DasUriSegDef*) realloc(
		pThis->pDefs, (pThis->nDefs + 1) * sizeof(DasUriSegDef)
	);
	if(pNew == NULL)
		return das_error(DASERR_URI, "out of memory in DasUriTplt_register");
	pThis->pDefs = pNew;

	/* shallow-copy the def struct into the new slot */
	DasUriSegDef* pSlot = pThis->pDefs + pThis->nDefs;
	memcpy(pSlot, pDef, sizeof(DasUriSegDef));

	/* deep-copy the field array */
	pSlot->pFields = (DasUriField*) malloc(pDef->nFields * sizeof(DasUriField));
	if(pSlot->pFields == NULL)
		return das_error(DASERR_URI, "out of memory in DasUriTplt_register");
	memcpy(pSlot->pFields, pDef->pFields, pDef->nFields * sizeof(DasUriField));

	++pThis->nDefs;
	return DAS_OKAY;
}

/* Parse semicolon-separated modifiers from sBuf (modified in place) into pSeg. */
static void _parse_modifiers(char* sBuf, DasUriSeg* pSeg)
{
	char* pMod = sBuf;
	while(pMod && *pMod){
		char* pNext = strchr(pMod, ';');
		if(pNext) *pNext = '\0';
		char* pEq = strchr(pMod, '=');
		if(pEq){
			*pEq = '\0';
			const char* sKey = pMod;
			const char* sVal = pEq + 1;
			if(strcmp(sKey, "delta") == 0)
				pSeg->nDelta = atoi(sVal);
			else if(strcmp(sKey, "pad") == 0 && strcmp(sVal, "none") == 0)
				pSeg->bNoPad = true;
			else if(strcmp(sKey, "type") == 0){
				if     (strcmp(sVal, "int")   == 0) pSeg->uVerType = DURI_VER_INT;
				else if(strcmp(sVal, "alpha") == 0) pSeg->uVerType = DURI_VER_ALPHA;
				else                                pSeg->uVerType = DURI_VER_SEP;
			}
		}
		pMod = pNext ? pNext + 1 : NULL;
	}
}

/* Fill pSeg as a DURI_COORD by searching registered defs for sName.
 * Search order: (a) exact coord name with one field, (b) field long name.
 * Returns true on match, false if not found (caller should warn + use WILD). */
static bool _lookup_coord(
	DasUriTplt* pThis, const char* sName, DasUriSeg* pSeg
){
	/* (a) exact coord name, single-field coord */
	for(int i = 0; i < pThis->nDefs; ++i){
		if(strcmp(pThis->pDefs[i].sCoord, sName) == 0 &&
		   pThis->pDefs[i].nFields == 1)
		{
			pSeg->uRole = DURI_COORD;
			strncpy(pSeg->coord.sCoord, pThis->pDefs[i].sCoord, 31);
			pSeg->coord.sCoord[31] = '\0';
			pSeg->coord.field = pThis->pDefs[i].pFields[0];
			return true;
		}
	}
	/* (b) field long name in any registered coord */
	for(int i = 0; i < pThis->nDefs; ++i){
		for(int j = 0; j < pThis->pDefs[i].nFields; ++j){
			if(strcmp(pThis->pDefs[i].pFields[j].sLong, sName) == 0){
				pSeg->uRole = DURI_COORD;
				strncpy(pSeg->coord.sCoord, pThis->pDefs[i].sCoord, 31);
				pSeg->coord.sCoord[31] = '\0';
				pSeg->coord.field = pThis->pDefs[i].pFields[j];
				return true;
			}
		}
	}
	return false;
}

DasErrCode DasUriTplt_pattern(DasUriTplt* pThis, const char* sTemplate)
{
	const char* p = sTemplate;

	/* Strip scheme prefix */
	if(strncmp(p, "https://", 8) == 0){
		pThis->eProto = DURI_PROTO_HTTPS; p += 8;
	} else if(strncmp(p, "http://", 7) == 0){
		pThis->eProto = DURI_PROTO_HTTP;  p += 7;
	} else if(strncmp(p, "file://", 7) == 0){
		pThis->eProto = DURI_PROTO_FILE;  p += 7;
	} else {
		pThis->eProto = DURI_PROTO_FILE;
	}

	/* Pre-size segment array: 2*nDollars+2 is always sufficient */
	int nDollars = 0;
	for(const char* q = p; *q; ++q)
		if(*q == '$') ++nDollars;
	int nCap = 2 * nDollars + 2;
	if(nCap < 4) nCap = 4;

	pThis->pSegs = (DasUriSeg*) calloc(nCap, sizeof(DasUriSeg));
	if(pThis->pSegs == NULL)
		return das_error(DASERR_URI, "out of memory in DasUriTplt_pattern");
	pThis->nSegs = 0;

	/* Main scan */
	while(*p){
		if(*p != '$'){
			/* Collect literal text up to next '$' or end */
			const char* pStart = p;
			while(*p && *p != '$') ++p;
			int nLen = (int)(p - pStart);
			if(nLen == 0) continue;
			if(nLen >= DURI_MAX_LIT)
				return das_error(DASERR_URI,
					"literal segment exceeds %d chars in URI template", DURI_MAX_LIT);
			DasUriSeg* pSeg = &pThis->pSegs[pThis->nSegs++];
			pSeg->uRole = DURI_LITERAL;
			memcpy(pSeg->sText, pStart, nLen);
			pSeg->sText[nLen] = '\0';
		} else {
			++p; /* consume '$' */

			DasUriSeg* pSeg = &pThis->pSegs[pThis->nSegs++];
			pSeg->nDelta = 1;

			if(*p == '('){
				++p; /* consume '(' */
				const char* pClose = strchr(p, ')');
				if(pClose == NULL)
					return das_error(DASERR_URI, "unclosed $() in URI template");

				char sBuf[256];
				int nContent = (int)(pClose - p);
				if(nContent >= (int)sizeof(sBuf))
					return das_error(DASERR_URI, "$() content too long in URI template");
				memcpy(sBuf, p, nContent);
				sBuf[nContent] = '\0';
				p = pClose + 1;

				/* Split name from modifiers at first ';' */
				char sName[64];
				char* pSemi = strchr(sBuf, ';');
				int nName = pSemi ? (int)(pSemi - sBuf) : (int)strlen(sBuf);
				if(nName >= (int)sizeof(sName))
					return das_error(DASERR_URI, "name in $() too long");
				memcpy(sName, sBuf, nName);
				sName[nName] = '\0';

				if(strcmp(sName, "v") == 0){
					pSeg->uRole    = DURI_VER;
					pSeg->uVerType = DURI_VER_SEP;
				} else if(strcmp(sName, "x") == 0){
					pSeg->uRole = DURI_WILD;
				} else if(!_lookup_coord(pThis, sName, pSeg)){
					daslog_warn_v(
						"URI template: unrecognised token $(%s), treating as wildcard",
						sName
					);
					pSeg->uRole = DURI_WILD;
				}

				if(pSemi)
					_parse_modifiers(pSemi + 1, pSeg);

			} else {
				/* Single-char token: $X */
				char cShort = *p++;

				if(cShort == 'x'){
					pSeg->uRole = DURI_WILD;
				} else if(cShort == 'v'){
					pSeg->uRole    = DURI_VER;
					pSeg->uVerType = DURI_VER_SEP;
				} else {
					bool bFound = false;
					for(int i = 0; i < pThis->nDefs && !bFound; ++i){
						for(int j = 0; j < pThis->pDefs[i].nFields && !bFound; ++j){
							if(pThis->pDefs[i].pFields[j].cShort == cShort){
								pSeg->uRole = DURI_COORD;
								strncpy(pSeg->coord.sCoord,
								        pThis->pDefs[i].sCoord, 31);
								pSeg->coord.sCoord[31] = '\0';
								pSeg->coord.field = pThis->pDefs[i].pFields[j];
								bFound = true;
							}
						}
					}
					if(!bFound){
						daslog_warn_v(
							"URI template: unrecognised token $%c, treating as wildcard",
							cShort
						);
						pSeg->uRole = DURI_WILD;
					}
				}
			}
		}
	}

	/* Set template-level flags */
	pThis->bLiteral = true;
	pThis->bHasWild = false;
	for(int i = 0; i < pThis->nSegs; ++i){
		uint8_t r = pThis->pSegs[i].uRole;
		if(r == DURI_COORD)
			pThis->bLiteral = false;
		if(r == DURI_WILD || r == DURI_VER){
			pThis->bLiteral = false;
			pThis->bHasWild = true;
		}
	}

	return DAS_OKAY;
}

void del_DasUriTplt(DasUriTplt* pTplt)
{
	if(pTplt == NULL) return;
	for(int i = 0; i < pTplt->nDefs; ++i)
		free(pTplt->pDefs[i].pFields);
	free(pTplt->pDefs);
	free(pTplt->pSegs);
	free(pTplt);
}

char* DasUriTplt_toStr(const DasUriTplt* pThis, char* sBuf, int nLen)
{
	(void)pThis;
	if(nLen > 0) sBuf[0] = '\0';
	return sBuf;
}

/* Extract the integer value for a DURI_COORD segment from pRanges.
 * Tries whole-coordinate match first ("time"), then dotted sub-field
 * ("time.yday", "sclk.partition", etc.).
 * Returns 0 and sets *pVal on success; returns -1 if not constrained. */
static int _seg_value(
	const DasUriSeg* pSeg, int nRanges, const das_range* pRanges, int* pVal
){
	const char* sCoord = pSeg->coord.sCoord;
	const char* sLong  = pSeg->coord.field.sLong;

	char sDot[64];
	snprintf(sDot, sizeof(sDot), "%s.%s", sCoord, sLong);

	for(int i = 0; i < nRanges; ++i){
		/* Whole-coordinate match: only valid for vtTime datums */
		if(strcmp(pRanges[i].sCoord, sCoord) == 0 &&
		   pRanges[i].dBeg.vt == vtTime)
		{
			das_time dt;
			das_datum_toTime(&pRanges[i].dBeg, &dt);
			if     (strcmp(sLong, "year")   == 0) *pVal = dt.year;
			else if(strcmp(sLong, "month")  == 0) *pVal = dt.month;
			else if(strcmp(sLong, "mday")   == 0) *pVal = dt.mday;
			else if(strcmp(sLong, "yday")   == 0) *pVal = dt.yday;
			else if(strcmp(sLong, "hour")   == 0) *pVal = dt.hour;
			else if(strcmp(sLong, "minute") == 0) *pVal = dt.minute;
			else if(strcmp(sLong, "second") == 0) *pVal = (int)dt.second;
			else return -1;
			return 0;
		}
		/* Sub-field match: "time.yday", "sclk.partition", etc. */
		if(strcmp(pRanges[i].sCoord, sDot) == 0){
			*pVal = (int)das_datum_toDbl(&pRanges[i].dBeg);
			return 0;
		}
	}
	return -1;
}

char* DasUriTplt_render(
	const DasUriTplt* pThis, int nRanges, const das_range* pRanges,
	char* sBuf, int nLen
){
	char* pOut = sBuf;
	char* pEnd = sBuf + nLen - 1;  /* reserve one byte for null terminator */

	for(int i = 0; i < pThis->nSegs; ++i){
		const DasUriSeg* pSeg = &pThis->pSegs[i];

		switch(pSeg->uRole){
		case DURI_LITERAL: {
			int n = (int)strlen(pSeg->sText);
			if(pOut + n > pEnd) goto overflow;
			memcpy(pOut, pSeg->sText, n);
			pOut += n;
			break;
		}
		case DURI_WILD:
		case DURI_VER:
			if(pOut >= pEnd) goto overflow;
			*pOut++ = '*';
			break;
		case DURI_COORD: {
			int nVal = 0;
			if(_seg_value(pSeg, nRanges, pRanges, &nVal) != 0){
				/* not constrained — render as wildcard */
				if(pOut >= pEnd) goto overflow;
				*pOut++ = '*';
			} else {
				char sFmt[16];
				int nWidth = pSeg->coord.field.nWidth;
				if(nWidth > 0 && !pSeg->bNoPad)
					snprintf(sFmt, sizeof(sFmt), "%%0%dd", nWidth);
				else
					snprintf(sFmt, sizeof(sFmt), "%%d");
				char sTmp[32];
				int n = snprintf(sTmp, sizeof(sTmp), sFmt, nVal);
				if(pOut + n > pEnd) goto overflow;
				memcpy(pOut, sTmp, n);
				pOut += n;
			}
			break;
		}
		}
	}
	*pOut = '\0';
	return sBuf;

overflow:
	das_error(DASERR_URI, "rendered URI path would exceed %d chars", nLen);
	return NULL;
}

DasErrCode init_DasUriIter(
	DasUriIter* pThis, const DasUriTplt* pTplt,
	int nRanges, const das_range* pRanges
){
	(void)pTplt; (void)nRanges; (void)pRanges;
	pThis->bDone = true;
	pThis->pState = NULL;
	return DAS_OKAY;
}

void fini_DasUriIter(DasUriIter* pThis)
{
	(void)pThis;
}

DasUriIter* new_DasUriIter(
	const DasUriTplt* pTplt, int nRanges, const das_range* pRanges
){
	(void)pTplt; (void)nRanges; (void)pRanges;
	return NULL;
}

const char* DasUriIter_next(DasUriIter* pThis)
{
	(void)pThis;
	return NULL;
}

void del_DasUriIter(DasUriIter* pThis)
{
	(void)pThis;
}

char** das_uri_list(
	const char* sTemplate, int nRanges, const das_range* pRanges,
	size_t* pCount
){
	(void)sTemplate; (void)nRanges; (void)pRanges;
	if(pCount) *pCount = 0;
	return NULL;
}

void das_uri_free_list(char** ppList)
{
	(void)ppList;
}
