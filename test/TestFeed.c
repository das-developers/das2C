/** @file TestFeed.c Verify the push parser (DasIO_feed) against the pull
 * parser (DasIO_readAll).
 *
 * Each fixture is parsed once with DasIO_readAll and then repeatedly with
 * DasIO_feed at several chunk sizes, including a single byte at a time, so
 * every possible packet-boundary split is exercised.  All handler callbacks
 * fold what they see into a running FNV-1a hash: descriptor info strings,
 * per-record dataset shapes, out-of-band content, and at stream close the
 * full backing-array bytes of every dataset.  The digest from every feed
 * run must equal the pull-mode digest bit for bit.
 */

/* Author: Chris Piker <chris-piker@uiowa.edu>
 *
 * This file is intended to demonstrate an interface.  This is free
 * and unencumbered software released into the public domain.
 *
 * For more information, please refer to <http://unlicense.org/>
 */

#define _POSIX_C_SOURCE 200112L

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <das3/core.h>

/* ************************************************************************* */
/* Running digest, FNV-1a 64-bit */

static uint64_t g_uHash;

static void hashInit(void){ g_uHash = 0xcbf29ce484222325ULL; }

static void hashBytes(const void* pData, size_t uLen)
{
	const unsigned char* p = (const unsigned char*)pData;
	for(size_t u = 0; u < uLen; ++u){
		g_uHash ^= p[u];
		g_uHash *= 0x100000001b3ULL;
	}
}

static void hashStr(const char* s){ if(s != NULL) hashBytes(s, strlen(s)); }

static void hashLong(int64_t n){ hashBytes(&n, sizeof(n)); }

static void hashDouble(double d){ hashBytes(&d, sizeof(d)); }

/* Deliveries per parse, for the streams that end on an exception.  An
 * exception stops reading, so nothing after it may arrive, and the close
 * handler, the one place a reader sees it, runs exactly once. */
static int g_nData;
static int g_nComment;
static int g_nClose;
static int g_nExType;    /* -1 for none */

static void tallyInit(void){ g_nData = g_nComment = g_nClose = 0; g_nExType = -1; }

/* ************************************************************************* */
/* Stream handlers: fold everything seen into the digest */

static das_errcode onStream(DasStream* pSd, void* pUser)
{
	char sBuf[16000] = {'\0'};
	DasStream_toInfoStr(pSd, sBuf, 15999);
	hashStr(sBuf);
	return DAS_OKAY;
}

static das_errcode onDataset(DasDs* pDs, int iPktId, void* pUser)
{
	char sBuf[16000] = {'\0'};
	DasDs_info(pDs, sBuf, 15999);
	hashLong(iPktId);
	hashStr(sBuf);
	return DAS_OKAY;
}

static das_errcode onData(DasDs* pDs, int iPktId, void* pUser)
{
	ptrdiff_t aShape[DASIDX_MAX] = DASIDX_INIT_UNUSED;
	int nRank = DasDs_shape(pDs, aShape);

	++g_nData;
	hashLong(iPktId);
	for(int i = 0; i < nRank; ++i)
		hashLong(aShape[i]);
	return DAS_OKAY;
}

static das_errcode onComment(DasStream* pSd, const DasComment* pCmt, void* pUser)
{
	++g_nComment;
	hashStr(DasComment_get(pCmt, "type"));
	hashStr(DasComment_get(pCmt, "source"));
	hashStr(DasComment_get(pCmt, "value"));
	return DAS_OKAY;
}

/* Fold the complete contents of one array into the digest.  Pure numeric
 * elements hash as raw bytes.  DasTime elements hash through DasTime_toSec1958 and
 * strings hash by content, since both have representations (struct padding,
 * heap pointers) that legitimately differ between two parses of the same
 * input.  Anything else contributes its type and count only. */
static void hashAry(DasAry* pAry)
{
	size_t uVals = 0;
	das_vt vt = DasAry_vt(pAry);

	hashStr(DasAry_id(pAry));
	hashStr(dasvt_name(vt));
	hashLong((int64_t)DasAry_size(pAry));

	if((vt >= DASVT_UBYTE)&&(vt <= DASVT_DOUBLE)){
		const uint8_t* pRaw = DasAry_valsIn(pAry, vt, DAS_ADDR0, &uVals);
		if(pRaw != NULL)
			hashBytes(pRaw, uVals * DasAry_valSize(pAry));
		return;
	}

	if(vt == DASVT_TIME){
		const DasTime* pT = DasAry_timesIn(pAry, DAS_ADDR0, &uVals);
		for(size_t u = 0; pT != NULL && u < uVals; ++u)
			hashDouble(DasTime_toSec1958(pT + u));
		return;
	}

	if(vt == DASVT_TEXT){
		char** pS = (char**)DasAry_valsIn(pAry, DASVT_TEXT, DAS_ADDR0, &uVals);
		for(size_t u = 0; pS != NULL && u < uVals; ++u)
			hashStr(pS[u]);
		return;
	}
}

static das_errcode onClose(DasStream* pSd, const DasExcept* pEx, void* pUser)
{
	char sBuf[16000] = {'\0'};

	++g_nClose;
	if(pEx != NULL){
		g_nExType = (int)DasExcept_type(pEx);
		hashStr(DasExcept_msg(pEx));
	}

	for(int iPktId = 1; (pSd != NULL)&&(iPktId < DASSTREAM_V2_ENDPKTID); ++iPktId){
		DasDesc* pDesc = DasStream_desc(pSd, iPktId);
		if((pDesc == NULL)||(DasDesc_type(pDesc) != DASDESCTYPE_DS))
			continue;

		DasDs* pDs = (DasDs*)pDesc;
		memset(sBuf, 0, sizeof(sBuf));
		DasDs_info(pDs, sBuf, 15999);
		hashLong(iPktId);
		hashStr(sBuf);

		for(size_t u = 0; u < DasDs_numArys(pDs); ++u)
			hashAry(DasDs_aryAt(pDs, u));
	}
	return DAS_OKAY;
}

static void addHandlers(DasIO* pIo, DasIOProc* pHndlr)
{
	init_DasIOProc(pHndlr, NULL);
	pHndlr->onStream = onStream;
	pHndlr->onDs = onDataset;
	pHndlr->onDsData = onData;
	pHndlr->onComment = onComment;
	pHndlr->onClose = onClose;
	DasIO_addProc(pIo, pHndlr);
}

static das_errcode onMonComment(DasStream* pSd, const DasComment* pCmt, void* pUser){ return DAS_OKAY; }

/* A data-less sibling processor, the shape of a progress monitor.  Whether
   records are kept is DasIO_setAccum()'s call alone; an earlier rule keyed
   clearing off which processors had data handlers, and a data-less one wiped
   the records its siblings were accumulating -- identically in pull and feed
   mode, so only a with-vs-without comparison catches a regression. */
static void addMonitor(DasIO* pIo, DasIOProc* pHndlr)
{
	init_DasIOProc(pHndlr, NULL);
	pHndlr->onComment = onMonComment;
	DasIO_addProc(pIo, pHndlr);
}

/* ************************************************************************* */
/* Known skips, same reasoning as the table in TestV3Read.c */

static const char* g_aXfail[][2] = {
	{"ex28_epop_fai_mgf_img.d3b", "no secondary PNG block decoder"},
	{"ex29_ext_contract.d3t", "jpeg extension contract fails loud by design"},
	{"", ""},  /* inert placeholder: keeps a valid non-empty array under -Werror */
};
static const int g_nXfail = sizeof(g_aXfail)/sizeof(g_aXfail[0]);

static const char* baseName(const char* sPath){
	const char* sBase = sPath;          /* strip any directory prefix */
	for(const char* p = sPath; *p; ++p)
		if((*p == '/')||(*p == '\\')) sBase = p + 1;
	return sBase;
}

static const char* xfailReason(const char* sPath){
	const char* sBase = baseName(sPath);
	for(int i = 0; i < g_nXfail; ++i)
		if(strcmp(sBase, g_aXfail[i][0]) == 0) return g_aXfail[i][1];
	return NULL;
}

/* ************************************************************************* */
/* Streams that end on an exception: what a reader must and must not see.
 * Each has data and a comment before the exception, and more of both after
 * it that must never arrive. */

static const struct {
	const char* sBase;
	int nData;       /* data packets before the exception */
	int nComment;    /* comments before the exception */
	das_excepttype nType;
} g_aStop[] = {
	{"das2_except_mid.d2t", 2, 1, DASEXCEPTTYPE_SERVER_ERR},
	{"das3_except_mid.d3t", 3, 1, DASEXCEPTTYPE_SERVER_ERR},
};
static const int g_nStop = sizeof(g_aStop)/sizeof(g_aStop[0]);

/* After a parse: true unless sPath is one of the above and the tallies
 * disagree with it */
static bool stopOkay(const char* sPath, const char* sHow)
{
	const char* sBase = baseName(sPath);
	for(int i = 0; i < g_nStop; ++i){
		if(strcmp(sBase, g_aStop[i].sBase) != 0) continue;
		if((g_nData != g_aStop[i].nData)||(g_nComment != g_aStop[i].nComment)||
			(g_nClose != 1)||(g_nExType != (int)g_aStop[i].nType)){
			printf("ERROR: %s %s: %d data, %d comments, %d closes, exception "
				"type %d; expected %d, %d, 1, %d\n", sPath, sHow, g_nData,
				g_nComment, g_nClose, g_nExType, g_aStop[i].nData,
				g_aStop[i].nComment, (int)g_aStop[i].nType);
			return false;
		}
	}
	return true;
}

/* ************************************************************************* */

static uint8_t* slurp(const char* sPath, size_t* pLen)
{
	FILE* pFile = fopen(sPath, "rb");
	if(pFile == NULL) return NULL;
	fseek(pFile, 0, SEEK_END);
	long nLen = ftell(pFile);
	fseek(pFile, 0, SEEK_SET);
	uint8_t* pData = (uint8_t*)malloc((nLen > 0) ? nLen : 1);
	if(fread(pData, 1, nLen, pFile) != (size_t)nLen){
		free(pData);
		fclose(pFile);
		return NULL;
	}
	fclose(pFile);
	*pLen = (size_t)nLen;
	return pData;
}

/* Parse via DasIO_readAll.  Returns the parser result and leaves the digest
 * in g_uHash. */
static das_errcode pullParse(const char* sPath, bool bMonitor)
{
	FILE* pFile = fopen(sPath, "rb");
	DasIO* pIo = new_DasIO("TestFeed", new_DasXport_cfile(pFile, "r"), NULL);
	DasIO_setModel(pIo, 3);   /* up-convert das2 so one handler set covers all */
	DasIO_setAccum(pIo, true);  /* hash the growing shapes, not just one packet */

	DasIOProc handler, monitor;
	addHandlers(pIo, &handler);
	if(bMonitor)
		addMonitor(pIo, &monitor);

	hashInit();
	tallyInit();
	das_errcode nRet = DasIO_readAll(pIo);
	del_DasIO(pIo);
	fclose(pFile);    /* borrowed, see new_DasXport_cfile() */
	return nRet;
}

/* Parse via DasIO_feed in uChunk sized pieces.  Returns the parser result
 * and leaves the digest in g_uHash. */
static das_errcode feedParse(const uint8_t* pData, size_t uLen, size_t uChunk)
{
	DasIO* pIo = new_DasIO_feed("TestFeed", "r");
	DasIO_setModel(pIo, 3);
	DasIO_setAccum(pIo, true);

	DasIOProc handler, monitor;
	addHandlers(pIo, &handler);
	addMonitor(pIo, &monitor);

	hashInit();
	tallyInit();
	das_errcode nRet = DAS_OKAY;
	for(size_t u = 0; u < uLen && nRet == DAS_OKAY; u += uChunk){
		size_t uThis = (uLen - u < uChunk) ? uLen - u : uChunk;
		nRet = DasIO_feed(pIo, pData + u, uThis);
	}
	das_errcode nEndRet = DasIO_feedEnd(pIo);
	if(nRet == DAS_OKAY) nRet = nEndRet;
	del_DasIO(pIo);
	return nRet;
}

/* Feed-mode behaviors no well-formed stream file can trigger */
static int selfChecks(void)
{
	/* An un-packetized document accumulates in the framing layer and is
	   cleanly refused at end of input, see _DasIO_handleDoc in io.c.  When
	   das-basic-doc parsing lands this check inverts. */
	const char* sDoc = "<stream version=\"3.0\"><properties/></stream>";
	DasIO* pIo = new_DasIO_feed("TestFeed", "r");
	if(DasIO_feed(pIo, (const uint8_t*)sDoc, strlen(sDoc)) != DAS_OKAY){
		printf("ERROR: document bytes refused by the framing layer\n");
		return 1;
	}
	if(DasIO_feedEnd(pIo) == DAS_OKAY){
		printf("ERROR: expected a not-implemented error for document parsing\n");
		return 1;
	}
	del_DasIO(pIo);

	/* Abandon a parse mid-packet, del_DasIO has to clean up alone */
	const char* sFrag = "|Sx||100|<stream";
	pIo = new_DasIO_feed("TestFeed", "r");
	if(DasIO_feed(pIo, (const uint8_t*)sFrag, strlen(sFrag)) != DAS_OKAY){
		printf("ERROR: partial packet refused before end of input\n");
		return 1;
	}
	del_DasIO(pIo);   /* no DasIO_feedEnd on purpose */

	/* Garbage latches an error that repeats on every later call */
	pIo = new_DasIO_feed("TestFeed", "r");
	das_errcode nRet = DasIO_feed(pIo, (const uint8_t*)"garbage!", 8);
	if(nRet == DAS_OKAY){
		printf("ERROR: garbage input accepted\n");
		return 1;
	}
	if(DasIO_feed(pIo, (const uint8_t*)"more", 4) != nRet){
		printf("ERROR: feed error did not latch\n");
		return 1;
	}
	(void)DasIO_feedEnd(pIo);
	del_DasIO(pIo);

	return 0;
}

/* ************************************************************************* */
/* Packets bigger than DasIO's starting buffer, and the length field rules.
 *
 * DasIO's buffer starts at 16 KiB and grows to at most 16 MiB
 * (DASIO_INIT_PKT_SZ and DASIO_MAX_PKT_SZ in io.c).  No fixture is that
 * large, so these streams are written here, through DasIO, into memory, then
 * read back by both parsers. */

static size_t g_uBigLen;    /* total length of the bigN stream properties */
static size_t g_uCmtLen;    /* length of the last comment value seen */

/* A single property value is capped near 128 KiB (DASPROP_VMAX_SZ in
   prop.c), so big headers are built from several of them */
#define BIG_MAX_PROPS 64

static das_errcode onBigStream(DasStream* pSd, void* pUser)
{
	(void)pUser;
	char sName[16];
	g_uBigLen = 0;
	for(int i = 0; i < BIG_MAX_PROPS; ++i){
		snprintf(sName, sizeof(sName), "big%d", i);
		const char* s = DasDesc_val((DasDesc*)pSd, sName);
		if(s != NULL) g_uBigLen += strlen(s);
	}
	return DAS_OKAY;
}

static das_errcode onBigComment(DasStream* pSd, const DasComment* pCmt, void* pUser)
{
	(void)pSd; (void)pUser;
	const char* s = DasComment_get(pCmt, "value");
	g_uCmtLen = (s != NULL) ? strlen(s) : 0;
	return DAS_OKAY;
}

static char* patText(size_t uLen)
{
	char* s = (char*)malloc(uLen + 1);
	for(size_t u = 0; u < uLen; ++u) s[u] = 'a' + (char)(u % 26);
	s[uLen] = '\0';
	return s;
}

/* Write a stream header carrying uProp bytes of property text, split into
   pieces of at most 60000, and if uCmt > 0 a comment that long, into pOut.
   Returns the writer's result; *pWrote is the stream length (the output is
   text, pOut starts zeroed). */
static das_errcode writeBig(
	const char* sMode, size_t uProp, size_t uCmt, char* pOut, size_t uOutSz,
	size_t* pWrote
){
	memset(pOut, 0, uOutSz);
	DasIO* pIo = new_DasIO("TestFeed", new_DasXport_mem(pOut, uOutSz - 1, "w"), sMode);
	DasStream* pSd = new_DasStream();
	char* sProp = patText(uProp);
	char sName[16];
	size_t uDone = 0;
	for(int i = 0; (uDone < uProp)&&(i < BIG_MAX_PROPS); ++i){
		size_t uPiece = (uProp - uDone > 60000) ? 60000 : uProp - uDone;
		char cSave = sProp[uDone + uPiece];
		sProp[uDone + uPiece] = '\0';
		snprintf(sName, sizeof(sName), "big%d", i);
		DasDesc_setVal((DasDesc*)pSd, sName, sProp + uDone);
		sProp[uDone + uPiece] = cSave;
		uDone += uPiece;
	}

	das_errcode nRet = DasIO_writeStreamDesc(pIo, pSd);
	if((nRet == DAS_OKAY)&&(uCmt > 0)){
		char* sCmt = patText(uCmt);
		DasComment* pCmt = new_DasComment();
		DasComment_set(pCmt, "type", "log:info");
		DasComment_set(pCmt, "value", sCmt);
		nRet = DasIO_comment(pIo, pCmt);
		del_DasComment(pCmt);
		free(sCmt);
	}
	del_DasIO(pIo);
	del_DasDesc((DasDesc*)pSd);
	free(sProp);
	*pWrote = strlen(pOut);
	return nRet;
}

/* Read a stream from memory; uChunk 0 is pull mode, else feed mode in
   uChunk pieces.  Leaves what was seen in g_uBigLen and g_uCmtLen. */
static das_errcode readBig(const char* pData, size_t uLen, size_t uChunk)
{
	DasIO* pIo = (uChunk == 0) ?
		new_DasIO("TestFeed", new_DasXport_mem((void*)pData, uLen, "r"), NULL) :
		new_DasIO_feed("TestFeed", "r");
	DasIO_setModel(pIo, 3);   /* das3 headers, and das2 ones up-converted */

	DasIOProc handler;
	init_DasIOProc(&handler, NULL);
	handler.onStream = onBigStream;
	handler.onComment = onBigComment;
	DasIO_addProc(pIo, &handler);

	g_uBigLen = g_uCmtLen = 0;
	das_errcode nRet = DAS_OKAY;
	if(uChunk == 0)
		nRet = DasIO_readAll(pIo);
	else{
		for(size_t u = 0; u < uLen && nRet == DAS_OKAY; u += uChunk){
			size_t uThis = (uLen - u < uChunk) ? uLen - u : uChunk;
			nRet = DasIO_feed(pIo, (const uint8_t*)pData + u, uThis);
		}
		das_errcode nEndRet = DasIO_feedEnd(pIo);
		if(nRet == DAS_OKAY) nRet = nEndRet;
	}
	del_DasIO(pIo);
	return nRet;
}

/* Every read mode must see exactly uProp and uCmt */
static int readBigAll(const char* sWhat, const char* pData, size_t uLen,
                      size_t uProp, size_t uCmt)
{
	const size_t aChunks[] = {0 /* pull */, 7, 4096, 65536, 1048576};
	int nBad = 0;
	for(size_t i = 0; i < sizeof(aChunks)/sizeof(aChunks[0]); ++i){
		das_errcode nRet = readBig(pData, uLen, aChunks[i]);
		if((nRet != DAS_OKAY)||(g_uBigLen != uProp)||(g_uCmtLen != uCmt)){
			printf("ERROR: %s, %s %zu: ret %d, property %zu of %zu, comment %zu of "
			       "%zu bytes\n", sWhat, aChunks[i] ? "feed chunk" : "pull", aChunks[i],
			       nRet, g_uBigLen, uProp, g_uCmtLen, uCmt);
			++nBad;
		}
	}
	return nBad;
}

/* Both parsers must refuse sStream, a packet tag that breaks a rule */
static int refuseBoth(const char* sWhy, const char* sStream)
{
	size_t uLen = strlen(sStream);
	int nBad = 0;
	if(readBig(sStream, uLen, 0) == DAS_OKAY){
		printf("ERROR: pull parser accepted %s: %s\n", sWhy, sStream); ++nBad;
	}
	if(readBig(sStream, uLen, 3) == DAS_OKAY){
		printf("ERROR: feed parser accepted %s: %s\n", sWhy, sStream); ++nBad;
	}
	return nBad;
}

static int bigPacketChecks(void)
{
	int nBad = 0;
	size_t uOutSz = 4 * 1048576;
	char* pOut = (char*)malloc(uOutSz);
	size_t uWrote = 0;

	/* Past the old fixed 256 KiB buffer, header and comment both */
	if(writeBig("w3", 300000, 100000, pOut, uOutSz, &uWrote) != DAS_OKAY){
		printf("ERROR: das3 writer refused a 300000 byte header\n"); ++nBad;
	}
	/* The framing layer reads the whole 100 KB comment packet, but das3
	   comment text is collected in a 4096 byte buffer (oob.c) and cut to
	   4095 bytes.  Known limit, pinned here so a fix has to update it. */
	else nBad += readBigAll("das3, 300 KB header + 100 KB comment", pOut, uWrote,
	                        300000, 4095);

	if(writeBig("w2", 300000, 100000, pOut, uOutSz, &uWrote) != DAS_OKAY){
		printf("ERROR: das2 writer refused a 300000 byte header\n"); ++nBad;
	}
	else nBad += readBigAll("das2, 300 KB header + 100 KB comment", pOut, uWrote,
	                        300000, 100000);

	/* das2 lengths are six digits, a bigger header can't be framed */
	if(writeBig("w2", 1000000, 0, pOut, uOutSz, &uWrote) == DAS_OKAY){
		printf("ERROR: das2 writer framed a header over 999999 bytes\n"); ++nBad;
	}
	if(writeBig("w3", 1000000, 0, pOut, uOutSz, &uWrote) != DAS_OKAY){
		printf("ERROR: das3 writer refused a 1 MB header\n"); ++nBad;
	}
	else nBad += readBigAll("das3, 1 MB header", pOut, uWrote, 1000000, 0);

	/* Headers that end exactly on a buffer size leave no NUL behind the
	   text, the writer has to frame them by length.  Measure the encoding
	   overhead once, then land on 16 KiB, one past it, and 32 KiB. */
	size_t uBody = 0;
	writeBig("w3", 100, 0, pOut, uOutSz, &uWrote);
	if(sscanf(pOut, "|Sx||%zu|", &uBody) != 1){
		printf("ERROR: no das3 stream tag in: %.40s\n", pOut); ++nBad;
	}
	else{
		size_t uOver = uBody - 100;
		const size_t aTarget[] = {16384, 16385, 32768};
		for(size_t i = 0; i < 3; ++i){
			size_t uProp = aTarget[i] - uOver;
			size_t uGot = 0;
			writeBig("w3", uProp, 0, pOut, uOutSz, &uWrote);
			if((sscanf(pOut, "|Sx||%zu|", &uGot) != 1)||(uGot != aTarget[i])){
				printf("ERROR: expected a %zu byte header, got %zu\n", aTarget[i], uGot);
				++nBad;
				continue;
			}
			char sWhat[64];
			snprintf(sWhat, sizeof(sWhat), "das3, header of exactly %zu bytes", uGot);
			nBad += readBigAll(sWhat, pOut, uWrote, uProp, 0);
		}
	}
	free(pOut);

	/* Length field: leading zeros are padding, the value is what counts */
	const char* sHdr = "<stream type=\"das-basic-stream\" version=\"3.0\" />";
	char sStream[128];
	snprintf(sStream, sizeof(sStream), "|Sx||%010zu|%s", strlen(sHdr), sHdr);
	nBad += readBigAll("das3, zero padded 10 digit length", sStream, strlen(sStream), 0, 0);

	snprintf(sStream, sizeof(sStream), "|Sx||%015zu|%s", strlen(sHdr), sHdr);
	nBad += readBigAll("das3, 21 byte tag, 15 digit length", sStream, strlen(sStream), 0, 0);
	snprintf(sStream, sizeof(sStream), "|Sx|00000|%010zu|%s", strlen(sHdr), sHdr);
	nBad += readBigAll("das3, stream ID 0 zero padded", sStream, strlen(sStream), 0, 0);
	snprintf(sStream, sizeof(sStream), "|Sx||%016zu|%s", strlen(sHdr), sHdr);
	nBad += refuseBoth("a 22 byte tag", sStream);
	snprintf(sStream, sizeof(sStream), "|Sx|1|%zu|%s", strlen(sHdr), sHdr);
	nBad += refuseBoth("a stream header with ID 1", sStream);
	nBad += refuseBoth("a signed length", "|Sx||+49|");
	nBad += refuseBoth("a negative length", "|Sx||-49|");
	nBad += refuseBoth("a space in the length", "|Sx|| 49|");
	/* The published limits are the ones the parsers enforce */
	const DasLimit* pPktMax = DasLimit_lookup("max_packet_bytes");
	const DasLimit* pIdMax  = DasLimit_lookup("das3_max_packet_id");
	if((pPktMax == NULL)||(pIdMax == NULL)){
		printf("ERROR: packet size or ID limit missing from DasLimit_lookup()\n");
		return nBad + 1;
	}
	char sTag[64];
	snprintf(sTag, sizeof(sTag), "|Sx||%d|", (int)pPktMax->value + 1);
	nBad += refuseBoth("a length one past max_packet_bytes", sTag);
	nBad += refuseBoth("a 10 digit length past the limit", "|Sx||9999999999|");

	/* Packet IDs after a good stream header.  Refusal comes at the tag, so
	   the bodies don't need to be valid. */
	static const char* aBadTags[][2] = {
		{"a header without an ID",       "|Hx||5|<xx/>"},
		{"a header with ID 0",           "|Hx|0|5|<xx/>"},
		{"a signed data ID",             "|Pd|+1|5|abcde"},
		{"a data ID with a space",       "|Pd| 1|5|abcde"},
		{"a comment with an ID",         "|Cx|1|5|<xx/>"},
		{"an exception with ID 0",       "|Ex|0|5|<xx/>"},
		{"a 16 byte ID field",           "|Hx|000000000000001|1|x"}
	};
	for(size_t i = 0; i < sizeof(aBadTags)/sizeof(aBadTags[0]); ++i){
		snprintf(sStream, sizeof(sStream), "|Sx||%zu|%s%s", strlen(sHdr), sHdr,
		         aBadTags[i][1]);
		nBad += refuseBoth(aBadTags[i][0], sStream);
	}
	snprintf(sStream, sizeof(sStream), "|Sx||%zu|%s|Hx|%d|5|<xx/>", strlen(sHdr),
	         sHdr, (int)pIdMax->value + 1);
	nBad += refuseBoth("a header ID one past das3_max_packet_id", sStream);

	if(nBad == 0)
		printf("INFO: Large packet, length field and packet ID checks passed\n");
	return nBad;
}

/* Rewrite every das3 tag of a stream at the widest legal padding: a five
   digit ID (none stays none) and a ten digit length, so each tag with an
   ID is exactly 21 bytes.  Walks packet by packet, never matches text in
   bodies.  Returns NULL if the input isn't a well formed das3 stream. */
static char* padTags(const char* pIn, size_t uIn, size_t* pOut)
{
	char* pOutBuf = (char*)malloc(uIn + 64 * (uIn / 8 + 1));  /* tags grow, bodies don't */
	size_t uPos = 0, uOut = 0;
	while(uPos < uIn){
		char sId[32] = {'\0'}, sLen[32] = {'\0'};
		if((uIn - uPos < 6)||(pIn[uPos] != '|')||(pIn[uPos + 3] != '|')) goto fail;
		size_t u = uPos + 4, n = 0;
		while((u < uIn)&&(pIn[u] != '|')&&(n < 31)) sId[n++] = pIn[u++];
		if((u >= uIn)||(pIn[u] != '|')) goto fail;
		++u; n = 0;
		while((u < uIn)&&(pIn[u] != '|')&&(n < 31)) sLen[n++] = pIn[u++];
		if((u >= uIn)||(pIn[u] != '|')) goto fail;
		++u;
		size_t uBody = (size_t)strtoul(sLen, NULL, 10);
		if(u + uBody > uIn) goto fail;

		if(sId[0] == '\0')
			uOut += sprintf(pOutBuf + uOut, "|%c%c||%010zu|", pIn[uPos+1], pIn[uPos+2], uBody);
		else
			uOut += sprintf(pOutBuf + uOut, "|%c%c|%05d|%010zu|", pIn[uPos+1], pIn[uPos+2],
			                atoi(sId), uBody);
		memcpy(pOutBuf + uOut, pIn + u, uBody);
		uOut += uBody;
		uPos = u + uBody;
	}
	*pOut = uOut;
	return pOutBuf;
fail:
	free(pOutBuf);
	return NULL;
}

/* Pull parse from memory, digest in g_uHash, same handlers as pullParse() */
static das_errcode pullMemParse(const uint8_t* pData, size_t uLen)
{
	DasIO* pIo = new_DasIO("TestFeed", new_DasXport_mem((void*)pData, uLen, "r"), NULL);
	DasIO_setModel(pIo, 3);
	DasIO_setAccum(pIo, true);
	DasIOProc handler;
	addHandlers(pIo, &handler);
	hashInit();
	tallyInit();
	das_errcode nRet = DasIO_readAll(pIo);
	del_DasIO(pIo);
	return nRet;
}

/* Padding is only spelling: a fixture with every tag padded to the 21 byte
   limit must parse to the same digest as the original, both parsers */
static int paddedTagChecks(const char* sPath)
{
	size_t uLen = 0, uPad = 0;
	uint8_t* pData = slurp(sPath, &uLen);
	if(pData == NULL){ printf("ERROR: Couldn't read %s\n", sPath); return 1; }
	char* pPad = padTags((const char*)pData, uLen, &uPad);
	if(pPad == NULL){
		printf("ERROR: %s is not a well formed das3 stream\n", sPath);
		free(pData);
		return 1;
	}

	int nBad = 0;
	if(pullMemParse(pData, uLen) != DAS_OKAY){
		printf("ERROR: %s did not parse\n", sPath); ++nBad;
	}
	uint64_t uWant = g_uHash;
	if((pullMemParse((const uint8_t*)pPad, uPad) != DAS_OKAY)||(g_uHash != uWant)){
		printf("ERROR: %s with padded tags, pull parse differs\n", sPath); ++nBad;
	}
	const size_t aChunks[] = {1, 7, 4096, 0};
	for(size_t i = 0; i < sizeof(aChunks)/sizeof(aChunks[0]); ++i){
		size_t uChunk = aChunks[i] ? aChunks[i] : uPad;
		if((feedParse((const uint8_t*)pPad, uPad, uChunk) != DAS_OKAY)||(g_uHash != uWant)){
			printf("ERROR: %s with padded tags, feed parse differs at chunk %zu\n",
			       sPath, uChunk);
			++nBad;
		}
	}
	free(pPad);
	free(pData);
	if(nBad == 0)
		printf("INFO: %s parses the same with every tag padded to 21 bytes\n", sPath);
	return nBad;
}

int main(int argc, char** argv)
{
	size_t aChunks[] = {1, 3, 7, 64, 4096, 1048576, 0 /* 0 = whole input */};
	const int nChunks = sizeof(aChunks)/sizeof(aChunks[0]);

	/* Log errors and return codes, many fixtures are fed at hostile chunk
	   sizes and the failures have to flow back up to this harness */
	daslib_init(argv[0], DASERRDISP_RET, DASLOG_ERROR, NULL);

	if(argc < 2){
		fprintf(stderr, "Usage: %s STREAM_FILE [STREAM_FILE ...]\n", argv[0]);
		return 13;
	}

	printf("INFO: ERROR log lines below are intentional, provoked by negative checks\n");
	if(selfChecks() != 0)
		return 64;
	if(bigPacketChecks() != 0)
		return 64;
	if(paddedTagChecks("examples/ex12_sounder_xyz.d3t") != 0)
		return 64;

	int nFailed = 0;
	for(int i = 1; i < argc; ++i){

		const char* sXfail = xfailReason(argv[i]);
		if(sXfail != NULL){
			printf("INFO: Skipping %s (known xfail: %s)\n", argv[i], sXfail);
			continue;
		}

		size_t uLen = 0;
		uint8_t* pData = slurp(argv[i], &uLen);
		if(pData == NULL){
			printf("ERROR: Couldn't read %s\n", argv[i]);
			return 64;
		}

		das_errcode nPullRet = pullParse(argv[i], false);
		uint64_t uPullHash = g_uHash;

		bool bOk = stopOkay(argv[i], "pull");

		/* A data-less sibling processor must not perturb anything.  Feed
		   mode runs always carry the monitor, so this plus the pull-vs-feed
		   digest checks below covers both parse modes. */
		if(nPullRet == DAS_OKAY){
			if((pullParse(argv[i], true) != DAS_OKAY)||(g_uHash != uPullHash)){
				printf("ERROR: %s results changed when a data-less monitor "
					"processor was attached\n", argv[i]);
				bOk = false;
			}
		}
		for(int j = 0; j < nChunks; ++j){
			size_t uChunk = (aChunks[j] == 0) ? uLen : aChunks[j];

			das_errcode nFeedRet = feedParse(pData, uLen, uChunk);

			if(nPullRet != DAS_OKAY){
				/* Pull couldn't parse it: feed must refuse it too, but the
				   digests of two half-finished parses aren't comparable */
				if(nFeedRet == DAS_OKAY){
					printf("ERROR: %s pull parse failed (%d) but feed parse "
						"passed at chunk size %zu\n", argv[i], nPullRet, uChunk);
					bOk = false;
				}
				continue;
			}

			if(nFeedRet != DAS_OKAY){
				printf("ERROR: %s feed parse failed (%d) at chunk size %zu\n",
					argv[i], nFeedRet, uChunk);
				bOk = false;
				continue;
			}
			char sHow[48];
			snprintf(sHow, sizeof(sHow), "feed, chunk size %zu", uChunk);
			if(!stopOkay(argv[i], sHow)) bOk = false;
			if(g_uHash != uPullHash){
				printf("ERROR: %s digest mismatch at chunk size %zu "
					"(pull %016llx, feed %016llx)\n", argv[i], uChunk,
					(unsigned long long)uPullHash, (unsigned long long)g_uHash);
				bOk = false;
			}
		}
		free(pData);

		if(bOk){
			if(nPullRet == DAS_OKAY)
				printf("INFO: %s pull/feed digests match at all chunk sizes\n", argv[i]);
			else
				printf("INFO: %s rejected by both pull and feed parsers\n", argv[i]);
		}
		else
			++nFailed;
	}

	if(nFailed > 0){
		printf("ERROR: %d of %d stream files failed pull/feed comparison\n",
			nFailed, argc - 1);
		return 64;
	}
	printf("INFO: All feed mode checks passed\n");
	return 0;
}
