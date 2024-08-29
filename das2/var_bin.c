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
#include <math.h>

#include "operator.h"
#include "variable.h"

#include "log.h"

/* ************************************************************************* */
/* Protected functions from the base class */

char* _DasVar_prnUnits(const DasVar* pThis, char* sBuf, int nLen);
char* _DasVar_prnRange(const DasVar* pThis, char* sBuf, int nLen);
char* _DasVar_prnType(const DasVar* pThis, char* pWrite, int nLen);
int _DasVar_noIntrShape(const DasVar* pBase, ptrdiff_t* pShape);



/* ************************************************************************* */
/* Binary functions on other Variables */

typedef struct das_var_binary{
	DasVar base;
	
	char sId[DAS_MAX_ID_BUFSZ];  /* The combination has it's own name, 
										   * may be empty for anoymous combinations */
	
	DasVar* pRight;      /* right hand sub-variable pointer */
	DasVar* pLeft;       /* left hand sub-variable pointer  */
	int     nOp;         /* operator for unary and binary operations */
	double  rRightScale; /* Scaling factor for right hand values */

	das_val_type et;     /* Pre calculated element type, avoid sub-calls*/
} DasVarBinary;

DasVar* copy_DasVarBinary(const DasVar* pBase)
{	
	assert(pBase->vartype == D2V_BINARY_OP); /* Okay to not be present in release code */
	DasVarBinary* pThis = (DasVarBinary*)pBase;

	DasVar* pRet = calloc(1, sizeof(DasVarBinary));
	memcpy(pRet, pBase, sizeof(DasVarBinary));

	((DasVarBinary*)pRet)->pLeft = pThis->pLeft->copy(pThis->pLeft);
	((DasVarBinary*)pRet)->pRight = pThis->pRight->copy(pThis->pRight);

	return pRet;
}

das_val_type DasVarBinary_elemType(const DasVar* pBase)
{
	return ((const DasVarBinary*) pBase)->et;
}


bool DasVarBinary_degenerate(const DasVar* pBase, int iIndex)
{
	const DasVarBinary* pThis = (const DasVarBinary*) pBase;

	return (
		pThis->pLeft->degenerate(pThis->pLeft, iIndex) && 
		pThis->pRight->degenerate(pThis->pRight, iIndex)
	);
}

const char* DasVarBinary_id(const DasVar* pBase)
{
	const DasVarBinary* pThis = (const DasVarBinary*) pBase;
	return pThis->sId;
}

bool DasVarBinary_isNumeric(const DasVar* pBase)
{
	/* Put most common ones first for faster checks */
	if((pBase->vt == vtFloat  ) || (pBase->vt == vtDouble ) ||
	   (pBase->vt == vtInt    ) || (pBase->vt == vtUInt   ) ||
	   (pBase->vt == vtLong   ) || (pBase->vt == vtULong  ) || 
	   (pBase->vt == vtUShort ) || (pBase->vt == vtShort  ) ||
	   (pBase->vt == vtByte) /* That's a signed byte, usually numeric */
	) return true;
	
	/* All the rest but vtUByte are not numeric */
	if(pBase->vt != vtUByte) return false;
	
	const DasVarBinary* pThis = (const DasVarBinary*) pBase;
		
	return (DasVar_isNumeric(pThis->pLeft) && 
			 DasVar_isNumeric(pThis->pRight)    );
}

int DasVarBinary_shape(const DasVar* pBase, ptrdiff_t* pShape)
{
	int i = 0;
	
	if(pShape == NULL){
		das_error(DASERR_VAR, "null shape pointer, can't output shape values");
		return -1;
	}
	
	DasVarBinary* pThis = (DasVarBinary*)pBase;
	
	/* Fill in shape with left variable */
	pThis->pLeft->shape(pThis->pLeft, pShape);
	
	ptrdiff_t aRight[DASIDX_MAX] = DASIDX_INIT_UNUSED;
		
	pThis->pRight->shape(pThis->pRight, aRight);
	das_varindex_merge(pBase->nExtRank, pShape, aRight);
	
	int nRank = 0;
	for(i = 0; i < pBase->nExtRank; ++i) 
		if(pShape[i] != DASIDX_UNUSED) 
			++nRank;
	
	return nRank;
}

/* Our expressions looks like this:  
 * 
 * ( sub_exp_left operator [scale *] sub_exp_right )[units][range]
 */
char* DasVarBinary_expression(
	const DasVar* pBase, char* sBuf, int nLen, unsigned int uFlags
){	
	if(nLen < 12) return sBuf; /* No where to write */
	memset(sBuf, 0, nLen); /* Insure null termination whereever I stop writing */
	
	const DasVarBinary* pThis = (const DasVarBinary*)pBase;
	
	char* pWrite = sBuf;
	int nWrite = 0;
	int d = 0;
	ptrdiff_t aShape[DASIDX_MAX] = DASIDX_INIT_UNUSED;
	
	/* Write our named info if not anoymous */
	if(pThis->sId[0] != '\0'){
		nWrite = strlen(pThis->sId);
		nWrite = nWrite > (nLen - 1) ? nLen - 1 : nWrite;
		strncpy(sBuf, pThis->sId, nWrite);
	
		pWrite = sBuf + nWrite;  nLen -= nWrite;
		
		DasVarBinary_shape(pBase, aShape);
		for(d = 0; d < pBase->nExtRank; ++d){
			if(aShape[d] == DASIDX_UNUSED) continue;
			
			if(nLen < 3) return pWrite;
			*pWrite = '[';  ++pWrite; --nLen;
			*pWrite = g_sIdxLower[d]; ++pWrite; --nLen;
			*pWrite = ']';  ++pWrite; --nLen;
		}
	}
	
	/* Add in the sub-expression if requested (or if we're anonymous) */
	char* pSubWrite = NULL;
	int nTmp = 0;
	char sScale[32] = {'\0'};
	if((uFlags & D2V_EXP_SUBEX)||(pThis->sId[0] == '\0')){
	
		if(nLen < 4) return pWrite;
		
		*pWrite = ' '; ++pWrite; *pWrite = '('; ++pWrite;
		nLen -= 2;
	
		pSubWrite = pThis->pLeft->expression(pThis->pLeft, pWrite, nLen, 0);
		nTmp = pSubWrite - pWrite;
		nLen -= nTmp;
		pWrite = pSubWrite;
		if((nTmp == 0)||(nLen < 6)) goto DAS_VAR_BIN_EXP_PUNT;
		
		*pWrite = ' '; ++pWrite; --nLen;
	
		/* Print the operator, we know this is an in-between operator */
		nTmp = strlen(das_op_toStr(pThis->nOp, NULL));
		if(nTmp > (nLen - 3)) goto DAS_VAR_BIN_EXP_PUNT;
	
		strncpy(pWrite, das_op_toStr(pThis->nOp, NULL), nTmp);
		nLen -= nTmp;
		pWrite += nTmp;
		*pWrite = ' '; ++pWrite, --nLen;
		
		if(pThis->rRightScale != 1.0){
			snprintf(sScale, 31, "%.6e", pThis->rRightScale);
			/* Should pop off strings of zeros after the decimal pt here */
			nTmp = strlen(sScale);
			if(nTmp > (nLen - 2)) goto DAS_VAR_BIN_EXP_PUNT;
			strncpy(pWrite, sScale, nTmp);
			pWrite += nTmp;
			nLen -= nTmp;
			*pWrite = '*'; ++pWrite; --nLen;
		}
	
		pSubWrite = pThis->pRight->expression(pThis->pRight, pWrite, nLen, 0);
		nTmp = pSubWrite - pWrite;
		nLen -= nTmp;
		pWrite = pSubWrite;
		if((nTmp == 0)||(nLen < 3)) goto DAS_VAR_BIN_EXP_PUNT;
	
		*pWrite = ')'; ++pWrite; --nLen;
	}
	
	if(uFlags & D2V_EXP_UNITS){
		if(pBase->units != UNIT_DIMENSIONLESS){
			pSubWrite = _DasVar_prnUnits(pBase, pWrite, nLen);
			nLen -= pSubWrite - pWrite;
			pWrite = pSubWrite;
		}
	}
	
	if(uFlags & D2V_EXP_RANGE){
		pSubWrite = _DasVar_prnRange(&(pThis->base), pWrite, nLen);
		nLen -= pSubWrite - pWrite;
		pWrite = pSubWrite;
	}

	if(uFlags & D2V_EXP_TYPE)
		return _DasVar_prnType(&(pThis->base), pWrite, nLen);
	else
		return pWrite;
	
	DAS_VAR_BIN_EXP_PUNT:
	sBuf[0] = '\0';
	return sBuf;
}

ptrdiff_t DasVarBinary_lengthIn(const DasVar* pBase, int nIdx, ptrdiff_t* pLoc)
{
	DasVarBinary* pThis = (DasVarBinary*)pBase;
	
	ptrdiff_t nLeft = pThis->pLeft->lengthIn(pThis->pLeft, nIdx, pLoc);
	ptrdiff_t nRight = pThis->pRight->lengthIn(pThis->pRight, nIdx, pLoc);
	
	return das_varlength_merge(nLeft, nRight);
}


bool DasVarBinary_get(const DasVar* pBase, ptrdiff_t* pIdx, das_datum* pDatum)
{
	DasVarBinary* pThis = (DasVarBinary*)pBase;
	
	float fTmp;
	double dTmp;
	if(! pThis->pLeft->get(pThis->pLeft, pIdx, pDatum)) return false;
	das_datum dmRight;
	if(! pThis->pRight->get(pThis->pRight, pIdx, &dmRight)) return false;
	
	if(pThis->rRightScale != 1.0){
		switch(dmRight.vt){
			case vtUByte:  dTmp = *((uint8_t*)&dmRight);  break;
			case vtByte:   dTmp = *((int8_t*)&dmRight);   break;
			case vtUShort: dTmp = *((uint16_t*)&dmRight); break;
			case vtShort:  dTmp = *((int16_t*)&dmRight);  break;
			case vtUInt:   dTmp = *((uint32_t*)&dmRight);     break;
			case vtInt:    dTmp = *((int32_t*)&dmRight);      break;
			case vtULong:  dTmp = *((uint64_t*)&dmRight);    break;
			case vtLong:   dTmp = *((int64_t*)&dmRight);     break;
			case vtFloat:  dTmp = *((float*)&dmRight);    break;
			case vtDouble: dTmp = *((double*)&dmRight);   break;
			default:
				das_error(DASERR_VAR, "Can't multiply types %s and %s", 
				          das_vt_toStr(dmRight.vt), das_vt_toStr(vtDouble));
				return false;
		}
		*((double*)(&dmRight)) = pThis->rRightScale * dTmp;
		dmRight.vt = vtDouble;
		dmRight.vsize = sizeof(double);
	}
	
	/* Promote left and right datums to the output type if needed */
	/* Note that for time output, only the left value is promoted if needed */
	switch(pBase->vt){
		
	/* Float promotions and calculation */
	case vtFloat:
		switch(pDatum->vt){
		case vtUByte:   fTmp = *((uint8_t*)pDatum);  *((float*)pDatum) = fTmp; break;
		case vtByte:    fTmp = *((int8_t*)pDatum);  *((float*)pDatum) = fTmp; break;
		case vtShort:  fTmp = *((int16_t*)pDatum);  *((float*)pDatum) = fTmp; break;
		case vtUShort: fTmp = *((uint16_t*)pDatum); *((float*)pDatum) = fTmp; break;
		case vtFloat: break; /* nothing to do */
		default:
			das_error(DASERR_ASSERT, "Logic mismatch between das_vt_merge and DasVarBinary_get");
			return false;
		}
		
		switch(dmRight.vt){
		case vtUByte:  fTmp = *((uint8_t*)&dmRight);  *((float*)&dmRight) = fTmp; break;
		case vtByte:   fTmp = *((int8_t*)&dmRight);   *((float*)&dmRight) = fTmp; break;
		case vtShort:  fTmp = *((int16_t*)&dmRight);  *((float*)&dmRight) = fTmp; break;
		case vtUShort: fTmp = *((uint16_t*)&dmRight); *((float*)&dmRight) = fTmp; break;
		case vtFloat:  break; /* nothing to do */
		default:
			das_error(DASERR_ASSERT, "Logic mismatch between das_vt_merge and DasVarBinary_get");
			return false;
		}
		
		switch(pThis->nOp){
		case D2BOP_ADD: *((float*)pDatum) += *((float*)&dmRight); break;
		case D2BOP_SUB: *((float*)pDatum) -= *((float*)&dmRight); break;
		case D2BOP_MUL: *((float*)pDatum) *= *((float*)&dmRight); break;
		case D2BOP_DIV: *((float*)pDatum) /= *((float*)&dmRight); break;
		case D2BOP_POW: *((float*)pDatum) = powf(*((float*)pDatum), *((float*)&dmRight)); break;
		default:
			das_error(DASERR_NOTIMP, "Binary operation not yet implemented ");
		}
		pDatum->vsize = sizeof(float);
		pDatum->vt = vtFloat;
		break;
	
	/* Double promotions and calculation */
	case vtDouble:
		
		/* Promote left hand side to doubles... */
		switch(pDatum->vt){
		case vtUByte:   dTmp = *((uint8_t*)pDatum);  *((double*)pDatum) = dTmp; break;
		case vtByte:    dTmp = *((int8_t*)pDatum);   *((double*)pDatum) = dTmp; break;
		case vtUShort:  dTmp = *((uint16_t*)pDatum); *((double*)pDatum) = dTmp; break;				
		case vtShort:   dTmp = *((int16_t*)pDatum);  *((double*)pDatum) = dTmp; break;
		case vtUInt:    dTmp = *((uint32_t*)pDatum); *((double*)pDatum) = dTmp; break;
		case vtInt:     dTmp = *((int32_t*)pDatum);  *((double*)pDatum) = dTmp; break;
		case vtULong:   dTmp = *((uint64_t*)pDatum); *((double*)pDatum) = dTmp; break;			
		case vtLong:    dTmp = *((int64_t*)pDatum);  *((double*)pDatum) = dTmp; break;
		case vtFloat:   dTmp = *((float*)pDatum);    *((double*)pDatum) = dTmp; break;
		case vtDouble: break; /* Nothing to do */
		case vtTime:
			/* The only way the left input is a time and my output is a double is 
			 * if I'm subtracting two times. Just go ahead and do than now and
			 * avoid messing up code below */
			if(dmRight.vt != vtTime){
				das_error(DASERR_ASSERT, "Logic mismatch between das_vt_merge and DasVarBinary_get");
				return false;
			}
			dTmp = dt_diff((das_time*)pDatum, (das_time*)&dmRight);
			*((double*)pDatum) = dTmp;
			pDatum->vsize = sizeof(das_time);
			pDatum->vt = vtTime;
			return true;
			break;
		default:
			das_error(DASERR_ASSERT, "Logic mismatch between das_vt_merge and DasVarBinary_get");
			return false;
		}
		
		/* Promote right hand side to doubles... */
		switch(dmRight.vt){
		case vtUByte:  dTmp = *((uint8_t*)&dmRight);  *((double*)&dmRight) = dTmp; break;
		case vtByte:   dTmp = *((int8_t*)&dmRight);   *((double*)&dmRight) = dTmp; break;
		case vtUShort: dTmp = *((uint16_t*)&dmRight); *((double*)&dmRight) = dTmp; break;					
		case vtShort:  dTmp = *((int16_t*)&dmRight);  *((double*)&dmRight) = dTmp; break;
		case vtUInt:   dTmp = *((uint32_t*)&dmRight); *((double*)&dmRight) = dTmp; break;
		case vtInt:    dTmp = *((int32_t*)&dmRight);  *((double*)&dmRight) = dTmp; break;
		case vtULong:  dTmp = *((uint64_t*)&dmRight); *((double*)&dmRight) = dTmp; break;			
		case vtLong:   dTmp = *((int64_t*)&dmRight);  *((double*)&dmRight) = dTmp; break;
		case vtFloat:  dTmp = *((float*)&dmRight);    *((double*)&dmRight) = dTmp; break;
		case vtDouble: break; /* Nothing to do */
		default:
			das_error(DASERR_ASSERT, "Logic mismatch between das_vt_merge and DasVarBinary_get");
			return false;
		}
		
		switch(pThis->nOp){
		case D2BOP_ADD: *((double*)pDatum) += *((double*)&dmRight); break;
		case D2BOP_SUB: *((double*)pDatum) -= *((double*)&dmRight); break;
		case D2BOP_MUL: *((double*)pDatum) *= *((double*)&dmRight); break;
		case D2BOP_DIV: *((double*)pDatum) /= *((double*)&dmRight); break;
		case D2BOP_POW: *((double*)pDatum) = pow(*((double*)&dmRight), *((double*)pDatum)); break;
		default:
			das_error(DASERR_NOTIMP, "Binary operation not yet implemented ");
		}

		pDatum->vsize = sizeof(double);
		pDatum->vt = vtDouble;
		break;
	
	/* If output is a time then the left side better be a time and the operation
	 * is to add to the seconds field and then normalize */
	case vtTime:
		if(pDatum->vt != vtTime){
			das_error(DASERR_ASSERT, "Logic mismatch between das_vt_merge and DasVarBinary_get");
			return false;
		}
		
		/* Promote right hand side to double */
		switch(dmRight.vt){
		case vtUByte:   dTmp = *((uint8_t*)&dmRight);  break;
		case vtByte:    dTmp = *((int8_t*)&dmRight);   break;
		case vtUShort:  dTmp = *((uint16_t*)&dmRight); break;			
		case vtShort:   dTmp = *((int16_t*)&dmRight);  break;
		case vtUInt:    dTmp = *((uint32_t*)&dmRight); break;
		case vtInt:     dTmp = *((int32_t*)&dmRight);  break;
		case vtULong:   dTmp = *((uint64_t*)&dmRight); break;
		case vtLong:    dTmp = *((int64_t*)&dmRight);  break;
		case vtFloat:   dTmp = *((float*)&dmRight);    break;
		case vtDouble:  dTmp = *((double*)&dmRight);   break;
		default:
			das_error(DASERR_ASSERT, "Logic mismatch between das_vt_merge and DasVarBinary_get");
			return false;
		}
		
		switch(pThis->nOp){
		case D2BOP_ADD:  ((das_time*)pDatum)->second += dTmp; break;
		case D2BOP_SUB:  ((das_time*)pDatum)->second -= dTmp; break;
		default:
			das_error(DASERR_ASSERT, "Logic mismatch between das_vt_merge and DasVarBinary_get");
			return false;
		}
		
		dt_tnorm((das_time*)pDatum);
		pDatum->vsize = sizeof(das_time);
		pDatum->vt = vtTime;
		break;
		
	default:
		das_error(DASERR_ASSERT, "Logic mismatch between das_vt_merge and DasVarBinary_get");
		return false;
	}
	
	return true;
}

DasAry* DasVarBinary_subset(
	const DasVar* pBase, int nRank, const ptrdiff_t* pMin, const ptrdiff_t* pMax
){
	if(nRank != pBase->nExtRank){
		das_error(
			DASERR_VAR, "External variable is rank %d, but subset specification "
			"is rank %d", pBase->nExtRank, nRank
		);
		return NULL;
	}
	
	DasVarBinary* pThis = (DasVarBinary*)pBase;
	
	size_t shape[DASIDX_MAX] = DASIDX_INIT_BEGIN;
	int nSliceRank = das_rng2shape(nRank, pMin, pMax, shape);
	if(nSliceRank < 1){ 
		das_error(DASERR_VAR, "Can't output a rank 0 array, use DasVar_get() for single points");
		return NULL;
	}
	
	DasAry* pAry = new_DasAry(
		pThis->sId, pBase->vt, pBase->vsize, NULL, nSliceRank, shape, pBase->units
	);
	
	if(pAry == NULL) return NULL;
	
	/* Going to take the slow boat from China on this one.  Just repeatedly
	 * invoke the get function */
	
	ptrdiff_t pIdx[DASIDX_MAX] = DASIDX_INIT_UNUSED;
	memcpy(pIdx, pMin,  pBase->nExtRank * sizeof(ptrdiff_t));
	
	size_t uTotCount;
	ubyte* pWrite = DasAry_getBuf(pAry, pBase->vt, DIM0, &uTotCount);
	das_datum dm;
#ifndef NDEBUG
	size_t vSzChk = DasAry_valSize(pAry);
#endif
	
	int d = 0;
	size_t uWrote = 0;
	while(pIdx[0] < pMax[0]){
		
		if(!DasVarBinary_get(pBase, pIdx, &dm)){
			dec_DasAry(pAry);
			return NULL;
		}
		
		memcpy(pWrite, &dm, dm.vsize);
		++uWrote;
		assert(dm.vsize == vSzChk);
		
		/* Roll the index */
		for(d = pBase->nExtRank - 1; d > -1; --d){
			pIdx[d] += 1;
			if((d > 0) && (pIdx[d] == pMax[d]))
				pIdx[d] = pMin[d];   /* next higher index will roll on loop iter */
			else
				break;  /* Stop rolling */
		}
		
		pWrite += dm.vsize;
	}
	
	if(uWrote != uTotCount){
		das_error(DASERR_VAR, "Logic error in subset extraction");
		dec_DasAry(pAry);
		return NULL;
	}
	return pAry;
}

/* Fill propogates, if either item is fill, the result is fill */
bool DasVarBinary_isFill(const DasVar* pBase, const ubyte* pCheck, das_val_type vt)
{
	DasVarBinary* pThis = (DasVarBinary*)pBase;
	
	bool bEither = (pThis->pLeft->isFill(pThis->pLeft, pCheck, vt) ||
	                pThis->pRight->isFill(pThis->pRight, pCheck, vt) );
	return bEither;
}

int dec_DasVarBinary(DasVar* pBase){
	pBase->nRef -= 1;
	if(pBase->nRef > 0) return pBase->nRef;
	
	DasVarBinary* pThis = (DasVarBinary*)pBase;
	pThis->pLeft->decRef(pThis->pLeft);
	pThis->pRight->decRef(pThis->pRight);
	free(pThis);
	return 0;
}

DasVar* new_DasVarBinary_tok(
	const char* sId, DasVar* pLeft, int op, DasVar* pRight
){
	if(pLeft == NULL){
		das_error(DASERR_VAR, "Left side variable NULL in binary var definition");
		return NULL;
	}
	if(pRight == NULL){
		das_error(DASERR_VAR, "Right side variable NULL in binary var definition");
		return NULL;
	}	
	
	if(!Units_canMerge(pLeft->units, op, pRight->units)){
		das_error(DASERR_VAR, 
			"Units of '%s' can not be combined with units '%s' using operation '%s'",
			Units_toStr(pRight->units), Units_toStr(pLeft->units), das_op_toStr(op, NULL)
		);
		return NULL;
	}
	
	if(pLeft->nExtRank != pRight->nExtRank){
		das_error(DASERR_VAR,
			"Sub variables appear to be from different datasets, on with %d "
			"indices, the other with %d.", pLeft->nExtRank,
			pRight->nExtRank
		);
		return NULL;
	}
	
	das_val_type vt = das_vt_merge(pLeft->vt, op, pRight->vt);
	if(vt == vtUnknown){ 
		das_error(DASERR_VAR, "Don't know how to merge types %s and %s under "
				  "operation %s", das_vt_toStr(pLeft->vt), 
				  das_vt_toStr(pRight->vt), das_op_toStr(op, NULL));
		return NULL;
	}
	
	if(sId != NULL){
		if(!das_assert_valid_id(sId)) return NULL;
	}
	
	DasVarBinary* pThis = (DasVarBinary*)calloc(1, sizeof(DasVarBinary));
	DasDesc_init((DasDesc*)pThis, VARIABLE);
	
	pThis->base.vartype    = D2V_BINARY_OP;
	pThis->base.vt         = vt;
	pThis->base.vsize      = das_vt_size(vt);
	pThis->base.nRef       = 1;
	pThis->base.nExtRank = pRight->nExtRank;
	
	pThis->base.id         = DasVarBinary_id;
	pThis->base.shape      = DasVarBinary_shape;
	pThis->base.intrShape  = _DasVar_noIntrShape;
	pThis->base.expression = DasVarBinary_expression;
	pThis->base.lengthIn   = DasVarBinary_lengthIn;
	pThis->base.get        = DasVarBinary_get;
	pThis->base.isFill     = DasVarBinary_isFill;
	pThis->base.isNumeric  = DasVarBinary_isNumeric;
	pThis->base.subset     = DasVarBinary_subset;
	
	pThis->base.incRef     = inc_DasVar;
	pThis->base.decRef     = dec_DasVarBinary;
	pThis->base.copy       = copy_DasVarBinary;
	pThis->base.degenerate = DasVarBinary_degenerate;
	pThis->base.elemType   = DasVarBinary_elemType;

	if(sId != NULL) strncpy(pThis->sId, sId, 63);
	
	/* Extra items for this derived class including any conversion factors that
	 * must be applied to the pLeft hand values so that they are in the same
	 * units as the pRight */
	pThis->et = das_vt_merge(
		DasVar_elemType(pLeft), op, DasVar_elemType(pRight)
	);
	pThis->base.semantic   = das_def_semantic(pThis->et);
	
	pThis->nOp = op;
	pThis->pLeft = pLeft;
	pThis->pRight = pRight;
	
	/* Save any conversion factors that must be applied to the pRight hand 
	 * values so that they are in the same units as the pLeft hand value */
	if(Units_haveCalRep(pLeft->units)){
		das_units left_interval = Units_interval(pLeft->units);
		
		if(Units_haveCalRep(pRight->units)){
			das_units right_interval = Units_interval(pRight->units);
			pThis->rRightScale = Units_convertTo(right_interval, 1.0, left_interval);
			pThis->base.units = left_interval;
		}
		else{
			pThis->rRightScale = Units_convertTo(left_interval, 1.0, pRight->units);
			pThis->base.units = pLeft->units;
		}
	}
	else{
		/* Just regular numbers.  Scale if adding subtracting, merge units if
		 * multiply divide */
		switch(op){
		case D2BOP_ADD:
		case D2BOP_SUB:
			pThis->rRightScale = Units_convertTo(pRight->units, 1.0, pLeft->units);
			pThis->base.units = pLeft->units;
			break;
		
		case D2BOP_MUL:
			pThis->base.units = Units_multiply(pRight->units, pLeft->units);
			pThis->rRightScale = 1.0;
			break;
		
		case D2BOP_DIV:
			pThis->base.units = Units_divide(pRight->units, pLeft->units);
			pThis->rRightScale = 1.0;
			break;
		
		default:
			das_error(DASERR_VAR, 
				"I don't know how to combine units '%s' and '%s' under the operation"
				" '%s'", Units_toStr(pRight->units), Units_toStr(pLeft->units), 
				das_op_toStr(op, NULL)
			);
			free(pThis);
			return NULL;
			break;	 
		}
	}
	
	/* If we're going to scale the pRight value, then it's type will convert 
	 * to double.  That might change our output type */
	vt = das_vt_merge(pLeft->vt, op, vtDouble);
	if(vt == vtUnknown){
		free(pThis);
		das_error(DASERR_VAR, "Scaling converts vartype %s to %s, "
		   "Don't know how to merge types %s and %s under "
		   "operation %s", das_vt_toStr(pLeft->vt), das_vt_toStr(vtDouble),
		   das_vt_toStr(pLeft->vt), das_vt_toStr(vtDouble), 
		   das_op_toStr(op, NULL)
		);
		return NULL;
	}
	
	pRight->incRef(pRight);
	pLeft->incRef(pLeft);
	
	return &(pThis->base);
}

DasVar* new_DasVarBinary(
	const char* sId, DasVar* pLeft, const char* sOp, DasVar* pRight)
{
	int nOp = das_op_binary(sOp);
	if(nOp == 0) return NULL;

	if((pLeft->vt < VT_MIN_SIMPLE)||(pLeft->vt > VT_MAX_SIMPLE)||
	   (pRight->vt < VT_MIN_SIMPLE)||(pRight->vt > VT_MAX_SIMPLE)
	){
		das_error(DASERR_VAR, "Vector & Matrix operations not yet implemented");
		return NULL;
	}

	return new_DasVarBinary_tok(sId, pLeft, nOp, pRight);
}

DasErrCode DasVarBinary_encode(DasVar* pBase, const char* sRole, DasBuf* pBuf)
{
	DasVarBinary* pThis = (DasVarBinary*)pBase;

	/* The only common binary operation we have now is refererce + offset and
	   that's often re-created on demand.  For now just issue an info message
	   and move on. */
	if(pThis->nOp == D2BOP_ADD){
		/* Variables should know thier role! */
		daslog_info("Likely reference + offset binary varible not serialized");
		return DAS_OKAY;
	}

	return das_error(DASERR_NOTIMP, "Encoding scheme for unary operations is not yet implemented.");
}
