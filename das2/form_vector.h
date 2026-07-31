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

/** @file form_vector.h A frame-tagged geometric vector.  NO origin.
 *
 * A magnetic field, a velocity, a displacement: something with direction and
 * magnitude that is not measured FROM anywhere.  Free vectors add, subtract
 * and scale, and one day will dot and cross.
 *
 * Contrast form_geoloc.h, which is the same components with an ORIGIN and is
 * therefore an affine point: positions subtract to give one of these, and two
 * of them cannot be added at all.  The discriminator is the origin, which is
 * why they are two formalisms and not one with a flag -- see
 * co_notes/libdas_form_class_spec.md.
 *
 * `surface=` and `center=` are ILLEGAL here and say so loudly.  A surface is
 * an ellipsoid reference and a center is an origin; a free vector has neither,
 * so the systems needing them (detic, graphic) belong to geoloc.
 *
 * Parameters:
 *
 *   frame=     a CTX_FRAME reference, resolved to a handle here.  OPTIONAL:
 *              a frameless vector is legal, it just cannot be frame-checked
 *              against anything.
 *   body=      describes the FRAME, so it folds into the frame's context
 *              entry and is never stored here.
 *   system=    cartesian | cylindrical | spherical | centric.  Default
 *              cartesian, which is what nearly all measured data is.
 *   sysorder=  storage slot to canonical direction, e.g. "1;0;2".  Default
 *              ascending.
 *
 * ANGLES ARE ALWAYS DEGREES on the wire, so units carries whatever the
 * radial component is in.  The conversion helpers below take and return
 * degrees for that reason.
 */

#ifndef _das_form_vector_h_
#define _das_form_vector_h_

#include <das2/form.h>

/* For DAS_VSYS_*, das_compsys_* and VEC_DIRS3.  That vocabulary is slated to
   move here when geovec.[ch] dissolve; until then this include is the seam. */
#include <das2/geovec.h>

#ifdef __cplusplus
extern "C" {
#endif

/** The vector vtable, exported for identity comparison.  @memberof DasForm */
DAS_API extern const DasForm_VTbl das_form_vector_vtbl;

/** Is this form a frame-tagged free vector?  @memberof DasForm */
#define DasForm_isVector(P) ((P)->pVTbl == &das_form_vector_vtbl)

/** Build a vector formalism from resolved handles.
 *
 * @param uFrameId a CTX_FRAME handle, 0 for a frameless vector
 * @param uSysType a DAS_VSYS_* code; the ellipsoidal ones are refused
 * @param uDirs storage slot to canonical direction, packed with VEC_DIRS3
 * @returns a new form with one reference, or NULL on a loud error.
 * @memberof DasForm */
DAS_API DasForm* new_DasFormVector(
	ubyte uFrameId, ubyte uSysType, ubyte uDirs
);

/** The frame these components are expressed in, or 0 when frameless.
 * @memberof DasForm */
DAS_API ubyte DasFormVector_frameId(const DasForm* pThis);

/** The coordinate system, a DAS_VSYS_* code.  @memberof DasForm */
DAS_API ubyte DasFormVector_sysType(const DasForm* pThis);

/** Component order: storage slot to canonical direction, VEC_DIRS3 packed.
 * Slot i holds direction (uDirs >> 2*i) & 0x3.  @memberof DasForm */
DAS_API ubyte DasFormVector_dirs(const DasForm* pThis);

/** The display symbol for one storage slot: "x", "lat", "r" ...
 *
 * What a client needs to label N stacked component lines without
 * understanding the formalism.  This replaces the retired DasSet_vecMap()
 * and das_makeCompLabels(): the library hands out the symbol, the client
 * composes whatever label it wants.
 *
 * @returns a constant symbol, or NULL for a bad slot or a non-vector form.
 * @memberof DasForm */
DAS_API const char* DasFormVector_slotSym(const DasForm* pThis, int iSlot);

/** Read a vector datum's components as doubles, in STORAGE order.
 *
 * The supported replacement for casting a datum to a packed struct.  Missing
 * components are defaulted the way the old das_geovec_values() did: for the
 * curvilinear systems an absent radial component reads as 1.0, giving a unit
 * vector rather than a zero one.
 *
 * @param pThis the vector form, from das_datum_form()
 * @param pDm a vtComposite datum this form packed
 * @param pOut receives up to nMax components
 * @param nMax the size of pOut
 * @returns the count written, or a negative das error code.
 * @memberof DasForm */
DAS_API int DasFormVector_values(
	const DasForm* pThis, const das_datum* pDm, double* pOut, int nMax
);


/* --- coordinate system conversion --------------------------------------- *
 *
 * Free functions rather than form methods, because form_geoloc.c needs them
 * for the systems it shares with this file and adds its own ellipsoidal arms.
 * Values are in CANONICAL direction order (not storage order) and angles are
 * in DEGREES.
 *
 * Under SPICE=yes these delegate to cspice so that every das tool agrees to
 * the last bit.  Otherwise they do the trigonometry directly, which is exact
 * for these four systems -- no kernel data is involved.
 */

/** Convert one vector from its system to cartesian.
 * @returns false loudly for a system with no kernel-free conversion
 *          (detic, graphic), which are geoloc's business. */
DAS_API bool das_vsys_toCart(ubyte uSys, const double* pIn, double* pOut);

/** The inverse of das_vsys_toCart(). */
DAS_API bool das_vsys_fromCart(ubyte uSys, const double* pIn, double* pOut);

/* The trigonometric implementations, ALWAYS COMPILED even under SPICE=yes.
 *
 * Exported so a SPICE build can assert the two paths agree.  The risk in
 * having two implementations is not rounding, it is CONVENTION -- recsph_c
 * returns colatitude from +Z while reclat_c returns latitude, and the two
 * also disagree about which slot the longitude sits in.  A mismatch there is
 * a 90 degree error that surfaces in someone's plot months later, so the
 * fallback must stay testable rather than being compiled out. */
DAS_API bool das_vsys_toCartTrig(ubyte uSys, const double* pIn, double* pOut);
DAS_API bool das_vsys_fromCartTrig(ubyte uSys, const double* pIn, double* pOut);

#ifdef __cplusplus
}
#endif

#endif /* _das_form_vector_h_ */
