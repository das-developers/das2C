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
FIXTURES="ex22_mag_grid_vec ex24_isee_rapid_rank1 ex25_isee_rapid_rank2 ex26_isee_rapid_rank3 ex27_epop_fai_mgf_blob ex33_cassini_ragged_utf8 ex34_ragged_fixstr"

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

exit 0
