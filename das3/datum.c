/* Copyright (C) 2017 Chris Piker <chris-piker@uiowa.edu>
 *
 * This file is part of das2C, the Core Das2 C Library.
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

#include "das1.h"
#include "datum.h"
#include "array.h"
#include "util.h"

/* If this were D code it would use SumType and be about 10 lines long :-)
   ...and have so many automatic features it would be hard to understand  :-( 
 */

/* ************************************************************************* */
/* Datum functions and structures */

void das_datum_init(
   das_datum* pThis, const ubyte* pSrc, das_val_type vt, uint32_t vsize, 
   das_units units
){
	pThis->vt = vt;
	pThis->units = units;

	switch(vt){
	case vtUnknown:
	case vtText:
		memcpy(pThis->bytes, pSrc, sizeof(void*));
		pThis->vsize = vsize;
		break;
	/* pointer AND length: copying only the pointer leaves a garbage size that
	   nothing downstream can detect */
	case vtByteSeq:
		memcpy(pThis->bytes, pSrc, sizeof(das_cbyte_seq));
		pThis->vsize = vsize;
		break;
	default:
		pThis->vsize = das_vt_size(vt);
		assert(pThis->vsize <= DATUM_BUF_SZ);
		memcpy(pThis->bytes, pSrc, pThis->vsize);
		break;
	}
}


bool das_datum_fromDbl(das_datum* pDatum, double value, das_units units)
{
	memset(pDatum, 0, sizeof(das_datum));
	pDatum->vt = vtDouble;
	pDatum->vsize = das_vt_size(vtDouble);
	memcpy(pDatum->bytes, &value, sizeof(double));
	pDatum->units = units;
	return true;
}

/* Initialize a small structure using a string, works for any string 
   but might not give you the results you expected */
bool das_datum_fromStr(das_datum* pDatum, const char* sStr)
{
	char sBuf[128] = {'\0'};
	das_time dt = {0};
	
	memset(pDatum, 0,  sizeof(das_datum));
	pDatum->vt = vtUnknown;
	
	const char* pRead = sStr;
	const char* pAhead = sStr + 1;
	
	struct lconv* pLocale = localeconv();
	char cDecPt = pLocale->decimal_point[0];  /* Not generically UTF-8 safe, but
															 * will handle French convention */
	if( sStr[0] == '\0') return false;
	
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
		if(dt_parsetime(sBuf, &dt) ){ 
			pDatum->units = UNIT_UTC;
			memcpy(pDatum->bytes, &dt, sizeof(das_time));
			pDatum->vt = vtTime;
			return true;
		}
	}
	
	/* Try classic datum, double value plus units */
	size_t uCopy = pRead - sStr < 127 ? pRead - sStr : 127;
	
	strncpy(sBuf, sStr, uCopy);
	if( ! das_str2double(sBuf, (double*)pDatum->bytes )) return false;
	pDatum->vt = vtDouble;
	
	if(*pRead == '\0'){
		pDatum->units = UNIT_DIMENSIONLESS;
		return true;
	}
		
	memset(sBuf, 0, 128);
	uCopy = strlen(sStr) - (pRead - sStr);
	uCopy = uCopy < 127 ? uCopy : 127;
	strncpy(sBuf, pRead, uCopy);
	
	pDatum->units = Units_fromStr(sBuf);
	
	return true;
}


/** A run of numeric cells plus the formalism that says what they mean.
 *
 * The caller owns the memory.  pRun, and pShape when it is used, point at
 * storage the caller supplied, so a composite datum is good for exactly as
 * long as that storage is.  Nothing here is copied and nothing is owned.
 *
 * A string or byte run is not one of these; those are vtText and vtByteSeq,
 * which have their own boxes.  A composite is always a run of numeric cells.
 */
typedef struct das_composite_t {

	/* FIRST, so the cast-to-your-type idiom still works */
	const ubyte* pRun;

	/* What the run means.  Never NULL: an unrecognized kind still gets a
	   generic form, so there is no "composite with no formalism" state. */
	const struct das_form* pForm;

	/* Only when DASCOMP_BIGSHAPE is set, NULL otherwise.  Small shapes ride in
	   uInfo so the common vector carries no second buffer to keep alive. */
	const ptrdiff_t* pShape;

	/* bits  0-3   rank, 0 to VARIDX_MAX
	   bits  4-7   DASCOMP_* flags
	   bits  8-15  das_val_type of one cell, a full byte so the enum can grow
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
#define DASCOMP_ET(U)    ((das_val_type)(((U) >> 8) & 0xFF))
#define DASCOMP_EXT(U,I) ((ptrdiff_t)(((U) >> (16 + 16*(I))) & 0xFFFF))

#define DASCOMP_INLINE_RANK 3
#define DASCOMP_INLINE_MAX  0xFFFF

/* The box rides inside a das_datum's byte array, so it has to fit.  32 bytes
   on a 64-bit host and 24 on wasm32 where pointers are four; nothing should
   assume which, only that it fits. */
typedef char _das_composite_fits[
	(sizeof(das_composite) <= DATUM_BUF_SZ) ? 1 : -1
];

/* Declared here rather than by including form.h: form.h already includes
   datum.h because pack() hands one back, so including it back would close a
   cycle.  One extern beats a function pointer in every datum. */
extern char* DasForm_prnRun(
	const struct das_form* pThis, const ubyte* pRun, uint32_t nElems,
	das_val_type et, char* sBuf, int nLen
);

bool das_datum_box(
	das_datum* pThis, const struct das_form* pForm, const ubyte* pRun,
	int nRank, const ptrdiff_t* pShape, das_val_type et, das_units units
){
	if((pThis == NULL)||(pForm == NULL)||(pRun == NULL))
		return das_error_false(DASERR_DATUM, "Null pointer boxing a composite");

	if((nRank < 0)||(nRank > ARYIDX_MAX))
		return das_error_false(DASERR_DATUM, "Invalid composite rank %d", nRank);

	if((nRank > 0)&&(pShape == NULL))
		return das_error_false(DASERR_DATUM,
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

	pThis->vt    = vtComposite;
	pThis->vsize = sizeof(das_composite);
	pThis->units = units;
	return true;
}

const struct das_form* das_datum_form(const das_datum* pThis)
{
	if(pThis->vt != vtComposite) return NULL;
	return ((const das_composite*)pThis)->pForm;
}

const ubyte* das_datum_run(const das_datum* pThis)
{
	if(pThis->vt != vtComposite) return NULL;
	return ((const das_composite*)pThis)->pRun;
}

int das_datum_shape(const das_datum* pThis, ptrdiff_t* pShape)
{
	switch(pThis->vt){

	case vtComposite: {
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
	   Exactly what strlen reports: the NUL that D2ARY_AS_STRING puts in the
	   array is das2C's storage choice, not part of the encoded value. */
	case vtText: {
		const char* sVal = *((const char* const*)pThis);
		if(pShape != NULL)
			pShape[0] = (sVal == NULL) ? 0 : (ptrdiff_t)strlen(sVal);
		return 1;
	}

	/* A das_cbyte_seq rides inline, pointer and length together */
	case vtByteSeq:
		if(pShape != NULL)
			pShape[0] = (ptrdiff_t)((const das_cbyte_seq*)pThis)->sz;
		return 1;

	default:
		return 0;
	}
}

size_t das_datum_nElems(const das_datum* pThis)
{
	ptrdiff_t aShape[ARYIDX_MAX];
	int nRank = das_datum_shape(pThis, aShape);

	size_t uElems = 1;
	for(int i = 0; i < nRank; ++i){
		if(aShape[i] < 0) return 0;      /* ragged, no fixed count */
		uElems *= (size_t)aShape[i];
	}
	return uElems;
}

size_t das_datum_runBytes(const das_datum* pThis)
{
	if(pThis->vt != vtComposite) return 0;

	size_t uElems = das_datum_nElems(pThis);
	if(uElems == 0) return 0;
	return uElems * das_vt_size(DASCOMP_ET(((const das_composite*)pThis)->uInfo));
}

das_val_type das_datum_elemType(const das_datum* pThis)
{
	switch(pThis->vt){
	case vtText: return vtUByte;
	case vtByteSeq: return vtUByte;
	case vtComposite: return DASCOMP_ET(((const das_composite*)pThis)->uInfo);
	default:  return pThis->vt;
	}
}

/* This one is simple */
/* Argument order matches das_datum_fromDbl and _byteSeq: value, then units.
   das_units is a const char*, so a swap here compiles clean and silently
   trades the two -- the header is the contract. */
bool das_datum_wrapStr(das_datum* pDatum, const char* sStr, das_units units)
{
	/* Careful, we are copying an address, not a value at an address*/
	memcpy(pDatum->bytes, &sStr,  sizeof(const char*));
	pDatum->vt = vtText;
	pDatum->units = units;
	pDatum->vsize = sizeof(const char*);
	return true;
}

bool das_datum_byteSeq(
	das_datum* pDatum, das_cbyte_seq seq, das_units units
){
	memcpy(pDatum->bytes, &seq, sizeof(das_cbyte_seq));
	pDatum->vt = vtByteSeq;
	pDatum->vsize = sizeof(das_cbyte_seq);
	pDatum->units = units;
	return true;
}

int das_datum_toDoubles(const das_datum* pThis, double* pOut, int nMax)
{
	if((pThis == NULL)||(pOut == NULL)||(nMax < 1))
		return -1 * das_error(DASERR_DATUM, "Invalid inputs to das_datum_toDoubles");

	/* A scalar is the one-component case of the same question, so a caller
	   holding a datum of unknown class does not have to test for composite
	   before it can ask. */
	if(pThis->vt != vtComposite){
		if(!das_datum_toDbl(pThis, pOut)) return -1 * DASERR_DATUM;
		return 1;
	}

	const ubyte* pRun = das_datum_run(pThis);
	if(pRun == NULL)
		return -1 * das_error(DASERR_DATUM, "Datum carries no component run");

	int nElems = (int)das_datum_nElems(pThis);
	das_val_type et = das_datum_elemType(pThis);

	/* Short runs are legal.  A stream may ship fewer components than its
	   system defines, so slots past the count are left EXACTLY as the caller
	   set them; what an absent component means belongs to the formalism, and
	   das_vsys_default() is where that answer lives. */
	int n = (nElems < nMax) ? nElems : nMax;

	/* One type test for the whole item rather than one per component.  These
	   two arms carry most real traffic and neither can fail, so the general
	   path below is left for the storage types that need checking. */
	switch(et){
	case vtDouble:
		memcpy(pOut, pRun, (size_t)n * sizeof(double));
		return n;
	case vtFloat: {
		const float* pF = (const float*)pRun;
		for(int i = 0; i < n; ++i) pOut[i] = pF[i];
		return n;
	}
	default: break;
	}

	size_t uSz = das_vt_size(et);
	for(int i = 0; i < n; ++i){
		if(das_value_binXform(et, pRun + i*uSz, NULL, vtDouble,
		                      (ubyte*)(pOut + i), NULL, 0) != DAS_OKAY)
			return -1 * das_error(DASERR_DATUM, "Bad component %d", i);
	}
	return n;
}

bool das_datum_toDbl(const das_datum* pThis, double* pOut)
{
	if((pThis == NULL)||(pOut == NULL))
		return das_error_false(DASERR_DATUM, "Invalid inputs to das_datum_toDbl");

	switch(pThis->vt){
	case vtUByte:  *pOut = *((ubyte*)pThis);    return true;
	case vtByte:   *pOut = *((int8_t*)pThis);   return true;
	case vtUShort: *pOut = *((uint16_t*)pThis); return true;
	case vtShort:  *pOut = *((int16_t*)pThis);  return true;
	case vtUInt:   *pOut = *((uint32_t*)pThis); return true;
	case vtInt:    *pOut = *((int32_t*)pThis);  return true;
	case vtULong:  *pOut = *((uint64_t*)pThis); return true;
	case vtLong:   *pOut = *((int64_t*)pThis);  return true;
	case vtFloat:  *pOut = *((float*)pThis);    return true;
	case vtDouble: *pOut = *((double*)pThis);   return true;

	/* Attempt string conversion */
	case vtText:
		if(!das_str2double((const char*)pThis, pOut))
			return das_error_false(DASERR_DATUM, "Couldn't convert %s to a double",
			                       (const char*)pThis);
		return true;

	default: break;
	}

	/* vtTime lands here on purpose.  A calendar time has no reading as a plain
	   double until an epoch is named, which is das_datum_toEpoch()'s job. */
	return das_error_false(DASERR_DATUM,
		"Don't know how to convert items of type %s to doubles.",
		das_vt_toStr(pThis->vt)
	);
}

/* Like toDbl but cares about scale and epoch */
bool das_datum_toEpoch(
	const das_datum* pThis, das_units epoch, double* pResult
){
	if(!Units_haveCalRep(epoch) || (epoch == UNIT_UTC)) return false;
	
	das_time dt;
	
	if(pThis->vt == vtTime){ 
		*pResult = Units_convertFromDt(epoch, (das_time*)pThis);
		return (*pResult != DAS_FILL_VALUE);
	}
	
	/* text is interesting, because it could be 2017-01-01 or something
	   like "2.37455" which are handle very differently */
	
	double rDbl = 0.0;
	if(pThis->vt == vtText){
		if( dt_parsetime((const char*)pThis, &dt)){
			return Units_convertFromDt(epoch, &dt);
		}
		else{
			/* parsetime failed, try to convert as an ASCII real */
			if( ! das_str2double((const char*)pThis, &rDbl)) return false;
			
			/* have a real, see if I'm in non UTC epoch units */
			if(!Units_haveCalRep(pThis->units) || (pThis->units == UNIT_UTC)) 
				return false;
			
			*pResult = Units_convertTo(epoch, rDbl, pThis->units);
			return (*pResult != DAS_FILL_VALUE);
		}
	}
	
	/* for the rest, I have to have an epoch of my own or I don't 
	   know where zero is at */
	if(!Units_haveCalRep(pThis->units) || (pThis->units == UNIT_UTC)) 
		return false;

	switch(pThis->vt){
	case vtUByte:  rDbl = *((uint8_t*)pThis); break;
	case vtByte:   rDbl = *((int8_t*)pThis); break;
	case vtUShort: rDbl = *((uint16_t*)pThis); break;
	case vtShort:  rDbl = *((int16_t*)pThis); break;
	case vtUInt:   rDbl = *((uint32_t*)pThis); break;
	case vtInt:    rDbl = *((int32_t*)pThis); break;
	case vtULong:  rDbl = *((uint64_t*)pThis); break;
	case vtLong:   rDbl = *((int64_t*)pThis); break;
	case vtFloat:  rDbl = *((float*)pThis); break;
	case vtDouble: rDbl = *((double*)pThis); break;
	default:
		das_error(DASERR_DATUM, "Don't know how to convert items of type %s"
		          " to epoch times", das_vt_toStr(pThis->vt));
		return false;
	}
	
	*pResult = Units_convertTo(epoch, rDbl, pThis->units);
	return (*pResult != DAS_FILL_VALUE);
}

bool das_datum_toTime(const das_datum* pThis, das_time* pDt)
{
	
	
	if(pThis->vt == vtTime){
		memcpy(pDt, pThis, sizeof(das_time));
		return true;
	}
	if(pThis->vt == vtText)
		return dt_parsetime((const char*)pThis, pDt);
	
	if(!Units_haveCalRep(pThis->units) || (pThis->units == UNIT_UTC)) 
		return false;

	/* Special case for TT2000 long integers, need to preserve resolution */
	if((pThis->vt == vtLong)&&(pThis->units == UNIT_TT2000)){
		dt_from_tt2k(pDt, *((int64_t*)pThis) );
		return true;
	}

	double rDbl = 0.0;
	switch(pThis->vt){
	case vtUByte:  rDbl = *((uint8_t*)pThis); break;
	case vtByte:   rDbl = *((int8_t*)pThis); break;
	case vtUShort: rDbl = *((uint16_t*)pThis); break;
	case vtShort:  rDbl = *((int16_t*)pThis); break;
	case vtUInt:   rDbl = *((uint32_t*)pThis); break;
	case vtInt:    rDbl = *((int32_t*)pThis); break;
	case vtULong:  rDbl = *((uint64_t*)pThis); break;
	case vtLong:   rDbl = *((int64_t*)pThis); break;
	case vtFloat:  rDbl = *((float*)pThis); break;
	case vtDouble: rDbl = *((double*)pThis); break;
	default:
		das_error(DASERR_DATUM, "Don't know how to convert items of type %s"
		          " to epoch times", das_vt_toStr(pThis->vt));
		return false;
	}

	return (Units_convertToDt(pDt, rDbl, pThis->units) == DAS_OKAY);
}

	
/** Write a datum out as a string, straight-forward, no type conversion */
char* _das_datum_toStr(
	const das_datum* pThis, char* sBuf, int nLen, int nFracDigits, bool bPrnUnits,
	const char* sSep
){
	if(nLen < 2) return NULL;
	memset(sBuf, 0, nLen);
	
	/* Write the value... */
	char sFmt[32] = {'\0'};
	size_t u = 0;
	const das_idx_info* pInfo = NULL;
	const das_cbyte_seq* pBs = NULL;

	int nWrote = 0;
	switch(pThis->vt){

	case vtByte:
		nWrote = snprintf(sBuf, nLen - 1, "%hhd", *((int8_t*)pThis));
		break;
			
	case vtUByte:
		nWrote = snprintf(sBuf, nLen - 1, "%hhu", *((uint8_t*)pThis));
		break;
		
	case vtUShort:
		nWrote = snprintf(sBuf, nLen - 1, "%hu", *((uint16_t*)pThis));
		break;
		
	case vtShort:
		nWrote = snprintf(sBuf, nLen - 1, "%hd", *((int16_t*)pThis));
		break;

	case vtUInt:
		nWrote = snprintf(sBuf, nLen - 1, "%u", *((uint32_t*)pThis));
		break;		
		
	case vtInt:
		nWrote = snprintf(sBuf, nLen - 1, "%d", *((int32_t*)pThis));
		break;

	case vtULong:
		nWrote = snprintf(sBuf, nLen - 1, "%" PRIu64, *((uint64_t*)pThis));
		break;

	case vtLong:
		/* nWrote = snprintf(sBuf, nLen - 1, "%lld", *((int64_t*)pThis)); */
		/* RPId64 is a built-in format code for int64 items since the actual
		   code to use varies from system to system.  Defined in inttypes.h
			above */
		nWrote = snprintf(sBuf, nLen - 1, "%" PRId64, *((int64_t*)pThis));
		break;
		
	case vtFloat:
		/* A negative digit count means "no guidance, pick a default".  Use a
		   round-trippable width (9 sig figs covers a 32-bit float) rather than
		   pasting the negative straight into the format and emitting garbage. */
		snprintf(sFmt, 31, "%%.%de", (nFracDigits < 0) ? 8 : nFracDigits);
		nWrote = snprintf(sBuf, nLen - 1, sFmt, *((float*)pThis));
		break;

	case vtDouble:
		/* Likewise, 17 sig figs round-trips a 64-bit double exactly. */
		snprintf(sFmt, 31, "%%.%de", (nFracDigits < 0) ? 16 : nFracDigits);
		nWrote = snprintf(sBuf, nLen - 1, sFmt, *((double*)pThis));
		break;
		
	case vtTime:
		dt_isoc(sBuf, nLen - 1, (const das_time*)pThis, nFracDigits);
		nWrote = strlen(sBuf);
		break;
		
	case vtText:
		strncpy(sBuf, *((const char**)pThis), nLen - 1);
		nWrote = ((nLen - 1 ) > strlen(sBuf)) ? nLen - 1 : strlen(sBuf);
		break;
		
	case vtByteSeq:
		/* Two hex digits per byte with sSep between, as many as fit.  The
		   separator may be more than one character, so no fixed stride. */
		pBs = (das_cbyte_seq*)pThis;
		for(u = 0; u < pBs->sz; ++u){
			int nNeed = 2 + ((u > 0) ? (int)strlen(sSep) : 0);
			if(nLen - nWrote <= nNeed) break;
			nWrote += snprintf(sBuf + nWrote, (size_t)(nLen - nWrote), "%s%02hhX",
			                   (u > 0) ? sSep : "", ((const ubyte*)pBs->ptr)[u]);
		}
		break;
	
	case vtComposite: {
		const das_composite* pC = (const das_composite*)pThis;
		size_t uElems = das_datum_nElems(pThis);

		/* The formalism renders if it has an opinion.  It usually does, and it
		   is the only thing that knows a run of three is a vector rather than
		   three unrelated numbers. */
		if(DasForm_prnRun(
			pC->pForm, pC->pRun, (uint32_t)uElems, DASCOMP_ET(pC->uInfo),
			sBuf, nLen
		) != NULL){
			nWrote = strlen(sBuf);
			break;
		}

		/* Otherwise the cells, separated and bracketed.  Enough to read, and
		   it needs nothing from the form layer. */
		das_val_type etCell = DASCOMP_ET(pC->uInfo);
		size_t uCellSz = das_vt_size(etCell);
		char* pWr = sBuf;
		int nLeft = nLen;
		int nUsed = snprintf(pWr, (size_t)nLeft, "[");
		pWr += nUsed; nLeft -= nUsed;

		for(size_t v = 0; (v < uElems)&&(nLeft > 2); ++v){
			das_datum dmCell;
			das_datum_init(
				&dmCell, pC->pRun + v*uCellSz, etCell, (uint32_t)uCellSz,
				pThis->units
			);
			_das_datum_toStr(&dmCell, pWr, nLeft, nFracDigits, false, sSep);
			nUsed = strlen(pWr);
			pWr += nUsed; nLeft -= nUsed;
			if((v + 1 < uElems)&&(nLeft > 2)){
				nUsed = snprintf(pWr, (size_t)nLeft, "%s", sSep);
				pWr += nUsed; nLeft -= nUsed;
			}
		}
		if(nLeft > 1) snprintf(pWr, (size_t)nLeft, "]");
		nWrote = strlen(sBuf);
		break;
	}

	case vtIndex:
		pInfo = (const das_idx_info*)pThis;
		snprintf(sBuf, nLen - 1, "Offset:%zd%sCount:%zu", pInfo->nOffset, sSep, pInfo->uCount);
		nWrote = strlen(sBuf);
		break;

	default:
		strncpy(sBuf, "UNKNOWN", nLen -1);
		nWrote = (7 < (nLen - 1)) ? 7 : (nLen - 1);
		break;
	}
	
	nLen -= nWrote;

	/* Write the units */
	const char* sUnits = Units_toStr(pThis->units);
	if((pThis->units != UNIT_DIMENSIONLESS)&&(bPrnUnits)){
		if(nLen > strlen(sUnits) + 2){
			sBuf[nWrote] = ' ';  ++nWrote;
			strncpy(sBuf + nWrote, sUnits, strlen(sUnits));
			nWrote += strlen(sUnits);
			sBuf[nWrote] = '\0';
		}
	}
		
	return sBuf;
}

char* das_datum_toStr(
	const das_datum* pThis, char* sStr, size_t uLen, int nFracDigits
){
	return _das_datum_toStr(pThis, sStr, uLen, nFracDigits, true, ";");
}

char* das_datum_toStrValOnly(
	const das_datum* pThis, char* sStr, size_t uLen, int nFracDigits
){
	return _das_datum_toStr(pThis, sStr, uLen, nFracDigits, false, ";");
}

char* das_datum_toStrValOnlySep(
	const das_datum* pThis, char* sStr, size_t uLen, int nFracDigits, 
	const char* sSep
){
	return _das_datum_toStr(pThis, sStr, uLen, nFracDigits, false, sSep);
}


