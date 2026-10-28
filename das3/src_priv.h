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

/* Source internals.  Not installed.
 *
 * Everything in source.c sees these; catalog.c includes this header only
 * for the default source rule.  Applications see the accessors in
 * source.h and nothing of the layout, so it can change after the tag.
 *
 * Ownership in one sentence: the source owns its document copy, its
 * groups, its properties and their controls, and the example
 * requisitions; everything that points into the document is borrowed
 * from it and dies with it.
 */

#ifndef _das_src_priv_h_
#define _das_src_priv_h_

#include "source.h"
#include "json.h"
#include "req_priv.h"

/* A control.  Borrowed pointers point into the source's document.  JSON
 * number text is not NUL terminated, hence the owned copies of enum and
 * range strings. */
struct das_src_ctrl {
	DasSrcProp* pProp;          /* back pointer, the owner */
	const DasJdo* pSet;         /* the set dict: param, flag, pval, enum, range, value */
	const DasJdo* pParam;       /* the protocol parameter entry for sParam, or
	                               NULL if the node does not describe it */
	const char* sParam;         /* borrowed from pSet, never NULL */
	bool bRequired;             /* set/required or the parameter's required */
	char** aEnum;               /* owned, enum values in node order */
	size_t uEnum;
	char* sLo;                  /* owned, range ends, NULL when none stated */
	char* sHi;
};

struct das_src_prop {
	das_src_sec_e sec;
	DasSrcGroup* pGroup;        /* back pointer */
	const char* sName;          /* borrowed dict key, the node's spelling */
	char* sKey;                 /* owned, "coords.time.min" */
	const DasJdo* pJdo;         /* the property dict: value, set, type, label... */
	das_srcprop_type_e type;
	const char* sTypeName;      /* borrowed, "" when inferred */
	char* sValue;               /* owned scalar copy, NULL when null or absent */
	DasSrcCtrl* pCtrl;          /* owned, NULL for a fixed property */
};

/* Properties of a group are adjacent in the source's aProp array, so a
 * group is an index range. */
struct das_src_group {
	const DasSrc* pSrc;         /* back pointer, for reaching the properties */
	das_src_sec_e sec;
	const char* sName;          /* borrowed dict key, "" for options */
	const DasJdo* pJdo;         /* the coordinate, variable, format or options dict */
	size_t uFirst;              /* into DasSrc.aProp */
	size_t uCount;
	int nEnable;                /* index of the "enabled" prop, -1 if none */
	const char* sXor;           /* borrowed, enable prop's xorGroup, or NULL */
	const char** aMime;         /* owned array of borrowed strings, formats only */
	size_t uMime;
};

struct das_src {
	das_src_kind_e kind;
	char* sPath;                /* owned, "" when none */
	char* sUrl;                 /* owned, "" when none */

	DasJdo* pDoc;               /* owned, the parsed copy of the node document */
	const DasJdo* pIface;       /* "interface", may be NULL */
	bool bLegacy;               /* the 2018 layout, see _DasSrc_isLegacy() */

	DasSrcGroup* aGroup;        /* owned, in section then presentation order */
	size_t uGroup;
	size_t aSecFirst[das_src_sec_size];   /* first group of each section */
	size_t aSecCount[das_src_sec_size];

	DasSrcProp* aProp;          /* owned, grouped as the groups say */
	size_t uProp;
	DasSrcCtrl* aCtrl;          /* owned, one per settable property, in
	                               property order; props point into it */
	size_t uCtrl;

	DasReq** aExample;          /* owned, one per example, built at load */
	size_t uExample;
};

/* The stream source.  Base first so a DasStreamSrc* is a DasSrc*; the cast
 * back is made only after kind says das_src_kind_stream. */
typedef struct das_stream_src {
	DasSrc base;
	const DasJdo* pProto;       /* "protocol", NULL makes the source unqueryable */
	const DasJdo* pParams;      /* protocol "httpParams" or "http_params" */
	const DasJdo* pBaseUrls;    /* protocol "baseUrls" or "base_urls" array */
} DasStreamSrc;

/* Key grammar helpers, see "Keys" in source.h.  They live next to the
 * parser so that the spellings accepted on input and the spellings
 * written into keys cannot drift apart. */

/* Fold a section name as a user may type it to a section.  Case
 * insensitive.  Returns false if the word is not a section name. */
bool _das_src_secByName(const char* sName, das_src_sec_e* pSec);

/* The short spelling of a property name: "minimum" -> "min", "enable" ->
 * "enabled".  Names with no alias are returned as given. */
const char* _das_src_propName(const char* sProp);

/* True if two property names are the same after _das_src_propName() and
 * case folding. */
bool _das_src_propEq(const char* sA, const char* sB);

/* Split a dotted key into at most three parts.
 *
 * @param aParts Receives the parts, each NUL terminated.
 * @returns The number of parts, 1 to 3, or 0 if the key is empty, has an
 *        empty part, has more than three parts, or has a part that does
 *        not fit. */
int _das_src_splitKey(const char* sKey, char aParts[3][DAS_MAX_ID_BUFSZ]);

/* Find properties matching a key, the engine behind DasSrc_findProp()
 * and DasSrc_check().
 *
 * Tries the spellings from most to least specific: SEC.GROUP.PROP;
 * SEC.PROP for options or SEC.GROUP for a toggle; GROUP.PROP in any
 * section; a lone option name or group toggle; and last a bare property
 * name.  The first level that matches anything decides, so a lone name
 * that is both an option and a property reports the option.
 *
 * @param aOut Receives up to nMax matches.  May be NULL with nMax 0 to
 *        count only.
 * @returns The number of matches, which may exceed nMax.  0 is no such
 *        key, 1 a hit, more is ambiguous and aOut holds the candidates. */
int _DasSrc_match(const DasSrc* pThis, const char* sKey, const DasSrcProp** aOut, int nMax);

/* If sKey, with or without a section prefix, names a coordinate group,
 * return that group, else NULL.  How a check recognizes the range forms. */
const DasSrcGroup* _DasSrc_coordGroup(const DasSrc* pThis, const char* sKey);

#endif /* _das_src_priv_h_ */
