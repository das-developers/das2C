#define _POSIX_C_SOURCE 200112L

#include <stdarg.h>
#include <string.h>
#include <stdio.h>

#include "buffer.h"
#include "util.h"
#include "ctype.h"

DasBuf* new_DasBuf(size_t uLen)
{
	DasBuf* pThis = (DasBuf*)calloc(1, sizeof(DasBuf));
	pThis->sBuf = (char*)calloc(uLen, sizeof(char));
	if(pThis->sBuf == NULL){
		das2_error(12, "Error allocating a %d byte buffer", uLen);
		return NULL;
	}
	pThis->uLen = uLen;
	pThis->pWrite = pThis->sBuf;
	pThis->pReadBeg = pThis->sBuf;
	pThis->pReadEnd = pThis->pReadBeg;
	return pThis;
}

ErrorCode DasBuf_initReadOnly(DasBuf* pThis, const char* sExternal, size_t uLen)
{
	pThis->sBuf = (char*)sExternal;
	pThis->uLen = uLen;
	pThis->pReadBeg = pThis->sBuf;
	pThis->pReadEnd = pThis->pReadBeg;
	return 0;
}

ErrorCode DasBuf_initReadWrite(DasBuf* pThis, char* sBuf, size_t uLen)
{
	if(pThis->pWrite == NULL)
		return das2_error(12, "DasBuf_reinit: Attempt to re-initialize a read "
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

ErrorCode DasBuf_printf(DasBuf* pThis, const char* sFmt, ...)
{
	if(pThis->pWrite == NULL) 
		return das2_error(12, "Attempted write to a read only buffer");
	
	size_t uLeft = DasBuf_writeSpace(pThis);
	
	/* Don't let a size_t object roll-around*/
	if(uLeft == 0) 
		return das2_error(12, "%zu byte buffer full", pThis->uLen);
	
	va_list argp;
	va_start(argp, sFmt);
	int nRet = vsnprintf(pThis->pWrite, uLeft, sFmt, argp);
	va_end(argp);
	
	/* If an error occurred, don't increment anything, just bail */
	if(nRet < 0) return das2_error(12, "Error in vsnprintf");
	
	if(nRet >= uLeft)
		return das2_error(12, "Couldn't write %d bytes to buffer", nRet);
	
	pThis->pWrite += nRet;
	pThis->pReadEnd = pThis->pWrite;
	return 0;
}

ErrorCode DasBuf_write(DasBuf* pThis, const void* pData, size_t uLen)
{
	if(pThis->pWrite == NULL) 
		return das2_error(12, "Attempted write to a read only buffer");
		
	if(uLen == 0) return 0;  /* successfully did nothing */
	size_t uLeft = DasBuf_writeSpace(pThis);
	if(uLeft < uLen)
		return das2_error(12, "Buffer has %zu bytes of space left, can't write "
				                "%zu bytes.", uLeft, uLen);
	
	memcpy(pThis->pWrite, pData, uLen);
	pThis->pWrite += uLen;
	pThis->pReadEnd = pThis->pWrite;
	return 0;
}

/* version of write for null-terminated strings */
ErrorCode DasBuf_puts(DasBuf* pThis, const char* sStr){
	return DasBuf_write(pThis, sStr, strlen(sStr));
}

/* code adapted from the old das CLI library, seemed general enough to include
 here */
ErrorCode DasBuf_wrapWrite(
	DasBuf* pThis, int nWidth, const char* sIndent, const char* sTxt
){
	size_t uTmp = 0;
	if(sIndent) uTmp = strlen(sIndent);
		
	if(nWidth < uTmp + 20) 
		return das2_error(12, "Wrap width was %d, must be at least 20 + size of "
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

ErrorCode DasBuf_writeFrom(DasBuf* pThis, FILE* pIn, size_t uLen){
	if(pThis->pWrite == NULL) 
		return -1 * das2_error(12, "Attempted write to a read only buffer");
	
	if(uLen == 0) return 0; /* successfully did nothing */
	size_t uLeft = DasBuf_writeSpace(pThis);
	if(uLeft < uLen)
		return das2_error(12, "Buffer has %zu bytes of space left, can't write "
				                "%zu bytes.", uLeft, uLen);
	
	size_t uRead = fread(pThis->pWrite, sizeof(char), uLen, pIn);
	pThis->pWrite += uLen;
	pThis->pReadEnd = pThis->pWrite;
	
	if(uRead < uLen){
		if(feof(pIn)) return (int)uRead;
		if(ferror(pIn) ) return das2_error(12, "Error reading from input file");
	}
	return uRead;
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

size_t DasBuf_readOffset(const DasBuf* pThis){
	return pThis->pReadBeg - pThis->sBuf;
}

ErrorCode DasBuf_setReadOffset(DasBuf* pThis, size_t uPos)
{
	if(pThis->pWrite == NULL){
		if(uPos > pThis->uLen)
			return das2_error(12, "Attempt to set read point %zu for a %zu "
			                  "byte buffer", uPos, pThis->uLen);
	}
	else{
		if(uPos > pThis->pWrite - pThis->sBuf)
			return das2_error(12, "Attempt to set read point %zu but only %zu "
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
