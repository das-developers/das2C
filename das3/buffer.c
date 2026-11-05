/* Copyright (C) 2015-2017 Chris Piker <chris-piker@uiowa.edu>
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

#include <stdarg.h>
#include <string.h>
#include <stdio.h>
#include <sys/types.h>

#include "buffer_p.h"
#include "util.h"
#include "ctype.h"

DasBuf* new_DasBuf(size_t uLen)
{
	DasBuf* pThis = (DasBuf*)calloc(1, sizeof(DasBuf));
	if(pThis == NULL){
		das_error(DASERR_BUF, "Error allocating a buffer object");
		return NULL;
	}
	pThis->sBuf = (char*)calloc(uLen, sizeof(char));
	if(pThis->sBuf == NULL){
		free(pThis);
		das_error(DASERR_BUF, "Error allocating a %zu byte buffer", uLen);
		return NULL;
	}
	pThis->uLen = uLen;
	pThis->pWrite = pThis->sBuf;
	pThis->pReadBeg = pThis->sBuf;
	pThis->pReadEnd = pThis->pReadBeg;
	pThis->bOwned = true;
	return pThis;
}

/* Takes a struct in any state, including uninitialized stack memory, so
   every field is assigned and none is read.  pData is never written here,
   which is what lets a const array be wrapped "r". */
DasErrCode _DasBuf_initWrap(DasBuf* pThis, void* pData, size_t uLen, const char* sMode)
{
	if((sMode == NULL)||(sMode[1] != '\0')||((sMode[0] != 'r')&&(sMode[0] != 'w')))
		return das_error(DASERR_BUF, "Buffer mode must be \"r\" or \"w\", not \"%s\"",
		                 sMode ? sMode : "(null)");
	if((pData == NULL)&&(uLen > 0))
		return das_error(DASERR_BUF, "No memory given for a %zu byte buffer", uLen);

	pThis->sBuf = (char*)pData;
	pThis->uLen = uLen;
	pThis->uWrap = 0;
	pThis->bOwned = false;
	pThis->pReadBeg = pThis->sBuf;
	if(sMode[0] == 'r'){
		pThis->pWrite = NULL;    /* NULL write point is what marks a buffer read-only */
		pThis->pReadEnd = pThis->sBuf + uLen;
	}
	else{
		pThis->pWrite = pThis->sBuf;
		pThis->pReadEnd = pThis->sBuf;
	}
	return DAS_OKAY;
}

DasBuf* new_DasBuf_wrap(void* pData, size_t uLen, const char* sMode)
{
	DasBuf* pThis = (DasBuf*)calloc(1, sizeof(DasBuf));
	if(pThis == NULL){
		das_error(DASERR_BUF, "Error allocating a buffer object");
		return NULL;
	}
	if(_DasBuf_initWrap(pThis, pData, uLen, sMode) != DAS_OKAY){
		free(pThis);
		return NULL;
	}
	return pThis;
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

DasErrCode DasBuf_zero(DasBuf* pThis)
{
	if(pThis->pWrite == NULL)
		return das_error(DASERR_BUF, "Attempted to zero a read only buffer");
	if(pThis->uLen > 0) memset(pThis->sBuf, 0, pThis->uLen);
	pThis->pWrite = pThis->sBuf;
	pThis->pReadBeg = pThis->sBuf;
	pThis->pReadEnd = pThis->sBuf;
	return DAS_OKAY;
}

void del_DasBuf(DasBuf* pThis)
{
	if(pThis == NULL) return;
	if(pThis->bOwned) free(pThis->sBuf);
	free(pThis);
}

size_t DasBuf_capacity(const DasBuf* pThis){ return pThis->uLen; }

size_t DasBuf_written(const DasBuf* pThis){
	if(pThis->pWrite == NULL) return 0;
	
	return pThis->pWrite - pThis->sBuf;
}

DasErrCode DasBuf_printf(DasBuf* pThis, const char* sFmt, ...)
{
	if(pThis->pWrite == NULL) 
		return das_error(DASERR_BUF, "Attempted write to a read only buffer");
	
	size_t uLeft = DasBuf_writeSpace(pThis);
	
	/* Don't let a size_t object roll-around*/
	if(uLeft == 0) 
		return das_error(DASERR_BUF, "%zu byte buffer full", pThis->uLen);
	
	va_list argp;
	va_start(argp, sFmt);
	int nRet = vsnprintf(pThis->pWrite, uLeft, sFmt, argp);
	va_end(argp);
	
	/* If an error occurred, don't increment anything, just bail */
	if(nRet < 0) 
		return das_error(DASERR_BUF, "Error in vsnprintf");
	
	if(nRet >= uLeft)
		return das_error(DASERR_BUF, "Couldn't write %d bytes to buffer", nRet);
	
	pThis->pWrite += nRet;
	pThis->pReadEnd = pThis->pWrite;
	return DAS_OKAY;
}

DasErrCode DasBuf_write(DasBuf* pThis, const void* pData, size_t uLen)
{
	if(pThis->pWrite == NULL) 
		return das_error(DASERR_BUF, "Attempted write to a read only buffer");
		
	if(uLen == 0) return 0;  /* successfully did nothing */
	size_t uLeft = DasBuf_writeSpace(pThis);
	if(uLeft < uLen)
		return das_error(DASERR_BUF, "Buffer has %zu bytes of space left, can't write "
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

ubyte* DasBuf_prepare(DasBuf* pThis, size_t* pSpace)
{
	*pSpace = DasBuf_writeSpace(pThis);  /* 0 for read-only, see there */
	if(*pSpace == 0) return NULL;
	return (ubyte*)pThis->pWrite;
}

DasErrCode DasBuf_commit(DasBuf* pThis, size_t uLen)
{
	if(uLen == 0) return DAS_OKAY;
	size_t uLeft = DasBuf_writeSpace(pThis);
	if(uLen > uLeft)
		return das_error(DASERR_BUF, "Commit of %zu bytes, only %zu were prepared",
		                 uLen, uLeft);
	pThis->pWrite += uLen;
	pThis->pReadEnd = pThis->pWrite;
	return DAS_OKAY;
}


/* code adapted from the old das CLI library, seemed general enough to
   include here.
   
   TODO: make it wrap based on number of characters not bytes, utf8.h
         maybe useful here.
 */
DasErrCode DasBuf_wrapWrite(
	DasBuf* pThis, int nWidth, const char* sIndent, const char* sTxt
){
	DasErrCode nRet;
	size_t uTmp = 0;
	if(sIndent) uTmp = strlen(sIndent);
		
	/* nWidth is signed, test it alone first so a negative one can't pass the
	   unsigned comparison */
	if((nWidth < 20) || ((size_t)nWidth < uTmp + 20))
		return das_error(DASERR_BUF, "Wrap width was %d, must be at least 20 + size of "
				            "indent string ", nWidth);
	
	/* Indents past 80 bytes are cut to fit, the first line uses the same cut */
	char sNewLine[80+2] = {'\0'};
	if(sIndent != NULL) snprintf(sNewLine, sizeof(sNewLine), "\n%s", sIndent);
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
			are otherwise skipped.  The unsigned casts keep utf-8 bytes out of
			isspace()'s undefined range. */
		
		while(isspace((unsigned char)*pBeg) && *pBeg != '\0'){ 
			if(*pBeg == '\n'){ 
				if((nRet = DasBuf_write(pThis, "\n", 1)) != DAS_OKAY) return nRet;
				nCol = 1;
			}
			++pBeg;
		}
		if(*pBeg == '\0') break;
		
		/* At a non-space character, find end of this word */
		pEnd = pBeg;
		while( !isspace((unsigned char)*pEnd) && *pEnd != '\0') pEnd++;
		nWord = pEnd - pBeg;
		
		
		/* handle preceding space */
		
		/* Do we need to indent first? */
		if(nCol == 1){
			if(nIndent > 0){
				if((nRet = DasBuf_write(pThis, sIndent, nIndent)) != DAS_OKAY) return nRet;
				nCol += nIndent;
			}
		}
		else{
			/* Do we need a line break? */
			if((nWord + nCol > nWidth)&&(nCol != (nIndent + 1))){
				if((nRet = DasBuf_write(pThis, sNewLine, strlen(sNewLine))) != DAS_OKAY)
					return nRet;
				nCol = nIndent + 1;
			}
			else{
				/* Nope, just a space character */
				if((nRet = DasBuf_write(pThis, " ", 1)) != DAS_OKAY) return nRet;
				nCol += 1;	
			}
		}
			
		/* Okay now really print it */
		if((nRet = DasBuf_write(pThis, pBeg, nWord)) != DAS_OKAY) return nRet;
		nCol += nWord;
		
		pBeg = pEnd;
	}
	return DAS_OKAY;
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
			return das_error(DASERR_BUF, "Attempt to set read point %zu for a %zu "
			                  "byte buffer", uPos, pThis->uLen);
	}
	else{
		if(uPos > pThis->pWrite - pThis->sBuf)
			return das_error(DASERR_BUF, "Attempt to set read point %zu but only %zu "
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

const ubyte* DasBuf_data(const DasBuf* pThis, size_t* pLen)
{
	/* Set on the empty path too, so a caller may test the length alone */
	*pLen = DasBuf_unread(pThis);
	if(*pLen == 0) return NULL;
	return (const ubyte*) pThis->pReadBeg;
}

DasErrCode DasBuf_consume(DasBuf* pThis, size_t uLen)
{
	size_t uLeft = DasBuf_unread(pThis);
	if(uLen > uLeft)
		return das_error(DASERR_BUF, "Consume of %zu bytes, only %zu are unread",
		                 uLen, uLeft);
	pThis->pReadBeg += uLen;
	return DAS_OKAY;
}

size_t DasBuf_writeSpace(const DasBuf* pThis)
{
	if(pThis->pWrite == NULL) return 0;  /* This is a read-only buffer */
	return pThis->uLen - (pThis->pWrite - pThis->sBuf);
}

size_t DasBuf_strip(DasBuf* pThis)
{
	/* pReadEnd is one past the data, for a read-only buffer one past the
	   array, so the trailing test looks at pReadEnd[-1].  The casts keep
	   utf-8 bytes out of isspace()'s undefined negative range. */
	while( pThis->pReadBeg < pThis->pReadEnd && isspace((unsigned char)*(pThis->pReadBeg)) )
		pThis->pReadBeg += 1;

	while( pThis->pReadEnd > pThis->pReadBeg && isspace((unsigned char)pThis->pReadEnd[-1]) )
		pThis->pReadEnd -= 1;
	
	return pThis->pReadEnd - pThis->pReadBeg;
}
