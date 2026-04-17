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
#include <errno.h>

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

#define DURI_MAX_LIT    128  /* max literal text in one segment              */
#define DURI_MAX_FIELDS  16  /* upper bound on COORD sub-segs extracted per    */
                             /* level; time=7, sclk=4 today — 16 is safe       */

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
/* ## Level plan  (DasUriTplt.pLevels)
 *
 * One path component of the template after decomposition.  A level is one
 * directory name (levels 0 .. nLevels-2) or the filename (level nLevels-1).
 * Literal segments in the parent template that span '/' boundaries are
 * deep-copied and split here so that each level's pSegs array contains only
 * sub-segments with no embedded '/'.
 *
 * Example — "/data/$Y/$j/file_$Y$j.cdf" decomposes to:
 *
 *     sBase   = "/data"
 *     level 0 = [ COORD($Y) ]                                         dir
 *     level 1 = [ COORD($j) ]                                         dir
 *     level 2 = [ LIT("file_"), COORD($Y), COORD($j), LIT(".cdf") ]   file
 *
 * Header forward-declares this as an opaque type (typedef struct
 * das_uri_level_t DasUriLevel).  Fields are private to uri.c.                */

struct das_uri_level_t {
	DasUriSeg* pSegs;    /* owned sub-segment array (deep-copied from pTplt) */
	int        nSegs;
	bool       bIsFile;  /* true for the basename level (last in plan)       */
	bool       bHasWild; /* true if any sub-segment is DURI_WILD / DURI_VER  */
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
 * Allocated in init_DasUriIter only when pTplt->nLevels > 0.  For a literal
 * template (bLiteral == true) no scan state is needed and pState stays NULL;
 * DasUriIter_next renders the path once and then sets bDone.
 *
 * ### Directory stack
 *
 * One _DasUriDepth entry exists per level of the template (pTplt->nLevels).
 * Depth D corresponds to scanning for level D: readdir is called on
 * pDepth[D].pDir and each entry is matched against pTplt->pLevels[D].pSegs.
 * If level D is a directory level, matched entries cause opendir + push to
 * depth D+1.  If level D is the file level (bIsFile), matched entries are
 * yielded.
 *
 * When nCurDepth == N, the open DIR* handles are pDepth[0 .. N-1] and the
 * deepest one is being actively read.  nCurDepth == 0 means no DIRs are
 * open (initial state, before the first DasUriIter_next() call).
 *
 * $x and $v buffering fields will be added in step 6c.                        */

typedef struct _das_uri_depth_t {
	DIR* pDir;                  /* open handle; NULL until this depth is live */
	char sPath[DURI_MAX_PATH];  /* full path of the directory opened here     */

	/* File-level $x/$v buffering — only used when this depth is the file
	 * level AND the level has bHasWild set.  Tracks the best match seen so
	 * far in the current parent directory. */
	bool bHaveBest;
	char sBestName[256];        /* filename of current best candidate        */
	char sBestWild[64];          /* wild-token string for the current best   */
} _DasUriDepth;

typedef struct das_uri_scan_t {
	_DasUriDepth* pDepth;    /* pTplt->nLevels entries; runtime walk state   */
	int           nCurDepth; /* count of currently-open DIRs in pDepth;      */
	                         /* 0 = none open, max = pTplt->nLevels          */
	bool          bFatal;    /* set at init if the template needs features    */
	                         /* not yet implemented (e.g. $x/$v in 6b); next  */
	                         /* returns NULL without opening any directory    */
} _DasUriScan;


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

/* Walk pTplt->pSegs to build sBase + pLevels.  Splits literals at '/' so each
 * resulting sub-segment represents either one path-component chunk or one
 * named/wildcard field.  Called at the end of DasUriTplt_pattern.  On failure
 * del_DasUriTplt cleans up whatever was allocated. */
static DasErrCode _decompose_levels(DasUriTplt* pThis)
{
	/* Literal templates need no level plan — bail out before any alloc. */
	if(pThis->bLiteral){
		pThis->sBase   = NULL;
		pThis->pLevels = NULL;
		pThis->nLevels = 0;
		return DAS_OKAY;
	}

	/* Step 1: collect leading literal text up to the first non-literal seg. */
	char sPre[DURI_MAX_PATH];
	int  nPre = 0;
	int  iFirstVar = pThis->nSegs;
	sPre[0] = '\0';

	for(int i = 0; i < pThis->nSegs; ++i){
		if(pThis->pSegs[i].uRole != DURI_LITERAL){
			iFirstVar = i;
			break;
		}
		const char* pText = pThis->pSegs[i].sText;
		int nLen = (int)strlen(pText);
		if(nPre + nLen >= DURI_MAX_PATH)
			return das_error(DASERR_URI,
				"URI template leading literal exceeds %d chars", DURI_MAX_PATH);
		memcpy(sPre + nPre, pText, nLen);
		nPre += nLen;
		sPre[nPre] = '\0';
	}

	/* Step 2: split sPre at the last '/' into sBase + level-0 leading text. */
	const char* sLvl0Lead = sPre;
	int         nLvl0Lead = nPre;
	char*       pLast     = strrchr(sPre, '/');

	if(pLast == NULL){
		/* No '/' in leading text — CWD-relative. */
		pThis->sBase = strdup(".");
	}
	else if(pLast == sPre){
		/* Leading character is '/' and it's the only or first '/' — root. */
		pThis->sBase = strdup("/");
		sLvl0Lead = sPre + 1;
		nLvl0Lead = nPre - 1;
	}
	else {
		int nBase = (int)(pLast - sPre);
		pThis->sBase = (char*)malloc(nBase + 1);
		if(pThis->sBase){
			memcpy(pThis->sBase, sPre, nBase);
			pThis->sBase[nBase] = '\0';
		}
		sLvl0Lead = pLast + 1;
		nLvl0Lead = nPre - nBase - 1;
	}
	if(pThis->sBase == NULL)
		return das_error(DASERR_URI, "out of memory in _decompose_levels");

	/* Step 3: count levels = 1 + number of '/' in all post-sBase literals. */
	int nLevels = 1;
	for(int i = iFirstVar; i < pThis->nSegs; ++i){
		if(pThis->pSegs[i].uRole == DURI_LITERAL)
			for(const char* p = pThis->pSegs[i].sText; *p; ++p)
				if(*p == '/') ++nLevels;
	}

	/* Step 4: allocate pLevels and per-level sub-segment arrays. */
	pThis->pLevels = (DasUriLevel*)calloc(nLevels, sizeof(DasUriLevel));
	if(pThis->pLevels == NULL)
		return das_error(DASERR_URI, "out of memory in _decompose_levels");
	pThis->nLevels = nLevels;

	/* Upper bound: any one level can hold at most nSegs + nLevels sub-segs. */
	int nPerLvl = pThis->nSegs + nLevels + 1;
	for(int i = 0; i < nLevels; ++i){
		pThis->pLevels[i].pSegs =
			(DasUriSeg*)calloc(nPerLvl, sizeof(DasUriSeg));
		if(pThis->pLevels[i].pSegs == NULL)
			return das_error(DASERR_URI, "out of memory in _decompose_levels");
	}

	/* Step 5: seed level 0 with any non-empty leading text. */
	DasUriLevel* pCur = &pThis->pLevels[0];
	if(nLvl0Lead > 0){
		if(nLvl0Lead >= DURI_MAX_LIT)
			return das_error(DASERR_URI,
				"level 0 leading literal exceeds %d chars", DURI_MAX_LIT);
		DasUriSeg* pS = &pCur->pSegs[pCur->nSegs++];
		pS->uRole = DURI_LITERAL;
		memcpy(pS->sText, sLvl0Lead, nLvl0Lead);
		pS->sText[nLvl0Lead] = '\0';
	}

	/* Step 6: walk post-sBase segments, pushing and splitting at '/'. */
	int iLvl = 0;
	for(int i = iFirstVar; i < pThis->nSegs; ++i){
		const DasUriSeg* pSeg = &pThis->pSegs[i];

		if(pSeg->uRole != DURI_LITERAL){
			/* Deep-copy into current level (struct copy carries sCoord/field). */
			pCur->pSegs[pCur->nSegs++] = *pSeg;
			continue;
		}

		const char* pText = pSeg->sText;
		while(*pText){
			const char* pSlash = strchr(pText, '/');
			int nChunk = pSlash ? (int)(pSlash - pText) : (int)strlen(pText);

			if(nChunk > 0){
				if(nChunk >= DURI_MAX_LIT)
					return das_error(DASERR_URI,
						"literal sub-segment exceeds %d chars", DURI_MAX_LIT);
				DasUriSeg* pS = &pCur->pSegs[pCur->nSegs++];
				pS->uRole = DURI_LITERAL;
				memcpy(pS->sText, pText, nChunk);
				pS->sText[nChunk] = '\0';
			}

			if(pSlash == NULL) break;

			/* Close current level, open next. */
			++iLvl;
			if(iLvl >= nLevels)
				return das_error(DASERR_URI,
					"URI template level count mismatch (internal)");
			pCur = &pThis->pLevels[iLvl];
			pText = pSlash + 1;
		}
	}

	/* Step 7: mark bIsFile, bHasWild on each level. */
	for(int i = 0; i < nLevels; ++i){
		pThis->pLevels[i].bIsFile = (i == nLevels - 1);
		for(int j = 0; j < pThis->pLevels[i].nSegs; ++j){
			uint8_t r = pThis->pLevels[i].pSegs[j].uRole;
			if(r == DURI_WILD || r == DURI_VER){
				pThis->pLevels[i].bHasWild = true;
				break;
			}
		}
	}

	/* Validation: filename level must have at least one sub-segment. */
	if(pThis->pLevels[nLevels - 1].nSegs == 0)
		return das_error(DASERR_URI,
			"URI template ends with '/' — filename component is empty");

	return DAS_OKAY;
}

DasErrCode DasUriTplt_pattern(DasUriTplt* pThis, const char* sTemplate)
{
#ifdef _WIN32
	/* On Windows, rewrite user-supplied '\' to '/' for file:// templates so
	 * the level decomposer sees a single separator.  http/https URLs keep
	 * '\' verbatim — backslashes are not path separators there and may be
	 * meaningful inside query strings. */
	char sNorm[DURI_MAX_PATH];
	{
		int nLen = (int)strlen(sTemplate);
		if(nLen >= DURI_MAX_PATH)
			return das_error(DASERR_URI,
				"URI template exceeds %d chars", DURI_MAX_PATH);
		memcpy(sNorm, sTemplate, nLen);
		sNorm[nLen] = '\0';
	}
	bool bIsHttp = (strncmp(sNorm, "http://",  7) == 0 ||
	                strncmp(sNorm, "https://", 8) == 0);
	if(!bIsHttp){
		for(char* q = sNorm; *q; ++q)
			if(*q == '\\') *q = '/';
	}
	const char* p = sNorm;
#else
	const char* p = sTemplate;
#endif

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

	/* Build the per-level plan used by the iterator (no-op for literal). */
	return _decompose_levels(pThis);
}

void del_DasUriTplt(DasUriTplt* pTplt)
{
	if(pTplt == NULL) return;
	for(int i = 0; i < pTplt->nDefs; ++i)
		free(pTplt->pDefs[i].pFields);
	free(pTplt->pDefs);
	free(pTplt->pSegs);
	for(int i = 0; i < pTplt->nLevels; ++i)
		free(pTplt->pLevels[i].pSegs);
	free(pTplt->pLevels);
	free(pTplt->sBase);
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
	/* Populate the caller-visible fields first, so fini is safe even if we
	 * return an error below. */
	pThis->pTplt       = pTplt;
	pThis->nRanges     = nRanges;
	pThis->pRanges     = pRanges;
	pThis->bDone       = false;
	pThis->sCurrent[0] = '\0';
	pThis->pState      = NULL;

	/* Only file:// (implicit or explicit) is wired up today. */
	if(pTplt->eProto != DURI_PROTO_FILE){
		pThis->bDone = true;
		return das_error(DASERR_URI,
			"HTTP and HTTPS URI templates are not yet implemented");
	}

	/* Literal templates need no scan state; next() renders and yields once. */
	if(pTplt->bLiteral)
		return DAS_OKAY;

	/* Allocate the private scan state + one _DasUriDepth per level. */
	_DasUriScan* pScan = (_DasUriScan*)calloc(1, sizeof(_DasUriScan));
	if(pScan == NULL)
		return das_error(DASERR_URI, "out of memory in init_DasUriIter");

	pScan->pDepth = (_DasUriDepth*)calloc(pTplt->nLevels, sizeof(_DasUriDepth));
	if(pScan->pDepth == NULL){
		free(pScan);
		return das_error(DASERR_URI, "out of memory in init_DasUriIter");
	}
	pScan->nCurDepth = 0;
	pScan->bFatal    = false;

	pThis->pState = pScan;
	return DAS_OKAY;
}

void fini_DasUriIter(DasUriIter* pThis)
{
	if(pThis == NULL || pThis->pState == NULL) return;

	_DasUriScan* pScan = (_DasUriScan*)pThis->pState;

	/* Close any DIR handles still open from partial/interrupted iteration. */
	for(int i = 0; i < pScan->nCurDepth; ++i){
		if(pScan->pDepth[i].pDir != NULL){
			closedir(pScan->pDepth[i].pDir);
			pScan->pDepth[i].pDir = NULL;
		}
	}

	free(pScan->pDepth);
	free(pScan);
	pThis->pState = NULL;
}

DasUriIter* new_DasUriIter(
	const DasUriTplt* pTplt, int nRanges, const das_range* pRanges
){
	DasUriIter* pThis = (DasUriIter*)calloc(1, sizeof(DasUriIter));
	if(pThis == NULL){
		das_error(DASERR_URI, "out of memory in new_DasUriIter");
		return NULL;
	}
	if(init_DasUriIter(pThis, pTplt, nRanges, pRanges) != DAS_OKAY){
		fini_DasUriIter(pThis);
		free(pThis);
		return NULL;
	}
	return pThis;
}

/* ========================================================================= */
/* ## Iterator helpers (step 6b)                                              */

/* Join sDir + '/' + sLeaf into sOut, avoiding a duplicate separator when
 * sDir already ends with one (e.g. sDir == "/" at POSIX root). */
static void _join_path(const char* sDir, const char* sLeaf, char* sOut, int nOut)
{
	int nDir = (int)strlen(sDir);
	bool bSep = (nDir > 0 && sDir[nDir - 1] != '/');
	snprintf(sOut, nOut, "%s%s%s", sDir, bSep ? "/" : "", sLeaf);
}

/* Output bundle from _match_entry.  Only pFieldVals/nVals are populated for
 * plain-COORD levels.  When the level has bHasWild, pWildStart/nWildLen/
 * uWildRole/uWildVerType describe the single $x or $v token's matched span.
 * (Multiple $x/$v per level is not yet supported — first wins.) */
typedef struct {
	int64_t     aVals[DURI_MAX_FIELDS];
	int         nVals;
	const char* pWildStart;    /* pointer into sName; NULL if level has no wild */
	int         nWildLen;      /* -1 if no wild span captured                    */
	uint8_t     uWildRole;     /* DURI_WILD or DURI_VER (0 if none)              */
	uint8_t     uWildVerType;  /* copied from $v seg for later comparison        */
} _MatchOut;

/* Match sName against pLvl->pSegs.  On success: pOut is populated with
 * extracted COORD values (in order) and — when the level has a $x/$v
 * segment — the wild span.  Returns false on non-match (with debug/warn
 * log as appropriate).
 *
 * Wildcard matching strategy: look ahead to the segment AFTER the $x/$v.
 * If the next segment is LITERAL, consume chars up to (but not including)
 * the first occurrence of that literal.  If $x/$v is the last segment in
 * the level, consume the rest of sName.  Other lookahead patterns return
 * false for now (ambiguous). */
static bool _match_entry(
	const DasUriLevel* pLvl, const char* sName, _MatchOut* pOut
){
	const char* p   = sName;
	int         nOut = 0;
	pOut->pWildStart   = NULL;
	pOut->nWildLen     = -1;
	pOut->uWildRole    = 0;
	pOut->uWildVerType = 0;

	for(int i = 0; i < pLvl->nSegs; ++i){
		const DasUriSeg* pSeg = &pLvl->pSegs[i];

		switch(pSeg->uRole){
		case DURI_LITERAL: {
			int nLit = (int)strlen(pSeg->sText);
			if(strncmp(p, pSeg->sText, nLit) != 0){
				daslog_debug_v(
					"skipping '%s': does not match literal '%s'",
					sName, pSeg->sText
				);
				return false;
			}
			p += nLit;
			break;
		}
		case DURI_COORD: {
			if(nOut >= DURI_MAX_FIELDS){
				das_error(DASERR_URI,
					"URI level has more than %d coord fields (internal limit)",
					DURI_MAX_FIELDS);
				return false;
			}
			int nWidth = pSeg->coord.field.nWidth;
			bool bFixed = (nWidth > 0 && !pSeg->bNoPad);

			int nTake = 0;
			if(bFixed){
				for(int k = 0; k < nWidth; ++k){
					if(p[k] < '0' || p[k] > '9'){
						daslog_debug_v(
							"skipping '%s': expected %d digits for $%c at offset %d",
							sName, nWidth, pSeg->coord.field.cShort, (int)(p - sName)
						);
						return false;
					}
				}
				nTake = nWidth;
			} else {
				while(p[nTake] >= '0' && p[nTake] <= '9') ++nTake;
				if(nTake == 0){
					daslog_debug_v(
						"skipping '%s': expected digits at offset %d",
						sName, (int)(p - sName)
					);
					return false;
				}
			}

			int64_t nVal = 0;
			for(int k = 0; k < nTake; ++k)
				nVal = nVal * 10 + (p[k] - '0');

			int nMin = pSeg->coord.field.nMin;
			int nMax = pSeg->coord.field.nMax;
			if(nVal < (int64_t)nMin || nVal > (int64_t)nMax){
				daslog_warn_v(
					"skipping '%s': %s=%lld out of coord bounds [%d..%d]",
					sName, pSeg->coord.field.sLong,
					(long long)nVal, nMin, nMax
				);
				return false;
			}

			pOut->aVals[nOut++] = nVal;
			p += nTake;
			break;
		}
		case DURI_WILD:
		case DURI_VER: {
			/* Determine wild span by look-ahead: next LITERAL terminates it,
			 * else if $x/$v is the last seg, take rest of name. */
			const DasUriSeg* pNext = (i + 1 < pLvl->nSegs)
				? &pLvl->pSegs[i + 1] : NULL;
			const char* pWildEnd = NULL;
			if(pNext == NULL){
				pWildEnd = p + strlen(p);
			} else if(pNext->uRole == DURI_LITERAL){
				pWildEnd = strstr(p, pNext->sText);
				if(pWildEnd == NULL){
					daslog_debug_v(
						"skipping '%s': wildcard end-marker '%s' not found",
						sName, pNext->sText
					);
					return false;
				}
			} else {
				daslog_debug_v(
					"skipping '%s': wildcard followed by non-literal segment "
					"(ambiguous match)", sName);
				return false;
			}

			int nLen = (int)(pWildEnd - p);
			if(nLen <= 0){
				daslog_debug_v(
					"skipping '%s': empty wildcard match", sName);
				return false;
			}

			/* Record the first wild span we see (only one supported per level). */
			if(pOut->pWildStart == NULL){
				pOut->pWildStart   = p;
				pOut->nWildLen     = nLen;
				pOut->uWildRole    = pSeg->uRole;
				pOut->uWildVerType = pSeg->uVerType;
			}
			p = pWildEnd;
			break;
		}
		}
	}

	if(*p != '\0'){
		daslog_debug_v(
			"skipping '%s': %d trailing chars unmatched",
			sName, (int)strlen(p)
		);
		return false;
	}

	pOut->nVals = nOut;
	return true;
}

/* Integer window for range comparison.  A simple [nLo1, nHi1] range covers
 * most cases; bTwo indicates a rollover where the valid set is
 * [nLo1, nHi1] ∪ [nLo2, nHi2] (e.g. spacecraft-clock mod64k crossing the
 * partition boundary at 65535/0).  Rollover is only recognised for dotted
 * sub-field ranges where dBeg > dEnd — when that happens we use the
 * segment's intrinsic nMin/nMax as the wrap bounds. */
typedef struct {
	int64_t nLo1, nHi1;
	bool    bTwo;
	int64_t nLo2, nHi2;
} _Bounds;

/* Look for a user range that constrains pSeg.  Returns true with pB
 * populated; false if the segment is unconstrained. */
static bool _seg_range(
	const DasUriSeg* pSeg, int nRanges, const das_range* pRanges,
	_Bounds* pB
){
	const char* sCoord = pSeg->coord.sCoord;
	const char* sLong  = pSeg->coord.field.sLong;

	char sDot[64];
	snprintf(sDot, sizeof(sDot), "%s.%s", sCoord, sLong);

	pB->bTwo = false;

	for(int i = 0; i < nRanges; ++i){
		/* Whole-coordinate time range: decompose to per-field integer window.
		 * The check is conservative — permissive for partial-field matches —
		 * to avoid false negatives.  The caller can apply exact time filtering
		 * after opening the file. */
		if(strcmp(pRanges[i].sCoord, sCoord) == 0 &&
		   pRanges[i].dBeg.vt == vtTime)
		{
			das_time dtLo, dtHi;
			das_datum_toTime(&pRanges[i].dBeg, &dtLo);
			das_datum_toTime(&pRanges[i].dEnd, &dtHi);
			if     (strcmp(sLong, "year")   == 0){ pB->nLo1 = dtLo.year;   pB->nHi1 = dtHi.year;   }
			else if(strcmp(sLong, "month")  == 0){ pB->nLo1 = dtLo.month;  pB->nHi1 = dtHi.month;  }
			else if(strcmp(sLong, "mday")   == 0){ pB->nLo1 = dtLo.mday;   pB->nHi1 = dtHi.mday;   }
			else if(strcmp(sLong, "yday")   == 0){ pB->nLo1 = dtLo.yday;   pB->nHi1 = dtHi.yday;   }
			else if(strcmp(sLong, "hour")   == 0){ pB->nLo1 = dtLo.hour;   pB->nHi1 = dtHi.hour;   }
			else if(strcmp(sLong, "minute") == 0){ pB->nLo1 = dtLo.minute; pB->nHi1 = dtHi.minute; }
			else if(strcmp(sLong, "second") == 0){ pB->nLo1 = (int64_t)dtLo.second;
			                                       pB->nHi1 = (int64_t)dtHi.second; }
			else continue;
			return true;
		}
		/* Dotted sub-field: integer range, half-open [dBeg, dEnd).  When
		 * dBeg > dEnd we interpret as a rollover crossing and split into two
		 * intervals using the segment's intrinsic bounds. */
		if(strcmp(pRanges[i].sCoord, sDot) == 0){
			int64_t nBeg = (int64_t)das_datum_toDbl(&pRanges[i].dBeg);
			int64_t nEnd = (int64_t)das_datum_toDbl(&pRanges[i].dEnd);
			if(nBeg <= nEnd){
				pB->nLo1 = nBeg;
				pB->nHi1 = nEnd - 1;
			} else {
				pB->nLo1 = nBeg;
				pB->nHi1 = pSeg->coord.field.nMax;
				pB->bTwo = true;
				pB->nLo2 = pSeg->coord.field.nMin;
				pB->nHi2 = nEnd - 1;
			}
			return true;
		}
	}
	return false;
}

/* Apply user ranges to the values just extracted from one level's entry.
 * Returns true if the entry should be kept, false if it should be filtered.
 * No logging: filtered entries are a legitimate, expected outcome. */
static bool _in_ranges(
	const DasUriLevel* pLvl, const int64_t* pFieldVals,
	int nRanges, const das_range* pRanges
){
	if(nRanges == 0) return true;

	int iVal = 0;
	for(int i = 0; i < pLvl->nSegs; ++i){
		const DasUriSeg* pSeg = &pLvl->pSegs[i];
		if(pSeg->uRole != DURI_COORD) continue;

		_Bounds b;
		if(_seg_range(pSeg, nRanges, pRanges, &b)){
			int64_t nVal = pFieldVals[iVal];
			bool bOk = (nVal >= b.nLo1 && nVal <= b.nHi1)
				|| (b.bTwo && nVal >= b.nLo2 && nVal <= b.nHi2);
			if(!bOk) return false;
		}
		++iVal;
	}
	return true;
}

/* Compare two version tokens under the given type rule.  Returns:
 *   >0 if sA > sB (A is a later version)
 *    0 if equal under the rule
 *   <0 if sA < sB
 *
 * For DURI_VER_SEP, split both on '.' and compare component-wise as integers.
 * For DURI_VER_INT, parse each as a single integer.
 * For DURI_VER_ALPHA (and DURI_WILD), use strcmp. */
static int _ver_cmp(const char* sA, const char* sB, uint8_t uType)
{
	if(uType == DURI_VER_INT){
		long long a = atoll(sA);
		long long b = atoll(sB);
		if(a < b) return -1;
		if(a > b) return  1;
		return 0;
	}
	if(uType == DURI_VER_SEP){
		const char* pA = sA;
		const char* pB = sB;
		while(*pA || *pB){
			long long a = 0, b = 0;
			while(*pA >= '0' && *pA <= '9'){ a = a*10 + (*pA - '0'); ++pA; }
			while(*pB >= '0' && *pB <= '9'){ b = b*10 + (*pB - '0'); ++pB; }
			if(a < b) return -1;
			if(a > b) return  1;
			if(*pA == '.') ++pA;
			if(*pB == '.') ++pB;
			if(!*pA && !*pB) return 0;
			if(!*pA) return -1;
			if(!*pB) return  1;
		}
		return 0;
	}
	/* DURI_VER_ALPHA / DURI_WILD */
	return strcmp(sA, sB);
}

/* Open the directory for scan-depth iDepth into pScan->pDepth[iDepth].
 * pScan->pDepth[iDepth].sPath must already be populated with the full path.
 * ENOENT is not an error — caller handles the NULL return by popping. */
static DIR* _open_dir(_DasUriScan* pScan, int iDepth)
{
	const char* sPath = pScan->pDepth[iDepth].sPath;
	DIR* pDir = opendir(sPath);
	if(pDir == NULL){
		daslog_debug_v("opendir('%s') failed: %s", sPath, strerror(errno));
	}
	pScan->pDepth[iDepth].pDir = pDir;
	return pDir;
}

const char* DasUriIter_next(DasUriIter* pThis)
{
	if(pThis == NULL || pThis->bDone) return NULL;

	const DasUriTplt* pTplt = pThis->pTplt;

	/* Literal-template fast path: render once on first call, then done. */
	if(pTplt->bLiteral){
		if(pThis->sCurrent[0] != '\0'){
			pThis->bDone = true;
			return NULL;
		}
		if(DasUriTplt_render(pTplt, pThis->nRanges, pThis->pRanges,
		                     pThis->sCurrent, DURI_MAX_PATH) == NULL){
			pThis->bDone = true;
			return NULL;
		}
		return pThis->sCurrent;
	}

	_DasUriScan* pScan = (_DasUriScan*)pThis->pState;
	if(pScan == NULL || pScan->bFatal){
		pThis->bDone = true;
		return NULL;
	}

	/* First call: seed depth 0 with sBase and open it. */
	if(pScan->nCurDepth == 0){
		snprintf(pScan->pDepth[0].sPath, DURI_MAX_PATH, "%s", pTplt->sBase);
		pScan->nCurDepth = 1;
		if(_open_dir(pScan, 0) == NULL){
			pThis->bDone = true;
			return NULL;
		}
	}

	_MatchOut match;

	/* Walk the depth stack until we yield a file or run out. */
	while(pScan->nCurDepth > 0){
		int iDepth = pScan->nCurDepth - 1;
		DIR* pDir  = pScan->pDepth[iDepth].pDir;
		const DasUriLevel* pLvl = &pTplt->pLevels[iDepth];

		struct dirent* pEnt = readdir(pDir);
		if(pEnt == NULL){
			/* End of directory — before popping, yield the buffered best
			 * match for a file-level wildcard level (6c). */
			if(pLvl->bIsFile && pLvl->bHasWild
			   && pScan->pDepth[iDepth].bHaveBest)
			{
				_join_path(pScan->pDepth[iDepth].sPath,
				           pScan->pDepth[iDepth].sBestName,
				           pThis->sCurrent, DURI_MAX_PATH);
				pScan->pDepth[iDepth].bHaveBest = false;
				closedir(pDir);
				pScan->pDepth[iDepth].pDir = NULL;
				--pScan->nCurDepth;
				return pThis->sCurrent;
			}
			closedir(pDir);
			pScan->pDepth[iDepth].pDir = NULL;
			--pScan->nCurDepth;
			continue;
		}

		const char* sName = pEnt->d_name;
		if(sName[0] == '.' && (sName[1] == '\0' ||
		                      (sName[1] == '.' && sName[2] == '\0')))
			continue;

		if(!_match_entry(pLvl, sName, &match))
			continue;
		if(!_in_ranges(pLvl, match.aVals, pThis->nRanges, pThis->pRanges))
			continue;

		if(pLvl->bIsFile){
			/* File level. */
			if(pLvl->bHasWild && match.pWildStart != NULL){
				/* 6c: buffer the best candidate; yield on dir exhaustion. */
				char sThisWild[64];
				int  nCopy = match.nWildLen < (int)sizeof(sThisWild) - 1
				             ? match.nWildLen
				             : (int)sizeof(sThisWild) - 1;
				memcpy(sThisWild, match.pWildStart, nCopy);
				sThisWild[nCopy] = '\0';

				_DasUriDepth* pD = &pScan->pDepth[iDepth];
				bool bTakeIt = false;
				if(!pD->bHaveBest){
					bTakeIt = true;
				} else {
					int cmp = _ver_cmp(sThisWild, pD->sBestWild,
					                   match.uWildVerType);
					if(cmp > 0){
						bTakeIt = true;
					} else if(cmp == 0 && match.uWildRole == DURI_VER){
						/* Same version resolves under both strings —
						 * warn and fall back to lex-last. */
						daslog_warn_v(
							"version collision: '%s' and '%s' both resolve "
							"to the same version; picking lex-last",
							pD->sBestName, sName);
						if(strcmp(sName, pD->sBestName) > 0) bTakeIt = true;
					}
				}
				if(bTakeIt){
					strncpy(pD->sBestName, sName, sizeof(pD->sBestName) - 1);
					pD->sBestName[sizeof(pD->sBestName) - 1] = '\0';
					strncpy(pD->sBestWild, sThisWild, sizeof(pD->sBestWild) - 1);
					pD->sBestWild[sizeof(pD->sBestWild) - 1] = '\0';
					pD->bHaveBest = true;
				}
				continue;  /* keep reading this directory */
			}
			/* No wildcard at file level — yield directly. */
			_join_path(pScan->pDepth[iDepth].sPath, sName,
			           pThis->sCurrent, DURI_MAX_PATH);
			return pThis->sCurrent;
		}

		/* Directory level.  For bHasWild dirs (6d) we accept every matching
		 * name regardless of the wild-token content; for non-wild dirs the
		 * COORD/range check above already filtered. */
		if(iDepth + 1 >= pTplt->nLevels){
			das_error(DASERR_URI,
				"URI iterator depth overflow (internal)");
			pThis->bDone = true;
			return NULL;
		}
		_join_path(pScan->pDepth[iDepth].sPath, sName,
		           pScan->pDepth[iDepth + 1].sPath, DURI_MAX_PATH);
		pScan->nCurDepth = iDepth + 2;
		/* Reset the pushed depth's buffer state for a fresh scan. */
		pScan->pDepth[iDepth + 1].bHaveBest = false;
		if(_open_dir(pScan, iDepth + 1) == NULL){
			pScan->nCurDepth = iDepth + 1;
			continue;
		}
	}

	pThis->bDone = true;
	return NULL;
}

void del_DasUriIter(DasUriIter* pThis)
{
	if(pThis == NULL) return;
	fini_DasUriIter(pThis);
	free(pThis);
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
