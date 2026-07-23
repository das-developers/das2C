#!/usr/bin/env bash

# Round-trip the das3 ISEE rapid-sample fixtures through das3_text.  These are the
# FIRST regression coverage of the das3 encode path (DasDs/DasVar/DasCodec write
# side): das2_ascii exercises only the legacy das2 plane model, and TestV3Read
# only checks the read side.  das3_text is the one filter that drives a stream all
# the way through the das3 model and back out as text.
#
# Two legs per fixture:
#   (a) binary -> text MUST match the committed gold (pins binary->text decode +
#       text encode; this is the leg that catches a sequence storage/interval bug).
#   (b) text gold -> text MUST reproduce itself (idempotent; pins the text-input
#       read path and proves the gold is a fixed point of the codec).
#
# Reals are emitted at 4 sig-digits and times at 3 sub-second digits (-r 4 -s 3):
# enough to eyeball correctness in a diff, small enough to keep golds < 1 MB.
#
# Drafted by Claude Opus 4.8, filed by Chris Piker.

BD=$1
TEXT=$BD/das3_text
OPTS="-s 3 -r 4"

# Pick an available checksum tool (md5sum on Linux, md5 on BSD/macOS).  This was
# unset, so every ${MD5SUM} expanded to nothing -- s1 and s2 were both empty and
# the comparisons silently passed no matter what the output was.  We only compare
# our own outputs to each other, so either tool's format is fine if consistent.
if   command -v md5sum >/dev/null 2>&1; then MD5SUM="md5sum"
elif command -v md5    >/dev/null 2>&1; then MD5SUM="md5"
else echo " Result: FAILED (no md5sum/md5 found)"; exit 5; fi

# ex22: a 2-component VECTOR sequence (geo_loc grid, one <sequence> per component,
# minval=1 interval="1;0"/"0;1") -- pins the vector-sequence parse/transpose/encode.
# ex24-26: rank-1 scatter, rank-2 fixed-frequency (reference+offset sequence), and
# rank-3 full-sweep blocks (multi-index sequence, offset = 16*j + 0.125*k) -- the same
# ISEE rapid-sample data packed three ways.
# ex27: a raw-blob passthrough (encoding="blob", no mime) -- pins {N} length-prefixed
# byte-run decode and its base64 text round-trip.
# ex33/ex34: a variable item-COUNT run of FIXED-width values (Case 4), reals and strings,
# NOT last in the packet -- pins idxTerm run-terminator emission AND the terminator-bounded
# read (leg b is the first regression cover of the ragged-text decode path).
# ex35: a variable-COUNT variable-WIDTH utf8 string run (Case 2) -- pins the trim of
# cosmetic pad on decode (internal spaces kept), the rank-3 string encode iterator, and
# the canonical terminator normalization (every value gets its valTerm before idxTerm).
# ex30: a variable-count run of scalar reals, not-last -- the simplest ragged case,
# pinning the [j|N] tag read re-framed as a single idxTerm ('!') text run.
# ex31: a variable-count run of 3-VECTORS, not-last -- pins ragged vector text output
# (atoms flattened to a terminator-bounded run) and, in leg b, the ragged-vector text
# decode with component auto-roll under the run markEnd.
# ex32: rank-3 MULTI-LEVEL ragged (index="*;*;*", ragged var NOT last) -- leg a pins the
# [j|N][k|N] binary tag read re-framed as two-level idxTerm text ("!,$"), leg b pins the
# terminator-hierarchy decode (DasCodec_decodeRuns) and the full spelled closing stack.
# ex36: rank-3 MULTI-LEVEL ragged VAR-WIDTH strings, ragged var LAST (Case 2 at depth).
# The .d3b mixes legal input forms -- fully spelled, collapsed, frame-leaning, empty
# value -- so leg a pins the liberal reads normalizing to one canonical output, and
# leg b pins '\n' as a declared outer terminator (not incidental whitespace).
# ex37: a fixed-width utf8 parsed field WIDER than the 127-byte small-vector
# assumption (itemBytes="132") -- pins the overflow-buffer paths in
# _fixed_text_convert (read) and _DasCodec_printItems (write).
# ex38: a TAGGED variable-count binary run in LAST position (non-text runs carry
# their [j|N] tag in every position) -- pins the tag read at the packet edge and
# its re-frame as a '\n'-terminated text run.
# ex39: the SANDWICH (index="*;3;*", a fixed extent between two ragged indices).
# Binary runs tag the ragged sample index only ([k|N]; the fixed sensor extent has
# no wire framing, its count is declared once); the utf8 string var declares the
# ragged-only idxTerm form.  Leg a pins both reads plus the DECORATED re-frame
# (das3_text declares a terminator for every span index, so the fixed extent gets
# a readable boundary and the record its '\n'); leg b pins the decorated decode
# with the count check on the fixed extent.
FIXTURES="ex22_mag_grid_vec ex24_isee_rapid_rank1 ex25_isee_rapid_rank2 ex26_isee_rapid_rank3 ex27_epop_fai_mgf_blob ex30_cassini_ragged_notlast ex31_efi_ragged_vec ex32_marsis_2d_ragged ex33_cassini_ragged_utf8 ex34_ragged_fixstr ex35_strings_rank2 ex36_events_rank3 ex37_wide_fixed_utf8 ex38_wbr_wfrm_tags ex39_sandwich"

for f in $FIXTURES; do
	echo "Testing: das3_text round-trip, $f (phys-dim != array-dim)"

	# (a) binary -> text vs gold
	echo "   exec: $TEXT $OPTS < test/$f.d3b > $BD/$f.d3t"
	$TEXT $OPTS < test/$f.d3b > $BD/$f.d3t
	if [ "$?" != "0" ]; then echo " Result: FAILED (das3_text errored on $f.d3b)"; exit 4; fi

	s1=$(cat test/$f.d3t | ${MD5SUM})
	s2=$(cat $BD/$f.d3t  | ${MD5SUM})
	echo "   gold test/$f.d3t --> $s1"
	echo "   new  $BD/$f.d3t  --> $s2"
	if [ "$s1" != "$s2" ] ; then echo " Result: FAILED (binary->text != gold)"; exit 4; fi

	# (b) text gold -> text must be idempotent
	echo "   exec: $TEXT $OPTS < test/$f.d3t > $BD/${f}_idem.d3t"
	$TEXT $OPTS < test/$f.d3t > $BD/${f}_idem.d3t
	if [ "$?" != "0" ]; then echo " Result: FAILED (das3_text errored on text input)"; exit 4; fi

	s3=$(cat $BD/${f}_idem.d3t | ${MD5SUM})
	if [ "$s1" != "$s3" ] ; then echo " Result: FAILED (text->text not idempotent)"; exit 4; fi

	echo " Result: PASSED"
	echo
done

# ex28: an undecodable embedded image (encoding="blob" mime="image/png").  It needs
# its own block because it drives the -f (embed-as-bytes) path, not the plain loop.
# Without -f das3_text MUST fail loud (no codec registered).  With -f it flattens the
# image to an opaque per-record blob (recording embedded* provenance), and the offset
# VECTOR SEQUENCE keeps pinning 256;280 so the dataset stays a coherent rank-3 -- this
# leg is the regression guard for DasVarSeq honoring its declared extent.
f=ex28_epop_fai_mgf_img
echo "Testing: das3_text -f flatten, $f (undecodable embedded blob)"

# (0) default (no -f) MUST refuse -- fail loud on what it can't decode faithfully
$TEXT $OPTS < test/$f.d3b > /dev/null 2>&1
if [ "$?" == "0" ]; then echo " Result: FAILED ($f read without -f should fail loud)"; exit 4; fi

# (a) -f flatten: binary -> text vs gold
echo "   exec: $TEXT $OPTS -f < test/$f.d3b > $BD/$f.d3t"
$TEXT $OPTS -f < test/$f.d3b > $BD/$f.d3t 2>/dev/null
if [ "$?" != "0" ]; then echo " Result: FAILED (das3_text -f errored on $f.d3b)"; exit 4; fi
s1=$(cat test/$f.d3t | ${MD5SUM})
s2=$(cat $BD/$f.d3t  | ${MD5SUM})
echo "   gold test/$f.d3t --> $s1"
echo "   new  $BD/$f.d3t  --> $s2"
if [ "$s1" != "$s2" ] ; then echo " Result: FAILED (-f binary->text != gold)"; exit 4; fi

# (b) the flattened gold is a bare blob (no mime), so it re-reads WITHOUT -f and must
#     reproduce itself (proves the recovered stream is a stable fixed point)
echo "   exec: $TEXT $OPTS < test/$f.d3t > $BD/${f}_idem.d3t"
$TEXT $OPTS < test/$f.d3t > $BD/${f}_idem.d3t 2>/dev/null
if [ "$?" != "0" ]; then echo " Result: FAILED (das3_text errored on flattened text input)"; exit 4; fi
s3=$(cat $BD/${f}_idem.d3t | ${MD5SUM})
if [ "$s1" != "$s3" ] ; then echo " Result: FAILED (flattened text->text not idempotent)"; exit 4; fi

echo " Result: PASSED"
echo

# ex29: the extension-contract demo (an image/jpeg blob the author declares decodes to
# a single integer).  Same shape as the ex28 block, but the input is already text and
# the flattened gold has its own name.  Without -f it MUST fail loud (no jpeg codec);
# with -f it flattens to a bare base64 blob + embedded* provenance, which then re-reads
# WITHOUT -f as a stable fixed point.
f=ex29_ext_contract
echo "Testing: das3_text -f flatten, $f (undecodable extension contract)"

$TEXT $OPTS < test/$f.d3t > /dev/null 2>&1
if [ "$?" == "0" ]; then echo " Result: FAILED ($f read without -f should fail loud)"; exit 4; fi

echo "   exec: $TEXT $OPTS -f < test/$f.d3t > $BD/${f}_flat.d3t"
$TEXT $OPTS -f < test/$f.d3t > $BD/${f}_flat.d3t 2>/dev/null
if [ "$?" != "0" ]; then echo " Result: FAILED (das3_text -f errored on $f.d3t)"; exit 4; fi
s1=$(cat test/${f}_flat.d3t | ${MD5SUM})
s2=$(cat $BD/${f}_flat.d3t  | ${MD5SUM})
echo "   gold test/${f}_flat.d3t --> $s1"
echo "   new  $BD/${f}_flat.d3t  --> $s2"
if [ "$s1" != "$s2" ] ; then echo " Result: FAILED (-f text->text != gold)"; exit 4; fi

$TEXT $OPTS < test/${f}_flat.d3t > $BD/${f}_flat_idem.d3t 2>/dev/null
if [ "$?" != "0" ]; then echo " Result: FAILED (das3_text errored on flattened text input)"; exit 4; fi
s3=$(cat $BD/${f}_flat_idem.d3t | ${MD5SUM})
if [ "$s1" != "$s3" ] ; then echo " Result: FAILED (flattened text->text not idempotent)"; exit 4; fi

echo " Result: PASSED"
echo

# reject_rank3_noterm: utf8 with 2+ external ragged indices and no idxTerm.  There is
# no other declarable boundary mechanism for utf8, and only a SINGLE ragged index may
# lean on the packet frame, so the header read must refuse (checked in dataset_hdr3.c,
# not at decode time).
f=reject_rank3_noterm
echo "Testing: header rejection, $f (multi-level utf8 without idxTerm)"
$TEXT $OPTS < test/$f.d3t > /dev/null 2>&1
if [ "$?" == "0" ]; then echo " Result: FAILED ($f.d3t should fail loud at the header)"; exit 4; fi
echo " Result: PASSED"
echo

# reject_sandwich_noterm: a utf8 SANDWICH ("*;3;*") without idxTerm.  Only one index
# is ragged, but the walk is 2 indices deep (multiple runs per record), so the packet
# frame cannot bound it -- the header read must refuse on walk DEPTH, not ragged count.
f=reject_sandwich_noterm
echo "Testing: header rejection, $f (utf8 sandwich without idxTerm)"
$TEXT $OPTS < test/$f.d3t > /dev/null 2>&1
if [ "$?" == "0" ]; then echo " Result: FAILED ($f.d3t should fail loud at the header)"; exit 4; fi
echo " Result: PASSED"
echo

# reject_sandwich_partial: the record terminator arrives after 2 of the 3 declared
# sensor runs.  The header is legal; the DECODER must refuse rather than store a
# record that contradicts the declared fixed extent.
f=reject_sandwich_partial
echo "Testing: decode rejection, $f (sandwich record short of its fixed extent)"
$TEXT $OPTS < test/$f.d3t > /dev/null 2>&1
if [ "$?" == "0" ]; then echo " Result: FAILED ($f.d3t should fail loud at decode)"; exit 4; fi
echo " Result: PASSED"
echo

exit 0
