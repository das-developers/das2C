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

/* ************************************************************************ */

DasFrame* new_DasFrame(DasDesc* pParent, ubyte id, const char* sName, const char* sBody)
{
   DasFrame* pThis = (DasFrame*) calloc(1, sizeof(DasFrame));
   DasDesc_init(&(pThis->base), FRAME);
   pThis->base.parent = pParent; /* Can be null */
   
   if(sName != NULL) strncpy(pThis->name, sName, DASFRM_NAME_SZ-1);

   if(id == 0){
      das_error(DASERR_FRM, "Frame IDs must be in the range 1 to 255");
      goto ERROR;
   }
   pThis->id = id;
   
   if( DasFrame_setName(pThis, sName) != DAS_OKAY)
      goto ERROR;

   if(sName != NULL)

   return pThis;
ERROR:
   free(pThis);
   return NULL;
}

/*

DasFrame* new_DasFrame2(DasDesc* pParent, ubyte id, const char* sName, ubyte uType)
{
   DasFrame* pThis = (DasFrame*) calloc(1, sizeof(DasFrame));
   DasDesc_init(&(pThis->base), FRAME);
   pThis->base.parent = pParent; / * Can be null * /
   
   if(sName != NULL) strncpy(pThis->name, sName, DASFRM_NAME_SZ-1);

   if(id == 0){
      das_error(DASERR_FRM, "Frame IDs must be in the range 1 to 255");
      goto ERROR;
   }
   pThis->id = id;
   
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

*/

DasFrame* copy_DasFrame(const DasFrame* pThis){

   DasFrame* pCopy = (DasFrame*) calloc(1, sizeof(DasFrame));
   DasDesc_init(&(pCopy->base), FRAME);
   DasDesc_copyIn((DasDesc*) pCopy, (const DasDesc*)pThis);

   pCopy->id     = pThis->id;
   pCopy->flags  = pThis->flags;
   pCopy->pUser  = pThis->pUser;
   memcpy(pCopy->name, pThis->name, DASFRM_NAME_SZ);
   memcpy(pCopy->body, pThis->body, DASFRM_NAME_SZ);
   
   return pCopy;
}

char* DasFrame_info(const DasFrame* pThis, char* sBuf, int nLen)
{
	if(nLen < 30)
		return sBuf;

	char* pWrite = sBuf;

	int nWritten = snprintf(pWrite, nLen - 1, "\n   Vector Frame %02hhu: %s", 
		pThis->id, pThis->name
	);

	pWrite += nWritten;
	nLen -= nWritten;

/* 
	/ * Type and inertial * /
	if(nLen < 40) return pWrite;
	switch(pThis->flags & DASFRM_TYPE_MASK){
	case DASFRM_CARTESIAN     : nWritten = snprintf(pWrite, nLen - 1, " | cartesian");      break;
	case DASFRM_POLAR         : nWritten = snprintf(pWrite, nLen - 1, " | polar");          break;
	case DASFRM_CYLINDRICAL   : nWritten = snprintf(pWrite, nLen - 1, " | cylindrical");    break;
	case DASFRM_SPHERICAL     : nWritten = snprintf(pWrite, nLen - 1, " | spherical");      break;
   case DASFRM_CENTRIC       : nWritten = snprintf(pWrite, nLen - 1, " | planetocentric"); break;
   case DASFRM_DETIC         : nWritten = snprintf(pWrite, nLen - 1, " | planetodetic");   break;
   case DASFRM_GRAPHIC       : nWritten = snprintf(pWrite, nLen - 1, " | planetographic"); break;
	default: nWritten = 0;
	}
*/

	pWrite += nWritten; nLen -= nWritten;
	if(nLen < 40) return pWrite;

	if(pThis->flags & DASFRM_INERTIAL)
		nWritten = snprintf(pWrite, nLen - 1, " (inertial)\n");
	else
		nWritten = snprintf(pWrite, nLen - 1, " (non-inertial)\n");
	
	pWrite += nWritten; nLen -= nWritten;
	if(nLen < 40) return pWrite;

   nWritten = snprintf(
      pWrite, nLen - 1, " %s", pThis->body[0] != '\0' ? pThis->body : "UNK_Body"
   );
   pWrite += nWritten; nLen -= nWritten;
   if(nLen < 30) return pWrite;   

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

DasErrCode DasFrame_encode(
   const DasFrame* pThis, DasBuf* pBuf, const char* sIndent, int nDasVer
){
   char aIndent[24] = {'\0'};
   strncpy(aIndent, sIndent, 21);
   char* pWrite = strlen(sIndent) < 21 ? aIndent + strlen(sIndent) : aIndent + 21;
   strcpy(pWrite, "   ");

   if(nDasVer != 3)
      return das_error(DASERR_FRM, "Currently dasStream version %d is not supported", nDasVer);

   char sBody[DASFRM_NAME_SZ + 12] = {'\0'};
   if(pThis->body[0] != '\0')
      snprintf(sBody, (DASFRM_NAME_SZ+12) - 1, "body=\"%s\"", pThis->body);

   DasBuf_puts(pBuf, sIndent);
   DasBuf_printf(pBuf, "<frame name=\"%s\" %s>\n", 
      pThis->name, sBody
   );

   DasErrCode nRet = DasDesc_encode3((DasDesc*)pThis, pBuf, aIndent);
   if(nRet != 0) return nRet;

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
