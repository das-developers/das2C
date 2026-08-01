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
#include "var_priv.h"


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

bool DasVar_itemAt(
	const DasVar* pThis, ptrdiff_t* pLoc, ubyte* pBuf, size_t uBufLen
){
	if((pThis == NULL)||(pBuf == NULL))
		return das_error_false(DASERR_VAR, "Null pointer reading an item run");

	/* An array backed variable hands back its own storage, so the copy is the
	   only work.  A computed one has to be evaluated into the buffer, which is
	   what the generator's eval does. */
	size_t uCount = 0;
	const ubyte* pRun = (pThis->pGen == NULL)
		? NULL : DasGen_at(pThis->pGen, pLoc, &uCount);

	if(pRun != NULL){
		size_t uBytes = uCount * das_vt_size((das_val_type)DasVar_elemType(pThis));
		if(uBytes > uBufLen)
			return das_error_false(DASERR_VAR,
				"An item run of %zu bytes will not fit a %zu byte buffer",
				uBytes, uBufLen
			);
		memcpy(pBuf, pRun, uBytes);
		return true;
	}

	if(pThis->pGen != NULL)
		return (DasGen_eval(pThis->pGen, pLoc, pBuf, uBufLen) >= 1);

	/* No generator at all is a binary operation: it has no leaf values, it
	   combines its two operands through the same call. */
	return DasVarBin_itemAt(pThis, pLoc, pBuf, uBufLen);
}

const char* DasVar_element(const DasVar* pThis)
{
	return pThis->pVTbl->element(pThis);
}

das_elem_type DasVar_elemType(const DasVar* pThis)
{
	if(pThis == NULL) return etUnknown;
	return pThis->pVTbl->elemType(pThis);
}

DasAry* DasVar_getArray(const DasVar* pThis)
{
	/* A DasVarBin carries no generator at all, so the NULL test is load
	   bearing and not defensive noise. */
	if((pThis == NULL)||(pThis->pGen == NULL)) return NULL;
	return DasGen_getArray(pThis->pGen);
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

/* ************************************************************************* */
/* Serialization                                                             */

static char* _var_seqValToStr(
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

DasErrCode DasVar_encode(DasVar* pThis, const char* sRole, DasBuf* pBuf)
{
	const DasDim* pDim = (const DasDim*) ((DasDesc*)pThis)->parent;
	const DasDs*  pDs  = (const DasDs*) ((DasDesc*)pDim)->parent;

	das_elem_type et = DasGen_elemType(pThis->pGen);
	das_gen_type  gt = DasGen_type(pThis->pGen);
	das_units units  = (pThis->units != NULL) ? pThis->units : UNIT_DIMENSIONLESS;

	/* Structure comes from the class, so the element name IS the classification;
	   nothing here derives structure back out of a presentation vocabulary. */
	const char* sElement = DasVar_element(pThis);
	bool bScalar  = (strcmp(sElement, "scalar") == 0);
	bool bByteRun = (strcmp(sElement, "bytes")  == 0);

	/* 1. index= from the generator's own declared shape.  A sequence reports
	   the extents it DECLARED, borrow marks included; that is the point of
	   asking the generator rather than the merged dataset shape. */
	ptrdiff_t aExtShape[VARIDX_MAX] = VARIDX_INIT_UNUSED;
	int nExtRank = DasGen_extShape(pThis->pGen, aExtShape);

	/* An array-backed var writes the record index as ragged whatever extent it
	   declared, since storage grows as records arrive.  That override applies
	   only to a real extent: '-' and '^' are statements about the container and
	   outrank it.  Print from a COPY, because the declared shape is still needed
	   below (a header-values var is the one with VARIDX_UNUSED at index 0). */
	ptrdiff_t aPrnShape[VARIDX_MAX];
	memcpy(aPrnShape, aExtShape, sizeof(aPrnShape));
	if((gt == gtArray)&&(aPrnShape[0] != VARIDX_UNUSED)&&
	   (aPrnShape[0] != VARIDX_BORROW))
		aPrnShape[0] = VARIDX_RAGGED;

	char sIndex[128] = {'\0'};
	if(das_shape_toStr(aPrnShape, nExtRank, sIndex, sizeof(sIndex)) < 0)
		return DASERR_VAR;

	/* Items per record and whether any of them are ragged; index 0 is the record
	   index and never counts toward either. */
	int nItems = 1;
	bool bRaggedItems = false;
	for(int i = 1; i < nExtRank; ++i){
		if(aExtShape[i] == VARIDX_RAGGED)  bRaggedItems = true;
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
		bool bHdrVals = (aExtShape[0] == VARIDX_UNUSED);
		bool bText = (pCkCodec != NULL)&&(pCkCodec->vtBuf == vtText);
		if((bHdrVals || bText) && !bByteRun &&
		   (vtAry != vtText) && (vtAry != vtByteSeq)){
			DasBuf_printf(pBuf, " storage=\"%s\"",
				(vtAry == vtTime) ? "struct" : das_vt_toStr(vtAry)
			);
		}
	}

	if((!bScalar)&&(!bByteRun)){
		const DasVarComp* pComp = (const DasVarComp*)pThis;
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

	/* The form writes its own <ops>, parameters and all.  Linear writes
	   nothing, which is how "no ops element" gets said.  A byte run has no
	   formalism at all. */
	if(pThis->pForm != NULL){
		int nRet = pThis->pForm->pVTbl->encode(pThis->pForm, pBuf);
		if(nRet != DAS_OKAY) return nRet;
	}

	/* 4. the generator child */
	if(gt == gtSeq){
		const DasGenSeq* pSeq = (const DasGenSeq*)pThis->pGen;
		size_t uElem  = das_vt_size((das_val_type)et);
		size_t uSlope = (et == etTime) ? sizeof(double) : uElem;
		das_elem_type etSlope = (et == etTime) ? etDouble : et;

		for(int c = 0; c < pSeq->nComps; ++c){
			char sMin[64] = {'\0'};
			_var_seqValToStr(pSeq->aIntercept[c], et, sMin, sizeof(sMin));

			/* full-rank interval, '-'-aligned to index= */
			char sInterval[256] = {'\0'};
			char* pIv = sInterval;
			for(int i = 0; i < nExtRank; ++i){
				if(i > 0){ *pIv = ';'; ++pIv; }
				if(aExtShape[i] == VARIDX_UNUSED){ *pIv = '-'; ++pIv; }
				else{
					char sM[64] = {'\0'};
					_var_seqValToStr(pSeq->aInterval[c][i], etSlope, sM, sizeof(sM));
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
		if(bByteRun)   /* the sentinel is what tells a string from a blob */
			sSemC = ((const DasVarBytes*)pThis)->bSentinel ? DAS_SEM_TEXT
			                                             : DAS_SEM_BLOB;

		DasCodec codecHdr;
		if(pCodec == NULL){
			/* header values: no packet codec exists, make a transient writer */
			if(aExtShape[0] != VARIDX_UNUSED){
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

			char sIdxTerm[12 + (VARIDX_MAX - 1)*3] = {'\0'};
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
				const DasVarComp* pComp = (const DasVarComp*)pThis;
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
	if((pBase->pForm == NULL)||(pBase->pForm->pVTbl->pack == NULL)){
		das_error(DASERR_NOTIMP,
			"No single-datum representation for a %s composite "
			"(components are readable through the generator)",
			(pBase->pForm != NULL) ? DasForm_kindStr(pBase->pForm) : "plain"
		);
		return false;
	}

	/* A composite datum is a view, so the run has to be memory that outlives
	   this call.  DasGen_at hands back the array's own storage and answers
	   NULL for a computed source, which is the whole of the question. */
	size_t uCount = 0;
	const ubyte* pRun = (pBase->pGen == NULL)
		? NULL : DasGen_at(pBase->pGen, pLoc, &uCount);

	if(pRun == NULL){
		das_error(DASERR_NOTIMP,
			"A computed %s composite has no run to view.  Only an array backed "
			"composite can be read as a single datum today",
			DasForm_kindStr(pBase->pForm)
		);
		return false;
	}

	das_operand op;
	if(!DasVar_operand(pBase, &op)) return false;

	/* pack() sizes the run from the declared extent, so a short run would be
	   read off its end.  Nothing checks declared against actual at build time
	   yet, which is why the read checks. */
	size_t uWant = 1;
	for(int i = 0; i < op.nIntRank; ++i){
		if(op.aIntShape[i] < 1){ uWant = 0; break; }  /* ragged, no fixed want */
		uWant *= (size_t)op.aIntShape[i];
	}
	if((uWant > 0)&&(uCount < uWant)){
		das_error(DASERR_VAR,
			"A %s composite declares %zu elements per item but the array holds "
			"%zu at this location", DasForm_kindStr(pBase->pForm), uWant, uCount
		);
		return false;
	}

	return pBase->pForm->pVTbl->pack(pBase->pForm, &op, pRun, pOut);
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

