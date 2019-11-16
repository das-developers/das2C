/* Copyright (C) 2015-2017 Chris Piker <chris-piker@uiowa.edu>
 *
 * This file is part of libdas2, the Core Das2 C Library.
 * 
 * Libdas2 is free software; you can redistribute it and/or modify it under
 * the terms of the GNU Lesser General Public License version 2.1 as published
 * by the Free Software Foundation.
 *
 * Libdas2 is distributed in the hope that it will be useful, but WITHOUT ANY
 * WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS
 * FOR A PARTICULAR PURPOSE.  See the GNU Lesser General Public License for
 * more details.
 *
 * You should have received a copy of the GNU Lesser General Public License
 * version 2.1 along with libdas2; if not, see <http://www.gnu.org/licenses/>. 
 */

/** @file binner.h Header for stream binners.  Unlike screen binners
 * where known edge points are given these binners only assume a resoultion
 * value but the end points are unknown.
 */

#ifndef _das2_tools_via_h_
#define _das2_tools_via_h_

#include <stdbool.h>
#include <stdlib.h>

/** Virtual index array
 *
 * This class baby-sits an array allowing writes at any positive or negative
 * indices.  Writes before the start of the array wrap around to the end.  If
 * a write occures too early the buffer automatically grows to include it. If
 * a write occurs so late that it would overwrite the earliest index, the
 * buffer automatically grows.
 *
 * This could be re-worked to be typeless and put in libdas2 if desired.
 *
 *
 *  The general storage of the array looks like this:
 *
 * Real Idx  0              iMax                         iMin              Sz-1
 *          +-+-------------+-+--------------------------+-+----------------+-+
 *          | | data > Vo   | |    No man's land         | |    data < Vo     |
 *          +-+-------------+-+--------------------------+-+----------------+-+
 * Virt Idx  Vo             Vmax                         Vmin
 *
 *  Thus:   iMax = Vmax - Vo   and   iMin = Sz - (Vo - Vmin)
 *
 * The positions iMax move up as indices > Vo are set and iMin moves down
 * as positions < Vo are set.  If iMin would ever equal or cross iMax then
 * the array is re-allocated if possible and the values at the end are copied
 * to the end of the new array, the real index iMin is then set to a new 
 * value.
 */
typedef struct virt_idx_ary{

  double* pBuf;
 
  int nSz;
  int nMaxSz;
  
  int iVirt0;
  int iVmin;
  int iVmax;
  int iVlast;
 
  bool bHasData;
} Via;


/** Create a new virtual index array on the heap.
 * @param uInitialSz - The initial size of the array
 * @param uMaxSz - The maximum size of the array
 *
 * @memberof Via
 */
Via* new_Via(int uInitLen, int uMaxLen);

/** Clear the virtual index table and zero's memory
 * does not realloc internal array 
 */
void Via_clear(Via* pThis);

/** Destructor frees all memory. 
 * pThis is invalid after the call, calling code should set it to NULL.
 */
void del_Via(Via* pThis);


/** See if an index is valid */
bool Via_valid(const Via* pThis, int iVirt);

/** Get a value at legal index value.
 *
 * @returns the value ant index iVirt (assuming iVirt is defined).
 */
double Via_get(const Via* pThis, int iVirt);


/** Set a value at an index 
 *
 * @returns false if placing data at that index would cause the array to 
 *          grow beyond it's max size or if a memory allocation error 
 *          occurs
 */
bool Via_set(Via* pThis, int iVirt, double dVal);

/** Increment a value at an index 
 *
 * @returns false if placing data at that index would cause the array to 
 *          grow beyond it's max size or if a memory allocation error 
 *          occurs
 */
bool Via_add(Via* pThis, int iVirt, double dVal);


/** Get lowest defined virtual index in the array 
 * @returns -1 if a minimum virtual index has not been defined
 */
int Via_minIndex(const Via* pThis);

/** Get the highest defined virtual index in the array
 * @returns -2 if a maximum virtual index has not been defined 
 */
int Via_maxIndex(const Via* pThis);

/** Get the length of the defined index span in the array */
int Via_length(const Via* pThis);


/** Get the value of the last index set in this array, fails if no
 * index has been set since instatiation or the last call to clear */
int Via_lastSet(const Via* pThis);

#endif /* _das2_tools_via_h_ */
