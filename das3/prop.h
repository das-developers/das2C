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
 * version 2.1 along with das2C; if not, see <http://www.gnu.org/licenses/>. 
 */

/** @file prop.h */

#ifndef _das_prop_h_
#define _das_prop_h_

#include <das3/defs.h>
#include <das3/units.h>

#ifdef __cplusplus
extern "C" {
#endif

/** @addtogroup datamodel
 * @{
 */

/** @class DasProp prop.h <das3/prop.h>
 *
 * Individual properties of a descriptor.
 *
 * Properties are always made by their container, which packs them into
 * storage it owns (a DasDesc, or the das_form_param_buf handed to
 * DasForm_paramAt()).  Thus there are no "new_" or "del_" function for
 * properties, set them with the DasDesc_set*() calls and read them with
 * the accessors below.
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
typedef struct das_prop DasProp;

/** @} */

/** Get the units for a property
 * @memberof DasProp
 */
DAS_API das_units DasProp_units(const DasProp* pProp);

/** Get name of a property
 * @memberof DasProp
 */
DAS_API const char* DasProp_name(const DasProp* pProp);

/** Get the string value for a property 
 * @memberof DasProp
 */
DAS_API const char* DasProp_value(const DasProp* pProp);


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
 * @param nLen The length of the buffer to receive the value. Up to nLen - 1
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
DAS_API char DasProp_sep(const DasProp* pProp);


/** Determine if two properties contain equal content 
 * @memberof DasProp
 */
DAS_API bool DasProp_equal(const DasProp* pOne, const DasProp* pTwo);


/** Get a das3 type string for this property 
 * @memberof DasProp
 */
DAS_API const char* DasProp_typeStr3(const DasProp* pProp);

/** Convert integer property values to 64-bit ints
 * 
 * Returns the number of conversions, or a negative error value
 * 
 * @memberof DasProp
 */
DAS_API int DasProp_convertInt(const DasProp* pProp, int64_t* pBuf, size_t uBufLen);

/** Convert real-value properties to double
 * 
 * @returns the number of conversions, or a negative error value
 * 
 * @memberof DasProp
 */
DAS_API int DasProp_convertReal(const DasProp* pProp, double* pBuf, size_t uBufLen);

/** Convert boolean property values to bytes
 *
 * @returns the number of conversions, or a negative error value
 *
 * @memberof DasProp
 */
DAS_API int DasProp_convertBool(const DasProp* pProp, uint8_t* pBuf, size_t uBufLen);

/** Convert datatime properties TT2K long integers 
 * 
 * @returns the number of conversions, or a negative error value
 * 
 * @memberof DasProp
 */
DAS_API int DasProp_convertTt2k(const DasProp* pProp, int64_t* pBuf, size_t uBufLen);

/** Convert datatime properties to a double based value of units 
 * @memberof DasProp
 */
DAS_API int DasProp_convertTime(const DasProp* pProp, uint64_t* pBuf, size_t uBufLen);

/** Just extract the property strings, don't convert anything 
 * 
 * @returns the number of extracted properties, or a negative error value
 * 
 * @memberof DasProp
 */
DAS_API int DasProp_extractItems(const DasProp* pProp, char** psBuf, size_t uNumStrs, size_t uLenEa);


/** Get a property type code.
 * 
 * Use the values: DASPROP_MULTI_MASK & DASPROP_TYPE_MASK to extract sections
 * 
 * @memberof DasProp
 */
DAS_API ubyte DasProp_type(const DasProp* pProp);

/** Determine if this property has a valid type definition 
 * @memberof DasProp
 */
DAS_API bool DasProp_isValid(const DasProp* pProp);

/** Determine the number of items in a multi valued property 
 * @memberof DasProp
 */
DAS_API int DasProp_items(const DasProp* pProp);

/** A mask to select a property's multiplicity setting.
 * This is useful when interpreting the results of DasProp_type() 
 */
#define DASPROP_MULTI_MASK  0x00000003

#define DASPROP_INVALID     0x00000000  /* A retired property, ignore it */
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
 *
 * @param pProp The property
 * @param uType One of the item type constants, DASPROP_STRING etc.
 *        Multiplicity bits are not masked off, so including them never
 *        matches.
 * @memberof DasProp
 */
DAS_API bool DasProp_isType(const DasProp* pProp, ubyte uType);

/** Returns true if this property has range multiplicity (aka 2 items)
 *
 * A set never counts as a range, even a two element one.
 * @memberof DasProp
 */
DAS_API bool DasProp_isRange(const DasProp* pProp);

/** Returns true if this property has more then one element (aka a set)
 * but the set isn't a bounding range.
 *
 * @memberof DasProp
 */
DAS_API bool DasProp_isSet(const DasProp* pProp);

#ifdef __cplusplus
}
#endif

#endif // _das_prop_h_
