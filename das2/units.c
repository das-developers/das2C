#define _POSIX_C_SOURCE 200112L


#include <stdio.h>
#include <stdlib.h>
#include <math.h>
#include <strings.h>
#include <stdarg.h>
#include <string.h>
#include <stdbool.h>
#include <ctype.h>

#include "das1.h"
#include "util.h"

#define _das2_units_c_
#include "units.h"

/* Component limits for generic units */
#define _COMP_MAX_NAME  63
#define _COMP_MAX_EXP   19
#define _MAX_NUM_COMP   20


/* Time Point Units */
const char* UNIT_US2000 = "us2000";
const char* UNIT_MJ1958 = "mj1958";
const char* UNIT_T2000 = "t2000";
const char* UNIT_T1970 = "t1970";
const char* UNIT_UTC   = "UTC";

/* Other common units */
const char* UNIT_SECONDS = "s";
const char* UNIT_HOURS = "hours";
const char* UNIT_DAYS  = "days";
const char* UNIT_MILLISECONDS = "ms";
const char* UNIT_MICROSECONDS = "μs";

const char* UNIT_DEGREES = "deg";

const char* UNIT_HERTZ = "Hz";
const char* UNIT_KILO_HERTZ = "kHz";
const char* UNIT_MEGA_HERTZ = "MHz";
const char* UNIT_E_SPECDENS = "V**2 m**-2 Hz**-1";
const char* UNIT_B_SPECDENS = "nT**2 Hz**-1";

const char* UNIT_NT = "nT";

const char* UNIT_NUMBER_DENS = "cm**-3";

const char* UNIT_DB = "dB";

const char* UNIT_KM = "km";   /* Welcome to radar work! */

const char* UNIT_DIMENSIONLESS = "";

/* This is the global units array.  */

/* WARNING: Units are not necessarily stored in the most reduced form.  It is
 * first come, first serve, on defining the arrangement of components in a unit
 * string. */
#define NUM_UNITS 63
const char* g_lUnits[NUM_UNITS+1] = {NULL};

/* Definitions for ad-hoc conversions, compare is case insensitive */
#define NUM_ADHOC 8
const char* g_sAdHocFrom[] = {
	"days", "day", "hours", "hour", "hr", "minutes", "minute", "min"
};

const char* g_sAdHocTo[] = {
	"s", "s", "s", "s", "s", "s", "s", "s"
};

double g_rAdHocFactor[NUM_ADHOC] = {
	3600*24, 3600*24, 3600, 3600, 3600, 60, 60, 60
};

/* Definitions for SI conversions */
#define NUM_SI_PREFIX 20
const char* g_sSiPreSym[] = {
	"Y", "Z", "E", "P", "T", "G", "M", "k", "h", "da",
	"d", "c", "m", "μ", "n", "p", "f", "a", "z", "y"
};

const char* g_sSiPreName[] = {
	"yotta", "zetta",   "exa",  "peta", "tera", 
	 "giga",  "mega",  "kilo", "hecto", "deca", 
	 "deci", "centi", "milli", "micro", "nano", 
	 "pico", "femto",  "atto", "zepto", "yocto"
};

const int g_nSiPrePower[NUM_SI_PREFIX] = {
	 24,  21, 18,   15,  12, 
	  9,   6,  3,    2,   1,
	 -1,  -2, -3,   -6,  -9,
	-12, -15, -18, -21, -24
};

# define NUM_SI_NAME 31
const char* g_sSiName[] = {
	  "meter",     "gram",    "second", "ampere", "kelvin",       "mole", 
	"candela",   "radian", "steradian",  "hertz", "newton",     "pascal",
	  "joule",     "watt",   "coulomb", "electronvolt", "volt",  "farad", 
	    "ohm",	"siemens",    "weber",   "tesla",    "henry",  "celsius",
	      "C",    "lumen",      "lux", "becquerel",   "gray",  "sievert",
	  "katal",
};

const char* g_sSiSymbol[] = {
	 "m",   "g",  "s",  "A",  "K", "mol", 
	"ca", "rad", "sr", "Hz",  "N",  "Pa",
	 "J",   "W",  "C", "eV",  "V",   "F",
	 "Ω",   "S",  "Wb",  "T",  "H", "°C",
	"°C",  "lm",  "lx", "Bq", "Gy", "Sv",
  "kat",
};

UnitType _Units_getUnique(const char* string)
{
	int i = 0;
	for(i = 0; g_lUnits[i] != NULL && i < NUM_UNITS; i++)
		if(strcmp(string, g_lUnits[i]) == 0) return g_lUnits[i];
	
	if(i < NUM_UNITS - 1){
		char* sHeap = (char*)calloc(strlen(string) + 1, sizeof(char));
		strncpy(sHeap, string, strlen(string));
		g_lUnits[i] = sHeap;
		return g_lUnits[i];
	}
	
	das2_error(15, "Out of space for user defined units, only %d different "
	           "unit types supported in a single program", NUM_UNITS - 1);
	return NULL;
}

/* Unfortunately this is just an enumeration of know interval types */
UnitType Units_interval(UnitType unit){
	if(unit == UNIT_US2000) return UNIT_MICROSECONDS;
	if(unit == UNIT_MJ1958) return UNIT_DAYS;
	if(unit == UNIT_T2000) return UNIT_SECONDS;
	if(unit == UNIT_T1970) return UNIT_SECONDS;
	if(unit == UNIT_UTC) return UNIT_SECONDS;
	return unit;
}

/* Happens to be identical to Units_haveCalRep for now, will likely diverge
 * in the future. */
bool Units_isInterval(UnitType unit){
	return (unit == UNIT_US2000) || (unit == UNIT_MJ1958) || (unit == UNIT_T2000)
	       || (unit == UNIT_T1970) || (unit == UNIT_UTC);
}

/* Happens to be identical to Units_isInterval for now, will likely diverge
 * in the future. */
bool Units_haveCalRep(UnitType unit){
	return (unit == UNIT_US2000) || (unit == UNIT_MJ1958) || 
			 (unit == UNIT_T2000)  || (unit == UNIT_T1970) || 
			(unit == UNIT_UTC);
}

/* ************************************************************************* */
/* ************************************************************************* */
/* A generic unit string parser, breaks items down into base units with powers
 * 
 * This is an overly lenient parser handles stuff like... 
 * 
 *     thing_a**-1 thing_b thing_c^+2 / thing_d**2     
 *
 * Since all the special values are 7-bit ASCII characters, this code is UTF-8 
 * safe (thanks New Jersey napkin boys!)
 * 
 * legal component terminators are space, null, '/' sign  
 * legal name terminators are above and '*' or '^' 
 * legal power sign terminators are '-' '+' or a digit
 * legal exponent terminators are component terminators only
 * legal / terminator is itself
 * legal space terminator is null or non punctuation or non numbers
 * 
 * Parenthesis are explicitly not supported, if you want to do that it's
 * time chuck this ad-hoc code and use a real lexer -cwp
 *  follows this pattern 
 */

/* Note on arrays of base units.  */
struct base_unit{
	char sName[_COMP_MAX_NAME+1];
	char sExp[_COMP_MAX_EXP+1];
	int nExpNum;
	int nExpDenom;
	int nSortPref;   /* Lower numbered items prefere to sort earlier in */
	                 /* in output strings, used by multiply to somewhat */
						  /* preserve order */
};

/* Is this needed?  I think C equality would handle item by item struct copies */
void _Units_compCopy(struct base_unit* pDest, const struct base_unit* pSrc)
{
	strncpy(pDest->sName, pSrc->sName, _COMP_MAX_NAME);
	strncpy(pDest->sExp, pSrc->sExp, _COMP_MAX_EXP);
	pDest->nExpNum = pSrc->nExpNum;
	pDest->nExpDenom = pSrc->nExpDenom;
	pDest->nSortPref = pSrc->nSortPref;
}

/* Copy component array */
int _Units_copy(
	struct base_unit* pDest, const struct base_unit* pSrc, int nComp
){
	for(int i = 0; i < nComp; ++i) _Units_compCopy(pDest + i, pSrc + i);
	return nComp;
}

/* See if two component arrays are trivially equal, does not reorder look
 * for equivalent strings or any other jazz, ignores sort preference field */
bool _Units_reducedEqual(
	const struct base_unit* pA, const struct base_unit* pB, int nComp
){
	for(int i = 0; i < nComp; ++i){
		if(pA->nExpNum != pB->nExpNum) return false;
		if(pA->nExpDenom != pB->nExpDenom) return false;
		if((pA->sName != NULL)&&(pB->sName == NULL)) return false;
		if((pA->sName == NULL)&&(pB->sName != NULL)) return false;
		if((pA->sName != NULL)&&(pB->sName != NULL)){ 
			if(strcmp(pA->sName, pB->sName) != 0) return false;
		}
	}
	return true;
}

#define _STATE_INVALID 0
#define _STATE_NAME    1
#define _STATE_SEP     2
#define _STATE_EXOP    3
#define _STATE_EXP     4

/* These are just handled as part of the name for canonical unit string 
 * purposes, but for printing labels subscripts are detected.  That way
 * R_J looks the way you want it to. */
#define _STATE_SUBOP   5  
#define _STATE_SUB     6

bool _Units_isSepByte(char c, char n){
	if( c == '\0') return true;        /* True if end of string */
	if(isspace(c)) return true;        /* True if whitespace */
	return (c == '/')&&(! isdigit(n)); /* not true if in an exponent like r^1/2 */
}

bool _Units_isNameByte(char c, char n){
	if( ((c>>7)&0x1) == 0 ) return isalpha(c);  /* True if a ASCII 7-bit letter */
	if( ((c>>6)&0x3) == 0x2 ) return true;      /* True if UTF-8 continuation byte */
	
	/* True if UTF-8 start byte and next byte is a continuation byte */
	if( ( ((c>>6)&0x3) == 0x3 ) && ( ((n>>6)&0x3) == 0x2 ) ) return true;
	return false;
}

bool _Units_setNameByte(
	const char* sUnits, struct base_unit* pComp, int iName, char c
){
	if(iName >= _COMP_MAX_NAME){
		das2_error(15, "Units string '%s' has a subcomponent name longer than "
		           "%d bytes.", sUnits, _COMP_MAX_NAME);
		return false;
	}
	pComp->sName[iName] = c;
	return true;
}

bool _Units_isOpByte(char c, char n){ return (c == '*')||(c == '^'); }

bool _Units_isExpByte(char c, char n){
	if(c == '0') return false;
	return isdigit(c) || (c=='+') || (c=='-') || ((c=='/')&&(isdigit(n)));
}

bool _Units_setExpByte(
	const char* sUnits, struct base_unit* pComp, int iPower, char c
){
	if(iPower >= _COMP_MAX_EXP){
		das2_error(15, "Units string '%s' has an exponent string longer than "
		           "%d bytes.", sUnits, _COMP_MAX_EXP);
		return false;
	}
	pComp->sExp[iPower] = c;
	return true;
}

int _gcd(int a, int b)  /* Get greatest common divisor */
{
	if(a < 0) a *= -1;
	if(b < 0) b *= -1;
	
   while(a != b){
		if(a > b) a = a - b;
		else      b = b - a;
	}
	return a; /* choice is arbitrary at this point */
}

void _Units_reduceExp(struct base_unit* pBase){
	int nNum, nDenom, nGcd;
	
	nNum = pBase->nExpNum;
	nDenom = pBase->nExpDenom;
	
	/* Check signs, negative denominators are not allowed */
	if(pBase->nExpDenom < 1){
		das2_error(15, "Illegal denominator in exponent for '%s**%d/%d'",
		           pBase->sName, pBase->nExpNum, pBase->nExpDenom);
		return;
	}
	
	/* Simple reduction, denominator evenly divides numerator */
	if(nNum % nDenom == 0){ 
		nNum = nNum / nDenom;
		nDenom = 1;
	}
	else{
		/* More complex reduction, look for a common divisor > 1 */
		nGcd = _gcd(nNum, nDenom);
		
		nNum /= nGcd;  nDenom /= nGcd;
		
		if(nNum < 0 && nDenom < 0){	nNum *= -1;  nDenom *= -1; }
	}
	
	pBase->nExpNum = nNum;
	pBase->nExpDenom = nDenom;
}

bool _Units_finishComp(const char* sUnits, struct base_unit* pComp, int nPwrFlip)
{
	pComp->nExpNum = nPwrFlip;
	pComp->nExpDenom = 1;
	if(strlen(pComp->sExp) == 0) return true;
	
	/* Exponents have the following format:  [+ | -] 12345 [ / 12345 ]  */	
	char* pDiv = NULL;
	bool bOkay = true;
	if( (pDiv = strchr(pComp->sExp, '/')) != NULL){
		*pDiv = '\0';
		if( (bOkay=das2_str2baseint(pComp->sExp, 10, &(pComp->nExpNum))) )
			bOkay = das2_str2baseint(pDiv + 1, 10, &(pComp->nExpDenom));
		*pDiv = '/';
		bOkay = pComp->nExpDenom >= 1;
	}
	else{		
		bOkay = das2_str2baseint(pComp->sExp, 10, &(pComp->nExpNum));
	}
	
	if(! bOkay){
		das2_error(15, "Units string '%s' has an error in the exponent for "
				     "base unit '%s'", sUnits, pComp->sName);
		return false;
	}
	
	pComp->nExpNum *= nPwrFlip;
	
	_Units_reduceExp(pComp);
	
	return true;
}

/* ************************************************************************* */
/* These Component Sorting Rules: 
 *
 * 1. All positive powers always proceed all negative powers.
 *
 * 2. Higher preference goes first, lower preference last
 *	
 * 3. Higher exponents go first, higher go last 
 *    (i.e.:  a**2 proceeds b**1  but  a**-1 proceeds b**-2 )
 *
 * 4. Use locale's collating sequence 
 */
int _Units_positiveFirst(const void* vpUnit1, const void* vpUnit2)
{
	struct base_unit* pUnit1 = (struct base_unit*) vpUnit1;
	struct base_unit* pUnit2 = (struct base_unit*) vpUnit2;
	
	
	double rExp1 = ((double)pUnit1->nExpNum) / pUnit1->nExpDenom;
	double rExp2 = ((double)pUnit2->nExpNum) / pUnit2->nExpDenom;
	
	/* 1. Positive first */
	if((rExp1 > 0)&&(rExp2 < 0)) return -1;  
	if((rExp1 < 0)&&(rExp2 > 0)) return 1;
	
	/* 2. Higher preference goes first */
	if(pUnit1->nSortPref > pUnit2->nSortPref) return -1;
	if(pUnit1->nSortPref < pUnit2->nSortPref) return 1;
	
	/* 3. Higher exponents go first (or last if you think in absolute val 
	      terms) */
	if(rExp1 > rExp2) return -1;
	if(rExp1 < rExp2) return 1;
	
	/* 4. Just use strcmp for stability at least, this make capital leters
	      go first is the C locale (don't know about UTF-8) */
	return strcmp(pUnit1->sName, pUnit2->sName);
}

/* ************************************************************************* */
/* These Component Sorting Rules: 
 *
 * 1. Use locale's collating sequence (puts names together)
 * 
 * 2. higher powers always proceed lower powers.
 *
 * 3. Higher preference goes first, lower preference last
 * 	
 */
int _Units_adjacentNames(const void* vpUnit1, const void* vpUnit2)
{
	struct base_unit* pUnit1 = (struct base_unit*) vpUnit1;
	struct base_unit* pUnit2 = (struct base_unit*) vpUnit2;

	int nRet = strcmp(pUnit1->sName, pUnit2->sName);
	if(nRet != 0) return nRet;
	
	double rExp1 = ((double)pUnit1->nExpNum) / pUnit1->nExpDenom;
	double rExp2 = ((double)pUnit2->nExpNum) / pUnit2->nExpDenom;
	
	/* Higher powers go first */
	if(rExp1 > rExp2) return -1;
	if(rExp1 < rExp2) return 1;
	
	/* 2. Higher preference goes first */
	if(pUnit1->nSortPref > pUnit2->nSortPref) return -1;
	if(pUnit1->nSortPref < pUnit2->nSortPref) return 1;
	
	return 0;	
}
/* ************************************************************************** */
/* Returns the number of components successfully parsed, or -1 on an error */
/* Components are produced in the order they are decoded */

int _Units_strToComponents(
	const char* sUnits, struct base_unit* pComp, int nMaxComp
){
		
	int i,j;
	for(i = 0; i < nMaxComp; ++i){
		pComp[i].nExpNum = 1; pComp[i].nExpDenom = 1;
		for(j = 0; j < _COMP_MAX_NAME; ++j) pComp[i].sName[j] = '\0';
		for(j = 0; j < _COMP_MAX_EXP; ++j) pComp[i].sExp[j] = '\0';
		pComp[i].nSortPref = 0;
	}
	
	const char* pRead = sUnits;
	const char* pAhead = sUnits + 1;
	int iComp = 0, iName = 0, iPower = 0;
	int nOldState = _STATE_SEP;
	int nCurState = _STATE_INVALID;
	int nPwrFlip = 1;
	bool bError = true;
	
	do{
		/* 1. Determine the current parsing state based off the next character
		 *    assume state is unchanged unless proven otherwise */
		bError = false;
		nCurState = nOldState;
		
		switch(nOldState){	
		case _STATE_SEP:
			if(! _Units_isSepByte(*pRead, *pAhead)){
				if( _Units_isNameByte(*pRead, *pAhead)) nCurState = _STATE_NAME;
				else bError = true;
			}
			break;
			
		case _STATE_NAME:
			if(! _Units_isNameByte(*pRead, *pAhead)){
				if(_Units_isSepByte(*pRead, *pAhead)){ nCurState = _STATE_SEP; }
				else{
					if(_Units_isOpByte(*pRead, *pAhead)) nCurState = _STATE_EXOP;
					else bError = true;
				}
			}
			break;
			
		case _STATE_EXOP:
			if(! _Units_isOpByte(*pRead, *pAhead)){
				if( _Units_isExpByte(*pRead, *pAhead)) nCurState = _STATE_EXP; 
				else bError = true;
			}
			break;
		
		case _STATE_EXP:
			if(! _Units_isExpByte(*pRead, *pAhead)){
				if( _Units_isSepByte(*pRead, *pAhead)) nCurState = _STATE_SEP;
				else bError = true;
			}
			break;
			
		default: /* Byte was not consumed */ break;
		}
		
		if(bError){
			das2_error(15, "Error parsing units string '%s' at byte number %d",
					    sUnits, (pRead - sUnits)+1);
			return -1;
		}
		
		/* 2. On transition from Name or Exponent to Separator, finalize the old
		 *    component. */
		if((nCurState == _STATE_SEP) && 
			((nOldState == _STATE_NAME)||(nOldState == _STATE_EXP))){
			if(! _Units_finishComp(sUnits, pComp + iComp, nPwrFlip)) return -1;
			++iComp; iName = 0; iPower = 0;
		}
		
		/* 3. If we hit the END separator, quit reading */
		if(*pRead == '\0' ) break;
		
		/* 4. Eat the byte as part of the current state */
		switch(nCurState){
		case _STATE_SEP: 
			/* '/' can be buried in separator tokens, these flip the exp sign */
			/* The exponent can only flip once */
			if((*pRead == '/')&&(nPwrFlip == 1)) nPwrFlip = -1;
			break;
		case _STATE_NAME:
			if(iComp >= nMaxComp){
				das2_error(15, "Units string '%s' exceeds %d base unit sets", 
						     sUnits, nMaxComp);
				return -1;
			}
			if(! _Units_setNameByte(sUnits, pComp + iComp, iName, *pRead)) return -1;
			++iName;
			break;
		case _STATE_EXOP: 
			break;
		case _STATE_EXP:
			if(iComp >= nMaxComp){
				das2_error(15, "Units string '%s' exceeds %d base unit sets", 
						     sUnits, nMaxComp);
				return -1;
			}
			if(! _Units_setExpByte(sUnits, pComp + iComp, iPower, *pRead)) return -1;
			++iPower;
			break;
		default:
			das2_error(15, "Broken assumption, code fix required"); 
			return -1;
		}
				 
		nOldState = nCurState;
		++pRead;
		++pAhead;
		
	} while( true );
	
	return iComp;
}

/* Print a single base unit and it's power to a string buffer, returns number
 * of bytes (not characters) written, should really make this check buffer
 * length. */
int _Units_prnComp(char* pOut, const struct base_unit* pBase)
{
	int nWrote = 0, nOffset = 0;
	if( (nWrote = sprintf(pOut, pBase->sName)) < 0) return -1;
	nOffset += nWrote;
	if((pBase->nExpNum == 1)&&(pBase->nExpDenom == 1)) return nOffset;
	
	strcpy(pOut + nOffset, "**"); nOffset += 2;
	
	if( (nWrote = sprintf(pOut + nOffset, "%d", pBase->nExpNum)) < 0) return -1;
	nOffset += nWrote;
	if(pBase->nExpDenom == 1) return nOffset;
	
	strcpy(pOut + nOffset, "/"); nOffset += 1;
	if(pBase->nExpDenom < 1) return -1;   /* Can't have negative denominators */
	
	if( (nWrote = sprintf(pOut + nOffset, "%d", pBase->nExpDenom)) < 0) return -1;
	nOffset += nWrote;
	
	return nOffset;
}

/* Function prints units in the order they are defined ! */
UnitType _Units_fromCompAry(struct base_unit* pComp, int nComp){

	char sBuf[(_COMP_MAX_NAME * _COMP_MAX_EXP + 3)*_MAX_NUM_COMP + 1] = {'\0'};
	int i, nOffset = 0, nWrote = 0;
	for(i = 0; i<nComp; ++i){
		if( (nWrote = _Units_prnComp(sBuf + nOffset, pComp+i)) < 0){
			das2_error(15, "Error printing unit component units '%s'",pComp->sName);
			return NULL;
		}
		nOffset += nWrote;
		
		/* If not last item, add a space */
		if( i < (nComp - 1)){ sBuf[nOffset] = ' ';  ++nOffset; }
	}
		
	return _Units_getUnique(sBuf);
}

/* End general unit parser */


/* ************************************************************************** */
/* ************************************************************************** */
/* Unit Math Operations */

UnitType Units_invert(UnitType unit) {
	
	if(unit == UNIT_DIMENSIONLESS) return UNIT_DIMENSIONLESS;
	
	if( Units_isInterval(unit) ){
		
		das2_error(15, "Units '%s' are an offset from an epoch, these are not"
		           " invertible.  Use Unit_interval() to get an invertible"
		           " unit type first.", unit);
		return NULL;
	}
	
	if(unit == UNIT_SECONDS) return UNIT_HERTZ;
	if(unit == UNIT_MILLISECONDS) return UNIT_KILO_HERTZ; 
	if(unit == UNIT_MICROSECONDS) return UNIT_MEGA_HERTZ;

	if(unit == UNIT_HERTZ) return UNIT_SECONDS;
	if(unit == UNIT_KILO_HERTZ) return UNIT_MILLISECONDS;
	if(unit == UNIT_MEGA_HERTZ) return UNIT_MICROSECONDS;
	
	/* Okay, the rest use the general invert function */
	struct base_unit aComp[_MAX_NUM_COMP];
	
	int i; 
	int nComp = _Units_strToComponents(unit, aComp, _MAX_NUM_COMP);
	if(nComp < 0) return NULL;
	
	/* Now invert and resort */
	for(i = 0; i<nComp; ++i) aComp[i].nExpNum *= -1;
	
	qsort(aComp, nComp, sizeof(struct base_unit), _Units_positiveFirst);
	
	return _Units_fromCompAry(aComp, nComp);
}

void _Units_accumPowers(struct base_unit* pBase, const struct base_unit* pAdd)
{
	/* Use Identity:   a/x + b/y  = (ay + bx) / xy   */
	
	pBase->nExpNum = (pBase->nExpNum * pAdd->nExpDenom) + 
			              (pAdd->nExpNum * pBase->nExpDenom);
	
	pBase->nExpDenom = pBase->nExpDenom * pAdd->nExpDenom;
	
	/* Note, sort order is sticky */
	if(pAdd->nSortPref > pBase->nSortPref) pBase->nSortPref = pAdd->nSortPref;
	
	 _Units_reduceExp(pBase);		
}

UnitType Units_multiply(UnitType ut1, UnitType ut2)
{
	/* Dimensionless stuff reduces to trivial operations*/
	if(ut1 == UNIT_DIMENSIONLESS) return ut2;
	if(ut2 == UNIT_DIMENSIONLESS) return ut1;
	
	int i = 0;
	const char* ut[2] = {ut1, ut2};
	for(i = 0; i < 2; ++i){
		if( Units_isInterval(ut[i]) ){
			das2_error(15, "Units '%s' are an offset from an epoch, these are not"
			           " usable in algebraic operations.  Use Unit_interval() to get "
					     "an invertible unit type first.", ut[i]);
			return NULL;
		}
	}
	
	/* Do a general units square */
	struct base_unit comp1[_MAX_NUM_COMP] = { {{'\0'}, {'\0'}, 0, 0, 0} };
	struct base_unit comp2[_MAX_NUM_COMP] = { {{'\0'}, {'\0'}, 0, 0, 0} }; 
	int len1, len2;
	if( (len1=_Units_strToComponents(ut1,comp1,_MAX_NUM_COMP)) < 0) return NULL;
	if( (len2=_Units_strToComponents(ut2,comp2,_MAX_NUM_COMP)) < 0) return NULL;
	
	if(len1 + len2 > _MAX_NUM_COMP){
		das2_error(15, "Resulting units from the operation '%s' * '%s' has "
		           "more than %d subcomponents.", ut1, ut2, _MAX_NUM_COMP);
		return NULL;
	}
	
	/* Set a sort order for ut1 items that scoots them to the left, this */
	/* helps to make units come out closer to the way the programmer intended */
	int i1 = 0;
	for(i1 = 0; i1 < len1; ++i1) comp1[i1].nSortPref = 1;

	/* Combine components */
	struct base_unit comp3[_MAX_NUM_COMP] = { {{'\0'}, {'\0'}, 0, 0, 0} };
	int len3 = 0;
	bool bAdd = false;

	int i2 = 0, i3 = 0;
	for(i1 = 0; i1 < len1; ++i1){
		
		/* If component is already in the output, just accumulate powers */
		bAdd = true;
		for(i3 = 0; i3 < len3; ++i3){
			if(strcmp(comp1[i1].sName, comp3[i3].sName) == 0){
				_Units_accumPowers(comp3 + i3, comp1 + i1);
				bAdd = false;
			}
		}
		/* Else, copy it over */
		if(bAdd){
			_Units_compCopy(comp3 + len3, comp1 + i1);
			++len3;
		}
	}
	
	/* Really should do better than copy and paste here --cwp */
	for(i2 = 0; i2 < len2; ++i2){
		
		/* If component is already in the output, just accumulate powers */
		bAdd = true;
		for(i3 = 0; i3 < len3; ++i3){
			if(strcmp(comp2[i2].sName, comp3[i3].sName) == 0){
				_Units_accumPowers(comp3 + i3, comp2 + i2);
				bAdd = false;
			}
		}
		/* Else, copy it over */
		if(bAdd){
			_Units_compCopy(comp3 + len3, comp2 + i2);
			++len3;
		}
	}
	
	/* Sort positive first and use the sort order tags */
	qsort(comp3, len3, sizeof(struct base_unit), _Units_positiveFirst);
	return _Units_fromCompAry(comp3, len3);
}

UnitType Units_power(UnitType unit, int power)
{
	if( unit == UNIT_DIMENSIONLESS) return unit;
	
	if( Units_isInterval(unit) ){
		das2_error(15, "Units '%s' are an offset from an epoch, these are not"
			           " usable in algebraic operations.  Use Unit_interval() to get "
					     "an invertible unit type first.", unit);
		return NULL;
	}
	
	struct base_unit comp[_MAX_NUM_COMP] = { {{'\0'}, {'\0'}, 0, 0, 0} };
	int i, len = 0;
	if( (len=_Units_strToComponents(unit,comp,_MAX_NUM_COMP)) < 0)
		return NULL;
	
	for(i = 0; i<len; ++i) comp[i].nExpNum *= power;
	
	_Units_reduceExp(comp + i);
	
	/* Sort positive first and use the sort order tags */
	qsort(comp, len, sizeof(struct base_unit), _Units_positiveFirst);
	
	return _Units_fromCompAry(comp, len);
}

UnitType Units_root(UnitType unit, int root)
{
	if(root < 1){
		das2_error(15, "Value error root = '%d', expected >= 1", root);
		return NULL;
	}
	if( unit == UNIT_DIMENSIONLESS) return unit;
	
	if( Units_isInterval(unit) ){
		das2_error(15, "Units '%s' are an offset from an epoch, these are not"
			           " usable in algebraic operations.  Use Unit_interval() to get "
					     "an invertible unit type first.", unit);
		return NULL;
	}
	
	struct base_unit comp[_MAX_NUM_COMP] = { {{'\0'}, {'\0'}, 0, 0, 0} };
	int i, len = 0;
	if( (len=_Units_strToComponents(unit,comp,_MAX_NUM_COMP)) < 0)
		return NULL;
	
	for(i = 0; i<len; ++i){ comp[i].nExpDenom *= root; }
	
	/* Sort positive first and use the sort order tags */
	qsort(comp, len, sizeof(struct base_unit), _Units_positiveFirst);
	
	return _Units_fromCompAry(comp, len);
}

UnitType Units_divide(UnitType a, UnitType b)
{
	return Units_multiply( a, Units_power(b, -1));
}


/* End Units Algebra Section */
/* ************************************************************************** */

/* ************************************************************************** */
/* Generating various strings from units */

const char * Units_toStr( UnitType unit ) {
    if (unit == NULL) {
        return UNIT_DIMENSIONLESS;
    }
    return (const char *)unit;
}

char* Units_toLabel(UnitType unit, char* sBuf, int nLen)
{
	if(unit == NULL){ memset(sBuf, 0, nLen); return sBuf; }
	if((unit == UNIT_US2000) || (unit == UNIT_MJ1958) || (unit == UNIT_T2000)
	       || (unit == UNIT_T1970) || (unit == UNIT_UTC)){
		snprintf(sBuf, nLen - 1, "UTC");
		return sBuf;
	}
	
	const char* sUnits = Units_toStr(unit);
	int nOld = _STATE_SEP;
	int nCur = _STATE_INVALID;
	const char* pRead = sUnits;
	const char* pAhead = sUnits + 1;
	int iOut = 0;
	bool bError = false;
	
	--nLen; /* Always save a spot for the null */
	
	/* Byte by byte loop to generate the label */
	do{
		/* 1. Determine the current parsing state based off the next character
		 *    assume state is unchanged unless proven otherwise */
		nCur = nOld;
		bError = false;
		
		switch(nOld){
		case _STATE_SEP:
			if(! _Units_isSepByte(*pRead, *pAhead)){
				if((*pRead != '_')&&( _Units_isNameByte(*pRead, *pAhead))) 
					nCur = _STATE_NAME;
				else bError = true;
			}
			break;
		
		case _STATE_NAME:
			if(*pRead == '_'){ nCur = _STATE_SUBOP; break; }
			if(! _Units_isNameByte(*pRead, *pAhead)){
				if(_Units_isSepByte(*pRead, *pAhead)){ nCur = _STATE_SEP; }
				else{
					if(_Units_isOpByte(*pRead, *pAhead)) nCur = _STATE_EXOP;
					else bError = true;
				}
			}
			break;
		
		case _STATE_SUBOP:
			if(! (*pRead == '_')){
				if( _Units_isNameByte(*pRead, *pAhead)) nCur = _STATE_SUB;
				else bError = true; 
			}
			break;
		
		case _STATE_SUB:
			if((*pRead == '_' ) || ! _Units_isNameByte(*pRead, *pAhead)){
				if(_Units_isSepByte(*pRead, *pAhead)){  nCur = _STATE_SEP; }
				else{ 
					if(_Units_isOpByte(*pRead, *pAhead)) nCur = _STATE_EXOP;
					else bError = true; 
				}
			}
			break;
			
		case _STATE_EXOP:
			if(! _Units_isOpByte(*pRead, *pAhead)){
				if( _Units_isExpByte(*pRead, *pAhead)) nCur = _STATE_EXP; 
				else bError = true;
			}
			break;
		
		case _STATE_EXP:
			if(! _Units_isExpByte(*pRead, *pAhead)){
				if( _Units_isSepByte(*pRead, *pAhead)) nCur = _STATE_SEP;
				else bError = true;
			}
			break;
			
		default: /* Byte was not consumed */ break;
		}
		
		if(bError){
			das2_error(15, "Error parsing units string '%s' at byte number %d",
					    sUnits, (pRead - sUnits)+1);
			return NULL;
		}
		
		/* 2. Output special characters for transitions:
		 * 
		 * _STATE_NAME -> _STATE_SUBOP:                --> "!b"
		 * _STATE_SUB  -> _STATE_SEP || _STATE_EXOP:   --> "!n"
		 * _STATE_NAME || _STATE_SUB -> _STATE_EXOP:   --> "!a"
		 * _STATE_EXP -> _STATE_SEP:                   --> "!n"
		*/
		bError = false;
		if((nOld == _STATE_NAME) && (nCur == _STATE_SUBOP)){
			if(iOut < (nLen - 2)) strncpy(sBuf + iOut, "!b", 2);
			else bError = true;
			iOut += 2;
		}
		if((nOld == _STATE_SUB) && ((nCur == _STATE_EXOP)||(nCur == _STATE_SEP))){
			if(iOut < (nLen - 2)) strncpy(sBuf + iOut, "!n", 2);
			else bError = true;
			iOut += 2;
		}
		if(((nOld == _STATE_NAME)||(nOld == _STATE_SUB)) && (nCur == _STATE_EXOP)){
			if(iOut < (nLen - 2)) strncpy(sBuf + iOut, "!a", 2);
			else bError = true;
			iOut += 2;
		}
		if((nOld == _STATE_EXP) && (nCur == _STATE_SEP)){
			if(iOut < (nLen - 2)) strncpy(sBuf + iOut, "!n", 2);
			else bError = true;
			iOut += 2;
		}
		
		if(bError){
			das2_error(15, "Error parsing units string '%s' at byte number %d",
					    sUnits, (pRead - sUnits)+1);
			return NULL;
		}
		
		/* 3. If we hit the END separator, quit reading */
		if(*pRead == '\0' ) break;
		
		/* 4. States NAME, SUB, EXP, SEP all just cause byte copy */
		if((nCur == _STATE_NAME)||(nCur == _STATE_SUB)||(nCur == _STATE_EXP)||
		   (nCur == _STATE_SEP)){
			if(iOut < (nLen - 1)) sBuf[iOut] = *pRead;
			else bError = true;
			++iOut;
		}
		
		nOld = nCur;
		++pRead;
		++pAhead;
		
	} while(true);
	
	return sBuf;
}

/* ************************************************************************** */
/* Unit Reduction */

double _Units_reduceComp(struct base_unit* pComp)
{
	/* Special Case reductions */
	int nBytes = strlen(pComp->sName);
	if(nBytes == 0) return 1.0;  /* Good ole unitless */
	
	double rFactor = 1.0;
	
	/* First handle complete names that may look like something with metric
	 * prefixes included but really don't have those (ex: days looks like 
	 * 'deca' 'ys', whatever 'ys' units are) */

	int i = 0;
	for(i = 0; i < NUM_ADHOC; ++i){
		if( strcasecmp(pComp->sName, g_sAdHocFrom[i]) == 0){
			strncpy(pComp->sName, g_sAdHocTo[i], _COMP_MAX_NAME);
			
			rFactor *= pow(
				g_rAdHocFactor[i],
				pComp->nExpNum / ((double)(pComp->nExpDenom))
			);
			return rFactor;
		}
	}
	
	/* If the end of the unit looks like a full metric unit name (plus an 
	 * optional ending 's'),  replace it with it's base unit symbol.  This
	 * prevents units with names that start with one of the SI prefixes
	 * from being misinterpreted.  Save the replacement spot so that searches
	 * for prefixes don't run past this point */
	int nSiLen = 0, iOffset = 0, iReplace = -1;
	int nNameLen = strlen(pComp->sName);
	for(i = 0; i < NUM_SI_NAME; ++i){
		nSiLen = strlen(g_sSiName[i]);
		if(nNameLen < nSiLen) continue;
		iOffset = nNameLen - nSiLen;
		iReplace = -1;
		if( strcmp(pComp->sName + iOffset, g_sSiName[i] ) == 0){
			iReplace = iOffset;
		}
		else{
			if( (strncmp(pComp->sName + iOffset - 1, g_sSiName[i], nSiLen) == 0)&&
			  (pComp->sName[nSiLen - 1] = 's') )
				iReplace = iOffset -1;
		}
		
		if(iReplace > -1){
			memset(pComp->sName + iReplace, 0, nNameLen - iOffset);
			strcpy(pComp->sName + iReplace, g_sSiSymbol[i]);
			break;
		}
	}
	
	/* First search for metric prefix names.  This allows for weird stuff like 
	 * nanodays.  If the tail end of the string was replaced then don't compare
	 * past the iReplace index, we know this is not part of the prefix. */
	int nPreBytes = 0;
	double rExp = 1.0;
	char sBuf[_COMP_MAX_NAME] = {'\0'};
	bool bFoundSiPreName = false;
	if(iReplace != 0){   /* no prefix bytes present if iReplace == 0 */
		for(i = 0; i < NUM_SI_PREFIX; ++i){
			nPreBytes = strlen(g_sSiPreName[i]);
			
			/* If we know the size of the prefix and it ain't this length skip it */
			if((iReplace != -1)&&(nPreBytes != iReplace)) continue;
			
			/* Have to have a least 1 char for base unit*/
			if(nPreBytes > (nBytes - 1)) continue;
		
			if(strncmp(pComp->sName, g_sSiPreName[i], nPreBytes) == 0){
			
				/* remove SI prefix */
				strncpy(sBuf, pComp->sName + nPreBytes, _COMP_MAX_NAME - 1);
				memset(pComp->sName, 0, _COMP_MAX_NAME);
				strcpy(pComp->sName, sBuf);
			
				/* Calculate adjustment factor to move old values to these units */
				rExp = g_nSiPrePower[i] * pComp->nExpNum;
				rExp /= (double)(pComp->nExpDenom);
				rFactor = pow(10.0, rExp);
				bFoundSiPreName = true;
				break;
			}
		}
	}
	
	/* Now try to find metric prefix symbols. */
	int j = 0, nPreLen;
	bool bOkayReduceSiPre = false;
	if(! bFoundSiPreName){
		
		/* Test to see if it's okay to reduce metric prefix symbols.  We allow
		 * this if the tail of the string looks like a metric base symbol and the 
		 * rest of the string looks like a metric prefix.
		 * example using the word 'cats'
		 * 
		 *  cats -> tail matches 's', 
		 *  cat -> head matches nothing, don't allow reduction */
		for(i = 0; i < NUM_SI_NAME; ++i){
			nSiLen = strlen(g_sSiSymbol[i]);
			nNameLen = strlen(pComp->sName);
			if( (iOffset = nNameLen - nSiLen) == 0) continue;
			
			if( strcmp(pComp->sName + iOffset, g_sSiSymbol[i] ) == 0){
			
				for(j = 0; j < NUM_SI_PREFIX; ++j){
					nPreLen = strlen(g_sSiPreSym[j]);
					if( strncmp(pComp->sName, g_sSiPreSym[j], nPreLen) == 0){
						bOkayReduceSiPre = true;
						break;
					}
				}
			}
		}
		
		/* Now try metric prefix symbols (if allowed), if any are found reduce 
		 * to base unit */
		if(bOkayReduceSiPre ){
			for(i = 0; i < NUM_SI_PREFIX; ++i){
				nPreBytes = strlen(g_sSiPreSym[i]);
		
				/* Have to have a least 1 char for base unit*/
				if(nPreBytes > (nBytes - 1)) continue;
		
				if(strncmp(pComp->sName, g_sSiPreSym[i], nPreBytes) == 0){
			
					/* remove SI prefix */
					strncpy(sBuf, pComp->sName + nPreBytes, _COMP_MAX_NAME - 1);
					memset(pComp->sName, 0, _COMP_MAX_NAME);
					strcpy(pComp->sName, sBuf);
			
					/* Calculate adjustment factor to move old values to these units */
					rExp = g_nSiPrePower[i] * pComp->nExpNum;
					rExp /= (double)(pComp->nExpDenom);
					rFactor = pow(10.0, rExp);
					bFoundSiPreName = true;
					break;
				}
			}
		}
	}
	
	/* If I found an SI prefix name, it could have been applied to one of our 
	 * adhoc types, double check reduction for those before leaving */
	for(i = 0; i < NUM_ADHOC; ++i){
		if( strcasecmp(pComp->sName, g_sAdHocFrom[i]) == 0){
			strncpy(pComp->sName, g_sAdHocTo[i], _COMP_MAX_NAME);
			
			rFactor *= pow(
				g_rAdHocFactor[i],
				pComp->nExpNum / ((double)(pComp->nExpDenom))
			);
			return rFactor;
		}
	}	
	
	/* Lastly, do SINGLE Component SI decomposition.  If you want multi item
	 * decomposition, integrate UDUNITS2 into das2 */
	if(strcasecmp(pComp->sName, "Hz") == 0){
		pComp->nExpNum *= -1;
		pComp->sName[0] = 's'; pComp->sName[1] = '\0';
	}
	
	return rFactor;
}

double _Units_reduce(struct base_unit* pComp, int* pLen)
{	
	int i = 0;
	double rFactor = 1.0;
	
	/* Gather up the cumulative reduction factor */
	for(i = 0; i < *pLen; ++i) 
		rFactor *= _Units_reduceComp(pComp + i);
	
	/* Now put resulting components in canonical form */
	
	/* Sort items placing adjacent unit names together */
	qsort(pComp, *pLen, sizeof(struct base_unit), _Units_adjacentNames);
	
	/* Combine adjacent */
	i = 1;
	int j = 0;
	while(i < *pLen){
		/* Probably can't use no-case compare here, SI units are case specific */
		if(strcmp(pComp[i-1].sName, pComp[i].sName) == 0){
			
			_Units_accumPowers(pComp + i - 1, pComp + i);  /* Merge down exponents */
			
			for(j = i; j < (*pLen - 1); ++j)           /* Shift down components */
				_Units_compCopy(pComp + j, pComp + j + 1);
			
			*pLen = *pLen - 1;                         /* Reduce array length */
		}
		else{
			++i;
		}
	}
	
	/* Now sort using our positive first scheme */
	qsort(pComp, *pLen, sizeof(struct base_unit), _Units_positiveFirst);
	
	return rFactor;
}

UnitType Units_reduce(UnitType orig, double* pFactor){
	struct base_unit aComp[_MAX_NUM_COMP];
	memset(aComp, 0, sizeof(struct base_unit)*_MAX_NUM_COMP);
	
	int nComp = _Units_strToComponents(orig, aComp, _MAX_NUM_COMP);
	if(nComp < 0){
		das2_error(15, "Error reducing Unit type %s ", orig);	
		return orig;
	}
	
	*pFactor = _Units_reduce(aComp, &nComp);
	
	return _Units_fromCompAry(aComp, nComp);
}

/* ************************************************************************** */
/* Construct Unit singletons using strings  */

UnitType Units_fromStr(const char* string) 
{
	if(string == NULL)
		return UNIT_DIMENSIONLESS;
	
	char sBuf[_MAX_NUM_COMP*(_COMP_MAX_NAME+_COMP_MAX_EXP+3)] = {'\0'};
	strncpy(sBuf, string, _MAX_NUM_COMP*(_COMP_MAX_NAME+_COMP_MAX_EXP+3));
	
	/* Initialize our list if this is the first time */
	if(g_lUnits[0] == NULL){
		g_lUnits[0] = UNIT_US2000;
		g_lUnits[1] = UNIT_MJ1958;
		g_lUnits[2] = UNIT_T2000;
		g_lUnits[3] = UNIT_T1970;
		g_lUnits[4] = UNIT_UTC;
		g_lUnits[5] = UNIT_MILLISECONDS;
		g_lUnits[6] = UNIT_MICROSECONDS;
		g_lUnits[7] = UNIT_SECONDS;
		g_lUnits[8] = UNIT_HOURS;
		g_lUnits[9] = UNIT_DAYS;
		g_lUnits[10] = UNIT_HERTZ;
		g_lUnits[11] = UNIT_KILO_HERTZ;
		g_lUnits[12] = UNIT_MEGA_HERTZ;
		g_lUnits[13] = UNIT_E_SPECDENS;
		g_lUnits[14] = UNIT_B_SPECDENS;
		g_lUnits[15] = UNIT_NT;
		g_lUnits[16] = UNIT_NUMBER_DENS;
		g_lUnits[17] = UNIT_DB;
		g_lUnits[18] = UNIT_KM;
		g_lUnits[19] = UNIT_DIMENSIONLESS;
		g_lUnits[20] = NULL;
	}
	
	/* Hack in time unit collapse for now, make unit synonyms general later */
	if(strcasecmp(sBuf, "sec") == 0) return UNIT_SECONDS;
	if(strncasecmp(sBuf, "second", 6) == 0) return UNIT_SECONDS;
	if(strcasecmp(sBuf, "millisec") == 0) return UNIT_MILLISECONDS;
	if(strcasecmp(sBuf, "microsec") == 0) return UNIT_MICROSECONDS;
	if(strcasecmp(sBuf, "hertz") == 0) return UNIT_HERTZ;
	
	/* Decompose any old Latin-1 Supplemental microsign characters into lower case
	   greek mu.  */
	char* pFind = NULL;
	const char* sRep = "μ";
	if( (pFind = strstr(sBuf, "µ")) != NULL){ 
		pFind[0] = sRep[0]; pFind[1] = sRep[1]; 
	}
	
	/* Decompose any ohm sign characters into Greek capital omegas, since the
	 * ohm sign takes 3 bytes, but the capital omega takes 2 we have to shift
	 * down the bytes */
	sRep = "Ω";
	char* pChar = NULL;
	if( (pFind = strstr(sBuf, "Ω")) != NULL){
		pFind[0] = sRep[0]; pFind[1] = sRep[1];
		for(pChar = pFind +2; *pChar != '\0'; ++pChar) *pChar = *(pChar+1);
	}
	
	/* Since that's all the special characters we care about, don't worry about 
	 * general unicode decomposition */
	
	/* See if this exact unit string was hit before */
	int i = 0;
	for(i = 0; g_lUnits[i] != NULL && i < NUM_UNITS; i++)
		if(strcmp(sBuf, g_lUnits[i]) == 0) return g_lUnits[i];
		
	/* Well, can't avoid general unit parsing now.  See if the reduced form of
	 * any units are equivalent to the reduced form of these units */
	
	struct base_unit lComp[_MAX_NUM_COMP];
	int nComp = _Units_strToComponents(sBuf, lComp, _MAX_NUM_COMP);
	struct base_unit lReduced[_MAX_NUM_COMP];
	int nReduced = _Units_copy(lReduced, lComp, nComp);
	
	double rReduceFactor = _Units_reduce(lReduced, &nReduced);

	struct base_unit lOther[_MAX_NUM_COMP];
	int nOther = 0;
	double rOtherFactor = 0;
	for(i = 0; g_lUnits[i] != NULL && i < NUM_UNITS; ++i){
		
		if(Units_isInterval(g_lUnits[i])) continue;
		
		nOther = _Units_strToComponents(g_lUnits[i], lOther, _MAX_NUM_COMP);
		
		if(nOther != nReduced) continue;
		
		rOtherFactor = _Units_reduce(lOther, &nOther);
		if(rOtherFactor != rReduceFactor) continue;
		
		if( _Units_reducedEqual(lReduced, lOther, nOther)) return g_lUnits[i];
	}
	
	/* Nope, these are completely new, make new using string that preserves the
	 * original order */
	UnitType sOut = _Units_fromCompAry(lComp, nComp);
	return sOut;
}


/* ************************************************************************* */
/* Numeric Value Conversion */

/* General Rules:  Time point units convert, special built in conversions
 * work, and anything that is a pile of SI units can convert */
bool Units_canConvert(UnitType from, UnitType to )
{  
	if(to == NULL && from == NULL) return true;
	if(strcmp(from, to) == 0) return true;
	if(Units_haveCalRep(from) && Units_haveCalRep(to)) return true;
	
	struct base_unit aCompFrom[_MAX_NUM_COMP];
	memset(aCompFrom, 0, sizeof(struct base_unit)*_MAX_NUM_COMP);
	int nCompFrom = 0;
	struct base_unit aCompTo[_MAX_NUM_COMP];
	memset(aCompTo, 0, sizeof(struct base_unit)*_MAX_NUM_COMP);
	int nCompTo = 0;
	nCompFrom = _Units_strToComponents(from, aCompFrom, _MAX_NUM_COMP);
	if( nCompFrom < 0) return false;
	
	nCompTo = _Units_strToComponents(to, aCompTo, _MAX_NUM_COMP);
	if(nCompTo < 0) return false;
		
	_Units_reduce(aCompFrom, &nCompFrom);
	_Units_reduce(aCompTo, &nCompTo);
	
	if(nCompFrom != nCompTo) return false;
	
	/* Since these are in a canonical order, if two components have different
	   base units at any point, then they don't convert */
	for(int i = 0; i < nCompFrom; i++){
		if(strcasecmp(aCompFrom[i].sName, aCompTo[i].sName) != 0) return false;
	}
	
	return true;
}

/* Special conversion helpers */
double _Units_convertToUS2000( double value, UnitType fromUnits ) {
	if(strcmp(fromUnits, UNIT_US2000) == 0) return value;
	if(strcmp(fromUnits, UNIT_T2000) == 0)  return value * 1.0e6;
	if(strcmp(fromUnits, UNIT_MJ1958) == 0) return (value - 15340) * 86400 * 1e6;
	if(strcmp(fromUnits, UNIT_T1970) == 0 ) return ( value - 946684800 ) * 1e6;
	
	das2_error(15, "unsupported conversion to US2000 from %s\n", 
	           Units_toStr(fromUnits));
	return FILL_VALUE;
}

double _Units_convertFromUS2000( double value, UnitType toUnits ) {
    if ( strcmp(toUnits, UNIT_US2000) == 0 ) {
        return value;
    } else if ( strcmp(toUnits, UNIT_T2000) == 0 ) {
        return value / ( 1e6 );
    } else if ( strcmp(toUnits, UNIT_MJ1958) == 0 ) {
        return value / ( 86400 * 1e6 ) + 15340;
    } else if ( strcmp( toUnits, UNIT_T1970) == 0 ) {
        return value / 1e6 + 946684800;
    } else {
        das2_error( -1, "unsupported conversion from US2000 to %s\n",
				       Units_toStr( toUnits ) );   abort();
    }
	 return FILL_VALUE;
}


double Units_convertTo(UnitType to, double rFrom, UnitType from)
{
	if(to == NULL && from == NULL) return rFrom;
	if(strcmp(to, from) == 0) return rFrom;
	
	double rUs2k = 0.0, rTo = 0.0;
	if(Units_haveCalRep(to) && Units_haveCalRep(from)){
		rUs2k = _Units_convertToUS2000( rFrom, from );
		rTo = _Units_convertFromUS2000( rUs2k, to );
		
		return rTo; /* D2 */
	}
	
	struct base_unit aCompForm[_MAX_NUM_COMP];
	int nCompFrom = 0;
	memset(aCompForm, 0, sizeof(struct base_unit)*_MAX_NUM_COMP);
	struct base_unit aCompTo[_MAX_NUM_COMP];
	int nCompTo = 0;
	memset(aCompTo, 0, sizeof(struct base_unit)*_MAX_NUM_COMP);
	
	if( (nCompFrom = _Units_strToComponents(from, aCompForm, _MAX_NUM_COMP))  < 0)
		goto Units_convertTo_Error;
	if( (nCompTo = _Units_strToComponents(to, aCompTo, _MAX_NUM_COMP))  < 0)
		goto Units_convertTo_Error;
	
	double rFactorForm, rFactorTo;
	
	rFactorForm = _Units_reduce(aCompForm, &nCompFrom);
	rFactorTo = _Units_reduce(aCompTo, &nCompTo);
	
	if(nCompFrom != nCompTo) goto Units_convertTo_Error;
	
	/* Since these are in a canonical order, if two components have different
	   base units at any point, then they don't convert */
	for(int i = 0; i < nCompFrom; i++)
		if(strcasecmp(aCompForm[i].sName, aCompTo[i].sName) != 0) 
			goto Units_convertTo_Error;
	
	rTo = rFrom * (rFactorForm / rFactorTo);
	
	return rTo; /* ... D2 */
	
Units_convertTo_Error:
	das2_error(15, "Unit types %s and %s are not convertible.", to, from);	
	return FILL_VALUE;
}


/* ************************************************************************* */
/* Epoch Times to Calendar Times */

/* Note, no time parsing is included in this module, that is the job of the
   parsetime.c file and it's public interface */

double Units_secondsSinceMidnight( double rVal, UnitType epoch_units ) {
    double xx= Units_convertTo( UNIT_T2000, rVal, epoch_units );
    double result;
    if (xx<0) {
        xx= fmod( xx, 86400 );
        if (xx==0) {
            result= 0;
        } else {
            result= 86400+xx;
        }
    } else {
        result= fmod( xx, 86400 );
    }
    return result;
}

int Units_getJulianDay( double time, UnitType units ) {
    double xx= Units_convertTo( UNIT_MJ1958, time, units );
    return (int)floor( xx ) + 2436205;
}


void Units_convertToDt(das_time_t* pDt, double value, UnitType epoch_units)
{
	dt_null(pDt);
	
	int julian = Units_getJulianDay(value, epoch_units);
	
	/* Break the Julian day apart into month, day year.  This is based on
    * http://en.wikipedia.org/wiki/Julian_day (GNU Public License)
	 */
	int j = julian + 32044;
	int g = j / 146097;
	int dg = j % 146097;
	int c = (dg / 36524 + 1) * 3 / 4;
	int dc = dg - c * 36524;
	int b = dc / 1461;
	int db = dc % 1461;
	int a = (db / 365 + 1) * 3 / 4;
	int da = db - a * 365;
	int y = g * 400 + c * 100 + b * 4 + a;
	int m = (da * 5 + 308) / 153 - 2;
	int d = da - (m + 4) * 153 / 5 + 122;
	int Y = y - 4800 + (m + 2) / 12;
	int M = (m + 2) % 12 + 1;
	int D = d + 1;
	
	
	double seconds = Units_secondsSinceMidnight(value, epoch_units);
	
	int hour = (int)(seconds/3600.0);
	int minute = (int)((seconds - hour*3600.0)/60.0);
	double justSeconds = seconds - hour*(double)3600.0 - minute*(double)60.0;

	
   pDt->year   = Y;
   pDt->month  = M;
	pDt->mday   = D;
	pDt->hour   = hour;
	pDt->minute = minute;
	pDt->second = justSeconds;
	
	dt_tnorm(pDt);
}

double Units_convertFromDt(UnitType epoch_units, const das_time_t* pDt)
{
	double mj1958 = 0.0;
	
	int jd = 367 * pDt->year - 7 * (pDt->year + (pDt->month + 9) / 12) / 4 -
	         3 * ((pDt->year + (pDt->month - 9) / 7) / 100 + 1) / 4 +
	         275 * pDt->month / 9 + pDt->mday + 1721029;

	double ssm = pDt->second + pDt->hour*3600.0 + pDt->minute*60.0;
	
	if( strcmp(epoch_units, UNIT_MJ1958) == 0 ) {
		mj1958 = ( jd - 2436205. ) + ssm / 86400.;
		return mj1958;
	} 
	
	double us2000= ( jd - 2451545 ) * 86400000000. + ssm * 1000000;
	
	if( strcmp(epoch_units, UNIT_US2000) == 0 ) return us2000;
	 
	return _Units_convertFromUS2000(us2000, epoch_units);
}
