#define _POSIX_C_SOURCE 200112L


/*---------------------------------------------------------------------

  daspkt.c written by L. Granroth  1998-02-24

  Utility routines for handling das packets.
  
  Added byte swapping by C. Piker 2016-06-30

  ---------------------------------------------------------------------*/

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <assert.h>

#include "das1.h"

/* Swap single floats, returns new float, wanted to have this as an 
   inline function, but this keeps causing "repeated definition" 
	problems during compilation of programs that use libdas2.a */
	
float swapFloat(float rIn){
	const uint8_t* pIn  = (const uint8_t*)(&rIn);
	
	float rOut;
	uint8_t* pOut = (uint8_t*)(&rOut);
	
	pIn += 3;       *pOut = *pIn; 
	pIn--; pOut++;  *pOut = *pIn; 
	pIn--; pOut++;  *pOut = *pIn; 
	pIn--; pOut++;  *pOut = *pIn;
	
	return rOut;
}


/* extern char *myname; */

void fail (const char *msg)
{
  if (errno) perror ("ERROR: ");
  if (msg) fprintf (stderr, "ERROR: %s\n", msg);
  exit (DAS1ERR);
} /* fail */

int fgetpkt (FILE* fin, char *ph, void *data, int max)
{
  int datsize;
  int mask = *(int *)":\0\0:";

  if (!fread (ph, 8, 1, fin)) return 0;
  if ( (*(int *)ph & mask) != mask) return 0;
  if (!sscanf (ph+4, "%4x", &datsize)) return 0;
  if (datsize > max) {
     fprintf(stderr, "ERROR: In getpkt, buffer Not Big Enough! Need "
             "%d bytes!\n", datsize);
	  exit(DAS1ERR);
  }
  int read = fread(data, 1, datsize, fin);
  
  if (read < datsize){
	  fprintf(stderr, "ERROR: Size in das1 packet header %d > remainder "
	          "of input, %d\n", datsize, read);
     exit(DAS1ERR);
  }
  
  
  /* All das packet stream are still floats, even if tagged with
     b0 - data
	  by - Ycoordinates (frequencies)
	  bx - Xadjust (time offsets for each frequency)
  */
  if(read/4 > 0) swapBufIfHostLE(data, 4, read/4);
  
  return read;
}


int getpkt (char *ph, void *data, int max)
{
  int datsize;
  int mask = *(int *)":\0\0:";

  if (!fread (ph, 8, 1, stdin)) return 0;
  if ( (*(int *)ph & mask) != mask) return 0;
  if (!sscanf (ph+4, "%4x", &datsize)) return 0;
  if (datsize > max) {
     fprintf(stderr, "ERROR: In getpkt, buffer Not Big Enough! Need "
             "%d bytes!\n", datsize);
	  exit(DAS1ERR);
  }
  int read = fread (data, 1, datsize, stdin);
  if (read < datsize){
	  fprintf(stderr, "ERROR: Size in das1 packet header %d > remainder "
	          "of file\n", datsize);
     exit(DAS1ERR);
  }
  
  /* All das packet stream are still floats, even if tagged with
     b0 - data
	  by - Ycoordinates (frequencies)
	  bx - Xadjust (time offsets for each frequency)
  */
  swapBufIfHostLE(data, 4, datsize/4);
  
  return datsize;
}

int putpkt (const char *ph, const void *data, const int bytes)
{
  char hex[5];
  int mask = *(int *)":\0\0:";
 
  if ( (*(int *)ph & mask) != mask) return 0;
  if (bytes <= 0 || bytes >= 32768) return 0;
  sprintf (hex, "%04X", bytes);
  if (!fwrite (ph, 4, 1, stdout)) return 0;
  if (!fwrite (hex, 4, 1, stdout)) return 0;
  
#ifdef HOST_IS_LSB_FIRST
  int items = 0;
  float f;
  for(int i = 0; i < bytes/4; ++i){
	  f = swapFloat( *((float*)(data + i*4)) );
     items += fwrite(&f, 4, 1, stdout);
  }
  return items * 4;
#else
  return fwrite (data, bytes, 1, stdout);
#endif
}

void _swapBufInPlace(void* pMem, size_t szEach, size_t numItems)
{
	/* Only works when szEach is an even number greater than 0 */
	assert((szEach % 2 == 0) && szEach);
	size_t szHalf = szEach / 2;
	
	uint8_t* _pMem = (unsigned char*)pMem;
	uint8_t* pSrc = 0;
	uint8_t* pDest = 0;
	
	uint8_t uTmp = 0;
	
	size_t n = 0, e = 0;
	for(n = 0; n < numItems; ++n){
		for(e = 0; e < szHalf; ++e){
			pSrc  = _pMem + n*szEach + e;
			pDest = _pMem + n*szEach + (szEach - e - 1);
			uTmp = *pDest;
			*pDest = *pSrc;
			*pSrc = uTmp;
		}
	}
}





