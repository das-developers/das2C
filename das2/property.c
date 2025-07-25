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

#include <ctype.h>
#include <string.h>
#ifdef _WIN32
#define strcasecmp _stricmp
#else
#include <strings.h>
#endif
#include <assert.h>

#include "util.h"
#include "property.h"

/* The property flags (32 bits total), most not used, listed high byte
	(Actual memory is backwards to chart below for an x86_64 machine)

3        2        1        0
 76543210 76543210 76543210 76543210 76543210 76543210 76543210 76543210 
+--------+--------+--------+--------+--------+--------+--------+--------+
|                          |sep_char|  name+val length |valoff |TTTT  MM|
+--------+--------+--------+--------+--------+--------+--------+--------+
*/

/* #define DASPROP_INVALID     0x00000000  */
/* #define DASPROP_SINGLE      0x00000001  */
/* #define DASPROP_RANGE       0x00000002  */
/* #define DASPROP_SET         0x00000003  */

/* #define DASPROP_TYPE_MASK   0x000000F0 */
/* #define DASPROP_STRING      0x00000010 */
/* #define DASPROP_BOOL        0x00000020 */
/* #define DASPROP_INT         0x00000030 */
/* #define DASPROP_REAL        0x00000040 */
/* #define DASPROP_DATETIME    0x00000050 */

#define DASPROP_NLEN_MASK   0x00007F00  /* value offset (max 128 bytes) */
#define DASPROP_NLEN_SHIFT  8
#define DASPROP_TLEN_MASK   0xFFFF8000  /* Total length (max 131,072 bytes)  */
#define DASPROP_TLEN_SHIFT  15

#define DASPROP_SEP_SHIFT   32

#define DASPROP_NMAX_SZ     127
#define DASPROP_VMAX_SZ     130943      /* leaves room for end null and val name */

#define DASPROP_MIN_MALLOC  8 + 8 + 4       /* Smallest possible property size */
#define DASPROP_MAX_MALLOC  8 + 8 + 131072  /* largest possible malloc 2^17 + 16 */

/** An initializer for DasProp stack variables. */
#define DAS_PROP_EMPTY {0x0ULL, UNIT_DIMENSIONLESS, {'\0'}}

/* Memory requirments ***************************************************** */

size_t dasprop_memsz(const char* sName, const char* sValue)
{
	size_t sz = sizeof(uint64_t) + sizeof(das_units);
	if(sName)  sz += strlen(sName) + 1;
	if(sValue) sz += strlen(sValue) + 1;
	if(sz < DASPROP_MIN_MALLOC) 
		return DASPROP_MIN_MALLOC;
	if(sz > DASPROP_MAX_MALLOC)
		return DASPROP_MAX_MALLOC;
	return sz;
}

/* Initalization, alteration *********************************************** */

DasErrCode DasProp_init(
	ubyte* pBuf, size_t uBufSz, const char* sType, ubyte uType, const char* sName,
	const char* sValue, char cSep, das_units units, int nStandard
){
	/* Check args */
	if((pBuf == NULL)||(uBufSz < dasprop_memsz(sName, sValue)))
		return das_error(DASERR_PROP, "Property buffer is too small, %zu bytes", uBufSz);
	
	if(sName == NULL) return das_error(DASERR_PROP, "Null value for property name");

	if(nStandard > 1){
		// Some stuff from das1 doesn't fly later on
		for(int j = 0; j < strlen(sName); j++) 
			if(!isalnum(sName[j]) && (sName[j] != '_') && (sName[j] != ':'))
				return das_error(DASERR_DESC, "Invalid das2/3 property name '%s'", sName);
	}

	size_t uNameSz = strlen(sName);
	if(uNameSz > DASPROP_NMAX_SZ) 
		return das_error(DASERR_PROP, "Name too large (%d bytes) for property", DASPROP_NMAX_SZ);
	if((uType == 0)&&(sType == NULL))
		return das_error(DASERR_PROP, "Null value for property type");

	if(sValue == NULL )
		sValue = "";
	
	/* Save off the value size */
	size_t uValSz = strlen(sValue);
	if(uValSz > DASPROP_VMAX_SZ) 
		return das_error(DASERR_PROP, "Value too large (%d bytes) for property %s",
			DASPROP_VMAX_SZ, sName
		);

	/* Get the units, either explicity or by parsing (if das2 type = Datum) */
	if((units == NULL) && (nStandard == DASPROP_DAS2) && (sType != NULL)){

		int nUnitWord = 0;
		if((strcasecmp(sType, "datum") == 0)||(strcasecmp(sType, "time") == 0)
			||(strcasecmp(sType, "datetime") == 0)
		)
			nUnitWord = 1;
		else 
			if( (strcasecmp(sType, "datumrange") == 0)||(strcasecmp(sType, "timerange") == 0)
				||(strcasecmp(sType, "datetimerange") == 0))
				nUnitWord = 3;

		if(nUnitWord > 0){
			const char* pRead = sValue;
	
			while(*pRead == ' ') ++pRead;       /* Eat spaces before the first word */

			/* find the start of the next word */
			int nAtWord = 0;
			for(int i = 0; (i < nUnitWord)&&(pRead != NULL); ++i){
				if( (pRead = strchr(pRead, ' ')) != NULL){
					++pRead;
					while(*pRead == ' ') ++pRead;  /* Eat extra spaces if needed */
				}
				nAtWord += 1;
			}

			if((nAtWord == nUnitWord)&&(pRead != NULL)&&(*pRead != '\0')){

				/* das2 had some things as units that were actually data display
				   preferences. (I'm looking at you log10Ratio) If some of these
				   poor choices show up, let them pass through as just string 
				   types */
				int nErrDisp = -1;
				if(nStandard == 2){
					das_errdisp_get_lock();
					nErrDisp = das_error_disposition(); /* MUST RELEASE LOCK IN THIS FUNCTION */
					das_return_on_error();  
				}

				units = Units_fromStr(pRead);

				if(units == NULL){
					sType = "string";
				}
				else{
					/* TRUNCATE the value so that units and the proceeding space are
					   not included.*/
					uValSz = (pRead - sValue) - 1;
				}

				if(nStandard == 2){
					das_error_setdisp(nErrDisp);
					das_errdisp_release_lock();  /* LOCK RELEASED */
				}
			}
		}
	}
	
	if(units == NULL)
		units = UNIT_DIMENSIONLESS;

	uint64_t uFlags = 0ull;

	/* Get the data type and multiplicity */

	if(sType == NULL){
		/* Explicit type and mulitplicity supplied (hurray!) */
		if((uType & DASPROP_MULTI_MASK) == 0)
			return das_error(DASERR_PROP, "Invalid muliplicity flag");
		ubyte uTmp = (uType & DASPROP_TYPE_MASK) >> 4;
		if((uTmp < 1)||(uTmp > 5))
			return das_error(DASERR_PROP, "Invalid type setting");

		uFlags = uType & (DASPROP_TYPE_MASK | DASPROP_MULTI_MASK);
	}
	else{
		/* Have to get it from type strings */

		if((sType == NULL)||(strcasecmp(sType,"string") == 0))
			uFlags |= (DASPROP_STRING | DASPROP_SINGLE);
		else if(strcasecmp(sType, "stringarray") == 0)
			uFlags |= (DASPROP_STRING | DASPROP_SET);
		else if((strcasecmp(sType, "boolean") == 0)||(strcasecmp(sType, "bool") == 0))
			uFlags |= (DASPROP_BOOL   | DASPROP_SINGLE);
		else if((strcasecmp(sType, "int") == 0)||(strcasecmp(sType, "integer") == 0))
			uFlags |= (DASPROP_INT   | DASPROP_SINGLE);
		else if((strcasecmp(sType, "double") == 0)||(strcasecmp(sType, "real") == 0)||
			     (strcasecmp(sType, "datum") == 0))
			uFlags |= (DASPROP_REAL  | DASPROP_SINGLE);
		else if(strcasecmp(sType, "realrange") == 0)
			uFlags |= (DASPROP_REAL  | DASPROP_RANGE);
		else if(strcasecmp(sType, "realArray") == 0)
			uFlags |= (DASPROP_REAL  | DASPROP_SET);
		else if(strcasecmp(sType, "doublearray") == 0)
			uFlags |= (DASPROP_REAL  | DASPROP_SET);
		else if((strcasecmp(sType, "time") == 0)||(strcasecmp(sType, "datetime") == 0))
			uFlags |= (DASPROP_DATETIME | DASPROP_SINGLE);
		else if((strcasecmp(sType, "timerange") == 0)||(strcasecmp(sType, "datetimerange") == 0))
			uFlags |= (DASPROP_DATETIME | DASPROP_RANGE);
		else if( (strcasecmp(sType, "datum") == 0) && (sValue[0] != '\0'))
			uFlags |= (DASPROP_REAL | DASPROP_SINGLE);
		else if((strcasecmp(sType, "datumrange") == 0) && (sValue[0] != '\0')){
			/* Some time ranges have been listed as datum ranges, if you 
			 * see units of 'UTC', make this a time range instead */
			if(Units_haveCalRep(units))
				uFlags |= (DASPROP_DATETIME | DASPROP_RANGE);
			else
				uFlags |= (DASPROP_REAL | DASPROP_RANGE);
		}
		else
			return das_error(DASERR_PROP, 
				"Invalid property type '%s' for value '%s'", sType, sName
			);

		/* If a range property was indicated, make sure there is a second value */
		if((uFlags & DASPROP_MULTI_MASK) == DASPROP_RANGE){
			const char* s2ndVal = NULL;
			s2ndVal = strstr(sValue, " to ");
			if(s2ndVal) s2ndVal += 4;
			if(s2ndVal && (!isdigit(s2ndVal[0])&&(s2ndVal[0]!='-')&&(s2ndVal[0]!='+')))
				s2ndVal = NULL;

			if(!s2ndVal)
				return das_error(DASERR_PROP, "Range types require two values separated by ' to '. ");
		}
	}

	/* If a set, try to guess the separator character*/
	if((uFlags & DASPROP_MULTI_MASK) == DASPROP_SET){
		if(cSep == '\0'){
			const char* sSeps = "|\t;, ";
			for(int i = 0; i < strlen(sSeps); ++i){
				if(strchr(sValue, sSeps[i]) != 0){
					cSep = sSeps[i];
					break;
				}
			}
			// If separator is still null, fallback to the default
			if(cSep == '\0') cSep = ';';
		}
	}
	else{
		cSep = '\0';
	}

	/* Set the sizes in the flags */
	uFlags |= (uNameSz + 1) << DASPROP_NLEN_SHIFT;
	uint64_t uNamValSz = uValSz + uNameSz + 2;
	if(uNamValSz < 16) uNamValSz = 16;
	uFlags |= (uNamValSz << DASPROP_TLEN_SHIFT);       

	/* and the stash away the separator */
	if(cSep != '\0')
		uFlags |= ((uint64_t)cSep) << DASPROP_SEP_SHIFT;

	ubyte* pWrite = pBuf;
	memcpy(pWrite, &uFlags, sizeof(uint64_t));
	pWrite += sizeof(uint64_t);

	/* Copy in the units */
	memcpy(pWrite, &units, sizeof(das_units));
	pWrite += sizeof(das_units);

	/* Copy in the name, depend in null termination */
	memcpy(pWrite, sName, uNameSz+1);
	pWrite += uNameSz+1;

	/* And finally the value, do NOT depend on null term, since we may have */
	/* shaved off the UNITS from a datum value. */
	memcpy(pWrite, sValue, uValSz);
	pWrite[uValSz] = 0;

	return DAS_OKAY;
}

void DasProp_invalidate(DasProp* pProp)
{
	pProp->flags &= 0xFFFFFFFFFFFFFFFC;
}

/* Information strings **************************************************** */

bool DasProp_isValid(const DasProp* pProp)
{
	return ((pProp != NULL) && (pProp->flags & DASPROP_VALID_MASK));
}

/* 
const char* DasProp_name(const DasProp* pProp)
{
	if(! (pProp->flags & DASPROP_MULTI_MASK))
		return NULL;
	return pProp->buffer;
}
*/

const char* DasProp_value(const DasProp* pProp)
{
	if(! (pProp->flags & DASPROP_MULTI_MASK))
		return NULL;

	const char* sBuf = pProp->buffer;
	size_t uOffset = ((pProp->flags & DASPROP_NLEN_MASK) >> DASPROP_NLEN_SHIFT);
	return sBuf + uOffset;
}

/* TODO: Add a unittest for this thing, it need's it */
size_t DasProp_escapeSize(const DasProp* pProp)
{
	size_t uEscapeSz = 0;

	bool bNeedXlate = false;
	const char* p = DasProp_value(pProp);

	static const char* entities[] = {"lt;", "gt;", "quot;", "apos;", "amp;"};
	size_t uEntLen = 0;
	bool bEnt = false;
	int i = 0;

	while(*p != '\0'){
		if((*p == '"')||(*p == '\'')||(*p == '<')||(*p == '>')){
			bNeedXlate = true;
			uEscapeSz += 6;
		}
		else if(*p == '&'){
			/* This one is a pain, because the text may *already* be escaped
			   and this is the start of a character entity (yay) 
				
				All the strlen checks etc. could be avoided below with extra
				loop state combined with a small lookup table on the stack
				but this will do for now.
			*/
			bEnt = false;
			for(i = 0; i < 5; ++i){
				uEntLen = strlen(entities[i]);
				if(strncmp(p+1, entities[i], uEntLen) == 0){
					p += (uEntLen + 1);
					uEscapeSz += (uEntLen + 1);
					bEnt = true;
					break;
				}
			}
			bNeedXlate = ! bEnt;
		}
		else{
			uEscapeSz += 1;
		}
		++p;
	}

	return bNeedXlate ? (uEscapeSz + 1) : 0;
}

// Escape invalid XML characters
const char* DasProp_xmlValue(const DasProp* pProp, char* sBuf, size_t uLen)
{
	bool bNeedXlate = false;
	const char* sProp = DasProp_value(pProp);
	const char* p = sProp;
	while(*p != '\0'){
		if((*p == '"')||(*p == '\'')||(*p == '<')||(*p == '>')||(*p == '&')){
			bNeedXlate = true;
			break;
		}
		++p;
	}
	if(!bNeedXlate) return sProp;

	// Okay, needs translation
	if(sBuf == NULL){
		das_error(DASERR_PROP, "XML translation needed but supplied buffer is NULL");
		return NULL;
	}

	// Use the fact that all escaped characters are 1-byte long
	p = sProp;  // reset to beginning
	char* pOut = sBuf;
	while(*p != '\0'){
		if(uLen < 6){
			das_error(DASERR_PROP, "Remaining buffer not large enough to hold an escaped XML character");
			return NULL;
		}

		switch(*p){
		case '"':  strcpy(pOut, "&quot;"); pOut += 6; break;
		case '\'': strcpy(pOut, "&apos;"); pOut += 6; break;
		case '<':  strcpy(pOut, "&lt;");   pOut += 4; break;
		case '>':  strcpy(pOut, "&gt;");   pOut += 4; break;
		case '&':  strcpy(pOut, "&amp;");  pOut += 5; break;
		default:   *pOut = *p;             ++pOut;    break;
		}

		++p;
		--uLen;
	}
	return sBuf;
}

size_t DasProp_size(const DasProp* pProp)
{
	// Mask off total length and shift down.
	uint64_t sz = (pProp->flags & DASPROP_TLEN_MASK) >> DASPROP_TLEN_SHIFT;
	assert(sz <= DASPROP_VMAX_SZ);
	sz += sizeof(uint64_t);
	sz += sizeof(das_units);
	return sz;
}

/* Return das2 types, this includes all the documented ones from the
 * das 2.2.2 ICD, as well as the undocumented ones that were previously
 * allowed by the library.
 */
const char* DasProp_typeStr2(const DasProp* pProp)
{
	uint32_t uMulti = pProp->flags & DASPROP_MULTI_MASK;
	if(!uMulti)
		return NULL;

	switch(pProp->flags & DASPROP_TYPE_MASK){
	case DASPROP_BOOL:   return "boolean";
	case DASPROP_REAL: 
		if(pProp->units == UNIT_DIMENSIONLESS){
			if(uMulti == DASPROP_SET)
				return "doubleArray";
			else
				return "double";
		}
		else{
			if(uMulti == DASPROP_RANGE)
				return "DatumRange";
			else
				return "Datum";
		}
	case DASPROP_INT:      return "int";
	case DASPROP_DATETIME:
		if(uMulti == DASPROP_RANGE)
			return "TimeRange";
		else
			return "Time";
	default:  return "String";
	}
}

const char* DasProp_typeStr3(const DasProp* pProp)
{
	uint64_t uInfo = pProp->flags & (DASPROP_TYPE_MASK |DASPROP_MULTI_MASK) ;
	switch(uInfo){
	case DASPROP_STRING  |DASPROP_SINGLE: return "string";
	case DASPROP_STRING  |DASPROP_SET:    return "stringArray";
	case DASPROP_BOOL    |DASPROP_SINGLE: return "bool";
	case DASPROP_BOOL    |DASPROP_SET:    return "boolArray";
	case DASPROP_INT     |DASPROP_SINGLE: return "integer";
	case DASPROP_INT     |DASPROP_RANGE:  return "intRange";
	case DASPROP_INT     |DASPROP_SET:    return "intArray";
	case DASPROP_REAL    |DASPROP_SINGLE: return "real";
	case DASPROP_REAL    |DASPROP_RANGE:  return "realRange";
	case DASPROP_REAL    |DASPROP_SET:    return "realArray";
	case DASPROP_DATETIME|DASPROP_SINGLE: return "datetime";
	case DASPROP_DATETIME|DASPROP_RANGE:  return "datetimeRange";
	case DASPROP_DATETIME|DASPROP_SET:    return "datetimeArray";
	default: return "";
	}
}   

ubyte DasProp_type(const DasProp* pProp)
{
	return (ubyte)(pProp->flags & (DASPROP_TYPE_MASK|DASPROP_MULTI_MASK));
}

bool DasProp_equal(const DasProp* pOne, const DasProp* pTwo)
{
	if((pOne == NULL)||(pTwo == NULL))
		return false;

	if( DasProp_isValid(pOne) != DasProp_isValid(pTwo))
		return false;

	if( pOne->flags != pTwo->flags)
		return false;

	if( pOne->units != pTwo->units)
		return false;

	if(strcmp(DasProp_name(pOne), DasProp_name(pTwo)) != 0)
		return false;

	return (strcmp(DasProp_value(pOne), DasProp_value(pTwo)) == 0);
}

char DasProp_sep(const DasProp* pProp)
{
	return (char)(pProp->flags >> DASPROP_SEP_SHIFT & 0xFF);
}

int DasProp_items(const DasProp* pProp)
{
	if((DASPROP_MULTI_MASK & pProp->flags) == DASPROP_SINGLE)
		return 1;
	
	if((DASPROP_MULTI_MASK & pProp->flags) == DASPROP_RANGE)
		return 2;

	/* For sets, count separators */
	if((DASPROP_MULTI_MASK & pProp->flags) == DASPROP_SET){
		char cSep = DasProp_sep(pProp);
		const char* sValue = DasProp_value(pProp);

		/* Take a default cSep or a space cSep to mean arbitrary whitespace parsing */
		if((cSep != '\0')||(cSep != '\0')){
			/* Whitespace parsing, as soon as we see a non-whitespace char
			   mark an item as existing. */
			int nItems = 0;
			bool bInItem = false;
			while(*sValue != '\0'){
				if(!bInItem){ 
					if(!isspace(*sValue)){
						bInItem = true;
						++nItems;
					}
				}
				else{
					if(isspace(*sValue))
						bInItem = false;
				}
				++sValue;
			}
		}
		else{
			int nItems = 1;
			while(*sValue != '\0'){
				if(*sValue == cSep){
					++nItems;
				}
				++sValue;
			}
			return nItems;
		}
	}

	return 0;
}

/* The output functions here *********************************************** */ 
/*   TODO: Functionality is repeated in Descriptor and shouldn't be.         */

/** Copy out the next value into a seperate buffer */
bool _DasProp_next(const DasProp* pProp, const char** ppRead, char* sBuf, size_t uLen)
{
	if((*ppRead == NULL)||(*(*ppRead) == '\0')) /* Read pointing to '\0' indicates done too? */
		return false;

	if(uLen < 2){
		das_error(DASERR_PROP, "Output buffer too short, less then 2 bytes");
		return false;
	}

	const char* sValue = DasProp_value(pProp);
	const char* sTo = NULL;
	ptrdiff_t uWrite = 0;
	
	switch(pProp->flags & 0x00000003){
	case DASPROP_SINGLE:
		if(*ppRead == sValue){ // 1st item
			*ppRead = NULL;
			strncpy(sBuf, sValue, uLen-1);
			return true;
		}
		break;

	case DASPROP_RANGE:
		sTo = strstr(sValue, " to ");
		if(*ppRead == sValue){ // 1st item
			uWrite = sTo - sValue;
			if(uWrite > uLen) uWrite = uLen;
			strncpy(sBuf, sValue, uWrite);
			*ppRead = sTo + 4;
			return true;
		}
		else{
			uWrite = strlen(sValue) - (sTo - sValue) - 4;
			if(uWrite > uLen) uWrite = uLen;
			strncpy(sBuf, *ppRead, uWrite);
			*ppRead = NULL;
			return true;
		}
		break;

	case DASPROP_SET:
		/* Find next sep*/
		sTo = strchr(*ppRead, DasProp_sep(pProp));
		if(sTo != NULL){
			uWrite = sTo - *ppRead;
			if(uWrite > uLen) uWrite = uLen;
			strncpy(sBuf, *ppRead, uWrite);
			*ppRead = sTo + 1;
		}
		else{
			uWrite = strlen(*ppRead);
			if(uWrite > uLen) uWrite = uLen;
			strncpy(sBuf, *ppRead, uWrite);
			*ppRead = NULL;
		}
		return true;

	default:
		break;	
	}

	*ppRead = NULL;
	return false;
}

/* Similar to the items below, but doesn't do any conversions */
int DasProp_extractItems(const DasProp* pProp, char** psBuf, size_t uNumStrs, size_t uLenEa)
{
	const char* pRead = DasProp_value(pProp);
	size_t uRead = 0;

	/* Go right into the output buffer */
	while( _DasProp_next(pProp, &pRead, psBuf[uRead], uLenEa) && (uRead < uNumStrs)){
		++uRead;
	}
	return (int)uRead;
}

/** Convert integer property values to 64-bit ints
 * 
 * Returns the number of conversions, or a negative error value
 */
int DasProp_convertInt(const DasProp* pProp, int64_t* pBuf, size_t uBufLen)
{
	char sConv[32] = {'\0'};
	const char* pRead = DasProp_value(pProp);
	size_t uRead = 0;

	while( _DasProp_next(pProp, &pRead, sConv, 31) && (uRead < uBufLen)){
#if defined(_WIN32) || defined(__APPLE__)
		if(sscanf(sConv, "%lld", pBuf) != 1)
#else
		if(sscanf(sConv, "%ld", pBuf) != 1)
#endif
			return -1 * das_error(DASERR_PROP, "Error converting '%s' to an integer", DasProp_value(pProp));
		++pBuf;
		++uRead;
		memset(sConv, 0, 32);
	}
	return (int)uRead;
}

/** Convert real-value properties to double
 * 
 * Returns the number of conversions, or a negative error value
 */
int DasProp_convertReal(const DasProp* pProp, double* pBuf, size_t uBufLen)
{
	char sConv[32] = {'\0'};
	const char* pRead = DasProp_value(pProp);
	size_t uRead = 0;
	
	while( _DasProp_next(pProp, &pRead, sConv, 31) && (uRead < uBufLen)){
		if(sscanf(sConv, "%lf", pBuf) != 1)
			return -1 * das_error(DASERR_PROP, "Error converting '%s' to a double", DasProp_value(pProp));
		++pBuf;
		++uRead;
		memset(sConv, 0, 32);
	}
	return (int)uRead;
}

/** Convert boolean property values to bytes
 *
 * Returns the number of conversions, or a negative error value
 */
int DasProp_convertBool(const DasProp* pProp, uint8_t* pBuf, size_t uBufLen)
{
	return -1 * das_error(DASERR_NOTIMP, "Boolean property conversion not yet implemented");
}

/** Convert datatime properties TT2K long integers */
int DasProp_convertTt2k(const DasProp* pProp, int64_t* pBuf, size_t uBufLen)
{
	char sConv[32] = {'\0'};
	const char* pRead = DasProp_value(pProp);
	size_t uRead = 0;
	das_time dt;

	while( _DasProp_next(pProp, &pRead, sConv, 31) && (uRead < uBufLen)){
		if(!dt_parsetime(sConv, &dt))
			return -1 * das_error(DASERR_TIME, "Could not convert %s to a datetime", DasProp_value(pProp));
		*pBuf = dt_to_tt2k(&dt);
		++pBuf;
		++uRead;
		memset(sConv, 0, 32);
	}
	return (int)uRead;
}

/** Convert datetime properties to a double based value of units */
int DasProp_convertTime(const DasProp* pProp, uint64_t* pBuf, size_t uBufLen){
	return -1 * das_error(DASERR_NOTIMP, "Time property conversion not yet implemented");
}
