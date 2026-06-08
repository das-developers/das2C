#!/usr/bin/env bash

# Round-trip the das3 property coverage fixture through TestProp.  ex23_props.d3t
# is its own gold: it is the canonical (idempotent) form, so re-emitting it must
# reproduce it byte-for-byte.  This checks that property separators, types,
# whitespace canonicalization and the long-stringArray line-wrap all serialize
# as expected.

echo "Testing: das3 property round-trip (separators, types, whitespace)"

echo "   exec: ./$1/TestProp test/ex23_props.d3t > $1/ex23_props.d3t"
./$1/TestProp test/ex23_props.d3t > $1/ex23_props.d3t

if [ "$?" != "0" ]; then
	echo "  Result: FAILED"
	exit 4
fi

echo -n "   exec: cat test/ex23_props.d3t | ${MD5SUM}"
s1=$(cat test/ex23_props.d3t | ${MD5SUM})
echo " --> $s1"

echo -n "   exec: cat $1/ex23_props.d3t | ${MD5SUM}"
s2=$(cat $1/ex23_props.d3t | ${MD5SUM})
echo " --> $s2"

if [ "$s1" != "$s2" ] ; then
	echo " Result: FAILED"
	echo
	exit 4
fi

echo " Result: PASSED"
echo
exit 0
