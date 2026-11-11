/* Copyright (C) 2015-2026 Chris Piker <chris-piker@uiowa.edu>
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


/** @file oob.h
 * Exceptions and comments, the "out of band" items of a das stream.
 *
 * Besides headers and data, a stream may carry two kinds of messages for
 * whoever reads it.  An exception ends the stream and says why: nothing
 * matched the query, the query was bad, or the server failed.  It is always
 * the last packet in a stream.  A comment carries anything else: log lines,
 * task progress, notes from the program that wrote the stream.  A problem
 * that does not end the stream is a comment, "log:error" for example.
 *
 * @par Reading
 *
 * A program reading a stream gets comments through the onComment callback
 * of its das_stream_proc, and the exception, if any, through its onClose,
 * the last call it gets either way.  The object handed over belongs to the
 * DasIO, so it and every string read from it are good only until the
 * callback returns.  Copy what you keep.
 *
 * @code
 *   DasErrCode onComment(DasStream* pSd, const DasComment* pCmt, void* pUser)
 *   {
 *       if(strcmp(DasComment_get(pCmt, "type"), "taskProgress") == 0)
 *           updateBar(atoi(DasComment_get(pCmt, "value")));
 *       return DAS_OKAY;
 *   }
 * @endcode
 *
 * A filter that passes them along hands the same pointer to DasIO_comment()
 * or DasIO_exception() on its output; see das_close_f for the order a
 * filter does things in when the stream ends.
 *
 * @par Writing
 *
 * DasIO_comment_v() and DasIO_exception_v() send one from a printf style
 * format without making an object.  To build one by hand:
 *
 * @code
 *   DasComment* pCmt = new_DasComment();
 *   DasComment_set(pCmt, "type",   "log:info");
 *   DasComment_set(pCmt, "source", "my_reader");
 *   DasComment_set(pCmt, "value",  "Skipping a corrupt record");
 *   DasIO_comment(pOut, pCmt);
 *   del_DasComment(pCmt);
 * @endcode
 *
 * DasIO_comment() and DasIO_exception() serialize the object before they
 * return and keep no reference to it, so it may be changed and sent again,
 * or deleted straight away.  DasIO_exception() also closes the output.
 */

#ifndef _das_out_of_band_h_
#define _das_out_of_band_h_

#include <das3/util.h>

#ifdef __cplusplus
extern "C" {
#endif

/** @addtogroup IO
 * @{
 */

/** The kinds of exception a stream can carry */
typedef enum das_except_type {
	DAS_EX_UNKNOWN,     /**< A type name this library does not know */
	DAS_EX_NO_DATA,     /**< Nothing matched; not an error */
	DAS_EX_SERVER_ERR,  /**< The program making the stream failed */
	DAS_EX_QUERY_ERR    /**< The request could not be understood */
} das_except_e;

/** The stream name for an exception type
 *
 * Two of the names changed between stream versions:
 *
 *   | das_except_e      | das2.2           | das3.0         |
 *   |-------------------|------------------|----------------|
 *   | DAS_EX_UNKNOWN    | Unknown          | Unknown        |
 *   | DAS_EX_NO_DATA    | NoDataInInterval | NoMatchingData |
 *   | DAS_EX_SERVER_ERR | ServerError      | ServerError    |
 *   | DAS_EX_QUERY_ERR  | IllegalArgument  | QueryError     |
 *
 * @param nType The exception type
 * @param nDasVer The stream major version, 2 or 3.  das1 streams have no
 *        exceptions.
 * @returns A static string, or NULL for an unknown type or version
 */
DAS_API const char* das_except_toStr(das_except_e nType, int nDasVer);

/** The exception type for a stream name, from either stream version
 *
 * The two vocabularies never use one name for two different types, so
 * this needs no version.  Streams may carry types outside the table, so an
 * unrecognized name is not an error.
 *
 * @returns The type, or DAS_EX_UNKNOWN for NULL or an unrecognized name
 */
DAS_API das_except_e das_except_fromStr(const char* sType);


/* ************************************************************************* */
/* Exceptions */

/** @class DasExcept oob.h <das3/oob.h>
 *
 * An exception on a stream: a type and a message for a person to read.
 *
 * Exceptions are fixed by the stream formats, a type and a message and
 * nothing else, so they are made whole and only read afterwards.
 */
typedef struct das_except DasExcept;

/** Make an exception to send
 *
 * @param nType One of DAS_EX_NO_DATA, DAS_EX_SERVER_ERR or DAS_EX_QUERY_ERR
 * @param sMsg  The message, copied.  NULL is taken as "".
 * @returns A new exception, or NULL if memory ran out
 * @memberof DasExcept
 */
DAS_API DasExcept* new_DasExcept(das_except_e nType, const char* sMsg);

/** The exception type
 * @memberof DasExcept
 */
DAS_API das_except_e DasExcept_type(const DasExcept* pThis);

/** The das3.0 stream name of the exception type, e.g. "ServerError"
 *
 * A static string.  For the das2.2 name use DasExcept_typeStr2().
 * @memberof DasExcept
 */
DAS_API const char* DasExcept_typeStr(const DasExcept* pThis);

/** The das2.2 stream name of the exception type, e.g. "NoDataInInterval"
 *
 * A static string, never NULL: a type outside the table reads "Unknown",
 * as DasExcept_typeStr() does.
 * @memberof DasExcept
 */
DAS_API const char* DasExcept_typeStr2(const DasExcept* pThis);

/** The exception message, never NULL
 *
 * Good for as long as the exception is; for one handed to an
 * onClose callback, until the callback returns.
 * @memberof DasExcept
 */
DAS_API const char* DasExcept_msg(const DasExcept* pThis);

/** Free an exception made with new_DasExcept()
 *
 * Never call this on one handed to an onClose callback; the DasIO owns it.
 * @memberof DasExcept
 */
DAS_API void del_DasExcept(DasExcept* pThis);


/* ************************************************************************* */
/* Comments */

/** @class DasComment oob.h <das3/oob.h>
 *
 * A comment on a stream, a small set of named text values.
 *
 * The values are read and written by name with DasComment_get() and
 * DasComment_set().  These names are known:
 *
 *   | name   | meaning                             | das2.2    | das3.0    |
 *   |--------|-------------------------------------|-----------|-----------|
 *   | type   | what kind, any token; readers know  | attribute | attribute |
 *   |        | log:info, log:warning, log:error,   |           |           |
 *   |        | taskSize, taskProgress              |           |           |
 *   | source | the program that wrote it           | attribute | attribute |
 *   | value  | the comment itself                  | attribute | body text |
 *
 * Currently other names are refused. A value not set reads as "".
 */
typedef struct das_comment DasComment;

/** Make an empty comment to send
 *
 * @returns A new comment with every value "", or NULL if memory ran out
 * @memberof DasComment
 */
DAS_API DasComment* new_DasComment(void);

/** Set one value of a comment
 *
 * @param pThis The comment
 * @param sName The name of the value to set, one of:
 *          - "type"    what kind of comment, any token.  Readers know
 *                      "log:" plus a level ("log:info", "log:error", ...)
 *                      and "taskSize", "taskProgress".
 *          - "source"  the program that wrote it
 *          - "value"   the comment itself
 * @param sVal  The value, copied.  NULL is taken as "".
 * @returns DAS_OKAY, or a positive error code for an unknown name
 * @memberof DasComment
 */
DAS_API DasErrCode DasComment_set(
	DasComment* pThis, const char* sName, const char* sVal
);

/** Get one value of a comment
 *
 * The string is good until that value is set again or the comment is freed;
 * for one handed to an onComment callback, until the callback returns.
 *
 * @param pThis The comment
 * @param sName One of the names in the DasComment table
 * @returns The value, "" if never set, or NULL for an unknown name
 * @memberof DasComment
 */
DAS_API const char* DasComment_get(const DasComment* pThis, const char* sName);

/** Free a comment made with new_DasComment()
 *
 * Never call this on one handed to an onComment callback; the DasIO owns it.
 * @memberof DasComment
 */
DAS_API void del_DasComment(DasComment* pThis);

/** @} */

#ifdef __cplusplus
}
#endif

#endif /* _das_out_of_band_h_ */
