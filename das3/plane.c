/* Copyright (C) 2004-2017 Jeremy Faden <jeremy-faden@uiowa.edu>
 *                         Chris Piker <chris-piker@uiowa.edu>
 *
 * This file is part of das2C, the Core Das C Library.
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

#include <math.h>
#include <string.h>
#include <stdio.h>
#include <ctype.h>
#include <stddef.h>

#include "util.h"
#include "str.h"
#include "log.h"
#include "buffer_p.h"
#include "enc_p.h"
#include "value.h"
#include "units.h"
#include "packet_p.h"

static void _del_DasPlane(DasDesc* pDesc);  /* del_DasDesc() target */


#define _IDX_YTAG_INT 0
#define _IDX_YTAG_MIN 1
#define _IDX_YTAG_MAX 2

/* ************************************************************************* */
/* PlaneType info */

das_planetype dasplanetype_id( const char * type ) {
    if( strcmp( type, "x" )==0 )     return DASPLANETYPE_X;
    if( strcmp( type, "y" )==0 )     return DASPLANETYPE_Y;
	 if( strcmp( type, "yscan" )==0 ) return DASPLANETYPE_YSCAN;
	 if( strcmp( type, "z" )==0 )     return DASPLANETYPE_Z;
    
	 daserrf(17, "getPlaneType: unrecognized type %s\n", type );
	 return -1; /* Never reached, making compiler happy */
}

const char * dasplanetype_name( das_planetype type ) {
	switch(type){
	case DASPLANETYPE_X:     return "x";
	case DASPLANETYPE_YSCAN: return "yscan";
	case DASPLANETYPE_Y:     return "y";
	case DASPLANETYPE_Z:     return "z";
	default:
		daserrf(17, "getPlaneTypeString: unrecognized type: %d\n", type );
	}
	return NULL; /* Never reached, making compiler happy */
}

/* ************************************************************************* */
/* Construction/Destruction */

/* Empty Constructor */
DasPlane* new_DasPlane_empty() {
	DasPlane* pThis = (DasPlane*)calloc(1, sizeof(DasPlane));
	
	DasDesc_init((DasDesc*)pThis, DASDESCTYPE_PLANE, _del_DasPlane);
	pThis->planeType = DASPLANETYPE_INV;
   pThis->bPlaneDataValid = false;
	pThis->rFill = DASVAL_FILL;
	pThis->_bFillSet = false;
	return pThis;
}

/* Generic Constructor */
DasPlane* new_DasPlane(
	das_planetype pt, const char* sGroup, DasEnc* pType, DasUnits units)
{
	DasPlane* pThis = (DasPlane*)calloc(1, sizeof(DasPlane));

	DasDesc_init((DasDesc*)pThis, DASDESCTYPE_PLANE, _del_DasPlane);
	
	pThis->planeType = pt;
	if(sGroup != NULL){
		size_t uGrpLen = strlen(sGroup);
		pThis->sName = calloc(uGrpLen+1, sizeof(char));
		memcpy(pThis->sName, sGroup, uGrpLen);
	}
	
	pThis->units = units;
	pThis->uItems = 1;
	pThis->pData = &pThis->value;
	pThis->value = DASVAL_FILL;
	pThis->rFill = DASVAL_FILL;
	pThis->_bFillSet = false;
	
	pThis->yTagInter = DASVAL_FILL;
	pThis->yTagMin   = DASVAL_FILL;
	pThis->yTagMax   = DASVAL_FILL;
	
	pThis->pEncoding = pType;
		
   pThis->bPlaneDataValid = false;
	return pThis;
}

/* YScan Constructor */
DasPlane* new_DasPlane_yscan(
	const char* sGroup, DasEnc* pZType, DasUnits zUnits, size_t uItems, 
	DasEnc* pYType, const double* pYTags, DasUnits yUnits
){
	/* Refuse before construction: new_DasPlane takes over pZType, so a later
	   refusal would have to free a plane and an encoder the caller still
	   thinks it owns.  Here nothing has been taken. */
	if(uItems < 1){
		daserrf(17, "Must have at least 1 item in a yscan");
		return NULL;
	}
	if((pYTags != NULL)&&(pYType != NULL)&&
	   ((pYType->nCat == DAS2DT_BE_REAL)||(pYType->nCat == DAS2DT_LE_REAL))){
		daserrf(17, "Binary encodings can't be used for YTags values, "
				"cause they end up in XML headers.");
		return NULL;
	}

	DasPlane* pThis;
	pThis = new_DasPlane(DASPLANETYPE_YSCAN,sGroup,pZType,zUnits);
	int nWidth = 0;
	
	pThis->uItems = uItems;
	pThis->yTagUnits = yUnits;
	pThis->pData = (double*)calloc(pThis->uItems, sizeof(double));
	
	for(unsigned int u = 0; u < pThis->uItems; u++)
		pThis->pData[u] = DASVAL_FILL;
	pThis->bAlloccedBuf = true;
	
	pThis->pYTags = (double*)calloc(pThis->uItems, sizeof(double));
	if(pYTags != NULL){
		for(unsigned int u = 0; u < pThis->uItems; u++) 
			pThis->pYTags[u] = pYTags[u];
		
		if(pYType != NULL){ 
			pThis->pYEncoding = pYType;
		}		
		else{
			pThis->pYEncoding = new_DasEnc(DAS2DT_ASCII, 12, NULL);
		}
		
	}
	else{
		for(unsigned int u = 0; u < pThis->uItems; u++)	pThis->pYTags[u] = u;
		
		if(pYType != NULL){
			pThis->pYEncoding = pYType;
		}
		else{
			nWidth = (int) ceil( log10(pThis->uItems+1) ) + 1;
			pThis->pYEncoding = new_DasEnc(DAS2DT_ASCII, nWidth, "%.0f");
		}
	}
	
	pThis->ytag_spec = DASYTAGTYPE_LIST;
	pThis->yTagInter = DASVAL_FILL;
	pThis->yTagMin   = DASVAL_FILL;
	pThis->yTagMax   = DASVAL_FILL;
	
	return pThis;	
}

DasPlane* new_DasPlane_yscanSeq(
	const char* sGroup, DasEnc* pZType, DasUnits zUnits, size_t uItems,
   double yTagInter, double yTagMin, double yTagMax, DasUnits yUnits
){
	if(uItems < 1){
		daserrf(17, "Must have at least 1 item in a yscan");
		return NULL;
	}
	if(yTagInter <= 0.0){
		daserrf(17, "YTag series interval must be greater than 0");
		return NULL;
	}
	
	DasPlane* pThis;
	pThis = new_DasPlane(DASPLANETYPE_YSCAN,sGroup,pZType,zUnits);
		
	pThis->uItems = uItems;
	pThis->yTagUnits = yUnits;
	pThis->pData = (double*)calloc(pThis->uItems, sizeof(double));
	
	for(unsigned int u = 0; u < pThis->uItems; u++)
		pThis->pData[u] = DASVAL_FILL;
	pThis->bAlloccedBuf = true;
	
	pThis->ytag_spec = DASYTAGTYPE_SEQ;
	
	pThis->yTagInter = yTagInter;
	
	if(dasval_isFill(yTagMin) && dasval_isFill(yTagMax)){
		pThis->yTagMin = 0.0;
		pThis->yTagMax = pThis->yTagInter * pThis->uItems;
	}
	else{
		if(dasval_isFill(yTagMin)){
			pThis->yTagMax = yTagMax;
			pThis->yTagMin = yTagMax - (pThis->yTagInter * pThis->uItems);
		}
		else{
			pThis->yTagMin = yTagMin;
			pThis->yTagMax = yTagMin + (pThis->yTagInter * pThis->uItems);
		}
	}
	
	return pThis;
}

/* ************************************************************************* */
/* Key/Value Pairs Constructor */


/* Helper for new_DasPlane_pairs, tries to determine a reasonable
   encoding for the ytags values, assumes nItems is already valid */
static das_errcode _DasPlane_decodeYTags(DasPlane* pThis, const char* sYTags)
{
	int nCat = DAS2DT_ASCII;
	char sFmt[64] = {'\0'};
	DasEnc* pEnc = NULL;
	unsigned int uCommas = 0;
	
	int nSigBefore = 0;
	int nSigAfter = 0;
	int  nMaxSigBefore = 0;    
	int  nMaxSigAfter = 0;
	int nMaxSig = 0;
	int nFmtWidth = 0;
	
	bool bHasExp = false;
	bool bHasDecimal = false;
	bool bOutputExp = false;
	
	const char* pRead = NULL;
	size_t u = 0;
	char sVal[64] = {'\0'};
	char* pWrite = NULL;
	
	
	
	/* If the YTagsString isn't present just use a data index */
	pThis->pYTags = (double*)calloc(pThis->uItems, sizeof(double));
	
	if(sYTags == NULL){
		for(unsigned int u = 0; u < pThis->uItems; u++)
			pThis->pYTags[u] = (double)u;
		
		nFmtWidth = (int) ceil( log10(u+1) ) + 1;
		strcpy(sFmt, "%.0f");
		return 0;
	}
	
	/* How many commas do I have */
	for(u = 0; u<strlen(sYTags); u++ )
       if(sYTags[u] == ',' ) uCommas++;
		
	if(uCommas + 1 != pThis->uItems)
		return daserrf(17, "Number of YTag values (%d) is not equal to the "
		                  "nitems value (%zu)", uCommas + 1, pThis->uItems);
		
	/* Convert each value, while getting the max num of significant digits 
	   and use of exponents */
	pRead = sYTags;
	int v = 0;
	int nSignSpace = 0;
	for(u = 0; u<pThis->uItems; u++){
			
		bHasDecimal = false;
		bHasExp = false;
		nSigBefore = 0;
		nSigAfter = 0;
		
		memset(sVal, 0, 64);			
		pWrite = sVal;
			
		/* Get this value into it's own buffer */
		while(*pRead != ',' && *pRead != '\0' && (pWrite - sVal) < 64){
			if(!isspace(*pRead)){ 
				*pWrite = *pRead;
				++pWrite;
			}
			++pRead;
		}
		++pRead;
			
		/* Trim leading zeros, but put back one if there are no digits
		 * left before the decimal point */
		while((strlen(sVal) > 1) && (sVal[0] == '0')){
			int nLen = strlen(sVal);
			for(v = 1; v < nLen; v++) sVal[v-1] = sVal[v];
		}
		if(sVal[0] == '.'){ 
			int nLen = strlen(sVal);
			for(v=nLen - 1; v >= 0; v--) sVal[v+1] = sVal[v];
			sVal[0] = '0';
		}
	
		/* Trim Trailing zeros, if there is a decimal point in the number.
		   make sure to move to before the exponent (if present) before
			doing this */
		dasstr_trimReal(sVal);
			
		/* Convert it to a double */
		if( sVal[0] != '\0'){ 
			if( sscanf(sVal, "%lf", pThis->pYTags + u) != 1)
				return daserrf(17, "Couldn't parse YTag value '%s'", sVal);
		}
			
		/* Figure out a reasonable output format */
		for(v = 0; v < strlen(sVal); ++v){
				
			if(sVal[v] == 'e' || sVal[v] == 'E'){
				bHasExp = true;
				bOutputExp = true;
				continue;
			}
			
			if(sVal[v] == '-') nSignSpace = 1;
				
			/* bHasDecimal only matters for %f format values. */
			if(sVal[v] == '.'){
				bHasDecimal = true;
				continue;
			}
				
			if(sVal[v] == ':' || sVal[v] == 'T'){
				return daserrf(17, "Time values in YTags are not yet "
				                  "supported, but there's no reason not to");
			}
				
			/* Don't count digits in the exponent, these are always the same */
			if(!bHasExp && isdigit(sVal[v])){
				if(bHasDecimal) nSigAfter++;
				else nSigBefore++;
			}
		}
			
		if(nSigBefore > nMaxSigBefore) nMaxSigBefore = nSigBefore;
		if(nSigAfter > nMaxSigAfter) nMaxSigAfter = nSigAfter;
	}
		
	/* This can happen when all Ytags are zero */
	if((nMaxSigBefore + nMaxSigAfter) != 0) 
		nMaxSig = nMaxSigBefore + nMaxSigAfter;
	else
		nMaxSig = 1;
	
	/* Do something about formats with an insane number of digits... */
	
	/* If range is > 4 orders of magnitude switch to exponential   */
	int nMagEnd = (int)ceil(log10(pThis->pYTags[pThis->uItems-1]));
	int nMagBeg = (int)ceil(log10(pThis->pYTags[0]));
	if(!bOutputExp && ((nMagEnd - nMagBeg) > 5)){
		bOutputExp = true;
		nMaxSigAfter += nMaxSigBefore - 1;
		nMaxSigBefore = 1;
	}
	
	/* If SigDigits are > 5, trim to 5 if exponential.  Since exponential
	 * uses more space only switch to it if there are more than 9 sig 
	 * digits in a decimal number.  Also, since values can range over quite
	 * a range, when trimming sig-digits go to exponential to avoid loosing
	 * relative precision */
	if((bOutputExp && (nMaxSig > 5)) || (!bOutputExp && (nMaxSig > 9))){
		bOutputExp = true;
		int nRm = nMaxSig - 5;
		nMaxSigAfter = (nMaxSigAfter < nRm) ? 0 : (nMaxSigAfter - nRm);
		nMaxSig = nMaxSigAfter + nMaxSigBefore;
	}
		
	/* Determine format string */
	if(bOutputExp){
		/* Sig digit + sign + decimal + exponent str  */
		sprintf(sFmt, "%%%d.%de", nMaxSig + nSignSpace + 5 , nMaxSig - 1);
		nFmtWidth = nMaxSig + nSignSpace + 1 + 4;
	}
	else{
		/* Sig digit + sign + (optional) decimal*/
		nFmtWidth = nMaxSig + nSignSpace + ((nMaxSigAfter > 0) ? 1 : 0);
		sprintf(sFmt, "%%%d.%df", nFmtWidth, nMaxSigAfter);
	}
	    
	if( (pEnc = new_DasEnc(nCat, nFmtWidth+1, sFmt)) == NULL) return 17;
	pThis->pYEncoding = pEnc;
	
	return 0;
}

/* ************************************************************************* */
/* Construct from XML data */

/* Units here are stream input.  Text that doesn't parse becomes dimensionless
 * and is kept as a string property so nothing is lost on the way through.
 * DasUnits_fromStr() has already logged why it didn't parse. */
static DasUnits _DasPlane_unitsOrText(
	DasPlane* pThis, const char* sText, const char* sProp
){
	DasUnits units = DasUnits_fromStr(sText);
	if(units != NULL) return units;

	DasDesc_setVal((DasDesc*)pThis, sProp, sText);
	daslog_warnf("Units '%s' set to dimensionless, the text is kept in "
	              "property '%s'", sText, sProp);
	return DASUNITS_DIMENSIONLESS;
}

DasPlane* new_DasPlane_pairs(
	DasDesc* pParent, das_planetype pt, const char** attr
){
	DasPlane* pThis = (DasPlane*)calloc(1, sizeof(DasPlane));
	
	const char* sYTags = NULL;
	
	DasDesc_init((DasDesc*)pThis, DASDESCTYPE_PLANE, _del_DasPlane);
	pThis->base.parent = pParent;
	pThis->planeType = pt;
	
	pThis->ytag_spec = DASYTAGTYPE_NONE;
	pThis->yTagInter = DASVAL_FILL;
	pThis->yTagMin   = DASVAL_FILL;
	pThis->yTagMax   = DASVAL_FILL;
	
	if(pt != DASPLANETYPE_X && pt != DASPLANETYPE_Y && pt != DASPLANETYPE_YSCAN && pt != DASPLANETYPE_Z){
		daserrf(17, "Invalid plane type %d", pt);
		free(pThis);
		return NULL;
	}

	int i;
	
	/* Preprocess to get the encoding first, it affects the interpretation of
	 * the type value */
	for (i=0; attr[i]; i+=2) {
		if ( strcmp( attr[i], "type" )==0 ) {
			pThis->pEncoding= new_DasEnc_str((char *)attr[i+1]);
			break;
		}
	}
	
	/* Common processing for all plane types */
	for (i=0; attr[i]; i+=2) {
		if ( strcmp( attr[i], "type" )==0 ) continue;
		
		if(strcmp(attr[i], "name") == 0){
				if(strlen(attr[i+1]) > 0){
					pThis->sName = (char*)calloc(strlen(attr[i+1]) + 1, 
							                       sizeof(char));
					strcpy( pThis->sName, attr[i+1] );
				}
				continue;
		}
		if(strcmp(attr[i], "units")==0 ) {
			/* There is an entanglement between encoding and units that probably
			 * shouldn't exist.  The encoding type 'timeXX' means that there really
			 * aren't any preferred units for describing double precision time
			 * values.  If the user has set one use it, otherwise use us2000 for
			 * now.  The flag UTC means there are not preferred epoch units */
			
			DasUnits units = _DasPlane_unitsOrText(pThis, attr[i+1], "unitsText");
			if(units == DASUNITS_UTC){
				if((pThis->pEncoding != NULL)&&(pThis->pEncoding->nCat == DAS2DT_TIME))
					pThis->units = DASUNITS_US2000;
			}
			else
				pThis->units = units;
		}
	}
	
	if(pThis->pEncoding == NULL){
		daserrf(17, "Data 'type' attribute missing from plane description");
		return NULL;
	}
	
	/* Additional processing by plane type */
	switch(pThis->planeType){			
	case DASPLANETYPE_X:
	case DASPLANETYPE_Y:
	case DASPLANETYPE_Z:
		pThis->uItems= 1;
		pThis->pData = &(pThis->value);
		pThis->bAlloccedBuf = false;
		break;
	
	case DASPLANETYPE_YSCAN:
		for (i=0; attr[i]; i+=2){
								  
			if ( strcmp(attr[i], "nitems")==0 ) {
				if(sscanf(attr[i+1], "%zu", &pThis->uItems) != 1) {
					daserrf(17, "Couldn't convert %s=\"%s\" to a positive integer",
					           attr[i], attr[i+1]);
					return NULL;
				}
				
				/* Assuming 6 digits to store sizes, no <x> plane and smallest
				   encoding of 4-bytes/value the largest number of items is 
					249999 */
				if(pThis->uItems > 249999){
					daserrf(17, "Max number of supported items in a Das2 stream"
					           " is 249999\n");
					return NULL;
				}
				
				pThis->pData = (double*)calloc(pThis->uItems, sizeof(double));
				pThis->bAlloccedBuf = true;
				continue;
			} 
			if ( strcmp(attr[i], "zUnits")==0 ) {
				pThis->units = _DasPlane_unitsOrText(pThis, attr[i+1], "unitsText");
				continue;
			} 
			if ( strcmp( attr[i], "yTags" )==0 ) {
				sYTags = attr[i+1];
				pThis->ytag_spec = DASYTAGTYPE_LIST;
				continue;			
			}
			if ( strcmp( attr[i], "yTagInterval" )==0 ) {
				
				if( (! dasstr_toDouble(attr[i+1], &(pThis->yTagInter))) ||
				    (pThis->yTagInter <= 0.0) ){
					daserrf(17, "Couldn't convert %s to a real positive number",
							         attr[i+1]);
					return NULL;
				}
				pThis->ytag_spec = DASYTAGTYPE_SEQ;
				continue;			
			}
			if ( strcmp( attr[i], "yTagMin" )==0 ) {
				if(! dasstr_toDouble(attr[i+1], &(pThis->yTagMin))){
					daserrf(17, "Couldn't convert %s to a real number", attr[i+1]);
					return NULL;
				}
				continue;			
			}
			if ( strcmp( attr[i], "yTagMax" )==0 ) {
				if(! dasstr_toDouble(attr[i+1], &(pThis->yTagMax))){
					daserrf(17, "Couldn't convert %s to a real number", attr[i+1]);
					return NULL;
				}
				continue;			
			}
			if ( strcmp( attr[i], "yUnits" )==0 ) {
				pThis->yTagUnits = _DasPlane_unitsOrText(pThis, attr[i+1], "yUnitsText");
				continue;
			}					
		}
	   break;
	default:
		daserrf(17, "Code Change caused error in new_DasPlane_pairs");
		return NULL;
	}
	
	/* Some checks for required items */
	if(pThis->uItems < 1){
		daserrf(17, "Illegal number of items, %zu, in %s plane", 
		                pThis->uItems, dasplanetype_name(pt));
		return NULL;
	}
	
	/* Have to have a units string, unless these are time values then
	   the units string will be set internally to us2000 */
	if(pThis->units == NULL){
		if(pThis->pEncoding->nCat == DAS2DT_TIME){
			pThis->units = DASUNITS_US2000;
		}
		else{
			daserrf(17, "Units element missing in plane description");
			return NULL;
		}
	}
	
	/* handle the ytags array now that both tags should be present */
	if(pThis->planeType == DASPLANETYPE_YSCAN){
		if(pThis->ytag_spec != DASYTAGTYPE_SEQ){
			if( _DasPlane_decodeYTags(pThis, sYTags) != 0) return NULL;
		}
		else{	
			/* For series yTags, have at least one of yTagMin or yTagMax */
			if( dasval_isFill(pThis->yTagMin) && dasval_isFill(pThis->yTagMax)){
				pThis->yTagMin = 0.0;
				pThis->yTagMax = pThis->yTagInter * pThis->uItems;
			}
		}
	}
		
	return pThis;
}

/* ************************************************************************* */
/* Copy Constructor */
DasPlane* new_DasPlane_copy(const DasPlane* pThis){
	DasEnc* pEncode = new_DasEnc_copy(pThis->pEncoding);
	DasEnc* pYEncode = NULL;
	
	DasPlane* pOther = NULL;
	switch(pThis->planeType){
	case DASPLANETYPE_X:
	case DASPLANETYPE_Y:
	case DASPLANETYPE_Z:
		pOther = new_DasPlane(pThis->planeType, pThis->sName, pEncode, 
				                 pThis->units);
		break;
	case DASPLANETYPE_YSCAN:		
		if(pThis->ytag_spec == DASYTAGTYPE_SEQ){
			pOther = new_DasPlane_yscanSeq(
					pThis->sName, pEncode, pThis->units, pThis->uItems,
					pThis->yTagInter, pThis->yTagMin, pThis->yTagMax, 
					pThis->yTagUnits
			);
		}
		else{
			if(pThis->pYEncoding != NULL)
				pYEncode = new_DasEnc_copy(pThis->pYEncoding);
			
			pOther = new_DasPlane_yscan(
					pThis->sName, pEncode, pThis->units, pThis->uItems, pYEncode,
					pThis->pYTags, pThis->yTagUnits
			);
		}
		break;
	
	default:
		daserrf(17, "ERROR: Plane type %d is unknown\n", pThis->planeType);
		return NULL;
		break;
	}
	
	DasDesc_copyIn((DasDesc*)pOther, (DasDesc*)pThis);
	return pOther;
}

/* Destructor */
static void _del_DasPlane(DasDesc* pDesc)
{
	DasPlane* pThis = (DasPlane*)pDesc;
	DasDesc_freeProps(&(pThis->base));
	
	if(pThis->sName != NULL) free(pThis->sName);
	del_DasEnc(pThis->pEncoding);
	del_DasEnc(pThis->pYEncoding);
	
	if(pThis->pYTags != NULL) free(pThis->pYTags);
	if(pThis->bAlloccedBuf && pThis->pData != NULL) free(pThis->pData);
	
	free(pThis);
}

/* ************************************************************************* */
/* Equality check */
bool DasPlane_isEquivalent(const DasPlane* pThis, const DasPlane* pOther)
{
	if((pThis == NULL)||(pOther == NULL)) return false;
	if(pThis == pOther) return true;
	
	if(pThis->planeType != pOther->planeType ) return false;
	
	/* Independent planes don't need a data group, dependent ones do*/
	if((pThis->sName != NULL)&&(pOther->sName != NULL)){
		if(strcmp(pThis->sName, pOther->sName) != 0) return false;
	}
	else{
		if(pThis->sName != pOther->sName) return false;
	}
	
	if(! DasEnc_equals(pThis->pEncoding, pOther->pEncoding)) return false;
	if(pThis->units != pOther->units)return false;
	if(pThis->uItems != pOther->uItems)return false;
	
	/* Now check out the y-tags */
	if(pThis->planeType == DASPLANETYPE_YSCAN){
		if(pThis->ytag_spec != pOther->ytag_spec ) return false;
		if(! DasEnc_equals(pThis->pYEncoding, pOther->pYEncoding)) return false;
		
		if(pThis->ytag_spec == DASYTAGTYPE_LIST){
			for(size_t u = 0; u < pThis->uItems; ++u)
				if(pThis->pYTags[u] != pOther->pYTags[u]) return false;
		}
		else{
			if(pThis->ytag_spec == DASYTAGTYPE_SEQ){
				if(pThis->yTagInter != pOther->yTagInter) return false;
				if(pThis->yTagMin != pOther->yTagMin) return false;
				if(pThis->yTagMax != pOther->yTagMax) return false;
			}
		}
	}
	
	return true;
}

/* ************************************************************************* */
/* Helper for structure setters, if this plane's header is changed then let the 
 * parent know, so a writer can tell that the header it sent is out of date */
static void _pkt_header_changed(DasPlane* pThis){
	/* I'm casting away constant here for a special circumstance, in general
	 * this is not a good idea */
	
	DasDesc* pDesc = (DasDesc*) DasDesc_parent((DasDesc*)pThis);
	if(pDesc != NULL) ((DasPkt*)pDesc)->uHdrRev += 1;
}

/* ************************************************************************* */
/* Setting/Getting Values */

size_t DasPlane_numItems(const DasPlane* pThis ) {
    return pThis->uItems;
}

void DasPlane_setNumItems(DasPlane* pThis, size_t uItems)
{
	if(uItems == 0){
		daserrf(17, "All planes have at least one item.");
		return;
	}
	if(pThis->planeType != DASPLANETYPE_YSCAN){
		if(uItems == 1) return; /* okay, a do-nothing call */
		daserrf(17, "Only YScan planes may have more than 1 item");
		return;
	}
	
	if(pThis->uItems == uItems) return;
	
	_pkt_header_changed(pThis);
	
	/* Copy over existing ytags and then NAN the rest */
	double* pYTags = NULL;
	size_t u;
	if(pThis->pYTags != NULL){
		pYTags = pThis->pYTags;
		pThis->pYTags = (double*)calloc(uItems, sizeof(double));
		for(u = 0; u < uItems; u++){
			if(u < pThis->uItems) pThis->pYTags[u] = pYTags[u];
			else pThis->pYTags[u] = NAN;
		}
	}
	
	/* Better Damn well call setYTagSeries() after this !*/
	
	free(pThis->pData);
	pThis->pData = (double*)calloc(uItems, sizeof(double));
	pThis->uItems = uItems;
}


double DasPlane_valAt(const DasPlane* pThis, size_t uIdx)
{
	if(uIdx >= pThis->uItems){
		daserrf(17, "%s: Index %zu is out of range for %s plane", __func__,
				            uIdx, dasplanetype_name(pThis->planeType));
		return DASVAL_FILL;
	}
	
	return pThis->pData[uIdx];
}

const DasDatum* DasPlane_datumAt(
	const DasPlane* pThis, size_t uIdx, DasDatum* pD
){
	if(uIdx >= pThis->uItems)
		daserrf(17, "%s: Index %zu is out of range for %s plane", __func__,
				            uIdx, dasplanetype_name(pThis->planeType));
	else
		DasDatum_fromDouble(pD, pThis->pData[uIdx], pThis->units);
	
	return pD;
}


das_errcode DasPlane_setValAt(DasPlane* pThis, size_t uIdx, double value)
{
	if(uIdx >= pThis->uItems)
		return daserrf(17, "Index %zu is out of range for %s plane", uIdx,		
			            dasplanetype_name(pThis->planeType));
	
	/* make sure we set fill if that hasn't been done */
	if(!pThis->_bFillSet && (pThis->planeType != DASPLANETYPE_X)){
		pThis->rFill = DasPlane_fill(pThis);
		pThis->_bFillSet = true;
	}
	
	pThis->pData[uIdx] = value;
	return 0;
}

das_errcode DasPlane_setTimeAt(
	DasPlane* pThis, const char* sTime, size_t idx
){
	das_errcode nErr = 0;
	double rVal = DASVAL_FILL;
	
	if(idx >= pThis->uItems)
		return daserrf(17, "Index %zu in not value for %s plane", 
				            idx, dasplanetype_name(pThis->planeType));
	
	if(pThis->pEncoding->nCat != DAS2DT_TIME)
		return daserrf(17, "Plane data type is not a in the TIME category");
	
	/* My decoder normally has a fixed width, trick it for now and reset.
	   WARNING: this writes into the plane's own encoder, so two threads
	   setting values in the same plane at once will trip over each other.
	   Planes never share an encoder, so different streams are safe. */
	size_t uSvLen = pThis->pEncoding->nWidth;
	size_t uLen = strlen(sTime);
	if(uLen < 4)
		return daserrf(17, "Time string is too short to contain a valid time");
	
	pThis->pEncoding->nWidth = uLen;
	
	DasBuf db;
	_init_DasBuf_wrap(&db, (void*)sTime, uLen, "r");
	
	nErr = DasEnc_read(pThis->pEncoding, &db, pThis->units, &rVal);
	pThis->pEncoding->nWidth = uSvLen;
	
	if(nErr != 0) return nErr;
	
	pThis->pData[idx] = rVal;
	return 0;
}

const double* DasPlane_vals(const DasPlane* pThis)
{
	/* pretty simple... Function enforces const safety, that's about it */
	return pThis->pData;
}

void DasPlane_setVals(DasPlane* pThis, const double* pData ){
	
	/* make sure we set fill if that hasn't been done */
	if(!pThis->_bFillSet && (pThis->planeType != DASPLANETYPE_X)){
		pThis->rFill = DasPlane_fill(pThis);
		pThis->_bFillSet = true;
	}
	
	for(unsigned int u = 0; u<pThis->uItems; u++){
		pThis->pData[u] = pData[u];
	}
}

/* ************************************************************************* */
/* General Information */

das_planetype DasPlane_type(const DasPlane* pThis)
{
	return pThis->planeType;
}


double DasPlane_fill(const DasPlane* pThis){
	/* Here cascading gets a little annoying since we can't just have the  */
	/* keyword "fill" work anywhere, but the benefits outweigh the negatives */
	
	if(pThis->planeType == DASPLANETYPE_X){
		/* daserrf(17, "<x> planes should never have fill values\n"); */
		return DASVAL_FILL;
	}
	
	/* Break const correctness, D doesn't allow this since it's worried
	 * about multi-threading.  Basically I'm only doing this because
	 * I don't have easy access to virtual functions in C. */
	DasPlane* pVarThis = (DasPlane*)pThis;
	
	if(! pThis->_bFillSet && (pThis->planeType != DASPLANETYPE_X)){ 
	
		if(pVarThis->planeType == DASPLANETYPE_Y){
			if(DasDesc_has((DasDesc*)pVarThis, "yFill")){
				pVarThis->rFill = DasDesc_valDouble( (DasDesc*)pThis, "yFill");
				pVarThis->_bFillSet = true;
			}
		}
		
		if(pThis->planeType == DASPLANETYPE_YSCAN || pThis->planeType == DASPLANETYPE_Z){
			if(DasDesc_has((DasDesc*)pThis, "zFill")){
				pVarThis->rFill = DasDesc_valDouble((DasDesc*)pThis, "zFill");
				pVarThis->_bFillSet = true;
			}
		}
	
		/* Take the generic as a last resort */
		if((!pVarThis->_bFillSet)&&( DasDesc_has((DasDesc*)pThis, "fill") )) {
			pVarThis->rFill = DasDesc_valDouble( (DasDesc*)pThis, "fill" );
			pVarThis->_bFillSet = true;
		}
		
		if(! pVarThis->_bFillSet ) pVarThis->rFill = DASVAL_FILL;
	}
	
	pVarThis->_bFillSet = true;
	return pThis->rFill;
}

void DasPlane_setFill( DasPlane* pThis, double value ){
	_pkt_header_changed(pThis);

	pThis->_bFillSet = true;
	pThis->rFill = value;
	
	if(pThis->planeType == DASPLANETYPE_Y){
		DasDesc_setValDouble((DasDesc*)pThis, "yFill", value);
		return;
	}
	
	if(pThis->planeType == DASPLANETYPE_YSCAN || pThis->planeType == DASPLANETYPE_Z){
		DasDesc_setValDouble((DasDesc*)pThis, "zFill", value);
		return;
	}
	daserrf(17, "<x> planes don't have fill values");
}

/* Decoding and the first set call resolve the fill from the
   properties beforehand, see DasPlane_fill().  */
bool DasPlane_isFill(const DasPlane* pThis, double value)
{
	return ((pThis->rFill == 0.0) && (value == 0.0)) ||
	       (fabs((pThis->rFill - value)/pThis->rFill) < 0.00001);
}

const char* DasPlane_name(const  DasPlane* pThis ) {
	return pThis->sName;
}

void DasPlane_setName(DasPlane* pThis, const char* sName)
{
	size_t uOldLen = strlen(pThis->sName);
	size_t uNewLen = strlen(sName);
	if(sName == NULL){
		free(pThis->sName);
		pThis->sName = NULL;
	}
	else{
		if(uNewLen > uOldLen){
			pThis->sName = realloc(pThis->sName, uNewLen+1);
		}
		strncpy(pThis->sName, sName, uNewLen + 1);
	}
	_pkt_header_changed(pThis);
}

DasUnits DasPlane_units(const DasPlane* pThis ) {
	return pThis->units;
}

das_errcode DasPlane_setUnits(DasPlane* pThis, DasUnits units)
{
	if(units == NULL)
		return daserrf(DASERR_PLANE, "NULL units, use DASUNITS_DIMENSIONLESS for none");

	DasUnits newUnits = DasUnits_fromStr(units);
	if(newUnits == NULL)
		return daserrf(DASERR_PLANE, "Units '%s' can't be set on a plane", units);

	pThis->units = newUnits;
	_pkt_header_changed(pThis);
	return DAS_OKAY;
}

DasUnits DasPlane_yTagUnits(DasPlane* pThis) 
{
	if(pThis->planeType != DASPLANETYPE_YSCAN){
		daserrf(17, "getYTagUnits: plane is not a yscan!" );
	}
	return pThis->yTagUnits;
}

void DasPlane_setYTagUnits(DasPlane* pThis, DasUnits units)
{
	_pkt_header_changed(pThis);
	if(pThis->planeType != DASPLANETYPE_YSCAN){
		daserrf(17, "getYTagUnits: plane is not a yscan!" );
	}
	pThis->yTagUnits = units;
}

DasEnc* DasPlane_valEnc(DasPlane* pThis)
{
	return pThis->pEncoding;
}

void DasPlane_setValEnc(DasPlane* pThis, DasEnc* pEnc)
{
	del_DasEnc(pThis->pEncoding);
	pThis->pEncoding = pEnc;
	_pkt_header_changed(pThis);
}

das_ytagtype DasPlane_yTagType(const DasPlane* pThis)
{
	return pThis->ytag_spec;
}

double DasPlane_yTagInter(const DasPlane* pThis)
{
	return (pThis->ytag_spec == DASYTAGTYPE_SEQ) ? pThis->yTagInter : DASVAL_FILL;
}

double DasPlane_yTagMin(const DasPlane* pThis)
{
	return (pThis->ytag_spec == DASYTAGTYPE_SEQ) ? pThis->yTagMin : DASVAL_FILL;
}

const double* DasPlane_yTags(const DasPlane* pThis)
{
	if(pThis->planeType != DASPLANETYPE_YSCAN){
		daserrf(17, "getYTags: plane is not a yscan!" );
		return NULL;
	}
    
	return pThis->pYTags;
}

const double* DasPlane_makeYTags(DasPlane* pThis)
{
	ptrdiff_t n = 0;
	if(pThis->planeType != DASPLANETYPE_YSCAN){
		daserrf(17, "getYTags: plane is not a yscan!" );
		return NULL;
	}
   
	if((pThis->pYTags == NULL)&&(pThis->ytag_spec == DASYTAGTYPE_SEQ)){

		pThis->pYTags = (double*)calloc(pThis->uItems, sizeof(double));
		if(pThis->yTagMin != DASVAL_FILL){
			for(n = 0; n < pThis->uItems; ++n){
				pThis->pYTags[n] = pThis->yTagMin + (pThis->yTagInter)*n;
			}
		}
		else{
			for(n = pThis->uItems - 1; n >= 0; --n){
				pThis->pYTags[n] = pThis->yTagMax - (pThis->yTagInter)*n;
			}
		}
	}
	return pThis->pYTags;
}
		

void DasPlane_setYTags(DasPlane* pThis, const double* pYTags)
{
	if( pThis->planeType != DASPLANETYPE_YSCAN){
		daserrf(17, "getYTags: plane is not a yscan!" );
		return;
   }
	
	/* This can change the ytag_spec */
	size_t u;
	bool bSame = true;
	if(pThis->ytag_spec == DASYTAGTYPE_LIST){
		/* Before going nutty, see if they are just resetting existing YTags */
		for(u = 0; u < pThis->uItems; ++u){
			if(pThis->pYTags[u] != pYTags[u]){
				bSame = false;
				break;
			}
		}
		if(bSame) return;
	}
	else{
		pThis->ytag_spec = DASYTAGTYPE_LIST;
		pThis->pYTags = (double*)calloc(pThis->uItems, sizeof(double));
	}
	for(u = 0; u < pThis->uItems; ++u) pThis->pYTags[u] = pYTags[u];
	
	_pkt_header_changed(pThis);
}

void DasPlane_yTagSeq(
	const DasPlane* pThis, double* pInterval, double* pMin, double* pMax
){
	if(pThis->ytag_spec == DASYTAGTYPE_LIST){
		*pInterval = DASVAL_FILL;
		if(pMin != NULL) *pMin = DASVAL_FILL;
		if(pMax != NULL) *pMax = DASVAL_FILL;
		return;
	}
	*pInterval = pThis->yTagInter;
	if(pMin != NULL) *pMin = pThis->yTagMin;
	if(pMax != NULL) *pMax = pThis->yTagMax;
}

void DasPlane_setYTagSeq(
	DasPlane* pThis, double rInterval, double rMin, double rMax
){
	if(rInterval < 0.0 || dasval_isFill(rInterval)){
		daserrf(17, "Invalid value for rInterval");
		return;
	}
	
	if(pThis->ytag_spec == DASYTAGTYPE_LIST){
		_pkt_header_changed(pThis);
		if(pThis->pYTags != NULL){ 
			free(pThis->pYTags);
			pThis->pYTags = NULL;
		}
		pThis->ytag_spec = DASYTAGTYPE_SEQ;
	}
	
	/* Handle the do-nothing case */
	if(pThis->yTagInter == rInterval){ 
		if(!dasval_isFill(rMin) && (rMax = pThis->yTagMax)) return;
		if(!dasval_isFill(rMax) && (rMin = pThis->yTagMin)) return;
	}
	
	_pkt_header_changed(pThis);
	
	pThis->yTagInter = rInterval;
	if(dasval_isFill(rMin) && dasval_isFill(rMax)){
		pThis->yTagMin = 0.0;
		pThis->yTagMax = pThis->yTagInter * pThis->uItems;
	}
	else{
		if(dasval_isFill(rMin)){
			pThis->yTagMax = rMax;
			pThis->yTagMin = rMax - (pThis->yTagInter * pThis->uItems);
		}
		else{
			pThis->yTagMin = rMin;
			pThis->yTagMax = rMin + (pThis->yTagInter * pThis->uItems);
		}
	}
}

/* ************************************************************************* */
/* Encode/Decode Values */


das_errcode DasPlane_decodeData(const DasPlane* pThis, DasBuf* pBuf)
{
	unsigned int u =0;
	int nRet = 0; 
	
	/* make sure we set fill if that hasn't been done */
	if(!pThis->_bFillSet && (pThis->planeType != DASPLANETYPE_X)){
		DasPlane* pVarThis = (DasPlane*)pThis;
		pVarThis->rFill = DasPlane_fill(pThis);
		pVarThis->_bFillSet = true;
	}
	
	for(u = 0; u < pThis->uItems; u++){
		nRet = DasEnc_read(pThis->pEncoding, pBuf, pThis->units, pThis->pData + u);
		if(nRet != 0) return nRet;
	}
	return nRet;
}


das_errcode DasPlane_encodeData(DasPlane* pThis, DasBuf* pBuf, bool bLast)
{
	int nRet = 0;
	static const char cSpace = ' ';
	static const char cNL    = '\n';
	unsigned int u = 0;
	
	size_t uStart = DasBuf_written(pBuf);
	
	for(u = 0; u < pThis->uItems; u++){
		nRet = DasEnc_write(pThis->pEncoding, pBuf, pThis->pData[u], pThis->units);		
		if(nRet != 0) return nRet;
		
		/* For ascii encoding add a space after the value, unless it's the 
		   last one, then add a newline */
		if(pThis->pEncoding->nCat == DAS2DT_ASCII || 
		   pThis->pEncoding->nCat == DAS2DT_TIME){
			
			if(bLast && u == (pThis->uItems - 1))
				DasBuf_write(pBuf, &cNL, 1);
			else
				DasBuf_write(pBuf, &cSpace, 1);
		}
	}
	
	size_t uEnd = DasBuf_written(pBuf);
	
	/* Double check bytes written */
	int nTotal = pThis->pEncoding->nWidth * pThis->uItems;
	if((uEnd - uStart) != nTotal){
		return daserrf(
			17, "Packet length check error in DasPlane_encodeData:  Expected to "
			"encode %d bytes for <%s> plane, encoded %zu", nTotal, 
			dasplanetype_name(pThis->planeType), (uEnd - uStart));
	}
	
	return 0;
}



/* ************************************************************************* */
/* Serialize Out */

static const char* _DasPlane_unitStr(DasPlane* pThis){
	
	if(pThis->pEncoding->nCat == DAS2DT_TIME) 
		return DasUnits_toStr(DASUNITS_UTC);
	
	return (pThis->units != NULL) ? DasUnits_toStr(pThis->units) : "";
}


static das_errcode _DasPlane_encodeYScan(
	DasPlane* pThis, DasBuf* pBuf, const char* sIndent, const char* sSubIn,
	const char* sValType
){
	das_errcode nRet = 0;
	const char comma = ',';
	const char* sName = pThis->sName;
	if(sName == NULL) sName = "";
	
	nRet = DasBuf_printf(			
		pBuf, "%s<yscan name=\"%s\" type=\"%s\" zUnits=\"%s\" yUnits=\"%s\" "
		"nitems=\"%zu\" ", sIndent,  sName, sValType, _DasPlane_unitStr(pThis),
		(pThis->yTagUnits != NULL) ? DasUnits_toStr(pThis->yTagUnits) : "",
		pThis->uItems
	);
	if(nRet != 0) return nRet;
	
	if(pThis->ytag_spec != DASYTAGTYPE_SEQ){
	
		nRet = DasBuf_printf(pBuf, "\n%s       yTags=\"", sIndent);
		
		for(unsigned int u = 0; u<pThis->uItems; u++){
			if(u > 0){
				if( (nRet = DasBuf_write(pBuf, &comma, 1)) != 0) return nRet;
			}
			nRet = DasEnc_write(pThis->pYEncoding, pBuf, pThis->pYTags[u], pThis->yTagUnits);
			if(nRet != 0) return nRet;
		}
		
		nRet = DasBuf_printf(pBuf, "\">\n"); 
	}
	else{
		nRet = DasBuf_printf(pBuf, "yTagInterval=\"%.6e\" ", pThis->yTagInter);
		if(nRet != 0) return nRet;
		
		if( ! dasval_isFill(pThis->yTagMin)){
			if(pThis->yTagMin == 0.0)
				nRet = DasBuf_printf(pBuf, "yTagMin=\"0\" ");
			else
				nRet = DasBuf_printf(pBuf, "yTagMin=\"%.6e\" ", pThis->yTagMin);
			if(nRet != 0) return nRet;			
		}
		else{
			if( ! dasval_isFill(pThis->yTagMax)){
				nRet = DasBuf_printf(pBuf, "yTagMax=\"%.6e\" ", pThis->yTagMax);
				if(nRet != 0) return nRet;			
			}
		}
		
		nRet = DasBuf_printf(pBuf, " >\n"); 
	}
	
	
	if(nRet != 0) return nRet;
	
	nRet = DasDesc_encode2((DasDesc*)pThis, pBuf, sSubIn);
	if(nRet != 0) return nRet;
	
   return DasBuf_printf(pBuf, "%s</yscan>\n", sIndent);
}

static das_errcode _DasPlane_encodeX(
	DasPlane* pThis, DasBuf* pBuf, const char* sIndent, const char* sSubIn, 
	const char* sValType
){ 
	das_errcode nRet = 0;
	const char* sName = pThis->sName;
	
	/* Would like to go out on a limb here and just set the name to time if the
	 * units have a calendar representation, but reducers can't change the
	 * names of things or it really screws up autoplot */
	if(sName == NULL) sName = "";
	
	nRet = DasBuf_printf(pBuf, "%s<x name=\"%s\" type=\"%s\" units=\"%s\">\n",
			                  sIndent, sName, sValType, _DasPlane_unitStr(pThis));
	if(nRet != 0) return nRet;
	if((nRet = DasDesc_encode2((DasDesc*)pThis, pBuf, sSubIn)) != 0) return nRet;
	return DasBuf_printf(pBuf, "%s</x>\n", sIndent);
}

/* Little tricky here because Y can be either a dependent or independent var
   depending on if we are a X-Y-Z scatter dataset or not */
static das_errcode _DasPlane_encodeY(
	DasPlane* pThis, DasBuf* pBuf, const char* sIndent, const char* sSubIn, 
	const char* sValType
) {
	das_errcode nRet = 0;
	const char* sName = pThis->sName;
	if(sName == NULL) sName = "";
	
	nRet = DasBuf_printf(pBuf, "%s<y name=\"%s\" type=\"%s\" units=\"%s\">\n",
			                  sIndent, sName, sValType, _DasPlane_unitStr(pThis));

	if(nRet != 0) return nRet;
	if((nRet = DasDesc_encode2((DasDesc*)pThis, pBuf, sSubIn)) != 0) return nRet;
	return DasBuf_printf(pBuf, "%s</y>\n", sIndent);
}

static das_errcode _DasPlane_encodeZ(
	DasPlane* pThis, DasBuf* pBuf, const char* sIndent, const char* sSubIn, 
	const char* sValType
) {
	das_errcode nRet = 0;
	const char* sName = pThis->sName;
	if(sName == NULL) sName = "";
	
	nRet = DasBuf_printf(pBuf, "%s<z name=\"%s\" type=\"%s\" units=\"%s\">\n",
	                     sIndent, sName, sValType, _DasPlane_unitStr(pThis));
	if(nRet != 0) return nRet;
	if((nRet = DasDesc_encode2((DasDesc*)pThis, pBuf, sSubIn)) != 0) return nRet;
	return DasBuf_printf(pBuf, "%s</z>\n", sIndent);
}

das_errcode DasPlane_encode(
	DasPlane* pThis, DasBuf* pBuf, const char* sIndent
){	
	char sValType[24] = {'\0'};
	DasEnc_toStr(pThis->pEncoding, sValType, 24);
	
	char sSubIndent[64] = {'\0'};
	snprintf(sSubIndent, 63, "%s  ", sIndent);
	
	switch(pThis->planeType){
	case DASPLANETYPE_X:
		return _DasPlane_encodeX(pThis, pBuf, sIndent, sSubIndent, sValType);
	case DASPLANETYPE_Y:
		return _DasPlane_encodeY(pThis, pBuf, sIndent, sSubIndent, sValType);
	case DASPLANETYPE_Z:
		return _DasPlane_encodeZ(pThis, pBuf, sIndent, sSubIndent, sValType);
	case DASPLANETYPE_YSCAN:
		return _DasPlane_encodeYScan(pThis, pBuf, sIndent, sSubIndent, sValType);
	default:
		; /* Make the compiler happy */
	}
	return daserrf(17, "Code Change: Update DasPlane_encode");
}


