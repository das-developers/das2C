/* Copyright (C) 2020 Chris Piker <chris-piker@uiowa.edu>
 *
 * This file is part of das2C, the Core Das2 C Library.
 *
 * das2C is free software; you can redistribute it and/or modify it under
 * the terms of the GNU Lesser General Public License version 2.1 as published
 * by the Free Software Foundation.
 *
 * das2C is distributed in the hope that it will be useful, but WITHOUT ANY
 * WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS
 * FOR A PARTICULAR PURPOSE.  See the GNU Lesser General Public License for
 * more details.
 *
 * You should have received a copy of the GNU Lesser General Public License
 * version 2.1 along with das2C; if not, see <http://www.gnu.org/licenses/>.
 */


/** @file defs.h Minimal definitions for das2 utilities that can safely be
 * run without calling das_init().  
 *
 * This is mostly useful for old das1 programs.
 */

#include <limits.h>
#include <stdbool.h>
#include <stdint.h>


#ifndef _das_defs_h_
#define _das_defs_h_

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

/* Make it obvious when we are just moving data as opposed to characters */
typedef uint8_t byte;

/** return code type
 * 0 indicates success, negative integer indicates failure
 */
typedef int DasErrCode;

/** success return code */
#define DAS_OKAY 0

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
#define DASERR_TIME   37
#define DASERR_MAX    37


#endif /* _das_defs_h_ */


