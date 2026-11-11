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

/** @file iter.h Objects which automatically increment indexes
 *
 * These iterators are meant to live on the stack, so programs see their
 * whole layout.  That layout can change between releases.  Rebuild against
 * each new library, and don't copy the structures by hand into a foreign
 * function interface such as Python's ctypes.
 */

#ifndef _das_iter_h_
#define _das_iter_h_

#include <das3/datset.h>

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
 * DasDim* pDimTime = DasDs_dim(pDs, "time");
 * DasVar* pVarTime = DasDim_pointVar(pDimTime);
 * 
 * DasDim* pDimFreq = DasDs_dim(pDs, "frequency");
 * DasVar* pVarFreq = DasDim_pointVar(pDimFreq);
 * 
 * DasDim* pDimAmp  = DasDs_dim(pDs, "e_spec_dens");
 * DasVar* pVarAmp  = DasDim_pointVar(pDimAmp);
 * 
 * DasDsIter iter;
 * DasDatum set[3];
 * 
 * for(init_DasDsIter(&iter, pDs); !iter.done; DasDsIter_next(&iter)){
 *		
 *	  DasVar_datumAt(pVarTime, iter.addr, set);
 *	  DasVar_datumAt(pVarFreq, iter.addr, set + 1);
 *	  DasVar_datumAt(pVarAmp,  iter.addr, set + 2);
 * 
 *	  // Plot, or bin, or what-have-you, the triplet here.
 *   // Plot() is not a real function in the  das2C API
 *	  Plot(set);
 *	}
 * 
 * @endcode
 */
typedef struct das_dsiter_t{
	
	/** If true the value in addr is valid, false otherwise */
	bool       done;
	
	/** A dataset bulk iteration address suitable for use in DasVar functions
	 * like ::DasVar_datumAt */
	ptrdiff_t  addr[DASIDX_MAX];
	
	/** The number of valid entries in addr */
	int        rank;

	/* Internal state, not for callers */
	ptrdiff_t  aShape[DASIDX_MAX];  /* Used for CUBIC datasets */
	ptrdiff_t  nLenLast;            /* Used for ragged datasets */
	bool       bRagged;
	const DasDs* pDs;
} DasDsIter;

/** Initialize a const dataset iterator
 * 
 * The initialized iterator is safe to use for datasets that are growing
 * as it will not exceed the valid index range of the dataset at the time
 * this function was called.  However, if the dataset shrinks during iteration
 * DasDsIter_next() could overstep the array bounds.
 * 
 * For usage see the example in ::DasDsIter
 * 
 * @param pThis A pointer to an iterator, will be initialize to index 0
 * 
 * @param pDs A pointer to a dataset.  If the dataset changes while the
 *        iterator is in use invalid memory access could occur
 * 
 * @memberof DasDsIter
 */
DAS_API void init_DasDsIter(DasDsIter* pThis, const DasDs* pDs);

/** Increment the iterator's index by one position, rolling as needed at 
 * data boundaries.
 * 
 * For efficiency this function does not re-check array bounds on each call
 * a slower but safer version of this function could be created if needed.
 *
 * For usage see the example in ::DasDsIter
 * 
 * @param pThis A pointer to an iterator.  The addr member of the iterator 
 *        will be incremented.
 * 
 * @return true if the new index is within range, false if the index could not
 *       be incremented without producing an invalid location.
 * 
 * @memberof DasDsIter
 */
DAS_API bool DasDsIter_next(DasDsIter* pThis);

/* ************************************************************************* */

/** A non-degenerate iterator
 */
typedef struct das_dsuniqiter_t{
	/** If true the current value in addr is valid, false otherwise */
	bool       done;
	
	/** A dataset bulk iteration address suitable for use in DasVar functions
	 * like ::DasVar_datumAt */
	ptrdiff_t  addr[DASIDX_MAX];
	
	/** The number of valid entries in addr */
	int        rank;

	/* Internal state, not for callers */
	bool       aLock[DASIDX_MAX];   /* indices held at zero, the variable doesn't use them */
	int        iIdxFirst;           /* first index that matters, inclusive */
	int        iIdxLast;            /* last index that matters, inclusive */
	ptrdiff_t  aShape[DASIDX_MAX];  /* Used for CUBIC datasets */
	ptrdiff_t  nLenLast;            /* Used for ragged datasets */
	bool       bRagged;
	const DasDs* pDs;
} DasDsUniqIter;

/** Initialize a non-degenerate iterator for a variable
 * 
 * This iterator type runs over all indexes in a dataset except for
 * those that are not used by the given variable.  It works for 
 * ragged and cubic datasets.  This is a convenient way to extract
 * unique values for a variable.
 * 
 * For usage see the example in ::DasDsIter
 * 
 * @param pThis A pointer to an iterator, will be initialize to index 0
 * 
 * @param pDs A pointer to a dataset.  If the dataset changes while the
 *        iterator is in use invalid memory access could occur
 * 
 * @param pVar The variable to iterate over.  Indices it does not use
 *        are skipped.
 * 
 * @memberof DasDsUniqIter
 */

DAS_API void init_DasDsUniqIter(
	DasDsUniqIter* pThis, const DasDs* pDs, const DasVar* pVar
);

/** Increment the iterator's index by one position, rolling as needed at 
 * data boundaries.
 * 
 * For efficiency this function does not re-check array bounds on each call
 * a slower but safer version of this function could be created if needed.
 *
 * For usage see the example in ::DasDsIter
 * 
 * @param pThis A pointer to an iterator.  The addr member of the iterator 
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
typedef struct das_dscubeiter_t{
	
	/** If true the value in addr is valid, false otherwise */
	bool       done;
	
	/** A dataset bulk iteration address suitable for use in DasVar functions
	 * like ::DasVar_datumAt */
	ptrdiff_t  addr[DASIDX_MAX];
	
	/** The number of valid entries in addr */
	int        rank;

	/* Internal state, not for callers */
	ptrdiff_t  aAddrBeg[DASIDX_MAX];  /* first address */
	ptrdiff_t  aAddrEnd[DASIDX_MAX];  /* exclusive, 1 after the last valid address */
	
} DasDsCubeIter;

/** Initialize an iterator to cubic section in index space 
 *
 * @param pThis The iterator to initialize
 *
 * @param nRank The number of indices in pAddrBeg and pAddrEnd
 *
 * @param pAddrBeg The starting address, also the value each index rolls
 *        back to when the index before it increments
 *
 * @param pAddrEnd The ending address.  This is an exclusive upper bound,
 *        as for init_DasAryIter()
 *
 * @memberof DasDsCubeIter
 */
DAS_API void init_DasDsCubeIter(
	DasDsCubeIter* pThis, int nRank, const ptrdiff_t* pAddrBeg,
	const ptrdiff_t* pAddrEnd
);

/** Increment a cubic iterator by one position, rolling as needed 
 * 
 * @param pThis A pointer to an iterator.  The addr member of the iterator 
 *        will be incremented.
 * 
 * @return true if the new index is within range, false if the index could not
 *       be incremented without producing an invalid location.
 * 
 * @memberof DasDsCubeIter
 */
DAS_API bool DasDsCubeIter_next(DasDsCubeIter* pThis);

/** Iterate over a sub-set of the index space of a DasAry */
typedef struct das_aryiter_t{

	/** If true the value in addr is valid, false otherwise */
	bool          done;

	/** The current address, suitable for DasAry functions */
	ptrdiff_t     addr[DASIDX_MAX];

	/** The number of valid entries in addr */
	int           rank;

	/* Internal state, not for callers */
	const DasAry* pAry;
	bool          bRagged;              /* flag for no aAddrEnd */
	bool          bNaturalEnd;          /* no end given, run to the array's end */
	ptrdiff_t     aAddrEnd[DASIDX_MAX]; /* exclusive, 1 after the last valid address */
	int           iIdxFirst;            /* first index to change, inclusive */
	int           iIdxLast;             /* last index to change, inclusive */
	ptrdiff_t     aShape[DASIDX_MAX];   /* Used for CUBIC arrays */
	ptrdiff_t     nLenLast;             /* Used for ragged arrays */
} DasAryIter;


/** Initialize an array iterator
 * 
 * The iterator can be used to simple iterate over the whole array when
 * iIdxFirst = 0, iIdxLast = -1, pAddrBeg = NULL, and pAddrEnd = NULL, though
 * it's primary purpose it to iterate over a subset of indices.
 * 
 * @param pThis - The iterator to initialize
 * 
 * @param pAry  - The array over which it will iterate
 * 
 * @param iIdxFirst - The first (left most) index to change.  Use 0 to
 *        increment the highest level index as needed, 1 for the next highest
 *        and so on.
 * 
 * @param iIdxLast - The last (right most) index to change, inclusive.  So for
 *        a rank 3 array this would be 2. Use -1 to allow changes for the last
 *        index, -2 for the next to last and so on.
 * 
 * @param pAddrBeg - The starting address for iteration, use NULL to start at
 *        the beginning.
 * 
 * @param pAddrEnd - The ending address for iteration.  This is an exclusive
 *        upper bound!  Use NULL to end after the last valid index in the
 *        array.
 * 
 * @memberof DasAryIter
 */
DAS_API void init_DasAryIter(
	DasAryIter* pThis, const DasAry* pAry, int iIdxFirst, int iIdxLast,
	const ptrdiff_t* pAddrBeg, const ptrdiff_t* pAddrEnd
);


/** Increment an array iterated to the next expected index
 * 
 * Updates the .addr member to the next value in range.
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

#endif /* _das_iter_h_ */
