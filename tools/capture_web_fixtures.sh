#!/usr/bin/env bash
# Regenerates the web console's server fixtures from a real daemon run.
#
# The fixtures under web/src/test/fixtures are what the page tests render
# against, so they have to match what the daemon actually sends. This script
# boots the daemon against the same offline fixtures the server test uses and
# captures the two report-shaped responses the console reads.
#
# Usage: bash tools/capture_web_fixtures.sh [path/to/gs]

set -eu

GS="${1:-./build/gs}"
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
OUT="$ROOT/web/src/test/fixtures"
WORK="$(mktemp -d)"
trap 'rm -rf "$WORK"' EXIT
FIX="$WORK/fixtures"
mkdir -p "$FIX"

iso_days_ago() { date -u -d "$1 days ago" +%Y-%m-%dT%H:%M:%SZ 2>/dev/null \
                  || date -u -v-"$1"d +%Y-%m-%dT%H:%M:%SZ; }
D1=$(iso_days_ago 1)
D2=$(iso_days_ago 2)
D4=$(iso_days_ago 4)

cat > "$FIX/prs.json" <<EOF
[
  {"number": 42, "title": "Fix the widget", "state": "closed", "created_at": "$D4", "updated_at": "$D1", "merged_at": "$D1", "html_url": "https://example.test/p/42", "user": {"login": "alice"}, "labels": [{"name": "bug"}, {"name": "ui"}]},
  {"number": 41, "title": "Add docs", "state": "open", "created_at": "$D4", "updated_at": "$D2", "merged_at": null, "html_url": "https://example.test/p/41", "user": {"login": "bob"}, "labels": []}
]
EOF
cat > "$FIX/commits.json" <<EOF
[
  {"sha": "aaaa1111bbbb2222", "html_url": "https://example.test/c/1", "commit": {"message": "fix the widget\n\nlong body", "author": {"name": "alice", "date": "$D1"}}},
  {"sha": "cccc2222dddd3333", "html_url": "https://example.test/c/2", "commit": {"message": "add docs", "author": {"name": "bob", "date": "$D2"}}}
]
EOF
cat > "$FIX/issues.json" <<EOF
[{"number": 99, "title": "Something broke", "state": "closed", "created_at": "$D4", "updated_at": "$D1", "closed_at": "$D1", "html_url": "https://example.test/i/99", "user": {"login": "bob"}, "labels": []}]
EOF
cat > "$FIX/releases.json" <<EOF
[{"tag_name": "v1.2.3", "name": "Release 1.2.3", "published_at": "$D2", "html_url": "https://example.test/r", "author": {"login": "alice"}, "body": "release notes", "draft": false}]
EOF
cat > "$FIX/contributors.json" <<EOF
[{"login": "alice", "contributions": 9}, {"login": "bob", "contributions": 3}]
EOF
echo '{"C": 61000, "Go": 39000}' > "$FIX/languages.json"
echo '{"stargazers_count": 1234, "forks_count": 56, "description": "demo repo", "language": "C", "default_branch": "main", "license": {"spdx_id": "MIT"}}' > "$FIX/meta.json"

PORT=$((19000 + RANDOM % 900))
BASE="http://127.0.0.1:$PORT"
export GS_FIXTURE_DIR="$FIX"
export GS_CACHE_DIR="$WORK/cache"
export XDG_CONFIG_HOME="$WORK/xdg"
export NO_COLOR=1
mkdir -p "$XDG_CONFIG_HOME/gitsight"

"$GS" serve --port "$PORT" --config "$WORK/config.json" --web-root "$WORK/web" \
    >"$WORK/server.log" 2>&1 &
SERVER=$!
trap 'kill $SERVER 2>/dev/null || true; rm -rf "$WORK"' EXIT

for _ in $(seq 1 60); do
  curl -sf "$BASE/api/health" >/dev/null 2>&1 && break
  sleep 0.2
done

# --- one repository ---------------------------------------------------------
JOB=$(curl -s -X POST -H 'Content-Type: application/json' \
  -d '{"repo":"acme/gadget","since_days":7}' "$BASE/api/jobs/analyze" \
  | sed -n 's/.*"id":"\([^"]*\)".*/\1/p')
for _ in $(seq 1 100); do
  BODY=$(curl -s "$BASE/api/jobs/$JOB/result")
  case "$BODY" in *'"pull_requests"'*) break;; esac
  sleep 0.1
done
printf '%s\n' "$BODY" > "$OUT/report.json"

# --- two windows ------------------------------------------------------------
CJOB=$(curl -s -X POST -H 'Content-Type: application/json' \
  -d '{"repo":"acme/gadget","days_a":7,"days_b":30}' "$BASE/api/jobs/compare" \
  | sed -n 's/.*"id":"\([^"]*\)".*/\1/p')
for _ in $(seq 1 100); do
  CBODY=$(curl -s "$BASE/api/jobs/$CJOB/result")
  case "$CBODY" in *'"report_a"'*) break;; esac
  sleep 0.1
done
printf '%s\n' "$CBODY" > "$OUT/compare.json"

# The report timestamps are relative to the moment of the capture, so the day
# is recorded next to them: a test that measures a window has to pin the clock
# to it, or it starts failing a week after the capture.
printf '{"iso":"%s"}\n' "$(date -u +%Y-%m-%dT%H:%M:%SZ)" > "$OUT/captured-at.json"

echo "wrote $OUT/report.json, $OUT/compare.json and $OUT/captured-at.json"
