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

/** @file form_cplx.h A complex number: two components, one value.
 *
 * A transfer function, a cross spectral density, a calibration response.  Two
 * numbers that are not two measurements: they are one quantity that happens to
 * need a plane rather than a line to sit in.  That is the whole reason this is
 * a formalism and not a labeled 2-bundle -- multiplication of a bundle is
 * component-wise and multiplication of a complex number is not, so a reader
 * that guessed would produce a confident wrong answer.
 *
 *     complex +- complex  = complex
 *     complex *   complex = complex   (ac - bd, ad + bc), NOT component-wise
 *     complex /   complex = complex
 *     complex op  real    = complex   the real promotes to (v, 0)
 *     real    op  complex = complex   stated explicitly, never inferred
 *
 * A real number entering complex arithmetic is promoted to a zero-imaginary
 * complex, so every rule above is one rule with a promotion in front of it.
 * That is why there is no separate scale path here the way form_vec.c has
 * one: scaling a complex IS multiplying by (v, 0).
 *
 * Parameters:
 *
 *   system=    rectangular | polar.  Default rectangular.
 *
 * Two representations, one meaning:
 *
 *     rectangular   real, imaginary
 *     polar         magnitude, phase
 *
 * ANGLES ARE ALWAYS DEGREES in a das stream, the same law form_vec.h states, so
 * a polar variable's units= describes the MAGNITUDE and the phase carries no
 * units of its own.  Both components of a rectangular variable share the one
 * units= between them, since a real part and an imaginary part of different
 * scales would not be a number.
 *
 * The arithmetic runs in rectangular and converts back to the LEFT operand's
 * system, so a polar variable stays polar through a multiply.
 *
 * EXACTLY TWO COMPONENTS, one level: intern="2".  A complex vector -- three
 * complex numbers making up a spectral E-field, say -- is real and common and
 * is its own formalism yet to be written, not this one with a bigger shape.
 * Only one \<ops\> fits on a composite, so the pairing cannot be expressed by
 * stacking this kind on top of a vector.
 */

#ifndef _das_form_cplx_h_
#define _das_form_cplx_h_

#include <das3/form.h>

/* For the shared component-system numbering ONLY -- DASVECSYS_UNKNOWN is the
   whole space's "no system" sentinel and form_vec.h is where the space is
   chartered.  This file learns nothing else about vectors from it: complex
   has no rule for one and declines the pairing.  form_pos.h, by contrast,
   includes it for the geometric systems' lookups as well. */
#include <das3/form_vec.h>

#ifdef __cplusplus
extern "C" {
#endif

/* --- component systems, the complex half --------------------------------- *
 *
 * The codes CONTINUE form_vec.h's numbering, for the reason stated there:
 * one ubyte must read the same next to whichever formalism handed it out.  A
 * second space starting over at 1 would make a bare system code meaningless
 * without knowing its issuer, which is the exact confusion the shared space
 * exists to prevent.
 *
 * Sharing the space does NOT make these geometric.  dasvecsys_name() answers
 * NULL for both of them, which is correct: a complex plane has no third
 * component, no frame and no cross product, so no code in this file may be
 * handed to dasvecsys_toCart().
 */

#define DASCPLXSYS_RECT      0x00000007  /* real, imaginary       */
#define DASCPLXSYS_POLAR     0x00000008  /* magnitude, phase      */

#define DASCPLXSYS_MIN  0x00000007
#define DASCPLXSYS_MAX  0x00000008

/** The system= token for a complex system code, NULL if it is not one of
 * these two.
 *
 * Deliberately narrow.  dasvecsys_name() spans two formalisms because vectors
 * and positions share the geometric six, but nothing needs to switch over a
 * geometric system and a complex one in the same statement, so nothing here
 * invites it. */
DAS_API const char* dascplxsys_name(uint8_t uSys);

/** The complex system code for a system= token, DASVECSYS_UNKNOWN if
 * unrecognized.
 * Accepts the leading "rect" and "polar" the way dasvecsys_id() accepts
 * "cart", since abbreviating a system name is long-standing practice in
 * stream headers. */
DAS_API uint8_t dascplxsys_id(const char* sSys);

/** One line of prose about a complex system, for a "notes" property. */
DAS_API const char* dascplxsys_info(uint8_t uSys);

/** The canonical symbol for component iComp of a complex system, NULL if the
 * system is not one of these two or the component is not 0 or 1. */
DAS_API const char* dascplxsys_compSym(uint8_t uSys, int iComp);

/** Components as stored to a rectangular (real, imaginary) pair.
 * pIn and pOut may not overlap.  @returns false on a loud error. */
DAS_API bool dascplxsys_toRect(uint8_t uSys, const double* pIn, double* pOut);

/** A rectangular (real, imaginary) pair to components as stored.
 * pIn and pOut may not overlap.  @returns false on a loud error. */
DAS_API bool dascplxsys_fromRect(uint8_t uSys, const double* pIn, double* pOut);

/* Exported so that its address can be compared.  Client code uses the
   DASFORM_CPLX macro below and has no reason to name this directly. */
DAS_API extern const DasFormVTable DasFormCplx_vtable;

/** A complex number, two components making a single value.  Multiplication is
 * the complex rule and not component by component.
 * @see DasForm_isKind(), DasVar_hasForm().  @relates DasForm */
#define DASFORM_CPLX (&DasFormCplx_vtable)

/** Build a complex formalism.
 *
 * @param uSysType DASCPLXSYS_RECT or DASCPLXSYS_POLAR.  A geometric system code
 *        is refused: a complex number is not a vector in a frame.
 * @returns a new form with one reference, or NULL on a loud error.
 * @memberof DasForm */
DAS_API DasForm* new_DasFormCplx(uint8_t uSysType);

/** Which representation this form's components are stored in.
 * @returns DASCPLXSYS_RECT, DASCPLXSYS_POLAR, or 0 on a form that is not complex.
 * @memberof DasForm */
DAS_API uint8_t DasFormCplx_sysId(const DasForm* pThis);

/* To read a complex datum's two components use DasDatum_toDoubles().  Values
   come back as stored, so a polar variable returns a magnitude and a phase in
   degrees.  Use dascplxsys_toRect() above to convert them. */

#ifdef __cplusplus
}
#endif

#endif /* _das_form_cplx_h_ */
