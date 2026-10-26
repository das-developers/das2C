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

/* Settable internals shared by settable.c and source.c.  Not installed.
 *
 * The struct is here rather than in settable.c because source.c builds the
 * list when a source node loads and walks it when matching keys, and C has
 * no package-private.  Applications see only the accessors in settable.h.
 */

#ifndef _das_sett_priv_h_
#define _das_sett_priv_h_

#include "settable.h"
#include "json.h"

/* One settable.  Borrowed pointers point into the node's parsed document,
 * which the node keeps for its whole life, so nothing here is copied that
 * the document already holds in a usable form.  JSON number text is not
 * NUL terminated, hence the owned copies of default, enum and range
 * strings.
 *
 * Roughly a dozen pointers per settable.  The hsk node, the largest in the
 * wild, has 73 variables and comes to about 150 settables, so a list is a
 * few tens of kilobytes rather than the 700 KB the fixed arrays of the
 * first fetcher cost.
 */
struct das_settable {
	das_src_sec_e sec;
	das_settable_type_e type;   /* resolved at parse, see _DasSettable_type() */

	const char* sGroup;         /* borrowed dict key, "" for options */
	const char* sProp;          /* borrowed dict key, the node's spelling */
	char* sKey;                 /* owned, "coords.time.min" */

	const DasJdo* pGroup;       /* the coordinate, variable or format dict,
	                               NULL for options */
	const DasJdo* pProp;        /* the property dict: value, set, type, label */
	const DasJdo* pSet;         /* the set dict: param, flag, pval, enum, range */
	const DasJdo* pParam;       /* the protocol parameter entry for sParam, or
	                               NULL if the node does not describe it */
	const char* sParam;         /* borrowed from pSet, never NULL, see below */
	const char* sTypeName;      /* borrowed, "" when inferred */
	bool bRequired;             /* set/required or the parameter's required */

	char* sDefVal;              /* owned, NULL when the node's value is null */
	char** aEnum;               /* owned, enum values in node order */
	size_t uEnum;
	char* sLo;                  /* owned, range ends, NULL when none stated */
	char* sHi;
};

/* Build the settable list for a source node.
 *
 * Walks the four interface sections in das_src_sec_e order and makes one
 * entry for every property that has a "set" dict, so entries come out
 * grouped by section and then in document order, which is the order a
 * settings panel should show them in.  Both interface layouts in the wild
 * are read: the 2018 one with "coordinates", properties beside the group's
 * own label and units, and long property names; and the current one with
 * "coords" and properties under "props".  The layout is not reported, the
 * entries look the same either way.
 *
 * A property whose set block has no "param" cannot be sent anywhere.  Such
 * an entry is dropped with a warning naming the key instead of failing the
 * load, so a node with one bad entry still describes and serves the rest.
 * Nothing else in the interface can fail: unknown types become text and
 * missing titles are composed on demand.
 *
 * @param pIface The node's "interface" dict.  NULL or a non-dict gives an
 *        empty list, which is not an error; a source may have nothing to
 *        set.
 *
 * @param pParams The protocol's parameter dict, "httpParams" or
 *        "http_params", used for the type, required flag and range of a
 *        settable that does not state its own.  May be NULL.
 *
 * @param puCount Receives the number of entries.
 *
 * @returns A heap array of exactly *puCount entries, to be released with
 *        _DasSettable_freeList(), or NULL only when memory could not be
 *        had, in which case *puCount is 0 and an error was logged.  An
 *        empty list is a non-NULL pointer to nothing in particular, so
 *        callers test the count, not the pointer.
 */
DasSettable* _DasSettable_parse(
	const DasJdo* pIface, const DasJdo* pParams, size_t* puCount
);

/* Release a list from _DasSettable_parse() and the strings it owns.  The
 * document the entries pointed into is not touched. */
void _DasSettable_freeList(DasSettable* aList, size_t uCount);

/* Key grammar helpers, see the "Keys" block in settable.h.  Queries match
 * user input with these; keeping them next to the parser means the
 * spellings accepted on input and the spellings written into keys cannot
 * drift apart. */

/* Fold a section name as a user may type it, "coord", "coords",
 * "coordinate", "fmt", "opts" and so on, to a section.  Case insensitive.
 * Returns false if the word is not a section name. */
bool _das_src_secByName(const char* sName, das_src_sec_e* pSec);

/* The short spelling of a property name: "minimum" -> "min", "enable" ->
 * "enabled".  Names with no alias are returned as given.  Used on both
 * sides of a comparison so that either spelling matches either. */
const char* _das_src_propName(const char* sProp);

/* True if two property names are the same after _das_src_propName() and
 * case folding. */
bool _das_src_propEq(const char* sA, const char* sB);

/* Split a dotted key into at most three parts.
 *
 * @param sKey The key as typed.
 * @param aParts Receives the parts, each NUL terminated.  DAS_MAX_ID_BUFSZ
 *        per part is enough for anything a node may contain.
 * @returns The number of parts, 1 to 3, or 0 if the key is empty, has an
 *        empty part, has more than three parts, or has a part that does
 *        not fit.
 */
int _das_src_splitKey(const char* sKey, char aParts[3][DAS_MAX_ID_BUFSZ]);

/* Copy a JSON scalar as text: strings as they are, numbers in their source
 * notation, true and false as those words.  Returns false for null,
 * containers and missing values, leaving sBuf untouched.  This is the one
 * reader of JSON number text in the catalog code, since das_json keeps
 * numbers as unterminated text and a default of 86.4 should stay 86.4. */
bool _das_json_scalar(const DasJdo* pJdo, char* sBuf, size_t uLen);

#endif /* _das_sett_priv_h_ */
