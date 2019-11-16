/** @file log.h Simple message logging 
 *
 * Generic thread safe logging.
 * By default messages are simply printed to standard error, use
 * das_log_sethandler() to send messages some where else.  All log messages 
 * are sent via das_log(), however the following convience macros make for
 * less typing:
 * 
 *  - das_trace(): Log a DAS_LL_TRACE level message.
 *  - das_trace_v(): Log a DAS_LL_TRACE level message using fprintf style varargs.
 *  - das_debug(): Log a DAS_LL_DEBUG level message.
 *  - das_debug_v(): Log a DAS_LL_DEBUG level message using fprintf style varargs.
 *  - das_notice(): Log a DAS_LL_NOTICE level message.
 *  - das_notice_v(): Log a DAS_LL_NOTICE level message using fprintf style varargs.
 *  - das_warn(): Log a DAS_LL_WARN level message.
 *  - das_warn_v(): Log a DAS_LL_WARN level message using fprintf style varargs.
 *  - das_error(): Log a DAS_LL_ERROR level message.
 *  - das_error_v(): Log a DAS_LL_ERROR level message using fprintf style varargs.
 *  - das_critical(): Log a DAS_LL_CRITICAL level message, these should be reserved for program exit conditions.
 *  - das_critical_v(): Log a DAS_LL_CRITICAL level message using fprintf style varargs, these should be reserved for program exit conditions.
 *
 * For example a log line such as:
 * @code
 *   das_warn_v("File %s, Pkt %05d: Header Block > 256 bytes", sFile, nIdx);
 * @endcode
 * is equivalent to:
 * @code
 *   das_log(DAS_LL_WARN, __FILE__, __LINE__, 
 *           "File %s, Pkt %05d: Header Block > 256 bytes", sFile, nIdx);
 * @endcode
 * but shorter.
 */

/* Ported over from librpwgse which was laborously developed for Juno Waves
 * support.  Since logging is much different then just failing with an
 * error, this is a different falcility than the das2_error_func from util.h
 * but the two items have common functionality that should be merged over time.
 * -cwp 2016-10-20 
 *
 * In general we use the das_ prefix instead of the das2_ prefix.  The
 * das2_ prefix should probably be phased out.
 */
 
#ifndef _das2_log_h_
#define _das2_log_h_

#define DAS_LL_NOTHING 255
#define DAS_LL_CRIT   100  /* same as java.util.logging.Level.SEVERE */
#define DAS_LL_ERROR   80  
#define DAS_LL_WARN    60  /* same as java.util.logging.Level.WARNING */
#define DAS_LL_INFO    40  /* same as java.util.logging.Level.INFO & CONFIG */
#define DAS_LL_DEBUG   20  /* same as java.util.logging.Level.FINE */
#define DAS_LL_TRACE    0  /* same as java.util.logging.Level.FINER & FINEST */

/** Get the log level.
 *
 * @returns one of: DAS_LL_CRIT, DAS_LL_ERROR, DAS_LL_WARN, DAS_LL_INFO,
 *                  DAS_LL_DEBUG, DAS_LL_TRACE 
 */
int das_log_level(void);

/** Set the logging level for this thread.
 *
 * @param nLevel Set to one of
 *   - DAS_LL_TRACE
 *   - DAS_LL_DEBUG
 *   - DAS_LL_INFO
 *   - DAS_LL_WARN
 *   - DAS_LL_ERROR
 *   - DAS_LL_CRITICAL
 *
 * @return The previous log level.
 */
int das_log_setlevel(int nLevel);

/** Output source file and line numbers for messages at or above this level */
bool das_log_set_showline(int nLevel);


/* Basic logging function, macros use this */
void das_log(int nLevel, const char* sSrcFile, int nLine, const char* sFmt, ...);

		
/** Macro wrapper around das_log() for TRACE messages with out variable args */
#define das_trace(M) das_log(DAS_LL_TRACE, __FILE__, __LINE__, M)
/** Macro wrapper around das_log() for DEBUG messages with out variable args */
#define das_debug(M) das_log(DAS_LL_DEBUG, __FILE__, __LINE__, M)
/** Macro wrapper around das_log() for info messages with out variable args */
#define das_notice(M) das_log(DAS_LL_INFO, __FILE__, __LINE__, M)
/** Alias for das_notice */
#define das_info(M) das_log(DAS_LL_INFO, __FILE__, __LINE__, M) 
/** Macro wrapper around das_log() for WARNING messages with out variable args */
#define das_warn(M) das_log(DAS_LL_WARN, __FILE__, __LINE__, M) 
/** Macro wrapper around das_log() for ERROR messages with out variable args */
#define das_error(M) das_log(DAS_LL_ERROR, __FILE__, __LINE__, M) 
/** Macro wrapper around das_log() for CRITICAL messages with out variable args */
#define das_critical(M) das_log(DAS_LL_CRITICAL, __FILE__, __LINE__, M)


/** Macro wrapper around das_log() for TRACE messages with variable arguments */
#define das_trace_v(F, ...)\
  das_log(DAS_LL_TRACE, __FILE__, __LINE__, F, __VA_ARGS__)
/** Macro wrapper around das_log() for DEBUG messages with variable arguments */
#define das_debug_v(F, ...)\
  das_log(DAS_LL_DEBUG, __FILE__, __LINE__, F, __VA_ARGS__)
/** Macro wrapper around das_log() for NOTICE messages with variable arguments */
#define das_notice_v(F, ...)\
  das_log(DAS_LL_INFO, __FILE__, __LINE__, F, __VA_ARGS__)
/** Alias for das_notice_v */
#define das_info_v(F, ...)\
  das_log(DAS_LL_INFO, __FILE__, __LINE__, F, __VA_ARGS__) 
/** Macro wrapper around das_log() for WARNING messages with variable arguments */
#define das_warn_v(F, ...)\
  das_log(DAS_LL_WARN, __FILE__, __LINE__, F, __VA_ARGS__) 
/** Macro wrapper around das_log() for ERROR messages with variable arguments */
#define das_error_v(F, ...)\
  das_log(DAS_LL_ERROR, __FILE__, __LINE__, F, __VA_ARGS__) 
/** Macro wrapper around das_log() for CRITICAL messages with variable arguments */
#define das_critical_v(F, ...)\
  das_log(DAS_LL_CRIT, __FILE__, __LINE__, F, __VA_ARGS__)

/** Definition of a message handler function pointer.
 * Message handlers need to be prepared for any of the string pointers 
 * sMsg, sDataStatus, or sStackTrace to be null.
 *
 * @param nLevel The message level.  If nLevel is equal to or greater than
 *  das_log_getlevel() then the message should be logged.
 * 
 * @param sMsg The message, usually not null.
 * 
 * @param bPrnTime The current system time should be included in the log
 *        output.
 */ 
typedef void (*das_log_handler_t)(int nLevel, const char* sMsg, bool bPrnTime);

/** Install a new message handler function for this thread.
 * The default message handler just prints to stderr, which is not very 
 * effecient, nor is it appropriate for GUI applications.
 * 
 * @param new_handler The new message handler, or NULL to set to the default
 *        handler.
 * @return The previous message handler function pointer
 */
das_log_handler_t das_log_sethandler(das_log_handler_t new_handler);

#endif /* _das2_log_h_ */
