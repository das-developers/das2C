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

/** @file uri.h
 * Finding files whose names encode coordinate values, via URI templates.
 *
 * Archives commonly encode coordinate values (a date, an orbit number, a
 * spacecraft clock count) in directory and file names.  A URI template says
 * where those values sit in a path so that, given a coordinate range, the
 * matching files can be found by listing directories.  Paths are never
 * guessed: every directory that could hold a match is read.
 *
 * @par Field tokens
 *
 *   $X                       short token, one character
 *   $(coord.field)           qualified long token
 *   $(coord.field;mod=val)   long token with modifiers
 *   $(coord)                 shorthand, only for a coordinate with one field
 *
 * Any DasUriField with a non-zero cShort gets a short token.  A Voyager
 * spacecraft clock registered with fields 'P' (partition), 'M' (mod64k) and
 * 'S' (mod60) could be used as:
 *
 *   P$P/V1P$P_$x/C$M$S.DAT
 *
 * The long form names the coordinate (DasUriSegDef.sCoord) and the field
 * (DasUriField.sLong), the same dotted name that a das_range uses:
 *
 *   template:   /data/$(time.year)/$(time.yday)/file_$(time.year)$(time.yday).dat
 *   range key:  "time.year",  "time.yday"
 *
 * A token that names no registered coordinate or field is an error, never a
 * silent wildcard.  Short tokens take no modifiers, so the Autoplot form
 * $(Y;pad=none) is not accepted.
 *
 * Two variable-width fields may not sit side by side; put a literal between
 * them or give one a fixed width.
 *
 * @par Coordinate definitions
 *
 * The engine knows nothing about time.  Register any DasUriSegDef with
 * DasUriTplt_register() before calling DasUriTplt_pattern().  The library
 * ships one, das_time_uridef(), which supplies $Y $m $d $j $H $M $S.  A
 * coordinate such as an orbit counter is one more definition; no mapping
 * from orbit to time is assumed.
 *
 * Field values are non-negative integers written in decimal digits.
 *
 * @par Wildcard and version tokens
 *
 *   $x   Opaque wildcard.  Of the files that match, the lexicographically
 *        last is used.
 *
 *   $v   Version.  Of the files that match, the greatest version is used,
 *        compared as the type= modifier says.
 *
 * Neither is a coordinate.  Files compete only when all their coordinate
 * values agree, so a directory holding many days yields one file per day.
 * In a directory name the token selects nothing: every matching directory
 * is searched.
 *
 * A path component may hold one $x or $v, and it must be followed by literal
 * text or end the component.
 *
 * @par Modifiers
 *
 *   pad=none    The field is not zero padded and has variable width.
 *
 *   delta=N     Accepted and preserved, but not yet used.  A file is taken to
 *               cover one unit of the finest field in its path.
 *
 *   type=sep    (default for $v) Split on '.' and compare each part as an
 *               integer, so 1.10.0 is later than 1.9.0.
 *
 *   type=int    Compare as a single integer; for counters such as v01, v02.
 *
 *   type=alpha  Compare as text, the same as $x.
 *
 * Under type=sep and type=int two names can resolve to the same version,
 * "v1" and "v01" for example.  The lexicographically last is used and a
 * warning is logged.
 *
 * @par Protocols
 *
 * A template with no scheme, or with file://, names local files, and the
 * paths returned carry no scheme.  http:// and https:// are parsed but can
 * not be iterated yet: init_DasUriIter() fails for them.
 *
 * @par Relationship to Autoplot URI templates
 *
 * Autoplot's field codes are used wherever possible, so common templates
 * read the same in both.  The differences:
 *
 *   - Paths are always found by listing directories, never generated.
 *   - Coordinates are not limited to time.
 *   - $v compares numerically by default.
 */

#ifndef _das_uri_h_
#define _das_uri_h_

#include <stdbool.h>
#include <stdint.h>

#include <das3/defs.h>
#include <das3/time.h>
#include <das3/datum.h>

#ifdef __cplusplus
extern "C" {
#endif


/* ************************************************************************* */
/* Limits */

/** Maximum length of a rendered URI / file path */
#define DURI_MAX_PATH  2048


/* ************************************************************************* */
/* Coordinate segment field and type definitions */

/** One sub-field within a named coordinate type.
 *
 * The "time" coordinate has seven of these, an orbit counter has one.
 * sLong must be lower case to be reachable from a das_range.
 *
 * @see DasUriSegDef
 */
typedef struct das_uri_field_t {
	char cShort;      /* single-char short token, e.g. 'Y'; '\0' if none     */
	char sLong[32];   /* long name, the "year" in $(time.year)               */
	int  nWidth;      /* rendered field width (zero-padded); 0 = variable    */
	int  nMin;        /* smallest valid value; names outside nMin..nMax are  */
	int  nMax;        /* skipped with a warning                               */
} DasUriField;


/** Definition of one coordinate type for use by the URI template parser.
 *
 * DasUriTplt_register() copies the definition and its field array, so both
 * may be on the stack and go out of scope afterwards.  sCoord must be lower
 * case to be reachable from a das_range.
 *
 * @code
 *   DasUriField aSclkFlds[] = {
 *       { 'P', "partition", 1,  0,     9     },
 *       { 'M', "mod64k",    5,  0,     65535 },
 *       { 'S', "mod60",     2,  0,     59    },
 *       { 'L', "line",      3,  1,     800   },
 *   };
 *   DasUriSegDef sclkDef = { "sclk", 4, aSclkFlds };
 *   DasUriTplt_register(pTplt, &sclkDef);
 * @endcode
 */
typedef struct das_uri_seg_def_t {
	char         sCoord[32]; /* coordinate name, e.g. "time", "sclk", "orbit" */
	int          nFields;    /* number of entries in pFields                   */
	DasUriField* pFields;    /* sub-field table; caller owns until register()  */
} DasUriSegDef;


/* ************************************************************************* */
/* Coordinate query range */

/** A constraint on one coordinate (or sub-field) used to select matching files.
 *
 * Template fields that no range names are unconstrained.  Ranges are
 * half-open: [dBeg, dEnd).
 *
 *   "time"          The whole time coordinate; dBeg and dEnd are vtTime.
 *                   An entry is kept when the interval it covers overlaps the
 *                   range.  That interval runs from the time assembled out
 *                   of every time field in the path so far to one unit of
 *                   the finest such field later: a $Y directory covers a
 *                   year, a file named to the day covers that day.
 *
 *   "sclk.mod64k"   One sub-field, as "coord.field"; dBeg and dEnd are
 *   "time.year"     numbers, truncated to integers, tested against that
 *                   field alone.  dBeg > dEnd is a rollover: the range runs
 *                   from dBeg up to the field's nMax, then from nMin up to
 *                   dEnd.
 *
 * A file's name is taken as the start of its coverage.  No search is made
 * for a file that starts before dBeg and runs into the range.
 */
typedef struct das_range_t {
	char      sCoord[32]; /* coordinate or sub-field: "time", "sclk.mod64k"  */
	das_datum dBeg;       /* inclusive range begin                             */
	das_datum dEnd;       /* exclusive range end                               */
} das_range;


/* ************************************************************************* */
/* Range initializers */

/** Initialize a das_range for the "time" coordinate from ISO-8601 UTC strings.
 *
 * Any format das_datum_fromStr() takes works here: "2025-10-01", "2025-288",
 * "2025-10-15T06:00".
 *
 * @param pRng  Storage to initialise; all fields are overwritten.
 * @param sBeg  Range begin (inclusive).
 * @param sEnd  Range end (exclusive).
 * @return DAS_OKAY on success, a positive error code on parse failure.
 */
DAS_API DasErrCode das_range_fromUtc(
	das_range* pRng, const char* sBeg, const char* sEnd
);


/** Initialize a das_range for the "time" coordinate from das_time structs.
 *
 * @param pRng  Storage to initialise; all fields are overwritten.
 * @param tBeg  Inclusive range begin; copied.
 * @param tEnd  Exclusive range end; copied.
 * @return DAS_OKAY on success, a positive error code otherwise.
 */
DAS_API DasErrCode das_range_fromTime(
	das_range* pRng, const das_time* tBeg, const das_time* tEnd
);


/** Initialize a das_range for a named integer sub-field.
 *
 * @param pRng    Storage to initialise; all fields are overwritten.
 * @param sCoord  Sub-field name, e.g. "sclk.mod64k".  Stored lower-cased;
 *                must fit in 31 chars.
 * @param nBeg    Inclusive range begin.
 * @param nEnd    Exclusive range end.  nBeg > nEnd is a rollover, see
 *                das_range.
 * @return DAS_OKAY on success, a positive error code if sCoord is too long.
 */
DAS_API DasErrCode das_range_fromInt(
	das_range* pRng, const char* sCoord, int64_t nBeg, int64_t nEnd
);


/** Initialize a das_range from pre-built das_datum values.
 *
 * The datums are copied, so they must own their bytes: a datum that refers
 * to outside memory is refused.  Use vtTime for "time" and numeric datums
 * for sub-fields.
 *
 * @param pRng    Storage to initialise; all fields are overwritten.
 * @param sCoord  Coordinate or sub-field name.  Stored lower-cased.
 * @param dmBeg   Inclusive begin datum.
 * @param dmEnd   Exclusive end datum.
 * @return DAS_OKAY on success, a positive error code if sCoord is too long
 *         or a datum does not own its bytes.
 */
DAS_API DasErrCode das_range_fromDatum(
	das_range* pRng, const char* sCoord,
	const das_datum* dmBeg, const das_datum* dmEnd
);


/* ************************************************************************* */
/* Protocol enumeration */

/** Transport protocol, from the leading scheme of a URI template. */
typedef enum das_uri_proto_e {
	DURI_PROTO_FILE  = 0, /* no prefix, or explicit file://                    */
	DURI_PROTO_HTTP,      /* http://                                            */
	DURI_PROTO_HTTPS,     /* https://                                           */
} DasUriProto;


/* Opaque, defined in uri.c.  A level is one path component: a directory name
 * or the file name. */
typedef struct das_uri_seg_t DasUriSeg;
typedef struct das_uri_level_t DasUriLevel;


/* ************************************************************************* */
/* A parsed URI template */

typedef struct das_uri_tplt_t {
	bool         bHasWild;  /* true if template contains $x or $v             */
	bool         bLiteral;  /* true if template contains no coordinate fields; */
	                        /* ranges are ignored and one path is yielded      */
	DasUriProto  eProto;    /* protocol derived from leading scheme, or        */
	                        /* DURI_PROTO_FILE if no prefix is present         */
	int          nSegs;     /* number of entries in pSegs                      */
	DasUriSeg*   pSegs;     /* heap-allocated segment array; freed by del_     */
	int          nDefs;     /* number of registered coordinate definitions     */
	DasUriSegDef* pDefs;    /* deep-copied def array; freed by del_            */
	char*        sBase;     /* fixed directory above the first variable        */
	                        /* component, no trailing separator; "." when the  */
	                        /* template is relative, "/" when only the root    */
	                        /* is fixed                                        */
	int          nLevels;   /* number of directory + filename levels           */
	DasUriLevel* pLevels;   /* level plan built in DasUriTplt_pattern();       */
	                        /* freed by del_DasUriTplt()                       */
} DasUriTplt;


/* ************************************************************************* */
/* A streaming iterator over a URI template and coordinate ranges */

typedef struct das_uri_iter_t {
	const DasUriTplt* pTplt;
	int               nRanges;
	const das_range*  pRanges;        /* caller owns; must outlive iterator   */
	bool              bDone;
	char              sCurrent[DURI_MAX_PATH]; /* path returned by _next()    */
	void*             pState;         /* scan state, owned by the iterator    */
} DasUriIter;


/* ************************************************************************* */
/* API */

/** Return the built-in coordinate definition for the time coordinate.
 *
 *   short  long name   width  range        qualified token
 *   -----  ---------   -----  -----------  ---------------
 *   $Y     year          4    1678 - 2262  $(time.year)
 *   $m     month         2    01 - 12      $(time.month)
 *   $d     mday          2    01 - 31      $(time.mday)
 *   $j     yday          3    001 - 366    $(time.yday)
 *   $H     hour          2    00 - 23      $(time.hour)
 *   $M     minute        2    00 - 59      $(time.minute)
 *   $S     second        2    00 - 60      $(time.second)
 *
 * The result is static: nothing to free, always valid.
 */
DAS_API const DasUriSegDef* das_time_uridef(void);


/** Allocate an empty URI template object.
 *
 * Register coordinate definitions with DasUriTplt_register(), then call
 * DasUriTplt_pattern().  With none registered only literals, $x and $v are
 * recognised.
 *
 * @return A heap-allocated DasUriTplt, or NULL on allocation failure.
 *
 * @memberof DasUriTplt
 */
DAS_API DasUriTplt* new_DasUriTplt(void);


/** Register a coordinate type definition with a template.
 *
 * Must be called before DasUriTplt_pattern().  pDef and its field array are
 * copied.  It is an error to register a coordinate name twice, or a short
 * token that an earlier definition already uses: short tokens are shared by
 * all coordinates of a template.
 *
 * @param pThis  The template to extend.
 * @param pDef   Coordinate definition to copy in; caller retains ownership.
 * @return DAS_OKAY on success, a positive error code otherwise.
 *
 * @memberof DasUriTplt
 */
DAS_API DasErrCode DasUriTplt_register(DasUriTplt* pThis, const DasUriSegDef* pDef);


/** Parse a template string using the registered coordinate definitions.
 *
 * Call once, after all DasUriTplt_register() calls.  The token syntax and
 * its rules are in the file description.
 *
 * @param pThis      The template to populate.
 * @param sTemplate  A URI template string, for example:
 *                   "/data/$Y/$j/file_$Y$j_$v.cdf"
 *                   "/data/$(time.year)/$(time.yday)/file_$v.cdf"
 *                   "vgr/$P/data_$P$(sclk.mod64k)$(sclk.mod60).dat"
 * @return DAS_OKAY on success, a positive error code otherwise.
 *
 * @memberof DasUriTplt
 */
DAS_API DasErrCode DasUriTplt_pattern(DasUriTplt* pThis, const char* sTemplate);


/** Free a template and all deep-copied coordinate definitions.
 * @memberof DasUriTplt
 */
DAS_API void del_DasUriTplt(DasUriTplt* pTplt);


/** Write a parsed template back out as a template string.
 *
 * @param pThis  The template to print.
 * @param sBuf   Buffer to receive the string; truncated if too short.
 * @param nLen   Length of sBuf.
 * @return       sBuf.
 *
 * @memberof DasUriTplt
 */
DAS_API char* DasUriTplt_toStr(const DasUriTplt* pThis, char* sBuf, int nLen);


/** Initialize a caller-allocated iterator.
 *
 * The same as new_DasUriIter() but the caller supplies the storage:
 * @code
 *   das_range r;
 *   das_range_fromUtc(&r, "2025-10-01", "2025-11-01");
 *
 *   DasUriIter iter;
 *   if(init_DasUriIter(&iter, pTplt, 1, &r) != DAS_OKAY) ...
 *   const char* sPath;
 *   while((sPath = DasUriIter_next(&iter)) != NULL) { ... }
 *   fini_DasUriIter(&iter);
 * @endcode
 *
 * @param pThis    Caller-allocated iterator storage to initialise.
 * @param pTplt    A parsed URI template; must outlive the iterator.
 * @param nRanges  Number of entries in pRanges.
 * @param pRanges  Coordinate range constraints; must outlive the iterator.
 *
 * @return DAS_OKAY on success, a positive error code otherwise, including
 *         for any http:// or https:// template.
 *
 * @memberof DasUriIter
 */
DAS_API DasErrCode init_DasUriIter(
	DasUriIter* pThis, const DasUriTplt* pTplt,
	int nRanges, const das_range* pRanges
);


/** Release the resources held by a caller-allocated iterator.
 *
 * Call this whether iteration ran to completion or was abandoned early.
 *
 * @memberof DasUriIter
 */
DAS_API void fini_DasUriIter(DasUriIter* pThis);


/** Create a heap-allocated iterator over the files that match a template.
 *
 * Template fields that no range names are unconstrained.  A template with
 * no fields at all yields its one path, whether or not the file exists.
 *
 * @param pTplt    A parsed URI template; must outlive the iterator.
 * @param nRanges  Number of entries in pRanges.
 * @param pRanges  Coordinate range constraints; must outlive the iterator.
 *
 * @return A heap-allocated DasUriIter, or NULL on error, including for any
 *         http:// or https:// template.
 *
 * @memberof DasUriIter
 */
DAS_API DasUriIter* new_DasUriIter(
	const DasUriTplt* pTplt, int nRanges, const das_range* pRanges
);


/** Return the next matching file path.
 *
 * The path has no scheme prefix and can go straight to fopen().  It sits in
 * a buffer inside the iterator that the next call overwrites.
 *
 * Within a directory, paths come back in byte order of the entry name, so
 * zero padded coordinate fields come out in coordinate order.
 *
 * @param pThis  The iterator to advance.
 * @return  The next path, or NULL when there are no more.
 *
 * @memberof DasUriIter
 */
DAS_API const char* DasUriIter_next(DasUriIter* pThis);


/** Free a heap-allocated iterator.
 * @memberof DasUriIter
 */
DAS_API void del_DasUriIter(DasUriIter* pThis);


/* ************************************************************************* */
/* Convenience functions */

/** Render a template at a single coordinate point, without touching disk.
 *
 * dBeg of each range supplies the values; dEnd is ignored.  $x, $v and
 * fields that no range names are written as '*', so the result may be a
 * glob pattern rather than a path.  http:// and https:// are kept; file://
 * is dropped, leaving a bare path.
 *
 * @param pThis    A parsed URI template.
 * @param nRanges  Number of entries in pRanges.
 * @param pRanges  Coordinate values to render; dBeg of each entry is used.
 * @param sBuf     Buffer to receive the result.
 * @param nLen     Length of sBuf; DURI_MAX_PATH is always sufficient.
 * @return         sBuf on success, NULL if the result would not fit.
 *
 * @memberof DasUriTplt
 */
DAS_API char* DasUriTplt_render(
	const DasUriTplt* pThis, int nRanges, const das_range* pRanges,
	char* sBuf, int nLen
);


/** Collect every path a template and ranges yield into one heap array.
 *
 * For callers that want the whole list up front.  Use the iterator to
 * handle files as they are found.
 *
 * @code
 *   das_range r;
 *   das_range_fromUtc(&r, "2025-288", "2025-290");
 *
 *   size_t nCount = 0;
 *   char** ppPaths = das_uri_list(
 *       "/data/$Y/$j/instrument_$Y$j_$v.cdf",
 *       das_time_uridef(),
 *       1, &r, &nCount
 *   );
 *   for(size_t i = 0; i < nCount; ++i)
 *       printf("%s\n", ppPaths[i]);
 *   free(ppPaths);
 * @endcode
 *
 * @param sTemplate  URI template string, as for DasUriTplt_pattern().
 * @param pDef       The one coordinate definition to register, or NULL for
 *                   a template with only literals, $x and $v.  Templates
 *                   that need more than one must use the iterator.
 * @param nRanges    Number of entries in pRanges.
 * @param pRanges    Array of coordinate range constraints.
 * @param pCount     If not NULL, receives the number of paths.
 * @return           A NULL-terminated array of paths, array and strings in
 *                   one block released by a single free().  NULL on error
 *                   or when nothing matches.
 */
DAS_API char** das_uri_list(
	const char* sTemplate, const DasUriSegDef* pDef,
	int nRanges, const das_range* pRanges,
	size_t* pCount
);


#ifdef __cplusplus
}
#endif

#endif /* _das_uri_h_ */
