/* Copyright (C) 2015-2017 Chris Piker <chris-piker@uiowa.edu>
 *
 * This file is part of das2C, the Core Das2 C Library.
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

#include <stdio.h>
#include <string.h>
#include <math.h>

#include "das1.h"

#include "encoding.h"
#include "value.h"
#include "buffer.h"

/* ************************************************************************* */
/* FILL */
double getDas2Fill() {
	return DAS_FILL_VALUE;
}

int isDas2Fill( double value ) {
	double fill= getDas2Fill();
	return fabs((fill-value)/fill)<0.00001;
}

/* ************************************************************************* */
/* Construction/Destruction */

DasEncoding* new_DasEncoding(int nCat, int nWidth, const char* sFmt){
	
	if(nCat != DAS2DT_BE_REAL && nCat != DAS2DT_LE_REAL &&
		nCat != DAS2DT_ASCII   && nCat != DAS2DT_TIME ){
		das_error(14, "Encoding category 0x%04X is unknown");
		return NULL;
	}
	
	if(nCat == DAS2DT_BE_REAL || nCat == DAS2DT_LE_REAL){
		if(nWidth != 4 && nWidth != 8){
			das_error(14, "%d-byte binary reals are not supported", nWidth);
			return NULL;
		}
	}

	if(nWidth < 2 || nWidth > 127){
		das_error(14, "Error in encoding type %s, valid field width range "
		              "is 2 to 127 characters", nWidth);
		return NULL;
	}

	/* The default formatters have more restrictions on the field width
	   but don't enforce those here as the lib user may set a custom 
		formatter that handles it */
			
	DasEncoding* pThis = (DasEncoding*)calloc(1, sizeof(DasEncoding));
	pThis->nCat = nCat;
	pThis->nWidth = nWidth;
	if(sFmt != NULL){
		if(strlen(sFmt) >= DASENC_FMT_LEN){
			das_error(14, "Format string is longer than %d bytes",DASENC_FMT_LEN-1);
			return NULL;
		}
		strncpy(pThis->sFmt, sFmt, DASENC_FMT_LEN-1);
	}
	
	if( DasEnc_toStr(pThis, pThis->sType, DASENC_TYPE_LEN) != 0) return NULL;
	
	return pThis;
}

DasEncoding* new_DasEncoding_str(const char* sType)
{	
	char sBuf[4] = {'\0'};
	
	if(sType == NULL)
		das_error(5, "Null pointer in dasEncoding_fromString");
	
	DasEncoding* pThis = (DasEncoding*)calloc(1, sizeof(DasEncoding));
	
	strncpy(pThis->sType, sType, DASENC_TYPE_LEN - 1);
	
	/* Real encodings*/
	if((strcmp(sType, "sun_real8" ) == 0)||(strcmp(sType, "double") == 0)){
		pThis->nCat = DAS2DT_BE_REAL;
		pThis->nWidth = 8;
		return pThis;
	}
	if(strcmp(sType, "little_endian_real8")==0){ 
		pThis->nCat = DAS2DT_LE_REAL;
		pThis->nWidth = 8;
		return pThis;
	}
	 
	if((strcmp(sType, "float") == 0 )||(strcmp(sType, "sun_real4") == 0)){
		pThis->nCat = DAS2DT_BE_REAL;
		pThis->nWidth = 4;
		return pThis;
	}
	if(strcmp(sType,"little_endian_real4")==0){
		pThis->nCat = DAS2DT_LE_REAL;
		pThis->nWidth = 4;
		return pThis;
	}
	
	/* Integer encodings */
	char w = '\0';
	if(strncmp(sType, "little_endian_int", 17) == 0){
		pThis->nCat = DAS2DT_LE_INT;
		w = sType[17];
	}
	if(strncmp(sType, "big_endian_int", 14) == 0){
		pThis->nCat = DAS2DT_BE_INT;
		w = sType[14];
	}
	if(strncmp(sType, "little_endian_uint", 18) == 0){
		pThis->nCat = DAS2DT_LE_UINT;
		w = sType[18];
	}
	if(strncmp(sType, "big_endian_uint", 15) == 0){
		pThis->nCat = DAS2DT_BE_UINT;
		w = sType[15];
	}
	if(w){
		if((w != '1')&&(w != '2')&&(w != '4')&&(w != '8')){
			pThis->nWidth = w - '0';  /* Relies on C and UTF-8 encoding scheme */
		}
		else{
			das_error(14, "Error parsing encoding type '%s'", sType);
			free(pThis);
			return NULL;
		}
	}
	
	
	int i = 0;
	int nOff = 0;
	if(strncmp(sType, "ascii", 5) == 0){
		pThis->nCat = DAS2DT_ASCII;
		nOff = 5;
	}
	if(strncmp(sType, "time", 4) == 0){
		pThis->nCat = DAS2DT_TIME;
		nOff = 4;
	}
	
	for(i = 0; i < 3; i++){
		if(sType[i+nOff] == '\0') break;
		sBuf[i] = sType[i+nOff];
	}
	if(sscanf(sBuf, "%d", &(pThis->nWidth)) != 1){
		das_error(14, "Error parsing encoding type '%s'", sType);
		free(pThis);
		return NULL;
	}
	
	if(pThis->nWidth < 2 || pThis->nWidth > 127){
		free(pThis);
		das_error(14, "Error in encoding type %s, valid field width range "
		              "is 2 to 127 characters", sType);
	}
	    
 	return pThis;
}

/* Copy Constructor */
DasEncoding* DasEnc_copy(DasEncoding* pThis)
{
	DasEncoding* pOther = (DasEncoding*)calloc(1, sizeof(DasEncoding));
	
	/* DasEncoding has no externally referenced memory */
	memcpy(pOther, pThis, sizeof(DasEncoding));
	
	return pOther;
}

/* ************************************************************************* */
/* Check equality */
bool DasEnc_equals(const DasEncoding* pOne, const DasEncoding* pTwo)
{
	if((pOne == NULL)&&(pTwo == NULL)) return true;
	
	if((pOne != NULL)&&(pTwo == NULL)) return false;
	if((pOne == NULL)&&(pTwo != NULL)) return false;
	
	if(pOne->nCat != pTwo->nCat) return false;
	if(pOne->nWidth != pTwo->nWidth) return false;
	if(strcmp(pOne->sFmt, pTwo->sFmt) != 0) return false;
	return true;
}

/* ************************************************************************* */
/* Get Info */
unsigned int DasEnc_hash(const DasEncoding* pThis)
{
	unsigned int uHash = 0;
	uHash = pThis->nCat & 0xFF;
	uHash |= (pThis->nWidth << 8) & 0xFF00;
	return uHash;
}

/* ************************************************************************* */
/* Setting String Formatters */

void DasEnc_setAsciiFormat(
	DasEncoding* pThis, const char* sValFmt, int nFmtWidth
){
	if(pThis->nCat != DAS2DT_ASCII)
		das_error(14, "Encoding %s is not a general ASCII type", pThis->sType); 
	
	pThis->nWidth = nFmtWidth + 1;
	strncpy(pThis->sFmt, sValFmt, DASENC_FMT_LEN - 1);	
}

void DasEnc_setTimeFormat(
	DasEncoding* pThis, const char* sValFmt, int nFmtWidth
){
	/* Yea, yea same code as above.  But these are kept separate for library
	   design purposes.  I want the end user to remember that time format
		strings are very different from general value format strings. -cwp */
	
	if(pThis->nCat != DAS2DT_TIME)
		das_error(14, "Encoding %s is not a ASCII Time type", pThis->sType); 
	
	pThis->nWidth = nFmtWidth + 1;
	strncpy(pThis->sFmt, sValFmt, DASENC_FMT_LEN - 1);	
}

/* Guess a good default ascii format string based off the encoding width */
void _DasEnc_setDefaultAsciiFmt(DasEncoding* pThis)
{
	if(pThis->nCat != DAS2DT_ASCII)
		das_error(18, "Plane data encoding is not general ASCII");
	
	if((pThis->nWidth < 9)||(pThis->nWidth > 24))
		das_error(14, "Use DasEnc_setAsciiFormat to output general ASCII "
				          "values in less than 9 characters or more than 24 "
				          "characters");
	
	snprintf(pThis->sFmt, 47, "%%%d.%de", pThis->nWidth - 1, pThis->nWidth - 8);
}

/* Guess a good default TIME format string based off the encoding width. */
/* Makes use of the the fact that var-args functions ignore un-needed   */
/* input parameters at the end.  This way we can vary the format string */
/* but not the number of arguments used.                                */
void _DasEnc_setDefaultTimeFmt(DasEncoding* pThis)
{
	if(pThis->nCat != DAS2DT_TIME)
		das_error(14, "Encoding is not ASCII Time strings");
		
	/* At least expect the width to be 5 long, this gets the year plus a */
	/* separator */
	if((pThis->nWidth < 5)||(pThis->nWidth > 31))
		das_error(14, "Use DasEnc_setTimeFormat to output ASCII "
				          "time values in less than 5 characters or more than 31 "
				          "characters");
	
	/* The assumption in this guesser is that people usually want whole */
	/* fields, and some old code likes to throw Z's on the end of all times */
	/* to indicate UTC (i.e. Zulu time) */
	
	switch(pThis->nWidth){  /* Remember, width includes the separator */
	
	/* Year only */
	case 5: strncpy(pThis->sFmt, "%04d", DASENC_FMT_LEN-1); break;
	case 6: strncpy(pThis->sFmt, "%04d ", DASENC_FMT_LEN-1); break;
	case 7: strncpy(pThis->sFmt, "%04d  ", DASENC_FMT_LEN-1); break;
	
	/* Year and Month */
	case 8: strncpy(pThis->sFmt,  "%04d-%02d",DASENC_FMT_LEN-1); break;
	case 9: strncpy(pThis->sFmt,  "%04d-%02d ",DASENC_FMT_LEN-1); break;
	case 10: strncpy(pThis->sFmt, "%04d-%02d  ",DASENC_FMT_LEN-1); break;
	
	/* Year/Month/Day of Month */
	case 11: strncpy(pThis->sFmt, "%04d-%02d-%02d",DASENC_FMT_LEN-1); break;
	case 12: strncpy(pThis->sFmt, "%04d-%02d-%02d ",DASENC_FMT_LEN-1); break;
	case 13: strncpy(pThis->sFmt, "%04d-%02d-%02d  ",DASENC_FMT_LEN-1); break;
	
	/* Date + Hour */
	case 14: strncpy(pThis->sFmt, "%04d-%02d-%02dT%02d",DASENC_FMT_LEN-1); break;
	case 15: strncpy(pThis->sFmt, "%04d-%02d-%02dT%02d ",DASENC_FMT_LEN-1); break;
	case 16: strncpy(pThis->sFmt, "%04d-%02d-%02dT%02d  ",DASENC_FMT_LEN-1); break;
	
	/* Date + Hour:min */
	case 17: strncpy(pThis->sFmt, "%04d-%02d-%02dT%02d:%02d",DASENC_FMT_LEN-1); break;
	case 18: strncpy(pThis->sFmt, "%04d-%02d-%02dT%02d:%02d ",DASENC_FMT_LEN-1); break;
	case 19: strncpy(pThis->sFmt, "%04d-%02d-%02dT%02d:%02d  ",DASENC_FMT_LEN-1); break;
	
	/* Date + Hour:min:sec */
	case 20: strncpy(pThis->sFmt, "%04d-%02d-%02dT%02d:%02d:%02.0f",DASENC_FMT_LEN-1); break;
	case 21: strncpy(pThis->sFmt, "%04d-%02d-%02dT%02d:%02d:%02.0f ",DASENC_FMT_LEN-1); break;
	
	/* Date + hour:min:sec + frac seconds */
	case 22: strncpy(pThis->sFmt, "%04d-%02d-%02dT%02d:%02d:%04.1f",DASENC_FMT_LEN-1); break;
	case 23: strncpy(pThis->sFmt, "%04d-%02d-%02dT%02d:%02d:%05.2f",DASENC_FMT_LEN-1); break;
	case 24: strncpy(pThis->sFmt, "%04d-%02d-%02dT%02d:%02d:%06.3f",DASENC_FMT_LEN-1); break;
	case 25: strncpy(pThis->sFmt, "%04d-%02d-%02dT%02d:%02d:%06.3f ",DASENC_FMT_LEN-1); break;
	case 26: strncpy(pThis->sFmt, "%04d-%02d-%02dT%02d:%02d:%06.3f  ",DASENC_FMT_LEN-1); break;
	case 27: strncpy(pThis->sFmt, "%04d-%02d-%02dT%02d:%02d:%09.6f",DASENC_FMT_LEN-1); break;
	case 28: strncpy(pThis->sFmt, "%04d-%02d-%02dT%02d:%02d:%09.6f ",DASENC_FMT_LEN-1); break;
	case 29: strncpy(pThis->sFmt, "%04d-%02d-%02dT%02d:%02d:%09.6f  ",DASENC_FMT_LEN-1); break;
	case 30: strncpy(pThis->sFmt, "%04d-%02d-%02dT%02d:%02d:%012.9f",DASENC_FMT_LEN-1); break;
	case 31: strncpy(pThis->sFmt, "%04d-%02d-%02dT%02d:%02d:%012.9f ",DASENC_FMT_LEN-1); break;
	
	/* If nano-seconds isn't good enough, revise in the future */
	}
}

/* ************************************************************************* */
/* Info and Config changes */

DasErrCode DasEnc_toStr(DasEncoding* pThis, char* sType, size_t nLen )
{
	const char* _sType = NULL;
		
	/* Try to hit one of the set width types first */	
	int nHash = DasEnc_hash(pThis);
	
	switch(nHash){
	case DAS2DT_BE_REAL_8: _sType = "sun_real8"; break;
	case DAS2DT_LE_REAL_8: _sType = "little_endian_real8"; break;
	case DAS2DT_BE_REAL_4: _sType = "sun_real4"; break;
	case DAS2DT_LE_REAL_4: _sType = "little_endian_real4"; break;
	}
	
	if(_sType != NULL){
		if(strlen(_sType) >= nLen){
			return das_error(5, "Buffer is too small to receive encoding string "
					          "'%s'", _sType);
		}
		strcpy(sType, _sType);
		return 0;
	}
	
	/* Try to encode a var-width type */
	int nRet = 0;

	switch(pThis->nCat){
	case DAS2DT_ASCII: nRet = snprintf(sType, nLen-1, "ascii%d", pThis->nWidth);break;
	case DAS2DT_TIME:  nRet = snprintf(sType, nLen-1, "time%d", pThis->nWidth); break;
	default:
		return das_error(14, "Value Encoding category %d is unknown", pThis->nCat);
	}
	
	if(nRet < 0)
		return das_error(14, "Couldn't convert DasEncoding 0x%X04 to a string", nHash);
	
	if(nRet >= nLen - 1)
		return das_error(14, "Buffer too small to receive DasEncoding string", nHash);
	
	return 0;
}

/* ************************************************************************* */
/* Encoding */


DasErrCode _writePacketMsb2(DasBuf* pBuf, void* pData)
{
#ifdef HOST_IS_LSB_FIRST
	unsigned char cdata[4];
	cdata[0] = *(((unsigned char*)pData) + 1);
	cdata[1] = *((unsigned char*)pData);
	
	return DasBuf_write(pBuf, cdata, 2);
#else
	return DasBuf_write(pBuf, pData, 2);
#endif
}

DasErrCode _writePacketLsb2(DasBuf* pBuf, void* pData)
{
#ifdef HOST_IS_LSB_FIRST
	return DasBuf_write(pBuf, pData, 2);
#else
	unsigned char cdata[4];
	cdata[0] = *(((unsigned char*)pData) + 1);
	cdata[1] = *((unsigned char*)pData);
	
	return DasBuf_write(pBuf, cdata, 2);
#endif
}


DasErrCode _writePacketMsb4(DasBuf* pBuf, void* pData)
{
#ifdef HOST_IS_LSB_FIRST
	unsigned char cdata[4];
	cdata[0] = *(((unsigned char*)pData) + 3);
	cdata[1] = *(((unsigned char*)pData) + 2);
	cdata[2] = *(((unsigned char*)pData) + 1);
	cdata[3] = *((unsigned char*)pData);
	
	return DasBuf_write(pBuf, cdata, 4);
#else
	return DasBuf_write(pBuf, pData, 4);
#endif
}

DasErrCode _writePacketLsb4(DasBuf* pBuf, void* pData)
{
#ifdef HOST_IS_LSB_FIRST
	return DasBuf_write(pBuf, pData, 4);
#else
	unsigned char cdata[4];
	cdata[0] = *(((unsigned char*)pData) + 3);
	cdata[1] = *(((unsigned char*)pData) + 2);
	cdata[2] = *(((unsigned char*)pData) + 1);
	cdata[3] = *((unsigned char*)pData);
	
	return DasBuf_write(pBuf, cdata, 4);
#endif
}

DasErrCode _writePacketMsb8(DasBuf* pBuf, void* pData)
{
#ifdef HOST_IS_LSB_FIRST
	unsigned char cdata[8];
	cdata[0] = *(((unsigned char*)pData) + 7);
	cdata[1] = *(((unsigned char*)pData) + 6);
	cdata[2] = *(((unsigned char*)pData) + 5);
	cdata[3] = *(((unsigned char*)pData) + 4);
	cdata[4] = *(((unsigned char*)pData) + 3);
	cdata[5] = *(((unsigned char*)pData) + 2);
	cdata[6] = *(((unsigned char*)pData) + 1);
	cdata[7] = *((unsigned char*)pData);
	
	return DasBuf_write(pBuf, cdata, 8);
#else
	return DasBuf_write(pBuf, pData, 8);
#endif
}

DasErrCode _writePacketLsb8(DasBuf* pBuf, void* pData)
{
#ifdef HOST_IS_LSB_FIRST
	return DasBuf_write(pBuf, pData, 8);
#else
	unsigned char cdata[8];
	cdata[0] = *(((unsigned char*)pData) + 7);
	cdata[1] = *(((unsigned char*)pData) + 6);
	cdata[2] = *(((unsigned char*)pData) + 5);
	cdata[3] = *(((unsigned char*)pData) + 4);
	cdata[4] = *(((unsigned char*)pData) + 3);
	cdata[5] = *(((unsigned char*)pData) + 2);
	cdata[6] = *(((unsigned char*)pData) + 1);
	cdata[7] = *((unsigned char*)pData);
	
	return DasBuf_write(pBuf, cdata, 8);
#endif
}

DasErrCode _encodeAsciiValue(DasEncoding* pThis, DasBuf* pBuf, double data)
{	
	int nExpect = pThis->nWidth - 1;
	
	/* Select a default output format if needed */
	if(pThis->sFmt[0] == '\0') _DasEnc_setDefaultAsciiFmt(pThis);
	
	size_t uPosBeg = DasBuf_written(pBuf);
	int nRet = DasBuf_printf(pBuf, pThis->sFmt, data);
	if(nRet != 0) return nRet;
	size_t uPosEnd = DasBuf_written(pBuf);
	
	if(nExpect != uPosEnd - uPosBeg)
		return das_error(
			14, "Output value '%s' using format '%s' for encoding '%s' occupied %zu "
			"bytes, expected %d", pBuf->sBuf + uPosBeg, pThis->sFmt, pThis->sType, 
			uPosEnd - uPosBeg, nExpect
		);
	
	return 0;
}

DasErrCode _encodeTimeValue(
	DasEncoding* pThis, DasBuf* pBuf, double data, das_units units
){

	das_time dt = {0};
	if(data != DAS_FILL_VALUE)
		Units_convertToDt(&dt, data, units);
	else
		dt_set(&dt, 1, 1, 1, 1, 0, 0, 0.0);
	
	int nExpect = pThis->nWidth - 1;
	
	/* Select a default output format if needed */
	if(pThis->sFmt[0] == '\0') _DasEnc_setDefaultTimeFmt(pThis);
	
	size_t uPosBeg = DasBuf_written(pBuf);
	int nRet = DasBuf_printf(pBuf, pThis->sFmt, dt.year, dt.month, dt.mday, 
			                   dt.hour, dt.minute, dt.second);
	if(nRet != 0) return nRet;
	size_t uPosEnd = DasBuf_written(pBuf);
	
	if(nExpect != uPosEnd - uPosBeg){
		return das_error(14, "Output value '%s' for encoding %s occupied %d "
				            "bytes, expected %d", pBuf->sBuf + uPosBeg, pThis->sType, 
		                  uPosEnd - uPosBeg, nExpect);
	}
	return 0;
}


DasErrCode DasEnc_write(
	DasEncoding* pThis, DasBuf* pBuf, double value, das_units units
){
	int nHash = DasEnc_hash(pThis);
	float fTmp;
	
	switch(nHash){
	case DAS2DT_BE_REAL_4: 
		fTmp = value;
		return _writePacketMsb4(pBuf, &fTmp);
	case DAS2DT_LE_REAL_4: 
		fTmp = value;
		return _writePacketLsb4(pBuf, &fTmp);
	case DAS2DT_BE_REAL_8: return _writePacketMsb8(pBuf, &value);
	case DAS2DT_LE_REAL_8: return _writePacketLsb8(pBuf, &value);
	}
	
	/* Okay, must be ascii or time */
	if(pThis->nCat == DAS2DT_ASCII)
		return _encodeAsciiValue(pThis, pBuf, value);
	
	if(pThis->nCat == DAS2DT_TIME)
		return _encodeTimeValue(pThis, pBuf, value, units);
	
	return das_error(14, "Don't know how to encode values to format %s", 
			          pThis->sType);
}

/* ************************************************************************* */
/* Decoding */

DasErrCode DasEnc_read(
	const DasEncoding* pThis, DasBuf* pBuf, das_units units, double* pOut
){
	*pOut = DAS_FILL_VALUE;
	
	char sBuf[128] = {'\0'};  /* Max field width is 127 chars so we're good */
	int nHash = DasEnc_hash(pThis);
	
	if( DasBuf_read(pBuf, sBuf, pThis->nWidth) != pThis->nWidth)
		return das_error(14, "Input buffer ends in the middle of a value");
	
	int i = 0;
	const float* fbuf = NULL;
	const double* dbuf = NULL;
	unsigned char bSwap[8] = {'\0'};
	
	switch(nHash){
	case DAS2DT_BE_REAL_4:
	case DAS2DT_LE_REAL_4:
		if(nHash != DAS2DT_FLOAT){
			/* Native float is not the same as the dataType, so byte swap */
			for(i=0; i<4; i++) 
				bSwap[i] = sBuf[3 - i];
			fbuf = (float*) bSwap;
		}
		else{
			fbuf= (float*) sBuf;
		}
		*pOut = (double) (*fbuf);		
		return 0;
		
	case DAS2DT_BE_REAL_8: 
	case DAS2DT_LE_REAL_8: 
		if( DAS2DT_DOUBLE != nHash){
			/* Native double is not the same as the dataType, so byte swap */
			for(i=0; i<8; i++) 
				bSwap[i] = sBuf[7 - i];
			dbuf = ( double *)bSwap;
		}
		else{
			dbuf= (double*)sBuf;
		}
		*pOut = *dbuf;
		return 0;
	}
	
	if(pThis->nCat == DAS2DT_ASCII){
		sscanf(sBuf, "%lf", pOut);
		return 0;
	}
	
	das_time dt = {0};
	if(pThis->nCat == DAS2DT_TIME){
		/* String parsing can be persnicity, copy over to a null terminated buffer */
		if(pThis->nWidth > 63)
			return das_error(14, "Time values wider than 63 bytes are not "
					            "handled by the das2C");
	
		if(! dt_parsetime(sBuf, &dt)){
			return das_error(14, "Error in parsetime for ACSII time type.\n" );
		}
		*pOut = Units_convertFromDt(units, &dt);
		return 0;
	}
	
	return das_error(14, "Don't know how to decode values stored as '%s'", 
			            pThis->sType);
}

