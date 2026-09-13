#!/usr/bin/env bash

# Since das1ToDas2 outputs native endian streams, we have to run the
# output through toAscii to compare output in a platform neutral manner.
# We could start using platform specific comparison sets but that sounds
# like asking for trouble.


echo "Testing: Das1 To Das2 Stream conversion - TCA Dataset"

echo "   exec: ./$1/das2_from_das1 ${PWD}/examples/ex02_galileo_sys3.dsdf 86400 1996-09-01 1996-11-02 | ./$1/das2_ascii -r 4 -s 3 > $1/ex02_galileo_sys3.d2t"
./$1/das2_from_das1 ${PWD}/examples/ex02_galileo_sys3.dsdf 86400 1996-09-01 1996-11-02 | ./$1/das2_ascii -r 4 -s 3 > $1/ex02_galileo_sys3.d2t

echo -n "   exec: cat examples/ex02_galileo_sys3.d2t | ${MD5SUM}"
s1=$(cat examples/ex02_galileo_sys3.d2t | ${MD5SUM})
echo " --> $s1"

echo -n "   exec: cat $1/ex02_galileo_sys3.d2t | ${MD5SUM}"
s2=$(cat $1/ex02_galileo_sys3.d2t | ${MD5SUM})
echo " --> $s2"

# A missing file makes cat fail, ${MD5SUM} hash nothing, and both sides come out
# as the empty-input digest -- which compares equal and reports PASSED.  This
# test did exactly that after the ex02 rename.  Refuse an empty digest.
if [ -z "$s1" ] || [ "${s1%% *}" = "d41d8cd98f00b204e9800998ecf8427e" ]; then
	echo " Result: FAILED (no reference stream to compare against)"
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
