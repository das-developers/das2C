/* Test various forms of NaN to use a binary encodings for floating point
   values */

#define _POSIX_C_SOURCE 200112L

#include <math.h>
#include <stdio.h>
#include <stdint.h>


int main(int argc, char** argv)
{
	
	float f = NAN;
	double d = NAN;
	
	printf("NAN is: %0X\n", *((unsigned int*)(&f)));
	printf("NAN is: %0lX\n", *((unsigned long*)(&d)));
	
	f = nanf("11");
	printf("nanf(\"11\") is: %0x\n", *((int*)(&f)));
	
	uint32_t u = 0x7F80807F;
	printf("Is %0X nan? %s\n", u, isnan(*(float*)(&u)) ? "yes" : "no");
	
	
	return 0;
}
