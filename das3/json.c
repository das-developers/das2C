// The latest version of this library is available on GitHub;
//   https://github.com/sheredom/json.h

// This is free and unencumbered software released into the public domain.
//
// Anyone is free to copy, modify, publish, use, compile, sell, or
// distribute this software, either in source code form or as a compiled
// binary, for any purpose, commercial or non-commercial, and by any
// means.
//
// In jurisdictions that recognize copyright laws, the author or authors
// of this software dedicate any and all copyright interest in the
// software to the public domain. We make this dedication for the benefit
// of the public at large and to the detriment of our heirs and
// successors. We intend this dedication to be an overt act of
// relinquishment in perpetuity of all present and future rights to this
// software under copyright law.
//
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
// EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
// MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.
// IN NO EVENT SHALL THE AUTHORS BE LIABLE FOR ANY CLAIM, DAMAGES OR
// OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE,
// ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR
// OTHER DEALINGS IN THE SOFTWARE.
//
// For more information, please refer to <http://unlicense.org/>

/* Downstream developer modification note:
 *    Symbols visible outside this module have been changed to avoid name 
 *    name collisions in other projects.  See the modification note in json.h 
 */

#include <stdlib.h>
#include <stdio.h>
#include <stdbool.h>
#include <string.h>  /* Only needed for DasJdo_subtree, could do manually */
#include <ctype.h>

#include "util.h"
#include "json.h"
#include "time.h"
#include "value.h"

// work around MSVC 2013 and lower not having strtoull
#if defined(_MSC_VER) && (_MSC_VER <= 1800)
#define json_strtoull _strtoui64
#else
#define json_strtoull strtoull
#endif

#if defined(__clang__)
#pragma clang diagnostic push

// we do one big allocation via malloc, then cast aligned slices of this for
// our structures - we don't have a way to tell the compiler we know what we
// are doing, so disable the warning instead!
#pragma clang diagnostic ignored "-Wcast-align"
#elif defined(_MSC_VER)
#pragma warning(push)

// disable 'function selected for inline expansion' warning
#pragma warning(disable : 4711)

// disable '#pragma warning: there is no warning number' warning
#pragma warning(disable : 4619)

// disable 'warning number not a valid compiler warning' warning
#pragma warning(disable : 4616)

// disable 'Compiler will insert Spectre mitigation for memory load if /Qspectre
// switch specified' warning
#pragma warning(disable : 5045)
#endif

struct json_parse_state_s {
  const char *src;
  size_t size;
  size_t offset;
  size_t flags_bitset;
  char *data;
  char *dom;
  size_t dom_size;
  size_t data_size;
  size_t line_no;     // line counter for error reporting
  size_t line_offset; // (offset-line_offset) is the character number (in bytes)
  size_t error;
};

static int json_hexadecimal_digit(const char c) {
  if ('0' <= c && c <= '9') {
    return c - '0';
  }
  if ('a' <= c && c <= 'f') {
    return c - 'a' + 10;
  }
  if ('A' <= c && c <= 'F') {
    return c - 'A' + 10;
  }
  return -1;
}

static int json_hexadecimal_value(const char * c, const unsigned long size, unsigned long * result) {
  const char * p;
  int digit;

  if (size > sizeof(unsigned long) * 2) {
    return 0;
  }

  *result = 0;
  for (p = c; (unsigned long)(p - c) < size; ++p) {
    *result <<= 4;
    digit = json_hexadecimal_digit(*p);
    if (digit < 0 || digit > 15) {
      return 0;
    }
    *result |= (unsigned char)digit;
  }
  return 1;
}

static int json_skip_whitespace(struct json_parse_state_s *state) {
  size_t offset = state->offset;
  const size_t size = state->size;
  const char *const src = state->src;

  // the only valid whitespace according to ECMA-404 is ' ', '\n', '\r' and '\t'
  switch (src[offset]) {
  default:
    return 0;
  case ' ':
  case '\r':
  case '\t':
  case '\n':
    break;
  }

  do {
    switch (src[offset]) {
    default:
      // Update offset
      state->offset = offset;
      return 1;
    case ' ':
    case '\r':
    case '\t':
      break;
    case '\n':
      state->line_no++;
      state->line_offset = offset;
      break;
    }

    offset++;
  } while (offset < size);

  // Update offset
  state->offset = offset;
  return 1;
}

static int json_skip_c_style_comments(struct json_parse_state_s *state) {
  // do we have a comment?
  if ('/' == state->src[state->offset]) {
    // skip '/'
    state->offset++;

    if ('/' == state->src[state->offset]) {
      // we had a comment of the form //

      // skip second '/'
      state->offset++;

      while (state->offset < state->size) {
        switch (state->src[state->offset]) {
        default:
          // skip the character in the comment
          state->offset++;
          break;
        case '\n':
          // if we have a newline, our comment has ended! Skip the newline
          state->offset++;

          // we entered a newline, so move our line info forward
          state->line_no++;
          state->line_offset = state->offset;
          return 1;
        }
      }

      // we reached the end of the JSON file!
      return 1;
    } else if ('*' == state->src[state->offset]) {
      // we had a comment in the form /* */

      // skip '*'
      state->offset++;

      while (state->offset + 1 < state->size) {
        if (('*' == state->src[state->offset]) &&
            ('/' == state->src[state->offset + 1])) {
          // we reached the end of our comment!
          state->offset += 2;
          return 1;
        } else if ('\n' == state->src[state->offset]) {
          // we entered a newline, so move our line info forward
          state->line_no++;
          state->line_offset = state->offset;
        }

        // skip character within comment
        state->offset++;
      }

      // a comment like /* has to end with a */. We reached the end of the file
      // which is a failure!
      return 1;
    }
  }

  // we didn't have any comment, which is ok too!
  return 0;
}

static int json_skip_all_skippables(struct json_parse_state_s *state) {
  // skip all whitespace and other skippables until there are none left
  // note that the previous version suffered from read past errors should
  // the stream end on json_skip_c_style_comments eg. '{"a" ' with comments flag

  int did_consume = 0;
  const size_t size = state->size;

  if (DASJPARSE_ALLOW_C_STYLE_COMMENTS & state->flags_bitset) {
    do {
      if (state->offset == size) {
        state->error = DASJPARSEERR_PREMATURE_END_OF_BUFFER;
        return 1;
      }

      did_consume = json_skip_whitespace(state);

      // This should really be checked on access, not in front of every call
      if (state->offset == size) {
        state->error = DASJPARSEERR_PREMATURE_END_OF_BUFFER;
        return 1;
      }

      did_consume |= json_skip_c_style_comments(state);
    } while (0 != did_consume);
  } else {
    do {
      if (state->offset == size) {
        state->error = DASJPARSEERR_PREMATURE_END_OF_BUFFER;
        return 1;
      }

      did_consume = json_skip_whitespace(state);
    } while (0 != did_consume);
  }

  if (state->offset == size) {
    state->error = DASJPARSEERR_PREMATURE_END_OF_BUFFER;
    return 1;
  }

  return 0;
}

static int json_get_value_size(struct json_parse_state_s *state,
                               int is_global_object);

static int json_get_string_size(struct json_parse_state_s *state,
                                size_t is_key) {
  size_t offset = state->offset;
  const size_t size = state->size;
  size_t data_size = 0;
  const char *const src = state->src;
  const int is_single_quote = '\'' == src[offset];
  const char quote_to_use = is_single_quote ? '\'' : '"';
  const size_t flags_bitset = state->flags_bitset;
  unsigned long codepoint;

  if ((DASJPARSE_ALLOW_LOCATION_INFORMATION & flags_bitset) != 0 &&
      is_key != 0) {
    state->dom_size += sizeof(struct das_jsonstrex_t);
  } else {
    state->dom_size += sizeof(struct das_jsonstr_t);
  }

  if ('"' != src[offset]) {
    // if we are allowed single quoted strings check for that too
    if (!((DASJPARSE_ALLOW_SINGLE_QUOTED_STRINGS & flags_bitset) &&
          is_single_quote)) {
      state->error = DASJPARSEERR_EXPECTED_OPENING_QUOTE;
      state->offset = offset;
      return 1;
    }
  }

  // skip leading '"' or '\''
  offset++;

  while ((offset < size) && (quote_to_use != src[offset])) {
    // add space for the character
    data_size++;

    if ('\\' == src[offset]) {
      // skip reverse solidus character
      offset++;

      if (offset == size) {
        state->error = DASJPARSEERR_PREMATURE_END_OF_BUFFER;
        state->offset = offset;
        return 1;
      }

      switch (src[offset]) {
      default:
        state->error = DASJPARSEERR_INVALID_STRING_ESCAPE_SEQUENCE;
        state->offset = offset;
        return 1;
      case '"':
      case '\\':
      case '/':
      case 'b':
      case 'f':
      case 'n':
      case 'r':
      case 't':
        // all valid characters!
        offset++;
        break;
      case 'u':
        if (!(offset + 5 < size)) {
          // invalid escaped unicode sequence!
          state->error = DASJPARSEERR_INVALID_STRING_ESCAPE_SEQUENCE;
          state->offset = offset;
          return 1;
        }
     
        codepoint = 0;
        if (!json_hexadecimal_value(&src[offset + 1], 4, &codepoint)) {
          // escaped unicode sequences must contain 4 hexadecimal digits!
          state->error = DASJPARSEERR_INVALID_STRING_ESCAPE_SEQUENCE;
          state->offset = offset;
          return 1;
        }

        // valid sequence!
        // see: https://en.wikipedia.org/wiki/UTF-8#Invalid_code_points
        //      1       7       U + 0000        U + 007F        0xxxxxxx
        //      2       11      U + 0080        U + 07FF        110xxxxx        10xxxxxx
        //      3       16      U + 0800        U + FFFF        1110xxxx        10xxxxxx        10xxxxxx
        //      4       21      U + 10000       U + 10FFFF      11110xxx        10xxxxxx        10xxxxxx        10xxxxxx
        // Note: the high and low surrogate halves used by UTF-16 (U+D800 through U+DFFF) and code points not encodable by UTF-16 (those after U+10FFFF) are not legal Unicode values, and their UTF-8 encoding must be treated as an invalid byte sequence.
    
        if (codepoint <= 0x7f) {
          offset += 1;
          data_size += 1;
        }
        else if (codepoint <= 0x7ff) {
          offset += 2;
          data_size += 2;
        }
        else if (codepoint >= 0xd800 && codepoint <= 0xdfff) {
          state->error = DASJPARSEERR_INVALID_STRING_ESCAPE_SEQUENCE;
          state->offset = offset;
          return 1;
        }
        else {
          offset += 3;
          data_size += 3;
        }
        // codepoints after 0xffff are not supported in json

        break;
      }
    } else if (('\r' == src[offset]) || ('\n' == src[offset])) {
      if (!(DASJPARSE_ALLOW_MULTI_LINE_STRINGS & flags_bitset)) {
        // invalid escaped unicode sequence!
        state->error = DASJPARSEERR_INVALID_STRING_ESCAPE_SEQUENCE;
        state->offset = offset;
        return 1;
      }

      offset++;
    } else {
      // skip character (valid part of sequence)
      offset++;
    }
  }

  // skip trailing '"' or '\''
  offset++;

  // add enough space to store the string
  state->data_size += data_size;

  // one more byte for null terminator ending the string!
  state->data_size++;

  // update offset
  state->offset = offset;

  return 0;
}

static int is_valid_unquoted_key_char(const char c) {
  return (('0' <= c && c <= '9') || ('a' <= c && c <= 'z') ||
          ('A' <= c && c <= 'Z') || ('_' == c));
}

static int json_get_key_size(struct json_parse_state_s *state) {
  const size_t flags_bitset = state->flags_bitset;

  if (DASJPARSE_ALLOW_UNQUOTED_KEYS & flags_bitset) {
    size_t offset = state->offset;
    const size_t size = state->size;
    const char *const src = state->src;
    size_t data_size = state->data_size;

    // if we are allowing unquoted keys, first grok for a quote...
    if ('"' == src[offset]) {
      // ... if we got a comma, just parse the key as a string as normal
      return json_get_string_size(state, 1);
    } else if ((DASJPARSE_ALLOW_SINGLE_QUOTED_STRINGS & flags_bitset) &&
               ('\'' == src[offset])) {
      // ... if we got a comma, just parse the key as a string as normal
      return json_get_string_size(state, 1);
    } else {
      while ((offset < size) && is_valid_unquoted_key_char(src[offset])) {
        offset++;
        data_size++;
      }

      // one more byte for null terminator ending the string!
      data_size++;

      if (DASJPARSE_ALLOW_LOCATION_INFORMATION & flags_bitset) {
        state->dom_size += sizeof(struct das_jsonstrex_t);
      } else {
        state->dom_size += sizeof(struct das_jsonstr_t);
      }

      // update offset
      state->offset = offset;

      // update data_size
      state->data_size = data_size;

      return 0;
    }
  } else {
    // we are only allowed to have quoted keys, so just parse a string!
    return json_get_string_size(state, 1);
  }
}

static int json_get_object_size(struct json_parse_state_s *state,
                                int is_global_object) {
  const size_t flags_bitset = state->flags_bitset;
  const char *const src = state->src;
  const size_t size = state->size;
  size_t elements = 0;
  int allow_comma = 0;

  if (is_global_object) {
    // if we found an opening '{' of an object, we actually have a normal JSON
    // object at the root of the DOM...
    if (!json_skip_all_skippables(state) && '{' == state->src[state->offset]) {
      // .. and we don't actually have a global object after all!
      is_global_object = 0;
    }
  }

  if (!is_global_object) {
    if ('{' != src[state->offset]) {
      state->error = DASJPARSEERR_UNKNOWN;
      return 1;
    }

    // skip leading '{'
    state->offset++;
  }

  state->dom_size += sizeof(struct das_jsondict_t);

  while (state->offset < size) {
    if (!is_global_object) {
      if (json_skip_all_skippables(state)) {
        state->error = DASJPARSEERR_PREMATURE_END_OF_BUFFER;
        return 1;
      }
      if ('}' == src[state->offset]) {
        // skip trailing '}'
        state->offset++;

        // finished the object!
        break;
      }
    } else {
      // we don't require brackets, so that means the object ends when the input
      // stream ends!
      if (json_skip_all_skippables(state)) {
        break;
      }
    }

    // if we parsed at least once element previously, grok for a comma
    if (allow_comma) {
      if (',' == src[state->offset]) {
        // skip comma
        state->offset++;
        allow_comma = 0;
      } else if (DASJPARSE_ALLOW_NO_COMMAS & flags_bitset) {
        // we don't require a comma, and we didn't find one, which is ok!
        allow_comma = 0;
      } else {
        // otherwise we are required to have a comma, and we found none
        state->error = DASJPARSEERR_EXPECTED_COMMA_OR_CLOSING_BRACKET;
        return 1;
      }

      if (DASJPARSE_ALLOW_TRAILING_COMMA & flags_bitset) {
        continue;
      } else {
        if (json_skip_all_skippables(state)) {
          state->error = DASJPARSEERR_PREMATURE_END_OF_BUFFER;
          return 1;
        }
      }
    }

    if (json_get_key_size(state)) {
      // key parsing failed!
      state->error = DASJPARSEERR_INVALID_STRING;
      return 1;
    }

    if (json_skip_all_skippables(state)) {
      state->error = DASJPARSEERR_PREMATURE_END_OF_BUFFER;
      return 1;
    }

    if (DASJPARSE_ALLOW_EQUALS_IN_OBJECT & flags_bitset) {
      const char current = src[state->offset];
      if ((':' != current) && ('=' != current)) {
        state->error = DASJPARSEERR_EXPECTED_COLON;
        return 1;
      }
    } else {
      if (':' != src[state->offset]) {
        state->error = DASJPARSEERR_EXPECTED_COLON;
        return 1;
      }
    }

    // skip colon
    state->offset++;

    if (json_skip_all_skippables(state)) {
      state->error = DASJPARSEERR_PREMATURE_END_OF_BUFFER;
      return 1;
    }

    if (json_get_value_size(state, /* is_global_object = */ 0)) {
      // value parsing failed!
      return 1;
    }

    // successfully parsed a name/value pair!
    elements++;
    allow_comma = 1;
  }

  state->dom_size += sizeof(struct das_jsondictel_t) * elements;

  return 0;
}

static int json_get_array_size(struct json_parse_state_s *state) {
  const size_t flags_bitset = state->flags_bitset;
  size_t elements = 0;
  int allow_comma = 0;
  const char *const src = state->src;
  const size_t size = state->size;

  if ('[' != src[state->offset]) {
    // expected array to begin with leading '['
    state->error = DASJPARSEERR_UNKNOWN;
    return 1;
  }

  // skip leading '['
  state->offset++;

  state->dom_size += sizeof(struct das_jsonary_t);

  while (state->offset < size) {
    if (json_skip_all_skippables(state)) {
      state->error = DASJPARSEERR_PREMATURE_END_OF_BUFFER;
      return 1;
    }

    if (']' == src[state->offset]) {
      // skip trailing ']'
      state->offset++;

      state->dom_size += sizeof(struct das_jsonaryel_t) * elements;

      // finished the object!
      return 0;
    }

    // if we parsed at least once element previously, grok for a comma
    if (allow_comma) {
      if (',' == src[state->offset]) {
        // skip comma
        state->offset++;
        allow_comma = 0;
      } else if (!(DASJPARSE_ALLOW_NO_COMMAS & flags_bitset)) {
        state->error = DASJPARSEERR_EXPECTED_COMMA_OR_CLOSING_BRACKET;
        return 1;
      }

      if (DASJPARSE_ALLOW_TRAILING_COMMA & flags_bitset) {
        allow_comma = 0;
        continue;
      } else {
        if (json_skip_all_skippables(state)) {
          state->error = DASJPARSEERR_PREMATURE_END_OF_BUFFER;
          return 1;
        }
      }
    }

    if (json_get_value_size(state, /* is_global_object = */ 0)) {
      // value parsing failed!
      return 1;
    }

    // successfully parsed an array element!
    elements++;
    allow_comma = 1;
  }

  // we consumed the entire input before finding the closing ']' of the array!
  state->error = DASJPARSEERR_PREMATURE_END_OF_BUFFER;
  return 1;
}


#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wimplicit-fallthrough"

static int json_get_number_size(struct json_parse_state_s *state) {
  const size_t flags_bitset = state->flags_bitset;
  size_t offset = state->offset;
  const size_t size = state->size;
  int had_leading_digits = 0;
  const char *const src = state->src;

  state->dom_size += sizeof(struct das_jsonnum_t);

  if ((DASJPARSE_ALLOW_HEXADECIMAL_NUMBERS & flags_bitset) &&
      (offset + 1 < size) && ('0' == src[offset]) &&
      (('x' == src[offset + 1]) || ('X' == src[offset + 1]))) {
    // skip the leading 0x that identifies a hexadecimal number
    offset += 2;

    // consume hexadecimal digits
    while ((offset < size) && (('0' <= src[offset] && src[offset] <= '9') ||
                               ('a' <= src[offset] && src[offset] <= 'f') ||
                               ('A' <= src[offset] && src[offset] <= 'F'))) {
      offset++;
    }
  } else {
    int found_sign = 0;
    int inf_or_nan = 0;

    if ((offset < size) &&
        (('-' == src[offset]) ||
         ((DASJPARSE_ALLOW_LEADING_PLUS_SIGN & flags_bitset) &&
          ('+' == src[offset])))) {
      // skip valid leading '-' or '+'
      offset++;

      found_sign = 1;
    }

    if (DASJPARSE_ALLOW_INF_AND_NAN & flags_bitset) {
      const char inf[9] = "Infinity";
      const size_t inf_strlen = sizeof(inf) - 1;
      const char nan[4] = "NaN";
      const size_t nan_strlen = sizeof(nan) - 1;

      if (offset + inf_strlen < size) {
        int found = 1;
        size_t i;
        for (i = 0; i < inf_strlen; i++) {
          if (inf[i] != src[offset + i]) {
            found = 0;
            break;
          }
        }

        if (found) {
          // We found our special 'Infinity' keyword!
          offset += inf_strlen;

          inf_or_nan = 1;
        }
      }

      if (offset + nan_strlen < size) {
        int found = 1;
        size_t i;
        for (i = 0; i < nan_strlen; i++) {
          if (nan[i] != src[offset + i]) {
            found = 0;
            break;
          }
        }

        if (found) {
          // We found our special 'NaN' keyword!
          offset += nan_strlen;

          inf_or_nan = 1;
        }
      }
    }

    if (found_sign && !inf_or_nan && (offset < size) &&
        !('0' <= src[offset] && src[offset] <= '9')) {
      // check if we are allowing leading '.'
      if (!(DASJPARSE_ALLOW_LEADING_OR_TRAILING_DECIMAL_POINT &
            flags_bitset) ||
          ('.' != src[offset])) {
        // a leading '-' must be immediately followed by any digit!
        state->error = DASJPARSEERR_INVALID_NUMBER_FORMAT;
        state->offset = offset;
        return 1;
      }
    }

    if ((offset < size) && ('0' == src[offset])) {
      // skip valid '0'
      offset++;

      // we need to record whether we had any leading digits for checks later
      had_leading_digits = 1;

      if ((offset < size) && ('0' <= src[offset] && src[offset] <= '9')) {
        // a leading '0' must not be immediately followed by any digit!
        state->error = DASJPARSEERR_INVALID_NUMBER_FORMAT;
        state->offset = offset;
        return 1;
      }
    }

    // the main digits of our number next
    while ((offset < size) && ('0' <= src[offset] && src[offset] <= '9')) {
      offset++;

      // we need to record whether we had any leading digits for checks later
      had_leading_digits = 1;
    }

    if ((offset < size) && ('.' == src[offset])) {
      offset++;

      if (!('0' <= src[offset] && src[offset] <= '9')) {
        if (!(DASJPARSE_ALLOW_LEADING_OR_TRAILING_DECIMAL_POINT &
              flags_bitset) ||
            !had_leading_digits) {
          // a decimal point must be followed by at least one digit
          state->error = DASJPARSEERR_INVALID_NUMBER_FORMAT;
          state->offset = offset;
          return 1;
        }
      }

      // a decimal point can be followed by more digits of course!
      while ((offset < size) && ('0' <= src[offset] && src[offset] <= '9')) {
        offset++;
      }
    }

    if ((offset < size) && ('e' == src[offset] || 'E' == src[offset])) {
      // our number has an exponent!
      // skip 'e' or 'E'
      offset++;

      if ((offset < size) && ('-' == src[offset] || '+' == src[offset])) {
        // skip optional '-' or '+'
        offset++;
      }

      // consume exponent digits
      while ((offset < size) && ('0' <= src[offset] && src[offset] <= '9')) {
        offset++;
      }
    }
  }

  if (offset < size) {
    switch (src[offset]) {
    case ' ':
    case '\t':
    case '\r':
    case '\n':
    case '}':
    case ',':
    case ']':
      // all of the above are ok
      break;
    case '=':
      if (DASJPARSE_ALLOW_EQUALS_IN_OBJECT & flags_bitset) {
        break;
      }
    default:
      state->error = DASJPARSEERR_INVALID_NUMBER_FORMAT;
      state->offset = offset;
      return 1;
    }
  }

  state->data_size += offset - state->offset;

  // one more byte for null terminator ending the number string!
  state->data_size++;

  // update offset
  state->offset = offset;

  return 0;
}

#pragma GCC diagnostic pop

static int json_get_value_size(struct json_parse_state_s *state,
                               int is_global_object) {
  const size_t flags_bitset = state->flags_bitset;
  const char *const src = state->src;
  size_t offset;
  const size_t size = state->size;

  if (DASJPARSE_ALLOW_LOCATION_INFORMATION & flags_bitset) {
    state->dom_size += sizeof(struct das_jsonvalex_t);
  } else {
    state->dom_size += sizeof(struct das_jdo_t);
  }

  if (is_global_object) {
    return json_get_object_size(state, /* is_global_object = */ 1);
  } else {
    if (json_skip_all_skippables(state)) {
      state->error = DASJPARSEERR_PREMATURE_END_OF_BUFFER;
      return 1;
    }

    // can cache offset now
    offset = state->offset;

    switch (src[offset]) {
    case '"':
      return json_get_string_size(state, 0);
    case '\'':
      if (DASJPARSE_ALLOW_SINGLE_QUOTED_STRINGS & flags_bitset) {
        return json_get_string_size(state, 0);
      } else {
        // invalid value!
        state->error = DASJPARSEERR_INVALID_VALUE;
        return 1;
      }
    case '{':
      return json_get_object_size(state, /* is_global_object = */ 0);
    case '[':
      return json_get_array_size(state);
    case '-':
    case '0':
    case '1':
    case '2':
    case '3':
    case '4':
    case '5':
    case '6':
    case '7':
    case '8':
    case '9':
      return json_get_number_size(state);
    case '+':
      if (DASJPARSE_ALLOW_LEADING_PLUS_SIGN & flags_bitset) {
        return json_get_number_size(state);
      } else {
        // invalid value!
        state->error = DASJPARSEERR_INVALID_NUMBER_FORMAT;
        return 1;
      }
    case '.':
      if (DASJPARSE_ALLOW_LEADING_OR_TRAILING_DECIMAL_POINT &
          flags_bitset) {
        return json_get_number_size(state);
      } else {
        // invalid value!
        state->error = DASJPARSEERR_INVALID_NUMBER_FORMAT;
        return 1;
      }
    default:
      if ((offset + 4) <= size && 't' == src[offset + 0] &&
          'r' == src[offset + 1] && 'u' == src[offset + 2] &&
          'e' == src[offset + 3]) {
        state->offset += 4;
        return 0;
      } else if ((offset + 5) <= size && 'f' == src[offset + 0] &&
                 'a' == src[offset + 1] && 'l' == src[offset + 2] &&
                 's' == src[offset + 3] && 'e' == src[offset + 4]) {
        state->offset += 5;
        return 0;
      } else if ((offset + 4) <= size && 'n' == state->src[offset + 0] &&
                 'u' == state->src[offset + 1] &&
                 'l' == state->src[offset + 2] &&
                 'l' == state->src[offset + 3]) {
        state->offset += 4;
        return 0;
      } else if ((DASJPARSE_ALLOW_INF_AND_NAN & flags_bitset) &&
                 (offset + 3) <= size && 'N' == src[offset + 0] &&
                 'a' == src[offset + 1] && 'N' == src[offset + 2]) {
        return json_get_number_size(state);
      } else if ((DASJPARSE_ALLOW_INF_AND_NAN & flags_bitset) &&
                 (offset + 8) <= size && 'I' == src[offset + 0] &&
                 'n' == src[offset + 1] && 'f' == src[offset + 2] &&
                 'i' == src[offset + 3] && 'n' == src[offset + 4] &&
                 'i' == src[offset + 5] && 't' == src[offset + 6] &&
                 'y' == src[offset + 7]) {
        return json_get_number_size(state);
      }

      // invalid value!
      state->error = DASJPARSEERR_INVALID_VALUE;
      return 1;
    }
  }
}

static void json_parse_value(struct json_parse_state_s *state,
                             int is_global_object, struct das_jdo_t *value);

static void json_parse_string(struct json_parse_state_s *state,
                              struct das_jsonstr_t *string) {
  size_t offset = state->offset;
  size_t bytes_written = 0;
  const char *const src = state->src;
  const char quote_to_use = '\'' == src[offset] ? '\'' : '"';
  char *data = state->data;
  unsigned long codepoint;

  string->string = data;

  // skip leading '"' or '\''
  offset++;

  while (quote_to_use != src[offset]) {
    if ('\\' == src[offset]) {
      // skip the reverse solidus
      offset++;

      switch (src[offset++]) {
      default:
        return; // we cannot ever reach here
      case 'u':
        {
          codepoint = 0;
          if (!json_hexadecimal_value(&src[offset], 4, &codepoint)) {
            return; // this shouldn't happen as the value was already validated
          }
  
          offset += 4;
  
          if (codepoint <= 0x7fu) {
            data[bytes_written++] = (char)codepoint; // 0xxxxxxx
          }
          else if (codepoint <= 0x7ffu) {
            data[bytes_written++] = (char)(0xc0u | (codepoint >> 6)); // 110xxxxx
            data[bytes_written++] = (char)(0x80u | (codepoint & 0x3fu)); // 10xxxxxx
          }
          else {
            // we assume the value was validated and thus is within the valid range
            data[bytes_written++] = (char)(0xe0u | (codepoint >> 12)); // 1110xxxx
            data[bytes_written++] = (char)(0x80u | ((codepoint >> 6) & 0x3fu)); // 10xxxxxx
            data[bytes_written++] = (char)(0x80u | (codepoint & 0x3fu)); // 10xxxxxx
          }
        }
        break;
      case '"':
        data[bytes_written++] = '"';
        break;
      case '\\':
        data[bytes_written++] = '\\';
        break;
      case '/':
        data[bytes_written++] = '/';
        break;
      case 'b':
        data[bytes_written++] = '\b';
        break;
      case 'f':
        data[bytes_written++] = '\f';
        break;
      case 'n':
        data[bytes_written++] = '\n';
        break;
      case 'r':
        data[bytes_written++] = '\r';
        break;
      case 't':
        data[bytes_written++] = '\t';
        break;
      case '\r':
        data[bytes_written++] = '\r';

        // check if we have a "\r\n" sequence
        if ('\n' == src[offset]) {
          data[bytes_written++] = '\n';
          offset++;
        }

        break;
      case '\n':
        data[bytes_written++] = '\n';
        break;
      }
    } else {
      // copy the character
      data[bytes_written++] = src[offset++];
    }
  }

  // skip trailing '"' or '\''
  offset++;

  // record the size of the string
  string->stringSize = bytes_written;

  // add null terminator to string
  data[bytes_written++] = '\0';

  // move data along
  state->data += bytes_written;

  // update offset
  state->offset = offset;
}

static void json_parse_key(struct json_parse_state_s *state,
                           struct das_jsonstr_t *string) {
  if (DASJPARSE_ALLOW_UNQUOTED_KEYS & state->flags_bitset) {
    const char *const src = state->src;
    char *const data = state->data;
    size_t offset = state->offset;

    // if we are allowing unquoted keys, check for quoted anyway...
    if (('"' == src[offset]) || ('\'' == src[offset])) {
      // ... if we got a quote, just parse the key as a string as normal
      json_parse_string(state, string);
    } else {
      size_t size = 0;

      string->string = state->data;

      while (is_valid_unquoted_key_char(src[offset])) {
        data[size++] = src[offset++];
      }

      // add null terminator to string
      data[size] = '\0';

      // record the size of the string
      string->stringSize = size++;

      // move data along
      state->data += size;

      // update offset
      state->offset = offset;
    }
  } else {
    // we are only allowed to have quoted keys, so just parse a string!
    json_parse_string(state, string);
  }
}

static void json_parse_object(struct json_parse_state_s *state,
                              int is_global_object,
                              struct das_jsondict_t *object) {
  const size_t flags_bitset = state->flags_bitset;
  const size_t size = state->size;
  const char *const src = state->src;
  size_t elements = 0;
  int allow_comma = 0;
  struct das_jsondictel_t *previous = 0;

  if (is_global_object) {
    // if we skipped some whitespace, and then found an opening '{' of an
    // object, we actually have a normal JSON object at the root of the DOM...
    if ('{' == src[state->offset]) {
      // .. and we don't actually have a global object after all!
      is_global_object = 0;
    }
  }

  if (!is_global_object) {
    // skip leading '{'
    state->offset++;
  }

  (void)json_skip_all_skippables(state);

  // reset elements
  elements = 0;

  while (state->offset < size) {
    struct das_jsondictel_t *element = 0;
    struct das_jsonstr_t *string = 0;
    struct das_jdo_t *value = 0;

    if (!is_global_object) {
      (void)json_skip_all_skippables(state);

      if ('}' == src[state->offset]) {
        // skip trailing '}'
        state->offset++;

        // finished the object!
        break;
      }
    } else {
      if (json_skip_all_skippables(state)) {
        // global object ends when the file ends!
        break;
      }
    }

    // if we parsed at least one element previously, grok for a comma
    if (allow_comma) {
      if (',' == src[state->offset]) {
        // skip comma
        state->offset++;
        allow_comma = 0;
        continue;
      }
    }

    element = (struct das_jsondictel_t *)state->dom;

    state->dom += sizeof(struct das_jsondictel_t);

    if (0 == previous) {
      // this is our first element, so record it in our object
      object->start = element;
    } else {
      previous->next = element;
    }

    previous = element;

    if (DASJPARSE_ALLOW_LOCATION_INFORMATION & flags_bitset) {
      struct das_jsonstrex_t *string_ex =
          (struct das_jsonstrex_t *)state->dom;
      state->dom += sizeof(struct das_jsonstrex_t);

      string_ex->offset = state->offset;
      string_ex->line = state->line_no;
      string_ex->row = state->offset - state->line_offset;

      string = &(string_ex->string);
    } else {
      string = (struct das_jsonstr_t *)state->dom;
      state->dom += sizeof(struct das_jsonstr_t);
    }

    element->name = string;

    (void)json_parse_key(state, string);

    (void)json_skip_all_skippables(state);

    // skip colon or equals
    state->offset++;

    (void)json_skip_all_skippables(state);

    if (DASJPARSE_ALLOW_LOCATION_INFORMATION & flags_bitset) {
      struct das_jsonvalex_t *value_ex = (struct das_jsonvalex_t *)state->dom;
      state->dom += sizeof(struct das_jsonvalex_t);

      value_ex->offset = state->offset;
      value_ex->line = state->line_no;
      value_ex->row = state->offset - state->line_offset;

      value = &(value_ex->value);
    } else {
      value = (struct das_jdo_t *)state->dom;
      state->dom += sizeof(struct das_jdo_t);
    }

    element->value = value;

    json_parse_value(state, /* is_global_object = */ 0, value);

    // successfully parsed a name/value pair!
    elements++;
    allow_comma = 1;
  }

  // if we had at least one element, end the linked list
  if (previous) {
    previous->next = 0;
  }

  if (0 == elements) {
    object->start = 0;
  }

  object->length = elements;
}

static void json_parse_array(struct json_parse_state_s *state,
                             struct das_jsonary_t *array) {
  const char *const src = state->src;
  const size_t size = state->size;
  size_t elements = 0;
  int allow_comma = 0;
  struct das_jsonaryel_t *previous = 0;

  // skip leading '['
  state->offset++;

  (void)json_skip_all_skippables(state);

  // reset elements
  elements = 0;

  do {
    struct das_jsonaryel_t *element = 0;
    struct das_jdo_t *value = 0;

    (void)json_skip_all_skippables(state);

    if (']' == src[state->offset]) {
      // skip trailing ']'
      state->offset++;

      // finished the array!
      break;
    }

    // if we parsed at least one element previously, grok for a comma
    if (allow_comma) {
      if (',' == src[state->offset]) {
        // skip comma
        state->offset++;
        allow_comma = 0;
        continue;
      }
    }

    element = (struct das_jsonaryel_t *)state->dom;

    state->dom += sizeof(struct das_jsonaryel_t);

    if (0 == previous) {
      // this is our first element, so record it in our array
      array->start = element;
    } else {
      previous->next = element;
    }

    previous = element;

    if (DASJPARSE_ALLOW_LOCATION_INFORMATION & state->flags_bitset) {
      struct das_jsonvalex_t *value_ex = (struct das_jsonvalex_t *)state->dom;
      state->dom += sizeof(struct das_jsonvalex_t);

      value_ex->offset = state->offset;
      value_ex->line = state->line_no;
      value_ex->row = state->offset - state->line_offset;

      value = &(value_ex->value);
    } else {
      value = (struct das_jdo_t *)state->dom;
      state->dom += sizeof(struct das_jdo_t);
    }

    element->value = value;

    json_parse_value(state, /* is_global_object = */ 0, value);

    // successfully parsed an array element!
    elements++;
    allow_comma = 1;
  } while (state->offset < size);

  // end the linked list
  if (previous) {
    previous->next = 0;
  }

  if (0 == elements) {
    array->start = 0;
  }

  array->length = elements;
}

static void json_parse_number(struct json_parse_state_s *state,
                              struct das_jsonnum_t *number) {
  const size_t flags_bitset = state->flags_bitset;
  size_t offset = state->offset;
  const size_t size = state->size;
  size_t bytes_written = 0;
  const char *const src = state->src;
  char *data = state->data;

  number->number = data;

  if (DASJPARSE_ALLOW_HEXADECIMAL_NUMBERS & flags_bitset) {
    if (('0' == src[offset]) &&
        (('x' == src[offset + 1]) || ('X' == src[offset + 1]))) {
      // consume hexadecimal digits
      while ((offset < size) &&
             (('0' <= src[offset] && src[offset] <= '9') ||
              ('a' <= src[offset] && src[offset] <= 'f') ||
              ('A' <= src[offset] && src[offset] <= 'F') ||
              ('x' == src[offset]) || ('X' == src[offset]))) {
        data[bytes_written++] = src[offset++];
      }
    }
  }

  while (offset < size) {
    int end = 0;

    switch (src[offset]) {
    case '0':
    case '1':
    case '2':
    case '3':
    case '4':
    case '5':
    case '6':
    case '7':
    case '8':
    case '9':
    case '.':
    case 'e':
    case 'E':
    case '+':
    case '-':
      data[bytes_written++] = src[offset++];
      break;
    default:
      end = 1;
      break;
    }

    if (0 != end) {
      break;
    }
  }

  if (DASJPARSE_ALLOW_INF_AND_NAN & flags_bitset) {
    const char inf[9] = "Infinity";
    const size_t inf_strlen = sizeof(inf) - 1;
    const char nan[4] = "NaN";
    const size_t nan_strlen = sizeof(nan) - 1;

    if (offset + inf_strlen < size) {
      if ('I' == src[offset]) {
        size_t i;
        // We found our special 'Infinity' keyword!
        for (i = 0; i < inf_strlen; i++) {
          data[bytes_written++] = src[offset++];
        }
      }
    }

    if (offset + nan_strlen < size) {
      if ('N' == src[offset]) {
        size_t i;
        // We found our special 'NaN' keyword!
        for (i = 0; i < nan_strlen; i++) {
          data[bytes_written++] = src[offset++];
        }
      }
    }
  }

  // record the size of the number
  number->numberSize = bytes_written;
  // add null terminator to number string
  data[bytes_written++] = '\0';
  // move data along
  state->data += bytes_written;
  // update offset
  state->offset = offset;
}

static void json_parse_value(struct json_parse_state_s *state,
                             int is_global_object, struct das_jdo_t *value) {
  const size_t flags_bitset = state->flags_bitset;
  const char *const src = state->src;
  const size_t size = state->size;
  size_t offset;

  (void)json_skip_all_skippables(state);

  // cache offset now
  offset = state->offset;

  if (is_global_object) {
    value->type = DASJSONTYPE_DICT;
    value->value = state->dom;
    state->dom += sizeof(struct das_jsondict_t);
    json_parse_object(state, /* is_global_object = */ 1,
                      (struct das_jsondict_t *)value->value);
  } else {
    switch (src[offset]) {
    case '"':
    case '\'':
      value->type = DASJSONTYPE_STR;
      value->value = state->dom;
      state->dom += sizeof(struct das_jsonstr_t);
      json_parse_string(state, (struct das_jsonstr_t *)value->value);
      break;
    case '{':
      value->type = DASJSONTYPE_DICT;
      value->value = state->dom;
      state->dom += sizeof(struct das_jsondict_t);
      json_parse_object(state, /* is_global_object = */ 0,
                        (struct das_jsondict_t *)value->value);
      break;
    case '[':
      value->type = DASJSONTYPE_ARY;
      value->value = state->dom;
      state->dom += sizeof(struct das_jsonary_t);
      json_parse_array(state, (struct das_jsonary_t *)value->value);
      break;
    case '-':
    case '+':
    case '0':
    case '1':
    case '2':
    case '3':
    case '4':
    case '5':
    case '6':
    case '7':
    case '8':
    case '9':
    case '.':
      value->type = DASJSONTYPE_NUM;
      value->value = state->dom;
      state->dom += sizeof(struct das_jsonnum_t);
      json_parse_number(state, (struct das_jsonnum_t *)value->value);
      break;
    default:
      if ((offset + 4) <= size && 't' == src[offset + 0] &&
          'r' == src[offset + 1] && 'u' == src[offset + 2] &&
          'e' == src[offset + 3]) {
        value->type = DASJSONTYPE_TRUE;
        value->value = 0;
        state->offset += 4;
      } else if ((offset + 5) <= size && 'f' == src[offset + 0] &&
                 'a' == src[offset + 1] && 'l' == src[offset + 2] &&
                 's' == src[offset + 3] && 'e' == src[offset + 4]) {
        value->type = DASJSONTYPE_FALSE;
        value->value = 0;
        state->offset += 5;
      } else if ((offset + 4) <= size && 'n' == src[offset + 0] &&
                 'u' == src[offset + 1] && 'l' == src[offset + 2] &&
                 'l' == src[offset + 3]) {
        value->type = DASJSONTYPE_NULL;
        value->value = 0;
        state->offset += 4;
      } else if ((DASJPARSE_ALLOW_INF_AND_NAN & flags_bitset) &&
                 (offset + 3) <= size && 'N' == src[offset + 0] &&
                 'a' == src[offset + 1] && 'N' == src[offset + 2]) {
        value->type = DASJSONTYPE_NUM;
        value->value = state->dom;
        state->dom += sizeof(struct das_jsonnum_t);
        json_parse_number(state, (struct das_jsonnum_t *)value->value);
      } else if ((DASJPARSE_ALLOW_INF_AND_NAN & flags_bitset) &&
                 (offset + 8) <= size && 'I' == src[offset + 0] &&
                 'n' == src[offset + 1] && 'f' == src[offset + 2] &&
                 'i' == src[offset + 3] && 'n' == src[offset + 4] &&
                 'i' == src[offset + 5] && 't' == src[offset + 6] &&
                 'y' == src[offset + 7]) {
        value->type = DASJSONTYPE_NUM;
        value->value = state->dom;
        state->dom += sizeof(struct das_jsonnum_t);
        json_parse_number(state, (struct das_jsonnum_t *)value->value);
      }
      break;
    }
  }
}

struct das_jdo_t *
new_DasJdo_parseEx(const void *src, size_t src_size, size_t flags_bitset,
              void *(*alloc_func_ptr)(void *user_data, size_t size),
              void *user_data, DasJparseResult *result) {
  struct json_parse_state_s state;
  void *allocation;
  struct das_jdo_t *value;
  size_t total_size;
  int input_error;

  if (result) {
    result->error = DASJPARSEERR_NONE;
    result->errOffset = 0;
    result->errLine = 0;
    result->errRow = 0;
  }

  if (0 == src) {
    // invalid src pointer was null!
    return 0;
  }

  state.src = (const char *)src;
  state.size = src_size;
  state.offset = 0;
  state.line_no = 1;
  state.line_offset = 0;
  state.error = DASJPARSEERR_NONE;
  state.dom_size = 0;
  state.data_size = 0;
  state.flags_bitset = flags_bitset;

  input_error = json_get_value_size(
      &state, /* is_global_object = */ (DASJPARSE_ALLOW_GLOBAL_OBJECT &
                                        state.flags_bitset));

  if (0 == input_error) {
    json_skip_all_skippables(&state);

    if (state.offset != state.size) {
      /* our parsing didn't have an error, but there are characters remaining in
       * the input that weren't part of the JSON! */

      state.error = DASJPARSEERR_UNEXPECTED_TRAILING_CHARACTERS;
      input_error = 1;
    }
  }

  if (input_error) {
    // parsing value's size failed (most likely an invalid JSON DOM!)
    if (result) {
      result->error = state.error;
      result->errOffset = state.offset;
      result->errLine = state.line_no;
      result->errRow = state.offset - state.line_offset;
    }
    return 0;
  }

  // our total allocation is the combination of the dom and data sizes (we
  // first encode the structure of the JSON, and then the data referenced by
  // the JSON values)
  total_size = state.dom_size + state.data_size;

  if (0 == alloc_func_ptr) {
    allocation = malloc(total_size);
  } else {
    allocation = alloc_func_ptr(user_data, total_size);
  }

  if (0 == allocation) {
    // malloc failed!
    if (result) {
      result->error = DASJPARSEERR_ALLOCATOR_FAILED;
      result->errOffset = 0;
      result->errLine = 0;
      result->errRow = 0;
    }

    return 0;
  }

  // reset offset so we can reuse it
  state.offset = 0;

  // reset the line information so we can reuse it
  state.line_no = 1;
  state.line_offset = 0;

  state.dom = (char *)allocation;
  state.data = state.dom + state.dom_size;

  if (DASJPARSE_ALLOW_LOCATION_INFORMATION & state.flags_bitset) {
    struct das_jsonvalex_t *value_ex = (struct das_jsonvalex_t *)state.dom;
    state.dom += sizeof(struct das_jsonvalex_t);

    value_ex->offset = state.offset;
    value_ex->line = state.line_no;
    value_ex->row = state.offset - state.line_offset;

    value = &(value_ex->value);
  } else {
    value = (struct das_jdo_t *)state.dom;
    state.dom += sizeof(struct das_jdo_t);
  }

  json_parse_value(&state, /* is_global_object = */
                   (DASJPARSE_ALLOW_GLOBAL_OBJECT & state.flags_bitset),
                   value);

  return (struct das_jdo_t *)allocation;
}

struct das_jdo_t *new_DasJdo_parse(const void *src, size_t src_size) {
  return new_DasJdo_parseEx(src, src_size, DASJPARSE_DEFAULT, 0, 0, 0);
}

void del_DasJdo(DasJdo* pThis)
{
	free(pThis);  /* The tree is one allocation, see new_DasJdo_parseEx() */
}

static int json_write_minified_get_value_size(const struct das_jdo_t *value,
                                              size_t *size);

static int json_write_get_number_size(const struct das_jsonnum_t *number,
                                      size_t *size) {
  unsigned long long parsed_number;
  size_t i;

  if (number->numberSize >= 2) {
    switch (number->number[1]) {
    default:
      break;
    case 'x':
    case 'X':
      // the number is a json_parse_flags_allow_hexadecimal_numbers hexadecimal
      // so we have to do extra work to convert it to a non-hexadecimal for JSON
      // output
      parsed_number = json_strtoull(number->number, 0, 0);

      i = 0;

      while (0 != parsed_number) {
        parsed_number /= 10;
        i++;
      }

      *size += i;
      return 0;
    }
  }

  // check to see if the number has leading/trailing decimal point
  i = 0;

  // skip any leading '+' or '-'
  if ((i < number->numberSize) &&
      (('+' == number->number[i]) || ('-' == number->number[i]))) {
    i++;
  }

  // check if we have infinity
  if ((i < number->numberSize) && ('I' == number->number[i])) {
    char *inf = "Infinity";
    size_t k;

    for (k = i; k < number->numberSize; k++) {
      const char c = *inf++;

      // Check if we found the Infinity string!
      if ('\0' == c) {
        break;
      } else if (c != number->number[k]) {
        break;
      }
    }

    if ('\0' == *inf) {
      // Inf becomes 1.7976931348623158e308 because JSON can't support it
      *size += 22;

      // if we had a leading '-' we need to record it in the JSON output
      if ('-' == number->number[0]) {
        *size += 1;
      }
    }

    return 0;
  }

  // check if we have nan
  if ((i < number->numberSize) && ('N' == number->number[i])) {
    char *nan = "NaN";
    size_t k;

    for (k = i; k < number->numberSize; k++) {
      const char c = *nan++;

      // Check if we found the NaN string!
      if ('\0' == c) {
        break;
      } else if (c != number->number[k]) {
        break;
      }
    }

    if ('\0' == *nan) {
      // NaN becomes 1 because JSON can't support it
      *size += 1;

      return 0;
    }
  }

  // if we had a leading decimal point
  if ((i < number->numberSize) && ('.' == number->number[i])) {
    // 1 + because we had a leading decimal point
    *size += 1;
    goto cleanup;
  }

  for (; i < number->numberSize; i++) {
    const char c = number->number[i];
    if (!('0' <= c && c <= '9')) {
      break;
    }
  }

  // if we had a trailing decimal point
  if ((i + 1 == number->numberSize) && ('.' == number->number[i])) {
    // 1 + because we had a trailing decimal point
    *size += 1;
    goto cleanup;
  }

cleanup:
  *size += number->numberSize; // the actual string of the number

  // if we had a leading '+' we don't record it in the JSON output
  if ('+' == number->number[0]) {
    *size -= 1;
  }

  return 0;
}

static int json_write_get_string_size(const struct das_jsonstr_t *string,
                                      size_t *size) {
  size_t i;
  for (i = 0; i < string->stringSize; i++) {
    switch (string->string[i]) {
    case '"':
    case '\\':
    case '\b':
    case '\f':
    case '\n':
    case '\r':
    case '\t':
      *size += 2;
      break;
    default:
      *size += 1;
      break;
    }
  }

  *size += 2; // need to encode the surrounding '"' characters

  return 0;
}

static int json_write_minified_get_array_size(const struct das_jsonary_t *array,
                                              size_t *size) {
  struct das_jsonaryel_t *element;

  *size += 2; // '[' and ']'

  if (1 < array->length) {
    *size += array->length - 1; // ','s separate each element
  }

  for (element = array->start; 0 != element; element = element->next) {
    if (json_write_minified_get_value_size(element->value, size)) {
      // value was malformed!
      return 1;
    }
  }

  return 0;
}

static int
json_write_minified_get_object_size(const struct das_jsondict_t *object,
                                    size_t *size) {
  struct das_jsondictel_t *element;

  *size += 2; // '{' and '}'

  *size += object->length; // ':'s separate each name/value pair

  if (1 < object->length) {
    *size += object->length - 1; // ','s separate each element
  }

  for (element = object->start; 0 != element; element = element->next) {
    if (json_write_get_string_size(element->name, size)) {
      // string was malformed!
      return 1;
    }

    if (json_write_minified_get_value_size(element->value, size)) {
      // value was malformed!
      return 1;
    }
  }

  return 0;
}

static int json_write_minified_get_value_size(const struct das_jdo_t *value,
                                              size_t *size) {
  switch (value->type) {
  default:
    // unknown value type found!
    return 1;
  case DASJSONTYPE_NUM:
    return json_write_get_number_size((struct das_jsonnum_t *)value->value,
                                      size);
  case DASJSONTYPE_STR:
    return json_write_get_string_size((struct das_jsonstr_t *)value->value,
                                      size);
  case DASJSONTYPE_ARY:
    return json_write_minified_get_array_size(
        (struct das_jsonary_t *)value->value, size);
  case DASJSONTYPE_DICT:
    return json_write_minified_get_object_size(
        (struct das_jsondict_t *)value->value, size);
  case DASJSONTYPE_TRUE:
    *size += 4; // the string "true"
    return 0;
  case DASJSONTYPE_FALSE:
    *size += 5; // the string "false"
    return 0;
  case DASJSONTYPE_NULL:
    *size += 4; // the string "null"
    return 0;
  }
}

static char *json_write_minified_value(const struct das_jdo_t *value,
                                       char *data);

static char *json_write_number(const struct das_jsonnum_t *number, char *data) {
  unsigned long long parsed_number, backup;
  size_t i;

  if (number->numberSize >= 2) {
    switch (number->number[1]) {
    default:
      break;
    case 'x':
    case 'X':
      // The number is a json_parse_flags_allow_hexadecimal_numbers hexadecimal
      // so we have to do extra work to convert it to a non-hexadecimal for JSON
      // output.
      parsed_number = json_strtoull(number->number, 0, 0);

      // We need a copy of parsed number twice, so take a backup of it.
      backup = parsed_number;

      i = 0;

      while (0 != parsed_number) {
        parsed_number /= 10;
        i++;
      }

      // Restore parsed_number to its original value stored in the backup.
      parsed_number = backup;

      // Now use backup to take a copy of i, or the length of the string.
      backup = i;

      do {
        *(data + i - 1) = '0' + (char)(parsed_number % 10);
        parsed_number /= 10;
        i--;
      } while (0 != parsed_number);

      data += backup;

      return data;
    }
  }

  // check to see if the number has leading/trailing decimal point
  i = 0;

  // skip any leading '-'
  if ((i < number->numberSize) &&
      (('+' == number->number[i]) || ('-' == number->number[i]))) {
    i++;
  }

  // check if we have infinity
  if ((i < number->numberSize) && ('I' == number->number[i])) {
    char *inf = "Infinity";
    size_t k;

    for (k = i; k < number->numberSize; k++) {
      const char c = *inf++;

      // Check if we found the Infinity string!
      if ('\0' == c) {
        break;
      } else if (c != number->number[k]) {
        break;
      }
    }

    if ('\0' == *inf++) {
      char *dbl_max;

      // if we had a leading '-' we need to record it in the JSON output
      if ('-' == number->number[0]) {
        *data++ = '-';
      }

      // Inf becomes 1.7976931348623158e308 because JSON can't support it
      for (dbl_max = "1.7976931348623158e308"; '\0' != *dbl_max; dbl_max++) {
        *data++ = *dbl_max;
      }

      return data;
    }
  }

  // check if we have nan
  if ((i < number->numberSize) && ('N' == number->number[i])) {
    char *nan = "NaN";
    size_t k;

    for (k = i; k < number->numberSize; k++) {
      const char c = *nan++;

      // Check if we found the NaN string!
      if ('\0' == c) {
        break;
      } else if (c != number->number[k]) {
        break;
      }
    }

    if ('\0' == *nan++) {
      // NaN becomes 0 because JSON can't support it
      *data++ = '0';
      return data;
    }
  }

  // if we had a leading decimal point
  if ((i < number->numberSize) && ('.' == number->number[i])) {
    i = 0;

    // skip any leading '+'
    if ('+' == number->number[i]) {
      i++;
    }

    // output the leading '-' if we had one
    if ('-' == number->number[i]) {
      *data++ = '-';
      i++;
    }

    // insert a '0' to fix the leading decimal point for JSON output
    *data++ = '0';

    // and output the rest of the number as normal
    for (; i < number->numberSize; i++) {
      *data++ = number->number[i];
    }

    return data;
  }

  for (; i < number->numberSize; i++) {
    const char c = number->number[i];
    if (!('0' <= c && c <= '9')) {
      break;
    }
  }

  // if we had a trailing decimal point
  if ((i + 1 == number->numberSize) && ('.' == number->number[i])) {
    i = 0;

    // skip any leading '+'
    if ('+' == number->number[i]) {
      i++;
    }

    // output the leading '-' if we had one
    if ('-' == number->number[i]) {
      *data++ = '-';
      i++;
    }

    // and output the rest of the number as normal
    for (; i < number->numberSize; i++) {
      *data++ = number->number[i];
    }

    // insert a '0' to fix the trailing decimal point for JSON output
    *data++ = '0';

    return data;
  }

  i = 0;

  // skip any leading '+'
  if ('+' == number->number[i]) {
    i++;
  }

  for (; i < number->numberSize; i++) {
    *data++ = number->number[i];
  }

  return data;
}

static char *json_write_string(const struct das_jsonstr_t *string, char *data) {
  size_t i;

  *data++ = '"'; // open the string

  for (i = 0; i < string->stringSize; i++) {
    switch (string->string[i]) {
    case '"':
      *data++ = '\\'; // escape the control character
      *data++ = '"';
      break;
    case '\\':
      *data++ = '\\'; // escape the control character
      *data++ = '\\';
      break;
    case '\b':
      *data++ = '\\'; // escape the control character
      *data++ = 'b';
      break;
    case '\f':
      *data++ = '\\'; // escape the control character
      *data++ = 'f';
      break;
    case '\n':
      *data++ = '\\'; // escape the control character
      *data++ = 'n';
      break;
    case '\r':
      *data++ = '\\'; // escape the control character
      *data++ = 'r';
      break;
    case '\t':
      *data++ = '\\'; // escape the control character
      *data++ = 't';
      break;
    default:
      *data++ = string->string[i];
      break;
    }
  }

  *data++ = '"'; // close the string

  return data;
}

static char *json_write_minified_array(const struct das_jsonary_t *array,
                                       char *data) {
  struct das_jsonaryel_t *element = 0;

  *data++ = '['; // open the array

  for (element = array->start; 0 != element; element = element->next) {
    if (element != array->start) {
      *data++ = ','; // ','s separate each element
    }

    data = json_write_minified_value(element->value, data);

    if (0 == data) {
      // value was malformed!
      return 0;
    }
  }

  *data++ = ']'; // close the array

  return data;
}

static char *json_write_minified_object(const struct das_jsondict_t *object,
                                        char *data) {
  struct das_jsondictel_t *element = 0;

  *data++ = '{'; // open the object

  for (element = object->start; 0 != element; element = element->next) {
    if (element != object->start) {
      *data++ = ','; // ','s separate each element
    }

    data = json_write_string(element->name, data);

    if (0 == data) {
      // string was malformed!
      return 0;
    }

    *data++ = ':'; // ':'s separate each name/value pair

    data = json_write_minified_value(element->value, data);

    if (0 == data) {
      // value was malformed!
      return 0;
    }
  }

  *data++ = '}'; // close the object

  return data;
}

static char *json_write_minified_value(const struct das_jdo_t *value,
                                       char *data) {
  switch (value->type) {
  default:
    // unknown value type found!
    return 0;
  case DASJSONTYPE_NUM:
    return json_write_number((struct das_jsonnum_t *)value->value, data);
  case DASJSONTYPE_STR:
    return json_write_string((struct das_jsonstr_t *)value->value, data);
  case DASJSONTYPE_ARY:
    return json_write_minified_array((struct das_jsonary_t *)value->value,
                                     data);
  case DASJSONTYPE_DICT:
    return json_write_minified_object((struct das_jsondict_t *)value->value,
                                      data);
  case DASJSONTYPE_TRUE:
    data[0] = 't';
    data[1] = 'r';
    data[2] = 'u';
    data[3] = 'e';
    return data + 4;
  case DASJSONTYPE_FALSE:
    data[0] = 'f';
    data[1] = 'a';
    data[2] = 'l';
    data[3] = 's';
    data[4] = 'e';
    return data + 5;
  case DASJSONTYPE_NULL:
    data[0] = 'n';
    data[1] = 'u';
    data[2] = 'l';
    data[3] = 'l';
    return data + 4;
  }
}

void* DasJdo_newMiniStr(const DasJdo* pThis, size_t *out_size) {
  size_t size = 0;
  char *data = 0;
  char *data_end = 0;

  if (0 == pThis) {
    return 0;
  }

  if (json_write_minified_get_value_size(pThis, &size)) {
    // value was malformed!
    return 0;
  }

  data = (char *)malloc(size + 1);  // plus the '\0' null terminating character

  if (0 == data) {
    // malloc failed!
    return 0;
  }

  data_end = json_write_minified_value(pThis, data);

  if (0 == data_end) {
    // bad chi occurred!
    free(data);
    return 0;
  }

  // null terminated the string
  *data_end = '\0';

  if (0 != out_size) {
    *out_size = size;  // the length, not counting the terminator
  }

  return data;
}

static int json_write_pretty_get_value_size(const struct das_jdo_t *value,
                                            size_t depth, size_t indent_size,
                                            size_t newline_size, size_t *size);

static int json_write_pretty_get_array_size(const struct das_jsonary_t *array,
                                            size_t depth, size_t indent_size,
                                            size_t newline_size, size_t *size) {
  struct das_jsonaryel_t *element;

  *size += 1; // '['

  if (0 < array->length) {
    // if we have any elements we need to add a newline after our '['
    *size += newline_size;

    *size += array->length - 1; // ','s separate each element

    for (element = array->start; 0 != element; element = element->next) {
      // each element gets an indent
      *size += (depth + 1) * indent_size;

      if (json_write_pretty_get_value_size(element->value, depth + 1,
                                           indent_size, newline_size, size)) {
        // value was malformed!
        return 1;
      }

      // each element gets a newline too
      *size += newline_size;
    }

    // since we wrote out some elements, need to add a newline and indentation
    // to the trailing ']'
    *size += depth * indent_size;
  }

  *size += 1; // ']'

  return 0;
}

static int json_write_pretty_get_object_size(const struct das_jsondict_t *object,
                                             size_t depth, size_t indent_size,
                                             size_t newline_size,
                                             size_t *size) {
  struct das_jsondictel_t *element;

  *size += 1; // '{'

  if (0 < object->length) {
    *size += newline_size; // need a newline next

    *size += object->length - 1; // ','s separate each element

    for (element = object->start; 0 != element; element = element->next) {
      // each element gets an indent and newline
      *size += (depth + 1) * indent_size;
      *size += newline_size;

      if (json_write_get_string_size(element->name, size)) {
        // string was malformed!
        return 1;
      }

      *size += 3; // separate each name/value pair with " : "

      if (json_write_pretty_get_value_size(element->value, depth + 1,
                                           indent_size, newline_size, size)) {
        // value was malformed!
        return 1;
      }
    }

    *size += depth * indent_size;
  }

  *size += 1; // '}'

  return 0;
}

static int json_write_pretty_get_value_size(const struct das_jdo_t *value,
                                            size_t depth, size_t indent_size,
                                            size_t newline_size, size_t *size) {
  switch (value->type) {
  default:
    // unknown value type found!
    return 1;
  case DASJSONTYPE_NUM:
    return json_write_get_number_size((struct das_jsonnum_t *)value->value,
                                      size);
  case DASJSONTYPE_STR:
    return json_write_get_string_size((struct das_jsonstr_t *)value->value,
                                      size);
  case DASJSONTYPE_ARY:
    return json_write_pretty_get_array_size(
        (struct das_jsonary_t *)value->value, depth, indent_size, newline_size,
        size);
  case DASJSONTYPE_DICT:
    return json_write_pretty_get_object_size(
        (struct das_jsondict_t *)value->value, depth, indent_size,
        newline_size, size);
  case DASJSONTYPE_TRUE:
    *size += 4; // the string "true"
    return 0;
  case DASJSONTYPE_FALSE:
    *size += 5; // the string "false"
    return 0;
  case DASJSONTYPE_NULL:
    *size += 4; // the string "null"
    return 0;
  }
}

static char *json_write_pretty_value(const struct das_jdo_t *value,
                                     size_t depth, const char *indent,
                                     const char *newline, char *data);

static char *json_write_pretty_array(const struct das_jsonary_t *array,
                                     size_t depth, const char *indent,
                                     const char *newline, char *data) {
  size_t k, m;
  struct das_jsonaryel_t *element;

  *data++ = '['; // open the array

  if (0 < array->length) {
    for (k = 0; '\0' != newline[k]; k++) {
      *data++ = newline[k];
    }

    for (element = array->start; 0 != element; element = element->next) {
      if (element != array->start) {
        *data++ = ','; // ','s separate each element

        for (k = 0; '\0' != newline[k]; k++) {
          *data++ = newline[k];
        }
      }

      for (k = 0; k < depth + 1; k++) {
        for (m = 0; '\0' != indent[m]; m++) {
          *data++ = indent[m];
        }
      }

      data = json_write_pretty_value(element->value, depth + 1, indent, newline,
                                     data);

      if (0 == data) {
        // value was malformed!
        return 0;
      }
    }

    for (k = 0; '\0' != newline[k]; k++) {
      *data++ = newline[k];
    }

    for (k = 0; k < depth; k++) {
      for (m = 0; '\0' != indent[m]; m++) {
        *data++ = indent[m];
      }
    }
  }

  *data++ = ']'; // close the array

  return data;
}

static char *json_write_pretty_object(const struct das_jsondict_t *object,
                                      size_t depth, const char *indent,
                                      const char *newline, char *data) {
  size_t k, m;
  struct das_jsondictel_t *element;

  *data++ = '{'; // open the object

  if (0 < object->length) {
    for (k = 0; '\0' != newline[k]; k++) {
      *data++ = newline[k];
    }

    for (element = object->start; 0 != element; element = element->next) {
      if (element != object->start) {
        *data++ = ','; // ','s separate each element

        for (k = 0; '\0' != newline[k]; k++) {
          *data++ = newline[k];
        }
      }

      for (k = 0; k < depth + 1; k++) {
        for (m = 0; '\0' != indent[m]; m++) {
          *data++ = indent[m];
        }
      }

      data = json_write_string(element->name, data);

      if (0 == data) {
        // string was malformed!
        return 0;
      }

      // " : "s separate each name/value pair
      *data++ = ' ';
      *data++ = ':';
      *data++ = ' ';

      data = json_write_pretty_value(element->value, depth + 1, indent, newline,
                                     data);

      if (0 == data) {
        // value was malformed!
        return 0;
      }
    }

    for (k = 0; '\0' != newline[k]; k++) {
      *data++ = newline[k];
    }

    for (k = 0; k < depth; k++) {
      for (m = 0; '\0' != indent[m]; m++) {
        *data++ = indent[m];
      }
    }
  }

  *data++ = '}'; // close the object

  return data;
}

static char *json_write_pretty_value(const struct das_jdo_t *value,
                                     size_t depth, const char *indent,
                                     const char *newline, char *data) {
  switch (value->type) {
  default:
    // unknown value type found!
    return 0;
  case DASJSONTYPE_NUM:
    return json_write_number((struct das_jsonnum_t *)value->value, data);
  case DASJSONTYPE_STR:
    return json_write_string((struct das_jsonstr_t *)value->value, data);
  case DASJSONTYPE_ARY:
    return json_write_pretty_array((struct das_jsonary_t *)value->value, depth,
                                   indent, newline, data);
  case DASJSONTYPE_DICT:
    return json_write_pretty_object((struct das_jsondict_t *)value->value,
                                    depth, indent, newline, data);
  case DASJSONTYPE_TRUE:
    data[0] = 't';
    data[1] = 'r';
    data[2] = 'u';
    data[3] = 'e';
    return data + 4;
  case DASJSONTYPE_FALSE:
    data[0] = 'f';
    data[1] = 'a';
    data[2] = 'l';
    data[3] = 's';
    data[4] = 'e';
    return data + 5;
  case DASJSONTYPE_NULL:
    data[0] = 'n';
    data[1] = 'u';
    data[2] = 'l';
    data[3] = 'l';
    return data + 4;
  }
}

void *DasJdo_newPrettyStr(const DasJdo* pThis, const char *indent,
                        const char *newline, size_t *out_size) {
  size_t size = 0;
  size_t indent_size = 0;
  size_t newline_size = 0;
  char *data = 0;
  char *data_end = 0;

  if (0 == pThis) {
    return 0;
  }

  if (0 == indent) {
    indent = "  "; // default to two spaces
  }

  if (0 == newline) {
    newline = "\n"; // default to linux newlines
  }

  while ('\0' != indent[indent_size]) {
    ++indent_size; // skip non-null terminating characters
  }

  while ('\0' != newline[newline_size]) {
    ++newline_size; // skip non-null terminating characters
  }

  if (json_write_pretty_get_value_size(pThis, 0, indent_size, newline_size,
                                       &size)) {
    // value was malformed!
    return 0;
  }

  data = (char *)malloc(size + 1);  // plus the '\0' null terminating character

  if (0 == data) {
    // malloc failed!
    return 0;
  }

  data_end = json_write_pretty_value(pThis, 0, indent, newline, data);

  if (0 == data_end) {
    // bad chi occurred!
    free(data);
    return 0;
  }

  // null terminated the string
  *data_end = '\0';

  if (0 != out_size) {
    *out_size = size;  // the length, not counting the terminator
  }

  return data;
}

/* ************************************************************************* */
/* End of Sheredom's original work, add-ons below */

const char* DasJparseResult_info(
	const DasJparseResult* pRes, char* sTmp, size_t uLen
){
	const char* s;
	switch(pRes->error){
	case DASJPARSEERR_NONE: 
		s = "No Problem, why did you call me?";
		break;
	case DASJPARSEERR_EXPECTED_COMMA_OR_CLOSING_BRACKET:
		s = "expected either a comma or a closing '}' or ']' to close an "
		    "object or array";
		break;
	case DASJPARSEERR_EXPECTED_COLON:
		s = "colon separating name/value pair was missing";
		break;
	case DASJPARSEERR_EXPECTED_OPENING_QUOTE:
		s = "expected string to begin with '\"'";
		break;
	case DASJPARSEERR_INVALID_STRING_ESCAPE_SEQUENCE:
		s = "invalid escaped sequence in string";
		break;
	case DASJPARSEERR_INVALID_NUMBER_FORMAT:
		s = "invalid number format";
		break;
	case DASJPARSEERR_INVALID_VALUE:
		s = "invalid value";
		break;
	case DASJPARSEERR_PREMATURE_END_OF_BUFFER:
		s = "reached end of buffer before object/array was complete";
		break;
	case DASJPARSEERR_INVALID_STRING:
		s = "string was malformed";
		break;
	case DASJPARSEERR_ALLOCATOR_FAILED:
		s = "a call to malloc, or a user provider allocator, failed";
		break;
	case DASJPARSEERR_UNEXPECTED_TRAILING_CHARACTERS:
		s = "the JSON input had unexpected trailing characters that weren't "
		    "part of the JSON value";
		break;
  	default:
		s = "Unknown error, something exploded (real bad chi!)";
		break;
	}
	snprintf(sTmp, uLen, "%s @ line %zd, offset %zd", s, pRes->errLine,
			   pRes->errOffset);
	return sTmp;
}

const char* DasJdo_str(const DasJdo* pThis) {
	
	if( (pThis != NULL) &&
	    (pThis->type == DASJSONTYPE_STR) &&
	    (pThis->value != NULL) )
		return ((DasJsonStr*)pThis->value)->string;
	
	return NULL;
}

const DasJsonDict* DasJdo_dict(const DasJdo* pThis) {
	
	if( (pThis != NULL) && (pThis->type == DASJSONTYPE_DICT) )
		return (DasJsonDict*) pThis->value;
	
	return NULL;
}

const DasJsonDictEl* DasJdo_dictFirst(const DasJdo* pThis)
{
	if( (pThis != NULL) && (pThis->type == DASJSONTYPE_DICT) ){
		if(pThis->value) return ((DasJsonDict*)pThis->value)->start;
	}
	return NULL;
}

const DasJsonAryEl* DasJdo_aryFirst(const DasJdo* pThis)
{
	if( (pThis != NULL) && (pThis->type == DASJSONTYPE_ARY) ){
		if(pThis->value) return ((DasJsonAry*)pThis->value)->start;
	}
	return NULL;
}

const DasJsonAry* DasJdo_ary(const DasJdo* pThis) {
	
	if( (pThis != NULL) && (pThis->type == DASJSONTYPE_ARY) )
		return (DasJsonAry*) pThis->value;
	
	return NULL;
}

/* The path parser, allows integers be used on dictionaries as well */
const DasJdo* DasJdo_subtree(const DasJdo* pThis, const char* sRelPath){
	
	const char* pSep = sRelPath;
	while((*pSep != '/')&&(*pSep != '\0')) ++pSep;
		
	size_t uLen = (*pSep == '\0') ? strlen(sRelPath) : pSep - sRelPath;

	if(uLen == 0) return NULL;  /* Invalid relative path */
	bool bRecurse =  ((*pSep != '\0') && (*(pSep+1) != '\0'));
	
	/* A segment of digits is an index, usable for arrays and dictionaries
	 * alike.  Only this segment counts, "0/EMAIL" is index 0 then EMAIL. */
	int iItem = -1;
	if(uLen < 10){
		iItem = 0;
		for(size_t u = 0; u < uLen; ++u){
			if(!isdigit((unsigned char)sRelPath[u])){ iItem = -1; break; }
			iItem = iItem*10 + (sRelPath[u] - '0');
		}
	}
	
	/* Really should avoid linear searches like this */
	int i = 0;
	if(pThis->type == DASJSONTYPE_DICT){
		
		const DasJsonDictEl* pEl = NULL;
		
		for(pEl = DasJdo_dictFirst(pThis); pEl != NULL; pEl = pEl->next){
			
			if((i == iItem)||(
				(strncmp(pEl->name->string, sRelPath, uLen) == 0) && 
				(strlen(pEl->name->string) == uLen)
			)){
				if(bRecurse)
					return DasJdo_subtree(pEl->value, pSep+1);
				else
					return pEl->value;
			}
			++i;
		}
	}
	else{
		if(pThis->type == DASJSONTYPE_ARY){
			const DasJsonAryEl* pEl = NULL;
		
			for(pEl = DasJdo_aryFirst(pThis); pEl != NULL; pEl = pEl->next){
			
				if(i == iItem){
					if(bRecurse)
						return DasJdo_subtree(pEl->value, pSep+1);
					else
						return pEl->value;
				}
				++i;
			}
		}
	}
	return NULL;
}

#if defined(__clang__)
#pragma clang diagnostic pop
#elif defined(_MSC_VER)
#pragma warning(pop)
#endif
