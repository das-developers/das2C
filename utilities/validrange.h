/* Copyright (C) 2026 Chris Piker <chris-piker@uiowa.edu>
 *
 * This file is part of das2C, the Core Das C Library.
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

/** @file validrange.h  The valid data range of a das2 plane, shared by the
 * das2 reducers.
 *
 * The library gives no meaning to the valid range properties, so reading
 * them is left to the programs that act on them.
 */

#ifndef _das_tools_validrange_h_
#define _das_tools_validrange_h_

#include <float.h>
#include <stdbool.h>
#include <das3/core.h>

/* One bound in the units of the plane's data.  A property without units is
   taken to be in the data units already. */
static bool _das2_validBound(
	const DasPlane* pPlane, const char* sProp, double* pBound
){
	const DasProp* pProp = DasDesc_prop((const DasDesc*)pPlane, sProp);
	if(pProp == NULL) return false;

	double rVal = 0.0;
	if(DasProp_toDoubles(pProp, &rVal, 1) != 1){
		daslog_warnf("Ignoring %s, '%s' is not a number", sProp, DasProp_value(pProp));
		return false;
	}

	DasUnits have = DasProp_units(pProp);
	DasUnits want = DasPlane_units(pPlane);
	if((have != NULL) && (have != DASUNITS_DIMENSIONLESS) && (have != want)){
		if(!DasUnits_canConvert(have, want)){
			daslog_warnf("Ignoring %s, its units '%s' do not convert to the "
				"data units '%s'", sProp, have, want
			);
			return false;
		}
		rVal = DasUnits_convert(rVal, have, want);
	}
	*pBound = rVal;
	return true;
}

/** Get the range outside of which a plane's values are not data
 *
 * Reads yValidMin and yValidMax for a <y> plane, zValidMin and zValidMax
 * for <yscan> and <z> planes, from the plane, its packet or the stream.
 *
 * @param pPlane  The plane whose values will be tested.
 * @param pMin    Receives the smallest valid value, -DBL_MAX if not given.
 * @param pMax    Receives the largest valid value, DBL_MAX if not given.
 * @return true if the plane has at least one bound.
 */
static bool das2_validRange(const DasPlane* pPlane, double* pMin, double* pMax)
{
	*pMin = -DBL_MAX;
	*pMax =  DBL_MAX;

	bool bHave = false;
	switch(DasPlane_type(pPlane)){
	case DASPLANETYPE_Y:
		bHave  = _das2_validBound(pPlane, "yValidMin", pMin);
		bHave |= _das2_validBound(pPlane, "yValidMax", pMax);
		break;
	case DASPLANETYPE_YSCAN:
	case DASPLANETYPE_Z:
		bHave  = _das2_validBound(pPlane, "zValidMin", pMin);
		bHave |= _das2_validBound(pPlane, "zValidMax", pMax);
		break;
	default:
		break;
	}
	return bHave;
}

/* Both bounds are valid values.  NaN fails. */
#define das2_inRange(V, MIN, MAX) (((V) >= (MIN)) && ((V) <= (MAX)))

#endif /* _das_tools_validrange_h_ */
