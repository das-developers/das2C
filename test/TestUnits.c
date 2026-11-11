/** @file TestUnits.c Unit tests for scientific unit handling */

/* Author: Chris Piker <chris-piker@uiowa.edu>
 *
 * This file is intended to demonstrate an interface.  This is free
 * and unencumbered software released into the public domain
 *
 * Anyone is free to copy, modify, publish, use, compile, sell, or
 * distribute this file, either in source code form or as a compiled
 * binary, for any purpose, commercial or non-commercial, and by any
 * means.
 *
 * In jurisdictions that recognize copyright laws, the author or authors
 * of this file dedicate any and all copyright interest in this file to 
 * the public domain. We make this dedication for the benefit of the
 * public at large and to the detriment of our heirs and successors. We
 * intend this dedication to be an overt act of relinquishment in
 * perpetuity of all present and future rights to this file under
 * copyright law.
 *
 * THIS FILE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.
 *
 * For more information, please refer to <http://unlicense.org/>
 */

#define _POSIX_C_SOURCE 200112L

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <limits.h>

#include <das3/core.h>

int main(int argc, char** argv) {

	/* Exit on errors, log info messages and above */
	daslib_init(argv[0], DASERRDISP_EXIT, DASLOG_INFO, NULL);

	/* Test singleton nature of unit values */
	DasUnits Hz1 = DasUnits_fromStr("Hz");
	char sHz2[20] = {'\0'};  strcpy(sHz2, "Hz");
	DasUnits Hz2 = DasUnits_fromStr(sHz2);
	if( (Hz1 != Hz2) || (Hz2 != DASUNITS_HERTZ)){
		printf("ERROR: Test 1 Failed, %s != %s\n", Hz1, Hz2);
		return 15;
	}
	
	/* Test US2000 forward transformations */
	DasUnits units = DASUNITS_US2000;
	const char* sTime = "2000-1-1T1:00";
	DasTime dt = DASTIME_NULL;
	if(!DasTime_fromStr(&dt, sTime)){
		printf("ERROR: Test 2 Failed, can't parse %s as a string\n", sTime);
		return 15;
	}

	/* For the French ;-) test times with ','' for the radix */
	sTime = "2000-01-01T01:00:00,000001";
	DasTime_clear(&dt);
	if(!DasTime_fromStr(&dt, sTime)){
		printf("ERROR: Test 3 Failed, can't parse %s as a string\n", sTime);
		return 15;
	}
	if(dt.second != 0.000001){
		printf("ERROR: Test 4 Failed, can't parse subseconds in %s\n", sTime);
		return 15;	
	}

	double rTime = DasTime_toPoint(&dt, units);
	if( rTime != 3600000001.0){
		printf("ERROR: Test 5 Failed, %s (%f) != 3600000001.0 μs since 2000-01-01\n",
				sTime, rTime);
		return 15;
	}
	double ssm = DasTime_pointToSod(rTime, units);
	if(ssm != 3600.000001 ){
		printf("ERROR: Test 6 Failed, %f US2000 is not %f seconds since midnight\n", 
				 rTime, ssm);
		return 15;
	}
	int jd = DasTime_pointToJdn(rTime, units);
	if(jd != 2451545){
		printf("ERROR: Test 7 Failed, %s is not jullian day %d\n", sTime, jd);
		return 15;
	}

	/* Test US2000 backward transformations */
	DasTime_fromPoint(&dt, rTime, units);
	char sBuf[64] = {'\0'};
	DasTime_toIsoC(&dt, 0, sBuf, 64);
	if( strcmp(sBuf, "2000-01-01T01:00:00") != 0){
		printf("ERROR: Test 8 Failed, %s != %f US2000\n", sBuf, rTime);
		return 15;
	}

	/* Test MJ1958 units forward transformations */
	
	units = DASUNITS_MJ1958;
	sTime = "2000-001T01:00";
	if(!DasTime_fromStr(&dt, sTime)){
		printf("ERROR: Test 9 Failed, can't parse %s as a string\n", sTime);
		return 15;
	}	
	rTime = DasTime_toPoint(&dt, units);
	if( rTime != 15340.041666666666){
		printf("ERROR: Test 10 Failed, %s != %f MJ1958\n", sTime, rTime);
		return 15;
	}

	sTime = "1958-01-01T13:00";
	if(!DasTime_fromStr(&dt, sTime)){
		printf("ERROR: Test 11 Failed, can't parse %s as a string\n", sTime);
		return 15;
	}	
	rTime = DasTime_toPoint(&dt, units);
	if( rTime != 0.5416666666666666){
		printf("ERROR: Test 12 Failed, %s != %f MJ1958\n", sTime, rTime);
		return 15;
	}
	
	/* Test conversion from MJ1958 to US2000 */
	double rUs2000 = DasUnits_convert(rTime, units, DASUNITS_US2000);
	if( rUs2000 != -1325329200000000.0){ 
		printf("ERROR: Test 13 Failed, %f MJ1958 != %f US2000\n", rTime, rUs2000);
		return 15;
	}
	
	/* Test SSM and Julian Day */
	double rSsm = DasTime_pointToSod(rTime, units);
	int nJd  = DasTime_pointToJdn(rTime, units);

	if( rSsm != 46800){
		printf("ERROR: Test 14 Failed, %f MJ1958 is not %f seconds since midnight\n",
				 rTime, rSsm);
		return 15;
	}
	if( nJd != 2436205 ){
		printf("ERROR: Test 15 Failed, %f MJ1958 is not %d Julian days\n", rTime, nJd);
		return 15;
	}
	
	/* Test MJ1958 backward transformation */
	rTime = 0.541667;
	DasTime dt1 = {0};
	DasTime_fromPoint(&dt1, rTime, units);
	DasTime_toIsoD(&dt1, 0, sBuf, 64);
	if( strcmp(sBuf, "1958-001T13:00:00") != 0){
		printf("ERROR: Test 16 Failed, %f MJ1958 is not %s UTC\n", rTime, sBuf);
		return 15;
	}

	/* Test ns1970 conversions (used by numpy) */
	int64_t nTime = 1451606400000000000LL; /* 2016-01-01 */
	rTime = (double)nTime;
	DasTime_clear(&dt1);
	units = DasUnits_fromStr("ns1970");
	DasTime_fromPoint(&dt1, rTime, units);
	DasTime_toIsoC(&dt1, 3, sBuf, 64);
	if( strcmp(sBuf, "2016-01-01T00:00:00.000") != 0){
		printf("ERROR: Test 17 Failed, %f ns1970 is not %s UTC\n", rTime, sBuf);
		return 15;
	}

	/* Do it again for negative years */
	nTime = -1767225600000001000LL; /* almost 1914-01-01 */
	rTime = (double)nTime;
	DasTime_clear(&dt1);
	units = DasUnits_fromStr("ns1970");
	DasTime_fromPoint(&dt1, rTime, units);
	DasTime_toIsoC(&dt1, 6, sBuf, 64);
	if( strcmp(sBuf, "1913-12-31T23:59:59.999999") != 0){
		printf("ERROR: Test 18 Failed, %f ns1970 is not %s UTC\n", rTime, sBuf);
		return 15;
	}

	/* Test basic string parsing into canonical representation without
	   unit reduction */
	DasUnits a = DasUnits_fromStr("V/m");
	DasUnits b = DasUnits_fromStr("V m^-1");
	DasUnits c = DasUnits_fromStr("V m**-2/2"); /*<-- don't use this, but it does work */
	
	if( a != b ){ printf("ERROR: Test 19 Failed, '%s' != '%s' \n", a, b); return 15; }
	if( a != c ){ printf("ERROR: Test 20 Failed, '%s' != '%s' \n", a, c); return 15; }
	
	
	/* Test unit inversion */
	DasUnits d = DasUnits_fromStr("m V**-1");
	DasUnits e = DasUnits_invert(a);
	
	if( d != e ){ printf("ERROR: Test 21 Failed, '%s' != '%s' \n", d, e); return 15; }
	
	/* Test unit raise to power */
	DasUnits f = DasUnits_fromStr("V**2 m**-2");
	DasUnits g = DasUnits_pow(a, 2);	
	
	if( f != g ){ printf("ERROR: Test 22 Failed, '%s' != '%s' \n", f, g); return 15; }
	
	/* Test unit multiplication */
	DasUnits h = DASUNITS_E_SPECDENS;
	
	DasUnits i = DasUnits_mul( DasUnits_pow(a, 2), DasUnits_pow(DASUNITS_HERTZ, -1));
	
	if( h != i ){ printf("ERROR: Test 23 Failed, '%s' != '%s' \n", h, i); return 15; }
	
	/* Test interval units for t2000 */
	DasUnits j = DasUnits_toInterval(DASUNITS_T2000);
	DasUnits k = DasUnits_invert( DasUnits_fromStr("Hertz") );
	if( j != k ){ printf("ERROR: Test 24 Failed, '%s' != '%s' \n", j, k); return 15; }
	
	/* Test interval units for us2000 */
	DasUnits l = DasUnits_toInterval(DASUNITS_US2000);
	DasUnits m = DasUnits_invert( DasUnits_fromStr("MHz") );
	if( l != m ){ printf("ERROR: Test 25 Failed, '%s' != '%s' \n", l, m); return 15; }
	
	
	/* Test unit conversions */
	DasUnits ms = DasUnits_fromStr("microsecond");
	DasUnits delta = DasUnits_invert( ms );
	double rFactor = DasUnits_convert(1.0, delta, DASUNITS_HERTZ);
	if( rFactor != 1.0e+6){ 
		printf("ERROR: Test 26 Failed, '%s' to '%s' factor = %.1e, expected 1.0e+06\n", 
				 delta, DASUNITS_HERTZ, rFactor);
		return 15;
	}
	
	DasUnits perday = DasUnits_fromStr("kilodonut/day");
	DasUnits persec = DasUnits_fromStr("donut hertz");
	
	double rTo = DasUnits_convert(86.4, perday, persec);
	if( rTo != 1.0){
		printf("ERROR: Test 27 Failed, 86.4 %s is %.4f %s, expected 1.0\n", perday,
				 rTo, persec);
		return 15;
	}
	
	/* Test unit reduction, not done implicitly because that would mess up
	 * people's intended output, but it is needed for the convertTo() function
	 * to work properly.  Reduction collapses all units to basic types with no
	 * SI prefixes and then returns a factor that can be used to adjust values
	 * in the non-reduced units to the reduced representation */
	DasUnits O = DasUnits_fromStr("ohms");
	DasUnits O_reduced = DasUnits_reduce(O, &rFactor);
	DasUnits muO = DasUnits_fromStr("μΩ");
	
	DasUnits muO_reduced = DasUnits_reduce(muO, &rFactor);
	if(O_reduced != muO_reduced ){
		printf("ERROR: Test 28 Failed, %s != %s\n", O_reduced, muO_reduced);
		return 15;
	}
	if(rFactor != 1.0e-6){
		printf("ERROR: Test 29 Failed, 1.0 %s != %.1e %s\n", muO, rFactor, 
				 muO_reduced);
		return 15;
	}
	
	/* Test reduction part two, avoid false positive match on SI prefix */
	DasUnits cats = DasUnits_fromStr("cats");  /* <-- looks like centi "ats" */
	DasUnits cats_reduced = DasUnits_reduce(cats, &rFactor);
	if(cats != cats_reduced){
		printf("ERROR: Test 30 failed, %s != %s\n", cats, cats_reduced);
		return 15;
	}
	
	/* Test unicode decomposition for the special characters μ and Ω */
	DasUnits bad_microohms  = DasUnits_fromStr("µΩ m^-1"); /* Depending on the font in your  */
	DasUnits good_microohms = DasUnits_fromStr("μΩ m^-1"); /* editor you might not the       */
	                                                    /* difference, but it's there and */
	                                                    /* das2C handles it.              */
	if( bad_microohms != good_microohms){
		printf("ERROR: Test 31 Failed, decomposition failed %s != %s\n", bad_microohms,
		       good_microohms);
		return 15;
	}
	
	/* Test order preservation for unknown units.  Assume that the user wants
	 * units in the given order unless otherwise specified. */
	const char* sUnits = "cm**-2 keV**-1 s**-1 sr**-1";
	DasUnits flux = DasUnits_fromStr(sUnits);
	
	if( strcmp( sUnits, flux) != 0){
		printf("ERROR: Test 32 Failed, unknown units are re-arranged by default. %s != %s\n",
				 sUnits, flux);
		return 15;
	}
	
	/* New unit strings are sticky.  Test that a new variation of the new units
	 * defined above reuses the first definition */
	const char* sSameUnits = "hertz / kiloelectronvolt / centimeters^2 / steradian";
	DasUnits flux2 = DasUnits_fromStr(sSameUnits);
	if( flux2 != flux){
		printf("ERROR: Test 33 Failed, repeated unknown units are not normalized to "
				"first instance, %s != %s\n", flux2, flux);
		return 15;
	}
	
	/* Test that weird unit strings don't crash the program */
	
	/* from Aspera reader... */
	sUnits = "eV/(cm**-2 s**1 sr**1 eV**1)";
	DasUnits energy_flux = DasUnits_fromStr(sUnits);
	DasUnits test_e_flux = DasUnits_fromStr("m**2 s**-1 sr**-1");
	DasUnits reduced_flux = DasUnits_reduce(energy_flux, &rFactor);
	
	if( reduced_flux != test_e_flux){
		printf("ERROR: Test 34 Failed, eV did not cancel: %s (expected %s)\n",
				 reduced_flux, test_e_flux);
		return 15;
	}
	
	/* from Cassini density reader ... */
	sUnits = "electrons / cm ^ 3";
	DasUnits num_dens1 = DasUnits_fromStr(sUnits);
	DasUnits num_dens2 = DasUnits_fromStr("electrons cm**-3");
	if( num_dens1 != num_dens2){
		printf("ERROR: Test 35 Failed, %s != %s\n", num_dens1, num_dens2);
		return 15;
	}

	/* Odd one inspired by Tracers/Magic, milli percent per meter*/
	DasUnits milli_percent = DasUnits_fromStr("m%");
	DasUnits meter = DasUnits_fromStr("m");
	DasUnits milli_per2 = DasUnits_mul(milli_percent, DasUnits_invert(meter));

	rFactor = 0.0;
	DasUnits milli_per3 = DasUnits_reduce( DasUnits_fromStr("milli%/m"), &rFactor);

	if(rFactor != 0.001 ){
		printf("ERROR: Test 36 Failed, %s is not 1/1000 of %s\n", milli_per2, milli_per3);
		return 15;
	}

	/* Test canConvert when NULL args turn up */
	DasUnits_canConvert(DASUNITS_US2000, NULL);
	DasUnits_canConvert(NULL, DASUNITS_HERTZ);
	DasUnits_canConvert(NULL, NULL);

	/* Test 37: parsetime tells a padded 4-digit year from a 2-digit one by digit
	   count, so small explicit years round-trip.  "0001-01-01" used to come back
	   as the current year (the value 1 fell through the magnitude heuristic and
	   the year kept its now() default); a 2-digit "97" must still get the +1900
	   century fixup. */
	{
		DasTime dt;

		DasTime_clear(&dt);
		if(!DasTime_fromStr(&dt, "0001-01-01T00:00:00.000000")){
			printf("ERROR: Test 37 Failed, could not parse 0001-01-01\n");
			return 15;
		}
		if((dt.year != 1) || (dt.month != 1) || (dt.mday != 1)){
			printf("ERROR: Test 37 Failed, 0001-01-01 parsed as %04d-%02d-%02d "
			       "(4-digit year not honored)\n", dt.year, dt.month, dt.mday);
			return 15;
		}

		DasTime_clear(&dt);
		if(!DasTime_fromStr(&dt, "0050-03-15") || (dt.year != 50)){
			printf("ERROR: Test 37 Failed, 0050-03-15 year parsed as %04d, expected "
			       "literal 50 (a 4-digit year must not get the century fixup)\n", dt.year);
			return 15;
		}

		DasTime_clear(&dt);
		if(!DasTime_fromStr(&dt, "97-05-07") || (dt.year != 1997)){
			printf("ERROR: Test 37 Failed, 2-digit year 97 parsed as %04d, expected "
			       "1997 (century fixup lost)\n", dt.year);
			return 15;
		}
	}

	/* Test 38: a base unit whose exponent comes to zero drops out of the
	   result, and when nothing is left the result is DASUNITS_DIMENSIONLESS.  On
	   input a zero exponent is accepted and ignored.  Exponents may contain
	   the digit zero. */
	{
		DasUnits V_m = DasUnits_fromStr("V/m");
		DasUnits mtr = DasUnits_fromStr("m");
		DasUnits vlt = DasUnits_fromStr("V");
		DasUnits s10 = DasUnits_fromStr("s**10");

		struct { const char* sWhat; DasUnits got; DasUnits expect; } aChk[] = {
			{"V/m divided by V/m", DasUnits_div(V_m, V_m), DASUNITS_DIMENSIONLESS},
			{"m times 1/m", DasUnits_mul(mtr, DasUnits_invert(mtr)), DASUNITS_DIMENSIONLESS},
			{"V/m times m", DasUnits_mul(V_m, mtr), vlt},
			{"V/m to the power 0", DasUnits_pow(V_m, 0), DASUNITS_DIMENSIONLESS},
			{"m**2 squared", DasUnits_pow(DasUnits_fromStr("m**2"), 2), DasUnits_fromStr("m**4")},
			{"square root of m**2", DasUnits_root(DasUnits_fromStr("m**2"), 2), mtr},
			{"parse of 'V**0 m'", DasUnits_fromStr("V**0 m"), mtr},
			{"parse of 'V**0'", DasUnits_fromStr("V**0"), DASUNITS_DIMENSIONLESS},
			{"s**10 over s**9", DasUnits_div(s10, DasUnits_fromStr("s**9")), DASUNITS_SECONDS},
		};
		int nBad = 0;
		for(size_t u = 0; u < sizeof(aChk)/sizeof(aChk[0]); ++u){
			if(aChk[u].got != aChk[u].expect){
				printf("ERROR: Test 38 Failed, %s gave '%s', expected '%s'\n",
					aChk[u].sWhat, aChk[u].got ? aChk[u].got : "(null)", aChk[u].expect
				);
				++nBad;
			}
		}
		if(nBad > 0) return 15;
	}

	/* DasTime_isInRange() takes the time tested first, then the range.  All three are
	   DasTime*, so a call left in the old (begin, end, test) order would
	   compile; these four answers would not all survive it. */
	{
		DasTime dtBeg, dtEnd, dtIn, dtOut;
		DasTime_fromStr(&dtBeg, "2026-01-01");
		DasTime_fromStr(&dtEnd, "2026-02-01");
		DasTime_fromStr(&dtIn,  "2026-01-15");
		DasTime_fromStr(&dtOut, "2026-03-01");
		if(!DasTime_isInRange(&dtIn, &dtBeg, &dtEnd) || DasTime_isInRange(&dtOut, &dtBeg, &dtEnd)
		   || !DasTime_isInRange(&dtBeg, &dtBeg, &dtEnd) || DasTime_isInRange(&dtEnd, &dtBeg, &dtEnd)){
			printf("ERROR: DasTime_isInRange(test, begin, end) gave a wrong answer\n");
			return 15;
		}
	}

	printf("INFO: All unit manipulation tests passed\n\n");
	return 0;
}

