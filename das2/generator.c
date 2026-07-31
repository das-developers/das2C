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
#include <assert.h>

#include "util.h"
#include "time.h"
#include "generator.h"

/* ************************************************************************* */
/* Shape strings.  The index= and intern= grammar, parsed and emitted in one  */
/* place so the two halves cannot drift apart.                               */
/*                                                                           */
/* Plain ptrdiff_t arrays in and out, never a DasSet: this layer sits below   */
/* set.h and has no business knowing what a variable is.  Callers apply their */
/* own policy (exact rank, whether a flag token is legal here, item counts).  */

DasErrCode das_shape_fromStr(
	const char* sShape, int nMaxRank, bool bAllowFlags, ptrdiff_t* pShape,
	int* pnRank, const char* sWhere
){
	const char* sBeg = sShape;
	int nRank = 0;

	if((sShape == NULL)||(pShape == NULL)||(pnRank == NULL))
		return das_error(DASERR_GEN, "Null argument to das_shape_fromStr");

	while(*sBeg != '\0'){
		if(nRank >= nMaxRank)
			return das_error(DASERR_SERIAL,
				"More than %d index entries in \"%s\" for %s", nMaxRank, sShape,
				sWhere
			);

		if(*sBeg == '*'){
			pShape[nRank] = SETIDX_RAGGED;
			++sBeg;
		}
		else if((*sBeg == '^')||(*sBeg == '-')){
			/* Borrowed and unused are statements a VARIABLE makes about the
			   container's index space.  A dataset is that container, so it can
			   neither borrow an extent nor decline one. */
			if(!bAllowFlags)
				return das_error(DASERR_SERIAL,
					"'%c' is not allowed in \"%s\" for %s", *sBeg, sShape, sWhere
				);
			pShape[nRank] = (*sBeg == '^') ? SETIDX_BORROW : SETIDX_UNUSED;
			++sBeg;
		}
		else{
			ptrdiff_t nExtent = 0;
			int nDigits = 0;
			while((*sBeg >= '0')&&(*sBeg <= '9')){
				if(++nDigits > 9)      /* keeps nExtent well inside ptrdiff_t */
					return das_error(DASERR_SERIAL,
						"Absurd extent in \"%s\" for %s", sShape, sWhere
					);
				nExtent = nExtent*10 + (*sBeg - '0');
				++sBeg;
			}
			if(nDigits == 0)
				return das_error(DASERR_SERIAL,
					"Unexpected '%c' in \"%s\" for %s", *sBeg, sShape, sWhere
				);
			pShape[nRank] = nExtent;
		}
		++nRank;

		if(*sBeg == '\0')
			break;
		if(*sBeg != ';')
			return das_error(DASERR_SERIAL,
				"Unexpected '%c' in \"%s\" for %s", *sBeg, sShape, sWhere
			);
		++sBeg;
	}

	if(nRank < 1)
		return das_error(DASERR_SERIAL, "Empty shape string for %s", sWhere);

	*pnRank = nRank;
	return DAS_OKAY;
}

int das_shape_toStr(const ptrdiff_t* pShape, int nRank, char* sBuf, int nLen)
{
	if((pShape == NULL)||(sBuf == NULL)||(nLen < 2)){
		das_error(DASERR_GEN, "Invalid argument to das_shape_toStr");
		return -1;
	}

	char* pWrite = sBuf;
	char* pEnd   = sBuf + nLen - 1;    /* leave room for the null */

	for(int i = 0; i < nRank; ++i){
		if((pEnd - pWrite) < 12){      /* widest token is a 10 digit count */
			das_error(DASERR_GEN, "Buffer too small for a rank %d shape", nRank);
			return -1;
		}
		if(i > 0){ *pWrite = ';'; ++pWrite; }

		switch(pShape[i]){
		case SETIDX_UNUSED: *pWrite = '-'; ++pWrite; break;
		case SETIDX_BORROW: *pWrite = '^'; ++pWrite; break;
		case SETIDX_RAGGED: *pWrite = '*'; ++pWrite; break;
		default:
			pWrite += snprintf(pWrite, (size_t)(pEnd - pWrite), "%td", pShape[i]);
		}
	}
	*pWrite = '\0';
	return (int)(pWrite - sBuf);
}

/* ************************************************************************* */
/* The shape lattice.  These merge extents in the MODEL's index space, which  */
/* has BORROW and UNUSED extents and marks ragged with SETIDX_RAGGED (-1).    */
/*                                                                            */
/* They are NOT for array storage shapes.  An array marks an unset extent with */
/* ARYIDX_UNBOUND (0), which this lattice would read as a real extent of zero  */
/* and silently propagate as the minimum.                                     */

void das_varindex_merge(int nRank, ptrdiff_t* pDest, ptrdiff_t* pSrc)
{
	
	for(size_t u = 0; u < nRank && u < SETIDX_MAX; ++u){
		
		/* Here's the order of shape merge precidence
		 *
		 * Ragged > Number > Borrow > Unused
		 *
		 *    | R | N | B | U
		 *  --+---+---+---+---
		 *  R | R | R | R | R
		 *  --+---+---+---+---
		 *  N | R |low| N | N
		 *  --+---+---+---+---
		 *  B | R | N | B | B
		 *  --+---+---+---+---
		 *  U | R | N | B | U
		 *  --+---+---+---+---
		 */
		
		/* If either is ragged, the result is ragged */
		if((pDest[u] == SETIDX_RAGGED) || (pSrc[u] == SETIDX_RAGGED)){ 
			pDest[u] = SETIDX_RAGGED;
			continue;
		}
		
		/* If either is a number, the result is the smallest number */
		if((pDest[u] >= 0) || (pSrc[u] >= 0)){
			if((pDest[u] >= 0) && (pSrc[u] >= 0))
				pDest[u] = (pDest[u] < pSrc[u]) ? pDest[u] : pSrc[u];
			
			else
				/* Take item that is a number, cause one of them must be */
				pDest[u] = (pDest[u] < pSrc[u]) ? pSrc[u] : pDest[u];
			
			continue;
		}
		
		/* All that's left is a borrowed extent or unused; borrow beats unused */
		if((pDest[u] == SETIDX_BORROW)||(pSrc[u] == SETIDX_BORROW)){
			pDest[u] = SETIDX_BORROW;
			continue;
		}
		
		/* default to unused requires no action */
	}
}

ptrdiff_t das_varlength_merge(ptrdiff_t nLeft, ptrdiff_t nRight)
{
	/* Two real lengths -> the smaller, on purpose: during a live read variables
	 * fill in different-sized blocks, so the extent usable across all of them is
	 * the minimum currently populated (das-general-stream lineage).  A mismatch
	 * is a normal mid-stream state, not an error. */
	if((nLeft >= 0)&&(nRight >= 0)) return nLeft < nRight ? nLeft : nRight;

	/* Reflect at 0 since FUNC beats UNUSED, and a real index beats anything
	 * that's just a flag */

	return nLeft > nRight ? nLeft : nRight;
}

/* ************************************************************************* */

/* ************************************************************************* */
/* Base refcounting.  Generators destroy themselves at zero; the owning set  */
/* holds one reference, expression trees hold more.                          */

int DasGen_incRef(DasGen* pThis)
{
	pThis->nRef += 1;
	return pThis->nRef;
}

int DasGen_decRef(DasGen* pThis)
{
	assert(pThis->nRef > 0);
	pThis->nRef -= 1;
	if(pThis->nRef == 0){
		pThis->pVTbl->destroy(pThis);
		return 0;
	}
	return pThis->nRef;
}

int DasGen_eval(
	const DasGen* pThis, const ptrdiff_t* pExtLoc, ubyte* pRun, size_t uRunMax
){
	return pThis->pVTbl->eval(pThis, pExtLoc, pRun, uRunMax);
}

int DasGen_extShape(const DasGen* pThis, ptrdiff_t* pShape)
{
	return pThis->pVTbl->extShape(pThis, pShape);
}

ptrdiff_t DasGen_lengthIn(const DasGen* pThis, int nIdx, ptrdiff_t* pLoc)
{
	return pThis->pVTbl->lengthIn(pThis, nIdx, pLoc);
}

const ubyte* DasGen_at(
	const DasGen* pThis, const ptrdiff_t* pExtLoc, size_t* pCount
){
	return pThis->pVTbl->at(pThis, pExtLoc, pCount);
}

DasAry* DasGen_subsetView(
	const DasGen* pThis, int nExtRank, const ptrdiff_t* pMin, const ptrdiff_t* pMax
){
	return pThis->pVTbl->subsetView(pThis, nExtRank, pMin, pMax);
}

int DasGen_subsetInto(
	const DasGen* pThis, int nExtRank, const ptrdiff_t* pMin,
	const ptrdiff_t* pMax, ubyte* pBuf, size_t uBufLen
){
	return pThis->pVTbl->subsetInto(pThis, nExtRank, pMin, pMax, pBuf, uBufLen);
}

const ubyte* DasGen_getFill(const DasGen* pThis)
{
	if(pThis->kind != gtArray) return NULL;
	return DasAry_getFill(((const DasGenAry*)pThis)->pAry);
}

size_t DasGen_itemElems(const DasGen* pThis)
{
	switch(pThis->kind){
	case gtArray: return ((const DasGenAry*)pThis)->uItemElems;
	case gtSeq:   return (size_t)((const DasGenSeq*)pThis)->nComps;
	case gtConst: return 1;
	default:      return 1;   /* gtBinop/gtUnop are scalar-only in v1 */
	}
}

/* Clone the generator OBJECT; backing storage is shared (array refs
   incremented), matching the retired variable layer's copy semantics.  The
   clone matters: an owner may later re-aim ITS generator at replacement
   storage (DasSet_setArray) without touching other owners' view. */
DasGen* DasGen_copy(const DasGen* pThis)
{
	switch(pThis->kind){
	case gtArray: {
		DasGenAry* pOut = (DasGenAry*)calloc(1, sizeof(DasGenAry));
		*pOut = *((const DasGenAry*)pThis);
		pOut->base.nRef = 1;
		inc_DasAry(pOut->pAry);
		return (DasGen*)pOut;
	}
	case gtSeq: {
		DasGenSeq* pOut = (DasGenSeq*)calloc(1, sizeof(DasGenSeq));
		*pOut = *((const DasGenSeq*)pThis);
		pOut->base.nRef = 1;
		return (DasGen*)pOut;
	}
	case gtConst: {
		DasGenConst* pOut = (DasGenConst*)calloc(1, sizeof(DasGenConst));
		*pOut = *((const DasGenConst*)pThis);
		pOut->base.nRef = 1;
		return (DasGen*)pOut;
	}
	case gtBinop: case gtUnop: {
		const DasGenOp* pIn = (const DasGenOp*)pThis;
		DasGenOp* pOut = (DasGenOp*)calloc(1, sizeof(DasGenOp));
		*pOut = *pIn;
		pOut->base.nRef = 1;
		pOut->pLeft  = (pIn->pLeft  != NULL) ? DasGen_copy(pIn->pLeft)  : NULL;
		pOut->pRight = (pIn->pRight != NULL) ? DasGen_copy(pIn->pRight) : NULL;
		return (DasGen*)pOut;
	}
	}
	das_error(DASERR_ARRAY, "Unknown generator kind %d", (int)pThis->kind);
	return NULL;
}

DasAry* DasGen_getArray(const DasGen* pThis)
{
	if(pThis->kind != gtArray) return NULL;
	return ((const DasGenAry*)pThis)->pAry;
}

bool DasGen_setArray(DasGen* pThis, DasAry* pNew)
{
	if(pThis->kind != gtArray){
		das_error(DASERR_ARRAY, "Only array-backed generators hold an array");
		return false;
	}
	DasGenAry* pGa = (DasGenAry*)pThis;

	/* The index map is preserved, so the replacement has to have the same
	   index structure as the array it stands in for. */
	if(DasAry_rank(pNew) != DasAry_rank(pGa->pAry)){
		das_error(DASERR_ARRAY,
			"Replacement array '%s' is rank %d, but '%s' is rank %d",
			DasAry_id(pNew), DasAry_rank(pNew), DasAry_id(pGa->pAry),
			DasAry_rank(pGa->pAry)
		);
		return false;
	}

	/* Simple storage swaps only (the common one being an epoch integer/real
	   array traded for a das_time array on the way to ISO-8601 text).  Byte
	   runs carry sentinel structure this function does not re-derive. */
	das_val_type vtNew = DasAry_valType(pNew);
	if((vtNew == vtUByte)||(vtNew == vtByte)||
	   (vtNew < VT_MIN_SIMPLE)||(vtNew > VT_MAX_SIMPLE)){
		das_error(DASERR_ARRAY,
			"DasGen_setArray only handles simple value types, not %s",
			das_vt_toStr(vtNew)
		);
		return false;
	}

	inc_DasAry(pNew);
	dec_DasAry(pGa->pAry);
	pGa->pAry = pNew;
	pThis->elem = (das_elem_type)vtNew;
	return true;
}

/* Promote any plain numeric element to double; false for struct times */
bool das_elem_asDouble(das_elem_type et, const ubyte* p, double* pOut)
{
	switch(et){
	case etUByte:  *pOut = *((const uint8_t*)p);  return true;
	case etByte:   *pOut = *((const int8_t*)p);   return true;
	case etUShort: *pOut = *((const uint16_t*)p); return true;
	case etShort:  *pOut = *((const int16_t*)p);  return true;
	case etUInt:   *pOut = *((const uint32_t*)p); return true;
	case etInt:    *pOut = *((const int32_t*)p);  return true;
	case etULong:  *pOut = (double)*((const uint64_t*)p); return true;
	case etLong:   *pOut = (double)*((const int64_t*)p);  return true;
	case etFloat:  *pOut = *((const float*)p);    return true;
	case etDouble: *pOut = *((const double*)p);   return true;
	default: return false;
	}
}

/* computed sources have no storage to point at */
static const ubyte* _DasGen_atNone(
	const DasGen* pThis, const ptrdiff_t* pExtLoc, size_t* pCount
){
	(void)pThis; (void)pExtLoc;
	if(pCount != NULL) *pCount = 0;
	return NULL;
}

/* A computed generator is bounded by whatever extent it was declared with,
   and behaves like an array from the outside wherever that is possible.  A
   stated length is such a case: outside it there is simply no value, so the
   ask is refused rather than answered with a number the stream never claimed
   to have.

   Only a NON-NEGATIVE extent bounds anything.  SETIDX_RAGGED takes its size
   from the data, SETIDX_BORROW from elsewhere in the dataset, and neither is
   knowable here, so those indices are defined for every location the caller
   asks about.  SETIDX_UNUSED does not move the value at all.  That is the
   ragged case where arrays and sequences genuinely cannot match.

   Returns DAS_OKAY, or a negative das error code naming the offending index. */
static int _DasGen_checkExtent(
	const ptrdiff_t* pExtShape, int nExtRank, const ptrdiff_t* pExtLoc
){
	for(int i = 0; i < nExtRank; ++i){
		if(pExtShape[i] < 0) continue;

		if((pExtLoc[i] < 0)||(pExtLoc[i] >= pExtShape[i]))
			return -1 * das_error(DASERR_GEN,
				"Index %td on dimension %d is outside the declared extent of "
				"%td", pExtLoc[i], i, pExtShape[i]
			);
	}
	return DAS_OKAY;
}

/* ...and nothing to hand out a view of, so the set always allocates */
static DasAry* _DasGen_subsetViewNone(
	const DasGen* pThis, int nExtRank, const ptrdiff_t* pMin, const ptrdiff_t* pMax
){
	(void)pThis; (void)nExtRank; (void)pMin; (void)pMax;
	return NULL;
}

/* The default fill: walk the requested range and evaluate every cell.

   This is the whole implementation for sequences, constants and operators.
   Those three used to carry a subset function apiece in the retired variable
   layer, and all three were the same loop wrapped around a different
   per-cell call -- which is exactly what eval() already abstracts.  gtArray
   is the one kind that needs its own, because it can miss (ragged rows) and
   a miss must become fill rather than an error. */
static int _DasGen_subsetIntoEval(
	const DasGen* pThis, int nExtRank, const ptrdiff_t* pMin,
	const ptrdiff_t* pMax, ubyte* pBuf, size_t uBufLen
){
	size_t uItemElems = DasGen_itemElems(pThis);
	if(uItemElems == 0)
		return -1 * das_error(DASERR_NOTIMP,
			"Ragged item runs have no fixed width to subset; use DasGen_at"
		);

	size_t uItemSz = uItemElems * das_vt_size((das_val_type)pThis->elem);

	ptrdiff_t aLoc[SETIDX_MAX];
	for(int d = 0; d < nExtRank; ++d) aLoc[d] = pMin[d];

	ubyte* pWrite = pBuf;
	size_t uWrote = 0, uRemain = uBufLen;
	while(aLoc[0] < pMax[0]){

		if(uRemain < uItemSz)
			return -1 * das_error(DASERR_ARRAY,
				"Subset buffer of %zu bytes is short for the requested range",
				uBufLen
			);

		int nRet = pThis->pVTbl->eval(pThis, aLoc, pWrite, uRemain);
		if(nRet < 0) return nRet;

		pWrite  += uItemSz;
		uRemain -= uItemSz;
		++uWrote;

		/* row-major roll: last external index moves fastest */
		for(int d = nExtRank - 1; d > -1; --d){
			aLoc[d] += 1;
			if((d > 0) && (aLoc[d] == pMax[d]))
				aLoc[d] = pMin[d];
			else
				break;
		}
	}

	return (int)uWrote;
}

/* Sequence and constant elements are computed in 8-byte slots; etTime does
   not fit and arrives with the var_seq.c migration, not before. */
static bool _gen_elemOk(das_elem_type et)
{
	switch(et){
	case etLong: case etFloat: case etDouble: return true;
	default: return false;
	}
}

/* ************************************************************************* */
/* gtArray: a lookup into a backing DasAry                                   */

static int _DasGenAry_eval(
	const DasGen* pBase, const ptrdiff_t* pExtLoc, ubyte* pRun, size_t uRunMax
){
	const DasGenAry* pThis = (const DasGenAry*)pBase;

	ptrdiff_t aAryLoc[SETIDX_MAX] = SETIDX_INIT_BEGIN;
	int nAryRank = DasAry_rank(pThis->pAry);

	for(int i = 0; i < pThis->nExtRank; ++i){
		if(pThis->idxmap[i] == SETIDX_UNUSED) continue;
		aAryLoc[(int)pThis->idxmap[i]] = pExtLoc[i];
	}

	if(pThis->uItemElems == 0)
		return -1 * das_error(DASERR_NOTIMP,
			"Ragged item runs don't fit fixed-size eval; use DasGen_at");

	size_t uElemSz = das_vt_size((das_val_type)pBase->elem);
	if(pThis->uItemElems * uElemSz > uRunMax)
		return -1 * das_error(DASERR_ARRAY,
			"Run buffer of %zu bytes can't hold a %zu element item",
			uRunMax, pThis->uItemElems
		);

	/* The item run is contiguous in the trailing (internal) indices, so one
	   getAt on the leading corner covers the whole item for a cubic internal
	   shape. */
	(void)nAryRank;
	const ubyte* pSrc = DasAry_getAt(
		pThis->pAry, (das_val_type)pBase->elem, aAryLoc
	);
	if(pSrc == NULL)
		return -1 * das_error(DASERR_ARRAY, "Invalid index for backing array");

	memcpy(pRun, pSrc, pThis->uItemElems * uElemSz);
	return (int)pThis->uItemElems;
}

static int _DasGenAry_extShape(const DasGen* pBase, ptrdiff_t* pShape)
{
	const DasGenAry* pThis = (const DasGenAry*)pBase;

	ptrdiff_t aAryShape[SETIDX_MAX];
	DasAry_shape(pThis->pAry, aAryShape);

	for(int i = 0; i < pThis->nExtRank; ++i){
		if(pThis->idxmap[i] == SETIDX_UNUSED)
			pShape[i] = SETIDX_UNUSED;
		else
			pShape[i] = aAryShape[(int)pThis->idxmap[i]];
	}
	return pThis->nExtRank;
}

static ptrdiff_t _DasGenAry_lengthIn(
	const DasGen* pBase, int nIdx, ptrdiff_t* pLoc
){
	const DasGenAry* pThis = (const DasGenAry*)pBase;

	/* lock down the indices BEFORE nIdx, then report the count along nIdx */
	ptrdiff_t aAryLoc[SETIDX_MAX] = SETIDX_INIT_BEGIN;
	int nIndexes = 0;
	for(int i = 0; i < nIdx; ++i){
		if(pThis->idxmap[i] < 0) continue;
		aAryLoc[(int)pThis->idxmap[i]] = pLoc[i];
		++nIndexes;
	}

	/* a source that does not run along index nIdx has no length there, and
	   must say so rather than answer for an index it does not own */
	if((nIdx >= pThis->nExtRank)||(pThis->idxmap[nIdx] < 0))
		return SETIDX_UNUSED;

	return (ptrdiff_t)DasAry_lengthIn(pThis->pAry, nIndexes, aAryLoc);
}

static const ubyte* _DasGenAry_at(
	const DasGen* pBase, const ptrdiff_t* pExtLoc, size_t* pCount
){
	const DasGenAry* pThis = (const DasGenAry*)pBase;

	ptrdiff_t aAryLoc[SETIDX_MAX] = SETIDX_INIT_BEGIN;
	int nMapped = 0;
	for(int i = 0; i < pThis->nExtRank; ++i){
		if(pThis->idxmap[i] == SETIDX_UNUSED) continue;
		aAryLoc[(int)pThis->idxmap[i]] = pExtLoc[i];
		if(pThis->idxmap[i] >= nMapped) nMapped = pThis->idxmap[i] + 1;
	}
	/* getAt validates the location; the run is the trailing extents.  Cubic
	   internal shapes have a fixed count, ragged ones ask the array at this
	   location. */
	const ubyte* pSrc = DasAry_getAt(
		pThis->pAry, DasAry_valType(pThis->pAry), aAryLoc
	);
	if(pCount != NULL){
		if(pSrc == NULL)
			*pCount = 0;
		else if(pThis->uItemElems > 0)
			*pCount = pThis->uItemElems;
		else
			*pCount = DasAry_lengthIn(pThis->pAry, nMapped, aAryLoc);
	}
	return pSrc;
}

/* Array indices are laid out mapped-first, so anything past the last mapped
   one is the item run.  (See the idxmap comment on DasGenAry.) */
static int _DasGenAry_numMapped(const DasGenAry* pThis)
{
	int nMapped = 0;
	for(int i = 0; i < pThis->nExtRank; ++i){
		if(pThis->idxmap[i] == SETIDX_UNUSED) continue;
		if(pThis->idxmap[i] >= nMapped) nMapped = pThis->idxmap[i] + 1;
	}
	return nMapped;
}

/* Zero-copy path, ported from _DasVarAry_directSubset in the retired layer.
   Answers only when the request lands on storage that is already contiguous
   and correctly shaped: a prefix of pinned single indices followed by full
   ranges.  Anything else (a partial range, a degenerate index asked for more
   than one value) returns NULL and the caller copies instead. */
static DasAry* _DasGenAry_subsetView(
	const DasGen* pBase, int nExtRank, const ptrdiff_t* pMin, const ptrdiff_t* pMax
){
	const DasGenAry* pThis = (const DasGenAry*)pBase;
	if(nExtRank != pThis->nExtRank) return NULL;

	ptrdiff_t aAryMin[SETIDX_MAX] = SETIDX_INIT_BEGIN;
	ptrdiff_t aAryMax[SETIDX_MAX] = SETIDX_INIT_BEGIN;

	for(int d = 0; d < pThis->nExtRank; ++d){
		ptrdiff_t nSz = pMax[d] - pMin[d];
		if(pThis->idxmap[d] == SETIDX_UNUSED){
			/* A degenerate index has one value to give; asking for a run of
			   them means replication, which a view cannot express. */
			if(nSz != 1) return NULL;
		}
		else{
			aAryMin[(int)pThis->idxmap[d]] = pMin[d];
			aAryMax[(int)pThis->idxmap[d]] = pMax[d];
		}
	}

	ptrdiff_t aAryShape[SETIDX_MAX];
	int nAryRank = DasAry_shape(pThis->pAry, aAryShape);
	int nMapped  = _DasGenAry_numMapped(pThis);

	ptrdiff_t aLoc[SETIDX_MAX];
	int nLocSz = 0;
	int iBegFullRng = -1;

	for(int d = 0; d < nMapped; ++d){

		if((aAryMin[d] < 0)||(aAryMax[d] > aAryShape[d])){
			das_error(DASERR_ARRAY, "Invalid subset request");
			return NULL;
		}

		if((aAryMax[d] - aAryMin[d]) == 1){
			/* Once full ranges start, single items can't resume: the result
			   would not be contiguous. */
			if(iBegFullRng != -1) return NULL;
			aLoc[nLocSz] = aAryMin[d];
			++nLocSz;
		}
		else{
			if((aAryMin[d] == 0)&&(aAryMax[d] == aAryShape[d])){
				if(iBegFullRng == -1) iBegFullRng = d;
			}
			else
				return NULL;   /* partial range, has to be copied */
		}
	}

	if(nLocSz >= nAryRank) return NULL;

	return DasAry_subSetIn(pThis->pAry, NULL, nLocSz, aLoc);
}

/* Can the copy be done by pointer arithmetic?  Striding assumes a constant
   step per index, which a ragged extent breaks unless everything above it is
   pinned to a single value.  Ported from _DasVarAry_canStride. */
static bool _DasGenAry_canStride(
	const DasGenAry* pThis, const ptrdiff_t* pMin, const ptrdiff_t* pMax
){
	ptrdiff_t aShape[SETIDX_MAX] = SETIDX_INIT_UNUSED;
	DasAry_shape(pThis->pAry, aShape);

	int iFirstUsed = -1;
	ptrdiff_t nSzFirstUsed = 0;
	int iFirstRagged = -1;

	for(int d = 0; d < pThis->nExtRank; ++d){
		if(pThis->idxmap[d] == SETIDX_UNUSED) continue;

		int iLoc = pThis->idxmap[d];
		if(iFirstUsed == -1){
			iFirstUsed = iLoc;
			nSzFirstUsed = pMax[d] - pMin[d];
			continue;
		}
		if((aShape[iLoc] == SETIDX_RAGGED)&&(iFirstRagged == -1)){
			iFirstRagged = iLoc;
			break;
		}
	}

	return (iFirstRagged == -1) || (nSzFirstUsed == 1);
}

/* The copy.  Two ports in one function: the stride walk from
   _DasVarAry_strideSubset when the geometry allows it, and the cell-by-cell
   walk from _DasVarAry_slowSubset otherwise.  The slow walk is the only
   place raggedness is rectangularized, by substituting fill wherever the
   backing array has no value.

   Correction against the old code: it read whole ITEMS as single elements,
   using base.vt, which for a vector variable was vtGeoVec.  strideSubset
   patched around that by swapping in the component type, slowSubset did not,
   so a ragged vector taking the slow path copied the wrong width.  Here the
   unit of transfer is uItemElems elements of the generator's element type in
   both walks, which is what the backing storage actually holds. */
static int _DasGenAry_subsetInto(
	const DasGen* pBase, int nExtRank, const ptrdiff_t* pMin,
	const ptrdiff_t* pMax, ubyte* pBuf, size_t uBufLen
){
	const DasGenAry* pThis = (const DasGenAry*)pBase;

	if(nExtRank != pThis->nExtRank)
		return -1 * das_error(DASERR_ARRAY,
			"Generator is external rank %d, but subset request is rank %d",
			pThis->nExtRank, nExtRank
		);

	if(pThis->uItemElems == 0)
		return -1 * das_error(DASERR_NOTIMP,
			"Ragged item runs have no fixed width to subset; use DasGen_at"
		);

	/* Running off the end of a KNOWN extent is an error, not fill.  Only a
	   ragged extent gets squared off below, because only there is "no value
	   here" a statement about the data rather than a bad request.  This check
	   has to be here: the retired layer did it inside its zero-copy attempt
	   and used a continue-flag to stop the fall-through, but subsetView
	   returning NULL now means only "not applicable", so nothing upstream
	   would catch an out of range ask before the stride walk trusted it. */
	{
		ptrdiff_t aAryShape[SETIDX_MAX];
		DasAry_shape(pThis->pAry, aAryShape);
		for(int d = 0; d < nExtRank; ++d){
			if(pThis->idxmap[d] == SETIDX_UNUSED) continue;
			ptrdiff_t nLen = aAryShape[(int)pThis->idxmap[d]];
			if(nLen < 0) continue;                  /* ragged, fill applies */
			if((pMin[d] < 0)||(pMax[d] > nLen))
				return -1 * das_error(DASERR_ARRAY,
					"Subset range %td to %td on index %d is outside array "
					"'%s', which is %td long there",
					pMin[d], pMax[d], d, DasAry_id(pThis->pAry), nLen
				);
		}
	}

	das_val_type vtEl = (das_val_type)pBase->elem;
	size_t uElemSz = das_vt_size(vtEl);
	size_t uItemSz = pThis->uItemElems * uElemSz;
	const ubyte* pFill = DasAry_getFill(pThis->pAry);

	ptrdiff_t aLoc[SETIDX_MAX];
	for(int d = 0; d < nExtRank; ++d) aLoc[d] = pMin[d];

	ubyte* pWrite = pBuf;
	size_t uWrote = 0, uRemain = uBufLen;

	/* Fast path: one base pointer plus a per-index byte stride. */
	bool bStride = _DasGenAry_canStride(pThis, pMin, pMax);
	const ubyte* pBaseRead = NULL;
	ptrdiff_t aVarStride[SETIDX_MAX] = {0};

	if(bStride){
		ptrdiff_t aBaseIdx[SETIDX_MAX] = {0};
		for(int d = 0; d < nExtRank; ++d){
			if(pThis->idxmap[d] == SETIDX_UNUSED) continue;
			aBaseIdx[(int)pThis->idxmap[d]] = pMin[d];
		}
		size_t uAvail = 0;
		pBaseRead = DasAry_getIn(
			pThis->pAry, vtEl, DasAry_rank(pThis->pAry), aBaseIdx, &uAvail
		);
		if(pBaseRead == NULL)
			bStride = false;   /* fall through to the safe walk */
		else{
			ptrdiff_t aAryShape[SETIDX_MAX];
			ptrdiff_t aAryStride[SETIDX_MAX];
			if(DasAry_stride(pThis->pAry, aAryShape, aAryStride) < 1)
				bStride = false;
			else{
				for(int d = 0; d < DasAry_rank(pThis->pAry); ++d)
					aAryStride[d] *= (ptrdiff_t)uElemSz;

				for(int d = 0; d < nExtRank; ++d){
					if((pMax[d] - pMin[d]) == 1) continue;  /* pinned, no step */
					if(pThis->idxmap[d] == SETIDX_UNUSED) continue;
					aVarStride[d] = aAryStride[(int)pThis->idxmap[d]];
				}
			}
		}
	}

	while(aLoc[0] < pMax[0]){

		if(uRemain < uItemSz)
			return -1 * das_error(DASERR_ARRAY,
				"Subset buffer of %zu bytes is short for the requested range",
				uBufLen
			);

		if(bStride){
			const ubyte* pRead = pBaseRead;
			for(int d = 0; d < nExtRank; ++d) pRead += aLoc[d]*aVarStride[d];
			memcpy(pWrite, pRead, uItemSz);
		}
		else{
			ptrdiff_t aAryLoc[SETIDX_MAX] = SETIDX_INIT_BEGIN;
			for(int d = 0; d < nExtRank; ++d){
				if(pThis->idxmap[d] == SETIDX_UNUSED) continue;
				aAryLoc[(int)pThis->idxmap[d]] = aLoc[d];
			}

			/* An invalid location is not an error here: this is how a subset
			   of a ragged array comes back rectangular. */
			if(!DasAry_validAt(pThis->pAry, aAryLoc)){
				if(pFill == NULL)
					return -1 * das_error(DASERR_ARRAY,
						"Array '%s' has no fill value, so a ragged range "
						"cannot be squared off", DasAry_id(pThis->pAry)
					);
				for(size_t u = 0; u < pThis->uItemElems; ++u)
					memcpy(pWrite + u*uElemSz, pFill, uElemSz);
			}
			else{
				const ubyte* pRead = DasAry_getAt(pThis->pAry, vtEl, aAryLoc);
				if(pRead == NULL)
					return -1 * das_error(DASERR_ARRAY,
						"Invalid index for backing array '%s'",
						DasAry_id(pThis->pAry)
					);
				memcpy(pWrite, pRead, uItemSz);
			}
		}

		pWrite  += uItemSz;
		uRemain -= uItemSz;
		++uWrote;

		for(int d = nExtRank - 1; d > -1; --d){
			aLoc[d] += 1;
			if((d > 0) && (aLoc[d] == pMax[d]))
				aLoc[d] = pMin[d];
			else
				break;
		}
	}

	return (int)uWrote;
}

static void _DasGenAry_destroy(DasGen* pBase)
{
	DasGenAry* pThis = (DasGenAry*)pBase;
	if(pThis->pAry != NULL) dec_DasAry(pThis->pAry);
	free(pThis);
}

static const DasGen_VTbl g_vtblGenAry = {
	_DasGenAry_eval, _DasGenAry_extShape, _DasGenAry_lengthIn, _DasGenAry_at,
	_DasGenAry_subsetView, _DasGenAry_subsetInto, _DasGenAry_destroy
};

DasGen* new_DasGenAry(DasAry* pAry, int nExtRank, const int8_t* pIdxMap)
{
	if((pAry == NULL)||(nExtRank < 1)||(nExtRank > SETIDX_MAX)||(pIdxMap == NULL)){
		das_error(DASERR_ARRAY, "Invalid arguments to new_DasGenAry");
		return NULL;
	}

	das_val_type vt = DasAry_valType(pAry);
	if((vt < VT_MIN_SIMPLE)||(vt > VT_MAX_SIMPLE)){
		das_error(DASERR_ARRAY,
			"Array %s holds %s items, not a generator element type",
			DasAry_id(pAry), DasAry_valTypeStr(pAry)
		);
		return NULL;
	}

	ptrdiff_t aAryShape[SETIDX_MAX];
	int nAryRank = DasAry_shape(pAry, aAryShape);

	/* which array indices are consumed by the external map? */
	bool aUsed[SETIDX_MAX] = {false};
	for(int i = 0; i < nExtRank; ++i){
		if(pIdxMap[i] == SETIDX_UNUSED) continue;
		if((pIdxMap[i] < 0)||(pIdxMap[i] >= nAryRank)){
			das_error(DASERR_ARRAY, "Index map entry %d out of range", i);
			return NULL;
		}
		aUsed[(int)pIdxMap[i]] = true;
	}

	/* the rest are the item run.  A cubic internal shape gives a fixed item
	   count; any ragged internal extent marks the count per-location
	   (uItemElems 0), served by at()/lengthIn but not by fixed-size eval. */
	size_t uItemElems = 1;
	for(int i = 0; i < nAryRank; ++i){
		if(aUsed[i]) continue;
		if(aAryShape[i] < 1){ uItemElems = 0; break; }
		uItemElems *= (size_t)aAryShape[i];
	}

	DasGenAry* pThis = (DasGenAry*)calloc(1, sizeof(DasGenAry));
	pThis->base.kind = gtArray;
	pThis->base.pVTbl   = &g_vtblGenAry;
	pThis->base.elem = (das_elem_type)vt;
	pThis->base.nRef = 1;
	pThis->pAry      = pAry;
	inc_DasAry(pAry);
	pThis->nExtRank  = nExtRank;
	memcpy(pThis->idxmap, pIdxMap, (size_t)nExtRank);
	pThis->uItemElems = uItemElems;

	return (DasGen*)pThis;
}

/* ************************************************************************* */
/* gtSeq: intercept + interval, computed on demand                           */

static int _DasGenSeq_eval(
	const DasGen* pBase, const ptrdiff_t* pExtLoc, ubyte* pRun, size_t uRunMax
){
	const DasGenSeq* pThis = (const DasGenSeq*)pBase;

	int nRet = _DasGen_checkExtent(pThis->aExtShape, pThis->nExtRank, pExtLoc);
	if(nRet != DAS_OKAY) return nRet;

	size_t uElemSz = das_vt_size((das_val_type)pBase->elem);
	if(uElemSz * (size_t)pThis->nComps > uRunMax)
		return -1 * das_error(DASERR_ARRAY, "Run buffer too small for sequence");

	for(int c = 0; c < pThis->nComps; ++c){
		ubyte* pOut = pRun + (size_t)c * uElemSz;

		switch(pBase->elem){
		case etDouble: case etFloat: {
			double r = (pBase->elem == etDouble) ?
				*((const double*)pThis->aIntercept[c]) :
				(double)*((const float*)pThis->aIntercept[c]);
			for(int i = 0; i < pThis->nExtRank; ++i){
				double rSlope = (pBase->elem == etDouble) ?
					*((const double*)(pThis->aInterval[c][i])) :
					(double)*((const float*)(pThis->aInterval[c][i]));
				r += rSlope * (double)pExtLoc[i];
			}
			if(pBase->elem == etDouble) *((double*)pOut) = r;
			else                        *((float*)pOut)  = (float)r;
			break;
		}
		case etLong: {
			int64_t n = *((const int64_t*)pThis->aIntercept[c]);
			for(int i = 0; i < pThis->nExtRank; ++i)
				n += *((const int64_t*)(pThis->aInterval[c][i])) * (int64_t)pExtLoc[i];
			*((int64_t*)pOut) = n;
			break;
		}
		case etTime: {
			/* das_time intercept, DOUBLE slopes in seconds */
			das_time dt = *((const das_time*)pThis->aIntercept[c]);
			double rSpan = 0.0;
			for(int i = 0; i < pThis->nExtRank; ++i)
				rSpan += *((const double*)(pThis->aInterval[c][i])) * (double)pExtLoc[i];
			dt.second += rSpan;
			dt_tnorm(&dt);
			memcpy(pOut, &dt, sizeof(das_time));
			break;
		}
		default:
			return -1 * das_error(DASERR_NOTIMP,
				"Sequence element type %d not yet implemented", (int)pBase->elem
			);
		}
	}
	return pThis->nComps;
}

static int _DasGenSeq_extShape(const DasGen* pBase, ptrdiff_t* pShape)
{
	const DasGenSeq* pThis = (const DasGenSeq*)pBase;
	memcpy(pShape, pThis->aExtShape, sizeof(ptrdiff_t)*(size_t)pThis->nExtRank);
	return pThis->nExtRank;
}

static ptrdiff_t _DasGenSeq_lengthIn(
	const DasGen* pBase, int nIdx, ptrdiff_t* pLoc
){
	const DasGenSeq* pThis = (const DasGenSeq*)pBase;
	(void)pLoc;
	if((nIdx < 0)||(nIdx >= pThis->nExtRank)) return SETIDX_UNUSED;
	return pThis->aExtShape[nIdx];
}

static void _DasGenSeq_destroy(DasGen* pBase){ free(pBase); }

static const DasGen_VTbl g_vtblGenSeq = {
	_DasGenSeq_eval, _DasGenSeq_extShape, _DasGenSeq_lengthIn, _DasGen_atNone,
	_DasGen_subsetViewNone, _DasGen_subsetIntoEval, _DasGenSeq_destroy
};

DasGen* new_DasGenSeqN(
	das_elem_type et, int nComps, const ubyte* pIntercepts, int nExtRank,
	const ubyte* pIntervals, const ptrdiff_t* pExtShape
){
	if((pIntercepts==NULL)||(pIntervals==NULL)||(pExtShape==NULL)||
	   (nExtRank < 1)||(nExtRank > SETIDX_MAX)||
	   (nComps < 1)||(nComps > DASGEN_SEQ_MAXCOMP)){
		das_error(DASERR_ARRAY, "Invalid arguments to new_DasGenSeqN");
		return NULL;
	}
	if(!_gen_elemOk(et) && (et != etTime)){
		das_error(DASERR_NOTIMP,
			"Sequence element type %d not yet implemented", (int)et
		);
		return NULL;
	}

	size_t uElemSz  = das_vt_size((das_val_type)et);
	/* etTime slopes are doubles in seconds */
	size_t uSlopeSz = (et == etTime) ? sizeof(double) : uElemSz;

	DasGenSeq* pThis = (DasGenSeq*)calloc(1, sizeof(DasGenSeq));
	pThis->base.kind = gtSeq;
	pThis->base.pVTbl   = &g_vtblGenSeq;
	pThis->base.elem = et;
	pThis->base.nRef = 1;
	pThis->nComps = nComps;
	for(int c = 0; c < nComps; ++c){
		memcpy(pThis->aIntercept[c], pIntercepts + (size_t)c*uElemSz, uElemSz);
		for(int i = 0; i < nExtRank; ++i)
			memcpy(pThis->aInterval[c][i],
			       pIntervals + ((size_t)c*(size_t)nExtRank + (size_t)i)*uSlopeSz,
			       uSlopeSz);
	}
	pThis->nExtRank = nExtRank;
	memcpy(pThis->aExtShape, pExtShape, sizeof(ptrdiff_t)*(size_t)nExtRank);

	return (DasGen*)pThis;
}

DasGen* new_DasGenSeq(
	das_elem_type et, const ubyte* pIntercept, int nExtRank,
	const ubyte* pIntervals, const ptrdiff_t* pExtShape
){
	return new_DasGenSeqN(et, 1, pIntercept, nExtRank, pIntervals, pExtShape);
}

/* ************************************************************************* */
/* gtConst: one value, everywhere                                            */

static int _DasGenConst_eval(
	const DasGen* pBase, const ptrdiff_t* pExtLoc, ubyte* pRun, size_t uRunMax
){
	const DasGenConst* pThis = (const DasGenConst*)pBase;

	int nRet = _DasGen_checkExtent(pThis->aExtShape, pThis->nExtRank, pExtLoc);
	if(nRet != DAS_OKAY) return nRet;

	size_t uElemSz = das_vt_size((das_val_type)pBase->elem);
	if(uElemSz > uRunMax)
		return -1 * das_error(DASERR_ARRAY, "Run buffer too small for constant");
	memcpy(pRun, pThis->aValue, uElemSz);
	return 1;
}

static int _DasGenConst_extShape(const DasGen* pBase, ptrdiff_t* pShape)
{
	const DasGenConst* pThis = (const DasGenConst*)pBase;
	memcpy(pShape, pThis->aExtShape, sizeof(ptrdiff_t)*(size_t)pThis->nExtRank);
	return pThis->nExtRank;
}

static ptrdiff_t _DasGenConst_lengthIn(
	const DasGen* pBase, int nIdx, ptrdiff_t* pLoc
){
	const DasGenConst* pThis = (const DasGenConst*)pBase;
	(void)pLoc;
	if((nIdx < 0)||(nIdx >= pThis->nExtRank)) return SETIDX_UNUSED;
	return pThis->aExtShape[nIdx];
}

static void _DasGenConst_destroy(DasGen* pBase){ free(pBase); }

static const DasGen_VTbl g_vtblGenConst = {
	_DasGenConst_eval, _DasGenConst_extShape, _DasGenConst_lengthIn,
	_DasGen_atNone,
	_DasGen_subsetViewNone, _DasGen_subsetIntoEval, _DasGenConst_destroy
};

DasGen* new_DasGenConst(
	das_elem_type et, const ubyte* pVal, int nExtRank, const ptrdiff_t* pExtShape
){
	if((pVal==NULL)||(pExtShape==NULL)||(nExtRank < 1)||(nExtRank > SETIDX_MAX)){
		das_error(DASERR_ARRAY, "Invalid arguments to new_DasGenConst");
		return NULL;
	}
	if(!_gen_elemOk(et)){
		das_error(DASERR_NOTIMP,
			"Constant element type %d not yet implemented", (int)et
		);
		return NULL;
	}

	DasGenConst* pThis = (DasGenConst*)calloc(1, sizeof(DasGenConst));
	pThis->base.kind = gtConst;
	pThis->base.pVTbl   = &g_vtblGenConst;
	pThis->base.elem = et;
	pThis->base.nRef = 1;
	memcpy(pThis->aValue, pVal, das_vt_size((das_val_type)et));
	pThis->nExtRank = nExtRank;
	memcpy(pThis->aExtShape, pExtShape, sizeof(ptrdiff_t)*(size_t)nExtRank);

	return (DasGen*)pThis;
}


/* ************************************************************************* */
/* gtBinop: an operation over two child generators                           */

static int _DasGenOp_eval(
	const DasGen* pBase, const ptrdiff_t* pExtLoc, ubyte* pRun, size_t uRunMax
){
	const DasGenOp* pThis = (const DasGenOp*)pBase;

	ubyte aL[sizeof(das_time)], aR[sizeof(das_time)];
	int nL = DasGen_eval(pThis->pLeft, pExtLoc, aL, sizeof(aL));
	if(nL < 0) return nL;
	int nR = DasGen_eval(pThis->pRight, pExtLoc, aR, sizeof(aR));
	if(nR < 0) return nR;
	if((nL != 1)||(nR != 1))
		return -1 * das_error(DASERR_NOTIMP,
			"Operations are scalar-only in v1, refusing a %d x %d item pairing",
			nL, nR
		);

	if(das_vt_size((das_val_type)pBase->elem) > uRunMax)
		return -1 * das_error(DASERR_ARRAY, "Run buffer too small for op result");

	/* a right-scale factor promotes the right operand to double */
	das_elem_type etR = DasGen_elemType(pThis->pRight);
	if(pThis->rRightScale != 1.0){
		double rVal;
		if(!das_elem_asDouble(etR, aR, &rVal))
			return -1 * das_error(DASERR_GEN,
				"Can't scale element type %d for the operation", (int)etR);
		rVal *= pThis->rRightScale;
		memcpy(aR, &rVal, sizeof(double));
		etR = etDouble;
	}

	if(!pThis->apply(DasGen_elemType(pThis->pLeft), etR, aL, aR, pRun))
		return -1 * das_error(DASERR_GEN, "Operand elements not supported");
	return 1;
}

static int _DasGenOp_extShape(const DasGen* pBase, ptrdiff_t* pShape)
{
	const DasGenOp* pThis = (const DasGenOp*)pBase;

	ptrdiff_t aRight[SETIDX_MAX];
	for(int i = 0; i < SETIDX_MAX; ++i){
		pShape[i] = SETIDX_UNUSED; aRight[i] = SETIDX_UNUSED;
	}

	int nRank = DasGen_extShape(pThis->pLeft, pShape);
	int nRankR = DasGen_extShape(pThis->pRight, aRight);
	if(nRankR > nRank) nRank = nRankR;

	das_varindex_merge(nRank, pShape, aRight);
	return nRank;
}

static ptrdiff_t _DasGenOp_lengthIn(
	const DasGen* pBase, int nIdx, ptrdiff_t* pLoc
){
	const DasGenOp* pThis = (const DasGenOp*)pBase;
	return das_varlength_merge(
		DasGen_lengthIn(pThis->pLeft, nIdx, pLoc),
		DasGen_lengthIn(pThis->pRight, nIdx, pLoc)
	);
}

static void _DasGenOp_destroy(DasGen* pBase)
{
	DasGenOp* pThis = (DasGenOp*)pBase;
	if(pThis->pLeft != NULL)  DasGen_decRef(pThis->pLeft);
	if(pThis->pRight != NULL) DasGen_decRef(pThis->pRight);
	free(pThis);
}

static const DasGen_VTbl g_vtblGenOp = {
	_DasGenOp_eval, _DasGenOp_extShape, _DasGenOp_lengthIn, _DasGen_atNone,
	_DasGen_subsetViewNone, _DasGen_subsetIntoEval, _DasGenOp_destroy
};

DasGen* new_DasGenBinop(
	das_gen_applyfn apply, das_elem_type etOut, DasGen* pLeft, DasGen* pRight,
	double rRightScale
){
	if((apply == NULL)||(pLeft == NULL)||(pRight == NULL)){
		das_error(DASERR_GEN, "Invalid arguments to new_DasGenBinop");
		return NULL;
	}
	DasGenOp* pThis = (DasGenOp*)calloc(1, sizeof(DasGenOp));
	pThis->base.kind = gtBinop;
	pThis->base.pVTbl   = &g_vtblGenOp;
	pThis->base.elem = etOut;
	pThis->base.nRef = 1;
	pThis->apply  = apply;
	pThis->rRightScale = rRightScale;
	pThis->pLeft  = pLeft;   DasGen_incRef(pLeft);
	pThis->pRight = pRight;  DasGen_incRef(pRight);
	return (DasGen*)pThis;
}
