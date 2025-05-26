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
#include <inttypes.h>  /* Get format strings for 64-bit items */
#include <limits.h>
#include <locale.h>
#include <string.h>
#include <float.h>     /* get FLT_MAX */
#ifdef _WIN32
#define strcasecmp _stricmp
#else
#include <strings.h>
#endif
#include <ctype.h>
#include <math.h>

#include "util.h"
#include "units.h"
#include "value.h"
#include "array.h"
#include "operator.h"
#include "datum.h"
#include "util.h"
#include "vector.h"
#include "log.h"

/* ************************************************************************* */
/* value type functions */

static const das_idx_info g_idxFill = {0, 0};
static const ubyte g_ubyteFill = 255;
static const int8_t  g_byteFill = -128;
static const uint16_t g_ushortFill = 65535;
static const int16_t g_shortFill = -32767;
static const uint32_t g_uintFill = 4294967295;
static const int32_t g_intFill = -2147483647;
static const int64_t g_longFill = -9223372036854775807L;
static const uint64_t g_ulongFill = 18446744073709551615UL;
static const float g_floatFill = DAS_FILL_VALUE;
static const double g_doubleFill = DAS_FILL_VALUE;
static const das_time g_timeFill = {1, 1, 1, 1, 0, 0, 0.0};
static const das_geovec g_geovecFill = {{0,0,0}, 0, 0, 0, 0, 0, 0, 0, 0};

static const void* g_voidFill = NULL;
static const das_byteseq g_byteSeqFill = {NULL, 0};

const void* das_vt_fill(das_val_type et)
{
	switch(et){
	case vtIndex: return &(g_idxFill);
	case vtUByte: return &(g_ubyteFill);
	case vtByte:  return &(g_byteFill);
	case vtUShort: return &(g_ushortFill);
	case vtShort: return &(g_shortFill);
	case vtUInt:  return &(g_uintFill);
	case vtInt: return &(g_intFill);
	case vtULong: return &(g_ulongFill);
	case vtLong: return &(g_longFill);
	case vtFloat: return &(g_floatFill);
	case vtDouble: return &(g_doubleFill);
	case vtTime: return &(g_timeFill);
	case vtGeoVec: return &(g_geovecFill);
	case vtText:   return &(g_voidFill);
	case vtByteSeq: return &(g_byteSeqFill);
	default:	return NULL;
	}
}

size_t das_vt_size(das_val_type et)
{
	switch(et){
	case vtIndex: return sizeof(g_idxFill);

	case vtByte:  case vtUByte:  return 1;
	case vtShort: case vtUShort: return 2;
	case vtUInt:  case vtInt:    case vtFloat:  return 4;
	case vtLong:  case vtULong:  case vtDouble: return 8;
	
	case vtTime: return sizeof(g_timeFill);
	case vtText: return sizeof(char*);
	case vtGeoVec: return sizeof(das_geovec);
	case vtByteSeq: return sizeof(das_byteseq);
	default:
		das_error(DASERR_ARRAY, "Program logic error");
		return 0;
	}
}

const char* das_vt_toStr(das_val_type et)
{
	switch(et){
	case vtUnknown: return "unknown";
	case vtIndex:   return "index_info";
	case vtUByte:   return "ubyte";
	case vtByte:    return "byte";
	case vtUShort:  return "ushort";
	case vtShort:   return "short";
	case vtUInt:    return "uint";
	case vtInt:     return "int";
	case vtULong:   return "ulong";
	case vtLong:    return "long";
	case vtFloat:   return "float";
	case vtDouble:  return "double";
	case vtTime:    return "das_time";
	case vtGeoVec:  return "das_geovec";
	case vtText:    return "char*";
	case vtByteSeq: return "ubyte*";
	default: return NULL;
	}
}

das_val_type das_vt_fromStr(const char* sStorage)
{
	if(strcasecmp(sStorage, "float") == 0)  return vtFloat;
	if(strcasecmp(sStorage, "double") == 0) return vtDouble;
	if(strcasecmp(sStorage, "int") == 0)    return vtInt;
	if(strcasecmp(sStorage, "short") == 0)  return vtShort;
	if(strcasecmp(sStorage, "long") == 0)   return vtLong;
	if(strcasecmp(sStorage, "uint") == 0)   return vtUInt;
	if(strcasecmp(sStorage, "ushort") == 0) return vtUShort;
	if(strcasecmp(sStorage, "ulong") == 0)  return vtLong;
	if(strcasecmp(sStorage, "byte") == 0)   return vtByte;
	if(strcasecmp(sStorage, "ubyte") == 0)  return vtUByte;
	if(strcasecmp(sStorage, "index_info") == 0) return vtIndex;
	if(strcasecmp(sStorage, "utf8") == 0)   return vtText;
	if(strcasecmp(sStorage, "char*") == 0)  return vtText;
	if(strcasecmp(sStorage, "ubyte*") == 0) return vtByteSeq;
	return vtUnknown;
}

/* Useful for picking a storage type when it's not explicity stated */
das_val_type das_vt_store_type(
	const char* sEncType, int nItemBytes, const char* sInterp
){
	if(strcmp(sEncType, "none") == 0){
		return vtByteSeq;
	}
	if(strcmp(sEncType, "byte") == 0){
		return vtByte;  // promote to higher type to get sign bits
	}	
	if(strcmp(sEncType, "ubyte") == 0){
		return vtUByte;
	}
	if((strcmp(sEncType, "BEint") == 0)||(strcmp(sEncType, "LEint") == 0)){
		if(nItemBytes == 2) return vtShort;
		else if(nItemBytes == 4) return vtInt;
		else if(nItemBytes == 8) return vtLong;
		else{
			das_error(DASERR_VALUE, "Unsupported length %d for binary integers", nItemBytes);
			return vtUnknown;
		}
	}
	if((strcmp(sEncType, "BEuint") == 0)||(strcmp(sEncType, "LEuint") == 0)){
		if(nItemBytes == 2) return vtUShort;
		else if(nItemBytes == 4) return vtUInt;
		else if(nItemBytes == 8) return vtULong;
		else{
			das_error(DASERR_VALUE, "Unsupported length %d for binary integers", nItemBytes);
			return vtUnknown;
		}
	}
	if((strcmp(sEncType, "BEreal") == 0)||(strcmp(sEncType, "LEreal") == 0)){
		if(nItemBytes == 4) return vtFloat;
		else if (nItemBytes == 8) return vtDouble;
		else{
			das_error(DASERR_VALUE, "Unsupported length %d for binary floating point values", nItemBytes);
			return vtUnknown;	
		}
	}

	/* Okay it's a text type, deal with that */
	if(strcmp(sEncType, "utf8") == 0){
		if(strcmp(sInterp, "bool") == 0)
			return vtByte;  /* signed range allows for a fill value */
		if(strcmp(sInterp, "datetime") == 0)
			return vtTime;
		if(strcmp(sInterp, "real") == 0){
			/* Can get hints from the length of the field, assume var-width items
			 * need the bigest available encoding */
			if((nItemBytes > 15)||(nItemBytes < 1))
				return vtDouble;  
			else
				return vtFloat;
		}
		if(strcmp(sInterp, "int") == 0){
			/* can get hints from the length of the field */
			if((nItemBytes < 1)||(nItemBytes > 11))
				return vtLong;  /* For var-length ints, return biggest storage for safety */
			if(nItemBytes > 5)
				return vtInt;
			if(nItemBytes > 4)
				return vtShort;
			return vtByte;
		}
		if(strcmp(sInterp, "string") == 0){
			return vtText;
		}
	}
	if(strcmp(sEncType, "none") == 0){
		return vtByteSeq;
	}

	das_error(DASERR_VALUE, "Unknown encoding type %s", sEncType);
	return vtUnknown;
}

const char* das_vt_serial_type(das_val_type et)
{
#ifdef HOST_IS_LSB_FIRST
	const bool LE = true;
#else
	const bool LE = false;
#endif

	switch(et){
	case vtUnknown: return NULL;
	case vtIndex:   return NULL;
	case vtUByte:   return "ubyte";
	case vtByte:    return "byte";
	case vtUShort:  return LE ?  "LEuint" : "BEuint";
	case vtShort:   return LE ?  "LEint"  : "BEint";
	case vtUInt:    return LE ?  "LEuint" : "BEuint";
	case vtInt:     return LE ?  "LEint"  : "BEint";
	case vtULong:   return LE ?  "LEuint" : "BEuint";
	case vtLong:    return LE ?  "LEint"  : "BEint";
	case vtFloat:   return LE ?  "LEreal" : "BEreal";
	case vtDouble:  return LE ?  "LEreal" : "BEreal";
	case vtTime:    return "utf8";  /* encodes as old ASCII(X) */
	case vtGeoVec:  return NULL;
	case vtText:    return "utf8";  /* encodes as old TIME(X)  */
	case vtByteSeq: return "ubyte";
	default: return NULL;
	}
}

/* ************************************************************************* */
/* semantics */

const char* DAS_SEM_BIN   = "binary";
const char* DAS_SEM_BOOL  = "bool";
const char* DAS_SEM_DATE  = "datetime";
const char* DAS_SEM_INT   = "int";
const char* DAS_SEM_REAL  = "real";
const char* DAS_SEM_TEXT  = "string";

/** Given a value type, suggest a default semantic */
const char* das_sem_default(das_val_type vt)
{
	switch(vt){
	case vtFloat:  case vtDouble:        return DAS_SEM_REAL;
	case vtTime:   return DAS_SEM_DATE;
	case vtText:   return DAS_SEM_TEXT;
	default:       return DAS_SEM_INT;
	}
	/* Non atomic types do not have defaults, this includes:
	   vtGeoVec, vtPixel, vtByteSeq, vtIndex etc. */
}

/* Given a semantic, suggest a default value type */
das_val_type das_vt_default(const char* sSemantic)
{
	if(strcmp(sSemantic, "bool")) return vtByte;
	if(strcmp(sSemantic, "datetime")) return vtTime;
	if(strcmp(sSemantic, "int")) return vtInt;
	if(strcmp(sSemantic, "real")) return vtDouble;
	if(strcmp(sSemantic, "string")) return vtText;
	return vtByteSeq;
}


/*
das_val_type das_vt_guess_store(const char* sInterp, const char* sValue)
{
	int nLen = strlen(sValue);
	if((sValue == NULL)||(sValue[0] == '\0')){
		das_error(DASERR_VALUE, "Null value");
		return vtUnknown;
	}

	if(strcmp(sInterp, "real") == 0){

		// Cut down expected lengths by 4 if using scientific notation
		char* pE = strchr(sValue, 'e');
		if(pE == NULL)
			pE = strchr(sValue, 'E');
		if(pE != NULL)
			uLen -= p;
		if((sValue[0] == '+')||(sValue[0]=='-'))
			uLen -= 1
		if((strchr(sValue, '.') != NULL)||(strchr(sValue, ',') != NULL))
			uLen -= 1

		return uLen > 6 ? vtDouble, vtFloat;
	}

	if(strcmp(sInterp, "int") == 0){
		// Just assume signed
		if(nLen < 2) return vtByte;
		if(nLen < 5) return vtShort;
		if(nLen < 9) return vtInt;
		return vtLong;
	}

	if(strcmp(sInterp, "bool") == 0){
		return vtByte;
	}
	if(strcmp(sInterp, "datetime") == 0){
		return vtTime;
	}
	if(strcmp(sInterp, "string") == 0){
		return vtText;
	}
	return vtUnknown;
}
*/

/* ************************************************************************* */
/* Default Comparison functions */

int vt_cmp_byte(const ubyte* vpFirst, const ubyte* vpSecond){
	const ubyte* pFirst = (const ubyte*) vpFirst;
	const ubyte* pSecond = (const ubyte*) vpSecond;
	if(*pFirst < *pSecond) return -1;
	if(*pFirst > *pSecond) return 1;
	return 0;
}
int vt_cmp_ushort(const ubyte* vpFirst, const ubyte* vpSecond){
	const uint16_t* pFirst = (const uint16_t*) vpFirst;
	const uint16_t* pSecond = (const uint16_t*) vpSecond;
	if(*pFirst < *pSecond) return -1;
	if(*pFirst > *pSecond) return 1;
	return 0;
}
int vt_cmp_short(const ubyte* vpFirst, const ubyte* vpSecond){
	const int16_t* pFirst = (const int16_t*) vpFirst;
	const int16_t* pSecond = (const int16_t*) vpSecond;
	if(*pFirst < *pSecond) return -1;
	if(*pFirst > *pSecond) return 1;
	return 0;
}
int vt_cmp_int(const ubyte* vpFirst, const ubyte* vpSecond){
	const int32_t* pFirst = (const int32_t*) vpFirst;
	const int32_t* pSecond = (const int32_t*) vpSecond;
	if(*pFirst < *pSecond) return -1;
	if(*pFirst > *pSecond) return 1;
	return 0;
}
int vt_cmp_long(const ubyte* vpFirst, const ubyte* vpSecond){
	const int64_t* pFirst = (const int64_t*) vpFirst;
	const int64_t* pSecond = (const int64_t*) vpSecond;
	if(*pFirst < *pSecond) return -1;
	if(*pFirst > *pSecond) return 1;
	return 0;
}
int vt_cmp_float(const ubyte* vpFirst, const ubyte* vpSecond){
	const float* pFirst = (const float*) vpFirst;
	const float* pSecond = (const float*) vpSecond;
	if(*pFirst < *pSecond) return -1;
	if(*pFirst > *pSecond) return 1;
	return 0;
}
int vt_cmp_double(const ubyte* vpFirst, const ubyte* vpSecond){
	const double* pFirst = (const double*) vpFirst;
	const double* pSecond = (const double*) vpSecond;
	if(*pFirst < *pSecond) return -1;
	if(*pFirst > *pSecond) return 1;
	return 0;
}
int vt_cmp_time(const ubyte* vpFirst, const ubyte* vpSecond){
	return dt_compare((const das_time*)vpFirst, (const das_time*)vpSecond);
}
int vt_cmp_text(const ubyte* vpFirst, const ubyte* vpSecond){
	const char** pFirst = (const char**) vpFirst;
	const char** pSecond = (const char**) vpSecond;
	return strcmp(*pFirst, *pSecond);
}

int vt_cmp_byteseq(const ubyte* vpA, const ubyte* vpB)
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

int vt_cmp_geovec(const ubyte* vpA, const ubyte* vpB)
{
	/*const das_geovec* pA = (const das_vector*)vpA; */
	/*const das_geovec* pB = (const das_vector*)vpB; */

	/*das_val_type etA =  das_vec_eltype(pA); */
	/*das_val_type etB =  das_vec_eltype(pB); */

	das_error(DASERR_VALUE, 
		"Vector comparison not yet implemented, infact, I'm not sure"
		" what it should do"
	);
	return 0;
}

das_valcmp_func das_vt_getcmp(das_val_type et)
{
	switch(et){
	case vtUByte: return vt_cmp_byte;
	case vtShort: return vt_cmp_short;
	case vtUShort: return vt_cmp_ushort;
	case vtInt: return vt_cmp_int;
	case vtLong: return vt_cmp_long;
	case vtFloat: return vt_cmp_float;
	case vtDouble: return vt_cmp_double;
	case vtTime: return vt_cmp_time;
	case vtText: return vt_cmp_text;
	case vtByteSeq: return vt_cmp_byteseq;
	case vtGeoVec: return vt_cmp_geovec;
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
	
	bool bShortRight = (left == vtUByte || left == vtShort || left == vtUShort 
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

int das_value_cmpAny(
	const ubyte* pA, das_val_type vtA, const ubyte* pB, das_val_type vtB
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
			return vt_cmp_byteseq(pA, (const ubyte*) &bs);
		}
		else{
			bs.ptr = pA;
			bs.sz = das_vt_size(vtA);
			return vt_cmp_byteseq((const ubyte*)&bs, pB);		
		}
	}
	
	/* Generic numeric comparisons below... These need a little work.  */
	/* I'm sure there are many patterns that could be exploted to make */
	/* the code shorter.  In general there are 8x8 types, for 64       */
	/* different code paths.  But many of them collapse                */
	
	/* In D we'd use the 80-bit numeric types to make this easier, have */
	/* to do it manually for VC++                                       */

	int64_t lA = 0, lB = 0;
	ubyte uAHas = 0;  /* float = 0x2 */
	ubyte uBHas = 0;  /* int =   0x1 */
	switch(vtA){
	case vtUByte:   lA = *((const uint8_t*)pA);  dA = *((const uint8_t*)pA);  uAHas = HAS_BOTH; break;
	case vtShort:  lA = *((const int16_t*)pA);  dA = *((const int16_t*)pA);  uAHas = HAS_BOTH; break;
	case vtUShort: lA = *((const uint16_t*)pA); dA = *((const uint16_t*)pA); uAHas = HAS_BOTH; break;
	case vtInt:    lA = *((const int32_t*)pA);  dA = *((const int32_t*)pA);  uAHas = HAS_BOTH; break;
	
	case vtLong:   lA = *((const int64_t*)pA);  /* No float value */         uAHas = HAS_INT;  break;
	
	case vtFloat:  /* No int value */           dA = *((const float*)pA);    uAHas = HAS_FLT;  break;
	case vtDouble: /* No int value */           dA = *((const double*)pA);   uAHas = HAS_FLT;  break;
	default: return -2;
	}
	
	switch(vtB){
	case vtUByte:   lB = *((const uint8_t*)pB);  dB = *((const uint8_t*)pB);  uBHas = HAS_BOTH; break;
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
	fprintf(stderr, "TODO: Long to double comparison needed in das2C\n");

	return -2;
}

#undef HAS_INT
#undef HAS_FLT
#undef HAS_BOTH

/* ************************************************************************** */
/* Convert any one itegral value type into any other, with range and 
 * resolution checks 
 */

/* No range checks are needed to go big to same signed type, 
   or larger type with different sign 
*/
#define GO_BIG(TY_OUT, TY_IN) ( *((TY_OUT*)pO) = *((TY_IN*)pI) )

/* Go to bigger or same size, so long as the value is 0 or greater */
#define GO_POSI(TY_OUT, TY_IN) \
  if(bRng &&( *((TY_IN*)pI) < 0) ){ goto ERR_RANGE;} *((TY_OUT*)pO) = *((TY_IN*)pI)

/* Go to the bigger size so long as val is > 0 and < max */
#define GO_ZMAX(TY_OUT, TY_IN, MAX_OK) \
  if(bRng &&( (*((TY_IN*)pI) < 0) || (*((TY_IN*)pI) > MAX_OK ) ) ){goto ERR_RANGE;} *((TY_OUT*)pO) = *((TY_IN*)pI)

/* Go to other size if under a maximum limit */
#define GO_MAX(TY_OUT, TY_IN, MAX_OK) \
  if(bRng && (*((TY_IN*)pI) > MAX_OK )){ goto ERR_RANGE;} *((TY_OUT*)pO) = *((TY_IN*)pI)

/* Go to other size if within range */
#define GO_RNG(TY_OUT, TY_IN, MIN_OK, MAX_OK) \
	if(bRng &&( (*((TY_IN*)pI) < MIN_OK) || (*((TY_IN*)pI) > MAX_OK ) ) ){ goto ERR_RANGE;} *((TY_OUT*)pO) = *((TY_IN*)pI)	  

/* Go to the other size if max value doesn't incure resolution loss */
#define GO_ZRES(TY_OUT, TY_IN, MAX_OK) \
  if(bRes && (*((TY_IN*)pI) > MAX_OK )){ goto ERR_RESLOSS;} *((TY_OUT*)pO) = *((TY_IN*)pI)

/* Go to the other size if min or max value don't incure resolution loss */
#define GO_RES(TY_OUT, TY_IN, MIN_OK, MAX_OK) \
	if(bRes &&( (*((TY_IN*)pI) < MIN_OK) || (*((TY_IN*)pI) > MAX_OK ) ) ){ goto ERR_RESLOSS;} *((TY_OUT*)pO) = *((TY_IN*)pI)	  

/* Got to an integer if I'm in range, and lose too much resolution, 

   Min and max are straightforward, but resolution loss is hard to quantify.
   If the fractional part is far enough away from an integer, call it a conversion error.  Thus:

     1.9998 would convert to 2 without error but
     1.5    would not.
*/
#define GO_TRUNC(TY_IN, TY_OUT, MIN_OK, MAX_OK, EPSILON) \
	rIn = *((TY_IN*)pI); \
	if(bRng && ((rIn < MIN_OK)||(rIn > MAX_OK))) goto ERR_RANGE; \
	if(bRes){ rRnd = round(rIn); if( fabs(rIn - rRnd) > EPSILON) goto ERR_RESLOSS; } \
	*((TY_OUT*)pO) = rIn


DasErrCode das_value_binXform(
   das_val_type vtIn,  const ubyte* pI, const ubyte* pFI, 
   das_val_type vtOut,       ubyte* pO, const ubyte* pFO,
   uint32_t uFlags
){
	DasErrCode nRet;

	/* Handle fill up-front */
	size_t uInSz  = das_vt_size(vtIn);
	size_t uOutSz = das_vt_size(vtOut);
	if((pFI != NULL) && (memcmp(pI, pFI, uInSz) == 0)){
		memcpy(pO, pFO, uOutSz);
		return DAS_OKAY;
	}

	bool bRng = (uFlags & DAS_VAL_NOERR_RNG) ? false : true;  /* on true, issue range errors */
	bool bRes = (uFlags & DAS_VAL_ERR_RESLOSS);
	double rIn, rRnd; /* Used by the trunc macros */

	/* Value conversions */
	switch(vtIn){
	case vtUByte :
		switch(vtOut){
		case vtUByte : GO_BIG( uint8_t, uint8_t          ); break; /* works for same */
		case vtByte  : GO_MAX(  int8_t, uint8_t, INT8_MAX); break;
		case vtUShort: GO_BIG(uint16_t, uint8_t          ); break;
		case vtShort : GO_BIG( int16_t, uint8_t          ); break;
		case vtUInt  : GO_BIG(uint32_t, uint8_t          ); break;
		case vtInt   : GO_BIG( int32_t, uint8_t          ); break;
		case vtULong : GO_BIG(uint64_t, uint8_t          ); break;
		case vtLong  : GO_BIG( int64_t, uint8_t          ); break;
		case vtFloat : GO_BIG(   float, uint8_t          ); break;
		case vtDouble: GO_BIG(  double, uint8_t          ); break;
		default: goto ERR_NO_XFORM; break;
		}
		break;
	case vtByte  :
		switch(vtOut){
		case vtUByte : GO_POSI( uint8_t,  int8_t);  break;
		case vtByte  : GO_BIG (  int8_t,  int8_t);  break; /* works for same */
		case vtUShort: GO_POSI(uint16_t,  int8_t);  break;
		case vtShort : GO_BIG ( int16_t,  int8_t);  break;
		case vtUInt  : GO_POSI(uint32_t,  int8_t);  break;
		case vtInt   : GO_BIG ( int32_t,  int8_t);  break;
		case vtULong : GO_POSI(uint64_t,  int8_t);  break;
		case vtLong  : GO_BIG ( int64_t,  int8_t);  break;
		case vtFloat : GO_BIG (   float,  int8_t);  break;
		case vtDouble: GO_BIG (  double,  int8_t);  break;
		default: goto ERR_NO_XFORM; break;
		}
		break;
	case vtUShort:
		switch(vtOut){
		case vtUByte : GO_MAX( uint8_t, uint16_t,  UINT8_MAX); break; 
		case vtByte  : GO_MAX(  int8_t, uint16_t,   INT8_MAX); break;
		case vtUShort: GO_BIG(uint16_t, uint16_t);            break; /* works for same */
		case vtShort : GO_MAX(  int8_t, uint16_t,  INT16_MAX); break;
		case vtUInt  : GO_BIG(uint32_t, uint16_t            ); break;
		case vtInt   : GO_BIG( int32_t, uint16_t            ); break;
		case vtULong : GO_BIG(uint64_t, uint16_t            ); break;
		case vtLong  : GO_BIG( int64_t, uint16_t            ); break;
		case vtFloat : GO_BIG(   float, uint16_t            ); break;
		case vtDouble: GO_BIG(  double, uint16_t            ); break;
		default: goto ERR_NO_XFORM; break;
		}
		break;
	case vtShort :
		switch(vtOut){
		case vtUByte : GO_ZMAX( uint8_t,  int16_t, UINT8_MAX); break;
		case vtByte  : GO_RNG(   int8_t,  int16_t,  INT8_MIN, INT8_MAX); break;
		case vtUShort: GO_POSI(uint16_t,  int16_t);  break;
		case vtShort : GO_BIG ( int16_t,  int16_t);  break; /* works for same */
		case vtUInt  : GO_POSI(uint32_t,  int16_t);  break;
		case vtInt   : GO_BIG ( int32_t,  int16_t);  break;
		case vtULong : GO_POSI(uint64_t,  int16_t);  break; 
		case vtLong  : GO_BIG ( int64_t,  int16_t);  break;
		case vtFloat : GO_BIG (   float,  int16_t);  break;
		case vtDouble: GO_BIG (  double,  int16_t);  break;
		default: goto ERR_NO_XFORM; break;
		}
		break;
	case vtUInt  :
		switch(vtOut){
		case vtUByte : GO_MAX ( uint8_t, uint32_t,  UINT8_MAX); break;
		case vtByte  : GO_MAX (  int8_t, uint32_t,   INT8_MAX); break;
		case vtUShort: GO_MAX (uint16_t, uint32_t, UINT16_MAX); break;
		case vtShort : GO_MAX ( int16_t, uint32_t,  INT16_MAX); break;
		case vtUInt  : GO_BIG (uint32_t, uint32_t);             break; /* works for same */
		case vtInt   : GO_MAX ( int32_t, uint32_t, UINT32_MAX); break;
		case vtULong : GO_BIG (uint64_t, uint32_t);             break;
		case vtLong  : GO_BIG ( int64_t, uint32_t);             break;
		case vtFloat : GO_ZRES(   float, uint32_t,   8388608U); break;
		case vtDouble: GO_BIG (  double, uint32_t);             break;
		default: goto ERR_NO_XFORM; break;
		}
		break;
	case vtInt   :
		switch(vtOut){
		case vtUByte : GO_ZMAX( uint8_t,  int32_t,  UINT8_MAX);            break;
		case vtByte  : GO_RNG(   int8_t,  int32_t,   INT8_MIN,  INT8_MAX); break;
		case vtUShort: GO_ZMAX(uint16_t,  int32_t, UINT16_MAX);            break;
		case vtShort : GO_RNG(  int16_t,  int32_t,  INT16_MIN, INT16_MAX); break;
		case vtUInt  : GO_POSI(uint32_t,  int32_t);                        break;
		case vtInt   : GO_BIG ( int32_t,  int32_t);                        break; /* works for same */
		case vtULong : GO_POSI(uint64_t,  int32_t);                        break;
		case vtLong  : GO_BIG ( int64_t,  int32_t);                        break;
		case vtFloat : GO_RES (   float,  int32_t,  -8388608, 8388608);    break;
		case vtDouble: GO_BIG (  double,  int32_t);                        break;
		default: goto ERR_NO_XFORM; break;
		}
		break;
	case vtULong :
		switch(vtOut){
		case vtUByte : GO_MAX( uint8_t, uint64_t,  UINT8_MAX); break;
		case vtByte  : GO_MAX(  int8_t, uint64_t,   INT8_MAX); break;
		case vtUShort: GO_MAX(uint16_t, uint64_t, UINT16_MAX); break;
		case vtShort : GO_MAX( int16_t, uint64_t,  INT16_MAX); break;
		case vtUInt  : GO_MAX(uint32_t, uint64_t, UINT32_MAX); break;
		case vtInt   : GO_MAX( int32_t, uint64_t,  INT32_MAX); break;
		case vtULong : GO_BIG(uint64_t, uint64_t);             break; /* works for same */
		case vtLong  : GO_MAX( int64_t, uint64_t,  INT64_MAX); break;
		case vtFloat : GO_ZRES(  float, uint32_t,     999999); break;
		case vtDouble: GO_ZRES( double, uint32_t, 9007199254740992ULL ); break;
		default: goto ERR_NO_XFORM; break;
		}
		break;
	case vtLong  :
		switch(vtOut){
		case vtUByte : GO_ZMAX( uint8_t,  int64_t,  UINT8_MAX);            break;
		case vtByte  : GO_RNG(   int8_t,  int64_t,   INT8_MIN,  INT8_MAX); break;
		case vtUShort: GO_ZMAX(uint16_t,  int64_t, UINT16_MAX);            break;
		case vtShort : GO_RNG(  int16_t,  int64_t,  INT16_MIN, INT16_MAX); break;
		case vtUInt  : GO_ZMAX(uint32_t,  int64_t, UINT32_MAX);            break;
		case vtInt   : GO_RNG(  int32_t,  int64_t,  INT32_MIN, INT32_MAX); break;
		case vtULong : GO_POSI(uint64_t,  int64_t);                        break;
		case vtLong  : GO_BIG ( int64_t,  int64_t);                        break; /* works for same */
		case vtFloat : GO_RES (   float,  int32_t, -9007199254740992LL, 9007199254740992LL);  break;
		case vtDouble: break;
		default: goto ERR_NO_XFORM; break;
		}
		break;
	case vtFloat :
		switch(vtOut){
		case vtUByte : GO_TRUNC( uint8_t, float,         0,  UINT8_MAX, 0.02); break;
		case vtByte  : GO_TRUNC(  int8_t, float,  INT8_MIN,   INT8_MAX, 0.02); break;
		case vtUShort: GO_TRUNC(uint16_t, float,         0, UINT16_MAX, 0.02); break;
		case vtShort : GO_TRUNC( int16_t, float, INT16_MIN,  INT16_MAX, 0.02); break;
		case vtUInt  : GO_TRUNC(uint32_t, float,         0, UINT32_MAX, 0.02); break;
		case vtInt   : GO_TRUNC( int32_t, float, INT32_MIN,  INT32_MAX, 0.02); break;
		case vtULong : GO_TRUNC(uint64_t, float,         0, UINT64_MAX, 0.02); break;
		case vtLong  : GO_TRUNC( int64_t, float, INT64_MIN,  INT64_MAX, 0.02); break;
		case vtFloat : GO_BIG(     float, float);  break; /* works for same */
		case vtDouble: GO_BIG(    double, float);  break;
		default: goto ERR_NO_XFORM; break;
		}
		break;
	case vtDouble:
		switch(vtOut){
		case vtUByte : GO_TRUNC( uint8_t, double,         0,  UINT8_MAX, 0.02); break;
		case vtByte  : GO_TRUNC(  int8_t, double,  INT8_MIN,   INT8_MAX, 0.02); break;
		case vtUShort: GO_TRUNC(uint16_t, double,         0, UINT16_MAX, 0.02); break;
		case vtShort : GO_TRUNC( int16_t, double, INT16_MIN,  INT16_MAX, 0.02); break;
		case vtUInt  : GO_TRUNC(uint32_t, double,         0, UINT32_MAX, 0.02); break;
		case vtInt   : GO_TRUNC( int32_t, double, INT32_MIN,  INT32_MAX, 0.02); break;
		case vtULong : GO_TRUNC(uint64_t, double,         0, UINT64_MAX, 0.02); break;
		case vtLong  : GO_TRUNC( int64_t, double, INT64_MIN,  INT64_MAX, 0.02); break;
		case vtFloat : GO_RNG(     float, double, (-1*FLT_MAX), FLT_MAX); break;
		case vtDouble: GO_BIG(    double, double);                        break; /* works for same */
		default: goto ERR_NO_XFORM; break;
		}
		break;

	default: goto ERR_NO_XFORM; break;
	}

	return DAS_OKAY;

	char sFmt[32] = {'\0'};
	char sVal[32] = {'\0'};

ERR_RESLOSS:
	nRet = das_value_fmt(sFmt, 31, vtIn, "", -1);
	if(nRet != DAS_OKAY) return nRet;
	snprintf(sVal, 31, sFmt, pI);  /* <-- compilers hate variable fmt strings */
	return das_error(DASERR_VALUE, "Resolution loss converting %s (%s) to %s",
		sVal, das_vt_toStr(vtIn), das_vt_toStr(vtOut)
	);

ERR_RANGE:
	nRet = das_value_fmt(sFmt, 31, vtIn, "", -1);
	if(nRet != DAS_OKAY) return nRet;
	snprintf(sVal, 31, sFmt, pI);  /* <-- compilers hate variable fmt strings */
	return das_error(DASERR_VALUE, "Range violation converting %s (%s) to %s",
		sVal, das_vt_toStr(vtIn), das_vt_toStr(vtOut)
	);

ERR_NO_XFORM:
	return das_error(DASERR_VALUE, "No conversion from %s to %s defined",
		das_vt_toStr(vtIn), das_vt_toStr(vtOut)
	);
}

/* ************************************************************************** */
/* Parse any string into a value */

DasErrCode das_value_fromStr(
   ubyte* pBuf, int nBufLen, das_val_type vt, const char* sStr
){
	if(sStr == NULL){
		return das_error(DASERR_VALUE, "Empty string can't be converted to a value");
	}
	/* See if the buffer is big enough for the binary size of this values type */
	if(nBufLen < das_vt_size(vt)){
		return das_error(DASERR_VALUE, 
			"Output buffer is too small %d bytes to hold a value of type %s",
			nBufLen, das_vt_toStr(vt)
		);
	}

	switch(vt){
	case vtUnknown:
		return das_error(DASERR_VALUE, "Cannot determine fill values for unknown types");

	case vtText:
		{
			size_t nLen = strlen(sStr);
			if(nLen <= nBufLen){
				strncpy((char*)pBuf, sStr, nLen);
				return DAS_OKAY;
			}
		}
		daslog_error_v(
			"string value '%s' can't fit into %d byte buffer", sStr, nBufLen
		);
		return DASERR_VALUE;

	case vtUByte:
	case vtByteSeq:
		return sscanf(sStr, "%hhu", (uint8_t*)pBuf) == 1 ? DAS_OKAY : DASERR_VALUE;
	case vtByte:
		return sscanf(sStr, "%hhd", (int8_t*)pBuf) == 1 ? DAS_OKAY : DASERR_VALUE;
	case vtUShort:
		return sscanf(sStr, "%hu", (uint16_t*)pBuf) == 1 ? DAS_OKAY : DASERR_VALUE;
	case vtShort:
		return sscanf(sStr, "%hd", (int16_t*)pBuf) == 1 ? DAS_OKAY : DASERR_VALUE;
	case vtUInt:
		return sscanf(sStr, "%u", (uint32_t*)pBuf) == 1 ? DAS_OKAY : DASERR_VALUE;
	case vtInt:
		return sscanf(sStr, "%d", (int32_t*)pBuf) == 1 ? DAS_OKAY : DASERR_VALUE;
	case vtULong:
#ifdef HOST_IS_64_BIT
		return sscanf(sStr, 
#if defined(_WIN32) || defined(__APPLE__)
			"%llu"
#else
			"%lu"
#endif
			, (uint64_t*)pBuf) == 1 ? DAS_OKAY : DASERR_VALUE;
#else
		return sscanf(sStr, "%Lu", (uint64_t*)pBuf) == 1 ? DAS_OKAY : DASERR_VALUE;
#endif
	case vtLong:
#ifdef HOST_IS_64_BIT
		return sscanf(sStr, 
#if defined(_WIN32) || defined(__APPLE__)
			"%lld"
#else
			"%ld"
#endif
			, (int64_t*)pBuf) == 1 ? DAS_OKAY : DASERR_VALUE;
#else
		return sscanf(sStr, "%Ld", (int64_t*)pBuf) == 1 ? DAS_OKAY : DASERR_VALUE;
#endif
	case vtFloat:
		return sscanf(sStr, "%f", (float*)pBuf) == 1 ? DAS_OKAY : DASERR_VALUE;
	case vtDouble:
		return sscanf(sStr, "%lf", (double*)pBuf) == 1 ? DAS_OKAY : DASERR_VALUE;
	case vtTime:
		return dt_parsetime(sStr, (das_time*)pBuf) == 1 ? DAS_OKAY : DASERR_VALUE;
	default:
		return das_error(DASERR_VALUE, "Unknown value type code: %d", vt);
	}
}

/* ************************************************************************* */
/* Generate a printf string for any value type, if with supplied try to fit 
 * it in a certian width.  It's often that case that values are stored in
 * types that have far greater range then the actual data */

/* Don't fail for certian semantics, but do offer extra support where detected */
DasErrCode das_value_fmt(
	char* sBuf, int nBufLen, das_val_type vt, const char* sSemantic, int nFitTo
){
	bool bBin = (strcmp(sSemantic, DAS_SEM_BIN) == 0);
	bool bText = (strcmp(sSemantic, DAS_SEM_TEXT) == 0);

	switch(vt){
	case vtUByte:
		if(nFitTo < 1){
			if(bBin)       strncpy(sBuf, "%0hhX", nBufLen);
			else if(bText) strncpy(sBuf,    "%s", nBufLen);
			else           strncpy(sBuf,  "%hhu", nBufLen);
		}
		else{
			if(bBin)       snprintf(sBuf, nBufLen, "%%0%dhhX", nFitTo); 
			else if(bText) snprintf(sBuf, nBufLen,   "%% %ds", nFitTo); 
			else           snprintf(sBuf, nBufLen, "%% %dhhu", nFitTo);
		}
		break;
	case vtByte:
		if(nFitTo < 1){
			if(bText) strncpy(sBuf,   "%s", nBufLen);
			else      strncpy(sBuf, "%hhd", nBufLen);
		}
		else{
			if(bText) snprintf(sBuf, nBufLen,   "%% %ds", nFitTo); 
			else      snprintf(sBuf, nBufLen, "%% %dhhd", nFitTo); 
		}
		break;


	case vtUShort:
		if(nFitTo < 1){
			if(bBin) strncpy(sBuf, "%0hX", nBufLen);
			else     strncpy(sBuf,  "%hu", nBufLen);
		}
		else{
			if(bBin) snprintf(sBuf, nBufLen, "%%0%dhX", nFitTo);
			else     snprintf(sBuf, nBufLen, "%% %dhu", nFitTo);
		}
		break;
	case vtShort:
		if(nFitTo < 1) strncpy(sBuf, "%hd", nBufLen);
		else           snprintf(sBuf, nBufLen, "%% %dhd", nFitTo);
		break;


	case vtUInt:
		if(nFitTo < 1){
			if(bBin) strncpy(sBuf, "%0X", nBufLen);
			else     strncpy(sBuf,  "%u", nBufLen);
		}
		else{
			if(bBin) snprintf(sBuf, nBufLen, "%%0%dX", nFitTo);
			else     snprintf(sBuf, nBufLen, "%% %du", nFitTo);
		}
		break;
	case vtInt:
		if(nFitTo < 1) strncpy(sBuf, "%d", nBufLen);
		else           snprintf(sBuf, nBufLen, "%% %dd", nFitTo);
		break;

	case vtULong:
		if(nFitTo < 1){
			if(bBin) strncpy(sBuf, "%0" PRIX64, nBufLen); /* from inttypes.h */
			else     strncpy(sBuf,  "%" PRIu64, nBufLen);
		}
		else{
			if(bBin) snprintf(sBuf, nBufLen, "%%0%d" PRIX64, nFitTo);
			else     snprintf(sBuf, nBufLen, "%% %d" PRIu64, nFitTo);
		}
		break;
	case vtLong:
		if(nFitTo < 1) strncpy(sBuf,  "%" PRId64, nBufLen);
		else           snprintf(sBuf, nBufLen, "%% %d" PRId64, nFitTo);
		break;

	case vtFloat:
	case vtDouble:
		if(nFitTo < 1){
			if(vt == vtFloat) strncpy(sBuf, "%.4e", nBufLen);
			else              strncpy(sBuf, "%.8e", nBufLen);
		}
		else{ 
			/* Get a precision as a function of field width if it's big enough 
			 * to give 2 digits after the decimal */
			if(nFitTo >= 9)
				snprintf(sBuf, nBufLen, "%% %d.%de", nFitTo, nFitTo - 6);
			else
				snprintf(sBuf, nBufLen, "%% %d.%de", nFitTo, 2);
		}
		break;

	case vtTime:
		if(nFitTo < 1){
			/* No guidance, just pick milliseconds, usually pretty good in space physics */
			strncpy(sBuf, "%04d-%02d-%02dT%02d:%02d:%06.3f", nBufLen);
		}
		else{
			/* Okay, they want resolution to fit a particular width, here goes... */
			switch(nFitTo){
			/* Year only */
			case 4: strncpy(sBuf, "%04d", nBufLen); break;
			case 5: strncpy(sBuf, "%04d ", nBufLen); break;
			case 6: strncpy(sBuf, "%04d  ", nBufLen); break;
			
			/* Year and Month */
			case 7: strncpy(sBuf,  "%04d-%02d",nBufLen); break;
			case 8: strncpy(sBuf,  "%04d-%02d ",nBufLen); break;
			case 9: strncpy(sBuf,  "%04d-%02d  ",nBufLen); break;
			
			/* Year/Month/Day of Month */
			case 10: strncpy(sBuf, "%04d-%02d-%02d",nBufLen); break;
			case 11: strncpy(sBuf, "%04d-%02d-%02d ",nBufLen); break;
			case 12: strncpy(sBuf, "%04d-%02d-%02d  ",nBufLen); break;
			
			/* Date + Hour */
			case 13: strncpy(sBuf, "%04d-%02d-%02dT%02d",nBufLen); break;
			case 14: strncpy(sBuf, "%04d-%02d-%02dT%02d ",nBufLen); break;
			case 15: strncpy(sBuf, "%04d-%02d-%02dT%02d  ",nBufLen); break;
			
			/* Date + Hour:min */
			case 16: strncpy(sBuf, "%04d-%02d-%02dT%02d:%02d",nBufLen); break;
			case 17: strncpy(sBuf, "%04d-%02d-%02dT%02d:%02d ",nBufLen); break;
			case 18: strncpy(sBuf, "%04d-%02d-%02dT%02d:%02d  ",nBufLen); break;
			
			/* Date + Hour:min:sec */
			case 19: strncpy(sBuf, "%04d-%02d-%02dT%02d:%02d:%02.0f",nBufLen); break;
			case 20: strncpy(sBuf, "%04d-%02d-%02dT%02d:%02d:%02.0f ",nBufLen); break;
			
			/* Date + hour:min:sec + frac seconds */
			case 21: strncpy(sBuf, "%04d-%02d-%02dT%02d:%02d:%04.1f",nBufLen); break;
			case 22: strncpy(sBuf, "%04d-%02d-%02dT%02d:%02d:%05.2f",nBufLen); break;
			case 23: strncpy(sBuf, "%04d-%02d-%02dT%02d:%02d:%06.3f",nBufLen); break;
			case 24: strncpy(sBuf, "%04d-%02d-%02dT%02d:%02d:%06.3f ",nBufLen); break;
			case 25: strncpy(sBuf, "%04d-%02d-%02dT%02d:%02d:%06.3f  ",nBufLen); break;
			case 26: strncpy(sBuf, "%04d-%02d-%02dT%02d:%02d:%09.6f",nBufLen); break;
			case 27: strncpy(sBuf, "%04d-%02d-%02dT%02d:%02d:%09.6f ",nBufLen); break;
			case 28: strncpy(sBuf, "%04d-%02d-%02dT%02d:%02d:%09.6f  ",nBufLen); break;
			case 29: strncpy(sBuf, "%04d-%02d-%02dT%02d:%02d:%012.9f",nBufLen); break;
			
			/* No more resolution available, just space pad */
			default: 
				strncpy(sBuf, "%04d-%02d-%02dT%02d:%02d:%012.9f",nBufLen); break;
				for(int i = 32; i < nBufLen; ++i) sBuf[i] = ' ';
			}
		}
		break;

	default:
		return das_error(DASERR_VALUE, 
			"Default format string not available for type '%s'", das_vt_toStr(vt)
		);
	}

	return DAS_OKAY;
}

/* ************************************************************************* */
/* Helper for trimming zeros after decimal */

void das_value_trimReal(char* sVal){
		
	if(strchr(sVal, '.') == NULL) return;
	
	/* technically could handle normalizing stuff like
	   10000e6 as well, but that's rare so forget it for now */
	
	int iDec = strchr(sVal, '.') - sVal;
	int iExp = -1;
	int v = strlen(sVal);
	int i = -1, j = -1;
	
	if(strchr(sVal, 'e') || strchr(sVal, 'E')){
		if(strchr(sVal, 'e')) iExp = strchr(sVal, 'e') - sVal;
		else                  iExp = strchr(sVal, 'E') - sVal;
		
		for(i = iExp-1; i > iDec; --i){
			/* Shift out Zeros after decimal but before exponent*/
			if(sVal[i] == '0'){
				for(j = i; j < v-1; ++j)
					sVal[j] = sVal[j+1];
				--v;
				sVal[v] = '\0';
				--iExp;  
			}
			else{
				break;
			}
		}	
	}
	else{
		while((v > 0) && (sVal[ v - 1] == '0')){
			/* NULL out Zeros after the decimal*/
			sVal[ v - 1] = '\0';
			v = strlen(sVal);
		}
	}
}


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

char* das_doubles2csv(char* pBuf, size_t uSz, const double* pValues, int nValues) 
{
	if((pBuf == NULL)||(uSz < 12)){
		das_error(DASERR_ARRAY, "Invalid arguments, buffer too small or non-existant");
		return NULL;
	}

	/* Insure null termination */
	pBuf[uSz - 1] = '\0';
	--uSz;

	// Decrement uSz after each write
	size_t uWrote;
	for(int i = 0; i < nValues; ++i){
		if((fabs(pValues[i]) < 0.00001)||(fabs(pValues[i]) > 100000))
			uWrote = snprintf(pBuf, uSz, "%e", pValues[i]);
		else
			uWrote = snprintf(pBuf, uSz, "%f", pValues[i]);

		if(uWrote > uSz){
			das_error(DASERR_ARRAY, "Insufficient space provided for all %d converted doubles", nValues);
			return NULL;
		}
		uSz -= uWrote;
	}
	
	return pBuf;
}

char* das_floats2csv(char* pBuf, size_t uSz, const float* pValues, int nValues) 
{
	if((pBuf == NULL)||(uSz < 12)){
		das_error(DASERR_ARRAY, "Invalid arguments, buffer too small or non-existant");
		return NULL;
	}

	/* Insure null termination */
	pBuf[uSz - 1] = '\0';
	--uSz;

	// Decrement uSz after each write
	size_t uWrote;
	for(int i = 0; i < nValues; ++i){
		if((fabs(pValues[i]) < 0.00001)||(fabs(pValues[i]) > 100000))
			uWrote = snprintf(pBuf, uSz, "%e", (double)(pValues[i]));
		else
			uWrote = snprintf(pBuf, uSz, "%f", (double)(pValues[i]));

		if(uWrote > uSz){
			das_error(DASERR_ARRAY, "Insufficient space provided for all %d converted floats", nValues);
			return NULL;
		}
		uSz -= uWrote;
	}
	
	return pBuf;
}
