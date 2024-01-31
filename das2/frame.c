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

#define _POSIX_C_SOURCE 200112L

#include <string.h>
#ifdef _WIN32
#define strcasecmp _stricmp
#define strncasecmp _strnicmp
#else
#include <strings.h>
#endif

#include "frame.h"

DasFrame* new_DasFrame(DasDesc* pParent, byte id, const char* sName, const char* sType)
{
   DasFrame* pThis = (DasFrame*) calloc(1, sizeof(DasFrame));
   DasDesc_init(&(pThis->base), FRAME);
   
   if(sName != NULL) strncpy(pThis->name, sName, DASFRM_NAME_SZ-1);

   if(id == 0){
      das_error(DASERR_FRM, "Frame IDs must be in the range 1 to 255");
      goto ERROR;
   }
   
   if( DasFrame_setType(pThis, sType) != DAS_OKAY)
      goto ERROR;
   
   if( DasFrame_setName(pThis, sName) != DAS_OKAY)
      goto ERROR;

   return pThis;
ERROR:
   free(pThis);
   return NULL;
}

void DasFrame_inertial(DasFrame* pThis, bool bInertial)
{
   if(bInertial)
      pThis->flags &= DASFRM_INERTIAL;
   else
      pThis->flags &= ~DASFRM_INERTIAL;
}

DasErrCode DasFrame_setName(DasFrame* pThis, const char* sName)
{
   if((sName == NULL)||(sName[0] == '\0'))
      return das_error(DASERR_FRM, "Null or empty name string");

   strncpy(pThis->name, sName, DASFRM_NAME_SZ-1);
   return DAS_OKAY;
}

DAS_API DasErrCode DasFrame_setType(DasFrame* pThis, const char* sType)
{
   if((sType == NULL)||(sType[0] == '\0'))
      return das_error(DASERR_FRM, "Empty coordinate frame type");

   strncpy(pThis->type, sType, DASFRM_TYPE_SZ-1);
   if( strcasecmp(pThis->type, "cartesian") == 0)
      pThis->flags |= DASFRM_CARTESIAN;
   else if( strcasecmp(pThis->type, "polar") == 0)
      pThis->flags |= DASFRM_POLAR;
   else if( strcasecmp(pThis->type, "sphere_surface") == 0)
      pThis->flags |= DASFRM_SPHERE_SURFACE;
   else if( strcasecmp(pThis->type, "cylindrical") == 0)
      pThis->flags |= DASFRM_CYLINDRICAL;
   else if( strcasecmp(pThis->type, "spherical") == 0)
      pThis->flags |= DASFRM_CYLINDRICAL; 

   return DAS_OKAY;
}

DAS_API byte DasFrame_getType(const DasFrame* pThis){
   return (pThis->flags & DASFRM_TYPE_MASK);
}

DasErrCode DasFrame_addDir(DasFrame* pThis, const char* sDir)
{
   if(pThis->ndirs >= DASFRM_MAX_DIRS)
      return das_error(DASERR_FRM, 
         "Only %d coordinate directions supported without a recompile.",
         DASFRM_MAX_DIRS
      );

   // Make sure we don't already have one with that name
   for(int8_t i = 0; i < pThis->ndirs; ++i){
      if(strcasecmp(sDir, pThis->dirs[pThis->ndirs]) == 0)
         return das_error(DASERR_FRM, "Directory %s already defined for frame %s",
            sDir, pThis->name
         );
   }  

   strncpy(pThis->dirs[pThis->ndirs], sDir, DASFRM_DNAM_SZ-1);
   pThis->ndirs += 1;
   return DAS_OKAY;
}

const char* DasFrame_dirByIdx(const DasFrame* pThis, int iIndex)
{
   if(iIndex >= pThis->ndirs){
      das_error(DASERR_FRM, "No coordinate direction defined at index %d", iIndex);
      return NULL;
   }
   return pThis->dirs[iIndex];
}

int8_t DasFrame_idxByDir(const DasFrame* pThis, const char* sDir)
{
   for(int8_t i = 0; i < pThis->ndirs; ++i){
      if(strcasecmp(sDir, pThis->dirs[pThis->ndirs]) == 0){
         return i;
      }
   }

   return -1 * das_error(DASERR_FRM, "Direction %s not defined for frame %s",
      sDir, pThis->name
   );
}

void del_DasFrame(DasFrame* pThis)
{
   // We have no sub-pointers, so this is just a type safe wrapper on free
   if(pThis != NULL) 
      free(pThis);
}
