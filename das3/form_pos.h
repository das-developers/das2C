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

/** @file form_pos.h A body-centered position.  An affine point in 3-D.
 *
 * Spacecraft ephemeris, a ground station, a sub-satellite track.  What makes
 * it a position rather than a vector is an ORIGIN -- the center body -- and
 * that single fact changes the algebra completely:
 *
 *     position - position = vector     a displacement from one to the other
 *     position + vector   = position
 *     vector + position   = position   stated explicitly, never inferred
 *     position - vector   = position
 *     position + position = REFUSED    adding two positions means nothing
 *     position * anything = REFUSED    scaling a position without moving its
 *                                      origin is meaningless
 *
 * That is the SAME rule set as form_point.h, which governs calendar time.  A
 * datetime is a 1-D affine space whose origin rides in units (TT2000, US2000);
 * a position is the 3-D case whose origin is named by body=.
 *
 * Parameters:
 *
 *   body=      the origin body's name, where zero is.  This is a defining
 *              feature of a position, without it values are free vectors and
 *              belong in form_vector.h.
 *   frame=     the frame's name, which way the axes point.  Required in
 *              practice: a position has to be expressed in SOME frame.  The
 *              frame need not be attached to body=: Cassini relative to Titan
 *              in IAU_SATURN is body=TITAN frame=IAU_SATURN.
 *   surface=   the ellipsoid the detic and graphic systems measure against.
 *   fixed=     true when the frame is fixed to body= and rotates with it, in
 *              the SPICE sense of a body-fixed frame.  So IAU_SATURN is fixed
 *              for body=SATURN and not for body=TITAN.  In a frame that is not
 *              fixed, the reference surface rotates beneath the point, and
 *              h is the height above whatever surface point lies below it at
 *              that instant.
 *   system=    all six, including detic and graphic.
 *   sysorder=  which canonical direction each stored component holds.
 *
 * Detic and graphic do *not* share a component order.  Detic is
 * (lon, lat, height) and graphic is (lat, lon, height).  Graphic longitude runs
 * west, and since every component order in das2C is right handed, latitude has
 * to come first to keep it that way.
 *
 * Graphic means west-positive here.  Where a body's planetographic longitude
 * runs east the system is the same as detic and should be spelled that way;
 * there is no east-positive spelling of graphic.
 *
 * THE ORIGIN AND THE FRAME ARE INDEPENDENT.  Cassini relative to Saturn
 * expressed in IAU_JUPITER is body=SATURN frame=IAU_JUPITER fixed=false: zero
 * is at Saturn, the axes point the way Jupiter's do.  das3_spice keeps the
 * two apart in the same way -- nOutCenter goes to spkezp_c separately from
 * aOutFrame.  The body a frame is attached to (SPICE's frame center) is part
 * of the frame's definition and is not carried here.
 *
 * All of these are plain strings resolved by applications, das2C has no
 * frame or body registry, but SPICE does if you need to check.
 */

#ifndef _das_form_pos_h_
#define _das_form_pos_h_

#include <das3/form.h>
#include <das3/form_vector.h>

#ifdef __cplusplus
extern "C" {
#endif

/* --- component systems, the ellipsoidal half ----------------------------- *
 *
 * A surface is an ellipsoid reference, and detic/graphic are lat/lon/alt ON
 * that ellipsoid, so the systems that need a surface are exactly the systems
 * that need an origin.  They therefore belong to positions, and a free vector
 * cannot hold them -- form_vector.c refuses them by range.
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

/** The value an ABSENT component of any of the six defaults to.
 *
 * Zero for every slot of detic and graphic, The other four answer
 * as das_vsys_default() does. */
DAS_API double das_geosys_default(ubyte uSys, int iDir);

/** The canonical symbol for direction iDir of any of the six. */
DAS_API const char* das_geosys_symbol(ubyte uSys, int iDir);

/** The inverse of das_geosys_symbol(), or -1 if the symbol is not in it. */
DAS_API int8_t das_geosys_index(ubyte uSys, const char* sSymbol);

/** The ASCII name for direction iDir of any of the six: "x", "rho", "theta",
 * "lambda", "h" ...  Handy when unicode symbols are a hazard.
 * @returns a constant name, or NULL for a bad system or direction. */
DAS_API const char* das_geosys_name(ubyte uSys, int iDir);

/** The inverse of das_geosys_name(), also accepting the symbols, so both
 * "theta" and the Greek letter give 1 for a spherical system.
 * @returns the direction index, or -1 if the token is not in the system. */
DAS_API int8_t das_geosys_nameIndex(ubyte uSys, const char* sName);

/** Is this an ellipsoidal system, i.e. one that requires a surface= ? */
#define das_geosys_isEllipsoidal(S) \
	(((S) == DAS_VSYS_DETIC)||((S) == DAS_VSYS_GRAPHIC))

/* Exported so that its address can be compared.  Client code uses the
   DAS_FORM_POS macro below and has no reason to name this directly. */
DAS_API extern const DasForm_VTbl das_form_pos_vtbl;

/** A position measured from the center of some body.  Two of these subtract to
 * give a vector, but they do not add.
 * @see DasForm_isKind(), DasVar_formIs().  @relates DasForm */
#define DAS_FORM_POS (&das_form_pos_vtbl)

/** Build a position formalism from resolved handles.
 *
 * @param sBody the origin body's name; NULL or "" is refused, since a
 *        position without an origin is a free vector
 * @param sFrame the frame name, NULL when unframed
 * @param sSurface the ellipsoid's name, NULL when the system needs none
 * @param bFixed true if the frame is body-fixed in the SPICE sense
 * @param uSysType a DAS_VSYS_* code, any of the six
 * @param pDirs three entries; stored component i holds canonical direction
 *        pDirs[i].  NULL for ascending
 * @returns a new form with one reference, or NULL on a loud error.
 * @memberof DasForm */
DAS_API DasForm* new_DasFormPos(
	const char* sBody, const char* sFrame, const char* sSurface,
	bool bFixed, ubyte uSysType, const ubyte* pDirs
);

/** The origin body these positions are measured from.  Wire name: body=
 * @returns the body's name, never NULL on a valid form.  @memberof DasForm */
DAS_API const char* DasFormPos_body(const DasForm* pThis);

/** The frame the components are expressed in.  @memberof DasForm */
DAS_API const char* DasFormPos_frame(const DasForm* pThis);

/** The ellipsoid the detic and graphic systems measure against, NULL if none.
 * @memberof DasForm */
DAS_API const char* DasFormPos_surface(const DasForm* pThis);

/** The coordinate system, a DAS_VSYS_* code.  @memberof DasForm */
DAS_API ubyte DasFormPos_sysType(const DasForm* pThis);

/** True if the frame is body-fixed in the SPICE sense.
 * @memberof DasForm */
DAS_API bool DasFormPos_fixed(const DasForm* pThis);

/** Component order: three entries, slot i holds canonical direction pDirs[i].
 * @returns a pointer into the form, or NULL if not a position formalism.
 * @memberof DasForm */
DAS_API const ubyte* DasFormPos_dirs(const DasForm* pThis);

/* For a component's display symbol -- "λ", "φ", "h" -- use DasVar_compSym(),
   which works for every kind of composite value and not just this one. */

/* To read a position datum's components use das_datum_toDoubles().  It returns
   them in storage order and leaves any slot it does not fill untouched, so set
   those from das_geosys_default() above when a stream sends fewer components
   than its system defines. */

#ifdef __cplusplus
}
#endif

#endif /* _das_form_pos_h_ */
