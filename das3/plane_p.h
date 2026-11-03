/* Copyright (C) 2026 Chris Piker <chris-piker@uiowa.edu>
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

/** @file plane_p.h INTERNAL header, subject to change without notice.
 *
 * Nothing here is API, and it is not installed.
 */

#ifndef _das_plane_p_h_
#define _das_plane_p_h_

#include <das3/plane.h>
#include <das3/desc_p.h>

#ifdef __cplusplus
extern "C" {
#endif

struct das_plane {
	DasDesc base;

	plane_type_t planeType;
	char* sName;
	 
	/* The encoder/decoder used to read and write values for this plane. */
	DasEnc* pEncoding;
	 
	/* The units of measurement for values in this plane */
	das_units units;
	 
	/* The number of values in each packet of this plane.
	 * For planes other than \<yscan\>'s this is always 1
	 */
	size_t uItems;
	
	/* Das 3.0 note:
	 * One of the fundamental assumptions in this code, way back from when
	 * it started was that all data could be converted to doubles.  That 
    * remains true for this old packet structure.  For das3 streams many
    * data types are supported, see codec.h and array.h for details.
	 */
	double* pData;
	double value;  /* Convenience for planes that only store one data point */
	bool bAlloccedBuf; /* true if had to allocate a data buffer (<yscan> only)*/
	
	double rFill;  /* The fill value for this plane */
	bool _bFillSet;  /* Flag to make sure fill value has been set */
	
	ytag_spec_t ytag_spec;
	
	double* pYTags;       /* Explicit Y value array <yscan>'s */
	
	double yTagInter;    /* Or spec as a series <yscans>'s */
	double yTagMin;
	double yTagMax;
	
	das_units yTagUnits;
	DasEnc* pYEncoding;
	
	/* set to true setValues or decode is called, set to false when encode is 
	 * called */
	bool bPlaneDataValid;
	 
};


/* Moved from plane.h: library plumbing, not API */

/** Read in a plane's current data.
 * 
 * @param pThis The plane to receive the data values
 * @param pBuf The buffer containing values to decode.
 * @return 0 if successful, or a positive integer if not.
 * @memberof DasPlane
 */
DasErrCode DasPlane_decodeData(const DasPlane* pThis, DasBuf* pBuf);

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
DasErrCode DasPlane_encode(
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
DasErrCode DasPlane_encodeData(DasPlane* pThis, DasBuf* pBuf, bool bLast);

/** Creates a Plane Descriptor with mostly empty settings.
 * @memberof DasPlane
 */
DasPlane* new_DasPlane_empty(void);

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
DasPlane* new_DasPlane_pairs(
	DasDesc* pParent, plane_type_t pt, const char** attrs
);

#ifdef __cplusplus
}
#endif

#endif /* _das_plane_p_h_ */
