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

/** @file spice.h - Direct SPICE output away from stdout
 *
 * Since das1, readers have been required to send only valid das stream bytes
 * to stdout. Errors and other status information go to stderr. This has
 * made linking against SPICE somewhat problematic as SPICE outputs it's 
 * status and error messages to stdout by default.
 * 
 * This code contains a recommended way to redirect SPICE errors to das
 * error packets. Das readers that communicate with spice should include
 * this header, or redirect SPICE status messages on thier own in a similar
 * manner.  To send a SPICE error as a stream exception:
 * @code
 *   if(failed_c())
 *       DasIO_exception_v(pOut, DAS_EX_SERVER_ERR, "%s", das_get_spice_error());
 * @endcode
 * 
 * This header has no library code behind it, the functions are all defined
 * here so than libdas3.so/dll isn't shipped with undefined symbols.  A 
 * program that includes this header must also include the SpiceUsr.h. and
 * must link against SPICE itself.
 */

#ifndef _das2_spice_H_
#define _das2_spice_H_

#include <stdio.h>
#include <string.h>

#include <SpiceUsr.h>

#include <das3/util.h>  /* das_error */

/** @addtogroup spice
 * @{
 */

/* Read the current SPICE error into sBuf as one line: the short message, a
   space and the long message.  The short message is at most 25 characters
   and the long one at most 1840, so 2048 bytes is plenty.  Gives an empty
   string if there is no current error. */
static inline void _das_spice_getmsg(char* sBuf, int nLen)
{
	int n;

	sBuf[0] = '\0';
	getmsg_c("SHORT", (SpiceInt)nLen, sBuf);
	n = (int)strlen(sBuf);
	while((n > 0)&&(sBuf[n-1] == ' ')) sBuf[--n] = '\0';

	if(n > 0){
		if(n + 2 >= nLen) return;
		sBuf[n++] = ' ';
		sBuf[n] = '\0';
	}

	getmsg_c("LONG", (SpiceInt)(nLen - n), sBuf + n);
	n += (int)strlen(sBuf + n);
	while((n > 0)&&(sBuf[n-1] == ' ')) sBuf[--n] = '\0';
}

/** Setup spice so that errors are not automatically output to the standard
 *  output channel.
 *
 * Das2 readers (and unix programs in general) are only supposed to output
 * data to standard out, not error messages.
 */
static inline void das_spice_err_setup(void)
{
	errprt_c("SET", 4, "ALL");    /* Want all error text */
	erract_c("SET", 7, "RETURN"); /* Just return on error, we'll check for it */
	errdev_c("SET", 4, "NULL");   /* Don't go printing to the screen on error */
}

/** Get a spice error as string.
 *
 * @Warning This function is not MT safe, but then again SPICE isn't
 * MT safe, so not a big deal.
 *
 * @returns A pointer to a static buffer holding the error text, an empty
 *  string if there is no current spice error.  Each source file that calls
 *  this has its own buffer.
 */
static inline const char* das_get_spice_error(void)
{
	static char sBuf[2048];
	_das_spice_getmsg(sBuf, (int)sizeof(sBuf));
	return sBuf;
}

/** Print a spice error to standard error
 *
 * @param sProgName The program name to put in the message, may be NULL
 *
 * @returns DASERR_SPICE, you can return this to the calling shell if you
 *      like, or call reset_c() to process different spice input
 */
static inline int das_print_spice_error(const char* sProgName)
{
	char sMsg[2048];
	_das_spice_getmsg(sMsg, (int)sizeof(sMsg));

	if(sProgName) fprintf(stderr, "ERROR (%s): %s\n", sProgName, sMsg);
	else fprintf(stderr, "ERROR: %s\n", sMsg);

	return DASERR_SPICE;
}

/** @} */

#endif /* _das2_spice_H_ */
