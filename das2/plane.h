/** @file plane.h Header for Plane Descriptor Objects */

#ifndef _das2_plane_h_
#define _das2_plane_h_

#include <math.h>
#include <stdbool.h>

#include <das2/buffer.h>
#include <das2/descriptor.h>
#include <das2/units.h>
#include <das2/encoding.h>

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
		
/** Returns the enumeration for the data type string */
plane_type_t str2PlaneType(const char * type);

/** Returns the string for the enumeration */
const char* PlaneType_toString( plane_type_t type );


/* ************************************************************************* */
/** Describes a data plane within a packet type.
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
 *   <y type="ascii11" group="radius" units=""><properties String:yLabel="R!DE!N" /></y>
 * @endcode
 * @code
 *   <y type="ascii11" group="mag_lat" units=""><properties String:yLabel="MLat" /></y>
 * @endcode
 * @code
 *   <y type="ascii11" group="mag_lt" units=""><properties String:yLabel="MLT" /></y>
 * @endcode
 * @code
 *   <y type="ascii11" group="l_shell" units=""><properties String:yLabel="L" /></y>
 * @endcode
 * @code
 * </packet>
 * :01:2013-001T01:00:00.000  5.782e+00 -1.276e+01  3.220e+00  6.079e+00
 * :01:2013-001T01:01:00.000  5.782e+00 -1.274e+01  3.232e+00  6.077e+00
 * :01:2013-001T01:02:00.000  5.781e+00 -1.272e+01  3.244e+00  6.076e+00
 * @endcode
 *
 * Each @b \<x\> and @b \<y\> element inside the \<packet\> element above is a 
 * serialized PlaneDesc.  The point of a PlaneDesc structure is to:
 * 
 *    -# Hold the definition of a single plane within a single packet type. 
 *    -# Assist PacketDescriptors with serializing and de-serializing data packets.
 *
 * Just as an isolated plane without a parent \<packet\> tag does not make sense 
 * in a Das2 Stream, PlaneDesc structures don't have much use on their own.
 * They are typically owned by a PacketDescriptor and accessed via the
 * ::PacketDescriptor.planes array.
 *
 * @extends Descriptor 
 * @nosubgrouping
 */
typedef struct plane_descriptor{
	Descriptor base;

	plane_type_t planeType;
	char* sGroup;
	 
	/* The encoder/decoder used to read and write values for this plane. */
	DasEncoding* pEncoding;
	 
	/* The units of measurement for values in this plane */
	UnitType units;
	 
	/* The number of values in each packet of this plane.
	 * For planes other than \<yscan\>'s this is always 1
	 */
	size_t uItems;
	
	double* pData;
	double value;  /* Convenience for planes that only store one data point */
	bool bAlloccedBuf; /* true if had to allocate a data buffer (<yscan> only)*/
	
	double rFill;  /* The fill value for this plane, will be wrapped in a 
	                  macro to make isFill look like a function */
	bool _bFillSet;  /* Flag to make sure fill value has been set */
	
	ytag_spec_t ytag_spec;
	
	double* pYTags;       /* Explicit Y value array <yscan>'s */
	
	double yTagInter;    /* Or spec as a series <yscans>'s */
	double yTagMin;
	double yTagMax;
	
	UnitType yTagUnits;
	DasEncoding* pYEncoding;
	
	/* set to true setValues or decode is called, set to false when encode is 
	 * called */
	bool bPlaneDataValid;
	 
	/* User data pointer.
	 * The stream->packet->plane hierarchy provides a good organizational
	 * structure for application data, especially for applications whose
	 * purpose is to filter streams.  This pointer can be used to hold
	 * a reference to information that is not serialized.  It is initialized
	 * to NULL when a Plane Descriptor is created otherwise the library
	 * doesn't deal with it in any other way. */
	void* pUser;  
	
} PlaneDesc;

/** Creates a Plane Descriptor with mostly empty settings.
 * @memberof PlaneDesc
 */
PlaneDesc* new_PlaneDesc_empty(void);

/** Creates a new X,Y or Z plane descriptor
 *
 * @param pt The ::PlaneType, must be one of: 
 *    - X - Independent Values
 *    - Y - Dependent or Independent Values
 *    - Z - Dependent Values
 *    - YScan - Dependent Values
 * 
 * @param sGroup the name for the data group this plane belongs to, may be the
 *        empty string ""
 * @param pType an encoding object for the new plane.  The PlaneDesc will
 *        take ownership of this object and free it when del_PlaneDesc is
 *        called.
 * @param units The units of measurement for data in this plane.
 * @memberof PlaneDesc
 * @returns A pointer to new PlaneDesc allocated on the heap.
 */
PlaneDesc* new_PlaneDesc( 
	plane_type_t pt, const char* sGroup, DasEncoding* pType, UnitType units
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
 * @param pZType an encoding object for the new plane.  The PlaneDesc will
 *        take ownership of this object and free it when del_PlaneDesc is
 *        called.
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
 * @returns A pointer to new PlaneDesc allocated on the heap.
 * @memberof PlaneDesc
 */
PlaneDesc* new_PlaneDesc_yscan(
	const char* sGroup, DasEncoding* pZType, UnitType zUnits, size_t uItems,
	DasEncoding* pYType, const double* pYTags, UnitType yUnits
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
 * @param pZType an encoding object for the new plane.  The PlaneDesc will
 *        take ownership of this object and free it when del_PlaneDesc is
 *        called.
 * @param zUnits The units of measurement for Z-axis data in this plane.
 * @param uItems The number of data values in each packet of this plane's 
 *        data.  i.e. the number of yTags.
 * @param yTagInter the interval between values in the yTag series
 * @param yTagMin the initial value of the series.  Use FILL_VALUE to have 
          the starting point set automatically using yTagMax.
 * @param yTagMax the final value of the series.  Use FILL_VALUE to have 
          the ending point set automatically using yTagMin.
 * @param yUnits The units for yTag values.
 *
 * @returns A pointer to new PlaneDesc allocated on the heap.
 * @memberof PlaneDesc
 */
PlaneDesc* new_PlaneDesc_yscan_series(
	const char* sGroup, DasEncoding* pZType, UnitType zUnits, size_t uItems,
   double yTagInter, double yTagMin, double yTagMax, UnitType yUnits
);

/* Creates a new plane descriptor from attribute strings
 * 
 * Unlike the other top-level descriptor objects in a Das2 Stream planes
 * are not independent XML documents.  This constructor is called from
 * the new_PktDesc_xml constructor to build plane descriptor object from
 * keyword / value stlye string lists.  The top level XML parsing is handled
 * by the PktDesc class.
 *
 * @param pParent the Properties parent for the new plane descriptor, this
 *        is always a PktDesc object pointer.
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
 * @returns A pointer to new PlaneDesc allocated on the heap or NULL on an 
 *          error
 * @memberof PlaneDesc
 */
PlaneDesc* new_PlaneDesc_pairs(
	Descriptor* pParent, plane_type_t pt, const char** attrs
);

/** Copy constructor for planes
 * Deep copy one a plane except for the parent id.
 * @param pThis The plane descriptor to copy
 * @returns A new plane descriptor allocated on the heap.  All properties of
 *         the plane descriptor are duplicated as well. 
 * @memberof PlaneDesc
 */
PlaneDesc* PlaneDesc_copy(const PlaneDesc* pThis);


/** Free a plane object allocated on the heap.
 * 
 * This frees the memory allocated for the main structure as well as any
 * internal buffers allocated for storing data values and Y-tags.
 *
 * @param pThis The plane descriptor to free
 * @memberof PlaneDesc
 */
void del_PlaneDesc(PlaneDesc* pThis);

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
 * @warning Passing the same non-NULL pointer twice to this function will always
 *          result in a return of true, as planes are equivalent to themselves.
 * 
 * @param The first plane descriptor
 * @param the second plane descriptor
 * 
 * @returns true if the two are equivalent, false otherwise.  
 */
bool PlaneDesc_equivalent(const PlaneDesc* pThis, const PlaneDesc* pOther);

/** Get a plane's type
 *
 * @param pThis The plane descriptor to query
 * @return The plane type, which is one of X, Y, YScan or Z
 * @memberof PlaneDesc
 */
plane_type_t PlaneDesc_getType(const PlaneDesc* pThis);

/** Get the number of items in a plane
 * YScan planes have a variable number of items, for all other types this 
 * function returns 1
 * @param pThis the PlaneDiscriptor
 * @returns the number of items in plane 
 * @memberof PlaneDesc
 */
size_t PlaneDesc_getNItems(const PlaneDesc* pThis);

/**  Set the number of items in a plane
 * 
 * @warning Calling this function with a different size then the current 
 * number of items in a plane will cause a re-allocation of the internal
 * data values memory buffer.  Any pointers returned previously by 
 * PlaneDesc_getValues() or PlaneDesc_getYTags() will be invalidated.
 * 
 * @warning Always call PlaneDesc_setYTags() or PlaneDesc_setYTagSeries()
 * after changing the number of items in a plane!  Failure do to so has 
 * undefined results. 
 * 
 * @param pThis The plane, which must be of type YScan
 * @param nItems the new number of items in the plane.
 */
void PlaneDesc_setNItems(PlaneDesc* pThis, size_t nItems);


/** Get the first value from a plane
 *
 * Get the current value for an item in a plane, Note that X, Y and Z
 * planes only have one item.  
 * @see DasEnc_write() for converting doubles to time strings.
 * 
 * @param pThis The Plane descriptor object
 * @param uIdx the index of the value to retrieve, this is always 0 for 
 *        X, Y, and Z planes.
 * @returns the current value or FILL_VALUE in no data have been read or set.
 * @memberof PlaneDesc
 */
double PlaneDesc_getValue(const PlaneDesc* pThis, size_t uIdx);

/** Set a current value in a plane
 * 
 * Set a current value for an item in a plane.  Note X, Y and Z
 * planes only have one item.
 * @param pThis The plane in question
 * @param uIdx the index of the value to set.  Only YScan planes have data
 *        at indicies above 0.
 * @param value The new value
 * @returns 0 if successful or a positive error number otherwise.
 * @memberof PlaneDesc
 */
ErrorCode PlaneDesc_setValue(PlaneDesc* pThis, size_t uIdx, double value);

/** Set a single time value in a plane 
 * 
 * Manually sets a current value for a plane instead of decoding it from an
 * input stream.  The given time string is converted to a broken down time
 * using the parsetime function from daslib and then the brokend down time is
 * converted to a double in the units specified for this plane.
 * 
 * @param pThis The plane to get the value
 * @param sTime a parse-able time string
 * @param idx The index at which to write the value, for X, Y and Z planes 0 is
 *        the only valid index.
 * @returns 0 if successful or a positive error number otherwise.  Attempting
 *        to set a time value for planes who's units are not time is an error.
 * 
 * @memberof PlaneDesc
 */
ErrorCode PlaneDesc_setTimeValue(
	PlaneDesc* pThis, const char* sTime, size_t idx
);

/** Get the data value encoder/decoder object for a plane
 * The encoder returned via this pointer can be mutated and any changes will
 * be reflected in the next data write for the plane.
 * @param pThis The data plane in question
 * @return The current encoder for this plane.  You may change fields within
 *         the returned encoder but do not free() the return value.
 * @memberof PlaneDesc
 */
DasEncoding* PlaneDesc_getValEncoder(PlaneDesc* pThis);

/** Set the data value encoder/decoder object for a plane
 * The previous encoder's memory is returned the heap via free().
 * 
 * @param pThis The data plane in question
 * @param pEnc A pointer to the new encoder structure, the plane takes ownership
 *        of it's memory.  This value must not be null
 * @memberof PlaneDesc
 */
void PlaneDesc_setValEncoder(PlaneDesc* pThis, DasEncoding* pEnc);

/** Get a pointer to the current set of values in a plane.
 * 
 * Planes are basically wrappers around a double array with some encoding and
 * decoding information.  This function provides a pointer to the value array
 * for the plane.
 * 
 * @param[in] pThis The plane in question
 * @returns A pointer to the current plane's data, or NULL if neither
 *          PlaneDesc_setValue() or PlaneDesc_decode() have been called.
 * 
 * @memberof PlaneDesc
 */
const double* PlaneDesc_getValues(const PlaneDesc* pThis);


/** Set all the current values for a plane
 * @see PlaneDesc_getNItems()
 * 
 * @param pThis The plane to receive the values
 * @param pData A pointer to an array of doubles that is the same length as 
 *        the number of items in this plane.  Values are copied in, no
 *        references are kept to the input pointer after the function completes
 * @memberof PlaneDesc
 */
void PlaneDesc_setValues(PlaneDesc* pThis, const double* pData);


/** Returns the fill value identified for the plane.
 * Note: If the value has not been explicitly specified, then the canonical
 * value is used.
 * @memberof PlaneDesc
 */
double PlaneDesc_getFill(const PlaneDesc* pThis );

/** Returns non-zero if the value is the fill value identified 
 * for the data plane.
 * @memberof PlaneDesc
 */
#define PlaneDesc_isFill(P, V) \
 ((P->rFill == 0.0 && V == 0.0) || (fabs((P->rFill - V)/P->rFill)<0.00001))

/* bool PlaneDesc_isFill(const PlaneDesc* pThis, double value ); */

/** Identify the double fill value for the plane.
 * @memberof PlaneDesc
 */
void PlaneDesc_setFill( PlaneDesc* pThis, double value );
									 
/** Get the data group of a plane
 * @returns the group of the plane, or "" if it is not specified.
 * @memberof PlaneDesc
 */
const char* PlaneDesc_getGroup(const PlaneDesc* pThis );

/** Set the data group of a plane
 * @param pThis The plane descriptor to regroup
 * @param sGroup the new data group for the plane, will be copied into internal 
 *        memory
 * @memberof PlaneDesc
 */
void PlaneDesc_setGroup(PlaneDesc* pThis, const char* sGroup);
 
/** Get the units of measure for a plane's packet data
 * @returns the units of the data values in a plane
 * @memberof PlaneDesc
 */
UnitType PlaneDesc_getUnits(const PlaneDesc* pThis );

/** Set the unit type for the plane data
 * 
 * All Das2 Stream values are stored as doubles within the library the units
 * property indicates what these doubles mean
 * 
 * @param pThis The Plane to alter
 * @param units The new units setting.  Note this value may be NULL in which
 *        case the Plane will be set to UNIT_DIMENSIONLESS.
 * @memberof PlaneDesc
 */
void PlaneDesc_setUnits(PlaneDesc* pThis, UnitType units);

/** Get Y axis units for a 2-D plane
 * @returns the Units of the YTags of a \<yscan\> plane.  
 * @memberof PlaneDesc
 */
UnitType PlaneDesc_getYTagUnits( PlaneDesc* pThis );

/** Set the YTag units for a YScan plane
 * 
 * @param pThis The plane, which must be of type YScan.
 * @param units The new units
 */
void PlaneDesc_setYTagUnits(PlaneDesc* pThis, UnitType units);

/** Get the storage method for yTag values
 *
 * The 2nd dimension of a yScan plane may have data values associated 
 * with each index point.  These values can be specified individually
 * or by simply providing an interval between point and the value of
 * the 0th index.  Use this function to determine which method is
 * used.
 */
ytag_spec_t PlaneDesc_getYTagSpec(const PlaneDesc* pThis);

/** Get Y axis coordinates for a 2-D plane of data.
 * @returns an array of doubles containing the yTags for the YSCAN plane 
 *          or null if yTags are just a simple series.
 *
 * @see PlaneDesc_getYTagInterval()
 * @memberof PlaneDesc
 */
const double* PlaneDesc_getYTags(const PlaneDesc* pThis);

/** Provide a new set of yTag values to a yScan plane
 * 
 * @param pThis A pointer to a YScan plane
 * @param pYTags a pointer to an array of doubles that must be at least as 
 *        long as the number of items returned by PlaneDesc_getNItems() for
 *        this plane.
 */
void PlaneDesc_setYTags(PlaneDesc* pThis, const double* pYTags);

/** Get the Y axis coordinate series for a 2-D plane of data
 * 
 * @param[in] A pointer to a YScan plane
 * 
 * @param[out] pInterval a pointer to a double which will be set to the interval
 *             between samples in a series, or FILL_VALUE if y-tags are 
 *             specified individually.
 * 
 * @param[out] pMin a pointer to a double which will be set to the minimum 
 *             value of the series, or FILL_VALUE if y-tags are are specified
 *             as a list.  If NULL, minimum yTag value is not outpu.
 * 
 * @param[out] pMin a pointer to a double which will be set to the maximum
 *             value of the series, or FILL_VALUE if y-tags are are specified
 *             as a list.  This is not an exclusive upper bound, but rather the
 *             actual value for the last yTag.  If NULL, maximum yTag value is
 *             not output
 */
void PlaneDesc_getYTagSeries(
	const PlaneDesc* pThis, double* pInterval, double* pMin, double* pMax
);

/** Set a YScan to use series definition for yTags
 * 
 * Note that this can change the return value from PlaneDesc_getYTagSpec() 
 * and trigger deallocation of internal yTag lists
 * 
 * @param pThis a pointer to a YScan
 * @param rInterval the interval between yTag values
 * @param rMin The initial yTag value or FILL_VALUE if rMax is supplied
 * @param rMax The final yTag value or FILL_VALUE if rMin is supplied
 */
void PlaneDesc_setYTagSeries(
	PlaneDesc* pThis, double rInterval, double rMin, double rMax
);


/** Serialize a Plane Descriptor as XML data
 *
 * This function is almost the opposite of new_PlaneDesc_pairs()
 * 
 * @param pThis The plane descriptor to store as string data
 * @param pBuf A buffer object to receive the bytes
 * @param sIndent A string to place before each line of output
 * @param bDependant If true this is dependant data and should serialized with
 *        a data group attribute.
 * @return 0 if successful, or a positive integer if not.
 * @memberof PlaneDesc
 */
ErrorCode PlaneDesc_encode(
	PlaneDesc* pThis, DasBuf* pBuf, const char* sIndent, bool bDependant
);

/** Serialize a plane's current data.
 * 
 * In addition to holding the format information for a single data plane in a 
 * Packet, PlaneDesc objects also hold one set of samples for the plane.  For
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
 * @memberof PlaneDesc
 */
ErrorCode PlaneDesc_encodeData(PlaneDesc* pThis, DasBuf* pBuf, bool bLast);

/** Read in a plane's current data.
 * 
 * @param pThis The plane to receive the data values
 * @param pBuf The buffer containing values to decode.
 * @return 0 if successful, or a positive integer if not.
 * @memberof PlaneDesc
 */
ErrorCode PlaneDesc_decodeData(const PlaneDesc* pThis, DasBuf* pBuf);

#endif /* _das2_plane_h_ */
