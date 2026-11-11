/* The latest version of this library is available on GitHub;
    https://github.com/sheredom/json.h

   This is free and unencumbered software released into the public domain.
  
   Anyone is free to copy, modify, publish, use, compile, sell, or
   distribute this software, either in source code form or as a compiled
   binary, for any purpose, commercial or non-commercial, and by any
   means.
  
   In jurisdictions that recognize copyright laws, the author or authors
   of this software dedicate any and all copyright interest in the
   software to the public domain. We make this dedication for the benefit
   of the public at large and to the detriment of our heirs and
   successors. We intend this dedication to be an overt act of
   relinquishment in perpetuity of all present and future rights to this
   software under copyright law.
  
   THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
   EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
   MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.
   IN NO EVENT SHALL THE AUTHORS BE LIABLE FOR ANY CLAIM, DAMAGES OR
   OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE,
   ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR
   OTHER DEALINGS IN THE SOFTWARE.
  
   For more information, please refer to <http://unlicense.org/>
*/

/** @file json.h Sheredom's json.h parser with global symbol name changes. 
 * The upstream version of this file can be found on GitHub at 
 * https://github.com/sheredom/json.h
 */

/* das2 developer modification note:  
 * 
 * Neil Hennings's JSON library is quite good and I can imagine many others 
 * would pick it up (I know I adopted it instead of json-c), so I've changed
 * the names of all the visible objects.  Structure names have been changed to
 * blend in with the rest of the das2C and functions have had their prefixes
 * changed from "json_" to "das_j".   If I could be sure that das2C users
 * would never need to access raw json objects, this change would not be
 * necessary.  However preventing access to the raw objects limits das2C's
 * usefulness, hence this trivial token transformation.  Original "json.h"
 * logic has not been altered in any way, though some new functions have been
 * added at the end of the C file.
 * 
 * Thanks to Neil, for helping out space physics research.
 *   -cwp 2018-10-14
 */

#ifndef _das_json_h_
#define _das_json_h_

#if defined(_MSC_VER)
#pragma warning(push)

/* disable 'bytes padding added after construct' warning */
#pragma warning(disable : 4820)
#endif

#include <das3/defs.h>

#ifdef __cplusplus
extern "C" {
#endif
	
	
/** @defgroup json JSON
 * A parser for JSON text and accessors for the resulting document tree.
 *
 * Catalog nodes happen to be JSON documents, so DasNode_jdo() returns
 * these objects, but nothing here is specific to catalogs.
 */

/** @addtogroup json
 * @{
 */


/** The various types JSON values can be. Used to identify what a value is */
typedef enum das_jsontype_e {
  DASJSONTYPE_STR,
  DASJSONTYPE_NUM,
  DASJSONTYPE_DICT,
  DASJSONTYPE_ARY,
  DASJSONTYPE_TRUE,
  DASJSONTYPE_FALSE,
  DASJSONTYPE_NULL
} das_jsontype;

/** A JSON string value */
typedef struct das_jsonstr_t {
  /** utf-8 string */
  const char *string;
  /** the size (in bytes) of the string */
  size_t stringSize;
} DasJsonStr;

/** A JSON string value (extended) */
typedef struct das_jsonstrex_t {
  /** the JSON string this extends. */
  struct das_jsonstr_t string;

  /** the character offset for the value in the JSON input */
  size_t offset;

  /** the line number for the value in the JSON input */
  size_t line;

  /** the row number for the value in the JSON input, in bytes */
  size_t row;
} DasJsonStrEx;

/** A JSON number value */
typedef struct das_jsonnum_t {
  /** ASCII string containing representation of the number */
  const char *number;
  /** the size (in bytes) of the number */
  size_t numberSize;
} DasJsonNum;

/** An element of a JSON dictionary */
typedef struct das_jsondictel_t {
  /** the name of this element */
  struct das_jsonstr_t *name;
  /** the value of this element */
  struct das_jdo_t *value;
  /** the next object element (can be NULL if the last element in the object) */
  struct das_jsondictel_t *next;
} DasJsonDictEl;

/** a JSON dictionary payload */
typedef struct das_jsondict_t {
  /** a linked list of the elements in the object */
  struct das_jsondictel_t *start;
  /** the number of elements in the object */
  size_t length;
} DasJsonDict;

/** an element of a JSON array */
typedef struct das_jsonaryel_t {
  /** the value of this element */
  struct das_jdo_t *value;
  /** the next array element (can be NULL if the last element in the array) */
  struct das_jsonaryel_t *next;
} DasJsonAryEl;

/** a JSON array value */
typedef struct das_jsonary_t {
  /** a linked list of the elements in the array */
  struct das_jsonaryel_t *start;
  /** the number of elements in the array */
  size_t length;
} DasJsonAry;


	
/** JSON Dom Element */
typedef struct das_jdo_t {
  /** a pointer to either a DasJsonStr, DasJsonNum, DasJsonDict, or
   * DasJsonAry. Should be cast to the appropriate struct type based on what
   * the type parameter
	*/
  void* value;
  
  /** must be one of das_jsontype. If type is dasj_objtype_true, 
	* dasj_objtype_false, or dasj_objtype_null, payload will be NULL */
  size_t type;
  
} DasJdo;

/** a JSON value (extended) 
 * @extends das_json_val_s
 */
typedef struct das_jsonvalex_t {
  /** the JSON value this extends. */
  DasJdo value;

  /** the character offset for the value in the JSON input */
  size_t offset;

  /** the line number for the value in the JSON input */
  size_t line;

  /** the row number for the value in the JSON input, in bytes */
  size_t row;
} DasJsonValEx;

	

/** Flag used by dasj_parse() and dasj_parse_ex() to alter parsing behavior */
typedef enum das_jparse_e {
  DASJPARSE_DEFAULT = 0,

  /** allow trailing commas in objects and arrays. For example, both [true,] and
   * {"a" : null,} would be allowed with this option on.
	*/
  DASJPARSE_ALLOW_TRAILING_COMMA = 0x1,

  /** allow unquoted keys for objects. For example, {a : null} would be allowed
   * with this option on.
	*/
  DASJPARSE_ALLOW_UNQUOTED_KEYS = 0x2,

  /** allow a global unbracketed object. For example, a : null, b : true, c : {}
   * would be allowed with this option on.
	*/
  DASJPARSE_ALLOW_GLOBAL_OBJECT = 0x4,

  /** allow objects to use '=' instead of ':' between key/value pairs. For
   * example, a = null, b : true would be allowed with this option on.
	*/
  DASJPARSE_ALLOW_EQUALS_IN_OBJECT = 0x8,

  /** allow that objects don't have to have comma separators between key/value
   * pairs.
	*/
  DASJPARSE_ALLOW_NO_COMMAS = 0x10,

  /** allow c-style comments (// or /\* *\/) to be ignored in the input JSON
   * file.
   */
  DASJPARSE_ALLOW_C_STYLE_COMMENTS = 0x20,

  // deprecated flag, unused
  DASJPARSE_DEPRECATED = 0x40,

  /** record location information for each value. */
  DASJPARSE_ALLOW_LOCATION_INFORMATION = 0x80,

  /** allow strings to be 'single quoted' */
  DASJPARSE_ALLOW_SINGLE_QUOTED_STRINGS = 0x100,

  /** allow numbers to be hexadecimal */
  DASJPARSE_ALLOW_HEXADECIMAL_NUMBERS = 0x200,

  /** allow numbers like +123 to be parsed */
  DASJPARSE_ALLOW_LEADING_PLUS_SIGN = 0x400,

  /** allow numbers like .0123 or 123. to be parsed */
  DASJPARSE_ALLOW_LEADING_OR_TRAILING_DECIMAL_POINT = 0x800,

  /** allow Infinity, -Infinity, NaN, -NaN */
  DASJPARSE_ALLOW_INF_AND_NAN = 0x1000,

  /** allow multi line string values */
  DASJPARSE_ALLOW_MULTI_LINE_STRINGS = 0x2000,

  /** allow simplified JSON to be parsed. Simplified JSON is an enabling of a set
   * of other parsing options.
	*/
  DASJPARSE_ALLOW_SIMPLIFIED_JSON =
      (DASJPARSE_ALLOW_TRAILING_COMMA |
       DASJPARSE_ALLOW_UNQUOTED_KEYS |
       DASJPARSE_ALLOW_GLOBAL_OBJECT |
       DASJPARSE_ALLOW_EQUALS_IN_OBJECT |
       DASJPARSE_ALLOW_NO_COMMAS),

  /** allow JSON5 to be parsed. JSON5 is an enabling of a set of other parsing
   * options.
	*/
  DASJPARSE_ALLOW_JSON5 =
      (DASJPARSE_ALLOW_TRAILING_COMMA |
       DASJPARSE_ALLOW_UNQUOTED_KEYS |
       DASJPARSE_ALLOW_C_STYLE_COMMENTS |
       DASJPARSE_ALLOW_SINGLE_QUOTED_STRINGS |
       DASJPARSE_ALLOW_HEXADECIMAL_NUMBERS |
       DASJPARSE_ALLOW_LEADING_PLUS_SIGN |
       DASJPARSE_ALLOW_LEADING_OR_TRAILING_DECIMAL_POINT |
       DASJPARSE_ALLOW_INF_AND_NAN |
       DASJPARSE_ALLOW_MULTI_LINE_STRINGS)
} das_jparse;

/** JSON parsing error codes */
typedef enum das_jparseerr_e {
  /** no error occurred (huzzah!) */
  DASJPARSEERR_NONE = 0,

  /** expected either a comma or a closing '}' or ']' to close an object or
   * array!
	*/
  DASJPARSEERR_EXPECTED_COMMA_OR_CLOSING_BRACKET,

  /** colon separating name/value pair was missing! */
  DASJPARSEERR_EXPECTED_COLON,

  /** expected string to begin with '"'! */
  DASJPARSEERR_EXPECTED_OPENING_QUOTE,

  /** invalid escaped sequence in string! */
  DASJPARSEERR_INVALID_STRING_ESCAPE_SEQUENCE,

  /** invalid number format! */
  DASJPARSEERR_INVALID_NUMBER_FORMAT,

  /** invalid value! */
  DASJPARSEERR_INVALID_VALUE,

  /** reached end of buffer before object/array was complete! */
  DASJPARSEERR_PREMATURE_END_OF_BUFFER,

  /** string was malformed! */
  DASJPARSEERR_INVALID_STRING,

  /** a call to malloc, or a user provider allocator, failed */
  DASJPARSEERR_ALLOCATOR_FAILED,

  /** the JSON input had unexpected trailing characters that weren't part of the
   * JSON value
	*/
  DASJPARSEERR_UNEXPECTED_TRAILING_CHARACTERS,

  /** catch-all error for everything else that exploded (real bad chi!) */
  DASJPARSEERR_UNKNOWN
} das_jparseerr;

/** error report from json_parse_ex() */
typedef struct das_jparseresult_t {
  /** the error code (one of json_parse_error_e), use dasj_parse_error_info() 
	* To convert the value to an error string.
	*/
  size_t error;

  /** the character offset for the error in the JSON input */
  size_t errOffset;

  /** the line number for the error in the JSON input */
  size_t errLine;

  /** the row number for the error, in bytes */
  size_t errRow;
} DasJparseResult;

/** Provide error string describing a parsing error result */
DAS_API const char* DasJparseResult_info(
	const DasJparseResult* pRes, char* sTmp, size_t uLen
);


/** Parse a JSON text file, returning a pointer to the root of the JSON
 * structure. 
 * 
 * @param src a pointer to a utf-8 encoded JSON document, type is void* to
 *        emphasize that the data may contain byte values > 127.
 * 
 * @param src_size The size in bytes of the document string to parse
 
 * @param flags_bitset values from das_jparse OR'ed together
 *           to configure parsing preferences.
 * 
 * @param alloc_func_ptr May be use to allocate memory via some method other
 *           than malloc.
 * 
 * @param user_data A user data pointer pto be passed as the first argument
 *           to alloc_func_ptr, if alloc_func_ptr is not NULL.
 * 
 * @param result (if not NULL) will explain the type of error, and the
 *           location in the input it occurred.
 
 * @return  A pointer to the entire malloc'ed memory.  Use del_DasJdo() on
 *          the return value when it's no longer needed, unless
 *          alloc_func_ptr supplied the memory, in which case release it the
 *          way that allocator expects.  Returns NULL if an error occurred
 *          (malformed JSON input, or malloc failed)
 */
DAS_API DasJdo* new_DasJdo_parseEx(
	const void *src, size_t src_size, size_t flags_bitset,
	void *(*alloc_func_ptr)(void *, size_t), void *user_data,
	DasJparseResult *result
);


/** Parse a JSON text file with default options and without detailed error
 * reporting. 
 * 
 * This is just shorthand for new_DasJdo_parseEx(src, size, 0, NULL, NULL, NULL);
 */
DAS_API DasJdo* new_DasJdo_parse(const void *src, size_t src_size);

/** Free a parsed JSON document
 *
 * The whole tree is one allocation, so this frees every node at once.  Call
 * it only on the root that new_DasJdo_parse() or new_DasJdo_parseEx() returned
 * with the default allocator, never on a node found inside it.
 *
 * @param pThis the root, NULL is ignored
 * @memberof DasJdo
 */
DAS_API void del_DasJdo(DasJdo* pThis);


/** Given a DOM path retrieve a JSON element
 * 
 * @param pThis
 * 
 * @param sRelPath Pointer to a utf-8 string providing a relative path
 *              to the sub object.  This has the form:
 *              [STRING | INTEGER] [ / [STRING | INTEGER] [ / [STRING | INTEGER]]]
 *              For example: "cassini/ephemeris/CONTACTS/0/EMAIL".  For Arrays
 *              sub items are denoted by ascii integers 0 - size, for 
 *              dictionaries sub items can be denoted by either keys strings
 *              or by ascii integers.  Objects of type string, number, true,
 *              false and null have no sub items
 *
 * @return NULL if no object exists at the specified relative path or if the 
 *              object is atomic and has no sub items.
 * 
 * @memberof DasJdo
 */
DAS_API const DasJdo* DasJdo_subtree(const DasJdo* pThis, const char* sRelPath);

/** Get the first dictionary element from a JSON dictionary */
DAS_API const DasJsonDictEl* DasJdo_dictFirst(const DasJdo* pThis);

/** Get the first array element from a JSON array */
DAS_API const DasJsonAryEl* DasJdo_aryFirst(const DasJdo* pThis);



/** Get a string value from a JSON DOM element
 * 
 * @param pThis The element in question
 * @return NULL if the object type is not a string or if pThis is NULL, a null
 *         terminated utf-8 string otherwise
 */
DAS_API const char* DasJdo_str(const DasJdo* pThis);


DAS_API const DasJsonDict* DasJdo_dict(const DasJdo* pThis);

/** Get an array value from a JSON DOM element
 *
 * @param pThis The element in question
 * @return NULL if the object type is not an array or if pThis is NULL
 */
DAS_API const DasJsonAry* DasJdo_ary(const DasJdo* pThis);



/** Write out a minified JSON utf-8 string. 
 * 
 * This string is an encoding of the minimal string characters required to still
 * encode the same data.  DasJdo_newMiniStr performs 1 call to malloc for the
 * entire encoding.
 * 
 * @param pThis The top level document object to write, all sub-objects will 
 *       be written as well.
 * 
 * @param out_size If not NULL, receives the length of the string in bytes,
 *         not counting the null terminator, as strlen() would report.
 *
 * @return A new buffer containing utf-8 string data, or NULL if an error
 *         occurred (malformed JSON input, or malloc failed),
 *         
 * @memberof DasJdo
 */
DAS_API void* DasJdo_newMiniStr(const DasJdo* pThis, size_t *out_size);


/** Write out a pretty JSON utf-8 string. 
 * 
 * This string is encoded such that the resultant JSON is pretty in that it is 
 * easily human readable. The indent and newline parameters allow a user to
 * specify what kind of indentation and newline they want (two spaces / three 
 * spaces / tabs? \\r, \\n, \\r\\n ?). Both indent and newline can be NULL, indent
 * defaults to two spaces ("  "), and newline defaults to linux newlines ('\\n'
 * as the newline character).
 * 
 * DasJdo_newPrettyStr performs 1 call to malloc for the entire encoding.
 * 
 * @param pThis The top level document object to write, all sub-objects
 *        will be written as well.
 * @param indent The indentation string, NULL for two spaces
 * @param newline The line ending, NULL for "\n"
 * @param out_size If not NULL, receives the length of the string in bytes,
 *         not counting the null terminator, as strlen() would report.
 *
 * @return NULL if an error occurred (malformed JSON input, or malloc failed).
 * @memberof DasJdo
 */
DAS_API void* DasJdo_newPrettyStr(const DasJdo* pThis, const char *indent,
                        const char *newline, size_t *out_size);


/** @} */

#ifdef __cplusplus
} // extern "C"
#endif

#if defined(_MSC_VER)
#pragma warning(pop)
#endif

#endif // _das_json_h_
