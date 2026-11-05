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

/** @file proc.h Callback processing for das2 stream reads and writes
 *
 * These structure allow one to hook in Das2 Stream object processing that
 * is triggered when Headers and Data are read or written to a file.
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
 * @param sd A pointer to the parsed DasStream
 * @param ud A pointer to a user data structure, may be NULL.
 * @see StreamHandler
 */
typedef DasErrCode (*DasStreamHandler)(DasStream* sd, void* ud);

/** Definition of the callback function invoked when a packet header is 
 * encountered in the input.
 * @param sd A pointer to the parsed Stream Descriptor
 * @param pd A pointer to the parsed Packet Descriptor
 * @param ud A pointer to a user data structure, may be NULL.
 * @see StreamHandler
 */
typedef DasErrCode (*PktDescHandler)(DasStream* sd, DasPkt* pd, void* ud);

/** Definition of the callback function invoked when a packet header is
 * going to be deleted.  This only occurs if streams re-define packet IDs
 *
 * @param sd A pointer to the parsed Stream Descriptor
 * @param pd The descriptor about to be deleted.  das2 streams keep a DasPkt in
 *        the ID slot, das3 streams a DasDs, so check DasDesc_type() before
 *        casting.  Handlers that only want the signal can ignore it entirely.
 * @param ud A pointer to a user data structure, may be NULL.
 *
 * @returns DAS_OKAY to let the redefinition proceed.  Any other value refuses
 *        it: the old descriptor is left intact and the read stops with that
 *        code.  Refuse if you were accumulating data that can't survive the
 *        descriptor going away.
 *
 * @see StreamHandler
 */
typedef DasErrCode (*PktRedefHandler)(DasStream* sd, DasDesc* pd, void* ud);
		
/** Callback function invoked when a data packet is encountered in the input.
 *
 * @note Decoding happens on demand.  If no processor supplies a packet data
 *   handler, DasIO reads past the data bytes without parsing them, so a
 *   program only pays for values it asked for.  To validate packet contents
 *   without otherwise using them, register a handler that does nothing.
 *
 * @param pd A pointer to the parsed Packet Descriptor
 * @param ud A pointer to a user data structure, may be NULL.
 * @see StreamHandler
 */
typedef DasErrCode (*PktDataHandler)(DasPkt* pd, void* ud);


/** Callback function invoked when a dataset header is encountered
 * on the input stream.
 * 
 * @param sd A pointer to the parsed Stream Descriptor
 * @param pi The packet ID associated with this dataset
 * @param dd A pointer to a parsed DasDs (dataset) definition
 * @param ud A pointer to a user data structure, may be NULL
 */
typedef DasErrCode (*DsDescHandler)(DasStream* sd, int pi, DasDs* dd, void* ud);

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
 * @param sd A pointer to the parsed Stream Descriptor
 * @param pi The packet ID associated with this dataset
 * @param dd A pointer to a parsed DasDs (dataset) definition
 * @param ud A pointer to a user data structure, may be NULL
 */
typedef DasErrCode (*DsDataHandler)(DasStream* sd, int pi, DasDs* dd, void* ud);

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
 * @param sd A pointer to the parsed Stream Descriptor
 * @param pi The packet ID the bytes belong to
 * @param dd The descriptor in that ID slot, a DasPkt for das2 model reads or
 *           a DasDs for das3, so check DasDesc_type() before casting.  It is
 *           what DasIO used to find the payload length in a das2 stream.
 * @param pb The payload bytes
 * @param ud A pointer to a user data structure, may be NULL
 * @see StreamHandler
 */
typedef DasErrCode (*RawDataHandler)(
	DasStream* sd, int pi, DasDesc* dd, const DasBuf* pb, void* ud
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
 * @param sd The stream that was read.  NULL only when the stream ended on
 *        an exception before its header arrived.
 * @param pExcept The exception that ended the stream, or NULL.  It belongs
 *        to the DasIO and is good only until the callback returns.
 * @param ud A pointer to a user data structure, may be NULL.
 * @see StreamHandler
 */
typedef DasErrCode (*CloseHandler)(
	DasStream* sd, const DasExcept* pExcept, void* ud
);

/** Callback functions that handle comments
 *
 * The comment belongs to the DasIO, which reuses it for the next one, so it
 * and every value read from it are good only until the callback returns.
 *
 * @param pCmt The parsed comment
 * @param ud A pointer to a user data structure, may be NULL.
 * @see StreamHandler
 */
typedef DasErrCode (*CommentHandler)(const DasComment* pCmt, void* ud);


/** A set of callbacks used for input and output stream processing 
 *
 * However a stream ends, normally or on an exception, each handler's
 * closeHandler is its last call, see CloseHandler.
 *
 * @interface StreamHandler
 * @ingroup streams
 */
typedef struct _streamHandler {
	/** The function to be called when the stream header is read in.
	 * This is the header with the element \<stream\>\</stream\> in the input
	 * file.  
	 */
	DasStreamHandler streamDescHandler;
	 
	/** Sets the function to be called when each \<packet\\> element
	 * is read in.
	 */
	PktDescHandler pktDescHandler;
	
	/** Sets the function to be called when a packet ID is about to be 
	 * re-defined before the old pkt descriptor object is deleted */
	PktRedefHandler pktRedefHandler;
	 
	/** Sets the function to be called when each data packet is read in. */
	PktDataHandler pktDataHandler;

   /** Sets the function to be called when each dataset definition is read
    * in (das3) */
   DsDescHandler dsDescHandler;

   /** Sets the function to be called when each dataset receives new data
    * (das3) */
   DsDataHandler dsDataHandler;

   /** Sets the function to be called with the raw bytes of each data packet,
    * before any decoding.  Works for das2 and das3 input alike. */
   RawDataHandler rawDataHandler;

	 
	/** StreamCommentHandler receives stream annotations.
	 * 
	 * These include progress messages, log files, informational messages, and
	 * other messages humans might be interested in.  The default handler throws
	 * out the comment. 
	 *
	 * A filter passing on "taskProgress" comments may want to send them
	 * with DasIO_setTaskProgress() instead, which rate limits them so they
	 * don't swamp the output stream.
	 */
	CommentHandler commentHandler;
	
	/** Sets the function to be called when the stream ends, normally or on
	 * an exception.  The default handler does nothing at a normal end or on
	 * "no data", and prints any other exception and returns an error, so an
	 * unhandled exception makes the read fail.
	 */
	CloseHandler closeHandler;
	
	/** An optional User-data pointer that is passed along to all callbacks.
	 * This value may be NULL.
	 */
	void* userData;
	 
} StreamHandler;

/** Initialize a stream processor with default callbacks.
 *
 * The library has builtin callbacks for the closeHandler and the
 * commentHandler.  Calling this function will initialize a StreamHandler
 * structure with the defaults and set all other callback pointers to NULL.
 * 
 * @param pThis The stream handler structure to initialize
 * @param pUserData A pointer that will be passed in to each callback, used
 *        to provide access to whatever state data you may need to reference.
 * @memberof StreamHandler
 */
DAS_API void StreamHandler_init(StreamHandler* pThis, void* pUserData);

/** Create a new stream processor with default callbacks.
 *
 * The library has builtin callbacks for the closeHandler and the
 * commentHandler.  Calling this function will initialize a StreamHandler
 * structure with the defaults and set all other callback pointers to NULL.
 *
 * @param pUserData The user data pointer will be set to this value.
 * @return A new StreamHandler allocated on the heap
 * 
 * @memberof StreamHandler
 */
DAS_API StreamHandler* new_StreamHandler(void* pUserData);

/** Type safe wrapper around free() */
DAS_API void del_StreamHandler(StreamHandler* pThis);

#ifdef __cplusplus
}
#endif

#endif /* _das_proc_h_ */
