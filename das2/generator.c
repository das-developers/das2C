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
#include "generator.h"

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
		pThis->vt->destroy(pThis);
		return 0;
	}
	return pThis->nRef;
}

int DasGen_eval(
	const DasGen* pThis, const ptrdiff_t* pExtLoc, ubyte* pRun, size_t uRunMax
){
	return pThis->vt->eval(pThis, pExtLoc, pRun, uRunMax);
}

int DasGen_extShape(const DasGen* pThis, ptrdiff_t* pShape)
{
	return pThis->vt->extShape(pThis, pShape);
}

ptrdiff_t DasGen_lengthIn(const DasGen* pThis, int nIdx, ptrdiff_t* pLoc)
{
	return pThis->vt->lengthIn(pThis, nIdx, pLoc);
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

	ptrdiff_t aAryLoc[DASIDX_MAX] = DASIDX_INIT_BEGIN;
	int nAryRank = DasAry_rank(pThis->pAry);

	for(int i = 0; i < pThis->nExtRank; ++i){
		if(pThis->idxmap[i] == DASIDX_UNUSED) continue;
		aAryLoc[(int)pThis->idxmap[i]] = pExtLoc[i];
	}

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

	ptrdiff_t aAryShape[DASIDX_MAX];
	DasAry_shape(pThis->pAry, aAryShape);

	for(int i = 0; i < pThis->nExtRank; ++i){
		if(pThis->idxmap[i] == DASIDX_UNUSED)
			pShape[i] = DASIDX_UNUSED;
		else
			pShape[i] = aAryShape[(int)pThis->idxmap[i]];
	}
	return pThis->nExtRank;
}

static ptrdiff_t _DasGenAry_lengthIn(
	const DasGen* pBase, int nIdx, ptrdiff_t* pLoc
){
	const DasGenAry* pThis = (const DasGenAry*)pBase;

	/* map the external partial location into array space */
	ptrdiff_t aAryLoc[DASIDX_MAX] = DASIDX_INIT_BEGIN;
	int nAryIdx = 0;
	for(int i = 0; i < nIdx; ++i){
		if(pThis->idxmap[i] == DASIDX_UNUSED) continue;
		aAryLoc[(int)pThis->idxmap[i]] = pLoc[i];
		if(pThis->idxmap[i] >= nAryIdx) nAryIdx = pThis->idxmap[i] + 1;
	}
	return (ptrdiff_t)DasAry_lengthIn(pThis->pAry, nAryIdx, aAryLoc);
}

static void _DasGenAry_destroy(DasGen* pBase)
{
	DasGenAry* pThis = (DasGenAry*)pBase;
	if(pThis->pAry != NULL) dec_DasAry(pThis->pAry);
	free(pThis);
}

static const das_gen_vt g_vtGenAry = {
	_DasGenAry_eval, _DasGenAry_extShape, _DasGenAry_lengthIn, _DasGenAry_destroy
};

DasGen* new_DasGenAry(DasAry* pAry, int nExtRank, const int8_t* pIdxMap)
{
	if((pAry == NULL)||(nExtRank < 1)||(nExtRank > DASIDX_MAX)||(pIdxMap == NULL)){
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

	ptrdiff_t aAryShape[DASIDX_MAX];
	int nAryRank = DasAry_shape(pAry, aAryShape);

	/* which array indices are consumed by the external map? */
	bool aUsed[DASIDX_MAX] = {false};
	for(int i = 0; i < nExtRank; ++i){
		if(pIdxMap[i] == DASIDX_UNUSED) continue;
		if((pIdxMap[i] < 0)||(pIdxMap[i] >= nAryRank)){
			das_error(DASERR_ARRAY, "Index map entry %d out of range", i);
			return NULL;
		}
		aUsed[(int)pIdxMap[i]] = true;
	}

	/* the rest are the item run; cubic only in v1 */
	size_t uItemElems = 1;
	for(int i = 0; i < nAryRank; ++i){
		if(aUsed[i]) continue;
		if(aAryShape[i] < 1){
			das_error(DASERR_NOTIMP,
				"Ragged INTERNAL extents are not yet handled by DasGenAry "
				"(array %s, index %d)", DasAry_id(pAry), i
			);
			return NULL;
		}
		uItemElems *= (size_t)aAryShape[i];
	}

	DasGenAry* pThis = (DasGenAry*)calloc(1, sizeof(DasGenAry));
	pThis->base.kind = gtArray;
	pThis->base.vt   = &g_vtGenAry;
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

	size_t uElemSz = das_vt_size((das_val_type)pBase->elem);
	if(uElemSz > uRunMax)
		return -1 * das_error(DASERR_ARRAY, "Run buffer too small for sequence");

	switch(pBase->elem){
	case etDouble: case etFloat: {
		double r = (pBase->elem == etDouble) ?
			*((const double*)pThis->aIntercept) :
			(double)*((const float*)pThis->aIntercept);
		for(int i = 0; i < pThis->nExtRank; ++i){
			double rSlope = (pBase->elem == etDouble) ?
				*((const double*)(pThis->aInterval[i])) :
				(double)*((const float*)(pThis->aInterval[i]));
			r += rSlope * (double)pExtLoc[i];
		}
		if(pBase->elem == etDouble) *((double*)pRun) = r;
		else                        *((float*)pRun)  = (float)r;
		return 1;
	}
	case etLong: {
		int64_t n = *((const int64_t*)pThis->aIntercept);
		for(int i = 0; i < pThis->nExtRank; ++i)
			n += *((const int64_t*)(pThis->aInterval[i])) * (int64_t)pExtLoc[i];
		*((int64_t*)pRun) = n;
		return 1;
	}
	default:
		return -1 * das_error(DASERR_NOTIMP,
			"Sequence element type %d not yet implemented", (int)pBase->elem
		);
	}
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
	if((nIdx < 0)||(nIdx >= pThis->nExtRank)) return DASIDX_UNUSED;
	return pThis->aExtShape[nIdx];
}

static void _DasGenSeq_destroy(DasGen* pBase){ free(pBase); }

static const das_gen_vt g_vtGenSeq = {
	_DasGenSeq_eval, _DasGenSeq_extShape, _DasGenSeq_lengthIn, _DasGenSeq_destroy
};

DasGen* new_DasGenSeq(
	das_elem_type et, const ubyte* pIntercept, int nExtRank,
	const ubyte* pIntervals, const ptrdiff_t* pExtShape
){
	if((pIntercept==NULL)||(pIntervals==NULL)||(pExtShape==NULL)||
	   (nExtRank < 1)||(nExtRank > DASIDX_MAX)){
		das_error(DASERR_ARRAY, "Invalid arguments to new_DasGenSeq");
		return NULL;
	}
	if(!_gen_elemOk(et)){
		das_error(DASERR_NOTIMP,
			"Sequence element type %d not yet implemented (etTime arrives "
			"with the var_seq migration)", (int)et
		);
		return NULL;
	}

	size_t uElemSz = das_vt_size((das_val_type)et);

	DasGenSeq* pThis = (DasGenSeq*)calloc(1, sizeof(DasGenSeq));
	pThis->base.kind = gtSeq;
	pThis->base.vt   = &g_vtGenSeq;
	pThis->base.elem = et;
	pThis->base.nRef = 1;
	memcpy(pThis->aIntercept, pIntercept, uElemSz);
	for(int i = 0; i < nExtRank; ++i)
		memcpy(pThis->aInterval[i], pIntervals + (size_t)i*uElemSz, uElemSz);
	pThis->nExtRank = nExtRank;
	memcpy(pThis->aExtShape, pExtShape, sizeof(ptrdiff_t)*(size_t)nExtRank);

	return (DasGen*)pThis;
}

/* ************************************************************************* */
/* gtConst: one value, everywhere                                            */

static int _DasGenConst_eval(
	const DasGen* pBase, const ptrdiff_t* pExtLoc, ubyte* pRun, size_t uRunMax
){
	const DasGenConst* pThis = (const DasGenConst*)pBase;
	(void)pExtLoc;
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
	if((nIdx < 0)||(nIdx >= pThis->nExtRank)) return DASIDX_UNUSED;
	return pThis->aExtShape[nIdx];
}

static void _DasGenConst_destroy(DasGen* pBase){ free(pBase); }

static const das_gen_vt g_vtGenConst = {
	_DasGenConst_eval, _DasGenConst_extShape, _DasGenConst_lengthIn,
	_DasGenConst_destroy
};

DasGen* new_DasGenConst(
	das_elem_type et, const ubyte* pVal, int nExtRank, const ptrdiff_t* pExtShape
){
	if((pVal==NULL)||(pExtShape==NULL)||(nExtRank < 1)||(nExtRank > DASIDX_MAX)){
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
	pThis->base.vt   = &g_vtGenConst;
	pThis->base.elem = et;
	pThis->base.nRef = 1;
	memcpy(pThis->aValue, pVal, das_vt_size((das_val_type)et));
	pThis->nExtRank = nExtRank;
	memcpy(pThis->aExtShape, pExtShape, sizeof(ptrdiff_t)*(size_t)nExtRank);

	return (DasGen*)pThis;
}
