/** @file units.h Defines units used for items in the stream, most notably
 * time units that reference an epoch and a step size.
 */

#ifndef _das2_units_h_
#define _das2_units_h_

#include <stdbool.h>
#include <das2/das1.h>

#ifndef _das2_units_c_

extern const char* UNIT_US2000; /* microseconds since midnight, Jan 1, 2000 */
extern const char* UNIT_MJ1958; /* days since midnight, Jan 1, 1958 */
extern const char* UNIT_T2000; /* seconds since midnight, Jan 1, 2000 */
extern const char* UNIT_T1970; /* seconds since midnight, Jan 1, 1970 */
extern const char* UNIT_UTC;   /* Time strings on the Gregorian Calendar */

/* Other common units */
extern const char* UNIT_SECONDS;
extern const char* UNIT_HOURS;
extern const char* UNIT_DAYS;
extern const char* UNIT_MILLISECONDS;
extern const char* UNIT_MICROSECONDS;

extern const char* UNIT_HERTZ;
extern const char* UNIT_KILO_HERTZ;
extern const char* UNIT_MEGA_HERTZ;
extern const char* UNIT_E_SPECDENS;
extern const char* UNIT_B_SPECDENS;
extern const char* UNIT_NT;

extern const char* UNIT_NUMBER_DENS;
		
extern const char* UNIT_DB;

extern const char* UNIT_KM;

extern const char* UNIT_DEGREES;
extern const char* UNIT_DIMENSIONLESS;

/* color:  Color should be handled as as vector, we don't have
 * support for vectors at this time.   Also a datatype of 
 * byte is needed for small values
 */
/* extern const char* UNIT_RGB; */


#endif

/** @defgroup units Units */

/** @addtogroup units 
 * @{
 */

/** Enumeration of unit types, that correspond to physical unit types.  
 *
 * Note that although these are strings, Units_fromStr() should be be
 * used to get a reference to the enumerated string since *pointer equality*
 * comparison is done in the code.  Thus UnitType objects created using the
 * functions in this module satisfy the rule:
 *
 * @code
 *   UnitType a;
 *   UnitType b;
 *
 *   if(a == b){
 *     // Units are equal
 *   }
 * @endcode
 *
 * The Epoch Time unit types understood by this library are:
 *   - UNIT_US2000 - Microseconds since midnight, January 1st 2000
 *   - UNIT_MJ1958 - Days since midnight January 1st 1958 
 *   - UNIT_T2000  - Seconds since midnight, January 1st 2000
 *   - UNIT_T1970  - Seconds since midnight, January 1st 1970
 *   - UNIT_UTC    - Time strings on the gregorian calendar
 *
 * As it stands the library currently does not understand SI prefixes, so
 * each scaled unit has it's own entry.  This should change.
 *   - UNIT_SECONDS - Seconds, a time span.
 *   - UNIT_HOURS - hours, a time span = 3600 seconds.
 *   - UNIT_MIRCOSECONDS - A smaller time span.
 *   - UNIT_HERTZ   - Hertz, a measure of frequency.
 *   - UNIT_KILO_HERTZ - KiloHertz, another measure of frequency.
 *   - UNIT_E_SPECDENS - Electric Spectral Density, V**2 m**-2 Hz**-1;
 *   - UNIT_B_SPECDENS - Magnetic Spectral Density, nT**2 Hz**-1;
 *   - UNIT_NT      - Magnetic Field intensity, nT
 *   - UNIT_NUMBER_DENS - Number density, the number of items in a cubic 
		 centimeter
 *   - UNIT_DB      - Decibels, a ratio measure, typically versus 1.0.
 *   - UNIT_KM      - Kilometers, a unit of distance
 *   - UNIT_DEGREES - Degrees, a ratio measure on circles: (arch length / circumference) * 360
 *
 * And if you don't know what else to use, try this:
 *
 *   - UNIT_DIMENSIONLESS - I.E. No units
 * 
 * @todo Redo units as small structures
 */
typedef const char* UnitType;

/** Canonical fill value */
#define FILL_VALUE -1e31


/** Basic constructor for UnitType's
 *
 * UnitType values are just char pointers, however they as singletons so
 * that equality operations are possible.  For proper operation of the
 * functions in the module it is assumed that one of the pre-defined 
 * units are use, or that new unit types are created via this function.
 *
 * @returns a pointer to the singleton string representing these units.
 */
UnitType Units_fromStr(const char* string);


/** Get the canonical string representation of the UnitType
 * Even though UnitType is char*, this function should be used in case the
 * UnitType implementation is changed in the future.
 * @see Units_toLabel()
 */
const char* Units_toStr(UnitType unit);


/** Get label string representation of the UnitType
 * 
 * This function inserts formatting characters into the unit string returned
 * by Units_toStr().  The resulting output is suitable for use in Das2 labels
 * For example if Units_toStr() returns:
 * 
 *    V**2 m**-2 Hz**-1
 * 
 * this function would generate the string
 * 
 *    V!a2!n m!a-2!n Hz!a-1!n
 * 
 * Units that are an offset from some UTC time merely return "UTC"
 * 
 * @param unit
 * @param sBuf a buffer to hold the UTF-8 label string
 * @return a pointer to sBuf, or NULL if nLen was too short to hold the label,
 *         or if the name contains a trailing '_' or there was more than one
 *         '_' characters in a unit name.
 */
char* Units_toLabel(UnitType unit, char* sBuf, int nLen);


/** Invert the units, most commonly used for Fourier transform results
 *
 * Create the corresponding inverted unit from a given unit.  For example
 * seconds become Hz, milliseconds become kHz and so on.  This function does
 * <b>not</b> product the same output as calling:
 * @code
 *   
 *   Units_exponentiate(unit, -1, 1);
 * 
 * @endcode
 * 
 * because a special lookup table is used for converting s**-1 (and related)
 * values to Hertz.
 * 
 * For all other unit types, calling this function is equivalent to calling
 * Units_exponentiate(unit, -1, 1)
 *
 * @Warning This function is not multi-thread safe.  It alters global 
 *       library state data
 *
 * @param unit the input unit to invert
 *
 * @returns the inverted unit
 */
UnitType Units_invert(UnitType unit);


/** Combine units via multiplication
 * 
 * Examples:
 *   A, B  ->  A B
 * 
 *   A, A  ->  A**2
 * 
 *   kg m**2 s**-1, kg**-1  ->  m**2 s**-1 
 * 
 * @param unit
 * @return A new unit type which is the product of a and b.
 */
UnitType Units_multiply(UnitType ut1, UnitType ut2);

/** Combine units via division
 * 
 * This is just a convenience routine that has the effect of calling:
 * 
 * @code
 *   Units_multiply( a, Units_power(b, -1) );
 * @endcode
 * 
 * @param unit
 * @return A new unit type which is the quotient of a divided by b
 */
UnitType Units_divide(UnitType a, UnitType b);


/** Raise units to a power
 *
 * To invert a unit use the power -1.  
 * 
 * For units following the canonical pattern:
 *
 *   A**n B**m
 *
 * A new inverted unit:
 *
 *   A**-n B**-m
 * 
 * is produced.
 */
UnitType Units_power(UnitType unit, int power);


/** Reduce units to a root
 * 
 * Use this to reduce units to a integer root, for example:
 * 
 *  Units_root( "m**2", 2 ) --> "m"
 *  Units_root( "nT / cm**2" ) --> "nT**1/2 cm**-1"
 * 
 * @param unit The input unit
 * @param root A positive integer greater than 0
 * 
 * @returns the new unit.
 */
UnitType Units_root(UnitType unit, int root );


/** Get the unit type for intervals between data points of a given unit type.
 * 
 * This is confusing, but basically some data points, such as calendar times
 * and various other Das epoch based values cannot represent differences, only
 * absolute positions.  Use this to get the unit type of the subtraction of 
 * two point specified as the given time.
 * 
 * For example the units of 2017-10-14 UTC - 2017-10-13 UTC is seconds.
 * 
 * @param unit The unit type for which the difference type is desired
 * 
 * @returns the interval unit type.  Basic units such as meters have no
 *       standard epoch and thus they are just their own interval type.
 */
UnitType Units_interval(UnitType unit);


/** Reduce arbitrary units to the most basic know representation
 * 
 * Units such as days can be represented as 86400 seconds, likewise units such
 * as km**2 can be represented as 10e6 m**2.  Use this function to reduce units
 * to the most basic type known by this library and return the scaling factor
 * that would be needed to convert data in the given units to the reduced units.
 * 
 * This handles all SI units (except candela) and allows for metric
 * prefix names on arbitrary items, but not metric prefix symbols on 
 * arbitrary unit tyes.  For example 'microcows' are reduced to '1e-6 cows',
 * but 'μcows' are not converted to 'cows'.
 * 
 * @param[in] orig the original unit type
 * 
 * @param[out] pFactor a pointer to a double which will hold the scaling factor, 
 *             for units that are already in the most basic form this factor 
 *             is 1.0.
 * @returns    the new UnitType, which may just be the old unit type if the
 *             given units are already in their most basic form
 */
UnitType Units_reduce(UnitType orig, double* pFactor);

/** Determine if given units are interchangeable
 * Though not a good a solution as using UDUNITS2 works for common space physics
 * quantities as well as SI units.  Units are convertible if:
 * 
 *   1. They are both known time offset units.
 *   2. They have a built in conversion factor (ex: 1 day = 24 hours)
 *   3. Both unit sets use SI units, including Hz
 * 
 */
bool Units_canConvert(UnitType fromUnits , UnitType toUnits);


/** Conversion utility used for time unit conversion.
 * 
 * @param value The value to convert, to get a conversion factor from one unit
 *              type to another set this to 1.0.
 *
 * @note: Thanks Wikipedia.  This code incorporates the algorithm on page
 *        http://en.wikipedia.org/wiki/Julian_day used here under the
 *        GNU Public License.
 */
double Units_convertTo( UnitType toUnits, double rVal, UnitType fromUnits );


/** Determine if the units in question can be converted to date-times 
 *
 * If this function returns true, then the following functions may be
 * used on this unit type:
 *
 *  Units_convertToDt()
 *  Units_convertFromDt()
 *  Units_secondsSinceMidnight()
 *  Units_getJulianDay()
 *
 * Furthermore a call to Units_interval() returns a different unittype then
 * the given units.
 */
bool Units_haveCalRep(UnitType unit);


/** Convert a value in time offset units to a calendar representation
 * 
 * @param[in] value the double value representing time from the epoch in some
 *            scale
 * @param[in] units Unit string
 * @param[out] pDt a pointer to a das_time structure to receive the broken 
 *            down time.
 */
void Units_convertToDt(das_time_t* pDt, double value, UnitType epoch_units);

/** Convert a calendar representation of a time to value in time offset units
 * 
 * @param epoch_units The units associated with the return value
 * @param pDt the calendar time object from which to derive the value
 * @return the value as a floating point offset from the epoch associated with 
 *         epoch_units, or FILL_VALUE on an error
 */
double Units_convertFromDt(UnitType epoch_units, const das_time_t* pDt);

/** Get seconds since midnight for some value of an epoch time unit
 * @param rVal the value of the epoch time
 * @param units so type of epoch time unit.
 * @returns the number of floating point second since midnight
 */
double Units_secondsSinceMidnight( double rVal, UnitType epoch_units );


/* Get the Julian day for the Datum (double,unit) */
int Units_getJulianDay( double timeDouble, UnitType epoch_units );

/** @} */

#endif /* _das2_units_h_ */
