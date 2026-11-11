/* Copyright (C) 2018 Chris Piker <chris-piker@uiowa.edu>
 *
 * This file is part of das2C, the Core Das C Library.
 *
 * Das2C is free software; you can redistribute it and/or modify it under
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

#ifndef _das_dim_h_
#define _das_dim_h_

#include <das3/desc.h>
#include <das3/var.h>

#ifdef __cplusplus
extern "C" {
#endif

#define DASDIM_MAXDEP 16   // Arbitrary decision, can be changed
#define DASDIM_MAXVAR 16   // Another arbitrary changeable decision
#define DASDIM_NAXES 4     // can change later
#define DASDIM_AXIS_SZ 4     // Instead of single character so we can handle utf-8

	
/* OFFSET and REFERENCE variable roles were a tough call.  In the end you only
 * need center values to do DFT's.  The DFT code should look at the coordinate
 * series and see what if it has a constant change in index.  If so, you can do
 * a DFT, otherwise you can't.
 * 
 * Currently in das 2.2 land we know that one "package" of values is a
 * continuous waveform set.  We can look at the yTags, see if they are
 * consistent and the if they are then we can transform.  It's very clear when
 * we can do this, but it's also it requires a very specific data structure. 
 * So in a general data model, how do you get the yTags?  You *can* do it with a 
 * morphology check but those tend to be a rabbit hole of exploding if
 * statements.
 * 
 * The initial thought was to have offset dimensions, but that caused a massive
 * symmetry break in the data model.  Why have a rule that always combines two
 * dimensions with a + operator?  Why not other operators?  Also, if you set 
 * say the the "STD_DEV" variable in the reference dimension and also in the
 * offset dimension, how do you combine those?  It seems offset dimensions
 * trigger more problems then they solve.
 * 
 * The solution taken here is to introduce two variable roles, REFERENCE and
 * OFFSET.  Since this choice only requires adding two string constants it can
 * be ignored if turns out it's a bad choice.  Otherwise it simplifies the
 * concept of  breaking down values into a reference point that may change for
 * each packet and a set of fixed offsets.  These sorts of coordinates come up
 * a lot in our work, for example frequency values for down-mixed data, radar
 * return altitudes and waveform captures.
 * 
 * DASDIM_ROLE_CENTER should still be provided for client codes that don't understand
 * the offset and reference semantic.  And since this can be done with one 
 * operation set without exploding the network data volume 
 * it doesn't seem like that much of a burden.
 * 
 * Another approach would have been to expand properties to include an 
 * index/value relationship section.  We could define things like constant 
 * change of value for a change in a certain index and then define if rolling
 * the next index up is the same change as the lower index roll.  This seemed
 * like a much more complicated idea and didn't work so well with offsets that
 * are constant by record (i.e. the i value) but not constant by value index
 * (j,k,l ...).
 * -cwp
 */
	
DAS_API extern const char* DASDIM_ROLE_CENTER;
DAS_API extern const char* DASDIM_ROLE_MIN;
DAS_API extern const char* DASDIM_ROLE_MAX;
DAS_API extern const char* DASDIM_ROLE_WIDTH;
DAS_API extern const char* DASDIM_ROLE_MEAN;   /* all these can substitute for the center    */
DAS_API extern const char* DASDIM_ROLE_MEDIAN; /* if the center is missing but they are      */
DAS_API extern const char* DASDIM_ROLE_MODE;   /* distinct so it's good to include them here */
DAS_API extern const char* DASDIM_ROLE_REF; 
DAS_API extern const char* DASDIM_ROLE_OFFSET; 
DAS_API extern const char* DASDIM_ROLE_MAX_ERR;
DAS_API extern const char* DASDIM_ROLE_MIN_ERR;
DAS_API extern const char* DASDIM_ROLE_STD_DEV;
/* A binner that averages already-averaged data has to know how many 
   measurements are in each bin or sparse bin weigh as much as full ones. */
DAS_API extern const char* DASDIM_ROLE_COUNT;
DAS_API extern const char* DASDIM_ROLE_WEIGHT;

DAS_API extern const char* DASDIM_ROLE_NORM;

/* Withheld until the math exists.  point_spread has no formalism to
   interpret it, so naming it would promise handling das2C does not have:
   extern const char* DASDIM_ROLE_SPREAD;
 */


#define DASDIM_ROLE_SZ 32

typedef enum das_dimtype_e { DASDIMTYPE_UNK = 0, DASDIMTYPE_COORD, DASDIMTYPE_DATA } das_dimtype;

/** @addtogroup datamodel
 * @{
 */

/** @class DasDim dim.h <das3/dim.h>
 *
 * Das Physical Dimensions
 * 
 * Das dimensions are groups of variables within a single dataset that describe
 * the same physical thing.  For example the "Time" coordinate dimension would
 * groups all variables that locate data in time.  An "Ex" dimension would 
 * provide a group of variables describing the electric field in a spacecraft X
 * direction.  
 * 
 * A dimension needs to have at least one variable, but it may have many.
 * For example, a dataset that only provides time values by single points
 * would only have a single variable in the time dimension.  Hovever a dataset
 * providing extended duration events would need two time variables.  One time
 * variable could provide the event start times and another the end times.
 * 
 * There are two basic types of dimensions, coordinates and data.  Coordinate
 * dimensions provide variables to locate data in an independent parameter space,
 * these are typically the X-axis values (or X and Y for spectrograms).  Data
 * dimensions typically group together related measurements.
 * 
 * @extends DasDesc
 */
typedef struct das_dim_t DasDim;

/** @} */


/** Create a new dimension (not as impressive as it sounds)
 * 
 * @param sDim The id of the dimension, which should be a common name such as
 *         time, energy, frequency, latitude, longitude, solar_zenith_angle, 
 *         electric_spectral_density, netural_flux_density, etc.  It's much
 *         more important for coordinate dimensions to have common names than 
 *         data dimensions.
 * 
 * @param sName The name of this particular variable group in this dimension
 *        If NULL, defaults to sDim.  This matters because sometimes we may
 *        have multiple things that take data in the same coordinates.
 * 
 * @param dtype One of DASDIMTYPE_COORD, DASDIMTYPE_DATA
 * 
 * @param nRank
 * @memberof DasDim
 * @return 
 */
DAS_API DasDim* new_DasDim(const char* sDim, const char* sName, das_dimtype dtype, int nRank);


/** Get the dimension's category
 *
 * @param pThis a pointer to a das dimension structure.
 * 
 * @return The category of the dimension, which should be a common name
 *         physical thing such as time, energy, frequency, voltage, 
 *         electric_spectral_density, netural_flux_density, etc.  
 * 
 * @memberof DasDim
 */
DAS_API const char* DasDim_physDim(const DasDim* pThis);

/** Get the dimension's name
 *
 * This is the unique name of a coord or data element in das3 headers.  Where
 * DasDim_physDim() gives the physical category, the name tells apart the
 * dimensions of a dataset that share one, 'search_coil' and 'static_mag' for
 * example.  Lowercase is recommend in the manner of a database key.
 *
 * @param pThis a pointer to a das dimension structure.
 * 
 * @return The name of the dimension, which should be a particular name such
 *         as loc_iau_earth
 * 
 * @memberof DasDim
 */
DAS_API const char* DasDim_name(const DasDim* pThis);

/** Get this dimension's usage as coordinates or data 
 * 
 * @memberof DasDim
 */
DAS_API das_dimtype DasDim_type(const DasDim* pThis);

/** Get this dimensions's usage as coordinates or data as a string
 * 
 * @memberof DasDim
 */
DAS_API const char* DasDim_typeName(const DasDim* pThis);

/** Get the number of plot axes this dimension is associated with
 *
 * Axes are filled from index 0, so loop to this count with DasDim_axisSymAt().
 *
 * @returns The number of axes set, 0 if the dimension has no axis affinity
 * @memberof DasDim
 */
DAS_API int DasDim_numAxes(const DasDim* pThis);

/** Set a name for one of the axes associated with this coordinate dimension
 * 
 * Note: Axis associations are only permitted for coordinate dimensions
 * 
 * @param pThis The dimension
 * @param iAxis Which axis to set, from 0 up to DasDim_numAxes(), so axes
 *        can be replaced or added but not skipped
 * @param sAxis A single ut8 character, one of "x", "y", "z", "ρ", "r", "θ", "φ", or "t"
 * @returns DAS_OKAY, or an error code if iAxis would leave a gap or sAxis is
 *        empty
 * 
 * @memberof DasDim
 */
DAS_API das_errcode DasDim_setAxisSym(DasDim* pThis, int iAxis, const char* sAxis);

/** Get the axis affinity, if any.
 * 
 * @returns The axis name, or NULL if axis iAxis is not set or iAxis is not
 *          between 0 and DASDIM_NAXES - 1
 * @memberof DasDim
 */
DAS_API const char* DasDim_axisSymAt(const DasDim* pThis, int iAxis);

/** Set my AXES to match some other dimension,
 * Useful for stream filters
 * @memberof DasDim
 */
DAS_API void DasDim_setAxesFrom(DasDim* pThis, const DasDim* pOther);

/** Set primary coordinate status
 * 
 * @memberof DasDim
 */
DAS_API void DasDim_setPrimeCoord(DasDim* pThis, bool bPrimary);

/** Is this dimension a primary coordinate for most plotting purposes ?
 * @memberof DasDim
 */
DAS_API bool DasDim_isPrimeCoord(const DasDim* pThis);

/** Print a multi-line description of a dimension.
 *
 * A multi-line summary: the dimension, its properties, and one line per
 * variable.  Same calling convention as DasDs_info().
 *
 * @param pThis The dimension to describe.
 *
 * @param sBuf The output buffer, may be NULL if uLen is 0.
 *
 * @param uLen The size of sBuf in bytes, including room for the terminating
 *        NUL.  Output that does not fit is cut off, but sBuf is always NUL
 *        terminated when uLen > 0 and nothing past sBuf[uLen-1] is touched.
 *
 * @returns The length of the full description, not counting the NUL, as
 *        snprintf() does.  If this is uLen or more the output was cut off.
 *        To size a buffer, call once with (NULL, 0) and allocate the
 *        return value plus one.
 *
 * @memberof DasDim
 */
DAS_API int DasDim_info(const DasDim* pThis, char* sBuf, size_t uLen);

/** Print a one line summary of a dimension
 *
 * The first line of the _info() output, for example:
 *
 *    Data Dimension: B (magnetic_field) | axis: y
 *
 * Short enough to use inline as a printf %s argument.
 *
 * @param pThis The object to describe.
 * @param sBuf  The output buffer.
 * @param uLen  The size of sBuf, including room for the NUL.
 * @returns sBuf, or NULL if the whole line did not fit.  sBuf is always NUL
 *        terminated when uLen > 0.
 *
 * @memberof DasDim
 */
DAS_API char* DasDim_toStr(const DasDim* pThis, char* sBuf, size_t uLen);

/** Does the library have built-in handling for a given variable role?
 *
 * Role strings are free-form.  DasDim_addVar() accepts any non-empty name
 * under DASDIM_ROLE_SZ characters, and a dimension stores and returns
 * whatever it was given.  A role the library does not recognize is legal and
 * is preserved intact; it simply carries no meaning the library can act on.
 *
 * Use it when accepting a role from outside the library -- a command line, a
 * configuration file, a foreign format's metadata -- and the caller needs to
 * know whether das2C will do anything with it beyond storage.
 *
 * @param sPurpose The role name to check.  Comparison is case SENSITIVE and
 *                 exact; the DASVAR_* values are all lower case.
 *
 * @returns true if the role is one das2C knows, false for any other string.
 *
 * @warning A true answer here does not mean the role can be serialized.  The
 *          das-basic-stream schema restricts the `use` attribute to a closed
 *          list which is not identical to this one, so a role can be known to
 *          the C library and still fail stream validation.
 *
 * @memberof DasDim
 */
DAS_API bool DasDim_isKnownRole(const char* sPurpose);

/** Canonicalize a variable role arriving from outside
 *
 * Readers are liberal in what they accept and strict in what they emit, so a
 * role read off a stream passes through here before it is stored.
 *
 * This is a canonicalizer, not a validator.  Roles are free-form, so an
 * unrecognized name is returned exactly as given rather than refused. 
 * 
 * @see DasDim_isKnownRole() if you need to know whether das2C will act on it.
 *
 * @param sRole The role name as it arrived, or NULL
 *
 * @returns The canonical spelling.  This may be the caller's own pointer, so
 *          copy the result if it must outlive the input.  NULL in, NULL out.
 *
 * @memberof DasDim
 */
DAS_API const char* DasDim_roleFromStr(const char* sRole);

/** Add a variable to a dimension
 *
 * @param pThis the dimension in question
 * @param pVar the variable to add
 * @param sRole The type of information this variable supplies for the 
 *             dimension.  Any string may be used, standard values are
 *             provided in the defines: DASDIM_ROLE_CENTER, DASDIM_ROLE_MIN, DASDIM_ROLE_MAX,
 *             DASDIM_ROLE_WIDTH, DASDIM_ROLE_REF, DASDIM_ROLE_OFFSET, DASDIM_ROLE_MEAN, DASDIM_ROLE_MEDIAN,
 *             DASDIM_ROLE_MODE, DASDIM_ROLE_MAX_ERR, DASDIM_ROLE_MIN_ERR, DASDIM_ROLE_STD_DEV,
 *             DASDIM_ROLE_COUNT, DASDIM_ROLE_WEIGHT and DASDIM_ROLE_NORM.  Any non-empty
 *             string under DASDIM_ROLE_SZ characters is acceptable, using a
 *             single case is preferred; see DasDim_isKnownRole() for which
 *             names the library acts on rather than merely stores.
 * 
 * @returns true if the variable could be added, or false otherwise.  Trying
 *          to add a second variable for the same role will result in a return
 *          value of false.
 * 
 * @memberof DasDim
 */
DAS_API bool DasDim_addVar(DasDim* pThis, const char* sRole, DasVar* pVar);


/** Get a variable providing values for a particular role in the dimension
 * 
 * @param pThis A pointer to a dimension
 * 
 * @param sRole A string defining the role,  Any string may be used,
 *              standard values are provided in the defines: DASDIM_ROLE_CENTER,
 *              DASDIM_ROLE_MIN, DASDIM_ROLE_MAX, DASDIM_ROLE_WIDTH, DASDIM_ROLE_REF, DASDIM_ROLE_OFFSET, 
 *              DASDIM_ROLE_MEAN, DASDIM_ROLE_MEDIAN, DASDIM_ROLE_MODE, DASDIM_ROLE_MAX_ERR,
 *              DASDIM_ROLE_MIN_ERR, DASDIM_ROLE_STD_DEV, DASDIM_ROLE_COUNT, DASDIM_ROLE_WEIGHT
 *              and DASDIM_ROLE_NORM.
 * 
 * @returns A pointer to a DasVar or NULL if no variable exists within this 
 *         dimension for the given role.
 * @memberof DasDim
 */
DAS_API DasVar* DasDim_var(DasDim* pThis, const char* sRole);


/** Get the number of vars in a dimension 
 * 
 * @param pThis A pointer to a dimension
 * 
 * @returns The number of defined variables
 * @memberof DasDim
 */
DAS_API size_t DasDim_numVars(const DasDim* pThis);

/** Get a variable by index 
 * 
 * The range of valid indices is provided by DasDim_numVars();
 * 
 * @param pThis A pointer to a dimension
 * 
 * @param uIdx The index, from 0 to DasDim_numVars() - 1
 * 
 * @returns the variable at the given index or NULL if no variable is
 *        defined at that index
 * @memberof DasDim
 */
DAS_API DasVar* DasDim_varAt(const DasDim* pThis, size_t uIdx);

/** Get a variable's role by index
 * 
 * The range of valid indices is provided by DasDim_numVars();
 * 
 * @param pThis A pointer to a dimension
 * 
 * @param uIdx The index, from 0 to DasDim_numVars() - 1
 * 
 * @returns a constant pointer to the variable's role for a index or NULL
 *        if no variable is defined at that index
 * @memberof DasDim
 */
DAS_API const char* DasDim_roleAt(const DasDim* pThis, size_t uIdx);

 

/** Get a variable providing single point values in a dimension
 * 
 * The most common variable role, DASDIM_ROLE_CENTER, is typically present in a 
 * dimension but not always.  Sometimes other roles take this variable's 
 * place, such as the mean, median or mode or an average of the minimum and
 * maximum values.  Use this function to autoselect a variable to use as the
 * center point when plotting data.
 * 
 * @param pThis A pointer to a dimension
 * 
 * @return A pointer to a variable that can be used to provide single points
 *         in this dimension, or NULL in the rare instance that nothing in
 *         this dimension can be used for single point values.  A return of
 *         false from this call probably means you have an invalid or highly
 *         customized dataset.
 * 
 * @memberof DasDim
 */
DAS_API DasVar* DasDim_pointVar(DasDim* pThis);


/** Remove a variable by role from a dimensions
 * 
 * The caller is considered to own the variable and must delete it if no longer
 * in use.
 * 
 * @param pThis The dimension in question
 * 
 * @param role A role string.  Can be anything less that 32 characters but
 *        library uses are recommended to choose from the predefined strings:
 *        D2VP_CENTER, D2VP_MIN, D2VP_MAX, D2VP_WIDTH, D2VP_MEAN, D2VP_MEDIAN,
 *        D2VP_MODE, D2VP_REF, D2VP_OFFSET, D2VP_MAXERR, D2VP_MINERR, 
 *        D2VP_UNCERT, D2VP_STD_DEV
 * 
 * @return A pointer to the variable occupying the given role, or NULL if no
 *         variable occupied the specified role 
 * 
 * @memberof DasDim
 */
DAS_API DasVar* DasDim_popVar(DasDim* pThis, const char* role);


/** Get the maximum extent of this dimension in index space
 * 
 * This function can be used to determine if there is a set of indices that 
 * when changed, only affect the values of the variables in one dimension
 * without affecting the other.  If such an index set exists then the dimensions
 * are orthogonal in index space.
 * 
 * @code
 * // Given two dimension structure pointer, pLat and pLong
 * ptrdiff_t lat_shape[DASIDX_MAX];
 * ptrdiff_t long_shape[DASIDX_MAX];
 * 
 * DasDim_shape(pLat, lat_shape);
 * DasDim_shape(pLong, long_shape);
 * 
 * if(Shape_IsOrthoginal(lat_shape, long_shape)){
 *		// Yes, we can slice in latitude and longitude simply by changing the
 *		// indices lat_shape but not long_shape and vice versa
 * }
 * else{
 *		// No, we can't simply slice in index space to hold latitude or 
 *    // longitude constant.  Will need to define an index of a latitude or
 *		// longitude range.  I.e. will need to make a thin slab in
 * }
 * @endcode
 * 
 * This is a convenience wrapper around Variable_shape that takes the maximum
 * extent in all contained variables.
 * 
 * @param pThis the Coordinate Dimension or Data Dimension in question
 * 
 * @param pShape a pointer to an array up to DASIDX_MAX in size to receive the
 *        shape values.  A -1 in any position means that the index in question
 *        does not affect any of variables within this dimension.
 * 
 * @return The rank of the variable
 * 
 * @see Variable_shape()
 * 
 * @memberof DasDim
 */
DAS_API int DasDim_shape(const DasDim* pThis, ptrdiff_t* pShape);


/** Return the current max value index value + 1 for any partial index
 * 
 * This is a more general version of DasDim_shape that works for both cubic
 * arrays and with ragged dimensions, or sequence values.
 * 
 * @param pThis A pointer to a DasDim structure
 * @param nIdx The number of location indices which may be less than the 
 *             number needed to specify an exact value.
 * @param pAddr A list of values for the previous indexes, must be a value 
 *             greater than or equal to 0
 * @return The number of sub-elements at this index location or D2IDX_UNUSED
 *         if this variable doesn't depend on a given location, or DASIDX_UNUSED
 *         if this variable returns computed results for this location
 * 
 * @see DasAry_lengthIn
 * 
 * @memberof DasDim
 */
DAS_API ptrdiff_t DasDim_lengthIn(const DasDim* pThis, int nIdx, ptrdiff_t* pAddr);


/** Does a given extern index even matter to data in this physical dimension?
 * 
 * @param pThis A pointer to a DasDim structure
 * 
 * @param iIndex The index in question, from 0 to DASIDX_MAX - 1
 * 
 * @return true if varying this index could cause any variable's output
 *         to change, false if it would have no effect.
 * 
 * @memberof DasDim
 */
DAS_API bool DasDim_isDegenerate(const DasDim* pThis, int iIndex);


#ifdef __cplusplus
}
#endif

#endif /* _das_dim_h_ */

