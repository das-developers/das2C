#include <stdio.h>

#include <udunits2.h>

/* Testing out udunits2 code, I like it because it handles 
   UTF-8 characters, like any non-acient library should */

int main(int argc, char** argv)
{
	
	/* Example pre-stream initialization code, setup Das2 time offset */
	ut_system* pSys = ut_read_xml(NULL);
	
	/* mj1958 - Symbol mapping */
	ut_unit* pMJ1958 = ut_parse(pSys, "days since 1958-01-01", UT_UTF8);
	if(ut_map_unit_to_name(pMJ1958, "mj1958", UT_UTF8) != UT_SUCCESS) return 5;
	if(ut_map_name_to_unit("mj1958", UT_UTF8, pMJ1958) != UT_SUCCESS) return 5;
	
	/* us2000 - Symbol mapping */
	ut_unit* pUS2000 = ut_parse(pSys, "microseconds since 2000-01-01", UT_UTF8);
	if(ut_map_unit_to_name(pUS2000, "us2000", UT_UTF8) != UT_SUCCESS) return 5;
	if(ut_map_name_to_unit("us2000", UT_UTF8, pUS2000) != UT_SUCCESS) return 5;
	
	/* Now read a string to see what kind of units it has */
	
	const char* sXunit = "us2000";
	 
	ut_unit* pUnit = NULL;
	if( (pUnit = ut_get_unit_by_name(pSys, sXunit)) == NULL) return 6; 
	
	
	
	/* Setup a conversion between this and the MJ1958 units */
	cv_converter* pConv;
	if( (pConv = ut_get_converter(pUnit, pMJ1958)) == NULL) return 7;
	
	
	/* Print some value conversions, with units strings */
	double lVals[5] = {
		1e7, 1e7+7000,  1e6,  7.896e6,  8.1e18
	};
	
	
	/* Get the in and out units strings */
	char sIn[64], sOut[64];
	
	if(ut_format(pUnit, sIn, 64, UT_UTF8|UT_NAMES) < 0) return 8;
	if(ut_format(pMJ1958, sOut, 64, UT_UTF8|UT_NAMES) < 0) return 8;
	
	
	/* Convert values */
	double rConv = 0.0;
	for(int i = 0; i < 5; ++i){
		rConv = cv_convert_double(pConv, lVals[i]);
		printf("%.3e %s  -> %.3e %s\n", lVals[i], sIn, rConv, sOut);
	}
	
	
	return 0;
}
