#!/usr/bin/env bash

# The input stream carries zValidMin and zValidMax on two of its four packet
# types and has a few samples replaced with out of range values.  Bins must
# average only the valid samples where a range is given, and everything but
# fill where one is not.

echo "Testing: Bin Avgerage Seconds Reduction, valid range honored"

echo "   exec: cat examples/ex09_juno_waves_survey.d2t |  ./$1/das2_bin_avgsec 60 | ./$1/das2_ascii -r 4 -s 3 > $1/ex09_juno_waves_survey.avg60.d2t"
cat examples/ex09_juno_waves_survey.d2t |  ./$1/das2_bin_avgsec 60 | ./$1/das2_ascii -r 4 -s 3 > $1/ex09_juno_waves_survey.avg60.d2t

if [ "$?" != "0" ]; then
	echo "  Result: FAILED"
	exit 4
fi

echo -n "   exec: cat examples/ex09_juno_waves_survey.avg60.d2t | ${MD5SUM}"
s1=$(cat examples/ex09_juno_waves_survey.avg60.d2t | ${MD5SUM})
echo " --> $s1"

echo -n "   exec: cat $1/ex09_juno_waves_survey.avg60.d2t | ${MD5SUM}"
s2=$(cat $1/ex09_juno_waves_survey.avg60.d2t | ${MD5SUM})
echo " --> $s2"

if [ "$s1" != "$s2" ] ; then
	echo " Result: FAILED"
	echo
	exit 4
fi

echo " Result: PASSED"
echo
exit 0
