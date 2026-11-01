/* Copyright (C) 2015-2026 Chris Piker <chris-piker@uiowa.edu>
 *               2004-2006 Jeremy Faden <jeremy-faden@uiowa.edu>
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

/** @file desc_p.h DasDesc internals, not installed.
 *
 * Every derived descriptor (DasStream, PktDesc, PlaneDesc, DasDs, DasDim,
 * DasVar) embeds a DasDesc as its first member, so their private headers
 * include this one.
 */

#ifndef _das_desc_p_h_
#define _das_desc_p_h_

#include <das3/desc.h>
#include <das3/array_p.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Stamped into every descriptor by its constructor.  "DDSC" in ASCII, so it
   is unlikely to occur by accident and reads plainly in a hex dump. */
#define DASDESC_MAGIC 0x44445343u

struct das_descriptor {
	desc_type_t type;

	/* DASDESC_MAGIC, set by DasDesc_init() and new_DasDesc().  del_DasDesc()
	   checks it because callers reach it through a pointer cast the compiler
	   can't check.  Never cleared: DasVar is reference counted, so a
	   del_DasDesc() call is not always the object's end.  Sits in what would
	   otherwise be padding before pfDel on 64-bit hosts. */
	uint32_t uMagic;

	/* The derived type's destructor, called by del_DasDesc().  Set once by
	   the constructor through DasDesc_init().  NULL for descriptors that live
	   in caller storage (parse contexts, test stack objects), which are torn
	   down with DasDesc_freeProps() instead.  Kept near the front so the guard
	   in del_DasDesc() only reads the first few bytes of a pointer it can't
	   trust yet. */
	void (*pfDel)(DasDesc* pThis);

	/* Properties are held in a das array so that they are in a continuous
	   block of memory.  Properties are laid out in memory as so:

	   valid_code\0name\0type_code\0value\0

	   this means that the array is RANK_2(0,4,*) and it will need 2 ancillary
	   arrays of pointers to keep track of the boundaries.  Thus the number of
	   independent allocations drops from:

	      properties * N  so O(N)
	   to:
	      3               so O(1)

	   and there is no upper limit to the number of properties (yay!)
	*/
	DasAry properties;

	/* Number of invalid properties (saved to make length cals faster) */
	size_t uInvalid;

	struct das_descriptor* parent;
	bool bLooseParsing;

	/* This descriptor does NOT inherit properties: DasDesc_getProp stops
	   its parent walk here even when parent is set. */
	bool bNoInherit;

	/* Application data, see DasDesc_setUser().  The library never reads,
	   copies or frees it: DasDesc_copyIn() is a property merge, and the
	   derived *_copy() functions leave a new object's pointer NULL. */
	void* pUser;
};

/* Initialize caller storage (a derived type's base member, or a stack object
 * in the tests) as a descriptor.  The whole DasDesc is zeroed first.  Call
 * DasDesc_freeProps() before re-initializing one that holds properties.
 *
 * pfDel is the derived type's destructor for del_DasDesc(), or NULL when the
 * descriptor is not a heap object of its own. */
void DasDesc_init(DasDesc* pThis, desc_type_t type, void (*pfDel)(DasDesc*));

/* For use in derived destructors, frees the property array.  After calling
 * this function, no new properties can be added. */
void DasDesc_freeProps(DasDesc* pThis);

#ifdef __cplusplus
}
#endif

#endif /* _das_desc_p_h_ */
