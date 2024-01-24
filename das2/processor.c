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

#define _POSIX_C_SOURCE 200112L

#include <stdio.h>

#include "processor.h"


/* ************************************************************************* */
/* Default Stream Object Handlers */

DasErrCode defaultStreamExceptionHandler(OobExcept* se, void* ud) 
{
    fprintf(stderr, "StreamException encountered\n" );
    fprintf(stderr, "  type: %s\n", se->sType );
    fprintf(stderr, "  message: %s\n", se->sMsg );
	 return 24;
}

DasErrCode defaultStreamCommentHandler(OobComment* sc, void* ud) 
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
	pThis->closeHandler = NULL;
    pThis->dsDescHandler = NULL;
    pThis->dsDataHandler = NULL;
	pThis->exceptionHandler = defaultStreamExceptionHandler;
   pThis->commentHandler = defaultStreamCommentHandler;
}

StreamHandler* new_StreamHandler(void* pUserData)
{
	StreamHandler* pThis = (StreamHandler*)calloc(1, sizeof(StreamHandler));
    
	/* calloc handles setting everything to NULL */
	pThis->userData = pUserData;
	pThis->exceptionHandler = defaultStreamExceptionHandler;
	pThis->commentHandler = defaultStreamCommentHandler;
	return pThis;
}

void del_StreamHandler(StreamHandler* pThis){
    if(pThis) free(pThis);
}
