/** @file  data.h */

#ifndef DATA_H
#define DATA_H

#include <das2/units.h>
#include <das2/descriptor.h>

#include "util.h"

/* Basic datum structure, a number plus it's units */
typedef struct datum_t{
	UnitType units;
	double value;
} datum_t;

/** Initialize a datum structure using a string 
 * 
 * @param pDatum pointer to the datum structure to initialize
 * @param sStr the source of the data plus value
 * @return true if the string was parseable as a datum, false otherwise.
 */
bool Datum_fromStr(datum_t* pDatum, const char* sStr);

/** Write a datum out as a string 
 * 
 * Time values are printed as ISO-8601 time strings, all other values are
 * printed using a generic exponential notation.
 * 
 * @param sStr Buffer to hold the output
 * @param uLen Length of buffer (-1 if you want null termination)
 * @param nFracDigits Number of digits after the decimal place to print.
 *        for multi-part values, such a calendar times this refers to the
 *        number of digits after the decimal point for the last component
 *        only.
 * @param pDatum A constant pointer to the datum to print
 * @return the sStr pointer.
 */
char* Datum_toStr(
	char* sStr, size_t uLen, int nFracDigits, const datum_t* pDatum
);

/* The structures below are the start of an idea on how to get independent
 * parameters for data at any particular index.  These are just thoughts
 * at the moment and don't affect any working code.  There are many ways
 * to do this.  The CDF and QStream assumption is that there are the same
 * number of parameters locating a data point in parameter space as there
 * are indices to the dataset.  Because of this x,y,z scatter data are 
 * hard to handle.
 *  
 * For x,y,z scatter lists there is 1 index for any point in the dataset, 
 * but for each index there are 2 independent parameters.  Basically QStream
 * and CDF assume that all datasets are CUBEs in parameter space but this
 * is not the case for a great many sets. 
 *
 * To adequatly handle these 'path' datasets a parameter map is required.
 * The mapping takes 1 index value per data rank and returns 1 to N parameter
 * values.
 *
 * These structures start to handle this idea but are just doodles at this
 * point. -cwp 2017-07-25
 */
	
typedef struct param {
	UnitType units;
	char label;
} Param;

typedef struct param_converter{
	struct param a;
	
} ParamConverter;

typedef struct das_dataset_t{
	UnitType units;
	double* values;
	das_int_array shape;
	ParamConverter* converters;
	Descriptor props;
} DasDs;

das_cint_array* DasDs_shape(const DasDs* pThis);

bool DasDs_coord(const DasDs* pThis, das_real_array* pCoords);


#endif	/* DATA_H */


















