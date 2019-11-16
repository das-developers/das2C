#define _POSIX_C_SOURCE 200112L

/* ----------------------------------------------------------------------

  parsetime.c written by L. Granroth  04-15-93
  to parse typical ascii date/time strings and return
  year, month, day of month, day of year, hour, minute, second.

  --------------------------------------------------------------------- */

#include <ctype.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <sys/time.h>

#ifdef _WIN32
#pragma warning(disable : 4706)
#endif

#define DELIMITERS " \t/-:,_;\r\n"
#define PDSDELIMITERS " \t/-T:,_;\r\n"

#define DATE 0
#define YEAR 1
#define MONTH 2
#define DAY 3
#define HOUR 4
#define MINUTE 5
#define SECOND 6

#include "das1.h"


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
void dt_null(das_time_t* pDt)
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

  /* tokenize the time string */

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

    if (want[DATE]) {

      if (!number) return -1;

      if (number > 31) {

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

      } else if (number > 12) {

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

      } else if (!want[MONTH]) {

	if (*month) {
	  *day_month = number;
	  *day_year = 0;
	} else {
	  *day_year = number;
	  *day_month = 0;
	}
	want[DAY] = 0;

      } else if (!want[DAY]) {

	if (*day_year) return -1;
	*month = number;
	want[MONTH] = 0;

      } else if (!want[YEAR]) {

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

      } else if (hold) {

	*month = hold;
	hold = 0;
	want[MONTH] = 0;
	*day_month = number;
	want[DAY] = 0;

      } else hold = number;

      if (!(want[YEAR] || want[MONTH] || want[DAY])) {
        want[DATE] = 0;
        want[HOUR] = want[MINUTE] = want[SECOND] = 1;
      }

    } else if (want[HOUR]) {

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

    } else if (want[MINUTE]) {

      if (number > 59) return -1;
      *minute = number;
      want[MINUTE] = 0;

    } else if (want[SECOND]) {

      if (number > 61) return -1;
      *second = number;
      want[SECOND] = 0;

    } else return -1;

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
bool dt_parsetime(const char* sTime, das_time_t* dt)
{
	int nRet = parsetime(sTime,  &(dt->year), &(dt->month), &(dt->mday),
			               &(dt->yday), &(dt->hour), &(dt->minute),
	                     &(dt->second) );
	if(nRet == 0) return true;
	else return false;
}

/* ************************************************************************* */
bool dt_now(das_time_t* pDt)
{
	if(pDt == NULL) return false;
	
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
	
	dt_tnorm(pDt);
	return true;
}

/* ************************************************************************* */

void dt_set(
	das_time_t* pDt, int year, int month, int mday, int yday, int hour, 
	int minute, double second
){
	pDt->year = year;
	pDt->month = month;
	pDt->mday = mday;
	pDt->yday = yday;
	pDt->hour = hour;
	pDt->minute = minute;
	pDt->second = second;
}

void dt_copy(das_time_t* pDest, const das_time_t* pSrc)
{
	pDest->year = pSrc->year;
	pDest->month = pSrc->month;
	pDest->mday = pSrc->mday;
	pDest->yday = pSrc->yday;
	pDest->hour = pSrc->hour;
	pDest->minute = pSrc->minute;
	pDest->second = pSrc->second;
}

int dt_compare(const das_time_t* pA, const das_time_t* pB)
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
	const das_time_t* begin, const das_time_t* end, const das_time_t* test
){
	return ( (dt_compare(test, begin) >= 0 ) && (dt_compare(test, end) < 0));
}

int _date_to_jday(const das_time_t* pDt)
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
double dt_diff(const das_time_t* pA, const das_time_t* pB)
{
	double fDiff = 0;
	
	/* Difference time of day */
	fDiff = (pA->hour*3600 + pA->minute*60 + pA->second)  - 
		      (pB->hour*3600 + pB->minute*60 + pB->second);
	
	/* Add jullian day difference */
	int nDiff = _date_to_jday(pA) - _date_to_jday(pB);
	
	fDiff += nDiff * 86400.0;
	
	return fDiff;
}

char* dt_isoc(char* sBuf, size_t nLen, const das_time_t* pDt, int nFracSec)
{
	char sFmt[48] = {'\0'};
	int nSec = 0;
	const char* sMinuteFmt = "%04d-%02d-%02dT%02d:%02d"; 
	
	if(nFracSec < 1){
		nSec = (int) (round(pDt->second));
		snprintf(sFmt, 47,  "%s:%%02d", sMinuteFmt);
		snprintf(sBuf, nLen, sFmt, pDt->year, pDt->month, pDt->mday, pDt->hour,
				   pDt->minute, nSec);
	}
	else{
		if(nFracSec > 9) nFracSec = 9;
		
		snprintf(sFmt, 47,  "%s:%%0%d.%df", sMinuteFmt, nFracSec+3, nFracSec);
		snprintf(sBuf, nLen, sFmt, pDt->year, pDt->month, pDt->mday,
			 	  pDt->hour, pDt->minute, pDt->second);
	}
	
	return sBuf;
}

char* dt_isod(char* sBuf, size_t nLen, const das_time_t* pDt, int nFracSec)
{
	char sFmt[48] = {'\0'};
	int nSec = 0;
	const char* sMinuteFmt = "%04d-%03dT%02d:%02d";
	
	if(nFracSec < 1){
		nSec = (int) (round(pDt->second));
		snprintf(sFmt, 47,  "%s:%%02d", sMinuteFmt);
		snprintf(sBuf, nLen, sFmt, pDt->year, pDt->yday, pDt->hour, pDt->minute,
				   nSec);
	}
	else{
		if(nFracSec > 9) nFracSec = 9;
		
		snprintf(sFmt, 47,  "%s:%%0%d.%df", sMinuteFmt, nFracSec+3, nFracSec);
		snprintf(sBuf, nLen, sFmt, pDt->year, pDt->yday, pDt->hour, pDt->minute, 
		         pDt->second);
	}
	
	return sBuf;
}

char* dt_dual_str(char* sBuf, size_t nLen, const das_time_t* pDt, int nFracSec)
{
	char sFmt[64] = {'\0'};
	int nSec = 0;
	
	const char* sMinuteFmt = "%04d-%02d-%02d (%03d) %02d:%02d";
	if(nFracSec < 1){
		nSec = (int) (round(pDt->second));
		snprintf(sFmt, 63,  "%s:%%02d", sMinuteFmt);
		snprintf(sBuf, nLen, sFmt, pDt->year, pDt->month, pDt->mday, 
				   pDt->yday, pDt->hour, pDt->minute, nSec);
	}
	else{
		if(nFracSec > 9) nFracSec = 9;
		
		snprintf(sFmt, 63,  "%s:%%0%d.%df", sMinuteFmt, nFracSec+3, nFracSec);
		snprintf(sBuf, nLen, sFmt, pDt->year, pDt->month, pDt->mday, pDt->yday,
				   pDt->hour, pDt->minute, pDt->second);
	}
	
	return sBuf;
}

/* ************************************************************************* */
/* Used in most of Joe Groene's programs */

static int days_past [138] = {     0,   365,   730,  1096,  1461,  1826,
                                2191,  2557,  2922,  3287,  3652,  4018,
                                4383,  4748,  5113,  5479,  5844,  6209,
                                6574,  6940,  7305,  7670,  8035,  8401,
                                8766,  9131,  9496,  9862, 10227, 10592,
                               10957, 11323, 11688, 12053, 12418, 12784,
                               13149, 13514, 13879, 14245, 14610, 14975,
                               15340, 15706, 16071, 16436, 16801, 17167,
                               17532, 17897, 18262, 18628, 18993, 19358,
                               19723, 20089, 20454, 20819, 21184, 21550,
                               21915, 22280, 22645, 23011, 23376, 23741,
                               24106, 24472, 24837, 25202, 25567, 25933,
                               26298, 26663, 27028, 27394, 27759, 28124,
                               28489, 28855, 29220, 29585, 29950, 30316,
                               30681, 31046, 31411, 31777, 32142, 32507,
                               32872, 33238, 33603, 33968, 34333, 34699,
                               35064, 35429, 35794, 36160, 36525, 36890,
                               37255, 37621, 37986, 38351, 38716, 39082,
                               39447, 39812, 40177, 40543, 40908, 41273,
                               41638, 42004, 42369, 42734, 43099, 43465,
                               43830, 44195, 44560, 44926, 45291, 45656,
                               46021, 46387, 46752, 47117, 47482, 47848,
                               48213, 48578, 48943, 49309, 49674, 50039 };

void yrdy1958(int* pYear, int* pDoy, int days_since_1958)
{
   int index;
	
	if((days_since_1958 < 0)||(days_since_1958 > 50404)){
		fprintf(stderr, "ERROR: Can't convert %d Days Since 1958 to a year and "
				  "day of year value\n", days_since_1958);
		exit(127);
	}

   index = 0;
   while (days_since_1958 >= days_past [index])
      index++;

   index--;
   *pYear = 1958 + index;
   *pDoy = days_since_1958 - days_past [index] + 1;
}

int past_1958 (int year, int day)
{
   int yr, past;

   yr = year - 1958;
   past = days_past [yr];
   past += (day - 1);

   return (past);
}

void ms2hms (int* pHour, int* pMin, float* pSec, double ms_of_day)
{
   *pSec = (float) fmod ((double) (ms_of_day / 1000.0), 60.0);
   *pMin = (int) fmod ((double) (ms_of_day / 60000.0), 60.0);
   *pHour = (int) fmod ((double) (ms_of_day / 3600000.0), 24.0);
}

void dt_from_1958(
	unsigned short int daysSince1958, unsigned int msOfDay, das_time_t* dt
){
	dt->month = 1;
	yrdy1958( &(dt->year), &(dt->mday), daysSince1958);
	float fSec = 0.0;
	ms2hms( &(dt->hour), &(dt->minute), &fSec, msOfDay);
	dt->second = fSec;
	dt_tnorm(dt);
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
