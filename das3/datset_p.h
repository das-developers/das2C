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

struct dataset {
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
	
	/* The dataset's name, unique within its group; (sGroupId, sId) is the
	 * dataset key.  Written as name= in das3 headers. */
	char sId[DAS_MAX_ID_BUFSZ];
	
	/* The join group for this dataset.  Datasets with the same group should
	 * be joined automatically by display clients.  Empty is the null group,
	 * which joins nothing.  Written as group= in das3 headers, and is what
	 * das2 calls a plane or packet name. */
	char sGroupId[DAS_MAX_ID_BUFSZ];

	/* Overall plotting hint for dataset consumers, same idea as DasDim.axis.
       See schema for legal values */
	char sPlot[DASDS_PLOT_SZ];
								
	size_t uDims;        /* Number of dimensions, das datasets are 
	                       * implicitly bundles in qdataset terms. */
	
	DasDim** lDims;      /* The data variable object arrays, internal or external */
	size_t uSzDims;      /* lDims memory size, internal or external */

	DasDim* aDims[DASDS_LOC_DIM_SZ];  /* small vector memory */
	
	size_t uArrays;      /* The number of low-level arrays */
	DasAry** lArrays;    /* Array objects, internal or external */
	size_t uSzArrays;    /* lArrays memory size, internal or external */

	DasAry* aArrays[DASDS_LOC_ARY_SZ];  /* small vector memory */
	
	ptrdiff_t _shape[VARIDX_MAX];  /* cache shape calls for speed */
	
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

	/* Set to true when encode is called, make sure data doesn't go 
	 * out the door unless the descriptor is sent first */
	bool bSentHdr;

};

#ifdef __cplusplus
}
#endif

#endif /* _das_datset_p_h_ */
