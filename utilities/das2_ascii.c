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
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>

#include <das2/core.h>

/* ************************************************************************* */
/* Globals */

int g_nTimeWidth = 24;    /* default to 'time24' */
const char* g_sTimeFmt = NULL;

int g_n8ByteWidth = 17;   /* default to 'ascii17' for 8-byte floats */
int g_n4ByteWidth = 14;   /* default to 'ascii14' for 4-byte floats */

StreamDesc* g_pSdOut = NULL;

bool g_bAnnotations = true;

/* ************************************************************************* */
/* Output a Stream Header */
DasErrCode onStreamHdr(StreamDesc* pSdIn, void* vpOut)
{
	DasIO* pOut = (DasIO*)vpOut;
	g_pSdOut = StreamDesc_copy(pSdIn);              /* Copy input descriptor */
	return DasIO_writeStreamDesc(pOut, g_pSdOut);   /* Write output stream hdr */
}

/* ************************************************************************* */
/* Output a Packet Header with different value encodings */
DasErrCode OnPktHdr(StreamDesc* pSdIn, PktDesc* pPdIn, void* vpOut)
{
	DasIO* pOut = (DasIO*)vpOut;
	PktDesc* pPdOut = NULL;
	int i = 0;
	PlaneDesc* pPlane = NULL;
	DasEncoding* pCurEnc = NULL;
	DasEncoding* pNewEnc = NULL;
	
	int nPktId = PktDesc_getId(pPdIn);
	
	/* Handle packet re-definitions */
	if(StreamDesc_isValidId(g_pSdOut, nPktId))
		StreamDesc_freePktDesc(g_pSdOut, nPktId);
	
   pPdOut = StreamDesc_clonePktDescById(g_pSdOut, pSdIn, nPktId);
	
	/* Possibly swap out encodings for some of the planes */
	for(i=0; i<pPdOut->uPlanes; i++){
		
		pPlane = PktDesc_getPlane(pPdOut, i);
		pCurEnc = PlaneDesc_getValEncoder(pPlane);
		pNewEnc = NULL;
		
		/* If encoder is already ascii, it's fine as it is */
		if((pCurEnc->nCat == DAS2DT_ASCII)||(pCurEnc->nCat == DAS2DT_TIME))
			continue;

		/* Convert binary time units in a different manner than non-time units */
		if(strcmp(pPlane->units, UNIT_US2000) == 0 ||
		   strcmp(pPlane->units, UNIT_MJ1958) == 0 ||
			strcmp(pPlane->units, UNIT_T2000) == 0 ||
			strcmp(pPlane->units, UNIT_T1970) == 0   ){
			
			if(g_sTimeFmt == NULL)
				pNewEnc = new_DasEncoding(DAS2DT_TIME, 24, NULL);
			else
				pNewEnc = new_DasEncoding(DAS2DT_TIME, g_nTimeWidth, g_sTimeFmt);
	
			PlaneDesc_setValEncoder(pPlane, pNewEnc);
			
			/* Also switch to us2000 if we are here anyway. */
			/* PlaneDesc_setUnits(pPlane, UNIT_US2000); */
			continue;
		}
		
		/* Give Doubles and Floats different output resolutions */
		if(pCurEnc->nCat == DAS2DT_BE_REAL || pCurEnc->nCat == DAS2DT_LE_REAL){
			switch(pCurEnc->nWidth){
			case 8: pNewEnc = new_DasEncoding(DAS2DT_ASCII, g_n8ByteWidth, NULL); break;
			case 4: pNewEnc = new_DasEncoding(DAS2DT_ASCII, g_n4ByteWidth, NULL); break;
			default:
				return das_error(100, "Don't know how to deal with %d byte wide "
						"binary reals", pCurEnc->nWidth);
			}
			PlaneDesc_setValEncoder(pPlane, pNewEnc);
			continue;
		}
		
		return das_error(100, "Don't know what to do with value type %s in plane"
		                  "index %d of packet ID %02d", pCurEnc->sType, i, pPdIn->id);
	}
	return DasIO_writePktDesc(pOut, pPdOut);
}

/* ************************************************************************* */
/* Copy and output data */
DasErrCode onPktData(PktDesc* pPdIn, void* vpOut )
{
	DasIO* pOut = (DasIO*)vpOut;
	PktDesc* pPdOut = StreamDesc_getPktDesc(g_pSdOut, PktDesc_getId(pPdIn));
	PlaneDesc* pInPlane = NULL;
	PlaneDesc* pOutPlane = NULL;
	
	for(size_t u=0; u<pPdIn->uPlanes; u++ ){
		pInPlane = PktDesc_getPlane(pPdIn, u);
		pOutPlane = PktDesc_getPlane(pPdOut, u);
		PlaneDesc_setValues(pOutPlane, PlaneDesc_getValues(pInPlane));
	}

	return DasIO_writePktData(pOut, pPdOut);	 
}

/* ************************************************************************* */
/* Maybe copy out Exceptions and Comments */

DasErrCode onException(OobExcept* pExcept, void* vpOut)
{
	if(! g_bAnnotations) return 0;
	
	DasIO* pOut = (DasIO*)vpOut;
	return DasIO_writeException(pOut, pExcept);
}

DasErrCode onComment(OobComment* pCmt, void* vpOut)
{
	if(! g_bAnnotations) return 0;
	
	DasIO* pOut = (DasIO*)vpOut;
	return DasIO_writeComment(pOut, pCmt);
}

/* ************************************************************************* */
/* Close Output */
DasErrCode onClose( StreamDesc* sd, void* vpOut)
{
	DasIO* pOut = (DasIO*)vpOut;
	DasIO_close(pOut);
	return 0;
}

/*****************************************************************************/
void prnHelp()
{
	fprintf(stderr,
"SYNOPSIS:\n"
"   das2_ascii - Reformat Binary values to ASCII in a Das2 Stream\n"
"\n"
"USAGE:\n"
"   das2_ascii [-r N] [-s N]\n"
"\n"
"DESCRIPTION:\n"
"   das2_ascii is a filter.  It reads a das2 stream on standard input and\n"
"   writes a Das2 Stream to standard output.  Any data variables in the input\n"
"   stream which contain binary data values are translated to ASCII values\n"
"   before sending to standard output.  Planes already contanining ASCII \n"
"   data are transmitted without effect.\n"
"\n"
"   By default 32-bit floating points numbers are written with 7 significant\n"
"   digits in the mantissa and 2 digits in the exponent.  Any 64-bit floats\n"
"   encontered in the input stream are written with 17 significant digits in\n"
"   the mantissa and 2 digits in the exponent.  Binary time values are written\n"
"   as ISO-8601 timestamps with microsecond resolution, i.e. the pattern\n"
"   yyyy-mm-ddThh:mm:ss.ssssss\n"
"\n"
"   All output values are rounded normally instead of truncating fractions.\n"
"\n"
"OPTIONS:\n"
"\n"
"   -h,--help\n"
"         Print this help text\n"
"\n"
"   -r N  General data value resolution.  Output all non-time values with N\n"
"         significant digits instead of the defaults.  The minimum resolution\n"
"         is 2 significant digits\n"
"\n"
"   -s N  Sub-second resolution.  Output N digits of sub-second resolution.\n"
"         Times are always output with at least seconds resolution.\n"
"\n"
"   -c    Clean comment and exception annotations out of the stream.\n"
"\n"
"AUTHORS:\n"
"   jeremy-faden@uiowa.edu  (original)\n"
"   chris-piker@uiowa.edu   (current maintainer)\n"
"\n"
"SEE ALSO:\n"
"   das2_csv, das2_binary, das2_hapi\n"
"\n"
"   The das 2 ICD @ http://das2.org for an introduction to the das 2 system.\n"
"\n");	
}

/*****************************************************************************/
/* Main */

int main( int argc, char *argv[]) {

	int i = 0;
	int status = 0;
	int nGenRes = 7;
	int nSecRes = 3;
	char sTimeFmt[64] = {'\0'};
	
	/* Exit on errors, log info messages and above */
	das_init(argv[0], DASERR_DIS_EXIT, 0, DASLOG_INFO, NULL);

	for(i = 1; i < argc; i++){
		if(strcmp(argv[i], "-h") == 0 || strcmp(argv[i], "--help") == 0 ){
			prnHelp();
			return 0;
		}
		
		if(strcmp(argv[i], "-r") == 0){
			i++;
			if(i >= argc){
				fprintf(stderr, "ERROR: Resolution parameter missing after -r\n");
				return 13;
			}
			nGenRes = atoi(argv[i]);
			if(nGenRes < 2 || nGenRes > 18){
				fprintf(stderr, "ERROR: Can't format to %d significant digits, "
						  "Supported range is only 2 to 18 significant digits.\n",
						  nGenRes);
				return 13;
			}
			continue;
		}
		
		if(strcmp(argv[i], "-v") == 0 || strcmp(argv[i], "--version") == 0){
			printf("$TODO: Find a git auto substitution method$\n");
			return 0;
		}
		
		if(strcmp(argv[i], "-s") == 0){
			i++;
			if(i >= argc){
				fprintf(stderr, "ERROR: Sub-seconds resolution parameter missing after -s\n");
				return 13;
			}
			nSecRes = atoi(argv[i]);
			if(nSecRes < 0 || nSecRes > 9){
				fprintf(stderr, "ERROR: Only 0 to 9 sub-seconds digits supported "
				        "don't know how to handle %d sub-second digits.", nSecRes);
				return 13;
			}
			continue;
		}
		
		if(strcmp(argv[i], "-c") == 0){
			g_bAnnotations = false;
			continue;
		}
		
		fprintf(stderr, "ERROR: unknown parameter '%s'\n", argv[i]);
		return 13;
	}
	
	if(nGenRes != 7){
		g_n4ByteWidth = nGenRes + 7;
		g_n8ByteWidth = nGenRes + 7;
	}
	
	if(nSecRes != 3){
		if(nSecRes == 0)
			g_nTimeWidth = 20;
		else
			g_nTimeWidth = 21 + nSecRes;
		
		sprintf(sTimeFmt, "%%04d-%%02d-%%02dT%%02d:%%02d:%%0%d.%df", 
				  nSecRes + 3, nSecRes);
		g_sTimeFmt = sTimeFmt;
	}
	
	/* Create an un-compressed output I/O object */
	DasIO* pOut = new_DasIO_cfile("das2_ascii", stdout, "w");
	
	/* Create an input processor, provide the output processor as a user data
	   object so that the callbacks have access to it without using a global
	   variable. */
	StreamHandler* pSh= new_StreamHandler(pOut);
	
	pSh->streamDescHandler = onStreamHdr;
	pSh->pktDescHandler    = OnPktHdr;
	pSh->pktDataHandler    = onPktData;
	pSh->exceptionHandler  = onException;
	pSh->commentHandler    = onComment;
	pSh->closeHandler      = onClose;

	DasIO* pIn = new_DasIO_cfile("Standard Input", stdin, "r");
	DasIO_addProcessor(pIn, pSh);
	
	status = DasIO_readAll(pIn);
	return status;
}

