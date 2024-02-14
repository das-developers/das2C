/* Copyright (C) 2024  Chris Piker <chris-piker@uiowa.edu>
 *
 * This file used to be named parsetime.c.  It is part of das2C, the Core
 * Das2 C Library.
 * 
 * das2C is free software; you can redistribute it and/or modify it under
 * the terms of the GNU Lesser General Public License version 2.1 as published
 * by the Free Software Foundation.
 *
 * das2C is distributed in the hope that it will be useful, but WITHOUT ANY
 * WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS
 * FOR A PARTICULAR PURPOSE.  See the GNU Lesser General Public License for
 * more details.
 *
 * You should have received a copy of the GNU Lesser General Public License
 * version 2.1 along with libdas2; if not, see <http://www.gnu.org/licenses/>. 
 */

#define _POSIX_C_SOURCE 200112L

#include "vector.h"


DasErrCode das_geovec_init(
   das_geovec* pVec, const ubyte* pData, ubyte frame, ubyte ftype, 
   ubyte et, ubyte esize,  ubyte ncomp, const ubyte* pDirs
){

   pVec->frame = frame;
   pVec->ftype = ftype;
   pVec->esize = das_vt_size(et);
   pVec->ncomp = ncomp;

   if((ncomp < 1)||(ncomp > 3))
      return das_error(DASERR_VEC, "Geometric vectors must have 1 to 3 components");
      
   /* Set the data */
   switch(et){
   case vtByte:
   case vtUByte:
      for(int i = 0; (i < ncomp)&&(i<3); ++i){
         ((ubyte*)(pVec->comp))[i] = pData[i];
         pVec->dirs[i] = pDirs[i];
      }
      break;
	case vtShort:
   case vtUShort:
      for(int i = 0; (i < ncomp)&&(i<3); ++i){
         ((uint16_t*)(pVec->comp))[i] = ((uint16_t*)pData)[i];
         pVec->dirs[i] = pDirs[i];
      }
      break;
	case vtUint:
   case vtInt:
      for(int i = 0; (i < ncomp)&&(i<3); ++i){
         ((uint32_t*)(pVec->comp))[i] = ((uint32_t*)pData)[i];
         pVec->dirs[i] = pDirs[i];
      }
      break;
	case vtULong:
   case vtLong:
      for(int i = 0; (i < ncomp)&&(i<3); ++i){
         ((uint64_t*)(pVec->comp))[i] = ((uint64_t*)pData)[i];
         pVec->dirs[i] = pDirs[i];
      }
      break;
   case vtFloat:
      for(int i = 0; (i < ncomp)&&(i<3); ++i){
         ((float*)(pVec->comp))[i] = ((float*)pData)[i];
         pVec->dirs[i] = pDirs[i];
      }
      break;
   case vtDouble:
      for(int i = 0; (i < ncomp)&&(i<3); ++i){
         ((double*)(pVec->comp))[i] = ((double*)pData)[i];
         pVec->dirs[i] = pDirs[i];
      }
      break;
   default:
      return das_error(DASERR_VEC, "Invalid element type for vector %d", et);
   }
   pVec->et = et;

   return DAS_OKAY;
}
