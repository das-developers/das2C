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

/** @file iterator.h Objects which automatically increment indexes */

#ifndef _das_iterator_h_
#define _das_iterator_h_

#include <das2/dataset.h>

#ifdef __cplusplus
extern "C" {
#endif

/** Dataset iterator structure. 
 * 
 * Since dataset rank and shape is a union of the shape of it's components 
 * iterating over datasets can be tricky.  This structure and it's associated
 * functions are provided to simplify this task.  Usage is demonstrated by
 * the example below:
 * 
 * @code
 * // Assume a dataset with time, amplitude and frequency dimensions but with
 * // arbitrary shape in index space.
 * 
 * // pDs is a pointer to a das dataset 
 * 
 * DasDim* pDimTime = DasDs_getDimById(pDs, "time");
 * DasVar* pVarTime = DasDim_getPointVar(pDimTime);
 * 
 * DasDim* pDimFreq = DasDs_getDimById(pDs, "frequency");
 * DasVar* pVarFreq = DasDim_getPointVar(pDimFreq);
 * 
 * DasDim* pDimAmp  = DasDs_getDimById(pDs, "e_spec_dens");
 * DasVar* pVarAmp  = DasDim_getPointVar(pDimAmp);
 * 
 * dasds_iterator iter;
 * das_datum set[3];
 * 
 * for(das_iter_init(&iter, pDs); !iter.done; das_iter_next(&iter)){
 *		
 *	  DasVar_get(pVarTime, iter.index, set);
 *	  DasVar_get(pVarFreq, iter.index, set + 1);
 *	  DasVar_get(pVarAmp,  iter.index, set + 2);
 * 
 *	  // Plot, or bin, or what-have-you, the triplet here.
 *   // Plot() is not a real function in the  das2C API
 *	  Plot(set);
 *	}
 * 
 * @endcode
 */
typedef struct dasds_iterator_t{
	
	/** If true the value in index is valid, false otherwise */
	bool       done;
	
	/** A dataset bulk iteration index suitable for use in DasVar functions like
	 * ::DasVar_getDatum */
	ptrdiff_t index[DASIDX_MAX];
	
	int        rank;
	ptrdiff_t  shape[DASIDX_MAX];  /* Used for CUBIC datasets */
	ptrdiff_t  nLenIn;            /* Used for ragged datasets */
	bool      ragged;
	const DasDs* pDs;
} DasDsIter;

#define dasds_iterator DasDsIter

/** Initialize a const dataset iterator
 * 
 * The initialized iterator is safe to use for datasets that are growing
 * as it will not exceed the valid index range of the dataset at the time
 * this function was called.  However, if the dataset shrinks during iteration
 * das_iter_next() could overstep the array bounds.
 * 
 * For usage see the example in ::das_iterator
 * 
 * @param pThis A pointer to an iterator, will be initialize to index 0
 * 
 * @param pDs A pointer to a dataset.  If the dataset changes while the
 *        iterator is in use invalid memory access could occur
 * 
 * @memberof DasDsIter
 */
DAS_API void DasDsIter_init(DasDsIter* pThis, const DasDs* pDs);
#define dasds_iter_init DasDsIter_init

/** Increment the iterator's index by one position, rolling as needed at 
 * data boundaries.
 * 
 * For efficiency this function does not re-check array bounds on each call
 * a slower but safer version of this function could be created if needed.
 *
 * For usage see the example in ::das_iterator
 * 
 * @param pThis A pointer to an iterator.  The index member of the iterator 
 *        will be incremented.
 * 
 * @return true if the new index is within range, false if the index could not
 *       be incremented without producing an invalid location.
 * 
 * @memberof DasDsIter
 */
DAS_API bool DasDsIter_next(DasDsIter* pThis);
#define dasds_iter_next DasDsIter_next

/* ************************************************************************* */

/** A non-degenerate iterator
 */
typedef struct das_uniq_iter_t{
	/** If true the current value in the index is valid, false otherwise */
	bool       done;
	
	/** A dataset bulk iteration index suitable for use in DasVar functions like
	 * ::DasVar_getDatum */
	ptrdiff_t index[DASIDX_MAX];
	
	/** A list of index values that will be auto assigned to zero */
	bool       lock[DASIDX_MAX];

	/** Shortcut iteration by saving off the indexes that matter */
	int        first;
	int        last;
	
	int        rank;
	ptrdiff_t  shape[DASIDX_MAX];  /* Used for CUBIC datasets */
	ptrdiff_t  nLenIn;            /* Used for ragged datasets */
	bool       ragged;
	const DasDs* pDs;
} DasDsUniqIter;

/** Initialize a non-degenerate iterator for a variable
 * 
 * This iterator type runs over all indexes in a dataset except for
 * those that are not used by the given variable.  It works for 
 * ragged and cubic datasets.  This is a convenient way to extract
 * unique values for a variable.
 * 
 * For usage see the example in ::das_iterator
 * 
 * @param pThis A pointer to an iterator, will be initialize to index 0
 * 
 * @param pDs A pointer to a dataset.  If the dataset changes while the
 *        iterator is in use invalid memory access could occur
 * 
 * @memberof DasDsUniqIter
 */

DAS_API void DasDsUniqIter_init(
	DasDsUniqIter* pThis, const DasDs* pDs, const DasVar* pVar
);

/** Increment the iterator's index by one position, rolling as needed at 
 * data boundaries.
 * 
 * For efficiency this function does not re-check array bounds on each call
 * a slower but safer version of this function could be created if needed.
 *
 * For usage see the example in ::das_iterator
 * 
 * @param pThis A pointer to an iterator.  The index member of the iterator 
 *        will be incremented.
 * 
 * @return true if the new index is within range, false if the index could not
 *       be incremented without producing an invalid location.
 * 
 * @memberof DasDsUniqIter
 */
DAS_API bool DasDsUniqIter_next(DasDsUniqIter* pThis);

/* ************************************************************************* */

/** Simple cubic iterator
 */
typedef struct das_cube_iter_t{
	
	/** If true the value in index is valid, false otherwise */
	bool       done;
	
	/** A dataset bulk iteration index suitable for use in DasVar functions like
	 * ::DasVar_getDatum */
	ptrdiff_t index[DASIDX_MAX];
	
	int        rank;
	ptrdiff_t  idxmin[DASIDX_MAX]; 
	ptrdiff_t  idxmax[DASIDX_MAX]; 
	
} DasDsCubeIter;

#define das_cube_iter DasDsCubeIter
#define das_cube_iter_init DasDsCubeIter_init
#define das_cube_iter_next DasDsCubeIter_next

/** Initialize an iterator to cubic section in index space 
 * @memberof das_cube_iterator
 */
DAS_API void DasDsCubeIter_init(
	das_cube_iter* pThis, int nRank, ptrdiff_t* pMin, ptrdiff_t* pMax 
);

/** Increment a cubic iterator by one position, rolling as needed 
 * 
 * @param pThis A pointer to an iterator.  The index member of the iterator 
 *        will be incremented.
 * 
 * @return true if the new index is within range, false if the index could not
 *       be incremented without producing an invalid location.
 * 
 * @memberof das_cube_iterator
 */
DAS_API bool DasDsCubeIter_next(das_cube_iter* pThis);

/** Iterate over a sub-set of the index space of a DasAry */
typedef struct das_array_iter_t{
	const DasAry* pAry;
	bool          done;
	bool          ragged;             /* flag for no end_idx */
	bool          bNaturalEnd;       
	int           rank;
	ptrdiff_t     index[DASIDX_MAX];   /* current index */
	ptrdiff_t     end_idx[DASIDX_MAX]; /* 1 after last valid index */
	int           dim_min;
	int           dim_max;

	ptrdiff_t  shape[DASIDX_MAX];  /* Used for CUBIC arrays */
	ptrdiff_t  nLenLast;            /* Used for ragged arrays */
} DasAryIter;


/** Initialize an array iterator
 * 
 * The iterator can be used to simple iterate over the whole array when
 * nDimMin = 0, nDimMax = -1, pBeg = NULL, and pEnd = NULL, though it's
 * primary purpose it to iterate over a subset of indices.
 * 
 * @param pThis - The iterator to initialize
 * 
 * @param pAry  - The array over which it will iterate
 * 
 * @param iDimMin - The minimum (left most) index to change.  Use 0 to increment
 *        the higest level index as needed, 1 for the next highest and so on.
 * 
 * @param iDimMax - The maximum (right most) index to change.  So for a rank 3
 *        array this would be 2. Use -1 to allow changes for the last index,
 *        -2 for the next to last and so on.
 * 
 * @param pBeg - The starting point for iteration, use NULL to start at
 *        the beginning.
 * 
 * @param pEnd - The ending point for iteration.  This is an exclusive
 *        upper bound!  Use NULL to end after the last valid index in the
 *        array.
 * 
 * @memberof DasAryIter
 */
DAS_API void DasAryIter_init(
	DasAryIter* pThis, const DasAry* pAry, int iDimMin, int iDimMax, ptrdiff_t* pBeg, 
	ptrdiff_t* pEnd
);


/** Increment an array iterated to the next expected index
 * 
 * Updates the .index member to the next value in range.
 * 
 * @param pThis - The iterator in increment
 * 
 * @returns true if the new index is in range, false otherwise and the .done
 *   member is set to true.
 */
DAS_API bool DasAryIter_next(DasAryIter* pThis);


#ifdef __cplusplus
}
#endif

#endif /* _das_dataset_h */
