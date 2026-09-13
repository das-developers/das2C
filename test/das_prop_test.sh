#!/usr/bin/env bash

# Round-trip the das3 property coverage fixture through TestProp.  das3_test_props.d3t
# is its own gold: it is the canonical (idempotent) form, so re-emitting it must
# reproduce it byte-for-byte.  This checks that property separators, types,
# whitespace canonicalization and the long-stringArray line-wrap all serialize
# as expected.

echo "Testing: das3 property round-trip (separators, types, whitespace)"

EX=test/streams

# The makefile exports MD5SUM, platform-correct: md5sum on Linux, "md5 -r" on
# MacOS.  This fallback is only for running the script by hand outside make,
# where an empty MD5SUM makes both checksums empty and the comparison passes
# no matter what TestProp emitted.
if [ -z "${MD5SUM}" ]; then
	if   command -v md5sum >/dev/null 2>&1; then MD5SUM="md5sum"
	elif command -v md5    >/dev/null 2>&1; then MD5SUM="md5 -r"
	else echo " Result: FAILED (no md5sum/md5 found)"; exit 5; fi
fi

echo "   exec: ./$1/TestProp $EX/das3_test_props.d3t > $1/das3_test_props.d3t"
./$1/TestProp $EX/das3_test_props.d3t > $1/das3_test_props.d3t

if [ "$?" != "0" ]; then
	echo "  Result: FAILED"
	exit 4
fi

echo -n "   exec: cat $EX/das3_test_props.d3t | ${MD5SUM}"
s1=$(cat $EX/das3_test_props.d3t | ${MD5SUM})
echo " --> $s1"

echo -n "   exec: cat $1/das3_test_props.d3t | ${MD5SUM}"
s2=$(cat $1/das3_test_props.d3t | ${MD5SUM})
echo " --> $s2"

if [ "$s1" != "$s2" ] ; then
	echo " Result: FAILED"
	echo
	exit 4
fi

echo " Result: PASSED"
echo
exit 0
