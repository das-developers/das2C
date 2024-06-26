/* Copyright (C) 2015-2017 Chris Piker <chris-piker@uiowa.edu>
 *
 * This file is part of libdas2, the Core Das2 C Library.
 * 
 * Libdas2 is free software; you can redistribute it and/or modify it under
 * the terms of the GNU Lesser General Public License version 2.1 as published
 * by the Free Software Foundation.
 *
 * Libdas2 is distributed in the hope that it will be useful, but WITHOUT ANY
 * WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS
 * FOR A PARTICULAR PURPOSE.  See the GNU Lesser General Public License for
 * more details.
 *
 * You should have received a copy of the GNU Lesser General Public License
 * version 2.1 along with libdas2; if not, see <http://www.gnu.org/licenses/>. 
 */


/** @file log.h Simple message logging 
 *
 * Generic thread safe logging.
 * By default messages are simply printed to standard error, use
 * das_log_sethandler() to send messages some where else.  All log messages 
 * are sent via das_log(), however the following convience macros make for
 * less typing:
 * 
 *  - daslog_trace(): Log a DAS_LL_TRACE level message.
 *  - daslog_trace_v(): Log a DAS_LL_TRACE level message using fprintf style varargs.
 *  - daslog_debug(): Log a DAS_LL_DEBUG level message.
 *  - daslog_debug_v(): Log a DAS_LL_DEBUG level message using fprintf style varargs.
 *  - daslog_info(): Log a DAS_LL_NOTICE level message.
 *  - daslog_info_v(): Log a DAS_LL_NOTICE level message using fprintf style varargs.
 *  - daslog_warn(): Log a DAS_LL_WARN level message.
 *  - daslog_warn_v(): Log a DAS_LL_WARN level message using fprintf style varargs.
 *  - daslog_error(): Log a DAS_LL_ERROR level message.
 *  - daslog_error_v(): Log a DAS_LL_ERROR level message using fprintf style varargs.
 *  - daslog_critical(): Log a DAS_LL_CRITICAL level message, these should be reserved for program exit conditions.
 *  - daslog_critical_v(): Log a DAS_LL_CRITICAL level message using fprintf style varargs, these should be reserved for program exit conditions.
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
 * error, this is a different falcility than the das_error_func from util.h
 * but the two items have common functionality that should be merged over time.
 * -cwp 2016-10-20 
 */
 
#ifndef _das_log_h_
#define _das_log_h_

#include <das2/util.h>

#ifdef __cplusplus
extern "C" {
#endif
	
/** @addtogroup utilities
 * @{
 */

#define DASLOG_NOTHING 255
#define DASLOG_CRIT   100  /* same as java.util.logging.Level.SEVERE */
#define DASLOG_ERROR   80  
#define DASLOG_WARN    60  /* same as java.util.logging.Level.WARNING */
#define DASLOG_INFO    40  /* same as java.util.logging.Level.INFO & CONFIG */
#define DASLOG_DEBUG   20  /* same as java.util.logging.Level.FINE */
#define DASLOG_TRACE    0  /* same as java.util.logging.Level.FINER & FINEST */

/** Get the log level.
 *
 * @returns one of: DASLOG_CRIT, DASLOG_ERROR, DASLOG_WARN, DASLOG_INFO,
 *                  DASLOG_DEBUG, DASLOG_TRACE 
 */
DAS_API int daslog_level(void);

/** Set the logging level for this thread.
 *
 * @param nLevel Set to one of
 *   - DASLOG_TRACE
 *   - DASLOG_DEBUG
 *   - DASLOG_NOTICE
 *   - DASLOG_WARN
 *   - DASLOG_ERROR
 *   - DASLOG_CRITICAL
 *   - DASLOG_NOTHING
 *
 * @return The previous log level.
 */
DAS_API int daslog_setlevel(int nLevel);

/** Get a logging level integer from a string.
 * This function may safely be called prior to das_init() 
 * 
 * @param sLevel One of "crit", "err", "warn", "info", 
 *        "debug", "trace". Case is not signifiant, extra letters
 *        after the first are actually ignored.
 * 
 * @returns One of DASLOG_CRIT, DASLOG_ERROR, DASLOG_WARN,
 *        DASLOG_INFO, DASLOG_DEBUG, DASLOG_TRACE, if the string is 
 *        understood, DASLOG_NOTHING if not.
 * */
DAS_API int daslog_strlevel(const char* sLevel);

/** Output source file and line numbers for messages at or above this level */
DAS_API bool daslog_set_showline(int nLevel);


/* Basic logging function, macros use this */
DAS_API void daslog(int nLevel, const char* sSrcFile, int nLine, const char* sFmt, ...);

		
/** Macro wrapper around das_log() for TRACE messages with out variable args */
#define daslog_trace(M) daslog(DASLOG_TRACE, __FILE__, __LINE__, M)
/** Macro wrapper around das_log() for DEBUG messages with out variable args */
#define daslog_debug(M) daslog(DASLOG_DEBUG, __FILE__, __LINE__, M)
/** Macro wrapper around das_log() for INFO messages with out variable args */
#define daslog_info(M) daslog(DASLOG_INFO, __FILE__, __LINE__, M) 
/** Macro wrapper around das_log() for WARNING messages with out variable args */
#define daslog_warn(M) daslog(DASLOG_WARN, __FILE__, __LINE__, M) 
/** Macro wrapper around das_log() for ERROR messages with out variable args */
#define daslog_error(M) daslog(DASLOG_ERROR, __FILE__, __LINE__, M) 
/** Macro wrapper around das_log() for CRITICAL messages with out variable args */
#define daslog_critical(M) daslog(DAS_LL_CRITICAL, __FILE__, __LINE__, M)


/** Macro wrapper around das_log() for TRACE messages with variable arguments */
#define daslog_trace_v(F, ...)\
  daslog(DASLOG_TRACE, __FILE__, __LINE__, F, __VA_ARGS__)
/** Macro wrapper around das_log() for DEBUG messages with variable arguments */
#define daslog_debug_v(F, ...)\
  daslog(DASLOG_DEBUG, __FILE__, __LINE__, F, __VA_ARGS__)
/** Macro wrapper around das_log() for INFO messages with variable arguments */
#define daslog_info_v(F, ...)\
  daslog(DASLOG_INFO, __FILE__, __LINE__, F, __VA_ARGS__) 
/** Macro wrapper around das_log() for WARNING messages with variable arguments */
#define daslog_warn_v(F, ...)\
  daslog(DASLOG_WARN, __FILE__, __LINE__, F, __VA_ARGS__) 
/** Macro wrapper around das_log() for ERROR messages with variable arguments */
#define daslog_error_v(F, ...)\
  daslog(DASLOG_ERROR, __FILE__, __LINE__, F, __VA_ARGS__) 
/** Macro wrapper around das_log() for CRITICAL messages with variable arguments */
#define daslog_critical_v(F, ...)\
  daslog(DASLOG_CRIT, __FILE__, __LINE__, F, __VA_ARGS__)


/** Install a new message handler function for this thread.
 * The default message handler just prints to stderr, which is not very 
 * effecient, nor is it appropriate for GUI applications.
 * 
 * @param new_handler The new message handler, or NULL to set to the default
 *        handler.
 * @return The previous message handler function pointer
 */
DAS_API das_log_handler_t daslog_sethandler(das_log_handler_t new_handler);

/** @} */

#ifdef __cplusplus
}
#endif

#endif /* _das_log_h_ */
