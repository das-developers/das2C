/** @file spice.h - Das Reader spice error message handling */

#include <das3/datum.h>
#include <das3/oob.h>

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
DAS_API void das_spice_err_setup(void);


/** Get a spice error as string.
 * 
 * @Warning This function is not MT safe, but then again SPICE isn't
 * MT safe, so not a big deal.
 * 
 * @returns NULL if there is no current spice error, a string pointer
 *  otherwise.
 */
DAS_API const char* das_get_spice_error(void);


/** Reads a spice error and outputs it as a das exception, the program
 * should only call this if failed_ returns non-zero, and it should exit
 * after calling this function.
 *
 * @param nDasVer - The stream major version, 1, 2 or 3.  das1 streams have
 *        no in-band errors, so for 1 the message only goes to stderr.
 *
 * @param nType - The exception type, usually DAS_EX_SERVER_ERR.  The name
 *        written for it depends on nDasVer, see das_except_toStr().
 *
 * @return The function always returns a non-zero value so that the das
 *      server knows the request did not complete.
 */
DAS_API int das_send_spice_err(int nDasVer, das_except_t nType);

/** Print a spice error to standard output 
 *
 * @returns 89, you can set this to the calling shell if you like, or call
 *      reset_c() get process different spice input 
 */
DAS_API int das_print_spice_error(const char* sProgName);

/** @} */

#endif /* _das2_spice_H_ */
