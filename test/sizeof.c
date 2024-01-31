/* Test structure padding */

#include <stdint.h>
#include <stdio.h>

typedef uint8_t byte;
typedef uint16_t ushort;

typedef struct das_time_new_t{

	/** Calendar month number, 1 = January */
	int8_t month; 
	
	/** Hour of day, range is 0 to 23 */
   int8_t hour;
	
	/** Minute of the hour, range 0 to 59 */
	int8_t minute; 

	/** Calender Day of month, starts at 1 */
	int8_t mday; 

   /** Calendar year number, cannot hold years before 1 AD */
   int16_t year; 

   /** Integer Day of year, Jan. 1st = 1.  
    *  This field is <b>output only</b> for most Das1 functions see the
    *  warning in dt_tnorm() */
   int16_t yday; 
   
	/** Second of the minute, range 0.0 to 60.0 - epsilon.  
	 * Note, there is no provision for leap seconds in the library.  All
	 * minutes are assumed to have 60 seconds.
	 */
	double second;
	
} das_time_new;

typedef struct das_time_t{

	/** Calendar year number, cannot hold years before 1 AD */
	int year; 
	
	/** Calendar month number, 1 = January */
	int month; 
	
	/** Calender Day of month, starts at 1 */
	int mday; 
	
	/** Integer Day of year, Jan. 1st = 1.  
	 *  This field is <b>output only</b> for most Das1 functions see the
	 *  warning in dt_tnorm() */
	int yday; 
	
	/** Hour of day, range is 0 to 23 */
   int hour;
	
	/** Minute of the hour, range 0 to 59 */
	int minute; 
	
	/** Second of the minute, range 0.0 to 60.0 - epsilon.  
	 * Note, there is no provision for leap seconds in the library.  All
	 * minutes are assumed to have 60 seconds.
	 */
	double second;
	
} das_time;


/*
typedef struct quantity_s {
	byte value[32];   / * 32 bytes * /
	ushort qinfo[4];  / * 8 bytes    (40) * /
	void*  units;     / * 4 or 8 bytes   (44, 48) * /
} quantity;
*/

typedef struct datum_t {
   byte bytes[32]; /* 32 bytes of space */
   byte vt;
   byte vsize;
   const char* units;
} das_datum;

typedef struct das_vector_t{

   /* The vector values if local */
   double comp[3];

   /* The ID of the vector frame, or 0 if unknown */
   byte   frame;  

   /* Frame type copied from Frame Descrptor */
   byte   ft;

   /* the element type, taken from das_val_type */
   byte   et;

   /* the size of each elemnt, in bytes, copied from das_vt_size */
   byte   elsz; 

   /* Number of valid components */
   byte   ncomp;

   /* Direction for each component */
   byte   dirs[3];

} das_vector;

// Little endian
//  01234567 01234567
// +--------+--------+
// |[vt][ct]|[esz]rrp|
// +--------+--------+
// vt = Element type          --> 4 bits    21 ~ 3 bytes
// ct = Composite type        --> 4 bits
// element size               --> 5 bits 
// RR = Rank (up to 3-D)      --> 2 bits
//  P = Value contains pointer. -> 1 bit

int main(int argc, char** argv)
{
	printf("sizeof(das_datum)  = %zu\n", sizeof(das_datum));
	printf("sizeof(das_vector) = %zu\n", sizeof(das_vector));
	printf("sizeof(das_time)   = %zu\n", sizeof(das_time));
	printf("sizeof(das_time)   = %zu\n", sizeof(das_time_new));
	return 0;
}