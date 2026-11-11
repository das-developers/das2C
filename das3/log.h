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


/** @file log.h Message logging, and where daserrf() messages go
 *
 * Every message das2C produces goes one way: through a single log hook.
 * By default the hook prints to standard error.  Install your own with
 * daslib_init() or daslog_setHook() to send messages somewhere else: a GUI,
 * Python's logging module, the IDL message system.
 *
 * Library errors are log messages too.  daserrf() logs at DASLOG_ERROR
 * when the library is set to return on errors, and at DASLOG_CRITICAL just
 * before it exits the program when it isn't.  Each message carries the
 * error code that daserrf() was given, so a hook can tell an error from
 * an ordinary log line.  The library keeps no copy of any message.  If you
 * need the text of an error after the function that raised it returns,
 * save it in your hook.
 *
 * Messages are sent with daslog_funcf(), though the macros are less typing:
 *
 * | Level           | Plain message       | printf style          |
 * |-----------------|---------------------|-----------------------|
 * | DASLOG_TRACE    | daslog_trace()      | daslog_tracef()      |
 * | DASLOG_DEBUG    | daslog_debug()      | daslog_debugf()      |
 * | DASLOG_INFO     | daslog_info()       | daslog_infof()       |
 * | DASLOG_WARN     | daslog_warn()       | daslog_warnf()       |
 * | DASLOG_ERROR    | daslog_error()      | daslog_errorf()      |
 * | DASLOG_CRITICAL     | daslog_critical()   | daslog_criticalf()   |
 *
 * For example:
 * @code
 *   daslog_warnf("File %s, Pkt %05d: Header Block > 256 bytes", sFile, nIdx);
 * @endcode
 * is equivalent to:
 * @code
 *   daslog_funcf(DASLOG_WARN, __FILE__, __func__, __LINE__,
 *          "File %s, Pkt %05d: Header Block > 256 bytes", sFile, nIdx);
 * @endcode
 *
 * Messages below the current level, see daslog_setLevel(), are dropped
 * before they reach the hook.  DASLOG_CRITICAL is the top level, so a message
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

#define DASLOG_CRITICAL   100  /* same as java.util.logging.Level.SEVERE */
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
typedef struct das_logmsg_t {

	/** One of DASLOG_TRACE through DASLOG_CRITICAL */
	int level;

	/** The error code given to daserrf(), or 0 (DAS_OKAY) for a message
	 *  that did not come from daserrf() */
	das_errcode errCode;

	/** The message text, never NULL.  It carries no source location, that
	 *  is in the next three members. */
	const char* msg;

	/** Source file the message came from, may be NULL */
	const char* file;

	/** Function the message came from, may be NULL */
	const char* func;

	/** Line in file, 0 if not known */
	int line;

} DasLogMsg;

/** A log message handler.
 *
 * @param pMsg The message, valid only until the handler returns
 * @param pUser The pointer given along with the handler in a DasLogHook
 */
typedef void (*dasfn_onLog)(const DasLogMsg* pMsg, void* pUser);

/** A log handler and the pointer passed to it, set as a pair.
 *
 * The library never reads or writes through userData, it only hands it back to
 * callback with each message.  It's the place to hang whatever state the handler
 * needs: a capture buffer, a GUI window, a Python logger.  Whatever userData
 * points to must remain valid for as long as the hook is installed.
 *
 * The library copies the structure itself when it is installed, so the
 * DasLogHook passed to daslib_init() or daslog_setHook() can be a local
 * variable.
 *
 * @par The handler contract
 *
 * The library calls the handler synchronously, on the thread that logged
 * the message, before daslog_funcf() or daserrf() returns.  Calls are made one
 * at a time under a library lock, so the handler need not be thread safe,
 * and neither does state reached through userData as long as only the handler
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
 * function, not in the handler.  daserrf() exits only after the handler
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
 *   static void onLog(const DasLogMsg* pMsg, void* pUser){
 *      capture_t* pCap = (capture_t*)pUser;
 *      if(pMsg->errCode != DAS_OKAY)
 *         snprintf(pCap->sErr, DASLOG_MSG_SZ, "%s", pMsg->msg);
 *      else
 *         daslog_stderr(pMsg, NULL);
 *   }
 *
 *   static capture_t g_cap;
 *   DasLogHook hook = {onLog, &g_cap};
 *   daslib_init("mymodule", DASERRDISP_RET, DASLOG_INFO, &hook);
 * @endcode
 * Clear sErr before each call into das2C and it holds the error from that
 * call, if any.
 */
typedef struct das_loghook_t {
	dasfn_onLog callback;  /**< The handler */
	void* userData;        /**< Handed back to callback with each message */
} DasLogHook;


/** Get the log level.
 *
 * @returns one of DASLOG_TRACE, DASLOG_DEBUG, DASLOG_INFO, DASLOG_WARN,
 *          DASLOG_ERROR, DASLOG_CRITICAL
 */
DAS_API int daslog_level(void);

/** Set the log level for the program.
 *
 * Messages below this level are dropped before they reach the hook.  The
 * level is shared by all threads.
 *
 * When the library is set to return on errors, daserrf() messages are at
 * DASLOG_ERROR, so a level of DASLOG_CRITICAL hides them.  daslib_init() refuses
 * that combination, this function allows it: it is useful around a call
 * that is expected to fail, a test for example.
 *
 * @param nLevel One of DASLOG_TRACE, DASLOG_DEBUG, DASLOG_INFO, DASLOG_WARN,
 *        DASLOG_ERROR, DASLOG_CRITICAL
 *
 * An invalid level is logged as a warning and leaves the level as it was,
 * so the return value is always a valid level and can be passed back here
 * to restore it:
 * @code
 *   int nOld = daslog_setLevel(daslog_levelFromStr(sUserText));
 *   ...
 *   daslog_setLevel(nOld);
 * @endcode
 *
 * @return The previous level
 */
DAS_API int daslog_setLevel(int nLevel);

/** Get a log level from a string.
 * This function may safely be called prior to daslib_init()
 *
 * @param sLevel One of "crit", "err", "warn", "info", "debug", "trace".
 *        Case is not significant and letters after the first are ignored.
 *
 * @returns One of DASLOG_CRITICAL, DASLOG_ERROR, DASLOG_WARN, DASLOG_INFO,
 *        DASLOG_DEBUG, DASLOG_TRACE, or -1 if the string is not understood.
 */
DAS_API int daslog_levelFromStr(const char* sLevel);

/** The opposite of daslog_levelFromStr()
 *
 * Is also safe to call prior to daslib_init().
 *
 * @param nLevel A log level
 *
 * @returns One of the strings "TRACE", "DEBUG", "INFO", "WARNING", "ERROR",
 *          "CRITICAL", or "" if nLevel is negative.  Values between the
 *          levels round down, 70 is "WARNING".
 */
DAS_API const char* daslog_levelToStr(int nLevel);

/** Install a log hook
 *
 * @param pHook The new hook, copied in.  NULL restores the default, which
 *        prints to standard error, see daslog_stderr().
 *
 * @returns The hook that was installed before.  Pass its address back to
 *        this function to restore it:
 * @code
 *   DasLogHook prev = daslog_setHook(&mine);
 *   ...
 *   daslog_setHook(&prev);
 * @endcode
 */
DAS_API DasLogHook daslog_setHook(const DasLogHook* pHook);

/** The default log handler, prints messages to standard error.
 *
 * Messages look like:
 * @code
 *   WARNING: Can't parse units 'log10Ratio', unexpected '1' at byte 4
 * @endcode
 * and optionally carry the local time and the source location, see
 * daslog_showTime() and daslog_showLine().  Your own handler may call this
 * to print the messages it doesn't handle itself.
 *
 * @param pMsg The message
 * @param pUser Not used, may be NULL
 */
DAS_API void daslog_stderr(const DasLogMsg* pMsg, void* pUser);

/** Have daslog_stderr() start each line with the time, off by default.
 *
 * Times are the local time, in ISO 8601 with the UTC offset, for example
 * 2026-11-05T14:02:51-0600.  Log times are for the operator reading them,
 * unlike data times, which are always UTC.
 *
 * @param bShow true to show the time, false to leave it out
 * @returns the previous setting
 */
DAS_API bool daslog_showTime(bool bShow);

/** Have daslog_stderr() show where ERROR and CRITICAL messages came from.
 *
 * On by default.  The source file, line and function are added on a second
 * line.  Turn it off when messages are meant for users rather than for
 * the people debugging the code.
 *
 * @param bShow true to show the source location, false to leave it out
 * @returns the previous setting
 */
DAS_API bool daslog_showLine(bool bShow);


/** Send a log message, the macros below are the usual way to call this
 *
 * @param nLevel The message level
 * @param sSrcFile The source file, normally __FILE__, may be NULL
 * @param sFunc The function, normally __func__, may be NULL
 * @param nLine The line number, normally __LINE__
 * @param sFmt A printf style format string
 */
DAS_API void daslog_funcf(
	int nLevel, const char* sSrcFile, const char* sFunc, int nLine,
	const char* sFmt, ...
) _das_fmt_check(5, 6);


/** Macro wrapper around daslog_funcf() for TRACE messages with out variable args */
#define daslog_trace(M) daslog_funcf(DASLOG_TRACE, __FILE__, __func__, __LINE__, "%s", M)
/** Macro wrapper around daslog_funcf() for DEBUG messages with out variable args */
#define daslog_debug(M) daslog_funcf(DASLOG_DEBUG, __FILE__, __func__, __LINE__, "%s", M)
/** Macro wrapper around daslog_funcf() for INFO messages with out variable args */
#define daslog_info(M) daslog_funcf(DASLOG_INFO, __FILE__, __func__, __LINE__, "%s", M)
/** Macro wrapper around daslog_funcf() for WARNING messages with out variable args */
#define daslog_warn(M) daslog_funcf(DASLOG_WARN, __FILE__, __func__, __LINE__, "%s", M)
/** Macro wrapper around daslog_funcf() for ERROR messages with out variable args */
#define daslog_error(M) daslog_funcf(DASLOG_ERROR, __FILE__, __func__, __LINE__, "%s", M)
/** Macro wrapper around daslog_funcf() for CRITICAL messages with out variable args */
#define daslog_critical(M) daslog_funcf(DASLOG_CRITICAL, __FILE__, __func__, __LINE__, "%s", M)


/** Macro wrapper around daslog_funcf() for TRACE messages with variable arguments */
#define daslog_tracef(F, ...)\
  daslog_funcf(DASLOG_TRACE, __FILE__, __func__, __LINE__, F, __VA_ARGS__)
/** Macro wrapper around daslog_funcf() for DEBUG messages with variable arguments */
#define daslog_debugf(F, ...)\
  daslog_funcf(DASLOG_DEBUG, __FILE__, __func__, __LINE__, F, __VA_ARGS__)
/** Macro wrapper around daslog_funcf() for INFO messages with variable arguments */
#define daslog_infof(F, ...)\
  daslog_funcf(DASLOG_INFO, __FILE__, __func__, __LINE__, F, __VA_ARGS__)
/** Macro wrapper around daslog_funcf() for WARNING messages with variable arguments */
#define daslog_warnf(F, ...)\
  daslog_funcf(DASLOG_WARN, __FILE__, __func__, __LINE__, F, __VA_ARGS__)
/** Macro wrapper around daslog_funcf() for ERROR messages with variable arguments */
#define daslog_errorf(F, ...)\
  daslog_funcf(DASLOG_ERROR, __FILE__, __func__, __LINE__, F, __VA_ARGS__)
/** Macro wrapper around daslog_funcf() for CRITICAL messages with variable arguments */
#define daslog_criticalf(F, ...)\
  daslog_funcf(DASLOG_CRITICAL, __FILE__, __func__, __LINE__, F, __VA_ARGS__)

/** @} */

#ifdef __cplusplus
}
#endif

#endif /* _das_log_h_ */
