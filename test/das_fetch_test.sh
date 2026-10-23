#!/usr/bin/env bash

# Regression test for das_fetch's interface-to-protocol translation.  Runs
# entirely offline: the fixtures in test/das_fetch/ are real catalog nodes
# saved from the uiowa servers with their base URLs pointed at localhost,
# and the 'url' command resolves a query without contacting anything.
#
# Three node generations are covered:
#   geomag_flex_0.7.json  dasflex HttpStreamSrc 0.7, required interval param,
#                         FlagSet data toggle, csv/cdf/das format groups
#   hsk_flex_0.7.json     0.7 with 73 data variables and a das2/das3 version enum
#   juno_survey_0.6.json  2018 era HttpStreamSrc 0.6, flag_set with space
#                         separator, base URL that already has a query string
#
# Each case is: arguments, then the exact URL expected on stdout.  Error cases
# check the exit code instead.
#
# Drafted by Claude Fable 5.1, filed by Chris Piker.

BD=$1
FETCH="$BD/das_fetch -C none"   # no node cache, the fixtures are files anyway
FX=test/das_fetch

nFail=0
nPass=0

# expect_url ARGS... -- EXPECTED
expect_url() {
	local args=()
	while [ "$1" != "--" ]; do args+=("$1"); shift; done
	shift
	local expected="$1"
	local actual
	actual=$($FETCH url "${args[@]}" 2>/dev/null)
	if [ "$actual" == "$expected" ]; then
		nPass=$((nPass + 1))
	else
		echo " FAIL: das_fetch url ${args[*]}"
		echo "   expected: $expected"
		echo "   actual:   $actual"
		nFail=$((nFail + 1))
	fi
}

# expect_rc CODE ARGS...
expect_rc() {
	local want=$1; shift
	$FETCH "$@" > /dev/null 2>&1
	local got=$?
	if [ $got -eq $want ]; then
		nPass=$((nPass + 1))
	else
		echo " FAIL: das_fetch $* : expected exit $want, got $got"
		nFail=$((nFail + 1))
	fi
}

G=$FX/geomag_flex_0.7.json
H=$FX/hsk_flex_0.7.json
J=$FX/juno_survey_0.6.json
GB="http://127.0.0.1:8765/data/geomag.d2s"
HB="https://space.physics.uiowa.edu/das/stream/source/tracers/tracers-2/l1a/cdpu/hsk/flex"
JB="http://127.0.0.1:8765/data/juno_survey.d2s?server=dataset&dataset=Juno/WAV/Survey"

# --- 0.7 geomag: ranges, shortcuts, aliases, flags, examples, formats --------

expect_url $G time=2026-04-01,2026-04-01T01:30,60 lshell -- \
	"$GB?read.time.min=2026-04-01&read.time.max=2026-04-01T01%3A30&read.time.inter=60&read.data=lshell"

# minimum/maximum spellings work on a 0.7 node, section prefix optional
expect_url $G coord.time.minimum=2026-02-01 time.maximum=2026-02-02 inter=60 -- \
	"$GB?read.time.min=2026-02-01&read.time.max=2026-02-02&read.time.inter=60"

# example 0, then an explicit key overrides one of its settings
expect_url $G 0 -- \
	"$GB?read.time.min=2026-01-01&read.time.max=2026-01-02&read.time.inter=60"
expect_url $G 0 time.inter=30 -- \
	"$GB?read.time.min=2026-01-01&read.time.max=2026-01-02&read.time.inter=30"

# a boolean set to its default is not sent
expect_url $G 0 lshell=no -- \
	"$GB?read.time.min=2026-01-01&read.time.max=2026-01-02&read.time.inter=60"

# format group by name, enum pval substitution (Tab -> tab), boolean with pval
expect_url $G 0 csv delimiter=Tab csv.headers=false serial=text -- \
	"$GB?read.time.min=2026-01-01&read.time.max=2026-01-02&read.time.inter=60&format.serial=text&format.type=csv&format.headers=1&format.delim=tab"

# example by bare number, by examples.N, and as the default when nothing is asked
expect_url $G 0 time.inter=30 -- \
	"$GB?read.time.min=2026-01-01&read.time.max=2026-01-02&read.time.inter=30"
expect_url $G examples.0 -- \
	"$GB?read.time.min=2026-01-01&read.time.max=2026-01-02&read.time.inter=60"
expect_url $G -- \
	"$GB?read.time.min=2026-01-01&read.time.max=2026-01-02&read.time.inter=60"

# -r may name the root node itself, PATH is then optional
expect_url -r $G 0 -- \
	"$GB?read.time.min=2026-01-01&read.time.max=2026-01-02&read.time.inter=60"

# a seed, URI=URL: the node at URL is named URI, relative paths hang off it
SU="tag:das2.org,2012:site:/uiowa/tracers/tracers-2/spice/geomagcoords/flex"
expect_url -r $SU=$G 0 -- \
	"$GB?read.time.min=2026-01-01&read.time.max=2026-01-02&read.time.inter=60"
expect_url -r $SU=$G $SU 0 -- \
	"$GB?read.time.min=2026-01-01&read.time.max=2026-01-02&read.time.inter=60"
if $FETCH node -r $SU=$G 2>/dev/null | grep -q "^URI:      $SU\$"; then
	nPass=$((nPass + 1))
else
	echo " FAIL: das_fetch node -r $SU=$G did not report the seed URI"; nFail=$((nFail + 1))
fi

# -g grafts the same seed over the federated catalog.  Offline only the
# seed side can be exercised; -g and -r URL together is a usage error.
expect_url -g $SU=$G 0 -- \
	"$GB?read.time.min=2026-01-01&read.time.max=2026-01-02&read.time.inter=60"
export DASFETCH_GRAFT=$SU=$G
expect_url 0 -- \
	"$GB?read.time.min=2026-01-01&read.time.max=2026-01-02&read.time.inter=60"
unset DASFETCH_GRAFT
expect_rc 2 url -g $SU=$G -r $H 0                             # two roots
expect_rc 2 url -g site:/uiowa 0                              # graft needs URI=URL
expect_rc 2 url -r site:/uiowa=nourl 0                        # '=' but no URL after it
expect_rc 3 url -r $SU=$G site:/uiowa/juno                    # -r seed is the whole catalog

# DASFETCH_ROOT stands in for -r, and -r still wins over it
export DASFETCH_ROOT=$G
expect_url 0 -- \
	"$GB?read.time.min=2026-01-01&read.time.max=2026-01-02&read.time.inter=60"
expect_url -r $H 0 -- \
	"$HB?read.time.min=2026-01-06T14%3A12&read.time.max=2026-01-06T14%3A24"
unset DASFETCH_ROOT

# dotted member paths, section names may be plural or singular
expect_url $G 0 data.lshell coord.time.inter=30 -- \
	"$GB?read.time.min=2026-01-01&read.time.max=2026-01-02&read.time.inter=30&read.data=lshell"

# raw protocol parameters override translated ones and may add new ones
expect_url $G 0 -p read.time.inter=5 -p extra=1 -- \
	"$GB?read.time.min=2026-01-01&read.time.max=2026-01-02&read.time.inter=5&extra=1"

# --- 0.7 hsk: version enum, FlagSet accumulation with comma, cdf example ----

# the node default (das3) is not sent, only choices are
expect_url $H 0 -- \
	"$HB?read.time.min=2026-01-06T14%3A12&read.time.max=2026-01-06T14%3A24"
expect_url $H 0 version=das2 nmi_errors irq3_errors data.hsk_task_errors -- \
	"$HB?read.time.min=2026-01-06T14%3A12&read.time.max=2026-01-06T14%3A24&read.data=nmi_errors%2Cirq3_errors%2Chsk_task_errors&format.version=2"
expect_url $H 1 -- \
	"$HB?read.time.min=2026-01-05T14%3A24&read.time.max=2026-01-06T14%3A24&format.type=cdf"
expect_url $H 0 formats.csv das.version=das2 time.res=10 -- \
	"$HB?read.time.min=2026-01-06T14%3A12&read.time.max=2026-01-06T14%3A24&bin.time.max=10&format.version=2&format.type=csv"

# --- 0.6 juno: '&' joiner, space separated flags, protocol level examples ---

expect_url $J time=2021-02-21,2021-02-22 b lfrl das2_text -- \
	"$JB&end_time=2021-02-22&start_time=2021-02-21&params=B%20LFRL&ascii=true"
expect_url $J time=2021-02-21,2021-02-22,0 ascii option.xmas=true -- \
	"$JB&end_time=2021-02-22&start_time=2021-02-21&resolution=0&params=ASCII%20XMAS"

# a 0.6 example is raw parameters; user keys still win, required defaults
# must not clobber the example's values
expect_url $J 7 resolution=10 -- \
	"$JB&end_time=2016-177T00%3A00&params=B&resolution=10&start_time=2016-176T00%3A00"

# --- error paths, by exit code ----------------------------------------------

expect_rc 4 url $G time=2026-04-01,2026-04-01T01:30          # required inter missing
expect_rc 4 url $G 0 foo=1                                    # no such key
expect_rc 4 url $G 0 fracSecs=4                               # ambiguous (das, csv)
expect_rc 4 url $G 0 formats.xml                              # no such format
expect_rc 4 url $G 0 lshell=maybe                             # not a boolean
expect_rc 4 url $G 0 delimiter=Colon                          # not in enum
expect_rc 4 url $G 0 version=das3                             # geomag offers only das2
expect_rc 4 url $G 9                                        # no such example
expect_rc 4 url $G example=0                                  # sections are not keys
expect_rc 4 url $G examples.one                               # example must be a number
expect_rc 4 url $G 0 data.nope                                 # no such data variable
expect_rc 2 node -s bogus $G                                  # bad section list
expect_rc 2 url                                               # missing path
expect_rc 2 bogus $G                                          # unknown command
expect_rc 3 url $FX/does_not_exist.json 0                     # unreadable node

# formats are hidden by default, -s or -v brings them back
if $FETCH node $G 2>/dev/null | grep -q "delimiter"; then
	echo " FAIL: das_fetch node $G showed format keys without -s"; nFail=$((nFail + 1))
else
	nPass=$((nPass + 1))
fi
if $FETCH node -s formats $G 2>/dev/null | grep -q "delimiter"; then
	nPass=$((nPass + 1))
else
	echo " FAIL: das_fetch node -s formats $G hid the format keys"; nFail=$((nFail + 1))
fi

# node and json commands run on every fixture and print the key table header
for f in $G $H $J; do
	if $FETCH node $f 2>/dev/null | grep -q "^  Key "; then
		nPass=$((nPass + 1))
	else
		echo " FAIL: das_fetch node $f printed no key table"
		nFail=$((nFail + 1))
	fi
	if $FETCH json "$f#protocol" 2>/dev/null | grep -q '"method"\|"convention"'; then
		nPass=$((nPass + 1))
	else
		echo " FAIL: das_fetch json $f#protocol printed no protocol block"
		nFail=$((nFail + 1))
	fi
done

if [ $nFail -eq 0 ]; then
	echo " Result: PASSED ($nPass checks)"
	exit 0
else
	echo " Result: FAILED ($nFail of $((nPass + nFail)) checks)"
	exit 1
fi
