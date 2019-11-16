/** @file das2io.h Reading and writing Das2 Stream objects to standard I/O.
 */

#ifndef _das2_io_h_
#define _das2_io_h_

#include <stdio.h>
#include <zlib.h>
#include <das2/stream.h>
#include <das2/oob.h>
#include <das2/processor.h>

/* used internally for implementing stream inflate/deflate, etc/ */
#define STREAM_MODE_STRING 0

/* stream is going directly to socket (not used) */
#define STREAM_MODE_SOCKET 1

/* stream is going to file */
#define STREAM_MODE_FILE   2

/* max number of stream processor objects */
#define DAS2_MAX_PROCESSORS 10

#define DASIO_NAME_SZ 128

/** Tracks input and output operations for das2 stream headers and data.
 * 
 * Members of this class handle overall stream operations reading writing 
 * packets, checking packet lengths, passing XML string data off to descriptor
 * object constructors, triggering processing callbacks and most other general
 * Das2 Stream IO tasks.
 */
typedef struct das_io_struct {
	char     rw;         /* w' for write, 'r' for read */
	bool     compressed; /* 1 if stream is compressed or should be compressed */
	
	int      mode;       /* STREAM_MODE_STRING or STREAM_MODE_FILE */
	char     sName[DASIO_NAME_SZ]; /* A human readable name for data source or sink */
	
	int      offset;     /* current offset for file reads */
	
	/* File I/O */
	FILE     *file;      /* input/output file  (File I/O) */
	
	/* Buffer IO */
	char     *sBuffer;   /* buffer for string input/output */
	int      nLength;    /* length of buffer pointed to by sbuffer */
	
	/* Compressed I/O */
	z_stream *zstrm;     /* z_stream for inflate/deflate operations */
	Byte     *inbuf;     /* input buffer */
	Byte     *outbuf;    /* output buffer */
	int      zerr;       /* error code for last stream operation */
	int      eof;        /* set if end of input file */
	
	/* data object processor's with callbacks  (Input / Output) */
	StreamHandler* pProcs[DAS2_MAX_PROCESSORS+1];
	bool bSentHeader;
	
	/* Sub Object Writing (output) */
	DasBuf* pDb;        /* Sub-Object serializing buffer */
	
	int logLevel;       /* to-stream logging level. (output) */
	
	int taskSize;       /* progress indicator max value (output) */
	long tmLastProgMsg; /* Time the last progress message was emitted (output)*/
	
	OobComment cmt;     /* Hold buffers for comments and logs */
} DasIO;

/** Create a new DasIO object from a standard C FILE.
 * 
 * Create a DasIO object that reads or writes to C standard IO files.  The 
 * C file object's read or write state should be compatible with the DasIO
 * state.  For example:
 * @code
 *  DasIO* dio = new_DasIO(stdin, "wc");
 * @endcode
 * is @b invalid, but the library has no way to tell until operations start.
 *
 * @param sProg A spot to store the name of the program creating the file
 *        this is useful for automatically generated error and log messages
 *
 * @param file a C standard IO file object.
 *        
 * @param mode A string containing the mode, one of:
 *        - 'r' read
 *        - 'w' write uncompressed
 *        - 'wc' write compressed 
 * 
 * @memberof DasIO
 */
DasIO* new_DasIO_cfile(const char* sProg, FILE* file, const char* mode);

/** Create a new DasIO object from a disk file.
 *
 * @param sProg A spot to store the name of the program creating the file
 *        this is useful for automatically generated error and log messages
 *
 * @param sFile the name of a file on disk.  If the file doesn't exist it
 *        is created.  
 *        
 * @param mode A string containing the mode, one of:
 *        - 'r' read (reads compressed and uncompressed files)
 *        - 'w' write uncompressed
 *        - 'wc' write compressed 
 * 
 * @memberof DasIO
 */
DasIO* new_DasIO_file(const char* sProg, const char* sFile, const char* mode);

DasIO* new_DasIO_str(const char* sProg, char* sbuf, size_t len, const char* mode);

/** Free resources associated with a DasIO structure
 * Typically you don't need to do this as heap memory is free'ed when the
 * program exits anyway.  But if you need a destructor, here it is.
 * 
 * @param pThis The DasIO abject to delete.  Any associated file handles except
 *        for standard input and standard output are closed.  The pointer 
 *        given to this function should be set to NULL after the function 
 *        completes.
 */
void del_DasIO(DasIO* pThis);


/** Add a packet processor to be invoked during I/O operations
 * 
 * A DasIO object may have 0 - DAS2_MAX_PROCESSORS packet processors attached
 * to it.  Each processor contains one or more callback functions that will
 * be invoked and provided with ::Descriptor objects.  
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
int DasIO_addProcessor(DasIO* pThis, StreamHandler* pProc);


/** Starts the processing of the stream read from FILE* infile.  
 *
 * This function does not return until all input has been read or an exception
 * condition has occurred.  If a ::StreamHandler has been set with
 * DasIO_setProcessor() then as each header packet and each data packet is read
 * callbacks specified in the stream handler will be invoked.  Otherwise all 
 * data is merely parsed and then discarded.
 * 
 * @returns a status code.  0 indicates that all processing ended with no 
 *          errors, otherwise a non-zero value is returned.
 * 
 * @memberof DasIO
 */
int DasIO_readAll(DasIO* pThis);


/** Writes the data describing the stream to the output channel (e.g. File* ).
 *
 * This serializes the descriptor structure into XML and writes it out. 
 * @param pThis The IO object, must be set up in a write mode
 * @param pSd The stream descriptor to serialize
 * @returns 0 on success a positive integer error code other wise.
 * @memberof DasIO
 */
ErrorCode DasIO_writeStreamDesc(DasIO* pThis, StreamDesc* pSd);

/** Writes the data describing a packet type to the output channel (e.g. File* ).
 *
 * This serializes the descriptor structure into XML and writes it out.  The 
 * packet type will be assigned a number on the das2Stream, and data packets 
 * will be tagged with this number.
 * @memberof DasIO
 */
ErrorCode DasIO_writePktDesc(DasIO* pThis, PktDesc* pd);

/** Sends the data packet on to the stream after checking validity. 
 *
 * This check insures that all planes have been set via setDataPacket.
 * 
 * @param pThis the IO object to use for sending the data
 * @param pPd a Packet Descriptor loaded with values for output.
 * 
 * @returns 0 if the operation was successful or an positive value on an error.
 * @memberof DasIO
 */
ErrorCode DasIO_writePktData(DasIO* pThis, PktDesc* pPd);

/** Output an exception structure
 *
 * @memberof DasIO
 */
ErrorCode DasIO_writeException(DasIO* pThis, OobExcept* pSe);

/** Output a StreamComment
 * Stream comments are generally messages interpreted only by humans and may
 * change without affecting processes.  Progress messages are sent via stream
 * comments.
 * @memberof DasIO
 */
ErrorCode DasIO_writeComment(DasIO* pThis, OobComment* pSc);

#define LOGLVL_FINEST 0
#define LOGLVL_FINER 300
#define LOGLVL_FINE 400
#define LOGLVL_CONFIG 500
#define LOGLVL_INFO 600
#define LOGLVL_WARNING 700
#define LOGLVL_ERROR 800

/** Send a log message onto the stream at the given log level.  
 * Note that messages sent at a level greater than INFO may be thrown out if
 * performance would be degraded.
 * 
 * @memberof DasIO
 */
ErrorCode DasIO_sendLog(DasIO* pThis, int level, char * msg, ... );

/** Set the minimum log level that will be transmitted on the stream.  
 * The point of the DasIO logging functions is not to handle debugging, but 
 * to inform a client program what is going on by embedding messages in an 
 * output stream.  Since the point of a stream is to transmit data, placing
 * too many comments in the stream will degrade performance.
 * 
 * Unless this function is called, the default logging level is 
 * @b LOGLEVEL_WARNING
 * 
 * @param pThis the DasIO object who's log level will be altered.
 * @param minLevel A logging level, one of:
 *     - LOGLVL_ERROR - Only output messages that indicate improper operation.
 *     - LOGLVL_WARNING - Messages that indicate a problem but processing can
 *          continue
 *     - LOGLVL_INFO - Extra stuff the operator may want to know
 *     - LOGLVL_CONFIG - Include basic setup information as well
 * 
 * @memberof DasIO
 */
void DasIO_setLogLvl(DasIO* pThis, int minLevel);

/** Get logging verbosity level
 * @param pThis a DasIO object to query
 * @returns A logging level as defined in DasIO_setLogLevel()
 * @memberof DasIO
 */
int DasIO_getLogLvl(const DasIO* pThis);

/** Returns a string identifying the log level.
 * 
 * @param logLevel a log level value as defined in DasIO_setLogLvl()
 * @returns a string such as 'error', 'warning', etc.
 * 
 */
const char* LogLvl_string(int logLevel);


/* Identifies the size of task for producing a stream, in arbitrary units. 
 * 
 * This number is used to generate a relative progress position, and also as
 * a weight if several Das2 Streams (implicitly of similar type) are combined.
 * See setTaskProgress().
 * 
 * @memberof DasIO
 */
ErrorCode DasIO_setTaskSize(DasIO* pThis, int size);

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
ErrorCode DasIO_setTaskProgress( DasIO* pThis, int progress );

/** Set the logging level */

/** Close and free an output stream with an exception.
 * 
 * Call this to notify stream consumers that there was an exceptional condition
 * that prevents completion of the stream.  For example, this might be used to
 * indicate that no input files were available.  This is a convenience function
 * for the calls:
 *
 *   - DasIO_writeStreamDesc()
 *   - DasIO_writeException()
 *   - DasIO_close()
 *   - del_DasIO()
 *
 * @param pThis the output object to receive the exception packet
 * @param pSd The stream descriptor.  All Das2 Streams have to start with a 
 *        stream header.  If this header hasn't been output then this object
 *        will be serialized to create a stream header.
 * @param type the type of exception may be one of the pre-defined strings:
 *   - DAS2_EXCEPT_NO_DATA_IN_INTERVAL
 *   - DAS2_SERVER_ERROR
 *   or some other short type string of your choice.
 * @param msg a longer message explaining what went wrong.
 * 
 * @memberof DasIO
 */
void DasIO_throwException(
	DasIO* pThis, StreamDesc* pSd, const char* type, char* msg
);

/** Throw a server exception and close the stream 
 *
 * If no stream descriptor has been sent then a stub descriptor is 
 * output first.  The output is encoded for XML transport so there
 * is no need to escape the text prior to sending.  The total mesage
 * may not exceed 2047 UTF-8 bytes.
 *
 * If this function is called on an input stream the program will
 * immediately exit with the value 10.
 *
 * @returns The value 11
 */
int DasIO_serverExcept(DasIO* pThis, const char* fmt, ...);

/** Throw a bad query exception and close the stream 
 *
 * If no stream descriptor has been sent then a stub descriptor is 
 * output first.  The output is encoded for XML transport so there
 * is no need to escape the text prior to sending.  The total mesage
 * may not exceed 2047 UTF-8 bytes.
 *
 * Not having any data for the requested parameter range is not
 * cause for throwing an exception.  Send a no data in interval
 * response instead.
 *
 * If this function is called on an input stream the program will
 * immediately exit with the value 10.
 *
 * @returns The value 11
 */
int DasIO_queryExcept(DasIO* pThis, const char* fmt, ...);


/** Send a "no data in interval" message and close the stream
 *
 * If no stream descriptor has been sent then a stub descriptor is 
 * output first.  The output is encoded for XML transport so there
 * is no need to escape the text prior to sending.  The total mesage
 * may not exceed 2047 UTF-8 bytes.
 *
 * Not having any data for the requested parameter range is not
 * cause for throwing an exception.  Send a no data in interval
 * response instead.
 *
 * If this function is called on an input stream the program will
 * immediately exit with the value 10.
 *
 * @returns The value 0
 */
int DasIO_closeNoData(DasIO* pThis, const char* fmt, ...);



/** Analog of fclose
 * Closes the output file descriptor, flushes a gzip buffer, etc.
 * May possibly send a stream termination comment as well.
 * 
 * @param pThis
 * @memberof DasIO
 */
void DasIO_close(DasIO* pThis);

/** Print a string with a format specifier (Low-level API)
 * This works similar to the C printf function.
 * 
 * @param pThis
 * @param format
 * @param ... The variables to print
 * @returns The number of characters printed.
 * @memberof DasIO
 */
int DasIO_printf(DasIO* pThis, const char* format, ...);

/** Anolog of fwrite (Low-level API)
 * @memberof DasIO
 */
int DasIO_write(DasIO* pThis, const char* data, int length);

/** Analog of fread (Low-level API)
 * @memberof DasIO
 */
int DasIO_read(DasIO* pThis, DasBuf* pBuf, size_t nBytes);

/** Analog of getc (Low-level API)
 * 
 * @memberof DasIO
 */
int DasIO_getc(DasIO* pThis);



#endif /* _das2_io_h_ */
