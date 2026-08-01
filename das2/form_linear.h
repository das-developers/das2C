/* Copyright (C) 2026 Chris Piker <chris-piker@uiowa.edu>
 *
 * Author: C. Piker, via Claude Opus 5
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

/** @file form_linear.h Plain numbers, the formalism an absent <ops> implies.
 *
 * Linear sits at the BOTTOM of the formalism knowledge graph.  It knows about
 * nothing, so it includes no other form's header and implements no
 * binOpRight; every pairing between a plain number and something richer is
 * claimed by the richer side, in that side's file.  This header exists so
 * those files can recognize a linear partner and construct a linear result.
 *
 * A point minus a point is an interval, and an interval is linear -- so
 * form_point.c includes this.  A vector scaled by a number is a vector, so
 * form_vector.c and form_geoloc.c do too.  The arrow never comes back.
 */

#ifndef _das_form_linear_h_
#define _das_form_linear_h_

#include <das2/form.h>

#ifdef __cplusplus
extern "C" {
#endif

/** The linear vtable, exported for identity comparison.  @memberof DasForm */
DAS_API extern const DasForm_VTbl das_form_linear_vtbl;

/** Is this form plain numbers?  @memberof DasForm */
#define DasForm_isLinear(P) ((P)->pVTbl == &das_form_linear_vtbl)

/** Build a linear formalism.
 *
 * Stateless, but still heap and refcounted like every other form: a shared
 * static instance would need an is-static branch in refcounting, and a
 * sixteen byte object next to a DasDesc is not worth that.
 *
 * @returns a new form with one reference.  @memberof DasForm */
DAS_API DasForm* new_DasFormLinear(void);

#ifdef __cplusplus
}
#endif

#endif /* _das_form_linear_h_ */
