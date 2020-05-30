#ifndef _tt2000_h_
#define _tt2000_h_

#ifdef __cplusplus
extern "C" {
#endif

/* Basic TT2000 handling, functions in time.h and units.h make make use
   of these */
long long das_utc_to_tt2000(double year, double month, double day, ...);

void das_tt2000_to_utc(
	long long nanoSecSinceJ2000, double* ly, double* lm, double* ld, ...
);

#ifdef __cplusplus
}
#endif

#endif /* _tt2000_h_ */
