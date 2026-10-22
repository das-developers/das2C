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

/** @file send.h - Das Reader output formatting helpers
 *
 * The core library, libdas3, supplies a full API for generating das
 * streams, however any particular das stream is easy to write. Just 
 * print to standard output and check it in a verifier.  These functions
 * are provided for readers that mostly want to format thier own streams
 * and just need assistance with peripheral affairs such as sending error
 * messages.
 *
 * Note when writing your own output formatter, put stdout in to binary
 * mode first via das_stdio_binary() before sending packets.  This is 
 * critical on Windows. Also, call flush after each packet (slow readers)
 * or group of packets (fast readers).
 */

#ifndef _das2_outbuf_H_
#define _das2_outbuf_H_

#include <stdarg.h>
#include <stdlib.h>

#include <das3/das1.h>  /* das_swap_float, and defs.h for HOST_IS_LSB_FIRST */
#include <das3/oob.h>   /* das_except_t */

/** Send a stub stream header. 
 *
 * All das2 streams must start with a stream header.  This function sends a
 * minimal stream header which is just good enough to be the prefix for an
 * error message.  Only call this if your program needs to output an error
 * message before it's sent it's own stream header.
 */
DAS_API void das_send_stub(int nDasVer);

/** Output an exception of any type
 *
 * The other exception writers below are built on this one.  Unlike them it
 * only writes the exception, nothing is logged to standard error.
 *
 * @param nDasVer The stream major version, 2 or 3.  das1 streams have no
 *        in-band exceptions, so for any other version nothing is written.
 *
 * @param nType The exception type, its name in the stream depends on
 *        nDasVer, see das_except_toStr()
 *
 * @param sMsg The message, a plain string not a format.  It's XML escaped
 *        here, so pass it as is.
 */
DAS_API void das_send_except(int nDasVer, das_except_t nType, const char* sMsg);

/** Output a no data in interval message
 * @returns the integer 0
 */
DAS_API int das_send_nodata(int nDasVer, const char* sFmt, ...);

/** Output a "user-messed up" message when receiving a badly formed query 
 * @returns the integer 47
 */
DAS_API int das_send_queryerr(int nDasVer, const char* sFmt, ...);

DAS_API int das_vsend_queryerr(int nDasVer, const char* sFmt, va_list argp);

/** Output a server problem message (i.e. missing spice kernel, etc) 
 * @returns the integer 48
 */
DAS_API int das_send_srverr(int nDasVer, const char* sFmt, ...);

/** Output a log status message (i.e. reading file T120101.DAT) */
DAS_API void das_send_msg(int nDasVer, const char* source, const char* sFmt, ...);

/** Start a progress bar on the recieving client 
 *
 * @warning Not thread safe, only use from your writing thread
 *
 * This function keeps track of the last progress update and will
 * not issue a new one unless at least 1% more progress has been made
 * 
 * @note: most das clients don't use this method for tracking progress
 * the data packets themselves usually contain time values which can
 * be compared to the query range.  The value of the current packet 
 * thus provides a natural progress bar.  Kept for old statically linked
 * programs that need this capability.
 *
 * @param nDasVer the das stream version number you're writing, either 1 or 2
 * @param sSrc The name of the thing setting the progress bar
 * @param fBeg some begin mark, can be any floating point number.
 * @param fEnd some end mark, can be any floating point number bigger than fBeg
 */
void das_send_progbeg(int nDasVer, const char* sSrc, double fBeg, double fEnd);

/** Update a progress bar on the receiving client 
 * 
 * @warning Not thread safe, only use from your writing thread
 * 
 * @note: you probably don't need to call this function, see the note in
 * das_send_progbeg()
 *
 * @param nDasVer the das stream version number you're writing, either 1 or 2
 * @param sSrc The name of the thing setting the progress bar
 * @param fCurrent some floating point number between the start and end marks
 *        set in das_send_progbeg().
 */
void das_send_progup(int nDasVer,  const char* sSrc, double fCurrent);

/** Big endian float on any host, a no-op on big endian machines.
 * This is useful for Das1 readers that must output all data in big endian
 * format.  Same as swapFloatIfHostLE() in das1.h. */
#define das_msb_float(x) swapFloatIfHostLE(x)
		
#endif /* _das2_outbuf_H_ */
