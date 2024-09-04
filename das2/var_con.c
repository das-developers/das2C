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

#include "variable.h"

/* ************************************************************************* */
/* Protected functions from the base class */

char* _DasVar_prnUnits(const DasVar* pThis, char* sBuf, int nLen);
char* _DasVar_prnType(const DasVar* pThis, char* pWrite, int nLen);
int _DasVar_noIntrShape(const DasVar* pBase, ptrdiff_t* pShape);

/* ************************************************************************* */
/* Constants */

typedef struct das_var_const{
	DasVar base;
	char sId[DAS_MAX_ID_BUFSZ];
	
	/* A constant holds a single datum, in 1 freaking KB of space!  Okay if
	 * you don't make arrays of them I guess. */
	das_datum datum;
} DasConstant;

DasVar* copy_DasConstant(const DasVar* pBase){
	assert(pBase->vartype == D2V_CONST);

	DasVar* pRet = calloc(1, sizeof(DasConstant));
	memcpy(pRet, pBase, sizeof(DasConstant));
	return pRet;
}

das_val_type DasConstant_elemType(const DasVar* pBase)
{
	const DasConstant* pThis = (const DasConstant*)pBase;	
	return das_datum_elemType(&(pThis->datum));
}

const char* DasConstant_id(const DasVar* pBase)
{
	return ((DasConstant*)pBase)->sId;
}

int dec_DasConstant(DasVar* pBase){
	pBase->nRef -= 1;
	if(pBase->nRef > 0) return pBase->nRef;
	free(pBase);
	return 0;
}

/* Doesn't even look at the index */
bool DasConstant_get(const DasVar* pBase, ptrdiff_t* pLoc, das_datum* pDatum)
{
	const DasConstant* pThis = (const DasConstant*)pBase;
	memcpy(pDatum, &(pThis->datum), pBase->vsize);
	return true;
}

bool DasConstant_isNumeric(const DasVar* pBase)
{
	return ((pBase->vt >= VT_MIN_SIMPLE)&&(pBase->vt <= VT_MAX_SIMPLE));
}

/* Returns the pointer an the next write point of the string */
char* DasConstant_expression(
	const DasVar* pBase, char* sBuf, int nLen, unsigned int uFlags
){
	if(nLen < 3) return sBuf;
	
	memset(sBuf, 0, nLen);  /* Insure null termination whereever I stop writing */
	
	const DasConstant* pThis = (const DasConstant*)pBase;
	das_datum dm;
	DasConstant_get(pBase, NULL, &dm);
			  
	das_datum_toStrValOnly(&dm, sBuf, nLen, -1);
	int nWrote = strlen(sBuf);
	nLen -= nWrote;
	char* pWrite = sBuf + nWrote;
	
	if(pBase->units == UNIT_DIMENSIONLESS) return pWrite;
	if( (uFlags & D2V_EXP_UNITS) == 0) return pWrite;
	
	char* pSubWrite = _DasVar_prnUnits((DasVar*)pThis, pWrite, nLen);
	nLen -= (pSubWrite - pWrite);
	pWrite = pSubWrite;

	if(uFlags & D2V_EXP_TYPE)
		return _DasVar_prnType((DasVar*)pThis, pWrite, nLen);
	else
		return pWrite;
}

int DasConstant_shape(const DasVar* pBase, ptrdiff_t* pShape)
{
	const das_datum* pDm = &( ((DasConstant*)pBase)->datum);

	int i = 0, nMax = DASIDX_MAX - das_vt_rank(pDm->vt);
	for(i = 0; i < nMax; ++i) pShape[i] = DASIDX_FUNC;
	if(i < DASIDX_MAX){
		pShape[i] = das_datum_shape0(pDm);
	}
	return 0;
}

int DasConstant_interShape(const DasVar* pBase, ptrdiff_t* pShape)
{
	for(int i = 0; i < DASIDX_MAX;++i) pShape[i] = DASIDX_UNUSED;
	pShape[0] = das_datum_shape0(&( ((DasConstant*)pBase)->datum));
	if(pShape[0] == 0)
		return 0;
	else
		return 1;
}

ptrdiff_t DasConstant_lengthIn(const DasVar* pBase, int nIdx , ptrdiff_t* pLoc)
{
	if(nIdx < (DASIDX_MAX - 1))
		return DASIDX_FUNC;
	else
		return das_datum_shape0(&( ((DasConstant*)pBase)->datum));
}


bool DasConstant_isFill(const DasVar* pBase, const ubyte* pCheck, das_val_type vt)
{
	return false;
}

DasAry* DasConstant_subset(
	const DasVar* pBase, int nRank, const ptrdiff_t* pMin, const ptrdiff_t* pMax
){
	if(nRank != pBase->nExtRank){
		das_error(
			DASERR_VAR, "External variable is rank %d, but subset specification "
			"is rank %d", pBase->nExtRank, nRank
		);
		return NULL;
	}
	
	DasConstant* pThis = (DasConstant*)pBase;
	
	size_t shape[DASIDX_MAX] = DASIDX_INIT_BEGIN;
	int nSliceRank = das_rng2shape(nRank, pMin, pMax, shape);
	if(nSliceRank < 1){ 
		das_error(DASERR_VAR, "Can't output a rank 0 array, use DasVar_get() for single points");
		return NULL;
	}
	
	if((pBase->vt == vtText)||(pBase->vt == vtGeoVec)||(pBase->vt == vtGeoVec)){
		das_error(DASERR_VAR, "Subsetting constant vectors and text strings is not yet implemented");
	}

	/* The trick here is to use the fact that the das ary constructor fills
	 * memory with the fill value, so we give it our constant value as the
	 * fill value. */
	DasAry* pAry = new_DasAry(
		pThis->sId, pBase->vt, das_vt_size(pThis->datum.vt), (const ubyte*) &(pThis->datum), 
		nSliceRank, shape, pBase->units
	);
	
	/* Now toggle the fill value to the connonical one for this data type */
	if(pAry != NULL)
		DasAry_setFill(pAry, pBase->vt, NULL);
	
	return pAry;
}

bool DasConstant_degenerate(const DasVar* pBase, int iIdx)
{
	return true;
}


DasVar* new_DasConstant(const char* sId, const das_datum* pDm)
{

	if(pDm->vt == vtUnknown){
		das_error(DASERR_VAR, "Can't make a constant out of unknown bytes");
		return NULL;
	}

	DasConstant* pThis = (DasConstant*)calloc(1, sizeof(DasConstant));
	DasDesc_init((DasDesc*)pThis, VARIABLE);
	
	pThis->base.vartype = D2V_CONST;

	pThis->base.vt = pDm->vt;
	/* vsize set below */
	pThis->base.units = pDm->units;
	pThis->base.nRef       = 1;

	pThis->base.nIntRank = 0;
	if((pDm->vt == vtText)||(pDm->vt == vtGeoVec)||(pDm->vt == vtByteSeq)){
		pThis->base.nIntRank = 1;
	}
	
	pThis->base.nExtRank = DASIDX_MAX - pThis->base.nIntRank;
	
	pThis->base.id         = DasConstant_id;
	pThis->base.shape      = DasConstant_shape;
	pThis->base.intrShape  = _DasVar_noIntrShape;
	pThis->base.expression = DasConstant_expression;
	
	pThis->base.lengthIn   = DasConstant_lengthIn;
	pThis->base.get        = DasConstant_get;
	pThis->base.isFill     = DasConstant_isFill;
	pThis->base.isNumeric  = DasConstant_isNumeric;
	pThis->base.subset     = DasConstant_subset;
	pThis->base.incRef     = inc_DasVar;
	pThis->base.decRef     = dec_DasConstant;
	pThis->base.copy       = copy_DasConstant;
	pThis->base.degenerate = DasConstant_degenerate;
	pThis->base.elemType   = DasConstant_elemType;
	
	/* Vsize setting */
	pThis->base.vsize      = das_vt_size(pDm->vt);

	/* Def. semantic based on val type */
	strncpy(pThis->base.semantic, das_sem_default(pDm->vt), D2V_MAX_SEM_LEN-1); 
	
	strncpy(pThis->sId, sId, DAS_MAX_ID_BUFSZ - 1);
	
	/* Copy in the value */
	memcpy(&(pThis->datum), pDm, sizeof(das_datum));
	
	return (DasVar*)pThis;
}

DasErrCode DasConstant_encode(DasVar* pBase, const char* sRole, DasBuf* pBuf)
{
	return das_error(DASERR_NOTIMP, "Encoding scheme for constants is not yet implemented.");
}
