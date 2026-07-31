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

/** @file form_geovec.h Geometric vectors: a run with a frame and a system.
 *
 * The name is "geovec" and not "vector" on purpose.  A plain numeric run with
 * no frame is already a free vector and its formalism is LINEAR -- see
 * form_linear.h, where elementwise math lives.  What this formalism adds is
 * the reference-frame baggage: which frame the components are expressed in,
 * which coordinate system, which surface for the ellipsoidal systems, and what
 * order the components sit in.  Everything here exists because of that
 * baggage; the arithmetic would otherwise be linear's.
 *
 * Parameters, all of them:
 *
 *   frame=     a CTX_FRAME reference.  Resolved to a handle HERE.
 *   body=      describes the FRAME, not this vector, so it is folded into the
 *              frame's context entry and never stored on the form.  Two
 *              variables in one frame then cannot disagree about the body.
 *   surface=   a CTX_SURFACE reference, for the ellipsoidal systems.
 *   system=    cartesian, spherical, centric, detic, graphic ...  Default
 *              cartesian, which is what nearly all measured data is.
 *   sysorder=  storage slot to canonical direction, e.g. "1;0;2".  Default
 *              ascending.
 *
 * ARITHMETIC REQUIRES CARTESIAN COMPONENTS.  Adding two spherical vectors
 * componentwise is simply wrong -- angles do not add -- and there is no
 * silent conversion, so a non-cartesian operand is refused loudly.  That is
 * the whole reason a vector's system rides with it instead of being assumed.
 *
 * WHO INCLUDES THIS: a peer formalism that acts on vectors (form_rot.c), or a
 * client that understands them (das3_csv synthesizing component labels).  Not
 * reachable through core.h, and generic library code needs nothing here.
 */

#ifndef _das_form_geovec_h_
#define _das_form_geovec_h_

#include <das2/form.h>
#include <das2/geovec.h>

#ifdef __cplusplus
extern "C" {
#endif

/** The geovec vtable, exported for identity comparison.  @memberof DasForm */
DAS_API extern const DasForm_VTbl das_form_geovec_vtbl;

/** Is this form a geometric vector?  @memberof DasForm */
#define DasForm_isGeoVec(P) ((P)->pVTbl == &das_form_geovec_vtbl)

/** Build a geovec formalism from resolved handles.
 *
 * For code that already holds them -- das3_spice producing a rotated vector,
 * or another formalism naming its result.  A reader goes through
 * das_form_fromStr("geovec") and lets setParam resolve the tokens.
 *
 * @param uFrameId a CTX_FRAME handle, 0 for a frameless vector
 * @param uSurfId a CTX_SURFACE handle, 0 for none
 * @param uSysType a DAS_VSYS_* code
 * @param uDirs storage slot to canonical direction, packed with VEC_DIRS3
 * @returns a new form with one reference, or NULL on a loud error.
 * @memberof DasForm */
DAS_API DasForm* new_DasFormGeoVec(
	ubyte uFrameId, ubyte uSurfId, ubyte uSysType, ubyte uDirs
);

/** The frame these components are expressed in.
 * @returns a CTX_FRAME handle, or 0 when frameless.  Fails loud if the form is
 *          not a geovec.  @memberof DasForm */
DAS_API ubyte DasFormGeoVec_frameId(const DasForm* pThis);

/** The surface, for the ellipsoidal systems.
 * @returns a CTX_SURFACE handle, or 0 for none.  @memberof DasForm */
DAS_API ubyte DasFormGeoVec_surfId(const DasForm* pThis);

/** The coordinate system, a DAS_VSYS_* code.  @memberof DasForm */
DAS_API ubyte DasFormGeoVec_sysType(const DasForm* pThis);

/** The component order: storage slot to canonical direction, VEC_DIRS3 packed.
 *
 * Slot i of the stored run holds canonical direction (uDirs >> 2*i) & 0x3.
 * @memberof DasForm */
DAS_API ubyte DasFormGeoVec_dirs(const DasForm* pThis);

/** The display symbol for one storage slot: "x", "lat", "r" ...
 *
 * What a client needs to label N stacked component lines without
 * understanding the formalism.  This is the supported replacement for the
 * retired DasSet_vecMap() and das_makeCompLabels(): the library hands out the
 * symbol, the client composes the label it wants.
 *
 * @param iSlot the storage slot, 0 based
 * @returns a constant symbol, or NULL if the slot is out of range or the form
 *          is not a geovec.  @memberof DasForm */
DAS_API const char* DasFormGeoVec_slotSym(const DasForm* pThis, int iSlot);

#ifdef __cplusplus
}
#endif

#endif /* _das_form_geovec_h_ */
