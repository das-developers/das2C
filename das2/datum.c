/* Copyright (C) 2017 Chris Piker <chris-piker@uiowa.edu>
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

#include <locale.h>
#include <string.h>
#include <inttypes.h>
#include <ctype.h>

#include "das1.h"
#include "datum.h"
#include "array.h"
#include "util.h"

/* ************************************************************************* */
/* Datum functions and structures */

bool das_datum_fromDbl(das_datum* pDatum, double value, das_units units)
{
	memset(pDatum, 0, sizeof(das_datum));
	pDatum->vt = vtDouble;
	pDatum->vsize = das_vt_size(vtDouble);
	memcpy(pDatum->bytes, &value, sizeof(double));
	pDatum->units = units;
	return true;
}

/* Initialize a small structure using a string, works for any string 
   but might not give you the results you expected */
bool das_datum_fromStr(das_datum* pDatum, const char* sStr)
{
	char sBuf[128] = {'\0'};
	das_time dt = {0};
	
	memset(pDatum, 0,  sizeof(das_datum));
	pDatum->vt = vtUnknown;
	
	const char* pRead = sStr;
	const char* pAhead = sStr + 1;
	
	struct lconv* pLocale = localeconv();
	char cDecPt = pLocale->decimal_point[0];  /* Not generically UTF-8 safe, but
															 * will handle French convention */
	if( sStr[0] == '\0') return false;
	
	/* Find first character of the units string. */  
	/* ' -3.145e+14dogs', '2017-001T14:00:59.431 UTC' */
	bool bTryTime = false;
	while(*pRead != '\0'){
		
		if( *pRead == ':' || (isdigit(*pRead) && (*pAhead == '-'))) bTryTime = true;
		
		if( isdigit(*pRead) || *pRead == '+' || *pRead == '-' || 
		    *pRead == cDecPt || *pRead == ':' || isspace(*pRead) ){
			++pRead; ++pAhead; continue;
		}
		if( (*pRead == 'x' || *pRead == 'X') && isdigit(*pAhead)){
			++pRead; ++pAhead; continue;
		}
		if( (*pRead == 'e' || *pRead == 'E') && isdigit(*pAhead)){
			++pRead; ++pAhead; continue;
		}
		if((*pRead == 'T') && isdigit(*pAhead)){ 
			++pRead; ++pAhead; bTryTime = true;
			continue; 
		}
		break;
	}
	
	/* All time strings are UTC in das, ignore units */
	if(bTryTime){
		strncpy(sBuf, sStr, 127);
		sBuf[ pRead - sStr] = '\0';
		if(dt_parsetime(sBuf, &dt) ){ 
			pDatum->units = UNIT_UTC;
			memcpy(pDatum->bytes, &dt, sizeof(das_time));
			pDatum->vt = vtTime;
			return true;
		}
	}
	
	/* Try classic datum, double value plus units */
	size_t uCopy = pRead - sStr < 127 ? pRead - sStr : 127;
	
	strncpy(sBuf, sStr, uCopy);
	if( ! das_str2double(sBuf, (double*)pDatum->bytes )) return false;
	pDatum->vt = vtDouble;
	
	if(*pRead == '\0'){
		pDatum->units = UNIT_DIMENSIONLESS;
		return true;
	}
		
	memset(sBuf, 0, 128);
	uCopy = strlen(sStr) - (pRead - sStr);
	uCopy = uCopy < 127 ? uCopy : 127;
	strncpy(sBuf, pRead, uCopy);
	
	pDatum->units = Units_fromStr(sBuf);
	
	return true;
}

/* This one is simple */
bool das_datum_wrapStr(das_datum* pDatum, das_units units, const char* sStr)
{
	/* Careful, we are copying an address, not a value at an address*/
	memcpy(pDatum->bytes, &sStr,  sizeof(const char*));
	pDatum->vt = vtText;
	pDatum->units = units;
	pDatum->vsize = sizeof(const char*);
	return true;
}

bool das_datum_byteSeq(
	das_datum* pDatum, das_byteseq seq, das_units units
){
	memcpy(pDatum->bytes, &seq, sizeof(das_byteseq));
	pDatum->vt = vtByteSeq;
	pDatum->vsize = sizeof(das_byteseq);
	pDatum->units = units;
	return true;
}

double das_datum_toDbl(const das_datum* pThis)
{
	double rRet;
	switch(pThis->vt){
	case vtByte:   rRet = *((byte*)pThis); break;
	case vtUShort: rRet = *((uint16_t*)pThis); break;
	case vtShort:  rRet = *((int16_t*)pThis); break;
	case vtInt:    rRet = *((int32_t*)pThis); break;
	case vtFloat:  rRet = *((float*)pThis); break;
	case vtDouble: rRet = *((double*)pThis); break;
	
	/* Attempt string conversion */
	case vtText:
		if(!das_str2double((const char*)pThis, &rRet)){
			das_error(DASERR_DATUM, "Couldn't convert %s to a double", 
				       (const char*)pThis );
			rRet = DAS_FILL_VALUE;
		}
	default:
		das_error(DASERR_DATUM, "Don't know how to convert items of type %s"
		          " to doubles.", das_vt_toStr(pThis->vt));
		rRet = DAS_FILL_VALUE;
	}
	return rRet;
}

/* Like toDbl but cares about scale and epoch */
bool das_datum_toEpoch(
	const das_datum* pThis, das_units epoch, double* pResult
){
	if(!Units_haveCalRep(epoch) || (epoch == UNIT_UTC)) return false;
	
	das_time dt;
	
	if(pThis->vt == vtTime){ 
		*pResult = Units_convertFromDt(epoch, (das_time*)pThis);
		return (*pResult != DAS_FILL_VALUE);
	}
	
	/* text is interesting, because it could be 2017-01-01 or something
	   like "2.37455" which are handle very differently */
	
	double rDbl = 0.0;
	if(pThis->vt == vtText){
		if( dt_parsetime((const char*)pThis, &dt)){
			return Units_convertFromDt(epoch, &dt);
		}
		else{
			/* parsetime failed, try to convert as an ASCII real */
			if( ! das_str2double((const char*)pThis, &rDbl)) return false;
			
			/* have a real, see if I'm in non UTC epoch units */
			if(!Units_haveCalRep(pThis->units) || (pThis->units == UNIT_UTC)) 
				return false;
			
			*pResult = Units_convertTo(epoch, rDbl, pThis->units);
			return (*pResult != DAS_FILL_VALUE);
		}
	}
	
	/* for the rest, I have to have an epoch of my own or I don't 
	   know where zero is at */
	if(!Units_haveCalRep(pThis->units) || (pThis->units == UNIT_UTC)) 
		return false;
	
	switch(pThis->vt){
	case vtByte:   rDbl = *((byte*)pThis); break;
	case vtUShort: rDbl = *((uint16_t*)pThis); break;
	case vtShort:  rDbl = *((int16_t*)pThis); break;
	case vtInt:    rDbl = *((int32_t*)pThis); break;
	case vtFloat:  rDbl = *((float*)pThis); break;
	case vtDouble: rDbl = *((double*)pThis); break;
	default:
		das_error(DASERR_DATUM, "Don't know how to convert items of type %s"
		          " to epoch times", das_vt_toStr(pThis->vt));
		return false;
	}
	
	*pResult = Units_convertTo(epoch, rDbl, pThis->units);
	return (*pResult != DAS_FILL_VALUE);
}
	
/** Write a datum out as a string */
char* _das_datum_toStr(
	const das_datum* pThis, char* sBuf, int nLen, int nFracDigits, bool bPrnUnits
){
	if(nLen < 2) return NULL;
	memset(sBuf, 0, nLen);
	
	/* Sometimes you have a time that is encoded as as double or other
	   numeric type.  Convert this to a broken down time then print it */
	das_datum dm;
	if((pThis->vt != vtTime) && Units_haveCalRep(pThis->units)){
		Units_convertToDt((das_time*)&dm, das_datum_toDbl(pThis), pThis->units);
		dm.vt = vtTime;
		dm.vsize = sizeof(das_time);
		dm.units = UNIT_UTC;
		pThis = &dm;   /* Pull a switcheroo*/
	}
	
	/* Write the value... */
	char sFmt[32] = {'\0'};
	size_t u = 0;
	const das_idx_info* pInfo = NULL;
	const das_byteseq* pBs = NULL;
	
	int nWrote = 0;
	switch(pThis->vt){
			
	case vtByte:
		nWrote = snprintf(sBuf, nLen - 1, "%hhu", *((uint8_t*)pThis));
		break;
		
	case vtUShort:
		nWrote = snprintf(sBuf, nLen - 1, "%hu", *((uint16_t*)pThis));
		break;
		
	case vtShort:
		nWrote = snprintf(sBuf, nLen - 1, "%hd", *((int16_t*)pThis));
		break;
		
	case vtInt:
		nWrote = snprintf(sBuf, nLen - 1, "%d", *((int32_t*)pThis));
		break;
		
	case vtLong:
		/* nWrote = snprintf(sBuf, nLen - 1, "%lld", *((int64_t*)pThis)); */
		/* RPId64 is a built-in format code for int64 items since the actual
		   code to use varies from system to system.  Defined in inttypes.h
			above */
		nWrote = snprintf(sBuf, nLen - 1, PRId64, *((int64_t*)pThis));
		break;
		
	case vtFloat:
		snprintf(sFmt, 31, "%%.%de", nFracDigits);
		nWrote = snprintf(sBuf, nLen - 1, sFmt, *((float*)pThis));
		break;
		
	case vtDouble:
		snprintf(sFmt, 31, "%%.%de", nFracDigits);
		nWrote = snprintf(sBuf, nLen - 1, sFmt, *((double*)pThis));
		break;
		
	case vtTime:
		dt_isoc(sBuf, nLen - 1, (const das_time*)pThis, nFracDigits);
		nWrote = strlen(sBuf);
		break;
		
	case vtText:
		strncpy(sBuf, (const char*)pThis, nLen - 1);
		nWrote = ((nLen - 1 ) > strlen(sBuf)) ? nLen - 1 : strlen(sBuf);
		break;
		
	case vtByteSeq:
		/* Print as hex */
		u = 0;
		pBs = (das_byteseq*)pThis;
				 
		while((u*3 < (nLen - 4))&&(u < pBs->sz)){

			snprintf(sBuf + u*3, 3, "%hhX ", ((byte*)pBs->ptr)[u]);
			++u;
			nWrote += 3;
		}
		/* Write null at last spot */
		if(u > 0) sBuf[u*3 - 1] = '\0';
		else sBuf[0] = '\0';
		break;
	
	case vtIndex:
		pInfo = (const das_idx_info*)pThis;
		snprintf(sBuf, nLen - 1, "Offset: %zd, Count: %zu", pInfo->nOffset, pInfo->uCount);
		nWrote = strlen(sBuf);
		break;
		
	default:
		strncpy(sBuf, "UNKNOWN", nLen -1);
		nWrote = (7 < (nLen - 1)) ? 7 : (nLen - 1);
		break;
	}
	
	nLen -= nWrote;

	/* Write the units */
	const char* sUnits = Units_toStr(pThis->units);
	if((pThis->units != UNIT_DIMENSIONLESS)&&(bPrnUnits)){
		if(nLen > strlen(sUnits) + 2){
			sBuf[nWrote] = ' ';  ++nWrote;
			strncpy(sBuf + nWrote, sUnits, strlen(sUnits));
			nWrote += strlen(sUnits);
			sBuf[nWrote] = '\0';
		}
	}
		
	return sBuf;
}

char* das_datum_toStr(
	const das_datum* pThis, char* sStr, size_t uLen, int nFracDigits
){
	return _das_datum_toStr(pThis, sStr, uLen, nFracDigits, true);
}

char* das_datum_toStrValOnly(
	const das_datum* pThis, char* sStr, size_t uLen, int nFracDigits
){
	return _das_datum_toStr(pThis, sStr, uLen, nFracDigits, false);
}

