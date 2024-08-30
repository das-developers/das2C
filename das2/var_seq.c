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

typedef struct das_var_seq{
	DasVar base;
	int iDep;      /* The one and only index I depend on */
	char sId[DAS_MAX_ID_BUFSZ];  /* Since we can't just use our array ID */
	
	/* TODO: Replace these with datums */

	ubyte B[DATUM_BUF_SZ];  /* Intercept */
	ubyte* pB;

	ubyte M[DATUM_BUF_SZ];  /* Slope */
	ubyte* pM;
	
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

bool DasVarSeq_get(const DasVar* pBase, ptrdiff_t* pLoc, das_datum* pDatum)
{
	const DasVarSeq* pThis = (const DasVarSeq*)pBase;
	
	if(pDatum == NULL){
		das_error(DASERR_VAR, "NULL datum pointer");
		return false;
	}
	
	/* Can't use negative indexes with a sequence because it doesn't know
	   how big it is! */
	
	if(pLoc[pThis->iDep] < 0){
		das_error(DASERR_VAR, "Negative indexes undefined for sequences");
		return false;
	}
	
	pDatum->vt = pBase->vt;
	pDatum->vsize = pBase->vsize;
	pDatum->units = pBase->units;
	
	size_t u = (size_t)(pLoc[pThis->iDep]);
			  
	/* casting to smaller types is well defined for unsigned values only 
	 * according to the C standards that I can't read because they cost money.
	 * (why have a standard and hide it behind a paywall?) */
			  
	switch(pThis->base.vt){
	case vtUByte: 
		*((ubyte*)pDatum) = *(pThis->pM) * ((ubyte)u) + *(pThis->pB);
		return true;
	case vtUShort:
		*((uint16_t*)pDatum) = *( (uint16_t*)pThis->pM) * ((uint16_t)u) + 
		                       *( (uint16_t*)pThis->pB);
		return true;
	case vtShort:
		if(u > 32767ULL){
			das_error(DASERR_VAR, "Range error, max index for vtShort sequence is 32,767");
			return false;
		}
		*((int16_t*)pDatum) = *( (int16_t*)pThis->pM) * ((int16_t)u) + 
		                      *( (int16_t*)pThis->pB);
		return true;
	case vtUInt:
		if(u > 4294967295LL){
			das_error(DASERR_VAR, "Range error max index for vtInt sequence is 2,147,483,647");
			return false;
		}
		*((uint32_t*)pDatum) = *( (uint32_t*)pThis->pM) * ((uint32_t)u) + 
		                       *( (uint32_t*)pThis->pB);
		return true;
	case vtInt:
		if(u > 2147483647LL){
			das_error(DASERR_VAR, "Range error max index for vtInt sequence is 2,147,483,647");
			return false;
		}
		*((int32_t*)pDatum) = *( (int32_t*)pThis->pM) * ((int32_t)u) + 
		                      *( (int32_t*)pThis->pB);
		return true;
	case vtULong:
		*((uint64_t*)pDatum) = *( (uint64_t*)pThis->pM) * ((int64_t)u) + 
		                       *( (uint64_t*)pThis->pB);
		return true;
	case vtLong:
		*((int64_t*)pDatum) = *( (int64_t*)pThis->pM) * ((int64_t)u) + 
		                      *( (int64_t*)pThis->pB);
		return true;
	case vtFloat:
		*((float*)pDatum) = *( (float*)pThis->pM) * u + *( (float*)pThis->pB);
		return true;
	case vtDouble:
		*((double*)pDatum) = *( (double*)pThis->pM) * u + *( (double*)pThis->pB);
		return true;
	case vtTime:
		/* Here assume that the intercept is a dastime, then add the interval.
		 * The constructor saves the interval in seconds using the units value */
		*((das_time*)pDatum) = *((das_time*)(pThis->pB));
		((das_time*)pDatum)->second += *( (double*)pThis->pM ) * u;
		dt_tnorm( (das_time*)pDatum );
		return true;
	default:
		das_error(DASERR_VAR, "Unknown data type %d", pThis->base.vt);
		return false;	
	}
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
	/* The location works on the directed graph assumption.  Since simple
	 * sequences are homogenous in index space (i.e. not ragged) then we
	 * only actually care about the number of indices specified.  If it's
	 * not equal to our dependent index then it's immaterial what pLoc is.
	 */
	DasVarSeq* pThis = (DasVarSeq*)pBase;
	
	if(nIdx == (pThis->iDep + 1)) return DASIDX_FUNC;
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
	ubyte* pVal = value;
	size_t uTotalLen;        /* Used to check */ 	
	ubyte* pWrite = DasAry_getBuf(pAry, pBase->vt, DIM0, &uTotalLen);
	
	if(uTotalLen != uRepBlk * uBlkCount){
		das_error(DASERR_VAR, "Logic error in sequence copy");
		dec_DasAry(pAry);
		return NULL;
	}
	
	size_t uWriteInc = 0;
	
	/* I know it's messier, but put the switch on the outside so we don't hit 
	 * it on each loop iteration */
	uWriteInc = uRepEach * uSzElm;
	
	switch(pThis->base.vt){
	case vtUByte:	
		for(u = uMin; u < uMax; ++u){
			/* The Calc */
			*pVal = *(pThis->pM) * ((ubyte)u) + *(pThis->pB); 
			
			das_memset(pWrite, pVal, uSzElm, uRepEach);
			pWrite += uWriteInc;
		}
		break;
	
	case vtUShort:
		for(u = uMin; u < uMax; ++u){
			 /* The Calc */
			*((uint16_t*)pVal) = *( (uint16_t*)pThis->pM) * ((uint16_t)u) + 
		                        *( (uint16_t*)pThis->pB);
			
			das_memset(pWrite, pVal, uSzElm, uRepEach);
			pWrite += uWriteInc;
		}
		break;
	
	case vtShort:
		for(u = uMin; u < uMax; ++u){
			if(u > 32767UL){
				das_error(DASERR_VAR, "Range error, max index for vtShort sequence is 32,767");
				dec_DasAry(pAry);
				return false;
			}
			/* The Calc */
			*((int16_t*)pVal) = *( (int16_t*)pThis->pM) * ((int16_t)u) + 
			                    *( (int16_t*)pThis->pB);
			das_memset(pWrite, pVal, uSzElm, uRepEach);
			pWrite += uWriteInc;
		}
		break;
		
	case vtUInt:
		for(u = uMin; u < uMax; ++u){
			if(u > 4294967295UL){
				das_error(DASERR_VAR, "Range error max index for vtInt sequence is 2,147,483,647");
				dec_DasAry(pAry);
				return false;
			}
			/* The Calc */
			*((int32_t*)pVal) = *( (uint32_t*)pThis->pM) * ((uint32_t)u) + 
				                 *( (uint32_t*)pThis->pB);
			das_memset(pWrite, pVal, uSzElm, uRepEach);
			pWrite += uWriteInc;
		}
		break;
	case vtInt:
		for(u = uMin; u < uMax; ++u){
			if(u > 2147483647UL){
				das_error(DASERR_VAR, "Range error max index for vtInt sequence is 2,147,483,647");
				dec_DasAry(pAry);
				return false;
			}
			/* The Calc */
			*((int32_t*)pVal) = *( (int32_t*)pThis->pM) * ((int32_t)u) + 
				                 *( (int32_t*)pThis->pB);
			das_memset(pWrite, pVal, uSzElm, uRepEach);
			pWrite += uWriteInc;
		}
		break;
	case vtULong:
		for(u = uMin; u < uMax; ++u){
			/* The Calc */
			*((uint64_t*)pVal) = *( (uint64_t*)pThis->pM) * ((uint64_t)u) + 
		                       *( (uint64_t*)pThis->pB);
			das_memset(pWrite, pVal, uSzElm, uRepEach);
			pWrite += uWriteInc;
		}
		break;
	case vtLong:
		for(u = uMin; u < uMax; ++u){
			/* The Calc */
			*((int64_t*)pVal) = *( (int64_t*)pThis->pM) * ((int64_t)u) + 
		                       *( (int64_t*)pThis->pB);
			das_memset(pWrite, pVal, uSzElm, uRepEach);
			pWrite += uWriteInc;
		}
		break;
		
	case vtFloat:
		for(u = uMin; u < uMax; ++u){
			/* The Calc */
			*((float*)pVal) = *( (float*)pThis->pM) * u + *( (float*)pThis->pB);
			das_memset(pWrite, pVal, uSzElm, uRepEach);
			pWrite += uWriteInc;
		}
		break;
		
	case vtDouble:
		for(u = uMin; u < uMax; ++u){
			/* The Calc */
			*((double*)pVal) = *( (double*)pThis->pM) * u + *( (double*)pThis->pB);
			das_memset(pWrite, pVal, uSzElm, uRepEach);
			pWrite += uWriteInc;
		}
		break;
		
	case vtTime:
		for(u = uMin; u < uMax; ++u){
			
			/* The Calc */
			*((das_time*)pVal) = *((das_time*)(pThis->pB));
			((das_time*)pVal)->second += *( (double*)pThis->pM ) * u;
			dt_tnorm( (das_time*)pVal );
			
			das_memset(pWrite, pVal, uSzElm, uRepEach);
			pWrite += uWriteInc;
		}
		break;
	
	default:
		das_error(DASERR_VAR, "Unknown data type %d", pBase->vt);
		dec_DasAry(pAry);
		return false;
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
	
	if((vt < VT_MIN_SIMPLE)||(vt > VT_MAX_SIMPLE)||(vt == vtTime)){
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

	
	pThis->iDep = -1;
	for(int i = 0; i < nExtRank; ++i){
		if(pMap[i] == 0){
			if(pThis->iDep != -1){
				das_error(DASERR_VAR, "Simple sequence can only depend on one axis");
				free(pThis);
				return NULL;
			}
			pThis->iDep = i;
		}
	}
	if(pThis->iDep < 0){
		das_error(DASERR_VAR, "Invalid dependent axis map");
		free(pThis);
		return NULL;
	}
	
	pThis->pB = pThis->B;
	pThis->pM = pThis->M;
	double rScale;

	pThis->base.semantic = D2V_SEM_INT; /* assume integer, till poven different */
	
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
		pThis->base.semantic = D2V_SEM_REAL;
		break;
	case vtDouble:
		*((double*)(pThis->pB)) = *((double*)pMin);  
		*((double*)(pThis->pM)) = *((double*)pInterval);
		pThis->base.semantic = D2V_SEM_REAL;
		break;
	case vtTime:
		/* Use the units to get the conversion factor for the slope to seconds */
		/* The save the units as UTC */
		rScale = Units_convertTo(UNIT_SECONDS, *((double*)pInterval), units);
		*((double*)(pThis->pM)) = rScale * *((double*)pInterval);
		pThis->base.units = UNIT_UTC;
		
		*((das_time*)pThis->pB) = *((das_time*)pMin);
		pThis->base.semantic = D2V_SEM_DATE;
		break;
	default:
		das_error(DASERR_VAR, "Value type %d not yet supported for sequences", vt);
		free(pThis);
		return NULL;
	}
	return (DasVar*)pThis;
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
			else pWrite += snprintf(pWrite, 11, "%td", aDsShape[i]); 
		}
	}

	const char* sStorage = das_vt_toStr(pBase->vt);
	if(strcmp(sStorage, "das_time") == 0) sStorage = "struct";
	
	DasBuf_printf(pBuf, 
		"    <scalar use=\"%s\" semantic=\"%s\" storage=\"%s\" index=\"%s\" units=\"%s\">\n",
		sRole, pBase->semantic, sStorage, sIndex, pThis->base.units
	);

	das_datum dmB;
	das_datum dmM;
	assert(pBase->vsize <= DATUM_BUF_SZ);
	
	das_datum_init(&dmB, pThis->pB, pBase->vt, 0, pBase->units);
	char sBufB[64] = {'\0'};
	das_datum_toStrValOnly(&dmB, sBufB, 63, -1);
	
	das_datum_init(&dmM, pThis->pM, pBase->vt, 0, pBase->units);
	char sBufM[64] = {'\0'};	
	das_datum_toStrValOnly(&dmM, sBufM, 63, -1);
	
	DasBuf_printf(pBuf, "      <sequence minval=\"%s\" interval=\"%s\" />\n",
		sBufB, sBufM
	);
	DasBuf_puts(pBuf, "    </scalar>\n");
	
	return DAS_OKAY;
}