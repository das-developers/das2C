/** @file TestSpice.c Unit test for basic spice function calls */

#define _POSIX_C_SOURCE 200112L

#include <stdio.h>
#include <math.h>

#include <das2/core.h>
#include <das2/spice.h>

#include <SpiceUsr.h>

void prnConvert(double rET){
	char sBuf[32] = {'\0'};
	et2utc_c(rET, "ISOC", 12, 31, sBuf);
	das_time dt;
	dt_parsetime(sBuf, &dt);

	int64_t nTT = dt_to_tt2k(&dt);
	printf("ET    %20.9f is %s UTC, %17ld TT (TT-ET is %f)\n", rET, sBuf, nTT, nTT*1e-9 - rET);
	
	/* TT and ET have the same epoch, so once you get to TT it's just: 
	     ET = TT*1e-9 + K sin( E )
	      K = 1.657e-3 
	      E = M + EB sin M
	      M = M0 + M1*ET
	      EB = 1.671e-2
	      M0 = 6.239996
	      M1 = 1.99096871e-7
	  from there.
   */

	const double K = 1.657e-3 ;
	const double EB = 1.671e-2;
	const double M0 = 6.239996;
	const double M1 = 1.99096871e-7;
	double M = M0 + M1*(nTT*1e-9);
	double E = M + EB * sin(M);
	double rMyET = nTT*1e-9 + K * sin( E );

	printf("myET  %20.9f  (delta Mine - Spice) %.12f ms\n", rMyET, (rMyET - rET)*1e6);
}

int main(int argc, char** argv)
{
	
	/* Exit on errors, log info messages and above */
	das_init(argv[0], DASERR_DIS_EXIT, 0, DASLOG_INFO, NULL);
	
	das_spice_err_setup();  /* Make sure spice errors don't go to stdout */

	printf("INFO: Can redirect spice errors from stdout, good\n");

	/* Load the leap seconds kernel */
	furnsh_c("test/leapseconds.tls");

	/* Print ET 0 */
	prnConvert(-86400*366*20);
	prnConvert(-86400*366);
	prnConvert(-86400*274.5);
	prnConvert(-86400*183); 
	prnConvert(-86400*91.5);
	prnConvert(-86400*10.0);
	prnConvert(-86400*1.0);

	prnConvert(86400*0.0);

	prnConvert(86400*1.0);
	prnConvert(86400*10.0);
	prnConvert(86400*91.5);
	prnConvert(86400*183); /* Mean eccentricity should be opposite here */
	prnConvert(86400*274.5);
	prnConvert(86400*366);

	prnConvert(86400*366*20);

	return 0;
}
