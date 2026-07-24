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
#include "context.h"

/* ************************************************************************ */
/* The legacy DasFrame face of the frame kind */

DasFrame* new_DasFrame(DasDesc* pParent, ubyte id, const char* sName)
{
   /* pParent deliberately ignored: context entries are parentless so
      property lookups don't fall through to the stream */
   (void)pParent;
   return new_DasCtx(CTX_FRAME, id, sName, NULL);
}

char* DasCtx_info(const DasCtx* pThis, char* sBuf, int nLen)
{
	if(nLen < 30)
		return sBuf;

	char* pWrite = sBuf;

	int nWritten = snprintf(pWrite, nLen - 1, "\n   Context %s %02hhu: %s",
		pThis->sKind, pThis->id, pThis->sName
	);
	pWrite += nWritten; nLen -= nWritten;
	if(nLen < 40) return pWrite;

	if(pThis->kind != CTX_GIVEN){
		const char* sBody = DasCtx_body(pThis);
		nWritten = snprintf(
			pWrite, nLen - 1, " %s", sBody[0] != '\0' ? sBody : "UNK_Body"
		);
		pWrite += nWritten; nLen -= nWritten;
		if(nLen < 40) return pWrite;
	}

	if(pThis->kind == CTX_FRAME){
		if(pThis->u.frame.bFixed)
			nWritten = snprintf(pWrite, nLen - 1, " (body fixed)\n");
		else
			nWritten = snprintf(pWrite, nLen - 1, " (body centered)\n");
		pWrite += nWritten; nLen -= nWritten;
		if(nLen < 30) return pWrite;
	}

	char* pSubWrite = DasDesc_info((DasDesc*)pThis, pWrite, nLen, "      ");
	nLen -= (pSubWrite - pWrite);
	pWrite = pSubWrite;

	if(nLen > 4){
		nWritten = snprintf(pWrite, nLen-1, "\n");
		pWrite += nWritten; nLen -= nWritten;
	}
	return pWrite;
}

void DasFrame_fixed(DasFrame* pThis, bool bFixed)
{
   if(pThis->kind == CTX_FRAME)
      pThis->u.frame.bFixed = bFixed;
   else
      das_error(DASERR_FRM, "Entry %s is not a frame", pThis->sName);
}

DasErrCode DasFrame_setName(DasFrame* pThis, const char* sName)
{
   if((sName == NULL)||(sName[0] == '\0'))
      return das_error(DASERR_FRM, "Null or empty name string");
   if(strlen(sName) >= DASCTX_NAME_SZ)
      return das_error(DASERR_FRM,
         "Context name '%s' exceeds %d bytes", sName, DASCTX_NAME_SZ - 1
      );

   strcpy(pThis->sName, sName);
   return DAS_OKAY;
}

DasErrCode DasFrame_setBody(DasFrame* pThis, const char* sBody)
{
   if(pThis->kind != CTX_FRAME)
      return das_error(DASERR_FRM, "Entry %s is not a frame", pThis->sName);

   /* Unlike the name, a body is optional and often unknowable: only a stream
      that declares a <frame> section states one.  Clearing is therefore legal,
      not an error -- DasFrame_encode() omits the attribute when empty and
      DasCtx_info() reports UNK_Body. */
   if(sBody == NULL){
      pThis->u.frame.sBody[0] = '\0';
      return DAS_OKAY;
   }

   strncpy(pThis->u.frame.sBody, sBody, DASCTX_NAME_SZ-1);
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

   char sBody[DASCTX_NAME_SZ + 12] = {'\0'};
   if(pThis->u.frame.sBody[0] != '\0')
      snprintf(sBody, (DASCTX_NAME_SZ+12) - 1, "body=\"%s\"", pThis->u.frame.sBody);

   /* Fixed defaults to false on read. Only emit it when set. */
   const char* sFixed = DasCtx_isFixed(pThis) ? " fixed=\"true\"" : "";

   DasBuf_puts(pBuf, sIndent);
   DasBuf_printf(pBuf, "<frame name=\"%s\" %s%s>\n",
      pThis->sName, sBody, sFixed
   );

   DasErrCode nRet = DasDesc_encode3((DasDesc*)pThis, pBuf, aIndent);
   if(nRet != 0) return nRet;

   DasBuf_puts(pBuf, sIndent);
   DasBuf_puts(pBuf, "</frame>\n");
   return DAS_OKAY;
}

/* ************************************************************************* */
/* Context entries: the general form frames belong to */

DasCtx* new_DasCtx(ubyte kind, ubyte id, const char* sName, const char* sKind)
{
   if((id < 1)||(id >= DASCTX_MAX)){
      das_error(DASERR_FRM,
         "Context handle %hhu is outside 1 to %d", id, DASCTX_MAX - 1
      );
      return NULL;
   }
   if((sName == NULL)||(sName[0] == '\0')){
      das_error(DASERR_FRM, "Context entries require an instance name");
      return NULL;
   }
   if(strlen(sName) >= DASCTX_NAME_SZ){
      das_error(DASERR_FRM,
         "Context name '%s' exceeds %d bytes", sName, DASCTX_NAME_SZ - 1
      );
      return NULL;
   }

   const char* sSetKind = NULL;
   switch(kind){
   case CTX_FRAME:   sSetKind = "frame";   break;
   case CTX_SURFACE: sSetKind = "surface"; break;
   case CTX_GIVEN:
      if((sKind == NULL)||(sKind[0] == '\0')){
         das_error(DASERR_FRM, "A <given> context entry requires a type token");
         return NULL;
      }
      if(strlen(sKind) >= DASCTX_KIND_SZ){
         das_error(DASERR_FRM,
            "Context type '%s' exceeds %d bytes", sKind, DASCTX_KIND_SZ - 1
         );
         return NULL;
      }
      /* A wildcat may not masquerade as a known kind; the schema can't
         express this exclusion (XSD 1.0 regex has no negation) so the
         library is the gate, both directions. */
      if((strcmp(sKind, "frame") == 0)||(strcmp(sKind, "surface") == 0)){
         das_error(DASERR_FRM,
            "A <given> may not use the reserved type '%s'; declare a real "
            "<%s> instead", sKind, sKind
         );
         return NULL;
      }
      sSetKind = sKind;
      break;
   default:
      das_error(DASERR_FRM, "Unknown context kind code %hhu", kind);
      return NULL;
   }

   DasCtx* pThis = (DasCtx*)calloc(1, sizeof(DasCtx));
   DasDesc_init(&(pThis->base), CONTEXT);
   pThis->base.bNoInherit = true;  /* isolation by construction, not by hoping
                                      parent stays NULL */
   pThis->kind = kind;
   pThis->id   = id;
   strcpy(pThis->sName, sName);
   strcpy(pThis->sKind, sSetKind);
   return pThis;
}

void del_DasCtx(DasCtx* pThis)
{
   if(pThis){
      DasDesc_freeProps(&(pThis->base));
      free(pThis);
   }
}

DasCtx* copy_DasCtx(const DasCtx* pThis)
{
   DasCtx* pCopy = new_DasCtx(pThis->kind, pThis->id, pThis->sName,
                  (pThis->kind == CTX_GIVEN) ? pThis->sKind : NULL);
   if(pCopy == NULL)
      return NULL;
   pCopy->pUser = pThis->pUser;
   memcpy(&(pCopy->u), &(pThis->u), sizeof(pCopy->u));
   DasDesc_copyIn(&(pCopy->base), &(pThis->base));
   return pCopy;
}
