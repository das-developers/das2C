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


/** @file log.h Message logging, and where das_error() messages go
 *
 * Every message das2C produces goes one way: through a single log hook.
 * By default the hook prints to standard error.  Install your own with
 * das_init() or daslog_sethook() to send messages somewhere else: a GUI,
 * Python's logging module, the IDL message system.
 *
 * Library errors are log messages too.  das_error() logs at DASLOG_ERROR
 * when the library is set to return on errors, and at DASLOG_CRIT just
 * before it exits the program when it isn't.  Each message carries the
 * error code that das_error() was given, so a hook can tell an error from
 * an ordinary log line.  The library keeps no copy of any message.  If you
 * need the text of an error after the function that raised it returns,
 * save it in your hook.
 *
 * Messages are sent with daslog(), though the macros are less typing:
 *
 * | Level           | Plain message       | printf style          |
 * |-----------------|---------------------|-----------------------|
 * | DASLOG_TRACE    | daslog_trace()      | daslog_trace_v()      |
 * | DASLOG_DEBUG    | daslog_debug()      | daslog_debug_v()      |
 * | DASLOG_INFO     | daslog_info()       | daslog_info_v()       |
 * | DASLOG_WARN     | daslog_warn()       | daslog_warn_v()       |
 * | DASLOG_ERROR    | daslog_error()      | daslog_error_v()      |
 * | DASLOG_CRIT     | daslog_critical()   | daslog_critical_v()   |
 *
 * For example:
 * @code
 *   daslog_warn_v("File %s, Pkt %05d: Header Block > 256 bytes", sFile, nIdx);
 * @endcode
 * is equivalent to:
 * @code
 *   daslog(DASLOG_WARN, __FILE__, __func__, __LINE__,
 *          "File %s, Pkt %05d: Header Block > 256 bytes", sFile, nIdx);
 * @endcode
 *
 * Messages below the current level, see daslog_setlevel(), are dropped
 * before they reach the hook.  DASLOG_CRIT is the top level, so a message
 * that ends the program is always delivered.
 */

#ifndef _das_log_h_
#define _das_log_h_

#include <das3/defs.h>

#ifdef __cplusplus
extern "C" {
#endif

/** @addtogroup utilities
 * @{
 */

#define DASLOG_CRIT   100  /* same as java.util.logging.Level.SEVERE */
#define DASLOG_ERROR   80
#define DASLOG_WARN    60  /* same as java.util.logging.Level.WARNING */
#define DASLOG_INFO    40  /* same as java.util.logging.Level.INFO & CONFIG */
#define DASLOG_DEBUG   20  /* same as java.util.logging.Level.FINE */
#define DASLOG_TRACE    0  /* same as java.util.logging.Level.FINER & FINEST */

/** Size of the buffer a message is formatted into, terminator included.
 *
 * Longer messages are cut short and end in "...".  The library has no
 * other limit on message length.  Define it at build time to change it.
 */
#ifndef DASLOG_MSG_SZ
#define DASLOG_MSG_SZ 1024
#endif

/** One log message, as handed to a log hook.
 *
 * The library fills this in and lends it to the hook for the length of a
 * single call.  The structure and every string it points to are invalid
 * once the hook returns, so copy what you want to keep.
 *
 * Later versions of the library may add members to the end of this
 * structure, never in the middle.
 */
typedef struct das_logmsg {

	/** One of DASLOG_TRACE through DASLOG_CRIT */
	int nLevel;

	/** The error code given to das_error(), or 0 (DAS_OKAY) for a message
	 *  that did not come from das_error() */
	DasErrCode nErr;

	/** The message text, never NULL.  It carries no source location, that
	 *  is in the next three members. */
	const char* sMsg;

	/** Source file the message came from, may be NULL */
	const char* sFile;

	/** Function the message came from, may be NULL */
	const char* sFunc;

	/** Line in sFile, 0 if not known */
	int nLine;

} das_logmsg;

/** A log message handler.
 *
 * @param pMsg The message, valid only until the handler returns
 * @param pUser The pointer given along with the handler in a das_loghook
 */
typedef void (*das_loghandler_f)(const das_logmsg* pMsg, void* pUser);

/** A log handler and the pointer passed to it, set as a pair.
 *
 * The library never reads or writes through pUser, it only hands it back to
 * fn with each message.  It's the place to hang whatever state the handler
 * needs: a capture buffer, a GUI window, a Python logger.  Whatever pUser
 * points to must remain valid for as long as the hook is installed.
 *
 * The library copies the structure itself when it is installed, so the
 * das_loghook passed to das_init() or daslog_sethook() can be a local
 * variable.
 *
 * @par The handler contract
 *
 * The library calls the handler synchronously, on the thread that logged
 * the message, before daslog() or das_error() returns.  Calls are made one
 * at a time under a library lock, so the handler need not be thread safe,
 * and neither does state reached through pUser as long as only the handler
 * touches it.  In return the handler must:
 *
 *   - Forward the message and return.  Call no das2C function other than
 *     daslog_stderr(), and never exit(), longjmp() or throw out of it.
 *   - Not wait on any lock that a thread calling das2C might hold.  A
 *     Python binding, for example, should queue messages in C and pass them
 *     to the logging module after it has the GIL back, not take the GIL
 *     inside the handler.
 *
 * Cleanup before an error exits the program belongs in an atexit()
 * function, not in the handler.  das_error() exits only after the handler
 * has returned and the library lock is released, so atexit() functions may
 * use das2C freely, closing an output stream for example.
 *
 * @par Example: keeping an error for later
 *
 * The library stores no error messages, so a binding that raises an
 * exception after a call fails saves the text in its hook:
 * @code
 *   typedef struct { char sErr[DASLOG_MSG_SZ]; } capture_t;
 *
 *   static void onLog(const das_logmsg* pMsg, void* pUser){
 *      capture_t* pCap = (capture_t*)pUser;
 *      if(pMsg->nErr != DAS_OKAY)
 *         snprintf(pCap->sErr, DASLOG_MSG_SZ, "%s", pMsg->sMsg);
 *      else
 *         daslog_stderr(pMsg, NULL);
 *   }
 *
 *   static capture_t g_cap;
 *   das_loghook hook = {onLog, &g_cap};
 *   das_init("mymodule", DASERR_DIS_RET, DASLOG_INFO, &hook);
 * @endcode
 * Clear sErr before each call into das2C and it holds the error from that
 * call, if any.
 */
typedef struct das_loghook {
	das_loghandler_f fn;  /**< The handler */
	void* pUser;          /**< Handed back to fn with each message */
} das_loghook;


/** Get the log level.
 *
 * @returns one of DASLOG_TRACE, DASLOG_DEBUG, DASLOG_INFO, DASLOG_WARN,
 *          DASLOG_ERROR, DASLOG_CRIT
 */
DAS_API int daslog_level(void);

/** Set the log level for the program.
 *
 * Messages below this level are dropped before they reach the hook.  The
 * level is shared by all threads.
 *
 * When the library is set to return on errors, das_error() messages are at
 * DASLOG_ERROR, so a level of DASLOG_CRIT hides them.  das_init() refuses
 * that combination, this function allows it: it is useful around a call
 * that is expected to fail, a test for example.
 *
 * @param nLevel One of DASLOG_TRACE, DASLOG_DEBUG, DASLOG_INFO, DASLOG_WARN,
 *        DASLOG_ERROR, DASLOG_CRIT
 *
 * An invalid level is logged as a warning and leaves the level as it was,
 * so the return value is always a valid level and can be passed back here
 * to restore it:
 * @code
 *   int nOld = daslog_setlevel(daslog_strlevel(sUserText));
 *   ...
 *   daslog_setlevel(nOld);
 * @endcode
 *
 * @return The previous level
 */
DAS_API int daslog_setlevel(int nLevel);

/** Get a log level from a string.
 * This function may safely be called prior to das_init()
 *
 * @param sLevel One of "crit", "err", "warn", "info", "debug", "trace".
 *        Case is not significant and letters after the first are ignored.
 *
 * @returns One of DASLOG_CRIT, DASLOG_ERROR, DASLOG_WARN, DASLOG_INFO,
 *        DASLOG_DEBUG, DASLOG_TRACE, or -1 if the string is not understood.
 */
DAS_API int daslog_strlevel(const char* sLevel);

/** The opposite of daslog_strlevel()
 *
 * Is also safe to call prior to das_init().
 *
 * @param nLevel A log level
 *
 * @returns One of the strings "TRACE", "DEBUG", "INFO", "WARNING", "ERROR",
 *          "CRITICAL", or "" if nLevel is negative.  Values between the
 *          levels round down, 70 is "WARNING".
 */
DAS_API const char* daslog_levelstr(int nLevel);

/** Install a log hook
 *
 * @param pHook The new hook, copied in.  NULL restores the default, which
 *        prints to standard error, see daslog_stderr().
 *
 * @returns The hook that was installed before.  Pass its address back to
 *        this function to restore it:
 * @code
 *   das_loghook prev = daslog_sethook(&mine);
 *   ...
 *   daslog_sethook(&prev);
 * @endcode
 */
DAS_API das_loghook daslog_sethook(const das_loghook* pHook);

/** The default log handler, prints messages to standard error.
 *
 * Messages look like:
 * @code
 *   WARNING: Can't parse units 'log10Ratio', unexpected '1' at byte 4
 * @endcode
 * and optionally carry the local time and the source location, see
 * daslog_showtime() and daslog_showline().  Your own handler may call this
 * to print the messages it doesn't handle itself.
 *
 * @param pMsg The message
 * @param pUser Not used, may be NULL
 */
DAS_API void daslog_stderr(const das_logmsg* pMsg, void* pUser);

/** Have daslog_stderr() start each line with the time, off by default.
 *
 * Times are the local time, in ISO 8601 with the UTC offset, for example
 * 2026-11-05T14:02:51-0600.  Log times are for the operator reading them,
 * unlike data times, which are always UTC.
 *
 * @param bShow true to show the time, false to leave it out
 * @returns the previous setting
 */
DAS_API bool daslog_showtime(bool bShow);

/** Have daslog_stderr() show where ERROR and CRITICAL messages came from.
 *
 * On by default.  The source file, line and function are added on a second
 * line.  Turn it off when messages are meant for users rather than for
 * the people debugging the code.
 *
 * @param bShow true to show the source location, false to leave it out
 * @returns the previous setting
 */
DAS_API bool daslog_showline(bool bShow);


/** Send a log message, the macros below are the usual way to call this
 *
 * @param nLevel The message level
 * @param sSrcFile The source file, normally __FILE__, may be NULL
 * @param sFunc The function, normally __func__, may be NULL
 * @param nLine The line number, normally __LINE__
 * @param sFmt A printf style format string
 */
DAS_API void daslog(
	int nLevel, const char* sSrcFile, const char* sFunc, int nLine,
	const char* sFmt, ...
) _das_fmt_check(5, 6);


/** Macro wrapper around daslog() for TRACE messages with out variable args */
#define daslog_trace(M) daslog(DASLOG_TRACE, __FILE__, __func__, __LINE__, "%s", M)
/** Macro wrapper around daslog() for DEBUG messages with out variable args */
#define daslog_debug(M) daslog(DASLOG_DEBUG, __FILE__, __func__, __LINE__, "%s", M)
/** Macro wrapper around daslog() for INFO messages with out variable args */
#define daslog_info(M) daslog(DASLOG_INFO, __FILE__, __func__, __LINE__, "%s", M)
/** Macro wrapper around daslog() for WARNING messages with out variable args */
#define daslog_warn(M) daslog(DASLOG_WARN, __FILE__, __func__, __LINE__, "%s", M)
/** Macro wrapper around daslog() for ERROR messages with out variable args */
#define daslog_error(M) daslog(DASLOG_ERROR, __FILE__, __func__, __LINE__, "%s", M)
/** Macro wrapper around daslog() for CRITICAL messages with out variable args */
#define daslog_critical(M) daslog(DASLOG_CRIT, __FILE__, __func__, __LINE__, "%s", M)


/** Macro wrapper around daslog() for TRACE messages with variable arguments */
#define daslog_trace_v(F, ...)\
  daslog(DASLOG_TRACE, __FILE__, __func__, __LINE__, F, __VA_ARGS__)
/** Macro wrapper around daslog() for DEBUG messages with variable arguments */
#define daslog_debug_v(F, ...)\
  daslog(DASLOG_DEBUG, __FILE__, __func__, __LINE__, F, __VA_ARGS__)
/** Macro wrapper around daslog() for INFO messages with variable arguments */
#define daslog_info_v(F, ...)\
  daslog(DASLOG_INFO, __FILE__, __func__, __LINE__, F, __VA_ARGS__)
/** Macro wrapper around daslog() for WARNING messages with variable arguments */
#define daslog_warn_v(F, ...)\
  daslog(DASLOG_WARN, __FILE__, __func__, __LINE__, F, __VA_ARGS__)
/** Macro wrapper around daslog() for ERROR messages with variable arguments */
#define daslog_error_v(F, ...)\
  daslog(DASLOG_ERROR, __FILE__, __func__, __LINE__, F, __VA_ARGS__)
/** Macro wrapper around daslog() for CRITICAL messages with variable arguments */
#define daslog_critical_v(F, ...)\
  daslog(DASLOG_CRIT, __FILE__, __func__, __LINE__, F, __VA_ARGS__)

/** @} */

#ifdef __cplusplus
}
#endif

#endif /* _das_log_h_ */
