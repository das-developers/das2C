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
 * This file holds NO formalism rows.  `ls das2/form_*.c` is the inventory,
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

int DasForm_incRef(DasForm* pThis){ return ++(pThis->nRef); }

int DasForm_decRef(DasForm* pThis)
{
	if(pThis == NULL) return 0;
	if(--(pThis->nRef) > 0) return pThis->nRef;
	pThis->pVTbl->release(pThis);
	return 0;
}

DasForm* DasForm_copy(const DasForm* pThis)
{
	return pThis->pVTbl->copy(pThis);
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

/* pForm is NEVER NULL for a set that carries math, so "I do not know this
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

extern const DasForm_VTbl das_form_generic_vtbl;

static DasForm* _gen_new(void)
{
	DasFormGeneric* pThis = (DasFormGeneric*)calloc(1, sizeof(DasFormGeneric));
	pThis->base.pVTbl = &das_form_generic_vtbl;
	pThis->base.nRef  = 1;
	return &(pThis->base);
}

static DasErrCode _gen_setParam(
	DasForm* pBase, DasCtxTbl* pTbl, const char* sName, const char* sVal
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

static DasErrCode _gen_encode(
	const DasForm* pBase, const DasCtxTbl* pTbl, DasBuf* pBuf
){
	const DasFormGeneric* pThis = (const DasFormGeneric*)pBase;

	DasErrCode nRet = DasBuf_printf(pBuf, "<ops kind=\"%s\"", pThis->sKind);
	if(nRet != DAS_OKAY) return nRet;

	for(int i = 0; i < pThis->nParams; ++i){
		nRet = DasBuf_printf(pBuf, " %s=\"%s\"",
			pThis->aParam[i].sName, pThis->aParam[i].sVal);
		if(nRet != DAS_OKAY) return nRet;
	}
	return DasBuf_puts(pBuf, "/>\n");
}

/* A generic form cannot resolve references it does not understand, so it holds
   none.  A context entry a generic <ops> mentions is kept as a STRING, which
   means a context pruner must not treat "unreferenced" as "unused" while any
   generic form is present. */
static int _gen_getRefs(const DasForm* pBase, ubyte* pIds, int nMax){ return 0; }

static bool _gen_pack(
	const DasForm* pBase, const das_operand* pOp, const ubyte* pRun,
	das_datum* pOut
){
	/* No pack: the C library does not know what these numbers mean, so it
	   hands them out as plain elements through subset, not as a rich datum. */
	return false;
}

static das_val_type _gen_datumType(const DasForm* pBase){ return vtUnknown; }

static char* _gen_prnIntr(
	const DasForm* pBase, const DasCtxTbl* pTbl, char* sBuf, int nLen
){
	snprintf(sBuf, (size_t)nLen, " %s(unknown)",
	         ((const DasFormGeneric*)pBase)->sKind);
	return sBuf;
}

static DasForm* _gen_copy(const DasForm* pBase)
{
	DasFormGeneric* pCopy = (DasFormGeneric*)calloc(1, sizeof(DasFormGeneric));
	memcpy(pCopy, pBase, sizeof(DasFormGeneric));
	pCopy->base.nRef = 1;
	return &(pCopy->base);
}

static void _gen_release(DasForm* pBase){ free(pBase); }

const DasForm_VTbl das_form_generic_vtbl = {
	"",                /* no wire kind of its own; sKind holds what arrived */
	_gen_new,
	_gen_setParam,
	_gen_encode,
	_gen_getRefs,
	_gen_pack,
	_gen_datumType,
	_gen_prnIntr,
	NULL,              /* binOpLeft  -- unknown math is refused by having no */
	NULL,              /* binOpRight    hook at all, not by a check */
	_gen_copy,
	_gen_release
};

bool DasForm_isGeneric(const DasForm* pThis)
{
	return (pThis != NULL)&&(pThis->pVTbl == &das_form_generic_vtbl);
}

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
extern const DasForm_VTbl das_form_geovec_vtbl;   /* form_geovec.c */
extern const DasForm_VTbl das_form_rotate_vtbl;   /* form_rot.c    */

static const DasForm_VTbl* g_kindTable[] = {
	&das_form_linear_vtbl,
	&das_form_point_vtbl,
	&das_form_geovec_vtbl,
	&das_form_rotate_vtbl,
	NULL
};

const DasForm_VTbl* das_form_lookup(const char* sKind)
{
	if((sKind == NULL)||(sKind[0] == '\0')) return &das_form_linear_vtbl;

	for(int i = 0; g_kindTable[i] != NULL; ++i){
		if(strcmp(g_kindTable[i]->sKind, sKind) == 0)
			return g_kindTable[i];
	}
	return NULL;
}

DasForm* das_form_fromStr(const char* sKind)
{
	/* An absent <ops> means ordinary numbers, not "no rules".  Binding linear
	   explicitly is what keeps every operation on one dispatch path with no
	   bypass for "plain" math. */
	const DasForm_VTbl* pVTbl = das_form_lookup(sKind);

	if(pVTbl == NULL)
		return _gen_newFor(sKind);   /* unknown kind, never an error */

	return pVTbl->create();
}
