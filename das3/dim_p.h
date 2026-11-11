/* Copyright (C) 2026 Chris Piker <chris-piker@uiowa.edu>
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

/** @file dim_p.h INTERNAL header, subject to change without notice.
 *
 * Nothing here is API, and it is not installed.
 */

#ifndef _das_dim_p_h_
#define _das_dim_p_h_

#include <das3/dim.h>
#include <das3/desc_p.h>

#ifdef __cplusplus
extern "C" {
#endif

struct das_dim {
	DasDesc base;        /* Attributes or properties for this variable */
	das_dim_type_e dtype;    /* Coordinate or Data flag */
	
   /* A name for this particular variable group, cannot repeat in the dataset */
   char sId[DAS_MAX_ID_BUFSZ]; 

   /* A general dimension category such as 'B', 'E', etc */
   char sDim[DAS_MAX_ID_BUFSZ]; 

   /* Display Info: Plot axes affinity, if any. For variables that have no
    * internal indices, only the first axis make any sense.  Multiple axis 
    * entries are possible because this dimension may contain a vector.
    *
    * A common example of a vector is a "space" dimension defined by a 
    * 3-vector.
    */
   char axes[DASDIM_NAXES][DASDIM_AXLEN];

   /* Display info: Is this the primary coordinate for a given axes */
   bool primary;

	/* Holds the max index to report out of this dimension.
	 * The dimension may have internal indices beyond these
	 * but they are not correlated with the overall dataset 
	 * indices */
	int iFirstInternal;
	
	/* The variables which supply data for this dimension */
	DasVar* aVars[DASDIM_MAXVAR];
	char aRoles[DASDIM_MAXVAR][DASDIM_ROLE_SZ];
	size_t uVars;
	
	/* For dependent variables (i.e. data) pointers to relevant independent
	 * dimensions are here.  I don't think we need this here as the dataset
	 * provides this information.  Going to punt it for now but will use
	 * orthogonality checks when printing coordinate information */
	/* struct das_dim* aCoords[DASDIM_MAXDEP];
	size_t uCoords;*/
};

/* Write this dimension's das3 <coord>/<data> element.  Not DAS_API, the
   dataset header writer in datset.c is the caller, user code writes whole
   datasets with DasIO_writeDesc(). */
das_errcode DasDim_encode(DasDim* pThis, DasBuf* pBuf);

#ifdef __cplusplus
}
#endif

#endif /* _das_dim_p_h_ */
