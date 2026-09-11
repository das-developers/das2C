#!/usr/bin/env bash

# das3_csv regression tests: gold comparisons over small fixtures, and a legality
# check over fixtures too large to keep as gold.
#
# ex23: a stream carrying a SIGNED 1-byte column (encoding "byte").  A deliberate
# regression watch: das2C once coerced every 1-byte scalar variable to vtUByte
# (unsigned), which made das3_csv fail on a signed column.  The fixture's first
# records carry negative values (-1, -128, -42, ...) precisely because a positive
# byte (0..127) renders the SAME whether read signed or unsigned -- only a
# high-bit-set byte proves signedness.  If the unsigned coercion ever returns the
# rendered values diverge from the gold (-1 -> 255, -128 -> 128, ...).
#
# ex30: rank 2, RAGGED, a reference time plus an offset sequence with a borrowed
# extent.  Pins the row model (one row per index location, the record's time
# repeated down its run) and the computed center column: reference + offset is
# written as one time column, its two parts are not.
#
# ex31: ex30's twin with a 3-VECTOR field and a reference-only time.  Pins one
# column per component, in storage order, under matching header cells.
#
# ex22: rank 3, a 2x2 sensor grid per record with a coordinate that varies along
# the grid indices only.  Pins fixed sub-extents flattening to rows, and a
# non-record-varying coordinate becoming ordinary columns.
#
# ex27 and ex42 are checked for LEGALITY rather than against gold: an embedded
# image renders as a base64 cell of tens of kilobytes, and a rank 3 tensor as 27
# columns.  Every row of the output must have the same number of fields, which is
# the property a spreadsheet or pandas needs and the one a misplaced composite
# breaks.  ex27 carries a geoloc, a byte run and a vector; ex42 a plain matrix
# with no <ops> and a composite under a kind das2C does not know.
#
# Drafted by Claude Opus 4.8 and Fable 5.1, filed by Chris Piker.

BD=$1
CSV=$BD/das3_csv
EX=examples

# Pick whatever checksum tool exists (md5sum on Linux, md5 on BSD/macOS).
if   command -v md5sum >/dev/null 2>&1; then MD5SUM="md5sum"
elif command -v md5    >/dev/null 2>&1; then MD5SUM="md5"
else echo " Result: FAILED (no md5sum/md5 found)"; exit 5; fi

GOLDS="ex23_tracers_mag_hsk ex30_cassini_ragged_notlast ex31_efi_ragged_vec ex22_mag_grid_vec"

for f in $GOLDS; do
	echo "Testing: das3_csv gold, $f"
	echo "   exec: $CSV < $EX/$f.d3b > $BD/$f.csv"
	$CSV < $EX/$f.d3b > $BD/$f.csv 2>/dev/null
	if [ "$?" != "0" ]; then echo " Result: FAILED (das3_csv errored on $f.d3b)"; exit 4; fi

	s1=$(cat $EX/$f.csv | ${MD5SUM})
	s2=$(cat $BD/$f.csv | ${MD5SUM})
	echo "   gold $EX/$f.csv --> $s1"
	echo "   new  $BD/$f.csv --> $s2"
	if [ "$s1" != "$s2" ] ; then echo " Result: FAILED (rendered rows != gold)"; exit 4; fi

	echo " Result: PASSED"
	echo
done

LEGAL="ex27_epop_fai_mgf_blob ex42_plain_tensor"

for f in $LEGAL; do
	echo "Testing: das3_csv legality, $f (same field count on every row)"
	echo "   exec: $CSV < $EX/$f.d3b > $BD/$f.csv"
	$CSV < $EX/$f.d3b > $BD/$f.csv 2>/dev/null
	if [ "$?" != "0" ]; then echo " Result: FAILED (das3_csv errored on $f.d3b)"; exit 4; fi

	# The field delimiter is ';' and no cell may carry one: labels are quoted
	# names, numbers and base64 have no ';' in their alphabets.
	nCounts=$(awk -F';' '{print NF}' $BD/$f.csv | sort -u | wc -l)
	nRows=$(wc -l < $BD/$f.csv)
	echo "   rows $nRows, distinct field counts $nCounts"
	if [ "$nCounts" != "1" ]; then echo " Result: FAILED (rows disagree on field count)"; exit 4; fi
	if [ "$nRows" -lt 4 ]; then echo " Result: FAILED (header rows only, no values)"; exit 4; fi

	echo " Result: PASSED"
	echo
done

exit 0
