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

#include "log.h"
#include "frame.h"


const char* das_frametype2str( ubyte uFT)
{
   ubyte ft = uFT & DASFRM_TYPE_MASK;

   if(ft == DASFRM_CARTESIAN     ) return "cartesian";
   if(ft == DASFRM_POLAR         ) return "polar";
   if(ft == DASFRM_SPHERE_SURFACE) return "sphere_surface";
   if(ft == DASFRM_CYLINDRICAL   ) return "cylindrical";
   if(ft == DASFRM_CYLINDRICAL   ) return "spherical";

   daslog_error_v("Unknown vector or coordinate frame type id: '%hhu'.", uFT);
   return "";   
}

ubyte das_str2frametype(const char* sFT)
{
   if( strcasecmp(sFT, "cartesian") == 0) return DASFRM_CARTESIAN;
   if( strcasecmp(sFT, "polar") == 0)     return DASFRM_POLAR;
   if( strcasecmp(sFT, "sphere_surface") == 0) return DASFRM_SPHERE_SURFACE;
   if( strcasecmp(sFT, "cylindrical") == 0) return DASFRM_CYLINDRICAL;
   if( strcasecmp(sFT, "spherical") == 0) return DASFRM_CYLINDRICAL;

   daslog_error_v("Unknown vector or coordinate frame type: '%s'.", sFT);
   return 0;
}

/* ************************************************************************ */

DasFrame* new_DasFrame(DasDesc* pParent, ubyte id, const char* sName, const char* sType)
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

DasFrame* copy_DasFrame(const DasFrame* pThis){

   DasFrame* pCopy = (DasFrame*) calloc(1, sizeof(DasFrame));
   DasDesc_init(&(pCopy->base), FRAME);
   DasDesc_copyIn((DasDesc*) pCopy, (const DasDesc*)pThis);

   pCopy->id     = pThis->id;
   pCopy->flags  = pThis->flags;
   pCopy->ndirs  = pThis->ndirs;
   pCopy->pUser  = pThis->pUser;
   memcpy(pCopy->name, pThis->name, DASFRM_NAME_SZ);
   memcpy(pCopy->type, pThis->type, DASFRM_TYPE_SZ);
   memcpy(pCopy->dirs, pThis->dirs, DASFRM_MAX_DIRS * DASFRM_DNAM_SZ);

   return pCopy;
}

char* DasFrame_info(const DasFrame* pThis, char* sBuf, int nLen)
{
	if(nLen < 30)
		return sBuf;

	char* pWrite = sBuf;

	int nWritten = snprintf(pWrite, nLen - 1, "\n   Vector Frame %02hhu: %s |", 
		pThis->id, pThis->name
	);

	pWrite += nWritten;
	nLen -= nWritten;

	/* The directions */
	for(uint32_t u = 0; u < pThis->ndirs; ++u){
		if(nLen < 40) return pWrite;

		if(u > 0){
			*pWrite = ','; ++pWrite; *pWrite = ' '; ++pWrite;
			nLen -= 2;
		}
		else{
			*pWrite = ' '; ++pWrite; --nLen;
		}
		strncpy(pWrite, pThis->dirs[u], 40);
		nWritten = strlen(pThis->dirs[u]);
		pWrite += nWritten; nLen -= nWritten;
	}

	/* Type and inertial */
	if(nLen < 40) return pWrite;
	switch(pThis->flags & DASFRM_TYPE_MASK){
	case DASFRM_CARTESIAN     : nWritten = snprintf(pWrite, nLen - 1, " | cartesian"); break;
	case DASFRM_POLAR         : nWritten = snprintf(pWrite, nLen - 1, " | polar"); break;
	case DASFRM_SPHERE_SURFACE: nWritten = snprintf(pWrite, nLen - 1, " | sphere_surface"); break;
	case DASFRM_CYLINDRICAL   : nWritten = snprintf(pWrite, nLen - 1, " | cylindrical"); break;
	case DASFRM_SPHERICAL     : nWritten = snprintf(pWrite, nLen - 1, " | spherical"); break;
	default: nWritten = 0;
	}

	pWrite += nWritten; nLen -= nWritten;
	if(nLen < 40) return pWrite;

	if(pThis->flags & DASFRM_INERTIAL)
		nWritten = snprintf(pWrite, nLen - 1, " (inertial)\n");
	else
		nWritten = snprintf(pWrite, nLen - 1, " (non-inertial)\n");
	
	pWrite += nWritten; nLen -= nWritten;
	if(nLen < 40) return pWrite;

	char* pSubWrite = DasDesc_info((DasDesc*)pThis, pWrite, nLen, "      ");
	nLen -= (pSubWrite - pWrite);
	pWrite = pSubWrite;

	if(nLen > 4){
		nWritten = snprintf(pWrite, nLen-1, "\n");
		pWrite += nWritten; nLen -= nWritten;
	}
	return pWrite;
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
   pThis->flags |= das_str2frametype(pThis->type);

   return DAS_OKAY;
}

DAS_API ubyte DasFrame_getType(const DasFrame* pThis){
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
         return das_error(DASERR_FRM, "Direction %s already defined for frame %s",
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
      if(strcasecmp(sDir, pThis->dirs[i]) == 0){
         return i;
      }
   }

   return -1;
}

void del_DasFrame(DasFrame* pThis)
{
   // We have no sub-pointers, so this is just a type safe wrapper on free
   if(pThis != NULL) 
      free(pThis);
}
