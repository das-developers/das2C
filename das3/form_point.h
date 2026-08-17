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

/** @file form_point.h Absolute positions on a scale with an agreed origin.
 *
 * The searchable name for these rules is an AFFINE SPACE: points and the
 * vectors (here "intervals") that translate between them.  Measurement theory
 * calls the same idea an INTERVAL SCALE.  das uses the word "interval" for the
 * difference quantity itself, so this library says affine for the rule set.
 *
 * The rules, all of them:
 *
 *     point - point     = interval    (linear, in the epoch's interval units)
 *     point + interval  = point
 *     interval + point  = point       (stated explicitly, never inferred)
 *     point - interval  = point
 *     point + point     = REFUSED     adding two calendar dates means nothing
 *     interval - point  = REFUSED
 *     point * anything  = REFUSED     scaling an origin-relative position is
 *                                     meaningless without moving the origin
 *
 * Calendar time is the canonical case -- a datetime is stored as plain ticks
 * with this formalism riding on top -- but nothing here is time specific.  Any
 * zero-referenced axis fits: a position measured from a chosen origin, a
 * pressure measured from a reference altitude.
 *
 * The wire form takes no parameters, so kind= alone carries it:
 *
 *   <scalar semantic="datetime" units="TT2000" index="*">
 *     <ops kind="point"/>
 *   </scalar>
 */

#ifndef _das_form_point_h_
#define _das_form_point_h_

#include <das3/form.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Exported so that its address can be compared.  Client code uses the
   DAS_FORM_POINT macro below and has no reason to name this directly. */
DAS_API extern const DasForm_VTbl das_form_point_vtbl;

/** A position on a scale measured from an agreed origin, calendar time being
 * the common case.  Two of these subtract to give an interval, but adding two
 * calendar dates means nothing and is refused.
 * @see DasForm_isKind(), DasVar_formIs().  @relates DasForm */
#define DAS_FORM_POINT (&das_form_point_vtbl)

/** Build a point formalism.  Stateless; the affine rule needs no parameters.
 * @returns a new form with one reference.  @memberof DasForm */
DAS_API DasForm* new_DasFormPoint(void);

#ifdef __cplusplus
}
#endif

#endif /* _das_form_point_h_ */
