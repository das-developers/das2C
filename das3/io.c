/* Copyright (C) 2012-2026 Chris Piker <chris-piker@uiowa.edu>
 *
 * Adapted from:
 *   Copyright (C) 2004-2006 Jeremy Faden <jeremy-faden@uiowa.edu> 
 *               
 * This file is part of das2C, the Core Das C Library.
 * 
 * Das2C is free software; you can redistribute it and/or modify it under
 * the terms of the GNU Lesser General Public License version 2.1 as published
 * by the Free Software Foundation.
 *
 * Das2C is distributed in the hope that it will be useful, but WITHOUT ANY
 * WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS
 * FOR A PARTICULAR PURPOSE.  See the GNU Lesser General Public License for
 * more details.
 *
 * You should have received a copy of the GNU Lesser General Public License
 * version 2.1 along with Das2C; if not, see <http://www.gnu.org/licenses/>. 
 */

#define _POSIX_C_SOURCE 200112L

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <ctype.h>
#include <stdarg.h>
#include <assert.h>

#ifndef _WIN32
#include <sys/time.h>
#else
#include "w_gtod.h"
#define gettimeofday win_gettimeofday
#endif

#include "defs.h"
#include "util.h"  /* <-- Make sure endianness macros are present */
#include "str.h"
#include "log.h"
#include "io.h"
#include "xport_p.h"   /* files, pipes, sockets, TLS and zlib all live there */
#include "stream_p.h"  /* reads the stream's descriptor table and options */
#include "packet_p.h"  /* tracks whether each packet header went out */
#include "datset_p.h"  /* same for datasets */
#include "oob_p.h"
#include "util_p.h"
#include "io_p.h"     /* packet size and ID limits, shared with daslimit_get() */

#ifdef _WIN32
typedef ptrdiff_t ssize_t;
#endif

/* A das3 packet tag, "|Hx|" + ID field + '|' + length field + '|', is at most
   21 bytes (5 digit ID, 10 digit length).  The limit is on the tag as a
   whole: leading zeros may pad either field and are ignored, only the
   converted values are range checked. */
#define DAS3_TAG_MAX 21

/* ID field bytes with its pipe: what is left after the 4 byte prefix and
   the shortest possible length field, "1|" */
#define DAS3_IDFIELD_MAX (DAS3_TAG_MAX - 4 - 2)

/* Parser positions between calls to DasIO_feed() */
#define IOF_TAG    0   /* accumulating the 4-byte packet tag */
#define IOF_PKTID  1   /* accumulating a das3 packet ID field */
#define IOF_SIZE2  2   /* accumulating a das2 6-byte length field */
#define IOF_SIZE3  3   /* accumulating a das3 length field */
#define IOF_BODY   4   /* accumulating packet body bytes */
#define IOF_DOC    5   /* input is one un-packetized document, runs to EOF */
#define IOF_DONE   6   /* DasIO_feedEnd accepted the end of input */
#define IOF_ERR    7   /* latched error, nothing further will parse */
#define IOF_STOP   8   /* an exception ended the stream, later bytes are
                          ignored; nErr holds the close handlers' result */

/* Holds any das3 tag field plus a NUL, see DAS3_TAG_MAX */
#define IOF_FIELD_SZ 24

/* The DasIO structure itself.  The transport half (file, socket, TLS, zlib)
 * lives behind pX, see xport_p.h; what is left here is the framing half
 * (version, model, processors).  Everything is per instance: two DasIO
 * objects in two threads don't share anything.  One object in two threads is
 * not supported. */

#define DAS2_MAX_PROCESSORS 10  /* max number of stream processor objects */
#define DASIO_NAME_SZ 128


/* The defaults for NULL callbacks are applied at dispatch time in io.c, not
   installed here, so a structure built with "= {DAS_STREAM_PROC_VER}" behaves
   the same as one from das_stream_proc_init(). */
DasErrCode das_stream_proc_init_ver(
	das_stream_proc* pThis, void* pUser, size_t uVersion
){
	/* A later library adds cases here for each older layout it can still
	   serve, clearing only that layout's size */
	if(uVersion != DAS_STREAM_PROC_V3_0)
		return das_error(DASERR_IO,
			"Unknown das_stream_proc version 0x%04zx, this library knows 0x%04x",
			uVersion, DAS_STREAM_PROC_VER
		);

	memset(pThis, 0, sizeof(das_stream_proc));
	pThis->uVersion = uVersion;
	pThis->pUser = pUser;
	return DAS_OKAY;
}


/* What a writer remembers about a header it sent.  "Sent" is a fact about
   an output, not about a descriptor: descriptors don't do IO, one may be
   written to several DasIO objects, and several may take turns under one ID
   of the same output. */
struct das_io_sent {
	const DasDesc* pDesc; /* Compared, never followed, so a descriptor that
	                         was freed after its header went out is harmless */
	uint32_t uRev;        /* DasPkt header revision when sent, 0 for datasets */
};

/* Output IDs tracked inline before the lists move to the heap */
#define DASIO_LOC_SENT 16

struct das_io_struct {
	char     rw;         /* w' for write, 'r' for read, plus the tag style */
   das_stream_model model; /* Expected datastructure types in the stream  */

	bool     compressed; /* writer: 'c' was in the mode, compress after the header */

	bool     bEmbedAsBytes; /* read-side: recover undecodable embedded formats as
	                           opaque bytes instead of failing (DasIO_embedAsBytes) */

	bool     bAccum;     /* read-side: keep dataset records across packets instead
	                        of clearing before each decode (DasIO_setAccum) */

	bool     bReadDone;  /* read-side: DasIO_readAll returned or DasIO_feedEnd
	                        ran, so the stream is complete and may be taken */

	DasStream* pSd;      /* read-side: the stream being read.  Owned here until
	                        del_DasIO() unless DasIO_takeStream() detached it.
	                        Feed mode and DasIO_readAll() both build into it. */

	char     sName[DASIO_NAME_SZ]; /* A human readable name for data source or sink */
	
	long int offset;     /* bytes read so far, for messages */

   das_stream_ver dasver; /* Wire version.  Writers: what goes out, fixed once the header
                            is sent.  Readers: UNKNOWN unless the mode string pinned it */
	
	/* Where the bytes go or come from, owned.  NULL only in feed mode.  When
	   a stream turns compression on this becomes a zlib filter wrapping the
	   original transport. */
	DasXport* pX;
	
	/* data object processor's with callbacks  (Input / Output) */
	das_stream_proc* pProcs[DAS2_MAX_PROCESSORS+1];
	bool bSentHeader;

	/* Writer: the header each output ID last carried, so data can't go out
	   ahead of its header, see _DasIO_sentChk().  Parallel lists like
	   DasStream's, the IDs apart so a lookup scans one small array.  The
	   heap pointers are NULL until the inline arrays fill. */
	size_t uSent;
	size_t uSentCap;     /* 0 means the inline arrays */
	size_t uSentLast;    /* one past the index last matched, 0 when unset */
	uint16_t* pSentIdHeap;
	struct das_io_sent* pSentHeap;
	uint16_t aSentIdLoc[DASIO_LOC_SENT];
	struct das_io_sent aSentLoc[DASIO_LOC_SENT];
	
	/* Sub Object Writing (output) */
	DasBuf* pDb;        /* Sub-Object serializing buffer */
	
	int taskSize;       /* progress indicator max value (output) */
	long tmLastProgMsg; /* Time the last progress message was emitted (output)*/

	DasComment cmt;     /* scratch for DasIO_comment_v (output) */
	DasExcept ex;       /* scratch for DasIO_exception_v (output) */

	bool bClosed;       /* DasIO_close ran, nothing more may be written */
	bool bExceptOut;    /* the close came from DasIO_exception */

	/* read-side: the exception that ended the input, set while the close
	   handlers run.  Points at the reader's own scratch object. */
	const DasExcept* pExceptIn;

	/* Feed (push-parser) state, non-NULL only for feed-mode objects */
	struct das_feed_state* pFeed;
};

/* Push-parser state, allocated only for feed-mode objects */
struct das_feed_state {
	int nState;           /* one of the IOF_* positions above */
	bool bFirstRead;      /* no complete packet tag parsed yet */
	bool bEnded;          /* DasIO_feedEnd has run, object is inert */

	int nContent;         /* content flags for the packet in progress */
	int nPktId;           /* packet ID for the packet in progress */
	size_t uNeed;         /* body bytes still outstanding in IOF_BODY */

	char sTag[5];         /* the leading 4-byte packet tag */
	char sField[IOF_FIELD_SZ];  /* das2 size / das3 ID & size accumulator */
	size_t uField;        /* valid bytes in the active accumulator */
	size_t uTagLen;       /* das3 tag bytes so far, against DAS3_TAG_MAX */

	DasComment cmt;       /* out-of-band scratch objects, same role as the */
	DasExcept ex;         /* locals in DasIO_readAll */
	OutOfBand* apOobs[3];

	ubyte* pDoc;          /* un-packetized document accumulator, grows */
	size_t uDocLen;
	size_t uDocAlloc;

	DasErrCode nErr;      /* latched error code */
};
typedef struct das_feed_state DasFeed;

/* ************************************************************************** */
/* Constructors/Destructors */

/* Parse the mode against what the transport can do.  uXFlags is the
 * transport's DASXPORT_RD / DASXPORT_WR, 0 for feed mode, which only reads.
 * The direction letter may be left out when the transport goes one way. */
static DasErrCode _DasIO_setMode(DasIO* pThis, const char* mode, unsigned int uXFlags)
{
	if(mode == NULL) mode = "";
	const char* sShow = (mode[0] != '\0') ? mode : "(none)";

	/* 'b' is an fopen() habit in old mode strings, accepted and ignored */
	if(strspn(mode, "rwc23b") != strlen(mode))
		return das_error(DASERR_IO, "%s: DasIO mode '%s' has a letter other than "
		                 "r, w, c, 2 or 3", pThis->sName, mode);

	bool bR = (strchr(mode, 'r') != NULL);
	bool bW = (strchr(mode, 'w') != NULL);
	if(bR && bW)
		return das_error(DASERR_IO, "%s: DasIO mode '%s' says both read and write, "
		                 "a DasIO does one or the other", pThis->sName, mode);

	if(!bR && !bW){
		if(uXFlags == 0)                 bR = true;   /* feed mode */
		else if(uXFlags == DASXPORT_RD)  bR = true;
		else if(uXFlags == DASXPORT_WR)  bW = true;
		else
			return das_error(DASERR_IO, "%s: the transport goes both ways, so the "
			                 "DasIO mode must say 'r' or 'w', got '%s'", pThis->sName, sShow);
	}
	else if(uXFlags != 0){
		unsigned int uNeed = bR ? DASXPORT_RD : DASXPORT_WR;
		if(!(uXFlags & uNeed))
			return das_error(DASERR_IO, "%s: mode '%s' %s, but the transport was not "
			                 "opened for that", pThis->sName, mode, bR ? "reads" : "writes");
	}
	pThis->rw = bR ? 'r' : 'w';

	pThis->dasver = STREAM_VER_UNK;  /* Any version by default */
	if(strchr(mode, '2') != NULL)
		pThis->dasver = STREAM_VER_2_2;
	else if (strchr(mode, '3') != NULL)
		pThis->dasver = STREAM_VER_3_0;

	if(strchr(mode, 'c') != NULL){
		if(bR)
			return das_error(DASERR_IO, "%s: 'c' is for writers, a reader decompresses "
			                 "when the stream header says to", pThis->sName);
		pThis->compressed = true;
	}

	/* When writing we have to have a version, default to das2 */
	if(bW && (pThis->dasver == STREAM_VER_UNK))
		pThis->dasver = STREAM_VER_2_2;

	return DAS_OKAY;
}

/* The parts every DasIO has, feed or not.  NULL on a bad mode. */
static DasIO* _DasIO_alloc(const char* sProg, const char* mode, unsigned int uXFlags)
{
	DasIO* pThis = (DasIO*)calloc(1, sizeof(DasIO));
	if(pThis == NULL){
		das_error(DASERR_IO, "Couldn't allocate a DasIO");
		return NULL;
	}
	pThis->model = STREAM_MODEL_V2;
	pThis->taskSize = -1;  /* for progress indication */
	strncpy(pThis->sName, sProg ? sProg : "das", DASIO_NAME_SZ - 1);
	init_DasComment(&pThis->cmt);
	init_DasExcept(&pThis->ex);
	das_store_str(&(pThis->cmt.sSrc), &(pThis->cmt.uSrcLen), pThis->sName);

	if(_DasIO_setMode(pThis, mode, uXFlags) != DAS_OKAY){
		fini_DasComment(&pThis->cmt);
		fini_DasExcept(&pThis->ex);
		free(pThis);
		return NULL;
	}

	/* An I/O buffer re-used for the life of the object */
	if((pThis->pDb = new_DasBuf_elastic(DASIO_INIT_PKT_SZ, DASIO_MAX_PKT_SZ)) == NULL){
		fini_DasComment(&pThis->cmt);
		fini_DasExcept(&pThis->ex);
		free(pThis);
		return NULL;
	}
	return pThis;
}

DasIO* new_DasIO(const char* sProg, DasXport* pX, const char* mode)
{
	if(pX == NULL){
		das_error(DASERR_IO, "No transport given to %s, the open probably failed",
		          sProg ? sProg : "new_DasIO");
		return NULL;
	}

	DasIO* pThis = _DasIO_alloc(sProg, mode, pX->uFlags);
	if(pThis == NULL){
		del_DasXport(pX);   /* ours either way, see io.h */
		return NULL;
	}
	pThis->pX = pX;
	return pThis;
}

void DasIO_embedAsBytes(DasIO* pThis, bool bEnable){
	pThis->bEmbedAsBytes = bEnable;
}

void DasIO_setAccum(DasIO* pThis, bool bAccum){
	pThis->bAccum = bAccum;
}

bool DasIO_getAccum(const DasIO* pThis){
	return pThis->bAccum;
}

DasStream* DasIO_getStream(DasIO* pThis){
	return pThis->pSd;
}

DasStream* DasIO_takeStream(DasIO* pThis)
{
	if(pThis->rw != 'r'){
		das_error(DASERR_IO, "%s is an output, only readers own a stream", pThis->sName);
		return NULL;
	}

	/* Mid-read the parser still writes into the stream (new headers, redefined
	   packets), so ownership can't leave until reading is over */
	if(!pThis->bReadDone){
		das_error(DASERR_IO, "Can't take the stream from %s before DasIO_readAll "
			"returns or DasIO_feedEnd runs", pThis->sName);
		return NULL;
	}

	DasStream* pSd = pThis->pSd;
	pThis->pSd = NULL;
	return pSd;
}

DasErrCode DasIO_setModel(DasIO* pThis, das_stream_model nModel)
{
	switch(nModel){
	case STREAM_MODEL_V2:
	case STREAM_MODEL_V3:
		pThis->model = nModel;
		return DAS_OKAY;

	case STREAM_MODEL_MIXED:
		/* A reader can hand each header to the callbacks as whatever it was, but
		   a writer has one header dialect and one tag style per stream */
		if(pThis->rw == 'w')
			return das_error(DASERR_IO, "Mixed model streams can be read but not written");
		pThis->model = nModel;
		return DAS_OKAY;

	default:
		return das_error(DASERR_IO, "Invalid stream model: %d", (int)nModel);
	}
}

das_stream_model DasIO_getModel(const DasIO* pThis)
{
	return pThis->model;
}

DasErrCode DasIO_setOutVer(DasIO* pThis, das_stream_ver nVer)
{
	if(pThis->rw != 'w')
		return das_error(DASERR_IO,
			"The output version applies to writers, readers take it from the header"
		);
	if(pThis->bSentHeader)
		return das_error(DASERR_IO,
			"Can't change the stream version, the header for %s was already sent",
			pThis->sName
		);

	switch(nVer){
	case STREAM_VER_2_2:
	case STREAM_VER_3_0:
		pThis->dasver = nVer;
		return DAS_OKAY;

	case STREAM_VER_1_0:
	case STREAM_VER_1_1:
		return das_error(DASERR_IO,
			"DasIO does not write das v%s streams, see the das1 utilities",
			das_stream_ver_toStr(nVer)
		);

	default:
		return das_error(DASERR_IO, "Invalid stream version: %d", (int)nVer);
	}
}

das_stream_ver DasIO_getOutVer(const DasIO* pThis)
{
	return pThis->dasver;
}

bool DasIO_headerSent(const DasIO* pThis)
{
	return pThis->bSentHeader;
}

/* ************************************************************************ */
/* The byte layer, hides compression and transport from the framing code    */

/* The byte layer.  These were public until the v3.0 API work; now the
 * framing code above and below is their only client, which is what lets
 * the transport half of DasIO change later without an ABI break. */
/* The text of a just encoded header or OOB buffer, "" if none.  Not NUL
   terminated when the text fills the buffer to its last byte, which an
   elastic pDb does whenever a write lands exactly on its current size, so
   print it with "%.*s" and the length, never a bare "%s". */
static const char* _DasIO_bufText(const DasBuf* pBuf)
{
	size_t uLen = 0;
	const ubyte* pData = DasBuf_data(pBuf, &uLen);
	return pData ? (const char*)pData : "";
}

static int _DasIO_read(DasIO* pThis, DasBuf* pBuf, size_t uLen)
{
	if(pThis->pFeed != NULL){
		das_error(DASERR_IO, "Feed mode sources push bytes, see DasIO_feed");
		return 0;
	}
	if(uLen == 0) return 0;

	/* Exact lengths, short only at the end of input.  Decompression, if the
	   stream asked for it, happens inside pX. */
	ptrdiff_t nRead = _DasXport_fill(pThis->pX, pBuf, uLen);
	if(nRead < 0){
		const char* sErr = DasXport_error(pThis->pX);
		return -1 * das_error(DASERR_IO, "%s: %s", pThis->sName,
		                      sErr ? sErr : "buffer too small for packet");
	}
	pThis->offset += nRead;
	return (int)nRead;
}


/* TODO: Support this with specialized version of the DasBuf_write* 
         functions for faster results */
static int _DasIO_readUntil(DasIO* pThis, DasBuf* pBuf, size_t uMax, char cStop)
{
	int c;
	int nTotalRead = 0;
	int nRead = 0;
	for(size_t u = 0; u < uMax; ++u){
		nRead = _DasIO_read(pThis, pBuf, 1);
		if(nRead < 1)
			return nRead;
		else
			nTotalRead += nRead;
		
		if( (c = DasBuf_last(pBuf)) < 0) 
			return -1 * das_error(DASERR_IO, 
				"Empty buffer while searching for %c in the input stream", cStop
			);

		if(c == (int)cStop)
			return nTotalRead;
	}

	return -1 * das_error(DASERR_IO, "Couldn't find %c within %zu bytes", cStop, uMax);
}

/* Returns length on success, 0 after das_error() on failure */
static size_t _DasIO_write(DasIO* pThis, const char *data, int length)
{
	if(length <= 0) return 0;
	if(DasXport_write(pThis->pX, data, (size_t)length) < 0){
		das_error(DASERR_IO, "%s: %s", pThis->sName, DasXport_error(pThis->pX));
		return 0;
	}
	return (size_t)length;
}

/* das2 length fields are six digits, and pDb happily holds more than
   that (up to DASIO_MAX_PKT_SZ), so every das2 header and OOB writer asks
   first.  %06zu would otherwise print seven digits and break the framing. */
static DasErrCode _DasIO_d2LenChk(const DasIO* pThis, size_t uLen, const char* sWhat)
{
	if(uLen <= DAS2_LEN_MAX) return DAS_OKAY;
	return das_error(DASERR_IO, "%s: a %zu byte %s doesn't fit a das2 length "
	                 "field, the limit is %d bytes; write das3 for larger ones",
	                 pThis->sName, uLen, sWhat, DAS2_LEN_MAX);
}

/* Format, then write as one block.  Returns the bytes written, or -1 */
static int _DasIO_printf( DasIO* pThis, const char * format, ... )
{
	/* Typical output is short, get by with the stack when it fits */
	char sBuf[4096];
	char* pText = sBuf;

	va_list va;
	va_start(va, format);
	int nLen = vsnprintf(sBuf, sizeof(sBuf), format, va);
	va_end(va);
	if(nLen < 0) return -1;

	if((size_t)nLen >= sizeof(sBuf)){
		va_start(va, format);
		pText = das_vstring(format, va);
		va_end(va);
		if(pText == NULL) return -1;
	}

	int nRet = (_DasIO_write(pThis, pText, nLen) == (size_t)nLen) ? nLen : -1;
	if(pText != sBuf) free(pText);
	return nRet;
}

void DasIO_close(DasIO* pThis)
{
	/* Twice is harmless: DasIO_exception closes, and callers close again */
	if(pThis->bClosed) return;
	pThis->bClosed = true;

	/* A writer's last bytes go out here, a full disk shows up here too.  A
	   reader downstream that hung up early (| head) is not worth more than a
	   warning, so this does not go through das_error(). */
	if((pThis->pX != NULL) && (DasXport_close(pThis->pX) != DAS_OKAY))
		daslog_warn_v("%s: %s", pThis->sName, DasXport_error(pThis->pX));
}

/* Every writer refuses once the stream is closed, an exception included */
static DasErrCode _DasIO_closedErr(const DasIO* pThis, const char* sFunc)
{
	if(pThis->bExceptOut)
		return das_error(DASERR_IO,
			"%s: %s ended with an exception, nothing may follow it", sFunc,
			pThis->sName
		);
	return das_error(DASERR_IO, "%s: %s is closed", sFunc, pThis->sName);
}

void del_DasIO(DasIO* pThis){
	if(pThis == NULL) return;
	if(pThis->pX != NULL){
		DasIO_close(pThis);
		del_DasXport(pThis->pX);   /* borrowed handles stay open, see xport.h */
	}
	/* Close out the write buffer here */
	del_DasBuf(pThis->pDb);
	if(pThis->pSentIdHeap != NULL) free(pThis->pSentIdHeap);
	if(pThis->pSentHeap != NULL) free(pThis->pSentHeap);
	fini_DasComment(&pThis->cmt);
	fini_DasExcept(&pThis->ex);

	/* Never taken, so still ours.  No close callbacks if a read was cut
	   short, those only run from DasIO_readAll and DasIO_feedEnd. */
	if(pThis->pSd != NULL) del_DasDesc((DasDesc*)pThis->pSd);

	if(pThis->pFeed != NULL){
		DasFeed* pF = pThis->pFeed;
		if(!pF->bEnded){
			/* DasIO_feedEnd never ran, free the parser scratch space */
			fini_DasComment(&(pF->cmt));
			fini_DasExcept(&(pF->ex));
			if(pF->pDoc != NULL) free(pF->pDoc);
		}
		free(pF);
	}
	free(pThis);
}

/* ************************************************************************* */
/* Adding processors */
int DasIO_addProcessor(DasIO* pThis, das_stream_proc* pProc)
{
	/* Zero means the structure never went through das_stream_proc_init() */
	if(pProc->uVersion != DAS_STREAM_PROC_V3_0)
		return -1 * das_error(DASERR_IO,
			"Stream processor version 0x%04zx is not one this library knows, "
			"start from das_stream_proc_init()", pProc->uVersion
		);

	int i = 0;
	while(pThis->pProcs[i] != NULL) i++;
	if(i<DAS2_MAX_PROCESSORS){
		pThis->pProcs[i] = pProc;
		return i+1;
	}
	else {
		return -1 * das_error(DASERR_OOB, "Max number of processors exceeded");
	}
}

/* ************************************************************************* */
/* Processing the whole thing, Code in this area only has knowledge of the
 * basic stream structure.  Header encodings and data encodings are not it's
 * prerogative 
 */


/* Helper, Returns:
 *  -N: Error code, exit with an error
 *   0: No packet, normal exit
 *   1: Packet is Descriptor Header, get ID from pPktID
 *   2: Packet is data, get ID from pPktID
 *   3: Packet is out-of-band info.
 */

/* For das IO, there's 2 chunking states
 *
 *  - Packets     (simple tag based chucks)
 *  - Documents   (have to parse, may be chunkable (XML is))
 *
 * Four packet tagging schemes
 *  - das1-untagged 
 *  - das1-tagged
 *  - das2
 *  - das3
 *
 * There's 4 encodings
 *  - Header, XML
 *  - Header, JSON
 *  - Data (determined by headers)
 *  - Extension (unknown)
 *
 * There two packet dispositions:
 *  - In band data     (descriptors, data, documents)
 *  - Out of band data
 */
#define IO_CHUNK_PKT  0x0001
#define IO_CHUNK_DOC  0x0002
#define IO_CHUNK_MASK 0x000F

#define IO_TAG_D1U    0x0000
#define IO_TAG_D1T    0x0010
#define IO_TAG_D2     0x0020
#define IO_TAG_D3     0x0030
#define IO_TAG_MASK   0x00F0

#define IO_ENC_XML    0x0100
#define IO_ENC_JSON   0x0200
#define IO_ENC_DATA   0x0300
#define IO_ENC_EXT    0x0400
#define IO_ENC_MASK   0x0F00

#define IO_USAGE_CNT  0x1000  /* content, pass down to parsers */
#define IO_USAGE_OOB  0x2000  /* out-of-band, parse in I/O layer */
#define IO_USAGE_PASS 0x3000  /* Just pass it to the output */
#define IO_USAGE_MASK 0xF000

/* Classify a complete 4-byte packet tag.
 *
 * For das2 tags and document starts the returned content flags are complete
 * and the packet ID (if any) is parsed.  das3 tags are followed by a
 * variable length ID field, so for those only (IO_CHUNK_PKT|IO_TAG_D3) is
 * returned and the caller continues with _DasIO_finishD3Tag() once the ID
 * field bytes are in hand.
 *
 * Shared by the pull-mode reader (_DasIO_dataTypeOrErr) and the push-mode
 * parser (DasIO_feed), neither of which is visible from here, so no I/O
 * calls are allowed in this function.
 *
 * @returns content flags, or -1 * error code on an invalid tag.
 */
static int _DasIO_classifyTag(
	DasIO* pThis, const char* sTag, bool bFirstRead, int* pPktId
){
	int nContent = 0;
	uint32_t uPack = 0;

	/* If a document type (not packets) just return now */
	switch(sTag[0]){
	case '<':
		/* Save the first 4 bytes in pPktId so that they don't evaporate */
		uPack = ( (uint32_t)(ubyte)sTag[0] )|( ((uint32_t)(ubyte)sTag[1]) << 8 )|
		        ( ((uint32_t)(ubyte)sTag[2]) << 16 )|( ((uint32_t)(ubyte)sTag[3]) << 24 );
		memcpy(pPktId, &uPack, sizeof(uint32_t));
		if(bFirstRead)
			return (IO_CHUNK_DOC | IO_ENC_XML);

		return -1 * das_error(DASERR_IO,
			"Unpacketized XML document discovered in packetize stream at offset %ld",
			pThis->offset
		);

	case '{':
		/* Save the first 4 bytes in pPktId so that they don't evaporate */
		uPack = ( (uint32_t)(ubyte)sTag[0] )|( ((uint32_t)(ubyte)sTag[1]) << 8 )|
		        ( ((uint32_t)(ubyte)sTag[2]) << 16 )|( ((uint32_t)(ubyte)sTag[3]) << 24 );
		memcpy(pPktId, &uPack, sizeof(uint32_t));

		if(bFirstRead)
			return (IO_CHUNK_DOC | IO_ENC_JSON);

		return -1 * das_error(DASERR_IO,
			"Unpacketized JSON document discovered in packetize stream at offset %ld",
			pThis->offset
		);

	case '[':
		if(sTag[3] != ']')
			break;

		/* OOB packets have no ID, the caller's pPktId is left untouched */
		if(tolower(sTag[1]) == 'x' && tolower(sTag[2]) == 'x')
			return (IO_CHUNK_PKT | IO_TAG_D2 | IO_ENC_XML | IO_USAGE_OOB);

		if(!isdigit(sTag[1]) || !isdigit(sTag[2]) )
			break;

		nContent = (IO_CHUNK_PKT | IO_TAG_D2 | IO_ENC_XML | IO_USAGE_CNT);

		if(bFirstRead){
			if( (sTag[1] != '0')||(sTag[2] != '0') )
				return -1 * das_error(DASERR_IO,
					"Input is not a valid das-basic-stream-v2.2. Valid streams start "
					"with [00], the input started with: %02X %02X %02X %02X (%c%c%c%c)\n",
					(unsigned int)sTag[0], (unsigned int)sTag[1], (unsigned int)sTag[2],
					(unsigned int)sTag[3], sTag[0], sTag[1], sTag[2], sTag[3]
				);
		}
		else{
			if((sTag[1] == '0')&&(sTag[2] == '0')){
				return -1 * das_error(DASERR_IO,
					"Packet ID 0 is only valid for the initial stream header and may not "
					"repeat in the packet (repeat sighted at offset %ld", pThis->offset
				);
			}
		}

		if(sscanf(sTag+1, "%2d", pPktId) != 1)
			break;

		return nContent;

	case ':':
		if((!isdigit(sTag[1]))||(!isdigit(sTag[2]))||(sTag[3] != ':'))
			break;

		if(sscanf(sTag+1, "%2d", pPktId) != 1)
			break;
		return (IO_CHUNK_PKT | IO_TAG_D2 | IO_ENC_DATA | IO_USAGE_CNT);

	case '|':
		if(sTag[3] != '|')
			break;

		/* The variable length ID field between this tag and the size field
		 * is gathered by the caller then parsed in _DasIO_finishD3Tag() */
		return (IO_CHUNK_PKT | IO_TAG_D3);

	default:  /* Unknown first character... */
		break;
	}

	return -1 * das_error(DASERR_IO,
		"Unknown bytes %02X %02X %02X %02X (%c%c%c%c) at input offset %ld\n",
		(unsigned int)sTag[0], (unsigned int)sTag[1], (unsigned int)sTag[2],
		(unsigned int)sTag[3], sTag[0], sTag[1], sTag[2], sTag[3],
		pThis->offset
	);
}

/* Finish parsing a das3 packet tag.
 *
 * sTag holds the leading 4-byte tag from _DasIO_classifyTag(), sPktId the
 * null-terminated contents of the ID field that follows it (trailing pipe
 * removed, may be empty).  No I/O calls allowed here, see note on
 * _DasIO_classifyTag().
 *
 * @returns complete content flags, or -1 * error code.
 */
static int _DasIO_finishD3Tag(
	DasIO* pThis, const char* sTag, const char* sPktId, bool bFirstRead,
	int* pPktId
){
	int nContent = 0;

	/* Digits only; sscanf("%d") let signs, spaces and overflow through and
	   the result indexed the descriptor table.  The field is at most
	   DAS3_IDFIELD_MAX - 1 digits, which can't wrap 64 bits. */
	bool bHasId = (sPktId[0] != '\0');
	uint64_t uId = 0;
	for(const char* p = sPktId; *p != '\0'; ++p){
		if((*p < '0')||(*p > '9'))
			return -1 * das_error(DASERR_IO,
				"Packet ID field '%s' at offset %ld is not all digits", sPktId,
				pThis->offset
			);
		uId = uId*10 + (uint64_t)(*p - '0');
	}
	*pPktId = 0;

	/* the known packet types designators for das3 are (from das2py reader.py)
	 	"Sx" - XML stream definition (parse for content)
	    "Hx" - XML packet definition (parse for content)
	    "Pd" - Packetize data, content defined by a header
	    "Cx" - XML Comment packet (XML content)
	    "Ex" - XML Exception packet (XML content)
	    "XX" - Extra packet, content completely unknown */
	nContent = (IO_CHUNK_PKT | IO_TAG_D3);

	/* If this is the first read, this must be a stream header */
	if(bFirstRead){
		if(sTag[1] != 'S')
			return -1 * das_error(DASERR_IO, "Input is not a valid das-basic-stream-v3.0, "
				"Valid streams start |Sx| or |Sj|, this one started with "
				"%02X %02X %02X %02X (%c%c%c%c)\n",
				(unsigned int)sTag[0], (unsigned int)sTag[1], (unsigned int)sTag[2],
				(unsigned int)sTag[3], sTag[0], sTag[1], sTag[2], sTag[3]
			);
	}
	else{
		if(sTag[1] == 'S')
			return -1 * das_error(DASERR_IO, "Stream header detected after the first "
				"packet at offset %ld", pThis->offset);
	}

	/* A later stream header was refused above, so 'S' here is the first
	   packet.  IDs: the stream header has 0 or none, headers, data and
	   extension packets 1 to DAS3_MAX_PKTIDS - 1, comments and exceptions none
	   at all, they are outside the ID space (like das2's [xx]). */
	switch(sTag[1]){
		case 'S':
		if(uId != 0)
			return -1 * das_error(DASERR_IO,
				"Input is not a valid das-basic-stream-v3.0, Valid streams start "
				"with packet ID 0 (or not packet ID at all), this one started with "
				"id %s", sPktId
			);
		nContent |= IO_USAGE_CNT;
		break;

		case 'H':
		case 'P':
		case 'X':
		if((!bHasId)||(uId == 0))
			return -1 * das_error(DASERR_IO,
				"|%c%c| packet at offset %ld needs a packet ID from 1 to %d",
				sTag[1], sTag[2], pThis->offset, DAS3_MAX_PKTIDS - 1
			);
		if(uId >= DAS3_MAX_PKTIDS)
			return -1 * das_error(DASERR_IO,
				"Packet ID %llu at offset %ld is past the largest das2C reads, %d",
				(unsigned long long)uId, pThis->offset, DAS3_MAX_PKTIDS - 1
			);
		*pPktId = (int)uId;
		nContent |= IO_USAGE_CNT;
		break;

		case 'C':
		case 'E':
		if(bHasId)
			return -1 * das_error(DASERR_IO,
				"|%c%c| packets carry no packet ID, found '%s' at offset %ld",
				sTag[1], sTag[2], sPktId, pThis->offset
			);
		nContent |= IO_USAGE_OOB;
		break;
		default:
			return -1 * das_error(DASERR_IO,
				"Unknown bytes %02X %02X %02X %02X (%c%c%c%c) at input offset %ld\n",
				(unsigned int)sTag[0], (unsigned int)sTag[1], (unsigned int)sTag[2],
				(unsigned int)sTag[3], sTag[0], sTag[1], sTag[2], sTag[3],
				pThis->offset
			);
	}

	if(sTag[2] == 'x') nContent |= IO_ENC_XML;
	else if(sTag[2] == 'j') nContent |= IO_ENC_JSON;
	else if(sTag[2] == 'd') nContent |= IO_ENC_DATA;
	else nContent |= IO_ENC_EXT;

	return nContent;
}

/* Pull-mode packet tag reader, see the parse logic in _DasIO_classifyTag()
 * and _DasIO_finishD3Tag() */
static int _DasIO_dataTypeOrErr(
	DasIO* pThis, DasBuf* pBuf, bool bFirstRead, int* pPktId, size_t* pTagLen
){
	int nContent = 0;
	char sTag[5] = {'\0'};
	char sPktId[DAS3_TAG_MAX] = {'\0'};
	*pTagLen = 4;

	int nRead = _DasIO_read(pThis, pBuf, 4);
	if((bFirstRead)&&(nRead < 3))
		return -1 * das_error(DASERR_IO, "Input stream %s contains no packets.", pThis->sName);

	if(nRead == 0) return 0;  /* Normal end, except on first read */

	if(nRead < 3){
		/* Abnormal stream end, but just log it, don't trigger the error response */
		fprintf(stderr, "Partial packet in stream %s.", pThis->sName);
		return -1 * DASERR_IO;
	}
	DasBuf_read(pBuf, sTag, 4);

	nContent = _DasIO_classifyTag(pThis, sTag, bFirstRead, pPktId);

	/* das3 tags continue with a variable length packet ID field */
	if(nContent == (IO_CHUNK_PKT | IO_TAG_D3)){

		if((nRead = _DasIO_readUntil(pThis, pBuf, DAS3_IDFIELD_MAX, '|')) < 0)
			return nRead;

		/* Zero is end of input after the tag prefix, a truncated stream.  The
		   ID copy below indexes nRead - 1, so this can't pass through. */
		if(nRead == 0)
			return -1 * das_error(DASERR_IO,
				"Input ended inside a packet tag, the stream is truncated"
			);

		nRead = DasBuf_read(pBuf, sPktId, nRead);
		sPktId[nRead - 1] = '\0';
		*pTagLen += (size_t)nRead;

		nContent = _DasIO_finishD3Tag(pThis, sTag, sPktId, bFirstRead, pPktId);
	}

	return nContent;
}

/* Parse the 6-byte ASCII length field that follows a das2 header tag.
 * sLen must be null terminated.  No I/O calls allowed here, see note on
 * _DasIO_classifyTag().
 *
 * @returns the packet size, or -1 * error code.
 */
static int _DasIO_parseD2Size(const char* sLen)
{
	int nPktSz = 0;
	if(sscanf(sLen, "%d", &nPktSz) != 1)
		return -1 * das_error(DASERR_IO, "Can't get packet size from bytes %s", sLen);
	return nPktSz;
}

/* Parse a das3 length field, the null terminated contents between the
 * pipes (trailing pipe removed, may be empty).  No I/O calls allowed here,
 * see note on _DasIO_classifyTag().
 *
 * @returns the packet size, or -1 * error code.
 */
static int _DasIO_parseD3Size(DasIO* pThis, const char* sLen, int nPktId)
{
	int nPktSz = 0;

	if(sLen[0] == '\0')
		return -1 * das_error(DASERR_IO,
			"No packet size provided for packet ID %d at offset %ld",
			nPktId, pThis->offset
		);

	/* Digits only: sscanf("%d") let signs, spaces and overflow through.
	   The tag cap keeps the field under 16 digits, which can't wrap 64
	   bits, and the limit check below keeps the result inside an int. */
	uint64_t uSz = 0;
	for(const char* p = sLen; *p != '\0'; ++p){
		if((*p < '0')||(*p > '9'))
			return -1 * das_error(DASERR_IO,
				"Packet ID %d at offset %ld: length field '%s' is not all digits",
				nPktId, pThis->offset, sLen
			);
		uSz = uSz*10 + (uint64_t)(*p - '0');
	}

	if(uSz > DASIO_MAX_PKT_SZ)
		return -1 * das_error(DASERR_IO,
			"Packet ID %d at offset %ld is %llu bytes, das2C reads packets "
			"up to %d bytes", nPktId, pThis->offset, (unsigned long long)uSz,
			DASIO_MAX_PKT_SZ
		);

	nPktSz = (int)uSz;
	return nPktSz;
}

/* Get the size of a tagless das2 data packet from its header descriptor.
 * No I/O calls allowed here, see note on _DasIO_classifyTag().
 *
 * @returns the packet size, or -1 * error code.
 */
static int _DasIO_d2DataSize(DasStream* pSd, int nPktId)
{
	int nPktSz = 0;

	if(pSd == NULL)
		return -1 * das_error(DASERR_IO, "Data packets received before stream header");

	DasDesc* pDesc = DasStream_findDesc(pSd, nPktId);
	if(pDesc == NULL)
		return -1 * das_error(DASERR_IO, "Packet type %02d data received before packet "
					            	"type %02d header", nPktId, nPktId);

	if(pDesc->type == DASDESC_DS){
		DasErrCode nErr = DasDs_recBytes((DasDs*)pDesc, &nPktSz);
		if(nErr != DAS_OKAY)
			return -1 * nErr;
		if(nPktSz == 0){
			if(DasDs_numCodecs((DasDs*)pDesc) == 0)
				return -1 * das_error(DASERR_IO, "No codecs are defined for the dataset");
			else
				return -1 * das_error(DASERR_IO, "Logic error in io.c");
		}
		if(nPktSz < 0)
			return -1 * das_error(DASERR_IO, "Das2 streams do not support variable length packets");
	}
	else{
		if(pDesc->type == DASDESC_PKT)
			nPktSz = DasPkt_recBytes((DasPkt*)pDesc);
		else
			return -1 * das_error(DASERR_IO, "Logic error in io.c");
	}
	return nPktSz;
}

/* Pull-mode packet size reader, see the parse logic in _DasIO_parseD2Size(),
 * _DasIO_parseD3Size() and _DasIO_d2DataSize() */
static int _DasIO_sizeOrErr(
	DasIO* pThis, DasBuf* pBuf, int nContent, DasStream* pSd, int nPktId,
	size_t uTagLen
){
	char sLen[DAS3_TAG_MAX] = {'\0'};
	int nRead = 0;

	/* ...Old das2 data packets don't have lengths, they are fixed by the
	 * header for the given packet ID */
	if((nContent & (IO_TAG_MASK | IO_ENC_MASK)) == (IO_TAG_D2|IO_ENC_DATA))
		return _DasIO_d2DataSize(pSd, nPktId);

	/* For das2 tags we just read the next 6 bytes */
	if((nContent & IO_TAG_MASK) == IO_TAG_D2){
		if(_DasIO_read(pThis, pBuf, 6) != 6){
			return -1 * das_error(DASERR_IO, "Input stream ends in a partial packet");
		}

		DasBuf_read(pBuf, sLen, 6); /* Advances the read point */
		return _DasIO_parseD2Size(sLen);
	}

	if((nContent & IO_TAG_MASK) != IO_TAG_D3)
		return -1 * das_error(DASERR_IO, "Unknown packet tag type");

	/* Whatever the ID field left of the tag's DAS3_TAG_MAX bytes */
	if( (nRead = _DasIO_readUntil(pThis, pBuf, DAS3_TAG_MAX - uTagLen, '|')) < 0)
		return nRead;

	nRead = DasBuf_read(pBuf, sLen, nRead);
	if(nRead > 0)
		sLen[nRead - 1] = '\0';   /* Overwrite the trailing pipe */

	return _DasIO_parseD3Size(pThis, sLen, nPktId);
}

static DasErrCode _DasIO_handleDesc(
	DasIO* pThis, DasBuf* pBuf, DasStream** ppSd, int nPktId
){
	DasDesc* pDesc = NULL;
	DasStream* pSd = *ppSd;
	das_stream_proc* pHndlr = NULL;
	DasErrCode nRet = 0;
	
	/* Supply the stream descriptor if it exits */
	if( (pDesc = DasDesc_decode(pBuf, pSd, nPktId, pThis->model)) == NULL)
		return DASERR_IO;
	
	if(pDesc->type == DASDESC_STREAM){
		if(*ppSd != NULL)
			return das_error(DASERR_IO, "Multiple Stream descriptors in input");

		*ppSd = (DasStream*)pDesc;
		pSd = *ppSd;

		/* Carry the read-side embedded-recovery choice onto the stream so the
		   dataset parser (new_DasDs_xml) can see it -- the dataset headers that
		   follow are decoded with only the stream in hand, not the DasIO. */
		pSd->bEmbedAsBytes = pThis->bEmbedAsBytes;

		if(strcmp("deflate", pSd->compression)==0){
			/* Inflation happens below the packet framing layer, which a push
			 * parser has no access to.  No known deflated streams exist in the
			 * wild, so fail loud rather than carry zlib state through the feed
			 * state machine. */
			if(pThis->pFeed != NULL)
				return das_error(DASERR_IO,
					"Deflated streams are not supported in feed mode");

			/* The header was read to its exact length, so nothing compressed
			   is sitting in a buffer; the filter starts at the next byte */
			DasXport* pInf = _new_DasXport_inflate(pThis->pX);
			if(pInf == NULL)
				return das_error(DASERR_IO, "%s: couldn't start decompression", pThis->sName);
			pThis->pX = pInf;
		}
	}
	else{
		if((pDesc->type == DASDESC_PKT)||(pDesc->type == DASDESC_DS)){
			if(pSd == NULL)
				return das_error(DASERR_IO,
					"Streams must be defined before datasets can be defined"
				);
		
			/* The ID may already be in use.  A das2 packet holds one record,
			   so the old one is deleted once the processors have had a look.

			   A dataset is never deleted before its stream is: processors
			   point at it, and with DasIO_setAccum() it holds every record
			   read so far.  DasStream_addDesc() registers the new dataset over
			   it, so it stays attached to the stream but gets no more data. */
			DasDesc* pOld = DasStream_findDesc(pSd, nPktId);
			if((pOld != NULL)&&(pOld->type == DASDESC_PKT)){

				for(size_t u = 0; pThis->pProcs[u] != NULL; u++){
					pHndlr = pThis->pProcs[u];
					if(pHndlr->onPktRedef != NULL)
						nRet = pHndlr->onPktRedef(pSd, (DasPkt*)pOld, nPktId, pHndlr->pUser);

					if(nRet != 0) break;
				}

				/* A processor objected, so drop the new descriptor and stop.
				   The old one is still in place. */
				if(nRet != 0){
					del_DasDesc(pDesc);
					return nRet;
				}

				DasStream_freeDesc(pSd, pOld);
			}
			
			if((nRet = DasStream_addDesc(pSd, pDesc, nPktId)) != 0){
				del_DasDesc(pDesc);   /* not attached, so still ours */
				return nRet;
			}
		}
		else{
			return das_error(DASERR_IO, "Only Stream and Packet descriptors expected");
		}
	}
			
	/* Call the stream handlers */
	for(size_t u = 0; pThis->pProcs[u] != NULL; u++){
		pHndlr = pThis->pProcs[u];
		switch(pDesc->type){
		case DASDESC_STREAM:
			if(pHndlr->onStreamDesc != NULL)
				nRet = pHndlr->onStreamDesc(pSd, pHndlr->pUser);
			break;
		case DASDESC_PKT:
			if(pHndlr->onPktDesc != NULL)
				nRet = pHndlr->onPktDesc(pSd, (DasPkt*)pDesc, nPktId, pHndlr->pUser);
			break;
		case DASDESC_DS:
			if(pHndlr->onDsDesc != NULL)
				nRet = pHndlr->onDsDesc((DasDs*)pDesc, nPktId, pHndlr->pUser);
			break;
		default:
			nRet = das_error(DASERR_IO, "Unexpected descriptor type %d", pDesc->type);
			break;
		}
		if(nRet != 0) break;
	}
	
	return nRet;
}

static DasErrCode _DasIO_handleData(
	DasIO* pThis, DasBuf* pBuf, DasStream* pSd, int nPktId
){
	int nRet = 0;
	das_stream_proc* pHndlr = NULL;
	
	DasDesc* pDesc = DasStream_findDesc(pSd, nPktId);

	/* Raw handlers see the payload before anything parses it.  The read point
	   is restored after each one so a handler that consumes the buffer can't
	   starve the next handler, or the decoders below. */
	size_t uReadPt = DasBuf_readOffset(pBuf);
	bool bDecode = false;
	for(size_t u = 0; pThis->pProcs[u] != NULL; u++){
		pHndlr = pThis->pProcs[u];

		if(pHndlr->onRawData != NULL){
			nRet = pHndlr->onRawData(pDesc, pBuf, nPktId, pHndlr->pUser);
			DasBuf_setReadOffset(pBuf, uReadPt);
			if(nRet != DAS_OKAY) return nRet;
		}

		if(((pDesc->type == DASDESC_PKT)&&(pHndlr->onPktData != NULL)) ||
		   ((pDesc->type == DASDESC_DS)&&(pHndlr->onDsData != NULL)))
			bDecode = true;
	}

	/* Accumulating is itself a request for values, even with no handler
	   to hand them to (read everything, then DasIO_takeStream) */
	if((pDesc->type == DASDESC_DS)&&(pThis->bAccum))
		bDecode = true;

	/* Decoding on demand: with nobody to hand values to, the bytes are
	   simply left behind in the buffer. A handler has to be registered 
	   (or accumulation turned on) to trigger parsing and validation, see
	   DasIO_addProcessor() in io.h */
	if(!bDecode) return DAS_OKAY;

	/* Datasets grow without limit, so unless the caller asked to accumulate,
	   drop the previous packet's records before decoding this one.  Clearing
	   before rather than after the handlers leaves the last packet readable
	   once the read ends. */
	if((pDesc->type == DASDESC_DS)&&(!pThis->bAccum))
		DasDs_clearRagged0((DasDs*)pDesc);

	if(pDesc->type == DASDESC_PKT)
		nRet = DasPkt_decodeData((DasPkt*)pDesc, pBuf);
	else if(pDesc->type == DASDESC_DS)
		nRet = DasDs_decodeData((DasDs*)pDesc, pBuf);
	else
		assert(false);

	if(nRet != 0) return nRet;

	for(size_t u = 0; pThis->pProcs[u] != NULL; u++){
		pHndlr = pThis->pProcs[u];

		if((pDesc->type == DASDESC_PKT)&&(pHndlr->onPktData != NULL))
			nRet = pHndlr->onPktData((DasPkt*)pDesc, nPktId, pHndlr->pUser);

		else if((pDesc->type == DASDESC_DS)&&(pHndlr->onDsData != NULL))
			nRet = pHndlr->onDsData((DasDs*)pDesc, nPktId, pHndlr->pUser);

		if(nRet != DAS_OKAY) break;
	}

	return nRet;
}

static DasErrCode _DasIO_handleOOB(
	DasIO* pThis, DasBuf* pBuf, DasStream* pSd, OutOfBand** ppObjs
){	
	int nRet = 0;
	int nWhich = -1;
	das_stream_proc* pHndlr = NULL;
	if( (nRet = OutOfBand_decode(pBuf, ppObjs, &nWhich)) != 0) return nRet;
	
	/* Just Drop stuff I don't understand */
	if(nWhich < 0) return 0;
	
	OutOfBand* pOob = ppObjs[nWhich];

	/* An exception ends the stream.  The readers stop on seeing this set and
	   hand it to the close handlers, the only place handlers see one. */
	if(pOob->pkttype == OOB_EXCEPT){
		pThis->pExceptIn = (const DasExcept*)pOob;
		return DAS_OKAY;
	}

	for(size_t u = 0; pThis->pProcs[u] != NULL; u++){
		pHndlr = pThis->pProcs[u];
		if(pHndlr->onComment != NULL)
			nRet = pHndlr->onComment(pSd, (DasComment*)pOob, pHndlr->pUser);
		if(nRet != 0) break;
	}
	
	return nRet;
}

/* Route one complete chunk, with the body bytes at pBuf's read point, to
 * the handler layer.  Shared by the pull loop (DasIO_readAll) and the push
 * parser (DasIO_feed). */
static DasErrCode _DasIO_dispatchChunk(
	DasIO* pThis, DasBuf* pBuf, int nContent, int nPktId, DasStream** ppSd,
	OutOfBand** ppOobs
){
	switch(nContent & IO_ENC_MASK){
	case IO_ENC_JSON:
		return das_error(DASERR_IO, "JSON stream parsing is not yet supported");

	case IO_ENC_EXT:
		return das_error(DASERR_IO, "Extension formats are not yet supported");

	case IO_ENC_XML:
		if((nContent & IO_USAGE_MASK) == IO_USAGE_CNT)
			return _DasIO_handleDesc(pThis, pBuf, ppSd, nPktId);
		else if((nContent & IO_USAGE_MASK) == IO_USAGE_OOB)
			return _DasIO_handleOOB(pThis, pBuf, *ppSd, ppOobs);
		else
			return das_error(DASERR_IO, "XML pass through is not yet supported");

	case IO_ENC_DATA:
		/* The das2 tagless path catches this in _DasIO_d2DataSize, but sized
		 * das3 data packets can arrive before their header on a bad stream */
		if((*ppSd == NULL)||(DasStream_getDesc(*ppSd, nPktId) == NULL))
			return das_error(DASERR_IO,
				"Data packet with ID %d received before its header packet", nPktId);
		return _DasIO_handleData(pThis, pBuf, *ppSd, nPktId);

	default:
		return das_error(DASERR_IO, "Logic error in stream parser");
	}
}

/* What a read gets when no processor has an onClose, including a read with
 * no processors at all.  A normal end, or "no data", is fine.  Any other exception fails the read,
 * so a program that never looked at exceptions still notices one; without
 * this a reader that only accumulates would quietly hand back a truncated
 * stream. */
static DasErrCode _DasIO_defaultClose(const DasExcept* pExcept)
{
	if((pExcept == NULL)||(DasExcept_type(pExcept) == DAS_EX_NO_DATA))
		return DAS_OKAY;

	return das_error(DASERR_IO, "Stream exception, type %s: %s",
		DasExcept_typeStr(pExcept), DasExcept_msg(pExcept)
	);
}

/* Invoke the close handler of each attached stream processor, stopping on
 * the first error.  Shared by DasIO_readAll, DasIO_feed (on an exception)
 * and DasIO_feedEnd.  With no stream and no exception nothing was read, so
 * there is nothing to close.
 *
 * The default runs only when nobody supplied an onClose.  Running it for
 * each processor with a NULL one would let a passive sibling, such as a
 * progress monitor, fail a read whose exception another processor handled. */
static DasErrCode _DasIO_runCloseHandlers(
	DasIO* pThis, DasStream* pSd, const DasExcept* pExcept
){
	DasErrCode nRet = DAS_OKAY;

	if((pSd == NULL)&&(pExcept == NULL))
		return DAS_OKAY;

	bool bAnyClose = false;
	for(size_t u = 0; pThis->pProcs[u] != NULL; u++){
		das_stream_proc* pHndlr = pThis->pProcs[u];
		if(pHndlr->onClose == NULL) continue;
		bAnyClose = true;
		nRet = pHndlr->onClose(pSd, pExcept, pHndlr->pUser);
		if(nRet != 0) break;
	}
	return bAnyClose ? nRet : _DasIO_defaultClose(pExcept);
}

DasErrCode DasIO_readAll(DasIO* pThis)
{
	DasErrCode nRet = 0;

	DasComment sc;
	DasExcept ex;
	init_DasComment(&sc);
	init_DasExcept(&ex);
	OutOfBand* oobs[3] = {(OutOfBand*)&sc, (OutOfBand*)&ex, NULL};

	int nPktId, nBytes;
	int nContent;
	size_t uTagLen = 0;

	DasBuf* pBuf = pThis->pDb;
	bool bFirstRead = true;

	if(pThis->rw == 'w'){
		return das_error(DASERR_IO, "Can't read input, this is an output stream");
	}

	if(pThis->pFeed != NULL){
		return das_error(DASERR_IO, "Feed mode sources push bytes, see DasIO_feed");
	}

	/* One stream per object: a second pass would find the first stream still
	   held here and fail on its header anyway, say so plainly instead */
	if(pThis->bReadDone){
		return das_error(DASERR_IO, "DasIO_readAll called twice on %s", pThis->sName);
	}

	/* Loop over all the input packets, make sure to break out of the loop
	 * instead of just returning so that the stream close handlers can be called.
	 */
	while(nRet == 0){
		DasBuf_reinit(pBuf);

		/* What Kind of Packet do we have? */
		nPktId = -1;
		if((nContent = _DasIO_dataTypeOrErr(pThis, pBuf, bFirstRead, &nPktId, &uTagLen)) < 1){
			nRet = -1 * nContent;
			break;
		}
		bFirstRead = false;

		/* Get the number of bytes to read next */
		if((nContent & IO_CHUNK_MASK) == IO_CHUNK_PKT){

			nBytes = _DasIO_sizeOrErr(pThis, pBuf, nContent, pThis->pSd, nPktId, uTagLen);
			if(nBytes < 0){
				nRet = -1 * nBytes;
				break;
			}
			if(nBytes == 0){
				/* Wow, a null packet, let's call those illegal */
				nRet = das_error(DASERR_IO, "0-length input packet.");
				break;
			}

			/* das3 lengths were checked as parsed, das2 header lengths and
			   data record sizes come here unchecked */
			if(nBytes > DASIO_MAX_PKT_SZ){
				nRet = das_error(DASERR_IO, "Packet at offset %ld is %d bytes, das2C "
				                 "reads packets up to %d bytes", pThis->offset, nBytes,
				                 DASIO_MAX_PKT_SZ);
				break;
			}

			/* The tag is parsed, drop it so the body starts at the front, as it
			   does in feed mode.  pDb grows here if the body needs it. */
			DasBuf_reinit(pBuf);
			if( _DasIO_read(pThis, pBuf, nBytes) != nBytes){
				nRet = das_error(DASERR_IO, "Partial packet on input at offset %ld", pThis->offset);
				break;
			}
		}
		else{
			/* An un-packetized document runs to the end of input, which only
			 * the push parser can see coming.  See DasIO_feed */
			nRet = das_error(DASERR_IO,
				"Un-packetized documents are not supported by DasIO_readAll, "
				"use DasIO_feed");
			break;
		}

		nRet = _DasIO_dispatchChunk(pThis, pBuf, nContent, nPktId, &(pThis->pSd), oobs);
		if(nRet != 0) break;
		if(pThis->pExceptIn != NULL) break;  /* an exception ends the stream */

	}/* repeat until endOfStream */

	int nHdlrRet = _DasIO_runCloseHandlers(pThis, pThis->pSd, pThis->pExceptIn);
	pThis->pExceptIn = NULL;  /* points into ex, about to be freed */

	fini_DasComment(&sc);
	fini_DasExcept(&ex);

	/* The stream stays with this object until del_DasIO() or
	   DasIO_takeStream(), even after a failed read */
	pThis->bReadDone = true;

	return nRet == 0 ? nHdlrRet : nRet ;
}

/* ************************************************************************* */
/* Push-mode (feed) parsing
 *
 * The pull loop above blocks inside DasIO_read until bytes arrive.  The
 * feed parser inverts control: DasIO_feed() consumes caller supplied chunks
 * of any size and dispatches each packet as it completes.  Both run the
 * same classify/parse/dispatch functions, only byte acquisition differs.
 */

/* Latch an error so later DasIO_feed calls repeat it without re-parsing */
static DasErrCode _DasFeed_latch(DasFeed* pF, DasErrCode nErr)
{
	pF->nState = IOF_ERR;
	pF->nErr = nErr;
	return nErr;
}

/* Grow-on-demand accumulator for un-packetized documents */
static DasErrCode _DasFeed_docAppend(DasFeed* pF, const ubyte* pData, size_t uLen)
{
	if(pF->uDocLen + uLen > pF->uDocAlloc){
		size_t uAlloc = (pF->uDocAlloc > 0) ? pF->uDocAlloc : 65536;
		while(uAlloc < pF->uDocLen + uLen) uAlloc *= 2;
		ubyte* pNew = (ubyte*)realloc(pF->pDoc, uAlloc);
		if(pNew == NULL)
			return das_error(DASERR_IO,
				"Couldn't allocate %zu bytes for document input", uAlloc);
		pF->pDoc = pNew;
		pF->uDocAlloc = uAlloc;
	}
	memcpy(pF->pDoc + pF->uDocLen, pData, uLen);
	pF->uDocLen += uLen;
	return DAS_OKAY;
}

/* Transition to body accumulation once a packet size is known */
static DasErrCode _DasFeed_setBody(DasIO* pThis, DasFeed* pF, int nBytes)
{
	if(nBytes == 0)
		return das_error(DASERR_IO, "0-length input packet.");
	if(nBytes < 0)
		return das_error(DASERR_IO, "Negative packet length at offset %ld",
			pThis->offset);
	if(nBytes > DASIO_MAX_PKT_SZ)
		return das_error(DASERR_IO, "Packet at offset %ld is %d bytes, das2C reads "
		                 "packets up to %d bytes", pThis->offset, nBytes, DASIO_MAX_PKT_SZ);

	DasBuf_reinit(pThis->pDb);
	pF->uNeed = (size_t)nBytes;
	pF->nState = IOF_BODY;
	return DAS_OKAY;
}

/* Dispatch a complete un-packetized document.
 *
 * This is the plug-in point for das-basic-doc support (see
 * schema/das-basic-doc-ns-v3.0.xsd).  The framing layer accumulates the
 * entire input and hands it here; nothing below this point knows how to
 * parse one yet. */
static DasErrCode _DasIO_handleDoc(
	DasIO* pThis, const ubyte* pDoc, size_t uLen, int nContent
){
	(void)pThis; (void)pDoc; (void)uLen;
	return das_error(DASERR_NOTIMP,
		"Parsing un-packetized %s documents is not yet supported",
		((nContent & IO_ENC_MASK) == IO_ENC_XML) ? "XML" : "JSON"
	);
}

DasIO* new_DasIO_feed(const char* sProg, const char* mode)
{
	DasIO* pThis = _DasIO_alloc(sProg, mode, 0);
	if(pThis == NULL) return NULL;
	if(pThis->rw != 'r'){
		das_error(DASERR_IO, "Feed mode only supports reading, mode '%s' given", mode);
		del_DasIO(pThis);
		return NULL;
	}

	DasFeed* pF = (DasFeed*)calloc(1, sizeof(DasFeed));
	pF->nState = IOF_TAG;
	pF->bFirstRead = true;
	init_DasComment(&(pF->cmt));
	init_DasExcept(&(pF->ex));
	pF->apOobs[0] = (OutOfBand*)&(pF->cmt);
	pF->apOobs[1] = (OutOfBand*)&(pF->ex);
	pF->apOobs[2] = NULL;
	pThis->pFeed = pF;

	return pThis;
}

DasErrCode DasIO_feed(DasIO* pThis, const ubyte* pData, size_t uLen)
{
	DasFeed* pF = pThis->pFeed;
	int nRet = 0;
	size_t uTake = 0;

	if(pF == NULL)
		return das_error(DASERR_IO, "Not a feed mode object, see new_DasIO_feed");

	if(pF->nState == IOF_ERR)
		return pF->nErr;
	if(pF->nState == IOF_STOP)
		return DAS_OKAY;
	if(pF->nState == IOF_DONE)
		return _DasFeed_latch(pF, das_error(DASERR_IO,
			"DasIO_feed called after DasIO_feedEnd"));

	while(uLen > 0){
		switch(pF->nState){

		case IOF_TAG:
			uTake = 4 - pF->uField;
			if(uTake > uLen) uTake = uLen;
			memcpy(pF->sTag + pF->uField, pData, uTake);
			pF->uField += uTake; pData += uTake; uLen -= uTake;
			pThis->offset += uTake;
			if(pF->uField < 4)
				break;

			nRet = _DasIO_classifyTag(pThis, pF->sTag, pF->bFirstRead, &(pF->nPktId));
			if(nRet < 0)
				return _DasFeed_latch(pF, (DasErrCode)(-nRet));
			pF->nContent = nRet;
			pF->uField = 0;

			if((pF->nContent & IO_CHUNK_MASK) == IO_CHUNK_DOC){
				/* The entire input is one document, keep the tag bytes */
				pF->bFirstRead = false;
				pF->nState = IOF_DOC;
				if((nRet = _DasFeed_docAppend(pF, (const ubyte*)pF->sTag, 4)) != DAS_OKAY)
					return _DasFeed_latch(pF, nRet);
				break;
			}

			if((pF->nContent & IO_TAG_MASK) == IO_TAG_D3){
				/* bFirstRead stays set until _DasIO_finishD3Tag runs */
				pF->nState = IOF_PKTID;
				break;
			}

			pF->bFirstRead = false;
			if((pF->nContent & IO_ENC_MASK) == IO_ENC_DATA){
				/* Tagless das2 data, the header descriptor has the size */
				if((nRet = _DasIO_d2DataSize(pThis->pSd, pF->nPktId)) < 0)
					return _DasFeed_latch(pF, (DasErrCode)(-nRet));
				if((nRet = _DasFeed_setBody(pThis, pF, nRet)) != DAS_OKAY)
					return _DasFeed_latch(pF, nRet);
			}
			else
				pF->nState = IOF_SIZE2;
			break;

		case IOF_SIZE2:
			uTake = 6 - pF->uField;
			if(uTake > uLen) uTake = uLen;
			memcpy(pF->sField + pF->uField, pData, uTake);
			pF->uField += uTake; pData += uTake; uLen -= uTake;
			pThis->offset += uTake;
			if(pF->uField < 6)
				break;

			pF->sField[6] = '\0';
			pF->uField = 0;
			if((nRet = _DasIO_parseD2Size(pF->sField)) < 0)
				return _DasFeed_latch(pF, (DasErrCode)(-nRet));
			if((nRet = _DasFeed_setBody(pThis, pF, nRet)) != DAS_OKAY)
				return _DasFeed_latch(pF, nRet);
			break;

		case IOF_PKTID:
		case IOF_SIZE3: {
			/* Accumulate through the next pipe, bounded the same way as the
			 * pull reader: the two fields share the tag's DAS3_TAG_MAX bytes */
			size_t uCap = (pF->nState == IOF_PKTID) ? DAS3_IDFIELD_MAX :
			              (DAS3_TAG_MAX - pF->uTagLen);
			bool bFound = false;
			while(uLen > 0){
				char c = (char)*pData;
				++pData; --uLen; ++pThis->offset;
				if(c == '|'){
					pF->sField[pF->uField] = '\0';
					bFound = true;
					break;
				}
				pF->sField[pF->uField++] = c;
				if(pF->uField >= uCap)
					return _DasFeed_latch(pF, das_error(DASERR_IO,
						"Couldn't find | within %zu bytes", uCap));
			}
			if(!bFound)
				break;     /* out of input mid-field, resume on the next call */

			if(pF->nState == IOF_PKTID){
				nRet = _DasIO_finishD3Tag(
					pThis, pF->sTag, pF->sField, pF->bFirstRead, &(pF->nPktId)
				);
				if(nRet < 0)
					return _DasFeed_latch(pF, (DasErrCode)(-nRet));
				pF->nContent = nRet;
				pF->bFirstRead = false;
				pF->uTagLen = 4 + pF->uField + 1;   /* prefix, ID digits, pipe */
				pF->uField = 0;
				pF->nState = IOF_SIZE3;
			}
			else{
				nRet = _DasIO_parseD3Size(pThis, pF->sField, pF->nPktId);
				pF->uField = 0;
				if(nRet < 0)
					return _DasFeed_latch(pF, (DasErrCode)(-nRet));
				if((nRet = _DasFeed_setBody(pThis, pF, nRet)) != DAS_OKAY)
					return _DasFeed_latch(pF, nRet);
			}
			break;
		}

		case IOF_BODY:
			uTake = (pF->uNeed < uLen) ? pF->uNeed : uLen;
			if((nRet = DasBuf_write(pThis->pDb, pData, uTake)) != DAS_OKAY)
				return _DasFeed_latch(pF, nRet);
			pF->uNeed -= uTake; pData += uTake; uLen -= uTake;
			pThis->offset += uTake;
			if(pF->uNeed > 0)
				break;

			nRet = _DasIO_dispatchChunk(
				pThis, pThis->pDb, pF->nContent, pF->nPktId, &(pThis->pSd), pF->apOobs
			);
			if(nRet != DAS_OKAY)
				return _DasFeed_latch(pF, nRet);

			if(pThis->pExceptIn != NULL){
				/* The stream is over, close up now rather than at feedEnd */
				nRet = _DasIO_runCloseHandlers(pThis, pThis->pSd, pThis->pExceptIn);
				pThis->pExceptIn = NULL;
				pF->nState = IOF_STOP;
				pF->nErr = nRet;
				return nRet;
			}
			pF->nState = IOF_TAG;
			break;

		case IOF_DOC:
			if((nRet = _DasFeed_docAppend(pF, pData, uLen)) != DAS_OKAY)
				return _DasFeed_latch(pF, nRet);
			pThis->offset += uLen;
			uLen = 0;
			break;

		default:
			return _DasFeed_latch(pF, das_error(DASERR_IO, "Logic error in feed parser"));
		}
	}

	return DAS_OKAY;
}

DasErrCode DasIO_feedEnd(DasIO* pThis)
{
	DasFeed* pF = pThis->pFeed;
	DasErrCode nRet = DAS_OKAY;

	if(pF == NULL)
		return das_error(DASERR_IO, "Not a feed mode object, see new_DasIO_feed");

	if(pF->bEnded)
		return das_error(DASERR_IO, "DasIO_feedEnd called twice");

	switch(pF->nState){
	case IOF_ERR:
	case IOF_STOP:
		nRet = pF->nErr;
		break;
	case IOF_TAG:
		if(pF->bFirstRead)
			nRet = das_error(DASERR_IO, "Input stream %s contains no packets.",
				pThis->sName);
		else if(pF->uField != 0)
			nRet = das_error(DASERR_IO, "Partial packet in stream %s at offset %ld",
				pThis->sName, pThis->offset);
		break;
	case IOF_DOC:
		nRet = _DasIO_handleDoc(pThis, pF->pDoc, pF->uDocLen, pF->nContent);
		break;
	default:
		nRet = das_error(DASERR_IO, "Partial packet in stream %s at offset %ld",
			pThis->sName, pThis->offset);
		break;
	}

	/* An exception already ran the close handlers, see DasIO_feed */
	DasErrCode nHdlrRet = DAS_OKAY;
	if(pF->nState != IOF_STOP)
		nHdlrRet = _DasIO_runCloseHandlers(pThis, pThis->pSd, NULL);
	if((nRet == DAS_OKAY)&&(nHdlrRet != DAS_OKAY))
		nRet = nHdlrRet;

	/* The stream stays with this object until del_DasIO() or
	   DasIO_takeStream() */
	pThis->bReadDone = true;
	fini_DasComment(&(pF->cmt));
	fini_DasExcept(&(pF->ex));
	if(pF->pDoc != NULL){
		free(pF->pDoc);
		pF->pDoc = NULL;
	}

	pF->bEnded = true;
	if(nRet == DAS_OKAY){
		pF->nState = IOF_DONE;
	}
	else{
		pF->nState = IOF_ERR;
		pF->nErr = nRet;
	}
	return nRet;
}

/* ************************************************************************* */
/* Whole-stream convenience readers */

static DasStream* _stream_from_io(DasIO* pIn, const char* sWhat)
{
	DasIO_setModel(pIn, STREAM_MODEL_V3);  /* das2 packets come up as datasets */
	DasIO_setAccum(pIn, true);

	DasStream* pSd = NULL;
	if(DasIO_readAll(pIn) == DAS_OKAY)
		pSd = DasIO_takeStream(pIn);
	else
		daslog_error_v("Couldn't process the contents of %s", sWhat);

	del_DasIO(pIn);  /* frees the partial stream on failure */

	if(pSd != NULL)
		daslog_info_v("%zu Datasets retrieved from %s", DasStream_numDesc(pSd, DASDESC_UNK), sWhat);
	return pSd;
}

DasStream* new_DasStream_stdin(const char* sProgName)
{
	daslog_info("Reading das stream from standard input");
	DasIO* pIn = new_DasIO(sProgName, new_DasXport_stdin(), NULL);
	if(pIn == NULL) return NULL;
	return _stream_from_io(pIn, "standard input");
}

DasStream* new_DasStream_path(const char* sProg, const char* sFile)
{
	daslog_info_v("Reading %s", sFile);
	DasIO* pIn = new_DasIO(sProg, new_DasXport_file(sFile, "r"), NULL);
	if(pIn == NULL) return NULL;
	return _stream_from_io(pIn, sFile);
}

/* ************************************************************************* */
/* Task Tracking */

DasErrCode DasIO_setTaskSize(DasIO* pThis, int size) {
    if ( pThis->bSentHeader ) {
        return das_error(DASERR_OOB, "setTaskSize must be called before the stream "
					               "descriptor is sent.\n" );
    }
	 struct timeval tv;
    pThis->taskSize= size;

    gettimeofday(&tv, NULL);
	 
    /*pThis->tmLastProgMsg= ( (long)(tp.time)-1073706472 ) * 1000 + 
				                  (long)(tp.millitm);*/
	 pThis->tmLastProgMsg = ((double)tv.tv_sec) * 1000 + (tv.tv_usec / 1000.0);
    
    return DAS_OKAY;
}

DasErrCode DasIO_setTaskProgress( DasIO* pThis, int progress ) {
	if(pThis->bClosed)
		return _DasIO_closedErr(pThis, __func__);

	DasErrCode nRet = 0;
	char ss[64] = {'\0'};
	struct timeval tv;
	DasComment cmt;
	cmt.base.pkttype = OOB_COMMENT;
	
	long elapsedTimeMillis;
	double nowTime;
    
	static int decimate=1;
	static int decSz=1;
	int newDecSz;  

	const int targetUpdateRateMilli= 100; 

	decimate--;
	if(decimate == 0){ 
		gettimeofday(&tv, NULL); 
		
		/* nowTime = ( (long)(tp.time)-1073706472 ) * 1000 + (long)(tp.millitm); */
		
		nowTime = ((double)tv.tv_sec) * 1000 + (tv.tv_usec / 1000.0);
		
		elapsedTimeMillis = (int)(nowTime - pThis->tmLastProgMsg);

		/* target progress messages to 1 per targetUpdateRateMilli milliseconds by 
		 * decimation */

		/* calculate the decimation rate implied by the last interval.  
		 * Where +1 is added, this assumes that elapsedTimeMillis is large wrt 1, 
		 * also the decimate rate. */
		newDecSz = (decSz*targetUpdateRateMilli/(elapsedTimeMillis+1)) + 1; 
		decSz= 0.5 * decSz + 0.5 * newDecSz ; /* put in some intertia */

		decimate = decSz;

		sprintf( ss, "%d", progress );
		
		cmt.sType = "taskProgress";
		cmt.sVal = ss;
		cmt.sSrc = pThis->sName;
		cmt.uSrcLen = strlen(pThis->sName);
		pThis->tmLastProgMsg= nowTime;
		DasBuf* pBuf = pThis->pDb;
		DasBuf_reinit(pThis->pDb);
		
		if( (nRet = OobComment_encode(&cmt, pBuf)) != 0) return nRet;
		_DasIO_printf(pThis, "[xx]%06zu%.*s", DasBuf_written(pBuf),
		              (int)DasBuf_written(pBuf), _DasIO_bufText(pBuf));
    } 
	 
	return 0;
}

/* ************************************************************************* */
/* Top Level Send Functions */

DasErrCode DasIO_writeStreamDesc(DasIO* pThis, DasStream* pSd)
{
	if(pThis->rw == 'r')
		return das_error(DASERR_IO, "Can't write, this is an input stream.");
	if(pThis->bClosed)
		return _DasIO_closedErr(pThis, __func__);
	if(pThis->bSentHeader) 
		return das_error(DASERR_IO, "Can't double send a Das2 Stream Header");
	
	if(!DasDesc_has(&(pSd->base), "sourceId"))
		DasDesc_setStr(&(pSd->base), "sourceId", pThis->sName);
	
	/* 'c' in the mode means compress, and a reader only inflates what the
	   header declares, so the header has to say so */
	if(pThis->compressed)
		strncpy(pSd->compression, "deflate", STREAMDESC_CMP_SZ - 1);

	DasBuf* pBuf = pThis->pDb;
	DasBuf_reinit(pBuf);
	
	int nRet;
	if(pThis->dasver == STREAM_VER_2_2){
		if( (nRet = DasStream_encode2(pSd, pBuf)) != 0) return nRet;
		if( (nRet = _DasIO_d2LenChk(pThis, DasBuf_written(pBuf), "stream header")) != 0)
			return nRet;
		_DasIO_printf(pThis, "[00]%06zu%.*s", DasBuf_written(pBuf),
		              (int)DasBuf_written(pBuf), _DasIO_bufText(pBuf));
	}
	else{
		if( (nRet = DasStream_encode3(pSd, pBuf)) != 0) return nRet;
		_DasIO_printf(pThis, "|Sx||%zu|%.*s", DasBuf_written(pBuf),
		              (int)DasBuf_written(pBuf), _DasIO_bufText(pBuf));
	}
	
	/* The header itself goes out plain, everything after it compressed */
	if(strcmp( "deflate", pSd->compression ) == 0 ){
		DasXport* pDef = _new_DasXport_deflate(pThis->pX);
		if(pDef == NULL)
			return das_error(DASERR_IO, "%s: couldn't start compression", pThis->sName);
		pThis->pX = pDef;
	}
	
	if(pThis->taskSize > 0)
		nRet = DasIO_setTaskSize(pThis, pThis->taskSize);

	pThis->bSentHeader = true;
	return nRet;
}

/* The writer always names the ID for packet and dataset headers and data;
   0 is the stream header's.  A das2 tag has two digits and the reader gets
   the payload size from the header, so an ID that won't fit can't be fixed
   up on the far end, check before anything is written. */
static DasErrCode _DasIO_idChk(const DasIO* pThis, int iPktId)
{
	if(pThis->dasver == STREAM_VER_2_2){
		if((iPktId < 1)||(iPktId > 99))
			return das_error(DASERR_IO,
				"Packet ID %d is outside the das2 range of 1 to 99", iPktId
			);
	}
	else{
		if((iPktId < 1)||(iPktId >= DAS3_MAX_PKTIDS))
			return das_error(DASERR_IO,
				"Packet ID %d is outside the das3 range of 1 to %d", iPktId, DAS3_MAX_PKTIDS - 1
			);
	}
	return DAS_OKAY;
}

/* Position of an output ID in the sent lists, -1 if no header has gone out
   under it.  Data packets usually come in runs of one ID, so last time's
   answer is tried first. */
static ptrdiff_t _DasIO_sentIdx(DasIO* pThis, int iPktId)
{
	const uint16_t* pIds = (pThis->uSentCap == 0) ? pThis->aSentIdLoc : pThis->pSentIdHeap;

	if((pThis->uSentLast > 0)&&(pIds[pThis->uSentLast - 1] == iPktId))
		return (ptrdiff_t)(pThis->uSentLast - 1);

	for(size_t u = 0; u < pThis->uSent; u++){
		if(pIds[u] == iPktId){
			pThis->uSentLast = u + 1;
			return (ptrdiff_t)u;
		}
	}
	return -1;
}

/* Note that this descriptor's header is now the one in force for an output
   ID.  A later header under the same ID replaces the entry. */
static DasErrCode _DasIO_sentSet(
	DasIO* pThis, int iPktId, const DasDesc* pDesc, uint32_t uRev
){
	ptrdiff_t iIdx = _DasIO_sentIdx(pThis, iPktId);

	if(iIdx < 0){
		size_t uCap = (pThis->uSentCap == 0) ? DASIO_LOC_SENT : pThis->uSentCap;
		if(pThis->uSent == uCap){
			/* Rare, so plain malloc and copy rather than two reallocs that
			   could fail separately */
			size_t uNew = uCap * 2;
			uint16_t* pNewIds = (uint16_t*)malloc(uNew * sizeof(uint16_t));
			struct das_io_sent* pNew = (struct das_io_sent*)malloc(
				uNew * sizeof(struct das_io_sent)
			);
			if((pNewIds == NULL)||(pNew == NULL)){
				if(pNewIds != NULL) free(pNewIds);
				if(pNew != NULL) free(pNew);
				return das_error(DASERR_IO, "Can't track %zu output packet IDs", uNew);
			}
			if(pThis->uSentCap == 0){
				memcpy(pNewIds, pThis->aSentIdLoc, uCap * sizeof(uint16_t));
				memcpy(pNew, pThis->aSentLoc, uCap * sizeof(struct das_io_sent));
			}
			else{
				memcpy(pNewIds, pThis->pSentIdHeap, uCap * sizeof(uint16_t));
				memcpy(pNew, pThis->pSentHeap, uCap * sizeof(struct das_io_sent));
				free(pThis->pSentIdHeap);
				free(pThis->pSentHeap);
			}
			pThis->pSentIdHeap = pNewIds;
			pThis->pSentHeap = pNew;
			pThis->uSentCap = uNew;
		}
		iIdx = (ptrdiff_t)pThis->uSent;
		pThis->uSent += 1;
		if(pThis->uSentCap == 0) pThis->aSentIdLoc[iIdx] = (uint16_t)iPktId;
		else pThis->pSentIdHeap[iIdx] = (uint16_t)iPktId;
	}

	struct das_io_sent* pSent = (pThis->uSentCap == 0) ? pThis->aSentLoc : pThis->pSentHeap;
	pSent[iIdx].pDesc = pDesc;
	pSent[iIdx].uRev = uRev;
	return DAS_OKAY;
}

/* Data may only follow its own header: the last header this output carried
   under the ID has to be this descriptor's, as it stands now.  Catches data
   sent with no header, a header that went to some other output, another
   descriptor's header having taken over the ID since, and das2 planes that
   were changed after their header went out. */
static DasErrCode _DasIO_sentChk(
	DasIO* pThis, int iPktId, const DasDesc* pDesc, uint32_t uRev
){
	ptrdiff_t iIdx = _DasIO_sentIdx(pThis, iPktId);
	if(iIdx >= 0){
		const struct das_io_sent* pSent =
			(pThis->uSentCap == 0) ? pThis->aSentLoc : pThis->pSentHeap;
		if((pSent[iIdx].pDesc == pDesc)&&(pSent[iIdx].uRev == uRev))
			return DAS_OKAY;
	}
	return das_error(DASERR_IO, "Send packet header ID %02d first", iPktId);
}

DasErrCode DasIO_writeDesc(DasIO* pThis, DasDesc* pDesc, int iPktId)
{
	if(pThis->rw == 'r')
		return das_error(DASERR_IO, "Can't write, this is an input stream.");
	if(pThis->bClosed)
		return _DasIO_closedErr(pThis, __func__);
	
	das_desc_type_e type = DasDesc_type(pDesc);
	DasBuf* pBuf = pThis->pDb;
	DasErrCode nRet = DAS_OKAY;
	size_t uToWrite;

	if(type == DASDESC_STREAM){
		if(iPktId != 0)
			return das_error(DASERR_IO, "The stream header is always ID 0, not %d", iPktId);
		return DasIO_writeStreamDesc(pThis, (DasStream*)pDesc);
	}
	if((type == DASDESC_PKT)||(type == DASDESC_DS)){
		if((nRet = _DasIO_idChk(pThis, iPktId)) != DAS_OKAY) return nRet;
	}

	switch(type){
	case DASDESC_PKT: {
		if(! pThis->bSentHeader)
			return das_error(DASERR_IO, "Send the stream descriptor first");
		DasPkt* pPkt = (DasPkt*)pDesc;

		DasBuf_reinit(pBuf);
		if( (nRet = DasPkt_encode(pPkt, pBuf)) != DAS_OKAY) return nRet;

		if(pThis->dasver == STREAM_VER_2_2){
			if((nRet = _DasIO_d2LenChk(pThis, DasBuf_unread(pBuf), "packet header")) != 0)
				return nRet;
			uToWrite = DasBuf_unread(pBuf) + 10;
			if( _DasIO_printf(
				pThis, "[%02d]%06zu%.*s", iPktId, DasBuf_unread(pBuf),
				(int)DasBuf_unread(pBuf), _DasIO_bufText(pBuf)
			) != uToWrite)
				return das_error(DASERR_IO, "Partial packet descriptor written");
		}
		else{
			uToWrite = DasBuf_unread(pBuf) + 8;
			if( _DasIO_printf(
				pThis, "|Hx|%02d|%zu|%.*s", iPktId, DasBuf_unread(pBuf),
				(int)DasBuf_unread(pBuf), _DasIO_bufText(pBuf)
			) < uToWrite)
				return das_error(DASERR_IO, "Partial packet descriptor written");
		}
		return _DasIO_sentSet(pThis, iPktId, pDesc, pPkt->uHdrRev);
	}

	case DASDESC_DS: 
		DasBuf_reinit(pBuf);	
		if( (nRet = DasDs_encodeHdr((DasDs*)pDesc, pBuf)) != DAS_OKAY)
			return nRet;
		uToWrite = DasBuf_unread(pBuf) + 8;
		if( _DasIO_printf(
			pThis, "|Hx|%02d|%zu|%.*s", iPktId, DasBuf_unread(pBuf),
				(int)DasBuf_unread(pBuf), _DasIO_bufText(pBuf)
		) < uToWrite)
			return das_error(DASERR_IO, "Partial header written");

		return _DasIO_sentSet(pThis, iPktId, pDesc, 0);

	default:
		break;
	}
	return das_error(DASERR_IO, 
		"Descriptors of type '%s' don't have independent headers", 
		das_desc_type_toStr(type)
	);
}

DasErrCode DasIO_writePktBytes(
	DasIO* pThis, int iPktId, const ubyte* pData, size_t uLen
){
	if(pThis->rw == 'r')
		return das_error(DASERR_IO, "Can't write, this is an input stream.");
	if(pThis->bClosed)
		return _DasIO_closedErr(pThis, __func__);
	if(! pThis->bSentHeader)
		return das_error(DASERR_IO, "Send the stream descriptor first");

	int nRet = _DasIO_idChk(pThis, iPktId);
	if(nRet != DAS_OKAY) return nRet;

	if(pThis->dasver == STREAM_VER_2_2){
		nRet = _DasIO_printf(pThis, ":%02d:", iPktId);
	}
	else{
		nRet = _DasIO_printf(pThis, "|Pd|%d|%zu|", iPktId, uLen);
	}

	/* printf returns the bytes written, or -1 after a failure */
	if(nRet < 0)
		return das_error(DASERR_IO,
			"Short write of packet %d tag to %s", iPktId, pThis->sName
		);

	if(_DasIO_write(pThis, (const char*)pData, (int)uLen) != uLen)
		return das_error(DASERR_IO,
			"Short write of packet %d payload (%zu bytes) to %s", iPktId, uLen,
			pThis->sName
		);

	return DAS_OKAY;
}

DasErrCode DasIO_writeData(DasIO* pThis, DasDesc* pDesc, int iPktId)
{
	if(pThis->rw == 'r')
		return das_error(DASERR_IO, "Can't write, this is an input stream.");
	if(pThis->bClosed)
		return _DasIO_closedErr(pThis, __func__);

	if(! pThis->bSentHeader) 
		return das_error(DASERR_IO, "Send the stream descriptor first");

	int nRet = 0;
	DasBuf* pBuf = pThis->pDb;

	das_desc_type_e type = DasDesc_type(pDesc);
	
	if(type == DASDESC_PKT){
		/* Only prints one packet */

		DasPkt* pPkt = (DasPkt*)pDesc;
		if((nRet = _DasIO_idChk(pThis, iPktId)) != DAS_OKAY) return nRet;
		if((nRet = _DasIO_sentChk(pThis, iPktId, pDesc, pPkt->uHdrRev)) != DAS_OKAY)
			return nRet;

		DasBuf_reinit(pBuf);
		if( (nRet = DasPkt_encodeData(pPkt, pBuf)) != DAS_OKAY) return nRet;

		nRet = DasIO_writePktBytes(
			pThis, iPktId, (const ubyte*)_DasIO_bufText(pBuf), DasBuf_unread(pBuf)
		);
		if(nRet != DAS_OKAY) return nRet;
	}
	else if(type == DASDESC_DS){
		/* This may print many packets */

		DasDs* pDs = (DasDs*)pDesc;
		if((nRet = _DasIO_idChk(pThis, iPktId)) != DAS_OKAY) return nRet;
		if((nRet = _DasIO_sentChk(pThis, iPktId, pDesc, 0)) != DAS_OKAY)
			return nRet;

		ptrdiff_t aZeros[VARIDX_MAX] = VARIDX_INIT_BEGIN;
		ptrdiff_t nSz0 = DasDs_lengthIn(pDs, 0, aZeros);
		for(ptrdiff_t iIdx0 = 0; iIdx0 < nSz0; ++iIdx0){

			/* TODO: add "flush-on-full" to buffer to minimize writes */
			DasBuf_reinit(pBuf);

			nRet = DasDs_encodeData(pDs, pBuf, iIdx0);
			if(nRet != DAS_OKAY) return nRet;

			nRet = DasIO_writePktBytes(
				pThis, iPktId, (const ubyte*)_DasIO_bufText(pBuf), DasBuf_unread(pBuf)
			);
			if(nRet != DAS_OKAY) return nRet;
		}
	}
	else
		return das_error(DASERR_IO, 
			"Descriptors of type '%s' don't have packet data.", das_desc_type_toStr(type)
		);

	return DAS_OKAY;
}

/* Format into a growable string, reallocating only when the message is
   longer than any before.  For the DasIO scratch objects. */
static DasErrCode _DasIO_vstore(
	char** psDest, size_t* puLen, const char* sFmt, va_list ap
){
	va_list ap2;
	va_copy(ap2, ap);
	int nLen = vsnprintf(NULL, 0, sFmt, ap2);
	va_end(ap2);
	if(nLen < 0)
		return das_error(DASERR_IO, "Can't format message from '%s'", sFmt);

	if((*psDest == NULL)||((size_t)nLen + 1 > *puLen)){
		char* pNew = (char*)realloc(*psDest, (size_t)nLen + 1);
		if(pNew == NULL)
			return das_error(DASERR_IO, "Can't allocate %d bytes for a message", nLen + 1);
		*psDest = pNew;
		*puLen = (size_t)nLen + 1;
	}
	vsnprintf(*psDest, *puLen, sFmt, ap);
	return DAS_OKAY;
}

DasErrCode DasIO_exception(DasIO* pThis, const DasExcept* pSe)
{
	if(pThis->rw == 'r')
		return das_error(DASERR_IO, "Can't write, this is an input stream.");
	if(pThis->bClosed)
		return _DasIO_closedErr(pThis, __func__);

	DasErrCode nRet = DAS_OKAY;

	/* An exception may be all there is to say, the stream still needs a head */
	if(!pThis->bSentHeader){
		DasStream* pSd = new_DasStream();
		nRet = DasIO_writeStreamDesc(pThis, pSd);
		del_DasDesc((DasDesc*)pSd);
		if(nRet != DAS_OKAY) return nRet;
	}

	DasBuf_reinit(pThis->pDb);

	int nWrote = 0;
	if(pThis->dasver == STREAM_VER_2_2){
		if((nRet = OobExcept_encode(pSe, pThis->pDb)) == DAS_OKAY)
			nRet = _DasIO_d2LenChk(pThis, DasBuf_written(pThis->pDb), "exception");
		if(nRet == DAS_OKAY)
			nWrote = _DasIO_printf(pThis, "[xx]%06zu", DasBuf_written(pThis->pDb));
	}
	else{
		if((nRet = OobExcept_encode3(pSe, pThis->pDb)) == DAS_OKAY)
			nWrote = _DasIO_printf(pThis, "|Ex||%zu|", DasBuf_written(pThis->pDb));
	}
	if(nRet == DAS_OKAY){
		nWrote += _DasIO_write(pThis, _DasIO_bufText(pThis->pDb), DasBuf_written(pThis->pDb));
		if(nWrote <= 10)
			nRet = das_error(DASERR_IO, "Error writing exception");
	}

	/* Written or not, the stream is over */
	pThis->bExceptOut = true;
	DasIO_close(pThis);
	return nRet;
}

DasErrCode DasIO_exception_v(
	DasIO* pThis, das_except_e nType, const char* sFmt, ...
){
	if(pThis->rw == 'r')
		return das_error(DASERR_IO, "Can't write, this is an input stream.");
	if(pThis->bClosed)
		return _DasIO_closedErr(pThis, __func__);

	va_list ap;
	va_start(ap, sFmt);
	DasErrCode nRet = _DasIO_vstore(&(pThis->ex.sMsg), &(pThis->ex.uMsgLen), sFmt, ap);
	va_end(ap);
	if(nRet != DAS_OKAY) return nRet;

	pThis->ex.nType = nType;
	return DasIO_exception(pThis, &(pThis->ex));
}

DasErrCode DasIO_comment(DasIO* pThis, const DasComment* pSc)
{
	if(pThis->rw == 'r')
		return das_error(DASERR_IO, "Can't write, this is an input stream.");
	if(pThis->bClosed)
		return _DasIO_closedErr(pThis, __func__);

	if(!pThis->bSentHeader)
		return das_error(DASERR_OOB, "stream header not sent before comment packet!\n");

	DasErrCode nRet = 0;
	DasBuf_reinit(pThis->pDb);

	int nWrote;
	if(pThis->dasver == STREAM_VER_2_2){
		if((nRet = OobComment_encode(pSc, pThis->pDb)) != 0) return nRet;
		if((nRet = _DasIO_d2LenChk(pThis, DasBuf_written(pThis->pDb), "comment")) != 0)
			return nRet;
		nWrote = _DasIO_printf(pThis, "[xx]%06zu", DasBuf_written(pThis->pDb));
	}
	else{
		if((nRet = OobComment_encode3(pSc, pThis->pDb)) != 0) return nRet;
		nWrote = _DasIO_printf(pThis, "|Cx||%zu|", DasBuf_written(pThis->pDb));
	}
	nWrote += _DasIO_write(pThis, _DasIO_bufText(pThis->pDb), DasBuf_written(pThis->pDb));
	if(nWrote > 10) return 0;

	return das_error(DASERR_IO, "Error writing comment");
}

DasErrCode DasIO_comment_v(
	DasIO* pThis, const char* sType, const char* sFmt, ...
){
	if(pThis->rw == 'r')
		return das_error(DASERR_IO, "Can't write, this is an input stream.");
	if(pThis->bClosed)
		return _DasIO_closedErr(pThis, __func__);

	/* The source was set to the program name when this DasIO was made */
	das_store_str(&(pThis->cmt.sType), &(pThis->cmt.uTypeLen), sType ? sType : "");

	va_list ap;
	va_start(ap, sFmt);
	DasErrCode nRet = _DasIO_vstore(&(pThis->cmt.sVal), &(pThis->cmt.uValLen), sFmt, ap);
	va_end(ap);
	if(nRet != DAS_OKAY) return nRet;

	return DasIO_comment(pThis, &(pThis->cmt));
}
