/* Copyright (C) 2004       Jeremy Faden <jeremy-faden@uiowa.edu>
 *               2015-2017  Chris Piker  <chris-piker@uiowa.edu>
 *                         
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
#include <stdlib.h>
#include <string.h>

#include <das2/core.h>


/* The output stream header object, we can get to all our output objects
   from this pointer */
StreamDesc* g_pOutSd = NULL;

/* The following three globals keep track of data while averaging */

/* The current X-axis 'bin number' by packet id */
long    ibin[100] = {0};

/* Sum accumulation array, one for each plane of each packet type, fill values
 * aren't added to the count */
double* sum[100][MAXPLANES] = {{NULL}};

/* Counting array, one for each plane of each packet type, fill values aren't
   added to the count. */
double* count[100][MAXPLANES] = {{NULL}};


/* The following two globals keep track of data bins */

double binSize = 0.0;   /* Set from the command line */
double bin0min= -1e31;  /* Place holder value with be replaced with starting
                           X coordinate the first output bin */

DasErrCode sendData(DasIO* pOut, StreamDesc* pSd, int iPktId)
{
	PktDesc*   pPkt = StreamDesc_getPktDesc(pSd, iPktId);
	PlaneDesc* pPlane = NULL;
	int i = 0, iPlane = 0;
	double value = 0.0; 
	
	/* Don't flush a packet that hasn't seen any data */
	if(ibin[iPktId] == -99999) return DAS_OKAY;
	
	for(iPlane = 0; iPlane < PktDesc_getNPlanes(pPkt); iPlane++){
		pPlane = PktDesc_getPlane(pPkt, iPlane);

		for(i = 0; i < PlaneDesc_getNItems(pPlane); i++){
				
			if(pPlane->planeType == X){
				value = binSize*(ibin[iPktId]+0.5) + bin0min;
			}
			else{
				if(count[iPktId][iPlane][i] == 0.0)
					value = PlaneDesc_getFill(pPlane);
				else
					value = sum[iPktId][iPlane][i] / count[iPktId][iPlane][i];
			}	
				
			PlaneDesc_setValue(pPlane, i, value);
			sum[iPktId][iPlane][i] = 0.0;
			count[iPktId][iPlane][i] = 0.0;
		}
	}	
	
	return DasIO_writePktData(pOut, pPkt);
}

DasErrCode onStreamHdr( StreamDesc* sd, void* ud) {
    DasIO* pOut = (DasIO*)ud;
	 int i;
	 
	 g_pOutSd = StreamDesc_copy(sd);
	 
	 for(i=0; i<100; i++)
        ibin[i]= -99999;
    
	 g_pOutSd->bDescriptorSent = false;
	 
   return DasIO_writeStreamDesc(pOut, g_pOutSd);
}

DasErrCode onPktHdr(StreamDesc* pSdIn, PktDesc* pPdIn, void* vpOut)
{
	DasIO* pOut = (DasIO*)vpOut;	
	int nPktId= pPdIn->id;

	/* Get rid of the existing packet definition if a new definition comes in
	 * with the same id */
	if(StreamDesc_isValidId(g_pOutSd, nPktId) ){
		sendData(pOut, g_pOutSd, nPktId);
		StreamDesc_freeDesc(g_pOutSd, nPktId);
	}
	
	/* Deep copy the input pkt */
	PktDesc* pPdOut = StreamDesc_clonePktDescById(g_pOutSd, pSdIn, nPktId);
	if(pPdOut == NULL) return 100;
	
	/* Initialize the plane arrays we'll use to store sums and counts */
	PlaneDesc* pPlIn = NULL;
	int iPlane = 0, nItems = 0;
	for(iPlane = 0; iPlane < PktDesc_getNPlanes(pPdIn); iPlane++){
		pPlIn = PktDesc_getPlane(pPdIn, iPlane);
		nItems = PlaneDesc_getNItems(pPlIn); 
		if(sum[nPktId][iPlane] != NULL) free(sum[nPktId][iPlane]);
		if(count[nPktId][iPlane] != NULL) free(count[nPktId][iPlane]);
		sum[nPktId][iPlane] = (double*)calloc(nItems, sizeof(double));
		count[nPktId][iPlane] = (double*)calloc(nItems, sizeof(double));
	}
	
	/* Output the new packet descriptor */
	return DasIO_writePktDesc(pOut, pPdOut);
}

DasErrCode onPktData(PktDesc* pPdIn, void* vpOut) {
   DasIO* pOut = (DasIO*)vpOut;
	DasErrCode nRet = 0;
	
	int iPlane, i;
	double xTag;
	int ibinThis;
	PlaneDesc* pX = NULL;
	PlaneDesc* pPlane = NULL;
	const double* pVals = NULL;

	int packetId= pPdIn->id;
	
	pX = PktDesc_getXPlane(pPdIn);
	xTag = PlaneDesc_getValue(pX, 0);
    if ( bin0min==-1e31 ) {
        bin0min= xTag;
    }

    ibinThis= ( xTag - bin0min ) / binSize;

    if ( ibinThis!=ibin[packetId] ) {
        if((nRet = sendData(pOut, g_pOutSd, packetId)) != 0) return nRet;
        ibin[packetId]= ibinThis;
    }
	 
	for(iPlane = 0; iPlane < pPdIn->uPlanes; iPlane++){
		pPlane = PktDesc_getPlane(pPdIn, iPlane);
		
		if(pPlane->planeType == X) continue;
		
		pVals = PlaneDesc_getValues(pPlane);
		for(i = 0; i < PlaneDesc_getNItems(pPlane); i++){
			if(!PlaneDesc_isFill(pPlane, pVals[i])){
				sum[packetId][iPlane][i] += pVals[i];
				count[packetId][iPlane][i] += 1;
			}
		}
	}
	return 0;
}

DasErrCode onClose( StreamDesc* pSdIn, void* vpOut ) {
   DasIO* pOut = (DasIO*)vpOut;
	
	int i;
    for ( i=1; i<100; i++ ) {
        if (StreamDesc_isValidId(g_pOutSd, i)) {
           sendData(pOut, g_pOutSd, i);
        }
    }
	return DAS_OKAY;
}

/* ************************************************************************* */

void prnHelp()
{
	fprintf(stderr, 
"SYNOPSIS:\n"
"   das2_bin_avgsec - Reduces the size of Das2 streams by averaging\n"
"\n"
"USAGE:\n"
"   READER | das2_bin_avg BIN_WIDTH\n"
"\n"
"DESCRIPTION:\n"
"   das2_bin_avg is a classic Unix filter, reading a Das 2 Stream on standard\n"
"   input and producing an X-axis reduced Das 2 stream on standard output.  The\n" 
"   program averages <y>, <z> and <yscan> data values over <x>, but does not\n"
"   preform rebinning across packet types.  Only values with the same packet\n"
"   ID and the same plane index are averaged.  Within <yscan> planes, only\n"
"   Z-values with the same Y coordinate are combined.\n"
"\n"
"   The BIN_WIDTH parameter provides the number of <x> units over which to \n"
"   average <y>, <yscan>, and <z> plane values.  Up to total 99 planes may \n"
"   exist in each packet type, and up to 99 packet types may exist in the input\n"
"   stream.  This is a plane limit, not a limit on the total number of data\n"
"   vectors, since <yscan> planes may contain an arbitrary number values per\n"
"   plane per packet type.  The output stream has the same form as the input\n"
"   stream but presumably with many fewer data packets.\n"
"\n"
"LIMITATIONS:\n"
"   * This is a 1-dimensional averager, <x>, <y>, <z> scatter data are\n"
"     handled by this reducer as if <y> was not an independent value.  A\n"
"     proper 2-D bin averager should be used for such datasets.\n"
"\n"
"   * The BIN_WIDTH parameter has no units, so you have to just know the\n"
"     units of the input stream somehow in order to pick a proper bin width.\n"
"     See the program 'das2_bin_avgsec' for an averager that scales <x>\n"
"     units to the BIN_WIDTH units during processing.\n"
"\n"
"AUTHORS:\n"
"   chris-piker@uiowa.edu   (2015 revised)\n"
"   jeremy-faden@uiowa.edu  (original)\n"
"\n"
"SEE ALSO:\n"
"   das2_bin_avgsec, das2_bin_peakavgsec, das2_ascii\n"
"\n"
"   The Das 2 ICD @ http://das2.org for an introduction to the das 2 system.\n"
"\n");
}

/* ************************************************************************* */
int main( int argc, char *argv[]) {
	int status;
	
	/* Exit on errors, log info messages and above */
	das_init(argv[0], DASERR_DIS_EXIT, 0, DASLOG_INFO, NULL);
    
	if(argc!=2){
		fprintf(stderr, "Usage: das2_bin_avg BIN_WIDTH \n"
		        "Issue the command %s -h for more info.\n\n", argv[0]);
		return 13;
	}
	if(strcmp(argv[1], "-h") == 0 || strcmp(argv[1], "--help") == 0){
		prnHelp();
		return 0;
	}

	sscanf( argv[1], "%lf", &binSize );

	DasIO* pOut = new_DasIO_cfile("das2_bin_avg", stdout, "w");
   
	/* An example of using a user-data pointer.  In this case the user-data
	   pointer is set to the output writer.  That way all the input processing
		functions will have access to the output writer without consulting a 
		global variable.  This done as a demonstration since it's overkill
		for a small single-threaded program */
	StreamHandler* pSh = new_StreamHandler(pOut);
	
	pSh->closeHandler = onClose;
	pSh->streamDescHandler = onStreamHdr;
	pSh->pktDescHandler = onPktHdr;
	pSh->pktDataHandler = onPktData;
   
	DasIO* pIn = new_DasIO_cfile("Standard Input", stdin, "r");
	DasIO_addProcessor(pIn, pSh);
	 
	status = DasIO_readAll(pIn);
	return status;
}

