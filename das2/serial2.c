/* Copyright (C) 2017-2024 Chris Piker <chris-piker@uiowa.edu>
 *
 * This file is part of das2C, the Core Das2 C Library.
 *
 * das2C is free software; you can redistribute it and/or modify it under
 * the terms of the GNU Lesser General Public License version 2.1 as published
 * by the Free Software Foundation.
 *
 * das2C is distributed in the hope that it will be useful, but WITHOUT ANY
 * WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS
 * FOR A PARTICULAR PURPOSE.  See the GNU Lesser General Public License for
 * more details.
 *
 * You should have received a copy of the GNU Lesser General Public License
 * version 2.1 along with das2C; if not, see <http://www.gnu.org/licenses/>.
 */

#define _POSIX_C_SOURCE 200112L
#include <string.h>
#include <ctype.h>

#include "stream.h"

/* ************************************************************************** */
/* Converting old style packets to datasets */

/* Max number of dimensions in a dataset */
#define LEGACY_MAX_DIMS 64
#define LEGACY_SRC_ARY_SZ 64

/* ************************************************************************* */
/* Inspect plane properties and output standardized dimension role string	 */

const char* _serial_role(PlaneDesc* pPlane)
{
	const char* sRole = DasDesc_get((DasDesc*)pPlane, "operation");
	if(sRole == NULL) return DASVAR_CENTER;
	
	/* Interpret  Autoplot style strings */
	if(strcmp("BIN_AVG", sRole) == 0) return DASVAR_MEAN;
	if(strcmp("BIN_MAX", sRole) == 0) return DASVAR_MAX;
	if(strcmp("BIN_MIN", sRole) == 0) return DASVAR_MIN;
	
	return DASVAR_CENTER;
}

/* ************************************************************************** */
/* Specialized property copies only used by the legacy converter */

/** Copy in dataset properties from some other descriptor
 * 
 * This is a helper for das 2.2 streams.
 * 
 * Any properties that don't start with a specific dimension identifier i.e.
 * 'x','y','z','w' are copied into this dataset's properties dictionary.  Only
 * properties not present in the internal dictionary are copied in.
 * 
 * @param pThis this dataset object
 * @param pOther The descriptor containing properites to copy in
 * @return The number of properties copied in
 */
int DasDs_copyInProps(DasDs* pThis, const DasDesc* pOther)
{
	const DasAry* pSource = &(pOther->properties);
	size_t uProps = DasAry_lengthIn(pSource, DIM0);

	int nCopied = 0;
	for(size_t u = 0; u < uProps; ++u){
		size_t uPropLen = 0;
		const DasProp* pIn = (const DasProp*) DasAry_getBytesIn(
			pSource, DIM1_AT(u), &uPropLen
		);
		if(!DasProp_isValid(pIn))
			continue;

		const char* sName = DasProp_name(pIn);

		/* Do I want this prop? */
		if((*sName == 'x')||(*sName == 'y')||(*sName == 'z')||(*sName == '\0'))
			continue; /* ... nope */
		
		/* Do I have this property? ... */
		const DasProp* pOut = DasDesc_getLocal((DasDesc*)pThis, sName);
		if(DasProp_isValid(pOut))
			continue;  /* ... yep */
		
		/* Set the property */
		if(DasDesc_setProp((DasDesc*)pThis, pIn) != DAS_OKAY){
			return nCopied;
		}
		++nCopied;
	}
	return nCopied;
}

/** Copy in dataset properties from some other descriptor
 * 
 * This is a helper for das 2.2 streams as these use certian name patterns to
 * indicate which dimension a property is for
 * 
 * Any properties that start with a specific dimension identifier i.e.
 * 'x','y','z','w' are copied into this dataset's properties dictionary.  Only
 * properties not present in the internal dictionary are copied in.  
 * 
 * @param pThis this dimension object
 * @param cAxis the connonical axis to copy in.
 * @param pOther The descriptor containing properites to copy in
 * @return The number of properties copied in
 * @memberof DasDim
 */

int DasDim_copyInProps(DasDim* pThis, char cAxis, const DasDesc* pOther)
{
	char sNewName[32] = {'\0'};

	const DasAry* pSrcAry = &(pOther->properties);
	size_t uProps = DasAry_lengthIn(pSrcAry, DIM0);

	int nCopied = 0;
	for(size_t u = 0; u < uProps; ++u){
		size_t uPropLen = 0;
		const DasProp* pIn = (const DasProp*) DasAry_getBytesIn(pSrcAry, DIM1_AT(u), &uPropLen);
		if(!DasProp_isValid(pIn))
			continue;

		const char* sName = DasProp_name(pIn);

		/* We only want stuff for the given axis, but we don't want to copy in
		 * the axis name, so make sure there's something after it. */
		if((*sName != cAxis)||( *(sName +1) == '\0'))
			continue; /* ... nope */
		
		/* Since we strip the x,y,z, make next char lower to preserve the look 
		 * of the prop naming scheme */
		memset(sNewName, 0, 32);
		int nLen = (int)(strlen(sName)) - 1;
		nLen = nLen > 31 ? 31 : nLen;
		strncpy(sNewName, sName + 1, 31);
		sNewName[0] = tolower(sNewName[0]);  
		
		DasDesc_flexSet((DasDesc*)pThis, NULL, DasProp_type(pIn), sNewName, 
			DasProp_value(pIn), DasProp_sep(pIn), pIn->units, DASPROP_DAS3
		);
		++nCopied;
	}
	return nCopied;
}

/* ************************************************************************* */
/* Handle matching up planes into single dimensions, interface is really	  */
/* complicated because this code was inline in another function				  */

DasDim* _serial_getDim(
	PlaneDesc* pPlane, PktDesc* pPd, DasStream* pSd,
	char cAxis, 
	DasDs* pDs, enum dim_type dType, const char* sDimId,
	DasDim** ppDims, char* pDimSrc, size_t* puDims
){
	/* if this plane has a source property then it might be grouped */
	char* p = NULL;
	char sNewDimId[64] = {'\0'};
	DasDim* pDim = NULL;
	
	const char* sSource = DasDesc_get((DasDesc*)pPlane, "source");
	if(sSource != NULL){
		/* See if this plane source appears in an existing dim */
		for(size_t u = 0; u < *puDims; u++){
			p = pDimSrc + u*LEGACY_SRC_ARY_SZ;
			if(strncmp(sSource, p, LEGACY_SRC_ARY_SZ) == 0){
				/* Same group, add in any plane level properties that might be
				 * missing in this dimension */
				if(cAxis != '\0') 
					DasDim_copyInProps(ppDims[u], cAxis, (DasDesc*)pPlane);
				return ppDims[u];
			}
		}
	}
	
	/* Didn't find a matching dim, so add one to the dataset, record it's info
	 * if it has an operation source property.  Also de-kludge the name by 
	 * removing anything after the dot
	 */
	
	if(sSource != NULL){
		p = (char*) strchr(sDimId, '.');
		if(p && (p != sDimId)){
			strncpy(sNewDimId, sDimId, 63);
			p = strchr(sNewDimId, '.');
			*p = '\0';
			sDimId = sNewDimId;
		}
		if((pDim = DasDs_makeDim(pDs, dType, sDimId, "")) == NULL) return NULL;
		
		if((*puDims) + 1 >= LEGACY_MAX_DIMS){
			das_error(DASERR_BLDR, "Too many dimensions in a single packet %d",
						 LEGACY_MAX_DIMS);
			return NULL;
		}
		
		ppDims[*puDims] = pDim;
	
		p = pDimSrc + (*puDims)*LEGACY_SRC_ARY_SZ;
		strncpy(p, sSource, LEGACY_SRC_ARY_SZ - 1);
		*puDims = *puDims + 1;
	}
	else{
		if((pDim = DasDs_makeDim(pDs, dType, sDimId, ""))==NULL) return NULL;
		
	}
	
	if(cAxis != '\0'){
		DasDim_copyInProps(pDim, cAxis, (DasDesc*)pSd);
		DasDim_copyInProps(pDim, cAxis, (DasDesc*)pPd);
		DasDim_copyInProps(pDim, cAxis, (DasDesc*)pPlane);
	}
	return pDim;
}

/* ************************************************************************* */
/* Handle making arrays suitable for receiving stream values 
 *
 * Note: Internal arrays need to be some binary type if possible so that
 * downstream programs can easily work with data.
 * 
 * @param bRaw - If True, expect raw stream data, if False expect data 
 *		  parsed to doubles.
 */
DasAry* _serial_makeAry(
	bool bRaw, const char* sAryId, DasEncoding* pEncoder, const ubyte* pFill, 
	int rank, size_t* shape, das_units defUnits
){
	DasAry* pAry = NULL;
	das_val_type vtAry = vtDouble;  /* no choice, data already read */
	das_units units = defUnits;
	
	/* I'm reading the stream directly and have a choice of internal encodings */
	if(bRaw){

		switch(pEncoder->nCat){
		case DAS2DT_TIME:	 /* store text times as structures, and rm epoch units */
			vtAry = vtTime; 
			units = UNIT_UTC;
			break;
		case DAS2DT_ASCII:  /* If over 12 chars (including whitespace) encode as double */
			vtAry = (pEncoder->nWidth > 12) ? vtDouble : vtFloat; 
			break;
		default:				/* All that's left are DAS2DT_BE_REAL & DAS2DT_LE_REAL */
			vtAry = (pEncoder->nWidth > 4) ? vtDouble : vtFloat;
			break;
		}
	}

	pAry = new_DasAry(sAryId, vtAry, 0, pFill, rank, shape, units);
	return pAry;
}

/* ************************************************************************* */
/* Internal storage and possible direct encoding */

DasErrCode _serial_addCodec(
	DasDs* pDs, const char* sAryId, int nItems, DasEncoding* pEncoder
){

	int nHash = DasEnc_hash(pEncoder);
	const char* sEncType = NULL;
	int nItemBytes = 0;
	const char* sSemantic = "real";
	switch(nHash){
	case DAS2DT_BE_REAL_8: sEncType = "BEreal"; nItemBytes = 8; break;
	case DAS2DT_LE_REAL_8: sEncType = "LEreal"; nItemBytes = 8; break;
	case DAS2DT_BE_REAL_4: sEncType = "BEreal"; nItemBytes = 4; break;
	case DAS2DT_LE_REAL_4: sEncType = "LEreal"; nItemBytes = 4; break;
	}
	if(sEncType == NULL){
		nItemBytes = pEncoder->nWidth;
		sEncType = "utf8";
		if(pEncoder->nCat == DAS2DT_TIME)
			sSemantic = "datetime";
	}

	return DasDs_addFixedCodec(pDs, sAryId, sSemantic, sEncType, nItemBytes, nItems);
}


/* ************************************************************************* */
/* Initialize X-Y pattern
 * Make one dimension for X and one each for the Y's.  Could do some deeper
 * inspection to see if some of the columns should be grouped into the same
 * dimension.  For das 2.3 streams where we can have multiple X columns 
 * this will be a must. */

void _strrep(char* pId, char c, char r)
{
	char* pTmp = pId;
	while(*pTmp != '\0'){
		if(*pTmp == c) *pTmp = r;
		++pTmp;
	}
}


DasDs* _serial_initXY(
	DasStream* pSd, PktDesc* pPd, const char* pGroup, bool bCodecs
){
	/* If my group name is null, make up a new one appropriate to XY data */
	const char* pId = NULL;
	PlaneDesc* pPlane = NULL;
	DasEncoding* pEncoder = NULL;
	const char* sRole = NULL; /* The reason this plane is present */
	int nY = 0;
	char sBuf[64] = {'\0'};
	char sDsId[64] = {'\0'};
	if(pGroup == NULL) pGroup = PktDesc_getGroup(pPd);
	if(pGroup == NULL){
		if(PktDesc_getNPlanesOfType(pPd, Y) == 1){
			pPlane = PktDesc_getPlaneByType(pPd, Y, 0);
			pGroup = PlaneDesc_getName(pPlane);
		}
		else{
			nY = PktDesc_getNPlanesOfType(pPd, Y);
			snprintf(sBuf, 63, "unknown_%dY", nY);
			pGroup = sBuf;
			nY = 0;
		}
	}
	
	snprintf(sDsId, 63, "%s_%02d", pGroup, PktDesc_getId(pPd));

	DasDs* pDs = new_DasDs(sDsId, pGroup, 1);
	DasDim* pDim = NULL;
	DasAry* pAry = NULL;
	DasVar* pVar = NULL;
	das_units units = UNIT_DIMENSIONLESS;
	enum dim_type dType = DASDIM_UNK;
	
	/* Tracking for array groups (aka dimensions) */
	DasDim* aDims[LEGACY_MAX_DIMS] = {NULL};
	char aDimSrc[LEGACY_MAX_DIMS * LEGACY_SRC_ARY_SZ] = {'\0'};
	size_t uDims = 0;
	
	/* Copy any properties that don't start with one of the axis prefixes */
	
	/* ... actually, we output stream headers now, so this isn't needed, it 
		was always a bit of a hack and doesn't play well with CDF generation.
	DasDs_copyInProps(pDs, (DasDesc*)pSd);
	DasDs_copyInProps(pDs, (DasDesc*)pPd);
	*/
	
	char sAryId[64] = {'\0'};
	double fill;
	char cAxis = '\0';
	
	for(size_t u = 0; u < pPd->uPlanes; ++u){
		pPlane = pPd->planes[u];
		pEncoder = PlaneDesc_getValEncoder(pPlane);
		fill = PlaneDesc_getFill(pPlane);
		units = pPlane->units;

		pId = pPlane->sName;
		if(pPlane->planeType == X){
			cAxis = 'x';
			if(pId == NULL){
				if(Units_haveCalRep(pPlane->units)) pId = "time";
				else pId = "X";
			}

			strncpy(sAryId, pId, 63);
			pAry = _serial_makeAry(
				bCodecs, pId, pEncoder, (const ubyte*)&fill, RANK_1(0), units
			);
		}
		else{
			cAxis = 'y';
			++nY;
			
			/* Always copy over the name if it exists */		 
			if(pId == NULL) snprintf(sAryId, 63, "Y_%d", nY);
			else				strncpy(sAryId, pId, 63);
			_strrep(sAryId, '.', '_');  /* handle amplitude.max type stuff */

			pAry = _serial_makeAry(
				bCodecs, sAryId, pEncoder, (const ubyte*)&fill, RANK_1(0), units
			);
		}
		if(pAry == NULL) return NULL;
		
		/* add the new array to the DS so it has somewhere to put this item from
		 * the packets */
		if(DasDs_addAry(pDs, pAry) != DAS_OKAY) return NULL;
		
		/* Remember how to fill this array.  This will get more complicated
		 * when variable length packets are introduced */
		DasAry_setSrc(pAry, PktDesc_getId(pPd), u, 1);
		
		/* On to higher level data organizational structures:  Create dimensions
		 * and variables as needed for the new arrays */
		if(pPlane->planeType == X) dType = DASDIM_COORD;
		else dType = DASDIM_DATA;
		
		pDim = _serial_getDim(
			pPlane, pPd, pSd, cAxis, pDs, dType, pId, aDims, aDimSrc, &uDims
		);
		if(pDim == NULL) return NULL;
		
		pVar = new_DasVarArray(pAry, SCALAR_1(0));
		if( pVar == NULL) return NULL;
		sRole = _serial_role(pPlane);
		if(! DasDim_addVar(pDim, sRole, pVar)) return NULL;

		if(bCodecs)
			if(_serial_addCodec(pDs, sAryId, 1, pEncoder) != DAS_OKAY)
				return NULL;
	}
	return pDs;
}

/* ************************************************************************* */
/* Initialize X-Y-Z pattern */

DasDs* _serial_initXYZ(
	DasStream* pSd, PktDesc* pPd, const char* pGroup, bool bCodecs
){
	/* If my group name is null, make up a new one appropriate to XYZ data */
	const char* pId = NULL;
	PlaneDesc* pPlane = NULL;
	DasEncoding* pEncoder = NULL;
	const char* sRole = NULL; /* The reason this plane is present */
	int nZ = 0;
	char sBuf[64] = {'\0'};
	char sDsId[64] = {'\0'};
	if(pGroup == NULL) pGroup = PktDesc_getGroup(pPd);
	if(pGroup == NULL){
		if(PktDesc_getNPlanesOfType(pPd, Z) == 1){
			pPlane = PktDesc_getPlaneByType(pPd, Z, 0);
			pGroup = PlaneDesc_getName(pPlane);
		}
		else{
			nZ = PktDesc_getNPlanesOfType(pPd, Z);
			snprintf(sBuf, 63, "unknown_%dZ", nZ);
			pGroup = sBuf;
			nZ = 0;
		}
	}
	
	snprintf(sDsId, 63, "%s_%02d", pGroup, PktDesc_getId(pPd));
	
	DasDs* pDs = new_DasDs(sDsId, pGroup, 1);
	DasDim* pDim = NULL;
	DasAry* pAry = NULL;
	DasVar* pVar = NULL;
	das_units units = UNIT_DIMENSIONLESS;
	
	enum dim_type dType = DASDIM_UNK;
	
	/* Tracking for array groups (aka dimensions) */
	DasDim* aDims[LEGACY_MAX_DIMS] = {NULL};
	char aDimSrc[LEGACY_MAX_DIMS * LEGACY_SRC_ARY_SZ] = {'\0'};
	size_t uDims = 0;
	
	/* Copy any properties that don't start with one of the axis prefixes */
	/* ... or don't.  Messes up CDF output 
	DasDs_copyInProps(pDs, (DasDesc*)pSd);
	DasDs_copyInProps(pDs, (DasDesc*)pPd); */
	
	char sAryId[64] = {'\0'};
	double fill;
	char cAxis = '\0';
	
	for(size_t u = 0; u < pPd->uPlanes; ++u){
		pPlane = pPd->planes[u];
		pId = pPlane->sName; 
		pEncoder = PlaneDesc_getValEncoder(pPlane);
		fill = PlaneDesc_getFill(pPlane);
		units = pPlane->units;
		
		switch(pPlane->planeType){
		case X:
			cAxis = 'x';
			if(pId == NULL){
				if(Units_haveCalRep(pPlane->units)) pId = "time";
				else pId = "X";
			}

			/* Fill is not allowed for Das 2.2 X planes in an X,Y,Z pattern */
			strncpy(sAryId, pId, 63);
			pAry = _serial_makeAry(
				bCodecs, pId, pEncoder, (const ubyte*)&fill, RANK_1(0), units
			);
			break;
			
		case Y:
			cAxis = 'y';
			if(pId == NULL)pId = "Y";

			/* Fill is not allowed for Das 2.2 Y planes in an X,Y,Z pattern */
			pAry = _serial_makeAry(
				bCodecs, pId, pEncoder, (const ubyte*)&fill, RANK_1(0), units
			);
			break;
			
		case Z:
			cAxis = 'z';
			++nZ;
			
			if(pId == NULL)  snprintf(sAryId, 63, "Z_%d", nZ);
			else				 strncpy(sAryId, pId, 63);
			_strrep(sAryId, '.', '_');	/* handle amplitude.max stuff */

			pAry = _serial_makeAry(
				bCodecs, pId, pEncoder, (const ubyte*)&fill, RANK_1(0), units
			);
			break;
			
		
		default:
			das_error(DASERR_BLDR, "Unexpected plane type in XYZ pattern");
			return NULL;
		}
		
		if(pAry == NULL) return NULL;
		
		/* add the new array to the DS so it has somewhere to put this item from
		 * the packets */
		if(DasDs_addAry(pDs, pAry) != DAS_OKAY) return NULL;
		
		/* Remember how to fill this array.  This will get more complicated
		 * when variable length packets are introduced.  So this is item 'u'
		 * from the packet and 1 item is read for each packet */
		DasAry_setSrc(pAry, PktDesc_getId(pPd), u, 1);
		
		/* Create dimensions and variables as needed for the new arrays */
		if(pPlane->planeType == Z) dType = DASDIM_DATA;
		else dType = DASDIM_COORD;
		
		pDim = _serial_getDim(
			pPlane, pPd, pSd, cAxis, pDs, dType, pId, aDims, aDimSrc, &uDims
		);
		if(pDim == NULL) return NULL;
		
		pVar = new_DasVarArray(pAry, SCALAR_1(0));
		if( pVar == NULL) return NULL;
		
		/* Assume these are center values unless there is a property stating
		 * otherwise */
		sRole = _serial_role(pPlane);
		if(! DasDim_addVar(pDim, sRole, pVar)) return NULL;

		if(bCodecs)
			if(_serial_addCodec(pDs, sAryId, 1, pEncoder) != DAS_OKAY)
				return NULL;
	}
	
	return pDs;
}

/* ************************************************************************* */
/* Initialize Events Pattern */

DasDs* _serial_initEvents(DasStream* pSd, PktDesc* pPd, const char* sGroupId)
{
	das_error(DASERR_BLDR, "Event stream reading has not been implemented");
	return NULL;
}

/* ************************************************************************* */
/* Initialize YScan Pattern */

bool _serial_checkYTags(PktDesc* pPd)
{
	size_t uYScans = PktDesc_getNPlanesOfType(pPd, YScan);
	if(uYScans < 2) return true;

	PlaneDesc* pFirst = PktDesc_getPlaneByType(pPd, YScan, 0);
	size_t uYTags = PlaneDesc_getNItems(pFirst);
	ytag_spec_t spec = PlaneDesc_getYTagSpec(pFirst);
	double rInterval = -1.0, rMin = -1.0, rMax = -1.0;
	if(spec == ytags_series)
		PlaneDesc_getYTagSeries(pFirst, &rInterval, &rMin, &rMax);
	const double* pYTags = NULL;
	if(spec == ytags_list) pYTags = PlaneDesc_getYTags(pFirst);

	das_units units = PlaneDesc_getYTagUnits(pFirst);

	PlaneDesc* pNext = NULL;
	size_t u,v;
	double rNextInterval = -1.0, rNextMin = -1.0, rNextMax = -1.0;
	const double* pNextYTags = NULL;
	for(u = 1; u < uYScans; ++u){
		pNext = PktDesc_getPlaneByType(pPd, YScan, u);
		if(uYTags != PlaneDesc_getNItems(pNext)) return false;

		/* The tags can be specified as none, a list, or a series */
		if(spec != PlaneDesc_getYTagSpec(pNext)) return false;

		if(units != PlaneDesc_getYTagUnits(pNext)) return false;

		switch(spec){
		case ytags_none: break;
		case ytags_series:
			PlaneDesc_getYTagSeries(pNext, &rNextInterval, &rNextMin, &rNextMax);
			if(rInterval != rNextInterval ) return false;
			if(rMin != rNextMin) return false;
			if(rMax != rNextMax) return false;
			break;

		case ytags_list:
			pNextYTags = PlaneDesc_getYTags(pNext);
			for(v = 0; v<uYTags; ++v) if(pYTags[v] != pNextYTags[v]) return false;
			break;
		}
	}

	return true;
}

double* _serial_yTagVals(PlaneDesc* pPlane)
{
	if(pPlane->planeType != YScan){
		das_error(DASERR_BLDR, "Program logic error");
		return NULL;
	}
	double* pTags = (double*)calloc(PlaneDesc_getNItems(pPlane), sizeof(double));
	size_t u, uItems = PlaneDesc_getNItems(pPlane);
	const double* pListTags = NULL;
	double rInterval, rMin, rMax;
	switch(pPlane->ytag_spec){
	case ytags_list:
		pListTags = PlaneDesc_getYTags(pPlane);
		for(u = 0; u < uItems; ++u) pTags[u] = pListTags[u];
		break;
	case ytags_none:
		for(u = 0; u < uItems; ++u) pTags[u] = u;
		break;
	case ytags_series:
		PlaneDesc_getYTagSeries(pPlane, &rInterval, &rMin, &rMax);
		for(u = 0; u < uItems; ++u) pTags[u] = rMin + (rInterval * u);
		break;
	}
	return pTags;
}

bool _serial_isWaveform(PlaneDesc* pPlane){

	const char* sRend = DasDesc_getStr((DasDesc*)pPlane, "renderer");
	if(sRend == NULL) return false;
	if(strcmp("waveform", sRend) != 0) return false;
	
	das_units units = PlaneDesc_getYTagUnits(pPlane);
	if(! Units_canConvert(units, UNIT_SECONDS)) return false;
	return true;
}

DasDs* _serial_initYScan(
	DasStream* pSd, PktDesc* pPd, const char* pGroup, bool bCodecs
){
	/* Make sure all the yscans have the same yTags.  The assumption here is
	 * that a single packet only has data correlated in it's coordinates.  If
	 * two yscans have different yTag sets then they are not correlated. */
	
	if(!_serial_checkYTags(pPd)){
		das_error(DASERR_BLDR, "YTags are not equivalent in multi-yscan packet");
		return NULL;
	}

	/* If my group name is null, make up a new one appropriate to YScan data */
	PlaneDesc* pPlane = PktDesc_getPlaneByType(pPd, YScan, 0);
	int nY = 0, nYScan = 0;
	char sDsGroup[64] = {'\0'};
	char sDsId[64] = {'\0'};
	
	if(pGroup == NULL) pGroup = PktDesc_getGroup(pPd);
	if(pGroup == NULL) pGroup = PlaneDesc_getName(pPlane);
	if(pGroup == NULL){
		nYScan = PktDesc_getNPlanesOfType(pPd, YScan);
		snprintf(sDsGroup, 63, "default_%d_MultiZ", nYScan);
		pGroup = sDsGroup;
	}
	snprintf(sDsId, 63, "%s_%02d", pGroup, PktDesc_getId(pPd));

	size_t uItems = PlaneDesc_getNItems(pPlane);

	DasDs* pDs = new_DasDs(sDsId, pGroup, 2);
	DasDim* pDim = NULL;
	DasDim* pXDim = NULL;
	DasDim* pYDim = NULL;
	DasVar* pVar = NULL;
	DasVar* pOffset = NULL;
	DasVar* pReference = NULL;
	
	/* Dito, see above searching for string CDF
	DasDs_copyInProps(pDs, (DasDesc*)pSd);
	DasDs_copyInProps(pDs, (DasDesc*)pPd); */ /*These never have props in das 2.2 */
	const char* pPlaneId = NULL;
	DasEncoding* pEncoder = NULL;
	const char* sRole = NULL;
	das_units Yunits = UNIT_DIMENSIONLESS;
	das_units Zunits = UNIT_DIMENSIONLESS;
	const char* pYTagId = NULL;
	DasAry* pAry = NULL;
	char sAryId[64] = {'\0'};
	double fill;
	
	/* One thing to watch out for.  The number of dimensions is not equal to
	 * the number of planes.  If two planes have the same 'source' property
	 * but different roles then they go together in the same dimension. 
	 * 
	 * The old das2 stream model is *REALLY* showing it's age and needs to be
	 * updated.  To much hoop jumping and heuristics are needed to shove 
	 * complex info into such a simple structure.  QStream is better, but still
	 * not explicit enough.  We just need a break from the past. -cwp 2019-04-05
	 */
	DasDim* aDims[LEGACY_MAX_DIMS] = {NULL};
	char aDimSrc[LEGACY_MAX_DIMS*LEGACY_SRC_ARY_SZ] = {'\0'};
	size_t uDims = 0;
	
	double* pYTags = NULL;
	bool bAddedYTags = false;
	for(size_t u = 0; u < pPd->uPlanes; ++u){
		pPlane = pPd->planes[u];
		if(pPlane->sName != NULL) pPlaneId = pPlane->sName;
		
		/* Assume this is a center value unless told otherwise */
		sRole	 = _serial_role(pPlane);
		pEncoder = PlaneDesc_getValEncoder(pPlane);
		fill	  = PlaneDesc_getFill(pPlane);
		
		/* If we're lucky the plane will tell us the purpose for it's
		 * values, i.e. min, center, max, err-bar  */

		switch(pPlane->planeType){
		case X:
			if(pPlaneId == NULL){
				if(Units_haveCalRep(pPlane->units)) pPlaneId = "time"; 
				else										  pPlaneId = "X";
			}
	
			pAry = _serial_makeAry(
				bCodecs, pPlaneId, pEncoder, (const ubyte*)&fill, RANK_1(0), pPlane->units
			);
			
			if(pAry == NULL) return NULL;
			DasAry_setSrc(pAry, PktDesc_getId(pPd), u, 1);
			if(DasDs_addAry(pDs, pAry) != DAS_OKAY) return NULL;
			
			pXDim = _serial_getDim(
				pPlane, pPd, pSd, 'x', pDs, DASDIM_COORD, pPlaneId,
				aDims, aDimSrc, &uDims
			);
			if(pXDim == NULL) return NULL; 
		
			/* Map index 0 to time array 0, ignore index 1 */
			pVar = new_DasVarArray(pAry, SCALAR_2(0, DASIDX_UNUSED));
			if(pVar == NULL) return NULL;
			if(! DasDim_addVar(pXDim, sRole, pVar)) return NULL;

			if(bCodecs)
				if(_serial_addCodec(pDs, pPlaneId, 1, pEncoder) != DAS_OKAY)
					return NULL;
			break;
			
		case Y:
			++nY;
			if(pPlaneId == NULL)
				snprintf(sAryId, 63, "Y_%d", nY);
			else
				strncpy(sAryId, pPlaneId, 63);
			_strrep(sAryId, '.', '_');

			pAry = _serial_makeAry(
				bCodecs, sAryId, pEncoder, (const ubyte*)&fill, RANK_1(0), pPlane->units
			);
			if(pAry == NULL) return NULL;

			DasAry_setSrc(pAry, PktDesc_getId(pPd), u, 1);
			if(DasDs_addAry(pDs, pAry) != DAS_OKAY) return NULL;
			
			/* Assume that extra Y values are more coordinates unless told 
			 * otherwise by a setting of some sort that I don't yet know */
			pYDim = _serial_getDim(
				pPlane, pPd, pSd, 'y', pDs, DASDIM_COORD, pPlaneId,
				aDims, aDimSrc, &uDims
			);
			if(pYDim == NULL) return NULL; 
			
			/* Map index 0 to this array, ignore index 1 */
			pVar = new_DasVarArray(pAry, SCALAR_2(0, DASIDX_UNUSED));

			if(pVar == NULL) return NULL;
			if(! DasDim_addVar(pYDim, sRole, pVar)) return NULL;

			if(bCodecs)
				if(_serial_addCodec(pDs, pPlaneId, 1, pEncoder) != DAS_OKAY)
					return NULL;
			break;
			
		case YScan:
			++nYScan;
			
			/* This one is interesting, maybe have to add two arrays.
			 * 
			 * Also sometimes this is a waveform and we need to set the yTags as
			 * an offset dimension for time.
			 */
			
			if(!bAddedYTags){
				Yunits = PlaneDesc_getYTagUnits(pPlane);
				if( Units_canConvert(Yunits, UNIT_HERTZ) ){ 
					pYTagId = "frequency";
				}
				else{
					if(Units_canConvert(Yunits, UNIT_SECONDS)) pYTagId = "offset";
					else{
						if(Units_canConvert(Yunits, UNIT_EV)) pYTagId = "energy";
						else pYTagId = "ytags";
					}
				}
				pAry = new_DasAry(pYTagId, vtDouble, 0, NULL, RANK_1(uItems), Yunits);
				if(pAry == NULL) return NULL;
				if(DasDs_addAry(pDs, pAry) != DAS_OKAY) return NULL;
				pYTags = _serial_yTagVals(pPlane);

				/* Use put instead of append since we've already allocated the space */
				DasAry_putAt(pAry, IDX0(0), (const ubyte*)pYTags, uItems);
				free(pYTags);  pYTags = NULL;
				
				/* If the data aren't waveforms we're going to need to add a new
				 * dimension, otherwise we'll add in the REF and OFFSET variables */
				if( _serial_isWaveform(pPlane) ){
					/* ignore first index, map second index to array */
					pOffset = new_DasVarArray(pAry, SCALAR_2(DASIDX_UNUSED, 0));
					DasDim_addVar(pXDim, DASVAR_OFFSET, pOffset);
					
					/* Convert the old CENTER variable to a Reference variable */
					pReference = DasDim_popVar(pXDim, DASVAR_CENTER);
					DasDim_addVar(pXDim, DASVAR_REF, pReference);
					
					/* Make new center variable that is rank 2 since reference and
					 * offset are orthogonal */
					pVar = new_DasVarBinary("center", pReference, "+", pOffset);
					if(! DasDim_addVar(pXDim, DASVAR_CENTER, pVar) ) return NULL;
				}
				else{
					/* If we have <y> and <yscan> then the Y's are our reference values
						Convert the old CENTER variable to a Reference variable and
						add the Ytags as offsets */
					if(pYDim != NULL){
						pOffset = new_DasVarArray(pAry, SCALAR_2(DASIDX_UNUSED, 0));
						DasDim_addVar(pYDim, DASVAR_OFFSET, pOffset);

						/* Convert the old CENTER variable to a Reference variable */
						pReference = DasDim_popVar(pYDim, DASVAR_CENTER);
						DasDim_addVar(pYDim, DASVAR_REF, pReference);
					
						/* Make new center variable that is rank 2 since reference and
						* offset are orthogonal */
						pVar = new_DasVarBinary("center", pReference, "+", pOffset);
						if(! DasDim_addVar(pYDim, DASVAR_CENTER, pVar) ) return NULL;
					}
					else{
						/* Nope no offsets in freq or time, just a new center variable */
						pDim = DasDs_makeDim(pDs, DASDIM_COORD, pYTagId, "");
						if(pDim == NULL) return NULL;

						DasDim_copyInProps(pDim, 'y', (DasDesc*)pSd);
						DasDim_copyInProps(pDim, 'y', (DasDesc*)pPd);
						DasDim_copyInProps(pDim, 'y', (DasDesc*)pPlane);
					
						/* Map index 1 to this array's index 0, ignore 0, assume  */
						/* center values */
						pVar = new_DasVarArray(pAry, SCALAR_2(DASIDX_UNUSED, 0));
						if(! DasDim_addVar(pDim, DASVAR_CENTER, pVar)) return NULL;
					}
				}
			
				bAddedYTags = true;
			}

			/* Now for the actual data values array, try for a good array name */
			Zunits = PlaneDesc_getUnits(pPlane);
			if(pPlaneId == NULL){
				if(Units_canConvert(Zunits, UNIT_E_SPECDENS)) 
					strncpy(sAryId, "e_spec_dens", 63);
				else if(Units_canConvert(Zunits, UNIT_B_SPECDENS))
					strncpy(sAryId, "b_spec_dens", 63);
				else
					snprintf(sAryId, 63, "YScan_%d", nYScan);
			}
			else{
				strncpy(sAryId, pPlaneId, 63);
			}
			_strrep(sAryId, '.', '_');
						
			pAry = _serial_makeAry(
				bCodecs, sAryId, pEncoder, (const ubyte*)&fill, RANK_2(0, uItems), Zunits
			);
			if(pAry == NULL) return NULL;

			if(DasDs_addAry(pDs, pAry) != DAS_OKAY) return NULL;
			DasAry_setSrc(pAry, PktDesc_getId(pPd), u, uItems);
			
			pDim = _serial_getDim(
				pPlane, pPd, pSd, 'z', pDs, DASDIM_DATA, pPlaneId, aDims, 
				aDimSrc, &uDims
			);
			if(pDim == NULL) return NULL; 
			
			/* Map index 0 to 0 and 1 to 1 */
			pVar = new_DasVarArray(pAry, SCALAR_2(0, 1));
			if(pVar == NULL) return NULL;
			if(! DasDim_addVar(pDim, sRole, pVar)) return NULL;

			if(bCodecs)
				if(_serial_addCodec(pDs, sAryId, (int)uItems, pEncoder) != DAS_OKAY)
					return NULL;

			break;
			
		default:
			das_error(DASERR_DS, "logic error");
			return NULL;
			break;
		}
	}
	return pDs;
}

/* bCodecs - If true, define codecs for the generated dataset object so that
 *	it can be used to parse data packet payloads.
 */
DasDs* dasds_from_packet(DasStream* pSd, PktDesc* pPd, const char* sGroup, bool bCodecs)
{
	/* Initialize based on the observed pattern.  Das2 streams have traditionally
	 * followed certian layout patterns, you can't have arbitrary collections of
	 * <x> <y> <yscan> and <z> planes.	 */
	size_t u, uPlanes = PktDesc_getNPlanes(pPd);
	PlaneDesc* pPlane = NULL;
	int nXs = 0, nYs = 0, nYScans = 0, nZs = 0;
	for(u = 0; u < uPlanes; ++u){
		pPlane = PktDesc_getPlane(pPd, u);
		switch(PlaneDesc_getType(pPlane)){
		case X: ++nXs; break;
		case Y: ++nYs; break;
		case YScan: ++nYScans; break;
		case Z: ++nZs; break;
		case Invalid: das_error(DASERR_DS, "logic error"); return NULL;
		}
	}

	DasDs* pCd = NULL;
	if(nYScans == 0){
		if(nZs != 0) pCd = _serial_initXYZ(pSd, pPd, sGroup, bCodecs);
		else{
			if(nXs == 2) pCd = _serial_initEvents(pSd, pPd, sGroup);
			else pCd = _serial_initXY(pSd, pPd, sGroup, bCodecs);
		}
	}
	else{
		pCd = _serial_initYScan(pSd, pPd, sGroup, bCodecs);
	}
	return pCd;
}
