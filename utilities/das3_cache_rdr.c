/* das3_cache_rdr PROTOTYPE - night-1 stretch goal, dasflex cache effort.
 *
 * Lives in dasflex/tmp for review; the real home is
 * das2C/utilities/das3_cache_rdr.c once blessed.  Contract:
 * das2C/co_notes/feature_das3_cache_rdr.md and decision D1 in
 * dasflex/co_notes/design_cache_system.md.
 *
 * The front door principle: this program learns EVERYTHING from argv.
 * The server passes a rendered uri.h template with the norm-params,
 * format and resolution components baked in as literals, plus one range
 * per constrained coordinate.  No internal.json, flex.json or dsdf is
 * ever read here; level selection and miss detection are dasflex
 * Python's job, done before this program runs.
 *
 *   das3_cache_rdr [options] -r time,BEG,END TEMPLATE
 *
 * Compared to utilities/das2_cache_rdr.c, everything that read dsdf
 * cacheLevel lines, chose a bin and walked directories by hand is gone:
 * das_uri_list() replaces about four hundred lines of that.  The stream
 * merge callbacks are carried over nearly verbatim since the job is
 * unchanged: one stream out of many block files, repeated headers
 * dropped, packet IDs reconciled, records trimmed to the request range.
 *
 * Prototype limits (all exit loudly, nothing is silent):
 *   - time coordinate only; --coord is parsed and reserved but refused
 *   - das2 stream blocks only (.d2s); das3 block merging needs the
 *     dataset-level API and arrives with the real utility
 *
 * Build (from the dasflex project root):
 *
 *   gcc -I../das2C -o tmp/das3_cache_rdr tmp/das3_cache_rdr.c \
 *       ../das2C/build.rocky9/libdas3.a \
 *       -lexpat -lssl -lcrypto -lfftw3 -lz -lm -lpthread
 */

#define _POSIX_C_SOURCE 200112L

#include <stdio.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>

#ifdef _WIN32
#define PATH_MAX 260
#else
#include <limits.h>
#endif

#include <das3/core.h>

#define P_ERR 92

/* ************************************************************************* */
/* Stream merge state, carried over from das2_cache_rdr.c                   */

typedef struct handler_data {
	bool bHdrSent;
	int nPktsSent;
	double rBeg;        /* request begin, us2000, pad applied   */
	double rEnd;        /* request end, us2000, pad applied     */
	const char* sBeg;   /* request begin as given, for messages */
	const char* sEnd;
	DasIO* pOut;
	DasStream* pSdOut;
	DasBuf* pBuf;       /* re-encoded payloads, sized by onPktHdr */
	size_t uBufSz;
} handler_data_t;


das_errcode onStreamHdr(DasStream* pSdIn, void* vpHDat)
{
	handler_data_t* pHDat = (handler_data_t*)vpHDat;
	DasDesc* pSdOut = (DasDesc*)(pHDat->pSdOut);

	if(!pHDat->bHdrSent){
		/* Keep our own cache range note across the wholesale copy */
		char sRng[160] = {'\0'};
		snprintf(sRng, 159, "%s to %s UTC", pHDat->sBeg, pHDat->sEnd);

		DasDesc_copyIn(pSdOut, (DasDesc*)pSdIn);
		DasDesc_set(pSdOut, "DatumRange", "xCacheRange", sRng);

		das_errcode nRet = DasIO_writeStreamDesc(pHDat->pOut, pHDat->pSdOut);
		if(nRet != 0) return nRet;
		pHDat->bHdrSent = true;
	}
	return 0;
}

das_errcode onPktHdr(DasStream* pSdIn, DasPkt* pPdIn, void* vpHDat)
{
	handler_data_t* pHDat = (handler_data_t*)vpHDat;
	DasStream* pSdOut = pHDat->pSdOut;
	DasPkt* pPdOut = NULL;
	int nPktId = DasPkt_getId(pPdIn);

	/* Encoding writes exactly the wire size decoding just demanded */
	size_t uRecBytes = DasPkt_recBytes(pPdIn);
	if(uRecBytes > pHDat->uBufSz){
		if(pHDat->pBuf) del_DasBuf(pHDat->pBuf);
		if((pHDat->pBuf = new_DasBuf(uRecBytes)) == NULL) return P_ERR;
		pHDat->uBufSz = uRecBytes;
	}

	if(DasStream_isValidId(pSdOut, nPktId)){
		/* Drop repeated packet descriptions, re-emit true redefinitions */
		pPdOut = DasStream_getPkt(pSdOut, nPktId);
		if(DasPkt_equalFormat(pPdIn, pPdOut))
			return 0;
		DasStream_freeDesc(pSdOut, nPktId);
	}

	if((pPdOut = DasStream_clonePktById(pSdOut, pSdIn, nPktId)) == NULL)
		return P_ERR;

	return DasIO_writeDesc(pHDat->pOut, (DasDesc*)pPdOut, 0);
}

das_errcode onPktData(DasPkt* pPktIn, void* vpHDat)
{
	handler_data_t* pHDat = (handler_data_t*)vpHDat;

	/* Trim to the (padded) request range.  Only the first and last
	   block can straddle it, but the per-packet test keeps the logic
	   one screen long; see the RawDataHandler note in das2_cache_rdr.c
	   for the pass-through upgrade both programs want. */
	DasPlane* pPlane = DasPkt_getXPlane(pPktIn);
	double rX = DasPlane_getValue(pPlane, 0);
	if(strcmp(DasPlane_getUnits(pPlane), UNIT_US2000) != 0)
		rX = Units_convertTo(rX, DasPlane_getUnits(pPlane), UNIT_US2000);
	if(rX < pHDat->rBeg) return 0;
	if(rX >= pHDat->rEnd) return 0;

	das_errcode nRet = DAS_OKAY;
	DasBuf_reinit(pHDat->pBuf);
	if((nRet = DasPkt_encodeData(pPktIn, pHDat->pBuf)) != DAS_OKAY)
		return nRet;

	size_t uLen = 0;
	const ubyte* pData = DasBuf_data(pHDat->pBuf, &uLen);
	nRet = DasIO_writePktBytes(pHDat->pOut, DasPkt_getId(pPktIn), pData, uLen);
	if(nRet == 0){
		pHDat->nPktsSent += 1;
		return 0;
	}
	return nRet;
}

/* Exceptions inside block files (typically no-data notes from the build
   pipeline) must not end the merge; we count packets and write our own
   no-data message if nothing came through. */
#ifdef __GNUC__
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-parameter"
#endif
das_errcode onIgnoreExcept(DasStream* pSd, const DasExcept* pExcept, void* vpHDat)
{
	return DAS_OKAY;
}
#ifdef __GNUC__
#pragma GCC diagnostic pop
#endif

void sendNoData(handler_data_t* pHDat)
{
	if(!pHDat->bHdrSent)
		DasIO_writeStreamDesc(pHDat->pOut, pHDat->pSdOut);

	char sMsg[512] = {'\0'};
	snprintf(sMsg, 511, "No data in the interval %s to %s in this cache set",
	         pHDat->sBeg, pHDat->sEnd);

	daserr_setdisp(DASERR_DIS_RET);
	DasExcept* pEx = new_DasExcept(DAS_EX_NO_DATA, sMsg);
	daserr_setdisp(DASERR_DIS_EXIT);

	DasIO_exception(pHDat->pOut, pEx);
	del_DasExcept(pEx);
}

/* ************************************************************************* */

void prnHelp(void)
{
	printf(
"SYNOPSIS\n"
"   das3_cache_rdr - Emit dasflex cache block files as one stream (PROTOTYPE)\n"
"\n"
"USAGE\n"
"   das3_cache_rdr [options] -r time,BEG,END TEMPLATE\n"
"\n"
"DESCRIPTION\n"
"   das3_cache_rdr is the hit path of the dasflex block cache.  The server\n"
"   decides cacheability, picks the blockSet and verifies every block exists\n"
"   BEFORE invoking this program; here the given uri.h template is iterated\n"
"   over the given range and the matching block files are emitted as a\n"
"   single das stream.  Repeated headers are dropped, packet IDs are\n"
"   reconciled across blocks, and records are trimmed to the request range.\n"
"\n"
"   Everything is learned from the command line; no server configuration\n"
"   file of any kind is read.\n"
"\n"
"   TEMPLATE is a rendered uri.h path template for one cache set, with the\n"
"   norm-params, stream format and resolution components baked in, e.g.:\n"
"\n"
"      /cache/data/site/inst/time/das2/o10khz/$Y/$m/$Y-$m-$d_bin-6s.d2s\n"
"\n"
"OPTIONS\n"
"   -r COORD,BEG,END   The range to emit, half open [BEG, END).  This\n"
"                      prototype accepts the built-in time coordinate only.\n"
"   -p SEC, --pad=SEC  Also emit records SEC seconds either side of the\n"
"                      range.  The server passes two resolution widths by\n"
"                      default so plot edges get neighbor points.\n"
"   --coord DEF        RESERVED: runtime coordinate definitions for\n"
"                      non-time caches; refused in this prototype.\n"
"   -h, --help         This text.\n"
"\n"
"LIMITS (prototype)\n"
"   das2 stream blocks only.  Missing blocks are not detectable here since\n"
"   only files that exist are iterated; block completeness is the server's\n"
"   pre-flight check.\n"
"\n");
}

/* ************************************************************************* */

int main(int argc, char* argv[])
{
	double rPad = 0.0;
	const char* sTplt = NULL;
	char sBeg[64] = {'\0'};
	char sEnd[64] = {'\0'};
	bool bHaveRng = false;

	das_init(argv[0], DASERR_DIS_EXIT, DASLOG_INFO, NULL);

	for(int i = 1; i < argc; i++){
		if((strcmp(argv[i],"-h")==0)||(strcmp(argv[i],"--help")==0)){
			prnHelp();
			return 0;
		}
		if(strcmp(argv[i], "--coord") == 0){
			return das_error(P_ERR,
				"--coord is reserved for runtime coordinate definitions and "
				"is not implemented in this prototype");
		}
		if(strcmp(argv[i], "-p") == 0){
			if(argc <= i+1)
				return das_error(P_ERR, "Argument missing for pad (-p)");
			i++;
			if(!das_str2double(argv[i], &rPad) || (rPad < 0.0))
				return das_error(P_ERR, "Bad pad value '%s'", argv[i]);
			continue;
		}
		if(strncmp(argv[i], "--pad=", 6) == 0){
			if(!das_str2double(argv[i]+6, &rPad) || (rPad < 0.0))
				return das_error(P_ERR, "Bad pad value '%s'", argv[i]+6);
			continue;
		}
		if(strcmp(argv[i], "-r") == 0){
			if(argc <= i+1)
				return das_error(P_ERR, "Argument missing for range (-r)");
			i++;
			char sCoord[32] = {'\0'};
			if(sscanf(argv[i], "%31[^,],%63[^,],%63s", sCoord, sBeg, sEnd) != 3)
				return das_error(P_ERR,
					"Can't parse '-r %s', expected COORD,BEG,END", argv[i]);
			if(strcmp(sCoord, "time") != 0)
				return das_error(P_ERR,
					"Coordinate '%s' is not implemented; this prototype "
					"handles time only (see --coord)", sCoord);
			bHaveRng = true;
			continue;
		}
		if(argv[i][0] == '-')
			return das_error(P_ERR, "Unknown option '%s'", argv[i]);
		if(sTplt != NULL)
			return das_error(P_ERR, "Only one TEMPLATE may be given");
		sTplt = argv[i];
	}

	if((sTplt == NULL) || (!bHaveRng)){
		fprintf(stderr, "Usage: das3_cache_rdr [options] -r time,BEG,END "
		        "TEMPLATE\nIssue %s -h for details.\n", argv[0]);
		return 13;
	}

	size_t uLen = strlen(sTplt);
	if((uLen > 4) && (strcmp(sTplt + uLen - 4, ".d3b") == 0))
		return das_error(P_ERR,
			"das3 block merging is not implemented in this prototype");

	/* The record trim bounds, pad applied */
	das_time dtBeg = {0}, dtEnd = {0};
	if(!dt_parsetime(&dtBeg, sBeg))
		return das_error(P_ERR, "Can't parse begin time '%s'", sBeg);
	if(!dt_parsetime(&dtEnd, sEnd))
		return das_error(P_ERR, "Can't parse end time '%s'", sEnd);

	double rBeg = Units_convertFromDt(&dtBeg, UNIT_US2000) - rPad*1.0e+6;
	double rEnd = Units_convertFromDt(&dtEnd, UNIT_US2000) + rPad*1.0e+6;
	if(rEnd <= rBeg)
		return das_error(P_ERR, "Begin %s is not before end %s", sBeg, sEnd);

	/* The file selection bounds: the padded range, so a pad reaching
	   into the neighbor block picks that block up too */
	das_time dtSelBeg = {0}, dtSelEnd = {0};
	Units_convertToDt(&dtSelBeg, rBeg, UNIT_US2000);
	Units_convertToDt(&dtSelEnd, rEnd, UNIT_US2000);
	char sSelBeg[64] = {'\0'}, sSelEnd[64] = {'\0'};
	dt_isoc(&dtSelBeg, 0, sSelBeg, 63);
	dt_isoc(&dtSelEnd, 0, sSelEnd, 63);

	das_range rng;
	if(das_range_fromUtc(&rng, sSelBeg, sSelEnd) != DAS_OKAY)
		return das_error(P_ERR, "Can't form a time range from %s to %s",
		                 sSelBeg, sSelEnd);
	das_dim_range dr;
	das_dim_range_init(&dr, "time", &rng);

	size_t uFiles = 0;
	char** ppFiles = das_uri_list(sTplt, das_time_uridef(), 1, &dr, &uFiles);

	/* Merge whatever matched into one output stream */
	handler_data_t hdat = {0};
	hdat.rBeg = rBeg;
	hdat.rEnd = rEnd;
	hdat.sBeg = sBeg;
	hdat.sEnd = sEnd;
	hdat.pOut = new_DasIO("das3_cache_rdr", new_DasXport_stdout(), NULL);
	hdat.pSdOut = new_DasStream();

	char sRng[160] = {'\0'};
	snprintf(sRng, 159, "%s to %s UTC", sBeg, sEnd);
	DasDesc_set((DasDesc*)hdat.pSdOut, "DatumRange", "xCacheRange", sRng);

	if((ppFiles == NULL) || (uFiles == 0)){
		daslog_info_v("0 cache blocks match %s", sTplt);
		sendNoData(&hdat);
		DasIO_close(hdat.pOut);
		return 0;
	}

	das_stream_proc proc;
	das_stream_proc_init(&proc, &hdat);
	proc.onStreamDesc = onStreamHdr;
	proc.onPktDesc = onPktHdr;
	proc.onPktData = onPktData;
	proc.onClose = onIgnoreExcept;

	int nRet = 0;
	DasIO* pIn = NULL;
	for(size_t u = 0; u < uFiles; u++){
		if(pIn != NULL){ del_DasIO(pIn); pIn = NULL; }

		daslog_info_v("Reading: %s", ppFiles[u]);
		pIn = new_DasIO("das3_cache_rdr",
		                new_DasXport_file(ppFiles[u], "r"), NULL);
		if(pIn == NULL)
			continue;

		DasIO_addProcessor(pIn, &proc);
		if((nRet = DasIO_readAll(pIn)) != 0) break;
	}
	if(pIn != NULL) del_DasIO(pIn);
	free(ppFiles);

	if((nRet == 0) && (hdat.nPktsSent == 0))
		sendNoData(&hdat);

	DasIO_close(hdat.pOut);
	if(hdat.pBuf) del_DasBuf(hdat.pBuf);

	daslog_info_v("%d packets sent from %zu cache block(s)",
	              hdat.nPktsSent, uFiles);
	return nRet;
}
