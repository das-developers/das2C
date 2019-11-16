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

DasIO* new_DasIO_cfile(const char* sProg,  FILE * file, const char* mode ) 
{
	if(file == NULL){
		das_error(DASERR_IO, "NULL file pointer argument");
		return NULL;
	}
	
	DasIO* pThis = (DasIO*)calloc(1, sizeof( DasIO ) );
	pThis->mode= STREAM_MODE_FILE;
	pThis->file = file;
	pThis->nSockFd = -1;
	pThis->taskSize= -1;  /* for progress indication */
	pThis->logLevel=LOGLVL_WARNING;
	strncpy(pThis->sName, sProg, DASIO_NAME_SZ - 1);
	OobComment_init(&pThis->cmt);
	das_store_str(&(pThis->cmt.sSrc), &(pThis->cmt.uSrcLen), pThis->sName);
	 
	 if(strchr(mode, 'r') != NULL){ 
		 pThis->rw = 'r';
	 }
	 else{
		if(strchr(mode, 'w') != NULL){
			pThis->rw = 'w';
			if(strchr(mode, 'c') != NULL) pThis->compressed = true;
		 }
		else{
			das_error(DASERR_IO, "Illegal argument for mode in new_DasIO_cfile");
			free(pThis);
			return NULL;
		}
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
	pThis->taskSize= -1;  /* for progress indication */
	pThis->logLevel=LOGLVL_WARNING;
	pThis->nSockFd = -1;
	strncpy(pThis->sName, sProg, DASIO_NAME_SZ - 1);
	OobComment_init(&(pThis->cmt));
	das_store_str(&(pThis->cmt.sSrc), &(pThis->cmt.uSrcLen), pThis->sName);
	
	if(strchr(mode, 'r') != NULL){ 
		 pThis->rw = 'r';
		 pThis->file = fopen(sFile, "rb");
	 }
	 else{
		if(strchr(mode, 'w') != NULL){
			pThis->rw = 'w';
			pThis->file = fopen(sFile, "wb");
			if(strchr(mode, 'c') != NULL) pThis->compressed = true;
		 }
		else{
			das_error(22, "Illegal argument for mode in new_DasIO_cfile");
			free(pThis);
			return NULL;
		}
	 }
	
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
	pThis->nSockFd = nSockFd;
	pThis->taskSize= -1;  /* for progress indication */
	pThis->logLevel=LOGLVL_WARNING;
	strncpy(pThis->sName, sProg, DASIO_NAME_SZ - 1);
	OobComment_init(&(pThis->cmt));
	das_store_str(&(pThis->cmt.sSrc), &(pThis->cmt.uSrcLen), pThis->sName);
	
	if(strchr(mode, 'r') != NULL){
		pThis->rw = 'r';
	}
	else{
		if(strchr(mode, 'w') != NULL){
			pThis->rw = 'w';
			if(strchr(mode, 'c') != NULL) pThis->compressed = true;
		}
		else{
			das_error(22, "Illegal argument for mode in new_DasIO_cfile");
			return NULL;
		}
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
	pThis->nSockFd = SSL_get_fd((SSL*)pSsl);
	pThis->pSsl = pSsl;
	pThis->taskSize= -1;  /* for progress indication */
	pThis->logLevel=LOGLVL_WARNING;
	strncpy(pThis->sName, sProg, DASIO_NAME_SZ - 1);
	OobComment_init(&(pThis->cmt));
	das_store_str(&(pThis->cmt.sSrc), &(pThis->cmt.uSrcLen), pThis->sName);
	
	if(strchr(mode, 'r') != NULL){
		pThis->rw = 'r';
	}
	else{
		if(strchr(mode, 'w') != NULL){
			pThis->rw = 'w';
			if(strchr(mode, 'c') != NULL) pThis->compressed = true;
		}
		else{
			das_error(22, "Illegal argument for mode in new_DasIO_cfile");
			return NULL;
		}
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
	pThis->sBuffer = sbuf;
	pThis->nLength = length;
	pThis->taskSize= -1;  /* for progress indication */
	pThis->logLevel=LOGLVL_WARNING;
	strncpy(pThis->sName, sProg, DASIO_NAME_SZ - 1);
	OobComment_init(&(pThis->cmt));
	das_store_str(&(pThis->cmt.sSrc), &(pThis->cmt.uSrcLen), pThis->sName);
   
	if(strchr(mode, 'r') != NULL){ 
		 pThis->rw = 'r';
	 }
	 else{
		if(strchr(mode, 'w') != NULL){
			pThis->rw = 'w';
			if(strchr(mode, 'c') != NULL) pThis->compressed = true;
		 }
		else{
			das_error(22, "Illegal argument for mode in new_DasIO_cfile");
			return NULL;
		}
	 }
	return pThis;
}

/* ************************************************************************** */
/* Socket Assistance */

size_t _DasIO_sockWrite(DasIO* pThis, const char* sBuf, size_t uLen)
{
/* Have to handle the fact that blocking SIGPIPE is different on Apple */
#if defined(__APPLE__) || defined(_WIN32)
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
		das_error(22, "not implemented\n" ); abort();
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
	
	pThis->offset+= nRead;
	return nRead;
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
		return -1 * das_error(20, "Max number of processors exceeded");
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

#define PKTTYPE_DESC  1
#define PKTTYPE_DATA  2
#define PKTTYPE_OOB   3

int _DasIO_dataTypeOrErr(DasIO* pThis, DasBuf* pBuf, bool bFirstRead, int* pPktId)
{
	int nRet;
	char sTag[5] = {'\0'};
	
	int i = DasIO_read(pThis, pBuf, 4);
	if((bFirstRead)&&(i < 3))
		return das_error(22, "Input stream %s contains no packets.", pThis->sName);
	
	if(i == 0) return 0;  /* Normal end, except on first read */
		
	if(i < 3){
		/* Abnormal end */
		fprintf(stderr, "Partial packet in stream %s.", pThis->sName);
		return -22;
	}
	DasBuf_read(pBuf, sTag, 4);
	
	int nTagType = 0;
	if( sTag[0]=='[' && sTag[3]==']' ){
		if(tolower(sTag[1]) == 'x' && tolower(sTag[2]) == 'x')
			nTagType = PKTTYPE_OOB;
		else{
			if(isdigit(sTag[1]) && isdigit(sTag[2]))
				nTagType = PKTTYPE_DESC;
		}
	}
	else{
		if(sTag[0]==':' && sTag[3]==':'){
			nTagType = PKTTYPE_DATA;
		}
	}
		
	if(bFirstRead && nTagType != 1){
		nRet = das_error(22, 
			"Input is not a valid Das2 stream. Valid streams start with [00], the "
			"input started with: %02X %02X %02X %02X (%c%c%c%c)\n", 
			(unsigned int)sTag[0], (unsigned int)sTag[1], (unsigned int)sTag[2],
			(unsigned int)sTag[3], sTag[0], sTag[1], sTag[2], sTag[3]
		);
		return -1 * nRet;
	}
		
	if(nTagType == 0)
		return -1 * das_error(22, "Garbled Packet Tag \"%s\" at input offset "
				                 "0x%08X", sTag, pThis->offset);
		
	if(nTagType == 1||nTagType == 2){
		sTag[3] = '\0';
		sscanf( sTag+1, "%d", pPktId );
	}
	
	return nTagType;
}

int _DasIO_sizeOrErr(
	DasIO* pThis, DasBuf* pBuf, int nPktType, StreamDesc* pSd, int nPktId
){
	int nPktSz; 
	char sLen[7] = {'\0'};
	
	/* These packets have lengths... */
	if(nPktType == PKTTYPE_DESC || nPktType == PKTTYPE_OOB){
		if(DasIO_read(pThis, pBuf, 6) != 6){ 
			return -1 * das_error(22, "Input stream ends in a partial packet");
		}
		
		DasBuf_read(pBuf, sLen, 6); /* Advances the read point */
		
		if(sscanf(sLen, "%d", &nPktSz) != 1){
			return -1 * das_error(22, "Can't get packet size from bytes %s", sLen);
		}
		return nPktSz;
	}
	
	/* ...Data packets don't */
	if(pSd == NULL)
		return -1 * das_error(22, "Data packets received before stream header");
	
	if(pSd->pktDesc[nPktId] == NULL)
		return -1 * das_error(22, "Packet type %02d data received before packet "
						            "type %02d header", nPktId, nPktId);
	return PktDesc_recBytes( pSd->pktDesc[nPktId] );
}

DasErrCode _DasIO_handleDesc(
	DasIO* pThis, DasBuf* pBuf, StreamDesc** ppSd, int nPktId
){
	DasDesc* pDesc = NULL;
	StreamDesc* pSd = *ppSd;
	StreamHandler* pHndlr = NULL;
	DasErrCode nRet = 0;
	
	if( (pDesc = Das2Desc_decode(pBuf)) == NULL) return 17;
	
	if(pDesc->type == STREAM){
		if(*ppSd != NULL)
			return das_error(22, "Multiple Stream descriptors in input");

		*ppSd = (StreamDesc*)pDesc;
		pSd = *ppSd;
		if(strcmp("deflate", pSd->compression)==0)
			_DasIO_enterDecompressMode(pThis);
	}
	else{
		if(pDesc->type == PACKET){
			if(pSd == NULL)
				return das_error(22, "Streams must be defined before packets can be "
						"defined");
		
			/* Handle packet redefinitions. */
			if(pSd->pktDesc[nPktId] != NULL){
				
				/* Let any stream processors know that this packet desc is about
				 * to be deleted so that they can do stuff with the old one 1st */
				for(size_t u = 0; pThis->pProcs[u] != NULL; u++){
					pHndlr = pThis->pProcs[u];
					if(pHndlr->pktRedefHandler != NULL)
						nRet = pHndlr->pktRedefHandler(pSd, pSd->pktDesc[nPktId], 
								                         pHndlr->userData);
				
					if(nRet != 0) break;
				}
				
				StreamDesc_freePktDesc(pSd, nPktId);
			}
			
			if((nRet = StreamDesc_addPktDesc(pSd, (PktDesc*)pDesc, nPktId)) != 0)
				return nRet;
		}
		else{
			return das_error(22, "Only Stream and Packet descriptors expected");
		}
	}
			
	/* Call the stream handlers */
	for(size_t u = 0; pThis->pProcs[u] != NULL; u++){
		pHndlr = pThis->pProcs[u];
		if(pDesc->type == STREAM){
			if(pHndlr->streamDescHandler != NULL)
				nRet = pHndlr->streamDescHandler(pSd, pHndlr->userData);
		}
		else{
			if(pHndlr->pktDescHandler != NULL)
				nRet = pHndlr->pktDescHandler(pSd, pSd->pktDesc[nPktId], pHndlr->userData);
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
	
	nRet = PktDesc_decodeData(pSd->pktDesc[nPktId], pBuf);
	if(nRet != 0) return nRet;
			
	for(size_t u = 0; pThis->pProcs[u] != NULL; u++){
		pHndlr = pThis->pProcs[u];
		if(pHndlr->pktDataHandler != NULL)
			nRet = pHndlr->pktDataHandler(pSd->pktDesc[nPktId], pHndlr->userData);
		if(nRet != 0) break;
	}
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
	
	int nPktType, nPktId, nBytes;
	
	DasBuf* pBuf = pThis->pDb;
	bool bFirstRead = true;
	
	if(pThis->rw == 'w'){
		return das_error(22, "Can't read input, this is an output stream");
	}

	/* Loop over all the input packets, make sure to break out of the loop
	 * instead of just returning so that the stream close handlers can be called. 
	 */
	while(nRet == 0){
		DasBuf_reinit(pBuf);
		
		/* What Kind of Packet do we have? */
		nPktId = -1;
		if((nPktType = _DasIO_dataTypeOrErr(pThis, pBuf, bFirstRead, &nPktId)) < 1){
			nRet = -1 * nPktType;
			break;
		}
		bFirstRead = false;
		
		/* Get the number of bytes to read next */
		nBytes = _DasIO_sizeOrErr(pThis, pBuf, nPktType, pSd, nPktId);
		if(nBytes < 0){ 
			nRet = -1 * nBytes;
			break;
		}
		if(nBytes == 0){
			/* Wow, a null packet, let's call those illegal */
			nRet = das_error(22, "0-length input packet.");
			break;
		}
		
		/* Read the bytes */
		if(nBytes > pBuf->uLen){
			nRet = das_error(22, "Packet's length is %d, library buffer is only"
					            "%zu bytes long", nBytes, pThis->pDb->uLen);
			break;
		}
		
		if( DasIO_read(pThis, pBuf, nBytes) != nBytes){
			nRet = das_error(22, "Partial packet on input at offset %ld", pThis->offset);
			break;
		}
		
		/* Decode the packets, calling handlers as we go */
		switch(nPktType){
		case PKTTYPE_DESC:
			nRet = _DasIO_handleDesc(pThis, pBuf, &pSd, nPktId); break;
		case PKTTYPE_DATA:
			nRet = _DasIO_handleData(pThis, pBuf, pSd, nPktId);  break;
		case PKTTYPE_OOB:
			nRet = _DasIO_handleOOB(pThis, pBuf, oobs); break;
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
        return das_error(20, "setTaskSize must be called before the stream "
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
		return das_error(22, "Can't write, this is an output stream.");
	if(pThis->bSentHeader) 
		return das_error(22, "Can't double send a Das2 Stream Header");
	
	if(!DasDesc_has(&(pSd->base), "sourceId"))
		DasDesc_setStr(&(pSd->base), "sourceId", pThis->sName);
	
	DasBuf* pBuf = pThis->pDb;
	DasBuf_reinit(pBuf);
	
	int nRet;

	if( (nRet = StreamDesc_encode(pSd, pBuf)) != 0) return nRet;
	DasIO_printf(pThis, "[00]%06zu%s", DasBuf_written(pBuf), pBuf->sBuf);
	
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
		return das_error(22, "Can't write, this is an output stream.");
	if(! pThis->bSentHeader) 
		return das_error(22, "Send the stream descriptor first");
	
	int nRet = 0;
	DasBuf* pBuf = pThis->pDb;
	DasBuf_reinit(pBuf);
	
	if( (nRet = PktDesc_encode(pPd, pBuf)) != 0) return nRet;
	size_t uToWrite = DasBuf_unread(pBuf) + 10;
	if( DasIO_printf(pThis, "[%02d]%06d%s", pPd->id, DasBuf_unread(pBuf), 
	                 pBuf->pReadBeg) != uToWrite)
		return das_error(22, "Partial packet descriptor written");
	 
	pPd->bSentHdr = true;
	return DAS_OKAY;
}

int DasIO_writePktData(DasIO* pThis, PktDesc* pPdOut ) {
	
	if(pThis->rw == 'r')
		return das_error(22, "Can't write, this is an output stream.");
	if(! pThis->bSentHeader) 
		return das_error(22, "Send the stream descriptor first");
	if(! pPdOut->bSentHdr)
		return das_error(22, "Send packet header ID %02d first", pPdOut->id);
	
	int nRet = 0;
	DasBuf* pBuf = pThis->pDb;
	DasBuf_reinit(pBuf);
	
	if( (nRet = PktDesc_encodeData(pPdOut, pBuf)) != 0) return nRet;
	DasIO_printf(pThis, ":%02d:", pPdOut->id);
	DasIO_write(pThis, pBuf->pReadBeg, DasBuf_unread(pBuf));
	
	return 0;
}

DasErrCode DasIO_writeException(DasIO* pThis, OobExcept* pSe)
{
	if(pThis->rw == 'r')
		return das_error(22, "Can't write, this is an output stream.");
	
   if( !pThis->bSentHeader ) {
		return das_error(20, "streamDescriptor not sent before steamComment!\n");
	}
	DasErrCode nRet = 0;
	DasBuf_reinit(pThis->pDb);  /* Write zeros up to the previous data point */
	if((nRet = OobExcept_encode(pSe, pThis->pDb)) != 0) return nRet;
	
	int nWrote = DasIO_printf(pThis, "[xx]%06zu", DasBuf_written(pThis->pDb)); 
	nWrote += DasIO_write(pThis, pThis->pDb->pReadBeg, DasBuf_written(pThis->pDb));
	if(nWrote > 10) return 0;
	
	return das_error(22, "Error writing stream comment");
}

DasErrCode DasIO_writeComment(DasIO* pThis, OobComment* pSc) 
{ 
	if(pThis->rw == 'r')
		return das_error(22, "Can't write, this is an output stream.");
	
	if( !pThis->bSentHeader ) {
		return das_error(20, "streamDescriptor not sent before steamComment!\n");
	}
	DasErrCode nRet = 0;
	DasBuf_reinit(pThis->pDb);  /* Write zeros up to the previous data point */
	if((nRet = OobComment_encode(pSc, pThis->pDb)) != 0) return nRet;
	
	int nWrote = DasIO_printf(pThis, "[xx]%06zu", DasBuf_written(pThis->pDb));
	nWrote += DasIO_write(pThis, pThis->pDb->pReadBeg, DasBuf_written(pThis->pDb));
	if(nWrote > 10) return 0;
	
	return das_error(22, "Error writing stream comment");
}

/* ************************************************************************* */
/* Exit with message or exception */

void DasIO_throwException(
	DasIO* pThis, StreamDesc* pSd, const char* type, char* message
){
	if(pThis->rw == 'r'){
		int nErr = das_error(22, "DasIO_throwException: Can't write, this is an "
		                  "output stream.");
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
		           "an output stream.");
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
