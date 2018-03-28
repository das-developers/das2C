#define _POSIX_C_SOURCE 200112L

#include <stdio.h>
#include <string.h>
#include <math.h>

#include "das1.h"

#include "encoding.h"
#include "buffer.h"

/* ************************************************************************* */
/* FILL */
double getDas2Fill() {
    return FILL_VALUE;
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
		das2_error(14, "Encoding category 0x%04X is unknown");
		return NULL;
	}
	
	if(nCat == DAS2DT_BE_REAL || nCat == DAS2DT_LE_REAL){
		if(nWidth != 4 && nWidth != 8){
			das2_error(14, "%d-byte binary reals are not supported", nWidth);
			return NULL;
		}
	}

	if(nWidth < 2 || nWidth > 127){
		das2_error(14, "Error in encoding type %s, valid field width range "
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
			das2_error(14, "Format string is longer than %d bytes",DASENC_FMT_LEN-1);
			return NULL;
		}
		strcpy(pThis->sFmt, sFmt);
	}
	
	if( DasEnc_toString(pThis, pThis->sType, DASENC_TYPE_LEN) != 0) return NULL;
	
	return pThis;
}

DasEncoding* new_DasEncoding_str(const char* sType)
{	
	char sBuf[4] = {'\0'};
	
	if(sType == NULL)
		das2_error(5, "Null pointer in dasEncoding_fromString");
	
	DasEncoding* pThis = (DasEncoding*)calloc(1, sizeof(DasEncoding));
	
	strncpy(pThis->sType, sType, DASENC_TYPE_LEN - 1);
	
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
   
	
	int i = 0;
	if(strncmp(sType, "ascii", 5) == 0){
		
		pThis->nCat = DAS2DT_ASCII;
		
		for(i = 0; i < 3; i++){
			if(sType[i+5] == '\0') break;
			sBuf[i] = sType[i+5];
		}
		if(sscanf(sBuf, "%d", &(pThis->nWidth)) != 1)
			das2_error(14, "Error parsing encoding type '%s'", sType);
		return pThis;
	}
	
	if(strncmp(sType, "time", 4) == 0){
		
		pThis->nCat = DAS2DT_TIME;
				
		for(i = 0; i < 3; i++){
			if(sType[i+4] == '\0') break;
			sBuf[i] = sType[i+4];
		}
		if(sscanf(sBuf, "%d", &(pThis->nWidth)) != 1)
			das2_error(14, "Error parsing encoding type '%s'", sType);
		
		return pThis;
	}
	
	if(pThis->nWidth < 2 || pThis->nWidth > 127)
		das2_error(14, "Error in encoding type %s, valid field width range "
		                "is 2 to 127 characters", sType);
	    
	das2_error(14, "Error parsing encoding type '%s'", sType );
 	return NULL; /* Make Compiler Happy, code is never reached */
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
	if(pOne->sFmt && !(pTwo->sFmt)) return false;
	if(!(pOne->sFmt) && pTwo->sFmt) return false;
	if(pOne->sFmt && pTwo->sFmt && strcmp(pOne->sFmt, pTwo->sFmt) != 0) 
		return false;
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
		das2_error(14, "Encoding %s is not a general ASCII type", pThis->sType); 
	
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
		das2_error(14, "Encoding %s is not a ASCII Time type", pThis->sType); 
	
	pThis->nWidth = nFmtWidth + 1;
	strncpy(pThis->sFmt, sValFmt, DASENC_FMT_LEN - 1);	
}

/* Guess a good default ascii format string based off the encoding width */
void _DasEnc_setDefaultAsciiFmt(DasEncoding* pThis)
{
	if(pThis->nCat != DAS2DT_ASCII)
		das2_error(18, "Plane data encoding is not general ASCII");
	
	if((pThis->nWidth < 9)||(pThis->nWidth > 24))
		das2_error(14, "Use DasEnc_setAsciiFormat to output general ASCII "
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
		das2_error(14, "Encoding is not ASCII Time strings");
		
	/* At least expect the width to be 5 long, this gets the year plus a */
	/* separator */
	if((pThis->nWidth < 5)||(pThis->nWidth > 31))
		das2_error(14, "Use DasEnc_setTimeFormat to output ASCII "
				          "time values in less than 5 characters or more than 31 "
				          "characters");
		
	/* The assumption in this guesser is that people usually want whole */
	/* fields, and some old code likes to throw Z's on the end of all times */
	/* to indicate UTC (i.e. Zulu time) */
	
	switch(pThis->nWidth){  /* Remember, width includes the separator */
	
	/* Year only */
	case 5: strcpy(pThis->sFmt, "%04d"); break;
	case 6: strcpy(pThis->sFmt, "%04d "); break;
	case 7: strcpy(pThis->sFmt, "%04d  "); break;
	
	/* Year and Month */
	case 8: strcpy(pThis->sFmt,  "%04d-%02d"); break;
	case 9: strcpy(pThis->sFmt,  "%04d-%02d "); break;
	case 10: strcpy(pThis->sFmt, "%04d-%02d  "); break;
	
	/* Year/Month/Day of Month */
	case 11: strcpy(pThis->sFmt, "%04d-%02d-%02d"); break;
	case 12: strcpy(pThis->sFmt, "%04d-%02d-%02d "); break;
	case 13: strcpy(pThis->sFmt, "%04d-%02d-%02d  "); break;
	
	/* Date + Hour */
	case 14: strcpy(pThis->sFmt, "%04d-%02d-%02dT%02d"); break;
	case 15: strcpy(pThis->sFmt, "%04d-%02d-%02dT%02d "); break;
	case 16: strcpy(pThis->sFmt, "%04d-%02d-%02dT%02d  "); break;
	
	/* Date + Hour:min */
	case 17: strcpy(pThis->sFmt, "%04d-%02d-%02dT%02d:%02d"); break;
	case 18: strcpy(pThis->sFmt, "%04d-%02d-%02dT%02d:%02d "); break;
	case 19: strcpy(pThis->sFmt, "%04d-%02d-%02dT%02d:%02d  "); break;
	
	/* Date + Hour:min:sec */
	case 20: strcpy(pThis->sFmt, "%04d-%02d-%02dT%02d:%02d:%02.0f"); break;
	case 21: strcpy(pThis->sFmt, "%04d-%02d-%02dT%02d:%02d:%02.0f "); break;
	
	/* Date + hour:min:sec + frac seconds */
	case 22: strcpy(pThis->sFmt, "%04d-%02d-%02dT%02d:%02d:%04.1f"); break;
	case 23: strcpy(pThis->sFmt, "%04d-%02d-%02dT%02d:%02d:%05.2f"); break;
	case 24: strcpy(pThis->sFmt, "%04d-%02d-%02dT%02d:%02d:%06.3f"); break;
	case 25: strcpy(pThis->sFmt, "%04d-%02d-%02dT%02d:%02d:%06.3f "); break;
	case 26: strcpy(pThis->sFmt, "%04d-%02d-%02dT%02d:%02d:%06.3f  "); break;
	case 27: strcpy(pThis->sFmt, "%04d-%02d-%02dT%02d:%02d:%09.6f"); break;
	case 28: strcpy(pThis->sFmt, "%04d-%02d-%02dT%02d:%02d:%09.6f "); break;
	case 29: strcpy(pThis->sFmt, "%04d-%02d-%02dT%02d:%02d:%09.6f  "); break;
	case 30: strcpy(pThis->sFmt, "%04d-%02d-%02dT%02d:%02d:%012.9f"); break;
	case 31: strcpy(pThis->sFmt, "%04d-%02d-%02dT%02d:%02d:%012.9f "); break;
	
	/* If nano-seconds isn't good enough, revise in the future */
	}
}

/* ************************************************************************* */
/* Info and Config changes */

ErrorCode DasEnc_toString(DasEncoding* pThis, char* sType, size_t nLen )
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
			return das2_error(5, "Buffer is too small to receive encoding string "
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
		return das2_error(14, "Value Encoding category %d is unknown", pThis->nCat);
	}
	
	if(nRet < 0)
		return das2_error(14, "Couldn't convert DasEncoding 0x%X04 to a string", nHash);
	
	if(nRet >= nLen - 1)
		return das2_error(14, "Buffer too small to receive DasEncoding string", nHash);
	
	return 0;
}

/* ************************************************************************* */
/* Encoding */

ErrorCode _writePacketMsbfReal4(DasBuf* pBuf, float data)
{
#ifdef HOST_IS_LSB_FIRST
	unsigned char cdata[4];
	cdata[0] = *(((unsigned char*)&data) + 3);
	cdata[1] = *(((unsigned char*)&data) + 2);
	cdata[2] = *(((unsigned char*)&data) + 1);
	cdata[3] = *((unsigned char*)&data);
	
	return DasBuf_write(pBuf, cdata, 4);
#else
	return DasBuf_write(pBuf, &data, 4);
#endif
}

ErrorCode _writePacketLsbfReal4(DasBuf* pBuf, float data)
{
#ifdef HOST_IS_LSB_FIRST
	return DasBuf_write(pBuf, &data, 4);
#else
	unsigned char cdata[4];
	cdata[0] = *(((unsigned char*)&data) + 3);
	cdata[1] = *(((unsigned char*)&data) + 2);
	cdata[2] = *(((unsigned char*)&data) + 1);
	cdata[3] = *((unsigned char*)&data);
	
	return DasBuf_write(pBuf, cdata, 4);
#endif
}

ErrorCode _writePacketMsbfReal8(DasBuf* pBuf, double data)
{
#ifdef HOST_IS_LSB_FIRST
	unsigned char cdata[8];
	cdata[0] = *(((unsigned char*)&data) + 7);
	cdata[1] = *(((unsigned char*)&data) + 6);
	cdata[2] = *(((unsigned char*)&data) + 5);
	cdata[3] = *(((unsigned char*)&data) + 4);
	cdata[4] = *(((unsigned char*)&data) + 3);
	cdata[5] = *(((unsigned char*)&data) + 2);
	cdata[6] = *(((unsigned char*)&data) + 1);
	cdata[7] = *((unsigned char*)&data);
	
	return DasBuf_write(pBuf, cdata, 8);
#else
	return DasBuf_write(pBuf, &data, 8);
#endif
}

ErrorCode _writePacketLsbfReal8(DasBuf* pBuf, double data)
{
#ifdef HOST_IS_LSB_FIRST
	return DasBuf_write(pBuf, &data, 8);
#else
	unsigned char cdata[8];
	cdata[0] = *(((unsigned char*)&data) + 7);
	cdata[1] = *(((unsigned char*)&data) + 6);
	cdata[2] = *(((unsigned char*)&data) + 5);
	cdata[3] = *(((unsigned char*)&data) + 4);
	cdata[4] = *(((unsigned char*)&data) + 3);
	cdata[5] = *(((unsigned char*)&data) + 2);
	cdata[6] = *(((unsigned char*)&data) + 1);
	cdata[7] = *((unsigned char*)&data);
	
	return DasBuf_write(pBuf, cdata, 8);
#endif
}

ErrorCode _encodeAsciiValue(DasEncoding* pThis, DasBuf* pBuf, double data)
{	
	int nExpect = pThis->nWidth - 1;
	
	/* Select a default output format if needed */
	if(pThis->sFmt[0] == '\0') _DasEnc_setDefaultAsciiFmt(pThis);
	
	size_t uPosBeg = DasBuf_written(pBuf);
	int nRet = DasBuf_printf(pBuf, pThis->sFmt, data);
	if(nRet != 0) return nRet;
	size_t uPosEnd = DasBuf_written(pBuf);
	
	if(nExpect != uPosEnd - uPosBeg)
		return das2_error(
			14, "Output value '%s' using format '%s' for encoding '%s' occupied %zu "
			"bytes, expected %d", pBuf->sBuf + uPosBeg, pThis->sFmt, pThis->sType, 
			uPosEnd - uPosBeg, nExpect
		);
	
	return 0;
}

ErrorCode _encodeTimeValue(
	DasEncoding* pThis, DasBuf* pBuf, double data, UnitType units
){

	das_time_t dt = {0};
	Units_convertToDt(&dt, data, units);
	
	int nExpect = pThis->nWidth - 1;
	
	/* Select a default output format if needed */
	if(pThis->sFmt[0] == '\0') _DasEnc_setDefaultTimeFmt(pThis);
	
	size_t uPosBeg = DasBuf_written(pBuf);
	int nRet = DasBuf_printf(pBuf, pThis->sFmt, dt.year, dt.month, dt.mday, 
			                   dt.hour, dt.minute, dt.second);
	if(nRet != 0) return nRet;
	size_t uPosEnd = DasBuf_written(pBuf);
	
	if(nExpect != uPosEnd - uPosBeg){
		return das2_error(14, "Output value '%s' for encoding %s occupied %d "
				            "bytes, expected %d", pBuf->sBuf + uPosBeg, pThis->sType, 
		                  uPosEnd - uPosBeg, nExpect);
	}
	return 0;
}


ErrorCode DasEnc_write(
	DasEncoding* pThis, DasBuf* pBuf, double value, UnitType units
){
	int nHash = DasEnc_hash(pThis);
	
	switch(nHash){
	case DAS2DT_BE_REAL_4: return _writePacketMsbfReal4(pBuf, value);
	case DAS2DT_LE_REAL_4: return _writePacketLsbfReal4(pBuf, value);
	case DAS2DT_BE_REAL_8: return _writePacketMsbfReal8(pBuf, value);
	case DAS2DT_LE_REAL_8: return _writePacketLsbfReal8(pBuf, value);
	}
	
	/* Okay, must be ascii or time */
	if(pThis->nCat == DAS2DT_ASCII)
		return _encodeAsciiValue(pThis, pBuf, value);
	
	if(pThis->nCat == DAS2DT_TIME)
		return _encodeTimeValue(pThis, pBuf, value, units);
	
	return das2_error(14, "Don't know how to encode values to format %s", 
			          pThis->sType);
}

/* ************************************************************************* */
/* Decoding */

ErrorCode DasEnc_read(
	const DasEncoding* pThis, DasBuf* pBuf, UnitType units, double* pOut
){
	*pOut = FILL_VALUE;
	
	char sBuf[128] = {'\0'};  /* Max field width is 127 chars so we're good */
	int nHash = DasEnc_hash(pThis);
	
	if( DasBuf_read(pBuf, sBuf, pThis->nWidth) != pThis->nWidth)
		return das2_error(14, "Input buffer ends in the middle of a value");
	
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
	
	das_time_t dt = {0};
	if(pThis->nCat == DAS2DT_TIME){
		/* String parsing can be persnicity, copy over to a null terminated buffer */
		if(pThis->nWidth > 63)
			return das2_error(14, "Time values wider than 63 bytes are not "
					            "handled by the libdas2");
	
		if(! dt_parsetime(sBuf, &dt)){
			return das2_error(14, "Error in parsetime for ACSII time type.\n" );
		}
		*pOut = Units_convertFromDt(units, &dt);
		return 0;
	}
	
	return das2_error(14, "Don't know how to decode values stored as '%s'", 
			            pThis->sType);
}

