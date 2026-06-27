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

# rank-3 (ex26) is intentionally absent: its multi-index <sequence> is not yet a
# supported variable class, so das3_text errors on it by design (see TestV3Read
# xfail + var_seq.c new_DasVarSeq guard).
FIXTURES="ex24_isee_rapid_rank1 ex25_isee_rapid_rank2"

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

exit 0
