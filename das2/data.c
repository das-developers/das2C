#define _POSIX_C_SOURCE 200112L

#include <locale.h>
#include <string.h>
#include <ctype.h>

#include "das1.h"
#include "data.h"
#include "util.h"

/** Initialize a datum structure using a string, choosing the right offset
 * units for time values is a bit tricky and there's no obvious answer. */
bool Datum_fromStr(datum_t* pDatum, const char* sStr)
{
	char sBuf[128] = {'\0'};
	das_time_t dt = {0};
	
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
		if(! dt_parsetime(sBuf, &dt) ) return false;
		
		/* Lot's of offset units to choose from, just pick one */
		pDatum->units = UNIT_T2000;
		pDatum->value = Units_convertFromDt(pDatum->units, &dt);
		return true;
	}
	
	/* General values */
	size_t uCopy = pRead - sStr < 127 ? pRead - sStr : 127;
	
	strncpy(sBuf, sStr, uCopy);
	if(! das2_str2double(sBuf, &(pDatum->value) )) return false;
	
	if(pRead == '\0'){
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

/** Write a datum out as a string using default decisions */
char* Datum_toStr(
	char* sStr, size_t uLen, int nFracDigits, const datum_t* pDatum
){
	if(nFracDigits < 0) nFracDigits = 0;
	if(nFracDigits > 9) nFracDigits = 9;
	
	das_time_t dt = {'\0'};
	if( Units_haveCalRep(pDatum->units)){
		Units_convertToDt(&dt, pDatum->value, pDatum->units);
		dt_isoc(sStr, uLen, &dt, nFracDigits);
		return sStr;
	}
	
	char sFmt[32] = {'\0'};
	if(pDatum->units == UNIT_DIMENSIONLESS){
		sprintf(sFmt, "%%.%de", nFracDigits);
		snprintf(sStr, uLen, sFmt, pDatum->value);
	}
	else{
		sprintf(sFmt, "%%.%de %%s", nFracDigits);
		snprintf(sStr, uLen, sFmt, pDatum->value, Units_toStr(pDatum->units));
	}
	return sStr;
}
