/* Copyright (C) 2017 Chris Piker <chris-piker@uiowa.edu>
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

#ifndef _das_builder_h_
#define _das_builder_h_

#include <das2/processor.h>
#include <das2/dataset.h>

#ifdef __cplusplus
extern "C" {
#endif

struct ds_pd_set{
	PktDesc* pPd;
	DasDs* pDs;
};

/** Builds datasets from a das2 stream
 *
 * General usage for this object type would be:
 *
 * @code
 * DasIO* pIn = new_DasIO_file("myprogram", stdin, "r");
 * Builder pBldr = new_Builder();
 * DasIO_addProcessor(pIn, (StreamHandler*)pBldr);
 * DasIO_readAll(pIn);
 * size_t nSets = 0;
 * DataSet** lDataSets = Builder_datasets(pBldr, &nSets);
 * @endcode
 *
 * @extends StreamHandler
 * @class Builder
 * @ingroup datasets
 */
typedef struct das_builder {
	StreamHandler base;

   /** Holds the global properties and the frame definitions.  When reading is
    * finished, all datasets are set as children of this stream object */
	DasStream* pStream;

	bool _released;       /* true if stream taken over by some other object */

	/* Das2 allows packet descriptors to be re-defined.  This is annoying but
	 * we have to deal with it.  Here's the tracking mechanism
	 *
	 * lDsMap - The index in this array is a packet ID
	 *          The value is the index in lPairs that holds a copy of the packet
	 *          descriptor and it's dataset.
	 *
	 * lPairs -    The dataset and corresponding packet descriptors.
	 *
	 * If a packet ID is re-defined, first look to see if the new definition is
	 * actually something that's been seen before and change the value in lDsMap
	 * to the old definition. */
	int lDsMap[MAX_PKTIDS];

	size_t uValidPairs;
	struct ds_pd_set* lPairs;
	size_t uSzPairs;

} DasDsBldr;

/** Generate a new dataset builder.
 *
 * @return A new dataset builder allocated on the heap suitable for use in
 *         DasIO::addProcessor()
 *
 * @member of DasDsBldr
 */
DAS_API DasDsBldr* new_DasDsBldr(void);

/** Delete a builder object, freeing it's memory and the array memory if
 * it has not been released
 *
 * @param pThis
 * @member of DasDsBldr
 */
DAS_API void del_DasDsBldr(DasDsBldr* pThis);

/** Detach data ownership from builder.
 *
 * Call this function to indicate that deleting the builder should not delete
 * any DataSets or properties that have been constructed.  If this call is no
 * made, then del_Builder() will also deallocate any dataset objects and
 * descriptor objects that have been generated.
 *
 * @member of DasDsBldr
 */
DAS_API void DasDsBldr_release(DasDsBldr* pThis);

/** Get a stream object that only contains datasets, even for das2 streams
 * 
 * @param pThis a pointer to a builder object
 * 
 * @return A pointer to a stream descriptor object. If DasDsBldr_release()
 *         has been called, then the caller is assumed to own the stream
 *         descriptor and all it's children.
 */
DAS_API DasStream* DasDsBldr_getStream(DasDsBldr* pThis);

/** Gather all correlated data sets after stream processing has finished.
 *
 * @deprecated Use DasDsBldr_getStream() instead
 * 
 * @param[in] pThis a pointer to this builder object.
 * @param[out] uLen pointer to a size_t variable to receive the number of
 *         correlated dataset objects.
 * @return A pointer to an array of correlated dataset objects allocated on the
 *         heap.  Each data correlation may contain 1-N datasets
 *
 * @member of DasDsBldr
 */
DAS_DEPRECATED DAS_API DasDs** DasDsBldr_getDataSets(DasDsBldr* pThis, size_t* pLen);

/** Get a pointer to the global properties read from the stream.
 * The caller does not own the descriptor unless Builder_release() is called.
 *
 * @deprecated Use DasDsBldr_getStream() instead.
 * 
 * @param pThis a pointer to the builder object
 * @return A pointer the builder's copy of the top-level stream descriptor,
 *         or NULL if no stream was read, or it had no properties
 * 
 * @member of DasDsBldr
 */
DAS_DEPRECATED DAS_API DasDesc* DasDsBldr_getProps(DasDsBldr* pThis);

/** Convenience function to read all data from standard input and store it
 *  in memory.
 *
 * @deprecated Use stream_from_stdin() instead
 * 
 * @param sProgName the name of the program for log writing.
 *
 * @param pSets pointer to a value to hold the number of datasets read from
 *              standard input
 *
 * @return NULL if there was an error building the dataset, an array of
 *         correlated dataset pointers otherwise
 */
DAS_DEPRECATED DAS_API DasDs** build_from_stdin(
	const char* sProgName, size_t* pSets, DasDesc** ppGlobal
);

/** Convenience function to read all data from standard input and store it
 *  in memory.
 *
 * @deprecated Use stream_from_stdin() instead
 * 
 * @param sProgName the name of the program for log writing.
 *
 * @param pSets pointer to a value to hold the number of datasets read from
 *              standard input
 *
 * @return NULL if not even a stream header was read in.
 */
DAS_API DasStream* stream_from_stdin(const char* sProgName);

#ifdef __cplusplus
}
#endif

#endif	/* _das_builder_h_ */

