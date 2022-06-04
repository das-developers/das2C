/** @file cli.h - Das Reader Version 2.2/2.3 command line argument handling 
 *
 * Note: It's time to make a new version of this library that uses functions
 *       to setup the selector and output arrays.  They are getting
 *       complicated enough.
 */

#ifndef das2_2_cli_H_
#define das2_2_cli_H_

#include <das2/das1.h>

#include <string.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/** Strings to assist with operator comparisons */
#define OP_BEG ".ge."
#define OP_END ".lt."
#define OP_EQ  ".eq."
#define OP_NE  ".ne."
#define OP_LT  ".lt."
#define OP_GT  ".gt."
#define OP_LE  ".le."
#define OP_GE  ".ge."

/** An enumeration of das selector value types */
enum das_selfmt{bool_t, int_t, real_t, string_t, timept_t};

/** Flags for selector and output defintions */
#define REQUIRED    0x00
#define OPTIONAL    0x01
#define ENUM        0x02
#define INTERVAL    0x04
#define XLATE_GE_LT 0x08  /* Use .beg. and .end. synonyms for .ge. and .lt. 
                           when providing user text for this selector. */
											
#define SHOW_DEFAULT 0x10  /* Show the default values in the help text */

/** Holds a single data selection query parameter */
typedef struct das_selector_t{
	
	/** The key used to find this data selector */
	const char* sKey;
	
	/** The value type for the data selector */
	enum das_selfmt  nFmt;
	
	/** Selector options, can be any of: REQUIRED|OPTIONAL|ENUM */
	unsigned int nFlags;
		
	/** The allowed comparisons for a PARAM selector, or the enumeration
	    values for a ENUM selector */
	const char** psBounds;
	
	/* A major design oversight:  Should be allowed to place the default
	   values here.  Since we can't do that the user ends up re-specifing
		them all over the place in their programs which invites bugs! */
	/* const char** psDefaults; */
	
	/** A human-readable summary of the selector */
	const char* sSummary;
	
	/** The string value(s) from the command line */
	char** psValues;
	
}DasSelector;


/** Get the a selection boundary as a raw string.
 * Since all values are stored internally as strings, this always works.
 *
 * @param pSels the selector array
 * @param sKey the selector key
 * @param sOp the comparison operation string, one of ".ge.", .lt.", etc.
 * @param sDefault a default value to return is this comparison criteria was
 *        not provided on the command line.
 * @return The value to compare the parameter with, as a string.
 */
const char* das_get_selstr(const DasSelector* pSels, const char* sKey, 
		                 const char* sOp, const char* sDefault);

const char* das_selstr(const DasSelector* pSel, const char* sOp, 
                    const char* sDefault);

/** Get the selection boundary as an integer
 * This function only works for INTEGER parameter types.
 */
int das_get_selint(const DasSelector* pSels, const char* sKey,
		         const char* sOp, int nDefault);

int das_selint(const DasSelector* pSel, const char* sOp, int nDefault);


/** Get a selection criteria as a boolean
 * Only works for BOOLEAN format selectors
 */
bool das_get_selbool(const DasSelector* pSels, const char* sKey, bool bDefault);

bool das_selbool(const DasSelector* pSel, bool bDefault);


/** Search for a selection criteria, return boundary value as a double.
 *
 * Only works for REAL format selectors
 */
double das_get_selreal(const DasSelector* pSels, const char* sKey, 
	                const char* sOp, double rDefault);

/** Get a selection criteria from a particular selector as a double.
 */
double das_selreal(const DasSelector* pSel, const char* sOp, double rDefault);


/** Parse the boundary value into a time tuple.
 * NOTE: Argument doy can be null if you don't care about Day of Year
 */
void das_get_seltime(const DasSelector* pSels, const char* sKey, const char* sOp,
		  int* yr, int* mon, int* dom, int* doy, int* hr, int* min, double* sec);

void das_seltime(const DasSelector* pSel, const char* sOp, int* yr, int* mon,
              int* dom, int* doy, int* hr, int* min, double* sec);

/** Equivalent to das_get_seltime but uses the new das_time type from time.h */
void das_get_seldastime(
	const DasSelector* pSels, const char* sKey, const char* sOp, das_time* pDt
);

/** Equivalent to das_seltime but uses the new das_time type from time.h */
void das_seldastime(const DasSelector* pSel, const char* sOp, das_time* pDt);


/** A short form of das_getstr for enumeration selectors
 *
 * Enumeration values are always strings, so das_getstr will wolk, this
 * version of that function allows one to leave out the comparitor argument.
 */
const char* das_get_selenum(const DasSelector* pSels, const char* sKey, 
		                      const char* sDefault);

const char* das_selenum(const DasSelector* pSel, const char* sDefault);


/** Use this in nOpts to disable the output be default */

#define DAS_OUT_DISABLE  0x0200 

/** A basic statement of output is needed to help with output resolution 
    reduction */
	 
typedef struct das_output_t{
	
	/** The name of this "axis" */
	const char* sKey;
	
	/** The units string for this axis */
	const char* sUnits;
	
	/** Output Options, can be any of, OPTIONAL|INTERVAL */
	unsigned int nOpts;
	
	/** A listing of the dependent outputs for this independent value */
	const char** psDepends;

	/** A summary of the output */
	const char* sSummary;
		
	/** Storage for the interval value, if provided */
	char* sInterval;
	
}DasOutput;


/** Search for the enabled status for a named output */
bool das_get_outenabled(const DasOutput* pOuts, const char* sKey);

/** Get the enabled status for a given output */
bool das_outenabled(const DasOutput* pOut);

/** Search for the interval setting for a named output */
double das_get_outinterval(const DasOutput* pOuts, const char* sKey, 
                          double rDefault);

/** Get the interval setting for a given output */
double das_outinterval(const DasOutput* pOut, double rDefault);

/** Get the units for an output */
const char* das_get_outunit(const DasOutput* pOut);


/** Parse a Das 2.3 style commandline, with Das 2.1 support.
 *
 * Keword=value pair arguments are handled as directed by the selector set.
 * There special argumets are also handled internally.
 *  
 *  --das2times=SELECTOR
 *      This causes the first two non-special, non keyword.OP.value arguments
 *      to be treated as start_time and end_time.  It also will search within
 *      the third argument to make sure it doesn't contain sub-arugments.
 *
 *  --das2int=OUTPUT
 *      This causes the third non-special, non keyword.OP.value arugement to
 *      be treated as the sampling resolution.  Using this requires the
 *      use of --das2times as well.
 *          
 *  -h,--help
 *      Will print help text
 *
 *  -l LEVEL,--log=LEVEL
 *      This will set a global variable indicating which level details the
 *      reader should output when writing log messages to standard error.
 *      there is no facility for loging in this library this is just a 
 *      standard way to indicate the caller's preference
 * 
 * Under normal operation the function exits with return value 15 with a
 * message to standard error if there is a runtime problem.  If there is a
 * programming issue that should be fixed before release a message is still
 * printed to standard error and 46 is returned.
 *
 * @param nArgs - The number of argument character strings
 * @param sArgs - The arugment string pointer array
 * @param pSels - A pointer to the first element of an array of data selectors
 *                These define the query parameters for this reader.
 * @param sDesc - An optional chunk of text to output as the discription part
 *                of the reader help.  It may be NULL and is only used if
 *                '--help', '-h', or '-?' is detected on the commandline.
 */
void das_parsecmdline(int nArgs, char** sArgs, DasSelector* pSels, 
                      DasOutput* pOuts, const char* sDesc, const char* sFooter);


/** Get the program basename
 * Only works after das_parsecmdline() has been called 
 */
const char* das_progname();

#define DAS_LL_CRIT   100  /* same as java.util.logging.Level.SEVERE */
#define DAS_LL_ERROR   80  
#define DAS_LL_WARN    60  /* same as java.util.logging.Level.WARNING */
#define DAS_LL_INFO    40  /* same as java.util.logging.Level.INFO & CONFIG */
#define DAS_LL_DEBUG   20  /* same as java.util.logging.Level.FINE */
#define DAS_LL_TRACE    0  /* same as java.util.logging.Level.FINER & FINEST */

/** Get the log level.
 * If -l or --log is not specified on the command line, or this function is
 * called before das_parsecmdline() then the default log level is DAS2_LL_INFO.
 *
 * @returns one of: DAS_LL_CRIT, DAS_LL_ERROR, DAS_LL_WARN, DAS_LL_INFO,
 *                  DAS_LL_DEBUG, DAS_LL_TRACE 
 */
int das_loglevel();


#ifdef __cplusplus
}
#endif


#endif /* das2_2_cli_H_ */
