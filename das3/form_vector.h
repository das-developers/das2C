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
 * why they are two formalisms and not one with a flag.
 *
 * `surface=` and `center=` are ILLEGAL here and say so loudly.  A surface is
 * an ellipsoid reference and a center is an origin; a free vector has neither,
 * so the systems needing them (detic, graphic) belong to geoloc.
 *
 * Parameters:
 *
 *   frame=     the frame's NAME.  OPTIONAL: a frameless vector is legal, it
 *              just cannot be frame-checked against anything.
 *   body=      describes the FRAME, and rides here because there is no frame
 *              object to fold it into.  Two vectors in one frame can therefore
 *              disagree; the library carries both and the consumer decides.
 *   fixed=     true when the frame does not rotate.
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

#include <das3/form.h>

#ifdef __cplusplus
extern "C" {
#endif

/* --- component systems, the free-vector half ----------------------------- *
 *
 * The system vocabulary belongs to the formalisms that use it, not to a
 * central table, so that a new formalism can bring its own systems without
 * editing shared code.  This file owns the four that need no origin; the two
 * ellipsoidal ones are measured ON a body and live in form_geoloc.h.
 *
 * The ID SPACE IS CONTINUOUS ACROSS THE TWO FILES (geoloc picks up at 5) and
 * that is deliberate.  Separate per-formalism spaces would be tidier, but a
 * ubyte would then only be readable next to the form that issued it, and the
 * consumers -- das3_spice above all -- switch over all six in one statement.
 * One space costs geoloc a downward include it already has.
 */

#define DAS_VSYS_TYPE_MASK 0x0000000F
#define DAS_VSYS_UNKNOWN   0x00000000
#define DAS_VSYS_MIN       0x00000001

#define DAS_VSYS_CART      0x00000001  /* x, y, z                            */
#define DAS_VSYS_CYL       0x00000002  /* rho, phi, z                        */
#define DAS_VSYS_SPH       0x00000003  /* r, colatitude from +Z, phi         */
#define DAS_VSYS_CENTRIC   0x00000004  /* r, phi, latitude from the equator  */

#define DAS_VSYS_VEC_MAX   0x00000004  /* the last one a free vector may use */

/** Component order: storage slot to canonical direction, two bits each. */
#define VEC_DIRS1(a)       ((a)&0x3)
#define VEC_DIRS2(a,b)   ( ((a)&0x3) | (((b)<<2)&0xC) )
#define VEC_DIRS3(a,b,c) ( ((a)&0x3) | (((b)<<2)&0xC) | (((c)<<4)&0x30))

/** Wire token for a system code, or NULL if it is not one of this file's.
 * form_geoloc.h has das_geosys_str() for the superset. */
DAS_API const char* das_vsys_str(ubyte uSys);

/** Wire token to system code, 0 if unrecognized here. */
DAS_API ubyte das_vsys_id(const char* sSys);

/** One line of prose about a system, for a "notes" property.  NULL if not
 * one of this file's. */
DAS_API const char* das_vsys_desc(ubyte uSys);

/** The canonical symbol for direction iDir of a system: "x", "r", "λ" ...
 * @returns a constant symbol, or NULL for a bad system or direction. */
DAS_API const char* das_vsys_symbol(ubyte uSys, int iDir);

/** The inverse of das_vsys_symbol(): which direction a symbol names.
 * @returns the direction index, or -1 if the symbol is not in the system. */
DAS_API int8_t das_vsys_index(ubyte uSys, const char* sSymbol);

/** The value an ABSENT component of this system defaults to.
 *
 * Zero everywhere except a radius, which defaults to 1 so an angles-only
 * vector reads as a unit direction rather than as a null vector.  Positions
 * want different defaults; see form_geoloc.h. */
DAS_API double das_vsys_default(ubyte uSys, int iDir);

/* Exported so that its address can be compared.  Client code uses the
   DAS_FORM_VEC macro below and has no reason to name this directly. */
DAS_API extern const DasForm_VTbl das_form_vector_vtbl;

/** A geometric vector in a reference frame.  It has a direction and a
 * magnitude but no starting point, so two of them add.
 * @see DasForm_isKind(), DasVar_formIs().  @relates DasForm */
#define DAS_FORM_VEC (&das_form_vector_vtbl)

/** Build a vector formalism from resolved handles.
 *
 * @param sFrame the frame name, NULL or "" for a frameless vector
 * @param uSysType a DAS_VSYS_* code; the ellipsoidal ones are refused
 * @param uDirs storage slot to canonical direction, packed with VEC_DIRS3
 * @returns a new form with one reference, or NULL on a loud error.
 * @memberof DasForm */
DAS_API DasForm* new_DasFormVector(
	const char* sFrame, ubyte uSysType, ubyte uDirs
);

/** The frame these components are expressed in, NULL when frameless.
 *
 * A plain name, resolved by nobody: there is no frame registry to look it up
 * in and none is wanted.  Whoever acts on a frame -- das3_spice handing it to
 * namfrm_c -- is the one positioned to say whether it is real.
 * @memberof DasForm */
DAS_API const char* DasFormVector_frame(const DasForm* pThis);

/** The coordinate system, a DAS_VSYS_* code.  @memberof DasForm */
DAS_API ubyte DasFormVector_sysType(const DasForm* pThis);

/** Component order: storage slot to canonical direction, VEC_DIRS3 packed.
 * Slot i holds direction (uDirs >> 2*i) & 0x3.  @memberof DasForm */
DAS_API ubyte DasFormVector_dirs(const DasForm* pThis);

/** The display symbol for one storage slot: "x", "λ", "θ" ...
 *
 * What a client needs to label N stacked component lines without
 * understanding the formalism.  This replaces the retired DasVar_vecMap()
 * and das_makeCompLabels(): the library hands out the symbol, the client
 * composes whatever label it wants.
 *
 * @returns a constant symbol, or NULL for a bad slot or a non-vector form.
 * @memberof DasForm */
DAS_API const char* DasFormVector_slotSym(const DasForm* pThis, int iSlot);

/* To read a vector datum's components use das_datum_toDoubles().  It returns
   them in storage order and leaves any slot it does not fill untouched, so set
   those from das_vsys_default() above when a stream sends fewer components
   than its system defines. */


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
