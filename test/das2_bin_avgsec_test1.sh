#!/usr/bin/env bash

# The original input dataset was generated using the command:

# /opt/project/cassini/SunOS.sparc/bin/rpws_lr_das2rdr.sh  \
#   '2013-05-21T02:00:00.000Z' '2013-05-21T14:00:00.000Z' '-lfdr ExEw \
#   -mfdr ExEw -mfr 13ExEw -hfr ABC12EuEvEx -n hfr_snd -n lp_rswp \
#   -n bad_data -n dpf_zero -n mfdr_mfr2 -n mfr3_hfra -n hf1_hfrc -a 
#   -b 30 -bgday'


# Since das1ToDas2 outputs native endian streams, we have to run the
# output through toAscii to compare output in a platform neutral manner.
# We could start using platform specific comparison sets but that sounds
# like asking for trouble.


echo "Testing: Bin Avgerage Seconds Reduction Single YScan"

echo "   exec: cat test/das2_bin_avgsec_input1.d2s |  ./$1/das2_bin_avgsec 300 | ./$1/das2_ascii -r 4 -s 3 > $1/das2_bin_avgsec_output1.d2t"
cat test/das2_bin_avgsec_input1.d2s |  ./$1/das2_bin_avgsec 300 | ./$1/das2_ascii -r 4 -s 3 > $1/das2_bin_avgsec_output1.d2t

if [ "$?" != "0" ]; then
	echo "  Result: FAILED"
	exit 4
fi

echo -n "   exec: cat test/das2_bin_avgsec_output1.d2t | ${MD5SUM}"
s1=$(cat test/das2_bin_avgsec_output1.d2t | ${MD5SUM})
echo " --> $s1"

if [ "$?" != "0" ]; then
	echo "  Result: FAILED"
	exit 4
fi


echo -n "   exec: cat $1/das2_bin_avgsec_output1.d2t | ${MD5SUM}"
s2=$(cat $1/das2_bin_avgsec_output1.d2t | ${MD5SUM})
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
