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
#   subsep_0.7.json       0.7 multi-select flags (subSep), from das-cat
#   multi_0.7.json        0.7 list properties ("multi"), joined and repeated
#
# Each case is: arguments, then the exact URL expected on stdout.  Error cases
# check the exit code instead.
#
# Drafted by Claude Fable 5.1, filed by Chris Piker.

BD=$1
FETCH="$BD/das_fetch --no-cache -l warning"   # the fixtures are files, nothing to cache
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
	"$GB?read.time.min=2026-04-01&read.time.max=2026-04-01T01:30&read.time.inter=60&read.data=lshell"

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
expect_url $G 0 csv delimiter=Tab csv.headers=false -- \
	"$GB?read.time.min=2026-01-01&read.time.max=2026-01-02&read.time.inter=60&format.type=csv&format.headers=1&format.delim=tab"
expect_rc 4 url $G 0 csv delimiter=Tab serial=text               # serial is a das setting, das is off now

# example by bare number, by examples.N, and as the default when nothing is asked
expect_url $G 0 time.inter=30 -- \
	"$GB?read.time.min=2026-01-01&read.time.max=2026-01-02&read.time.inter=30"
expect_url $G examples.0 -- \
	"$GB?read.time.min=2026-01-01&read.time.max=2026-01-02&read.time.inter=60"
expect_url $G -- \
	"$GB?read.time.min=2026-01-01&read.time.max=2026-01-02&read.time.inter=60"

# -s FILE=NAME names the seed node, and PATH is then optional.  A bare
# location is a seed with no name, a root, which works too.  Long options
# take their value after a space or an '='.
SU="tag:das2.org,2012:site:/uiowa/tracers/tracers-2/spice/geomagcoords/flex"
HU="tag:das2.org,2012:site:/uiowa/tracers/tracers-2/l1a/cdpu/hsk/flex"
expect_url -s $G 0 -- \
	"$GB?read.time.min=2026-01-01&read.time.max=2026-01-02&read.time.inter=60"   # a root seed
expect_url -s $G=$SU 0 -- \
	"$GB?read.time.min=2026-01-01&read.time.max=2026-01-02&read.time.inter=60"
expect_url --seed=$G=$SU 0 -- \
	"$GB?read.time.min=2026-01-01&read.time.max=2026-01-02&read.time.inter=60"
expect_url --seed $G=$SU 0 -- \
	"$GB?read.time.min=2026-01-01&read.time.max=2026-01-02&read.time.inter=60"
expect_url -s $G=$SU $SU 0 -- \
	"$GB?read.time.min=2026-01-01&read.time.max=2026-01-02&read.time.inter=60"
if $FETCH node -s $G=$SU 2>/dev/null | grep -q "^URI:      $SU\$"; then
	nPass=$((nPass + 1))
else
	echo " FAIL: das_fetch node -s $G=$SU did not report the seed URI"; nFail=$((nFail + 1))
fi
# A NAME alone is a node found through the das catalog root.  Offline,
# only a mount at that name can answer for it.
expect_url -s $SU -m $G=$SU $SU 0 -- \
	"$GB?read.time.min=2026-01-01&read.time.max=2026-01-02&read.time.inter=60"
expect_rc 2 url -r -s $G 0                                    # -r and -s pick the seed both
expect_rc 2 url -s $G=                                        # nothing after the '='
expect_rc 2 url --seed                                        # no value

# -m mounts the same node on the federated catalog.  Offline only the
# mounted side can be exercised, and a mount is not a place to start, so
# the path is named.
expect_url -m $G=$SU $SU 0 -- \
	"$GB?read.time.min=2026-01-01&read.time.max=2026-01-02&read.time.inter=60"
expect_rc 2 url -m $G=$SU 0                                   # a mount gives no default path
export DASFETCH_MOUNT=$G=$SU
expect_url $SU 0 -- \
	"$GB?read.time.min=2026-01-01&read.time.max=2026-01-02&read.time.inter=60"
unset DASFETCH_MOUNT
expect_url -m $G=$SU -s $H=$HU 0 -- \
	"$HB?read.time.min=2026-01-06T14:12&read.time.max=2026-01-06T14:24"   # a seed and a mount make an offline catalog
expect_url -m $G=$SU -s $H=$HU $SU 0 -- \
	"$GB?read.time.min=2026-01-01&read.time.max=2026-01-02&read.time.inter=60"
expect_rc 2 url -m site:/uiowa 0                              # mount needs FILE=NAME
expect_url --mount=$G=$SU $SU 0 -- \
	"$GB?read.time.min=2026-01-01&read.time.max=2026-01-02&read.time.inter=60"
# Under a root seed a mount is named by a path from the root, and it is
# consulted for the relative spelling, shadowing the listing
expect_url -s $FX/cat/site.json -m $G=uiowa/broken uiowa/broken 0 -- \
	"$GB?read.time.min=2026-01-01&read.time.max=2026-01-02&read.time.inter=60"
expect_rc 3 url -s $G=$SU site:/uiowa/juno                    # a seed is the whole catalog

# DASFETCH_SEED stands in for -s, -s still wins over it, and -r sets it
# aside.  Set but empty is the same as not set.
export DASFETCH_SEED=$G=$SU
expect_url 0 -- \
	"$GB?read.time.min=2026-01-01&read.time.max=2026-01-02&read.time.inter=60"
expect_url -s $H=$HU 0 -- \
	"$HB?read.time.min=2026-01-06T14:12&read.time.max=2026-01-06T14:24"
expect_rc 2 url -r 0                                          # the root is no source
export DASFETCH_SEED=
expect_rc 2 url 0                                             # no seed file, no default path
unset DASFETCH_SEED

# dotted member paths, section names may be plural or singular
expect_url $G 0 data.lshell coord.time.inter=30 -- \
	"$GB?read.time.min=2026-01-01&read.time.max=2026-01-02&read.time.inter=30&read.data=lshell"

# raw protocol parameters override translated ones and may add new ones
expect_url $G 0 -p read.time.inter=5 -p extra=1 -- \
	"$GB?read.time.min=2026-01-01&read.time.max=2026-01-02&read.time.inter=5&extra=1"

# --- 0.7 hsk: version enum, FlagSet accumulation with comma, cdf example ----

# the node default (das3) is not sent, only choices are
expect_url $H 0 -- \
	"$HB?read.time.min=2026-01-06T14:12&read.time.max=2026-01-06T14:24"
expect_url $H 0 version=das2 nmi_errors irq3_errors data.hsk_task_errors -- \
	"$HB?read.time.min=2026-01-06T14:12&read.time.max=2026-01-06T14:24&read.data=nmi_errors,irq3_errors,hsk_task_errors&format.version=2"
expect_url $H 1 -- \
	"$HB?read.time.min=2026-01-05T14:24&read.time.max=2026-01-06T14:24&format.type=cdf"
expect_url $H 0 formats.csv time.res=10 -- \
	"$HB?read.time.min=2026-01-06T14:12&read.time.max=2026-01-06T14:24&bin.time.max=10&format.type=csv"

# --- formats are an exclusive set (xorGroup): the last one named is on ---
expect_url $H 0 formats.csv formats.cdf -- \
	"$HB?read.time.min=2026-01-06T14:12&read.time.max=2026-01-06T14:24&format.type=cdf"
expect_url $H 0 formats.cdf formats.csv -- \
	"$HB?read.time.min=2026-01-06T14:12&read.time.max=2026-01-06T14:24&format.type=csv"
# on top of an example that chose cdf
expect_url $H 1 csv -- \
	"$HB?read.time.min=2026-01-05T14:24&read.time.max=2026-01-06T14:24&format.type=csv"
expect_url $H 1 csv cdf -- \
	"$HB?read.time.min=2026-01-05T14:24&read.time.max=2026-01-06T14:24&format.type=cdf"
expect_url $H 1 das -- \
	"$HB?read.time.min=2026-01-05T14:24&read.time.max=2026-01-06T14:24"
expect_rc 4 url $H 0 formats.csv das.version=das2                # a setting of the format not chosen
expect_rc 4 url $H 0 csv.delimiter=Tab                           # the same, csv was never chosen
expect_rc 4 url $H 0 das=false                                   # no format at all is not a choice
expect_url $H 0 csv csv.delimiter=Tab cdf csv.delimiter= -- \
	"$HB?read.time.min=2026-01-06T14:12&read.time.max=2026-01-06T14:24&format.type=cdf"

# --- 0.6 juno: '&' joiner, space separated flags, protocol level examples ---

expect_url $J time=2021-02-21,2021-02-22 b lfrl das2_text -- \
	"$JB&end_time=2021-02-22&start_time=2021-02-21&params=B%20LFRL&ascii=true"
expect_url $J time=2021-02-21,2021-02-22,0 ascii option.xmas=true -- \
	"$JB&end_time=2021-02-22&start_time=2021-02-21&resolution=0&params=ASCII%20XMAS"

# a 0.6 example is raw parameters, read back into interface terms, so the
# URL comes out in interface order; user keys still win, required defaults
# must not clobber the example's values
expect_url $J 7 resolution=10 -- \
	"$JB&end_time=2016-177T00:00&start_time=2016-176T00:00&resolution=10&params=B"

# --- 0.7 subsep: multi-select flags, elements joined behind one prefix ------

S=$FX/subsep_0.7.json
SB="https://example.org/das/server?server=dataset&dataset=Test/SubSep&start_time=2017-168&end_time=2017-169"
expect_url $S 0 -- "$SB&params=fan=090,270%20outputType=flux"
expect_url $S lfdr_ex lfdr_l fan_180 -- "$SB&params=fan=180%20-lfdr%20LEx"

# --- 0.7 multi: list properties, KEY=a,b ---------------------------------

M=$FX/multi_0.7.json
MB="https://example.org/das/server?server=dataset&dataset=Test/Multi&start_time=2017-168&end_time=2017-169"
# enum items go out in node order, two properties fill one flag, an item's
# pval is what travels (HF2 -> 2)
expect_url $M 0 -- "$MB&params=-lfdr%20ExEw%20-mfr%2012Ex%20-hfr%20A2%20-n%20bad_data%20hf1pwr"
expect_url $M 1 -- "$MB&params=fft512%202.4%202.5"
# a plain parameter joins with its subSep or repeats; a flag repeats prefix and all
expect_url $M 2 -- "$MB&read.data=ts2_l2_bac_tscs,ts2_l2_bac_fac&params=--drop=a%20b%20--drop=c&parameter=bx&parameter=bz"
expect_url $M 3 -- "$MB"
expect_url $M lfdr=ew,EX 'nix=fake_data, mfdr_mfr2' -- "$MB&params=-lfdr%20ExEw%20-n%20mfdr_mfr2%20fake_data"
# the default list in any order is the default, and is not sent
expect_url $M fan=270,090 -- "$MB"
expect_url $M fan=180 -- "$MB&params=fan=180"
# each chosen item may be a switch of its own
expect_url $M verbosity=loud,quiet -- "$MB&params=-q%20-v"
# a list and booleans may feed one joined parameter
expect_url $M bac_fac pair=4,3 -- "$MB&read.data=ts2_l2_bac_fac,4,3"
# an empty value takes back the example's list, whatever spelling set it
expect_url $M 0 lfdr= mfr_bands= mfr_sensors= -- "$MB&params=-hfr%20A2%20-n%20bad_data%20hf1pwr"
expect_url $M 'fan= ' -- "$MB"                                # blanks alone are an empty list
expect_url $M drop=a,A -- "$MB&params=--drop=a%20--drop=A"    # free text items differ by case
expect_rc 4 url $M lfdr=Ex,ex                                 # enum items do not
expect_rc 4 url $M notches=1,2,3,4                            # more than the node allows
expect_rc 4 url $M pair=1                                     # fewer
expect_rc 4 url $M notches=1,,2                               # empty item
expect_rc 4 url $M notches=2,2                                # repeated item
expect_rc 4 url $M notches=1,x                                # an item of the wrong type
expect_rc 4 url $M lfdr=Ex,Ez                                 # an item not in the enum
expect_rc 4 url $M neither=a                                  # flag says neither join nor repeat
expect_rc 4 url $M both=a                                     # flag says both
expect_rc 4 url $M plain=a                                    # parameter says neither

if $FETCH node $M 2>/dev/null | grep -q "notches  *real list  *kHz .*0.0 to 14.41 (up to 3)"; then
	nPass=$((nPass + 1))
else
	echo " FAIL: das_fetch node $M did not list notches as a bounded real list"; nFail=$((nFail + 1))
fi

expect_url -o params $M 2 -- \
"https://example.org/das/server?server=dataset&dataset=Test/Multi
   start_time=2017-168
   end_time=2017-169
   read.data=ts2_l2_bac_tscs,ts2_l2_bac_fac
   params=--drop=a b --drop=c
   parameter=bx
   parameter=bz"

# --- url -o params: every base, then the query's parameters decoded ------

expect_url -o params $S lfdr_ex lfdr_l fan_180 -- \
"https://example.org/das/server?server=dataset&dataset=Test/SubSep
   start_time=2017-168
   end_time=2017-169
   params=fan=180 -lfdr LEx"

expect_url -o params $G 0 -- \
"$GB
http://127.0.0.1:8765/data/geomag_alt.d2s
   read.time.min=2026-01-01
   read.time.max=2026-01-02
   read.time.inter=60"

# a raw '+' and '%' show as typed, not as a space or an escape
expect_url -o params $S 3 -p 'x=a+b%41' -- \
"https://example.org/das/server?server=dataset&dataset=Test/SubSep
   start_time=2017-168
   end_time=2017-169
   x=a+b%41"

expect_url -o url $S 3 -- "$SB"

# --- choosing among a node's sources by -t ---------------------------------

# twin.json offers flex (das2 stream, csv, cdf) and das3 (das3 stream); with
# no -t das_fetch asks for das3 streams, then das2, then anything, one ask
# per type, so das3 wins here.  A -t list is a set.  An item with a '/' is
# a whole media type, one without is looked for anywhere in the type.
T=$FX/cat/twin.json
O=$FX/cat/old.json
expect_url -s $T=site:/uiowa/twin 0 -- \
	"$HB?read.time.min=2026-01-06T14:12&read.time.max=2026-01-06T14:24"
expect_url -s $T=site:/uiowa/twin -t text/csv 0 -- \
	"$GB?read.time.min=2026-01-01&read.time.max=2026-01-02&read.time.inter=60"
expect_url -s $T=site:/uiowa/twin -t 'image/png, application/x-cdf' 0 -- \
	"$GB?read.time.min=2026-01-01&read.time.max=2026-01-02&read.time.inter=60"
expect_rc 3 url -s $T=site:/uiowa/twin -t '*/*' 0                 # two sources qualify, name one
expect_rc 3 url -s $T=site:/uiowa/twin -t 'application/vnd.das.stream, text/csv' 0   # order does not pick
expect_rc 3 url -s $T=site:/uiowa/twin -t image/png 0             # none does
expect_url -s $T=site:/uiowa/twin -t csv 0 -- \
	"$GB?read.time.min=2026-01-01&read.time.max=2026-01-02&read.time.inter=60"   # part of text/csv
expect_url -s $T=site:/uiowa/twin --type=CSV 0 -- \
	"$GB?read.time.min=2026-01-01&read.time.max=2026-01-02&read.time.inter=60"   # case does not matter
expect_url -s $T=site:/uiowa/twin --type das2 0 -- \
	"$GB?read.time.min=2026-01-01&read.time.max=2026-01-02&read.time.inter=60"
expect_rc 3 url -s $T=site:/uiowa/twin -t das 0                   # both have it, name one
expect_url -s $T=site:/uiowa/twin -t das3 0 -- \
	"$HB?read.time.min=2026-01-06T14:12&read.time.max=2026-01-06T14:24"   # everyday names
expect_url -s $T=site:/uiowa/twin -t DAS3 0 -- \
	"$HB?read.time.min=2026-01-06T14:12&read.time.max=2026-01-06T14:24"
expect_url -s $T=site:/uiowa/twin -t d3b 0 -- \
	"$HB?read.time.min=2026-01-06T14:12&read.time.max=2026-01-06T14:24"
expect_rc 3 url -s $T=site:/uiowa/twin -t d3t 0                   # no das3 text source
expect_url -s $T=site:/uiowa/twin -t d2s 0 -- \
	"$GB?read.time.min=2026-01-01&read.time.max=2026-01-02&read.time.inter=60"
expect_url -s $T=site:/uiowa/twin -t 'png, cdf' 0 -- \
	"$GB?read.time.min=2026-01-01&read.time.max=2026-01-02&read.time.inter=60"   # names in a list
expect_rc 3 url -s $T=site:/uiowa/twin -t csv/x 0                 # with a '/' only whole types
expect_url -s $T=site:/uiowa/twin site:/uiowa/twin/flex -t '*/*' 0 -- \
	"$GB?read.time.min=2026-01-01&read.time.max=2026-01-02&read.time.inter=60"
if $FETCH node -s $T=site:/uiowa/twin -t '*/*' 2>/dev/null | grep -q "^  das3 "; then
	nPass=$((nPass + 1))
else
	echo " FAIL: das_fetch node with an ambiguous pick did not list the children"; nFail=$((nFail + 1))
fi
if $FETCH node -s $T=site:/uiowa/twin -o sources 2>/dev/null | grep -q "^  das3 "; then
	nPass=$((nPass + 1))
else
	echo " FAIL: das_fetch node -o sources did not list the children"; nFail=$((nFail + 1))
fi

# a 2018 Collection has no provides lists, convention stands in
expect_url -s $O=site:/uiowa/old -t application/vnd.das2.das2stream time=2021-02-21,2021-02-22 -- \
	"$JB&end_time=2021-02-22&start_time=2021-02-21"
expect_url -s $O=site:/uiowa/old time=2021-02-21,2021-02-22 -- \
	"$JB&end_time=2021-02-22&start_time=2021-02-21"                    # nothing provides das3, the das2 ask settles it
expect_rc 3 url -s $O=site:/uiowa/old -t text/csv time=2021-02-21,2021-02-22

# -m may be repeated, and the deeper mount wins whatever the order
expect_url -m $T=site:/uiowa/twin -m $G=site:/uiowa/twin/flex -t '*/*' site:/uiowa/twin/flex 0 -- \
	"$GB?read.time.min=2026-01-01&read.time.max=2026-01-02&read.time.inter=60"
expect_url -m $G=site:/uiowa/twin/flex -m $T=site:/uiowa/twin -t '*/*' site:/uiowa/twin/flex 0 -- \
	"$GB?read.time.min=2026-01-01&read.time.max=2026-01-02&read.time.inter=60"

# --- error paths, by exit code ----------------------------------------------

expect_rc 2 url -o coords $S
expect_rc 2 node -o params $S
expect_rc 2 json -o url $S

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
expect_rc 2 node -o bogus $G                                  # bad section list
expect_rc 2 url -o examples $G 0                              # -o is for node
expect_rc 2 node -l bogus $G                                  # bad log level
expect_rc 0 node -l W $G                                      # a level's first letter will do
expect_rc 0 node --log=warning $G
expect_rc 2 url --timeout 0 $G 0                              # not a time to wait
expect_rc 2 url --timeout=ten $G 0
expect_rc 2 url --cache                                       # no value
expect_rc 2 url -x $G 0                                       # no such option
expect_rc 2 url --seedling=$G 0                               # a long name must match whole
expect_rc 0 -v                                                # version
expect_rc 0 --version
expect_rc 2 url                                               # missing path
expect_rc 2 bogus $G                                          # unknown command
expect_rc 3 url $FX/does_not_exist.json 0                     # unreadable node

# formats are hidden by default, -o brings them back
if $FETCH node $G 2>/dev/null | grep -q "delimiter"; then
	echo " FAIL: das_fetch node $G showed format keys without -o"; nFail=$((nFail + 1))
else
	nPass=$((nPass + 1))
fi
if $FETCH node -o formats $G 2>/dev/null | grep -q "delimiter"; then
	nPass=$((nPass + 1))
else
	echo " FAIL: das_fetch node -o formats $G hid the format keys"; nFail=$((nFail + 1))
fi
if $FETCH node -o all $G 2>/dev/null | grep -q "delimiter"; then
	nPass=$((nPass + 1))
else
	echo " FAIL: das_fetch node -o all $G hid the format keys"; nFail=$((nFail + 1))
fi
if $FETCH node -o examples $G 2>/dev/null | grep -q "^coords:"; then
	echo " FAIL: das_fetch node -o examples $G printed key tables"; nFail=$((nFail + 1))
else
	nPass=$((nPass + 1))
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
