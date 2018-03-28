/* Robert Johnson's handy fxtime (fix time) command line program.
 * 
 * This was found in the Cassini and Mars Express software areas 
 * but is acutally a useful general program and so has been copied
 * here to give it a more appropriate home.  As a bonus the program
 * came with an extensive test set, all and all nice work. 
 *
 *   -cwp 2016-09-11
 */

#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <stdint.h>
#include <string.h>

#include <das2/das1.h>

/* 
Version 1.0
  Wednesday, Janurary 12, 2005
Version 1.1
  Monday, Feburary 7, 2005
   fixed Feb 3, 2005 problem with -j 0.5 -h 0.5 -m 0.5, but -s 0.5 works fine
Version 1.2
  November 15, 2005
   implement time subtraction.
*/
static const char *sVersion="fxtime(), ver 1.2";

typedef struct raj_time_tag{
  char sTime[128];
  int32_t nYear,nDoy,nMonth,nDom,nDow,nHour,nMinute,nSecond,nMsec;
  double dYear,dDoy,dMonth,dDom,dHour,dMinute,dSecond;
}rajTime;

int fxParseTime(char *string,rajTime *t);
void fxNormalize(rajTime *t);

int rajParseTime(char *string,rajTime *t);
int rajParseIsoTFormat(char *s,rajTime *t);
void rajNormalize(rajTime *t);

double cmdln_parse_number(char *s);
double time_difference(rajTime t1,rajTime t2);
void show_help(FILE *h);


int bVerbose=0;

int main(int argc,char *argv[])
{
char *cmdln_time,*cmdln_time2,cSign,*sFormat=NULL;
char s1[128],s2[128];
double dYear,dDoy,dMonth,dDom,dHour,dMinute,dSecond;
rajTime t,t2;

int bDiff=0;

  cmdln_time=cmdln_time2=NULL;
  dYear=dDoy=dMonth=dDom=dHour=dMinute=dSecond=0.0;
 
  while(--argc){
    ++argv;
    if((!strcmp("-help",*argv)) || (!strcmp("-help",*argv))){
      show_help(stdout);
      exit(0);
    }
    else if(!strcmp("-diff",*argv)){
      bDiff=1;
    }
    else if(!strcmp("-f",*argv)){
      --argc;  ++argv;
      sFormat=*argv;
    }
    else if(!strcmp("-y",*argv) || !strcmp("+y",*argv)){
      cSign=*argv[0];  --argc;  ++argv;
      if(cSign=='-')  dYear-=cmdln_parse_number(*argv);
      else            dYear+=cmdln_parse_number(*argv);
    }
    else if(!strcmp("-j",*argv) || !strcmp("+j",*argv)){
      cSign=*argv[0];  --argc;  ++argv;
      if(cSign=='-')  dDoy-=cmdln_parse_number(*argv);
      else            dDoy+=cmdln_parse_number(*argv);
    }
    else if(!strcmp("-h",*argv) || !strcmp("+h",*argv)){
      cSign=*argv[0];  --argc;  ++argv;
      if(cSign=='-')  dHour-=cmdln_parse_number(*argv);
      else            dHour+=cmdln_parse_number(*argv);
    }
    else if(!strcmp("-m",*argv) || !strcmp("+m",*argv)){
      cSign=*argv[0];  --argc;  ++argv;
      if(cSign=='-')  dMinute-=cmdln_parse_number(*argv);
      else            dMinute+=cmdln_parse_number(*argv);
    }
    else if(!strcmp("-s",*argv) || !strcmp("+s",*argv)){
      cSign=*argv[0];  --argc;  ++argv;
      if(cSign=='-')  dSecond-=cmdln_parse_number(*argv);
      else            dSecond+=cmdln_parse_number(*argv);
    }
    else if((!strcmp("-version",*argv)) || (!strcmp("-ver",*argv))){
      fprintf(stderr,"%s\n",sVersion);
    }
    else if(!strcmp("-v",*argv)){
      bVerbose=1;
    }
    else{
      if(cmdln_time2!=NULL)
        fprintf(stderr,"invalid option %s\n",*argv);
      else if(cmdln_time!=NULL)
        cmdln_time2=*argv;
      else
        cmdln_time=*argv;
    }/* else */
  }

  if(cmdln_time==NULL){
    fprintf(stderr,"fxtime() - error - no time specified\n");
    exit(1);
  }


  
  fxParseTime(cmdln_time,&t);

  t.dYear+=dYear;
  t.dDoy+=dDoy;
  t.dHour+=dHour;
  t.dMinute+=dMinute;
  t.dSecond+=dSecond;

  fxNormalize(&t);

  if(cmdln_time2){
    fxParseTime(cmdln_time2,&t2);
    fxNormalize(&t2);
    sprintf(s1,"%04d-%03dT%02d:%02d:%02d.%03d",t.nYear,t.nDoy,
            t.nHour,t.nMinute,t.nSecond,t.nMsec);
    sprintf(s2,"%04d-%03dT%02d:%02d:%02d.%03d",t2.nYear,t2.nDoy,
            t2.nHour,t2.nMinute,t2.nSecond,t2.nMsec);
/*
    sprintf(s1,"%04ld-%03ldT%02ld:%02ld:%02ld.%03ld",t.nYear,t.nDoy+1,
            t.nHour,t.nMinute,t.nSecond,t.nMsec);
    sprintf(s2,"%04ld-%03ldT%02ld:%02ld:%02ld.%03ld",t2.nYear,t2.nDoy+1,
            t2.nHour,t2.nMinute,t2.nSecond,t2.nMsec);
*/

    if(bDiff>0){
      fprintf(stdout,"%.3f\n",time_difference(t,t2));
      return 0;
    }

    if(bVerbose)
      fprintf(stderr,"%s %s\n",s1,s2);
    fprintf(stdout,"%d\n",strcmp(s1,s2));
    return 0;
  }



  if(sFormat!=NULL){  /* raj */
  char sTime[512];
  struct tm x;

    x.tm_year=t.nYear-1900;   /* since 1900 */
    x.tm_mon=t.nMonth-1;      /* 0 - 11 */
    x.tm_mday=t.nDom;         /* 1 - 31 */
    x.tm_hour=t.nHour;        /* 0 - 23 */
    x.tm_min=t.nMinute;       /* 0 - 59 */
    x.tm_sec=t.nSecond;       /* 0 - 59 */
    x.tm_isdst=-1;            /* voodo sequence for mktime() */
    mktime(&x);
    x.tm_wday=t.nDow;                /* strftime fails around 1900 for days of*/
    strftime(sTime,512,sFormat,&x);  /* the week, so subsitute in our dow */
    fprintf(stdout,"%s\n",sTime);    
  }
  else{
    fprintf(stdout,"%04d-%03dT%02d:%02d:%02d.%03d\n",t.nYear,t.nDoy,
            t.nHour,t.nMinute,t.nSecond,t.nMsec);
  }
 

return 0;
}



void show_help(FILE *h)
{
fprintf(h,"%s\n",sVersion);
fprintf(h,"fxtime [OPTIONS] time_string [time_string2]\n");
fprintf(h,"\n");

fprintf(h,
"fxtime() parses most any time_string and optionally performs time\n"
"calculations.  fxtime() is useful for reading an arbitrary time \n"
"format and converting it to a known format.  If two time strings are \n"
"given, only a comparison is performed, NO OPTIONS are applied.  Return\n"
"values are -1,0,1.\n"
"\n"
);

fprintf(h,
"  The preferred time_string format is ISO(T) format: yyyy-doyThh:mm:ss.msc or\nyyyy-mn-dmThh:mm:ss.msc, but any time_string agreeable with LJG's parsetime() \nwill work.  ISO(T) is preferred because fxtime() will accept out of range and \nnormalize them.  Negative and fractional years, days, hours, minutes, and     \nseconds are acceptable to fxtime(); even negative hexidecimal values.\n\n"
);

fprintf(h,
"  Time strings not in the ISO(T) format MUST have all time components in     \nrange.  Only positive integer values are allowed, except seconds.  Seconds may\nbe specified with floating point numbers.\n\n"
);

fprintf(h,"OPTIONS\n");
fprintf(h,"  -diff T1 T2    return difference in seconds of T1 - T2 \n");
fprintf(h,"  -v             be verbose\n");
fprintf(h,"  -ver|-version  output program version\n");
fprintf(h,"  -h|-help       show help\n");
fprintf(h,"\n");
fprintf(h,"  -|+y NUM     subtract|add NUM of years\n");
fprintf(h,"  -|+j NUM     subtract|add NUM of days\n");
fprintf(h,"  -|+h NUM     subtract|add NUM of hours\n");
fprintf(h,"  -|+m NUM     subtract|add NUM of minutes\n");
fprintf(h,"  -|+s NUM     subtract|add NUM of seconds\n");
fprintf(h,"  NOTE: NUM may be one of the following valid numbers: floating point, integer, or hexadecimal (NUMs may be negative as well).\n\n");
fprintf(h,"  Examples using the ISO(T) format:\n");
fprintf(h,"    Subtract 3.2 days from Janurary 1, 2004\n");
fprintf(h,"    fxtime -j 3.2 2004-001\n");
fprintf(h,"    Subtract 15 days from Janurary 15, 2004\n");
fprintf(h,"    fxtime -j 0xF 2004-0xF\n");
fprintf(h,"    Subtract 15 days from Janurary 15, 2004\n");
fprintf(h,"    fxtime +j -0xF 2004-0x0F\n");
fprintf(h,"    Subtract 15 days from Janurary 15, 2004\n");
fprintf(h,"    fxtime 2004--0xF\n");

fprintf(h,"\n");
fprintf(h,"  Acceptable paresetime() formats\n");
fprintf(h,"    2004//180 12:30:59.125\n");
fprintf(h,"    2004-06-18 12:30:59.125\n");

return;
}



double cmdln_parse_number(char *s)
{
double d;

  if((d=strtod(s,NULL))==0.0)
    d=strtol(s,NULL,0);

return d;
}



double time_difference(rajTime t1,rajTime t2)
{
time_t d1,d2;
struct tm x1,x2;

    x1.tm_year=t1.nYear-1900;   /* since 1900 */
    x1.tm_mon=t1.nMonth-1;      /* 0 - 11 */
    x1.tm_mday=t1.nDom;         /* 1 - 31 */
    x1.tm_hour=t1.nHour;        /* 0 - 23 */
    x1.tm_min=t1.nMinute;       /* 0 - 59 */
    x1.tm_sec=t1.nSecond;      /* 0 - 59 */
    x1.tm_isdst=-1;             /* voodo sequence for mktime() */
    d1=mktime(&x1);
    x1.tm_wday=t1.nDow;         /* strftime fails around 1900 for days of*/
                                /* the week, so subsitute in our dow */

    x2.tm_year=t2.nYear-1900;   /* since 1900 */
    x2.tm_mon=t2.nMonth-1;      /* 0 - 11 */
    x2.tm_mday=t2.nDom;         /* 1 - 31 */
    x2.tm_hour=t2.nHour;        /* 0 - 23 */
    x2.tm_min=t2.nMinute;       /* 0 - 59 */
    x2.tm_sec=t2.nSecond;      /* 0 - 59 */
    x2.tm_isdst=-1;             /* voodo sequence for mktime() */
    d2=mktime(&x2);
    x2.tm_wday=t2.nDow;         /* strftime fails around 1900 for days of*/
                                /* the week, so subsitute in our dow */

return difftime(d1,d2);
}


/* ************************************************************************* 
 *
 * Functions copied in from the old rajTime.c file...
 *
 * ************************************************************************* */

/* functions accepting normalized values */
int leap_year(int nYear);
int days_in_month(int nYear,int nMonth);
int day_of_week(int nYear,int nDoy);

/* functions accepting unnormalized values */
void doy_to_monthdom(int nDoy,int nYear,int *nMonth,int *nDom);
double dom_to_doy(double dYear,double dMonth,double dDom);



/*                        J  F  M  A  M  J  J  A  S  O  N  D */
static int arDim[12]    ={31,28,31,30,31,30,31,31,30,31,30,31};
static int arDimLeap[12]={31,29,31,30,31,30,31,31,30,31,30,31};

/* static const int nDaysInMonth[12]={31,28,31,30,31,30,31,31,30,31,30,31}; */

/* 
static const char *sDayOfWeek[7]={
  "Sunday","Monday","Tuesday","Wednesday","Thursday","Friday","Saturday"
};
static const char *sMonthOfYear[12]={
  "Janurary","Feburary","March","April","May","June","July","August",
  "September","October","November","December"
};
*/


/*
  returns:
    0 - not iso(T) format
    1 - iso(T) format, yyyy-doyThh:mm:ss.msc
    2 - iso(T) format, yyyy-month-dayThh:mm:ss.msc
*/
int fxParseTime(char *string,rajTime *t)
{
int nIsoT;

  nIsoT=rajParseTime(string,t);
  t->dDoy=t->nDoy=t->dDoy+1.0;
  t->dMonth=t->nMonth=t->dMonth+1.0;
  t->dDom=t->nDom=t->dDom+1.0;

return nIsoT;
}



void fxNormalize(rajTime *t)
{

  t->dDoy-=1.0;
  t->dMonth-=1.0;
  t->dDom-=1.0;

  rajNormalize(t);

  t->dDoy+=1.0;
  t->dMonth+=1.0;
  t->dDom+=1.0;

  t->nDoy=t->dDoy;
  t->nMonth=t->dMonth;
  t->nDom=t->dDom;

return;
}




/*
  returns:
    0 - not iso(T) format
    1 - iso(T) format, yyyy-doyThh:mm:ss.msc
    2 - iso(T) format, yyyy-month-dayThh:mm:ss.msc
*/
int rajParseTime(char *string,rajTime *t)
{
int nIsoT;
int nYear,nMonth,nDom,nDoy,nHour,nMinute;
double dSec;

  if((nIsoT=rajParseIsoTFormat(string,t))==0){  /* NOT IS0-D Format */
    parsetime(string,&nYear,&nMonth,&nDom,&nDoy,&nHour,&nMinute,&dSec);
    if(nDoy>0)    nDoy-=1;
    if(nMonth>0)  nMonth-=1;
    if(nDom>0)    nDom-=1;
    t->dYear=nYear;
    t->dDoy=nDoy;
    t->dMonth=nMonth;
    t->dDom=nDom;
    t->dHour=nHour;
    t->dMinute=nMinute;
    t->dSecond=dSec;
  }

  rajNormalize(t);

return nIsoT;
}



int leap_year(int nYear)
{
int nLeap;

  if(nYear%100){   /* year is NOT a century year */
    if(nYear%4)    /* NOT evenly divisible by 4 */
      nLeap=365;
    else             
      nLeap=366;
  }
  else{            /* year IS a century year */
    if(nYear%400)  /* 1900 is not a leap year */
      nLeap=365;
    else           /* 2000 is a leap year */
      nLeap=366;
  }

return nLeap;
}



void doy_to_monthdom(int nDoy,int nYear,int *nMonth,int *nDom)
{
int *p;

  if((nDoy<0) || (nDoy>=leap_year(nYear))){
    fprintf(stderr,"fxtime() error - doy_to_monthdom(%d,%d,%p,%p)\n",
            nDoy,nYear,nMonth,nDom);
    exit(1);
  }

  if(leap_year(nYear) == 366)
    p=arDimLeap;
  else
    p=arDim;

  *nMonth=0;  *nDom=0;
  while(nDoy>=p[*nMonth])
    nDoy-=p[(*nMonth)++];
  *nDom=nDoy;

return;
}



/* Jan. 1, 0000 is a Saturday, Jan. 1, 001 is a Monday */
/* Doy should be normalized at this point */
int day_of_week(int nYear,int nDoy)
{
int norm,leap,dow;

  if(nYear==0){
    leap=0;
    norm=0;
  }
  else if(nYear==1){
    leap=1;
    norm=0;
  }
  else{
    nYear-=1;
    leap=(nYear/4);         /* maximum number of leap years since 1 B.C. */
    leap=leap-(nYear/100);  /* subtract bogus leap year, century years   */
    leap=leap+(nYear/400);  /* add back real century leap years          */
    norm=nYear-leap;        /* normal years                              */
    leap+=1;                /* count leap year in 1 B.C.                 */
  }

  dow=leap*2;    /* day advancement for leap years                           */
  dow=dow+norm;  /* total day advancement, normal and leap years             */
  dow=dow+7;     /* transform days advanced from saturday to the day of week */
                 /* number: sun=1, mon=2, tue=3, wed=4, thu=5, fri=6, sat=7  */
                 /* dow is the day of week for Janurary 1, xxxx              */
  dow=dow+nDoy;  /* add in current day of year */
  dow=dow%7;
  if(dow==0)
    dow=7;

  dow-=1;  /* normalize day of week to zero: sun=0, mon=1, tues=2, ... */

return dow;
}



/* assumes year, month, day are normalized to zero */
double dom_to_doy(double dYear,double dMonth,double dDom)
{
int i;
double dDoy=0.0;
int *p;


  while(dMonth>12.0){
    dDoy+=(double)leap_year((int)dYear); 
    dMonth-=12.0;
    dYear+=1.0;
  }
  while(dMonth<0.0){
    dYear-=1.0;
    dDoy-=(double)leap_year((int)dYear); 
    dMonth+=12.0;
  }
  if(leap_year((int)dYear)==366)
    p=arDimLeap;
  else
    p=arDim;

  for(i=0;i<(int)dMonth;i++)
   dDoy+=p[i]; 
  dDoy+=(dMonth-(int)dMonth)*p[i]; 

  dDoy+=dDom;


return dDoy;
}



/* 
  -> expects s to be of the form yyyy-doyThh:mm:ss.msc 
  -> normalizes doy to zero
  -> returns 1 if in iso-d format, otherwise 0
*/
int rajParseIsoTFormat(char *s,rajTime *t)
{
char *pBeg,*pEnd;


  t->dDoy=t->dMonth=t->dDom=0.0;

  /* decode year */
  pBeg=s;
  if( (t->dYear=strtod(pBeg,&pEnd)) == 0.0)
    t->dYear=strtol(pBeg,&pEnd,0);

  while(*pEnd==' ' || *pEnd=='\t')  ++pEnd;
  if(*pEnd=='\0')      return 1;
  else if(*pEnd!='-')  return 0;
  else                 ++pEnd;
  pBeg=pEnd;
  if( (t->dDoy=strtod(pBeg,&pEnd)) == 0.0)
    t->dDoy=strtol(pBeg,&pEnd,0);
  if(t->dDoy>=1.0)
    t->dDoy-=1.0;  /* normalize doy to zero */

  while(*pEnd==' ' || *pEnd=='\t')  ++pEnd;  /* allow yyyy-month-dayThh:mm:ss*/
  if(*pEnd=='\0')      ;
  else if(*pEnd=='-'){  /* last token was month, this is day of month */
    t->dMonth=t->dDoy;  /* already normalized to zero */
    t->dDoy=0.0;        
    pBeg=++pEnd;
    if( (t->dDom=strtod(pBeg,&pEnd)) == 0.0)
      t->dDom=strtol(pBeg,&pEnd,0);
    if(t->dDom>=1.0)
      t->dDom-=1.0;  /* normalize dom to zero */
  }

  while(*pEnd==' ' || *pEnd=='\t')  ++pEnd;
  if(*pEnd=='\0')      ;
  else if(*pEnd!='T')  return 0;
  else                 ++pEnd;
  pBeg=pEnd;
  if( (t->dHour=strtod(pBeg,&pEnd)) == 0.0)
    t->dHour=strtol(pBeg,&pEnd,0);

  while(*pEnd==' ' || *pEnd=='\t')  ++pEnd;
  if(*pEnd=='\0')      ;
  else if(*pEnd!=':')  return 0;
  else                 ++pEnd;
  pBeg=pEnd;
  if( (t->dMinute=strtod(pBeg,&pEnd)) == 0.0)
    t->dMinute=strtol(pBeg,&pEnd,0);

  while(*pEnd==' ' || *pEnd=='\t')  ++pEnd;
  if(*pEnd=='\0')      ;
  else if(*pEnd!=':')  return 0;
  else                 ++pEnd;
  pBeg=pEnd;
  if( (t->dSecond=strtod(pBeg,&pEnd)) == 0.0)
    t->dSecond=strtol(pBeg,&pEnd,0);

  /* deal with day of year -vs- month/day of month */
  if((t->dMonth != 0.0) || (t->dDom != 0.0)){
    t->dDoy=dom_to_doy(t->dYear,t->dMonth,t->dDom);
    return 2;
  }

return 1;
}



/* assume years and days have been normalized to include zero in the range   */
/* only works with day of year, NOT month/day of month; but will write month */
/* and day of month to the structure.                                        */
void rajNormalize(rajTime *t)
{
int nMonth,nDom;
double dLeap;


  /* fractions of time */
  t->nYear=t->dYear;
  t->dDoy+=(t->dYear-t->nYear)*leap_year(t->nYear);
  t->dYear=t->nYear;

  t->nDoy=t->dDoy;
  t->dHour+=(t->dDoy-t->nDoy)*24.0;
  t->dDoy=t->nDoy;

  t->nHour=t->dHour;
  t->dMinute+=(t->dHour-t->nHour)*60.0;
  t->dHour=t->nHour;

  t->nMinute=t->dMinute;
  t->dSecond+=(t->dMinute-t->nMinute)*60.0;
  t->dMinute=t->nMinute;


  while(t->dSecond>=60.0){
    t->dSecond-=60.0;
    t->dMinute+=1.0;
  }
  while(t->dSecond<0.0){
    t->dSecond+=60.0;
    t->dMinute-=1.0;
  }

  while(t->dMinute>=60.0){
    t->dMinute-=60.0;
    t->dHour+=1.0;
  }
  while(t->dMinute<0.0){
    t->dMinute+=60.0;
    t->dHour-=1.0;
  }

  while(t->dHour>=24.0){
    t->dHour-=24.0;
    t->dDoy+=1;
  }
  while(t->dHour<0.0){
    t->dHour+=24.0;
    t->dDoy-=1.0;
  }

  dLeap=(double)leap_year((int)t->dYear);
  while(t->dDoy>=dLeap){
    t->dDoy-=dLeap;
    t->dYear+=1.0;
    dLeap=(double)leap_year((int)t->dYear);
  }
  while(t->dDoy<0.0){
    t->dYear-=1.0;
    dLeap=(double)leap_year((int)t->dYear);
    t->dDoy+=dLeap;
  }

  /* all should be normalized */
  t->nYear=t->dYear;
  t->nDoy=t->dDoy;
  t->nHour=t->dHour;
  t->nMinute=t->dMinute;
  t->nSecond=t->dSecond;
  t->nMsec=(t->dSecond-t->nSecond)*1000.0;

  doy_to_monthdom(t->nDoy,t->nYear,&nMonth,&nDom);
  t->dMonth=t->nMonth=nMonth;
  t->dDom=t->nDom=nDom;

  t->nDow=day_of_week(t->nYear,t->nDoy);



return;
}


