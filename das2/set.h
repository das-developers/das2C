/* Copyright (C) 2026 Chris Piker <chris-piker@uiowa.edu>
 *
 * Author: C. Piker, via Claude Fable 5
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

/** @file set.h The DasSet layer: named value sets with formalisms.
 *
 * The successor to variable.h.  It grows here beside the current code; when
 * it can do everything variable.h and friends do, they are removed.  No rip
 * and replace.  Join and succeed.  XML snippets show the stream form of each
 * type right next to the struct that carries it.
 *
 * Design record: co_notes/libdas_wire_model_pilot.md,
 * co_notes/libdas_type_extension_map.md, co_notes/libdas_set_sketch_notes.md.
 */

#ifndef _das_set_h_
#define _das_set_h_

#include <das2/descriptor.h>
#include <das2/datum.h>
#include <das2/units.h>
#include <das2/generator.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Naming convention (Dude's): heap objects with constructors and reference
   counts take Java-style CamelCase (DasSet, DasGen, DasCompSet).  Stack,
   embedded, and static things take snake_case (das_elem_type, das_form_kind,
   das_set_vt), matching das_time and das_geovec.  A formalism table row and a
   vtable are static, not heap, so they are snake_case. */

/* ========================================================================= *
 * THE FOUR AXES
 *
 * A variable, here a DasSet, is described by four axes that today's das_val_type
 * plus a semantic string tangle together.  Pulling them apart is the point of
 * this refactor.
 *
 *   Axis A, the generator (gt).  How the values are produced: array, sequence,
 *           constant, operation.  Lives in generator.h.  A set OWNS one.
 *
 *   Axis B, the element type (et).  What one cell holds.  Lives in generator.h.
 *           A pinned, restricted subset of vt.
 *
 *   Axis C, the structural family (sf).  What the wire element name carries:
 *           scalar, string, blob, composite.  This is the set's own stored
 *           identity.  The RICH presentation vocabulary (vector, complex,
 *           rotation...) is NOT stored: it is a view computed from C + D,
 *           because for composites the formalism fully determines it and
 *           duplicated facts drift.  (Historical note: the family's first
 *           home was a four-class list, DasSetScalar/Str/Blob/Comp; the
 *           classes contracted to machinery boundaries and the family is now
 *           one field.)
 *
 *   Axis D, the formalism.  What math the values obey: none, point, geovec,
 *           complex, rotation.  A set HAS one, symmetric with the generator:
 *           pGen says how values are produced, form says what they mean to
 *           arithmetic.  On the wire this is the skippable <formalism>
 *           element; in C it is an embedded das_formalism binding the set to
 *           a static kind row plus that row's context bindings.
 *
 *   Semantic is NOT a carried axis.  It is a DIRECTIVE, consumed by the
 *   reader at parse time to pick a text parse target, then spent.  With the
 *   structural families now named by the wire element, semantic no longer does
 *   structural work; its vocabulary shrinks to the pure interpretation set
 *   { bool, datetime, integer, real }.  See the wire notes below.
 *
 * The word "formalism" is deliberately broader than "algebra": it houses the
 * true algebras (point, geovec, complex, rotation) and also rules that are not
 * strictly algebra in this context, such as a point spread function sampling
 * response.
 * ========================================================================= */


/* Axis C: the stored structural family.  Mirrors the wire exactly: sfScalar
   is <scalar>, sfString and sfBlob are <bytes> split by the sentinel rule
   (utf8 vs raw/base64 encoding), sfComposite is <composite>. */
typedef enum das_set_family_e {
	sfUnknown = 0,
	sfScalar,
	sfString,
	sfBlob,
	sfComposite
} das_set_family;

/* The DERIVED presentation vocabulary: what one word tells a consumer this
   set hands out.  Computed by presType() from family + formalism, never
   stored (for composites the formalism fully determines it). */
typedef enum das_pres_type_e {
	prUnknown = 0,
	prScalar,     /* one value per point, no internal index.  <scalar> */
	prString,     /* a ubyte run, sentinel REQUIRED, hands out a bare char* */
	prBlob,       /* a ubyte run, no sentinel, always carried as ptr + len */
	prVector,     /* a numeric run, geometric vector formalism */
	prComplex,    /* a numeric run of two, complex formalism */
	prRotation,   /* a numeric run, 3;3 or a quaternion, rotation formalism */
	prMatrix,     /* a numeric run, r;c, a plain matrix, no rotation promise */
	prImage,      /* a numeric run on a two dimensional grid, plus planes */
	prGeneric     /* a numeric run whose formalism is known only as a string */
} das_pres_type;


/* ========================================================================= *
 * Axis D: the formalism.  One mechanism for scalars and composites alike.
 *
 * A das_form_kind is one row of the formalism table: a compiled-in registry,
 * one row per known formalism.  Adding point, geovec, complex, rotation,
 * matrix, image is adding rows, not classes.  The row is where the old
 * per-type knowledge in var_ary.c and value.c collapses.
 *
 * A das_formalism is the per-set INSTANCE: which row (if any), the wire token
 * as it arrived, and the row's context bindings.  It is embedded in DasSet by
 * value; the rows are static.  Contrast the generator: pGen is heap-owned,
 * refcounted, swappable.  The formalism is a bound classification, not an
 * owned machine.
 * ========================================================================= */

typedef struct das_set DasSet;

typedef struct das_form_kind {

	const char* sToken;         /* the wire type= value: "point", "geovec",
	                               "complex", ...  The linear row's token is
	                               "linear" though the wire idiom is absence;
	                               das_formalism_init maps absent to that row */

	das_elem_type elemWanted;   /* numeric for every current row */

	/* Read one item's internal run and stamp a datum template: a geovec, a
	   complex pair, a quaternion.  For a scalar row (point) the run is one
	   element.  A function pointer in a data row, not a subclass. */
	bool (*pack)(const DasSet* pThis, const ubyte* pRun, das_datum* pOut);

	/* Print the internal structure for expression(). */
	char* (*prnIntr)(const DasSet* pThis, char* sBuf, int nLen);

	/* True if a client that does not understand this formalism may still stack
	   the components as separate lines.  True for geovec, complex, rotation,
	   matrix, image.  The byte run branch never reaches the table. */
	bool bUserFacing;

} das_form_kind;


typedef struct das_formalism {

	/* The formalism table row, or NULL.  Linear IS a row: the unstated
	   formalism ordinary numbers obey, bound explicitly when no <formalism>
	   arrives, so every operation dispatches through one registry with no
	   bypass path for "plain" math.  NULL therefore means exactly one thing:
	   the token missed the table, the generic case.  The C library is then
	   its own dumb client: it knows the elements are numeric, carries the
	   layout it was told, exposes plain numbers, and REFUSES arithmetic
	   (rules it does not know are rules it must not guess). */
	const das_form_kind* pKind;

	/* The wire type= token exactly as it arrived.  Always set when a
	   <formalism> was present, even on a table hit, so a generic reader can
	   name the formalism it is skipping.  Empty means none arrived. */
	char sToken[32];

	/* The binding store, role to context token: a geovec's frame and surface,
	   a rotation's from and to.  It lives HERE because this is where the wire
	   puts it (attributes of the <formalism> sub-element), not on the set
	   body.  DasSet_getFrame() is a typed accessor over this store, so common
	   geovec code never sees the generality.  With the dim-level frame
	   cascade dropped (2026-07-26) there is no inherit step: the bindings on
	   the variable are the whole truth. */
	/* TODO struct das_ref_store refs; */

} das_formalism;


/* --- The binop registry: cross-formalism arithmetic ----------------------
 *
 * Binary operations between formalisms are PAIRWISE facts, so they do not
 * belong on the rows (a row owning a "mulRight that switches on partner"
 * smears N x N knowledge across every row).  They live in a second, sparse
 * table keyed by the ORDERED triple (op, kind left, kind right).  Ordered on
 * purpose: rotation * vector is defined, vector * rotation is not, and a
 * missing key FAILS LOUD rather than auto-commuting.
 *
 * A rule takes and returns full formalisms, bindings included, because the
 * math acts on bindings too: a rotation from=TSCS to=GEI applied to a vector
 * bound to TSCS yields a vector bound to GEI, and applied to anything else it
 * refuses.  Binding checks are the rule's job; that is where a frame-mismatch
 * fail-loud belongs.
 *
 * Population plan: v1 registers the linear ring (add/sub/mul over plain
 * numbers, the rules every scalar always had) plus the point affine rules,
 * so the whole dispatch path is exercised by math we already ship.  The
 * composite rules (geovec add/dot/cross, rotation compose/apply, complex,
 * matrix) come in their own session; each is one new row here, no existing
 * code touched.  Until then every composite pair misses, which IS the settled
 * v1 rule: gtBinop/gtUnop are scalar-only and fail loud on composites.
 *
 * A generic formalism (pKind NULL) can never match a rule slot, so unknown
 * math is refused by construction.  A "scalar scales anything" wildcard, when
 * composite rules land, will use a dedicated sentinel, never NULL.
 */

typedef enum das_form_op_e { dfoAdd, dfoSub, dfoMul } das_form_op;

typedef struct das_form_rule {
	das_form_op op;
	const das_form_kind* pLeft;
	const das_form_kind* pRight;

	/* Check bindings and units, name the result.  Returns false (loudly) when
	   the pairing is illegal: wrong frame, wrong units, wrong shapes. */
	bool (*resolve)(const das_formalism* pL, const das_formalism* pR,
	                das_units uL, das_units uR,
	                das_formalism* pOut, das_units* pUnitsOut);

	/* Item-wise evaluation: both operand runs in, result run out.  Item-wise
	   is enough even for shape-changing math (a dot product shrinks the
	   INTERNAL shape only), so DasGenOp's single-call eval survives.  The
	   element type rides along because a rule is registered per formalism
	   pair, not per storage width. */
	bool (*apply)(
		das_elem_type et, const ubyte* pLRun, const ubyte* pRRun, ubyte* pOutRun
	);
} das_form_rule;

/** Look up a formalism table row by its wire type= token.
 * @returns the row, or NULL: unknown tokens are the generic case, not an
 *          error.  @memberof DasSet */
DAS_API const das_form_kind* das_form_lookup(const char* sToken);

/** The explicit linear row, the formalism no <formalism> element implies.
 * @memberof DasSet */
DAS_API const das_form_kind* das_form_linear(void);

/** Look up a binop rule by the ORDERED (op, left kind, right kind) triple.
 * @returns the rule, or NULL: a miss means the pairing is undefined and the
 *          caller must refuse the operation, loudly.  A generic formalism's
 *          NULL kind matches nothing, so unknown math misses by
 *          construction.  @memberof DasSet */
DAS_API const das_form_rule* das_form_findRule(
	das_form_op op, const das_form_kind* pLeft, const das_form_kind* pRight
);

/** Bind a formalism instance from a wire token.  "" or NULL binds the
 * explicit linear row (the wire's unstated default made concrete); a table
 * miss keeps the token and leaves pKind NULL, the generic case.
 * @memberof DasSet */
DAS_API DasErrCode das_formalism_init(struct das_formalism* pThis, const char* sToken);

/* The inaugural registry content: the linear ring plus the point pilot.
 *
 *   { dfoAdd/dfoSub/dfoMul, linear, linear }  ->  linear (the common ops)
 *   { dfoSub, point,  point  }  ->  interval  (units via Units_interval)
 *   { dfoAdd, point,  linear }  ->  point     (interval units required)
 *   { dfoAdd, linear, point  }  ->  point     (registered, addition of an
 *                                              interval commutes; stated
 *                                              explicitly, never inferred)
 *   { dfoAdd, point,  point  }  ->  REFUSED   (a lookup miss, not a rule)
 *
 * Time lives here: a datetime is etLong TT2000 ticks with the point row.
 * point also fits any zero-referenced axis, a position measured from a
 * chosen origin. */


/* ========================================================================= *
 * DasSet, the base.  Supersedes DasVar.
 * ========================================================================= */

typedef struct das_set_vt {

	/* Read one value at a full external index into a datum.  For a composite
	   this packs the whole item, a vector or a complex, into one datum.  For a
	   generic composite on a table miss it returns the run as plain numbers. */
	bool (*get)(const DasSet* pThis, ptrdiff_t* pLoc, das_datum* pOut);

	das_elem_type (*elemType)(const DasSet* pThis);   /* asks the generator */
	das_pres_type (*presType)(const DasSet* pThis);

	int (*shape)(const DasSet* pThis, ptrdiff_t* pShape);      /* full shape */
	int (*intrShape)(const DasSet* pThis, ptrdiff_t* pShape);  /* internal only */

	char* (*expression)(const DasSet* pThis, char* sBuf, int nLen, unsigned int uFlags);

	bool (*isNumeric)(const DasSet* pThis);

	int     (*incRef)(DasSet* pThis);
	int     (*decRef)(DasSet* pThis);
	DasSet* (*copy)(const DasSet* pThis);

} das_set_vt;


struct das_set {

	DasDesc base;          /* a set IS a descriptor: it has properties, a
	                          parent, and it serializes */

	const das_set_vt* vt;

	DasGen* pGen;          /* Axis A.  The set owns a generator but is not one.
	                          Swapping array for sequence does not change the
	                          set's type. */

	das_set_family fam;    /* Axis C.  Axis B is read from pGen, not stored;
	                          the rich das_pres_type is derived, not stored. */

	das_formalism form;    /* Axis D.  Embedded by value; empty for <bytes>
	                          and for a plain linear number. */

	/* One unit.  The wire allows a semicolon list of exactly intern-many
	   (degrees;degrees;km for a geodetic position) but das_geovec holds one
	   units set and the sizeof(das_time) >= sizeof(das_geovec) rule leaves no
	   room for more, so v1 FAILS LOUD on a ';' list the holder cannot carry
	   (Dude's ruling 2026-07-27; a scope cut, stated, not silent).  On an
	   angular-system geovec the single unit is the altitude/radial unit and
	   the angles are always degrees. */
	das_units units;

	int   nRef;
	void* pUser;
};

#define DasSet_family(P)   ((P)->fam)
#define DasSet_presType(P) ((P)->vt->presType(P))
#define DasSet_units(P)    ((P)->units)
#define DasSet_gen(P)      ((P)->pGen)

DAS_API int DasSet_incRef(DasSet* pThis);

/** Drop a reference; the set destroys itself (and drops its generator
 * reference) at zero.  @returns the remaining count.  @memberof DasSet */
DAS_API int DasSet_decRef(DasSet* pThis);

/** Read one value at a full external index into a datum.  @memberof DasSet */
DAS_API bool DasSet_get(const DasSet* pThis, ptrdiff_t* pLoc, das_datum* pOut);

/** External shape, from the generator.  @memberof DasSet */
DAS_API int DasSet_shape(const DasSet* pThis, ptrdiff_t* pShape);

/** Create a scalar set: one value per point, no internal index.
 *
 * @param pGen the value source; this call takes a reference
 * @param units the set's units.  A ';' units list FAILS: nothing in the
 *        library can hold per-component units yet, and silently keeping the
 *        first entry would misstate the data.
 * @param sFormToken the wire formalism type=, "" or NULL for plain linear,
 *        "point" for the affine rule.  Unknown tokens are kept generically.
 * @returns a new set, or NULL on a loud error.  @memberof DasSet */
DAS_API DasSet* new_DasSetScalar(
	DasGen* pGen, das_units units, const char* sFormToken
);


/* ========================================================================= *
 * The scalar case.  One value per point, no internal index.  Supersedes the
 * scalar case of DasVarAry.  There is NO DasScalarSet struct: with the
 * formalism hoisted into the base, a scalar is just a DasSet whose pres is
 * prScalar, and its only formalisms so far are none (linear) or point.
 *
 * Wire, a plain linear scalar.  Linear is the default, so nothing is stated:
 *
 *   <scalar semantic="real" units="Hz" index="*">
 *     <packet numItems="1" itemBytes="8" encoding="LEreal"/>
 *   </scalar>
 *
 * Wire, a time scalar.  This is where the word "point" appears.  The affine
 * rule is a <formalism> whose type alone suffices, so it has no sub-element.
 * semantic="datetime" still rides along as the display and calendar-units
 * directive.  Child order is properties, then formalism, then generator:
 *
 *   <scalar semantic="datetime" units="TT2000" index="*">
 *     <formalism type="point"/>
 *     <packet numItems="1" itemBytes="8" encoding="LEint"/>
 *   </scalar>
 *
 * A future binding-bearing scalar formalism (a scalar with a point spread
 * function) is no longer an open item: it is a formalism with bindings on a
 * scalar, exactly like geovec on a composite.  Same struct, same store.
 * ========================================================================= */


/* ========================================================================= *
 * DasCompSet.  A value with an internal index.  Supersedes DasVarVecAry and the
 * roughly thirty scattered das_val_type switch sites the scout found.
 *
 * The internal layout is pure SHAPE.  The formalism does not live in the shape.
 * A 3;3 rotation and a 3;3 plain matrix share the same layout and differ only
 * in their formalism.  TRACERS ships non-rotation matrices, so this matters.
 *
 * Dispatch inside a composite looks at Axis B first.
 *
 *   element is etUByte    ->  the byte run branch: string or blob.  Empty
 *                             formalism.  No table lookup.  <bytes> on the wire.
 *
 *   element is numeric    ->  base.form carries the row.  A hit gives a rich
 *                             datum.  A miss is the generic case: carry the
 *                             token, hand back plain numbers.
 * ========================================================================= */

typedef struct das_comp_set {

	DasSet base;           /* base.form: the formalism row, token, bindings */

	/* Internal layout exactly as declared.  "3" is a vector.  "3;3" is a
	   matrix.  "4;*" is a ragged multi level composite.  Ragged levels are
	   negative.  This is the axis that first exercises internal rank greater
	   than one, which today's var_ary.c refuses. */
	int       nIntRank;
	ptrdiff_t aIntShape[DASIDX_MAX];

	/* Per component labels live at this structural level, readable without
	   understanding the formalism (the skippability contract: a dumb client
	   still stacks N labeled lines).  Per component units WOULD live here
	   too; until a holder exists the ';' list fails loud, see base.units. */
	/* TODO structural label storage */

} DasCompSet;


/* --- The numeric composite wire forms ---
 *
 * A composite carries semantic in its interpretation role, same as a scalar:
 * integer or real.  It is the parse target for utf8 components and redundant
 * with the encoding for binary.  The formalism is always a <formalism type="...">
 * whose type names the math.  The wire attribute is type= to match <stream type=>
 * and <p type=>; the C lookup key is das_form_kind.sToken (a separate audience).
 * The sub-element is OPTIONAL, present only when the formalism needs bindings or
 * params a classification string cannot carry.  The type token and the
 * sub-element name match, and the type doubles as the label a client shows when
 * it cannot parse the sub-element.
 *
 * Complex needs no context, so type alone suffices and no sub-element is spent.
 * This is the original das3_from_cdf request, landed with no new semantic and no
 * codec change:
 *
 *   <composite semantic="int" intern="2" index="*">
 *     <formalism type="complex"/>
 *     <packet numItems="2" itemBytes="1" encoding="byte"/>
 *   </composite>
 *
 * A geometric vector needs a frame, so it carries a sub-element for the frame,
 * body, and component order, plus a refs summary a dumb client reads at the skip
 * boundary.  Child order reads purpose to bytes: properties, formalism, generator:
 *
 *   <composite semantic="real" units="m" intern="3" index="*">
 *     <properties><p name="label" type="stringArray">B_x;B_y;B_z</p></properties>
 *     <formalism type="geovec" refs="TS2_TSCS">
 *       <geovec frame="TS2_TSCS" body="-64678" sysorder="0;1;2"/>
 *     </formalism>
 *     <packet numItems="3" itemBytes="4" encoding="LEreal"/>
 *   </composite>
 *
 * A rotation carries two bindings.  Layout is 3;3, pure shape; the formalism,
 * not the shape, says "rotation":
 *
 *   <composite semantic="real" intern="3;3" index="*">
 *     <formalism type="rotation" refs="TSCS;GEI2000">
 *       <rotation from="TSCS" to="GEI2000"/>
 *     </formalism>
 *     <packet numItems="9" itemBytes="8" encoding="LEreal"/>
 *   </composite>
 *
 * The name <formalism> rather than <algebra> is on purpose: a point spread
 * function on an image, or a sampling response, is a rule a capable client
 * needs but is not an algebra.  Same container, same skip boundary.
 */


/* ========================================================================= *
 * The byte run branch: string and blob.  Element is etUByte.
 *
 * These are DasCompSet in C, because a byte run IS an internal index, but they
 * are ONE wire element, <bytes>, because their bytes are not user facing lines.
 * The structural family comes from the element name, so no semantic is needed
 * for structure.  A byte run's formalism is EMPTY (a byte run has no
 * auto-math), matching <bytes> carrying no <formalism> on the wire.  string
 * and blob are told apart by the packet encoding, an explicit required
 * triplet:
 *
 *   encoding="utf8"    text
 *   encoding="base64"  ASCII-armored binary, decode to get the bytes
 *   encoding="raw"     no transform, opaque bytes
 *
 * raw is a NAMED token, not an omitted attribute, so a forgotten encoding is a
 * loud error rather than a silent default.
 *
 * Wire:
 *
 *   <bytes index="*">
 *     <packet numItems="1" itemBytes="*" encoding="utf8" valTerm=";"/>
 *   </bytes>
 *
 *   <bytes index="*">
 *     <packet numItems="1" itemBytes="*" encoding="raw" embedded="image/png"/>
 *   </bytes>
 *
 * The primary encoding gets the bytes off the wire.  An embedded= attribute,
 * if present, is the extension-codec trigger: it names the format the bytes
 * must further be decoded FROM to realize the declared values, and a reader
 * with no handler fails loud.  A purely descriptive content label rides in a
 * property (contentType) instead.  Two independent stages on two attributes.
 *
 * The only difference between string and blob is the sentinel.  A string ends
 * in a REQUIRED trailing null in the array's last index, which is why it can
 * hand out a bare char*.  A blob is stored AS GIVEN with no sentinel, which is
 * why it must always be carried as pointer plus length.  Fixed versus ragged is
 * orthogonal; both can be either.
 *
 * OPEN: whether the byte run branch is just DasCompSet with a etUByte element
 * and a small sentinel flag, or earns a thin DasStrSet subclass.  The scout
 * showed string and blob take a distinct codec path, ITEMLEN and WRAP, while
 * numeric composites ride the plain path.  Decide against real code.
 * ========================================================================= */

/* typedef struct das_str_set { DasCompSet base; ... } DasStrSet;  back pocket */


/* ========================================================================= *
 * Far corners, one line each.  Sketched shallow on purpose.
 * ========================================================================= *
 *
 * prMatrix   a numeric composite, r;c layout, formalism token "matrix", no
 *            rotation promise.  A formalism table row.
 *
 * prImage    a numeric composite on a two dimensional grid plus planes.  A
 *            point spread function attaches as a <given> context ref, per the
 *            note now in context.h, carried through <formalism>.  v3.1
 *            territory.  Note that a MEASURED image is often just a <scalar>
 *            field over a 2-D external grid, block-encoded (png, jpeg) over that
 *            grid; prImage is for display images with planes and colorspace,
 *            not measurement grids.
 *
 * prGeneric  already handled above as the table miss.  It is the base
 *            DasCompSet behavior, not a separate type.
 */

#ifdef __cplusplus
}
#endif

#endif /* _das_set_h_ */
