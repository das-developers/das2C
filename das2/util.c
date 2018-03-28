#define _POSIX_C_SOURCE 200112L

#include <errno.h>

#include <stdlib.h>
#include <stdio.h>
#include <stdarg.h>
#include <string.h>
#include <strings.h>
#include <sys/stat.h>
#include <dirent.h>
#include <limits.h>
#include <ctype.h>

#include "util.h"


/* ************************************************************************** */
/* Data structure creation utilities */

char* das2_strdup(const char* sIn)
{
	char* sOut = (char*)calloc(strlen(sIn)+1, sizeof(char));
	strcpy(sOut, sIn);
	return sOut;
}

/* ************************************************************************* */
/* Program Exit Utilities */

#define DAS2_ERRDIS_RET   0
#define DAS2_ERRDIS_EXIT  1
#define DAS2_ERRDIS_ABORT 43

#define DAS2_MSGDIS_STDERR 0
#define DAS2_MSGDIS_SAVE   1

int g_nErrDisposition = DAS2_ERRDIS_EXIT;

int g_nMsgDisposition = DAS2_MSGDIS_STDERR;

Das2ErrorMessage* g_msgBuf = NULL;

/* You should almost never use this, it causes partial packet output */
void das2_abort_on_error() { g_nErrDisposition = DAS2_ERRDIS_ABORT; }

void das2_exit_on_error()  { g_nErrDisposition = DAS2_ERRDIS_EXIT; }

void das2_return_on_error(){ g_nErrDisposition = DAS2_ERRDIS_RET; }

int das2_error_disposition(){ return g_nErrDisposition; }

void das2_free_msgbuf(void) {
	Das2ErrorMessage* tmp = NULL;
	if (g_msgBuf) {
		tmp = g_msgBuf;
		g_msgBuf = NULL;
		if (tmp->message) {
			free(tmp->message);
		}
		if (tmp->sFile) {
			free(tmp->sFile);
		}
		if (tmp->sFunc) {
			free(tmp->sFunc);
		}
		free(tmp);
	}
}

void das2_print_error() {
	g_nMsgDisposition = DAS2_MSGDIS_STDERR;
	das2_free_msgbuf();
}

void das2_save_error(int nMaxMsg) {
	Das2ErrorMessage* tmp = NULL;
	g_nMsgDisposition = DAS2_MSGDIS_SAVE;
	if (g_msgBuf) {
		das2_free_msgbuf();
	}
	tmp = (Das2ErrorMessage*)malloc(sizeof(Das2ErrorMessage));
	if (!tmp) return;
	tmp->nErr = DAS_OKAY;
	tmp->message = (char*)malloc(sizeof(char)*nMaxMsg);
	if (!tmp->message) {
		free(tmp);
		return;
	}
	tmp->maxmsg = nMaxMsg;
	tmp->sFile[0] = '\0';
	tmp->sFunc[0] = '\0';
	tmp->nLine = -1;

	g_msgBuf = tmp;
}

Das2ErrorMessage* das2_get_error() {
	return g_msgBuf;
}


ErrorCode das2_error_func( 
	const char* sFile, const char* sFunc, int nLine, ErrorCode nCode,
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
		if (g_msgBuf != NULL && g_msgBuf->message != NULL) {
			va_start(argp, sFmt);
			vsnprintf(g_msgBuf->message, g_msgBuf->maxmsg, sFmt, argp);
			va_end(argp);

			snprintf(g_msgBuf->sFile, sizeof(g_msgBuf->sFile), "%s", sFile);
			snprintf(g_msgBuf->sFunc, sizeof(g_msgBuf->sFunc), "%s", sFunc);
			g_msgBuf->nLine = nLine;
		}
	}
	
   
	if(g_nErrDisposition == DAS2_ERRDIS_ABORT) abort(); /* Should dump core*/
	if(g_nErrDisposition == DAS2_ERRDIS_EXIT) exit(nCode);
	
	return nCode;
}

/* ************************************************************************* */
/* String to Value utilities */

bool das2_str2double(const char* str, double* pRes){
	double rRet;
	char* endptr;
	
	if((str == NULL)||(pRes == NULL)){ return false; }
	
	errno = 0;
	
	rRet = strtod(str, &endptr); 
	
	if( (errno == ERANGE) || ((errno != 0) && (rRet == 0)) )return false;
	if(endptr == str) return false;
		
	*pRes = rRet;
	return true;
}

bool das2_str2bool(const char* str, bool* pRes)
{	
	if((str == NULL)||(strlen(str) < 1) ) return false;
	
	if(str[0] == 'T' || str[0] == '1'  || str[0] == 'Y'){
		*pRes = true;
		return true;
	}
	
	if(str[0] == 'F' || str[0] == '0'  || str[0] == 'N'){
		*pRes = false;
		return true;
	}
	
	if((strcasecmp("true", str) == 0)||(strcasecmp("yes", str) == 0)){
		*pRes = true;
		return true;
	}
	
	if((strcasecmp("false", str) == 0)||(strcasecmp("no", str) == 0)){
		*pRes = false;
		return true;
	}
	
	return false;
}

bool das2_str2int(const char* str, int* pRes)
{
	if((str == NULL)||(pRes == NULL)){ return false; }
	
	size_t i, len;
	int nBase = 10;
	long int lRet;
	char* endptr;
	len = strlen(str);
	
	/* check for hex, don't use strtol's auto-base as leading zero's cause
	   a switch to octal */
	for(i = 0; i<len; i++){
		if((str[i] != '0')&&isalnum(str[i])) break;
		
		if((str[i] == '0')&&(i<(len-1))&&
			((str[i+1] == 'x')||(str[i+1] == 'X')) ){
			nBase = 16;
			break;
		}
	}	
	
	errno = 0;
	lRet = strtol(str, &endptr, nBase);
	
	if( (errno == ERANGE) || (errno != 0 && lRet == 0) ){
		return false;
	}
	
	if(endptr == str) return false;
	
	if((lRet > INT_MAX)||(lRet < INT_MIN)) return false;
	
	*pRes = (int)lRet;
	return true;
}

bool das2_str2baseint(const char* str, int base, int* pRes)
{
	if((str == NULL)||(pRes == NULL)){ return false; }
	
	if((base < 1)||(base > 60)){return false; }
	
	long int lRet;
	char* endptr;
	
	errno = 0;
	lRet = strtol(str, &endptr, base);
	
	if( (errno == ERANGE) || (errno != 0 && lRet == 0) ){
		return false;
	}
	
	if(endptr == str) return false;
	
	if((lRet > INT_MAX)||(lRet < INT_MIN)) return false;
	
	*pRes = (int)lRet;
	return true;
}

bool das2_strn2baseint(const char* str, int nLen, int base, int* pRes)
{
	if((str == NULL)||(pRes == NULL)){ return false; }
	
	if((base < 1)||(base > 60)){return false; }
		
	/* Find the first non-whitespace character or NULL, start copy 
	   from that location up to length of remaining characters */
	int i, nOffset = 0;
	for(i = 0; i < nLen; ++i){
		if(isspace(str[i]) && (str[i] != '\0')) nOffset++;
		else break;
	}
	
	if(nOffset >= nLen) return false;  /* All space case */
	
	int nCopy = nLen - nOffset > 64 ? 64 : nLen - nOffset;
			
	char _str[68] = {'\0'};
	strncpy(_str, str + nOffset, nCopy);
	
	
	long int lRet;
	char* endptr;
	errno = 0;
	lRet = strtol(_str, &endptr, base);
	
	if( (errno == ERANGE) || (errno != 0 && lRet == 0) ){
		return false;
	}
	
	if(endptr == _str) return false;
	
	if((lRet > INT_MAX)||(lRet < INT_MIN)) return false;
	
	*pRes = (int)lRet;
	return true;
}

double * das2_csv2doubles(const char* arrayString, int* p_nitems )
{
    int i;
    int ncomma;
    const char * ipos;
    double * result;

    ncomma=0;
    for ( i=0; i< strlen(arrayString); i++ ) {
        if ( arrayString[i]==',' ) ncomma++;
    }

    *p_nitems= ncomma+1;
    result= (double*) calloc(ncomma+1, sizeof(double));

    ipos=arrayString;
    for ( i=0; i<*p_nitems; i++ ) {

        sscanf( ipos, "%lf", &result[i] );
        ipos= (char *)( index( ipos+1, (int)',' ) + 1 );
    }
    return result;
}

char * das2_doubles2csv( char * buf, const double * value, int nitems ) {
    int i;
    sprintf( buf, "%f", value[0] );
    for ( i=1; i<nitems; i++ ) {
        sprintf( &buf[ strlen( buf ) ], ", %f", value[i] );
    }
    return buf;
}

/* ************************************************************************* */
/* String utilities */

void das2_store_str(char** psDest, size_t* puLen, const char* sSrc){
	size_t uNewLen = strlen(sSrc) + 1;
	if(uNewLen > *puLen){
		if(*psDest != NULL) free(*psDest);
		*psDest = calloc(uNewLen, sizeof(char));
		*puLen = uNewLen;
	}
	strncpy(*psDest, sSrc, uNewLen);
}

char* das2_vstring(const char* fmt, va_list ap){
	
	/* Guess we need no more than 64 bytes. */
	int n, size = 64;
	char *p;
	va_list _ap;
	
	if( (p = malloc(size)) == NULL){
		das2_error(DAS2ERR_UTIL, "Can't alloc %d bytes", size);
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
			das2_error(DAS2ERR_UTIL, "Can't alloc %d bytes", size);
			return NULL;
		}
	}
	
	return p;
}

char* das2_string(const char *fmt, ...){
	
	/* Guess we need no more than 64 bytes. */
	int n, size = 64;
	char *p;
	va_list ap;

	if( (p = malloc(size)) == NULL){
		das2_error(DAS2ERR_UTIL, "Can't alloc %d bytes", size);
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
			das2_error(DAS2ERR_UTIL, "Can't alloc %d bytes", size);
			return NULL;
		}
	}
	
	return p;
}


/* ************************************************************************* */
/* Version Control Info (Broken!) */

const char * das2_lib_version( ) {
    const char* sRev = "$Revision$";
	 if(strcmp(sRev, "$" "Revision" "$") == 0) return "untagged";
	 else return sRev;
}

/* ************************************************************************* */
/* File utilities */
bool das2_isdir(const char* path)
{
	struct stat stbuf;
	if(stat(path, &stbuf) != 0) return false;
	
	if(S_ISDIR(stbuf.st_mode)) return true;
	else return false;
}

bool das2_isfile(const char* path)
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

int das2_dirlist(
	const char* sPath, char ppDirList[][NAME_MAX], size_t uMaxDirs, char cType
){
	int nErrno = 0;
	DIR* pDir = NULL;
	struct dirent* pEnt = NULL;
	char sItemPath[NAME_MAX];  /* NAME_MAX is from POSIX header: limits.h */
	
	if( (pDir = opendir(sPath)) == NULL){
		return - das2_error(DAS2ERR_UTIL, "Can't read directory %s", sPath);
	}
	
	size_t uEntry = 0;
	char* pNext = ppDirList[uEntry];
	
	errno = 0;
	while( (nErrno == 0) && (pEnt = readdir(pDir)) != NULL){
		nErrno = errno;
		if((strcmp(pEnt->d_name, ".") == 0)||(strcmp(pEnt->d_name, "..") == 0))
			continue;
		
		if(cType == 'd' || cType == 'f'){
			snprintf(sItemPath, NAME_MAX, "%s/%s", sPath, pEnt->d_name);
			if(cType == 'd' && das2_isfile(sItemPath)) continue;
			if(cType == 'f' && das2_isdir(sItemPath)) continue;
		}
		
		if(uEntry >= uMaxDirs){
			closedir(pDir);
			return - das2_error(DAS2ERR_UTIL, "Directory contains more than %zu items", uMaxDirs);
		}
		
		strncpy(pNext, pEnt->d_name, NAME_MAX);
		uEntry++;
		pNext = ppDirList[uEntry];
		
	}
	
	closedir(pDir);
	
	if((pEnt == NULL)&&(nErrno != 0))
		return - das2_error(DAS2ERR_UTIL, "Could not read all the directory entries from %s",
		                    sPath);
	
	/* Client can handle sorting. */
	qsort(ppDirList, uEntry, sizeof(char)*NAME_MAX, _wrap_strcmp);
	
	return uEntry;
}
