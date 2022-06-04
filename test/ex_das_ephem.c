/* Example code for parsing command lines with Das 2.2 compatibility */
/* This example would apply to any ephemeris reader */
#define _POSIX_C_SOURCE 200112L

#include <stdio.h>
#include <das2/core.h>
#include "das2/cli.h"

const char* g_sDesc = 
"Provides Voyager epemeris data in from a variety of reference points.\n";

const char* g_sFooter = 
"Maintainer:\n"
"  Joe Groene <joseph-groene@uiowa.edu>\n\n";


int main(int nArgs, char** sArgs)
{
	int byr, bmon, bdom, bhr, bmin; double bsec;
	int eyr, emon, edom, ehr, emin; double esec;
	const char* sVgr;
	const char* sRef;
	double rRes;
	

	/* This reader has three data selecters */
	DasSelector sels[] = {
	
		/* This selector allows for two types of comparison, a begin and an end*/
		{"scet", timept_t, XLATE_GE_LT, (const char* []){OP_GE, OP_LT ,NULL}, 
		  "Spacecraft Event Time" },
		
		/* Pick the spacecraft */
		{"vgr", string_t, ENUM, (const char* []){"1","2",NULL}, 
		 "Spacecraft Selection" },
		
		/* Pick the reference point */
		{"ref", string_t, ENUM, 
		 (const char* []){"sun","earth","jupiter","saturn","uranus","neptune", NULL},
		 "Reference Point Selection"},
		
		{NULL}  /* <-- Requered to terminate the array */
	};
	
	/* This reader has upto 4 indepent variable outputs, and the dependent
	   variable has a randomly selectable resolution */
	DasOutput outs[] = {
	
		{"time", "UTC", REQUIRED, NULL, 
		 "The spacecraft event time"},
		
		{"radius", NULL, OPTIONAL, (const char*[]) {"time", NULL},
		 "The radial position reference body Radii"},
		
		{"Lon", NULL, OPTIONAL, (const char*[]) {"time", NULL},
		 "The planeocentric longitude of the spacecraft"},
		
		{"Lat", NULL, OPTIONAL, (const char*[]) {"time", NULL},
		 "The planeocentric latitude of the spacecraft"},
		
		{"LT", NULL, OPTIONAL, (const char*[]) {"time", NULL},
		 "The local time of the spacecraft position"},
		
		{"L", NULL, OPTIONAL, (const char*[]) {"time", NULL},
		 "The magnetic L-Shell from a dipole magnetic field model."},
		
		{NULL},
	};
	
	das_parsecmdline(nArgs, sArgs, sels, outs, g_sDesc, g_sFooter);
	
	/* This is a required parameter so it's necessary to set the time values
	   to a default before the call */
	das_get_seltime(sels, "scet", OP_BEG, &byr, &bmon, &bdom, NULL, &bhr, &bmin, &bsec);
	das_get_seltime(sels, "scet", OP_END, &eyr, &emon, &edom, NULL, &ehr, &emin, &esec);

	/* This is a required enum so it's not necessary to provide a default*/
	sVgr = das_get_selenum(sels, "vgr", NULL);
	
	/* Pick a reference point, required so again, no default */
	sRef = das_get_selenum(sels, "ref", NULL);
	
	
	/* Now get the output interval in time, default to once per minute */
	rRes = das_get_outinterval(outs, "time", 60.0);
	
	
	/* Let the user know which output are going out the door */	
	fprintf(stderr, "INFO: Outputs for VGR %s from reference %s are ", sVgr, sRef);
	int nOut = 0;
	for(int i = 1; i<6; i++){
		if(das_outenabled(outs+i)){ 
			nOut++;
			if(nOut > 1) fputs(" and ", stderr);
			else fputc(' ', stderr);
			fputs(outs[i].sKey, stderr);
		}
	}
	fputc('\n', stderr);
	
	fprintf(stderr, "INFO: Every %f seconds from %s to %s\n", 
	       rRes, das_get_selstr(sels, "scet", OP_BEG, NULL), 
	       das_get_selstr(sels, "scet", OP_END, NULL) );
	
	fprintf(stderr, "INFO: For Voyager %s, in reference to %s\n", 
	       das_get_selenum(sels, "vgr", NULL), das_get_selenum(sels, "ref", NULL) );
	
	return 0;
}
  
  
  
