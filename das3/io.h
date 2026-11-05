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
/* #include <das3/stream.h> */
#include <das3/oob.h>
#include <das3/proc.h>
#include <das3/xport.h>

#ifdef __cplusplus
extern "C" {
#endif

/** @defgroup IO Input/Output
 * Classes and functions reading and writing byte streams
 */

/** @addtogroup IO
 * @{
 */

/** @class DasIO io.h <das3/io.h>
 *
 * Tracks input and output operations for das2 stream headers and data.
 * 
 * Members of this class handle overall stream operations reading writing 
 * packets, checking packet lengths, passing XML string data off to descriptor
 * object constructors, triggering processing callbacks and most other general
 * Das2 Stream IO tasks.
 *
 * The structure is opaque.
 */
typedef struct das_io_struct DasIO;

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
 *   DasHttpResp* pRes = new_DasHttpResp_get(sUrl, PROG, pMgr, DASHTTP_TO_MIN);
 *   if(DasHttpResp_ok(pRes))
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
 * On a reader this selects what the callbacks receive: STREAM_MODEL_V2 keeps
 * das2 packets as DasPkt and fails on das3 headers, STREAM_MODEL_V3
 * up-converts das2 headers to DasDs, STREAM_MODEL_MIXED hands each header
 * over as whatever it was.  On a writer only V2 and V3 are legal since one
 * stream has one header dialect.
 * 
 * @returns DAS_OKAY if successful or an error code if not.
 * @memberof DasIO
 */
DAS_API DasErrCode DasIO_setModel(DasIO* pThis, das_stream_model nModel);

/** Get the in-memory data model, see DasIO_setModel()
 * @memberof DasIO
 */
DAS_API das_stream_model DasIO_getModel(const DasIO* pThis);

/** Set the stream version a writer emits.
 *
 * Writers default to the version given in the constructor mode string, or
 * STREAM_VER_2_2 when the mode string names none.  Only STREAM_VER_2_2 and
 * STREAM_VER_3_0 are writable, and the version can't change once the stream
 * header has gone out.
 *
 * @returns DAS_OKAY, or an error code on a reader, an unwritable version, or
 *          a writer whose header has already been sent.
 * @memberof DasIO
 */
DAS_API DasErrCode DasIO_setOutVer(DasIO* pThis, das_stream_ver nVer);

/** Get the stream version a writer will emit, see DasIO_setOutVer()
 *
 * On a reader this is STREAM_VER_UNK unless the constructor mode string
 * restricted the input to one version.
 * @memberof DasIO
 */
DAS_API das_stream_ver DasIO_getOutVer(const DasIO* pThis);

/** True once a writer has emitted the stream header.
 *
 * Comments need a header to follow.  DasIO_exception() sends a stub header
 * itself when none went out.
 * @memberof DasIO
 */
DAS_API bool DasIO_headerSent(const DasIO* pThis);

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
DAS_API void DasIO_embedAsBytes(DasIO* pThis, bool bEnable);

/** Keep dataset records in memory across packets
 *
 * By default a reader drops a dataset's record-varying values before each
 * new data packet is decoded, so memory use stays flat and a dsDataHandler
 * sees only the current packet's records.  With accumulation on, records
 * pile up for the life of the stream instead.  Turn it on to read a whole
 * stream into memory (see DasIO_takeStream()), or when a handler buffers
 * several packets before flushing, in which case the handler calls
 * DasDs_clearRagged0() itself after each flush.
 *
 * Accumulation also counts as a request for values: datasets are decoded
 * even when no processor has a dsDataHandler.
 *
 * A stream that redefines a dataset's packet ID is an error while
 * accumulating, since the old dataset and its records would have to be
 * dropped (das2C issue #33).
 *
 * Only affects das3 datasets (including das2 packets read with the
 * STREAM_MODEL_V3 model).  das2 packet descriptors only ever hold one packet.
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
DAS_API bool DasIO_getAccum(const DasIO* pThis);


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
 * attached StreamHandler callbacks before the call returns.  Bytes that
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
DAS_API DasErrCode DasIO_feed(DasIO* pThis, const ubyte* pData, size_t uLen);

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
DAS_API DasErrCode DasIO_feedEnd(DasIO* pThis);

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
 * @param pThis The DasIO object to receive the processor.
 * @param pProc The StreamProc object to add to the list of processors.
 * @return The number of packet processors attached to this DasIO object or
 *         a negative error value if there is a problem.
 * 
 * @memberof DasIO
 */
DAS_API int DasIO_addProcessor(DasIO* pThis, StreamHandler* pProc);

/** Starts the processing of the stream read from FILE* infile.  
 *
 * This function does not return until all input has been read, an
 * exception in the stream ended it, or an error occurred.  If a
 * ::StreamHandler has been set with DasIO_addProcessor() then as each header
 * packet read, callbacks specified in stream handlers will be 
 * invoked.  Otherwise all data is merely parsed and then discarded.
 * Reading stops at an exception or the end of the input.
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
 * DasIO_setModel(pIn, STREAM_MODEL_V3);  // das2 packets come up as datasets
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
 *          was already taken.  Also NULL, with a das_error, on an output
 *          object or while reading is still in progress.
 *
 * @memberof DasIO
 */
DAS_API DasStream* DasIO_takeStream(DasIO* pThis);

/** Read all of standard input into memory
 *
 * Reads with the STREAM_MODEL_V3 model and accumulation on, so das2
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
 * Reads with the STREAM_MODEL_V3 model and accumulation on, so das2
 * packets come back as datasets.
 * 
 * @param sProg The name of the program for log writing.
 *
 * @param sFile The name of a file to read
 * 
 * @returns The stream, owned by the caller, or NULL on a read error
 * @memberof DasStream
 */
DAS_API DasStream* new_DasStream_path(const char* sProg, const char* sFile);


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
DAS_API DasErrCode DasIO_writeStreamDesc(DasIO* pThis, DasStream* pSd);

/** Serialize a top-level descriptor to an destination
 * 
 * Handles DasStream, das2 DasPkt and das3 DasDs items.  Send the stream
 * first, then a header for each packet ID before any of its data.
 * 
 * @param pThis the IO object to use for sending the data
 * 
 * @param pDesc A top level in-band data descriptor such as DasPkt or DasDs.
 * 
 * @param iPktId The packet ID to tag the header with.  A DasDs does not
 *        know its own ID so it must be given here.  A DasPkt does, so
 *        pass 0 to use it; any other value must match.  Ignored for a
 *        DasStream.
 * 
 * @returns DAS_OKAY if the operation was successful or an positive value on an error.
 * 
 * @memberof DasIO
 */
DAS_API DasErrCode DasIO_writeDesc(DasIO* pThis, DasDesc* pDesc, int iPktId);

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
DAS_API DasErrCode DasIO_writeData(DasIO* pThis, DasDesc* pDesc, int iPktId);

/** Write one data packet whose payload is already encoded.
 *
 * Emits the packet tag in the stream's own version, ":NN:" for das2 and
 * "|Pd|N|len|" for das3, followed by the bytes unchanged.  For pass-through
 * tools that re-tag packets without decoding them.  The header for iPktId
 * must already have been sent; this function has no descriptor to check
 * that against, so the caller owns the promise.
 *
 * @param pThis the IO object to use for sending the data
 * @param iPktId the packet ID, 1 to 99 for das2 output
 * @param pData the encoded payload
 * @param uLen the payload length in bytes
 *
 * @returns DAS_OKAY, or an error code on a short write
 *
 * @memberof DasIO
 */
DAS_API DasErrCode DasIO_writePktBytes(
	DasIO* pThis, int iPktId, const ubyte* pData, size_t uLen
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
 * type DAS_EX_NO_DATA is used for this purpos.
 *
 * If no stream header has been sent yet, DasIO sends a stub header 
 * first.  DasIO then closes any input streams, but the object itself
 * is not deleted, so call del_DasIO() as usual.
 * 
 * Any write to a DasIO instance is an error after calling DasIO_exception().
 *
 * The exception is serialized before this function returns and no
 * reference to it is kept.  A filter may pass on a received exception in
 * its closeHandler as given.
 *
 * @param pThis An output stream
 * @param pExcept The exception to send
 * @returns DAS_OKAY, or a positive error code if it could not be written
 *
 * @memberof DasIO
 */
DAS_API DasErrCode DasIO_exception(DasIO* pThis, const DasExcept* pExcept);

/** End an output stream with an exception built from a printf style format
 *
 * The same as DasIO_exception() without making a DasExcept:
 * @code
 *   return DasIO_exception_v(pOut, DAS_EX_QUERY_ERR, "Unknown option %s", sOpt);
 * @endcode
 *
 * The message is built in space the DasIO keeps for the purpose.
 *
 * @param pThis An output stream
 * @param nType DAS_EX_NO_DATA, DAS_EX_SERVER_ERR or DAS_EX_QUERY_ERR
 * @param sFmt  A printf style format for the message.  To send text that
 *        may hold a '%', pass "%s" and the text.
 * @returns DAS_OKAY, or a positive error code if it could not be written
 *
 * @memberof DasIO
 */
DAS_API DasErrCode DasIO_exception_v(
	DasIO* pThis, das_except_e nType, const char* sFmt, ...
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
DAS_API DasErrCode DasIO_comment(DasIO* pThis, const DasComment* pCmt);

/** Output a comment built from a printf style format
 *
 * The quick way to tell whoever reads the stream what is going on, without
 * making a DasComment.  The source is the program name given when the DasIO
 * was made.
 *
 * @code
 *   DasIO_comment_v(pOut, "log:info", "Searching file system...");
 *   DasIO_comment_v(pOut, "log:warning", "Skipped %d bad records", nBad);
 * @endcode
 *
 * The comment is built in DasIO reserved memory so nothing is allocated
 * per call. As with any comment, the stream header must already
 * have been sent.
 *
 * @param pThis An output stream
 * @param sType Any token.  Readers know "log:" followed by a level name
 *        ("log:info", "log:warning", "log:error", ...), and the progress
 *        pair "taskSize" and "taskProgress" (see DasIO_setTaskProgress()).
 * @param sFmt  A printf style format for the comment value.  To send text
 *        that may hold a '%', pass "%s" and the text.
 * @returns DAS_OKAY, or a positive error code if the comment could not be
 *        written
 *
 * @memberof DasIO
 */
DAS_API DasErrCode DasIO_comment_v(
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
DAS_API DasErrCode DasIO_setTaskSize(DasIO* pThis, int size);

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
DAS_API DasErrCode DasIO_setTaskProgress( DasIO* pThis, int progress );

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
