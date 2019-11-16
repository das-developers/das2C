#!/usr/bin/env bash

# The original input dataset was generated using the commands:

# vgr_env
# vgpw_sa_rdr -R /opt/project/voyager/DATA 1 2014-04-02 2014-04-12 > FILE


echo "Testing: Peak Average Seconds Reduction Single YScan"

echo "   exec: cat test/das2_bin_peakavgsec_input1.d2s |  ./$1/das2_bin_peakavgsec 1800 | ./$1/das2_ascii -r 4 -s 3 > $1/das2_bin_peakavgsec_output1.d2t"
cat test/das2_bin_peakavgsec_input1.d2s |  ./$1/das2_bin_peakavgsec 1800 | ./$1/das2_ascii -r 4 -s 3 > $1/das2_bin_peakavgsec_output1.d2t

if [ "$?" != "0" ]; then
	echo "  Result: FAILED"
	exit 4
fi

echo -n "   exec: cat test/das2_bin_peakavgsec_output1.d2t | ${MD5SUM}"
s1=$(cat test/das2_bin_peakavgsec_output1.d2t | ${MD5SUM})
echo " --> $s1"

if [ "$?" != "0" ]; then
	echo "  Result: FAILED"
	exit 4
fi


echo -n "   exec: cat $1/das2_bin_peakavgsec_output1.d2t | ${MD5SUM}"
s2=$(cat $1/das2_bin_peakavgsec_output1.d2t | ${MD5SUM})
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
