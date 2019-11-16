/* Copyright (C) 2015-2017 Chris Piker <chris-piker@uiowa.edu>
 *
 * This file is part of libdas2, the Core Das2 C Library.
 * 
 * Libdas2 is free software; you can redistribute it and/or modify it under
 * the terms of the GNU Lesser General Public License version 2.1 as published
 * by the Free Software Foundation.
 *
 * Libdas2 is distributed in the hope that it will be useful, but WITHOUT ANY
 * WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS
 * FOR A PARTICULAR PURPOSE.  See the GNU Lesser General Public License for
 * more details.
 *
 * You should have received a copy of the GNU Lesser General Public License
 * version 2.1 along with libdas2; if not, see <http://www.gnu.org/licenses/>. 
 */


/** @file send.h - Das Reader output formatting helpers
 *
 * The das core library libdas2.a supplies a full API for generating Das2 
 * streams, however these streams are relativily simple and don't really
 * require a heavy weight library for applications that only write a fixed
 * das2 stream type.  Most readers fall into this use case.  
 * 
 * These functions supply a few fixed functions to assist Das2 readers.
 */

#ifndef _das2_outbuf_H_
#define _das2_outbuf_H_

#include <stdlib.h>

/** Send a stub stream header. 
 *
 * All das2 streams must start with a stream header.  This function sends a
 * minimal stream header which is just good enough to be the prefix for an
 * error message.  Only call this if your program needs to output an error
 * message before it's sent it's own stream header.
 */
void das_send_stub(int nDasVer);

/** Output a no data in interval message 
 * @returns the integer 0
 */
int das_send_nodata(int nDasVer, const char* sFmt, ...);

/** Output a "user-messed up" message when receiving a badly formed query 
 * @returns the integer 0
 */
int das_send_queryerr(int nDasVer, const char* sFmt, ...);

/** Output a server problem message (i.e. missing spice kernel, etc) 
 * @returns the integer 48
 */
int das_send_srverr(int nDasVer, const char* sFmt, ...);

/** Output a log status message (i.e. reading file T120101.DAT) */
void das_send_msg(int nDasVer, const char* source, const char* sFmt, ...);


#ifdef HOST_IS_LSB_FIRST
/** Macro to invoke byte swaping only on little endian machines 
 * This is useful for Das1 readers that must output all data in big endian
 * format */
#define das_msb_float(x) _das_swap_float(x)
#else
#define das_msb_float(x) x
#endif

float _das_swap_float(float rIn);

void _das_escape_xml(char* sDest, size_t uOutLen, const char* sSrc);
		
#endif /* _das2_outbuf_H_ */
