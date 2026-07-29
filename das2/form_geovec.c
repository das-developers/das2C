/* Copyright (C) 2026 Chris Piker <chris-piker@uiowa.edu>
 *
 * Author: C. Piker, via Claude Fable 5
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

/* The geometric-vector formalism row.  The pattern for per-formalism files:
 * define your das_form_kind (and later your das_form_rule entries), then add
 * one extern line per table in set.c.  Nothing else in the library changes.
 *
 * Wire bindings this row reads: frame= (context ref), system= (default
 * cartesian), sysorder= (storage order to canonical directions, default
 * ascending), surface= (context ref, detic/graphic only).
 *
 * The packed datum carries the NUMERIC frame and surface ids of the old
 * das_geovec, which are assigned by the stream context.  Token-to-id
 * resolution needs that context at hand, so it arrives with the header
 * reader; until then packed vectors carry id 0 (unknown) and the tokens
 * remain readable through DasSet_getFrame / das_formalism_getBind.
 */

#define _POSIX_C_SOURCE 200112L

#include <string.h>
#include <stdio.h>
#include <stdlib.h>

#include "util.h"
#include "vector.h"
#include "set.h"

static bool _geovec_pack(const DasSet* pBase, const ubyte* pRun, das_datum* pOut)
{
	const DasIntrSet* pThis = (const DasIntrSet*)pBase;

	if((pThis->nIntRank != 1)||(pThis->aIntShape[0] < 1)||
	   (pThis->aIntShape[0] > 3)){
		das_error(DASERR_VAR, "A geovec has 1 to 3 components in one level");
		return false;
	}
	ubyte nComp = (ubyte)pThis->aIntShape[0];

	const char* sSys = das_formalism_getBind(&(pBase->form), "system");
	if(sSys == NULL) sSys = "cartesian";
	ubyte uSysType = das_compsys_id(sSys);
	if(uSysType == 0){
		das_error(DASERR_VAR, "Unknown component system '%s'", sSys);
		return false;
	}

	/* sysorder "1;0;2": storage slot i holds canonical direction sOrd[2i] */
	ubyte dirs = VEC_DIRS3(0, 1, 2);
	const char* sOrd = das_formalism_getBind(&(pBase->form), "sysorder");
	if(sOrd != NULL){
		ubyte aDir[3] = {0, 1, 2};
		int iSlot = 0;
		for(const char* p = sOrd; (*p != '\0')&&(iSlot < 3); ++p){
			if((*p >= '0')&&(*p <= '2')){ aDir[iSlot] = (ubyte)(*p - '0'); ++iSlot; }
			else if(*p != ';'){
				das_error(DASERR_VAR, "Bad sysorder token '%s'", sOrd);
				return false;
			}
		}
		dirs = VEC_DIRS3(aDir[0], aDir[1], aDir[2]);
	}

	das_elem_type et = DasGen_elemType(pBase->pGen);

	/* numeric context ids, when the header reader resolved and bound them */
	ubyte uFrameId = 0, uSurfId = 0;
	const char* sId = das_formalism_getBind(&(pBase->form), "frameId");
	if(sId != NULL) uFrameId = (ubyte)atoi(sId);
	sId = das_formalism_getBind(&(pBase->form), "surfId");
	if(sId != NULL) uSurfId = (ubyte)atoi(sId);

	das_geovec vec;
	if(das_geovec_init(
		&vec, pRun, uFrameId, uSysType, uSurfId, (ubyte)et,
		(ubyte)das_vt_size((das_val_type)et), nComp, dirs
	) != DAS_OKAY)
		return false;

	memcpy(pOut, &vec, sizeof(das_geovec));
	pOut->vt    = vtGeoVec;
	pOut->vsize = sizeof(das_geovec);
	pOut->units = pBase->units;
	return true;
}

static char* _geovec_prnIntr(const DasSet* pThis, char* sBuf, int nLen)
{
	const char* sFrame = DasSet_getFrame(pThis);
	snprintf(sBuf, (size_t)nLen, " geovec(%s)", sFrame ? sFrame : "no frame");
	return sBuf;
}

const das_form_kind das_form_kind_geovec = {
	"geovec", etUnknown /* any numeric */, _geovec_pack, _geovec_prnIntr,
	true /* a dumb client may stack the components as lines */
};
