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
mkdir -p "$XDG_CONFIG_HOME/gitsight"
cat > "$XDG_CONFIG_HOME/gitsight/config.json" <<'EOF'
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

echo "cache tests: 4 passed, 0 failed"
