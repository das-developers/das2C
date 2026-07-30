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
#include <stdio.h>
#include <assert.h>

#include "util.h"
#include "log.h"
#include "time.h"
#include "operator.h"
#include "vector.h"
#include "dimension.h"
#include "property.h"
#include "dataset.h"
#include "stream.h"
#include "set.h"

/* ************************************************************************* */
/* The formalism table.  One row per known formalism; a miss is the generic  */
/* case, not an error.                                                       */

/* Neither scalar row needs special packing: the bits are a plain element and
   the behavioral differences live in the binop registry.  pack stamps the
   set's units on the value. */
static bool _scalar_pack(const DasSet* pThis, const ubyte* pRun, das_datum* pOut)
{
	das_datum_init(
		pOut, pRun, (das_val_type)DasGen_elemType(pThis->pGen), 0, pThis->units
	);
	return true;
}

static char* _linear_prnIntr(const DasSet* pThis, char* sBuf, int nLen)
{
	(void)pThis;
	if(nLen > 0) sBuf[0] = '\0';
	return sBuf;
}

static char* _point_prnIntr(const DasSet* pThis, char* sBuf, int nLen)
{
	(void)pThis;
	snprintf(sBuf, (size_t)nLen, " point");
	return sBuf;
}

/* Linear is a REAL row, not an absent one: the unstated formalism ordinary
   numbers obey.  Binding it explicitly keeps NULL meaning exactly "rules
   unknown, refuse arithmetic" and sends ALL math through one registry. */
static const das_form_kind g_kindLinear = {
	"linear", etUnknown /* any numeric */, _scalar_pack, _linear_prnIntr, false
};

static const das_form_kind g_kindPoint = {
	"point", etUnknown /* any numeric */, _scalar_pack, _point_prnIntr, false
};

/* Rows land here as formalisms become real: geovec, complex, rotation.  Until
   a token has a row it reads back generically, which is defined behavior. */
extern const das_form_kind das_form_kind_geovec;   /* form_geovec.c */

static const das_form_kind* g_formTable[] = {
	&g_kindLinear,
	&g_kindPoint,
	&das_form_kind_geovec,
	NULL
};

const das_form_kind* das_form_lookup(const char* sToken)
{
	if((sToken == NULL)||(sToken[0] == '\0')) return NULL;
	for(int i = 0; g_formTable[i] != NULL; ++i){
		if(strcmp(g_formTable[i]->sToken, sToken) == 0)
			return g_formTable[i];
	}
	return NULL;
}

DasErrCode das_formalism_init(das_formalism* pThis, const char* sToken)
{
	memset(pThis, 0, sizeof(das_formalism));
	if((sToken == NULL)||(sToken[0] == '\0')){
		pThis->pKind = &g_kindLinear;           /* the default, made explicit */
		return DAS_OKAY;
	}

	if(strlen(sToken) >= sizeof(pThis->sToken))
		return das_error(DASERR_VAR,
			"Formalism token '%s' exceeds %zu bytes", sToken,
			sizeof(pThis->sToken) - 1
		);

	strncpy(pThis->sToken, sToken, sizeof(pThis->sToken) - 1);
	pThis->pKind = das_form_lookup(sToken);     /* NULL = generic, not error */
	return DAS_OKAY;
}

const das_form_kind* das_form_linear(void){ return &g_kindLinear; }

DasErrCode das_formalism_bind(
	das_formalism* pThis, const char* sRole, const char* sVal
){
	if((sRole==NULL)||(sRole[0]=='\0')||(sVal==NULL))
		return das_error(DASERR_VAR, "Empty role or null value in binding");
	if(pThis->nBinds >= DASFORM_MAX_BINDS)
		return das_error(DASERR_VAR,
			"No room for binding '%s', %d slots in use", sRole, pThis->nBinds
		);
	if(strlen(sRole) >= sizeof(pThis->aBind[0].sRole))
		return das_error(DASERR_VAR, "Binding role '%s' too long", sRole);
	if(strlen(sVal) >= sizeof(pThis->aBind[0].sVal))
		return das_error(DASERR_VAR, "Binding value '%s' too long", sVal);

	strncpy(pThis->aBind[pThis->nBinds].sRole, sRole,
	        sizeof(pThis->aBind[0].sRole)-1);
	strncpy(pThis->aBind[pThis->nBinds].sVal, sVal,
	        sizeof(pThis->aBind[0].sVal)-1);
	pThis->nBinds += 1;
	return DAS_OKAY;
}

const char* das_formalism_getBind(const das_formalism* pThis, const char* sRole)
{
	for(int i = 0; i < pThis->nBinds; ++i){
		if(strcmp(pThis->aBind[i].sRole, sRole) == 0)
			return pThis->aBind[i].sVal;
	}
	return NULL;
}

/* ************************************************************************* */
/* The binop registry.  Sparse, ordered, misses fail loud in the caller.     */
/*                                                                           */
/* v1 content is the linear ring plus the point pilot, so the dispatch path  */
/* is exercised by shipping math before any composite rules exist.  No slot  */
/* is ever NULL: a generic formalism's NULL kind matches nothing, so unknown */
/* math is refused by construction.                                          */

static bool _affine_apply_sub(
	das_elem_type etL, das_elem_type etR,
	const ubyte* pL, const ubyte* pR, ubyte* pOut
){
	/* time - time is a double span in seconds */
	if((etL == etTime)&&(etR == etTime)){
		*((double*)pOut) = dt_diff((const das_time*)pL, (const das_time*)pR);
		return true;
	}
	if((etL == etLong)&&(etR == etLong)){
		*((int64_t*)pOut) = *((const int64_t*)pL) - *((const int64_t*)pR);
		return true;
	}
	double rL, rR;
	if(!das_elem_asDouble(etL, pL, &rL) || !das_elem_asDouble(etR, pR, &rR))
		return false;
	*((double*)pOut) = rL - rR;
	return true;
}

static bool _affine_apply_add(
	das_elem_type etL, das_elem_type etR,
	const ubyte* pL, const ubyte* pR, ubyte* pOut
){
	/* broken-down time plus a span in seconds */
	if(etL == etTime){
		double rSpan;
		if(!das_elem_asDouble(etR, pR, &rSpan)) return false;
		das_time dt = *((const das_time*)pL);
		dt.second += rSpan;
		dt_tnorm(&dt);
		memcpy(pOut, &dt, sizeof(das_time));
		return true;
	}
	if(etR == etTime)
		return _affine_apply_add(etR, etL, pR, pL, pOut);

	if((etL == etLong)&&(etR == etLong)){
		*((int64_t*)pOut) = *((const int64_t*)pL) + *((const int64_t*)pR);
		return true;
	}
	double rL, rR;
	if(!das_elem_asDouble(etL, pL, &rL) || !das_elem_asDouble(etR, pR, &rR))
		return false;
	*((double*)pOut) = rL + rR;
	return true;
}

/* The linear ring: the common operators every plain number always had, now
   registered like everything else so no operation bypasses the registry. */
static bool _linear_res_addsub(
	const das_formalism* pL, const das_formalism* pR,
	das_units uL, das_units uR, das_formalism* pOut, das_units* pUnitsOut,
	double* pRightScale
){
	(void)pL; (void)pR;
	*pRightScale = 1.0;
	if(uL != uR){
		if(!Units_canConvert(uR, uL)){
			das_error(DASERR_VAR,
				"Adding %s to %s needs convertible units", uL, uR
			);
			return false;
		}
		*pRightScale = Units_convertTo(uL, 1.0, uR);
	}
	das_formalism_init(pOut, NULL);
	*pUnitsOut = uL;
	return true;
}

static bool _linear_res_mul(
	const das_formalism* pL, const das_formalism* pR,
	das_units uL, das_units uR, das_formalism* pOut, das_units* pUnitsOut,
	double* pRightScale
){
	(void)pL; (void)pR;
	*pRightScale = 1.0;
	das_formalism_init(pOut, NULL);
	*pUnitsOut = Units_multiply(uL, uR);
	return true;
}

static bool _linear_apply_mul(
	das_elem_type etL, das_elem_type etR,
	const ubyte* pL, const ubyte* pR, ubyte* pOut
){
	if((etL == etLong)&&(etR == etLong)){
		*((int64_t*)pOut) = *((const int64_t*)pL) * *((const int64_t*)pR);
		return true;
	}
	double rL, rR;
	if(!das_elem_asDouble(etL, pL, &rL) || !das_elem_asDouble(etR, pR, &rR))
		return false;
	*((double*)pOut) = rL * rR;
	return true;
}

/* point - point = interval.  Both points must sit on the same epoch; v1 is
   strict identity, convert before subtracting. */
static bool _affine_res_sub_pp(
	const das_formalism* pL, const das_formalism* pR,
	das_units uL, das_units uR, das_formalism* pOut, das_units* pUnitsOut,
	double* pRightScale
){
	(void)pL; (void)pR;
	*pRightScale = 1.0;
	if(uL != uR){
		das_error(DASERR_VAR,
			"point - point needs one epoch on both sides, have %s and %s "
			"(convert first)", uL, uR
		);
		return false;
	}
	das_formalism_init(pOut, NULL);           /* an interval is linear */
	*pUnitsOut = Units_interval(uL);
	return true;
}

/* point + interval = point (and the stated commute).  The interval units
   must be the epoch's own interval units; v1 is strict, convert first. */
static bool _affine_res_add_pi(
	const das_formalism* pL, const das_formalism* pR,
	das_units uL, das_units uR, das_formalism* pOut, das_units* pUnitsOut,
	double* pRightScale
){
	(void)pR;
	*pRightScale = 1.0;
	das_units uNeed = Units_interval(uL);
	if(uR != uNeed){
		if(!Units_canConvert(uR, uNeed)){
			das_error(DASERR_VAR,
				"point + interval needs %s (or convertible) on the right for "
				"epoch %s, have %s", uNeed, uL, uR
			);
			return false;
		}
		*pRightScale = Units_convertTo(uNeed, 1.0, uR);
	}
	*pOut = *pL;          /* result is a point on the same epoch */
	*pUnitsOut = uL;
	return true;
}

/* the commuted form: scale applies to the POINT side's needs, so the scale
   out-param refers to the interval operand, which is the LEFT one here; the
   caller must therefore swap operands rather than reuse the right-scale.
   Simplest correct v1: require the interval already in the epoch's own
   interval units for this ordering. */
static bool _affine_res_add_ip(
	const das_formalism* pL, const das_formalism* pR,
	das_units uL, das_units uR, das_formalism* pOut, das_units* pUnitsOut,
	double* pRightScale
){
	(void)pL;
	*pRightScale = 1.0;
	if(uL != Units_interval(uR)){
		das_error(DASERR_VAR,
			"interval + point needs %s on the left for epoch %s, have %s "
			"(or put the point on the left)", Units_interval(uR), uR, uL
		);
		return false;
	}
	*pOut = *pR;
	*pUnitsOut = uR;
	return true;
}

/* Result element types are math facts owned by the rules */
static das_elem_type _outElem_promote(das_elem_type etL, das_elem_type etR)
{
	if((etL == etLong)&&(etR == etLong)) return etLong;
	return etDouble;
}

static das_elem_type _outElem_left(das_elem_type etL, das_elem_type etR)
{
	(void)etR;
	return etL;   /* point + interval keeps the point's storage */
}

static das_elem_type _outElem_right(das_elem_type etL, das_elem_type etR)
{
	(void)etL;
	return etR;
}

static das_elem_type _outElem_span(das_elem_type etL, das_elem_type etR)
{
	if((etL == etTime)||(etR == etTime)) return etDouble;  /* seconds */
	return _outElem_promote(etL, etR);
}

static const das_form_rule g_ruleTable[] = {
	{ dfoAdd, &g_kindLinear, &g_kindLinear, _linear_res_addsub, _outElem_promote, _affine_apply_add },
	{ dfoSub, &g_kindLinear, &g_kindLinear, _linear_res_addsub, _outElem_promote, _affine_apply_sub },
	{ dfoMul, &g_kindLinear, &g_kindLinear, _linear_res_mul,    _outElem_promote, _linear_apply_mul },
	{ dfoSub, &g_kindPoint,  &g_kindPoint,  _affine_res_sub_pp, _outElem_span,    _affine_apply_sub },
	{ dfoAdd, &g_kindPoint,  &g_kindLinear, _affine_res_add_pi, _outElem_left,    _affine_apply_add },
	{ dfoAdd, &g_kindLinear, &g_kindPoint,  _affine_res_add_ip, _outElem_right,   _affine_apply_add },
	/* { dfoAdd, point, point } is ABSENT on purpose: adding two calendar
	   positions is meaningless and the lookup miss is the refusal.  A generic
	   formalism's pKind is NULL and no slot here is NULL, so unknown math
	   misses by construction. */
};

const das_form_rule* das_form_findRule(
	das_form_op op, const das_form_kind* pLeft, const das_form_kind* pRight
){
	for(size_t u = 0; u < sizeof(g_ruleTable)/sizeof(g_ruleTable[0]); ++u){
		if((g_ruleTable[u].op == op)&&
		   (g_ruleTable[u].pLeft == pLeft)&&(g_ruleTable[u].pRight == pRight))
			return g_ruleTable + u;
	}
	return NULL;
}

/* ************************************************************************* */
/* Index print direction, a global the old variable layer owned.  Nothing inside
   das2C calls the setter, but das2dlm does (src/das2c.c), so this is live public
   API and not dead code.  The expression printers below read the flag. */
static bool g_bFastIdxLast = true;

void das_varindex_prndir(bool bFastLast)
{
	g_bFastIdxLast = bFastLast;
}




/* Shape range printer (declared in array.h, previously defined by the
   retired variable layer) */

char* das_shape_prnRng(
	ptrdiff_t* pShape, int nExtRank, int nShapeLen, char* sBuf, int nBufLen
){

	memset(sBuf, 0, nBufLen);  /* Insure null termination where ever I stop writing */
	
	int nUsed = 0;
	int i;
	for(i = 0; i < nExtRank; ++i)
		if(pShape[i] != SETIDX_UNUSED) ++nUsed;
	
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
		
		if(pShape[i] == SETIDX_UNUSED){ 
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
			if((pShape[i] == SETIDX_RAGGED)||(pShape[i] == SETIDX_BORROW)){
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

/* ************************************************************************* */
/* DasSet base                                                               */

int DasSet_incRef(DasSet* pThis)
{
	pThis->nRef += 1;
	return pThis->nRef;
}

int DasSet_decRef(DasSet* pThis)
{
	assert(pThis->nRef > 0);
	pThis->nRef -= 1;
	if(pThis->nRef == 0){
		if(pThis->pGen != NULL) DasGen_decRef(pThis->pGen);
		DasDesc_freeProps(&(pThis->base));
		free(pThis);
		return 0;
	}
	return pThis->nRef;
}

bool DasSet_get(const DasSet* pThis, ptrdiff_t* pLoc, das_datum* pOut)
{
	return pThis->vt->get(pThis, pLoc, pOut);
}

int DasSet_shape(const DasSet* pThis, ptrdiff_t* pShape)
{
	return pThis->vt->shape(pThis, pShape);
}

ptrdiff_t DasSet_lengthIn(const DasSet* pThis, int nIdx, ptrdiff_t* pLoc)
{
	return DasGen_lengthIn(pThis->pGen, nIdx, pLoc);
}

const char* DasSet_role(const DasSet* pThis)
{
	const DasDim* pDim = (const DasDim*)DasDesc_parent((const DasDesc*)pThis);

	/* No parent, no role.  A set built on its own is a perfectly good set;
	   it just isn't playing a part in a dimension yet. */
	if(pDim == NULL) return NULL;

	for(size_t u = 0; u < pDim->uVars; ++u){
		if(pDim->aVars[u] != pThis) continue;

		if(pDim->aRoles[u][0] == '\0')
			break;   /* listed but unnamed: same broken state as not listed */

		return pDim->aRoles[u];
	}

	/* Having a dimension for a parent but no role in it cannot happen through
	   the public API: DasDim_addVar() stamps the parent pointer and the role
	   name together.  Reaching here means the two halves have come apart */
	das_error(DASERR_VAR,
		"Dimension '%s' is the parent of a set it has no role for",
		DasDim_id(pDim)
	);
	return NULL;
}

/* Shared body for the two public subset entries.  The division of labor:
   the SET owns rank agreement, slice geometry, the rank-0 refusal, naming,
   units and the element-vs-presentation decision; the GENERATOR owns whether
   a view is possible and how bytes are produced. */
static DasAry* _DasSet_subset(
	const DasSet* pThis, int nRank, const ptrdiff_t* pMin, const ptrdiff_t* pMax,
	bool bMustCopy
){
	ptrdiff_t aSetShape[SETIDX_MAX];
	int nExtRank = DasSet_shape(pThis, aSetShape);

	if(nRank != nExtRank){
		das_error(DASERR_VAR,
			"Set is external rank %d, but the subset specification is rank %d",
			nExtRank, nRank
		);
		return NULL;
	}

	size_t aSliceShape[SETIDX_MAX] = SETIDX_INIT_BEGIN;
	int nSliceRank = das_rng2shape(nRank, pMin, pMax, aSliceShape);
	if(nSliceRank < 0) return NULL;
	if(nSliceRank == 0){
		das_error(DASERR_VAR,
			"Can't output a rank 0 array, use DasSet_get() for single items"
		);
		return NULL;
	}

	/* Fast path first, when the caller can live with shared storage.  A NULL
	   answer just means "not applicable here", so nothing is riding on it. */
	if(!bMustCopy){
		DasAry* pView = DasGen_subsetView(pThis->pGen, nRank, pMin, pMax);
		if(pView != NULL) return pView;
	}

	/* Values come out as ELEMENTS.  A composite's components ride as trailing
	   array indices, which is how the backing storage already holds them; 
	   Expanding each one to portable das_datum format would have huge 
	   performance penalties in both CPU and RAM. */
	das_val_type vtEl = (das_val_type)DasGen_elemType(pThis->pGen);
	size_t uItemElems = DasGen_itemElems(pThis->pGen);
	if(uItemElems == 0){
		das_error(DASERR_NOTIMP,
			"Ragged item runs have no fixed width to subset; read them "
			"through the generator"
		);
		return NULL;
	}

	/* An item run wider than one element needs a home in the output shape,
	   so it becomes one more (trailing, fixed) index. */
	if(uItemElems > 1){
		if(nSliceRank >= SETIDX_MAX){
			das_error(DASERR_VAR, "Subset rank %d leaves no room for the "
				"component index", nSliceRank);
			return NULL;
		}
		aSliceShape[nSliceRank] = uItemElems;
		++nSliceRank;
	}

	DasAry* pAry = DasGen_getArray(pThis->pGen);
	char sName[DAS_MAX_ID_BUFSZ] = {'\0'};
	snprintf(sName, DAS_MAX_ID_BUFSZ - 1, "%s_subset",
		(pAry != NULL) ? DasAry_id(pAry) : "set"
	);

	DasAry* pOut = new_DasAry(
		sName, vtEl, das_vt_size(vtEl), DasGen_getFill(pThis->pGen),
		nSliceRank, aSliceShape, pThis->units
	);
	if(pOut == NULL) return NULL;

	/* getBuf counts ELEMENTS; the generator writes bytes. */
	size_t uBufElems = 0;
	ubyte* pBuf = DasAry_getBuf(pOut, vtEl, DIM0, &uBufElems);
	if(pBuf == NULL){ dec_DasAry(pOut); return NULL; }
	size_t uBufLen = uBufElems * das_vt_size(vtEl);

	if(DasGen_subsetInto(pThis->pGen, nRank, pMin, pMax, pBuf, uBufLen) < 0){
		dec_DasAry(pOut);
		return NULL;
	}

	return pOut;
}

DasAry* DasSet_subset(
	const DasSet* pThis, int nRank, const ptrdiff_t* pMin, const ptrdiff_t* pMax
){
	return _DasSet_subset(pThis, nRank, pMin, pMax, false);
}

DasAry* DasSet_subsetCopy(
	const DasSet* pThis, int nRank, const ptrdiff_t* pMin, const ptrdiff_t* pMax
){
	return _DasSet_subset(pThis, nRank, pMin, pMax, true);
}

bool DasSet_degenerate(const DasSet* pThis, int iIndex)
{
	ptrdiff_t aShape[SETIDX_MAX];
	int nRank = DasSet_shape(pThis, aShape);
	if((iIndex < 0)||(iIndex >= nRank)) return true;
	return aShape[iIndex] == SETIDX_UNUSED;
}

int DasSet_intrShape(const DasSet* pThis, ptrdiff_t* pShape)
{
	return pThis->vt->intrShape(pThis, pShape);
}

bool DasSet_isNumeric(const DasSet* pThis)
{
	return pThis->vt->isNumeric(pThis);
}

char* DasSet_toStr(const DasSet* pThis, char* sBuf, int nLen)
{
	return pThis->vt->expression(pThis, sBuf, nLen, 0);
}

DasSet* DasSet_copy(const DasSet* pThis)
{
	return pThis->vt->copy(pThis);
}

bool DasSet_setArray(DasSet* pThis, DasAry* pNew)
{
	if(!DasGen_setArray(pThis->pGen, pNew))
		return false;
	pThis->units = DasAry_units(pNew);
	return true;
}

das_val_type DasSet_valType(const DasSet* pThis)
{
	switch(DasSet_presType(pThis)){
	case prString: return vtText;
	case prBlob:   return vtByteSeq;
	case prVector: return vtGeoVec;
	default:       return (das_val_type)DasGen_elemType(pThis->pGen);
	}
}

ubyte DasSet_vecMap(const DasSet* pThis, ubyte* pNumDirs, ubyte* pDirs)
{
	if(DasSet_valType(pThis) != vtGeoVec){
		if(pNumDirs != NULL) *pNumDirs = 0;
		return 0;
	}
	const DasIntrSet* pComp = (const DasIntrSet*)pThis;
	ubyte nComp = (ubyte)pComp->aIntShape[0];
	if(pNumDirs != NULL) *pNumDirs = nComp;

	if(pDirs != NULL){
		/* natural order unless sysorder says otherwise */
		for(ubyte c = 0; c < nComp; ++c) pDirs[c] = c;
		const char* sOrd = das_formalism_getBind(&(pThis->form), "sysorder");
		if(sOrd != NULL){
			int iSlot = 0;
			for(const char* p = sOrd; (*p != '\0')&&(iSlot < (int)nComp); ++p){
				if((*p >= '0')&&(*p <= '2')){
					pDirs[iSlot] = (ubyte)(*p - '0');
					++iSlot;
				}
			}
		}
	}
	return nComp;
}

/* Component (or scalar) display labels.  Preference order matches the
   retired variable layer: the set's own label property, then the dim's
   compLabel (composites) or label (scalars), then physdim + canonical
   direction symbols. */
int das_makeCompLabels(const DasSet* pVar, char** psBuf, size_t uLenEa)
{
	const DasDesc* pDesc = (const DasDesc*)pVar;
	const DasDesc* pDim  = DasDesc_parent((DasDesc*)pDesc);
	const DasProp* pProp = DasDesc_getLocal(pDesc, "label");

	if(uLenEa < 2)
		return -1 * das_error(DASERR_VAR, "uLenEa too small in das_makeCompLabels");

	if(DasSet_valType(pVar) == vtGeoVec){
		ubyte aDirs[3] = {0};
		ubyte nComp = 0;
		DasSet_vecMap(pVar, &nComp, aDirs);

		const char* sSys = das_formalism_getBind(&(pVar->form), "system");
		ubyte uSysType = das_compsys_id(sSys ? sSys : "cartesian");

		if(pProp == NULL)
			pProp = DasDesc_getLocal(pDim, "compLabel");

		if(pProp != NULL){
			int nItems = DasProp_extractItems(pProp, psBuf, 3, uLenEa);
			if(nItems == (int)nComp)
				return nComp;
			daslog_warn_v(
				"Expected %d values in the component label %s, found %d instead",
				nComp, DasProp_value(pProp), nItems
			);
		}

		/* Data components read as measurement + direction symbol; coordinate
		   components read as symbol + frame */
		const char* sFrame = DasSet_getFrame(pVar);
		for(ubyte i = 0; i < nComp; ++i){
			const char* sSym = das_compsys_symbol(uSysType, aDirs[i]);
			if(DasDim_type((DasDim*)pDim) == DASDIM_DATA)
				snprintf(psBuf[i], uLenEa - 1, "%s_%s", DasDim_dim((DasDim*)pDim), sSym);
			else if(sFrame != NULL)
				snprintf(psBuf[i], uLenEa - 1, "%s_%s", sSym, sFrame);
			else
				strncpy(psBuf[i], sSym, uLenEa - 1);
		}
		return nComp;
	}

	/* scalar (or plain composite/byte-run) version */
	if(pProp == NULL)
		pProp = DasDesc_getLocal(pDim, "label");

	if(pProp != NULL)
		strncpy(psBuf[0], DasProp_value(pProp), uLenEa-1);
	else
		strncpy(psBuf[0], DasDim_dim((DasDim*)pDim), uLenEa-1);

	return 1;
}

/* ---- The writer: sets emit the new wire dialect ------------------------- */

/* forward decl, defined in stream.h consumers; resolved at link */
DasStream* _DasSet_getStream(DasSet* pThis)
{
	DasDesc* p = (DasDesc*)pThis;
	while((p != NULL)&&(p->type != STREAM)) p = p->parent;
	return (DasStream*)p;
}

/* %g for plain reals (pretty, no trailing zeros, matches the <values> path);
   integers print whole; broken-down times render ISO to microseconds. */
static char* _set_seqValToStr(
	const ubyte* pVal, das_elem_type et, char* sBuf, int nLen
){
	switch(et){
	case etTime:
		dt_isoc(sBuf, (size_t)nLen, (const das_time*)pVal, 6);
		return sBuf;
	case etFloat:
		snprintf(sBuf, (size_t)nLen, "%.7g", (double)(*(const float*)pVal));
		return sBuf;
	case etDouble:
		snprintf(sBuf, (size_t)nLen, "%.15g", *(const double*)pVal);
		return sBuf;
	default: {
		das_datum dm;
		das_datum_init(&dm, pVal, (das_val_type)et, 0, NULL);
		das_datum_toStrValOnly(&dm, sBuf, nLen, 6);
		return sBuf;
	}
	}
}

DasErrCode DasSet_encode(DasSet* pThis, const char* sRole, DasBuf* pBuf)
{
	const DasDim* pDim = (const DasDim*) ((DasDesc*)pThis)->parent;
	const DasDs*  pDs  = (const DasDs*) ((DasDesc*)pDim)->parent;

	das_elem_type et = DasGen_elemType(pThis->pGen);
	das_gen_type  gt = DasGen_type(pThis->pGen);
	das_units units  = (pThis->units != NULL) ? pThis->units : UNIT_DIMENSIONLESS;

	das_pres_type pres = DasSet_presType(pThis);
	bool bByteRun = (pres == prString)||(pres == prBlob);
	bool bScalar  = (pres == prScalar);

	const char* sElement = "scalar";
	if(bByteRun)      sElement = "bytes";
	else if(!bScalar) sElement = "composite";

	/* 1. index= from the generator's own declared shape.  A sequence reports
	   the extents it DECLARED, borrow marks included; that is the point of
	   asking the generator rather than the merged dataset shape. */
	ptrdiff_t aExtShape[SETIDX_MAX] = SETIDX_INIT_UNUSED;
	int nExtRank = DasGen_extShape(pThis->pGen, aExtShape);

	/* An array-backed var writes the record index as ragged whatever extent it
	   declared, since storage grows as records arrive.  That override applies
	   only to a real extent: '-' and '^' are statements about the container and
	   outrank it.  Print from a COPY, because the declared shape is still needed
	   below (a header-values var is the one with SETIDX_UNUSED at index 0). */
	ptrdiff_t aPrnShape[SETIDX_MAX];
	memcpy(aPrnShape, aExtShape, sizeof(aPrnShape));
	if((gt == gtArray)&&(aPrnShape[0] != SETIDX_UNUSED)&&
	   (aPrnShape[0] != SETIDX_BORROW))
		aPrnShape[0] = SETIDX_RAGGED;

	char sIndex[128] = {'\0'};
	if(das_shape_toStr(aPrnShape, nExtRank, sIndex, sizeof(sIndex)) < 0)
		return DASERR_VAR;

	/* Items per record and whether any of them are ragged; index 0 is the record
	   index and never counts toward either. */
	int nItems = 1;
	bool bRaggedItems = false;
	for(int i = 1; i < nExtRank; ++i){
		if(aExtShape[i] == SETIDX_RAGGED)  bRaggedItems = true;
		else if(aExtShape[i] > 0)          nItems *= aExtShape[i];
	}

	/* 2. the open tag.  semantic is derived, not stored: it was spent at
	   parse and the writer re-states the interpretation for the reader.

	   use= is omitted when it equals the schema's default.  The schema carries
	   default="center", so any schema-aware reader recovers it for free and
	   writing it out is noise.  Contrast the <ops> parameters below, whose
	   defaults the schema can no longer state at all. */
	if(strcmp(sRole, "center") == 0)
		DasBuf_printf(pBuf, "    <%s", sElement);
	else
		DasBuf_printf(pBuf, "    <%s use=\"%s\"", sElement, sRole);

	if(!bByteRun){
		const char* sSem = das_sem_default((das_val_type)et, units);
		DasBuf_printf(pBuf, " semantic=\"%s\"", sSem ? sSem : "");
	}

	/* storage hint: scalar sequences state it outright (their storage is
	   invisible any other way) */
	if((gt == gtSeq)&&(bScalar)){
		DasBuf_printf(pBuf, " storage=\"%s\"",
			(et == etTime) ? "struct" : das_vt_toStr((das_val_type)et)
		);
	}

	/* storage hint: header values and text-encoded numerics need it */
	if(gt == gtArray){
		DasAry* pAry = DasGen_getArray(pThis->pGen);
		das_val_type vtAry = DasAry_valType(pAry);
		int nCkItems = 0;
		const DasCodec* pCkCodec = DasDs_getCodecFor(pDs, DasAry_id(pAry), &nCkItems);
		bool bHdrVals = (aExtShape[0] == SETIDX_UNUSED);
		bool bText = (pCkCodec != NULL)&&(pCkCodec->vtBuf == vtText);
		if((bHdrVals || bText) && !bByteRun &&
		   (vtAry != vtText) && (vtAry != vtByteSeq)){
			DasBuf_printf(pBuf, " storage=\"%s\"",
				(vtAry == vtTime) ? "struct" : das_vt_toStr(vtAry)
			);
		}
	}

	if((!bScalar)&&(!bByteRun)){
		const DasIntrSet* pComp = (const DasIntrSet*)pThis;
		char sIntern[64] = {'\0'};
		if(das_shape_toStr(pComp->aIntShape, pComp->nIntRank, sIntern, sizeof(sIntern)) < 0)
			return DASERR_VAR;
		DasBuf_printf(pBuf, " intern=\"%s\"", sIntern);
	}

	/* Absent units means dimensionless, so an empty units= carries exactly the
	   information absence does.  Emit only a real one. */
	if(units == UNIT_DIMENSIONLESS)
		DasBuf_printf(pBuf, " index=\"%s\">\n", sIndex);
	else
		DasBuf_printf(pBuf, " index=\"%s\" units=\"%s\">\n", sIndex, units);

	/* 3. child order is purpose to bytes: properties, ops, generator */
	if(DasDesc_length((DasDesc*)pThis) > 0){
		int nRet = DasDesc_encode3((DasDesc*)pThis, pBuf, "      ");
		if(nRet != DAS_OKAY) return nRet;
	}

	if((pThis->form.pKind != das_form_linear())&&
	   ((pThis->form.pKind != NULL)||(pThis->form.sToken[0] != '\0'))){

		const char* sTok = (pThis->form.pKind != NULL)
		                 ? pThis->form.pKind->sToken : pThis->form.sToken;
		DasBuf_printf(pBuf, "      <ops kind=\"%s\"", sTok);

		/* Process-local resolutions stay off the wire, and body= belongs to the
		   frame's context entry, which is where the reader put it. */
		for(int i = 0; i < pThis->form.nBinds; ++i){
			const char* sR = pThis->form.aBind[i].sRole;
			if((strcmp(sR,"frameId")==0)||(strcmp(sR,"surfId")==0)||
			   (strcmp(sR,"body")==0)) continue;
			DasBuf_printf(pBuf, " %s=\"%s\"", sR, pThis->form.aBind[i].sVal);
		}

		/* An <ops> writer is TALKATIVE: it states a parameter even when the value
		   is the default.  The schema cannot carry these defaults (there is no
		   type behind an anyAttribute), so a reader working from the .xsd alone
		   has no way to recover them.  Contrast use= above, whose default the
		   schema still declares and which is therefore safe to omit.  Guessing
		   system= wrong yields a wrong magnitude rather than a cosmetic slip. */
		if(strcmp(sTok, "geovec") == 0){
			if(das_formalism_getBind(&(pThis->form), "system") == NULL)
				DasBuf_puts(pBuf, " system=\"cartesian\"");
			if(das_formalism_getBind(&(pThis->form), "sysorder") == NULL){
				const DasIntrSet* pComp = (const DasIntrSet*)pThis;
				DasBuf_puts(pBuf, " sysorder=\"");
				for(ptrdiff_t c = 0; c < pComp->aIntShape[0]; ++c)
					DasBuf_printf(pBuf, "%s%td", (c>0)?";":"", c);
				DasBuf_puts(pBuf, "\"");
			}
		}
		DasBuf_puts(pBuf, "/>\n");
	}

	/* 4. the generator child */
	if(gt == gtSeq){
		const DasGenSeq* pSeq = (const DasGenSeq*)pThis->pGen;
		size_t uElem  = das_vt_size((das_val_type)et);
		size_t uSlope = (et == etTime) ? sizeof(double) : uElem;
		das_elem_type etSlope = (et == etTime) ? etDouble : et;

		for(int c = 0; c < pSeq->nComps; ++c){
			char sMin[64] = {'\0'};
			_set_seqValToStr(pSeq->aIntercept[c], et, sMin, sizeof(sMin));

			/* full-rank interval, '-'-aligned to index= */
			char sInterval[256] = {'\0'};
			char* pIv = sInterval;
			for(int i = 0; i < nExtRank; ++i){
				if(i > 0){ *pIv = ';'; ++pIv; }
				if(aExtShape[i] == SETIDX_UNUSED){ *pIv = '-'; ++pIv; }
				else{
					char sM[64] = {'\0'};
					_set_seqValToStr(pSeq->aInterval[c][i], etSlope, sM, sizeof(sM));
					int n = snprintf(pIv, (size_t)(sInterval + sizeof(sInterval) - pIv), "%s", sM);
					if(n > 0) pIv += n;
				}
			}
			(void)uSlope;
			DasBuf_printf(pBuf,
				"      <sequence minval=\"%s\" interval=\"%s\" />\n", sMin, sInterval
			);
		}
	}
	else if(gt == gtArray){
		DasAry* pAry = DasGen_getArray(pThis->pGen);
		das_val_type vtAry = DasAry_valType(pAry);
		int nItemsPerWrite = 0;
		const DasCodec* pCodec = DasDs_getCodecFor(pDs, DasAry_id(pAry), &nItemsPerWrite);

		const char* sSemC = das_sem_default((das_val_type)et, units);
		if(pres == prString)    sSemC = DAS_SEM_TEXT;
		else if(pres == prBlob) sSemC = DAS_SEM_BLOB;

		DasCodec codecHdr;
		if(pCodec == NULL){
			/* header values: no packet codec exists, make a transient writer */
			if(aExtShape[0] != SETIDX_UNUSED){
				return das_error(DASERR_VAR, "No codec provided for %s/%s/%s/%s packet data!",
					DasDs_id(pDs), DasDim_typeName(pDim), DasDim_id(pDim), sRole
				);
			}
			DasCodec_init(
				DASENC_WRITE, &codecHdr, pAry, sSemC, "utf8", DASENC_ITEM_TERM, ' ',
				units, NULL
			);
			DasBuf_puts(pBuf, "      <values>\n");
			int nWrite = (int)DasAry_size(pAry);
			int nVals = DasCodec_encode(&codecHdr, pBuf, DIM0, nWrite, DASENC_IN_HDR|DASENC_PKT_LAST);
			if(nVals < 0){
				return das_error(DASERR_VAR, "Error encoding data for %s/%s/%s/%s",
					DasDs_id(pDs), DasDim_typeName(pDim), DasDim_id(pDim), sRole
				);
			}
			DasBuf_puts(pBuf, "      </values>\n");
			DasCodec_deInit(&codecHdr);
		}
		else{
			/* fill: strings and blobs have an empty fill, bools a glyph */
			char sFill[64] = {'\0'};
			das_val_type vtExt = pCodec->vtBuf;
			if((sSemC == DAS_SEM_BOOL) && (vtExt == vtText)){
				strncpy(sFill, "*", sizeof(sFill) - 1);
			}
			else if(!bByteRun){
				das_datum dmFill;
				das_datum_init(&dmFill, DasAry_getFill(pAry), vtAry, das_vt_size(vtAry), units);
				das_datum_toStrValOnly(&dmFill, sFill, 63, 6);
			}

			char sItemBytes[16] = {'\0'};
			if(pCodec->nBufValSz < 1)
				strncpy(sItemBytes, "*", sizeof(sItemBytes) - 1);
			else
				snprintf(sItemBytes, sizeof(sItemBytes) - 1, "%d", pCodec->nBufValSz);

			char sValTerm[32] = {'\0'};
			if((pCodec->nBufValSz == DASENC_ITEM_TERM) && (pCodec->sSepSet[0] != '\0'))
				snprintf(sValTerm, sizeof(sValTerm) - 1, " valTerm=\"%c\"", pCodec->sSepSet[0]);

			char sIdxTerm[12 + (SETIDX_MAX - 1)*3] = {'\0'};
			if(pCodec->nSep > 1){
				int n = snprintf(sIdxTerm, sizeof(sIdxTerm), " idxTerm=\"");
				for(ubyte j = 1; (j < pCodec->nSep) && (n < (int)sizeof(sIdxTerm) - 4); ++j){
					char cLvl = pCodec->sSepSet[j];
					char cbuf[2] = {cLvl, '\0'};
					const char* sLvl = cbuf;
					switch(cLvl){
					case '\n': sLvl = "\\n"; break;  case '\t': sLvl = "\\t"; break;
					case '\r': sLvl = "\\r"; break;  case '\0': sLvl = "\\0"; break;
					}
					n += snprintf(sIdxTerm + n, sizeof(sIdxTerm) - n, "%s%s", (j > 1) ? "," : "", sLvl);
				}
				if(n < (int)sizeof(sIdxTerm))
					snprintf(sIdxTerm + n, sizeof(sIdxTerm) - n, "\"");
			}

			/* composites put intern-many values in each item run */
			if((!bScalar)&&(!bByteRun)){
				const DasIntrSet* pComp = (const DasIntrSet*)pThis;
				for(int i = 0; i < pComp->nIntRank; ++i)
					if(pComp->aIntShape[i] > 0) nItems *= pComp->aIntShape[i];
			}

			char sNumItems[16] = {'\0'};
			if(bRaggedItems)
				strncpy(sNumItems, "*", sizeof(sNumItems) - 1);
			else
				snprintf(sNumItems, sizeof(sNumItems) - 1, "%d", nItems);

			const char* sEnc = pCodec->sEncType;

			char sTrim[16] = {'\0'};
			if((pCodec->nBufValSz < 1) && (strcmp(sEnc, "utf8") == 0) && !DasCodec_isTrim(pCodec))
				strncpy(sTrim, " trim=\"false\"", sizeof(sTrim) - 1);

			DasBuf_printf(pBuf,
				"      <packet numItems=\"%s\" itemBytes=\"%s\" encoding=\"%s\"%s%s%s fill=\"%s\" />\n",
				sNumItems, sItemBytes, sEnc, sValTerm, sIdxTerm, sTrim, sFill
			);
		}
	}
	else{
		return das_error(DASERR_NOTIMP,
			"Serializing a generator of kind %d is not yet implemented", (int)gt
		);
	}

	DasBuf_printf(pBuf, "    </%s>\n", sElement);
	return DAS_OKAY;
}

/* ************************************************************************* */
/* The scalar case: a bare DasSet, pres == prScalar                          */

static bool _DasSetScalar_get(
	const DasSet* pThis, ptrdiff_t* pLoc, das_datum* pOut
){
	ubyte aBuf[DATUM_BUF_SZ];
	int nRet = DasGen_eval(pThis->pGen, pLoc, aBuf, sizeof(aBuf));
	if(nRet != 1) return false;

	if(pThis->form.pKind != NULL)
		return pThis->form.pKind->pack(pThis, aBuf, pOut);

	das_datum_init(
		pOut, aBuf, (das_val_type)DasGen_elemType(pThis->pGen), 0, pThis->units
	);
	return true;
}

static das_elem_type _DasSetScalar_elemType(const DasSet* pThis)
{
	return DasGen_elemType(pThis->pGen);
}

static das_pres_type _DasSetScalar_presType(const DasSet* pThis)
{
	(void)pThis;
	return prScalar;   /* derived: the scalar family presents one way only */
}

static int _DasSetScalar_shape(const DasSet* pThis, ptrdiff_t* pShape)
{
	return DasGen_extShape(pThis->pGen, pShape);
}

static int _DasSetScalar_intrShape(const DasSet* pThis, ptrdiff_t* pShape)
{
	(void)pThis; (void)pShape;
	return 0;                           /* a scalar has no internal index */
}

static char* _DasSetScalar_expression(
	const DasSet* pThis, char* sBuf, int nLen, unsigned int uFlags
){
	(void)uFlags;
	int n = snprintf(sBuf, (size_t)nLen, "scalar(%s%s%s)",
		pThis->units ? pThis->units : "",
		pThis->form.sToken[0] ? " " : "", pThis->form.sToken
	);
	(void)n;
	return sBuf;
}

static bool _DasSetScalar_isNumeric(const DasSet* pThis)
{
	das_elem_type et = DasGen_elemType(pThis->pGen);
	return (et != etUnknown)&&(et != etUByte);
}

static DasSet* _DasSetScalar_copy(const DasSet* pThis);

static const das_set_vt g_vtSetScalar = {
	_DasSetScalar_get,
	_DasSetScalar_elemType, _DasSetScalar_presType,
	_DasSetScalar_shape, _DasSetScalar_intrShape,
	_DasSetScalar_expression, _DasSetScalar_isNumeric,
	DasSet_incRef, DasSet_decRef,
	_DasSetScalar_copy
};

static DasSet* _DasSetScalar_copy(const DasSet* pThis)
{
	DasSet* pOut = (DasSet*)calloc(1, sizeof(DasSet));
	*pOut = *pThis;
	/* the struct copy dragged the source's property array along shallowly;
	   zero it so DasDesc_init builds fresh storage instead of inheriting
	   pointers it does not own */
	memset(&(pOut->base), 0, sizeof(DasDesc));
	DasDesc_init(&(pOut->base), VARIABLE);
	DasDesc_copyIn(&(pOut->base), &(pThis->base));
	pOut->nRef = 1;
	/* the GENERATOR is cloned so a later setArray on one owner cannot
	   re-aim the other; only the backing array is shared */
	if(pOut->pGen != NULL)
		pOut->pGen = DasGen_copy(pThis->pGen);
	return pOut;
}

DasSet* new_DasSetScalar(DasGen* pGen, das_units units, const char* sFormToken)
{
	if(pGen == NULL){
		das_error(DASERR_VAR, "Null generator for new_DasSetScalar");
		return NULL;
	}

	/* The wire allows a ';' units list; nothing in the library can hold
	   per-component units yet and keeping only the first entry would misstate
	   the data, so refuse (Dude's ruling 2026-07-27). */
	if((units != NULL)&&(strchr(units, ';') != NULL)){
		das_error(DASERR_NOTIMP,
			"Per-component units lists ('%s') are not yet supported, and "
			"keeping only the first entry would misstate the data", units
		);
		return NULL;
	}

	DasSet* pThis = (DasSet*)calloc(1, sizeof(DasSet));
	DasDesc_init(&(pThis->base), VARIABLE);
	pThis->vt    = &g_vtSetScalar;
	pThis->units = units;
	pThis->nRef  = 1;

	if(das_formalism_init(&(pThis->form), sFormToken) != DAS_OKAY){
		DasSet_decRef(pThis);
		return NULL;
	}

	pThis->pGen = pGen;
	DasGen_incRef(pGen);
	return pThis;
}


/* ************************************************************************* */
/* DasIntrSet: items with an internal index (composites, strings, blobs)    */

static bool _DasIntrSet_get(const DasSet* pBase, ptrdiff_t* pLoc, das_datum* pOut)
{
	const DasIntrSet* pThis = (const DasIntrSet*)pBase;

	if(pThis->content == icString){
		size_t uCount = 0;
		const ubyte* ptr = DasGen_at(pBase->pGen, pLoc, &uCount);
		if(ptr == NULL) return false;
		/* the datum is a VIEW: a pointer into backing storage */
		memcpy(pOut, &ptr, sizeof(const ubyte*));
		pOut->vt    = vtText;
		pOut->vsize = das_vt_size(vtText);
		pOut->units = pBase->units;
		return true;
	}

	if(pThis->content == icBlob){
		size_t uCount = 0;
		const ubyte* ptr = DasGen_at(pBase->pGen, pLoc, &uCount);
		if(ptr == NULL) return false;
		das_byteseq bs;
		bs.ptr = ptr;
		bs.sz  = uCount;
		memcpy(pOut, &bs, sizeof(das_byteseq));
		pOut->vt    = vtByteSeq;
		pOut->vsize = sizeof(das_byteseq);
		pOut->units = pBase->units;
		return true;
	}

	/* numeric composite: the formalism row interprets the run */
	if((pBase->form.pKind != NULL)&&(pBase->form.pKind->pack != NULL)&&
	   (pBase->form.pKind != das_form_linear())){
		ubyte aRun[DATUM_BUF_SZ];
		int nGot = DasGen_eval(pBase->pGen, pLoc, aRun, sizeof(aRun));
		if(nGot < 1) return false;
		return pBase->form.pKind->pack(pBase, aRun, pOut);
	}

	das_error(DASERR_NOTIMP,
		"No single-datum representation for a %s composite yet "
		"(components are readable through the generator)",
		pThis->base.form.sToken[0] ? pThis->base.form.sToken : "plain"
	);
	return false;
}

static das_elem_type _DasIntrSet_elemType(const DasSet* pThis)
{
	return DasGen_elemType(pThis->pGen);
}

static das_pres_type _DasIntrSet_presType(const DasSet* pBase)
{
	const DasIntrSet* pThis = (const DasIntrSet*)pBase;
	if(pThis->content == icString) return prString;
	if(pThis->content == icBlob)   return prBlob;

	/* Derived from the formalism, never stored.  Only linear, point and geovec
	   have rows today, so the complex/rotation/matrix/image arms below are
	   unreachable for now: those tokens leave pKind NULL and land on prGeneric,
	   which is correct behavior (carry the numbers, refuse the math).  The arms
	   start working the moment their rows are registered. */
	if(pBase->form.pKind == NULL) return prGeneric;
	const char* sTok = pBase->form.pKind->sToken;
	if(strcmp(sTok, "geovec") == 0)   return prVector;
	if(strcmp(sTok, "complex") == 0)  return prComplex;
	if(strcmp(sTok, "rotation") == 0) return prRotation;
	if(strcmp(sTok, "matrix") == 0)   return prMatrix;
	if(strcmp(sTok, "image") == 0)    return prImage;
	return prGeneric;
}

static int _DasIntrSet_shape(const DasSet* pThis, ptrdiff_t* pShape)
{
	return DasGen_extShape(pThis->pGen, pShape);
}

static int _DasIntrSet_intrShape(const DasSet* pBase, ptrdiff_t* pShape)
{
	const DasIntrSet* pThis = (const DasIntrSet*)pBase;
	memcpy(pShape, pThis->aIntShape, sizeof(ptrdiff_t)*(size_t)pThis->nIntRank);
	return pThis->nIntRank;
}

static char* _DasIntrSet_expression(
	const DasSet* pThis, char* sBuf, int nLen, unsigned int uFlags
){
	(void)uFlags;
	das_intrset_class ic = ((const DasIntrSet*)pThis)->content;
	const char* sFam = (ic == icString) ? "string" :
	                   (ic == icBlob)   ? "blob"   : "composite";
	snprintf(sBuf, (size_t)nLen, "%s(%s%s%s)", sFam,
		pThis->units ? pThis->units : "",
		pThis->form.sToken[0] ? " " : "", pThis->form.sToken
	);
	return sBuf;
}

static bool _DasIntrSet_isNumeric(const DasSet* pThis)
{
	return ((const DasIntrSet*)pThis)->content == icNumeric;
}

static DasSet* _DasIntrSet_copy(const DasSet* pThis)
{
	DasIntrSet* pOut = (DasIntrSet*)calloc(1, sizeof(DasIntrSet));
	*pOut = *((const DasIntrSet*)pThis);
	memset(&(pOut->base.base), 0, sizeof(DasDesc));   /* see scalar copy */
	DasDesc_init(&(pOut->base.base), VARIABLE);
	DasDesc_copyIn(&(pOut->base.base), &(pThis->base));
	pOut->base.nRef = 1;
	/* the GENERATOR is cloned so a later setArray on one owner cannot
	   re-aim the other; only the backing array is shared */
	if(pOut->base.pGen != NULL)
		pOut->base.pGen = DasGen_copy(pThis->pGen);
	return (DasSet*)pOut;
}

static const das_set_vt g_vtIntrSet = {
	_DasIntrSet_get,
	_DasIntrSet_elemType, _DasIntrSet_presType,
	_DasIntrSet_shape, _DasIntrSet_intrShape,
	_DasIntrSet_expression, _DasIntrSet_isNumeric,
	DasSet_incRef, DasSet_decRef,
	_DasIntrSet_copy
};

DasIntrSet* new_DasIntrSet(
	das_intrset_class ic, DasGen* pGen, das_units units, const char* sFormToken,
	int nIntRank, const ptrdiff_t* pIntShape
){
	if((pGen == NULL)||(pIntShape == NULL)||(nIntRank < 1)||
	   (nIntRank >= SETIDX_MAX)){
		das_error(DASERR_VAR, "Invalid arguments to new_DasIntrSet");
		return NULL;
	}

	das_elem_type et = DasGen_elemType(pGen);

	if((ic == icString)||(ic == icBlob)){
		if(et != etUByte){
			das_error(DASERR_VAR,
				"A byte-run set needs an etUByte source, not element type %d",
				(int)et
			);
			return NULL;
		}
		if((sFormToken != NULL)&&(sFormToken[0] != '\0')){
			das_error(DASERR_VAR,
				"A byte run has no auto-math; formalism '%s' refused", sFormToken
			);
			return NULL;
		}
		if(DasGen_type(pGen) != gtArray){
			das_error(DASERR_NOTIMP,
				"A %s datum is a pointer into storage; computed byte runs "
				"have no home to point at", (ic==icString)?"string":"blob"
			);
			return NULL;
		}
	}
	else if(ic == icNumeric){
		if((et == etUnknown)||(et == etTime)){
			das_error(DASERR_VAR,
				"Composite components must be plain numeric, not element "
				"type %d", (int)et
			);
			return NULL;
		}
	}
	else{
		das_error(DASERR_VAR, "Class %d is not an internal-run class", (int)ic);
		return NULL;
	}

	if((units != NULL)&&(strchr(units, ';') != NULL)){
		das_error(DASERR_NOTIMP,
			"Per-component units lists ('%s') are not yet supported, and "
			"keeping only the first entry would misstate the data", units
		);
		return NULL;
	}

	DasIntrSet* pThis = (DasIntrSet*)calloc(1, sizeof(DasIntrSet));
	DasDesc_init(&(pThis->base.base), VARIABLE);
	pThis->base.vt    = &g_vtIntrSet;
	pThis->content    = ic;
	pThis->base.units = units;
	pThis->base.nRef  = 1;

	/* byte runs carry NO formalism, not even the linear default: a byte run
	   has no auto-math, and an empty form (pKind NULL) can never match a
	   registry rule.  calloc already zeroed it. */
	if(ic == icNumeric){
		if(das_formalism_init(&(pThis->base.form), sFormToken) != DAS_OKAY){
			free(pThis);
			return NULL;
		}
	}

	pThis->nIntRank = nIntRank;
	memcpy(pThis->aIntShape, pIntShape, sizeof(ptrdiff_t)*(size_t)nIntRank);

	pThis->base.pGen = pGen;
	DasGen_incRef(pGen);
	return pThis;
}


/* ************************************************************************* */
/* Operation sets: the registry made runnable                                */

DasSet* new_DasSetBinaryOp(DasSet* pLeft, char cOp, DasSet* pRight)
{
	if((pLeft == NULL)||(pRight == NULL)){
		das_error(DASERR_VAR, "Null operand for a binary operation");
		return NULL;
	}
	if((DasSet_presType(pLeft) != prScalar)||
	   (DasSet_presType(pRight) != prScalar)){
		das_error(DASERR_NOTIMP,
			"Operations are scalar-only in v1; composite arithmetic arrives "
			"with its own registry rules"
		);
		return NULL;
	}

	das_form_op op;
	switch(cOp){
	case '+': op = dfoAdd; break;
	case '-': op = dfoSub; break;
	case '*': op = dfoMul; break;
	default:
		das_error(DASERR_VAR, "Unknown operator '%c'", cOp);
		return NULL;
	}

	const das_form_rule* pRule = das_form_findRule(
		op, pLeft->form.pKind, pRight->form.pKind
	);
	if(pRule == NULL){
		das_error(DASERR_VAR,
			"No rule for %s %c %s: the pairing is undefined",
			pLeft->form.pKind ? pLeft->form.pKind->sToken : pLeft->form.sToken,
			cOp,
			pRight->form.pKind ? pRight->form.pKind->sToken : pRight->form.sToken
		);
		return NULL;
	}

	das_formalism formOut;
	das_units unitsOut = NULL;
	double rRightScale = 1.0;
	if(!pRule->resolve(
		&(pLeft->form), &(pRight->form), pLeft->units, pRight->units,
		&formOut, &unitsOut, &rRightScale
	))
		return NULL;   /* resolve already spoke loudly */

	das_elem_type etOut = pRule->outElem(
		DasGen_elemType(pLeft->pGen),
		(rRightScale != 1.0) ? etDouble : DasGen_elemType(pRight->pGen)
	);

	DasGen* pGen = new_DasGenBinop(
		pRule->apply, etOut, pLeft->pGen, pRight->pGen, rRightScale
	);
	if(pGen == NULL) return NULL;

	DasSet* pThis = new_DasSetScalar(pGen, unitsOut, NULL);
	DasGen_decRef(pGen);   /* the set holds the surviving reference */
	if(pThis == NULL) return NULL;

	pThis->form = formOut;
	return pThis;
}
