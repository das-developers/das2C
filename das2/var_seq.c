/* Copyright (C) 2017-2024 Chris Piker <chris-piker@uiowa.edu>
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
#include <assert.h>

#include "dataset.h"
#include "vector.h"   /* das_geovec + accessors for vtGeoVec sequences */
#include "stream.h"   /* frame-id -> frame lookup for a vector sequence's frame */

/* ************************************************************************* */
/* Protected functions from the base class */

char* _DasVar_prnUnits(const DasVar* pThis, char* sBuf, int nLen);
char* _DasVar_prnType(const DasVar* pThis, char* pWrite, int nLen);
int _DasVar_noIntrShape(const DasVar* pBase, ptrdiff_t* pShape);
DasStream* _DasVar_getStream(const DasVar* pThis);
void _DasVarVec_encodeFrame(const DasVar* pVar, DasBuf* pBuf);


/* ************************************************************************* */
/* Sequences derived from direct operation on indices */

/* A sequence generates values from a linear function of one OR MORE external
   dataset indexes:

	   value = B + sum_k( M[k] * i[ aDep[k] ] ).

	It's common that the sequence only depends on one index, for example
	frequencies in a common spectrogram depend on `j` alone.

   All slopes share ONE unit (see `interval`): every term adds into the same
	accumulator. */
typedef struct das_var_seq{
	DasVar base;
	int nDeps;                  /* number of dependent indexes, >= 1 */
	int aDep[DASIDX_MAX];       /* the dependent dataset indexes, in map order */
	char sId[DAS_MAX_ID_BUFSZ]; /* Since we can't just use our array ID */

	/* TODO: Replace these with datums */

	ubyte B[DATUM_BUF_SZ];              /* Intercept (value at the all-zero index) */
	ubyte M[DASIDX_MAX][DATUM_BUF_SZ];  /* Slope for each dependent index */

	/* Units of the slopes M.  Derived once at construction as

		  Units_interval(base.units):

		for a plain unit that is the unit itself, for calendar units it's
		depends on the basic `tick` since the epoch (taken to be 1-second for
		UTC units).
		All slopes share this one unit.
	*/
	das_units interval;

} DasVarSeq;

DasVar* copy_DasVarSeq(const DasVar* pBase)
{	
	assert(pBase->vartype == D2V_SEQUENCE); /* Okay to not be present in release code */
	DasVar* pRet = calloc(1, sizeof(DasVarSeq));
	memcpy(pRet, pBase, sizeof(DasVarSeq));
	return pRet;
}

das_val_type DasVarSeq_elemType(const DasVar* pBase)
{
	if(pBase->vartype != D2V_SEQUENCE){
		das_error(DASERR_VAR, "logic error, type not a sequence variable");
		return vtUnknown;
	}
	return pBase->vt;
}

int dec_DasVarSeq(DasVar* pBase){
	pBase->nRef -= 1;
	if(pBase->nRef > 0) return pBase->nRef;
	free(pBase);
	return 0;
}

/* Evaluate a sequence at a full index location into pOut:
 *
 *     value = B + sum_k( M[k] * pLoc[aDep[k]] )
 *
 * Numeric types accumulate via das_value_accum() -- one typed, range-checked,
 * overflow-guarded add per dependent stride.
 *
 * vtTime is the lone heterogeneous case -- a das_time intercept plus slopes in
 * seconds (the value unit is UTC, so Units_interval() makes the slopes seconds)
 * -- so it cannot route through das_value_accum (calendar math needs two value
 * types) and the steps are summed into .second here.
 *
 * pOut must have room for pThis->base.vsize bytes (DATUM_BUF_SZ for das_time).
 */
static bool _DasVarSeq_calc(
	const DasVarSeq* pThis, const ptrdiff_t* pLoc, ubyte* pOut
){
	/* Can't use negative indexes with a sequence because it doesn't know
	   how big it is! */
	for(int k = 0; k < pThis->nDeps; ++k){
		if(pLoc[pThis->aDep[k]] < 0){
			das_error(DASERR_VAR, "Negative indexes undefined for sequences");
			return false;
		}
	}

	if(pThis->base.vt == vtTime){
		*((das_time*)pOut) = *((das_time*)(pThis->B));
		for(int k = 0; k < pThis->nDeps; ++k)
			((das_time*)pOut)->second += *((double*)pThis->M[k]) * pLoc[pThis->aDep[k]];
		dt_tnorm( (das_time*)pOut );
		return true;
	}

	if(pThis->base.vt == vtGeoVec){
		/* B and each slope M[k] are geovecs.  Copying B carries the frame,
		   systype, dirs, et/esize/ncomp AND the intercept values; then accumulate
		   each component independently at the geovec element type.  Components sit
		   esize apart (das_geovec_comp), so das_value_accum lands on the right one. */
		memcpy(pOut, pThis->B, pThis->base.vsize);
		das_geovec* pVec = (das_geovec*)pOut;
		das_val_type et = das_geovec_eltype(pVec);
		for(ubyte c = 0; c < das_geovec_numComp(pVec); ++c){
			for(int k = 0; k < pThis->nDeps; ++k){
				if(das_value_accum(
					et, das_geovec_comp(pVec, c),
					das_geovec_comp((das_geovec*)(pThis->M[k]), c),
					pLoc[pThis->aDep[k]]
				) != DAS_OKAY)
					return false;
			}
		}
		return true;
	}

	memcpy(pOut, pThis->B, pThis->base.vsize);
	for(int k = 0; k < pThis->nDeps; ++k){
		if(das_value_accum(pThis->base.vt, pOut, pThis->M[k], pLoc[pThis->aDep[k]]) != DAS_OKAY)
			return false;
	}
	return true;
}

bool DasVarSeq_get(const DasVar* pBase, ptrdiff_t* pLoc, das_datum* pDatum)
{
	const DasVarSeq* pThis = (const DasVarSeq*)pBase;

	if(pDatum == NULL){
		das_error(DASERR_VAR, "NULL datum pointer");
		return false;
	}

	pDatum->vt = pBase->vt;
	pDatum->vsize = pBase->vsize;
	pDatum->units = pBase->units;

	/* The value occupies the front of the datum's byte storage */
	return _DasVarSeq_calc(pThis, pLoc, (ubyte*)pDatum);
}

bool DasVarSeq_isNumeric(const DasVar* pBase)
{
	return true;   /* Text based sequences have not been implemented */
}

/* Returns the pointer an the next write point of the string */
char* DasVarSeq_expression(
	const DasVar* pBase, char* sBuf, int nLen, unsigned int uFlags
){
	if(nLen < 3) return sBuf;
	
	/* Output is: 
	 *   B + A * i  units    (most sequences) 
	 *   B + A * i s  UTC (time sequences)
	 */
	
	memset(sBuf, 0, nLen);  /* Insure null termination whereever I stop writing */

	const DasVarSeq* pThis = (const DasVarSeq*)pBase;

	int nWrote = strlen(pThis->sId);
	nWrote = nWrote > (nLen - 1) ? nLen - 1 : nWrote;
	char* pWrite = sBuf;
	strncpy(sBuf, pThis->sId, nWrote);

	pWrite = sBuf + nWrote;  nLen -= nWrote;
	int i;

	/* index list: one [x] bracket per dependent index ([j], or [j][k] ...) */
	for(int k = 0; k < pThis->nDeps; ++k){
		if(nLen < 4) return pWrite;
		*pWrite = '['; ++pWrite; --nLen;
		*pWrite = g_sIdxLower[pThis->aDep[k]]; ++pWrite; --nLen;
		*pWrite = ']'; ++pWrite; --nLen;
	}

	/* Print units if desired */
	if(uFlags & D2V_EXP_UNITS){
		char* pNewWrite = _DasVar_prnUnits(pBase, pWrite, nLen);
		nLen -= (pNewWrite - pWrite);
		pWrite = pNewWrite;
	}

	/* Most of the rest is range printing... (with data type at the end) */
	if(! (uFlags & D2V_EXP_RANGE)) return pWrite;

	if(nLen < 4) return pWrite;
	strncpy(pWrite, " | ", 3 /* shutup gcc */ + 1);
	pWrite += 3;
	nLen -= 3;

	das_datum dm;
	dm.units = pThis->base.units;
	dm.vt    = pThis->base.vt;
	dm.vsize = pThis->base.vsize;

	/* intercept B */
	if(pThis->base.vt == vtTime){
		das_time dt = *((das_time*)(pThis->B));
		*((das_time*)&dm) = dt;
		das_datum_toStrValOnly(&dm, pWrite, nLen, (dt.second == 0.0) ? 0 : 5);
	}
	else{
		for(i = 0; i < dm.vsize; ++i) dm.bytes[i] = pThis->B[i];
		das_datum_toStrValOnly(&dm, pWrite, nLen, 5);
	}
	nWrote = strlen(pWrite);
	nLen -= nWrote;
	pWrite += nWrote;

	/* one " + Mk*x" term per dependent index */
	for(int k = 0; k < pThis->nDeps; ++k){
		if(nLen < 4) return pWrite;
		strncpy(pWrite, " + ", 3 /*shutup gcc */ +1);
		pWrite += 3; nLen -= 3;

		if(nLen < 7) return pWrite;
		if(pThis->base.vt == vtTime){
			das_datum_fromDbl(&dm, *((double*)pThis->M[k]), pThis->interval);
		}
		else{
			dm.units = pThis->base.units;
			dm.vt    = pThis->base.vt;
			dm.vsize = pThis->base.vsize;
			for(i = 0; i < dm.vsize; ++i) dm.bytes[i] = pThis->M[k][i];
		}
		das_datum_toStrValOnly(&dm, pWrite, nLen, 5);
		nWrote = strlen(pWrite);
		nLen -= nWrote;
		pWrite += nWrote;

		if(nLen < 3) return pWrite;
		*pWrite = '*'; ++pWrite; --nLen;
		*pWrite = g_sIdxLower[pThis->aDep[k]];  ++pWrite; --nLen;
	}

	if(pBase->units == UNIT_DIMENSIONLESS) return pWrite;
	if( (uFlags & D2V_EXP_UNITS) == 0) return pWrite;

	if(nLen < 3) return pWrite;

	*pWrite = ' '; pWrite += 1;
	nLen -= 1;

	if(uFlags & D2V_EXP_TYPE)
		return _DasVar_prnType((DasVar*)pThis, pWrite, nLen);
	else
		return pWrite;
}

int DasVarSeq_shape(const DasVar* pBase, ptrdiff_t* pShape){
	DasVarSeq* pThis = (DasVarSeq*)pBase;

	for(int i = 0; i < DASIDX_MAX; ++i)
		pShape[i] = DASIDX_UNUSED;
	for(int k = 0; k < pThis->nDeps; ++k)
		pShape[pThis->aDep[k]] = DASIDX_FUNC;
	return 0;
}

/* A vector sequence has one internal index -- the component index, ncomp long.
   Scalar sequences use _DasVar_noIntrShape (rank 0) instead. */
int DasVarSeq_intrShape(const DasVar* pBase, ptrdiff_t* pShape){
	for(int i = 0; i < DASIDX_MAX; ++i)
		pShape[i] = DASIDX_UNUSED;
	if(pBase->vt == vtGeoVec){
		pShape[0] = das_geovec_numComp((das_geovec*)(((const DasVarSeq*)pBase)->B));
		return 1;
	}
	return 0;
}

/* Frame accessors.  Only a vector sequence (vt=vtGeoVec) has a frame; it lives in
   the intercept geovec B, exactly as DasVarVecAry keeps it in its template.  Scalar
   sequences report "no frame".  var_base.c delegates here unconditionally so all
   the frame-or-not logic stays next to the vector code. */
ubyte DasVarSeq_getFrame(const DasVar* pBase)
{
	if(pBase->vt != vtGeoVec) return 0;
	return ((const das_geovec*)(((const DasVarSeq*)pBase)->B))->frame;
}

const char* DasVarSeq_getFrameName(const DasVar* pBase)
{
	if(pBase->vt != vtGeoVec) return NULL;
	const das_geovec* pVec = (const das_geovec*)(((const DasVarSeq*)pBase)->B);
	if(pVec->frame == 0) return NULL;

	DasStream* pStream = _DasVar_getStream(pBase);
	if(pStream == NULL) return NULL;

	const DasFrame* pFrame = DasStream_getFrameById(pStream, pVec->frame);
	if(pFrame == NULL) return NULL;

	return DasFrame_getName(pFrame);
}

ubyte DasVarSeq_vecMap(const DasVar* pBase, ubyte* nDirs, ubyte* pDirs)
{
	if(pBase->vt != vtGeoVec){ if(nDirs) *nDirs = 0; return 0; }

	das_geovec gv = *((const das_geovec*)(((const DasVarSeq*)pBase)->B));
	if(pDirs != NULL){
		if(gv.ncomp > 0) pDirs[0] = gv.dirs & 0x3;
		if(gv.ncomp > 1) pDirs[1] = (gv.dirs >> 2) & 0x3;
		if(gv.ncomp > 2) pDirs[2] = (gv.dirs >> 4) & 0x3;
	}
	*nDirs = gv.ncomp;
	return gv.systype;
}

bool DasVarSeq_setFrame(DasVar* pBase, ubyte nFrameId)
{
	if(pBase->vt != vtGeoVec) return false;
	((das_geovec*)(((DasVarSeq*)pBase)->B))->frame = nFrameId;
	return true;
}

ptrdiff_t DasVarSeq_lengthIn(const DasVar* pBase, int nIdx, ptrdiff_t* pLoc)
{
	/* A sequence is homogenous (not ragged): it is a generator along each of its
	 * dependent indexes and absent everywhere else, so pLoc is immaterial.  
	 * 
	 * Note that sequences *can* be used with ragged datasets as they are
	 * a function that take on the shape of the overall dataset.  The adjacent
	 * ragged arrays will determine the maximum valid sub-index for parent
	 * index.  See das_varindex_merge() for details.
	 */
	DasVarSeq* pThis = (DasVarSeq*)pBase;

	for(int k = 0; k < pThis->nDeps; ++k)
		if(nIdx == pThis->aDep[k]) return DASIDX_FUNC;
	return DASIDX_UNUSED;
}


bool DasVarSeq_isFill(const DasVar* pBase, const ubyte* pCheck, das_val_type vt)
{
	return false;
}

/* NOTES in: variable.md. */
DasAry* DasVarSeq_subset(
	const DasVar* pBase, int nRank, const ptrdiff_t* pMin, const ptrdiff_t* pMax
){
	if(nRank != pBase->nExtRank){
		das_error(
			DASERR_VAR, "External variable is rank %d, but subset specification "
			"is rank %d", pBase->nExtRank, nRank
		);
		return NULL;
	}
	
	DasVarSeq* pThis = (DasVarSeq*)pBase;
	
	size_t shape[DASIDX_MAX] = DASIDX_INIT_BEGIN;
	int nSliceRank = das_rng2shape(nRank, pMin, pMax, shape);
	if(nSliceRank < 1){ 
		das_error(DASERR_VAR, "Can't output a rank 0 array, use DasVar_get() for single points");
		return NULL;
	}
	
	DasAry* pAry = new_DasAry(
		pThis->sId, pBase->vt, 0, NULL, nSliceRank, shape, pBase->units
	);
	if(pAry == NULL) return NULL;

	size_t uSzElm = pBase->vsize, uTotalLen;
	ubyte* pWrite = DasAry_getBuf(pAry, pBase->vt, DIM0, &uTotalLen);

	/* The value is a function only of the dependent indexes, but the requested
	 * subset is a full cube and must be filled cell-by-cell: a value that depends
	 * on more than one index is not constant along any single array dimension, so
	 * the old "compute along one array-dim, block-replicate the rest" shortcut does
	 * not generalize.  Walk every cell of [pMin,pMax) in row-major (last index
	 * fastest) order -- which is the array's own storage order -- and evaluate
	 * _DasVarSeq_calc() at each; non-dependent dimensions simply don't move the 
	 * value. */
	ptrdiff_t loc[DASIDX_MAX];
	for(int d = 0; d < nRank; ++d) loc[d] = pMin[d];

	ubyte value[DATUM_BUF_SZ];
	for(size_t n = 0; n < uTotalLen; ++n){
		if(!_DasVarSeq_calc(pThis, loc, value)){
			dec_DasAry(pAry);
			return NULL;
		}
		memcpy(pWrite, value, uSzElm);
		pWrite += uSzElm;

		for(int d = nRank - 1; d >= 0; --d){      /* row-major increment in range */
			if(++loc[d] < pMax[d]) break;
			loc[d] = pMin[d];
		}
	}

	return pAry;
}

bool DasVarSeq_degenerate(const DasVar* pBase, int iIndex)
{
	DasVarSeq* pThis = (DasVarSeq*)pBase;
	for(int k = 0; k < pThis->nDeps; ++k)
		if(pThis->aDep[k] == iIndex) return false;
	return true;
}


DasVar* new_DasVarSeq(
	const char* sId, das_val_type vt, size_t vSz, const void* pMin, 
	const void* pInterval, int nExtRank, int8_t* pMap, int nIntRank, 
	das_units units
){
	/* A vector sequence (vt=vtGeoVec) carries exactly one internal index, the
	   component index; a scalar sequence has none.  It is also the sole non-simple
	   type a sequence may hold -- its components are still a simple type, stored in
	   the geovec's element slots. */
	bool bVec = (vt == vtGeoVec);

	if((sId == NULL)||((vt == vtUnknown)&&(vSz == 0))||(pMin == NULL)||
	   (pInterval == NULL)||(pMap == NULL)||(nExtRank < 1)||
	   (nIntRank != (bVec ? 1 : 0))
	  ){
		das_error(DASERR_VAR, "Invalid argument");
		return NULL;
	}

	if(!bVec && ((vt < VT_MIN_SIMPLE)||(vt > VT_MAX_SIMPLE))){
		das_error(DASERR_VAR, "Only simple types allowed for sequences");
		return NULL;
	}
	
	DasVarSeq* pThis = (DasVarSeq*)calloc(1, sizeof(DasVarSeq));
	DasDesc_init((DasDesc*)pThis, VARIABLE);
	
	if(!das_assert_valid_id(sId)) return NULL;
	strncpy(pThis->sId, sId, DAS_MAX_ID_BUFSZ - 1);
	
	pThis->base.vartype    = D2V_SEQUENCE;
	pThis->base.vt         = vt;
	pThis->base.vsize      = das_vt_size(vt);
	
	pThis->base.nExtRank   = nExtRank;
	pThis->base.nIntRank   = nIntRank;   /* 1 for a vector sequence, else 0 */
	pThis->base.nRef       = 1;
	pThis->base.units      = units;
	pThis->base.decRef     = dec_DasVarSeq;
	pThis->base.copy       = copy_DasVarSeq;
	pThis->base.isNumeric  = DasVarSeq_isNumeric;
	pThis->base.expression = DasVarSeq_expression;
	pThis->base.incRef     = inc_DasVar;
	pThis->base.get        = DasVarSeq_get;
	pThis->base.shape      = DasVarSeq_shape;
	pThis->base.intrShape  = DasVarSeq_intrShape;
	pThis->base.lengthIn   = DasVarSeq_lengthIn;
	pThis->base.isFill     = DasVarSeq_isFill;
	pThis->base.subset     = DasVarSeq_subset;
	pThis->base.degenerate = DasVarSeq_degenerate;
	pThis->base.elemType   = DasVarSeq_elemType;

	
	/* A sequence depends on one OR MORE dataset external indexes. Record them in
	   map order; pInterval must then supply one slope per dependent index. */
	pThis->nDeps = 0;
	for(int i = 0; i < nExtRank; ++i){
		if(pMap[i] >= 0){
			if(pThis->nDeps >= DASIDX_MAX){
				das_error(DASERR_VAR, "Too many dependent indexes for a sequence");
				free(pThis);
				return NULL;
			}
			pThis->aDep[pThis->nDeps] = i;
			++pThis->nDeps;
		}
	}
	if(pThis->nDeps < 1){
		das_error(DASERR_VAR, "A <sequence> must depend on at least one index");
		free(pThis);
		return NULL;
	}

	/* Default semantic from the value type, in case the caller never calls
	   DasVar_setSemantic().  A vector takes it from the geovec's element type */
	das_val_type vtSem = (vt == vtGeoVec) ? das_geovec_eltype((const das_geovec*)pMin) : vt;
	strncpy(pThis->base.semantic, das_sem_default(vtSem), D2V_MAX_SEM_LEN - 1);

	/* Copy the intercept B and one slope M[k] per dependent index.  For a vtTime
	   sequence the intercept is a das_time but each slope is a real number of
	   seconds, so the intercept and slopes have different element sizes. */
	size_t uBSz = (vt == vtTime) ? sizeof(das_time) : pThis->base.vsize;
	size_t uMSz = (vt == vtTime) ? sizeof(double)   : pThis->base.vsize;
	memcpy(pThis->B, pMin, uBSz);
	for(int k = 0; k < pThis->nDeps; ++k)
		memcpy(pThis->M[k], (const ubyte*)pInterval + (size_t)k * uMSz, uMSz);

	/* The slopes' unit is the interval (duration) unit of the value unit.  For a
	   plain unit that is itself; for a calendar unit it is the matching duration
	   (UTC -> s, TT2000 -> ns).  Derived once, never asked of the caller. */
	pThis->interval = Units_interval(pThis->base.units);

	return (DasVar*)pThis;
}

/* Format a sequence's minval/interval for the header.  These land in XML text,
   so plain reals use %g (pretty, no trailing zeros, consistent with the
   <values> path in codec.c); calendar/time-valued sequences fall back to the
   datum stringifier so they still render as ISO times. */
static void _DasVarSeq_realToStr(
	const ubyte* pVal, das_val_type vt, das_units units, char* sBuf, int nLen
){
	if(((vt == vtFloat)||(vt == vtDouble)) && !Units_haveCalRep(units)){
		double d = (vt == vtFloat) ? (double)(*(const float*)pVal) : *(const double*)pVal;
		snprintf(sBuf, nLen, (vt == vtFloat) ? "%.7g" : "%.15g", d);
	}
	else{
		/* Calendar/time values render through the datum stringifier.  Default to
		   MICROSECOND precision (6 sub-second digits): the fastest instruments in
		   this field cycle at ~1 ms, and microseconds leave room for phase shifts
		   between instruments.  (-1 here rendered whole seconds, dropping the
		   sub-second part of a sequence's start time.) */
		das_datum dm;
		das_datum_init(&dm, (ubyte*)pVal, vt, 0, units);
		das_datum_toStrValOnly(&dm, sBuf, nLen - 1, 6);
	}
}

DasErrCode DasVarSeq_encode(DasVar* pBase, const char* sRole, DasBuf* pBuf)
{
	DasVarSeq* pThis = (DasVarSeq*)pBase;

	/* Sequences mold to the shape of thier container */
	const DasDs* pDs = (const DasDs*) ( ((DasDesc*)pBase)->parent->parent );
	ptrdiff_t aDsShape[DASIDX_MAX] = DASIDX_INIT_UNUSED;
	int nRank = DasDs_shape(pDs, aDsShape);

	char sIndex[128] = {'\0'};
	char* pWrite = sIndex;
	for(int i = 0; i < nRank; ++i){
		if(pWrite - sIndex > 117)
			continue;
		if(i > 0){ *pWrite = ';'; ++pWrite;}

		bool bDep = false;
		for(int k = 0; k < pThis->nDeps; ++k){ if(pThis->aDep[k] == i){ bDep = true; break; } }

		if(!bDep){ *pWrite = '-'; ++pWrite;}
		else{
			/* It is possible for a sequence to run in index 0, though not common */
			if(i == 0){ *pWrite = '*'; ++pWrite;}
			else if(aDsShape[i] == DASIDX_RAGGED){ *pWrite = '*'; ++pWrite; }
			else pWrite += snprintf(pWrite, 11, "%td", aDsShape[i]);
		}
	}

	/* A vector sequence emits a <vector> holding one <sequence> PER COMPONENT --
	   the transpose of how B / M[k] store the components together.  Component c's
	   minval is B[c]; its interval is M[0][c];M[1][c];... (its slope on each
	   dependent index).  This reproduces the input a <sequence>-per-component gave. */
	if(pBase->vt == vtGeoVec){
		das_geovec* pB = (das_geovec*)pThis->B;
		das_val_type et = das_geovec_eltype(pB);
		ubyte nComp     = das_geovec_numComp(pB);

		DasBuf_printf(pBuf,
			"    <vector components=\"%hhu\" use=\"%s\" semantic=\"%s\" index=\"%s\" units=\"%s\"",
			nComp, sRole, pBase->semantic, sIndex, pThis->base.units
		);
		_DasVarVec_encodeFrame(pBase, pBuf);   /* per-vector frame= only if it differs from the dim */
		DasBuf_printf(pBuf, " system=\"%s\" ", das_compsys_str(pB->systype));
		if(das_geovec_hasRefSurf(pB))
			DasBuf_printf(pBuf, " surface=\"%hhu\" ", das_geovec_surfId(pB));
		DasBuf_puts(pBuf, "sysorder=\"");
		for(int i = 0; i < nComp; ++i){
			if(i > 0) DasBuf_puts(pBuf, ";");
			DasBuf_printf(pBuf, "%d", das_geovec_dir(pB, i));
		}
		DasBuf_puts(pBuf, "\">\n");

		if(DasDesc_length((DasDesc*)pBase) > 0){
			int nRet = DasDesc_encode3((DasDesc*)pBase, pBuf, "      ");
			if(nRet != DAS_OKAY) return nRet;
		}

		for(ubyte c = 0; c < nComp; ++c){
			char sBufB[64] = {'\0'};
			_DasVarSeq_realToStr(das_geovec_comp(pB, c), et, pBase->units, sBufB, 64);

			char sInterval[256] = {'\0'};
			char* pIv = sInterval;
			for(int k = 0; k < pThis->nDeps; ++k){
				char sBufM[64] = {'\0'};
				_DasVarSeq_realToStr(
					das_geovec_comp((das_geovec*)pThis->M[k], c), et, pThis->interval, sBufM, 64
				);
				if(k > 0){ *pIv = ';'; ++pIv; }
				int n = snprintf(pIv, sInterval + sizeof(sInterval) - pIv, "%s", sBufM);
				if(n > 0) pIv += n;
			}
			DasBuf_printf(pBuf, "      <sequence minval=\"%s\" interval=\"%s\" />\n",
				sBufB, sInterval
			);
		}
		DasBuf_puts(pBuf, "    </vector>\n");
		return DAS_OKAY;
	}

	const char* sStorage = das_vt_toStr(pBase->vt);
	if(strcmp(sStorage, "das_time") == 0) sStorage = "struct";

	DasBuf_printf(pBuf,
		"    <scalar use=\"%s\" semantic=\"%s\" storage=\"%s\" index=\"%s\" units=\"%s\">\n",
		sRole, pBase->semantic, sStorage, sIndex, pThis->base.units
	);

	/* 4. Write any properties, make sure a generic summary is included */
	if(DasDesc_length((DasDesc*)pBase) > 0){
		int nRet = DasDesc_encode3((DasDesc*)pBase, pBuf, "      ");
		if(nRet != DAS_OKAY) return nRet;
	}

	assert(pBase->vsize <= DATUM_BUF_SZ);

	char sBufB[64] = {'\0'};
	_DasVarSeq_realToStr(pThis->B, pBase->vt, pBase->units, sBufB, 64);

	/* One interval value per dependent index, ';'-separated.  For a time sequence
	   the steps are reals in the interval unit (seconds), not calendar times, so
	   render them as that real type, not as pBase->vt. */
	das_val_type vtStep = (pBase->vt == vtTime) ? vtDouble : pBase->vt;
	char sInterval[256] = {'\0'};
	char* pIv = sInterval;
	for(int k = 0; k < pThis->nDeps; ++k){
		char sBufM[64] = {'\0'};
		_DasVarSeq_realToStr(pThis->M[k], vtStep, pThis->interval, sBufM, 64);
		if(k > 0){ *pIv = ';'; ++pIv; }
		int n = snprintf(pIv, sInterval + sizeof(sInterval) - pIv, "%s", sBufM);
		if(n > 0) pIv += n;
	}

	DasBuf_printf(pBuf, "      <sequence minval=\"%s\" interval=\"%s\" />\n",
		sBufB, sInterval
	);
	DasBuf_puts(pBuf, "    </scalar>\n");

	return DAS_OKAY;
}
