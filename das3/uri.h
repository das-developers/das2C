/* Copyright (C) 2026 Chris Piker <chris-piker@uiowa.edu>
 *
 * This file is part of das2C, the Core Das C Library.
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
 * Any DasUriField with a non-zero shortChar gets a short token.  A Voyager
 * spacecraft clock registered with fields 'P' (partition), 'M' (mod64k) and
 * 'S' (mod60) could be used as:
 *
 *   P$P/V1P$P_$x/C$M$S.DAT
 *
 * The long form names the coordinate (DasUriSegDef.coord) and the field
 * (DasUriField.longName), the same dotted name that a DasDimRng uses:
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
 * DasUriTplt_register() before calling DasUriTplt_setPattern().  The library
 * ships one, DasUriSegDef_timeToks(), which supplies $Y $m $d $j $H $M $S.  A
 * coordinate such as an orbit counter is one more definition; no mapping
 * from orbit to time is assumed.
 *
 * Field values are non-negative integers written in decimal digits.
 *
 * @par How ranges select files
 *
 * Files are selected by DasDimRng values: a DasRng (see datum.h)
 * plus the name of what it constrains.  Template fields that no range names
 * are unconstrained.  Ranges are half-open: [dmBeg, dmEnd).
 *
 *   "time"          The whole time coordinate; dmBeg and dmEnd are DASVT_TIME.
 *                   An entry is kept when the interval it covers overlaps the
 *                   range.  That interval runs from the time assembled out
 *                   of every time field in the path so far to one unit of
 *                   the finest such field later: a $Y directory covers a
 *                   year, a file named to the day covers that day.
 *
 *   "sclk.mod64k"   One sub-field, as "coord.field"; begin and end are
 *   "time.year"     numbers, truncated to integers, tested against that
 *                   field alone.  begin > end is a rollover: the range runs
 *                   from begin up to the field's max, then from min up to
 *                   end.
 *
 * A null end is open, see DasRng.  A "time" range with only begin keeps
 * everything from begin on; with only end, everything before it.  A
 * sub-field range with one open end runs to the field's min or max and
 * never rolls over.  A range open at both ends constrains nothing.
 *
 * A file's name is taken as the start of its coverage.  No search is made
 * for a file that starts before dmBeg and runs into the range.
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

/** Longest template, and the suggested buffer size for paths.
 *
 * Templates longer than this are refused.  Path buffers are the caller's,
 * see DasUriIter_next() and DasUriTplt_render(); this size always holds a
 * local path from a template that parsed. */
#define DASURITPLT_PATH_SZ  1024


/* ************************************************************************* */
/* Coordinate segment field and type definitions */

/** One sub-field within a named coordinate type.
 *
 * The "time" coordinate has seven of these, an orbit counter has one.
 * longName must be lower case to be reachable from a DasDimRng.
 *
 * Callers build arrays of these.  The layout is versioned by the
 * DasUriSegDef that points to the array, so the rows carry no version of
 * their own.
 *
 * @see DasUriSegDef
 */
typedef struct das_urifield_t {
	int  width;          /* rendered field width (zero-padded); 0 = variable   */
	int  min;            /* smallest valid value; names outside min..max are  */
	int  max;            /* skipped with a warning                            */
	char longName[32];   /* long name, the "year" in $(time.year)             */
	char shortChar;      /* single-char short token, e.g. 'Y'; '\0' if none   */
} DasUriField;


/** The first DasUriSegDef and DasUriField layout, das 3.0.  Same numbering
 * as DASIOPROC_V3_0. */
#define DASURISEGDEF_V3_0 ((3 << 8) | 0)

/** The DasUriSegDef and DasUriField layout described by this header */
#define DASURISEGDEF_VER DASURISEGDEF_V3_0

/** Start a DasUriSegDef designated initializer with this, so a definition
 * records its layout version without naming the field:
 *   DasUriSegDef def = { DASURISEGDEF_INIT, .coord = "sclk", ... };
 */
#define DASURISEGDEF_INIT .uStructVer = DASURISEGDEF_VER

/** Definition of one coordinate type for use by the URI template parser.
 *
 * DasUriTplt_register() copies the definition and its field array, so both
 * may be on the stack and go out of scope afterwards.  coord must be lower
 * case to be reachable from a DasDimRng.
 *
 * Start each definition with DASURISEGDEF_INIT.  It records the layout of this
 * structure and of its DasUriField rows as this program was compiled, so a
 * later library that adds members can still read an older program's tables.
 * A zero or unknown version is refused.
 *
 * @code
 *   DasUriField aSclkFlds[] = {
 *     { .shortChar = 'P', .longName = "partition", .width = 1, .min = 0, .max =     9 },
 *     { .shortChar = 'M', .longName = "mod64k",    .width = 5, .min = 0, .max = 65535 },
 *     { .shortChar = 'S', .longName = "mod60",     .width = 2, .min = 0, .max =    59 },
 *     { .shortChar = 'L', .longName = "line",      .width = 3, .min = 1, .max =   800 },
 *   };
 *   DasUriSegDef sclkDef = {
 *       DASURISEGDEF_INIT, .coord = "sclk",
 *       .numFields = 4, .fields = aSclkFlds
 *   };
 *   DasUriTplt_register(pTplt, &sclkDef);
 * @endcode
 */
typedef struct das_urisegdef_t {
	size_t             uStructVer; /* DASURISEGDEF_VER, see above              */
	const DasUriField* fields;     /* sub-field table, copied by register()    */
	int                numFields;  /* number of entries in fields              */
	char               coord[32];  /* coordinate, e.g. "time", "sclk", "orbit" */
} DasUriSegDef;


/* ************************************************************************* */
/* A parsed URI template */

/** @class DasUriTplt uri.h <das3/uri.h>
 *
 * A parsed URI template: the coordinate definitions registered with it and
 * the plan for searching directories that DasUriTplt_setPattern() builds.
 * The steps run in a fixed order.  
 * 
 * 1. Create it
 * 2. register each coordinate type the template uses
 * 3. parse the template string once, then
 * 4. hand it to as many iterators or DasUriTplt_render() calls as needed.  
 * 
 * After parsing the template is only read, so several iterators may share one.
 *
 * @code
 *   DasUriTplt* pTplt = new_DasUriTplt();
 *   DasUriTplt_register(pTplt, DasUriSegDef_timeToks());
 *   if(DasUriTplt_setPattern(pTplt, "/data/$Y/$j/file_$Y$j_$v.cdf") != DAS_OKAY)
 *       
 *   // ... iterate, see init_DasUriIter() ...
 * 
 *   del_DasUriTplt(pTplt);
 * @endcode
 *
 * The template must outlive every iterator made from it.
 */
typedef struct das_uritplt_t DasUriTplt;


/* ************************************************************************* */
/* A streaming iterator over a URI template and coordinate ranges */

/** Iteration state values, see DasUriIter.nState */
#define DASURIITER_RUNNING  -1
#define DASURIITER_DONE      0

/** A streaming iterator over the files a template and ranges select.
 *
 * A frozen stack record: it holds the iterator's inputs and a handle to its
 * scan state, so it never needs to grow.  Paths are written to a buffer the
 * caller passes to each DasUriIter_next() call.
 *
 * nState is DASURIITER_RUNNING until the iteration ends, DASURIITER_DONE when
 * it ran to completion, or a positive das_errcode when it stopped on a
 * problem.  A zeroed DasUriIter reads as done.
 */
typedef struct das_uriiter_t {
	const DasUriTplt*    pTplt;    /* must outlive the iterator               */
	const DasDimRng* pRanges;  /* caller owns; must outlive iterator      */
	void*                pState;   /* scan state, owned by the iterator       */
	int                  nRanges;
	int                  nState;   /* running, done, or an error code         */
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
DAS_API const DasUriSegDef* DasUriSegDef_timeToks(void);


/** Allocate an empty URI template object.
 *
 * Register coordinate definitions with DasUriTplt_register(), then call
 * DasUriTplt_setPattern().  With none registered only literals, $x and $v are
 * recognised.
 *
 * @return A heap-allocated DasUriTplt, or NULL on allocation failure.
 *
 * @memberof DasUriTplt
 */
DAS_API DasUriTplt* new_DasUriTplt(void);


/** Register a coordinate type definition with a template.
 *
 * Must be called before DasUriTplt_setPattern().  pDef and its field array are
 * copied.  A definition whose uStructVer this library doesn't know is refused.  It is an error to register a coordinate name twice, or a short
 * token that an earlier definition already uses: short tokens are shared by
 * all coordinates of a template.
 *
 * @param pThis  The template to extend.
 * @param pDef   Coordinate definition to copy in; caller retains ownership.
 * @return DAS_OKAY on success, a positive error code otherwise.
 *
 * @memberof DasUriTplt
 */
DAS_API das_errcode DasUriTplt_register(DasUriTplt* pThis, const DasUriSegDef* pDef);


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
DAS_API das_errcode DasUriTplt_setPattern(DasUriTplt* pThis, const char* sTemplate);


/** Free a template and all deep-copied coordinate definitions.
 * @memberof DasUriTplt
 */
DAS_API void del_DasUriTplt(DasUriTplt* pTplt);


/** Write a parsed template back out as a template string.
 *
 * @param pThis  The template to print.
 * @param sBuf   Buffer to receive the string; truncated if too short.
 * @param uLen   Size of sBuf, including room for the NUL.
 * @return       sBuf, or NULL if the whole pattern did not fit.  sBuf is
 *               always NUL terminated when uLen > 0.
 *
 * @memberof DasUriTplt
 */
DAS_API char* DasUriTplt_toStr(const DasUriTplt* pThis, char* sBuf, size_t uLen);


/** Initialize a caller-allocated iterator.
 *
 * The same as new_DasUriIter() but the caller supplies the storage:
 * @code
 *   DasRng rng;
 *   DasRng_fromIso(&rng, "2025-10-01", "2025-11-01");
 *   DasDimRng dr;
 *   DasDimRng_set(&dr, "time", &rng);
 *
 *   DasUriIter iter;
 *   if(init_DasUriIter(&iter, pTplt, 1, &dr) != DAS_OKAY) ...
 *   char sPath[DASURITPLT_PATH_SZ];
 *   while(DasUriIter_next(&iter, sPath, sizeof(sPath)) != NULL) { ... }
 *   if(DasUriIter_errCode(&iter) != DAS_OKAY) ...
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
DAS_API das_errcode init_DasUriIter(
	DasUriIter* pThis, const DasUriTplt* pTplt,
	int nRanges, const DasDimRng* pRanges
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
	const DasUriTplt* pTplt, int nRanges, const DasDimRng* pRanges
);


/** Write the next matching file path to a caller's buffer.
 *
 * The path has no scheme prefix and can go straight to fopen().  The
 * iterator keeps no reference to sBuf, so each call may pass a different
 * one.
 *
 * Within a directory, paths come back in byte order of the entry name, so
 * zero padded coordinate fields come out in coordinate order.
 *
 * A path that does not fit in uLen stops the iteration: it is logged, and
 * nState takes the error code.  Problems found in the archive end the
 * iteration, never the program, so they do not go through daserrf().
 * Once stopped, every later call returns NULL.
 *
 * @param pThis  The iterator to advance.
 * @param sBuf   Buffer to receive the path.
 * @param uLen   Size of sBuf; DASURITPLT_PATH_SZ is the suggested size.
 * @return  sBuf, or NULL when the iteration is done or has stopped on a
 *          problem; DasUriIter_errCode() tells which.
 *
 * @memberof DasUriIter
 */
DAS_API const char* DasUriIter_next(DasUriIter* pThis, char* sBuf, size_t uLen);


/** Why did the iteration stop?
 *
 * @param pThis  The iterator to check.
 * @return DAS_OKAY while running or after a complete run, otherwise the
 *         error code that stopped it.
 *
 * @memberof DasUriIter
 */
DAS_API das_errcode DasUriIter_errCode(const DasUriIter* pThis);


/** Free a heap-allocated iterator.
 * @memberof DasUriIter
 */
DAS_API void del_DasUriIter(DasUriIter* pThis);


/* ************************************************************************* */
/* Convenience functions */

/** Render a template at a single coordinate point, without touching disk.
 *
 * dmBeg of each range supplies the values; dmEnd is ignored.  $x, $v and
 * fields that no range names, or whose dmBeg is open, are written as '*', so
 * the result may be a glob pattern rather than a path.  http:// and https:// are kept; file://
 * is dropped, leaving a bare path.
 *
 * @param pThis    A parsed URI template.
 * @param nRanges  Number of entries in pRanges.
 * @param pRanges  Coordinate values to render; dmBeg of each entry is used.
 * @param sBuf     Buffer to receive the result.
 * @param uLen     Size of sBuf; DASURITPLT_PATH_SZ is always sufficient.
 * @return         sBuf on success, NULL if the result would not fit.
 *
 * @memberof DasUriTplt
 */
DAS_API char* DasUriTplt_render(
	const DasUriTplt* pThis, int nRanges, const DasDimRng* pRanges,
	char* sBuf, size_t uLen
);


/** Collect every path a template and ranges yield into one heap array.
 *
 * For callers that want the whole list up front.  Use the iterator to
 * handle files as they are found.
 *
 * @code
 *   DasRng rng;
 *   DasRng_fromIso(&rng, "2025-288", "2025-290");
 *   DasDimRng dr;
 *   DasDimRng_set(&dr, "time", &rng);
 *
 *   size_t nCount = 0;
 *   char** ppPaths = DasUriTplt_newList(
 *       "/data/$Y/$j/instrument_$Y$j_$v.cdf",
 *       DasUriSegDef_timeToks(),
 *       1, &dr, &nCount
 *   );
 *   for(size_t i = 0; i < nCount; ++i)
 *       printf("%s\n", ppPaths[i]);
 *   free(ppPaths);
 * @endcode
 *
 * @param sTemplate  URI template string, as for DasUriTplt_setPattern().
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
DAS_API char** DasUriTplt_newList(
	const char* sTemplate, const DasUriSegDef* pDef,
	int nRanges, const DasDimRng* pRanges,
	size_t* pCount
);


#ifdef __cplusplus
}
#endif

#endif /* _das_uri_h_ */
