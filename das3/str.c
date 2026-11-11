/* Copyright (C) 2004-2026 Chris Piker <chris-piker@uiowa.edu>
 *                    2004 Jeremy Faden <jeremy-faden@uiowa.edu>
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

#include <errno.h>
#include <locale.h>
#include <stdlib.h>
#include <stdio.h>
#include <stdarg.h>
#include <stdint.h>
#include <string.h>
#include <limits.h>
#include <ctype.h>
#include <math.h>

#include "str.h"
#include "util.h"
#include "util_p.h"
#include "log.h"

/* ************************************************************************* */
/* Locale handling */

#ifdef _WIN32
static bool g_bCLocalInit = false;  /* set by windows das_strtod_c, if needed */

/* das_init() resets this so the next das_strtod_c() makes the C locale anew */
void _dasstr_resetCLocale(void){ g_bCLocalInit = false; }
#endif

/* ************************************************************************** */
/* Data structure creation utilities */

char* das_strdup(const char* sIn)
{
	if((sIn == NULL)||(sIn[0] == '\0')) return NULL;
	size_t uLen = strlen(sIn);
	char* sOut = (char*)calloc(uLen+1, sizeof(char));
	if(sOut == NULL){
		das_error(DASERR_UTIL, "Memory allocation error");
		return NULL;
	}
	memcpy(sOut, sIn, uLen);
	return sOut;
}


/* ************************************************************************* */
/* String utilities */

void das_store_str(char** psDest, size_t* puLen, const char* sSrc){
	size_t uNewLen = strlen(sSrc) + 1;
	if(uNewLen > *puLen){
		if(*psDest != NULL) free(*psDest);
		*psDest = calloc(uNewLen, sizeof(char));
		*puLen = uNewLen;
	}
	strncpy(*psDest, sSrc, uNewLen);
}

char* das_vstring(const char* fmt, va_list ap){

	/* Guess we need no more than 64 bytes. */
	int n, size = 64;
	char *p;
	va_list _ap;

	if( (p = malloc(size)) == NULL){
		das_error(DASERR_UTIL, "Can't alloc %d bytes", size);
		return NULL;
	}

	while (1) {
		/* Try to print in the allocated space. */
		va_copy(_ap, ap);
		n = vsnprintf (p, size, fmt, _ap);
		va_end(_ap);

		/* If that worked, return the string. */
		if (n > -1 && n < size)
			break;

		/* Else try again with more space. */
		if (n > -1) 	/* glibc 2.1 */
			size = n+1; /* precisely what is needed */
		else  			/* glibc 2.0 */
			size *= 2;  /* twice the old size */

		if((p = realloc(p, size)) == NULL){
			das_error(DASERR_UTIL, "Can't alloc %d bytes", size);
			return NULL;
		}
	}

	return p;
}

char* das_string(const char *fmt, ...){

	/* Guess we need no more than 64 bytes. */
	int n, size = 64;
	char *p;
	va_list ap;

	if( (p = malloc(size)) == NULL){
		das_error(DASERR_UTIL, "Can't alloc %d bytes", size);
		return NULL;
	}

	while (1) {
		/* Try to print in the allocated space. */
		va_start(ap, fmt);
		n = vsnprintf (p, size, fmt, ap);
		va_end(ap);

		/* If that worked, return the string. */
		if (n > -1 && n < size)
			break;

		/* Else try again with more space. */
		if (n > -1) 	/* glibc 2.1 */
			size = n+1; /* precisely what is needed */
		else  			/* glibc 2.0 */
			size *= 2;  /* twice the old size */

		if((p = realloc(p, size)) == NULL){
			das_error(DASERR_UTIL, "Can't alloc %d bytes", size);
			return NULL;
		}
	}

	return p;
}

/* Common config-file case, remove whitespace and simple line comments */
char* das_strip(char* sLine, char cComment)
{
	if(sLine == NULL) return NULL;

	/* Terminate at comment char if there is one */
	char* pBeg = sLine;
	if(cComment != '\0'){
		while(*pBeg != '\0'){
			if(*pBeg == cComment){ 
				*pBeg = '\0';
				break; 
			}
			else
				++pBeg;
		}
	}

	/* Advance past whitespace, or bail out */
	pBeg = sLine;
	while(*pBeg != '\0'){
		if(isspace(*pBeg))
			++pBeg;
		else	
			break;
	}
	if(*pBeg == '\0')
		return NULL;

	/* Null out ending whitespace.  Loop will terminate because
	    we know there's at least one non-space char in there */
	char* pEnd = pBeg + strlen(pBeg) - 1;
	while(isspace(*pEnd)){
		*pEnd = '\0';
		--pEnd;
	}
	return pBeg;
}

/* ************************************************************************* */
/* Copy string as an XML token, leading and traily spaces are ignore,
 * internal space characters are converted and collapsed to single spaces 
 *
 * Returns the equivalent of strlen(dest).
 *
 * The output string is always null terminated if n > 1
 */

size_t das_tokncpy(char* dest, const char* src, size_t n)
{
	size_t u = 0;
	
	if(n < 1) return u;
	if(n == 1){ *dest = '\0'; return u; }
	
	*(dest + n - 1) = '\0';
	bool inspace = true;  /* Act as if there were spaces ahead of the start */
	
	while((*src != '\0')&&(u < (n-1))){
		if(isspace(*src)){
			if(!inspace){
				*dest = ' ';
				++dest;
				++u;
				inspace = true;
			}
		}
		else{
			inspace = false;
			*dest = *src;
			++dest;
			++src;
			++u;
		}
		++src;
	}
	
	if((u > 0)&&(dest[u-1] == ' ')){
		dest[u-1] = '\0';
		--u;
	}
	return u;
}

static const char* _g_sEscChar = "\"'<>&";
static const int _g_nEscChar = 5;
static const char* _g_sReplace[5] = {
	"&quot;", "&apos;", "&lt;", "&gt;","&amp;"
};

/* ************************************************************************* */
/* translate unsafe characters for XML string data  */
	
const char* das_xml_escape(const char* src, char* dest, size_t uOutLen)
{
	if(uOutLen < 1) return NULL;

	memset(dest, 0, sizeof(char)*uOutLen);

	/* Bound on output, not input: one '&' becomes five bytes.  Stop at the
	   first character that doesn't fit whole, a cut entity is not XML. */
	size_t uOut = 0;
	size_t uIn = 0;
	for(; src[uIn] != '\0'; ++uIn){
		const char* sRep = NULL;
		for(int i = 0; i < _g_nEscChar; ++i){
			if(src[uIn] == _g_sEscChar[i]){ sRep = _g_sReplace[i]; break; }
		}
		size_t uLen = sRep ? strlen(sRep) : 1;
		if(uOut + uLen > uOutLen - 1) break;

		if(sRep) memcpy(dest + uOut, sRep, uLen);
		else     dest[uOut] = src[uIn];
		uOut += uLen;
	}

	/* Stopped inside a multi-byte UTF-8 character, drop the partial one.
	   Entities are ASCII, so any high bytes at the end came from src. */
	if(((unsigned char)src[uIn] & 0xC0) == 0x80){
		while((uOut > 0)&&(((unsigned char)dest[uOut-1] & 0xC0) == 0x80))
			dest[--uOut] = '\0';
		if((uOut > 0)&&(((unsigned char)dest[uOut-1] & 0xC0) == 0xC0))
			dest[--uOut] = '\0';
	}
	return dest;
}


/* ************************************************************************ */
/* Dealing with string to double conversions across locales */

#ifdef WIN32

static _locale_t c_locale;

double das_strtod_c(const char *nptr, char **endptr){

  if(!g_bCLocalInit){
    c_locale = _create_locale(LC_ALL,"C");
    g_bCLocalInit = true;
  }

  return _strtod_l(nptr, endptr, c_locale);
}

#else  /* Posix */

double das_strtod_c(const char *nptr, char **endptr){
  char buf[80];
  
  char* pPt = strchr(nptr, '.');

  // If no radix, don't bother filling the buffer
  if(pPt == NULL || (size_t)(pPt - nptr) >= sizeof(buf))
    return strtod(nptr, endptr);
  
  struct lconv* pLocale= localeconv();
  *buf = '\0';
  strncat(buf, nptr, sizeof(buf) - 1);
  
  buf[pPt - nptr] = *(pLocale->decimal_point); // Assumes radix is 1-char long!

  char* pEnd;
  double rVal = strtod(buf, &pEnd);

  if(endptr)
    *endptr = ((char*) nptr) + (pEnd - buf);
  
  return rVal;
}

#endif


/* ************************************************************************* */
/* Base 64 */

/* Simple base 64 encoding (maybe too simple, probably dosen't handle
 * utf-8 correctly).  Moved here from creds.c so codec.c and others can
 * share it.  Gangk-ed from stackoverflow at url:
 *
 * https://stackoverflow.com/questions/342409/how-do-i-base64-encode-decode-in-c
 *
 * Posted by user: ryyst
 */
static char encoding_table[] = {'A', 'B', 'C', 'D', 'E', 'F', 'G', 'H',
                                'I', 'J', 'K', 'L', 'M', 'N', 'O', 'P',
                                'Q', 'R', 'S', 'T', 'U', 'V', 'W', 'X',
                                'Y', 'Z', 'a', 'b', 'c', 'd', 'e', 'f',
                                'g', 'h', 'i', 'j', 'k', 'l', 'm', 'n',
                                'o', 'p', 'q', 'r', 's', 't', 'u', 'v',
                                'w', 'x', 'y', 'z', '0', '1', '2', '3',
                                '4', '5', '6', '7', '8', '9', '+', '/'};
static int mod_table[] = {0, 2, 1};

char* das_b64_encode(
	const unsigned char *data, size_t input_length, size_t *output_length
) {

    *output_length = 4 * ((input_length + 2) / 3);

    /* One more than the length: calloc's zero there is the terminator */
    char *encoded_data = calloc(*output_length + 1, sizeof(char));
    if (encoded_data == NULL) return NULL;

    for (int i = 0, j = 0; i < input_length;) {

        uint32_t octet_a = i < input_length ? (unsigned char)data[i++] : 0;
        uint32_t octet_b = i < input_length ? (unsigned char)data[i++] : 0;
        uint32_t octet_c = i < input_length ? (unsigned char)data[i++] : 0;

        uint32_t triple = (octet_a << 0x10) + (octet_b << 0x08) + octet_c;

        encoded_data[j++] = encoding_table[(triple >> 3 * 6) & 0x3F];
        encoded_data[j++] = encoding_table[(triple >> 2 * 6) & 0x3F];
        encoded_data[j++] = encoding_table[(triple >> 1 * 6) & 0x3F];
        encoded_data[j++] = encoding_table[(triple >> 0 * 6) & 0x3F];
    }

    for (int i = 0; i < mod_table[input_length % 3]; i++)
        encoded_data[*output_length - 1 - i] = '=';

    return encoded_data;
}

/* Reverse base64 alphabet: ASCII byte -> 6-bit value, -1 for non-alphabet.  A
   compile-time const table, so decoding has no per-call setup and no malloc. */
static const signed char b64_dec[256] = {
	 -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,
	 -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,
	 -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,  62,  -1,  -1,  -1,  63,
	 52,  53,  54,  55,  56,  57,  58,  59,  60,  61,  -1,  -1,  -1,  -1,  -1,  -1,
	 -1,   0,   1,   2,   3,   4,   5,   6,   7,   8,   9,  10,  11,  12,  13,  14,
	 15,  16,  17,  18,  19,  20,  21,  22,  23,  24,  25,  -1,  -1,  -1,  -1,  -1,
	 -1,  26,  27,  28,  29,  30,  31,  32,  33,  34,  35,  36,  37,  38,  39,  40,
	 41,  42,  43,  44,  45,  46,  47,  48,  49,  50,  51,  -1,  -1,  -1,  -1,  -1,
	 -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,
	 -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,
	 -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,
	 -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,
	 -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,
	 -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,
	 -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,
	 -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,
};

/* No-alloc base64 decode into a caller buffer.  
   Decoded output is at most 3/4 of input_length, so
     out_cap >= 3*input_length/4
   is always long enough. */
int das_b64_decode_buf(
	const char* data, size_t input_length, unsigned char* out, size_t out_cap,
	size_t* output_length
){
	if((input_length % 4) != 0)
		return -1 * das_error(DASERR_UTIL,
			"base64 input length %zu is not a multiple of 4", input_length
		);
	if(input_length == 0){ *output_length = 0; return DAS_OKAY; }

	size_t olen = (input_length / 4) * 3;
	if(data[input_length - 1] == '=') --olen;
	if(data[input_length - 2] == '=') --olen;

	if(olen > out_cap)
		return -1 * das_error(DASERR_UTIL,
			"base64 decode needs %zu bytes but the buffer holds %zu", olen, out_cap
		);

	size_t j = 0;
	for(size_t i = 0; i < input_length; i += 4){
		signed char a = (data[i]  =='=') ? 0 : b64_dec[(unsigned char)data[i]];
		signed char b = (data[i+1]=='=') ? 0 : b64_dec[(unsigned char)data[i+1]];
		signed char c = (data[i+2]=='=') ? 0 : b64_dec[(unsigned char)data[i+2]];
		signed char d = (data[i+3]=='=') ? 0 : b64_dec[(unsigned char)data[i+3]];
		if((a < 0)||(b < 0)||(c < 0)||(d < 0))
			return -1 * das_error(DASERR_UTIL, "invalid base64 character in input");

		uint32_t triple = ((uint32_t)a << 18)|((uint32_t)b << 12)|((uint32_t)c << 6)|(uint32_t)d;
		if(j < olen) out[j++] = (triple >> 16) & 0xFF;
		if(j < olen) out[j++] = (triple >>  8) & 0xFF;
		if(j < olen) out[j++] =  triple        & 0xFF;
	}
	*output_length = olen;
	return DAS_OKAY;
}

/* Alloc-and-return base64 decode, symmetric with das_b64_encode.  Thin wrapper
   over das_b64_decode_buf for one-shot / general callers; the codec uses the
   _buf form directly to stay malloc-free in its item loop. */
unsigned char* das_b64_decode(
	const char* data, size_t input_length, size_t* output_length
){
	size_t cap = (input_length / 4) * 3;
	unsigned char* out = (unsigned char*)malloc(cap > 0 ? cap : 1);
	if(out == NULL) return NULL;
	if(das_b64_decode_buf(data, input_length, out, cap, output_length) != DAS_OKAY){
		free(out);
		*output_length = 0;
		return NULL;
	}
	return out;
}


/* ************************************************************************* */
/* URL percent encoding */

size_t das_url_encode(const char* sIn, char* sOut, size_t uOut)
{
	static const char* sHex = "0123456789ABCDEF";
	size_t uNeed = 0;
	if(uOut > 0) sOut[0] = '\0';
	for(const unsigned char* p = (const unsigned char*)sIn; p && *p; ++p){
		bool bPlain = isalnum(*p)||(*p == '-')||(*p == '.')||(*p == '_')||(*p == '~');
		size_t n = bPlain ? 1 : 3;
		if(uNeed + n < uOut){
			if(bPlain){
				sOut[uNeed] = (char)*p;
			}
			else{
				sOut[uNeed] = '%';
				sOut[uNeed + 1] = sHex[*p >> 4];
				sOut[uNeed + 2] = sHex[*p & 0x0F];
			}
			sOut[uNeed + n] = '\0';
		}
		uNeed += n;
	}
	return uNeed;
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

	/* First character decides, case-insensitively: T/t/Y/y/1 -> true, F/f/N/n/0
	   -> false.  This subsumes the whole words (true/false/yes/no) and their mixed
	   case (TrUe), so no separate strcasecmp pass is needed. */
	char c = (char)tolower((unsigned char)str[0]);

	if(c == 't' || c == 'y' || c == '1'){
		*pRes = true;
		return true;
	}

	if(c == 'f' || c == 'n' || c == '0'){
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

double* das_csv2doubles(const char* sCsv, int* pNumItems)
{
	if(pNumItems == NULL){
		das_error(DASERR_VALUE, "No place to put the item count");
		return NULL;
	}
	*pNumItems = 0;
	if(sCsv == NULL){
		das_error(DASERR_VALUE, "No text to parse as comma separated values");
		return NULL;
	}

	/* Blank text is an empty list, not one zero */
	const char* p = sCsv;
	while(isspace((unsigned char)*p)) ++p;
	int nItems = (*p == '\0') ? 0 : 1;
	for(const char* q = p; *q != '\0'; ++q)
		if(*q == ',') ++nItems;

	/* Never NULL on success, so NULL always means an error */
	double* pOut = (double*)calloc((nItems > 0) ? nItems : 1, sizeof(double));
	if(pOut == NULL){
		das_error(DASERR_VALUE, "Couldn't allocate %d doubles", nItems);
		return NULL;
	}

	for(int i = 0; i < nItems; ++i){
		char* pEnd = NULL;
		pOut[i] = das_strtod_c(p, &pEnd);    /* '.' whatever the locale */
		while(isspace((unsigned char)*pEnd)) ++pEnd;
		if((pEnd == p)||((*pEnd != ',')&&(*pEnd != '\0'))){
			das_error(DASERR_VALUE, "Item %d of '%s' is not a number", i, sCsv);
			free(pOut);
			return NULL;
		}
		p = (*pEnd == ',') ? pEnd + 1 : pEnd;
	}
	*pNumItems = nItems;
	return pOut;
}

/* One value, preceded by ", " unless it is the first.  Returns false when it
   would not fit, leaving pBuf terminated after the last value that did. */
static bool _csv_put(char* pBuf, size_t uSz, size_t* pOff, int i, double rVal)
{
	bool bExp = (fabs(rVal) < 0.00001)||(fabs(rVal) > 100000);
	int n = snprintf(pBuf + *pOff, uSz - *pOff, bExp ? "%s%e" : "%s%f",
	                 (i > 0) ? ", " : "", rVal);
	if((n < 0)||((size_t)n >= uSz - *pOff)){
		pBuf[*pOff] = '\0';
		return false;
	}
	*pOff += (size_t)n;
	return true;
}

char* das_doubles2csv(const double* pValues, int nValues, char* pBuf, size_t uSz)
{
	if((pBuf == NULL)||(uSz < 12)||((nValues > 0)&&(pValues == NULL))){
		das_error(DASERR_ARRAY, "Invalid arguments, buffer too small or non-existant");
		return NULL;
	}
	pBuf[0] = '\0';
	size_t uOff = 0;
	for(int i = 0; i < nValues; ++i){
		if(!_csv_put(pBuf, uSz, &uOff, i, pValues[i])){
			das_error(DASERR_ARRAY, "Insufficient space provided for all %d converted doubles", nValues);
			return NULL;
		}
	}
	return pBuf;
}

char* das_floats2csv(const float* pValues, int nValues, char* pBuf, size_t uSz)
{
	if((pBuf == NULL)||(uSz < 12)||((nValues > 0)&&(pValues == NULL))){
		das_error(DASERR_ARRAY, "Invalid arguments, buffer too small or non-existant");
		return NULL;
	}
	pBuf[0] = '\0';
	size_t uOff = 0;
	for(int i = 0; i < nValues; ++i){
		if(!_csv_put(pBuf, uSz, &uOff, i, (double)pValues[i])){
			das_error(DASERR_ARRAY, "Insufficient space provided for all %d converted floats", nValues);
			return NULL;
		}
	}
	return pBuf;
}
