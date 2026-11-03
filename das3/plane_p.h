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
	DasEncoding* pEncoding;
	 
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
	DasEncoding* pYEncoding;
	
	/* set to true setValues or decode is called, set to false when encode is 
	 * called */
	bool bPlaneDataValid;
	 
};

#ifdef __cplusplus
}
#endif

#endif /* _das_plane_p_h_ */
