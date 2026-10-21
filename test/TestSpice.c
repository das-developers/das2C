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
#include <inttypes.h>
#include <string.h>

#include <das3/core.h>
#include <das3/spice.h>
#include <das3/form_vec.h>

#include <SpiceUsr.h>

void prnConvert(double rET){
	
	char sBuf[40] = {'\0'};
	et2utc_c(rET, "ISOC", 9, sizeof(sBuf), sBuf);
	das_time dt;
	dt_parsetime(&dt, sBuf);

	int64_t nTT = dt_to_tt2k(&dt);
	printf("ET    %20.9f is %s UTC, %17" PRId64 " TT (TT-ET is %f)\n", rET, sBuf, nTT, nTT*1e-9 - rET);
	
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

	printf("myET  %20.9f  (delta Mine - Spice) %.3f µs\n", rMyET, (rMyET - rET)*1e6);
}

/* ************************************************************************* */
/* Component system conversions vs. cspice
 *
 * libdas3 does its own trig for das_vsys_toCart()/fromCart() so its numbers
 * never depend on whether cspice is linked.  The risk in that is not
 * rounding, it is convention: recsph_c returns colatitude while reclat_c
 * returns latitude, and they put longitude in different slots.  A slot mix-up
 * is a 90 degree error that looks plausible in a plot, so check the das
 * canonical orders against cspice here, where cspice is already linked.
 */

#define VSYS_TOL 1e-12

/* Angles are compared modulo 360, reccyl_c returns [0, 2pi) where atan2()
   based code returns [-pi, pi]; both name the same direction. */
static bool _angEq(double rA, double rB)
{
	double rD = fmod(rA - rB, 360.0);
	if(rD >  180.0) rD -= 360.0;
	if(rD < -180.0) rD += 360.0;
	return fabs(rD) < 1e-9;
}

static bool _lenEq(double rA, double rB, double rScale)
{
	return fabs(rA - rB) <= VSYS_TOL * (rScale > 1.0 ? rScale : 1.0);
}

/* cspice in das canonical order, angles in degrees */
static void _spiceFromCart(ubyte uSys, const double* pIn, double* pOut)
{
	switch(uSys){
	case DAS_VSYS_CYL:     reccyl_c((double*)pIn, pOut, pOut+1, pOut+2); break;
	case DAS_VSYS_SPH:     recsph_c((double*)pIn, pOut, pOut+1, pOut+2); break;
	case DAS_VSYS_CENTRIC: reclat_c((double*)pIn, pOut, pOut+1, pOut+2); break;
	}
	pOut[1] *= dpr_c();
	if(uSys != DAS_VSYS_CYL) pOut[2] *= dpr_c();
}

static void _spiceToCart(ubyte uSys, const double* pIn, double* pOut)
{
	switch(uSys){
	case DAS_VSYS_CYL:
		cylrec_c(pIn[0], pIn[1]*rpd_c(), pIn[2], pOut); break;
	case DAS_VSYS_SPH:
		sphrec_c(pIn[0], pIn[1]*rpd_c(), pIn[2]*rpd_c(), pOut); break;
	case DAS_VSYS_CENTRIC:
		latrec_c(pIn[0], pIn[1]*rpd_c(), pIn[2]*rpd_c(), pOut); break;
	}
}

static bool _isAngle(ubyte uSys, int i)
{
	return (i == 1) || ((i == 2) && (uSys != DAS_VSYS_CYL));
}

static int _checkVsys(void)
{
	/* All octants, both Z axes, points on the X and -Y axes, the origin */
	const double aPts[][3] = {
		{ 1.0,  2.0,  3.0}, {-1.0,  2.0,  3.0}, {-1.0, -2.0,  3.0},
		{ 1.0, -2.0,  3.0}, { 1.0,  2.0, -3.0}, {-1.0,  2.0, -3.0},
		{-1.0, -2.0, -3.0}, { 1.0, -2.0, -3.0}, { 0.0,  0.0,  5.0},
		{ 0.0,  0.0, -5.0}, { 3.0,  0.0,  0.0}, { 0.0, -4.0,  0.0},
		{ 7.0e4, -2.5e3, 1.2e4}, { 0.0,  0.0,  0.0}
	};
	const size_t uPts = sizeof(aPts) / sizeof(aPts[0]);
	const ubyte aSys[3] = {DAS_VSYS_CYL, DAS_VSYS_SPH, DAS_VSYS_CENTRIC};

	int nFail = 0;
	bool bCylWide = false;

	for(int iSys = 0; iSys < 3; ++iSys){
		ubyte uSys = aSys[iSys];
		const char* sSys = das_vsys_toStr(uSys);

		for(size_t u = 0; u < uPts; ++u){
			const double* pP = aPts[u];
			double rScale = sqrt(pP[0]*pP[0] + pP[1]*pP[1] + pP[2]*pP[2]);
			double aD[3], aS[3], aDc[3], aSc[3], aTmp[3];

			/* cartesian -> system */
			if(!das_vsys_fromCart(uSys, pP, aD)) return 1;
			_spiceFromCart(uSys, pP, aS);
			for(int i = 0; i < 3; ++i){
				bool bOk = _isAngle(uSys, i) ? _angEq(aD[i], aS[i]) :
				                               _lenEq(aD[i], aS[i], rScale);
				if(!bOk){
					printf("ERROR: %s fromCart (%g,%g,%g) slot %d: das %.15g, "
					       "cspice %.15g\n", sSys, pP[0], pP[1], pP[2], i, aD[i],
					       aS[i]);
					++nFail;
				}
			}
			if((uSys == DAS_VSYS_CYL)&&(aS[1] > 180.0)) bCylWide = true;

			/* system -> cartesian, each library from its own angles */
			if(!das_vsys_toCart(uSys, aD, aDc)) return 1;
			_spiceToCart(uSys, aS, aSc);
			for(int i = 0; i < 3; ++i){
				if(!_lenEq(aDc[i], aSc[i], rScale) || !_lenEq(aDc[i], pP[i], rScale)){
					printf("ERROR: %s toCart (%g,%g,%g) slot %d: das %.15g, cspice "
					       "%.15g, start %.15g\n", sSys, pP[0], pP[1], pP[2], i,
					       aDc[i], aSc[i], pP[i]);
					++nFail;
				}
			}

			/* In place must be bit-identical to separate buffers, the header
			   promises it */
			memcpy(aTmp, pP, sizeof(aTmp));
			das_vsys_fromCart(uSys, aTmp, aTmp);
			if(memcmp(aTmp, aD, sizeof(aTmp)) != 0){
				printf("ERROR: %s fromCart in place differs for (%g,%g,%g)\n",
				       sSys, pP[0], pP[1], pP[2]);
				++nFail;
			}
			das_vsys_toCart(uSys, aTmp, aTmp);
			if(memcmp(aTmp, aDc, sizeof(aTmp)) != 0){
				printf("ERROR: %s toCart in place differs for (%g,%g,%g)\n",
				       sSys, pP[0], pP[1], pP[2]);
				++nFail;
			}
		}
	}

	/* The @note on das_vsys_fromCart() in form_vec.h says reccyl_c returns
	   [0, 360); keep the doc honest. */
	if(!bCylWide){
		printf("ERROR: reccyl_c never returned an angle above 180 deg, fix the "
		       "range note on das_vsys_fromCart() in form_vec.h\n");
		++nFail;
	}

	if(nFail == 0)
		printf("INFO: das_vsys_toCart/fromCart agree with cspice for cyl, sph, "
		       "centric (%zu points each, in place too), good\n", uPts);
	return nFail;
}

int main(int argc, char** argv)
{
	
	/* Exit on errors, log info messages and above */
	das_init(argv[0], DASERR_DIS_EXIT, 0, DASLOG_INFO, NULL);
	
	das_spice_err_setup();  /* Make sure spice errors don't go to stdout */

	printf("INFO: Can redirect spice errors from stdout, good\n");

	/* Load the leap seconds kernel.  Errors are set to RETURN above, so a
	   missing kernel would otherwise just print empty times and pass. */
	furnsh_c("test/spice/leapseconds.tls");
	if(failed_c()){
		printf("ERROR: Could not load test/spice/leapseconds.tls\n");
		return 13;
	}

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

	if(_checkVsys() != 0){
		printf("ERROR: Component system conversion test failed\n");
		return 13;
	}

	return 0;
}
