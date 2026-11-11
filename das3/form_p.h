/* Copyright (C) 2026 Chris Piker <chris-piker@uiowa.edu>
 *
 * This file is part of das2C, the Core Das C Library.
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

/** @file form_p.h INTERNAL header, subject to change without notice.
 *
 * Nothing here is API, and it is not installed.  The formalism base, its
 * virtual table, the operand snapshot and the binary operation recipes, for
 * form.c, the form_*.c kinds, and the variable layer that drives them.
 *
 * Forms compute, variables walk
 * -----------------------------
 * A form is handed values and hands back values.  It never fetches.  It has
 * no generator, no array, no index and no loop, so there is exactly one piece
 * of code that knows how to walk external indices and it lives at the variable
 * layer.  Everything else follows from this:
 *
 *   - binop apply() takes *one* item run from each side and writes *one* out
 *   - a form gets a das_operand, a snapshot of facts, never a DasVar
 *   - no form_*.c includes var.h; form.h needs only value, units, datum
 *
 * When non-local math arrives (interpolation, convolution, a point spread
 * response) the answer is *not* to hand forms a generator.  The recipe declares
 * a stencil, the variable gathers that window and passes it in, and the
 * walker is still the only thing that walks.  A form says what it needs;
 * it never goes and gets it.
 *
 * Knowledge flows one way
 * -----------------------
 * A form may know a peer form.  form_rot.c includes form_vec.h because a
 * rotation is defined as a thing that acts on vectors, and so rotations have
 * to know about vectors in order to provide useful operations.  The arrow
 * never reverses: form_vec.c need not know what a rotation is, and including
 * form_rot.h there would make the graph cyclic.  That asymmetry is what
 * binOpLeft and binOpRight are *for*.  They are not a fallback mechanism;
 * they are how the better-informed partner claims a pairing no matter which
 * side it is standing on.
 *
 * Generic library code (var.c, datset.c) reaches every kind through the
 * vtable and includes no kind header.  If it ever needs one, the layering
 * sprung a leak.
 *
 * Adding a kind
 * -------------
 * One file, form_<kind>.c, plus its public header and one line in form.c's
 * kind table.  The vtable is exported because its address is the kind token
 * DasForm_isKind() compares; recipe vtables stay static since nobody tags
 * with them.
 */

#ifndef _das_form_p_h_
#define _das_form_p_h_

#include <das3/form.h>

#ifdef __cplusplus
extern "C" {
#endif

struct das_buffer;

/* ========================================================================= *
 * das_operand: what a variable tells a form.
 * ========================================================================= */

/* The form layer's currency: a snapshot of the facts a formalism may need.

   The variable fills one of these from live facts at the moment of the call,
   which is why a form never reaches up and why nothing here can go stale
   behind a DasVar_setAry() that re-tags an element type.

   These four are the complete list.  Anything else a form wants is either its
   own state (a rotation's two frames) or something it must ask a PEER form
   for through that form's header. */
typedef struct das_operand {

	/* The operand's formalism, carried so a form can recognize a partner by
	   vtable address.  Peer to peer, never upward. */
	const DasForm* pForm;

	das_val_type   vtElem;       /* what one element of this operand holds */
	das_units      units;

	/* The internal shape as DECLARED.  Rank matters and the product does not:
	   a 3;3 rotation and a nine component vector share an element count and
	   are not the same thing. */
	int            nIntRank;              /* 0 for a scalar, aIntShape untouched */
	ptrdiff_t      aIntShape[VARIDX_MAX];

} das_operand;

/* Total elements in one item run, the product of the internal extents.
   Returns 0 if any level is ragged (no fixed width). */
size_t das_operand_elems(const das_operand* pThis);


/* ========================================================================= *
 * DasBinOp: the resolved recipe for one pairing.
 *
 * Handed back by binOpLeft/binOpRight once, at DasVarBin construction.  The
 * walker reads six facts off it and then calls apply() per item forever; it
 * never asks again what math made a value.
 *
 * Derived types carry whatever that pairing needs.  This is where a unit
 * conversion factor lives for linear, and a component permutation for
 * rotation -- private to the pairing that needs it, not a field every
 * formalism pays for.
 * ========================================================================= */

typedef struct das_binop DasBinOp;

typedef struct DasBinOp_VTbl {

	/* One item run from each side in, one item run out.  Item-at-a-time is
	   the whole contract, see "Forms compute, variables walk" above. */
	bool (*apply)(
		const DasBinOp* pThis, const ubyte* pLRun, const ubyte* pRRun,
		ubyte* pOutRun
	);

	void (*release)(DasBinOp* pThis);   /* derived storage, then free */

} DasBinOp_VTbl;

struct das_binop {

	const DasBinOp_VTbl* pVTbl;

	/* Everything the walker needs, settled once by the resolving form. */
	DasForm*     pForm;                    /* result formalism, owned here */
	das_units    units;
	das_val_type vtOut;
	int          nIntRank;                 /* result item shape; a rotation */
	ptrdiff_t    aIntShape[VARIDX_MAX];    /* applied to a vector SHRINKS it */

	int nRef;
};

/* Was this pairing claimed?

   Three states, not two.  A DECLINE is silent and means "not mine, ask the
   other operand"; a REFUSE means "this pairing is mine and it is illegal",
   and the refusing form has already said why in terms of the actual problem.
   Collapsing them would turn a frame mismatch into a generic "no rule for
   rotation * vector", which is a worse diagnostic and very nearly a lie. */
typedef enum das_binop_stat_e {
	dbsDecline = 0,
	dbsOkay,
	dbsRefuse
} das_binop_stat;

int DasBinOp_incRef(DasBinOp* pThis);
int DasBinOp_decRef(DasBinOp* pThis);

/* Per item, so a macro: this is the walker's inner loop */
#define DasBinOp_apply(P,L,R,O) ((P)->pVTbl->apply((P),(L),(R),(O)))


/* ========================================================================= *
 * DasForm, the base.
 * ========================================================================= */

struct DasForm_VTbl {

	const char* sKind;          /* the wire kind= token, "vector", "rotation" */

	/* An unbound instance for setParam to fill, the kind's
	   _new_DasForm<Kind>_empty().  Registering the vtable in form.c's kind
	   table is the ENTIRE footprint of a new formalism outside its own file;
	   if it ever becomes more than that, this layer has failed. */
	DasForm* (*create)(void);

	/* Accept one <ops> attribute.  A form refuses a name it does not know: a
	   reader that claims a kind must not skip a misspelled parameter. */
	das_errcode (*setParam)(
		DasForm* pThis, const char* sName, const char* sVal
	);

	/* Is this form usable for a variable of this internal shape?

	   Called once when the form is attached to a variable, and the point
	   where the form learns its shape: compSym() and any shape dependent
	   parameter answer from what was recorded here.  Check that required
	   parameters are set or their defaults are okay, and that the external
	   world will hand in element composites of the right shape.

	   NULL when a kind has nothing to insist on.
	   Returns DAS_OKAY or an error naming what is wrong. */
	das_errcode (*validate)(
		DasForm* pThis, int nIntRank, const ptrdiff_t* pIntShape
	);

	/* Read one formalism parameter.

	   pType, when not NULL, receives a DASPROP_* code that adheres to the
	   DasProp_type() return type.  For formalisms the parameter names imply
	   an explicit type, which is provided here.

	   Returns NULL if this form has no such parameter.  The returned string
	   is owned by the form and lives as long as it does. */
	const char* (*getParam)(
		const DasForm* pThis, const char* sName, ubyte* pType
	);

	/* The name of the iParam'th parameter this kind can carry, in the order
	   encode() writes them, NULL past the end.  A known kind answers from a
	   static list; whether a parameter currently has a value is getParam's
	   business, so a name may come back here that getParam answers NULL for.
	   NULL slot for a kind with no parameters at all. */
	const char* (*paramName)(const DasForm* pThis, int iParam);

	/* Emit <ops .../>.  talkative: state a parameter even at its default,
	   since the schema cannot carry a default behind an anyAttribute. */
	das_errcode (*encode)(
		const DasForm* pThis, struct das_buffer* pBuf
	);

	/* Read one item run into a typed datum.  NULL when the kind has no single
	   datum form. */
	bool (*pack)(
		const DasForm* pThis, const das_operand* pOp, const ubyte* pRun,
		das_datum* pOut
	);

	/* The das_val_type a datum from this kind carries, vtUnknown to fall back
	   to the element type. */
	das_val_type (*datumType)(const DasForm* pThis);

	/* Render one item run.  NULL means "no rendering of my own", and the
	   caller falls back to printing the elements as plain numbers. */
	char* (*prnRun)(
		const DasForm* pThis, const ubyte* pRun, uint32_t nElems,
		das_val_type et, char* sBuf, size_t uLen
	);

	/* The canonical symbol for one component, in storage order.  NULL if this
	   kind has no named components, or none for that component.

	   No shape argument.  A form is owned outright by one variable and was
	   handed that variable's intern= at validate(), so it already knows how
	   many components it is describing. */
	const char* (*compSym)(const DasForm* pThis, int iComp);

	/* Claim a pairing, or decline it for the other side to try.

	   pThis is pL->pForm in the Left hook and pR->pForm in the Right one; it
	   rides along so every slot has the same shape.  The operands are NEVER
	   reordered by either hook, so an undefined pairing fails loud instead of
	   silently commuting.

	   nOp is a D2BOP_* code from oper.h.  There is no separate formalism
	   op enum; the library already has one vocabulary for operators and
	   das_vt_merge() and Units_canMerge() both speak it. */
	das_binop_stat (*binOpLeft)(
		const DasForm* pThis, const das_operand* pL, int nOp,
		const das_operand* pR, DasBinOp** ppOut
	);
	das_binop_stat (*binOpRight)(
		const DasForm* pThis, const das_operand* pL, int nOp,
		const das_operand* pR, DasBinOp** ppOut
	);

	DasForm* (*copy)(const DasForm* pThis);
	void     (*release)(DasForm* pThis);

};

struct das_form {
	const DasForm_VTbl* pVTbl;
};


/* ========================================================================= *
 * Helpers shared by the formalism modules, form.c defines them.
 * ========================================================================= */

/* Shared by the formalisms that carry a component order.  Emits sysorder=
   when it says something the default does not: a non-ascending order,
   printed to the component count recorded when validate() was run.

   Use sysorder= in XML headers when the order is not ascending.  pOrder is
   uComps entries long slot to canonical component, so this works for a nine
   element rotation as readily as a three element vector. */
das_errcode _dasform_prnOrder(
	struct das_buffer* pBuf, const ubyte* pOrder, ubyte uComps
);

/* The wire spelling of a component order, "1;0;2", for the forms that keep
   one beside the decoded order so getParam() stays a read.  Always all
   uComps slots, ascending or not: getParam reports defaults too. */
void _dasform_fmtOrder(
	char* sBuf, size_t uLen, const ubyte* pOrder, ubyte uComps
);

#ifdef __cplusplus
}
#endif

#endif /* _das_form_p_h_ */
