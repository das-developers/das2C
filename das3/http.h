/* Copyright (C) 2017 - 2026 Chris Piker <chris-piker@uiowa.edu>
 *
 * This file is part of das2C, the Core Das2 C Library.
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

#ifndef _das_http_h_
#define _das_http_h_

#include <stdbool.h>
#include <stdio.h>

#include <das3/array.h>
#include <das3/credentials.h>

#ifdef __cplusplus
extern "C" {
#endif

/** @file http.h functions for reading and writing http messages */
	
/** @addtogroup IO
 * @{
 */

/* Sizes of the parts of struct das_url, not counting the terminator.  A URL
 * with a part longer than this is refused by DasHttpResp_init(), it is never
 * sent truncated. */
#define DASURL_SZ_SCHEME   31
#define DASURL_SZ_HOST     63
#define DASURL_SZ_PATH    255
#define DASURL_SZ_QUERY   3071
#define DASURL_SZ_DATASET  63
#define DASURL_SZ_PORT      7

/** Get a new string allocated on the heap explaining an SSL error
 * or NULL in nRet == 0.
 * 
 * To prevent memory leaks, caller must free string if return is NON null.
 */
DAS_API char* das_ssl_getErr(const void* vpSsl, int nRet);


/* Recommended time out values for HTTP Connections, these settings result
 * in an initial expectation of 2 second response, 2 retries and a max 
 * connection timeout of 18.0 seconds.  Probably too generous  */
#define DASHTTP_TO_MIN     2.0
#define DASHTTP_TO_MULTI   3.0
#define DASHTTP_TO_MAX    18.0


/** A parsed URL structure */
struct das_url {
	/** The scheme string */
	char sScheme[DASURL_SZ_SCHEME+1];
	/** The host string */
	char sHost[DASURL_SZ_HOST+1];
	/** The path on the host */
	char sPath[DASURL_SZ_PATH+1];
	/** The query string */
	char sQuery[DASURL_SZ_QUERY+1];
	/** The dataset identified in the query string (if any) */
	char sDataset[DASURL_SZ_DATASET+1];
	
	/** The port number used to make the request, saved as a string */
	char sPort[DASURL_SZ_PORT+1];
};

/** Encapsulates the status of a HTTP resource request */
typedef struct das_http_response_t{
	
	/** The socket file descriptor that can be used to read the message body.
	 * A value of -1 indicates that the connection could not be made if 
	 * pSsl is also NULL */
	int nSockFd;
	
	/** The SSL Connection if using HTTPS.  If both this value and nSockFd
	 * are null it means the connection failed*/
	void* pSsl;
	
	/** The HTTP status code returned by the server (if any) */
	int nCode; 
	
	/** An error message created by the library if a problem occurred */
	char* sError;
	
	/** The full HTTP header set from the final response */
	char* sHeaders;
	
	/** The parsed out mime-type string from the headers */
	char* pMime;
	
	/** The filename (if any) provided for the message body */
	char* sFilename;
	
	/** The parsed URL structure that was used to make the connection */
	struct das_url url;
	
} DasHttpResp;

/** @} */

/** Convert a URL structure into a string
 *
 * The scheme's default port is left out, any other is written after a colon.
 *
 * @param pUrl The parsed URL
 * @param sBuf The output buffer
 * @param uLen The size of sBuf, including room for the NUL
 * @returns sBuf, or NULL if the URL has no scheme or host or the whole
 *          string did not fit.  sBuf is always NUL terminated when uLen > 0.
 * @memberof das_url
 */
DAS_API char* das_url_toStr(const struct das_url* pUrl, char* sBuf, size_t uLen);


/** Initialize all fields in an http response to default values.
 * 
 * All char fields are set to zero, the file descriptor (nSockFd) and the
 * response code are set to -1.  This function is called automatically by
 * das_http_getBody() so there is usually no need to use it directly.
 * 
 * @param pRes The response to clear
 * @memberof DasHttpResp
 */
DAS_API void DasHttpResp_clear(DasHttpResp* pRes);

/** Free any fields that contain allocated memory 
 * This will not free the pRes structure itself, only sub-items such as 
 * pMime etc.  To free the over all structure (if it was allocated on the 
 * heap) call free(pRes).
 */
DAS_API void DasHttpResp_freeFields(DasHttpResp* pRes);

/** Initialize the das_url component of an HTTP response
 * 
 * The URL will be parsed into a das_url structure and stored internally.  
 * 
 * @param pRes The response to initialize
 * @param sUrl The fully qualified URL including the scheme, host, port (if any),
 *             path and query parameters.
 * @returns true if every part fit its field in struct das_url.  False, with
 *          pRes->sError set, if the scheme is not http or https, the host
 *          or port is bad, or a part is longer than its DASURL_SZ_ limit.
 *          Nothing is truncated.
 * @memberof DasHttpResp
 */
DAS_API bool DasHttpResp_init(DasHttpResp* pRes, const char* sUrl);


/** Returns true if the response is an SSL (Secure Socket Layer) connection 
 * @memberof DasHttpResp
 */
DAS_API bool DasHttpResp_useSsl(DasHttpResp* pRes);

/** Get a socket positioned at the start of a remote resource
 * 
 * This function makes a connection to a remote HTTP or HTTPS server, reads
 * through the HTTP headers and then passes the socket descriptor and possibly
 * an SSL connection object back to the caller.
 * 
 * If a redirect response is detected, the initial connection is dropped and a
 * new one is made to the indicated location.
 * 
 * If the GET request fails due to needing authentication, the given credential
 * manager is consulted for a username and password.  If one is present that 
 * matches the URL path (and optionally the dataset, see below) then it is used,
 * otherwise the manager's getPassword function is called and another attempt is
 * made.  The cycle continues until the connection succeeds or getPassword
 * fails.
 * 
 * Meta details about the connection including the body mime-type and any 
 * error messages are stored in the sHeaders element of the DasHttpResp
 * structure.
 * 
 * It is the caller's responsibility to call shutdown for the provided
 * socket descriptor or the SSL connection when it is finished reading the
 * message body.
 * 
 * <b>das2.2 Note:</b> The das2.2 protocol lets a server require a different
 * login for each dataset it serves (das2.2 ICD, https://zenodo.org/records/3588535).
 * So when the query string carries a dataset= value, credentials are looked
 * up and saved under http://SERVER/path?dataset=DATASET rather than
 * http://SERVER/path.  This is part of the das2.2 interface, which das2C
 * supports alongside das3.
 * 
 * @param sUrl The location to connect to, sent as given.  No encoding is
 *             applied here, so reserved characters in parameter values must
 *             already be percent encoded by the caller.  Each part must fit
 *             its field in struct das_url, see DasHttpResp_init().
 * 
 * @param sAgent The user agent string you wish to send to the server.  If NULL 
 *        then the string "libdas/3" is sent.
 * 
 * @param pMgr A credentials manager object to consult if a password is
 *             requested.  May be set to NULL to indicate that only public 
 *             items may be requested.
 * 
 * @param pRes A pointer to a response object that will be filled in with the
 *             result of the connection attempt.  This contains the headers 
 *             error messages etc.
 * 
 * @param rConSec If > 0.0 this is the floating point number of seconds to
 *             wait on a connection before giving up.  If 0.0 or less then
 *             operating system defaults are used.  Typical OS default is about
 *             five minutes.  This value only affects the initial connection
 *             timeout and not the wait time for data to appear.
 * 
 * @return true if pRes->nSockFd, or pRes->pSsl is ready for reading, false 
 *         otherwise.
 */
DAS_API bool das_http_getBody(
	const char* sUrl, const char* sAgent, DasCredMngr* pMgr, DasHttpResp* pRes,
	float rConSec
);
 
/** Read all the bytes for a URL into a byte array
 * 
 * Since network operations are presumed to fail often, all errors are 
 * logged using the functions in log.h, das_error is not called unless a memory
 * allocation error occurs.
 * 
 * @param sUrl - The URL to read, may be http or https contain alternate port
 *               numbers etc.
 * 
 * @param sAgent The user agent string you wish to send to the server.  If 
 *               NULL then the string "libdas/3" is sent.
 * 
 * @param pMgr   A credentials manager object to consult if a password is
 *               requested.  May be set to NULL to indicate that only public 
 *               items may be requested.
 * 
 * @param pRes   A pointer to a http response object that will hold the headers
 *               from the final destination.  Due to redirects the URL in 
 *               the response may not be the same as the initial one provided
 *               in sUrl
 * 
 * @param nLimit An upper limit on the number of bytes to download, if less than
 *               1 then limit checks are disabled and download will continue 
 *               until complete, a socket error occurs, or no additional 
 *               memory can be allocated.  The actual amount of data read maybe
 *               upto 32K - 1 bytes more than the given limit.
 * 
 * * @param rConSec If > 0.0 this is the floating point number of seconds to
 *             wait on a connection before giving up.  If 0.0 or less then
 *             operating system defaults are used.  Typical OS default is about
 *             five minutes.  This value only affects the initial connection
 *             timeout and not the wait time for data to appear.
 * 
 * @return       A 1-dimensional DasAry with element type vtUByte allocated on
 *               the heap, or NULL if the download failed.  
 * 
 *               The ID member of the allocated array will correspond to the
 *               last component of the URL path, not including any fragments,
 *               or if accessing the root of a server, then the host name will
 *               be used.
 */
DAS_API DasAry* das_http_readUrl(
	const char* sUrl, const char* sAgent, DasCredMngr* pMgr, DasHttpResp* pRes,
	int64_t nLimit, float rConSec
);

/** Progress callback for das_http_copyBody()
 *
 * Follows the StreamHandler convention: a non-zero return stops the copy
 * and becomes the return value of das_http_copyBody().  Use a code of your
 * own rather than DASERR_HTTP or DASERR_IO, otherwise a deliberate stop
 * can't be told from a transport or write failure.
 *
 * @param nBytes The number of body bytes written so far
 * @param pUser  The opaque pointer handed to das_http_copyBody()
 * @returns DAS_OKAY to keep copying, anything else to stop
 */
typedef DasErrCode (*das_progress_f)(int64_t nBytes, void* pUser);

/** Copy the message body of an open HTTP response to a FILE
 *
 * The streaming counterpart to das_http_readUrl().  Call das_http_getBody()
 * first, then hand the response here to drain the socket, or SSL connection,
 * into pOut.  Only one chunk is held in memory at a time so bodies of any
 * size may be copied.
 *
 * The connection is closed before returning, whether or not the copy
 * succeeded, so afterwards pRes->nSockFd is -1 and pRes->pSsl is NULL.
 * The remaining response fields are untouched, release them with
 * DasHttpResp_freeFields().
 *
 * Errors are logged using the functions in log.h in the manner of 
 * das_http_readUrl(), das_error() is not called.
 *
 * @param pRes       A response for which das_http_getBody() returned true
 * @param pOut       Destination for the body bytes.  No flush is performed.
 * @param pCopied    If not NULL, receives the number of bytes written to pOut.
 *                   Set even on failure or an early stop.
 * @param onProgress If not NULL, called after each chunk is written.  A
 *                   non-zero return stops the copy.
 * @param pUser      Passed through to onProgress untouched
 *
 * @returns DAS_OKAY if the whole body was copied, DASERR_HTTP if reading
 *          from the server failed, DASERR_IO if writing to pOut failed, or
 *          whatever onProgress returned if it stopped the copy.
 */
DAS_API DasErrCode das_http_copyBody(
	DasHttpResp* pRes, FILE* pOut, int64_t* pCopied, das_progress_f onProgress,
	void* pUser
);

/** @} */
 
#ifdef __cplusplus
}
#endif

#endif /* _das_http_h_ */

