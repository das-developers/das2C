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
const char* DasProp_type2(const DasProp* pProp);

/** Get a property type code. 
 * @returns The type code, which is a two part value.  The low nibble 
 * contains the multiplicity, the high nibble contains the type. */
byte DasProp_typeCode(const DasProp* pProp);

/** Mark this property as invalid, a non-reversable operation */
void DasProp_invalid(DasProp* pProp);

#define DASPROP_VALID_MASK  0x00000003  // If these bits are 0, the property
#define DASPROP_SINGLE      0x00000001  //  is invalid, ignore it.
#define DASPROP_RANGE       0x00000002
#define DASPROP_SET         0x00000003

#define DASPROP_STRING      0x00000010
#define DASPROP_BOOL        0x00000020
#define DASPROP_INT         0x00000030
#define DASPROP_REAL        0x00000040
#define DASPROP_DATETIME    0x00000050

/** Make a new property directly in a das array without extra mallocs
 * 
 * @param pSink A DasAry of type vtByte that is ragged in the last index
 *              The last index must be ragged because properties are of
 *              variable length
 * 
 * @param type  All values are stored as UTF-8 strings, but this field
 *              provides the semantic type of the property.  Use one of
 *              the values: 
 *                DASPROP_STRING, DASPROP_BOOL, DASPROP_INT, DASPROP_REAL,
 *                DASPROP_DATETIME
 * 
 * @param multi The multiplicity of the values, use one of the settings:
 *              DASPROP_SINGLE, DASPROP_RANGE, DASPROP_SET   
 * 
 * @param cSep  A separator character if multi=DASPROP_SET, otherwise
 *              ignored.  If this is a set then cCep must be a 7-bit
 *              ascii character in the range 0x21 through 0x7E (aka a
 *              printable).
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
 *              
 */
DasProp* DasProp_append(
	DasAry* pSink, byte type, byte multi, char cSep, const char* sName, 
	const char* sValue, das_units units, bool bStrict
);

/** A das2 compatable version of DasProp_append() above.  
 * 
 * This version parses the sType string to set the type and multiplicity
 * codes and potentially the units.
 */
DasProp* DasProp_append2(
	DasAry* pSink, const char* sType, const char* sName, const char* sValue, bool bStrict 
);

// 1100 = 8 + 4 = 12 = C