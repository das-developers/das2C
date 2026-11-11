/* Copyright (C) 1997-2020 Chris Piker <chris-piker@uiowa.edu>
 *                         Larry Granroth <larry-granroth@uiowa.edu> 
 *                         Jeremy Faden <jeremy-faden@uiowa.edu>
 *
 * This file is part of das2C, the Core Das C Library.
 *
 * Das2C is free software; you can redistribute it and/or modify it under
 * the terms of the GNU Lesser General Public License version 2.1 as published
 * by the Free Software Foundation.
 *
 * Das2C is distributed in the hope that it will be useful, but WITHOUT ANY
 * WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS
 * FOR A PARTICULAR PURPOSE.  See the GNU Lesser General Public License for
 * more details.
 *
 * You should have received a copy of the GNU Lesser General Public License
 * version 2.1 along with das2C; if not, see <http://www.gnu.org/licenses/>.
 */

/** @file util.h */

#ifndef _das_util_h_
#define _das_util_h_

#include <stdlib.h>
#include <stdio.h>
#include <stdarg.h>
#include <sys/stat.h>
#include <sys/types.h>

#include <das3/defs.h>
#include <das3/log.h>

#ifdef __cplusplus
extern "C" {
#endif

/** @addtogroup utilities
 * @{
 */

/** What daserrf() does after logging, see daslib_init() */
typedef enum das_errdisp_e {
	
	/** Errors end the program: daserrf() logs at DASLOG_CRITICAL, then calls
	 *  exit().  The default. */
	DASERRDISP_EXIT = 0,
	
	/** Errors are returned: daserrf() logs at DASLOG_ERROR, then returns
	 *  its code */
	DASERRDISP_RET = 1
	
} das_errdisp;

/** Initialize any global structures in the Das2 library.
 *
 * This should be the first function your program calls before using any Das2C
 * functions.  In general Das2C tries to avoid global structures but does use
 * them in following areas:
 *
 *   * Error and log handling - Since the error and logging disposition should
 *     be the same for all library calls handlers are set here
 *
 *   * Unit conversions - Since das_unit variables should be comparable using a
 *     simple equality test, a global registry of const char pointers is needed
 *
 *   * TT2000 leapsecond table - To avoid rebuilding the library after each
 *     leapsecond is announced, an external table defined by the environment
 *     variable CDF_LEAPSECONDSTABLE is loaded, if the variable is defined.
 *
 *   * FFTW plan mutexes - Since the FFTW library unfortunately uses global
 *     plan memory
 *
 *   * OpenSSL Context mutexes - The openssl library context cannot be changed
 *     by multiple threads at the same time, a mutex is setup to prevent this
 *     from happening
 *
 * This function initializes defaults for the items above.  Call it once.
 *
 * Most programs end on a library error and take the defaults:
 * @code
 *   daslib_init(argv[0], DASERRDISP_EXIT, DASLOG_INFO, NULL);
 * @endcode
 * A library binding returns on errors and sends messages to its host:
 * @code
 *   DasLogHook hook = {myHandler, &myState};
 *   if(daslib_init("mymodule", DASERRDISP_RET, DASLOG_INFO, &hook) != DAS_OKAY)
 *      return NULL;
 * @endcode
 * When setup fails the hook has already been told why.
 *
 * The hook is installed before anything else is set up, so a failure inside
 * daslib_init() is reported through it, and is handled by the error
 * disposition like any other error.
 *
 * @param sProgName The name of the program using the library.  Used in
 *        some error messages.
 *
 * @param nErrDis What daserrf() does after logging: DASERRDISP_EXIT, end
 *        the program, or DASERRDISP_RET, return the error code to the caller.
 *        Any other value is an error, handled as DASERRDISP_EXIT.
 *
 * @param nLevel The log level, one of DASLOG_TRACE, DASLOG_DEBUG,
 *        DASLOG_INFO, DASLOG_WARN, DASLOG_ERROR, DASLOG_CRITICAL.  With
 *        DASERRDISP_RET a level above DASLOG_ERROR is an error, since it
 *        would hide every error message; daslog_setLevel() can still do that
 *        on purpose later.
 *
 * @param pHook The log hook, copied in.  NULL for the default, which prints
 *        to standard error.  See DasLogHook for what a handler may and may
 *        not do.
 *
 * @returns DAS_OKAY, or with DASERRDISP_RET, DASERR_INIT if setup failed.
 */
DAS_API das_errcode daslib_init(
	const char* sProgName, das_errdisp nErrDis, int nLevel,
	const DasLogHook* pHook
);

/** Get the library version
 *
 * The git description of the source the library was built from.  A release
 * reads as its tag, "v3.0.0".  A development build reads as the last tag,
 * the commits since it and the commit hash, "v3.0-pre7-94-g75aff62", with
 * "-dirty" added when it was built from uncommitted changes.
 *
 * @returns the version string, or "untagged" if the build could not tell
 */
DAS_API const char* daslib_version(void);

/** One limit of this build of the library
 *
 * Some limits come from the das2 and das3 formats, others are choices made
 * when the library was compiled, such as the largest packet it will read
 * or write. This structure provides an interface to compiled in limits
 * at runtime.  An example of listing all builtin limits for the library
 * follows.
 *
 * @code
 *   const DasLimit* pLimit = DasLimit_lookup("max_packet_bytes");
 *   if((pLimit != NULL) && (uMyPacket > (size_t)pLimit->value))
 *      fprintf(stderr, "Packet too big for this das library\n");
 *
 *   // Or list them all, without knowing their names
 *   for(pLimit = DasLimit_next(NULL); pLimit != NULL;
 *       pLimit = DasLimit_next(pLimit)
 *   ){
 *      printf("%s = %d\n", pLimit->name, (int)pLimit->value);
 *   }
 * @endcode
 *
 * Limit names are stable: new ones may be added, existing names are
 * not removed.  The limit names current defined are:
 *
 *   - @b max_packet_bytes      Largest das3 packet read or written
 *   - @b das2_max_header_bytes Largest das2 header or out-of-band packet,
 *                              set by the das2 six digit length field
 *   - @b das2_max_packet_id    Largest das2 packet ID
 *   - @b das3_max_packet_id    Largest das3 packet ID this build accepts
 *   - @b max_prop_name_bytes   Longest property name
 *   - @b max_prop_value_bytes  Longest property value
 *   - @b max_dataset_rank      Most index dimensions in a dataset
 *   - @b das2_max_item_bytes   Widest das2 text or time item
 *
 * Only take entries from DasLimit_lookup() and DasLimit_next(), and don't
 * step from one entry to the next with pointer arithmetic. 
 *
 * The internal table is constant, so these calls are thread safe and may be
 * made before daslib_init().
 */
typedef struct das_limit_t {
	const char* name;  /** The limit's name, see the list above */
	int32_t     value; /** The largest value allowed */
} DasLimit;

/** Look up a library limit by name
 *
 * @param sName The name of a limit, see DasLimit
 * @returns the limit, or NULL if this library has no limit by that name
 *          (or sName is NULL)
 * @memberof DasLimit
 */
DAS_API const DasLimit* DasLimit_lookup(const char* sName);

/** Walk the library's limits
 *
 * @param pPrev NULL to get the first limit, otherwise a limit pointer
 *        returned by an earlier call to this function or DasLimit_lookup()
 * @returns the limit after pPrev, or NULL after the last one.  A pPrev that
 *        did not come from this library is reported with daserrf() and
 *        also returns NULL.
 * @memberof DasLimit
 */
DAS_API const DasLimit* DasLimit_next(const DasLimit* pPrev);


/** A do nothing function on Unix, closes network sockets on windows */
DAS_API void daslib_finish(void);

DAS_API das_errcode daserr_funcf(
	const char* sFile, const char* sFunc, int nLine, das_errcode nCode,
	const char* sFmt, ...
) _das_fmt_check(5, 6);

DAS_API das_errcode daserr_func(
	const char* sFile, const char* sFunc, int nLine, das_errcode nCode,
	const char* sMsg
);

/** Signal an error condition.
 *
 * This routine is called throughout the code when an error condition arrises.
 *
 * The message goes to the log hook, see log.h, carrying nErrCode in its nErr
 * member.  What happens next depends on the error disposition:
 *
 *   - DASERRDISP_EXIT, the default: the message is logged at DASLOG_CRITICAL,
 *     then the program calls exit(nErrCode).  The hook has returned by then,
 *     so atexit() functions run normally and may use das2C.
 *
 *   - DASERRDISP_RET: the message is logged at DASLOG_ERROR and nErrCode is
 *     returned.
 *
 * The library keeps no copy of the message.  A program that wants the text
 * after daserrf() returns saves it in its hook.
 *
 * Each source file in the code has it's own error code.  Though it's probably
 * not that useful to end users, the codes are provided here:
 *
 *  - @b  8 : Not yet implemented - DASERR_NOTIMP
 *  - @b  9 : Assertion failures  - DASERR_ASSERT
 *  - @b 11 : Lib initialization  - DASERR_INIT
 *  - @b 12 : buffer.c      - DASERR_BUF
 *  - @b 13 : util.c        - DASERR_UTIL
 *  - @b 14 : enc.c    - DASERR_ENC
 *  - @b 15 : units.c       - DASERR_UNITS
 *  - @b 16 : desc.c  - DASERR_DESC
 *  - @b 17 : plane.c       - DASERR_PLANE
 *  - @b 18 : packet.c      - DASERR_PKT
 *  - @b 19 : stream.c      - DASERR_STREAM
 *  - @b 20 : oob.c         - DASERR_OOB
 *  - @b 22 : io.c          - DASERR_IO
 *  - @b 23 : dsdf.c        - DASERR_DSDF
 *  - @b 24 : dft.c         - DASERR_DFT
 *  - @b 25 : log.c         - DASERR_LOG
 *  - @b 26 : array.c       - DASERR_ARRAY
 *  - @b 27 : var.c    - DASERR_VAR
 *  - @b 28 : dim.c   - DASERR_DIM
 *  - @b 29 : datset.c     - DASERR_DS
 *  - @b 30 : datset_2.c    - DASERR_BLDR (das2 packet to dataset conversion)
 *  - @b 31 : http.c        - DASERR_HTTP
 *  - @b 32 : datum.c       - DASERR_DATUM
 *  - @b 33 : value.c       - DASERR_VALUE
 *  - @b 34 : oper.c    - DASERR_OP
 *  - @b 35 : creds.c - DASERR_CRED
 *  - @b 36 : node.c        - DASERR_NODE
 *  - @b 37 : time.c        - DASERR_TIME
 *  - @b 38 : prop.c    - DASERR_PROP
 *  - @b 39 : Serialization - DASERR_SERIAL
 *  - @b 40 : iter.c    - DASERR_ITER
 *  - @b 41 : spice.h       - DASERR_SPICE
 *  - @b 42 : uri.c         - DASERR_URI
 *  - @b 43 : form*.c       - DASERR_FORM
 *  - @b 44 : gen.c   - DASERR_GEN
 *  - @b 45 : catalog.c     - DASERR_CAT
 *  - @b 46 : settable.c    - DASERR_SRC
 * 
 * Application programs are recommended to use values 64 and above to avoid
 * colliding with future das2 error codes.
 *
 * @param nErrCode The value to return to the shell, should be one of the above.
 * @return With DASERRDISP_EXIT this function does not return, with
 *         DASERRDISP_RET it returns nErrCode.
 */
#define daserrf(nErrCode, ...) \
  daserr_funcf(__FILE__, __func__, __LINE__, nErrCode, __VA_ARGS__ )

/** Signal an error condition with a message that is not a format string.
 *
 * Use this when the message comes from elsewhere (a caller, a parser, a
 * library) and may contain '%' characters.  Nothing is interpreted.
 *
 * @see daserrf */
#define daserr(nErrCode, sMsg) \
  daserr_func(__FILE__, __func__, __LINE__, nErrCode, sMsg)

/** Raise an error and return false, for a function that reports failure as a
 * bool rather than a das_errcode.
 *
 * `return daserrf(...)` is correct in the das_errcode functions that are most
 * of this library, and silently WRONG in a bool one: daserrf() hands back a
 * non-zero code, which as a bool is true, so the failure reports success.  The
 * types are both integers and no compiler diagnostic exists for it.  Write
 * `return daserr_falsef(...)` in a bool function and the question does not
 * come up.
 */
#define daserr_falsef(nErrCode, ...) \
  (daserr_funcf(__FILE__, __func__, __LINE__, nErrCode, __VA_ARGS__), false)

/** Raise an error and return NULL, for a function that answers with a pointer.
 * @see daserr_falsef */
#define daserr_nullf(nErrCode, ...) \
  (daserr_funcf(__FILE__, __func__, __LINE__, nErrCode, __VA_ARGS__), NULL)


/** Get the library's error disposition
 * @returns DASERRDISP_EXIT or DASERRDISP_RET
 */
DAS_API das_errdisp daserr_disp(void);

/** Set the error disposition, the inverse of daserr_disp()
 *
 * The disposition is shared by all threads.  Set it once at startup; a
 * library function that changes it for a moment changes it for every
 * thread.
 *
 * @param nDisp DASERRDISP_EXIT or DASERRDISP_RET.  Any other value is an
 *        error and leaves the disposition unchanged.
 */
DAS_API void daserr_setDisp(das_errdisp nDisp);


/** The limit on xml packet length, in bytes.  (ascii encoding.) */
#define DASXML_BUF_LEN 1000000

/** The limit of xml element name length, in bytes. */
#define DASXML_NAME_LEN 256


/** The parts of a URL, see DasUrlParts_fromStr() */
typedef enum das_urlpart_e {
	DASURLPART_SCHEME,    /**< "http", "https", "file" ... */
	DASURLPART_USERINFO,  /**< "user:password" before an "@", rare and best avoided */
	DASURLPART_HOST,      /**< Host name or address, brackets kept on IPv6 literals */
	DASURLPART_PORT,      /**< Port number, the scheme's default if none was given */
	DASURLPART_PATH,      /**< Path on the host, starting with "/" */
	DASURLPART_QUERY,     /**< Everything between "?" and "#", still percent encoded */
	DASURLPART_FRAGMENT,  /**< Everything after "#" */
	DASURLPART_DATASET    /**< das2.2 "dataset=" value from the query, still encoded */
} das_urlpart;

/** Where the parts of a URL are, as found by DasUrlParts_fromStr()
 *
 * A plain value that holds no text, only where each part lies in the string
 * it was parsed from.  It is small enough for the stack and needs no
 * cleanup.  The members are private, read the parts with DasUrlParts_get().
 */
typedef struct das_urlparts_t {
	uint16_t aOff[12];   /* indexed by das_urlpart, room for more parts */
	uint16_t aLen[12];
} DasUrlParts;

/** Split a URL into its parts without copying it
 *
 * Finds the scheme, authority, path, query and fragment of an absolute URL
 * in the manner of RFC 3986.  Nothing is decoded and nothing is copied: the
 * result records where each part is in sUrl, so sUrl must stay unchanged
 * while the parts are in use.
 *
 * The das2.2 interface names a data source with a "dataset=" value in the
 * query, and das2 servers may want a separate login for each one (das2.2
 * ICD, https://zenodo.org/records/3588535).  That value is found here too,
 * as DASURLPART_DATASET, so every caller splits it out the same way.
 *
 * @code
 *   DasUrlParts parts;
 *   if(DasUrlParts_fromStr(&parts, sUrl)){
 *       size_t uLen;
 *       const char* pHost = DasUrlParts_get(&parts, sUrl, DASURLPART_HOST, &uLen);
 *       printf("host: %.*s\n", (int)uLen, pHost);
 *   }
 * @endcode
 *
 * @param pParts Receives the part locations
 * @param sUrl The URL, at most 65534 bytes
 * @returns true if sUrl has a scheme and the rest is well formed, false
 *          otherwise.  Nothing is logged; a caller with a user to tell
 *          decides what to say.
 */
DAS_API bool DasUrlParts_fromStr(DasUrlParts* pParts, const char* sUrl);

/** Get one part of a URL split by DasUrlParts_fromStr()
 *
 * The part is returned in place, so it is not NUL terminated.  Print it
 * with "%.*s", or copy it out with the length when a C string is needed.
 *
 * For a URL that gives no port, DASURLPART_PORT points to a static string with
 * the scheme's default, "80" for http and "443" for https; for other schemes
 * without one it is absent.
 *
 * @param pParts The result of DasUrlParts_fromStr()
 * @param sUrl The same string given to DasUrlParts_fromStr()
 * @param ePart Which part
 * @param pLen Receives the length of the part, 0 when it is absent
 * @returns A pointer to the start of the part within sUrl (or to a static
 *          default port), or NULL if the URL has no such part.  An empty part,
 *          such as the query of "http://h/p?", is non-NULL with length 0.
 */
DAS_API const char* DasUrlParts_get(
	const DasUrlParts* pParts, const char* sUrl, das_urlpart ePart, size_t* pLen
);

/** Is this string a full URI rather than a relative catalog path?
 *
 * True when it starts with a URI scheme, a letter followed by letters,
 * digits, '+', '-' or '.' and then a colon, as in "tag:", "doi:" and the
 * shorthands "site:" and "test:".  A single letter before the colon is a
 * Windows drive, not a scheme.  Note that URLs are URIs too; test with
 * DasCat_isNodeUrl() first when the difference matters.
 *
 */
DAS_API bool dasurl_hasScheme(const char* sPath);


/** Is the path a directory.
 * @param path The directory in question, passed to stat(2)
 * @return true if @b path can be determined to be a directory, false otherwise
 */
DAS_API bool dasfs_isDir(const char* path);

/** Insure directories to a specific location exist 
 * 
 * @param path a full path to a file.  If this string contains no 
 *        path separators, the function does nothing.
 * 
 * @return true if directories up to the final location either exist, or 
 *        could be generated, false otherwise.
 */
DAS_API das_errcode dasfs_mkdirsTo(const char* path);


/** Get the home directory for the current account 
 * 
 * @return A pointer to a global string that is the current user's
 *         home directory, or other likely writable location if
 *         the home directory could not be determined.
 */
DAS_API const char* dasfs_userHome(void);

/** Get the directory for everything das keeps on behalf of the current user
 *
 * This is ".das" under the home directory on every platform, the way ssh
 * and git keep their files, or the value of the DAS_USERDIR environment
 * variable if it was set when daslib_init() ran, or whatever the program gave
 * das_setUserDir().  The catalog node cache and saved credentials live
 * under it.  Only the path is returned, nothing is created; whatever
 * writes under it makes the directories it needs.
 *
 * @return A pointer to a global string, never NULL or empty.
 */
DAS_API const char* dasfs_userDir(void);

/** Choose the directory dasfs_userDir() reports
 *
 * For programs that embed the library and want to set the location
 * themselves instead of relying on the environment.  Call it right after
 * daslib_init() and before any DasCat or other object that reads the
 * directory is made: those take their copy when constructed and are not
 * moved by a later call.
 *
 * @param sDir The directory to use, copied.  NULL or an empty string goes
 *        back to the default, the DAS_USERDIR environment variable if set,
 *        else ".das" under the home directory.
 */
DAS_API void dasfs_setUserDir(const char* sDir);

/** Copy a file to a destination creating directories as needed. 
 *
 * If the files exists at the destination it in overwritten.  Directories are
 * created as needed.  Directory permissions are are the same as the file
 * with the addition that for each READ permission in the mode, directory 
 * EXEC permission is added.
 *
 * @param src - name of file to copy
 * @param dest - name of destination
 * @param mode - the permission mode of the destination file, 0664 is 
 *               recommended if you can decide on the output permissions mode.
 *               (mode argument not present in WIN32 version)
 *
 * @returns - true if the copy was successful, false otherwise.
 * 
 */
#ifdef _WIN32
DAS_API bool dasfs_copyFile(const char* src, const char* dest);
#else
DAS_API bool dasfs_copyFile(const char* src, const char* dest, mode_t mode);
#endif


/** Is the path a file.
 * @param path The file in question, passed to stat(2)
 * @return true if @b path can be determined to be a file, false otherwise
 */
DAS_API bool dasfs_isFile(const char* path);

/** Rename a file, replacing the destination if it exists.
 *
 * POSIX rename(2) already does this, atomically.  The Windows C runtime's
 * rename() fails when the destination exists, so there this is a wrapper
 * around MoveFileEx() with MOVEFILE_REPLACE_EXISTING, which is the atomic
 * replace within one volume.  Use it for the write-to-side-file-then-rename
 * pattern so that the pattern behaves the same everywhere.
 *
 * @param sFrom The existing file
 * @param sTo The new name, any file already there is replaced
 * @return 0 on success, -1 on failure with errno set as rename(2) would
 */
DAS_API int dasfs_rename(const char* sFrom, const char* sTo);

/** Put a stdio stream in binary mode
 *
 * Das streams count bytes, but on Windows stdin and stdout start in text mode,
 * which turns LF into CRLF on output and stops input at the first 0x1A byte.
 * Call this on the stream before reading or writing das data through it.
 * new_DasXport_cfile() already does.  Does nothing on other platforms.
 *
 * @param pFile The stream to change, NULL is ignored
 */
DAS_API void dasfs_setStdioBinary(FILE* pFile);

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
DAS_API int dasfs_listDir(
	const char* sPath, char ppDirList[][256], size_t uMaxDirs, char cType
);

/** See a given string matches the short or long form of an option
 *
 * This is commonly used in loops over each command line argument. It
 * works for arguments in the form:
 * 
 *   -a value
 *   --my-arg=value
 * 
 * The form that has spaces between the long option name and the value
 * is not supported
 * 
 * @param sArg - A string to test
 * @param sShort - The short form of an option
 * @param sLong  - The long form of an option
 * @param pLong  - If not NULL, the associated bool value is set to true if
 *         the long form of a argument was found.
 * 
 * @returns true if the string matches the given option, false otherwise
 */
DAS_API bool dasarg_isOpt(const char* sArg, const char* sShort, const char* sLong, bool* pLong);


/** Get the value of an argument if the current entry in argv matches
 * either the short or long form of an option.
 * 
 * This is typically used to test command line arguments in a loop.  It
 * works for arguments in the form:
 * 
 *   -a value
 *   --my-arg=value
 * 
 * The form that has spaces between the long option name and the value
 * is not supported
 *
 * @param argv -  The list of command line parameters
 * @param argc -  The number of command line parameters
 * @param sShort - The short form of the option
 * @param sLong - The long form of the option
 * @param pArgIdx - A pointer to the current argument index.  It is 
 *        incremented if the option is found.
 * @param sDest - The location to receive the option value
 * @param uDest - The size of the location to receive the option value
 *                Note the macro DAS_FIELD_SZ is useful for getting 
 *                the size of a field in a structure.
 * 
 * @returns false if the argument was not found or the option value
 *   missing often the argument, true otherwise and the index at 
 *   pArgInd is incremented.
 * 
 * @see also DAS_FIELD_SZ
 */
DAS_API bool dasarg_getOptVal(
  char** argv, int argc, const char* sShort, const char* sLong, int* pArgIdx, 
  char* sDest, size_t uDest
);

/** @} */


#ifdef __cplusplus
}
#endif

#endif /* _das_util_h_ */
