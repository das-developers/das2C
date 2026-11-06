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
#define _POSIX_C_SOURCE 200112

#include <pthread.h>
#include <time.h>
#include <stdio.h>
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>

#include "util.h"
#include "log.h"

/* The cut-short marker needs room, and a tiny buffer is a build mistake */
#if DASLOG_MSG_SZ < 64
#error "DASLOG_MSG_SZ must be at least 64 bytes"
#endif

/* Process wide settings.  The level is read without the lock on every
 * message; it is a single int that is set at startup, and a reader that
 * sees the old value for one message loses nothing. */
static int  g_nLevel     = DASLOG_WARN;
static bool g_bShowTime  = false;
static bool g_bShowLine  = true;

static das_loghook g_hook = {daslog_stderr, NULL};

/* The lock that serializes handler calls.  It is an error checking mutex
 * in every build: a handler that calls back into das2C gets EDEADLK from
 * the second lock attempt instead of hanging the thread.  There is no
 * portable static initializer for that type, hence pthread_once.
 *
 * Not every pthreads checks the type.  Emscripten without -pthread links
 * stubs whose lock always returns 0, so g_bInHook backs it up.  The flag is
 * only read or written with the lock held, so under real threads no other
 * thread can see it set and no TLS is needed; with stubs there is only one
 * thread to begin with. */
static pthread_mutex_t g_mtx;
static pthread_once_t  g_mtxOnce = PTHREAD_ONCE_INIT;
static bool            g_bInHook = false;

static void _daslog_initMutex(void)
{
	pthread_mutexattr_t attr;
	pthread_mutexattr_init(&attr);
	pthread_mutexattr_settype(&attr, PTHREAD_MUTEX_ERRORCHECK);
	pthread_mutex_init(&g_mtx, &attr);
	pthread_mutexattr_destroy(&attr);
}

static int _lock(void)
{
	pthread_once(&g_mtxOnce, _daslog_initMutex);
	return pthread_mutex_lock(&g_mtx);
}

static void _unlock(void){ pthread_mutex_unlock(&g_mtx); }

/* ************************************************************************* */
/* Level names */

int daslog_strlevel(const char* sLevel){
	if(sLevel != NULL){
		if(sLevel[0] == 'c'||sLevel[0] == 'C') return DASLOG_CRIT;
		if(sLevel[0] == 'e'||sLevel[0] == 'E') return DASLOG_ERROR;
		if(sLevel[0] == 'w'||sLevel[0] == 'W') return DASLOG_WARN;
		if(sLevel[0] == 'i'||sLevel[0] == 'I') return DASLOG_INFO;
		if(sLevel[0] == 'd'||sLevel[0] == 'D') return DASLOG_DEBUG;
		if(sLevel[0] == 't'||sLevel[0] == 'T') return DASLOG_TRACE;
	}
	return -1;
}

const char* daslog_levelstr(int nLevel)
{
	if(nLevel < DASLOG_TRACE)    return "";
	if(nLevel >= DASLOG_CRIT)    return "CRITICAL";
	if(nLevel >= DASLOG_ERROR)   return "ERROR";
	if(nLevel >= DASLOG_WARN)    return "WARNING";
	if(nLevel >= DASLOG_INFO)    return "INFO";
	if(nLevel >= DASLOG_DEBUG)   return "DEBUG";
	return "TRACE";
}

/* ************************************************************************* */
/* Settings */

/* Setters tolerate a failed lock.  The only way to get one is to be called
 * from inside a handler, which already holds it, and a plain store is still
 * the right outcome there. */

int daslog_level(void){ return g_nLevel; }

int daslog_setlevel(int nLevel){
	int nOld = g_nLevel;

	/* Report a bad level without changing anything, so the return value is
	   always a level the caller can hand back to restore it. */
	if((nLevel < DASLOG_TRACE) || (nLevel > DASLOG_CRIT)){
		daslog_warn_v("Log level %d is not in the range %d to %d, staying at %s",
		              nLevel, DASLOG_TRACE, DASLOG_CRIT, daslog_levelstr(nOld));
		return nOld;
	}

	int nLock = _lock();
	g_nLevel = nLevel;
	if(nLock == 0) _unlock();
	return nOld;
}

bool daslog_showtime(bool bShow)
{
	int nLock = _lock();
	bool bOld = g_bShowTime;
	g_bShowTime = bShow;
	if(nLock == 0) _unlock();
	return bOld;
}

bool daslog_showline(bool bShow)
{
	int nLock = _lock();
	bool bOld = g_bShowLine;
	g_bShowLine = bShow;
	if(nLock == 0) _unlock();
	return bOld;
}

das_loghook daslog_sethook(const das_loghook* pHook)
{
	das_loghook hookNew = {daslog_stderr, NULL};
	if((pHook != NULL)&&(pHook->fn != NULL)) hookNew = *pHook;

	int nLock = _lock();
	das_loghook hookOld = g_hook;
	g_hook = hookNew;
	if(nLock == 0) _unlock();
	return hookOld;
}

/* ************************************************************************* */
/* Delivery */

static void _dispatch(const das_logmsg* pMsg)
{
	int nLock = _lock();
	if((nLock != 0)||g_bInHook){
		/* The handler called back into das2C.  Say so where someone will see
		   it and drop the nested message, a hang helps nobody. */
		fprintf(stderr, "das2C: a log handler called back into the library, "
		        "nested %s message dropped: %s\n", daslog_levelstr(pMsg->nLevel),
		        pMsg->sMsg);

		/* A lock that "succeeded" here is a stub (no-op unlock) or a libc
		   that ignored the type and gave a recursive mutex, whose count this
		   rebalances.  Either way the outer call still holds it. */
		if(nLock == 0) _unlock();
		return;
	}

	/* Copy first so a handler that swaps hooks (it shouldn't) can't pull the
	   one being called out from under itself */
	das_loghook hook = g_hook;
	g_bInHook = true;
	hook.fn(pMsg, hook.pUser);
	g_bInHook = false;
	_unlock();
}

static void _daslog_vsend(
	int nLevel, DasErrCode nErr, const char* sFile, const char* sFunc,
	int nLine, const char* sFmt, va_list ap
){
	if(nLevel < g_nLevel) return;

	/* On the stack: no allocation means an out of memory error can still be
	   reported, and it costs nothing per message */
	char sBuf[DASLOG_MSG_SZ];
	int nNeed = vsnprintf(sBuf, DASLOG_MSG_SZ, sFmt, ap);
	if(nNeed < 0)
		snprintf(sBuf, DASLOG_MSG_SZ, "(message could not be formatted)");
	else if(nNeed >= DASLOG_MSG_SZ)
		memcpy(sBuf + DASLOG_MSG_SZ - 4, "...", 4);

	das_logmsg msg = {
		.nLevel = nLevel, .nErr = nErr, .sMsg = sBuf,
		.sFile = sFile, .sFunc = sFunc, .nLine = nLine
	};
	_dispatch(&msg);
}

static void _daslog_send(
	int nLevel, DasErrCode nErr, const char* sFile, const char* sFunc,
	int nLine, const char* sMsg
){
	if(nLevel < g_nLevel) return;

	das_logmsg msg = {
		.nLevel = nLevel, .nErr = nErr, .sMsg = (sMsg != NULL) ? sMsg : "",
		.sFile = sFile, .sFunc = sFunc, .nLine = nLine
	};
	_dispatch(&msg);
}

void daslog(
	int nLevel, const char* sSrcFile, const char* sFunc, int nLine,
	const char* sFmt, ...
){
	if(nLevel < g_nLevel) return;

	va_list ap;
	va_start(ap, sFmt);
	_daslog_vsend(nLevel, DAS_OKAY, sSrcFile, sFunc, nLine, sFmt, ap);
	va_end(ap);
}

/* ************************************************************************* */
/* The error facility, das_error() messages are log messages */

/* Set once at startup, see daserr_setdisp() */
static das_errdis_e g_nErrDisposition = DASERR_DIS_EXIT;

das_errdis_e daserr_disp(void){ return g_nErrDisposition; }

void daserr_setdisp(das_errdis_e nDisp){
	if((nDisp != DASERR_DIS_EXIT) && (nDisp != DASERR_DIS_RET)){
		das_error(DASERR_LOG, "Invalid error disposition %d", (int)nDisp);
		return;
	}
	g_nErrDisposition = nDisp;
}

/* The message goes to the log with the caller's location, then the
 * disposition decides.  exit() comes only after the hook has returned and
 * the log lock is released, so atexit() functions may use the library. */

DasErrCode daserr_func(
	const char* sFile, const char* sFunc, int nLine, DasErrCode nCode,
	const char* sFmt, ...
){
	bool bExit = (g_nErrDisposition != DASERR_DIS_RET);
	
	va_list ap;
	va_start(ap, sFmt);
	_daslog_vsend(bExit ? DASLOG_CRIT : DASLOG_ERROR, nCode, sFile, sFunc,
	              nLine, sFmt, ap);
	va_end(ap);

	if(bExit) exit(nCode);
	return nCode;
}

DasErrCode daserr_func_fixed(
	const char* sFile, const char* sFunc, int nLine, DasErrCode nCode,
	const char* sMsg
){
	bool bExit = (g_nErrDisposition != DASERR_DIS_RET);
	
	_daslog_send(bExit ? DASLOG_CRIT : DASLOG_ERROR, nCode, sFile, sFunc,
	             nLine, sMsg);

	if(bExit) exit(nCode);
	return nCode;
}

/* ************************************************************************* */
/* The default handler */

void daslog_stderr(const das_logmsg* pMsg, void* pUser)
{
	(void)pUser;

	const char* sLvl = daslog_levelstr(pMsg->nLevel);

	if(g_bShowTime){
		char sTime[32] = {'\0'};
		time_t tEpoch = time(NULL);
#ifdef _WIN32
		/* localtime is thread safe on win32, it uses thread local storage */
		strftime(sTime, sizeof(sTime), "%Y-%m-%dT%H:%M:%S%z", localtime(&tEpoch));
#else
		struct tm bdTime;
		strftime(sTime, sizeof(sTime), "%Y-%m-%dT%H:%M:%S%z",
		         localtime_r(&tEpoch, &bdTime));
#endif
		fprintf(stderr, "(%s, %s) %s\n", sTime, sLvl, pMsg->sMsg);
	}
	else{
		fprintf(stderr, "%s: %s\n", sLvl, pMsg->sMsg);
	}

	if(g_bShowLine && (pMsg->nLevel >= DASLOG_ERROR) && (pMsg->sFile != NULL)){
		if(pMsg->sFunc != NULL)
			fprintf(stderr, "\t(reported from %s:%d, %s)\n", pMsg->sFile,
			        pMsg->nLine, pMsg->sFunc);
		else
			fprintf(stderr, "\t(reported from %s:%d)\n", pMsg->sFile, pMsg->nLine);
	}
}
