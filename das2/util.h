/** @file util.h */

#ifndef _das2_util_h_
#define _das2_util_h_

#include <das2/das1.h>


#define DAS_STREAM_VERSION "2.2"

#include <stdbool.h>
#include <stdlib.h>
#include <stdarg.h>
#include <limits.h>

/* On Solaris systems NAME_MAX is not defined because pathconf() is supposed
 * to be used to get the exact limit by filesystem.  Since all the filesystems
 * in common use today have support 255 characters, let's just define that
 * to be NAME_MAX in the absence of something better.
 */
#ifdef __sun
#ifndef NAME_MAX
#define NAME_MAX 255
#endif
#endif

/* Again, not sure why this is in the library -cwp */
/* void printZErr(FILE * file, int zerr); */

/** return code type
 * 0 indicates success, negative integer indicates failure
 */
typedef int ErrorCode;

/** success return code */
#define DAS_OKAY 0

typedef struct das2_error_message {
	int nErr;
	char * message;
	size_t maxmsg;
	char sFile[256];
	char sFunc[64];
	int nLine;
} Das2ErrorMessage;

ErrorCode das2_error_func(
	const char* sFile, const char* sFunc, int nLine, ErrorCode nCode,
	const char* sFmt, ... 
);

#define DAS2ERR_ASSERT 10  /* Error returns that trigger immediate lib exit
                              should never happen in production code */
#define DAS2ERR_PROC 11    /* General processing exception */
#define DAS2ERR_BUF 12
#define DAS2ERR_UTIL 13
#define DAS2ERR_ENC 14
#define DAS2ERR_UNITS 15
#define DAS2ERR_DESC 16
#define DAS2ERR_PLANE 17
#define DAS2ERR_PKT 18
#define DAS2ERR_STREAM 19
#define DAS2ERR_OOB 20
#define DAS2ERR_IO 22
#define DAS2ERR_DSDF 23
#define DAS2ERR_DFT 24
#define DAS2ERR_LOG 25

#define DAS2ERR_NOTIMP 99

/** Signal an error condition.
 * 
 * This routine is called throughout the code when an error condition arrises.
 * 
 * The default handler for error conditions prints the message provided to 
 * the standard error channel and then calls exit(nErrCode).  To have the library
 * call your handler instead use the das2_set_error_handler() function.  To have
 * the library abort with a core dump on an error use das2_abort_on_error(). 
 * 
 * Each source file in the code has it's own error code.  Though it's probably
 * not that useful to end users, the codes are provided here:
 *  - @b 12 : buffer.c     - DAS2ERR_BUF
 *  - @b 13 : util.c       - DAS2ERR_UTIL
 *  - @b 14 : encoding.c   - DAS2ERR_ENC
 *  - @b 15 : units.c      - DAS2ERR_UNITS
 *  - @b 16 : descriptor.c - DAS2ERR_DESC
 *  - @b 17 : plane.c      - DAS2ERR_PLANE
 *  - @b 18 : packet.c     - DAS2ERR_PKT
 *  - @b 19 : stream.c     - DAS2ERR_STREAM
 *  - @b 20 : oob.c        - DAS2ERR_OOB
 *  - @b 22 : das2io.c     - DAS2ERR_IO
 *  - @b 23 : dsdf.c       - DAS2ERR_DSDF
 *  - @b 24 : dft.c        - DAS2ERR_DFT
 *  - @b 25 : log.c        - DAS2ERR_LOG
 * 
 * @param nErrCode The value to return to the shell, should be one of the above.
 * @param sFmt An fprintf style format string
 * @return By default this function never returns but if das2_continue_on_error
 *         has been called then the value of nErrCode is returned.
 */
#define das2_error(nErrCode, ...)\
  das2_error_func(__FILE__, __func__, __LINE__, nErrCode, __VA_ARGS__ )


/** Error handling: Trigger Core Dumps
 * 
 * Call this function to have the library exit via an abort() call instead of
 * using exit(ErrorCode).  On most systems this will trigger the generation of
 * a core file that can be used for debugging.  
 * @warning: Calling this function prevents open file handles from being flushed
 *           to disk which will typically result in corrupted output.
 */
void das2_abort_on_error(void);

/** Error handling: Normal Exit
 * Set the library to call exit(ErrorCode) when a problem is detected.  This is
 * usually what you want and the library's default setting.
 */
void das2_exit_on_error(void);

/** Error handling: Normal Return
 * Set the library to return normally to the calling function with a return value
 * that indicates a problem has occurred.  This will be the new default, but is
 * not yet tested.   
 */
void das2_return_on_error(void);


#define DAS2_ERRDIS_RET   0
#define DAS2_ERRDIS_EXIT  1
#define DAS2_ERRDIS_ABORT 43

/** Error handling: get the libraries error disposition
 * @returns one of the following integers:
 *    - DAS2_ERRDIS_EXIT - Libary exits when there is a problem
 *    - DAS2_ERRDIS_ABORT - Library aborts, possibly with core dump on a problem
 *    - DAS2_ERRDIS_RET - Library returns normally with an error code
 */
int das2_error_disposition(void);

/** Error handling: Print formatted error to standard error stream
 * Set the library to ouput formatted error messages to the processes 
 * standard error stream. This is the default.
 */
void das2_print_error(void);

/** Error handling: Save formatted error in a message buffer.
 * Set the library to save formatted error message to a message buffer.
 *
 * @param maxmsg maximum message size. The buffer created will be maxmsg in
 *        length, meaning any formatted messages longer than the available
 *        buffer size will be truncated to maxmsg-1
 */
void das2_save_error(int maxmsg);

/** Return the saved das2 error message buffer.
 * @returns an instance of Das2ErrorMessage. The struct returned contains
 *          the error code, formatted message, max message size, and the 
 *          source file, function name, and line number of where the 
 *          message originated.
 */
Das2ErrorMessage* das2_get_error(void);

/** Convert a string value to a 8-byte float, similar to strtod(3).
 *
 * @param str the string to convert.  Conversion stops at the first improper
 *        character.  Whitespace and leading 0's are ignored in the input.
 *        The number is assumed to be in base 10, unless the first non-whitespace
 *        characters after the option as '+' or '-' sign are '0x'.
 *
 * @param pRes The location to store the resulting 8-byte float.
 *
 * @returns @c true if the conversion succeeded, @c false otherwise.  Among 
 *        other reason, conversion will fail if the resulting value won't fit
 *        in a 8 byte float.
 */
bool das2_str2double(const char* str, double* pRes);


/** Convert a string value to an integer with explicit over/underflow checks
 * 
 * @param str the string to convert.  Conversion stops at the first improper
 *        character.  Whitespace and leading 0's are ignored in the input.
 *        The number is assumed to be in base 10, unless the first non-whitespace
 *        characters after the optional '+' or '-' sign are '0x'.
 *
 * @param pRes The location to store the resulting integer.
 *
 * @returns @c true if the conversion succeeded, @c false otherwise. 
 */
bool das2_str2int(const char* str, int* pRes);

/** Convert a string value to a boolean value.
 * 
 * @param str the string to convert.  The following values are accepted as
 *        representing true:  'true' (any case), 'yes' (any case), 'T', 'Y',
 *        '1'.  The following values are accepted as representing false:
 *        'false' (any case), 'no', (any case), 'F', 'N', '0'.  Anything else
 *        results in no conversion.
 * @param pRes the location to store the resulting boolean value
 * @return true if the string could be converted to a boolean, false otherwise.
 */
bool das2_str2bool(const char* str, bool* pRes);

/** Convert a string to an integer with explicit base and overflow
 * checking.
 *
 * @param str the string to convert.  Conversion stops at the first improper
 *        character.  Whitespace and leading 0's are ignored in the input.
 *        No assumptions are made about the base of the string.  So anything
 *        that is not a proper character is the given base is causes an 
 *        error return. 
 *
 * @param base an integer from 1 to 60 inclusive.
 *
 * @param pRes The location to store the resulting integer.
 *
 * @returns @c true if the conversion succeeded, @c false otherwise. 
 */
bool das2_str2baseint(const char* str, int base, int* pRes);

/** Convert an explicit length string to an integer with explicit base with
 * over/underflow checks.
 *
 * @param str the string to convert.  Conversion stops at the first improper
 *        character.  Whitespace and leading 0's are ignored in the input.
 *        No assumptions are made about the base of the string.  So anything
 *        that is not a proper character is the given base is causes an 
 *        error return. 
 *
 * @param base an integer from 1 to 60 inclusive.
 *
 * @param nLen only look at up to this many characters of input.  Encountering
 *        whitespace or a '\\0' characater will still halt character 
 *        accumlation.
 *
 * @param pRes The location to store the resulting integer.
 *
 * @returns @c true if the conversion succeeded, @c false otherwise.
 *
 * Will only inspect up to 64 non-whitespace characters when converting a
 * value.
 */
bool das2_strn2baseint(const char* str, int nLen, int base, int* pRes);


typedef struct das_real_array{
	double* values;
	size_t length;
} das_real_array;

typedef struct das_creal_array{
	const double* values;
	size_t length;
} das_creal_array;

typedef struct das_int_array{
	int* values;
	size_t length;
} das_int_array;

typedef struct das_cint_array{
	const int* values;
	size_t length;
} das_cint_array;

/** Parse a comma separated list of ASCII values into a double array.
 * @param[in] s The string of comma separated values
 * @param[out] nitems a pointer to an integer which will be set to the 
 *             length of the newly allocated array.
 *
 * @returns a new double array allocated on the heap.
 */
double* das2_csv2doubles(const char * s, int* nitems);


/** Print an array of doubles into a string buffer.
 * Prints an array of doubles into a string buffer with commas and spaces
 * between each entry.  Note there is no precision limit for the printing
 * so the space needed to hold the array may 24 bytes times the number
 * number of values, or more.
 *
 * @todo this function is a potential source of buffer overruns, fix it.
 *
 * @param[out] buf a pointer to the buffer to receive the printed values
 * @param[in] value an array of doubles
 * @param[in] nitems the number of items to print to the array
 *
 * @returns A pointer to the supplied buffer.
 */
char* das2_doubles2csv( char * buf, const double * value, int nitems );


/** limit of number of properties per descriptor. */
#define XML_MAXPROPERTIES 100

/** The limit on xml packet length, in bytes.  (ascii encoding.) */
#define XML_BUFFER_LENGTH 1000000

/** The limit of xml element name length, in bytes. */
#define XML_ELEMENT_NAME_LENGTH 256

/** maximum size of arrays for yTags, properties, etc  */
#define MAX_ARRAY_SIZE 1000

/** Get the library version
 *  
 * @returns the version tag string for the das2 core library, or 
 * the string "untagged" if the version is unknown
 */
const char* das2_lib_version( void );

/** Store string in a buffer that is reallocated if need be
 * 
 * @param psDest a pointer to the storage location
 * @param puLen a pointer to the size of the storage location
 * @param sSrc the source string to store.
 */
void das2_store_str(char** psDest, size_t* puLen, const char* sSrc);

/** Allocate a new string on the heap and format it
 *
 * Except for using das2_error on a failure, this is a copy of the
 * code out of man 3 printf on Linux.
 *
 * @returns A pointer to the newly allocated and formatted string on
 *          the heap, or NULL if the function failed and the das2 error
 *          disposition allows for continuation after a failure
 */
char* das2_string(const char* fmt, ...); 

/** Store a formatted string in a newly allocated buffer 
 *
 * This version is suitable for calling from variable argument functions.
 *
 * Except for using das2_error on a failure, this is a copy of the
 * code out of man 3 printf on Linux.
 *
 * @param fmt - a printf format string
 * @param ap A va_list list, see vfprintf or stdarg.h for details
 *
 * @returns A pointer to the newly allocated and formatted string on
 *          the heap, or NULL if the function failed and the das2 error
 *          disposition allows for continuation after a failure
 */
char* das2_vstring(const char* fmt, va_list ap);



/** Is the path a directory.
 * @param path The directory in question, passed to stat(2)
 * @return true if @b path can be determined to be a directory, false otherwise 
 */
bool das2_isdir(const char* path);

/** Is the path a file.
 * @param path The file in question, passed to stat(2)
 * @return true if @b path can be determined to be a file, false otherwise 
 */
bool das2_isfile(const char* path);

/** Get a sorted directory listing 
 * 
 * @param sPath    The path to the directory to read.
 * 
 * @param ppDirList A pointer to a 2-D character array where the first index is
 *                 the directory item and the second index is the character 
 *                 position.  The max value of the second index @b must be 
 *                 = NAME_MAX - 1. The value NAME_MAX is defined in the POSIX
 *                 header limits.h
 * 
 * @param uMaxDirs The maximum number of directory entries that may be stored
 * * 
 * @param cType May be used to filter the items returned.  If cType = 'f' only
 *        files will be return, if cType = 'd' then only directories will be
 *        returned.  Any other value, including 0 will return both.
 * 
 * @return On success the number of items in the directory not counting '.' and 
 *         '..' are returned, on failure a negative error code is returned. 
 *         Item names are sorted before return.
 */
int das2_dirlist( 
	const char* sPath, char ppDirList[][NAME_MAX], size_t uMaxDirs, char cType
);


#endif /* _das2_util_h_ */
