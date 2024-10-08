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

#include "stream.h"

#define _das_variable_c_
#include "variable.h"

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

/** Override the default intended purpose of values in this variable */
DasErrCode DasVar_setSemantic(DasVar* pThis, const char* sSemantic)
{
	if((sSemantic == NULL)||(sSemantic[0] == '\0'))
		return das_error(DASERR_VAR, "Semantic property data values can not be empty");

	strncpy(pThis->semantic, sSemantic, D2V_MAX_SEM_LEN-1);
	return DAS_OKAY;
}

/* Pure virtual */
DasVar* copy_DasVar(const DasVar* pThis){ return pThis->copy(pThis); }

/* A copy helper for derived classes */
void _DasVar_copyTo(const DasVar* pThis, DasVar* pOther)
{
	DasDesc_init((DasDesc*)pOther, VARIABLE);
	DasDesc_copyIn((DasDesc*)pOther, (const DasDesc*)pThis);

	pOther->vartype    = pThis->vartype;
	pOther->vt         = pThis->vt;
	pOther->vsize      = pThis->vsize;
	memcpy(pOther->semantic, pThis->semantic, D2V_MAX_SEM_LEN);
	pOther->nExtRank   = pThis->nExtRank;
	pOther->nIntRank   = pThis->nIntRank;
	pOther->units      = pThis->units;
	pOther->nRef       = 1;
	
	pOther->id         = pThis->id;
	pOther->elemType   = pThis->elemType;
	pOther->shape      = pThis->shape;
	pOther->intrShape  = pThis->intrShape;
	pOther->expression = pThis->expression;
	pOther->lengthIn   = pThis->lengthIn;
	pOther->get        = pThis->get;
	pOther->isFill     = pThis->isFill;
	pOther->isNumeric  = pThis->isNumeric;
	pOther->subset     = pThis->subset;
	pOther->incRef     = pThis->incRef;
	pOther->copy       = pThis->copy;
	pOther->decRef     = pThis->decRef;
	pOther->degenerate = pThis->degenerate;
	pOther->pUser      = NULL;   /* Do not copy over the user data pointer */
}

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

DasStream* _DasVar_getStream(DasVar* pThis)
{
	DasDesc* pDim = NULL;
	DasDesc* pDs = NULL;
	DasStream* pSd = NULL;
	
	if( (pDim = ((DasDesc*)pThis)->parent) == NULL) return NULL;
	if( (pDs  = pDim->parent ) == NULL) return NULL;
	if( (pSd  = (DasStream*) pDs->parent  ) == NULL) return NULL;

	return pSd;
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
/* Virtual function interface, using explicit switch-case instead of         */
/* function pointers. */

/* "Virtual" functions from derived classes.
 * Using a case instead of a function pointer to stop structure bloat,
 * but maybe the function pointer style is better, I don't know. 
 * 
 * This way makes the structures smaller and constructors simpler but it 
 * spreads information out across multiple modules, which is bad. -cwp
 */

DasErrCode DasConstant_encode(DasVar* pVar, const char* sRole, DasBuf* pBuf);
DasErrCode DasVarSeq_encode(DasVar* pVar, const char* sRole, DasBuf* pBuf);
DasErrCode DasVarAry_encode(DasVar* pVar, const char* sRole, DasBuf* pBuf);
DasErrCode DasVarUnary_encode(DasVar* pVar, const char* sRole, DasBuf* pBuf);
DasErrCode DasVarBinary_encode(DasVar* pVar, const char* sRole, DasBuf* pBuf);

DasErrCode DasVar_encode(DasVar* pVar, const char* sRole, DasBuf* pBuf)
{
	switch(pVar->vartype){
	case D2V_CONST:     return DasConstant_encode(pVar, sRole, pBuf);
	case D2V_SEQUENCE:  return DasVarSeq_encode(pVar, sRole, pBuf);
	case D2V_ARRAY:     return DasVarAry_encode(pVar, sRole, pBuf);
	case D2V_UNARY_OP:  return DasVarUnary_encode(pVar, sRole, pBuf);
	case D2V_BINARY_OP: return DasVarBinary_encode(pVar, sRole, pBuf);
	}

	return das_error(DASERR_VAR, "Logic error");
}

ubyte DasVarAry_getFrame(const DasVar* pVar);

ubyte DasVar_getFrame(const DasVar* pVar){
	switch(pVar->vartype){
	case D2V_CONST:     return 0;
	case D2V_SEQUENCE:  return 0;
	case D2V_ARRAY:     return DasVarAry_getFrame(pVar);
	case D2V_UNARY_OP:  return 0;
	case D2V_BINARY_OP: return 0;
	}

	return das_error(DASERR_VAR, "Logic error");
}

const char* DasVarAry_getFrameName(const DasVar* pVar);

const char* DasVar_getFrameName(const DasVar* pVar)
{
	switch(pVar->vartype){
	case D2V_CONST:     return NULL;
	case D2V_SEQUENCE:  return NULL;
	case D2V_ARRAY:     return DasVarAry_getFrameName(pVar);
	case D2V_UNARY_OP:  return NULL;
	case D2V_BINARY_OP: return NULL;
	}

	das_error(DASERR_VAR, "Logic error");
	return NULL;
}

ubyte DasVarAry_vecMap(const DasVar* pVar, ubyte* nDirs, ubyte* pDirs);

ubyte DasVar_vecMap(const DasVar* pVar, ubyte* nDirs, ubyte* pDirs)
{
	switch(pVar->vartype){
	case D2V_CONST:     return 0;
	case D2V_SEQUENCE:  return 0;
	case D2V_ARRAY:     return DasVarAry_vecMap(pVar, nDirs, pDirs);
	case D2V_UNARY_OP:  return 0;
	case D2V_BINARY_OP: return 0;
	}

	das_error(DASERR_VAR, "Logic error");
	return 0;
}

bool DasVarAry_setFrame(DasVar* pBase, ubyte nFrameId);

bool DasVar_setFrame(DasVar* pVar, ubyte nFrameId){
	
	switch(pVar->vartype){
	case D2V_CONST:     return false;
	case D2V_SEQUENCE:  return false;
	case D2V_ARRAY:     return DasVarAry_setFrame(pVar, nFrameId);
	case D2V_UNARY_OP:  return false;
	case D2V_BINARY_OP: return false;
	}

	das_error(DASERR_VAR, "Logic error");
	return false;	
}