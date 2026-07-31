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

/** @file form_geoloc.h A body-centered position.  An affine point in 3-D.
 *
 * Spacecraft ephemeris, a ground station, a sub-satellite track.  What makes
 * it a geoloc rather than a vector is an ORIGIN -- the center body -- and
 * that single fact changes the algebra completely:
 *
 *     geoloc - geoloc  = vector      a displacement from one to the other
 *     geoloc + vector  = geoloc
 *     vector + geoloc  = geoloc      stated explicitly, never inferred
 *     geoloc - vector  = geoloc
 *     geoloc + geoloc  = REFUSED     adding two positions means nothing
 *     geoloc * anything = REFUSED    scaling a position without moving its
 *                                    origin is meaningless
 *
 * That is the SAME rule set as form_point.h, which governs calendar time.  A
 * datetime is a 1-D affine space whose origin rides in units (TT2000, US2000);
 * a geoloc is the 3-D case whose origin is named by center=.  Recognizing
 * they were one structure is what split this file out of the old "geovec".
 *
 * Parameters:
 *
 *   center=    a context reference naming the origin body.  REQUIRED; without
 *              it the values are free vectors and belong in form_vector.h.
 *   frame=     a CTX_FRAME reference.  Required in practice: a position has
 *              to be expressed in SOME frame.
 *   surface=   a CTX_SURFACE reference, the ellipsoid the detic and graphic
 *              systems measure against.
 *   system=    all six, including detic and graphic.
 *   sysorder=  storage slot to canonical direction.
 *
 * A FRAME'S body= AND A GEOLOC'S center= ARE DIFFERENT THINGS.  Cassini
 * relative to Saturn expressed in IAU_JUPITER: the frame's body is Jupiter,
 * the center is Saturn.  das3_spice already keeps them apart -- nOutCenter
 * goes to spkezp_c separately from aOutFrame -- so body= folds into the frame
 * entry and center= cannot.
 */

#ifndef _das_form_geoloc_h_
#define _das_form_geoloc_h_

#include <das2/form.h>
#include <das2/form_vector.h>

#ifdef __cplusplus
extern "C" {
#endif

/** The geoloc vtable, exported for identity comparison.  @memberof DasForm */
DAS_API extern const DasForm_VTbl das_form_geoloc_vtbl;

/** Is this form a body-centered position?  @memberof DasForm */
#define DasForm_isGeoLoc(P) ((P)->pVTbl == &das_form_geoloc_vtbl)

/** Build a geoloc formalism from resolved handles.
 *
 * @param uCenterId the origin body's context handle; 0 is refused, since a
 *        position without an origin is a free vector
 * @param uFrameId a CTX_FRAME handle
 * @param uSurfId a CTX_SURFACE handle, 0 when the system needs no ellipsoid
 * @param uSysType a DAS_VSYS_* code, any of the six
 * @param uDirs storage slot to canonical direction, packed with VEC_DIRS3
 * @returns a new form with one reference, or NULL on a loud error.
 * @memberof DasForm */
DAS_API DasForm* new_DasFormGeoLoc(
	ubyte uCenterId, ubyte uFrameId, ubyte uSurfId, ubyte uSysType, ubyte uDirs
);

/** The origin body these positions are measured from.
 * @returns a context handle, never 0 on a valid form.  @memberof DasForm */
DAS_API ubyte DasFormGeoLoc_centerId(const DasForm* pThis);

/** The frame the components are expressed in.  @memberof DasForm */
DAS_API ubyte DasFormGeoLoc_frameId(const DasForm* pThis);

/** The ellipsoid the detic and graphic systems measure against, or 0.
 * @memberof DasForm */
DAS_API ubyte DasFormGeoLoc_surfId(const DasForm* pThis);

/** The coordinate system, a DAS_VSYS_* code.  @memberof DasForm */
DAS_API ubyte DasFormGeoLoc_sysType(const DasForm* pThis);

/** Component order, VEC_DIRS3 packed.  @memberof DasForm */
DAS_API ubyte DasFormGeoLoc_dirs(const DasForm* pThis);

/** The display symbol for one storage slot: "φ", "θ", "a" ...
 * @memberof DasForm */
DAS_API const char* DasFormGeoLoc_slotSym(const DasForm* pThis, int iSlot);

/** Read a position datum's components as doubles, in STORAGE order.
 * @see DasFormVector_values, which this mirrors.  @memberof DasForm */
DAS_API int DasFormGeoLoc_values(
	const DasForm* pThis, const das_datum* pDm, double* pOut, int nMax
);

#ifdef __cplusplus
}
#endif

#endif /* _das_form_geoloc_h_ */
