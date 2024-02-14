/* Copyright (C) 2017 Chris Piker <chris-piker@uiowa.edu>
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
#include <string.h>
#include <assert.h>
#include <ctype.h>

#include "log.h"
#include "io.h"
#include "variable.h"
#include "dataset.h"
#include "builder.h"

/* Max number of dimensions in a dataset */
#define DASBLDR_MAX_DIMS 64
#define DASBLDR_SRC_ARY_SZ 64

/* ************************************************************************** */
/* Specialized property copies only used by the das2 builder */


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


/* ************************************************************************** */
/* Helpers */

size_t _DasDsBldr_addPair(DasDsBldr* pThis, PktDesc* pPd, DasDs* pCd)
{
	struct ds_pd_set* pNewSet = NULL;
	if(pThis->uSzPairs < (pThis->uValidPairs + 1)){
		pThis->uSzPairs *= 2;
		if(pThis->uSzPairs < 16) pThis->uSzPairs = 16;
		pNewSet = (struct ds_pd_set*) calloc(pThis->uSzPairs, sizeof(struct ds_pd_set));

		if(pThis->uValidPairs > 0)
			memcpy(pNewSet, pThis->lPairs, pThis->uValidPairs * sizeof(struct ds_pd_set));
		pThis->lPairs = pNewSet;
	}

	/* Save a copy of the packet descriptor, including it's planes.  This is
	 * used to see if this packet descriptor has been seen before. */
	PktDesc* pNewPd = new_PktDesc();
	PktDesc_copyPlanes(pNewPd, pPd);

	pThis->lPairs[pThis->uValidPairs].pPd = pNewPd;
	pThis->lPairs[pThis->uValidPairs].pDs = pCd;
	pThis->uValidPairs += 1;
	return (pThis->uValidPairs - 1);
}

/* ************************************************************************** */
/* On new stream  */

DasErrCode DasDsBldr_onStreamDesc(StreamDesc* pSd, void* vpUd)
{
	DasDsBldr* pThis = (DasDsBldr*)vpUd;

	DasDesc_copyIn((DasDesc*) pThis->pProps, (DasDesc*)pSd);


	return DAS_OKAY;
}

/* ************************************************************************* */
/* On new packet type */

int _DasDsBldr_hasContainer(DasDsBldr* pThis, PktDesc* pPd)
{
	/* Find a container whose packet descriptor matches this one.  This
	 * has to have the same number of planes in the same places with the
	 * the same ytag values. */
	PlaneDesc* pPlane = NULL;
	PlaneDesc* pPlTest = NULL;

	PktDesc* pPdTest = NULL;
	ptrdiff_t iMatch = -1;
	bool bSame = false;

	size_t u,v,w;

	for(u = 0; u < pThis->uValidPairs; ++u){
		pPdTest = pThis->lPairs[u].pPd;

		/* Check number of planes */
		if(pPd->uPlanes != pPdTest->uPlanes) continue;

		bSame = true;
		for(v = 0; v < pPd->uPlanes; ++v){
			pPlane = pPd->planes[v];
			pPlTest = pPdTest->planes[v];

			/* Check plane type */
			if(pPlane->planeType != pPlTest->planeType){bSame = false; break;}

			/* Check item length */
			if(PlaneDesc_getNItems(pPlane) != PlaneDesc_getNItems(pPlTest)){
				bSame = false; break;
			}

			/* Check units */
			if( strcmp(pPlane->units, pPlTest->units) != 0){
				bSame = false; break;
			}

			/* Check names (careful, either of these may be null) */
			if(pPlane->sName && !(pPlTest->sName)) {bSame = false; break;}
			if(!(pPlane->sName) && pPlTest->sName) {bSame = false; break;}
			if(pPlane->sName && pPlTest->sName &&
			   strcmp(pPlane->sName, pPlTest->sName) != 0) {
				bSame = false; break;
			}

			/* For YScans, check the yTags */
			if(pPlane->planeType == YScan){
				if(pPlane->ytag_spec != pPlTest->ytag_spec){bSame=false; break;}
				switch(pPlane->ytag_spec){
					case ytags_list:
						for(w=0;w<pPlane->uItems;++w){
							if(pPlane->pYTags[w] != pPlTest->pYTags[w]){
								bSame=false; break;
							}
						}
						break;
					case ytags_series:
						if((pPlane->yTagInter != pPlTest->yTagInter)||
						   (pPlane->yTagMin != pPlTest->yTagMin) ||
							(pPlane->yTagMax != pPlTest->yTagMax)) bSame = false;

						break;
					default:
						break;
				}
				if(!bSame) break;
			}
		}

		if(bSame){ iMatch = u; break;}
	}

	return iMatch;   /* They are going to have to make a new one */
}

/* Loop through the existing pairs.  If you come across one that has the same
 * number and types of planes, and each plane has the same name as mine then
 * re-use it's group ID.  If not, take my first data plane name as the group
 * id.  */
char* _DasDsBldr_getExistingGroup(
	DasDsBldr* pThis, PktDesc* pPd, char* sGroupId, size_t uLen
){

	if(uLen < 2){das_error(DASERR_BLDR, "Really?"); 	return NULL; }

	PlaneDesc* pPlane = NULL;
	PlaneDesc* pPlTest = NULL;

	PktDesc* pPdTest = NULL;
	ptrdiff_t iMatch = -1;
	bool bSimilar = false;

	for(size_t u = 0; u < pThis->uValidPairs; ++u){
		pPdTest = pThis->lPairs[u].pPd;

		/* Check number of planes */
		if(pPd->uPlanes != pPdTest->uPlanes) continue;

		bSimilar = true;
		for(size_t v = 0; v < pPd->uPlanes; ++v){
			pPlane = pPd->planes[v];
			pPlTest = pPdTest->planes[v];

			/* Check plane type */
			if(pPlane->planeType != pPlTest->planeType){bSimilar = false; break;}

			/* Check units */
			if( strcmp(pPlane->units, pPlTest->units) != 0){
				bSimilar = false; break;
			}

			/* Check names (careful, either of these may be null) */
			if(pPlane->sName && !(pPlTest->sName)) {bSimilar = false; break;}
			if(!(pPlane->sName) && pPlTest->sName) {bSimilar = false; break;}
			if(pPlane->sName && pPlTest->sName &&
			   strcmp(pPlane->sName, pPlTest->sName) != 0) {
				bSimilar = false; break;
			}
		}

		if(bSimilar){ iMatch = u; break;}
	}

	if(iMatch != -1){
		strncpy(sGroupId, pThis->lPairs[iMatch].pDs->sGroupId, uLen-1);
		return sGroupId;
	}

	return NULL;   /* They are going to have to make something up */
}

/* ************************************************************************* */
/* Inspect plane properties and output standardized dimension role string    */

const char* _DasDsBldr_role(PlaneDesc* pPlane)
{
	const char* sRole = DasDesc_get((DasDesc*)pPlane, "operation");
	if(sRole == NULL) return DASVAR_CENTER;
	
	/* Interpret  Autoplot style strings */
	if(strcmp("BIN_AVG", sRole) == 0) return DASVAR_MEAN;
	if(strcmp("BIN_MAX", sRole) == 0) return DASVAR_MAX;
	if(strcmp("BIN_MIN", sRole) == 0) return DASVAR_MIN;
	
	return DASVAR_CENTER;
}

/* ************************************************************************* */
/* Handle matching up planes into single dimensions, interface is really     */
/* complicated because this code was inline in another function              */

DasDim* _DasDsBldr_getDim(
	PlaneDesc* pPlane, PktDesc* pPd, StreamDesc* pSd,
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
			p = pDimSrc + u*DASBLDR_SRC_ARY_SZ;
			if(strncmp(sSource, p, DASBLDR_SRC_ARY_SZ) == 0){
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
		
		if((*puDims) + 1 >= DASBLDR_MAX_DIMS){
			das_error(DASERR_BLDR, "Too many dimensions in a single packet %d",
					    DASBLDR_MAX_DIMS);
			return NULL;
		}
		
		ppDims[*puDims] = pDim;
	
		p = pDimSrc + (*puDims)*DASBLDR_SRC_ARY_SZ;
		strncpy(p, sSource, DASBLDR_SRC_ARY_SZ - 1);
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


DasDs* _DasDsBldr_initXY(StreamDesc* pSd, PktDesc* pPd, const char* pGroup)
{
	/* If my group name is null, make up a new one appropriate to XY data */
	const char* pId = NULL;
	PlaneDesc* pPlane = NULL;
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
	enum dim_type dType = DASDIM_UNK;
	
	/* Tracking for array groups (aka dimensions) */
	DasDim* aDims[DASBLDR_MAX_DIMS] = {NULL};
	char aDimSrc[DASBLDR_MAX_DIMS * DASBLDR_SRC_ARY_SZ] = {'\0'};
	size_t uDims = 0;
	
	/* Copy any properties that don't start with one of the axis prefixes */
	DasDs_copyInProps(pDs, (DasDesc*)pSd);
	DasDs_copyInProps(pDs, (DasDesc*)pPd);
	
	char sAryId[64] = {'\0'};
	double fill;
	char cAxis = '\0';
	
	for(size_t u = 0; u < pPd->uPlanes; ++u){
		pPlane = pPd->planes[u];
		pId = pPlane->sName;
		if(pPlane->planeType == X){
			cAxis = 'x';
			if(pId == NULL){
				if(Units_haveCalRep(pPlane->units)) pId = "time";
				else pId = "X";
			}
			
			/* Fill is not allowed for Das 2.2 X planes */ 
			/* Also das 2.2 planes always convert ASCII times to doubles.
			 * Though it's probably un-needed processing, we at leas know 
			 * the array types will always be etDouble */
			pAry = new_DasAry(pId, vtDouble, 0, NULL, RANK_1(0), pPlane->units);
		}
		else{
			cAxis = 'y';
			++nY;
			
			/* Always copy over the name if it exists */			
			if(pId == NULL) snprintf(sAryId, 63, "Y_%d", nY);
			else            strncpy(sAryId, pId, 63);
			_strrep(sAryId, '.', '_');  /* handle amplitude.max type stuff */

			fill = PlaneDesc_getFill(pPlane);
			pAry = new_DasAry(
				sAryId, vtDouble, 0, (const ubyte*)&fill, RANK_1(0), pPlane->units
			);
		}
		if(pAry == NULL) return NULL;
		
		/* add the new array to the DS so it has somewhere to put this item from
		 * the packets */
		if(! DasDs_addAry(pDs, pAry) ) return NULL;
		
		/* Remember how to fill this array.  This will get more complicated
		 * when variable length packets are introduced */
		DasAry_setSrc(pAry, PktDesc_getId(pPd), u, 1);
		
		/* On to higher level data organizational structures:  Create dimensions
		 * and variables as needed for the new arrays */
		if(pPlane->planeType == X) dType = DASDIM_COORD;
		else dType = DASDIM_DATA;
		
		pDim = _DasDsBldr_getDim(
			pPlane, pPd, pSd, cAxis, pDs, dType, pId, aDims, aDimSrc, &uDims
		);
		if(pDim == NULL) return NULL;
		
		pVar = new_DasVarArray(pAry, SCALAR_1(0));
		if( pVar == NULL) return NULL;
		sRole = _DasDsBldr_role(pPlane);
		if(! DasDim_addVar(pDim, sRole, pVar)) return NULL;
	}
	return pDs;
}

/* ************************************************************************* */
/* Initialize X-Y-Z pattern */

DasDs* _DasDsBldr_initXYZ(StreamDesc* pSd, PktDesc* pPd, const char* pGroup)
{
	/* If my group name is null, make up a new one appropriate to XYZ data */
	const char* pId = NULL;
	PlaneDesc* pPlane = NULL;
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
	enum dim_type dType = DASDIM_UNK;
	
	/* Tracking for array groups (aka dimensions) */
	DasDim* aDims[DASBLDR_MAX_DIMS] = {NULL};
	char aDimSrc[DASBLDR_MAX_DIMS * DASBLDR_SRC_ARY_SZ] = {'\0'};
	size_t uDims = 0;
	
	/* Copy any properties that don't start with one of the axis prefixes */
	DasDs_copyInProps(pDs, (DasDesc*)pSd);
	DasDs_copyInProps(pDs, (DasDesc*)pPd);
	
	char sAryId[64] = {'\0'};
	double fill;
	char cAxis = '\0';
	
	for(size_t u = 0; u < pPd->uPlanes; ++u){
		pPlane = pPd->planes[u];
		pId = pPlane->sName;
		
		
		switch(pPlane->planeType){
		case X:
			cAxis = 'x';
			if(pId == NULL){
				if(Units_haveCalRep(pPlane->units)) pId = "time";
				else pId = "X";
			}
			
			/* Fill is not allowed for Das 2.2 X planes */
			/* Also das 2.2 planes always convert ASCII times to doubles.
			 * Though it's probably un-needed processing, we at least know 
			 * the array types will always be etDouble */
			pAry = new_DasAry(pId, vtDouble, 0, NULL, RANK_1(0), pPlane->units);
			break;
			
		case Y:
			cAxis = 'y';
			if(pId == NULL)pId = "Y";

			/* Fill is not allowed for Das 2.2 Y planes in an X,Y,Z pattern */
			pAry = new_DasAry(pId, vtDouble, 0, NULL, RANK_1(0), pPlane->units);
			break;
			
		case Z:
			cAxis = 'z';
			++nZ;
			
			if(pId == NULL)  snprintf(sAryId, 63, "Z_%d", nZ);
			else             strncpy(sAryId, pId, 63);
			_strrep(sAryId, '.', '_');   /* handle amplitude.max stuff */

			fill = PlaneDesc_getFill(pPlane);
			pAry = new_DasAry(
				sAryId, vtDouble, 0, (const ubyte*)&fill, RANK_1(0), pPlane->units
			);
			break;
			
		
		default:
			das_error(DASERR_BLDR, "Unexpected plane type in XYZ pattern");
			return NULL;
		}
		
		if(pAry == NULL) return NULL;
		
		/* add the new array to the DS so it has somewhere to put this item from
		 * the packets */
		if(! DasDs_addAry(pDs, pAry) ) return NULL;
		
		/* Remember how to fill this array.  This will get more complicated
		 * when variable length packets are introduced.  So this is item 'u'
		 * from the packet and 1 item is read for each packet */
		DasAry_setSrc(pAry, PktDesc_getId(pPd), u, 1);
		
		/* Create dimensions and variables as needed for the new arrays */
		if(pPlane->planeType == Z) dType = DASDIM_DATA;
		else dType = DASDIM_COORD;
		
		pDim = _DasDsBldr_getDim(
			pPlane, pPd, pSd, cAxis, pDs, dType, pId, aDims, aDimSrc, &uDims
		);
		if(pDim == NULL) return NULL;
		
		pVar = new_DasVarArray(pAry, SCALAR_1(0));
		if( pVar == NULL) return NULL;
		
		/* Assume these are center values unless there is a property stating
		 * otherwise */
		sRole = _DasDsBldr_role(pPlane);
		if(! DasDim_addVar(pDim, sRole, pVar)) return NULL;
	}
	
	return pDs;
}

/* ************************************************************************* */
/* Initialize Events Pattern */

DasDs* _DasDsBldr_initEvents(StreamDesc* pSd, PktDesc* pPd, const char* sGroupId)
{
	das_error(DASERR_BLDR, "Event stream reading has not been implemented");
	return NULL;
}

/* ************************************************************************* */
/* Initialize YScan Pattern */

bool _DasDsBldr_checkYTags(PktDesc* pPd)
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

double* _DasDsBldr_yTagVals(PlaneDesc* pPlane)
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

bool _DasDsBldr_isWaveform(PlaneDesc* pPlane){

	const char* sRend = DasDesc_getStr((DasDesc*)pPlane, "renderer");
	if(sRend == NULL) return false;
	if(strcmp("waveform", sRend) != 0) return false;
	
	das_units units = PlaneDesc_getYTagUnits(pPlane);
	if(! Units_canConvert(units, UNIT_SECONDS)) return false;
	return true;
}

DasDs* _DasDsBldr_initYScan(StreamDesc* pSd, PktDesc* pPd, const char* pGroup)
{
	/* Make sure all the yscans have the same yTags.  The assumption here is
	 * that a single packet only has data correlated in it's coordinates.  If
	 * two yscans have different yTag sets then they are not correlated. */
	
	if(!_DasDsBldr_checkYTags(pPd)){
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
	DasVar* pVar = NULL;
	DasVar* pOffset = NULL;
	DasVar* pReference = NULL;
	

	DasDs_copyInProps(pDs, (DasDesc*)pSd);
	DasDs_copyInProps(pDs, (DasDesc*)pPd); /*These never have props in das 2.2 */
	const char* pPlaneId = NULL;
	const char* sRole = NULL;
	das_units Yunits;
	das_units Zunits;
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
	DasDim* aDims[DASBLDR_MAX_DIMS] = {NULL};
	char aDimSrc[DASBLDR_MAX_DIMS*DASBLDR_SRC_ARY_SZ] = {'\0'};
	size_t uDims = 0;
	
	double* pYTags = NULL;
	bool bAddedYTags = false;
	for(size_t u = 0; u < pPd->uPlanes; ++u){
		pPlane = pPd->planes[u];
		if(pPlane->sName != NULL) pPlaneId = pPlane->sName;
		
		/* Assume this is a center value unless told otherwise */
		sRole = _DasDsBldr_role(pPlane);
		
		/* If we're lucky the plane will tell us the purpose for it's
		 * values, i.e. min, center, max, err-bar  */

		switch(pPlane->planeType){
		case X:
			if(pPlaneId == NULL){
				if(Units_haveCalRep(pPlane->units)) pPlaneId = "time";
				else pPlaneId = "X";
			}
			pAry = new_DasAry(pPlaneId, vtDouble, 0, NULL, RANK_1(0), pPlane->units);
			if(pAry == NULL) return NULL;
			DasAry_setSrc(pAry, PktDesc_getId(pPd), u, 1);
			if(!DasDs_addAry(pDs, pAry)) return NULL;
			
			pXDim = _DasDsBldr_getDim(
				pPlane, pPd, pSd, 'x', pDs, DASDIM_COORD, pPlaneId,
				aDims, aDimSrc, &uDims
			);
			if(pXDim == NULL) return NULL; 
			
		
			/* Map index 0 to time array 0, ignore index 1 */
			pVar = new_DasVarArray(pAry, SCALAR_2(0, DASIDX_UNUSED));
			if(pVar == NULL) return NULL;
			if(! DasDim_addVar(pXDim, sRole, pVar)) return NULL;
			break;
			
		case Y:
			++nY;
			if(pPlaneId == NULL)
				snprintf(sAryId, 63, "Y_%d", nY);
			else
				strncpy(sAryId, pPlaneId, 63);
			_strrep(sAryId, '.', '_');

			fill = PlaneDesc_getFill(pPlane);
			pAry = new_DasAry(
				sAryId, vtDouble, 0, (const ubyte*)&fill, RANK_1(0), pPlane->units
			);
			if(pAry == NULL) return NULL;
			DasAry_setSrc(pAry, PktDesc_getId(pPd), u, 1);
			if(!DasDs_addAry(pDs, pAry)) return NULL;
			
			/* Assume that extra Y values are more coordinates unless told 
			 * otherwise by a setting of some sort that I don't yet know */
			pDim = _DasDsBldr_getDim(
				pPlane, pPd, pSd, 'y', pDs, DASDIM_COORD, pPlaneId,
				aDims, aDimSrc, &uDims
			);
			if(pDim == NULL) return NULL; 
			
			/* Map index 0 to this array, ignore index 1 */
			pVar = new_DasVarArray(pAry, SCALAR_2(0, DASIDX_UNUSED));
			if(pVar == NULL) return NULL;
			if(! DasDim_addVar(pDim, sRole, pVar)) return NULL;
			
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
				if(!DasDs_addAry(pDs, pAry)) return NULL;
				pYTags = _DasDsBldr_yTagVals(pPlane);

				/* Use put instead of append since we've already allocated the space */
				DasAry_putAt(pAry, IDX0(0), (const ubyte*)pYTags, uItems);
				free(pYTags);  pYTags = NULL;
				
				/* If the data aren't waveforms we're going to need to add a new
				 * dimension, otherwise we'll add in the REF and OFFSET variables */
				if( _DasDsBldr_isWaveform(pPlane) ){
					/* ignore first index, map second index to array */
					pOffset = new_DasVarArray(pAry, SCALAR_2(DASIDX_UNUSED, 0));
					DasDim_addVar(pXDim, DASVAR_OFFSET, pOffset);
					
					/* Convert the old CENTER variable to a Reference variable */
					pReference = DasDim_popVar(pXDim, DASVAR_CENTER);
					DasDim_addVar(pXDim, DASVAR_REF, pReference);
					
					/* Make new center variable that is rank 2 since reference and
					 * offset are orthogonal */
					pVar = new_DasVarBinary("center", pReference, "+", pOffset);
					DasDim_addVar(pXDim, DASVAR_CENTER, pVar);
				}
				else{
					pDim = DasDs_makeDim(pDs, DASDIM_COORD, pYTagId, "");
					if(pDim == NULL) return NULL;
					DasDim_copyInProps(pDim, 'y', (DasDesc*)pSd);
					DasDim_copyInProps(pDim, 'y', (DasDesc*)pPd);
					DasDim_copyInProps(pDim, 'y', (DasDesc*)pPlane);
					
					/* Map index 1 to this array's index 0, ignore 0, always assume  */
					/* center values */
					pVar = new_DasVarArray(pAry, SCALAR_2(DASIDX_UNUSED, 0));
					if(pVar == NULL) return NULL;
					if(! DasDim_addVar(pDim, DASVAR_CENTER, pVar)) return NULL;
				}
			
				bAddedYTags = true;
			}

			/* Now for the actual data values array, try for a good array name */
			Zunits = PlaneDesc_getUnits(pPlane);
			if(pPlaneId == NULL){
				if(Units_canConvert(Zunits, UNIT_E_SPECDENS)){
					strncpy(sAryId, "e_spec_dens", 63);
				}
				else{
					if(Units_canConvert(Zunits, UNIT_B_SPECDENS))
						strncpy(sAryId, "b_spec_dens", 63);
					else
						snprintf(sAryId, 63, "YScan_%d", nYScan);
				}
			}
			else{
				strncpy(sAryId, pPlaneId, 63);
			}
			_strrep(sAryId, '.', '_');
						
			
			fill = PlaneDesc_getFill(pPlane);
			pAry = new_DasAry(
				sAryId, vtDouble, 0, (const ubyte*)&fill, RANK_2(0, uItems), Zunits
			);
			if(pAry == NULL) return NULL;
			if(!DasDs_addAry(pDs, pAry)) return NULL;
			DasAry_setSrc(pAry, PktDesc_getId(pPd), u, uItems);
			
			pDim = _DasDsBldr_getDim(
				pPlane, pPd, pSd, 'z', pDs, DASDIM_DATA, pPlaneId, aDims, 
				aDimSrc, &uDims
			);
			if(pDim == NULL) return NULL; 
			
			/* Map index 0 to 0 and 1 to 1 */
			pVar = new_DasVarArray(pAry, SCALAR_2(0, 1));
			if(pVar == NULL) return NULL;
			if(! DasDim_addVar(pDim, sRole, pVar)) return NULL;
			break;
			
		default:
			das_error(DASERR_DS, "logic error");
			return NULL;
			break;
		}
	}
	return pDs;
}

/* Get the correlated dataset container that corresponds to the given packet
 * descriptor.  This uses duck typing.  If a packet descriptor has the same
 * number and type of planes, and each plane has the same name (and ytags if
 * applicable) then it is considered to describe the same data object.
 *
 * The index of the container is stored in the lDsMap array. */

DasErrCode DasDsBldr_onPktDesc(StreamDesc* pSd, PktDesc* pPd, void* vpUd)
{
	DasDsBldr* pThis = (DasDsBldr*)vpUd;

	/* The big question, are they defining a new dataset (in which case the
	 * old one needs to be kept, or are they starting an actual new one) */
	int iPktId = PktDesc_getId(pPd);
	int iPairIdx = -1;
	if(pThis->lDsMap[iPktId] != -1){
		if( (iPairIdx = _DasDsBldr_hasContainer(pThis, pPd)) != -1){
			/* Reuse old CorData */
			pThis->lDsMap[iPktId] = iPairIdx;
			return DAS_OKAY;
		}
	}

	char sGroupId[64] = {'\0'};
	const char* pGroup = _DasDsBldr_getExistingGroup(pThis, pPd, sGroupId, 64);

	/* Initialize based on the observed pattern.  Das2 streams have traditionally
	 * followed certian layout patterns, you can't have arbitrary collections of
	 * <x> <y> <yscan> and <z> planes.    */
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
		case Invalid: das_error(DASERR_DS, "logic error"); return DASERR_DS;
		}
	}

	DasDs* pCd = NULL;
	if(nYScans == 0){
		if(nZs != 0) pCd = _DasDsBldr_initXYZ(pSd, pPd, pGroup);
		else{
			if(nXs == 2) pCd = _DasDsBldr_initEvents(pSd, pPd, pGroup);
			else pCd = _DasDsBldr_initXY(pSd, pPd, pGroup);
		}
	}
	else{
		pCd = _DasDsBldr_initYScan(pSd, pPd, pGroup);
	}
	if(!pCd) return DASERR_BLDR;

	size_t uIdx = _DasDsBldr_addPair(pThis, pPd, pCd);
	pThis->lDsMap[iPktId] = uIdx;

	return DAS_OKAY;
}

/* ************************************************************************* */

DasErrCode DasDsBldr_onPktData(PktDesc* pPd, void* vpUd)
{
	DasDsBldr* pThis = (DasDsBldr*)vpUd;
	int nPktId = PktDesc_getId(pPd);

	struct ds_pd_set set = pThis->lPairs[ pThis->lDsMap[nPktId] ];
	DasDs* pDs = set.pDs;

	/* Loop through all the arrays in the dataset object and add values */
	PlaneDesc* pPlane = NULL;
	DasAry* pAry = NULL;
	for(size_t u = 0; u < pDs->uArrays; ++u){
		pAry = pDs->lArrays[u];
		if(pAry->nSrcPktId != nPktId){
			assert(pAry->nSrcPktId < 1);
			continue;
		}
		pPlane = PktDesc_getPlane(pPd, pAry->uStartItem);
		assert(pAry->uItems == pPlane->uItems);
		DasAry_append(pAry, (const ubyte*) PlaneDesc_getValues(pPlane), pAry->uItems);
	}

	return DAS_OKAY;
}

/* ************************************************************************* */

DasErrCode DasDsBldr_onComment(OobComment* pSc, void* vpUd)
{
	/* Builder* pThis = (Builder*)vpUd; */
	return DAS_OKAY;
}


/* ************************************************************************* */

DasErrCode DasDsBldr_onException(OobExcept* pSe, void* vpUd)
{
	/* Builder* pThis = (Builder*)vpUd; */
	return DAS_OKAY;
}

DasErrCode DasDsBldr_onClose(StreamDesc* pSd, void* vpUd)
{
	/* Go through all the datasets and turn-off mutability, they can always 
	 * turn on mutability again if need be.
	 * This will cache the dataset size. */
	DasDsBldr* pThis = (DasDsBldr*)vpUd;
	
	for(int i = 0; i < pThis->uValidPairs; ++i)
		DasDs_setMutable(pThis->lPairs[i].pDs, false);
	
	return DAS_OKAY;
}

/* ************************************************************************** */
/* Constructor */

DasDsBldr* new_DasDsBldr(void)
{
	DasDsBldr* pThis = (DasDsBldr*) calloc(1, sizeof(DasDsBldr));
	DasDesc* pDesc = (DasDesc*)calloc(1, sizeof(DasDesc));
	pThis->pProps = pDesc;
	DasDesc_init(pThis->pProps,STREAM);

	pThis->base.userData = pThis;
	pThis->base.streamDescHandler = DasDsBldr_onStreamDesc;
	pThis->base.pktDescHandler = DasDsBldr_onPktDesc;
	pThis->base.pktDataHandler = DasDsBldr_onPktData;
	pThis->base.exceptionHandler = DasDsBldr_onException;
	pThis->base.closeHandler = DasDsBldr_onClose;
	pThis->base.commentHandler = DasDsBldr_onComment;
	pThis->_released = false;

	for(int i = 0; i<MAX_PKTIDS; ++i) pThis->lDsMap[i] = -1;

	/* Make room for 64 dataset pointers, can always expand if needed */
	pThis->uValidPairs = 0;
	pThis->uSzPairs = 64;
	pThis->lPairs = (struct ds_pd_set*)calloc(
		pThis->uSzPairs, sizeof(struct ds_pd_set)
	);

	return pThis;
}

void DasDsBldr_release(DasDsBldr* pThis){ 
	pThis->_released = true;
	
}


DasDs** DasDsBldr_getDataSets(DasDsBldr* pThis, size_t* pLen){
	if(pThis->uValidPairs == 0) return NULL;

	*pLen = pThis->uValidPairs;
	DasDs** pRet = (DasDs**)calloc(pThis->uValidPairs, sizeof(void*));
	for(int i = 0; i < pThis->uValidPairs; ++i) pRet[i] = pThis->lPairs[i].pDs;
	return pRet;
}

DasDesc* DasDsBldr_getProps(DasDsBldr* pThis)
{
	/* Hope they call release if they want to keep these */
	return pThis->pProps;  
}

void del_DasDsBldr(DasDsBldr* pThis){

	if(! pThis->_released){
		/* I own the correlated datasets, */
		for(size_t u = 0; u < pThis->uValidPairs; ++u)
			del_DasDs(pThis->lPairs[u].pDs);

		free(pThis->pProps);
	}

	for(size_t u = 0; u < pThis->uValidPairs; ++u)
		del_PktDesc(pThis->lPairs[u].pPd);

   free(pThis->lPairs);
	free(pThis);
}


/* ************************************************************************* */
/* Convenience functions */

DasDs** build_from_stdin(const char* sProgName, size_t* pSets, DasDesc** ppGlobal)
{
	daslog_info("Reading Das2 stream from standard input");

	DasIO* pIn = new_DasIO_cfile(sProgName, stdin, "r");
	DasDsBldr* pBldr = new_DasDsBldr();
	DasIO_addProcessor(pIn, (StreamHandler*)pBldr);

	if(DasIO_readAll(pIn) != 0){
		daslog_info("Error processing standard input");
		del_DasIO(pIn);
		del_DasDsBldr(pBldr);
		return NULL;
	}
	del_DasIO(pIn);
	DasDs** lCorDs = DasDsBldr_getDataSets(pBldr, pSets);
	*ppGlobal = DasDsBldr_getProps(pBldr);
	DasDsBldr_release(pBldr);
	daslog_info_v("%zu Correlated Datasets retrieved from stdin", *pSets);
	return lCorDs;
}


