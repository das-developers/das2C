/** @file LoadArray.c  Program to load a das2 stream into memory and
 * then do nothing with it.  This can be used to see how much memory a 
 * stream would occupy */
 
#define _POSIX_C_SOURCE 200112L

#include <stdio.h>
#include <stdlib.h>
#include <math.h>
#include <string.h>

#ifdef _WIN32
#include <direct.h>
#define getcwd _getcwd
#else
#include <unistd.h>
#endif

#include <das2/core.h>

#define ERRNUM 64

int main(int argc, char** argv)
{	
	/* Exit on errors, log info messages and above */
	das_init(argv[0], DASERR_DIS_EXIT, 0, DASLOG_INFO, NULL);
	
	/* If no test file was specified, try to load a known test file */
	char sCurDir[255] = {'\0'};
	getcwd(sCurDir, 254);
	daslog_info_v("Current directory is: %s", sCurDir);

	const char* sInput = NULL;
	if(argc < 2){
		sInput = "test/cassini_rpws_wfrm_sample.d2s";
	}
	else{
		sInput = argv[1];
	}
	daslog_info_v("Reading %s", sInput);
	
	DasIO* pIn = new_DasIO_file("Load Array", sInput, "r");
	DasDsBldr* pBldr = new_DasDsBldr();
	DasIO_addProcessor(pIn, (StreamHandler*)pBldr);
	
	if(DasIO_readAll(pIn) != 0)
		return das_error(ERRNUM, "Couldn't process Das2 Stream file %s", argv[1]);
	
	size_t uSets = 0;
	DasDs** lDs = DasDsBldr_getDataSets(pBldr, &uSets);
	for(size_t u = 0; u < uSets; ++u)
		del_DasDs(lDs[u]);
	
	/* The next line is causing an error at exit on windows, find out why.
	   The error does not stop the log line at the bottom from running but
	   does trigger a non-zero return to the shell... curious. */
	del_DasDsBldr(pBldr);  /* Automatically closes the file, not sure if I like this */
	del_DasIO(pIn);

	daslog_info_v("%z datasets sucessfully loaded and unloaded", uSets);
	return 0;
}
