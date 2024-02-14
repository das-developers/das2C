/* Copyright (C) 2024 Chris Piker <chris-piker@uiowa.edu>
 *
 * This file is part of das2C, the Core Das2 C Library.
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

/** @file serial.h Creating, reading and writing datasets to and from buffers */

#ifndef _das_serial_h_
#define _das_serial_h_

#include <das2/stream.h>
#include <das2/dataset.h>

#ifdef __cplusplus
extern "C" {
#endif


/** Define a das dataset and all it's constiutant parts from an XML header
 * 
 * @param nDasVer The major version of the header format.  If this is 2
 *             then the top level element should be <packet>, if this is 3
 *             then the top level element is expected to be <dataset>
 * 
 * @param pBuf The buffer to read.  Reading will start with the read point
 *             and will run until DasBuf_remaining() is 0 or the end tag
 *             is found, which ever comes first.
 * 
 * @param pParent The parent descriptor for this data set. This is assumed
 *             to be an object which can hold vector frame definitions.
 * 
 * @param nPktId  The packet's ID within it's parent's array.  My be 0 if
 *             and only if pParent is NULL
 * 
 * @return A pointer to a new DasDs and all if it's children allocated 
 *         on the heap, or NULL on an error.
 */
DAS_API DasDs* dasds_from_xmlheader(int nDasVer, DasBuf* pBuf, StreamDesc* pParent, int nPktId);



#ifdef __cplusplus
}
#endif


#endif /* _serial */