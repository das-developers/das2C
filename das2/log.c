#define _POSIX_C_SOURCE 200112
#define _XOPEN_SOURCE 500

#ifdef DAS_THREAD_SAFE
#include <pthread.h>
#endif

#include "util.h"
#include "log.h"

#include <time.h>

/* Code adapted from GseLog.h by cwp */

void das_def_log_handler(int nLevel, const char* sMsg, bool bPrnTime);

static int das_nMinLevel = DAS_LL_WARN;
static int das_nMinLineLvl = DAS_LL_ERROR;
static bool das_bLogWithTimes   = false;
das_log_handler_t das_curMsgHandler = das_def_log_handler;


#ifdef DAS_THREAD_SAFE /* IS THREADED (per-thread storage muste be implemented
                          above */

#error Implement per-thread log level variables in log.c to have thread-safe logging
										  
static pthread_mutex_t s_das_log_mtx = PTHREAD_MUTEX_INITIALIZER;
#define LOCK()    pthread_mutex_lock(&s_das_log_mtx)
#define UNLOCK()  pthread_mutex_unlock(&s_das_log_mtx)

#else /* NOT THREADED */

#define LOCK() 
#define UNLOCK()

#endif /* THREADED */


/* Used to initialize per-thread variables, not used for now */
/*
void das_log_thread_init(int nLevel, das_log_handler_t func)
{
	LOCK();
	if((nLevel > DAS_LL_TRACE) || (nLevel < DAS_LL_NOTHING))
		das_nMinLevel = nLevel;
	if(func != NULL)
		das_curMsgHandler = func;
	UNLOCK();
}

		
void das_log_deinit()
{
	/ * This function does nothing right now, but is a place holder
	   for future implementation of per thread logging * /
	return;
};

*/

bool das_log_set_showline(int nLevel){ 
	int old;
	LOCK();
	old = das_nMinLineLvl;
	
	if((nLevel < DAS_LL_TRACE) || (nLevel > DAS_LL_NOTHING))
		das2_error(DAS2ERR_LOG, "Source line display threshold "
		             "level %d is not in the range %d to %d.", nLevel,
		             DAS_LL_TRACE, DAS_LL_NOTHING);
	
	das_nMinLineLvl = nLevel;
	UNLOCK();
	return old;	
}

int das_log_setlevel(int nLevel){ 
	int old;
	LOCK();
	old = das_nMinLevel;
	
	if((nLevel < DAS_LL_TRACE) || (nLevel > DAS_LL_NOTHING))
		das2_error(DAS2ERR_LOG, "Message level %d is not in the "
				       "range %d to %d.", nLevel, DAS_LL_TRACE, DAS_LL_NOTHING);
	
	das_nMinLevel = nLevel;
	UNLOCK();
	return old;	
}

int das_log_level(){ return das_nMinLevel; }


void das_log_include_time(bool bPrnTime)
{
	LOCK();
	das_bLogWithTimes = bPrnTime;
	UNLOCK();
}


void das_log(int nLevel, const char* sSrcFile, int nLine, const char* sFmt, ...){
	char* sMsg, * sTmp;
	va_list ap;
	
	if(nLevel < das_nMinLevel) return;
	
	va_start(ap, sFmt);
	sMsg = das2_vstring(sFmt, ap);
	va_end(ap);
	
	if((nLevel >= das_nMinLineLvl)&&(sSrcFile != NULL)&&(nLine > 0)){
		sTmp = sMsg;
		sMsg = das2_string("%s\n\t(Reported from %s, line %d)", sMsg, sSrcFile, nLine);
		free(sTmp);
	}
	
	das_curMsgHandler(nLevel, sMsg, das_bLogWithTimes);
	free(sMsg);
}

das_log_handler_t das_log_sethandler(das_log_handler_t func){
	das_log_handler_t old;
	
	if(func == NULL)
		func = das_def_log_handler;
					
	old = das_curMsgHandler;
	das_curMsgHandler = func;
	return old;
}

void das_def_log_handler(int nLevel, const char* sMsg, bool bPrnTime)
{
	const char* sLvl;
	char buf[32];
	char sTime[32];
#ifndef __MINGW32__
	struct tm bdTime;
#endif
	time_t tEpoch;
	buf[31] = '\0';
	sTime[31] = '\0';
	
	switch(nLevel){
		case DAS_LL_TRACE: sLvl = "TRACE"; break;
		case DAS_LL_DEBUG: sLvl = "DEBUG"; break;
		case DAS_LL_INFO:  sLvl = "INFO"; break; 
		case DAS_LL_WARN:  sLvl = "WARNING"; break; 
		case DAS_LL_ERROR: sLvl = "ERROR"; break;	
		case DAS_LL_CRIT:  sLvl = "CRITICAL"; break;
		default:
			snprintf(buf, 31, "LEVEL %d MSG", nLevel); sLvl = buf; break;
	}
	
	if(bPrnTime){
		tEpoch = time(NULL);
#ifdef __MINGW32__
		/* Note: localtime is thread safe on win32, see note:
		 * http://sources.redhat.com/ml/pthreads-win32/2005/msg00011.html
		 */
		strftime(sTime, 31, "%Y-%m-%dT%H:%M:%S", localtime( &tEpoch));
#else
		strftime(sTime, 31, "%Y-%m-%dT%H:%M:%S", localtime_r( &tEpoch, &bdTime));
#endif
	
		fprintf(stderr, "(%s, %8s) ", sTime, sLvl);
	}
	else{
		fprintf(stderr, "%8s: ", sLvl);
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
