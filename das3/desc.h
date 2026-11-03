/* Copyright (C) 2015-2024 Chris Piker <chris-piker@uiowa.edu>
 *               2004-2006 Jeremy Faden <jeremy-faden@uiowa.edu>
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

/** @file desc.h */

#ifndef _das_desc_h_
#define _das_desc_h_
#include <stdbool.h>

#include <das3/units.h>
#include <das3/util.h>
#include <das3/buffer.h>
#include <das3/prop.h>
#include <das3/array.h>

#ifdef __cplusplus
extern "C" {
#endif

/** enumeration of Descriptor types, used internally for type checking.
 * May have one of the following values:
 *    -# PLANE 
 *    -# PACKET
 *    -# STREAM
 *    -# PHYSDIM
 *    -# DATASET
 */
typedef enum DescriptorType {
	UNK_DESC=0, STREAM=14000, 
   PLANE=14001, PACKET=14002, 
   DATASET=15000, PHYSDIM=15001, 
   VARIABLE=15002,
} desc_type_t;


DAS_API const char* das_desc_type_toStr(desc_type_t dt);

/** @class DasDesc desc.h <das3/desc.h>
 *
 * Base structure for Stream Header Items.
 *  
 * Descriptors have properties associated with them.  Stream Descriptors,
 * Packet Descriptors, and Plane Descriptors are all extensions of this object 
 * type, thus the functions for this structure may be used with any Descriptor
 * structure in the library.
 *
 * Note: <b>Properties Cascade</b>
 *
 * Properties @e cascade in das streams.  Thus if a particular descriptor
 * does not have a particular property then the various getProperty() functions
 * will search parent descriptors for requested property.  The descriptor
 * ownership hierarchy for das stream is:
 * 
 *   - ::DasStream's have 1-N ::DasPkt's or ::DasDs's
 *   - ::DasPkt's have 1-N ::DasPlane's
 *   - ::
 * 
 * @todo Move child relationship into the descriptor base class, parent is there
 *       but the other direction is missing
 */
typedef struct das_descriptor DasDesc;

/** Get the type of this descriptor
 *
 * @param pThis A pointer to a descriptor structure
 *
 * @returns One of the values for the enumeration desc_type_t
 *
 * @memberof DasDesc
 */
DAS_API desc_type_t DasDesc_type(const DasDesc* pThis);

/** Attach application data to a descriptor
 *
 * Works for any descriptor. The library never reads, @b copies or frees
 * this pointer.  All new descriptors, including those made
 * by the *_copy() functions and DasStream_clonePktById(), start with NULL.
 *
 * @param pThis The descriptor
 * @param pUser Anything, the library treats it as an opaque value
 * @memberof DasDesc
 */
DAS_API void DasDesc_setUser(DasDesc* pThis, void* pUser);

/** Get the application data attached by DasDesc_setUser()
 *
 * @param pThis The descriptor
 * @returns The pointer last given to DasDesc_setUser(), or NULL
 * @memberof DasDesc
 */
DAS_API void* DasDesc_getUser(const DasDesc* pThis);

/* @name DasDesc Functions
 * These work for any type of Descriptor, including ::DasPlane ,
 * ::DasPkt, ::DasStream, ::DasDs and ::DasDim.
 * To make your compiler happy you will need to cast Plane, Packet and
 * Stream Descriptor pointers to just the generic type of Descriptor pointer
 * when using these functions. For example:
 * @code
 * DasPkt* pPktDesc;
 * DasDesc_has((Descriptor*)pPktDesc, "SomePropName");
 * @endcode
 * @memberof DasDesc
 */
/* @{ */


/** Print 1-line versions of each property in a descriptor
 *
 * Values longer than 48 bytes are clipped and end in "...".  Same calling
 * convention as DasDs_info(), so the containers can nest it.
 *
 * @param pThis The descriptor whose properties are printed.
 *
 * @param sBuf The output buffer, may be NULL if uLen is 0.
 *
 * @param uLen The size of sBuf in bytes, including room for the terminating
 *        NUL.  Output that does not fit is cut off, but sBuf is always NUL
 *        terminated when uLen > 0 and nothing past sBuf[uLen-1] is touched.
 *
 * @returns The length of the full description, not counting the NUL, as
 *        snprintf() does.  If this is uLen or more the output was cut off.
 *        To size a buffer, call once with (NULL, 0) and allocate the
 *        return value plus one.
 *
 * @param sIndent Prefix for each line, for nesting under a container.
 *
 * @memberof DasDesc
 */
DAS_API int DasDesc_info(
	const DasDesc* pThis, char* sBuf, size_t uLen, const char* sIndent
);


/** Release a descriptor of any type
 *
 * Works for every descriptor: cast a ::DasStream, ::DasPkt, ::DasPlane,
 * ::DasDs, ::DasDim or ::DasVar pointer to DasDesc*.  Deletes the object and
 * everything it owns.  Variables are reference counted, for them this drops
 * one reference and the last one deletes it.
 *
 * Only call this on a descriptor you own: one from a new_ or copy function,
 * or a variable you have called inc_DasVar() on.  Pointers handed out by
 * getters belong to their container.  A descriptor still attached to a parent
 * leaves a dangling pointer in that parent, detach it first (for example with
 * DasStream_rmDesc()).
 *
 * @param pThis The descriptor, NULL is ignored
 * @memberof DasDesc
 */
DAS_API void del_DasDesc(DasDesc* pThis);

/** Resets the property count to 0, but frees no memory 
 * 
 * Call this to re-use a descriptor.
 * @memberof DasDesc
 * */
DAS_API void DasDesc_clearProps(DasDesc* pThis);

/** Check to see if two descriptors contain the same properties
 * Note, the order of the properties may be different between the descriptors
 * but if the contents are the same then the descriptors are considered to be
 * equal.
 * 
 * Note that parent descriptor properties are not checked when handling the
 * comparison. 
 * @todo maybe check parents too.
 * 
 * @param pThis The first descriptor
 * @param pOther The second descriptor
 * @memberof DasDesc
 */
DAS_API bool DasDesc_equals(const DasDesc* pThis, const DasDesc* pOther);

/** The the parent of a Descriptor
 * 
 * Plane descriptors are owned by packet descriptors and packet descriptors are
 * owned by stream descriptors.  This function lets you craw the ownership 
 * hierarchy
 * 
 * @param pThis
 * @return The owner of a descriptor, or NULL if this is a top level descriptor,
 *         (i.e. a Stream Descriptor)
 * @memberof DasDesc
 */
DAS_API const DasDesc* DasDesc_parent(const DasDesc* pThis);

/** Get the number of properties in a descriptor.
 * 
 * Descriptor's have a hierarchy.  In general when a property is requested, if
 * a given Descriptor does not have a property the request is passed to the 
 * parent descriptor.  This function @b only returns the number of properties 
 * in the given descriptor.  It does not include properties owned by parents 
 * or ancestors.
 * 
 * This is useful when iterating over all properties in a descriptor.  
 * @see DasDesc_getNameByIdx()
 * @see DasDesc_getValByIdx()
 * @see DasDesc_getTypeByIdx()
 * 
 * @param pThis A pointer to the descriptor to query
 * @return The number of properties in this, and only this, descriptor.
 * @memberof DasDesc
 */
DAS_API size_t DasDesc_length(const DasDesc* pThis);

/** Get a property name by an index 
 * 
 * This is useful when iterating over all properties in a Descriptor.  Only
 * valid properties owed by a descriptor are queried in this manner.  Parent
 * descriptors are not consulted.
 * 
 * @see DasDesc_length()
 * @param pThis A pointer to the descriptor to query
 * 
 * @param uIdx The index of the property, will be a value between 0 and
 *        the return value from Desc_length().  For efficient storage
 *        properties that have been erased or over-written are left in
 *        place internally and just marked as invalid.
 * 
 * @return A pointer the requested property, or NULL if the property is 
 *       not valid.  A NULL return does not mean than the next higher
 *       index has an invalid property
 * 
 * @see DasDesc_getProp() for how long the returned pointer stays valid
 * @memberof DasDesc
 */
DAS_API const DasProp* DasDesc_getPropByIdx(const DasDesc* pThis, size_t uIdx);

/** Get a property name by an index 
 * 
 * This is useful when iterating over all properties in a Descriptor.  Only
 * properties owed by a descriptor are queried in this manner.  Parent
 * descriptors are not consulted.
 * 
 * @see DasDesc_length()
 * @param pThis A pointer to the descriptor to query
 * @param uIdx The index of the property, will be a value between 0 and
 *        the return value from Desc_length()
 * @return A pointer the requested property name or NULL if there is no
 *        property at the given index.
 * @see DasDesc_getProp() for how long the returned pointer stays valid
 * @memberof DasDesc
 */
DAS_API const char* DasDesc_getNameByIdx(const DasDesc* pThis, size_t uIdx);

/** Get a property value by an index 
 * 
 * This is useful when iterating over all properties in a Descriptor.  Only
 * properties owned by a descriptor are queried in this manner.  Parent
 * descriptors are not consulted.
 * 
 * @see DasDesc_length()
 * @param pThis A pointer to the descriptor to query
 * @param uIdx The number of the property, will be a value from 0 and 1 less 
 *        than the return value from Desc_length()
 * @return A pointer the requested property value or NULL if there is no
 *        property at the given index.
 * @see DasDesc_getProp() for how long the returned pointer stays valid
 * @memberof DasDesc
 */
DAS_API const char* DasDesc_getValByIdx(const DasDesc* pThis, size_t uIdx);

/** Get a data type of a property by an index 
 * @memberof DasDesc
 */
DAS_API const char* DasDesc_getTypeByIdx(const DasDesc* pThis, size_t uIdx);


/** Generic property setter
 *
 * All properties are stored internally as strings.  The various typed
 * Desc_setProp* functions all call this function after converting their
 * arguments to strings. 
 * 
 * @warning To insure that the string to type conversions are consistent it is
 * strongly recommended that you use one of the typed functions instead of this
 * generic version unless you have no choice.
 * 
 * @param pThis The Descriptor to receive the property
 * 
 * @param sType The Type of property.  This value is passed down to
 *    DasDesc_flexSet(), see that function for a list of known values.
 * 
 * @param sName The property name.  For das2 & das3 this can't contain spaces.
 * 
 * @param sVal The value, which may be anything including NULL
 * 
 * @return 0 on success or a positive error code if there is a problem.
 * @memberof DasDesc
 */
DAS_API DasErrCode DasDesc_set(
	DasDesc* pThis, const char* sType, const char* sName, const char* sVal
);

/** Create or set a existing property
 *
 * @param pThis The Descriptor to receive the property
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
 * @param uType An alternate and more efficient method of specifying
 *    the property type.  If sType is NULL, this is read instead.
 *    To set uType or together one constant from each set below.
 *
 *    Item type: DASPROP_STRING, DASPROP_BOOL, DASPROP_INT, DASPROP_REAL,
 *       DASPROP_DATETIME
 *
 *    Multiplicity: DASPROP_SINGLE, DASPROP_RANGE, DASPROP_SET
 *
 * @param sName The name of the property, can be no longer then 126
 *              bytes. This is a looser restriction then associated XSDs.
 *
 * @param sVal  The data value, can be no longer then 130,943 bytes.
 *
 * @param cSep  For array values, this (in addition to possible whitespace)
 *              is the separator between values.  Ignored if Multiplicity
 *              is not DASPROP_SET.
 *
 * @param units The units for this value.  If the type is Datum or DatumRange
 *              this value will be ignored, otherwise if this value is NULL
 *              then UNIT_DIMENSIONLESS will be assigned.
 *
 * @param nStandard  One of DASPROP_DAS1, _DAS2 or _DAS3.  For das2 & 3,
 *              property names may only consist of the characters
 *              [a-z][A-Z][0-9] and '_'.
 *
 * @return 0 on success or a positive error code if there is a problem.
 * @memberof DasDesc
 */
DAS_API DasErrCode DasDesc_flexSet(
   DasDesc* pThis, const char* sType, ubyte uType, const char* sName,
   const char* sVal, char cSep, das_units units, int nStandard
);

/** Overwrite, or copy-in a fully formatted property 
 * 
 * * @memberof DasDesc
 */
DAS_API DasErrCode DasDesc_setProp(DasDesc* pThis, const DasProp* pProp);

/** Get the type string for a property 
 * 
 * * @memberof DasDesc
 */
DAS_API const char* DasDesc_getType(const DasDesc* pThis, const char* sName);

/** Get the units of a property
 *
 * @returns the property's units, or UNIT_DIMENSIONLESS when the property is
 *          missing or carries none, so the two cases can't be told apart.
 *          Check DasDesc_has() first if that matters.
 *
 * @memberof DasDesc
 */
DAS_API das_units DasDesc_getUnits(const DasDesc* pThis, const char* sName);

/** Get a raw property string
 * 
 * This function performs no transforms from the backing store.
 * @see DasDesc_getProp() for how long the returned pointer stays valid
 * @memberof DasDesc
 */
DAS_API const char* DasDesc_get(const DasDesc* pThis, const char* sName);

/** Determine if a property is present in a Descriptor or it's ancestors.
 *
 * @param pThis the descriptor object to query
 * @param sName  the name of the property to retrieve.
 * @returns true if the descriptor or one of it's ancestors has a property
 *          with the given name, false otherwise.
 * @memberof DasDesc
 */
DAS_API bool DasDesc_has(const DasDesc* pThis, const char* sName );

/** Does this descriptor alone have this property
 * 
 * The question does not cascade up the tree
 * @memberof DasDesc
 */
DAS_API bool DasDesc_hasLocal(const DasDesc* pThis, const char* sName);


/** Get a property if present in descriptor or it's parent (das3)
 * 
 * @param pThis the descriptor object to query
 * @param sName  the name of the property to retrieve.
 * @returns a the property, if present here or in a parent descriptor,
 *        NULL otherwise.  The pointer is into the descriptor's packed
 *        property storage: setting any property on the descriptor that
 *        holds it may move that storage, so do not keep the pointer across
 *        DasDesc_set*() calls on this descriptor or its parents.  Calls
 *        that copy the value out, such as DasDesc_getInt(),
 *        DasDesc_getDouble() or DasDesc_getStrRng(), avoid the issue.
 */
DAS_API const DasProp* DasDesc_getProp(const DasDesc* pThis, const char* sName);

/** Get a property if present in this descriptor only (das3)
 * 
 * In das3 property cascades don't make as much sense.  The label for a
 * particular physical dim axis is not the stream label.  Clients may
 * want a property for just this object.
 * 
 * @param pThis the descriptor object to query
 * @param sName  the name of the property to retrieve.
 * @returns a the property, if present here NULL otherwise
 * @see DasDesc_getProp() for how long the returned pointer stays valid
 */
DAS_API const DasProp* DasDesc_getLocal(const DasDesc* pThis, const char* sName);

/** Remove a property from a descriptor, if preset
 *
 * It is safe to call this function for properties not present on the 
 * descriptor, it simply does nothing and returns false.
 *
 * @returns false if the property wasn't present to begin with, true
 *          otherwise
 * @memberof DasDesc
 * */
DAS_API bool DasDesc_remove(DasDesc* pThis, const char* sName);

/** read the property of type String named sName.
 * @see DasDesc_getProp() for how long the returned pointer stays valid
 * @memberof DasDesc
 */
DAS_API const char* DasDesc_getStr(const DasDesc* pThis, const char* sName);


/** Get a multi-valued string property
 * 
 * Some properties, especially those from DSDF files, contain multiple 
 * string values in a single field separated by pipe "|" characters.
 * For example:
 *
 *  data_01 = 'efield | Electric field intensity | V m**-1'
 *
 * This function breaks these values into multiple strings without 
 * requiring heap memory.
 *
 * Output bytes are copied into the given pBuf.  Then null values
 * are then written over all leading and trailing whitespace for each
 * element as well as the pipe characters.
 *
 * Finally a pointer to each starting string is copied into psVals.
 * If an element contains not data, for example:
 *
 *   coord_01 = 'frequency | | Hz'
 *
 * then the corresponding character pointer will be NULL, but the
 * number of character pointers is unchanged.
 *
 * @param[in] pThis the descriptor to query
 *
 * @param[in] sName the name of the property to retrieve
 *
 * @param[out] pBuf will hold the full output property data
 *
 * @param[in] uBufSz the maximum number of bytes to copy out including
 *            the terminating null character.
 *
 * @param[out] psVals will hold pointers to the start of each
 *            property value.  If a value is empty, the corresponding
 *            pointer is null.
 *
 * @param[in] uMaxVals the maximum number of string pointers to
 *            write to psVals.
 *
 * @returns The number of string values for this property, which is
 *          zero if a property with the given name is not present.
 *
 * @memberof DasDesc
 *
 * @see DasDesc_getStr to retrieve the original property value in
 * a single buffer unaltered.
 */
DAS_API size_t DasDesc_getStrAry(
	DasDesc* pThis, const char* sName, char* pBuf, size_t uBufSz,
	char** psVals, size_t uMaxVals
);

/** Get string array with given separator 
 *
 * This is just a helper, though you can use it if you like to specify a
 * a separator character.  The combination '\\SEP' is treated as a literal
 * SEP and does not break a field.  Literal '\\' characters have no meaning
 * unless followed by a SEP character.
 *
 * @see DasDesc_getStrAry
 */
DAS_API size_t DasDesc_getArray(
	DasDesc* pThis, const char* sName, char cSep,
	char* pBuf, size_t uBufSz, char** psVals, size_t uMaxVals
);


/** SetProperty methods add properties to any Descriptor (stream,packet,plane). 
 * The typed methods (e.g. setPropertyDatum) property tag the property with
 * a type so that it will be parsed into the given type.
 * @memberof DasDesc
 */
DAS_API DasErrCode DasDesc_setStr(
	DasDesc* pThis, const char* sName, const char* sVal
);

/** Set a string property in the manner of sprintf
 * @memberof DasDesc
 */
DAS_API DasErrCode DasDesc_vSetStr(
	DasDesc* pThis, const char* sName, const char* sFmt, ...
);


/** Read the property of type double named sName.
 * The property value is parsed using sscanf.
 * @memberof DasDesc
 */
DAS_API double DasDesc_getDouble(const DasDesc* pThis, const char* sName);

/** Set property of type double.  
 * @memberof DasDesc
 */
DAS_API DasErrCode DasDesc_setDouble(
	DasDesc* pThis, const char* sName, double value
);

/** Get the a numeric property in the specified units.
 * 
 * Descriptor properties my be provided as Datums.  Datums are a double value 
 * along with a specified measurement unit.   
 * @param pThis The Descriptor containing the property in question.
 * @param sName The name of the property to retrieve.  
 * @param units The units of measure in which the return value will be
 *        represented.  If the property value is stored in a different set of
 *        units than those indicated by this parameter than the output will be
 *        converted to the given unit type.  
 * @returns The converted value or @b DAS_FILL_VALUE if conversion to the desired
 *        units is not possible.
 * @memberof DasDesc
 */
DAS_API double DasDesc_getDatum(
	DasDesc* pThis, const char* sName, das_units units 
);

/** Set property of type Datum (double, UnitType pair)
 *
 * If a property with this name already exists it is 1st deleted and then the
 * new property is added in its place.
 *
 * @param pThis The descriptor to receive the property
 * @param sName The name of the property to set
 * @param rVal The numeric value of the property
 * @param units The units of measure for the property
 *
 * @memberof DasDesc
 */
DAS_API DasErrCode DasDesc_setDatum(
   DasDesc* pThis, const char* sName, double rVal, das_units units
);

/** Get the values of an array property
 * 
 * Space for the array is allocated by getDoubleArrayFromString.
 * and *pNumItems is set to indicate the size of the array.
 *
 * @param[in] pThis the descriptor object to query
 * @param[in] sName the name of the property to retrieve
 * @param[out] pNumItems a pointer to an integer receiving the number of
 *        values in the returned array.
 *
 * @returns A pointer to a double array allocated on the heap.  It is the
 *          caller's responsibility to depose of the memory when it is no 
 *          longer needed.  If the named property doesn't exist 
 *          the program exits.  
 * 
 * @see DasDesc_has()
 *
 * @memberof DasDesc
 */
DAS_API double* DasDesc_getDoubleAry(
   DasDesc* pThis, const char* sName, int* pNumItems
);

/** Set the property of type double array.
 * @memberof DasDesc
 */
DAS_API DasErrCode DasDesc_setDoubleArray(
	DasDesc* pThis, const char* sName, int nItems, double* pValues
);

/** Get a property integer value
 *
 * @param pThis the descriptor object to query
 * @param sName the name of the property to retrieve
 * @returns The value of the named property or exits the program if the
 *          named property doesn't exist in this descriptor.
 * 
 * @see DasDesc_has()
 * @memberof DasDesc
 */
DAS_API int DasDesc_getInt(const DasDesc* pThis, const char* sName);

/** Set the property of type int.
 * @memberof DasDesc
 */
DAS_API DasErrCode DasDesc_setInt(DasDesc* pThis, const char* sName, int nVal);

/** Get a property boolean value
 * 
 * @param pThis the descriptor object to query
 * @param sName the name of the property to retrieve
 * @returns True if the value is "true", or any positive integer, false otherwise.
 * @memberof DasDesc
 */
DAS_API bool DasDesc_getBool(DasDesc* pThis, const char* sName);

/** Set a boolean property
 * Encodes the value as either the string "true" or the string "false"
 * @param pThis The descriptor to receive the property
 * @param sName the name of the property
 * @param bVal either true or false.
 */
DAS_API DasErrCode DasDesc_setBool(
	DasDesc* pThis, const char* sName, bool bVal
);

/** Set property of type DatumRange (double, double, UnitType triple)
 * @memberof DasDesc
 */
DAS_API DasErrCode DasDesc_setDatumRng(
	DasDesc* pThis, const char* sName, double beg, double end, das_units units
);

/** Get a property of type DatumRange with unconverted strings. 
 *
 * This version is handy if you just want to know the intrinsic units of 
 * the range without converting the values to some specific type of double
 * value.
 * @memberof DasDesc
 */
DAS_API DasErrCode DasDesc_getStrRng(
	DasDesc* pThis, const char* sName, char* sMin, char* sMax, 
	das_units* pUnits, size_t uLen
);

/** Set the property of type float array.
 * Note the array is cast to a double array before encoding.
 * @memberof DasDesc
 */
DAS_API DasErrCode DasDesc_setFloatAry(
	DasDesc* pThis, const char* sName, int nItems, float* pValues
);

/** Deepcopy properties into a descriptor
 * @param pThis the descriptor to receive a copy of the properties
 * @param pOther the descriptor with the properties to be copied.
 * @memberof DasDesc
 */
DAS_API void DasDesc_copyIn(DasDesc* pThis, const DasDesc* pOther);


/** Does this descriptor hold at least one valid property?
 * @memberof DasDesc
 */
DAS_API bool DasDesc_hasAnyProps(const DasDesc* pThis);


/* * @} */

#ifdef __cplusplus
}
#endif

#endif /* _das_desc_h_ */
