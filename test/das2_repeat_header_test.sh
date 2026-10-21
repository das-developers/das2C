#!/usr/bin/env bash

# DasStream_addDesc() deliberately rejects a das2 <packet> header that is
# identical to one already live under another ID.  A ~2001 das1B shim
# re-sent known headers under fresh IDs, which defeats stitching a dataset
# back together by ID on read.  Pin that rule so it isn't "fixed" away
# again (it nearly was, 2026-10), and check that headers differing slightly
# (here, a plane name) are still accepted, each under its own ID.
#
# Only das2-model readers apply the rule.  A das3-model reader (das3_csv)
# converts each <packet> to a dataset before it reaches the stream, and
# datasets are not compared, so it accepts the repeat; that has always been
# so and is an open question, not pinned here.

BD=$1

H='<stream version="2.2">
  <properties String:title="repeat header test" />
</stream>
'
P1='<packet>
  <x type="time24" units="us2000"></x>
  <y type="ascii10" name="amp" units=""></y>
</packet>
'
P2='<packet>
  <x type="time24" units="us2000"></x>
  <y type="ascii10" name="amp2" units=""></y>
</packet>
'

# mkstream FILE HDR1 HDR2
mkstream(){
	printf '[00]%06d%s[01]%06d%s[02]%06d%s' ${#H} "$H" ${#2} "$2" ${#3} "$3" > $1
	printf ':01:2025-01-01T00:00:00.000 1.000e+00\n' >> $1
	printf ':02:2025-01-01T00:00:01.000 2.000e+00\n' >> $1
}

mkstream $BD/repeat_header_test.d2t    "$P1" "$P1"
mkstream $BD/similar_header_test.d2t "$P1" "$P2"

echo "Testing: das2-model readers reject a das2 header repeated under a second ID"
for PROG in das2_ascii das3_from_das2; do
	$BD/$PROG < $BD/repeat_header_test.d2t > /dev/null 2>$BD/repeat_header_test.err
	if [ "$?" == "0" ] || ! grep -q 'repeats the header for ID 01' $BD/repeat_header_test.err
	then
		echo "   $PROG did not reject the repeated header:"
		cat $BD/repeat_header_test.err
		echo " Result: FAILED"
		echo
		exit 4
	fi
done
echo " Result: PASSED"
echo

echo "Testing: Readers accept das2 headers that differ only slightly"
for PROG in das2_ascii das3_csv das3_from_das2; do
	$BD/$PROG < $BD/similar_header_test.d2t > /dev/null 2>$BD/similar_header_test.err
	if [ "$?" != "0" ]; then
		echo "   $PROG failed:"
		cat $BD/similar_header_test.err
		echo " Result: FAILED"
		echo
		exit 4
	fi
done

# das2_ascii re-emits both data packets, each under its own ID
s1=$($BD/das2_ascii < $BD/similar_header_test.d2t 2>/dev/null | grep -c '^:0[12]:')
if [ "$s1" != "2" ]; then
	echo "   das2_ascii emitted $s1 of 2 data packets"
	echo " Result: FAILED"
	echo
	exit 4
fi
echo " Result: PASSED"
echo
exit 0
