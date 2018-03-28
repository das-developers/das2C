#define _POSIX_C_SOURCE 200112L

/*---------------------------------------------------------------------------

  ttime.c adapted by L. Granroth  1997-08-29
  from dtime.c written by L. Granroth  1997-03-03
  converts time components to a double precision floating point value
  (seconds since the beginning of 1958, ignoring leap seconds) and normalize
  inputs.  Note that this floating point value should only be used for
  "internal" purposes.  (There's no need to propagate yet another time
  system, plus I want to be able to change/fix these values.)
  
  There is no accomodation for calendar adjustments, for example the
  transition from Julian to Gregorian calendar, so I wouldn't recommend
  using these routines for times prior to the 1800's.  Sun IEEE 64-bit
  floating point preserves millisecond accuracy past the year 3000.
  For various applications, it may be wise to round to nearest millisecond
  (or microsecond, etc.) after the value is returned.

  Arguments (will be normalized if necessary):
    int *year	year (1900 will be added to two-digit values)
    int *month	month of year (1-12)
    int *mday	day of month (1-31)
    int *yday	day of year (1-366) input value ignored
    int *hour	hour of day (0-23)
    int *minute	minute of hour (0-59)
    double *second second of minute (0.0 <= s < 60.0), leapseconds ignored

  Note:  To use day of year as input, simple specify 1 for the month and
  the day of year in place of day of month.  Beware of the normalization.

  1999-01-26 Removed sunmath aint requirement

  ---------------------------------------------------------------------------*/

#include <math.h>

/* forward references, constants */
#include "das1.h"

/* offset days at beginning of month indexed by leap year and month */
static int days[2][14] = {
  { 0, 0, 31, 59, 90, 120, 151, 181, 212, 243, 273, 304, 334, 365 },
  { 0, 0, 31, 60, 91, 121, 152, 182, 213, 244, 274, 305, 335, 366 } };

#define LEAP(y) ((y) % 4 ? 0 : ((y) % 100 ? 1 : ((y) % 400 ? 0 : 1)))

#define AINT(x) ((x) < 0.0 ? ceil(x) : floor(x) )

double ttime (
	int *year, int *month, int *mday, int *yday, int *hour, int *minute, 
	double *second
){

  double s, sjd;

  /* There is no error checking for time range. */

  /* normalize the input values */
  tnorm (year, month, mday, yday, hour, minute, second);

  /* Use the difference of Julian Days from the arbitrary epoch */
  sjd = (double)(jday(*year, *month, *mday) - EPOCH) * 86400.0;

  s = *second + (double)(*minute) * 60.0 + (double)(*hour) * 3600.0 + sjd;

  return s;

} /* ttime - convert time to double seconds */


double dt_ttime(const das_time_t* dt){
	
	das_time_t _dt;
	dt_copy(&_dt, dt);
	
	return ttime(&(_dt.year), &(_dt.month), &(_dt.mday), &(_dt.yday),
			       &(_dt.hour), &(_dt.minute), &(_dt.second) );
}

/* ************************************************************************* */

void emitt (
	double dt, int *year, int *month, int *mday, int *yday, int *hour,
	int *minute, double *second
){
  int id, jd; /* iy; */

  *second  = fmod (dt, 60.0);
  dt /= 60.0; dt = AINT(dt);
  *minute  = (int) fmod (dt, 60.0);
  dt /= 60.0; dt = AINT(dt);
  *hour    = (int) fmod (dt, 24.0);
  dt /= 24.0; dt = AINT(dt);

  /* days since the beginning of 1958 */
  id = (int)dt + EPOCH - 2436205;

  /* approximate year */
  *year = id / 365 + 1958;

  jd = jday (*year, 1, 1) - 2436205;

  *month = 1;
  *mday = id - jd + 1;
  tnorm (year, month, mday, yday, hour, minute, second);

} /* emitt - convert double clock to components */

void dt_emitt (double tt, das_time_t* dt){
	emitt(tt, &(dt->year), &(dt->month), &(dt->mday), &(dt->yday),
			       &(dt->hour), &(dt->minute), &(dt->second) );
}

/*---------------------------------------------------------------------------
 
  tnorm -- normalize date and time components for the Gregorian calendar
  ignoring leap seconds

  (This is the most likely bug nest.)

  ---------------------------------------------------------------------------*/
   
void
tnorm (int *year, int *month, int *mday, int *yday,
       int *hour, int *minute, double *second)
{
  int leap, ndays;

  /* add 1900 to two-digit years (and really mess-up negative years) */
  if (*year < 100) *year += 1900;

  /* month is required input -- first adjust month */
  if (*month > 12 || *month < 1) {
    /* temporarily make month zero-based */
    (*month)--;
    *year += *month / 12;
    *month %= 12;
    if (*month < 0) {
      *month += 12;
      (*year)--;
    }
    (*month)++;
  }

  /* index for leap year */
  leap = LEAP(*year);

  /* day of year is output only -- calculate it */
  *yday = days[leap][*month] + *mday;

  /* now adjust other items . . . */

  /* again, we're ignoring leap seconds */
  if (*second >= 60.0 || *second < 0.0) {
    *minute += (int)(*second / 60.0);
    *second = fmod (*second, 60.0);
    if (*second < 0.0) {
      *second += 60.0;
      (*minute)--;
    }
  }

  if (*minute >= 60 || *minute < 0) {
    *hour += *minute / 60;
    *minute %= 60;
    if (*minute < 0) {
      *minute += 60;
      (*hour)--;
    }
  }

  if (*hour >= 24 || *hour < 0) {
    *yday += *hour / 24;
    *hour %= 24;
    if (*hour < 0) {
      *hour += 24;
      (*yday)--;
    }
  }

  /* final adjustments for year and day of year */
  ndays = leap ? 366 : 365;
  if (*yday > ndays || *yday < 1) {
    while (*yday > ndays) {
      (*year)++;
      *yday -= ndays;
      leap = LEAP(*year);
      ndays = leap ? 366 : 365;
    }
    while (*yday < 1) {
      (*year)--;
      leap = LEAP(*year);
      ndays = leap ? 366 : 365;
      *yday += ndays;
    }
  }

  /* and finally convert day of year back to month and day */
  while (*yday <= days[leap][*month]) (*month)--;
  while (*yday >  days[leap][*month + 1]) (*month)++;
  *mday = *yday - days[leap][*month];

} /* tnorm - normalize date and time components */


void dt_tnorm(das_time_t* dt){
	tnorm(&(dt->year), &(dt->month), &(dt->mday), &(dt->yday), &(dt->hour), 
			&(dt->minute), &(dt->second) );
}


/*---------------------------------------------------------------------------

  jday -- calculate Julian Day number given Year, Month, Day

  This was adapted from JHU IDL procedure ymd2jd.pro and
  should be accurate for years after adoption of the Gregorian calendar.

  ints are assumed to be 32 or more bits

  ---------------------------------------------------------------------------*/

int
jday (int year, int month, int day)
{
  return 367 * year - 7 * (year + (month + 9) / 12) / 4 -
         3 * ((year + (month - 9) / 7) / 100 + 1) / 4 +
         275 * month / 9 + day + 1721029;

} /* jday - calculate Julian Day number given Year, Month, Day */
