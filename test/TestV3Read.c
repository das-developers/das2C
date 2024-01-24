/** @file TestV3Read.c Testing basic das stream v3.0 packet parsing */

/* Author: Chris Piker <chris-piker@uiowa.edu>
 * 
 * This file contains test and example code that intends to explain an
 * interface.
 * 
 * As United States courts have ruled that interfaces cannot be copyrighted,
 * the code in this individual source file, TestBuilder.c, is placed into the
 * public domain and may be displayed, incorporated or otherwise re-used without
 * restriction.  It is offered to the public without any without any warranty
 * including even the implied warranty of merchantability or fitness for a
 * particular purpose. 
 */

#define _POSIX_C_SOURCE 200112L

#include <stdio.h>
#include <das2/core.h>

const char* g_sTestFile1 = "./test/ex12_sounder_xyz.d3t";
const char* g_sTestFile1 = "./test/tag_test.dNt";

int main(int argc, char** argv)
{
   /* Exit on errors, log info messages and above */
   das_init(argv[0], DASERR_DIS_EXIT, 0, DASLOG_INFO, NULL);

   printf("INFO: Reading %s\n", g_sTestFile1);
   FILE* pFile = fopen(g_sTestFile1, "r");

   DasIO* pIn = new_DasIO_cfile("TestV3Read", pFile, "r");

   /* Just read it parsing packets.  Don't invoke any stream handlers to
      do stuff with the packets */
   int nTest = 1;
   if(DasIO_readAll(pIn) != 0){
      printf("ERROR: Test %d failed, couldn't parse %s\n", nTest, g_sTestFile1);
      return 64;
   }

   printf("INFO: %s parsed without errors\n", g_sTestFile1);

   return 0;
}
