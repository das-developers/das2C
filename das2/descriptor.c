/* Copyright (C) 2004-2006 Jeremy Faden <jeremy-faden@uiowa.edu> 
 *               2015-2019 Chris Piker <chris-piker@uiowa.edu>
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

/* ************************************************************************* */
/* Construction/Destruction */

void DasDesc_init(DasDesc* pThis, desc_type_t type){
	pThis->type = type;  /* Intentionally invalid */
	memset(pThis->properties, 0, sizeof(char*)*DAS_XML_MAXPROPS);
	pThis->parent = NULL;
	pThis->bLooseParsing = false;
}

DasDesc* new_Descriptor(){
	DasDesc* pThis = (DasDesc*)calloc(1, sizeof(DasDesc));
	pThis->type = UNK_DESC;
	return pThis;
}

void DasDesc_freeProps(DasDesc* pThis){
	for(int i = 0; pThis->properties[i] != NULL; i++){
		free(pThis->properties[i]);
	}
	/* Just get the property strings, don't get the item itself, will mess with
	   sub-classing */
}

/* ************************************************************************* */
/* Ownership */

const DasDesc* DasDesc_parent(DasDesc* pThis)
{
	return pThis->parent;  /* only useful for hiding the structure, not sure
								   * if it's worth it */
}

/* ************************************************************************* */
/* Getting Properties */

/* returns NULL if property does not exist, pointer to string value          */
/* otherwise.  Recursively searches up the parent hierarchy if the given     */

/* property isn't present in this descriptor                                 */

const char* DasDesc_get(const DasDesc* pThis, const char* sKey)
{
	int i;
	char *tt;

	for (i = 0; i < DAS_XML_MAXPROPS; i += 2) {
		if (pThis->properties[i] == NULL) continue;

		tt = strchr(pThis->properties[i], ':');
		if (tt != NULL) {
			if (strcmp((char *) (tt + 1), sKey) == 0) {
				return pThis->properties[i + 1];
			}
		} else {
			if (strcmp(pThis->properties[i], sKey) == 0) {
				return pThis->properties[i + 1];
			}
		}
	}

	if (pThis->parent != NULL) {
		return DasDesc_get(pThis->parent, sKey);
	} else {
		return NULL;
	}
}


const char* DasDesc_getType(const DasDesc* pThis, const char* sKey)
{
	int i;
	char *tt;

	for (i = 0; i < DAS_XML_MAXPROPS; i += 2) {
		if (pThis->properties[i] == NULL) continue;

		tt = strchr(pThis->properties[i], ':');
		if(tt != NULL){
			if(strcmp(tt + 1, sKey) == 0){
				tt = pThis->properties[i]; /* note var reuse */
				if(strncmp(tt, "double", 6) == 0) return "double";
				if(strncmp(tt, "boolean", 7) == 0) return "boolean";
				if(strncmp(tt, "String", 6) == 0) return "String";
				if(strncmp(tt, "DatumRange", 10) == 0) return "DatumRange";
				if(strncmp(tt, "Datum", 5) == 0) return "Datum";
				if(strncmp(tt, "int", 3) == 0) return "int";
				if(strncmp(tt, "doubleArray", 11) == 0) return "doubleArray";
				return "Unknown";
			}
		}
		else{
			if(strcmp(pThis->properties[i], sKey) == 0)
				return "String";
		}
	}

	if (pThis->parent != NULL) {
		return DasDesc_get(pThis->parent, sKey);
	} else {
		return NULL;
	}
}


bool DasDesc_has(const DasDesc* pThis, const char * propertyName ) {
    const char* result = DasDesc_get( pThis, propertyName );
    return result!=NULL;    
}

size_t DasDesc_length(const DasDesc* pThis)
{
	size_t uProps = 0;
	for(size_t u = 0; u < DAS_XML_MAXPROPS; u += 2){
		if(pThis->properties[u] != NULL) uProps += 1;
	}
	return uProps;
}

const char* DasDesc_getNameByIdx(const DasDesc* pThis, size_t uIdx)
{
	if(uIdx*2 >= DAS_XML_MAXPROPS) return NULL;
	size_t u, uPropNum = 0;
	for(u = 0; u < DAS_XML_MAXPROPS; u += 2){
		if(uPropNum == uIdx) break;
		if(pThis->properties[u] != NULL) ++uPropNum;
	}
	
	const char* sName = pThis->properties[u];
	if(sName == NULL) return NULL;
	const char* pColon = strchr(sName, ':');
	if(pColon != NULL) return pColon+1;
	else return sName;
}

const char* DasDesc_getValByIdx(const DasDesc* pThis, size_t uIdx)
{
	if(uIdx*2 + 1 >= DAS_XML_MAXPROPS) return NULL;
	return pThis->properties[uIdx*2 + 1];
}

const char* DasDesc_getTypeByIdx(const DasDesc* pThis, size_t uIdx)
{
	if(uIdx*2 >= DAS_XML_MAXPROPS) return NULL;
	size_t u, uPropNum = 0;
	for(u = 0; u < DAS_XML_MAXPROPS; u += 2){
		if(uPropNum == uIdx) break;
		if(pThis->properties[u] != NULL) ++uPropNum;
	}
	
	/* This is silly, properties should be a small structure of type, name
	 * and value */
	const char* sName = pThis->properties[u];
	if(strncmp(sName, "double", 6) == 0) return "double";
	if(strncmp(sName, "boolean", 7) == 0) return "boolean";
	if(strncmp(sName, "String", 6) == 0) return "String";
	if(strncmp(sName, "DatumRange", 10) == 0) return "DatumRange";
	if(strncmp(sName, "Datum", 5) == 0) return "Datum";
	if(strncmp(sName, "int", 3) == 0) return "int";
	if(strncmp(sName, "doubleArray", 11) == 0) return "doubleArray";
	return "String";
}

const char* DasDesc_getStr(const DasDesc* pThis, const char* propertyName)
{
	return DasDesc_get(pThis, propertyName);
}

bool DasDesc_getBool(DasDesc* pThis, const char* sPropName)
{
	const char* sVal = DasDesc_get(pThis, sPropName);
	if(sVal == NULL) return false;
	if(strlen(sVal) == 0) return false;
	if(isdigit(sVal[0]) && sVal[0] != '0') return true;
	if(strcasecmp("true", sVal) == 0) return true;
	return false;
}

double DasDesc_getDouble(const DasDesc* pThis, const char * propertyName ) {
    double result;
    const char* value;
    value = DasDesc_get( pThis, propertyName );
    if ( value==NULL ) {
        result= DAS_FILL_VALUE;
    } else {
        sscanf( value, "%lf", &result );
    }
    return result;
}

int DasDesc_getInt(const DasDesc* pThis, const char * propertyName ) {
    int result;
    const char * value;
    value = DasDesc_get( pThis, propertyName );
    if ( value==NULL ) {
        /* result= FILL; */
		 result = INT_MIN;
    } else {
       if( sscanf( value, "%d", &result ) != 1){
			 das_error(16, "Can't convert %s to an integer", value);
			 return 0;
		 }
    }
    return result;
}

bool _Desc_looksLikeTime(const char* sVal)
{
	if(strchr(sVal, ':') != NULL) return true;
	if(strchr(sVal, 'T') != NULL) return true;
	return false;
}

double DasDesc_getDatum(DasDesc* pThis, const char * propertyName, 
		                   das_units units )
{
	const char* sVal;
	const char* idx;
	double rValue;
	double rResult;
	das_units unitsVal;
	bool bIsTimeStr = false;
	das_time dt = {0};
	 
	if( (sVal = DasDesc_get(pThis, propertyName )) == NULL) return DAS_FILL_VALUE;
	
	idx= strchr( sVal, ' ' );
	if(idx == NULL){
		/* If the units string is null, assume dimensionless unless the value
			looks like a Time string */
		bIsTimeStr = _Desc_looksLikeTime(sVal);
		
		if(bIsTimeStr)
			unitsVal = UNIT_US2000;
		else
			unitsVal = UNIT_DIMENSIONLESS;
	} 
	else {
		unitsVal= Units_fromStr( idx+1 );
	}
	
	if(!bIsTimeStr){
		if( sscanf(sVal, "%lf", &rValue) != 1){
			das_error(16, "Couldn't parse %s as a real value", sVal);
			return DAS_FILL_VALUE;
		}
		if(strcmp(unitsVal, units) == 0){
			rResult = rValue;
		}
		else{
			if(Units_canConvert(unitsVal, units))
				rResult = Units_convertTo(units, rValue, unitsVal);
			else
				rResult = DAS_FILL_VALUE;
		}
	}
	else{
		if(! dt_parsetime(sVal, &dt) ){
			das_error(16, "Couldn't parse %s as a date time", sVal);
			return DAS_FILL_VALUE;
		}
		rResult = Units_convertFromDt(unitsVal, &dt);
	}
	 
	return rResult;
}

double* DasDesc_getDoubleAry( 
	DasDesc* pThis, const char * propertyName, int *p_nitems 
){
    const char* arrayString = DasDesc_get( pThis, propertyName );
    return das_csv2doubles( arrayString, p_nitems );
}

/* ************************************************************************* */
/* Checking equality of content */

bool DasDesc_equals(const DasDesc* pOne, const DasDesc* pTwo)
{
	size_t uProps = DasDesc_length(pOne);
	if(uProps != DasDesc_length(pTwo)) return false;
	
	const char* sVal = NULL;
	size_t u = 0, v = 0; 
	
	for(u = 0; u < uProps; u++){
		
		sVal = NULL;
		for(v = 0; v < uProps; v++){
			if(pOne->properties[u*2] && pTwo->properties[v*2]){
				if(strcmp(pOne->properties[u*2], pTwo->properties[v*2]) == 0){
					sVal = pTwo->properties[v*2 + 1];
					break;
				}
			}
		}
		
		if(sVal == NULL) return false;
		if(strcmp(pOne->properties[u*2 + 1], sVal) != 0) return false;
	}
	return true;
}


/* ************************************************************************* */
/* Setting Properties */

/* copies the property into the property list. */
DasErrCode DasDesc_set(
	DasDesc* pThis, const char* sType, const char* sName, const char* sVal
){
	if(sType == NULL) sType = "String";
	if(sName == NULL) return das_error(16, "Null value for sName");
	
	int i = 0;
	/* handle odd stuff from Das1 DSDFs only if an internal switch is thrown */
	if(!pThis->bLooseParsing){
		for(i = 0; i < strlen(sName); i++) 
			if(!isalnum(sName[i]) && (sName[i] != '_'))
				return das_error(16, "Invalid property name '%s'", sName);
	}
	else{
		i = strlen(sName);
	}
	
	if(i < 1) 
		return das_error(16, "Property can not be empty");
	
	if(strlen(sType) < 2 ) 
		return das_error(16, "Property type '%s' is too short.", sType);
	
	char** pProps = pThis->properties;
	char sBuf[128] = {'\0'};
	int iProp=-1;
	
	/* Look for the prop string skipping over holes */
	for(i=0; i < DAS_XML_MAXPROPS; i+=2 ){
		if( pProps[i] == NULL ) continue;
		snprintf(sBuf, 128, "%s:%s", sType, sName);
		if (strcmp( pProps[i], sBuf )==0 ) iProp= i;
	}
	
	size_t uLen;
	if(iProp == -1){
		/* Look for the lowest index slot for the property */
		for(i=0; i< DAS_XML_MAXPROPS; i+= 2){
			if( pProps[i] == NULL){ iProp = i; break;}
		}
		if(iProp == -1){
			return das_error(16, "Descriptor exceeds the max number of "
					            "properties %d", DAS_XML_MAXPROPS/2);
		}
		if(sType != NULL){
			uLen = strlen(sType) + strlen(sName) + 2;	
			pProps[iProp] = (char*)calloc(uLen, sizeof(char));
			snprintf(pProps[iProp], uLen, "%s:%s", sType, sName);
		}
		/* pProps[iProp+2]= NULL; */
	} else {
		free( pProps[iProp+1] );
	}

	/* own it */
	if((sVal != NULL) && (strlen(sVal) > 0)){
		pProps[iProp+1]= (char*)calloc(strlen(sVal)+1, sizeof(char));
		strcpy( pProps[iProp+1], sVal );
	}
	else{
		pProps[iProp+1] = NULL;
	}
	return 0;
}

DasErrCode DasDesc_setStr(
	DasDesc* pThis, const char* sName, const char * sVal 
) {
	return DasDesc_set( pThis, "String", sName, sVal);
}

DasErrCode DasDesc_vSetStr(
	DasDesc* pThis, const char* sName, const char* sFmt, ...
) {
	
	/* Guess we need no more than 128 bytes. */
	int n, nLen = 128;
	char* sVal;
	va_list ap;
	
	if( (sVal = malloc(nLen)) == NULL) 
		return das_error(16, "Unable to malloc %d bytes", nLen);
	
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
		
		if( (sVal = realloc(sVal, nLen)) == NULL)
			return das_error(16, "Unable to malloc %d bytes", nLen);
	}
	
	DasErrCode nRet = DasDesc_set(pThis, "String", sName, sVal);
	free(sVal);
	return nRet;
}

DasErrCode DasDesc_setBool(DasDesc* pThis, const char* sPropName, bool bVal)
{
	const char* value = "false";
	if(bVal) value = "true";
	return DasDesc_set(pThis, "boolean", sPropName, value);
}

DasErrCode DasDesc_setDatum( 
	DasDesc* pThis, const char* sName, double rVal, das_units units 
) {
    char buf[50] = {'\0'};

    if ( fabs(rVal)>1e10 ) {
        sprintf(buf, "%e %s", rVal, Units_toStr( units ) );
    } else {
        sprintf( buf, "%f %s", rVal, Units_toStr( units ) );
    }
    return DasDesc_set( pThis, "Datum", sName, buf );
}

DasErrCode DasDesc_setDatumRng(
	DasDesc* pThis, const char * sName, double beg, double end, das_units units 
){
    char buf[50] = {'\0'};

    if ( fabs(beg)>1e10 || fabs(end)>1e10 ) {
        snprintf( buf, 49, "%e to %e %s", beg, end, Units_toStr( units ) );
    } else {
        snprintf( buf, 49, "%f to %f %s", beg, end, Units_toStr( units ) );
    }
    return DasDesc_set( pThis, "DatumRange", sName, buf );
}

DasErrCode DasDesc_getStrRng(
	DasDesc* pThis, const char* sName, char* sMin, char* sMax, 
	das_units* pUnits, size_t uLen
){
	if(uLen < 2) das_error(16, "uLen too small (%zu bytes)", uLen);
	
	const char* sVal = NULL;
	char buf[128] = {'\0'};
	char* pBeg = buf;
	char* pEnd = NULL;
	das_time dt = {0};
	
	/* Copy everything up to the first | character */
	if( (sVal = DasDesc_getStr(pThis, sName)) == NULL){
		return das_error(16, "Property %s not present in descriptor", sName);
	}
	pEnd = strchr(sVal, '|');
	if(pEnd != NULL)
		strncpy(buf, sVal, pEnd - sVal);
	else
		strncpy(buf, sVal, 127);
	
	
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
	
	/* Now for the units, if we are at the end of the string check to see
	 * if the first item is convertable to a dastime, if so call the units
	 * UTC, if not call it dimensionless */
	if(pBeg == NULL){
		if(dt_parsetime(sMin, &dt))
			*pUnits = UNIT_UTC;
		else
			*pUnits = UNIT_DIMENSIONLESS;
	}
	else{
		*pUnits = Units_fromStr(pBeg);
	}
	return DAS_OKAY;
			
	ERROR:
	return das_error(16, "Malformed range string %s", buf);	
}


DasErrCode DasDesc_setDouble(DasDesc* pThis, const char* sName, double rVal) 
{
    char buf[50] = {'\0'};
    if ( fabs(rVal)>1e10 ) {
        sprintf( buf, "%e", rVal );
    } else {
        sprintf( buf, "%f", rVal );
    }
    return DasDesc_set( pThis, "double", sName, buf );
}

DasErrCode DasDesc_setInt(DasDesc* pThis, const char * sName, int nVal ) 
{
    char buf[50] = {'\0'};
    sprintf( buf, "%d", nVal );
    return DasDesc_set( pThis, "int", sName, buf );
}

DasErrCode DasDesc_setDoubleArray(
	DasDesc* pThis, const char * propertyName, int nitems, double *value 
){
    char* buf;
    if ( nitems> 1000000 / 50 ) {
        das_error(16, "too many elements for setPropertyDoubleArray to handle" );   
    }
    buf= ( char * ) malloc( nitems * 50 );
    int nRet = DasDesc_set( pThis, "doubleArray", propertyName, 
			        das_doubles2csv( buf, value, nitems ) );
    free( buf );
	 return nRet;
}

DasErrCode DasDesc_setFloatAry( DasDesc* pThis, const char * propertyName, 
		                      int nitems, float *value ) 
{
    char* buf;
    double dvalue[ 1000000 / 50 ];
    int i;
    if ( nitems> 1000000 / 50 ) {
        das_error(16, "too many elements for setPropertyDoubleArray to handle" ); 
    }
    for ( i=0; i<nitems; i++ ) {
        dvalue[i]= (double)value[i];
    }
    buf= ( char * ) malloc( nitems * 50 );
    int nRet = DasDesc_set( pThis, "doubleArray", propertyName, 
			        das_doubles2csv( buf, dvalue, nitems ) );
	 free(buf);
	 return nRet;
}

void DasDesc_copyIn(DasDesc* pThis, const DasDesc* source ) {
    int i;
    for ( i=0; ( source->properties[i]!=NULL ) && ( i<DAS_XML_MAXPROPS ); i++ ) {
        pThis->properties[i]= (char *)calloc( strlen( source->properties[i] )+1, sizeof(char) );
        strcpy( pThis->properties[i], source->properties[i] );
    }
    if ( i<DAS_XML_MAXPROPS ) pThis->properties[i]=NULL;
}

/* ************************************************************************* */
/* Removing Properties */

bool DasDesc_remove(DasDesc* pThis, const char* propertyName)
{
	/* 1st, do we have it? */
	char** pProps = pThis->properties;
	int i, iRm=-1;
	char* pColon = NULL;
	int nCmpLen = 0, nOffset = 0;
	int nPropSets = 0;
	
	for(i=0;  i<DAS_XML_MAXPROPS; i+=2 ){
		if(pProps[i] == NULL) continue;
		
		nPropSets++;
		
		if( (pColon = strchr(pProps[i], ':')) == NULL){
			nOffset = 0;
			nCmpLen = strlen(pProps[i]);
		}
		else{
			nOffset = (pColon - pProps[i]) + 1;
			nCmpLen = strlen(pProps[i] + nOffset);
		}
		if(strncmp(pProps[i] + nOffset, propertyName, nCmpLen ) == 0){ 
			iRm = i;
			free(pProps[i]);   pProps[i] = NULL;
			free(pProps[i+1]); pProps[i+1] = NULL;
			break;
		}
	}
	
	if( iRm == -1) return false;
	
	/* Move them down if needed */
	for(int i= iRm; i < 2*(nPropSets-1); i+=2){
		
		pProps[i]   = pProps[i+2];
		pProps[i+1] = pProps[i+3];
		
	}
	return true;
}

/* ************************************************************************* */
/* Output */

DasErrCode DasDesc_encode(DasDesc* pThis, DasBuf* pBuf, const char * indent)
{
	char** pProps = pThis->properties;
	
	if(*pProps == NULL) return 0; /* Successfully did nothing! */
	
	DasErrCode nRet = 0;
	if((nRet = DasBuf_printf(pBuf, "%s<properties", indent)) != 0) return nRet;
	
	int i, j;
	for(i = 0; pProps[i] != NULL; i += 2){
		
		/* In order to handle some Das1 stuff allow reading odd-ball property
		   names such as label(1), but don't write things like this to disk*/
		for(j = 0; j < strlen(pProps[i]); j++) 
		if(!isalnum(pProps[i][j]) && (pProps[i][j] != '_') && (pProps[i][j] != ':'))
			return das_error(16, "Invalid property name '%s'", pProps[i]);
		
		if(i == 0){
			if(pProps[i+1] == NULL)
				nRet = DasBuf_printf(pBuf, " %s=\"\"", pProps[i]);
			else
				nRet = DasBuf_printf(pBuf, " %s=\"%s\"", pProps[i], pProps[i+1]);
		}
		else{
			if(pProps[i+1] == NULL)
				nRet = DasBuf_printf(pBuf, "\n%s            %s=\"\"", indent, pProps[i]);
			else
				nRet = DasBuf_printf(pBuf, "\n%s            %s=\"%s\"", indent, 
			                        pProps[i], pProps[i+1]);
		}
		if(nRet != 0) return nRet;
	}
	return DasBuf_printf(pBuf, "/>\n");
}




