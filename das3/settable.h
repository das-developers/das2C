/* Copyright (C) 2026 Chris Piker <chris-piker@uiowa.edu>
 *
 * This file is part of das2C, the Core Das2 C Library.
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

/** @file settable.h The adjustable properties of a data source */

#ifndef _das_settable_h_
#define _das_settable_h_

#include <das3/util.h>

#ifdef __cplusplus
extern "C" {
#endif

/** @addtogroup catalog
 * @{
 */

/** @defgroup settable Source settings
 *
 * A data source node describes what a client may ask for in its "interface"
 * section: coordinate ranges, which data variables to include, output
 * formats and server specific options.  Each entry that a client may change
 * carries a "set" block saying how a value for it reaches the server.
 * The library flattens those entries into a list of DasSettable objects
 * when a source node loads, one per changeable property, and the node
 * hands them out by index.  Everything else in the interface is
 * documentation and is reached through DasNode_getJdo() as before.
 *
 * A DasSettable is a description.  It says what a property is, what values
 * it takes and what it is called, and it never changes after the node
 * loads.  Values chosen for a particular request live in a query object
 * (see source.h), so one node can serve any number of queries at once.
 *
 * @anchor settable_keys
 * ### Keys
 *
 * Every settable has a dotted key built from its section, group and
 * property names as the node spells them, for example `coords.time.min`,
 * `data.lshell.enabled`, `formats.csv.delim` or `options.text`.  Options
 * have no group, so their keys have two parts.  Keys are how a request
 * names a settable, and a query accepts the following shorter spellings as
 * well, so long as the result is unambiguous within the source:
 *
 *   - Section names in singular or plural form, `coord` or `coords`,
 *     `format` or `formats`, `option` or `options`, and `fmt` or `opt`.
 *   - The group and property alone, `time.min`, when only one section has
 *     that group.
 *   - A group alone, `lshell` or `data.lshell`, meaning the group's
 *     `enabled` toggle.
 *   - An option alone, `text`, when no group has that name.
 *   - The property names `min`, `max`, `res` and `inter` may be written
 *     out as `minimum`, `maximum`, `resolution` and `interval`, and
 *     `enable` as `enabled`.  Nodes from 2018 spell them the long way,
 *     later nodes the short way, and users may type either.
 *
 * Matching is case insensitive.  A spelling that fits more than one
 * settable is an error that names the candidates, not a guess.  Section
 * names are ordered as in das_src_sec_e, which is also the order nodes
 * present them in.
 */

/** The parts of a source interface, in presentation order.
 *
 * `das_src_sec_size` is the number of real sections and is handy for
 * sizing per section arrays.
 */
typedef enum das_src_sec_enum {
	das_src_sec_coord = 0,  /**< "coords": time, frequency, L-shell ... */
	das_src_sec_data,       /**< "data": the variables a source can emit */
	das_src_sec_format,     /**< "formats": output serializations and their knobs */
	das_src_sec_option,     /**< "options": server specific switches, no groups */
	das_src_sec_size
} das_src_sec_e;

/** The name of a section as it appears in keys, "coords", "data",
 * "formats" or "options".  Returns "" for das_src_sec_size. */
DAS_API const char* das_src_sec_name(das_src_sec_e sec);

/** The kind of value a settable takes.
 *
 * Source nodes name types with a handful of spellings that have drifted
 * over the years.  They are folded into this set when the node loads so
 * that clients need not know the history.  A type the library does not
 * recognize is reported as das_settable_text and the node's spelling of it
 * is still available via DasSettable_typeName().
 */
typedef enum das_settable_type_enum {
	das_settable_text = 0,  /**< any string, including unrecognized types */
	das_settable_bool,      /**< true or false, with the spellings DasSettable_check() allows */
	das_settable_int,       /**< an integer in decimal */
	das_settable_real,      /**< a real number */
	das_settable_time,      /**< a UTC time, ISO 8601 preferred */
	das_settable_enum       /**< one of a fixed list, see DasSettable_enumCount() */
} das_settable_type_e;

/** One changeable property of a data source.
 *
 * Settables are owned by the source node that lists them and are valid for
 * as long as that node is.  Applications never create or free them; they
 * ask the node for them by index.  All accessors take a const pointer and
 * may be called from several threads at once.
 */
typedef struct das_settable DasSettable;

/** The section of the interface this settable came from */
DAS_API das_src_sec_e DasSettable_section(const DasSettable* pThis);

/** The group name as the node spells it.
 *
 * For a coordinate that is the coordinate name, `time`; for a variable or
 * format its name, `lshell` or `csv`.  Options have no group and return
 * "" (never NULL), so the result is always safe to print.
 */
DAS_API const char* DasSettable_group(const DasSettable* pThis);

/** The property name as the node spells it, `min` or `minimum`,
 * `enabled`, `delim`.  Use DasSettable_key() rather than rebuilding keys
 * from this, since the key is what a query matches against. */
DAS_API const char* DasSettable_prop(const DasSettable* pThis);

/** The full dotted key, see @ref settable_keys.  Unique within a source. */
DAS_API const char* DasSettable_key(const DasSettable* pThis);

/** A short human readable title for the property.
 *
 * The node's own title is returned when it has one.  Coordinate range
 * properties and enable toggles usually have none, since their meaning is
 * conventional, so a title is composed for them in sBuf from the group's
 * label, for example "Minimum Time value to output" or "Output as CSV".
 * Anything else without a title gets the group label and property name.
 *
 * @param sBuf Space for a composed title.  64 bytes covers the composed
 *        forms, longer group labels are cut to fit.
 * @param uLen The size of sBuf.
 * @returns A pointer to the title, which is either inside the node's
 *        document or is sBuf.  Never NULL, but may be "" for an option
 *        with no title at all.
 */
DAS_API const char* DasSettable_title(const DasSettable* pThis, char* sBuf, size_t uLen);

/** The units a value is expressed in, as a string the node supplies, or
 * "UTC" for time typed settables with no stated units, or "" when there
 * are none.  Units are for display and are not checked against values. */
DAS_API const char* DasSettable_units(const DasSettable* pThis);

/** The kind of value this settable takes */
DAS_API das_settable_type_e DasSettable_type(const DasSettable* pThis);

/** The node's own spelling of the type, "isotime", "real", "boolean" and
 * so on, or "" when the node gave none and the type was inferred.  For
 * display, DasSettable_type() is the one to branch on. */
DAS_API const char* DasSettable_typeName(const DasSettable* pThis);

/** True when a query must supply a value, or the server will refuse the
 * request.  A required settable with a default is satisfied by the default,
 * see DasSettable_defVal(). */
DAS_API bool DasSettable_required(const DasSettable* pThis);

/** The value in effect when a query sets nothing, as the node spells it,
 * or NULL if the node has no default.  Numbers are given in the node's
 * own notation, "86.4", not reformatted. */
DAS_API const char* DasSettable_defVal(const DasSettable* pThis);

/** The name of the protocol parameter that carries this setting to the
 * server, "start_time" or "read.time.min".  Several settables may share a
 * parameter, boolean toggles that pack into one flag set in particular.
 * Mostly of interest when composing requests by hand. */
DAS_API const char* DasSettable_param(const DasSettable* pThis);

/** The number of allowed values for an enumerated settable, 0 for any
 * other type. */
DAS_API size_t DasSettable_enumCount(const DasSettable* pThis);

/** Allowed value number uIdx of an enumerated settable, as the node spells
 * it, or NULL when uIdx is out of range.  DasSettable_check() accepts
 * these in any letter case; the value a query sends is the node's. */
DAS_API const char* DasSettable_enum(const DasSettable* pThis, size_t uIdx);

/** The stated range of a numeric or time settable.
 *
 * Nodes put ranges in several places: on the set block, on the protocol
 * parameter, or for coordinate `min` and `max` on the coordinate itself as
 * its valid range.  All three are consulted, in that order.  The upper end
 * of a time range is often the word "now".
 *
 * @param psLo Receives the low end, as the node spells it.  May be NULL.
 * @param psHi Receives the high end.  May be NULL.
 * @returns true if a range was stated, false otherwise, in which case the
 *        output pointers are not touched.
 */
DAS_API bool DasSettable_range(
	const DasSettable* pThis, const char** psLo, const char** psHi
);

/** Decide if a string is an acceptable value for this settable.
 *
 * Booleans accept true, false, yes, no, on, off, 1 and 0 in any letter
 * case.  Enumerations accept any listed value, case insensitively.
 * Integers, reals and times are checked for form only, not against the
 * stated range, because servers are the authority on what they hold and
 * ranges in nodes are routinely out of date.  Text is always acceptable.
 *
 * Queries call this before storing a value, so applications rarely need to,
 * but a settings panel may want to validate as the user types.
 *
 * @returns DAS_OKAY, or DASERR_SRC after logging a message that names the
 *        key, the offending value and, for an enumeration, the choices.
 */
DAS_API DasErrCode DasSettable_check(const DasSettable* pThis, const char* sVal);

/** @} */
/** @} */

#ifdef __cplusplus
}
#endif

#endif /* _das_settable_h_ */
