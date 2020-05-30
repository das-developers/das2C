/******************************************************************************
* Copyright 1996-2014 United States Government as represented by the
* Administrator of the National Aeronautics and Space Administration.
* All Rights Reserved.
******************************************************************************/

/* Text from page: https://cdf.gsfc.nasa.gov/html/cdf_copyright.html ...
 *
 * Space Physics Data Facility
 * NASA/Goddard Space Flight Center
 * 
 * This software may be copied or redistributed as long as it is not sold for 
 * profit, but it can be incorporated into any other substantive product with 
 * or without modifications for profit or non-profit. If the software is 
 * modified, it must include the following notices:
 *
 * The software is not the original (for protection of the original author's 
 * reputations from any problems introduced by others)
 *
 * Change history (e.g. date, functionality, etc.) 
 *
 * This copyright notice must be reproduced on each copy made. This software 
 * is provided as is without any express or implied warranties whatsoever. 
 */
 
/* The software is not the original (for protection of the original author's 
 * reputations from any problems introduced by others)
 *
 * Change history:
 * 
 *  Chris Piker <chris-piker@uiowa.edu>, 2020-02-15
 *
 *  The following code was extracted from the CDF version 3.8 sources due
 *  to the need for a standalone TT2000 to UTC converter. Any errors in
 *  transcription are my own and should net reflect upon the CDF Authors.
 *
 *  The function names computeTT2000 and breakdownTT2000 have been changed
 *  to avoid symbol conflicts in the case that das2 libraries and official
 *  CDF libraries are loaded into the same address space.
 */


#include <stdlib.h>
#include <stdarg.h>
#include <math.h>
#include <stdio.h>
#include <string.h>

#include "tt2000.h"


#define YearWithin(a)		((a >= 1708) && (a <= 2291))
#define JulianDateJ2000_12h     2451545
#define J2000Since0AD12h        730485 /* 2000-1-1 since 0-1-1 */
#define J2000Since0AD12hSec     63113904000.0
#define J2000Since0AD12hMilsec  63113904000000.0
                                /* 86400000.0 * 730485 */
#define J2000LeapSeconds        32.0
#define dT                      32.184
#define dTinNanoSecs            32184000000LL
#define MJDbase                 2400000.5
#define SECinNanoSecs           1000000000LL
#define SECinNanoSecsD          1000000000.0
#define DAYinNanoSecs           86400000000000LL
#define HOURinNanoSecs          3600000000000LL
#define MINUTEinNanoSecs        60000000000LL
#define T12hinNanoSecs          43200000000000LL
/* Julian days for 1707-09-22 and 2292-04-11, the valid TT2000 range. */
#define JDY17070922             2344793
#define JDY22920411             2558297

#define ILLEGAL_TT2000_VALUE    (-9223372036854775805LL) 
#define FILLED_TT2000_VALUE	(-9223372036854775807LL-1)
#define TT2000END 		-99999.999
#define DEFAULT_TT2000_PADVALUE         ((long long)-9223372036854775807LL)

static double *TT2000NULL = 0;



/* Number of Delta(dAT) expressions before leap seconds were introduced */
#define NERA1 14

#define LASTLEAPSECONDDAY	20170101
/* Dates, Delta(AT)s and drift rates */
static double  LTS [][6]= {
/*      year month day    delta       drift      drift     */
      { 1960,  1,   1,  1.4178180,   37300.0, 0.0012960 },
      { 1961,  1,   1,  1.4228180,   37300.0, 0.0012960 },
      { 1961,  8,   1,  1.3728180,   37300.0, 0.0012960 },
      { 1962,  1,   1,  1.8458580,   37665.0, 0.0011232 },
      { 1963, 11,   1,  1.9458580,   37665.0, 0.0011232 },
      { 1964,  1,   1,  3.2401300,   38761.0, 0.0012960 },
      { 1964,  4,   1,  3.3401300,   38761.0, 0.0012960 },
      { 1964,  9,   1,  3.4401300,   38761.0, 0.0012960 },
      { 1965,  1,   1,  3.5401300,   38761.0, 0.0012960 },
      { 1965,  3,   1,  3.6401300,   38761.0, 0.0012960 },
      { 1965,  7,   1,  3.7401300,   38761.0, 0.0012960 },
      { 1965,  9,   1,  3.8401300,   38761.0, 0.0012960 },
      { 1966,  1,   1,  4.3131700,   39126.0, 0.0025920 },
      { 1968,  2,   1,  4.2131700,   39126.0, 0.0025920 },
      { 1972,  1,   1, 10.0,             0.0, 0.0       },
      { 1972,  7,   1, 11.0,             0.0, 0.0       },
      { 1973,  1,   1, 12.0,             0.0, 0.0       },
      { 1974,  1,   1, 13.0,             0.0, 0.0       },
      { 1975,  1,   1, 14.0,             0.0, 0.0       },
      { 1976,  1,   1, 15.0,             0.0, 0.0       },
      { 1977,  1,   1, 16.0,             0.0, 0.0       },
      { 1978,  1,   1, 17.0,             0.0, 0.0       },
      { 1979,  1,   1, 18.0,             0.0, 0.0       },
      { 1980,  1,   1, 19.0,             0.0, 0.0       },
      { 1981,  7,   1, 20.0,             0.0, 0.0       },
      { 1982,  7,   1, 21.0,             0.0, 0.0       },
      { 1983,  7,   1, 22.0,             0.0, 0.0       },
      { 1985,  7,   1, 23.0,             0.0, 0.0       },
      { 1988,  1,   1, 24.0,             0.0, 0.0       },
      { 1990,  1,   1, 25.0,             0.0, 0.0       },
      { 1991,  1,   1, 26.0,             0.0, 0.0       },
      { 1992,  7,   1, 27.0,             0.0, 0.0       },
      { 1993,  7,   1, 28.0,             0.0, 0.0       },
      { 1994,  7,   1, 29.0,             0.0, 0.0       },
      { 1996,  1,   1, 30.0,             0.0, 0.0       },
      { 1997,  7,   1, 31.0,             0.0, 0.0       },
      { 1999,  1,   1, 32.0,             0.0, 0.0       },
      { 2006,  1,   1, 33.0,             0.0, 0.0       },
      { 2009,  1,   1, 34.0,             0.0, 0.0       },
      { 2012,  7,   1, 35.0,             0.0, 0.0       },
      { 2015,  7,   1, 36.0,             0.0, 0.0       },
      { 2017,  1,   1, 37.0,             0.0, 0.0       }
     };

/* Number of Delta(AT) in static table */
static const int NDAT = sizeof (LTS) / sizeof (LTS[0]);
static double **LTD = NULL;
static long long *NST = NULL;
/* Pre-computed times from das_utc_to_tt2000 for days in LTS table */
static long long NST2 [] = {
         0LL, 0LL, 0LL, 0LL, 0LL, 0LL, 0LL, 0LL, 0LL, 0LL,
         0LL, 0LL, 0LL, 0LL,
         -883655957816000000LL, -867931156816000000LL, -852033555816000000LL,
         -820497554816000000LL, -788961553816000000LL, -757425552816000000LL,
         -725803151816000000LL, -694267150816000000LL, -662731149816000000LL,
         -631195148816000000LL, -583934347816000000LL, -552398346816000000LL,
         -520862345816000000LL, -457703944816000000LL, -378734343816000000LL,
         -315575942816000000LL, -284039941816000000LL, -236779140816000000LL,
         -205243139816000000LL, -173707138816000000LL, -126273537816000000LL,
          -79012736816000000LL,  -31579135816000000LL,  189345665184000000LL,
          284040066184000000LL,  394372867184000000LL,  488980868184000000LL,
          536500869184000000LL};

static int entryCnt;
static char *leapTableEnv = NULL;

static long doys1[] = {31,59,90,120,151,181,212,243,273,304,334,365};
static long doys2[] = {31,60,91,121,152,182,213,244,274,305,335,366};
static long daym1[] = {31,28,31,30,31,30,31,31,30,31,30,31};
static long daym2[] = {31,29,31,30,31,30,31,31,30,31,30,31};

static int tableChecked = 0;
static int openCDF64s = 0;
static int toPlus = 0;
static long currentDay = -1;
static double currentLeapSeconds;
static double currentJDay;
static int fromFile = 0;

static double LeapSecondsfromYMD (long iy, long im, long id);


/******************************************************************************
* cdf_AllocateMemory.
******************************************************************************/

/******************************************************************************
* MEM structures/typedef's/global variables.
******************************************************************************/

typedef struct memSTRUCT {	/* Structure. */
  void *ptr;
  struct memSTRUCT *next;
  size_t nBytes;
} MEM;
typedef MEM *MEMp;		/* Pointer (to structure). */
static MEMp memHeadP = NULL;	/* Head of memory linked list. */

typedef struct CDFidSTRUCT {	/* Structure. */
  void *ptr;
  struct CDFidSTRUCT *next;
} CDFidMEM;
typedef CDFidMEM *CDFidMEMp;	/* Pointer (to structure). */

void* cdf_AllocateMemory (size_t nBytes, void (*fatalFnc)(char*) )
{
  MEMp mem;
  if (nBytes < 1) {
    fprintf(stderr, "%sllocation FAILED [%d]: %lu bytes\n", "A",1,nBytes);
    return NULL;
  }
  mem = (MEMp) malloc (sizeof(MEM));
  if (mem == NULL) {
	 fprintf(stderr, "%sllocation FAILED [%d]: %lu bytes\n","A",2,nBytes);
    if (fatalFnc != NULL) (*fatalFnc)("Unable to allocate memory buffer [1].");
    return NULL;
  }
  mem->ptr = (void *) malloc (nBytes);
  if (mem->ptr == NULL) {
	 fprintf(stderr, "%sllocation FAILED [%d]: %lu bytes\n", "A",3,nBytes);
    free (mem);
    if (fatalFnc != NULL) (*fatalFnc)("Unable to allocate memory buffer [2].");
    return NULL;
  }
  mem->nBytes = nBytes;
  mem->next = memHeadP;
  memHeadP = mem;
  /* ALLOCSUCCESS("A",mem->nBytes,mem->ptr,TotalBytes(memHeadP)) */
  return mem->ptr;
}

/******************************************************************************
* cdf_FreeMemory.
* If NULL is passed as the pointer to free, then free the entire list of
* allocated memory blocks.
******************************************************************************/

int cdf_FreeMemory(void* ptr, void (*fatalFnc)(char*) )
{
  /* --numAllocs; */ /* printf("(-1)numAllocs=%ld\n",numAllocs); */
  if (ptr == NULL) {
    int count = 0;
    MEMp mem = memHeadP;
    while (mem != NULL) {
      MEMp memX = mem;
      mem = mem->next;
      free (memX->ptr);
      free (memX);
      count++;
    }
    memHeadP = NULL;
    return count;
  }
  else {
    MEMp mem = memHeadP, memPrev = NULL;
    while (mem != NULL) {
      if (mem->ptr == ptr) {
	MEMp memX = mem;
	if (memPrev == NULL)
	  memHeadP = mem->next;
	else
	  memPrev->next = mem->next;
	free (memX->ptr);
	free (memX);
	return 1;
      }
      memPrev = mem;
      mem = mem->next;
    }
    fprintf (stdout, "Free failed [pointer not found]: %p\n", ptr);
    if (fatalFnc != NULL) (*fatalFnc)("Unable to free memory buffer.");
    return 0;
  }
}

/******************************************************************************
* CDFgetLeapSecondsTableEnvVar.
******************************************************************************/
    
char *CDFgetLeapSecondsTableEnvVar ()
{
    if (openCDF64s > 0) return leapTableEnv;
    else return getenv("CDF_LEAPSECONDSTABLE");
}

/* ************************************************************************** */

static double JulianDay12h (long y, long m, long d)
{
  if (m == 0) m = 1;
  return (double) (367*y-7*(y+(m+9)/12)/4-3*((y+(m-9)/7)/100+1)/4+275*m/9+d+1721029);
}

/* ************************************************************************** */

static int ValidateYMD (long yy, long mm, long dd)
{
  double jday;
  if (yy <= 0 || mm < 0 || dd < 0) return 0;
  /* Y-M-D should be in the 1707-09-22 and 2292-04-11 range. */
  jday = JulianDay12h(yy, mm, dd);
  if (jday < JDY17070922 || jday > JDY22920411) return 0;
  return 1; 
}


/* ************************************************************************** */

static void DatefromJulianDay (double julday, long *y, long *m, long *d)
{
  long i, j, k, l, n;
  l=(long) (julday+68569);
  n=4*l/146097;
  l=l-(146097*n+3)/4;
  i=4000*(l+1)/1461001;
  l=l-1461*i/4+31;
  j=80*l/2447;
  k=l-2447*j/80;
  l=j/11;
  j=j+2-12*l;
  i=100*(n-49)+i+l;
  *y = i;
  *m = j;
  *d = k;
  return; 
}




/* ************************************************************************** */

static void LoadLeapSecondsTable ()
{

  if (tableChecked == 0) {
    char *table;
    FILE *leaptable = NULL;
    int im, ix;
    table = CDFgetLeapSecondsTableEnvVar();
    if (table != NULL && strlen(table) > 0) {
      leapTableEnv = (char *) malloc (strlen(table)+1);
      strcpy (leapTableEnv, table);
      leaptable = fopen (table, "r");
      if (leaptable != NULL) {
        int togo = 1;
        char line[81];
        int count = 0;
        long yy, mm, dd;
        while (fgets(line, 80, leaptable)) {
          if (line[0] == ';') continue;
          ++count;
        }
        rewind(leaptable);
        LTD = (double **) cdf_AllocateMemory (count * sizeof (double *), NULL);
        ix = 0;
        while (fgets(line, 80, leaptable)) {
          if (line[0] == ';') continue;
          LTD[ix] = (double *) cdf_AllocateMemory(6 * 8, NULL);
          im = sscanf (line, "%ld %ld %ld %lf %lf %lf", &yy, &mm, &dd,
                       &(LTD[ix][3]), &(LTD[ix][4]), &(LTD[ix][5]));
          if (im != 6) {
            int iz;
            for (iz = 0; iz < ix; ++iz)
              cdf_FreeMemory (LTD[iz], NULL);
            cdf_FreeMemory (LTD, NULL);
            togo = 0;
            break;
          }
          LTD[ix][0] = (double) yy;
          LTD[ix][1] = (double) mm;
          LTD[ix][2] = (double) dd;
          ++ix;
        }
        fclose (leaptable);
        if (togo == 1) {
          entryCnt = count;
          fromFile = 1;
        }
      } else
        fromFile = 0;
    } else {
      leapTableEnv = NULL;
      fromFile = 0;
    }
    if (fromFile == 0) {
      LTD = (double **) cdf_AllocateMemory (NDAT * sizeof (double *), NULL);
      for (ix = 0; ix < NDAT; ++ix) {
        LTD[ix] = (double *) cdf_AllocateMemory(6 * 8, NULL);
        LTD[ix][0] = LTS[ix][0];
        LTD[ix][1] = LTS[ix][1];
        LTD[ix][2] = LTS[ix][2];
        LTD[ix][3] = LTS[ix][3];
        LTD[ix][4] = LTS[ix][4];
        LTD[ix][5] = LTS[ix][5];
      }
      entryCnt = NDAT;
    }
    tableChecked = 1;
  }
}

/* ************************************************************************** */

static void LoadLeapNanoSecondsTable () 
{
  int ix;
  if (LTD == NULL) LoadLeapSecondsTable ();
  NST = (long long *) cdf_AllocateMemory (entryCnt * sizeof (long long), NULL);
  if (fromFile == 0)
    memcpy (NST, NST2, entryCnt * sizeof (long long));
  else {
    if (LTD[entryCnt-1][0] == LTS[entryCnt-1][0]) 
      memcpy (NST, NST2, entryCnt * sizeof (long long));
    else {
      for (ix = NERA1; ix < entryCnt; ++ix) {
         NST[ix] = das_utc_to_tt2000(LTD[ix][0], LTD[ix][1],
                                             LTD[ix][2], 0.0, 0.0, 0.0,
                                             0.0, 0.0, 0.0);
      }
    }
  }
}

/* ************************************************************************** */

long long das_utc_to_tt2000(double yy, double mm, double dd, ...)
{
  double jd;
  long long subDayinNanoSecs, nanoSecSinceJ2000;
  long long t2, iy;
  double tmp, opt[6];
  va_list ap;
  int  ix;
  double ly, lm, ld, lh, ln, ls, ll, lu, la;
  long lyl, lml, ldl, lhl, lnl, lsl, lll, lul, lal;
  long xy, xm, xd;
  int  lyear;

  va_start (ap, dd);

  ix = 0;
  tmp = va_arg (ap, double);
  while (tmp != TT2000END) {
    opt[ix] = tmp;
    ++ix;
    if (ix == 6) break;
    tmp = va_arg (ap, double);
  }
  va_end (ap);
  if (mm == 0.0) mm = 1.0;
  ly = floor(yy);
  lm = floor(mm);
  ld = floor(dd);
  if (ix == 6) {
    if (opt[0] < 0.0 || opt[1] < 0.0 || opt[2] < 0.0 || opt[3] < 0.0 ||
        opt[4] < 0.0 || opt[5] < 0.0) return ILLEGAL_TT2000_VALUE;
    lh = opt[0];
    ln = opt[1];
    ls = opt[2];
    ll = opt[3];
    lu = opt[4];
    if ((dd-ld) != 0.0 || (lh-floor(lh)) != 0.0 || (ln-floor(ln)) != 0.0 ||
        (ls-floor(ls)) != 0.0 || (ll-floor(ll)) != 0.0 ||
        (lu-floor(lu)) != 0.0) return ILLEGAL_TT2000_VALUE;
    la = opt[5];
  } else if (ix == 5) {
    if (opt[0] < 0.0 || opt[1] < 0.0 || opt[2] < 0.0 || opt[3] < 0.0 ||
        opt[4] < 0.0) return ILLEGAL_TT2000_VALUE;
    lh = opt[0];
    ln = opt[1];
    ls = opt[2];
    ll = opt[3];
    if ((dd-ld) != 0.0 || (lh-floor(lh)) != 0.0 || (ln-floor(ln)) != 0.0 ||
        (ls-floor(ls)) != 0.0 || (ll-floor(ll)) != 0.0)
      return ILLEGAL_TT2000_VALUE;
    lu = floor(opt[4]);
    la = (opt[4] - lu) * 1000.0;
  } else if (ix == 4) {
    if (opt[0] < 0.0 || opt[1] < 0.0 || opt[2] < 0.0 || opt[3] < 0.0)
      return ILLEGAL_TT2000_VALUE;
    lh = opt[0];
    ln = opt[1];
    ls = opt[2];
    if ((dd-ld) != 0.0 || (lh-floor(lh)) != 0.0 || (ln-floor(ln)) != 0.0 ||
        (ls-floor(ls)) != 0.0) return ILLEGAL_TT2000_VALUE;
    ll = floor(opt[3]); 
    tmp = (opt[3] - ll) * 1000.0;
    lu = floor(tmp);
    la = (tmp - lu) * 1000.0;
  } else if (ix == 3) {
    if (opt[0] < 0.0 || opt[1] < 0.0 || opt[2] < 0.0)
      return ILLEGAL_TT2000_VALUE;
    lh = opt[0];
    ln = opt[1];
    if ((dd-ld) != 0.0 || (lh-floor(lh)) != 0.0 || (ln-floor(ln)) != 0.0)
      return ILLEGAL_TT2000_VALUE;
    ls = floor(opt[2]);
    tmp = (opt[2] - ls) * 1000.0;
    ll = floor(tmp); 
    tmp = (tmp - ll) * 1000.0;
    lu = floor(tmp);
    la = (tmp - lu) * 1000.0;
  } else if (ix == 2) {
    if (opt[0] < 0.0 || opt[1] < 0.0) return ILLEGAL_TT2000_VALUE;
    lh = opt[0];
    if ((dd-ld) != 0.0 || (lh-floor(lh)) != 0.0) return ILLEGAL_TT2000_VALUE;
    ln = floor(opt[1]);
    tmp = opt[1] - ln;
    if (tmp > 0.0) {
      tmp *= 60.0;
      ls = floor(tmp);
      tmp = (tmp - ls) * 1000.0;
      ll = floor(tmp);
      tmp = (tmp - ll) * 1000.0;
      lu = floor(tmp);
      la = (tmp - lu) * 1000.0;
    } else {
      ls = ll = lu = la = 0.0;
    }
  } else if (ix == 1) {
    if ((dd-ld) != 0.0) return ILLEGAL_TT2000_VALUE;
    if (opt[0] < 0.0) return ILLEGAL_TT2000_VALUE;
    tmp = opt[0];
    if (tmp > 0.0) {
      lh = floor(tmp);
      tmp = (tmp - lh) * 60.0;
      ln = floor(tmp);
      tmp = (tmp - ln) * 60.0;
      ls = floor(tmp);
      tmp = (tmp - ls) * 1000.0;
      ll = floor(tmp);
      tmp = (tmp - ll) * 1000.0;
      lu = floor(tmp);
      la = (tmp - lu) * 1000.0;
    } else {
      lh = ln = ls = ll = lu = la = 0.0;
    }
  } else { /* ix == 0 */
    tmp = dd - ld;
    if (tmp > 0.0) {
      tmp *= 24.0;
      lh = floor(tmp);
      tmp = (tmp - lh) * 60.0;
      ln = floor(tmp);
      tmp = (tmp - ln) * 60.0;
      ls = floor(tmp);
      tmp = (tmp - ls) * 1000.0;
      ll = floor(tmp);
      tmp = (tmp - ll) * 1000.0;
      lu = floor(tmp);
      la = (tmp - lu) * 1000.0;
    } else {
      lh = ln = ls = ll = lu = la = 0.0;
    }
  }
  lyl = lml = -999;
  if (la >= 1000.0) {
    double ad, ah, am, as, al, au;
    ad = floor(la / 86400000000000.0);
    la = la - ad * 86400000000000.0;
    ah = floor(la /3600000000000.0);
    la = la - ah * 3600000000000.0;
    am = floor(la / 60000000000.0);
    la = la - am * 60000000000.0;
    as = floor(la / 1000000000.0);
    la = la - as * 1000000000.0;
    al = floor(la /1000000.0); 
    la = la - al * 1000000.0;
    au = floor(la /1000.0);
    la = la - au * 1000.0;
    ld += ad;
    lh += ah;
    ln += am; 
    ls += as;
    ll += al; 
    lu += au;
    tmp = JulianDay12h ((long) ly, (long) lm, (long) ld);
    DatefromJulianDay (tmp, &lyl, &lml, &ldl);
  } 
  if (lu >= 1000.0) {
    double ad, ah, am, as, al;
    ad = floor(lu / 86400000000.0);
    lu = lu - ad * 86400000000.0;
    ah = floor(lu /3600000000.0);
    lu = lu - ah * 3600000000.0;
    am = floor(lu / 60000000);
    lu = lu - am * 60000000;
    as = floor(lu / 1000000);
    lu = lu - as * 1000000;
    al = floor(lu /1000);
    lu = lu - al * 1000;
    ld += ad;
    lh += ah;
    ln += am;
    ls += as;
    ll += al;
    tmp = JulianDay12h ((long) ly, (long) lm, (long) ld);
    DatefromJulianDay (tmp, &lyl, &lml, &ldl);
  }
  if (ll >= 1000.0) {
    double ad, ah, am, as;
    ad = floor(ll / 86400000);
    ll = ll - ad * 86400000;
    ah = floor(ll /3600000);
    ll = ll - ah * 3600000;
    am = floor(ll / 60000);
    ll = ll - am * 60000;
    as = floor(ll / 1000);
    ll = ll - as * 1000;
    ld += ad;
    lh += ah;
    ln += am;
    ls += as;
    tmp = JulianDay12h ((long) ly, (long) lm, (long) ld);
    DatefromJulianDay (tmp, &lyl, &lml, &ldl);
  }
  if (ls >= 60.0) {
    tmp = JulianDay12h ((long) ly, (long) lm, (long) ld);
    DatefromJulianDay (tmp+1, &xy, &xm, &xd);
    toPlus = LeapSecondsfromYMD(xy,xm,xd) - LeapSecondsfromYMD(ly,lm,ld);
    toPlus = floor(toPlus);
    if (ls >= (60.0+toPlus)) {
      double ad, ah, am;
      ad = floor(ls / (86400+toPlus));
      ls = ls - ad * (86400+toPlus);
      ah = floor(ls /(3600+toPlus));
      ls = ls - ah * (3600+toPlus);
      am = floor(ls / (60+toPlus));
      ls = ls - am * (60+toPlus);
      ld += ad;
      lh += ah;
      ln += am;
      tmp = JulianDay12h ((long) ly, (long) lm, (long) ld);
      DatefromJulianDay (tmp, &lyl, &lml, &ldl);
    }
  }
  if (ln >= 60.0) {
    double ad, ah;
    ad = floor(ln / 1440);
    ln = ln - (ad * 1440);
    ah = floor(ln / 60);
    ln = ln - ah * 60;
    ld += ad;
    lh += ah;
    tmp = JulianDay12h ((long) ly, (long) lm, (long) ld);
    DatefromJulianDay (tmp, &lyl, &lml, &ldl);
  }
  if (lh >= 24.0) {
    double ad;
    ad = floor(lh / 24.0); 
    lh = lh - ad * 24.0;
    ld += ad;
    tmp = JulianDay12h ((long) ly, (long) lm, (long) ld);
    DatefromJulianDay (tmp, &lyl, &lml, &ldl);
  }
  if (lyl == -999 && lml == -999) {
    lyl = (long) ly;
    lml = (long) lm;
    ldl = (long) ld;
  }
  lhl = (long) lh;
  lnl = (long) ln;
  lsl = (long) ls;
  lll = (long) ll;
  lul = (long) lu;
  lal = (long) la;
  if (lyl == 9999 && lml == 12 && ldl == 31 && lhl == 23 && lnl == 59 &&
/*      lsl == 59 && lll == 999 && lul == 999 && lal == 999) */
      lsl == 59 && lll == 999)
    return FILLED_TT2000_VALUE;
  else if (lyl == 0 && lml == 1 && ldl == 1 && lhl == 0 && lnl == 0 &&
           lsl == 0 && lll == 0 && lul == 0 && lal == 0)
    return DEFAULT_TT2000_PADVALUE;
  if (!YearWithin(lyl) && !ValidateYMD(lyl,lml,ldl))
    return ILLEGAL_TT2000_VALUE;
  lyear = (lyl & 3) == 0 && ((lyl % 25) != 0 || (lyl & 15) == 0);
  if ((!lyear && ldl > 365) || (lyear && ldl > 366))
    return ILLEGAL_TT2000_VALUE;
  if ((!lyear && lml > 1 && ldl > daym1[(int)lml-1]) ||
      (lyear && lml > 1 && ldl > daym2[(int)lml-1]))
    return ILLEGAL_TT2000_VALUE;
  if (lml <= 1 && ldl > 31) {
     /* DOY is passed in. */
     int ix;
     if (lml == 0) lml = 1;
     for (ix = 0; ix < 12; ++ix) {
        if ((!lyear && ldl <= doys1[ix]) || (lyear && ldl <= doys2[ix])) {
          if (ix == 0) break;
          else {
            lml = ix + 1;
            if (!lyear) ldl = ldl - doys1[ix-1];
            else ldl = ldl - doys2[ix-1];
            break;
          }
        }
     }
  }
  iy = 10000000 * lml + 10000 * ldl + lyl;
  if (iy != currentDay) {
    currentDay = iy;
    currentLeapSeconds = LeapSecondsfromYMD(lyl, lml, ldl);
    currentJDay = JulianDay12h(lyl,lml,ldl);
  }
  jd = currentJDay;
  jd = jd - JulianDateJ2000_12h;
  subDayinNanoSecs = lhl * HOURinNanoSecs + lnl * MINUTEinNanoSecs +
                     lsl * SECinNanoSecs + lll * 1000000 + lul * 1000 + lal;
  nanoSecSinceJ2000 = (long long) jd * DAYinNanoSecs + subDayinNanoSecs;
  t2 = (long long) (currentLeapSeconds * SECinNanoSecs);
  if (nanoSecSinceJ2000 < 0) {
    nanoSecSinceJ2000 += t2;
    nanoSecSinceJ2000 += dTinNanoSecs;
    nanoSecSinceJ2000 -= T12hinNanoSecs;
  } else {
    nanoSecSinceJ2000 -= T12hinNanoSecs;
    nanoSecSinceJ2000 += t2;
    nanoSecSinceJ2000 += dTinNanoSecs;
  }
  return nanoSecSinceJ2000;
}

/* ************************************************************************** */

static double LeapSecondsfromYMD (long iy, long im, long id)
{
  int i, j;
  long m, n;
  double da;
  if (LTD == NULL) LoadLeapSecondsTable();
  j = -1;
  m = 12 * iy + im;
  for (i = entryCnt-1; i >=0; --i) {
    n = (long) (12 * LTD[i][0] + LTD[i][1]);
    if (m >= n) {
      j = i;
      break;
    }
  }
  if (j == -1) return 0.0;
  da = LTD[j][3];
  /* If pre-1972, adjust for drift. */
  if (j < NERA1) {
    double jda;
    jda = JulianDay12h (iy, im, id);
    da += ((jda - MJDbase) - LTD[j][4]) * LTD[j][5];
  }
  return da;
}

/* ************************************************************************** */

static double LeapSecondsfromJ2000 (long long nanosecs, int *leapSecond)
{
  int i, j;
  double da;
  *leapSecond = 0;
  if (NST == NULL) LoadLeapNanoSecondsTable();
  j = -1;
  for (i = entryCnt-1; i >=NERA1; --i) {
    if (nanosecs >= NST[i]) {
      j = i;
      if (i < (entryCnt - 1)) {
        /* Check for time following on leap second (second = 60). */
        if ((nanosecs + 1000000000L) >= NST[i+1]) {
          *leapSecond = 1;
        }
      }
      break;
    }
  }
  if (j == -1) return 0.0; /* Pre 1972 .... do it later.... */
  da = LTD[j][3];
  return da;
}

/* ************************************************************************** */

void EPOCHbreakdownTT2000 (double epoch, long *year, long *month, long *day,
                           long *hour, long *minute, long *second)
{
  long jd,i,j,k,l,n;
  double second_AD, minute_AD, hour_AD, day_AD;
  second_AD = epoch;
  minute_AD = second_AD / 60.0;
  hour_AD = minute_AD / 60.0;
  day_AD = hour_AD / 24.0;

  jd = (long) (1721060 + day_AD);
  l=jd+68569;
  n=4*l/146097;
  l=l-(146097*n+3)/4;
  i=4000*(l+1)/1461001;
  l=l-1461*i/4+31;
  j=80*l/2447;
  k=l-2447*j/80;
  l=j/11;
  j=j+2-12*l;
  i=100*(n-49)+i+l;

  *year = i;
  *month = j;
  *day = k;

  *hour   = (long) fmod (hour_AD, (double) 24.0);
  *minute = (long) fmod (minute_AD, (double) 60.0);
  *second = (long) fmod (second_AD, (double) 60.0);

  return;
}


/* ************************************************************************** */

void das_tt2000_to_utc(
	long long nanoSecSinceJ2000, double *ly, double *lm, double *ld, ...
){
  double epoch, *tmp, tmp1, dat0;
  long long t2, t3, secSinceJ2000;
  long nansec;
  double *opt[6];
  long ye1, mo1, da1, ho1, mi1, se1, ml1, ma1, na1;
  int  ix, leapSec;
  va_list ap;
  va_start (ap, ld);
  ix = 0;
  tmp = va_arg (ap, double *);
  while (tmp != TT2000NULL) {
    opt[ix] = tmp;
    ++ix;
    if (ix == 6) break;
    tmp = va_arg (ap, double *);
  }
  va_end (ap);
  if (nanoSecSinceJ2000 == FILLED_TT2000_VALUE) {
    *ly = 9999.0;
    *lm = 12.0;
    *ld = 31.0;
    if (ix > 0) *opt[0] = 23.0;
    if (ix > 1) *opt[1] = 59.0;
    if (ix > 2) *opt[2] = 59.0;
    if (ix > 3) *opt[3] = 999.0;
    if (ix > 4) *opt[4] = 999.0;
    if (ix > 5) *opt[5] = 999.0;
    return;
  } else if (nanoSecSinceJ2000 == DEFAULT_TT2000_PADVALUE) {
    *ly = 0.0;
    *lm = 1.0;
    *ld = 1.0;
    if (ix > 0) *opt[0] = 0.0;
    if (ix > 1) *opt[1] = 0.0;
    if (ix > 2) *opt[2] = 0.0;
    if (ix > 3) *opt[3] = 0.0;
    if (ix > 4) *opt[4] = 0.0;
    if (ix > 5) *opt[5] = 0.0;
    return;
  }
  toPlus = 0;
  t3 = nanoSecSinceJ2000;
  dat0 = LeapSecondsfromJ2000 (nanoSecSinceJ2000, &leapSec);
  if (nanoSecSinceJ2000 > 0) { /* try to avoid overflow (substraction first) */
    secSinceJ2000 = (long long) ((double)nanoSecSinceJ2000/SECinNanoSecsD);
    nansec = (long) (nanoSecSinceJ2000 - secSinceJ2000 * SECinNanoSecs);
    secSinceJ2000 -= 32; /* secs portion in dT */
    secSinceJ2000 += 43200; /* secs in 12h */
    nansec -= 184000000L; /* nanosecs portion in dT */
  } else { /* try to avoid underflow (addition first) */
    nanoSecSinceJ2000 += T12hinNanoSecs; /* 12h in nanosecs */
    nanoSecSinceJ2000 -= dTinNanoSecs /* dT in nanosecs */;
    secSinceJ2000 = (long long) ((double)nanoSecSinceJ2000/SECinNanoSecsD);
    nansec = (long) (nanoSecSinceJ2000 - secSinceJ2000 * SECinNanoSecs);
  }
  if (nansec < 0) {
    nansec = SECinNanoSecs + nansec;
    --secSinceJ2000;
  }
  t2 = secSinceJ2000 * SECinNanoSecs + nansec;
  if (dat0 > 0.0) { /* Post-1972.... */
    secSinceJ2000 -= (long long) dat0;
    epoch = (double) J2000Since0AD12hSec + secSinceJ2000;
    if (leapSec == 0) 
      EPOCHbreakdownTT2000 (epoch, &ye1, &mo1, &da1, &ho1, &mi1, &se1);
    else {
      /* second is at 60.... bearkdown function can't handle 60 so make it
         59 first and then add 1 second back. */
      epoch -= 1.0;
      EPOCHbreakdownTT2000 (epoch, &ye1, &mo1, &da1, &ho1, &mi1, &se1);
      se1 += 1;
    }
  } else { /* Pre-1972.... */
    long long tmpNanosecs;
    epoch = (double) secSinceJ2000 + J2000Since0AD12hSec;
    /* First guess */
    EPOCHbreakdownTT2000 (epoch, &ye1, &mo1, &da1, &ho1, &mi1, &se1);
    tmpNanosecs = das_utc_to_tt2000 ((double) ye1, (double) mo1,
                                             (double) da1, (double) ho1,
                                             (double) mi1, (double) se1,
                                             0.0, 0.0, (double) nansec);
    if (tmpNanosecs != t3) {
      long long tmpx, tmpy;
      dat0 = LeapSecondsfromYMD (ye1, mo1, da1);
      tmpx = t2 - (long long) (dat0 * SECinNanoSecs);
      tmpy = (long long) ((double)tmpx/SECinNanoSecsD);
      nansec = (long) (tmpx - tmpy * SECinNanoSecs);
      if (nansec < 0) {
        nansec = SECinNanoSecs + nansec;
        --tmpy;
      }
      epoch = (double) tmpy + J2000Since0AD12hSec;
      /* Second guess */
      EPOCHbreakdownTT2000 (epoch, &ye1, &mo1, &da1, &ho1, &mi1, &se1);
      tmpNanosecs = das_utc_to_tt2000 ((double) ye1, (double) mo1,
                                               (double) da1, (double) ho1,
                                               (double) mi1, (double) se1,
                                               0.0, 0.0, (double) nansec);
      if (tmpNanosecs != t3) {
        dat0 = LeapSecondsfromYMD (ye1, mo1, da1);
        tmpx = t2 - (long long) (dat0 * SECinNanoSecs);
        tmpy = (long long) ((double)tmpx/SECinNanoSecsD);
        nansec = (long) (tmpx - tmpy * SECinNanoSecs);
        if (nansec < 0) {
          nansec = SECinNanoSecs + nansec;
          --tmpy;
        }
        epoch = (double) tmpy + J2000Since0AD12hSec;
        /* One more determination */
        EPOCHbreakdownTT2000 (epoch, &ye1, &mo1, &da1, &ho1, &mi1, &se1);
      }
    }
  }
  if (se1 == 60) toPlus = 1; 
  ml1 = (long) (nansec / 1000000);
  tmp1 = nansec - 1000000 * ml1;
  if (ml1 > 1000) {
    ml1 -= 1000;
    se1 += 1;
  }
  ma1 = (long) (tmp1 / 1000);
  na1 = (long)  (tmp1 - 1000 * ma1);
  *ly = (double) ye1;
  *lm = (double) mo1;
  if (ix == 6) {
    *ld = (double) da1;
    *(opt[0]) = (double) ho1;
    *(opt[1]) = (double) mi1;
    *(opt[2]) = (double) se1;
    *(opt[3]) = (double) ml1;
    *(opt[4]) = (double) ma1;
    *(opt[5]) = (double) na1;
  } else if (ix == 5) {
    *ld = (double) da1;
    *(opt[0]) = (double) ho1;
    *(opt[1]) = (double) mi1;
    *(opt[2]) = (double) se1;
    *(opt[3]) = (double) ml1;
    *(opt[4]) = (double) ma1 + (na1 / 1000.0);
  } else if (ix == 4) {
    *ld = (double) da1;
    *(opt[0]) = (double) ho1;
    *(opt[1]) = (double) mi1;   
    *(opt[2]) = (double) se1;   
    *(opt[3]) = (double) ml1 + (ma1*1000.0 + na1) / 1000000.0;
  } else if (ix == 3) {
    *ld = (double) da1;
    *(opt[0]) = (double) ho1;
    *(opt[1]) = (double) mi1;
    tmp1 = ml1*1000000.0 + ma1*1000.0 + na1;
    *(opt[2]) = (double) se1 + tmp1 / 1000000000.0;
  } else if (ix == 2) {
    *ld = (double) da1;
    *(opt[0]) = (double) ho1;
    tmp1 = se1*1000000000.0 + ml1*1000000.0 + ma1*1000.0 + na1;
    *(opt[1]) = (double) mi1 + tmp1 / (60000000000.0+1000000000*toPlus);
  } else if (ix == 1) {
    *ld = (double) da1;
    tmp1 = mi1*60000000000.0 + se1*1000000000.0 + ml1*1000000.0 +
           ma1*1000.0 + na1;
    *(opt[0]) = (double) ho1 + tmp1 / (3600000000000.0+1000000000*toPlus);
  } else if (ix == 0) {
    tmp1 = ho1*3600000000000.0 + mi1*60000000000.0 + se1*1000000000.0 +
           ml1*1000000.0 + ma1*1000.0 + na1;
    *ld = (double) da1 + tmp1 / (86400000000000.0 + 1000000000*toPlus);
  }
}

