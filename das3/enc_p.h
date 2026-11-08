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

/* INTERNAL header, not installed.  The das2 encoding body, for enc.c and the
 * das2 packet and plane code that sizes records from it.  Applications use
 * DasEnc_cat() and DasEnc_width(). */

#ifndef _das_enc_p_h_
#define _das_enc_p_h_

#include <das3/enc.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Widest text field, ascii or time, a das2 encoding may declare.  Formatting
   buffers are sized DASENC_MAX_WIDTH + 1, daslimit_get() reports it. */
#define DASENC_MAX_WIDTH 127

struct das_encoding {
	/* The basic encoding category. 
	 * This may be one of:
	 *  - @b DAS2DT_BE_REAL Most Significant byte First (big-endian) IEEE-754 reals
	 *  - @b DAS2DT_LE_REAL Most Significant byte First (little-endian) IEEE-754 reals
	 *  - @b DAS2DT_ASCII A real number formatted in some number of significant digits
	 *  - @b DAS2DT_TIME A date-time formatted as an ASCII ISO-8601 string.
	 * Additionally the following is defined for code that just wants to work
	 * in the host byte order, which ever it happens to be:
	 *  - @b DAS2DT_HOST_REAL
	 */
	unsigned int nCat;
	
	/* The width in bytes of the encoded values. 
	 * The largest width for an encoding in Das2 is 127 bytes. Not all encoding
	 * categories support all widths the REAL types only allow for 4 or 8 bytes
	 * widths.
	 */
	unsigned int nWidth;
	
	/* The sprintf format string.
	 * This is the format converting doubles and times to ASCII strings. 
	 * Non-ASCII encodings do not make use of this string.  In that case
	 * it is set by new_DasEnc() and new_DasEnc_str() to an empty string
	 */
	char sFmt[DASENC_FMT_LEN];
	
	/* The type value for this encoding on a Das2 Stream. 
	 * For ASCII types the type value width is assumed to be one larger than 
	 * the actual number of bytes in the formatted output to allow for a field
	 * separator of some type.
	 *  */
	char sType[DASENC_TYPE_LEN];
	
};


/* Moved from enc.h: library plumbing, not API */

/** Check for equality between two encodings
 * 
 * @param pOne The first encoding
 * @param pTwo The second encoding
 * @return true if the encodings have the same category, width, and format
 *         string.
 * @memberof DasEnc
 */
bool DasEnc_equals(const DasEnc* pOne, const DasEnc* pTwo);

/** Get a hash value suitable for use in switch statements.
 *
 * Combines DasEnc_width() and DasEnc_cat() into a single
 * integer that represents the encoding.   
 *
 * Since there are only 4 binary value output encodings the following
 * constants are defined for the binary encoding hash values.
 *
 *  - @b DAS2DT_BE_REAL_4 - Big-Endian 4-byte IEEE-754 reals
 *  - @b DAS2DT_LE_REAL_4 - Little-Endian 4-byte IEEE-754 reals
 *  - @b DAS2DT_BE_REAL_8 - Big-Endian 4-byte IEEE-754 reals
 *  - @b DAS2DT_LE_REAL_8 - Little-Endian 4-byte IEEE-754 reals
 *
 * To help with byte-swapping logic the following are also defined
 *
 *  - @b DAS2DT_DOUBLE - The 8-byte real hash above that corresponds to
 *       either DAS2DT_BE_REAL_8 or DAS2DT_LE_REAL_8 depending on the 
 *       host endian orber.
 *
 *  - @b DAS2DT_FLOAT - The 4-byte real hash above that corresponds to 
 *       either DAS2DT_BE_REAL_4 or DAS2DT_LE_REAL_4 depending on the the
 *       host endian order.
 *
 * @param pThis the encoding to hash
 *
 * @returns The encoding category in the lower byte and the width in 
 *          the next to lowest byte.
 *
 * @memberof DasEnc
 */
unsigned int DasEnc_hash(const DasEnc* pThis);

#ifdef __cplusplus
}
#endif

#endif /* _das_enc_p_h_ */
