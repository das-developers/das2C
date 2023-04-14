/* Copyright (C) 1993-2018 Larry Granroth <larry-granroth@uiowa.edu>
 *                         Joseph Groene  <joseph-groene@uiowa.edu>
 *                         Chris Piker    <chris-piker@uiowa.edu>
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


/*---------------------------------------------------------------------

  daspkt.c written by L. Granroth  1998-02-24

  Utility routines for handling das packets.
  
  Added byte swapping by C. Piker 2016-06-30

  ---------------------------------------------------------------------*/

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <assert.h>
#include <math.h>

/* #include "util.h" */
#include "time.h"
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

void swapU4(uint32_t* pIn){
  uint32_t nOut;
  uint8_t* pOut = (uint8_t*)(&nOut);
  
  pIn += 3;       *pOut = *pIn; 
  pIn--; pOut++;  *pOut = *pIn; 
  pIn--; pOut++;  *pOut = *pIn; 
  pIn--; pOut++;  *pOut = *pIn;
  
  *pIn = nOut;
}


/* extern char *myname; */

void fail (const char *msg)
{
  if (errno) perror ("ERROR: ");
  if (msg) fprintf (stderr, "ERROR: %s\n", msg);
  exit (D1ERR);
} /* fail */

int fgetpkt (FILE* fin, char *ph, byte *data, int max)
{
  int datsize;
  int mask = *(int *)":\0\0:";

  if (!fread (ph, 8, 1, fin)) return 0;
  if ( (*(int *)ph & mask) != mask) return 0;
  if (!sscanf (ph+4, "%4x", &datsize)) return 0;
  if (datsize > max) {
     fprintf(stderr, "ERROR: In getpkt, buffer Not Big Enough! Need "
             "%d bytes!\n", datsize);
	  exit(D1ERR);
  }
  int read = fread(data, 1, datsize, fin);
  
  if (read < datsize){
	  fprintf(stderr, "ERROR: Size in das1 packet header %d > remainder "
	          "of input, %d\n", datsize, read);
     exit(D1ERR);
  }
  
  
  /* All das packet stream are still floats, even if tagged with
     b0 - data
	  by - Ycoordinates (frequencies)
	  bx - Xadjust (time offsets for each frequency)
  */
  if(read/4 > 0) swapBufIfHostLE(data, 4, read/4);
  
  return read;
}


int getpkt (char *ph, byte *data, int max)
{
  int datsize;
  int mask = *(int *)":\0\0:";

  if (!fread (ph, 8, 1, stdin)) return 0;
  if ( (*(int *)ph & mask) != mask) return 0;
  if (!sscanf (ph+4, "%4x", &datsize)) return 0;
  if (datsize > max) {
     fprintf(stderr, "ERROR: In getpkt, buffer Not Big Enough! Need "
             "%d bytes!\n", datsize);
	  exit(D1ERR);
  }
  int read = fread (data, 1, datsize, stdin);
  if (read < datsize){
	  fprintf(stderr, "ERROR: Size in das1 packet header %d > remainder "
	          "of file\n", datsize);
     exit(D1ERR);
  }
  
  /* All das packet stream are still floats, even if tagged with
     b0 - data
	  by - Ycoordinates (frequencies)
	  bx - Xadjust (time offsets for each frequency)
  */
  swapBufIfHostLE(data, 4, datsize/4);
  
  return datsize;
}

int putpkt (const char *ph, const byte *data, const int bytes)
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
	  f = swapFloat( *((const float*)(data + i*4)) );
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


/* ************************************************************************* */
/* Used in most of Joe Groene's programs */

static int days_past [138] = {     0,   365,   730,  1096,  1461,  1826,
                                2191,  2557,  2922,  3287,  3652,  4018,
                                4383,  4748,  5113,  5479,  5844,  6209,
                                6574,  6940,  7305,  7670,  8035,  8401,
                                8766,  9131,  9496,  9862, 10227, 10592,
                               10957, 11323, 11688, 12053, 12418, 12784,
                               13149, 13514, 13879, 14245, 14610, 14975,
                               15340, 15706, 16071, 16436, 16801, 17167,
                               17532, 17897, 18262, 18628, 18993, 19358,
                               19723, 20089, 20454, 20819, 21184, 21550,
                               21915, 22280, 22645, 23011, 23376, 23741,
                               24106, 24472, 24837, 25202, 25567, 25933,
                               26298, 26663, 27028, 27394, 27759, 28124,
                               28489, 28855, 29220, 29585, 29950, 30316,
                               30681, 31046, 31411, 31777, 32142, 32507,
                               32872, 33238, 33603, 33968, 34333, 34699,
                               35064, 35429, 35794, 36160, 36525, 36890,
                               37255, 37621, 37986, 38351, 38716, 39082,
                               39447, 39812, 40177, 40543, 40908, 41273,
                               41638, 42004, 42369, 42734, 43099, 43465,
                               43830, 44195, 44560, 44926, 45291, 45656,
                               46021, 46387, 46752, 47117, 47482, 47848,
                               48213, 48578, 48943, 49309, 49674, 50039 };

void yrdy1958(int* pYear, int* pDoy, int days_since_1958)
{
   int index;
	
	if((days_since_1958 < 0)||(days_since_1958 > 50404)){
		fprintf(stderr, "ERROR: Can't convert %d Days Since 1958 to a year and "
				  "day of year value\n", days_since_1958);
		exit(127);
	}

   index = 0;
   while (days_since_1958 >= days_past [index])
      index++;

   index--;
   *pYear = 1958 + index;
   *pDoy = days_since_1958 - days_past [index] + 1;
}

int past_1958 (int year, int day)
{
   int yr, past;

   yr = year - 1958;
   past = days_past [yr];
   past += (day - 1);

   return (past);
}

void ms2hms (int* pHour, int* pMin, float* pSec, double ms_of_day)
{
   *pSec = (float) fmod ((double) (ms_of_day / 1000.0), 60.0);
   *pMin = (int) fmod ((double) (ms_of_day / 60000.0), 60.0);
   *pHour = (int) fmod ((double) (ms_of_day / 3600000.0), 24.0);
}

void dt_from_1958(
	unsigned short int daysSince1958, unsigned int msOfDay, das_time* dt
){
	dt->month = 1;
	yrdy1958( &(dt->year), &(dt->mday), daysSince1958);
	float fSec = 0.0;
	ms2hms( &(dt->hour), &(dt->minute), &fSec, msOfDay);
	dt->second = fSec;
	dt_tnorm(dt);
}	


