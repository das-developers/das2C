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

/* INTERNAL header, not installed.  The parts of value.h only the library
 * calls. */

#ifndef _das_value_p_h_
#define _das_value_p_h_

#include <das3/value.h>

#ifdef __cplusplus
extern "C" {
#endif


/* Moved from value.h: library plumbing, not API */

/** Get a storage value type given the common packet encodings
 * 
 * Storage types are values you can do calculations on.  For binary
 * encodings, these just represent the type minus any endian considerations
 * 
 * For text types that have an intended use, this returns a suitable binary
 * storage type.
 * 
 * @param sEncType The storage type one of:
 *           byte, ubyte, BEint, BEuint, BEreal, LEint, LEuint, LEreal
 *        or:
 *           utf8, none
 * 
 * @param nItemsBytes The number of bytes for each stored item
 * 
 * @param sInterp - Ignored unless the encoding is utf8, otherwise one of
 *        the values:
 *           bool, datetime, int, real, string
 * 
 * @memberof das_val_type
 */
das_val_type das_vt_store_type(
	const char* sEncType, int nItemBytes, const char* sInterp
);

/** Resolve a packet header encoding string to its canonical schema value.
 *
 * An encoding names how values are serialized in a packet, orthogonal to how they are
 * stored (das_val_type) or interpreted (semantic).  The vocabulary is CLOSED -- exactly
 * the DAS_ENC_* set, the schema's EncodingType pattern -- and not application-expandable.
 * This is the one place an encoding string is validated; output is a canonical DAS_ENC_*
 * pointer, so callers may compare the result by pointer.
 *
 * @param sEncType an encoding string from a stream header (may be NULL).
 * @returns the canonical DAS_ENC_* pointer, or NULL if sEncType is NULL or names no
 *          member of the closed set (the caller should fail loud).
 */
const char* das_enc_fromStr(const char* sEncType);

/** Resolve a semantic string to its canonical schema value.
 *
 * A variable's *semantic* is how its values are meant to be interpreted, 
 * orthogonal to how they are stored.  So a vtText field may values meant as
 * integers, or a vtLong of TT2000 times.  So storage answers "what bytes?"
 * but semantic answers "what do they mean, and what operations make sense?".
 *
 * This is the single place stream header spellings are normalized (liberal-in): 
 * "int" and 'integer' both maps to DAS_SEM_INT. Output is always a canonical
 * DAS_SEM_* singleton pointer defined above.
 *
 * @param sSemantic a semantic string from a stream header (may be NULL).
 * @returns the canonical DAS_SEM_* pointer, or NULL if sSemantic is NULL
 *   or names no member of the closed set.
 */
const char* das_sem_fromStr(const char* sSemantic);

#ifdef __cplusplus
}
#endif

#endif /* _das_value_p_h_ */
