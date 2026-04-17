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
 * ## Core concept: coordinates in filenames
 *
 * Scientific file archives commonly encode one or more *coordinate values*
 * in each filename to locate that file in a parameter space.  A URI template
 * describes how those coordinates appear in a path so that, given a coordinate
 * range, the matching set of files can be found by directory scan.
 *
 * The most common coordinate is time.  Because time appears so frequently,
 * and because its sub-fields (year, month, day, hour, ...) have well-known
 * rollover relationships, short single-character codes are provided for each
 * sub-field ($Y, $m, $d, etc.).  These are syntactic sugar for the time
 * coordinate; they are not conceptually different from any other coordinate.
 *
 * Other coordinates are expressed with named tokens in the $() syntax.
 * For example, a future version of this module is expected to support:
 *
 *   $(lat) $(lon)    geodetic latitude and longitude (e.g. orbital camera
 *                    images named by the central pointing of the instrument,
 *                    as opposed to the start time of the observation)
 *
 *   $(orbit)         monotonically increasing integer orbit counter, common
 *                    in planetary missions.  Mars Express, Juno, and Cassini
 *                    all organise some of their data products by orbit number.
 *                    Orbit numbers are NOT time; no orbit-to-time mapping is
 *                    assumed or required by this module.
 *
 *   $(sclk:partition)  Spacecraft-clock sub-fields.  Like time, a spacecraft
 *   $(sclk:mod64k)     clock is a compound coordinate with named sub-fields
 *   $(sclk:mod60)      and defined rollover rules, but those rules are
 *   $(sclk:line)       mission-specific and must be supplied by the caller.
 *                      For example Voyager spacecraft clock has the following:
 *                      fields and rollover points and are often found in
 *                      filenames: 'mod64k' 0-65535, 'mod60' 0-59, 'line' 1-800.
 *
 * These extended coordinates are documented here as design intent.  Only
 * time sub-fields are implemented in the current version (see field codes
 * below).  Code that encounters an unrecognised coordinate name in $() syntax
 * should treat it as a wildcard and emit a warning, not a hard error, so
 * that templates written for future versions degrade gracefully.
 *
 * ## Directory listing is always available
 *
 * This implementation does not attempt to guess or generate file paths
 * speculatively.  A directory listing (or equivalent) is always performed
 * when needed.  Templates whose file stores cannot provide directory listings
 * are out of scope.
 *
 * ## Relationship to the Autoplot URI template specification
 *
 * Since Autoplot is a very common (and useful) program in space-physics,
 * it's URI field codes are use here whenever possible. In most common cases a
 * DasUriTplt will look identical to it's Autoplot counterpart. Some of the
 * ways in which this imlementation differs from Autoplot are noted below.
 *
 *   - Directory listing is always assumed available; speculative URL
 *     generation (the AP "shoot-in-the-dark" mode) is not supported.
 *   - The coordinate concept is generalised beyond time.
 *   - Version selection ($v) uses numeric comparison, not just lexicographic.
 *   - Variable-duration files (where the encoded coordinate is the start
 *     point and file length is non-uniform) are handled by directory scan
 *     and backward search, not by fixed-cadence stepping.
 *
 * ## Protocols
 *
 * Templates may begin with a protocol prefix.  If no prefix is present,
 * local filesystem access is assumed.
 *
 *   file://    Explicit local filesystem.  The prefix is stripped before
 *              any filesystem call so the returned path is usable directly
 *              with fopen(), CDFopenCDF(), etc.
 *
 *   http://    Remote HTTP.  DasUriIter_next() returns the full URL; the
 *   https://   caller is responsible for downloading before opening.
 *
 * Wildcard and version scanning ($x, $v) requires directory listing.  For
 * file:// this is done with opendir/readdir.  Directory listing over HTTP
 * is not yet supported but will be implemented as soon as it's needed.
 * using $x or $v in an http:// or https:// template is an error detected at
 * iterator initialisation time.
 *
 * ## Supported time field codes (v1)
 *
 *   $Y   4-digit year                  \
 *   $m   month (01-12)                  |
 *   $d   day of month (01-31)           |  sub-fields of the
 *   $j   day of year (001-366)          |  implicit time coordinate
 *   $H   hour (00-23)                   |
 *   $M   minute (00-59)                 |
 *   $S   integer second (00-60)        /
 *
 * ## Other supported field codes (v1)
 * 
 *   $x   Unstructured wildcard.  Matched portion is treated as an opaque
 *        string; the lexicographically last match in the directory is used.
 *        Not a coordinate — carries no query-relevant information.
 *        Requires file:// or implicit local protocol.
 *
 *   $v   Version wildcard.  Like $x but the matched portion is interpreted
 *        as a version identifier; the numerically greatest version is used.
 *        The comparison strategy is set by the type= modifier (see below).
 *        Not a coordinate — carries no query-relevant information.
 *        Requires file:// or implicit local protocol.
 *
 * ## Complex field syntax
 *
 *   $(code) or $(code;modifier=value;...)
 *
 * ## Supported general modifiers
 *
 *   delta=N     Coverage hint.  File covers approximately N units of the
 *               field's own unit (default 1).  Used to scan backward
 *               when looking for variable-coverage files that may start
 *               before the query start range.
 *
 *   pad=none    Suppress leading-zero padding (default is zero-padded).
 *
 * ## Supported version field modifiers
 * 
 *   type=sep    (default for $v) Split the matched string on '.' and compare
 *               each component as a non-negative integer left-to-right.
 *               Handles semantic versions such as 0.5.20 and 0.7.1
 *               correctly regardless of component width.
 *
 *   type=int    Parse the entire matched string as a single non-negative
 *               integer.  Handles zero-padded counters such as v01 and v02.
 *
 *   type=alpha  Lexicographic comparison, identical to $x behaviour.
 *               Useful when the version token is already fixed-width and
 *               lexicographic order matches version order.
 *
 * ## Version collision warning
 *
 * When type=sep or type=int is in use, two files may resolve to the same
 * numeric version through different string representations.  For example:
 *
 *   data_$Y$m$d_$(v;type=int).cdf
 *
 * could match both "data_20250930_v1.cdf" and "data_20250930_v01.cdf" in
 * the same directory, both evaluating to version 1. Since this is usually
 * unintentional, a warning is issued on standard error to help you find 
 * such cases in your datasets.
 */

#ifndef _das_uri_h_
#define _das_uri_h_

#include <stdbool.h>

#include <das2/defs.h>
#include <das2/datum.h>

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
 * A DasUriSegDef carries an array of these, one per sub-field.  For example
 * the "time" coordinate has seven entries ($Y/$m/$d/$j/$H/$M/$S).  A simple
 * scalar coordinate such as orbit has exactly one entry.
 *
 * @see DasUriSegDef
 */
typedef struct das_uri_field_t {
	char cShort;      /* single-char short token, e.g. 'Y'; '\0' if none     */
	char sLong[32];   /* long name used in $() syntax, e.g. "year"           */
	int  nWidth;      /* rendered field width (zero-padded); 0 = variable    */
	int  nMin;        /* valid decoded-value range, minimum (inclusive)       */
	int  nMax;        /* valid decoded-value range, maximum (inclusive)       */
} DasUriField;


/** Definition of one coordinate type for use by the URI template parser.
 *
 * Register one or more of these with a DasUriTplt before calling
 * DasUriTplt_pattern().  The template copies the definition and the field
 * array into its own heap storage, so the caller's DasUriSegDef and its
 * pFields array may be stack-allocated and may go out of scope after
 * DasUriTplt_register() returns.
 *
 * Example — Voyager spacecraft clock:
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
 *
 * Use das_time_uridef() to obtain the pre-built definition for the time
 * coordinate rather than constructing it by hand.
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
 * An array of das_range structures is passed to the iterator to describe the
 * query.  Each entry constrains one named coordinate or sub-field.  Template
 * fields whose coordinate is not represented in the array are treated as
 * wildcards.
 *
 * ## sCoord format
 *
 * sCoord names the coordinate or sub-field to constrain:
 *
 *   "time"          Whole time coordinate.  dBeg and dEnd must be vtTime
 *                   datums (e.g. from das_datum_fromStr() with an ISO-8601
 *                   string).  All time sub-fields in the template are
 *                   filtered against this range.
 *
 *   "time.year"     A single time sub-field.  dBeg/dEnd must satisfy
 *   "time.month"    das_vt_isint(datum.vt).  Useful when only part of the
 *   "time.mday"     time coordinate is encoded in the filename.
 *   etc.
 *
 *   "sclk.mod64k"   A sub-field of a user-registered coordinate type.
 *   "sclk.mod60"    dBeg/dEnd must satisfy das_vt_isint(datum.vt).
 *   etc.
 *
 * Matching is case-insensitive on the coordinate name portion.  The dot
 * separator and sub-field name are case-sensitive.
 *
 * ## Datum types
 *
 * - sCoord == "time" (whole coordinate): datum.vt must be vtTime.
 * - All other sCoord values: das_vt_isint(datum.vt) must be true.
 *   Floating-point coordinates are not supported; decimal points in
 *   filenames (before the extension) are essentially unheard of in practice.
 *
 * ## Example — query files covering 2025-Oct:
 * @code
 *   das_range r;
 *   strcpy(r.sCoord, "time");
 *   das_datum_fromStr(&r.dBeg, "2025-10-01");
 *   das_datum_fromStr(&r.dEnd, "2025-11-01");
 * @endcode
 *
 * The range is half-open: [dBeg, dEnd).  Because a filename may encode only
 * the *start* of coverage, an extra backward scan is performed to catch files
 * that started before dBeg but whose coverage extends into the query range;
 * the breadth of that scan is governed by the delta= modifier on the
 * template's finest-grained field, or by the natural unit of that field if
 * no delta is specified.
 */
typedef struct das_range_t {
	char      sCoord[32]; /* coordinate or sub-field: "time", "sclk.mod64k"  */
	das_datum dBeg;       /* inclusive range begin                             */
	das_datum dEnd;       /* exclusive range end                               */
} das_range;


/* ************************************************************************* */
/* Protocol enumeration */

/** Transport protocol detected from the leading scheme of a URI template.
 *  Stored on DasUriTplt for fast access; also carried by the first segment
 *  when an explicit scheme is present (DURI_PROTOCOL segment). */
typedef enum das_uri_proto_e {
	DURI_PROTO_FILE  = 0, /* no prefix, or explicit file://                    */
	DURI_PROTO_HTTP,      /* http://                                            */
	DURI_PROTO_HTTPS,     /* https://                                           */
} DasUriProto;


/* Opaque segment type — full definition in uri.c */
typedef struct das_uri_seg_t DasUriSeg;

/* Opaque level plan type — full definition in uri.c.
 * A "level" is one path component (one directory name, or the filename).
 * The plan is built from pSegs at DasUriTplt_pattern() time and is read-only
 * thereafter; iterators hold only runtime directory-walk state, not plan state. */
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
	char*        sBase;     /* fixed path prefix up to the first variable;     */
	                        /* "." for CWD-relative templates; root ("/" on    */
	                        /* POSIX, "C:\\" on Windows) if only the root is   */
	                        /* fixed.  No trailing separator otherwise.        */
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
	                 /* For file:// the file:// prefix is stripped; the path  */
	                 /* is usable directly with fopen() etc.                  */
	                 /* For http/https the full URL is stored here; the caller*/
	                 /* is responsible for downloading before use.            */
	void*             pState;         /* internal heap-allocated scan state;  */
	                                  /* managed by init_/fini_/new_/del_     */
} DasUriIter;


/* ************************************************************************* */
/* API */

/** Return the built-in coordinate definition for the time coordinate.
 *
 * Returns a pointer to a file-scope static DasUriSegDef pre-loaded with
 * entries for $Y, $m, $d, $j, $H, $M, $S.  No allocation; the pointer is
 * always valid.  Pass it to DasUriTplt_register() to enable time field
 * parsing.
 */
DAS_API const DasUriSegDef* das_time_uridef(void);


/** Allocate an empty URI template object.
 *
 * The returned template recognises only literals, $x, and $v until one or
 * more coordinate definitions are registered with DasUriTplt_register().
 * Call DasUriTplt_pattern() after all registrations to parse the template
 * string.
 *
 * @return A heap-allocated DasUriTplt, or NULL on allocation failure.
 *
 * @memberof DasUriTplt
 */
DAS_API DasUriTplt* new_DasUriTplt(void);


/** Register a coordinate type definition with a template.
 *
 * Deep-copies pDef and its pFields array into the template's own heap
 * storage.  The caller's DasUriSegDef and pFields array may be
 * stack-allocated and may go out of scope immediately after this call.
 *
 * Must be called before DasUriTplt_pattern().  Registering the same
 * coordinate name twice is an error.
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
 * Tokens recognised by any registered DasUriSegDef are parsed as DURI_COORD
 * segments.  Unrecognised $() tokens emit daslog_warn_v() and are treated as
 * DURI_WILD.  Must be called after all DasUriTplt_register() calls.
 *
 * @param pThis      The template to populate.
 * @param sTemplate  A URI template string such as
 *                   "/data/$Y/$m/$d/file_$Y$m$d_$v.cdf" or
 *                   "https://host/data/$Y/$m/file_$Y$m.cdf" or
 *                   "file:///vgr/$P/data_$P$M_$S_$L.dat"
 * @return DAS_OKAY on success, a positive error code otherwise.
 *
 * @memberof DasUriTplt
 */
DAS_API DasErrCode DasUriTplt_pattern(DasUriTplt* pThis, const char* sTemplate);


/** Free a template and all deep-copied coordinate definitions.
 * @memberof DasUriTplt
 */
DAS_API void del_DasUriTplt(DasUriTplt* pTplt);


/** Describe a parsed template: protocol, wildcard status, segment count.
 *
 * @param pThis  The template to describe.
 * @param sBuf   Buffer to receive the description string.
 * @param nLen   Length of sBuf.
 * @return       sBuf.
 *
 * @memberof DasUriTplt
 */
DAS_API char* DasUriTplt_toStr(const DasUriTplt* pThis, char* sBuf, int nLen);


/** Initialize a caller-allocated iterator.
 *
 * Same semantics as new_DasUriIter() but the caller supplies the storage,
 * making stack allocation possible:
 * @code
 *   das_range r;
 *   strcpy(r.sCoord, "time");
 *   das_datum_fromStr(&r.dBeg, "2025-10-01");
 *   das_datum_fromStr(&r.dEnd, "2025-11-01");
 *
 *   DasUriIter iter;
 *   if(init_DasUriIter(&iter, pTplt, 1, &r) != DAS_OKAY) ...
 *   const char* sPath;
 *   while((sPath = DasUriIter_next(&iter)) != NULL) { ... }
 *   fini_DasUriIter(&iter);
 * @endcode
 *
 * The caller must keep pRanges alive for the full lifetime of the iterator.
 *
 * Returns an error if the template uses $x or $v with an http:// or
 * https:// protocol, as remote directory listing is not yet supported.
 *
 * @param pThis    Caller-allocated iterator storage to initialise.
 * @param pTplt    A parsed URI template.
 * @param nRanges  Number of entries in pRanges.
 * @param pRanges  Array of coordinate range constraints; caller owns.
 *
 * @return DAS_OKAY on success, a positive error code otherwise.
 *
 * @memberof DasUriIter
 */
DAS_API DasErrCode init_DasUriIter(
	DasUriIter* pThis, const DasUriTplt* pTplt,
	int nRanges, const das_range* pRanges
);


/** Release any resources held by a stack-allocated iterator.
 *
 * Always call this when done, whether iteration ran to completion or was
 * abandoned early.  Safe to call on any initialised iterator.
 *
 * @memberof DasUriIter
 */
DAS_API void fini_DasUriIter(DasUriIter* pThis);


/** Create a heap-allocated iterator that yields one matching file path per call.
 *
 * The iterator scans directories whose path-level coordinates fall within the
 * given ranges, then filters files by their filename-level coordinates.
 * Template fields whose coordinate is absent from pRanges are treated as
 * wildcards.  For file:// templates containing $x or $v, each directory scan
 * step selects the lexicographically last ($x) or numerically greatest ($v)
 * match.  Steps with no matching file are silently skipped.
 *
 * For http:// and https:// templates each step yields the rendered URL.
 * Downloading is the caller's responsibility.  $x and $v are not permitted
 * with http:// or https:// and will cause this function to return NULL.
 *
 * For templates with no coordinate fields (bLiteral == true), pRanges is
 * ignored and exactly one path is yielded.
 *
 * @param pTplt    A parsed URI template.
 * @param nRanges  Number of entries in pRanges.
 * @param pRanges  Array of coordinate range constraints; caller owns.
 *
 * @return A heap-allocated DasUriIter, or NULL on error.
 *
 * @memberof DasUriIter
 */
DAS_API DasUriIter* new_DasUriIter(
	const DasUriTplt* pTplt, int nRanges, const das_range* pRanges
);


/** Advance the iterator and return the next file path or URL.
 *
 * For file:// templates the returned string is a bare filesystem path with
 * no scheme prefix, usable directly with fopen(), CDFopenCDF(), etc.
 *
 * For http:// and https:// templates the returned string is the full URL.
 * The caller must download the resource before opening it.
 *
 * The returned pointer addresses an internal buffer that is overwritten on
 * each call; copy the string if you need to retain it across calls.
 *
 * @param pThis  The iterator to advance.
 * @return  Next path or URL, or NULL when the coordinate ranges are exhausted.
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

/** Render a template at a single coordinate point without creating an iterator.
 *
 * For each das_range in pRanges, dBeg is used as the rendering point; dEnd
 * is ignored.  Template fields whose coordinate is absent from pRanges are
 * rendered as '*' so the result may be a glob pattern rather than a usable
 * path when wildcards are present.
 *
 * Useful for log messages, probing whether a specific file exists, and
 * unit testing the render logic independently of directory scan logic.
 * For file:// templates the file:// prefix is stripped from the output.
 * For http:// and https:// templates the full URL is written to sBuf.
 *
 * @param pThis    A parsed URI template.
 * @param nRanges  Number of entries in pRanges.
 * @param pRanges  Coordinate values to render; dBeg of each entry is used.
 * @param sBuf     Buffer to receive the rendered path or URL.
 * @param nLen     Length of sBuf; DURI_MAX_PATH is always sufficient.
 * @return         sBuf on success, NULL if the rendered path would overflow
 *                 (error reported via das_error).
 *
 * @memberof DasUriTplt
 */
DAS_API char* DasUriTplt_render(
	const DasUriTplt* pThis, int nRanges, const das_range* pRanges,
	char* sBuf, int nLen
);


/** Collect all paths yielded by a template and coordinate ranges into a heap array.
 *
 * Convenience wrapper around new_DasUriTplt() + new_DasUriIter() for
 * callers that need the complete list up front (unit tests, small utilities).
 * For production streaming use prefer the iterator directly so that files
 * can be processed as they are found without buffering the full path list.
 *
 * The returned array is NULL-terminated; the count is also written to
 * *pCount if pCount is not NULL.  Each element and the array itself are
 * heap-allocated; free the whole structure with das_uri_free_list().
 *
 * Returns NULL (and sets *pCount to 0) if the template cannot be parsed,
 * all ranges are empty, or no files are found.
 *
 * @param sTemplate  URI template string, same syntax as new_DasUriTplt().
 * @param nRanges    Number of entries in pRanges.
 * @param pRanges    Array of coordinate range constraints.
 * @param pCount     If not NULL, receives the number of entries in the array.
 * @return           NULL-terminated array of heap-allocated path strings,
 *                   or NULL on error or empty result.
 */
DAS_API char** das_uri_list(
	const char* sTemplate, int nRanges, const das_range* pRanges,
	size_t* pCount
);


/** Free a list returned by das_uri_list().
 *
 * Frees each string in the array and then the array itself.  Safe to call
 * with a NULL pointer.
 *
 * @param ppList  The array returned by das_uri_list().
 */
DAS_API void das_uri_free_list(char** ppList);


#ifdef __cplusplus
}
#endif

#endif /* _das_uri_h_ */
