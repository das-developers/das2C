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

#include <ctype.h>
#include <stdbool.h>
#include <string.h>
#include <math.h>
#include <assert.h>

#include "util.h"
#include "variable.h"
#include "array.h"
#include "operator.h"
#include "variable.h"
#include "frame.h"
#include "vector.h"

/* This would be alot easier to implement in D using sumtype... oh well */

/* ************************************************************************* */
/* Set index printing direction... NOT thread safe */
bool g_bFastIdxLast = false;

DAS_API void das_varindex_prndir(bool bFastLast)
{
	g_bFastIdxLast = bFastLast;
}

/* ************************************************************************* */
/* Helpers */

void das_varindex_merge(int nRank, ptrdiff_t* pDest, ptrdiff_t* pSrc)
{
	
	for(size_t u = 0; u < nRank && u < DASIDX_MAX; ++u){
		
		/* Here's the order of shape merge precidence
		 * 
		 * Ragged > Number > Seq > Unused
		 * 
		 *    | R | N | S | U  
		 *  --+---+---+---+---  
		 *  R | R | R | R | R
		 *  --+---+---+---+---  
		 *  N | R |low| N | N
		 *  --+---+---+---+---   
		 *  F | R | N | S | S
		 *  --+---+---+---+---   
		 *  S | R | N | S | U
		 *  --+---+---+---+---  
		 */ 
		
		/* If either is ragged, the result is ragged */
		if((pDest[u] == DASIDX_RAGGED) || (pSrc[u] == DASIDX_RAGGED)){ 
			pDest[u] = DASIDX_RAGGED;
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
		
		/* All that's left at this point is to be a function or unused */
		if((pDest[u] == DASIDX_FUNC)||(pSrc[u] == DASIDX_FUNC)){
			pDest[u] = DASIDX_FUNC;
			continue;
		}
		
		/* default to unused requires no action */
	}
}

ptrdiff_t das_varlength_merge(ptrdiff_t nLeft, ptrdiff_t nRight)
{
	if((nLeft >= 0)&&(nRight >= 0)) return nLeft < nRight ? nLeft : nRight;
	
	/* Reflect at 0 since FUNC beats UNUSED, and a real index beats anything
	 * that's just a flag */
	
	return nLeft > nRight ? nLeft : nRight;
}

/* ************************************************************************* */
/* Base class functions */

int inc_DasVar(DasVar* pThis){ pThis->nRef += 1; return pThis->nRef;}

int dec_DasVar(DasVar* pThis){
	return pThis->decRef(pThis);
};

int ref_DasVar(const DasVar* pThis){ return pThis->nRef;}

enum var_type DasVar_type(const DasVar* pThis){ return pThis->vartype; }

das_val_type DasVar_valType(const DasVar* pThis){ return pThis->vt;}

size_t DasVar_valSize(const DasVar* pThis){return pThis->vsize;}

/* Pure virtual */
das_val_type DasVar_elemType(const DasVar* pThis){ 
	return pThis->elemType(pThis);
}

das_units DasVar_units(const DasVar* pThis){ return pThis->units;}


bool DasVar_get(const DasVar* pThis, ptrdiff_t* pLoc, das_datum* pDatum)
{
	return pThis->get(pThis, pLoc, pDatum);
}

bool DasVar_isFill(const DasVar* pThis, const ubyte* pCheck, das_val_type vt)
{
	return pThis->isFill(pThis, pCheck, vt);	
}


bool DasVar_isComposite(const DasVar* pVar){
	return (pVar->vartype == D2V_BINARY_OP || pVar->vartype == D2V_UNARY_OP);
}

int DasVar_shape(const DasVar* pThis, ptrdiff_t* pShape)
{
	return pThis->shape(pThis, pShape);
}

int DasVar_intrShape(const DasVar* pThis, ptrdiff_t* pShape)
{
	return pThis->intrShape(pThis, pShape);
}

// For all the items that don't currently support internal shapes
int _DasVar_noIntrShape(const DasVar* pBase, ptrdiff_t* pShape)
{
	return 0;
}

bool DasVar_degenerate(const DasVar* pBase, int iIndex){
	return pBase->degenerate(pBase, iIndex);
}

/*
component_t DasVar_intrType(const DasVar* pThis)
{
	return pThis->intrtype;
}
*/

ptrdiff_t DasVar_lengthIn(const DasVar* pThis, int nIdx, ptrdiff_t* pLoc)
{
	return pThis->lengthIn(pThis, nIdx, pLoc);
}

char* DasVar_toStr(const DasVar* pThis, char* sBuf, int nLen)
{
	char* sBeg = sBuf;
	const unsigned int uFlags = 
		D2V_EXP_RANGE | D2V_EXP_UNITS | D2V_EXP_SUBEX | D2V_EXP_TYPE | D2V_EXP_INTR;
	pThis->expression(pThis, sBuf, nLen, uFlags);
	return sBeg;
}

DasAry* DasVar_subset(
	const DasVar* pThis, int nRank, const ptrdiff_t* pMin, const ptrdiff_t* pMax
){
	return pThis->subset(pThis, nRank, pMin, pMax);
}

bool DasVar_isNumeric(const DasVar* pThis)
{
	return pThis->isNumeric(pThis);
}

char* _DasVar_prnUnits(const DasVar* pThis, char* sBuf, int nLen)
{
	
	if(pThis->units == UNIT_DIMENSIONLESS) return sBuf;
	if(nLen < 3) return sBuf;
	
	memset(sBuf, 0, nLen);  /* Insure null termination where ever I stop writing */
	
	char* pWrite = sBuf;
	
	/* if( nLen > 0 ){ *pWrite = '{'; --nLen; ++pWrite;} */
	if( nLen > 0 ){ *pWrite = ' '; --nLen; ++pWrite;}
	
	const char* sUnits = Units_toStr(pThis->units);
	int nStr = strlen(sUnits);
	int nWrite = nStr < nLen ? nStr : nLen;
	strncpy(pWrite, sUnits, nWrite);
	pWrite += nWrite;
	nLen -= nWrite;
	
	/* if( nLen > 0 ){ *pWrite = '}'; --nLen; ++pWrite;} */
	
	return pWrite;
}

/* Just outputs the base value type */
char* _DasVar_prnType(const DasVar* pThis, char* pWrite, int nLen)
{
	
	const char* sVt = das_vt_toStr(pThis->vt);
	int nStrLen = strlen(sVt);
	if((sVt == NULL)||(nLen < (nStrLen+4)))
		return pWrite;

	*pWrite = ' '; ++pWrite; *pWrite = '['; ++pWrite;
	strncpy(pWrite, das_vt_toStr(pThis->vt), nStrLen);

	pWrite += nStrLen;
	*pWrite = ']'; ++pWrite;

	return pWrite;
}
	
/*	
iFirst = 3

i = 0, 1, 2
    I  J  K

i = 2, 1, 0    iFirst - i - 1 = 0 1 2
    I  J  K
	 
iLetter = i (last fastest)
iLetter = iFirst - i - 1   (first fastest)

*/

char* das_shape_prnRng(
	ptrdiff_t* pShape, int nExtRank, int nShapeLen, char* sBuf, int nBufLen
){

	memset(sBuf, 0, nBufLen);  /* Insure null termination where ever I stop writing */
	
	int nUsed = 0;
	int i;
	for(i = 0; i < nExtRank; ++i)
		if(pShape[i] != DASIDX_UNUSED) ++nUsed;
	
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
		
		if(pShape[i] == DASIDX_UNUSED){ 
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
			if((pShape[i] == DASIDX_RAGGED)||(pShape[i] == DASIDX_FUNC)){
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

/* Range expressions look like " | i:0..60, j:0..1442 "  */
char* _DasVar_prnRange(const DasVar* pThis, char* sBuf, int nLen)
{
	ptrdiff_t aShape[DASIDX_MAX];
	pThis->shape(pThis, aShape);
	
	int nExtRank = pThis->nExtRank;
	return das_shape_prnRng(aShape, nExtRank, nExtRank, sBuf, nLen);
}

/* Printing internal structure information, here's some examples
 *
 * Variable: center | event[i] us2000 | i:0..4483 | k:0..* string
 * Variable: center | event[i] us2000 | i:0..4483, j:- | k:0..3 vec:tscs(0,2,1)
 */
char* _DasVar_prnIntr(
	const DasVar* pThis, const char* sFrame, ubyte* pFrmDirs, ubyte nFrmDirs, 
	char* sBuf, int nBufLen
){
	/* If I have no internal structure, print nothing */
	if(pThis->nIntRank == 0)
		return sBuf;

	memset(sBuf, 0, nBufLen);  /* Insure null termination where ever I stop writing */

	ptrdiff_t aShape[DASIDX_MAX];
	pThis->shape(pThis, aShape);

	int iBeg = pThis->nExtRank;  // First dir to write
	int iEnd = iBeg;                   // one after (or before) first dir to write
	while((iEnd < (DASIDX_MAX - 1))&&(aShape[iEnd] != DASIDX_UNUSED))
		++iEnd;
	
	int nIntrRank = iEnd - iBeg;

	/* Just return if no hope of enough room */
	if(nBufLen < (8 + nIntrRank*6 + (nIntrRank-1)*2))
		return sBuf;

	/* Grap the array index letter before swapping around the iteration direction */
	int iLetter = iBeg;
	if(!g_bFastIdxLast){
		int nTmp = iBeg;
		iBeg = iEnd - 1;
		iEnd = nTmp - 1;
	}

	char sEnd[32] = {'\0'};
	char* pWrite = sBuf;  // Save off the write point
	int i = iBeg;
	bool bAnyWritten = false;
	while(i != iEnd){
		if((aShape[i] == DASIDX_RAGGED)||(aShape[i] == DASIDX_FUNC)){
			sEnd[0] = '*'; sEnd[1] = '\0';
		}
		else{
			snprintf(sEnd, 31, "%zd", aShape[i]);
		}

		size_t nNeedLen = 6 + strlen(sEnd) + ( bAnyWritten ? 1 : 0);
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

		pWrite += nNeedLen;
		nBufLen -= nNeedLen;
		bAnyWritten = true;
		
		if(g_bFastIdxLast) ++i;
		else               --i;
		
		 ++iLetter;  /* always report in order of I,J,K */
	}

	/* now add in the internal information:
	 *  
	 *   vec:tscs(0,2,1)
	 */

	if(nBufLen < 8)
		return pWrite;
	
	switch(pThis->vt){
	case vtText: 
		strcpy(pWrite, " string"); 
		pWrite += 7; nBufLen -= 7;
		break;

	case vtGeoVec: 
		if(!sFrame){
			strcpy(pWrite, " vector"); 
			pWrite += 7; nBufLen -= 7;
		}
		else{
			int nTmp = strlen(sFrame);
			if(nBufLen < 4+nTmp) // out of room
				return pWrite;
			strcpy(pWrite, " vec:");
			pWrite += 5; nBufLen -= 5;
			strcpy(pWrite, sFrame);
			pWrite += nTmp; nBufLen -= nTmp;
		}
		break;

	case vtByteSeq:
		strcpy(pWrite, " bytes"); 
		pWrite += 6; nBufLen -= 6;
		break;

	default: break;
	}
	
	/* Finally, for vectors add the direction map if it's present and not too big,
	 * expert space for (999,999,999,... ) up n nFrmDirs
	 */
	if( !pFrmDirs || nFrmDirs == 0 || nBufLen < (nFrmDirs*4 + 3))
		return pWrite;

	for(int nDir = 0; nDir < nFrmDirs; ++nDir){
		if(pFrmDirs[nDir] > 99)
			return pWrite;
	}

	*pWrite = ' '; ++pWrite; --nBufLen;
	*pWrite = '('; ++pWrite; --nBufLen;

	for(int nDir = 0; nDir < nFrmDirs; ++nDir){
		if(nDir > 0){
			*pWrite = ','; ++pWrite; --nBufLen;
		}		
		int nWrote = snprintf(pWrite, nBufLen, "%d", pFrmDirs[nDir]);
		pWrite += nWrote;
		nBufLen -= nWrote;
	}

	*pWrite = ')'; ++pWrite; --nBufLen;	
	
	return pWrite;	
}

/* ************************************************************************* */
/* Constants */

typedef struct das_var_const{
	DasVar base;
	char sId[DAS_MAX_ID_BUFSZ];
	
	/* A constant holds a single datum */
	das_datum datum;
} DasConstant;

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
	pThis->base.degenerate = DasConstant_degenerate;
	pThis->base.elemType   = DasConstant_elemType;
	
	/* Vsize setting */
	pThis->base.vsize = das_vt_size(pDm->vt);
	
	strncpy(pThis->sId, sId, DAS_MAX_ID_BUFSZ - 1);
	
	/* Copy in the value */
	memcpy(&(pThis->datum), pDm, sizeof(das_datum));
	
	return (DasVar*)pThis;
}

/* ************************************************************************* */
/* Array mapping functions */

enum var_subtype {D2V_STDARY=1, D2V_GEOVEC=2};

typedef struct das_var_array{
	DasVar base;
	
	/* Array pointer and index map to support lookup variables */
	DasAry* pAry; /* A pointer to the array containing the values */
	int idxmap[DASIDX_MAX];      /* i,j,k data set space to array space */

	enum var_subtype varsubtype;  /* Var sub type */
	
} DasVarArray;

das_val_type DasVarAry_elemType(const DasVar* pBase){
	DasVarArray* pThis = (DasVarArray*)pBase;
	return DasAry_valType(pThis->pAry);
}


bool DasVarAry_degenerate(const DasVar* pBase, int iIndex)
{
	DasVarArray* pThis = (DasVarArray*)pBase;

	if((iIndex >= 0)&&(iIndex < DASIDX_MAX)){
		if(pThis->idxmap[iIndex] != DASIDX_UNUSED)
			return false;
	}
	return true;
}

bool DasVarAry_isNumeric(const DasVar* pBase)
{
	/* Put most common ones first for faster checks */
	if((pBase->vt == vtFloat  ) || (pBase->vt == vtDouble ) ||
	   (pBase->vt == vtInt    ) || (pBase->vt == vtUInt   ) || 
	   (pBase->vt == vtLong   ) || (pBase->vt == vtULong  ) || 
	   (pBase->vt == vtUShort ) || (pBase->vt == vtShort  ) ||
	   (pBase->vt == vtByte) /* signed bytes considered numeric */
	) return true;
	
	/* All the rest but vtUByte are not numeric */
	if(pBase->vt == vtUByte){
		const DasVarArray* pThis = (const DasVarArray*) pBase;
		return ! (DasAry_getUsage(pThis->pAry) & D2ARY_AS_SUBSEQ);
	}
	
	return false;
}

DasAry* DasVarAry_getArray(DasVar* pThis)
{
	if( pThis->vartype != D2V_ARRAY) return NULL;
	DasVarArray* pReallyThis = (DasVarArray*)pThis;
	return pReallyThis->pAry;
}

/* Public function, call from the top level 
 */ 
int DasVarAry_shape(const DasVar* pBase, ptrdiff_t* pShape)
{
	if(pShape == NULL){
		das_error(DASERR_VAR, "null shape pointer, can't output shape values");
		return -1;
	}
	
	const DasVarArray* pThis = (const DasVarArray*)pBase;
	
	/* Force a memory error right up front if they've provided a variable too
	 * short to hold the answer */
	for(int i = 0; i < DASIDX_MAX; ++i) pShape[i] = DASIDX_UNUSED;
	
	/* Must be an array function */
	ptrdiff_t aShape[DASIDX_MAX] = DASIDX_INIT_UNUSED;
	int nAryRank = DasAry_shape(pThis->pAry, aShape);
	int iAryIdx = -1;
	int nRank = 0;
	
	for(int iVarIdx = 0; iVarIdx < pBase->nExtRank; ++iVarIdx){
		if(pThis->idxmap[iVarIdx] == DASIDX_UNUSED)
			continue;
		
		iAryIdx = pThis->idxmap[iVarIdx];
		if(iAryIdx >= nAryRank){
			das_error(DASERR_VAR, "Invalid index map detected, max array index"
			           " is %d, lookup index is %d", nAryRank - 1, iAryIdx);
			return -1;
		}
		
		/* Any particular array point may be marked as ragged and that's okay */
		pShape[iVarIdx] = aShape[iAryIdx];
		++nRank;
	}
	return nRank;
}

int DasVarAry_intrShape(const DasVar* pBase, ptrdiff_t* pShape)
{
	
	assert(pBase->vartype == D2V_ARRAY);
	DasVarArray* pThis = (DasVarArray*)pBase;

	int i;
	for(i = 0; i < DASIDX_MAX; ++i)
		pShape[i] = DASIDX_UNUSED;

	ptrdiff_t aShape[DASIDX_MAX] = DASIDX_INIT_UNUSED;
	int nAryRank = DasAry_shape(pThis->pAry, aShape);

	if(pBase->nIntRank > 0){
		/* Just copy out the last nIntRank indicies of the array 
		   because all internal indicies are dense. */
		int j = 0;
		for(i = (nAryRank - pBase->nIntRank); i < nAryRank; ++i){
			pShape[j] = aShape[i];
			++j;
		}
	}

	return pBase->nIntRank;
}

/* This one is tough.  What is my shape in a particular index given all 
 * other indexes.  This is different from the array version in that 
 * 
 * 1: I might not even depend on the previous indexes
 * 
 * 2. Indexes further to the right might affect the range of indexes to
 *    the left
 * 
 * Just code the mapping and see what happens  Let's see what this looks
 * like for ragged arrays
 * 
 *                 j
 *          time   0    1    2    3    4    5    6    7    8
 *       +---------------------------------------------------
 *  freq |       25.1 50.2 75.3  100  126  151  176  201  226
 *   i  0|  2000   X    X    X    X    X    X    X    X 
 *      1|  2001   X    X    X    X    X    X    X    X    X 
 *      2|  2002   X    X    X    X    X    X 
 *      3|  2003   X    X    X    X    X    X    X 
 *      4|  2004   X    X    X    X    X    X    X    X    X 
 *      5|  2005   X    X  
 *      6|  2006   X    X    X    X    X    X    X    X
 *      7|  2007   X    X    X    X    X    X    X  
 *      8|  2008   X    X    X    X    X    X    X    X 
 *      9|  2009   X    X    X    X    X    X    X    X
 *     10|  2010   X    X    X    X    X    X    X    X    X 
 *     11|  2011   X    X    X    X    X    X
 * 
 *  amp  len_in_j @ i = 0 : 7
 *  freq len_in_j @ i = 0 : 7
 *  time len_in_j @ i = 0 : 1 ? 7 ? ==> - (no dependence)
 * 
 * 
 *  amp len_in_i @ j = 3 : 10 ?  The transpose of the above amplitude array 
 *                               is not a valid array.  So assuming low to 
 *                               high packing len_in_i @ j is an invalid value.
 */
ptrdiff_t DasVarAry_lengthIn(const DasVar* pBase, int nIdx, ptrdiff_t* pLoc)
{
	DasVarArray* pThis = (DasVarArray*)pBase;
	
	/* Map the location, it should provide a partial map */
	ptrdiff_t aAryLoc[DASIDX_MAX] = DASIDX_INIT_UNUSED;  /* we have to resolve all these
	                                                      * to a positive number before
                                                          * asking the array for its 
	                                                      * size */
	int i = 0;
	int nIndexes = 0;
	for(i = 0; i < nIdx; ++i){
		
		if(pLoc[i] < 0){
			das_error(DASERR_VAR, "Location index must not contain negative values");
			return DASIDX_UNUSED;
		}
		
		if(pThis->idxmap[i] >= 0){
			++nIndexes;
			aAryLoc[ pThis->idxmap[i] ] = pLoc[i];
		}
	}
	
	/* Sequences would return D2IDX_FUNC here instead */
	if(nIndexes == 0) return DASIDX_UNUSED; 
	
	/* Make sure the front of the array is packed */
	for(i = 0; i < nIndexes; ++i){
		if(aAryLoc[i] < 0){
			das_error(DASERR_VAR, "Unexpected index map result, review this code");
			return DASIDX_UNUSED;
		}
	}
	
	return DasAry_lengthIn(pThis->pAry, nIndexes, aAryLoc);
}

bool DasVarAry_get(const DasVar* pBase, ptrdiff_t* pLoc, das_datum* pDatum)
{
	const DasVarArray* pThis = (const DasVarArray*)pBase;
	
	/* Ignore indices you don't understand, that's what makes this work */
	/* I need a way to make this code fast, maybe the loop should be unrolled? */
	ptrdiff_t pAryLoc[DASIDX_MAX] = DASIDX_INIT_BEGIN;
	
	int nDim = 0;
	for(int i = 0; i < pBase->nExtRank; ++i){
		if(pThis->idxmap[i] >= 0){ /* all the wierd flags are less than 0 */
			pAryLoc[ pThis->idxmap[i] ] = pLoc[i];
			++nDim;
		}
	}

	das_val_type vtAry = DasAry_valType(pThis->pAry);
	
	/* If my last index >= first internal, use getIn*/
	if(pBase->nIntRank == 0){
		const ubyte* ptr = DasAry_getAt(pThis->pAry, pBase->vt, pAryLoc);
		if(pBase->vsize > DATUM_BUF_SZ) return false;
		assert(pBase->vsize <= DATUM_BUF_SZ);
		memcpy(pDatum, ptr, pBase->vsize);
		pDatum->vt = vtAry;
		pDatum->vsize = das_vt_size(vtAry);
		pDatum->units  = pBase->units;
	}
	else if(pBase->nIntRank == 1){
		size_t uCount = 1;
		const ubyte* ptr = DasAry_getIn(pThis->pAry, vtUByte, nDim, pAryLoc, &uCount);
		if(ptr == NULL) return false;

		if(vtAry == vtUByte){   /* Make a datum */

			if(pBase->vt == vtText){
				pDatum->vt = vtText;
				pDatum->vsize = das_vt_size(vtText);
				pDatum->units = pBase->units;
				memcpy(pDatum, &ptr, sizeof(const ubyte*));
			}
			else{
				das_byteseq bs;
				pDatum->vt = vtByteSeq;
				pDatum->vsize = sizeof(das_byteseq);
				bs.ptr = ptr;
				bs.sz  = uCount;
				memcpy(pDatum, &bs, sizeof(das_byteseq));
			}
		}
		else{
			das_error(DASERR_VAR, 
				"Don't know how represent value type %s using a single datum. "
				"(Hint: did you mean to make a GeoVector ?)", das_vt_toStr(vtAry)
			);
			return false;
		}
	}
	else{
		das_error(DASERR_VAR, "Handling for internal types larger then rank 1 not implemented");
		return false;
	}
	return true;
}

bool DasVarAry_isFill(const DasVar* pBase, const ubyte* pCheck, das_val_type vt)
{
	const DasVarArray* pThis = (const DasVarArray*)pBase;
	const ubyte* pFill = DasAry_getFill(pThis->pAry);
	
	return (das_vt_cmpAny(pFill, pBase->vt, pCheck, vt) == 0);
}

int dec_DasVarAry(DasVar* pBase){
	pBase->nRef -= 1;
	if(pBase->nRef > 0) return pBase->nRef;
	
	DasVarArray* pThis = (DasVarArray*)pBase;
	dec_DasAry(pThis->pAry);
	free(pThis);
	return 0;
}


bool _DasVarAry_canStride(
	const DasVarArray* pThis, const ptrdiff_t* pMin, const ptrdiff_t* pMax
){
	/* You can't have more than one increment (of a ragged range)
	 * So say J is ragged, and you only want one I then that's okay.
	 * If you want more than one I then the stride equation no longer
	 * works. 
	 */
	ptrdiff_t shape[DASIDX_MAX] = DASIDX_INIT_UNUSED;
	DasAry_shape(pThis->pAry, shape);
	
	/* the more than one of a ragged range test */
	int d = 0;
	int iFirstUsed = -1;
	ptrdiff_t nSzFirstUsed = 0;
	int iFirstRagged = -1;
	int iLoc;
	
	int nVarRank = pThis->base.nExtRank;
	
	for(d = 0; d < nVarRank; ++d){
		if(pThis->idxmap[d] == DASIDX_UNUSED) continue;
		
		iLoc = pThis->idxmap[d];              /* the real index */
		if(iFirstUsed == -1){
			iFirstUsed = iLoc;
			nSzFirstUsed = pMax[d] - pMin[d];
			continue;
		}
		
		if((shape[iLoc] == DASIDX_RAGGED)&&(iFirstRagged == -1)){
			iFirstRagged = iLoc;
			break;
		}
	}
	
	/* first ragged only set after first used */	
	return (iFirstRagged == -1) || (nSzFirstUsed == 1);
}

/* NOTES in: variable.md. */
DasAry* _DasVarAry_strideSubset(
	const DasVarArray* pThis, const ptrdiff_t* pMin, const ptrdiff_t* pMax,
	bool* pContinue
){	
	*pContinue = true; /* We assume */
	/* If can't stride, this is pointless */
	if(!_DasVarAry_canStride(pThis, pMin, pMax))
		return NULL;
	
	int nVarRank = pThis->base.nExtRank;
	size_t elSz = pThis->base.vsize;
	
	/* Allocate the output array and get a pointer to the memory */
	size_t aSliceShape[DASIDX_MAX] = DASIDX_INIT_BEGIN;
	int nSliceRank = das_rng2shape(nVarRank, pMin, pMax, aSliceShape);
	
	char sName[DAS_MAX_ID_BUFSZ] = {'\0'};
	snprintf(sName, DAS_MAX_ID_BUFSZ - 1, "%s_subset", DasAry_id(pThis->pAry));
	DasAry* pSlice = new_DasAry(
		sName, pThis->base.vt, pThis->base.vsize, DasAry_getFill(pThis->pAry),
		nSliceRank, aSliceShape, pThis->base.units
	);
	
	size_t uWriteBufLen = 0;
	ubyte* pWriteBuf = DasAry_getBuf(pSlice, pThis->base.vt, DIM0, &uWriteBufLen);
	
	/* Get the base starting point pointer */	
	ptrdiff_t base_idx[DASIDX_MAX] = {0};
	int d = 0;
	int iLoc = 0;
	for(d = 0; d < nVarRank; ++d){
		iLoc = pThis->idxmap[d];
		if(iLoc == DASIDX_UNUSED) continue;
		base_idx[iLoc] = pMin[d];
	}
	size_t uRemain = 0;
	const ubyte* pBaseRead = DasAry_getIn(
		pThis->pAry, pThis->base.vt, DasAry_rank(pThis->pAry), base_idx, &uRemain
	);
	if(pBaseRead == NULL){
		*pContinue = false;
		return NULL;
	}
	
	/* make a variable stride from the array stride, note that the var_stride
	 * may be degenerate and have offset changes of 0 */
	ptrdiff_t ary_shape[DASIDX_MAX];
	ptrdiff_t ary_stride[DASIDX_MAX];
	ptrdiff_t var_stride[DASIDX_MAX] = {0};
	if(DasAry_stride(pThis->pAry, ary_shape, ary_stride) < 1){
		*pContinue = false;
		return NULL;
	}
	/* Multiply the strides by the element size, we're going to work in bytes */
	for(d = 0; d < DasAry_rank(pThis->pAry); ++d) ary_stride[d] *= elSz;
	
	for(d = 0; d < nVarRank; ++d){
		/* If only 1 value is chosen for this index there is no striding */
		if((pMax[d] - pMin[d]) == 1) continue;
		iLoc = pThis->idxmap[d];
		if(iLoc == DASIDX_UNUSED) continue;
				
		var_stride[d] = ary_stride[iLoc];
	}
	
	/* Sanity check, are the var strides > 0 */
#ifndef NDEBUG
	for(d = 0; d < nVarRank; ++d){
		assert(var_stride[d] >= 0);
	}
#endif
	
	/* Stride over the array copying values */
	ptrdiff_t idx_cur[DASIDX_MAX];
	memcpy(idx_cur, pMin, nVarRank * sizeof(ptrdiff_t));
	const ubyte* pRead = pBaseRead;
	ubyte* pWrite = pWriteBuf;
	
	/* Copy the data.  Unroll the loop up to dimension 4.  Unchecked there
	 * are *all* kinds of security errors here:
	 *
	 * 1. We could write off the end of the buffer
	 * 2. We could read outside array memory. 
	 *
	 */
	switch(nVarRank){
	case 1:
		while(idx_cur[0] < pMax[0]){
			pRead = pBaseRead; 
			pRead += idx_cur[0]*var_stride[0];
			memcpy(pWrite, pRead, elSz);
			idx_cur[0] += 1;
			pWrite += elSz;
		}
		break;
	
	case 2:
		while(idx_cur[0] < pMax[0]){
			pRead = pBaseRead;
			pRead += idx_cur[0]*var_stride[0];
			pRead += idx_cur[1]*var_stride[1];
			
			memcpy(pWrite, pRead, elSz);

			idx_cur[1] += 1;
			if(idx_cur[1] == pMax[1]){
				idx_cur[1] = pMin[1];
				idx_cur[0] += 1;
			}
			pWrite += elSz;
		}
		break;
	
	case 3:
		while(idx_cur[0] < pMax[0]){
			pRead = pBaseRead;
			pRead += idx_cur[0]*var_stride[0];
			pRead += idx_cur[1]*var_stride[1];
			pRead += idx_cur[2]*var_stride[2];
			
			memcpy(pWrite, pRead, elSz);

			idx_cur[2] += 1;
			if(idx_cur[2] == pMax[2]){
				idx_cur[2] = pMin[2];
				idx_cur[1] += 1;
				if(idx_cur[1] == pMax[1]){
					idx_cur[1] = pMin[1];
					idx_cur[0] += 1;
				}
			}
			pWrite += elSz;
		}
		break;
		
	case 4:
		while(idx_cur[0] < pMax[0]){
			pRead = pBaseRead;
			pRead += idx_cur[0]*var_stride[0];
			pRead += idx_cur[1]*var_stride[1];
			pRead += idx_cur[2]*var_stride[2];
			pRead += idx_cur[3]*var_stride[3];
			
			memcpy(pWrite, pRead, elSz);

			idx_cur[3] += 1;
			if(idx_cur[3] == pMax[3]){
				idx_cur[3] = pMin[3];
				idx_cur[2] += 1;
				if(idx_cur[2] == pMax[2]){
					idx_cur[2] = pMin[2];
					idx_cur[1] += 1;
					if(idx_cur[1] == pMax[1]){
						idx_cur[1] = pMin[1];
						idx_cur[0] += 1;
					}					
				}
			}
			pWrite += elSz;
		}
		break;
		
	default:
		/* all higher dimensions, now we need inner loops */
		while(idx_cur[0] < pMax[0]){
			pRead = pBaseRead;
			for(d = 0; d < nVarRank; ++d) pRead += idx_cur[d]*var_stride[d];
		
			memcpy(pWrite, pRead, elSz);
			
			/* Roll index */
			for(d = nVarRank - 1; d > -1; --d){
				idx_cur[d] += 1;
				if((d > 0) && (idx_cur[d] == pMax[d]))  
					idx_cur[d] = pMin[d];
				else	
					break;  /* Stop rolling */
			}
			
			pWrite += elSz;
		}	
		break;		
	}
	
	return pSlice;
}

/* See if we can use the DasAry_SubSetIn function to make a subset without 
 * allocating memory or copying any data */
DasAry* _DasVarAry_directSubset(
	const DasVarArray* pThis, const ptrdiff_t* pMin, const ptrdiff_t* pMax, 
	bool* pContinue
){
	*pContinue = true;  /* We assume */
	
	/* Map the requested range to the array range */
	ptrdiff_t aAryMin[DASIDX_MAX];
	ptrdiff_t aAryMax[DASIDX_MAX];
	ptrdiff_t nSz;
	int iDim;
	for(iDim = 0; iDim < pThis->base.nExtRank; ++iDim){
		nSz = pMax[iDim] - pMin[iDim];
		if(pThis->idxmap[iDim] == DASIDX_UNUSED){
			if(nSz != 1) 
				return NULL;
		}
		else{
			aAryMin[ pThis->idxmap[iDim] ] = pMin[iDim];
			aAryMax[ pThis->idxmap[iDim] ] = pMax[iDim];
		}
	}
	
	/* Look over the array range and make sure it points to a single subset */
	ptrdiff_t aAryShape[DASIDX_MAX];
	int nAryRank = DasAry_shape(pThis->pAry, aAryShape);
	ptrdiff_t aLoc[DASIDX_MAX];
	int nLocSz = 0;
	int iBegFullRng = -1;
	
	for(iDim = 0; iDim < nAryRank; ++iDim){
			
		/* Sanity check */
		if((aAryMin[iDim] < 0)||(aAryMax[iDim] > aAryShape[iDim])){
			das_error(DASERR_VAR, "Invalid subset request");
			*pContinue = false;
			return NULL;
		}
			
		if((aAryMax[iDim] - aAryMin[iDim]) == 1){	
			/* Going full range locks, can't go back to single items after */
			if(iBegFullRng != -1)
				return NULL;
				
			++nLocSz;
			aLoc[iDim] = aAryMin[iDim];
		}
		else{
			/* Has to be 1 or full range */
			if((aAryMin[iDim] == 0)&&(aAryMax[iDim] == aAryShape[iDim])){
				if(iBegFullRng == -1) iBegFullRng = iDim;
			}
			else{
				/* Fractional range zzziitt, gonna have to copy the data */
				return NULL;
			}
		}
	}
	
	/* Can just make a subset IF nLocSz less than nAryRank */
	if(nLocSz < nAryRank){
		DasAry* pSubSet = DasAry_subSetIn(pThis->pAry, NULL, nLocSz, aLoc);
		return pSubSet;
	}
	
	return NULL;
}

DasAry* _DasVarAry_slowSubset(
	const DasVarArray* pThis, const ptrdiff_t* pMin, const ptrdiff_t* pMax
){
	/* This is the easiest subset code to write but it is also the slowest */
	
	/* Allocate the output array and get a pointer to the memory */
	size_t aSliceShape[DASIDX_MAX] = DASIDX_INIT_BEGIN;
	int nVarRank = pThis->base.nExtRank;
	das_val_type vtEl = pThis->base.vt;
	size_t uSzEl = pThis->base.vsize;
	const ubyte* pFill = DasAry_getFill(pThis->pAry);
	
	int nSliceRank = das_rng2shape(nVarRank, pMin, pMax, aSliceShape);
	
	char sName[DAS_MAX_ID_BUFSZ] = {'\0'};
	snprintf(sName, DAS_MAX_ID_BUFSZ - 1, "%s_subset", DasAry_id(pThis->pAry));
	DasAry* pSlice = new_DasAry(
		sName, vtEl, uSzEl, pFill, nSliceRank, aSliceShape, pThis->base.units
	);
	
	size_t uBufSz = 0;
	ubyte* pBase = DasAry_getBuf(pSlice, vtEl, DIM0, &uBufSz);
	
	ptrdiff_t var_idx[DASIDX_MAX];
	memcpy(var_idx, pMin, nVarRank*sizeof(ptrdiff_t));
	ptrdiff_t read_idx[DASIDX_MAX];  /* Right pad for internal indexes */
	
	const ubyte* pValue = NULL;
	int d;
	ubyte* pWrite = pBase;
	while(var_idx[0] < pMax[0]){
		
		/* Get the real read and the real write locations */
		for(d = 0; d < nVarRank; ++d){
			if(pThis->idxmap[d] != DASIDX_UNUSED){
				read_idx[ pThis->idxmap[d] ] = var_idx[d];
			}
		}
		
		/* If this is an invalid location just use fill.  This is how we take
		 * slices of ragged arrays */
		if(!DasAry_validAt(pThis->pAry, read_idx))
			pValue = pFill;
		else
			pValue = DasAry_getAt(pThis->pAry, vtEl, read_idx);
		
		memcpy(pWrite, pValue, uSzEl);
			
		/* Roll index var index. */
		for(d = nVarRank - 1; d > -1; --d){
			var_idx[d] += 1;
			if((d > 0) && (var_idx[d] == pMax[d]))  
				var_idx[d] = pMin[d];
			else	
				break;  /* Stop rolling */
		}
		pWrite += uSzEl;
	}	
	
	return pSlice;
}

/* subset algorithm router */

DasAry* DasVarAry_subset(
	const DasVar* pBase, int nRank, const ptrdiff_t* pMin, const ptrdiff_t* pMax
){
	const DasVarArray* pThis = (DasVarArray*)pBase;
	
	if(nRank != pBase->nExtRank){
		das_error(
			DASERR_VAR, "External variable is rank %d, but subset specification "
			"is rank %d", pBase->nExtRank, nRank
		);
		return NULL;
	}
	
	size_t aSliceShape[DASIDX_MAX] = DASIDX_INIT_BEGIN;
	int nSliceRank = das_rng2shape(nRank, pMin, pMax, aSliceShape);
	if(nSliceRank < 0) return NULL;
	if(nSliceRank == 0){ 
		das_error(DASERR_VAR, "Can't output a rank 0 array, use DasVar_get() "
				   "for single items");
		return NULL;
	}
	
	/* Try to get the subset in order from fastest method to slowest */
	
	bool bCont = true;
	
	DasAry* pRet = _DasVarAry_directSubset(pThis, pMin, pMax, &bCont);
	if(pRet != NULL) return pRet;
	if(!bCont) return NULL;
	
	pRet = _DasVarAry_strideSubset(pThis, pMin, pMax, &bCont);
	if(pRet != NULL) return pRet;
	if(!bCont) return NULL;
	
	return _DasVarAry_slowSubset(pThis, pMin, pMax);
}
			
/* It is certianly possible to implement an "evaluate_at" function for 
 * variables.  It would look something like the following.  Not going to do this
 * now because we just need to get data into python and other environments.  
 * 
 * But it does provide a nice diagnostic tool, so maybe it's worth considering
 * 
 * General:
 *  app_alt[i][j] => (sqrt(altitude[i]) - (delay_time[j] * 3.14567e-00)) / 42.0) V**2 m**-2 Hz**-1 | i:0..60, j:0..1442
 * 
 * Evaluate at i = 14:
 *  app_alt @ i=14 => (1240 - (delay_time[j] * 3.14567e-00) / 42.0)) | j:0..1442
 *
 * Evaluate at slice j = 346:
 *  app_alt @ j=346 => (sqrt(altitude[i]) - 80.45) | i:0..60
 * 
 * Evaluate above again for i = 14:
 *  app_alt @ i=14,j=346 => (14.4765e+01) V**2 m**2 Hz**-1
 * 
 * After faltten: nothing fixed:
 *  app_alt => app_alt[i][j] V**2 m**-2 Hz**-1 | i:0..60, j:0..1442
 * 
 */

// Combined expression printer for both regular & vector arrays
char* _DasVarAry_intrExpress(
	const DasVar* pBase, char* sBuf, int nLen, unsigned int uExFlags,
	const char* sFrame, ubyte* pDirs, ubyte nDirs
){

	if(nLen < 2) return sBuf;  /* No where to write and remain null terminated */
	memset(sBuf, 0, nLen);  /* Insure null termination whereever I stop writing */
	
	const DasVarArray* pThis = (const DasVarArray*)pBase;	

	int nWrite = strlen(pThis->pAry->sId);
	nWrite = nWrite > (nLen - 1) ? nLen - 1 : nWrite;
	char* pWrite = sBuf;
	strncpy(sBuf, pThis->pAry->sId, nWrite);
	
	pWrite = sBuf + nWrite;  nLen -= nWrite;
	if(nLen < 2) return pWrite;

	int nRank = 0;
	for(int i = 0; i < pBase->nExtRank; i++){
		if(pThis->idxmap[i] != DASIDX_UNUSED) ++nRank;
	}
	
	if(nLen < (nRank*3 + 1)) return pWrite;
	
	for(int i = 0; i < pBase->nExtRank; i++){
		if(pThis->idxmap[i] != DASIDX_UNUSED){ 
			*pWrite = '['; ++pWrite; --nLen;
			*pWrite = g_sIdxLower[i]; ++pWrite; --nLen;
			*pWrite = ']'; ++pWrite; --nLen;
		}
	}

	// i now holds the first internal dimension
	
	char* pSubWrite = pWrite;
	
	if((pBase->units != UNIT_DIMENSIONLESS) && (uExFlags & D2V_EXP_UNITS)){
		pSubWrite = _DasVar_prnUnits((DasVar*)pThis, pWrite, nLen);
		nLen -= (pSubWrite - pWrite);
		pWrite = pSubWrite;
	}
	
	if(uExFlags & D2V_EXP_RANGE){
		pSubWrite = _DasVar_prnRange(&(pThis->base), pWrite, nLen);
		nLen -= (pSubWrite - pWrite);
		pWrite = pSubWrite;
	}

	// Print internal object info if there is any
	if((uExFlags & D2V_EXP_INTR) && (das_vt_rank(pBase->vt) > 0)){
		pSubWrite = _DasVar_prnIntr(pBase, sFrame, pDirs, nDirs, pWrite, nLen);
		nLen -= (pSubWrite - pWrite);
		pWrite = pSubWrite;	
	}

	if(uExFlags & D2V_EXP_TYPE)
		return _DasVar_prnType((DasVar*)pThis, pWrite, nLen);
	else
		return pWrite;
}

char* DasVarAry_expression(
	const DasVar* pBase, char* sBuf, int nLen, unsigned int uFlags
){
	return _DasVarAry_intrExpress(pBase, sBuf, nLen, uFlags, NULL, NULL, 0);
}

DasErrCode init_DasVarArray(
	DasVarArray* pThis, DasAry* pAry, int nExtRank, int8_t* pExtMap, int nIntRank
){
	if((nExtRank == 0)||(nExtRank > (DASIDX_MAX-1))){
		das_error(DASERR_VAR, "Invalid start of internal indices: %d", nExtRank);
		return false;
	}
	
	pThis->base.vartype    = D2V_ARRAY;
	pThis->base.nRef       = 1;
	pThis->base.decRef     = dec_DasVarAry;
	pThis->base.isNumeric  = DasVarAry_isNumeric;
	pThis->base.expression = DasVarAry_expression;
	pThis->base.incRef     = inc_DasVar;
	pThis->base.get        = DasVarAry_get;
	pThis->base.shape      = DasVarAry_shape;
	pThis->base.intrShape  = DasVarAry_intrShape;
	pThis->base.lengthIn   = DasVarAry_lengthIn;
	pThis->base.isFill     = DasVarAry_isFill;
	pThis->base.subset     = DasVarAry_subset;
	pThis->base.nExtRank   = nExtRank;
	pThis->base.nIntRank   = nIntRank;
	pThis->base.degenerate = DasVarAry_degenerate;
	pThis->base.elemType   = DasVarAry_elemType;
	
	/* Extra stuff for array variables */
	if(pAry == NULL)
		return das_error(DASERR_VAR, "Null array pointer\n");
	
	pThis->pAry = pAry;
	pThis->varsubtype = D2V_STDARY;
	
	/* Connection between variable units and array units broken here, this
	 * is intentional, but be aware of it! */
	pThis->base.units = pAry->units;
	
	int nValid = 0;
	char sBuf[128] = {'\0'};
	pThis->base.nExtRank = nExtRank;
	for(int i = 0; i < DASIDX_MAX; ++i)
		pThis->idxmap[i] = DASIDX_UNUSED;
	
	size_t u;
	for(u = 0; u < nExtRank; ++u){ 
		pThis->idxmap[u] = pExtMap[u];
		
		/* Make sure that the map has the same number of non empty indexes */
		/* as the rank of the array */
		if(pExtMap[u] >= 0){
			++nValid;
			if(pExtMap[u] >= pAry->nRank){
				return das_error(DASERR_VAR, 
					"Variable dimension %zu maps to non-existant dimension %zu in "
					"array %s", u, pExtMap[u], DasAry_toStr(pAry, sBuf, 127)
				);
			}
		}
	}

	/* Now make sure that we have enough extra array indicies for the internal
	   structure */
	if((nValid + nIntRank) != DasAry_rank(pAry))
		return das_error(DASERR_VAR,
			"Backing array is rank %d. Expected %d external plus "
			"%d internal indicies.", DasAry_rank(pAry), nExtRank, nIntRank
		);

	/* Here's the score. We're putting a template on top of simple das arrays
	 * that allows composite datums such as strings and GeoVec to be stored with
	 * dense packing.  
	 * 
	 * vtUByte w/string -> vtText and needs one internal index
	 * vtGeoVec needs one internal index and it's equal to the number of components
	 *          It also needs the value type set to the index vector type
	 * vtByteSeq needs one internal index, and it's ragged.
	 */
	das_val_type vtAry = DasAry_valType(pAry);

	if(nIntRank > 1)
		return das_error(DASERR_VAR, 
			"Internal rank = %d, ranks > 1 are not yet supported", nIntRank
		);

	/* Make sure that the last index < the first internal for scalar types,
	   and that last index == first internal for rank 1 types */
	if((vtAry == vtUByte)||(vtAry == vtByte)){
		if((pAry->uFlags & D2ARY_AS_STRING) == D2ARY_AS_STRING){
			if(nIntRank != 1)
				return das_error(DASERR_VAR, "Dense text needs an internal rank of 1");
			pThis->base.vt = vtText;
		}
		else{
			if(nIntRank > 0)
				pThis->base.vt = vtByteSeq;
			else
				pThis->base.vt = vtUByte;
		}
	}
	else {
		if((vtAry < VT_MIN_SIMPLE)||(vtAry > VT_MAX_SIMPLE))
			return das_error(DASERR_VAR, 
				"Only simple types understood by DasVarAry, not vt = %d", vtAry
			);
		pThis->base.vt = vtAry;
	}
	
	pThis->base.vsize = das_vt_size(pThis->base.vt);

	inc_DasAry(pAry);    /* Increment the reference count for this array */
	return DAS_OKAY;
}

DasVar* new_DasVarArray(DasAry* pAry, int nExtRank, int8_t* pExtMap, int nIntIdx)
{
	/* DasVarArray does not point outside of it's stack */
	DasVarArray* pThis = (DasVarArray*)calloc(1, sizeof(DasVarArray));

	if(init_DasVarArray(pThis, pAry, nExtRank, pExtMap, nIntIdx) != DAS_OKAY){
		/* Don't decrement the array ownership on failure because it wasn't
		   incremented, free */
		free(pThis);
		return NULL;
	}
	return (DasVar*)pThis;
}

/* ************************************************************************* */
/* A specific array var, internal structure is a cartesian vector            */

typedef struct das_var_vecary{
	DasVarArray base;

	das_geovec tplt;
	char fname[DASFRM_NAME_SZ]; // frame name for printing info
	
} DasVarVecAry;

int DasVarVecAry_getFrame(const DasVar* pBase)
{
	if(pBase->vartype != D2V_ARRAY) 
		return -1 * DASERR_VAR;

	if( ((const DasVarArray*)pBase)->varsubtype != D2V_GEOVEC)
		return -1 * DASERR_VAR;

	return ((const DasVarVecAry*)pBase)->tplt.frame;
}

const char* DasVarVecAry_getFrameName(const DasVar* pBase)
{
	if(pBase->vartype != D2V_ARRAY) 
		return NULL;

	if( ((const DasVarArray*)pBase)->varsubtype != D2V_GEOVEC)
		return NULL;

	return ((const DasVarVecAry*)pBase)->fname;	
}

const ubyte* DasVarVecAry_getDirs(const DasVar* pBase, ubyte* pNumComp)
{
	if(pBase->vartype != D2V_ARRAY) 
		return NULL;

	if( ((const DasVarArray*)pBase)->varsubtype != D2V_GEOVEC)
		return NULL;

	*pNumComp = ((const DasVarVecAry*)pBase)->tplt.ncomp;
	return ((const DasVarVecAry*)pBase)->tplt.dirs;
}

char* DasVarVecAry_expression(
	const DasVar* pBase, char* sBuf, int nLen, unsigned int uFlags
){
	DasVarVecAry* pThis = (DasVarVecAry*)pBase;

	return _DasVarAry_intrExpress(
		pBase, sBuf, nLen, uFlags, pThis->fname, pThis->tplt.dirs, pThis->tplt.ncomp
	);
}

bool DasVarVecAry_get(const DasVar* pAncestor, ptrdiff_t* pLoc, das_datum* pDm)
{
	const DasVarArray*  pBase = (const DasVarArray*)pAncestor;
	const DasVarVecAry* pThis = (const DasVarVecAry*)pAncestor;
	
	/* Ignore indices you don't understand, that's what makes this work */
	/* I need a way to make this code fast, maybe the loop should be unrolled? */
	ptrdiff_t pAryLoc[DASIDX_MAX] = DASIDX_INIT_BEGIN;
	
	int nDim = 0;
	for(int i = 0; i < pAncestor->nExtRank; ++i){
		if(pBase->idxmap[i] >= 0){ /* all the wierd flags are less than 0 */
			pAryLoc[ pBase->idxmap[i] ] = pLoc[i];
			++nDim;
		}
	}

	if(pAncestor->nIntRank != 1){
		das_error(DASERR_VAR, "Logic error in vector access");
		return false;
	}
	
	size_t uCount = 1;
	const ubyte* ptr = DasAry_getIn(
		pBase->pAry, pThis->tplt.et, nDim, pAryLoc, &uCount
	);

	memcpy(pDm, &(pThis->tplt), sizeof(das_geovec));
	das_geovec* pVec = (das_geovec*)pDm;

	memcpy(pDm, ptr, pVec->esize * pVec->ncomp);
	pDm->units = pAncestor->units;
	pDm->vsize = sizeof(das_geovec);
	pDm->vt    = vtGeoVec;

	return true;
}

DasVar* new_DasVarVecAry(
   DasAry* pAry, int nExtRank, int8_t* pExtMap, int nIntRank, 
   const char* sFrame, ubyte nFrameId, ubyte frameType, ubyte nDirs, const ubyte* pDirs
){

	if((sFrame == NULL)||(sFrame[0] == '\0')){
		das_error(DASERR_VAR, "Vectors cannot have an empty frame name");
		return NULL;
	}
	
	// Handle the base class
	DasVarVecAry* pThis = (DasVarVecAry*) calloc(1, sizeof(DasVarVecAry));
	DasVarArray* pBase = (DasVarArray*)pThis;
	DasVar* pAncestor = (DasVar*)pThis;

	if(init_DasVarArray(pBase, pAry, nExtRank, pExtMap, nIntRank) != DAS_OKAY){
		/* Don't decrement the array ownership on failure because it wasn't
		   incremented, free */
		free(pThis);
		return NULL;
	}

	/* Add in our changes */
	pAncestor->get         = DasVarVecAry_get;
	pAncestor->expression  = DasVarVecAry_expression;
	
	/* And now our derived class data including the vector template*/
	strncpy(pThis->fname, sFrame, DASFRM_NAME_SZ-1);

	ubyte nodata[24] = {0};

	DasErrCode nRet =  das_geovec_init(&(pThis->tplt), nodata, 
		nFrameId, frameType, pAncestor->vt, das_vt_size(pAncestor->vt), 
		nDirs, pDirs
	);

	/* Now switch our value type to geovec */
	pAncestor->vt = vtGeoVec;
	pBase->varsubtype = D2V_GEOVEC;

	if(nRet != DAS_OKAY){
		free(pThis);
		return NULL;
	}

	return (DasVar*) pThis;
}

/* ************************************************************************* */
/* Sequences derived from direct operation on indices */

typedef struct das_var_seq{
	DasVar base;
	int iDep;      /* The one and only index I depend on */
	char sId[DAS_MAX_ID_BUFSZ];  /* Since we can't just use our array ID */
	
	ubyte B[DATUM_BUF_SZ];  /* Intercept */
	ubyte* pB;
	
	ubyte M[DATUM_BUF_SZ];  /* Slope */
	ubyte* pM;
	
} DasVarSeq;

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
	
	if(!das_assert_valid_id(sId)) return NULL;
	strncpy(pThis->sId, sId, DAS_MAX_ID_BUFSZ - 1);
	
	pThis->base.vartype    = D2V_SEQUENCE;
	pThis->base.vt         = vt;
	pThis->base.vsize      = das_vt_size(vt);
	
	pThis->base.nExtRank   = nExtRank;
	pThis->base.nRef       = 1;
	pThis->base.units      = units;
	pThis->base.decRef     = dec_DasVarSeq;
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
		break;
	case vtDouble:
		*((double*)(pThis->pB)) = *((double*)pMin);  
		*((double*)(pThis->pM)) = *((double*)pInterval);
		break;
	case vtTime:
		/* Use the units to get the conversion factor for the slope to seconds */
		/* The save the units as UTC */
		rScale = Units_convertTo(UNIT_SECONDS, *((double*)pInterval), units);
		*((double*)(pThis->pM)) = rScale * *((double*)pInterval);
		pThis->base.units = UNIT_UTC;
		
		*((das_time*)pThis->pB) = *((das_time*)pMin);
		break;
	default:
		das_error(DASERR_VAR, "Value type %d not yet supported for sequences", vt);
		free(pThis);
		return NULL;
	}
	return (DasVar*)pThis;
}

/* ************************************************************************* */
/* Unary Functions on other Variables */

typedef struct das_var_op{
	DasVar base;
	
	/* right hand sub-variable pointer for binary operations */
	const DasVar* pLeft;     
	
	/* Right hand sub-variable pointer for unary and binary operations */
	const DasVar* pRight;
	
	/* operator for unary and binary operations */
	int     nOp;
} DasVarUnary;

/*
DasVar* new_DasVarUnary(const char* sOp, const DasVar* left)
{
	
}



DasVar* new_DasVarUnary_tok(int nOp, const DasVar* left)
{
	/ * TODO: write this once the expression lexer exist * /
	return NULL;
}
*/

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
	pThis->base.degenerate = DasVarBinary_degenerate;
	pThis->base.elemType   = DasVarBinary_elemType;

	if(sId != NULL) strncpy(pThis->sId, sId, 63);
	
	/* Extra items for this derived class including any conversion factors that
	 * must be applied to the pLeft hand values so that they are in the same
	 * units as the pRight */
	pThis->et = das_vt_merge(
		DasVar_elemType(pLeft), op, DasVar_elemType(pRight)
	);
	
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
