#!/usr/bin/env bash

# das3_info regression tests: gold comparisons of the dataset printers over a
# handful of small fixtures, plus a check that -q reports parse status through
# the exit code alone.
#
# The golds pin DasStream_info(), DasDs_toStr(), DasDim_toStr() and
# DasVar_toStr(), which are the strings das3_text, das3_csv and das2py show
# people when they ask what a dataset is made of.  A variable line has three
# pipe separated sections: source with element type and units, item counts
# per index, then formalism with its parameters.  These fixtures cover:
#
#   ex07: a das2 stream up-converted on read, whose center time is a binary
#         operation on the reference and offset arrays
#   ex17: a rank 1 stored vector with no frame, and a TT2000 time
#   ex22: rank 3, a composite sequence coordinate degenerate in the record
#         index, and two vectors in different frames
#   ex27: a ragged byte blob, a geoloc with every parameter stated, and a
#         vector with a body binding
#   ex30: ragged in the inner index, an offset sequence, a dimensionless
#         quality scalar
#   ex40: a 3;3 rotation with from and to frames
#   ex44: a calendar (struct) time sequence and the same cadence on an epoch
#
# Drafted by Claude Fable 5.1, filed by Chris Piker.

BD=$1
INFO=$BD/das3_info
EX=examples

# Pick whatever checksum tool exists (md5sum on Linux, md5 on BSD/macOS).
if   command -v md5sum >/dev/null 2>&1; then MD5SUM="md5sum"
elif command -v md5    >/dev/null 2>&1; then MD5SUM="md5"
else echo " Result: FAILED (no md5sum/md5 found)"; exit 5; fi

GOLDS="ex07_cassini_rpws_wbr.d2s ex17_vector_noframe.d3b ex22_mag_grid_vec.d3t \
 ex27_epop_fai_mgf_blob.d3b ex30_cassini_ragged_notlast.d3b ex40_rotation.d3t \
 ex44_time_sequence.d3t"

for f in $GOLDS; do
	b=${f%.*}
	echo "Testing: das3_info gold, $f"
	echo "   exec: $INFO < $EX/$f > $BD/$b.info"
	$INFO < $EX/$f > $BD/$b.info 2>/dev/null
	if [ "$?" != "0" ]; then echo " Result: FAILED (das3_info errored on $f)"; exit 4; fi

	s1=$(cat $EX/$b.info | ${MD5SUM})
	s2=$(cat $BD/$b.info | ${MD5SUM})
	echo "   gold $EX/$b.info --> $s1"
	echo "   new  $BD/$b.info --> $s2"
	if [ "$s1" != "$s2" ] ; then echo " Result: FAILED (description != gold)"; exit 4; fi

	echo " Result: PASSED"
	echo
done

# The file argument path, as opposed to stdin, on one fixture
f=ex17_vector_noframe
echo "Testing: das3_info gold via file argument, $f.d3b"
echo "   exec: $INFO $EX/$f.d3b > $BD/$f.info"
$INFO $EX/$f.d3b > $BD/$f.info 2>/dev/null
s1=$(cat $EX/$f.info | ${MD5SUM})
s2=$(cat $BD/$f.info | ${MD5SUM})
if [ "$s1" != "$s2" ] ; then echo " Result: FAILED (description != gold)"; exit 4; fi
echo " Result: PASSED"
echo

# Quiet mode: exit status only, nothing on either output channel
echo "Testing: das3_info -q on a valid stream is silent and exits 0"
out=$($INFO -q $EX/ex43_msc_complex_cal.d3b 2>&1); rc=$?
if [ "$rc" != "0" ] || [ -n "$out" ]; then echo " Result: FAILED (rc $rc, output '$out')"; exit 4; fi
echo " Result: PASSED"
echo

echo "Testing: das3_info -q on a rejected stream is silent and exits non-zero"
out=$($INFO -q test/streams/reject_index_mismatch.d3t 2>&1); rc=$?
if [ "$rc" == "0" ] || [ -n "$out" ]; then echo " Result: FAILED (rc $rc, output '$out')"; exit 4; fi
echo " Result: PASSED"
echo

exit 0
