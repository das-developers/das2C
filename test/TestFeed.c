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

/* ************************************************************************* */
/* Stream handlers: fold everything seen into the digest */

static DasErrCode onStream(DasStream* pSd, void* pUser)
{
	char sBuf[16000] = {'\0'};
	DasStream_info(pSd, sBuf, 15999);
	hashStr(sBuf);
	return DAS_OKAY;
}

static DasErrCode onDataset(DasStream* pSd, int iPktId, DasDs* pDs, void* pUser)
{
	char sBuf[16000] = {'\0'};
	DasDs_info(pDs, sBuf, 15999);
	hashLong(iPktId);
	hashStr(sBuf);
	return DAS_OKAY;
}

static DasErrCode onData(DasStream* pSd, int iPktId, DasDs* pDs, void* pUser)
{
	ptrdiff_t aShape[VARIDX_MAX] = VARIDX_INIT_UNUSED;
	int nRank = DasDs_shape(pDs, aShape);

	hashLong(iPktId);
	for(int i = 0; i < nRank; ++i)
		hashLong(aShape[i]);
	return DAS_OKAY;
}

static DasErrCode onComment(OobComment* pCmt, void* pUser)
{
	hashStr(pCmt->sType);
	hashStr(pCmt->sSrc);
	hashStr(pCmt->sVal);
	return DAS_OKAY;
}

static DasErrCode onExcept(OobExcept* pEx, void* pUser)
{
	hashStr(pEx->sMsg);
	return DAS_OKAY;
}

/* Fold the complete contents of one array into the digest.  Pure numeric
 * elements hash as raw bytes.  das_time elements hash through dt_ttime and
 * strings hash by content, since both have representations (struct padding,
 * heap pointers) that legitimately differ between two parses of the same
 * input.  Anything else contributes its type and count only. */
static void hashAry(DasAry* pAry)
{
	size_t uVals = 0;
	das_val_type vt = DasAry_valType(pAry);

	hashStr(DasAry_id(pAry));
	hashStr(das_vt_toStr(vt));
	hashLong((int64_t)DasAry_size(pAry));

	if((vt >= vtUByte)&&(vt <= vtDouble)){
		const ubyte* pRaw = DasAry_getIn(pAry, vt, DIM0, &uVals);
		if(pRaw != NULL)
			hashBytes(pRaw, uVals * DasAry_valSize(pAry));
		return;
	}

	if(vt == vtTime){
		const das_time* pT = DasAry_getTimesIn(pAry, DIM0, &uVals);
		for(size_t u = 0; pT != NULL && u < uVals; ++u)
			hashDouble(dt_ttime(pT + u));
		return;
	}

	if(vt == vtText){
		char** pS = (char**)DasAry_getIn(pAry, vtText, DIM0, &uVals);
		for(size_t u = 0; pS != NULL && u < uVals; ++u)
			hashStr(pS[u]);
		return;
	}
}

static DasErrCode onClose(DasStream* pSd, void* pUser)
{
	char sBuf[16000] = {'\0'};

	for(int iPktId = 1; iPktId < MAX_PKTIDS; ++iPktId){
		DasDesc* pDesc = DasStream_getDesc(pSd, iPktId);
		if((pDesc == NULL)||(pDesc->type != DATASET))
			continue;

		DasDs* pDs = (DasDs*)pDesc;
		memset(sBuf, 0, sizeof(sBuf));
		DasDs_info(pDs, sBuf, 15999);
		hashLong(iPktId);
		hashStr(sBuf);

		for(size_t u = 0; u < DasDs_numAry(pDs); ++u)
			hashAry(DasDs_getAry(pDs, u));
	}
	return DAS_OKAY;
}

static void addHandlers(DasIO* pIo, StreamHandler* pHndlr)
{
	memset(pHndlr, 0, sizeof(StreamHandler));
	pHndlr->streamDescHandler = onStream;
	pHndlr->dsDescHandler = onDataset;
	pHndlr->dsDataHandler = onData;
	pHndlr->commentHandler = onComment;
	pHndlr->exceptionHandler = onExcept;
	pHndlr->closeHandler = onClose;
	DasIO_addProcessor(pIo, pHndlr);
}

static DasErrCode onMonComment(OobComment* pCmt, void* pUser){ return DAS_OKAY; }

/* A data-less sibling processor, the shape of a progress monitor.  Before
   the bClearDs fix in _DasIO_handleData a processor with no dsDataHandler
   wiped the records every OTHER processor was accumulating -- identically
   in pull and feed mode, so only a with-vs-without comparison catches a
   regression. */
static void addMonitor(DasIO* pIo, StreamHandler* pHndlr)
{
	memset(pHndlr, 0, sizeof(StreamHandler));
	pHndlr->commentHandler = onMonComment;
	DasIO_addProcessor(pIo, pHndlr);
}

/* ************************************************************************* */
/* Known skips, same reasoning as the table in TestV3Read.c */

static const char* g_aXfail[][2] = {
	{"ex28_epop_fai_mgf_img.d3b", "no secondary PNG block decoder"},
	{"ex29_ext_contract.d3t", "jpeg extension contract fails loud by design"},
	{"", ""},  /* inert placeholder: keeps a valid non-empty array under -Werror */
};
static const int g_nXfail = sizeof(g_aXfail)/sizeof(g_aXfail[0]);

static const char* xfailReason(const char* sPath){
	const char* sBase = sPath;          /* strip any directory prefix */
	for(const char* p = sPath; *p; ++p)
		if((*p == '/')||(*p == '\\')) sBase = p + 1;
	for(int i = 0; i < g_nXfail; ++i)
		if(strcmp(sBase, g_aXfail[i][0]) == 0) return g_aXfail[i][1];
	return NULL;
}

/* ************************************************************************* */

static ubyte* slurp(const char* sPath, size_t* pLen)
{
	FILE* pFile = fopen(sPath, "rb");
	if(pFile == NULL) return NULL;
	fseek(pFile, 0, SEEK_END);
	long nLen = ftell(pFile);
	fseek(pFile, 0, SEEK_SET);
	ubyte* pData = (ubyte*)malloc((nLen > 0) ? nLen : 1);
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
static DasErrCode pullParse(const char* sPath, bool bMonitor)
{
	FILE* pFile = fopen(sPath, "rb");
	DasIO* pIo = new_DasIO_cfile("TestFeed", pFile, "r");
	DasIO_model(pIo, 3);   /* up-convert das2 so one handler set covers all */

	StreamHandler handler, monitor;
	addHandlers(pIo, &handler);
	if(bMonitor)
		addMonitor(pIo, &monitor);

	hashInit();
	DasErrCode nRet = DasIO_readAll(pIo);
	del_DasIO(pIo);   /* closes pFile too */
	return nRet;
}

/* Parse via DasIO_feed in uChunk sized pieces.  Returns the parser result
 * and leaves the digest in g_uHash. */
static DasErrCode feedParse(const ubyte* pData, size_t uLen, size_t uChunk)
{
	DasIO* pIo = new_DasIO_feed("TestFeed", "r");
	DasIO_model(pIo, 3);

	StreamHandler handler, monitor;
	addHandlers(pIo, &handler);
	addMonitor(pIo, &monitor);

	hashInit();
	DasErrCode nRet = DAS_OKAY;
	for(size_t u = 0; u < uLen && nRet == DAS_OKAY; u += uChunk){
		size_t uThis = (uLen - u < uChunk) ? uLen - u : uChunk;
		nRet = DasIO_feed(pIo, pData + u, uThis);
	}
	DasErrCode nEndRet = DasIO_feedEnd(pIo);
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
	if(DasIO_feed(pIo, (const ubyte*)sDoc, strlen(sDoc)) != DAS_OKAY){
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
	if(DasIO_feed(pIo, (const ubyte*)sFrag, strlen(sFrag)) != DAS_OKAY){
		printf("ERROR: partial packet refused before end of input\n");
		return 1;
	}
	del_DasIO(pIo);   /* no DasIO_feedEnd on purpose */

	/* Garbage latches an error that repeats on every later call */
	pIo = new_DasIO_feed("TestFeed", "r");
	DasErrCode nRet = DasIO_feed(pIo, (const ubyte*)"garbage!", 8);
	if(nRet == DAS_OKAY){
		printf("ERROR: garbage input accepted\n");
		return 1;
	}
	if(DasIO_feed(pIo, (const ubyte*)"more", 4) != nRet){
		printf("ERROR: feed error did not latch\n");
		return 1;
	}
	(void)DasIO_feedEnd(pIo);
	del_DasIO(pIo);

	return 0;
}

int main(int argc, char** argv)
{
	size_t aChunks[] = {1, 3, 7, 64, 4096, 1048576, 0 /* 0 = whole input */};
	const int nChunks = sizeof(aChunks)/sizeof(aChunks[0]);

	/* Log errors and return codes, many fixtures are fed at hostile chunk
	   sizes and the failures have to flow back up to this harness */
	das_init(argv[0], DASERR_DIS_RET, 0, DASLOG_ERROR, NULL);

	if(argc < 2){
		fprintf(stderr, "Usage: %s STREAM_FILE [STREAM_FILE ...]\n", argv[0]);
		return 13;
	}

	printf("INFO: ERROR log lines below are intentional, provoked by negative checks\n");
	if(selfChecks() != 0)
		return 64;

	int nFailed = 0;
	for(int i = 1; i < argc; ++i){

		const char* sXfail = xfailReason(argv[i]);
		if(sXfail != NULL){
			printf("INFO: Skipping %s (known xfail: %s)\n", argv[i], sXfail);
			continue;
		}

		size_t uLen = 0;
		ubyte* pData = slurp(argv[i], &uLen);
		if(pData == NULL){
			printf("ERROR: Couldn't read %s\n", argv[i]);
			return 64;
		}

		DasErrCode nPullRet = pullParse(argv[i], false);
		uint64_t uPullHash = g_uHash;

		bool bOk = true;

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

			DasErrCode nFeedRet = feedParse(pData, uLen, uChunk);

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
