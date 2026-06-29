#!/usr/bin/env bash

# Gold-compare das3_csv over a stream carrying a SIGNED 1-byte column (encoding
# "byte").  This is a deliberate regression watch: das2C once coerced every
# 1-byte scalar variable to vtUByte (unsigned), which made das3_csv fail on a
# signed column.  The fixture's first records carry negative values (-1, -128,
# -42, ...) precisely because a positive byte (0..127) renders the SAME whether
# read signed or unsigned -- only a high-bit-set byte proves signedness.  So if
# the unsigned coercion ever returns, in any form, the rendered values diverge
# from the gold (-1 -> 255, -128 -> 128, ...) and this test fails.
#
# Drafted by Claude Opus 4.8, filed by Chris Piker.

BD=$1
IN=test/das3_csv_unsigned_test.d3b
GOLD=test/das3_csv_unsigned_test.csv

# MD5SUM was unset -- ${MD5SUM} expanded to nothing, so s1==s2=="" and this test
# silently passed regardless of output.  Pick whatever checksum tool exists.
if   command -v md5sum >/dev/null 2>&1; then MD5SUM="md5sum"
elif command -v md5    >/dev/null 2>&1; then MD5SUM="md5"
else echo " Result: FAILED (no md5sum/md5 found)"; exit 5; fi

echo "Testing: das3_csv signed 1-byte column (vtByte must not become vtUByte)"

echo "   exec: $BD/das3_csv < $IN > $BD/das3_csv_unsigned_test.csv"
$BD/das3_csv < $IN > $BD/das3_csv_unsigned_test.csv
if [ "$?" != "0" ]; then echo " Result: FAILED (das3_csv errored on signed byte)"; exit 4; fi

s1=$(cat $GOLD | ${MD5SUM})
s2=$(cat $BD/das3_csv_unsigned_test.csv | ${MD5SUM})
echo "   gold $GOLD --> $s1"
echo "   new  $BD/das3_csv_unsigned_test.csv --> $s2"

if [ "$s1" != "$s2" ] ; then
	echo " Result: FAILED (rendered values != gold; signed/unsigned regression?)"
	echo
	exit 4
fi

echo " Result: PASSED"
echo
exit 0
