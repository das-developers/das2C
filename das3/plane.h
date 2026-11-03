/* Copyright (C) 2017-2024  Chris Piker <chris-piker@uiowa.edu>
 *                    2004  Jeremy Faden <jeremy-faden@uiowa.edu>
 *
 * This file is part of das2C, the Core Das C Library.
 * 
 * das2C is free software; you can redistribute it and/or modify it under
 * the terms of the GNU Lesser General Public License version 2.1 as published
 * by the Free Software Foundation.
 *
 * das2C is distributed in the hope that it will be useful, but WITHOUT ANY
 * WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS
 * FOR A PARTICULAR PURPOSE.  See the GNU Lesser General Public License for
 * more details.
 *
 * You should have received a copy of the GNU Lesser General Public License
 * version 2.1 along with das2C; if not, see <http://www.gnu.org/licenses/>. 
 */

/** @file plane.h Header for Plane Descriptor Objects */

#ifndef _das_plane_h_
#define _das_plane_h_

#include <math.h>
#include <stdbool.h>

#include <das3/buffer.h>
#include <das3/desc.h>
#include <das3/units.h>
#include <das3/enc.h>
#include <das3/datum.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ************************************************************************* */
/** An enumeration of packet data plane types.
 * 
 * A Das2 packet contains one dependent value set from each of it's Planes.  
 * The plane types are:
 * 
 * X - Data defined within an \<x\> plane, typically these are time values.
 *     There is one value for each X plane in a Das2 data packet
 * 
 * Y - Data defined within a \<y\> plane, this would be line-plot data.
 *     There is one value for each Y plane in a Das2 data packet.  This value
 *     is correlated with the \<x\> plane value in the packet.
 *
 * Z - Data defined within a \<z\> plane.  There is one value for each Z plane
 *     in a Das2 data packet.  The Z value is correlated with both the \<x\>
 *     plane value and the \<y\> plane value.
 * 
 * YScan - Our most common data type, values are defined by a \<yscan\> plain
 *     tag.  There are 1-N values in each YScan plane for each das2 data
 *     packet.  All YScan values from a single data packet are correlated with
 *     the X value provide in the \<x\> plane. 
 */
typedef enum plane_type {Invalid=-1, X=2001, Y=2003, Z=2004, YScan=2012
} plane_type_t;


typedef enum ytag_spec {ytags_none=0, ytags_list=1, ytags_series=2} ytag_spec_t;
		
/** Get the plane type for a das2 element name
 *
 * @param sType An element name such as "x", "y", "yscan" or "z"
 * @returns the matching plane_type_t, or Invalid after a das_error() call
 *          if sType is not one
 */
DAS_API plane_type_t das_plane_type_fromStr(const char* sType);

/** Get the das2 element name for a plane type
 *
 * @param type A plane type
 * @returns a constant string such as "x" or "yscan"
 */
DAS_API const char* das_plane_type_toStr(plane_type_t type);

/* ************************************************************************* */
/** @class DasPlane plane.h <das3/plane.h>
 *
 * Describes a data plane within a packet type.
 * Each packet of Das2 stream data contains values for one or more \e planes.
 * To illustrate this take a look at the packet header in the X Tagged Stream Example
 * from the <a href="http://www-pw.physics.uiowa.edu/das2/Das2.2-ICD-2014-11-17.pdf">
 * Das2 ICD</a>.
 * @code
 * [00]000088<stream>
 * <properties DatumRange:xRange="2013-001T01:00:00 to 2013-01:10:00"/>
 * </stream>
 * [01]000424<packet>
 * @endcode
 * @code
 *   <x type="time23" units="us2000"></x>
 * @endcode
 * @code
 *   <y type="ascii11" name="radius" units=""><properties String:yLabel="R!DE!N" /></y>
 * @endcode
 * @code
 *   <y type="ascii11" name="mag_lat" units=""><properties String:yLabel="MLat" /></y>
 * @endcode
 * @code
 *   <y type="ascii11" name="mag_lt" units=""><properties String:yLabel="MLT" /></y>
 * @endcode
 * @code
 *   <y type="ascii11" name="l_shell" units=""><properties String:yLabel="L" /></y>
 * @endcode
 * @code
 * </packet>
 * :01:2013-001T01:00:00.000  5.782e+00 -1.276e+01  3.220e+00  6.079e+00
 * :01:2013-001T01:01:00.000  5.782e+00 -1.274e+01  3.232e+00  6.077e+00
 * :01:2013-001T01:02:00.000  5.781e+00 -1.272e+01  3.244e+00  6.076e+00
 * @endcode
 *
 * Each @b \<x\> and @b \<y\> element inside the \<packet\> element above is a 
 * serialized DasPlane.  The point of a DasPlane structure is to:
 * 
 *    -# Hold the definition of a single plane within a single packet type. 
 *    -# Assist PacketDescriptors with serializing and de-serializing data packets.
 *
 * Just as an isolated plane without a parent \<packet\> tag does not make sense 
 * in a Das2 Stream, DasPlane structures don't have much use on their own.
 * They are typically owned by a PacketDescriptor and accessed via the
 * ::DasPkt:getPlane function
 *
 * @extends DasDesc 
 * @nosubgrouping
 * @ingroup streams
 */
typedef struct das_plane DasPlane;

/** Creates a Plane Descriptor with mostly empty settings.
 * @memberof DasPlane
 */
DAS_API DasPlane* new_DasPlane_empty(void);

/** Creates a new X,Y or Z plane descriptor
 *
 * @param pt The ::plane_type_t, must be one of: 
 *    - X - Independent Values
 *    - Y - Dependent or Independent Values
 *    - Z - Dependent Values
 *    - YScan - Dependent Values
 * 
 * @param sGroup the name for the data group this plane belongs to, may be the
 *        empty string ""
 * @param pType an encoding object for the new plane.  The DasPlane will
 *        take ownership of this object and free it when the plane is
 *        deleted.
 * @param units The units of measurement for data in this plane.
 * @memberof DasPlane
 * @returns A pointer to new DasPlane allocated on the heap.
 */
DAS_API DasPlane* new_DasPlane( 
	plane_type_t pt, const char* sGroup, DasEncoding* pType, das_units units
);

/** Creates a new \<yscan\> plane descriptor
 * 
 * A \<yscan\> plane is an array of Z-values taken along Y for each X tag. 
 * Spectrogram amplitudes are commonly transmitted in this plane type.  In
 * that case <x\> contains times, \<yscan\> values are amplitudes, and the
 * frequencies are held in the yTags attribute for the plane.
 *
 * @param sGroup the name for the data group this plane belongs to, may be the
 *        empty string ""
 *
 * @param pZType an encoding object for the new plane.  The DasPlane will
 *        take ownership of this object and free it when the plane is
 *        deleted.
 * @param zUnits The units of measurement for Z-axis data in this plane.
 * @param uItems The number of data values in each packet of this plane's 
 *        data.  i.e. the number of yTags.
 * @param pYType The encoding for the Y values.  If NULL, a simple encoding
 *        will be defined with the format string "%.6e"
 * @param pYTags The YTags for the new plane, if NULL the value index will be
 *        used as the YTags.  I.e. the Y axis values will be 0, 1, 2, ... 
 *        uItems - 1
 * @param yUnits The units for yTag values.
 *
 * @returns A pointer to new DasPlane allocated on the heap.
 * @memberof DasPlane
 */
DAS_API DasPlane* new_DasPlane_yscan(
	const char* sGroup, DasEncoding* pZType, das_units zUnits, size_t uItems,
	DasEncoding* pYType, const double* pYTags, das_units yUnits
);

/** Creates a new \<yscan\> plane descriptor using a yTag series
 * 
 * A \<yscan\> plane is an array of Z-values taken along Y for each X tag. 
 * Waveform packets are commonly transmitted in this plane type.  In
 * that case <x\> contains first sample times, \<yscan\> values are amplitudes,
 * and the time offsets are defined by the YTag series attributes of this plane
 *
 * @param sGroup the name for the data group this plane belongs to, may be the
 *        empty string ""
 *
 * @param pZType an encoding object for the new plane.  The DasPlane will
 *        take ownership of this object and free it when the plane is
 *        deleted.
 * @param zUnits The units of measurement for Z-axis data in this plane.
 * @param uItems The number of data values in each packet of this plane's 
 *        data.  i.e. the number of yTags.
 * @param yTagInter the interval between values in the yTag series
 * @param yTagMin the initial value of the series.  Use DAS_FILL_VALUE to have 
          the starting point set automatically using yTagMax.
 * @param yTagMax the final value of the series.  Use DAS_FILL_VALUE to have 
          the ending point set automatically using yTagMin.
 * @param yUnits The units for yTag values.
 *
 * @returns A pointer to new DasPlane allocated on the heap.
 * @memberof DasPlane
 */
DAS_API DasPlane* new_DasPlane_yscan_series(
	const char* sGroup, DasEncoding* pZType, das_units zUnits, size_t uItems,
   double yTagInter, double yTagMin, double yTagMax, das_units yUnits
);

/* Creates a new plane descriptor from attribute strings
 * 
 * Unlike the other top-level descriptor objects in a Das2 Stream planes
 * are not independent XML documents.  This constructor is called from
 * the new_DasPkt_xml constructor to build plane descriptor object from
 * keyword / value style string lists.  The top level XML parsing is handled
 * by the DasPkt class.
 *
 * @param pParent the Properties parent for the new plane descriptor, this
 *        is always a DasPkt object pointer.
 *
 * @param pt The ::PlaneType, must be one of: 
 *    - X
 *    - Y 
 *    - YScan
 *    - Z
 *
 * @param attrs A null terminated array of strings.  It is assumed that
 *        the strings represent keyword value pairs.  i.e the first string
 *        is a setting name, such as 'units' the second string is the the
 *        value for 'units'.  Strings are processed in pairs until a NULL
 *        pointer is encountered.
 * 
 * @todo When encountering ASCII times, change the units to us2000 to better
 *       preserve precision for fine times for down stream processors.
 *  
 * @returns A pointer to new DasPlane allocated on the heap or NULL on an 
 *          error
 * @memberof DasPlane
 */
DAS_API DasPlane* new_DasPlane_pairs(
	DasDesc* pParent, plane_type_t pt, const char** attrs
);

/** Copy constructor for planes
 * Deep copy one a plane except for the parent id.
 * @param pThis The plane descriptor to copy
 * @returns A new plane descriptor allocated on the heap.  All properties of
 *         the plane descriptor are duplicated as well. 
 * @memberof DasPlane
 */
DAS_API DasPlane* DasPlane_copy(const DasPlane* pThis);


/** Check to see if two plane descriptors describe the same output
 * 
 * Two plane descriptors are considered to be the same if they result in an
 * equivalent packet header definition.  This means the same plane type with
 * the same number of items and the same ytags along with the same data encoding
 * and units.  
 * 
 * The following items are not checked for equivalency:
 *   - The properties
 *   - The current data values (if any)
 *   - The user data pointer
 * 
 * Passing the same non-NULL pointer twice to this function will always
 * result in a return of true, as planes are equivalent to themselves.
 * 
 * @param pThis The first plane descriptor
 * @param pOther the second plane descriptor
 * 
 * @returns true if the two are equivalent, false otherwise.  
 * @memberof DasPlane
 */
DAS_API bool DasPlane_equivalent(const DasPlane* pThis, const DasPlane* pOther);

/** Get a plane's type
 *
 * @param pThis The plane descriptor to query
 * @return The plane type, which is one of X, Y, YScan or Z
 * @memberof DasPlane
 */
DAS_API plane_type_t DasPlane_getType(const DasPlane* pThis);

/** Get the number of items in a plane
 * YScan planes have a variable number of items, for all other types this 
 * function returns 1
 * @param pThis the PlaneDiscriptor
 * @returns the number of items in plane 
 * @memberof DasPlane
 */
DAS_API size_t DasPlane_getNItems(const DasPlane* pThis);

/**  Set the number of items in a plane
 * 
 * @warning Calling this function with a different size then the current 
 * number of items in a plane will cause a re-allocation of the internal
 * data values memory buffer.  Any pointers returned previously by 
 * DasPlane_getValues() or DasPlane_getYTags() will be invalidated.
 * 
 * @warning Always call DasPlane_setYTags() or DasPlane_setYTagSeries()
 * after changing the number of items in a plane!  Failure do to so has 
 * undefined results. 
 * 
 * @param pThis The plane, which must be of type YScan
 * @param nItems the new number of items in the plane.
 */
DAS_API void DasPlane_setNItems(DasPlane* pThis, size_t nItems);


/** Get the a value from a plane
 *
 * Get the current value for an item in a plane, Note that X, Y and Z
 * planes only have one item.  
 * @see DasEnc_write() for converting doubles to time strings.
 * 
 * @param pThis The Plane descriptor object
 * @param uIdx the index of the value to retrieve, this is always 0 for 
 *        X, Y, and Z planes.
 * @returns the current value or DAS_FILL_VALUE in no data have been read or set.
 * @memberof DasPlane
 */
DAS_API double DasPlane_getValue(const DasPlane* pThis, size_t uIdx);


/** Get a datum from a plane
 *
 * Get the i-th datum from a plane.  Datums are a value plus a unit.
 * 
 * @param pD A pointer to a datum structure that will be initialized 
 *    the value at uIdx and the plane's data unit type.
 *
 * @param pThis The plane descriptor object
 *
 * @param uIdx The the index of the value to retrieve, this is always 0 for 
 *        X, Y, and Z planes.
 *
 * @returns a pointer to the given datum, which will not be initialized if
 *        uIdx is out of range.
 * @memberof DasPlane
 */
DAS_API const das_datum* DasPlane_getDatum(
	const DasPlane* pThis, size_t uIdx, das_datum* pD
);
 

/** Set a current value in a plane
 * 
 * Set a current value for an item in a plane.  Note X, Y and Z
 * planes only have one item.
 * @param pThis The plane in question
 * @param uIdx the index of the value to set.  Only YScan planes have data
 *        at indices above 0.
 * @param value The new value
 * @returns 0 if successful or a positive error number otherwise.
 * @memberof DasPlane
 */
DAS_API DasErrCode DasPlane_setValue(DasPlane* pThis, size_t uIdx, double value);

/** Set a single time value in a plane 
 * 
 * Manually sets a current value for a plane instead of decoding it from an
 * input stream.  The given time string is converted to a broken down time
 * using the parsetime function from daslib and then the broken down time is
 * converted to a double in the units specified for this plane.
 * 
 * @param pThis The plane to get the value
 * @param sTime a parse-able time string
 * @param idx The index at which to write the value, for X, Y and Z planes 0 is
 *        the only valid index.
 * @returns 0 if successful or a positive error number otherwise.  Attempting
 *        to set a time value for planes who's units are not time is an error.
 * 
 * @memberof DasPlane
 */
DAS_API DasErrCode DasPlane_setTimeValue(
	DasPlane* pThis, const char* sTime, size_t idx
);

/** Get the data value encoder/decoder object for a plane
 * The encoder returned via this pointer can be mutated and any changes will
 * be reflected in the next data write for the plane.
 * @param pThis The data plane in question
 * @return The current encoder for this plane.  You may change fields within
 *         the returned encoder but do not free() the return value.
 * @memberof DasPlane
 */
DAS_API DasEncoding* DasPlane_getValEncoder(DasPlane* pThis);

/** Set the data value encoder/decoder object for a plane
 * The previous encoder's memory is returned the heap via free().
 * 
 * @param pThis The data plane in question
 * @param pEnc A pointer to the new encoder structure, the plane takes ownership
 *        of it's memory.  This value must not be null
 * @memberof DasPlane
 */
DAS_API void DasPlane_setValEncoder(DasPlane* pThis, DasEncoding* pEnc);

/** Get a pointer to the current set of values in a plane.
 * 
 * Planes are basically wrappers around a double array with some encoding and
 * decoding information.  This function provides a pointer to the value array
 * for the plane.
 * 
 * @param[in] pThis The plane in question
 * @returns A pointer to the current plane's data, or NULL if neither
 *          DasPlane_setValue() or DasPlane_decode() have been called.
 * 
 * @memberof DasPlane
 */
DAS_API const double* DasPlane_getValues(const DasPlane* pThis);


/** Set all the current values for a plane
 * @see DasPlane_getNItems()
 * 
 * @param pThis The plane to receive the values
 * @param pData A pointer to an array of doubles that is the same length as 
 *        the number of items in this plane.  Values are copied in, no
 *        references are kept to the input pointer after the function completes
 * @memberof DasPlane
 */
DAS_API void DasPlane_setValues(DasPlane* pThis, const double* pData);


/** Returns the fill value identified for the plane.
 * Note: If the value has not been explicitly specified, then the canonical
 * value is used.
 * @memberof DasPlane
 */
DAS_API double DasPlane_getFill(const DasPlane* pThis );

/** Returns non-zero if the value is the fill value identified 
 * for the data plane.
 * @memberof DasPlane
 */
DAS_API bool DasPlane_isFill(const DasPlane* pThis, double value);

/** Identify the double fill value for the plane.
 * @memberof DasPlane
 */
DAS_API void DasPlane_setFill( DasPlane* pThis, double value );
									 
/** Get the data group of a plane
 * @returns the group of the plane, or "" if it is not specified.
 * @memberof DasPlane
 */
DAS_API const char* DasPlane_getName(const DasPlane* pThis );


/** Set the data group of a plane
 * @param pThis The plane descriptor to regroup
 * @param sName the new data group for the plane, will be copied into
 *         internal memory
 * @memberof DasPlane
 */
DAS_API void DasPlane_setName(DasPlane* pThis, const char* sName);

 
/** Get the units of measure for a plane's packet data
 * @returns the units of the data values in a plane
 * @memberof DasPlane
 */
DAS_API das_units DasPlane_getUnits(const DasPlane* pThis );

/** Set the unit type for the plane data
 * 
 * All Das2 Stream values are stored as doubles within the library the units
 * property indicates what these doubles mean
 * 
 * @param pThis The Plane to alter
 * @param units The new units setting.  Note this value may be NULL in which
 *        case the Plane will be set to UNIT_DIMENSIONLESS.
 * @memberof DasPlane
 */
DAS_API void DasPlane_setUnits(DasPlane* pThis, das_units units);

/** Get Y axis units for a 2-D plane
 * @returns the Units of the YTags of a \<yscan\> plane.  
 * @memberof DasPlane
 */
DAS_API das_units DasPlane_getYTagUnits( DasPlane* pThis );

/** Set the YTag units for a YScan plane
 * 
 * @param pThis The plane, which must be of type YScan.
 * @param units The new units
 */
DAS_API void DasPlane_setYTagUnits(DasPlane* pThis, das_units units);


/** Get the storage method for yTag values
 *
 * The 2nd dimension of a yScan plane may have data values associated 
 * with each index point.  These values can be specified individually
 * or by simply providing an interval between point and the value of
 * the 0th index.  Use this function to determine which method is
 * used.
 * @memberof DasPlane
 */
DAS_API ytag_spec_t DasPlane_getYTagSpec(const DasPlane* pThis);

/** Get the interval between yTags, when they are a series
 * @returns the interval, or DAS_FILL_VALUE unless DasPlane_getYTagSpec() is
 *          ytags_series
 * @memberof DasPlane
 */
DAS_API double DasPlane_getYTagInterval(const DasPlane* pThis);

/** Get the first yTag value, when they are a series
 * @returns the series start, or DAS_FILL_VALUE unless DasPlane_getYTagSpec()
 *          is ytags_series
 * @memberof DasPlane
 */
DAS_API double DasPlane_getYTagMin(const DasPlane* pThis);

/** Get Y axis coordinates for a 2-D plane of data.
 * @returns an array of doubles containing the yTags for the YSCAN plane 
 *          or null if yTags are just a simple series.
 *
 * @see DasPlane_getOrMakeYTags() for a function that always creates a set
 *      of YTags
 *
 * @see DasPlane_getYTagInterval()
 * @memberof DasPlane
 */
DAS_API const double* DasPlane_getYTags(const DasPlane* pThis);


/** Get Y tags as an array regardless of the storage type
 * If a yTags array is constructed via this method it is cleaned up when
 * the plane destructor is called.
 *
 * @see DasPlane_getYTagSpec() getYTags() 
 * @memberof DasPlane
 */
DAS_API const double* DasPlane_getOrMakeYTags(DasPlane* pThis);


/** Provide a new set of yTag values to a yScan plane
 * 
 * @param pThis A pointer to a YScan plane
 * @param pYTags a pointer to an array of doubles that must be at least as 
 *        long as the number of items returned by DasPlane_getNItems() for
 *        this plane.
 */
DAS_API void DasPlane_setYTags(DasPlane* pThis, const double* pYTags);

/** Get the Y axis coordinate series for a 2-D plane of data
 * 
 * @param[in] pThis A pointer to a YScan plane
 * 
 * @param[out] pInterval a pointer to a double which will be set to the interval
 *             between samples in a series, or DAS_FILL_VALUE if y-tags are 
 *             specified individually.
 * 
 * @param[out] pMin a pointer to a double which will be set to the minimum 
 *             value of the series, or DAS_FILL_VALUE if y-tags are are specified
 *             as a list.  If NULL, minimum yTag value is not output.
 * 
 * @param[out] pMax a pointer to a double which will be set to the maximum
 *             value of the series, or DAS_FILL_VALUE if y-tags are are specified
 *             as a list.  This is not an exclusive upper bound, but rather the
 *             actual value for the last yTag.  If NULL, maximum yTag value is
 *             not output
 */
DAS_API void DasPlane_getYTagSeries(
	const DasPlane* pThis, double* pInterval, double* pMin, double* pMax
);

/** Set a YScan to use series definition for yTags
 * 
 * Note that this can change the return value from DasPlane_getYTagSpec() 
 * and trigger deallocation of internal yTag lists
 * 
 * @param pThis a pointer to a YScan
 * @param rInterval the interval between yTag values
 * @param rMin The initial yTag value or DAS_FILL_VALUE if rMax is supplied
 * @param rMax The final yTag value or DAS_FILL_VALUE if rMin is supplied
 */
DAS_API void DasPlane_setYTagSeries(
	DasPlane* pThis, double rInterval, double rMin, double rMax
);


/** Serialize a Plane Descriptor as XML data
 *
 * This function is almost the opposite of new_DasPlane_pairs()
 * 
 * @param pThis The plane descriptor to store as string data
 * @param pBuf A buffer object to receive the bytes
 * @param sIndent A string to place before each line of output
 * @return 0 if successful, or a positive integer if not.
 * @memberof DasPlane
 */
DAS_API DasErrCode DasPlane_encode(
	DasPlane* pThis, DasBuf* pBuf, const char* sIndent
);

/** Serialize a plane's current data.
 * 
 * In addition to holding the format information for a single data plane in a 
 * Packet, DasPlane objects also hold one set of samples for the plane.  For
 * the plane types \<x\>, \<y\>, and \<z\> a single plane's data is just one
 * value.  For the \<yscan\> type a single plane's data is 1-N values.  Use
 * this function to encode a planes current data values for output.
 * 
 * @param pThis The plane descriptor that has been loaded with data
 * @param pBuf A buffer to receive the encoded bytes
 * @param bLast All ASCII type values are written with a single space after
 *        the value except for the last value of the last plane which has
 *        a newline character appended.  Set this to true if this is the last
 *        plane in the packet which is being encoded.
 * @returns 0 if successful, or a positive integer if not.
 * @memberof DasPlane
 */
DAS_API DasErrCode DasPlane_encodeData(DasPlane* pThis, DasBuf* pBuf, bool bLast);

/** Read in a plane's current data.
 * 
 * @param pThis The plane to receive the data values
 * @param pBuf The buffer containing values to decode.
 * @return 0 if successful, or a positive integer if not.
 * @memberof DasPlane
 */
DAS_API DasErrCode DasPlane_decodeData(const DasPlane* pThis, DasBuf* pBuf);

#ifdef __cplusplus
}
#endif

#endif /* _das_plane_h_ */
