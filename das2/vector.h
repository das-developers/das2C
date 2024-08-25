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

#define DASVEC_MAXCOMP 3

/** @addtogroup values
 * @{
 */

/** Holds a geometric vector of some sort
 * 
 * This structure is tied in with DasFrame an is ment to hold one vector
 * from a frame, with components in the same order as provided in the 
 * backing DasAry that is managed by a DasVarVecAry.
 * 
 * @note Many data systems define a vector generically in the linear algebra
 * sense.  Though this is perfectably reasonable, in practice the data sets
 * that are normally seen by das systems almost always have regular old 3-space
 * vectors.  Thus we have a class customized for this very common case. 
 * 
 * Longer algebraic vectors would normally be handled as single runs of a
 * scalar index and wouldn't correspond to a das_datum compatible item.
 * 
 */
typedef struct das_geovec_t{

   /* The vector values if local */
   double comp[DASVEC_MAXCOMP];

   /* The ID of the vector frame, or 0 if unknown */
   ubyte   frame;  

   /* Frame type copied from Frame Descrptor */
   ubyte   ftype;

   /* the element value type, taken from das_val_type */
   ubyte   et;

   /* the size of each element, in bytes, copied in from das_vt_size */
   ubyte   esize; 

   /* Number of valid components */
   ubyte   ncomp;

   /* Direction for each component, storred in nibbles */ 
   ubyte   dirs[DASVEC_MAXCOMP];

} das_geovec;

/** @} */

/** Initialize an memory area with information for a geometric vector
 * 
 * @memberof das_geovec 
 */
DasErrCode das_geovec_init(
   das_geovec* pVec, const ubyte* pData, ubyte frame, ubyte ftype, 
   ubyte et, ubyte esize,  ubyte ncomp, const ubyte* pDirs
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
DasErrCode das_geovec_values(das_geovec* pVec, double* pValues);


#ifdef __cplusplus
}
#endif

#endif /* _vector_h_ */
