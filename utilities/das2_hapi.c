/* Copyright (C) 2017 Chris Piker <chris-piker@uiowa.edu>
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


/* Read a stream on standard input and output information about the 
   stream in a varity of ways */
	
#define _POSIX_C_SOURCE 200112L

#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <string.h>
#include <locale.h>
#include <limits.h>

#ifndef _WIN32 
#include <unistd.h> 
#endif

#include <das2/core.h>

#define _QDEF(x) #x
#define QDEF(x) _QDEF(x)

#define APP_NORMAL_EARLY_END 100
#define ERR_HAPI_INCOMPAT 101


/* ************************************************************************* */
void prnHelp(FILE* pOut)
{
	fprintf(pOut,
"SYNOPSIS:\n"
"   das2_hapi - Convert a Das2 Stream to various Heliophysics API outputs\n"
"\n"
"USAGE:\n"
"   das2_hapi [-d DSDF] [-i] [-n] [-p ID] [-b BEGIN] [-e MAX] [PARAM1,PARAM2]\n"
"\n"
"DESCRIPTION:\n"
"   das2_hapi is a filter.  It reads a Das2 Stream on standard input and writes\n"
"   a Comma Separated Values stream to standard output.  Since the stream\n"
"   format defined by the Helophysics Application Programming Interface (HAPI)\n"
"   can only represent a single Das2 packet type at a time, the first packet\n"
"   type encountered is output and and the rest are dropped to keep it happy.\n"
"\n"
"   Various options below may be used to change the default behavior.\n"
"\n"
"OPTIONS:\n"
"   -h,--help Print this help text\n"
"\n"
"   -b BEGIN\n"
"           Drop any output values that stamped with date-time values that are\n"
"           less than BEGIN.  Argument which must be a parsable date-time stamp.\n"
"\n"
"   -e TRIM_END\n"
"           Drop any output values that stamped with date-time values that are\n"
"           greater than or equal to END.   Argument which must be a parsable\n"
"           date-time stamp.\n"
"\n"
"   -d DSDF Provide the location of the DSDF that corresponds to the reader that\n"
"           generated the stream.  Though not strictly required, legal headers\n"
"           cannot be created without information in the DSDF.\n"
"\n"
"   -i,--info\n"
"           Output JSON header info before the data stream.  Das2 streams do not\n"
"           contain enough information on thier own to create a conforming\n"
"           header, so by default only the parameters section is output.  If a\n"
"           DSDF file is also give (via the -d option) then a complete header\n"
"           is written.  It is possible to combine this option with -n to \n"
"           output only header information.\n"
"\n"
"   -n,--no-data\n"
"           Don't output data, just the header.\n"
"\n"
"   -p ID   Output packets with ID instead of just the first packet type\n"
"           encountered\n"
"\n"
"   PARAM_LIST\n"
"           The HAPI spec requires output variable sub-setting.  So the final\n"
"           command line parameter is a comma separated list of items to\n"
"           output in the stream.  Since Das2 Streams typically do not label\n"
"           thier <x> plane, the name 'time' is chosen for that variable by\n"
"           default.  Unnamed <y> and <yscan> planes in the input are simply\n"
"           named 'Y_1' through 'Y_n' and 'YSCAN_1' through 'YSCAN_N' respectively.\n"
"\n"
"           The short-hands Y_1, Y_2, YSCAN_1, YSCAN_2 etc. may be used even if\n"
"           the associated plane actually has a name in the stream.\n"
"\n"
"EXAMPLES:\n"
"   Output a full HAPI header for the first packet type encountered for data\n"
"   from wav_reader program:\n"
"\n"
"      wav_reader 2017-001 2017-002 | das2_hapi -i -n -d Survey.dsdf\n"
"\n"
"   Output only the data for packet type 2 as CSV text with no headers and trim\n"
"   any extraneous data outside a time range\n"
"\n"
"      wav_reader 2017-001 2017-002 | das2_hapi -p 02 -b 2017-001 -e 2017-002\n"
"\n"
"   Read Voyager 1 spectrum analyzer data, bin it on 60 second boundaries but\n"
"   output only the peaks as CSV text with no headers.\n"
"\n"
"      vgr1_reader 2016-001 2016-002 | das2_bin_peakavgsec 60 | das2_hapi amplitude.max\n"
"   or\n"
"      vgr1_reader 2016-001 2016-002 | das2_bin_peakavgsec 60 | das2_hapi yscan2\n"
"\n"
"AUTHOR:\n"
"   chris-piker@uiowa.edu\n"
"\n"
"SEE ALSO:\n"
"   das2_csv for an alternate das2 stream to CSV generator as will as the\n"
"   Heliophysics API specification at http://spase-group.org/hapi\n"
"\n"

);

}

/* ************************************************************************* */

/* Processing state struct */
typedef struct proc_state {
	bool bHdrOut;
	bool bDatOut;
	const char* sDsdfFile;
	int nPktId;
	das_time dtTrimBeg;
	das_time dtTrimEnd;
	DasDesc* pDsdf;
	const char* sPlaneList;
	
	DasBuf* pRow;  /* Output Stuff */
	bool bDone;
	bool bAnyOutput;  /* True if program emitted something */
} proc_state_t;

/* ************************************************************************* */
bool outputPlane(proc_state_t* pPs, const char* sName)
{
	if(! pPs->sPlaneList) return true;
	
	/* remember how we ended the parameters list with a comma... */
	char sNeedle[128] = {'\0'};
	strncpy(sNeedle, sName, 126);
	sNeedle[ strlen(sNeedle) ] = ',';
	
	if( strstr(pPs->sPlaneList, sNeedle) ) return true;
	return false;
}

/* ************************************************************************* */
DasErrCode OnPktHdr(StreamDesc* pSdIn, PktDesc* pPdIn, void* vpPs)
{
	proc_state_t* pPs = (proc_state_t*)vpPs;
	
	/* If we already know which packet ID we are going to parse and this
	   one isn't it, just skip it */
	if(pPs->nPktId < 1) 	pPs->nPktId = PktDesc_getId(pPdIn);
	
	if(pPs->nPktId != PktDesc_getId(pPdIn)){ 
		fprintf(stderr, "WARNING: Skipping packets of type %d\n", PktDesc_getId(pPdIn));
		return DAS_OKAY;
	}
	
	/* Add a prefix to the data if it's going to be attached to a dataset */
	const char* sPre = "";
	if(pPs->bDatOut) sPre = "# ";
	
	if(pPs->bHdrOut){
		pPs->bAnyOutput = true;
		printf("%s{ \"HAPI\":\"1.1\",\n"
		       "%s  \"status\":{\"code\":1200, \"message\":\"OK\"},\n"
		       "%s  \"format\":\"csv\",\n", sPre, sPre, sPre);
	}
	
	const char* sDesc = NULL;
	const char* sKey = NULL;
	char sBeg[64] = {'\0'};  das_time dtBegin = {0};
	char sEnd[64] = {'\0'};  das_time dtEnd = {0};
	das_units units = UNIT_DIMENSIONLESS;
	if(pPs->pDsdf && pPs->bHdrOut){
		sDesc = DasDesc_getStr(pPs->pDsdf, "description");
				
		if(DasDesc_getStrRng(pPs->pDsdf, "validRange", sBeg, sEnd, &units, 64) == 0){
			if(dt_parsetime(sBeg, &dtBegin)){
				dt_isoc(sBeg, 64, &dtBegin, 0);
				printf("%s  \"startDate\":\"%s\",\n", sPre, sBeg);
			}
			
			if(dt_parsetime(sEnd, &dtEnd)){
				dt_isoc(sBeg, 64, &dtEnd, 0);
				printf("%s  \"stopDate\":\"%s\",\n", sPre, sEnd);
			}
		}
		
		if(DasDesc_has(pPs->pDsdf, "exampleRange")){
			sKey = "exampleRange";
		}
		else{
			if(DasDesc_has(pPs->pDsdf, "exampleRange_00"))
				sKey = "exampleRange_00";
		}
		if(sKey){
			if(DasDesc_getStrRng(pPs->pDsdf, sKey, sBeg, sEnd, &units, 64) == 0){
				if(dt_parsetime(sBeg, &dtBegin)){
					dt_isoc(sBeg, 64, &dtBegin, 0);
					printf("%s  \"sampleStartDate\":\"%s\",\n", sPre, sBeg);
				}
			
				if(dt_parsetime(sEnd, &dtEnd)){
					dt_isoc(sBeg, 64, &dtEnd, 0);
					printf("%s  \"sampleStopDate\":\"%s\",\n", sPre, sEnd);
				}
			}
		}
		if(DasDesc_has(pPs->pDsdf, "techContact"))
			printf("%s  \"contact\":\"%s\",\n",
			       sPre, DasDesc_getStr(pPs->pDsdf, "techContact"));
	}
	
	/* Get the description out of the stream itself if not in the dsdf */
	if( sDesc == NULL)
		sDesc = DasDesc_getStr((DasDesc*)pSdIn, "title");
	
	if(pPs->bHdrOut && (sDesc != NULL))
		printf("%s  \"description\":\"%s\",\n", sPre, sDesc);
	
	if(pPs->bHdrOut) printf("%s  \"parameters\":[\n", sPre);
	
	PlaneDesc* pPlane = NULL;
	const char* sName = "";
	char sNameBuf[64] = {'\0'};
	int nYs = 0;
	int nYScans = 0;
	size_t uItems = 0;
	const char* sUnits = NULL;
	const char* sYUnits = NULL;
	const double* pTags = NULL;
	size_t uRowBufLen = 2;   /* \r\n at end */
	DasEncoding* pEncoder = NULL;
	int nAsciiOutWidth = 13;
	
	for(size_t u = 0; u < PktDesc_getNPlanes(pPdIn); ++u){
		pPlane = PktDesc_getPlane(pPdIn, u);
		units = PlaneDesc_getUnits(pPlane);
		sName = PlaneDesc_getName(pPlane);
		pEncoder = PlaneDesc_getValEncoder(pPlane);
		uItems = PlaneDesc_getNItems(pPlane);
		
		switch(PlaneDesc_getType(pPlane)){
		
		case PT_X:
			if(! Units_haveCalRep(units)){
				fprintf(stderr, "ERROR: <x> plane data is not convertable to UTC\n");
				return ERR_HAPI_INCOMPAT;
			}
			if(!sName || (strlen(sName) == 0)) sName = "time";
			
			/* Wow, using FILL with CSV, for the love of Geebus why? */
			/* Not sending binary:  "%s     \"length\":24 */
			
			if(pPs->bHdrOut)
				printf("%s    {\"name\":\"%s\",\n"
				       "%s     \"type\":\"isotime\",\n"
				       "%s     \"units\":\"UTC\",\n"
				       "%s     \"fill\":null}\n"  
						, sPre, sName, sPre, sPre, sPre);
			
			/* Watch out for the TIME22 types.  These ususally indicate a day-of-year
			   input encoding which has been LOST(!) before the data have been read */
			if((pEncoder->nCat != DAS2DT_TIME)||(pEncoder->nWidth == 22)){
				/* This is tough, must determine encoding.  Since it would be silly to
				   use the HAPI protocol for high-density Radio Astronomy data lets 
					just default to milliseconds for now. */
				/*fprintf(stderr, "   das2_hapi: Making 'time24' encoder for plane %s\n", 
						 sName);*/
				pEncoder = new_DasEncoding(DAS2DT_TIME, 24, NULL);
				pPlane->pUser = (void*)pEncoder;
			}
			uRowBufLen += pEncoder->nWidth +2;
			break;
		
		case PT_Y:
			++nYs;			
			
			if(strlen(sName) == 0){
				snprintf(sNameBuf, 63, "Y_%d", nYs);
				sName = sNameBuf;
			}
			if(! outputPlane(pPs, sName)) break;
			
			if(pPs->bHdrOut){
				printf("%s    ,{\"name\":\"%s\",\n"
				       "%s     \"type\":\"double\",\n", sPre, sName, sPre);		
				printf("%s     \"fill\":null,\n", sPre);
			
				if(DasDesc_has((DasDesc*)pPlane, "yLabel"))
					printf("%s     \"description\":\"%s\",\n", sPre,
							DasDesc_getStr((DasDesc*)pPlane, "yLabel"));
			
				sUnits = "null";
				if(PlaneDesc_getUnits(pPlane) != UNIT_DIMENSIONLESS)
					sUnits = Units_toStr(PlaneDesc_getUnits(pPlane));
			
				printf("%s     \"units\":\"%s\"}\n", sPre, sUnits);
			}
			
			if(pEncoder->nCat != DAS2DT_ASCII){
				/* Default to ascii13 for 4-byte floats ascii17 for 8-byte floats */
				if(pEncoder->nWidth == 4) nAsciiOutWidth = 13;
				else nAsciiOutWidth = 17;
				
				/* fprintf(stderr, "   das2_hapi: Making 'ascii%d' encoder for plane %s\n",
						  nAsciiOutWidth, sName);*/
				pEncoder = new_DasEncoding(DAS2DT_ASCII, nAsciiOutWidth, NULL);
				pPlane->pUser = (void*)pEncoder;
			}
			uRowBufLen += pEncoder->nWidth +2;
			break;
			
		case PT_YScan:
			++nYScans;

			if(strlen(sName) == 0){
				snprintf(sNameBuf, 63, "YSCAN_%d", nYs);
				sName = sNameBuf;
			}
			if(! outputPlane(pPs, sName)) break;
			
			if(pPs->bHdrOut){
				printf("%s    ,{\"name\":\"%s\",\n"
				       "%s     \"type\":\"double\",\n", sPre, sName, sPre);
						
				printf("%s     \"fill\":null,\n", sPre);
			
				if(DasDesc_has((DasDesc*)pPlane, "zLabel"))
					printf("%s     \"description\":\"%s\",\n", sPre,
							DasDesc_getStr((DasDesc*)pPlane, "zLabel"));
			
				sUnits = "null";
				if(PlaneDesc_getUnits(pPlane) != UNIT_DIMENSIONLESS)
					sUnits = Units_toStr(PlaneDesc_getUnits(pPlane));
			
				sYUnits = "null";
				if(PlaneDesc_getOffsetUnits(pPlane) != UNIT_DIMENSIONLESS)
					sYUnits = Units_toStr(PlaneDesc_getOffsetUnits(pPlane));
								
				printf("%s     \"units\":\"%s\",\n", sPre, sUnits);
			
				/* Now to deal with the frequencies */
				printf("%s     \"size\":[%zu],\n", sPre, uItems);
				
				printf("%s     \"bins\":[{\n", sPre);
				printf("%s       \"name\":\"yTags\",\n", sPre);
				printf("%s       \"units\":\"%s\",\n", sPre, sYUnits);
				
				if(DasDesc_has((DasDesc*)pPlane, "yLabel"))
					printf("%s     \"description\":\"%s\",\n", sPre,
							DasDesc_getStr((DasDesc*)pPlane, "yLabel"));
				
				printf("%s       \"centers\":[", sPre);
			
				pTags = PlaneDesc_getOrMakeOffsets(pPlane);
				for(size_t v = 0; v < uItems; ++v){
					if(v > 0) putchar(',');
					printf("%.4e", pTags[v]);
				}
				printf("]\n%s     }]\n    }\n", sPre);
			}
			
			
			if(pEncoder->nCat != DAS2DT_ASCII){
				if(pEncoder->nWidth == 4) nAsciiOutWidth = 13;
				else nAsciiOutWidth = 17;
				
				/* fprintf(stderr, "   das2_hapi: Making 'ascii%d' encoder for plane %s\n",
						  nAsciiOutWidth, sName); */
				pEncoder = new_DasEncoding(DAS2DT_ASCII, nAsciiOutWidth, NULL);
				pPlane->pUser = (void*)pEncoder;

			}
			uRowBufLen += (pEncoder->nWidth +2) * uItems;
			break;
		
		default:
			break;
		}
	}
			
	if(pPs->bHdrOut) printf("%s  ]\n%s}\n", sPre, sPre);
	
	/* If we are only sending headers then we are done now, close standard
	   input to prevent a potentially lengthy reader operation */
	if(! pPs->bDatOut){ 
		pPs->bDone = true;
		return APP_NORMAL_EARLY_END;
	}
	
	/* Setup buffer for output */
	if(pPs->pRow != NULL) del_DasBuf(pPs->pRow);
	pPs->pRow = new_DasBuf(uRowBufLen);
	
	return DAS_OKAY;
}

/* ************************************************************************* */
DasErrCode onPktData(PktDesc* pPdIn, void* vpPs)
{
	proc_state_t* pPs = (proc_state_t*)vpPs;
	
	if(! pPs->bDatOut) return DAS_OKAY;
	if(pPs->bDone) return DAS_OKAY;
			
	/* Skip this if it's not the packet we are looking for */
	if(pPs->nPktId != PktDesc_getId(pPdIn) ) return DAS_OKAY;
	
	DasBuf_reinit(pPs->pRow);
	
	/* Convert it to CSV data */
	const double* pVals = NULL;
	PlaneDesc* pPlane = NULL;
	const char* sName;
	int nYs = 0;
	int nYScans = 0;
	char sNameBuf[64] = {'\0'};
	

	das_time dt = {0};
	size_t uItems = 0;
	DasEncoding* pEncoder = NULL;
	das_units units;
	
	bool bWrite = true;
	for(size_t u = 0; (u < PktDesc_getNPlanes(pPdIn)) && bWrite; ++u){
		pPlane = PktDesc_getPlane(pPdIn, u);
		
		units = PlaneDesc_getUnits(pPlane);
		sName = PlaneDesc_getName(pPlane);
		pVals = PlaneDesc_getValues(pPlane);
		
		/* If the output encoder is different from the input encoder then we
		   should have saved a pointer to it in the onPktHdr function above */
		if(pPlane->pUser) 
			pEncoder = (DasEncoding*)(pPlane->pUser);
		else
			pEncoder = PlaneDesc_getValEncoder(pPlane);
		
		switch(PlaneDesc_getType(pPlane)){
			
		case PT_X:
			Units_convertToDt(&dt, pVals[0], units);
			
			/* Monotonic Assumption: If we are at or after the end, just be */
			/* done with all output */
			if((pPs->dtTrimEnd.year != 0) && (dt_compare(&dt, &(pPs->dtTrimEnd)) >= 0)){
				pPs->bDone = true;
				bWrite = false;
				break;
			}
			
			/* If to early, don't output this packet */
			if((pPs->dtTrimBeg.year != 0) && (dt_compare(&dt, &(pPs->dtTrimBeg)) < 0)){
				bWrite = false;
				break;
			}
			
			/* Okay, looks to be in range, print it */
			DasEnc_write(pEncoder, pPs->pRow, pVals[0], units);
			break;
		
		case PT_Y:
			++nYs;
			if(strlen(sName) == 0){
				snprintf(sNameBuf, 63, "Y_%d", nYs);
				sName = sNameBuf;
			}
			if(outputPlane(pPs, sName)){
				DasBuf_puts(pPs->pRow, ",");
				
				if(! PlaneDesc_isFill(pPlane, pVals[0]))
					DasEnc_write(pEncoder, pPs->pRow, pVals[0], units);
				else
					DasBuf_puts(pPs->pRow, "NaN");
			}
			break;
		
		case PT_YScan:
			++nYScans;
			if(strlen(sName) == 0){
				snprintf(sNameBuf, 63, "YSCAN_%d", nYScans);
				sName = sNameBuf;
			}
			if(outputPlane(pPs, sName)){
				uItems = PlaneDesc_getNItems(pPlane);
				for(u = 0; u < uItems; ++u){
					DasBuf_puts(pPs->pRow, ",");
	
					if(! PlaneDesc_isFill(pPlane, pVals[u]))
						DasEnc_write(pEncoder, pPs->pRow, pVals[u], units);
					else
						DasBuf_puts(pPs->pRow, "NaN");
				}
			}
			break;
		default:
			break;
		}
	}
	
	if(bWrite){
		DasBuf_puts(pPs->pRow, "\r\n");
		if( fputs(pPs->pRow->sBuf, stdout) == EOF){
			fprintf(stderr, "   ERROR: das2_hapi can't write to standard output\n");
			exit(101);
		}
		pPs->bAnyOutput = true;
		fflush(stdout);
	}
	
	return DAS_OKAY;
}
			
/* ************************************************************************* */
DasErrCode onException(OobExcept* pExcept, void* vpPs)
{
	/* Can't do much here but quit with log message */
	fprintf(stderr, "Stream Exception: %s, %s\n", pExcept->sType, pExcept->sMsg);
	
	return DASERR_OOB;	
}

/* ************************************************************************* */
/* Close Output */
DasErrCode onClose( StreamDesc* sd, void* vpPs)
{
	
	/* TODO: STRIP this out use a count of zero bytes in the h_api/info.py 
	         handler */
	
	/* If nothing has been output send the good-ole no data in range message */
	proc_state_t* pPs = (proc_state_t*)vpPs;
	
	if(!pPs->bAnyOutput && !pPs->bDatOut){
	
		/* if(pPs->bDatOut){
			printf("{ \"HAPI\":\"1.1\",\n"
			    "  \"status\":{\"code\":1201, \"message\":\"OK - no data for time range\"}\n"
			    "}\n");
		}
		else{ */
			printf(
				"{ \"HAPI\":\"1.1\",\n"
			   "  \"status\":{\n"
            "    \"code\":1501,\n"
				"    \"message\":\"Internal server error - upstream request error\"\n"
				"    \"x_reason\":\"No packet headers encounteder in input Das2 stream\"\n"
				"  }\n"
				"}\n");
		/*}*/
	}
	
	return 0;
}


/*****************************************************************************/
/* Main */

void initProcState(proc_state_t* pPs){
	pPs->bHdrOut = false;
	pPs->bDatOut = true;
	pPs->sDsdfFile = NULL;
	pPs->nPktId = -1;
	dt_null( &(pPs->dtTrimBeg));
	dt_null( &(pPs->dtTrimEnd));
	pPs->pDsdf = NULL;
	pPs->sPlaneList = NULL;
	
	pPs->bDone = false;
	pPs->bAnyOutput = false;
	pPs->pRow = NULL;
}

int main( int argc, char *argv[]) {
		
	/* Exit on errors, log info messages and above */
	das_init(argv[0], DASERR_DIS_EXIT, 0, DASLOG_INFO, NULL);
	
	proc_state_t ps;
	DasBuf* pParamBuf = NULL;
	
	das_exit_on_error();  /* Make sure internal library problems cause an 
								   * exit to the shell with a non-zero value */
	
	initProcState(&ps);
	
	for(int i = 1; i < argc; ++i){
		if(strcmp(argv[i], "-h") == 0 || strcmp(argv[i], "--help") == 0 ){
			prnHelp(stdout); /* Assume if they asked for it they want it on stdout */
			return 0;
		}
		if(strcmp(argv[i], "-i") == 0 || strcmp(argv[i], "--info") == 0 ){
			ps.bHdrOut = true;
			continue;
		}
		if(strcmp(argv[i], "-n") == 0 || strcmp(argv[i], "--no-data") == 0 ){
			ps.bDatOut = false;
			continue;
		}
		if(strcmp(argv[i], "-d") == 0){
			i++;
			if(i >= argc){
				fprintf(stderr, "ERROR: DSDF file missing after -d\n");
				return 13;
			}
			ps.sDsdfFile = argv[i];
			continue;
		}
		if(strcmp(argv[i], "-p") == 0){
			i++;
			if(i >= argc){
				fprintf(stderr, "ERROR: Packet ID missing after -p\n");
				return 13;
			}
			ps.nPktId = atoi(argv[i]);
			if(ps.nPktId < 0 || ps.nPktId > 99){
				fprintf(stderr, "ERROR: Packet ID argument, %s, outside of "
				        "valid range [1, 99)", argv[i]);
				return 13;
			}
			continue;
		}
		if(strcmp(argv[i], "-b") == 0){
			i++;
			if(i >= argc){
				fprintf(stderr, "ERROR: Begin trim range missing after -b\n");
				return 13;
			}
			
			if(!dt_parsetime(argv[i], &(ps.dtTrimBeg))){
				fprintf(stderr, "ERROR: Couldn't parse begin trim time %s",argv[i]);
				return 13;
			}
			continue;
		}
		if(strcmp(argv[i], "-e") == 0){
			i++;
			if(i >= argc){
				fprintf(stderr, "ERROR: Begin trim range missing after -e\n");
				return 13;
			}
			
			if(!dt_parsetime(argv[i], &(ps.dtTrimEnd))){
				fprintf(stderr, "ERROR: Couldn't parse begin trim time %s",argv[i]);
				return 13;
			}
			continue;
		}
		
		/* Okay, must be a parameter in the param list, make sure it doesn't 
		 * start with a '-' because that looks like an option */
		if(argv[i][0] == '-'){
			fprintf(stderr, "ERROR: '%s' is not a legal Das2 plane name\n", argv[i]);
			return 13;
		}
		
		if(pParamBuf == NULL) {
#ifndef _WIN32
			pParamBuf = new_DasBuf( sysconf(_SC_ARG_MAX));
#else
			/* Value below from:
			   https://social.msdn.microsoft.com/Forums/vstudio/en-US/9ead72f9-0ca1-4358-b0b5-bdb3b6459636/regarding-posixargmax-and-argmax?forum=vclanguage
         */				
			pParamBuf = new_DasBuf( 8192 );
#endif
		}
				
		/* Make sure all args list ends in a comma, this will be important later */
		DasBuf_puts(pParamBuf, argv[i]);
		DasBuf_puts(pParamBuf, ",");  
	}
	
	/* Copy out the plane list, depends on calloc initialzing vals to '\0' */
	if(pParamBuf) ps.sPlaneList = pParamBuf->sBuf;
	
	/* If we were given a DSDF, go ahead and parse it */
	if(ps.sDsdfFile){
		if( (ps.pDsdf = dsdf_parse(ps.sDsdfFile)) == NULL){
			fprintf(stderr, "ERROR: Problem parsing DSDF file %s\n", ps.sDsdfFile);
			return 15;
		}
	}
	
	/* Create an input processor, pass in application state structure in a
	   user data pointer */
	StreamHandler* pSh = new_StreamHandler(&ps);
	
	pSh->streamDescHandler = NULL;
	pSh->pktDescHandler    = OnPktHdr;
	pSh->pktDataHandler    = onPktData;
	pSh->exceptionHandler  = onException;
	pSh->commentHandler    = NULL;
	pSh->closeHandler      = onClose;

	DasIO* pIn = new_DasIO_cfile("Standard Input", stdin, "r");
	DasIO_addProcessor(pIn, pSh);
	
	int status = DasIO_readAll(pIn);
	return status;
}
