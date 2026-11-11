/* Test structure padding */

#include <stdint.h>
#include <stdio.h>

typedef uint8_t uint8_t;
typedef uint16_t ushort;

typedef struct das_time_new_t{

	/** Calendar month number, 1 = January */
	int8_t month; 
	
	/** Hour of day, range is 0 to 23 */
   int8_t hour;
	
	/** Minute of the hour, range 0 to 59 */
	int8_t minute; 

	/** Calendar Day of month, starts at 1 */
	int8_t mday; 

   /** Calendar year number, cannot hold years before 1 AD */
   int16_t year; 

   /** Integer Day of year, Jan. 1st = 1.  
    *  This field is <b>output only</b> for most Das1 functions see the
    *  warning in DasTime_normalize() */
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
	
	/** Calendar Day of month, starts at 1 */
	int mday; 
	
	/** Integer Day of year, Jan. 1st = 1.  
	 *  This field is <b>output only</b> for most Das1 functions see the
	 *  warning in DasTime_normalize() */
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
	
} DasTime;

/*
typedef struct quantity_s {
	uint8_t value[32];   / * 32 bytes * /
	ushort qinfo[4];  / * 8 bytes    (40) * /
	void*  units;     / * 4 or 8 bytes   (44, 48) * /
} quantity;
*/

typedef struct das_datum_t {
   uint8_t bytes[32]; /* 32 bytes of space */
   uint8_t vt;
   uint8_t vsize;
   const char* units;
} DasDatum;


typedef struct das_geovec_t{

   /* The vector values if local */
   double comp[3];

   /* The ID of the vector frame, or 0 if unknown */
   uint8_t   frame;  

   /* The system type. */
   uint8_t system;

   /* The surface ID if the coordinate system uses a non-standard
      surface */
   uint8_t surface; 

   /* the element value type, taken from das_vt */
   uint8_t   et;

   /* the size of each element, in bytes, copied in from dasvt_size */
   uint8_t   esize; 

   /* Number of valid components */
   uint8_t   ncomp;

   /* Direction for each component - 1, 2 bits for each */ 
   uint8_t   dirs;

   /* Flags. 0x01 == directions, components are not angles */
   /*        0x02 == location, some components are angles */
   uint8_t   flags;

} das_geovec;

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
	printf("sizeof(DasDatum)  = %zu\n", sizeof(DasDatum));
	printf("sizeof(das_geovec) = %zu\n", sizeof(das_geovec));
	printf("sizeof(DasTime)   = %zu\n", sizeof(DasTime));
	printf("sizeof(DasTime)   = %zu\n", sizeof(das_time_new));
	return 0;
}
