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

/* shape-lattice merges live in set.c (declared in set.h, which includes this
   header); local declarations avoid an upward include */
void das_varindex_merge(int nRank, ptrdiff_t* pDest, ptrdiff_t* pSrc);
ptrdiff_t das_varlength_merge(ptrdiff_t nLeft, ptrdiff_t nRight);

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

const ubyte* DasGen_at(
	const DasGen* pThis, const ptrdiff_t* pExtLoc, size_t* pCount
){
	return pThis->vt->at(pThis, pExtLoc, pCount);
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

	/* lock down the indices BEFORE nIdx, then report the count along nIdx */
	ptrdiff_t aAryLoc[DASIDX_MAX] = DASIDX_INIT_BEGIN;
	int nIndexes = 0;
	for(int i = 0; i < nIdx; ++i){
		if(pThis->idxmap[i] < 0) continue;
		aAryLoc[(int)pThis->idxmap[i]] = pLoc[i];
		++nIndexes;
	}

	/* a source that does not run along index nIdx has no length there, and
	   must say so rather than answer for an index it does not own */
	if((nIdx >= pThis->nExtRank)||(pThis->idxmap[nIdx] < 0))
		return DASIDX_UNUSED;

	return (ptrdiff_t)DasAry_lengthIn(pThis->pAry, nIndexes, aAryLoc);
}

static const ubyte* _DasGenAry_at(
	const DasGen* pBase, const ptrdiff_t* pExtLoc, size_t* pCount
){
	const DasGenAry* pThis = (const DasGenAry*)pBase;

	ptrdiff_t aAryLoc[DASIDX_MAX] = DASIDX_INIT_BEGIN;
	int nMapped = 0;
	for(int i = 0; i < pThis->nExtRank; ++i){
		if(pThis->idxmap[i] == DASIDX_UNUSED) continue;
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

static void _DasGenAry_destroy(DasGen* pBase)
{
	DasGenAry* pThis = (DasGenAry*)pBase;
	if(pThis->pAry != NULL) dec_DasAry(pThis->pAry);
	free(pThis);
}

static const das_gen_vt g_vtGenAry = {
	_DasGenAry_eval, _DasGenAry_extShape, _DasGenAry_lengthIn, _DasGenAry_at,
	_DasGenAry_destroy
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
	if((nIdx < 0)||(nIdx >= pThis->nExtRank)) return DASIDX_UNUSED;
	return pThis->aExtShape[nIdx];
}

static void _DasGenSeq_destroy(DasGen* pBase){ free(pBase); }

static const das_gen_vt g_vtGenSeq = {
	_DasGenSeq_eval, _DasGenSeq_extShape, _DasGenSeq_lengthIn, _DasGen_atNone,
	_DasGenSeq_destroy
};

DasGen* new_DasGenSeqN(
	das_elem_type et, int nComps, const ubyte* pIntercepts, int nExtRank,
	const ubyte* pIntervals, const ptrdiff_t* pExtShape
){
	if((pIntercepts==NULL)||(pIntervals==NULL)||(pExtShape==NULL)||
	   (nExtRank < 1)||(nExtRank > DASIDX_MAX)||
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
	pThis->base.vt   = &g_vtGenSeq;
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
	_DasGen_atNone, _DasGenConst_destroy
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
			return -1 * das_error(DASERR_VAR,
				"Can't scale element type %d for the operation", (int)etR);
		rVal *= pThis->rRightScale;
		memcpy(aR, &rVal, sizeof(double));
		etR = etDouble;
	}

	if(!pThis->apply(DasGen_elemType(pThis->pLeft), etR, aL, aR, pRun))
		return -1 * das_error(DASERR_VAR, "Operand elements not supported");
	return 1;
}

static int _DasGenOp_extShape(const DasGen* pBase, ptrdiff_t* pShape)
{
	const DasGenOp* pThis = (const DasGenOp*)pBase;

	ptrdiff_t aRight[DASIDX_MAX];
	for(int i = 0; i < DASIDX_MAX; ++i){
		pShape[i] = DASIDX_UNUSED; aRight[i] = DASIDX_UNUSED;
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

static const das_gen_vt g_vtGenOp = {
	_DasGenOp_eval, _DasGenOp_extShape, _DasGenOp_lengthIn, _DasGen_atNone,
	_DasGenOp_destroy
};

DasGen* new_DasGenBinop(
	das_gen_applyfn apply, das_elem_type etOut, DasGen* pLeft, DasGen* pRight,
	double rRightScale
){
	if((apply == NULL)||(pLeft == NULL)||(pRight == NULL)){
		das_error(DASERR_VAR, "Invalid arguments to new_DasGenBinop");
		return NULL;
	}
	DasGenOp* pThis = (DasGenOp*)calloc(1, sizeof(DasGenOp));
	pThis->base.kind = gtBinop;
	pThis->base.vt   = &g_vtGenOp;
	pThis->base.elem = etOut;
	pThis->base.nRef = 1;
	pThis->apply  = apply;
	pThis->rRightScale = rRightScale;
	pThis->pLeft  = pLeft;   DasGen_incRef(pLeft);
	pThis->pRight = pRight;  DasGen_incRef(pRight);
	return (DasGen*)pThis;
}
