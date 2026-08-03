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
 * That is why there is no separate scale path here the way form_vector.c has
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
 * ANGLES ARE ALWAYS DEGREES on the wire, the same law form_vector.h states, so
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
 * Only one <ops> fits on a composite, so the pairing cannot be expressed by
 * stacking this kind on top of a vector.
 */

#ifndef _das_form_cplx_h_
#define _das_form_cplx_h_

#include <das3/form.h>

/* For the shared component-system numbering ONLY -- DAS_VSYS_UNKNOWN is the
   whole space's "no system" sentinel and form_vector.h is where the space is
   chartered.  This file learns nothing else about vectors from it: complex
   has no rule for one and declines the pairing.  form_geoloc.h reaches for the
   same header for the same reason. */
#include <das3/form_vector.h>

#ifdef __cplusplus
extern "C" {
#endif

/* --- component systems, the complex half --------------------------------- *
 *
 * The codes CONTINUE the numbering form_vector.h opens and form_geoloc.h
 * extends, for the reason stated there: one ubyte must read the same next to
 * whichever formalism handed it out.  A second space starting over at 1 would
 * make a bare system code meaningless without knowing its issuer, which is the
 * exact confusion the shared space exists to prevent.
 *
 * Sharing the space does NOT make these geometric.  das_vsys_str() and
 * das_geosys_str() answer NULL for both of them, which is correct: a complex
 * plane has no third component, no frame and no cross product, so no code in
 * this file may be handed to das_vsys_toCart().
 */

#define DAS_VSYS_RECT      0x00000007  /* real, imaginary       */
#define DAS_VSYS_POLAR     0x00000008  /* magnitude, phase      */

#define DAS_VSYS_CPLX_MIN  0x00000007
#define DAS_VSYS_CPLX_MAX  0x00000008

/** Wire token for a complex system code, NULL if it is not one of these two.
 *
 * Deliberately narrow: this is not a superset lookup the way das_geosys_str()
 * is over the geometric six.  Nothing needs to switch over a geometric system
 * and a complex one in the same statement, so nothing here invites it. */
DAS_API const char* das_cplxsys_str(ubyte uSys);

/** Wire token to complex system code, DAS_VSYS_UNKNOWN if unrecognized.
 * Accepts the leading "rect" and "polar" the way das_vsys_id() accepts
 * "cart", since abbreviating a system name is long-standing wire practice. */
DAS_API ubyte das_cplxsys_id(const char* sSys);

/** One line of prose about a complex system, for a "notes" property. */
DAS_API const char* das_cplxsys_desc(ubyte uSys);

/** The canonical symbol for component iComp of a complex system, NULL if the
 * system is not one of these two or the component is not 0 or 1. */
DAS_API const char* das_cplxsys_symbol(ubyte uSys, int iComp);

/** Components as stored to a rectangular (real, imaginary) pair.
 * pIn and pOut may not overlap.  @returns false on a loud error. */
DAS_API bool das_cplx_toRect(ubyte uSys, const double* pIn, double* pOut);

/** A rectangular (real, imaginary) pair to components as stored.
 * pIn and pOut may not overlap.  @returns false on a loud error. */
DAS_API bool das_cplx_fromRect(ubyte uSys, const double* pIn, double* pOut);

/** The complex vtable, exported for identity comparison.  @memberof DasForm */
DAS_API extern const DasForm_VTbl das_form_cplx_vtbl;

/** Is this form a complex number?
 *
 * NULL tolerant for the reason DasForm_isVector() is: a byte run carries no
 * formalism at all, and "is it complex?" has a perfectly good answer for one.
 * @memberof DasForm */
#define DasForm_isCplx(P) (((P) != NULL)&&((P)->pVTbl == &das_form_cplx_vtbl))

/** Build a complex formalism.
 *
 * @param uSysType DAS_VSYS_RECT or DAS_VSYS_POLAR.  A geometric system code
 *        is refused: a complex number is not a vector in a frame.
 * @returns a new form with one reference, or NULL on a loud error.
 * @memberof DasForm */
DAS_API DasForm* new_DasFormCplx(ubyte uSysType);

/** Which representation this form's components are stored in.
 * @returns DAS_VSYS_RECT, DAS_VSYS_POLAR, or 0 on a form that is not complex.
 * @memberof DasForm */
DAS_API ubyte DasFormCplx_sysType(const DasForm* pThis);

/** The two components of a complex datum, AS STORED.
 *
 * Polar values come back as (magnitude, phase in degrees), not converted;
 * das_cplx_toRect() is the conversion and the caller decides whether it wants
 * one.  Symmetric with DasFormVector_values(), which likewise hands back a
 * curvilinear vector in its own system.
 *
 * @param pDm a vtComposite datum packed by this form
 * @param pOut receives up to nMax components
 * @param nMax the room in pOut; 2 is the whole answer
 * @returns the count written, or a negative error value.  @memberof DasForm */
DAS_API int DasFormCplx_values(
	const DasForm* pThis, const das_datum* pDm, double* pOut, int nMax
);

#ifdef __cplusplus
}
#endif

#endif /* _das_form_cplx_h_ */
