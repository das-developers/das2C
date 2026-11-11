/* Copyright (C) 2017-2026 Chris Piker <chris-piker@uiowa.edu>
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

/** @file array.h A dynamic buffer with multi-dimensional array style access */

/* I'm sure this kind of buffer has been done many times before, but I haven't
 * seen an implementation that handles arbitrarily ragged arrays.  Thanks to
 * Larry Granroth, and Matt Leifert for helping me talk through the issues 
 * around this class.  
 * -cwp
 */

#ifndef _das_array_h_
#define _das_array_h_

#include <stdarg.h>
#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>

#include <das3/value.h>
#include <das3/units.h>

#ifdef __cplusplus
extern "C" {
#endif

/** The maximum number of indices, for an array and for the model alike.
 * Every model index lands in one array's index list in the end. */
#define DASIDX_MAX 8

/** Python style: index -1 is the last element along a dimension */
#define DASIDX_LAST {-1,-1,-1,-1,-1,-1,-1,-1}

/** An UNBOUNDED extent for one array dimension: the dimension has no declared
 * size and grows as values arrive.  Internal concerns force the use of 
 * 0 here instead of -1
 */
#define DASIDX_UNBOUND 0

/** A RAGGED extent flag for signed index arrays.  Used in all upper layers.*/
#define DASIDX_RAGGED -1

/* ------------------------------------------------------------------------- *
 * The index vocabulary.
 *
 * An array declares extents that are either a real count or unbounded, where
 * 0 means unbounded, thus an unsigned integer is sufficent to hold array 
 * extents.
 *
 * Variables provide a virtual index space and can also have indexes where
 * the maximum legal index is computed or unbounded.  So variables use signed
 * integers for indicies, with negative values for the special case flags.
 * These negative flags walk downward for DASIDX_RAGGED on purpose.
 * dasshape_mergeLen() implements the precedence lattice
 *
 *      Ragged > Number > Borrow > Unused
 *
 * so the flags have to descend in precedence order for it to be right.
 * ------------------------------------------------------------------------- */

/* DASIDX_RAGGED is just above: DasAry_shape() has to write it. */
#define DASIDX_BORROW  (DASIDX_RAGGED - 1)  /* no intrinsic extent, take the container's */
#define DASIDX_UNUSED  (DASIDX_BORROW - 1)  /* a change in this index changes nothing here */

/** Every flag is <= this, so (n > DASIDX_MAXFLG) reads as "is a real extent" */
#define DASIDX_MAXFLG  DASIDX_RAGGED

#define DASIDX_INIT_UNUSED {-3,-3,-3,-3,-3,-3,-3,-3}
#define DASIDX_INIT_BEGIN  { 0, 0, 0, 0, 0, 0, 0, 0}

	
#define DAS_SHAPE1(I) 1, (size_t[1]){I}
#define DAS_SHAPE2(I, J) 2, (size_t[2]){I, J}
#define DAS_SHAPE3(I, J, K) 3, (size_t[3]){I, J, K}
#define DAS_SHAPE4(I, J, K, L) 4, (size_t[4]){I, J, K, L}
#define DAS_SHAPE5(I, J, K, L, M) 5, (size_t[5]){I, J, K, L, M}
#define DAS_SHAPE6(I, J, K, L, M, N) 6, (size_t[6]){I, J, K, L, M, N}
#define DAS_SHAPE7(I, J, K, L, M, N, O) 7, (size_t[7]){I, J, K, L, M, N, O}
#define DAS_SHAPE8(I, J, K, L, M, N, O, P) 8, (size_t[8]){I, J, K, L, M, N, O, P}

#define DAS_ADDR0  0, (NULL)
#define DAS_ADDR1(I)  1, (ptrdiff_t[1]){I}
#define DAS_ADDR2(I,J)  2, (ptrdiff_t[2]){I,J}
#define DAS_ADDR3(I,J,K)  3, (ptrdiff_t[3]){I,J,K}
#define DAS_ADDR4(I,J,K,L)  4, (ptrdiff_t[4]){I,J,K,L}
#define DAS_ADDR5(I,J,K,L,M)  5, (ptrdiff_t[5]){I,J,K,L,M}
#define DAS_ADDR6(I,J,K,L,M,N)  6, (ptrdiff_t[6]){I,J,K,L,M,N}
#define DAS_ADDR7(I,J,K,L,M,N,O)  7, (ptrdiff_t[7]){I,J,K,L,M,N,O}
#define DAS_ADDR8(I,J,K,L,M,N,O,P)  8, (ptrdiff_t[8]){I,J,K,L,M,N,O,P}

#define DAS_IDX0 0
#define DAS_IDX1 1
#define DAS_IDX2 2
#define DAS_IDX3 3
#define DAS_IDX4 4
#define DAS_IDX5 5
#define DAS_IDX6 6
#define DAS_IDX7 7

/** @addtogroup datamodel
 * @{
 */

/* ************************************************************************* */
/* Object identifiers, the names arrays, datasets and dimensions carry */

/** The size of an char buffer large enough to hold valid object IDs */
#define DASID_SZ 64

/** Check that a string is suitable for use as an object ID
 * 
 * Object ID strings are ascii strings using only characters from the set
 * a-z, A-Z, 0-9, and _.  They do not start with a number.  They are no more
 * than 63 bytes long.  Basically they can be used as variable names in most
 * programming languages.
 * 
 * If the library exits on errors, see daslib_init(), this function never returns.
 *  
 * @param sId
 * @return True if the string can be used as an ID, false otherwise.
 */
DAS_API bool dasid_check(const char* sId);


/** @class DasAry array.h <das3/array.h>
 *
 * Dynamic recursive ragged arrays
 *
 * This class maps any number of indices, (i,j,k...) to elements stored into a
 * continuous array.  Any or all particular indexes of the array may be ragged
 * if desired, though typically for fixed record size data streams, only the
 * first index has an arbitrary length.
 *
 * The backing buffers for the array grow as needed when new elements are 
 * appended.  Individual elements may be arbitrary composite types, though
 * extra capabilities are provided for a handful of known types.
 * 
 * Handling ragged data comes at a cost in that the length of each continuous
 * run of elements must also be stored in an ancillary array since address
 * strides are not necessarily constant.  This cost is 8 to 16 bytes times the
 * size of all indexes, except the last.  For example, an array of shape
 * (10,100,40) would use 10*100*16 extra bytes compared to a simple strideable
 * array, though this is typically much smaller than the 10*100*40*element_size
 * bytes used by the primary data buffer.
 * 
 * Handling ragged data also comes at a cost in terms of potential cache misses.
 * For example to lookup a value in a rank 3 array requires accessing three 
 * dynamically allocated buffers. One for each dimension of the array.  The
 * first two are offset value buffers, the last is the actual data buffer.
 * 
 * Automatically switching to more efficient strided index calculations in cases
 * where all records are of a constant size has yet to be implemented, though
 * the DasAry_stride() function can be used to stride across the raw data buffer
 * efficiently if desired.
 * 
 * @code
 *
 * // For this example, assume the file below holds floats in host byte
 * // order.  An array stores bytes as given, swap them first if needed.
 * const char* my_file = "my_datafile.dat";
 * FILE* in_file = fopen(my_file, "rb");
 *
 * // The following Rank 2 array is initially 0 records long and has
 * // 152 values per record.  Thus it starts as a 0 by 152 array
 * DasAry* pAry = new_DasAry(
 *    "amplitudes", DASVT_FLOAT, 0, NULL, DAS_SHAPE2(0, 152), DASUNITS_DIMENSIONLESS
 * );
 *
 * // Read values into the array
 * float buf[152] = {0.0f};
 * while(fread(buf, sizeof(float), 152, in_file) == 152)
 *   DasAry_append(pAry, (const uint8_t*)buf, 152);
 *
 * // Get the shape of the first index.
 * printf("%zu records read from %s\n", DasAry_lengthIn(pAry, DAS_ADDR0), my_file);
 *
 * // Print the values in the array, loop below works with ragged arrays and
 * // reduces function call overhead by accessing a full record at a time
 * const float* pVals;
 * size_t uVals;
 * for(size_t i = 0; i < DasAry_lengthIn(pAry, DAS_ADDR0); ++i){
 *    pVals = DasAry_floatsIn(pAry, DAS_ADDR1(i), &uVals);
 *    for(size_t j = 0; j < uVals; ++j){
 *      if( j > 0) printf(", ");
 *      printf("%.4e", pVals[j]);
 *    }
 *    printf("\n");
 * }
 * @endcode
 */
typedef struct das_ary_t DasAry;

/** @} */


/** Creates a new dynamic array buffer
 *
 * @param id A string token for this array.  Must not be null, must not be
 *        more than 63 characters long.  In general it follows the rules for
 *        variable names in most languages.
 *        @todo see if these restrictions can be lifted.
 *
 * @param et The element type, for arbitrary element storage use DASVT_UNK,
 *        specific types are provided in enum element_type.
 *
 * @param sz_each The size of each element in the array in bytes.  This
 *        parameter is only used when the element type is DASVT_UNK.
 *
 * @param fill A pointer to the value for initializing all empty array records.
 *        The value should be size_each bytes long.  These bytes are copied into
 *        the array and this pointer need not remain valid after the constructor
 *        call.
 *        For unknown types (et = DASVT_UNK) this is a required, non NULL
 *        parameter.  For known types you can use NULL to automatically set
 *        fill to the following values, based on the element_type:
 * 
 *           - DASVT_BYTE                          0
 *           - DASVT_USHORT                    65535
 *           - DASVT_SHORT                    -32767
 *           - DASVT_INT                 -2147483647
 *           - DASVT_LONG       -9223372036854775807L
 *           - DASVT_FLOAT                   -1.0e31
 *           - DASVT_DOUBLE                  -1.0e31
 *           - DASVT_TIME    0000-01-01T00:00:00.000
 *
 *        pFill can point to stack memory as the fill bytes are copied in.
 *
 * @param rank The number of dimensions in the array.  This sets the number
 *        of arguments needed in the get() function call.  To make your code
 *        easier to read, the defines DAS_SHAPE1, DAS_SHAPE2, ... DAS_SHAPE8 are provided.
 *
 * @param shape The initial shape of the array.  One integer is needed here
 *        for each dimension in the array.  Use 0 (DASIDX_UNBOUND) to leave a
 *        dimension unbounded, so that it grows as values arrive.  Multi-dimension arrays used to hold an arbitrarily
 *        long set of records are typically only unbounded in the first index,
 *        though see the example in DasAry_markEnd for handling multiply ragged
 *        arrays.
 *
 * @param units The units of the values, or NULL for none
 *
 * @returns A new array buffer allocated on the heap.
 *
 *
 * @code
 *    // Create a rank 3 array that is ragged only in the first index, with a
 *    // fill value of -1.0f, useful for storing a stream of fixed size MARSIS
 *    // AIS records
 *    float fill = -1.0f;
 *    DasAry* pA = new_DasAry(
 *       "fee", DASVT_FLOAT, 0, (const uint8_t*)&fill, DAS_SHAPE3(0,160,80),
 *       DASUNITS_DIMENSIONLESS
 *    );
 *
 *    // Create an empty Rank 2 array, that is ragged in both indices.
 *    // (Useful for Cassini WBR data)
 *    DasAry* pB = new_DasAry(
 *       "fi", DASVT_FLOAT, 0, NULL, DAS_SHAPE2(0,0), DASUNITS_DIMENSIONLESS
 *    );
 *
 *    // Theoretical example of creating a triply ragged array to hold
 *    // text from a document, where the first index is the page the
 *    // second is the line and the third is the byte in a line
 *    DasAry* pC = new_DasAry(
 *       "text", DASVT_UBYTE, 0, NULL, DAS_SHAPE3(0,0,0), DASUNITS_DIMENSIONLESS
 *    );
 *
 * @endcode
 *
 * @warning The number of shape parameters must be equal to the rank
 *        of the array.  The compiler @b can't check this, but you <b> will get
 *        segfaults </b> if the value of rank does not match the number of shape
 *        values.  To help avoid this the use of the RANK_* macros is highly
 *        recommended.
 *
 * @memberof DasAry
 */
DAS_API DasAry* new_DasAry(
	const char* id, das_vt et, size_t sz_each, const uint8_t* fill,
	int rank, const size_t* shape, DasUnits units
);

/** This array's elements aren't intended to be addressed to the last index,
 * instead each run of the last index should be used as if it were a complete
 * an individual entity.  */
#define DASARY_AS_SUBSEQ 0x00000001

/** A stronger condition then DASARY_AS_SUBSEQ.
 * Not only should the last index be ignored when using this array, in addition
 * for each run of the fastest moving index a FILL value is always inserted as
 * the last element. */
#define DASARY_FILL_TERM 0x00000003

/** A still stronger condition than DASARY_FILL_TERM.
 * This flag indicates not only that the last index shouldn't be addressed 
 * and that each fast-index run is FILL terminated, the fill value is 0.
 * This flag is useful for UTF-8 string data. */
#define DASARY_AS_STRING 0x00000007

/** Wipe memory before the array lets go of it
 *
 * For arrays holding secrets such as passwords and tokens.  Storage is
 * zeroed before it is freed, including the old block each time the array
 * grows, and DasAry_clear() zeroes what it clears.  Elements taken with
 * DasAry_disownData() belong to the caller and are not wiped.
 *
 * Other then a slight performance decrease, this flag has no user visible
 * changes. DasAry_setUsage() replaces all the flags at once, so combine it
 * with the others:
 *
 * @code
 *   DasAry_setUsage(pAry, DASARY_AS_STRING | DASARY_WIPE_ON_FREE);
 * @endcode
 */
#define DASARY_WIPE_ON_FREE   0x00000100

/** Set usage flags to assist arbitrary consumers understand how to use this
 * array.
 * 
 * das arrays can store co-operative flags, these do not change the array API
 * but do indicate how the array should be used.  The following two usage flags
 * are currently defined:
 *
 *   - DASARY_AS_SUBSEQ : Contains Sub-sequences
 *   - DASARY_FILL_TERM : Contains FILL terminated sub-sequences 
 *   - DASARY_AS_STRING : Contains FILL terminated sub-sequences and FILL is 0
 *   - DASARY_WIPE_ON_FREE : Zero memory before it is freed or cleared
 * 
 * You can add your own flags as well so long as they have the value:
 * 
 *   - 0x0001000
 * 
 * or higher they will be preserved through calls to setUsage and getUsage
 * 
 * @param pThis
 * 
 * @param uFlags All the flag values to set at once.
 * 
 * @return The old flag setting
 * 
 * @memberof DasAry
 */
DAS_API unsigned int DasAry_setUsage(DasAry* pThis, unsigned int uFlags);

/** Returns the usage flags for this array 
 * @memberof DasAry
 */
DAS_API unsigned int DasAry_usage(DasAry* pThis);

/** Increment the count of objects using this Das Array. 
 * 
 * @param pThis the array referenced.
 * 
 * @returns The new count.
 * @memberof DasAry
 */
DAS_API int inc_DasAry(DasAry* pThis);

/** Return the reference count of objects using this array. 
 * @memberof DasAry
 */
DAS_API int ref_DasAry(const DasAry* pThis);

/** Maybe remove the array.
 * 
 * Calling this function decrements the reference count for the array.  If the
 * count reaches zero all backing buffers (owned by this array) are deleted.
 * 
 * @note See "An aside on reference counting and const" in inc_DasAry()
 * 
 * @memberof DasAry
 */
DAS_API void dec_DasAry(DasAry* pThis);


/** Take ownership of array element memory
 *
 * Internally all Das Array elements are stored in a single continuous 1-D
 * buffer.  When the Das Array is deleted, this buffer is removed as well,
 * if it's owned by the das array.
 *
 * @param pThis A pointer to a das array structure
 * 
 * @param pLen A pointer to a variable to hold the length of the element
 *         array in elements (not bytes).  If the array had no values
 *         then length is set to 0
 *
 * @param  pOffset Where to store the offset in elements (not bytes) to 
 *         the start of valid values.  Internally arrays may have invalid
 *         values in the memory buffer before valid values start.
 *   
 * @return A pointer to the raw 1-D element buffer if the Array actually owned
 *         the elements, NULL otherwise.  If not NULL the returned pointer
 *         may be passed to the free() function.
 *         See additional return notes below.
 *
 * Return Notes: 
 *   Buffer memory is not allocated until elements are inserted (lazy
 *   allocation) thus an empty array WILL NOT own any elements.  So this
 *   this function will ALWAYS return NULL for an empty array.  Use the value
 *   returned by pLen to see if the NULL return was because the array didn't
 *   own it's elements or if there were no elements to own.
 *
 *   Once elements are disowned, they can't be disowned again.  Calling 
 *   disownElements twice in succession in a single threaded program will
 *   always return NULL on the second call.
 *
 * Implementation detail:  The internal ragged index tracking arrays may still
 * be owned by the DasAry object and will be deleted when the overall structure
 * is deleted
 * 
 * @memberof DasAry
 */
DAS_API uint8_t* DasAry_disownData(
	DasAry* pThis, size_t* pLen, size_t* pOffset
);

/** Does this array own it's own memory?
 *
 * If an array owns it's element memory then DasAry_disownData will
 * return a valid memory buffer if there are any valid values.
 *
 * @return true if the array owns it's element memory, false otherwise.
 * @memberof DasAry
 */
DAS_API bool DasAry_ownsData(const DasAry* pThis);

/** Get the name of an array
 *
 * @param pThis A constant pointer to this array structure
 * @returns A constant pointer to the identifier for the array
 * @memberof DasAry
 */
DAS_API const char* DasAry_id(const DasAry* pThis);

/** Set the name of an array 
 * 
 * @param pThis A pointer to an array structure
 * @param sId The new id, copied
 * @returns (void)
 * @memberof DasAry
 */
DAS_API void DasAry_setId(DasAry* pThis, const char* sId);

/** Get the number of dimensions in an array
 *
 * @param pThis a pointer to a das array structure
 * @return a value greater than 0 giving the number of dimensions in the array
 * @memberof DasAry
 */
DAS_API int DasAry_rank(const DasAry* pThis);

/** Get the units for the values in the array 
 * @memberof DasAry
 */
DAS_API DasUnits DasAry_units(const DasAry* pThis);

/** Get the type of value stored in the array if knownh
 *
 * This function is used by dataset objects to know how to cast pointers
 * to different data array values.
 *
 * @param pThis A constant pointer to this array structure
 * @returns A constant pointer to string containing the name for the values in
 *          the array, or NULL if the value type has not been set.
 * @memberof DasAry
 */
DAS_API das_vt DasAry_vt(const DasAry* pThis);

/** Get the type of value stored in the array as a text string 
 * @memberof DasAry
 */
DAS_API const char* DasAry_vtName(const DasAry* pThis);

/** Get a informational string representing the array.
 *
 * @param pThis The Array in question
 * @param sInfo pointer to the destination to hold the info string
 * @param uLen the size of sInfo, including room for the NUL
 * @return sInfo, or NULL if the whole line did not fit.  sInfo is always NUL
 *         terminated when uLen > 0.
 * @memberof DasAry
 */
DAS_API char* DasAry_toStr(const DasAry* pThis, char* sInfo, size_t uLen);

/** Get the total number of data values in an array, regardless of it's shape.
 *
 * @param pThis A constant pointer to this array structure
 * @returns The total number of items stored in the array, regardless
 *          of it's shape.
 * @memberof DasAry
 */
DAS_API size_t DasAry_size(const DasAry* pThis);

/** Get the size in bytes of each element stored in the das array
 *
 * @param pThis The Array in question
 * @return The size of each element in bytes
 * @memberof DasAry
 */
DAS_API size_t DasAry_valSize(const DasAry* pThis);


/** Get number of bytes currently used for data values and associated indexes
 * by this dynamic array.
 * 
 * This is not the number of bytes allocated for dynamic storage.  It
 * represents how much of the allocated memory is currently in use.  Memory is
 * used for the actual data values themselves, as well as the index arrays
 * that point to index roll-over points.
 * 
 * This is the number of bytes of dynamic storage used.  For sub-arrays
 * the value is zero, since they don't use any dynamic memory.
 * 
 * @param pThis a DasAry structure
 * 
 * @returns The sum of heap bytes currently holding data values and array 
 *          indexes by this array.
 * 
 * @see DasAry_memOwned() for the number of bytes allocated
 * @see DasAry_memIndexed() for the number of bytes indexed, even if it's
 *      owned by some other array.
 */
DAS_API size_t DasAry_memUsed(const DasAry* pThis);

/** Get the number of bytes currently owned for use in storing data values and
 * indexes.
 * 
 * This is the number of bytes allocated for dynamic storage.  For sub-arrays
 * the value is zero, since they don't own their own memory.
 * 
 * @note Arrays allocated more memory then is needed during append operations
 * to avoid frequent (and time expensive) malloc calls.  Calling DasAry_clear()
 * does not actually free memory, it only marks it available for reuse.
 * 
 * @param pThis a DasAry structure
 * 
 * @returns The sum of heap bytes currently allocated for data values and
 *    array indexes, even if those bytes are not currently in use.
 */
DAS_API size_t DasAry_memOwned(const DasAry* pThis);


/** Get the number of bytes needed to store these values and their associated
 * indexes.
 * 
 * Even if the array doesn't own it's own memory, this function will not
 * return unless there are no values accessible by this array.
 * 
 * @param pThis a DasAry structure
 * 
 * @returns The sum of heap bytes currently holding data values and array
 *   indexes accessible from this DasAry structure, even if the memory is
 *   owned by some other array.
 */
DAS_API size_t DasAry_memIndexed(const DasAry* pThis);


/** Return the current max value + 1 for any index
 *
 * This is a more general version of DasAry_shape that works for both cubic arrays
 * and those with ragged dimensions.  For any index to be inspected, the value
 * of all previous indices must be given.  This is less confusing then it sounds,
 * see the example below
 *
 * @see DasAry_shape For simple arrays that are only ragged in the first index.
 *
 * @param pThis A pointer to an array object
 *
 * @param nAddr The number of location indices, should be less than the rank
 *             in order to get the length of a range of values. The macros
 *             DAS_ADDR0, DAS_ADDR1, DAS_ADDR2, etc. are provided which combine
 *             this argument and the one below to make calling code more
 *             readable, see the example below.
 *
 * @param pAddr  A list of the values for previous indices.  The macros DAS_ADDR1,
 *              DAS_ADDR2, DAS_ADDR3 etc. are provided which combine this argument
 *              with the one above to make calling code more readable, see the
 *              example below.
 *
 * @return The current maximum valid value for the <i>i<sup>th</sup></i>
 *         index at current location in the previous indices.
 *
 * @code
 *
 * // Here pWBR is a ragged array of all waveform amplitudes taken in a single
 * // capture.  Dimension 0 corresponds to the capture time point and dimension
 * // 1 corresponds to each sample in a capture.
 *
 * // Print the number of samples in each waveform.
 * size_t uWaveforms = DasAry_lengthIn(pWBR, DAS_ADDR0);
 * for(size_t u = 0; u &lt; uWaveforms; ++u)
 *	   printf("Capture %zu has %zu samples\n", u, DasAry_lengthIn(pWBR, DAS_ADDR1(u)));
 * @endcode
 *
 * @see DasAry_valAt to obtain both the length and a pointer to a continuous
 *      range of data values an once.
 * @memberof DasAry
 */
DAS_API size_t DasAry_lengthIn(const DasAry* pThis, int nAddr, ptrdiff_t* pAddr);

/** Count the logical items in a subset of the array.
 *
 * Unlike DasAry_lengthIn, which reports the length of the NEXT index down,
 * this walks the whole sub-space below the given point and, for arrays
 * flagged DASARY_AS_SUBSEQ (or stronger, e.g. DASARY_AS_STRING), excludes the
 * final payload index -- so a string or byte-blob counts as ONE item no
 * matter how many bytes it holds, while a vector's components each count.
 * 
 * This is useful for counting encodables in a stream.
 *
 * @param pThis the array
 * @param nAddr the number of indices provided in pAddr (0 for the whole array)
 * @param pAddr the fixed index values, or NULL if nAddr is 0.  The DAS_ADDR0,
 *             DAS_ADDR1() ... macros may be used to supply both arguments.
 * @returns the number of logical items in the subset
 * @memberof DasAry
 */
DAS_API size_t DasAry_itemsIn(const DasAry* pThis, int nAddr, ptrdiff_t* pAddr);

/** Return current valid ranges for this array indices.
 *
 * @see DasAry_lengthIn
 *
 * @param pThis pointer to an array object
 *
 * @param[out] pShape pointer to an array to received the current number of
 *        entries in each dimension, should be at least RANK in length.  Each
 *        element of the output array will be one of the following.
 * 
 *        * An integer from 0 to LONG_MAX to indicate a valid index range
 * 
 *        * The value DASIDX_RAGGED to indicate that the valid index is variable
 *          and depends on the values of other indices.
 *
 * @returns The rank of the array.
 *
 * @memberof DasAry
 */
DAS_API int DasAry_shape(const DasAry* pThis, ptrdiff_t* pShape);


/** Return the strides used for offset calculations.
 *
 * To support fast iteration over array data it's often useful to get a raw
 * pointer and then stride across the 1-D array using an index calculation.
 * Ragged arrays do not have a uniform stride, but many arrays are not 
 * ragged and sub-sections of ragged arrays may not be ragged.  Use this 
 * function to get the stride coefficients.
 *
 * @param pThis pointer to an array object
 *
 * @param[out] pShape The extent of the array in each index.  The total number
 *        of elements is just the multiple of all values in this array up to
 *        the rank.
 * 
 * @param[out] pStride pointer to an array to receive the number of elements to
 *        increment for each successive value of this index.   Note that this
 *        is *not* the number of bytes to stride.  Use DasAry_valSize() to
 *        get the size of each element.
 *
 * @returns the rank of the array, or -1 if an input error is detected.
 *
 * @memberof DasAry
 */
DAS_API int DasAry_stride(
	const DasAry* pThis, ptrdiff_t* pShape, ptrdiff_t* pStride
);

/** Return the fill value for this array
 * 
 * The caller is responsible for casting to the proper type
 */
DAS_API const uint8_t* DasAry_fill(const DasAry* pThis);

/** Change the fill value for this array
 * 
 * Set the fill value but don't re-write the data in the array.  Any data
 * locations in the array will still have the fill value.  To change them
 * iterate over the array looking for the old fill value an replace them in a
 * loop.
 * 
 * @param pThis The array to structure to alter
 * @param pFill Pointer to the replacement fill value, use NULL to set this
 *              to the canonical fill value for this type.
 * @param vt The element type for the new fill value.  This is used as a cross
 *        check.  The element type of the new fill value must match the old
 *        one.
 * @returns true if fill setting succeeded, false otherwise.
 * @memberof DasAry
 */
DAS_API bool DasAry_setFill(DasAry* pThis, das_vt vt, const uint8_t* pFill);


/** Is a valid item located at a complete index
 *
 * @param pThis A pointer to the array
 *
 * @param nAddr  The number of indices in pAddr, which must equal the array's
 *              rank.  The macros DAS_ADDR1, DAS_ADDR2, etc. supply nAddr and pAddr
 *              together, see the example below.
 *
 * @param pAddr  The indices, nAddr of them.  Negative values count back from
 *              the end of their index.
 *
 * @returns true if the location list refers to a valid array index set,
 *          false otherwise (daserrf is called for a count that does not
 *          match the rank).  Note that the actual value at the index may be
 *          a fill value.
 * @code
 * DasAry* pAry = new_DasAry(
 *    "amplitudes", DASVT_FLOAT, 0, NULL, DAS_SHAPE3(0, 160, 80), DASUNITS_DIMENSIONLESS
 * );
 *
 * size_t uRec = 0;
 * // See if we have a complete MARSIS frame for this record...
 * if(! DasAry_isValidAt(pAry, DAS_ADDR3(uRec, 159, 79)))
 *    fprintf(stderr, "Error: Short frame count in record %zu", uRec);
 * @endcode
 * @memberof DasAry
 */
DAS_API bool DasAry_isValidAt(const DasAry* pThis, int nAddr, const ptrdiff_t* pAddr);

/** Get a pointer to an element at a complete index
 *
 * For type safety the macros DasAry_floatAt, DasAry_doubleAt, etc. have
 * been provided and should be used instead of the base function here.
 *
 * @param pThis A pointer to the array
 *
 * @param et    The element type pointer expected at the given location, this
 *              is used by the type checking macros DasAry_floatAt and
 *              friends.
 *
 * @param nAddr  The number of indices in pAddr, which must equal the array's
 *              rank: a rank 2 array takes two indices to reach an element, a
 *              rank 3 takes three, etc.  The macros DAS_ADDR1, DAS_ADDR2, etc.
 *              supply nAddr and pAddr together.  See the example below.
 *
 * @param pAddr  The indices, nAddr of them.  Negative values count back from
 *              the end of their index.
 *
 * @returns a pointer to value at the given indices, or NULL if that location
 *          is not valid, or nAddr is not the rank, and the library returns on
 *          errors, see daslib_init().
 *
 * @code
 * // Uses type checking macro's
 * DasTime dt = DasAry_timeAt(pAry, DAS_ADDR1(uRec));
 *
 * // Get last event, wherever it is
 * const char* sEvent = DasAry_textAt(pAry, DAS_ADDR1(-1));
 * @endcode
 *
 * @see DasAry_valsIn to access multiple values at once avoiding function call
 *      overhead in tight loops.
 * @memberof DasAry
 */
DAS_API const uint8_t* DasAry_valAt(
	const DasAry* pThis, das_vt et, int nAddr, const ptrdiff_t* pAddr
);

/* The typed wrappers below take the address as "..." so that one DIMn_AT()
   argument can expand into the nAddr and pAddr pair: a macro counts its
   arguments before it expands them. */

/** Wrapper around DasAry_valAt for IEEE-754 binary32 (float)
 * @relates DasAry */
#define DasAry_floatAt(pThis, ...)  *((float*)(DasAry_valAt(pThis, DASVT_FLOAT, __VA_ARGS__)))
/** Wrapper around DasAry_valAt for IEEE-754 binary64 (double)
 * @relates DasAry */
#define DasAry_doubleAt(pThis, ...)  *((double*)(DasAry_valAt(pThis, DASVT_DOUBLE, __VA_ARGS__)))
/** Wrapper around DasAry_valAt for unsigned bytes
 * @relates DasAry */
#define DasAry_byteAt(pThis, ...)  *((uint8_t*)(DasAry_valAt(pThis, DASVT_UBYTE, __VA_ARGS__)))
/** Wrapper around DasAry_valAt for unsigned 16-bit integers
 * @relates DasAry */
#define DasAry_ushortAt(pThis, ...)  *((uint16_t*)(DasAry_valAt(pThis, etUint16, __VA_ARGS__)))
/** Wrapper around DasAry_valAt for signed 16-bit integers
 * @relates DasAry */
#define DasAry_shortAt(pThis, ...)  *((int16_t*)(DasAry_valAt(pThis, etInt16, __VA_ARGS__)))
/** Wrapper around DasAry_valAt for 32-bit integers
 * @relates DasAry */
#define DasAry_intAt(pThis, ...)  *((int32_t*)(DasAry_valAt(pThis, etInt32, __VA_ARGS__)))
/** Wrapper around DasAry_valAt for signed 64-bit integers
 * @relates DasAry */
#define DasAry_longAt(pThis, ...)  *((int64_t*)(DasAry_valAt(pThis, etInt64, __VA_ARGS__)))
/** Wrapper around DasAry_valAt for DasTime structures
 * @relates DasAry */
#define DasAry_timeAt(pThis, ...)  *((DasTime*)(DasAry_valAt(pThis, DASVT_TIME, __VA_ARGS__)))
/** Wrapper around DasAry_valAt for pointers to null-terminated strings
 * @relates DasAry */
#define DasAry_textAt(pThis, ...)  *((char**)(DasAry_valAt(pThis, DASVT_TEXT, __VA_ARGS__)))

/** Wrapper around DasAry_valAt for DASVT_UNK pointers. */
#define DasAry_ptrAt(pThis, ...)  *((void**)(DasAry_valAt(pThis, DASVT_UNK, __VA_ARGS__)))


/** Set values starting at a complete index
 *
 * Note, this will not expand the size of the array.  Use the function
 * append() to automatically grow the array to store the desired number of
 * items.
 *
 * @param pThis A pointer to the array
 * @param nAddr The number of indices in pStart, which must equal the array's
 *        rank.  DAS_ADDR1, DAS_ADDR2, etc. supply nAddr and pStart together.
 * @param pStart The complete index to the starting point to write values
 * @param pVals The values to write
 * @param uVals The number of values to write
 * @returns true if the items could be written, false if the array is not large
 *          enough to hold all the values, nAddr is not the rank, or any other
 *          error is encountered
 * @memberof DasAry
 */
DAS_API bool DasAry_setValsAt(
	DasAry* pThis, int nAddr, const ptrdiff_t* pStart, const uint8_t* pVals,
	size_t uVals
);

/** Get a pointer to the elements contained by a partial index
 *
 * @param pThis A Array containing the data of interest
 *
 * @param et The expected type of element to be returned.  This is used by the
 *        type safe macros DasAry_doubleAt, DasAry_textAt, etc.
 *
 * @param nAddr The number of location indices, should be less than the rank
 *             in order to get the length of a range of values. The macros
 *              DAS_ADDR0, DAS_ADDR1, DAS_ADDR2, etc. are provided which combine
 *              this argument and the one below to make calling code more
 *              readable, see the example below.
 *
 * @param pAddr An array of location indices, nAddr long.  Use the macros
 *             DAS_ADDR0, DAS_ADDR1, DAS_ADDR2, etc. for cleaner code.
 *
 * @param pCount A pointer to a variable to hold the number of elements under
 *        the given index.  If nIndices is equal to the array rank the returned
 *        value will be at most 1.
 *
 * @return A pointer a continuous subset of elements, or NULL if no elements are
 *         located at the given index set.  You will have to cast this pointer
 *         to the element type.
 *
 * @code
 * // Print all the events in a rank 1 array of strings
 * size_t uVals;
 * const char** events = DasAry_textIn(pAry, DAS_ADDR0, &uVals);
 * for(size_t u = 0; u < uVals; ++u)
 *    printf("Event %06zu: %s\n", u, events[u]);
 * @endcode
 *
 * @code
 * // Print all the magnetic amplitudes for a single time slice at index 117
 * size_t uVals;
 * const float* pAmp = DasAry_floatsIn(pAry, DAS_ADDR1(117), &uVals);
 * for(size_t u = 0; u < uVals; ++u)
 *    printf("Amp at freq %03zu: %.3e nT**2/Hz \n", u, pAmp[u]);
 *
 * @endcode
 * @memberof DasAry
 */
DAS_API const uint8_t* DasAry_valsIn(
	const DasAry* pThis, das_vt et, int nAddr, ptrdiff_t* pAddr, size_t* pCount
);

/** This is the writable pointer version of DasAry_valsIn
 * 
 * See the argument list form DasAry_valsIn()
 * @return A raw byte pointer suitable for direct value insertion.  Not that
 *         the amount of space for writing is *pCount times the element size 
 *         from DasAry_valSize().
 */
DAS_API uint8_t* DasAry_dataIn(
	DasAry* pThis, das_vt et, int nAddr, ptrdiff_t* pAddr, size_t* pCount
);

/** A wrapper around DasAry_valsIn that casts the output and preforms type checking
 * @relates DasAry */
#define DasAry_floatsIn(T, ...) (const float*) DasAry_valsIn(T, DASVT_FLOAT, __VA_ARGS__)

/** A wrapper around DasAry_valsIn that casts the output and preforms type checking
 * @relates DasAry */
#define DasAry_doublesIn(T, ...) (const double*) DasAry_valsIn(T, DASVT_DOUBLE, __VA_ARGS__)

/** A wrapper around DasAry_valsIn that casts the output and preforms type checking
 * @relates DasAry */
#define DasAry_charsIn(T, ...) (const char*) DasAry_valsIn(T, DASVT_UBYTE, __VA_ARGS__)

/** A wrapper around DasAry_valsIn that casts the output and preforms type checking
 * @relates DasAry */
#define DasAry_bytesIn(T, ...) (const uint8_t*) DasAry_valsIn(T, DASVT_UBYTE, __VA_ARGS__)

/** A wrapper around DasAry_valsIn that casts the output and preforms type checking
 * @relates DasAry */
#define DasAry_ushortsIn(T, ...) (const uint16_t*) DasAry_valsIn(T, DASVT_USHORT, __VA_ARGS__)

/** A wrapper around DasAry_valsIn that casts the output and preforms type checking
 * @relates DasAry */
#define DasAry_shortsIn(T, ...) (const int16_t*) DasAry_valsIn(T, DASVT_SHORT, __VA_ARGS__)

/** A wrapper around DasAry_valsIn that casts the output and preforms type checking
 * @relates DasAry */
#define DasAry_intsIn(T, ...) (const int32_t*) DasAry_valsIn(T, DASVT_INT, __VA_ARGS__)

/** A wrapper around DasAry_valsIn that casts the output and preforms type checking
 * @relates DasAry */
#define DasAry_longsIn(T, ...) (const int64_t*) DasAry_valsIn(T, DASVT_LONG, __VA_ARGS__)

/** A wrapper around DasAry_valsIn that casts the output and preforms type checking
 * @relates DasAry */
#define DasAry_timesIn(T, ...) (const DasTime*) DasAry_valsIn(T, DASVT_TIME, __VA_ARGS__)

/** A wrapper around DasAry_valsIn that casts the output and preforms type checking
 * @relates DasAry */
#define DasAry_textIn(T, ...) (const char**) DasAry_valsIn(T, DASVT_TEXT, __VA_ARGS__)

/** Forget all the fancy indexing, just get a pointer to all the elements
 * 
 * This can be handy for just writing array data to disk, be warned that
 * it is not sufficient.  In addition the stride coefficients will need to
 * be saved if the array is a qubic.  If not, all the indexes will also
 * need to be saved.  
 * 
 * @see DasAry_getAllIdx for a raw return of the associated index buffers.
 * 
 * @param pThis A pointer to an array structure
 * 
 * @param pElSize A pointer to a location to receive the element size
 * 
 * @param pElements A pointer to a location to receive the number of valid
 *        elements
 * 
 * @returns A pointer to the first valid element, or NULL if no elements 
 *        are valid.  Note that DasAry storage is *always* dense.  There
 *        is no reason to believe that the returned pointer can be strided.
 */
DAS_API const uint8_t* DasAry_data(
   const DasAry* pThis, size_t* pElSize, size_t* pElements
);

/** Get a lower rank array that is a sub-set of the current array.
 *
 * For some given number of indices, produce a sub-array.  This is similar to
 * DasAry_valAt but produces a whole new Array object whose data are provided
 * stored in a separate Array.
 *
 * @code
 * // Get all data for the 10th record of a 2-D dataset, would often be a
 * // time slice for das2 streams.
 * DasAry* pRec = new_DasAry_slice(pAllData, "rec_10", DAS_ADDR1(10));
 *
 * // Get the time delays for frequency index 100 for time point 22 of a
 * // MARSIS AIS data stream
 * DasAry* pDelays = new_DasAry_slice(pAllData, "rec_22", DAS_ADDR2(22, 100));
 * @endcode
 *
 * @param pThis A pointer to the array to subset.  It is not a constant
 *              pointer due to the need to increment the reference count.
 *
 * @param id A identifying name for the new sub-array
 *
 * @param nIndices The number of location indices, should be less than the rank
 *               in order to get a range of values. Use the macros DAS_ADDR0,
 *               DAS_ADDR1, DAS_ADDR2, etc. for cleaner code.
 *
 * @param pAddr An array of location indices, nIndices long.  Use the macros
 *             DAS_ADDR0, DAS_ADDR1, DAS_ADDR2, etc. for cleaner code.
 *
 * @return A new Array allocated on the heap that does not own it's backing
 *         buffer.
 *
 * @memberof DasAry
 */
DAS_API DasAry* new_DasAry_slice(
	DasAry* pThis, const char* id, int nIndices, ptrdiff_t* pAddr
);

/** Use fill values to make sure the last subset in a dimension is a QUBE
 *
 * @param pThis the array
 *
 * @param iRecDim The dimension to qube, typically the last dimension in the
 *        array, dimensions are numbered starting from 0.  Use the macro's DAS_IDX1,
 *        DAS_IDX2, etc. to make the code more readable.  Dimension 0 can't be
 *        marked as ended and the macro DAS_ADDR0 will not work here.
 *
 * @returns the number of fill values added to the array
 *
 * @code
 * DasAry* pAry = new_DasAry(
 *    "marsis", DASVT_FLOAT, 0, NULL, DAS_SHAPE3(0, 160, 80), DASUNITS_DIMENSIONLESS
 * );
 *
 * //Example 1: Read a complete sets of delay times
 * float buf[80];
 * size_t uRead = fread(buf, sizeof(float), 80, stdin);
 * DasAry_append(pAry, (const uint8_t*)buf, uRead);
 * DasAry_qubeIn(pAry, DAS_IDX2);
 *
 * //Example 2: Read in complete ionograms
 * float buf[160*80];
 * size_t uRead = fread(buf, sizeof(float), 80*160, stdin);
 * DasAry_append(pAry, (const uint8_t*)buf, uRead);
 * DasAry_qubeIn(pAry, DAS_IDX1);
 *
 * @endcode
 *
 * @see DasAry_append
 * @memberof DasAry
 */
DAS_API size_t DasAry_qubeIn(DasAry* pThis, int iRecDim);

/** Append some number of items to the end of the array.
 * This works as you would expect for all arrays that are only ragged in the
 * 0th dimension.
 *
 * @see DasAry_markEnd.  Arrays that are ragged in dimension other that the 0th
 * need some way to know that it's time to roll the index back to 0 on the next
 * append operation, DasAry_markEnd sets the needed flags.
 *
 * @param pThis The array which should copy in the new values.
 * 
 * @param pVals A constant pointer to values to add MAY BE NULL!  If NULL
 *              uCount fill values are appended
 * 
 * @param uCount The number of values to add, *not* the number of bytes to
 *          add.  Total content appended to the array will be of length
 *          uCount * element_size
 * 
 * @returns A pointer to the first value written or NULL if an error occurred
 *
 * @memberof DasAry
 */
DAS_API uint8_t* DasAry_append(DasAry* pThis, const uint8_t* pVals, size_t uCount);

/** Mark a ragged dimension as finished
 *
 * @param pThis The das array
 *
 * @param iDim The dimension which should have it's index rolled back to zero
 *              on the next insert.  Marking the end of a low index (say 1)
 *              automatically marks the end of any higher indices (ex 2,3).
 * @code
 * // Read in lines of text into an array that stores data by page number,
 * // line number and the byte number.  Input processing is simplistic in
 * // order to focus on Array calls.
 *
 * uint8_t fill = 0;
 * DasAry* pAry = new_DasAry(
 *    "source", DASVT_UBYTE, 0, &fill, DAS_SHAPE3(0,0,0), DASUNITS_DIMENSIONLESS
 * );
 * char sBuf[1024] = {'\0'};
 * size_t uLen = 0;
 * while(!feof(stdin)){
 *    while(fgets(sBuf, 1024, stdin)){
 *       uLen = strlen(sBuf);
 *       if(sBuf[0] == 0x0C) DasAry_markEnd(pAry, DAS_IDX1);        // end page
 *	      DasAry_append(pAry, (const uint8_t*)sBuf, uLen+1); // keep NULL terminators
 *	      if(sBuf[uLen - 1] == '\n') DasAry_markEnd(pAry, DAS_IDX2); // end line
 *    }
 * }
 * @endcode
 * @memberof DasAry
 */
DAS_API void DasAry_markEnd(DasAry* pThis, int iDim);

/* Remove some number of records in a given index.
 *
 * This does not actually delete any data it just removes references to it.
 * Thus any subsets of this array will still reference the existing values.
 *
 * @see rmLastRec() for the definition of a record
 */
/* size_t DasAry_rmHead(Array* pThis, size_t uCount, int nIndex);*/

/* Remove N records from the end of the array
 *
 * For rank 1 arrays, a 'record' is just a single value, for higher rank
 * arrays a record is the number of values represented by a change in the
 * first index.  For example, for the array:
 * @code
 *    a[][10][4]
 * @endcode
 * calling
 * @code
 *   rmTail(pArray, 2)
 * @endcode
 * will result in 80 individual values being remove from the array.
 *
 * @param pThis the array to change
 * @param uRecs the number of records to remove.  If uRecs is greater than
 *        the shape of the first index, everything is removed and an error
 *        is thrown.
 * @returns The new size of the array
 * @memberof DasAry
 */
/* size_t DasAry_rmTail(Array* pThis, size_t uRecs); */

/** Clear all values from the array
 *
 * This operation internally just resets the count of items to 0 in all
 * arrays it does not free memory.
 * Dimensions above the 0th retain their shape.
 *
 * @param pThis The array to clear
 * @returns then number of items cleared
 * @memberof DasAry
 */
DAS_API size_t DasAry_clear(DasAry* pThis);



#ifdef __cplusplus
}
#endif

#endif /* _das_array_h_ */
