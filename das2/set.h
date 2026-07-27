/* set.h -- DESIGN SKETCH, not built, no companion .c, no tests.
 *
 * This is the successor to variable.h.  It grows here beside the current code.
 * When it can do everything variable.h and friends do, they are removed.  No
 * rip and replace.  Join and succeed.
 *
 * The whole file lives inside #ifdef DAS_FUTURE so it evaporates to nothing if
 * a real source accidentally includes it.  It is a development plan written as
 * C, so the wire form and the code form stay collected in one place.  Read the
 * comments.  XML snippets show the stream form of each type right next to the
 * struct that carries it.
 *
 * Provenance: Claude + Dude design thread, 2026-07-26.  Fallible scaffolding.
 * Nothing here is signed off.  Companion reasoning lives in
 * co_notes/libdas_wire_model_pilot.md and co_notes/libdas_type_extension_map.md.
 */

#ifdef DAS_FUTURE

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
   counts take Java-style CamelCase (DasSet, DasGen, DasScalarSet).  Stack,
   embedded, and static things take snake_case (das_elem_type, das_comp_kind,
   das_set_vt), matching das_time and das_geovec.  A composite content table row
   and a vtable are static, not heap, so they are snake_case. */

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
 *   Axis C, the presentation type (pr).  What the set hands a consumer: a
 *           scalar, a byte run, a vector, a complex.  This is the set's own
 *           identity, and it is what the wire element name carries.
 *
 *   Semantic is NOT a fifth carried axis.  It is a DIRECTIVE, consumed by the
 *   reader at parse time to pick a text parse target, then spent.  With the
 *   structural families now named by the wire element, semantic no longer does
 *   structural work; its vocabulary shrinks to the pure interpretation set
 *   { bool, datetime, integer, real }.  See the wire notes below.
 *
 * The MATH of a set, what auto-operations it supports, is a cross cutting fact
 * carried on the wire by <formalism>.  The word is deliberately broader than
 * "algebra": it houses the true algebras (vector, complex, rotation) and also
 * rules that are not strictly algebra in this context, such as a point spread
 * function sampling response.  See DasCompSet.
 * ========================================================================= */


/* Axis C: presentation type (pr).  Its own axis, its own numbering. */
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


/* A scalar's algebra: the composition rules its single value obeys.  This is
 * NOT an element type.  The bits are a plain etLong or etDouble; only the legal
 * operations differ.  saPoint is the affine rule that time obeys, and it also
 * fits any zero-referenced axis, a position measured from a chosen origin.
 *
 * On the wire this is carried the same way the composites carry theirs, a
 * <formalism type="point"/> whose type alone suffices.  linear and point ARE
 * algebras, so this enum keeps the "alg" name; formalism is just the wider wire
 * word that also covers the non-algebra cases a composite can carry. */
typedef enum das_scalar_alg_e {
	saLinear = 0,   /* add, subtract, scale freely.  The default. */
	saPoint         /* affine: point - point = interval, point + interval =
	                   point, point + point refused.  Time lives here. */
} das_scalar_alg;


/* ========================================================================= *
 * DasSet, the base.  Supersedes DasVar.
 * ========================================================================= */

typedef struct das_set DasSet;

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

	/* Frame and other context access stays a TYPED accessor over a generic
	   role to token store.  A geovec caller keeps DasSet_getFrame().  The
	   generic store underneath is what lets new formalisms carry bindings
	   without a new accessor each time.  See the ref store on DasCompSet. */

} das_set_vt;


struct das_set {

	DasDesc base;          /* a set IS a descriptor: it has properties, a
	                          parent, and it serializes */

	const das_set_vt* vt;

	DasGen* pGen;          /* Axis A.  The set owns a generator but is not one.
	                          Swapping array for sequence does not change the
	                          set's type. */

	das_pres_type pres;    /* Axis C.  Axis B is read from pGen, not stored. */

	das_units units;       /* one unit, or the first of a per component list */

	int   nRef;
	void* pUser;
};


/* ========================================================================= *
 * DasScalarSet.  One value per point, no internal index.  Supersedes the
 * scalar case of DasVarAry.
 *
 * The scalar's algebra is a small field here (saLinear or saPoint), NOT the
 * element type.  A datetime is a DasScalarSet whose generator produces etLong
 * (TT2000 ticks) with alg == saPoint.
 *
 * Wire, a plain linear scalar.  saLinear is the default, so nothing is stated:
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
 * ========================================================================= */

typedef struct das_scalar_set {
	DasSet         base;
	das_scalar_alg alg;    /* saLinear (default) or saPoint */
} DasScalarSet;


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
 *   element is etUByte    ->  the byte run branch: string or blob.  No
 *                             formalism.  No table lookup.  <bytes> on the wire.
 *
 *   element is numeric    ->  consult the composite content table by the
 *                             formalism token.  A hit gives a rich datum.  A
 *                             miss is the generic case: carry the formalism as
 *                             a string, hand back plain numbers.
 * ========================================================================= */

typedef struct das_comp_set DasCompSet;

/* One row of the composite content table.  This is NOT a DasDesc.  It is a
   compiled-in registry, one row per known numeric formalism.  Adding vector,
   complex, rotation, matrix, image is adding rows, not classes.  The row is
   where the old per-type knowledge in var_ary.c and value.c collapses. */
typedef struct das_comp_kind {

	const char* sToken;         /* the wire type= value: "geovec", "complex", ... */

	das_elem_type elemWanted;   /* numeric for every current row */

	/* Read the internal run and stamp a datum template: a geovec, a complex
	   pair, a quaternion.  A function pointer in a data row, not a subclass. */
	bool (*pack)(const DasCompSet* pThis, const ubyte* pRun, das_datum* pOut);

	/* Print the internal structure for expression(). */
	char* (*prnIntr)(const DasCompSet* pThis, char* sBuf, int nLen);

	/* True if a client that does not understand this formalism may still stack
	   the components as separate lines.  True for vector, complex, rotation,
	   matrix, image.  The byte run branch never reaches the table. */
	bool bUserFacing;

} das_comp_kind;


struct das_comp_set {

	DasSet base;

	/* Internal layout exactly as declared.  "3" is a vector.  "3;3" is a
	   matrix.  "4;*" is a ragged multi level composite.  Ragged levels are
	   negative.  This is the axis that first exercises internal rank greater
	   than one, which today's var_ary.c refuses. */
	int       nIntRank;
	ptrdiff_t aIntShape[DASIDX_MAX];

	/* The composite content table row that interprets the run, or NULL.  NULL
	   means a table miss: the generic case.  The C library is then its own
	   dumb client.  It knows the elements are numeric, it carries the layout it
	   was told, and it exposes each element as a plain scalar. */
	const das_comp_kind* pKind;

	/* The formalism token exactly as it arrived on the wire.  Always set, even
	   when pKind is found, so a generic reader can name the formalism it is
	   skipping.  On a miss this is the ONLY thing known about the formalism. */
	char sFormalism[32];

	/* The generic role to token ref store.  A vector's frame lives here as
	   role "frame".  A rotation's endpoints live here as roles "from" and
	   "to".  DasSet_getFrame() is a typed accessor over this store, so common
	   geovec code never sees the generality.  refs cascade per role from the
	   parent dimension; a child overrides one role without wiping the others. */
	/* TODO struct das_ref_store refs; */

	/* Per component labels and per component units live at this structural
	   level, readable without understanding the formalism.  Units may be one
	   value or a semicolon list, e.g. "degrees;degrees;km" for a geodetic
	   position. */
	/* TODO structural label and per-component unit storage */
};


/* --- The numeric composite wire forms ---
 *
 * A composite carries semantic in its interpretation role, same as a scalar:
 * integer or real.  It is the parse target for utf8 components and redundant
 * with the encoding for binary.  The formalism is always a <formalism type="...">
 * whose type names the math.  The wire attribute is type= to match <stream type=>
 * and <p type=>; the C lookup key is das_comp_kind.sToken (a separate audience).
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
 * for structure.  string and blob are told apart by the packet encoding, an
 * explicit required triplet:
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
 *     <packet numItems="1" itemBytes="*" encoding="raw" mime="image/png"/>
 *   </bytes>
 *
 * The primary encoding gets the bytes off the wire; a secondary mime codec, if
 * present, says what those bytes decode into (a PNG, a gzip block).  Two
 * independent stages on two attributes.
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
 *            rotation promise.  A composite content table row.
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

#endif /* DAS_FUTURE */
