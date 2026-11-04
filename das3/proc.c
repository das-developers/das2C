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

#include <stdio.h>

#include "proc.h"


/* ************************************************************************* */
/* Default Stream Object Handlers */

/* A normal end, or "no data", is fine.  Any other exception fails the read,
   so a program that never looked at exceptions still notices one. */
static DasErrCode def_stream_close_hndlr(
	DasStream* sd, const DasExcept* se, void* ud
){
	if((se == NULL)||(DasExcept_type(se) == DAS_EX_NO_DATA))
		return DAS_OKAY;

	fprintf(stderr, "StreamException encountered\n" );
	fprintf(stderr, "  type: %s\n", DasExcept_typeStr(se) );
	fprintf(stderr, "  message: %s\n", DasExcept_msg(se) );
	return 24;
}

static DasErrCode def_stream_cmt_hndlr(const DasComment* sc, void* ud) 
{
    /* do nothing */
	return 0;
}


/* ************************************************************************* */
/* Initialization */

void StreamHandler_init(StreamHandler* pThis, void* pUserData)
{
	pThis->userData = pUserData;
	pThis->streamDescHandler = NULL;
	pThis->pktDescHandler = NULL;
	pThis->pktDataHandler = NULL;
	pThis->pktRedefHandler = NULL;
	pThis->closeHandler = def_stream_close_hndlr;
    pThis->dsDescHandler = NULL;
    pThis->dsDataHandler = NULL;
    pThis->rawDataHandler = NULL;
   pThis->commentHandler = def_stream_cmt_hndlr;
}

StreamHandler* new_StreamHandler(void* pUserData)
{
	StreamHandler* pThis = (StreamHandler*)calloc(1, sizeof(StreamHandler));
    
	/* calloc handles setting everything to NULL */
	pThis->userData = pUserData;
	pThis->closeHandler = def_stream_close_hndlr;
	pThis->commentHandler = def_stream_cmt_hndlr;
	return pThis;
}

void del_StreamHandler(StreamHandler* pThis){
    if(pThis) free(pThis);
}
