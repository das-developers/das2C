/* Copyright (C) 2004  Robert Johnson <robert-a-johnson@uiowa.edu>
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

/* Converts the das1 stream B0 triplicate packets to the das2 stream.

Version 0.0
Version 0.1 March 22, 2004
  add sample width dInterpWidth as a command line option.
Version 0.2 March 22, 2004
  das2stream float had double array insteaded of float arrays, causing
  fill values to be injected into the stream.
Version 0.2 March 3, 2004
  das2 library change 
  
Version 0.4 October 19, 2004
  das2 library change 
    - unit types are not integers any more => strings
	 
Version 0.5 (no documentation was present)
	 
Version 0.6
  Copied over to cassini SVN source tree, changed includes.

Version 0.7 2016-09-11
  Reworked to use current version of libdas2.a (cwp), major changes but
  mostly the same functionality.  Major change is that packet IDs are
  reused if N different modes are interleaved.
  
  Since this is a general tool, it has been moved into the regular C Das2
  tools area and renamed
*/

#include <stdio.h> 
#include <stdlib.h>
#include <assert.h>
#include <string.h>

#ifdef _WIN32
#define strcasecmp _stricmp
#else
#include <strings.h>
#endif

#include <das2/das1.h>
#include <das2/core.h>

const char *sVersion="das2_from_tagged_das1 Ver 0.7 (formerly dasI2dasII)";

/* ************************************************************************* */

double timestr_to_epoc_1958(char *sTime)
{	
	das_time dt;
	int i,nDays;
	double dTime;


	if(! dt_parsetime(sTime,&dt)){
		fprintf(stderr,"dt_parsetime(%s) failed\n",sTime);
		exit(1);
	}

	nDays=0;
	for(i=1958;i<dt.year;i++){
		if(i%100)                  /* Year is NOT a century year */
			nDays+=(i%4)?365:366;    /* if evenly divisible by 4, leap year */
		else                       /* Year is a century year */
			nDays+=(i%400)?365:366;  /* if evenly divisible by 400, leap year */
	}
	nDays+=(dt.yday-1);   /* doy is number 1-365 (366) */

	dTime=0;
	dTime+=dt.hour/24.0;
	dTime+=dt.minute/(24.0*60.0);
	dTime+=dt.second/(24.0*60.0*60.0);
	dTime+=nDays;

	return dTime;
}

/* ************************************************************************* */
/* Command line options */

typedef struct {        /* Defaults: */
	bool bSilent;        /* false */
	int nStreamType;     /* ST_FLOAT */
	int nTimeFormat;     /* EPOCH_1958 */
	double dInterpWidth; /* 128.0 */
	double dInterpHeight;/* 0.0 */
	das_units utYvals;    /* UNIT_HZ (see units.h) */
	das_units utZvals;    /* UNIT_E_SPECDENS (see units.h) */
	double dFillValue;   /* 1E31 */
	double dBaseTime;    /* Required input */
} Options;

#define ST_ASCII  0
#define ST_FLOAT  1
#define ST_DOUBLE 2

#define EPOCH_1958 0
#define EPOCH_2000 1

void show_help(FILE *h)
{
	fprintf(h,"%s\n",sVersion);
	fprintf(h,
	"  -h               Show help.\n"
	"\n"
	"  -fill DOUBLE     Fill value to be used for bad data, default -1E31\n"
	"\n"
	"  -s               Silent operation, don't ouput to stderr\n"
	"\n"
	"  -t INTEGER       Stream type output: 0=ascii,1=float,2=double, default 1\n"
	"                     (float)\n"
	"\n"
	"  -t2000           Time tags; use days since Jan. 1, 2000, default sec from\n"
	"                   Jan. 1, 1958\n"
	"\n"
	"  -xWidth DOUBLE   Sample width, DDD seconds to interpolate over, default \n"
	"                      128.0 seconds\n"
	"\n"
	"  -yUnit           Set the units for the yValues.  Defaults to Hz\n"
	"\n"
	"  -yWidth DOUBLE   Sample height, DDD yUnits to interpolate over, default\n"
	"                      is to leave this unspecified in the output stream\n"
	"\n"
	"  -zUnit           Set the units for the zValues.  Defaults to\n"
	"                       V**2 M**-2 Hz**-1\n"
	"\n"
	"  -tBeg STRING     Begin time of data capture (required)\n"
	"\n"
	);
}

void getCmdOpts(int argc, char** argv, Options* pOpts){
	int nTmp = 0;
	char* sBeg = NULL;
	while(--argc){
	  ++argv;
	  if( !strcmp("-h",*argv) || !strcmp("-help",*argv)){
	    show_help(stdout);
	    exit(0);
	  }
	  else if(!strcmp("-fill",*argv)){
	    --argc;  ++argv;
	    pOpts->dFillValue=strtod(*argv,NULL);
	  }
	  else if(!strcmp("-s",*argv)){
	    pOpts->bSilent=true;
	  }
	  else if(!strcmp("-t",*argv)){
	    --argc;  ++argv;
		 nTmp = strtoul(*argv,NULL,10);
		 switch(nTmp){
			case 0: pOpts->nStreamType = ST_ASCII; break;
			case 1: pOpts->nStreamType = ST_FLOAT; break;
			case 2: pOpts->nStreamType = ST_DOUBLE; break;
			default:
				fprintf(stderr, "Unknown stream type, '%d', used -h for more info\n",
				       nTmp);
				break;
		 }
	  }
	  else if(!strcmp("-tBeg",*argv)){
	    --argc;  ++argv;
	    sBeg=*argv;
	  }
	  else if(!strcmp("-t2000",*argv)){
	    pOpts->nTimeFormat=EPOCH_2000;
	  }
	  else if(!strcmp("-xWidth",*argv)){
	    --argc;  ++argv;
	    pOpts->dInterpWidth=strtod(*argv,NULL);
	  }
	  else if(!strcmp("-yUnit",*argv)){
	    --argc;  ++argv;
	    pOpts->utYvals = Units_fromStr(*argv);
	  }
	  else if(!strcmp("-zUnit",*argv)){
	    --argc;  ++argv;
	    pOpts->utZvals = Units_fromStr(*argv);
	  }
	  else if(!strcmp("-yWidth",*argv)){
	    --argc;  ++argv;
	    pOpts->dInterpHeight=strtod(*argv,NULL);
	  }
	  else{
	    fprintf(stderr,"argc=%d, argv=%s\n",argc,*argv);
	  }
	}/* while parsing command line argurments */
	
	if(sBeg==NULL){
		fprintf(stderr,"no begin time specified, ex: -tBeg 1958-001T00:00:00.000\n");
		exit(7);
	}
	
	if(!pOpts->bSilent)
		fprintf(stderr,"Begin Time=%s, Fill Value=%.24E\n",sBeg, pOpts->dFillValue);
	
	pOpts->dBaseTime=timestr_to_epoc_1958(sBeg);
}	

/* ************************************************************************* */
/* Helper to see if a frequency table has been sent before */

bool hasMatchingYTags(
	PktDesc* pPd, const char* sPlane, const double* pYTags, size_t uTags
){
	/* Must have a plane with this name */
	PlaneDesc* pPlane = PktDesc_getPlaneByName(pPd, sPlane);
	if(pPlane == NULL) return false;
	
	/* Must have same number of ytags */
	if(PlaneDesc_getNItems(pPlane) != uTags) return false;
	
	/* Must be a YScan Plane */
	const double* pCkTags = NULL;
	if( (pCkTags = PlaneDesc_getOffsets(pPlane)) == NULL) return false;
	
	/* Tags must match */
	for(size_t u = 0; u<uTags; ++u) if(pCkTags[u] != pYTags[u]) return false;
	
	return true;
}

/* ************************************************************************* */
/* If this frequency table is new, make a packet descriptor and send it,     */
/* else just return the matching descriptor */

/* The most likely packet descriptor needed is the one you just sent */
int g_nLastPdId = 0;

/* Track the number of packet types created, just for reporting purposes */
int g_nPktType=0;

PktDesc* sendPktDesc(
	const Options* pOpts, /* Command line options */
	DasIO* pOut,          /* Stream writer object */
	StreamDesc* pSd,      /* Stream description object */
	const char* sPlane,   /* Name to use for yscan plane */
	const double* pFreqs, size_t uFreqs  /* Sorted YTags */
){
	PktDesc* pPd = NULL;
		
	/* Try the last one */
	if(g_nLastPdId != 0){
		pPd = StreamDesc_getPktDesc(pSd, g_nLastPdId);		
		if(hasMatchingYTags(pPd, "amplitude", pFreqs, uFreqs)) return pPd;
	}
	
	/* Okay, search all of them */
	pPd = NULL;
	for(int id = 1; id < 100; ++id){
		if( (pPd = StreamDesc_getPktDesc(pSd, id)) == NULL) continue;
		
		if(hasMatchingYTags(pPd, "amplitude", pFreqs, uFreqs)){ 
			g_nLastPdId = id;
			return pPd;
		}
	}
	
	/* Well that didn't work, make a new one and send it out */
	if(!pOpts->bSilent) fprintf(stderr,"  createPacketDescriptor()...");
	
	DasEncoding* pTimeEnc = NULL;
	if(pOpts->nStreamType == ST_ASCII)
		pTimeEnc = new_DasEncoding(DAS2DT_TIME, 24, NULL);
	else
		pTimeEnc = new_DasEncoding(DAS2DT_HOST_REAL, 8, NULL);
	
	/* Note: libdas2 default is to exit on an error, we don't change that
	   default so there is no point to handling NULL pointer returns, our
		error checking code will never run */
	
	if(pOpts->nTimeFormat == EPOCH_1958){
		pPd = StreamDesc_createPktDesc(pSd, pTimeEnc, UNIT_MJ1958);
		if(!pOpts->bSilent)  fprintf(stderr,"MJ1958...");
	}
	else{
		pPd = StreamDesc_createPktDesc(pSd, pTimeEnc, UNIT_US2000);
		if(!pOpts->bSilent)  fprintf(stderr,"US2000...");
	}
	
	if(!pOpts->bSilent)  fprintf(stderr,"done, addPlane...");
	
	DasEncoding* pAmpEnc = NULL;
	switch(pOpts->nStreamType){
	case ST_ASCII: 
		pAmpEnc = new_DasEncoding(DAS2DT_ASCII, 10, NULL); 
		if(!pOpts->bSilent)  fprintf(stderr,"ascii...");
		break;
	case ST_FLOAT: 
		pAmpEnc = new_DasEncoding(DAS2DT_HOST_REAL, 4, NULL); 
		if(!pOpts->bSilent)  fprintf(stderr,"float...");
		break;
	case ST_DOUBLE:
		pAmpEnc = new_DasEncoding(DAS2DT_HOST_REAL, 8, NULL); 
		if(!pOpts->bSilent)  fprintf(stderr,"double...");
		break;
	default: fprintf(stderr, "WTF???\n"); exit(13); break;
	}
	
	PlaneDesc* pPlane = NULL;
	pPlane = new_PlaneDesc_yscan(
	           sPlane, pAmpEnc, pOpts->utZvals, 
	           uFreqs, NULL, pFreqs, pOpts->utYvals
	         );
	
	/* TODO: Looks like in addition to yTags, there was supposed to be a
	         scanXOffset property that was never put into use.  
				Add that if you can. */
	
	PktDesc_addPlane(pPd, pPlane);
	
	if(!pOpts->bSilent)  fprintf(stderr,"done\n  DasIO_writePktDesc...");
	DasIO_writePktDesc(pOut, pPd);
	if(!pOpts->bSilent)  fprintf(stderr,"done\n");
	
	++g_nPktType;
	return pPd;	  		
}

/* ************************************************************************* */

int read_b0_packet(FILE* pIn, unsigned char *p)
{
char sHdr[32];
unsigned long nLen;
size_t nRead;

  if((nRead=fread(sHdr,sizeof(char),8,pIn))!=8)
    return 0;
  sHdr[8]='\0';
  nLen=strtoul(sHdr+4,NULL,16);
  sHdr[4]='\0';
  if(strcmp(sHdr,":b0:")){
    fprintf(stderr,"not a das1 b0 packet, %s.\n",sHdr);
    return 0;
  }  
  if(nLen>65532){
    fprintf(stderr,"bad length for das1 b0 packet, len=%08lX\n",nLen);
    return 0;
  }  
  if((nRead=fread(p,sizeof(char),nLen,pIn))!=nLen)
    return 0;
  
	/* All das1 streams are big endian, swap the buffer after the packet
		header if on a little endian computer */
	if(nLen % 4 != 0){
		fprintf(stderr, "das1 b0 packet length, %lu, is not a multiple of 4", 
				  nLen);
		return 0;
	}
	
	swapBufIfHostLE(p, 4, nLen / 4);
	
return nLen;
}


/** Product and array of indicies that place another array's contents in
 * order from min to max value. 
 *
 * @param ar - Input array
 * @param nLen - length of both input array and sort array
 * @param sort - an array of indicies
 */
void SwapSort_Min(const float* ar,unsigned int nLen,unsigned int *sort)
{
int i;
unsigned long nMin,nHead,nPasses,nTmp;

  for(i=0;i<nLen;i++)
    sort[i]=i;

  nPasses=nLen;
  for(nHead=0;nHead<nPasses;nHead++){
    nMin=nHead;
    for(i=nHead+1;i<nLen;i++){
      if(ar[sort[i]]<ar[sort[nMin]])
        nMin=i;
    }
    nTmp=sort[nHead];
    sort[nHead]=sort[nMin];
    sort[nMin]=nTmp;
  }/* for nPasses */

return;
}

/* ************************************************************************* */

#define MAX_B0_SIZE (64*1024)
#define MAX_ITEMS   (64*1024)

static unsigned int buf[MAX_B0_SIZE/4];
static unsigned int arSort[MAX_ITEMS];

static float arTime[MAX_ITEMS], arFreq[MAX_ITEMS], arAmpl[MAX_ITEMS];

int main(int argc,char *argv[])
{
	char* sProgName = argv[0];
		
	Options opts = {false, ST_FLOAT, EPOCH_1958, 128.0, 0.0,
	                UNIT_HERTZ, UNIT_E_SPECDENS, -1E31, -1.0};
				
	/* Exit on errors, log info messages and above */
	das_init(argv[0], DASERR_DIS_EXIT, 0, DASLOG_INFO, NULL);
	/* double arDas2Offset[MAX_ITEMS]; */ /* das2 interface for offset is doubles only */
		  
	fprintf(stderr,"%s\n",sVersion);
	
	getCmdOpts(argc, argv, &opts);
	
	int i = 0;
	/* for(i=0;i<MAX_ITEMS;i++)
	  arDas2Offset[i]=0.0; */ /* these are currently zeroed in the das2 stream */
 
 
	if(!opts.bSilent)  fprintf(stderr,"Create Stream...");
	DasIO* pOut = new_DasIO_cfile(sProgName, stdout, "w");
	StreamDesc* pSd = new_StreamDesc();
	
	if(opts.dInterpWidth!=0.0)
	  DasDesc_setDatum((DasDesc*)pSd, "xTagWidth",opts.dInterpWidth,UNIT_SECONDS);

	if(opts.dInterpHeight!=0.0)
	  DasDesc_setDatum((DasDesc*)pSd, "yTagWidth", opts.dInterpHeight, UNIT_HERTZ);

	DasDesc_setDouble((DasDesc*)pSd,"zFill", opts.dFillValue);
	
	if(!opts.bSilent)  fprintf(stderr,"done,  Send Stream Header...");
	DasIO_writeStreamDesc(pOut, pSd);
	if(!opts.bSilent)  fprintf(stderr,"done\n\n");
	
	int nNumPkt = 0;
	/* int nLastPkId = -1; */
	unsigned long nRecLen,nItems;
	float *pF = NULL;
	double dDas2Time = 0.0;
	
	/* das2 interface for freq is doubles only */
	double arDas2Freq[MAX_ITEMS] = {0.0};
	
	while((nRecLen=read_b0_packet(stdin, (unsigned char*)buf))!=0){
		nItems=nRecLen/(12);
		pF=(float*)buf;
		for(i=0;i<nItems;i++){    
			arTime[i]=*(pF++);  /* C Increment operator notes:               */
			arFreq[i]=*(pF++);  /*   p++ (as suffix) returns old value, but  */
			arAmpl[i]=*(pF++);  /*   ++p (as prefix) returns new value       */
		}
	  
		/* sort by frequencies to enable das2 streams to work */
		SwapSort_Min(arFreq,nItems,arSort);
		for(i=0;i<nItems;i++){
			
			/* TODO: Check to see if time offsets are not all the same, if not
			         send as X-Y-Z stream */
			/* arDas2Offset[i]=arTime[arSort[i]]; currently zeroed in das2 stream */
			
			arDas2Freq[i]=arFreq[arSort[i]];
		}
	  
		/* Only sends if freq. table changes */ 
		PktDesc* pPd = sendPktDesc(&opts,pOut,pSd,"amplitude",arDas2Freq,nItems);
		
		/* if(!opts.bSilent && (PktDesc_getId(pPd) != nLastPkId)){
			fprintf(stderr,"packet type=%d, pkt cnt=%d\n", 
					  PktDesc_getId(pPd), nNumPkt);
			nLastPkId = PktDesc_getId(pPd);
		}
		*/
		
		PlaneDesc* pX = PktDesc_getXPlane(pPd);
	  
		dDas2Time=opts.dBaseTime + arTime[0]/(24.0*60.0*60.0);
		if(opts.nTimeFormat != EPOCH_1958){ 
			dDas2Time-=15340;
			dDas2Time*=(24.0*60.0*60.0*1E6);  /* uSec from 2000-001 */
		}
		PlaneDesc_setValue(pX, 0, dDas2Time);
		
		PlaneDesc* pYScan = PktDesc_getPlaneByName(pPd, "amplitude");
	  
		for(i=0;i<nItems;i++){
			if(arAmpl[arSort[i]] < 1.0E-25)
				PlaneDesc_setValue(pYScan, i, opts.dFillValue);
			else
				PlaneDesc_setValue(pYScan, i, arAmpl[arSort[i]]);
		}
			
	  	DasIO_writePktData(pOut, pPd);
	  
		++nNumPkt;
	}

	if(!opts.bSilent)  fprintf(stderr,"closing StreamDescriptor()...");
	DasIO_close(pOut);
	if(!opts.bSilent)  fprintf(stderr,"done\n");

	if(!opts.bSilent)  fprintf(stderr,"%6d packets, %3d types\n",nNumPkt,g_nPktType);

	return 0;
}
