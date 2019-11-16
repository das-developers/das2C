/** @file LoadArray.c  Program to load a das2 stream into memory and
 * then do nothing with it.  This can be used to see how much memory a 
 * stream would occupy */
 
#define _POSIX_C_SOURCE 200112L

#include <stdio.h>
#include <stdlib.h>
#include <math.h>
#include <string.h>

#include <das2/core.h>

#define ERRNUM 64

int main(int argc, char** argv)
{	
	/* Exit on errors, log info messages and above */
	das_init(argv[0], DASERR_DIS_EXIT, 0, DASLOG_INFO, NULL);
	
	if(argc < 2)
		return das_error(ERRNUM, "No filename provided, usage: %s DAS2STREAM_FILE", argv[0]);
	
	DasIO* pIn = new_DasIO_file("Load Array", argv[1], "r");
	DasDsBldr* pBldr = new_DasDsBldr();
	DasIO_addProcessor(pIn, (StreamHandler*)pBldr);
	
	if(DasIO_readAll(pIn) != 0)
		return das_error(ERRNUM, "Couldn't process Das2 Stream file %s", argv[1]);
	
	size_t uSets = 0;
	DasDs** lDs = DasDsBldr_getDataSets(pBldr, &uSets);
	for(size_t u = 0; u < uSets; ++u) del_DasDs(lDs[u]);
	
	del_DasDsBldr(pBldr);  /* Automatically closes the file, not sure if I like this */
	del_DasIO(pIn);
	return 0;
}
