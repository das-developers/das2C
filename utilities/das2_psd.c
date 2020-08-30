/* Copyright (C) 2017   Chris Piker <chris-piker@uiowa.edu>
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


/******************************************************************************
 das2_psd: Convert incoming series data to a Fourier transform of the series.
	        This program changes the morphology of the stream, examples are:

This is a transformer in the X axis.  Thus:

	Packet Remapping, Transform over X:  <y> to <yscan>
	
	   <x> <y A> <y B>
	   <x> <y A> <y B>  ===>   <x> <yscan A> <yscan B>
	   <x> <y A> <y B>

	Packet Remapping, Transform over X:  <xscan> to <yscan>
	
	   <x> <xscan A> <xscan B>       <x> <yscan A> <yscan B>
	   <x> <xscan A> <xscan B>  ===> <x> <yscan A> <yscan B>
		<x> <xscan A> <xscan B> 		<x> <yscan A> <yscan B>
	
These have not been implemented, and may never be....

	Packet Remapping, Transform over Y:  <yscan> to <yscan>
	
	                                  <x>    <yscan A> <yscan B>
	   <x> <yscan A> <yscan B>  ===>  <x+>   <yscan A> <yscan B>
	                                  <x++>  <yscan A> <yscan B>
	

	Packet Remapping, Transform over Y with un-transformable yscan ( here yscan
	   B is too short to transform, so it's dropped ):
	
	                                            <x>    <yscan A> <yscan C>
	   <x> <yscan A> <yscan B> <yscan C>  ===>  <x+>   <yscan A> <yscan C>
	                                            <x++>  <yscan A> <yscan C>
	
See the help text in prnHelp() for mor info. -cwp

*******************************************************************************/

#define _POSIX_C_SOURCE 200112L

#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <math.h>

#include <das2/core.h>
#include "psd_xcenter.h"
#include "psd_xoffset.h"
#include "send.h"

#define PROG_NAME "das2_psd"

#define P_ERR 105

#ifndef DasIO_throw
#define DasIO_srverr(pIo, pSd, sMsg) \
  DasIO_throwException(pIo, pSd, "DAS2_SERVER_ERROR", sMsg); exit(P_ERR);
#endif

/* ************************************************************************* */
/* Globals */

StreamDesc* g_pSdOut = NULL;
DftPlan* g_pDftPlan = NULL;        /* The PSD plan */
Das2Psd* g_pPsdCalc = NULL;        /* The PSD calculator */

#define TRANSFORM_UNK   0x00
#define TRANSFORM_IN_X  0x01
#define TRANSFORM_IN_Y  0x02
#define NO_TRANSFORM    0xFF

size_t g_uDftLen = 0;
size_t g_uSlideDenom = 0;
das_datum g_cadence;
bool g_bSkip = true;
int g_nPktsOut = 0;


/* We remap packet IDs starting with 1, since this code can trigger
   packet ID collapse and packet ID expansion */
int g_nNextPktId = 1;


/* The calculator */
Das2Psd* psdCalc(){
	if(g_pPsdCalc == NULL){
		g_pDftPlan = new_DftPlan(g_uDftLen, true);
		g_pPsdCalc = new_Psd(g_pDftPlan, true, "hann");
	}
	return g_pPsdCalc;
}

/* Get previously defined packet header that is identical to the given one,
 * it must be unique! */
PktDesc* hasMatchingPktDef(DasIO* pOut, StreamDesc* pSd, PktDesc* pPd)
{
	if(pPd == NULL){
		DasIO_srverr(pOut, pSd, "Assertion error in hasMatchingPktDef()");
	}

	PktDesc* pCheck = NULL;
	bool bMatch = false;
	PlaneDesc* pChkPlane = NULL;
	PlaneDesc* pPlane = NULL;
	int nPktId, iPlane;

	for(nPktId = 0; nPktId < 100; ++nPktId){

		if(!StreamDesc_isValidId(pSd, nPktId)) continue;
		pCheck = StreamDesc_getPktDesc(pSd, nPktId);

		if(PktDesc_getNPlanes(pCheck) != PktDesc_getNPlanes(pPd)) continue;

		bMatch = true;
		for(iPlane = 0; iPlane < PktDesc_getNPlanes(pCheck); ++iPlane){
			pChkPlane = PktDesc_getPlane(pCheck, iPlane);
			pPlane = PktDesc_getPlane(pPd, iPlane);
			if(! PlaneDesc_equivalent(pChkPlane, pPlane)){ bMatch = false; break; }
		}
		if(!bMatch) continue;

		return pCheck;
	}
	return NULL;
}


/*****************************************************************************/
/* Stream Header Processing */

DasErrCode onStreamHdr(StreamDesc* pSdIn, void* vpIoOut)
{
	g_pSdOut = StreamDesc_copy(pSdIn);

	/* Remove old Y and Z labels */
	DasDesc_remove((DasDesc*)g_pSdOut, "yLabel");
	DasDesc_remove((DasDesc*)g_pSdOut, "zLabel");

	/* Override the renderer and source ID*/
	DasDesc_setStr((DasDesc*)g_pSdOut, "yScaleType", "linear");
	DasDesc_setStr((DasDesc*)g_pSdOut, "zScaleType", "log");
	DasDesc_setStr((DasDesc*)g_pSdOut, "renderer", "spectrum");
	DasDesc_vSetStr((DasDesc*)g_pSdOut, "sourceId", "das2_psd %d %d",
			           g_uDftLen, g_uSlideDenom);

	/* Set a title using replacements.  Das2 clients don't have to update the
	 * title on every load but will substitute new replacement values when they
	 * change */
	char sTitle[256] = {'\0'};
	const char* sProp = NULL;
	if( (sProp = DasDesc_get((DasDesc*)g_pSdOut, "title")) != NULL){
		strncpy(sTitle, sProp, 255);
		DasDesc_vSetStr((DasDesc*)g_pSdOut, "title",
		"%s!c%%{DFT_length} point DFT, %%{xDftOverlapInfo}", sTitle);
	}
	else{
		DasDesc_vSetStr((DasDesc*)g_pSdOut, "title",
		"!c%%{DFT_length} point DFT, %%{xDftOverlapInfo}");
	}

	if(das_datum_toDbl(&g_cadence) > 0.0){
		DasDesc_setDatum((DasDesc*)g_pSdOut, "xTagWidth",
				            das_datum_toDbl(&g_cadence), g_cadence.units);
	}

	char sOverlap[64] = {'\0'};
	if(g_uSlideDenom == 1) strcpy(sOverlap, "No Overlap");
	else sprintf(sOverlap, "%zu/%zu Overlap", g_uSlideDenom - 1, g_uSlideDenom);
	DasDesc_setStr((DasDesc*)g_pSdOut, "xDftOverlapInfo", sOverlap);

	DasDesc_setInt((DasDesc*)g_pSdOut, "DFT_length", g_uDftLen);
	DasDesc_setInt((DasDesc*)g_pSdOut, "DFT_slide_denominator", g_uSlideDenom);
	DasDesc_setStr((DasDesc*)g_pSdOut, "DFT_window", "hann");

	return DasIO_writeStreamDesc((DasIO*)vpIoOut, g_pSdOut);
}

/*****************************************************************************/
/* Packet Header Processing */

DasErrCode onPktHdr(StreamDesc* pSdIn, PktDesc* pPdIn, void* vpIoOut)
{
	DasIO* pIoOut = (DasIO*)vpIoOut;

	int nPktId = PktDesc_getId(pPdIn);
	PlaneDesc* pXOut = NULL;

	PktHandler* pHandler = NULL;	
	
	if(PktDesc_getNPlanesOfType(pPdIn, PT_Y) > 0){
			
		/* This handler transforms Ys to YScans. */
		pHandler = get_XCenterHandler(
			pPdIn, pIoOut, g_pSdOut, g_uDftLen, g_uSlideDenom, &g_cadence
		);
	
		if( (pPdIn->pUser = (void*)pHandler) == NULL) return P_ERR;
		return DAS_OKAY;	
	}

	if(PktDesc_getNPlanesOfType(pPdIn, PT_XScan) > 0){
		
		/* This handler transforms XScans to Yscans */
		pHandler = get_XScanHandler(
			pPdIn, pIoOut, g_pSdOut, g_uDftLen, g_uSlideDenom, &g_cadence
		);

		if( (pPdIn->pUser = (void*)pHandler) == NULL) return P_ERR;
		return DAS_OKAY;	
	}

	DasIO_srverr(pIoOut, g_pSdOut, 
		"das2_psd is a 1-D FFT transformer, <yscan> and <z> planes are "
		"not handled."
	);
	return P_ERR;
}

DasErrCode onPktData(PktDesc* pPdIn, void* vpIoOut)
{
	/* Remember, certain packet IDs may not be transformable and may have
	   been dropped from the output */
	if(pPdIn->pUser == NULL) return DAS_OKAY;

	DasIO* pIoOut = (DasIO*)vpIoOut;
	
	PktHandler* pHandler = (PktProcInfo*)(pPdIn->pUser);
	if(pHandler != NULL)
		return pHandler->onData(pInfo, pPdIn, pIoOut, g_pSdOut);
	
	return das_send_srverr(2, "Bug in das2_psd, onPktData()");
}

/* ************************************************************************* */
/* Stream Close Handling - if nothing could be written, send no-data except  */

DasErrCode onClose(StreamDesc* pSdIn, void* vIoOut)
{
	DasIO* pIoOut = (DasIO*)vIoOut;
	OobExcept except;
	DasErrCode nRet = DAS_OKAY;

	if(g_nPktsOut == 0){
		OobExcept_set(
			&except, DAS2_EXCEPT_NO_DATA_IN_INTERVAL, "All input data segments "
			"were too short to for the requested DFT size"
		);

		nRet = DasIO_writeException(pIoOut, &except);
		OutOfBand_clean((OutOfBand*)&except);
	}

	return nRet;
}

/* ************************************************************************* */
/* Copy out Comments, trigger halts on exceptions */

DasErrCode onException(OobExcept* pExcept, void* vIoOut)
{
	DasIO* pIoOut = (DasIO*)vIoOut;

	DasErrCode nRet = DasIO_writeException(pIoOut, pExcept);
	if(nRet != 0) return nRet;
	return P_ERR;
}

DasErrCode onComment(OobComment* pCmt, void* vIoOut)
{
	DasIO* pIoOut = (DasIO*)vIoOut;
	return DasIO_writeComment(pIoOut, pCmt);
}

/* ************************************************************************* */
void prnHelp()
{
	fprintf(stderr,
"SYNOPSIS\n"
"   das2_psd - Convert time series streams into power spectral density streams\n"
"\n");

	fprintf(stderr,
"USAGE\n"
"   das2_psd [options] LENGTH SLIDE_DENOMINATOR\n"
"\n");

	fprintf(stderr,
"DESCRIPTION\n"
"   das2_psd is a classic Unix filter, reading das2 streams on standard input\n"
"   and producing a transformed stream containing packets that are LENGTH/2 +1\n"
"   y values long on the standard output.  Note that LENGTH must be an even\n"
"   number, but need not be a power of two.\n"
"\n"
"   Input data are gathered into FFT buffers in the following manner:\n"
"\n"
"     * Values with regular time cadence are read into a buffer of size\n"
"       LENGTH.  If the cadence is broken, values are discarded and\n"
"       accumulation starts over.\n"
"\n"
"     * Once the buffer has been filled, a power spectral density calculation\n"
"       is preformed on the input values and output in a <yscan> plane\n"
"\n"
"     * Values are shifted down by LENGTH/SLIDE_DENOMINATOR points and\n"
"       filling continues\n"
"\n"
"   The following table relates SLIDE_DENOMINATOR and percentage overlap for\n"
"   DFT (Discrete Fourier Transform) calculations:\n"
"\n"
"      SLIDE_DENOM      Percent Overlap\n"
"      -----------      ---------------\n"
"           1                  0%%\n"
"           2                 50%%\n"
"           3                 66%%\n"
"           4                 75%%\n"
"           5                 80%%\n"
"          ...                ...\n"
"\n"
"   The shape of the stream changes when transformed.  Though the number of\n"
"   independent packet IDs remains the same, the number of actual data packets\n"
"   in the output stream can vary dramatically from the input stream.  Stream\n"
"   morphology changes fall into three categories:\n"
"\n"
"      Case A: X with multiple Y's\n"
"      ===============================================================\n"
"      Input )  LENGTH packets with shape: <x><y><y><y>\n"
"\n"
"      Output)  1 packet with shape:       <x><yscan><yscan><yscan>\n"
"\n"
"\n"
"      Case B:  X with multiple YScans\n"
"      ===============================================================\n"
"      Input ) One packet with shape:     <x><yscan><yscan><yscan>\n"
"\n"
"      Output) 1-N packets with shape:    <x><yscan><yscan><yscan>\n"
"\n"
"\n"
"      Case C:  X with multiple Y's and YScans\n"
"      ===============================================================\n"
"      Input ) One packet with shape:     <x><y><y><yscan><yscan>\n"
"\n"
"      Output) 1-N packets with shape:    <x><y><y><yscan><yscan>\n"
"\n"
"\n"
"   In case C (mixed line-plot and table data) above, the <y> values are\n"
"   treated as <x> values and just copied to the output stream.\n"
"\n");

	fprintf(stderr,
"OPTIONS\n"
"   -h,--help     Display this text and exit.\n"
"\n"
"   -v,--version  Display source version information and exit.\n"
"\n"
"   -c \"DATUM\",--cadence=\"DATUM\"\n"
"                 The display interpolation DATUM that makes sense for waveform\n"
"                 data is often way too small for spectrograms.  For streams\n"
"                 transformed in <x> a new one of 2x the length of the DFT is\n"
"                 emitted.  Use this parameter to override the xTagWidth that\n"
"                 would normally be transmitted.\n"
"                 Note: A space is required between unit value and the unit\n"
"                 string, so this argument will need quotes.\n"
"\n"
"   -n,--no-skip  Do not skip over input packet *types* that cannot be\n"
"                 transformed, instead exit the program with an error message.\n"
"                 Individual data packets that cannot be transformed are always\n"
"                 skipped.\n"
"\n"
"   -x,--trans-x  A series of <yscan> packets can be equally spaced in either\n"
"                 the X or the Y dimension.  By default <yscan>s are assumed to\n"
"                 be waveform packets which have a regular Y spacing but\n"
"                 irregular X spacing.  Use this option to force all transforms\n"
"                 to be over the X dimension.\n"
"\n"
"  -m \"ID,DATUM\" ,--map \"ID,DATUM\"\n"
"                 For <x><y><y> set the packet ID to use when ever a particular\n"
"                 sample time is detected.  This allows for consistent packet\n"
"                 ID assignment for datasets with variable sample rates and \n"
"                 thus simpler reduced-resolution cache sets (see \n"
"                 das2_cache_rdr for mor info).  If this option is not selected\n"
"                 packet ID are assigned in order base on the detected sample\n"
"                 rate in the input stream.\n"
"\n");
	fprintf(stderr,
"LIMITATIONS\n"
"   Transforms for input <yscan> packets are always preformed in the Y dimension\n"
"   and *never* cross packet boundaries.  Thus if the LENGTH argument is larger \n"
"   the number of items in all <yscan> packets, no output is generated.\n"
"\n"
"   Transforming N <yscan> packets in the X dimension, to N <yscan>s in the 1/X\n"
"   dimension is useful, but not yet supported.\n"
"\n");
	fprintf(stderr,
"AUTHOR\n"
"   chris-piker@uiowa.edu\n"
"\n");
	fprintf(stderr,
"SEE ALSO\n"
"   * das2_bin_avg, das2_bin_avgsec, das2_ascii\n"
"\n"
"   * The Das2 ICD at http://das2.org for a general introduction\n"
"\n");
}

/* ************************************************************************* */
void prnVersion()
{
	fprintf(stderr, "SVN ID:  $Id: das2_psd.c 11341 2019-04-04 06:46:57Z cwp $\n");
	fprintf(stderr, "SVN URL: $Url$\n");
}

/* ************************************************************************* */
bool parseArgs(
	int argc, char** argv, size_t* pDftLen, size_t* pSlideDenom, 
	das_datum* pCadence
){
	int i = 0;
	size_t uTmp = 0;
	const char* sArg = NULL;
	while( i < (argc - 1)){
		++i;

		if(argv[i][0] == '-'){
			if((strcmp(argv[i], "-h") == 0)||(strcmp(argv[i], "--help") == 0)){
				prnHelp();
				exit(0);
			}
			if((strcmp(argv[i], "-v") == 0)||(strcmp(argv[i], "--version") == 0)){
				prnVersion();
				exit(0);
			}

			if((strcmp(argv[i], "-c") == 0)||(strncmp(argv[i], "--cadence=", 10) == 0)){
				if(strcmp(argv[i], "-c") == 0){
					if(i > (argc - 1)){
						das_send_queryerr(2, "Missing argument for -c");
						return false;
					}
					++i;
					sArg = argv[i];
				}
				else{
					if(strlen(argv[i]) < 11){
						das_send_queryerr(2, "Missing argument for --cadence==");
						return false;
					}
					sArg = argv[i];
					sArg += 10;
				}
				if(! das_datum_fromStr(pCadence, sArg) || (das_datum_toDbl(pCadence) <= 0.0 )){
					das_send_queryerr(2, "Couldn't convert %s to a valid X-Tag cadence", sArg);
					return false;
				}
				continue;
			}

			if((strcmp(argv[i], "-n") == 0)||(strcmp(argv[i], "--no-skip") == 0)){
				g_bSkip = false;
				continue;
			}

			if((strcmp(argv[i], "-x") == 0)||(strcmp(argv[i], "--trans-x") == 0)){
				das_send_srverr(2, "Forcing X transformations is not yet supported");
				return false;
			}

		}
		else{

			if(sscanf(argv[i], "%zu", &uTmp) != 1){
				das_send_queryerr(2,"Couldn't convert '%s' to an integer", argv[i]);
				return false;
			}

			if(*pDftLen == 0){
				if(uTmp < 16){
					das_send_queryerr(2,"%d is below the minimum DFT length of 16 "
							             "points",uTmp);
					return false;
				}
				if(uTmp % 2 != 0){
					das_send_queryerr(2,"%d must be an even number",uTmp);
					return false;
				}
				*pDftLen = uTmp;
				continue;
			}

			if(*pSlideDenom == 0){
				if(uTmp < 1){
					das_send_queryerr(2,"%d is below the minimum slide fraction "
						             "denominator of 1",uTmp);
					return false;
				}
				*pSlideDenom = uTmp;

				if(*pSlideDenom > (*pDftLen - 1)){
					das_send_queryerr(2,"The given slide denominator was %d, but the "
							            "maximum overlap is to slide by one point, i.e. "
							            "denominator = %d", *pSlideDenom, (*pDftLen)-1);
					return false;

				}
				continue;
			}
		}

		das_send_queryerr(2, "Unknown command line parameter '%s'", argv[i]);
		return false;
	}
	return true;
}

/* ************************************************************************* */
int main(int argc, char** argv) {

	/* Exit on errors, log info messages and above */
	das_init(argv[0], DASERR_DIS_EXIT, 0, DASLOG_INFO, NULL);

	if(!parseArgs(argc, argv, &g_uDftLen, &g_uSlideDenom, &g_cadence)) return 13;

	/* We know our output size, generate an FFT plan */


	/* Make output writer and a stream handler structure */
	DasIO* pIoOut = new_DasIO_cfile("das2_psd", stdout, "w");

	StreamHandler* pSh = new_StreamHandler( pIoOut );

	pSh->streamDescHandler = onStreamHdr;
	pSh->pktDescHandler = onPktHdr;
	pSh->pktDataHandler = onPktData;
	pSh->closeHandler = onClose;
	pSh->commentHandler = onComment;
	pSh->exceptionHandler = onException;

	/* Make input reader, give it the stream handler structure containing
	   per-packet call-backs */
	DasIO* pIn = new_DasIO_cfile("Standard Input", stdin, "r");
	DasIO_addProcessor(pIn, pSh);

	int nRet = DasIO_readAll(pIn);

	/* Uncomment to make valgrind happy */
	/*
	PktDesc* pPdOut = NULL;
	PlaneDesc* pXOut = NULL;

	for(int i = 1; i < 100; ++i){
		if( ! StreamDesc_isValidId(g_pSdOut, i) ) continue;

		pPdOut = StreamDesc_getPktDesc(g_pSdOut, i);
		pXOut = PktDesc_getXPlane(pPdIn);
		if(pXOut->pUser) del_Accum((Accum*)(pXOut->pUser));

		StreamDesc_freePktDesc(g_pSdOut, nPktId);
	}*/

	return nRet;
}
