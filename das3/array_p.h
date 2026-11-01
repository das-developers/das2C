/* Copyright (C) 2017-2026 Chris Piker <chris-piker@uiowa.edu>
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

/** @file array_p.h DasAry internals, not installed.
 *
 * Library files that embed a DasAry by value (DasDesc, stream and dataset
 * header parse contexts) or read its streaming bookkeeping include this.
 * Everything else goes through array.h.
 */

#ifndef _das_array_p_h_
#define _das_array_p_h_

#include <das3/array.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Higher dimensions are handled by index buffers.  These are the elements for
 * the index buffers, and the storage behind vtIndex. */
typedef struct child_info_t{
	ptrdiff_t nOffset;        /* Start of child elements data in child buffer */
	size_t uCount;            /* The count child elements in child buffer     */
}das_idx_info;

typedef struct dyna_buf{

	ubyte* pBuf;               /* The beginning a continuous buffer uSize long */
	ubyte* pHead;              /* The beginning of valid values in the buffer  */
	/*ubyte* pWrite;*/         /* The beginning of the append point for the buffer */
	size_t uSize;             /* The amount of space in the backing buffer    */
	size_t uValid;            /* The number of valid elements in this buffer  */
	size_t uElemSz;           /* The number of bytes occupied by each element */

	ubyte* pFill;              /* Pointer to fill value buffer */
	ubyte  fillBuf[sizeof(das_idx_info)];        /* Storage for short fill items */

	size_t uChunkSz;           /* Alloc helper, if set, allocate in even chunks
                               * of this size. */
	size_t uShape;             /* Qube helper, how many items in this array per
										 * parent item, 0 means ragged */
	das_val_type etype;       /* The element type */

	bool bRollParent;          /* The end mark.  If set, adding data to the array
										 * will clear the end mark and trigger creation of
										 * a new parent.  If this is set on all indexes
										 * attempting to add data will fail */

	bool bKeepMem;            /* If true memory will not be deleted when the
									   * buffer is deleted */

} DynaBuf;

struct das_array {
	char        sId[DAS_MAX_ID_BUFSZ];   /* A text identifier of this instance of the array */
	int           nRank;   /* The number of index dimensions for the array */
	das_idx_info*   pIdx0;   /* top lever container, may not point here */

	/* bool      bTopOwned;*/   /* Same as pIdx0 == &index0 */
	das_idx_info   index0;   /* Storage for element 0 of buffer 0, if needed */

	/* Pointers to item arrays.  One array is needed for each dimension,
	 * these may point to internal locations if the array is owned */
	DynaBuf*  pBufs[ARYIDX_MAX];

	/* bool     bOwned[16]; */   /* Same as pBufs[i] == &(bufs[i]) */
	DynaBuf    bufs[ARYIDX_MAX];    /* Storage for dynamic buffers, if needed */

	/* Current compare function, set automatically if a known type is used,
	 * otherwise user needs to supply their own */
	int (*compare)(const ubyte* vpFirst, const ubyte* vpSecond);

	/* Where to send/read data when streaming */
	int nSrcPktId;
	size_t uStartItem;
	size_t uItems;

	/* Since so many datasets and functions can reference the same array,
	 * a reference count is needed to make sure they aren't deleted if
	 * still in use. */
	int refcount;

	/* Used to notify the owner of my memory that I'm not using it anymore. */
	struct das_array* pMemOwner;

	unsigned int uFlags;      /* Store flags indicating intended use */

	das_units units;
};

/* Same as new_DasAry, but initializes caller storage (an embedded member or a
 * stack object) instead of making a heap object.  Returns true on success. */
bool DasAry_init(
   DasAry* pThis, const char* id, das_val_type et, size_t sz_each,
   const ubyte* fill, int rank, size_t* shape, das_units units
);

/* Similar to dec_DasAry, but for objects set up by DasAry_init */
void DasAry_deInit(DasAry* pThis);

#ifdef __cplusplus
}
#endif

#endif /* _das_array_p_h_ */
