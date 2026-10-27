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

/* Requisition internals shared by req.c and source.c.  Not installed.
 *
 * source.c is the only writer of verdicts and derived entries, so those
 * operations are here rather than in req.h: an application has no
 * business validating a requisition.
 */

#ifndef _das_req_priv_h_
#define _das_req_priv_h_

#include "req.h"
#include "json.h"

struct das_req_entry {
	char* sKey;                 /* owned, as the client typed it */
	char* sVal;                 /* owned, never NULL, may be "" */
	das_req_verdict_e verdict;
	char* sWhy;                 /* owned, NULL unless bad */
	bool bDerived;
	int nProp;                  /* index of the source property the last check
	                               resolved this entry to, -1 if none.  Only
	                               meaningful to the source that set it. */
};

struct das_req_raw {
	char* sName;
	char* sVal;
};

struct das_req {
	char* sPath;                /* stamp, each "" when unknown */
	char* sUrl;
	char* sVersion;
	char* sLabel;

	struct das_req_entry* aEntry;
	size_t uEntry;
	size_t uEntryCap;

	struct das_req_raw* aRaw;
	size_t uRaw;
	size_t uRawCap;
};

/* Append an entry without looking for a duplicate.  Used for derived
 * entries, which are keyed by full key and rebuilt wholesale, so the
 * uniqueness DasReq_set() maintains for client keys is not needed.
 * Returns the new index, or -1 if memory ran out. */
int _DasReq_add(DasReq* pThis, const char* sKey, const char* sVal, bool bDerived);

/* Drop every derived entry.  Called at the start of a check so that a
 * second check never sees its own output. */
void _DasReq_dropDerived(DasReq* pThis);

/* Set the verdict of an entry, with a reason for a bad one (ignored
 * otherwise).  sWhy is copied. */
void _DasReq_setVerdict(DasReq* pThis, size_t uIdx, das_req_verdict_e verdict, const char* sWhy);

/* Mark every entry unchecked.  DasReq_set() does this for the one entry
 * it touches; a check that is about to validate everything again calls this. */
void _DasReq_clearVerdicts(DasReq* pThis);

/* Copy a JSON scalar as text: strings as they are, numbers in their
 * source notation, true and false as those words.  Returns false for
 * null, containers and missing values, leaving sBuf untouched.  The one
 * reader of JSON number text in the catalog code, since das_json keeps
 * numbers as unterminated text and a default of 86.4 should stay 86.4. */
bool _das_json_scalar(const DasJdo* pJdo, char* sBuf, size_t uLen);

#endif /* _das_req_priv_h_ */
