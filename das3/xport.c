/* Copyright (C) 2026 Chris Piker <chris-piker@uiowa.edu>
 *
 * This file is part of das2C, the Core Das C Library.
 *
 * das2C is free software; you can redistribute it and/or modify it under
 * the terms of the GNU Lesser General Public License version 2.1 as published
 * by the Free Software Foundation.
 *
 * das2C is distributed in the hope that it will be useful, but WITHOUT ANY
 * WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS
 * FOR A PARTICULAR PURPOSE.  See the GNU Lesser General Public License for
 * more details.
 *
 * You should have received a copy of the GNU Lesser General Public License
 * version 2.1 along with das2C; if not, see <http://www.gnu.org/licenses/>.
 */

/* Transports, see xport_p.h.  This is the only file that knows how bytes get
 * on and off a FILE*, a pipe, a socket or a TLS session.  Errors go through
 * DASERR_IO; the transport is a layer of the I/O module, not a module of its
 * own with a public error code. */

#define _POSIX_C_SOURCE 200112L

#include <errno.h>
#include <limits.h>
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>
#include <zlib.h>

#include "defs.h"   /* first, it decides DAS_NO_NETWORK */

#ifndef _WIN32
#include <unistd.h>
#include <sys/wait.h>
#ifndef DAS_NO_NETWORK
#include <sys/socket.h>
#endif
#else
#include <winsock2.h>
#define popen _popen
#define pclose _pclose
#endif

#ifndef DAS_NO_NETWORK
#include <openssl/ssl.h>
#include <openssl/err.h>
#endif

#include "util.h"
#include "log.h"
#include "xport_p.h"

/* Linux has a per-call flag to keep a closed peer from raising SIGPIPE.
   Mac, Windows and Solaris do not, so the flag becomes a no-op there. */
#ifndef MSG_NOSIGNAL
#define MSG_NOSIGNAL 0
#endif

/* ************************************************************************* */
/* The base */

void _DasXport_init(DasXport* pBase, const das_xport_vt* pVt, unsigned int uFlags)
{
	memset(pBase, 0, sizeof(DasXport));
	pBase->pVt = pVt;
	pBase->uFlags = uFlags;
}

ptrdiff_t _DasXport_fail(DasXport* pBase, const char* sFmt, ...)
{
	/* The first failure is the cause, later ones are usually fallout */
	if(!pBase->bFailed){
		va_list argp;
		va_start(argp, sFmt);
		vsnprintf(pBase->sErr, DASXPORT_ERR_SZ, sFmt, argp);
		va_end(argp);
		pBase->bFailed = true;
	}
	return -1;
}

const char* DasXport_error(const DasXport* pThis)
{
	return pThis->bFailed ? pThis->sErr : NULL;
}

ptrdiff_t DasXport_read(DasXport* pThis, void* pOut, size_t uLen)
{
	if(uLen == 0) return 0;
	if(!(pThis->uFlags & DASXPORT_RD))
		return _DasXport_fail(pThis, "The %s transport is not open for reading",
		                      pThis->pVt->sKind);

	/* Pushed back bytes were read once already, so they still come out after
	   a close or a failure, and they are not counted in nMoved again */
	if(pThis->uBackPos < pThis->uBackLen){
		size_t uHave = pThis->uBackLen - pThis->uBackPos;
		size_t uCopy = (uLen < uHave) ? uLen : uHave;
		memcpy(pOut, pThis->pBack + pThis->uBackPos, uCopy);
		pThis->uBackPos += uCopy;
		if(pThis->uBackPos == pThis->uBackLen)
			pThis->uBackPos = pThis->uBackLen = 0;
		return (ptrdiff_t)uCopy;
	}

	if(pThis->bClosed)
		return _DasXport_fail(pThis, "Read from a closed %s transport", pThis->pVt->sKind);
	if(pThis->bFailed) return -1;

	ptrdiff_t nRead = pThis->pVt->read(pThis, pOut, uLen);
	if(nRead > 0)
		pThis->nMoved += nRead;
	else if(nRead < 0)
		_DasXport_fail(pThis, "Read failed on the %s transport", pThis->pVt->sKind);
	return nRead;
}

ptrdiff_t _DasXport_readFull(DasXport* pThis, void* pOut, size_t uLen)
{
	size_t uGot = 0;
	while(uGot < uLen){
		ptrdiff_t nRead = DasXport_read(pThis, (ubyte*)pOut + uGot, uLen - uGot);
		if(nRead < 0) return -1;
		if(nRead == 0) break;
		uGot += (size_t)nRead;
	}
	return (ptrdiff_t)uGot;
}

ptrdiff_t DasXport_write(DasXport* pThis, const void* pIn, size_t uLen)
{
	if(!(pThis->uFlags & DASXPORT_WR))
		return _DasXport_fail(pThis, "The %s transport is not open for writing",
		                      pThis->pVt->sKind);
	if(pThis->bClosed)
		return _DasXport_fail(pThis, "Write to a closed %s transport", pThis->pVt->sKind);
	if(pThis->bFailed) return -1;
	if(uLen == 0) return 0;

	ptrdiff_t nWrote = pThis->pVt->write(pThis, pIn, uLen);
	if(nWrote != (ptrdiff_t)uLen)
		return _DasXport_fail(pThis, "Write failed on the %s transport", pThis->pVt->sKind);
	pThis->nMoved += nWrote;
	return nWrote;
}

DasErrCode _DasXport_unread(DasXport* pThis, const void* pIn, size_t uLen)
{
	if(!(pThis->uFlags & DASXPORT_RD))
		return das_error(DASERR_IO, "Unread on the %s transport, which is not "
		                 "open for reading", pThis->pVt->sKind);
	if(uLen == 0) return DAS_OKAY;

	/* The new bytes go in front of any still waiting.  This happens about
	   once per HTTP request, a fresh allocation each time is fine. */
	size_t uHave = pThis->uBackLen - pThis->uBackPos;
	ubyte* pNew = (ubyte*)malloc(uLen + uHave);
	if(pNew == NULL)
		return das_error(DASERR_IO, "Couldn't allocate %zu bytes of pushback", uLen + uHave);
	memcpy(pNew, pIn, uLen);
	if(uHave > 0) memcpy(pNew + uLen, pThis->pBack + pThis->uBackPos, uHave);
	free(pThis->pBack);
	pThis->pBack = pNew;
	pThis->uBackLen = uLen + uHave;
	pThis->uBackPos = 0;
	return DAS_OKAY;
}

ptrdiff_t _DasXport_fill(DasXport* pThis, DasBuf* pBuf, size_t uLen)
{
	size_t uSpace = 0;
	ubyte* pDest = DasBuf_prepare(pBuf, &uSpace);
	if(uSpace < uLen){
		das_error(DASERR_BUF, "Buffer has %zu bytes of space left, can't read "
		          "%zu bytes.", uSpace, uLen);
		return -1;
	}
	if(uLen == 0) return 0;

	ptrdiff_t nRead = _DasXport_readFull(pThis, pDest, uLen);
	if(nRead > 0) DasBuf_commit(pBuf, (size_t)nRead);
	return nRead;
}

ptrdiff_t _DasXport_drain(DasXport* pThis, DasBuf* pBuf)
{
	size_t uLen = 0;
	const ubyte* pData = DasBuf_data(pBuf, &uLen);
	if(pData == NULL) return 0;

	ptrdiff_t nWrote = DasXport_write(pThis, pData, uLen);
	if(nWrote > 0) DasBuf_consume(pBuf, (size_t)nWrote);
	return nWrote;
}

DasErrCode DasXport_close(DasXport* pThis)
{
	if(pThis->bClosed) return DAS_OKAY;
	pThis->bClosed = true;

	DasErrCode nRet = DAS_OKAY;
	if(pThis->pVt->close != NULL)
		nRet = pThis->pVt->close(pThis);
	if(nRet != DAS_OKAY)
		_DasXport_fail(pThis, "Error closing the %s transport", pThis->pVt->sKind);

	/* Checked first so a quiet log skips building the varargs call */
	if(daslog_level() <= DASLOG_DEBUG)
		daslog_debug_v("%s transport closed after %lld bytes", pThis->pVt->sKind,
		               (long long)pThis->nMoved);
	return nRet;
}

void del_DasXport(DasXport* pThis)
{
	if(pThis == NULL) return;
	if(!pThis->bClosed) DasXport_close(pThis);
	if(pThis->pVt->free != NULL) pThis->pVt->free(pThis);
	free(pThis->pBack);
	free(pThis);
}

/* Mode strings to flags, 0 for anything not allowed */
static unsigned int _xport_mode(const char* sMode, bool bAllowRw)
{
	if(sMode == NULL) return 0;
	if(strcmp(sMode, "r") == 0) return DASXPORT_RD;
	if(strcmp(sMode, "w") == 0) return DASXPORT_WR;
	if(bAllowRw && (strcmp(sMode, "rw") == 0)) return DASXPORT_RD | DASXPORT_WR;
	return 0;
}

static DasXport* _xport_alloc(size_t uSize, const char* sKind)
{
	DasXport* pThis = (DasXport*)calloc(1, uSize);
	if(pThis == NULL)
		das_error(DASERR_IO, "Couldn't allocate a %s transport", sKind);
	return pThis;
}

/* ************************************************************************* */
/* C streams and pipes */

typedef struct das_xport_file {
	DasXport base;
	FILE* pFile;
	bool  bOwn;
	char* sCmd;     /* cmd kind only, for the exit status warning */
} DasXportFile;

static ptrdiff_t _DasXportFile_read(DasXport* pBase, void* pOut, size_t uLen)
{
	DasXportFile* pThis = (DasXportFile*)pBase;
	errno = 0;
	size_t uRead = fread(pOut, 1, uLen, pThis->pFile);
	if((uRead < uLen) && ferror(pThis->pFile))
		return _DasXport_fail(pBase, "Error reading from %s, %s", pBase->pVt->sKind,
		                      errno ? strerror(errno) : "stream error");
	return (ptrdiff_t)uRead;
}

static ptrdiff_t _DasXportFile_write(DasXport* pBase, const void* pIn, size_t uLen)
{
	DasXportFile* pThis = (DasXportFile*)pBase;
	errno = 0;
	if(fwrite(pIn, 1, uLen, pThis->pFile) != uLen)
		return _DasXport_fail(pBase, "Error writing to %s, %s", pBase->pVt->sKind,
		                      errno ? strerror(errno) : "stream error");
	return (ptrdiff_t)uLen;
}

static DasErrCode _DasXportFile_close(DasXport* pBase)
{
	DasXportFile* pThis = (DasXportFile*)pBase;
	DasErrCode nRet = DAS_OKAY;

	/* A full disk or a closed pipe downstream often only shows up here */
	errno = 0;
	if((pBase->uFlags & DASXPORT_WR) && (fflush(pThis->pFile) != 0)){
		_DasXport_fail(pBase, "Error flushing output, %s", strerror(errno));
		nRet = DASERR_IO;
	}
	if(pThis->bOwn && (fclose(pThis->pFile) != 0) && (nRet == DAS_OKAY)){
		_DasXport_fail(pBase, "Error closing file, %s", strerror(errno));
		nRet = DASERR_IO;
	}
	pThis->pFile = NULL;
	return nRet;
}

static DasErrCode _DasXportCmd_close(DasXport* pBase)
{
	/* A non-zero exit status is not a transport failure, the reader has
	   already seen whatever the command wrote.  It is often why that output
	   is short, though, so it is logged.  Only a failed pclose() is ours. */
	DasXportFile* pThis = (DasXportFile*)pBase;
	int nStat = pclose(pThis->pFile);
	pThis->pFile = NULL;
	if(nStat == -1){
		_DasXport_fail(pBase, "Error closing sub-command, %s", strerror(errno));
		return DASERR_IO;
	}
#ifndef _WIN32
	if(WIFEXITED(nStat) && (WEXITSTATUS(nStat) != 0))
		daslog_warn_v("Sub-command exited with status %d: %s", WEXITSTATUS(nStat),
		              pThis->sCmd);
	else if(WIFSIGNALED(nStat))
		daslog_warn_v("Sub-command ended by signal %d: %s", WTERMSIG(nStat), pThis->sCmd);
#else
	if(nStat != 0)
		daslog_warn_v("Sub-command exited with status %d: %s", nStat, pThis->sCmd);
#endif
	return DAS_OKAY;
}

static void _DasXportCmd_free(DasXport* pBase)
{
	free(((DasXportFile*)pBase)->sCmd);
}

static const das_xport_vt g_vtFile = {
	"file", _DasXportFile_read, _DasXportFile_write, _DasXportFile_close, NULL
};

static const das_xport_vt g_vtCmd = {
	"cmd", _DasXportFile_read, NULL, _DasXportCmd_close, _DasXportCmd_free
};

DasXport* _new_DasXport_cfile(FILE* pFile, const char* sMode, bool bOwn)
{
	unsigned int uFlags = _xport_mode(sMode, false);
	if(uFlags == 0){
		das_error(DASERR_IO, "File transport mode must be \"r\" or \"w\", not \"%s\"",
		          sMode ? sMode : "(null)");
		return NULL;
	}
	if(pFile == NULL){
		das_error(DASERR_IO, "No FILE given for a file transport");
		return NULL;
	}
	DasXportFile* pThis = (DasXportFile*)_xport_alloc(sizeof(DasXportFile), "file");
	if(pThis == NULL) return NULL;
	_DasXport_init(&(pThis->base), &g_vtFile, uFlags);
	pThis->pFile = pFile;
	pThis->bOwn = bOwn;
	return &(pThis->base);
}

DasXport* new_DasXport_cfile(FILE* pFile, const char* sMode)
{
	if(pFile != NULL) das_stdio_binary(pFile);  /* text mode alters bytes on Windows */
	return _new_DasXport_cfile(pFile, sMode, false);
}

DasXport* new_DasXport_stdin(void){  return new_DasXport_cfile(stdin, "r"); }
DasXport* new_DasXport_stdout(void){ return new_DasXport_cfile(stdout, "w"); }

DasXport* new_DasXport_file(const char* sPath, const char* sMode)
{
	if(sPath == NULL){
		das_error(DASERR_IO, "No path given for a file transport");
		return NULL;
	}
	unsigned int uFlags = _xport_mode(sMode, false);
	if(uFlags == 0){
		das_error(DASERR_IO, "File transport mode must be \"r\" or \"w\", not \"%s\"",
		          sMode ? sMode : "(null)");
		return NULL;
	}
	errno = 0;
	FILE* pFile = fopen(sPath, (uFlags & DASXPORT_RD) ? "rb" : "wb");
	if(pFile == NULL){
		das_error(DASERR_IO, "Couldn't open %s, %s", sPath, strerror(errno));
		return NULL;
	}
	DasXport* pThis = _new_DasXport_cfile(pFile, sMode, true);
	if(pThis == NULL) fclose(pFile);
	return pThis;
}

DasXport* new_DasXport_cmd(const char* sCmd)
{
	if(sCmd == NULL){
		das_error(DASERR_IO, "No command given for a command transport");
		return NULL;
	}
	errno = 0;
#ifdef _WIN32
	FILE* pPipe = popen(sCmd, "rb");  /* text mode would rewrite binary packets */
#else
	FILE* pPipe = popen(sCmd, "r");
#endif
	if(pPipe == NULL){
		das_error(DASERR_IO, "Error running sub-command %s, %s", sCmd, strerror(errno));
		return NULL;
	}
	DasXportFile* pThis = (DasXportFile*)_xport_alloc(sizeof(DasXportFile), "cmd");
	if(pThis == NULL){ pclose(pPipe); return NULL; }
	_DasXport_init(&(pThis->base), &g_vtCmd, DASXPORT_RD);
	pThis->pFile = pPipe;
	pThis->bOwn = true;
	pThis->sCmd = das_strdup(sCmd);  /* NULL only costs the warning its text */
	return &(pThis->base);
}

/* ************************************************************************* */
/* Memory */

typedef struct das_xport_mem {
	DasXport base;
	ubyte* pMem;
	size_t uLen;
	size_t uPos;
} DasXportMem;

static ptrdiff_t _DasXportMem_read(DasXport* pBase, void* pOut, size_t uLen)
{
	DasXportMem* pThis = (DasXportMem*)pBase;
	size_t uHave = pThis->uLen - pThis->uPos;
	size_t uCopy = (uLen < uHave) ? uLen : uHave;
	memcpy(pOut, pThis->pMem + pThis->uPos, uCopy);
	pThis->uPos += uCopy;
	return (ptrdiff_t)uCopy;
}

static ptrdiff_t _DasXportMem_write(DasXport* pBase, const void* pIn, size_t uLen)
{
	DasXportMem* pThis = (DasXportMem*)pBase;
	if(uLen > pThis->uLen - pThis->uPos)
		return _DasXport_fail(pBase, "Memory output is full, %zu bytes left for a "
		                      "%zu byte write", pThis->uLen - pThis->uPos, uLen);
	memcpy(pThis->pMem + pThis->uPos, pIn, uLen);
	pThis->uPos += uLen;
	return (ptrdiff_t)uLen;
}

static const das_xport_vt g_vtMem = {
	"mem", _DasXportMem_read, _DasXportMem_write, NULL, NULL
};

DasXport* new_DasXport_mem(void* pMem, size_t uLen, const char* sMode)
{
	unsigned int uFlags = _xport_mode(sMode, false);
	if(uFlags == 0){
		das_error(DASERR_IO, "Memory transport mode must be \"r\" or \"w\", not \"%s\"",
		          sMode ? sMode : "(null)");
		return NULL;
	}
	if((pMem == NULL) && (uLen > 0)){
		das_error(DASERR_IO, "No memory given for a %zu byte memory transport", uLen);
		return NULL;
	}
	DasXportMem* pThis = (DasXportMem*)_xport_alloc(sizeof(DasXportMem), "mem");
	if(pThis == NULL) return NULL;
	_DasXport_init(&(pThis->base), &g_vtMem, uFlags);
	pThis->pMem = (ubyte*)pMem;
	pThis->uLen = uLen;
	return &(pThis->base);
}

/* ************************************************************************* */
/* Sockets */

#ifndef DAS_NO_NETWORK

#ifdef _WIN32
#define _XP_SOCK(n) ((SOCKET)(n))
#else
#define _XP_SOCK(n) ((int)(n))
#endif

/* Read the socket error before anything else can overwrite it */
static const char* _xport_sockErr(char* sBuf, size_t uLen)
{
	/* A receive timeout (see das_http_opts.rReadSec) shows up as a would-block
	   error on a blocking socket, which reads as nonsense to a user */
#ifdef _WIN32
	int nErr = WSAGetLastError();
	if(nErr == WSAETIMEDOUT) snprintf(sBuf, uLen, "timed out waiting for data");
	else snprintf(sBuf, uLen, "Winsock error %d", nErr);
#else
	if((errno == EAGAIN)||(errno == EWOULDBLOCK))
		snprintf(sBuf, uLen, "timed out waiting for data");
	else
		snprintf(sBuf, uLen, "%s", strerror(errno));
#endif
	return sBuf;
}

static void _xport_sockClose(intptr_t nSock)
{
#ifndef _WIN32
	shutdown(_XP_SOCK(nSock), SHUT_RDWR);
	close(_XP_SOCK(nSock));
#else
	shutdown(_XP_SOCK(nSock), SD_BOTH);
	closesocket(_XP_SOCK(nSock));
#endif
}

typedef struct das_xport_sock {
	DasXport base;
	intptr_t nSock;
	bool bOwn;
} DasXportSock;

static ptrdiff_t _DasXportSock_read(DasXport* pBase, void* pOut, size_t uLen)
{
	DasXportSock* pThis = (DasXportSock*)pBase;
	char sErr[128];

	/* recv() takes an int length on Windows; a short read is allowed anyway */
	int nAsk = (uLen > INT_MAX) ? INT_MAX : (int)uLen;
	while(true){
		errno = 0;
		ptrdiff_t nRead = recv(_XP_SOCK(pThis->nSock), (char*)pOut, nAsk, 0);
		if(nRead >= 0) return nRead;
#ifndef _WIN32
		if(errno == EINTR) continue;
#endif
		return _DasXport_fail(pBase, "Error reading from socket, %s",
		                      _xport_sockErr(sErr, sizeof(sErr)));
	}
}

static ptrdiff_t _DasXportSock_write(DasXport* pBase, const void* pIn, size_t uLen)
{
	DasXportSock* pThis = (DasXportSock*)pBase;
	char sErr[128];
	size_t uSent = 0;
	while(uSent < uLen){
		size_t uLeft = uLen - uSent;
		int nAsk = (uLeft > INT_MAX) ? INT_MAX : (int)uLeft;
		errno = 0;
		ptrdiff_t nSent = send(
			_XP_SOCK(pThis->nSock), (const char*)pIn + uSent, nAsk, MSG_NOSIGNAL
		);
		if(nSent < 0){
#ifndef _WIN32
			if(errno == EINTR) continue;
#endif
			return _DasXport_fail(pBase, "Error writing to socket, %s",
			                      _xport_sockErr(sErr, sizeof(sErr)));
		}
		uSent += (size_t)nSent;
	}
	return (ptrdiff_t)uLen;
}

static DasErrCode _DasXportSock_close(DasXport* pBase)
{
	DasXportSock* pThis = (DasXportSock*)pBase;
	if(pThis->bOwn && (pThis->nSock != -1)) _xport_sockClose(pThis->nSock);
	pThis->nSock = -1;
	return DAS_OKAY;
}

static const das_xport_vt g_vtSock = {
	"sock", _DasXportSock_read, _DasXportSock_write, _DasXportSock_close, NULL
};

DasXport* _new_DasXport_sock(intptr_t nSock, const char* sMode, bool bOwn)
{
	unsigned int uFlags = _xport_mode(sMode, true);
	if(uFlags == 0){
		das_error(DASERR_IO, "Socket transport mode must be \"r\", \"w\" or \"rw\", "
		          "not \"%s\"", sMode ? sMode : "(null)");
		return NULL;
	}
	if(nSock == -1){
		das_error(DASERR_IO, "Invalid socket given for a socket transport");
		return NULL;
	}
	DasXportSock* pThis = (DasXportSock*)_xport_alloc(sizeof(DasXportSock), "sock");
	if(pThis == NULL) return NULL;
	_DasXport_init(&(pThis->base), &g_vtSock, uFlags);
	pThis->nSock = nSock;
	pThis->bOwn = bOwn;
	return &(pThis->base);
}

DasXport* new_DasXport_sock(intptr_t nSock, const char* sMode)
{
	return _new_DasXport_sock(nSock, sMode, false);
}

/* ************************************************************************* */
/* TLS sessions */

typedef struct das_xport_ssl {
	DasXport base;
	SSL* pSsl;
	intptr_t nSock;
} DasXportSsl;

/* Put the reason an SSL call failed into the base and return -1.  The
   OpenSSL error queue is per thread and sticky, it is emptied here so an
   old entry is never reported as the cause of a later failure. */
static ptrdiff_t _xport_sslFail(DasXport* pBase, SSL* pSsl, int nRet, const char* sWhat)
{
	char sErr[160] = {'\0'};
	int nCode = SSL_get_error(pSsl, nRet);
	unsigned long uErr = 0;

	switch(nCode){
	case SSL_ERROR_SSL:
		uErr = ERR_get_error();
		if(uErr != 0) ERR_error_string_n(uErr, sErr, sizeof(sErr));
		else snprintf(sErr, sizeof(sErr), "TLS protocol error");
		break;
	case SSL_ERROR_SYSCALL:
		_xport_sockErr(sErr, sizeof(sErr));
		break;
	case SSL_ERROR_WANT_READ:
	case SSL_ERROR_WANT_WRITE:
		/* Blocking sockets with SSL_MODE_AUTO_RETRY, the OpenSSL default,
		   never get here; seeing it means the socket was made non-blocking */
		snprintf(sErr, sizeof(sErr), "TLS session wants a retry, socket is "
		         "non-blocking");
		break;
	default:
		snprintf(sErr, sizeof(sErr), "TLS error code %d", nCode);
		break;
	}
	ERR_clear_error();
	return _DasXport_fail(pBase, "%s, %s", sWhat, sErr);
}

static ptrdiff_t _DasXportSsl_read(DasXport* pBase, void* pOut, size_t uLen)
{
	DasXportSsl* pThis = (DasXportSsl*)pBase;
	int nAsk = (uLen > INT_MAX) ? INT_MAX : (int)uLen;
	int nRead = SSL_read(pThis->pSsl, pOut, nAsk);
	if(nRead > 0) return nRead;

	/* Many HTTP servers drop the connection without a TLS close_notify once
	   the body is sent.  OpenSSL 3 reports that as an error, but the body is
	   delimited by the HTTP layer, so a 0 here is the end of input as it was
	   for the old readers.  Clear the queue it leaves behind. */
	int nCode = SSL_get_error(pThis->pSsl, nRead);
	if((nRead == 0) || (nCode == SSL_ERROR_ZERO_RETURN)){
		if(nCode != SSL_ERROR_ZERO_RETURN)
			daslog_debug("TLS peer closed without close_notify, taken as end of input");
		ERR_clear_error();
		return 0;
	}
	return _xport_sslFail(pBase, pThis->pSsl, nRead, "Error reading from TLS session");
}

static ptrdiff_t _DasXportSsl_write(DasXport* pBase, const void* pIn, size_t uLen)
{
	DasXportSsl* pThis = (DasXportSsl*)pBase;
	size_t uSent = 0;
	while(uSent < uLen){
		size_t uLeft = uLen - uSent;
		int nAsk = (uLeft > INT_MAX) ? INT_MAX : (int)uLeft;
		int nSent = SSL_write(pThis->pSsl, (const ubyte*)pIn + uSent, nAsk);
		if(nSent <= 0)
			return _xport_sslFail(pBase, pThis->pSsl, nSent, "Error writing to TLS session");
		uSent += (size_t)nSent;
	}
	return (ptrdiff_t)uLen;
}

static DasErrCode _DasXportSsl_close(DasXport* pBase)
{
	DasXportSsl* pThis = (DasXportSsl*)pBase;
	if(pThis->pSsl != NULL){
		/* One call sends our close_notify.  Waiting for the server's reply
		   (a second call) can block on servers that just drop the socket,
		   and a client that has read what it wanted gains nothing by it. */
		if(SSL_shutdown(pThis->pSsl) < 0)
			daslog_debug("TLS close_notify could not be sent, closing anyway");
		ERR_clear_error();
		SSL_free(pThis->pSsl);
		pThis->pSsl = NULL;
	}
	if(pThis->nSock != -1) _xport_sockClose(pThis->nSock);
	pThis->nSock = -1;
	return DAS_OKAY;
}

static const das_xport_vt g_vtSsl = {
	"ssl", _DasXportSsl_read, _DasXportSsl_write, _DasXportSsl_close, NULL
};

DasXport* _new_DasXport_ssl(struct ssl_st* pSsl, const char* sMode)
{
	unsigned int uFlags = _xport_mode(sMode, true);
	if(uFlags == 0){
		das_error(DASERR_IO, "TLS transport mode must be \"r\", \"w\" or \"rw\", "
		          "not \"%s\"", sMode ? sMode : "(null)");
		return NULL;
	}
	if(pSsl == NULL){
		das_error(DASERR_IO, "No TLS session given for a TLS transport");
		return NULL;
	}
	DasXportSsl* pThis = (DasXportSsl*)_xport_alloc(sizeof(DasXportSsl), "ssl");
	if(pThis == NULL) return NULL;
	_DasXport_init(&(pThis->base), &g_vtSsl, uFlags);
	pThis->pSsl = pSsl;
	pThis->nSock = SSL_get_fd(pSsl);  /* -1 if none, close skips it then */
	return &(pThis->base);
}

#else

DasXport* new_DasXport_sock(intptr_t nSock, const char* sMode)
{
	(void)nSock; (void)sMode;
	das_error(DASERR_NOTIMP, "Sockets are not available in this build");
	return NULL;
}

#endif /* DAS_NO_NETWORK */

/* ************************************************************************* */
/* Copying */

#define _XP_COPY_CHUNK 16384

DasErrCode DasXport_copyTo(
	DasXport* pThis, DasXport* pDest, int64_t* pCopied, das_progress_f onProgress,
	void* pUser
){
	DasErrCode nRet = DAS_OKAY;
	int64_t nTotal = 0;
	ubyte aBuf[_XP_COPY_CHUNK];

	while(true){
		ptrdiff_t nRead = DasXport_read(pThis, aBuf, _XP_COPY_CHUNK);
		if(nRead == 0) break;
		if((nRead < 0) || (DasXport_write(pDest, aBuf, (size_t)nRead) < 0)){
			nRet = DASERR_IO;
			break;
		}
		nTotal += nRead;

		/* The callback is the only way a caller can stop a long copy, so its
		   code goes back unchanged for the caller to recognize */
		if(onProgress != NULL){
			nRet = onProgress(nTotal, pUser);
			if(nRet != DAS_OKAY) break;
		}
	}
	if(pCopied != NULL) *pCopied = nTotal;
	return nRet;
}

/* ************************************************************************* */
/* zlib filters */

#define _XP_ZCHUNK 16384

typedef struct das_xport_zlib {
	DasXport base;
	DasXport* pInner;
	z_stream zs;
	bool bEnd;               /* inflate saw the end of the zlib stream */
	ubyte aChunk[_XP_ZCHUNK];
} DasXportZlib;

static ptrdiff_t _DasXportInflate_read(DasXport* pBase, void* pOut, size_t uLen)
{
	DasXportZlib* pThis = (DasXportZlib*)pBase;
	if(pThis->bEnd) return 0;

	uInt uAsk = (uLen > UINT_MAX) ? UINT_MAX : (uInt)uLen;
	pThis->zs.next_out = (Bytef*)pOut;
	pThis->zs.avail_out = uAsk;

	/* Loop until something comes out, a short read is fine after that */
	while(pThis->zs.avail_out == uAsk){
		if(pThis->zs.avail_in == 0){
			ptrdiff_t nIn = DasXport_read(pThis->pInner, pThis->aChunk, _XP_ZCHUNK);
			if(nIn < 0)
				return _DasXport_fail(pBase, "%s", DasXport_error(pThis->pInner));
			if(nIn == 0)
				return _DasXport_fail(pBase, "Compressed input ended before the end "
				                      "of the zlib stream");
			pThis->zs.next_in = pThis->aChunk;
			pThis->zs.avail_in = (uInt)nIn;
		}

		int nZ = inflate(&(pThis->zs), Z_NO_FLUSH);
		if(nZ == Z_STREAM_END){
			pThis->bEnd = true;
			/* Whatever follows the zlib stream is not ours, hand it back */
			if(pThis->zs.avail_in > 0){
				daslog_debug_v("%u bytes follow the zlib stream, handed back",
				               (unsigned)pThis->zs.avail_in);
				_DasXport_unread(pThis->pInner, pThis->zs.next_in, pThis->zs.avail_in);
			}
			pThis->zs.avail_in = 0;
			break;
		}
		if((nZ == Z_BUF_ERROR) && (pThis->zs.avail_in == 0)) continue;
		if(nZ != Z_OK)
			return _DasXport_fail(pBase, "Inflate error, %s",
			                      pThis->zs.msg ? pThis->zs.msg : "corrupt input");
	}
	return (ptrdiff_t)(uAsk - pThis->zs.avail_out);
}

/* Push whatever deflate has made out to the inner transport */
static ptrdiff_t _xport_deflateRun(DasXportZlib* pThis, int nFlush)
{
	DasXport* pBase = &(pThis->base);
	int nZ;
	do{
		pThis->zs.next_out = pThis->aChunk;
		pThis->zs.avail_out = _XP_ZCHUNK;
		nZ = deflate(&(pThis->zs), nFlush);
		if(nZ == Z_STREAM_ERROR)
			return _DasXport_fail(pBase, "Deflate error, %s",
			                      pThis->zs.msg ? pThis->zs.msg : "bad state");
		size_t uHave = _XP_ZCHUNK - pThis->zs.avail_out;
		if((uHave > 0) && (DasXport_write(pThis->pInner, pThis->aChunk, uHave) < 0))
			return _DasXport_fail(pBase, "%s", DasXport_error(pThis->pInner));
	}while(pThis->zs.avail_out == 0 || (nFlush == Z_FINISH && nZ != Z_STREAM_END));
	return 0;
}

static ptrdiff_t _DasXportDeflate_write(DasXport* pBase, const void* pIn, size_t uLen)
{
	DasXportZlib* pThis = (DasXportZlib*)pBase;
	size_t uDone = 0;
	while(uDone < uLen){
		size_t uLeft = uLen - uDone;
		uInt uTake = (uLeft > UINT_MAX) ? UINT_MAX : (uInt)uLeft;
		pThis->zs.next_in = (Bytef*)((const ubyte*)pIn + uDone);
		pThis->zs.avail_in = uTake;
		if(_xport_deflateRun(pThis, Z_NO_FLUSH) < 0) return -1;
		uDone += uTake;
	}
	return (ptrdiff_t)uLen;
}

static DasErrCode _DasXportInflate_close(DasXport* pBase)
{
	DasXportZlib* pThis = (DasXportZlib*)pBase;
	return DasXport_close(pThis->pInner);
}

static DasErrCode _DasXportDeflate_close(DasXport* pBase)
{
	DasXportZlib* pThis = (DasXportZlib*)pBase;
	pThis->zs.avail_in = 0;
	ptrdiff_t nFin = _xport_deflateRun(pThis, Z_FINISH);
	DasErrCode nRet = DasXport_close(pThis->pInner);
	if(nFin < 0) return DASERR_IO;
	if(nRet != DAS_OKAY)
		_DasXport_fail(pBase, "%s", DasXport_error(pThis->pInner));
	return nRet;
}

static void _DasXportInflate_free(DasXport* pBase)
{
	DasXportZlib* pThis = (DasXportZlib*)pBase;
	inflateEnd(&(pThis->zs));
	del_DasXport(pThis->pInner);
}

static void _DasXportDeflate_free(DasXport* pBase)
{
	DasXportZlib* pThis = (DasXportZlib*)pBase;
	deflateEnd(&(pThis->zs));
	del_DasXport(pThis->pInner);
}

static const das_xport_vt g_vtInflate = {
	"inflate", _DasXportInflate_read, NULL, _DasXportInflate_close, _DasXportInflate_free
};

static const das_xport_vt g_vtDeflate = {
	"deflate", NULL, _DasXportDeflate_write, _DasXportDeflate_close, _DasXportDeflate_free
};

DasXport* _new_DasXport_inflate(DasXport* pInner)
{
	if((pInner == NULL) || !(pInner->uFlags & DASXPORT_RD)){
		das_error(DASERR_IO, "Inflate needs a transport open for reading");
		return NULL;
	}
	DasXportZlib* pThis = (DasXportZlib*)_xport_alloc(sizeof(DasXportZlib), "inflate");
	if(pThis == NULL) return NULL;
	if(inflateInit(&(pThis->zs)) != Z_OK){
		das_error(DASERR_IO, "Couldn't start zlib inflate, %s",
		          pThis->zs.msg ? pThis->zs.msg : "no detail");
		free(pThis);
		return NULL;
	}
	_DasXport_init(&(pThis->base), &g_vtInflate, DASXPORT_RD);
	pThis->pInner = pInner;
	return &(pThis->base);
}

DasXport* _new_DasXport_deflate(DasXport* pInner)
{
	if((pInner == NULL) || !(pInner->uFlags & DASXPORT_WR)){
		das_error(DASERR_IO, "Deflate needs a transport open for writing");
		return NULL;
	}
	DasXportZlib* pThis = (DasXportZlib*)_xport_alloc(sizeof(DasXportZlib), "deflate");
	if(pThis == NULL) return NULL;
	if(deflateInit(&(pThis->zs), Z_DEFAULT_COMPRESSION) != Z_OK){
		das_error(DASERR_IO, "Couldn't start zlib deflate, %s",
		          pThis->zs.msg ? pThis->zs.msg : "no detail");
		free(pThis);
		return NULL;
	}
	_DasXport_init(&(pThis->base), &g_vtDeflate, DASXPORT_WR);
	pThis->pInner = pInner;
	return &(pThis->base);
}
