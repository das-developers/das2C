#!/usr/bin/env bash

echo "Testing: Little endian input stream to ASCII conversion"

echo "   exec: cat test/das2_ascii_input1.d2s | ./$1/das2_ascii -r 4 -s 3 > $1/das2_ascii_output1.d2t"
cat test/das2_ascii_input1.d2s | ./$1/das2_ascii -r 4 -s 3 > $1/das2_ascii_output1.d2t

echo -n "   exec: cat test/das2_ascii_output1.d2t | ${MD5SUM}"
s1=$(cat test/das2_ascii_output1.d2t | ${MD5SUM})
echo " --> $s1"

echo -n "   exec: cat $1/das2_ascii_output1.d2t | ${MD5SUM}"
s2=$(cat $1/das2_ascii_output1.d2t | ${MD5SUM})
echo " --> $s2"


if [ "$s1" != "$s2" ] ; then
	echo " Result: FAILED"
	echo
	exit 4
fi

echo " Result: PASSED"
echo
exit 0
