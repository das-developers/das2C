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

#define _POSIX_C_SOURCE 200112L

#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <assert.h>

#include "util.h"
#include "log.h"
#include "time.h"
#include "operator.h"
#include "dimension.h"
#include "property.h"
#include "dataset.h"
#include "stream.h"
#include "variable.h"
#include "var_priv.h"


/* Axis D used to live here: a static formalism table, per-row pack/prnIntr
   functions, and a sparse binop rule registry keyed on (op, left row, right
   row).  All of it is now form.h plus one file per formalism, dispatched
   through DasForm_VTbl rather than looked up.

   The rules were HARVESTED, not discarded:
     the linear ring (units via Units_canConvert / Units_multiply)  -> form_linear.c
     the affine rules (Units_interval, "time minus time is a span") -> form_point.c
     the geovec row (pack, sysorder, frame binding)                 -> form_vector.c

   The far-corner sketches that sat here for matrix and image went with them.
*/

/* ************************************************************************* */
/* Index print direction, a global the old variable layer owned.  Nothing inside
   das2C calls the setter, but das2dlm does (src/das2c.c), so this is live public
   API and not dead code.  The expression printers below read the flag. */
static bool g_bFastIdxLast = true;

void das_varindex_prndir(bool bFastLast)
{
	g_bFastIdxLast = bFastLast;
}




/* Shape range printer (declared in array.h, previously defined by the
   retired variable layer) */

char* das_shape_prnRng(
	ptrdiff_t* pShape, int nExtRank, int nShapeLen, char* sBuf, int nBufLen
){

	memset(sBuf, 0, nBufLen);  /* Insure null termination where ever I stop writing */
	
	int nUsed = 0;
	int i;
	for(i = 0; i < nExtRank; ++i)
		if(pShape[i] != VARIDX_UNUSED) ++nUsed;
	
	if(nUsed == 0) return sBuf;
	
	/* If don't have the minimum num of bytes to print the range don't do it */
	if(nBufLen < (3 + nUsed*6 + (nUsed - 1)*2)) return sBuf;
	
	char* pWrite = sBuf;
	strncpy(pWrite, " |", 3);  /* using 3 not 2 to make GCC shutup */
	nBufLen -= 2;
	pWrite += 2;
	
	char sEnd[32] = {'\0'};
	int nNeedLen = 0;
	bool bAnyWritten = false;
	
	i = 0;
	int iEnd = nExtRank;
	int iLetter = 0;
	if(!g_bFastIdxLast){
		i = nExtRank - 1;
		iEnd = -1;
	}
	
	while(i != iEnd){
		
		if(pShape[i] == VARIDX_UNUSED){ 
			nNeedLen = 4 + ( bAnyWritten ? 1 : 0);
			if(nBufLen < (nNeedLen + 1)){
				sBuf[0] = '\0'; return sBuf;
			}
			
			if(bAnyWritten)
				snprintf(pWrite, nBufLen - 1, ", %c:-", g_sIdxLower[iLetter]);
			else
				snprintf(pWrite, nBufLen - 1, " %c:-", g_sIdxLower[iLetter]);
		}
		else{
			if((pShape[i] == VARIDX_RAGGED)||(pShape[i] == VARIDX_BORROW)){
				sEnd[0] = '*'; sEnd[1] = '\0';
			}
			else{
				snprintf(sEnd, 31, "%zd", pShape[i]);
			}
		
			nNeedLen = 6 + strlen(sEnd) + ( bAnyWritten ? 1 : 0);
			if(nBufLen < (nNeedLen + 1)){ 
				/* If I've run out of room close off the string at the original 
				 * write point and exit */
				sBuf[0] = '\0';
				return sBuf;
			}
		
			if(bAnyWritten)
				snprintf(pWrite, nBufLen - 1, ", %c:0..%s", g_sIdxLower[iLetter], sEnd);
			else
				snprintf(pWrite, nBufLen - 1, " %c:0..%s", g_sIdxLower[iLetter], sEnd);
		}
		
		pWrite += nNeedLen;
		nBufLen -= nNeedLen;
		bAnyWritten = true;
		
		if(g_bFastIdxLast) ++i;
		else               --i;
		
		 ++iLetter;  /* always report in order of I,J,K */
	}
	
	return pWrite;
}

/* ************************************************************************* */
/* DasVar base                                                               */

/* The generator-backed classes share these; DasVarBin has its own, which is why
   the public entries below dispatch instead of doing the work.  Registering
   the public function in a vtable slot would recurse forever. */
static int DasVarGen_incRef(DasVar* pThis)
{
	pThis->nRef += 1;
	return pThis->nRef;
}

int inc_DasVar(DasVar* pThis)
{
	return pThis->pVTbl->incRef(pThis);
}

int dec_DasVar(DasVar* pThis)
{
	return pThis->pVTbl->decRef(pThis);
}

static int DasVarGen_decRef(DasVar* pThis)
{
	assert(pThis->nRef > 0);
	pThis->nRef -= 1;
	if(pThis->nRef == 0){
		DasGen_decRef(pThis->pGen);   /* NULL for an operation, and fine */

		/* Forms are refcounted and SHARED, never cloned: they are immutable
		   once built and validated, so a copy can point at the same one.  That
		   makes releasing here mandatory.  DasVarBin_decRef always did it and
		   the three generator-backed classes reach THIS function instead, so
		   every scalar, composite and byte run leaked a form until now. */
		del_DasForm(pThis->pForm);

		DasDesc_freeProps(&(pThis->base));
		free(pThis);
		return 0;
	}
	return pThis->nRef;
}

size_t _DasVar_itemBytes(const DasVar* pThis)
{
	ptrdiff_t aShape[VARIDX_MAX];
	int nRank = pThis->pVTbl->intrShape(pThis, aShape);

	size_t uElems = 1;
	for(int i = 0; i < nRank; ++i){
		if(aShape[i] < 1) return 0;   /* ragged: a location's property, not mine */
		uElems *= (size_t)aShape[i];
	}
	return uElems * das_vt_size((das_val_type)DasVar_elemType(pThis));
}

size_t _DasVar_runScratch(const DasVar* pThis)
{
	/* Storage to point at means nothing to compute into. */
	if(DasVar_getAry(pThis) != NULL) return 0;

	size_t uSelf = _DasVar_itemBytes(pThis);

	/* A sequence or constant computes its run and nothing else. */
	if(pThis->pGen != NULL) return uSelf;

	/* An operation also needs somewhere to put each operand's run, and an
	   operand may itself be an operation. */
	const DasVarBin* pBin = (const DasVarBin*)pThis;
	return uSelf + _DasVar_runScratch(pBin->pLeft)
	             + _DasVar_runScratch(pBin->pRight);
}

const ubyte* _DasVar_runAt(
	const DasVar* pThis, ptrdiff_t* pLoc, ubyte* pScratch, size_t uScratch,
	size_t* pBytes, size_t* pNeed
){
	*pNeed = 0;

	/* An operation has no leaf values; it walks its operands. */
	if(pThis->pGen == NULL)
		return _DasVarBin_runAt(pThis, pLoc, pScratch, uScratch, pBytes, pNeed);

	/* Lend the variable's own memory whenever there is any.  This is the
	   das2C bargain: zero copy on the common path, and the caller keeps the
	   run only as long as the variable. */
	size_t uCount = 0;
	const ubyte* pRun = DasGen_at(pThis->pGen, pLoc, &uCount);
	if(pRun != NULL){
		*pBytes = uCount * das_vt_size((das_val_type)DasVar_elemType(pThis));
		return pRun;
	}

	/* Computed: it has to land somewhere, and that somewhere is the caller's. */
	size_t uWant = _DasVar_itemBytes(pThis);
	if(uWant == 0){
		das_error(DASERR_NOTIMP,
			"A computed run with no fixed width cannot be sized in advance"
		);
		return NULL;
	}
	if((pScratch == NULL)||(uScratch < uWant)){ *pNeed = uWant; return NULL; }

	if(DasGen_eval(pThis->pGen, pLoc, pScratch, uScratch) < 1) return NULL;
	*pBytes = uWant;
	return pScratch;
}

int DasVar_get(
	const DasVar* pThis, ptrdiff_t* pLoc, das_byte_seq work, das_datum* pOut
){
	if((pThis == NULL)||(pOut == NULL))
		return -1 * das_error(DASERR_VAR, "Null pointer reading a value");

	return pThis->pVTbl->get(pThis, pLoc, work, pOut);
}

bool DasVar_getNeedsBuf(const DasVar* pThis)
{
	/* Two different questions share _DasVar_runScratch().  It answers "where
	   does a RUN land", and a computed scalar's run does need somewhere -- but
	   DasVar_get() lands that one in the datum's own bytes, so the CALLER
	   never sees it.  Only a run with an internal index can outgrow a datum. */
	ptrdiff_t aShape[VARIDX_MAX];
	if(pThis->pVTbl->intrShape(pThis, aShape) < 1) return false;

	return _DasVar_runScratch(pThis) > 0;
}

int DasVar_shape(const DasVar* pThis, ptrdiff_t* pShape)
{
	/* Every slot is set before the class writes its own, so a caller reading
	   past the returned rank sees "not used here" instead of whatever was in
	   its buffer.  Zero would be indistinguishable from a real extent of zero,
	   which is why the fill is UNUSED and not a memset. */
	for(int i = 0; i < VARIDX_MAX; ++i) pShape[i] = VARIDX_UNUSED;

	return pThis->pVTbl->shape(pThis, pShape);
}

ptrdiff_t DasVar_lengthIn(const DasVar* pThis, int nIdx, ptrdiff_t* pLoc)
{
	return pThis->pVTbl->lengthIn(pThis, nIdx, pLoc);
}

const char* DasVar_role(const DasVar* pThis)
{
	const DasDim* pDim = (const DasDim*)DasDesc_parent((const DasDesc*)pThis);

	/* No parent, no role.  A variable built on its own is perfectly good;
	   it just isn't playing a part in a dimension yet. */
	if(pDim == NULL) return NULL;

	for(size_t u = 0; u < pDim->uVars; ++u){
		if(pDim->aVars[u] != pThis) continue;

		if(pDim->aRoles[u][0] == '\0')
			break;   /* listed but unnamed: same broken state as not listed */

		return pDim->aRoles[u];
	}

	/* Having a dimension for a parent but no role in it cannot happen through
	   the public API: DasDim_addVar() stamps the parent pointer and the role
	   name together.  Reaching here means the two halves have come apart */
	das_error(DASERR_VAR,
		"Dimension '%s' is the parent of a variable it has no role for",
		DasDim_id(pDim)
	);
	return NULL;
}

/* ************************************************************************* */
/* Bulk reads.  Two independent axes -- lend vs own, natural vs squared off -- */
/* so four public entries over one body.                                      */

/* How far a walk actually runs at one level.  The asked-for bound is a
   CEILING, never a promise: a negative bound is the "all of it" sentinel a
   ragged index carries, and an oversized one is a qube pad target that no
   walker should try to read.  Reality wins in both directions, which is what
   keeps every walk below off the corners a ragged source never had. */
#define _RUN_END(nAsked, nHave)  \
   ((((nAsked) < 0)||((nAsked) > (nHave))) ? (nHave) : (nAsked))

/* The widest run along external index nIdx anywhere in [pMin, pMax).

   A rectangle has to be built from the largest run, and a ragged index has a
   different length at every parent position, so the only way to know is to
   walk.  This walks the SOURCE's real tree rather than a cube: each level is
   bounded by its own run length, so positions that never existed are never
   visited.  Answers VARIDX_BORROW if any level declines to answer at all,
   which is the caller's cue to demand a model. */
static ptrdiff_t _DasVar_maxLenAt(
	const DasVar* pThis, int nIdx, const ptrdiff_t* pMin, const ptrdiff_t* pMax,
	ptrdiff_t* pLoc, int iLevel
){
	if(iLevel == nIdx){
		/* Any flag rather than a count means this source will not say how far
		   it runs -- a borrowing sequence, or one that declared `*`.  Same
		   answer either way: somebody else has to supply the extent. */
		ptrdiff_t nLen = DasVar_lengthIn(pThis, nIdx, pLoc);
		return (nLen < 0) ? VARIDX_BORROW : nLen;
	}

	ptrdiff_t nHere = DasVar_lengthIn(pThis, iLevel, pLoc);
	if(nHere == VARIDX_BORROW) return VARIDX_BORROW;
	if(nHere == VARIDX_UNUSED) nHere = 1;  /* degenerate: exactly one position */
	if(nHere < 0) return VARIDX_BORROW;

	ptrdiff_t nEnd = _RUN_END(pMax[iLevel], nHere);
	ptrdiff_t nMax = 0;

	for(pLoc[iLevel] = pMin[iLevel]; pLoc[iLevel] < nEnd; ++pLoc[iLevel]){
		ptrdiff_t nSub = _DasVar_maxLenAt(pThis, nIdx, pMin, pMax, pLoc, iLevel + 1);
		if(nSub == VARIDX_BORROW) return VARIDX_BORROW;
		if(nSub > nMax) nMax = nSub;
	}

	pLoc[iLevel] = pMin[iLevel];  /* leave the odometer as it was found */
	return nMax;
}

static ptrdiff_t _DasVar_maxLenIn(
	const DasVar* pThis, int nIdx, const ptrdiff_t* pMin, const ptrdiff_t* pMax
){
	ptrdiff_t aLoc[VARIDX_MAX] = VARIDX_INIT_BEGIN;
	return _DasVar_maxLenAt(pThis, nIdx, pMin, pMax, aLoc, 0);
}

/* The widest item run in [pMin, pMax), counted in ELEMENTS.  Same walk one
   level deeper.  Only an array backed byte run can be ragged internally, so
   _DasVar_runAt() always has real memory to measure here and never reaches for
   the scratch arguments.  A negative answer means some position could not be
   measured, which is a corrupt source rather than a shape question. */
static ptrdiff_t _DasVar_maxItemAt(
	const DasVar* pThis, int nExtRank, const ptrdiff_t* pMin,
	const ptrdiff_t* pMax, ptrdiff_t* pLoc, int iLevel, size_t uElemSz
){
	if(iLevel == nExtRank){
		size_t uBytes = 0, uNeed = 0;
		if(_DasVar_runAt(pThis, pLoc, NULL, 0, &uBytes, &uNeed) == NULL) return -1;
		return (ptrdiff_t)(uBytes / uElemSz);
	}

	ptrdiff_t nHere = DasVar_lengthIn(pThis, iLevel, pLoc);
	if(nHere == VARIDX_UNUSED) nHere = 1;
	if(nHere < 0) return -1;

	ptrdiff_t nEnd = _RUN_END(pMax[iLevel], nHere);
	ptrdiff_t nMax = 0;

	for(pLoc[iLevel] = pMin[iLevel]; pLoc[iLevel] < nEnd; ++pLoc[iLevel]){
		ptrdiff_t nSub = _DasVar_maxItemAt(
			pThis, nExtRank, pMin, pMax, pLoc, iLevel + 1, uElemSz
		);
		if(nSub < 0) return -1;
		if(nSub > nMax) nMax = nSub;
	}

	pLoc[iLevel] = pMin[iLevel];
	return nMax;
}

/* The container's extents, for a variable that has a container.

   A dataset is a cohesive thing: a parented variable may reach up through its
   dimension and ask.  Not cheap -- DasDs_shape() answers by calling back DOWN
   into every variable and merging -- so ask once and keep it in the call
   frame.  Never cache it on the object; N threads may read one variable with
   no locking, so per-object scratch is not available. */
static const DasDs* _DasVar_container(const DasVar* pThis)
{
	const DasDesc* pDim = DasDesc_parent((const DasDesc*)pThis);
	if(pDim == NULL) return NULL;

	const DasDesc* pDs = DasDesc_parent(pDim);
	if((pDs == NULL)||(pDs->type != DATASET)) return NULL;

	return (const DasDs*)pDs;
}

/* The same widest-run measurement one level up.  Needed when the index is
   degenerate HERE: this variable has no length to report along it, but the
   container it repeats across does, and DasDs_lengthIn() is already the
   merged answer over every variable. */
static ptrdiff_t _DasDs_maxLenAt(
	const DasDs* pDs, int nIdx, ptrdiff_t* pLoc, int iLevel
){
	if(iLevel == nIdx) return DasDs_lengthIn(pDs, nIdx, pLoc);

	ptrdiff_t nHere = DasDs_lengthIn(pDs, iLevel, pLoc);
	if(nHere < 1) return -1;

	ptrdiff_t nMax = 0;
	for(pLoc[iLevel] = 0; pLoc[iLevel] < nHere; ++pLoc[iLevel]){
		ptrdiff_t nSub = _DasDs_maxLenAt(pDs, nIdx, pLoc, iLevel + 1);
		if(nSub < 0) return -1;
		if(nSub > nMax) nMax = nSub;
	}
	pLoc[iLevel] = 0;
	return nMax;
}

/* Is this location inside the variable's real extent?  A cube walk visits
   corners a ragged source never had; those are exactly the ones that get fill.
   A degenerate index is in range at any value -- the generator ignores it. */
static bool _DasVar_locValid(const DasVar* pThis, int nExtRank, ptrdiff_t* pLoc)
{
	for(int d = 0; d < nExtRank; ++d){
		ptrdiff_t nLen = DasVar_lengthIn(pThis, d, pLoc);
		if(nLen == VARIDX_UNUSED) continue;
		if(nLen < 0) return false;
		if(pLoc[d] >= nLen) return false;
	}
	return true;
}

/* Where each external index lands in the output, and whether it stays ragged.
   Pinned indices collapse and get -1, matching what das_rng2shape() does on
   the rectangular path -- the two shapes have to agree wherever they can. */
typedef struct das_out_map {
	int   aDim[VARIDX_MAX];    /* external index -> output index, or -1 */
	bool  aRagged[VARIDX_MAX]; /* by OUTPUT index */
	int   nRank;
	int   iItemDim;            /* output index holding the item run, or -1 */
} das_out_map;

/* The natural copy.  Visits only positions the source really has, bounded
   level by level by its own run lengths, and appends each run in order -- so
   the destination comes out with the input's structure rather than a
   rectangle drawn around it.

   markEnd closes a ragged level once its children are done.  Fixed extents
   need no mark: the array rolls a full parent by itself (array.c
   _newIndexInfo), which is the same rule the codec's run walkers follow. */
static int _DasVar_appendRuns(
	const DasVar* pThis, const DasVar* pSrc, int nExtRank, const ptrdiff_t* pMin,
	const ptrdiff_t* pMax, ptrdiff_t* pLoc, int iLevel, DasAry* pOut,
	size_t uElemSz, const das_out_map* pMap, ubyte* pScratch, size_t uScratch
){
	if(iLevel == nExtRank){
		size_t uBytes = 0, uNeed = 0;
		const ubyte* pRun = _DasVar_runAt(
			pThis, pLoc, pScratch, uScratch, &uBytes, &uNeed
		);
		if(pRun == NULL)
			return -1 * das_error(DASERR_VAR,
				"Could not read the run at this location while copying a subset"
			);

		if(DasAry_append(pOut, pRun, uBytes / uElemSz) == NULL) return -1;

		/* Only a variable width item needs telling where it stopped. */
		if((pMap->iItemDim > 0)&&(pMap->aRagged[pMap->iItemDim]))
			DasAry_markEnd(pOut, pMap->iItemDim);
		return 0;
	}

	/* Bounds come from pSrc, values from pThis.  That split is the whole point
	   of a shape model: a reference-plus-offset time has no extent of its own
	   along the sample index, but the waveform it belongs to does, and the
	   caller knows which variable that is. */
	ptrdiff_t nHere = DasVar_lengthIn(pSrc, iLevel, pLoc);
	if(nHere == VARIDX_UNUSED) nHere = 1;
	if(nHere < 0)
		return -1 * das_error(DASERR_VAR,
			"Index %d cannot report its own length, so it cannot be walked", iLevel
		);

	ptrdiff_t nEnd = _RUN_END(pMax[iLevel], nHere);

	for(pLoc[iLevel] = pMin[iLevel]; pLoc[iLevel] < nEnd; ++pLoc[iLevel]){
		if(_DasVar_appendRuns(pThis, pSrc, nExtRank, pMin, pMax, pLoc, iLevel + 1,
		                      pOut, uElemSz, pMap, pScratch, uScratch) != 0)
			return -1;
	}
	pLoc[iLevel] = pMin[iLevel];

	/* Index 0 is the growth index and is never marked; marking it would roll
	   the whole array back to the start. */
	int iOut = pMap->aDim[iLevel];
	if((iOut > 0)&&(pMap->aRagged[iOut])) DasAry_markEnd(pOut, iOut);

	return 0;
}

/* The Qube copy for a ragged ITEM run.  Unlike the natural walk this one
   iterates the DESTINATION cube, because every cell has to be written --
   including the corners the source never had.  Validity is decided from the
   source's own run lengths rather than DasAry_validAt(), so a computed
   variable walks the same code as an array backed one. */
static int _DasVar_qubeRuns(
	const DasVar* pThis, int nExtRank, const ptrdiff_t* pMin,
	const ptrdiff_t* pMax, ptrdiff_t* pLoc, int iLevel, ubyte** ppWrite,
	size_t* pRemain, size_t uElemSz, size_t uItemMax, const ubyte* pFill
){
	if(iLevel == nExtRank){
		size_t uWant = uItemMax * uElemSz;
		if(*pRemain < uWant)
			return -1 * das_error(DASERR_VAR, "Squared off buffer ran short");

		size_t uBytes = 0, uNeed = 0;
		const ubyte* pRun = NULL;

		if(_DasVar_locValid(pThis, nExtRank, pLoc))
			pRun = _DasVar_runAt(pThis, pLoc, NULL, 0, &uBytes, &uNeed);

		if(pRun != NULL){
			if(uBytes > uWant) uBytes = uWant;   /* cannot happen; cheap to pin */
			memcpy(*ppWrite, pRun, uBytes);
		}
		else
			uBytes = 0;   /* no run here at all: the whole cell is pad */

		/* Pad by ELEMENT, not by byte -- fill is one element wide and das_memset
		   repeats it, which is also how a multi byte fill would work. */
		size_t uPad = (uWant - uBytes) / uElemSz;
		if(uPad > 0) das_memset(*ppWrite + uBytes, pFill, uElemSz, uPad);

		*ppWrite += uWant;
		*pRemain -= uWant;
		return 0;
	}

	for(pLoc[iLevel] = pMin[iLevel]; pLoc[iLevel] < pMax[iLevel]; ++pLoc[iLevel]){
		if(_DasVar_qubeRuns(pThis, nExtRank, pMin, pMax, pLoc, iLevel + 1,
		                    ppWrite, pRemain, uElemSz, uItemMax, pFill) != 0)
			return -1;
	}
	pLoc[iLevel] = pMin[iLevel];
	return 0;
}

/* Shared body for the four public subset entries.  The division of labor:
   the VARIABLE owns rank agreement, slice geometry, the rank-0 refusal, naming,
   units and the element-vs-presentation decision; the GENERATOR owns whether
   a view is possible and how bytes are produced. */
static DasAry* _DasVar_subset(
	const DasVar* pThis, int nRank, const ptrdiff_t* pMin, const ptrdiff_t* pMax,
	const DasVar* pShapeFrom, bool bMustCopy, bool bQube
){
	ptrdiff_t aSetShape[VARIDX_MAX];
	int nExtRank = DasVar_shape(pThis, aSetShape);

	if(nRank != nExtRank){
		das_error(DASERR_VAR,
			"Variable is external rank %d, but the subset specification is rank %d",
			nExtRank, nRank
		);
		return NULL;
	}

	/* Whose extents are we wearing?  The caller's model when there is one --
	   the caller knows which variable actually spans the dataset, and saying so
	   is cheaper than making every variable rediscover it.  Otherwise our own.
	   Everything below asks pSrc for SHAPE and pThis for VALUES. */
	const DasVar* pSrc = (pShapeFrom != NULL) ? pShapeFrom : pThis;
	ptrdiff_t aSrcShape[VARIDX_MAX];
	int nSrcRank = DasVar_shape(pSrc, aSrcShape);
	if((pShapeFrom != NULL)&&(nSrcRank != nExtRank)){
		das_error(DASERR_VAR,
			"Shape model is external rank %d, but this variable is rank %d",
			nSrcRank, nExtRank
		);
		return NULL;
	}

	/* Resolve the bounds.  A NULL pair means "everything", which the variable
	   answers for itself unless a model was named.  An index this variable does
	   not use collapses to one element; that is per-VARIABLE knowledge an array
	   cannot supply, which is why a model is a DasVar and not a DasAry. */
	ptrdiff_t aMin[VARIDX_MAX], aMax[VARIDX_MAX];
	if((pMin == NULL)||(pMax == NULL)){

		/* A degenerate index is not absent: the variable repeats along it with a
		   step size of 0 (das3/variable.md, "Degeneracy alters the step size"),
		   which is exactly what lets every variable in a dataset answer at every
		   position.  So its extent is the CONTAINER's, and collapsing it is the
		   caller's move -- spelled as a restricted pMin/pMax, the way das3_cdf
		   does it.  Reach up for that only when no model was named and only
		   once, since the answer costs a walk of every variable. */
		ptrdiff_t aConShape[VARIDX_MAX];
		int nConRank = -1;
		const DasDs* pCon = NULL;
		if(pShapeFrom == NULL){
			for(int i = 0; i < nExtRank; ++i){
				if(aSetShape[i] != VARIDX_UNUSED) continue;
				if((pCon = _DasVar_container(pThis)) != NULL)
					nConRank = DasDs_shape(pCon, aConShape);
				break;
			}
		}

		for(int i = 0; i < nExtRank; ++i){
			aMin[i] = 0;

			if(aSetShape[i] == VARIDX_UNUSED){
				ptrdiff_t nHave = 1;      /* unparented and unmodelled: all there is */
				if(pShapeFrom != NULL)         nHave = aSrcShape[i];
				else if(nConRank == nExtRank)  nHave = aConShape[i];
				if(nHave == VARIDX_UNUSED) nHave = 1;
				if(nHave < 1) nHave = VARIDX_RAGGED;   /* settled like any other */
				aMax[i] = nHave;
				continue;
			}

			ptrdiff_t nLen = aSrcShape[i];
			if(nLen == VARIDX_UNUSED) nLen = 1;

			/* BORROW is the one extent nobody present can supply.  RAGGED is
			   different in kind -- the source knows, one position at a time --
			   so it stays as the "all of it" sentinel and the walkers clip
			   against reality. */
			if(nLen == VARIDX_BORROW){
				das_error(DASERR_VAR,
					"Index %d has no settled extent (borrowed).  Name a range, or "
					"pass a variable whose shape to wear as pShapeFrom", i
				);
				return NULL;
			}
			if(nLen < 1) nLen = VARIDX_RAGGED;

			aMax[i] = nLen;
		}

		/* A rectangle cannot be built out of a sentinel, so a Qube resolves each
		   ragged index to the widest run in range.  Left to right, because a
		   deeper walk clips against the bounds already settled above it. */
		if(bQube){

			/* Measure over the SOURCE's whole extent, not this variable's.  A
			   degenerate index collapses in the OUTPUT, but collapsing it here
			   would walk one record, report the widest run inside it, and call
			   that the answer for the whole dataset. */
			ptrdiff_t aWalkMin[VARIDX_MAX], aWalkMax[VARIDX_MAX];
			for(int i = 0; i < nExtRank; ++i){
				aWalkMin[i] = 0;
				aWalkMax[i] = (aSrcShape[i] == VARIDX_UNUSED) ? 1 : aSrcShape[i];
				if(aWalkMax[i] < 0) aWalkMax[i] = VARIDX_RAGGED;   /* all of it */
			}

			for(int i = 0; i < nExtRank; ++i){
				if(aMax[i] != VARIDX_RAGGED) continue;

				/* An index this variable is degenerate along has no length HERE
				   to measure; the container owns that question. */
				ptrdiff_t nWide;
				if((aSetShape[i] == VARIDX_UNUSED)&&(pCon != NULL)){
					ptrdiff_t aConLoc[VARIDX_MAX] = VARIDX_INIT_BEGIN;
					nWide = _DasDs_maxLenAt(pCon, i, aConLoc, 0);
				}
				else
					nWide = _DasVar_maxLenIn(pSrc, i, aWalkMin, aWalkMax);
				if(nWide < 1){
					das_error(DASERR_VAR,
						"Index %d has no settled extent and %s cannot measure one.  "
						"Name a range, or pass a variable whose shape to wear as "
						"pShapeFrom", i,
						(pShapeFrom != NULL) ? "the shape model" : "this variable"
					);
					return NULL;
				}
				aMax[i] = nWide;
			}
		}

		pMin = aMin;
		pMax = aMax;
	}

	/* Values come out as ELEMENTS.  A composite's components ride as trailing
	   array indices, which is how the backing storage already holds them;
	   Expanding each one to portable das_datum format would have huge
	   performance penalties in both CPU and RAM. */
	das_val_type vtEl  = (das_val_type)DasVar_elemType(pThis);
	size_t uElemSz     = das_vt_size(vtEl);
	size_t uItemElems  = pThis->pVTbl->itemElems(pThis);
	size_t aSliceShape[VARIDX_MAX] = VARIDX_INIT_BEGIN;

	/* Where the two shape answers actually diverge.  With nothing ragged in
	   range there is nothing to square off, so natural and Qube are the same
	   request over the same bytes and both take the rectangular path below. */
	bool bRaggedItem = (uItemElems == 0);
	bool bRaggedExt  = false;
	{
		/* Ragged means the extent VARIES from one position to the next, which
		   only a real store can do.  A sequence declaring `*` means "as far as
		   you like" and answers everywhere in range, so a named bound is simply
		   obeyed -- asking it for a length is how the two are told apart. */
		ptrdiff_t aWhere[VARIDX_MAX] = VARIDX_INIT_BEGIN;
		for(int d = 0; d < nRank; ++d) aWhere[d] = pMin[d];

		for(int d = 0; d < nRank; ++d){
			if(aSrcShape[d] != VARIDX_RAGGED)  continue;
			if((pMax[d] - pMin[d]) == 1)       continue;  /* pinned, nothing varies */
			if(DasVar_lengthIn(pSrc, d, aWhere) < 0) continue;
			bRaggedExt = true;
		}
	}

	/* Name and fill both come from the backing store when there is one.  A
	   computed variable has none, and a NULL fill is a complete answer:
	   new_DasAry substitutes the type default. */
	DasAry* pAry = DasVar_getAry(pThis);
	char sName[DAS_MAX_ID_BUFSZ] = {'\0'};
	snprintf(sName, DAS_MAX_ID_BUFSZ - 1, "%s_subset",
		(pAry != NULL) ? DasAry_id(pAry) : "variable"
	);
	const ubyte* pFill = (pAry != NULL) ? DasAry_getFill(pAry) : NULL;

	/* Fast path first, when the caller can live with shared storage.  A NULL
	   answer just means "not applicable here", so nothing is riding on it.

	   A Qube declines the offer over ragged storage.  The view would be the
	   backing array itself, ragged extents and all, which is precisely the
	   shape this call exists in order not to return. */
	if(!bMustCopy && !(bQube && (bRaggedExt || bRaggedItem))){
		DasAry* pView = pThis->pVTbl->subsetView(pThis, nRank, pMin, pMax);
		if(pView != NULL) return pView;
	}

	/* ---- natural shape, something ragged: copy the structure as it is ---- */
	if(!bQube && (bRaggedExt || bRaggedItem)){

		das_out_map map;
		memset(&map, 0, sizeof(map));
		map.iItemDim = -1;

		for(int d = 0; d < nRank; ++d){
			bool bRag = (aSrcShape[d] == VARIDX_RAGGED);
			ptrdiff_t nSpan = bRag ? VARIDX_RAGGED : (pMax[d] - pMin[d]);

			if(!bRag && (nSpan == 1)){ map.aDim[d] = -1; continue; }

			map.aDim[d]              = map.nRank;
			map.aRagged[map.nRank]   = bRag;
			aSliceShape[map.nRank]   = bRag ? 0 : (size_t)nSpan;
			++map.nRank;
		}

		/* An item run wider than one element needs a home in the output shape.
		   A ragged one is unbound there; a fixed one keeps its width and the
		   array rolls it without being told. */
		if(bRaggedItem || (uItemElems > 1)){
			if(map.nRank >= VARIDX_MAX){
				das_error(DASERR_VAR, "Subset rank %d leaves no room for the "
					"item index", map.nRank);
				return NULL;
			}
			map.iItemDim            = map.nRank;
			map.aRagged[map.nRank]  = bRaggedItem;
			aSliceShape[map.nRank]  = bRaggedItem ? 0 : uItemElems;
			++map.nRank;
		}

		if(map.nRank == 0){
			das_error(DASERR_VAR,
				"Can't output a rank 0 array, use DasVar_get() for single items"
			);
			return NULL;
		}

		/* Index 0 grows as runs arrive, whatever the source declared there. */
		aSliceShape[0] = 0;

		DasAry* pOut = new_DasAry(
			sName, vtEl, uElemSz, pFill, map.nRank, aSliceShape, pThis->units
		);
		if(pOut == NULL) return NULL;

		if(pAry != NULL) DasAry_setUsage(pOut, DasAry_getUsage(pAry));

		/* Only a computed run needs somewhere to land; an array backed one is
		   lent in place and never touches this. */
		size_t uScratch = _DasVar_runScratch(pThis);
		ubyte* pScratch = (uScratch > 0) ? (ubyte*)calloc(uScratch, 1) : NULL;
		if((uScratch > 0)&&(pScratch == NULL)){ dec_DasAry(pOut); return NULL; }

		ptrdiff_t aLoc[VARIDX_MAX] = VARIDX_INIT_BEGIN;
		for(int d = 0; d < nRank; ++d) aLoc[d] = pMin[d];

		int nRet = _DasVar_appendRuns(
			pThis, pSrc, nRank, pMin, pMax, aLoc, 0, pOut, uElemSz, &map,
			pScratch, uScratch
		);
		if(pScratch != NULL) free(pScratch);
		if(nRet != 0){ dec_DasAry(pOut); return NULL; }

		return pOut;
	}

	/* ---- from here down the destination is a rectangle ---- */

	/* A bound can still be the ragged sentinel here only when nothing could
	   measure it: the shape source declared `*` but answers with a flag rather
	   than a count, so there is no length to walk and none to pad to.  Say
	   that, rather than letting das_rng2shape report a range of 0 to -1. */
	for(int d = 0; d < nRank; ++d){
		if(pMax[d] >= 0) continue;
		das_error(DASERR_VAR,
			"Index %d is ragged but nothing can report a length along it.  Name "
			"a range, or pass a variable that spans the dataset as pShapeFrom", d
		);
		return NULL;
	}

	int nSliceRank = das_rng2shape(nRank, pMin, pMax, aSliceShape);
	if(nSliceRank < 0) return NULL;
	if(nSliceRank == 0){
		das_error(DASERR_VAR,
			"Can't output a rank 0 array, use DasVar_get() for single items"
		);
		return NULL;
	}

	/* ---- Qube of a ragged item: measure the widest, then pad to it ---- */
	if(bRaggedItem){

		/* Padding is only honest where the pad byte means "not data".  A fill
		   terminated run says so by construction; a bare blob has no byte left
		   over to say it with, and its per item length would not survive. */
		if((pAry == NULL)||((DasAry_getUsage(pAry) & D2ARY_FILL_TERM) != D2ARY_FILL_TERM)){
			das_error(DASERR_NOTIMP,
				"Variable width runs in '%s' are not fill terminated, so squaring "
				"them off would destroy each item's length with no way to tell pad "
				"from data.  Read them with DasVar_subset() or one at a time with "
				"DasVar_get()", (pAry != NULL) ? DasAry_id(pAry) : "this variable"
			);
			return NULL;
		}
		if(pFill == NULL){
			das_error(DASERR_VAR,
				"Array '%s' has no fill value, so its ragged runs cannot be "
				"squared off", DasAry_id(pAry)
			);
			return NULL;
		}

		ptrdiff_t aLoc[VARIDX_MAX] = VARIDX_INIT_BEGIN;
		for(int d = 0; d < nRank; ++d) aLoc[d] = pMin[d];

		ptrdiff_t nItemMax = _DasVar_maxItemAt(
			pThis, nRank, pMin, pMax, aLoc, 0, uElemSz
		);
		if(nItemMax < 1){
			das_error(DASERR_VAR,
				"No readable item run in the requested range of '%s'", sName
			);
			return NULL;
		}

		if(nSliceRank >= VARIDX_MAX){
			das_error(DASERR_VAR, "Subset rank %d leaves no room for the "
				"item index", nSliceRank);
			return NULL;
		}
		aSliceShape[nSliceRank] = (size_t)nItemMax;
		++nSliceRank;

		DasAry* pOut = new_DasAry(
			sName, vtEl, uElemSz, pFill, nSliceRank, aSliceShape, pThis->units
		);
		if(pOut == NULL) return NULL;
		DasAry_setUsage(pOut, DasAry_getUsage(pAry));

		size_t uBufElems = 0;
		ubyte* pBuf = DasAry_getBuf(pOut, vtEl, DIM0, &uBufElems);
		if(pBuf == NULL){ dec_DasAry(pOut); return NULL; }

		size_t uRemain = uBufElems * uElemSz;
		ubyte* pWrite  = pBuf;
		for(int d = 0; d < nRank; ++d) aLoc[d] = pMin[d];

		if(_DasVar_qubeRuns(pThis, nRank, pMin, pMax, aLoc, 0, &pWrite, &uRemain,
		                    uElemSz, (size_t)nItemMax, pFill) != 0){
			dec_DasAry(pOut);
			return NULL;
		}

		return pOut;
	}

	/* ---- square source, square destination: one buffer and a stride walk --- */

	/* An item run wider than one element needs a home in the output shape,
	   so it becomes one more (trailing, fixed) index. */
	if(uItemElems > 1){
		if(nSliceRank >= VARIDX_MAX){
			das_error(DASERR_VAR, "Subset rank %d leaves no room for the "
				"component index", nSliceRank);
			return NULL;
		}
		aSliceShape[nSliceRank] = uItemElems;
		++nSliceRank;
	}

	DasAry* pOut = new_DasAry(
		sName, vtEl, uElemSz, pFill, nSliceRank, aSliceShape, pThis->units
	);
	if(pOut == NULL) return NULL;

	/* getBuf counts ELEMENTS; the class writes bytes. */
	size_t uBufElems = 0;
	ubyte* pBuf = DasAry_getBuf(pOut, vtEl, DIM0, &uBufElems);
	if(pBuf == NULL){ dec_DasAry(pOut); return NULL; }
	size_t uBufLen = uBufElems * uElemSz;

	if(pThis->pVTbl->subsetInto(pThis, nRank, pMin, pMax, pBuf, uBufLen) < 0){
		dec_DasAry(pOut);
		return NULL;
	}

	return pOut;
}

DasAry* DasVar_subset(
	const DasVar* pThis, int nRank, const ptrdiff_t* pMin, const ptrdiff_t* pMax,
	const DasVar* pShapeFrom
){
	return _DasVar_subset(pThis, nRank, pMin, pMax, pShapeFrom, false, false);
}

DasAry* DasVar_materialize(
	const DasVar* pThis, int nRank, const ptrdiff_t* pMin, const ptrdiff_t* pMax,
	const DasVar* pShapeFrom
){
	return _DasVar_subset(pThis, nRank, pMin, pMax, pShapeFrom, true, false);
}

DasAry* DasVar_subsetQube(
	const DasVar* pThis, int nRank, const ptrdiff_t* pMin, const ptrdiff_t* pMax,
	const DasVar* pShapeFrom
){
	return _DasVar_subset(pThis, nRank, pMin, pMax, pShapeFrom, false, true);
}

DasAry* DasVar_materializeQube(
	const DasVar* pThis, int nRank, const ptrdiff_t* pMin, const ptrdiff_t* pMax,
	const DasVar* pShapeFrom
){
	return _DasVar_subset(pThis, nRank, pMin, pMax, pShapeFrom, true, true);
}


bool DasVar_degenerate(const DasVar* pThis, int iIndex)
{
	ptrdiff_t aShape[VARIDX_MAX];
	int nRank = DasVar_shape(pThis, aShape);
	if((iIndex < 0)||(iIndex >= nRank)) return true;
	return aShape[iIndex] == VARIDX_UNUSED;
}

int DasVar_intrShape(const DasVar* pThis, ptrdiff_t* pShape)
{
	return pThis->pVTbl->intrShape(pThis, pShape);
}

const char* DasVar_compSym(const DasVar* pThis, int iComp)
{
	if(pThis == NULL) return NULL;

	/* A convenience only.  The form is this variable's own and was handed the
	   intern= shape at construction, so there is nothing left for the variable
	   to tell it. */
	return DasForm_compSym(pThis->pForm, iComp);
}

bool DasVar_isNumeric(const DasVar* pThis)
{
	return pThis->pVTbl->isNumeric(pThis);
}

char* DasVar_toStr(const DasVar* pThis, char* sBuf, int nLen)
{
	return pThis->pVTbl->expression(pThis, sBuf, nLen, 0);
}

DasVar* DasVar_copy(const DasVar* pThis)
{
	return pThis->pVTbl->copy(pThis);
}

const char* DasVar_element(const DasVar* pThis)
{
	return pThis->pVTbl->element(pThis);
}

das_elem_type DasVar_elemType(const DasVar* pThis)
{
	if(pThis == NULL) return etUnknown;
	return pThis->pVTbl->elemType(pThis);
}

DasAry* DasVar_getAry(const DasVar* pThis)
{
	/* A DasVarBin carries no generator at all, so the NULL test is load
	   bearing and not defensive noise. */
	if((pThis == NULL)||(pThis->pGen == NULL)) return NULL;
	return DasGen_getArray(pThis->pGen);
}

bool DasVar_setAry(DasVar* pThis, DasAry* pNew)
{
	if(!DasGen_setArray(pThis->pGen, pNew))
		return false;
	pThis->units = DasAry_units(pNew);
	return true;
}

das_val_type DasVar_valType(const DasVar* pThis)
{
	/* A byte run is the only formless class -- its CLASS says it carries no
	   math -- and the sentinel is what tells a string from a blob. */
	if(pThis->pForm == NULL){
		return ((const DasVarBytes*)pThis)->bSentinel ? vtText : vtByteSeq;
	}

	/* Otherwise the FORM names its own datum type.  This is what replaced the
	   presentation enum: one authority that each formalism answers for itself,
	   instead of a switch here that had to grow an arm per formalism and be
	   kept in agreement with the token it switched on. */
	das_val_type vt = pThis->pForm->pVTbl->datumType(pThis->pForm);
	if(vt != vtUnknown) return vt;

	/* vtUnknown means "no datum type of my own", so fall back to storage. */
	return (das_val_type)DasGen_elemType(pThis->pGen);
}

/* DasVar_vecMap() and das_makeCompLabels() were RETIRED here, not moved.
   Both were geovec knowledge in the generic variable layer, and answering
   them from here would mean variable.c including form_vector.h -- library code
   learning one specific formalism.

   The replacement is DasVar_compSym(), which hands out the symbol for one
   storage slot and lets the caller compose whatever label it wants.  das3_csv
   and das3_cdf are the two consumers. */

/* ************************************************************************* */
/* The scalar case: a bare DasVar, no internal index                         */

/* A scalar never needs the caller's scratch: one value fits the datum's own
   bytes, which is what DATUM_BUF_SZ is sized for (a das_time is the largest). */
static int DasVarScalar_get(
	const DasVar* pThis, ptrdiff_t* pLoc, das_byte_seq work, das_datum* pOut
){
	(void)work;

	ubyte aBuf[DATUM_BUF_SZ];
	int nRet = DasGen_eval(pThis->pGen, pLoc, aBuf, sizeof(aBuf));
	if(nRet != 1) return -1 * DASERR_VAR;

	/* The form packs, because it is the only thing that knows what the bits
	   mean.  A form with no pack slot has no single-datum representation,
	   which is an answer rather than a failure. */
	if((pThis->pForm != NULL)&&(pThis->pForm->pVTbl->pack != NULL)){
		das_operand op;
		if(!_DasVar_operand(pThis, &op)) return -1 * DASERR_VAR;
		if(pThis->pForm->pVTbl->pack(pThis->pForm, &op, aBuf, pOut))
			return 0;
	}

	das_datum_init(
		pOut, aBuf, (das_val_type)DasGen_elemType(pThis->pGen), 0, pThis->units
	);
	return 0;
}

static const char* DasVarScalar_element(const DasVar* pThis)
{
	(void)pThis;
	return "scalar";
}

static das_elem_type DasVarScalar_elemType(const DasVar* pThis)
{
	return DasGen_elemType(pThis->pGen);
}

static int DasVarScalar_shape(const DasVar* pThis, ptrdiff_t* pShape)
{
	return DasGen_extShape(pThis->pGen, pShape);
}

static int DasVarScalar_intrShape(const DasVar* pThis, ptrdiff_t* pShape)
{
	(void)pThis; (void)pShape;
	return 0;                           /* a scalar has no internal index */
}

static char* DasVarScalar_expression(
	const DasVar* pThis, char* sBuf, int nLen, unsigned int uFlags
){
	(void)uFlags;
	int n = snprintf(sBuf, (size_t)nLen, "scalar(%s%s%s)",
		pThis->units ? pThis->units : "",
		(pThis->pForm != NULL) ? " " : "",
		(pThis->pForm != NULL) ? DasForm_kindStr(pThis->pForm) : ""
	);
	(void)n;
	return sBuf;
}

static bool DasVarScalar_isNumeric(const DasVar* pThis)
{
	das_elem_type et = DasGen_elemType(pThis->pGen);
	return (et != etUnknown)&&(et != etUByte);
}

static DasVar* DasVarScalar_copy(const DasVar* pThis);

/* The generator-backed answers to the two slots a computed variable has to
   override.  Every class built on a DasGen shares these; var_bin.c supplies its
   own,
   which is the whole reason these are vtable slots and not shared code. */
static ptrdiff_t DasVarGen_lengthIn(
	const DasVar* pThis, int nIdx, ptrdiff_t* pLoc
){
	return DasGen_lengthIn(pThis->pGen, nIdx, pLoc);
}

static int _DasVarGen_subsetInto(
	const DasVar* pThis, int nExtRank, const ptrdiff_t* pMin,
	const ptrdiff_t* pMax, ubyte* pBuf, size_t uBufLen
){
	return DasGen_subsetInto(pThis->pGen, nExtRank, pMin, pMax, pBuf, uBufLen);
}

static DasAry* _DasVarGen_subsetView(
	const DasVar* pThis, int nExtRank, const ptrdiff_t* pMin, const ptrdiff_t* pMax
){
	return DasGen_subsetView(pThis->pGen, nExtRank, pMin, pMax);
}

static size_t _DasVarGen_itemElems(const DasVar* pThis)
{
	return DasGen_itemElems(pThis->pGen);
}

/* Designated initializers throughout: a positional table silently mis-wires
   every slot after an insertion, and this one now has twelve. */
static const DasVar_VTbl g_vtblVarScalar = {
	.get        = DasVarScalar_get,
	.element    = DasVarScalar_element,
	.elemType   = DasVarScalar_elemType,
	.shape      = DasVarScalar_shape,
	.intrShape  = DasVarScalar_intrShape,
	.lengthIn   = DasVarGen_lengthIn,
	.itemElems  = _DasVarGen_itemElems,
	.subsetView = _DasVarGen_subsetView,
	.subsetInto = _DasVarGen_subsetInto,
	.expression = DasVarScalar_expression,
	.isNumeric  = DasVarScalar_isNumeric,
	.incRef     = DasVarGen_incRef,
	.decRef     = DasVarGen_decRef,
	.copy       = DasVarScalar_copy
};

static DasVar* DasVarScalar_copy(const DasVar* pThis)
{
	DasVar* pOut = (DasVar*)calloc(1, sizeof(DasVar));
	*pOut = *pThis;
	/* the struct copy dragged the source's property array along shallowly;
	   zero it so DasDesc_init builds fresh storage instead of inheriting
	   pointers it does not own */
	memset(&(pOut->base), 0, sizeof(DasDesc));
	DasDesc_init(&(pOut->base), VARIABLE);
	DasDesc_copyIn(&(pOut->base), &(pThis->base));
	pOut->nRef = 1;
	/* the GENERATOR is cloned so a later setArray on one owner cannot
	   re-aim the other; only the backing array is shared */
	if(pOut->pGen != NULL)
		pOut->pGen = DasGen_copy(pThis->pGen);

	/* The struct copy above carried pForm across SHALLOWLY, which is the one
	   place two variables could end up on one form.  Give the copy its own. */
	pOut->pForm = DasForm_copy(pOut->pForm);
	return pOut;
}

/* ************************************************************************* */
/* Serialization                                                             */

static char* _var_seqValToStr(
	const ubyte* pVal, das_elem_type et, char* sBuf, int nLen
){
	switch(et){
	case etTime:
		dt_isoc(sBuf, (size_t)nLen, (const das_time*)pVal, 6);
		return sBuf;
	case etFloat:
		snprintf(sBuf, (size_t)nLen, "%.7g", (double)(*(const float*)pVal));
		return sBuf;
	case etDouble:
		snprintf(sBuf, (size_t)nLen, "%.15g", *(const double*)pVal);
		return sBuf;
	default: {
		das_datum dm;
		das_datum_init(&dm, pVal, (das_val_type)et, 0, NULL);
		das_datum_toStrValOnly(&dm, sBuf, nLen, 6);
		return sBuf;
	}
	}
}

DasErrCode DasVar_encode(DasVar* pThis, const char* sRole, DasBuf* pBuf)
{
	const DasDim* pDim = (const DasDim*) ((DasDesc*)pThis)->parent;
	const DasDs*  pDs  = (const DasDs*) ((DasDesc*)pDim)->parent;

	das_elem_type et = DasGen_elemType(pThis->pGen);
	das_gen_type  gt = DasGen_type(pThis->pGen);
	das_units units  = (pThis->units != NULL) ? pThis->units : UNIT_DIMENSIONLESS;

	/* Structure comes from the class, so the element name IS the classification;
	   nothing here derives structure back out of a presentation vocabulary. */
	const char* sElement = DasVar_element(pThis);
	bool bScalar  = (strcmp(sElement, "scalar") == 0);
	bool bByteRun = (strcmp(sElement, "bytes")  == 0);

	/* 1. index= from the generator's own declared shape.  A sequence reports
	   the extents it DECLARED, borrow marks included; that is the point of
	   asking the generator rather than the merged dataset shape. */
	ptrdiff_t aExtShape[VARIDX_MAX] = VARIDX_INIT_UNUSED;
	int nExtRank = DasGen_extShape(pThis->pGen, aExtShape);

	/* An array-backed var writes the record index as ragged whatever extent it
	   declared, since storage grows as records arrive.  That override applies
	   only to a real extent: '-' and '^' are statements about the container and
	   outrank it.  Print from a COPY, because the declared shape is still needed
	   below (a header-values var is the one with VARIDX_UNUSED at index 0). */
	ptrdiff_t aPrnShape[VARIDX_MAX];
	memcpy(aPrnShape, aExtShape, sizeof(aPrnShape));
	if((gt == gtArray)&&(aPrnShape[0] != VARIDX_UNUSED)&&
	   (aPrnShape[0] != VARIDX_BORROW))
		aPrnShape[0] = VARIDX_RAGGED;

	char sIndex[128] = {'\0'};
	if(das_shape_toStr(aPrnShape, nExtRank, sIndex, sizeof(sIndex)) < 0)
		return DASERR_VAR;

	/* Items per record and whether any of them are ragged; index 0 is the record
	   index and never counts toward either. */
	int nItems = 1;
	bool bRaggedItems = false;
	for(int i = 1; i < nExtRank; ++i){
		if(aExtShape[i] == VARIDX_RAGGED)  bRaggedItems = true;
		else if(aExtShape[i] > 0)          nItems *= aExtShape[i];
	}

	/* 2. the open tag.  semantic is derived, not stored: it was spent at
	   parse and the writer re-states the interpretation for the reader.

	   use= is omitted when it equals the schema's default.  The schema carries
	   default="center", so any schema-aware reader recovers it for free and
	   writing it out is noise.  Contrast the <ops> parameters below, whose
	   defaults the schema can no longer state at all. */
	if(strcmp(sRole, "center") == 0)
		DasBuf_printf(pBuf, "    <%s", sElement);
	else
		DasBuf_printf(pBuf, "    <%s use=\"%s\"", sElement, sRole);

	if(!bByteRun){
		const char* sSem = das_sem_default((das_val_type)et, units);
		DasBuf_printf(pBuf, " semantic=\"%s\"", sSem ? sSem : "");
	}

	/* storage hint: scalar sequences state it outright (their storage is
	   invisible any other way) */
	if((gt == gtSeq)&&(bScalar)){
		DasBuf_printf(pBuf, " storage=\"%s\"",
			(et == etTime) ? "struct" : das_vt_toStr((das_val_type)et)
		);
	}

	/* storage hint: header values and text-encoded numerics need it */
	if(gt == gtArray){
		DasAry* pAry = DasGen_getArray(pThis->pGen);
		das_val_type vtAry = DasAry_valType(pAry);
		int nCkItems = 0;
		const DasCodec* pCkCodec = DasDs_getCodecFor(pDs, DasAry_id(pAry), &nCkItems);
		bool bHdrVals = (aExtShape[0] == VARIDX_UNUSED);
		bool bText = (pCkCodec != NULL)&&(pCkCodec->vtBuf == vtText);
		if((bHdrVals || bText) && !bByteRun &&
		   (vtAry != vtText) && (vtAry != vtByteSeq)){
			DasBuf_printf(pBuf, " storage=\"%s\"",
				(vtAry == vtTime) ? "struct" : das_vt_toStr(vtAry)
			);
		}
	}

	if((!bScalar)&&(!bByteRun)){
		const DasVarComp* pComp = (const DasVarComp*)pThis;
		char sIntern[64] = {'\0'};
		if(das_shape_toStr(pComp->aIntShape, pComp->nIntRank, sIntern, sizeof(sIntern)) < 0)
			return DASERR_VAR;
		DasBuf_printf(pBuf, " intern=\"%s\"", sIntern);
	}

	/* Absent units means dimensionless, so an empty units= carries exactly the
	   information absence does.  Emit only a real one. */
	if(units == UNIT_DIMENSIONLESS)
		DasBuf_printf(pBuf, " index=\"%s\">\n", sIndex);
	else
		DasBuf_printf(pBuf, " index=\"%s\" units=\"%s\">\n", sIndex, units);

	/* 3. child order is purpose to bytes: properties, ops, generator */
	if(DasDesc_length((DasDesc*)pThis) > 0){
		int nRet = DasDesc_encode3((DasDesc*)pThis, pBuf, "      ");
		if(nRet != DAS_OKAY) return nRet;
	}

	/* The form writes its own <ops>, parameters and all.  Linear writes
	   nothing, which is how "no ops element" gets said.  A byte run has no
	   formalism at all. */
	if(pThis->pForm != NULL){
		int nRet = pThis->pForm->pVTbl->encode(pThis->pForm, pBuf);
		if(nRet != DAS_OKAY) return nRet;
	}

	/* 4. the generator child */
	if(gt == gtSeq){
		const DasGenSeq* pSeq = (const DasGenSeq*)pThis->pGen;
		size_t uElem  = das_vt_size((das_val_type)et);
		size_t uSlope = (et == etTime) ? sizeof(double) : uElem;
		das_elem_type etSlope = (et == etTime) ? etDouble : et;

		for(int c = 0; c < pSeq->nComps; ++c){
			char sMin[64] = {'\0'};
			_var_seqValToStr(pSeq->aIntercept[c], et, sMin, sizeof(sMin));

			/* full-rank interval, '-'-aligned to index= */
			char sInterval[256] = {'\0'};
			char* pIv = sInterval;
			for(int i = 0; i < nExtRank; ++i){
				if(i > 0){ *pIv = ';'; ++pIv; }
				if(aExtShape[i] == VARIDX_UNUSED){ *pIv = '-'; ++pIv; }
				else{
					char sM[64] = {'\0'};
					_var_seqValToStr(pSeq->aInterval[c][i], etSlope, sM, sizeof(sM));
					int n = snprintf(pIv, (size_t)(sInterval + sizeof(sInterval) - pIv), "%s", sM);
					if(n > 0) pIv += n;
				}
			}
			(void)uSlope;
			DasBuf_printf(pBuf,
				"      <sequence minval=\"%s\" interval=\"%s\" />\n", sMin, sInterval
			);
		}
	}
	else if(gt == gtArray){
		DasAry* pAry = DasGen_getArray(pThis->pGen);
		das_val_type vtAry = DasAry_valType(pAry);
		int nItemsPerWrite = 0;
		const DasCodec* pCodec = DasDs_getCodecFor(pDs, DasAry_id(pAry), &nItemsPerWrite);

		const char* sSemC = das_sem_default((das_val_type)et, units);
		if(bByteRun)   /* the sentinel is what tells a string from a blob */
			sSemC = ((const DasVarBytes*)pThis)->bSentinel ? DAS_SEM_TEXT
			                                             : DAS_SEM_BLOB;

		DasCodec codecHdr;
		if(pCodec == NULL){
			/* header values: no packet codec exists, make a transient writer */
			if(aExtShape[0] != VARIDX_UNUSED){
				return das_error(DASERR_VAR, "No codec provided for %s/%s/%s/%s packet data!",
					DasDs_id(pDs), DasDim_typeName(pDim), DasDim_id(pDim), sRole
				);
			}
			DasCodec_init(
				DASENC_WRITE, &codecHdr, pAry, sSemC, "utf8", DASENC_ITEM_TERM, ' ',
				units, NULL
			);
			DasBuf_puts(pBuf, "      <values>\n");
			int nWrite = (int)DasAry_size(pAry);
			int nVals = DasCodec_encode(&codecHdr, pBuf, DIM0, nWrite, DASENC_IN_HDR|DASENC_PKT_LAST);
			if(nVals < 0){
				return das_error(DASERR_VAR, "Error encoding data for %s/%s/%s/%s",
					DasDs_id(pDs), DasDim_typeName(pDim), DasDim_id(pDim), sRole
				);
			}
			DasBuf_puts(pBuf, "      </values>\n");
			DasCodec_deInit(&codecHdr);
		}
		else{
			/* fill: strings and blobs have an empty fill, bools a glyph */
			char sFill[64] = {'\0'};
			das_val_type vtExt = pCodec->vtBuf;
			if((sSemC == DAS_SEM_BOOL) && (vtExt == vtText)){
				strncpy(sFill, "*", sizeof(sFill) - 1);
			}
			else if(!bByteRun){
				das_datum dmFill;
				das_datum_init(&dmFill, DasAry_getFill(pAry), vtAry, das_vt_size(vtAry), units);
				das_datum_toStrValOnly(&dmFill, sFill, 63, 6);
			}

			char sItemBytes[16] = {'\0'};
			if(pCodec->nBufValSz < 1)
				strncpy(sItemBytes, "*", sizeof(sItemBytes) - 1);
			else
				snprintf(sItemBytes, sizeof(sItemBytes) - 1, "%d", pCodec->nBufValSz);

			char sValTerm[32] = {'\0'};
			if((pCodec->nBufValSz == DASENC_ITEM_TERM) && (pCodec->sSepSet[0] != '\0'))
				snprintf(sValTerm, sizeof(sValTerm) - 1, " valTerm=\"%c\"", pCodec->sSepSet[0]);

			char sIdxTerm[12 + (VARIDX_MAX - 1)*3] = {'\0'};
			if(pCodec->nSep > 1){
				int n = snprintf(sIdxTerm, sizeof(sIdxTerm), " idxTerm=\"");
				for(ubyte j = 1; (j < pCodec->nSep) && (n < (int)sizeof(sIdxTerm) - 4); ++j){
					char cLvl = pCodec->sSepSet[j];
					char cbuf[2] = {cLvl, '\0'};
					const char* sLvl = cbuf;
					switch(cLvl){
					case '\n': sLvl = "\\n"; break;  case '\t': sLvl = "\\t"; break;
					case '\r': sLvl = "\\r"; break;  case '\0': sLvl = "\\0"; break;
					}
					n += snprintf(sIdxTerm + n, sizeof(sIdxTerm) - n, "%s%s", (j > 1) ? "," : "", sLvl);
				}
				if(n < (int)sizeof(sIdxTerm))
					snprintf(sIdxTerm + n, sizeof(sIdxTerm) - n, "\"");
			}

			/* composites put intern-many values in each item run */
			if((!bScalar)&&(!bByteRun)){
				const DasVarComp* pComp = (const DasVarComp*)pThis;
				for(int i = 0; i < pComp->nIntRank; ++i)
					if(pComp->aIntShape[i] > 0) nItems *= pComp->aIntShape[i];
			}

			char sNumItems[16] = {'\0'};
			if(bRaggedItems)
				strncpy(sNumItems, "*", sizeof(sNumItems) - 1);
			else
				snprintf(sNumItems, sizeof(sNumItems) - 1, "%d", nItems);

			const char* sEnc = pCodec->sEncType;

			char sTrim[16] = {'\0'};
			if((pCodec->nBufValSz < 1) && (strcmp(sEnc, "utf8") == 0) && !DasCodec_isTrim(pCodec))
				strncpy(sTrim, " trim=\"false\"", sizeof(sTrim) - 1);

			DasBuf_printf(pBuf,
				"      <packet numItems=\"%s\" itemBytes=\"%s\" encoding=\"%s\"%s%s%s fill=\"%s\" />\n",
				sNumItems, sItemBytes, sEnc, sValTerm, sIdxTerm, sTrim, sFill
			);
		}
	}
	else{
		return das_error(DASERR_NOTIMP,
			"Serializing a generator of kind %d is not yet implemented", (int)gt
		);
	}

	DasBuf_printf(pBuf, "    </%s>\n", sElement);
	return DAS_OKAY;
}

DasVar* new_DasVar(DasGen* pGen, das_units units, DasForm* pForm)
{
	if(pGen == NULL){
		das_error(DASERR_VAR, "Null generator for new_DasVar");
		return NULL;
	}
	/* Checked before anything is allocated, so the error path leaks nothing.
	   NULL is not "no formalism" here -- an absent <ops> binds the explicit
	   linear form, and only a byte run's CLASS gets to say it has no math. */
	if(pForm == NULL){
		das_error(DASERR_VAR,
			"A scalar needs a formalism; pass the linear form, not NULL");
		return NULL;
	}

	/* THE gate every construction path passes through.  A form arrives here
	   either from the wire (new_DasForm_pairs) or from an application calling
	   a typed constructor, and this is the first point where the form and the
	   shape it has to describe are both known.  See form.h's validate slot. */
	if(DasForm_validate(pForm, 0, NULL) != DAS_OKAY){
		return NULL;
	}

	/* The wire allows a ';' units list; nothing in the library can hold
	   per-component units yet and keeping only the first entry would misstate
	   the data, so refuse (Dude's ruling 2026-07-27). */
	if((units != NULL)&&(strchr(units, ';') != NULL)){
		das_error(DASERR_NOTIMP,
			"Per-component units lists ('%s') are not yet supported, and "
			"keeping only the first entry would misstate the data", units
		);
		return NULL;
	}

	DasVar* pThis = (DasVar*)calloc(1, sizeof(DasVar));
	DasDesc_init(&(pThis->base), VARIABLE);
	pThis->pVTbl    = &g_vtblVarScalar;
	pThis->units = units;
	pThis->nRef  = 1;

	/* A variable owns its formalism outright; it takes a COPY and leaves the
	   caller's object alone.  Sharing one form between two variables would put
	   a per-variable fact (how many components intern= declares) in an object
	   two variables read, and the second validate() would overwrite the first.
	   Forms are flat and small, so the copy costs less than the hazard.

	   The generator below still follows the add-a-reference rule.  Doing both
	   after the refusals above is what makes an early return safe: there is
	   nothing to give back because nothing was taken. */
	pThis->pForm = DasForm_copy(pForm);

	pThis->pGen = pGen;
	DasGen_incRef(pGen);
	return pThis;
}


/* ************************************************************************* */
/* DasVarComp: the numeric component run, the <composite> element            */

/* A composite datum is a VIEW: das_datum_box stores the run pointer, it never
   copies.  So the run must be memory that outlives this call -- the variable's
   own storage when it has any, the caller's scratch when it does not. */
static int DasVarComp_get(
	const DasVar* pBase, ptrdiff_t* pLoc, das_byte_seq work, das_datum* pOut
){
	/* the formalism interprets the run */
	if((pBase->pForm == NULL)||(pBase->pForm->pVTbl->pack == NULL)){
		das_error(DASERR_NOTIMP,
			"No single-datum representation for a %s composite "
			"(components are readable through DasVar_subset)",
			(pBase->pForm != NULL) ? DasForm_kindStr(pBase->pForm) : "plain"
		);
		return -1 * DASERR_NOTIMP;
	}

	size_t uBytes = 0, uNeed = 0;
	const ubyte* pRun = _DasVar_runAt(
		pBase, pLoc, work.ptr, work.sz, &uBytes, &uNeed
	);
	if(pRun == NULL)
		return (uNeed > 0) ? (int)uNeed : -1 * DASERR_VAR;

	das_operand op;
	if(!_DasVar_operand(pBase, &op)) return -1 * DASERR_VAR;

	/* pack() sizes the run from the declared extent, so a short run would be
	   read off its end.  Nothing checks declared against actual at build time
	   yet, which is why the read checks. */
	size_t uWant = _DasVar_itemBytes(pBase);
	if((uWant > 0)&&(uBytes < uWant)){
		das_error(DASERR_VAR,
			"A %s composite declares %zu bytes per item but only %zu are "
			"available at this location",
			DasForm_kindStr(pBase->pForm), uWant, uBytes
		);
		return -1 * DASERR_VAR;
	}

	if(!pBase->pForm->pVTbl->pack(pBase->pForm, &op, pRun, pOut))
		return -1 * DASERR_VAR;
	return 0;
}

static const char* DasVarComp_element(const DasVar* pThis)
{
	(void)pThis;
	return "composite";
}

static das_elem_type DasVarIntr_elemType(const DasVar* pThis)
{
	return DasGen_elemType(pThis->pGen);
}

static int DasVarIntr_shape(const DasVar* pThis, ptrdiff_t* pShape)
{
	return DasGen_extShape(pThis->pGen, pShape);
}

static int DasVarComp_intrShape(const DasVar* pBase, ptrdiff_t* pShape)
{
	const DasVarComp* pThis = (const DasVarComp*)pBase;
	memcpy(pShape, pThis->aIntShape, sizeof(ptrdiff_t)*(size_t)pThis->nIntRank);
	return pThis->nIntRank;
}

static char* DasVarComp_expression(
	const DasVar* pThis, char* sBuf, int nLen, unsigned int uFlags
){
	(void)uFlags;
	snprintf(sBuf, (size_t)nLen, "composite(%s%s%s)",
		pThis->units ? pThis->units : "",
		(pThis->pForm != NULL) ? " " : "",
		(pThis->pForm != NULL) ? DasForm_kindStr(pThis->pForm) : ""
	);
	return sBuf;
}

static bool DasVar_isNumericTrue(const DasVar* pThis)
{
	(void)pThis;
	return true;
}

static DasVar* DasVarComp_copy(const DasVar* pThis)
{
	DasVarComp* pOut = (DasVarComp*)calloc(1, sizeof(DasVarComp));
	*pOut = *((const DasVarComp*)pThis);
	memset(&(pOut->base.base), 0, sizeof(DasDesc));   /* see scalar copy */
	DasDesc_init(&(pOut->base.base), VARIABLE);
	DasDesc_copyIn(&(pOut->base.base), &(pThis->base));
	pOut->base.nRef = 1;
	/* the GENERATOR is cloned so a later setArray on one owner cannot
	   re-aim the other; only the backing array is shared */
	if(pOut->base.pGen != NULL)
		pOut->base.pGen = DasGen_copy(pThis->pGen);

	pOut->base.pForm = DasForm_copy(pOut->base.pForm);
	return (DasVar*)pOut;
}

static const DasVar_VTbl g_vtblVarComp = {
	.get        = DasVarComp_get,
	.element    = DasVarComp_element,
	.elemType   = DasVarIntr_elemType,
	.shape      = DasVarIntr_shape,
	.intrShape  = DasVarComp_intrShape,
	.lengthIn   = DasVarGen_lengthIn,
	.itemElems  = _DasVarGen_itemElems,
	.subsetView = _DasVarGen_subsetView,
	.subsetInto = _DasVarGen_subsetInto,
	.expression = DasVarComp_expression,
	.isNumeric  = DasVar_isNumericTrue,
	.incRef     = DasVarGen_incRef,
	.decRef     = DasVarGen_decRef,
	.copy       = DasVarComp_copy
};

/* Shared by both internal-run constructors: one units string per variable.
   A run carries one units string for every cell in it, so a per-component list has
   nowhere to live. */
static bool _intr_unitsOk(das_units units)
{
	if((units != NULL)&&(strchr(units, ';') != NULL)){
		das_error(DASERR_NOTIMP,
			"Per-component units lists ('%s') are not yet supported, and "
			"keeping only the first entry would misstate the data", units
		);
		return false;
	}
	return true;
}

DasVarComp* new_DasVarComp(
	DasGen* pGen, das_units units, DasForm* pForm,
	int nIntRank, const ptrdiff_t* pIntShape
){
	if((pGen == NULL)||(pIntShape == NULL)||(pForm == NULL)||(nIntRank < 1)||
	   (nIntRank >= VARIDX_MAX)){
		das_error(DASERR_VAR, "Invalid arguments to new_DasVarComp");
		return NULL;
	}

	if(DasForm_validate(pForm, nIntRank, pIntShape) != DAS_OKAY)
		return NULL;

	das_elem_type et = DasGen_elemType(pGen);
	if((et == etUnknown)||(et == etTime)){
		das_error(DASERR_VAR,
			"Composite components must be plain numeric, not element type %d",
			(int)et
		);
		return NULL;
	}

	if(!_intr_unitsOk(units)) return NULL;

	DasVarComp* pThis = (DasVarComp*)calloc(1, sizeof(DasVarComp));
	DasDesc_init(&(pThis->base.base), VARIABLE);
	pThis->base.pVTbl    = &g_vtblVarComp;
	pThis->base.units = units;
	pThis->base.nRef  = 1;

	/* Takes a copy; see new_DasVar.  The four refusals above return with the
	   caller's object untouched. */
	pThis->base.pForm = DasForm_copy(pForm);

	pThis->nIntRank = nIntRank;
	memcpy(pThis->aIntShape, pIntShape, sizeof(ptrdiff_t)*(size_t)nIntRank);

	pThis->base.pGen = pGen;
	DasGen_incRef(pGen);
	return pThis;
}


/* ************************************************************************* */
/* DasVarBytes: the byte run, the <bytes> element                             */

/* A byte run never needs the caller's scratch however long it is:
   new_DasVarBytes accepts array-backed generators only, precisely so that a
   text or blob datum always has real memory to point at. */
static int DasVarBytes_get(
	const DasVar* pBase, ptrdiff_t* pLoc, das_byte_seq work, das_datum* pOut
){
	const DasVarBytes* pThis = (const DasVarBytes*)pBase;
	(void)work;

	size_t uCount = 0;
	const ubyte* ptr = DasGen_at(pBase->pGen, pLoc, &uCount);
	if(ptr == NULL) return -1 * DASERR_VAR;

	if(pThis->bSentinel){
		/* the datum is a VIEW: a pointer into backing storage.  The trailing
		   null the sentinel rule guarantees is what makes a bare char* legal. */
		memcpy(pOut, &ptr, sizeof(const ubyte*));
		pOut->vt    = vtText;
		pOut->vsize = das_vt_size(vtText);
	}
	else{
		das_cbyte_seq bs;
		bs.ptr = ptr;
		bs.sz  = uCount;
		memcpy(pOut, &bs, sizeof(das_cbyte_seq));
		pOut->vt    = vtByteSeq;
		pOut->vsize = sizeof(das_cbyte_seq);
	}
	pOut->units = pBase->units;
	return 0;
}

static const char* DasVarBytes_element(const DasVar* pThis)
{
	(void)pThis;
	return "bytes";
}

static int DasVarBytes_intrShape(const DasVar* pBase, ptrdiff_t* pShape)
{
	pShape[0] = ((const DasVarBytes*)pBase)->nExtent;
	return 1;   /* a byte run's internal rank is always 1 */
}

static char* DasVarBytes_expression(
	const DasVar* pThis, char* sBuf, int nLen, unsigned int uFlags
){
	(void)uFlags;
	snprintf(sBuf, (size_t)nLen, "%s(%s)",
		((const DasVarBytes*)pThis)->bSentinel ? "string" : "blob",
		pThis->units ? pThis->units : ""
	);
	return sBuf;
}

static bool DasVar_isNumericFalse(const DasVar* pThis)
{
	(void)pThis;
	return false;
}

static DasVar* DasVarBytes_copy(const DasVar* pThis)
{
	DasVarBytes* pOut = (DasVarBytes*)calloc(1, sizeof(DasVarBytes));
	*pOut = *((const DasVarBytes*)pThis);
	memset(&(pOut->base.base), 0, sizeof(DasDesc));   /* see scalar copy */
	DasDesc_init(&(pOut->base.base), VARIABLE);
	DasDesc_copyIn(&(pOut->base.base), &(pThis->base));
	pOut->base.nRef = 1;
	if(pOut->base.pGen != NULL)
		pOut->base.pGen = DasGen_copy(pThis->pGen);

	pOut->base.pForm = DasForm_copy(pOut->base.pForm);
	return (DasVar*)pOut;
}

static const DasVar_VTbl g_vtblVarBytes = {
	.get        = DasVarBytes_get,
	.element    = DasVarBytes_element,
	.elemType   = DasVarIntr_elemType,
	.shape      = DasVarIntr_shape,
	.intrShape  = DasVarBytes_intrShape,
	.lengthIn   = DasVarGen_lengthIn,
	.itemElems  = _DasVarGen_itemElems,
	.subsetView = _DasVarGen_subsetView,
	.subsetInto = _DasVarGen_subsetInto,
	.expression = DasVarBytes_expression,
	.isNumeric  = DasVar_isNumericFalse,
	.incRef     = DasVarGen_incRef,
	.decRef     = DasVarGen_decRef,
	.copy       = DasVarBytes_copy
};

DasVarBytes* new_DasVarBytes(
	DasGen* pGen, das_units units, bool bSentinel, ptrdiff_t nExtent
){
	if(pGen == NULL){
		das_error(DASERR_VAR, "Invalid arguments to new_DasVarBytes");
		return NULL;
	}

	das_elem_type et = DasGen_elemType(pGen);
	if(et != etUByte){
		das_error(DASERR_VAR,
			"A byte-run variable needs an etUByte source, not element type %d", (int)et
		);
		return NULL;
	}
	if(DasGen_type(pGen) != gtArray){
		das_error(DASERR_NOTIMP,
			"A %s datum is a pointer into storage; computed byte runs have no "
			"home to point at", bSentinel ? "string" : "blob"
		);
		return NULL;
	}

	if(!_intr_unitsOk(units)) return NULL;

	DasVarBytes* pThis = (DasVarBytes*)calloc(1, sizeof(DasVarBytes));
	DasDesc_init(&(pThis->base.base), VARIABLE);
	pThis->base.pVTbl    = &g_vtblVarBytes;
	pThis->base.units = units;
	pThis->base.nRef  = 1;

	/* base.form stays zeroed.  There is no formalism argument to this call:
	   the class is how "no auto-math" is stated, and a zeroed form can never
	   match a registry rule. */

	pThis->bSentinel = bSentinel;
	pThis->nExtent   = nExtent;

	pThis->base.pGen = pGen;
	DasGen_incRef(pGen);
	return pThis;
}


/* ************************************************************************* */
/* Component labels, a convenience                                           */

/* Not core.  Nothing in the library calls this and no application has to;
   labelling the components of a composite is a chore every output format
   faces, so here is one default answer for the ones that would rather not
   invent their own.  das3_cdf and das3_csv both take it. */

int DasVar_compLabels(
	const DasVar* pThis, char** psBuf, int nMax, size_t uLenEa
){
	if((pThis == NULL)||(psBuf == NULL)||(nMax < 1)||(uLenEa < 2))
		return -1 * das_error(DASERR_VAR, "Bad inputs to DasVar_compLabels");

	/* How many labels are wanted is the variable's own business.  A scalar has
	   no internal index and is the one component case of the same question. */
	ptrdiff_t aIntShape[VARIDX_MAX] = VARIDX_INIT_UNUSED;
	int nIntRank = DasVar_intrShape(pThis, aIntShape);

	int nComp = 1;
	for(int i = 0; i < nIntRank; ++i){
		if(aIntShape[i] < 1)
			return -1 * das_error(DASERR_VAR,
				"A value with a ragged component count can not be labelled"
			);
		nComp *= (int)aIntShape[i];
	}
	if(nComp > nMax)
		return -1 * das_error(DASERR_VAR,
			"%d components to label, only %d were provided for", nComp, nMax
		);

	const char* sStem = NULL;

	/* Inherited on purpose, so a label on the dimension covers every variable
	   under it and a producer states it once. */
	const DasProp* pProp = DasDesc_getProp((const DasDesc*)pThis, "label");
	if(pProp != NULL){
		/* One per component is the author saying exactly what they want */
		int nItems = DasProp_extractItems(pProp, psBuf, nComp, uLenEa);
		if(nItems == nComp) return nComp;

		if(DasProp_items(pProp) == 1)
			sStem = DasProp_value(pProp);
		else
			daslog_warn_v(
				"Expected 1 or %d values in label '%s', found %d; using the "
				"dimension name instead", nComp, DasProp_value(pProp), nItems
			);
	}

	/* name= is required on a dimension, so there is always something to build
	   from.  physDim is the backstop for a stream that somehow lacks one. */
	if(sStem == NULL){
		const DasDesc* pDim = DasDesc_parent((const DasDesc*)pThis);
		if(pDim == NULL)
			return -1 * das_error(DASERR_VAR,
				"An unattached variable has no name to label its components with"
			);
		sStem = DasDim_id((const DasDim*)pDim);
		if((sStem == NULL)||(sStem[0] == '\0'))
			sStem = DasDim_dim((const DasDim*)pDim);
	}

	if(nComp == 1){
		strncpy(psBuf[0], sStem, uLenEa - 1);
		psBuf[0][uLenEa - 1] = '\0';
		return 1;
	}

	for(int i = 0; i < nComp; ++i){
		const char* sSym = DasVar_compSym(pThis, i);
		if(sSym != NULL)
			snprintf(psBuf[i], uLenEa - 1, "%s_%s", sStem, sSym);
		else
			/* A kind with no symbols still needs distinct labels, or a reader
			   gets N identical ones and cannot tell the components apart. */
			snprintf(psBuf[i], uLenEa - 1, "%s_%d", sStem, i);
	}
	return nComp;
}
