/* Copyright (C) 2024  Chris Piker <chris-piker@uiowa.edu>
 *
 * This file used to be named parsetime.c.  It is part of das2C, the Core
 * Das2 C Library.
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
 * version 2.1 along with libdas2; if not, see <http://www.gnu.org/licenses/>. 
 */

#define _POSIX_C_SOURCE 200112L

#include <string.h>
#ifndef _WIN32
#include <strings.h>
#else
#define strcasecmp _stricmp
#endif

#include "form_geoloc.h"
#include "geovec.h"
#include "log.h"

/* The component-system vocabulary that used to live here has MOVED to the
   formalisms that own it: form_vector.c for the four systems a free vector
   may use, form_geoloc.c for the two ellipsoidal ones plus the das_geosys_*
   superset lookups.  Only the packed das_geovec payload is left here, and it
   retires with vtGeoVec. */

DasErrCode das_geovec_init(
	das_geovec* pVec, const ubyte* pData, ubyte frame, ubyte surfid, 
	ubyte systype, ubyte et, ubyte esize,  ubyte ncomp, ubyte dirs
){

	pVec->frame = frame;
	pVec->surfid = surfid;
	if(systype > DAS_VSYS_MAX)
		return das_error(DASERR_VEC,
			"Unknown coordinate system type code %hhu", systype);
	pVec->systype = systype;
	pVec->esize = das_vt_size(et);
	pVec->ncomp = ncomp;

	if((ncomp < 1)||(ncomp > 3))
		return das_error(DASERR_VEC, "Geometric vectors must have 1 to 3 components");

	/* Check that all directions are non-zero and different */
	int nX = dirs & 0x3;
	int nY = (dirs >> 2) & 0x3;
	int nZ = (dirs >> 4) & 0x3;
	if(
		((ncomp > 1)&&(nX == nY))||
		((ncomp > 2)&&((nX == nY)||(nX == nZ)||(nY == nZ)) )
	)
		return das_error(DASERR_VEC, "Repeated components in vector variable definition");

	pVec->dirs = dirs;
		
	/* Set the data */
	switch(et){
	case vtByte:
	case vtUByte:
		for(int i = 0; (i < ncomp)&&(i<3); ++i)
			((ubyte*)(pVec->comp))[i] = pData[i];
		break;
	case vtShort:
	case vtUShort:
		for(int i = 0; (i < ncomp)&&(i<3); ++i)
			((uint16_t*)(pVec->comp))[i] = ((uint16_t*)pData)[i];
		break;
	case vtUInt:
	case vtInt:
		for(int i = 0; (i < ncomp)&&(i<3); ++i)
			((uint32_t*)(pVec->comp))[i] = ((uint32_t*)pData)[i];
		break;
	case vtULong:
	case vtLong:
		for(int i = 0; (i < ncomp)&&(i<3); ++i)
			((uint64_t*)(pVec->comp))[i] = ((uint64_t*)pData)[i];
		break;
	case vtFloat:
		for(int i = 0; (i < ncomp)&&(i<3); ++i)
			((float*)(pVec->comp))[i] = ((float*)pData)[i];
		break;
	case vtDouble:
		for(int i = 0; (i < ncomp)&&(i<3); ++i)
			((double*)(pVec->comp))[i] = ((double*)pData)[i];
		break;
	default:
		return das_error(DASERR_VEC, "Invalid element type for vector %d", et);
	}
	pVec->et = et;

	return DAS_OKAY;
}

/* 
ubyte das_geovec_addDir(ubyte curDir, int iComp, int iDir){
	int _comp = iComp & 0x3;
	int _dir  = iDir & 0x3;

	curDir |= (iDir << (2*iComp));

	return curDir;
}
*/

int das_geovec_dir(const das_geovec* pThis, int i)
{
	if((i < 0)||(i > pThis->ncomp))
		return -1*das_error(DASERR_VEC, "Invalid vector component index");
	return (pThis->dirs >> (2*i)) & 0x3;
}

int das_geovec_dirs(const das_geovec* pThis, ubyte* pDirs)
{
	for(int i = 0; i < pThis->ncomp; ++i){
		pDirs[i] = (pThis->dirs << i*2)&0x3;
	}
	return pThis->ncomp;
}

/* Dude, when I redo this lib in D, sumtype is going to save a hell of 
   alot of typing */

DasErrCode das_geovec_values(das_geovec* pThis, double* pValues)
{
	if(pThis->ncomp == 0) 
		return das_error(DASERR_VEC, "Geometric vector is not initialized");

	size_t i = 0;

	/* set default values to handle missing components.  Only matters
		for non-cartesian systems */
	  
	if((pThis->systype == DAS_VSYS_CYL)||(pThis->systype == DAS_VSYS_SPH)||
		(pThis->systype == DAS_VSYS_CENTRIC)
	)
		pValues[0] = 1.0;

	/* Remap based on dirs, sure wish I had room to save this away */
	int dirs[3] = {0};
	for(int i = 0; i < pThis->ncomp; ++i)
		dirs[i] = (pThis->dirs >> i*2)&0x3;	

	switch(pThis->et){
	case vtByte:
		for(i = 0; i < pThis->ncomp; ++i)
			pValues[dirs[i]] = ((int8_t*)(pThis->comp))[ i ];
		return DAS_OKAY;

	case vtUByte:
		for(i = 0; i < pThis->ncomp; ++i)
			pValues[dirs[i]] = ((ubyte*)(pThis->comp))[ i ];
		return DAS_OKAY;

	case vtShort:
		for(i = 0; i < pThis->ncomp; ++i)
			pValues[dirs[i]] = ((int16_t*)(pThis->comp))[ i ];
		return DAS_OKAY;

	case vtUShort:
		for(i = 0; i < pThis->ncomp; ++i)
			pValues[dirs[i]] = ((uint16_t*)(pThis->comp))[ i ];
		return DAS_OKAY;

	case vtInt:
		for(i = 0; i < pThis->ncomp; ++i)
			pValues[dirs[i]] = ((int32_t*)(pThis->comp))[ i ];
		return DAS_OKAY;

	case vtUInt:
		for(i = 0; i < pThis->ncomp; ++i)
			pValues[dirs[i]] = ((uint32_t*)(pThis->comp))[ i ];
		return DAS_OKAY;

	case vtLong:
		for(i = 0; i < pThis->ncomp; ++i)
			pValues[dirs[i]] = ((int64_t*)(pThis->comp))[ i ];
		return DAS_OKAY;

	case vtULong:
		for(i = 0; i < pThis->ncomp; ++i)
			pValues[dirs[i]] = ((uint64_t*)(pThis->comp))[ i ];
		return DAS_OKAY;

	case vtFloat:
		for(i = 0; i < pThis->ncomp; ++i)
			pValues[dirs[i]] = ((float*)(pThis->comp))[ i ];
		return DAS_OKAY;

	case vtDouble:
		for(i = 0; i < pThis->ncomp; ++i)
			pValues[dirs[i]] = ((double*)(pThis->comp))[ i ];
		return DAS_OKAY;

	default: break;
	}
	
	return das_error(DASERR_VEC, "Invalid element type for vector %hhd", pThis->et);
}

/* Similar to the generic version but handle component remapping */
const char* das_geovec_compSym(const das_geovec* pThis, int iIndex)
{
	if((iIndex >= pThis->ncomp)||(iIndex < 0)){
		das_error(DASERR_VEC, "Vector does not have %d components", iIndex + 1);
		return NULL;
	}

	int iStdIdx = ((pThis->dirs >> 2*iIndex)&0x3);
	return das_geosys_symbol(pThis->systype, iStdIdx);
}