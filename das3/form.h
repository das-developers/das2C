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

/* Note this is a library-internal file, no stable public API is defined here */

/** @file form.h Axis D: what a variable's values mean to arithmetic.
 *
 * On the wire this is <ops kind="vector"/>.  In C it is DasForm, because the
 * object is a CLASSIFICATION and not a bag of verbs: pack, datumType and
 * prnIntr are not operations in the arithmetic sense.  A stream reader mostly
 * wants to know what math is allowed on a thing, storage having already been
 * taken care of for them.
 *
 * `ls das3/form_*.c` IS the inventory of known formalisms.  form.c holds no
 * rows.
 *
 * THE RULE: FORMS COMPUTE, VARIABLES WALK.
 * ----------------------------------
 * A form is handed values and hands back values.  It never fetches.  It has
 * no generator, no array, no index and no loop, so there is exactly one piece
 * of code that knows how to walk external indices and it lives at the
 * layer.  Everything else follows from this:
 *
 *   - binop apply() takes ONE item run from each side and writes ONE out
 *   - a form gets a das_operand, a snapshot of facts, never a DasVar
 *   - no form_*.c includes variable.h; form.h needs only value, units, datum
 *
 * When non-local math arrives (interpolation, convolution, a point spread
 * response) the answer is NOT to hand forms a generator.  The recipe declares
 * a stencil, the variable gathers that window and passes it in, and the
 * walker is still the only thing that walks.  A form says what it needs;
 * it never goes and gets it.
 *
 * KNOWLEDGE FLOWS ONE WAY.
 * ------------------------
 * A generator never knows what a form is.  A form may know a PEER form:
 * form_rot.c includes form_vector.h because a rotation is defined as a thing
 * that acts on vectors, and rotation therefore implements the pairing.  The
 * arrow never reverses -- form_vector.c must not learn what a rotation is.
 * That asymmetry is what binOpLeft and binOpRight are FOR.  They are not a
 * fallback mechanism; they are how the better-informed partner claims a
 * pairing no matter which side it is standing on.
 *
 * Design record: co_notes/libdas_ops_class_spec.md, and the wire side in
 * schema/das2c_operations.md.
 */

#ifndef _das_form_h_
#define _das_form_h_

#include <das3/value.h>
#include <das3/units.h>
#include <das3/datum.h>
#include <das3/operator.h>
#include <das3/property.h>

/* Only for the VARIDX_* index vocabulary, which is parked in Axis A's header
   and wants a home of its own.  Nothing else here uses a generator. */
#include <das3/generator.h>

#ifdef __cplusplus
extern "C" {
#endif

/* A formalism's named references -- a frame, a body, a center -- are plain
   strings stored on the form. */
#define DASFORM_NAME_SZ 64

struct das_buffer;

typedef struct das_form DasForm;


/* ========================================================================= *
 * das_operand: what a variable tells a form.
 * ========================================================================= */

/** The form layer's currency: a snapshot of the facts a formalism may need.
 *
 * The variable fills one of these from live facts at the moment of the call,
 * is why a form never reaches up and why nothing here can go stale behind a
 * DasVar_setArray() that re-tags an element type.
 *
 * These four are the complete list.  Anything else a form wants is either its
 * own state (a rotation's two frames) or something it must ask a PEER form
 * for through that form's header.
 */
typedef struct das_operand {

	/* The operand's formalism, carried so a form can recognize a partner by
	   vtable address.  Peer to peer, never upward. */
	const DasForm* pForm;

	das_val_type   vtElem;       /* what one cell of this operand holds */
	das_units      units;

	/* The internal shape as DECLARED.  Rank matters and the product does not:
	   a 3;3 rotation and a nine component vector share an element count and
	   are not the same thing. */
	int            nIntRank;              /* 0 for a scalar, aIntShape untouched */
	ptrdiff_t      aIntShape[VARIDX_MAX];

} das_operand;

/** Total elements in one item run, the product of the internal extents.
 * @returns the count, or 0 if any level is ragged (no fixed width). */
DAS_API size_t das_operand_elems(const das_operand* pThis);


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
	   the whole contract; see THE RULE above. */
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

/** Was this pairing claimed?
 *
 * Three states, not two.  A DECLINE is silent and means "not mine, ask the
 * other operand"; a REFUSE means "this pairing is mine and it is illegal",
 * and the refusing form has already said why in terms of the actual problem.
 * Collapsing them would turn a frame mismatch into a generic "no rule for
 * rotation * vector", which is a worse diagnostic and very nearly a lie.
 */
typedef enum das_binop_stat_e {
	dbsDecline = 0,
	dbsOkay,
	dbsRefuse
} das_binop_stat;

DAS_API int  DasBinOp_incRef(DasBinOp* pThis);
DAS_API int  DasBinOp_decRef(DasBinOp* pThis);

#define DasBinOp_apply(P,L,R,O) ((P)->pVTbl->apply((P),(L),(R),(O)))


/* ========================================================================= *
 * DasForm, the base.
 * ========================================================================= */

typedef struct DasForm_VTbl {

	const char* sKind;          /* the wire kind= token, "vector", "rotation" */

	/* An unbound instance for setParam to fill.  Registering this in form.c's
	   kind table is the ENTIRE footprint of a new formalism outside its own
	   file; if it ever becomes more than that, this layer has failed. */
	DasForm* (*create)(void);

	/* Accept one <ops> attribute.  A form refuses a name it does not know: a
	   reader that claims a kind must not skip a misspelled parameter. */
	DasErrCode (*setParam)(
		DasForm* pThis, const char* sName, const char* sVal
	);

	/* Is this form usable for a variable of this internal shape?
	 *
	 * Called once when the form is attached to a variable.
	 *
	 * Derived classes of DasForm need to check to see if all thier required
	 * parameters are initialize or the defaults are okay.  It's also the time
	 * to see if the external world will hand-in element composits of the right
	 * shape.  Basically the hand-shake agreement stage.
	 *
	 * NULL when a kind has nothing to insist on.  
	 * @returns DAS_OKAY or error naming what is wrong. 
	 */
	DasErrCode (*validate)(
		const DasForm* pThis, int nIntRank, const ptrdiff_t* pIntShape
	);

	/* Read one formalism parameter. 
    *
    * @param pType, when not NULL, receives a DASPROP_* code in that
	 * adheres to DasProp_type() return type.  For formalisms an parameter
	 * names imply an explicit type, which is provided here.
	 *
    * @returns NULL if this form has no such parameter.  The returned string 
    *   is owned by the form and lives as long as it does.
	 */
	const char* (*getParam)(
		const DasForm* pThis, const char* sName, ubyte* pType
	);

	/* Emit <ops .../>.  TALKATIVE: state a parameter even at its default,
	   since the schema cannot carry a default behind an anyAttribute. */
	DasErrCode (*encode)(
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

	char* (*prnIntr)(const DasForm* pThis, char* sBuf, int nLen);

	/* Render one item run.  NULL means "no rendering of my own", and the
	   caller falls back to printing the cells as plain numbers. */
	char* (*prnRun)(
		const DasForm* pThis, const ubyte* pRun, uint32_t nElems,
		das_val_type et, char* sBuf, int nLen
	);

	/* Claim a pairing, or decline it for the other side to try.
	 *
	 * pThis is pL->pForm in the Left hook and pR->pForm in the Right one; it
	 * rides along so every slot has the same shape.  The operands are NEVER
	 * reordered by either hook, so an undefined pairing fails loud instead of
	 * silently commuting.
	 *
	 * nOp is a D2BOP_* code from operator.h.  There is no separate formalism
	 * op enum; the library already has one vocabulary for operators and
	 * das_vt_merge() and Units_canMerge() both speak it.
	 */
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

} DasForm_VTbl;

struct das_form {
	const DasForm_VTbl* pVTbl;
	int nRef;
};

/** The wire kind= token this form answers to.  @memberof DasForm */
#define DasForm_kindStr(P) ((P)->pVTbl->sKind)

DAS_API int      DasForm_incRef(DasForm* pThis);
DAS_API int      DasForm_decRef(DasForm* pThis);
DAS_API DasForm* DasForm_copy(const DasForm* pThis);

/** Read one <ops> parameter back out by name.
 *
 * The names depend on the specifics of the formalism, each is free to
 * define it's own.
 *
 * @param sName the wire attribute name, e.g. "frame", "system", "center"
 * @param pType if not NULL, receives the DASPROP_* type of the parameter
 * @returns the parameter's wire spelling, or NULL if this form has no such
 *          parameter.  Owned by the form; see the vtable slot for the lifetime
 *          rule on parameters that are stored decoded.
 * @memberof DasForm */
DAS_API const char* DasForm_getParam(
	const DasForm* pThis, const char* sName, ubyte* pType
);

/** Build a form from keyword, value string pairs.
 *
 * Hand it expat's NULL-terminated name/value array and it does the whole job.
 * 
 * An unrecogsized 'kind=' is not an error, it just produces the generic
 * form which can only hold parameters for application level code.  It can
 * not be used with das2C functions for auto-math, but is otherwise useful.
 *
 * Note that the at least the 'kind' attribute must be provided. To produce
 * a Linear Formamlism, call that form's type-specific constructor directly.
 *
 * The result carries ONE reference; release it with DasForm_decRef().
 *
 * @param psAttr name/value pairs, NULL-terminated, as expat delivers them
 * @returns a new form, or NULL on a loud error.  @memberof DasForm */
DAS_API DasForm* new_DasForm_pairs(const char** psAttr);

/** Render one item run the way its formalism says it reads.
 *
 * The dispatcher datum.c calls.  It declares this itself rather than
 * including form.h, which already includes datum.h because pack() hands one
 * back, so the include would close a cycle.
 *
 * @returns sBuf, or NULL if the formalism has no rendering of its own, in
 *          which case the caller prints the cells as plain numbers. */
DAS_API char* DasForm_prnRun(
	const DasForm* pThis, const ubyte* pRun, uint32_t nElems,
	das_val_type et, char* sBuf, int nLen
);

/** Is this form usable for a variable of this internal shape?
 *
 * Called by the variable constructors; an application building a form by hand
 * not need to call it.  @returns DAS_OKAY or a loud error.  @memberof DasForm */
DAS_API DasErrCode DasForm_validate(
	const DasForm* pThis, int nIntRank, const ptrdiff_t* pIntShape
);

/** The vtable for a wire token, or NULL on a miss.  For identity comparisons
 * where a form object is not wanted.  @memberof DasForm */
DAS_API const DasForm_VTbl* das_form_lookup(const char* sKind);

/** Is this the generic (unrecognized kind) form?  @memberof DasForm */
DAS_API bool DasForm_isGeneric(const DasForm* pThis);

#ifdef __cplusplus
}
#endif

#endif /* _das_form_h_ */
