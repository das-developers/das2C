/* Copyright (C) 2017-2018 Chris Piker <chris-piker@uiowa.edu>
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


/** @file dataset.h Objects which define a iteration space */

#ifndef _das_dataset_h_
#define _das_dataset_h_

#include <das2/dimension.h>
#include <das2/codec.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Old initial comment that kicked off the entire das2 data model design...
 *
 * The structures below are the start of an idea on how to get independent
 * parameters for data at any particular index.  These are just thoughts
 * at the moment and don't affect any working code.  There are many ways
 * to do this.  The CDF and QStream assumption is that there are the same
 * number of parameters locating a data point in parameter space as there
 * are indices to the dataset.  Because of this x,y,z scatter data are 
 * hard to handle.
 *  
 * For x,y,z scatter lists there is 1 index for any point in the dataset, 
 * but for each index there are 2 independent parameters.  Basically QStream
 * and CDF assume that all datasets are CUBEs in parameter space but this
 * is not the case for a great many sets. 
 *
 * To adequately handle these 'path' datasets a parameter map is required.
 * The mapping takes 1 index value per data rank and returns 1 to N parameter
 * values.
 *
 * These structures start to handle this idea but are just doodles at this
 * point. -cwp 2017-07-25
 */

/* Second comment that added desire for flexible data types ...
 * 
 * Thinking about coordinate returns, how about a data set of thefts / month
 * in 5 cities...  Won't usually come up, but should be possible
 * to handle.  Here's the data set:
 *
 *              2016-01 2016-02 2016-03 2016-04 2016-05 2016-06
 * Baltimore       2351    3789    4625    5525    6135    5902
 * BogotaÃ       109065  110365   99625   98265   43850   33892
 * Chicago         4789    5764    8901   10145   13456   22678
 * Des Moines         4      10      33      35      44     107
 *
 * Properties:  Title -> "Thefts/Month for selected cities"
 *
 * Okay, the X axis data type is text[12] (need null char)
 *           Y axis data type is datetime
 *           Z axis data type is datum, "thefts month**-1"
 *
 * So what is the return value from pDs->bin(pDs, 0, 0) ?
 *
 * The bin is defined on the space of all UTC times, and on the space of all
 * cities in the data set.
 *
 *
 * So, what about this common data set, interference events:
 *
 * |<----------     Bin      ------>|  |<----- Value --->|
 * 2016-01-01T14:00  2016-01-02T02:20  Mag Roll
 * 2016-01-01T15:40  2016-01-01T15:41  Stabilization Pulse
 * 2016-01-01T15:43  2016-01-01T15:44  Stabilization Pulse
 * 2016-01-01T15:45  2016-01-01T15:47  Stabilization Pulse
 * 2016-01-01T15:48  2016-01-01T15:50  Stabilization Pulse
 *
 * So what is the return value from pDs->bin(pDs, 0) ?
 *
 * The space is UTC time,  So each bin start and stop is defined on the space
 * of all UTC times.
 * -cwp 2017-??-??
 */
 
/** @defgroup datasets Datasets
 * Classes and functions for storing and manipulating correlated data values
 */

/* Number of encoders that can be stored internally, more then this and they
 * have to be allocated on the heap.  This is the common "small vector" 
 * optimization */
#define DASDS_LOC_ENC_SZ 32

/** @addtogroup datasets 
 * @{
 */
	
/** Das Datasets
 *
 * Das Datasets provide storage for arrays that contains both data values and
 * coordinate values.  Each dataset corresponds to a single index space.
 * All variables in the dataset support the same bulk index range, though they
 * may not produce unique values for each distinct set of indices.
 *  
 * Mapping from the dataset index space to individual arrays is handled by
 * variables (::DasVar).
 *
 * Variables are grouped together into *physical* dimension by das2 
 * dimension (::DasDim) objects.  Each variable in a dimension servers a 
 * role.  For example providing center point values.  Bin max values, bin
 * min, uncertianty, etc.
 * 
 * A typical dataset consisting of a Time dimension, Frequency dimension and 
 * Amplitude dimension may have the following index ranges:
 * 
 * @code
     Time(i:0..152, j:-    )   // Defined in 1st index, any 2nd index is okay
     Freq(i:-,      j:0..1440) // Defined in 2nd index, any 1st index is okay
     Amp( i:0..152, j:0..1440) // Defined in both indices
 * @endcode
 * 
 * Here <b>i</b> is the first index and <b>j</b> is the second.
 *
 * The first two dimensions define a time and frequency coordinates space, and 
 * the last provides amplitude values collected at over time and frequency.
 * 
 * @todo explain variables in dimensions and point-spreads
 * 
 * Binning these values could proceed in a loops such as described in the 
 * the ::DasDs_lengthLast function. 
 * 
 * @extends DasDesc
 */
typedef struct dataset {
	DasDesc base;        /* This would be equivalent to the properties for 
	                        a packet descriptor.  Typically in das 2.2 packets
	                        don't have a descriptor, only streams and planes
	                        but access to the stream descriptor forwards through
	                        here. */
	
	int nRank;           /* The number of whole-dataset index dimenions. 
								 * Variables can define internal dimensions but they
								 * can't use indices in the first nRank positions for
								 * internal use, as these are used to correlate values
								 * across the dataset. */
	
	/* A text identifier for this instance of a data set */
	char sId[DAS_MAX_ID_BUFSZ];
	
	/* A text identifier for the join group for this dataset.  Datasets with
	 * the same groupID should be joined automatically by display clients. 
	 */
	char sGroupId[DAS_MAX_ID_BUFSZ];
								
	size_t uDims;        /* Number of dimensions, das datasets are 
                          * implicitly bundles in qdataset terms. */
	
	DasDim** lDims;      /* The data variable object arrays */
	size_t uSzDims;      /* Current size of dimension array */
	
	size_t uArrays;      /* The number of low-level arrays */
	DasAry** lArrays;    /* An array of array objects */
	size_t uSzArrays;
	
	ptrdiff_t _shape[DASIDX_MAX];  /* cache shape calls for speed */
	
	bool _dynamic;      /* If true, the dataset may still be changing and all
	                       bulk properties such as the iteration shape should be
	                       recalculated instead of using cached values. 
                          If false, cached values are expected to already be 
	                       available */

	/* dataset arrays can be written in chunks to output buffers. The number of
	 * elements in each chuck, the encoding of each element any separators are
	 * defined below. */
	/* DasCodec** lEncs; */

	/* Use a fixed size for now */
	size_t uSzEncs;
	DasCodec aPktEncs[DASDS_LOC_ENC_SZ];
	int nPktItems[DASDS_LOC_ENC_SZ];

    /** User data pointer
    * 
    * The stream -> dataset hierarchy provides a goood organizational structure
    * for application data, especially applications that filter streams.  It is
    * initialized to NULL when a variable is created but otherwise the library
    * dosen't deal with it.
    */
   void* pUser;

} DasDs;            

/** Create a new dataset object.
 *
 * @param sId An identifier for this dataset should be unique within a group
 *            but this requirement is not yet enforced.
 *
 * @param sGroupId An identifier for the group to which the dataset belongs.
 *            Datasets within a group can be plotted in the same physical
 *            dimensions, though the index shape need not be the same in
 *            any respect.
 *            
 *            Said another way, datasets in the same group must have the
 *            same number of coordinate and data dimensions and the units
 *            of corresponding variables in the datasets should be 
 *            identical, or at least inter-convertible.
 *            
 * @param nRank The overall iteration rank for the dataset, i.e. the number
 *            of indicies needed to retrive values from this dataset's 
 *            variables.  ALL variables in a dateset accept the same 
 *            number of indices in the same relative positions when
 *            reading values.
 *
 *            Unlike ISTP CDF's, rank is an iteration property and has no
 *            defined relationship to the number of physical dimensions of
 *            the dataset.  Thus two datasets may have different ranks but
 *            be part of the same group.
 *
 * @return
 *
 * @memberof DasDs
 */
DAS_API DasDs* new_DasDs(
	const char* sId, const char* sGroupId, int nRank
);

/** Delete a Data object, cleaning up it's memory
 *
 * If the underlying arrays and property values are needed else where
 * call release on sub items.
 *
 * @param pThis The dataset object to delete, provided pointer
 *         should be set to NULL after this operation.
 * @memberof DasDs
 */
DAS_API void del_DasDs(DasDs* pThis);

/** Lock/Unlock the dataset for changes.
 * 
 * All DasDs object default to mutable.  This has the side effect that
 * certian values which could be cached for speed (such as the shape) must be
 * re-calculated on demand.  Use this function to lock the dataset from being
 * changed so that it can cache fequent requests.
 *
 * @param pThis The dataset in question
 *
 * @param bChangeAllowed if false, the shape of the data set will be cached
 *        and all calls that would alter the dataset will fail.  Note that
 *        it is possible to change a dataset in an external manner that is
 *        not visible using the DasDim_, DasVar_ and DasAry_ functions 
 *        directly.
 */
DAS_API void DasDs_setMutable(DasDs* pThis, bool bChangeAllowed);


/** Get the lock state of the dataset */
#define DasDs_mutable(P) P->_mutable

/** Return current valid ranges for whole data set iteration
 *
 * To plot all values in a dataset iterate over the entire range provided for
 * each function.  The returned shape is the maximum value + 1 of each index
 * of the given dataset.  The shape can change as data are added to the 
 * dataset.
 * 
 * Data variables that include point spread functions and variables that 
 * provide vectors require an inner iteration that is not part of the 
 * returned shape.
 * 
 * Note that for a properly defined dataset all indices below the rank of
 * the dataset will be used.  
 * 
 * @code
 * // Setup the shape array to contain all D2IDX_UNUSED values first
 * ptrdiff_t aBulkShape[D2IDX_MAX] = D2IDX_EMPTY;
 * 
 * // Now get the shape
 * int nRank = DasDs_shape(pDs, aBulkShape);
 * 
 * @endcode
 *
 * @param pThis A pointer to a dataset object
 *
 * @param[out] pShape pointer to an array to receive the current bulk 
 *             iteration shape required to get all the values from all
 *             variables in the dataset.
 * 
 *             * An integer from 0 to LONG_MAX indicating the valid range
 *               of values for this index.
 * 
 *             * The constant DASIDX_RAGGED indicating that the range of
 *               values for this index depend on upper indicies.
 * 
 *             * The constant DASIDX_UNUSED to indicate that a index is 
 *               un-used by this dataset.
 *
 * @return The iteration rank sufficient to read all coordinate and data
 *          values.
 *
 * @memberof DasDs
 */
DAS_API int DasDs_shape(const DasDs* pThis, ptrdiff_t* pShape);

/** Return the current max value index value + 1 for any partial index
 * 
 * This is a more general version of DasDim_shape that works for both cubic
 * arrays and with ragged dimensions, or sequence values.
 * 
 * @param pThis A pointer to a DasDim structure
 * @param nIdx The number of location indices which may be less than the 
 *             number needed to specify an exact value.
 * @param pLoc A list of values for the previous indexes, must be a value 
 *             greater than or equal to 0
 * @return The number of sub-elements at this index location or D2IDX_UNUSED
 *         if this variable doesn't depend on a given location, or D2IDx_FUNC
 *         if this variable returns computed results for this location
 * 
 * @see DasAry_lengthIn
 */
DAS_API ptrdiff_t DasDs_lengthIn(const DasDs* pThis, int nIdx, ptrdiff_t* pLoc);

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
 * for(dasds_iter_init(&iter, pDs); !iter.done; dasds_iter_next(&iter)){
 *		
 *	  DasVar_getDatum(pVarTime, iter.index, set);
 *	  DasVar_getDatum(pVarFreq, iter.index, set + 1);
 *	  DasVar_getDatum(pVarAmp,  iter.index, set + 2);
 * 
 *	  // Plot, or bin, or what-have-you, the triplet here.
 *    // Plot() is not a real function in the libdas2 C API
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
} dasds_iterator;

/** Initialize a const dataset iterator
 * 
 * The initialized iterator is safe to use for datasets that are growing
 * as it will not exceed the valid index range of the dataset at the time
 * this function was called.  However, if the dataset shrinks during iteration
 * das_iter_next() could overstep the array bounds.
 * 
 * For usage see the example in ::das_iterator
 * 
 * @param pIter A pointer to an iterator, will be initialize to index 0
 * 
 * @param pDs A pointer to a dataset.  If the dataset changes while the
 *        iterator is in use invalid memory access could occur
 * 
 * @memberof dasds_iterator
 */
DAS_API void dasds_iter_init(dasds_iterator* pIter, const DasDs* pDs);

/** Increment the iterator's index by one position, rolling as needed at 
 * data boundaries.
 * 
 * For efficiency this function does not re-check array bounds on each call
 * a slower but safer version of this function could be created if needed.
 *
 * For usage see the example in ::das_iterator
 * 
 * @param pIter A pointer to an iterator.  The index member of the iterator 
 *        will be incremented.
 * 
 * @return true if the new index is within range, false if the index could not
 *       be incremented without producing an invalid location.
 * 
 * @memberof dasds_iterator
 */
DAS_API bool dasds_iter_next(dasds_iterator* pIter);


/** Get the data set group id
 *
 * Datasets with the same group ID are representable in the same coordinate
 * and data types (for example time, frequency, and power), but have different
 * locations in the coordinate space.  Another way of saying this is all
 * datasets with have the same physical units for thier coordinates and data
 * but not the same coordinate values.  
 *
 * Since a dataset is defined in this library to include all items in as single
 * index space more than one dataset may encountered in a stream.  All datasets
 * with the same groupID should be plottable on the same set of axis.
 *
 * @param pThis A pointer to a dataset sturcture
 * @returns a string pointer than is never null
 * @memberof DasDs
 */
#define DasDs_group(P) ((const char*)(P)->sGroupId)

/** Get the data set string id
 *
 * @param pThis A pointer to a dataset sturcture
 * @returns a string pointer than is never null
 * @memberof DasDs
 */
#define DasDs_id(P) ((const char*)(P)->sId)


/** Get the rank of a dataset 
 * 
 * A dataset's rank is one of it's key immutable properties.  It defines the
 * maximum number of valid external indicies for all included variables.
 * Any physical dimension included in the dataset will have the same rank
 * as the dataset.  Any variable includid in those physical dimensions will
 * present the same rank as well, even if the underlying storage areas are
 * composed of smaller rank arrays.
 * 
 * @param pThis A pointer to a dataset sturcture
 *
 * @returns The rank, which defines the number of valid external index
 *          positions for sub items.
 * 
 * @memberof DasDs
 */
#define DasDs_rank(P) ((P)->nRank)

/** Add an array to the dataset, stealing it's reference.
 *
 * Arrays are raw backing storage for the dataset.  They contain elements
 * but do not provide a meaning for those elements.  Variables are a 
 * semantic layer on top of the raw arrays.
 *
 * @param pThis a Dataset structure pointer
 *
 * @param pAry The array to add.  Note: This function "steals" a reference to the
 *        array.  Meaning it does not increment the refenece count of the 
 *        array when adding it to the function, but it @b does decrement
 *        the refenece when the dataset is deleted!  So if you want the
 *        calling code to still have access to the array after the dataset
 *        it's attached too is removed you'll have to call inc_DasAry() on
 *        your own.
 *
 * @returns Returns DAS_OKAY so long as no previous arrays have the same array id.
 * 
 * @memberof DasDs
 */
DAS_API DasErrCode DasDs_addAry(DasDs* pThis, DasAry* pAry);

#define DasDs_numAry(P) ((P)->uArrays)

#define DasDs_getAry(P, I) ((P)->lArrays[(I)])


/** Get a dataset array given it's identifier
 * 
 * Every array must have a text ID, furthermore these must be unique within
 * the dataset (enforced by DasDs_addAry).
 * 
 * @param pThis a dataset structure pointer
 * 
 * @param sId A text string identifying one of the datasets arrays
 * 
 * @returns A pointer to the array, or NULL if no array with the given ID 
 *        could be found in the dataset.
 */
DAS_API DasAry* DasDs_getAryById(DasDs* pThis, const char* sAryId);

/** Get the currently used memory of all arrays in the dataset 
 * 
 * Note that this is not the memory footprint, as DasAry's will allocate
 * more space then needed during append operations.  This is done to
 * reduce the number of allocations.
 *
 * @note Static structures such as DasDims and DasVars also require some
 *       space. Static memory usage is not returned, only array dynamic
 *       memory usage.
 * 
 * @param pThis a dataset structure pointer
 * 
 * @returns The sum of used heap bytes in all the arrays in the dataset.
 *      These are the bytes that contain usable data values as well as
 *      the bytes used by index arrays.
 * 
 * @see DasDs_memOwned() to get the allocated heap bytes for all 
 *      arrays in the dataset.
 */
DAS_API size_t DasDs_memUsed(const DasDs* pThis);

/** The apparent memory usage of all arrays in the dataset.  Note that
 * this is less the the apparent memory usage of all variables in the
 * dataset.
 */
DAS_API size_t DasDs_memIndexed(const DasDs* pThis);

/** Get the currently allocated memory of all arrays in the dataset
 * 
 * @note The allocated memory may not be indexed yet, especally after
 *       DasAry_clear() has been called.
 * 
 * @note Static structures such as DasDims and DasVars also require some
 *       space. Static memory usage is not returned, only array dynamic
 *       memory usage.
 * 
 * @param pThis a dataset structure pointer
 * 
 * @returns The sum of used heap bytes in all the arrays in the dataset.
 *      These are the bytes that contain usable data values.
 * 
 * @see DasDs_memUse() to get the bytes currently used for dynamic
 *      storage
 */
DAS_API size_t DasDs_memOwned(const DasDs* pThis);


/** Define a packet data encoded/decoder for fixed length items and arrays
 * 
 * @param pThis a Dataset structure pointer
 * 
 * @param sAryId The array to encode to/decode from
 * 
 * @param sSemantic How the values are to be used.  This affects parsing.
 *        For example a string meant to represent a datatime is stored
 *        differently from one that represents an annotation.
 * 
 * @param sEncType one of the following encoding types as taken from 
 *        the das-basic-stream-v3.0.xsd schema:
 *        
 *        - byte   : 8-bit signed integer
 *        - ubyte  : 8-bit un-signed integer
 *        - utf8   : A string of text bytes
 *        - BEint  : A signed integer 2+ bytes long, most significant byte first
 *        - BEuint : An un-signed integer 2+ bytes long MSB first
 *        - LEint  : Little-endian version of BEint
 *        - LEuint : Little-endian version of BEuint
 *        - BEreal : An IEEE-754 floating point value, MSB first
 *        - LEreal : An IEEE-754 floating point value, LSB first
 * 
 * @param nItemBytes The number of bytes in an item.  For variable
 *        length items terminated by a separator, use -9 (DASENC_USE_SEP) 
 *        and specify an item terminator.  For variable length items
 *        with explicit lengths use -1 (DASENC_ITEM_LEN)
 * 
 * @param nNumItems The number of items to read/write at a time.
 * 
 * @returns DAS_OKAY if the array codec could be defined
 */
DAS_API DasErrCode DasDs_addFixedCodec(
	DasDs* pThis, const char* sAryId, const char* sSemantic, 
	const char* sEncType, int nItemBytes, int nNumItems
);

/** Define a packet data encoder for variable length items and arrays
 * 
 * @param pThis @see DasDs_addFixedCodec
 * 
 * @param sAryId @see DasDs_addFixedCodec
 * 
 * @param sEncType @see DasDs_addFixedCodec
 * 
 * @param nItemBytes The number of bytes in an item.  For variable
 *        length items terminated by a separator, use -9 (DASENC_USE_SEP) 
 *        and specify an item terminator.  
 * 
 *        @note At present, variable length items with explicit length
 *        in packets are not yet supported
 * 
 * @param nSeps The number of separators for variable length items.
 * 
 *        For text items, item separator is first.  Next are the separators
 *        that indicate the end of fastest moving dataset index, followed
 *        by the end of the next fastests an so on.  The max number of
 *        separators must be 1 less then the rank of the dataset for not
 *        text encodings, and not greater then the rank of the dataset
 *        for non-text encodings.
 * 
 * @param uSepLen The length in bytes of the variable length separators.
 *        Must be a value from 1 through 8, inclusive.
 *
 * @param pSepByIdx An array of pointers to separator values.
 * 
 * @returns DAS_OKAY if the array codec could be defined
 */
DAS_API DasErrCode DasDs_addRaggedCodec(
	DasDs* pThis, const char* sAryId, const char* sEncType, 
	int nItemBytes, int nSeps, ubyte uSepLen, const ubyte* pSepByIdx
);

/** Clear any arrays that are ragged in index l
 * 
 * This function is handy when reading data to insure that memory usage
 * does not grow without limit.  Any memory allocated is not freed, but
 * the write points are reset so that the same buffers can be used over
 * and over again.
 * 
 * @param pThis A dataset
 * 
 * @returns The number of bytes cleared.
 * 
 * @memberof DasAry
 */
DAS_API size_t DasDs_clearRagged0Arrays(DasDs* pThis);


/** Make a new dimension within this dataset
 *
 * Adding a dimension to a dataset will change cause the parent descriptor for
 * the variable to be set to this dataset.  The dataset takes ownership of the
 * variable and will delete it when the dataset is deleted
 * 
 * @param pThis A pointer to a dataset structure
 * 
 * @param dType The type of dimension.  If this is a coordinate dimension
 *              all data dimensions that vary in any of the same indices as
 *              this dimension will be set to depend on these coordinates.
 * 
 * @param sDim A name for this dimension.  Standard names such as 'time', 
 *        'frequence' 'range' 'altitude' etc. should be used if possible.
 *        No standard list of dimension names are provided by this library, 
 *        it is left up to the application programmers to handle this.
 * 
 * @param sId An identifier for this paritiular variable group in a dimension.
 *        For example 'Search_Coil', 'DC_MAG', etc.
 *        
 * @memberof DasDs
 */
DAS_API DasDim* DasDs_makeDim(
    DasDs* pThis, enum dim_type dType, const char* sDim, const char* sId
);

/** Add a physical dimension to the dataset
 * 
 * @warning The dataset takes ownership of the dimesion object and will delete
 * it when the dataset is deleted.  It is important not to provide a pointer
 * to a stack variable.
 * 
 * @param pThis A pointer to a dataset structure
 * 
 * @param pDim A existing dimension object created *on the heap*.  
 * 
 * @returns DAS_OKAY if the dimension
 * 
 * @membefof DasDs
 */
DAS_API DasErrCode DasDs_addDim(DasDs* pThis, DasDim* pDim);

/** Get the number of physical dimensions in this dataset
 *
 * @param pThis The dataset object
 * @param vt The variable type, either COOR or DATA
 * @return The number of data functions provided for a dataset.
 * @memberof DasDs
 */
DAS_API size_t DasDs_numDims(const DasDs* pThis, enum dim_type vt);


/** Get a dimension by it's basic kind
 * 
 * @param sDim The general dimension type, like time, position, voltage, etc.
 * 
 * @memberof DasDs
 */
DAS_API const DasDim* DasDs_getDim(const DasDs* pThis, const char* sDim);

/** Get a dimension by index
 * @param pThis a pointer to a dataset structure
 * @param idx the index of the variable in question
 * @param vt the variable type, either COORD or DATA
 * @returns A Variable pointer or NULL if idx is invalid
 * @memberof DasDs
 */
DAS_API const DasDim* DasDs_getDimByIdx(
	const DasDs* pThis, size_t idx, enum dim_type vt
);

/** Get a dimension by string id 
 * @param pThis a pointer to a dataset structure
 * @param sId The name of the dimension to retrieve, for example 'time' or 'frequency'
 * @returns A dimesion pointer or NULL if sId does not match any dimesion 
 *          name 
 * @memberof DasDs
 */
DAS_API const DasDim* DasDs_getDimById(const DasDs* pThis, const char* sId);


/** Print a string representation of this dataset.
 * 
 * Note: Datasets can be complicated items provide a good sized buffer 
 * (~1024 bytes), when calling this function as it triggers subcalls for 
 * all the compontent toStr as well
 */
DAS_API char* DasDs_toStr(const DasDs* pThis, char* sBuf, int nLen);



/* Ideas I'm still working on...


/ * The two functions below are really useful but I'll need to crack open
   a double pack of Flex and Bison to get it done so I'm punting for now. * /
	
/ * Ex Expression:  $spec_dens[i][j][k] * /
const Function* Dataset_evalDataExp(Dataset* pThis, const char* sExpression);

/ * Ex Expression:  $craft_alt[i][j] - 0.5 * $delay_time[k] * 299792 * /
const Function* Dataset_evalCoordExp(Dataset* pThis, const char* sExpression);


/ ** 
 * 
 * This function answers the question by either provided the spanning set of
 * coordinates or returning nothing.  
 * For a dataset to be defined
 * on a coordinate grid there must exist one coordinate set for each index in
 * the data set and each coordinate must be a function of only one index.  
 * 
 * Non-gridded data can still be sliced but coordintate slices will need to be
 * produced as well in order to plot the slice. See Dataset_orthogonal()
 * 
 * @param pThis A correlated dataset object
 * @param sDs The string id of the dataset in question
 * @param[out] psCoords a pointer to a const char* array to recived the 
 *              coordinate ID's forming the spanning set.  Note that every
 *              combination of returned coordinates satisfies the orthogonal
 *              condition and would return true from Dataset_orthogonal().
 * 
 * @return     The number of spanning coordinates.  Will be equal to the 
 *              rank of the dataset.
 * /
size_t Dataset_gridCoords(
	const Dataset* pThis, const char* sDs, const char** psCoords
);

bool DataGen_grid(const DataSet* pDataset);

const DataSet** Dg_griddedIn(const DataSet* pDataset);



/ ** Get the coefficients for iterating over a 1-D slice of a regular (i.e. 
 * non-ragged) dataset.
 *
 * This function dose not work for ragged datasets and merely returns NULL if
 * asked for iteration coefficents for such a set.  In such a case use 
 * Dataset_copySlice1D().
 *
 * /
const void* Dataset_slice1D(
	const Dataset* pThis, const char* sDs, const char* sCoord, int iCoordIdx,
	int* pCoeff
);

/ ** Increment the reference count on any array objects that are part of 
 * a data space.
 *
 * This is useful in instances where the underlying data arrays are going
 * to be represented by an organizational structure other than datasets
 * and DataSets since Das array objects only free data memory if thier
 * feference count is zero.
 *
 * @param pThis
 * /
void DataSpace_incAryRef(Dataset* pThis);

/ * Need a way to trigger callbacks from datasets changing, not just
   packets changing.  It could be useful to work on items from the
	dataset level instead of just the packet level * /
 bool DataSpace_stream(Dataset* pThis); * /



/ ** Indicate the physical degrees of freedom for a dataset by denoting a
 * complete list of coordinate sets.  
 *
 * A list of coordinates over which an entire dataset is defined is called
 * a span.  Datasets may have 1-N spans.
 * /
int DataSpace_addSpan(const char* sDsId, const char** lCoords, size_t nCoords);
*/

/** @} */

#ifdef __cplusplus
}
#endif

#endif /* _das_dataset_h */
