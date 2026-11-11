/* Copyright (C) 2018-2026 Chris Piker <chris-piker@uiowa.edu>
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
#include "time.h"
#include "value.h"
#include "array.h"
#include "array_p.h"
#include "oper.h"
#include "log.h"
#include "value_p.h"

/* ************************************************************************* */
/* value type functions */

static const das_idx_info g_idxFill = {0, 0};
static const uint8_t g_ubyteFill = 255;
static const int8_t  g_byteFill = -128;
static const uint16_t g_ushortFill = 65535;
static const int16_t g_shortFill = -32767;
static const uint32_t g_uintFill = 4294967295;
static const int32_t g_intFill = -2147483647;
static const int64_t g_longFill = -9223372036854775807L;
static const uint64_t g_ulongFill = 18446744073709551615UL;
static const float g_floatFill = DASVAL_FILL;
static const double g_doubleFill = DASVAL_FILL;
static const DasTime g_timeFill = {1, 1, 1, 1, 0, 0, 0.0};

static const void* g_voidFill = NULL;
static const DasCByteSeq g_byteSeqFill = {NULL, 0};

bool dasval_isFill(double value)
{
	return fabs((DASVAL_FILL - value)/DASVAL_FILL) < 0.00001;
}

const void* dasvt_fill(das_vt et)
{
	switch(et){
	case DASVT_INDEX: return &(g_idxFill);
	case DASVT_UBYTE: return &(g_ubyteFill);
	case DASVT_BYTE:  return &(g_byteFill);
	case DASVT_USHORT: return &(g_ushortFill);
	case DASVT_SHORT: return &(g_shortFill);
	case DASVT_UINT:  return &(g_uintFill);
	case DASVT_INT: return &(g_intFill);
	case DASVT_ULONG: return &(g_ulongFill);
	case DASVT_LONG: return &(g_longFill);
	case DASVT_FLOAT: return &(g_floatFill);
	case DASVT_DOUBLE: return &(g_doubleFill);
	case DASVT_TIME: return &(g_timeFill);
	case DASVT_TEXT:   return &(g_voidFill);
	case DASVT_BYTESEQ: return &(g_byteSeqFill);
	default:	return NULL;
	}
}

size_t dasvt_size(das_vt et)
{
	switch(et){
	case DASVT_NULL:  return 0;   /* holds nothing */
	case DASVT_INDEX: return sizeof(g_idxFill);

	case DASVT_BYTE:  case DASVT_UBYTE:  return 1;
	case DASVT_SHORT: case DASVT_USHORT: return 2;
	case DASVT_UINT:  case DASVT_INT:    case DASVT_FLOAT:  return 4;
	case DASVT_LONG:  case DASVT_ULONG:  case DASVT_DOUBLE: return 8;
	
	case DASVT_TIME: return sizeof(g_timeFill);
	case DASVT_TEXT: return sizeof(char*);
	case DASVT_BYTESEQ: return sizeof(DasCByteSeq);
	default:
		daserrf(DASERR_ARRAY, "Program logic error");
		return 0;
	}
}

const char* dasvt_name(das_vt et)
{
	switch(et){
	case DASVT_NULL:    return "null";
	case DASVT_INDEX:   return "index_info";
	case DASVT_UBYTE:   return "ubyte";
	case DASVT_BYTE:    return "byte";
	case DASVT_USHORT:  return "ushort";
	case DASVT_SHORT:   return "short";
	case DASVT_UINT:    return "uint";
	case DASVT_INT:     return "int";
	case DASVT_ULONG:   return "ulong";
	case DASVT_LONG:    return "long";
	case DASVT_FLOAT:   return "float";
	case DASVT_DOUBLE:  return "double";
	case DASVT_TIME:    return "das_time";
	case DASVT_TEXT:    return "char*";
	case DASVT_BYTESEQ: return "ubyte*";

	/* something made up of a collection of one of the other types above  */
	case DASVT_COMP: return "composite";

	/* App defined type case, we don't know what this is but we know
	   it's something someone else might understand */
	case DASVT_UNK: return "unknown";

	default: return NULL;
	}
}

das_vt dasvt_id(const char* sStorage)
{
	if(strcasecmp(sStorage, "float") == 0)  return DASVT_FLOAT;
	if(strcasecmp(sStorage, "double") == 0) return DASVT_DOUBLE;
	if(strcasecmp(sStorage, "int") == 0)    return DASVT_INT;
	if(strcasecmp(sStorage, "short") == 0)  return DASVT_SHORT;
	if(strcasecmp(sStorage, "long") == 0)   return DASVT_LONG;
	if(strcasecmp(sStorage, "uint") == 0)   return DASVT_UINT;
	if(strcasecmp(sStorage, "ushort") == 0) return DASVT_USHORT;
	if(strcasecmp(sStorage, "ulong") == 0)  return DASVT_ULONG;
	if(strcasecmp(sStorage, "byte") == 0)   return DASVT_BYTE;
	if(strcasecmp(sStorage, "ubyte") == 0)  return DASVT_UBYTE;
	/* the wire token.  This works for now because the only struct that
	   is streamed is a DasTime.  May need to revisit this in future
	   stream versions. */
	if(strcasecmp(sStorage, "struct") == 0) return DASVT_TIME;
	if(strcasecmp(sStorage, "das_time") == 0) return DASVT_TIME; /* dasvt_name's */
	if(strcasecmp(sStorage, "utf8") == 0)   return DASVT_TEXT;
	if(strcasecmp(sStorage, "char*") == 0)  return DASVT_TEXT;
	if(strcasecmp(sStorage, "ubyte*") == 0) return DASVT_BYTESEQ;
	return DASVT_UNK;
}

/* Useful for picking a storage type when it's not explicitly stated */
das_vt das_vt_store_type(
	const char* sEncType, int nItemBytes, const char* sInterp
){
	/* Canonicalize the encoding to its DAS_ENC_* singleton, then dispatch by pointer --
	   the encoding vocabulary lives in das_enc_fromStr, not duplicated here. */
	const char* sEnc = das_enc_fromStr(sEncType);
	if(sEnc == DASENC_BYTE){
		return DASVT_BYTE;  // promote to higher type to get sign bits
	}
	if(sEnc == DASENC_UBYTE){
		return DASVT_UBYTE;
	}
	if((sEnc == DASENC_BEINT)||(sEnc == DASENC_LEINT)){
		if(nItemBytes == 2) return DASVT_SHORT;
		else if(nItemBytes == 4) return DASVT_INT;
		else if(nItemBytes == 8) return DASVT_LONG;
		else{
			daserrf(DASERR_VALUE, "Unsupported length %d for binary integers", nItemBytes);
			return DASVT_UNK;
		}
	}
	if((sEnc == DASENC_BEUINT)||(sEnc == DASENC_LEUINT)){
		if(nItemBytes == 2) return DASVT_USHORT;
		else if(nItemBytes == 4) return DASVT_UINT;
		else if(nItemBytes == 8) return DASVT_ULONG;
		else{
			daserrf(DASERR_VALUE, "Unsupported length %d for binary integers", nItemBytes);
			return DASVT_UNK;
		}
	}
	if((sEnc == DASENC_BEREAL)||(sEnc == DASENC_LEREAL)){
		if(nItemBytes == 4) return DASVT_FLOAT;
		else if (nItemBytes == 8) return DASVT_DOUBLE;
		else{
			daserrf(DASERR_VALUE, "Unsupported length %d for binary floating point values", nItemBytes);
			return DASVT_UNK;	
		}
	}

	/* Okay it's a text type, deal with that.  Normalize the semantic through the one
	   canonicalizer (das_sem_fromStr), then dispatch by canonical pointer -- so the
	   liberal "int"/"integer" spelling lives in exactly one place, not here. */
	if(sEnc == DASENC_UTF8){
		const char* sSem = das_sem_fromStr(sInterp);
		if(sSem == DASSEM_BOOL)
			return DASVT_BYTE;  /* signed range allows for a fill value */
		if(sSem == DASSEM_DATE)
			return DASVT_TIME;
		if(sSem == DASSEM_REAL){
			/* Can get hints from the length of the field, assume var-width items
			 * need the biggest available encoding */
			if((nItemBytes > 15)||(nItemBytes < 1))
				return DASVT_DOUBLE;
			else
				return DASVT_FLOAT;
		}
		if(sSem == DASSEM_INT){
			/* can get hints from the length of the field */
			if((nItemBytes < 1)||(nItemBytes > 11))
				return DASVT_LONG;  /* For var-length ints, return biggest storage for safety */
			if(nItemBytes > 5)
				return DASVT_INT;
			if(nItemBytes > 4)
				return DASVT_SHORT;
			return DASVT_BYTE;
		}
		if(sSem == DASSEM_TEXT){
			return DASVT_TEXT;
		}
		if(sSem == NULL){
			daserrf(DASERR_VALUE, "Unknown or unsupported semantic '%s'", sInterp);
			return DASVT_UNK;
		}
	}
	/* "blob" = native bytes, "base64" = the same bytes ASCII-encoded; both are
	   length-prefixed on the wire ({N}<field>) and store the same decoded bytes.
	   The encoding is orthogonal to the semantic: bytes bound for a string DasVar
	   store as DASVT_TEXT, everything else as an opaque byte sequence.  (So
	   encoding="blob" with semantic="string" aka `{12}I'm a string` , is legal,
	   though not preferred */
	if((sEnc == DASENC_RAW)||(sEnc == DASENC_B64)){
		if(das_sem_fromStr(sInterp) == DASSEM_TEXT)
			return DASVT_TEXT;
		return DASVT_BYTESEQ;
	}

	daserrf(DASERR_VALUE, "Unknown encoding type %s", (sEncType ? sEncType : "(null)"));
	return DASVT_UNK;
}

const char* dasvt_serializeAs(das_vt et)
{
#ifdef DASHOST_LSB_FIRST
	const bool LE = true;
#else
	const bool LE = false;
#endif

	switch(et){
	case DASVT_NULL:    return NULL;
	case DASVT_UNK: return NULL;
	case DASVT_INDEX:   return NULL;
	case DASVT_UBYTE:   return "ubyte";
	case DASVT_BYTE:    return "byte";
	case DASVT_USHORT:  return LE ?  "LEuint" : "BEuint";
	case DASVT_SHORT:   return LE ?  "LEint"  : "BEint";
	case DASVT_UINT:    return LE ?  "LEuint" : "BEuint";
	case DASVT_INT:     return LE ?  "LEint"  : "BEint";
	case DASVT_ULONG:   return LE ?  "LEuint" : "BEuint";
	case DASVT_LONG:    return LE ?  "LEint"  : "BEint";
	case DASVT_FLOAT:   return LE ?  "LEreal" : "BEreal";
	case DASVT_DOUBLE:  return LE ?  "LEreal" : "BEreal";
	case DASVT_TIME:    return "utf8";  /* encodes as old ASCII(X) */
	case DASVT_TEXT:    return "utf8";  /* encodes as old TIME(X)  */
	case DASVT_BYTESEQ: return "blob";  /* default framing; base64 armoring is a separate flag */
	default: return NULL;
	}
}

/* ************************************************************************* */
/* wire encodings */

/* These ARE the schema's EncodingType pattern (das-basic-stream-v3.0.xsd):
   byte|ubyte|BEint|BEuint|BEreal|LEint|LEuint|LEreal|utf8|blob|base64. */
const char* DASENC_BYTE   = "byte";
const char* DASENC_UBYTE  = "ubyte";
const char* DASENC_BEINT  = "BEint";
const char* DASENC_BEUINT = "BEuint";
const char* DASENC_BEREAL = "BEreal";
const char* DASENC_LEINT  = "LEint";
const char* DASENC_LEUINT = "LEuint";
const char* DASENC_LEREAL = "LEreal";
const char* DASENC_UTF8   = "utf8";
const char* DASENC_RAW   = "raw";  /* renamed from "blob" with the wire-model rework */
const char* DASENC_B64 = "base64";

/* Resolve an encoding string to its canonical DAS_ENC_* pointer, or NULL.  No liberal
   spellings here -- encoding names are exact. */
const char* das_enc_fromStr(const char* sEncType)
{
	if(sEncType == NULL) return NULL;
	if(strcmp(sEncType, DASENC_BYTE)   == 0) return DASENC_BYTE;
	if(strcmp(sEncType, DASENC_UBYTE)  == 0) return DASENC_UBYTE;
	if(strcmp(sEncType, DASENC_BEINT)  == 0) return DASENC_BEINT;
	if(strcmp(sEncType, DASENC_BEUINT) == 0) return DASENC_BEUINT;
	if(strcmp(sEncType, DASENC_BEREAL) == 0) return DASENC_BEREAL;
	if(strcmp(sEncType, DASENC_LEINT)  == 0) return DASENC_LEINT;
	if(strcmp(sEncType, DASENC_LEUINT) == 0) return DASENC_LEUINT;
	if(strcmp(sEncType, DASENC_LEREAL) == 0) return DASENC_LEREAL;
	if(strcmp(sEncType, DASENC_UTF8)   == 0) return DASENC_UTF8;
	if(strcmp(sEncType, DASENC_RAW)   == 0) return DASENC_RAW;
	if(strcmp(sEncType, "blob")         == 0) return DASENC_RAW;  /* legacy spelling */
	if(strcmp(sEncType, DASENC_B64) == 0) return DASENC_B64;
	return NULL;
}

/* ************************************************************************* */
/* semantics */

/* These ARE the schema's Semantic pattern (das-basic-stream-v3.0.xsd):
   bool|datetime|integer|real|string|blob.  "binary" was the pre-blob spelling. */
const char* DAS_SEM_BLOB  = "blob";
const char* DASSEM_BOOL  = "bool";
const char* DASSEM_DATE  = "datetime";
const char* DASSEM_INT   = "integer";
const char* DASSEM_REAL  = "real";
const char* DASSEM_TEXT  = "string";

/* A value type with no atomic semantic returns NULL */
const char* dassem_default(das_vt vt, DasUnits units)
{
	if(DasUnits_isTimePoint(units)) return DASSEM_DATE;

	switch(vt){
	case DASVT_FLOAT: case DASVT_DOUBLE:                           return DASSEM_REAL;
	case DASVT_UBYTE: case DASVT_BYTE:  case DASVT_USHORT: case DASVT_SHORT:
	case DASVT_UINT:  case DASVT_INT:   case DASVT_ULONG:  case DASVT_LONG: return DASSEM_INT;
	case DASVT_TIME:                                           return DASSEM_DATE;
	case DASVT_TEXT:                                           return DASSEM_TEXT;
	case DASVT_BYTESEQ:                                        return DAS_SEM_BLOB;
	default:                                               return NULL;  /* DASVT_INDEX, vtPixel, DASVT_UNK */
	}
}

/* Resolve a semantic string to its canonical DAS_SEM_* pointer, or NULL.  Liberal-in:
   "int" is accepted for "integer" (dasTelem quicklook streams emit both). */
const char* das_sem_fromStr(const char* sSemantic)
{
	if(sSemantic == NULL) return NULL;
	if((strcmp(sSemantic, DASSEM_INT) == 0)||(strcmp(sSemantic, "int") == 0))
		return DASSEM_INT;
	if(strcmp(sSemantic, DASSEM_REAL) == 0) return DASSEM_REAL;
	if(strcmp(sSemantic, DASSEM_DATE) == 0) return DASSEM_DATE;
	if(strcmp(sSemantic, DASSEM_BOOL) == 0) return DASSEM_BOOL;
	if(strcmp(sSemantic, DASSEM_TEXT) == 0) return DASSEM_TEXT;
	if(strcmp(sSemantic, DAS_SEM_BLOB) == 0) return DAS_SEM_BLOB;
	return NULL;
}

/* Given a semantic, suggest a default value type */
das_vt dasvt_default(const char* sSemantic)
{
	if(strcmp(sSemantic, DASSEM_BOOL) == 0) return DASVT_BYTE;
	if(strcmp(sSemantic, DASSEM_DATE) == 0) return DASVT_TIME;
	if(strcmp(sSemantic, DASSEM_INT)  == 0) return DASVT_INT;
	if(strcmp(sSemantic, DASSEM_REAL) == 0) return DASVT_DOUBLE;
	if(strcmp(sSemantic, DASSEM_TEXT) == 0) return DASVT_TEXT;
	return DASVT_BYTESEQ;  /* DAS_SEM_BLOB, and the fall-through for anything else */
}


/*
das_vt das_vt_guess_store(const char* sInterp, const char* sValue)
{
	int nLen = strlen(sValue);
	if((sValue == NULL)||(sValue[0] == '\0')){
		daserrf(DASERR_VALUE, "Null value");
		return DASVT_UNK;
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

		return uLen > 6 ? DASVT_DOUBLE, DASVT_FLOAT;
	}

	if(strcmp(sInterp, "int") == 0){
		// Just assume signed
		if(nLen < 2) return DASVT_BYTE;
		if(nLen < 5) return DASVT_SHORT;
		if(nLen < 9) return DASVT_INT;
		return DASVT_LONG;
	}

	if(strcmp(sInterp, "bool") == 0){
		return DASVT_BYTE;
	}
	if(strcmp(sInterp, "datetime") == 0){
		return DASVT_TIME;
	}
	if(strcmp(sInterp, "string") == 0){
		return DASVT_TEXT;
	}
	return DASVT_UNK;
}
*/

/* ************************************************************************* */
/* Default Comparison functions */

static int vt_cmp_byte(const uint8_t* vpFirst, const uint8_t* vpSecond){
	const uint8_t* pFirst = (const uint8_t*) vpFirst;
	const uint8_t* pSecond = (const uint8_t*) vpSecond;
	if(*pFirst < *pSecond) return -1;
	if(*pFirst > *pSecond) return 1;
	return 0;
}
static int vt_cmp_ushort(const uint8_t* vpFirst, const uint8_t* vpSecond){
	const uint16_t* pFirst = (const uint16_t*) vpFirst;
	const uint16_t* pSecond = (const uint16_t*) vpSecond;
	if(*pFirst < *pSecond) return -1;
	if(*pFirst > *pSecond) return 1;
	return 0;
}
static int vt_cmp_short(const uint8_t* vpFirst, const uint8_t* vpSecond){
	const int16_t* pFirst = (const int16_t*) vpFirst;
	const int16_t* pSecond = (const int16_t*) vpSecond;
	if(*pFirst < *pSecond) return -1;
	if(*pFirst > *pSecond) return 1;
	return 0;
}
static int vt_cmp_int(const uint8_t* vpFirst, const uint8_t* vpSecond){
	const int32_t* pFirst = (const int32_t*) vpFirst;
	const int32_t* pSecond = (const int32_t*) vpSecond;
	if(*pFirst < *pSecond) return -1;
	if(*pFirst > *pSecond) return 1;
	return 0;
}
static int vt_cmp_long(const uint8_t* vpFirst, const uint8_t* vpSecond){
	const int64_t* pFirst = (const int64_t*) vpFirst;
	const int64_t* pSecond = (const int64_t*) vpSecond;
	if(*pFirst < *pSecond) return -1;
	if(*pFirst > *pSecond) return 1;
	return 0;
}
static int vt_cmp_float(const uint8_t* vpFirst, const uint8_t* vpSecond){
	const float* pFirst = (const float*) vpFirst;
	const float* pSecond = (const float*) vpSecond;
	if(*pFirst < *pSecond) return -1;
	if(*pFirst > *pSecond) return 1;
	return 0;
}
static int vt_cmp_double(const uint8_t* vpFirst, const uint8_t* vpSecond){
	const double* pFirst = (const double*) vpFirst;
	const double* pSecond = (const double*) vpSecond;
	if(*pFirst < *pSecond) return -1;
	if(*pFirst > *pSecond) return 1;
	return 0;
}
static int vt_cmp_time(const uint8_t* vpFirst, const uint8_t* vpSecond){
	return DasTime_compare((const DasTime*)vpFirst, (const DasTime*)vpSecond);
}
static int vt_cmp_text(const uint8_t* vpFirst, const uint8_t* vpSecond){
	const char** pFirst = (const char**) vpFirst;
	const char** pSecond = (const char**) vpSecond;
	return strcmp(*pFirst, *pSecond);
}

static int vt_cmp_byteseq(const uint8_t* vpA, const uint8_t* vpB)
{
	const DasCByteSeq* pA = (const DasCByteSeq*)vpA;
	const DasCByteSeq* pB = (const DasCByteSeq*)vpB;
	
	size_t uLen = pA->size < pB->size ? pA->size : pB->size;  /*smallest size*/
	int nCmp = memcmp(vpA, vpB, uLen);
	if(nCmp != 0) return nCmp;
	
	/* If same length then truly zero, else the longer one is "bigger" 
	 * then the shorter one. */

	return ( (pA->size > pB->size) ? 1 : ((pA->size == pB->size) ? 0 : -1) );
}

/* Promote any plain numeric element to double; false for struct times */
bool daset_toDouble(das_et et, const uint8_t* p, double* pOut)
{
	switch(et){
	case DASET_UBYTE:  *pOut = *((const uint8_t*)p);  return true;
	case DASET_BYTE:   *pOut = *((const int8_t*)p);   return true;
	case DASET_USHORT: *pOut = *((const uint16_t*)p); return true;
	case DASET_SHORT:  *pOut = *((const int16_t*)p);  return true;
	case DASET_UINT:   *pOut = *((const uint32_t*)p); return true;
	case DASET_INT:    *pOut = *((const int32_t*)p);  return true;
	case DASET_ULONG:  *pOut = (double)*((const uint64_t*)p); return true;
	case DASET_LONG:   *pOut = (double)*((const int64_t*)p);  return true;
	case DASET_FLOAT:  *pOut = *((const float*)p);    return true;
	case DASET_DOUBLE: *pOut = *((const double*)p);   return true;
	default: return false;
	}
}

dasfn_valCmp dasvt_cmpFunc(das_vt et)
{
	switch(et){
	case DASVT_UBYTE: return vt_cmp_byte;
	case DASVT_SHORT: return vt_cmp_short;
	case DASVT_USHORT: return vt_cmp_ushort;
	case DASVT_INT: return vt_cmp_int;
	case DASVT_LONG: return vt_cmp_long;
	case DASVT_FLOAT: return vt_cmp_float;
	case DASVT_DOUBLE: return vt_cmp_double;
	case DASVT_TIME: return vt_cmp_time;
	case DASVT_TEXT: return vt_cmp_text;
	case DASVT_BYTESEQ: return vt_cmp_byteseq;
	default:	return NULL;
	}
}

/* There is a bit of a flaw in the design here in that how we store data must
 * agree somewhat with what units we use.  The broken down times especially
 * connotate units.
 */
/* The types whose results are kept as floats, rule 3 in value.h */
static bool _vt_isSmall(das_vt vt)
{
	return (vt == DASVT_UBYTE || vt == DASVT_BYTE || vt == DASVT_USHORT || vt == DASVT_SHORT
	        || vt == DASVT_FLOAT);
}

static bool _vt_isNum(das_vt vt)
{
	return dasvt_isInt(vt) || dasvt_isReal(vt);
}

/* Operands in written order, vtLeft op vtRight.  Until 2026-11 the header
   said (right, op, left), this file said (left, op, right) and every caller
   passed the right operand first, so the time rules ran backwards. */
das_vt dasvt_merge(das_vt vtLeft, int op, das_vt vtRight)
{
	/* DASVT_UNK is an application's type, so its answer is the app's too.
	   DASVT_NULL is "no value type", the answer when the operands don't combine. */
	if(vtLeft == DASVT_UNK || vtRight == DASVT_UNK) return DASVT_UNK;
	if(vtLeft == DASVT_NULL || vtRight == DASVT_NULL) return DASVT_NULL;
	if(vtLeft == DASVT_INDEX || vtRight == DASVT_INDEX) return DASVT_NULL;
	if(vtLeft == DASVT_BYTESEQ || vtRight == DASVT_BYTESEQ) return DASVT_NULL;
	if(vtLeft == DASVT_TEXT || vtRight == DASVT_TEXT) return DASVT_NULL;

	if(vtLeft != DASVT_TIME && vtRight != DASVT_TIME)
		return (_vt_isSmall(vtLeft) && _vt_isSmall(vtRight)) ? DASVT_FLOAT : DASVT_DOUBLE;

	/* A time is a calendar point: points subtract to a span, a span moves a
	   point either way, and a number can only be added to one */
	if(vtLeft == DASVT_TIME && vtRight == DASVT_TIME)
		return (op == DASOP_SUB) ? DASVT_DOUBLE : DASVT_NULL;

	if(vtLeft == DASVT_TIME && _vt_isNum(vtRight) &&
	   (op == DASOP_ADD || op == DASOP_SUB)) return DASVT_TIME;

	if(_vt_isNum(vtLeft) && vtRight == DASVT_TIME && op == DASOP_ADD) return DASVT_TIME;

	return DASVT_NULL;
}

#define HAS_INT  0x1
#define HAS_FLT  0x2	
#define HAS_BOTH 0x3

int dasval_cmpAny(
	const uint8_t* pA, das_vt vtA, const uint8_t* pB, das_vt vtB
){
	int nCmp = 0;
	double dA = 0.0, dB = 0.0;
			
	/* Handle most common compare first and get out */
	if(vtA == DASVT_DOUBLE && vtB == DASVT_DOUBLE){
		dA = *((const double*)pA);
		dB = *((const double*)pB);
		return ((dA > dB) ? 1 : ((dA == dB) ? 0 : -1));
	}
	
	/* Unknowns have no way to get length, nulls have no value, they never compare */
	if(vtA == DASVT_UNK || vtB == DASVT_UNK) return -2;
	if(vtA == DASVT_NULL || vtB == DASVT_NULL) return -2;
	
	/* DASVT_INDEX items only make since in the context of their parent 
	   structures, so no dice here either */
	if(vtA == DASVT_INDEX || vtB == DASVT_INDEX) return -2;
	
	if(vtA == vtB){
		if(vtA == DASVT_TEXT){  
			nCmp = strcmp((const char*)pA, (const char*)pB);
		}
		else{
			if(vtA == DASVT_TIME){
				nCmp = DasTime_compare((const DasTime*)pA, (const DasTime*)pB);
			}
			else{
				if(vtA == DASVT_BYTESEQ)
					nCmp = vt_cmp_byteseq(pA, pB);
				else
					nCmp = memcmp(pA, pB, dasvt_size(vtA));
			}
		}
		
		return (nCmp > 0 ? 1 : nCmp == 0 ? 0 : -1);
	}
	
	/* Unequal type comparisons ... */
	
	/* DASVT_TIME can't compare with anything else because there are no units */
	if(vtA == DASVT_TIME || vtB == DASVT_TIME) return -2;
	
	/* If one is a byte sequence and the other is not, compare as byteseq */
	DasCByteSeq bs;
	if(vtA == DASVT_BYTESEQ || vtB == DASVT_BYTESEQ){
		if(vtA == DASVT_BYTESEQ){
			bs.data = pB;
			bs.size = dasvt_size(vtB);
			return vt_cmp_byteseq(pA, (const uint8_t*) &bs);
		}
		else{
			bs.data = pA;
			bs.size = dasvt_size(vtA);
			return vt_cmp_byteseq((const uint8_t*)&bs, pB);		
		}
	}
	
	/* Generic numeric comparisons below... These need a little work.  */
	/* I'm sure there are many patterns that could be exploited to make */
	/* the code shorter.  In general there are 8x8 types, for 64       */
	/* different code paths.  But many of them collapse                */
	
	/* In D we'd use the 80-bit numeric types to make this easier, have */
	/* to do it manually for VC++                                       */

	int64_t lA = 0, lB = 0;
	uint8_t uAHas = 0;  /* float = 0x2 */
	uint8_t uBHas = 0;  /* int =   0x1 */
	switch(vtA){
	case DASVT_UBYTE:   lA = *((const uint8_t*)pA);  dA = *((const uint8_t*)pA);  uAHas = HAS_BOTH; break;
	case DASVT_SHORT:  lA = *((const int16_t*)pA);  dA = *((const int16_t*)pA);  uAHas = HAS_BOTH; break;
	case DASVT_USHORT: lA = *((const uint16_t*)pA); dA = *((const uint16_t*)pA); uAHas = HAS_BOTH; break;
	case DASVT_INT:    lA = *((const int32_t*)pA);  dA = *((const int32_t*)pA);  uAHas = HAS_BOTH; break;
	
	case DASVT_LONG:   lA = *((const int64_t*)pA);  /* No float value */         uAHas = HAS_INT;  break;
	
	case DASVT_FLOAT:  /* No int value */           dA = *((const float*)pA);    uAHas = HAS_FLT;  break;
	case DASVT_DOUBLE: /* No int value */           dA = *((const double*)pA);   uAHas = HAS_FLT;  break;
	default: return -2;
	}
	
	switch(vtB){
	case DASVT_UBYTE:   lB = *((const uint8_t*)pB);  dB = *((const uint8_t*)pB);  uBHas = HAS_BOTH; break;
	case DASVT_SHORT:  lB = *((const int16_t*)pB);  dB = *((const int16_t*)pB);  uBHas = HAS_BOTH; break;
	case DASVT_USHORT: lB = *((const uint16_t*)pB); dB = *((const uint16_t*)pB); uBHas = HAS_BOTH; break;
	case DASVT_INT:    lB = *((const int32_t*)pB);  dB = *((const int32_t*)pB);  uBHas = HAS_BOTH; break;
	
	case DASVT_LONG:   lB = *((const int64_t*)pB);  /* No float value */         uBHas = HAS_INT;  break;
	
	case DASVT_FLOAT:  /* No int value */           dB = *((const float*)pB);    uBHas = HAS_FLT;  break;
	case DASVT_DOUBLE: /* No int value */           dB = *((const double*)pB);   uBHas = HAS_FLT;  break;
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
/* Convert any one integral value type into any other, with range and 
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

/* The largest magnitude at which every integer is still exactly representable
   in a real, one per real type: 2^24 for float's 24-bit significand, 2^53 for
   double's 53.  Named so the bound and the destination type cannot drift
   apart across the GO_RES / GO_ZRES rows below. */
#define FLT_EXACT_INT 16777216
#define DBL_EXACT_INT 9007199254740992LL

/* Upper bounds for the floating point -> 64-bit integer range checks.
   UINT64_MAX and INT64_MAX have no double representation */
#define DBL_MAX_UINT64 0x1.fffffffffffffp63
#define DBL_MAX_INT64  0x1.fffffffffffffp62

/* Go to the other size if max value doesn't incur resolution loss */
#define GO_ZRES(TY_OUT, TY_IN, MAX_OK) \
  if(bRes && (*((TY_IN*)pI) > MAX_OK )){ goto ERR_RESLOSS;} *((TY_OUT*)pO) = *((TY_IN*)pI)

/* Go to the other size if min or max value don't incur resolution loss */
#define GO_RES(TY_OUT, TY_IN, MIN_OK, MAX_OK) \
	if(bRes &&( (*((TY_IN*)pI) < MIN_OK) || (*((TY_IN*)pI) > MAX_OK ) ) ){ goto ERR_RESLOSS;} *((TY_OUT*)pO) = *((TY_IN*)pI)	  

/* Got to an integer if I'm in range, and lose too much resolution,

   Min and max are straightforward, but resolution loss is hard to quantify.
   If the fractional part is far enough away from an integer, call it a conversion error.  Thus:

     1.9998 would convert to 2 without error but
     1.5    would not.

   Argument order is (TY_OUT, TY_IN), the same as every other GO_* macro
   above.  That order is load bearing rather than cosmetic: reversed, this
   reads the input's first byte as the output type and writes the input's
   full width into the output slot, overrunning any destination narrower
   than a double.
*/
#define GO_TRUNC(TY_OUT, TY_IN, MIN_OK, MAX_OK, EPSILON) \
	rIn = *((TY_IN*)pI); \
	if(bRng && ((rIn < MIN_OK)||(rIn > MAX_OK))) goto ERR_RANGE; \
	if(bRes){ rRnd = round(rIn); if( fabs(rIn - rRnd) > EPSILON) goto ERR_RESLOSS; } \
	*((TY_OUT*)pO) = rIn


/* Render one value for a diagnostic.  The value has to be dereferenced as its
   own type before it can be printed, and a format string built at runtime
   cannot carry that, so each type states its own literal format. */
static void _xform_prnVal(
	das_vt vt, const uint8_t* p, char* sBuf, size_t uLen
){
	switch(vt){
	case DASVT_UBYTE : snprintf(sBuf, uLen, "%u",  *((const uint8_t*)p));  break;
	case DASVT_BYTE  : snprintf(sBuf, uLen, "%d",  *((const int8_t*)p));   break;
	case DASVT_USHORT: snprintf(sBuf, uLen, "%u",  *((const uint16_t*)p)); break;
	case DASVT_SHORT : snprintf(sBuf, uLen, "%d",  *((const int16_t*)p));  break;
	case DASVT_UINT  : snprintf(sBuf, uLen, "%" PRIu32, *((const uint32_t*)p)); break;
	case DASVT_INT   : snprintf(sBuf, uLen, "%" PRId32, *((const int32_t*)p));  break;
	case DASVT_ULONG : snprintf(sBuf, uLen, "%" PRIu64, *((const uint64_t*)p)); break;
	case DASVT_LONG  : snprintf(sBuf, uLen, "%" PRId64, *((const int64_t*)p));  break;
	case DASVT_FLOAT : snprintf(sBuf, uLen, "%.7g", (double)*((const float*)p)); break;
	case DASVT_DOUBLE: snprintf(sBuf, uLen, "%.16g", *((const double*)p));  break;
	default      : snprintf(sBuf, uLen, "(a %s)", dasvt_name(vt));    break;
	}
}

das_errcode dasval_convert(
   das_vt vtIn,  const uint8_t* pI, const uint8_t* pFI,
   das_vt vtOut,       uint8_t* pO, const uint8_t* pFO,
   uint32_t uFlags
){
	/* Handle fill up-front */
	size_t uInSz  = dasvt_size(vtIn);
	size_t uOutSz = dasvt_size(vtOut);
	if((pFI != NULL) && (memcmp(pI, pFI, uInSz) == 0)){
		memcpy(pO, pFO, uOutSz);
		return DAS_OKAY;
	}

	/* The two conversions that cannot fail, taken before the table.  Same type
	   is a copy, and every float is exactly a double, so neither can trip a
	   range or resolution check -- these are the table's own GO_BIG rows with
	   the dispatch removed.  Both are hot: float storage dominates real
	   streams, a 16 bit A/D calibrated into 32 bit reals losing nothing. */
	if((vtIn == vtOut) && (vtIn >= DASVT_MIN_SIMPLE) && (vtIn <= DASVT_DOUBLE)){
		memcpy(pO, pI, uInSz);
		return DAS_OKAY;
	}
	if((vtIn == DASVT_FLOAT) && (vtOut == DASVT_DOUBLE)){
		*((double*)pO) = *((const float*)pI);
		return DAS_OKAY;
	}

	bool bRng = (uFlags & DASVAL_NOERR_RNG) ? false : true;  /* on true, issue range errors */
	bool bRes = (uFlags & DASVAL_ERR_RESLOSS);
	double rIn, rRnd; /* Used by the trunc macros */

	/* Value conversions */
	switch(vtIn){
	case DASVT_UBYTE :
		switch(vtOut){
		case DASVT_UBYTE : GO_BIG( uint8_t, uint8_t          ); break; /* works for same */
		case DASVT_BYTE  : GO_MAX(  int8_t, uint8_t, INT8_MAX); break;
		case DASVT_USHORT: GO_BIG(uint16_t, uint8_t          ); break;
		case DASVT_SHORT : GO_BIG( int16_t, uint8_t          ); break;
		case DASVT_UINT  : GO_BIG(uint32_t, uint8_t          ); break;
		case DASVT_INT   : GO_BIG( int32_t, uint8_t          ); break;
		case DASVT_ULONG : GO_BIG(uint64_t, uint8_t          ); break;
		case DASVT_LONG  : GO_BIG( int64_t, uint8_t          ); break;
		case DASVT_FLOAT : GO_BIG(   float, uint8_t          ); break;
		case DASVT_DOUBLE: GO_BIG(  double, uint8_t          ); break;
		default: goto ERR_NO_XFORM; break;
		}
		break;
	case DASVT_BYTE  :
		switch(vtOut){
		case DASVT_UBYTE : GO_POSI( uint8_t,  int8_t);  break;
		case DASVT_BYTE  : GO_BIG (  int8_t,  int8_t);  break; /* works for same */
		case DASVT_USHORT: GO_POSI(uint16_t,  int8_t);  break;
		case DASVT_SHORT : GO_BIG ( int16_t,  int8_t);  break;
		case DASVT_UINT  : GO_POSI(uint32_t,  int8_t);  break;
		case DASVT_INT   : GO_BIG ( int32_t,  int8_t);  break;
		case DASVT_ULONG : GO_POSI(uint64_t,  int8_t);  break;
		case DASVT_LONG  : GO_BIG ( int64_t,  int8_t);  break;
		case DASVT_FLOAT : GO_BIG (   float,  int8_t);  break;
		case DASVT_DOUBLE: GO_BIG (  double,  int8_t);  break;
		default: goto ERR_NO_XFORM; break;
		}
		break;
	case DASVT_USHORT:
		switch(vtOut){
		case DASVT_UBYTE : GO_MAX( uint8_t, uint16_t,  UINT8_MAX); break; 
		case DASVT_BYTE  : GO_MAX(  int8_t, uint16_t,   INT8_MAX); break;
		case DASVT_USHORT: GO_BIG(uint16_t, uint16_t);            break; /* works for same */
		case DASVT_SHORT : GO_MAX( int16_t, uint16_t,  INT16_MAX); break;
		case DASVT_UINT  : GO_BIG(uint32_t, uint16_t            ); break;
		case DASVT_INT   : GO_BIG( int32_t, uint16_t            ); break;
		case DASVT_ULONG : GO_BIG(uint64_t, uint16_t            ); break;
		case DASVT_LONG  : GO_BIG( int64_t, uint16_t            ); break;
		case DASVT_FLOAT : GO_BIG(   float, uint16_t            ); break;
		case DASVT_DOUBLE: GO_BIG(  double, uint16_t            ); break;
		default: goto ERR_NO_XFORM; break;
		}
		break;
	case DASVT_SHORT :
		switch(vtOut){
		case DASVT_UBYTE : GO_ZMAX( uint8_t,  int16_t, UINT8_MAX); break;
		case DASVT_BYTE  : GO_RNG(   int8_t,  int16_t,  INT8_MIN, INT8_MAX); break;
		case DASVT_USHORT: GO_POSI(uint16_t,  int16_t);  break;
		case DASVT_SHORT : GO_BIG ( int16_t,  int16_t);  break; /* works for same */
		case DASVT_UINT  : GO_POSI(uint32_t,  int16_t);  break;
		case DASVT_INT   : GO_BIG ( int32_t,  int16_t);  break;
		case DASVT_ULONG : GO_POSI(uint64_t,  int16_t);  break; 
		case DASVT_LONG  : GO_BIG ( int64_t,  int16_t);  break;
		case DASVT_FLOAT : GO_BIG (   float,  int16_t);  break;
		case DASVT_DOUBLE: GO_BIG (  double,  int16_t);  break;
		default: goto ERR_NO_XFORM; break;
		}
		break;
	case DASVT_UINT  :
		switch(vtOut){
		case DASVT_UBYTE : GO_MAX ( uint8_t, uint32_t,  UINT8_MAX); break;
		case DASVT_BYTE  : GO_MAX (  int8_t, uint32_t,   INT8_MAX); break;
		case DASVT_USHORT: GO_MAX (uint16_t, uint32_t, UINT16_MAX); break;
		case DASVT_SHORT : GO_MAX ( int16_t, uint32_t,  INT16_MAX); break;
		case DASVT_UINT  : GO_BIG (uint32_t, uint32_t);             break; /* works for same */
		case DASVT_INT   : GO_MAX ( int32_t, uint32_t,  INT32_MAX); break;
		case DASVT_ULONG : GO_BIG (uint64_t, uint32_t);             break;
		case DASVT_LONG  : GO_BIG ( int64_t, uint32_t);             break;
		case DASVT_FLOAT : GO_ZRES(   float, uint32_t, FLT_EXACT_INT); break;
		case DASVT_DOUBLE: GO_BIG (  double, uint32_t);             break;
		default: goto ERR_NO_XFORM; break;
		}
		break;
	case DASVT_INT   :
		switch(vtOut){
		case DASVT_UBYTE : GO_ZMAX( uint8_t,  int32_t,  UINT8_MAX);            break;
		case DASVT_BYTE  : GO_RNG(   int8_t,  int32_t,   INT8_MIN,  INT8_MAX); break;
		case DASVT_USHORT: GO_ZMAX(uint16_t,  int32_t, UINT16_MAX);            break;
		case DASVT_SHORT : GO_RNG(  int16_t,  int32_t,  INT16_MIN, INT16_MAX); break;
		case DASVT_UINT  : GO_POSI(uint32_t,  int32_t);                        break;
		case DASVT_INT   : GO_BIG ( int32_t,  int32_t);                        break; /* works for same */
		case DASVT_ULONG : GO_POSI(uint64_t,  int32_t);                        break;
		case DASVT_LONG  : GO_BIG ( int64_t,  int32_t);                        break;
		case DASVT_FLOAT : GO_RES (   float,  int32_t, -FLT_EXACT_INT, FLT_EXACT_INT); break;
		case DASVT_DOUBLE: GO_BIG (  double,  int32_t);                        break;
		default: goto ERR_NO_XFORM; break;
		}
		break;
	case DASVT_ULONG :
		switch(vtOut){
		case DASVT_UBYTE : GO_MAX( uint8_t, uint64_t,  UINT8_MAX); break;
		case DASVT_BYTE  : GO_MAX(  int8_t, uint64_t,   INT8_MAX); break;
		case DASVT_USHORT: GO_MAX(uint16_t, uint64_t, UINT16_MAX); break;
		case DASVT_SHORT : GO_MAX( int16_t, uint64_t,  INT16_MAX); break;
		case DASVT_UINT  : GO_MAX(uint32_t, uint64_t, UINT32_MAX); break;
		case DASVT_INT   : GO_MAX( int32_t, uint64_t,  INT32_MAX); break;
		case DASVT_ULONG : GO_BIG(uint64_t, uint64_t);             break; /* works for same */
		case DASVT_LONG  : GO_MAX( int64_t, uint64_t,  INT64_MAX); break;
		case DASVT_FLOAT : GO_ZRES(  float, uint64_t, FLT_EXACT_INT); break;
		case DASVT_DOUBLE: GO_ZRES( double, uint64_t, DBL_EXACT_INT); break;
		default: goto ERR_NO_XFORM; break;
		}
		break;
	case DASVT_LONG  :
		switch(vtOut){
		case DASVT_UBYTE : GO_ZMAX( uint8_t,  int64_t,  UINT8_MAX);            break;
		case DASVT_BYTE  : GO_RNG(   int8_t,  int64_t,   INT8_MIN,  INT8_MAX); break;
		case DASVT_USHORT: GO_ZMAX(uint16_t,  int64_t, UINT16_MAX);            break;
		case DASVT_SHORT : GO_RNG(  int16_t,  int64_t,  INT16_MIN, INT16_MAX); break;
		case DASVT_UINT  : GO_ZMAX(uint32_t,  int64_t, UINT32_MAX);            break;
		case DASVT_INT   : GO_RNG(  int32_t,  int64_t,  INT32_MIN, INT32_MAX); break;
		case DASVT_ULONG : GO_POSI(uint64_t,  int64_t);                        break;
		case DASVT_LONG  : GO_BIG ( int64_t,  int64_t);                        break; /* works for same */
		case DASVT_FLOAT : GO_RES (   float,  int64_t, -FLT_EXACT_INT, FLT_EXACT_INT); break;
		case DASVT_DOUBLE: GO_RES (  double,  int64_t, -DBL_EXACT_INT, DBL_EXACT_INT); break;
		default: goto ERR_NO_XFORM; break;
		}
		break;
	case DASVT_FLOAT :
		switch(vtOut){
		case DASVT_UBYTE : GO_TRUNC( uint8_t, float,         0,  UINT8_MAX, 0.02); break;
		case DASVT_BYTE  : GO_TRUNC(  int8_t, float,  INT8_MIN,   INT8_MAX, 0.02); break;
		case DASVT_USHORT: GO_TRUNC(uint16_t, float,         0, UINT16_MAX, 0.02); break;
		case DASVT_SHORT : GO_TRUNC( int16_t, float, INT16_MIN,  INT16_MAX, 0.02); break;
		case DASVT_UINT  : GO_TRUNC(uint32_t, float,         0, UINT32_MAX, 0.02); break;
		case DASVT_INT   : GO_TRUNC( int32_t, float, INT32_MIN,  INT32_MAX, 0.02); break;
		case DASVT_ULONG : GO_TRUNC(uint64_t, float,         0, DBL_MAX_UINT64, 0.02); break;
		case DASVT_LONG  : GO_TRUNC( int64_t, float, INT64_MIN,  DBL_MAX_INT64, 0.02); break;
		case DASVT_FLOAT : GO_BIG(     float, float);  break; /* works for same */
		case DASVT_DOUBLE: GO_BIG(    double, float);  break;
		default: goto ERR_NO_XFORM; break;
		}
		break;
	case DASVT_DOUBLE:
		switch(vtOut){
		case DASVT_UBYTE : GO_TRUNC( uint8_t, double,         0,  UINT8_MAX, 0.02); break;
		case DASVT_BYTE  : GO_TRUNC(  int8_t, double,  INT8_MIN,   INT8_MAX, 0.02); break;
		case DASVT_USHORT: GO_TRUNC(uint16_t, double,         0, UINT16_MAX, 0.02); break;
		case DASVT_SHORT : GO_TRUNC( int16_t, double, INT16_MIN,  INT16_MAX, 0.02); break;
		case DASVT_UINT  : GO_TRUNC(uint32_t, double,         0, UINT32_MAX, 0.02); break;
		case DASVT_INT   : GO_TRUNC( int32_t, double, INT32_MIN,  INT32_MAX, 0.02); break;
		case DASVT_ULONG : GO_TRUNC(uint64_t, double,         0, DBL_MAX_UINT64, 0.02); break;
		case DASVT_LONG  : GO_TRUNC( int64_t, double, INT64_MIN,  DBL_MAX_INT64, 0.02); break;
		case DASVT_FLOAT : GO_RNG(     float, double, (-1*FLT_MAX), FLT_MAX); break;
		case DASVT_DOUBLE: GO_BIG(    double, double);                        break; /* works for same */
		default: goto ERR_NO_XFORM; break;
		}
		break;

	default: goto ERR_NO_XFORM; break;
	}

	return DAS_OKAY;

	char sVal[40] = {'\0'};

ERR_RESLOSS:
	_xform_prnVal(vtIn, pI, sVal, sizeof(sVal));
	return daserrf(DASERR_VALUE, "Resolution loss converting %s (%s) to %s",
		sVal, dasvt_name(vtIn), dasvt_name(vtOut)
	);

ERR_RANGE:
	_xform_prnVal(vtIn, pI, sVal, sizeof(sVal));
	return daserrf(DASERR_VALUE, "Range violation converting %s (%s) to %s",
		sVal, dasvt_name(vtIn), dasvt_name(vtOut)
	);

ERR_NO_XFORM:
	return daserrf(DASERR_VALUE, "No conversion from %s to %s defined",
		dasvt_name(vtIn), dasvt_name(vtOut)
	);
}

/* ************************************************************************** */
/* One binary operation on one pair of values (dasval_binOp)
 *
 * The shared elementwise kernel.  Promotion is to the narrowest domain that
 * holds *both* operands exactly, not always to double: TT2000 nanoseconds are
 * int64 and sit above 2^53, where a trip through double drops ticks.  Three
 * domains, tried in this order:
 *
 *   int64   both operands integral and representable as an int64
 *   uint64  both integral and non-negative, but one is a DASVT_ULONG too large to
 *           be an int64.  That is the only way an integer escapes int64, so
 *           this domain exists solely for the top half of DASVT_ULONG
 *   double  either operand is real, or the operator has no exact integer
 *           answer (divide, power)
 *
 * The result leaves its domain through dasval_convert(), so the caller's
 * declared vtOut is range checked by the same code as every other cast rather
 * than by a second opinion written here.
 *
 * Every check below costs a few comparisons per value.  That is deliberate:
 * a silently wrapped result is the class of defect that has destroyed launch
 * vehicles, and this kernel runs under every formalism in the library.
 */

/* Load into the int64 domain.  A false return is a *question* answered, not an
   error: it means "does not fit here", and the caller retries in uint64. */
static bool _binop_asI64(das_vt vt, const uint8_t* p, int64_t* pOut)
{
	switch(vt){
	case DASVT_UBYTE : *pOut = *((const uint8_t*)p);  return true;
	case DASVT_BYTE  : *pOut = *((const int8_t*)p);   return true;
	case DASVT_USHORT: *pOut = *((const uint16_t*)p); return true;
	case DASVT_SHORT : *pOut = *((const int16_t*)p);  return true;
	case DASVT_UINT  : *pOut = *((const uint32_t*)p); return true;
	case DASVT_INT   : *pOut = *((const int32_t*)p);  return true;
	case DASVT_LONG  : *pOut = *((const int64_t*)p);  return true;
	case DASVT_ULONG : {
		uint64_t u = *((const uint64_t*)p);
		if(u > (uint64_t)INT64_MAX) return false;
		*pOut = (int64_t)u;
		return true;
	}
	default: return false;
	}
}

/* Load into the uint64 domain.  Same convention: false means the value has no
   place here, which for a negative operand it does not. */
static bool _binop_asU64(das_vt vt, const uint8_t* p, uint64_t* pOut)
{
	int64_t n;

	if(vt == DASVT_ULONG){ *pOut = *((const uint64_t*)p); return true; }

	if(!_binop_asI64(vt, p, &n)) return false;
	if(n < 0) return false;
	*pOut = (uint64_t)n;
	return true;
}

static bool _binop_asDbl(das_vt vt, const uint8_t* p, double* pOut)
{
	int64_t n;

	switch(vt){
	case DASVT_FLOAT : *pOut = *((const float*)p);            return true;
	case DASVT_DOUBLE: *pOut = *((const double*)p);           return true;
	case DASVT_ULONG : *pOut = (double)*((const uint64_t*)p); return true;
	default:
		if(!_binop_asI64(vt, p, &n)) return false;
		*pOut = (double)n;
		return true;
	}
}

das_errcode dasval_binOp(
	int nOp, das_vt vtL, const uint8_t* pL, das_vt vtR,
	const uint8_t* pR, das_vt vtOut, uint8_t* pOut
){
	int64_t  nLeft,  nRight,  nRes;
	uint64_t uLeft,  uRight,  uRes;
	double   rLeft,  rRight,  rRes;

	if((pL == NULL)||(pR == NULL)||(pOut == NULL))
		return daserrf(DASERR_VALUE, "Null pointer in dasval_binOp");

	/* dasop_isBinary() only tests the code *range*, so pair it with a name
	   lookup.  An unnamed code in range would otherwise reach the error
	   messages below, which print the operator. */
	if(!dasop_isBinary(nOp) || (dasop_name(nOp, NULL) == NULL))
		return daserrf(DASERR_VALUE,
			"Operator code %d is not a known binary operator", nOp
		);

	if((vtL == DASVT_TIME)||(vtR == DASVT_TIME))
		return daserrf(DASERR_VALUE,
			"dasval_binOp() refuses DASVT_TIME.  Broken down calendar math mixes "
			"two value types and belongs to whichever formalism understands the "
			"calendar"
		);

	/* Exact means: both sides are integers *and* the operator has an integer
	   answer.  Divide and power do not, for any interesting pair, so they
	   leave for the reals here rather than truncating in the middle. */
	bool bExact = dasvt_isInt(vtL) && dasvt_isInt(vtR) &&
	              (nOp != DASOP_DIV) && (nOp != DASOP_POW);

	if(bExact && _binop_asI64(vtL, pL, &nLeft) && _binop_asI64(vtR, pR, &nRight)){

		switch(nOp){
		case DASOP_ADD:
			if(nRight > 0){ if(nLeft > INT64_MAX - nRight) goto ERR_OVR; }
			else          { if(nLeft < INT64_MIN - nRight) goto ERR_OVR; }
			nRes = nLeft + nRight;
			break;

		case DASOP_SUB:
			if(nRight < 0){ if(nLeft > INT64_MAX + nRight) goto ERR_OVR; }
			else          { if(nLeft < INT64_MIN + nRight) goto ERR_OVR; }
			nRes = nLeft - nRight;
			break;

		/* The portable CERT INT32-C division forms, written so the trapping
		   expression INT64_MIN / -1 is never evaluated on any branch. */
		case DASOP_MUL:
			if(nLeft > 0){
				if(nRight > 0){ if(nLeft  > INT64_MAX / nRight) goto ERR_OVR; }
				else          { if(nRight < INT64_MIN / nLeft ) goto ERR_OVR; }
			}
			else{
				if(nRight > 0){ if(nLeft < INT64_MIN / nRight) goto ERR_OVR; }
				else if(nLeft != 0){ if(nRight < INT64_MAX / nLeft) goto ERR_OVR; }
			}
			nRes = nLeft * nRight;
			break;

		default: goto ERR_OP;
		}

		return dasval_convert(
			DASVT_LONG, (const uint8_t*)&nRes, NULL, vtOut, pOut, NULL, 0
		);
	}

	if(bExact && _binop_asU64(vtL, pL, &uLeft) && _binop_asU64(vtR, pR, &uRight)){

		switch(nOp){
		case DASOP_ADD:
			if(uLeft > UINT64_MAX - uRight) goto ERR_OVR;
			uRes = uLeft + uRight;
			break;

		/* A difference that runs below zero has no home: the pair only landed
		   in this domain because one value is too big to be an int64. */
		case DASOP_SUB:
			if(uLeft < uRight) goto ERR_OVR;
			uRes = uLeft - uRight;
			break;

		case DASOP_MUL:
			if((uLeft != 0) && (uRight > UINT64_MAX / uLeft)) goto ERR_OVR;
			uRes = uLeft * uRight;
			break;

		default: goto ERR_OP;
		}

		return dasval_convert(
			DASVT_ULONG, (const uint8_t*)&uRes, NULL, vtOut, pOut, NULL, 0
		);
	}

	/* An integer pair that reached here fits neither integer domain, which
	   takes a DASVT_ULONG above 2^63 against a negative operand.  Falling into
	   the reals would answer to 53 bits and say nothing about it. */
	if(bExact)
		return daserrf(DASERR_VALUE,
			"No integer domain holds both a %s and a %s operand exactly",
			dasvt_name(vtL), dasvt_name(vtR)
		);

	if(!_binop_asDbl(vtL, pL, &rLeft) || !_binop_asDbl(vtR, pR, &rRight))
		return daserrf(DASERR_VALUE,
			"dasval_binOp() handles simple numeric types, not %s %s %s",
			dasvt_name(vtL), dasop_name(nOp, NULL), dasvt_name(vtR)
		);

	switch(nOp){
	case DASOP_ADD: rRes = rLeft + rRight;    break;
	case DASOP_SUB: rRes = rLeft - rRight;    break;
	case DASOP_MUL: rRes = rLeft * rRight;    break;
	case DASOP_DIV: rRes = rLeft / rRight;    break;
	case DASOP_POW: rRes = pow(rLeft, rRight); break;
	default: goto ERR_OP;
	}

	/* An operand that arrived non-finite is the caller's business, fill
	   sentinels being one way that happens.  A finite pair that produces
	   inf or nan is this layer's overflow, divide by zero included. */
	if(!isfinite(rRes) && isfinite(rLeft) && isfinite(rRight)) goto ERR_OVR;

	return dasval_convert(
		DASVT_DOUBLE, (const uint8_t*)&rRes, NULL, vtOut, pOut, NULL, 0
	);

ERR_OVR:
	return daserrf(DASERR_VALUE,
		"Overflow in a %s %s %s operation", dasvt_name(vtL),
		dasop_name(nOp, NULL), dasvt_name(vtR)
	);

ERR_OP:
	return daserrf(DASERR_VALUE,
		"Binary operator %d is undefined in dasval_binOp", nOp
	);
}

/* ************************************************************************** */
/* Typed multiply-accumulate (dasval_accum)
 *
 * Both helpers carry the running value in the widest type of their signedness
 * and range-check against the target type's [min,max] supplied by the caller.
 * The integer-overflow tests are the portable CERT INT32-C division forms --
 * standard C99, no compiler builtins -- so they compile the same on gcc, clang,
 * emscripten and MSVC.  They are written to never evaluate the trapping form
 * INT64_MIN / -1. */

/* *pA += step*count in int64 space; false (no change) on any overflow or if the
   result falls outside [nMin, nMax]. */
static bool _das_accum_i64(
	int64_t* pA, int64_t step, int64_t count, int64_t nMin, int64_t nMax
){
	int64_t prod;
	if((step == 0)||(count == 0)){
		prod = 0;
	}
	else if(step > 0){
		if(count > 0){ if(step > INT64_MAX / count) return false; }   /* + * + */
		else         { if(count < INT64_MIN / step) return false; }   /* + * - */
		prod = step * count;
	}
	else{ /* step < 0 */
		if(count > 0){ if(step < INT64_MIN / count) return false; }   /* - * + */
		else         { if(count < INT64_MAX / step) return false; }   /* - * - */
		prod = step * count;
	}

	int64_t a = *pA;
	if(prod > 0){ if(a > INT64_MAX - prod) return false; }
	else        { if(a < INT64_MIN - prod) return false; }
	int64_t sum = a + prod;

	if((sum < nMin)||(sum > nMax)) return false;
	*pA = sum;
	return true;
}

/* *pA += step*count in uint64 space; false (no change) on any overflow or if the
   result exceeds uMax.  Unsigned arithmetic wraps by definition, so the checks
   are the simple comparison forms. */
static bool _das_accum_u64(
	uint64_t* pA, uint64_t step, uint64_t count, uint64_t uMax
){
	uint64_t prod;
	if((step == 0)||(count == 0)){
		prod = 0;
	}
	else{
		if(step > UINT64_MAX / count) return false;
		prod = step * count;
	}

	uint64_t a = *pA;
	if(a > UINT64_MAX - prod) return false;
	uint64_t sum = a + prod;

	if(sum > uMax) return false;
	*pA = sum;
	return true;
}

#define ACCUM_I(CTYPE, MINV, MAXV) \
	do{ \
		int64_t a = *((CTYPE*)pAccum); \
		if(!_das_accum_i64(&a, (int64_t)(*((CTYPE*)pStep)), (int64_t)nCount, MINV, MAXV)) \
			goto ERR_OVR; \
		*((CTYPE*)pAccum) = (CTYPE)a; \
	}while(0)

#define ACCUM_U(CTYPE, MAXV) \
	do{ \
		if(nCount < 0) goto ERR_NEG; \
		uint64_t a = *((CTYPE*)pAccum); \
		if(!_das_accum_u64(&a, (uint64_t)(*((CTYPE*)pStep)), (uint64_t)nCount, MAXV)) \
			goto ERR_OVR; \
		*((CTYPE*)pAccum) = (CTYPE)a; \
	}while(0)

das_errcode dasval_accum(
	das_vt vt, uint8_t* pAccum, const uint8_t* pStep, ptrdiff_t nCount
){
	if((pAccum == NULL)||(pStep == NULL))
		return daserrf(DASERR_VALUE, "Null pointer in dasval_accum");

	switch(vt){
	case DASVT_UBYTE : ACCUM_U( uint8_t,  UINT8_MAX); return DAS_OKAY;
	case DASVT_USHORT: ACCUM_U(uint16_t, UINT16_MAX); return DAS_OKAY;
	case DASVT_UINT  : ACCUM_U(uint32_t, UINT32_MAX); return DAS_OKAY;
	case DASVT_ULONG : ACCUM_U(uint64_t, UINT64_MAX); return DAS_OKAY;

	case DASVT_BYTE  : ACCUM_I(  int8_t,  INT8_MIN,  INT8_MAX); return DAS_OKAY;
	case DASVT_SHORT : ACCUM_I( int16_t, INT16_MIN, INT16_MAX); return DAS_OKAY;
	case DASVT_INT   : ACCUM_I( int32_t, INT32_MIN, INT32_MAX); return DAS_OKAY;
	case DASVT_LONG  : ACCUM_I( int64_t, INT64_MIN, INT64_MAX); return DAS_OKAY;

	case DASVT_FLOAT : {
		float r = (*((float*)pStep)) * (float)nCount + (*((float*)pAccum));
		if(!isfinite(r)) goto ERR_OVR;
		*((float*)pAccum) = r;
		return DAS_OKAY;
	}
	case DASVT_DOUBLE: {
		double r = (*((double*)pStep)) * (double)nCount + (*((double*)pAccum));
		if(!isfinite(r)) goto ERR_OVR;
		*((double*)pAccum) = r;
		return DAS_OKAY;
	}

	case DASVT_TIME:
		return daserrf(DASERR_VALUE,
			"dasval_accum() does not handle DASVT_TIME; calendar math mixes a "
			"DasTime with a seconds step, accumulate the offset in the caller"
		);
	default:
		return daserrf(DASERR_VALUE,
			"dasval_accum() only supports simple numeric types, not %s",
			dasvt_name(vt)
		);
	}

ERR_OVR:
	return daserrf(DASERR_VALUE,
		"Overflow accumulating a %s value", dasvt_name(vt)
	);
ERR_NEG:
	return daserrf(DASERR_VALUE,
		"Negative count %td is undefined for unsigned type %s", nCount, dasvt_name(vt)
	);
}

#undef ACCUM_I
#undef ACCUM_U

/* ************************************************************************** */
/* Parse any string into a value */

das_errcode dasval_fromStr(
   const char* sStr, das_vt vt, uint8_t* pBuf, int nBufLen
){
	if(sStr == NULL){
		return daserrf(DASERR_VALUE, "Empty string can't be converted to a value");
	}
	/* See if the buffer is big enough for the binary size of this values type */
	if(nBufLen < dasvt_size(vt)){
		return daserrf(DASERR_VALUE, 
			"Output buffer is too small %d bytes to hold a value of type %s",
			nBufLen, dasvt_name(vt)
		);
	}

	switch(vt){
	case DASVT_UNK:
		return daserrf(DASERR_VALUE, "Cannot determine fill values for unknown types");
	case DASVT_NULL:
		return daserrf(DASERR_VALUE, "Cannot read a string as a value of type null");

	case DASVT_TEXT:
		{
			size_t nLen = strlen(sStr);
			if(nLen <= nBufLen){
				memcpy((char*)pBuf, sStr, nLen);
				return DAS_OKAY;
			}
		}
		daslog_errorf(
			"string value '%s' can't fit into %d byte buffer", sStr, nBufLen
		);
		return DASERR_VALUE;

	case DASVT_UBYTE:
	case DASVT_BYTESEQ:
		if(sscanf(sStr, "%hhu", (uint8_t*)pBuf) == 1) return DAS_OKAY; 
		else goto ERR_PARSE;
	case DASVT_BYTE:
		if(sscanf(sStr, "%hhd", (int8_t*)pBuf) == 1) return DAS_OKAY;
		else goto ERR_PARSE;
	case DASVT_USHORT:
		if(sscanf(sStr, "%hu", (uint16_t*)pBuf) == 1) return DAS_OKAY;
		else goto ERR_PARSE;
	case DASVT_SHORT:
		if(sscanf(sStr, "%hd", (int16_t*)pBuf) == 1) return DAS_OKAY;
		else goto ERR_PARSE;
	case DASVT_UINT:
		if(sscanf(sStr, "%u", (uint32_t*)pBuf) == 1) return DAS_OKAY;
		else goto ERR_PARSE;
	case DASVT_INT:
		if(sscanf(sStr, "%d", (int32_t*)pBuf) == 1) return DAS_OKAY;
		else goto ERR_PARSE;
	case DASVT_ULONG:
		/* SCNu64, musl's sscanf accepts "%Lu" for an integer conversion but
		   stores nothing! */
		if(sscanf(sStr, "%" SCNu64, (uint64_t*)pBuf) == 1) return DAS_OKAY;
		else goto ERR_PARSE;
	case DASVT_LONG:
		if(sscanf(sStr, "%" SCNd64, (int64_t*)pBuf) == 1) return DAS_OKAY;
		else goto ERR_PARSE;
	case DASVT_FLOAT:
		if(sscanf(sStr, "%f", (float*)pBuf) == 1) return DAS_OKAY;
		else goto ERR_PARSE;
	case DASVT_DOUBLE:
		if(sscanf(sStr, "%lf", (double*)pBuf) == 1) return DAS_OKAY;
		else goto ERR_PARSE;
	case DASVT_TIME:
		if(DasTime_fromStr((DasTime*)pBuf, sStr) == 1) return DAS_OKAY;
		else goto ERR_PARSE;
	default:
		return daserrf(DASERR_VALUE, "Unknown value type code: %d", vt);
	}

ERR_PARSE:
	return daserrf(
		DASERR_VALUE, "Could not parse '%s' as a %s value", sStr, dasvt_name(vt)
	);
}

/* ************************************************************************* */
/* Generate a printf string for any value type, if with supplied try to fit 
 * it in a certain width.  It's often that case that values are stored in
 * types that have far greater range then the actual data */

/* Don't fail for certain semantics, but do offer extra support where detected */
das_errcode dasvt_fmt(
	das_vt vt, const char* sSemantic, int nFitTo, char* sBuf, size_t uBufLen
){
	bool bBlob = (strcmp(sSemantic, DAS_SEM_BLOB) == 0);
	bool bText = (strcmp(sSemantic, DASSEM_TEXT) == 0);

	switch(vt){
	case DASVT_UBYTE:
		if(nFitTo < 1){
			if(bBlob)      snprintf(sBuf, uBufLen, "%s", "%0hhX");
			else if(bText) snprintf(sBuf, uBufLen, "%s", "%s");
			else           snprintf(sBuf, uBufLen, "%s", "%hhu");
		}
		else{
			if(bBlob)      snprintf(sBuf, uBufLen, "%%0%dhhX", nFitTo);
			else if(bText) snprintf(sBuf, uBufLen,   "%% %ds", nFitTo);
			else           snprintf(sBuf, uBufLen, "%% %dhhu", nFitTo);
		}
		break;
	case DASVT_BYTE:
		if(nFitTo < 1){
			if(bText) snprintf(sBuf, uBufLen, "%s", "%s");
			else      snprintf(sBuf, uBufLen, "%s", "%hhd");
		}
		else{
			if(bText) snprintf(sBuf, uBufLen,   "%% %ds", nFitTo); 
			else      snprintf(sBuf, uBufLen, "%% %dhhd", nFitTo); 
		}
		break;


	case DASVT_USHORT:
		if(nFitTo < 1){
			if(bBlob) snprintf(sBuf, uBufLen, "%s", "%0hX");
			else      snprintf(sBuf, uBufLen, "%s", "%hu");
		}
		else{
			if(bBlob) snprintf(sBuf, uBufLen, "%%0%dhX", nFitTo);
			else      snprintf(sBuf, uBufLen, "%% %dhu", nFitTo);
		}
		break;
	case DASVT_SHORT:
		if(nFitTo < 1) snprintf(sBuf, uBufLen, "%s", "%hd");
		else           snprintf(sBuf, uBufLen, "%% %dhd", nFitTo);
		break;


	case DASVT_UINT:
		if(nFitTo < 1){
			if(bBlob) snprintf(sBuf, uBufLen, "%s", "%0X");
			else      snprintf(sBuf, uBufLen, "%s", "%u");
		}
		else{
			if(bBlob) snprintf(sBuf, uBufLen, "%%0%dX", nFitTo);
			else      snprintf(sBuf, uBufLen, "%% %du", nFitTo);
		}
		break;
	case DASVT_INT:
		if(nFitTo < 1) snprintf(sBuf, uBufLen, "%s", "%d");
		else           snprintf(sBuf, uBufLen, "%% %dd", nFitTo);
		break;

	case DASVT_ULONG:
		if(nFitTo < 1){
			if(bBlob) snprintf(sBuf, uBufLen, "%s", "%0" PRIX64); /* from inttypes.h */
			else      snprintf(sBuf, uBufLen, "%s", "%" PRIu64);
		}
		else{
			if(bBlob) snprintf(sBuf, uBufLen, "%%0%d" PRIX64, nFitTo);
			else      snprintf(sBuf, uBufLen, "%% %d" PRIu64, nFitTo);
		}
		break;
	case DASVT_LONG:
		if(nFitTo < 1) snprintf(sBuf, uBufLen, "%s", "%" PRId64);
		else           snprintf(sBuf, uBufLen, "%% %d" PRId64, nFitTo);
		break;

	case DASVT_FLOAT:
	case DASVT_DOUBLE:
		if(nFitTo < 1){
			if(vt == DASVT_FLOAT) snprintf(sBuf, uBufLen, "%s", "%.4e");
			else              snprintf(sBuf, uBufLen, "%s", "%.8e");
		}
		else{
			/* Precision as a function of field width.  A "% W.Pe" value with the
			 * space flag always occupies P+7 columns: a sign-or-space, one digit,
			 * the dot, P fraction digits, 'e', the exponent sign and two exponent
			 * digits.  So to keep every value (positive OR negative) inside the
			 * requested width, P must be nFitTo - 7, not nFitTo - 6.  The extra
			 * column the caller leaves on top of this holds the trailing
			 * separator/newline. */
			if(nFitTo >= 10)
				snprintf(sBuf, uBufLen, "%% %d.%de", nFitTo, nFitTo - 7);
			else
				snprintf(sBuf, uBufLen, "%% %d.%de", nFitTo, 2);
		}
		break;

	case DASVT_TIME:
		if(nFitTo < 1){
			/* No guidance, just pick milliseconds, usually pretty good in space physics */
			snprintf(sBuf, uBufLen, "%s", "%04d-%02d-%02dT%02d:%02d:%06.3f");
		}
		else{
			/* Okay, they want resolution to fit a particular width, here goes... */
			switch(nFitTo){
			/* Year only */
			case 4: snprintf(sBuf, uBufLen, "%s", "%04d"); break;
			case 5: snprintf(sBuf, uBufLen, "%s", "%04d "); break;
			case 6: snprintf(sBuf, uBufLen, "%s", "%04d  "); break;
			
			/* Year and Month */
			case 7: snprintf(sBuf, uBufLen, "%s", "%04d-%02d"); break;
			case 8: snprintf(sBuf, uBufLen, "%s", "%04d-%02d "); break;
			case 9: snprintf(sBuf, uBufLen, "%s", "%04d-%02d  "); break;
			
			/* Year/Month/Day of Month */
			case 10: snprintf(sBuf, uBufLen, "%s", "%04d-%02d-%02d"); break;
			case 11: snprintf(sBuf, uBufLen, "%s", "%04d-%02d-%02d "); break;
			case 12: snprintf(sBuf, uBufLen, "%s", "%04d-%02d-%02d  "); break;
			
			/* Date + Hour */
			case 13: snprintf(sBuf, uBufLen, "%s", "%04d-%02d-%02dT%02d"); break;
			case 14: snprintf(sBuf, uBufLen, "%s", "%04d-%02d-%02dT%02d "); break;
			case 15: snprintf(sBuf, uBufLen, "%s", "%04d-%02d-%02dT%02d  "); break;
			
			/* Date + Hour:min */
			case 16: snprintf(sBuf, uBufLen, "%s", "%04d-%02d-%02dT%02d:%02d"); break;
			case 17: snprintf(sBuf, uBufLen, "%s", "%04d-%02d-%02dT%02d:%02d "); break;
			case 18: snprintf(sBuf, uBufLen, "%s", "%04d-%02d-%02dT%02d:%02d  "); break;
			
			/* Date + Hour:min:sec */
			case 19: snprintf(sBuf, uBufLen, "%s", "%04d-%02d-%02dT%02d:%02d:%02.0f"); break;
			case 20: snprintf(sBuf, uBufLen, "%s", "%04d-%02d-%02dT%02d:%02d:%02.0f "); break;
			
			/* Date + hour:min:sec + frac seconds */
			case 21: snprintf(sBuf, uBufLen, "%s", "%04d-%02d-%02dT%02d:%02d:%04.1f"); break;
			case 22: snprintf(sBuf, uBufLen, "%s", "%04d-%02d-%02dT%02d:%02d:%05.2f"); break;
			case 23: snprintf(sBuf, uBufLen, "%s", "%04d-%02d-%02dT%02d:%02d:%06.3f"); break;
			case 24: snprintf(sBuf, uBufLen, "%s", "%04d-%02d-%02dT%02d:%02d:%06.3f "); break;
			case 25: snprintf(sBuf, uBufLen, "%s", "%04d-%02d-%02dT%02d:%02d:%06.3f  "); break;
			case 26: snprintf(sBuf, uBufLen, "%s", "%04d-%02d-%02dT%02d:%02d:%09.6f"); break;
			case 27: snprintf(sBuf, uBufLen, "%s", "%04d-%02d-%02dT%02d:%02d:%09.6f "); break;
			case 28: snprintf(sBuf, uBufLen, "%s", "%04d-%02d-%02dT%02d:%02d:%09.6f  "); break;
			case 29: snprintf(sBuf, uBufLen, "%s", "%04d-%02d-%02dT%02d:%02d:%012.9f"); break;
			
			/* No more resolution available.  Padding the output out to nFitTo
			   was intended here but never ran (it sat after the break), so
			   wider fields get the 29 character form, as they always have. */
			default: 
				snprintf(sBuf, uBufLen, "%s", "%04d-%02d-%02dT%02d:%02d:%012.9f"); break;
			}
		}
		break;

	default:
		return daserrf(DASERR_VALUE, 
			"Default format string not available for type '%s'", dasvt_name(vt)
		);
	}

	if((uBufLen == 0)||(strlen(sBuf) + 1 >= uBufLen))
		return daserrf(DASERR_VALUE, "%zu bytes is too small for a '%s' format "
		                 "string", uBufLen, dasvt_name(vt));
	return DAS_OKAY;
}
