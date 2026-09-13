/** @file TestSpice.c Unit test for basic spice function calls */

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
#include <math.h>

#include <das3/core.h>
#include <das3/spice.h>

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
