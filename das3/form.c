/* Copyright (C) 2026 Chris Piker <chris-piker@uiowa.edu>
 *
 * Author: C. Piker, via Claude Opus 5
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

/* The DasForm base: refcounting, the kind table, and the generic form.
 *
 * This file holds NO formalism rows.  `ls das3/form_*.c` is the inventory,
 * and the only thing a new formalism adds here is one line in g_kindTable.
 * If that ever grows, the layer has failed.
 */

#define _POSIX_C_SOURCE 200112L

#include <string.h>
#include <stdio.h>
#include <stdlib.h>

#include "util.h"
#include "value.h"
#include "units.h"
#include "buffer.h"
#include "form.h"

/* ************************************************************************* */
/* The base                                                                  */

/* NULL tolerant for use by generic DasVar destructors */
void del_DasForm(DasForm* pThis)
{
	if(pThis == NULL) return;
	pThis->pVTbl->release(pThis);
}

/* NULL tolerant for the same reason del_DasForm is. */
DasForm* DasForm_copy(const DasForm* pThis)
{
	if(pThis == NULL) return NULL;
	return pThis->pVTbl->copy(pThis);
}

/* A recipe IS reference counted, unlike the form above it.  DasVar_copy()
   hands the copy the original's recipe rather than re-resolving it, because
   resolution is a construction-time decision and two copies of one variable
   must not disagree about their on-demand value creation rules. */
int DasBinOp_incRef(DasBinOp* pThis){ return ++(pThis->nRef); }

int DasBinOp_decRef(DasBinOp* pThis)
{
	if(pThis == NULL) return 0;
	if(--(pThis->nRef) > 0) return pThis->nRef;

	/* The pairing's own release frees whatever that rule allocated AND drops
	   the result formalism it owns; see any form_*.c's _binop_release. */
	pThis->pVTbl->release(pThis);
	return 0;
}

char* DasForm_prnRun(
	const DasForm* pThis, const ubyte* pRun, uint32_t nElems,
	das_val_type et, char* sBuf, int nLen
){
	if((pThis == NULL)||(pThis->pVTbl->prnRun == NULL)) return NULL;
	return pThis->pVTbl->prnRun(pThis, pRun, nElems, et, sBuf, nLen);
}

const char* DasForm_compSym(const DasForm* pThis, int iComp)
{
	/* NULL tolerant twice over: a byte run carries no formalism, and a kind
	   that has no symbols leaves the slot empty rather than inventing one. */
	if((pThis == NULL)||(pThis->pVTbl->compSym == NULL)) return NULL;
	if(iComp < 0) return NULL;

	return pThis->pVTbl->compSym(pThis, iComp);
}

const char* DasForm_getParam(
	const DasForm* pThis, const char* sName, ubyte* pType
){
	if(pType != NULL) *pType = 0;   /* 0 is not a valid DASPROP_ type */
	/* Silent on a miss, unlike setParam's duty to refuse an unknown name.
	   Asking whether a form HAS a parameter is a legitimate question -- it is
	   how a caller finds out whether this formalism carries a frame at all --
	   so "no" is an answer here, not an error. */
	if((pThis == NULL)||(sName == NULL)) return NULL;
	if(pThis->pVTbl->getParam == NULL)   return NULL;

	return pThis->pVTbl->getParam(pThis, sName, pType);
}

size_t das_operand_elems(const das_operand* pThis)
{
	size_t uElems = 1;
	for(int i = 0; i < pThis->nIntRank; ++i){
		if(pThis->aIntShape[i] < 1) return 0;   /* ragged, no fixed width */
		uElems *= (size_t)pThis->aIntShape[i];
	}
	return uElems;
}

/* ************************************************************************* */
/* DasFormGeneric: an unrecognized kind=                                     */

/* pForm is NEVER NULL for a variable that carries math, so "I do not know this
   formalism" needs a vtable to say no from rather than a null check at every
   call site.  A generic form keeps its token and parameters verbatim so the
   stream round-trips, and refuses every pairing.

   The law that keeps unknown math honest: no rule is ever registered against
   THIS vtable, and every other form recognizes partners by vtable address, so
   a generic operand misses by construction rather than by a check somebody
   has to remember to write. */

#define DASFORM_MAX_PARAMS 10

typedef struct das_form_generic {
	DasForm base;

	char sKind[32];
	int  nParams;
	struct { char sName[16]; char sVal[48]; } aParam[DASFORM_MAX_PARAMS];
} DasFormGeneric;

/* No forward declaration here: form.h publishes this one, since DAS_FORM_EXT
   needs its address and a second decl without DAS_API would disagree about
   linkage on Windows. */

static DasForm* _gen_new(void)
{
	DasFormGeneric* pThis = (DasFormGeneric*)calloc(1, sizeof(DasFormGeneric));
	pThis->base.pVTbl = &das_form_generic_vtbl;
	return &(pThis->base);
}

static DasErrCode _gen_setParam(
	DasForm* pBase, const char* sName, const char* sVal
){
	DasFormGeneric* pThis = (DasFormGeneric*)pBase;

	/* A generic form accepts ANY parameter, which is the opposite of a known
	   form's duty to refuse a misspelling.  It does not understand the kind,
	   so it cannot know which names are legal; keeping them all is the only
	   way to write back what arrived. */
	if(pThis->nParams >= DASFORM_MAX_PARAMS)
		return das_error(DASERR_FORM,
			"<ops kind=\"%s\"> has more than %d parameters",
			pThis->sKind, DASFORM_MAX_PARAMS
		);
	if(strlen(sName) >= sizeof(pThis->aParam[0].sName))
		return das_error(DASERR_FORM, "Parameter name '%s' too long", sName);
	if(strlen(sVal) >= sizeof(pThis->aParam[0].sVal))
		return das_error(DASERR_FORM, "Parameter value '%s' too long", sVal);

	strncpy(pThis->aParam[pThis->nParams].sName, sName,
	        sizeof(pThis->aParam[0].sName) - 1);
	strncpy(pThis->aParam[pThis->nParams].sVal, sVal,
	        sizeof(pThis->aParam[0].sVal) - 1);
	++(pThis->nParams);
	return DAS_OKAY;
}

static DasErrCode _gen_encode(const DasForm* pBase, DasBuf* pBuf)
{
	const DasFormGeneric* pThis = (const DasFormGeneric*)pBase;

	DasErrCode nRet = DasBuf_printf(pBuf, "      <ops kind=\"%s\"", pThis->sKind);
	if(nRet != DAS_OKAY) return nRet;

	for(int i = 0; i < pThis->nParams; ++i){
		nRet = DasBuf_printf(pBuf, " %s=\"%s\"",
			pThis->aParam[i].sName, pThis->aParam[i].sVal);
		if(nRet != DAS_OKAY) return nRet;
	}
	return DasBuf_puts(pBuf, "/>\n");
}

/* The generic form is the ONLY one that stores parameters as the strings they
   arrived as, so its getParam is a plain lookup with nothing to reconstruct.
   Every other form decodes into typed fields and renders back on demand. */
static const char* _gen_getParam(
	const DasForm* pBase, const char* sName, ubyte* pType
){
	const DasFormGeneric* pThis = (const DasFormGeneric*)pBase;

	for(int i = 0; i < pThis->nParams; ++i){
		if(strcmp(pThis->aParam[i].sName, sName) != 0) continue;

		/* STRING even when the text looks like a number.  This form did not
		   decode the parameter, so it is not the authority on its type and
		   must not guess one; the kind it belongs to is the authority and
		   this reader has never heard of that kind. */
		if(pType != NULL) *pType = DASPROP_STRING | DASPROP_SINGLE;
		return pThis->aParam[i].sVal;
	}
	return NULL;
}

static bool _gen_pack(
	const DasForm* pBase, const das_operand* pOp, const ubyte* pRun,
	das_datum* pOut
){
	/* No pack: the C library does not know what these numbers mean, so it
	   hands them out as plain elements through subset, not as a rich datum. */
	return false;
}

static das_val_type _gen_datumType(const DasForm* pBase){ return vtUnknown; }

static char* _gen_prnIntr(const DasForm* pBase, char* sBuf, int nLen)
{
	snprintf(sBuf, (size_t)nLen, " %s(unknown)",
	         ((const DasFormGeneric*)pBase)->sKind);
	return sBuf;
}

static DasForm* _gen_copy(const DasForm* pBase)
{
	DasFormGeneric* pCopy = (DasFormGeneric*)calloc(1, sizeof(DasFormGeneric));
	memcpy(pCopy, pBase, sizeof(DasFormGeneric));
	return &(pCopy->base);
}

static void _gen_release(DasForm* pBase){ free(pBase); }

const DasForm_VTbl das_form_generic_vtbl = {
	"",                /* no wire kind of its own; sKind holds what arrived */
	_gen_new,
	_gen_setParam,
	NULL,              /* validate -- nothing is required of this kind */
	_gen_getParam,
	_gen_encode,
	_gen_pack,
	_gen_datumType,
	_gen_prnIntr,
	NULL,              /* prnRun -- an unknown kind has no rendering to offer */
	NULL,              /* compSym -- nor any idea what its components mean */
	NULL,              /* binOpLeft  -- unknown math is refused by having no */
	NULL,              /* binOpRight    hook at all, not by a check */
	_gen_copy,
	_gen_release
};

/* The generic form is the only one whose kind token is data rather than a
   vtable constant, so it needs a setter the table can call. */
static DasForm* _gen_newFor(const char* sKind)
{
	DasFormGeneric* pThis = (DasFormGeneric*)_gen_new();
	if(strlen(sKind) >= sizeof(pThis->sKind)){
		das_error(DASERR_FORM, "Formalism kind '%s' too long", sKind);
		free(pThis);
		return NULL;
	}
	strncpy(pThis->sKind, sKind, sizeof(pThis->sKind) - 1);
	return &(pThis->base);
}

/* ************************************************************************* */
/* The kind table.  ONE line per formalism, and that line is the entire       */
/* footprint of a new formalism outside its own file.                        */

extern const DasForm_VTbl das_form_linear_vtbl;   /* form_linear.c */
extern const DasForm_VTbl das_form_point_vtbl;    /* form_point.c  */
extern const DasForm_VTbl das_form_cplx_vtbl;     /* form_cplx.c   */
extern const DasForm_VTbl das_form_vector_vtbl;   /* form_vector.c */
extern const DasForm_VTbl das_form_geoloc_vtbl;   /* form_geoloc.c */
extern const DasForm_VTbl das_form_rotate_vtbl;   /* form_rot.c    */

static const DasForm_VTbl* g_kindTable[] = {
	&das_form_linear_vtbl,
	&das_form_point_vtbl,
	&das_form_cplx_vtbl,
	&das_form_vector_vtbl,
	&das_form_geoloc_vtbl,
	&das_form_rotate_vtbl,
	NULL
};

/* A pure lookup: it answers what a token names and nothing else.  "Absence
   means linear" is resolved once, at the wire boundary in new_DasForm_pairs. */
const DasForm_VTbl* das_form_lookup(const char* sKind)
{
	if((sKind == NULL)||(sKind[0] == '\0')) return NULL;

	for(int i = 0; g_kindTable[i] != NULL; ++i){
		if(strcmp(g_kindTable[i]->sKind, sKind) == 0)
			return g_kindTable[i];
	}
	return NULL;
}

/* Instantiate a kind.  The registry hands back a vtable and create() turns it
   into an object without this function knowing the concrete type.  The generic
   is the one branch that cannot be table driven: it needs the token it is
   standing in for, and create() takes no arguments. */
static DasForm* _form_create(const char* sKind)
{
	const DasForm_VTbl* pVTbl = das_form_lookup(sKind);

	if(pVTbl == NULL)
		return _gen_newFor(sKind);   /* unknown kind, never an error */

	return pVTbl->create();
}

DasForm* new_DasForm_pairs(const char** psAttr)
{
	if(psAttr == NULL){
		das_error(DASERR_FORM, "No attributes for an <ops> element");
		return NULL;
	}

	/* Pass one: find the kind.  Reading the array twice is fine and is what
	   new_PlaneDesc_pairs() has always done; the class has to be settled
	   before any parameter can be interpreted, and XML attribute order is not
	   significant, so kind= may legally arrive last. */
	const char* sKind = NULL;
	for(int i = 0; psAttr[i] != NULL; i += 2){
		if(strcmp(psAttr[i], "kind") == 0){ sKind = psAttr[i+1]; break; }
	}

	if((sKind == NULL)||(sKind[0] == '\0')){
		das_error(DASERR_FORM,
			"<ops> is missing its kind attribute; an operations element that "
			"does not say what kind of math it describes has no meaning"
		);
		return NULL;
	}

	DasForm* pThis = _form_create(sKind);
	if(pThis == NULL) return NULL;   /* it already said why */

	/* Pass two: everything that is not kind= is a parameter, and the class
	   decides what it means.  A refusal here is FATAL: a reader that claims a
	   kind must not skip a misspelling in it. */
	for(int i = 0; psAttr[i] != NULL; i += 2){
		if(strcmp(psAttr[i], "kind") == 0) continue;

		if(pThis->pVTbl->setParam(pThis, psAttr[i], psAttr[i+1]) != DAS_OKAY){
			del_DasForm(pThis);
			return NULL;
		}
	}

	/* NOT validated here.  Whether this form suits the variable it is going on
	   needs that variable's internal shape, which does not exist yet; the
	   constructors call DasForm_validate() once they have both. */
	return pThis;
}

DasErrCode DasForm_validate(
	DasForm* pThis, int nIntRank, const ptrdiff_t* pIntShape
){
	if(pThis == NULL) return DAS_OKAY;
	if(pThis->pVTbl->validate == NULL) return DAS_OKAY;

	return pThis->pVTbl->validate(pThis, nIntRank, pIntShape);
}

/* Write sysorder= only when it carries information.
 *
 * Two rules, and the second is the one that was getting this wrong.  Ascending
 * order is the default, so spelling it out is noise.  And the number of slots
 * is the VARIABLE's business (intern=), not the form's -- printing three for a
 * two component vector tells the reader it has a third.  uComps is what
 * validate() saw; 0 means the form was never validated, so there is no count
 * to trust and nothing is written. */
DasErrCode _das_form_prnOrder(
	DasBuf* pBuf, const ubyte* pOrder, ubyte uComps
){
	if((pOrder == NULL)||(uComps < 1)) return DAS_OKAY;

	const ubyte* aDir = pOrder;

	bool bCanon = true;
	for(ubyte u = 0; u < uComps; ++u)
		if(aDir[u] != u){ bCanon = false; break; }

	if(bCanon) return DAS_OKAY;

	DasErrCode nRet = DasBuf_puts(pBuf, " sysorder=\"");
	for(ubyte u = 0; (u < uComps)&&(nRet == DAS_OKAY); ++u)
		nRet = DasBuf_printf(pBuf, "%s%hhu", (u > 0) ? ";" : "", aDir[u]);

	if(nRet != DAS_OKAY) return nRet;
	return DasBuf_puts(pBuf, "\"");
}
