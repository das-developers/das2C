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

/** @file form_rot.h A rigid rotation from one reference frame into another.
 *
 * Rigid is the whole of it: lengths and handedness are preserved, so there is
 * no scaling, no shear and no reflection.  Everything this formalism promises
 * follows from that one constraint.  Two rotations compose into a rotation,
 * applying one to a vector gives a vector in the second frame, and the inverse
 * is the transpose.  A transform that stretches or reflects satisfies none of
 * those and is not a rotation; it would be a different kind= with its own
 * rules, not this one carrying a flag.
 *
 * WHO INCLUDES THIS
 * -----------------
 * Two audiences, and NOT a third.
 *
 * 1. A peer formalism that knows what a rotation is and implements a pairing
 *    with one.  Knowledge between formalisms is a directed ACYCLIC graph: the
 *    file that knows more includes the other's header and implements the
 *    hook.  form_rot.c includes form_vector.h because a rotation is defined
 *    as a thing that acts on vectors.  The arrow must never come back --
 *    form_vector.c including THIS header would make the graph cyclic and is
 *    a design error, not a shortcut.
 *
 * 2. A client that understands rotations: das3_spice building a rotated
 *    vector, a plotter labelling a transform.  This header is deliberately
 *    NOT reachable through core.h; a client that wants rotations names them.
 *
 * The third audience, generic library code, needs nothing here.  It reaches
 * everything through the DasForm vtable and never learns this type exists.
 * If something in variable.c or dataset.c ever wants this header, the layering
 * sprung a leak.
 *
 * WHY THE VTABLE IS EXPORTED
 * --------------------------
 * das_form_rotate_vtbl is one object in the whole program, so comparing its
 * ADDRESS answers "is this partner a rotation?" with no enum, no string, and
 * no kind field to keep in sync with the vtable it would describe.  That is
 * the one reason a vtable here is extern rather than static; the recipe
 * vtables inside form_rot.c stay static because nobody tags with them.
 */

#ifndef _das_form_rot_h_
#define _das_form_rot_h_

#include <das3/form.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Exported so that its address can be compared.  One object in the whole
   program, so its address answers "is this partner a rotation?".  Client code
   uses the DAS_FORM_ROT macro below and has no reason to name this directly. */
DAS_API extern const DasForm_VTbl das_form_rotate_vtbl;

/** A rotation from one reference frame into another.  Applying one to a vector
 * gives a vector in the second frame.
 * @see DasForm_isKind(), DasVar_formIs().  @relates DasForm */
#define DAS_FORM_ROT (&das_form_rotate_vtbl)


/* The two representations, as element counts.  A rotation's shape is NOT
   stored on the form: it is the operand's declared intern=, and it arrives in
   a das_operand.  Storing it in both places is how the two drift apart.

   Canonical element order, which is what sysorder= indexes:

     matrix      0 xx  1 xy  2 xz  3 yx  4 yy  5 yz  6 zx  7 zy  8 zz
                 row major, row is the to= axis and column the from= axis
     quaternion  0 w   1 x   2 y   3 z
                 scalar first, SPICE's convention

   A stream storing a matrix column major says sysorder="0;3;6;1;4;7;2;5;8";
   one storing a quaternion scalar last says sysorder="1;2;3;0".  Every element
   must be placed: a rotation has no default for a missing one. */
#define ROT_MATRIX  9    /* intern="3;3" */
#define ROT_QUAT    4    /* intern="4" */

/** Which layout is this operand carrying?
 *
 * Reports the operand form's system= as an element count, exported so a client
 * does not re-derive it from the extents and get it subtly different.
 *
 * @param pOp the operand to inspect
 * @returns ROT_MATRIX, ROT_QUAT, or 0 with das_error called if the operand
 *          does not carry a rotation.
 * @memberof DasForm */
DAS_API int DasFormRotate_layout(const das_operand* pOp);


/** Build a rotation formalism from two named frames.
 *
 * For code that already holds frame names, das3_spice being the case in mind.
 * Typically this function is called by stream serializers.
 *
 * The formalism defaults to a 3;3 matrix in canonical order.
 * 
 * To represent quaternions, follow this with a setParam of system="quaternion".
 * Das2C never picks the rotation representation for you by looking at the 
 * variable's internal shape.
 * 
 * A non-default storage order is stated with a setParam of sysorder=, 
 * and DasVar_compSym() then answers in that storage order.
 *
 * @param sFrom the name of the frame this rotation starts in
 * @param sTo the frame it lands in
 * @returns a new form with one reference, or NULL on a loud error.  Zero for
 *          either handle is accepted here and refused later by encode(); a
 *          program may build the object before interning its frames, but it
 *          may not write one out that way.
 * @memberof DasForm 
 */
DAS_API DasForm* new_DasFormRotate(const char* sFrom, const char* sTo);

/** The frame this rotation starts in.
 * @returns the frame name, or NULL when unbound.  Fails loud if the form is
 *          not a rotation.  @memberof DasForm */
DAS_API const char* DasFormRotate_from(const DasForm* pThis);

/** The frame this rotation lands in.  @see DasFormRotate_from
 * @memberof DasForm */
DAS_API const char* DasFormRotate_to(const DasForm* pThis);

#ifdef __cplusplus
}
#endif

#endif /* _das_form_rot_h_ */
