/* Copyright (C) 2024 Chris Piker <chris-piker@uiowa.edu> (re-written, new storage)
 *
 * This file is part of das2C, the Core Das2 C Library.
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

#include <assert.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <math.h>
#include <limits.h>
#include <ctype.h>

#ifdef _WIN32
#define strcasecmp _stricmp
#else
#include <strings.h> 
#endif


#include "util.h"
#include "encoding.h"
#include "value.h"
#include "descriptor.h"

const char* das_desc_type_str(desc_type_t dt){
	switch(dt){
	case STREAM:   return "stream";
	case PLANE:    return "plane";
	case PACKET:   return "packet";
	case PHYSDIM:  return "physdim";
	case DATASET:  return "dataset";
	default: return "unknown";
	}
}

/* ************************************************************************* */
/* Construction/Destruction */

void DasDesc_init(DasDesc* pThis, desc_type_t dt){
	pThis->type = dt;  /* Intentionally invalid */

	const char* sId;
	switch(dt){
	case STREAM:   sId = "stream_properties";    break;
	case PLANE:    sId = "plane_properties";     break;
	case PACKET:   sId = "packet_properties";    break;
	case PHYSDIM:  sId = "physdim_properties";   break;
	case DATASET:  sId = "dataset_properties";   break;
	default:       sId = "desciptor_properties"; break;
	}

	DasAry_init(&(pThis->properties), sId, vtByte, 0, NULL, RANK_2(0,0), UNIT_DIMENSIONLESS);
	DasAry_setUsage(&(pThis->properties), D2ARY_AS_SUBSEQ);

	pThis->parent = NULL;
	pThis->bLooseParsing = false;
	pThis->uInvalid = 0;
}

DasDesc* new_Descriptor(){
	DasDesc* pThis = (DasDesc*)calloc(1, sizeof(DasDesc));
	pThis->type = UNK_DESC;
	DasAry_init(
		&(pThis->properties), "descriptor_properties", vtByte, 0, NULL, RANK_2(0,0),
		UNIT_DIMENSIONLESS
	); 
	DasAry_setUsage(&(pThis->properties), D2ARY_AS_SUBSEQ);
	return pThis;
}

void DasDesc_freeProps(DasDesc* pThis){
	DasAry_deInit(&(pThis->properties));
}

/* ************************************************************************* */
/* Ownership */

const DasDesc* DasDesc_parent(DasDesc* pThis)
{
	return pThis->parent;  /* only useful for hiding the structure, not sure
	                          if it's worth it */
}

/* ************************************************************************* */
/* Getting Properties */

const DasProp* DasDesc_getLocal(const DasDesc* pThis, const char* sName)
{
	const DasAry* pProps = &(pThis->properties);
	size_t nProps = DasAry_lengthIn(pProps, DIM0);
	const DasProp* pProp;

	for(size_t i = 0; i < nProps; ++i){
		size_t uPropLen = 0;
		pProp = (const DasProp*) DasAry_getBytesIn(pProps, DIM1_AT(i), &uPropLen);
		if((pProp->flags & DASPROP_VALID_MASK) && 
			(strcmp(DasProp_name(pProp), sName) == 0)
		)
			return pProp;
	}
	return NULL;
}

const DasProp* DasDesc_getProp(const DasDesc* pThis, const char* sName)
{
	// Still a linear search, with two loops, but over a continuous block
	// of memory at least
	const DasProp* pProp = DasDesc_getLocal(pThis, sName);
	if(pProp != NULL)
		return pProp;

	if (pThis->parent != NULL) 
		return DasDesc_getProp(pThis->parent, sName);
	
	return NULL;
}


/* returns NULL if property does not exist, pointer to string value          */
/* otherwise.  Recursively searches up the parent hierarchy if the given     */

/* property isn't present in this descriptor                                 */

const char* DasDesc_get(const DasDesc* pThis, const char* sName)
{
	const DasProp* pProp = DasDesc_getProp(pThis, sName);
	if(pProp != NULL)
		return DasProp_value(pProp);

	return NULL;
}

size_t DasDesc_getArray(
	DasDesc* pThis, const char* sName, char cSep,
	char* pBuf, size_t uBufSz, char** psVals, size_t uMaxVals
){
	if((uBufSz < 2)||(uMaxVals < 1)) return 0;
	if(isspace(cSep)){
		das_error(DASERR_DESC, 
			"Space seperators not supported, since functions trims each output"
		);
		return 0;
	}
	
	const char* sVal = DasDesc_get(pThis, sName);
	if(sVal == NULL)
		return 0;

	size_t uLen = strlen(sVal);
	if(uLen > uBufSz - 2) uLen = uBufSz - 2;
	
	*pBuf = cSep;  /* Add seperator to first buffer */
	strncpy(pBuf+1, sVal, uLen); pBuf[uLen] = '\0';
	
	size_t u = 0;       /* Initial value is seperator at start of buffer */
	psVals[u] = pBuf;
	
	char* p = pBuf;     /* All other values start on separators */
	while((*p != '\0')||(u < uMaxVals-1)){
		if(*p == cSep){  psVals[u] = p;  ++u;  }
		++p;
	}
	uMaxVals = u;  /* Reduce value count to what we measured */
	
	/* point value begin after the seperator, if there is nothing after the
	 * seperator but an ending, mark the value as null */
	size_t uSz;
	for(u = 0; u < uMaxVals; ++u){  /* null space right */
		*(psVals[u]) = '\0';
		psVals[u] += 1;
		uSz = 0;
		p = psVals[u];
		while((*p != cSep)&&(*p != '\0')){++uSz; ++p;}
		if(uSz == 0) psVals[u] = NULL;
	}
	
	/* Trim elements left */
	for(u = 0; u < uMaxVals; ++u){
		if(psVals[u] == NULL) continue;
		
		while(isspace( *(psVals[u]) ) && ( *(psVals[u]) != '\0')){
			*(psVals[u]) = '\0';
			psVals[u] += 1;
		}
		if(*(psVals[u]) == '\0'){   /* all content was space chars */
			psVals[u] = NULL;
			continue;
		}
	}
	
	/* Anything that is left has something, do tail trim */
	for(u = 0; u < uMaxVals; ++u){
		if(psVals[u] == NULL) continue;
		
		uSz = strlen(psVals[u]);
		while((uSz > 0)&&( isspace(psVals[u][uSz - 1]))){
			psVals[u][uSz - 1] = '\0';
			uSz -= 1;
		}
	}
	return uMaxVals;
}

const char* DasDesc_getType(const DasDesc* pThis, const char* sName)
{
	const DasProp* pProp = DasDesc_getProp(pThis, sName);
	if(pProp != NULL)
		return DasProp_typeStr2(pProp);

	return NULL;
}

das_units DasDesc_getUnits(const DasDesc* pThis, const char* sName)
{
	const DasProp* pProp = DasDesc_getProp(pThis, sName);
	if(pProp != NULL)
		return pProp->units;
	return UNIT_DIMENSIONLESS;
}


bool DasDesc_has(const DasDesc* pThis, const char* sName)
{
    return (DasDesc_getProp(pThis, sName) != NULL);
}

size_t DasDesc_length(const DasDesc* pThis)
{
	size_t uTotal = DasAry_lengthIn(&(pThis->properties), DIM0);
	return uTotal;
}

const char* DasDesc_getNameByIdx(const DasDesc* pThis, size_t uIdx)
{
	const DasAry* pProps = &(pThis->properties);
	size_t uProps = DasAry_lengthIn(pProps, DIM0);
	if(uIdx >= uProps) 
		return NULL;

	size_t uPropLen;
	const DasProp* pProp = (const DasProp*) DasAry_getBytesIn(
		pProps, DIM1_AT(uIdx), &uPropLen
	);

	return DasProp_name(pProp);
}

const char* DasDesc_getValByIdx(const DasDesc* pThis, size_t uIdx)
{
	const DasAry* pProps = &(pThis->properties);
	size_t uProps = DasAry_lengthIn(pProps, DIM0);
	if(uIdx >= uProps) 
		return NULL;

	size_t uPropLen;
	const DasProp* pProp = (const DasProp*) DasAry_getBytesIn(
		pProps, DIM1_AT(uIdx), &uPropLen
	);

	return DasProp_value(pProp);
}

const char* DasDesc_getTypeByIdx(const DasDesc* pThis, size_t uIdx)
{
	const DasAry* pProps = &(pThis->properties);
	size_t uProps = DasAry_lengthIn(pProps, DIM0);
	if(uIdx >= uProps) 
		return NULL;

	size_t uPropLen;
	const DasProp* pProp = (const DasProp*) DasAry_getBytesIn(
		pProps, DIM1_AT(uIdx), &uPropLen
	);

	return DasProp_typeStr2(pProp);
}

const char* DasDesc_getStr(const DasDesc* pThis, const char* sName)
{
	return DasDesc_get(pThis, sName);
}

size_t DasDesc_getStrAry(
	DasDesc* pThis, const char* sName, char* pBuf, size_t uBufSz,
	char** psVals, size_t uMaxVals
){
	return DasDesc_getArray(pThis, sName, '|', pBuf, uBufSz, psVals, uMaxVals);
}

bool DasDesc_getBool(DasDesc* pThis, const char* sName)
{
	const char* sVal = DasDesc_get(pThis, sName);
	if(sVal == NULL) return false;
	if(strlen(sVal) == 0) return false;
	if(isdigit(sVal[0]) && sVal[0] != '0') return true;
	if(strcasecmp("true", sVal) == 0) return true;
	return false;
}

double DasDesc_getDouble(const DasDesc* pThis, const char* sName)
{
	double rVal;
	const char* sVal = DasDesc_get( pThis, sName );
	if(sVal == NULL) 
		return DAS_FILL_VALUE;
   
	if(sscanf(sVal, "%lf", &rVal ) != 1){
		das_error(DASERR_DESC, "Can't convert %s to an double", sVal);
		return DAS_FILL_VALUE;
	};
	return rVal;
}

int DasDesc_getInt(const DasDesc* pThis, const char* sName ) 
{
	int nVal;
	const char* sVal = DasDesc_get( pThis, sName );
	if(sVal == NULL)
		return INT_MIN;

	if( sscanf(sVal, "%d", &nVal) != 1){
		das_error(DASERR_DESC, "Can't convert %s to an integer", sVal);
		return INT_MIN;
	}
   
	return nVal;
}

bool _Desc_looksLikeTime(const char* sVal)
{
	if(strchr(sVal, ':') != NULL) return true;
	if(strchr(sVal, 'T') != NULL) return true;
	return false;
}

double DasDesc_getDatum(DasDesc* pThis, const char* sName, das_units units)
{
	const DasProp* pProp = DasDesc_getProp(pThis, sName);
	if(pProp == NULL) return DAS_FILL_VALUE;

	const char* sValue = DasProp_value(pProp);

	// Can't convert calendar units to non calendar units
	if(!Units_canConvert(pProp->units, units)){
		das_error(DASERR_DESC, 
			"Can't convert property units of type %s to %s", pProp->units, units
		);
		return DAS_FILL_VALUE;
	}

	/* If these are calendar units and I look like time, use parse time */
	if(Units_haveCalRep(pProp->units) || (pProp->units == UNIT_DIMENSIONLESS))
	{
		/* If the units string is null, assume dimensionless unless the value
			looks like a Time string */
		if(_Desc_looksLikeTime(sValue)){
			das_time dt;
			if(!dt_parsetime(sValue, &dt)){
				das_error(DASERR_DESC, "Couldn't parse %s as a date time", sValue);
				return DAS_FILL_VALUE;	
			}
			return Units_convertFromDt(pProp->units, &dt);
		}
	}

	double rValue;
	if( sscanf(sValue, "%lf", &rValue) != 1){
		das_error(DASERR_DESC, "Couldn't parse %s as a real value", sValue);
		return DAS_FILL_VALUE;
	}
	
	return Units_convertTo(units, rValue, pProp->units);
}

double* DasDesc_getDoubleAry(DasDesc* pThis, const char* sName, int* pNumItems)
{
	const char* sAry = DasDesc_get( pThis, sName);
	return das_csv2doubles(sAry, pNumItems);
}

/* ************************************************************************* */
/* Checking equality of content in this descriptor */

bool DasDesc_equals(const DasDesc* pThis, const DasDesc* pOther)
{
	const DasAry* pMyAry = &(pThis->properties);
	const DasAry* pYourAry = &(pOther->properties);

	size_t uProps = DasAry_lengthIn(pMyAry, DIM0);
	if(uProps != DasAry_lengthIn(pYourAry, DIM0))
		return false;
	
	for(size_t u = 0; u < uProps; u++){
		size_t uPropLen;
		const DasProp* pMyProp = (const DasProp*)DasAry_getBytesIn(pMyAry, DIM1_AT(u), &uPropLen);
		const DasProp* pYourProp = DasDesc_getLocal(pOther, DasProp_name(pMyProp));
		
		if(!DasProp_equal(pMyProp, pYourProp))
			return false;
	}
	return true;
}

/* ************************************************************************* */
/* Setting Properties */

/* Get pointer to property memory by name, even if it's invalid */
static byte* _DasDesc_getPropBuf(DasDesc* pThis, const char* sName, size_t* pPropSz)
{
	DasAry* pProps = &(pThis->properties);
	size_t nProps = DasAry_lengthIn(pProps, DIM0);
	DasProp* pProp;

	for(size_t i = 0; i < nProps; ++i){
		pProp = (DasProp*) DasAry_getBuf(pProps, vtByte, DIM1_AT(i), pPropSz);
		if(strcmp(DasProp_name(pProp), sName) == 0)
			return (byte*)pProp;
	}
	return NULL;
}

DasErrCode DasDesc_set(
	DasDesc* pThis, const char* sType, const char* sName, const char* sVal
){
	/* See if there are units in this value, if so dig them out.  Detect 
	 * units by seeing if the old type "Datum" or "DatumRange" is in use
	 * if so, pull out everything 2nd and later or 4th and later words and
	 * treat that as the units. */

	das_units units = UNIT_DIMENSIONLESS;
	int nUnitWord = 0;
	if(strcmp(sType, "Datum") == 0)
		nUnitWord = 1;
	else 
		if (strcmp(sType, "DatumRange") == 0)
			nUnitWord = 4;

	const char* pRead = sVal;
	for(int i = 0; i < nUnitWord; ++i){
		if( (pRead = strchr(sVal, ' ')) != NULL){
			++pRead;
			
			while(*pRead == ' ') ++pRead;  /* Eat extra spaces if needed */
		}
		else{
			pRead = NULL;
			break;
		}
	}

	if((pRead != NULL)&&(*pRead != '\0'))
		units = Units_fromStr(pRead);

	return DasDesc_set3(pThis, sType, sName, sVal, units);
}

/* copies the property into the property array */
DasErrCode DasDesc_set3(
	DasDesc* pThis, const char* sType, const char* sName, const char* sVal,
	das_units units
){

	size_t uNewSz = dasprop_memsz(sName, sVal);
	
	size_t uOldSz = 0;
	byte* pBuf = _DasDesc_getPropBuf(pThis, sName, &uOldSz);
	if(pBuf != NULL){
		if(uNewSz <= uOldSz)
			return DasProp_init2(
				pBuf, uOldSz, sType, sName, sVal, units, !pThis->bLooseParsing
			);
		
		// Nope, mark it as invalid and do a normal insert
		DasProp_invalid((DasProp*)pBuf);
		++(pThis->uInvalid);
	}

	// Make a new one
	DasAry* pProps = &(pThis->properties);

	if(!DasAry_append(pProps, NULL, uNewSz))
		return das_error(DASERR_DESC, "Couldn't create space for new property");
	DasAry_markEnd(pProps, DIM1);

	size_t uTmp = 0;
	pBuf = DasAry_getBuf(pProps, vtByte, DIM1_AT(-1), &uTmp);
	assert(uTmp == uNewSz);

	return DasProp_init2(pBuf, uNewSz, sType, sName, sVal, units, !pThis->bLooseParsing);
}

DasErrCode DasDesc_setStr(
	DasDesc* pThis, const char* sName, const char * sVal 
) {
	return DasDesc_set3( pThis, "String", sName, sVal, UNIT_DIMENSIONLESS);
}

DasErrCode DasDesc_vSetStr(
	DasDesc* pThis, const char* sName, const char* sFmt, ...
) {
	
	/* Guess we need no more than 128 bytes. */
	int n, nLen = 128;
	char* sVal;
	va_list ap;
	
	if( (sVal = (char*)malloc(nLen)) == NULL) 
		return das_error(DASERR_DESC, "Unable to malloc %d bytes", nLen);
	
	while (1) {
		/* Try to print in the allocated space. */
		va_start(ap, sFmt);
		n = vsnprintf (sVal, nLen, sFmt, ap);
		va_end(ap);
		
		/* If that worked, use this string. */
		if (n > -1 && n < nLen)
			break;
		
		/* Else try again with more space. */
		if (n > -1) 	/* glibc 2.1 */
			nLen = n+1; /* precisely what is needed */
		else  			/* glibc 2.0 */
			nLen *= 2;  /* twice the old nLen */
		
		if( (sVal = (char*)realloc(sVal, nLen)) == NULL)
			return das_error(DASERR_DESC, "Unable to malloc %d bytes", nLen);
	}
	
	DasErrCode nRet = DasDesc_set3(pThis, "String", sName, sVal, UNIT_DIMENSIONLESS);
	free(sVal);
	return nRet;
}

DasErrCode DasDesc_setBool(DasDesc* pThis, const char* sName, bool bVal)
{
	const char* value = "false";
	if(bVal) value = "true";
	return DasDesc_set3(pThis, "boolean", sName, value, UNIT_DIMENSIONLESS);
}

DasErrCode DasDesc_setDatum( 
	DasDesc* pThis, const char* sName, double rVal, das_units units 
) {
    char buf[50] = {'\0'};

    if ( fabs(rVal)>1e10 )
    	sprintf(buf, "%e", rVal);
    else
    	sprintf( buf, "%f", rVal);
    
    return DasDesc_set3( pThis, "Datum", sName, buf, units);
}

DasErrCode DasDesc_setDatumRng(
	DasDesc* pThis, const char * sName, double beg, double end, das_units units 
){
    char buf[50] = {'\0'};

    if ( fabs(beg)>1e10 || fabs(end)>1e10 ) {
        snprintf( buf, 49, "%e to %e", beg, end );
    } else {
        snprintf( buf, 49, "%f to %f", beg, end );
    }
    return DasDesc_set3( pThis, "DatumRange", sName, buf, units);
}

DasErrCode DasDesc_getStrRng(
	DasDesc* pThis, const char* sName, char* sMin, char* sMax, 
	das_units* pUnits, size_t uLen
){
	if(uLen < 2) das_error(DASERR_DESC, "uLen too small (%zu bytes)", uLen);

	const DasProp* pProp = DasDesc_getProp(pThis, sName);

	if(pProp == NULL)
		return das_error(DASERR_DESC, "Property %s not present in descriptor", sName);
	
	char buf[128] = {'\0'};
	char* pBeg = buf;
	char* pEnd = NULL;
	das_time dt = {0};
	
	/* Copy everything up to the first | character */
	const char* sValue = DasProp_value(pProp);
	
	pEnd = (char*)strchr(sValue, '|');
	if(pEnd != NULL)
		strncpy(buf, sValue, pEnd - sValue);
	else
		strncpy(buf, sValue, 127);
	
	if( (pEnd = strchr(pBeg, ' ')) == NULL) goto ERROR;
	if(pEnd == pBeg) goto ERROR;
	
	*pEnd = '\0'; 
	strncpy(sMin, pBeg, uLen - 1);
	pBeg = pEnd+1;
	
	/* Get past the __to__ */
	while( (*pBeg != 't') && (*(pBeg+1) != 'o') ){
		if( (*pBeg == '\0') || (*(pBeg+1) == '\0')) goto ERROR;
		++pBeg;
	}
	pBeg += 2; if(*pBeg == '\0') goto ERROR;
	
	while( isspace(*pBeg)) ++pBeg;  /* Read past whitespace */
	if(*pBeg == '\0') goto ERROR;
	
	if( (pEnd = strchr(pBeg, ' ')) == NULL){ 
		if( (pEnd = strchr(pBeg, '\0')) == NULL) goto ERROR;
	}
	if(pEnd == pBeg) goto ERROR;
	
	*pEnd = '\0'; 
	strncpy(sMax, pBeg, uLen - 1);
	pBeg = pEnd+1;
	
	/* If the end string is 'now' return the max as the current UTC time */
	if(strcasecmp(sMax, "now") == 0){
		dt_now(&dt);
		dt_isoc(sMax, 63, &dt, 0);
	}

	*pUnits = pProp->units;
	return DAS_OKAY;
			
	ERROR:
	return das_error(DASERR_DESC, "Malformed range string %s", buf);	
}


DasErrCode DasDesc_setDouble(DasDesc* pThis, const char* sName, double rVal) 
{
    char buf[50] = {'\0'};
    if ( fabs(rVal)>1e10 ) {
        sprintf( buf, "%e", rVal );
    } else {
        sprintf( buf, "%f", rVal );
    }
    return DasDesc_set3( pThis, "double", sName, buf, UNIT_DIMENSIONLESS);
}

DasErrCode DasDesc_setInt(DasDesc* pThis, const char * sName, int nVal ) 
{
    char buf[50] = {'\0'};
    sprintf( buf, "%d", nVal );
    return DasDesc_set3( pThis, "int", sName, buf, UNIT_DIMENSIONLESS);
}

DasErrCode DasDesc_setDoubleArray(
	DasDesc* pThis, const char* sName, int nitems, double *value 
){
	char* buf;
	if ( nitems> 1000000 / 50 ) {
		das_error(DASERR_DESC, "too many elements for DasDesc_setDoubleArray to handle" );   
	}
	buf= ( char * ) malloc( nitems * 50 );
	int nRet = DasDesc_set3( 
    	pThis, "doubleArray", sName, das_doubles2csv( buf, value, nitems ),
    	UNIT_DIMENSIONLESS
	);
	free( buf );
	return nRet;
}

DasErrCode DasDesc_setFloatAry(
	DasDesc* pThis, const char* sName, int nitems, float* value 
){
	char* buf;
	double dvalue[ 1000000 / 50 ];
	int i;
	if( nitems> 1000000 / 50 )
		das_error(DASERR_DESC, "too many elements for DasDesc_setFloatArray to handle" ); 
    
	for( i=0; i<nitems; i++ )
		dvalue[i]= (double)value[i];
   
	buf= ( char * ) malloc( nitems * 50 );
	int nRet = DasDesc_set3(
		pThis, "doubleArray", sName, das_doubles2csv( buf, dvalue, nitems),
		UNIT_DIMENSIONLESS
	);
	free(buf);
	return nRet;
}

void DasDesc_copyIn(DasDesc* pThis, const DasDesc* pOther)
{
	const DasAry* pSrc = &(pOther->properties);
	
	size_t uProps = DasAry_lengthIn(pSrc, DIM0);
	const DasProp* pProp = NULL;

	for(size_t u = 0; u < uProps; ++u){
		size_t uNewLen = 0;
		pProp = (const DasProp*) DasAry_getBytesIn(pSrc, DIM1_AT(u), &uNewLen);
		if(!DasProp_isValid(pProp))
			continue;

		size_t uOldLen = 0;
		byte* pBuf = _DasDesc_getPropBuf(pThis, DasProp_name(pProp), &uOldLen);
		if(pBuf != NULL){
			if(uNewLen <= uOldLen){
				// Since properties self-null, it's okay to have extra cruft after one of them
				memcpy(pBuf, pProp, uOldLen);
			}
			else{
				DasProp_invalid((DasProp*)pBuf);
			}
		}
		else{
			if(!DasAry_append(&(pThis->properties), NULL, uNewLen)){
				das_error(DASERR_DESC, "Couldn't create space for new property");
				return;
			}
			DasAry_markEnd(&(pThis->properties), DIM1);

			size_t uTmp;
			pBuf = DasAry_getBuf(&(pThis->properties), vtByte, DIM1_AT(-1), &uTmp);
			assert(uTmp >= uNewLen);
			memcpy(pBuf, pProp, uNewLen);
		}
	}
}

/* ************************************************************************* */
/* Removing Properties */

bool DasDesc_remove(DasDesc* pThis, const char* sName)
{
	/* properties aren't removed, just marked invalid */
	size_t uPropSz;
	byte* pBuf = _DasDesc_getPropBuf(pThis, sName, &uPropSz);
	if(pBuf == NULL)
		return false;

	DasProp_invalid((DasProp*)pBuf);
	return true;
}

/* ************************************************************************* */
/* Output, das2 or das3 style */

DasErrCode _DasDesc_encode(
	DasDesc* pThis, DasBuf* pBuf, const char* sIndent, int nVer
){
	const DasAry* pProps = &(pThis->properties);
	const DasProp* pProp = NULL;

	size_t u, uProps = DasAry_lengthIn(pProps, DIM0);
	bool bAnyValid = false;
	for(u = 0; u < uProps; ++u){
		size_t uPropLen = 0;
		pProp = (const DasProp*) DasAry_getBytesIn(pProps, DIM1_AT(u), &uPropLen);
		if(DasProp_isValid(pProp)){
			bAnyValid = true;
			break;
		}
	}
	if(!bAnyValid)
		return DAS_OKAY;

	DasBuf_puts(pBuf, sIndent);
	DasBuf_puts(pBuf, "<properties\n");
	if(nVer > 2)
		DasBuf_puts(pBuf, ">\n");
	
	DasErrCode nRet = DAS_OKAY; 
	for(u = 0; u < uProps; ++u){
		size_t uPropLen = 0;
		pProp = (const DasProp*) DasAry_getBytesIn(pProps, DIM1_AT(u), &uPropLen);
		if(!DasProp_isValid(pProp))
			continue;

		const char* sName = DasProp_name(pProp);

		/* In order to handle some Das1 stuff allow reading odd-ball property
		   names such as label(1), but don't write things like this to disk*/
		for(int j = 0; j < strlen(sName); j++) 
			if(!isalnum(sName[j]) && (sName[j] != '_') && (sName[j] != ':'))
				return das_error(DASERR_DESC, "Invalid property name '%s'", sName);
		
		byte uType = DasProp_type(pProp);

		//const char* sType = DasProp_type2(pProp);

		DasBuf_puts(pBuf, sIndent);

		// Type
		if(nVer > 2){
			DasBuf_puts(pBuf, "  <p");
			if(uType & DASPROP_STRING){
				DasBuf_puts(pBuf, " type=\"");
				DasBuf_puts(pBuf, DasProp_typeStr3(pProp));
				DasBuf_puts(pBuf, "\"");
			}
		}
		else{
			DasBuf_puts(pBuf, "  ");
			if(!(uType & DASPROP_STRING)){
				DasBuf_puts(pBuf, DasProp_typeStr2(pProp));
				DasBuf_puts(pBuf, ":");
			}
		}

		// Name
		if(nVer > 2){
			DasBuf_puts(pBuf, " name=\"");
			DasBuf_puts(pBuf, sName);
			DasBuf_puts(pBuf, "\"");
			if(pProp->units != UNIT_DIMENSIONLESS){
				DasBuf_printf(pBuf, " units=\"%s\"", Units_fromStr(pProp->units));
			}
			DasBuf_puts(pBuf, ">");
		}
		else{
			DasBuf_puts(pBuf, sName);
			DasBuf_puts(pBuf, "=\"");
		}

		// Value
		DasBuf_puts(pBuf, DasProp_value(pProp));

		if(nVer > 3){
			nRet = DasBuf_puts(pBuf, "</p>\n");
		}
		else{
			if(pProp->units != UNIT_DIMENSIONLESS)
				nRet = DasBuf_printf(pBuf, " %s\"\n", Units_fromStr(pProp->units));
			else
				nRet = DasBuf_puts(pBuf, "\"\n");
		}

		if(nRet != DAS_OKAY) return nRet;
	}

	if(nVer > 2)
		return DasBuf_printf(pBuf, "%s/>\n", sIndent);
	else
		return DasBuf_printf(pBuf, "%s/>\n", sIndent);
}

DasErrCode DasDesc_encode(DasDesc* pThis, DasBuf* pBuf, const char* sIndent)
{
	return _DasDesc_encode(pThis, pBuf, sIndent, 2);
}

DasErrCode DasDesc_encode3(DasDesc* pThis, DasBuf* pBuf, const char* sIndent)
{
	return _DasDesc_encode(pThis, pBuf, sIndent, 3);
}
