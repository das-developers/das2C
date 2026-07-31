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
#include "geovec.h"
#include "dimension.h"
#include "property.h"
#include "dataset.h"
#include "stream.h"
#include "set.h"


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

/* Climb the descriptor chain to the stream that owns this set's context
   entries.  set -> dim -> dataset -> stream in a parsed stream; NULL for a
   set a program built and has not attached yet.

   Used ONLY to turn a context handle back into a name, for serializing or for
   explaining a refusal.  The math never calls this -- bindings compare by
   handle -- so a detached set gets a vaguer message, never a wrong answer. */
const DasCtxTbl* _DasSet_ctxTbl(const DasSet* pThis)
{
	const DasDesc* pDesc = (const DasDesc*)pThis;

	while(pDesc != NULL){
		if(DasDesc_type(pDesc) == STREAM)
			return DasStream_ctxTbl((const DasStream*)pDesc);
		pDesc = DasDesc_parent(pDesc);
	}
	return NULL;
}

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
	return pThis->pVTbl->get(pThis, pLoc, pOut);
}

int DasSet_shape(const DasSet* pThis, ptrdiff_t* pShape)
{
	return pThis->pVTbl->shape(pThis, pShape);
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
	das_error(DASERR_SET,
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
		das_error(DASERR_SET,
			"Set is external rank %d, but the subset specification is rank %d",
			nExtRank, nRank
		);
		return NULL;
	}

	size_t aSliceShape[SETIDX_MAX] = SETIDX_INIT_BEGIN;
	int nSliceRank = das_rng2shape(nRank, pMin, pMax, aSliceShape);
	if(nSliceRank < 0) return NULL;
	if(nSliceRank == 0){
		das_error(DASERR_SET,
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
			das_error(DASERR_SET, "Subset rank %d leaves no room for the "
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
	return pThis->pVTbl->intrShape(pThis, pShape);
}

bool DasSet_isNumeric(const DasSet* pThis)
{
	return pThis->pVTbl->isNumeric(pThis);
}

char* DasSet_toStr(const DasSet* pThis, char* sBuf, int nLen)
{
	return pThis->pVTbl->expression(pThis, sBuf, nLen, 0);
}

DasSet* DasSet_copy(const DasSet* pThis)
{
	return pThis->pVTbl->copy(pThis);
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
	/* A byte run is the only formless class -- its CLASS says it carries no
	   math -- and the sentinel is what tells a string from a blob. */
	if(pThis->pForm == NULL){
		return ((const DasByteSet*)pThis)->bSentinel ? vtText : vtByteSeq;
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

/* DasSet_vecMap() and das_makeCompLabels() were RETIRED here, not moved.
   Both were geovec knowledge living in the generic set layer, and answering
   them from here would mean set.c including form_vector.h -- library code
   learning one specific formalism.

   The replacement is client-side: DasFormVector_slotSym() hands out the
   symbol for one storage slot and the caller composes whatever label it
   wants.  das3_csv and das3_cdf are the two consumers; see
   co_notes/downstream_fixups.md. */

/* ************************************************************************* */
/* The scalar case: a bare DasSet, pres == prScalar                          */

static bool _DasSetScalar_get(
	const DasSet* pThis, ptrdiff_t* pLoc, das_datum* pOut
){
	ubyte aBuf[DATUM_BUF_SZ];
	int nRet = DasGen_eval(pThis->pGen, pLoc, aBuf, sizeof(aBuf));
	if(nRet != 1) return false;

	/* The form packs, because it is the only thing that knows what the bits
	   mean.  A form with no pack slot has no single-datum representation,
	   which is an answer rather than a failure. */
	if((pThis->pForm != NULL)&&(pThis->pForm->pVTbl->pack != NULL)){
		das_operand op;
		if(!DasSet_operand(pThis, &op)) return false;
		if(pThis->pForm->pVTbl->pack(pThis->pForm, &op, aBuf, pOut))
			return true;
	}

	das_datum_init(
		pOut, aBuf, (das_val_type)DasGen_elemType(pThis->pGen), 0, pThis->units
	);
	return true;
}

static const char* _DasSetScalar_element(const DasSet* pThis)
{
	(void)pThis;
	return "scalar";
}

static das_elem_type _DasSetScalar_elemType(const DasSet* pThis)
{
	return DasGen_elemType(pThis->pGen);
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
		(pThis->pForm != NULL) ? " " : "",
		(pThis->pForm != NULL) ? DasForm_kindStr(pThis->pForm) : ""
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

static const DasSet_VTbl g_vtblSetScalar = {
	_DasSetScalar_get, _DasSetScalar_element,
	_DasSetScalar_elemType,
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

DasSet* new_DasSetScalar(DasGen* pGen, das_units units, DasForm* pForm)
{
	if(pGen == NULL){
		das_error(DASERR_SET, "Null generator for new_DasSetScalar");
		return NULL;
	}
	/* Checked before anything is allocated, so the error path leaks nothing.
	   NULL is not "no formalism" here -- an absent <ops> binds the explicit
	   linear form, and only a byte run's CLASS gets to say it has no math. */
	if(pForm == NULL){
		das_error(DASERR_SET,
			"A scalar needs a formalism; pass the linear form, not NULL");
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
	pThis->pVTbl    = &g_vtblSetScalar;
	pThis->units = units;
	pThis->nRef  = 1;

	pThis->pForm = pForm;   /* the reference is TAKEN, not cloned */

	pThis->pGen = pGen;
	DasGen_incRef(pGen);
	return pThis;
}


/* ************************************************************************* */
/* DasCompSet: the numeric component run, the <composite> element            */

static bool _DasCompSet_get(const DasSet* pBase, ptrdiff_t* pLoc, das_datum* pOut)
{
	/* the formalism interprets the run */
	if((pBase->pForm != NULL)&&(pBase->pForm->pVTbl->pack != NULL)){
		ubyte aRun[DATUM_BUF_SZ];
		int nGot = DasGen_eval(pBase->pGen, pLoc, aRun, sizeof(aRun));
		if(nGot < 1) return false;

		das_operand op;
		if(!DasSet_operand(pBase, &op)) return false;
		return pBase->pForm->pVTbl->pack(pBase->pForm, &op, aRun, pOut);
	}

	das_error(DASERR_NOTIMP,
		"No single-datum representation for a %s composite "
		"(components are readable through the generator)",
		(pBase->pForm != NULL) ? DasForm_kindStr(pBase->pForm) : "plain"
	);
	return false;
}

static const char* _DasCompSet_element(const DasSet* pThis)
{
	(void)pThis;
	return "composite";
}

static das_elem_type _DasSetIntr_elemType(const DasSet* pThis)
{
	return DasGen_elemType(pThis->pGen);
}

static int _DasSetIntr_shape(const DasSet* pThis, ptrdiff_t* pShape)
{
	return DasGen_extShape(pThis->pGen, pShape);
}

static int _DasCompSet_intrShape(const DasSet* pBase, ptrdiff_t* pShape)
{
	const DasCompSet* pThis = (const DasCompSet*)pBase;
	memcpy(pShape, pThis->aIntShape, sizeof(ptrdiff_t)*(size_t)pThis->nIntRank);
	return pThis->nIntRank;
}

static char* _DasCompSet_expression(
	const DasSet* pThis, char* sBuf, int nLen, unsigned int uFlags
){
	(void)uFlags;
	snprintf(sBuf, (size_t)nLen, "composite(%s%s%s)",
		pThis->units ? pThis->units : "",
		(pThis->pForm != NULL) ? " " : "",
		(pThis->pForm != NULL) ? DasForm_kindStr(pThis->pForm) : ""
	);
	return sBuf;
}

static bool _DasSet_isNumericTrue(const DasSet* pThis)
{
	(void)pThis;
	return true;
}

static DasSet* _DasCompSet_copy(const DasSet* pThis)
{
	DasCompSet* pOut = (DasCompSet*)calloc(1, sizeof(DasCompSet));
	*pOut = *((const DasCompSet*)pThis);
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

static const DasSet_VTbl g_vtblCompSet = {
	_DasCompSet_get, _DasCompSet_element,
	_DasSetIntr_elemType,
	_DasSetIntr_shape, _DasCompSet_intrShape,
	_DasCompSet_expression, _DasSet_isNumericTrue,
	DasSet_incRef, DasSet_decRef,
	_DasCompSet_copy
};

/* Shared by both internal-run constructors: one units set per set, and the
   holder (das_geovec) has no room for a per-component list. */
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

DasCompSet* new_DasCompSet(
	DasGen* pGen, das_units units, DasForm* pForm,
	int nIntRank, const ptrdiff_t* pIntShape
){
	if((pGen == NULL)||(pIntShape == NULL)||(pForm == NULL)||(nIntRank < 1)||
	   (nIntRank >= SETIDX_MAX)){
		das_error(DASERR_SET, "Invalid arguments to new_DasCompSet");
		return NULL;
	}

	das_elem_type et = DasGen_elemType(pGen);
	if((et == etUnknown)||(et == etTime)){
		das_error(DASERR_SET,
			"Composite components must be plain numeric, not element type %d",
			(int)et
		);
		return NULL;
	}

	if(!_intr_unitsOk(units)) return NULL;

	DasCompSet* pThis = (DasCompSet*)calloc(1, sizeof(DasCompSet));
	DasDesc_init(&(pThis->base.base), VARIABLE);
	pThis->base.pVTbl    = &g_vtblCompSet;
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
/* DasByteSet: the byte run, the <bytes> element                             */

static bool _DasByteSet_get(const DasSet* pBase, ptrdiff_t* pLoc, das_datum* pOut)
{
	const DasByteSet* pThis = (const DasByteSet*)pBase;

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

static const char* _DasByteSet_element(const DasSet* pThis)
{
	(void)pThis;
	return "bytes";
}

static int _DasByteSet_intrShape(const DasSet* pBase, ptrdiff_t* pShape)
{
	pShape[0] = ((const DasByteSet*)pBase)->nExtent;
	return 1;   /* a byte run's internal rank is always 1 */
}

static char* _DasByteSet_expression(
	const DasSet* pThis, char* sBuf, int nLen, unsigned int uFlags
){
	(void)uFlags;
	snprintf(sBuf, (size_t)nLen, "%s(%s)",
		((const DasByteSet*)pThis)->bSentinel ? "string" : "blob",
		pThis->units ? pThis->units : ""
	);
	return sBuf;
}

static bool _DasSet_isNumericFalse(const DasSet* pThis)
{
	(void)pThis;
	return false;
}

static DasSet* _DasByteSet_copy(const DasSet* pThis)
{
	DasByteSet* pOut = (DasByteSet*)calloc(1, sizeof(DasByteSet));
	*pOut = *((const DasByteSet*)pThis);
	memset(&(pOut->base.base), 0, sizeof(DasDesc));   /* see scalar copy */
	DasDesc_init(&(pOut->base.base), VARIABLE);
	DasDesc_copyIn(&(pOut->base.base), &(pThis->base));
	pOut->base.nRef = 1;
	if(pOut->base.pGen != NULL)
		pOut->base.pGen = DasGen_copy(pThis->pGen);
	return (DasSet*)pOut;
}

static const DasSet_VTbl g_vtblByteSet = {
	_DasByteSet_get, _DasByteSet_element,
	_DasSetIntr_elemType,
	_DasSetIntr_shape, _DasByteSet_intrShape,
	_DasByteSet_expression, _DasSet_isNumericFalse,
	DasSet_incRef, DasSet_decRef,
	_DasByteSet_copy
};

DasByteSet* new_DasByteSet(
	DasGen* pGen, das_units units, bool bSentinel, ptrdiff_t nExtent
){
	if(pGen == NULL){
		das_error(DASERR_SET, "Invalid arguments to new_DasByteSet");
		return NULL;
	}

	das_elem_type et = DasGen_elemType(pGen);
	if(et != etUByte){
		das_error(DASERR_SET,
			"A byte-run set needs an etUByte source, not element type %d", (int)et
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

	DasByteSet* pThis = (DasByteSet*)calloc(1, sizeof(DasByteSet));
	DasDesc_init(&(pThis->base.base), VARIABLE);
	pThis->base.pVTbl    = &g_vtblByteSet;
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


/* ************************************************************************* */
/* Operation sets: the registry made runnable                                */

/* A DasBinSet has no wire element, so it serializes by MATERIALIZING: evaluate
   the whole thing into an array and let the ordinary array path emit it.  The
   full-range subset already does exactly that walk, fill padding and all. */
DasAry* DasSet_materialize(const DasSet* pThis)
{
	ptrdiff_t aShape[SETIDX_MAX];
	int nRank = DasSet_shape(pThis, aShape);
	if(nRank < 1){
		das_error(DASERR_SET, "Can't materialize a rank 0 set");
		return NULL;
	}

	ptrdiff_t aMin[SETIDX_MAX], aMax[SETIDX_MAX];
	for(int i = 0; i < nRank; ++i){
		aMin[i] = 0;
		if(aShape[i] < 0){
			das_error(DASERR_NOTIMP,
				"Can't materialize index %d of a set with no settled extent", i
			);
			return NULL;
		}
		aMax[i] = aShape[i];
	}

	/* Copy, never a view: the point is to own numbers that outlive the recipe */
	return DasSet_subsetCopy(pThis, nRank, aMin, aMax);
}


static const DasSet_VTbl g_vtblBinSet;   /* defined with the other vtables */

DasBinSet* new_DasBinSet(DasSet* pLeft, char cOp, DasSet* pRight)
{
	if((pLeft == NULL)||(pRight == NULL)){
		das_error(DASERR_SET, "Null operand for a binary operation");
		return NULL;
	}

	int op;
	switch(cOp){
	case '+': op = D2BOP_ADD; break;
	case '-': op = D2BOP_SUB; break;
	case '*': op = D2BOP_MUL; break;
	default:
		das_error(DASERR_SET, "Unknown operator '%c'", cOp);
		return NULL;
	}

	DasForm*         pFormOut = NULL;
	das_units       unitsOut = NULL;
	double          rRightScale = 1.0;
	das_elem_type   etOut = etUnknown;
	das_gen_applyfn pApply = NULL;

	/* Python's __add__ / __radd__.  Ask the left operand, then the right.  Both
	   hooks decline SILENTLY, so a fallback costs no diagnostic noise; only a
	   double decline is an error.  Neither hook ever reorders the operands, so
	   an undefined pairing fails loud instead of silently commuting. */
	bool bOk = DasForm_binOpLeft(
		pLeft->pForm, pLeft, op, pRight->pForm, pRight,
		&pFormOut, &unitsOut, &rRightScale, &etOut, &pApply
	);
	if(!bOk)
		bOk = DasForm_binOpRight(
			pRight->pForm, pRight, op, pLeft->pForm, pLeft,
			&pFormOut, &unitsOut, &rRightScale, &etOut, &pApply
		);

	if(!bOk){
		das_error(DASERR_SET,
			"No rule for %s %c %s: neither operand defines the pairing",
			DasForm_kindStr(pLeft->pForm), cOp, DasForm_kindStr(pRight->pForm)
		);
		return NULL;
	}

	DasGen* pGen = new_DasGenBinop(
		pApply, etOut, pLeft->pGen, pRight->pGen, rRightScale
	);
	if(pGen == NULL){ DasForm_decRef(pFormOut); return NULL; }

	DasBinSet* pThis = (DasBinSet*)calloc(1, sizeof(DasBinSet));
	DasDesc_init(&(pThis->base.base), VARIABLE);
	pThis->base.pVTbl    = &g_vtblBinSet;
	pThis->base.units = unitsOut;
	pThis->base.pForm  = pFormOut;    /* the resolved result, ownership taken */
	pThis->base.nRef  = 1;
	pThis->base.pGen  = pGen;       /* new_DasGenBinop left us the reference */

	pThis->op     = op;
	pThis->pLeft  = pLeft;
	pThis->pRight = pRight;
	DasSet_incRef(pLeft);
	DasSet_incRef(pRight);

	return pThis;
}
