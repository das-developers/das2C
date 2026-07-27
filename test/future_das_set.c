/* Holding pen for tests of the DasSet / DasGen redesign.
 *
 * This file is deliberately NOT in TEST_PROGS in the buildfiles .mak set, so the build
 * never compiles or runs it.  It needs no DAS_FUTURE guard for that reason: it
 * just sits here and accretes test ideas, as comments now and as real cases
 * later, until the redesign in das2/set.h and das2/generator.h is ready to
 * grow up beside variable.h.  When a case here becomes real, move its program
 * name into TEST_PROGS and it joins the suite.
 *
 * Keep it C99 clean.  If it ever does enter TEST_PROGS it builds under
 * -std=c99, and _Static_assert is C11, so use the negative-size-array trick or
 * a runtime check instead.
 *
 * Design record: co_notes/libdas_wire_model_pilot.md,
 * co_notes/libdas_type_extension_map.md, co_notes/libdas_set_sketch_notes.md.
 * Nothing here is signed off.
 */

#include <stdio.h>

/* The tests this file will eventually hold.  One line each for now.
 *
 * 1. Element vs value enum drift.  et is a restricted subset of vt with pinned
 *    values, etDouble == vtDouble and so on.  Assert they have not drifted.
 *    Runtime check here, or a negative-size-array macro if we want it at build
 *    time under C99.
 *
 * 2. Generator eval.  A DasGen produces the internal run at one external index.
 *    Cover array, sequence, and constant.  The OPEN case is a binary op over
 *    two COMPOSITE operands: prove it fits one eval, or split scalar and run
 *    paths.  var_bin.c refuses vectors today.
 *
 * 3. Presentation round trip.  scalar, string, blob, vector, complex, rotation,
 *    matrix, generic.  A set built from a known algebra token reads back the
 *    right datum template.  A table miss reads back as plain numbers.
 *
 * 4. Composite content table.  Token to row lookup.  A miss lands in the
 *    generic path: algebra carried as a string, intern layout carried as told,
 *    including a ragged or multi level shape like 4 by star.
 *
 * 5. Byte run branch.  A string ends in a required null and hands out a bare
 *    char pointer.  A blob has no sentinel and is always carried as pointer
 *    plus length.  Fixed versus ragged is orthogonal to both.
 *
 * 6. Structural metadata.  Per component labels and a per component units list
 *    such as degrees;degrees;km survive a round trip and are readable without
 *    understanding the algebra.
 *
 * 7. Point algebra.  A point minus a point is an interval.  A point plus an
 *    interval is a point.  A point plus a point is refused.
 *
 * 8. Wire counts.  numItems is the count of user facing values, intern is the
 *    internal layout, itemBytes is the width of one value.  Check that the
 *    verify pass rejects a stream where these disagree.
 */

int main(void)
{
	printf("future_das_set: placeholder for DasSet and DasGen tests.\n");
	printf("Not built by the suite yet.  See the comment block for the\n");
	printf("cases this file will grow into, and the co_notes design record.\n");
	return 0;
}
