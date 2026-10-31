/* Copyright (C) 2026 Chris Piker <chris-piker@uiowa.edu>
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

/* Helpers shared by the formalism modules, form.c defines them.  Not
 * installed.
 */

#ifndef _das_form_priv_h_
#define _das_form_priv_h_

#include "defs.h"

struct das_buffer;

/* Shared by the formalisms that carry a component order.  Emits sysorder=
   when it says something the default does not: a non-ascending order,
   printed to the component count recorded when validate() was run.

   Use sysorder= in XML headers when the order is not ascending.  pOrder is
   uComps entries long slot to canonical component, so this works for a nine
   element rotation as readily as a three element vector. */
DasErrCode _dasform_prnOrder(
	struct das_buffer* pBuf, const ubyte* pOrder, ubyte uComps
);

/* The wire spelling of a component order, "1;0;2", for the forms that keep
   one beside the decoded order so getParam() stays a read.  Always all
   uComps slots, ascending or not: getParam reports defaults too. */
void _dasform_fmtOrder(
	char* sBuf, size_t uLen, const ubyte* pOrder, ubyte uComps
);

#endif /* _das_form_priv_h_ */
