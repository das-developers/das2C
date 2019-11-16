#!/usr/bin/env bash

# This test sample if from the Juno Magentometer data reader

# /opt/project/juno/bin/centos5.x86_64/waves_invoke.sh \
#  fgm_pds_miscrdr --das2times=scet 2014-04-08T00:15 2014-04-08T00:30


echo "Testing: Bin Avgerage Seconds Reduction X-Multi-Y"

echo "   exec: cat test/das2_bin_avgsec_input2.d2s |  ./$1/das2_bin_avgsec 10 | ./$1/das2_ascii -r 4 -s 3 > $1/das2_bin_avgsec_output2.d2t"
cat test/das2_bin_avgsec_input2.d2s |  ./$1/das2_bin_avgsec 10 | ./$1/das2_ascii -r 4 -c > $1/das2_bin_avgsec_output2.d2t

if [ "$?" != "0" ]; then
	echo "  Result: FAILED"
	exit 4
fi

echo -n "   exec: cat test/das2_bin_avgsec_output2.d2t | ${MD5SUM}"
s1=$(cat test/das2_bin_avgsec_output2.d2t | ${MD5SUM})
echo " --> $s1"

if [ "$?" != "0" ]; then
	echo "  Result: FAILED"
	exit 4
fi


echo -n "   exec: cat $1/das2_bin_avgsec_output2.d2t | ${MD5SUM}"
s2=$(cat $1/das2_bin_avgsec_output2.d2t | ${MD5SUM})
echo " --> $s2"

if [ "$?" != "0" ]; then
	echo "  Result: FAILED"
	exit 4
fi


if [ "$s1" != "$s2" ] ; then
	echo " Result: FAILED"
	echo
	exit 4
fi

echo " Result: PASSED"
echo
exit 0
