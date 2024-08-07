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
#define DASFRM_DNAM_SZ  32  /* Direction name size */
#define DASFRM_TYPE_SZ  32
#define DASFRM_MAX_DIRS  4

#define DASFRM_TYPE_MASK      0x0000000F
#define DASFRM_UNKNOWN        0x00000000
#define DASFRM_CARTESIAN      0x00000001
#define DASFRM_POLAR          0x00000002
#define DASFRM_SPHERE_SURFACE 0x00000003
#define DASFRM_CYLINDRICAL    0x00000004
#define DASFRM_SPHERICAL      0x00000005 /* ISO spherical using colatitude 0 = north pole */
#define DASFRM_CENTRIC        0x00000006 /* Spherical, but with 90 = north pole */
#define DASFRM_DETIC          0x00000007 /* Ellipsoidal, same angles as centric */
#define DASFRM_GRAPHIC        0x00000008 /* Ellipsoidal, longitude reversed */

#define DASFRM_INERTIAL       0x00000010

#define DASFRM_NULLNAME       "_UNDEFINED_SOURCE_FRAME_"

/* Converting vecClass strings back and forth to frame type bytes */
const char* das_frametype2str( ubyte uFT);

ubyte das_str2frametype(const char* sFT);

/** @addtogroup DM 
 * @{
 */

/** Stores the definitions for a directional coordinate frame
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
   ubyte id;  /* The frame ID, used in vectors, quaternions etc. */
              /* WARNING: If this is changed to something bigger, like a ushort,
                          go remove the double loop from DasStream_getFrameId! */

	char name[DASFRM_NAME_SZ];
   char type[DASFRM_TYPE_SZ];
	uint32_t flags;  /* Usually contains the type */

   char dirs[DASFRM_MAX_DIRS][DASFRM_DNAM_SZ];
   uint32_t ndirs;

   /** User data pointer
    * 
    * The stream -> frame  hierarchy provides a goood organizational structure
    * for application data, especially applications that filter streams.  It
    * is initialized to NULL when a variable is created but otherwise the
    * library dosen't deal with it.
    */
   void* pUser;

} DasFrame;

/** @} */

/** Create a new empty frame definition 
 * @param sType A coordinate name type string, such as "cartesian"
 * @memberof DasFrame
 */
DAS_API DasFrame* new_DasFrame(
   DasDesc* pParent, ubyte id, const char* sName, const char* sType
);

/** Create a new empty frame definition, alternate interface
 * 
 * @param uType A coordinate ID, one of: DASFRM_CARTESIAN, DASFRM_POLAR,
 *        DASFRM_SPHERE_SURFACE, DASFRM_CYLINDRICAL, DASFRM_SPHERICAL,
 *        DASFRM_CENTRIC, DASFRM_DETIC, DASFRM_GRAPHIC
 * 
 * @memberof DasFrame
 */
DAS_API DasFrame* new_DasFrame2(
   DasDesc* pParent, ubyte id, const char* sName, ubyte uType
);

/** Create a deepcopy of a DasFrame descriptor and all it's properties */
DAS_API DasFrame* copy_DasFrame(const DasFrame* pThis);

/** Print a 1-line summary of a frame and then it's properties 
 * 
 * @memberof DasFrame
 */
DAS_API char* DasFrame_info(const DasFrame* pThis, char* sBuf, int nLen);

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
DAS_API ubyte DasFrame_getType(const DasFrame* pThis);


/** Add a direction to a frame definition 
 * 
 * @memberof DasFrame
 */
DAS_API DasErrCode DasFrame_addDir(DasFrame* pThis, const char* sDir);

/** Set default direction names and descriptions based on the frame type.
 * 
 * @return 0 if successful, non-zero if the frame type is not one of the
 *           builtin defaults.
 * 
 * @memberof DasFrame
 */
DAS_API DasErrCode DasFrame_setDefDirs(DasFrame* pThis);

/** Given the index of a frame direction, return it's name 
 *
 * @memberof DasFrame
 */
DAS_API const char* DasFrame_dirByIdx(const DasFrame* pThis, int iIndex);

/** Given the name of a frame direction, return it's index
 * 
 * @return A signed byte.  If the value is less then 0 then that direction is not defined
 * 
 * @memberof DasFrame
 */
DAS_API int8_t DasFrame_idxByDir(const DasFrame* pThis, const char* sDir);

/** Encode a frame definition into a buffer
 * 
 * @param pThis The vector frame to encode
 * @param pBuf A buffer object to receive the XML data
 * @param sIndent An indent level for the frame
 * @param nDasVer expects 3 or higher
 * @return 0 if the operation succeeded, a non-zero return code otherwise.
 * @memberof DasDesc
 */
DAS_API DasErrCode DasFrame_encode(
   const DasFrame* pThis, DasBuf* pBuf, const char* sIndent, int nDasVer
);


/** Free a frame definition that was allocated on the heap 
 * 
 * @memberof DasFrame
 */
DAS_API void del_DasFrame(DasFrame* pThis);

#ifdef __cplusplus
}
#endif

#endif /* _frame_h_ */
