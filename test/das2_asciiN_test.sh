#!/usr/bin/env bash

# Issue #27: general ASCII planes, type="asciiW", of any legal width.
#
# The default output format is guessed when an encoding is constructed, and
# encodings are constructed while *reading* headers.  So a width the output
# guesser can't handle must not stop a reader; only writing such a plane with
# no explicit format is an error.  First check that pure readers accept every
# legal width, 2..127.  Then check that das2_ascii, which re-encodes the
# plane, round-trips a value byte for byte at every width the guesser covers
# (7 and up).  A negative value is used since it is the widest %e output for
# a given precision.

BD=$1

# W includes the trailing separator, so the value is W-1 characters
mkstream(){
	local W=$1 V=$2
	local H='<stream version="2.2">
  <properties String:title="asciiN test" />
</stream>
'
	local P='<packet>
  <x type="time24" units="us2000"></x>
  <y type="ascii'$W'" name="radius" units=""></y>
</packet>
'
	printf '[00]%06d%s[01]%06d%s:01:2025-01-01T00:00:00.000 %s\n' \
		${#H} "$H" ${#P} "$P" "$V"
}

# Same rule as the library's guesser, precision clamped to 0..16
deffmt(){
	local W=$1 P=$(( $1 - 8 ))
	[ $P -lt 0 ] && P=0
	[ $P -gt 16 ] && P=16
	echo "%$((W-1)).${P}e"
}

# Exactly a double.  The shell's printf formats in long double, so any other
# literal can round differently in the last digit than das2C does.
VAL=-1.2345678901234566904321354741114191710948944091796875

echo "Testing: Readers accept asciiN planes for N = 2..127"
for W in $(seq 2 127); do
	if [ $W -lt 7 ]; then V=$(printf "%$((W-1))s" 1)
	else V=$(printf "$(deffmt $W)" $VAL); fi
	mkstream $W "$V" > $BD/ascii${W}_test.d2t

	for PROG in das3_csv das3_from_das2; do
		$BD/$PROG < $BD/ascii${W}_test.d2t > /dev/null 2>$BD/ascii${W}_test.err
		if [ "$?" != "0" ]; then
			echo "   $PROG failed on ascii$W:"
			cat $BD/ascii${W}_test.err
			echo " Result: FAILED"
			echo
			exit 4
		fi
	done
done
echo " Result: PASSED"
echo

echo "Testing: das2_ascii round-trips asciiN values for N = 7..40"
for W in $(seq 7 40); do
	# Reuse the streams from above, the value was written with deffmt
	s1=$(grep '^:01:' $BD/ascii${W}_test.d2t)
	s2=$($BD/das2_ascii < $BD/ascii${W}_test.d2t 2>$BD/ascii${W}_test.err | grep '^:01:')
	if [ "$s1" != "$s2" ]; then
		echo "   ascii$W in : '$s1'"
		echo "   ascii$W out: '$s2'"
		cat $BD/ascii${W}_test.err
		echo " Result: FAILED"
		echo
		exit 4
	fi
done
echo " Result: PASSED"
echo
exit 0
