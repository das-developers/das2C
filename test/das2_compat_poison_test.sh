#!/usr/bin/env bash

# Every poison macro in das3/das2_compat.h must stop the build, and the
# error must name the fix.  A poison that silently compiles (say an old
# function-like name used as a bare identifier, or a das3 header that starts
# defining the old name again) would let old code link against something it
# doesn't mean, so each one is compiled on its own here.
#
# Poisons are found by expansion, not by a list: any macro whose use
# preprocesses to an identifier containing _removed__, _never_existed,
# _withheld__ or _is_private__.  New entries are covered automatically.

BD=$1
CC=${CC:-gcc}
HDR=das3/das2_compat.h
TAG='_removed__|_never_existed|_withheld__|_is_private__'

DIR="${BD:?}/compat_poison"
rm -rf "${DIR:?}"
mkdir -p "$DIR" || exit 3

nPoison=0
nFail=0
for sDef in $(grep -oE '^#define +[A-Za-z_0-9]+(\(\.\.\.\))?' $HDR | awk '{print $2}' | sort -u); do
	sName=${sDef%(...)}
	if [ "$sName" != "$sDef" ]; then sUse="$sName(0)"; else sUse="$sName"; fi

	sSrc="$DIR/$sName.c"
	printf '#include <%s>\nint f(void){ (void)(%s); return 0; }\n' "$HDR" "$sUse" > "$sSrc"

	sId=$($CC -std=c99 -I. -E "$sSrc" 2>/dev/null | tail -1 | grep -oE "[A-Za-z_0-9]*($TAG)[A-Za-z_0-9]*" | head -1)
	[ -z "$sId" ] && continue   # an alias or adapter, TestCompat covers those

	nPoison=$((nPoison + 1))
	if $CC -std=c99 -Wall -I. -c "$sSrc" -o /dev/null 2> "$DIR/$sName.err"; then
		echo "ERROR: poison $sName compiled, it should not"
		nFail=$((nFail + 1))
	elif ! grep -q -- "$sId" "$DIR/$sName.err"; then
		echo "ERROR: poison $sName failed without naming $sId"
		nFail=$((nFail + 1))
	fi
done

if [ $nPoison -eq 0 ]; then
	echo "ERROR: no poison macros found in $HDR, is the tag pattern stale?"
	exit 4
fi

if [ $nFail -gt 0 ]; then
	echo "ERROR: $nFail of $nPoison das2_compat.h poison macros misbehaved"
	exit 5
fi

rm -rf "${DIR:?}"
echo "INFO: All $nPoison das2_compat.h poison macros stop the build with their fix"
exit 0
