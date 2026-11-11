/* Copyright (C) 2017 Chris Piker <chris-piker@uiowa.edu>
 *
 * This file is part of das2C, the Core Das C Library.
 * 
 * das2C is free software; you can redistribute it and/or modify it under
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

#include <locale.h>
#include <string.h>
#include <inttypes.h>
#include <ctype.h>
#include <assert.h>

#include "time.h"
#include "datum.h"
#include "array.h"
#include "array_p.h"
#include "util.h"
#include "str.h"
#include "log.h"
#include "info_p.h"

/* If this were D code it would use SumType and be about 10 lines long :-)
   ...and have so many automatic features it would be hard to understand  :-( 
 */

/* ************************************************************************* */
/* Datum functions and structures */

void DasDatum_set(
   DasDatum* pThis, const uint8_t* pSrc, das_vt vt, uint32_t vsize, 
   DasUnits units
){
	/* Null holds nothing, whatever pSrc points at */
	if(vt == DASVT_NULL){
		DasDatum_setNull(pThis);
		return;
	}

	pThis->vt = vt;
	pThis->units = (units != NULL) ? units : DASUNITS_DIMENSIONLESS;

	switch(vt){
	case DASVT_UNK:
	case DASVT_TEXT:
		memcpy(pThis->bytes, pSrc, sizeof(void*));
		pThis->vsize = vsize;
		break;
	/* pointer AND length: copying only the pointer leaves a garbage size that
	   nothing downstream can detect */
	case DASVT_BYTESEQ:
		memcpy(pThis->bytes, pSrc, sizeof(DasCByteSeq));
		pThis->vsize = vsize;
		break;
	default:
		pThis->vsize = dasvt_size(vt);
		assert(pThis->vsize <= DASDATUM_BUF_SZ);
		memcpy(pThis->bytes, pSrc, pThis->vsize);
		break;
	}
}

void DasDatum_setNull(DasDatum* pThis)
{
	memset(pThis, 0, sizeof(DasDatum));
	pThis->vt = DASVT_NULL;
	pThis->units = DASUNITS_DIMENSIONLESS;
}

bool DasDatum_isNull(const DasDatum* pThis)
{
	if(pThis->vt != DASVT_NULL) return false;

#ifndef NDEBUG
	/* Zeroed memory and DasDatum_setNull() both pass.  Anything else typed
	   DASVT_NULL was built by hand. */
	bool bZero = (pThis->vsize == 0);
	for(int i = 0; bZero && (i < DASDATUM_BUF_SZ); ++i)
		bZero = (pThis->bytes[i] == 0);
	if(!bZero)
		daslog_debug("inconsistent null datum");
#endif

	return true;
}


bool DasDatum_fromDouble(DasDatum* pDatum, double value, DasUnits units)
{
	memset(pDatum, 0, sizeof(DasDatum));
	pDatum->vt = DASVT_DOUBLE;
	pDatum->vsize = dasvt_size(DASVT_DOUBLE);
	memcpy(pDatum->bytes, &value, sizeof(double));
	pDatum->units = (units != NULL) ? units : DASUNITS_DIMENSIONLESS;
	return true;
}

/* Initialize a small structure using a string, works for any string 
   but might not give you the results you expected */
bool DasDatum_fromStr(DasDatum* pDatum, const char* sStr)
{
	char sBuf[128] = {'\0'};
	DasTime dt = {0};

	DasDatum_setNull(pDatum);   /* what a failed parse leaves behind */

	const char* pRead = sStr;
	const char* pAhead = sStr + 1;
	
	struct lconv* pLocale = localeconv();
	char cDecPt = pLocale->decimal_point[0];  /* Not generically UTF-8 safe, but
															 * will handle French convention */
	/* Nothing but whitespace is the null datum, the inverse of how null prints */
	while(isspace((unsigned char)*pRead)) ++pRead;
	if(*pRead == '\0'){
		DasDatum_setNull(pDatum);
		return true;
	}
	pRead = sStr;

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
		if(DasTime_fromStr(&dt, sBuf) ){ 
			pDatum->units = DASUNITS_UTC;
			memcpy(pDatum->bytes, &dt, sizeof(DasTime));
			pDatum->vt = DASVT_TIME;
			return true;
		}
	}
	
	/* Try classic datum, double value plus units */
	size_t uCopy = pRead - sStr < 127 ? pRead - sStr : 127;
	
	strncpy(sBuf, sStr, uCopy);
	if( ! dasstr_toDouble(sBuf, (double*)pDatum->bytes )){
		DasDatum_setNull(pDatum);   /* the attempt may have left bytes behind */
		return false;
	}
	pDatum->vt = DASVT_DOUBLE;
	
	if(*pRead == '\0'){
		pDatum->units = DASUNITS_DIMENSIONLESS;
		return true;
	}
		
	/* The rest is the units.  Hand it over whole: a copy cut to fit a buffer
	   could parse as different units than were written. */
	pDatum->units = DasUnits_fromStr(pRead);
	if(pDatum->units == NULL){
		DasDatum_setNull(pDatum);
		return false;
	}

	return true;
}


/** A run of numeric elements plus the formalism that says what they mean.
 *
 * The caller owns the memory.  pRun, and pShape when it is used, point at
 * storage the caller supplied, so a composite datum is good for exactly as
 * long as that storage is.  Nothing here is copied and nothing is owned.
 *
 * A string or byte run is not one of these; those are DASVT_TEXT and DASVT_BYTESEQ,
 * which have their own boxes.  A composite is always a run of numeric elements.
 */
typedef struct das_composite_t {

	/* FIRST, so the cast-to-your-type idiom still works */
	const uint8_t* pRun;

	/* What the run means.  Never NULL: an unrecognized kind still gets a
	   generic form, so there is no "composite with no formalism" state. */
	const struct das_form_t* pForm;

	/* Only when DASCOMP_BIGSHAPE is set, NULL otherwise.  Small shapes ride in
	   uInfo so the common vector carries no second buffer to keep alive. */
	const ptrdiff_t* pShape;

	/* bits  0-3   rank, 0 to DASIDX_MAX
	   bits  4-7   DASCOMP_* flags
	   bits  8-15  das_vt of one element, a full byte so the enum can grow
	   bits 16-63  three 16-bit extents, valid only without DASCOMP_BIGSHAPE

	   One uint64_t read with masks rather than a bitfield: bitfield layout is
	   implementation defined, and this has to read the same on gcc, MSVC and
	   emscripten. */
	uint64_t uInfo;

} das_composite;

/* Extents live behind pShape rather than in uInfo: rank above
   DASCOMP_INLINE_RANK, an extent over DASCOMP_INLINE_MAX, or a ragged run
   resolved at one location. */
#define DASCOMP_BIGSHAPE 0x10

#define DASCOMP_RANK(U)  ((int)((U) & 0xF))
#define DASCOMP_ET(U)    ((das_vt)(((U) >> 8) & 0xFF))
#define DASCOMP_EXT(U,I) ((ptrdiff_t)(((U) >> (16 + 16*(I))) & 0xFFFF))

#define DASCOMP_INLINE_RANK 3
#define DASCOMP_INLINE_MAX  0xFFFF

/* The box rides inside a DasDatum's byte array, so it has to fit.  32 bytes
   on a 64-bit host and 24 on wasm32 where pointers are four; nothing should
   assume which, only that it fits. */
typedef char _das_composite_fits[
	(sizeof(das_composite) <= DASDATUM_BUF_SZ) ? 1 : -1
];

/* Declared here rather than by including form.h: form.h already includes
   datum.h because pack() hands one back, so including it back would close a
   cycle.  One extern beats a function pointer in every datum. */
extern char* DasForm_valToStr(
	const struct das_form_t* pThis, const uint8_t* pRun, uint32_t nElems,
	das_vt et, char* sBuf, size_t uLen
);

bool DasDatum_wrapElems(
	DasDatum* pThis, const struct das_form_t* pForm, const uint8_t* pRun,
	int nRank, const ptrdiff_t* pShape, das_vt et, DasUnits units
){
	if((pThis == NULL)||(pForm == NULL)||(pRun == NULL))
		return daserr_falsef(DASERR_DATUM, "Null pointer boxing a composite");

	if((nRank < 0)||(nRank > DASIDX_MAX))
		return daserr_falsef(DASERR_DATUM, "Invalid composite rank %d", nRank);

	if((nRank > 0)&&(pShape == NULL))
		return daserr_falsef(DASERR_DATUM,
			"A rank %d composite needs extents", nRank
		);

	/* Inline when it fits.  A ragged extent never fits: it is negative, and a
	   caller reading it back has to see the real value, not a truncation. */
	bool bBig = (nRank > DASCOMP_INLINE_RANK);
	if(!bBig)
		for(int i = 0; i < nRank; ++i)
			if((pShape[i] < 0)||(pShape[i] > DASCOMP_INLINE_MAX)){ bBig = true; break; }

	das_composite* pC = (das_composite*)pThis;
	pC->pRun   = pRun;
	pC->pForm  = pForm;
	pC->pShape = bBig ? pShape : NULL;
	pC->uInfo  = ((uint64_t)nRank & 0xF) | (((uint64_t)et & 0xFF) << 8);

	if(bBig)
		pC->uInfo |= DASCOMP_BIGSHAPE;
	else
		for(int i = 0; i < nRank; ++i)
			pC->uInfo |= ((uint64_t)pShape[i] & 0xFFFF) << (16 + 16*i);

	pThis->vt    = DASVT_COMP;
	pThis->vsize = sizeof(das_composite);
	pThis->units = (units != NULL) ? units : DASUNITS_DIMENSIONLESS;
	return true;
}

const struct das_form_t* DasDatum_form(const DasDatum* pThis)
{
	if(pThis->vt != DASVT_COMP) return NULL;
	return ((const das_composite*)pThis)->pForm;
}

const uint8_t* DasDatum_elems(const DasDatum* pThis)
{
	if(pThis->vt != DASVT_COMP) return NULL;
	return ((const das_composite*)pThis)->pRun;
}

int DasDatum_shape(const DasDatum* pThis, ptrdiff_t* pShape)
{
	switch(pThis->vt){

	case DASVT_COMP: {
		const das_composite* pC = (const das_composite*)pThis;
		int nRank = DASCOMP_RANK(pC->uInfo);
		if(pShape != NULL){
			if(pC->uInfo & DASCOMP_BIGSHAPE)
				memcpy(pShape, pC->pShape, (size_t)nRank * sizeof(ptrdiff_t));
			else
				for(int i = 0; i < nRank; ++i)
					pShape[i] = DASCOMP_EXT(pC->uInfo, i);
		}
		return nRank;
	}

	/* The datum boxes the address, so the string is one dereference away.
	   Exactly what strlen reports: the NUL that DASARY_AS_STRING puts in the
	   array is das2C's storage choice, not part of the encoded value. */
	case DASVT_TEXT: {
		const char* sVal = *((const char* const*)pThis);
		if(pShape != NULL)
			pShape[0] = (sVal == NULL) ? 0 : (ptrdiff_t)strlen(sVal);
		return 1;
	}

	/* A DasCByteSeq rides inline, pointer and length together */
	case DASVT_BYTESEQ:
		if(pShape != NULL)
			pShape[0] = (ptrdiff_t)((const DasCByteSeq*)pThis)->size;
		return 1;

	default:
		return 0;
	}
}

size_t DasDatum_numElems(const DasDatum* pThis)
{
	ptrdiff_t aShape[DASIDX_MAX];
	int nRank = DasDatum_shape(pThis, aShape);

	size_t uElems = 1;
	for(int i = 0; i < nRank; ++i){
		if(aShape[i] < 0) return 0;      /* ragged, no fixed count */
		uElems *= (size_t)aShape[i];
	}
	return uElems;
}

size_t DasDatum_elemsBytes(const DasDatum* pThis)
{
	if(pThis->vt != DASVT_COMP) return 0;

	size_t uElems = DasDatum_numElems(pThis);
	if(uElems == 0) return 0;
	return uElems * dasvt_size(DASCOMP_ET(((const das_composite*)pThis)->uInfo));
}

das_vt DasDatum_et(const DasDatum* pThis)
{
	switch(pThis->vt){
	case DASVT_TEXT: return DASVT_UBYTE;
	case DASVT_BYTESEQ: return DASVT_UBYTE;
	case DASVT_COMP: return DASCOMP_ET(((const das_composite*)pThis)->uInfo);
	default:  return pThis->vt;
	}
}

/* This one is simple */
/* Argument order matches DasDatum_fromDouble and _byteSeq: value, then units.
   DasUnits is a const char*, so a swap here compiles clean and silently
   trades the two -- the header is the contract. */
bool DasDatum_wrapStr(DasDatum* pThis, const char* sStr, DasUnits units)
{
	/* Careful, we are copying an address, not a value at an address*/
	memcpy(pThis->bytes, &sStr,  sizeof(const char*));
	pThis->vt = DASVT_TEXT;
	pThis->units = (units != NULL) ? units : DASUNITS_DIMENSIONLESS;
	pThis->vsize = sizeof(const char*);
	return true;
}

bool DasDatum_wrapBytes(
	DasDatum* pDatum, DasCByteSeq seq, DasUnits units
){
	memcpy(pDatum->bytes, &seq, sizeof(DasCByteSeq));
	pDatum->vt = DASVT_BYTESEQ;
	pDatum->vsize = sizeof(DasCByteSeq);
	pDatum->units = (units != NULL) ? units : DASUNITS_DIMENSIONLESS;
	return true;
}

int DasDatum_toDoubles(const DasDatum* pThis, double* pOut, int nMax)
{
	if((pThis == NULL)||(pOut == NULL)||(nMax < 1))
		return -1 * daserrf(DASERR_DATUM, "Invalid inputs to DasDatum_toDoubles");

	/* A scalar is the one-component case of the same question, so a caller
	   holding a datum of unknown class does not have to test for composite
	   before it can ask. */
	if(pThis->vt != DASVT_COMP){
		if(!DasDatum_toDouble(pThis, pOut)) return -1 * DASERR_DATUM;
		return 1;
	}

	const uint8_t* pRun = DasDatum_elems(pThis);
	if(pRun == NULL)
		return -1 * daserrf(DASERR_DATUM, "Datum carries no component run");

	int nElems = (int)DasDatum_numElems(pThis);
	das_vt et = DasDatum_et(pThis);

	/* Short runs are legal.  A stream may ship fewer components than its
	   system defines, so slots past the count are left EXACTLY as the caller
	   set them; what an absent component means belongs to the formalism, and
	   dasvecsys_dirDefault() is where that answer lives. */
	int n = (nElems < nMax) ? nElems : nMax;

	/* One type test for the whole item rather than one per component.  These
	   two arms carry most real traffic and neither can fail, so the general
	   path below is left for the storage types that need checking. */
	switch(et){
	case DASVT_DOUBLE:
		memcpy(pOut, pRun, (size_t)n * sizeof(double));
		return n;
	case DASVT_FLOAT: {
		const float* pF = (const float*)pRun;
		for(int i = 0; i < n; ++i) pOut[i] = pF[i];
		return n;
	}
	default: break;
	}

	size_t uSz = dasvt_size(et);
	for(int i = 0; i < n; ++i){
		if(dasval_convert(et, pRun + i*uSz, NULL, DASVT_DOUBLE,
		                      (uint8_t*)(pOut + i), NULL, 0) != DAS_OKAY)
			return -1 * daserrf(DASERR_DATUM, "Bad component %d", i);
	}
	return n;
}

bool DasDatum_toDouble(const DasDatum* pThis, double* pOut)
{
	if((pThis == NULL)||(pOut == NULL))
		return daserr_falsef(DASERR_DATUM, "Invalid inputs to DasDatum_toDouble");

	switch(pThis->vt){
	case DASVT_NULL:
		return daserr_falsef(DASERR_DATUM, "A null datum has no numeric value");
	case DASVT_UBYTE:  *pOut = *((uint8_t*)pThis);    return true;
	case DASVT_BYTE:   *pOut = *((int8_t*)pThis);   return true;
	case DASVT_USHORT: *pOut = *((uint16_t*)pThis); return true;
	case DASVT_SHORT:  *pOut = *((int16_t*)pThis);  return true;
	case DASVT_UINT:   *pOut = *((uint32_t*)pThis); return true;
	case DASVT_INT:    *pOut = *((int32_t*)pThis);  return true;
	case DASVT_ULONG:  *pOut = *((uint64_t*)pThis); return true;
	case DASVT_LONG:   *pOut = *((int64_t*)pThis);  return true;
	case DASVT_FLOAT:  *pOut = *((float*)pThis);    return true;
	case DASVT_DOUBLE: *pOut = *((double*)pThis);   return true;

	/* Attempt string conversion */
	case DASVT_TEXT:
		if(!dasstr_toDouble((const char*)pThis, pOut))
			return daserr_falsef(DASERR_DATUM, "Couldn't convert %s to a double",
			                       (const char*)pThis);
		return true;

	default: break;
	}

	/* DASVT_TIME lands here on purpose.  A calendar time has no reading as a plain
	   double until an epoch is named, which is DasDatum_toPoint()'s job. */
	return daserr_falsef(DASERR_DATUM,
		"Don't know how to convert items of type %s to doubles.",
		dasvt_name(pThis->vt)
	);
}

/* Like toDbl but cares about scale and epoch */
bool DasDatum_toPoint(
	const DasDatum* pThis, DasUnits epoch, double* pResult
){
	if(!DasUnits_isTimePoint(epoch) || (epoch == DASUNITS_UTC)) return false;
	if(pThis->vt == DASVT_NULL)
		return daserr_falsef(DASERR_DATUM, "A null datum has no time value");

	DasTime dt;
	
	if(pThis->vt == DASVT_TIME){ 
		*pResult = DasTime_toPoint((DasTime*)pThis, epoch);
		return (*pResult != DASVAL_FILL);
	}
	
	/* text is interesting, because it could be 2017-01-01 or something
	   like "2.37455" which are handle very differently */
	
	double rDbl = 0.0;
	if(pThis->vt == DASVT_TEXT){
		if( DasTime_fromStr(&dt, (const char*)pThis)){
			return DasTime_toPoint(&dt, epoch);
		}
		else{
			/* parsetime failed, try to convert as an ASCII real */
			if( ! dasstr_toDouble((const char*)pThis, &rDbl)) return false;
			
			/* have a real, see if I'm in non UTC epoch units */
			if(!DasUnits_isTimePoint(pThis->units) || (pThis->units == DASUNITS_UTC)) 
				return false;
			
			*pResult = DasUnits_convert(rDbl, pThis->units, epoch);
			return (*pResult != DASVAL_FILL);
		}
	}
	
	/* for the rest, I have to have an epoch of my own or I don't 
	   know where zero is at */
	if(!DasUnits_isTimePoint(pThis->units) || (pThis->units == DASUNITS_UTC)) 
		return false;

	switch(pThis->vt){
	case DASVT_UBYTE:  rDbl = *((uint8_t*)pThis); break;
	case DASVT_BYTE:   rDbl = *((int8_t*)pThis); break;
	case DASVT_USHORT: rDbl = *((uint16_t*)pThis); break;
	case DASVT_SHORT:  rDbl = *((int16_t*)pThis); break;
	case DASVT_UINT:   rDbl = *((uint32_t*)pThis); break;
	case DASVT_INT:    rDbl = *((int32_t*)pThis); break;
	case DASVT_ULONG:  rDbl = *((uint64_t*)pThis); break;
	case DASVT_LONG:   rDbl = *((int64_t*)pThis); break;
	case DASVT_FLOAT:  rDbl = *((float*)pThis); break;
	case DASVT_DOUBLE: rDbl = *((double*)pThis); break;
	default:
		daserrf(DASERR_DATUM, "Don't know how to convert items of type %s"
		          " to epoch times", dasvt_name(pThis->vt));
		return false;
	}
	
	*pResult = DasUnits_convert(rDbl, pThis->units, epoch);
	return (*pResult != DASVAL_FILL);
}

bool DasDatum_toTime(const DasDatum* pThis, DasTime* pDt)
{
	if(pThis->vt == DASVT_NULL)
		return daserr_falsef(DASERR_DATUM, "A null datum has no time value");

	if(pThis->vt == DASVT_TIME){
		memcpy(pDt, pThis, sizeof(DasTime));
		return true;
	}
	if(pThis->vt == DASVT_TEXT)
		return DasTime_fromStr(pDt, (const char*)pThis);
	
	if(!DasUnits_isTimePoint(pThis->units) || (pThis->units == DASUNITS_UTC)) 
		return false;

	/* Special case for TT2000 long integers, need to preserve resolution */
	if((pThis->vt == DASVT_LONG)&&(pThis->units == DASUNITS_TT2000)){
		DasTime_fromTt2k(pDt, *((int64_t*)pThis) );
		return true;
	}

	double rDbl = 0.0;
	switch(pThis->vt){
	case DASVT_UBYTE:  rDbl = *((uint8_t*)pThis); break;
	case DASVT_BYTE:   rDbl = *((int8_t*)pThis); break;
	case DASVT_USHORT: rDbl = *((uint16_t*)pThis); break;
	case DASVT_SHORT:  rDbl = *((int16_t*)pThis); break;
	case DASVT_UINT:   rDbl = *((uint32_t*)pThis); break;
	case DASVT_INT:    rDbl = *((int32_t*)pThis); break;
	case DASVT_ULONG:  rDbl = *((uint64_t*)pThis); break;
	case DASVT_LONG:   rDbl = *((int64_t*)pThis); break;
	case DASVT_FLOAT:  rDbl = *((float*)pThis); break;
	case DASVT_DOUBLE: rDbl = *((double*)pThis); break;
	default:
		daserrf(DASERR_DATUM, "Don't know how to convert items of type %s"
		          " to epoch times", dasvt_name(pThis->vt));
		return false;
	}

	return (DasTime_fromPoint(pDt, rDbl, pThis->units) == DAS_OKAY);
}

/* Rendering reads both yday and month/mday, and yday is output only in a
   DasTime, so normalize to recompute it.  DasTime_normalize() would roll a leap
   second into the next minute, hold :59 while it runs. */
bool DasDatum_fromTime(DasDatum* pThis, const DasTime* pDt)
{
	if(pDt == NULL){
		DasDatum_setNull(pThis);
		return daserr_falsef(DASERR_DATUM, "No DasTime to make a datum from");
	}

	DasTime dt = *pDt;
	double rSec = dt.second;
	bool bLeap = (rSec >= 60.0) && (rSec < 61.0);
	if(bLeap) dt.second = 59.0;
	DasTime_normalize(&dt);
	if(bLeap) dt.second = rSec;

	DasDatum_set(pThis, (const uint8_t*)&dt, DASVT_TIME, sizeof(DasTime), DASUNITS_UTC);
	return true;
}

	
/* Write a datum out as a string, straight-forward, no type conversion.
   Returns the running length as the info_p.h helpers count it; anything
   at or past uLen means the text was cut off, which is all the public
   wrappers need to know. */
static int _das_datum_prn(
	const DasDatum* pThis, char* sBuf, size_t uLen, int nFracDigits,
	bool bPrnUnits, const char* sSep
){
	if(uLen > 0) sBuf[0] = '\0';

	/* Null prints as nothing, units included, so it parses back to null */
	if(pThis->vt == DASVT_NULL) return 0;

	char sFmt[32] = {'\0'};
	int n = 0;

	switch(pThis->vt){

	case DASVT_BYTE:   n = _prn_catf(sBuf, uLen, n, "%hhd", *((int8_t*)pThis));   break;
	case DASVT_UBYTE:  n = _prn_catf(sBuf, uLen, n, "%hhu", *((uint8_t*)pThis));  break;
	case DASVT_USHORT: n = _prn_catf(sBuf, uLen, n, "%hu",  *((uint16_t*)pThis)); break;
	case DASVT_SHORT:  n = _prn_catf(sBuf, uLen, n, "%hd",  *((int16_t*)pThis));  break;
	case DASVT_UINT:   n = _prn_catf(sBuf, uLen, n, "%u",   *((uint32_t*)pThis)); break;
	case DASVT_INT:    n = _prn_catf(sBuf, uLen, n, "%d",   *((int32_t*)pThis));  break;

	/* PRIu64/PRId64 are the portable int64 codes from inttypes.h */
	case DASVT_ULONG:  n = _prn_catf(sBuf, uLen, n, "%" PRIu64, *((uint64_t*)pThis)); break;
	case DASVT_LONG:   n = _prn_catf(sBuf, uLen, n, "%" PRId64, *((int64_t*)pThis));  break;
		
	case DASVT_FLOAT:
		/* A negative digit count means "no guidance, pick a default".  Use a
		   round-trippable width (9 sig figs covers a 32-bit float) rather than
		   pasting the negative straight into the format and emitting garbage. */
		snprintf(sFmt, 31, "%%.%de", (nFracDigits < 0) ? 8 : nFracDigits);
		n = _prn_catf(sBuf, uLen, n, sFmt, *((float*)pThis));
		break;

	case DASVT_DOUBLE:
		/* Likewise, 17 sig figs round-trips a 64-bit double exactly. */
		snprintf(sFmt, 31, "%%.%de", (nFracDigits < 0) ? 16 : nFracDigits);
		n = _prn_catf(sBuf, uLen, n, sFmt, *((double*)pThis));
		break;
		
	case DASVT_TIME: {
		char sTime[64] = {'\0'};
		DasTime_toIsoC((const DasTime*)pThis, nFracDigits, sTime, sizeof(sTime));
		n = _prn_catf(sBuf, uLen, n, "%s", sTime);
		break;
	}
		
	case DASVT_TEXT: {
		const char* sText = *((const char**)pThis);
		n = _prn_catf(sBuf, uLen, n, "%s", (sText != NULL) ? sText : "");
		break;
	}
		
	case DASVT_BYTESEQ: {
		/* Two hex digits per byte with sSep between.  Only whole bytes are
		   written, a reader can't use half of one, so writing stops at the
		   first pair that doesn't fit while counting goes on. */
		const DasCByteSeq* pBs = (DasCByteSeq*)pThis;
		bool bStop = false;
		for(size_t u = 0; u < pBs->size; ++u){
			const char* sPre = (u > 0) ? sSep : "";
			int nPair = (int)strlen(sPre) + 2;
			if(!bStop && ((size_t)(n + nPair) < uLen))
				snprintf(sBuf + n, uLen - (size_t)n, "%s%02hhX", sPre, 
				         ((const uint8_t*)pBs->data)[u]);
			else
				bStop = true;
			n += nPair;
		}
		break;
	}
	
	case DASVT_COMP: {
		const das_composite* pC = (const das_composite*)pThis;
		size_t uElems = DasDatum_numElems(pThis);

		/* The formalism renders if it has an opinion.  It usually does, and it
		   is the only thing that knows a run of three is a vector rather than
		   three unrelated numbers.  It clips rather than counts, so a full
		   buffer is taken as cut off. */
		if((uLen > 0) && (DasForm_valToStr(
			pC->pForm, pC->pRun, (uint32_t)uElems, DASCOMP_ET(pC->uInfo),
			sBuf, uLen
		) != NULL)){
			n = (int)strlen(sBuf);
			if((size_t)n + 1 >= uLen) n = (int)uLen;
			break;
		}

		/* Otherwise the elements, separated and bracketed.  Enough to read, and
		   it needs nothing from the form layer. */
		das_vt etElem = DASCOMP_ET(pC->uInfo);
		size_t uElemSz = dasvt_size(etElem);
		n = _prn_catf(sBuf, uLen, n, "[");
		for(size_t v = 0; v < uElems; ++v){
			DasDatum dmElem;
			DasDatum_set(
				&dmElem, pC->pRun + v*uElemSz, etElem, (uint32_t)uElemSz,
				pThis->units
			);
			if(v > 0) n = _prn_catf(sBuf, uLen, n, "%s", sSep);
			n += _das_datum_prn(&dmElem, _prn_at(sBuf, uLen, n), _prn_rem(uLen, n),
			                    nFracDigits, false, sSep);
		}
		n = _prn_catf(sBuf, uLen, n, "]");
		break;
	}

	case DASVT_INDEX: {
		const das_idx_info* pInfo = (const das_idx_info*)pThis;
		n = _prn_catf(sBuf, uLen, n, "Offset:%zd%sCount:%zu", pInfo->nOffset, 
		              sSep, pInfo->uCount);
		break;
	}

	default:
		n = _prn_catf(sBuf, uLen, n, "UNKNOWN");
		break;
	}

	/* Write the units */
	if((pThis->units != DASUNITS_DIMENSIONLESS)&&(bPrnUnits))
		n = _prn_catf(sBuf, uLen, n, " %s", DasUnits_toStr(pThis->units));
		
	return n;
}

char* DasDatum_toStr(
	const DasDatum* pThis, int nFracDigits, char* sStr, size_t uLen
){
	return _prn_fit(sStr, uLen, 
		_das_datum_prn(pThis, sStr, uLen, nFracDigits, true, ";")
	);
}

char* DasDatum_toValStr(
	const DasDatum* pThis, int nFracDigits, char* sStr, size_t uLen
){
	return _prn_fit(sStr, uLen,
		_das_datum_prn(pThis, sStr, uLen, nFracDigits, false, ";")
	);
}

char* DasDatum_toValStrSep(
	const DasDatum* pThis, int nFracDigits, const char* sSep, char* sStr, 
	size_t uLen
){
	return _prn_fit(sStr, uLen,
		_das_datum_prn(pThis, sStr, uLen, nFracDigits, false, sSep)
	);
}

/* ************************************************************************* */
/* Ranges which support +/- infinity */

void DasRng_setOpen(DasRng* pRng)
{
	DasDatum_setNull(&pRng->begin);
	DasDatum_setNull(&pRng->end);
}

bool DasRng_hasBeg(const DasRng* pRng)
{
	return !DasDatum_isNull(&pRng->begin);
}

bool DasRng_hasEnd(const DasRng* pRng)
{
	return !DasDatum_isNull(&pRng->end);
}

/* One end of a time range from text: "" stays open, anything else must be
   a time.  A plain number would make a "time" range consumers ignore. */
static das_errcode _das_datum_utc_edge(DasDatum* pDm, const char* sVal, const char* sWhich)
{
	if(!DasDatum_fromStr(pDm, sVal))
		return daserrf(DASERR_DATUM,
			"DasRng_fromIso: cannot parse %s time '%s'", sWhich, sVal);
	if(DasDatum_isNull(pDm) || (pDm->vt == DASVT_TIME))
		return DAS_OKAY;

	DasDatum_setNull(pDm);
	return daserrf(DASERR_DATUM,
		"DasRng_fromIso: %s '%s' is not a time", sWhich, sVal);
}

das_errcode DasRng_fromIso(DasRng* pRng, const char* sBeg, const char* sEnd)
{
	DasRng_setOpen(pRng);
	das_errcode nRet = _das_datum_utc_edge(&pRng->begin, sBeg, "begin");
	if(nRet != DAS_OKAY) return nRet;
	return _das_datum_utc_edge(&pRng->end, sEnd, "end");
}

void DasRng_fromTime(
	DasRng* pRng, const DasTime* dtBeg, const DasTime* dtEnd
){
	/* DasRng_setOpen() leaves both ends null, a NULL DasTime keeps one open */
	DasRng_setOpen(pRng);
	if(dtBeg != NULL) DasDatum_fromTime(&pRng->begin, dtBeg);
	if(dtEnd != NULL) DasDatum_fromTime(&pRng->end, dtEnd);
}

void DasRng_fromInt(DasRng* pRng, int64_t nBeg, int64_t nEnd)
{
	DasDatum_fromDouble(&pRng->begin, (double)nBeg, DASUNITS_DIMENSIONLESS);
	DasDatum_fromDouble(&pRng->end, (double)nEnd, DASUNITS_DIMENSIONLESS);
}

das_errcode DasRng_fromDatum(
	DasRng* pRng, const DasDatum* dmBeg, const DasDatum* dmEnd
){
	DasRng_setOpen(pRng);

	/* A DasRng outlives the call that fills it, so it can only hold datums
	   that own their bytes. Null is local */
	if((dmBeg != NULL)&&(!DasDatum_isLocal(dmBeg)))
		return daserrf(DASERR_DATUM,
			"Range begin is a %s datum, a reference to memory the range would "
			"not own", dasvt_name(dmBeg->vt)
		);
	if((dmEnd != NULL)&&(!DasDatum_isLocal(dmEnd)))
		return daserrf(DASERR_DATUM,
			"Range end is a %s datum, a reference to memory the range would "
			"not own", dasvt_name(dmEnd->vt)
		);

	/* NULL leaves the end as DasRng_setOpen() made it, open */
	if(dmBeg != NULL) pRng->begin = *dmBeg;
	if(dmEnd != NULL) pRng->end = *dmEnd;
	return DAS_OKAY;
}

/* ************************************************************************* */
/* Ranges on a named dimension */

das_errcode DasDimRng_set(
	DasDimRng* pThis, const char* sDim, const DasRng* pRng
){
	memset(pThis, 0, sizeof(DasDimRng));
	if(pRng != NULL) pThis->range = *pRng;
	else             DasRng_setOpen(&pThis->range);

	/* Stored lower case: uri.c matches these against registered coordinate
	   and field names, which are lower case by rule */
	size_t i = 0;
	for(; (sDim[i] != '\0') && (i < sizeof(pThis->physDim) - 1); ++i)
		pThis->physDim[i] = (char)tolower((unsigned char)sDim[i]);
	pThis->physDim[i] = '\0';
	if(sDim[i] != '\0')
		return daserrf(DASERR_DATUM,
			"Dimension name '%s' exceeds %zu-char limit", sDim, sizeof(pThis->physDim) - 1
		);
	return DAS_OKAY;
}
