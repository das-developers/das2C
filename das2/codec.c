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

#include "codec.h"
#include "value.h"
#include "log.h"

/* Operations flags */
/* (hdr) DASENC_VALID   0x0001 / * If not 1, not a valid encoder */
#define DASENC_SWAP     0x0002 /* If set bytes must be swapped prior to IO */
#define DASENC_CAST     0x0004 /* If set bytes must be transformed prior to IO */
#define DASENC_TEXT     0x0008 /* Input is text */
#define DASENC_PARSE    0x0010 /* Input is text that should be parsed to a value */
#define DASENC_VARSZ    0x0020 /* Input is varible size text items */

/* Used in the big switch, ignores the valid bit since that's assumed by then */
#define DASENC_MAJ_MASK 0x00FE /* Everyting concerned with the input buffer */

/* Items used after the big switch */

#define DASENC_NULLTERM 0x0200 /* Input is text that should be null terminated */
#define DASENC_WRAP     0x0400 /* Wrap last dim reading a string value */


#define ENCODER_SETUP_ERROR "Logic error in encoder setup"

/* Perform various checks to see if this is even possible */ 
DasErrCode DasCodec_init(
   DasCodec* pThis, DasAry* pAry, const char* sSemantic, const char* sEncType,
   int16_t nSzEach, ubyte cSep, das_units epoch
){
	memset(pThis, 0, sizeof(DasCodec));  
	pThis->cSep = cSep;
	pThis->pAry = pAry;
	pThis->nBufValSz = nSzEach;
	assert(pAry != NULL);

	bool bDateTime = false;

	/* Don't let the array delete itself out from under us*/
	inc_DasAry(pThis->pAry);

	/* Makes the code below shorter */
	das_val_type vtAry = DasAry_valType( pThis->pAry ); 

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
		default:  goto UNSUPPORTED;
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
		default:  goto UNSUPPORTED;
		}
		bIntegral = true;
	}
	else if(strcmp(sEncType, "BEuint") == 0){
		switch(nSzEach){
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
		switch(nSzEach){
		case 8: pThis->vtBuf = vtULong;  break;
		case 4: pThis->vtBuf = vtUInt;   break;
		case 2: pThis->vtBuf = vtUShort; break;
		case 1: pThis->vtBuf = vtUByte;  break;
		default:  goto UNSUPPORTED;
		}
		bIntegral = true;
	}
	else if(strcmp(sEncType, "BEreal") == 0){
		switch(nSzEach){
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
		switch(nSzEach){
		case 8: pThis->vtBuf = vtDouble;  break;
		case 4: pThis->vtBuf = vtFloat;   break;
		default:  goto UNSUPPORTED;
		}
		bIntegral = true;
	}
	else if(strcmp(sEncType, "byte") == 0){
		if(nSzEach != 1) goto UNSUPPORTED;
		pThis->vtBuf = vtByte;
		bIntegral = true;
	}
	else if(strcmp(sEncType, "ubyte") == 0){
		if(nSzEach != 1) goto UNSUPPORTED;
		pThis->vtBuf = vtUByte;
		bIntegral = true;
	}

	if(bIntegral){
		if(das_vt_size(pThis->vtBuf) > das_vt_size(vtAry))
			goto UNSUPPORTED;

		/* If the array value type is floating point then it must 
		   and the buffer type is integer, then it must be wider then
		   the integers */
		if(das_vt_isint(pThis->vtBuf) && das_vt_isreal(vtAry)){
			if(das_vt_size(vtAry) == das_vt_size(pThis->vtBuf))
				goto UNSUPPORTED;
		}

		/* I need to cast values up to a larger size, flag that */
		if(das_vt_size(pThis->vtBuf) != das_vt_size(vtAry))
			pThis->uProc |= DASENC_CAST;

		/* Temporary: Remind myself to call DasAry_markEnd() when writing
		   non-string variable length items */
		if(nLastIdxSz == DASIDX_RAGGED){
			daslog_info("Hi Developer: Variable length last index detected, "
			            "make sure you call DasAry_markEnd() after packet reads.");
		}
		goto SUPPORTED;
	}

	if(strcmp(sEncType, "utf8") != 0){
		goto UNSUPPORTED;
	}
	
	pThis->vtBuf = vtText;
	pThis->uProc |= DASENC_TEXT;

	/* Deal with the text types */
	if(strcmp(sSemantic, "bool") == 0){
		return das_error(DASERR_NOTIMP, "TODO: Add parsing for 'true', 'false' etc.");
	}
	else if((strcmp(sSemantic, "int") == 0)||(strcmp(sSemantic, "real") == 0)){
		pThis->uProc |= DASENC_PARSE;
	}
	else if((strcmp(sSemantic, "datetime") == 0)){
		bDateTime = true;
		pThis->uProc |= DASENC_PARSE;

		/* If we're storing this as a datetime structure it's covered, if 
		   we need to convert to something else the units are needed */
		if(vtAry != vtTime){
			
			if( (epoch == NULL) || (! Units_haveCalRep(epoch) ) )
				goto UNSUPPORTED;
		
			/* Check that the array element size is big enough for the units in
			   question */
			if((epoch == UNIT_TT2000)&&(vtAry != vtLong)&&(vtAry != vtDouble))
				goto UNSUPPORTED;
			else
				if((vtAry != vtDouble)&&(vtAry != vtFloat))
					goto UNSUPPORTED;

			pThis->timeUnits = epoch;  /* In addition to parsing, we have to convert */
		}
	}
	else if(strcmp(sSemantic, "string") == 0){

		if(vtAry != vtUByte)   /* Expect uByte storage for strings not */
      	goto UNSUPPORTED;          /* vtText as there is no external place */
		                              /* to put the string data */

		if(DasAry_getUsage(pThis->pAry) & D2ARY_AS_STRING){
			pThis->uProc |= DASENC_NULLTERM;
		}

		/* If storing string data, we need to see if the last index of the array is 
		   big enough */
		if((nLastIdxSz != DASIDX_RAGGED)&&(nLastIdxSz < nSzEach))
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
	if(bDateTime)
		return das_error(DASERR_ENC, "Can not encode/decode datetime data from buffers "
			"with encoding '%s' for items of %hs bytes each to/from an array of "
			" '%s' type elements with time units of '%s'", sEncType, nSzEach, 
			das_vt_toStr(vtAry), epoch == NULL ? "none" : Units_toStr(epoch)
		);
	else
		return das_error(DASERR_ENC, "Can not encode/decode '%s' data from buffers "
			"with encoding '%s' for items of %hs bytes each to/from an array of "
			" '%s' type elements", sSemantic, sEncType, nSzEach, das_vt_toStr(vtAry)
		);
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
	int nBytesRead; 

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

static int _var_text_item_sz(const char* pBuf, int nBufLen, char cSep)
{
	/* Break the value on a null, a seperator, or if the separator is
	   null, then on space characters */
	int nSize = 0;
	while( 
		(nBufLen > 0)&&
		( *pBuf == cSep||*pBuf == '\0'||(!cSep && isspace(*pBuf)) ) 
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

	das_val_type vtAry = DasAry_valType( pThis->pAry );
	bool bParse = ((pThis->uProc & DASENC_PARSE) != 0);
	int nRet;
	char cSep = pThis->cSep;
	const char* pRead = (const char*)pBuf;
	int nLeft = nBufLen;

	char sValue[128] = {'\0'}; // small vector assumption, use pThis->pOverflow if needed
	char* pValue = NULL;
	
	int nValSz = 0;
	*pValsDidRead = 0;

	/* The value reading loop */
	while( (nLeft > 0)&&( (nValsToRead < 0) || (*pValsDidRead < nValsToRead) ) ){
		
   	/* 1. Eat any proceeding separators */
		while( (nLeft > 0)&&( *pRead == cSep||*pRead == '\0'||(!cSep && isspace(*pRead)) ) ){
			++pRead;
			--nLeft;
			if(nLeft == 0)
				goto PARSE_DONE;
		}

		/* 2. Get the size of the value */
		nValSz = _var_text_item_sz(pRead, nLeft, cSep);
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

		/* 4. Convert and save, or just save, with optional null and wrap */
		if(bParse){
			nRet = _convert_n_store_text(pThis, pValue);
			ubyte aValue[sizeof(das_time)];
			nRet = das_value_fromStr(aValue, sizeof(das_time), vtAry, pValue);
			if(nRet != DAS_OKAY)
				return -1 * nRet;
			if(!DasAry_append(pThis->pAry, aValue, 1))
				return -1 * DASERR_ARRAY;
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
	case 0:
		assert(pThis->nBufValSz == pThis->nAryValSz);
		assert(pThis->nBufValSz > 0);
		assert(nValsToRead > 0);

		if((pWrite = DasAry_append(pThis->pAry, pBuf, nValsToRead)) == NULL)
			return -1 * DASERR_ARRAY;
		
		nBytesRead = nValsToRead * (pThis->nBufValSz);
		break;


	/* Almost easy, only need to swap to get into internal storage */
	case DASENC_SWAP:
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


	/* Need to cast values to a larger type for storage */
	case DASENC_CAST:
		assert(nValsToRead > 0);
		assert(pThis->nBufValSz > 0);

		ubyte* pWrite = NULL;
		if((pWrite = DasAry_append(pThis->pAry, NULL, nValsToRead)) == NULL)
			return -1 * DASERR_ARRAY;

		if((nRet = _cast_read(pWrite, pBuf, nValsToRead, vtAry, pThis->vtBuf)) != DAS_OKAY)
			return -1 * nRet;
		
		nBytesRead = nValsToRead * (pThis->nBufValSz);
		break;


	/* Bigest binary change, swap and cast to a larger type for storage */
	case DASENC_CAST|DASENC_SWAP:
		assert(nValsToRead > 0);
		assert(pThis->nBufValSz > 0);

		if((pWrite = DasAry_append(pThis->pAry, NULL, nValsToRead)) == NULL)
			return -1 * DASERR_ARRAY;

		if((nRet = _swap_cast_read(pWrite, pBuf, nValsToRead, vtAry, pThis->vtBuf)) != DAS_OKAY)
			return -1 * nRet;
		
		nBytesRead = nValsToRead * (pThis->nBufValSz);
		break;


	/* Easy, just run in the text, don't markEnd */
	case DASENC_TEXT:
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
	case DASENC_TEXT|DASENC_PARSE:
		assert(nValsToRead > 0);
		assert(pThis->nBufValSz > 0);

		nBytesRead = _fixed_text_convert(pThis, pBuf, pThis->nBufValSz, nValsToRead);
		if(nBytesRead < 0 )
			return nBytesRead;

		break;

	/* ************** End of Fixed Length Item Cases ********************* */

	/* Search for the end, run and data, markEnd if array is variable size */
	/* or search of the end, parse, then run in the data */
	case DASENC_TEXT|DASENC_VARSZ:
	case DASENC_TEXT|DASENC_PARSE|DASENC_VARSZ:	
		int nValsDidRead = 0;
		nBytesRead = _var_text_read(pThis, pBuf, nBufLen, nValsToRead, &nValsDidRead);
		nValsToRead = nValsDidRead;

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
