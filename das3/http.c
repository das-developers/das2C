/* Copyright (C) 2017-2026 Chris Piker <chris-piker@uiowa.edu>
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
 #define _XOPEN_SOURCE 600  /* Trying to get pthread_mutexattr_settype */

#include <pthread.h>
#include <ctype.h>
#include <errno.h>
#include <stdarg.h>
#include <string.h>
#include <inttypes.h>

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

#include <openssl/ssl.h>
#include <openssl/err.h>

#include "util.h"
#include "http_p.h"
#include "creds_p.h"
#include "buffer.h"
#include "log.h"
#include "info_p.h"
#include "util_p.h"

#ifdef _WIN32
#define strncasecmp _strnicmp
#else
#include <strings.h>
#endif

#ifdef _WIN32
typedef ptrdiff_t ssize_t;
#endif

/* Linux has a per-call flag to keep a closed peer from raising SIGPIPE.
   Mac, Windows and Solaris do not; Mac gets SO_NOSIGPIPE at connect. */
#ifndef MSG_NOSIGNAL
#define MSG_NOSIGNAL 0
#endif

#define LIBDAS_USER_AGENT "libdas3"

#define HTTP_OK        200
#define HTTP_NotModified 304
#define HTTP_MovedPerm 301
#define HTTP_Found     302
#define HTTP_TempRedir 307   /* Treat as 307 */
#define HTTP_PermRedir 308   /* Treat as 301 */
#define HTTP_BadReq    400
#define HTTP_AuthReq   401
#define HTTP_Forbidden 403
#define HTTP_NotFound  404
#define HTTP_Error     500

/* Host names are at most 253 bytes (RFC 1035), ports at most 5 digits */
#define _HTTP_SZ_HOST 255
#define _HTTP_SZ_PORT   7

/* Larger than any sane server sends, a cookie-heavy reply runs a few KB */
#define _HTTP_HDR_MAX 16384

/* A redirect loop is a server bug, but it should not hang the client */
#define _HTTP_MAX_HOPS 10

#define _HTTP_CHUNK_SZ 16384

/* ************************************************************************* */
/* The response */

struct das_http_resp {
	int   nCode;         /* -1 until a server answers */
	bool  bOk;
	char* sError;
	char* sHeaders;      /* header block of the last reply, NUL terminated */
	char* sMime;
	char* sFilename;

	char* sUrl;          /* current URL, the final one after redirects */
	das_url_parts parts; /* where its pieces are, see das_url_parse() */

	/* NUL terminated copies for the socket calls, set with sUrl */
	bool  bHttps;
	bool  bDefPort;      /* the port is the scheme's, leave it out of Host: */
	bool  bIp6;          /* host is an IPv6 literal, brackets kept off sHost */
	char  sHost[_HTTP_SZ_HOST + 1];
	char  sPort[_HTTP_SZ_PORT + 1];

	DasXport* pX;        /* the open body, NULL when there is none */
};

/* Replace the error message, keeping only the newest */
static void _http_err(DasHttpResp* pRes, const char* sFmt, ...)
{
	va_list argp;
	va_start(argp, sFmt);
	char* sMsg = das_vstring(sFmt, argp);
	va_end(argp);
	free(pRes->sError);
	pRes->sError = sMsg;
}

static void _http_clearReply(DasHttpResp* pRes)
{
	free(pRes->sHeaders);  pRes->sHeaders = NULL;
	free(pRes->sMime);     pRes->sMime = NULL;
	free(pRes->sFilename); pRes->sFilename = NULL;
	pRes->nCode = -1;
}

DasHttpResp* _new_DasHttpResp(void)
{
	DasHttpResp* pThis = (DasHttpResp*)calloc(1, sizeof(DasHttpResp));
	if(pThis == NULL){
		das_error(DASERR_HTTP, "Couldn't allocate an HTTP response");
		return NULL;
	}
	pThis->nCode = -1;
	return pThis;
}

void del_DasHttpResp(DasHttpResp* pThis)
{
	if(pThis == NULL) return;
	del_DasXport(pThis->pX);
	_http_clearReply(pThis);
	free(pThis->sError);
	free(pThis->sUrl);
	free(pThis);
}

bool        DasHttpResp_ok(const DasHttpResp* pThis){ return pThis->bOk; }
int         DasHttpResp_code(const DasHttpResp* pThis){ return pThis->nCode; }
const char* DasHttpResp_error(const DasHttpResp* pThis){ return pThis->sError; }
const char* DasHttpResp_mime(const DasHttpResp* pThis){ return pThis->sMime; }
const char* DasHttpResp_filename(const DasHttpResp* pThis){ return pThis->sFilename; }
const char* DasHttpResp_url(const DasHttpResp* pThis){ return pThis->sUrl; }

DasXport* DasHttpResp_takeBody(DasHttpResp* pThis)
{
	DasXport* pX = pThis->pX;
	pThis->pX = NULL;
	return pX;
}

/* Copy a counted part into a fixed buffer, false if it does not fit */
static bool _http_partCopy(
	const char* sUrl, const das_url_parts* pParts, das_url_part_e ePart,
	char* sOut, size_t uOut
){
	size_t uLen = 0;
	const char* p = das_url_part(sUrl, pParts, ePart, &uLen);
	sOut[0] = '\0';
	if(p == NULL) return true;
	if(uLen >= uOut) return false;
	memcpy(sOut, p, uLen);
	sOut[uLen] = '\0';
	return true;
}

/* Make sUrl the current URL.  False, with the error set, for anything the
 * client can't request. */
static bool _http_setUrl(DasHttpResp* pRes, const char* sUrl)
{
	char* sCopy = das_strdup(sUrl);
	if(sCopy == NULL){
		_http_err(pRes, "Couldn't copy URL");
		return false;
	}
	free(pRes->sUrl);
	pRes->sUrl = sCopy;

	if(!das_url_parse(pRes->sUrl, &(pRes->parts))){
		_http_err(pRes, "Malformed URL %s", sUrl);
		return false;
	}

	size_t uLen = 0;
	const char* pScheme = das_url_part(pRes->sUrl, &(pRes->parts), DASURL_SCHEME, &uLen);
	if((uLen == 5) && (strncasecmp(pScheme, "https", 5) == 0))     pRes->bHttps = true;
	else if((uLen == 4) && (strncasecmp(pScheme, "http", 4) == 0)) pRes->bHttps = false;
	else{
		_http_err(pRes, "Unknown scheme, %.*s", (int)uLen, pScheme);
		return false;
	}

	if(!_http_partCopy(pRes->sUrl, &(pRes->parts), DASURL_HOST, pRes->sHost, sizeof(pRes->sHost))){
		_http_err(pRes, "Host name in URL %s is too long", sUrl);
		return false;
	}
	/* getaddrinfo() wants an IPv6 literal without its brackets */
	pRes->bIp6 = (pRes->sHost[0] == '[');
	if(pRes->bIp6){
		size_t u = strlen(pRes->sHost);
		memmove(pRes->sHost, pRes->sHost + 1, u - 2);
		pRes->sHost[u - 2] = '\0';
	}
	if(pRes->sHost[0] == '\0'){
		_http_err(pRes, "Invalid host in URL %s", sUrl);
		return false;
	}

	_http_partCopy(pRes->sUrl, &(pRes->parts), DASURL_PORT, pRes->sPort, sizeof(pRes->sPort));
	pRes->bDefPort = (strcmp(pRes->sPort, pRes->bHttps ? "443" : "80") == 0);
	return true;
}

/* scheme://host[:port]path, the key logins are stored under */
static void _http_server(const DasHttpResp* pRes, char* sBuf, size_t uLen)
{
	size_t uPath = 0;
	const char* pPath = das_url_part(pRes->sUrl, &(pRes->parts), DASURL_PATH, &uPath);
	snprintf(sBuf, uLen, "%s://%s%s%s%s%s%.*s", pRes->bHttps ? "https" : "http",
		pRes->bIp6 ? "[" : "", pRes->sHost, pRes->bIp6 ? "]" : "",
		pRes->bDefPort ? "" : ":", pRes->bDefPort ? "" : pRes->sPort,
		(int)uPath, pPath ? pPath : ""
	);
}

/* ************************************************************************* */
/* The global SSL context and mutexes for manipulating it.  We use lazy
 * initialization of SSL so that programs that don't need it don't have to
 * deal with the overhead.
 */

static pthread_mutex_t g_mtxHttp;
static SSL_CTX* g_pSslCtx = NULL;

static pthread_mutex_t g_mtxAddrArys;
static DasAry* g_pHostAry = NULL;
static DasAry* g_pAddrAry = NULL;


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
		"host_names", vtUByte, 0, (const ubyte[1]){0}, RANK_2(0,0), UNIT_DIMENSIONLESS
	);

	/* Let customers know we're storing null terminated strings as the
	   fastest moving dimension */
	DasAry_setUsage(g_pHostAry, DASARY_AS_STRING);

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

static bool das_http_setup_ssl(){
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
static char* _http_get_ssl_global_errors(){
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
		memcpy(pWrite, aErrs[iErr], uLen);
		++iErr;
		pWrite += uLen;
		*pWrite = '\n'; ++pWrite;
	}

	return sBuf;
}

/* Text for a failed SSL call, heap allocated.  Only SSL_connect() uses it
   now, every read and write goes through the transport's own messages. */
static char* _http_sslErr(const SSL* pSsl, int nRet)
{
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
		return das_strdup("Low level socket I/O error, timed out or reset");

	case SSL_ERROR_SSL:
		return _http_get_ssl_global_errors();

	default:
		return das_strdup("Unknown TLS error");
	}
}

/* ************************************************************************* */
/* Proxy support: http(s)_proxy and no_proxy, as curl reads them */

#define DASPROXY_SZ_USER 255      /* decoded user:pass */
#define DASPROXY_SZ_AUTH 340      /* its base64, 4*ceil(255/3) */

struct das_proxy {
	char sHost[_HTTP_SZ_HOST+1];  /* empty means connect directly */
	char sPort[_HTTP_SZ_PORT+1];
	char sAuth[DASPROXY_SZ_AUTH+1]; /* empty means no Proxy-Authorization */
};

/* Copy input into a fixed field up to the first stop character or the end
 * of input.  A part that does not fit is an error, never a truncation.
 * Returns where the copy stopped, or NULL with the error set.  sWhere names
 * the variable, never its value, which may carry a password. */
static const char* _http_field(
	DasHttpResp* pRes, char* sOut, size_t uMax, const char* pIn,
	const char* sStop, const char* sWhat, const char* sWhere
){
	size_t u = 0;
	while((*pIn != '\0') && (strchr(sStop, *pIn) == NULL)){
		if(u == uMax){
			_http_err(pRes, "The %s in %s is longer than %zu bytes", sWhat, sWhere, uMax);
			return NULL;
		}
		sOut[u++] = *pIn++;
	}
	sOut[u] = '\0';
	return pIn;
}

/* Entries are exact hosts or IPs, domain suffixes (leading dot optional, as
 * in curl) or "*".  CIDR ranges such as 10.0.0.0/8 and host:port entries are
 * not understood; they never match, so those targets go via the proxy. */
static bool _das_http_noProxy(const char* sHost)
{
	const char* pList = getenv("no_proxy");
	if(pList == NULL) pList = getenv("NO_PROXY");
	if(pList == NULL) return false;

	size_t uHost = strlen(sHost);
	while(*pList != '\0'){
		pList += strspn(pList, ", \t");
		const char* pEnt = pList;
		size_t uEnt = strcspn(pList, ", \t");
		pList += uEnt;

		if((uEnt == 1)&&(*pEnt == '*')) return true;
		if((uEnt > 0)&&(*pEnt == '.')){ ++pEnt; --uEnt; }
		if((uEnt == 0)||(uEnt > uHost)) continue;
		if(strncasecmp(sHost + uHost - uEnt, pEnt, uEnt) != 0) continue;

		/* Suffixes match on a label boundary, so "das2.org" skips "xdas2.org" */
		if((uEnt == uHost)||(sHost[uHost - uEnt - 1] == '.')) return true;
	}
	return false;
}

/* Userinfo is percent encoded so passwords can hold ':' and '@', the proxy
 * wants the raw bytes.  Returns the decoded length, or -1 if too long or
 * malformed. */
static int _das_pct_decode(const char* pIn, size_t uIn, char* sOut, size_t uMax)
{
	size_t uOut = 0;
	for(size_t u = 0; u < uIn; ++u){
		if(uOut == uMax) return -1;
		if(pIn[u] != '%'){ sOut[uOut++] = pIn[u]; continue; }

		if((u + 2 >= uIn)||(!isxdigit((unsigned char)pIn[u+1]))||
		   (!isxdigit((unsigned char)pIn[u+2])))
			return -1;
		char sHex[3] = {pIn[u+1], pIn[u+2], '\0'};
		sOut[uOut++] = (char)strtol(sHex, NULL, 16);
		u += 2;
	}
	return (int)uOut;
}

/* Fill pProxy from the environment for the current URL.  Returns false, with
 * the error set, only for a proxy variable that can't be used; a target that
 * should go direct leaves pProxy->sHost empty and returns true.
 *
 * Never put the variable's value in a message, it may carry a password. */
static bool _das_http_getProxy(DasHttpResp* pRes, struct das_proxy* pProxy)
{
	memset(pProxy, 0, sizeof(struct das_proxy));
	bool bHttps = pRes->bHttps;

	const char* sVar = bHttps ? "https_proxy" : "http_proxy";
	const char* pIn = getenv(sVar);
	if(pIn == NULL){
		/* A CGI server turns a request's "Proxy:" header into HTTP_PROXY
		   (httpoxy, CVE-2016-5385), so like Go, skip it under CGI. */
		if(bHttps || (getenv("REQUEST_METHOD") == NULL)){
			sVar = bHttps ? "HTTPS_PROXY" : "HTTP_PROXY";
			pIn = getenv(sVar);
		}
	}
	/* Set but empty disables the proxy, as in curl */
	if((pIn == NULL)||(*pIn == '\0')) return true;

	if(_das_http_noProxy(pRes->sHost)){
		daslog_debug_v("Host %s is in no_proxy, connecting directly", pRes->sHost);
		return true;
	}

	const char* pSep = strstr(pIn, "://");
	if(pSep != NULL){
		if((pSep - pIn != 4)||(strncasecmp(pIn, "http", 4) != 0)){
			_http_err(pRes,
				"Proxy in %s is %.*s://, only http:// proxies are supported",
				sVar, (int)(pSep - pIn) > 15 ? 15 : (int)(pSep - pIn), pIn
			);
			return false;
		}
		pIn = pSep + 3;
	}

	/* The last '@' before the path ends the userinfo, in case a password
	   holds an unencoded '@' */
	size_t uAuthority = strcspn(pIn, "/?#");
	const char* pAt = NULL;
	for(const char* p = pIn; p < pIn + uAuthority; ++p) if(*p == '@') pAt = p;

	if(pAt != NULL){
		char sUser[DASPROXY_SZ_USER];
		int nUser = _das_pct_decode(pIn, pAt - pIn, sUser, DASPROXY_SZ_USER);
		if(nUser < 0){
			_http_err(pRes,
				"Credentials in %s are badly percent encoded or over %d bytes",
				sVar, DASPROXY_SZ_USER
			);
			return false;
		}
		size_t uB64 = 0;
		char* s64 = das_b64_encode((const unsigned char*)sUser, nUser, &uB64);
		_dascred_wipe(sUser, sizeof(sUser));
		if(s64 == NULL){
			_http_err(pRes, "Couldn't encode proxy credentials from %s", sVar);
			return false;
		}
		memcpy(pProxy->sAuth, s64, uB64 + 1);  /* fits, see DASPROXY_SZ_AUTH */
		_dascred_wipe(s64, uB64);
		free(s64);
		pIn = pAt + 1;
	}

	pIn = _http_field(pRes, pProxy->sHost, _HTTP_SZ_HOST, pIn, ":/?#", "proxy host", sVar);
	if(pIn == NULL) return false;
	if(pProxy->sHost[0] == '\0'){
		_http_err(pRes, "No proxy host in %s", sVar);
		return false;
	}

	strcpy(pProxy->sPort, "1080");  /* curl's default proxy port */
	if(*pIn == ':'){
		pIn = _http_field(pRes, pProxy->sPort, _HTTP_SZ_PORT, pIn + 1, "/?#", "proxy port", sVar);
		if(pIn == NULL) return false;
		int nPort = atoi(pProxy->sPort);
		if((nPort < 1)||(nPort > 65535)){
			_http_err(pRes, "Invalid proxy port in %s", sVar);
			return false;
		}
	}

	daslog_debug_v("Using proxy %s:%s from %s for %s%s", pProxy->sHost,
		pProxy->sPort, sVar, pRes->sHost, pProxy->sAuth[0] ? ", with credentials" : ""
	);
	return true;
}

/* Ask the proxy on nFd for a tunnel to the target.  Afterwards the socket
 * carries bytes end to end, so TLS, or plain HTTP, runs as if direct.  Call
 * while the connect timeout is still set, a stalled proxy would hang us. */
static bool _das_http_tunnel(
	DasHttpResp* pRes, int nFd, const struct das_proxy* pProxy
){
	char sMsg[256 + _HTTP_SZ_HOST*2 + DASPROXY_SZ_AUTH];
	const char* sOpen = pRes->bIp6 ? "[" : "";
	const char* sClose = pRes->bIp6 ? "]" : "";

	int nLen = snprintf(sMsg, sizeof(sMsg),
		"CONNECT %s%s%s:%s HTTP/1.1\r\nHost: %s%s%s:%s\r\n",
		sOpen, pRes->sHost, sClose, pRes->sPort, sOpen, pRes->sHost, sClose, pRes->sPort
	);
	if(pProxy->sAuth[0] != '\0')
		nLen += snprintf(sMsg + nLen, sizeof(sMsg) - nLen,
			"Proxy-Authorization: Basic %s\r\n", pProxy->sAuth
		);
	nLen += snprintf(sMsg + nLen, sizeof(sMsg) - nLen, "\r\n");

	size_t uSent = 0;
	ssize_t nRet = 0;
	while(uSent < (size_t)nLen){
		errno = 0;
		nRet = send(nFd, sMsg + uSent, nLen - uSent, MSG_NOSIGNAL);
		if(nRet <= 0){
			_dascred_wipe(sMsg, sizeof(sMsg));
			_http_err(pRes, "Couldn't send CONNECT to proxy %s:%s, %s",
				pProxy->sHost, pProxy->sPort, strerror(errno)
			);
			return false;
		}
		uSent += nRet;
	}
	_dascred_wipe(sMsg, sizeof(sMsg));

	/* One byte at a time so nothing past the blank line is consumed, those
	   bytes would belong to the target.  Proxy replies are short. */
	size_t u = 0;
	while(true){
		if(u == sizeof(sMsg) - 1){
			_http_err(pRes, "Reply from proxy %s:%s is over %zu bytes",
				pProxy->sHost, pProxy->sPort, sizeof(sMsg) - 1
			);
			return false;
		}
		errno = 0;
		nRet = recv(nFd, sMsg + u, 1, 0);
		if(nRet <= 0){
			_http_err(pRes,
				"Proxy %s:%s closed the connection during CONNECT to %s:%s%s%s",
				pProxy->sHost, pProxy->sPort, pRes->sHost, pRes->sPort,
				(nRet < 0) ? ", " : "", (nRet < 0) ? strerror(errno) : ""
			);
			return false;
		}
		++u;
		if((u >= 4)&&(memcmp(sMsg + u - 4, "\r\n\r\n", 4) == 0)) break;
		if((u >= 2)&&(memcmp(sMsg + u - 2, "\n\n", 2) == 0)) break;
	}
	sMsg[u] = '\0';

	const char* pCode = strchr(sMsg, ' ');
	int nCode = (pCode != NULL) ? atoi(pCode + 1) : 0;
	if((nCode < 200)||(nCode > 299)){
		_http_err(pRes, "Proxy %s:%s refused tunnel to %s:%s, \"%.*s\"%s",
			pProxy->sHost, pProxy->sPort, pRes->sHost, pRes->sPort,
			(int)strcspn(sMsg, "\r\n"), sMsg,
			(nCode == 407) ? (pProxy->sAuth[0] ? ", credentials rejected" :
			                  ", proxy wants credentials, none in proxy URL") : ""
		);
		return false;
	}
	daslog_debug_v("Proxy tunnel open to %s:%s", pRes->sHost, pRes->sPort);
	return true;
}

/* ************************************************************************* */
/* Address lookup */

/* Getting address info is expensive, and can fail. So:
   1. Loop with multiple tries
   2. Cache the results, per host, port and IP version
   The host is passed in, not taken from the URL, because with a proxy the
   socket goes to the proxy; sKind ("host" or "proxy") is for messages.
*/
static struct addrinfo* _das_http_getsrvaddr(
	DasHttpResp* pRes, const char* sHost, const char* sPort, const char* sKind,
	das_ip_ver_e eIpVer
){
	struct addrinfo hints;
	struct addrinfo* pAddr = NULL;
	char sKey[_HTTP_SZ_HOST + _HTTP_SZ_PORT + 8];
	snprintf(sKey, sizeof(sKey), "%s:%s/%d", sHost, sPort, (int)eIpVer);

	/* First see if I already have the address info I need in the cache */
	pthread_mutex_lock(&g_mtxAddrArys);

	size_t uStrLen, uHosts = DasAry_lengthIn(g_pHostAry, DIM0);
	const char* sName = NULL;
	for(size_t u = 0; u < uHosts; ++u){
		sName = DasAry_getCharsIn(g_pHostAry, DIM1_AT(u), &uStrLen);

		if(strcmp(sKey, sName) == 0){
			pAddr = *((struct addrinfo**) DasAry_getAt(g_pAddrAry, vtUnknown, IDX0(u)));
			break;
		}
	}

	pthread_mutex_unlock(&g_mtxAddrArys);

	if(pAddr != NULL) return pAddr;   /* Yay, no DOSing the DNS today! */

	memset(&hints, 0, sizeof(struct addrinfo));
	/* Any version starts with IPv4, the das servers people use mostly have
	   it, and falls back to IPv6 for hosts that have no IPv4 address */
	hints.ai_family = (eIpVer == DASIP_V6) ? AF_INET6 : AF_INET;
	hints.ai_socktype = SOCK_STREAM;

	/* Address lookups trigger a blizzard of calls, see this article for more:

		 https://jameshfisher.com/2018/02/03/what-does-getaddrinfo-do/

		so they can fail in odd bizarre ways.  Do the lookup in a loop and back
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
		nErr = getaddrinfo(sHost, sPort, &hints, &pAddr);
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
				daslog_warn_v("Address resolution failed for %s, looping with timeout", sHost);
			break;
#endif

		/* ...because there's no IPv4 address.  glibc says so with any of
		   these, depending on the resolver, so all of them fall back. */
		case EAI_NONAME:
#ifdef EAI_ADDRFAMILY
		case EAI_ADDRFAMILY:
#endif
#if defined(EAI_NODATA) && (EAI_NODATA != EAI_NONAME)
		case EAI_NODATA:
#endif
			if((eIpVer == DASIP_ANY) && (hints.ai_family == AF_INET)){
				daslog_debug_v("No IPv4 address for %s, trying IPv6", sHost);
				hints.ai_family = AF_INET6;
				continue;               /* a different question, no backoff */
			}
			bStop = true;
			break;

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
		if(nLoops >= 20 /* Nominal is 15 */)  break;

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

		_http_err(pRes, "Couldn't get address info for %s %s, port %s because, %s",
			sKind, sHost, sPort, sReason
		);

		return NULL;
	}

	/* Got an address, so save it */
	pthread_mutex_lock(&g_mtxAddrArys);

	DasAry_append(g_pAddrAry, (const ubyte*) &pAddr, 1);

	DasAry_append(g_pHostAry, (ubyte*)(sKey), strlen(sKey) + 1);
	DasAry_markEnd(g_pHostAry, DIM1);  /* Roll first index, last idx is ragged */

	pthread_mutex_unlock(&g_mtxAddrArys);

	return pAddr;
}

/* ************************************************************************* */
/* Connecting */

static void _http_sockClose(int nFd)
{
#ifndef _WIN32
	shutdown(nFd, SHUT_RDWR);
	close(nFd);
#else
	shutdown(nFd, SD_BOTH);
	closesocket(nFd);
#endif
}

/* Set both socket timeouts, 0.0 or less clears them */
static bool _http_setTimeout(int nFd, float rSec)
{
#ifndef _WIN32
	struct timeval tv = {0, 0};
	if(rSec > 0.0){
		tv.tv_sec = (long)rSec;
		tv.tv_usec = (long)((rSec - (float)tv.tv_sec) * 1.0e6);
	}
	if(setsockopt(nFd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv)) < 0) return false;
	if(setsockopt(nFd, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv)) < 0) return false;
#else
	DWORD uMilli = (rSec > 0.0) ? (DWORD)(rSec * 1000.0) : 0;
	if(setsockopt(nFd, SOL_SOCKET, SO_RCVTIMEO, (const char*)&uMilli, sizeof(DWORD)) != 0)
		return false;
	if(setsockopt(nFd, SOL_SOCKET, SO_SNDTIMEO, (const char*)&uMilli, sizeof(DWORD)) != 0)
		return false;
#endif
	return true;
}

/* Connect to the current URL, through a proxy if the environment says so,
 * and start TLS for https.  Returns the connection as a transport, or NULL
 * with the error set.  The connect timeout covers the TLS handshake too;
 * after that the read timeout, if any, governs every read. */
static DasXport* _das_http_connect(DasHttpResp* pRes, const das_http_opts* pOpts)
{
	daslog_debug_v("Connecting to %s, port %s", pRes->sHost, pRes->sPort);

	/* Re-evaluated per call, a redirect may cross a no_proxy boundary */
	struct das_proxy proxy;
	if(! _das_http_getProxy(pRes, &proxy)) return NULL;
	bool bProxy = (proxy.sHost[0] != '\0');
	const char* sHost = bProxy ? proxy.sHost : pRes->sHost;
	const char* sPort = bProxy ? proxy.sPort : pRes->sPort;
	const char* sKind = bProxy ? "proxy" : "host";

	struct addrinfo* pAddr = _das_http_getsrvaddr(pRes, sHost, sPort, sKind, pOpts->eIpVer);
	if(pAddr == NULL) return NULL;

	errno = 0;
	int nFd = socket(pAddr->ai_family, pAddr->ai_socktype, pAddr->ai_protocol);
	if(nFd == -1){
		_http_err(pRes,
			"Couldn't get socket to %s %s, because %s, address info dump follows\n"
			"   ai_family %d  ai_socktype %d  ai_protocol %d",
			sKind, sHost, strerror(errno),
			pAddr->ai_family, pAddr->ai_socktype, pAddr->ai_protocol);
		return NULL;
	}
	daslog_debug_v("Connecting to %s %s, socket info follows\n"
		"ai_family %d  ai_socktype %d  ai_protocol %d  sock_fd %d",  sKind, sHost,
		pAddr->ai_family, pAddr->ai_socktype, pAddr->ai_protocol, nFd
	);

	if((pOpts->rConSec > 0.0) && !_http_setTimeout(nFd, pOpts->rConSec)){
		_http_err(pRes, "Error setting socket timeout value");
		_http_sockClose(nFd);
		return NULL;
	}

	errno = 0;
	if( connect(nFd, pAddr->ai_addr, pAddr->ai_addrlen) == -1){
		if(pOpts->rConSec > 0.0)
			_http_err(pRes, "Couldn't connect to %s %s port %s, within %.3f seconds",
				sKind, sHost, sPort, pOpts->rConSec
			);
		else
			_http_err(pRes, "Couldn't connect to %s %s port %s, %s", sKind,
				sHost, sPort, strerror(errno)
			);
		_http_sockClose(nFd);
		return NULL;
	}

	if(bProxy && !_das_http_tunnel(pRes, nFd, &proxy)){
		_http_sockClose(nFd);
		return NULL;
	}

	/* Blocking SIGPIPE on Apple is a socket option, not a send() flag */
#ifdef __APPLE__
	int nVal = 1;
	if(setsockopt(nFd, SOL_SOCKET, SO_NOSIGPIPE, (void*)&nVal, sizeof(nVal)) != 0){
		_http_err(pRes, "Couldn't set SO_NOSIGPIPE");
		_http_sockClose(nFd);
		return NULL;
	}
#endif

	DasXport* pX = NULL;
	if(pRes->bHttps){
		if(! das_http_setup_ssl()){
			_http_err(pRes, "Couldn't set up the TLS library");
			_http_sockClose(nFd);
			return NULL;
		}

		/* Create a new SSL object, keep the mutex lock until we are done
		 * reading /generating any error strings */
		pthread_mutex_lock(&g_mtxHttp);
		daslog_debug_v("Creating new SSL session for fd %d", nFd);
		SSL* pSsl = SSL_new(g_pSslCtx);
		pthread_mutex_unlock(&g_mtxHttp);

		if(pSsl == NULL){
			char* sErr = _http_get_ssl_global_errors();
			_http_err(pRes, "%s", sErr);
			free(sErr);
			_http_sockClose(nFd);
			return NULL;
		}

		if( SSL_set_fd(pSsl, nFd) != 1){
			char* sErr = _http_get_ssl_global_errors();
			_http_err(pRes, "%s", sErr);
			free(sErr);
			SSL_free(pSsl);
			_http_sockClose(nFd);
			return NULL;
		}

		SSL_set_mode(pSsl, SSL_MODE_AUTO_RETRY);

		/* Shared hosting serves many names from one address and picks the
		   certificate by this name (SNI).  Not sent for an IP literal. */
		if(!pRes->bIp6 && (strspn(pRes->sHost, "0123456789.") != strlen(pRes->sHost)))
			SSL_set_tlsext_host_name(pSsl, pRes->sHost);

		int nRet = SSL_connect(pSsl);
		if(nRet != 1){
			char* sErr = _http_sslErr(pSsl, nRet);
			_http_err(pRes, "TLS handshake with %s failed, %s", pRes->sHost, sErr);
			free(sErr);
			ERR_clear_error();
			SSL_free(pSsl);
			_http_sockClose(nFd);
			return NULL;
		}

		pX = _new_DasXport_ssl(pSsl, "rw");  /* owns pSsl and nFd from here */
		if(pX == NULL){
			SSL_free(pSsl);
			_http_sockClose(nFd);
			_http_err(pRes, "Couldn't make a TLS transport");
			return NULL;
		}
	}
	else{
		pX = _new_DasXport_sock(nFd, "rw", true);
		if(pX == NULL){
			_http_sockClose(nFd);
			_http_err(pRes, "Couldn't make a socket transport");
			return NULL;
		}
	}

	/* The connect timeout is over, from here on each read waits rReadSec */
	if(!_http_setTimeout(nFd, pOpts->rReadSec)){
		_http_err(pRes, "Error setting socket timeout value");
		del_DasXport(pX);
		return NULL;
	}
	return pX;
}

/* ************************************************************************* */
/* The request */

static bool _das_http_sendRequest(
	DasHttpResp* pRes, DasXport* pX, const char* sAgent, const char* sAuth,
	const char* sReqHdrs
){
	size_t uPath = 0, uQuery = 0;
	const char* pPath  = das_url_part(pRes->sUrl, &(pRes->parts), DASURL_PATH, &uPath);
	const char* pQuery = das_url_part(pRes->sUrl, &(pRes->parts), DASURL_QUERY, &uQuery);

	/* Sized to fit, the request line carries the whole query */
	size_t uSz = uPath + uQuery + strlen(pRes->sHost) + 512 + DASCRED_AUTH_SZ +
	             (sAgent ? strlen(sAgent) : 0) + (sReqHdrs ? strlen(sReqHdrs) : 0);
	DasBuf* pBuf = new_DasBuf(uSz);
	if(pBuf == NULL){
		_http_err(pRes, "Couldn't allocate %zu bytes for a request", uSz);
		return false;
	}

	DasBuf_printf(pBuf, "GET %.*s%s%.*s HTTP/1.0\r\n",
		(uPath > 0) ? (int)uPath : 1, (uPath > 0) ? pPath : "/", (pQuery != NULL) ? "?" : "",
		(int)uQuery, (pQuery != NULL) ? pQuery : ""
	);
	DasBuf_printf(pBuf, "Host: %s%s%s%s%s\r\n", pRes->bIp6 ? "[" : "", pRes->sHost,
		pRes->bIp6 ? "]" : "", pRes->bDefPort ? "" : ":", pRes->bDefPort ? "" : pRes->sPort
	);

	if((sAgent != NULL)&&(sAgent[0] != '\0'))
		DasBuf_printf(pBuf, "User-Agent: %s\r\n", sAgent);
	else
		DasBuf_printf(pBuf, "User-Agent: " LIBDAS_USER_AGENT "\r\n");

	/* sAuth is the whole value, scheme included */
	if((sAuth != NULL)&&(sAuth[0] != '\0'))
		DasBuf_printf(pBuf, "Authorization: %s\r\n", sAuth);
	if((sReqHdrs != NULL)&&(sReqHdrs[0] != '\0'))
		DasBuf_printf(pBuf, "%s", sReqHdrs);   /* caller's lines, each CRLF ended */
	DasErrCode nRet = DasBuf_printf(pBuf, "Connection: close\r\n\r\n");

	bool bOkay = (nRet == DAS_OKAY) && (_DasXport_drain(pX, pBuf) >= 0);
	if(!bOkay)
		_http_err(pRes, "Error sending request to host %s, %s", pRes->sHost,
			DasXport_error(pX) ? DasXport_error(pX) : "request too large"
		);

	/* The buffer held the Authorization line */
	DasBuf_zero(pBuf);
	del_DasBuf(pBuf);
	return bOkay;
}

/* ************************************************************************* */
/* The reply headers */

bool DasHttpResp_header(
	const DasHttpResp* pRes, const char* sField, char* sBuf, size_t uLen
){
	if(uLen > 0) sBuf[0] = '\0';
	if((pRes == NULL)||(pRes->sHeaders == NULL)||(sField == NULL)||(uLen == 0))
		return false;

	/* Field names are case insensitive (RFC 9110), and servers do differ:
	   Apache sends ETag, GitHub sends etag.  Values are trimmed. */
	size_t uField = strlen(sField);
	const char* pLine = pRes->sHeaders;
	while(*pLine != '\0'){
		const char* pEnd = strstr(pLine, "\r\n");
		size_t uLine = pEnd ? (size_t)(pEnd - pLine) : strlen(pLine);
		if((uLine > uField) && (pLine[uField] == ':') &&
		   (strncasecmp(pLine, sField, uField) == 0)){
			const char* pVal = pLine + uField + 1;
			const char* pValEnd = pLine + uLine;
			while((pVal < pValEnd) && isspace((unsigned char)*pVal)) ++pVal;
			while((pValEnd > pVal) && isspace((unsigned char)pValEnd[-1])) --pValEnd;
			size_t uVal = (size_t)(pValEnd - pVal);
			if(uVal >= uLen) return false;   /* does not fit, never truncated */
			memcpy(sBuf, pVal, uVal);
			sBuf[uVal] = '\0';
			return true;
		}
		if(pEnd == NULL) break;
		pLine = pEnd + 2;
	}
	return false;
}

/* filename=x or filename="x", from a Content-Disposition value */
static void _http_setFilename(DasHttpResp* pRes)
{
	char sDis[512];
	if(!DasHttpResp_header(pRes, "Content-Disposition", sDis, sizeof(sDis))) return;

	const char* pName = strstr(sDis, "filename=");
	if(pName == NULL) return;
	pName += 9;

	size_t uName = 0;
	if(*pName == '"'){
		++pName;
		while((pName[uName] != '\0') && (pName[uName] != '"')) ++uName;
	}
	else
		uName = strcspn(pName, "; \t");
	if(uName == 0) return;

	pRes->sFilename = (char*)calloc(uName + 1, 1);
	if(pRes->sFilename != NULL) memcpy(pRes->sFilename, pName, uName);
}

bool _DasHttpResp_readHdrs(DasHttpResp* pRes, DasXport* pX)
{
	_http_clearReply(pRes);

	char* sBuf = (char*)malloc(_HTTP_HDR_MAX + 1);
	if(sBuf == NULL){
		_http_err(pRes, "Couldn't allocate a header buffer");
		return false;
	}

	/* Read in chunks until the blank line.  Whatever came past it is the
	   start of the body, and goes back to the transport for the next reader. */
	size_t uLen = 0;
	size_t uHdr = 0;
	while(uHdr == 0){
		if(uLen == _HTTP_HDR_MAX){
			_http_err(pRes, "HTTP headers from %s are over %d bytes", pRes->sHost,
				_HTTP_HDR_MAX
			);
			free(sBuf);
			return false;
		}
		ptrdiff_t nRead = DasXport_read(pX, sBuf + uLen, _HTTP_HDR_MAX - uLen);
		if(nRead < 0){
			_http_err(pRes, "Error reading from host %s, %s", pRes->sHost,
				DasXport_error(pX)
			);
			free(sBuf);
			return false;
		}
		if(nRead == 0){
			_http_err(pRes, "Host %s closed the connection before sending all "
				"headers", pRes->sHost
			);
			free(sBuf);
			return false;
		}

		/* Back up 3 in case the blank line straddles two reads */
		size_t uFrom = (uLen > 3) ? uLen - 3 : 0;
		uLen += (size_t)nRead;
		for(size_t u = uFrom; u + 4 <= uLen; ++u){
			if(memcmp(sBuf + u, "\r\n\r\n", 4) == 0){ uHdr = u + 4; break; }
		}
	}
	if(uLen > uHdr)
		_DasXport_unread(pX, sBuf + uHdr, uLen - uHdr);
	sBuf[uHdr] = '\0';

	/* Status line, "HTTP/1.1 200 OK" */
	if((strncmp(sBuf, "HTTP/", 5) != 0)||(strchr(sBuf, ' ') == NULL)){
		_http_err(pRes, "Malformed header from host %s", pRes->sHost);
		free(sBuf);
		return false;
	}
	const char* pCode = strchr(sBuf, ' ') + 1;
	for(int i = 0; i < 3; ++i){
		if(!isdigit((unsigned char)pCode[i])){
			_http_err(pRes, "Malformed status line from host %s", pRes->sHost);
			free(sBuf);
			return false;
		}
	}
	pRes->nCode = (pCode[0] - '0')*100 + (pCode[1] - '0')*10 + (pCode[2] - '0');

	pRes->sHeaders = sBuf;   /* kept for every reply, see DasHttpResp_header() */

	char sType[256];
	if(DasHttpResp_header(pRes, "Content-Type", sType, sizeof(sType)))
		pRes->sMime = das_strdup(sType);
	_http_setFilename(pRes);
	return true;
}

/* ************************************************************************* */
/* Making requests */

DasErrCode das_http_opts_init_ver(das_http_opts* pOpts, size_t uVersion)
{
	/* A later library adds cases here for each older layout it can still
	   serve, clearing and defaulting only the members that layout has */
	if(uVersion != DAS_HTTP_OPTS_V3_0)
		return das_error(DASERR_HTTP,
			"Unknown das_http_opts version 0x%04zx, this library knows 0x%04x",
			uVersion, DAS_HTTP_OPTS_VER
		);

	memset(pOpts, 0, sizeof(das_http_opts));
	pOpts->uVersion = uVersion;
	pOpts->rConSec = DASHTTP_TO_MIN;
	pOpts->eIpVer = DASIP_ANY;
	return DAS_OKAY;
}

/* Turn a Location value into the next URL.  Absolute URLs pass as is; a
   path on the same server ("/new/place") is completed from the current URL.
   Other relative forms are rare from das servers and refused. */
static bool _http_nextUrl(DasHttpResp* pRes, const char* sLoc, char* sOut, size_t uOut)
{
	if(sLoc[0] != '/'){
		if(strlen(sLoc) >= uOut) return false;
		strcpy(sOut, sLoc);
		return true;
	}
	int n = snprintf(sOut, uOut, "%s://%s%s%s%s%s%s", pRes->bHttps ? "https" : "http",
		pRes->bIp6 ? "[" : "", pRes->sHost, pRes->bIp6 ? "]" : "",
		pRes->bDefPort ? "" : ":", pRes->bDefPort ? "" : pRes->sPort, sLoc
	);
	return (n > 0) && ((size_t)n < uOut);
}

DasHttpResp* new_DasHttpResp_get(
	const char* sUrl, const char* sAgent, DasCredMngr* pMgr, float rConSec
){
	das_http_opts opts;
	das_http_opts_init(&opts);
	opts.sAgent = sAgent;
	opts.rConSec = rConSec;
	return new_DasHttpResp_getEx(sUrl, pMgr, &opts);
}

DasHttpResp* new_DasHttpResp_getEx(
	const char* sUrl, DasCredMngr* pMgr, const das_http_opts* pOpts
){
	DasHttpResp* pRes = _new_DasHttpResp();
	if(pRes == NULL) return NULL;

	/* Work from a copy in this library's own layout.  Only one layout exists
	   so far; a later library copies just the members the caller's version
	   has, and members it didn't know about keep their defaults. */
	das_http_opts opts;
	das_http_opts_init(&opts);
	if(pOpts != NULL){
		/* A zeroed struct (memset, = {0}) would otherwise run on defaults and
		   quietly drop every setting the caller made */
		if(pOpts->uVersion == 0){
			_http_err(pRes, "das_http_opts was not initialized, start it with "
			          "das_http_opts_init()");
			return pRes;
		}
		if(pOpts->uVersion != DAS_HTTP_OPTS_V3_0){
			_http_err(pRes, "das_http_opts version is newer than this library");
			return pRes;
		}
		memcpy(&opts, pOpts, sizeof(opts));
	}
	if(sUrl == NULL){
		_http_err(pRes, "No URL given");
		return pRes;
	}
	if(!_http_setUrl(pRes, sUrl)) return pRes;

	char sAuth[DASCRED_AUTH_SZ] = {'\0'};  /* wiped on the way out */
	int16_t nCred = 0;
	int nHops = 0;
	char sServer[_HTTP_SZ_HOST + 512];
	char sDataset[256];
	char sRealmHdr[512];
	char sLoc[2048];
	char sNext[2048];

	while(true){

		DasXport* pX = _das_http_connect(pRes, &opts);
		if(pX == NULL) break;

		if(!_das_http_sendRequest(pRes, pX, opts.sAgent, sAuth, opts.sReqHdrs) ||
		   !_DasHttpResp_readHdrs(pRes, pX)){
			del_DasXport(pX);
			break;
		}

		_http_server(pRes, sServer, sizeof(sServer));
		if(!_http_partCopy(pRes->sUrl, &(pRes->parts), DASURL_DATASET, sDataset, sizeof(sDataset)))
			sDataset[0] = '\0';  /* too long to be a das2 dataset, no per-dataset login */

		switch(pRes->nCode){
		case HTTP_OK:
			/* The request is sent, what is left is a body to read.  Saying so
			   lets new_DasIO() take its direction from the transport. */
			pX->uFlags &= ~DASXPORT_WR;
			pRes->pX = pX;
			pRes->bOk = true;
			goto DONE;

		case HTTP_NotModified:
			/* Only reachable when sReqHdrs carried a validator.  There is no
			   body, so the connection goes now; the headers and code stay. */
			del_DasXport(pX);
			pRes->bOk = true;
			goto DONE;

		case HTTP_MovedPerm:
		case HTTP_Found:
		case HTTP_TempRedir:
		case HTTP_PermRedir:
			del_DasXport(pX);
			if(++nHops > _HTTP_MAX_HOPS){
				_http_err(pRes, "More than %d redirects starting from %s", _HTTP_MAX_HOPS, sUrl);
				goto DONE;
			}
			if(!DasHttpResp_header(pRes, "Location", sLoc, sizeof(sLoc))){
				_http_err(pRes, "Couldn't find Location header in redirect "
					"message from host %s", pRes->sHost);
				goto DONE;
			}
			if(!_http_nextUrl(pRes, sLoc, sNext, sizeof(sNext))){
				_http_err(pRes, "Can't follow redirect to %s from host %s", sLoc, pRes->sHost);
				goto DONE;
			}
			daslog_debug_v("Redirected to: %s", sNext);
			if(!_http_setUrl(pRes, sNext)) goto DONE;
			break;

		case HTTP_AuthReq:
			del_DasXport(pX);

			/* A 401 after sending credentials means they were refused.  Only a
			   401 says that; 403 and the rest say nothing about the password. */
			if((nCred > 0)&&(pMgr != NULL)){
				_DasCredMngr_authFailed(pMgr, nCred,
					"Credentials not accepted by remote server");
			}

			if(pMgr == NULL){
				_http_err(pRes, "Auth required for dataset %s on server %s, but no "
					"credentials manager was supplied.", sDataset, sServer
				);
				goto DONE;
			}
			else{
				/* Dig out the realm, which is optional */
				const char* pRealm = NULL;
				if(DasHttpResp_header(pRes, "WWW-Authenticate", sRealmHdr, sizeof(sRealmHdr))){
					char* p = strstr(sRealmHdr, "realm=\"");
					if(p != NULL){
						pRealm = p + 7;
						char* pEnd = strchr(p + 7, '"');
						if(pEnd != NULL) *pEnd = '\0';
					}
				}
				nCred = _DasCredMngr_getHttpAuth(
					pMgr, sServer, pRealm, sDataset, sAuth, sizeof(sAuth)
				);
				if(nCred < 1){
					_http_err(pRes, "Credentials manager did not supply an "
						"authentication token for dataset %s on server %s%s", sDataset,
						sServer, (DasCredMngr_getPrompt(pMgr, NULL) == NULL) ?
						", and it has no prompt, see DasCredMngr_setPrompt()" : ""
					);
					goto DONE;
				}
			}
			break;

		case HTTP_Forbidden:
			del_DasXport(pX);
			_http_err(pRes, "Access to dataset '%s' on server '%s' was forbidden",
				sDataset, sServer
			);
			goto DONE;

		case HTTP_NotFound:{
			size_t uPath = 0;
			const char* pPath = das_url_part(pRes->sUrl, &(pRes->parts), DASURL_PATH, &uPath);
			del_DasXport(pX);
			_http_err(pRes, "Error in request path '%.*s' for host '%s'",
				(int)uPath, pPath ? pPath : "", pRes->sHost
			);
			goto DONE;
		}

		case HTTP_BadReq:{
			size_t uQuery = 0;
			const char* pQuery = das_url_part(pRes->sUrl, &(pRes->parts), DASURL_QUERY, &uQuery);
			del_DasXport(pX);
			_http_err(pRes, "Error in query parameters '%.*s'", (int)uQuery,
				pQuery ? pQuery : ""
			);
			goto DONE;
		}

		default:
			del_DasXport(pX);
			_http_err(pRes, "Server returned HTTP status %d when accessing %s",
				pRes->nCode, pRes->sUrl
			);
			goto DONE;
		}
	}

DONE:
	_dascred_wipe(sAuth, sizeof(sAuth));
	return pRes;
}

/* ************************************************************************* */
/* A just-give-me-a-bag-of-bytes convenience function */

DasAry* das_http_readUrl(
	const char* sUrl, const char* sAgent, DasCredMngr* pMgr, DasHttpResp** ppRes,
	int64_t nLimit, float rConSec
){
	das_http_opts opts;
	das_http_opts_init(&opts);
	opts.sAgent = sAgent;
	opts.rConSec = rConSec;
	return das_http_readUrl_ex(sUrl, pMgr, &opts, nLimit, ppRes);
}

DasAry* das_http_readUrl_ex(
	const char* sUrl, DasCredMngr* pMgr, const das_http_opts* pOpts,
	int64_t nLimit, DasHttpResp** ppRes
){
	if(ppRes != NULL) *ppRes = NULL;

	DasHttpResp* pRes = new_DasHttpResp_getEx(sUrl, pMgr, pOpts);
	if(pRes == NULL) return NULL;

	DasAry* pAry = NULL;
	DasXport* pX = DasHttpResp_takeBody(pRes);   /* NULL on failure or 304 */
	if(pX != NULL){
		if(nLimit < 1) nLimit = -1;
		int64_t nTotal = 0;

		pAry = new_DasAry("http_body", vtUByte, 1, (const ubyte[1]){0}, RANK_1(0), UNIT_DIMENSIONLESS);

		ubyte aBuf[_HTTP_CHUNK_SZ];
		while((pAry != NULL) && ((nLimit == -1)||(nTotal < nLimit))){
			ptrdiff_t nRead = DasXport_read(pX, aBuf, _HTTP_CHUNK_SZ);
			if(nRead == 0) break;      /* Socket is done */
			if(nRead < 0){             /* Socket is broke */
				daslog_error_v("Error reading body of %s, %s", sUrl, DasXport_error(pX));
				dec_DasAry(pAry);
				pAry = NULL;
				break;
			}
			nTotal += nRead;
			if(!DasAry_append(pAry, aBuf, (size_t)nRead)){
				dec_DasAry(pAry);
				pAry = NULL;
				break;
			}
		}
		del_DasXport(pX);

		if(pAry != NULL){
			if((nLimit > 0)&&(nTotal > nLimit))
				daslog_warn_v("Limit of %" PRId64 " bytes hit, almost certainly returning a "
				              "partial download", nLimit);
			else
				daslog_debug_v("%zu bytes read from %s", (size_t)DasAry_size(pAry), sUrl);
		}
	}

	if(ppRes != NULL) *ppRes = pRes;
	else del_DasHttpResp(pRes);
	return pAry;
}
