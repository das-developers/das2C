/* Copyright (C) 2026 Chris Piker <chris-piker@uiowa.edu>
 *
 * Author: C. Piker, via Claude Opus 4.8
 *
 * This file is part of das2C, the Core Das C Library.
 *
 * das2C is free software; you can redistribute it and/or modify it under
 * the terms of the GNU Lesser General Public License version 2.1 as published
 * by the Free Software Foundation.
 *
 * das2C is distributed in the hope that it will be useful, but WITHOUT ANY
 * WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS
 * FOR A PARTICULAR PURPOSE.  See the GNU Lesser General Public License for
 * more details.
 *
 * You should have received a copy of the GNU Lesser General Public License
 * version 2.1 along with das2C; if not, see <http://www.gnu.org/licenses/>.
 */

/* *************************************************************************

   das3_text: the das3-era successor to das2_ascii.

   A stream filter.  Reads a dasStream on standard input and writes the *same*
   stream to standard output with every binary-encoded value re-encoded as
   human-readable text (binary epoch times become ISO-8601).  The output is
   still a valid dasStream: only the value encodings change, so the result is
   simultaneously human readable, diff-able, re-parseable and das_verify-able.

   The heavy lifting lives in das2C.  Reading lands the stream in the das3 data
   model (DasDs / DasDim / DasVar / DasCodec).  Per dataset we DasDs_copy() the
   structure (sharing the storage arrays), then walk the codecs swapping each
   binary one for a UTF-8 text codec.  The only value type that can't be
   re-encoded in place is a binary epoch time: das3 stores those as integer or
   real epoch counts, and the codec can only emit ISO-8601 from a das_time
   array.  So for those we splice in a das_time array (DasDs_replaceAry) and
   convert each record on the way out.

************************************************************************** */

#define _POSIX_C_SOURCE 200112L

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <das2/core.h>

#define PROG "das3_text"
#define PERR (DASERR_MAX + 1)

#define MAX_TIME_CONV 16   /* datetime coords converted per dataset */

/* ************************************************************************* */
/* A single epoch -> das_time conversion to run on each packet.  The epoch
   array lives in the *input* dataset (the reader fills it); the das_time array
   lives in the *output* dataset (we fill it, the codec writes it). */
typedef struct time_conv {
	DasAry* pEpoch;     /* source: integer/real epoch counts (in the input ds) */
	DasAry* pTime;      /* dest:   das_time values (in the output ds)           */
	das_units epoch;    /* the epoch scale of pEpoch (TT2000, US2000, ...)      */
} TimeConv;

/* Per output-dataset scratch, hung off DasDs.pUser */
typedef struct ds_work {
	TimeConv aConv[MAX_TIME_CONV];
	int nConv;
} DsWork;

/* ************************************************************************* */
/* Program context, passed to every callback */
typedef struct context {
	DasIO* pOut;
	DasStream* pSdOut;

	int  nGenRes;        /* general significant digits for reals (-r)      */
	int  nSecRes;        /* sub-second digits for times (-s)               */
	bool bAnnotations;   /* emit comments/exceptions (cleared by -c)       */
	bool bNoClock;       /* -n: leave clock values as raw numbers, no ISO  */
	bool bUpConv;        /* -3: accept das2 input and up-convert to das3   */
} Context;

/* ************************************************************************* */
/* Output field widths.  A real gets (sig-digits + 7) columns, a time gets 20 columns
   at seconds resolution and one more per sub-second digit.  The codec derives the
   actual printf format from the width when no explicit format is set.

   Default sig-digits are the "always round-trips a decimal" counts, DBL_DIG (15) and
   FLT_DIG (6) -- honest for science data, which rarely justifies more.  Bit-exact
   round-trip would need 17/9 (DBL/FLT_DECIMAL_DIG); a caller who needs that passes
   -r explicitly. */

static int _realWidth(const Context* p, das_val_type vt)
{
	if(p->nGenRes > 0)                     /* -r N given */
		return p->nGenRes + 7;
	return (vt == vtDouble) ? 22 : 13;     /* DBL_DIG=15, FLT_DIG=6, + 7 columns */
}

static int _intWidth(das_val_type vt)
{
	switch(vt){
	case vtLong: case vtULong: return 22;
	case vtInt:  case vtUInt:  return 13;
	case vtShort:case vtUShort:return 8;
	default:                   return 5;   /* byte/ubyte/bool */
	}
}

static int _timeWidth(const Context* p)
{
	return (p->nSecRes == 0) ? 20 : (21 + p->nSecRes);
}

/* ************************************************************************* */
/* Stream header: copy it through, but first decide if we'll even handle this
   stream's major version. */

DasErrCode onStream(DasStream* pSdIn, void* pUser)
{
	Context* pCtx = (Context*)pUser;

	/* The reader has already up-converted das2 into the das3 model in memory.
	   We always *emit* das3, so a das2 input means a silent version change.
	   Make that opt-in (-3) rather than accidental. */
	if((pSdIn->version[0] == '2') && !pCtx->bUpConv){
		return das_error(PERR,
			"Input is a das v%s stream.  das3_text emits das3; pass -3 to "
			"up-convert it, or use das2_ascii to keep it das2.", pSdIn->version
		);
	}

	pCtx->pSdOut = DasStream_copy(pSdIn);
	return DasIO_writeDesc(pCtx->pOut, (DasDesc*)pCtx->pSdOut, 0);
}

/* ************************************************************************* */
/* Dataset header: copy the structure, then re-aim every binary codec at a
   UTF-8 text encoding. */

DasErrCode onDataSet(DasStream* pSdIn, int iPktId, DasDs* pDsIn, void* pUser)
{
	Context* pCtx = (Context*)pUser;

	DasDs* pDsOut = DasDs_copy(pDsIn);
	if(pDsOut == NULL)
		return das_error(PERR, "Couldn't copy dataset %s", DasDs_id(pDsIn));

	if(DasStream_addDesc(pCtx->pSdOut, (DasDesc*)pDsOut, iPktId) != DAS_OKAY)
		return PERR;

	pDsIn->pUser = pDsOut;   /* link input -> output for the data callback */

	DsWork* pWork = (DsWork*)calloc(1, sizeof(DsWork));
	pDsOut->pUser = pWork;

	/* Walk the copied codecs.  Every one is currently a reader (the input was
	   parsed); output needs writers, so each branch re-initializes for write. */
	for(size_t i = 0; i < DasDs_numCodecs(pDsOut); ++i){
		DasCodec* pCodec = DasDs_getCodec(pDsOut, i);

		/* Already text?  Just flip it to a writer, encoding unchanged. */
		if(pCodec->vtBuf == vtText){
			if(DasCodec_update(DASENC_WRITE, pCodec, NULL, 0, '\0', NULL, NULL) != DAS_OKAY)
				return PERR;
			continue;
		}

		/* A native-byte blob can't ride a text stream as raw bytes, use base64
		   instead.  An already-base64 field stays base64. */
		if((strcmp(pCodec->sEncType, "blob") == 0)||(strcmp(pCodec->sEncType, "base64") == 0)){
			if(DasCodec_update(
				DASENC_WRITE, pCodec, "base64", DASENC_ITEM_LEN, '\0', NULL, NULL
			) != DAS_OKAY)
				return PERR;
			continue;
		}

		DasAry* pAry = pCodec->pAry;
		das_val_type vtAry = DasAry_valType(pAry);
		bool bDateTime = (strcmp(pCodec->sSemantic, "datetime") == 0);
		bool bBool     = (strcmp(pCodec->sSemantic, "bool") == 0);

		/* Datetime, default behavior: convert the epoch array to das_time so the
		   codec can write ISO-8601.  This is the one case DasDs_copy alone can't
		   cover, since the surface value type has to change. */
		if(bDateTime && !pCtx->bNoClock){
			if(pWork->nConv >= MAX_TIME_CONV)
				return das_error(PERR, "More than %d time coordinates in dataset %s",
					MAX_TIME_CONV, DasDs_id(pDsOut));

			das_units epoch = pAry->units;
			char sTimeId[80];
			snprintf(sTimeId, sizeof(sTimeId)-1, "%s_iso", DasAry_id(pAry));

			/* das_time array shaped like the epoch array */
			ptrdiff_t aShape[DASIDX_MAX] = DASIDX_INIT_UNUSED;
			int nRank = DasAry_shape(pAry, aShape);
			size_t aSz[DASIDX_MAX];
			for(int k = 0; k < nRank; ++k) aSz[k] = (aShape[k] < 1) ? 0 : (size_t)aShape[k];
			aSz[0] = 0;
			DasAry* pTime = new_DasAry(sTimeId, vtTime, 0, NULL, nRank, aSz, UNIT_UTC);
			if(pTime == NULL) return PERR;

			/* Record the per-packet conversion (pAry still lives in the input
			   dataset after the swap removes it from the output one). */
			pWork->aConv[pWork->nConv].pEpoch = pAry;
			pWork->aConv[pWork->nConv].pTime  = pTime;
			pWork->aConv[pWork->nConv].epoch  = epoch;
			pWork->nConv += 1;

			/* Splice the das_time array in for the epoch array (repoints the
			   variable), then re-aim this codec at it as a UTF-8 datetime writer. */
			if(DasDs_replaceAry(pDsOut, DasAry_id(pAry), pTime) != DAS_OKAY)
				return PERR;

			if(DasCodec_init(
				DASENC_WRITE, pCodec, pTime, "datetime", "utf8",
				(int16_t)_timeWidth(pCtx), ' ', NULL, NULL
			) != DAS_OKAY)
				return PERR;

			dec_DasAry(pTime);  /* the dataset (list + var + codec) owns it now */
			continue;
		}

		/* Everything else -- reals, integers, and (under -n) clocks left as raw
		   numbers -- re-encodes in place as UTF-8 text.  For a clock kept numeric
		   we hand the codec the epoch units so its datetime semantic still
		   validates; the integer/real value type makes it print the bare number. */
		int nWidth;
		das_units epoch = NULL;
		if(bDateTime){
			epoch = pAry->units;
			nWidth = _intWidth(vtAry);          /* integer epoch counts          */
			if((vtAry == vtFloat)||(vtAry == vtDouble))
				nWidth = _realWidth(pCtx, vtAry); /* or real epoch counts          */
		}
		else if(bBool)
			nWidth = 2;                          /* one glyph (T, F or *) + a separator */
		else if((vtAry == vtFloat)||(vtAry == vtDouble))
			nWidth = _realWidth(pCtx, vtAry);
		else
			nWidth = _intWidth(vtAry);

		if(DasCodec_update(
			DASENC_WRITE, pCodec, "utf8", (int16_t)nWidth, ' ', epoch, NULL
		) != DAS_OKAY)
			return PERR;
	}

	return DasIO_writeDesc(pCtx->pOut, (DasDesc*)pDsOut, iPktId);
}

/* ************************************************************************* */
/* Convert pending epoch values, write the packet, reset the record buffers. */

static DasErrCode writeAndClear(Context* pCtx, int iPktId, DasDs* pDsIn)
{
	DasDs* pDsOut = (DasDs*)pDsIn->pUser;
	DsWork* pWork = (DsWork*)pDsOut->pUser;

	/* Run the epoch -> das_time conversions for this batch of records */
	for(int c = 0; c < pWork->nConv; ++c){
		TimeConv* pC = &(pWork->aConv[c]);
		das_val_type vt = DasAry_valType(pC->pEpoch);

		size_t uVals = 0;
		const ubyte* pBeg = DasAry_getIn(pC->pEpoch, vt, 0, NULL, &uVals);
		if(pBeg == NULL) continue;

		/* A fill value is converted by its meaning, not by arithmetic: running an
		   epoch fill sentinel (e.g. INT64_MAX) through the calendar math overflows.
		   Recognize a value equal to the input array's fill and emit the das_time
		   fill instead -- that round-trips and stays semantically "fill". */
		const ubyte* pEpFill   = DasAry_getFill(pC->pEpoch);
		const ubyte* pTimeFill = DasAry_getFill(pC->pTime);
		int64_t nFillL = (vt == vtLong)   ? *((const int64_t*)pEpFill) : 0;
		double  rFillD = (vt == vtDouble) ? *((const double*)pEpFill)  : 0.0;

		das_time dt;
		for(size_t u = 0; u < uVals; ++u){
			bool bFill = (vt == vtLong)
				? (((const int64_t*)pBeg)[u] == nFillL)
				: (((const double*)pBeg)[u] == rFillD);

			if(bFill){
				if(DasAry_append(pC->pTime, pTimeFill, 1) == NULL)
					return PERR;
				continue;
			}

			/* Keep TT2000 integer-exact: int64 straight into das_time, never via
			   a double.  Other epochs are doubles already. */
			if((vt == vtLong) && (pC->epoch == UNIT_TT2000))
				dt_from_tt2k(&dt, ((const int64_t*)pBeg)[u]);
			else if(vt == vtDouble)
				Units_convertToDt(&dt, ((const double*)pBeg)[u], pC->epoch);
			else if(vt == vtLong)
				Units_convertToDt(&dt, (double)(((const int64_t*)pBeg)[u]), pC->epoch);
			else
				return das_error(PERR, "Unexpected epoch storage type %s", das_vt_toStr(vt));

			if(DasAry_append(pC->pTime, (const ubyte*)&dt, 1) == NULL)
				return PERR;
		}
	}

	DasErrCode nRet = DasIO_writeData(pCtx->pOut, (DasDesc*)pDsOut, iPktId);
	if(nRet != DAS_OKAY) return nRet;

	DasDs_clearRagged0(pDsOut);   /* clears shared + das_time arrays */
	DasDs_clearRagged0(pDsIn);    /* clears shared + epoch arrays    */
	return DAS_OKAY;
}

DasErrCode onData(DasStream* pSdIn, int iPktId, DasDs* pDsIn, void* pUser)
{
	return writeAndClear((Context*)pUser, iPktId, pDsIn);
}

/* ************************************************************************* */
/* Out of band content */

DasErrCode onException(OobExcept* pExcept, void* pUser)
{
	Context* pCtx = (Context*)pUser;
	if(!pCtx->bAnnotations) return DAS_OKAY;
	return DasIO_writeException(pCtx->pOut, pExcept);
}

DasErrCode onComment(OobComment* pCmt, void* pUser)
{
	Context* pCtx = (Context*)pUser;
	if(!pCtx->bAnnotations) return DAS_OKAY;
	return DasIO_writeComment(pCtx->pOut, pCmt);
}

/* ************************************************************************* */
/* Flush any datasets still holding data, then free the per-dataset scratch */

DasErrCode onClose(DasStream* pSdIn, void* pUser)
{
	Context* pCtx = (Context*)pUser;

	int nPktId = 0;
	DasDesc* pDesc = NULL;
	ptrdiff_t aShape[DASIDX_MAX] = DASIDX_INIT_UNUSED;

	while((pDesc = DasStream_nextDesc(pSdIn, &nPktId)) != NULL){
		if(DasDesc_type(pDesc) != DATASET) continue;
		DasDs* pDsIn = (DasDs*)pDesc;
		if(pDsIn->pUser == NULL) continue;

		DasDs_shape(pDsIn, aShape);
		if(aShape[0] > 0){
			DasErrCode nRet = writeAndClear(pCtx, nPktId, pDsIn);
			if(nRet != DAS_OKAY) return nRet;
		}

		DasDs* pDsOut = (DasDs*)pDsIn->pUser;
		if(pDsOut->pUser != NULL){ free(pDsOut->pUser); pDsOut->pUser = NULL; }
	}

	DasIO_close(pCtx->pOut);
	return DAS_OKAY;
}

/* ************************************************************************* */
void prnHelp()
{
	fprintf(stderr,
"SYNOPSIS\n"
"   " PROG " - Re-encode binary dasStream values as readable UTF-8 text\n"
"\n"
"USAGE\n"
"   " PROG " [-r N] [-s N] [-c] [-n] [-3] [INFILE]\n"
"\n"
"DESCRIPTION\n"
"   " PROG " is a filter.  It reads a das stream on standard input (or from\n"
"   INFILE if given) and writes a das stream to standard output.  Every data\n"
"   value encoded in binary is re-encoded as text, values already encoded as\n"
"   text pass through untouched.  Here 'text' means UTF-8 strings.\n"
"\n"
"   By default 4-byte reals are written with 6 significant digits (FLT_DIG) and\n"
"   8-byte reals with 15 (DBL_DIG); pass -r for more.  Binary epoch times\n"
"   (TT2000, US2000, ...) are written faithfully, as their raw numeric counts --\n"
"   das3_text transcodes, it does not interpret.\n"
"\n"
"   das3_text is the das3 successor to das2_ascii.  das2 streams are read into\n"
"   the das3 model automatically, but are only emitted as das3 when -3 is given\n"
"   (see OPTIONS); without it a das2 input is refused so the stream version is\n"
"   never changed by accident.\n"
"\n"
"   OUTPUT IS UTF-8.  This tool only changes the *encoding* of values from\n"
"   binary to text; it does not transliterate text.  Non-ASCII characters in\n"
"   the stream -- a degree sign, a micro sign, the properly spelled name of a\n"
"   European colleague -- are passed through as UTF-8, never flattened to ASCII\n"
"   approximations.\n"
"\n"
"OPTIONS\n"
"   -h, --help  Write this help text to standard error and exit.\n"
"\n"
"   -r N   General real-value resolution.  Write non-time values with N\n"
"          significant digits.  Minimum is 2.\n"
"\n"
"   -s N   Sub-second resolution.  Write N digits after the decimal in time\n"
"          values (0 to 9).  Times always carry at least whole seconds.\n"
"\n"
"   -c     Clean comment and exception annotations out of the stream.\n"
"\n"
"   -n     No clock conversion.  Leave epoch time values as their raw integer\n"
"          or real numbers (rendered as text) instead of ISO-8601, preserving\n"
"          the original time encoding and units.\n"
"\n"
"   -3     Up-convert a das2 input stream to das3 on output.\n"
"\n"
"AUTHOR\n"
"   chris-piker@uiowa.edu (drafted via Claude Opus 4.8)\n"
"\n"
"SEE ALSO\n"
"   das2_ascii, das3_csv, das2_binary, and das_verify (from das2py)\n"
"\n"
	);
}

/* ************************************************************************* */

int main(int argc, char** argv)
{
	das_init(argv[0], DASERR_DIS_EXIT, 0, DASLOG_INFO, NULL);

	Context ctx;
	memset(&ctx, 0, sizeof(Context));
	ctx.nGenRes      = -1;   /* unset -> per-type default in _realWidth */
	ctx.nSecRes      = 3;
	ctx.bAnnotations = true;
	ctx.bNoClock     = false;
	ctx.bUpConv      = false;

	const char* sInFile = NULL;

	for(int i = 1; i < argc; ++i){
		if((strcmp(argv[i], "-h") == 0)||(strcmp(argv[i], "--help") == 0)){
			prnHelp();
			return 0;
		}
		else if(strcmp(argv[i], "-r") == 0){
			if(++i >= argc){ fprintf(stderr, "ERROR: missing value after -r\n"); return 13; }
			ctx.nGenRes = atoi(argv[i]);
			if(ctx.nGenRes < 2 || ctx.nGenRes > 18){
				fprintf(stderr, "ERROR: -r supports 2 to 18 significant digits\n");
				return 13;
			}
		}
		else if(strcmp(argv[i], "-s") == 0){
			if(++i >= argc){ fprintf(stderr, "ERROR: missing value after -s\n"); return 13; }
			ctx.nSecRes = atoi(argv[i]);
			if(ctx.nSecRes < 0 || ctx.nSecRes > 9){
				fprintf(stderr, "ERROR: -s supports 0 to 9 sub-second digits\n");
				return 13;
			}
		}
		else if(strcmp(argv[i], "-c") == 0)  ctx.bAnnotations = false;
		else if(strcmp(argv[i], "-n") == 0)  ctx.bNoClock = true;
		else if(strcmp(argv[i], "-3") == 0)  ctx.bUpConv = true;
		else if(argv[i][0] == '-'){
			fprintf(stderr, "ERROR: unknown option '%s', use -h for help\n", argv[i]);
			return 13;
		}
		else
			sInFile = argv[i];   /* positional input file, else stdin */
	}

	/* Uncompressed das3 text output */
	ctx.pOut = new_DasIO_cfile(PROG, stdout, "w3");

	DasIO* pIn = NULL;
	if(sInFile == NULL)
		pIn = new_DasIO_cfile(PROG, stdin, "r");
	else
		pIn = new_DasIO_file(PROG, sInFile, "r");

	DasIO_model(pIn, 3);   /* read das2 or das3 into the das3 model */

	StreamHandler handler;
	memset(&handler, 0, sizeof(StreamHandler));
	handler.streamDescHandler = onStream;
	handler.dsDescHandler     = onDataSet;
	handler.dsDataHandler     = onData;
	handler.exceptionHandler  = onException;
	handler.commentHandler    = onComment;
	handler.closeHandler      = onClose;
	handler.userData          = &ctx;

	DasIO_addProcessor(pIn, &handler);

	int nRet = DasIO_readAll(pIn);

	return nRet;
}
