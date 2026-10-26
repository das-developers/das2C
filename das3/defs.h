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
#include <stddef.h>


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

#if defined __LP64__
#define HOST_IS_64_BIT
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

#ifdef _WIN64
#define HOST_IS_64_BIT
#endif

#else


#ifdef __EMSCRIPTEN__
/* Web assembly environment is defined as little endian */
#define HOST_IS_LSB_FIRST
#include <emscripten.h>
#else

#error "unknown byte order!"

#endif  /* webassembly */
#endif  /* _WIN32 */
#endif  /* __sun */
#endif  /* __linux */

/* Setup the DLL macros for windows.  GCC and clang builds that compile with
   -fvisibility=hidden export only DAS_API names from a shared library, the
   same set a Windows DLL exports.  Emscripten is left alone, its exports come
   from the link line. */
#if defined(_WIN32) && defined(DAS_USE_DLL)
#  ifdef BUILDING_DLL
#    define DAS_API __declspec(dllexport)
#  else
#    define DAS_API __declspec(dllimport)
#  endif
#elif defined(__GNUC__) && !defined(__EMSCRIPTEN__) && !defined(_WIN32)
#  define DAS_API __attribute__((visibility("default")))
#else
#define DAS_API
#endif

#if defined(_WIN32)
#define DAS_DEPRECATED __declspec(deprecated)
#else
#define DAS_DEPRECATED __attribute__((__deprecated__))
#endif

/* Ask the compiler to check printf-style calls the way it checks printf
   itself.  FMT_IDX and ARG_IDX are 1-based parameter positions of the format
   string and the first variadic argument.  Not part of any C standard;
   expands to nothing where the compiler lacks it. */
#if defined(__GNUC__) || defined(__clang__)
#define _das_fmt_check(FMT_IDX, ARG_IDX) \
	__attribute__((__format__(__printf__, FMT_IDX, ARG_IDX)))
#else
#define _das_fmt_check(FMT_IDX, ARG_IDX)
#endif

#ifdef _WIN32
/** The directory separator for this OS as a character */
#define DAS_DSEPC  '\\'

/** The directory separator for this OS as a string */
#define DAS_DSEPS "\\"

#else

/** The directory separator for this OS as a character */
#define DAS_DSEPC  '/'

/** The directory separator for this OS as a string */ 
#define DAS_DSEPS "/"
#endif

/** Returns the size of a structure field at compile time */
#define DAS_FIELD_SZ(type, field)  (sizeof(((type*)NULL)->field))

#ifdef __cplusplus
extern "C" {
#endif

/** @defgroup utilities Utilities
 * Library initialization, error handling, logging and a few minor libc extensions
 */

	
/** @addtogroup utilities
 * @{
 */

/* Stream identification.  These live here rather than in stream.h so that a
   framing-only tool (one that just needs to know how the bytes are chunked,
   say a WebSocket reader) can know them without pulling in the header
   parser.  The STREAM_ prefix predates the move and stays for compatibility. */

/** In-memory object model a reader builds, or a writer emits.
 *
 * The model is separate from the wire version.  A das 2.2 stream can be read
 * into das3 datasets (V3) for tools that only speak the newer model.
 */
typedef enum das_stream_model {
	STREAM_MODEL_MIXED = -1, /* Readers only: build whatever each header says */
	STREAM_MODEL_V2    =  2, /* PktDesc / PlaneDesc, fail on das3 headers */
	STREAM_MODEL_V3    =  3  /* DasDs / DasDim / DasVar, up-convert das2 headers */
} das_stream_model;

/** The version="" attribute of a stream header: schema generation.
 *
 * For packetized output this also fixes the packet tag style.  Values are
 * major*10 + minor so that ordering comparisons work.  The 1.x members name
 * das1 formats that have no in-band header; a DasStream never reports them,
 * they exist so the one list of stream versions lives in one place.
 */
typedef enum das_stream_ver {
	STREAM_VER_UNK = 0,  /* No header seen yet, or a version this library doesn't know */
	STREAM_VER_1_0     = 10, /* das1, fixed records, no header in band */
	STREAM_VER_1_1     = 11, /* tagged das1 */
	STREAM_VER_2_2     = 22,
	STREAM_VER_3_0     = 30
} das_stream_ver;

/** The type="" attribute of a stream header: container and serialization.
 *
 * Independent of the version.  das 2.2 headers omit the attribute and are
 * always basic streams.
 */
typedef enum das_stream_type {
	STREAM_TYPE_UNK = 0,
	STREAM_TYPE_BASIC,       /* "das-basic-stream": header then tagged packets */
	STREAM_TYPE_DOC          /* "das-basic-doc": one self-contained XML document */
} das_stream_type;

/** The wire text for a stream version, "2.2", "3.0" etc. */
DAS_API const char* das_stream_ver_toStr(das_stream_ver ver);

/** The wire text for a stream container type, "das-basic-stream" etc. */
DAS_API const char* das_stream_type_toStr(das_stream_type type);

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
typedef uint8_t ubyte;

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
#define DASERR_PROP   38
#define DASERR_SERIAL 39
#define DASERR_ITER   40
#define DASERR_SPICE  41
#define DASERR_URI    42
#define DASERR_FORM   43
#define DASERR_GEN    44
#define DASERR_CAT    45
#define DASERR_SRC    46
#define DASERR_MAX    46

#ifdef __cplusplus
 } 
#endif

#endif /* _das_defs_h_ */


