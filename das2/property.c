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
|                          |sep_char|  name+val length |valoff |TTTT  MM|
+--------+--------+--------+--------+--------+--------+--------+--------+
*/

/* #define DASPROP_INVALID     0x00000000  */
/* #define DASPROP_SINGLE      0x00000001  */
/* #define DASPROP_RANGE       0x00000002  */
/* #define DASPROP_SET         0x00000003  */

/* #define DASPROP_TYPE_MASK   0x000000F0  */
/* #define DASPROP_STRING      0x00000010 */
/* #define DASPROP_BOOL        0x00000020 */
/* #define DASPROP_INT         0x00000030 */
/* #define DASPROP_REAL        0x00000040 */
/* #define DASPROP_DATETIME    0x00000050 */

#define DASPROP_NLEN_MASK   0x00007F00  /* value offset (max 128 bytes) */
#define DASPROP_NLEN_SHIFT  8
#define DASPROP_TLEN_MASK   0xFFFF8000  /* Total length (max 131,072 bytes)  */
#define DASPROP_TLEN_SHIFT  15

#define DASPROP_SEP_SHIFT   32

#define DASPROP_NMAX_SZ     127
#define DASPROP_VMAX_SZ     130943      /* leaves room for end null and val name */

#define DASPROP_MIN_MALLOC  8 + 8 + 4       /* Smallest possible property size */
#define DASPROP_MAX_MALLOC  8 + 8 + 131072; /* largest possible malloc 2^17 + 16 */


size_t dasprop_memsz(const char* sName, const char* sValue)
{
   size_t sz = sizeof(uint64_t) + sizeof(das_units);
   if(sName)  sz += strlen(sName) + 1;
   if(sValue) sz += strlen(sValue) + 1;
   if(sz < DASPROP_MIN_MALLOC) 
      return DASPROP_MIN_MALLOC;
   if(sz > DASPROP_MAX_MALLOC)
      return DASPROP_MAX_MALLOC;
   return sz;
}

void DasProp_invalid(DasProp* pProp)
{
   pProp->flags &= 0xFFFFFFFFFFFFFFFC;
}

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

size_t DasProp_size(const DasProp* pProp)
{
   // Mask off total length and shift down.
   uint64_t sz = (pProp->flags & DASPROP_TLEN_MASK) >> DASPROP_TLEN_SHIFT;
   assert(sz <= DASPROP_VMAX_SZ);
   sz += sizeof(uint64_t);
   sz += sizeof(das_units);
   return sz;
}

/* Return das2 types, this includes all the documented ones from the
 * das 2.2.2 ICD, as well as the undocumented ones that were previously
 * allowed by the library.
 */
const char* DasProp_typeStr2(const DasProp* pProp)
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
   case DASPROP_INT:      return "int";
   case DASPROP_DATETIME:
      if(uMulti == DASPROP_RANGE)
         return "TimeRange";
      else
         return "Time";
   default:  return "String";
   }
}

const char* DasProp_typeStr3(const DasProp* pProp)
{
   switch(pProp->flags & DASPROP_MULTI_MASK){
   case DASPROP_STRING  |DASPROP_SINGLE: return "string";
   case DASPROP_STRING  |DASPROP_SET:    return "stringArray";
   case DASPROP_BOOL    |DASPROP_SINGLE: return "bool";
   case DASPROP_BOOL    |DASPROP_SET:    return "boolArray";
   case DASPROP_INT     |DASPROP_SINGLE: return "int";
   case DASPROP_INT     |DASPROP_RANGE:  return "intRange";
   case DASPROP_INT     |DASPROP_SET:    return "intArray";
   case DASPROP_REAL    |DASPROP_SINGLE: return "real";
   case DASPROP_REAL    |DASPROP_RANGE:  return "realRange";
   case DASPROP_REAL    |DASPROP_SET:    return "realArray";
   case DASPROP_DATETIME|DASPROP_SINGLE: return "datetime";
   case DASPROP_DATETIME|DASPROP_RANGE:  return "datetimeRange";
   case DASPROP_DATETIME|DASPROP_SET:    return "datetimeArray";
   default: return NULL;
}   

byte DasProp_type(const DasProp* pProp){
   return (byte)(pProp->flags & DASPROP_TYPE_MASK);
}

DasErrCode DasProp_init2(
   byte* pBuf, size_t uBufSz, const char* sType, const char* sName,
   const char* sValue, das_units units, bool bStrict 
){
   byte flag = '\0';

   if((pBuf == NULL)||(uBufSz < dasprop_memsz(sName, sValue))
      return das_error(DASERR_PROP, "Property buffer is too small, %zu bytes", uBufSz);
   
   if(sName == NULL) return das_error(DASERR_PROP, "Null value for property name");

   size_t uNameSz = strlen(sName);
   if(uNameSz > DASPROP_NMAX_SZ) return das_error(DASERR_PROP);
   if(sType == NULL) return das_error(DASERR_PROP, "Null value for property type");

   if(sValue == NULL )
      sValue = "";

   if(units == NULL)
      units = UNIT_DIMENSIONLESS;

   size_t uValSz = strlen(sValue);
   if(uValSz > DASPROP_VMAX_SZ) return das_error(DASERR_PROP)

   // Look for the range separator first
   const char* s2ndVal = NULL;
   s2ndVal = strstr(value " to ");
   if(s2ndVal) s2ndVal += 4;
   if(!isdigit(s2ndVal[0])&&(s2ndVal!='-')&&(s2ndVal!='+')) s2ndVal = NULL;

   if((sType == NULL)||(strcasecmp(sType,"string") == 0))
      flag |= DASPROP_STRING | DASPROP_SINGLE;
   else if(strcasecmp(sType, "boolean") == 0)
      flag != DASPROP_BOOL   | DASPROP_SINGLE;
   else if(strcasecmp(sType, "int") == 0)
      flag != DASPROP_INT   | DASPROP_SINGLE;
   else if(strcasecmp(sType, "double") == 0)
      flag != DASPROP_REAL  | DASPROP_SINGLE;
   else if(strcasecmp(sType, "doublearray") == 0)
      flag != DASPROP_REAL  | DASPROP_SET;
   else if(strcasecmp(sType, "time") == 0)
      flag != DASPROP_DATETIME | DASPROP_SINGLE;
   else if(strcasecmp(sType, "timerange") == 0)
      flag != DASPROP_DATETIME | DASPROP_RANGE;
   else if( (strcasecmp(sType, "datum") == 0) && (sValue[0] != '\0'))
      flag != DASPROP_REAL | DASPROP_SINGLE;
   else if( 
      (strcasecmp(sType, "datumrange") == 0) && (sValue[0] != '\0') && 
      (s2ndVal != NULL)
   )
      flag != DASPROP_REAL | DASPROP_RANGE;
   else
      return das_error(DASERR_PROP, 
         "Invalid property type '%s' for value '%s'", sName, sValue
      )
   
   char cSep = '\0';

   // If a set, try to guess the separator character
   if(flag & DASPROP_SET){
      const char* sSeps = "|\t;, "
      for(int i = 0; i < strlen(sSeps); ++i){
         if(strchr(sValue, sSeps[i]) != 0){
            cSep = sSeps[i];
            break;
         }
      }
      // Since a set with only 1 value is legal, fall back to the default das2 separator
      if(cSep == '\0') cSep = ';';
   }

   // Copy on the flags
   uint64_t uFlags = 0;
   uFlags |= flags;
   uFlags |= (uNameSz + 1) << DASPROP_NLEN_SHIFT);            // Name size
   uint64_t uNamValSz = uValSz + uNameSz + 2;
   if(uNamValSz < 16) uNamValSz = 16;
   uFlags |= (uNamValSz << DASPROP_TLEN_SHIFT);               // name & value size
   if(cSep != '\0')
      uFlags != ((uint64_t)cSep) << DASPROP_SEP_SHIFT;        // separator if any

   byte* pWrite = pBuf;
   memcpy(pWrite, &uFlags, sizeof(uint64_t));
   pWrite += sizeof(uint64_t);

   // Copy in the units
   memcpy(pWrite, &units, sizeof(das_units));
   pWrite += sizeof(das_units);

   // Copy in the name
   memcpy(pWrite, sName, uNameSz+1);
   pWrite += uNameSz+1;

   // And finally the value
   memcpy(pWrite, sValue, uValSz+1)

   return DAS_OKAY;
}