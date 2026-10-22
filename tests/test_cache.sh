#!/usr/bin/env bash
# A cached run must return exactly what a fresh one returns.
#
# The cache kept only the response body, so the pagination -- which lives in
# the response headers -- was lost on a hit and the run stopped after the
# first page. A warm cache silently reported a fraction of the repository.
# This drives the client against a mock API that serves two pages and checks
# that a second, cached run sees the same items.

set -u

GS="${1:-./build/gs}"
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
WORK="$(mktemp -d)"
PORT=$((19100 + RANDOM % 800))
BASE="http://127.0.0.1:$PORT"

cleanup() {
    [ -n "${MOCK_PID:-}" ] && kill "$MOCK_PID" 2>/dev/null
    rm -rf "$WORK"
}
trap cleanup EXIT

fail() { echo "FAIL: $1" >&2; exit 1; }
ok()   { echo "  ok - $1"; }

python3 "$ROOT/tools/mock_api.py" "$PORT" 2 >"$WORK/mock.log" 2>&1 &
MOCK_PID=$!
for _ in $(seq 1 50); do
    curl -sf "$BASE/repos/o/r/meta" >/dev/null 2>&1 && break
    sleep 0.1
done

export XDG_CONFIG_HOME="$WORK/xdg"
export GS_CACHE_DIR="$WORK/cache"
export NO_COLOR=1
mkdir -p "$XDG_CONFIG_HOME/.config/gitsight"
cat > "$XDG_CONFIG_HOME/.config/gitsight/config.json" <<'EOF'
{"default_days":7,"platform":"github","lang":"en","export":{"item_limit":10}}
EOF

count_prs() {
    python3 -c "
import json,sys
d=json.load(open(sys.argv[1]))
print(len(d['pull_requests']['items']))
" "$1"
}

# --- a fresh run ------------------------------------------------------------
"$GS" analyze acme/gadget --api-base "$BASE" --no-cache -q -d 7 --max-pages 5 \
    -f json -o "$WORK/cold.json" >/dev/null 2>&1 || fail "the cold run failed"
COLD=$(count_prs "$WORK/cold.json")
[ "$COLD" = "200" ] || fail "the cold run saw $COLD pull requests, expected 200"

# --- the same run, now served from the cache --------------------------------
"$GS" analyze acme/gadget --api-base "$BASE" -q -d 7 --max-pages 5 \
    -f json -o "$WORK/warm.json" >/dev/null 2>&1 || fail "the warm run failed"
WARM=$(count_prs "$WORK/warm.json")
[ "$WARM" = "$COLD" ] || fail "the cached run saw $WARM pull requests, the fresh one $COLD"

# --- and a third one, to be sure the cache settles ---------------------------
"$GS" analyze acme/gadget --api-base "$BASE" -q -d 7 --max-pages 5 \
    -f json -o "$WORK/warm2.json" >/dev/null 2>&1 || fail "the third run failed"
WARM2=$(count_prs "$WORK/warm2.json")
[ "$WARM2" = "$COLD" ] || fail "the third run saw $WARM2 pull requests, expected $COLD"

# --- the cache must not have grown without bound ----------------------------
ENTRIES=$(find "$GS_CACHE_DIR" -name '*.json' 2>/dev/null | wc -l)
[ "$ENTRIES" -gt 0 ] || fail "nothing was cached"

# --- the cache key must not change with every second -----------------------
#
# The request URL carried the window's start as a timestamp, and the URL is
# the cache key -- so every run wrote a fresh set of files that nothing would
# ever read, and the directory grew without bound.
BEFORE_KEYS=$(find "$GS_CACHE_DIR" -name '*.json' 2>/dev/null | wc -l)
sleep 2
"$GS" analyze acme/gadget --api-base "$BASE" -q -d 7 --max-pages 5 \
    -f json -o "$WORK/later.json" >/dev/null 2>&1 || fail "the later run failed"
AFTER_KEYS=$(find "$GS_CACHE_DIR" -name '*.json' 2>/dev/null | wc -l)
[ "$AFTER_KEYS" -eq "$BEFORE_KEYS" ] \
    || fail "the cache grew from $BEFORE_KEYS to $AFTER_KEYS entries in two seconds"
LATER=$(count_prs "$WORK/later.json")
[ "$LATER" = "$COLD" ] || fail "the later run saw $LATER pull requests, expected $COLD"

# --- a run that asks to bypass the cache must go to the network ------------
#
# The daemon never set the cache flag, so the option the console offers was
# silently ignored and every run was answered from the cache.
BEFORE=$(curl -s "$BASE/__stats" | python3 -c 'import json,sys; print(json.load(sys.stdin)["requests"])')
"$GS" analyze acme/gadget --api-base "$BASE" --no-cache -q -d 7 --max-pages 5 \
    -f json -o "$WORK/nocache.json" >/dev/null 2>&1 || fail "the no-cache run failed"
AFTER=$(curl -s "$BASE/__stats" | python3 -c 'import json,sys; print(json.load(sys.stdin)["requests"])')
[ "$AFTER" -gt "$BEFORE" ] || fail "--no-cache still read from the cache (before=$BEFORE after=$AFTER)"
NOCACHE=$(count_prs "$WORK/nocache.json")
[ "$NOCACHE" = "$COLD" ] || fail "the no-cache run saw $NOCACHE pull requests, expected $COLD"

# --- and the daemon must honour it too -------------------------------------
#
# The daemon never set the cache flag, so the option the console offers was
# silently ignored there.
PORT2=$((19800 + RANDOM % 100))
BASE2="http://127.0.0.1:$PORT2"
XDG_CONFIG_HOME="$WORK/xdg2"
mkdir -p "$XDG_CONFIG_HOME/.config/gitsight"
cat > "$XDG_CONFIG_HOME/.config/gitsight/config.json" <<EOF
{"default_days":7,"platform":"github","lang":"en","api_base":"$BASE","export":{"item_limit":10}}
EOF
export XDG_CONFIG_HOME
"$GS" serve --port "$PORT2" --config "$XDG_CONFIG_HOME/.config/gitsight/config.json" \
    >"$WORK/daemon.log" 2>&1 &
DAEMON_PID=$!
for _ in $(seq 1 50); do
    curl -sf "$BASE2/api/health" >/dev/null 2>&1 && break
    sleep 0.1
done

daemon_prs() {
    local job
    job=$(curl -s -X POST -H 'Content-Type: application/json' \
        -d "{\"repo\":\"acme/gadget\",\"since_days\":7,\"no_cache\":$1}" \
        "$BASE2/api/jobs/analyze" | sed -n 's/.*"id":"\([^"]*\)".*/\1/p')
    for _ in $(seq 1 100); do
        local body
        body=$(curl -s "$BASE2/api/jobs/$job/result")
        case "$body" in *'"pull_requests"'*) break;; esac
        sleep 0.1
    done
    printf '%s' "$body" | python3 -c "
import json,sys
d=json.load(sys.stdin)
print(len(d['pull_requests']['items']))
"
}

DAEMON_COLD=$(daemon_prs false)
[ "$DAEMON_COLD" = "200" ] || fail "the daemon saw $DAEMON_COLD pull requests, expected 200"
DAEMON_WARM=$(daemon_prs false)
[ "$DAEMON_WARM" = "200" ] || fail "the cached daemon run saw $DAEMON_WARM pull requests, expected 200"

BEFORE=$(curl -s "$BASE/__stats" | python3 -c 'import json,sys; print(json.load(sys.stdin)["requests"])')
DAEMON_NOCACHE=$(daemon_prs true)
AFTER=$(curl -s "$BASE/__stats" | python3 -c 'import json,sys; print(json.load(sys.stdin)["requests"])')
[ "$AFTER" -gt "$BEFORE" ] || fail "the daemon ignored no_cache (before=$BEFORE after=$AFTER)"
[ "$DAEMON_NOCACHE" = "200" ] || fail "the no-cache daemon run saw $DAEMON_NOCACHE pull requests"

kill "$DAEMON_PID" 2>/dev/null

# --- the cache must not grow without bound ----------------------------------
#
# Nothing removed the entries that had expired, so the directory grew for as
# long as the daemon ran.
for i in $(seq 1 40); do
    "$GS" analyze acme/gadget --api-base "$BASE" --no-cache -q -d 7 --max-pages 5 \
        -f json -o "$WORK/prune.json" >/dev/null 2>&1
done
ENTRIES_AFTER=$(find "$GS_CACHE_DIR" -name '*.json' 2>/dev/null | wc -l)
[ "$ENTRIES_AFTER" -le 64 ] \
    || fail "the cache holds $ENTRIES_AFTER entries after 40 runs"

echo "cache tests: 11 passed, 0 failed"
