/* Copyright (C) 2004-2017 Jeremy Faden <jeremy-faden@uiowa.edu>
 *                         Chris Piker <chris-piker@uiowa.edu>
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

#include <math.h>
#include <string.h>
#include <stdio.h>
#include <ctype.h>
#include <stddef.h>

#include "util.h"
#include "encoding.h"
#include "value.h"
#include "units.h"
#include "packet.h"


#define _IDX_YTAG_INT 0
#define _IDX_YTAG_MIN 1
#define _IDX_YTAG_MAX 2

/* ************************************************************************* */
/* PlaneType info */

plane_type_t str2PlaneType( const char * type ) {
    if( strcmp( type, "x" )==0 )     return X;
    if( strcmp( type, "y" )==0 )     return Y;
	 if( strcmp( type, "yscan" )==0 ) return YScan;
	 if( strcmp( type, "z" )==0 )     return Z;
    
	 das_error(17, "getPlaneType: unrecognized type %s\n", type );
	 return -1; /* Never reached, making compiler happy */
}

const char * PlaneType_toStr( plane_type_t type ) {
	switch(type){
	case X:     return "x";
	case YScan: return "yscan";
	case Y:     return "y";
	case Z:     return "z";
	default:
		das_error(17, "getPlaneTypeString: unrecognized type: %d\n", type );
	}
	return NULL; /* Never reached, making compiler happy */
}

/* ************************************************************************* */
/* Construction/Destruction */

/* Empty Constructor */
PlaneDesc* new_PlaneDesc_empty() {
	PlaneDesc* pThis = (PlaneDesc*)calloc(1, sizeof(PlaneDesc));
	
	DasDesc_init((DasDesc*)pThis, PLANE);
	pThis->planeType = Invalid;
   pThis->bPlaneDataValid = false;
	pThis->rFill = DAS_FILL_VALUE;
	pThis->_bFillSet = false;
	return pThis;
}

/* Generic Constructor */
PlaneDesc* new_PlaneDesc(
	plane_type_t pt, const char* sGroup, DasEncoding* pType, das_units units)
{
	PlaneDesc* pThis = (PlaneDesc*)calloc(1, sizeof(PlaneDesc));

	DasDesc_init((DasDesc*)pThis, PLANE);
	
	pThis->planeType = pt;
	if(sGroup != NULL){
		pThis->sName = calloc(strlen(sGroup)+1, sizeof(char));
		strncpy(pThis->sName, sGroup, strlen(sGroup));
	}
	
	pThis->units = units;
	pThis->uItems = 1;
	pThis->pData = &pThis->value;
	pThis->value = DAS_FILL_VALUE;
	pThis->rFill = DAS_FILL_VALUE;
	pThis->_bFillSet = false;
	
	pThis->yTagInter = DAS_FILL_VALUE;
	pThis->yTagMin   = DAS_FILL_VALUE;
	pThis->yTagMax   = DAS_FILL_VALUE;
	
	pThis->pEncoding = pType;
		
   pThis->bPlaneDataValid = false;
	return pThis;
}

/* YScan Constructor */
PlaneDesc* new_PlaneDesc_yscan(
	const char* sGroup, DasEncoding* pZType, das_units zUnits, size_t uItems, 
	DasEncoding* pYType, const double* pYTags, das_units yUnits
){
	PlaneDesc* pThis;
	pThis = new_PlaneDesc(YScan,sGroup,pZType,zUnits);
	int nWidth = 0;
	
	if(uItems < 1)
		das_error(17, "Must have at least 1 item in a yscan");
	
	pThis->uItems = uItems;
	pThis->yTagUnits = yUnits;
	pThis->pData = (double*)calloc(pThis->uItems, sizeof(double));
	
	for(unsigned int u = 0; u < pThis->uItems; u++)
		pThis->pData[u] = DAS_FILL_VALUE;
	pThis->bAlloccedBuf = true;
	
	pThis->pYTags = (double*)calloc(pThis->uItems, sizeof(double));
	if(pYTags != NULL){
		for(unsigned int u = 0; u < pThis->uItems; u++) 
			pThis->pYTags[u] = pYTags[u];
		
		if(pYType != NULL){ 
			if(pYType->nCat == DAS2DT_BE_REAL || pYType->nCat == DAS2DT_LE_REAL){
				das_error(17, "Binary encodings can't be used for YTags values, "
						"cause they end up in XML headers.");
				return NULL;
			}
				
			pThis->pYEncoding = pYType;
		}		
		else{
			pThis->pYEncoding = new_DasEncoding(DAS2DT_ASCII, 12, NULL);
		}
		
	}
	else{
		for(unsigned int u = 0; u < pThis->uItems; u++)	pThis->pYTags[u] = u;
		
		if(pYType != NULL){
			pThis->pYEncoding = pYType;
		}
		else{
			nWidth = (int) ceil( log10(pThis->uItems+1) ) + 1;
			pThis->pYEncoding = new_DasEncoding(DAS2DT_ASCII, nWidth, "%.0f");
		}
	}
	
	pThis->ytag_spec = ytags_list;
	pThis->yTagInter = DAS_FILL_VALUE;
	pThis->yTagMin   = DAS_FILL_VALUE;
	pThis->yTagMax   = DAS_FILL_VALUE;
	
	return pThis;	
}

PlaneDesc* new_PlaneDesc_yscan_series(
	const char* sGroup, DasEncoding* pZType, das_units zUnits, size_t uItems,
   double yTagInter, double yTagMin, double yTagMax, das_units yUnits
){
	if(uItems < 1){
		das_error(17, "Must have at least 1 item in a yscan");
		return NULL;
	}
	if(yTagInter <= 0.0){
		das_error(17, "YTag series interval must be greater than 0");
		return NULL;
	}
	
	PlaneDesc* pThis;
	pThis = new_PlaneDesc(YScan,sGroup,pZType,zUnits);
		
	pThis->uItems = uItems;
	pThis->yTagUnits = yUnits;
	pThis->pData = (double*)calloc(pThis->uItems, sizeof(double));
	
	for(unsigned int u = 0; u < pThis->uItems; u++)
		pThis->pData[u] = DAS_FILL_VALUE;
	pThis->bAlloccedBuf = true;
	
	pThis->ytag_spec = ytags_series;
	
	pThis->yTagInter = yTagInter;
	
	if(isDas2Fill(yTagMin) && isDas2Fill(yTagMax)){
		pThis->yTagMin = 0.0;
		pThis->yTagMax = pThis->yTagInter * pThis->uItems;
	}
	else{
		if(isDas2Fill(yTagMin)){
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


/* Helper for trimming zeros after decimal */
void _trimTrailingZeros(char* sVal){
		
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

/* Helper for new_PlaneDesc_pairs, tries to determine a reasonable
   encoding for the ytags values, assumes nItems is already valid */
DasErrCode _PlaneDesc_decodeYTags(PlaneDesc* pThis, const char* sYTags)
{
	int nCat = DAS2DT_ASCII;
	char sFmt[64] = {'\0'};
	DasEncoding* pEnc = NULL;
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
		return das_error(17, "Number of YTag values (%d) is not equal to the "
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
		_trimTrailingZeros(sVal);
			
		/* Convert it to a double */
		if( sVal[0] != '\0'){ 
			if( sscanf(sVal, "%lf", pThis->pYTags + u) != 1)
				return das_error(17, "Couldn't parse YTag value '%s'", sVal);
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
				return das_error(17, "Time values in YTags are not yet "
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
	    
	if( (pEnc = new_DasEncoding(nCat, nFmtWidth+1, sFmt)) == NULL) return 17;
	pThis->pYEncoding = pEnc;
	
	return 0;
}

/* ************************************************************************* */
/* Construct from XML data */
PlaneDesc* new_PlaneDesc_pairs(
	DasDesc* pParent, plane_type_t pt, const char** attr
){
	PlaneDesc* pThis = (PlaneDesc*)calloc(1, sizeof(PlaneDesc));
	
	const char* sYTags = NULL;
	
	DasDesc_init((DasDesc*)pThis, PLANE);
	pThis->base.parent = pParent;
	pThis->planeType = pt;
	
	pThis->ytag_spec = ytags_none;
	pThis->yTagInter = DAS_FILL_VALUE;
	pThis->yTagMin   = DAS_FILL_VALUE;
	pThis->yTagMax   = DAS_FILL_VALUE;
	
	if(pt != X && pt != Y && pt != YScan && pt != Z){
		das_error(17, "Invalid plane type %d", pt);
		free(pThis);
		return NULL;
	}

	int i;
	
	/* Preprocess to get the encoding first, it affects the interpretation of
	 * the type value */
	for (i=0; attr[i]; i+=2) {
		if ( strcmp( attr[i], "type" )==0 ) {
			pThis->pEncoding= new_DasEncoding_str((char *)attr[i+1]);
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
			
			if(Units_fromStr(attr[i+1]) == UNIT_UTC){
				if((pThis->pEncoding != NULL)&&(pThis->pEncoding->nCat == DAS2DT_TIME))
					pThis->units = UNIT_US2000;	
			}
			else
				pThis->units= Units_fromStr( attr[i+1] );
		}
	}
	
	if(pThis->pEncoding == NULL){
		das_error(17, "Data 'type' attribute missing from plane description");
		return NULL;
	}
	
	/* Additional processing by plane type */
	switch(pThis->planeType){			
	case X:
	case Y:
	case Z:
		pThis->uItems= 1;
		pThis->pData = &(pThis->value);
		pThis->bAlloccedBuf = false;
		break;
	
	case YScan:
		for (i=0; attr[i]; i+=2){
								  
			if ( strcmp(attr[i], "nitems")==0 ) {
				if(sscanf(attr[i+1], "%zu", &pThis->uItems) != 1) {
					das_error(17, "Couldn't convert %s to a positive integer %s",
					           attr[i+1]);
					return NULL;
				}
				
				/* Assuming 6 digits to store sizes, no <x> plane and smallest
				   encoding of 4-bytes/value the largest number of items is 
					249999 */
				if(pThis->uItems > 249999){
					das_error(17, "Max number of supported items in a Das2 stream"
					           " is 249999\n");
					return NULL;
				}
				
				pThis->pData = (double*)calloc(pThis->uItems, sizeof(double));
				pThis->bAlloccedBuf = true;
				continue;
			} 
			if ( strcmp(attr[i], "zUnits")==0 ) {
				pThis->units= Units_fromStr( attr[i+1] );
				continue;
			} 
			if ( strcmp( attr[i], "yTags" )==0 ) {
				sYTags = attr[i+1];
				pThis->ytag_spec = ytags_list;
				continue;			
			}
			if ( strcmp( attr[i], "yTagInterval" )==0 ) {
				
				if( (! das_str2double(attr[i+1], &(pThis->yTagInter))) ||
				    (pThis->yTagInter <= 0.0) ){
					das_error(17, "Couldn't convert %s to a real positive number",
							         attr[i+1]);
					return NULL;
				}
				pThis->ytag_spec = ytags_series;
				continue;			
			}
			if ( strcmp( attr[i], "yTagMin" )==0 ) {
				if(! das_str2double(attr[i+1], &(pThis->yTagMin))){
					das_error(17, "Couldn't convert %s to a real number", attr[i+1]);
					return NULL;
				}
				continue;			
			}
			if ( strcmp( attr[i], "yTagMax" )==0 ) {
				if(! das_str2double(attr[i+1], &(pThis->yTagMax))){
					das_error(17, "Couldn't convert %s to a real number", attr[i+1]);
					return NULL;
				}
				continue;			
			}
			if ( strcmp( attr[i], "yUnits" )==0 ) {
				pThis->yTagUnits= Units_fromStr( attr[i+1] );
				continue;
			}					
		}
	   break;
	default:
		das_error(17, "Code Change caused error in new_PlaneDesc_pairs");
		return NULL;
	}
	
	/* Some checks for required items */
	if(pThis->uItems < 1){
		das_error(17, "Illegal number of items, %d, in %s plane", 
		                pThis->uItems, PlaneType_toStr(pt));
		return NULL;
	}
	
	/* Have to have a units string, unless these are time values then
	   the units string will be set internally to us2000 */
	if(pThis->units == NULL){
		if(pThis->pEncoding->nCat == DAS2DT_TIME){
			pThis->units = UNIT_US2000;
		}
		else{
			das_error(17, "Units element missing in plane description");
			return NULL;
		}
	}
	
	/* handle the ytags array now that both tags should be present */
	if(pThis->planeType == YScan){
		if(pThis->ytag_spec != ytags_series){
			if( _PlaneDesc_decodeYTags(pThis, sYTags) != 0) return NULL;
		}
		else{	
			/* For series yTags, have at least one of yTagMin or yTagMax */
			if( isDas2Fill(pThis->yTagMin) && isDas2Fill(pThis->yTagMax)){
				pThis->yTagMin = 0.0;
				pThis->yTagMax = pThis->yTagInter * pThis->uItems;
			}
		}
	}
		
	return pThis;
}

/* ************************************************************************* */
/* Copy Constructor */
PlaneDesc* PlaneDesc_copy(const PlaneDesc* pThis){
	DasEncoding* pEncode = DasEnc_copy(pThis->pEncoding);
	DasEncoding* pYEncode = NULL;
	
	PlaneDesc* pOther = NULL;
	switch(pThis->planeType){
	case X:
	case Y:
	case Z:
		pOther = new_PlaneDesc(pThis->planeType, pThis->sName, pEncode, 
				                 pThis->units);
		break;
	case YScan:		
		if(pThis->ytag_spec == ytags_series){
			pOther = new_PlaneDesc_yscan_series(
					pThis->sName, pEncode, pThis->units, pThis->uItems,
					pThis->yTagInter, pThis->yTagMin, pThis->yTagMax, 
					pThis->yTagUnits
			);
		}
		else{
			if(pThis->pYEncoding != NULL)
				pYEncode = DasEnc_copy(pThis->pYEncoding);
			
			pOther = new_PlaneDesc_yscan(
					pThis->sName, pEncode, pThis->units, pThis->uItems, pYEncode,
					pThis->pYTags, pThis->yTagUnits
			);
		}
		break;
	
	default:
		das_error(17, "ERROR: Plane type %d is unknown\n", pThis->planeType);
		return NULL;
		break;
	}
	
	DasDesc_copyIn((DasDesc*)pOther, (DasDesc*)pThis);
	return pOther;
}

/* Destructor */
void del_PlaneDesc(PlaneDesc* pThis)
{
	DasDesc_freeProps(&(pThis->base));
	
	if(pThis->sName != NULL) free(pThis->sName);
	if(pThis->pEncoding != NULL) free(pThis->pEncoding);
	if(pThis->pYEncoding != NULL) free(pThis->pYEncoding);
	
	if(pThis->pYTags != NULL) free(pThis->pYTags);
	if(pThis->bAlloccedBuf && pThis->pData != NULL) free(pThis->pData);
	
	free(pThis);
}

/* ************************************************************************* */
/* Equality check */
bool PlaneDesc_equivalent(const PlaneDesc* pThis, const PlaneDesc* pOther)
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
	if(pThis->planeType == YScan){
		if(pThis->ytag_spec != pOther->ytag_spec ) return false;
		if(! DasEnc_equals(pThis->pYEncoding, pOther->pYEncoding)) return false;
		
		if(pThis->ytag_spec == ytags_list){
			for(size_t u = 0; u < pThis->uItems; ++u)
				if(pThis->pYTags[u] != pOther->pYTags[u]) return false;
		}
		else{
			if(pThis->ytag_spec == ytags_series){
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
 * parent know that a valid encoding has not been written */
void _pkt_header_not_sent(PlaneDesc* pThis){
	/* I'm casting away constant here for a special circumstance, in general
	 * this is not a good idea */
	
	DasDesc* pDesc = (DasDesc*) DasDesc_parent((DasDesc*)pThis);
	if(pDesc != NULL) ((PktDesc*)pDesc)->bSentHdr = false;
}

/* ************************************************************************* */
/* Setting/Getting Values */

size_t PlaneDesc_getNItems(const PlaneDesc* pThis ) {
    return pThis->uItems;
}

void PlaneDesc_setNItems(PlaneDesc* pThis, size_t uItems)
{
	if(uItems == 0){
		das_error(17, "All planes have at least one item.");
		return;
	}
	if(pThis->planeType != YScan){
		if(uItems == 1) return; /* okay, a do-nothing call */
		das_error(17, "Only YScan planes may have more than 1 item");
		return;
	}
	
	if(pThis->uItems == uItems) return;
	
	_pkt_header_not_sent(pThis);
	
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

ytag_spec_t PlaneDesc_getYTagSpec(const PlaneDesc* pThis) {
	return pThis->ytag_spec;
}


double PlaneDesc_getValue(const PlaneDesc* pThis, size_t uIdx)
{
	if(uIdx >= pThis->uItems){
		das_error(17, "%s: Index %s is out of range for %s plane", __func__,
				            PlaneType_toStr(pThis->planeType));
		return DAS_FILL_VALUE;
	}
	
	return pThis->pData[uIdx];
}

const das_datum* PlaneDesc_getDatum(
	const PlaneDesc* pThis, size_t uIdx, das_datum* pD
){
	if(uIdx >= pThis->uItems)
		das_error(17, "%s: Index %s is out of range for %s plane", __func__,
				            PlaneType_toStr(pThis->planeType));
	else
		das_datum_fromDbl(pD, pThis->pData[uIdx], pThis->units);
	
	return pD;
}


DasErrCode PlaneDesc_setValue(PlaneDesc* pThis, size_t uIdx, double value)
{
	if(uIdx >= pThis->uItems)
		return das_error(17, "Index %zu is out of range for %s plane", uIdx,		
			            PlaneType_toStr(pThis->planeType));
	
	/* make sure we set fill if that hasn't been done */
	if(!pThis->_bFillSet && (pThis->planeType != X)){
		pThis->rFill = PlaneDesc_getFill(pThis);
		pThis->_bFillSet = true;
	}
	
	pThis->pData[uIdx] = value;
	return 0;
}

DasErrCode PlaneDesc_setTimeValue(
	PlaneDesc* pThis, const char* sTime, size_t idx
){
	DasErrCode nErr = 0;
	double rVal = DAS_FILL_VALUE;
	
	if(idx >= pThis->uItems)
		return das_error(17, "Index %zu in not value for %s plane", 
				            idx, PlaneType_toStr(pThis->planeType));
	
	if(pThis->pEncoding->nCat != DAS2DT_TIME)
		return das_error(17, "Plane data type is not a in the TIME category");
	
	/* My decoder normally has a fixed width, trick it for now and reset */
	size_t uSvLen = pThis->pEncoding->nWidth;
	size_t uLen = strlen(sTime);
	if(uLen < 4)
		return das_error(17, "Time string is too short to contain a valid time");
	
	pThis->pEncoding->nWidth = uLen;
	
	DasBuf db;
	DasBuf_initReadOnly(&db, sTime, uLen);
	
	nErr = DasEnc_read(pThis->pEncoding, &db, pThis->units, &rVal);
	pThis->pEncoding->nWidth = uSvLen;
	
	if(nErr != 0) return nErr;
	
	pThis->pData[idx] = rVal;
	return 0;
}

const double* PlaneDesc_getValues(const PlaneDesc* pThis)
{
	/* pretty simple... Function enforces const safety, that's about it */
	return pThis->pData;
}

void PlaneDesc_setValues(PlaneDesc* pThis, const double* pData ){
	
	/* make sure we set fill if that hasn't been done */
	if(!pThis->_bFillSet && (pThis->planeType != X)){
		pThis->rFill = PlaneDesc_getFill(pThis);
		pThis->_bFillSet = true;
	}
	
	for(unsigned int u = 0; u<pThis->uItems; u++){
		pThis->pData[u] = pData[u];
	}
}

/* ************************************************************************* */
/* General Information */

plane_type_t PlaneDesc_getType(const PlaneDesc* pThis)
{
	return pThis->planeType;
}


double PlaneDesc_getFill(const PlaneDesc* pThis){
	/* Here cascading gets a little annoying since we can't just have the  */
	/* keyword "fill" work anywhere, but the benefits outweigh the negatives */
	
	if(pThis->planeType == X){
		/* das_error(17, "<x> planes should never have fill values\n"); */
		return getDas2Fill();
	}
	
	/* Break const correctness, D doesn't allow this since it's worried
	 * about multi-threading.  Basically I'm only doing this because
	 * I don't have easy access to virtual functions in C. */
	PlaneDesc* pVarThis = (PlaneDesc*)pThis;
	
	if(! pThis->_bFillSet && (pThis->planeType != X)){ 
	
		if(pVarThis->planeType == Y){
			if(DasDesc_has((DasDesc*)pVarThis, "yFill")){
				pVarThis->rFill = DasDesc_getDouble( (DasDesc*)pThis, "yFill");
				pVarThis->_bFillSet = true;
			}
		}
		
		if(pThis->planeType == YScan || pThis->planeType == Z){
			if(DasDesc_has((DasDesc*)pThis, "zFill")){
				pVarThis->rFill = DasDesc_getDouble((DasDesc*)pThis, "zFill");
				pVarThis->_bFillSet = true;
			}
		}
	
		/* Take the generic as a last resort */
		if((!pVarThis->_bFillSet)&&( DasDesc_has((DasDesc*)pThis, "fill") )) {
			pVarThis->rFill = DasDesc_getDouble( (DasDesc*)pThis, "fill" );
			pVarThis->_bFillSet = true;
		}
		
		if(! pVarThis->_bFillSet ) pVarThis->rFill = getDas2Fill();
	}
	
	pVarThis->_bFillSet = true;
	return pThis->rFill;
}

void PlaneDesc_setFill( PlaneDesc* pThis, double value ){
	_pkt_header_not_sent(pThis);

	pThis->_bFillSet = true;
	pThis->rFill = value;
	
	if(pThis->planeType == Y){
		DasDesc_setDouble((DasDesc*)pThis, "yFill", value);
		return;
	}
	
	if(pThis->planeType == YScan || pThis->planeType == Z){
		DasDesc_setDouble((DasDesc*)pThis, "zFill", value);
		return;
	}
	das_error(17, "<x> planes don't have fill values");
}

/* Retained for older code that needs an actual call site, but
   the macro is much faster */
/*bool PlaneDesc_isFill(const  PlaneDesc* pThis, double value ) {
	if(pThis->planeType == X) return false;
	
	double fill = PlaneDesc_getFill(pThis);
	if(fill == 0.0 && value == 0.0) return true;
	return fabs((fill-value)/fill)<0.00001;
}
*/

const char* PlaneDesc_getName(const  PlaneDesc* pThis ) {
	return pThis->sName;
}

void PlaneDesc_setName(PlaneDesc* pThis, const char* sName)
{
	size_t uOldLen = strlen(pThis->sName);
	size_t uNewLen = strlen(sName);
	if(sName == NULL){
		free(pThis->sName);
		pThis->sName = NULL;
	}
	else{
		if(uNewLen > uOldLen){
			pThis->sName = realloc(pThis->sName, uNewLen);
		}
		strncpy(pThis->sName, sName, uNewLen + 1);
	}
	_pkt_header_not_sent(pThis);
}

das_units PlaneDesc_getUnits(const PlaneDesc* pThis ) {
	return pThis->units;
}

void PlaneDesc_setUnits(PlaneDesc* pThis, das_units units)
{
	pThis->units = Units_fromStr(units);
	_pkt_header_not_sent(pThis);
}

das_units PlaneDesc_getYTagUnits(PlaneDesc* pThis) 
{
	if(pThis->planeType != YScan){
		das_error(17, "getYTagUnits: plane is not a yscan!" );
	}
	return pThis->yTagUnits;
}

void PlaneDesc_setYTagUnits(PlaneDesc* pThis, das_units units)
{
	_pkt_header_not_sent(pThis);
	if(pThis->planeType != YScan){
		das_error(17, "getYTagUnits: plane is not a yscan!" );
	}
	pThis->yTagUnits = units;
}

DasEncoding* PlaneDesc_getValEncoder(PlaneDesc* pThis)
{
	return pThis->pEncoding;
}

void PlaneDesc_setValEncoder(PlaneDesc* pThis, DasEncoding* pEnc)
{
	if(pThis->pEncoding != NULL) free(pThis->pEncoding);
	pThis->pEncoding = pEnc;
	_pkt_header_not_sent(pThis);
}

double PlaneDesc_getYTagInterval(const PlaneDesc* pThis){
	if(pThis->ytag_spec == ytags_series) return pThis->yTagInter;
	return DAS_FILL_VALUE;
}

const double* PlaneDesc_getYTags(const PlaneDesc* pThis)
{
	if(pThis->planeType != YScan){
		das_error(17, "getYTags: plane is not a yscan!" );
		return NULL;
	}
    
	return pThis->pYTags;
}

const double* PlaneDesc_getOrMakeYTags(PlaneDesc* pThis)
{
	ptrdiff_t n = 0;
	if(pThis->planeType != YScan){
		das_error(17, "getYTags: plane is not a yscan!" );
		return NULL;
	}
   
	if((pThis->pYTags == NULL)&&(pThis->ytag_spec == ytags_series)){

		pThis->pYTags = (double*)calloc(pThis->uItems, sizeof(double));
		if(pThis->yTagMin != DAS_FILL_VALUE){
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
		

void PlaneDesc_setYTags(PlaneDesc* pThis, const double* pYTags)
{
	if( pThis->planeType != YScan){
		das_error(17, "getYTags: plane is not a yscan!" );
		return;
   }
	
	/* This can change the ytag_spec */
	size_t u;
	bool bSame = true;
	if(pThis->ytag_spec == ytags_list){
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
		pThis->ytag_spec = ytags_list;
		pThis->pYTags = (double*)calloc(pThis->uItems, sizeof(double));
	}
	for(u = 0; u < pThis->uItems; ++u) pThis->pYTags[u] = pYTags[u];
	
	_pkt_header_not_sent(pThis);
}

void PlaneDesc_getYTagSeries(
	const PlaneDesc* pThis, double* pInterval, double* pMin, double* pMax
){
	if(pThis->ytag_spec == ytags_list){
		*pInterval = DAS_FILL_VALUE;
		if(pMin != NULL) *pMin = DAS_FILL_VALUE;
		if(pMax != NULL) *pMax = DAS_FILL_VALUE;
	}
	*pInterval = pThis->yTagInter;
	if(pMin != NULL) *pMin = pThis->yTagMin;
	if(pMax != NULL) *pMax = pThis->yTagMax;
}

void PlaneDesc_setYTagSeries(
	PlaneDesc* pThis, double rInterval, double rMin, double rMax
){
	if(rInterval < 0.0 || isDas2Fill(rInterval)){
		das_error(17, "Invalid value for rInterval");
		return;
	}
	
	if(pThis->ytag_spec == ytags_list){
		_pkt_header_not_sent(pThis);
		if(pThis->pYTags != NULL){ 
			free(pThis->pYTags);
			pThis->pYTags = NULL;
		}
		pThis->ytag_spec = ytags_series;
	}
	
	/* Handle the do-nothing case */
	if(pThis->yTagInter == rInterval){ 
		if(!isDas2Fill(rMin) && (rMax = pThis->yTagMax)) return;
		if(!isDas2Fill(rMax) && (rMin = pThis->yTagMin)) return;
	}
	
	_pkt_header_not_sent(pThis);
	
	pThis->yTagInter = rInterval;
	if(isDas2Fill(rMin) && isDas2Fill(rMax)){
		pThis->yTagMin = 0.0;
		pThis->yTagMax = pThis->yTagInter * pThis->uItems;
	}
	else{
		if(isDas2Fill(rMin)){
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


DasErrCode PlaneDesc_decodeData(const PlaneDesc* pThis, DasBuf* pBuf)
{
	unsigned int u =0;
	int nRet = 0; 
	
	/* make sure we set fill if that hasn't been done */
	if(!pThis->_bFillSet && (pThis->planeType != X)){
		PlaneDesc* pVarThis = (PlaneDesc*)pThis;
		pVarThis->rFill = PlaneDesc_getFill(pThis);
		pVarThis->_bFillSet = true;
	}
	
	for(u = 0; u < pThis->uItems; u++){
		nRet = DasEnc_read(pThis->pEncoding, pBuf, pThis->units, pThis->pData + u);
		if(nRet != 0) return nRet;
	}
	return nRet;
}


DasErrCode PlaneDesc_encodeData(PlaneDesc* pThis, DasBuf* pBuf, bool bLast)
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
		return das_error(
			17, "Packet length check error in PlaneDesc_encodeData:  Expected to "
			"encode %d bytes for <%s> plane, encoded %d", nTotal, 
			PlaneType_toStr(pThis->planeType), (uEnd - uStart));
	}
	
	return 0;
}



/* ************************************************************************* */
/* Serialize Out */

const char* _PlaneDesc_unitStr(PlaneDesc* pThis){
	
	if(pThis->pEncoding->nCat == DAS2DT_TIME) 
		return Units_toStr(UNIT_UTC);
	
	return Units_toStr(pThis->units);
}


DasErrCode _PlaneDesc_encodeYScan(
	PlaneDesc* pThis, DasBuf* pBuf, const char* sIndent, const char* sSubIn,
	const char* sValType
){
	DasErrCode nRet = 0;
	const char comma = ',';
	const char* sName = pThis->sName;
	if(sName == NULL) sName = "";
	
	nRet = DasBuf_printf(			
		pBuf, "%s<yscan name=\"%s\" type=\"%s\" zUnits=\"%s\" yUnits=\"%s\" "
		"nitems=\"%zu\" ", sIndent,  sName, sValType, _PlaneDesc_unitStr(pThis),
		Units_toStr(pThis->yTagUnits), pThis->uItems
	);
	if(nRet != 0) return nRet;
	
	if(pThis->ytag_spec != ytags_series){
	
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
		
		if( ! isDas2Fill(pThis->yTagMin)){
			if(pThis->yTagMin == 0.0)
				nRet = DasBuf_printf(pBuf, "yTagMin=\"0\" ");
			else
				nRet = DasBuf_printf(pBuf, "yTagMin=\"%.6e\" ", pThis->yTagMin);
			if(nRet != 0) return nRet;			
		}
		else{
			if( ! isDas2Fill(pThis->yTagMax)){
				nRet = DasBuf_printf(pBuf, "yTagMax=\"%.6e\" ", pThis->yTagMax);
				if(nRet != 0) return nRet;			
			}
		}
		
		nRet = DasBuf_printf(pBuf, " >\n"); 
	}
	
	
	if(nRet != 0) return nRet;
	
	nRet = DasDesc_encode((DasDesc*)pThis, pBuf, sSubIn);
	if(nRet != 0) return nRet;
	
   return DasBuf_printf(pBuf, "%s</yscan>\n", sIndent);
}

DasErrCode _PlaneDesc_encodeX(
	PlaneDesc* pThis, DasBuf* pBuf, const char* sIndent, const char* sSubIn, 
	const char* sValType
){ 
	DasErrCode nRet = 0;
	const char* sName = pThis->sName;
	
	/* Would like to go out on a limb here and just set the name to time if the
	 * units have a calendar representation, but reducers can't change the
	 * names of things or it really screws up autoplot */
	if(sName == NULL) sName = "";
	
	nRet = DasBuf_printf(pBuf, "%s<x name=\"%s\" type=\"%s\" units=\"%s\">\n",
			                  sIndent, sName, sValType, _PlaneDesc_unitStr(pThis));
	if(nRet != 0) return nRet;
	if((nRet = DasDesc_encode((DasDesc*)pThis, pBuf, sSubIn)) != 0) return nRet;
	return DasBuf_printf(pBuf, "%s</x>\n", sIndent);
}

/* Little tricky here because Y can be either a dependent or independent var
   depending on if we are a X-Y-Z scatter dataset or not */
DasErrCode _PlaneDesc_encodeY(
	PlaneDesc* pThis, DasBuf* pBuf, const char* sIndent, const char* sSubIn, 
	const char* sValType
) {
	DasErrCode nRet = 0;
	const char* sName = pThis->sName;
	if(sName == NULL) sName = "";
	
	nRet = DasBuf_printf(pBuf, "%s<y name=\"%s\" type=\"%s\" units=\"%s\">\n",
			                  sIndent, sName, sValType, _PlaneDesc_unitStr(pThis));

	if(nRet != 0) return nRet;
	if((nRet = DasDesc_encode((DasDesc*)pThis, pBuf, sSubIn)) != 0) return nRet;
	return DasBuf_printf(pBuf, "%s</y>\n", sIndent);
}

DasErrCode _PlaneDesc_encodeZ(
	PlaneDesc* pThis, DasBuf* pBuf, const char* sIndent, const char* sSubIn, 
	const char* sValType
) {
	DasErrCode nRet = 0;
	
	const char* sGroup = pThis->sName;
	if(sGroup == NULL) sGroup = "";
	const char* sName = pThis->sName;
	if(sName == NULL) sName = "";
	
	nRet = DasBuf_printf(pBuf, "%s<z name=\"%s\" name=\"%s\" type=\"%s\" units=\"%s\">\n",
	                     sIndent, sName, sGroup, sValType, _PlaneDesc_unitStr(pThis));
	if(nRet != 0) return nRet;
	if((nRet = DasDesc_encode((DasDesc*)pThis, pBuf, sSubIn)) != 0) return nRet;
	return DasBuf_printf(pBuf, "%s</z>\n", sIndent);
}

DasErrCode PlaneDesc_encode(
	PlaneDesc* pThis, DasBuf* pBuf, const char* sIndent
){	
	char sValType[24] = {'\0'};
	DasEnc_toStr(pThis->pEncoding, sValType, 24);
	
	char sSubIndent[64] = {'\0'};
	snprintf(sSubIndent, 63, "%s  ", sIndent);
	
	switch(pThis->planeType){
	case X:
		return _PlaneDesc_encodeX(pThis, pBuf, sIndent, sSubIndent, sValType);
	case Y:
		return _PlaneDesc_encodeY(pThis, pBuf, sIndent, sSubIndent, sValType);
	case Z:
		return _PlaneDesc_encodeZ(pThis, pBuf, sIndent, sSubIndent, sValType);
	case YScan:
		return _PlaneDesc_encodeYScan(pThis, pBuf, sIndent, sSubIndent, sValType);
	default:
		; /* Make the compiler happy */
	}
	return das_error(17, "Code Change: Update PlaneDesc_encode");
}


