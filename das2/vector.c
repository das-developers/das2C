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

#include "vector.h"
#include "log.h"

/* For the builtin systems we have default names.  Otherwise just return 
   the direction index. */
static const char* g_nUserDirs[] = {"0", "1", "2", "\0"};
static const char* g_nBuiltDirs[][4] = {
	
	/* ALL of these are listed in a RIGHT HANDED order ! */

	{  "",   "",   "", ""}, /* 0x00000000        */
	{ "x",  "y",  "z", ""}, /* DAS_VSYS_CART    */
	{ "ρ",  "φ",  "z", ""}, /* DAS_VSYS_CYL     */
	{ "r",  "θ",  "φ", ""}, /* DAS_VSYS_SPH     */
	{ "r",  "φ",  "θ", ""}, /* DAS_VSYS_CENTRIC */
	{ "φ",  "θ",  "a", ""}, /* DAS_VSYS_DETIC   */
	{ "φ",  "θ",  "a", ""}  /* DAS_VSYS_GRAPHIC */
};

const char* das_compsys_str( ubyte uFT)
{
   ubyte ft = uFT & DAS_VSYS_TYPE_MASK;

   if(ft == DAS_VSYS_CART    ) return "cartesian";
   if(ft == DAS_VSYS_CYL     ) return "cylindrical";
   if(ft == DAS_VSYS_SPH     ) return "spherical";
   if(ft == DAS_VSYS_CENTRIC ) return "centric";
   if(ft == DAS_VSYS_DETIC   ) return "detic";
   if(ft == DAS_VSYS_GRAPHIC ) return "graphic";

   daslog_error_v("Unknown vector or coordinate frame type id: '%hhu'.", uFT);
   return NULL;   
}

ubyte das_compsys_id(const char* sFT)
{
   if( strcasecmp(sFT, "cartesian") == 0)      return DAS_VSYS_CART   ;
   if( strcasecmp(sFT, "cylindrical") == 0)    return DAS_VSYS_CYL    ;
   if( strcasecmp(sFT, "spherical") == 0)      return DAS_VSYS_SPH    ;
   if( strcasecmp(sFT, "centric") == 0)        return DAS_VSYS_CENTRIC;
   if( strcasecmp(sFT, "detic") == 0)          return DAS_VSYS_DETIC  ;
   if( strcasecmp(sFT, "graphic") == 0)        return DAS_VSYS_GRAPHIC;

   daslog_error_v("Unknown vector or coordinate frame type: '%s'.", sFT);
   return 0;
}

/* Developers note:
 *   
 * Coordinate systems can be very confusing.  The componet names below
 * use X,Y,Z to represent the principle axes of a coordiante system.  Almost
 * all data vectors are defined using these.  However a data system may
 * be defined in a non-inertal frame, an the principle axes of those non-
 * inertial frames may happend correspond to angel directions!
 *
 * (By non-inertial, I mean a constant value may have different components
 *  at different times because the coordinate system rotated )
 *
 * So for a vector streamed with the following coordinate order:
 *
 *   X,Y,Z 
 *
 * For data defined in a frame:
 *
 *    GEOSPHERE
 *
 * Such that the 1st principle direction is radial out (aka Up), the 
 * second direction is in increasing southward and the last increases
 * eastward we might reasonable have principle component tokens from
 * the frame of:
 *
 *    R, θ, φ
 *
 * Which make it look like these are spherical coordiantes, but they
 * are not!
 *
 * Basically the spherical systems only show up when defining location
 * vectors.  Aka, latitude, longitude and altitude and the like.
 */
const char* das_compsys_desc(ubyte uST)
{
	switch(uST & 0xF){
	case DAS_VSYS_CART:
	return "A standard orthoginal coordiante system. The full component set "
	       "is (x,y,z). Missing components are assumed to be 0.";
	case DAS_VSYS_CYL:
	return "An ISO 31-11 standard cylindrical system. The full componet set "
	       "is (ρ,φ,z) where ρ is distance to the z-axis, φ is eastward "
	       "angle.  Z is assumed to be 0 if missing, ρ assumed to be 1 "
	       "if missing.";
	case DAS_VSYS_SPH:
	return "An ISO 31-11 standard spherical system. The full component set "
	       "is (r,θ,φ) where r is the radial diretion, θ is the colatitude "
	       "(which is 0° at the north pole) and φ is the eastward angle. "
	       "Both θ, φ  are assumed to be 0° if missing and r is assumed to "
	       "be 1 if missing.";

	case DAS_VSYS_CENTRIC:
	return "A spherical system.  The full component set is (r, φ, θ) where "
	       "'r' is the radial direction, 'φ'' is the eastward direction and "
	       "'θ' is positive towards the pole.  Both 'θ' and 'φ' are assumed "
	       "to be 0° if missing and 'r' is assumed to be 1 if not specified.";

	case DAS_VSYS_DETIC:
	return "An ellipsoidal coordinate system defined with respect to a "
	       "reference surface. Normals from the surface do not intersect"
	       "the origin except at the equator and poles.  The full "
	       "component set is (φ, θ, a) where 'φ' is the eastward angle of a"
	       "point on the reference ellipsoid, 'θ' is the latitude and 'a' "
	       "is the distance outside the ellipsoid along a surface normal. "
	       "All of 'a', 'θ' and 'φ' are assumed to be 0 if absent.";

	case DAS_VSYS_GRAPHIC:
	return "An ellipsoidal coordinate system defined with respect to a "
	       "reference surface. Normals from the surface do not intersect"
	       "the origin except at the equator and poles.  The full "
	       "component set is (a, φ, θ) where 'φ' is the WESTWARD angle of a"
	       "point on the reference ellipsoid, 'θ' is the latitude and 'a' "
	       "is the distance outside the ellipsoid along a surface normal. "
	       "All of 'a', 'θ' and 'φ' are assumed to be 0 if absent.";
	}
	return "";
}

const char* das_compsys_symbol(ubyte systype, int iIndex)
{
	int nSys = systype & 0xF;
	if(nSys > DAS_VSYS_MAX)
		return g_nUserDirs[ iIndex < 3 ? iIndex : 3];

	return g_nBuiltDirs[nSys][iIndex < 3 ? iIndex : 3];
}

int8_t das_compsys_index(ubyte systype, const char* sSymbol)
{
	for(int8_t i = 0; i < 3; ++i){
		if(strcasecmp(sSymbol, das_compsys_symbol(systype, i)) == 0)
			return i;
	}
	return -1;
}


DasErrCode das_geovec_init(
	das_geovec* pVec, const ubyte* pData, ubyte frame, ubyte surfid, 
	ubyte systype, ubyte et, ubyte esize,  ubyte ncomp, ubyte dirs
){

	pVec->frame = frame;
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

DAS_API int das_geovec_dir(const das_geovec* pThis, int i)
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
		dirs[i] = (pThis->dirs << i*2)&0x3;	

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

DasErrCode DasFrame_setSys(das_geovec* pThis, const char* sSystem)
{
   if((sSystem == NULL)||(sSystem[0] == '\0'))
      return das_error(DASERR_VEC, "Empty coordinate frame system");

   pThis->systype = das_compsys_id(sSystem);
   if(pThis->systype == 0)
   	return das_error(DASERR_VEC, "Coordinate system type '%s' is unknown");
   
   return DAS_OKAY;
}

/* Similar to the generic version but handle component remapping */
const char* das_geovec_compSym(const das_geovec* pThis, int iIndex)
{
	if((iIndex >= pThis->ncomp)||(iIndex < 0)){
		das_error(DASERR_VEC, "Vector does not have %d components", iIndex + 1);
		return NULL;
	}

	int iStdIdx = ((pThis->dirs >> 2*iIndex)&0x3);
	return das_compsys_symbol(pThis->systype, iStdIdx);
}