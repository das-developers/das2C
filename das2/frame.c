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
   if(ft == DASFRM_SPHERICAL     ) return "spherical";
   if(ft == DASFRM_CENTRIC       ) return "planetocentric";
   if(ft == DASFRM_DETIC         ) return "planetodetic";
   if(ft == DASFRM_GRAPHIC       ) return "planetographic";

   daslog_error_v("Unknown vector or coordinate frame type id: '%hhu'.", uFT);
   return "";   
}

ubyte das_str2frametype(const char* sFT)
{
   if( strcasecmp(sFT, "cartesian") == 0)      return DASFRM_CARTESIAN;
   if( strcasecmp(sFT, "polar") == 0)          return DASFRM_POLAR;
   if( strcasecmp(sFT, "sphere_surface") == 0) return DASFRM_SPHERE_SURFACE;
   if( strcasecmp(sFT, "cylindrical") == 0)    return DASFRM_CYLINDRICAL;
   if( strcasecmp(sFT, "spherical") == 0)      return DASFRM_CYLINDRICAL;
   if( strcasecmp(sFT, "planetocentric") == 0) return DASFRM_CENTRIC;
   if( strcasecmp(sFT, "planetodetic") == 0)   return DASFRM_DETIC;
   if( strcasecmp(sFT, "planetographic") == 0) return DASFRM_GRAPHIC;

   daslog_error_v("Unknown vector or coordinate frame type: '%s'.", sFT);
   return 0;
}

/* ************************************************************************ */

DasFrame* new_DasFrame(DasDesc* pParent, ubyte id, const char* sName, const char* sType)
{
   DasFrame* pThis = (DasFrame*) calloc(1, sizeof(DasFrame));
   DasDesc_init(&(pThis->base), FRAME);
   pThis->base.parent = pParent; /* Can be null */
   
   if(sName != NULL) strncpy(pThis->name, sName, DASFRM_NAME_SZ-1);

   if(id == 0){
      das_error(DASERR_FRM, "Frame IDs must be in the range 1 to 255");
      goto ERROR;
   }
   
   if( DasFrame_setSys(pThis, sType) != DAS_OKAY)
      goto ERROR;
   
   if( DasFrame_setName(pThis, sName) != DAS_OKAY)
      goto ERROR;

   return pThis;
ERROR:
   free(pThis);
   return NULL;
}

DasFrame* new_DasFrame2(DasDesc* pParent, ubyte id, const char* sName, ubyte uType)
{
   DasFrame* pThis = (DasFrame*) calloc(1, sizeof(DasFrame));
   DasDesc_init(&(pThis->base), FRAME);
   pThis->base.parent = pParent; /* Can be null */
   
   if(sName != NULL) strncpy(pThis->name, sName, DASFRM_NAME_SZ-1);

   if(id == 0){
      das_error(DASERR_FRM, "Frame IDs must be in the range 1 to 255");
      goto ERROR;
   }
   
   pThis->flags |= (uType & DASFRM_TYPE_MASK);
   const char* sType = das_frametype2str(uType);
   if(sType[0] == '\0')
      goto ERROR;
   
   strncpy(pThis->systype, sType, DASFRM_TYPE_SZ-1);
   
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
   memcpy(pCopy->systype, pThis->systype, DASFRM_TYPE_SZ);
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
	case DASFRM_CARTESIAN     : nWritten = snprintf(pWrite, nLen - 1, " | cartesian");      break;
	case DASFRM_POLAR         : nWritten = snprintf(pWrite, nLen - 1, " | polar");          break;
	case DASFRM_SPHERE_SURFACE: nWritten = snprintf(pWrite, nLen - 1, " | sphere_surface"); break;
	case DASFRM_CYLINDRICAL   : nWritten = snprintf(pWrite, nLen - 1, " | cylindrical");    break;
	case DASFRM_SPHERICAL     : nWritten = snprintf(pWrite, nLen - 1, " | spherical");      break;
   case DASFRM_CENTRIC       : nWritten = snprintf(pWrite, nLen - 1, " | planetocentric"); break;
   case DASFRM_DETIC         : nWritten = snprintf(pWrite, nLen - 1, " | planetodetic");   break;
   case DASFRM_GRAPHIC       : nWritten = snprintf(pWrite, nLen - 1, " | planetographic"); break;
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

DAS_API DasErrCode DasFrame_setSys(DasFrame* pThis, const char* sType)
{
   if((sType == NULL)||(sType[0] == '\0'))
      return das_error(DASERR_FRM, "Empty coordinate frame system");

   strncpy(pThis->systype, sType, DASFRM_TYPE_SZ-1);
   pThis->flags |= das_str2frametype(pThis->systype);

   return DAS_OKAY;
}

DAS_API ubyte DasFrame_getSys(const DasFrame* pThis){
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

/* Were going to go with ISO 31-11 on this */
DasErrCode DasFrame_setDefDirs(DasFrame* pThis)
{
   switch(pThis->flags & DASFRM_TYPE_MASK){
   case DASFRM_CARTESIAN:
      strcpy(pThis->dirs[0], "x"); strcpy(pThis->dirs[1], "y"); strcpy(pThis->dirs[2], "z");
      pThis->ndirs = 3;
      break;
   case DASFRM_POLAR:
      strcpy(pThis->dirs[0], "r"); strcpy(pThis->dirs[1], "φ");
      pThis->ndirs = 2;      
      break;
   case DASFRM_SPHERE_SURFACE: 
      strcpy(pThis->dirs[0], "θ"); strcpy(pThis->dirs[1], "φ");
      DasDesc_set((DasDesc*)pThis, "string", "description",
         "θ is the angle from the north pole, φ is eastward angle"
      );
      pThis->ndirs = 2;
      break;
   case DASFRM_CYLINDRICAL:
      strcpy(pThis->dirs[0], "ρ"); strcpy(pThis->dirs[1], "φ"); strcpy(pThis->dirs[2], "z");
      DasDesc_set((DasDesc*)pThis, "string", "description",
         "ρ is distance to the z-axis, φ is eastward angle"
      );
      pThis->ndirs = 3;
      break;
   case DASFRM_SPHERICAL: 
      strcpy(pThis->dirs[0], "r"); strcpy(pThis->dirs[1], "θ"); strcpy(pThis->dirs[2], "φ");
      DasDesc_set((DasDesc*)pThis, "string", "description",
         "θ is zero at the north pole (colatitude), φ is the eastward angle"
      );
      pThis->ndirs = 3;
      break;
   case DASFRM_CENTRIC: 
      strcpy(pThis->dirs[0], "r"); strcpy(pThis->dirs[1], "θ"); strcpy(pThis->dirs[2], "φ");
      DasDesc_set((DasDesc*)pThis, "string", "description",
         "θ is zero at the equator (latitude), φ is the eastward angle"
      );
      pThis->ndirs = 3;
      break;

   case DASFRM_DETIC:
      strcpy(pThis->dirs[0], "r"); strcpy(pThis->dirs[1], "θ"); strcpy(pThis->dirs[2], "φ");
      DasDesc_set((DasDesc*)pThis, "string", "description",
         "Ellipsoidal coordinates, surface normals ususally do not intersect the origin. "
         "θ is zero at the equator (latitude), φ is the eastward angle"
      );
      pThis->ndirs = 3;
      break;
   case DASFRM_GRAPHIC: 
      strcpy(pThis->dirs[0], "r"); strcpy(pThis->dirs[1], "θ"); strcpy(pThis->dirs[2], "φ");
      DasDesc_set((DasDesc*)pThis, "string", "description",
         "Ellipsoidal coordinates, surface normals ususally do not intersect the origin. "
         "θ is zero at the equator (latitude), φ is the westward angle"
      );
      pThis->ndirs = 3;
      break;

   default:
      return das_error(DASERR_FRM, 
         "Frame type %s has no default set of directions", pThis->name
      );
   }
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

DasErrCode DasFrame_encode(
   const DasFrame* pThis, DasBuf* pBuf, const char* sIndent, int nDasVer
){
   char aIndent[24] = {'\0'};
   strncpy(aIndent, sIndent, 21);
   char* pWrite = strlen(sIndent) < 21 ? aIndent + strlen(sIndent) : aIndent + 21;
   strcpy(pWrite, "   ");

   if(nDasVer != 3)
      return das_error(DASERR_FRM, "Currently dasStream version %d is not supported", nDasVer);

   DasBuf_puts(pBuf, sIndent);
   DasBuf_printf(pBuf, "<frame name=\"%s\" type=\"%s\" >\n", pThis->name, pThis->systype);

   DasErrCode nRet = DasDesc_encode3((DasDesc*)pThis, pBuf, aIndent);
   if(nRet != 0) return nRet;

   /* Now handle my directions */
   for(int i = 0; i < pThis->ndirs; ++i){
      DasBuf_puts(pBuf, sIndent);
      DasBuf_puts(pBuf, sIndent);
      DasBuf_printf(pBuf, "<dir name=\"%s\"/>\n", pThis->dirs[i]);
   }
   DasBuf_puts(pBuf, sIndent);
   DasBuf_puts(pBuf, "</frame>\n");
   return DAS_OKAY;
}

void del_DasFrame(DasFrame* pThis)
{
   // We have no sub-pointers, so this is just a type safe wrapper on free
   if(pThis != NULL) 
      free(pThis);
}
