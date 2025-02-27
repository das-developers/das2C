/* Copyright (C) 2017-2019 Chris Piker <chris-piker@uiowa.edu>
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
 #define _XOPEN_SOURCE 600  /* Trying to get pthread_mutexattr_settype */

#include <pthread.h>
#include <ctype.h>
#include <errno.h>
#include <string.h>

#ifdef __APPLE__
#define _DARWIN_C_SOURCE
#endif

#include <sys/types.h>

#ifndef _WIN32
#include <unistd.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <netdb.h>
#include <time.h>
#else
#include <winsock2.h>
#include <ws2tcpip.h>
#define gai_strerror gai_strerrorA
#endif

#include <assert.h>
#include <openssl/ssl.h>
#include <openssl/err.h>

#include "util.h"
#include "http.h"
#include "buffer.h"
#include "log.h"

#ifdef _WIN32
typedef ptrdiff_t ssize_t;
#endif

/* HTML5, URL Encode table, may need this for form data
char html5[256] = {
   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,
   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,
  43,   0,   0,   0,   0,   0,   0,   0,   0,   0,  42,   0,   0,  45,  46,   0,
  48,  49,  50,  51,  52,  53,  54,  55,  56,  57,   0,   0,   0,   0,   0,   0,
   0,  65,  66,  67,  68,  69,  70,  71,  72,  73,  74,  75,  76,  77,  78,  79,
  80,  81,  82,  83,  84,  85,  86,  87,  88,  89,  90,   0,   0,   0,   0,  95,
   0,  97,  98,  99, 100, 101, 102, 103, 104, 105, 106, 107, 108, 109, 110, 111,
 112, 113, 114, 115, 116, 117, 118, 119, 120, 121, 122,   0,   0,   0,   0,   0,
   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,
   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,
   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,
   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,
   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,
   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,
   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,
   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0
};
 */

#define LIBDAS2_USER_AGENT "libdas2/2.3"

#define HTTP_OK        200
#define HTTP_MovedPerm 301
#define HTTP_Found     302
#define HTTP_TempRedir 307   /* Treat as 307 */
#define HTTP_PermRedir 308   /* Treat as 301 */
#define HTTP_BadReq    400
#define HTTP_AuthReq   401
#define HTTP_Forbidden 403
#define HTTP_NotFound  404
#define HTTP_Error     500

#define _QDEF(x) #x
#define QDEF(x) _QDEF(x)

/* ************************************************************************* */
/* The global SSL context and mutexes for manipulating it.  We use lazy
 * initialization of SSL so that programs that don't need it don't have to
 * deal with the overhead.
 */

pthread_mutex_t g_mtxHttp;
SSL_CTX* g_pSslCtx = NULL;

pthread_mutex_t g_mtxAddrArys;
DasAry* g_pHostAry = NULL;
DasAry* g_pAddrAry = NULL;


bool das_http_init(const char* sProgName){

	pthread_mutexattr_t attr;
	pthread_mutexattr_init(&attr);
#ifndef NDEBUG
	pthread_mutexattr_settype(&attr, PTHREAD_MUTEX_ERRORCHECK);
#endif
    
    /* Init SSL context and address info and mutexes */
	if( pthread_mutex_init(&g_mtxHttp, &attr) != 0) return false;
	if( pthread_mutex_init(&g_mtxAddrArys, &attr) != 0) return false;

	g_pSslCtx = NULL;  /* Don't alter or use this without owning the mutext */
   
	void* fill = NULL;  /* the fill value *is* the void ptr, not it's value */
    
	/* After this point, don't alter or use this without owning the mutext */
	g_pAddrAry = new_DasAry(
		"addr_pointers", vtUnknown, sizeof(ubyte*), (const ubyte*) &fill, 
		RANK_1(0), UNIT_DIMENSIONLESS
	);
	
	/* Setup a host name array as well, this as ragged in both dimensions */
	g_pHostAry = new_DasAry(
		"host_names", vtUByte, 0, NULL, RANK_2(0,0), UNIT_DIMENSIONLESS
	);
	
	/* Let customers know we're storing null terminated strings as the 
	   fastest moving dimension */	
	DasAry_setUsage(g_pHostAry, D2ARY_AS_STRING);	

#ifdef _WIN32
	WORD wVersionRequested = MAKEWORD(2,2);
	WSADATA wsaData;
	int nErr = WSAStartup(wVersionRequested, &wsaData);
	if(nErr != 0){
		das_error(DASERR_INIT, "Windows Socket startup failed with error: %d\n", nErr);
		return false;
	}
#endif		
	
	return true;
}

/* Clean up address arrays, closes network socket facility on windows */
void das_http_finish()
{
	if(g_pHostAry) dec_DasAry(g_pHostAry);
	
	if(g_pAddrAry){
		size_t uAddrs = DasAry_lengthIn(g_pAddrAry, DIM0);
		
		struct addrinfo* pAddr = NULL;
		
		for(size_t u = 0; u < uAddrs; ++u){
			pAddr = *((struct addrinfo**) DasAry_getAt(g_pAddrAry, vtUnknown, IDX0(u)));
			freeaddrinfo(pAddr);
		}
	
		dec_DasAry(g_pAddrAry);
	}
	
#ifdef _WIN32
	WSACleanup();
#endif
}

bool das_http_setup_ssl(){
	if(g_pSslCtx != NULL) return true;

	pthread_mutex_lock(&g_mtxHttp);

	daslog_debug("Setting up SSL context");

	/* Now check a second time, ctx could have been setup while we were
	 * waiting */
	if(g_pSslCtx != NULL){
		pthread_mutex_unlock(&g_mtxHttp);
		return true;
	}
	OpenSSL_add_all_algorithms();
	SSL_library_init();
	/* ERR_load_BIO_strings(); No longer needed, BIO strings loaded automatically since 1.1.0 */
	ERR_load_crypto_strings();
	SSL_load_error_strings();

	const SSL_METHOD* pMeth = TLS_client_method();
	g_pSslCtx = SSL_CTX_new(pMeth);
	if(g_pSslCtx == NULL){
		/* have to use not thread locking error report here */
		ERR_print_errors_fp(stderr);
		pthread_mutex_unlock(&g_mtxHttp);
		return false;
	}

	pthread_mutex_unlock(&g_mtxHttp);
	return true;
}

/* Use under mutex lock, dig all the global errors out of the open SSL
 * library and put these into a newly allocated string */
char* _http_get_ssl_global_errors(){
	unsigned long uErr;
	const char* aErrs[34] = {NULL};
	aErrs[32] = "Maximum number of OpenSSL errors (32) encountered";
	int iErr = 0;
	while( (uErr = ERR_get_error()) != 0 ){
		if(iErr < 32)
			aErrs[iErr] = ERR_error_string(uErr, NULL);

		++iErr;
	}

	size_t uLen = 0; iErr = 0;
	while(aErrs[iErr] != NULL){uLen += strlen(aErrs[iErr]); ++iErr;}
	char* sBuf = (char*)calloc(uLen + 1 + iErr, sizeof(char));
	char* pWrite = sBuf;

	iErr = 0;
	while(aErrs[iErr] != NULL){
		uLen = strlen(aErrs[iErr]);     /* Warning: variable re-purpose */
		strncpy(pWrite, aErrs[iErr], uLen);
		++iErr;
		pWrite += uLen;
		*pWrite = '\n'; ++pWrite;
	}

	return sBuf;
}

char* das_ssl_getErr(const void* vpSsl, int nRet)
{
	if(nRet == 0) return NULL;

	const SSL* pSsl = (const SSL*)vpSsl;

	switch(SSL_get_error(pSsl, nRet)){
	case SSL_ERROR_ZERO_RETURN: return das_strdup("Connection closed");
	case SSL_ERROR_WANT_READ:
	case SSL_ERROR_WANT_WRITE:
		return das_strdup("Negotiation requested, initialize with "
			               "SSL_MODE_AUTO_RETRY  to avoid.");
	case SSL_ERROR_WANT_CONNECT:
	case SSL_ERROR_WANT_ACCEPT:
		return das_strdup("Connection not established.");

	case SSL_ERROR_WANT_X509_LOOKUP:
		return das_strdup("Cert change requested, callback given to "
				 "SSL_CTX_set_client_cert_cb() must be called again");

	case SSL_ERROR_SYSCALL:
		return das_strdup("Low level Socket I/O error, consult errno");

	case SSL_ERROR_SSL:
		return _http_get_ssl_global_errors();

	default:
		return das_strdup("WTF?");
	}
}

/* ************************************************************************* */
/* Parsing a URL */

bool DasHttpResp_init(DasHttpResp* pRes, const char* sUrl)
{
	struct das_url* pUrl = &(pRes->url);
	memset(pUrl, 0, sizeof(struct das_url));

	pRes->nSockFd = -1;
	pRes->nCode = -1;
	pRes->url.sPort[0] = '8'; pRes->url.sPort[1] = '0';

	/* Get the scheme, this is a PITA but I don't want a large library
	 * dependency and uriparser doesn't want to deal with utf-8 natively.
	 * I'm sure curl would take care of it nicely, but does that exist on
	 * windows? */
	char* pOut = pUrl->sScheme;
	const char* pIn = sUrl;
	while((*pIn != '\0')&&(*pIn != ':')&&((pOut - pUrl->sScheme) <= DASURL_SZ_SCHEME )){
		*pOut = *pIn; ++pOut; ++pIn;
	}
	if((strcmp("http", pUrl->sScheme)!=0 )&&(strcmp("https", pUrl->sScheme)!=0)){
		pRes->sError = das_string("Unknown scheme, %s", pUrl->sScheme);
		return false;
	}
	if(strcmp("https", pUrl->sScheme) == 0){
		pUrl->sPort[0] = '4'; pUrl->sPort[1] = '4'; pUrl->sPort[2] = '3';
	}
	else{
		pUrl->sPort[0] = '8'; pUrl->sPort[1] = '0';
	}

	while(*pIn == ':') pIn++;
	while(*pIn == '/') pIn++;  /* Advance past a couple /'s or four */

	pOut = pUrl->sHost;
	while( (*pIn != '\0')&&(*pIn != ':')&&(*pIn != '/')&&(*pIn != '?')&&
			 ((pOut - pUrl->sHost) <= DASURL_SZ_HOST)){
		*pOut = *pIn; ++pOut; ++pIn;
	}
	if((strlen(pUrl->sHost) < 1)||((pOut - pUrl->sHost) == (DASURL_SZ_HOST))){
		pRes->sError = das_string("Invalid host in URL %s", sUrl);
		return false;
	}

	char sPort[32] = {'\0'};
	pOut = sPort;
	int nPort;
	if(*pIn == ':'){
		while( (*pIn != '\0')&&(*pIn != '/')&&((pOut - sPort) < 32) ){
			*pOut = *pIn; ++pOut; ++pIn;
		}
		nPort = 0;
		if((strlen(sPort) < 1) || ((nPort = atoi(sPort)) == 0) ||
			(nPort > 65536) ){
			pRes->sError = das_string("Invalid port in URL %s", sUrl);
			return false;
		}
		strncpy(pUrl->sPort, sPort, 7);
	}

	/* don't skip the / it's part of the path */

	/* Skipping fragments for now */
	pOut = pUrl->sPath;  /* Note this can be zero */
	while( (*pIn != '\0')&&(*pIn != '?')&&((pOut - pUrl->sPath)<DASURL_SZ_PATH)){
		*pOut = *pIn; ++pOut; ++pIn;
	}
	if(*pIn == '?') ++pIn;

	pOut = pUrl->sQuery;
	while((*pIn != '\0')&&((pOut - pUrl->sQuery)<DASURL_SZ_QUERY)){
		*pOut = *pIn; ++pOut; ++pIn;
	}
	/* Das2 special check, see if the path contains "dataset="*/
	pIn = NULL;
	if( (pIn = strstr(pUrl->sQuery, "dataset=")) != NULL){
		pOut = pUrl->sDataset;
		while((*pIn != '\0')&&(*pIn != '=')) ++pIn;
		if(*pIn == '='){
			++pIn;
			while ((*pIn != '\0')&&(*pIn != '&')){ *pOut = *pIn; ++pOut; ++pIn;}
		}
	}
	return true;
}

bool das_url_toStr(const struct das_url* pUrl, char* sBuf, size_t uLen)
{
	if((pUrl == NULL)||(uLen < 12)) return false;
	if(pUrl->sScheme[0] == '\0') return false;
	if(pUrl->sHost[0] == '\0')   return false;

	--uLen;  /* Always leave rume for the null */

	const char* sPort = "";
	if(strcmp(pUrl->sScheme, "http") == 0){
		if(strcmp(pUrl->sPort, "80") != 0) sPort = pUrl->sPort;
	}
	else{
		if(strcmp(pUrl->sScheme, "https") == 0){
			if(strcmp(pUrl->sPort, "443") != 0) sPort = pUrl->sPort;
		}
		else{
			sPort = pUrl->sPort;
		}
	}

	int nChunk;
	nChunk = snprintf(sBuf, uLen, "%s://%s%s",pUrl->sScheme,pUrl->sHost,sPort);
	if(nChunk >= uLen) return false;
	uLen -= nChunk;
	sBuf += nChunk;

	if(pUrl->sPath[0] != '\0'){
		nChunk = snprintf(sBuf, uLen, "%s", pUrl->sPath);
		if(nChunk >= uLen) return false;
		uLen -= nChunk;
		sBuf += nChunk;
	}

	if( pUrl->sQuery[0] != '\0'){
		snprintf(sBuf, uLen, "?%s", pUrl->sQuery);
		if(nChunk >= uLen) return false;
		uLen -= nChunk;
		sBuf += nChunk;
	}

	return true;
}

/* ************************************************************************** */
/* HTTP Client Functions */

void DasHttpResp_clear(DasHttpResp* pRes)
{
	memset(pRes, 0, sizeof(DasHttpResp));
	pRes->nSockFd = -1;
	pRes->nCode = -1;
	pRes->url.sPort[0] = '8'; pRes->url.sPort[1] = '0';
}

bool DasHttpResp_useSsl(DasHttpResp* pRes){ return (pRes->pSsl != NULL); }



/* Getting address info is expensive, and can fail. So:
   1. Loop with multiple tries
   2. Cache the results 
*/
struct addrinfo* _das_http_getsrvaddr(DasHttpResp* pRes)
{
	struct das_url* pUrl = &(pRes->url);
	
	struct addrinfo hints;
	struct addrinfo* pAddr = NULL;
	char sHostAndPort[DASURL_SZ_HOST + DASURL_SZ_PORT + 2];
	snprintf(sHostAndPort, DASURL_SZ_HOST + DASURL_SZ_PORT + 2, "%s:%s",
		pUrl->sHost, pUrl->sPort
	);
	
	/* First see if I already have the address info I need in the cache */
	pthread_mutex_lock(&g_mtxAddrArys);
	
	size_t uStrLen, uHosts = DasAry_lengthIn(g_pHostAry, DIM0);
	const char* sName = NULL;
	for(size_t u = 0; u < uHosts; ++u){
		sName = DasAry_getCharsIn(g_pHostAry, DIM1_AT(u), &uStrLen);
		
		if(strcmp(sHostAndPort, sName) == 0){
			pAddr = *((struct addrinfo**) DasAry_getAt(g_pAddrAry, vtUnknown, IDX0(u)));
			break;
		}
	}
	
	pthread_mutex_unlock(&g_mtxAddrArys);
	
	if(pAddr != NULL) return pAddr;   /* Yay, no DOSing the DNS today! */
	
	memset(&hints, 0, sizeof(struct addrinfo));
	/* hints.ai_family = AF_UNSPEC; */
	hints.ai_family = AF_INET;       /* <-- Try for IPv4 first */
	hints.ai_socktype = SOCK_STREAM;
	
	/* Address lookups trigger a blizzard of calls, see this article for more:
	   
		 https://jameshfisher.com/2018/02/03/what-does-getaddrinfo-do/
		 
		so they can fail in odd bizare ways.  Do the lookup in a loop and back
		off the time.  Once you get to a 1 second backoff start printing to
		the log files. 
	*/

	int nLoops = 0;
#ifndef _WIN32	
	struct timespec wait = {0, 0};
	struct timespec remaining = {0, 0};
#else
	int nMilli = 0;
#endif
	
	int nErrno = 0;
	int nErr = 0;
	
	bool bStop = false;
	while(!bStop){
	
		errno = 0;
		nErr = getaddrinfo(pUrl->sHost, pUrl->sPort, &hints, &pAddr);
		nErrno = errno;
		if(nErr == 0) break;       /* Worked */
		
		if(pAddr){                 /* Didn't work... */
			freeaddrinfo(pAddr);
			pAddr = NULL;
		}
		
		switch(nErr){
#ifdef EAI_SYSTEM
		case EAI_SYSTEM:           /* ...because of a SIGNAL or system error */
			if(nLoops == 0)
				daslog_warn_v("Address resolution failed for %s, looping with timeout", pUrl->sHost);
			break;
#endif

#ifdef EAI_ADDRFAMILY
		case EAI_ADDRFAMILY:       /* ...because I don't speak IPv4 */
			if(nLoops == 0){
				daslog_info_v("IPv4 address resolution failed for %s, trying IPv6", pUrl->sHost);
				hints.ai_family = AF_INET6;
			}
			else
				bStop = true;        /* ...nor IPv6? */
			break;
#endif

		default:                   /* ...and likely won't */
			bStop = true;
			break;
		}
		if(bStop)
			break;
		
#ifndef _WIN32
		/* breaking at 800 ms and stepping by 50 millisec each time gives a
		   total wait time of 6 seconds.  Probably too generous. */
			
		if(wait.tv_nsec >= 800000000 /* 800 ms */) break;
		
		/* Unix version of the code could end up waiting much longer if
			sleep keeps getting interrupted by a Signal.  Hence the sanity
			loop count check */
		if(nLoops >= 20 /* Nominial is 15 */)  break;
		
		/* Set timer and try again */
		if(remaining.tv_nsec == 0) wait.tv_nsec += 50000000 /* 50 ms*/ ;
		
		nanosleep(&wait, &remaining);
#else
		if(nMilli >= 800) break;
		nMilli += 50;
		Sleep(nMilli);
#endif
		nLoops += 1;
	}
	
	/* If I couldn't get an address, report last error message */
	const char* sReason = NULL;
	if(!pAddr){

#ifdef EAI_SYSTEM
		/* EAI_SYSTEM not defined as a return on Windows */				
		if(nErr == EAI_SYSTEM) sReason = strerror(nErrno);
		else
#endif			
			sReason = gai_strerror(nErr);
		
		pRes->sError = das_string(
			"Couldn't getting address info for host %s, port %s because, %s",
			pUrl->sHost, pUrl->sPort, sReason
		);
		
		return NULL;	
	}
	
	/* Got an address, so save it */
	pthread_mutex_lock(&g_mtxAddrArys);
	
	DasAry_append(g_pAddrAry, (const ubyte*) &pAddr, 1);
	
	DasAry_append(g_pHostAry, (ubyte*)(sHostAndPort), strlen(sHostAndPort) + 1);
	DasAry_markEnd(g_pHostAry, DIM1);  /* Roll first index, last idx is ragged */
		
	pthread_mutex_unlock(&g_mtxAddrArys);
	
	return pAddr;
}


bool _das_http_connect(DasHttpResp* pRes, struct timeval* pTimeOut)
{
	struct das_url* pUrl = &(pRes->url);

	struct addrinfo* pAddr = NULL;
	
	daslog_debug_v("Connecting to %s, port %s, path %s, args %s", 
			        pUrl->sHost, pUrl->sPort, pUrl->sPath, pUrl->sQuery);
	
	pAddr = _das_http_getsrvaddr(pRes);
	if(pAddr == NULL) return false;
	
	int nErr = 0; errno = 0;
	int nFd = socket(pAddr->ai_family, pAddr->ai_socktype, pAddr->ai_protocol);
	if(nFd == -1){
		nErr = errno;
		pRes->sError = das_string(
			"Couldn't get socket to host %s, because %s, address info dump follows\n"
			"   ai_family %d  ai_socktype %d  ai_protocol %d", 
			pUrl->sHost, strerror(nErr),
			pAddr->ai_family, pAddr->ai_socktype, pAddr->ai_protocol);
		return false;
	}
	else{
		daslog_debug_v("Connecting to host %s, socket info follows\n"
			"ai_family %d  ai_socktype %d  ai_protocol %d  sock_fd %d",  pUrl->sHost, 
			pAddr->ai_family, pAddr->ai_socktype, pAddr->ai_protocol, nFd
		);
	}

	/* Set a time out for first connect, removed after initial connection */
	if((pTimeOut != NULL) && ((pTimeOut->tv_sec != 0)||(pTimeOut->tv_usec != 0))){
	
#ifndef _WIN32
		size_t uSz = sizeof(struct timeval);
		nErr = setsockopt(nFd, SOL_SOCKET, SO_RCVTIMEO, pTimeOut, uSz);
		if(nErr >= 0)
			nErr = setsockopt(nFd, SOL_SOCKET, SO_SNDTIMEO, pTimeOut, uSz);
#else
		DWORD uMilli = pTimeOut->tv_sec * 1000;
		uMilli += (DWORD) ( ((double)(pTimeOut->tv_usec)) / 1000.0 );
		nErr = setsockopt(nFd, SOL_SOCKET, SO_RCVTIMEO, (const char*)&uMilli, sizeof(DWORD));
		if(nErr != 0)
			nErr = setsockopt(nFd, SOL_SOCKET, SO_SNDTIMEO, (const char*)&uMilli, sizeof(DWORD));
#endif
		
		if(nErr < 0){
			das_error(DASERR_HTTP, "Error setting socket timeout value");
			/* freeaddrinfo(pAddr); */
			return false;
		}
	}

	nErr = 0; errno = 0;
	if( connect(nFd, pAddr->ai_addr, pAddr->ai_addrlen) == -1){
		nErr = errno;
		if((pTimeOut != NULL) && ((pTimeOut->tv_sec != 0)||(pTimeOut->tv_usec != 0)))
			pRes->sError= das_string(
				"Couldn't connect to host %s, within %.3f seconds", pUrl->sHost, 
				((double)pTimeOut->tv_sec) + (((double)pTimeOut->tv_usec)/1000.0)
			);
		else
			pRes->sError = das_string("Couldn't connect to host %s, %s", pUrl->sHost,
					    strerror(nErr));
		/* freeaddrinfo(pAddr); */
		return false;
	}
	
	/* Unsetting the timeout value for subsequent communication */
	struct timeval tv = {0, 0};
	if((pTimeOut != NULL) && ((pTimeOut->tv_sec != 0)||(pTimeOut->tv_usec != 0))){
	
#ifndef _WIN32	
		size_t uSz = sizeof(struct timeval);
		nErr = setsockopt(nFd, SOL_SOCKET, SO_RCVTIMEO, &tv, uSz);
		if(nErr >= 0)
			nErr = setsockopt(nFd, SOL_SOCKET, SO_SNDTIMEO, &tv, uSz);
#else
		DWORD uMilli = 0;
		nErr = setsockopt(nFd, SOL_SOCKET, SO_RCVTIMEO, (const char*)&uMilli, sizeof(DWORD));
		if(nErr != 0)
			nErr = setsockopt(nFd, SOL_SOCKET, SO_SNDTIMEO, (const char*)&uMilli, sizeof(DWORD));
#endif
			
		if(nErr < 0){
			das_error(DASERR_HTTP, "Error setting socket timeout value");
			/* freeaddrinfo(pAddr); */
			return false;
		}
	}

	/* freeaddrinfo(pAddr); */

	/* Have to handle the fact that blocking SIGPIPE is different on Apple, it's
	 * done as a socket option so let's just set it at the beginning of the
	 * connection */
#ifdef __APPLE__
	int nVal = 1;
	int nRet = setsockopt(nFd,SOL_SOCKET,SO_NOSIGPIPE,(void*)&nVal,sizeof(nVal));
	if(nRet) {
		das_error(DASERR_HTTP, "Couldn't set SO_NOSIGPIPE");
		return false;
	}
#endif

	/* If this is an https address, try to start an SSL session */

	if(strcmp(pUrl->sScheme, "https") == 0){
		if(! das_http_setup_ssl()){
#ifndef _WIN32		
			shutdown(nFd, SHUT_RDWR);
			close(nFd);
#else
			shutdown(nFd, SD_BOTH);
			closesocket(nFd);
#endif
			return false;
		}

		/* Create a new SSL object, keep the mutex lock until we are done
		 * reading /generating any error strings */
		SSL* pSsl = NULL;

		pthread_mutex_lock(&g_mtxHttp);
		daslog_debug_v("Creating new SSL session for fd %d", nFd);
		pSsl = SSL_new(g_pSslCtx);
		pthread_mutex_unlock(&g_mtxHttp);

		if(pSsl == NULL){
			pRes->sError = _http_get_ssl_global_errors();
#ifndef _WIN32		
			shutdown(nFd, SHUT_RDWR);
			close(nFd);
#else
			shutdown(nFd, SD_BOTH);
			closesocket(nFd);
#endif
			return false;
		}

		if( SSL_set_fd(pSsl, nFd) != 1){
			pRes->sError = _http_get_ssl_global_errors();
			SSL_free(pSsl);
#ifndef _WIN32		
			shutdown(nFd, SHUT_RDWR);
			close(nFd);
#else
			shutdown(nFd, SD_BOTH);
			closesocket(nFd);
#endif
			return false;
		}

		SSL_set_mode(pSsl, SSL_MODE_AUTO_RETRY);

		int nRet = SSL_connect(pSsl);
		if(nRet != 1){
			pRes->sError = das_ssl_getErr(pSsl, nRet);
			SSL_free(pSsl);
#ifndef _WIN32		
			shutdown(nFd, SHUT_RDWR);
			close(nFd);
#else
			shutdown(nFd, SD_BOTH);
			closesocket(nFd);
#endif
			return false;
		}

		pRes->pSsl = pSsl;
		pRes->nSockFd = -1;
	}
	else{
		pRes->nSockFd = nFd;
	}
	return true;
}


bool _das_http_getRequest(
	DasHttpResp* pRes, const char* sAgent, const char* sAuth, DasBuf* pBuf
){
/* Have to handle the fact that blocking SIGPIPE is different on Apple */
#if defined(__APPLE__) || defined(_WIN32) || defined(__sun)
#define MSG_NOSIGNAL 0
#endif

	struct das_url* pUrl = &(pRes->url);

	/* Why didn't I make buffer's expandible?  Maybe because it would screw up
	* sub buffers.  Should probably refactor to rememeber offsets and a parent
	* buffer pointer instead of storing data buffer pointers directly */
	DasBuf_reinit(pBuf);
	DasBuf_printf(pBuf, "GET %s?%s HTTP/1.0\r\n", pUrl->sPath, pUrl->sQuery);
	DasBuf_printf(pBuf, "Host: %s\r\n", pUrl->sHost);

	if((sAgent != NULL)&&(sAgent[0] != '\0'))
		DasBuf_printf(pBuf, "User-Agent: %s\r\n", sAgent);
	else
		DasBuf_printf(pBuf, "User-Agent: " LIBDAS2_USER_AGENT "\r\n");

	if((sAuth != NULL)&&(sAuth[0] != '\0')){
		/* fprintf(stderr, "Authorization: Basic %s\r\n", sAuth); */
		DasBuf_printf(pBuf, "Authorization: Basic %s\r\n", sAuth);
	}
	DasBuf_printf(pBuf, "Connection: close\r\n\r\n");

	size_t uSent = 0;
	ptrdiff_t nRet = 0;
	int nErr = 0;
	size_t uToSend = DasBuf_unread(pBuf);

	if(pRes->pSsl == NULL){
		/* Plain ole socket, may take multiple calls to get the data out */
		while(uSent < uToSend){
			errno = 0;
			nRet = send(pRes->nSockFd, pBuf->pReadBeg+uSent, uToSend, MSG_NOSIGNAL);
			nErr = errno;
			if(nRet < 0){
				pRes->sError = das_string("Error sending to host %s, %s",
				                            pRes->url.sHost, strerror(nErr));
				return false;
			}
			else{
				uSent += nRet;
			}
		}

	}
	else{
		/* SSL socket set in SSL_MODE_AUTO_RETRY mode*/

		nRet = SSL_write((SSL*)pRes->pSsl, pBuf->pReadBeg, uToSend);
		if(nRet <= 0){
			nErr = SSL_get_error((SSL*)pRes->pSsl, nRet);
			return false;
		}
	}

	return true;
}

bool _das_http_hdrSearch(DasBuf* pBuf, const char* sField, char* sBuf, size_t uLen)
{
	if((sField == NULL)||(sField[0] == '\0')) return false;
	DasBuf_setReadOffset(pBuf, 0);
	const char* pLine = NULL;
	size_t uRecLen = 0;
	size_t u, iSep = 0;
	size_t uFieldLen = strlen(sField);
	bool bCont = false;
	
	memset(sBuf, 0, uLen);  /* Clear the buf, shouldn't depend on caller to do it */
	
	while((pLine = DasBuf_readRec(pBuf, "\r\n", 2, &uRecLen)) != NULL){

		if(uRecLen < (uFieldLen + 4)) continue; /* must hold field+':'+X+sep */

		iSep = 0;

		while((iSep < uRecLen)&&(pLine[iSep] != ':')) ++iSep;
		if((iSep == 0)||(iSep == uRecLen)||(pLine[iSep]!=':')) continue;

		if(iSep != uFieldLen) continue; /* Can't be it if length mismatch */

		bCont = false;
		for(u = 0; u < uFieldLen; ++u){
			if(pLine[u] != sField[u]){	bCont = true; break;	}
		}
		if(bCont) continue;

		/* We have found the field, advance past colon and whitespace */
		++iSep;
		while((iSep < uRecLen)&&(isspace(pLine[iSep]))) ++iSep;
		if(iSep == uRecLen) continue;

		/* Copy over stopping at new-line character */
		for(u = 0;
		    (u<uLen) && (pLine[u+iSep] != '\r') && (pLine[u+iSep] != '\n') &&
			 ((u+iSep)<uRecLen);
		    ++u){
			sBuf[u] = pLine[u + iSep];
		}
		DasBuf_setReadOffset(pBuf, 0);
		return true;
	}
	DasBuf_setReadOffset(pBuf, 0);
	return false;
}

bool _das_http_setFileName(DasBuf* pBuf, DasHttpResp* pRes)
{
	char sDis[196] = {'\0'};
	if(!_das_http_hdrSearch(pBuf, "Content-Disposition", sDis, 195)) return false;
	char* pRead = NULL;
	if( (pRead = strstr(sDis, "filename=")) == NULL) return false;
	if(strlen(pRead + 9) == 0) return false;

	/* Temporary workaround for bug in das-flex server */
	if(pRead[9] == '"')
		pRes->sFilename = das_strdup(pRead + 10);
	else 
		pRes->sFilename = das_strdup(pRead + 9);
	return true;
}

bool _das_http_setMime(DasBuf* pBuf, DasHttpResp* pRes)
{
	char sType[128] = {'\0'};
	if(!_das_http_hdrSearch(pBuf, "Content-Type", sType, 127)) return false;
	pRes->pMime = das_strdup(sType);
	return true;
}

bool _das_http_readHdrs(DasHttpResp* pRes, DasBuf* pBuf)
{
	/* Just read through the headers, DON'T move the read point into
	 * the message body! */

	assert(DasBuf_writeSpace(pBuf) >= 2048);

	char* pHdrEnd = NULL;
	char sBuf[2048] = {'\0'};
	ssize_t nLen = 0;
	ssize_t nMore = 0;
	ssize_t nRead = 0;


	while(true){

		if(nLen == 2048){
			pRes->sError = das_string("HTTP Header over %d bytes long", 2048);
				return false;
		}

		if(pRes->pSsl){
			nRead = SSL_peek(pRes->pSsl, sBuf + nLen, 2048 - nLen);
			if(nRead == 0){
				pRes->sError = das_string("Host %s closed connection before sending "
						  "any headers", pRes->url.sHost);
				return false;
			}
			if(nRead < 0){
				char* sErr = das_ssl_getErr(pRes->pSsl, nRead);
				pRes->sError = das_string("Error reading from host %s, %s",
				                           pRes->url.sHost, sErr);
				free(sErr);
				return false;
			}
		}
		else{
			nRead = recv(pRes->nSockFd, sBuf + nLen, 2048 - nLen, MSG_PEEK);
			/* nRead + nLen will not go past the end of sBuf */

			if(nRead == -1){
				errno = 0;
				pRes->sError = das_string("Error reading from host %s, %s",
				                           pRes->url.sHost, strerror(errno));
				return false;
			}
			if(nRead == 0){
				pRes->sError = das_string("Host % closed connection before sending "
						  "any headers", pRes->url.sHost);
				return false;
			}
		}
		pHdrEnd = strstr(sBuf, "\r\n\r\n");
		if(pHdrEnd == NULL){
			/* Consume in the peek'ed bytes and go round again */
			if(pRes->pSsl)
				nLen += SSL_read(pRes->pSsl, sBuf + nLen, nRead);
			else
				nLen += recv(pRes->nSockFd, sBuf + nLen, nRead, 0);
		}
		else{
			/* Consume up to the end of the header */
			nMore = ((pHdrEnd + 4) - sBuf) - nLen;
			if(pRes->pSsl)
				nLen += SSL_read(pRes->pSsl, sBuf + nLen, nMore);
			else
				nLen += recv(pRes->nSockFd, sBuf + nLen, nMore, 0);

			break;
		}
	}


	/* Find the HTTP response code */
	char* pRead = sBuf;
	char sCode[4] = {'\0'};
	while((*pRead != '\0') && (*pRead != ' ')) ++pRead;
	if(*pRead != ' '){
		pRes->sError = das_string("Malformed header from host %s", pRes->url.sHost);
		return false;
	}
	++pRead;
	for(size_t u = 0; u < 3; ++u){
		if(isdigit(*pRead)) sCode[u] = pRead[u];
		else{
			pRes->sError = das_string("Malformed header from host %s", pRes->url.sHost);
			return false;
		}
	}
	pRes->nCode = atoi(sCode);

	DasBuf_write(pBuf, sBuf, nLen);            /* Save off the buffer */
	return true;  /* They should be ready to recieve the msg body now */
}

/* ************************************************************************* */
void _das_http_drain_socket(DasHttpResp* pRes){
	char sBuf[1024];
	ssize_t nTotal = 0;
	ssize_t nRead = 0;
	if(pRes->pSsl){
		while((nRead = SSL_read(pRes->pSsl, sBuf, 1024)) > 0) 
			nTotal += nRead;
	}
	else{
		while((nRead = recv(pRes->nSockFd, sBuf, 1024, 0)) > 0) 
			nTotal += nRead;
	}
	daslog_debug_v("Drained %d further bytes from %s", nTotal, pRes->url.sHost);
}

/* ************************************************************************* */
bool _das_http_redirect(DasHttpResp* pRes, DasBuf* pBuf)
{
	char sNewUrl[1024];
	if(!_das_http_hdrSearch(pBuf, "Location", sNewUrl, 1023)){
		pRes->sError = das_string("Couldn't find Location header in redirect "
				                     "message from host %s", pRes->url.sHost);
		return false;
	}

	//size_t uEnd = 0;
	//if(sNewUrl[0] != '\0'){
	//	uEnd = strlen(sNewUrl) - 1;
	//	if(sNewUrl[uEnd] == '?')
	//		sNewUrl[uEnd] = '\0';
	//}
	daslog_debug_v("Redirected to: %s", sNewUrl);
	_das_http_drain_socket(pRes); // Read and toss any remaining data...

	// Tear down the existing SSL socket if needed
	if(pRes->pSsl){
		daslog_debug("Old SSL socket teardown");
		pRes->nSockFd = SSL_get_fd((SSL*)pRes->pSsl);
		if(SSL_shutdown((SSL*)pRes->pSsl) == 0){
			SSL_shutdown((SSL*)pRes->pSsl);
		}
		SSL_free((SSL*)pRes->pSsl);
		pRes->pSsl = NULL;
	}

	daslog_debug_v("Shutting down socket: %d", pRes->nSockFd);
#ifndef _WIN32
	shutdown(pRes->nSockFd, SHUT_RDWR);
	close(pRes->nSockFd);
#else
	shutdown(pRes->nSockFd, SD_BOTH);
	closesocket(pRes->nSockFd);
#endif	
	pRes->nSockFd = -1;

	/* Parse a new URL into the url structure */
	if(!DasHttpResp_init(pRes, sNewUrl)) return false;
	return true;
}

void DasHttpResp_freeFields(DasHttpResp* pRes){
	if(pRes->pMime) free(pRes->pMime);
	if(pRes->sHeaders) free(pRes->sHeaders);
}

/* ************************************************************************* */
/* Get a message body socket, involves quite a few steps */

bool das_http_getBody(
	const char* sUrl, const char* sAgent, DasCredMngr* pMgr, DasHttpResp* pRes,
	float rConSec
 ){

	DasHttpResp_clear(pRes);  /* Sets nSockFd to -1 */

	if(strlen(sUrl) > DASURL_SZ_QUERY){
		pRes->sError = das_string("URL is greater than " QDEF(DASURL_SZ_QUERY) " bytes");
		return false;
	}
	
	struct timeval tv = {0, 0};
	if(rConSec > 0.0){
		tv.tv_sec = (long)rConSec;
		tv.tv_usec = (rConSec - (long)rConSec) * 1000;
	}

	if(! DasHttpResp_init(pRes, sUrl)) return false;

	DasBuf* pBuf = new_DasBuf(2048);
	const char* sAuth = NULL;
	char sServer[512] = {'\0'};
	char sRealmHdr[512] = {'\0'};
	const char* pRealm = NULL;  /* Used to dig realm out of header */
	char* pWrite = NULL; /* Used to null out ending realm quote */
	while(true){

		/* Try to connect */
		if(! _das_http_connect(pRes, &tv) ){ 
			goto CLEANUP_ERROR;
		}

		/* Send the request */
		if(! _das_http_getRequest(pRes, sAgent, sAuth, pBuf) ){
			goto CLEANUP_ERROR;
		}

		/* Get the response and save the headers */
		DasBuf_reinit(pBuf);  /* buffer will be used to write headers */
		if(! _das_http_readHdrs(pRes, pBuf)){
			goto CLEANUP_ERROR;
		}

		if((strcmp(pRes->url.sPort, "80") != 0)&&
		   (strcmp(pRes->url.sPort, "443") != 0))
			snprintf(sServer, 511, "%s://%s:%s%s", pRes->url.sScheme,
			         pRes->url.sHost, pRes->url.sPort, pRes->url.sPath);
		else
			snprintf(sServer, 511, "%s://%s%s", pRes->url.sScheme,
			         pRes->url.sHost, pRes->url.sPath);

		switch(pRes->nCode){
		case HTTP_OK:
			pRes->sHeaders = (char*)calloc(DasBuf_written(pBuf) + 1, sizeof(char));
			DasBuf_read(pBuf, pRes->sHeaders, DasBuf_written(pBuf));
			_das_http_setFileName(pBuf, pRes);
			_das_http_setMime(pBuf, pRes);
			del_DasBuf(pBuf);
			return true;
			break;
		case HTTP_MovedPerm:
		case HTTP_Found:
		case HTTP_TempRedir:
		case HTTP_PermRedir:
			// uses the existing socket to pull down the redirect, then
			// tears it down so the SSL context can be re-used.
			if(! _das_http_redirect(pRes, pBuf)) goto CLEANUP_ERROR;
			break;
		case HTTP_AuthReq:
			_das_http_drain_socket(pRes); // Read and toss any remaining data...
			if(pRes->pSsl){
				pRes->nSockFd = SSL_get_fd((SSL*)pRes->pSsl);
				if(SSL_shutdown((SSL*)pRes->pSsl) == 0){
					SSL_shutdown((SSL*)pRes->pSsl);
					SSL_free((SSL*)pRes->pSsl);
					pRes->pSsl = NULL;
				}
			}
#ifndef _WIN32		
			shutdown(pRes->nSockFd, SHUT_RDWR);
			close(pRes->nSockFd);
#else
			shutdown(pRes->nSockFd, SD_BOTH);
			closesocket(pRes->nSockFd);
#endif	
			pRes->nSockFd = -1;

			/* Dig out the realm */
			_das_http_hdrSearch(pBuf, "WWW-Authenticate", sRealmHdr, 512);
			pRealm = strstr(sRealmHdr, "realm=\"");
			if(pRealm != NULL){
				pRealm += 7;
				pWrite = strchr(pRealm, '"');
				if(pWrite != NULL) *pWrite = '\0';
			}

			/* If auth is non-NULL it means it didn't work, tell the credentials
			 manager that this auth string isn't working */
			if((sAuth != NULL)&&(pMgr != NULL)){
				CredMngr_authFailed(pMgr, sServer, pRealm, pRes->url.sDataset,
					                 "Credentials not accepted by remote server");
			}

			if(pMgr == NULL){
				pRes->sError = das_string(
						"Auth required for dataset %s on server %s, but no credentials "
						"manager was supplied.", pRes->url.sDataset, sServer
				);
				goto CLEANUP_ERROR;
			}
			else{
				sAuth = CredMngr_getHttpAuth(pMgr, sServer, pRealm, pRes->url.sDataset);
				if(sAuth == NULL){
					pRes->sError = das_string(
						"Credentials manager did not supply an authentication token"
						" for dataset %s on server %s", pRes->url.sDataset, sServer
					);
					goto CLEANUP_ERROR;
				}
			}
			break;
		case HTTP_Forbidden:
			pRes->sError = das_string("Access to dataset '%s' on server '%s' was "
			                           "forbidden", pRes->url.sDataset, sServer);
			goto CLEANUP_ERROR;
			break;

		case HTTP_NotFound:
			pRes->sError = das_string("Error in request path '%s' for host '%s'",
					pRes->url.sPath, pRes->url.sHost);
			goto CLEANUP_ERROR;
			break;

		case HTTP_BadReq:
			pRes->sError = das_string("Error in query parameters '%s'",
			                           pRes->url.sQuery);
			goto CLEANUP_ERROR;
			break;

		default:
			pRes->sError = das_string("Server returned HTTP status %d when "
					         "accessing %s", pRes->nCode, sUrl);
			goto CLEANUP_ERROR;
			break;
		}
	}

	del_DasBuf(pBuf);
	return true;

CLEANUP_ERROR:
	del_DasBuf(pBuf);
	if(pRes->pSsl != NULL){
		pRes->nSockFd = SSL_get_fd((SSL*)pRes->pSsl);
		if(SSL_shutdown((SSL*)pRes->pSsl) == 0){
			SSL_shutdown((SSL*)pRes->pSsl);
			SSL_free((SSL*)pRes->pSsl);
		}
	}
	if(pRes->nSockFd > -1){
#ifndef _WIN32		
		shutdown(pRes->nSockFd, SHUT_RDWR);
		close(pRes->nSockFd);
#else
		shutdown(pRes->nSockFd, SD_BOTH);
		closesocket(pRes->nSockFd);
#endif			
		pRes->nSockFd = -1;
	}
	return false;
}


/* ************************************************************************* */
/* A just-give-me-a-bag-of-bytes convenience function */

#define D2CHAR_CHUNK_SZ 16384

DasAry* das_http_readUrl(
	const char* sUrl, const char* sAgent, DasCredMngr* pMgr, DasHttpResp* pRes,
	int64_t nLimit, float rConSec
){
	if( ! das_http_getBody(sUrl, sAgent, pMgr, pRes, rConSec)){
		return NULL;
	}
	if(nLimit < 1) nLimit = -1;
	int64_t nTotal = 0;

	DasAry* pAry = new_DasAry("http_body", vtUByte, 1, NULL, RANK_1(0), UNIT_DIMENSIONLESS);

	SSL* pSsl = (SSL*)pRes->pSsl;
	char buf[D2CHAR_CHUNK_SZ];
	int nRead = 0;

	if(DasHttpResp_useSsl(pRes)){
		while((nLimit == -1)||(nTotal < nLimit)){
			nRead = SSL_read(pSsl, buf, D2CHAR_CHUNK_SZ);

			if(nRead == 0) break;  /* Socket is done */

			if(nRead < 1){         /* Socket is broke */
				char* sErr = das_ssl_getErr(pSsl, nRead);
				daslog_error_v("Error reading from SSL socket, %s", sErr);
				free(sErr);
				dec_DasAry(pAry);
				return false;
			}

			nTotal += nRead;
			if(!DasAry_append(pAry, (const ubyte*) buf, nRead)) return false; /* Yay data! */
		}

		pRes->nSockFd = SSL_get_fd((SSL*)pRes->pSsl);
		if(SSL_shutdown((SSL*)pRes->pSsl) == 0)  // If 0 needs 2-stage shutdown
			SSL_shutdown((SSL*)pRes->pSsl);
		
		SSL_free((SSL*)pRes->pSsl);
		pRes->pSsl = NULL;
	}
	else{
		while((nLimit == -1)||(nTotal < nLimit)){
			errno = 0;
			nRead = recv(pRes->nSockFd, buf, D2CHAR_CHUNK_SZ, 0);

			if(nRead == 0) break;  /* Socket is done */

			if(nRead < 0){         /* Socket is broke */
				daslog_error_v("Error reading from socket, %s", strerror(errno));
				dec_DasAry(pAry);
				return false;
			}

			nTotal += nRead;
			if(!DasAry_append(pAry, (const ubyte*) buf, nRead)) return false; /* Yay data! */
		}
	}
	if(nTotal > nLimit){
		daslog_warn_v("Limit of %ld bytes hit, almost certainly returning a "
				      "partial download", nLimit);
	}
	else
		daslog_debug_v("%ld bytes read from %s", DasAry_size(pAry), sUrl);

	daslog_debug_v("Shutting down socket %d", pRes->nSockFd);
#ifndef _WIN32
	shutdown(pRes->nSockFd, SHUT_RDWR);
	close(pRes->nSockFd);
#else
	shutdown(pRes->nSockFd, SD_BOTH);
	closesocket(pRes->nSockFd);
#endif	
	pRes->nSockFd = -1;

	return pAry;
}
