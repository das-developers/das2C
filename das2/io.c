/* Copyright (C) 2004-2006 Jeremy Faden <jeremy-faden@uiowa.edu> 
 *               2012-2019 Chris Piker <chris-piker@uiowa.edu>
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

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <ctype.h>
#include <stdarg.h>
#include <assert.h>

#ifndef _WIN32
#include <sys/socket.h>
#include <sys/time.h>
#else
#include <winsock2.h>
#define popen _popen
#define pclose _pclose
#include "win_gtod.h"
#define gettimeofday win_gettimeofday
#endif

#include <openssl/ssl.h>

#include "util.h"  /* <-- Make sure endianess macros are present */
#include "http.h"  /* Get ssl helpers */
#include "serial3.h"
#include "io.h"

#ifdef _WIN32
typedef ptrdiff_t ssize_t;
#endif

/* input buffer length (should probably move to the max packet size and then
   make sure we always reuse the same buffer */
#define CMPR_IN_BUF_SZ 262144

/* output buffer length */
#define CMPR_OUT_BUF_SZ 262144

/* used internally for implementing stream inflate/deflate, etc/ */
#define STREAM_MODE_STRING 0

/* stream is coming from/going to a socket */
#define STREAM_MODE_SOCKET 1

/* stream is coming from/going to a file */
#define STREAM_MODE_FILE   2
	
/* stream is coming from/going to an SSL context */
#define STREAM_MODE_SSL    3

/* stream is coming from a sub command */
#define STREAM_MODE_CMD    4

/* ************************************************************************** */
/* Constructors/Destructors */

static DasErrCode _DasIO_setMode(DasIO* pThis, const char* mode)
{
	pThis->dasver = 0;  // Any version by default

	if(strchr(mode, '2') != NULL)
		pThis->dasver = 2;
	else if (strchr(mode, '3') != NULL)
		pThis->dasver = 3;

	if(strchr(mode, 'r') != NULL){ 
		pThis->rw = 'r';
	}
	else{
		if(strchr(mode, 'w') != NULL){
			pThis->rw = 'w';

			if(strchr(mode, 'c') != NULL) 
				pThis->compressed = true;
			
			// When writing we have to have a version, default to das2
			if(pThis->dasver == 0)
				pThis->dasver = 2;
		}
		else
			return das_error(DASERR_IO, "Illegal argument for mode in new_DasIO_cfile");
	}
	return DAS_OKAY;
}

DasIO* new_DasIO_cfile(const char* sProg,  FILE * file, const char* mode ) 
{
	if(file == NULL){
		das_error(DASERR_IO, "NULL file pointer argument");
		return NULL;
	}
	
	DasIO* pThis  = (DasIO*)calloc(1, sizeof( DasIO ) );
	pThis->mode   = STREAM_MODE_FILE;
	pThis->model = STREAM_MODEL_V2;
	pThis->file   = file;
	pThis->nSockFd = -1;
	pThis->taskSize = -1;  /* for progress indication */
	pThis->logLevel=LOGLVL_WARNING;
	strncpy(pThis->sName, sProg, DASIO_NAME_SZ - 1);
	OobComment_init(&pThis->cmt);
	das_store_str(&(pThis->cmt.sSrc), &(pThis->cmt.uSrcLen), pThis->sName);

	if(_DasIO_setMode(pThis, mode) != DAS_OKAY){
		free(pThis);
		return NULL;
	}
	 	 
	/* Init an I/O buffer that we can re-use during the life of the object,
	 * This buffer is 1 byte more than the maximum Das Packet size, we may
	 * want to have a buffer that just grows on demand instead. */
	pThis->pDb = new_DasBuf(CMPR_OUT_BUF_SZ);
	 
   return pThis;
}

DasIO* new_DasIO_cmd(const char* sProg, const char* sCmd)
{
	DasIO* pThis = (DasIO*)calloc(1, sizeof( DasIO ) );
	pThis->mode = STREAM_MODE_CMD;
	errno = 0;
	pThis->file = popen(sCmd, "r");
	if(pThis->file == NULL){
		das_error(DASERR_IO, "Error running sub-command %s, %s", sCmd,
				      strerror(errno));
		free(pThis);
		return NULL;
	}
	pThis->nSockFd = -1;
	pThis->taskSize= -1;  /* for progress indication */
	pThis->logLevel=LOGLVL_WARNING;
	strncpy(pThis->sName, sProg, DASIO_NAME_SZ - 1);
	OobComment_init(&pThis->cmt);
	das_store_str(&(pThis->cmt.sSrc), &(pThis->cmt.uSrcLen), pThis->sName);
	pThis->rw = 'r';
	
	 /* Init an I/O buffer that we can re-use during the life of the object,
	  * This buffer is 1 byte more than the maximum Das Packet size, we may
	  * want to have a buffer that just grows on demand instead. */
	 pThis->pDb = new_DasBuf(CMPR_OUT_BUF_SZ);
	 
    return pThis;
}

DasIO* new_DasIO_file(const char* sProg, const char* sFile, const char* mode)
{
	DasIO* pThis = (DasIO*)calloc(1, sizeof( DasIO ) );
	pThis->mode= STREAM_MODE_FILE;
	pThis->model = STREAM_MODEL_V2;
	pThis->taskSize= -1;  /* for progress indication */
	pThis->logLevel=LOGLVL_WARNING;
	pThis->nSockFd = -1;
	strncpy(pThis->sName, sProg, DASIO_NAME_SZ - 1);
	OobComment_init(&(pThis->cmt));
	das_store_str(&(pThis->cmt.sSrc), &(pThis->cmt.uSrcLen), pThis->sName);
	
	if(_DasIO_setMode(pThis, mode) != DAS_OKAY){
		free(pThis);
		return NULL;
	}

	pThis->file = fopen(sFile, (pThis->rw == 'r') ? "rb" : "wb");
	if(pThis->file == NULL){
		free(pThis);
		das_error(DASERR_IO, "Error opening %s", sFile);
		return NULL;
	}
	
	/* Init an I/O buffer that we can re-use during the life of the object,
	  * This buffer is 1 byte more than the maximum Das Packet size, we may
	  * want to have a buffer that just grows on demand instead. */
	 pThis->pDb = new_DasBuf(CMPR_OUT_BUF_SZ);
	
	return pThis;
}

DasIO* new_DasIO_socket(const char* sProg, int nSockFd, const char* mode)
{
	DasIO* pThis = (DasIO*)calloc(1, sizeof( DasIO ) );
	pThis->mode = STREAM_MODE_SOCKET;
	pThis->model = STREAM_MODEL_V2;
	pThis->nSockFd = nSockFd;
	pThis->taskSize= -1;  /* for progress indication */
	pThis->logLevel=LOGLVL_WARNING;
	strncpy(pThis->sName, sProg, DASIO_NAME_SZ - 1);
	OobComment_init(&(pThis->cmt));
	das_store_str(&(pThis->cmt.sSrc), &(pThis->cmt.uSrcLen), pThis->sName);
	
	if(_DasIO_setMode(pThis, mode) != DAS_OKAY){
		free(pThis);
		return NULL;
	}
	
	/* Init an I/O buffer that we can re-use during the life of the object,
	 * This buffer is 1 byte more than the maximum Das Packet size, we may
	 * want to have a buffer that just grows on demand instead. */
	pThis->pDb = new_DasBuf(CMPR_OUT_BUF_SZ);
	 
	return pThis;
}

DasIO* new_DasIO_ssl(const char* sProg, void* pSsl, const char* mode)
{
	DasIO* pThis = (DasIO*)calloc(1, sizeof( DasIO ) );
	pThis->mode = STREAM_MODE_SSL;
	pThis->model = STREAM_MODEL_V2;
	pThis->nSockFd = SSL_get_fd((SSL*)pSsl);
	pThis->pSsl = pSsl;
	pThis->taskSize= -1;  /* for progress indication */
	pThis->logLevel=LOGLVL_WARNING;
	strncpy(pThis->sName, sProg, DASIO_NAME_SZ - 1);
	OobComment_init(&(pThis->cmt));
	das_store_str(&(pThis->cmt.sSrc), &(pThis->cmt.uSrcLen), pThis->sName);
	
	if(_DasIO_setMode(pThis, mode) != DAS_OKAY){
		free(pThis);
		return NULL;
	}
	
	/* Init an I/O buffer that we can re-use during the life of the object,
	 * This buffer is 1 byte more than the maximum Das Packet size, we may
	 * want to have a buffer that just grows on demand instead. */
	pThis->pDb = new_DasBuf(CMPR_OUT_BUF_SZ);
	 
	return pThis;
}

DasIO* new_DasIO_str(
	const char* sProg, char * sbuf, size_t length, const char* mode
){
	DasIO* pThis = (DasIO*)calloc(1, sizeof( DasIO ) );
	pThis->mode = STREAM_MODE_STRING;
	pThis->model = STREAM_MODEL_V2;
	pThis->sBuffer = sbuf;
	pThis->nLength = length;
	pThis->taskSize= -1;  /* for progress indication */
	pThis->logLevel=LOGLVL_WARNING;
	strncpy(pThis->sName, sProg, DASIO_NAME_SZ - 1);
	OobComment_init(&(pThis->cmt));
	das_store_str(&(pThis->cmt.sSrc), &(pThis->cmt.uSrcLen), pThis->sName);
   
	if(_DasIO_setMode(pThis, mode) != DAS_OKAY){
		free(pThis);
		return NULL;
	}

	return pThis;
}

DasErrCode DasIO_model(DasIO* pThis, int nModel){
	if(nModel == 2)
		pThis->model = STREAM_MODEL_V2;
	else if(nModel == 3)
		pThis->model = STREAM_MODEL_V3;
	else if(nModel == -1)
		pThis->model = STREAM_MODEL_MIXED;
	else
		return das_error(DASERR_IO, "Invalid stream model: %d", nModel);

	return DAS_OKAY;
}

/* ************************************************************************** */
/* Socket Assistance */

size_t _DasIO_sockWrite(DasIO* pThis, const char* sBuf, size_t uLen)
{
/* Have to handle the fact that blocking SIGPIPE is different on Apple */
#if defined(__APPLE__) || defined(_WIN32) || defined(__sun)
#define MSG_NOSIGNAL 0
#endif

	/* Try to send the buffer, use multiple writes if needed.  Returns the 
	 * number of bytes written */
	size_t uWrote = 0;
	ssize_t nChunk = 0;
	while(uWrote < uLen){
		nChunk = send(pThis->nSockFd, sBuf, uLen - uWrote, MSG_NOSIGNAL);
		if(nChunk < 0){
			das_error(DASERR_IO, "Socket write error");
			return 0;
		}
		uWrote += nChunk;
	}
	return uLen;
}

/* ************************************************************************** */
/* SSL Assistance */

size_t _DasIO_sslWrite(DasIO* pThis, const char* sBuf, size_t uLen)
{
	if(uLen == 0) return 0;  /* Successfully do nothing */
	if(uLen >= INTMAX_MAX){
		das_error(DASERR_IO, "Can't transmit %zu bytes in a single call", uLen);
		return 0;
	}
	
	/* Try to send the buffer, use multiple writes if needed.  Returns the 
	 * number of bytes written */
	SSL* pSsl = (SSL*)pThis->pSsl;
	
	if(!(SSL_get_mode(pSsl) & SSL_MODE_AUTO_RETRY)){
		das_error(DASERR_IO, "SSL connection not intialized with SSL_MODE_AUTO_RETRY");
		return 0;
	}
	int nRet = SSL_write(pSsl, sBuf, (int)uLen);
	if(nRet < 1) return uLen;
	
	char* sErr = das_ssl_getErr(pSsl, nRet);
	das_error(DASERR_IO, "SSL write error, %s", sErr);
	free(sErr);
	
	return 0;
}

/* ************************************************************************** */
/* Compressing Handling  */

/* return DAS_OKAY or an error code. */
/* TODO check that rw == 'w' and file != NULL and compress != 1 */
DasErrCode _DasIO_enterCompressMode(DasIO* pThis ) {
    pThis->compressed = 1;
    pThis->zstrm = (z_stream *)malloc(sizeof(z_stream));
    pThis->zstrm->zalloc = (alloc_func)Z_NULL;
    pThis->zstrm->zfree = (free_func)Z_NULL;
    pThis->zstrm->opaque = (voidpf)Z_NULL;
    pThis->zerr = deflateInit(pThis->zstrm, Z_DEFAULT_COMPRESSION);
    if (pThis->zerr != Z_OK) {
        return 22;
    }
    pThis->outbuf = (Byte *)malloc(sizeof(Byte) * CMPR_OUT_BUF_SZ);
    pThis->zstrm->next_out = pThis->outbuf;
    pThis->zstrm->avail_out = CMPR_OUT_BUF_SZ;
    return DAS_OKAY;
}

/* return DAS_OKAY or DAS_NOT_OKAY. */
/* TODO check that rw == 'r' and file != NULL and compress != 1 */
DasErrCode _DasIO_enterDecompressMode(DasIO* sid ) {
    sid->compressed = 1;
    sid->zstrm = (z_stream *)malloc(sizeof(z_stream));
    sid->zstrm->zalloc = (alloc_func)Z_NULL;
    sid->zstrm->zfree = (free_func)Z_NULL;
    sid->zstrm->opaque = (voidpf)Z_NULL;
    sid->zerr = inflateInit(sid->zstrm);
    if (sid->zerr != Z_OK) {
        return 22;
    }
    sid->inbuf = (Byte *)malloc(sizeof(Byte) * CMPR_IN_BUF_SZ);
    sid->zstrm->next_in = sid->inbuf;
    sid->zstrm->avail_in = 0;
    return DAS_OKAY;
}

/* TODO check for s->compressed != 1 and s->rw != 'r' */
int _DasIO_inflate_read(DasIO* pThis, char* data, size_t uLen)
{
	z_stream * zstrm = pThis->zstrm;
	ptrdiff_t nRec = 0;
	char* sErr = NULL;

	if(pThis->eof) { return 0; }
	
	zstrm->next_out = (unsigned char*)data;
	zstrm->avail_out = uLen;

	/* This looks like a bug, shouldn't we loop until we get uLen? */
	while(zstrm->avail_out != 0) {
		if(zstrm->avail_in == 0 && !pThis->eof) {
			if((pThis->mode == STREAM_MODE_FILE)||(pThis->mode == STREAM_MODE_CMD)){
				zstrm->avail_in = fread(pThis->inbuf, 1, CMPR_IN_BUF_SZ, pThis->file);
			}
			else{
				if(pThis->mode == STREAM_MODE_SOCKET){
					errno = 0;
					/* looks like a bug below, read doesn't itterate */
					nRec = recv(pThis->nSockFd, pThis->inbuf, CMPR_IN_BUF_SZ, 0);
					if(nRec == -1){
						das_error(DASERR_IO, "Error reading socket, %s", strerror(errno));
					}
					zstrm->avail_in = nRec;
				}
				else{
					nRec = SSL_read((SSL*)pThis->pSsl, pThis->inbuf, CMPR_IN_BUF_SZ);
					if(nRec == 0){
						if(SSL_get_shutdown((SSL*)pThis->pSsl) != 0) return 0;
						if(SSL_get_error((SSL*)pThis->pSsl, 0) == SSL_ERROR_ZERO_RETURN) 
							return 0;
					}
					if(nRec < 0){
						sErr = das_ssl_getErr(pThis->pSsl, nRec);
						das_error(DASERR_IO, "SSL read error %s", sErr);
						free(sErr);
						return 0;
					}
					zstrm->avail_in = nRec;
				}
			}
						  
			pThis->offset+= zstrm->avail_in;
			if(zstrm->avail_in == 0){
				pThis->eof = 1;
				if(ferror(pThis->file)){
					pThis->zerr = Z_ERRNO;
					break;
				}
			}
			zstrm->next_in = pThis->inbuf;
		}
		pThis->zerr = inflate(zstrm, Z_NO_FLUSH);
		if(pThis->zerr != Z_OK || pThis->eof) break;
	}
	return (uLen - pThis->zstrm->avail_out);
}

int _DasIO_inflate_getc(DasIO* pThis )
{
    unsigned char c;
    if (_DasIO_inflate_read(pThis, (char*) (&c), 1) == 1) return c;
    else return -1;
}

/** @todo: This function has multiple problems.  Partial writes are not 
 *          accounted for, and it can't write to a socket */
/* check for compressed != 1 and rw != 'w' */
size_t _DasIO_deflate_write(DasIO* pThis, const char* data, int length)
{
    z_stream * zstrm = pThis->zstrm;
    pThis->zstrm->next_in = (Bytef*)data;
    pThis->zstrm->avail_in = length;
	 size_t uSent = 0;

    while (zstrm->avail_in != 0) {
        if (zstrm->avail_out == 0) {
            zstrm->next_out = pThis->outbuf;
		
				if(pThis->mode == STREAM_MODE_FILE){
					/* bug from original, partial writes are not accounted for */
					uSent = fwrite(pThis->outbuf, 1, CMPR_OUT_BUF_SZ, pThis->file);
				}
				else{
					if(pThis->mode == STREAM_MODE_SOCKET)
						uSent = _DasIO_sockWrite(pThis, (char*)pThis->outbuf, CMPR_OUT_BUF_SZ);
					else
						uSent = _DasIO_sslWrite(pThis, (char*)pThis->outbuf, CMPR_OUT_BUF_SZ);
				}
					
				if(uSent) break;
            pThis->zstrm->avail_out = CMPR_OUT_BUF_SZ;
        }
        pThis->zerr = deflate(zstrm, Z_NO_FLUSH);
        if (pThis->zerr != Z_OK) break;
    }
    return (size_t)(length - pThis->zstrm->avail_in);
}

int _DasIO_deflate_fprintf( DasIO* pThis, const char * format, va_list va)
{
    int i;
    char * buf = (char *)malloc(4096);
    int length;

    length = vsnprintf(buf, 4096, format, va);
    if (length >= 4096) {
        free(buf);
        buf = (char *)malloc(length + 1);
        vsnprintf(buf, length + 1, format, va);
    }

    i = strlen(buf);
    pThis->zerr = _DasIO_deflate_write(pThis, buf, i);
    free(buf);
    return pThis->zerr;
}

DasErrCode _DasIO_deflate_flush( DasIO* pThis )
{
	int length;
	z_stream* zstrm = pThis->zstrm;
	int done = 0;

	zstrm->avail_in = 0;

	for (;;) {
		length = CMPR_OUT_BUF_SZ - zstrm->avail_out;
		if (length != 0) {
			if(pThis->mode == STREAM_MODE_FILE){
				if((uInt)fwrite(pThis->outbuf, 1, length, pThis->file) != length) {
					pThis->zerr = Z_ERRNO;
					return DASERR_IO;
				}
			}
			else{
				if(pThis->mode == STREAM_MODE_SOCKET){
					if(_DasIO_sockWrite(pThis, (char*)pThis->outbuf, length) != length){
						pThis->zerr = Z_ERRNO;
						return DASERR_IO;
					}
				}
				else{
					if(_DasIO_sslWrite(pThis, (char*)pThis->outbuf, length) != length){
						pThis->zerr = Z_ERRNO;
						return DASERR_IO;
					}
				}
			}
			
			pThis->zstrm->next_out = pThis->outbuf;
			pThis->zstrm->avail_out = CMPR_OUT_BUF_SZ;
		}
		
		if (done) break;
		pThis->zerr = deflate(zstrm, Z_FINISH);
		if (length == 0 && pThis->zerr == Z_BUF_ERROR) pThis->zerr = Z_OK;
		done = (zstrm->avail_out != 0 || pThis->zerr == Z_STREAM_END);
		if (pThis->zerr != Z_OK && pThis->zerr != Z_STREAM_END) break;
	}
	return pThis->zerr == Z_STREAM_END ? DAS_OKAY : DASERR_IO;
}


/* ************************************************************************ */
/* Public IO functions, hide compression from user                          */

int DasIO_getc(DasIO* pThis)
{	
	ssize_t nRet = 0;
	char c;
	int i = 0;
	char* sErr = NULL;
	switch(pThis->mode){
	case STREAM_MODE_STRING:
		if(pThis->nLength == 0){
			i = -1;
		}
		else{
			i = pThis->sBuffer[0];
			pThis->sBuffer++;
			pThis->nLength--;
		}
		break;
	case STREAM_MODE_FILE:
	case STREAM_MODE_CMD:
		if(pThis->compressed) {
			i = _DasIO_inflate_getc(pThis);
		}
		else{
			i = fgetc( pThis->file );
			pThis->offset++;
		}
		break;
	case STREAM_MODE_SOCKET:
		errno = 0;
		nRet = recv(pThis->nSockFd, &c, 1, 0);
		if(nRet == -1){
			das_error(DASERR_IO, "Error reading from host %s", strerror(errno));
			return -1;
		}
		i = c;
		break;
	case STREAM_MODE_SSL:
		/* Single character read, don't have to go round multiple times */
		nRet = SSL_read((SSL*)pThis->pSsl, &c, 1);
		if(nRet == 0){
			if(SSL_get_shutdown((SSL*)pThis->pSsl) != 0) return 0;
			if(SSL_get_error((SSL*)pThis->pSsl, 0) == SSL_ERROR_ZERO_RETURN) 
				return -1;
		}
		if(nRet < 0){
			sErr = das_ssl_getErr((SSL*)pThis->pSsl, nRet);
			das_error(DASERR_IO, "SSL read error %s", sErr);
			free(sErr);
			return -1;
		}
		i = c;
		break;
		
	default:
		das_error(DASERR_IO, "not implemented\n" ); abort();
		break;
	}
	  
    return i;
}

int DasIO_read(DasIO* pThis, DasBuf* pBuf, size_t uLen)
{
	int nRead = 0;
	
	if(pThis->compressed ){
		/* PITA: Breaking encapsalation.  Zlib stuff should move into the
		 * Buffer class itself */
		if(uLen > DasBuf_writeSpace(pBuf)) 
			das_error(DASERR_IO, "Buffer has %zu bytes of space left, can't write "
			                "%zu bytes.", DasBuf_writeSpace(pBuf), uLen);
			
		nRead = _DasIO_inflate_read(pThis, pBuf->pWrite, uLen);
			
		/* Definitely shouldn't do this here... */
		pBuf->pWrite += nRead;
		pBuf->pReadEnd = pBuf->pWrite;
	}
	else{
	
		switch(pThis->mode){
		case STREAM_MODE_STRING:	
			nRead = uLen < pThis->nLength ? uLen : pThis->nLength;
			DasBuf_write(pBuf, pThis->sBuffer, nRead);
			pThis->sBuffer += nRead;
			pThis->nLength -= nRead;
			break;
	
		case STREAM_MODE_FILE:
		case STREAM_MODE_CMD:
			nRead = DasBuf_writeFrom(pBuf, pThis->file, uLen);
			break;
		
		case STREAM_MODE_SOCKET:
			nRead = DasBuf_writeFromSock(pBuf, pThis->nSockFd, uLen);
			break;
		
		case STREAM_MODE_SSL:
			nRead = DasBuf_writeFromSSL(pBuf, pThis->pSsl, uLen);
			break;
	
		default:
			das_error(DASERR_NOTIMP, "not implemented\n" ); abort();
			return 0;
		}
	}
	
	if(nRead > 0)
		pThis->offset+= nRead;
	return nRead;
}


// TODO: Support this with specialized version of the DasBuf_write* 
//       functions for faster results
int DasIO_readUntil(DasIO* pThis, DasBuf* pBuf, size_t uMax, char cStop)
{
	int c;
	int nTotalRead = 0;
	int nRead = 0;
	for(size_t u = 0; u < uMax; ++u){
		nRead = DasIO_read(pThis, pBuf, 1);
		if(nRead < 1)
			return nRead;
		else
			nTotalRead += nRead;
		
		if( (c = DasBuf_last(pBuf)) < 0) 
			return -1 * das_error(DASERR_IO, 
				"Empty buffer while searching for %c in the input stream", cStop
			);

		if(c == (int)cStop)
			return nTotalRead;
	}

	return -1 * das_error(DASERR_IO, "Couldn't find %c within %zu bytes", cStop, uMax);
}

/* Should not be using int here, should ssize_t or ptrdiff_t */
size_t DasIO_write(DasIO* pThis, const char *data, int length) {
	
	if(pThis->compressed) 
		return _DasIO_deflate_write(pThis, data, length);
	
	switch(pThis->mode){
	case STREAM_MODE_FILE:
		return fwrite( data, 1, length, pThis->file );
		break;
	case STREAM_MODE_SOCKET:
		return _DasIO_sockWrite(pThis, data, length);
		break;
	
	default:
		das_error(DASERR_IO, "not implemented\n" );
		return -1;
		break;
	}
}

int DasIO_printf( DasIO* pThis, const char * format, ... ) {
	int i = 0;
	
	/* typical printf style output in less that 4K long, try to get by with 
	 * just a stack variable if you can */
	char sBuf[4096] = {'\0'};
	char* pWrite = NULL;
	ssize_t nLen = 0;
	va_list va;
	va_start(va, format);
	
	if(pThis->compressed){
		i = _DasIO_deflate_fprintf(pThis, format, va);
	} 
	else{
		switch(pThis->mode){
		case STREAM_MODE_STRING: 
			i = vsnprintf(pThis->sBuffer, pThis->nLength, format, va);
			pThis->sBuffer += i;
			pThis->nLength -= i;
			break;
	
		case STREAM_MODE_FILE:
		case STREAM_MODE_CMD:
			i = vfprintf( pThis->file, format, va );
			break;
		
		/* Both of these required that we do the string formatting, can't
		 * just punt down to a lower level library */
		case STREAM_MODE_SOCKET:
		case STREAM_MODE_SSL:
			nLen = vsnprintf(sBuf, 4095, format, va);
			if((nLen >= 4095)||(nLen < 0)) {
				pWrite = das_vstring(format, va);
				if(pThis->mode == STREAM_MODE_SOCKET)
					i = _DasIO_sockWrite(pThis, pWrite, nLen);
				else
					i = _DasIO_sslWrite(pThis, pWrite, nLen);
				free(pWrite);
			}
			else
				if(pThis->mode == STREAM_MODE_SOCKET)
					i = _DasIO_sockWrite(pThis, sBuf, nLen);
				else
					i = _DasIO_sslWrite(pThis, sBuf, nLen);
			break;
		
		default:
			das_error(DASERR_IO, "not implemented");
		}
	}
	va_end(va); 
	return i;
}

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wimplicit-fallthrough"

void DasIO_close(DasIO* pThis) {
	if(pThis->compressed) _DasIO_deflate_flush(pThis);
	int nRet = 0;
	switch(pThis->mode){
	case STREAM_MODE_FILE:
		if(pThis->rw == 'w') fflush(pThis->file);
		if((pThis->file != stdin) && (pThis->file != stdout))
			fclose(pThis->file);
		pThis->file = NULL;
		break;
	
	case STREAM_MODE_CMD:
		pclose(pThis->file);
		pThis->file = NULL;
		break;
	
	case STREAM_MODE_SSL:
		pThis->nSockFd = SSL_get_fd((SSL*)pThis->pSsl);
		nRet = SSL_shutdown((SSL*)pThis->pSsl);
		if(nRet == 0){
			/* Two-phase SSL shutdown needed, so do it again */
			nRet = SSL_shutdown((SSL*)pThis->pSsl);
		}
		if(nRet != 1){
			char* sErr = das_ssl_getErr(pThis->pSsl, nRet);
			das_error(DASERR_IO, "SSL shutdown error %s", sErr);
			free(sErr);
		}
		/* Fall through on purpose and close the socket as well */
	case STREAM_MODE_SOCKET:
#ifndef _WIN32
		shutdown(pThis->nSockFd, SHUT_RDWR);
		close(pThis->nSockFd);
#else
		shutdown(pThis->nSockFd, SD_BOTH);
		closesocket(pThis->nSockFd);
#endif
		break;
	default:
		/* Nothing special to do for string IO */
		break;
	}
}

#pragma GCC diagnostic pop

void del_DasIO(DasIO* pThis){
	if((pThis->file != NULL)||(pThis->zstrm != NULL)||(pThis->nSockFd != -1)||
		(pThis->pSsl != NULL)){
		DasIO_close(pThis);
	}
	/* Close out the write buffer here */
	del_DasBuf(pThis->pDb);
	OutOfBand_clean((OutOfBand*)&pThis->cmt);
	free(pThis);
}

/* ************************************************************************* */
/* Adding processors */
int DasIO_addProcessor(DasIO* pThis, StreamHandler* pProc)
{
	int i = 0;
	while(pThis->pProcs[i] != NULL) i++;
	if(i<DAS2_MAX_PROCESSORS){
		pThis->pProcs[i] = pProc;
		return i+1;
	}
	else {
		return -1 * das_error(DASERR_OOB, "Max number of processors exceeded");
	}
}

/* ************************************************************************* */
/* Processing the whole thing, Code in this area only has knowledge of the
 * basic stream structure.  Header encodings and data encodings are not it's
 * perogative 
 */

/* Helper, Returns:
 *  -N: Error code, exit with an error
 *   0: No packet, normal exit
 *   1: Packet is Descriptor Header, get ID from pPktID
 *   2: Packet is data, get ID from pPktID
 *   3: Packet is out-of-band info.
 */

/* For das IO, there's 2 chunking states
 *
 *  - Packets     (simple tag based chucks)
 *  - Documents   (have to parse, may be chunkable (XML is))
 *
 * Four packet taging schemes
 *  - das1-untagged 
 *  - das1-tagged
 *  - das2
 *  - das3
 *
 * There's 4 encodings
 *  - Header, XML
 *  - Header, JSON
 *  - Data (determined by headers)
 *  - Extension (unknown)
 *
 * There two packet dispositions:
 *  - In band data     (descriptors, data, documents)
 *  - Out of band data
 */
#define IO_CHUNK_PKT  0x0001
#define IO_CHUNK_DOC  0x0002
#define IO_CHUNK_MASK 0x000F

#define IO_TAG_D1U    0x0000
#define IO_TAG_D1T    0x0010
#define IO_TAG_D2     0x0020
#define IO_TAG_D3     0x0030
#define IO_TAG_MASK   0x00F0

#define IO_ENC_XML    0x0100
#define IO_ENC_JSON   0x0200
#define IO_ENC_DATA   0x0300
#define IO_ENC_EXT    0x0400
#define IO_ENC_MASK   0x0F00

#define IO_USAGE_CNT  0x1000  // content, pass down to parsers
#define IO_USAGE_OOB  0x2000  // out-of-band, parse in I/O layer
#define IO_USAGE_PASS 0x3000  // Just pass it to the output 
#define IO_USAGE_MASK 0xF000

int _DasIO_dataTypeOrErr(DasIO* pThis, DasBuf* pBuf, bool bFirstRead, int* pPktId)
{
	int nContent = 0;
	uint32_t uPack = 0;
	char sTag[5] = {'\0'};
	char sPktId[12] = {'\0'};
	
	int nRead = DasIO_read(pThis, pBuf, 4);
	if((bFirstRead)&&(nRead < 3))
		return -1 * das_error(DASERR_IO, "Input stream %s contains no packets.", pThis->sName);
	
	if(nRead == 0) return 0;  /* Normal end, except on first read */
		
	if(nRead < 3){
		/* Abnormal stream end, but just log it, don't trigger the error response */
		fprintf(stderr, "Partial packet in stream %s.", pThis->sName);
		return -1 * DASERR_IO;
	}
	DasBuf_read(pBuf, sTag, 4);

	// If a document type (not packets) just return now
	switch(sTag[0]){
	case '<':
		// Save the first 4 bytes in pPktId so that they don't evaporate
		uPack = ( (uint32_t)(ubyte)sTag[0] )|( ((uint32_t)(ubyte)sTag[1]) >> 8 )|
		        ( ((uint32_t)(ubyte)sTag[2]) >> 16 )|( ((uint32_t)(ubyte)sTag[3]) >> 24 );
		*pPktId = *((int*)(&uPack));
		if(bFirstRead)
			return (IO_CHUNK_DOC | IO_ENC_XML);
		
		return -1 * das_error(DASERR_IO, 
			"Unpacketized XML document discovered in packetize stream at offset %ld",
			pThis->offset
		);

	case '{':
		// Save the first 4 bytes in pPktId so that they don't evaporate
		uPack = ( (uint32_t)(ubyte)sTag[0] )|( ((uint32_t)(ubyte)sTag[1]) >> 8 )|
		        ( ((uint32_t)(ubyte)sTag[2]) >> 16 )|( ((uint32_t)(ubyte)sTag[3]) >> 24 );
		*pPktId = *((int*)(&uPack));

		if(bFirstRead)
			return (IO_CHUNK_DOC | IO_ENC_JSON);
		
		return -1 * das_error(DASERR_IO, 
			"Unpacketized JSON document discovered in packetize stream at offset %ld",
			pThis->offset
		);

	case '[':
		if(sTag[3] != ']')
			break;

		if(tolower(sTag[1]) == 'x' && tolower(sTag[2]) == 'x'){
			nContent = (IO_CHUNK_PKT | IO_TAG_D2 | IO_ENC_XML | IO_USAGE_OOB);
		}
		else{
			if(!isdigit(sTag[1]) || !isdigit(sTag[2]) )
				break;

			nContent = (IO_CHUNK_PKT | IO_TAG_D2 | IO_ENC_XML | IO_USAGE_CNT);

			if(bFirstRead){
				if( (sTag[1] != '0')||(sTag[2] != '0') )
					return -1 * das_error(DASERR_IO, 
						"Input is not a valid das-basic-stream-v2.2. Valid streams start "
						"with [00], the input started with: %02X %02X %02X %02X (%c%c%c%c)\n", 
						(unsigned int)sTag[0], (unsigned int)sTag[1], (unsigned int)sTag[2],
						(unsigned int)sTag[3], sTag[0], sTag[1], sTag[2], sTag[3]
					);
			}
			else{
				if((sTag[1] == '0')&&(sTag[2] == '0')){
					return -1 * das_error(DASERR_IO,
						"Packet ID 0 is only valid for the initial stream header and may not "
						"repeat in the packet (repeat sighted at offset %ld", pThis->offset
					);
				}
			}
		}
		sTag[3] = '\0';
		sscanf(sTag+1, "%d", pPktId);

		return nContent;

	case ':':
		if((!isdigit(sTag[1]))||(!isdigit(sTag[2]))||(sTag[3] != ':'))
			break;
		
		sTag[3] = '\0';
		sscanf(sTag+1, "%d", pPktId);
		return (IO_CHUNK_PKT | IO_TAG_D2 | IO_ENC_DATA | IO_USAGE_CNT);
	
	case '|':
		if(sTag[3] != '|')
			break;

		if((nRead = DasIO_readUntil(pThis, pBuf, 11, '|')) < 0)
			return nRead;
		assert(nRead > 0);

		nRead = DasBuf_read(pBuf, sPktId, nRead);
		sPktId[nRead - 1] = '\0';
		
		if(sPktId[0] == '\0')
			*pPktId = 0;
		else
			if(sscanf(sPktId, "%d", pPktId) != 1)
				return -1 * das_error(DASERR_IO, "Invalid packet ID character at offset %ld", pThis->offset);
		

		// the known packet types designators for das3 are (from das2py reader.py)
		// 	"Sx" - XML stream definition (parse for content)
		//    "Hx" - XML packet definition (parse for content)
		//    "Pd" - Packetize data, content defined by a header
		//    "Cx" - XML Comment packet (XML content)
		//    "Ex" - XML Exception packet (XML content)
		//    "XX" - Extra packet, content completely unknown
		nContent = (IO_CHUNK_PKT | IO_TAG_D3);

		// If this is the first read, this must be a stream header
		if(bFirstRead){
			if(sTag[1] != 'S')
				return -1 * das_error(DASERR_IO, "Input is not a valid das-basic-stream-v3.0, "
					"Valid streams start |Sx| or |Sj|, this one started with "
					"%02X %02X %02X %02X (%c%c%c%c)\n",
					(unsigned int)sTag[0], (unsigned int)sTag[1], (unsigned int)sTag[2],
					(unsigned int)sTag[3], sTag[0], sTag[1], sTag[2], sTag[3]
				);
		}
		else{
			if(sTag[1] == 'S')
				return -1 * das_error(DASERR_IO, "Stream header detected after the first "
					"packet at offset %ld", pThis->offset);
		}
		
		switch(sTag[1]){
			case 'S':
			if(bFirstRead){
				if(*pPktId != 0)
					return -1 * das_error(DASERR_IO, 
						"Input is not a valid das-basic-stream-v3.0, Valid streams start "
						"with packet ID 0 (or not packet ID at all), this one started with "
						"id %d", *pPktId
					);
			}
			else{
				if(*pPktId == 0)
					return -1 * das_error(DASERR_IO, "Packet ID 0 is only valid for the "
						"initial stream header.  ID 0 found at offset %ld", pThis->offset
					);
			}
			nContent |= IO_USAGE_CNT;
			break;

			case 'H': 
			case 'P': 
			case 'X': nContent |= IO_USAGE_CNT; break;
			
			case 'C': 
			case 'E': nContent |= IO_USAGE_OOB; break;
			default:
				goto TAG_ERROR;
		}

		if(sTag[2] == 'x') nContent |= IO_ENC_XML;
		else if(sTag[2] == 'j') nContent |= IO_ENC_JSON;
		else if(sTag[2] == 'd') nContent |= IO_ENC_DATA;
		else nContent |= IO_ENC_EXT;

		return nContent;

	default:  // Unknown first character...
		break;
	}

TAG_ERROR:
	return -1 * das_error(DASERR_IO, 
		"Unknown bytes %02X %02X %02X %02X (%c%c%c%c) at input offset %ld\n",
		(unsigned int)sTag[0], (unsigned int)sTag[1], (unsigned int)sTag[2],
		(unsigned int)sTag[3], sTag[0], sTag[1], sTag[2], sTag[3],
		pThis->offset
	);	
}

int _DasIO_sizeOrErr(
	DasIO* pThis, DasBuf* pBuf, int nContent, StreamDesc* pSd, int nPktId
){
	int nPktSz; 
	char sLen[12] = {'\0'};
	int nRead = 0;

	bool bNoLen = (nContent & (IO_TAG_MASK | IO_ENC_MASK)) == (IO_TAG_D2|IO_ENC_DATA);
	
	/* All other packets have lengths... */
	if(!bNoLen){

		// For das2 tags we just read the next 6 bytes
		if((nContent & IO_TAG_MASK) == (IO_TAG_D2)){
			if(DasIO_read(pThis, pBuf, 6) != 6){ 
				return -1 * das_error(DASERR_IO, "Input stream ends in a partial packet");
			}
		
			DasBuf_read(pBuf, sLen, 6); /* Advances the read point */
		
			if(sscanf(sLen, "%d", &nPktSz) != 1){
				return -1 * das_error(DASERR_IO, "Can't get packet size from bytes %s", sLen);
			}
		}
		else{
			if((nContent & IO_TAG_MASK) != (IO_TAG_D3))
				return -1 * das_error(DASERR_IO, "Unknown packet tag type");
			
			if( (nRead = DasIO_readUntil(pThis, pBuf, 10, '|')) < 0)
				return nRead;

			if(nRead < 2)
				return -1 * das_error(DASERR_IO, 
					"No packet size provided for packet ID %d at offset %ld",
					nPktId, pThis->offset
				);

			nRead = DasBuf_read(pBuf, sLen, nRead);
			sLen[nRead - 1] = '\0';
			if(sscanf(sLen, "%d", &nPktSz) != 1)
				return -1 * das_error(DASERR_IO, "Can't get packet syze from bytes %s", sLen);
		}
	}
	else{
		/* ...Old das2 data packets don't */
		if(pSd == NULL)
			return -1 * das_error(DASERR_IO, "Data packets received before stream header");
	
		DasDesc* pDesc = pSd->descriptors[nPktId];
		if(pDesc == NULL)
			return -1 * das_error(DASERR_IO, "Packet type %02d data received before packet "
						            	"type %02d header", nPktId, nPktId);

		if(pDesc->type == DATASET){
			nPktSz = DasDs_recBytes((DasDs*)pDesc);
			if(nPktSz == 0){
				if(DasDs_numCodecs((DasDs*)pDesc) == 0)
					return -1 * das_error(DASERR_IO, "No codecs are defined for the dataset");
				else
					return -1 * das_error(DASERR_IO, "Logic error in io.c");
			}
			if(nPktSz < 0)
				return -1 * das_error(DASERR_IO, "Das2 streams do not support variable length packets");
		}
		else{ 
			if(pDesc->type == PACKET)
				nPktSz = PktDesc_recBytes((PktDesc*)pDesc);
			else
				return -1 * das_error(DASERR_IO, "Logic error in io.c");
		}
	}
	return nPktSz;
}

DasErrCode _DasIO_handleDesc(
	DasIO* pThis, DasBuf* pBuf, StreamDesc** ppSd, int nPktId
){
	DasDesc* pDesc = NULL;
	StreamDesc* pSd = *ppSd;
	StreamHandler* pHndlr = NULL;
	DasErrCode nRet = 0;
	
	// Supply the stream descriptor if it exits
	if( (pDesc = DasDesc_decode(pBuf, pSd, nPktId, pThis->model)) == NULL)
		return DASERR_IO;
	
	if(pDesc->type == STREAM){
		if(*ppSd != NULL)
			return das_error(DASERR_IO, "Multiple Stream descriptors in input");

		*ppSd = (StreamDesc*)pDesc;
		pSd = *ppSd;
		if(strcmp("deflate", pSd->compression)==0)
			_DasIO_enterDecompressMode(pThis);
	}
	else{
		if((pDesc->type == PACKET)||(pDesc->type == DATASET)){
			if(pSd == NULL)
				return das_error(DASERR_IO,
					"Streams must be defined before datasets can be defined"
				);
		
			/* Handle packet redefinitions. */
			if(pSd->descriptors[nPktId] != NULL){
				
				/* Let any stream processors know that this packet desc is about
				 * to be deleted so that they can do stuff with the old one 1st */
				for(size_t u = 0; pThis->pProcs[u] != NULL; u++){
					pHndlr = pThis->pProcs[u];
					if(pHndlr->pktRedefHandler != NULL)
						nRet = pHndlr->pktRedefHandler(
							pSd, (PktDesc*)(pSd->descriptors[nPktId]), pHndlr->userData
						);
				
					if(nRet != 0) break;
				}
				
				StreamDesc_freeDesc(pSd, nPktId);
			}
			
			if((nRet = StreamDesc_addPktDesc(pSd, pDesc, nPktId)) != 0)
				return nRet;
		}
		else{
			return das_error(DASERR_IO, "Only Stream and Packet descriptors expected");
		}
	}
			
	/* Call the stream handlers */
	for(size_t u = 0; pThis->pProcs[u] != NULL; u++){
		pHndlr = pThis->pProcs[u];
		switch(pDesc->type){
		case STREAM:
			if(pHndlr->streamDescHandler != NULL)
				nRet = pHndlr->streamDescHandler(pSd, pHndlr->userData);
			break;
		case PACKET:
			if(pHndlr->pktDescHandler != NULL)
				nRet = pHndlr->pktDescHandler(pSd, (PktDesc*)pSd->descriptors[nPktId], pHndlr->userData);
			break;
		case DATASET:
			if(pHndlr->dsDescHandler != NULL)
				nRet = pHndlr->dsDescHandler(
					pSd, nPktId, (DasDs*)pSd->descriptors[nPktId], pHndlr->userData
				);
			break;
		default:
			nRet = das_error(DASERR_IO, "Unexpected descriptor type %d", pDesc->type);
			break;
		}
		if(nRet != 0) break;
	}
	
	return nRet;
}

DasErrCode _DasIO_handleData(
	DasIO* pThis, DasBuf* pBuf, StreamDesc* pSd, int nPktId
){
	int nRet = 0;
	StreamHandler* pHndlr = NULL;
	
	DasDesc* pDesc = pSd->descriptors[nPktId];
	
	if(pDesc->type == PACKET)
		nRet = PktDesc_decodeData((PktDesc*)pDesc, pBuf);
	else if(pDesc->type == DATASET)
		nRet = dasds_decode_data((DasDs*)pDesc, pBuf);
	else
		assert(false);

	if(nRet != 0) return nRet;
	
	bool bClearDs = false;
	for(size_t u = 0; pThis->pProcs[u] != NULL; u++){
		pHndlr = pThis->pProcs[u];
		
		if((pDesc->type == PACKET)&&(pHndlr->pktDataHandler != NULL))
			nRet = pHndlr->pktDataHandler((PktDesc*)pDesc, pHndlr->userData);

		else if(pDesc->type == DATASET){
			if(pHndlr->dsDataHandler == NULL)
				bClearDs = true;
			else
				nRet = pHndlr->dsDataHandler(pSd, nPktId, (DasDs*)pDesc, pHndlr->userData);
		}

		if(nRet != DAS_OKAY) break;
	}

	/* Since data sets can hold an arbitrary number of packets, clear
	 * them if no handler */
	if(bClearDs)
		DasDs_clearRagged0Arrays((DasDs*)pDesc);

	return nRet;
}

DasErrCode _DasIO_handleOOB(DasIO* pThis, DasBuf* pBuf, OutOfBand** ppObjs)
{	
	int nRet = 0;
	int nWhich = -1;
	StreamHandler* pHndlr = NULL;
	if( (nRet = OutOfBand_decode(pBuf, ppObjs, &nWhich)) != 0) return nRet;
	
	/* Just Drop stuff I don't understand */
	if(nWhich < 0) return 0;
	
	OutOfBand* pOob = ppObjs[nWhich];
	
	/* Call the stream handlers */
	for(size_t u = 0; pThis->pProcs[u] != NULL; u++){
		pHndlr = pThis->pProcs[u];
		if(pOob->pkttype == OOB_COMMENT){
			if(pHndlr->commentHandler != NULL)
				nRet = pHndlr->commentHandler((OobComment*)pOob, pHndlr->userData);
		}
		else{
			if(pHndlr->exceptionHandler != NULL)
				nRet = pHndlr->exceptionHandler((OobExcept*)pOob, pHndlr->userData);
		}
		
		if(nRet != 0) break;
	}
	
	return nRet;
}

DasErrCode DasIO_readAll(DasIO* pThis)
{
	DasErrCode nRet = 0;
	StreamDesc* pSd = NULL;
	
	OobComment sc;
	OobExcept ex;
	OobComment_init(&sc);
	OobExcept_init(&ex);
	OutOfBand* oobs[3] = {(OutOfBand*)&sc, (OutOfBand*)&ex, NULL};
	
	int nPktId, nBytes;
	int nContent;
	
	DasBuf* pBuf = pThis->pDb;
	bool bFirstRead = true;
	
	if(pThis->rw == 'w'){
		return das_error(DASERR_IO, "Can't read input, this is an output stream");
	}

	/* Loop over all the input packets, make sure to break out of the loop
	 * instead of just returning so that the stream close handlers can be called. 
	 */
	while(nRet == 0){
		DasBuf_reinit(pBuf);
		
		/* What Kind of Packet do we have? */
		nPktId = -1;
		if((nContent = _DasIO_dataTypeOrErr(pThis, pBuf, bFirstRead, &nPktId)) < 1){
			nRet = -1 * nContent;
			break;
		}
		bFirstRead = false;
		
		/* Get the number of bytes to read next */
		if((nContent & IO_CHUNK_MASK) == IO_CHUNK_PKT){

			nBytes = _DasIO_sizeOrErr(pThis, pBuf, nContent, pSd, nPktId);
			if(nBytes < 0){ 
				nRet = -1 * nBytes;
				break;
			}
			if(nBytes == 0){
				/* Wow, a null packet, let's call those illegal */
				nRet = das_error(DASERR_IO, "0-length input packet.");
				break;
			}
		
			/* Read the bytes */
			if(nBytes > pBuf->uLen){
				nRet = das_error(DASERR_IO, "Packet's length is %d, library buffer is only"
					            	"%zu bytes long", nBytes, pThis->pDb->uLen);
				break;
			}
		
			if( DasIO_read(pThis, pBuf, nBytes) != nBytes){
				nRet = das_error(DASERR_IO, "Partial packet on input at offset %ld", pThis->offset);
				break;
			}
		}
		else{
			nRet = das_error(DASERR_IO, "Un-packetized documents are not yet supported");
			break;
		}

		switch(nContent & IO_ENC_MASK){
		case IO_ENC_JSON:
			nRet = das_error(DASERR_IO, "JSON stream parsing is not yet supported");
			break;
		case IO_ENC_EXT:
			nRet = das_error(DASERR_IO, "Extension formats are not yet supported");
			break;
		case IO_ENC_XML:
			if((nContent & IO_USAGE_MASK) == IO_USAGE_CNT)
				nRet = _DasIO_handleDesc(pThis, pBuf, &pSd, nPktId); 
			else if((nContent & IO_USAGE_MASK) == IO_USAGE_OOB)
				nRet = _DasIO_handleOOB(pThis, pBuf, oobs);
			else
				nRet = das_error(DASERR_IO, "XML pass through is not yet supported");

			break;
		case IO_ENC_DATA:
			nRet = _DasIO_handleData(pThis, pBuf, pSd, nPktId);  
			break;
		default:
			nRet = das_error(DASERR_IO, "Logic error in stream parser");
			break;
		}
		if(nRet != 0) break;
		
	}/* repeat until endOfStream */

	/* Now for the close handlers */
	StreamHandler* pHndlr = NULL;
	int nHdlrRet = 0;
	for(size_t u = 0; pThis->pProcs[u] != NULL; u++){
		pHndlr = pThis->pProcs[u];
		if(pHndlr->closeHandler != NULL){
			nHdlrRet = pHndlr->closeHandler(pSd, pHndlr->userData);
			if(nHdlrRet != 0) break;
		}
	}
	
	OutOfBand_clean((OutOfBand*)&sc);
	OutOfBand_clean((OutOfBand*)&ex);
	if(pSd) del_StreamDesc(pSd);
	
	return nRet == 0 ? nHdlrRet : nRet ;
}

/* ************************************************************************* */
/* Logging and Task Tracking */

const char* LogLvl_string(int logLevel) 
{
	switch (logLevel) {
	case LOGLVL_FINEST: return "finest";
	case LOGLVL_FINER: return "finer";
	case LOGLVL_FINE: return "fine";
	case LOGLVL_CONFIG: return "config";
	case LOGLVL_INFO: return "info";
	case LOGLVL_WARNING: return "warning";
	case LOGLVL_ERROR:  
	default: das_error(19, "unrecognized log level: %d", logLevel );
	}
   return NULL; /* make compiler happy */ 
}

void DasIO_setLogLvl(DasIO* pThis, int logLevel){
    pThis->logLevel = logLevel;
}

DasErrCode DasIO_sendLog(
	DasIO* pThis, int level, char* sFmt, ... 
){
	OobComment* pCmt = &(pThis->cmt);
    va_list argp;
    char value[DAS_XML_BUF_LEN];
	 char type[DAS_XML_NODE_NAME_LEN];

    if(level < pThis->logLevel) return 0;
    
    va_start(argp, sFmt);
    vsnprintf(value, DAS_XML_BUF_LEN - 128, sFmt, argp );
    va_end(argp);
    
	snprintf(type, DAS_XML_NODE_NAME_LEN-1, "log:%s", LogLvl_string( level ) );
	das_store_str(&(pCmt->sType), &(pCmt->uTypeLen), type);
	das_store_str(&(pCmt->sVal), &(pCmt->uValLen), value);
	 
	DasIO_writeComment(pThis, pCmt);
	return 0;
}

DasErrCode DasIO_setTaskSize(DasIO* pThis, int size) {
    if ( pThis->bSentHeader ) {
        return das_error(DASERR_OOB, "setTaskSize must be called before the stream "
					               "descriptor is sent.\n" );
    }
	 struct timeval tv;
    pThis->taskSize= size;

    gettimeofday(&tv, NULL);
	 
    /*pThis->tmLastProgMsg= ( (long)(tp.time)-1073706472 ) * 1000 + 
				                  (long)(tp.millitm);*/
	 pThis->tmLastProgMsg = ((double)tv.tv_sec) * 1000 + (tv.tv_usec / 1000.0);
    
    return DAS_OKAY;
}

DasErrCode DasIO_setTaskProgress( DasIO* pThis, int progress ) {
   
	DasErrCode nRet = 0;
	char ss[64] = {'\0'};
	struct timeval tv;
	OobComment cmt;
	cmt.base.pkttype = OOB_COMMENT;
	
	long elapsedTimeMillis;
	double nowTime;
    
	static int decimate=1;
	static int decSz=1;
	int newDecSz;  

	const int targetUpdateRateMilli= 100; 

	decimate--;
	if(decimate == 0){ 
		gettimeofday(&tv, NULL); 
		
		/* nowTime = ( (long)(tp.time)-1073706472 ) * 1000 + (long)(tp.millitm); */
		
		nowTime = ((double)tv.tv_sec) * 1000 + (tv.tv_usec / 1000.0);
		
		elapsedTimeMillis = (int)(nowTime - pThis->tmLastProgMsg);

		/* target progress messages to 1 per targetUpdateRateMilli milliseconds by 
		 * decimation */

		/* calculate the decimation rate implied by the last interval.  
		 * Where +1 is added, this assumes that elapsedTimeMillis is large wrt 1, 
		 * also the decimate rate. */
		newDecSz = (decSz*targetUpdateRateMilli/(elapsedTimeMillis+1)) + 1; 
		decSz= 0.5 * decSz + 0.5 * newDecSz ; /* put in some intertia */

		decimate = decSz;

		sprintf( ss, "%d", progress );
		
		cmt.sType = "taskProgress";
		cmt.sVal = ss;
		cmt.sSrc = pThis->sName;
		cmt.uSrcLen = strlen(pThis->sName);
		pThis->tmLastProgMsg= nowTime;
		DasBuf* pBuf = pThis->pDb;
		DasBuf_reinit(pThis->pDb);
		
		if( (nRet = OobComment_encode(&cmt, pBuf)) != 0) return nRet;
		DasIO_printf(pThis, "[xx]%06d%s", DasBuf_written(pBuf), pBuf->sBuf);
    } 
	 
	return 0;
}

/* ************************************************************************* */
/* Top Level Send Functions */

DasErrCode DasIO_writeStreamDesc(DasIO* pThis, StreamDesc* pSd)
{
	if(pThis->rw == 'r')
		return das_error(DASERR_IO, "Can't write, this is an input stream.");
	if(pThis->bSentHeader) 
		return das_error(DASERR_IO, "Can't double send a Das2 Stream Header");
	
	if(!DasDesc_has(&(pSd->base), "sourceId"))
		DasDesc_setStr(&(pSd->base), "sourceId", pThis->sName);
	
	DasBuf* pBuf = pThis->pDb;
	DasBuf_reinit(pBuf);
	
	int nRet;

	if( (nRet = StreamDesc_encode(pSd, pBuf)) != 0) return nRet;
	if(pThis->dasver == 2)
		DasIO_printf(pThis, "[00]%06zu%s", DasBuf_written(pBuf), pBuf->sBuf);
	else
		DasIO_printf(pThis, "|Sx||%zu|%s", DasBuf_written(pBuf), pBuf->sBuf);
	
	if(strcmp( "deflate", pSd->compression ) == 0 ){
		_DasIO_enterCompressMode(pThis);
	}
	
	if(pThis->taskSize > 0)
		nRet = DasIO_setTaskSize(pThis, pThis->taskSize);

	pThis->bSentHeader = true;
	return nRet;
}

DasErrCode DasIO_writePktDesc(DasIO* pThis, PktDesc* pPd )
{   
	if(pThis->rw == 'r')
		return das_error(DASERR_IO, "Can't write, this is an input stream.");
	if(! pThis->bSentHeader) 
		return das_error(DASERR_IO, "Send the stream descriptor first");
	
	int nRet = 0;
	DasBuf* pBuf = pThis->pDb;
	DasBuf_reinit(pBuf);
	
	if( (nRet = PktDesc_encode(pPd, pBuf)) != 0) return nRet;
	size_t uToWrite = DasBuf_unread(pBuf) + 10;

	if(pThis->dasver == 2){
		if( DasIO_printf(
			pThis, "[%02d]%06d%s", pPd->id, DasBuf_unread(pBuf), pBuf->pReadBeg
		) != uToWrite)
			return das_error(DASERR_IO, "Partial packet descriptor written");
	}
	else{
		if( DasIO_printf(
			pThis, "|Hx|%02d|%d|%s", pPd->id, DasBuf_unread(pBuf), pBuf->pReadBeg
		) != uToWrite)
			return das_error(DASERR_IO, "Partial packet descriptor written");
	}
		
	 
	pPd->bSentHdr = true;
	return DAS_OKAY;
}

int DasIO_writePktData(DasIO* pThis, PktDesc* pPdOut ) {
	
	if(pThis->rw == 'r')
		return das_error(DASERR_IO, "Can't write, this is an input stream.");
	if(! pThis->bSentHeader) 
		return das_error(DASERR_IO, "Send the stream descriptor first");
	if(! pPdOut->bSentHdr)
		return das_error(DASERR_IO, "Send packet header ID %02d first", pPdOut->id);
	
	int nRet = 0;
	DasBuf* pBuf = pThis->pDb;
	DasBuf_reinit(pBuf);
	
	if( (nRet = PktDesc_encodeData(pPdOut, pBuf)) != 0) return nRet;

	if(pThis->dasver == 2)
		DasIO_printf(pThis, ":%02d:", pPdOut->id);
	else
		DasIO_printf(pThis, "|Pd|%d|%d|", pPdOut->id, DasBuf_unread(pBuf));
	DasIO_write(pThis, pBuf->pReadBeg, DasBuf_unread(pBuf));
	
	return 0;
}

DasErrCode DasIO_writeException(DasIO* pThis, OobExcept* pSe)
{
	if(pThis->rw == 'r')
		return das_error(DASERR_IO, "Can't write, this is an input stream.");
	
   if( !pThis->bSentHeader ) {
		return das_error(DASERR_OOB, "streamDescriptor not sent before steamComment!\n");
	}
	DasErrCode nRet = 0;
	DasBuf_reinit(pThis->pDb);  /* Write zeros up to the previous data point */
	if((nRet = OobExcept_encode(pSe, pThis->pDb)) != 0) return nRet;
	
	int nWrote = DasIO_printf(pThis, "[xx]%06zu", DasBuf_written(pThis->pDb)); 
	nWrote += DasIO_write(pThis, pThis->pDb->pReadBeg, DasBuf_written(pThis->pDb));
	if(nWrote > 10) return 0;
	
	return das_error(DASERR_IO, "Error writing stream comment");
}

DasErrCode DasIO_writeComment(DasIO* pThis, OobComment* pSc) 
{ 
	if(pThis->rw == 'r')
		return das_error(DASERR_IO, "Can't write, this is an input stream.");
	
	if( !pThis->bSentHeader ) {
		return das_error(DASERR_OOB, "streamDescriptor not sent before steamComment!\n");
	}
	DasErrCode nRet = 0;
	DasBuf_reinit(pThis->pDb);  /* Write zeros up to the previous data point */
	if((nRet = OobComment_encode(pSc, pThis->pDb)) != 0) return nRet;
	
	int nWrote = DasIO_printf(pThis, "[xx]%06zu", DasBuf_written(pThis->pDb));
	nWrote += DasIO_write(pThis, pThis->pDb->pReadBeg, DasBuf_written(pThis->pDb));
	if(nWrote > 10) return 0;
	
	return das_error(DASERR_IO, "Error writing stream comment");
}

/* ************************************************************************* */
/* Exit with message or exception */

void DasIO_throwException(
	DasIO* pThis, StreamDesc* pSd, const char* type, char* message
){
	if(pThis->rw == 'r'){
		int nErr = das_error(DASERR_IO, "DasIO_throwException: Can't write, this is an "
		                  "input stream.");
		exit(nErr); /* One of the few times exit should be explicitly called */
	}
	
   OobExcept se;
	char sType[128] = {'\0'};
	strncpy(sType, type, 127);
	
   if(!pThis->bSentHeader)
		DasIO_writeStreamDesc(pThis, pSd);
   
	se.base.pkttype = OOB_EXCEPT;
   se.sType = sType;
   se.sMsg = message;
   
   DasIO_writeException(pThis, &se );
	DasIO_close(pThis);
	del_DasIO(pThis);	
}

void DasIO_vExcept(DasIO* pThis, const char* type, const char* fmt, va_list ap)
{
	
	if(pThis->rw == 'r'){
		das_error(DASERR_ASSERT, "DasIO_throwException: Can't write, this is "
		           "an input stream.");
		exit(DASERR_ASSERT); /* One of the few times exit should be explicitly called */
	}
	
	OobExcept se;
	char sType[128] = {'\0'};
	strncpy(sType, type, 127);
	
   if(!pThis->bSentHeader){
		StreamDesc* pSd = new_StreamDesc();
		DasIO_writeStreamDesc(pThis, pSd);
		del_StreamDesc(pSd);
	}
   
	se.base.pkttype = OOB_EXCEPT;
   se.sType = sType;
   se.sMsg = das_vstring(fmt, ap);
   
   DasIO_writeException(pThis, &se );
	free(se.sMsg);  /* Make valgrind happy */
	DasIO_close(pThis);
}

int DasIO_serverExcept(DasIO* pThis, const char* fmt, ...)
{
	va_list ap;
	va_start(ap, fmt);
	DasIO_vExcept(pThis, DAS2_EXCEPT_SERVER_ERROR, fmt, ap);
	va_end(ap);
	return 11;
}

int DasIO_queryExcept(DasIO* pThis, const char* fmt, ...)
{
	va_list ap;
	va_start(ap, fmt);
	DasIO_vExcept(pThis, DAS2_EXCEPT_ILLEGAL_ARGUMENT, fmt, ap);
	va_end(ap);
	return 11;
}

int DasIO_closeNoData(DasIO* pThis, const char* fmt, ...)
{
	va_list ap;
	va_start(ap, fmt);
	DasIO_vExcept(pThis, DAS2_EXCEPT_NO_DATA_IN_INTERVAL, fmt, ap);
	va_end(ap);
	return 0;
}
