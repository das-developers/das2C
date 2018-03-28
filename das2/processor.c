#define _POSIX_C_SOURCE 200112L

#include <stdio.h>

#include "processor.h"


/* ************************************************************************* */
/* Default Stream Object Handlers */

ErrorCode defaultStreamExceptionHandler(OobExcept* se, void* ud) 
{
    fprintf(stderr, "StreamException encountered\n" );
    fprintf(stderr, "  type: %s\n", se->sType );
    fprintf(stderr, "  message: %s\n", se->sMsg );
	 return 24;
}

ErrorCode defaultStreamCommentHandler(OobComment* sc, void* ud) 
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
