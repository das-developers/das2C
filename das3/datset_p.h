/* Copyright (C) 2017-2026 Chris Piker <chris-piker@uiowa.edu>
 *
 * This file is part of das2C, the Core Das C Library.
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

/** @file datset_p.h INTERNAL header, subject to change without notice.
 *
 * Nothing here is API, and it is not installed.
 */

#ifndef _das_datset_p_h_
#define _das_datset_p_h_

#include <das3/datset.h>
#include <das3/desc_p.h>
#include <das3/codec_p.h>   /* DasCodec is embedded by value */

#ifdef __cplusplus
extern "C" {
#endif

/* Number of encoders that can be stored internally, more then this and they
 * have to be allocated on the heap. 
 *
 * Current test case with the most codec/dateste is ex21, at 7.
 * If you bump this make a test case with one more variable than the new
 * value below.
 */
#define DASDS_LOC_ENC_SZ 6

/* Small vector size for array and dimension pointer sets. Larger then
 * DASDS_LOC_ENC_SZ  because these are just sizeof(void*) each not ~200 B.
 *
 * NOTE: No test case reaches these sizes, but both were reduced to 6 for 
 *       a onetime test against ex21_tracers_cdpu_status.d3b (uDims=7, 
 *       uArrays=7), on 2026-07-16.  
 *
 * If you touch the dynamic #of array or dims code, drop these back to 6
 * each and run `make test`.
 */
#define DASDS_LOC_ARY_SZ 32
#define DASDS_LOC_DIM_SZ 32

/* Room for the longest PlotType, "cartesian3d:movie", update if schema changes. */
#define DASDS_PLOT_SZ 24

struct das_ds_t {
	DasDesc base;        /* This would be equivalent to the properties for 
	                        a packet descriptor.  Typically in das 2.2 packets
	                        don't have a descriptor, only streams and planes
	                        but access to the stream descriptor forwards through
	                        here. */
	
	int nRank;           /* The number of whole-dataset index dimensions. 
								 * Variables can define internal dimensions but they
								 * can't use indices in the first nRank positions for
								 * internal use, as these are used to correlate values
								 * across the dataset. */
	
	/* The dataset's name, written in das3 headers.  (sGroupId, sId)
	 * is the dataset key and should be unique in a stream, which nothing
	 * here enforces, see DasDs_name() in datset.h. */
	char sId[DASID_SZ];
	
	/* The join group for this dataset.  Datasets with the same group should
	 * be joined automatically by display clients.  Empty is the null group,
	 * which joins nothing.  Written as group= in das3 headers, and is what
	 * das2 calls a plane or packet name. */
	char sGroupId[DASID_SZ];

	/* Overall plotting hint for dataset consumers, same idea as DasDim.axis.
       See schema for legal values */
	char sPlot[DASDS_PLOT_SZ];

	size_t uDims;       /* Number of dimensions, das datasets are 
	                       * implicitly bundles in qdataset terms. */
	
	DasDim** lDims;      /* The data variable object arrays, internal or external */
	size_t uSzDims;      /* lDims memory size, internal or external */

	DasDim* aDims[DASDS_LOC_DIM_SZ];  /* small vector memory */
	
	size_t uArrays;      /* The number of low-level arrays */
	DasAry** lArrays;    /* Array objects, internal or external */
	size_t uSzArrays;    /* lArrays memory size, internal or external */

	DasAry* aArrays[DASDS_LOC_ARY_SZ];  /* small vector memory */
	
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

	size_t uCodecs;   /* Number of valid codecs */

	/* These become large vector memory when uSzEncs > DASDS_LOC_ENC_SZ */

	/* When the number of valid codecs grows past DASDS_LOC_ENC_SZ, use an
	   external buffer for all of them */

	DasCodec*  lCodecs;   /* Codec pointers, internal or external */
	int*       lItems;    /* Number of items to decode per codec, internal or ex */
	size_t     uSzCodecs; /* Codec & Item array memory size, internal or external */
	
	DasCodec aCodecs[DASDS_LOC_ENC_SZ];  /* small vector memory */
	int aItems[DASDS_LOC_ENC_SZ];      /* small vector memory */

};


/* Moved from datset.h: library plumbing, not API */

/** Decode one packet's worth of data into dataset memory.
 *
 * Uses the dataset's read codecs, in order, to parse the buffer contents into
 * the backing arrays, closing ragged runs (DasAry_markEnd) as needed. 
 * The buffer's read point is advanced past the consumed bytes.
 *
 * This is the bare payload layer: the caller (typically the das IO layer)
 * supplies one packet's content with the |Pd| framing already stripped.
 *
 * @param pThis a dataset with decoders defined, see new_DasDs_xml()
 * @param pBuf the buffer to read, starting from its current read point
 * @returns DAS_OKAY, or a das error code
 * @memberof DasDs
 */
das_errcode DasDs_decodeData(DasDs* pThis, DasBuf* pBuf);

/** Cache the dataset shape instead of recomputing it on every call
 * 
 * DasDs_shape() normally asks every dimension for its shape on each call,
 * since a dataset that is still being read or built may grow at any time.
 * Once the data are known to be final, caching saves that work, which adds
 * up in loops that test the shape on every pass (iterators, for example).
 *
 * Nothing is locked: arrays can still be appended to after the shape is
 * cached, and DasDs_shape() will then return the old, stale shape.  Turn the
 * cache off before changing a dataset again.
 *
 * @param pThis The dataset in question
 *
 * @param bCache true to capture the current shape and return it from then
 *        on, false (the default for new datasets) to recompute on demand.
 *
 * @memberof DasDs
 */
void DasDs_cacheShape(DasDs* pThis, bool bCache);

/** Get the number of bytes in each record of this dataset when serialized
 * 
 * Given the current codec set, determine how many bytes must be read
 * for each packet in a stream.  Works for the fixed encodings typical
 * in *.d3b and *.d3t files but not for serializing to and from pure
 * XML documents.
 * 
 * @param pThis a Dataset structure pointer
 * 
 * @param pBytes receives the number of bytes expected in each packet payload
 *          for this dataset.  It may be 0 if no codecs are defined, and is
 *          -1 for variable length packets.
 *
 * @returns DAS_OKAY, or a positive error code if the header counts add up
 *          to more than an int can hold.  *pBytes is not set on error.
 *
 * @memberof DasDs
 */
das_errcode DasDs_recBytes(const DasDs* pThis, int* pBytes);

#ifdef __cplusplus
}
#endif

#endif /* _das_datset_p_h_ */
