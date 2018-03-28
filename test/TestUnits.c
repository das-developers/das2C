/** @file TestUnits.c
 * Authors: Piker, Faden
 *
 * Created on February 2, 2004, 2:39 PM
 * Expanded again in June 2017
 */
#define _POSIX_C_SOURCE 200112L

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <limits.h>

#include <das2/core.h>

int main(int argc, char** argv) {
	
	/* Test singleton nature of unit values */
	UnitType Hz1 = Units_fromStr("Hz");
	char sHz2[20] = {'\0'};  strcpy(sHz2, "Hz");
	UnitType Hz2 = Units_fromStr(sHz2);
	if( (Hz1 != Hz2) || (Hz2 != UNIT_HERTZ)){
		printf("ERROR: Test 1 Failed, %s != %s\n", Hz1, Hz2);
		return 15;
	}
	
	/* Test US2000 forward transformations */
	UnitType units = UNIT_US2000;
	const char* sTime = "2000-1-1T1:00";
	das_time_t dt = {0};
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
	
	
	/*
endor:~/svn/das2/core/devel/libdas2$ ./build.centos6.x86_64/TestUnits
2000-1-1T1:00 us2000=3600000000.000000
  ssm=3600.000000 jd=2451545
 2000-01-01 01:00:00
3600000000.000000
2000-1-1T1:00=3600000000.000000 us2000
2000-1-1T1:00=15340.041667 mj1958
1958-1-1T13:00=0.541667 mj1958
MJ1958=0.541667 US2000=-1325329200000000.000000
ssm=46800.000000 jd=2436205
 1959-01-03 13:00:00
*/
	

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
	das_time_t dt1 = {0};
	Units_convertToDt(&dt1, rTime, units);
	dt_isod(sBuf, 64, &dt1, 0);
	if( strcmp(sBuf, "1958-001T13:00:00") != 0){
		printf("ERROR: Test 8 Failed, %f MJ1958 is not %s UTC\n", rTime, sBuf);
		return 15;
	}
	
	/* Test basic string parsing into canonical representation without
	   unit reduction */
	UnitType a = Units_fromStr("V/m");
	UnitType b = Units_fromStr("V m^-1");
	UnitType c = Units_fromStr("V m**-2/2"); /*<-- don't use this, but it does work */
	
	if( a != b ){ printf("ERROR: Test 8 Failed, '%s' != '%s' \n", a, b); return 15; }
	if( a != c ){ printf("ERROR: Test 8 Failed, '%s' != '%s' \n", a, c); return 15; }
	
	
	/* Test unit inversion */
	UnitType d = Units_fromStr("m V**-1");
	UnitType e = Units_invert(a);
	
	if( d != e ){ printf("ERROR: Test 9 Failed, '%s' != '%s' \n", d, e); return 15; }
	
	/* Test unit raise to power */
	UnitType f = Units_fromStr("V**2 m**-2");
	UnitType g = Units_power(a, 2);	
	
	if( f != g ){ printf("ERROR: Test 10 Failed, '%s' != '%s' \n", f, g); return 15; }
	
	/* Test unit multiplication */
	UnitType h = UNIT_E_SPECDENS;
	
	UnitType i = Units_multiply( Units_power(a, 2), Units_power(UNIT_HERTZ, -1));
	
	if( h != i ){ printf("ERROR: Test 11 Failed, '%s' != '%s' \n", h, i); return 15; }
	
	/* Test interval units for t2000 */
	UnitType j = Units_interval(UNIT_T2000);
	UnitType k = Units_invert( Units_fromStr("Hertz") );
	if( j != k ){ printf("ERROR: Test 12 Failed, '%s' != '%s' \n", j, k); return 15; }
	
	/* Test interval units for us2000 */
	UnitType l = Units_interval(UNIT_US2000);
	UnitType m = Units_invert( Units_fromStr("MHz") );
	if( l != m ){ printf("ERROR: Test 13 Failed, '%s' != '%s' \n", l, m); return 15; }
	
	
	/* Test unit conversions */
	UnitType ms = Units_fromStr("microsecond");
	UnitType delta = Units_invert( ms );
	double rFactor = Units_convertTo(UNIT_HERTZ, 1.0, delta);
	if( rFactor != 1.0e+6){ 
		printf("ERROR: Test 14 Failed, '%s' to '%s' factor = %.1e, expected 1.0e+06\n", 
				 delta, UNIT_HERTZ, rFactor);
		return 15;
	}
	
	UnitType perday = Units_fromStr("kilodonut/day");
	UnitType persec = Units_fromStr("donut hertz");
	
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
	UnitType O = Units_fromStr("ohms");
	UnitType O_reduced = Units_reduce(O, &rFactor);
	UnitType muO = Units_fromStr("μΩ");
	
	UnitType muO_reduced = Units_reduce(muO, &rFactor);
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
	UnitType bad_microohms  = Units_fromStr("µΩ m^-1"); /* Depending on the font in your  */
	UnitType good_microohms = Units_fromStr("μΩ m^-1"); /* editor you might not the       */
	                                                    /* difference, but it's there and */
	                                                    /* libdas2 handles it.            */
	if( bad_microohms != good_microohms){
		printf("ERROR: Test 16 Failed, decomposition failed %s != %s\n", bad_microohms,
		       good_microohms);
		return 15;
	}
	
	/* Test order preservation for unknown units.  Assume that the user wants
	 * units in the given order unless otherwise specified. */
	const char* sUnits = "cm**-2 keV**-1 s**-1 sr**-1";
	UnitType flux = Units_fromStr(sUnits);
	
	if( strcmp( sUnits, flux) != 0){
		printf("ERROR: Test 17 Failed, unknown units are re-arranged by default. %s != %s\n",
				 sUnits, flux);
		return 15;
	}
	
	/* New unit strings are sticky.  Test that a new variation of the new units
	 * defined above reuses the first definition */
	const char* sSameUnits = "hertz / kiloelectronvolt / centimeters^2 / sterradian";
	UnitType flux2 = Units_fromStr(sSameUnits);
	if( flux2 != flux){
		printf("ERROR: Test 18 Failed, repeated unknown units are not normalized to "
				"first instance, %s != %s\n", flux2, flux);
		return 15;
	}
	
	printf("INFO: All unit manipulation tests passed\n");
	return 0;
}

