/* Copyright (C) 2026 Chris Piker <chris-piker@uiowa.edu>
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

/** @file form.h What a variable's values (i.e. collections of elements) mean
 *  to arithmetic.  Corresponds to <ops kind="thing" />
 *
 * Interface
 * ---------
 * `ls das3/form_*.c` is the inventory of known formalisms.  This file, form.c,
 * provides no default formalism at all.  Most interactions with formalisms are
 * via virtual functions; in general, formalism specific functions are often
 * not needed.  Though if you are making your own formalisms, not just
 * serializing from an input file, you need to know the concrete type at
 * construction.  Thus the general principle:
 *
 *   Construction needs the concrete type, interrogations should not
 *
 * DasForm_isKind() and its type objects (DAS_FORM_VEC and friends) answer
 * "which kind is this", and they have exactly two legitimate callers:
 *
 *   - the formalism itself, guarding its own downcast before a cast
 *   - a peer formalism claiming a binop pairing, per the one-way rule below
 *
 * A client asking a *question* must not use them.  Naming the forms that
 * happen to answer today is a trait written out by enumeration, and it rots
 * the day a new formalism answers too: the test returns false, which is
 * indistinguishable from a legitimate "no", so nothing complains.  Ask instead
 *
 *   DasForm_getParam(pForm, "frame", NULL)   parameters, by name
 *   the vtable                               behavior, by dispatch
 *
 * both of which a formalism written next year answers without the client being
 * touched.
 *
 * Constructing a formalism is the other half, and it does not reduce.  A client
 * emitting a position must call new_DasFormPos() and one emitting a free
 * vector must call new_DasFormVector(); no parameter query distinguishes them,
 * because the difference is the algebra and not the parameter set, which the
 * position formalism merely extends.  So a writer names a concrete type once,
 * at the site where it decides what it is making.
 * 
 * Purpose
 * -------
 * Understanding the roles of formalisms within variables can be summarized as:
 *
 *    Forms compute, variables walk
 *
 * A form is handed values and hands back values.  It never fetches.  It has
 * no generator, no array, no index and no loop, so there is exactly one piece
 * of code that knows how to walk external indices and it lives at the variable
 * layer.  Everything else follows from this:
 *
 *   - binop apply() takes *one* item run from each side and writes *one* out
 *   - a form gets a das_operand, a snapshot of facts, never a DasVar
 *   - no form_*.c includes variable.h; form.h needs only value, units, datum
 *
 * When non-local math arrives (interpolation, convolution, a point spread
 * response) the answer is *not* to hand forms a generator.  The recipe declares
 * a stencil, the variable gathers that window and passes it in, and the
 * walker is still the only thing that walks.  A form says what it needs;
 * it never goes and gets it.
 *
 * Knowledge flows one way
 * -----------------------
 * A form may know a peer form.  So for example code in:
 *
 *   form_rot.c
 *
 * includes:
 *
 *    form_vec.h
 *
 * because a rotation is defined as a thing that acts on vectors, and so
 * rotations have to know about vectors in order to provide useful operations.
 * The arrow never reverses -- form_vec.c need not know what a rotation is.
 * That asymmetry is what binOpLeft and binOpRight are *for*.  They are not a
 * fallback mechanism; they are how the better-informed partner claims a
 * pairing no matter which side it is standing on.
 */

#ifndef _das_form_h_
#define _das_form_h_

#include <das3/value.h>
#include <das3/units.h>
#include <das3/datum.h>
#include <das3/operator.h>
#include <das3/property.h>

#include <das3/array.h>   /* only for the VARIDX_* index vocabulary */

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
 * DasVar_setAry() that re-tags an element type.
 *
 * These four are the complete list.  Anything else a form wants is either its
 * own state (a rotation's two frames) or something it must ask a PEER form
 * for through that form's header.
 */
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
	 * Derived classes of DasForm need to check to see if all their required
	 * parameters are initialize or the defaults are okay.  It's also the time
	 * to see if the external world will hand-in element composites of the right
	 * shape.  Basically the hand-shake agreement stage.
	 *
	 * NULL when a kind has nothing to insist on.  
	 * @returns DAS_OKAY or error naming what is wrong. 
	 */
	DasErrCode (*validate)(
		DasForm* pThis, int nIntRank, const ptrdiff_t* pIntShape
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

	/* The name of the iParam'th parameter this kind can carry, in the order
	   encode() writes them, NULL past the end.  A known kind answers from a
	   static list; whether a parameter currently has a value is getParam's
	   business, so a name may come back here that getParam answers NULL for.
	   NULL slot for a kind with no parameters at all. */
	const char* (*paramName)(const DasForm* pThis, int iParam);

	/* Emit <ops .../>.  talkative: state a parameter even at its default,
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

	/* Render one item run.  NULL means "no rendering of my own", and the
	   caller falls back to printing the elements as plain numbers. */
	char* (*prnRun)(
		const DasForm* pThis, const ubyte* pRun, uint32_t nElems,
		das_val_type et, char* sBuf, size_t uLen
	);

	/* The canonical symbol for one component, in storage order.  NULL if this
	 * kind has no named components, or none for that component.
	 *
	 * No shape argument.  A form is owned outright by one variable and was
	 * handed that variable's intern= at validate(), so it already knows how
	 * many components it is describing.
	 */
	const char* (*compSym)(const DasForm* pThis, int iComp);

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
};

/** Get the math formalism token
 *
 * Each mathematical form is identified by a short string. Some of 
 * them are know by das2C, some are not.  Regardless, the math 
 * rules are identified by the token returned here.  Examples include
 * "linear", "point", "vector" etc.
 * 
 * @param pThis the formalism, which may be NULL
 * @returns the form token, or NULL if pThis is NULL
 * @memberof DasForm */
DAS_API const char* DasForm_kind(const DasForm* pThis);

/** Is this formalism of the given kind?
 *
 * This macro compares formalism virtual table addresses directly, for
 * speed.  Each formalism defines a macro for its virtual table, use those here.
 * For example
 * @code
 *     if(DasForm_isKind(pForm, DAS_FORM_POS)) // ...
 * @endcode
 * @returns true if this formalism matches the given one.
 * @memberof DasForm
 */
#define DasForm_isKind(P,VT)  (((P) != NULL)&&((P)->pVTbl == (VT)))

/* The vtable every unrecognized kind= binds to.  Exported so that its address
   can be compared.  Client code uses the DAS_FORM_EXT macro below and has no
   reason to name this directly. */
DAS_API extern const DasForm_VTbl das_form_generic_vtbl;

/** An extended formalism unknown to das2C. Attributes are carried through
 * but no binary operations are defined, though specific applications may
 * know how to operate on these numeric collections.
 *
 * @see DasForm_isKind(), DasVar_formIs().  @relates DasForm
 */
#define DAS_FORM_EXT (&das_form_generic_vtbl)

/** Release a formalism.
 *
 * Forms are wholly owned and are not shared. Whoever makes one deletes it.
 *
 * @param pThis the form to free; NULL is accepted and does nothing, since a
 *        byte run's formalism is legitimately absent.
 * @memberof DasForm */
DAS_API void     del_DasForm(DasForm* pThis);

/** An independent duplicate of a formalism.
 * @returns a new form the caller owns, or NULL if handed NULL.
 * @memberof DasForm */
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
 * @memberof DasForm
 */
DAS_API const char* DasForm_getParam(
	const DasForm* pThis, const char* sName, ubyte* pType
);

/** Bytes in a das_form_param_buf.  Parameters are short names with short
 * values (a frame name tops out at DASFORM_NAME_SZ), so this is generous. */
#define DASFORM_PARAM_SZ 128

/** How many parameters this form currently holds, kind= excluded.
 *
 * Every parameter that has a value is counted, including ones at their
 * defaults: a vector with no system= on the wire still reports
 * system=cartesian.  Optional parameters that were never given and have no
 * default (a vector's frame, a position's surface) are not counted.
 *
 * @returns the count, 0 for NULL or a kind with no parameters
 * @memberof DasForm */
DAS_API int DasForm_numParams(const DasForm* pThis);

/** Caller owned storage for one formalism parameter as a DasProp.
 *
 * To insure proper byte allignment, create instances of this value
 * to store data returned from DasForm_paramAt.
 */
typedef union das_form_param_buf {
	uint64_t uAlign;
	ubyte    aBytes[DASFORM_PARAM_SZ];
} das_form_param_buf;

/** One parameter as a typed property.
 *
 * Get a single math operations paramter, use with DasForm_numParams()
 * to determine the loop extent.
 *
 * Output order is encode() order.
 *
 * @param pThis the formalism, which may be NULL
 * @param iParam 0 based, over the same set DasForm_numParams() counts
 * @param pBuf backing storage for the return property, keep it around as
 *        long as you want to use the return DasProp* pointer.
 * @returns the property, which is properly typed if this formalism
 *          is not generic and is actually one known by das2C.
 *          Returns NULL past the end
 * @memberof DasForm */
DAS_API const DasProp* DasForm_paramAt(
	const DasForm* pThis, int iParam, das_form_param_buf* pBuf
);

/** Build a form from keyword, value string pairs.
 *
 * Hand it expat's NULL-terminated name/value array and it does the whole job.
 * 
 * An unrecognized 'kind=' is not an error, it just produces the generic
 * form which can only hold parameters for application level code.  It can
 * not be used with das2C functions for auto-math, but is otherwise useful.
 *
 * Note that the at least the 'kind' attribute must be provided. To produce
 * a Linear Formalism, call that form's type-specific constructor directly.
 *
 * The result carries ONE reference; release it with del_DasForm().
 *
 * @param psAttr name/value pairs, NULL-terminated, as expat delivers them
 * @returns a new form, or NULL on a loud error.  
 * @memberof DasForm 
 */
DAS_API DasForm* new_DasForm_pairs(const char** psAttr);

/** Render one item run the way its formalism says it reads.
 *
 * The dispatcher datum.c calls.  It declares this itself rather than
 * including form.h, which already includes datum.h because pack() hands one
 * back, so the include would close a cycle.
 *
 * @returns sBuf, or NULL if the formalism has no rendering of its own, in
 *          which case the caller prints the elements as plain numbers.
 * 
 * @memberof DasForm
 */
/** The canonical symbol for one component of a composite value.
 *
 * What a client needs to label N stacked component lines without understanding
 * the formalism: "x", "λ", "re", "xy" and so on.  Storage order, so a form's
 * sysorder= has already been applied.
 *
 * @see DasVar_compSym() to get this information via a DasVar pointer.
 *
 * @param pThis the formalism, which may be NULL
 * @param iComp the component, counting from 0 in storage order
 * @returns a constant symbol owned by the library, or NULL when this kind has
 *          no symbol for that component, or the value does not carry one that
 *          far.  NULL is a normal answer, not an error.
 * @memberof DasForm */
DAS_API const char* DasForm_compSym(const DasForm* pThis, int iComp);

/** Render one item run through its formalism
 *
 * @param pThis  the formalism, which may be NULL
 * @param pRun   the item's elements, in storage order
 * @param nElems how many elements are in pRun
 * @param et     the element type
 * @param sBuf   receives the text, always NUL terminated when uLen > 0
 * @param uLen   the size of sBuf, including room for the NUL
 * @returns sBuf, or NULL when this kind has no rendering of its own and the
 *          caller should print the elements as plain numbers
 * @memberof DasForm */
DAS_API char* DasForm_prnRun(
	const DasForm* pThis, const ubyte* pRun, uint32_t nElems,
	das_val_type et, char* sBuf, size_t uLen
);

/** The kind and parameters of a formalism on one line 
 *
 * Examples: 
 *    vector frame=IAU_EARTH system=spherical
 *    rotation from=TSCS to=GEI2000
 * 
 * A kind with no attributes, such as linear or point, is just 
 * its name (ex: "point").
 *
 * @param pThis the formalism, which may be NULL for an empty string
 * @param sBuf  receives the text, always NUL terminated when uLen > 0
 * @param uLen  the size of sBuf, including room for the NUL
 * @returns sBuf, or NULL if the whole line did not fit or the form could
 *          not be rendered
 * @memberof DasForm */
DAS_API char* DasForm_toStr(const DasForm* pThis, char* sBuf, size_t uLen);

/** Is this form usable for a variable of this internal shape?
 *
 * Called by the variable constructors; an application building a form by hand
 * not need to call it.  @returns DAS_OKAY or a loud error.  
 * 
 * @memberof DasForm 
 */
DAS_API DasErrCode DasForm_validate(
	DasForm* pThis, int nIntRank, const ptrdiff_t* pIntShape
);

/** The vtable for a wire token, or NULL on a miss.  For identity comparisons
 * where a form object is not wanted. */
DAS_API const DasForm_VTbl* das_form_lookup(const char* sKind);

#ifdef __cplusplus
}
#endif

#endif /* _das_form_h_ */
