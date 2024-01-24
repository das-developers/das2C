/* Copyright (C) 2015-2017 Chris Piker <chris-piker@uiowa.edu>
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
#include <stdarg.h>
#include <string.h>
#include <stdio.h>
#include <sys/types.h>

#ifndef _WIN32
#include <sys/socket.h>
#else
#include <winsock2.h>
#endif

#include <openssl/ssl.h>

#include "buffer.h"
#include "util.h"
#include "ctype.h"
#include "http.h"

DasBuf* new_DasBuf(size_t uLen)
{
	DasBuf* pThis = (DasBuf*)calloc(1, sizeof(DasBuf));
	pThis->sBuf = (char*)calloc(uLen, sizeof(char));
	if(pThis->sBuf == NULL){
		das_error(12, "Error allocating a %d byte buffer", uLen);
		return NULL;
	}
	pThis->uLen = uLen;
	pThis->pWrite = pThis->sBuf;
	pThis->pReadBeg = pThis->sBuf;
	pThis->pReadEnd = pThis->pReadBeg;
	return pThis;
}

DasErrCode DasBuf_initReadOnly(DasBuf* pThis, const char* sExternal, size_t uLen)
{
	pThis->sBuf = (char*)sExternal;
	pThis->uLen = uLen;
	pThis->pReadBeg = pThis->sBuf;
	pThis->pReadEnd = pThis->pReadBeg;
	return 0;
}

DasErrCode DasBuf_initReadWrite(DasBuf* pThis, char* sBuf, size_t uLen)
{
	if(pThis->pWrite == NULL)
		return das_error(12, "DasBuf_reinit: Attempt to re-initialize a read "
		                  "only buffer");
	
	memset(pThis->sBuf, 0 , uLen);
	pThis->pWrite = pThis->sBuf;
	pThis->pReadBeg = pThis->sBuf;
	pThis->pReadEnd = pThis->pReadBeg;
	return 0;
}

void DasBuf_reinit(DasBuf* pThis)
{
	if(pThis->pWrite != NULL){
		if(pThis->pWrite > pThis->sBuf)
			memset(pThis->sBuf, 0, DasBuf_written(pThis));
		
		pThis->pWrite = pThis->sBuf;
		
		/* For a read-write buffer end read point is after all valid data of
		   which there is none. */
		pThis->pReadBeg = pThis->sBuf;
		pThis->pReadEnd = pThis->pWrite;
	}
	else{
		/* For a read-only buffer, end point is just end of buffer */
		pThis->pReadBeg = pThis->sBuf;
		pThis->pReadEnd = pThis->sBuf + pThis->uLen;
	}
}

void del_DasBuf(DasBuf* pThis)
{
	/* If write point is NULL we are a read-only buffer */
	if(pThis->pWrite != NULL) free(pThis->sBuf);
	free(pThis);
}

size_t DasBuf_written(const DasBuf* pThis){
	if(pThis->pWrite == NULL) return 0;
	
	return pThis->pWrite - pThis->sBuf;
}

DasErrCode DasBuf_printf(DasBuf* pThis, const char* sFmt, ...)
{
	if(pThis->pWrite == NULL) 
		return das_error(12, "Attempted write to a read only buffer");
	
	size_t uLeft = DasBuf_writeSpace(pThis);
	
	/* Don't let a size_t object roll-around*/
	if(uLeft == 0) 
		return das_error(12, "%zu byte buffer full", pThis->uLen);
	
	va_list argp;
	va_start(argp, sFmt);
	int nRet = vsnprintf(pThis->pWrite, uLeft, sFmt, argp);
	va_end(argp);
	
	/* If an error occurred, don't increment anything, just bail */
	if(nRet < 0) return das_error(12, "Error in vsnprintf");
	
	if(nRet >= uLeft)
		return das_error(12, "Couldn't write %d bytes to buffer", nRet);
	
	pThis->pWrite += nRet;
	pThis->pReadEnd = pThis->pWrite;
	return 0;
}

DasErrCode DasBuf_write(DasBuf* pThis, const void* pData, size_t uLen)
{
	if(pThis->pWrite == NULL) 
		return das_error(12, "Attempted write to a read only buffer");
		
	if(uLen == 0) return 0;  /* successfully did nothing */
	size_t uLeft = DasBuf_writeSpace(pThis);
	if(uLeft < uLen)
		return das_error(12, "Buffer has %zu bytes of space left, can't write "
				                "%zu bytes.", uLeft, uLen);
	
	memcpy(pThis->pWrite, pData, uLen);
	pThis->pWrite += uLen;
	pThis->pReadEnd = pThis->pWrite;
	return 0;
}

/* version of write for null-terminated strings */
DasErrCode DasBuf_puts(DasBuf* pThis, const char* sStr){
	return DasBuf_write(pThis, sStr, strlen(sStr));
}


/* code adapted from the old das CLI library, seemed general enough to include
 here */
DasErrCode DasBuf_wrapWrite(
	DasBuf* pThis, int nWidth, const char* sIndent, const char* sTxt
){
	size_t uTmp = 0;
	if(sIndent) uTmp = strlen(sIndent);
		
	if(nWidth < uTmp + 20) 
		return das_error(12, "Wrap width was %d, must be at least 20 + size of "
				            "indent string ", nWidth);
	
	
	char sNewLine[80+2] = {'\0'};
	if(sIndent != NULL) snprintf(sNewLine, nWidth+1, "\n%s", sIndent);	
	else sNewLine[0] = '\n';
	
	int nIndent = strlen(sNewLine) - 1;
	
	/* Word loop */
	int nCol = 1;  /* nCol is a 1 based offset, not 0 based */
	int nWord = 0;
	const char* pBeg = sTxt;
	const char* pEnd = NULL;
	while(*pBeg != '\0'){
		
		/* There may be whitespace to skip, advance to beginning of next word.  
		   newline characters are always emitted as soon as they are seen but
			are otherwise skipped. */
		
		while(isspace(*pBeg) && *pBeg != '\0'){ 
			if(*pBeg == '\n'){ 
				DasBuf_write(pThis, "\n", 1);
				nCol = 1;
			}
			++pBeg;
		}
		if(*pBeg == '\0') break;
		
		/* At a non-space character, find end of this word */
		pEnd = pBeg;
		while( !isspace(*pEnd) && *pEnd != '\0') pEnd++;
		nWord = pEnd - pBeg;
		
		
		/* handle preceeding space */
		
		/* Do we need to indent first? */
		if(nCol == 1){
			if(nIndent > 0){
				DasBuf_write(pThis, sIndent, nIndent);
				nCol += nIndent;
			}
		}
		else{
			/* Do we need a line break? */
			if((nWord + nCol > nWidth)&&(nCol != (nIndent + 1))){
				DasBuf_write(pThis, sNewLine, strlen(sNewLine));
				nCol = nIndent + 1;
			}
			else{
				/* Nope, just a space character */
				DasBuf_write(pThis, " ", 1);
				nCol += 1;	
			}
		}
			
		/* Okay now really print it */
		DasBuf_write(pThis, pBeg, nWord);
		nCol += nWord;
		
		pBeg = pEnd;
	}
	return 0;
}

int DasBuf_writeFrom(DasBuf* pThis, FILE* pIn, size_t uLen){
	if(pThis->pWrite == NULL) 
		return -1 * das_error(DASERR_BUF, "Attempted write to a read only buffer");
	
	if(uLen == 0) return 0; /* successfully did nothing */
	size_t uLeft = DasBuf_writeSpace(pThis);
	if(uLeft < uLen)
		return -1 * das_error(DASERR_BUF, "Buffer has %zu bytes of space left, "
				            "can't write %zu bytes.", uLeft, uLen);
	
	int nRead = fread(pThis->pWrite, sizeof(char), uLen, pIn);
	pThis->pWrite += uLen;
	pThis->pReadEnd = pThis->pWrite;
	
	if(nRead < uLen){
		if(feof(pIn)) return (int)nRead;
		if(ferror(pIn) ) 
			return -1 * das_error(DASERR_BUF, "Error reading from input file");
	}
	return nRead;
}

int DasBuf_writeFromSock(DasBuf* pThis, int nFd, size_t uLen)
{
	if(pThis->pWrite == NULL) 
		return -1 * das_error(DASERR_BUF, "Attempted write to a read only buffer");
	
	if(uLen == 0) return 0; /* successfully did nothing */
	size_t uLeft = DasBuf_writeSpace(pThis);
	if(uLeft < uLen)
		return -1 * das_error(DASERR_BUF, "Buffer has %zu bytes of space left, "
				            "can't write %zu bytes.", uLeft, uLen);
	
	errno = 0;
	int nChunk = 0, nRead = 0;
	while(nRead < uLen){
		errno = 0;
		nChunk = recv(nFd, pThis->pWrite, uLen - nRead, 0);
		
		if(nChunk == 0) break;  /* Socket is done */
		
		if(nChunk < 0){
			return -1 * das_error(DASERR_BUF, "Error reading from socket, %s", 
					                  strerror(errno));
		}
		nRead += nChunk;
		pThis->pWrite += nChunk;
		pThis->pReadEnd = pThis->pWrite;
	}
	return nRead;
	
}

int DasBuf_writeFromSSL(DasBuf* pThis, void* pvSsl, size_t uLen)
{
	if(pThis->pWrite == NULL) 
		return -1 * das_error(DASERR_BUF, "Attempted write to a read only buffer");
	
	if(uLen == 0) return 0; /* successfully did nothing */
	size_t uLeft = DasBuf_writeSpace(pThis);
	if(uLeft < uLen)
		return -1 * das_error(DASERR_BUF, "Buffer has %zu bytes of space left, "
				            "can't write %zu bytes.", uLeft, uLen);
	int nErr = 0;
	errno = 0;
	int nChunk = 0, nRead = 0;
	while(nRead < uLen){
		errno = 0;
		
		nChunk = SSL_read((SSL*)pvSsl, pThis->pWrite, uLen - nRead);
		
		if(nChunk == 0) break;  /* Socket is done */
		
		if(nChunk < 1){
			char* sErr = das_ssl_getErr(pvSsl, nChunk);
			nErr = das_error(DASERR_BUF, "Error reading from SSL socket, %s",sErr);
			free(sErr);
			return -1 * nErr;
		}
		
		nRead += nChunk;
		pThis->pWrite += nChunk;
		pThis->pReadEnd = pThis->pWrite;
	}
	return nRead;
}
	
size_t DasBuf_read(DasBuf* pThis, char* pOut, size_t uOut)
{
	size_t uRead = 0;
	while( pThis->pReadBeg < pThis->pReadEnd &&  uRead < uOut){
		*pOut = *pThis->pReadBeg;
		pOut++;
		pThis->pReadBeg++;
		uRead++;
	}
	return uRead;
}

size_t DasBuf_peek(const DasBuf* pThis, char* pOut, size_t uOut)
{
	size_t uRead = 0;
	while((pThis->pReadBeg + uRead) < pThis->pReadEnd && uRead < uOut){
		*pOut = *(pThis->pReadBeg + uRead);
		++pOut;
		++uRead;
	}
	return uRead;
}

// Output the last character in the buffer, or 
int DasBuf_last(const DasBuf* pThis){
	if(pThis->pReadEnd > pThis->pReadBeg)
		return *(pThis->pReadEnd - 1);
	else
		return -1;
}

const char* DasBuf_readRec(
	DasBuf* pThis, const char* sDelim, size_t uDelimLen, size_t* pLen
){
	size_t uLeft = DasBuf_unread(pThis);
	if(uLeft < 1) return NULL;
	
	const char* pStart = pThis->pReadBeg;
	const char* pEnd = pThis->pReadBeg;
	size_t u = 0;
	
	/* Count how many bytes you have to advance to hit the read point, don't
	 * use strstr because it will stop at embedded nulls */
	bool bMatch = false;
	while(pEnd < pThis->pReadEnd ){
		if(*pEnd == *sDelim){
			/* Do we have enough room for the whole delimiter ? */
			if((pThis->pReadEnd - pEnd) < uDelimLen) return NULL;
			
			/* Can we match all bytes in the delimiter? */
			bMatch = true;
			for(u = 1; u < uDelimLen; ++u){
				if(pEnd[u] != sDelim[u]){bMatch = false; break;}
			}
			
			/* Okay looks good, we're done, advance read point past delimiter */
			if(bMatch){
				*pLen = (pEnd - pStart) + uDelimLen;
				pThis->pReadBeg += *pLen;
				return pStart;
			}
		}
		++pEnd;
	}
	
	return NULL; /* Don't advance read pt, don't set pLen, don't collect $200 */
}

size_t DasBuf_readOffset(const DasBuf* pThis){
	return pThis->pReadBeg - pThis->sBuf;
}

DasErrCode DasBuf_setReadOffset(DasBuf* pThis, size_t uPos)
{
	if(pThis->pWrite == NULL){
		if(uPos > pThis->uLen)
			return das_error(12, "Attempt to set read point %zu for a %zu "
			                  "byte buffer", uPos, pThis->uLen);
	}
	else{
		if(uPos > pThis->pWrite - pThis->sBuf)
			return das_error(12, "Attempt to set read point %zu but only %zu "
			                  "bytes are in the buffer", uPos, 
			                  pThis->pWrite - pThis->sBuf);
	}
	pThis->pReadBeg = pThis->sBuf + uPos;
	return 0;
}

size_t DasBuf_unread(const DasBuf* pThis){
	if(pThis->pReadBeg >= pThis->pReadEnd)
		return 0;
	else
		return pThis->pReadEnd - pThis->pReadBeg;
}

size_t DasBuf_writeSpace(const DasBuf* pThis)
{
	if(pThis->pWrite == NULL) return 0;  /* This is a read-only buffer */
	return pThis->uLen - (pThis->pWrite - pThis->sBuf);
}

size_t DasBuf_strip(DasBuf* pThis)
{
	while( pThis->pReadBeg < pThis->pReadEnd && isspace(*(pThis->pReadBeg)) ) 
		pThis->pReadBeg += 1;
	
	while( pThis->pReadEnd > pThis->pReadBeg && isspace(*(pThis->pReadEnd)) )
		pThis->pReadEnd -= 1;
	
	return pThis->pReadEnd - pThis->pReadBeg;
}
