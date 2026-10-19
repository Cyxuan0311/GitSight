#!/usr/bin/env bash
# Boots `gs serve` against the real built console and checks that the SPA is
# served, that deep links fall back to index.html, and that hashed assets are
# cached immutably. Skipped when dist/ has not been built.
set -u

GS="$(cd "$(dirname "$1")" && pwd)/$(basename "$1")"
SRC="${2:-$(cd "$(dirname "$0")/.." && pwd)}"

if [ ! -f "$SRC/web/dist/index.html" ]; then
  echo "SKIP: web/dist not built (run: npm --prefix web run build)"
  exit 0
fi

PASS=0
FAIL=0
ok() { PASS=$((PASS + 1)); }
fail() { echo "FAIL: $1" >&2; FAIL=$((FAIL + 1)); }

WORK=$(mktemp -d)
trap 'rm -rf "$WORK"; [ -n "${SRV_PID:-}" ] && kill "$SRV_PID" 2>/dev/null' EXIT

cp -r "$SRC/web/dist" "$WORK/web"

PORT=$(awk 'BEGIN{srand();print 20000+int(rand()*2000)}')
BASE="http://127.0.0.1:$PORT"

NO_COLOR=1 "$GS" serve --port "$PORT" --config "$WORK/config.json" --web-root "$WORK/web" \
  > "$WORK/server.log" 2>&1 &
SRV_PID=$!

for _ in $(seq 1 60); do
  curl -s -o /dev/null "$BASE/api/health" 2>/dev/null && break
  sleep 0.1
done
if ! curl -s -o /dev/null "$BASE/api/health" 2>/dev/null; then
  echo "FAIL: server did not start" >&2
  cat "$WORK/server.log" >&2
  exit 1
fi

# index.html at the root and on every client-side route
body=$(curl -s "$BASE/")
echo "$body" | grep -q 'id="root"' && ok || fail "root does not serve index.html"
echo "$body" | grep -q '/assets/index-' && ok || fail "index.html references no hashed bundle"

for route in analyze compare batch subscriptions usage llm settings completion; do
  status=$(curl -s -o "$WORK/page" -w '%{http_code}' "$BASE/$route")
  if [ "$status" = "200" ] && grep -q 'id="root"' "$WORK/page"; then ok; else
    fail "SPA fallback for /$route returned $status"
  fi
done

# An asset-like miss must be a real 404, not the HTML shell.
status=$(curl -s -o /dev/null -w '%{http_code}' "$BASE/assets/missing.js")
[ "$status" = "404" ] && ok || fail "missing asset returned $status, expected 404"

status=$(curl -s -o /dev/null -w '%{http_code}' --path-as-is "$BASE/../../etc/passwd")
[ "$status" = "404" ] && ok || fail "traversal returned $status, expected 404"

# Hashed bundles are immutable; the entrypoint must not be cached.
asset=$(echo "$body" | sed -n 's/.*src="\([^"]*\/assets\/index-[^"]*\.js\)".*/\1/p')
if [ -n "$asset" ]; then
  headers=$(curl -s -D - -o /dev/null "$BASE$asset")
  echo "$headers" | grep -qi 'cache-control:.*immutable' && ok || fail "hashed asset is not immutable"
  echo "$headers" | grep -qi 'content-type:.*javascript' && ok || fail "asset served without a JS content type"
else
  fail "could not find the hashed bundle in index.html"
fi

index_headers=$(curl -s -D - -o /dev/null "$BASE/")
echo "$index_headers" | grep -qi 'cache-control:.*no-cache' && ok || fail "index.html is cacheable"

# API and static live side by side on one origin.
curl -s "$BASE/api/health" | grep -q '"version"' && ok || fail "API not reachable on the same origin"

echo
echo "web console tests: $PASS passed, $FAIL failed"
[ "$FAIL" -eq 0 ]