/* Copyright (C) 2000-2017 Larry Granroth <larry-granroth@@uiowa.edu>
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


/*-----------------------------------------------------------------------

  das1_stream prints fields from das data stream 
  similar to paf, except that a base time is given on the command line
  which is used to print absolute time value in PDS format, and this
  program can handle packetized das1 streams

-----------------------------------------------------------------------*/
#define _POSIX_C_SOURCE 200112L

 
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <limits.h>
#include <stdint.h>

#include "das2/util.h"
#include "das2/time.h"
#include "das2/value.h"

/*#ifndef _WIN32
#define INLINE inline
#else
#define INLINE
#endif
*/
#define INLINE 

INLINE float make_float(unsigned char* pChar){
	
	unsigned int nOut = 
          pChar[0] << 24 | pChar[1] << 16 | pChar[2] << 8 | pChar[3];
	
	/* This breaks optimization assumptions that GCC wants to make
	   use mem-move instead */
	/* return *( (float*)(&nOut) ); */
	float fOut;
	memcpy(&fOut, &nOut, 4);
	return fOut;
}

/* ************************************************************************* */
/* Help Info */

void prnUsage(FILE* pFile){
	fprintf(pFile, "Usage: das1_ascii ITEMS_PER_ROW START_TIME.\n"
	                "    (use -h for more help.)\n");
}

void prnHelp(FILE* pFile){
	fprintf(pFile, 
"das1_ascii - Print time indexed Das1 streams\n"
"\n"
"SYNOPSIS\n"
"     das_pdslist [options] START\n"
"\n"
"DECRIPTION\n"
"     das_pdslist takes a das1 stream on standard input and prints\n"
"     each 'row' as ascii data.  The first item in each row is assumed\n"
"     to be a time offset.  The following command line parameter is \n"
"     required:\n"
"\n"
"     START     DAS1 streams emitt time as 32-bit floating point values which\n"
"               are the offset in seconds from the start of the reader query.\n"
"               In order to interperate these offsets as absolute times\n"
"               das_pdslist needs the starting point.\n"
"\n"
"     Streams that start with the ascii bytes ':b0:' are assumed to be\n"
"     packetized Das1 streams containing an X multy Y dataset and the number\n"
"     of Y items is inferred from the encoded byte length.  Otherwise the\n"
"     stream is assumed to be a plain Das1 stream without packet headers and\n"
"     the program fails to print the stream unless the -y option was specified.\n"
"\n"
"OPTIONS\n"
"     -y ITEMS  The number of Y items in each row of data.  It is assumed\n"
"               that there is always a X time coordinate as well.  If you\n"
"               have access to a dataset descriptor file that goes with the\n"
"               reader, the number of ITEMS is the same as the 'items'\n"
"               keyword in the descriptor file.\n"
"\n"
"     --xyz     By default the first float in each set of ITEMS, or the first\n"
"               float in each packet is assumed to be a time offset value.\n"
"               This option let's das1_ascii know that packet contents are \n"
"               actually (x,y,z) triplets, and so every third value is a \n"
"               time offset and should be skipped.  Furthermore, frequency\n"
"               values will be output as if they were ':b1:' packets.\n"
"\n"
"               This option is not needed for plain streams as ITEMS can be set\n"
"               to 3 to accomplish the same effect.\n"
"\n"
"EXAMPLE\n"
"     Print Cassini Saturn centered ephemerides:\n"
"\n"
"          cephemrdr 2 60 2012-001 2012-002 | das_pdslist 6 2012-001\n"
"\n"
"BUGS\n"
"     Currently only plain Das 1 streams and ':B0:' packet streams are\n"
"     printable.\n"
"\n"
"SEE ALSO\n"
"     The DAS 1, packetized data format is defined at:\n"
"        http://www-pw.physics.uiowa.edu/plasma-wave/group/das/doc/is_mar96.html\n"
"\n"
	     );
	
}

/* ************************************************************************* */
void prnTime(double dTime){
	das_time dt;
	dt_emitt(dTime, &dt);
		
	printf("%04d-%02d-%02dT%02d:%02d:%06.3fZ", dt.year, dt.month, dt.mday,
			       dt.hour, dt.minute, dt.second);
}


/* ************************************************************************* */
int prnFloats(const das_time* dtBeg, int nItems, char* sTestBuf)
{
	
	double dBeg = dt_ttime(dtBeg);
		
	unsigned char* sBuf = (unsigned char*)calloc((nItems+1) * sizeof(float), 1);
	float*         fBuf = (float*)calloc(nItems+1, sizeof(float));
	
	if(!sBuf || !fBuf) return 127;
	
	/* First read is special since stdin is not a seekable stream */
	memcpy(sBuf, sTestBuf, 8);
	size_t uOffset = 2*sizeof(float);
	int nFlts2Read = (nItems + 1) - 2;
	das_time dt = {0};
	int i = 0;
	while(fread(sBuf+uOffset, sizeof(float), nFlts2Read, stdin) >= nFlts2Read){
	 
		for(i = 0; i<nItems+1; ++i) fBuf[i] = make_float(sBuf + i*sizeof(float));
	  
		dt_emitt(dBeg + fBuf[0], &dt);
		
		printf("%04d-%02d-%02dT%02d:%02d:%06.3fZ", dt.year, dt.month, dt.mday,
		      dt.hour, dt.minute, dt.second);
		
		for (i = 1; i < nItems+1; i++) printf ("%11.3e", fBuf[i]);
		printf ("\n");
	 
		nFlts2Read = nItems + 1;
		uOffset = 0;
  }

  return 0;
}

/* ************************************************************************* */
int prnPackets(const das_time* dtBeg, char* sFirstHdr, bool bXYZ)
{
	double dBeg = dt_ttime(dtBeg);
	
	size_t u;
	char sHdr[16] = {0};
	
	for(u=0;u<8;u++) sHdr[u] = sFirstHdr[u];
	
	int nBytes = 0;
	size_t uBytes = 0;
	size_t uRead = 0;
	
	uint8_t* pPktBuf = NULL;
	size_t uBufLen = 0;
	
	float* pFloats = NULL;
	size_t uFloats = 0;
	
	size_t uCurFreqs = 0;
	
	float* pFreqs = NULL;
	size_t uFreqs = 0;
	
	int nPkt = 1;	
	while(true){
		
		if(!das_strn2baseint(sHdr + 4, 4, 16, &nBytes)){
			fprintf(stderr, "ERROR: In packet %d, can't parse byte length "
			        "from %4s\n", nPkt, sHdr + 4);
			return 100;
		}
		if(nBytes < 1){
			fprintf(stderr, "ERROR: In packet %d, short packet length %d\n",
			        nPkt, nBytes);
			return 100;
		}
		else
			uBytes = (size_t)nBytes;
		
		/* Make more room if needed */
		if(uBufLen < uBytes){
			if(pPktBuf != NULL) free(pPktBuf);
			pPktBuf = (uint8_t*) calloc(uBytes, 1);
		}
		uBufLen = uBytes;
		
		if( (uRead = fread(pPktBuf, 1, uBytes, stdin)) < uBytes ){
			fprintf(stderr, "ERROR: In packet %d, short packet data count %zu\n",
			        nPkt, uRead);
			return 100;
		}
		
		
		/* Get the floats, make room if needed */
		if(uFloats < (uBufLen / 4)){
			if(pFloats != NULL) free(pFloats);
			pFloats = (float*)calloc(uBufLen/4, 4);
		}
		uFloats = uBufLen / 4;
		for(u = 0; u<uFloats; ++u) pFloats[u] = make_float(pPktBuf + u*4);
		
		
		/* Print frequency table packets  */
		if(strncmp(":by:", sHdr, 4) == 0){
			printf("# YTags: ");
			for(u = 0; u < uFloats; ++u) 
				printf ("%11.3e", pFloats[u]);
			printf("\n");
		}
		
		/* Print time offset packets */
		if(strncmp(":bx:", sHdr, 4) == 0){
			printf("# X-Offsets: ");
			for(u = 0; u < uFloats; ++u) printf("%11.3e", pFloats[u]);
		}
		
		/* Print data packets */
		if(strncmp(":b0:", sHdr, 4) == 0){
					
			if(!bXYZ){
				
				/* Do this the easy way... */	
				prnTime(dBeg + pFloats[0]);	
				for(u = 1; u < uFloats; ++u) printf("%11.3e", pFloats[u]);	
				
			}
			else{
				
				/* ... or try to flatten the x-y-z points */
				if(uFloats % 3 != 0){
					fprintf(stderr, "ERROR: --xyz was specified on the command line, "
							"but packet %d has %zu values, which is not a number that "
							"is divisible by 3.\n", 
							nPkt, uFloats);
					return 101;
				}
										
				/* See if the current frequency set is different from the last */
				bool bSame = (pFreqs != NULL); /* not same if no last freqs */
				
				uCurFreqs = uFloats /3;
				if(bSame)                          /* not same if diff lengths */
					bSame = (uCurFreqs == uFreqs); 
				
				if(bSame){                         /* not same if any different */
					for(u = 0; u<uCurFreqs; u++){
						if(pFreqs[u] != pFloats[3*u+1]){ 
							bSame = false; 
							break; 
						}
					}
				}
				
				/* If not the same, print the freqs and store them */
				if(!bSame){
					
					/* Make more room if needed */
					if(uCurFreqs > uFreqs){
						if(pFreqs) free(pFreqs);
						if( (pFreqs = (float*)calloc(uCurFreqs, 4)) == NULL){
							fprintf(stderr, "ERROR: Can't calloc %zu bytes\n", uCurFreqs*4);
							return 7;
						}
					}
					uFreqs = uCurFreqs;
							
					printf("# YTags: ");
					for(u = 0; u<uFreqs; ++u){ 
						pFreqs[u] = pFloats[3*u + 1];
						printf ("%11.3e", pFreqs[u]);
					}
					printf("\n");
				}
			   
				/* Print Amplitudes */
				prnTime(dBeg + pFloats[0]);
				for(u = 2; u < uFloats; u += 3) printf("%11.3e", pFloats[u]);
				
				/* Check to see if the times changed during the packet */
				for(u = 3; u < uFloats; u += 3){
					if(pFloats[u] != pFloats[0]){
						fprintf(stderr, "WARNING: Time offsets changed during packet "
								  "%d!\n", nPkt);
						break;
					}
				}
			}
			printf("\n");
		}
		
		fflush(stdout);
		
		/* Next Header or quit */
		if( (uRead = fread(sHdr, 1, 8, stdin) ) == 0) break;
		nPkt++;
		
		if(uRead < 8){
			fprintf(stderr, "ERROR: In packet %d, couldn't read 8 byte packet "
			        "header\n", nPkt);
			return 100;
		}
	}	
	
	return 0;
}


/* ************************************************************************* */

int main (int argc, char *argv[])
{
	int i;
	int nItems = 0;
	das_time dtBeg = {0};
	char sTestBuf[24] = {'\0'}; 
	bool bHasPkts = false;
	bool bXYZ = false;
	
	if((argc < 2)||(argc > 4)){
		prnUsage(stderr);
		return 13;
	}
  
	for(i = 1; i<argc; ++i){
		if((strcmp(argv[i], "-h") == 0)||(strcmp(argv[i], "--help") == 0)){
			prnHelp(stdout);
			return 0;
		}
		
		if(strcmp(argv[i], "--xyz") == 0){
			bXYZ = true;
			continue;
		}
		
		if(strcmp(argv[i], "-y") == 0){
			++i;
			if(i >= argc){
				fprintf(stderr, "Items value missing for -y option.  Use -h for "
				        "more help.\n");
				return 13;
			}
			if(!sscanf (argv[i], "%d", &nItems) || nItems < 1){
				fprintf(stderr, "Couldn't convert %s to a positive integer > 0."
				        " Use -h for more help.\n", argv[i]);
				return 13;
			}
			continue;
		}
		
		if(dtBeg.year == 0){
			if(!dt_parsetime(argv[i], &dtBeg)){ 
				fprintf(stderr, "Couldn't parse %s as a date-time\n", argv[i]);
				return 13;
			}
			continue;
		}
		
		fprintf(stderr, "Error at argument '%s', use -h for more help\n",argv[i]);
		return 13;
	}
	
	if(dtBeg.year == 0){
		fprintf(stderr, "Start-time argument missing, use -h for more help.\n");
		return 13;
	}
  
	if( sizeof(int) != sizeof(float) ){
		fprintf(stderr, "ERROR: int's and float's aren't stored in the same "
				 "number of bytes on this architecture.\n");
		return 7;
  }
  
	/* See if this is a packetized stream, requires a read ahead which is
		annoying */
   if(fread(sTestBuf, sizeof(char), 8, stdin) < 8){
		fprintf(stderr, "ERROR: Input stream is less than 8 characters long\n");
		return 3;
	}
	
	bHasPkts = ( (strncmp(":b0:", sTestBuf, 4) == 0) ||
	             (strncmp(":bx:", sTestBuf, 4) == 0) ||
	             (strncmp(":by:", sTestBuf, 4) == 0) );
		
	if(!bHasPkts && (nItems == 0)){
		fprintf(stderr, "ERROR: Input stream does not have recognizable packet "
		        "headers and -y was not specified.  Use -h for more help.\n");
		return 12;
	}
	
	if(bHasPkts)
		return prnPackets(&dtBeg, sTestBuf, bXYZ);
	else
		return prnFloats(&dtBeg, nItems, sTestBuf);
}
