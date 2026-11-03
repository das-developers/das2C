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
 * Include this to build rotations or read their frames; core.h does not.
 */

#ifndef _das_form_rot_h_
#define _das_form_rot_h_

#include <das3/form.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Named only by the DAS_FORM_ROT macro below */
DAS_API extern const DasForm_VTbl das_form_rot_vtbl;

/** A rotation from one reference frame into another.  Applying one to a vector
 * gives a vector in the second frame.
 * @see DasForm_isKind(), DasVar_formIs().  @relates DasForm */
#define DAS_FORM_ROT (&das_form_rot_vtbl)


/* The two representations, as element counts.

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

/** Which layout does this rotation use?
 *
 * The form's system= as an element count, so a client need not derive it
 * from a variable's internal shape.
 *
 * @param pThis the formalism to inspect
 * @returns ROT_MATRIX, ROT_QUAT, or 0 with das_error called if the form is
 *          not a rotation.
 * @memberof DasForm */
DAS_API int DasFormRot_layout(const DasForm* pThis);


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
 * @returns a new form the caller owns, or NULL on a loud error.  NULL or
 *          empty for either frame is accepted here and refused later when
 *          the form is written out; a program may build the object before it
 *          knows its frames, but it may not write one out that way.
 * @memberof DasForm 
 */
DAS_API DasForm* new_DasFormRot(const char* sFrom, const char* sTo);

/** The frame this rotation starts in.
 * @returns the frame name, "" when unbound, or NULL with das_error called if
 *          the form is not a rotation.  @memberof DasForm */
DAS_API const char* DasFormRot_from(const DasForm* pThis);

/** The frame this rotation lands in.  @see DasFormRot_from
 * @memberof DasForm */
DAS_API const char* DasFormRot_to(const DasForm* pThis);

#ifdef __cplusplus
}
#endif

#endif /* _das_form_rot_h_ */
