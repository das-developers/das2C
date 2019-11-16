/** @file TestUnits.c Unit tests for scientific unit handling */

/* Copyright 2004-2017 Chris Piker <chris-piker@uiowa.edu>
 *                     Jeremy Faden <jeremy-faden@uiowa.edu>
 * 
 * Licensed under the open source Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License. You may 
 * obtain a copy of the License at
 *
 *  http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#define _POSIX_C_SOURCE 200112L

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <limits.h>

#include <das2/core.h>

int main(int argc, char** argv) {

	/* Exit on errors, log info messages and above */
	das_init(argv[0], DASERR_DIS_EXIT, 0, DASLOG_INFO, NULL);
	
	/* Test singleton nature of unit values */
	das_units Hz1 = Units_fromStr("Hz");
	char sHz2[20] = {'\0'};  strcpy(sHz2, "Hz");
	das_units Hz2 = Units_fromStr(sHz2);
	if( (Hz1 != Hz2) || (Hz2 != UNIT_HERTZ)){
		printf("ERROR: Test 1 Failed, %s != %s\n", Hz1, Hz2);
		return 15;
	}
	
	/* Test US2000 forward transformations */
	das_units units = UNIT_US2000;
	const char* sTime = "2000-1-1T1:00";
	das_time dt = {0};
	if(!dt_parsetime(sTime, &dt)){
		printf("ERROR: Test 2 Failed, can't parse %s as a string\n", sTime);
		return 15;
	}	
	double rTime = Units_convertFromDt(units, &dt);
	if( rTime != 3600000000.0){
		printf("ERROR: Test 2 Failed, %s != %f μs since 2000-01-01\n",
				sTime, rTime);
		return 15;
	}
	double ssm = Units_secondsSinceMidnight(rTime, units);
	if(ssm != 3600.0 ){
		printf("ERROR: Test 2 Failed, %f US2000 is not %f seconds since midnight\n", 
				 rTime, ssm);
		return 15;
	}
	int jd = Units_getJulianDay(rTime, units);
	if(jd != 2451545){
		printf("ERROR: Test 2 Failed, %s is not jullian day %d\n", sTime, jd);
		return 15;
	}

	/* Test US2000 backward transformations */
	Units_convertToDt(&dt, rTime, units);
	char sBuf[64] = {'\0'};
	dt_isoc(sBuf, 64, &dt, 0);
	if( strcmp(sBuf, "2000-01-01T01:00:00") != 0){
		printf("ERROR: Test 3 Failed, %s != %f US2000\n", sBuf, rTime);
		return 15;
	}

	/* Test MJ1958 units forward transformations */
	
	units = UNIT_MJ1958;
	sTime = "2000-001T01:00";
	if(!dt_parsetime(sTime, &dt)){
		printf("ERROR: Test 4 Failed, can't parse %s as a string\n", sTime);
		return 15;
	}	
	rTime = Units_convertFromDt(units, &dt);
	if( rTime != 15340.041666666666){
		printf("ERROR: Test 4 Failed, %s != %f MJ1958\n", sTime, rTime);
		return 15;
	}

	sTime = "1958-01-01T13:00";
	if(!dt_parsetime(sTime, &dt)){
		printf("ERROR: Test 5 Failed, can't parse %s as a string\n", sTime);
		return 15;
	}	
	rTime = Units_convertFromDt(units, &dt);
	if( rTime != 0.5416666666666666){
		printf("ERROR: Test 5 Failed, %s != %f MJ1958\n", sTime, rTime);
		return 15;
	}
	
	/* Test conversion form MJ1958 to US2000 */
	double rUs2000 = Units_convertTo(UNIT_US2000, rTime, units);
	if( rUs2000 != -1325329200000000.0){ 
		printf("ERROR: Test 6 Failed, %f MJ1958 != %f US2000\n", rTime, rUs2000);
		return 15;
	}
	
	/* Test SSM and Julian Day */
	double rSsm = Units_secondsSinceMidnight(rTime, units);
	int nJd  = Units_getJulianDay(rTime, units);

	if( rSsm != 46800){
		printf("ERROR: Test 7 Failed, %f MJ1958 is not %f seconds since midnight\n",
				 rTime, rSsm);
		return 15;
	}
	if( nJd != 2436205 ){
		printf("ERROR: Test 7 Failed, %f MJ1958 is not %d Julian days\n", rTime, nJd);
		return 15;
	}
	
	
	/* Test MJ1958 backward transformation */
	rTime = 0.541667;
	das_time dt1 = {0};
	Units_convertToDt(&dt1, rTime, units);
	dt_isod(sBuf, 64, &dt1, 0);
	if( strcmp(sBuf, "1958-001T13:00:00") != 0){
		printf("ERROR: Test 8 Failed, %f MJ1958 is not %s UTC\n", rTime, sBuf);
		return 15;
	}
	
	/* Test basic string parsing into canonical representation without
	   unit reduction */
	das_units a = Units_fromStr("V/m");
	das_units b = Units_fromStr("V m^-1");
	das_units c = Units_fromStr("V m**-2/2"); /*<-- don't use this, but it does work */
	
	if( a != b ){ printf("ERROR: Test 8 Failed, '%s' != '%s' \n", a, b); return 15; }
	if( a != c ){ printf("ERROR: Test 8 Failed, '%s' != '%s' \n", a, c); return 15; }
	
	
	/* Test unit inversion */
	das_units d = Units_fromStr("m V**-1");
	das_units e = Units_invert(a);
	
	if( d != e ){ printf("ERROR: Test 9 Failed, '%s' != '%s' \n", d, e); return 15; }
	
	/* Test unit raise to power */
	das_units f = Units_fromStr("V**2 m**-2");
	das_units g = Units_power(a, 2);	
	
	if( f != g ){ printf("ERROR: Test 10 Failed, '%s' != '%s' \n", f, g); return 15; }
	
	/* Test unit multiplication */
	das_units h = UNIT_E_SPECDENS;
	
	das_units i = Units_multiply( Units_power(a, 2), Units_power(UNIT_HERTZ, -1));
	
	if( h != i ){ printf("ERROR: Test 11 Failed, '%s' != '%s' \n", h, i); return 15; }
	
	/* Test interval units for t2000 */
	das_units j = Units_interval(UNIT_T2000);
	das_units k = Units_invert( Units_fromStr("Hertz") );
	if( j != k ){ printf("ERROR: Test 12 Failed, '%s' != '%s' \n", j, k); return 15; }
	
	/* Test interval units for us2000 */
	das_units l = Units_interval(UNIT_US2000);
	das_units m = Units_invert( Units_fromStr("MHz") );
	if( l != m ){ printf("ERROR: Test 13 Failed, '%s' != '%s' \n", l, m); return 15; }
	
	
	/* Test unit conversions */
	das_units ms = Units_fromStr("microsecond");
	das_units delta = Units_invert( ms );
	double rFactor = Units_convertTo(UNIT_HERTZ, 1.0, delta);
	if( rFactor != 1.0e+6){ 
		printf("ERROR: Test 14 Failed, '%s' to '%s' factor = %.1e, expected 1.0e+06\n", 
				 delta, UNIT_HERTZ, rFactor);
		return 15;
	}
	
	das_units perday = Units_fromStr("kilodonut/day");
	das_units persec = Units_fromStr("donut hertz");
	
	double rTo = Units_convertTo(persec, 86.4, perday);
	if( rTo != 1.0){
		printf("ERROR: Test 15 Failed, 86.4 %s is %.4f %s, expected 1.0\n", perday,
				 rTo, persec);
		return 15;
	}
	
	/* Test unit reduction, not done implicitly because that would mess up
	 * people's intended output, but it is needed for the convertTo() function
	 * to work properly.  Reduction collapses all units to basic types with no
	 * SI prefixes and then returns a factor that can be used to adjust values
	 * in the non-reduced units to the reduced representation */
	das_units O = Units_fromStr("ohms");
	das_units O_reduced = Units_reduce(O, &rFactor);
	das_units muO = Units_fromStr("μΩ");
	
	das_units muO_reduced = Units_reduce(muO, &rFactor);
	if(O_reduced != muO_reduced ){
		printf("ERROR: Test 16 Failed, %s != %s\n", O_reduced, muO_reduced);
		return 15;
	}
	if(rFactor != 1.0e-6){
		printf("ERROR: Test 17 Failed, 1.0 %s != %.1e %s\n", muO, rFactor, 
				 muO_reduced);
		return 15;
	}
	
	/* Test unicode decomposition for the special characters μ and Ω */
	das_units bad_microohms  = Units_fromStr("µΩ m^-1"); /* Depending on the font in your  */
	das_units good_microohms = Units_fromStr("μΩ m^-1"); /* editor you might not the       */
	                                                    /* difference, but it's there and */
	                                                    /* libdas2 handles it.            */
	if( bad_microohms != good_microohms){
		printf("ERROR: Test 18 Failed, decomposition failed %s != %s\n", bad_microohms,
		       good_microohms);
		return 15;
	}
	
	/* Test order preservation for unknown units.  Assume that the user wants
	 * units in the given order unless otherwise specified. */
	const char* sUnits = "cm**-2 keV**-1 s**-1 sr**-1";
	das_units flux = Units_fromStr(sUnits);
	
	if( strcmp( sUnits, flux) != 0){
		printf("ERROR: Test 19 Failed, unknown units are re-arranged by default. %s != %s\n",
				 sUnits, flux);
		return 15;
	}
	
	/* New unit strings are sticky.  Test that a new variation of the new units
	 * defined above reuses the first definition */
	const char* sSameUnits = "hertz / kiloelectronvolt / centimeters^2 / sterradian";
	das_units flux2 = Units_fromStr(sSameUnits);
	if( flux2 != flux){
		printf("ERROR: Test 20 Failed, repeated unknown units are not normalized to "
				"first instance, %s != %s\n", flux2, flux);
		return 15;
	}
	
	/* Test that wierd unit strings don't crash the program */
	
	/* from Aspera reader... */
	sUnits = "eV/(cm**-2 s**1 sr**1 eV**1)";
	das_units energy_flux = Units_fromStr(sUnits);
	das_units test_e_flux = Units_fromStr("m**2 s**-1 sr**-1");
	das_units reduced_flux = Units_reduce(energy_flux, &rFactor);
	
	if( reduced_flux != test_e_flux){
		printf("ERROR: Test 21 Failed, eV did not cancel: %s (expected %s)\n",
				 reduced_flux, test_e_flux);
		return 15;
	}
	
	/* from Cassini density reader ... */
	sUnits = "electrons / cm ^ 3";
	das_units num_dens1 = Units_fromStr(sUnits);
	das_units num_dens2 = Units_fromStr("electrons cm**-3");
	if( num_dens1 != num_dens2){
		printf("ERROR: Test 22 Failed, %s != %s", num_dens1, num_dens2);
		return 15;
	}
	
	printf("INFO: All unit manipulation tests passed\n\n");
	return 0;
}

