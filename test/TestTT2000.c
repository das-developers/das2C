/** @file TestTT2000.c Unit test for handling Goddard's TT2000 time spec */

/* Author: Chris Piker <chris-piker@uiowa.edu>
 * 
 * This file contains test and example code and is meant to explain an
 * interface.
 * 
 * As United States courts have ruled that interfaces cannot be copyrighted,
 * the code in this individual source file, TestBuilder.c, is placed into the
 * public domain and may be displayed, incorporated or otherwise re-used without
 * restriction.  It is offered to the public without any without any warranty
 * including even the implied warranty of merchantability or fitness for a
 * particular purpose. 
 */

#define _POSIX_C_SOURCE 200112L
#include <string.h>

#include <das2/core.h>


void prnUtc(long long tt)
{
	double yr, mt, dy, hr, mn, sc, ms, us, ns;
	
	/* Don't use das_tt2k_to_utc() in application code, since it's a var
	   arg function without a template, it's too easy to provide the
		wrong data type!  It's used here for testing, but go with:
		
		Units_convertToDt(&dt, tt, UNIT_TT2000);
		
		instead.
	*/
	
	das_tt2K_to_utc(tt, &yr, &mt, &dy, &hr, &mn, &sc, &ms, &us, &ns);
	fprintf(stdout, 
		"%18lldLL -> %04.0f-%02.0f-%02.0fT%02.0f:%02.0f:%02.0f.%03.0f%03.0f%03.0f\n",
		tt, yr, mt, dy, hr, mn, sc, ms, us, ns
	);
}

char* tt2000_isoc(char* sBuf, size_t uLen, long long tt)
{
	double yr, mt, dy, hr, mn, sc, ms, us, ns;
	
	/* Don't use this function in application code, since it's a var
	   arg function without a template, it's too easy to provide the
		wrong data type!  It's used here for testing, but go with:
		
		das_time dt;
		Units_convertToDt(&dt, tt, UNIT_TT2000);
		
		instead.
	*/
	das_tt2K_to_utc(tt, &yr, &mt, &dy, &hr, &mn, &sc, &ms, &us, &ns);
	snprintf(sBuf, uLen, 
		"%04.0f-%02.0f-%02.0fT%02.0f:%02.0f:%02.0f.%03.0f%03.0f%03.0f",
		yr, mt, dy, hr, mn, sc, ms, us, ns
	);	
	return sBuf;
}

void prnTt2000(
	double yr, double mt, double dy, double hr, double mn, double sc, 
	double ms, double us, double ns
){
	long long tt = das_utc_to_tt2K(yr, mt, dy, hr, mn, sc, ms, us, ns);
	fprintf(stdout, 
		"%04.0f-%02.0f-%02.0fT%02.0f:%02.0f:%02.0f.%03.0f%03.0f%03.0f -> %18lldLL\n",
		yr, mt, dy, hr, mn, sc, ms, us, ns, tt
	);
}

char* infoTT2000(char* sBuf, size_t uLen,
	double yr, double mt, double dy, double hr, double mn, double sc, 
	double ms, double us, double ns
){
	long long tt = das_utc_to_tt2K(yr, mt, dy, hr, mn, sc, ms, us, ns);
	snprintf(sBuf, uLen,
		"%04.0f-%02.0f-%02.0fT%02.0f:%02.0f:%02.0f.%03.0f%03.0f%03.0f -> %.11e",
		yr, mt, dy, hr, mn, sc, ms, us, ns, (double)tt
	);
	return sBuf;
}


struct map_t {
	double us2000;
	double TT2000;
	double yr, mt, dy;
	double hr, mn, sc;
	double ms, us, ns;
};

int main(int argc, char** argv)
{
	int i; /* always need one of these */
	double dTT2K, dUS2K;
	char sBuf[64] = {'\0'};
	char sBuf2[64] = {'\0'};
	
	/* Remove leap seconds file from environment */
	unsetenv("CDF_LEAPSECONDSTABLE");
	
	/* Exit on errors, log info messages and above */
	das_init(argv[0], DASERR_DIS_EXIT, 0, DASLOG_INFO, NULL);
	
	/* Extra demonstration of zero points if desired */
	if((argc > 1)&&(strcmp(argv[1], "-h") != 0)){
		
		prnUtc(0LL);
		prnUtc(114198895034999000LL);
		prnUtc(114198941235000000LL);
		
		prnTt2000(2000, 1, 1, 11, 58, 55, 816,   0, 0);
		prnTt2000(2003, 8, 15, 5, 53, 50, 850, 999, 0);
		prnTt2000(2003, 8, 15, 5, 54, 37,  51,   0, 0);
		prnTt2000(2000, 1,  1, 0,  0,  0,   0,   0, 0);
	}
	
	/* Verify bi-directional conversion of TT2K to US2K */
	
	struct map_t aBoth[] = {
		
	/*            us2000,              TT2000                             */
	/*          +-------------------+- seconds place                      */
	/*          |                   |                                     */
	/*          V                   V                                     */
	{   -3.1536001000e13,  -3.1579137816e+16, 1998,12,31,23,59,59,  0, 0, 0},
	{   -3.1536000000e13,  -3.1579135816e+16, 1999, 1, 1, 0, 0, 0,  0, 0, 0},
   {   -3.1535999000e13,  -3.1579134816e+16, 1999, 1, 1, 0, 0, 1,  0, 0, 0},
					  			 
	{ -8.67888001000e+14, -8.67931158816e+17, 1972, 6,30,23,59,59,  0, 0, 0},
	{ -8.67888000000e+14, -8.67931156816e+17, 1972, 7, 1, 0, 0, 0,  0, 0, 0},
	{ -8.67887999000e+14, -8.67931155816e+17, 1972, 7, 1, 0, 0, 1,  0, 0, 0},
	
	{                0.0,     -4.3135816e+13, 2000, 1, 1, 0, 0, 0,  0, 0, 0},
	{      4.3135816e+10,                0.0, 2000, 1, 1,11,58,55,816, 0, 0},
	
	{  4.89023999000e+14,  4.88980866184e+17, 2015, 6,30,23,59,59,  0, 0, 0},
	{  4.89024000000e+14,  4.88980868184e+17, 2015, 7, 1, 0, 0, 0,  0, 0, 0},
	{  4.89024001000e+14,  4.88980869184e+17, 2015, 7, 1, 0, 0, 1,  0, 0, 0}
	
	};
	
	const char* sTmp = "ERROR: Test 1, bi-directional mapping failed";
	das_time dtTmp = {0};
	struct map_t a;
	bool bFail = false;
	for(i = 0; i < 9; ++i){
		a = aBoth[i];
		dTT2K = das_us2K_to_tt2K(a.us2000);
		dUS2K = das_tt2K_to_us2K(a.TT2000);
		
		if(dTT2K != a.TT2000){
			printf("%s, calc: %.11e, expect: %.11e, diff: %.3f (sec)\n", 
					 sTmp, dTT2K, a.TT2000, (a.TT2000 - dTT2K)*1e-9);
			
			infoTT2000(sBuf, 63, a.yr,a.mt,a.dy,a.hr,a.mn,a.sc,a.ms,a.us,a.ns);
			printf("%s, Direct conversion: %s\n\n", sTmp, sBuf);
			bFail = true;
		}
		
		if(dUS2K != a.us2000){
			printf("%s, calc: %.11e, expect: %.11e, diff: %.3f (sec)\n", 
					 sTmp, dUS2K, a.us2000, (a.us2000 - dUS2K)*1e-6);
			
			Units_convertToDt(&dtTmp, a.us2000, UNIT_US2000);
			dt_isoc(sBuf, 63, &dtTmp, 3);
			
			printf("%s, Direct conversion: %s\n\n", sTmp, sBuf);
			bFail = true;
		}
	}
	if(bFail) return 13;
	
	
	/* Extra demonstration of mapping if desired */
	if((argc > 1)&&(strcmp(argv[1], "-h") != 0)){
		double dTmp[] = {
			
			/* pass through 1999-01-01 leap second */
			-3.1579138816e+16, -3.1579137816e+16, -3.1579136816e+16, 
			-3.1579135816e+16, -3.1579134816e+16, -3.1579133816e+16,
					
			/* pass through 1972-07-01 leap second (2nd regular one)*/
			-8.67931159816e+17, -8.67931158816e+17, -8.67931157816e+17,
			-8.67931156816e+17, -8.67931155816e+17, -8.67931154816e+17			
		};
		
		printf("          TT2000,           us2000,  UTC                 , reverse\n");
		for(i = 0; i < 12; ++i){
			dUS2K = das_tt2K_to_us2K(dTmp[i]);
			printf(
				"%.10e  %.10e  %s  %.10e\n", dTmp[i], dUS2K,
				tt2000_isoc(sBuf, 63, dTmp[i]), das_us2K_to_tt2K(dUS2K)
			);
			if((i+1)%6 == 0) printf("\n");
		}
	}
	
	/* Verify double mapping of values to single US2K second */
	double aToUs2K[][3] = {
		
		/* 1998-12-31T23:59:59 &  1998-12-31T23:59:60 */
		{  -3.1579137816e+16,  -3.1579136816e+16,  -3.15360010e13},
		
		/* 1972-06-30T12:59:59.000 & 12:59:60.000 */
		{ -8.67931158816e+17, -8.67931157816e+17, -8.67888001e+14, }, 
		
		/* 2015-06-30T12:59:59.000 & 12:59:60.000 */
		{  4.88980866184e+17,  4.88980867184e+17,  4.89023999e+14}
	};
	
	
	sTmp = "ERROR: Test 2, double value mapping failed";
	for(i = 0; i < 2; ++i){
		dUS2K = das_tt2K_to_us2K(aToUs2K[i][0]);
		
		if(dUS2K != aToUs2K[i][2]){
			printf("%s, %.8e != %.8e (pre-leap)\n", sTmp, dUS2K, aToUs2K[i][2]);
			return 13;
		}
		
		dUS2K = das_tt2K_to_us2K(aToUs2K[i][1]);
		if(dUS2K != aToUs2K[i][2]){
			printf("%s, %.8e != %.8e (on-leap)\n", sTmp, dUS2K, aToUs2K[i][2]);
			return 13;
		}
	}
		
	/* Demonstrate pre-2000 missing second on us2000 scale */
	das_time dtPre;  dt_set(&dtPre,  1976, 12, 31,  366, 23, 59, 59.0);
	das_time dtPost; dt_set(&dtPost, 1977,  1,  1,    1,  0,  0,  0.0);
	double rNoLeapPre  = Units_convertFromDt(UNIT_US2000, &dtPre);
	double rNoLeapPost = Units_convertFromDt(UNIT_US2000, &dtPost);
	double rLeapPre    = Units_convertFromDt(UNIT_TT2000, &dtPre);
	double rLeapPost   = Units_convertFromDt(UNIT_TT2000, &dtPost);

	/* Check tt2000 and us2000 differences across leap seconds */
	if( (rNoLeapPost - rNoLeapPre) != 1e6){
		printf("ERROR: Test 3 failed, microseconds since 2000 keeps leap "
				 "second (diff %.3e)\n", rNoLeapPost - rNoLeapPre);
		return 13;
	}
	
	if( rLeapPost - rLeapPre != 2e9){
		printf("ERROR: Test 4 failed, TT2000 dropped leap second, "
		       " (pre: %.11e  post: %.11e  diff %.3e)\n", 
				rLeapPre, rLeapPost, rLeapPost - rLeapPre);
		return 13;
	}
	
	/* Now do it again for positive values */
	das_time dt_preleap = {2016, 12, 31, 0, 23, 59, 59};
	das_time dt_postleap = {2017,  1,  1, 0};
	double rPreLeap = Units_convertFromDt(UNIT_TT2000, &dt_preleap);
	double rPostLeap = Units_convertFromDt(UNIT_TT2000, &dt_postleap);
	if( abs((rPostLeap - rPreLeap) - 2e9) > 1e-9) {
		printf("ERROR: Test 5 Failed, TT2000 difference was %e s, expected ~2.0 s "
		       "across leapsecond boundary.\n", (rPostLeap - rPreLeap)*1e-9);
		return 15;
	}
	
	/* Test loading a Leapseconds file, okay since single threaded. */
	/* This one has a fake leap second at the end of 2020           */
	setenv("CDF_LEAPSECONDSTABLE", "./test/FakeLeapSeconds.txt", true);
	
	if(! das_tt2k_reinit(argv[0]) ){
		printf("ERROR: Test 6 failed, couldn't re-initialize leap-second table\n");
	}
	
	
	/* Make sure seconds == 60 is handled correctly by das_time to tt2000 */
	das_time dtLeap1; dt_set(&dtLeap1, 2020, 12, 31, 366, 23, 59, 60.0);
	double rLeap = Units_convertFromDt(UNIT_TT2000, &dtLeap1);
	das_time dtLeap2;
	Units_convertToDt(&dtLeap2, rLeap, UNIT_TT2000);
	if( dtLeap1.second != dtLeap2.second){
		printf(
			"ERROR: Test 7 failed, round-trip to das_time did not preserve leap "
			"second, pre %s, post %s, diff %.5e.\n", dt_isoc(sBuf, 63, &dtLeap1,3),
			dt_isoc(sBuf2, 63, &dtLeap2, 3), dtLeap2.second - dtLeap1.second
				);
		return 13;
	}
		
	/* Test the fake leap second at 2021-01-01 */
	dt_set(&dtPre, 2020, 12, 31,  366, 23, 59, 59.0);
	dt_set(&dtPost, 2021,  1,  1,    1,  0,  0,  1.0);
	rLeapPre  = Units_convertFromDt(UNIT_TT2000, &dtPre);
	rLeapPost = Units_convertFromDt(UNIT_TT2000, &dtPost);
	
	if(rLeapPost - rLeapPre != 3e9){
		printf("ERROR: Test 8 failed, time calculations not altered by external table\n");
		return 13;
	}
	
	printf("INFO: All TT2000 tests passed\n");
	return 0;
}
