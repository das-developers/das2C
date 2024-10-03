/* Copyright (C) 2024 Chris Piker <chris-piker@uiowa.edu>
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
 * version 2.1 along with Das2C; if not, see <http://www.gnu.org/licenses/>. 
 */

/** @file vector.h Geometric vectors, other vector types may be added */

#ifndef _vector_h_
#define _vector_h_

#include <das2/value.h>

#ifdef __cplusplus
extern "C" {
#endif

#define DAS_VSYS_TYPE_MASK 0x0000000F /* Leave room for 8 more */

/* Predefine these.  Users may use the upper 4 bits to define custom systems */
#define DAS_VSYS_UNKNOWN   0x00000000
#define DAS_VSYS_MIN       0x00000001

#define DAS_VSYS_CART      0x00000001 /* Almost always used for data values! */
#define DAS_VSYS_CYL       0x00000002
#define DAS_VSYS_SPH       0x00000003 /* ISO spherical using colatitude 0 = north pole */
#define DAS_VSYS_CENTRIC   0x00000004 /* Spherical, but with 90 = north pole */
#define DAS_VSYS_DETIC     0x00000005 /* Ellipsoidal, same angles as centric */
#define DAS_VSYS_GRAPHIC   0x00000006 /* Ellipsoidal, longitude reversed */

#define DAS_VSYS_MAX       0x00000006 /* Max known, not max possible */

/* The standard directions vary by coordinate system, need a token for each */


/** Convert a component system type ID to a string */
DAS_API const char* das_compsys_str(ubyte systype);

/** Convert a component system name to a type ID */
DAS_API ubyte das_compsys_id(const char* sSysType);

/** Get the description of a componet system type */
DAS_API const char* das_compsys_desc(ubyte systype);

/** Given the name of a component, get it's index in the standard 
 * right-handed triplet */
DAS_API int8_t das_compsys_index(ubyte systype, const char* sSymbol);

/** Given the index of a component in the standard right-handed triplet 
 *  get it's standard symbol.
 * 
 * The standard symbols vary by the vector system type, they are:
 *
 *    Cartesian:   (x, y, z)
 *    Cylendrical: (ρ, ϕ, z)
 *    Spherical:   (r, θ, ϕ)
 *    Centric:     (r, ϕ, θ)
 *    Detic:       (ϕ, θ, a)
 *    Graphic:     (ϕ, θ, a)
 * 
 * All arranged to generate a right-handed coordinate system.
 */
DAS_API const char* das_compsys_symbol(ubyte systype, int iIndex);

/** @addtogroup values
 * @{
 */

/** Holds a geometric vector of some sort
 * 
 * This structure is loosely tied in with DasFrame and is meant to hold one 
 * vector from a frame, in a defined system, with components in the same order
 * as provided in the backing DasAry that is managed by a DasVarVecAry.
 * 
 * @note Many data systems define a vector generically in the linear algebra
 * sense.  Though this is perfectably reasonable, in practice the data sets
 * that are normally seen by das systems almost always have regular old 3-space
 * vectors.  Thus we have a class customized for this very common case. 
 * 
 * Longer algebraic vectors would normally be handled as single runs of a
 * scalar index and wouldn't correspond to a das_datum compatible item.
 */
typedef struct das_geovec_t{

   /* The vector values if local */
   double comp[3];

   /* The ID of the vector frame, or 0 if unknown */
   ubyte   frame;  

   /* The system type. */
   ubyte systype;

   /* The surface ID if the coordinate system uses a non-standard
      surface */
   ubyte surfid; 

   /* the element value type, taken from das_val_type */
   ubyte   et;

   /* the size of each element, in bytes, copied in from das_vt_size */
   ubyte   esize; 

   /* Number of valid components */
   ubyte   ncomp;

   /* Direction for each component, 2 bits for each */ 
   ubyte   dirs;

   /* Unused, here for allignement */
   ubyte _spare;

} das_geovec;

/** @} */

#define VEC_DIRS1(a)       ((a)&0x3)
#define VEC_DIRS2(a,b)   ( ((a)&0x3) | (((b)<<2)&0xC) )
#define VEC_DIRS3(a,b,c) ( ((a)&0x3) | (((b)<<2)&0xC) | (((c)<<4)&0x30))

/** Initialize an memory area with information for a geometric vector
 * 
 * @param uSysType A coordinate system type ID, one of: 
 *        
 *        
 *        Since directions are mentioned separately, only 3-D versions
 *        of coordinate systems are identified.
 *
 * @memberof das_geovec 
 */
DAS_API DasErrCode das_geovec_init(
   das_geovec* pVec, const ubyte* pData, ubyte uFrameId, ubyte uSysType,
   ubyte uSurfaceId, ubyte et, ubyte esize, ubyte ncomp, ubyte dirs
);

/** Get the element type for this vector.  Can be anything that's not
 * a byte blob or text type
 * 
 * @memberof das_geovec
 */
#define das_geovec_eltype(p) ((p)->et & 0x0F)

/** Get the double value of a geo-vector in frame direction order.
 * 
 * The output is rearranged into the order supplied by das_geovec::dirs.
 * Thus if frame direction 2 was in storage location 0, then storage 
 * location 0 is converted to a double in placed in output location 2.
 * 
 * @param pVec A geo vector pointer.
 * 
 * @param pValues An array at least das_geovec::ncomp long.  Components are
 *   placed in the array in order of increasing directions.  
 * 
 * @returns DAS_OKAY if the geovec has a valid frame ID (aka not zero)
 *   or a positive error code otherwise.
 * 
 * @memberof das_geovec
 */
DAS_API DasErrCode das_geovec_values(das_geovec* pVec, double* pValues);


/** Set the coordinate system of the frame as a string
 * This is almost always the constant string "cartesian"
 * @memberof das_geovec
 */
#define das_geovec_setSys(P,S) ((P)->systype = S)

/** Get the type of the frame as a string
 * This is almost always the constant string "cartesian"
 * @memberof das_geovec
 */
#define das_geovec_sys(P) ((P)->systype)


#define das_geovec_hasRefSurf(P) ((P)->surfid != 0)

#define das_geovec_surfId(P) ((P)->systype)

#define das_geovec_numComp(P) ((P)->ndirs)

/** For a given data index get the vector component direction index
 * in the coordsys used here.
 * 
 * @memberof das_geovec
 */
DAS_API int das_geovec_dir(const das_geovec* pThis, int i);


/** Get the standard system component indexes as a function of the
 * address order of the internal components for this vector.
 * 
 * @param pDirs a pointer to at least three bytes to receive the values.
 * 
 * @returns the number of components defined for this vector.
 */
int das_geovec_dirs(const das_geovec* pThis, ubyte* pDirs);

/** Given the index of a component, return it's connonical symbol
 *
 * @param pThis The geovector structure in question
 * 
 * @param iIndex The component number, must be 0 to number of components - 1
 * 
 * @returns a very short string (4 bytes or less, including the null) Typical
 *        returns are "x", "y", "ϕ", "θ'", etc.
 * 
 * @memberof das_geovec
 */
DAS_API const char* das_geovec_compSym(const das_geovec* pThis, int iIndex);

/** Given the name of a frame direction, return it's index
 * 
 * @return A signed byte.  If the value is less then 0 then that direction is not defined
 * 
 * @memberof das_geovec
 */
DAS_API int8_t das_geovec_compIdx(const das_geovec* pThis, const char* sComp);


#ifdef __cplusplus
}
#endif

#endif /* _vector_h_ */
