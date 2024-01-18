/* Copyright (C) 2024 Chris Piker <chris-piker@uiowa.edu>
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

/** @file property.h */

#ifndef _property_h_
#define _property_h_

#include <das2/defs.h>
#include <das2/units.h>

#ifdef __cplusplus
extern "C" {
#endif

/** Object properties.
 * 
 * These are designed to fit into continuous DasAry structures. Each property 
 * has:
 *   A name, a datatype code, a multiplicity flag, a validity state, units
 *   and the property buffer.
 * 
 * These are put into continuous arrays by over stating the bytes on purpose
 */
typedef struct das_prop {
	uint64_t  flags;   // property type, validity, value offset
	das_units units;   // Units, if any
	char[16]  buffer;  // A buffer for the property name and value, 
	                   // typically over-malloc'ed.
} DasProp;

/** Utility: Given a name and value, calculate the required storage size for
 * a property.  Returns 0 for an invalid property */
size_t dasprop_memsz(const char* sName, const char* sValue);

#define DAS_PROP_EMPTY {0x0ULL, UNIT_DIMENSIONLESS, {'\0'}}

const char* DasProp_name(const DasProp* pProp);

const char* DasProp_value(const DasProp* pProp);

/** Get a das2 compatable property type */
const char* DasProp_typeStr2(const DasProp* pProp);

const char* DasProp_typeStr(const DasProp* pProp)

/** Get a 2-part roperty type code. 
 * Uses the values: DASPROP_MULTI_MASK & DASPROP_TYPE_MASK to extract sections
 */
byte DasProp_type(const DasProp* pProp);

/** Mark this property as invalid, a non-reversable operation */
void DasProp_invalid(DasProp* pProp);

bool DasProp_isValid(DasProp* pProp);

#define DASPROP_VALID_MASK  0x00000003  // If these bits are 0, the property

#define DASPROP_MULTI_MASK  0x00000003 
#define DASPROP_INVALID     0x00000000  //  is invalid, ignore it.
#define DASPROP_SINGLE      0x00000001  
#define DASPROP_RANGE       0x00000002
#define DASPROP_SET         0x00000003

#define DASPROP_TYPE_MASK   0x000000F0
#define DASPROP_STRING      0x00000010
#define DASPROP_BOOL        0x00000020
#define DASPROP_INT         0x00000030
#define DASPROP_REAL        0x00000040
#define DASPROP_DATETIME    0x00000050

/** Initialize a buffer as a das property
 * 
 * @param pBuf A byte buffer that is at least dasprop_memsz() bytes long
 * 
 * @param sType The data type of the property
 *
 * @param sName The name of the property, can be no longer then 127
 *              bytes. This is a looser restriction then associated XSDs.
 * 
 * @param sValue The data value, can be no longer then 130,943 bytes.
 * 
 * @param units The units for this value.  If units are UNIT_DIMENSIONLESS
 *              then this value is not considered a Datum for das v2.2
 *              compatability purposes
 * 
 * @param bStrict If true, names must not contain any characters other
 *              then [a-z][A-Z][0-9] and '_'.
 */
DasErrCode DasProp_init2(
   byte* pBuf, size_t uBufSz, const char* sType, const char* sName,
   const char* sValue, das_units units, bool bStrict 
);
