#!/usr/bin/env bash
# Final smoke test against the built console: drives the daemon exactly as the
# browser does, then checks that the served bundle actually boots by looking for
# the module entry the SPA needs.
set -u
GS="$(cd "$(dirname "$1")" && pwd)/$(basename "$1")"
SRC="${2:-$(cd "$(dirname "$0")/.." && pwd)}"

WORK=$(mktemp -d)
trap 'rm -rf "$WORK"; [ -n "${SRV_PID:-}" ] && kill "$SRV_PID" 2>/dev/null' EXIT
FIX="$WORK/fixtures"; mkdir -p "$FIX"
cp -r "$SRC/web/dist" "$WORK/web"

D1=$(date -u -d "1 day ago" +%Y-%m-%dT%H:%M:%SZ)
D2=$(date -u -d "2 days ago" +%Y-%m-%dT%H:%M:%SZ)
D4=$(date -u -d "4 days ago" +%Y-%m-%dT%H:%M:%SZ)
cat > "$FIX/prs.json" <<EOF
[{"number":42,"title":"Fix the widget","state":"closed","created_at":"$D4","updated_at":"$D1","merged_at":"$D1","html_url":"u","user":{"login":"alice"},"labels":[{"name":"bug"}]}]
EOF
printf '[]' > "$FIX/issues.json"
printf '[]' > "$FIX/releases.json"
cat > "$FIX/commits.json" <<EOF
[{"sha":"aaaa1111bbbb","html_url":"c1","commit":{"message":"fix the widget","author":{"name":"alice","date":"$D1"}}}]
EOF
printf '[{"login":"alice","contributions":9}]' > "$FIX/contributors.json"
printf '{"C":61000,"Go":39000}' > "$FIX/languages.json"
printf '{"stargazers_count":1234,"forks_count":56,"description":"demo","language":"C","default_branch":"main","license":{"spdx_id":"MIT"}}' > "$FIX/meta.json"

PORT=$(awk 'BEGIN{srand();print 22000+int(rand()*2000)}')
BASE="http://127.0.0.1:$PORT"
GS_FIXTURE_DIR="$FIX" NO_COLOR=1 "$GS" serve --port "$PORT" --config "$WORK/c.json" --web-root "$WORK/web" \
  > "$WORK/log" 2>&1 &
SRV_PID=$!
for _ in $(seq 1 60); do curl -s -o /dev/null "$BASE/api/health" 2>/dev/null && break; sleep 0.1; done

FAIL=0
note() { echo "$1"; }
fail() { echo "FAIL: $1" >&2; FAIL=1; }

# The SPA entry, then the module graph the browser would request from it.
INDEX=$(curl -s "$BASE/")
echo "$INDEX" | grep -q 'id="root"' || fail "no #root mount point"
MAIN=$(echo "$INDEX" | sed -n 's/.*src="\([^"]*\.js\)".*/\1/p')
[ -n "$MAIN" ] || fail "no module entry in index.html"
curl -s -o "$WORK/main.js" -w '%{http_code}' "$BASE$MAIN" | grep -q 200 || fail "entry module not served"
grep -q 'createRoot' "$WORK/main.js" || fail "entry module does not look like the app"

# Every route the sidebar advertises must resolve through the SPA fallback.
for route in '' analyze compare batch subscriptions usage llm settings completion; do
  code=$(curl -s -o /dev/null -w '%{http_code}' "$BASE/$route")
  [ "$code" = "200" ] || fail "route /$route returned $code"
done

# The endpoints each page loads on mount.
for ep in /api/health /api/meta/options /api/meta/i18n /api/quota /api/cache /api/config \
          /api/subscriptions /api/llms "/api/usage?days=14" /api/jobs; do
  body=$(curl -s "$BASE$ep")
  printf '%s' "$body" | head -c 1 | grep -q '[{a-zA-Z]' || fail "$ep returned no payload"
done
# The completion scripts are plain text, so only check that they are non-empty
# and mention the binary they complete.
for sh in bash zsh fish; do
  script=$(curl -s "$BASE/api/completion/$sh")
  printf '%s' "$script" | grep -q 'gs' || fail "/api/completion/$sh looks empty"
done

# A full analyze, as the page would drive it, with SSE progress observed live.
JOB=$(curl -s -X POST -H 'Content-Type: application/json' \
  -d '{"repo":"acme/gadget","since_days":7,"no_cache":true}' \
  "$BASE/api/jobs/analyze" | sed -n 's/.*"id":"\([^"]*\)".*/\1/p')
[ -n "$JOB" ] || fail "analysis submission returned no job id"

STREAM=$(timeout 25 curl -sN "$BASE/api/jobs/$JOB/events" 2>/dev/null || true)
for ev in hello progress done; do
  echo "$STREAM" | grep -q "event: $ev" || fail "SSE missing the $ev event"
done

RESULT=$(curl -s "$BASE/api/jobs/$JOB/result")
echo "$RESULT" | grep -q '"Fix the widget"' || fail "report is missing its data"
echo "$RESULT" | grep -q '"top_author":"alice (1 commits)"' || fail "top_author shape changed"

# A clean run must say so explicitly rather than omitting the key.
echo "$RESULT" | grep -q '"warnings":\[\]' || fail "a clean run did not report an empty warnings array"

PDF=$(curl -s "$BASE/api/jobs/$JOB/export?format=pdf")
echo "$PDF" | head -c 4 | grep -q '%PDF' || fail "pdf export is not a PDF"

if [ "$FAIL" -eq 0 ]; then
  echo "smoke: OK"
  note "  version:  $(curl -s "$BASE/api/health" | sed -n 's/.*"version":"\([^"]*\)".*/\1/p')"
  note "  console:  $BASE"
  note "  job:      $JOB"
  note "  sse:      $(echo "$STREAM" | grep -c '^event:') events"
fi
exit "$FAIL"