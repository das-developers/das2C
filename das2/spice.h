/** @file spice.h - Das Reader spice error message handling */

#include <das2/datum.h>

#ifndef _das2_spice_H_
#define _das2_spice_H_

/** @addtogroup spice
 * @{
 */

/** Setup spice so that errors are not automatically output to the standard 
 *  output channel.
 * 
 * Das2 readers (and unix programs in general) are only supposed to output
 * data to standard out, not error messages.
 */
void das_spice_err_setup();


/** Get a spice error as string.
 * 
 * @Warning This function is not MT safe, but then again SPICE isn't
 * MT safe, so not a big deal.
 * 
 * @returns NULL if there is no current spice error, a string pointer
 *  otherwise.
 */
const char* das_get_spice_error();


#define DAS2_EXCEPT_NO_DATA_IN_INTERVAL "NoDataInInterval"
#define DAS2_EXCEPT_ILLEGAL_ARGUMENT    "IllegalArgument"
#define DAS2_EXCEPT_SERVER_ERROR        "ServerError"


/** Reads a spice error and outputs it as a das exception, the program
 * should only call this if failed_ returns non-zero, and it should exit
 * after callling this function.
 *
 * @param nDasVer - Set to 1 to get das1 compatable output, 2 to get
 *        das2 output
 *
 * @param sErrType - Use one of the predefined strings from the core das2
 *       library:
 *
 *      - DAS2_EXCEPT_NO_DATA_IN_INTERVAL
 *      - DAS2_EXCEPT_ILLEGAL_ARGUMENT
 *      - DAS2_EXCEPT_SERVER_ERROR
 * 
 * @return The function always returns a non-zero value so that the das
 *      server knows the request did not complete.
 */
int das_send_spice_err(int nDasVer, const char* sErrType);

/** Print a spice error to standard output 
 *
 * @returns 89, you can set this to the calling shell if you like, or call
 *      reset_c() get process different spice input 
 */
int das_print_spice_error(const char* sProgName);

/** @} */

#endif /* _das2_spice_H_ */
