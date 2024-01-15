/* Copyright (C) 2024 Chris Piker <chris-piker@uiowa.edu> (re-written, new storage)
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

#define _POSIX_C_SOURCE 200112L

#include "property.h"

/* The property flags (32 bits total), most not used, listed high byte
   (Actual memory is backwards to chart below for an x86_64 machine)

3        2        1        0
 76543210 76543210 76543210 76543210 76543210 76543210 76543210 76543210 
+--------+--------+--------+--------+--------+--------+--------+--------+
|                          |sep_char|  total length    |valoff |TTTT  MM|
+--------+--------+--------+--------+--------+--------+--------+--------+
*/

#define DASPROP_MULTI_MASK  0x00000003  /* MM */
#define DASPROP_INVALID     0x00000000
/* #define DASPROP_SINGLE      0x00000001  */
/* #define DASPROP_RANGE       0x00000002  */
/* #define DASPROP_SET         0x00000003  */

#define DASPROP_TYPE_MASK   0x000000F0  /* TTTT */
/* #define DASPROP_STRING      0x00000010 */
/* #define DASPROP_BOOL        0x00000020 */
/* #define DASPROP_INT         0x00000030 */
/* #define DASPROP_REAL        0x00000040 */
/* #define DASPROP_DATETIME    0x00000050 */

#define DASPROP_NLEN_MASK   0x00007F00  /* value offset (max 128 bytes) */
#define DASPROP_NLEN_SHIFT  8
#define DASPROP_TLEN_MASK   0xFFFF8000  /* Total length (max 131,072 bytes)  */
#define DASPROP_TLEN_SHIFT  15       

#define DASPROP_NMAX_SZ     127
#define DASPROP_VMAX_SZ     130943      /* leaves room for end null and val name */

#define DASPROP_MIN_MALLOC  8 + 8 + 4   /* Smallest possible property size */


const char* DasProp_name(const DasProp* pProp)
{
   if(! (pProp->flags & DASPROP_MULTI_MASK))
      return NULL;
   return pProp->buffer;
}

const char* DasProp_value(const DasProp* pProp)
{
   if(! (pProp->flags & DASPROP_MULTI_MASK))
      return NULL;

   const char* sBuf = pProp->buffer;
   size_t uOffset = ((pProp->flags & DASPROP_NLEN_MASK) >> DASPROP_NLEN_SHIFT);
   return sBuf + uOffset;
}

/* Return das2 types, there are more types then this */
const char* DasProp_type2(const DasProp* pProp)
{
   uint32_t uMulti = pProp->flags & DASPROP_MULTI_MASK;
   if(!uMulti)
      return NULL;

   switch(pProp->flags & DASPROP_TYPE_MASK){
   case DASPROP_BOOL:   return "boolean";
   case DASPROP_REAL: 
      if(pProp->units == UNIT_DIMENSIONLESS){
         if(uMulti == DASPROP_RANGE)
            return "doubleArray"
         else
            return "double";
      }
      else{
         if(uMulti == DASPROP_RANGE)
            return "DatumRange";
         else
            return "Datum";
      }
   case DASPROP_INT:    return "int";
   default:  return "String";
   }
}

byte DasProp_typeCode(const DasProp* pProp){
   if(! (pProp->flags & DASPROP_MULTI_MASK))
      return 0;
   else
      return (pProp->flags & 0xFF);
}

DasProp* DasProp_append(
   DasAry* pSink, byte type, byte multi, char cSep, const char* sName, 
   const char* sValue, das_units units, bool bStrict
){
   // check arguments, empty value is okay, null value is not
   if(
      (type == 0)||(multi == 0)||(sName == NULL)||(sValue == NULL)||
      (sName[0] == '\0')||((type >> 4)<1)||((type >> 4)>5)||
      (multi < 1)||(multi > 3)||
      ((multi == DASPROP_SET)&&(cSep < 0x21)||(cSep > 0x7F))
   ){
      das_error(DASERR_PROP, "Invalid argument to property creator");
      return NULL;
   }

   size_t uLen = 8 + sizeof(das_units); // first two fields

   size_t uNameLen = strlen(sName);

   /* handle odd stuff from Das1 DSDFs only if an internal switch is thrown */
   if(!bStrict){
      for(size_t u = 0; u < uNameLen; ++u) 
         if(!isalnum(sName[i]) && (sName[i] != '_'))
            return das_error(DASERR_PROP, "Invalid property name '%s'", sName);   
   }
   sizeof(DasProp);  // This is the minimal size




}


DasProp* DasProp_append2(
   DasAry* pSink, const char* sType, const char* sName, const char* sValue, bool bStrict 
){

if(sType == NULL) sType = "String";
   if(sName == NULL) return das_error(DASERR_DESC, "Null value for sName");
   
   if(strlen(sType) < 2 ) 
      return das_error(DASERR_DESC, "Property type '%s' is too short.", sType);
   
   char** pProps = pThis->properties;
   char sBuf[128] = {'\0'};
   int iProp=-1;
   
   /* Look for the prop string skipping over holes */
   for(i=0; i < DAS_XML_MAXPROPS; i+=2 ){
      if( pProps[i] == NULL ) continue;
      snprintf(sBuf, 128, "%s:%s", sType, sName);
      if (strcmp( pProps[i], sBuf )==0 ) iProp= i;
   }
   
   size_t uLen;
   if(iProp == -1){
      /* Look for the lowest index slot for the property */
      for(i=0; i< DAS_XML_MAXPROPS; i+= 2){
         if( pProps[i] == NULL){ iProp = i; break;}
      }
      if(iProp == -1){
         return das_error(DASERR_DESC, "Descriptor exceeds the max number of "
                           "properties %d", DAS_XML_MAXPROPS/2);
      }
      if(sType != NULL){
         uLen = strlen(sType) + strlen(sName) + 2; 
         pProps[iProp] = (char*)calloc(uLen, sizeof(char));
         snprintf(pProps[iProp], uLen, "%s:%s", sType, sName);
      }
      /* pProps[iProp+2]= NULL; */
   } else {
      free( pProps[iProp+1] );
   }

   /* own it */
   if((sVal != NULL) && (strlen(sVal) > 0)){
      pProps[iProp+1]= (char*)calloc(strlen(sVal)+1, sizeof(char));
      strcpy( pProps[iProp+1], sVal );
   }
   else{
      pProps[iProp+1] = NULL;
   }




}