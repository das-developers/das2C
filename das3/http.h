/* Copyright (C) 2017 - 2026 Chris Piker <chris-piker@uiowa.edu>
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

/** @file http.h
 * Reading resources from http and https servers, see DasHttpResp
 */

#ifndef _das_http_h_
#define _das_http_h_

#include <stdbool.h>
#include <stddef.h>

#include <das3/array.h>
#include <das3/creds.h>
#include <das3/xport.h>

#ifdef __cplusplus
extern "C" {
#endif

/** @defgroup network Networking
 * Fetching resources from remote servers, and the logins they ask for
 */

/** @addtogroup network
 * @{
 */

/* Suggested connection timeouts in seconds, for the rConSec arguments below.
 * DASHTTP_TO_MIN suits interactive programs. */
#define DASHTTP_TO_MIN     2.0
#define DASHTTP_TO_MULTI   3.0
#define DASHTTP_TO_MAX    18.0


/** Which IP version to connect over, see das_http_opts */
typedef enum das_ip_ver {
	DASIP_ANY = 0,   /**< IPv4 if the host has an IPv4 address, else IPv6 */
	DASIP_V4  = 4,   /**< IPv4 only */
	DASIP_V6  = 6    /**< IPv6 only */
} das_ip_ver_e;

/** Settings for one request, see new_DasHttpResp_getEx()
 *
 * Start from das_http_opts_init() and change what you need:
 *
 * @code
 *   das_http_opts opts;
 *   das_http_opts_init(&opts);
 *   opts.rReadSec = 30.0;    // give up on a server silent for 30 seconds
 *   opts.eIpVer = DASIP_V6;
 *   DasHttpResp* pRes = new_DasHttpResp_getEx(sUrl, pMgr, &opts);
 * @endcode
 *
 * Later versions may add members.  das_http_opts_init() records the layout
 * version the program was built with, so a later library can tell which
 * members the program knows about and give the rest their defaults.  That
 * only works if the struct came from das_http_opts_init(), so always start
 * there; a zeroed one, from memset() or "= {0}", is refused with an error
 * rather than quietly ignored.
 */
typedef struct das_http_opts {
	/** The layout this program was compiled against, set by
	 *  das_http_opts_init(), leave it alone */
	size_t uVersion;

	/** The User-Agent to send, NULL for "libdas3" */
	const char* sAgent;

	/** Seconds to wait for the connection, TLS handshake included, before
	 *  giving up.  0.0 or less uses the operating system default, often
	 *  minutes. */
	float rConSec;

	/** Once connected, seconds to wait for each read before giving up on a
	 *  server that has gone quiet.  0.0 or less waits indefinitely, which is
	 *  the default since some das servers think a long time before the
	 *  first byte. */
	float rReadSec;

	/** Which IP version to connect over, DASIP_ANY by default */
	das_ip_ver_e eIpVer;

	/** Zero or more extra request header lines, each ending in "\r\n",
	 *  sent as given.  NULL for none.  See new_DasHttpResp_getEx(). */
	const char* sReqHdrs;
} das_http_opts;

/** The first das_http_opts layout, das 3.0.  Same numbering as
 * DAS_STREAM_PROC_V3_0. */
#define DAS_HTTP_OPTS_V3_0 ((3 << 8) | 0)

/** The das_http_opts layout described by this header */
#define DAS_HTTP_OPTS_VER DAS_HTTP_OPTS_V3_0

/** Fill in the request defaults for the layout given
 *
 * Use das_http_opts_init() instead, which supplies the version this program
 * was compiled against.
 *
 * @param pOpts The options structure to initialize
 * @param uVersion The structure layout pOpts has, one of the
 *        DAS_HTTP_OPTS_V* values.
 * @returns DAS_OKAY, or an error if uVersion is unknown to this library,
 *        in which case pOpts is not touched.
 */
DAS_API das_errcode das_http_opts_init_ver(das_http_opts* pOpts, size_t uVersion);

/** Fill in the request defaults, the starting point for das_http_opts
 *
 * The user agent is the library default, the connect timeout is
 * DASHTTP_TO_MIN, reads wait indefinitely, any IP version is used, and no
 * extra headers are sent.
 *
 * This is a macro so that the version recorded is the one from the header
 * the calling program was compiled with.  A function inside the library
 * would record the library's own layout, and on a newer library would
 * clear past the end of an older program's smaller structure.
 *
 * @param P A pointer to the das_http_opts to initialize
 * @returns DAS_OKAY, or an error if the library is older than the header
 */
#define das_http_opts_init(P) das_http_opts_init_ver((P), DAS_HTTP_OPTS_VER)


/** @class DasHttpResp http.h <das3/http.h>
 *
 * The outcome of one request to an http or https server.
 *
 * new_DasHttpResp_get() makes the request.  It connects, follows any
 * redirects, answers login requests from a credentials manager, and stops
 * once the headers of the final reply are read.  The body has not been read
 * yet; the response holds the open connection, positioned at its first
 * byte.
 *
 * Take the body with DasHttpResp_takeBody().  It comes out as a DasXport,
 * the same kind of byte source a local file gives, so code that reads it
 * need not care where it came from.  To parse a das stream, hand it to a
 * DasIO:
 *
 * @code
 *   DasHttpResp* pRes = new_DasHttpResp_get(sUrl, "my_prog", pMgr, DASHTTP_TO_MIN);
 *   if(!DasHttpResp_ok(pRes)){
 *       fprintf(stderr, "%s\n", DasHttpResp_error(pRes));
 *       del_DasHttpResp(pRes);
 *       return 1;
 *   }
 *
 *   DasIO* pIn = new_DasIO("my_prog", DasHttpResp_takeBody(pRes), NULL);
 *   del_DasHttpResp(pRes);   // pIn owns the connection now
 *
 *   DasIO_addProcessor(pIn, &handler);
 *   DasIO_readAll(pIn);
 *   del_DasIO(pIn);
 * @endcode
 *
 * To save the body, or handle the raw bytes, use DasXport_copyTo() or
 * DasXport_read() on the taken body.  For something small, such as a
 * catalog file, das_http_readUrl() makes the request and reads the whole
 * body into memory in one call.
 *
 * Taking the body moves the connection out of the response, so the two are
 * freed independently and in either order.  Everything else stays with the
 * response: the status code, the final URL and the headers can still be
 * read.  Deleting a response whose body was never taken closes the
 * connection, so nothing leaks when a program bails out early.
 *
 * @par When a request fails
 *
 * The constructor still returns a response; it is the record of what went
 * wrong.  DasHttpResp_ok() is false, DasHttpResp_error() gives a message
 * suitable for showing a user, and DasHttpResp_code() has the server's HTTP
 * status, or -1 if no server answered.  The code is how a program tells a
 * login problem from a bad request when it wants to handle them
 * differently:
 *
 * @code
 *   switch(DasHttpResp_code(pRes)){
 *   case -1:  // no connection: DNS failure, timeout, refused ...
 *   case 401:
 *   case 403: // ask for a different login, or give up
 *   case 400:
 *   case 404: // fix the request
 *   default:  // anything else
 *   }
 * @endcode
 *
 * A response is not thread safe; do not share one between threads without
 * locking.  Separate responses in separate threads are fine.
 */
typedef struct das_http_resp DasHttpResp;


/** Request a remote resource, stopping at the start of its body
 *
 * This connects to an http or https server, sends a GET request, and reads
 * the reply headers.  Redirects are followed to their end.  See DasHttpResp
 * for what to do with the result.
 *
 * Unlike most constructors this does not return NULL when the request
 * fails.  The response is the report: check DasHttpResp_ok(), and read
 * DasHttpResp_error() and DasHttpResp_code() to find out why.
 *
 * If the server asks for a login, the credentials manager is consulted.  A
 * login it already holds for the server is sent; otherwise it prompts, if it
 * was given a way to, and the request is tried again.  This repeats until
 * the server accepts a login or the manager has nothing more to offer.  A
 * login the server refuses is marked as rejected in the manager.  See
 * DasCredMngr.
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
 *             already be percent encoded by the caller, see das_url_encode().
 *
 * @param sAgent The user agent string to send to the server.  If NULL
 *        then the string "libdas/3" is sent.
 *
 * @param pMgr A credentials manager to consult if the server asks for a
 *             login.  NULL means only public resources can be read.
 *
 * @param rConSec If > 0.0 this is the floating point number of seconds to
 *             wait on a connection before giving up.  If 0.0 or less then
 *             operating system defaults are used.  Typical OS default is about
 *             five minutes.  This value only affects the initial connection
 *             timeout and not the wait time for data to appear.
 *
 * @returns A new response, free it with del_DasHttpResp(), or NULL only if
 *          allocation fails
 * @memberof DasHttpResp
 */
DAS_API DasHttpResp* new_DasHttpResp_get(
	const char* sUrl, const char* sAgent, DasCredMngr* pMgr, float rConSec
);

/** new_DasHttpResp_get() with full control over the request
 *
 * Use this to set timeouts, the IP version, or extra request headers, see
 * das_http_opts.
 *
 * The one use of extra headers today is cache revalidation.  A caller
 * holding a copy of a resource along with the ETag or Last-Modified header
 * it came with sends them back:
 *
 * @code
 * das_http_opts ops;
 * das_http_opts_init(&ops);   // not memset, see das_http_opts
 * ops.sReqHdrs =
 *    "If-None-Match: \"5a6-607e10c29b347\"\r\n"
 *    "If-Modified-Since: Tue, 17 Oct 2023 03:23:33 GMT\r\n"
 * ;
 * DasHttpResp* pRes = new_DasHttpResp_getEx(sMyUrl, NULL, &ops);
 @endcode
 *
 * and a server that still has that version answers 304 Not Modified with
 * no body.  Send both validators when both are known: a server with an
 * ETag ignores If-Modified-Since (RFC 9110) and one without an ETag can
 * only use the date.  Values go back exactly as the server sent them,
 * see DasHttpResp_header().
 *
 * @param sUrl The location to connect to, as for new_DasHttpResp_get()
 * @param pMgr A credentials manager, or NULL for public items only
 * @param pOpts Request settings, or NULL for the das_http_opts_init()
 *        defaults
 *
 * @returns As new_DasHttpResp_get().  A server that reports the copy is
 *        current gives DasHttpResp_ok() true with DasHttpResp_code() 304
 *        and no body to take; the 304 reply's headers are available.
 *        Callers that sent no validator never see this.
 * @memberof DasHttpResp
 */
DAS_API DasHttpResp* new_DasHttpResp_getEx(
	const char* sUrl, DasCredMngr* pMgr, const das_http_opts* pOpts
);

/** Free a response, closing its connection if the body was never taken
 *
 * @param pThis The response to free.  May be NULL, in which case nothing is
 *        done.
 * @memberof DasHttpResp
 */
DAS_API void del_DasHttpResp(DasHttpResp* pThis);

/** Did the request succeed?
 *
 * @returns true if the body is ready to take, or for a conditional request
 *          the server answered 304, see new_DasHttpResp_getEx().  false if
 *          the request failed, see DasHttpResp_error().
 * @memberof DasHttpResp
 */
DAS_API bool DasHttpResp_ok(const DasHttpResp* pThis);

/** Get the HTTP status code of the last reply
 *
 * After redirects this is the code from the final server, 200 for a
 * successful request.
 *
 * @returns The status code, or -1 if no server answered
 * @memberof DasHttpResp
 */
DAS_API int DasHttpResp_code(const DasHttpResp* pThis);

/** Get the reason the request failed
 *
 * @returns A message suitable for showing a user, or NULL if the request
 *          succeeded.  Owned by the response.
 * @memberof DasHttpResp
 */
DAS_API const char* DasHttpResp_error(const DasHttpResp* pThis);

/** Get the media type of the body
 *
 * @returns The Content-Type header of the final reply, for example
 *          "application/vnd.das.stream", or NULL if the server sent none.
 *          Owned by the response.
 * @memberof DasHttpResp
 */
DAS_API const char* DasHttpResp_mime(const DasHttpResp* pThis);

/** Get the file name the server suggests for the body
 *
 * Servers that offer a body as a download name it in a Content-Disposition
 * header.  The name comes from the server, so check it before using it as a
 * local path.
 *
 * @returns The file name, or NULL if the server gave none.  Owned by the
 *          response.
 * @memberof DasHttpResp
 */
DAS_API const char* DasHttpResp_filename(const DasHttpResp* pThis);

/** Get the URL of the request, after any redirects
 *
 * When a request succeeds this is where the body came from, which is often
 * not the URL the program asked for.  When it fails this is the last place
 * tried, which is useful in error messages.  To get at its parts, use
 * das_url_parse() and das_url_part().
 *
 * @returns The URL.  Owned by the response.
 * @memberof DasHttpResp
 */
DAS_API const char* DasHttpResp_url(const DasHttpResp* pThis);

/** Get the value of one header from the final reply
 *
 * Field names match without regard to case, as HTTP requires.  The value is
 * copied with surrounding white space removed.  Only the headers of the
 * final reply are searched, those of any redirect on the way are gone.  They
 * are kept whatever the status, so a failed request can be examined too,
 * WWW-Authenticate on a 401 for example.
 *
 * @param pThis The response whose headers to search
 * @param sField The header name, for example "ETag" or "Last-Modified"
 * @param sBuf Receives the value, always NUL terminated when uLen > 0
 * @param uLen Size of sBuf including the terminator
 * @returns true if the header is present and its value fit, false if it is
 *          absent, there are no headers, or the value would have been
 *          truncated
 * @memberof DasHttpResp
 */
DAS_API bool DasHttpResp_header(
	const DasHttpResp* pThis, const char* sField, char* sBuf, size_t uLen
);

/** Take the body out of a response
 *
 * The body comes out as a transport positioned at its first byte; read it
 * with DasXport_read(), copy it with DasXport_copyTo(), or parse it with
 * new_DasIO().  The caller owns it from here on and frees it with
 * del_DasXport(), or with the DasIO it was given to.  The response keeps
 * its code, URL and headers.
 *
 * @returns The body, or NULL if there is none to take: the request failed,
 *          the reply was a 304, or the body was already taken
 * @memberof DasHttpResp
 */
DAS_API DasXport* DasHttpResp_takeBody(DasHttpResp* pThis);

/** Read all the bytes for a URL into a byte array
 *
 * Since network operations are presumed to fail often, all errors are
 * logged using the functions in log.h, das_error is not called unless a memory
 * allocation error occurs.
 *
 * @param sUrl - The URL to read, may be http or https contain alternate port
 *               numbers etc.
 *
 * @param sAgent The user agent string to send to the server.  If NULL then
 *               the string "libdas/3" is sent.
 *
 * @param pMgr   A credentials manager to consult if the server asks for a
 *               login.  NULL means only public resources can be read.
 *
 * @param nLimit An upper limit on the number of bytes to download, if less than
 *               1 then limit checks are disabled and download will continue
 *               until complete, a socket error occurs, or no additional
 *               memory can be allocated.  The actual amount of data read maybe
 *               upto 32K - 1 bytes more than the given limit.
 *
 * @param rConSec If > 0.0 this is the floating point number of seconds to
 *             wait on a connection before giving up.  If 0.0 or less then
 *             operating system defaults are used.  Typical OS default is about
 *             five minutes.  This value only affects the initial connection
 *             timeout and not the wait time for data to appear.
 *
 * @param ppRes  If not NULL, receives the response for the request, with the
 *               code, final URL and headers.  Due to redirects the URL in the
 *               response may not be the same as sUrl.  The caller frees it
 *               with del_DasHttpResp().  Failures are logged either way.
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
	const char* sUrl, const char* sAgent, DasCredMngr* pMgr, int64_t nLimit,
	float rConSec, DasHttpResp** ppRes
);

/** das_http_readUrl() with full control over the request, see das_http_opts
 *
 * @returns The body as das_http_readUrl() does, or NULL in two cases the
 *        caller tells apart by DasHttpResp_code(): 304, the server says
 *        the caller's copy is current and sent no body, and
 *        DasHttpResp_error() is NULL; anything else, the request failed and
 *        DasHttpResp_error() says why.  Telling these apart needs the
 *        response, so ppRes should not be NULL when sending validators.
 */
DAS_API DasAry* das_http_readUrl_ex(
	const char* sUrl, DasCredMngr* pMgr, const das_http_opts* pOpts,
	int64_t nLimit, DasHttpResp** ppRes
);

/** @} */

#ifdef __cplusplus
}
#endif

#endif /* _das_http_h_ */
