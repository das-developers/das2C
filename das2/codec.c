/* Copyright (C) 2024 Chris Piker <chris-piker@uiowa.edu>
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

#define _POSIX_C_SOURCE 200112L

#include <assert.h>
#include <string.h>
#include <ctype.h>
#include <limits.h>

#include "value.h"
#include "log.h"
#include "iterator.h"

#define _das_codec_c_
#include "codec.h"
#undef _das_codec_c_


/* Standard separators for ragged binary real value encoding

   These all evaluate to NaN if read as a float (or double) but they are not
   the standard NaN encoding used by most libc implimentations, so they can
   be used as sentinals even when normal NaN's are embedded in the stream.

   Furthermore they are palindromes, so it doesn't matter if they are read
   big-endian or little endian.  

   Lastly, the middle two bytes provide the separator number under the
   operation " *(pVal + (nItemSz / 2)) & 0x0F "
*/
const ubyte DAS_FLOAT_SEP[DASIDX_MAX][4] = {
   {0x7f, 0x80, 0x80, 0x7f},
   {0x7f, 0x81, 0x81, 0x7f},
   {0x7f, 0x82, 0x82, 0x7f},
   {0x7f, 0x83, 0x83, 0x7f},
   {0x7f, 0x84, 0x84, 0x7f},
   {0x7f, 0x85, 0x85, 0x7f},
   {0x7f, 0x86, 0x86, 0x7f},
   {0x7f, 0x87, 0x87, 0x7f}
};

const ubyte DAS_DOUBLE_SEP[DASIDX_MAX][8] = {
   {0x7f, 0xf8, 0x00, 0x00, 0x00, 0x00, 0xf8, 0x7f},
   {0x7f, 0xf8, 0x00, 0x81, 0x81, 0x00, 0xf8, 0x7f},
   {0x7f, 0xf8, 0x00, 0x82, 0x82, 0x00, 0xf8, 0x7f},
   {0x7f, 0xf8, 0x00, 0x83, 0x83, 0x00, 0xf8, 0x7f},
   {0x7f, 0xf8, 0x00, 0x84, 0x84, 0x00, 0xf8, 0x7f},
   {0x7f, 0xf8, 0x00, 0x85, 0x85, 0x00, 0xf8, 0x7f},
   {0x7f, 0xf8, 0x00, 0x86, 0x86, 0x00, 0xf8, 0x7f},
   {0x7f, 0xf8, 0x00, 0x87, 0x87, 0x00, 0xf8, 0x7f},
};


/* Operations flags */
/* (hdr) DASENC_VALID    0x0001 / * If not 1, not a valid encoder */
#define DASENC_SWAP      0x0002 /* If set bytes must be swapped prior to IO */

#define DASENC_CAST_UP   0x0004 /* On read save in larger integral size */
                                /* on write save in smaller integral size */

#define DASENC_TEXT      0x0008 /* Input is text */
#define DASENC_PARSE     0x0010 /* Input is text that should be parsed to a value */

#define DASENC_VARSZ     0x0020 /* Input is varible size text items */

#define DASENC_CAST_DOWN 0x0040 /* On read, halt with error */
                                /* on write save in larger integral size */

#define DASENC_READER    0x0080 /* If set I'm buffer -> array, if not I'm array -> buffer */

/* Used in the big switch, ignores the valid bit since that's assumed by then */
#define DASENC_MAJ_MASK  0x00FE /* Everyting concerned with the input buffer */

/* Items used after the big switch */

#define DASENC_NULLTERM  0x0200 /* Input is text that should be null terminated */
#define DASENC_WRAP      0x0400 /* Wrap last dim reading a string value */

#define DASENC_EAT_SPACE 0x0800 /* Eat extra whitespace when reading data */

void DasCodec_eatSpace(DasCodec* pThis, bool bEat){
	if(bEat) 
		pThis->uProc |= DASENC_EAT_SPACE;
	else
		pThis->uProc = pThis->uProc & (~DASENC_EAT_SPACE);
}

#define ENCODER_SETUP_ERROR "Logic error in encoder setup"

/* Change the external format info for a codec, very useful for das3_text! */
DasErrCode DasCodec_update(
	bool bRead, DasCodec* pThis, const char* sEncType, int16_t nSzEach,
	ubyte cSep, das_units epoch, const char* sOutFmt
){
	/* Can't just point to existing items, memset is going to erase them! */
	DasAry* _pAry = pThis->pAry;               /* okay, is external */
	char _sSemantic[DASENC_SEM_LEN] = {'\0'};
	strncpy(_sSemantic, pThis->sSemantic, DASENC_SEM_LEN); 

	char _sEncType[DASENC_TYPE_LEN] = {'\0'};
	strncpy(_sEncType, (sEncType != NULL) ? sEncType : pThis->sEncType, DASENC_TYPE_LEN - 1);

	int _nSzEach     = (nSzEach != 0)  ? nSzEach : pThis->nBufValSz;
	char _cSep       = (cSep != '\0')  ? cSep    : pThis->sSepSet[0];
	das_units _epoch = (epoch != NULL) ? epoch   : pThis->timeUnits;

	char _sOutFmt[DASENC_FMT_LEN] =   {'\0'};
	strncpy(_sOutFmt, (sOutFmt != NULL)  ?  sOutFmt  : pThis->sOutFmt,  DASENC_TYPE_LEN - 1);

	return DasCodec_init(
		bRead, pThis, _pAry, _sSemantic, _sEncType, _nSzEach, _cSep, _epoch, _sOutFmt
	);
}

/* Perform various checks to see if this is even possible */ 
DasErrCode DasCodec_init(
   bool bRead, DasCodec* pThis, DasAry* pAry, const char* sSemantic, 
   const char* sEncType, int16_t nSzEach, ubyte cSep, das_units epoch, 
   const char* sOutFmt
){
	memset(pThis, 0, sizeof(DasCodec));  
	pThis->sSepSet[0] = cSep; pThis->nSep = 1;
	pThis->pAry = pAry;
	pThis->nBufValSz = nSzEach;
	assert(pAry != NULL);

	if(bRead) pThis->uProc |= DASENC_READER;

	/* Just save it off.  Equivalent information exists in the vtBuf value */
	strncpy(pThis->sEncType, sEncType, DASENC_TYPE_LEN-1);

	/* Save off the semantic & the output format */
	if(sSemantic != NULL)
		strncpy(pThis->sSemantic, sSemantic, DASENC_SEM_LEN - 1);

	if(sOutFmt != NULL)
		strncpy(pThis->sOutFmt, sOutFmt, DASENC_FMT_LEN-1);

	if(nSzEach == 0)
		return das_error(DASERR_ENC, "Invalid item size in buffer: 0");
	if(nSzEach == DASENC_ITEM_LEN){
		pThis->bItemLen = true;
		return das_error(DASERR_ENC, 
			"Parsing in-packet value lengths is not yet supported. Use seperators for now."
		);
	}
	else{
		pThis->bItemLen = false;
	}

	bool bDateTime = false;

	/* Don't let the array delete itself out from under us*/
	inc_DasAry(pThis->pAry);

	/* Makes the code below shorter */
	das_val_type vtAry = DasAry_valType( pThis->pAry ); 

	/* Save off the value size for the array */
	pThis->nAryValSz = das_vt_size(vtAry);

	ptrdiff_t aShape[DASIDX_MAX] = {0};
	int nRank = DasAry_shape(pThis->pAry, aShape);
	ptrdiff_t nLastIdxSz = aShape[nRank - 1];

	/* Figure out the encoding of data in the external buffer 
	   first handle the integral types */
	bool bIntegral = false;
	if(strcmp(sEncType, "BEint") == 0){
		switch(nSzEach){
		case 8: pThis->vtBuf = vtLong;  break;
		case 4: pThis->vtBuf = vtInt;   break;
		case 2: pThis->vtBuf = vtShort; break;
		case 1: pThis->vtBuf = vtByte;  break;
		default:  goto BAD_FORMAT;
		}
#ifdef HOST_IS_LSB_FIRST
		pThis->uProc |= DASENC_SWAP;
#endif
		bIntegral = true;
	}
	else if(strcmp(sEncType, "LEint") == 0){
		switch(nSzEach){
		case 8: pThis->vtBuf = vtLong;  break;
		case 4: pThis->vtBuf = vtInt;   break;
		case 2: pThis->vtBuf = vtShort; break;
		case 1: pThis->vtBuf = vtByte;  break;
		default:  goto BAD_FORMAT;
		}
		bIntegral = true;
	}
	else if(strcmp(sEncType, "BEuint") == 0){
		switch(nSzEach){
		case 8: pThis->vtBuf = vtULong;  break;
		case 4: pThis->vtBuf = vtUInt;   break;
		case 2: pThis->vtBuf = vtUShort; break;
		case 1: pThis->vtBuf = vtUByte;  break;
		default:  goto BAD_FORMAT;
		}
#ifdef HOST_IS_LSB_FIRST
		pThis->uProc |= DASENC_SWAP;
#endif
		bIntegral = true;
	}
	else if(strcmp(sEncType, "LEuint") == 0){
		switch(nSzEach){
		case 8: pThis->vtBuf = vtULong;  break;
		case 4: pThis->vtBuf = vtUInt;   break;
		case 2: pThis->vtBuf = vtUShort; break;
		case 1: pThis->vtBuf = vtUByte;  break;
		default:  goto BAD_FORMAT;
		}
		bIntegral = true;
	}
	else if(strcmp(sEncType, "BEreal") == 0){
		switch(nSzEach){
		case 8: pThis->vtBuf = vtDouble;  break;
		case 4: pThis->vtBuf = vtFloat;   break;
		default:  goto BAD_FORMAT;
		}
#ifdef HOST_IS_LSB_FIRST
		pThis->uProc |= DASENC_SWAP;
#endif
		bIntegral = true;
	}
	else if(strcmp(sEncType, "LEreal") == 0){
		switch(nSzEach){
		case 8: pThis->vtBuf = vtDouble;  break;
		case 4: pThis->vtBuf = vtFloat;   break;
		default:  goto BAD_FORMAT;
		}
		bIntegral = true;
	}
	else if(strcmp(sEncType, "byte") == 0){
		if(nSzEach != 1) goto BAD_FORMAT;
		pThis->vtBuf = vtByte;
		bIntegral = true;
	}
	else if(strcmp(sEncType, "ubyte") == 0){
		if(nSzEach != 1) goto BAD_FORMAT;
		pThis->vtBuf = vtUByte;
		bIntegral = true;
	}

	if(bIntegral){
		if(das_vt_size(pThis->vtBuf) > das_vt_size(vtAry)){
			if(bRead) goto UNSUPPORTED_READ;
			else pThis->uProc |= DASENC_CAST_DOWN;
		}

		/* If the array value type is floating point and the buffer type is 
		   integer, then it must be wider then the integers ??? */
		if(das_vt_isint(pThis->vtBuf) && das_vt_isreal(vtAry)){
			if(das_vt_size(vtAry) == das_vt_size(pThis->vtBuf)){
				if(bRead) goto UNSUPPORTED_READ;
				else pThis->uProc |= DASENC_CAST_DOWN;
			}
		}

		/* I need to cast values up to a larger size, flag that */
		if(das_vt_size(pThis->vtBuf) < das_vt_size(vtAry))
			pThis->uProc |= DASENC_CAST_UP;

		/* Temporary: Remind myself to call DasAry_markEnd() when writing
		   non-string variable length items */
		if(nLastIdxSz == DASIDX_RAGGED){
			daslog_info("Hi Developer: Variable length last index detected, "
			            "make sure you call DasAry_markEnd() after packet reads.");
		}
		goto SUPPORTED;
	}

	if(strcmp(sEncType, "utf8") != 0){
		/* goto UNSUPPORTED; */
		goto UNSUPPORTED_READ; /* <-- could use generic unsupported instead */
	}
	
	pThis->vtBuf = vtText;
	pThis->uProc |= DASENC_TEXT;

	if(pThis->nBufValSz < 1)
		pThis->uProc |= DASENC_VARSZ;

	/* Deal with the text types */
	if(strcmp(sSemantic, "bool") == 0){
		return das_error(DASERR_NOTIMP, "TODO: Add parsing for 'true', 'false' etc.");
	}
	else if((strcmp(sSemantic, "integer") == 0)||(strcmp(sSemantic, "real") == 0)){
		pThis->uProc |= DASENC_PARSE;
	}
	else if((strcmp(sSemantic, "datetime") == 0)){
		bDateTime = true;

		/* For datetimes stored as text, we just run it in */
		if((vtAry != vtUByte)&&(vtAry != vtByte)){

			pThis->uProc |= DASENC_PARSE; /* Gonna have to parse the text */

			/* If we're storing this as a datetime structure it's covered, if 
		   	we need to convert to something else the units are needed */
			if(vtAry != vtTime){
			
				if( (epoch == NULL) || (! Units_haveCalRep(epoch) ) )
					goto UNSUPPORTED_READ; /* just UNSUPPORTED ? */
		
				/* Check that the array element size is big enough for the units in question 
				   If the data are not stored as text */
				if((epoch == UNIT_TT2000)&&(vtAry != vtLong)&&(vtAry != vtDouble))
					goto UNSUPPORTED_READ; /* just UNSUPPORTED ? */
				else
					if((vtAry != vtDouble)&&(vtAry != vtFloat))
						goto UNSUPPORTED_READ; /* just UNSUPPORTED ? */
			}

			pThis->timeUnits = epoch;  /* In addition to parsing, we have to convert */
		}
		else{
			pThis->timeUnits = UNIT_UTC; /* kind of a fake placeholder unit */	
		}
	}
	else if(strcmp(sSemantic, "string") == 0){

		/* Expect uByte storage for strings not vtText as there is no external place
			to put the string data */
		if((vtAry != vtUByte)&&(vtAry != vtByte))
      	/* goto UNSUPPORTED; */
      	goto UNSUPPORTED_READ;

		if(DasAry_getUsage(pThis->pAry) & D2ARY_AS_STRING){
			pThis->uProc |= DASENC_NULLTERM;
		}

		/* If storing string data, we need to see if the last index of the array is 
		   big enough */
		if((nLastIdxSz != DASIDX_RAGGED)&&(nLastIdxSz < nSzEach)){
			/* goto UNSUPPORTED;  <-- could probably use generic one here */
			goto UNSUPPORTED_READ;
		}

		if((nLastIdxSz == DASIDX_RAGGED)&&(nRank > 1))
			pThis->uProc |= DASENC_WRAP;   /* Wrap last index for ragged strings */
	}
	else{
		/* goto UNSUPPORTED;  <-- could probably use generic one here */
		goto UNSUPPORTED_READ;
	}


	SUPPORTED:
	pThis->uProc |= DASENC_VALID;  /* Set the valid encoding bit */
	return DAS_OKAY;

	BAD_FORMAT:
		return das_error(DASERR_ENC, "For array %s: %d byte %s encoding is not understood.",
			nSzEach, sEncType
		);

	UNSUPPORTED_READ:
	if(bDateTime)
		return das_error(DASERR_ENC, "For array %s: Can not encode/decode datetime data from buffers "
			"with encoding '%s' for items of %hd bytes each to/from an array of "
			" '%s' type elements with time units of '%s'", DasAry_id(pAry), sEncType,
			nSzEach,  das_vt_toStr(vtAry), epoch == NULL ? "none" : Units_toStr(epoch)
		);
	else
		return das_error(DASERR_ENC, "For array %s: Can not encode/decode '%s' data from buffers "
			"with encoding '%s' for items of %hd bytes each to/from an array of "
			" '%s' type elements", DasAry_id(pAry), sSemantic, sEncType, nSzEach, das_vt_toStr(vtAry)
		);
}

DasErrCode DasCodec_setUtf8Fmt(
	DasCodec* pThis, const char* sValFmt, int16_t nFmtWidth, ubyte nSep,
	const char* sSepSet
){
	if((pThis->vtBuf != vtText)||(pThis->vtBuf != vtTime))
		return das_error(DASERR_ENC, "Output encoding is, %s, not UTF-8", 
			das_vt_serial_type(pThis->vtBuf)
		); 
	
	if(nFmtWidth > 0){
		pThis->nBufValSz = nFmtWidth + 1;
	}
	else{
		pThis->nBufValSz = -1;  /* Go to pure variable with output */
	}

	strncpy(pThis->sOutFmt, sValFmt, DASENC_FMT_LEN - 1);	
	if((nSep > 0) && (sSepSet != NULL))
		memcpy(pThis->sSepSet, sSepSet, (nSep < DASIDX_MAX ? nSep : DASIDX_MAX) );

	return DAS_OKAY;
}

bool DasCodec_isReader(const DasCodec* pThis){
	return (pThis->uProc & DASENC_READER);
}

/* ************************************************************************* */

void DasCodec_postBlit(DasCodec* pThis, DasAry* pAry){
	pThis->bResLossWarn = false; /* Clear resolution loss warning flag */
	pThis->pAry = pAry;          /* Repoint to my internal array */
	pThis->pOverflow = NULL;     /* Reset the overflow buffer */
	pThis->uOverflow = 0;
}


/* ************************************************************************* */
void DasCodec_deInit(DasCodec* pThis){
	if(pThis->pOverflow)
		free(pThis->pOverflow);

	if(pThis && pThis->pAry) 
		dec_DasAry(pThis->pAry);

	memset(pThis, 0, sizeof(DasCodec));
}

/* ************************************************************************* */
/* Read helper */

static DasErrCode _swap_read(ubyte* pDest, const ubyte* pSrc, size_t uVals, int nSzEa){
   /* Now swap and write */
	ubyte uSwap[8];

	switch(nSzEa){
	case 2:
		for(size_t u = 0; u < (uVals*2); u += 2){
			uSwap[0] = pSrc[u+1];
			uSwap[1] = pSrc[u];
			*((uint16_t*)(pDest + u)) = *((uint16_t*)uSwap);
		}
	case 4:
		for(size_t u = 0; u < (uVals*4); u += 4){
			uSwap[0] = pSrc[u+3];
			uSwap[1] = pSrc[u+2];
			uSwap[2] = pSrc[u+1];
			uSwap[3] = pSrc[u];
			*((uint32_t*)(pDest + u)) = *((uint32_t*)uSwap);
		}
	case 8:
		for(size_t u = 0; u < (uVals*8); u += 8){
			uSwap[0] = pSrc[u+7];
			uSwap[1] = pSrc[u+6];
			uSwap[2] = pSrc[u+5];
			uSwap[3] = pSrc[u+4];
			uSwap[4] = pSrc[u+3];
			uSwap[5] = pSrc[u+2];
			uSwap[6] = pSrc[u+1];
			uSwap[7] = pSrc[u];
			*((uint64_t*)(pDest + u)) = *((uint64_t*)uSwap);
		}
	default:
		return das_error(DASERR_ENC, "Logic error");
	}
	return DAS_OKAY;
}

/* ************************************************************************* */
/* Read helper */

/* TODO: Refactor via das_value_binXform */
static DasErrCode _cast_read(
	ubyte* pDest, const ubyte* pSrc, size_t uVals, das_val_type vtAry, das_val_type vtBuf
){
	size_t v;
	switch(vtAry){
	case vtDouble:
		switch(vtBuf){
		case vtUByte:  for(v=0; v<uVals; ++v) *((double*)(pDest + 8*v)) = pSrc[v];                    break;
		case vtByte:   for(v=0; v<uVals; ++v) *((double*)(pDest + 8*v)) = *((  int8_t*)(pSrc + v  )); break;
		case vtUShort: for(v=0; v<uVals; ++v) *((double*)(pDest + 8*v)) = *((uint16_t*)(pSrc + v*2)); break;
		case vtShort:  for(v=0; v<uVals; ++v) *((double*)(pDest + 8*v)) = *(( int16_t*)(pSrc + v*2)); break;
		case vtUInt:   for(v=0; v<uVals; ++v) *((double*)(pDest + 8*v)) = *((uint32_t*)(pSrc + v*4)); break;
		case vtInt:    for(v=0; v<uVals; ++v) *((double*)(pDest + 8*v)) = *(( int32_t*)(pSrc + v*4)); break;
		case vtFloat:  for(v=0; v<uVals; ++v) *((double*)(pDest + 8*v)) = *((   float*)(pSrc + v*4)); break;
		default: return das_error(DASERR_ENC, ENCODER_SETUP_ERROR);
		}
		return DAS_OKAY;
	case vtLong:
		switch(vtBuf){
		case vtUByte:  for(v=0; v<uVals; ++v) *((int64_t*)(pDest + 8*v)) = pSrc[v];                    break;
		case vtByte:   for(v=0; v<uVals; ++v) *((int64_t*)(pDest + 8*v)) = *((  int8_t*)(pSrc + v  )); break;
		case vtUShort: for(v=0; v<uVals; ++v) *((int64_t*)(pDest + 8*v)) = *((uint16_t*)(pSrc + v*2)); break;
		case vtShort:  for(v=0; v<uVals; ++v) *((int64_t*)(pDest + 8*v)) = *(( int16_t*)(pSrc + v*2)); break;
		case vtUInt:   for(v=0; v<uVals; ++v) *((int64_t*)(pDest + 8*v)) = *((uint32_t*)(pSrc + v*4)); break;
		case vtInt:    for(v=0; v<uVals; ++v) *((int64_t*)(pDest + 8*v)) = *(( int32_t*)(pSrc + v*4)); break;
		default: return das_error(DASERR_ENC, ENCODER_SETUP_ERROR);	
		}
		return DAS_OKAY;
	case vtULong:
		switch(vtBuf){
		case vtUByte:  for(v=0; v<uVals; ++v) *((uint64_t*)(pDest + 8*v)) = pSrc[v];                    break;
		case vtUShort: for(v=0; v<uVals; ++v) *((uint64_t*)(pDest + 8*v)) = *((uint16_t*)(pSrc + v*2)); break;
		case vtUInt:   for(v=0; v<uVals; ++v) *((uint64_t*)(pDest + 8*v)) = *((uint32_t*)(pSrc + v*4)); break;
		default: return das_error(DASERR_ENC, ENCODER_SETUP_ERROR);	
		}
		return DAS_OKAY;
	case vtFloat:
		switch(vtBuf){
		case vtUByte:  for(v=0; v<uVals; ++v) *((float*)(pDest + 4*v)) = pSrc[v];                    break;
		case vtByte:   for(v=0; v<uVals; ++v) *((float*)(pDest + 4*v)) = *((  int8_t*)(pSrc + v  )); break;
		case vtUShort: for(v=0; v<uVals; ++v) *((float*)(pDest + 4*v)) = *((uint16_t*)(pSrc + v*2)); break;
		case vtShort:  for(v=0; v<uVals; ++v) *((float*)(pDest + 4*v)) = *(( int16_t*)(pSrc + v*2)); break;
		default: return das_error(DASERR_ENC, ENCODER_SETUP_ERROR);
		}
		return DAS_OKAY;
	case vtInt:
		switch(vtBuf){
		case vtUByte:  for(v=0; v<uVals; ++v) *((int32_t*)(pDest + 4*v)) = pSrc[v];                    break;
		case vtByte:   for(v=0; v<uVals; ++v) *((int32_t*)(pDest + 4*v)) = *((  int8_t*)(pSrc + v  )); break;
		case vtUShort: for(v=0; v<uVals; ++v) *((int32_t*)(pDest + 4*v)) = *((uint16_t*)(pSrc + v*2)); break;
		case vtShort:  for(v=0; v<uVals; ++v) *((int32_t*)(pDest + 4*v)) = *(( int16_t*)(pSrc + v*2)); break;
		default: return das_error(DASERR_ENC, ENCODER_SETUP_ERROR);	
		}
		return DAS_OKAY;
	case vtUInt:
		switch(vtBuf){
		case vtUByte:  for(v=0; v<uVals; ++v) *((uint32_t*)(pDest + 4*v)) = pSrc[v];                    break;
		case vtUShort: for(v=0; v<uVals; ++v) *((uint32_t*)(pDest + 4*v)) = *((uint16_t*)(pSrc + v*2)); break;
		default: return das_error(DASERR_ENC, ENCODER_SETUP_ERROR);	
		}
		return DAS_OKAY;
	case vtShort:
		switch(vtBuf){
		case vtUByte:  for(v=0; v<uVals; ++v) *((int16_t*)(pDest + 2*v)) = pSrc[v];                  break;
		case vtByte:   for(v=0; v<uVals; ++v) *((int16_t*)(pDest + 2*v)) = *((int8_t*)(pSrc + v  )); break;
		default: return das_error(DASERR_ENC, ENCODER_SETUP_ERROR);	
		}
		return DAS_OKAY;
	case vtUShort:
		switch(vtBuf){
		case vtUByte:  for(v=0; v<uVals; ++v) *((uint16_t*)(pDest + 2*v)) = pSrc[v]; break;
		default: return das_error(DASERR_ENC, ENCODER_SETUP_ERROR);	
		}
		return DAS_OKAY;
	default:
		break;
	}
	return das_error(DASERR_ENC, ENCODER_SETUP_ERROR);	
}

/* ************************************************************************* */
/* Read helper */

/* TODO: Refactor via das_value_binXform */

#define _SWP2(p) val[0] = *(p+1); val[1] = *(p)
#define _SWP4(p) val[0] = *(p+3); val[1] = *(p+2); val[2] = *(p+1); val[3] = *(p)

static DasErrCode _swap_cast_read(
	ubyte* pDest, const ubyte* pSrc, size_t uVals, das_val_type vtAry, das_val_type vtBuf
){
	size_t v;
	ubyte val[8];

	switch(vtAry){
	case vtDouble:
		switch(vtBuf){
		case vtUShort: for(v=0; v<uVals; ++v){ _SWP2(pSrc + v*2); *((double*)(pDest + 8*v)) = *((uint16_t*)val); } break;
		case vtShort:  for(v=0; v<uVals; ++v){ _SWP2(pSrc + v*2); *((double*)(pDest + 8*v)) = *(( int16_t*)val); } break;
		case vtUInt:   for(v=0; v<uVals; ++v){ _SWP4(pSrc + v*4); *((double*)(pDest + 8*v)) = *((uint32_t*)val); } break;
		case vtInt:    for(v=0; v<uVals; ++v){ _SWP4(pSrc + v*4); *((double*)(pDest + 8*v)) = *(( int32_t*)val); } break;
		case vtFloat:  for(v=0; v<uVals; ++v){ _SWP4(pSrc + v*4); *((double*)(pDest + 8*v)) = *((   float*)val); } break;
		default: return das_error(DASERR_ENC, ENCODER_SETUP_ERROR);
		}
		return DAS_OKAY;
	case vtLong:
		switch(vtBuf){
		case vtUShort: for(v=0; v<uVals; ++v){ _SWP2(pSrc + v*2); *((int64_t*)(pDest + 8*v)) = *((uint16_t*)val); } break;
		case vtShort:  for(v=0; v<uVals; ++v){ _SWP2(pSrc + v*2); *((int64_t*)(pDest + 8*v)) = *(( int16_t*)val); } break;
		case vtUInt:   for(v=0; v<uVals; ++v){ _SWP4(pSrc + v*4); *((int64_t*)(pDest + 8*v)) = *((uint32_t*)val); } break;
		case vtInt:    for(v=0; v<uVals; ++v){ _SWP4(pSrc + v*4); *((int64_t*)(pDest + 8*v)) = *(( int32_t*)val); } break;
		default: return das_error(DASERR_ENC, ENCODER_SETUP_ERROR);	
		}
		return DAS_OKAY;
	case vtULong:
		switch(vtBuf){
		case vtUShort: for(v=0; v<uVals; ++v){ _SWP2(pSrc + v*2); *((uint64_t*)(pDest + 8*v)) = *((uint16_t*)val); } break;
		case vtUInt:   for(v=0; v<uVals; ++v){ _SWP4(pSrc + v*4); *((uint64_t*)(pDest + 8*v)) = *((uint32_t*)val); } break;
		default: return das_error(DASERR_ENC, ENCODER_SETUP_ERROR);	
		}
		return DAS_OKAY;
	case vtFloat:
		switch(vtBuf){
		case vtUShort: for(v=0; v<uVals; ++v){ _SWP2(pSrc + v*2); *((float*)(pDest + 4*v)) = *((uint16_t*)val); } break;
		case vtShort:  for(v=0; v<uVals; ++v){ _SWP2(pSrc + v*2); *((float*)(pDest + 4*v)) = *(( int16_t*)val); } break;
		default: return das_error(DASERR_ENC, ENCODER_SETUP_ERROR);
		}
		return DAS_OKAY;
	case vtInt:
		switch(vtBuf){
		case vtUShort: for(v=0; v<uVals; ++v){ _SWP2(pSrc + v*2); *((int32_t*)(pDest + 4*v)) = *((uint16_t*)val); } break;
		case vtShort:  for(v=0; v<uVals; ++v){ _SWP2(pSrc + v*2); *((int32_t*)(pDest + 4*v)) = *(( int16_t*)val); } break;
		default: return das_error(DASERR_ENC, ENCODER_SETUP_ERROR);	
		}
		return DAS_OKAY;
	case vtUInt:
		if(vtBuf == vtUShort){
			for(v=0; v<uVals; ++v){ _SWP2(pSrc + v*2); *((uint32_t*)(pDest + 4*v)) = *((uint16_t*)val); } 
			return DAS_OKAY;
		}
		break;
	default:
		break;
	}

	return das_error(DASERR_ENC, ENCODER_SETUP_ERROR);	
}

/* ************************************************************************ */
/* Helper for helpers */

static int _convert_n_store_text(DasCodec* pThis, const char* sValue)
{
	ubyte aValue[sizeof(das_time)];

	das_val_type vtAry = DasAry_valType(pThis->pAry);
	int nRet = das_value_fromStr(aValue, sizeof(das_time), vtAry, sValue);
	if(nRet != DAS_OKAY)
		return -1 * nRet;

	/* Simple conversion, either not a time, or stored as time structure */
	if((pThis->timeUnits == NULL)||(vtAry == vtTime)){
		
		DasAry_append(pThis->pAry, aValue, 1);
		return DAS_OKAY;
	}

	/* TT2000 time conversion */
	if(pThis->timeUnits == UNIT_TT2000){
		int64_t nTime = dt_to_tt2k((das_time*)aValue);

		if(vtAry != vtLong){
			if(vtAry == vtDouble){
				if(! pThis->bResLossWarn ){
					daslog_warn_v(
						"Resolution loss detected while converting TT2000 values "
						"to %s.  Hint: Use the 'storage' attribute in your streams "
						"to fix this.", das_vt_toStr(vtAry)
					);
					pThis->bResLossWarn = true;
				}
				double rTime = (double)nTime;
				memcpy(&nTime, &rTime, 8);
			}
			else{
				return das_error(DASERR_ENC,
					"Refusing to store TT2000 values in a %s", das_vt_toStr(vtAry)
				);
			}
		}
		
		DasAry_append(pThis->pAry, (const ubyte*) &nTime, 1);
		return DAS_OKAY;
	}

	/* Any other time conversion */
	double rTime = Units_convertFromDt(pThis->timeUnits, (das_time*)aValue);
	if(vtAry != vtDouble){
		if(vtAry == vtFloat){
			if(! pThis->bResLossWarn){
				daslog_warn_v(
					"Resolution loss detected while converting %s values "
					"to %s.  Hint: Use the 'storage' attribute in you're streams "
					"to fix this.", Units_toStr(pThis->timeUnits), das_vt_toStr(vtAry)
				);
				pThis->bResLossWarn = true;
			}
			float rTime2 = (float)rTime;
			DasAry_append(pThis->pAry, (const ubyte*) &rTime2, 1);
			return DAS_OKAY;			
		}
		else{
			return das_error(DASERR_ENC,"Refusing to store %s values in a %s", 
				Units_toStr(pThis->timeUnits), das_vt_toStr(vtAry)
			);
		}
	}
	DasAry_append(pThis->pAry, (const ubyte*) &rTime, 1);
	return DAS_OKAY;
}

/* Helper ***************************************************************** */

/* Returns the number of bytes read, or a negative error code */
static int _fixed_text_convert(
	DasCodec* pThis, const ubyte* pBuf, int nSzEach, int nNumToRead
){
	
	if(nSzEach > 127){
		return -1 * das_error(DASERR_NOTIMP, 
			"Handling fixed text values larger then 127 bytes is not yet implemented"
		);
	}

	const char* pRead = (const char*) pBuf;
	char* pWrite = NULL;
	int nRet;

	char sValue[128] = {'\0'};
	int nBytesRead = 0; 

	for(int i = 0; i < nNumToRead; ++i){
		memset(sValue, 0, 128);
		/* Copy in the non whitespace text */
		pWrite = sValue;
		for(int j = 0; j < nSzEach; ++j){
			if((*pRead != '\0')&&(!isspace(*pRead))){
				*pWrite = *pRead;
				++pWrite;
			}
			++pRead;
			++nBytesRead;
		}
		// Store fill or an actual value
		if(sValue[0] == '\0')
			DasAry_append(pThis->pAry, NULL, 1);
		else
			if((nRet = _convert_n_store_text(pThis, sValue)) != DAS_OKAY)
				return -1 * nRet;
	}

	return nBytesRead;
}

/* Helper's helper ******************************************************** */

static int _var_text_item_sz(const char* pBuf, int nBufLen, char cSep, bool bSpaceSep)
{
	/* Break the value on a null, or a seperator. 
	   If the separator is null, then break on space characters */
	int nSize = 0;
	while( 
		(nBufLen > 0)&&(*pBuf != cSep)&&(*pBuf != '\0')&&
		(!bSpaceSep || !(isspace(*pBuf)) )
	){
		--nBufLen;
		++nSize;
		++pBuf;
	}
	return nSize;
}

/* Helper ***************************************************************** */

/* Returns bytes actually read, or negative error */
static int _var_text_read(
	DasCodec* pThis, const ubyte* pBuf, int nBufLen, int nValsToRead, int* pValsDidRead
){
	/* Make into fuctions */
	if(pThis->vtBuf != vtText){
		return -1 * das_error(DASERR_ENC, "Expected a text type for the external buffer");
	}

#ifndef NDEBUG
	das_val_type vtAry = DasAry_valType( pThis->pAry );
#endif

	bool bParse = ((pThis->uProc & DASENC_PARSE) != 0);
	bool bSpaceSep = ((pThis->uProc & DASENC_EAT_SPACE) != 0);
	int nRet;
	char cSep = pThis->sSepSet[0];
	const char* pRead = (const char*)pBuf;
	int nLeft = nBufLen;

	char sValue[128] = {'\0'}; // small vector assumption, use pThis->pOverflow if needed
	char* pValue = NULL;
	
	int nValSz = 0;
	*pValsDidRead = 0;

	/* The value reading loop */
	while( (nLeft > 0)&&( (nValsToRead < 0) || (*pValsDidRead < nValsToRead) ) ){
		
   	/* 1. Eat any proceeding separators, shouldn't have to do this, but
   	      it's often needed if cSep = ' ' (aka space) 
      */
		while( (nLeft > 0)&&( *pRead == cSep||*pRead == '\0'||(bSpaceSep && isspace(*pRead)) ) ){
			++pRead;
			--nLeft;
			if(nLeft == 0)
				goto PARSE_DONE;
		}

		/* 2. Get the size of the value */
		nValSz = _var_text_item_sz(pRead, nLeft, cSep, bSpaceSep);
		if(nValSz == 0)
			break;

		/* 3. Copy it over into or local area, or to the overflow */
		if(nValSz > 127){
			if(nValSz > pThis->uOverflow){
				if(pThis->pOverflow) free(pThis->pOverflow);
				pThis->uOverflow = nValSz*2;
				pThis->pOverflow = (char*)calloc(pThis->uOverflow, sizeof(char));
			}
			else{
				memset(pThis->pOverflow, 0, pThis->uOverflow); /* Just clear it */
			}
			pValue = pThis->pOverflow;
		}
		else{
			memset(sValue, 0, 128);
			pValue = sValue;
		}
		strncpy(pValue, pRead, nValSz);
		pRead += nValSz;
		nLeft -= nValSz;

		/* Since terminators are supposted to follow values, but don't have to for the
		   last one, eat the next seperator if you see it */
		if(cSep && (nLeft > 0)&&(*pRead == cSep)){
			++pRead; --nLeft;
		}

		/* 4. Convert and save, or just save, with optional null and wrap */
		if(bParse){
			if((nRet = _convert_n_store_text(pThis, pValue)) != DAS_OKAY)
				return -1 * nRet;
		}
		else{
			assert(vtAry == vtUByte);

			/* Just assume it's a variable length text string */
			if(!DasAry_append(pThis->pAry, (const ubyte*) pValue, nValSz))
				return -1 * DASERR_ARRAY;

			if(pThis->uProc & DASENC_NULLTERM){
				ubyte uNull = 0;
				if(!DasAry_append(pThis->pAry, &uNull, 1))
					return -1 * DASERR_ARRAY;
			}

			if(pThis->uProc & DASENC_WRAP){
				DasAry_markEnd(pThis->pAry, DasAry_rank(pThis->pAry) - 1);
			}
		}

		/* 5. Record that a value was written */
		++(*pValsDidRead);
	}

PARSE_DONE:
	return nBufLen - nLeft;
}

/* ************************************************************************* */
/* Main decoder */

/* The goal of this function is to read the expected number of values from
 * an upstream source */
int DasCodec_decode(
	DasCodec* pThis, const ubyte* pBuf, int nBufLen, int nExpect, int* pValsRead
){
	assert(pThis->uProc & DASENC_VALID);

	if((pThis->uProc & DASENC_READER) == 0)
		return -1 * das_error(DASERR_ENC, 
			"Codec is set to encode mode, call DasEncode_update() to change"
		);
	
	if(nExpect == 0) return nBufLen;  /* Successfully do nothing */
	if(nBufLen == 0) return 0;

	das_val_type vtAry = DasAry_valType( pThis->pAry );  
	
	DasErrCode nRet = DAS_OKAY;
	ubyte uNull = 0;

	int nValsToRead = -1;  /* Becomes vals we did read before return */
	int nBytesRead = 0;    /* Used to get the return value */

	/* Know how many I want, know how big each one is (common das2 case) */
	if((pThis->nBufValSz > 0)&&(nExpect > 0)){
		if(nBufLen < (nExpect * pThis->nBufValSz)){
			return -1 * das_error(DASERR_ENC, 
				"Remaining read bytes, %d, too small to supply %d %d byte values",
				nBufLen, nExpect, pThis->nBufValSz
			);
		}
		nValsToRead = nExpect;
	}
	/* Know how many I want, don't know how big each one is (voyager events list) */
	else if((pThis->nBufValSz < 1)&&(nExpect > 0)){
		nValsToRead = nExpect;
		assert(pThis->uProc & DASENC_VARSZ);
	}
	/* Know how big each one is, don't know how many I want (decompressed cassini waveforms) */
	else if((pThis->nBufValSz > 0)&&(nExpect < 0)){
		nValsToRead = nBufLen / pThis->nBufValSz;
		if(nBufLen < pThis->nBufValSz){
			return -1 * das_error(DASERR_ENC,
				"Remaining read bytes, %zu, are too small to supply a single %d byte value",
				nBufLen, pThis->nBufValSz
			);
		}
	}
	/* Don't know how big each one is, don't know how many I want (header value parsing)*/
	else{
		assert((pThis->uProc & (DASENC_TEXT|DASENC_VARSZ)) == (DASENC_TEXT|DASENC_VARSZ));
	}

	/* The BIG switch one case = one decoding method */
	ubyte* pWrite = NULL;
	switch(pThis->uProc & DASENC_MAJ_MASK){

	/* Easy mode, external data and internal array have the same value type */
	case DASENC_READER:
		assert(pThis->nBufValSz == pThis->nAryValSz);
		assert(pThis->nBufValSz > 0);
		assert(nValsToRead > 0);

		if((pWrite = DasAry_append(pThis->pAry, pBuf, nValsToRead)) == NULL)
			return -1 * DASERR_ARRAY;
		
		nBytesRead = nValsToRead * (pThis->nBufValSz);
		break;


	/* Almost easy, only need to swap to get into internal storage */
	case DASENC_READER|DASENC_SWAP:
		assert(pThis->nBufValSz == pThis->nAryValSz);
		assert(nValsToRead > 0);
		assert(pThis->nBufValSz > 0);

		 /* Alloc space as fill, then write in swapped values */
		if((pWrite = DasAry_append(pThis->pAry, NULL, nValsToRead)) == NULL)
			return -1 * DASERR_ARRAY;

		if((nRet = _swap_read(pWrite, pBuf, nValsToRead, pThis->nBufValSz)) != DAS_OKAY)
			return -1 * nRet;

		nBytesRead = nValsToRead * (pThis->nBufValSz);
		break;


	/* Need to cast values up to a larger type for storage */
	case DASENC_READER|DASENC_CAST_UP:
		assert(nValsToRead > 0);
		assert(pThis->nBufValSz > 0);

		ubyte* pWrite = NULL;
		if((pWrite = DasAry_append(pThis->pAry, NULL, nValsToRead)) == NULL)
			return -1 * DASERR_ARRAY;

		if((nRet = _cast_read(pWrite, pBuf, nValsToRead, vtAry, pThis->vtBuf)) != DAS_OKAY)
			return -1 * nRet;
		
		nBytesRead = nValsToRead * (pThis->nBufValSz);
		break;

	case DASENC_READER|DASENC_CAST_DOWN:
	case DASENC_READER|DASENC_CAST_DOWN|DASENC_SWAP:
		return das_error(DASERR_ENC, "Downcasting to smaller types not supported on read");
		break;


	/* Bigest binary change, swap and cast to a larger type for storage */
	case DASENC_READER|DASENC_CAST_UP|DASENC_SWAP:
		assert(nValsToRead > 0);
		assert(pThis->nBufValSz > 0);

		if((pWrite = DasAry_append(pThis->pAry, NULL, nValsToRead)) == NULL)
			return -1 * DASERR_ARRAY;

		if((nRet = _swap_cast_read(pWrite, pBuf, nValsToRead, vtAry, pThis->vtBuf)) != DAS_OKAY)
			return -1 * nRet;
		
		nBytesRead = nValsToRead * (pThis->nBufValSz);
		break;


	/* Easy, just run in the text, don't markEnd */
	case DASENC_READER|DASENC_TEXT:
		assert(nValsToRead > 0);
		assert(pThis->nBufValSz > 0);
		assert((pThis->uProc & DASENC_WRAP) == 0);

		if(pThis->uProc & DASENC_NULLTERM){
			for(int i = 0; i < nValsToRead; ++i){
				DasAry_append(pThis->pAry, pBuf+((pThis->nBufValSz+1)*i), pThis->nBufValSz);
				DasAry_append(pThis->pAry, &uNull, 1);
			}
		}
		else{
			DasAry_append(pThis->pAry, pBuf, (pThis->nBufValSz)*nValsToRead);
		}
		
		nBytesRead = nValsToRead * (pThis->nBufValSz);
		break;


	/* Fixed length text to parse, then run in, also common in das2 */
	case DASENC_READER|DASENC_TEXT|DASENC_PARSE:
		assert(nValsToRead > 0);
		assert(pThis->nBufValSz > 0);

		nBytesRead = _fixed_text_convert(pThis, pBuf, pThis->nBufValSz, nValsToRead);
		if(nBytesRead < 0 )
			return nBytesRead;

		break;

	/* ************** End of Fixed Length Item Cases ********************* */

	/* Search for the end, run and data, markEnd if array is variable size */
	/* or search of the end, parse, then run in the data */
	case DASENC_READER|DASENC_TEXT|DASENC_VARSZ:
	case DASENC_READER|DASENC_TEXT|DASENC_PARSE|DASENC_VARSZ:	
		{
		int nValsDidRead = 0;
		nBytesRead = _var_text_read(pThis, pBuf, nBufLen, nValsToRead, &nValsDidRead);
		nValsToRead = nValsDidRead;
		}

		if(nBytesRead < 0 )
			return nBytesRead;
		break;

	/* Must have forgot one... */
	default: 
		return -1 * das_error(DASERR_ENC, ENCODER_SETUP_ERROR);
	}

	if(pValsRead)
		*pValsRead = nValsToRead;

	return nBufLen - nBytesRead;  /* Return number of unread bytes */
}


/* ************************************************************************* */
/* Main encoder */


/* Encode helper: Swap items before writing ******************************** */

/* TODO: Refactor via das_value_binXform */

static DasErrCode _swap_write(DasBuf* pBuf, const ubyte* pSrc, size_t uVals, int nSzEa){
   /* Now swap and write */
	ubyte uSwap[8];

	switch(nSzEa){
	case 2:
		for(size_t u = 0; u < (uVals*2); u += 2){
			uSwap[0] = pSrc[u+1];
			uSwap[1] = pSrc[u];
			DasBuf_write(pBuf, uSwap, 2);
		}
	case 4:
		for(size_t u = 0; u < (uVals*4); u += 4){
			uSwap[0] = pSrc[u+3];
			uSwap[1] = pSrc[u+2];
			uSwap[2] = pSrc[u+1];
			uSwap[3] = pSrc[u];
			DasBuf_write(pBuf, uSwap, 4);
		}
	case 8:
		for(size_t u = 0; u < (uVals*8); u += 8){
			uSwap[0] = pSrc[u+7];
			uSwap[1] = pSrc[u+6];
			uSwap[2] = pSrc[u+5];
			uSwap[3] = pSrc[u+4];
			uSwap[4] = pSrc[u+3];
			uSwap[5] = pSrc[u+2];
			uSwap[6] = pSrc[u+1];
			uSwap[7] = pSrc[u];
			DasBuf_write(pBuf, uSwap, 8);
		}
	default:
		return das_error(DASERR_ENC, "Logic error");
	}
	return DAS_OKAY;
}

/* Encode helper: change widths with checks and swap *********************** */

static DasErrCode _cast_swap_write(
	DasBuf* pBuf, const ubyte* pSrc, size_t uVals, das_val_type vtAry, 
	const ubyte* pFillIn, das_val_type vtBuf, const ubyte* pFillOut
){

	uint64_t outval;
	uint64_t outswap;

	size_t   insize = das_vt_size(vtAry);
	size_t   outsize = das_vt_size(vtBuf);
	assert(outsize <= 8);

	ubyte* pIn = NULL;
	ubyte* pOut = NULL;

	int nRet;
	for(size_t u = 0; u < (uVals*insize); u += insize){

		nRet = das_value_binXform(
			vtAry, pSrc + u,          pFillIn,
			vtBuf, (ubyte*)(&outval), pFillOut,
			0
		);
		if(nRet != DAS_OKAY) return nRet;

		pIn = (ubyte*)(&outval) + (outsize - 1);
		pOut = (ubyte*)(&outswap);
		for(size_t v = 0; v < outsize; ++v){
			*pOut = *pIn;
			--pIn;
			++pOut;
		}

		nRet = DasBuf_write(pBuf, (ubyte*)&outswap, outsize);
		if(nRet != DAS_OKAY) return nRet;
	}

	return DAS_OKAY;
}

/* Encode helper: change widths with checks ******************************** */

static DasErrCode _cast_write(
	DasBuf* pBuf, const ubyte* pSrc, size_t uVals, das_val_type vtAry, 
	const ubyte* pFillIn, das_val_type vtBuf, const ubyte* pFillOut
){

	uint64_t outval;
	size_t   insize  = das_vt_size(vtAry);
	size_t   outsize = das_vt_size(vtBuf);
	assert(outsize <= 8);
	
	int nRet;
	for(size_t u = 0; u < (uVals*insize); u += insize){

		nRet = das_value_binXform(
			vtAry, pSrc + u,          pFillIn,
			vtBuf, (ubyte*)(&outval), pFillOut,
			0
		);
		if(nRet != DAS_OKAY) return nRet;

		nRet = DasBuf_write(pBuf, (ubyte*)&outval, outsize);
		if(nRet != DAS_OKAY) return nRet;
	}

	return DAS_OKAY;
}

/* Encode helper: Print as text ******************************************** */

/* This one has more presentation layer stuff the normal so that <values> blocks
   in headers look attractive */

DasErrCode _DasCodec_printItems(
	DasCodec* pThis, DasBuf* pBuf, const ubyte* pItem0, int nToWrite, 
	uint32_t uFlags
){
	das_val_type vt = DasAry_valType(pThis->pAry);
	size_t uSzEa    = DasAry_valSize(pThis->pAry);
	char cSep       = pThis->sSepSet[0];
	if(cSep == '\0') cSep = ' '; /* They didn't set one, pick a default */

	das_time dt;

	int nRet = 0;
	if(pThis->sOutFmt[0] == '\0'){
		/* We add a separator after all ascii items, so use one less then
		   the allotted space for the format string */
		nRet = das_value_fmt(
			pThis->sOutFmt, DASENC_FMT_LEN, vt, pThis->sSemantic, 
			(pThis->nBufValSz > 1) ? (pThis->nBufValSz -1) : - 1
		);
		if(nRet != DAS_OKAY)
			return nRet;
	}

	/* If the header flag is set wrap after 100 chars or so */
	int nRoughOutEa = 25;
	int nTmp = pThis->nBufValSz;
	switch(vt){
	case vtUByte:  case vtByte:  nRoughOutEa = (nTmp > 1 ) ? nTmp : 5; break;
	case vtUShort: case vtShort: nRoughOutEa = (nTmp > 1 ) ? nTmp : 8; break;
	case vtUInt:   case vtInt:   nRoughOutEa = (nTmp > 1 ) ? nTmp :12; break;
	case vtULong:  case vtLong:  nRoughOutEa = (nTmp > 1 ) ? nTmp :20; break;
	case vtFloat:                nRoughOutEa = (nTmp > 1 ) ? nTmp :12; break; /* Assume that alot of these get trimmed */
	case vtDouble:               nRoughOutEa = (nTmp > 1 ) ? nTmp :15; break; 
	case vtTime:                 nRoughOutEa = (nTmp > 1 ) ? nTmp :24; break;
	default:                     nRoughOutEa = 25; break;
	}

	int nRowLen = 0;
	bool bInHdr = uFlags & DASENC_IN_HDR;
	char sReal[64] = {'\0'};
	
	for(int i = 0; i < nToWrite; ++i){
		if(i > 0){ 
			if(bInHdr&&(nRowLen > 100)){
				if((cSep != ' ')&&(cSep != '\0'))
					DasBuf_write(pBuf, &cSep, 1);
				DasBuf_write(pBuf, "\n        ", 9);
				nRowLen = 0;
			}
			else{ 
				DasBuf_write(pBuf, &cSep, 1); 
			}
		}
		else{ if(bInHdr) DasBuf_write(pBuf, "        ", 8); }

		switch(vt){
		case vtUByte:  DasBuf_printf(pBuf, pThis->sOutFmt, *(pItem0 + i) ); break;
		case vtByte:   DasBuf_printf(pBuf, pThis->sOutFmt, *((ubyte*   )(pItem0 + i)) ); break;
		case vtUShort: DasBuf_printf(pBuf, pThis->sOutFmt, *((uint16_t*)(pItem0 + i*uSzEa)) ); break;
		case vtShort:  DasBuf_printf(pBuf, pThis->sOutFmt,  *((int16_t*)(pItem0 + i*uSzEa)) ); break;
		case vtUInt:   DasBuf_printf(pBuf, pThis->sOutFmt, *((uint32_t*)(pItem0 + i*uSzEa)) ); break;
		case vtInt:    DasBuf_printf(pBuf, pThis->sOutFmt,  *((int32_t*)(pItem0 + i*uSzEa)) ); break;
		case vtULong:  DasBuf_printf(pBuf, pThis->sOutFmt, *((uint64_t*)(pItem0 + i*uSzEa)) ); break;
		case vtLong:   DasBuf_printf(pBuf, pThis->sOutFmt,  *((int64_t*)(pItem0 + i*uSzEa)) ); break;
		case vtFloat:
		case vtDouble:
			if(vt == vtFloat)
				snprintf(sReal, 63, pThis->sOutFmt,  (double) *((float*  )(pItem0 + i*uSzEa)));
			else
				snprintf(sReal, 63, pThis->sOutFmt,  *((double*  )(pItem0 + i*uSzEa)));
			if(bInHdr) das_value_trimReal(sReal);
			DasBuf_write(pBuf, sReal, strlen(sReal)); 
			break;

		case vtTime:
			dt = *((das_time*)(pItem0 + i*uSzEa));
			/* why this works... extra arguments are ignored if format string doesn't mention them :-) */
			DasBuf_printf(pBuf, pThis->sOutFmt, dt.year, dt.month, dt.mday, dt.hour, dt.minute, dt.second);
			break;
		default:
			return das_error(DASERR_ENC, "Guess I forgot about '%s'", das_vt_toStr(vt));
		}
		nRowLen += nRoughOutEa;
	}

	/* Space to next thing */
	if(uFlags & DASENC_PKT_LAST)
		DasBuf_write(pBuf, "\n", 1);
	else
		DasBuf_write(pBuf, &cSep, 1);

	return DAS_OKAY;
}

/* Main Encoder ************************************************************ */

/* The goal of this function is to emitt all data from continuous range of
 * indexes starting at a given point.  Examples of setting the start location:
 *
 * nDim=0, pLoc=NULL  as DIM0 => Emitt the entire array
 *
 * nDim=1, pLoc={I}   as DIM1_AT(I) => Emit all data for one increment of the 
 *                                     highest index
 *
 * nDim=2, pLoc={I,J} as DIM2_AT(I,J) => Emit all data for one increment of the
 *                                     the next highest index.
 *
 * To write all data for an array set: nDim = 0, pLoc = NULL (aka use DIM0 )
 *
 * @returns negative error code, or the number of values written
 */

int DasCodec_encode(
	DasCodec* pThis, DasBuf* pBuf, int nDim, ptrdiff_t* pLoc, int nExpect, uint32_t uFlags
){
	
	if((pThis->uProc & DASENC_READER) != 0)
		return -1 * das_error(DASERR_ENC, 
			"Codec is set to decode mode, call DasEncode_update() to change"
		);

	DasErrCode nRet    = DAS_OKAY;
	DasAry* pAry       = pThis->pAry;
	das_val_type vtAry = DasAry_valType(pAry);
	
	size_t uAvailable;  /* Total items, but for strings this is total bytes + nulls! */

	const ubyte* pItem0 = DasAry_getIn(pThis->pAry, vtAry, nDim, pLoc, &uAvailable);
	
	if(uAvailable > 2147483648L)
		return -1 * das_error(DASERR_ENC, "too many values at index");
	int nAvailable = (int)uAvailable;
	if(nAvailable == 0)
		return -1* das_error(DASERR_ENC, "No values were available to write from array %s",
			DasAry_id(pAry)
		);

	int nSzEa = (int) DasAry_valSize(pAry);

	/* Make sure the available data = the write request if not var write */
	if((nExpect > 0)&&(nAvailable < nExpect)){
		if(nDim == 0){
			return -1 * das_error(DASERR_ENC, 
				"Expected to write %d values for %s, but only %d were available in "
				"the array", nExpect, DasAry_id(pAry), nAvailable
			);
		}
		else{
			char sBuf[64] = {'\0'};
			return -1 * das_error(DASERR_ENC, 
				"Expected to write %d values for %s, but only %d were available under "
				"index %td", nExpect, DasAry_id(pAry), nAvailable, 
				das_idx_prn(nDim, pLoc, 63, sBuf)
			);	
		}
	}
	
	/* Big switch to avoid decision making in loops.  Most of the items can
	   just be streamed to the output buffer without making decisions about
	   how to encode each item in a tight loop. */

	switch(pThis->uProc & DASENC_MAJ_MASK){

	/* Easy mode, interal and external data format match */
	case 0:
		assert(pThis->nBufValSz == nSzEa);
		assert(pThis->nBufValSz > 0);

		if( (nRet = DasBuf_write(pBuf, pItem0, nAvailable*nSzEa)) != DAS_OKAY)
			return -1 * nRet;
		return nAvailable;

	case DASENC_SWAP:
		assert(pThis->nBufValSz == nSzEa);
		assert(pThis->nBufValSz > 0);

		if((nRet = _swap_write(pBuf, pItem0, nAvailable, nSzEa)) != DAS_OKAY)
			return -1 * nRet;		
		return nAvailable;
		break;

	case DASENC_CAST_UP:   /* Were read WIDE, write NARROW */
	case DASENC_CAST_DOWN: /* Want even wider output */	
		assert(pThis->nBufValSz > 0);

		/* Name not a typo, the #defines are written from the encoding point of view */
		nRet = _cast_write(
			pBuf, pItem0, nAvailable, vtAry, DasAry_getFill(pAry), pThis->vtBuf,
			das_vt_fill(pThis->vtBuf)
		);
		if(nRet != DAS_OKAY)
			return -1 * nRet;
		
		return nAvailable;
		break;

	case DASENC_CAST_UP|DASENC_SWAP:  /* Were read WIDE, write NARROW, opposite endian */
	case DASENC_CAST_DOWN|DASENC_SWAP: /* Want wider swapped output */

		assert(pThis->nBufValSz > 0);

		nRet = _cast_swap_write(
			pBuf, pItem0, nAvailable, vtAry, DasAry_getFill(pAry), pThis->vtBuf,
			das_vt_fill(pThis->vtBuf)
		);
		if(nRet != DAS_OKAY)
			return -1 * nRet;
		
		return nAvailable;
		break;


	/* Text stored, text output. Easy, just run out in fixed lengths  */
	case DASENC_TEXT:
		assert(pThis->nBufValSz > 0);

		/* No wrapping usually means output is fixed size */
		assert((pThis->uProc & DASENC_WRAP) == 0);
		int nNulls = 0;
		if(pThis->uProc & DASENC_NULLTERM){

			/* The input must have been fixed length text strings so boundaries are
			   found by lengths in the header statement.  So it looks like we can 
			   just run strings out to the buffer value size, then skip the NULL and
			   do it again. */
			int iBeg = 0;
			while(iBeg < nAvailable){
				nRet = DasBuf_write(pBuf, pItem0 + iBeg, pThis->nBufValSz);
				++nNulls;
				iBeg += pThis->nBufValSz + 1; /* skip over the null */
				if(nRet != DAS_OKAY)
					break;
			}
		}
		else{
			nRet = DasBuf_write(pBuf, pItem0, nAvailable);
		}
		if(nRet != DAS_OKAY) return -1 * nRet;

		assert( ((nAvailable - nNulls) % pThis->nBufValSz) == 0);
		return (nAvailable - nNulls) / pThis->nBufValSz;
		break;


	/* Binary stored, text output, common in das2 */
	case DASENC_TEXT|DASENC_PARSE:
	case DASENC_TEXT|DASENC_PARSE|DASENC_VARSZ:

		/* Note, pThis->nBufValSz can be -1, triggers variable length output */
		
		nRet = _DasCodec_printItems(pThis, pBuf, pItem0, nAvailable, uFlags);
		if(nRet != DAS_OKAY)
			return -1* nRet;
		
		return nAvailable;
		break;

	/* variable width text output, can't avoid array geometry any longer,
	   use an iterator to stay sane */
	case DASENC_TEXT|DASENC_VARSZ:
		{
			char cSep = ' ';
			if(pThis->sSepSet[0] != '\0') cSep = pThis->sSepSet[0];
			DasAryIter iter;
			DasAryIter_init(
				&iter, 
				pAry, 
				nDim,    /* First index to iterate over */
				-2,      /* Last index to iterate over (-2 means nAryRank - 2) */
				pLoc,    /* Starting location */
				NULL     /* No ending location, just exhaust the sub-space */
			);

			size_t uStrLen = 0;
			int nWrote = 0;
			size_t uRowChars = 0;
			const char* sStr = NULL;
			int nAryRank = DasAry_rank(pAry);
			for(; !iter.done; DasAryIter_next(&iter)){
				if(uRowChars > 0){
					if(uRowChars > 80)
						DasBuf_write(pBuf, "\n", 1);
					else
						DasBuf_write(pBuf, &cSep, 1);	
				}

				sStr = DasAry_getCharsIn(pAry, nAryRank - 1, iter.index, &uStrLen);
				DasBuf_write(pBuf, sStr, uStrLen);

				uRowChars += uStrLen;
				++nWrote;
			}
			return nWrote;
		}
		break;
		
	default: 
		break;
	}

	/* I must have forgot one ... */
	return -1 * das_error(DASERR_ENC, ENCODER_SETUP_ERROR);
}
