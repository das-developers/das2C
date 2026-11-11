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

/* INTERNAL header, not installed.  The bodies of DasExcept and DasComment
 * and the parts of oob.h only the library calls.  DasIO embeds one of each
 * as scratch space for decoding and reads the members directly. */

#ifndef _das_oob_p_h_
#define _das_oob_p_h_

#include <das3/oob.h>
#include <das3/buffer.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum das_oob_type {OOB_EXCEPT, OOB_COMMENT} das_oob_type_e;

/* Common head of both kinds, so OutOfBand_decode() can take one list of
   candidates and report which one it filled. */
typedef struct out_of_band {
	das_oob_type_e pkttype;
} OutOfBand;

struct das_except {
	OutOfBand base;
	das_except_e nType;
	char* sMsg;         /* never NULL after init, "" when empty */
	size_t uMsgLen;     /* allocated size of sMsg */
};

struct das_comment {
	OutOfBand base;
	char* sType;   size_t uTypeLen;   /* each never NULL after init, */
	char* sSrc;    size_t uSrcLen;    /* "" when unset; u*Len is the */
	char* sVal;    size_t uValLen;    /* allocated size              */
};


/* Caller or container storage, the scratch objects inside DasIO.  init_
   allocates the strings, fini_ frees them; new_ and del_ wrap these. */
void init_DasExcept(DasExcept* pThis);
void fini_DasExcept(DasExcept* pThis);

void init_DasComment(DasComment* pThis);
void fini_DasComment(DasComment* pThis);

/** Parse text data into a stream exception */
das_errcode OobExcept_decode(DasExcept* pThis, DasBuf* pBuf);

/** Serialize an exception in das2.2 format.
 * Writes: @c \<exception type="T" message="M" /\> */
das_errcode OobExcept_encode(const DasExcept* pThis, DasBuf* pBuf);

/** Serialize an exception in das3.0 format.
 * Writes: @c \<exception type="T"\>M\</exception\> */
das_errcode OobExcept_encode3(const DasExcept* pThis, DasBuf* pBuf);

/** Parse text data into a stream comment */
das_errcode OobComment_decode(DasComment* pThis, DasBuf* pBuf);

/** Serialize a comment in das2.2 format.
 * Writes: @c \<comment type="T" source="S" value="V" /\>
 * Quote and markup characters in type and source are written as '_'. */
das_errcode OobComment_encode(const DasComment* pThis, DasBuf* pBuf);

/** Serialize a comment in das3.0 format.
 * Writes: @c \<comment type="T" source="S"\>V\</comment\>
 * The source attribute is omitted when empty.  Quote and markup characters
 * in type and source are written as '_'. */
das_errcode OobComment_encode3(const DasComment* pThis, DasBuf* pBuf);

/** Decode whichever kind of out of band item is in a buffer
 *
 * Out of band items may be frequent, so the caller supplies the objects to
 * fill, reused from item to item; strings only grow.
 *
 * @param[in] pBuf    A readable buffer holding one out of band item
 * @param[in] ppObjs  A NULL terminated list of initialized objects to fill
 * @param[out] which  Index in ppObjs of the object filled, or -1 if the
 *                    element is of a kind not in the list.  Such an element
 *                    is skipped unparsed, so it is not checked.
 * @returns DAS_OKAY, or a positive error code for a malformed item
 */
das_errcode OutOfBand_decode(DasBuf* pBuf, OutOfBand** ppObjs, int* which);

#ifdef __cplusplus
}
#endif

#endif /* _das_oob_p_h_ */
