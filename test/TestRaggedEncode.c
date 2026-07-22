/** @file TestRaggedEncode.c Byte-exact re-encode of ragged binary fixtures.
 *
 * The first regression coverage of DasDs_encodeData's BINARY ragged paths,
 * especially the "[idx|N]" count-tag writer.  Each fixture's |Pd| payloads are
 * captured straight from the file bytes, the stream is parsed into a dataset,
 * every codec is flipped to a writer, and each record is re-encoded and
 * memcmp'd against the original payload -- so the committed fixtures serve as
 * the encode gold with no extra files.
 */

/* Author: Chris Piker <chris-piker@uiowa.edu>, via Claude Fable 5
 *
 * As with the other test programs, this file is placed into the public domain
 * and may be displayed, incorporated or otherwise re-used without restriction.
 * It is offered to the public without any warranty including the implied
 * warranty of merchantability or fitness for a particular purpose.
 */

#define _POSIX_C_SOURCE 200112L

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <das2/core.h>

#define MAX_PKTS    64
#define MAX_PAYLOAD 4096

static ubyte g_aPayload[MAX_PKTS][MAX_PAYLOAD];
static int   g_aPayLen[MAX_PKTS];
static int   g_nPkts;

static DasDs* g_pDs;
static int    g_nRecs;

/* Scan the raw file bytes for |Pd| packets; a das3 tag is |Tt|id|len| with a
   possibly-empty id field.  Runs before the parser so the comparison target
   can't be contaminated by the code under test. */
static int _loadPayloads(const char* sPath)
{
	FILE* pFile = fopen(sPath, "rb");
	if(pFile == NULL){ printf("ERROR: Couldn't open %s\n", sPath); return 13; }
	static ubyte aRaw[262144];
	size_t uRaw = fread(aRaw, 1, sizeof(aRaw), pFile);
	fclose(pFile);

	size_t i = 0;
	while(i < uRaw){
		if(aRaw[i] != '|'){ printf("ERROR: Bad tag framing at offset %zu\n", i); return 13; }
		char cT0 = (char)aRaw[i+1], cT1 = (char)aRaw[i+2];
		i += 3;
		if(aRaw[i] != '|'){ printf("ERROR: Bad tag at offset %zu\n", i); return 13; }
		++i;
		while((i < uRaw)&&(aRaw[i] != '|')) ++i;   /* id field (may be empty) */
		++i;
		long nLen = 0;
		while((i < uRaw)&&(aRaw[i] != '|')){ nLen = nLen*10 + (aRaw[i]-'0'); ++i; }
		++i;
		if((cT0 == 'P')&&(cT1 == 'd')){
			if((g_nPkts >= MAX_PKTS)||(nLen > MAX_PAYLOAD)){
				printf("ERROR: Fixture larger than test limits\n"); return 13;
			}
			memcpy(g_aPayload[g_nPkts], aRaw + i, (size_t)nLen);
			g_aPayLen[g_nPkts] = (int)nLen;
			++g_nPkts;
		}
		i += (size_t)nLen;
	}
	return 0;
}

static DasErrCode onDataset(StreamDesc* pSd, int iPktId, DasDs* pDs, void* pUser)
{
	if(g_pDs != NULL){
		printf("ERROR: Test expects a single dataset per fixture\n");
		return DASERR_DS;
	}
	g_pDs = pDs;
	return DAS_OKAY;
}

static DasErrCode onData(StreamDesc* pSd, int iPktId, DasDs* pDs, void* pUser)
{
	++g_nRecs;
	return DAS_OKAY;
}

/* The re-encode comparison runs in the CLOSE handler: DasIO_readAll deletes the
   stream (and its datasets) before returning, so this is the last moment the
   parsed model is alive. */
static DasErrCode onClose(StreamDesc* pSd, void* pUser)
{
	const char* sPath = (const char*)pUser;

	if((g_pDs == NULL)||(g_nRecs != g_nPkts)){
		printf("ERROR: Read %d records but %s holds %d packets\n",
			g_nRecs, sPath, g_nPkts);
		return DASERR_DS;
	}

	/* Flip every codec to a writer; the arrays keep their decoded data */
	for(size_t u = 0; u < DasDs_numCodecs(g_pDs); ++u){
		DasCodec* pCodec = DasDs_getCodec(g_pDs, u);
		if(DasCodec_update(DASENC_WRITE, pCodec, NULL, 0, '\0', NULL, NULL) != DAS_OKAY){
			printf("ERROR: Couldn't flip codec %zu to a writer\n", u);
			return DASERR_ENC;
		}
	}

	DasBuf* pBuf = new_DasBuf(MAX_PAYLOAD);
	if(pBuf == NULL)
		return DASERR_BUF;

	for(int r = 0; r < g_nRecs; ++r){
		DasBuf_reinit(pBuf);
		if(DasDs_encodeData(g_pDs, pBuf, r) != DAS_OKAY){
			printf("ERROR: Encode failed for record %d of %s\n", r, sPath);
			del_DasBuf(pBuf);
			return DASERR_DS;
		}
		size_t uWrote = DasBuf_written(pBuf);
		if(((int)uWrote != g_aPayLen[r])
		   ||(memcmp(pBuf->sBuf, g_aPayload[r], uWrote) != 0)){
			int iDiff = 0;
			int nMin = (uWrote < (size_t)g_aPayLen[r]) ? (int)uWrote : g_aPayLen[r];
			while((iDiff < nMin)&&(pBuf->sBuf[iDiff] == (char)g_aPayload[r][iDiff])) ++iDiff;
			printf("ERROR: Record %d of %s: re-encode differs from the file "
				"(wrote %zu bytes, expected %d, first difference at offset %d)\n",
				r, sPath, uWrote, g_aPayLen[r], iDiff);
			del_DasBuf(pBuf);
			return DASERR_DS;
		}
	}
	del_DasBuf(pBuf);
	printf("INFO: %d record(s) re-encoded byte-exact\n", g_nRecs);
	return DAS_OKAY;
}

int main(int argc, char** argv)
{
	das_init(argv[0], DASERR_DIS_EXIT, 0, DASLOG_WARN, NULL);

	if(argc < 2){
		fprintf(stderr, "Usage: %s FIXTURE.d3b [FIXTURE.d3b ...]\n", argv[0]);
		return 13;
	}

	for(int iFile = 1; iFile < argc; ++iFile){
		g_nPkts = 0; g_pDs = NULL; g_nRecs = 0;
		printf("INFO: Re-encoding %s\n", argv[iFile]);

		if(_loadPayloads(argv[iFile]) != 0) return 13;

		FILE* pFile = fopen(argv[iFile], "rb");
		if(pFile == NULL){ printf("ERROR: Couldn't open %s\n", argv[iFile]); return 13; }
		DasIO* pIn = new_DasIO_cfile("TestRaggedEncode", pFile, "r");
		DasIO_model(pIn, STREAM_MODEL_V3);

		StreamHandler handler;
		memset(&handler, 0, sizeof(StreamHandler));
		handler.dsDescHandler = onDataset;
		handler.dsDataHandler = onData;
		handler.closeHandler = onClose;
		handler.userData = (void*)argv[iFile];
		DasIO_addProcessor(pIn, &handler);

		/* onClose runs the flip + re-encode comparison; its error comes back here */
		int nRet = DasIO_readAll(pIn);
		del_DasIO(pIn);
		if(nRet != 0){
			printf("ERROR: Re-encode test failed for %s\n", argv[iFile]);
			return 13;
		}
	}

	printf("INFO: All ragged re-encode tests passed\n");
	return 0;
}
