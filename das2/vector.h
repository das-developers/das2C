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

/** Holds a geometric vector of some sort
 * 
 * This structure is tied in with DasFrame an is ment to hold one vector
 * from a frame, with components in the same order as provided in the 
 * backing DasAry that is managed by a DasVarVecAry.
 */
typedef struct das_geovec_t{

   /* The vector values if local */
   double comp[3];

   /* The ID of the vector frame, or 0 if unknown */
   byte   frame;  

   /* Frame type copied from Frame Descrptor */
   byte   ftype;

   /* the element value type, taken from das_val_type */
   byte   et;

   /* the size of each element, in bytes, copied in from das_vt_size */
   byte   esize; 

   /* Number of valid components */
   byte   ncomp;

   /* Direction for each component, storred in nibbles */ 
   byte   dirs[3];

} das_geovec;

DasErrCode das_geovec_init(
   das_geovec* pVec, const byte* pData, byte frame, byte ftype, 
   byte et, byte esize,  byte ncomp, const byte* pDirs
);

#define das_geovec_eltype(p) (p->vt & 0x0F)


#ifdef __cplusplus
}
#endif

#endif /* _vector_h_ */