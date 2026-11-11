/* Copyright (C) 2015-2026 Chris Piker <chris-piker@uiowa.edu>
 * Copyright (C) 2004-2006 Jeremy Faden <jeremy-faden@uiowa.edu> 
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

/** @file io.h Reading and writing Das2 Stream objects to standard I/O.
 */

#ifndef _das_io_h_
#define _das_io_h_

#include <stdio.h>
#include <das3/stream.h>
#include <das3/oob.h>
#include <das3/xport.h>

/** In-memory object model a reader builds, or a writer emits.
 *
 * The model is separate from the stream version.  A das 2.2 stream can be read
 * into das3 datasets (V3) for tools that only speak the newer model.
 */
typedef enum das_iomodel_e {
	DASIOMODEL_MIXED = -1, /* Readers only: build whatever each header says */
	DASIOMODEL_V2    =  2, /* DasPkt / DasPlane, fail on das3 headers */
	DASIOMODEL_V3    =  3  /* DasDs / DasDim / DasVar, up-convert das2 headers */
} das_iomodel;

#ifdef __cplusplus
extern "C" {
#endif

/** @defgroup IO Input/Output
 * Classes and functions reading and writing byte streams
 */

/** @addtogroup IO
 * @{
 */

/** Definition of the callback function invoked when a stream header is
 * encountered in the input.
 * @param pSd A pointer to the parsed DasStream
 * @param pUser The DasIOProc::pUser pointer, may be NULL.
 * @see DasIOProc
 */
typedef das_errcode (*dasfn_onStream)(DasStream* pSd, void* pUser);

/** Definition of the callback function invoked when a packet header is
 * encountered in the input.
 * @param pSd A pointer to the parsed Stream Descriptor
 * @param pPd A pointer to the parsed Packet Descriptor
 * @param nPktId The packet ID the header arrived under, 1 to 99
 * @param pUser The DasIOProc::pUser pointer, may be NULL.
 * @see DasIOProc
 */
typedef das_errcode (*dasfn_onPkt)(
	DasStream* pSd, DasPkt* pPd, int nPktId, void* pUser
);

/** Definition of the callback function invoked when a das2 packet
 * descriptor is about to be deleted.
 *
 * Some legacy das2 streams send a second, different, packet header under an
 * ID that is already in use.  A DasPkt holds a single record, so there is
 * nothing to keep: the old descriptor is deleted as soon as the last stream
 * processor returns from this call.  Write out anything you were holding for
 * it and drop every pointer to it and to its planes.
 *
 * Only das2 model reads make this call, dasfn_onPkt and dasfn_onPktData
 * are its companions.  Dataset reads never do. A DasDs is not deleted
 * before its stream is, @see dasfn_onDs.
 *
 * @param pSd A pointer to the parsed Stream Descriptor
 * @param pPd The packet descriptor about to be deleted
 * @param nPktId The packet ID it is filed under, 1 to 99
 * @param pUser The DasIOProc::pUser pointer, may be NULL.
 *
 * @returns DAS_OKAY to carry on.  Any other value stops the read with that
 *        code and the old descriptor is left in place.
 *
 * @see DasIOProc
 */
typedef das_errcode (*dasfn_onPktRedef)(
	DasStream* pSd, DasPkt* pPd, int nPktId, void* pUser
);

/** Callback function invoked when a data packet is encountered in the input.
 *
 * @note Decoding happens on demand.  If no processor supplies a packet data
 *   handler, DasIO reads past the data bytes without parsing them, so a
 *   program only pays for values it asked for.  To validate packet contents
 *   without otherwise using them, register a handler that does nothing.
 *
 * @param pPd A pointer to the parsed Packet Descriptor
 * @param nPktId The packet ID the data arrived under, 1 to 99
 * @param pUser The DasIOProc::pUser pointer, may be NULL.
 * @see DasIOProc
 */
typedef das_errcode (*dasfn_onPktData)(DasPkt* pPd, int nPktId, void* pUser);


/** Callback function invoked when a dataset header is encountered
 * on the input stream.
 *
 * DasDesc_parent() leads back to the stream that owns the dataset.
 *
 * The dataset stays where it is, and stays valid, until its stream is
 * deleted.  So the dataset itself is the thing to key on: hang per-dataset
 * state on it with DasDesc_setUser(), or keep the pointer, and release that
 * state in dasfn_onClose, not before.  Don't keep a table indexed by nPktId
 * instead.  Consider the ID to be a temporary tag that routes data packets.
 * In odd cases, a stream may hold more than one dataset under the same tag, 
 * see "Several datasets under one ID" in DasStream.
 *
 * @param pDs A pointer to a parsed DasDs (dataset) definition
 * @param nPktId The packet ID the header arrived under, 1 to 0xFEFF
 * @param pUser The DasIOProc::pUser pointer, may be NULL
 * @see DasIOProc
 */
typedef das_errcode (*dasfn_onDs)(DasDs* pDs, int nPktId, void* pUser);

/** Callback function invoked when new data packets for a dataset are
 * encountered on the stream.
 *
 * The dataset holds only the current packet's records: DasIO clears the
 * previous ones before decoding, unless DasIO_setAccum() was turned on.
 *
 * @note Decoding happens on demand.  If no processor supplies a dataset data
 *   handler (and accumulation is off), DasIO reads past the data bytes
 *   without parsing them, so a program only pays for values it asked for.
 *   To validate packet contents without otherwise using them, register a
 *   handler that does nothing.
 *
 * @see DasIO_setAccum()
 *
 * @param pDs The dataset that received the records, the same pointer that
 *        was given to dasfn_onDs.  It is what identifies the dataset.
 * @param nPktId The packet ID the data arrived under, 1 to 0xFEFF
 * @param pUser The DasIOProc::pUser pointer, may be NULL
 * @see DasIOProc
 */
typedef das_errcode (*dasfn_onDsData)(DasDs* pDs, int nPktId, void* pUser);

/** Callback function invoked with the undecoded bytes of a data packet.
 *
 * Raw handlers run before any decoding, so a program that registers only
 * these never pays for value parsing at all.
 *
 * The buffer holds exactly one packet payload with the read point at its
 * start; DasBuf_data() gives the pointer and length. The buffer is reused
 * for the next packet, so copy what you need before returning.  DasIO restores
 * the read point after each call, so multiple stream processors handlers may
 * use the buffer.
 *
 * @param pDesc The active descriptor for the payload's packet ID, a DasPkt
 *        for das2 model reads or a DasDs for das3, so check DasDesc_type()
 *        before casting.  It is what DasIO used to find the payload length
 *        in a das2 stream.
 * @param pBuf The payload bytes
 * @param nPktId The packet ID the data arrived under
 * @param pUser The DasIOProc::pUser pointer, may be NULL
 * @see DasIOProc
 */
typedef das_errcode (*dasfn_onRawData)(
	DasDesc* pDesc, const DasBuf* pBuf, int nPktId, void* pUser
);

/** Callback functions that are invoked when a stream ends
 *
 * The last call a handler gets.  This function will be called once
 * no matter however the stream ended.
 *
 * A stream ends normally at the end of input, or on an exception: reading
 * stops at the exception and no packet after it is seen.  Cleanup such as
 * flushing partial results or finishing an output file goes here.
 *
 * Here's an example of a filter preforming packet flush cleanup before
 * passing on an execption:
 * @code
 *   das_errcode onClose(DasStream* pSd, const DasExcept* pExcept, void* pUser)
 *   {
 *       flushBins(pUser);
 *       if(pExcept == NULL) return DAS_OKAY;
 *       DasIO_exception(pOut, pExcept);
 *       return (DasExcept_type(pExcept) == DASEXCEPTTYPE_NO_DATA) ? DAS_OKAY : MY_ERR;
 *   }
 * @endcode
 *
 * A "No data" execption is a normal outcome, but an exception noting
 * "No Data" will be the last packet received by a stream processor.
 * This situatio is not a failure, so a program that gets one should still
 * succeed. Any other execption indicates an abnormal codition.
 *
 * @param pSd The stream that was read.  NULL only when the stream ended on
 *        an exception before its header arrived.
 * @param pExcept The exception that ended the stream, or NULL.  It belongs
 *        to the DasIO and is good only until the callback returns.
 * @param pUser The DasIOProc::pUser pointer, may be NULL.
 * @see DasIOProc
 */
typedef das_errcode (*dasfn_onClose)(
	DasStream* pSd, const DasExcept* pExcept, void* pUser
);

/** Callback functions that handle comments
 *
 * The comment belongs to the DasIO, which reuses it for the next one, so it
 * and every value read from it are good only until the callback returns.
 *
 * @param pSd The stream the comment arrived on.  NULL if the comment came
 *        before the stream header.
 * @param pCmt The parsed comment
 * @param pUser The DasIOProc::pUser pointer, may be NULL.
 * @see DasIOProc
 */
typedef das_errcode (*dasfn_onComment)(
	DasStream* pSd, const DasComment* pCmt, void* pUser
);


/** The first DasIOProc layout, das 3.0.
 *
 * Structure versions hold the library major version in the high byte and
 * the minor in the low byte.  A version only changes when the structure or
 * one of its callback signatures does, so it can lag the library version.
 */
#define DASIOPROC_V3_0 ((3 << 8) | 0)

/** The DasIOProc layout described by this header */
#define DASIOPROC_VER DASIOPROC_V3_0

/** A set of callbacks used for input and output stream processing
 *
 * Start from init_DasIOProc() and set the callbacks you need.  A
 * callback left NULL means the processor has no interest in that event.  For
 * most events nothing happens, see onClose for the one that has a library
 * default.
 *
 * @code
 *   DasIOProc proc;
 *   init_DasIOProc(&proc, &myState);
 *   proc.onDsData = myDataHandler;
 *   DasIO_addProc(pIn, &proc);
 * @endcode
 *
 * However a stream ends, normally or on an exception, each processor's
 * onClose is its last call, see dasfn_onClose.
 *
 * The input stream belongs to the DasIO.  Processors read it, and may hang
 * their own data on descriptors with DasDesc_setUser(), but don't add,
 * remove or replace its descriptors.  Programs that transform a stream build
 * a separate output stream instead.
 *
 * @ingroup streams
 */
typedef struct das_ioproc_t {
	/** The layout this program was compiled against, set by
	 * init_DasIOProc(), leave it alone.
	 *
	 * Callback signatures can change between versions without changing the
	 * structure size, so the library needs the version and not just a size to
	 * call them correctly.  A zero or unknown version is refused.
	 */
	size_t uStructVer;

	/** The function to be called when the stream header is read in.
	 * This is the header with the element \<stream\>\</stream\> in the input
	 * file.
	 */
	dasfn_onStream onStream;

	/** Sets the function to be called when each \<packet\\> element
	 * is read in.
	 */
	dasfn_onPkt onPkt;

	/** Sets the function to be called before a das2 packet descriptor is
	 * deleted, see dasfn_onPktRedef (legacy das2 streams only) */
	dasfn_onPktRedef onPktRedef;

	/** Sets the function to be called when each data packet is read in. */
	dasfn_onPktData onPktData;

	/** Sets the function to be called when each dataset definition is read
	 * in (das3) */
	dasfn_onDs onDs;

	/** Sets the function to be called when each dataset receives new data
	 * (das3) */
	dasfn_onDsData onDsData;

	/** Sets the function to be called with the raw bytes of each data packet,
	 * before any decoding.  Works for das2 and das3 input alike. */
	dasfn_onRawData onRawData;

	/** Receives stream annotations.
	 *
	 * These include progress messages, log files, informational messages, and
	 * other messages humans might be interested in.  If NULL comments are
	 * discarded.
	 *
	 * A filter passing on "taskProgress" comments may want to send them
	 * with DasIO_setProgress() instead, which rate limits them so they
	 * don't swamp the output stream.
	 */
	dasfn_onComment onComment;

	/** Sets the function to be called when the stream ends, normally or on
	 * an exception.
	 *
	 * If no processor attached to the DasIO has an onClose, the library
	 * default runs instead: it does nothing at a normal end or on "no data",
	 * and reports any other exception and returns an error, so an unhandled
	 * exception makes the read fail.  Once any processor supplies one, the
	 * default is off and the processors that left this NULL are skipped, so
	 * a passive one, like a progress monitor, can't fail a read that another
	 * handled.  To ignore exceptions, supply a handler that returns DAS_OKAY.
	 */
	dasfn_onClose onClose;

	/** An optional User-data pointer that is passed along to all callbacks.
	 * This value may be NULL.
	 */
	void* pUser;

} DasIOProc;

/** Initialize a stream processor for the layout given
 *
 * Use init_DasIOProc() instead, which supplies the version this
 * program was compiled against.
 *
 * @param pThis The stream processor structure to initialize
 * @param pUser The value for DasIOProc::pUser, may be NULL
 * @param uVersion The structure layout pThis has, one of the
 *        DAS_STREAM_PROC_V* values.
 * @returns DAS_OKAY, or an error if uVersion is unknown to this library,
 *        in which case pThis is not touched.
 */
DAS_API das_errcode init_DasIOProc_ver(
	DasIOProc* pThis, void* pUser, size_t uVersion
);

/** Initialize a stream processor, all callbacks NULL
 *
 * This is a macro so that the version recorded is the one from the header
 * the calling program was compiled with, not the one the library was.  A
 * later library can then still recognize, and possibly serve, an older
 * program's structure.
 *
 * @param P A pointer to the DasIOProc to initialize
 * @param U A pointer passed to every callback, or NULL
 * @returns DAS_OKAY, or an error if the library is older than the header
 */
#define init_DasIOProc(P, U) \
	init_DasIOProc_ver((P), (U), DASIOPROC_VER)


/** @class DasIO io.h <das3/io.h>
 *
 * Tracks input and output operations for das2 stream headers and data.
 * 
 * Members of this class handle overall stream operations reading writing 
 * packets, checking packet lengths, passing XML string data off to descriptor
 * object constructors, triggering processing callbacks and most other general
 * Das2 Stream IO tasks.
 *
 * In the default build, packets up to 16 MiB are read and written,
 * headers and data alike; a longer one is refused with an error.
 * Memory follows the stream: a DasIO
 * starts with a 16 KiB packet buffer and grows it only as far as the
 * largest packet it meets.  das2 streams have a lower limit of their own,
 * since their length fields have six digits: 999,999 bytes.
 *
 * The structure is opaque.
 */
typedef struct das_io_t DasIO;

/** @} */


/** Create a DasIO object that reads or writes a stream through a transport
 *
 * Every byte source works the same way: open a transport for it, then
 * hand the transport to a DasIO.
 *
 * @code
 *   // Filter programs, standard input to standard output
 *   DasIO* pIn  = new_DasIO(PROG, new_DasXport_stdin(), NULL);
 *   DasIO* pOut = new_DasIO(PROG, new_DasXport_stdout(), "3");   // write das3
 *
 *   // A file, or the output of a reader program
 *   DasIO* pFile = new_DasIO(PROG, new_DasXport_file("ex01.d3b", "r"), NULL);
 *   DasIO* pCmd  = new_DasIO(PROG, new_DasXport_cmd("my_reader 2017-01-01 2017-01-02"), NULL);
 *
 *   // The body of an http or https download
 *   DasHttpResp* pRes = new_DasHttpResp_get(sUrl, PROG, pMgr, DASHTTP_TIMEOUT_MIN);
 *   if(DasHttpResp_isOk(pRes))
 *       pIn = new_DasIO(PROG, DasHttpResp_takeBody(pRes), NULL);
 *   del_DasHttpResp(pRes);
 * @endcode
 *
 * The DasIO reads or writes as the transport was opened.  A socket opened
 * "rw" goes both ways, so for one of those the mode has to say 'r' or 'w'.
 *
 * The DasIO owns the transport and frees it with del_DasIO().  It is freed
 * even if this constructor fails, so the calls nest without a leak.  What
 * the transport borrowed, such as stdin or a socket, stays open, see
 * DasXport.
 *
 * When a stream header asks for compression="deflate" the DasIO
 * decompresses everything after it.  A writer opened with 'c' in its mode
 * declares that in the header it sends and compresses what follows.
 *
 * @param sProg A name for the program or source, used in error and log
 *        messages
 *
 * @param pX The transport.  NULL gives NULL after an error report, so a
 *        failed open, or a failed DasHttpResp_takeBody(), can be passed
 *        straight in.
 *
 * @param mode Stream settings, NULL or "" for the defaults.  Letters:
 *        - '2' or '3': a reader accepts only that version's packet tags and
 *          fails on the other; a writer writes that version.  Without
 *          either, a reader takes both and a writer writes das v2.2.
 *        - 'c': a writer compresses everything after the stream header.
 *          Readers decompress when the header says to, so 'c' is refused
 *          for them.
 *        - 'r' or 'w': needed only for a transport opened "rw".  Given
 *          anyway, it must match how the transport was opened.
 *
 * @return A new DasIO object allocated on the heap, or NULL on a bad
 *         argument
 *
 * @memberof DasIO
 */
DAS_API DasIO* new_DasIO(const char* sProg, DasXport* pX, const char* mode);

/** Set the in-memory data model.
 * 
 * On a reader this selects what the callbacks receive: DASIOMODEL_V2 keeps
 * das2 packets as DasPkt and fails on das3 headers, DASIOMODEL_V3
 * up-converts das2 headers to DasDs, DASIOMODEL_MIXED hands each header
 * over as whatever it was.  On a writer only V2 and V3 are legal since one
 * stream has one header dialect.
 * 
 * @returns DAS_OKAY if successful or an error code if not.
 * @memberof DasIO
 */
DAS_API das_errcode DasIO_setModel(DasIO* pThis, das_iomodel nModel);

/** Get the in-memory data model, see DasIO_setModel()
 * @memberof DasIO
 */
DAS_API das_iomodel DasIO_model(const DasIO* pThis);

/** Set the stream version a writer emits.
 *
 * Writers default to the version given in the constructor mode string, or
 * DASSTREAMVER_2_2 when the mode string names none.  Only DASSTREAMVER_2_2 and
 * DASSTREAMVER_3_0 are writable, and the version can't change once the stream
 * header has gone out.
 *
 * @returns DAS_OKAY, or an error code on a reader, an unwritable version, or
 *          a writer whose header has already been sent.
 * @memberof DasIO
 */
DAS_API das_errcode DasIO_setOutVer(DasIO* pThis, das_streamver nVer);

/** Get the stream version a writer will emit, see DasIO_setOutVer()
 *
 * On a reader this is DASSTREAMVER_UNK unless the constructor mode string
 * restricted the input to one version.
 * @memberof DasIO
 */
DAS_API das_streamver DasIO_outVer(const DasIO* pThis);

/** True once a writer has emitted the stream header.
 *
 * Comments need a header to follow.  DasIO_exception() sends a stub header
 * itself when none went out.
 * @memberof DasIO
 */
DAS_API bool DasIO_hasSentHeader(const DasIO* pThis);

/** Recover undecodable embedded formats as opaque bytes instead of failing.
 *
 * By default a stream carrying an embedded format (an encoding="blob" value with
 * a mime, e.g. "image/png") whose codec extension is not registered is rejected
 * with a named error.  When this option is enabled the reader instead ingests
 * the raw bytes as a degenerate per-record blob and records the lost declared
 * morphology (mime, index, semantic, storage) as "embedded*" properties on the
 * variable.  This is lossy: the decoded index structure is NOT preserved.
 *
 * @param pThis The DasIO object to configure, must be in read mode
 * @param bEnable true to recover embedded formats as bytes, false (default) to
 *        fail loud on an undecodable embedded format.
 *
 * @memberof DasIO
 */
DAS_API void DasIO_setEmbedAsBytes(DasIO* pThis, bool bEnable);

/** Keep dataset records in memory across packets
 *
 * By default a reader drops a dataset's record-varying values before each
 * new data packet is decoded, so memory use stays flat and an onDsData
 * callback sees only the current packet's records.  With accumulation on, records
 * pile up for the life of the stream instead.  Turn it on to read a whole
 * stream into memory (see DasIO_takeStream()), or when a handler buffers
 * several packets before flushing, in which case the handler calls
 * DasDs_clearIdx0() itself after each flush.
 *
 * Accumulation also counts as a request for values: datasets are decoded
 * even when no processor has an onDsData callback.
 *
 * Only affects das3 datasets (including das2 packets read with the
 * DASIOMODEL_V3 model).  das2 packet descriptors only ever hold one packet.
 *
 * @param pThis The DasIO object to configure, must be in read mode
 * @param bAccum true to keep all records, false (default) to clear before
 *        each data packet
 *
 * @memberof DasIO
 */
DAS_API void DasIO_setAccum(DasIO* pThis, bool bAccum);

/** Is record accumulation on?  See DasIO_setAccum()
 * @memberof DasIO
 */
DAS_API bool DasIO_isAccum(const DasIO* pThis);


/** Create a new DasIO object that parses bytes pushed into it
 *
 * Unlike the other read-mode constructors, a feed-mode DasIO has no
 * transport.  The caller acquires bytes however it likes (async socket
 * library, browser fetch() under WebAssembly, etc.) and pushes them into
 * the parser with DasIO_feed().  Stream handler callbacks fire from
 * inside DasIO_feed() as each packet completes, exactly as they would
 * from inside DasIO_readAll().
 *
 * Since the parser never blocks waiting on input, it is suitable for use
 * in single-threaded event-loop environments where DasIO_readAll() can
 * not be called.
 *
 * Limitations compared to transport-mode reading:
 *   - Streams that declare compression="deflate" are rejected.  (No
 *     deflated das streams are known to exist in the wild.)
 *
 * @param sProg A name for the program/source, used in error and log
 *        messages
 *
 * @param mode A string containing the packet tag mode, one of:
 *        - 'r' read either tag type
 *        - 'r2' read only das v2 packet tags (error on anything else)
 *        - 'r3' read only das v3 packet tags (error on anything else)
 *
 * @return A new DasIO object allocated on the heap, or NULL on a bad
 *         mode string
 *
 * @memberof DasIO
 */
DAS_API DasIO* new_DasIO_feed(const char* sProg, const char* mode);

/** Push bytes into a feed-mode DasIO
 *
 * Bytes may be supplied in chunks of any size, down to a single byte;
 * packet boundaries need not be respected.  Whenever enough bytes have
 * accumulated to complete a packet, the packet is dispatched through the
 * attached DasIOProc callbacks before the call returns.  Bytes that
 * do not complete a packet are held internally until the next call.
 *
 * Once any call returns an error the parser is latched in the error
 * state and further calls return the same error.
 *
 * @param pThis A DasIO created by new_DasIO_feed()
 * @param pData The bytes to consume, may not be NULL unless uLen is 0
 * @param uLen  The number of bytes to consume
 *
 * An exception in the stream ends it: the close handlers run as soon as
 * it arrives, and any bytes fed after that are ignored.
 *
 * @returns DAS_OKAY if all bytes were consumed, or a positive error
 *          code if the stream is invalid or a handler callback failed.
 *
 * @memberof DasIO
 */
DAS_API das_errcode DasIO_feed(DasIO* pThis, const uint8_t* pData, size_t uLen);

/** Signal end-of-input to a feed-mode DasIO
 *
 * Call this exactly once after the last DasIO_feed() call.  Close
 * handlers for the attached stream processors are invoked here, unless an
 * exception in the stream already ran them, and an input that ends
 * mid-packet is reported here.  After this call the only
 * valid operations on the object are DasIO_getStream(), DasIO_takeStream()
 * and del_DasIO().
 *
 * @param pThis A DasIO created by new_DasIO_feed()
 *
 * @returns DAS_OKAY if the input ended on a packet boundary and all
 *          close handlers succeeded, a positive error code otherwise.
 *
 * @memberof DasIO
 */
DAS_API das_errcode DasIO_feedEnd(DasIO* pThis);

/** Free resources associated with a DasIO structure
 * Typically you don't need to do this as heap memory is free'ed when the
 * program exits anyway.  But if you need a destructor, here it is.
 *
 * A reader's stream, with every descriptor and dataset in it, is freed here
 * unless DasIO_takeStream() detached it first.
 * 
 * @param pThis The DasIO object to delete, which frees its transport too.
 *        Whatever the transport borrowed, a FILE* or a socket, stays open
 *        for its owner.  May be NULL, in which case nothing is done.
 */
DAS_API void del_DasIO(DasIO* pThis);



/** Add a packet processor to be invoked during I/O operations
 * 
 * A DasIO object holds a small fixed number of packet processors (ten today)
 * and this function returns an error once the table is full.  Each processor
 * contains one or more callback functions that will
 * be invoked and provided with ::DasDesc objects.  
 * 
 * During stream read operations, processors are invoked after the header or
 * data have been serialized from disk.  During stream write operations
 * processors are invoked before the write operation is completed.
 *
 * The DasIO keeps the pointer, not a copy, so the processor must outlive the
 * read.
 * 
 * @param pThis The DasIO object to receive the processor.
 * @param pProc The processor to add, prepared by init_DasIOProc().  A
 *        structure with a zero or unknown DasIOProc::uStructVer is
 *        refused.
 * @return The number of packet processors attached to this DasIO object or
 *         a negative error value if there is a problem.
 * 
 * @memberof DasIO
 */
DAS_API int DasIO_addProc(DasIO* pThis, DasIOProc* pProc);

/** Starts the processing of the stream read from FILE* infile.  
 *
 * This function does not return until all input has been read, an
 * exception in the stream ended it, or an error occurred.  If a
 * DasIOProc has been set with DasIO_addProc() then as each header
 * packet read, callbacks specified in stream processors will be 
 * invoked.  Otherwise all data is merely parsed and then discarded.
 * Reading stops at an exception or the end of the input.
 *
 * An exception other than "no data" makes the read fail unless some
 * processor's onClose callback says otherwise, and that includes reads with
 * no processors.
 *
 * The DasStream structure that build up during reading remains attached
 * to the DasIO object afterwards, use DasIO_takeStream() to insure 
 * it is not deleted when del_DasIO() is called.
 * 
 * Each DasIO object reads one stream, calling this a second time
 * is an error.
 * 
 * @returns a status code. 0 indicates that all processing ended with no 
 *          errors, otherwise a non-zero value is returned.
 * 
 * @memberof DasIO
 */
DAS_API int DasIO_readAll(DasIO* pThis);

/** Get the stream being read, without taking ownership
 *
 * Valid at any time on a reader, including between DasIO_feed() calls, so an
 * event-loop program can look at partial data as it arrives.  The pointer
 * stays valid until del_DasIO() or DasIO_takeStream().
 *
 * @returns The stream, or NULL before the stream header has been read, on an
 *          output object, or after DasIO_takeStream().
 *
 * @memberof DasIO
 */
DAS_API DasStream* DasIO_getStream(DasIO* pThis);

/** Detach the stream that was read, the caller owns it afterwards
 *
 * A reader keeps the stream it builds, with all its descriptors and datasets,
 * until del_DasIO().  Call this to keep the stream past that point.  Combine
 * with DasIO_setAccum() to read a whole stream into memory:
 *
 * @code
 * DasIO* pIn = new_DasIO("myprog", new_DasXport_file("input.d3b", "r"), NULL);
 * DasIO_setModel(pIn, DASIOMODEL_V3);  // das2 packets come up as datasets
 * DasIO_setAccum(pIn, true);
 * DasStream* pSd = NULL;
 * if(DasIO_readAll(pIn) == DAS_OKAY)
 *    pSd = DasIO_takeStream(pIn);
 * del_DasIO(pIn);
 * if(pSd != NULL){
 *    DasStream_cacheShapes(pSd, true);   // optional, data won't change now
 *    ...
 *    del_DasDesc((DasDesc*)pSd);
 * }
 * @endcode
 *
 * Only allowed once reading is over, after DasIO_readAll() returns or
 * DasIO_feedEnd() runs, successfully or not.  A failed read leaves whatever
 * was parsed before the error.
 *
 * @returns The stream, or NULL if no stream header was read or the stream
 *          was already taken.  Also NULL, with a daserrf, on an output
 *          object or while reading is still in progress.
 *
 * @memberof DasIO
 */
DAS_API DasStream* DasIO_takeStream(DasIO* pThis);

/** Read all of standard input into memory
 *
 * Reads with the DASIOMODEL_V3 model and accumulation on, so das2
 * packets come back as datasets.
 *
 * @param sProgName the name of the program for log writing.
 *
 * @returns The stream, owned by the caller, or NULL on a read error
 * @memberof DasStream
 */
DAS_API DasStream* new_DasStream_stdin(const char* sProgName);

/** Read all of a file into memory
 *
 * Reads with the DASIOMODEL_V3 model and accumulation on, so das2
 * packets come back as datasets.
 * 
 * @param sProg The name of the program for log writing.
 *
 * @param sFile The name of a file to read
 * 
 * @returns The stream, owned by the caller, or NULL on a read error
 * @memberof DasStream
 */
DAS_API DasStream* new_DasStream_file(const char* sProg, const char* sFile);


/** Writes the data describing the stream to the output channel (e.g. File* ).
 *
 * This serializes the descriptor structure into XML and writes it out.
 * 
 * The DasStream object type and version will be set before serialization
 * 
 * @param pThis The IO object, must be set up in a write mode
 * @param pSd The stream descriptor to serialize
 * @returns 0 on success a positive integer error code other wise.
 * @memberof DasIO
 */
DAS_API das_errcode DasIO_writeStreamDesc(DasIO* pThis, DasStream* pSd);

/** Serialize a top-level descriptor to an destination
 * 
 * Handles DasStream, das2 DasPkt and das3 DasDs items.  Send the stream
 * first, then a header for each packet ID before any of its data.
 * 
 * @param pThis the IO object to use for sending the data
 * 
 * @param pDesc A top level in-band data descriptor such as DasPkt or DasDs.
 * 
 * @param iPktId The packet ID to tag the header with.  Must be 0 for a
 *        DasStream, which is always ID 0.  For a DasPkt or DasDs it must be
 *        1 to 99 when writing a das2 stream, or 1 to 0xFEFF for das3; 0 is
 *        refused.  The writer chooses: it need not be the ID the descriptor
 *        has in its own stream, though keeping the input IDs is usual for
 *        filters.  For example, in a dasfn_onPkt handler:
 *        @code
 *          return DasIO_writeDesc(pOut, (DasDesc*)pPdOut, iPktId);
 *        @endcode
 *
 * @returns DAS_OKAY if the operation was successful or an positive value on an error.
 * 
 * @memberof DasIO
 */
DAS_API das_errcode DasIO_writeDesc(DasIO* pThis, DasDesc* pDesc, int iPktId);

/** Send all packet data associated with a descriptor
 * 
 * A DasPkt writes the one packet of values currently loaded in its planes.
 * A DasDs writes one packet for each index in its first dimension.
 * 
 * @param pThis the IO object to use for sending the data
 * @param pDesc A descriptor that can write packetized data, in this case
 *              one of PACKET, or DATASET
 * @param iPktId The packet ID to tag the data with, same rules as for
 *              DasIO_writeDesc()
 * @returns DAS_OKAY if the operation was successful or a positive value on error.
 * 
 * @memberof DasIO
 */
DAS_API das_errcode DasIO_writeData(DasIO* pThis, DasDesc* pDesc, int iPktId);

/** Write one data packet whose payload is already encoded.
 *
 * Emits the packet tag in the stream's own version, ":NN:" for das2 and
 * "|Pd|N|len|" for das3, followed by the bytes unchanged.  For pass-through
 * tools that re-tag packets without decoding them.  The header for iPktId
 * must already have been sent; this function has no descriptor to check
 * that against, so the caller owns the promise.
 *
 * @param pThis the IO object to use for sending the data
 * @param iPktId the packet ID, 1 to 99 for das2 output, 1 to 0xFEFF for das3
 * @param pData the encoded payload
 * @param uLen the payload length in bytes
 *
 * @returns DAS_OKAY, or an error code on a short write
 *
 * @memberof DasIO
 */
DAS_API das_errcode DasIO_writePktBytes(
	DasIO* pThis, int iPktId, const uint8_t* pData, size_t uLen
);

/** End an output stream with an exception
 *
 * An exception terminates a stream with an error, and povides a general
 * error category, for example the input arguments were bad, or some 
 * off nominal server condition occured. It's always the last thing on a 
 * stream.  Sending an exception triggers an orderly shutdown of the 
 * IO processor.  If this is not the desired bedavior then sentd an 
 * comment with "log:error" as the type. 
 * 
 * "No data" is a stream terminating condition the technically isn't
 * out of the ordinary, but since it's always the last packet anyway
 * the exception handling machinery is used for this purpose.  Exception
 * type DASEXCEPTTYPE_NO_DATA is used for this purpos.
 *
 * If no stream header has been sent yet, DasIO sends a stub header 
 * first.  DasIO then closes any input streams, but the object itself
 * is not deleted, so call del_DasIO() as usual.
 * 
 * Any write to a DasIO instance is an error after calling DasIO_exception().
 *
 * The exception is serialized before this function returns and no
 * reference to it is kept.  A filter may pass on a received exception in
 * its onClose callback as given.
 *
 * @param pThis An output stream
 * @param pExcept The exception to send
 * @returns DAS_OKAY, or a positive error code if it could not be written
 *
 * @memberof DasIO
 */
DAS_API das_errcode DasIO_exception(DasIO* pThis, const DasExcept* pExcept);

/** End an output stream with an exception built from a printf style format
 *
 * The same as DasIO_exception() without making a DasExcept:
 * @code
 *   return DasIO_exceptionf(pOut, DASEXCEPTTYPE_QUERY_ERR, "Unknown option %s", sOpt);
 * @endcode
 *
 * The message is built in space the DasIO keeps for the purpose.
 *
 * @param pThis An output stream
 * @param nType DASEXCEPTTYPE_NO_DATA, DASEXCEPTTYPE_SERVER_ERR or DASEXCEPTTYPE_QUERY_ERR
 * @param sFmt  A printf style format for the message.  To send text that
 *        may hold a '%', pass "%s" and the text.
 * @returns DAS_OKAY, or a positive error code if it could not be written
 *
 * @memberof DasIO
 */
DAS_API das_errcode DasIO_exceptionf(
	DasIO* pThis, das_excepttype nType, const char* sFmt, ...
);

/** Output a comment
 *
 * Comments are mostly for people and may change without affecting
 * processing.  Structured comments used to be used for progress
 * messages, but it's more efficent for a stream handler to just whatch
 * the current values of coordinate dimensions to determine processing
 * state. The given comment is erialized before this returns and no
 * reference to it is kept, thus a filter may pass on a comments 
 * it's received from upstream.
 * 
 * The stream header must already have been sent.
 *
 * @memberof DasIO
 */
DAS_API das_errcode DasIO_comment(DasIO* pThis, const DasComment* pCmt);

/** Output a comment built from a printf style format
 *
 * The quick way to tell whoever reads the stream what is going on, without
 * making a DasComment.  The source is the program name given when the DasIO
 * was made.
 *
 * @code
 *   DasIO_commentf(pOut, "log:info", "Searching file system...");
 *   DasIO_commentf(pOut, "log:warning", "Skipped %d bad records", nBad);
 * @endcode
 *
 * The comment is built in DasIO reserved memory so nothing is allocated
 * per call. As with any comment, the stream header must already
 * have been sent.
 *
 * @param pThis An output stream
 * @param sType Any token.  Readers know "log:" followed by a level name
 *        ("log:info", "log:warning", "log:error", ...), and the progress
 *        pair "taskSize" and "taskProgress" (see DasIO_setProgress()).
 * @param sFmt  A printf style format for the comment value.  To send text
 *        that may hold a '%', pass "%s" and the text.
 * @returns DAS_OKAY, or a positive error code if the comment could not be
 *        written
 *
 * @memberof DasIO
 */
DAS_API das_errcode DasIO_commentf(
	DasIO* pThis, const char* sType, const char* sFmt, ...
);


/* Identifies the size of task for producing a stream, in arbitrary units. 
 * 
 * This number is used to generate a relative progress position, and also as
 * a weight if several Das2 Streams (implicitly of similar type) are combined.
 * See setTaskProgress().
 * 
 * @memberof DasIO
 */
DAS_API das_errcode DasIO_setProgressMax(DasIO* pThis, int size);

/** Place rate-limited progress comments on an output stream.
 * 
 * Messages are decimated dynamically to try to limit the stream comment rate to
 * about 10 per second (see targetUpdateRateMilli).  If Note the tacit assumption
 * that is the stream is
 * reread, it will be reread at a faster rate than it was written.  Processes
 * reducing a stream should take care to call setTaskProgress themselves
 * instead of simply forwarding the progress comments, so that the new stream
 * is not burdened by excessive comments.  See setTaskSize().
 * 
 * @memberof DasIO
 */
DAS_API das_errcode DasIO_setProgress( DasIO* pThis, int progress );

/** Close a stream
 *
 * Flushes and closes the output, or stops reading the input.  Closing twice
 * is harmless, and DasIO_exception() closes the output itself.  Writing
 * after a close is an error.
 *
 * @param pThis
 * @memberof DasIO
 */
DAS_API void DasIO_close(DasIO* pThis);

#ifdef __cplusplus
}
#endif

#endif /* _das_io_h_ */
