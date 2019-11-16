#!/usr/bin/env bash

# Since das1ToDas2 outputs native endian streams, we have to run the
# output through toAscii to compare output in a platform neutral manner.
# We could start using platform specific comparison sets but that sounds
# like asking for trouble.


echo "Testing: Das1 To Das2 Stream conversion - Non-TCA Multi-Y Dataset"

echo "   exec: ./$1/das2_from_das1 ${PWD}/test/das2_from_das1_test1.dsdf 1997-01-01 1997-01-02 | ./$1/das2_ascii -r 4 -s 3 > $1/das2_from_das1_output1.d2t"
./$1/das2_from_das1 ${PWD}/test/das2_from_das1_test1.dsdf 1997-01-01 1997-01-02 | ./$1/das2_ascii -r 4 -s 3 > $1/das2_from_das1_output1.d2t

echo -n "   exec: cat test/das2_from_das1_output1.d2t | ${MD5SUM}"
s1=$(cat test/das2_from_das1_output1.d2t | ${MD5SUM})
echo " --> $s1"

echo -n "   exec: cat $1/das2_from_das1_output1.d2t | ${MD5SUM}"
s2=$(cat $1/das2_from_das1_output1.d2t | ${MD5SUM})
echo " --> $s2"


if [ "$s1" != "$s2" ] ; then
	echo " Result: FAILED"
	echo
	exit 4
fi

echo " Result: PASSED"
echo
exit 0
