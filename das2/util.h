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

#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <stdio.h>
#include <stdarg.h>
#include <limits.h>
#include <sys/stat.h>
#include <sys/types.h>

/* Get compile time byte order, results in faster code that avoids 
 * runtime checks.  For some newer chips this may not work as the
 * processor can be switched from big endian to little endian at runtime.
 *
 * At the end of the day either HOST_IS_LSB_FIRST will be defined, or it won't.
 * If this macro is defined then the host computer stores the least significant
 * byte of a word in the lowest address, i.e. it's a little endian machine.  If
 * this macro is not defined then the host computer stores the list significant
 * byte of a word in the highest address, i.e. it a big endian machine.
 */
 
#if defined __linux || defined __APPLE__

#if defined __linux
#include <endian.h>
#elif defined __APPLE__
#include <machine/endian.h>
#endif

#if __BYTE_ORDER == __LITTLE_ENDIAN
#define HOST_IS_LSB_FIRST
#else
#undef HOST_IS_LSB_FIRST
#endif

#else /* End Linux Section */

#ifdef __sun

#include <sys/isa_defs.h>
#ifdef _LITTLE_ENDIAN
#define HOST_IS_LSB_FIRST
#else
#undef HOST_IS_LSB_FIRST
#endif

#else

#ifdef _WIN32

/** This computer is a little endian machine, macro is not present on big
 * endian machines. 
 */
#define HOST_IS_LSB_FIRST

#else

#error "unknown byte order!"

#endif /* _WIN32 */
#endif  /* __sun */
#endif  /* __linux */

/* Setup the DLL macros for windows */
#if defined(_WIN32) && defined(DAS_USE_DLL)
#  ifdef BUILDING_DLL
#    define DAS_API __declspec(dllexport)
#  else
#    define DAS_API __declspec(dllimport)
#  endif
#else
#define DAS_API
#endif

#ifdef __cplusplus
extern "C" {
#endif

/** @defgroup utilities Utilities
 * Library initialization, error handling, logging and a few minor libc extensions
 */

	
/** @addtogroup utilities
 * @{
 */

	
#define DAS_22_STREAM_VER "2.2"


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

/** return code type
 * 0 indicates success, negative integer indicates failure
 */
typedef int DasErrCode;

/** success return code */
#define DAS_OKAY 0

/** Used to indicate that errors should trigger program exit */
#define DASERR_DIS_EXIT  0

/** Used to indicate that errors should trigger library functions to return error values */
#define DASERR_DIS_RET   1

/** Used to indicate that errors should trigger program abort with a core dump */
#define DASERR_DIS_ABORT 43

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
 * them in three areas:
 *
 *   * Error and log handling - Since the error and logging disposition should
 *     be the same for all library calls handlers are set here
 *
 *   * Unit conversions - Since das_unit varibles should be comparible using a
 *     simple equality test, a global registry of const char pointers is needed
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

#define DASERR_NOTIMP  8
#define DASERR_ASSERT  9
#define DASERR_INIT   11
#define DASERR_BUF    12
#define DASERR_UTIL   13
#define DASERR_ENC    14
#define DASERR_UNITS  15
#define DASERR_DESC   16
#define DASERR_PLANE  17
#define DASERR_PKT    18
#define DASERR_STREAM 19
#define DASERR_OOB    20
#define DASERR_IO     22
#define DASERR_DSDF   23
#define DASERR_DFT    24
#define DASERR_LOG    25
#define DASERR_ARRAY  26
#define DASERR_VAR    27
#define DASERR_DIM    28
#define DASERR_DS     29
#define DASERR_BLDR   30
#define DASERR_HTTP   31
#define DASERR_DATUM  32
#define DASERR_VALUE  33
#define DASERR_OP     34
#define DASERR_CRED   35
#define DASERR_NODE   36
#define DASERR_MAX    36


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

/** limit of number of properties per descriptor. */
#define DAS_XML_MAXPROPS 400

/** The limit on xml packet length, in bytes.  (ascii encoding.) */
#define DAS_XML_BUF_LEN 1000000

/** The limit of xml element name length, in bytes. */
#define DAS_XML_NODE_NAME_LEN 256

/** Get the library version
 *
 * @returns the version tag string for the das2 core library, or
 * the string "untagged" if the version is unknown
 */
DAS_API const char* das_lib_version( void );

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

/** @} */


#ifdef __cplusplus
}
#endif

#endif /* _das_util_h_ */
