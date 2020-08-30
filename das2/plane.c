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
    if( strcmp( type, "x" )==0 )     return PT_X;
    if( strcmp( type, "y" )==0 )     return PT_Y;
	 if( strcmp( type, "xscan" )==0 ) return PT_XScan;
	 if( strcmp( type, "z" )==0 )     return PT_Z;
	 if( strcmp( type, "yscan" )==0 ) return PT_YScan;
	 if( strcmp( type, "w" )==0 )     return PT_W;
	 if( strcmp( type, "zscan" )==0 ) return PT_ZScan;
    
	 das_error(DASERR_PLANE, "getPlaneType: unrecognized type %s\n", type );
	 return -1; /* Never reached, making compiler happy */
}

const char * PlaneType_toStr( plane_type_t type ) {
	switch(type){
	case PT_X:     return "x";
	case PT_Y:     return "y";
	case PT_XScan: return "xscan";	
	case PT_Z:     return "z";
	case PT_YScan: return "yscan";
	case PT_W:     return "w";
	case PT_ZScan: return "zscan";
	default:
		das_error(DASERR_PLANE, "PlaneType_toStr: unrecognized type: %d\n", type );
	}
	return NULL; /* Never reached, making compiler happy */
}

const char* AxisDir_toStr(axis_dir_t dir){
	switch(dir){
		case DIR_Invalid: return "none";
		case DIR_X: return "X";
		case DIR_Y: return "Y";
		case DIR_Z: return "Z";
	default:
		das_error(DASERR_PLANE, "AxisDir_toStr: unrecognized type: %d\n", dir);
	}
	return NULL; /* Never reached, making compiler happy */
}

/* ************************************************************************* */
/* Construction/Destruction */

/* Empty Constructor */
PlaneDesc* new_PlaneDesc_empty() {
	PlaneDesc* pThis = (PlaneDesc*)calloc(1, sizeof(PlaneDesc));
	
	DasDesc_init((DasDesc*)pThis, PLANE);
	pThis->planeType = PT_Invalid;
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
	
	for(int i = 0; i < 3; ++i){
		pThis->aOffsetInter[i] = DAS_FILL_VALUE;
		pThis->aOffsetMin[i]   = DAS_FILL_VALUE;
	}
	
	pThis->pEncoding = pType;
		
   pThis->bPlaneDataValid = false;
	return pThis;
}

/* YScan Constructor */
PlaneDesc* new_PlaneDesc_yscan(
	const char* sGroup, DasEncoding* pZType, das_units zUnits, size_t uItems, 
	DasEncoding* pYType, const double* pYOffsets, das_units yUnits
){
	PlaneDesc* pThis;
	pThis = new_PlaneDesc(PT_YScan,sGroup,pZType,zUnits);
	int nWidth = 0;
	
	if(uItems < 1)
		das_error(DASERR_PLANE, "Must have at least 1 item in a yscan");
	
	pThis->uItems = uItems;
	pThis->aOffsetUnits[DIR_Y] = yUnits;
	pThis->pData = (double*)calloc(pThis->uItems, sizeof(double));
	
	for(unsigned int u = 0; u < pThis->uItems; u++)
		pThis->pData[u] = DAS_FILL_VALUE;
	pThis->bAlloccedBuf = true;
	
	pThis->aOffsets[DIR_Y] = (double*)calloc(pThis->uItems, sizeof(double));
	if(pYOffsets != NULL){
		for(unsigned int u = 0; u < pThis->uItems; u++) 
			pThis->aOffsets[DIR_Y][u] = pYOffsets[u];
		
		if(pYType != NULL){ 
			if(pYType->nCat == DAS2DT_BE_REAL || pYType->nCat == DAS2DT_LE_REAL){
				das_error(DASERR_PLANE, "Binary encodings can't be used for YTags values, "
						"cause they end up in XML headers.");
				return NULL;
			}
				
			pThis->aOffEncoding[DIR_Y] = pYType;
		}		
		else{
			pThis->aOffEncoding[DIR_Y] = new_DasEncoding(DAS2DT_ASCII, 12, NULL);
		}
		
	}
	else{
		for(unsigned int u = 0; u < pThis->uItems; u++)	
			pThis->aOffsets[DIR_Y][u] = u;
		
		if(pYType != NULL){
			pThis->aOffEncoding[DIR_Y] = pYType;
		}
		else{
			nWidth = (int) ceil( log10(pThis->uItems+1) ) + 1;
			pThis->aOffEncoding[DIR_Y] = new_DasEncoding(DAS2DT_ASCII, nWidth, "%.0f");
		}
	}
	
	pThis->aOffsetSpec[DIR_Y] = ytags_list;
	pThis->aOffsetInter[DIR_Y] = DAS_FILL_VALUE;
	pThis->aOffsetMin[DIR_Y]   = DAS_FILL_VALUE;
	
	return pThis;	
}

PlaneDesc* new_PlaneDesc_yscan_series(
	const char* sGroup, DasEncoding* pZType, das_units zUnits, size_t uItems,
   double yTagInter, double yTagMin, das_units yUnits
){
	if(uItems < 1){
		das_error(DASERR_PLANE, "Must have at least 1 item in a yscan");
		return NULL;
	}
	if(yTagInter <= 0.0){
		das_error(DASERR_PLANE, "YTag series interval must be greater than 0");
		return NULL;
	}
	
	PlaneDesc* pThis;
	pThis = new_PlaneDesc(PT_YScan,sGroup,pZType,zUnits);
		
	pThis->uItems = uItems;
	pThis->aOffsetUnits[DIR_Y] = yUnits;
	pThis->pData = (double*)calloc(pThis->uItems, sizeof(double));
	
	for(unsigned int u = 0; u < pThis->uItems; u++)
		pThis->pData[u] = DAS_FILL_VALUE;
	pThis->bAlloccedBuf = true;
	
	pThis->aOffsetSpec[DIR_Y] = ytags_series;
	
	pThis->aOffsetInter[DIR_Y] = yTagInter;
	
	if(isDas2Fill(yTagMin)){
		pThis->aOffsetMin[DIR_Y] = 0.0;
	}
	else{
		pThis->aOffsetMin[DIR_Y] = yTagMin;
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
	pThis->aOffsets[DIR_Y] = (double*)calloc(pThis->uItems, sizeof(double));
	
	if(sYTags == NULL){
		for(unsigned int u = 0; u < pThis->uItems; u++)
			pThis->aOffsets[DIR_Y][u] = (double)u;
		
		nFmtWidth = (int) ceil( log10(u+1) ) + 1;
		strcpy(sFmt, "%.0f");
		return 0;
	}
	
	/* How many commas do I have */
	for(u = 0; u<strlen(sYTags); u++ )
       if(sYTags[u] == ',' ) uCommas++;
		
	if(uCommas + 1 != pThis->uItems)
		return das_error(DASERR_PLANE, "Number of YTag values (%d) is not equal to the "
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
			if( sscanf(sVal, "%lf", pThis->aOffsets[DIR_Y] + u) != 1)
				return das_error(DASERR_PLANE, "Couldn't parse YTag value '%s'", sVal);
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
				return das_error(DASERR_PLANE, "Time values in YTags are not yet "
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
	int nMagEnd = (int)ceil(log10(pThis->aOffsets[DIR_Y][pThis->uItems-1]));
	int nMagBeg = (int)ceil(log10(pThis->aOffsets[DIR_Y][0]));
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
	    
	if( (pEnc = new_DasEncoding(nCat, nFmtWidth+1, sFmt)) == NULL) return DASERR_PLANE;
	pThis->aOffEncoding[DIR_Y] = pEnc;
	
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
	
	for(int i = 0; i < 3; ++i){
		pThis->aOffsetSpec[i] = ytags_none;
		pThis->aOffsetInter[i] = DAS_FILL_VALUE;
		pThis->aOffsetMin[i]   = DAS_FILL_VALUE;
	}
	if(pt == PT_Invalid){
		das_error(DASERR_PLANE, "Invalid plane type %d", pt);
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
		das_error(DASERR_PLANE, "Data 'type' attribute missing from plane description");
		return NULL;
	}
	
	/* Additional processing by plane type */
	switch(pThis->planeType){			
	case PT_X:
	case PT_Y:
	case PT_Z:
		pThis->uItems= 1;
		pThis->pData = &(pThis->value);
		pThis->bAlloccedBuf = false;
		break;
	
	case PT_YScan:
		for (i=0; attr[i]; i+=2){
								  
			if ( strcmp(attr[i], "nitems")==0 ) {
				if(sscanf(attr[i+1], "%zu", &pThis->uItems) != 1) {
					das_error(DASERR_PLANE, "Couldn't convert %s to a positive integer %s",
					           attr[i+1]);
					return NULL;
				}
				
				/* Assuming 6 digits to store sizes, no <x> plane and smallest
				   encoding of 4-bytes/value the largest number of items is 
					249999 */
				if(pThis->uItems > 249999){
					das_error(DASERR_PLANE, "Max number of supported items in a Das2 stream"
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
				pThis->aOffsetSpec[DIR_Y] = ytags_list;
				continue;			
			}
			if ( strcmp( attr[i], "yTagInterval" )==0 ) {
				
				if( (! das_str2double(attr[i+1], &(pThis->aOffsetInter[DIR_Y]))) ||
				    (pThis->aOffsetInter[DIR_Y] <= 0.0) ){
					das_error(DASERR_PLANE, "Couldn't convert %s to a real positive number",
							         attr[i+1]);
					return NULL;
				}
				pThis->aOffsetSpec[DIR_Y] = ytags_series;
				continue;			
			}
			if ( strcmp( attr[i], "yTagMin" )==0 ) {
				if(! das_str2double(attr[i+1], &(pThis->aOffsetMin[DIR_Y]))){
					das_error(DASERR_PLANE, "Couldn't convert %s to a real number", attr[i+1]);
					return NULL;
				}
				continue;			
			}
			if ( strcmp( attr[i], "yUnits" )==0 ) {
				pThis->aOffsetUnits[DIR_Y] = Units_fromStr( attr[i+1] );
				continue;
			}					
		}
	   break;
	default:
		das_error(DASERR_PLANE, "Code Change caused error in new_PlaneDesc_pairs");
		return NULL;
	}
	
	/* Some checks for required items */
	if(pThis->uItems < 1){
		das_error(DASERR_PLANE, "Illegal number of items, %d, in %s plane", 
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
			das_error(DASERR_PLANE, "Units element missing in plane description");
			return NULL;
		}
	}
	
	/* handle the ytags array now that both tags should be present */
	if(pThis->planeType == PT_YScan){
		if(pThis->aOffsetSpec[DIR_Y] != ytags_series){
			if( _PlaneDesc_decodeYTags(pThis, sYTags) != 0) return NULL;
		}
		else{	
			/* For series yTags set min offset */
			if(isDas2Fill(pThis->aOffsetMin[DIR_Y])){
				pThis->aOffsetMin[DIR_Y] = 0.0;
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
	case PT_X:
	case PT_Y:
	case PT_Z:
		pOther = new_PlaneDesc(pThis->planeType, pThis->sName, pEncode, 
				                 pThis->units);
		break;
	case PT_YScan:		
		if(pThis->aOffsetSpec[DIR_Y] == ytags_series){
			pOther = new_PlaneDesc_yscan_series(
					pThis->sName, pEncode, pThis->units, pThis->uItems,
					pThis->aOffsetInter[DIR_Y], pThis->aOffsetMin[DIR_Y],
					pThis->aOffsetUnits[DIR_Y]
			);
		}
		else{
			if(pThis->aOffEncoding[DIR_Y] != NULL)
				pYEncode = DasEnc_copy(pThis->aOffEncoding[DIR_Y]);
			
			pOther = new_PlaneDesc_yscan(
					pThis->sName, pEncode, pThis->units, pThis->uItems, pYEncode,
					pThis->aOffsets[DIR_Y], pThis->aOffsetUnits[DIR_Y]
			);
		}
		break;
	
	default:
		das_error(DASERR_PLANE, "ERROR: Plane type %d is unknown\n", pThis->planeType);
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
	if(pThis->pEncoding != NULL) del_DasEncoding(pThis->pEncoding);
	
	/* The scan planes may have offset arrays and encondings to empty */
	for(int i = 0; i < 3; ++i){
		if(pThis->aOffsets[i] != NULL) free(pThis->aOffsets[i]);
		if(pThis->aOffEncoding[i] != NULL) del_DasEncoding(pThis->aOffEncoding[i]);
	}
	
	/* The scan planes have a data buffer */
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
	if(pThis->planeType == PT_YScan){
		if(pThis->aOffsetSpec != pOther->aOffsetSpec ) return false;
		if(! DasEnc_equals(pThis->aOffEncoding[DIR_Y], pOther->aOffEncoding[DIR_Y])) return false;
		
		if(pThis->aOffsetSpec[DIR_Y] == ytags_list){
			for(size_t u = 0; u < pThis->uItems; ++u)
				if(pThis->aOffsets[u] != pOther->aOffsets[u]) return false;
		}
		else{
			if(pThis->aOffsetSpec[DIR_Y] == ytags_series){
				if(pThis->aOffsetInter != pOther->aOffsetInter) return false;
				if(pThis->aOffsetMin != pOther->aOffsetMin) return false;
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
		das_error(DASERR_PLANE, "All planes have at least one item.");
		return;
	}
	if(pThis->planeType != PT_YScan){
		if(uItems == 1) return; /* okay, a do-nothing call */
		das_error(DASERR_PLANE, "Only YScan planes may have more than 1 item");
		return;
	}
	
	if(pThis->uItems == uItems) return;
	
	_pkt_header_not_sent(pThis);
	
	/* Copy over existing ytags and then NAN the rest */
	double* pYTags = NULL;
	size_t u;
	if(pThis->aOffsets[DIR_Y] != NULL){
		pYTags = pThis->aOffsets[DIR_Y];
		pThis->aOffsets[DIR_Y] = (double*)calloc(uItems, sizeof(double));
		for(u = 0; u < uItems; u++){
			if(u < pThis->uItems) pThis->aOffsets[DIR_Y][u] = pYTags[u];
			else pThis->aOffsets[DIR_Y][u] = NAN;
		}
	}
	
	/* Better Damn well call setYTagSeries() after this !*/
	
	free(pThis->pData);
	pThis->pData = (double*)calloc(uItems, sizeof(double));
	pThis->uItems = uItems;
}

offset_spec_t PlaneDesc_getOffsetSpec(const PlaneDesc* pThis, axis_dir_t dir)
{
	if(dir == DIR_Invalid) return ytags_none;
	return pThis->aOffsetSpec[dir];
}


double PlaneDesc_getValue(const PlaneDesc* pThis, size_t uIdx)
{
	if(uIdx >= pThis->uItems){
		das_error(DASERR_PLANE, "%s: Index %s is out of range for %s plane", __func__,
				            PlaneType_toStr(pThis->planeType));
		return DAS_FILL_VALUE;
	}
	
	return pThis->pData[uIdx];
}

DasErrCode PlaneDesc_setValue(PlaneDesc* pThis, size_t uIdx, double value)
{
	if(uIdx >= pThis->uItems)
		return das_error(DASERR_PLANE, "Index %zu is out of range for %s plane", uIdx,		
			            PlaneType_toStr(pThis->planeType));
	
	/* make sure we set fill if that hasn't been done */
	if(!pThis->_bFillSet && (pThis->planeType != PT_X)){
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
		return das_error(DASERR_PLANE, "Index %zu in not value for %s plane", 
				            idx, PlaneType_toStr(pThis->planeType));
	
	if(pThis->pEncoding->nCat != DAS2DT_TIME)
		return das_error(DASERR_PLANE, "Plane data type is not a in the TIME category");
	
	/* My decoder normally has a fixed width, trick it for now and reset */
	size_t uSvLen = pThis->pEncoding->nWidth;
	size_t uLen = strlen(sTime);
	if(uLen < 4)
		return das_error(DASERR_PLANE, "Time string is too short to contain a valid time");
	
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
	if(!pThis->_bFillSet && (pThis->planeType != PT_X)){
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
	
	if(pThis->planeType == PT_X){
		das_error(DASERR_PLANE, "<x> planes should never have fill values\n");
	}
	
	/* Break const correctness, D doesn't allow this since it's worried
	 * about multi-threading.  Basically I'm only doing this because
	 * I don't have easy access to virtual functions in C. */
	PlaneDesc* pVarThis = (PlaneDesc*)pThis;
	
	if(! pThis->_bFillSet && (pThis->planeType != PT_X)){ 
	
		if(pVarThis->planeType == PT_Y){
			if(DasDesc_has((DasDesc*)pVarThis, "yFill")){
				pVarThis->rFill = DasDesc_getDouble( (DasDesc*)pThis, "yFill");
				pVarThis->_bFillSet = true;
			}
		}
		
		if(pThis->planeType == PT_YScan || pThis->planeType == PT_Z){
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
	
	if(pThis->planeType == PT_Y){
		DasDesc_setDouble((DasDesc*)pThis, "yFill", value);
		return;
	}
	
	if(pThis->planeType == PT_YScan || pThis->planeType == PT_Z){
		DasDesc_setDouble((DasDesc*)pThis, "zFill", value);
		return;
	}
	das_error(DASERR_PLANE, "<x> planes don't have fill values");
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

das_units PlaneDesc_getOffsetUnits(PlaneDesc* pThis, axis_dir_t dir) 
{
	switch(pThis->planeType){
	case PT_XScan:
		if(dir == DIR_X) 
			return pThis->aOffsetUnits[dir];
		break;
	case PT_YScan:
		if((dir == DIR_X) || (dir == DIR_Y))
			return pThis->aOffsetUnits[dir];
		break;
	case PT_ZScan:
		if((dir == DIR_X) || (dir == DIR_Y) || (dir == DIR_Z))
			return pThis->aOffsetUnits[dir];
		break;
	default:
		das_error(DASERR_PLANE, "getOffsetUnits: Not a scan plane" );
		break;
	}
	
	das_error(DASERR_PLANE, 
		"getOffsetUnits: plane type %s can't have %s offsets", 
		PlaneType_toStr(pThis->planeType), AxisDir_toStr(dir)
	);
	return UNIT_DIMENSIONLESS;  /* never reached, make compiler happy */
}

void PlaneDesc_setOffsetUnits(PlaneDesc* pThis, axis_dir_t dir, das_units units)
{
	_pkt_header_not_sent(pThis);
	
	switch(pThis->planeType){
	case PT_XScan:
		if(dir == DIR_X){
			pThis->aOffsetUnits[dir] = units;
			return;
		}
		break;
		
	case PT_YScan:
		if((dir == DIR_X) || (dir == DIR_Y)){
			pThis->aOffsetUnits[dir] = units;
			return;
		}
		break;
	
	case PT_ZScan:
		if((dir == DIR_X) || (dir == DIR_Y) || (dir == DIR_Z)){
			pThis->aOffsetUnits[dir] = units;
			return;
		}
		break;
	default:
		das_error(DASERR_PLANE, "setOffsetUnits: Not a scan plane" );
		return;
	}
	
	das_error(DASERR_PLANE, 
		"getOffsetUnits: plane type %s can't have %s offsets", 
		PlaneType_toStr(pThis->planeType), AxisDir_toStr(dir)
	);
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

/*
double PlaneDesc_getYTagInterval(const PlaneDesc* pThis){
	if(pThis->aOffsetSpec == ytags_series) return pThis->aOffsetInter;
	return DAS_FILL_VALUE;
}
 */

const double* PlaneDesc_getOffsets(const PlaneDesc* pThis, axis_dir_t dir)
{
	if(dir == DIR_Invalid){
		das_error(DASERR_PLANE, "getOffsets: Invalid axis direction");
		return NULL;
	}
	if((pThis->planeType == PT_XScan)||(pThis->planeType != PT_YScan)||
	   (pThis->planeType != PT_ZScan))
		return pThis->aOffsets[dir];
		
	das_error(DASERR_PLANE, "getOffsets: not a scan plane!" );
	return NULL;
}

bool _ckOffsetDir(PlaneDesc* pThis, axis_dir_t dir){
	/* run the gauntlet of failure... */
	if(dir == DIR_Invalid){
		das_error(DASERR_PLANE, "Invalid axis direction");
		return false;
	}
	switch(pThis->planeType){
	case PT_X:
	case PT_Y:
	case PT_Z:
			das_error(DASERR_PLANE, "getOffsets: not a scan plane");
		return false;
	case PT_XScan: if(dir == DIR_X) break;
	case PT_YScan: if((dir == DIR_X)||(dir == DIR_Y)) break;
	case PT_ZScan: if((dir == DIR_X)||(dir == DIR_Y)||(dir == DIR_Z)) break;
	default:
		das_error(DASERR_PLANE, "plane type %s can't have %s offsets", 
			PlaneType_toStr(pThis->planeType), AxisDir_toStr(dir)
		);
		return false;
	}
	return true;
}

const double* PlaneDesc_getOrMakeOffsets(PlaneDesc* pThis, axis_dir_t dir)
{
	ptrdiff_t n = 0;
	
	if(!_ckOffsetDir(pThis, dir))
		return NULL;
	
	/* okay, now do something useful... */
   if((pThis->aOffsets[dir] == NULL)&&(pThis->aOffsetSpec[dir] == ytags_series)){

		pThis->aOffsets[dir] = (double*)calloc(pThis->uItems, sizeof(double));
		for(n = 0; n < pThis->uItems; ++n){
			pThis->aOffsets[dir][n] = 
				pThis->aOffsetMin[dir] + (pThis->aOffsetInter[dir])*n;
		}
	}
	
	return pThis->aOffsets[dir];
}
		

void PlaneDesc_setOffsets(
	PlaneDesc* pThis, axis_dir_t dir, const double* pOffsets
){
	
	if(!_ckOffsetDir(pThis, dir))
		return;
	
	/* This can change the ytag_spec */
	size_t u;
	bool bSame = true;
	if(pThis->aOffsetSpec[dir] == ytags_list){
		/* Before going nutty, see if they are just resetting existing YTags */
		for(u = 0; u < pThis->uItems; ++u){
			if(pThis->aOffsets[dir][u] != pOffsets[u]){
				bSame = false;
				break;
			}
		}
		if(bSame) return;
	}
	else{
		pThis->aOffsetSpec = ytags_list;
		if(pThis->aOffsets[dir] != NULL) free(pThis->aOffsets[dir]);
		pThis->aOffsets[dir] = (double*)calloc(pThis->uItems, sizeof(double));
	}
	for(u = 0; u < pThis->uItems; ++u) pThis->aOffsets[dir][u] = pOffsets[u];
	
	_pkt_header_not_sent(pThis);
}

void PlaneDesc_getOffsetSeries(
	const PlaneDesc* pThis, axis_dir_t dir, double* pInterval, double* pMin
){
	if(!_ckOffsetDir(pThis, dir))
		return;
	
	if(pThis->aOffsetSpec[dir] == ytags_list){
		*pInterval = DAS_FILL_VALUE;
		if(pMin != NULL) *pMin = DAS_FILL_VALUE;
	}
	*pInterval = pThis->aOffsetInter[dir];
	if(pMin != NULL) *pMin = pThis->aOffsetMin[dir];
}

void PlaneDesc_setOffsetSeries(
	PlaneDesc* pThis, axis_dir_t dir, double rInterval, double rMin
){
	if(rInterval < 0.0 || isDas2Fill(rInterval)){
		das_error(DASERR_PLANE, "Invalid value for rInterval");
		return;
	}

	if(!_ckOffsetDir(pThis, dir))
		return;
	
	if(pThis->aOffsetSpec[dir] == ytags_list){
		_pkt_header_not_sent(pThis);
		if(pThis->aOffsets != NULL){ 
			free(pThis->aOffsets);
			pThis->aOffsets[dir] = NULL;
		}
		pThis->aOffsetSpec[dir] = ytags_series;
	}
	
	/* Handle the do-nothing case */
	if((pThis->aOffsetInter[dir] == rInterval)&&(rMin = pThis->aOffsetMin[dir]))
		return;
	
	_pkt_header_not_sent(pThis);
	pThis->aOffsetInter[dir] = rInterval;
	pThis->aOffsetMin[dir] = isDas2Fill(rMin) ? 0.0 : rMin;
}

/* ************************************************************************* */
/* Encode/Decode Values */


DasErrCode PlaneDesc_decodeData(const PlaneDesc* pThis, DasBuf* pBuf)
{
	unsigned int u =0;
	int nRet = 0; 
	
	/* make sure we set fill if that hasn't been done */
	if(!pThis->_bFillSet && (pThis->planeType != PT_X)){
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
			DASERR_PLANE, "Packet length check error in PlaneDesc_encodeData:  Expected to "
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
		Units_toStr(pThis->aOffsetUnits), pThis->uItems
	);
	if(nRet != 0) return nRet;
	
	if(pThis->aOffsetSpec != ytags_series){
	
		nRet = DasBuf_printf(pBuf, "\n%s       yTags=\"", sIndent);
		
		for(unsigned int u = 0; u<pThis->uItems; u++){
			if(u > 0){
				if( (nRet = DasBuf_write(pBuf, &comma, 1)) != 0) return nRet;
			}
			nRet = DasEnc_write(pThis->aOffEncoding, pBuf, pThis->aOffsets[u], pThis->aOffsetUnits);
			if(nRet != 0) return nRet;
		}
		
		nRet = DasBuf_printf(pBuf, "\">\n"); 
	}
	else{
		nRet = DasBuf_printf(pBuf, "yTagInterval=\"%.6e\" ", pThis->aOffsetInter);
		if(nRet != 0) return nRet;
		
		if(pThis->aOffsetMin == 0.0)
			nRet = DasBuf_printf(pBuf, "yTagMin=\"0\" ");
		else
			nRet = DasBuf_printf(pBuf, "yTagMin=\"%.6e\" ", pThis->aOffsetMin);
		if(nRet != 0) return nRet;			
			
		nRet = DasBuf_printf(pBuf, " >\n"); 
	}
	
	
	if(nRet != 0) return nRet;
	
	nRet = DasDesc_encode((DasDesc*)pThis, pBuf, sSubIn);
	if(nRet != 0) return nRet;
	
   return DasBuf_printf(pBuf, "%s</yscan>\n", sIndent);
}

DasErrCode _PlaneDesc_encodePtPlane(
	PlaneDesc* pThis, DasBuf* pBuf, const char* sIndent, const char* sSubIn, 
	const char* sValType, int version
){ 
	DasErrCode nRet = 0;
	const char* sName = pThis->sName;
	
	char cAx = '?';
	if(pThis->planeType == PT_X) 
	 = sAxis[dir]
	
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
	PlaneDesc* pThis, DasBuf* pBuf, const char* sIndent, int version
){	
	char sValType[24] = {'\0'};
	DasEnc_toStr(pThis->pEncoding, sValType, 24);
	
	char sSubIndent[64] = {'\0'};
	snprintf(sSubIndent, 63, "%s  ", sIndent);
	
	switch(pThis->planeType){
	case PT_X:
	case PT_Y:
	case PT_Z:
	case PT_W:
		return _PlaneDesc_encodePtPlane(
			pThis, pBuf, sIndent, sSubIndent, sValType, version
		);
	case PT_XScan:
	case PT_YScan:
	case PT_ZScan:
		return _PlaneDesc_encodeScanPlane(
			pThis, pBuf, sIndent, sSubIndent, sValType, version
		);
	default:
		; /* Make the compiler happy */
	}
	return das_error(DASERR_PLANE, "Code Change: Update PlaneDesc_encode");
}


