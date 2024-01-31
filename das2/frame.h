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
 * version 2.1 along with Das2C; if not, see <http://www.gnu.org/licenses/>. 
 */

/** @file frame.h */

#ifndef _frame_h_
#define _frame_h_

#include <das2/descriptor.h>

#ifdef __cplusplus
extern "C" {
#endif

#define DASFRM_NAME_SZ  64
#define DASFRM_DNAM_SZ  32
#define DASFRM_TYPE_SZ  32
#define DASFRM_MAX_DIRS  4

#define DASFRM_TYPE_MASK      0x0000000F
#define DASFRM_UNKNOWN        0x00000000
#define DASFRM_CARTESIAN      0x00000001
#define DASFRM_POLAR          0x00000003
#define DASFRM_SPHERE_SURFACE 0x00000002
#define DASFRM_CYLINDRICAL    0x00000004
#define DASFRM_SPHERICAL      0x00000005

#define DASFRM_INERTIAL       0x00000010

/** Store the definitions for a directional coordinate frame
 * 
 * These are little more then a basic definition to allow new das3 vector
 * objects to be manipulated in a somewhat reasonable manner.  Two vectors that 
 * hare the same coordinate system can be subject to cross-products and other
 * useful manipulations.  If the do not share a coordinate system then some
 * out-of-band transformation will be needed.
 */
typedef struct frame_descriptor{

	/** The base class */
	DasDesc base;

	/* Required properties */
   byte id;  // The frame ID, used in vectors, quaternions etr
	char name[DASFRM_NAME_SZ];
   char type[DASFRM_TYPE_SZ];
	uint32_t flags;  /* Usually contains the type */

   char dirs[DASFRM_MAX_DIRS][DASFRM_DNAM_SZ];
   uint32_t ndirs;

} DasFrame;

/** @{ */

/** Create a new empty frame definition 
 * @param A coordinate name type string, such as "cartesian"
 * @memberof DasFrame
 */
DAS_API DasFrame* new_DasFrame(
   DasDesc* pParent, byte id, const char* sName, const char* sType
);

/** Change the frame name 
 * @memberof DasFrame
 */
DAS_API DasErrCode DasFrame_setName(
   DasFrame* pThis, const char* sName
);

#define DasFrame_id(p) ((p)->id)


DAS_API void DasFrame_inertial(DasFrame* pThis, bool bInertial);

#define DasFrame_isInertial(P) (P->flags & DASFRM_INERTIAL)

/** Get the frame name
 * @memberof DasFrame
 */
#define DasFrame_getName(P) (P->name)

/** Set the type of the frame as a string
 * This is almost always the constant string "cartesian"
 * @memberof DasFrame
 */
DAS_API DasErrCode DasFrame_setType(DasFrame* pThis, const char* sType);


/** Get the type of the frame as a string
 * This is almost always the constant string "cartesian"
 * @memberof DasFrame
 */
DAS_API byte DasFrame_getType(const DasFrame* pThis);


/** Add a direction to a frame definition 
 * 
 * @memberof DasFrame
 */
DAS_API DasErrCode DasFrame_addDir(DasFrame* pThis, const char* sDir);

/** Given the index of a frame direction, return it's name 
 *
 * @memberof DasFrame
 */
DAS_API const char* DasFrame_dirByIdx(const DasFrame* pThis, int iIndex);

/** Givin the name of a frame direction, return it's index
 * 
 * @return A signed byte.  If the value is less then 0 an error has occured.
 * 
 * @memberof DasFrame
 */
DAS_API int8_t DasFrame_idxByDir(const DasFrame* pThis, const char* sDir);

/** Free a frame definition that was allocated on the heap 
 * 
 * @memberof DasFrame
 */
DAS_API void del_DasFrame(DasFrame* pThis);

/** @} */

#ifdef __cplusplus
}
#endif

#endif /* _frame_h_ */