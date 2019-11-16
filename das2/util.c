/* Copyright (C) 2004-2017 Chris Piker <chris-piker@uiowa.edu>
 *                         Jeremy Faden <jeremy-faden@uiowa.edu>
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

#define _POSIX_C_SOURCE 200112L
#define _XOPEN_SOURCE 500

#include <pthread.h>
#include <errno.h>
#include <locale.h>

#include <stdlib.h>
#include <stdio.h>
#include <stdarg.h>
#include <string.h>

#ifndef _WIN32
#include <strings.h>
#include <dirent.h>
#else
#include "win_dirent.h"
#endif

#include <sys/stat.h>
#include <limits.h>
#include <ctype.h>

#include "util.h"
#include "log.h"
#include "dft.h"
#include "units.h"
#include "http.h"

#define _QDEF(x) #x
#define QDEF(x) _QDEF(x)

#define DAS2_MSGDIS_STDERR 0
#define DAS2_MSGDIS_SAVE   1

int g_nErrDisposition = DASERR_DIS_EXIT;
int g_nMsgDisposition = DAS2_MSGDIS_STDERR;

pthread_mutex_t g_mtxErrBuf = PTHREAD_MUTEX_INITIALIZER;
das_error_msg* g_msgBuf = NULL;

/* ************************************************************************** */
/* Unavoidable global library structure initialization */

void das_init(
	const char* sProgName, int nErrDis, int nErrBufSz, int nLevel, 
	das_log_handler_t logfunc
){

	/* Das2 Lib is UTF-8 internally, so set to an appropriate codepage */
#ifdef LOCALE
	setlocale(LC_ALL, QDEF(LOCALE));
#else
	setlocale(LC_ALL, "");
#endif
	
	/* Though messages should be language dependent sscanf for das2 streams
	   needs to be the same or else the streams won't parse properly in
		different locales. */
	setlocale(LC_NUMERIC, "C");


	if((nErrDis != DASERR_DIS_EXIT) && (nErrDis != DASERR_DIS_RET) &&
	   (nErrDis != DASERR_DIS_ABORT)){
		fprintf(stderr, "(%s) das_init: Invalid error disposition value, %d\n", 
		        sProgName, nErrDis);
		exit(DASERR_INIT);
	}
	g_nErrDisposition = nErrDis;

	/* Setup the mutex for buffer locking, even if it's not used */
	pthread_mutexattr_t attr;
	pthread_mutexattr_init(&attr);
#ifndef NDEBUG
	pthread_mutexattr_settype(&attr, PTHREAD_MUTEX_ERRORCHECK);
#endif
	if( pthread_mutex_init(&g_mtxErrBuf, &attr) != 0){
		fprintf(stderr, "(%s) das_init: Could not initialize error buffer "
		        "mutex\n", sProgName);
		exit(DASERR_INIT);
	}

	if(nErrBufSz > 63){
		if(! das_save_error(nErrBufSz) ){
			fprintf(stderr, "(%s) das_init: Could not initialize error buffer\n",
			        sProgName);
			exit(DASERR_INIT);
		}
	}

	if((nLevel < DASLOG_TRACE) || (nLevel > DASLOG_NOTHING)){
		fprintf(stderr, "(%s) das_init: Invalid log level value, %d\n", 
				  sProgName, nLevel);
		exit(DASERR_INIT);
	}
	else{
		daslog_setlevel(nLevel);
	}

	if( ! units_init(sProgName) ){
		das_error(DASERR_INIT, "(%s) Failed units initialization", sProgName);
		exit(DASERR_INIT);
	}
	if( ! dft_init(sProgName) ){
		das_error(DASERR_INIT, "(%s) Failed DFT initialization", sProgName);
		exit(DASERR_INIT);
	}
	
	if( ! das_http_init(sProgName)){
		das_error(DASERR_INIT, "(%s) Failed HTTP initialization", sProgName);
		exit(DASERR_INIT);
	}
	
	if(logfunc) daslog_sethandler(logfunc);
}

void das_finish(){
	/* A do nothing function on Unix, closes network sockets on windows */
	das_http_finish();
}


/* ************************************************************************** */
/* Data structure creation utilities */

char* das_strdup(const char* sIn)
{
	if((sIn == NULL)||(sIn[0] == '\0')) return NULL;
	char* sOut = (char*)calloc(strlen(sIn)+1, sizeof(char));
	if(sOut == NULL){
		das_error(DASERR_UTIL, "Memory allocation error");
		return NULL;
	}
	strncpy(sOut, sIn, strlen(sIn));
	return sOut;
}

/* ************************************************************************* */
/* Program Exit Utilities */

/* You should almost never use this, it causes partial packet output */
void das_abort_on_error() { g_nErrDisposition = DASERR_DIS_ABORT; }

void das_exit_on_error()  { g_nErrDisposition = DASERR_DIS_EXIT; }

void das_return_on_error(){ g_nErrDisposition = DASERR_DIS_RET; }

int das_error_disposition(){ return g_nErrDisposition; }

void das_free_msgbuf(void) {
	das_error_msg* tmp = NULL;
	if (g_msgBuf) {
		tmp = g_msgBuf;
		g_msgBuf = NULL;
		if (tmp->message) {
			free(tmp->message);
		}
		free(tmp);
	}
}

void das_print_error() {
	g_nMsgDisposition = DAS2_MSGDIS_STDERR;
	das_free_msgbuf();
}

bool das_save_error(int nMaxMsg)
{
	das_error_msg* tmp = NULL;
	g_nMsgDisposition = DAS2_MSGDIS_SAVE;
	if (g_msgBuf) {
		das_free_msgbuf();
	}
	tmp = (das_error_msg*)malloc(sizeof(das_error_msg));
	if (!tmp) return false;
	tmp->nErr = DAS_OKAY;
	tmp->message = (char*)malloc(sizeof(char)*nMaxMsg);
	if (!tmp->message) {
		free(tmp);
		return false;
	}
	tmp->maxmsg = nMaxMsg;
	tmp->sFile[0] = '\0';
	tmp->sFunc[0] = '\0';
	tmp->nLine = -1;

	pthread_mutex_lock(&g_mtxErrBuf);
	g_msgBuf = tmp;
	pthread_mutex_unlock(&g_mtxErrBuf);
	return true;
}

das_error_msg* das_get_error()
{
	das_error_msg* pMsg = (das_error_msg*)calloc(1, sizeof(das_error_msg));
	if(!pMsg) return NULL;
	pthread_mutex_lock(&g_mtxErrBuf);
	memcpy(pMsg, g_msgBuf, sizeof(das_error_msg));
	pMsg->message = (char*)calloc(g_msgBuf->maxmsg, sizeof(char));
	memcpy(pMsg->message, g_msgBuf->message, g_msgBuf->maxmsg);
	pthread_mutex_unlock(&g_mtxErrBuf);
	return pMsg;
}

void das_error_free(das_error_msg* pMsg)
{
	if(pMsg == g_msgBuf) return;
	free(pMsg->message);
	free(pMsg);
}


DasErrCode das_error_func(
	const char* sFile, const char* sFunc, int nLine, DasErrCode nCode,
	const char* sFmt, ...
){
	va_list argp;

	if (g_nMsgDisposition == DAS2_MSGDIS_STDERR) {
		fputs("ERROR: ", stderr);
		va_start(argp, sFmt);
		vfprintf(stderr, sFmt, argp );
		va_end(argp);

		fprintf(stderr, "  (reported from %s:%d, %s)\n", sFile, nLine, sFunc);
	}
	else if (g_nMsgDisposition == DAS2_MSGDIS_SAVE) {
		pthread_mutex_lock(&g_mtxErrBuf);

		if (g_msgBuf != NULL && g_msgBuf->message != NULL) {
			va_start(argp, sFmt);
			vsnprintf(g_msgBuf->message, g_msgBuf->maxmsg - 1, sFmt, argp);
			va_end(argp);

			snprintf(g_msgBuf->sFile, sizeof(g_msgBuf->sFile) - 1, "%s", sFile);
			snprintf(g_msgBuf->sFunc, sizeof(g_msgBuf->sFunc) - 1, "%s", sFunc);
			g_msgBuf->nLine = nLine;
			g_msgBuf->nErr = nCode;
		}

		pthread_mutex_unlock(&g_mtxErrBuf);
	}


	if(g_nErrDisposition == DASERR_DIS_ABORT) abort(); /* Should dump core*/
	if(g_nErrDisposition == DASERR_DIS_EXIT) exit(nCode);

	return nCode;
}

DasErrCode das_error_func_fixed(
	const char* sFile, const char* sFunc, int nLine, DasErrCode nCode,
	const char* sMsg
){

	if (g_nMsgDisposition == DAS2_MSGDIS_STDERR) {
		fputs("ERROR: ", stderr);
		fputs(sMsg, stderr);
		fprintf(stderr, "  (reported from %s:%d, %s)\n", sFile, nLine, sFunc);
	}
	else if (g_nMsgDisposition == DAS2_MSGDIS_SAVE) {
		pthread_mutex_lock(&g_mtxErrBuf);

		if (g_msgBuf != NULL && g_msgBuf->message != NULL) {
			strncpy(g_msgBuf->message, sMsg, g_msgBuf->maxmsg - 1);

			snprintf(g_msgBuf->sFile, sizeof(g_msgBuf->sFile) - 1, "%s", sFile);
			snprintf(g_msgBuf->sFunc, sizeof(g_msgBuf->sFunc) - 1, "%s", sFunc);
			g_msgBuf->nLine = nLine;
			g_msgBuf->nErr = nCode;
		}

		pthread_mutex_unlock(&g_mtxErrBuf);
	}

	if(g_nErrDisposition == DASERR_DIS_ABORT) abort(); /* Should dump core*/
	if(g_nErrDisposition == DASERR_DIS_EXIT) exit(nCode);

	return nCode;
}

/* ************************************************************************* */
/* String utilities */

void das_store_str(char** psDest, size_t* puLen, const char* sSrc){
	size_t uNewLen = strlen(sSrc) + 1;
	if(uNewLen > *puLen){
		if(*psDest != NULL) free(*psDest);
		*psDest = calloc(uNewLen, sizeof(char));
		*puLen = uNewLen;
	}
	strncpy(*psDest, sSrc, uNewLen);
}

char* das_vstring(const char* fmt, va_list ap){

	/* Guess we need no more than 64 bytes. */
	int n, size = 64;
	char *p;
	va_list _ap;

	if( (p = malloc(size)) == NULL){
		das_error(DASERR_UTIL, "Can't alloc %d bytes", size);
		return NULL;
	}

	while (1) {
		/* Try to print in the allocated space. */
		va_copy(_ap, ap);
		n = vsnprintf (p, size, fmt, _ap);
		va_end(_ap);

		/* If that worked, return the string. */
		if (n > -1 && n < size)
			break;

		/* Else try again with more space. */
		if (n > -1) 	/* glibc 2.1 */
			size = n+1; /* precisely what is needed */
		else  			/* glibc 2.0 */
			size *= 2;  /* twice the old size */

		if((p = realloc(p, size)) == NULL){
			das_error(DASERR_UTIL, "Can't alloc %d bytes", size);
			return NULL;
		}
	}

	return p;
}

char* das_string(const char *fmt, ...){

	/* Guess we need no more than 64 bytes. */
	int n, size = 64;
	char *p;
	va_list ap;

	if( (p = malloc(size)) == NULL){
		das_error(DASERR_UTIL, "Can't alloc %d bytes", size);
		return NULL;
	}

	while (1) {
		/* Try to print in the allocated space. */
		va_start(ap, fmt);
		n = vsnprintf (p, size, fmt, ap);
		va_end(ap);

		/* If that worked, return the string. */
		if (n > -1 && n < size)
			break;

		/* Else try again with more space. */
		if (n > -1) 	/* glibc 2.1 */
			size = n+1; /* precisely what is needed */
		else  			/* glibc 2.0 */
			size *= 2;  /* twice the old size */

		if((p = realloc(p, size)) == NULL){
			das_error(DASERR_UTIL, "Can't alloc %d bytes", size);
			return NULL;
		}
	}

	return p;
}

bool das_assert_valid_id(const char* sId){
	if((sId == NULL) || (sId[0] == '\0')){
		das_error(DASERR_UTIL, "Dataset Identifiers can't be empty");
		return false;
	}
	
	if(strlen(sId) > 63){
		das_error(DASERR_UTIL, "Dataset Identifers can't be more that 63 "
			       "characters long");
		return false;
	}

	bool bGood = true;
	char c;
	size_t u = 0; 
	size_t uLen = strlen(sId);
	for(u = 0; u < uLen; ++u){
		c = sId[u];
		/* Range of 0 to z */
		if((c < 48)||(c > 122)){  bGood = false; break;}
		
		/* Range of : to @ */
		if((c > 57) && (c < 65)){ bGood = false; break;}
		
		/* Range of [ to ^ */
		if((c > 90) && (c < 95)){ bGood = false; break;}
		
		/* the ` character */
		if(c == 96){              bGood = false; break;}
		
		/* Can't start with a digit */
		if((u == 0) && ( (c > 47)&&(c < 58) )){ bGood = false; break; }
	}
	
	if(!bGood){
		das_error(DASERR_UTIL, "Illegal character '%c' in identifier '%s'", sId[u], sId);
		return false;
	}

	return true;
}


/* ************************************************************************* */
/* Version Control Info (Broken!) */

const char * das_lib_version( ) {
    const char* sRev = "$Revision$";
	 if(strcmp(sRev, "$" "Revision" "$") == 0) return "untagged";
	 else return sRev;
}

/* ************************************************************************* */
/* File utilities */
bool das_isdir(const char* path)
{
	struct stat stbuf;
	if(stat(path, &stbuf) != 0) return false;

	if(S_ISDIR(stbuf.st_mode)) return true;
	else return false;
}

bool das_isfile(const char* path)
{
	struct stat stbuf;
	if(stat(path, &stbuf) != 0) return false;

	if(S_ISREG(stbuf.st_mode)) return true;
	else return false;
}


int _wrap_strcmp(const void* vp1, const void* vp2){
	const char* p1 = (const char*)vp1;
	const char* p2 = (const char*)vp2;
	return strcmp(p1, p2);
}

int das_dirlist(
	const char* sPath, char ppDirList[][256], size_t uMaxDirs, char cType
){
	int nErrno = 0;
	DIR* pDir = NULL;
	struct dirent* pEnt = NULL;
	char sItemPath[260];

	if( (pDir = opendir(sPath)) == NULL){
		return - das_error(DASERR_UTIL, "Can't read directory %s", sPath);
	}

	size_t uEntry = 0;
	char* pNext = ppDirList[uEntry];

	errno = 0;
	while( (nErrno == 0) && (pEnt = readdir(pDir)) != NULL){
		nErrno = errno;
		if((strcmp(pEnt->d_name, ".") == 0)||(strcmp(pEnt->d_name, "..") == 0))
			continue;

		if(cType == 'd' || cType == 'f'){
			snprintf(sItemPath, 259, "%s/%s", sPath, pEnt->d_name);
			if(cType == 'd' && das_isfile(sItemPath)) continue;
			if(cType == 'f' && das_isdir(sItemPath)) continue;
		}

		if(uEntry >= uMaxDirs){
			closedir(pDir);
			return - das_error(DASERR_UTIL, "Directory contains more than %zu items", uMaxDirs);
		}

		strncpy(pNext, pEnt->d_name, 256);
		uEntry++;
		pNext = ppDirList[uEntry];

	}

	closedir(pDir);

	if((pEnt == NULL)&&(nErrno != 0))
		return - das_error(DASERR_UTIL, "Could not read all the directory entries from %s",
		                    sPath);

	/* Client can handle sorting. */
	qsort(ppDirList, uEntry, sizeof(char)*256, _wrap_strcmp);

	return uEntry;
}
