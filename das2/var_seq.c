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

/* ************************************************************************* */
/* Protected functions from the base class */

char* _DasVar_prnUnits(const DasVar* pThis, char* sBuf, int nLen);
char* _DasVar_prnType(const DasVar* pThis, char* pWrite, int nLen);
int _DasVar_noIntrShape(const DasVar* pBase, ptrdiff_t* pShape);


/* ************************************************************************* */
/* Sequences derived from direct operation on indices */

/* NOTE: single dependent index only for now (one iDep).  A future sequence may
   need to depend on more than one index -- generalize to a set of dependent
   indexes if that day comes. */
typedef struct das_var_seq{
	DasVar base;
	int iDep;      /* The one and only index I depend on */
	char sId[DAS_MAX_ID_BUFSZ];  /* Since we can't just use our array ID */
	
	/* TODO: Replace these with datums */

	ubyte B[DATUM_BUF_SZ];  /* Intercept */
	ubyte* pB;

	ubyte M[DATUM_BUF_SZ];  /* Slope */
	ubyte* pM;

	/* Units of the slope M.  Derived once at construction as
	   Units_interval(base.units): for a plain unit that is the unit itself, for a
	   calendar/epoch unit it is the matching duration unit (UTC -> s, TT2000 ->
	   ns, US2000 -> us, MJ1958 -> days).  Cached here so the per-access path never
	   re-runs the Units_interval() dispatch.  For a vtTime sequence this is the
	   unit of the seconds-style step added to the das_time intercept. */
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

/* Evaluate a sequence at one (already index-resolved) position into pOut.
 *
 * Numeric types are B + M*idx, with das_value_accum() doing the typed,
 * range-checked arithmetic (one place, all widths, overflow guarded).
 *
 * vtTime is the lone heterogeneous case -- a das_time intercept plus a slope in
 * seconds (the value unit is UTC, so Units_interval() makes the slope seconds) --
 * so it cannot route through das_value_accum (calendar math needs two value
 * types) and is computed here.
 *
 * pOut must have room for pThis->base.vsize bytes (DATUM_BUF_SZ for das_time).
 */
static bool _DasVarSeq_calc(const DasVarSeq* pThis, ptrdiff_t idx, ubyte* pOut)
{
	/* Can't use negative indexes with a sequence because it doesn't know
	   how big it is! */
	if(idx < 0){
		das_error(DASERR_VAR, "Negative indexes undefined for sequences");
		return false;
	}

	if(pThis->base.vt == vtTime){
		*((das_time*)pOut) = *((das_time*)(pThis->pB));
		((das_time*)pOut)->second += *( (double*)pThis->pM ) * idx;
		dt_tnorm( (das_time*)pOut );
		return true;
	}

	memcpy(pOut, pThis->pB, pThis->base.vsize);
	return (das_value_accum(pThis->base.vt, pOut, pThis->pM, idx) == DAS_OKAY);
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
	return _DasVarSeq_calc(pThis, pLoc[pThis->iDep], (ubyte*)pDatum);
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
	if(nLen < 4) return pWrite;
	int i;
	
	*pWrite = '['; ++pWrite; --nLen;
	*pWrite = g_sIdxLower[pThis->iDep]; ++pWrite; --nLen;
	*pWrite = ']'; ++pWrite; --nLen;
	
	/* Print units if desired */
	if(uFlags & D2V_EXP_UNITS){
		char* pNewWrite = _DasVar_prnUnits(pBase, pWrite, nLen);
		nLen -= (pNewWrite - pWrite);
		pWrite = pNewWrite;
	}
	
	/* Most of the rest is range printing... (with data type at the end) */
	if(! (uFlags & D2V_EXP_RANGE)) return pWrite;
	
	if(nLen < 3) return pWrite;
	strncpy(pWrite, " | ", 3 /* shutup gcc */ + 1);
	pWrite += 3;
	nLen -= 3;
	
	das_datum dm;
	dm.units = pThis->base.units;
	dm.vt    = pThis->base.vt;
	dm.vsize = pThis->base.vsize;
	
	das_time dt;
	int nFracDigit = 5;
	if(pThis->base.vt == vtTime){
		dt = *((das_time*)(pThis->pB));
		*((das_time*)&dm) = dt;
		if(dt.second == 0.0) nFracDigit = 0;
		das_datum_toStrValOnly(&dm, pWrite, nLen, nFracDigit);
	}
	else{
		for(i = 0; i < dm.vsize; ++i) dm.bytes[i] = pThis->pB[i];
		das_datum_toStrValOnly(&dm, pWrite, nLen, 5);
	}
	
	nWrote = strlen(pWrite);
	nLen -= nWrote;
	pWrite += nWrote;
	
	if(nLen < 3) return pWrite;
	strncpy(pWrite, " + ", 3 /*shutup gcc */ +1);
	pWrite += 3; nLen -= 3;
	
	if(nLen < 7) return pWrite;
	
	if(pThis->base.vt == vtTime){
		das_datum_fromDbl(&dm, *((double*)pThis->pM), UNIT_SECONDS);
	}
	else{
		for(i = 0; i < dm.vsize; ++i) dm.bytes[i] = pThis->pM[i];
	}
	
	das_datum_toStrValOnly(&dm, pWrite, nLen, 5);
	nWrote = strlen(pWrite);
	nLen -= nWrote;
	pWrite += nWrote;
	
	if(nLen < 3) return pWrite;
	*pWrite = '*'; ++pWrite; --nLen;
	*pWrite = g_sIdxLower[pThis->iDep];  ++pWrite; --nLen;
	
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
		pShape[i] = pThis->iDep == i ? DASIDX_FUNC : DASIDX_UNUSED;
	return 0;
}

ptrdiff_t DasVarSeq_lengthIn(const DasVar* pBase, int nIdx, ptrdiff_t* pLoc)
{
	/* A simple sequence is homogenous (not ragged): it is a generator along its
	 * one dependent index and absent everywhere else, so pLoc is immaterial.
	 * Mirror DasVarSeq_shape() exactly -- FUNC along iDep, UNUSED otherwise.
	 * "lengthIn(nIdx)" asks for the length ALONG index nIdx, so the test is
	 * nIdx == iDep, not iDep + 1. */
	DasVarSeq* pThis = (DasVarSeq*)pBase;

	if(nIdx == pThis->iDep) return DASIDX_FUNC;
	else return DASIDX_UNUSED;
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
	
	/* We are expanding a 1-D item.  If my dependent index is not the last one
	 * then each value will be copied multiple times.  If my dependent index
	 * is not the first one, then each complete set will be copied multiple
	 * times.  
	 * 
	 * Since this is only dependent on a single axis, there is only a need
	 * for a loop in that one axis. */
	
	
	size_t uMin = pMin[pThis->iDep];
	size_t uMax = pMax[pThis->iDep];
	
	size_t u, uSzElm = pBase->vsize;
	
	size_t uRepEach = 1;
	for(int d = pThis->iDep + 1; d < pBase->nExtRank; ++d) 
		uRepEach *= (pMax[d] - pMin[d]);
	
	size_t uBlkCount = (pMax[pThis->iDep] - pMin[pThis->iDep]) * uRepEach;
	size_t uBlkBytes = uBlkCount * uSzElm;
	
	size_t uRepBlk = 1; 
	for(int d = 0; d < pThis->iDep; ++d) 
		uRepBlk *= (pMax[d] - pMin[d]);
	
	ubyte value[DATUM_BUF_SZ];
	size_t uTotalLen;        /* Used to check */
	ubyte* pWrite = DasAry_getBuf(pAry, pBase->vt, DIM0, &uTotalLen);
	
	if(uTotalLen != uRepBlk * uBlkCount){
		das_error(DASERR_VAR, "Logic error in sequence copy");
		dec_DasAry(pAry);
		return NULL;
	}
	
	/* One value per dependent-index position, each replicated across the trailing
	 * (uRepEach) axes.  The per-element calc is centralized in _DasVarSeq_calc, so
	 * the type switch lives in one place (das_value_accum) instead of being
	 * open-coded here, in DasVarSeq_get(), and in the constructor. */
	size_t uWriteInc = uRepEach * uSzElm;

	for(u = uMin; u < uMax; ++u){
		if(!_DasVarSeq_calc(pThis, (ptrdiff_t)u, value)){
			dec_DasAry(pAry);
			return NULL;
		}
		das_memset(pWrite, value, uSzElm, uRepEach);
		pWrite += uWriteInc;
	}
	
	/* Now replicate the whole blocks if needed */
	if(uRepBlk > 1)
		das_memset(pWrite + uBlkBytes, pWrite, uBlkBytes, uRepBlk-1);
	
	return pAry;
}

bool DasVarSeq_degenerate(const DasVar* pBase, int iIndex)
{
	DasVarSeq* pThis = (DasVarSeq*)pBase;
	if(pThis->iDep == iIndex) 
		return false;
	else
		return true;
}


DasVar* new_DasVarSeq(
	const char* sId, das_val_type vt, size_t vSz, const void* pMin, 
	const void* pInterval, int nExtRank, int8_t* pMap, int nIntRank, 
	das_units units
){
	if((sId == NULL)||((vt == vtUnknown)&&(vSz == 0))||(pMin == NULL)||
	   (pInterval == NULL)||(pMap == NULL)||(nExtRank < 1)||(nIntRank > 0)
	  ){
		das_error(DASERR_VAR, "Invalid argument");
		return NULL;
	}
	
	if((vt < VT_MIN_SIMPLE)||(vt > VT_MAX_SIMPLE)){
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
	pThis->base.nRef       = 1;
	pThis->base.units      = units;
	pThis->base.decRef     = dec_DasVarSeq;
	pThis->base.copy       = copy_DasVarSeq;
	pThis->base.isNumeric  = DasVarSeq_isNumeric;
	pThis->base.expression = DasVarSeq_expression;
	pThis->base.incRef     = inc_DasVar;
	pThis->base.get        = DasVarSeq_get;
	pThis->base.shape      = DasVarSeq_shape;
	pThis->base.intrShape  = _DasVar_noIntrShape;
	pThis->base.lengthIn   = DasVarSeq_lengthIn;
	pThis->base.isFill     = DasVarSeq_isFill;
	pThis->base.subset     = DasVarSeq_subset;
	pThis->base.degenerate = DasVarSeq_degenerate;
	pThis->base.elemType   = DasVarSeq_elemType;

	
	/* A simple sequence is a single-stride generator, B + i*M, that varies along
	   exactly ONE dataset axis (iDep), so the index map must mark exactly one used
	   axis (ordinal >= 0).  A map with more than one used axis would be a
	   multi-index (multi-stride) sequence, a distinct variable class that does not
	   exist yet; reject it rather than silently honoring just one of the axes. */
	pThis->iDep = -1;
	int nDeps = 0;
	for(int i = 0; i < nExtRank; ++i){
		if(pMap[i] >= 0){
			++nDeps;
			pThis->iDep = i;
		}
	}
	if(nDeps != 1){
		das_error(DASERR_VAR,
			"A simple <sequence> must depend on exactly one index, but its index "
			"map uses %d.  Multi-index (multi-stride) sequences are not yet "
			"supported.", nDeps
		);
		free(pThis);
		return NULL;
	}
	
	pThis->pB = pThis->B;
	pThis->pM = pThis->M;

	/* assume integer, till poven different */
	strncpy(pThis->base.semantic, DAS_SEM_INT, D2V_MAX_SEM_LEN -1);
	
	switch(vt){
	case vtUByte: 
		*(pThis->pB) = *((ubyte*)pMin);  *(pThis->pM) = *((ubyte*)pInterval);
		break;
	case vtUShort:
		*((uint16_t*)(pThis->pB)) = *((uint16_t*)pMin);  
		*((uint16_t*)(pThis->pM)) = *((uint16_t*)pInterval);
		break;
	case vtShort:
		*((int16_t*)(pThis->pB)) = *((int16_t*)pMin);  
		*((int16_t*)(pThis->pM)) = *((int16_t*)pInterval);
		break;
	case vtUInt:
		*((uint32_t*)(pThis->pB)) = *((uint32_t*)pMin);  
		*((uint32_t*)(pThis->pM)) = *((uint32_t*)pInterval);
		break;
	case vtInt:
		*((int32_t*)(pThis->pB)) = *((int32_t*)pMin);  
		*((int32_t*)(pThis->pM)) = *((int32_t*)pInterval);
		break;
	case vtULong:
		*((uint64_t*)(pThis->pB)) = *((uint64_t*)pMin);  
		*((uint64_t*)(pThis->pM)) = *((uint64_t*)pInterval);
		break;
	case vtLong:
		*((int64_t*)(pThis->pB)) = *((int64_t*)pMin);  
		*((int64_t*)(pThis->pM)) = *((int64_t*)pInterval);
		break;
	case vtFloat:
		*((float*)(pThis->pB)) = *((float*)pMin);  
		*((float*)(pThis->pM)) = *((float*)pInterval);
		strncpy(pThis->base.semantic, DAS_SEM_REAL, D2V_MAX_SEM_LEN -1);
		break;
	case vtDouble:
		*((double*)(pThis->pB)) = *((double*)pMin);  
		*((double*)(pThis->pM)) = *((double*)pInterval);
		strncpy(pThis->base.semantic, DAS_SEM_REAL, D2V_MAX_SEM_LEN -1);
		break;
	case vtTime:
		/* Intercept is the start das_time; the step is a real already in this
		   sequence's interval unit -- Units_interval(units), seconds for UTC -- so
		   no conversion is needed here (the das_time arithmetic in _DasVarSeq_calc
		   adds it to .second directly).  base.units stays the value unit (UTC). */
		*((das_time*)pThis->pB) = *((das_time*)pMin);
		*((double*)(pThis->pM)) = *((double*)pInterval);
		strncpy(pThis->base.semantic, DAS_SEM_DATE, D2V_MAX_SEM_LEN -1);
		break;
	default:
		das_error(DASERR_VAR, "Value type %d not yet supported for sequences", vt);
		free(pThis);
		return NULL;
	}

	/* The slope's unit is the interval (duration) unit of the value unit.  For a
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
		das_datum dm;
		das_datum_init(&dm, (ubyte*)pVal, vt, 0, units);
		das_datum_toStrValOnly(&dm, sBuf, nLen - 1, -1);
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
		if(i != pThis->iDep){ *pWrite = '-'; ++pWrite;}
		else{
			/* It is possible for a sequence to run in index 0, though not common */
			if(i == 0){ *pWrite = '*'; ++pWrite;}
			else if(aDsShape[i] == DASIDX_RAGGED){ *pWrite = '*'; ++pWrite; }
			else pWrite += snprintf(pWrite, 11, "%td", aDsShape[i]);
		}
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
	char sBufM[64] = {'\0'};
	_DasVarSeq_realToStr(pThis->pB, pBase->vt, pBase->units, sBufB, 64);
	_DasVarSeq_realToStr(pThis->pM, pBase->vt, pBase->units, sBufM, 64);

	DasBuf_printf(pBuf, "      <sequence minval=\"%s\" interval=\"%s\" />\n",
		sBufB, sBufM
	);
	DasBuf_puts(pBuf, "    </scalar>\n");
	
	return DAS_OKAY;
}