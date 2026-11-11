/* Copyright (C) 2015-2026 Chris Piker <chris-piker@uiowa.edu>
 *                    2004 Jeremy Faden <jeremy-faden@uiowa.edu>
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

#define _POSIX_C_SOURCE 200112L
/* #define _XOPEN_SOURCE 500 */

#include <pthread.h>
#include <errno.h>
#include <locale.h>

#include <stdlib.h>
#include <stdio.h>
#include <stdarg.h>
#include <stdint.h>
#include <string.h>

#ifndef _WIN32
#include <strings.h>
#include <dirent.h>
#else
#include "w_dirent.h"
#include <io.h>     /* _setmode, _fileno */
#include <fcntl.h>  /* _O_BINARY */
#endif

#include <sys/stat.h>
#include <limits.h>
#include <ctype.h>

#include "util.h"
#include "log.h"
#include "units.h"
#include "http.h"
#include "util_p.h"
#include "ver_p.h"
#include "array.h"   /* DASIDX_MAX */
#include "io_p.h"    /* the limits below come from where they are enforced */
#include "prop_p.h"
#include "enc_p.h"

#define _QDEF(x) #x
#define QDEF(x) _QDEF(x)


#define HOME_DIR_SZ 256
static char g_sHome[HOME_DIR_SZ] = {'\0'};
static char g_sUserDir[HOME_DIR_SZ] = {'\0'};


/* ************************************************************************** */
/* Let clients get the version of the library */

const char* daslib_version(void)
{
	/* A checkout passes its own description, see the makefile.  A release
	   tarball has none to pass and was stamped by git archive instead.  An
	   archive made where describe found no tag (a shallow CI clone) is
	   stamped with an empty string, so that counts as untagged too. */
#ifdef DAS_GIT_DESC
	return DAS_GIT_DESC;
#else
	if(DAS_GIT_ARCHIVE[0] != '$' && DAS_GIT_ARCHIVE[0] != '\0')
		return DAS_GIT_ARCHIVE;
	return "untagged";
#endif
}

/* ************************************************************************* */
/* Library limits */

/* Each value is the macro the owning module enforces, so a build that
   changes one (DASIO_MAX_PKT_SZ from the make line, say) reports it.
   Append only, names are API. */
static const DasLimit g_aLimits[] = {
	{"max_packet_bytes",      DASIO_MAX_PKT_SZ},
	{"das2_max_header_bytes", DAS2_LEN_MAX},
	{"das2_max_packet_id",    DASSTREAM_V2_ENDPKTID - 1},
	{"das3_max_packet_id",    DASSTREAM_V3_ENDPKTID - 1},
	{"max_prop_name_bytes",   DASPROP_NMAX_SZ},
	{"max_prop_value_bytes",  DASPROP_VMAX_SZ},
	{"max_dataset_rank",      DASIDX_MAX},
	{"das2_max_item_bytes",   DASENC_MAX_WIDTH}
};
#define DASLIMIT_COUNT (sizeof(g_aLimits) / sizeof(DasLimit))

const DasLimit* DasLimit_lookup(const char* sName)
{
	if(sName == NULL) return NULL;
	for(size_t u = 0; u < DASLIMIT_COUNT; ++u)
		if(strcmp(g_aLimits[u].name, sName) == 0) return g_aLimits + u;
	return NULL;
}

const DasLimit* DasLimit_next(const DasLimit* pPrev)
{
	if(pPrev == NULL) return g_aLimits;

	/* Step only from an entry of the table, a copy or a stray pointer would
	   walk off into whatever follows it */
	uintptr_t uBeg = (uintptr_t)g_aLimits;
	uintptr_t uPtr = (uintptr_t)pPrev;
	if((uPtr < uBeg)||(uPtr >= (uintptr_t)(g_aLimits + DASLIMIT_COUNT))||
	   (((uPtr - uBeg) % sizeof(DasLimit)) != 0)
	){
		daserrf(DASERR_UTIL, "Limit at %p is not from this library's table, "
		          "use DasLimit_lookup() or DasLimit_next()", (const void*)pPrev);
		return NULL;
	}

	size_t uNext = (uPtr - uBeg) / sizeof(DasLimit) + 1;
	return (uNext < DASLIMIT_COUNT) ? g_aLimits + uNext : NULL;
}

/* ************************************************************************** */
/* Unavoidable global library structure initialization */

das_errcode daslib_init(
	const char* sProgName, das_errdisp nErrDis, int nLevel,
	const DasLogHook* pHook
){
	/* The hook goes in first, so anything that goes wrong below is reported
	   where the caller asked for messages to go */
	daslog_setHook(pHook);

	/* Das2 Lib is UTF-8 internally, so set to an appropriate codepage */
#ifdef LOCALE
	setlocale(LC_ALL, QDEF(LOCALE));
#else
	setlocale(LC_ALL, ""); /* <-- sets the local based on env vars */
#endif

#ifdef _WIN32
	_dasstr_resetCLocale();  /* dasstr_toDoubleCLocale's C locale, str.c */
#endif

	/* With no valid disposition to follow, take the default one */
	if((nErrDis != DASERRDISP_EXIT) && (nErrDis != DASERRDISP_RET)){
		daserr_setDisp(DASERRDISP_EXIT);
		return daserrf(DASERR_INIT, "(%s) daslib_init: Invalid error disposition %d",
		                 sProgName, (int)nErrDis);
	}
	daserr_setDisp(nErrDis);

	if((nLevel < DASLOG_TRACE) || (nLevel > DASLOG_CRITICAL))
		return daserrf(DASERR_INIT, "(%s) daslib_init: Invalid log level %d",
		                 sProgName, nLevel);

	if((nErrDis == DASERRDISP_RET) && (nLevel > DASLOG_ERROR))
		return daserrf(DASERR_INIT, "(%s) daslib_init: Log level %s with return on "
		                 "errors would hide every error message", sProgName,
		                 daslog_levelToStr(nLevel));

	daslog_setLevel(nLevel);

	if( ! das_units_init(sProgName) )
		return daserrf(DASERR_INIT, "(%s) Failed units initialization", sProgName);

/* FFTW support enabled/disable see defs.h) */
#ifndef DAS_NO_DFT
	if( ! das_dft_init(sProgName) )
		return daserrf(DASERR_INIT, "(%s) Failed DFT initialization", sProgName);
#endif

/* network support enabled/disable see defs.h) */
#ifndef DASCONF_NO_NET
	if( ! das_http_init(sProgName))
		return daserrf(DASERR_INIT, "(%s) Failed HTTP initialization", sProgName);
#endif
	
	if( ! das_tt2K_init(sProgName))
		return daserrf(DASERR_INIT, "(%s) Failed TT2000 leap second "
		                 "initialization", sProgName);
	
	
	/* Save off the current account's home directory.  If a home directory
	 * is not available return some system directory that is likely writable */
#ifdef _WIN32
	if(getenv("USERPROFILE"))
		strncpy(g_sHome, getenv("USERPROFILE"), HOME_DIR_SZ - 1);
	else
		strcpy(g_sHome, "C:\\");
#else
	if(getenv("HOME"))
		strncpy(g_sHome, getenv("HOME"), HOME_DIR_SZ - 1);
	else
		strcpy(g_sHome, "/tmp");
#endif

	/* Everything das keeps for a user lives under one directory, ~/.das on
	 * every OS the way ssh and git do it, unless DAS_USERDIR says otherwise.
	 * Read once here so a setenv() later in the process cannot move the
	 * cache or the credentials file out from under an open session.  A
	 * program that wants to choose calls dasfs_setUserDir(). */
	dasfs_setUserDir(NULL);
	return DAS_OKAY;
}

void daslib_finish(){
	/* A do nothing function on Unix, closes network sockets on windows */
#ifndef DASCONF_NO_NET
	das_http_finish();
#endif
}


/* ************************************************************************* */
/* A memset that handles multi-byte items  */
               
/* Use memcpy because the amount of data written in each call goes up
 * exponentially and memcpy is freaking fast, much faster than a linear
 * write loop for large arrays.
 */
uint8_t* das_memset(
	uint8_t* pDest, const uint8_t* pSrc, size_t uElemSz, size_t uCount
){
	if(uCount == 0) return pDest;  /* Successfully did nothing */
	if(uElemSz == 0){ 
		daserrf(DASERR_UTIL, "Invalid element size");
		return NULL;
	}
	if(pDest == NULL){ 
		daserrf(DASERR_UTIL, "Invalid destination");
		return NULL;
	}
	if(pSrc == NULL){ 
		daserrf(DASERR_UTIL, "Invalid source");
		return NULL;
	}
	
	size_t uDone = 0, uWrite = 0;		
	
	memcpy(pDest, pSrc, uElemSz);
	uDone = 1;
	
	while(uDone < uCount){
		
		if(uDone > (uCount - uDone))  
			uWrite = uCount - uDone;
		else
			uWrite = uDone;	
		
		/* write from ourselves so that the amount of data written each time 
		   goes as the square of the number of loops */
		memcpy(pDest + uDone*uElemSz, pDest, uElemSz*uWrite);
		
		uDone += uWrite;
	}
	
	return pDest;	
}


/* ************************************************************************* */
/* File utilities */
bool dasfs_isDir(const char* path)
{
	struct stat stbuf;
	if(stat(path, &stbuf) != 0) return false;

	if(S_ISDIR(stbuf.st_mode)) return true;
	else return false;
}

const char* dasfs_userHome(void)
{
	return g_sHome;
}

const char* dasfs_userDir(void)
{
	return g_sUserDir;
}

void dasfs_setUserDir(const char* sDir)
{
	if((sDir == NULL)||(sDir[0] == '\0')) sDir = getenv("DAS_USERDIR");
	if((sDir != NULL) && (sDir[0] != '\0')){
		strncpy(g_sUserDir, sDir, HOME_DIR_SZ - 1);
		g_sUserDir[HOME_DIR_SZ - 1] = '\0';
	}
	else{
		snprintf(g_sUserDir, HOME_DIR_SZ, "%s" DASFS_SEPS ".das", g_sHome);
	}
}

bool dasfs_isFile(const char* path)
{
	struct stat stbuf;
	if(stat(path, &stbuf) != 0) return false;

	if(S_ISREG(stbuf.st_mode)) return true;
	else return false;
}

int dasfs_rename(const char* sFrom, const char* sTo)
{
#ifdef _WIN32
	if(MoveFileExA(sFrom, sTo, MOVEFILE_REPLACE_EXISTING)) return 0;
	/* The two causes a caller is likely to act on, the rest is EIO */
	switch(GetLastError()){
	case ERROR_FILE_NOT_FOUND: case ERROR_PATH_NOT_FOUND: errno = ENOENT; break;
	case ERROR_ACCESS_DENIED: case ERROR_SHARING_VIOLATION: errno = EACCES; break;
	default: errno = EIO;
	}
	return -1;
#else
	return rename(sFrom, sTo);
#endif
}

void dasfs_setStdioBinary(FILE* pFile)
{
#ifdef _WIN32
	if(pFile != NULL) _setmode(_fileno(pFile), _O_BINARY);
#else
	(void)pFile;
#endif
}

/* Length of a leading drive spec, such as "C:", that's never made as a
   directory.  stat("C:") doesn't report a directory, so mkdir would be
   tried and fail. */
static size_t _das_drivelen(const char* sPath)
{
#ifdef WIN32
	if((sPath[0] != '\0') && (sPath[1] == ':')) return 2;
#else
	(void)sPath;
#endif
	return 0;
}

#ifndef WIN32
bool dasfs_copyFile(const char* src, const char* dest, mode_t mode)
#else
bool dasfs_copyFile(const char* src, const char* dest)
#endif
{
	
	/* See if you can read the old file first */
	FILE* pIn = fopen(src, "rb");
	if(pIn == NULL){
		daserrf(DASERR_UTIL, "Can not read source file %s.", src);
		return false;
	}
	
	/* Make directories to output file */
	char sPath[256] = {'\0'};
	size_t u, uLen;
	int nErr;
	
#ifndef WIN32
	mode_t dirmode;
	dirmode = mode | S_IRWXU;
	if(dirmode & S_IRGRP) dirmode |= S_IXGRP;
	if(dirmode & S_IROTH) dirmode |= S_IXOTH;
#endif

	if((src == NULL)||(src[0] == '\0')){
		daserrf(DASERR_UTIL, "src is NULL or empty");
		fclose(pIn);
		return false;
	}
	if((dest == NULL)||(dest[0] == '\0')){
		daserrf(DASERR_UTIL, "dest is NULL or empty");
		fclose(pIn);
		return false;
	}
	
	/* Get all the parent directories */
	strncpy(sPath, dest, 255);
	
	uLen = strlen(sPath);
	size_t uDrive = _das_drivelen(sPath);

	for(u = 0; u<uLen; u++){
		if(((sPath[u] == '/')||(sPath[u] == DASFS_SEPC)) && (u>uDrive) && (u < uLen-1)){
			char cSep = sPath[u];
			sPath[u] = '\0';
			
			if(! dasfs_isDir(sPath) ){
#ifndef WIN32
				if(mkdir(sPath, dirmode) != 0)
#else
				if(mkdir(sPath) != 0)
#endif
				{
					nErr = errno;
					fclose(pIn);
					daserrf(
						DASERR_UTIL, "Cannot make directory '%s' because '%s'.",
					 	sPath, strerror(nErr)
					);
					return false;
				}
			}
			
			sPath[u] = cSep;
		}
	}
	
	/* Make the file */
	FILE* pOut = fopen(dest, "wb");
	if(pOut == NULL){
		daserrf(DASERR_UTIL, "Can not create output file '%s'", dest);
		fclose(pIn);
		return false;
	}
	
	/* copy the bytes */
	char buffer[65536];
	size_t uRead = 0;
	while( (uRead = fread(buffer, sizeof(char), 65536, pIn)) > 0){
		if(uRead != fwrite(buffer, sizeof(char), uRead, pOut)){
			daserrf(DASERR_UTIL, "Error writing to file '%s'", dest);
			fclose(pIn);
			fclose(pOut);
			return false;
		} 
	}

	fclose(pIn);
	fclose(pOut);
	
	/* Set the mode */
#ifndef WIN32
	chmod(dest, mode);
#endif
	
	return true;	
}

das_errcode dasfs_mkdirsTo(const char* path)
{
	/* Walk the set of items, make directories for everything upto the last */

	/* Make directories to output file */
	char sPath[256] = {'\0'};
	strncpy(sPath, path, 255);

#ifndef WIN32
	mode_t dirmode = S_IRUSR|S_IWUSR|S_IRWXU|S_IRGRP|S_IWGRP|S_IXGRP|S_IROTH|S_IXOTH;
#endif

	size_t uLen = strlen(sPath);
	size_t uDrive = _das_drivelen(sPath);
	int nErr;

	for(size_t u = 0; u < uLen; u++){
		if(((sPath[u] == '/')||(sPath[u] == DASFS_SEPC)) && (u>uDrive) && (u < uLen-1)){
			char cSep = sPath[u];
			sPath[u] = '\0';
			
			if(! dasfs_isDir(sPath) ){
#ifndef WIN32
				if(mkdir(sPath, dirmode) != 0)
#else
				if(mkdir(sPath) != 0)
#endif
				{
					nErr = errno;
					return daserrf(
						DASERR_UTIL, "Cannot make directory '%s' because '%s'.",
					 	sPath, strerror(nErr)
					);
				}
			}
			
			sPath[u] = cSep;
		}
	}
	return DAS_OKAY;
}


static int _wrap_strcmp(const void* vp1, const void* vp2){
	const char* p1 = (const char*)vp1;
	const char* p2 = (const char*)vp2;
	return strcmp(p1, p2);
}

int dasfs_listDir(
	const char* sPath, char ppDirList[][256], size_t uMaxDirs, char cType
){
	int nErrno = 0;
	DIR* pDir = NULL;
	struct dirent* pEnt = NULL;
	char sItemPath[260];

	if( (pDir = opendir(sPath)) == NULL){
		return - daserrf(DASERR_UTIL, "Can't read directory %s", sPath);
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
			if(cType == 'd' && dasfs_isFile(sItemPath)) continue;
			if(cType == 'f' && dasfs_isDir(sItemPath)) continue;
		}

		if(uEntry >= uMaxDirs){
			closedir(pDir);
			return - daserrf(DASERR_UTIL, "Directory contains more than %zu items", uMaxDirs);
		}

		strncpy(pNext, pEnt->d_name, 256);
		uEntry++;
		pNext = ppDirList[uEntry];

	}

	closedir(pDir);

	if((pEnt == NULL)&&(nErrno != 0))
		return - daserrf(DASERR_UTIL, "Could not read all the directory entries from %s",
		                    sPath);

	/* Client can handle sorting. */
	qsort(ppDirList, uEntry, sizeof(char)*256, _wrap_strcmp);

	return uEntry;
}

/* ************************************************************************* */
/* Command line helps */

/* See a given string matches the short or long form of an argument */
bool dasarg_isOpt(
	const char* sArg, const char* sShort, const char* sLong, bool* pLong 
){
	if(strcmp(sArg, sShort) == 0){
		if(pLong) *pLong = false;
		return true;
	}
	size_t uLen = strlen(sLong);  /* Include the '=' */

	if(strncmp(sArg, sLong, uLen) == 0){
		if(pLong) *pLong = true;
		return true;
	}
	return false;
}

/* Get the value of an argument if the current entry in argv matches
 * either the short or long form */

bool dasarg_getOptVal(
	char** argv, int argc, const char* sShort, const char* sLong, int* pArgIdx, 
	char* sDest, size_t uDest
){
	bool bIsLong = false;

	if(! dasarg_isOpt(argv[*pArgIdx], sShort, sLong, &bIsLong))
		return false;
	
	const char* sVal;
	if(bIsLong){
		size_t uLongSz = strlen(sLong);
		if(argv[*pArgIdx][uLongSz] == '\0') /* Nothing in str after prefix */
			goto NO_ARG;

		sVal = argv[*pArgIdx] + uLongSz;
	}
	else{
		if(*pArgIdx > argc-2) /* Ain't no subsequent arg */
			goto NO_ARG;

		sVal = argv[*pArgIdx+1];
		++(*pArgIdx);
	}

	strncpy(sDest, sVal, uDest - 1);
	return true;
	
NO_ARG:
	daserrf(DASERR_UTIL, "Missing option after argument %s", argv[*pArgIdx]);
	return false;
}

/* ************************************************************************* */
/* Strings that name places: URLs, files and catalog URIs */

bool dasurl_hasScheme(const char* sPath)
{
	if((sPath == NULL) || !isalpha((unsigned char)sPath[0])) return false;
	const char* p = sPath + 1;
	while(isalnum((unsigned char)*p) || (*p == '+') || (*p == '-') || (*p == '.')) ++p;
	/* A single letter before the colon is a Windows drive, not a scheme */
	return (*p == ':') && (p > sPath + 1);
}

/* ************************************************************************* */
/* Splitting URLs in place */

#define _URL_ABSENT 0xFFFF
#define _URL_MAX    65534

/* Case-blind compare of a counted scheme, without strncasecmp for Windows */
static bool _url_ieq(const char* pIn, size_t uLen, const char* sLower)
{
	if(strlen(sLower) != uLen) return false;
	for(size_t u = 0; u < uLen; ++u)
		if(tolower((unsigned char)pIn[u]) != sLower[u]) return false;
	return true;
}

static void _url_set(DasUrlParts* pParts, das_urlpart ePart, size_t uOff, size_t uLen)
{
	pParts->aOff[ePart] = (uint16_t)uOff;
	pParts->aLen[ePart] = (uint16_t)uLen;
}

/* Hand rolled on purpose: uriparser does not take UTF-8 natively and curl is
 * a large dependency to carry for a handful of fields.  Only the split is
 * done here, decoding and scheme rules belong to the caller. */
bool DasUrlParts_fromStr(DasUrlParts* pParts, const char* sUrl)
{
	for(int i = 0; i < 12; ++i){
		pParts->aOff[i] = _URL_ABSENT;
		pParts->aLen[i] = 0;
	}
	if(sUrl == NULL) return false;

	size_t uLen = strlen(sUrl);
	if(uLen > _URL_MAX) return false;

	/* A space or control character would end an HTTP request line early and
	   let the rest of the URL be read as request headers */
	for(size_t u = 0; u < uLen; ++u){
		unsigned char c = (unsigned char)sUrl[u];
		if((c <= 0x20)||(c == 0x7F)) return false;
	}

	/* Scheme, RFC 3986 section 3.1 */
	size_t u = 0;
	if(!isalpha((unsigned char)sUrl[0])) return false;
	while((u < uLen) && (isalnum((unsigned char)sUrl[u]) || strchr("+-.", sUrl[u])))
		++u;
	if((u == uLen)||(sUrl[u] != ':')) return false;
	_url_set(pParts, DASURLPART_SCHEME, 0, u);
	++u;

	/* Authority: [userinfo@]host[:port] */
	if((uLen - u >= 2) && (sUrl[u] == '/') && (sUrl[u+1] == '/')){
		u += 2;
		size_t uAuthBeg = u;
		size_t uAuthEnd = u;
		while((uAuthEnd < uLen) && (strchr("/?#", sUrl[uAuthEnd]) == NULL)) ++uAuthEnd;

		/* The last '@' ends the userinfo, in case a password holds one */
		size_t uHostBeg = uAuthBeg;
		for(size_t v = uAuthBeg; v < uAuthEnd; ++v) if(sUrl[v] == '@') uHostBeg = v + 1;
		if(uHostBeg > uAuthBeg)
			_url_set(pParts, DASURLPART_USERINFO, uAuthBeg, uHostBeg - uAuthBeg - 1);

		size_t uHostEnd = uHostBeg;
		if((uHostEnd < uAuthEnd) && (sUrl[uHostEnd] == '[')){   /* IPv6 literal */
			while((uHostEnd < uAuthEnd) && (sUrl[uHostEnd] != ']')) ++uHostEnd;
			if(uHostEnd == uAuthEnd) return false;
			++uHostEnd;
		}
		else
			while((uHostEnd < uAuthEnd) && (sUrl[uHostEnd] != ':')) ++uHostEnd;
		_url_set(pParts, DASURLPART_HOST, uHostBeg, uHostEnd - uHostBeg);

		if((uHostEnd < uAuthEnd) && (sUrl[uHostEnd] == ':')){
			size_t uPortBeg = uHostEnd + 1;
			if(uAuthEnd - uPortBeg > 5) return false;
			long nPort = 0;
			for(size_t v = uPortBeg; v < uAuthEnd; ++v){
				if(!isdigit((unsigned char)sUrl[v])) return false;
				nPort = nPort*10 + (sUrl[v] - '0');
			}
			if(uAuthEnd > uPortBeg){             /* "host:" is allowed, no port */
				if((nPort < 1)||(nPort > 65535)) return false;
				_url_set(pParts, DASURLPART_PORT, uPortBeg, uAuthEnd - uPortBeg);
			}
		}
		else if(uHostEnd != uAuthEnd) return false;   /* junk after an IPv6 literal */
		u = uAuthEnd;
	}

	/* Path, then query, then fragment */
	size_t uPathEnd = u;
	while((uPathEnd < uLen) && (sUrl[uPathEnd] != '?') && (sUrl[uPathEnd] != '#')) ++uPathEnd;
	_url_set(pParts, DASURLPART_PATH, u, uPathEnd - u);
	u = uPathEnd;

	if((u < uLen) && (sUrl[u] == '?')){
		size_t uQryBeg = u + 1;
		size_t uQryEnd = uQryBeg;
		while((uQryEnd < uLen) && (sUrl[uQryEnd] != '#')) ++uQryEnd;
		_url_set(pParts, DASURLPART_QUERY, uQryBeg, uQryEnd - uQryBeg);

		/* das2.2 dataset=, a whole parameter, not a suffix of another name */
		for(size_t v = uQryBeg; v + 8 <= uQryEnd; ++v){
			if(((v == uQryBeg)||(sUrl[v-1] == '&')) && (strncmp(sUrl + v, "dataset=", 8) == 0)){
				size_t uValEnd = v + 8;
				while((uValEnd < uQryEnd) && (sUrl[uValEnd] != '&')) ++uValEnd;
				_url_set(pParts, DASURLPART_DATASET, v + 8, uValEnd - (v + 8));
				break;
			}
		}
		u = uQryEnd;
	}

	if((u < uLen) && (sUrl[u] == '#'))
		_url_set(pParts, DASURLPART_FRAGMENT, u + 1, uLen - (u + 1));

	return true;
}

const char* DasUrlParts_get(
	const DasUrlParts* pParts, const char* sUrl, das_urlpart ePart, size_t* pLen
){
	*pLen = 0;
	if((sUrl == NULL)||(pParts == NULL)||(ePart < DASURLPART_SCHEME)||(ePart > DASURLPART_DATASET))
		return NULL;

	if(pParts->aOff[ePart] != _URL_ABSENT){
		*pLen = pParts->aLen[ePart];
		return sUrl + pParts->aOff[ePart];
	}

	/* A missing port is the scheme's default, for the schemes we speak */
	if((ePart == DASURLPART_PORT) && (pParts->aOff[DASURLPART_SCHEME] != _URL_ABSENT)){
		size_t uScheme = pParts->aLen[DASURLPART_SCHEME];
		if(_url_ieq(sUrl, uScheme, "http")) { *pLen = 2; return "80"; }
		if(_url_ieq(sUrl, uScheme, "https")){ *pLen = 3; return "443"; }
	}
	return NULL;
}

