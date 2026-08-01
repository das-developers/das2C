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
#include "dimension.h"
#include "property.h"
#include "dataset.h"
#include "stream.h"
#include "variable.h"


/* Axis D used to live here: a static formalism table, per-row pack/prnIntr
   functions, and a sparse binop rule registry keyed on (op, left row, right
   row).  All of it is now form.h plus one file per formalism, dispatched
   through DasForm_VTbl rather than looked up.

   The rules were HARVESTED, not discarded:
     the linear ring (units via Units_canConvert / Units_multiply)  -> form_linear.c
     the affine rules (Units_interval, "time minus time is a span") -> form_point.c
     the geovec row (pack, sysorder, frame binding)                 -> form_vector.c

   The far-corner sketches that sat here for matrix and image went with them;
   see co_notes/libdas_form_class_spec.md for the taxonomy and the punch list.
*/

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
		if(pShape[i] != VARIDX_UNUSED) ++nUsed;
	
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
		
		if(pShape[i] == VARIDX_UNUSED){ 
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
			if((pShape[i] == VARIDX_RAGGED)||(pShape[i] == VARIDX_BORROW)){
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
/* DasVar base                                                               */

int DasVar_incRef(DasVar* pThis)
{
	pThis->nRef += 1;
	return pThis->nRef;
}

int DasVar_decRef(DasVar* pThis)
{
	assert(pThis->nRef > 0);
	pThis->nRef -= 1;
	if(pThis->nRef == 0){
		if(pThis->pGen != NULL) DasGen_decRef(pThis->pGen);

		/* Forms are refcounted and SHARED, never cloned: they are immutable
		   once built and validated, so a copy can point at the same one.  That
		   makes releasing here mandatory.  _DasVarBin_decRef always did it and
		   the three generator-backed classes reach THIS function instead, so
		   every scalar, composite and byte run leaked a form until now. */
		DasForm_decRef(pThis->pForm);

		DasDesc_freeProps(&(pThis->base));
		free(pThis);
		return 0;
	}
	return pThis->nRef;
}

bool DasVar_get(const DasVar* pThis, ptrdiff_t* pLoc, das_datum* pOut)
{
	return pThis->pVTbl->get(pThis, pLoc, pOut);
}

int DasVar_shape(const DasVar* pThis, ptrdiff_t* pShape)
{
	return pThis->pVTbl->shape(pThis, pShape);
}

ptrdiff_t DasVar_lengthIn(const DasVar* pThis, int nIdx, ptrdiff_t* pLoc)
{
	return DasGen_lengthIn(pThis->pGen, nIdx, pLoc);
}

const char* DasVar_role(const DasVar* pThis)
{
	const DasDim* pDim = (const DasDim*)DasDesc_parent((const DasDesc*)pThis);

	/* No parent, no role.  A variable built on its own is perfectly good;
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
		"Dimension '%s' is the parent of a variable it has no role for",
		DasDim_id(pDim)
	);
	return NULL;
}

/* Shared body for the two public subset entries.  The division of labor:
   the VARIABLE owns rank agreement, slice geometry, the rank-0 refusal, naming,
   units and the element-vs-presentation decision; the GENERATOR owns whether
   a view is possible and how bytes are produced. */
static DasAry* _DasVar_subset(
	const DasVar* pThis, int nRank, const ptrdiff_t* pMin, const ptrdiff_t* pMax,
	bool bMustCopy
){
	ptrdiff_t aSetShape[VARIDX_MAX];
	int nExtRank = DasVar_shape(pThis, aSetShape);

	if(nRank != nExtRank){
		das_error(DASERR_VAR,
			"Variable is external rank %d, but the subset specification is rank %d",
			nExtRank, nRank
		);
		return NULL;
	}

	size_t aSliceShape[VARIDX_MAX] = VARIDX_INIT_BEGIN;
	int nSliceRank = das_rng2shape(nRank, pMin, pMax, aSliceShape);
	if(nSliceRank < 0) return NULL;
	if(nSliceRank == 0){
		das_error(DASERR_VAR,
			"Can't output a rank 0 array, use DasVar_get() for single items"
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
		if(nSliceRank >= VARIDX_MAX){
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
		(pAry != NULL) ? DasAry_id(pAry) : "variable"
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

DasAry* DasVar_subset(
	const DasVar* pThis, int nRank, const ptrdiff_t* pMin, const ptrdiff_t* pMax
){
	return _DasVar_subset(pThis, nRank, pMin, pMax, false);
}

DasAry* DasVar_subsetCopy(
	const DasVar* pThis, int nRank, const ptrdiff_t* pMin, const ptrdiff_t* pMax
){
	return _DasVar_subset(pThis, nRank, pMin, pMax, true);
}

bool DasVar_degenerate(const DasVar* pThis, int iIndex)
{
	ptrdiff_t aShape[VARIDX_MAX];
	int nRank = DasVar_shape(pThis, aShape);
	if((iIndex < 0)||(iIndex >= nRank)) return true;
	return aShape[iIndex] == VARIDX_UNUSED;
}

int DasVar_intrShape(const DasVar* pThis, ptrdiff_t* pShape)
{
	return pThis->pVTbl->intrShape(pThis, pShape);
}

bool DasVar_isNumeric(const DasVar* pThis)
{
	return pThis->pVTbl->isNumeric(pThis);
}

char* DasVar_toStr(const DasVar* pThis, char* sBuf, int nLen)
{
	return pThis->pVTbl->expression(pThis, sBuf, nLen, 0);
}

DasVar* DasVar_copy(const DasVar* pThis)
{
	return pThis->pVTbl->copy(pThis);
}

bool DasVar_setArray(DasVar* pThis, DasAry* pNew)
{
	if(!DasGen_setArray(pThis->pGen, pNew))
		return false;
	pThis->units = DasAry_units(pNew);
	return true;
}

das_val_type DasVar_valType(const DasVar* pThis)
{
	/* A byte run is the only formless class -- its CLASS says it carries no
	   math -- and the sentinel is what tells a string from a blob. */
	if(pThis->pForm == NULL){
		return ((const DasVarBytes*)pThis)->bSentinel ? vtText : vtByteSeq;
	}

	/* Otherwise the FORM names its own datum type.  This is what replaced the
	   presentation enum: one authority that each formalism answers for itself,
	   instead of a switch here that had to grow an arm per formalism and be
	   kept in agreement with the token it switched on. */
	das_val_type vt = pThis->pForm->pVTbl->datumType(pThis->pForm);
	if(vt != vtUnknown) return vt;

	/* vtUnknown means "no datum type of my own", so fall back to storage. */
	return (das_val_type)DasGen_elemType(pThis->pGen);
}

/* DasVar_vecMap() and das_makeCompLabels() were RETIRED here, not moved.
   Both were geovec knowledge in the generic variable layer, and answering
   them from here would mean variable.c including form_vector.h -- library code
   learning one specific formalism.

   The replacement is client-side: DasFormVector_slotSym() hands out the
   symbol for one storage slot and the caller composes whatever label it
   wants.  das3_csv and das3_cdf are the two consumers; see
   co_notes/downstream_fixups.md. */

/* ************************************************************************* */
/* The scalar case: a bare DasVar, no internal index                         */

static bool _DasVarScalar_get(
	const DasVar* pThis, ptrdiff_t* pLoc, das_datum* pOut
){
	ubyte aBuf[DATUM_BUF_SZ];
	int nRet = DasGen_eval(pThis->pGen, pLoc, aBuf, sizeof(aBuf));
	if(nRet != 1) return false;

	/* The form packs, because it is the only thing that knows what the bits
	   mean.  A form with no pack slot has no single-datum representation,
	   which is an answer rather than a failure. */
	if((pThis->pForm != NULL)&&(pThis->pForm->pVTbl->pack != NULL)){
		das_operand op;
		if(!DasVar_operand(pThis, &op)) return false;
		if(pThis->pForm->pVTbl->pack(pThis->pForm, &op, aBuf, pOut))
			return true;
	}

	das_datum_init(
		pOut, aBuf, (das_val_type)DasGen_elemType(pThis->pGen), 0, pThis->units
	);
	return true;
}

static const char* _DasVarScalar_element(const DasVar* pThis)
{
	(void)pThis;
	return "scalar";
}

static das_elem_type _DasVarScalar_elemType(const DasVar* pThis)
{
	return DasGen_elemType(pThis->pGen);
}

static int _DasVarScalar_shape(const DasVar* pThis, ptrdiff_t* pShape)
{
	return DasGen_extShape(pThis->pGen, pShape);
}

static int _DasVarScalar_intrShape(const DasVar* pThis, ptrdiff_t* pShape)
{
	(void)pThis; (void)pShape;
	return 0;                           /* a scalar has no internal index */
}

static char* _DasVarScalar_expression(
	const DasVar* pThis, char* sBuf, int nLen, unsigned int uFlags
){
	(void)uFlags;
	int n = snprintf(sBuf, (size_t)nLen, "scalar(%s%s%s)",
		pThis->units ? pThis->units : "",
		(pThis->pForm != NULL) ? " " : "",
		(pThis->pForm != NULL) ? DasForm_kindStr(pThis->pForm) : ""
	);
	(void)n;
	return sBuf;
}

static bool _DasVarScalar_isNumeric(const DasVar* pThis)
{
	das_elem_type et = DasGen_elemType(pThis->pGen);
	return (et != etUnknown)&&(et != etUByte);
}

static DasVar* _DasVarScalar_copy(const DasVar* pThis);

/* The generator-backed answers to the two slots a computed variable has to
   override.  Every class built on a DasGen shares these; var_bin.c supplies its
   own,
   which is the whole reason these are vtable slots and not shared code. */
static ptrdiff_t _DasVarGen_lengthIn(
	const DasVar* pThis, int nIdx, ptrdiff_t* pLoc
){
	return DasGen_lengthIn(pThis->pGen, nIdx, pLoc);
}

static int _DasVarGen_subsetInto(
	const DasVar* pThis, int nExtRank, const ptrdiff_t* pMin,
	const ptrdiff_t* pMax, ubyte* pBuf, size_t uBufLen
){
	return DasGen_subsetInto(pThis->pGen, nExtRank, pMin, pMax, pBuf, uBufLen);
}

static const DasVar_VTbl g_vtblVarScalar = {
	_DasVarScalar_get, _DasVarScalar_element,
	_DasVarScalar_elemType,
	_DasVarScalar_shape, _DasVarScalar_intrShape,
	_DasVarGen_lengthIn, _DasVarGen_subsetInto,
	_DasVarScalar_expression, _DasVarScalar_isNumeric,
	DasVar_incRef, DasVar_decRef,
	_DasVarScalar_copy
};

static DasVar* _DasVarScalar_copy(const DasVar* pThis)
{
	DasVar* pOut = (DasVar*)calloc(1, sizeof(DasVar));
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

	/* The struct copy above carried pForm across SHALLOWLY.  Claim a reference
	   for the copy, or the two of them release one reference between them. */
	DasForm_incRef(pOut->pForm);
	return pOut;
}

DasVar* new_DasVar(DasGen* pGen, das_units units, DasForm* pForm)
{
	if(pGen == NULL){
		das_error(DASERR_VAR, "Null generator for new_DasVar");
		return NULL;
	}
	/* Checked before anything is allocated, so the error path leaks nothing.
	   NULL is not "no formalism" here -- an absent <ops> binds the explicit
	   linear form, and only a byte run's CLASS gets to say it has no math. */
	if(pForm == NULL){
		das_error(DASERR_VAR,
			"A scalar needs a formalism; pass the linear form, not NULL");
		return NULL;
	}

	/* THE gate every construction path passes through.  A form arrives here
	   either from the wire (new_DasForm_pairs) or from an application calling
	   a typed constructor, and this is the first point where the form and the
	   shape it has to describe are both known.  See form.h's validate slot. */
	if(DasForm_validate(pForm, 0, NULL) != DAS_OKAY){
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

	DasVar* pThis = (DasVar*)calloc(1, sizeof(DasVar));
	DasDesc_init(&(pThis->base), VARIABLE);
	pThis->pVTbl    = &g_vtblVarScalar;
	pThis->units = units;
	pThis->nRef  = 1;

	pThis->pForm = pForm;   /* the reference is TAKEN, not cloned */

	pThis->pGen = pGen;
	DasGen_incRef(pGen);
	return pThis;
}


/* ************************************************************************* */
/* DasVarComp: the numeric component run, the <composite> element            */

static bool _DasVarComp_get(const DasVar* pBase, ptrdiff_t* pLoc, das_datum* pOut)
{
	/* the formalism interprets the run */
	if((pBase->pForm != NULL)&&(pBase->pForm->pVTbl->pack != NULL)){
		ubyte aRun[DATUM_BUF_SZ];
		int nGot = DasGen_eval(pBase->pGen, pLoc, aRun, sizeof(aRun));
		if(nGot < 1) return false;

		das_operand op;
		if(!DasVar_operand(pBase, &op)) return false;
		return pBase->pForm->pVTbl->pack(pBase->pForm, &op, aRun, pOut);
	}

	das_error(DASERR_NOTIMP,
		"No single-datum representation for a %s composite "
		"(components are readable through the generator)",
		(pBase->pForm != NULL) ? DasForm_kindStr(pBase->pForm) : "plain"
	);
	return false;
}

static const char* _DasVarComp_element(const DasVar* pThis)
{
	(void)pThis;
	return "composite";
}

static das_elem_type _DasVarIntr_elemType(const DasVar* pThis)
{
	return DasGen_elemType(pThis->pGen);
}

static int _DasVarIntr_shape(const DasVar* pThis, ptrdiff_t* pShape)
{
	return DasGen_extShape(pThis->pGen, pShape);
}

static int _DasVarComp_intrShape(const DasVar* pBase, ptrdiff_t* pShape)
{
	const DasVarComp* pThis = (const DasVarComp*)pBase;
	memcpy(pShape, pThis->aIntShape, sizeof(ptrdiff_t)*(size_t)pThis->nIntRank);
	return pThis->nIntRank;
}

static char* _DasVarComp_expression(
	const DasVar* pThis, char* sBuf, int nLen, unsigned int uFlags
){
	(void)uFlags;
	snprintf(sBuf, (size_t)nLen, "composite(%s%s%s)",
		pThis->units ? pThis->units : "",
		(pThis->pForm != NULL) ? " " : "",
		(pThis->pForm != NULL) ? DasForm_kindStr(pThis->pForm) : ""
	);
	return sBuf;
}

static bool _DasVar_isNumericTrue(const DasVar* pThis)
{
	(void)pThis;
	return true;
}

static DasVar* _DasVarComp_copy(const DasVar* pThis)
{
	DasVarComp* pOut = (DasVarComp*)calloc(1, sizeof(DasVarComp));
	*pOut = *((const DasVarComp*)pThis);
	memset(&(pOut->base.base), 0, sizeof(DasDesc));   /* see scalar copy */
	DasDesc_init(&(pOut->base.base), VARIABLE);
	DasDesc_copyIn(&(pOut->base.base), &(pThis->base));
	pOut->base.nRef = 1;
	/* the GENERATOR is cloned so a later setArray on one owner cannot
	   re-aim the other; only the backing array is shared */
	if(pOut->base.pGen != NULL)
		pOut->base.pGen = DasGen_copy(pThis->pGen);

	DasForm_incRef(pOut->base.pForm);
	return (DasVar*)pOut;
}

static const DasVar_VTbl g_vtblVarComp = {
	_DasVarComp_get, _DasVarComp_element,
	_DasVarIntr_elemType,
	_DasVarIntr_shape, _DasVarComp_intrShape,
	_DasVarGen_lengthIn, _DasVarGen_subsetInto,
	_DasVarComp_expression, _DasVar_isNumericTrue,
	DasVar_incRef, DasVar_decRef,
	_DasVarComp_copy
};

/* Shared by both internal-run constructors: one units string per variable.
   A run carries one units string for every cell in it, so a per-component list has
   nowhere to live. */
static bool _intr_unitsOk(das_units units)
{
	if((units != NULL)&&(strchr(units, ';') != NULL)){
		das_error(DASERR_NOTIMP,
			"Per-component units lists ('%s') are not yet supported, and "
			"keeping only the first entry would misstate the data", units
		);
		return false;
	}
	return true;
}

DasVarComp* new_DasVarComp(
	DasGen* pGen, das_units units, DasForm* pForm,
	int nIntRank, const ptrdiff_t* pIntShape
){
	if((pGen == NULL)||(pIntShape == NULL)||(pForm == NULL)||(nIntRank < 1)||
	   (nIntRank >= VARIDX_MAX)){
		das_error(DASERR_VAR, "Invalid arguments to new_DasVarComp");
		return NULL;
	}

	if(DasForm_validate(pForm, nIntRank, pIntShape) != DAS_OKAY)
		return NULL;

	das_elem_type et = DasGen_elemType(pGen);
	if((et == etUnknown)||(et == etTime)){
		das_error(DASERR_VAR,
			"Composite components must be plain numeric, not element type %d",
			(int)et
		);
		return NULL;
	}

	if(!_intr_unitsOk(units)) return NULL;

	DasVarComp* pThis = (DasVarComp*)calloc(1, sizeof(DasVarComp));
	DasDesc_init(&(pThis->base.base), VARIABLE);
	pThis->base.pVTbl    = &g_vtblVarComp;
	pThis->base.units = units;
	pThis->base.nRef  = 1;

	pThis->base.pForm = pForm;   /* the reference is TAKEN, not cloned */

	pThis->nIntRank = nIntRank;
	memcpy(pThis->aIntShape, pIntShape, sizeof(ptrdiff_t)*(size_t)nIntRank);

	pThis->base.pGen = pGen;
	DasGen_incRef(pGen);
	return pThis;
}


/* ************************************************************************* */
/* DasVarBytes: the byte run, the <bytes> element                             */

static bool _DasVarBytes_get(const DasVar* pBase, ptrdiff_t* pLoc, das_datum* pOut)
{
	const DasVarBytes* pThis = (const DasVarBytes*)pBase;

	size_t uCount = 0;
	const ubyte* ptr = DasGen_at(pBase->pGen, pLoc, &uCount);
	if(ptr == NULL) return false;

	if(pThis->bSentinel){
		/* the datum is a VIEW: a pointer into backing storage.  The trailing
		   null the sentinel rule guarantees is what makes a bare char* legal. */
		memcpy(pOut, &ptr, sizeof(const ubyte*));
		pOut->vt    = vtText;
		pOut->vsize = das_vt_size(vtText);
	}
	else{
		das_byteseq bs;
		bs.ptr = ptr;
		bs.sz  = uCount;
		memcpy(pOut, &bs, sizeof(das_byteseq));
		pOut->vt    = vtByteSeq;
		pOut->vsize = sizeof(das_byteseq);
	}
	pOut->units = pBase->units;
	return true;
}

static const char* _DasVarBytes_element(const DasVar* pThis)
{
	(void)pThis;
	return "bytes";
}

static int _DasVarBytes_intrShape(const DasVar* pBase, ptrdiff_t* pShape)
{
	pShape[0] = ((const DasVarBytes*)pBase)->nExtent;
	return 1;   /* a byte run's internal rank is always 1 */
}

static char* _DasVarBytes_expression(
	const DasVar* pThis, char* sBuf, int nLen, unsigned int uFlags
){
	(void)uFlags;
	snprintf(sBuf, (size_t)nLen, "%s(%s)",
		((const DasVarBytes*)pThis)->bSentinel ? "string" : "blob",
		pThis->units ? pThis->units : ""
	);
	return sBuf;
}

static bool _DasVar_isNumericFalse(const DasVar* pThis)
{
	(void)pThis;
	return false;
}

static DasVar* _DasVarBytes_copy(const DasVar* pThis)
{
	DasVarBytes* pOut = (DasVarBytes*)calloc(1, sizeof(DasVarBytes));
	*pOut = *((const DasVarBytes*)pThis);
	memset(&(pOut->base.base), 0, sizeof(DasDesc));   /* see scalar copy */
	DasDesc_init(&(pOut->base.base), VARIABLE);
	DasDesc_copyIn(&(pOut->base.base), &(pThis->base));
	pOut->base.nRef = 1;
	if(pOut->base.pGen != NULL)
		pOut->base.pGen = DasGen_copy(pThis->pGen);

	DasForm_incRef(pOut->base.pForm);
	return (DasVar*)pOut;
}

static const DasVar_VTbl g_vtblVarBytes = {
	_DasVarBytes_get, _DasVarBytes_element,
	_DasVarIntr_elemType,
	_DasVarIntr_shape, _DasVarBytes_intrShape,
	_DasVarGen_lengthIn, _DasVarGen_subsetInto,
	_DasVarBytes_expression, _DasVar_isNumericFalse,
	DasVar_incRef, DasVar_decRef,
	_DasVarBytes_copy
};

DasVarBytes* new_DasVarBytes(
	DasGen* pGen, das_units units, bool bSentinel, ptrdiff_t nExtent
){
	if(pGen == NULL){
		das_error(DASERR_VAR, "Invalid arguments to new_DasVarBytes");
		return NULL;
	}

	das_elem_type et = DasGen_elemType(pGen);
	if(et != etUByte){
		das_error(DASERR_VAR,
			"A byte-run variable needs an etUByte source, not element type %d", (int)et
		);
		return NULL;
	}
	if(DasGen_type(pGen) != gtArray){
		das_error(DASERR_NOTIMP,
			"A %s datum is a pointer into storage; computed byte runs have no "
			"home to point at", bSentinel ? "string" : "blob"
		);
		return NULL;
	}

	if(!_intr_unitsOk(units)) return NULL;

	DasVarBytes* pThis = (DasVarBytes*)calloc(1, sizeof(DasVarBytes));
	DasDesc_init(&(pThis->base.base), VARIABLE);
	pThis->base.pVTbl    = &g_vtblVarBytes;
	pThis->base.units = units;
	pThis->base.nRef  = 1;

	/* base.form stays zeroed.  There is no formalism argument to this call:
	   the class is how "no auto-math" is stated, and a zeroed form can never
	   match a registry rule. */

	pThis->bSentinel = bSentinel;
	pThis->nExtent   = nExtent;

	pThis->base.pGen = pGen;
	DasGen_incRef(pGen);
	return pThis;
}

