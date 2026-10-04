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

#define _das_uri_c_

#include <stdbool.h>
#include <stdint.h>
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <errno.h>
#include <ctype.h>

/* POSIX directory traversal: Windows uses das3/win_dirent.h */
#ifdef _WIN32
#  include <das3/win_dirent.h>
#else
#  include <dirent.h>
#endif

#include <das3/defs.h>
#include <das3/time.h>    /* das_time, dt_parsetime, dt_tnorm, dt_compare  */
#include <das3/datum.h>   /* das_datum, das_datum_toTime, das_datum_toDbl  */
#include <das3/log.h>     /* daslog_warn_v                                  */
#include <das3/uri.h>


/* ========================================================================= */
/* ## Segment roles
 *
 * The scheme prefix (file://, http://) is not a segment.  It is consumed by
 * DasUriTplt_pattern() and kept as DasUriTplt.eProto.
 */

typedef enum das_uri_role_e {
	DURI_LITERAL = 0,  /* plain text                                          */
	DURI_COORD,        /* a named coordinate sub-field                        */
	DURI_WILD,         /* $x, opaque wildcard, lexicographic-last              */
	DURI_VER,          /* $v, version wildcard, numeric-greatest               */
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
 * A DURI_COORD segment holds a copy of its DasUriField, so matching and
 * rendering never look back at the DasUriSegDef tables.
 */

#define DURI_MAX_LIT    128  /* max literal text in one segment              */
#define DURI_MAX_FIELDS  16  /* max COORD sub-segs in one level                */

struct das_uri_seg_t {
	uint8_t      uRole;              /* one of DasUriRole                     */
	bool         bNoPad;             /* pad=none modifier                     */
	uint8_t      uVerType;           /* DasUriVerType (DURI_VER only)         */
	int          nDelta;             /* delta= modifier; parsed, not yet used */
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
 * One path component of the template: a directory name (levels 0 ..
 * nLevels-2) or the file name (level nLevels-1).  Template literals that
 * span a '/' are split so no sub-segment of a level contains one.
 *
 * "/data/$Y/$j/file_$Y$j.cdf" decomposes to:
 *
 *     sBase   = "/data"
 *     level 0 = [ COORD($Y) ]                                         dir
 *     level 1 = [ COORD($j) ]                                         dir
 *     level 2 = [ LIT("file_"), COORD($Y), COORD($j), LIT(".cdf") ]   file
 */

struct das_uri_level_t {
	DasUriSeg* pSegs;    /* owned sub-segment array (deep-copied from pTplt) */
	int        nSegs;
	bool       bIsFile;  /* true for the basename level (last in plan)       */
	bool       bHasWild; /* true if any sub-segment is DURI_WILD / DURI_VER  */
};


/* ========================================================================= */
/* ## Built-in time coordinate definition */

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
	0,        /* nFields: filled by das_time_uridef() on first call          */
	NULL      /* pFields: filled by das_time_uridef() on first call          */
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
 * A directory is read whole when its depth goes live: every entry that
 * matches pTplt->pLevels[D].pSegs and survives the ranges is kept, sorted by
 * name, and then handed out through a cursor.  Reading the whole directory
 * first is what lets a file level pick one $x/$v winner per set of coordinate
 * values, and makes the output order the same on every file system.
 *
 * When nCurDepth == N the live depths are pDepth[0 .. N-1] and the deepest
 * one is being handed out.  nCurDepth == 0 before the first call to
 * DasUriIter_next() and again once every depth is spent; bDone on the
 * iterator tells the two apart.                                              */

/* One surviving directory entry */
typedef struct _das_uri_ent_t {
	char    sName[256];
	int64_t aVals[DURI_MAX_FIELDS];  /* coord fields, in segment order         */
	int     nVals;
	char    sWild[64];               /* text matched by $x/$v, "" if none      */
	uint8_t uWildRole;               /* DURI_WILD, DURI_VER or 0               */
	uint8_t uWildVerType;            /* carried here so qsort needs no context */
} _DasUriEnt;

typedef struct _das_uri_depth_t {
	char sPath[DURI_MAX_PATH];  /* full path of the directory read here       */

	_DasUriEnt* pEnts;          /* surviving entries in name order, owned     */
	int         nEnts;
	int         iNext;          /* next entry to hand out                     */

	/* Coord field values of the entry that opened the next depth, so child
	 * levels can assemble a full time from their ancestors. */
	int64_t aVals[DURI_MAX_FIELDS];
	int     nVals;
} _DasUriDepth;

typedef struct das_uri_scan_t {
	_DasUriDepth* pDepth;    /* pTplt->nLevels entries; runtime walk state   */
	int           nCurDepth; /* count of live depths in pDepth;              */
	                         /* 0 = none, max = pTplt->nLevels               */
} _DasUriScan;


/* ========================================================================= */
/* ## Range initializers */

/* Copy sCoord into pDest (up to nDest-1 chars), lowercasing as we go.
 * Returns DAS_OKAY or DASERR_URI if the source is too long. */
static DasErrCode _set_coord(char* pDest, int nDest, const char* sSrc)
{
	int i = 0;
	for(; sSrc[i] && i < nDest - 1; ++i)
		pDest[i] = (char)tolower((unsigned char)sSrc[i]);
	pDest[i] = '\0';
	if(sSrc[i] != '\0')
		return das_error(DASERR_URI,
			"coordinate name '%s' exceeds %d-char limit", sSrc, nDest - 1);
	return DAS_OKAY;
}

DasErrCode das_range_fromUtc(das_range* pRng, const char* sBeg, const char* sEnd)
{
	memset(pRng, 0, sizeof(das_range));
	strncpy(pRng->sCoord, "time", sizeof(pRng->sCoord) - 1);
	if(!das_datum_fromStr(&pRng->dBeg, sBeg))
		return das_error(DASERR_URI,
			"das_range_fromUtc: cannot parse begin time '%s'", sBeg);
	if(!das_datum_fromStr(&pRng->dEnd, sEnd))
		return das_error(DASERR_URI,
			"das_range_fromUtc: cannot parse end time '%s'", sEnd);
	return DAS_OKAY;
}

DasErrCode das_range_fromTime(
	das_range* pRng, const das_time* tBeg, const das_time* tEnd
){
	/* das_datum has no public fromTime constructor; format as ISO-8601 and
	 * let das_datum_fromStr do the parse.  This path is not in a hot loop. */
	memset(pRng, 0, sizeof(das_range));
	strncpy(pRng->sCoord, "time", sizeof(pRng->sCoord) - 1);

	char sBuf[32];
	snprintf(sBuf, sizeof(sBuf), "%04d-%03dT%02d:%02d:%06.3f",
		tBeg->year, tBeg->yday, tBeg->hour, tBeg->minute, tBeg->second);
	if(!das_datum_fromStr(&pRng->dBeg, sBuf))
		return das_error(DASERR_URI, "das_range_fromTime: invalid begin time");

	snprintf(sBuf, sizeof(sBuf), "%04d-%03dT%02d:%02d:%06.3f",
		tEnd->year, tEnd->yday, tEnd->hour, tEnd->minute, tEnd->second);
	if(!das_datum_fromStr(&pRng->dEnd, sBuf))
		return das_error(DASERR_URI, "das_range_fromTime: invalid end time");

	return DAS_OKAY;
}

DasErrCode das_range_fromInt(
	das_range* pRng, const char* sCoord, int64_t nBeg, int64_t nEnd
){
	memset(pRng, 0, sizeof(das_range));
	DasErrCode nErr = _set_coord(pRng->sCoord, sizeof(pRng->sCoord), sCoord);
	if(nErr != DAS_OKAY) return nErr;
	das_datum_fromDbl(&pRng->dBeg, (double)nBeg, UNIT_DIMENSIONLESS);
	das_datum_fromDbl(&pRng->dEnd, (double)nEnd, UNIT_DIMENSIONLESS);
	return DAS_OKAY;
}

DasErrCode das_range_fromDatum(
	das_range* pRng, const char* sCoord,
	const das_datum* dmBeg, const das_datum* dmEnd
){
	memset(pRng, 0, sizeof(das_range));
	DasErrCode nErr = _set_coord(pRng->sCoord, sizeof(pRng->sCoord), sCoord);
	if(nErr != DAS_OKAY) return nErr;

	/* A das_range outlives the call that fills it, so it can only hold datums
	   that own their bytes.  A referencing datum would leave the range
	   pointing at memory it does not own and cannot keep alive. */
	if(!das_datum_islocal(dmBeg))
		return das_error(DASERR_URI,
			"Range begin is a %s datum, a reference to memory the range would "
			"not own", das_vt_toStr(dmBeg->vt)
		);
	if(!das_datum_islocal(dmEnd))
		return das_error(DASERR_URI,
			"Range end is a %s datum, a reference to memory the range would "
			"not own", das_vt_toStr(dmEnd->vt)
		);

	pRng->dBeg = *dmBeg;
	pRng->dEnd = *dmEnd;
	return DAS_OKAY;
}


/* ========================================================================= */
/* ## Template */

DasUriTplt* new_DasUriTplt(void)
{
	return (DasUriTplt*) calloc(1, sizeof(DasUriTplt));
}

DasErrCode DasUriTplt_register(DasUriTplt* pThis, const DasUriSegDef* pDef)
{
	/* Duplicate coordinate name is an error */
	for(int i = 0; i < pThis->nDefs; ++i){
		if(strcmp(pThis->pDefs[i].sCoord, pDef->sCoord) == 0)
			return das_error(DASERR_URI,
				"coordinate '%s' is already registered", pDef->sCoord);
	}

	/* Duplicate cShort across all registered coords is an error.  Short tokens
	 * are a global namespace: two coords sharing e.g. 'S' would make $S
	 * ambiguous at pattern() time. */
	for(int i = 0; i < pThis->nDefs; ++i){
		for(int j = 0; j < pThis->pDefs[i].nFields; ++j){
			char cExist = pThis->pDefs[i].pFields[j].cShort;
			if(cExist == '\0') continue;
			for(int k = 0; k < pDef->nFields; ++k){
				if(pDef->pFields[k].cShort == cExist)
					return das_error(DASERR_URI,
						"short token '$%c' in coordinate '%s' conflicts with "
						"already-registered coordinate '%s'",
						cExist, pDef->sCoord, pThis->pDefs[i].sCoord);
			}
		}
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

/* Fill pSeg as DURI_COORD for a $() token whose name is sName.
 *
 *   "coord.field"  Qualified form: coord by sCoord, then sub-field by sLong.
 *
 *   "coord"        Scalar shorthand, valid only when the coord has exactly
 *                  one sub-field.
 *
 * Returns false when nothing matches.  There is no search by bare field
 * name: two coordinates may share one.
 */
static bool _lookup_coord(
	DasUriTplt* pThis, const char* sName, DasUriSeg* pSeg
){
	const char* pDot = strchr(sName, '.');

	if(pDot != NULL){
		/* Qualified form: "coord.field" */
		char sCoord[32];
		int nCoord = (int)(pDot - sName);
		if(nCoord < 1 || nCoord >= (int)sizeof(sCoord)) return false;
		memcpy(sCoord, sName, nCoord);
		sCoord[nCoord] = '\0';
		const char* sField = pDot + 1;

		for(int i = 0; i < pThis->nDefs; ++i){
			if(strcmp(pThis->pDefs[i].sCoord, sCoord) != 0) continue;
			for(int j = 0; j < pThis->pDefs[i].nFields; ++j){
				if(strcmp(pThis->pDefs[i].pFields[j].sLong, sField) == 0){
					pSeg->uRole = DURI_COORD;
					strncpy(pSeg->coord.sCoord, sCoord, 31);
					pSeg->coord.sCoord[31] = '\0';
					pSeg->coord.field = pThis->pDefs[i].pFields[j];
					return true;
				}
			}
			return false;  /* coord found, field not */
		}
		return false;  /* coord not found */
	}

	/* Scalar shorthand: coord name only; valid iff coord has exactly one field */
	for(int i = 0; i < pThis->nDefs; ++i){
		if(strcmp(pThis->pDefs[i].sCoord, sName) == 0){
			if(pThis->pDefs[i].nFields == 1){
				pSeg->uRole = DURI_COORD;
				strncpy(pSeg->coord.sCoord, pThis->pDefs[i].sCoord, 31);
				pSeg->coord.sCoord[31] = '\0';
				pSeg->coord.field = pThis->pDefs[i].pFields[0];
				return true;
			}
			return false;  /* multi-field coord: $(time) is ambiguous */
		}
	}
	return false;  /* coord not found */
}

/* Build sBase and pLevels from pThis->pSegs.  On failure del_DasUriTplt
 * frees whatever was allocated. */
static DasErrCode _decompose_levels(DasUriTplt* pThis)
{
	/* Literal templates need no level plan */
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
		/* No '/' in leading text: relative to the CWD */
		pThis->sBase = strdup(".");
	}
	else if(pLast == sPre){
		/* The only '/' is the leading one: root */
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

	/* Step 7: mark bIsFile, bHasWild on each level.  _match_entry records one
	 * wild span per level, so a second $x/$v in a path component is an error. */
	for(int i = 0; i < nLevels; ++i){
		pThis->pLevels[i].bIsFile = (i == nLevels - 1);
		int nWild = 0;
		for(int j = 0; j < pThis->pLevels[i].nSegs; ++j){
			uint8_t r = pThis->pLevels[i].pSegs[j].uRole;
			if(r == DURI_WILD || r == DURI_VER){
				pThis->pLevels[i].bHasWild = true;
				++nWild;
			}
		}
		if(nWild > 1)
			return das_error(DASERR_URI,
				"URI template path component %d has %d wild tokens ($x/$v), "
				"at most one $x or $v per path component is supported",
				i, nWild);
	}

	/* Validation: filename level must have at least one sub-segment. */
	if(pThis->pLevels[nLevels - 1].nSegs == 0)
		return das_error(DASERR_URI,
			"URI template ends with '/', filename component is empty");

	return DAS_OKAY;
}

DasErrCode DasUriTplt_pattern(DasUriTplt* pThis, const char* sTemplate)
{
#ifdef _WIN32
	/* On Windows, rewrite user-supplied '\' to '/' for file:// templates so
	 * the level decomposer sees a single separator.  http/https URLs keep
	 * '\' verbatim: backslashes are not path separators there and may be
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
					return das_error(DASERR_URI,
						"URI template: unrecognised token $(%s), "
						"use $(coord.field) qualified form or $(coord) for "
						"single-field coordinates", sName);
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
						return das_error(DASERR_URI,
							"URI template: unrecognised short token $%c, "
							"register a coordinate with cShort='%c' before "
							"calling DasUriTplt_pattern()", cShort, cShort);
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

	/* Reject adjacent variable-width coord fields: with no literal between
	 * them and no fixed width on either, the match can't tell where one ends
	 * and the next begins.  A $x/$v or a literal (including '/') between them
	 * resets the check. */
	{
		bool bPrevVarCoord  = false;
		bool bLitSince      = false;
		const char* sPrevLong = "";
		for(int i = 0; i < pThis->nSegs; ++i){
			uint8_t r = pThis->pSegs[i].uRole;
			if(r == DURI_LITERAL){
				bLitSince = true;
			} else if(r == DURI_COORD){
				int nW = (int)pThis->pSegs[i].coord.field.nWidth;
				if(bPrevVarCoord && !bLitSince && nW == 0)
					return das_error(DASERR_URI,
						"URI template: adjacent variable-width coord fields "
						"'%s' and '%s' are ambiguous, add a literal delimiter "
						"between them or set nWidth > 0 on at least one",
						sPrevLong, pThis->pSegs[i].coord.field.sLong);
				bPrevVarCoord = (nW == 0);
				bLitSince     = false;
				sPrevLong     = pThis->pSegs[i].coord.field.sLong;
			} else {
				bPrevVarCoord = false;  /* DURI_WILD / DURI_VER resets */
				bLitSince     = false;
			}
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
	if(nLen <= 0) return sBuf;

	char* p   = sBuf;
	char* pEnd = sBuf + nLen - 1; /* one byte reserved for null terminator */

#define _TSTR_APPEND(s) do { \
	int _n = (int)strlen(s); \
	if(p + _n > pEnd) { *pEnd = '\0'; return sBuf; } \
	memcpy(p, (s), _n); p += _n; \
} while(0)

#define _TSTR_CHAR(c) do { \
	if(p >= pEnd) { *pEnd = '\0'; return sBuf; } \
	*p++ = (c); \
} while(0)

	/* Scheme prefix (file:// is implicit; no output for it). */
	if(pThis->eProto == DURI_PROTO_HTTP)  _TSTR_APPEND("http://");
	if(pThis->eProto == DURI_PROTO_HTTPS) _TSTR_APPEND("https://");

	/* Walk segments and reconstruct the pattern. */
	for(int i = 0; i < pThis->nSegs; ++i){
		const DasUriSeg* pSeg = &pThis->pSegs[i];
		char sTok[64];

		switch(pSeg->uRole){

		case DURI_LITERAL:
			_TSTR_APPEND(pSeg->sText);
			break;

		case DURI_COORD:
			if(pSeg->coord.field.cShort != '\0'){
				/* Single-char sugar: $Y, $m, $d ... */
				_TSTR_CHAR('$');
				_TSTR_CHAR(pSeg->coord.field.cShort);
			} else {
				/* Long form: $(coord.field) with optional modifiers. */
				snprintf(sTok, sizeof(sTok), "$(%s.%s",
				         pSeg->coord.sCoord, pSeg->coord.field.sLong);
				if(pSeg->nDelta != 1){
					char sD[24]; snprintf(sD, sizeof(sD), ";delta=%d", pSeg->nDelta);
					strncat(sTok, sD, sizeof(sTok) - strlen(sTok) - 1);
				}
				if(pSeg->bNoPad)
					strncat(sTok, ";pad=none", sizeof(sTok) - strlen(sTok) - 1);
				strncat(sTok, ")", sizeof(sTok) - strlen(sTok) - 1);
				_TSTR_APPEND(sTok);
			}
			break;

		case DURI_WILD:
			_TSTR_APPEND("$x");
			break;

		case DURI_VER:
			/* $v renders as $(v;type=X) when the type is not the default (sep). */
			if(pSeg->uVerType == DURI_VER_SEP){
				_TSTR_APPEND("$v");
			} else {
				const char* sType =
					(pSeg->uVerType == DURI_VER_INT)   ? "int"   :
					(pSeg->uVerType == DURI_VER_ALPHA)  ? "alpha" : "sep";
				snprintf(sTok, sizeof(sTok), "$(v;type=%s)", sType);
				_TSTR_APPEND(sTok);
			}
			break;
		}
	}

#undef _TSTR_APPEND
#undef _TSTR_CHAR

	*p = '\0';
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
			double rBeg;
			if(!das_datum_toDbl(&pRanges[i].dBeg, &rBeg)) return -1;
			*pVal = (int)rBeg;
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

	/* The parser strips the scheme.  A local path stays bare, a URL is no
	 * use without it. */
	const char* sScheme = "";
	if(pThis->eProto == DURI_PROTO_HTTP)  sScheme = "http://";
	if(pThis->eProto == DURI_PROTO_HTTPS) sScheme = "https://";
	int nScheme = (int)strlen(sScheme);
	if(pOut + nScheme > pEnd) goto overflow;
	memcpy(pOut, sScheme, nScheme);
	pOut += nScheme;

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
				/* not constrained: render as wildcard */
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

	pThis->pState = pScan;
	return DAS_OKAY;
}

void fini_DasUriIter(DasUriIter* pThis)
{
	if(pThis == NULL || pThis->pState == NULL) return;

	_DasUriScan* pScan = (_DasUriScan*)pThis->pState;

	/* Directory listings still held from a partial iteration */
	for(int i = 0; i < pScan->nCurDepth; ++i)
		free(pScan->pDepth[i].pEnts);

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
/* ## Iterator helpers */

/* Join sDir + '/' + sLeaf into sOut, avoiding a duplicate separator when
 * sDir already ends with one (e.g. sDir == "/" at POSIX root). */
static void _join_path(const char* sDir, const char* sLeaf, char* sOut, int nOut)
{
	int nDir = (int)strlen(sDir);
	bool bSep = (nDir > 0 && sDir[nDir - 1] != '/');
	snprintf(sOut, nOut, "%s%s%s", sDir, bSep ? "/" : "", sLeaf);
}

/* Output of _match_entry: coord values in segment order and, for a level
 * with a $x or $v, the span of the name that token matched. */
typedef struct {
	int64_t     aVals[DURI_MAX_FIELDS];
	int         nVals;
	const char* pWildStart;    /* pointer into sName; NULL if level has no wild */
	int         nWildLen;      /* -1 if no wild span captured                    */
	uint8_t     uWildRole;     /* DURI_WILD or DURI_VER (0 if none)              */
	uint8_t     uWildVerType;  /* copied from $v seg for later comparison        */
} _MatchOut;

/* Match sName against pLvl->pSegs, filling pOut.  Returns false on non-match.
 *
 * A $x/$v span ends at the first occurrence of the literal that follows it,
 * or at the end of the name when the token is the last segment.  A wild
 * followed by a coord field can't be delimited and never matches. */
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



/* Integer window for a dotted sub-field range.  A simple [nLo1, nHi1] range
 * covers most cases; bTwo indicates a rollover where the valid set is
 * [nLo1, nHi1] U [nLo2, nHi2] (e.g. spacecraft-clock mod64k crossing the
 * partition boundary at 65535/0).  Rollover is only recognised for dotted
 * sub-field ranges where dBeg > dEnd, using the segment's intrinsic
 * nMin/nMax as the wrap bounds.
 *
 * Whole-coordinate time ranges never come through here; they are handled
 * by the interval test in _in_ranges. */
typedef struct {
	int64_t  nLo1, nHi1;
	bool     bTwo;
	int64_t  nLo2, nHi2;
} _Bounds;

/* Look for a dotted sub-field range that constrains pSeg.  Returns true
 * with pB populated; false if the segment is unconstrained. */
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
		/* Dotted sub-field: integer range, half-open [dBeg, dEnd).  When
		 * dBeg > dEnd we interpret as a rollover crossing and split into two
		 * intervals using the segment's intrinsic bounds. */
		if(strcmp(pRanges[i].sCoord, sDot) == 0){
			double rBeg, rEnd;
			if(!das_datum_toDbl(&pRanges[i].dBeg, &rBeg)) return false;
			if(!das_datum_toDbl(&pRanges[i].dEnd, &rEnd)) return false;
			int64_t nBeg = (int64_t)rBeg;
			int64_t nEnd = (int64_t)rEnd;
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

/* True if any range in pRanges covers sCoord with vtTime datums. */
static bool _has_vttime_range(
	const char* sCoord, int nRanges, const das_range* pRanges
){
	for(int i = 0; i < nRanges; ++i)
		if(strcmp(pRanges[i].sCoord, sCoord) == 0 && pRanges[i].dBeg.vt == vtTime)
			return true;
	return false;
}

/* Time field ranks, coarse to fine.  mday and yday share a rank: a level
   names one or the other, never both. */
#define _TF_NONE   -1
#define _TF_YEAR    0
#define _TF_MONTH   1
#define _TF_DAY     2
#define _TF_HOUR    3
#define _TF_MINUTE  4
#define _TF_SECOND  5

static int _time_field_rank(const char* sLong)
{
	if(strcmp(sLong, "year")   == 0) return _TF_YEAR;
	if(strcmp(sLong, "month")  == 0) return _TF_MONTH;
	if(strcmp(sLong, "mday")   == 0) return _TF_DAY;
	if(strcmp(sLong, "yday")   == 0) return _TF_DAY;
	if(strcmp(sLong, "hour")   == 0) return _TF_HOUR;
	if(strcmp(sLong, "minute") == 0) return _TF_MINUTE;
	if(strcmp(sLong, "second") == 0) return _TF_SECOND;
	return _TF_NONE;
}

/* Collect the "time" coord fields of the ancestor depths and of the current
 * entry into *pDt and normalise.  *pFinest gets the rank of the finest field
 * seen, _TF_NONE if there was none.
 *
 * dt_tnorm treats yday as output only, so a $j value goes in as mday with
 * month = 1 and dt_tnorm derives the calendar date. */
static void _assemble_time(
	const DasUriTplt* pTplt, const _DasUriScan* pScan, int iDepth,
	const DasUriLevel* pLvl, const int64_t* pFieldVals,
	das_time* pDt, int* pFinest
){
	memset(pDt, 0, sizeof(das_time));
	*pFinest = _TF_NONE;
	/* Smallest valid calendar date, so a template that omits coarser fields
	 * (a bare $j with no $Y) doesn't hand dt_tnorm a zero date. */
	pDt->year  = 1;
	pDt->month = 1;
	pDt->mday  = 1;

	bool    bHaveYday = false;
	int64_t nYday     = 0;

	for(int d = 0; d <= iDepth; ++d){
		const DasUriLevel* pL;
		const int64_t*     pVals;
		if(d < iDepth){
			pL    = &pTplt->pLevels[d];
			pVals = pScan->pDepth[d].aVals;
		} else {
			pL    = pLvl;
			pVals = pFieldVals;
		}

		int iVal = 0;
		for(int i = 0; i < pL->nSegs; ++i){
			const DasUriSeg* pSeg = &pL->pSegs[i];
			if(pSeg->uRole != DURI_COORD) continue;
			/* iVal indexes pVals across *all* coord segs, not just time ones */
			if(strcmp(pSeg->coord.sCoord, "time") != 0){ ++iVal; continue; }

			const char* sLong = pSeg->coord.field.sLong;
			int64_t     nV    = pVals[iVal];
			int         nRank = _time_field_rank(sLong);
			if(nRank > *pFinest) *pFinest = nRank;

			if     (strcmp(sLong, "year")   == 0) pDt->year   = (int)nV;
			else if(strcmp(sLong, "month")  == 0) pDt->month  = (int)nV;
			else if(strcmp(sLong, "mday")   == 0) pDt->mday   = (int)nV;
			else if(strcmp(sLong, "yday")   == 0){ bHaveYday = true; nYday = nV; }
			else if(strcmp(sLong, "hour")   == 0) pDt->hour   = (int)nV;
			else if(strcmp(sLong, "minute") == 0) pDt->minute = (int)nV;
			else if(strcmp(sLong, "second") == 0) pDt->second = (double)nV;
			++iVal;
		}
	}

	if(bHaveYday){
		pDt->month = 1;
		pDt->mday  = (int)nYday;
	}
	dt_tnorm(pDt);
}

/* The time interval an entry covers: from the time assembled out of every
 * field known at this depth to one unit of the finest such field later.  A
 * year directory covers a year, a $Y/$m directory a month, a file named to
 * the day covers that day.  Returns false when no time field is known yet
 * (a literal or $x level above the first time field), which means
 * unconstrained. */
static bool _time_interval(
	const DasUriTplt* pTplt, const _DasUriScan* pScan, int iDepth,
	const DasUriLevel* pLvl, const int64_t* pFieldVals,
	das_time* pStart, das_time* pStop
){
	int nFinest = _TF_NONE;
	_assemble_time(pTplt, pScan, iDepth, pLvl, pFieldVals, pStart, &nFinest);
	if(nFinest == _TF_NONE)
		return false;

	*pStop = *pStart;
	switch(nFinest){
	case _TF_YEAR:   pStop->year   += 1;   break;
	case _TF_MONTH:  pStop->month  += 1;   break;
	case _TF_DAY:    pStop->mday   += 1;   break;   /* yday already folded in */
	case _TF_HOUR:   pStop->hour   += 1;   break;
	case _TF_MINUTE: pStop->minute += 1;   break;
	default:         pStop->second += 1.0; break;
	}
	dt_tnorm(pStop);
	return true;
}

/* Apply user ranges to the field values just extracted from one level's entry.
 * Returns true if the entry should be kept, false if it should be filtered out.
 * No logging here: filtered entries are a legitimate, expected outcome.
 *
 * Whole-coordinate time ranges use one test at every level: the entry is
 * kept when the interval it covers (see _time_interval) overlaps the
 * half-open query [begin, end).  Per-field windows cannot do this job: a
 * query from July 31 to August 5 gives the day field the window 31..5,
 * which is empty, and a query across a year end does the same to the
 * month.  The interval test needs no round-up rules either: an end of
 * 2025-03-01 is the first instant of March, so the March directory
 * [Mar 1, Apr 1) does not overlap a query that ends there, while an end of
 * 2025-03-01T12:00 does.  Calendar carries are dt_tnorm's job.
 *
 * Dotted sub-field ranges (sclk.mod64k and the like) are integer windows on
 * one field and use _seg_range. */
static bool _in_ranges(
	const DasUriTplt* pTplt, const _DasUriScan* pScan, int iDepth,
	const DasUriLevel* pLvl, const int64_t* pFieldVals,
	int nRanges, const das_range* pRanges
){
	if(nRanges == 0) return true;

	for(int i = 0; i < nRanges; ++i){
		if(pRanges[i].dBeg.vt != vtTime) continue;
		if(strcmp(pRanges[i].sCoord, "time") != 0) continue;

		das_time dtStart, dtStop;
		if(!_time_interval(pTplt, pScan, iDepth, pLvl, pFieldVals, &dtStart, &dtStop))
			continue;   /* no time field known at this depth */

		das_time dtBeg, dtEnd;
		das_datum_toTime(&pRanges[i].dBeg, &dtBeg);
		das_datum_toTime(&pRanges[i].dEnd, &dtEnd);

		/* [start, stop) overlaps [beg, end) */
		if(!((dt_compare(&dtStart, &dtEnd) < 0) && (dt_compare(&dtStop, &dtBeg) > 0)))
			return false;
	}

	/* Per-field integer windows for dotted ranges.  Time fields under a
	   whole-coordinate range were handled above. */
	int iVal = 0;
	for(int i = 0; i < pLvl->nSegs; ++i){
		const DasUriSeg* pSeg = &pLvl->pSegs[i];
		if(pSeg->uRole != DURI_COORD) continue;

		if(_has_vttime_range(pSeg->coord.sCoord, nRanges, pRanges)){
			++iVal; continue;
		}

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

static int _ent_cmp_name(const void* vpA, const void* vpB)
{
	return strcmp(((const _DasUriEnt*)vpA)->sName, ((const _DasUriEnt*)vpB)->sName);
}

static int _ent_cmp_vals(const _DasUriEnt* pA, const _DasUriEnt* pB)
{
	for(int i = 0; i < pA->nVals && i < pB->nVals; ++i){
		if(pA->aVals[i] < pB->aVals[i]) return -1;
		if(pA->aVals[i] > pB->aVals[i]) return  1;
	}
	return 0;
}

/* Order that puts rivals side by side, best last: coordinate values, then
 * the wild token under its own compare rule, then the name so that a version
 * tie resolves to the lexicographically last file. */
static int _ent_cmp_rival(const void* vpA, const void* vpB)
{
	const _DasUriEnt* pA = (const _DasUriEnt*)vpA;
	const _DasUriEnt* pB = (const _DasUriEnt*)vpB;
	int nCmp = _ent_cmp_vals(pA, pB);
	if(nCmp != 0) return nCmp;
	nCmp = _ver_cmp(pA->sWild, pB->sWild, pA->uWildVerType);
	if(nCmp != 0) return nCmp;
	return strcmp(pA->sName, pB->sName);
}

/* Cut a file level's entries down to one per set of coordinate values.
 * Files compete only when every coordinate field agrees, so each day (orbit,
 * clock partition, ...) in a directory keeps its own best $x/$v match. */
static void _keep_winners(_DasUriDepth* pD)
{
	if(pD->nEnts < 2) return;
	qsort(pD->pEnts, pD->nEnts, sizeof(_DasUriEnt), _ent_cmp_rival);

	int nKeep = 0;
	for(int i = 0; i < pD->nEnts; ++i){
		const _DasUriEnt* pEnt = pD->pEnts + i;
		bool bLast = (i + 1 == pD->nEnts)
			|| (_ent_cmp_vals(pEnt, pEnt + 1) != 0);
		if(!bLast) continue;

		if((i > 0) && (pEnt->uWildRole == DURI_VER)
		   && (_ent_cmp_vals(pEnt - 1, pEnt) == 0)
		   && (_ver_cmp(pEnt[-1].sWild, pEnt->sWild, pEnt->uWildVerType) == 0)
		)
			daslog_warn_v(
				"version collision: '%s' and '%s' both resolve to the same "
				"version; picking lex-last", pEnt[-1].sName, pEnt->sName
			);

		if(nKeep != i) pD->pEnts[nKeep] = *pEnt;
		++nKeep;
	}
	pD->nEnts = nKeep;
}

/* Read the directory at pScan->pDepth[iDepth].sPath, keeping the entries that
 * match level iDepth and fall inside the ranges, in name order.  Ancestor
 * depths must already hold the aVals of the entries that led here.
 *
 * A directory that can't be opened is an empty one, not an error.  Returns
 * false only when out of memory. */
static bool _load_dir(DasUriIter* pThis, int iDepth)
{
	const DasUriTplt*  pTplt = pThis->pTplt;
	_DasUriScan*       pScan = (_DasUriScan*)pThis->pState;
	_DasUriDepth*      pD    = &pScan->pDepth[iDepth];
	const DasUriLevel* pLvl  = &pTplt->pLevels[iDepth];

	pD->pEnts = NULL;
	pD->nEnts = 0;
	pD->iNext = 0;

	DIR* pDir = opendir(pD->sPath);
	if(pDir == NULL){
		daslog_debug_v("opendir('%s') failed: %s", pD->sPath, strerror(errno));
		return true;
	}

	int nCap = 0;
	_MatchOut match;
	struct dirent* pDirEnt;
	while((pDirEnt = readdir(pDir)) != NULL){
		const char* sName = pDirEnt->d_name;
		if(sName[0] == '.' && (sName[1] == '\0' ||
		                      (sName[1] == '.' && sName[2] == '\0')))
			continue;

		if(!_match_entry(pLvl, sName, &match))
			continue;
		if(!_in_ranges(pTplt, pScan, iDepth, pLvl, match.aVals,
		               pThis->nRanges, pThis->pRanges))
			continue;

		if(pD->nEnts == nCap){
			nCap = (nCap == 0) ? 64 : nCap * 2;
			_DasUriEnt* pNew = (_DasUriEnt*)realloc(
				pD->pEnts, nCap * sizeof(_DasUriEnt)
			);
			if(pNew == NULL){
				closedir(pDir);
				free(pD->pEnts);
				pD->pEnts = NULL;
				pD->nEnts = 0;
				das_error(DASERR_URI, "out of memory reading '%s'", pD->sPath);
				return false;
			}
			pD->pEnts = pNew;
		}

		_DasUriEnt* pEnt = pD->pEnts + pD->nEnts;
		memset(pEnt, 0, sizeof(_DasUriEnt));
		strncpy(pEnt->sName, sName, sizeof(pEnt->sName) - 1);
		memcpy(pEnt->aVals, match.aVals, match.nVals * sizeof(int64_t));
		pEnt->nVals = match.nVals;
		if(match.pWildStart != NULL){
			int nCopy = match.nWildLen < (int)sizeof(pEnt->sWild) - 1
			            ? match.nWildLen : (int)sizeof(pEnt->sWild) - 1;
			memcpy(pEnt->sWild, match.pWildStart, nCopy);
			pEnt->uWildRole    = match.uWildRole;
			pEnt->uWildVerType = match.uWildVerType;
		}
		++pD->nEnts;
	}
	closedir(pDir);

	/* A wild token in a directory name selects nothing: every matching
	 * directory is descended. */
	if(pLvl->bIsFile && pLvl->bHasWild)
		_keep_winners(pD);

	/* Byte order, not strcoll: the walk order must not change with the
	 * caller's locale.  Zero padded fields make this coordinate order. */
	if(pD->nEnts > 1)
		qsort(pD->pEnts, pD->nEnts, sizeof(_DasUriEnt), _ent_cmp_name);

	return true;
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
	if(pScan == NULL){
		pThis->bDone = true;
		return NULL;
	}

	/* First call: seed depth 0 with sBase and read it.  bDone was checked
	 * above, so no live depths here means not yet started. */
	if(pScan->nCurDepth == 0){
		snprintf(pScan->pDepth[0].sPath, DURI_MAX_PATH, "%s", pTplt->sBase);
		if(!_load_dir(pThis, 0)){
			pThis->bDone = true;
			return NULL;
		}
		pScan->nCurDepth = 1;
	}

	/* Walk the depth stack until we yield a file or run out. */
	while(pScan->nCurDepth > 0){
		int iDepth = pScan->nCurDepth - 1;
		_DasUriDepth* pD = &pScan->pDepth[iDepth];
		const DasUriLevel* pLvl = &pTplt->pLevels[iDepth];

		if(pD->iNext >= pD->nEnts){
			free(pD->pEnts);
			pD->pEnts = NULL;
			pD->nEnts = 0;
			--pScan->nCurDepth;
			continue;
		}

		const _DasUriEnt* pEnt = pD->pEnts + pD->iNext;
		++pD->iNext;

		if(pLvl->bIsFile){
			_join_path(pD->sPath, pEnt->sName, pThis->sCurrent, DURI_MAX_PATH);
			return pThis->sCurrent;
		}

		if(iDepth + 1 >= pTplt->nLevels){
			das_error(DASERR_URI,
				"URI iterator depth overflow (internal)");
			pThis->bDone = true;
			return NULL;
		}

		memcpy(pD->aVals, pEnt->aVals, pEnt->nVals * sizeof(int64_t));
		pD->nVals = pEnt->nVals;
		_join_path(pD->sPath, pEnt->sName,
		           pScan->pDepth[iDepth + 1].sPath, DURI_MAX_PATH);
		if(!_load_dir(pThis, iDepth + 1)){
			pThis->bDone = true;
			return NULL;
		}
		pScan->nCurDepth = iDepth + 2;
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

/* ========================================================================= */
/* ## das_uri_list
 *
 * The result is one block, so one free() releases it:
 *
 *   [ char* ptr[0] | ... | char* ptr[N-1] | NULL | "path0\0" | ... ]
 *
 * Paths are gathered as strdup'd temporaries first, then packed, so the
 * file system is walked only once.
 */

char** das_uri_list(
	const char* sTemplate, const DasUriSegDef* pDef,
	int nRanges, const das_range* pRanges,
	size_t* pCount
){
	if(pCount) *pCount = 0;

	/* --- Phase 1: collect paths --- */

	DasUriTplt* pTplt = new_DasUriTplt();
	if(pTplt == NULL) return NULL;

	if(pDef != NULL && DasUriTplt_register(pTplt, pDef) != DAS_OKAY){
		del_DasUriTplt(pTplt); return NULL;
	}
	if(DasUriTplt_pattern(pTplt, sTemplate) != DAS_OKAY){
		del_DasUriTplt(pTplt); return NULL;
	}

	DasUriIter* pIter = new_DasUriIter(pTplt, nRanges, pRanges);
	if(pIter == NULL){ del_DasUriTplt(pTplt); return NULL; }

	/* Temporary list of strdup'd paths; grown with realloc. */
	int      nCap   = 64;
	int      nFound = 0;
	size_t   nBytes = 0;            /* total string bytes including null terms */
	char**   ppTmp  = (char**)malloc(nCap * sizeof(char*));
	if(ppTmp == NULL) goto cleanup;

	const char* sPath;
	while((sPath = DasUriIter_next(pIter)) != NULL){
		if(nFound == nCap){
			nCap *= 2;
			char** ppNew = (char**)realloc(ppTmp, nCap * sizeof(char*));
			if(ppNew == NULL) goto cleanup;
			ppTmp = ppNew;
		}
		ppTmp[nFound] = strdup(sPath);
		if(ppTmp[nFound] == NULL) goto cleanup;
		nBytes += strlen(sPath) + 1;
		++nFound;
	}

	/* --- Phase 2: pack into one block --- */

	if(nFound == 0) goto cleanup;  /* return NULL for empty result */

	/* Block: (nFound+1) pointer slots + all string bytes */
	size_t  nPtrBytes = (nFound + 1) * sizeof(char*);
	char**  ppOut     = (char**)malloc(nPtrBytes + nBytes);
	if(ppOut == NULL) goto cleanup;

	char* pStr = (char*)ppOut + nPtrBytes;   /* string region starts here */
	for(int i = 0; i < nFound; ++i){
		size_t nLen = strlen(ppTmp[i]) + 1;
		memcpy(pStr, ppTmp[i], nLen);
		ppOut[i] = pStr;
		pStr += nLen;
	}
	ppOut[nFound] = NULL;                    /* NULL terminator */

	/* Free temporaries */
	for(int i = 0; i < nFound; ++i) free(ppTmp[i]);
	free(ppTmp);
	del_DasUriIter(pIter);
	del_DasUriTplt(pTplt);

	if(pCount) *pCount = (size_t)nFound;
	return ppOut;

cleanup:
	if(ppTmp){
		for(int i = 0; i < nFound; ++i) if(ppTmp[i]) free(ppTmp[i]);
		free(ppTmp);
	}
	del_DasUriIter(pIter);
	del_DasUriTplt(pTplt);
	return NULL;
}

