#!/usr/bin/env bash

# Catalog node cache: the second run makes no request, an aged copy is
# revalidated with If-None-Match and renewed by a 304, a changed node is
# fetched again, an unreachable server leaves the old copy in use, and a
# server without validators still works, just without the saving.
#
# Runs against test/das_cache_srv.py on a loopback port picked at random,
# so nothing leaves the machine.  The fixtures are copied to a scratch
# directory so one of them can be touched.  Skips, passing, when python3
# is not available.
#
# Drafted by Claude Fable 5.1, filed by Chris Piker.

BD=$1
FETCH="$BD/das_fetch"
FX=test/das_fetch

if ! command -v python3 > /dev/null 2>&1; then
	echo " SKIP: python3 not found, cache revalidation test not run"
	exit 0
fi

nFail=0
nPass=0
check() {   # check DESCRIPTION CONDITION...
	local sWhat=$1; shift
	if "$@"; then nPass=$((nPass + 1)); else echo " FAIL: $sWhat"; nFail=$((nFail + 1)); fi
}

TMPD=$(mktemp -d)
SRVPID=""
cleanup() { [ -n "$SRVPID" ] && kill "$SRVPID" 2>/dev/null; rm -rf "$TMPD"; }
trap cleanup EXIT

SRV=$TMPD/srv; mkdir -p "$SRV"; cp $FX/geomag_flex_0.7.json "$SRV/"
LOG=$TMPD/requests.log; : > "$LOG"
export DAS_USERDIR=$TMPD/userdir
CACHE=$DAS_USERDIR/cache/nodes

start_server() {   # start_server [--no-validators]
	rm -f "$TMPD/port"
	python3 test/das_cache_srv.py --dir "$SRV" --log "$LOG" --port-file "$TMPD/port" "$@" &
	SRVPID=$!
	for i in 1 2 3 4 5 6 7 8 9 10; do [ -s "$TMPD/port" ] && break; sleep 0.5; done
	[ -s "$TMPD/port" ] || { echo " FAIL: test server did not start"; exit 1; }
	PORT=$(cat "$TMPD/port")
}
stop_server() { kill "$SRVPID" 2>/dev/null; wait "$SRVPID" 2>/dev/null; SRVPID=""; }

mtime() { python3 -c "import os,sys; print(int(os.stat(sys.argv[1]).st_mtime))" "$1"; }

EXPECT="http://127.0.0.1:8765/data/geomag.d2s?read.time.min=2026-01-01&read.time.max=2026-01-02&read.time.inter=60"

start_server
U="http://127.0.0.1:$PORT/geomag_flex_0.7.json"

# 1. First contact: a plain GET, a 200, a cached body and its validators
sOut=$($FETCH url $U 0 2>/dev/null)
check "first run resolves the node"         [ "$sOut" == "$EXPECT" ]
check "first run made exactly one request"  [ "$(wc -l < "$LOG")" -eq 1 ]
check "first request was unconditional"     grep -q "200 inm=- ims=-" "$LOG"
BODY=$(ls "$CACHE"/*.json 2>/dev/null | head -1)
check "a cached copy was written"           [ -n "$BODY" ] && [ -s "$BODY" ]
check "its validators were saved beside it" grep -q "^ETag: " "$BODY.hdr"
check "Last-Modified saved too"             grep -q "^Last-Modified: " "$BODY.hdr"

# 2. Inside the trust window nothing is sent at all
sOut=$($FETCH url $U 0 2>/dev/null)
check "second run resolves from the cache"  [ "$sOut" == "$EXPECT" ]
check "second run made no request"          [ "$(wc -l < "$LOG")" -eq 1 ]

# 3. -f forces a check: conditional GET, 304, body not resent, copy renewed
sleep 1.1
nBefore=$(mtime "$BODY")
sOut=$($FETCH -f url $U 0 2>/dev/null)
check "forced check still resolves"         [ "$sOut" == "$EXPECT" ]
check "forced check made one request"       [ "$(wc -l < "$LOG")" -eq 2 ]
sETag=$(sed -n 's/^ETag: //p' "$BODY.hdr")
check "it carried the saved ETag"           grep -q "304 inm=$sETag ims=" "$LOG"
check "it carried the saved date too"       grep -qv "ims=-" <(tail -1 "$LOG")
check "the copy's age was renewed"          [ "$(mtime "$BODY")" -gt "$nBefore" ]

# 4. The node changes on the server: the next check downloads it again
sleep 1.1
touch "$SRV/geomag_flex_0.7.json"
sOut=$($FETCH -f url $U 0 2>/dev/null)
check "changed node still resolves"         [ "$sOut" == "$EXPECT" ]
check "changed node came back as a 200"     grep -q "^GET .* 200 inm=$sETag" <(tail -1 "$LOG")
sNewETag=$(sed -n 's/^ETag: //p' "$BODY.hdr")
check "new validators replaced the old"     [ -n "$sNewETag" ] && [ "$sNewETag" != "$sETag" ]

# 5. Server gone: a forced check fails over to the cached copy with a warning
stop_server
sErr=$($FETCH -f url $U 0 2>&1 >/dev/null); nRet=$?
sOut=$($FETCH -f url $U 0 2>/dev/null)
check "unreachable server, cached copy used"  [ "$sOut" == "$EXPECT" ]
check "and the run succeeds"                  [ $nRet -eq 0 ]
check "with a warning that says so"           grep -q "cached copy" <<< "$sErr"

# 6. A server with no validators: every forced check is a full fetch, no side file
rm -rf "$DAS_USERDIR"; : > "$LOG"
start_server --no-validators
U="http://127.0.0.1:$PORT/geomag_flex_0.7.json"
sOut=$($FETCH url $U 0 2>/dev/null)
check "plain server resolves"                 [ "$sOut" == "$EXPECT" ]
BODY=$(ls "$CACHE"/*.json 2>/dev/null | head -1)
check "plain server copy cached"              [ -n "$BODY" ]
check "but no validators to save"             [ ! -e "$BODY.hdr" ]
sOut=$($FETCH -f url $U 0 2>/dev/null)
check "forced check is unconditional"         grep -q "200 inm=- ims=-" <(tail -1 "$LOG")
check "two full fetches in all"               [ "$(grep -c " 200 " "$LOG")" -eq 2 ]
stop_server

echo
if [ $nFail -eq 0 ]; then
	echo " Result: PASSED ($nPass checks)"
	exit 0
else
	echo " Result: FAILED ($nFail of $((nPass + nFail)) checks)"
	exit 1
fi
