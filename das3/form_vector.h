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
 * Contrast form_pos.h, which is the same components with an ORIGIN and is
 * therefore an affine point: positions subtract to give one of these, and two
 * of them cannot be added at all.  The discriminator is the origin, which is
 * why they are two formalisms and not one with a flag.
 *
 * `surface=` and `body=` aren't free vector concepts. A surface is a
 * reference surface and for a position a body is the origin; a free vector
 * has neither.
 *
 * A frame that no kernel defines can still be described by a property;
 * "frameInfo" is recommended for that purpose.
 *
 * Parameters:
 *
 *   frame=     the frame's NAME.  OPTIONAL: a frameless vector is legal, it
 *              just cannot be frame-checked against anything.
 *   system=    cartesian | cylindrical | spherical | centric.  Default
 *              cartesian, which is what nearly all measured data is.
 *   sysorder=  which canonical direction each stored component holds, e.g.
 *              "1;0;2".  Default ascending.
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

/* --- component systems --------------------------------------------------- *
 *
 * The six geometric systems, one vocabulary for vectors and positions alike.
 * The first four need no origin.  Detic and graphic are lat/lon/alt ON a
 * reference ellipsoid, so the systems that need a surface are exactly the
 * systems that need an origin, and only a position may use them.
 *
 * That last rule belongs to the FORMALISM, not to the vocabulary.  The
 * das_vsys_* lookups answer for all six, and new_DasFormVector() and the
 * vector system= parser refuse anything past DAS_VSYS_VEC_MAX themselves.  The
 * names and symbols are plain data that consumers -- das3_spice, das3_cdf, the
 * CDF reader -- read across all six without caring which form issued a code,
 * and splitting the lookups at the formalism line only made each of them pick
 * a family first.  form_pos.h includes this file, so position code needs only
 * that header.
 *
 * The ID SPACE IS SHARED with form_cplx.h, which continues it at 7.  Separate
 * per-formalism spaces would be tidier, but a ubyte would then only be
 * readable next to the form that issued it.  The das_vsys_* lookups answer
 * NULL or -1 for the complex codes.
 */

#define DAS_VSYS_TYPE_MASK 0x0000000F
#define DAS_VSYS_UNKNOWN   0x00000000
#define DAS_VSYS_MIN       0x00000001

#define DAS_VSYS_CART      0x00000001  /* x, y, z                            */
#define DAS_VSYS_CYL       0x00000002  /* rho, phi, z                        */
#define DAS_VSYS_SPH       0x00000003  /* r, colatitude from +Z, phi         */
#define DAS_VSYS_CENTRIC   0x00000004  /* r, phi, latitude from the equator  */
#define DAS_VSYS_DETIC     0x00000005  /* eastward lon, lat, alt  (Earth)    */
#define DAS_VSYS_GRAPHIC   0x00000006  /* WESTWARD lon, lat, alt             */

#define DAS_VSYS_VEC_MAX   0x00000004  /* the last one a free vector may use */
#define DAS_VSYS_MAX       0x00000006  /* the last geometric system          */

/** Wire token for any of the six system codes, NULL if unrecognized. */
DAS_API const char* das_vsys_str(ubyte uSys);

/** Wire token to system code across all six, DAS_VSYS_UNKNOWN if
 * unrecognized.  It knows detic and graphic, so code building a free vector
 * must still check the result against DAS_VSYS_VEC_MAX. */
DAS_API ubyte das_vsys_id(const char* sSys);

/** One line of prose about any of the six, for a "notes" property.  NULL if
 * unrecognized. */
DAS_API const char* das_vsys_desc(ubyte uSys);

/** The canonical symbol for direction iDir of a system: "x", "r", "λ", "h" ...
 * @returns a constant symbol, or NULL for a bad system or direction. */
DAS_API const char* das_vsys_symbol(ubyte uSys, int iDir);

/** The inverse of das_vsys_symbol(): which direction a symbol names.
 * @returns the direction index, or -1 if the symbol is not in the system. */
DAS_API int8_t das_vsys_index(ubyte uSys, const char* sSymbol);

/** The ASCII name for direction iDir of any of the six: "x", "rho", "theta",
 * "lambda", "h" ...  Handy when unicode symbols are a hazard.
 * @returns a constant name, or NULL for a bad system or direction. */
DAS_API const char* das_vsys_name(ubyte uSys, int iDir);

/** The inverse of das_vsys_name(), also accepting the symbols, so both
 * "theta" and the Greek letter give 1 for a spherical system.
 * @returns the direction index, or -1 if the token is not in the system. */
DAS_API int8_t das_vsys_nameIndex(ubyte uSys, const char* sName);

/** Is this an ellipsoidal system, i.e. one that requires a surface= ? */
#define das_vsys_isEllipsoidal(S) \
	(((S) == DAS_VSYS_DETIC)||((S) == DAS_VSYS_GRAPHIC))

/** The value an ABSENT component of this system defaults to.
 *
 * Zero everywhere except a radius, which defaults to 1 IN THE VARIABLE'S
 * UNITS.  Units always describe the radial component, even an implied one,
 * so an angles-only vector in degrees reads as a unit direction rather than a
 * null vector, and an angles-only position in R_E lies on the reference
 * sphere.  Detic and graphic have no radius, so every slot of theirs is zero
 * and a bare latitude and longitude lie on the reference surface. */
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
 * @param pDirs three entries; stored component i holds canonical direction
 *        pDirs[i].  NULL for
 *        ascending, which is what nearly all data is.  A stream sending fewer
 *        than three components still passes three here; how many are really
 *        used comes from the variable's internal shape.
 * @returns a new form with one reference, or NULL on a loud error.
 * @memberof DasForm */
DAS_API DasForm* new_DasFormVector(
	const char* sFrame, ubyte uSysType, const ubyte* pDirs
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

/** Component order: three entries, slot i holds canonical direction pDirs[i].
 * @returns a pointer into the form, good as long as the form is, or NULL if
 *          this is not a vector.  @memberof DasForm */
DAS_API const ubyte* DasFormVector_dirs(const DasForm* pThis);

/* For a component's display symbol -- "x", "λ", "θ" -- use DasVar_compSym(),
   which works for every kind of composite value and not just this one. */

/* To read a vector datum's components use das_datum_toDoubles().  It returns
   them in storage order and leaves any slot it does not fill untouched, so set
   those from das_vsys_default() above when a stream sends fewer components
   than its system defines. */


/* --- coordinate system conversion --------------------------------------- *
 *
 * Free functions rather than form methods, because form_pos.c needs them
 * for the systems it shares with this file.
 * Values are in CANONICAL direction order (not storage order) and angles are
 * in DEGREES.
 *
 * Under SPICE=yes these delegate to cspice so that every das tool agrees to
 * the last bit.  Otherwise they do the trigonometry directly, which is exact
 * for these four systems -- no kernel data is involved.
 */

/** Convert one vector from its system to cartesian.
 * @returns false loudly for a system with no kernel-free conversion
 *          (detic, graphic), which need a reference surface this call does
 *          not take. */
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
