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

/** @addtogroup DM
 * @{
 */

/** Individual properties of a desciptor.
 * 
 * DasProp objects assume that some other object, such as a DasAry 
 * the storage buffer and that these functions configure and read that 
 * storage.  Thus there are no "new_" or "del_" function for properties.
 * 
 * Each property has:
 *   - A name
 *   - A UTF-8 encoded value
 *   - A datatype code
 *   - A multiplicity flag
 *   - A validity state
 *   - Associated units
 * 
 */
typedef struct das_prop {
	uint64_t  flags;   // property type, validity, value offset
	das_units units;   // Units, if any
	char      buffer[16];  // A buffer for the property name and value, 
	                   // typically over-malloc'ed.
} DasProp;

/** @} */


/** Get required storage space for a property given a name and value.
 * Note the space is requirements are not sum of the string lengths.
 */
size_t dasprop_memsz(const char* sName, const char* sValue);

/** Flexible das1, das2 and das3 compatable property memory initializer
 * 
 * @param pBuf A byte buffer that is at least dasprop_memsz() bytes long
 * 
 * @param sType The data type of the property, can be one of:
 *    - @b boolean       (das2 & 3)
 *    - @b double, real  (das2, das3)
 *    - @b realRange     (das3)
 *    - @b doubleArray   (das2)
 *    - @b Datum         (das2, anything can have units in das3)
 *    - @b DatumRange    (das2)
 *    - @b int,integer   (das2, das3)
 *    - @b integerRange  (das3)
 *    - @b String,string (das2, das3)
 *    - @b Time,datetime (das2, das3)
 *    - @b TimeRange,datetimeRange (das2,das3)
 * 
 * @param uType An alternate and more efficent method of specifying
 *    the property type.  If sType is NULL, this is read instead.
 *    To set uType or together one constant from each set below.
 * 
 *    Item type: DASPROP_STRING, DASPROP_BOOL, DASPROP_INT, DASPROP_REAL,
 *       DASPROP_DATETIME
 * 
 *    Multiplicity: DASPROP_SINGLE, DASPROP_RANGE, DASPROP_SET
 *
 * @param sName The name of the property, can be no longer then 127
 *              bytes. This is a looser restriction then associated XSDs.
 * 
 * @param sValue The data value, can be no longer then 130,943 bytes.
 * 
 * @param cSep  For array values, this (in addition to possible whitespace)
 *              is the separator between values.  Ignored if Mulitplicity
 *              is not DASPROP_SET.
 * 
 * @param units The units for this value.  If the type is Datum or DatumRange
 *              this value will be ignored, otherwise if this value is NULL
 *              then UNIT_DIMENSIONLESS will be assigned.
 * 
 * @param nStandard.  One of 1, 2 or 3 for das1, das2, or das3.  For
 *              das2 & 3, property names may only consist of the characters 
 *              [a-z][A-Z][0-9] and '_'.
 * 
 * @memberof DasProp
 */
DasErrCode DasProp_init(
   ubyte* pBuf, size_t uBufSz, const char* sType, ubyte uType, const char* sName, 
   const char* sValue, char cSep, das_units units, int nStandard
);

/** Return the memory footprint of a property 
 * @memberof DasProp
 */
size_t DasProp_size(const DasProp* pProp);

/** Get the units for a property */
#define DasProp_units(P) ((P)->units)

/** Get name of a property 
 * @memberof DasProp
 */
#define DasProp_name(P) ((P)->buffer) 

/* const char* DasProp_name(const DasProp* pProp); */

/** Get the string value for a property 
 * @memberof DasProp
 */
const char* DasProp_value(const DasProp* pProp);

/** Get the size of needed escape buffer if property contains illegal XML chars
 * 
 * @return 0 if an XML escape buffer is not needed, the required size in bytes
 *         otherwise
 * @memberof DasProp
 */
size_t DasProp_escapeSize(const DasProp* pProp);

/** Get the string value for a property with illegal XML characters escaped
 * 
 * @param pProp pointer to the property in question
 * @param sBuf  pointer to a buffer to hold the translated property
 * @param uLen  length of the buffer which holds the translated property
 * 
 * @returns A pointer to the escaped property value, which may be the
 *          original internal storage if XML escapes weren't needed or
 *          it may be the given buffer.  
 * 
 *          If XML escape codes are needed and the given buffer is too small
 *          das_error is called, which may (depending on the error disposition)
 *          halt the program.
 * 
 * @memberof DasProp
 */
const char* DasProp_xmlValue(const DasProp* pProp, char* sBuf, size_t uLen);

/*
/ * Get a sub value for a multivalued property.
 * 
 * If DasProp_isSet() or DasProp_isRange() returns true, then this property
 * has sub values.  
 * 
 * @param pProp The property in question
 * 
 * @param idx The index of the sub property, index 0 should always be defined.
 * 
 * @param sBuf A buffer to receive the value.  
 * 
 * @param nLen The length of the buffer to recieve the value. Up to nLen - 1
 *        bytes will be copied in, then a null is appended.  Output should 
 *        always be null terminated even if there wasn't enough room for the
 *        entire sub-value.
 * 
 * @returns The number of bytes needed to store the sub value along with it's
 *        terminating null.  If this is greater then nLen, then the output 
 *        has been truncated.
 * /
bool DasProp_subValue(const DasProp* pProp, int idx, char* sBuf, size_t nLen);
*/

/** Get the value separator character for array-style properties 
 * @memberof DasProp
 */
char DasProp_sep(const DasProp* pProp);

/** Determine if two properties contain equal content 
 * @memberof DasProp
 */
bool DasProp_equal(const DasProp* pOne, const DasProp* pTwo);

/** Get a das2 type string for this property  
 * @memberof DasProp
 */
const char* DasProp_typeStr2(const DasProp* pProp);

/** Get a das3 type string for this property 
 * @memberof DasProp
 */
const char* DasProp_typeStr3(const DasProp* pProp);

/** Convert integer property values to 64-bit ints
 * 
 * Returns the number of conversions, or a negative error value
 * 
 * @memberof DasProp
 */
int DasProp_convertInt(const DasProp* pProp, int64_t* pBuf, size_t uBufLen);

/** Convert real-value properties to double
 * 
 * @returns the number of conversions, or a negative error value
 * 
 * @memberof DasProp
 */
int DasProp_convertReal(const DasProp* pProp, double* pBuf, size_t uBufLen);

/** Convert boolean property values to bytes
 *
 * @returns the number of conversions, or a negative error value
 *
 * @memberof DasProp
 */
int DasProp_convertBool(const DasProp* pProp, uint8_t* pBuf, size_t uBufLen);

/** Convert datatime properties TT2K long integers 
 * 
 * @returns the number of conversions, or a negative error value
 * 
 * @memberof DasProp
 */
int DasProp_convertTt2k(const DasProp* pProp, int64_t* pBuf, size_t uBufLen);

/** Convert datatime properties to a double based value of units 
 * @memberof DasProp
 */
int DasProp_convertTime(const DasProp* pProp, uint64_t* pBuf, size_t uBufLen);

/** Just extract the property strings, don't convert anything 
 * 
 * @returns the number of extracted properties, or a negative error value
 * 
 * @memberof DasProp
 */
int DasProp_extractItems(const DasProp* pProp, char** psBuf, size_t uNumStrs, size_t uLenEa);


/** Get a property type code.
 * 
 * Use the values: DASPROP_MULTI_MASK & DASPROP_TYPE_MASK to extract sections
 * 
 * @memberof DasProp
 */
ubyte DasProp_type(const DasProp* pProp);

/** Mark this property as invalid, this erases the type information and
 * is thus a non-reversable operation 
 * @memberof DasProp
 */
void DasProp_invalidate(DasProp* pProp);

/** Determine if this property has a valid type definition 
 * @memberof DasProp
 */
bool DasProp_isValid(const DasProp* pProp);

/** Determine the number of items in a multi valued property 
 * @memberof DasProp
 */
int DasProp_items(const DasProp* pProp);

/** A mask to select a property's multiplicity setting.
 * This is useful when interpreting the results of DasProp_type() 
 */
#define DASPROP_MULTI_MASK  0x00000003 

#define DASPROP_VALID_MASK  0x00000003  // If these bits are 0, the property

#define DASPROP_INVALID     0x00000000  //  is invalid, ignore it.
#define DASPROP_SINGLE      0x00000001  
#define DASPROP_RANGE       0x00000002
#define DASPROP_SET         0x00000003

/** A mask to select a properties's item type
 * This is useful when interpreting the results of DasProp_type()
 */
#define DASPROP_TYPE_MASK   0x000000F0

#define DASPROP_STRING      0x00000010
#define DASPROP_BOOL        0x00000020
#define DASPROP_INT         0x00000030
#define DASPROP_REAL        0x00000040
#define DASPROP_DATETIME    0x00000050

#define DASPROP_DAS1        1
#define DASPROP_DAS2        2
#define DASPROP_DAS3        3

/** Ask if a property values are a particular type 
 * @memberof DasProp
 */
#define DasProp_isType(P,T) ((P->flags & DASPROP_TYPE_MASK) == T)

/** Returns true if this property has range multiplicity (aka 2 items) 
 * @memberof DasProp
 */
#define DasProp_isRange(P) ((P->flags & DASPROP_RANGE)==DASPROP_RANGE)

/** Returns true if this property has more then one element (aka a set)
 * but the set isn't a bounding range.
 * 
 * @memberof DasProp
 */
#define DasProp_isSet(P) ((P->flags & DASPROP_SET)==DASPROP_SET)

#ifdef __cplusplus
}
#endif

#endif // _property_h_
