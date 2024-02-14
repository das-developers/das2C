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

#include "aryenc.h"
#include "value.h"

/* Operations flags */
#define DASENC_VALID    0x0001 /* If not 1, not a valid encoder */
#define DASENC_SWAP     0x0002 /* If set bytes must be swapped prior to IO */
#define DASENC_CAST     0x0004 /* If set bytes must be transformed prior to IO */

#define DASENC_TEXT     0x0010 /* Input is text */
#define DASENC_PARSE    0x0020 /* Input is text that should be parsed to a value */
#define DASENC_EPOCH    0x0040 /* Input is text time to convert to epoch time */

#define DASENC_NULLTERM 0x0100 /* Input is text that should be null terminated */
#define DASENC_WRAP     0x0200 /* Wrap last dim reading a string value */


#define ENCODER_SETUP_ERROR "Logic error in encoder setup"

/* Perform various checks to see if this is even possible */ 
DasErrCode DasAryEnc_init(
   DasAryEnc* pThis, DasAry* pAry, const char* sSemantic, const char* sEncType,
   uint16_t uSzEach, ubyte cSep, das_units epoch
){
	memset(pThis, 0, sizeof(DasAryEnc));  
	pThis->cSep = cSep;
	pThis->pAry = pAry;
	assert(pAry != NULL);

	/* Don't let the array delete itself out from under us*/
	inc_DasAry(pThis->pAry);

	pThis->vtAry = DasAry_valType( pThis->vtAry );  /* Copy in the value type of the given array */

	ptrdiff_t aShape[DASIDX_MAX] = {0};
	int nRank = DasAry_shape(pThis->pAry, aShape);
	ptrdiff_t nLastIdxSz = aShape[nRank - 1];

	/* Figure out the encoding of data in the external buffer 
	   first handle the integral types */
	bool bIntegral = false;
	if(strcmp(sEncType, "BEint") == 0){
		switch(uSzEach){
		case 8: pThis->vtBuf = vtLong;  break;
		case 4: pThis->vtBuf = vtInt;   break;
		case 2: pThis->vtBuf = vtShort; break;
		case 1: pThis->vtBuf = vtByte;  break;
		default:  goto UNSUPPORTED;
		}
#ifdef HOST_IS_LSB_FIRST
		pThis->uProc |= DASENC_SWAP;
#endif
		bIntegral = true;
	}
	else if(strcmp(sEncType, "LEint") == 0){
		switch(uSzEach){
		case 8: pThis->vtBuf = vtLong;  break;
		case 4: pThis->vtBuf = vtInt;   break;
		case 2: pThis->vtBuf = vtShort; break;
		case 1: pThis->vtBuf = vtByte;  break;
		default:  goto UNSUPPORTED;
		}
		bIntegral = true;
	}
	else if(strcmp(sEncType, "BEuint") == 0){
		switch(uSzEach){
		case 8: pThis->vtBuf = vtULong;  break;
		case 4: pThis->vtBuf = vtUInt;   break;
		case 2: pThis->vtBuf = vtUShort; break;
		case 1: pThis->vtBuf = vtUByte;  break;
		default:  goto UNSUPPORTED;
		}
#ifdef HOST_IS_LSB_FIRST
		pThis->uProc |= DASENC_SWAP;
#endif
		bIntegral = true;
	}
	else if(strcmp(sEncType, "LEuint") == 0){
		switch(uSzEach){
		case 8: pThis->vtBuf = vtULong;  break;
		case 4: pThis->vtBuf = vtUInt;   break;
		case 2: pThis->vtBuf = vtUShort; break;
		case 1: pThis->vtBuf = vtUByte;  break;
		default:  goto UNSUPPORTED;
		}
		bIntegral = true;
	}
	else if(strcmp(sEncType, "BEreal") == 0){
		switch(uSzEach){
		case 8: pThis->vtBuf = vtDouble;  break;
		case 4: pThis->vtBuf = vtFloat;   break;
		default:  goto UNSUPPORTED;
		}
#ifdef HOST_IS_LSB_FIRST
		pThis->uProc |= DASENC_SWAP;
#endif
		bIntegral = true;
	}
	else if(strcmp(sEncType, "LEreal") == 0){
		switch(uSzEach){
		case 8: pThis->vtBuf = vtDouble;  break;
		case 4: pThis->vtBuf = vtFloat;   break;
		default:  goto UNSUPPORTED;
		}
		bIntegral = true;
	}
	else if(strcmp(sEncTyp, "byte") == 0){
		if(uSzEach != 1) goto UNSUPPORTED;
		pThis->vtBuf = vtByte;
		bIntegral = true;
	}
	else if(strcmp(sEncTyp, "ubyte") == 0){
		if(uSzEach != 1) goto UNSUPPORTED;
		pThis->vtBuf = vtUByte;
		bIntegral = true;
	}

	if(bIntegral){
		if(das_vt_size(pThis->vtBuf) > das_vt_size(pThis->vtAry))
			goto UNSUPPORTED;

		/* If the array value type is floating point then it must 
		   and the buffer type is integer, then it must be wider then
		   the integers */
		if(das_vt_isInt(pThis->vtBuf) && das_vt_isReal(pThis->vtAry)){
			if(das_vt_size(pThis->vtAry) == das_vt_size(pThis->vtBuf))
				goto UNSUPPORTED;
		}

		/* I need to cast values up to a larger size, flag that */
		if(das_vt_size(pThis->vtBuf) != das_vt_size(pThis->vtAry))
			pThis->uFlags |= DASENC_CAST;

		/* Temporary: Remind myself to call DasAry_markEnd() when writing
		   non-string variable length items */
		if(nLastIdxSz == DASIDX_RAGGED){
			daslog_info("Hi Developer: Variable length last index detected, "
			            "make sure you call DasAry_markEnd() after packet reads.");
		}

		return DAS_OKAY;
	}

	if(strcmp(sEncType, 'utf8') != 0){
		goto UNSUPPORTED;
	}

	pThis->uFlags |= DASENC_TEXT;

	// Deal with the text types
	if(strcmp(sSemantic, "bool") == 0){
		return das_error(DASERR_NOTIMP, "TODO: Add parsing for 'true', 'false' etc.");
	}
	else if((strcmp(sSemantic, "int") == 0)||(strcmp(sSemantic, "real") == 0)){
		pThis->uFlags |= DASENC_PARSE;
	}
	else if((strcmp(sSemantic, "datetime") == 0)){
		/* If we're storing this as a datetime structure it's covered, if 
		   we need to convert to something else the units are needed */
		if(pThis->vtAry != vtTime){
			pThis->uFlags |= DASENC_EPOCH;
			if( (epoch == NULL) || !(Units_canConvert(epoch, UNIT_US2000)) )
				goto UNSUPPORTED;
		
			/* Check that the array element size is big enough for the units in
			   question */
			if((epoch == UNIT_TT2000)&&(pThis->vtAry != vtLong)&&(pThis->vtAry != vtULong))
				goto UNSUPPORTED;
		}
	}
	else if(strcmp(sSemantic, "string") == 0){

		if(pThis->vtAry != vtUByte)   /* Expect uByte storage for strings not */
      	goto UNSUPPORTED;          /* vtText as there is no external place */
		                              /* to put the string data */

		if(DasAry_getUsage(pThis->pAry) & D2ARY_AS_STRING){
			pThis->uProc |= DASENC_NULLTERM;
		}

		/* If storing string data, we need to see if the last index of the array is 
		   big enough */
		if((nLastIdxSz != DASIDX_RAGGED)&&(nLastIdxSz < uSzEach))
			goto UNSUPPORTED;

		if((nLastIdxSz == DASIDX_RAGGED)&&(nRank > 1))
			pThis->uProc |= DASENC_WRAP;   /* Wrap last index for ragged strings */
	}
	else{
		goto UNSUPPORTED;
	}


	SUPPORTED:
	pThis->uProc |= DASENC_VALID;  /* Set the valid encoding bit */
	return DAS_OKAY;

	UNSUPPORTED:
	if(pThis->uFlags & DASENC_EPOCH)
		return das_error(DASERR_ENC, "Can not encode/decode '%s' data from buffers "
			"with encoding '%s' for items of %hs bytes each to/from an array of "
			" '%s' type elements for time units of '%s'", sSemantic, sEncType, uSzEach, 
			das_vt_toStr(pThis->vtAry), epoch == NULL ? "none" : Units_toStr(epoch)
		);
	else
		return das_error(DASERR_ENC, "Can not encode/decode '%s' data from buffers "
			"with encoding '%s' for items of %hs bytes each to/from an array of "
			" '%s' type elements", sSemantic, sEncType, uSzEach, das_vt_toStr(pThis->vtAry)
		);
}

/* ************************************************************************* */
void DasAryEnc_deInit(DasAryEnc* pThis){
	/* No dynamic memory, just decrement the array usage count */
	if(pThis && pThis->pAry) 
		dec_DasAry(pThis->pAry);
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
			*((uint16_t*)(pDest + u)) = *(uint16_t)uSwap;
		}
	case 4:
		for(size_t u = 0; u < (nVals*4); u += 4){
			uSwap[0] = pSrc[u+3];
			uSwap[1] = pSrc[u+2];
			uSwap[2] = pSrc[u+1];
			uSwap[3] = pSrc[u];
			*((uint32_t*)(pDest + u)) = *(uint32_t)uSwap;
		}
	case 8:
		for(size_t u = 0; u < (nVals*8); u += 8){
			uSwap[0] = pSrc[u+7];
			uSwap[1] = pSrc[u+6];
			uSwap[2] = pSrc[u+5];
			uSwap[3] = pSrc[u+4];
			uSwap[4] = pSrc[u+3];
			uSwap[5] = pSrc[u+2];
			uSwap[6] = pSrc[u+1];
			uSwap[7] = pSrc[u];
			*((uint64_t*)(pDest + u)) = *(uint64_t)uSwap;
		}
	default:
		return das_error(DASERR_ENC, "Logic error");
	}
	return DAS_OKAY;
}

/* ************************************************************************* */
/* Read helper */

static DasErrCode _cast_read(
	ubyte* pDest, ubyte* pSrc, size_t uVals, das_val_type vtAry, das_val_type vtBuf
){
	size_t v;
	switch(vtAry){
	case vtDouble:
		switch(vtBuf){
		case vtUbyte:  for(v=0; v<uVals; ++v) *((*double)(pDest + 8*v)) = pSrc[v];                    break;
		case vtByte:   for(v=0; v<uVals; ++v) *((*double)(pDest + 8*v)) = *((  int8_t*)(pSrc + v  )); break;
		case vtUShort: for(v=0; v<uVals; ++v) *((*double)(pDest + 8*v)) = *((uint16_t*)(pSrc + v*2)); break;
		case vtShort:  for(v=0; v<uVals; ++v) *((*double)(pDest + 8*v)) = *(( int16_t*)(pSrc + v*2)); break;
		case vtUInt:   for(v=0; v<uVals; ++v) *((*double)(pDest + 8*v)) = *((uint32_t*)(pSrc + v*4)); break;
		case vtInt:    for(v=0; v<uVals; ++v) *((*double)(pDest + 8*v)) = *(( int32_t*)(pSrc + v*4)); break;
		case vtFloat:  for(v=0; v<uVals; ++v) *((*double)(pDest + 8*v)) = *((   float*)(pSrc + v*4)); break;
		default: return das_error(DASERR_ENC, ENCODER_SETUP_ERROR);
		}
		return DAS_OKAY;
	case vtLong:
		switch(vtBuf){
		case vtUbyte:  for(v=0; v<uVals; ++v) *((*int64_t)(pDest + 8*v)) = pSrc[v];                    break;
		case vtByte:   for(v=0; v<uVals; ++v) *((*int64_t)(pDest + 8*v)) = *((  int8_t*)(pSrc + v  )); break;
		case vtUShort: for(v=0; v<uVals; ++v) *((*int64_t)(pDest + 8*v)) = *((uint16_t*)(pSrc + v*2)); break;
		case vtShort:  for(v=0; v<uVals; ++v) *((*int64_t)(pDest + 8*v)) = *(( int16_t*)(pSrc + v*2)); break;
		case vtUInt:   for(v=0; v<uVals; ++v) *((*int64_t)(pDest + 8*v)) = *((uint32_t*)(pSrc + v*4)); break;
		case vtInt:    for(v=0; v<uVals; ++v) *((*int64_t)(pDest + 8*v)) = *(( int32_t*)(pSrc + v*4)); break;
		default: return das_error(DASERR_ENC, ENCODER_SETUP_ERROR);	
		}
		return DAS_OKAY;
	case vtULong:
		switch(vtBuf){
		case vtUbyte:  for(v=0; v<uVals; ++v) *((*uint64_t)(pDest + 8*v)) = pSrc[v];                    break;
		case vtUShort: for(v=0; v<uVals; ++v) *((*uint64_t)(pDest + 8*v)) = *((uint16_t*)(pSrc + v*2)); break;
		case vtUInt:   for(v=0; v<uVals; ++v) *((*uint64_t)(pDest + 8*v)) = *((uint32_t*)(pSrc + v*4)); break;
		default: return das_error(DASERR_ENC, ENCODER_SETUP_ERROR);	
		}
		return DAS_OKAY;
	case vtFloat:
		switch(vtBuf){
		case vtUbyte:  for(v=0; v<uVals; ++v) *((*float)(pDest + 4*v)) = pSrc[v];                    break;
		case vtByte:   for(v=0; v<uVals; ++v) *((*float)(pDest + 4*v)) = *((  int8_t*)(pSrc + v  )); break;
		case vtUShort: for(v=0; v<uVals; ++v) *((*float)(pDest + 4*v)) = *((uint16_t*)(pSrc + v*2)); break;
		case vtShort:  for(v=0; v<uVals; ++v) *((*float)(pDest + 4*v)) = *(( int16_t*)(pSrc + v*2)); break;
		default: return das_error(DASERR_ENC, ENCODER_SETUP_ERROR);
		}
		return DAS_OKAY;
	case vtInt:
		switch(vtBuf){
		case vtUbyte:  for(v=0; v<uVals; ++v) *((*int32_t)(pDest + 4*v)) = pSrc[v];                    break;
		case vtByte:   for(v=0; v<uVals; ++v) *((*int32_t)(pDest + 4*v)) = *((  int8_t*)(pSrc + v  )); break;
		case vtUShort: for(v=0; v<uVals; ++v) *((*int32_t)(pDest + 4*v)) = *((uint16_t*)(pSrc + v*2)); break;
		case vtShort:  for(v=0; v<uVals; ++v) *((*int32_t)(pDest + 4*v)) = *(( int16_t*)(pSrc + v*2)); break;
		default: return das_error(DASERR_ENC, ENCODER_SETUP_ERROR);	
		}
		return DAS_OKAY;
	case vtUInt:
		switch(vtBuf){
		case vtUbyte:  for(v=0; v<uVals; ++v) *((*uint32_t)(pDest + 4*v)) = pSrc[v];                    break;
		case vtUShort: for(v=0; v<uVals; ++v) *((*uint32_t)(pDest + 4*v)) = *((uint16_t*)(pSrc + v*2)); break;
		default: return das_error(DASERR_ENC, ENCODER_SETUP_ERROR);	
		}
		return DAS_OKAY;
	case vtShort:
		switch(vtBuf){
		case vtUbyte:  for(v=0; v<uVals; ++v) *((*int16_t)(pDest + 2*v)) = pSrc[v];                  break;
		case vtByte:   for(v=0; v<uVals; ++v) *((*int16_t)(pDest + 2*v)) = *((int8_t*)(pSrc + v  )); break;
		default: return das_error(DASERR_ENC, ENCODER_SETUP_ERROR);	
		}
		return DAS_OKAY;
	case vtUShort:
		switch(vtBuf){
		case vtUbyte:  for(v=0; v<uVals; ++v) *((*uint16_t)(pDest + 2*v)) = pSrc[v]; break;
		default: return das_error(DASERR_ENC, ENCODER_SETUP_ERROR);	
		}
		return DAS_OKAY;
	}
	return das_error(DASERR_ENC, ENCODER_SETUP_ERROR);	
}

/* ************************************************************************* */
/* Read helper */


#define _SWP2(p) val[0] = *(p+1); val[1] = *(p)
#define _SWP4(p) val[0] = *(p+3); val[1] = *(p+2); val[2] = *(p+1); val[3] = *(p)

static DasErrCode _swap_cast_read(
	ubyte* pDest, ubyte* pSrc, size_t uVals, das_val_type vtAry, das_val_type vtBuf
){
	size_t v;
	ubyte val[8];

	switch(vtAry){
	case vtDouble:
		switch(vtBuf){
		case vtUShort: for(v=0; v<uVals; ++v){ _SWP2(pSrc + v*2); *((*double)(pDest + 8*v)) = *((uint16_t*)val); } break;
		case vtShort:  for(v=0; v<uVals; ++v){ _SWP2(pSrc + v*2); *((*double)(pDest + 8*v)) = *(( int16_t*)val); } break;
		case vtUInt:   for(v=0; v<uVals; ++v){ _SWP4(pSrc + v*4); *((*double)(pDest + 8*v)) = *((uint32_t*)val); } break;
		case vtInt:    for(v=0; v<uVals; ++v){ _SWP4(pSrc + v*4); *((*double)(pDest + 8*v)) = *(( int32_t*)val); } break;
		case vtFloat:  for(v=0; v<uVals; ++v){ _SWP4(pSrc + v*4); *((*double)(pDest + 8*v)) = *((   float*)val); } break;
		default: return das_error(DASERR_ENC, ENCODER_SETUP_ERROR);
		}
		return DAS_OKAY;
	case vtLong:
		switch(vtBuf){
		case vtUShort: for(v=0; v<uVals; ++v){ _SWP2(pSrc + v*2); *((*int64_t)(pDest + 8*v)) = *((uint16_t*)val); } break;
		case vtShort:  for(v=0; v<uVals; ++v){ _SWP2(pSrc + v*2); *((*int64_t)(pDest + 8*v)) = *(( int16_t*)val); } break;
		case vtUInt:   for(v=0; v<uVals; ++v){ _SWP4(pSrc + v*4); *((*int64_t)(pDest + 8*v)) = *((uint32_t*)val); } break;
		case vtInt:    for(v=0; v<uVals; ++v){ _SWP4(pSrc + v*4); *((*int64_t)(pDest + 8*v)) = *(( int32_t*)val); } break;
		default: return das_error(DASERR_ENC, ENCODER_SETUP_ERROR);	
		}
		return DAS_OKAY;
	case vtULong:
		switch(vtBuf){
		case vtUShort: for(v=0; v<uVals; ++v){ _SWP2(pSrc + v*2); *((*uint64_t)(pDest + 8*v)) = *((uint16_t*)val); } break;
		case vtUInt:   for(v=0; v<uVals; ++v){ _SWP4(pSrc + v*4); *((*uint64_t)(pDest + 8*v)) = *((uint32_t*)val); } break;
		default: return das_error(DASERR_ENC, ENCODER_SETUP_ERROR);	
		}
		return DAS_OKAY;
	case vtFloat:
		switch(vtBuf){
		case vtUShort: for(v=0; v<uVals; ++v){ _SWP2(pSrc + v*2); *((*float)(pDest + 4*v)) = *((uint16_t*)val); } break;
		case vtShort:  for(v=0; v<uVals; ++v){ _SWP2(pSrc + v*2); *((*float)(pDest + 4*v)) = *(( int16_t*)val); } break;
		default: return das_error(DASERR_ENC, ENCODER_SETUP_ERROR);
		}
		return DAS_OKAY;
	case vtInt:
		switch(vtBuf){
		case vtUShort: for(v=0; v<uVals; ++v){ _SWP2(pSrc + v*2); *((*int32_t)(pDest + 4*v)) = *((uint16_t*)val); } break;
		case vtShort:  for(v=0; v<uVals; ++v){ _SWP2(pSrc + v*2); *((*int32_t)(pDest + 4*v)) = *(( int16_t*)val); } break;
		default: return das_error(DASERR_ENC, ENCODER_SETUP_ERROR);	
		}
		return DAS_OKAY;
	case vtUInt:
		if(vtBuf == vtUShort){
			for(v=0; v<uVals; ++v){ _SWP2(pSrc + v*2); *((*uint32_t)(pDest + 4*v)) = *((uint16_t*)val); } 
			return DAS_OKAY;
		}
		break;
	}

	return das_error(DASERR_ENC, ENCODER_SETUP_ERROR);	
}

/* The goal of this function is to read the expected number of values from
 * an upstream source */
int DasAryEnc_read(
	DasAryEnc* pThis, const ubyte* pBuf, size_t nBufLen, int nExpect, int* pRead
){
	if(uFlags & DASENC_VALID != DASENC_VALID)
		return -1 * das_error(DASERR_ENC, "Encoder is not initialized");

	if(uBufLen == 0) return DAS_OKAY;  /* Successfully do nothing */

	DasErrCode nErr = DAS_OKAY;

	size_t uVals = 0;

	if(!(pThis->uProc & DASENC_TEXT)){    /* .... Reading binary data  */

		uVals = uBufLen / pThis->nBufValSz;
		if((nExpect > 0) && (uVals > nExpect))
			uVals = (size_t)nExpect;

		ubyte* pWrite = NULL;

		switch(pThis->uProc){

		/* Easy mode, external data and internal array have the same value type */
		case DASENC_VALID:
			assert(pThis->nBufValSz == pThis->nAryValSz);	
			if(DasAry_append(pThis->pAry, pBuf, uVals) == NULL)
				return -1 * DASERR_ARRAY;
			break;

		/* Almost easy, only need to swap to get into internal storage */
		case DASENC_VALID|DASENC_SWAP:
			assert(pThis->nBufValSz == pThis->nAryValSz);	

			 /* Alloc space as fill, then write in swapped values */
			if((pWrite = DasAry_append(pThis->pAry, NULL, uVals)) == NULL)
				return -1 * DASERR_ARY;

			if((nRet = _swap_read(pWrite, pBuf, uVals, pThis->nBufValSz)) != DAS_OKAY)
				return -1 * nRet;
			break;

		/* Need to cast values to a larger type for storage */
		case DASENC_VALID|DASENC_CAST:

			if((pWrite = DasAry_append(pThis->pAry, NULL, uVals)) == NULL)
				return -1 * DASERR_ARY;

			if((nRet = _cast_read(pWrite, pBuf, uVals, pThis->vtAry, pThis->vtBuf)) != DAS_OKAY)
				return -1 * nRet;
			break

		/* Bigest binary change, swap and cast to a larger type for storage */
		case DASENC_VALID|DASENC_CAST|DASENC_SWAP:

			if((pWrite = DasAry_append(pThis->pAry, NULL, uVals)) == NULL)
				return -1 * DASERR_ARY;

			if((nRet = _swap_cast_read(pWrite, pBuf, uVals, pThis->vtAry, pThis->vtBuf)) != DAS_OKAY)
				return -1 * nRet;
			break

		default: 
			return -1 * das_error(DASERR_ENC, ENCODER_SETUP_ERROR);
		}

		if(pRead)
			*pRead = uVals;

		return uBufLen - (uVals * (pThis->nBufValSz));  /* Return count of unused bytes */
	}

	if(pThis->vtBuf != vtText){
		return -1 * das_error(DASERR_ENC, "Expected a text type for the external buffer");
	}

	/* Text parsing */
	char sValue[1024] = {'\0'};
	size_t uValSz, uToWrite, uGot = 0;
	ubyte aValue[sizeof(das_time)];
	uVals = 0;

	const char* pGet = pBuf;
	while((uGot < uBufLen)){
		if((nExpect > 0) && (uVals == nExpect))
			break;

   	/* Find a sep or the end of the buffer */
		while(*pGet == cSep || *pGet == '\0' || (!cSep && isspace(*pGet)) ){
			++pGet;
			++uGot;
			if(uGot == uBufLen)
				goto PARSE_DONE;
		}

		if(uValSz > 0)
			memset(sValue, 0, uValSz);
		uValSz = 0;

		while(*pGet != cSep && *pGet != '\0' && (cSep || !isspace(*pGet)) ){
			sValue[uValSz] = *pGet;
			++pGet;
			++uGot;
			++uValSz;
			if((uGot == uBufLen)||(uValSz > 254))
				break;
		}

		if(uValSz > 0){

			if(pThis->uProc & DASENC_PARSE){
				nErr = das_value_fromStr(aValue, sizeof(das_time), pThis->vtAry, sValue);
				if(nErr != DAS_OKAY)
					return -1 * nErr;
				if(!DasAry_append(pThis->pAry, aValue, 1))
					return -1 * DASERR_ARY;
			}
			else{
				/* No parsing needed, just run in the value.  Typically there are 
				   two versions of this.

				   1) We just append characters then call markEnd to roll the next
				      to last index.

				   2) We append exactly the number of characters to make up the
				      last index.
				*/
				if(pThis->uProc & DASENC_WRAP){
					uToWrite = pThis->uProc & DASENC_NULLTERM ? uValSz + 1 : uValSz;

					if(DasAry_append(pThis->pAry, sValue, uToWrite) == NULL);
						return -1 * DASERR_ARY;

					DasAry_markEnd(pThis->pAry, DasAry_rank(pThis->pAry) - 1);
				}
				else{
					uToWrite = uValSz > pThis->uMaxString ? pThis->uMaxStr : uValSz;

					if( (pWrite = DasAry_append(pThis->pAry, sValue, uToWrite)) == NULL);
						return -1 * DASERR_ARY;

					/* Fill pad if we need to */
					if(uValSz < pThis->uMaxString){
						uToWrite = pThis->uMaxString - uValSz;
						if( DasAry_append(pThis->pAry, NULL, uToWrite) );
							return -1 * DASERR_ARY;
					}
				}	
			}
			
			++uVals;
		}
	}

PARSE_DONE:
	if(pRead)
		*pRead = uVals;

	return uBufLen - (pGet - pBuf);
}

