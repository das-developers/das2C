/** @file dsdf.h Utilities for parsing DSDF files into descriptor objects. */

#ifndef _dsdf_h_
#define _dsdf_h_

#include <das2/descriptor.h>


/** Parse a DSDF file into a descriptor object.
 *
 * This DSDF parser supports IDL continuation characters, which are '$' 
 * immediately followed by a newline (\n).  The total line buffer across all
 * continuation lines is 22499 bytes long.
 *
 * @param sFileName - The DSDF file to open and parse
 *
 * @returns an Descriptor object allocated on the heap, or NULL if there
 *          was a parsing error 
 */
Descriptor* dsdf_parse(const char* sFileName);


/** Helper function to parse a DSDF value as a double array
 *
 * Certian Das1 DSDF values such as the y_coordinate contained executable
 * IDL code instead of a simple array of values.  If a global IDL executable
 * has been set via dsdf_setIdlBin, then any arrays this function cannot 
 * parse will be handed by an IDL subprocess.
 * 
 * @param[in] sValue - The DSDF value to parse
 * @param[out] pLen - a pointer to a size_t that will receive the length of
 *                    the returned double array
 *
 * @return - A pointer to an array of doubles allocated on the heap, or NULL
 *           if parsing failed.
 */
double* dsdf_valToArray(const char* sValue, size_t* pLen); 


/** Set the location of the IDL binary.  
 *
 * By default the library does not know how to find IDL, use this function
 * to set the location of the idl startup program.  Note that IDL is not
 * needed when parsing Das 2.2 (or higher) compliant DSDF files.  Only programs
 * that read older Das1 DSDF files may have the need to call IDL.
 */
const char* dsdf_setIdlBin(const char* sIdlBin);
	


/** Normalize a general reader command line parameter set. 
 *
 * The normalization rules are as follows:
 *
 * 0. If the raw parameter string is NULL or empty the string '_noparam'
 *    is copied into the norm-param buffer and the function returns.
 *
 * 1. The params are broken on whitespace into a set of ordered tokens.
 * 
 * 2. If the token starts with a '-' and the next token doe snot start
 *    with a '-' then the two tokens are merged with a '-' separator.
 *
 * 3. All tokens are sorted alphabetically and then merged via '_'
 *    separators.
 *
 * @param[in] sRawParam - The parameter string to normalize
 * @param[out] sNormParam - A buffer to hold the normalized parameter string
 * @param[in] uNormLen - The length of the sNormParam buffer
 *
 * @return A pointer to sNormParam which will contain a null terminated
 *         string if not NULL and uLen is at least 2
 *
 */
char* dsdf_valToNormParam(
	const char* sRawParam, char* sNormParam, size_t uNormLen
);

#endif /* _dsdf_h_ */
