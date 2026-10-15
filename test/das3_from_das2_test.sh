#!/usr/bin/env bash

# das3_from_das2 regression tests on the Juno FGM example, ex11.
#
# The program promises that data bytes pass through unaltered, so the first
# checks strip the packet tags from the das2 input and the das3 output and
# compare what is left, with and without a metafile.  The metafile run is also
# held to a gold, examples/ex11_juno_fgm_scse.d3t, which is the same stream
# with the three field components grouped as a vector in the SCSE frame.  The
# binary input is checked by parsing the output and counting its records.
# Last, a grouping that would need re-encoding must be refused.
#
# Drafted by Claude Fable 5.1, filed by Chris Piker.

BD=$1
PROG=$BD/das3_from_das2
INFO=$BD/das3_info
EX=examples
META=$EX/ex11_juno_fgm_scse.toml

if   command -v md5sum >/dev/null 2>&1; then MD5SUM="md5sum"
elif command -v md5    >/dev/null 2>&1; then MD5SUM="md5"
else echo " Result: FAILED (no md5sum/md5 found)"; exit 5; fi

# das2 text data lines are ":NN:payload", das3 are "|Pd|N|LEN|payload"
d2payload() { grep '^:[0-9][0-9]:' "$1" | sed 's/^:[0-9][0-9]://'; }
d3payload() { grep '^|Pd|' "$1" | sed 's/^|Pd|[0-9]*|[0-9]*|//'; }

f=ex11_juno_fgm_scse
for mode in plain meta; do
	if [ $mode = plain ]; then OPTS=""; else OPTS="-m $META"; fi
	echo "Testing: das3_from_das2 byte pass-through ($mode), $f.d2t"
	echo "   exec: $PROG $OPTS < $EX/$f.d2t > $BD/${f}_$mode.d3t"
	$PROG $OPTS < $EX/$f.d2t > $BD/${f}_$mode.d3t 2>/dev/null
	if [ "$?" != "0" ]; then echo " Result: FAILED (das3_from_das2 errored, $mode)"; exit 4; fi

	s1=$(d2payload $EX/$f.d2t | ${MD5SUM})
	s2=$(d3payload $BD/${f}_$mode.d3t | ${MD5SUM})
	echo "   das2 payload --> $s1"
	echo "   das3 payload --> $s2"
	if [ "$s1" != "$s2" ]; then echo " Result: FAILED (payload bytes changed, $mode)"; exit 4; fi
	echo " Result: PASSED"
	echo
done

echo "Testing: das3_from_das2 metafile gold, $f.d3t"
s1=$(cat $EX/$f.d3t | ${MD5SUM})
s2=$(cat $BD/${f}_meta.d3t | ${MD5SUM})
echo "   gold $EX/$f.d3t --> $s1"
echo "   new  $BD/${f}_meta.d3t --> $s2"
if [ "$s1" != "$s2" ]; then echo " Result: FAILED (output != gold)"; exit 4; fi
echo " Result: PASSED"
echo

echo "Testing: das3_from_das2 binary input, $f.d2s"
echo "   exec: $PROG -m $META < $EX/$f.d2s | $INFO"
$PROG -m $META < $EX/$f.d2s 2>/dev/null | $INFO > $BD/${f}_meta.info 2>/dev/null
if [ "$?" != "0" ]; then echo " Result: FAILED (output did not parse)"; exit 4; fi
grep -q 'i:4800' $BD/${f}_meta.info
if [ "$?" != "0" ]; then echo " Result: FAILED (expected 4800 records)"; exit 4; fi
grep -q 'vector frame=JUNO_SUN_EQU_RTN' $BD/${f}_meta.info
if [ "$?" != "0" ]; then echo " Result: FAILED (no vector in the SCSE frame)"; exit 4; fi
echo " Result: PASSED"
echo

echo "Testing: das3_from_das2 refuses a grouping that would re-encode"
cat > $BD/${f}_reject.toml <<'TOML'
[[dataset.dim]]
physDim = "B"
[[dataset.dim.var]]
match = [{ element = "y", name = "x" }, { element = "y", name = "mag" }]
ops = { kind = "vector" }
TOML
echo "   exec: $PROG -m $BD/${f}_reject.toml < $EX/$f.d2t"
$PROG -m $BD/${f}_reject.toml < $EX/$f.d2t > /dev/null 2>&1
if [ "$?" == "0" ]; then echo " Result: FAILED (non-adjacent planes were grouped)"; exit 4; fi
echo " Result: PASSED"
echo

exit 0
