/* test how conversions of integers to doubles affects the values */


#include <stdio.h>
#include <stdint.h>

int main(int argc, char** argv){

	int64_t nSec = 60L*60*24*( 365*55 + 13);  /* rough seconds in 55 years */
	printf("64-Bit Int seconds in ~55 years      %ld\n", nSec);

	double dSec = (double)nSec;
	printf("Double seconds in ~55 years          %f\n", dSec);	

	int64_t nNano = nSec * 1000000000 + 11111;  
	printf("64-Bit Int nanoseconds in ~55 years  %ld\n", nNano);

	double dNano = (double)nNano;

	printf("Double nanoseconds in ~55 years      %f\n", dNano);
	
	printf("\nPrecision loss is about 100 nanosec @ 50 years.\n");
}
