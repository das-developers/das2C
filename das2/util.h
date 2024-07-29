/* Copyright (C) 1997-2020 Chris Piker <chris-piker@uiowa.edu>
 *                         Larry Granroth <larry-granroth@uiowa.edu> 
 *                         Jeremy Faden <jeremy-faden@uiowa.edu>
 *
 * This file is part of libdas2, the Core Das2 C Library.
 *
 * Libdas2 is free software; you can redistribute it and/or modify it under
 * the terms of the GNU Lesser General Public License version 2.1 as published
 * by the Free Software Foundation.
 *
 * Libdas2 is distributed in the hope that it will be useful, but WITHOUT ANY
 * WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS
 * FOR A PARTICULAR PURPOSE.  See the GNU Lesser General Public License for
 * more details.
 *
 * You should have received a copy of the GNU Lesser General Public License
 * version 2.1 along with libdas2; if not, see <http://www.gnu.org/licenses/>.
 */

/** @file util.h */

#ifndef _das_util_h_
#define _das_util_h_

#include <stdlib.h>
#include <stdio.h>
#include <stdarg.h>
#include <sys/stat.h>
#include <sys/types.h>

#include <das2/defs.h>

/** Used to indicate that errors should trigger program exit */
#define DASERR_DIS_EXIT  0

/** Used to indicate that errors should trigger library functions to return error values */
#define DASERR_DIS_RET   1

/** Used to indicate that errors should trigger program abort with a core dump */
#define DASERR_DIS_ABORT 43

#ifdef __cplusplus
extern "C" {
#endif

/** Definition of a message handler function pointer.
 * Message handlers need to be prepared for any of the string pointers
 * sMsg, sDataStatus, or sStackTrace to be null.
 *
 * @param nLevel The message level.  If nLevel is equal to or greater than
 *  das_log_getlevel() then the message should be logged.
 *
 * @param sMsg The message, usually not null.
 *
 * @param bPrnTime The current system time should be included in the log
 *        output.
 */
typedef void (*das_log_handler_t)(int nLevel, const char* sMsg, bool bPrnTime);

/** Initialize any global structures in the Das2 library.
 *
 * This should be the first function your program calls before using any libdas2
 * functions.  In general libdas2 tries to avoid global structures but does use
 * them in following areas:
 *
 *   * Error and log handling - Since the error and logging disposition should
 *     be the same for all library calls handlers are set here
 *
 *   * Unit conversions - Since das_unit varibles should be comparible using a
 *     simple equality test, a global registry of const char pointers is needed
 *
 *   * TT2000 leapsecond table - To avoid rebuilding the library after each
 *     leapsocond is announced, an external table defined by the environment
 *     variable CDF_LEAPSECONDTABLE is loaded, if the variable is defined.
 *
 *   * FFTW plan mutexes - Since the FFTW library unfortunatly uses global
 *     plan memory
 *
 *   * OpenSSL Contex mutexes - The openssl library contex cannot be changed
 *     by multiple threads at the same time, a mutex is setup to prevent this
 *     from happening
 *
 * This function initializes defaults for the items above.
 *
 * @param sProgName The name of the program using the library.  Used in
 *        some error messages.
 *
 * @param nErrDis Set the behavior the library takes when an error is
 *        encountered.  May be one of DASERR_DIS_EXIT, call exit() when an
 *        error occurs; DASERR_DIS_RET, return with an error code; or
 *        DASERR_DIS_ABORT, call abort().  The value of DASERR_DIS_EXIT is
 *        0 so you can use that for the default behavior.  If DASERR_DIS_RET is
 *        used, the function das_get_error() can be used to retrieve the most
 *        recent error message.
 *
 * @param nErrBufSz If not zero, a global error message buffer will be allocated
 *        that is this many bytes long and error message will be saved into the
 *        buffer instead of being sent to the standard error channel.  Messages
 *        can be retrieved via das_get_error().
 *        If zero, these will be send to the standard error channel as soon as
 *        they occur.  Saving errors is only useful if the error disposition is
 *        DAS2_ERRDIS_RET as otherwise the program exits before the message can
 *        be output.
 *
 * @param nLevel Set the logging level to one of, DASLOG_TRACE, DASLOG_DEBUG,
 *        DASLOG_NOTICE, DASLOG_WARN, DASLOG_ERROR, DASLOG_CRITICAL.
 *
 * @param logfunc A callback for handling log messages.  The callback need not
 *        be thread safe as it will only be triggered inside mutual exclusion
 *        (mutex) locks.  If NULL messages are printed to the stardard error
 *        channel.
 *
 * The error disposition does not affect any errors that are encountered within
 * das_init.  Errors should not occur during initialization, any that do
 * trigger a call to exit()
 */
DAS_API void das_init(
	const char* sProgName, int nErrDis, int nErrBufSz, int nLevel, 
	das_log_handler_t logfunc
);

/** Return the version of this library */
DAS_API const char* das_version();

/** A do nothing function on Unix, closes network sockets on windows */
DAS_API void das_finish(void);

DasErrCode das_error_func(
	const char* sFile, const char* sFunc, int nLine, DasErrCode nCode,
	const char* sFmt, ...
);

DasErrCode das_error_func_fixed(
	const char* sFile, const char* sFunc, int nLine, DasErrCode nCode,
	const char* sMsg
);

/** Signal an error condition.
 *
 * This routine is called throughout the code when an error condition arrises.
 *
 * The default handler for error conditions prints the message provided to
 * the standard error channel and then calls exit(nErrCode).  To have the library
 * call your handler instead use the das_set_error_handler() function.  To have
 * the library abort with a core dump on an error use das_abort_on_error().
 *
 * Each source file in the code has it's own error code.  Though it's probably
 * not that useful to end users, the codes are provided here:
 *
 *  - @b  8 : Not yet implemented - DASERR_NOTIMP
 *  - @b  9 : Assertion Failures  - DASERR_ASSERT
 *  - @b 10 : das1.c        - D1ERR
 *  - @b 11 : Lib initialization errors - DASERR_INIT
 *  - @b 12 : buffer.c      - DASERR_BUF    
 *  - @b 13 : util.c        - DASERR_UTIL   
 *  - @b 14 : encoding.c    - DASERR_ENC    
 *  - @b 15 : units.c       - DASERR_UNITS  
 *  - @b 16 : descriptor.c  - DASERR_DESC   
 *  - @b 17 : plane.c       - DASERR_PLANE  
 *  - @b 18 : packet.c      - DASERR_PKT    
 *  - @b 19 : stream.c      - DASERR_STREAM 
 *  - @b 20 : oob.c         - DASERR_OOB    
 *  - @b 21 : io.c          - DASERR_IO     
 *  - @b 22 : dsdf.c        - DASERR_DSDF   
 *  - @b 23 : dft.c         - DASERR_DFT    
 *  - @b 24 : log.c         - DASERR_LOG    
 *  - @b 25 : array.c       - DASERR_ARRAY  
 *  - @b 26 : variable.c    - DASERR_VAR    
 *  - @b 27 : dimension.c   - DASERR_DIM    
 *  - @b 28 : dataset.c     - DASERR_DS     
 *  - @b 29 : builder.c     - DASERR_BLDR   
 *  - @b 30 : http.c        - DASERR_HTTP   
 *  - @b 31 : datum.c       - DASERR_DATUM  
 *  - @b 32 : value.c       - DASERR_VALUE  
 *  - @b 34 : operater.c    - DASERR_OP
 *  - @b 35 : credentials.c - DASERR_CRED
 *  - @b 36 : catalog.c     - DASERR_CAT
 *  - @b 37 : property.c    - DASERR_PROP
 * 
 * Application programs are recommended to use values 64 and above to avoid
 * colliding with future das2 error codes.
 *
 * @param nErrCode The value to return to the shell, should be one of the above.
 * @return By default this function never returns but if the libdas2 error
 *         disposition has been set to DAS2_ERRDIS_RET then the value of
 *         nErrCode is returned.
 */
#define das_error(nErrCode, ...) \
  das_error_func(__FILE__, __func__, __LINE__, nErrCode, __VA_ARGS__ )


/** Error handling: Trigger Core Dumps
 *
 * Call this function to have the library exit via an abort() call instead of
 * using exit(ErrorCode).  On most systems this will trigger the generation of
 * a core file that can be used for debugging.
 * @warning: Calling this function prevents open file handles from being flushed
 *           to disk which will typically result in corrupted output.
 */
DAS_API void das_abort_on_error(void);

/** Error handling: Normal Exit
 * Set the library to call exit(ErrorCode) when a problem is detected.  This is
 * usually what you want and the library's default setting.
 */
DAS_API void das_exit_on_error(void);

/** Error handling: Normal Return
 * Set the library to return normally to the calling function with a return value
 * that indicates a problem has occurred.  This will be the new default, but is
 * not yet tested.
 */
DAS_API void das_return_on_error(void);

/** Error handling: get the library's error disposition
 * @returns one of the following integers:
 *    - DAS2_ERRDIS_EXIT - Library exits when there is a problem
 *    - DAS2_ERRDIS_ABORT - Library aborts, possibly with core dump on a problem
 *    - DAS2_ERRDIS_RET - Library returns normally with an error code
 */
DAS_API int das_error_disposition(void);

/** Used for co-operative locking of time-limited error disposition changse.
 * 
 * Aquire this lock before your critical section, then release it.
 * All code that want's to toggle the error disposition should use this,
 * but it's not enforcable, except by code review.
 * 
 * YOU MUST BE SURE YOUR FUNCTION CAN'T EXIT BEFORE THE LOCK IS RELEASED!
 */
DAS_API void das_errdisp_get_lock();

/** Used for co-operative locking of time-limited error disposition changse.
 * 
 * Release this lock before your critical section, then release it.
 * All code that want's to toggle the error disposition should use this,
 * but it's not enforcable, except by code review.
 */
DAS_API void das_errdisp_release_lock();



/** The inverse of das_error_disposition.
 * @param nDisp One of DAS2_ERRDIS_EXIT, DAS2_ERRDIS_ABORT, DAS2_ERRDIS_RET
 */
DAS_API void das_error_setdisp(int nDisp);

/** Error handling: Print formatted error to standard error stream
 * Set the library to ouput formatted error messages to the processes
 * standard error stream. This is the default.
 */
DAS_API void das_print_error(void);

/** Error handling: Save formatted error in a message buffer.
 * Set the library to save formatted error message to a message buffer.
 *
 * @param maxmsg maximum message size. The buffer created will be maxmsg in
 *        length, meaning any formatted messages longer than the available
 *        buffer size will be truncated to maxmsg-1
 *
 * @returns true if error buffer setup was successful, false otherwise.
 */
DAS_API bool das_save_error(int maxmsg);

/** Structure returned from das_get_error().
 *
 * To get error messages libdas2 must be set to an error dispostition of
 * DAS2_ERRDIS_RET
 */
typedef struct das_error_message {
	int nErr;
	char * message;
	size_t maxmsg;
	char sFile[256];
	char sFunc[64];
	int nLine;
} das_error_msg;


/** Return the saved das2 error message buffer.
 * @returns an instance of Das2ErrorMessage. The struct returned contains
 *          the error code, formatted message, max message size, and the
 *          source file, function name, and line number of where the
 *          message originated.
 * @memberof das_error_msg
 */
DAS_API das_error_msg* das_get_error(void);

/** Free an error message structure allocated on the heap
 *
 * @param pMsg the message buffer to free
 * @memberof das_error_msg
 */
DAS_API void das_error_free(das_error_msg* pMsg);

/** Check to see if two floating point values are within an epsilon of each
 * other */
#define das_within(A, B, E) (fabs(A - B) < E ? true : false)

/** The limit on xml packet length, in bytes.  (ascii encoding.) */
#define DAS_XML_BUF_LEN 1000000

/** The limit of xml element name length, in bytes. */
#define DAS_XML_NODE_NAME_LEN 256

/** Get the library version
 *
 * @returns the version tag string for the das2 core library, or
 * the string "untagged" if the version is unknown
 */
DAS_API const char* das_lib_version(void);

/** The size of an char buffer large enough to hold valid object IDs */
#define DAS_MAX_ID_BUFSZ 64

/** Check that a string is suitable for use as an object ID
 * 
 * Object ID strings are ascii strings using only characters from the set
 * a-z, A-Z, 0-9, and _.  They do not start with a number.  They are no more
 * than 63 bytes long.  Basically they can be used as variable names in most
 * programming languages.
 * 
 * If the das_error_disposition is set to exit this function never returns.
 *  
 * @param sId
 * @return True if the string can be used as an ID, false otherwise.
 */
DAS_API bool das_assert_valid_id(const char* sId);


/** Store string in a buffer that is reallocated if need be
 *
 * @param psDest a pointer to the storage location
 * @param puLen a pointer to the size of the storage location
 * @param sSrc the source string to store.
 */
DAS_API void das_store_str(char** psDest, size_t* puLen, const char* sSrc);

/** Allocate a new string on the heap and format it
 *
 * Except for using das_error on a failure, this is a copy of the
 * code out of man 3 printf on Linux.
 *
 * @returns A pointer to the newly allocated and formatted string on
 *          the heap, or NULL if the function failed and the das2 error
 *          disposition allows for continuation after a failure
 */
DAS_API char* das_string(const char* fmt, ...);

/** Strip whitespace from a string
 * 
 * @param sLine - Pointer to a writable null terminated string, or a
 *          NULL pointer.
 * 
 * @param cComment - Treat the character if it was a null character
 *          terminating the string.  Replace it with an actual null if
 *          found. Use '\0' to avoid stripping "comments".
 * 
 * @returns NULL if the string consisted only of whitespace, or was empty
 *          or was a NULL pointer. 
 *          Otherwise a pointer to the first non-whitespace portion is 
 *          returned.  A null character will be written to the string
 *          at the start of trailing whitespace.
 */
DAS_API char* das_strip(char* sLine, char cComment);

/** Copy string as an XML token
 * 
 * Copy a string ignoring leading and traily spaces.  Internal whitespace
 * characters are converted to regular spaces and collapsed
 *
 * @param dest The buffer to receive string data, may be UTF-8 if desired
 *             The output string is always null terminated if n > 1
 *
 * @param src the source string, may be UTF-8 if desired
 *
 * @param n the size of the dest buffer
 * 
 * @returns the equivalent of strlen(dest).
 */
DAS_API size_t das_tokncpy(char* dest, const char* src, size_t n);


/** Translate unsafe characters for XML string output 
 *
 * At present only the characters set [ " < > & ] are translated. All white
 * space characters are preserved. So long as dest is at least one byte long
 * the result is null terminated.
 *
 * @param dest The buffer to receive the output
 * @param src  The data source
 * @param uOutLen the size of the dest buffer.  
 *
 * @returns the parameter dest cast to a constant pointer.
 */
DAS_API const char* das_xml_escape(char* dest, const char* src, size_t uOutLen);

/** Copy a string into a new buffer allocated on the heap
 *
 * @param sIn the string to copy
 * @return a pointer to the newly allocated buffer containing the same
 *          characters as the input string or NULL if the input length was
 *          zero
 */
DAS_API char* das_strdup(const char* sIn);

/** A memset that handles multi-byte items  
 *              
 * Uses memcpy because the amount of data written in each call goes up
 * exponentially and memcpy is freaking fast, much faster than a linear
 * write loop for large arrays.
 * 
 * @param pDest The destination area must not overlap with pSrc
 * @param pSrc  A location for an individual element to repeat in pDest
 * @param uElemSz The size in bytes of a single element
 * @param uCount The number of elements to repeat in pDest
 * @return The input pDest pointer.  There is no provision for a NULL return
 *         as this function should not fail since the memory is pre-allocated
 *         by the caller
 * 
 */
DAS_API uint8_t* das_memset(
	uint8_t* pDest, const uint8_t* pSrc, size_t uElemSz, size_t uCount
);



/** Store a formatted string in a newly allocated buffer
 *
 * This version is suitable for calling from variable argument functions.
 *
 * Except for using das_error on a failure, this is a copy of the
 * code out of man 3 printf on Linux.
 *
 * @param fmt - a printf format string
 * @param ap A va_list list, see vfprintf or stdarg.h for details
 *
 * @returns A pointer to the newly allocated and formatted string on
 *          the heap, or NULL if the function failed and the das2 error
 *          disposition allows for continuation after a failure
 */
DAS_API char* das_vstring(const char* fmt, va_list ap);


/** Is the path a directory.
 * @param path The directory in question, passed to stat(2)
 * @return true if @b path can be determined to be a directory, false otherwise
 */
DAS_API bool das_isdir(const char* path);

/** Insure directories to a specific location exist 
 * 
 * @param sPath a full path to a file.  If this string contains no 
 *        path separators, the function does nothing.
 * 
 * @return true if directories up to the final location either exist, or 
 *        could be generated, false otherwise.
 */
DAS_API DasErrCode das_mkdirsto(const char* path);


/** Get the home directory for the current account 
 * 
 * @return A pointer to a global string that is the current user's
 *         home directory, or other likely writable location if
 *         the home directory could not be determined.
 */
DAS_API const char* das_userhome(void);

/** Copy a file to a distination creating directories as needed. 
 *
 * If the files exists at the destination it in overwritten.  Directories are
 * created as needed.  Directory permissions are are the same as the file
 * with the addition that for each READ permission in the mode, directory 
 * EXEC permission is added.
 *
 * @param src - name of file to copy
 * @param dest - name of destination
 * @param mode - the permission mode of the destitation file, 0664 is 
 *               recommened if you can descide on the output permissions mode.
 *               (mode argument not present in WIN32 version)
 *
 * @returns - true if the copy was successful, false otherwise.
 * 
 */
#ifdef _WIN32
DAS_API bool das_copyfile(const char* src, const char* dest);
#else
DAS_API bool das_copyfile(const char* src, const char* dest, mode_t mode);
#endif


/** Is the path a file.
 * @param path The file in question, passed to stat(2)
 * @return true if @b path can be determined to be a file, false otherwise
 */
DAS_API bool das_isfile(const char* path);

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
DAS_API int das_dirlist(
	const char* sPath, char ppDirList[][256], size_t uMaxDirs, char cType
);

/** A C locale string to double converter 
 * 
 * This is essentially the same as the C-lib function strtod, except it always
 * evaluates strings in the "C" locale no matter the current locale of the 
 * program.  It does not alter the current program's locale.
 * 
 * @param nptr    The starting point of the string to convert
 * @param endptr  Where to store a pointer to the last item converted
 * @return        The converted value 
 * 
 * Notes: Errno is set as normal for the underlying strtod implementation.
 */
DAS_API double das_strtod_c(const char *nptr, char **endptr);

/** @} */


#ifdef __cplusplus
}
#endif

#endif /* _das_util_h_ */
