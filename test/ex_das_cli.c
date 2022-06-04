/* Example code for parsing Das Server 2.2 command lines, with Das 2.1 
 * compatibility  */
#define _POSIX_C_SOURCE 200112L

#include <stdio.h>
#include <das2/core.h>
#include "das2/cli.h"

const char* g_sDesc = 
"Reads Voyager 1 or Voyager 2 high rate frames, transforms the 4-bit\n"
"   waveforms into a specta of selectable size, and sends the values to\n"
"   standard output in DAS2 stream format\n";

const char* g_sFooter = 
"Maintainer:\n"
"   Chris Piker <chris-piker@uiowa.edu>\n\n";

int main(int nArgs, char** sArgs)
{	
	const char* sVgr;
	bool bClean;
	int byr, bmon, bdom, bhr, bmin; double bsec;
	int eyr, emon, edom, ehr, emin; double esec;
	const char* sBeg = NULL;
	const char* sEnd = NULL;
	const char* sMethod = NULL;
	
	/* Make the selectors that we care about, the parameter order is:
	
	     key, 
		  
		  selector boundary value type, 
		  
		  flags, 
		  
		  allowed comparisons, 
		  
		  constraint array, for params this is an operator constraint list
		                    for enums this is value set
		  selector summary, 
		  default value string,  <-- or you can use default values in the
		                             various das_get_* functions
   */
	
	DasSelector sels[] = {
	
	  /* The time selector is expecting a begin and end time */
	  {"scet", timept_t, XLATE_GE_LT, (const char* []){OP_BEG,OP_END,NULL}, 
	   "Spacecraft Event Time" },
	  
	  {"vgr", string_t, ENUM, (const char* []) {"1","2", NULL}, 
	   "Spacecraft Selection"},
	  
	  {"method", string_t, OPTIONAL|ENUM,
	   (const char* []) {"fft1600","fft3x512av","fft3x512", NULL}, 
	   "Spectrum Creation Method"},
	  
	  {"clean", bool_t, OPTIONAL, NULL, "Turn on noise spike removal"},
	  
	  {NULL} /* <-- Very important, this is how the end of the array is found */
	};
	
	
	/* Advertise your outputs.  For standard readers with no ability to alter 
	   thier this only provides more details for the help text, but for 
		ephemeris readers the outputs have to be defined in order to 
		get resolution arguments. */
	DasOutput outs[] = {
	
		{"time", "UTC", 0, NULL, "The time of the first field value in a Fourier "
		 "Transformed set of waveform points." },
				
		{"frequency", "Hz", 0, NULL, "The frequency bin."},
		
		{"amplitude", "V**2 m**-2 Hz**-1", 0,
		 (const char*[]) {"frequency","time", NULL}, 
		 "The Electric Field Spectral Density in each frequency bin."},
		
		{NULL} /* <-- Very important, this is how the end of the array is found */
	};
	
	das_parsecmdline(nArgs, sArgs, sels, outs, g_sDesc, g_sFooter);
	
	/* For time retrival the defaults would specified in the values before the
	   pointers are passed to the function, however, we haven't bothered here
		because the start and end times are required so we will get values the 
		program will exit in the above call. */
	das_get_seltime(sels, "scet", OP_BEG, &byr, &bmon, &bdom, NULL, &bhr, &bmin, &bsec);
	das_get_seltime(sels, "scet", OP_END, &eyr, &emon, &edom, NULL, &ehr, &emin, &esec);
	
	/* This is a required enum so it's not necessary to provide a default*/
	sVgr = das_get_selenum(sels, "vgr", NULL);
	
	/* This is an optional parameter so a default is provided */
	bClean = das_get_selbool(sels, "clean", false);
	
	/* All parameters can be retrieved as a string, the format determines if
	   they can be retrieved as some other type, these are required parameters
		so no default has been supplied. */
	sBeg = das_get_selstr(sels, "scet", OP_BEG, NULL);
	sEnd = das_get_selstr(sels, "scet", OP_END, NULL);
	
	/* In a real reader we would do stuff, just report on the command line */
	/* for this test reader */
	printf("Outputting Voyager %s data from %s to %s\n", sVgr, sBeg, sEnd);
	
	if(bClean) 
		printf("Noise spikes will be removed\n");
	
	/* This is an optional enumeration, so a default has been provided */
	sMethod = das_get_selenum(sels, "method", "fft1600");
	
	printf("The waveforms will be transformed using method %s\n", sMethod);
	
	return 0;
}
