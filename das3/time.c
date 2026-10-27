/* Copyright (C) 2015-2026 Chris Piker    <chris-piker@uiowa.edu>
 * Copyright (C) 1993-1998 Larry Granroth <larry-granroth@uiowa.edu>
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

/* ----------------------------------------------------------------------

  parsetime.c written by L. Granroth  1993-04-15
  to parse typical ascii date/time strings and return
  year, month, day of month, day of year, hour, minute, second.

  Updated for European systems by C. Piker 2022-09-30
  --------------------------------------------------------------------- */

#include <ctype.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <string.h>
#include <time.h>

#ifdef _WIN32
#include <winsock2.h>
#include <windows.h>
#else
#include <sys/time.h>
#endif
#include <locale.h>

#include "das1.h"
#include "time.h"
#include "util.h"
#include "tt2000.h"

#ifdef _WIN32
#pragma warning(disable : 4706)
#endif

/* Removed ',' as a delim since it's a common value for localeconv()->decimal_point */
#define DELIMITERS " \t/-:_;\r\n"
#define PDSDELIMITERS " \t/-T:_;\r\n"

#define DATE 0
#define YEAR 1
#define MONTH 2
#define DAY 3
#define HOUR 4
#define MINUTE 5
#define SECOND 6


/* ************************************************************************* */

static const char* months[] = {
	"january", "february", "march", "april", "may", "june",
	"july", "august", "september", "october", "november", "december"
};

static const int day_offset[2][14] = {
  {  0,   0,  31,  59,  90, 120, 151, 181, 212, 243, 273, 304, 334, 365},
  {  0,   0,  31,  60,  91, 121, 152, 182, 213, 244, 274, 305, 335, 366} 
};

static const int days_in_month[2][14] = {
  { 0, 31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31, 0},
  { 0, 31, 29, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31, 0} 
};


/* ************************************************************************* */
void dt_null(das_time* pDt)
{
	pDt->year = 0; pDt->month = 0; pDt->mday = 0; pDt->yday = 0;
	pDt->yday = 0; pDt->hour = 0; pDt->minute = 0; pDt->second = 0.0;
}
  
/* ************************************************************************* */
  
int parsetime (
   const char *string, int *year, int *month, int *day_month, int *day_year,
   int *hour, int *minute, double *second
){
  char s[80];
  char *c;
  char *delimiters;
  char *end_of_date;
  time_t curtime;
  struct tm *curtm;
  int i, j, len, n;
  char *tok[10];
  int want[7] = {0};
  char *ptr;
  int number;
  double value;
  int hold;
  int leap;

  (void)strncpy (s, string, 80);

  /* Handle fractional seconds specified via a decimal point or common even
     if the current locale has a different single character for the radix.
     WILL FAIL in languages that have utf-8 multibyte sequences for the radix! 
  */
  struct lconv* pLocale = localeconv();
  char cLocalPt = *(pLocale->decimal_point);
  char* pPt;
  if(cLocalPt == '.'){
    pPt = strchr(s, ',');  // If found European Radix
    if(pPt != NULL)
      *pPt = cLocalPt;
  }
  else if(cLocalPt == ','){
    pPt = strchr(s, '.');  // If found U.S. Radix
    if(pPt != NULL)
      *pPt = cLocalPt;
  }
  /* strtod should be able to take it from here... */
  
  /* handle PDS time format */

  delimiters = DELIMITERS;
  if ((c = strchr (s, 'Z'))) *c = (char)0;
  end_of_date = strchr (s, 'T');
  if (end_of_date) {
    c = end_of_date - 1;
    if (isdigit ((int)(*c))) delimiters = PDSDELIMITERS;
    else end_of_date = (char *)0;
  }

  /* if not PDS then count out 3 non-space delimiters */

  if (!end_of_date) {
    n = 0;
    len = strlen (s);
    for (i = 0; i < len; i++) {
      if ((c = strchr (delimiters+2, (int)s[i]))) n++;
      if (n == 3) {
        end_of_date = s + i;
        break;
      }
    }
  }

  /* default to current year */

  if (time (&curtime) == (time_t)(-1)) return -1;
  if (!(curtm = localtime (&curtime))) return -1;
  *year = curtm->tm_year + 1900;
  *month = 0;
  *day_month = 0;
  *day_year = 0;
  *hour = 0;
  *minute = 0;
  *second = 0.0;

  if (!(tok[0] = strtok (s, delimiters))) return -1;

  for (n = 1; n < 10 && (tok[n] = strtok ((char *)0, delimiters)); n++);

  want[DATE] = want[YEAR] = want[MONTH] = want[DAY] = 1;
  hold = 0;

  for (i = 0; i < n; i++) {

    if (end_of_date && want[DATE] && (tok[i] > end_of_date)) {
      want[DATE] = 0;
      want[HOUR] = want[MINUTE] = want[SECOND] = 1;
    }

    len = strlen (tok[i]);

    /* skip 3-digit day-of-year values in parenthesis */
    if ((len == 5) && (*tok[i] == '(') && (*(tok[i]+4) == ')')) {
      value = strtod (tok[i]+1, &ptr);
      if ((value > 0) && (value < 367)) continue; 
    }

    value = strtod (tok[i], &ptr); 
    if (ptr == tok[i]) {
      if (len < 3 || !want[DATE]) return -1;
      for (c = tok[i]; *c; c++) *c = tolower ((int)(*c));
      for (j = 0; j < 12; j++) {
        if (!strncmp (months[j], tok[i], len)) {
          *month = j + 1;
          want[MONTH] = 0;
          if (hold) {
            if (*day_month) return -1;
            *day_month = hold;
            hold = 0;
            want[DAY] = 0;
          }
          break;
        }
      }
      if (want[MONTH]) return -1;
      continue;
    }

    if (fmod (value, 1.0) != 0.0) {
      if (want[SECOND]) {
        *second = value;
        break;
      } else return -1;
    }

    number = (int)value;
    if (number < 0) return -1;

    if (want[DATE]) {   /* Date Part */

      if (!number) return -1;

      /* A 4-digit field is an explicit ISO year (yyyy), even when its value is
         small such as 0001 or 0050.  It is the *digit count*, not the magnitude,
         that tells a year from a month or day -- "01" and "0001" are different.
         Recognize it before the magnitude heuristics below, and do NOT apply the
         2-digit "+1900" century fixup, which is exactly what distinguishes a
         padded full year from a 2-digit one. */
      if (want[YEAR] && len == 4) {
        *year = number;
        want[YEAR] = 0;
      }
      else if (number > 31) {

        if (want[YEAR]) {
          *year = number;
          if (*year < 1000) *year += 1900;
          want[YEAR] = 0;
        } else if (want[MONTH]) {
          want[MONTH] = 0;
          *month = 0;
          *day_year = number;
          want[DAY] = 0;
        } else return -1;

      } 
      else if (number > 12) {

        if (want[DAY]) {
          if (hold) {
            *month = hold;
            want[MONTH] = 0;
          }
          if (len == 3) {
            if (*month) return -1;
            *day_year = number;
            *day_month = 0;
            want[MONTH] = 0;
          } else *day_month = number;
          want[DAY] = 0;
        } else return -1;

      } 
      else if (!want[MONTH]) {

        if (*month) {
          *day_month = number;
          *day_year = 0;
        } else {
          *day_year = number;
          *day_month = 0;
        }
        want[DAY] = 0;

      } 
      else if (!want[DAY]) {

        if (*day_year) return -1;
        *month = number;
        want[MONTH] = 0;

      } 
      else if (!want[YEAR]) {

        if (len == 3) {
          if (*month) return -1;
          *day_year = number;
          *day_month = 0;
          want[DAY] = 0;
        } else {
          if (*day_year) return -1;
          *month = number;
          if (hold) {
            *day_month = hold;
            want[DAY] = 0;
          }
        }
        want[MONTH] = 0;

      } 
      else if (hold) {

        *month = hold;
        hold = 0;
        want[MONTH] = 0;
        *day_month = number;
        want[DAY] = 0;

      } 
      else hold = number;

      if (!(want[YEAR] || want[MONTH] || want[DAY])) {
        want[DATE] = 0;
        want[HOUR] = want[MINUTE] = want[SECOND] = 1;
      }

    }

    else if (want[HOUR]) {       /* Time part */

      if (len == 4) {
        hold = number / 100;
        if (hold > 23) return -1;
        *hour = hold;
        hold = number % 100;
        if (hold > 59) return -1;
        *minute = hold;
        want[MINUTE] = 0;
      } else {
        if (number > 23) return -1;
        *hour = number;
      }
      want[HOUR] = 0;

    } 
    else if (want[MINUTE]) {

      if (number > 59) return -1;
      *minute = number;
      want[MINUTE] = 0;

    } 
    else if (want[SECOND]) {

      if (number > 61) return -1;
      *second = number;
      want[SECOND] = 0;

    } 
    else return -1;

  } /* for all tokens */

  if (*month > 12) return -1;
  if (*month && !*day_month) *day_month = 1;

  leap = *year & 3 ? 0 : (*year % 100 ? 1 : (*year % 400 ? 0 : 1));

  if (*month && *day_month && !*day_year) {
    if (*day_month > days_in_month[leap][*month]) return -1;
    *day_year = day_offset[leap][*month] + *day_month;
  } else if (*day_year && !*month && !*day_month) {
    if (*day_year > (365 + leap)) return -1;
    for (i = 2; i < 14 && *day_year > day_offset[leap][i]; i++);
    i--;
    *month = i;
    *day_month = *day_year - day_offset[leap][i];
  } else return -1;

  return 0;

} /* parsetime */


/* ************************************************************************* */
bool dt_parsetime(das_time* dt, const char* sTime)
{
	int nRet = parsetime(sTime,  &(dt->year), &(dt->month), &(dt->mday),
			               &(dt->yday), &(dt->hour), &(dt->minute),
	                     &(dt->second) );
	if(nRet == 0) return true;
	else return false;
}

/* The span rule needs to know which field was the last one written, and
 * parsetime() does not say, so the string is scanned for its shape
 * first and parsetime() is trusted for the values.  Shapes handled:
 * Y, Y-M, Y-M-D, Y-DOY, each optionally followed by T or space and
 * h, h:m, h:m:s, with an optional trailing Z.  Anything else, including
 * fractional seconds, is refused. */
bool dt_parsespan(const char* sTime, das_time* pBeg, das_time* pEnd)
{
	if(sTime == NULL) return false;
	char sBuf[64];
	strncpy(sBuf, sTime, sizeof(sBuf) - 1);
	sBuf[sizeof(sBuf) - 1] = '\0';

	/* trim blanks and a trailing Z */
	char* pS = sBuf;
	while(isspace((unsigned char)*pS)) ++pS;
	size_t uLen = strlen(pS);
	while((uLen > 0) && isspace((unsigned char)pS[uLen - 1])) pS[--uLen] = '\0';
	if((uLen > 0) && ((pS[uLen - 1] == 'Z')||(pS[uLen - 1] == 'z'))) pS[--uLen] = '\0';
	if(uLen == 0) return false;

	/* the date and the time of day */
	char* pTod = strchr(pS, 'T');
	if(pTod == NULL) pTod = strchr(pS, ' ');
	if(pTod != NULL){ *pTod = '\0'; ++pTod; if(*pTod == '\0') return false; }

	/* count fields and insist on digits */
	int nDate = 0, aDateLen[3] = {0, 0, 0};
	for(const char* p = pS; ; ++p){
		if((*p == '-')||(*p == '\0')){
			if(nDate == 3) return false;
			if(aDateLen[nDate] == 0) return false;
			++nDate;
			if(*p == '\0') break;
		}
		else if(isdigit((unsigned char)*p)) aDateLen[nDate] += 1;
		else return false;
	}
	int nTod = 0;
	if(pTod != NULL){
		int nLen = 0;
		for(const char* p = pTod; ; ++p){
			if((*p == ':')||(*p == '\0')){
				if((nTod == 3)||(nLen == 0)) return false;
				++nTod; nLen = 0;
				if(*p == '\0') break;
			}
			else if(isdigit((unsigned char)*p)) ++nLen;
			else return false;   /* a '.' lands here: fractions have no span */
		}
	}

	/* which unit steps: 0 year, 1 month, 2 day, 3 hour, 4 minute, 5 second */
	int nUnit;
	bool bDoy = (nDate == 2) && (aDateLen[1] == 3);
	if(nTod > 0){
		if((nDate < 3) && !bDoy) return false;   /* a time of day needs a whole date */
		nUnit = 2 + nTod;
	}
	else{
		nUnit = bDoy ? 2 : nDate - 1;
	}

	das_time dt;
	dt_null(&dt);
	if(nUnit == 0){
		/* parsetime() refuses a bare year */
		dt.year = atoi(pS);
		dt.month = 1; dt.mday = 1;
		if(dt.year <= 0) return false;
	}
	else if(nUnit == 1){
		dt.year = atoi(pS);
		dt.month = atoi(strchr(pS, '-') + 1);
		dt.mday = 1;
		if((dt.year <= 0)||(dt.month < 1)||(dt.month > 12)) return false;
	}
	else{
		if(pTod != NULL) *(pTod - 1) = 'T';   /* put the string back together */
		if(!dt_parsetime(&dt, pS)) return false;
	}
	dt_tnorm(&dt);

	das_time dtEnd;
	dt_copy(&dtEnd, &dt);
	switch(nUnit){
	case 0: dtEnd.year += 1;   break;
	case 1: dtEnd.month += 1;  break;
	case 2: dtEnd.mday += 1;   break;
	case 3: dtEnd.hour += 1;   break;
	case 4: dtEnd.minute += 1; break;
	default: dtEnd.second += 1.0; break;
	}
	dt_tnorm(&dtEnd);

	if(pBeg) dt_copy(pBeg, &dt);
	if(pEnd) dt_copy(pEnd, &dtEnd);
	return true;
}

/* ************************************************************************* */
bool dt_now(das_time* pDt)
{
	if(pDt == NULL) return false;

#ifndef _WIN32	
	struct timeval tv;
	struct tm* pTm;
		
	if(gettimeofday(&tv,NULL) != 0) return false;
	
	pTm = gmtime(&(tv.tv_sec));
	
	pDt->year = pTm->tm_year + 1900;
	pDt->month = pTm->tm_mon + 1;
	pDt->mday = pTm->tm_mday;
	pDt->yday = 0;
	pDt->hour = pTm->tm_hour;
	pDt->minute = pTm->tm_min;
	pDt->second = (double) pTm->tm_sec;
	pDt->second += (tv.tv_usec) / 1000000.0;
#else
	SYSTEMTIME st;
	GetSystemTime(&st);
	
	pDt->year   = st.wYear;
	pDt->month  = st.wMonth;
	pDt->mday   = st.wDay;
	pDt->yday   = 0;
	pDt->hour   = st.wHour;
	pDt->minute = st.wMinute;
	pDt->second = (double) st.wSecond;
	pDt->second += st.wMilliseconds / 1000.0;
	
#endif
	
	dt_tnorm(pDt);
	return true;
}

/* ************************************************************************* */

void dt_set(
	das_time* pDt, int year, int month, int mday, int yday, int hour, 
	int minute, double second
){
	/* Never call das_error from here!  It could trigger deadlocs in
	   LoadLeapSecondsTable() and LoadLeapNanoSecondsTable() */
	
	pDt->year = year;
	pDt->month = month;
	pDt->mday = mday;
	pDt->yday = yday;
	pDt->hour = hour;
	pDt->minute = minute;
	pDt->second = second;
}

void dt_copy(das_time* pDest, const das_time* pSrc)
{
	pDest->year = pSrc->year;
	pDest->month = pSrc->month;
	pDest->mday = pSrc->mday;
	pDest->yday = pSrc->yday;
	pDest->hour = pSrc->hour;
	pDest->minute = pSrc->minute;
	pDest->second = pSrc->second;
}

int dt_compare(const das_time* pA, const das_time* pB)
{	
	if(pA->year != pB->year) return pA->year - pB->year;
	if(pA->month != pB->month) return pA->month - pB->month;
	if(pA->mday != pB->mday) return pA->mday - pB->mday;
	if(pA->hour != pB->hour) return pA->hour - pB->hour;
	if(pA->minute != pB->minute) return pA->minute - pB->minute;
	
	if(pA->second < pB->second) return -1;
	if(pA->second > pB->second) return 1;
	return 0;
}


bool dt_in_range(
	const das_time* begin, const das_time* end, const das_time* test
){
	return ( (dt_compare(test, begin) >= 0 ) && (dt_compare(test, end) < 0));
}

static int _date_to_jday(const das_time* pDt)
{
	int y = pDt->year;
	int m = pDt->month;
	int d = pDt->mday;
	int jday = 0;
		
	m = (m + 9) % 12;
	y = y - m/10;
	jday = 365*y + y/4 - y/100 + y/400 + (m*306 + 5)/10 + ( d - 1 );
	return jday;
}


/* Adapted from similar algorithm in dastime.py */
double dt_diff(const das_time* pA, const das_time* pB)
{
	double fDiff = 0;
	
	/* Difference time of day */
	fDiff = (pA->hour*3600 + pA->minute*60 + pA->second)  - 
		      (pB->hour*3600 + pB->minute*60 + pB->second);
	
	/* Add julian day difference */
	int nDiff = _date_to_jday(pA) - _date_to_jday(pB);
	
	fDiff += nDiff * 86400.0;
	
	return fDiff;
}

/* The _toStr convention: sBuf on success, NULL if the string was cut off.
   snprintf has already NUL terminated whatever fit. */
static char* _dt_fit(char* sBuf, size_t uLen, int n)
{
	return ((n >= 0)&&((size_t)n < uLen)) ? sBuf : NULL;
}

char* dt_isoc(const das_time* pDt, char* sBuf, size_t uLen, int nFracSec)
{
	int n = 0;
	char sFmt[48] = {'\0'};
	int nSec = 0;
	const char* sMinuteFmt = "%04d-%02d-%02dT%02d:%02d"; 
	
	if(nFracSec < 1){
		nSec = (int) (round(pDt->second));
		snprintf(sFmt, 47,  "%s:%%02d", sMinuteFmt);
		n = snprintf(sBuf, uLen, sFmt, pDt->year, pDt->month, pDt->mday, pDt->hour,
				   pDt->minute, nSec);
	}
	else{
		if(nFracSec > 9) nFracSec = 9;
		
		snprintf(sFmt, 47,  "%s:%%0%d.%df", sMinuteFmt, nFracSec+3, nFracSec);
		n = snprintf(sBuf, uLen, sFmt, pDt->year, pDt->month, pDt->mday,
			 	  pDt->hour, pDt->minute, pDt->second);
	}
	
	return _dt_fit(sBuf, uLen, n);
}

char* dt_isod(const das_time* pDt, char* sBuf, size_t uLen, int nFracSec)
{
	int n = 0;
	char sFmt[48] = {'\0'};
	int nSec = 0;
	const char* sMinuteFmt = "%04d-%03dT%02d:%02d";
	
	if(nFracSec < 1){
		nSec = (int) (round(pDt->second));
		snprintf(sFmt, 47,  "%s:%%02d", sMinuteFmt);
		n = snprintf(sBuf, uLen, sFmt, pDt->year, pDt->yday, pDt->hour, pDt->minute,
				   nSec);
	}
	else{
		if(nFracSec > 9) nFracSec = 9;
		
		snprintf(sFmt, 47,  "%s:%%0%d.%df", sMinuteFmt, nFracSec+3, nFracSec);
		n = snprintf(sBuf, uLen, sFmt, pDt->year, pDt->yday, pDt->hour, pDt->minute, 
		         pDt->second);
	}
	
	return _dt_fit(sBuf, uLen, n);
}

char* dt_dual_str(const das_time* pDt, char* sBuf, size_t uLen, int nFracSec)
{
	int n = 0;
	char sFmt[64] = {'\0'};
	int nSec = 0;
	
	const char* sMinuteFmt = "%04d-%02d-%02d (%03d) %02d:%02d";
	if(nFracSec < 1){
		nSec = (int) (round(pDt->second));
		snprintf(sFmt, 63,  "%s:%%02d", sMinuteFmt);
		n = snprintf(sBuf, uLen, sFmt, pDt->year, pDt->month, pDt->mday, 
				   pDt->yday, pDt->hour, pDt->minute, nSec);
	}
	else{
		if(nFracSec > 9) nFracSec = 9;
		
		snprintf(sFmt, 63,  "%s:%%0%d.%df", sMinuteFmt, nFracSec+3, nFracSec);
		n = snprintf(sBuf, uLen, sFmt, pDt->year, pDt->month, pDt->mday, pDt->yday,
				   pDt->hour, pDt->minute, pDt->second);
	}
	
	return _dt_fit(sBuf, uLen, n);
}


/*---------------------------------------------------------------------------

  ttime.c adapted by L. Granroth  1997-08-29
  from dtime.c written by L. Granroth  1997-03-03
  converts time components to a double precision floating point value
  (seconds since the beginning of 1958, ignoring leap seconds) and normalize
  inputs.  Note that this floating point value should only be used for
  "internal" purposes.  (There's no need to propagate yet another time
  system, plus I want to be able to change/fix these values.)
  
  There is no accommodation for calendar adjustments, for example the
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


double dt_ttime(const das_time* dt){
	
	das_time _dt;
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

void dt_emitt(das_time* dt, double tt){
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


void dt_tnorm(das_time* dt){
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


int64_t dt_nano_1970(const das_time* pThis)
{
	/* Days since 1970, if abs is greater than 290 years (rounded down from
	 * the absolute max quoted on docs.scipy.org of 292) raise an error */
	
	das_time dt = *(pThis);  /* avoid constant pointer indirections */
	
	/* From: http://howardhinnant.github.io/date_algorithms.html Thanks! */
	dt.year -= dt.month <= 2;
	int32_t era = (dt.year >= 0 ? dt.year : dt.year-399) / 400;
	int32_t yoe = dt.year - era * 400;                           /* [0, 399] */
	int32_t doy = (153*(dt.month + (dt.month > 2 ? -3 : 9)) + 2)/5 + dt.mday -1;  /* [0, 365] */
	int32_t doe = yoe * 365 + yoe/4 - yoe/100 + doy;           /* [0, 146096] */
	int32_t epoch_days = era * 146097 + doe - 719468;
	 
	if( abs(epoch_days) > 105922 /* ~ 290 * 365.25 */ ){
		
		/* This is a das1 function, can't use das_error here!1 */
		fprintf(stderr, 
			"WARNING: Date %04d-%02d-%02d is not representable in the ns1970 time system\n", 
			dt.year, dt.month, dt.mday
		);
		/* Was actually hitting this in production code! Not confirmed but
		   the reason might be the odd default value for TT2000 time represented
		   as a string (9999-12-31T23:59:59.999999999 which is != to LONG_MIN!!!)
		   What the scientists did here is dumb.  Likely no programmer hand the 
		   power to challenge it.  
		   -cwp 2025-03-21
		*/
		/* exit(DASERR_TIME); */
		return LONG_MIN;
	}
	
	int64_t epoch = epoch_days;
	epoch *= ((int64_t)24)*60*60       *1000000000;
	
	epoch += ((int64_t)(dt.hour))*60*60*1000000000;
	epoch += ((int64_t)(dt.minute))*60 *1000000000;
	epoch += ((int64_t)(dt.second))    *1000000000;
	
	/* Rounded, not truncated: a parsed "58.939456789" is a double a hair
	   under the decimal value, and truncating loses the last nanosecond */
	double frac = dt.second - ((int64_t)(dt.second));
	epoch += llround(frac *1000000000);
	
	return epoch;
}

/* ************************************************************************ */
/* All TT2000 conversion in this file funnels through here.  The core takes
   doubles, but the sub-second fields arrive as integer counts so none of
   them is ever a fraction. */
static int64_t _tt2k_fromFields(
	int year, int month, int mday, int hour, int minute, int second,
	int64_t nNano
){
	return das_utc_to_tt2K(
		(double)year, (double)month, (double)mday, (double)hour, (double)minute,
		(double)second, (double)(nNano / 1000000), (double)((nNano / 1000) % 1000),
		(double)(nNano % 1000)
	);
}

int64_t dt_to_tt2k(const das_time* pThis){

	/* Round to the nanosecond once, then split.  Truncating each field in
	   turn dropped the last nanosecond of about half of all ns-resolution
	   times, since "58.939456789" parses to a double just under it.  The
	   clamp keeps a fraction within half a ns of the next second from
	   carrying into the seconds field, where 60 would read as a leap
	   second. */
	int nSec = (int)(pThis->second);
	int64_t nNano = llround((pThis->second - nSec) * 1.0e9);
	if(nNano > 999999999) nNano = 999999999;

	return _tt2k_fromFields(
		pThis->year, pThis->month, pThis->mday, pThis->hour, pThis->minute,
		nSec, nNano
	);
}

DasErrCode tt2k_from_parts(
	int year, int month, int mday, int hour, int minute, int second,
	int32_t nanosec, int64_t* pTT
){
	if(pTT == NULL)
		return das_error(DASERR_TIME, "No location given for the TT2000 value");

	int64_t nTT = _tt2k_fromFields(year, month, mday, hour, minute, second, nanosec);
	if(nTT == ILLEGAL_TT2000_VALUE)
		return das_error(DASERR_TIME, 
			"Can't express %04d-%02d-%02dT%02d:%02d:%02d + %d ns as TT2000, a field "
			"is negative or the time is outside 1707 to 2292", 
			year, month, mday, hour, minute, second, (int)nanosec
		);
	*pTT = nTT;
	return DAS_OKAY;
}

void tt2k_to_parts(
	int64_t nTT, int* pYear, int* pMonth, int* pMday, int* pHour,
	int* pMinute, int* pSecond, int32_t* pNanosec
){
	double yr, mt, dy, hr, mn, sc, ms, us, ns;
	das_tt2K_to_utc(nTT, &yr, &mt, &dy, &hr, &mn, &sc, &ms, &us, &ns);

	/* The core returns whole numbers in every field, the casts are exact */
	if(pYear)    *pYear   = (int)yr;
	if(pMonth)   *pMonth  = (int)mt;
	if(pMday)    *pMday   = (int)dy;
	if(pHour)    *pHour   = (int)hr;
	if(pMinute)  *pMinute = (int)mn;
	if(pSecond)  *pSecond = (int)sc;
	if(pNanosec) *pNanosec = (int32_t)ms*1000000 + (int32_t)us*1000 + (int32_t)ns;
}

void dt_from_tt2k(das_time* pThis, int64_t nTime)
{
	int nSec;
	int32_t nNano;
	tt2k_to_parts(
		nTime, &(pThis->year), &(pThis->month), &(pThis->mday), &(pThis->hour),
		&(pThis->minute), &nSec, &nNano
	);

	/* A leap second stays :60, as in Units_convertToDt().  The calendar
	   fields come back normalized, so only yday needs filling in, and that is
	   done by table rather than by dt_tnorm(): dt_tnorm() is NOT leap second
	   aware and rolls :60.5 forward to :00.5 of the next minute, which
	   collides with a real time.  The old clamp to :59 avoided that roll by
	   colliding with :59 instead. */
	pThis->second = nSec + nNano*1.0e-9;
	pThis->yday = days[LEAP(pThis->year)][pThis->month] + pThis->mday;
}

/* ************************************************************************* */

#ifdef TESTPROGRAM

int main (int argc, char *argv[]){
  int year, month, day_month, day_year, hour, minute;
  double second;
  char s[80];
  int len;

  do {

    printf ("%s: enter date/time string: ", argv[0]);
    gets (s);
    len = strlen (s);
    if (!len) break;
    if (parsetime (s, &year, &month, &day_month, &day_year,
                      &hour, &minute, &second))
      printf ("%s: error parsing %s\n", argv[0], s);
    else printf ("%s: %04d-%02d-%02d (%03d) %02d:%02d:%06.3f\n", argv[0],
      year, month, day_month, day_year, hour, minute, second);

  } while (!feof(stdin));

  exit (0);

}

#endif /* TESTPROGRAM */


/* ************************************************************************* */

#ifdef EXAMPLEUTIL

int main (int argc, char *argv[]){
  int year, month, day_month, day_year, hour, minute;
  double second;
  char s[80];
  int len, i;

  for (i = 1, len = 0; i < argc && len < 79; i++) {
    s[len++] = ' ';
    s[len] = '\0';
    strncat (s+len, argv[i], 79 - len);
    len = strlen (s);
  }

  s[80] = '\0';

  if (parsetime (s, &year, &month, &day_month, &day_year,
    &hour, &minute, &second)) {
    fprintf (stdout, "%s: error parsing %s\n", argv[0], s);
    exit (127);
  }
  
  fprintf (stdout, "%04d-%02d-%02d (%03d) %02d:%02d:%02d\n",
    year, month, day_month, day_year, hour, minute, (int)second);

  exit (0);

}

#endif /* EXAMPLEUTIL */
