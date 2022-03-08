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

#define _POSIX_C_SOURCE 200112
/* #define _XOPEN_SOURCE 500 */

#include <pthread.h>
#include <time.h>

#include "util.h"
#include "log.h"


/* Code adapted from GseLog.h by cwp */
void das_def_log_handler(int nLevel, const char* sMsg, bool bPrnTime);

int das_nMinLevel = DASLOG_WARN;
static int das_nMinLineLvl = DASLOG_ERROR;
static bool das_bLogWithTimes   = false;

static pthread_mutex_t mtxDasLog = PTHREAD_MUTEX_INITIALIZER;
das_log_handler_t das_curMsgHandler = das_def_log_handler;


#define LOCK()    pthread_mutex_lock(&mtxDasLog)
#define UNLOCK()  pthread_mutex_unlock(&mtxDasLog)


bool daslog_set_showline(int nLevel){
	int old;
	LOCK();
	old = das_nMinLineLvl;

	if((nLevel < DASLOG_TRACE) || (nLevel > DASLOG_NOTHING))
		das_error(DASERR_LOG, "Source line display threshold "
		             "level %d is not in the range %d to %d.", nLevel,
		             DASLOG_TRACE, DASLOG_NOTHING);

	das_nMinLineLvl = nLevel;
	UNLOCK();
	return old;
}

int daslog_setlevel(int nLevel){
	int old;
	LOCK();
	old = das_nMinLevel;

	if((nLevel < DASLOG_TRACE) || (nLevel > DASLOG_NOTHING))
		das_error(DASERR_LOG, "Message level %d is not in the "
				       "range %d to %d.", nLevel, DASLOG_TRACE, DASLOG_NOTHING);

	das_nMinLevel = nLevel;
	UNLOCK();
	return old;
}

int daslog_level(void){ return das_nMinLevel; }


void das_log_include_time(bool bPrnTime)
{
	LOCK();
	das_bLogWithTimes = bPrnTime;
	UNLOCK();
}


void daslog(int nLevel, const char* sSrcFile, int nLine, const char* sFmt, ...){
	char* sMsg, * sTmp;
	va_list ap;

	if(nLevel < das_nMinLevel) return;

	va_start(ap, sFmt);
	sMsg = das_vstring(sFmt, ap);
	va_end(ap);

	if((nLevel >= das_nMinLineLvl)&&(sSrcFile != NULL)&&(nLine > 0)){
		sTmp = sMsg;
		sMsg = das_string("%s\n\t(Reported from %s, line %d)", sMsg, sSrcFile, nLine);
		free(sTmp);
	}

	LOCK();
	das_curMsgHandler(nLevel, sMsg, das_bLogWithTimes);
	UNLOCK();
	free(sMsg);
}

das_log_handler_t daslog_sethandler(das_log_handler_t func){
	das_log_handler_t old;

	if(func == NULL)
		func = das_def_log_handler;
	LOCK();
	old = das_curMsgHandler;
	das_curMsgHandler = func;
	UNLOCK();
	return old;
}

/* No need to be thread safe in here, only called from das_log which locks
 * the mutex */
void das_def_log_handler(int nLevel, const char* sMsg, bool bPrnTime)
{
	const char* sLvl;
	char buf[32];
	char sTime[32];
#ifndef _WIN32
	struct tm bdTime;
#endif
	time_t tEpoch;
	buf[31] = '\0';
	sTime[31] = '\0';

	switch(nLevel){
		case DASLOG_TRACE: sLvl = "TRACE"; break;
		case DASLOG_DEBUG: sLvl = "DEBUG"; break;
		case DASLOG_INFO:  sLvl = "INFO"; break;
		case DASLOG_WARN:  sLvl = "WARNING"; break;
		case DASLOG_ERROR: sLvl = "ERROR"; break;
		case DASLOG_CRIT:  sLvl = "CRITICAL"; break;
		default:
			snprintf(buf, 31, "LEVEL %d MSG", nLevel); sLvl = buf; break;
	}

	if(bPrnTime){
		tEpoch = time(NULL);
#ifdef _WIN32
		/* Note: localtime is thread safe on win32, see note:
		 * http://sources.redhat.com/ml/pthreads-win32/2005/msg00011.html
		 */
		strftime(sTime, 31, "%Y-%m-%dT%H:%M:%S", localtime( &tEpoch));
#else
		strftime(sTime, 31, "%Y-%m-%dT%H:%M:%S", localtime_r( &tEpoch, &bdTime));
#endif

		fprintf(stderr, "(%s, %s) ", sTime, sLvl);
	}
	else{
		fprintf(stderr, "%s: ", sLvl);
	}
	fprintf(stderr, "%s\n", sMsg);

	/*
	if(sStackTrace != NULL)
		fprintf(stderr, "\t\n\tbacktrace\n\t---------\n%s", sStackTrace);
	*/
}

/* No exception or backtrace handling built into libdas2, but it may be
   interesting to do someday if desired */

/*
void das_log_except(int nLevel, except_t* pExcept)
{
	char code_buf[128];
	char group_buf[64];
	char xcept_buf[1024];
	const char* pGroup = NULL;
	const char* pMsg = NULL;
	size_t nWrote = 0;
	xcept_buf[1023] = '\0';

	if(nLevel < das_log_getlevel())
		return;

	/ * Make message string * /
	switch(except_group(pExcept)){
		case CLIB_XCEPT:
			pGroup = "CLIB";
			pMsg = clib_code_str(except_code(pExcept), code_buf, 127);
			break;
		case GSE_XCEPT:
			pGroup = "GSE";
			pMsg = das_code_str(except_code(pExcept), code_buf, 127);
			break;
		default:
			snprintf(group_buf, 63, "Group %lu", except_group(pExcept));
			pGroup = group_buf;
			snprintf(code_buf, 128, "Code %lu", except_code(pExcept));
			pMsg = code_buf;
	}
	nWrote = snprintf(xcept_buf, 1023, "%s exception, %s.\n", pGroup, pMsg);


	if(das_except_srcfile(pExcept) != NULL)
		nWrote += snprintf(xcept_buf + nWrote, 1023 - nWrote,
		                   "\tFrom %s, line %d: ", das_except_srcfile(pExcept),
		                   das_except_line(pExcept));
	else
		nWrote += snprintf(xcept_buf + nWrote, 1023 - nWrote,
		                   "\tFrom (unknown file), line %d: ",
		                   das_except_line(pExcept));


	if(nWrote < 1023){
		if(except_message(pExcept) != NULL)
			nWrote += snprintf(xcept_buf + nWrote, 1023 - nWrote,
			                   "%s", except_message(pExcept));
		else
			nWrote += snprintf(xcept_buf + nWrote, 1023 - nWrote, "(no message)");
	}

	/ * Prn the backtrace * /
	xcept_buf[1023] = '\0';
	/ * das_curMsgHandler(nLevel, xcept_buf, NULL, das_except_backtrace(pExcept));* /
	das_curMsgHandler(nLevel, xcept_buf, NULL, NULL, das_bLogWithTimes);
}
*/
