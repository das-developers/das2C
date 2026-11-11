/* Copyright (C) 2026 Chris Piker <chris-piker@uiowa.edu>
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

/** @file source.h Data sources: what they offer and how to ask for it */

#ifndef _das_source_h_
#define _das_source_h_

#include <das3/node.h>
#include <das3/req.h>

#ifdef __cplusplus
extern "C" {
#endif

/** @addtogroup catalog
 * @{
 */

/** @defgroup source Data sources
 *
 * A DasSrc is a standalone description of one place data comes from,
 * built from a DasNode or directly from JSON text.  It holds a copy of
 * that document, so the catalog tree it came from may be freed or
 * evicted and the source keeps working.  Once built it is read only and
 * may be shared between threads.
 *
 * Three things fall out of the description:
 *
 *   - **Properties.** The `interface` section of a source node is a
 *     tree of sections, groups and properties.  A DasSrcProp is one leaf:
 *     the minimum time, the units of the frequency axis, whether the
 *     L-shell variable is output.  Every property has a value; some also
 *     have a DasSrcCtrl saying how a client may change it.  The fixed ones
 *     are not noise, they tell a client what a stream will look like
 *     before any of it is downloaded.
 *   - **Groups.** A DasSrcGroup is the box the properties sit in: a
 *     coordinate, a data variable, an output format, or the one anonymous
 *     box of server options.  Forms are drawn group by group.
 *   - **Requisitions.** A DasSrc makes DasReq objects, blank or seeded
 *     from a worked example, validates them, and turns them into
 *     something a transport can act on.  For a stream source that is a
 *     URL.
 *
 * A DasSrc resolves a requisition to a URL but never fetches from one.
 * Nothing in it opens a socket, so the same code serves a command line
 * tool, a browser build, or a GUI toolkit that provides the network
 * functions.  Facilities such as new_DasHttpResp_get() and DasIO act on the
 * URL afterwards.
 *
 * The common path, through a DasCat:
 *
 * @code
 * DasSrc* pSrc = new_DasSrc_cat(pCat, "uiowa/juno/wav/survey");
 * DasReq* pReq = new_DasReq_src(pSrc, 0);             // seeded from example 0
 * DasReq_set(pReq, "time.res", "30");                 // then adjusted
 *
 * char sUrl[2048];
 * if(DasSrc_reqUrl(pSrc, pReq, 0, sUrl, sizeof(sUrl)) > 0)
 *    new_DasHttpResp_get(sUrl, ...);                  // or fetch(), or QNAM
 *
 * del_DasReq(pReq);
 * del_DasSrc(pSrc);
 * @endcode
 *
 * @anchor src_keys
 * @par Keys
 *
 * Every property has a dotted key built from its section, group and
 * property names as the node spells them: `coords.time.min`,
 * `data.lshell.enabled`, `formats.csv.delimiter`, `options.ascii`.
 * Options have no group, so their keys have two parts.  Keys are how a
 * requisition names a property, and these shorter spellings are accepted
 * as well, so long as the result is unambiguous within one source:
 *
 *   - Section names in singular or plural form, `coord` or `coords`,
 *     `format` or `formats`, `option` or `options`, and `fmt` or `opt`.
 *   - The group and property alone, `time.min`, when only one section
 *     has that group.
 *   - A group alone, `lshell` or `data.lshell`, meaning the group's
 *     `enabled` toggle.
 *   - An option alone, `ascii`, when no group has that name.
 *   - A bare property name, `serial`, when exactly one group has it.
 *   - The property names `min`, `max`, `res` and `inter` may be written
 *     out as `minimum`, `maximum`, `resolution` and `interval`, and
 *     `enable` as `enabled`.  Nodes from 2018 spell them the long way,
 *     later nodes the short way, and users may type either.
 *
 * Matching is case insensitive.  A spelling that fits more than one
 * property is rejected with a message naming the candidates, never
 * guessed.
 *
 * A coordinate name alone, `time`, takes range values:
 *
 *   - `time=2021-02-21,2021-02-22` sets the minimum and maximum, and a
 *     third item sets the resolution or the interval, whichever the
 *     coordinate has.  Empty items are skipped, so `time=,2021-02-22`
 *     sets only the maximum.
 *   - `time=2021-02-21` with no comma means the span that time denotes:
 *     the whole day; `time=2021-02-21T14` the hour from 14:00 to 15:00;
 *     `time=2021` the year.  See DasTime_strToSpan() for the rule.
 *
 * In a well formed source, the group names under `coords` are the physical
 * dimension names of the emitted stream, @see DasDim.  A client that has
 * plotted a stream and needs more data along `time` can look up that
 * group and issue a new requisition from the plot's axis range, which
 * is how zoom and pan become server requests without any source specific
 * code in the client.
 *
 * @par Drawing a form
 *
 * @code
 * for(int sec = 0; sec < DASSRCSEC_SIZE; ++sec){
 *    for(size_t g = 0; g < DasSrc_numGroups(pSrc, sec); ++g){
 *       const DasSrcGroup* pG = DasSrc_groupAt(pSrc, sec, g);
 *       const DasSrcProp* pOn = DasSrcGroup_enableProp(pG);   // the gate, or NULL
 *
 *       for(size_t p = 0; p < DasSrcGroup_numProps(pG); ++p){
 *          const DasSrcProp* pProp = DasSrcGroup_propAt(pG, p);
 *          const DasSrcCtrl* pCtrl = DasSrcProp_ctrl(pProp);
 *          if(pCtrl == NULL) continue;                    // nothing to set here
 *
 *          const char* sNow = DasReq_val(pReq, DasSrcProp_key(pProp));
 *          draw_control(DasSrcProp_type(pProp), pCtrl, sNow ? sNow : DasSrcProp_value(pProp));
 *       }
 *    }
 * }
 * @endcode
 *
 * The verdicts to mark boxes with come from DasSrc_check(), read back by
 * entry through DasReq_verdictAt() and DasReq_whyAt().
 *
 * @par Two node layouts
 *
 * Source nodes from 2018 and from current servers differ in spelling and
 * in where examples live.  Both are read and nothing here depends on
 * which one a source uses: examples from either come out as requisitions
 * in interface terms.
 * @{
 */

/** The parts of a source interface, in presentation order.
 * `DASSRCSEC_SIZE` is the number of real sections, handy for sizing
 * per section arrays. */
typedef enum das_srcsec_e {
	DASSRCSEC_COORD = 0,  /**< "coords": time, frequency, L-shell ... */
	DASSRCSEC_DATA,       /**< "data": the variables a source can emit */
	DASSRCSEC_FORMAT,     /**< "formats": output serializations and their knobs */
	DASSRCSEC_OPTION,     /**< "options": server specific switches, one group */
	DASSRCSEC_SIZE
} das_srcsec;

/** The name of a section as it appears in keys, "coords", "data",
 * "formats" or "options".  "" for DASSRCSEC_SIZE. */
DAS_API const char* dassrcsec_name(das_srcsec sec);

/** Kinds of source.  Only stream sources are implemented today. */
typedef enum das_srckind_e {
	DASSRCKIND_INVALID = 0,
	DASSRCKIND_STREAM,    /**< node type HttpStreamSrc, see DasSrc_reqUrl() */
	DASSRCKIND_FILE       /**< node type FileTimeAgg, reserved */
} das_srckind;

/** The kind of value a property holds.
 *
 * Nodes name types with a handful of spellings that have drifted over
 * the years.  They are folded into this set when a source is built.  A
 * type the library does not recognize is reported as text and the
 * node's spelling is still available via DasSrcProp_typeName().
 */
typedef enum das_srcproptype_e {
	DASSRCPROPTYPE_TEXT = 0,  /**< any string, including unrecognized types */
	DASSRCPROPTYPE_BOOL,      /**< true or false, see DasSrcCtrl_check() for spellings */
	DASSRCPROPTYPE_INT,       /**< an integer in decimal */
	DASSRCPROPTYPE_REAL,      /**< a real number */
	DASSRCPROPTYPE_TIME,      /**< a UTC time, ISO 8601 */
	DASSRCPROPTYPE_ENUM       /**< one of a fixed list, see DasSrcCtrl_numEnums() */
} das_srcproptype;

/** @class DasSrc source.h <das3/source.h>
 *
 * A data source description.  See the group description. */
typedef struct das_src_t DasSrc;

/** @class DasSrcGroup source.h <das3/source.h>
 *
 * A box on the form.  Owned by its source.  */
typedef struct das_srcgroup_t DasSrcGroup;

/** @class DasSrcProp source.h <das3/source.h>
 *
 * One property of a source, fixed or adjustable.  Owned by its source. */
typedef struct das_srcprop_t DasSrcProp;

/** @class DasSrcCtrl source.h <das3/source.h>
 *
 * How an adjustable property may be changed.  Owned by its property. */
typedef struct das_srcctrl_t DasSrcCtrl;

/* ************************************************************************* */
/* Making and identifying sources */

/** The children of a catalog node that new_DasSrc_node() can load
 *
 * Catalog paths provided by application programs usually end at a
 * Collection or SourceSet whose children are the actual data sources.
 * Multiple sources are listed because different clients understand how
 * to fully utilize different data source types.  This is the set of
 * them this library interacts with, HttpStreamSrc nodes today.  Which
 * of several to use is the application's decision, made on whatever
 * criteria it has, such as the media types each child says it provides.
 *
 * @param pNode A container node.  A source node itself has no children
 *        and reports 0.  A container that only lists other containers
 *        also reports 0.
 *
 * @param aNames Receives pointers to the child names, borrowed from the
 *        node.  May be NULL with uMax 0 to just count.
 *
 * @param uMax The size of aNames.
 *
 * @returns The number of loadable children, which may exceed uMax.
 */
DAS_API size_t DasSrc_loadable(const DasNode* pNode, const char** aNames, size_t uMax);

/** Build a DasSrc from a DasNode.
 *
 * The node's document is copied, so the node and its tree may be freed
 * afterwards.  Usually reached through new_DasSrc_cat().
 *
 * The node must be a source itself.  A container is refused with its
 * loadable children named in the log, see DasSrc_loadable().
 *
 * @param pNode A node of type HttpStreamSrc.  Borrowed.
 * 
 * @returns A new source, to be freed with del_DasSrc(), or NULL after
 *        logging why the node does not describe a source.  A source
 *        with a thin or broken interface still builds, so that it can
 *        at least be listed; missing protocol pieces are reported by
 *        whichever function builds a request from it.
 */
DAS_API DasSrc* new_DasSrc_node(const DasNode* pNode);

/** Build a DasSrc from the JSON text of a node, with no DasNode involved.
 *
 * For programs that fetch documents themselves, a browser build in
 * particular, and for anything else that can produce a source document.
 *
 * @param sDoc The JSON text, need not be NUL terminated.
 * @param uLen The number of bytes in sDoc.
 * @param sPathUri The catalog URI of the node, shorthands accepted.  It
 *        is the source's identity: requisitions are stamped with it and
 *        later checked against it, see DasSrc_check().  NULL or empty
 *        leaves the source without one, and its requisitions unstamped.
 * @param sUrl Where the text came from, recorded as the source's URL and
 *        used to resolve relative links.  May be NULL.
 * @returns A new source, or NULL after logging a parse problem or a
 *        document that is not a source node.
 */
DAS_API DasSrc* new_DasSrc_json(
	const char* sDoc, size_t uLen, const char* sPathUri, const char* sUrl
);

DAS_API void del_DasSrc(DasSrc* pThis);

/** @memberof DasSrc */
DAS_API das_srckind DasSrc_kind(const DasSrc* pThis);

/** The catalog URI, the source's identity, or "" for a source given none
 * @memberof DasSrc */
DAS_API const char* DasSrc_path(const DasSrc* pThis);

/** Where the source document was loaded from, "" if unknown.  @memberof DasSrc */
DAS_API const char* DasSrc_url(const DasSrc* pThis);

/** The node's short name, label, or "" .  @memberof DasSrc */
DAS_API const char* DasSrc_name(const DasSrc* pThis);

/** The node's title, or "".  @memberof DasSrc */
DAS_API const char* DasSrc_title(const DasSrc* pThis);

/** The node format version, "0.7" and so on, or "".  @memberof DasSrc */
DAS_API const char* DasSrc_version(const DasSrc* pThis);

/** True when the protocol section says requests are authenticated.
 * Servers may still ask when this is false; it is advice for the form,
 * not a promise.  @memberof DasSrc */
DAS_API bool DasSrc_needsAuth(const DasSrc* pThis);

/** The login realm named in the protocol section, or "" if none is given.
 * @memberof DasSrc */
DAS_API const char* DasSrc_authRealm(const DasSrc* pThis);

/** The number of listed contacts for the data.  @memberof DasSrc */
DAS_API size_t DasSrc_numContacts(const DasSrc* pThis);

/** Contact number uIdx.
 * @param pThis The source
 * @param uIdx Which contact, 0 to DasSrc_numContacts() - 1
 * @param psEmail Receives the address, or "" if none listed.  May be NULL.
 * @returns The name, or NULL past the end.  @memberof DasSrc */
DAS_API const char* DasSrc_contactAt(const DasSrc* pThis, size_t uIdx, const char** psEmail);

/** The source document, or an element of it by DasJdo_subtree() path, for
 * anything the functions here do not model.  NULL when the path finds
 * nothing.  @memberof DasSrc */
DAS_API const DasJdo* DasSrc_jdo(const DasSrc* pThis, const char* sFragment);

/* ************************************************************************* */
/* Groups */

/** The number of groups in a section, 0 for a section the source does
 * not have.  Coordinates list `time` first and the rest in node order;
 * the options section has one group when it has anything.
 * @memberof DasSrc */
DAS_API size_t DasSrc_numGroups(const DasSrc* pThis, das_srcsec sec);

/** Group number uIdx of a section, or NULL past the end.  @memberof DasSrc */
DAS_API const DasSrcGroup* DasSrc_groupAt(const DasSrc* pThis, das_srcsec sec, size_t uIdx);

/** The group of a section with a given name, case insensitively, or
 * NULL.  Pass "" for the options group.  @memberof DasSrc */
DAS_API const DasSrcGroup* DasSrc_group(
	const DasSrc* pThis, das_srcsec sec, const char* sName
);

/** @memberof DasSrcGroup */
DAS_API das_srcsec DasSrcGroup_section(const DasSrcGroup* pThis);

/** The name as the node spells it, "time", "lshell", "csv", or "" for
 * the options group.  @memberof DasSrcGroup */
DAS_API const char* DasSrcGroup_name(const DasSrcGroup* pThis);

/** A short caption: the node's label, else its name.  @memberof DasSrcGroup */
DAS_API const char* DasSrcGroup_label(const DasSrcGroup* pThis);

/** A longer description, or "".  @memberof DasSrcGroup */
DAS_API const char* DasSrcGroup_title(const DasSrcGroup* pThis);

/** The property that switches the whole group on and off, or NULL.
 *
 * Data variables usually have one, formats always do, coordinates and
 * options never.  A form draws it first and greys the rest of the group
 * when it is off.  @memberof DasSrcGroup */
DAS_API const DasSrcProp* DasSrcGroup_enableProp(const DasSrcGroup* pThis);

/** The name of the exclusive set this group's enable toggle belongs to,
 * or NULL.  Formats are in one, "format": exactly one is on at a time,
 * so they are radio buttons rather than check boxes.  @memberof DasSrcGroup */
DAS_API const char* DasSrcGroup_xorGroup(const DasSrcGroup* pThis);

/** The number of MIME types a format group may produce, 0 for other
 * groups.  @memberof DasSrcGroup */
DAS_API size_t DasSrcGroup_numMimes(const DasSrcGroup* pThis);

/** MIME type number uIdx of a format group, or NULL.  The first is the
 * usual one.  @memberof DasSrcGroup */
DAS_API const char* DasSrcGroup_mimeAt(const DasSrcGroup* pThis, size_t uIdx);

/** @memberof DasSrcGroup */
DAS_API size_t DasSrcGroup_numProps(const DasSrcGroup* pThis);

/** Property number uIdx of the group, in node order, or NULL.
 * @memberof DasSrcGroup */
DAS_API const DasSrcProp* DasSrcGroup_propAt(const DasSrcGroup* pThis, size_t uIdx);

/** The property of a group with a given name, accepting the long and
 * short spellings listed under @ref src_keys, or NULL.  @memberof DasSrcGroup */
DAS_API const DasSrcProp* DasSrcGroup_prop(const DasSrcGroup* pThis, const char* sName);

/** How many of the group's properties have controls.  0 means a fixed
 * group, which a form may skip and a verbose listing may grey out.
 * @memberof DasSrcGroup */
DAS_API size_t DasSrcGroup_numCtrls(const DasSrcGroup* pThis);

/* ************************************************************************* */
/* Properties */

/** The number of properties over all sections.  @memberof DasSrc */
DAS_API size_t DasSrc_numProps(const DasSrc* pThis);

/** Property number uIdx in presentation order: sections as in
 * das_srcsec, groups and properties within them in group order, so
 * a group's properties are always adjacent.  @memberof DasSrc */
DAS_API const DasSrcProp* DasSrc_propAt(const DasSrc* pThis, size_t uIdx);

/** Find the one property a key names.
 *
 * Any spelling from @ref src_keys is accepted.  Fixed properties are
 * found too, so a caller that means to set one still has to look at
 * DasSrcProp_ctrl().  A key that fits nothing or more than one property
 * returns NULL; the reason is not logged, since this is also how a
 * command line tool asks "is this a key", and sWhy says it instead.
 *
 * @param pThis The source
 * @param sKey The key as typed.
 * @param sWhy If not NULL, receives "" on success or a message naming
 *        the problem and, for an ambiguous key, the candidates.
 * @param uWhy The size of sWhy.
 * @returns The property, or NULL.
 * @memberof DasSrc
 */
DAS_API const DasSrcProp* DasSrc_findProp(
	const DasSrc* pThis, const char* sKey, char* sWhy, size_t uWhy
);

/** @memberof DasSrcProp */
DAS_API das_srcsec DasSrcProp_section(const DasSrcProp* pThis);

/** The group this property belongs to.  @memberof DasSrcProp */
DAS_API const DasSrcGroup* DasSrcProp_group(const DasSrcProp* pThis);

/** The property name as the node spells it, "min" or "minimum",
 * "enabled", "delimiter".  Use DasSrcProp_key() rather than rebuilding
 * keys from this.  @memberof DasSrcProp */
DAS_API const char* DasSrcProp_name(const DasSrcProp* pThis);

/** The full dotted key, see @ref src_keys.  Unique within a source.
 * @memberof DasSrcProp */
DAS_API const char* DasSrcProp_key(const DasSrcProp* pThis);

/** A short human readable title.
 *
 * The node's title is returned when it has one.  Coordinate range
 * properties and enable toggles usually have none, since their meaning
 * is conventional, so one is composed in sBuf from the group's label,
 * "Minimum Time value to output", "Output as CSV".
 *
 * @param pThis The property
 * @param sBuf Space for a composed title, 64 bytes covers the composed
 *        forms.
 * @param uLen The size of sBuf.
 * @returns A pointer to the title, inside the source or in sBuf.  Never
 *        NULL, but may be "" for an option with no title at all.
 * @memberof DasSrcProp
 */
DAS_API const char* DasSrcProp_title(const DasSrcProp* pThis, char* sBuf, size_t uLen);

/** The units the value is in, as the node states them, "UTC" for a time
 * with no stated units, or "".  Display only, values are not checked
 * against units.  @memberof DasSrcProp */
DAS_API const char* DasSrcProp_units(const DasSrcProp* pThis);

/** @memberof DasSrcProp */
DAS_API das_srcproptype DasSrcProp_type(const DasSrcProp* pThis);

/** The node's spelling of the type, "isotime", "boolean", or ""
 * when it gave none and the type was inferred.  @memberof DasSrcProp */
DAS_API const char* DasSrcProp_typeName(const DasSrcProp* pThis);

/** The property's value as the node spells it, numbers in the node's
 * notation.
 *
 * For a fixed property this is the fact: the units of an axis, the
 * valid range of a coordinate.  For an adjustable one it is the value in
 * effect when a requisition says nothing, the default.  NULL when the
 * node gives null or nothing, which for a required control means the
 * client must supply one.  @memberof DasSrcProp */
DAS_API const char* DasSrcProp_value(const DasSrcProp* pThis);

/** The control for an adjustable property, NULL for a fixed one.
 * @memberof DasSrcProp */
DAS_API const DasSrcCtrl* DasSrcProp_ctrl(const DasSrcProp* pThis);

/* ************************************************************************* */
/* Controls */

/** The property this control adjusts.  @memberof DasSrcCtrl */
DAS_API const DasSrcProp* DasSrcCtrl_prop(const DasSrcCtrl* pThis);

/** True when a requisition must supply a value or the server will refuse
 * the request.  A required control whose property has a value is
 * satisfied by that default.  @memberof DasSrcCtrl */
DAS_API bool DasSrcCtrl_isRequired(const DasSrcCtrl* pThis);

/** The name of the protocol parameter a value travels in, "start_time"
 * or "read.time.min".  Several controls may share one, toggles that pack
 * into a flag set in particular.  For messages and for people composing
 * requests by hand.  @memberof DasSrcCtrl */
DAS_API const char* DasSrcCtrl_param(const DasSrcCtrl* pThis);

/** The number of allowed values of an enumerated control, 0 otherwise.
 * @memberof DasSrcCtrl */
DAS_API size_t DasSrcCtrl_numEnums(const DasSrcCtrl* pThis);

/** Allowed value number uIdx as the node spells it, or NULL.  Checks
 * accept these in any letter case; what is sent is the node's spelling.
 * @memberof DasSrcCtrl */
DAS_API const char* DasSrcCtrl_enumAt(const DasSrcCtrl* pThis, size_t uIdx);

/** The stated range of a numeric or time control.
 *
 * Nodes put ranges in several places: on the set block, on the protocol
 * parameter, or for coordinate `min` and `max` on the coordinate itself
 * as its valid range.  All three are consulted, in that order.  The high
 * end of a time range is often the word "now".
 *
 * @param pThis The control
 * @param psLo Receives the low end, as the node spells it.  May be NULL.
 * @param psHi Receives the high end.  May be NULL.
 * @returns true if a range was stated, false otherwise with the outputs
 *        untouched.
 * @memberof DasSrcCtrl
 */
DAS_API bool DasSrcCtrl_range(const DasSrcCtrl* pThis, const char** psLo, const char** psHi);

/** Decide if a string is an acceptable value for this control.
 *
 * Booleans accept true, false, yes, no, on, off, 1 and 0 in any letter
 * case.  Enumerations accept any listed value, case insensitively.
 * Integers, reals and times are checked for form only, not against the
 * stated range, because servers are the authority on what they hold and
 * ranges in nodes are routinely out of date.  Times also accept the word
 * "now".  Text is always acceptable.
 *
 * DasSrc_check() calls this for every entry, so applications rarely
 * need to, but a settings panel may want to validate as the user types.
 *
 * @param pThis The control
 * @param sVal The value to test
 * @param sWhy If not NULL, receives "" or a message naming the key, the
 *        value and, for an enumeration, the choices.
 * @param uWhy The size of sWhy.
 * @returns DAS_OKAY or DASERR_SRC.  Nothing is logged.
 * @memberof DasSrcCtrl
 */
DAS_API das_errcode DasSrcCtrl_check(
	const DasSrcCtrl* pThis, const char* sVal, char* sWhy, size_t uWhy
);

/* ************************************************************************* */
/* Requisitions */

/** The number of worked examples a source provides, possibly 0.
 * @memberof DasSrc */
DAS_API size_t DasSrc_numExamples(const DasSrc* pThis);

/** What example number uIdx shows, from its label, title or name, "" if
 * it has none, NULL past the end.  @memberof DasSrc */
DAS_API const char* DasSrc_exampleLblAt(const DasSrc* pThis, size_t uIdx);

/** The argument to new_DasReq_src() for a requisition with nothing filled in */
#define DASSRC_BLANK_REQ -1

/** Make a DasReq for this source.
 *
 * With DASSRC_BLANK_REQ the requisition lists every required key with an
 * empty value, so a form knows what must be filled and a check on it
 * fails usefully.  With an example number it carries that example's
 * settings instead, in interface terms whatever the node layout, and the
 * example's label.  Either way it is stamped for this source.
 *
 * Nothing stops a client from making a requisition with new_DasReq()
 * and never involving the source until it checks; this is the
 * convenience that knows what the source wants.
 *
 * @returns A new requisition, to be freed with del_DasReq(), or NULL
 *        after logging that there is no such example.
 * @memberof DasSrc
 */
DAS_API DasReq* new_DasReq_src(const DasSrc* pThis, int nExample);

/** Stamp a requisition as belonging to this source, see DasReq_setSrc().
 * @memberof DasSrc */
DAS_API void DasSrc_stampReq(const DasSrc* pThis, DasReq* pReq);

/** Validate a request against this source.
 *
 * Every entry gets a verdict and the bad ones a reason, see DasReq_whyAt().
 * In order:
 *
 *   1. A DasReq stamped with another catalog URI is refused outright
 *      and nothing is validated.  Only the URI is compared, so a mirror
 *      or a local mount of the same node validates the same requisitions.
 *      An unstamped one is validated by whoever holds it.
 *   2. Each key is resolved as DasSrc_findProp() would.  No match,
 *      more than one match, or a fixed property is bad.  A coordinate
 *      group name with range values, see @ref src_keys, resolves to
 *      derived entries for the minimum, maximum and step; the client's
 *      entry stays so that a form's one box stays one box.
 *   3. Each value is checked with DasSrcCtrl_check().  An empty value
 *      is bad for a required control with no default, and otherwise
 *      means "not set".
 *   4. Every required control not covered by an entry or a default gets
 *      a derived entry with an empty value and a bad verdict, so a form
 *      can mark a box the user never touched.
 *
 * Derived entries from an earlier check are dropped first, so checking
 * is idempotent.  Nothing is logged for bad entries, the verdicts carry
 * the text; the function that builds a request from them reports them
 * when it refuses.
 *
 * @param pThis The source
 * @param pReq The requisition to check; its verdicts are updated
 * @param puBad If not NULL, receives the number of bad entries.
 * @returns DAS_OKAY when every entry is good, DASERR_SRC when any is bad
 *        or the stamp does not match, the latter after logging.
 * @memberof DasSrc
 */
DAS_API das_errcode DasSrc_check(const DasSrc* pThis, DasReq* pReq, size_t* puBad);

/** The MIME type a requisition is expected to produce.
 *
 * Taken from the enabled format group after the requisition's choices,
 * or the node's default when it states one.  Where a group lists several
 * types the first is returned; the server's Content-Type header is the
 * final word.  Tells a client whether to parse the body as a das stream,
 * hand it to a table viewer, or write it to disk.
 *
 * @returns The type, or "" when the source does not say.
 * @memberof DasSrc
 */
DAS_API const char* DasSrc_mime(const DasSrc* pThis, const DasReq* pReq);

/* ************************************************************************* */
/* Stream sources */

/** The number of base URLs a stream source may be reached at, usually
 * 1.  More means mirrors, tried in order by a careful client.  0 for a
 * source that is not a stream source or has no protocol section.
 * @memberof DasSrc */
DAS_API size_t DasSrc_numBaseUrls(const DasSrc* pThis);

/** Base URL number uIdx, before any parameters, or NULL.  Old nodes
 * carry a query string in it already; DasSrc_reqUrl() appends either
 * way.  @memberof DasSrc */
DAS_API const char* DasSrc_baseUrlAt(const DasSrc* pThis, size_t uIdx);

/** The request URL for a requisition.
 *
 * Checks the requisition, then translates each good entry to protocol
 * parameters through its control's set block: a plain value, a fixed
 * protocol value, or a flag packed into a parameter shared with others
 * of its set.  A value equal to the default is not sent unless the
 * control is required; required controls with defaults are sent even
 * when the requisition is silent.  Raw parameters go on last and
 * override.  Finally every protocol parameter the node marks required
 * must be present.  Values are percent encoded, see dasurl_encode().
 *
 * Works like snprintf: the return value is the length the whole URL
 * needs, and the buffer holds as much as fits, NUL terminated.
 *
 * @param pThis The source
 * @param pReq The requisition.  Its verdicts are updated as by
 *        DasSrc_check().
 * @param uBase Which base URL, see DasSrc_numBaseUrls().  0 unless an
 *        earlier attempt failed to connect.
 * @param sBuf Receives the URL.  2048 bytes is enough for any node seen
 *        so far, 4096 is safe.
 * @param uLen The size of sBuf.
 * @returns The length of the URL, or 0 after logging why none could be
 *        made: every bad entry, a mismatched stamp, a missing required
 *        parameter no key supplies, a bad base index, or a source with
 *        no base URLs.  A URL is never empty, so 0 is unambiguous.
 * @memberof DasSrc
 */
DAS_API size_t DasSrc_reqUrl(
	const DasSrc* pThis, DasReq* pReq, size_t uBase, char* sBuf, size_t uLen
);

/** @} */
/** @} */

#ifdef __cplusplus
}
#endif

#endif /* _das_source_h_ */
