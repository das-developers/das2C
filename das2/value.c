/* Copyright (C) 2018 Chris Piker <chris-piker@uiowa.edu>
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

#include <pthread.h>
#include <errno.h>
#include <locale.h>
#include <string.h>
#ifdef _WIN32
#define strcasecmp _stricmp
#else
#include <strings.h>
#endif
#include <ctype.h>

#include "util.h"
#include "units.h"
#include "value.h"
#include "array.h"
#include "operator.h"
#include "datum.h"
#include "util.h"

/* ************************************************************************* */
/* value type functions */

static const das_idx_info g_idxFill = {0, 0};
static const byte g_byteFill = 0;
static const uint16_t g_ushortFill = 65535;
static const int16_t g_shortFill = -32767;
static const int32_t g_intFill = -2147483647;
static const int64_t g_longFill = -9223372036854775807L;
static const float g_floatFill = DAS_FILL_VALUE;
static const double g_doubleFill = DAS_FILL_VALUE;
static const das_time g_timeFill = {0, 0, 0, 0, 0, 0, 0.0};

const void* das_vt_fill(das_val_type et)
{
	switch(et){
	case vtIndex: return &(g_idxFill);
	case vtByte: return &(g_byteFill);
	case vtShort: return &(g_shortFill);
	case vtUShort: return &(g_ushortFill);
	case vtInt: return &(g_intFill);
	case vtLong: return &(g_longFill);
	case vtFloat: return &(g_floatFill);
	case vtDouble: return &(g_doubleFill);
	case vtTime: return &(g_timeFill);
	default:	return NULL;
	}
}

size_t das_vt_size(das_val_type et)
{
	switch(et){
	case vtIndex: return sizeof(g_idxFill);
	case vtByte: return sizeof(g_byteFill);
	case vtShort: return sizeof(g_shortFill);
	case vtUShort: return sizeof(g_ushortFill);
	case vtInt: return sizeof(g_intFill);
	case vtLong: return sizeof(g_longFill);
	case vtFloat: return sizeof(g_floatFill);
	case vtDouble: return sizeof(g_doubleFill);
	case vtTime: return sizeof(g_timeFill);
	case vtText: return sizeof(char*);
	default:
		das_error(DASERR_ARRAY, "Program logic error");
		return 0;
	}
}

const char* das_vt_toStr(das_val_type et)
{
	switch(et){
	case vtUnknown: return "unknown";
	case vtIndex: return "index_info";
	case vtByte: return "byte";
	case vtUShort: return "uint16_t";
	case vtInt: return "int32_t";
	case vtLong: return "int64_t";
	case vtFloat: return "float";
	case vtDouble: return "double";
	case vtTime: return "das_time_t";
	case vtText: return "const char*";
	default: return NULL;
	}
}

/* ************************************************************************* */
/* Default Comparison functions */

int vt_cmp_byte(const byte* vpFirst, const byte* vpSecond){
	const byte* pFirst = (const byte*) vpFirst;
	const byte* pSecond = (const byte*) vpSecond;
	if(*pFirst < *pSecond) return -1;
	if(*pFirst > *pSecond) return 1;
	return 0;
}
int vt_cmp_ushort(const byte* vpFirst, const byte* vpSecond){
	const uint16_t* pFirst = (const uint16_t*) vpFirst;
	const uint16_t* pSecond = (const uint16_t*) vpSecond;
	if(*pFirst < *pSecond) return -1;
	if(*pFirst > *pSecond) return 1;
	return 0;
}
int vt_cmp_short(const byte* vpFirst, const byte* vpSecond){
	const int16_t* pFirst = (const int16_t*) vpFirst;
	const int16_t* pSecond = (const int16_t*) vpSecond;
	if(*pFirst < *pSecond) return -1;
	if(*pFirst > *pSecond) return 1;
	return 0;
}
int vt_cmp_int(const byte* vpFirst, const byte* vpSecond){
	const int32_t* pFirst = (const int32_t*) vpFirst;
	const int32_t* pSecond = (const int32_t*) vpSecond;
	if(*pFirst < *pSecond) return -1;
	if(*pFirst > *pSecond) return 1;
	return 0;
}
int vt_cmp_long(const byte* vpFirst, const byte* vpSecond){
	const int64_t* pFirst = (const int64_t*) vpFirst;
	const int64_t* pSecond = (const int64_t*) vpSecond;
	if(*pFirst < *pSecond) return -1;
	if(*pFirst > *pSecond) return 1;
	return 0;
}
int vt_cmp_float(const byte* vpFirst, const byte* vpSecond){
	const float* pFirst = (const float*) vpFirst;
	const float* pSecond = (const float*) vpSecond;
	if(*pFirst < *pSecond) return -1;
	if(*pFirst > *pSecond) return 1;
	return 0;
}
int vt_cmp_double(const byte* vpFirst, const byte* vpSecond){
	const double* pFirst = (const double*) vpFirst;
	const double* pSecond = (const double*) vpSecond;
	if(*pFirst < *pSecond) return -1;
	if(*pFirst > *pSecond) return 1;
	return 0;
}
int vt_cmp_time(const byte* vpFirst, const byte* vpSecond){
	return dt_compare((const das_time*)vpFirst, (const das_time*)vpSecond);
}
int vt_cmp_text(const byte* vpFirst, const byte* vpSecond){
	const char** pFirst = (const char**) vpFirst;
	const char** pSecond = (const char**) vpSecond;
	return strcmp(*pFirst, *pSecond);
}

int vt_cmp_byteseq(const byte* vpA, const byte* vpB)
{
	const das_byteseq* pA = (const das_byteseq*)vpA;
	const das_byteseq* pB = (const das_byteseq*)vpB;
	
	size_t uLen = pA->sz < pB->sz ? pA->sz : pB->sz;  /*smallest size*/
	int nCmp = memcmp(vpA, vpB, uLen);
	if(nCmp != 0) return nCmp;
	
	/* If same length then truly zero, else the longer one is "bigger" 
	 * then the shorter one. */

	return ( (pA->sz > pB->sz) ? 1 : ((pA->sz == pB->sz) ? 0 : -1) );
}

das_valcmp_func das_vt_getcmp(das_val_type et)
{
	switch(et){
	case vtByte: return vt_cmp_byte;
	case vtShort: return vt_cmp_short;
	case vtUShort: return vt_cmp_ushort;
	case vtInt: return vt_cmp_int;
	case vtLong: return vt_cmp_long;
	case vtFloat: return vt_cmp_float;
	case vtDouble: return vt_cmp_double;
	case vtTime: return vt_cmp_time;
	case vtText: return vt_cmp_text;
	case vtByteSeq: return vt_cmp_byteseq;
	default:	return NULL;
	}
}

/* There is a bit of a flaw in the design here in that how we store data must
 * agree somewhat with what units we use.  The broken down times especially
 * connotate units.
 */
das_val_type das_vt_merge(das_val_type left, int op, das_val_type right)
{
	if(left == vtUnknown || right == vtUnknown) return vtUnknown;
	if(left == vtIndex || right == vtIndex) return vtUnknown;
	if(left == vtByteSeq || right == vtByteSeq) return vtUnknown;
	if(left == vtText || right == vtText) return vtUnknown;
	
	bool bShortRight = (left == vtByte || left == vtShort || left == vtUShort 
			             || left == vtFloat);
	bool bShortLeft = (right == vtByte || right == vtShort || right == vtUShort
			             || right == vtFloat);
	if(bShortRight && bShortLeft) return vtFloat;
	if(left != vtTime && right != vtTime) return vtDouble;
	
	bool bNumericLeft = (right == vtByte || right == vtUShort || right == vtShort ||
	                     right == vtInt  || right == vtFloat  || right == vtDouble);
	
	if(left == vtTime && bNumericLeft && 
	   (op == D2BOP_ADD  || op == D2BOP_SUB)) return vtTime;
	
	if(left == vtTime && right == vtTime && op == D2BOP_SUB) return vtDouble;
	
	return vtUnknown;
}

#define HAS_INT  0x1
#define HAS_FLT  0x2	
#define HAS_BOTH 0x3

int das_vt_cmpAny(
	const byte* pA, das_val_type vtA, const byte* pB, das_val_type vtB
){
	int nCmp = 0;
	double dA = 0.0, dB = 0.0;
			
	/* Handle most common compare first and get out */
	if(vtA == vtDouble && vtB == vtDouble){
		dA = *((const double*)pA);
		dB = *((const double*)pB);
		return ((dA > dB) ? 1 : ((dA == dB) ? 0 : -1));
	}
	
	/* Unknowns have no way to get length, they never compare */
	if(vtA == vtUnknown || vtB == vtUnknown) return -2;
	
	/* vtIndex items only make since in the context of their parent 
	   structures, so no dice here either */
	if(vtA == vtIndex || vtB == vtIndex) return -2;
	
	if(vtA == vtB){
		if(vtA == vtText){  
			nCmp = strcmp((const char*)pA, (const char*)pB);
		}
		else{
			if(vtA == vtTime){
				nCmp = dt_compare((const das_time*)pA, (const das_time*)pB);
			}
			else{
				if(vtA == vtByteSeq)
					nCmp = vt_cmp_byteseq(pA, pB);
				else
					nCmp = memcmp(pA, pB, das_vt_size(vtA));
			}
		}
		
		return (nCmp > 0 ? 1 : nCmp == 0 ? 0 : -1);
	}
	
	/* Unequal type comparisons ... */
	
	/* vtTime can't compare with anything else because there are no units */
	if(vtA == vtTime || vtB == vtTime) return -2;
	
	/* If one is a byte sequence and the other is not, compare as byteseq */
	das_byteseq bs;
	if(vtA == vtByteSeq || vtB == vtByteSeq){
		if(vtA == vtByteSeq){
			bs.ptr = pB;
			bs.sz = das_vt_size(vtB);
			return vt_cmp_byteseq(pA, (const byte*) &bs);
		}
		else{
			bs.ptr = pA;
			bs.sz = das_vt_size(vtA);
			return vt_cmp_byteseq((const byte*)&bs, pB);		
		}
	}
	
	/* Generic numeric comparisons below... These need a little work.  */
	/* I'm sure there are many patterns that could be exploted to make */
	/* the code shorter.  In general there are 8x8 types, for 64       */
	/* different code paths.  But many of them collapse                */
	
	/* In D we'd use the 80-bit numeric types to make this easier, have */
	/* to do it manually for VC++                                       */

	int64_t lA = 0, lB = 0;
	byte uAHas = 0;  /* float = 0x2 */
	byte uBHas = 0;  /* int =   0x1 */
	switch(vtA){
	case vtByte:   lA = *((const uint8_t*)pA);  dA = *((const uint8_t*)pA);  uAHas = HAS_BOTH; break;
	case vtShort:  lA = *((const int16_t*)pA);  dA = *((const int16_t*)pA);  uAHas = HAS_BOTH; break;
	case vtUShort: lA = *((const uint16_t*)pA); dA = *((const uint16_t*)pA); uAHas = HAS_BOTH; break;
	case vtInt:    lA = *((const int32_t*)pA);  dA = *((const int32_t*)pA);  uAHas = HAS_BOTH; break;
	
	case vtLong:   lA = *((const int64_t*)pA);  /* No float value */         uAHas = HAS_INT;  break;
	
	case vtFloat:  /* No int value */           dA = *((const float*)pA);    uAHas = HAS_FLT;  break;
	case vtDouble: /* No int value */           dA = *((const double*)pA);   uAHas = HAS_FLT;  break;
	default: return -2;
	}
	
	switch(vtB){
	case vtByte:   lB = *((const uint8_t*)pB);  dB = *((const uint8_t*)pB);  uBHas = HAS_BOTH; break;
	case vtShort:  lB = *((const int16_t*)pB);  dB = *((const int16_t*)pB);  uBHas = HAS_BOTH; break;
	case vtUShort: lB = *((const uint16_t*)pB); dB = *((const uint16_t*)pB); uBHas = HAS_BOTH; break;
	case vtInt:    lB = *((const int32_t*)pB);  dB = *((const int32_t*)pB);  uBHas = HAS_BOTH; break;
	
	case vtLong:   lB = *((const int64_t*)pB);  /* No float value */         uBHas = HAS_INT;  break;
	
	case vtFloat:  /* No int value */           dB = *((const float*)pB);    uBHas = HAS_FLT;  break;
	case vtDouble: /* No int value */           dB = *((const double*)pB);   uBHas = HAS_FLT;  break;
	default: return -2;
	}
	
	/* If both have longs, compare them */
	if( (uAHas & uBHas) & HAS_INT ) return ( (lA > lB) ? 1 : ((lA == lB) ? 0 : -1) );
	
	/* if both have doubles, compare them */
	if( (uAHas & uBHas) & HAS_FLT ) return ( (dA > dB) ? 1 : ((dA == dB) ? 0 : -1) );
	
	
	/* Crap, now we have to compare longs to doubles, this is a pain */
	fprintf(stderr, "TODO: Long to double comparison needed in libdas2\n");

	return -2;
}

#undef HAS_INT
#undef HAS_FLT
#undef HAS_BOTH

/* ************************************************************************* */
/* String to Value utilities */

bool das_str2double(const char* str, double* pRes){
	double rRet;
	char* endptr;

	if((str == NULL)||(pRes == NULL)){ return false; }

	errno = 0;

	rRet = das_strtod_c(str, &endptr);

	if( (errno == ERANGE) || ((errno != 0) && (rRet == 0)) )return false;
	if(endptr == str) return false;

	*pRes = rRet;
	return true;
}

bool das_str2bool(const char* str, bool* pRes)
{
	if((str == NULL)||(strlen(str) < 1) ) return false;

	if(str[0] == 'T' || str[0] == '1'  || str[0] == 'Y'){
		*pRes = true;
		return true;
	}

	if(str[0] == 'F' || str[0] == '0'  || str[0] == 'N'){
		*pRes = false;
		return true;
	}

	if((strcasecmp("true", str) == 0)||(strcasecmp("yes", str) == 0)){
		*pRes = true;
		return true;
	}

	if((strcasecmp("false", str) == 0)||(strcasecmp("no", str) == 0)){
		*pRes = false;
		return true;
	}

	return false;
}

bool das_str2int(const char* str, int* pRes)
{
	if((str == NULL)||(pRes == NULL)){ return false; }

	size_t i, len;
	int nBase = 10;
	long int lRet;
	char* endptr;
	len = strlen(str);

	/* check for hex, don't use strtol's auto-base as leading zero's cause
	   a switch to octal */
	for(i = 0; i<len; i++){
		if((str[i] != '0')&&isalnum(str[i])) break;

		if((str[i] == '0')&&(i<(len-1))&&
			((str[i+1] == 'x')||(str[i+1] == 'X')) ){
			nBase = 16;
			break;
		}
	}

	errno = 0;
	lRet = strtol(str, &endptr, nBase);

	if( (errno == ERANGE) || (errno != 0 && lRet == 0) ){
		return false;
	}

	if(endptr == str) return false;

	if((lRet > INT_MAX)||(lRet < INT_MIN)) return false;

	*pRes = (int)lRet;
	return true;
}

bool das_str2baseint(const char* str, int base, int* pRes)
{
	if((str == NULL)||(pRes == NULL)){ return false; }

	if((base < 1)||(base > 60)){return false; }

	long int lRet;
	char* endptr;

	errno = 0;
	lRet = strtol(str, &endptr, base);

	if( (errno == ERANGE) || (errno != 0 && lRet == 0) ){
		return false;
	}

	if(endptr == str) return false;

	if((lRet > INT_MAX)||(lRet < INT_MIN)) return false;

	*pRes = (int)lRet;
	return true;
}

bool das_strn2baseint(const char* str, int nLen, int base, int* pRes)
{
	if((str == NULL)||(pRes == NULL)){ return false; }

	if((base < 1)||(base > 60)){return false; }

	/* Find the first non-whitespace character or NULL, start copy
	   from that location up to length of remaining characters */
	int i, nOffset = 0;
	for(i = 0; i < nLen; ++i){
		if(isspace(str[i]) && (str[i] != '\0')) nOffset++;
		else break;
	}

	if(nOffset >= nLen) return false;  /* All space case */

	int nCopy = nLen - nOffset > 64 ? 64 : nLen - nOffset;

	char _str[68] = {'\0'};
	strncpy(_str, str + nOffset, nCopy);


	long int lRet;
	char* endptr;
	errno = 0;
	lRet = strtol(_str, &endptr, base);

	if( (errno == ERANGE) || (errno != 0 && lRet == 0) ){
		return false;
	}

	if(endptr == _str) return false;

	if((lRet > INT_MAX)||(lRet < INT_MIN)) return false;

	*pRes = (int)lRet;
	return true;
}

double* das_csv2doubles(const char* arrayString, int* p_nitems )
{
    int i;
    int ncomma;
    const char * ipos;
    double * result;

    ncomma=0;
    for ( i=0; i< strlen(arrayString); i++ ) {
        if ( arrayString[i]==',' ) ncomma++;
    }

    *p_nitems= ncomma+1;
    result= (double*) calloc(ncomma+1, sizeof(double));

    ipos=arrayString;
    for ( i=0; i<*p_nitems; i++ ) {

        sscanf( ipos, "%lf", &result[i] );
        ipos= (char *)( strchr( ipos+1, (int)',' ) + 1 );
    }
    return result;
}

char * das_doubles2csv( char * buf, const double * value, int nitems ) {
    int i;
    sprintf( buf, "%f", value[0] );
    for ( i=1; i<nitems; i++ ) {
        sprintf( &buf[ strlen( buf ) ], ", %f", value[i] );
    }
    return buf;
}

