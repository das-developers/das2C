#!/usr/bin/env bash

echo "Testing: Little endian input stream to ASCII conversion"

echo "   exec: cat examples/ex04_voyager_pws_sa.d2s | ./$1/das2_ascii -r 4 -s 3 > $1/ex04_voyager_pws_sa.d2t"
cat examples/ex04_voyager_pws_sa.d2s | ./$1/das2_ascii -r 4 -s 3 > $1/ex04_voyager_pws_sa.d2t

echo -n "   exec: cat examples/ex04_voyager_pws_sa.d2t | ${MD5SUM}"
s1=$(cat examples/ex04_voyager_pws_sa.d2t | ${MD5SUM})
echo " --> $s1"

echo -n "   exec: cat $1/ex04_voyager_pws_sa.d2t | ${MD5SUM}"
s2=$(cat $1/ex04_voyager_pws_sa.d2t | ${MD5SUM})
echo " --> $s2"


if [ "$s1" != "$s2" ] ; then
	echo " Result: FAILED"
	echo
	exit 4
fi

echo " Result: PASSED"
echo
exit 0
