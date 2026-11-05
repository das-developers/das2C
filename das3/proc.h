/* Copyright (C) 2015-2017 Chris Piker <chris-piker@uiowa.edu>
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
 * version 2.1 along with das2C; if not, see <http://www.gnu.org/licenses/>.
 */

/** @file proc.h Callback processing for das stream reads and writes
 *
 * These structure allow one to hook in das stream object processing that
 * is triggered when headers and data are read or written to a file.
 */

#ifndef _das_proc_h_
#define _das_proc_h_

#include <das3/stream.h>
#include <das3/oob.h>

#ifdef __cplusplus
extern "C" {
#endif


/** Definition of the callback function invoked when a stream header is
 * encountered in the input.
 * @param pSd A pointer to the parsed DasStream
 * @param pUser The das_stream_proc::pUser pointer, may be NULL.
 * @see das_stream_proc
 */
typedef DasErrCode (*das_stream_desc_f)(DasStream* pSd, void* pUser);

/** Definition of the callback function invoked when a packet header is
 * encountered in the input.
 * @param pSd A pointer to the parsed Stream Descriptor
 * @param pPd A pointer to the parsed Packet Descriptor
 * @param pUser The das_stream_proc::pUser pointer, may be NULL.
 * @see das_stream_proc
 */
typedef DasErrCode (*das_pkt_desc_f)(DasStream* pSd, DasPkt* pPd, void* pUser);

/** Definition of the callback function invoked when a packet header is
 * going to be deleted.  This only occurs if streams re-define packet IDs
 *
 * @param pSd A pointer to the parsed Stream Descriptor
 * @param pPd The descriptor about to be deleted.  das2 streams keep a DasPkt
 *        in the ID slot, das3 streams a DasDs, so check DasDesc_type() before
 *        casting.  Handlers that only want the signal can ignore it entirely.
 * @param pUser The das_stream_proc::pUser pointer, may be NULL.
 *
 * @returns DAS_OKAY to let the redefinition proceed.  Any other value refuses
 *        it: the old descriptor is left intact and the read stops with that
 *        code.  Refuse if you were accumulating data that can't survive the
 *        descriptor going away.
 *
 * @see das_stream_proc
 */
typedef DasErrCode (*das_pkt_redef_f)(DasStream* pSd, DasDesc* pPd, void* pUser);

/** Callback function invoked when a data packet is encountered in the input.
 *
 * @note Decoding happens on demand.  If no processor supplies a packet data
 *   handler, DasIO reads past the data bytes without parsing them, so a
 *   program only pays for values it asked for.  To validate packet contents
 *   without otherwise using them, register a handler that does nothing.
 *
 * @param pPd A pointer to the parsed Packet Descriptor
 * @param pUser The das_stream_proc::pUser pointer, may be NULL.
 * @see das_stream_proc
 */
typedef DasErrCode (*das_pkt_data_f)(DasPkt* pPd, void* pUser);


/** Callback function invoked when a dataset header is encountered
 * on the input stream.
 *
 * The dataset carries its own packet ID, and DasDesc_parent() leads back to
 * the stream that owns it.
 *
 * @param pDs A pointer to a parsed DasDs (dataset) definition
 * @param pUser The das_stream_proc::pUser pointer, may be NULL
 * @see das_stream_proc
 */
typedef DasErrCode (*das_ds_desc_f)(DasDs* pDs, void* pUser);

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
 * @param pDs A pointer to a parsed DasDs (dataset) definition
 * @param pUser The das_stream_proc::pUser pointer, may be NULL
 * @see das_stream_proc
 */
typedef DasErrCode (*das_ds_data_f)(DasDs* pDs, void* pUser);

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
 * @param pDesc The descriptor in the payload's packet ID slot, a DasPkt for
 *        das2 model reads or a DasDs for das3, so check DasDesc_type() before
 *        casting.  Both carry their packet ID.  It is what DasIO used to find
 *        the payload length in a das2 stream.
 * @param pBuf The payload bytes
 * @param pUser The das_stream_proc::pUser pointer, may be NULL
 * @see das_stream_proc
 */
typedef DasErrCode (*das_raw_data_f)(
	DasDesc* pDesc, const DasBuf* pBuf, void* pUser
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
 *   DasErrCode onClose(DasStream* pSd, const DasExcept* pExcept, void* pUser)
 *   {
 *       flushBins(pUser);
 *       if(pExcept == NULL) return DAS_OKAY;
 *       DasIO_exception(pOut, pExcept);
 *       return (DasExcept_type(pExcept) == DAS_EX_NO_DATA) ? DAS_OKAY : MY_ERR;
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
 * @param pUser The das_stream_proc::pUser pointer, may be NULL.
 * @see das_stream_proc
 */
typedef DasErrCode (*das_close_f)(
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
 * @param pUser The das_stream_proc::pUser pointer, may be NULL.
 * @see das_stream_proc
 */
typedef DasErrCode (*das_comment_f)(
	DasStream* pSd, const DasComment* pCmt, void* pUser
);


/** The first das_stream_proc layout, das 3.0.
 *
 * Structure versions hold the library major version in the high byte and
 * the minor in the low byte.  A version only changes when the structure or
 * one of its callback signatures does, so it can lag the library version.
 */
#define DAS_STREAM_PROC_V3_0 ((3 << 8) | 0)

/** The das_stream_proc layout described by this header */
#define DAS_STREAM_PROC_VER DAS_STREAM_PROC_V3_0

/** A set of callbacks used for input and output stream processing
 *
 * Start from das_stream_proc_init() and set the callbacks you need.  A
 * callback left NULL means the processor has no interest in that event.  For
 * most events nothing happens, see onClose for the one that has a library
 * default.
 *
 * @code
 *   das_stream_proc proc;
 *   das_stream_proc_init(&proc, &myState);
 *   proc.onDsData = myDataHandler;
 *   DasIO_addProcessor(pIn, &proc);
 * @endcode
 *
 * However a stream ends, normally or on an exception, each processor's
 * onClose is its last call, see das_close_f.
 *
 * The input stream belongs to the DasIO.  Processors read it, and may hang
 * their own data on descriptors with DasDesc_setUser(), but don't add,
 * remove or replace its descriptors.  Programs that transform a stream build
 * a separate output stream instead.
 *
 * @ingroup streams
 */
typedef struct das_stream_proc_t {
	/** The layout this program was compiled against, set by
	 * das_stream_proc_init(), leave it alone.
	 *
	 * Callback signatures can change between versions without changing the
	 * structure size, so the library needs the version and not just a size to
	 * call them correctly.  A zero or unknown version is refused.
	 */
	size_t uVersion;

	/** The function to be called when the stream header is read in.
	 * This is the header with the element \<stream\>\</stream\> in the input
	 * file.
	 */
	das_stream_desc_f onStreamDesc;

	/** Sets the function to be called when each \<packet\\> element
	 * is read in.
	 */
	das_pkt_desc_f onPktDesc;

	/** Sets the function to be called when a packet ID is about to be
	 * re-defined before the old pkt descriptor object is deleted */
	das_pkt_redef_f onPktRedef;

	/** Sets the function to be called when each data packet is read in. */
	das_pkt_data_f onPktData;

	/** Sets the function to be called when each dataset definition is read
	 * in (das3) */
	das_ds_desc_f onDsDesc;

	/** Sets the function to be called when each dataset receives new data
	 * (das3) */
	das_ds_data_f onDsData;

	/** Sets the function to be called with the raw bytes of each data packet,
	 * before any decoding.  Works for das2 and das3 input alike. */
	das_raw_data_f onRawData;

	/** Receives stream annotations.
	 *
	 * These include progress messages, log files, informational messages, and
	 * other messages humans might be interested in.  If NULL comments are
	 * discarded.
	 *
	 * A filter passing on "taskProgress" comments may want to send them
	 * with DasIO_setTaskProgress() instead, which rate limits them so they
	 * don't swamp the output stream.
	 */
	das_comment_f onComment;

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
	das_close_f onClose;

	/** An optional User-data pointer that is passed along to all callbacks.
	 * This value may be NULL.
	 */
	void* pUser;

} das_stream_proc;

/** Initialize a stream processor for the layout given
 *
 * Use das_stream_proc_init() instead, which supplies the version this
 * program was compiled against.
 *
 * @param pThis The stream processor structure to initialize
 * @param pUser The value for das_stream_proc::pUser, may be NULL
 * @param uVersion The structure layout pThis has, one of the
 *        DAS_STREAM_PROC_V* values.
 * @returns DAS_OKAY, or an error if uVersion is unknown to this library,
 *        in which case pThis is not touched.
 */
DAS_API DasErrCode das_stream_proc_init_ver(
	das_stream_proc* pThis, void* pUser, size_t uVersion
);

/** Initialize a stream processor, all callbacks NULL
 *
 * This is a macro so that the version recorded is the one from the header
 * the calling program was compiled with, not the one the library was.  A
 * later library can then still recognize, and possibly serve, an older
 * program's structure.
 *
 * @param P A pointer to the das_stream_proc to initialize
 * @param U A pointer passed to every callback, or NULL
 * @returns DAS_OKAY, or an error if the library is older than the header
 */
#define das_stream_proc_init(P, U) \
	das_stream_proc_init_ver((P), (U), DAS_STREAM_PROC_VER)

#ifdef __cplusplus
}
#endif

#endif /* _das_proc_h_ */
