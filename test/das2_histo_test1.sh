#!/usr/bin/env bash

# Technically this is a test of both das2_histo and das2_ascii as the 
# stream output is run through that program as well.

echo "Testing: Das2 Stream Histogram Calculator"

echo "   exec: ./$1/das2_histo < ${PWD}/examples/ex10_juno_waves_hfrh.d2t | ./$1/das2_ascii -r 5 > $1/ex10_juno_waves_hfrh_histo.d2t"
./$1/das2_histo < ${PWD}/examples/ex10_juno_waves_hfrh.d2t | ./$1/das2_ascii -r 5 > $1/ex10_juno_waves_hfrh_histo.d2t

echo -n "   exec: cat examples/ex10_juno_waves_hfrh_histo.d2t | ${MD5SUM}"
s1=$(cat examples/ex10_juno_waves_hfrh_histo.d2t | ${MD5SUM})
echo " --> $s1"

echo -n "   exec: cat $1/ex10_juno_waves_hfrh_histo.d2t | ${MD5SUM}"
s2=$(cat $1/ex10_juno_waves_hfrh_histo.d2t | ${MD5SUM})
echo " --> $s2"


if [ "$s1" != "$s2" ] ; then
	echo " Result: FAILED"
	echo
	exit 4
fi

echo " Result: PASSED"
echo
exit 0
