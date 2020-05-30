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
 
#include <das2/core.h>

void prnUtc(long long tt)
{
	double yr, mt, dy, hr, mn, sc, ms, us, ns;
	das_tt2000_to_utc(tt, &yr, &mt, &dy, &hr, &mn, &sc, &ms, &us, &ns);
	fprintf(stdout, 
		"%18lldLL -> %04.0f-%02.0f-%02.0fT%02.0f:%02.0f:%02.0f.%03.0f%03.0f%03.0f\n",
		tt, yr, mt, dy, hr, mn, sc, ms, us, ns
	);
}

void prnTt2000(
	double yr, double mt, double dy, double hr, double mn, double sc, 
	double ms, double us, double ns
){
	long long tt = das_utc_to_tt2000(yr, mt, dy, hr, mn, sc, ms, us, ns);
	fprintf(stdout, 
		"%04.0f-%02.0f-%02.0fT%02.0f:%02.0f:%02.0f.%03.0f%03.0f%03.0f -> %18lldLL\n",
		yr, mt, dy, hr, mn, sc, ms, us, ns, tt
	);
}

int main(int argc, char** argv)
{
	/* Exit on errors, log info messages and above */
	das_init(argv[0], DASERR_DIS_EXIT, 0, DASLOG_INFO, NULL);
	
	prnUtc(0LL);
	prnUtc(114198895034999000LL);
	prnUtc(114198941235000000LL);
	
	prnTt2000(2000, 1, 1, 11, 58, 55, 816,   0, 0);
	prnTt2000(2003, 8, 15, 5, 53, 50, 850, 999, 0);
	prnTt2000(2003, 8, 15, 5, 54, 37,  51,   0, 0);
	
	return 0;
}
