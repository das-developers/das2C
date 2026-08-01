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
 *   body=      the ORIGIN body's name.  REQUIRED; without it the values are
 *              free vectors and belong in form_vector.h.  A frame also has a
 *              body -- what it is FIXED TO -- and the two differ for Cassini
 *              relative to Saturn expressed in IAU_JUPITER.  A position only
 *              ever needs the origin, so the name is spent on that.
 *   frame=     the frame's name.  Required in practice: a position has to be
 *              expressed in SOME frame.
 *   surface=   the ellipsoid the detic and graphic systems measure against.
 *   fixed=     true when the frame does not rotate.
 *   system=    all six, including detic and graphic.
 *   sysorder=  storage slot to canonical direction.
 *
 * A FRAME'S BODY AND A GEOLOC'S BODY ARE DIFFERENT THINGS.  Cassini
 * relative to Saturn expressed in IAU_JUPITER: the frame's body is Jupiter,
 * the center is Saturn.  das3_spice already keeps them apart -- nOutCenter
 * goes to spkezp_c separately from aOutFrame -- so they are two parameters and
 * always were, never one name doing double duty.
 *
 * All of these are plain strings resolved by nobody.  There is no registry to
 * check them against; whoever ACTS on a name is the one positioned to say
 * whether it is real.  See co_notes/libdas_context_removal.md.
 */

#ifndef _das_form_geoloc_h_
#define _das_form_geoloc_h_

#include <das2/form.h>
#include <das2/form_vector.h>

#ifdef __cplusplus
extern "C" {
#endif

/* --- component systems, the ellipsoidal half ----------------------------- *
 *
 * A surface is an ellipsoid reference, and detic/graphic are lat/lon/alt ON
 * that ellipsoid, so the systems that need a surface are exactly the systems
 * that need a center.  They are therefore geoloc's, and a free vector cannot
 * hold them -- form_vector.c refuses them by range.
 *
 * The codes CONTINUE form_vector.h's numbering rather than starting a second
 * space, so one ubyte reads the same next to either formalism and a consumer
 * like das3_spice can switch over all six in one statement.
 *
 * The das_geosys_* lookups below are the SUPERSET: they answer for all six by
 * handling these two and delegating the rest downward.  Call these when you
 * may be holding either kind of form; call das_vsys_* only when you know it
 * is a free vector.
 */

#define DAS_VSYS_DETIC     0x00000005  /* eastward lon, lat, alt  (Earth)    */
#define DAS_VSYS_GRAPHIC   0x00000006  /* WESTWARD lon, lat, alt             */

#define DAS_VSYS_MAX       0x00000006

/** Wire token for any of the six system codes, NULL if unrecognized. */
DAS_API const char* das_geosys_str(ubyte uSys);

/** Wire token to code across all six, 0 if unrecognized. */
DAS_API ubyte das_geosys_id(const char* sSys);

/** One line of prose about any of the six, for a "notes" property. */
DAS_API const char* das_geosys_desc(ubyte uSys);

/** The canonical symbol for direction iDir of any of the six. */
DAS_API const char* das_geosys_symbol(ubyte uSys, int iDir);

/** The inverse of das_geosys_symbol(), or -1 if the symbol is not in it. */
DAS_API int8_t das_geosys_index(ubyte uSys, const char* sSymbol);

/** Is this an ellipsoidal system, i.e. one that requires a surface= ? */
#define das_geosys_isEllipsoidal(S) \
	(((S) == DAS_VSYS_DETIC)||((S) == DAS_VSYS_GRAPHIC))

/** The geoloc vtable, exported for identity comparison.  @memberof DasForm */
DAS_API extern const DasForm_VTbl das_form_geoloc_vtbl;

/** Is this form a body-centered position?  @memberof DasForm */
#define DasForm_isGeoLoc(P) ((P)->pVTbl == &das_form_geoloc_vtbl)

/** Build a geoloc formalism from resolved handles.
 *
 * @param sBody the origin body's name; NULL or "" is refused, since a
 *        position without an origin is a free vector
 * @param sFrame the frame name, NULL when unframed
 * @param sSurface the ellipsoid's name, NULL when the system needs none
 * @param uSysType a DAS_VSYS_* code, any of the six
 * @param uDirs storage slot to canonical direction, packed with VEC_DIRS3
 * @returns a new form with one reference, or NULL on a loud error.
 * @memberof DasForm */
DAS_API DasForm* new_DasFormGeoLoc(
	const char* sBody, const char* sFrame, const char* sSurface,
	ubyte uSysType, ubyte uDirs
);

/** The origin body these positions are measured from.  Wire name: body=
 * @returns the body's name, never NULL on a valid form.  @memberof DasForm */
DAS_API const char* DasFormGeoLoc_body(const DasForm* pThis);

/** The frame the components are expressed in.  @memberof DasForm */
DAS_API const char* DasFormGeoLoc_frame(const DasForm* pThis);

/** The ellipsoid the detic and graphic systems measure against, NULL if none.
 * @memberof DasForm */
DAS_API const char* DasFormGeoLoc_surface(const DasForm* pThis);

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
