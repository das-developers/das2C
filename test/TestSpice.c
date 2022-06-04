/** @file TestSpice.c Unit test for basic spice function calls */

#define _POSIX_C_SOURCE 200112L

#include <stdio.h>
#include <das2/core.h>
#include <das2/spice.h>

int main(int argc, char** argv)
{
	
	/* Exit on errors, log info messages and above */
	das_init(argv[0], DASERR_DIS_EXIT, 0, DASLOG_INFO, NULL);
	
	das_spice_err_setup();  /* Make sure spice errors don't go to stdout */
	
	printf("INFO: Can redirect spice errors from stdout, good\n");
	
	return 0;
}
