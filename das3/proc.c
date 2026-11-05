/* Copyright (C) 2015-2017 Chris Piker <chris-piker@uiowa.edu>
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

#define _POSIX_C_SOURCE 200112L

#include <string.h>

#include "proc.h"

/* The defaults for NULL callbacks are applied at dispatch time in io.c, not
   installed here, so a structure built with "= {DAS_STREAM_PROC_VER}" behaves
   the same as one from das_stream_proc_init(). */

DasErrCode das_stream_proc_init_ver(
	das_stream_proc* pThis, void* pUser, size_t uVersion
){
	/* A later library adds cases here for each older layout it can still
	   serve, clearing only that layout's size */
	if(uVersion != DAS_STREAM_PROC_V3_0)
		return das_error(DASERR_IO,
			"Unknown das_stream_proc version 0x%04zx, this library knows 0x%04x",
			uVersion, DAS_STREAM_PROC_VER
		);

	memset(pThis, 0, sizeof(das_stream_proc));
	pThis->uVersion = uVersion;
	pThis->pUser = pUser;
	return DAS_OKAY;
}
