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
#define DASFRM_CNAME_SZ 12
#define DASFRM_BODY_SZ  64  /* Direction name size */
#define DASFRM_INERTIAL 0x00000010
#define DASFRM_NULLNAME ""

/** @addtogroup DM 
 * @{
 */

/** Stores the definitions of a coordinate frame
 * 
 * These are little more then a basic definition to allow new das3 vector
 * objects to be manipulated in a somewhat reasonable manner.  Two vectors that 
 * have the same frame can be subject to cross-products and other useful
 * manipulations.  If the do not share a coordinate system then some out-of-band
 * transformation will be needed.
 */
typedef struct frame_descriptor{

	/** The base class 
    * A common property to store is the suffexes for the principle coordinate
    * axes,  For eample in the East, North, Up system these would be "E","N","U" 
    */
	DasDesc base;

	/* Required properties */
   ubyte id;  /* The frame ID, used in vectors, quaternions etc. */
              /* WARNING: If this is changed to something bigger, like a ushort,
                          go remove the double loop from DasStream_getFrameId! */

	char name[DASFRM_NAME_SZ];
   char body[DASFRM_NAME_SZ];

   int32_t bodyId;  /* A place to store the spice body ID after lookup, 0 = unset */
   uint32_t flags;  /* Usually contains the type */

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
 * @param pParent
 * 
 * @param sBody the name of the central body used to define the reference frame
 *              typically this is a string understood by SPICE.
 * 
 * @param id The internal stream ID used to tag geovectors (das_geovec) in this
 *        frame.  Has no external meeting. Must be in the range 1 to 255
 *        inclusive.
 * 
 * @param sName The name of the frame.  Stream creators are encouraged to
 *        use external name systems for this, such as SPICE.
 * 
 * @param sBody The name of the defining body for the frame.  Common external
 *        names are "IAU_EARTH", or a spacecraft frame name such as "JUNO".
 * 
 * @memberof DasFrame
 */
DAS_API DasFrame* new_DasFrame(
   DasDesc* pParent, ubyte id, const char* sName, const char* sBody
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
DAS_API DasErrCode DasFrame_setName(DasFrame* pThis, const char* sName);

/** Change the frame central body name
 * @memberof DasFrame
 */
DAS_API DasErrCode DasFrame_setBody(DasFrame* pThis, const char* sBody);

/** Get the internal (stream only) ID of a frame
 * 
 * @memberof DasFrame
 */
#define DasFrame_id(p) ((p)->id)


DAS_API void DasFrame_inertial(DasFrame* pThis, bool bInertial);

#define DasFrame_isInertial(P) (P->flags & DASFRM_INERTIAL)

/** Get the frame name
 * @memberof DasFrame
 */
#define DasFrame_getName(P) (P->name)

/** Get the central body for the frame
 * @memberof DasFrame
 */
#define DasFrame_getBody(P) ((const char*)(P->body))

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
