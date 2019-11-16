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
#include <assert.h>
#include <math.h>
#include <limits.h>

#ifdef _WIN32
#define strcasecmp _stricmp
#else
#include <strings.h>
#endif

#include <das2/core.h>

#include "via.h"

#define P_ERR 100

#define MAX_NUM_BINS 0x10000000  /* 4 * 128 MB of bins , so a half a GB */


/* ************************************************************************** */
/* Structure to hold the application state as data flow by */

typedef struct user_data_t{
	double rStartMicroSec;   /* The starting bin position */
	double rBinSzMicroSec;   /* The size of each bin */
	Via* pVia;               /* Virtual index array to buffer output data */
	
	const char* sTitle;
	const char* sYlabel;
	DasIO* pIoOut;           /* The output writer */
	StreamDesc* pSdOut;      /* The output stream descriptor */
	bool bMonotonic;         /* true if data are assumed monotonic */
	bool bEvents;            /* true if output is just yes/no events */
	const char* sVolUnits;   /* non-null if output is data volume */
	int nVolUnitDiv;
} UserData;

/* ************************************************************************* */
/* Utility functions for setting the units and title for the output stream 
 * and packet descriptors.  This is a PITA but accurate labels are important  
 */

void getIntervalStr(double rBinSzMicroSec, char* sBinSz, size_t uLen)
{
	
	const char* sUnits = NULL;
	const char* sPlural = NULL;
	double rNumOfUnits = 0.0;
	
	assert(uLen > 6);
	assert(rBinSzMicroSec > 0.0);
	
	if(rBinSzMicroSec < 1.0){
		sUnits = "ns";
		rNumOfUnits = rBinSzMicroSec * 1.0e3;
		goto FINISH;
	}
	if(rBinSzMicroSec < 1.0e3){
		sUnits = "μs";
		rNumOfUnits = rBinSzMicroSec;
		goto FINISH;
	}
	if(rBinSzMicroSec < 1.0e6){
		sUnits = "ms";
		rNumOfUnits = rBinSzMicroSec / 1.0e3;
		goto FINISH;
	}
	if(rBinSzMicroSec < 60*1.0e6){
		sUnits = "s";
		rNumOfUnits = rBinSzMicroSec / 1.0e6;
		goto FINISH;
	}
	if(rBinSzMicroSec < 3600*1.0e6){
		sUnits = "minute";
		sPlural = "minutes";
		rNumOfUnits = rBinSzMicroSec / (60*1.0e6);
		goto FINISH;
	}
	if(rBinSzMicroSec < 86400*1.0e6){
		sUnits = "hour";
		sPlural = "hours";
		rNumOfUnits = rBinSzMicroSec / (3600*1.0e6);
		goto FINISH;
	}
	
	sUnits = "day";
	sPlural = "days";
	rNumOfUnits = rBinSzMicroSec / (86400*1.0e6);
		
	FINISH:
	/* maybe contriversial but handles the vast majority of time cases,
	   If we are within 1 part in 1000 of an even unit, just use that */
	if( fabs(1.0 - rNumOfUnits) < 1.0e-3){
		strncpy(sBinSz, sUnits, uLen-1);
	}
	else{
		snprintf(sBinSz, uLen-1, "%.3f", rNumOfUnits);
		/* Trim unneeded accuracy */
		for(int i = strlen(sBinSz) - 1; i > 0; --i){
			if((sBinSz[i] == '0')||(sBinSz[i] == '.'))
				sBinSz[i] = '\0';
			else
				break;
		}
		
		strncat(sBinSz, " ", uLen - (strlen(sBinSz) + 1));
		if(sPlural != NULL)
			strncat(sBinSz, sUnits, uLen - (strlen(sBinSz) + 2));
		else
			strncat(sBinSz, sPlural, uLen - (strlen(sBinSz) + 2));
	}
}

char* getYLabel(const UserData* pState, char* sOutLbl, size_t uLen){
	char sBinSz[64] = {'\0'};
	
	if(pState->sYlabel != NULL){
		strncpy(sOutLbl, pState->sYlabel, uLen-1);
		return sOutLbl;
	}
	if(pState->bEvents){
		strncpy(sOutLbl, "Coverage", uLen-1);
		return sOutLbl;
	}
	
	getIntervalStr(pState->rBinSzMicroSec, sBinSz, 64);
	
	if(pState->sVolUnits != NULL){
		snprintf(sOutLbl, uLen-1, "Stream Volume (%s/%s)", pState->sVolUnits, sBinSz);
		return sOutLbl;
	}
	/* Must be packets */
	snprintf(sOutLbl, uLen-1, "Stream Volume (packets/%s)", sBinSz);
	return sOutLbl;
}

das_units getYUnits(const UserData* pState)
{
	char sBinSz[64] = {'\0'};
	char sUnits[64] = {'\0'};
	
	if(pState->bEvents) return UNIT_DIMENSIONLESS;
	
	getIntervalStr(pState->rBinSzMicroSec, sBinSz, 64);
	
	if(pState->sVolUnits != NULL)
		snprintf(sUnits, 63, "%s %s**-1", pState->sVolUnits, sBinSz);
	else
		snprintf(sUnits, 63, "packets %s**-1", sBinSz);
	
	return Units_fromStr(sUnits);
}

/* ************************************************************************* */
/* Stream Header Processing  - Output my header as soon as I see an input
 * header.  It makes the downstream processors not time out. */

const int g_szRmProps = 14;
const char* g_pRmProps[] = {
	"yFill","yLabel","yRange","yScaleType","ySummary","yValidMin","yValidMax",
	"zFill","zLabel","zRange","zScaleType","zSummary","zValidMin","zValidMax"
};

DasErrCode onStreamHdr(StreamDesc* pSdIn, void* vpState)
{
	UserData* pState = (UserData*)vpState;
	DasDesc* pIn = NULL;
	DasDesc* pOut = NULL;
	char sYlabel[64] = {'\0'};
	int i = 0;
	double rSec = 0.0;
	PktDesc* pPkt = NULL;
	PlaneDesc* pPlane = NULL;
	
	/* Setup output stream descriptor handle enconternig multiple stream descriptors
	   in the input */
	if(pState->pSdOut != NULL) return 0;
		
	pState->pSdOut = StreamDesc_copy(pSdIn); /* Start by coping all the properties */

	pOut = (DasDesc*) pState->pSdOut;
	pIn = (DasDesc*)pSdIn;

	/* Remove known Y and Z properties if present */
	for(i = 0; i < g_szRmProps; ++i) DasDesc_remove(pOut, g_pRmProps[i]);
		
	/* Override the title */
	if(pState->sTitle != NULL){
		DasDesc_setStr(pOut, "title", pState->sTitle);
	}
	else{
		if(DasDesc_has(pIn, "title"))
			DasDesc_vSetStr(pOut, "title", "%s (coverage)", 
					           DasDesc_getStr(pIn, "title"));
		else
			DasDesc_setStr(pOut, "title", "Data Coverage");
	}
	
	/* Override the yLabel */
	DasDesc_setStr(pOut, "yLabel", getYLabel(pState, sYlabel, 64));
		
	/* Override the xTagWidth and xCacheResolution */
	rSec = pState->rBinSzMicroSec / 1.0e6;
	DasDesc_setDatum(pOut, "xTagWidth", rSec, UNIT_SECONDS);
	DasDesc_setDatum(pOut, "xCacheResolution", rSec, UNIT_SECONDS);
	
	/* Override the renderer selection */
	DasDesc_setStr(pOut, "renderer", "stairSteps");
	
	int nRet = 0;
	if( (nRet = DasIO_writeStreamDesc(pState->pIoOut, pState->pSdOut)) != 0) 
		return nRet;
	
	/* Go ahead and write our packet descriptor now since we know what the
	   output will look like */
	pPkt = StreamDesc_createPktDesc(pState->pSdOut, 
	                                new_DasEncoding(DAS2DT_HOST_REAL, 8, NULL),
	                                UNIT_US2000);
	assert(PktDesc_getId(pPkt) == 1);
	
	const char* sName = pState->bEvents ? "coverage" : "rate";
	
	das_units yUnits = getYUnits(pState);
	pPlane = new_PlaneDesc(Y, sName, new_DasEncoding(DAS2DT_HOST_REAL, 4, NULL), 
			                 yUnits);
	
	PktDesc_addPlane(pPkt, pPlane);
	
	return DasIO_writePktDesc(pState->pIoOut, pPkt);
}

/* ************************************************************************* */
/* Data processing 
 *
 * All I care about for input is counting incoming packets and maybe thier 
 * number of bytes.  
 * 
 * Output is a more complicated story.  If input data are monotonic I can emit
 * the last bin as soon as the new items position exceeds the area of the
 * current bin, otherwise I have to calculate which bin to store value in, make
 * sure there is memory for that bin, yada, yada.
 *
 * Needless to say it's much nicer if the --monotonic flag can be set for a 
 * dataset.
 */
 
DasErrCode onPktData(PktDesc* pPdIn, void* vpState)
{
	UserData* pState = (UserData*)vpState;
	PlaneDesc* pXIn = PktDesc_getXPlane(pPdIn);
	Via* pVia = pState->pVia;
	
	double rCurTime = PlaneDesc_getValue(pXIn, 0);
	rCurTime = Units_convertTo(UNIT_US2000, rCurTime, PlaneDesc_getUnits(pXIn));
	
	/* Initialize location of bin 0 if not already set */
	if(pState->rStartMicroSec == DAS_FILL_VALUE)
		pState->rStartMicroSec = rCurTime;
	
	/* Note: If data are out of order this can be negative! */
	long ilCurBin = (rCurTime - pState->rStartMicroSec)/pState->rBinSzMicroSec;
	
	if(labs(ilCurBin) > INT_MAX)
		return das_error(P_ERR, "Bin size too small, or bin0 too far away from "
		                  "this data value.  The bin position exceeds maximum "
				            "value for an integer on this system");
	int iCurBin = ilCurBin;
	
	double rAdd = 1.0;
	if(pState->sVolUnits) 
		rAdd = ((double)PktDesc_recBytes(pPdIn))/pState->nVolUnitDiv;
	
	/* Non-Monotonic */
	if(!pState->bMonotonic) 
		return Via_add(pVia, iCurBin, rAdd) ? 0 : P_ERR;
	
	/* Monotonic:  Current Bin == Only bin */
	if(Via_length(pVia) == 0)
		return Via_set(pVia, iCurBin, rAdd) ? 0 : P_ERR;
	     
	/* Monotonic:  Current Bin == Last Bin */	
	if(iCurBin == Via_lastSet(pVia))
		return Via_add(pVia, iCurBin, rAdd) ? 0 : P_ERR;
	
	/* Monotonic: Current Bin < Last Bin */
	if(iCurBin < Via_lastSet(pVia))
		return das_error(P_ERR, "Time revision detected in supposedly "
					            "monotonic data");
	
	/* Monotonic: Current Bin > Last Bin */
	int iLastBin = Via_lastSet(pVia);
	
	PktDesc* pPdOut = StreamDesc_getPktDesc(pState->pSdOut, 1);
	PlaneDesc* pXOut = PktDesc_getXPlane(pPdOut);
	PlaneDesc* pYOut = PktDesc_getPlane(pPdOut, 1);
		
	rCurTime = pState->rStartMicroSec  + pState->rBinSzMicroSec*(iLastBin + 0.5);
	PlaneDesc_setValue(pXOut, 0, rCurTime);
	
	if(pState->bEvents)
		PlaneDesc_setValue(pYOut, 0, 1.0);
	else
		PlaneDesc_setValue(pYOut, 0, Via_get(pVia, iLastBin));
	
	/* Save start of new data first ... */
	Via_clear(pVia);
	if(! Via_set(pVia, iCurBin, rAdd) ) return P_ERR;
						
	return DasIO_writePktData(pState->pIoOut, pPdOut);
}


/* ************************************************************************* */
/* Stream Close Handling */
DasErrCode onClose(StreamDesc* pSdIn, void* vpState)
{
	UserData* pState = (UserData*)vpState;
	DasIO*    pIoOut = pState->pIoOut;
	PktDesc* pPdOut = StreamDesc_getPktDesc(pState->pSdOut, 1);
	PlaneDesc* pXOut = PktDesc_getXPlane(pPdOut);
	PlaneDesc* pYOut = PktDesc_getPlane(pPdOut, 1);
	DasErrCode nRet = 0;
	double rTime = 0.0, rVal = 0.0;
	Via* pVia = pState->pVia;
	
	/* If we are in monotonic mode just output the last point, otherwise
	   dump everything */
	if(pState->bMonotonic){
		if(Via_length(pState->pVia) > 0){
			
			int iLastBin = Via_lastSet(pState->pVia);
			
			rTime = pState->rStartMicroSec + pState->rBinSzMicroSec*(iLastBin+0.5);
			PlaneDesc_setValue(pXOut, 0, rTime);
			
			if(pState->bEvents)
				PlaneDesc_setValue(pYOut, 0, 1.0);
			else
				PlaneDesc_setValue(pYOut, 0, Via_get(pState->pVia, iLastBin));
		}
		return DasIO_writePktData(pState->pIoOut, pPdOut);
	}
	
	
	for(int i = Via_minIndex(pVia); i<= Via_maxIndex(pVia); ++i){
		
		rVal = Via_get(pVia, i);
		
		rTime = pState->rStartMicroSec + pState->rBinSzMicroSec * (i + 0.5);
		PlaneDesc_setValue(pXOut, 0, rTime);
		
		if(pState->bEvents) PlaneDesc_setValue(pYOut, 0, 1.0);
		else                PlaneDesc_setValue(pYOut, 0, rVal);
		
		if( (nRet = DasIO_writePktData(pIoOut, pPdOut)) != 0) return nRet;
	}
	
	return 0;
}

/* ************************************************************************* */

void help(FILE* pOut)
{
	fprintf(pOut, 
"SYNOPSIS\n"
"   das2_bin_ratesec - Caculates packets or bytes per X bin for das2 streams\n"
"\n"
"USAGE\n"
"   das2_bin_ratesec [options] BIN_SECONDS\n"
"\n"
"DESCRIPTION\n"
"   das2_bin_ratesec is a classic Unix filter, reading a Das 2 stream on\n"
"   standard input and producing a single data point per X-axis interval.  The\n"
"   output is always in the format <x><y>.  In general, data output is delayed\n"
"   until the input stream is closed so that non-monotonic streams can be \n"
"   analyzed, but see the '--monotonic' option below for pure streaming\n"
"   behavior.\n"
"\n"
"   It is assumed that <x> plane values are time points.  For this reducer\n"
"   only the following <x> unit values are allowed:\n"
"\n"
"      * us2000 - Microseconds since midnight, January 1st 2000\n"
"      * t2000  - Seconds since midnight, January 1st 2000\n"
"      * mj1958 - Days since midnight January 1st 1958\n"
"      * t1970  - Seconds since midnight, January 1st 1970\n"
"\n"
"   All time values, regardless of scale, epoch, or representation in the\n"
"   input stream are handled as 8-byte IEEE floating point numbers internally.\n"
"   ASCII times are converted internally to us2000 values.\n"
"\n"
"   The BIN_SECONDS parameter provides the number of seconds over which to \n"
"   count packets.\n"
"\n"
"OPTIONS\n"
"   -h,--help         Show this help text\n"
"\n"
"   -m,--monotonic    Assume the input stream monotonically increases with\n"
"                     the <x> parameter.  This option be used when packet\n"
"                     types are re-defined and when there is more than one\n"
"                     packet type in the stream as long as there are no time\n"
"                     reversions in the stream.\n"
"\n"
"   -v UNITS,--volume=UNITS\n"
"                     Count bytes per interval instead of packets.  The output\n"
"                     will be measured in UNITS/interval.  Where UNITS is one\n"
"                     of 'bytes', 'kB', 'MB', 'GB'.  This is useful for\n"
"                     generating coverage datasets.\n"
"\n"
"   -e,--events       Just output the constant value 1.0 for intervals that\n"
"                     have data.\n"
"\n"
"   -b BEGIN, --begin=BEGIN\n"
"                     Instead of starting the 0th bin at the first time value\n"
"                     received, specify a starting bin.  This useful when\n"
"                     creating pre-generated coverage datasets because as it\n"
"                     keeps the bin boundaries predictable\n"
"\n"
"   -t,--title        Change the title for output dataset\n"
"\n"
"   -y,--ylabel       Change the y-label for the output dataset\n"
"\n"
"AUTHOR\n"
"   chris-piker@uiowa.edu\n"
"\n"
"SEE ALSO\n"
"   das2_bin_avgsec, das2_bin_peakavgsec, das2_ascii\n"
"\n"
"   The das 2 ICD @ http://das2.org for an introduction to the das 2 system.\n"
"\n");
}

/* ************************************************************************** */

int main(int argc, char* argv[])
{
	
	UserData ud = {
		DAS_FILL_VALUE, /* Bin Start */
		0.0,        /* bin Size microsec */
		NULL,       /* Virtual index array */
		NULL,       /* override title */
		NULL,       /* override yLabel */
		NULL,       /* DasIO out */
		NULL,       /* Das Stream Desc Out */
		false,      /* monotonic */
		false,      /* out units are bytes */
		false       /* out units are 0 and 1 for presents or absense of data */
	};
	
	double rBinSize = 0.0;
	
	/* Exit on errors, log info messages and above */
	das_init(argv[0], DASERR_DIS_EXIT, 0, DASLOG_INFO, NULL);
	
	das_time dt = {0};
	
	if(argc < 2){
		fprintf(stderr, "Usage das2_bin_avgsec BIN_SIZE_SECS\n\nIssue -h"
              " to output the help page.\n");
		return 4;
	}
	  
	if((strcmp(argv[1], "-h") == 0) || (strcmp(argv[1], "--help") == 0)){
		help(stdout);
		return 0;
	}
	if(strcmp(argv[1], "-v") == 0 || strcmp(argv[1], "--version") == 0){
		printf("$Header$\n");
		return 0;
	}

	for(int i = 1; i < argc; i++){
		if((strcmp(argv[i], "-b") == 0)||(strcmp(argv[i], "--begin")==0)){
			if(i+1 == argc)
				return das_error(P_ERR, "Begin bin position missing after %s", argv[i]);
			i++;
			if(!dt_parsetime(argv[i], &dt))
				return das_error(P_ERR, "Couldn't convert %s to a date-time", argv[i]);
			ud.rStartMicroSec = Units_convertFromDt(UNIT_US2000, &dt);
			continue;
		}
		if((strcmp(argv[i], "-m")==0)||(strcmp(argv[i], "--monotonic")==0)){
			ud.bMonotonic = true;
			continue;
		}
		if((strcmp(argv[i], "-v")==0) || (strcmp(argv[i], "--vol-units")==0)){
			if(i+1 == argc)
				return das_error(P_ERR, "Volume units missing after %s", argv[i]);
			i++;
			/* Handle odd case entries */
			if(strcasecmp(argv[i], "bytes")==0){
				ud.sVolUnits = "bytes"; ud.nVolUnitDiv = 1; continue;
			}
			if(strcasecmp(argv[i], "kb")==0){
				/* by definition 1 kB = 2^10 bytes */
				ud.sVolUnits = "kB"; ud.nVolUnitDiv = 0x400; continue;
			}
			if(strcasecmp(argv[i], "mb")==0){
				/* by definition 1 MB = 2^20 bytes */
				ud.sVolUnits = "MB"; ud.nVolUnitDiv = 0x100000; continue;
			}
			if(strcasecmp(argv[i], "gb")==0){
				/* by definition 1 GB = 2^30 bytes */
				ud.sVolUnits = "GB"; ud.nVolUnitDiv = 0x40000000; continue;
			}
			
			return das_error(P_ERR, "Unknown volume units '%s', use -h for help",
					            argv[i]);
		}
		if((strcmp(argv[i], "-e")==0) ||(strcmp(argv[i], "--events")==0)){
			ud.bEvents = true;
			continue;
		}
		if((strcmp(argv[i], "-t")==0)||(strcmp(argv[i], "--title")==0)){
			if(i+1 == argc)
				return das_error(P_ERR, "Title missing after %s", argv[i]);
			ud.sTitle = argv[i+1];
			i++;
			continue;
		}
		if((strcmp(argv[i], "-y")==0)||(strcmp(argv[i], "--ylabel")==0)){
			if(i+1 == argc)
				return das_error(P_ERR, "Y Axis label missing after %s", argv[i]);
			ud.sYlabel = argv[i+1];
			i++;
			continue;
		}
		
		if(rBinSize > 0.0)
			return das_error(P_ERR, "Unknown extra command line arguments "
					            "starting at '%s'", argv[i]);
					
		if(sscanf(argv[i], "%lf", &rBinSize) < 1)
			return das_error(P_ERR, "Couldn't convert %s to a positive seconds "
					            "value", argv[i]);
		
		if(rBinSize <= 0.0)
			return das_error(
					P_ERR,"Output bin size must be bigger than 0 seconds!");
	}
	
	if(rBinSize <= 0.0)
		return das_error(P_ERR, "Bin size not provided, use -h for help");

	ud.rBinSzMicroSec = rBinSize * 1.0e6;  /* Convert to microseconds */
	
	
	/* If I'm not going to be sending data as it accumulates then we need 
	 * an output buffer that's big enough to hold all the bins, otherwise
	 * only a 1 item deep buffer is needed.  For the expanding buffer start
	 * with 64K points and double as needed */
	if(ud.bMonotonic)
		ud.pVia = new_Via(1, 1);
	else
		ud.pVia = new_Via(65536, MAX_NUM_BINS);	
	
	ud.pIoOut = new_DasIO_cfile("das2_bin_ratesec", stdout, "w");

	/* Set the global stuff to be the user data pointer */
	StreamHandler* pSh = new_StreamHandler(&ud);
	pSh->streamDescHandler = onStreamHdr;
	pSh->pktDescHandler = NULL;   /* Ignore incomming packet headers */
	pSh->pktDataHandler = onPktData;
	pSh->closeHandler = onClose;
   pSh->commentHandler = NULL;   /* Ignore incomming comments */
	pSh->exceptionHandler = NULL; /* Ignore incomming exceptions */
	
	DasIO* pIn = new_DasIO_cfile("Standard Input", stdin, "r");
	DasIO_addProcessor(pIn, pSh);
	 
	return DasIO_readAll(pIn);
}
