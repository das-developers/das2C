#!/usr/bin/env bash

# Mixed-endian stream: little_endian_real8 x tags carrying sun_real4 (big
# endian) z values, so one pass covers both swap paths. The values there are
# picked to be exact in binary32 and clear of any 4-sig-fig rounding boundary,
# so the output should be the same across platform printf implementations.
echo "Testing: Mixed endian input stream to ASCII conversion (byte swap)"

echo "   exec: cat test/streams/das2_swap_test.d2s | ./$1/das2_ascii -r 4 -s 3 > $1/das2_swap_test.d2t"
cat test/streams/das2_swap_test.d2s | ./$1/das2_ascii -r 4 -s 3 > $1/das2_swap_test.d2t

echo -n "   exec: cat test/streams/das2_swap_test.d2t | ${MD5SUM}"
s1=$(cat test/streams/das2_swap_test.d2t | ${MD5SUM})
echo " --> $s1"

echo -n "   exec: cat $1/das2_swap_test.d2t | ${MD5SUM}"
s2=$(cat $1/das2_swap_test.d2t | ${MD5SUM})
echo " --> $s2"


if [ "$s1" != "$s2" ] ; then
	echo " Result: FAILED"
	echo
	exit 4
fi

echo " Result: PASSED"
echo
exit 0
