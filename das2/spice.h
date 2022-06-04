/** @file spice.h - Das Reader spice error message handling */

#ifndef _das2_spice_H_
#define _das2_spice_H_

/** Setup spice so that errors are not automatically output to the standard 
 *  output channel.
 * 
 * Das2 readers (and unix programs in general) are only supposed to output
 * data to standard out, not error messages.
 */
void das_spice_err_setup();


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



#endif /* _das2_spice_H_ */
