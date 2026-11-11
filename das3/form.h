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

/** @file form.h Formalisms, what a variable's values mean to arithmetic */

#ifndef _das_form_h_
#define _das_form_h_

#include <das3/value.h>
#include <das3/units.h>
#include <das3/datum.h>
#include <das3/oper.h>
#include <das3/prop.h>

#include <das3/array.h>   /* only for the VARIDX_* index vocabulary */

#ifdef __cplusplus
extern "C" {
#endif

/** @addtogroup datamodel
 * @{
 */

/** The size of a buffer holding a formalism's named reference: a frame, a
 * body or a surface name.  Names are at most DASFORM_NAME_SZ - 1 characters
 * and longer ones are cut. */
#define DASFORM_NAME_SZ 64

/** @class DasForm form.h <das3/form.h>
 *
 * A formalism: what one value of a variable means to arithmetic, opaque
 *
 * A formalism says what one value of a variable is: a plain number, a
 * vector, a complex number, a rotation, a point on a scale.  das2C uses it
 * to decide which operations between variables are legal and what they
 * produce, and an application reads it to label and render values.
 *
 * Every numeric variable has one, see DasVar_form().  The kinds das2C
 * knows, by the kind= names used in stream headers:
 *
 * |  kind     | Header      | What one value is                            |
 * |-----------|-------------|----------------------------------------------|
 * | linear    | form_lin.h  | a plain number, what an absent \<ops\> means |
 * | point     | form_pt.h   | a position on a scale, calendar time above all |
 * | complex   | form_cplx.h | a complex number, two components             |
 * | vector    | form_vec.h  | a geometric vector in a named frame          |
 * | position  | form_pos.h  | a location relative to a body                |
 * | rotation  | form_rot.h  | a rotation from one frame into another       |
 *
 * core.h does not include the kind headers; include the ones you use.
 *
 * @par Making one
 *
 * Call new_DasForm_pairs() with attribute pairs as they appear in a stream
 * header, or a kind's own
 * constructor, such as new_DasFormVec() or new_DasFormPos(), from that
 * kind's header.  A form you build is yours: free it with del_DasForm().
 * The variable constructors take a copy of the form they are given, so each
 * variable has its own and forms are never shared.
 *
 * @par Asking about one
 *
 * | Call                                   | Answers                           |
 * |----------------------------------------|-----------------------------------|
 * | DasForm_param(), DasForm_paramAt()  | its parameters, by name or index  |
 * | DasForm_compSym(), DasForm_valToStr()    | component symbols and value text  |
 * | DasForm_toStr()                        | the kind and parameters, one line |
 * | DasForm_isKind()                       | which kind it is                  |
 *
 * Use DasForm_isKind() when the answer really depends on the kind: before
 * calling a kind's own accessors (DasFormVec_sysId() and friends), when
 * mapping kinds onto another format's vocabulary, or when an operation is
 * only defined for one kind.  A kind added to das2C later answers false
 * there, which is what those uses want.  When the question is about a
 * parameter, such as "does this have a frame?", ask DasForm_param()
 * instead, so that kinds added later answer it too.
 */
typedef struct das_form_t DasForm;

/** A formalism kind token
 *
 * Each kind has exactly one, so tokens are compared by address and never
 * looked inside.  The kind headers name them: DASFORM_VEC, DASFORM_POS,
 * DASFORM_ROT and so on, plus DASFORM_EXT below.  DasForm_lookup() finds
 * one from a kind= name.
 * @relates DasForm
 */
typedef struct das_formvtable_t DasFormVTable;


/** The kind= name of a formalism, "linear", "point", "vector" and so on,
 * as written in a stream header
 *
 * For a kind das2C does not know, DASFORM_EXT, this is the name as it
 * arrived in the stream.  To test for a kind das2C knows, use
 * DasForm_isKind() rather than comparing names.
 *
 * @param pThis the formalism, which may be NULL
 * @returns the name, owned by the form, or NULL if pThis is NULL
 * @memberof DasForm */
DAS_API const char* DasForm_kind(const DasForm* pThis);

/** Is this formalism of the given kind?
 *
 * @code
 *     if(DasForm_isKind(pForm, DASFORM_POS)) // ...
 * @endcode
 * See the DasForm description for when a kind test is the right question.
 *
 * @param pThis the formalism, which may be NULL
 * @param pKind a kind token, DASFORM_VEC etc., or one from DasForm_lookup()
 * @returns true if pThis is of that kind, false for a NULL form
 * @memberof DasForm
 */
DAS_API bool DasForm_isKind(const DasForm* pThis, const DasFormVTable* pKind);

/* Named only by the DASFORM_EXT macro below */
DAS_API extern const DasFormVTable DasFormExt_vtable;

/** The kind of a formalism das2C does not know
 *
 * Its parameters are carried through, see DasForm_param(), and written
 * back out, but it defines no operations and names no components.  An
 * application that knows the kind does its own math on the values.
 *
 * @see DasForm_isKind(), DasVar_hasForm().  @relates DasForm
 */
#define DASFORM_EXT (&DasFormExt_vtable)

/** Release a formalism
 *
 * Forms are not shared: whoever makes one frees it.
 *
 * @param pThis the form to free.  NULL is accepted and does nothing, since
 *        a string or blob variable has no formalism.
 * @memberof DasForm */
DAS_API void     del_DasForm(DasForm* pThis);

/** An independent duplicate of a formalism
 * @returns a new form the caller owns, or NULL if handed NULL
 * @memberof DasForm */
DAS_API DasForm* new_DasForm_copy(const DasForm* pThis);

/** Read one parameter by its attribute name
 *
 * Each kind defines its own parameters; its header lists them.  A parameter
 * at its default value is reported like one the stream gave: a vector with
 * no system= still answers "cartesian".
 *
 * @param pThis the formalism, which may be NULL
 * @param sName the attribute name, e.g. "frame", "system", "center"
 * @param pType if not NULL, receives the DASPROP_* type of the value, or 0
 *        when there is no such parameter
 * @returns the value as it is spelled in a stream header, owned by the
 *          form and valid as
 *          long as it is, or NULL if this form has no such parameter.  NULL
 *          is an answer, not an error: "does this have a frame?" is a normal
 *          question.
 * @memberof DasForm
 */
DAS_API const char* DasForm_param(
	const DasForm* pThis, const char* sName, uint8_t* pType
);

/** Bytes in a DasFormParamBuf, enough for any one parameter as a DasProp */
#define DASFORM_PARAM_SZ 128

/** How many parameters this form holds, kind= excluded
 *
 * Every parameter that has a value is counted, defaults included: a vector
 * with no system= in its header still reports system=cartesian.  Optional
 * parameters never given and without a default (a vector's frame, a
 * position's surface) are not counted.
 *
 * @returns the count, 0 for NULL or a kind with no parameters
 * @memberof DasForm */
DAS_API int DasForm_numParams(const DasForm* pThis);

/** Caller owned storage for one parameter returned by DasForm_paramAt()
 *
 * Declare one of these rather than a byte array; the union keeps the
 * property inside it aligned.  The DasProp pointer returned points into it.
 */
typedef union das_formparambuf_t {
	uint64_t uAlign;
	uint8_t    aBytes[DASFORM_PARAM_SZ];
} DasFormParamBuf;

/** One parameter as a typed property, by index
 *
 * For walking all of a form's parameters together with DasForm_numParams().
 * The order is the order the kind writes them in its \<ops\> element.
 *
 * @param pThis the formalism, which may be NULL
 * @param iParam the parameter, 0 based, over the set DasForm_numParams()
 *        counts
 * @param pBuf storage for the property, which the returned pointer refers
 *        into; keep it as long as you use the property
 * @returns the property, or NULL past the end.  For a kind das2C knows it
 *          has the parameter's real type; for DASFORM_EXT every parameter
 *          is a string, since das2C cannot know better.
 * @memberof DasForm */
DAS_API const DasProp* DasForm_paramAt(
	const DasForm* pThis, int iParam, DasFormParamBuf* pBuf
);

/** Build a form from attribute name, value pairs, as expat delivers them
 *
 * kind= is required.  A stream element with no \<ops\> means linear, but
 * that default belongs to the stream reader, not to this call.  A kind das2C
 * does not know is not an error: the result is a DASFORM_EXT form that
 * carries the parameters for application code and writes them back out, but
 * takes part in no arithmetic.  A parameter that a known kind does not
 * recognize is an error.
 *
 * @param psAttr name, value, name, value and so on, ending in NULL
 * @returns a new form the caller owns and frees with del_DasForm(), or NULL
 *          after logging why not
 * @memberof DasForm
 */
DAS_API DasForm* new_DasForm_pairs(const char** psAttr);

/** The symbol for one component of a composite value
 *
 * What a plot or table needs to label N stacked component lines without
 * understanding the formalism: "x", "y", "z", or "re" and "im", and so on.
 * Components count from zero in storage order, so any sysorder= has already
 * been applied.  The form has to know its shape first, see
 * DasForm_validate().
 *
 * @see DasVar_compSym(), the same answer from a variable
 *
 * @param pThis the formalism, which may be NULL
 * @param iComp the component, counting from 0 in storage order
 * @returns a constant string owned by the library, or NULL when this kind
 *          has no symbol for that component, or the value does not carry
 *          one that far.  NULL is a normal answer, not an error.
 * @memberof DasForm */
DAS_API const char* DasForm_compSym(const DasForm* pThis, int iComp);

/** Render one value through its formalism
 *
 * @param pThis  the formalism, which may be NULL
 * @param pRun   the value's elements, in storage order
 * @param nElems how many elements are in pRun
 * @param et     the element type
 * @param sBuf   receives the text, always NUL terminated when uLen > 0
 * @param uLen   the size of sBuf, including room for the NUL
 * @returns sBuf, or NULL when this kind has no rendering of its own and the
 *          caller should print the elements as plain numbers
 * @memberof DasForm */
DAS_API char* DasForm_valToStr(
	const DasForm* pThis, const uint8_t* pRun, uint32_t nElems,
	das_vt et, char* sBuf, size_t uLen
);

/** The kind and parameters of a formalism on one line
 *
 * For example:
 *
 *    vector frame=IAU_EARTH system=spherical
 *    rotation from=TSCS to=GEI2000
 *
 * A kind with no parameters, such as linear or point, is just its name.
 *
 * @param pThis the formalism; NULL gives an empty string
 * @param sBuf  receives the text, always NUL terminated when uLen > 0
 * @param uLen  the size of sBuf, including room for the NUL
 * @returns sBuf, or NULL if the whole line did not fit or the form could
 *          not be rendered
 * @memberof DasForm */
DAS_API char* DasForm_toStr(const DasForm* pThis, char* sBuf, size_t uLen);

/** Fit a formalism to the internal shape of the values it describes
 *
 * The variable constructors do this, so a form taken from a variable is
 * already fitted.  Call it yourself when using a form without a variable,
 * for example to label the columns of your own buffers: until a form knows
 * its shape, DasForm_compSym() answers NULL for every component.
 *
 * @param pThis the formalism; NULL is accepted and is always okay
 * @param nIntRank the internal rank, 0 for a scalar
 * @param pIntShape the internal extents, DASIDX_RAGGED for a ragged level.
 *        May be NULL when nIntRank is 0.
 * @returns DAS_OKAY, or an error saying why the form cannot describe values
 *          of that shape
 * @memberof DasForm
 */
DAS_API das_errcode DasForm_validate(
	DasForm* pThis, int nIntRank, const ptrdiff_t* pIntShape
);

/** The kind token for a kind= name, such as "vector"
 *
 * For comparing a form against a kind known only by name:
 * DasForm_isKind(pForm, DasForm_lookup(sKind)).
 *
 * @returns the token, or NULL for a name das2C does not know.  Forms of
 *          unknown kinds are DASFORM_EXT.
 * @relates DasForm */
DAS_API const DasFormVTable* DasForm_lookup(const char* sKind);

/** @} */

#ifdef __cplusplus
}
#endif

#endif /* _das_form_h_ */
