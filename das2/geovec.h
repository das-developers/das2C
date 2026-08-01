/* Copyright (C) 2024  Chris Piker <chris-piker@uiowa.edu>
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
 * version 2.1 along with libdas2; if not, see <http://www.gnu.org/licenses/>.
 */

/** @file geovec.h The packed vector datum payload.  TRANSITIONAL.
 *
 * THIS WHOLE FILE IS SCAFFOLDING and retires with vtGeoVec.  Nothing new
 * should include it.
 *
 * The component-system vocabulary that used to live here -- DAS_VSYS_*,
 * VEC_DIRS*, das_compsys_* -- has MOVED to the formalisms that own it:
 * form_vector.h holds the four systems a free vector may use, form_geoloc.h
 * adds the two ellipsoidal ones and the das_geosys_* superset lookups.  Do not
 * re-add them here; two definitions of one vocabulary is how the two halves
 * drift apart.
 *
 * What remains is the packed das_geovec struct itself, which is a COPY of the
 * components inline in a datum.  Its replacement is the vtComposite box: a
 * VIEW of the run plus a DasForm* that says what the run means.  The three
 * files still reaching for this struct (value.c, datum.c, das3_spice.c) are
 * the excision list.
 */

#ifndef _vector_h_
#define _vector_h_

#include <das2/value.h>

#ifdef __cplusplus
extern "C" {
#endif

/** A packed geometric vector, small enough to sit inside a das_datum.
 *
 * Longer algebraic vectors would normally be handled as single runs of a
 * scalar index and wouldn't correspond to a das_datum compatible item.
 */
typedef struct das_geovec_t{

   /* The vector values if local */
   double comp[3];

   /* The ID of the vector frame, or 0 if unknown */
   ubyte   frame;

   /* The system type. */
   ubyte systype;

   /* The surface ID if the coordinate system uses a non-standard
      surface */
   ubyte surfid;

   /* the element value type, taken from das_val_type */
   ubyte   et;

   /* the size of each element, in bytes, copied in from das_vt_size */
   ubyte   esize;

   /* Number of valid components */
   ubyte   ncomp;

   /* Direction for each component, 2 bits for each */
   ubyte   dirs;

   /* Unused, here for allignement */
   ubyte _spare;

} das_geovec;

/** Initialize a packed vector from a run of cells.
 * @memberof das_geovec */
DAS_API DasErrCode das_geovec_init(
   das_geovec* pVec, const ubyte* pData, ubyte uFrameId, ubyte uSysType,
   ubyte uSurfaceId, ubyte et, ubyte esize, ubyte ncomp, ubyte dirs
);

/** The element type of one component.  @memberof das_geovec */
#define das_geovec_eltype(p) ((p)->et & 0x0F)

/** A byte pointer to component i; cast to das_geovec_eltype() to read it.
 * @memberof das_geovec */
#define das_geovec_comp(p,i) ( ((ubyte*)(p)->comp) + (size_t)(i)*(p)->esize )

/** Read the components out as doubles, in das_geovec::dirs order.
 * @memberof das_geovec */
DAS_API DasErrCode das_geovec_values(das_geovec* pVec, double* pValues);

#define das_geovec_sys(P) ((P)->systype)
#define das_geovec_hasRefSurf(P) ((P)->surfid != 0)
#define das_geovec_surfId(P) ((P)->surfid)
#define das_geovec_numComp(P) ((P)->ncomp)

/** The canonical direction of storage slot i.  @memberof das_geovec */
DAS_API int das_geovec_dir(const das_geovec* pThis, int i);

int das_geovec_dirs(const das_geovec* pThis, ubyte* pDirs);

/** The display symbol for storage slot iIndex.  @memberof das_geovec */
DAS_API const char* das_geovec_compSym(const das_geovec* pThis, int iIndex);

#ifdef __cplusplus
}
#endif

#endif /* _vector_h_ */
