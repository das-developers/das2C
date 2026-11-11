/* Copyright (C) 2024 Chris Piker <chris-piker@uiowa.edu> (re-written, new storage)
 *
 * This file is part of das2C, the Core Das C Library.
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
#include "time.h"
#include "str.h"
#include "enc.h"
#include "value.h"
#include "desc.h"
#include "desc_p.h"
#include "prop_p.h"
#include "info_p.h"

const char* das_desc_type_toStr(das_desc_type_e dt){
	switch(dt){
	case DASDESC_STREAM:   return "stream";
	case DASDESC_PLANE:    return "plane";
	case DASDESC_PKT:   return "packet";
	case DASDESC_DIM:  return "physdim";
	case DASDESC_DS:  return "dataset";
	default: return "unknown";
	}
}

/* ************************************************************************* */
/* Construction/Destruction */

void DasDesc_init(DasDesc* pThis, das_desc_type_e dt, void (*pfDel)(DasDesc*)){
	memset(pThis, 0, sizeof(DasDesc));
	pThis->type = dt;  /* Intentionally invalid */
	pThis->uMagic = DASDESC_MAGIC;
	pThis->pfDel = pfDel;

	const char* sId;
	switch(dt){
	case DASDESC_STREAM:   sId = "stream_properties";    break;
	case DASDESC_PLANE:    sId = "plane_properties";     break;
	case DASDESC_PKT:   sId = "packet_properties";    break;
	case DASDESC_DIM:  sId = "physdim_properties";   break;
	case DASDESC_DS:  sId = "dataset_properties";   break;
	default:       sId = "desciptor_properties"; break;
	}
	ubyte zero = 0;

	init_DasAry(&(pThis->properties), sId, vtUByte, 0, &zero, RANK_2(0,0), NULL);
	DasAry_setUsage(&(pThis->properties), DASARY_AS_SUBSEQ);

	pThis->parent = NULL;
	pThis->bLooseParsing = false;
	pThis->bNoInherit = false;
	pThis->uInvalid = 0;
}

/* Destructor for a bare descriptor from new_DasDesc() */
static void _del_DasDesc(DasDesc* pThis){
	DasDesc_freeProps(pThis);
	free(pThis);
}

DasDesc* new_DasDesc(){
	DasDesc* pThis = (DasDesc*)calloc(1, sizeof(DasDesc));
	pThis->type = DASDESC_UNK;
	pThis->uMagic = DASDESC_MAGIC;
	pThis->pfDel = _del_DasDesc;
	ubyte zero = 0;
	init_DasAry(
		&(pThis->properties), "descriptor_properties", vtUByte, 0, &zero, RANK_2(0,0),
		UNIT_DIMENSIONLESS
	); 
	DasAry_setUsage(&(pThis->properties), DASARY_AS_SUBSEQ);
	return pThis;
}

void del_DasDesc(DasDesc* pThis){
	if(pThis == NULL) return;

	/* Callers cast to DasDesc*, so the compiler can't tell us this is a
	   descriptor at all. Add a probabilistic guard against a wrong cast */
	if(pThis->uMagic != DASDESC_MAGIC){
		das_error(DASERR_DESC,"Object at %p is not a descriptor", (void*)pThis);
		return;
	}
	if(pThis->pfDel == NULL){
		das_error(DASERR_DESC,
			"Destructor pointer missing for descriptor at %p", (void*)pThis
		);
		return;
	}
	pThis->pfDel(pThis);
}

void DasDesc_freeProps(DasDesc* pThis){
	fini_DasAry(&(pThis->properties));
	pThis->uInvalid = 0;
}

void DasDesc_clearProps(DasDesc* pThis){
	DasAry_clear(&(pThis->properties));
	pThis->uInvalid = 0;
}

/* ************************************************************************* */
/* Property summaries */

int DasDesc_info(const DasDesc* pThis, const char* sIndent, char* sBuf, size_t uLen)
{
	if(uLen > 0) sBuf[0] = '\0';
	int n = 0;

	const DasAry* pProps = &(pThis->properties);
	size_t uProps = DasAry_lengthIn(pProps, DIM0);

	size_t uPropLen = 0;
	for(size_t u = 0; u < uProps; ++u){
		const DasProp* pProp = (const DasProp*) DasAry_getBytesIn(
			pProps, DIM1_AT(u), &uPropLen
		);
	
		if(!DasProp_isValid(pProp))
			continue;

		n = _prn_catf(sBuf, uLen, n, "%sProperty: %s | %s | ", sIndent, 
			pProp->buffer, DasProp_typeStr3(pProp)
		);

		/* Long values are clipped, this is a summary, not a serialization */
		const char* sVal = DasProp_value(pProp);
		if(strlen(sVal) > 48)
			n = _prn_catf(sBuf, uLen, n, "%.48s...", sVal);
		else
			n = _prn_catf(sBuf, uLen, n, "%s", sVal);

		if((pProp->units != NULL)&&(pProp->units != UNIT_DIMENSIONLESS))
			n = _prn_catf(sBuf, uLen, n, " (%s)", Units_toStr(pProp->units));

		n = _prn_catf(sBuf, uLen, n, "\n");
	}
	return n;
}

/* ************************************************************************* */
/* Ownership */

das_desc_type_e DasDesc_type(const DasDesc* pThis){ return pThis->type; }

void DasDesc_setUser(DasDesc* pThis, void* pUser){ pThis->pUser = pUser; }

void* DasDesc_getUser(const DasDesc* pThis){ return pThis->pUser; }

const DasDesc* DasDesc_parent(const DasDesc* pThis)
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
			(strcmp(pProp->buffer, sName) == 0)
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

	if ((pThis->parent != NULL) && (!pThis->bNoInherit))
		return DasDesc_getProp(pThis->parent, sName);
	
	return NULL;
}

/* The uIdx'th live property, skipping retired slots.  uInvalid keeps the
   usual case, nothing ever replaced or removed, a direct lookup. */
static const DasProp* _DasDesc_liveAt(const DasDesc* pThis, size_t uIdx)
{
	const DasAry* pProps = &(pThis->properties);
	size_t uSlots = DasAry_lengthIn(pProps, DIM0);
	if(uIdx >= uSlots - pThis->uInvalid)
		return NULL;

	size_t uPropLen;
	if(pThis->uInvalid == 0)
		return (const DasProp*)DasAry_getBytesIn(pProps, DIM1_AT(uIdx), &uPropLen);

	for(size_t u = 0; u < uSlots; ++u){
		const DasProp* pProp = (const DasProp*)DasAry_getBytesIn(
			pProps, DIM1_AT(u), &uPropLen
		);
		if(!DasProp_isValid(pProp)) continue;
		if(uIdx == 0) return pProp;
		--uIdx;
	}
	return NULL;   /* uInvalid out of step with the slots, a library bug */
}

const DasProp* DasDesc_getPropByIdx(const DasDesc* pThis, size_t uIdx)
{
	return _DasDesc_liveAt(pThis, uIdx);
}



/* returns NULL if property does not exist, pointer to string value          */
/* otherwise.  Recursively searches up the parent hierarchy if the given     */

/* property isn't present in this descriptor                                 */

size_t DasDesc_getArray(
	DasDesc* pThis, const char* sName, char cSep,
	char* pBuf, size_t uBufSz, char** psVals, size_t uMaxVals
){
	if((uBufSz < 2)||(uMaxVals < 1)) return 0;
	if(isspace(cSep)){
		das_error(DASERR_DESC, 
			"Space separators not supported, since functions trims each output"
		);
		return 0;
	}
	
	const char* sVal = DasDesc_getStr(pThis, sName);
	if(sVal == NULL)
		return 0;

	size_t uLen = strlen(sVal);
	if(uLen > uBufSz - 2) uLen = uBufSz - 2;
	
	*pBuf = cSep;  /* Add separator to first buffer */
	strncpy(pBuf+1, sVal, uLen); pBuf[uLen] = '\0';
	
	size_t u = 0;       /* Initial value is separator at start of buffer */
	psVals[u] = pBuf;
	
	char* p = pBuf;     /* All other values start on separators */
	while((*p != '\0')||(u < uMaxVals-1)){
		if(*p == cSep){  psVals[u] = p;  ++u;  }
		++p;
	}
	uMaxVals = u;  /* Reduce value count to what we measured */
	
	/* point value begin after the separator, if there is nothing after the
	 * separator but an ending, mark the value as null */
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

bool DasDesc_hasLocal(const DasDesc* pThis, const char* sName)
{
	return (DasDesc_getLocal(pThis, sName) != NULL);
}

size_t DasDesc_length(const DasDesc* pThis)
{
	/* Live properties only, retired slots are counted as they die */
	return DasAry_lengthIn(&(pThis->properties), DIM0) - pThis->uInvalid;
}

const char* DasDesc_getNameByIdx(const DasDesc* pThis, size_t uIdx)
{
	const DasProp* pProp = _DasDesc_liveAt(pThis, uIdx);
	return (pProp == NULL) ? NULL : DasProp_name(pProp);
}

const char* DasDesc_getValByIdx(const DasDesc* pThis, size_t uIdx)
{
	const DasProp* pProp = _DasDesc_liveAt(pThis, uIdx);
	return (pProp == NULL) ? NULL : DasProp_value(pProp);
}

const char* DasDesc_getTypeByIdx(const DasDesc* pThis, size_t uIdx)
{
	const DasProp* pProp = _DasDesc_liveAt(pThis, uIdx);
	return (pProp == NULL) ? NULL : DasProp_typeStr2(pProp);
}

const char* DasDesc_getTypeByIdx3(const DasDesc* pThis, size_t uIdx)
{
	const DasProp* pProp = _DasDesc_liveAt(pThis, uIdx);
	return (pProp == NULL) ? NULL : DasProp_typeStr3(pProp);
}

const char* DasDesc_getStr(const DasDesc* pThis, const char* sName)
{
	const DasProp* pProp = DasDesc_getProp(pThis, sName);
	if(pProp != NULL)
		return DasProp_value(pProp);

	return NULL;
}

size_t DasDesc_getStrAry(
	DasDesc* pThis, const char* sName, char* pBuf, size_t uBufSz,
	char** psVals, size_t uMaxVals
){
	const DasProp* pProp = DasDesc_getProp(pThis, sName);
	if(pProp == NULL) return 0;

	char cSep = DasProp_sep(pProp);

	/* Single-valued properties record no separator. 
	   going with a default of `|` here for old das2 DSDF reading */
	if(cSep == '\0') cSep = '|';

	return DasDesc_getArray(pThis, sName, cSep, pBuf, uBufSz, psVals, uMaxVals);
}

bool DasDesc_getBool(DasDesc* pThis, const char* sName)
{
	const char* sVal = DasDesc_getStr(pThis, sName);
	if(sVal == NULL) return false;
	if(strlen(sVal) == 0) return false;
	if(isdigit(sVal[0]) && sVal[0] != '0') return true;
	if(strcasecmp("true", sVal) == 0) return true;
	return false;
}

double DasDesc_getDouble(const DasDesc* pThis, const char* sName)
{
	double rVal;
	const char* sVal = DasDesc_getStr( pThis, sName );
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
	const char* sVal = DasDesc_getStr( pThis, sName );
	if(sVal == NULL)
		return INT_MIN;

	if( sscanf(sVal, "%d", &nVal) != 1){
		das_error(DASERR_DESC, "Can't convert %s to an integer", sVal);
		return INT_MIN;
	}
   
	return nVal;
}

static bool _Desc_looksLikeTime(const char* sVal)
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
			if(!dt_parsetime(&dt, sValue)){
				das_error(DASERR_DESC, "Couldn't parse %s as a date time", sValue);
				return DAS_FILL_VALUE;	
			}
			return Units_convertFromDt(&dt, pProp->units);
		}
	}

	double rValue;
	if( sscanf(sValue, "%lf", &rValue) != 1){
		das_error(DASERR_DESC, "Couldn't parse %s as a real value", sValue);
		return DAS_FILL_VALUE;
	}
	
	return Units_convertTo(rValue, pProp->units, units);
}

double* DasDesc_getDoubleAry(DasDesc* pThis, const char* sName, int* pNumItems)
{
	const char* sAry = DasDesc_getStr(pThis, sName);
	if(sAry == NULL){
		if(pNumItems != NULL) *pNumItems = 0;
		das_error(DASERR_DESC, "No property named '%s'", sName);
		return NULL;
	}
	return das_csv2doubles(sAry, pNumItems);
}

/* ************************************************************************* */
/* Checking equality of content in this descriptor */

bool DasDesc_equals(const DasDesc* pThis, const DasDesc* pOther)
{
	const DasAry* pMyAry = &(pThis->properties);

	/* Same live properties, whatever either side's replace history */
	if(DasDesc_length(pThis) != DasDesc_length(pOther))
		return false;

	size_t uProps = DasAry_lengthIn(pMyAry, DIM0);
	for(size_t u = 0; u < uProps; u++){
		size_t uPropLen;
		const DasProp* pMyProp = (const DasProp*)DasAry_getBytesIn(pMyAry, DIM1_AT(u), &uPropLen);
		if(!DasProp_isValid(pMyProp)) continue;
		const DasProp* pYourProp = DasDesc_getLocal(pOther, pMyProp->buffer);
		
		if(!DasProp_equal(pMyProp, pYourProp))
			return false;
	}
	return true;
}

/* ************************************************************************* */
/* Setting Properties */

/* Get pointer to the LIVE property slot with this name, or NULL.
 *
 * Dead slots are skipped on purpose.  Growing a value leaves its old slot
 * in place (invalidated) and appends a fresh one, so the first slot that
 * matches the name is not always the current value. */
static ubyte* _DasDesc_getPropBuf(
	DasDesc* pThis, const char* sName, size_t* pPropSz, size_t* pIdx
){
	DasAry* pProps = &(pThis->properties);
	size_t nProps = DasAry_lengthIn(pProps, DIM0);
	DasProp* pProp;

	for(size_t i = 0; i < nProps; ++i){
		pProp = (DasProp*) DasAry_getBuf(pProps, vtUByte, DIM1_AT(i), pPropSz);
		if(DasProp_isValid(pProp) && (strcmp(pProp->buffer, sName) == 0)){
			if(pIdx != NULL) *pIdx = i;
			return (ubyte*)pProp;
		}
	}
	return NULL;
}

/* Get a slot for a property the caller has already checked, so whatever
 * is written into it will be valid.  A live slot of the same name that is
 * big enough is reused in place.  Otherwise a new slot is appended and the
 * old one is retired only after that succeeds, so a failed append leaves
 * the old value alone. */
static ubyte* _DasDesc_getWriteBuf(DasDesc* pThis, const char* sName, size_t uNeedSz)
{	
	size_t uOldSz = 0;
	size_t uOldIdx = 0;
	ubyte* pBuf = _DasDesc_getPropBuf(pThis, sName, &uOldSz, &uOldIdx);
	if((pBuf != NULL) && (uNeedSz <= uOldSz))
		return pBuf;
	bool bRetire = (pBuf != NULL);

	// Make a new one
	DasAry* pProps = &(pThis->properties);

	if(!DasAry_append(pProps, NULL, uNeedSz)){
		das_error(DASERR_DESC, "Couldn't create space for new property");
		return NULL;
	}
	DasAry_markEnd(pProps, DIM1);

	size_t uTmp = 0;
	if(bRetire){
		/* The append may have moved the storage, look the old slot up again */
		pBuf = DasAry_getBuf(pProps, vtUByte, DIM1_AT((ptrdiff_t)uOldIdx), &uTmp);
		DasProp_invalidate((DasProp*)pBuf);
		++(pThis->uInvalid);
	}

	pBuf = DasAry_getBuf(pProps, vtUByte, DIM1_AT(-1), &uTmp);
	assert(uTmp == uNeedSz);
	return pBuf;
}

/* True if p points into this descriptor's own property storage.  Slots are
   packed back to back, so the start of the first and the end of the last
   bound it. */
static bool _DasDesc_ownsPtr(DasDesc* pThis, const void* p)
{
	DasAry* pProps = &(pThis->properties);
	if((p == NULL)||(DasAry_lengthIn(pProps, DIM0) == 0))
		return false;

	size_t uLen = 0;
	const ubyte* pBeg  = DasAry_getBuf(pProps, vtUByte, DIM1_AT(0), &uLen);
	const ubyte* pLast = DasAry_getBuf(pProps, vtUByte, DIM1_AT(-1), &uLen);
	uintptr_t uPtr = (uintptr_t)p;
	return (uPtr >= (uintptr_t)pBeg) && (uPtr < (uintptr_t)(pLast + uLen));
}

/* Heap copy of a string, NULL on failure (strdup is not C99) */
static char* _DasDesc_strcopy(const char* s)
{
	size_t uLen = strlen(s) + 1;
	char* sCopy = (char*)malloc(uLen);
	if(sCopy != NULL) memcpy(sCopy, s, uLen);
	return sCopy;
}

static das_errcode _DasDesc_flexSet(
	DasDesc* pThis, const char* sType, ubyte uType, const char* sName, 
	const char* sVal, char cSep, das_units units, int nStandard
){
	/* Check before taking a slot, the property array can't give one back */
	das_propplan plan;
	das_errcode nRet = dasprop_plan(
		&plan, sType, uType, sName, sVal, cSep, units, nStandard
	);
	if(nRet != DAS_OKAY) return nRet;

	size_t uPropSz = dasprop_memsz(sName, sVal);

	ubyte* pBuf = _DasDesc_getWriteBuf(pThis, sName, uPropSz);
	if(pBuf == NULL){
		return das_error(DASERR_DESC, 
			"Couldn't get write buffer for property %s of size %zu", sName, 
			uPropSz
		);
	}

	dasprop_write(&plan, pBuf);
	return DAS_OKAY;
}

/* copies the property into the property array */
das_errcode DasDesc_flexSet(
	DasDesc* pThis, const char* sType, ubyte uType, const char* sName, 
	const char* sVal, char cSep, das_units units, int nStandard
){
	/* A name or value read from this same descriptor lives in storage that
	   taking a new slot may move, das2_from_cdf sets ySummary from yLabel
	   for example.  That's rare, so only then pay for a copy. */
	bool bName = _DasDesc_ownsPtr(pThis, sName);
	bool bVal  = _DasDesc_ownsPtr(pThis, sVal);
	if(!bName && !bVal)
		return _DasDesc_flexSet(pThis, sType, uType, sName, sVal, cSep, units, nStandard);

	char* sNameCopy = bName ? _DasDesc_strcopy(sName) : NULL;
	char* sValCopy  = bVal  ? _DasDesc_strcopy(sVal)  : NULL;
	das_errcode nRet;
	if((bName && (sNameCopy == NULL))||(bVal && (sValCopy == NULL)))
		nRet = das_error(DASERR_DESC, "Couldn't copy a property from its own descriptor");
	else
		nRet = _DasDesc_flexSet(pThis, sType, uType, bName ? sNameCopy : sName,
			bVal ? sValCopy : sVal, cSep, units, nStandard
		);
	free(sNameCopy);
	free(sValCopy);
	return nRet;
}

das_errcode DasDesc_setProp(DasDesc* pThis, const DasProp* pProp)
{
	size_t uPropSz = DasProp_size(pProp);
	const char* sName = pProp->buffer;

	/* Same hazard as DasDesc_flexSet(), the source may move with the append */
	ubyte* pCopy = NULL;
	if(_DasDesc_ownsPtr(pThis, pProp)){
		if(DasDesc_getLocal(pThis, sName) == pProp)
			return DAS_OKAY;  /* set onto itself */
		if((pCopy = (ubyte*)malloc(uPropSz)) == NULL)
			return das_error(DASERR_DESC, "Couldn't copy property %s", sName);
		memcpy(pCopy, pProp, uPropSz);
		pProp = (const DasProp*)pCopy;
		sName = pProp->buffer;
	}

	das_errcode nRet = DAS_OKAY;
	ubyte* pBuf = _DasDesc_getWriteBuf(pThis, sName, uPropSz);
	if(pBuf == NULL)
		nRet = das_error(DASERR_DESC, 
			"Couldn't get write buffer for property %s of size %zu", 
			sName, uPropSz
		);
	else
		memcpy(pBuf, pProp, uPropSz);

	free(pCopy);
	return nRet;
}

das_errcode DasDesc_set(
	DasDesc* pThis, const char* sType, const char* sName, const char* sVal
){
	int nStandard = pThis->bLooseParsing ? DASPROP_DAS1 : DASPROP_DAS2;
	return DasDesc_flexSet(pThis, sType, '\0', sName, sVal, ',', NULL, nStandard);
}


das_errcode DasDesc_setStr(
	DasDesc* pThis, const char* sName, const char * sVal 
) {
	return DasDesc_flexSet(
		pThis, NULL, DASPROP_STRING|DASPROP_SINGLE, sName, sVal, 0, NULL, 
		DASPROP_DAS3
	);
}

das_errcode DasDesc_vSetStr(
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
	
	das_errcode nRet = DasDesc_flexSet(
		pThis, NULL, DASPROP_STRING|DASPROP_SINGLE, sName, sVal, 0, NULL, 
		DASPROP_DAS3
	);

	free(sVal);
	return nRet;
}

das_errcode DasDesc_setBool(DasDesc* pThis, const char* sName, bool bVal)
{
	return DasDesc_flexSet(
		pThis, NULL, DASPROP_BOOL|DASPROP_SINGLE, sName, 
		bVal ? "true" : "false", 0, NULL, DASPROP_DAS3
	);
}

das_errcode DasDesc_setDatum( 
	DasDesc* pThis, const char* sName, double rVal, das_units units 
) {
	char sVal[50] = {'\0'};

	if(fabs(rVal) > 1e10)
		sprintf(sVal, "%e", rVal);
	else
		sprintf(sVal, "%f", rVal);

	return DasDesc_flexSet(
		pThis, NULL, DASPROP_REAL|DASPROP_SINGLE, sName, sVal, 0, units, DASPROP_DAS3
	);
}

das_errcode DasDesc_setDatumRng(
	DasDesc* pThis, const char * sName, double beg, double end, das_units units 
){
	char sVal[50] = {'\0'};

	if(fabs(beg) > 1e10 || fabs(end) > 1e10)
		snprintf(sVal, 49, "%e to %e", beg, end );
	else
		snprintf(sVal, 49, "%f to %f", beg, end );

	return DasDesc_flexSet(
		pThis, NULL, DASPROP_REAL|DASPROP_RANGE, sName, sVal, 0, units, DASPROP_DAS3
	);
}

das_errcode DasDesc_getStrRng(
	DasDesc* pThis, const char* sName, char* sMin, char* sMax, 
	das_units* pUnits, size_t uLen
){
	if(uLen < 2) return das_error(DASERR_DESC, "uLen too small (%zu bytes)", uLen);

	const DasProp* pProp = DasDesc_getProp(pThis, sName);

	if(pProp == NULL)
		return das_error(DASERR_DESC, "Property %s not present in descriptor", sName);

	if(!DasProp_isRange(pProp))
		return das_error(DASERR_DESC, "Property %s is not a Range", sName);
	
	char buf[128] = {'\0'};
	char* pBeg = buf;
	char* pEnd = NULL;
	das_time dt = {0};
	
	/* Copy everything up to the old das2 comment section */
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
		dt_isoc(&dt, 0, sMax, 63);
	}

	*pUnits = pProp->units;
	return DAS_OKAY;
			
	ERROR:
	return das_error(DASERR_DESC, "Malformed range string %s", buf);	
}

das_errcode DasDesc_setDouble(DasDesc* pThis, const char* sName, double rVal) 
{
	/* Keep the precision the value has. trimming significant digits for
	   display is a text formatters' job */
	char sVal[50] = {'\0'};
	snprintf(sVal, 49, "%.15g", rVal);

	return DasDesc_flexSet(
		pThis, NULL, DASPROP_REAL|DASPROP_SINGLE, sName, sVal, 0, NULL, DASPROP_DAS3
	);
}

das_errcode DasDesc_setInt(DasDesc* pThis, const char* sName, int nVal) 
{
	char sVal[50] = {'\0'};
	snprintf(sVal, 49, "%d", nVal);
	return DasDesc_flexSet(
		pThis, NULL, DASPROP_INT|DASPROP_SINGLE, sName, sVal, 0, NULL, DASPROP_DAS3
	);
}

das_errcode DasDesc_setDoubleArray(
	DasDesc* pThis, const char* sName, int nItems, double* pValues
){

	char* sVal = (char *)calloc(nItems, 50);
	if(sVal == NULL){
		return das_error(DASERR_DESC, "Failed to allocate %d bytes for %d items", 
			nItems*50, nItems
		);
	}
	das_doubles2csv(pValues, nItems, sVal, nItems*50);

	das_errcode nRet = DasDesc_flexSet(
		pThis, NULL, DASPROP_REAL|DASPROP_SET, sName, sVal, ',', NULL, DASPROP_DAS3
	);
	free(sVal);
	return nRet;
}

das_errcode DasDesc_setFloatAry(
	DasDesc* pThis, const char* sName, int nItems, float* pValues
){ 
	char* sVal = (char*)calloc(nItems, 50);
	if(sVal == NULL){
		return das_error(DASERR_DESC, "Failed to allocate %d bytes for %d items", 
			nItems*50, nItems
		);
	}
	das_floats2csv(pValues, nItems, sVal, nItems*50);
	
	das_errcode nRet = DasDesc_flexSet(
		pThis, NULL, DASPROP_REAL|DASPROP_SET, sName, sVal, ',', NULL, DASPROP_DAS3
	);
	free(sVal);
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

		/* setProp reuses a same-named slot that's big enough, otherwise it
		   invalidates the old one and appends */
		if(DasDesc_setProp(pThis, pProp) != DAS_OKAY)
			return;
	}
}

/* ************************************************************************* */
/* Removing Properties */

bool DasDesc_remove(DasDesc* pThis, const char* sName)
{
	/* properties aren't removed, just marked invalid */
	size_t uPropSz;
	ubyte* pBuf = _DasDesc_getPropBuf(pThis, sName, &uPropSz, NULL);
	if(pBuf == NULL)
		return false;

	DasProp_invalidate((DasProp*)pBuf);
	++(pThis->uInvalid);
	return true;
}

/* ************************************************************************* */
/* Output, das2 or das3 style */

/* Write a property value, inserting a newline after a separator whenever the
   stringArray item just written exceeded 40 chars -- keeps long notes (e.g.
   VAR_NOTES) readable on the wire.  Only real string sets with a non-blank
   separator are wrapped: a whitespace separator (space, newline, tab) would
   make the inserted newline read as an empty item.  Never wrapped when the
   separator is an XML metacharacter (so the scan can't trip over an escape
   sequence). */
static das_errcode _writePropVal(DasBuf* pBuf, const DasProp* pProp, const char* sVal)
{
	char cSep = DasProp_sep(pProp);
	if( !DasProp_isSet(pProp) || !DasProp_isType(pProp, DASPROP_STRING)
	    || (cSep == '\0') || (cSep == ' ') || (cSep == '\n') || (cSep == '\t') || (cSep == '\r')
	    || (cSep=='<')||(cSep=='>')||(cSep=='&')||(cSep=='"')||(cSep=='\'') )
		return DasBuf_puts(pBuf, sVal);

	das_errcode nRet = DAS_OKAY;
	size_t uRun = 0;
	for(const char* p = sVal; (*p != '\0') && (nRet == DAS_OKAY); ++p){
		nRet = DasBuf_write(pBuf, p, 1);
		if(*p == cSep){
			if((uRun > 40) && (nRet == DAS_OKAY)) nRet = DasBuf_write(pBuf, "\n", 1);
			uRun = 0;
		}
		else ++uRun;
	}
	return nRet;
}

/* bWrap: emit the <properties> wrapper (the classic form).  Stream context
   entries ARE property arrays, so their <p> children ride bare and they pass
   false. */
static das_errcode _DasDesc_encodeMaybeWrap(
	DasDesc* pThis, DasBuf* pBuf, const char* sIndent, int nVer, bool bWrap
){
	const DasAry* pProps = &(pThis->properties);
	const DasProp* pProp = NULL;

	// Hate putting big stuff on the stack, use small vector assumption
#define _STACK_BUF_LEN 256
	char sStaticBuf[_STACK_BUF_LEN] = {'\0'};

	size_t u, uProps = DasAry_lengthIn(pProps, DIM0);

	if(DasDesc_length(pThis) == 0)
		return DAS_OKAY;

	if(bWrap){
		DasBuf_puts(pBuf, sIndent);
		if(nVer > 2)
			DasBuf_puts(pBuf, "<properties>\n");
		else
			DasBuf_puts(pBuf, "<properties\n");
	}
	
	das_errcode nRet = DAS_OKAY; 
	for(u = 0; u < uProps; ++u){
		size_t uPropLen = 0;
		pProp = (const DasProp*) DasAry_getBytesIn(pProps, DIM1_AT(u), &uPropLen);
		if(!DasProp_isValid(pProp))
			continue;

		const char* sName = pProp->buffer;

		/* In order to handle some Das1 stuff allow reading odd-ball property
		   names such as label(1), but don't write things like this to disk*/
		for(int j = 0; j < strlen(sName); j++) 
			if(!isalnum(sName[j]) && (sName[j] != '_') && (sName[j] != ':'))
				return das_error(DASERR_DESC, "Invalid property name '%s'", sName);
		
		//const char* sType = DasProp_type2(pProp);

		DasBuf_puts(pBuf, sIndent);

		// Type (omit only for a plain single string, the das3 default)
		if(nVer > 2){
			DasBuf_puts(pBuf, "  <p");
			if(!DasProp_isType(pProp, DASPROP_STRING) || DasProp_isSet(pProp)){
				DasBuf_puts(pBuf, " type=\"");
				DasBuf_puts(pBuf, DasProp_typeStr3(pProp));
				DasBuf_puts(pBuf, "\"");
			}
		}
		else{
			DasBuf_puts(pBuf, "  ");
			if(! DasProp_isType(pProp, DASPROP_STRING)){
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
				DasBuf_printf(pBuf, " units=\"%s\"", Units_toStr(pProp->units));
			}
			/* Emit the terminator for sets when it's a non-whitespace byte (whitespace
			   is the default and needs no attribute); escape non-printables, the
			   inverse of dasprop_unescapeSep(). */
			if(DasProp_isSet(pProp)){
				char cSep = DasProp_sep(pProp);
				if((cSep != ' ') && (cSep != '\0')){
					DasBuf_puts(pBuf, " term=\"");
					switch(cSep){
					case '\n': DasBuf_puts(pBuf, "\\n"); break;
					case '\t': DasBuf_puts(pBuf, "\\t"); break;
					case '\r': DasBuf_puts(pBuf, "\\r"); break;
					case '\\': DasBuf_puts(pBuf, "\\\\"); break;
					default:   DasBuf_write(pBuf, &cSep, 1); break;
					}
					DasBuf_puts(pBuf, "\"");
				}
			}
			DasBuf_puts(pBuf, ">");
		}
		else{
			DasBuf_puts(pBuf, sName);
			DasBuf_puts(pBuf, "=\"");
		}

		// Value, needs to be XML escaped, see if we have to use the big-buf
		size_t uEscapeSz = DasProp_escapeSize(pProp);
		if(uEscapeSz == 0){
			_writePropVal(pBuf, pProp, DasProp_value(pProp));
		}
		else{
			if((uEscapeSz + 1) > _STACK_BUF_LEN){
				// Inefficient local handling. If you end up with a bunch of giant XML
				// properties this can be converted to a "high-water-mark" dynabuf
				char* sDynaBuf = (char*)calloc(uEscapeSz, 1);
				if(sDynaBuf == NULL)
					return das_error(DASERR_DESC,
						"Failed to allocate %zu bytes for XML translation buffer", uEscapeSz
					);
				_writePropVal(pBuf, pProp, DasProp_xmlValue(pProp, sDynaBuf, uEscapeSz));
				free(sDynaBuf);
			}
			else{
				memset(sStaticBuf, 0, _STACK_BUF_LEN);
				_writePropVal(pBuf, pProp, DasProp_xmlValue(pProp, sStaticBuf, _STACK_BUF_LEN-1));
			}
		}

		if(nVer > 2){
			nRet = DasBuf_puts(pBuf, "</p>\n");
		}
		else{
			if(pProp->units != UNIT_DIMENSIONLESS)
				nRet = DasBuf_printf(pBuf, " %s\"\n", Units_toStr(pProp->units));
			else
				nRet = DasBuf_puts(pBuf, "\"\n");
		}

		if(nRet != DAS_OKAY) return nRet;
	}

	if(!bWrap)
		return DAS_OKAY;

	DasBuf_puts(pBuf, sIndent);
	if(nVer > 2)
		return DasBuf_puts(pBuf, "</properties>\n");
	else
		return DasBuf_puts(pBuf, "/>\n");
}

das_errcode DasDesc_encode2(DasDesc* pThis, DasBuf* pBuf, const char* sIndent)
{
	return _DasDesc_encodeMaybeWrap(pThis, pBuf, sIndent, 2, true);
}

das_errcode DasDesc_encode3(DasDesc* pThis, DasBuf* pBuf, const char* sIndent)
{
	return _DasDesc_encodeMaybeWrap(pThis, pBuf, sIndent, 3, true);
}

das_errcode DasDesc_encode3Bare(DasDesc* pThis, DasBuf* pBuf, const char* sIndent)
{
	return _DasDesc_encodeMaybeWrap(pThis, pBuf, sIndent, 3, false);
}
