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
static const das_form_kind* g_formTable[] = {
	&g_kindLinear,
	&g_kindPoint,
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

/* ************************************************************************* */
/* The binop registry.  Sparse, ordered, misses fail loud in the caller.     */
/*                                                                           */
/* v1 content is the linear ring plus the point pilot, so the dispatch path  */
/* is exercised by shipping math before any composite rules exist.  No slot  */
/* is ever NULL: a generic formalism's NULL kind matches nothing, so unknown */
/* math is refused by construction.                                          */

static bool _affine_apply_sub(
	das_elem_type et, const ubyte* pL, const ubyte* pR, ubyte* pOut
){
	switch(et){
	case etLong:   *((int64_t*)pOut) = *((const int64_t*)pL) - *((const int64_t*)pR); return true;
	case etDouble: *((double*)pOut)  = *((const double*)pL)  - *((const double*)pR);  return true;
	default: return false;
	}
}

static bool _affine_apply_add(
	das_elem_type et, const ubyte* pL, const ubyte* pR, ubyte* pOut
){
	switch(et){
	case etLong:   *((int64_t*)pOut) = *((const int64_t*)pL) + *((const int64_t*)pR); return true;
	case etDouble: *((double*)pOut)  = *((const double*)pL)  + *((const double*)pR);  return true;
	default: return false;
	}
}

/* The linear ring: the common operators every plain number always had, now
   registered like everything else so no operation bypasses the registry. */
static bool _linear_res_addsub(
	const das_formalism* pL, const das_formalism* pR,
	das_units uL, das_units uR, das_formalism* pOut, das_units* pUnitsOut
){
	(void)pL; (void)pR;
	if(uL != uR){
		das_error(DASERR_VAR,
			"Adding %s to %s needs one unit on both sides (convert first)",
			uL, uR
		);
		return false;
	}
	das_formalism_init(pOut, NULL);
	*pUnitsOut = uL;
	return true;
}

static bool _linear_res_mul(
	const das_formalism* pL, const das_formalism* pR,
	das_units uL, das_units uR, das_formalism* pOut, das_units* pUnitsOut
){
	(void)pL; (void)pR;
	das_formalism_init(pOut, NULL);
	*pUnitsOut = Units_multiply(uL, uR);
	return true;
}

static bool _linear_apply_mul(
	das_elem_type et, const ubyte* pL, const ubyte* pR, ubyte* pOut
){
	switch(et){
	case etLong:   *((int64_t*)pOut) = *((const int64_t*)pL) * *((const int64_t*)pR); return true;
	case etDouble: *((double*)pOut)  = *((const double*)pL)  * *((const double*)pR);  return true;
	default: return false;
	}
}

/* point - point = interval.  Both points must sit on the same epoch; v1 is
   strict identity, convert before subtracting. */
static bool _affine_res_sub_pp(
	const das_formalism* pL, const das_formalism* pR,
	das_units uL, das_units uR, das_formalism* pOut, das_units* pUnitsOut
){
	(void)pL; (void)pR;
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
	das_units uL, das_units uR, das_formalism* pOut, das_units* pUnitsOut
){
	(void)pR;
	if(uR != Units_interval(uL)){
		das_error(DASERR_VAR,
			"point + interval needs %s on the right for epoch %s, have %s "
			"(convert first)", Units_interval(uL), uL, uR
		);
		return false;
	}
	*pOut = *pL;          /* result is a point on the same epoch */
	*pUnitsOut = uL;
	return true;
}

static bool _affine_res_add_ip(
	const das_formalism* pL, const das_formalism* pR,
	das_units uL, das_units uR, das_formalism* pOut, das_units* pUnitsOut
){
	return _affine_res_add_pi(pR, pL, uR, uL, pOut, pUnitsOut);
}

static const das_form_rule g_ruleTable[] = {
	{ dfoAdd, &g_kindLinear, &g_kindLinear, _linear_res_addsub, _affine_apply_add },
	{ dfoSub, &g_kindLinear, &g_kindLinear, _linear_res_addsub, _affine_apply_sub },
	{ dfoMul, &g_kindLinear, &g_kindLinear, _linear_res_mul,    _linear_apply_mul },
	{ dfoSub, &g_kindPoint,  &g_kindPoint,  _affine_res_sub_pp, _affine_apply_sub },
	{ dfoAdd, &g_kindPoint,  &g_kindLinear, _affine_res_add_pi, _affine_apply_add },
	{ dfoAdd, &g_kindLinear, &g_kindPoint,  _affine_res_add_ip, _affine_apply_add },
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
	DasDesc_init(&(pOut->base), VARIABLE);
	DasDesc_copyIn(&(pOut->base), &(pThis->base));
	pOut->nRef = 1;
	if(pOut->pGen != NULL) DasGen_incRef(pOut->pGen);
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
	pThis->fam   = sfScalar;
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
