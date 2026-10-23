#!/usr/bin/env bash
# End-to-end check of the console the way a browser drives it: load the SPA,
# then walk the full analyze -> SSE progress -> report -> download flow against
# a real daemon, using the same endpoints the browser issues.
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
FIX="$WORK/fixtures"
mkdir -p "$FIX"
cp -r "$SRC/web/dist" "$WORK/web"

. "$(dirname "$0")/common.sh"

D1=$(iso_days_ago 1); D2=$(iso_days_ago 2); D4=$(iso_days_ago 4)

cat > "$FIX/prs.json" <<EOF
[{"number": 42, "title": "Fix the widget", "state": "closed", "created_at": "$D4", "updated_at": "$D1", "merged_at": "$D1", "html_url": "https://example.test/p/42", "user": {"login": "alice"}, "labels": [{"name": "bug"}, {"name": "ui"}]},
 {"number": 41, "title": "Add docs", "state": "open", "created_at": "$D4", "updated_at": "$D2", "merged_at": null, "html_url": "https://example.test/p/41", "user": {"login": "bob"}, "labels": []}]
EOF
cat > "$FIX/commits.json" <<EOF
[{"sha": "aaaa1111bbbb2222", "html_url": "https://example.test/c/1", "commit": {"message": "fix the widget\n\nlong body", "author": {"name": "alice", "date": "$D1"}}},
 {"sha": "cccc2222dddd3333", "html_url": "https://example.test/c/2", "commit": {"message": "add docs", "author": {"name": "bob", "date": "$D2"}}}]
EOF
cat > "$FIX/issues.json" <<EOF
[{"number": 99, "title": "Something broke", "state": "closed", "created_at": "$D4", "updated_at": "$D1", "closed_at": "$D1", "html_url": "https://example.test/i/99", "user": {"login": "bob"}, "labels": []}]
EOF
cat > "$FIX/releases.json" <<EOF
[{"tag_name": "v1.2.3", "name": "Release 1.2.3", "published_at": "$D2", "html_url": "https://example.test/r", "author": {"login": "alice"}, "body": "notes", "draft": false}]
EOF
cat > "$FIX/contributors.json" <<EOF
[{"login": "alice", "contributions": 9}, {"login": "bob", "contributions": 3}]
EOF
echo '{"C": 61000, "Go": 39000}' > "$FIX/languages.json"
echo '{"stargazers_count": 1234, "forks_count": 56, "description": "demo repo", "language": "C", "default_branch": "main", "license": {"spdx_id": "MIT"}}' > "$FIX/meta.json"

PORT=$(awk 'BEGIN{srand();print 21000+int(rand()*2000)}')
BASE="http://127.0.0.1:$PORT"
CFG="$WORK/config.json"

GS_FIXTURE_DIR="$FIX" NO_COLOR=1 "$GS" serve --port "$PORT" --config "$CFG" --web-root "$WORK/web" \
  > "$WORK/server.log" 2>&1 &
SRV_PID=$!

for _ in $(seq 1 60); do
  curl -s -o /dev/null "$BASE/api/health" 2>/dev/null && break
  sleep 0.1
done
curl -s -o /dev/null "$BASE/api/health" 2>/dev/null || {
  echo "FAIL: server did not start" >&2; cat "$WORK/server.log" >&2; exit 1;
}

# --- 1. the console boots from the SPA shell -------------------------------
curl -s "$BASE/" | grep -q 'id="root"' && ok || fail "SPA shell not served"

# --- 2. the metadata the UI builds its forms from ---------------------------
META=$(curl -s "$BASE/api/meta/options")
for key in '"platforms"' '"formats"' '"languages"' '"llm_modes"' '"named_colors"' '"batch_max_threads"'; do
  echo "$META" | grep -qF "$key" && ok || fail "meta/options missing $key"
done
# 20 palette entries, matching NAMED_COLORS in cmd_common.c.
COUNT=$(echo "$META" | grep -o '"hex"' | wc -l)
[ "$COUNT" -eq 20 ] && ok || fail "expected 20 named colors, got $COUNT"

# --- 3. the analyze page's default request shape is accepted ---------------
SUBMIT=$(curl -s -X POST -H 'Content-Type: application/json' \
  -d '{"repo":"acme/gadget","since_days":7,"platform":"github","max_pages":20,"no_cache":false,"llm_mode":"health","lang":"en"}' \
  "$BASE/api/jobs/analyze")
JOB=$(printf '%s' "$SUBMIT" | sed -n 's/.*"id":"\([^"]*\)".*/\1/p')
[ -n "$JOB" ] && ok || fail "could not submit an analysis: $SUBMIT"

# --- 4. progress streams over SSE, as the page subscribes ------------------
SSE=""
for _ in $(seq 1 5); do
  if [ -z "$SSE" ]; then
    SSE=$(timeout 30 curl -sN "$BASE/api/jobs/$JOB/events" 2>/dev/null || true)
  else
    break
  fi
done
echo "$SSE" | grep -q 'event: hello' && ok || fail "no SSE hello frame"
echo "$SSE" | grep -q 'event: progress' && ok || fail "no SSE progress frames"
echo "$SSE" | grep -q 'Fetching' && ok || fail "no fetch stage in the stream"
echo "$SSE" | grep -q 'event: done' && ok || fail "no terminal SSE frame"

# --- 5. the report payload the view renders --------------------------------
RESULT=$(curl -s "$BASE/api/jobs/$JOB/result")
for key in '"pull_requests"' '"commits"' '"issues"' '"releases"' '"contributors"' '"languages"' '"repository"' '"activity"' '"top_labels"' '"summary"'; do
  echo "$RESULT" | grep -qF "$key" && ok || fail "report missing $key"
done
# The states the badge component maps to must all be present.
echo "$RESULT" | grep -q '"state":"merged"' && ok || fail "no merged PR in the fixture report"
echo "$RESULT" | grep -q '"state":"open"' && ok || fail "no open PR in the fixture report"
# Semantics the console depends on.
CLOSE=$(echo "$RESULT" | grep -o '"closed":[0-9]*' | head -1 | cut -d: -f2)
MERGED=$(echo "$RESULT" | grep -o '"merged":[0-9]*' | head -1 | cut -d: -f2)
[ "$CLOSE" -ge "$MERGED" ] && ok || fail "closed ($CLOSE) should include merged ($MERGED)"
echo "$RESULT" | grep -q '"top_author":"alice (1 commits)"' && ok || fail "top_author format changed"
echo "$RESULT" | grep -q '"top_list":"alice (9), bob (3)"' && ok || fail "top_list format changed"
echo "$RESULT" | grep -q '"percent":61' && ok || fail "language percentage wrong"
# The browser relabels the window and the weekdays from these numbers rather than
# replaying the strings above, which are frozen in the analysis language.
echo "$RESULT" | grep -q '"period_days":7' && ok || fail "report is missing period_days"
# The weekday series has to be a populated array: it used to be added with
# cJSON_AddItemToObject, which left the key present but null.
echo "$RESULT" | grep -q '"weekday":\[{"day":"Sun","index":0,"commits":[0-9]' && ok \
  || fail "weekday series is not a populated array with indices"

# --- 6. every export renders from the stored analysis ----------------------
for pair in "json:application/json" "md:text/markdown" "txt:text/plain" \
            "html:text/html" "csv:text/csv" "xlsx:spreadsheetml" "pdf:application/pdf"; do
  f=${pair%%:*}; mime=${pair#*:}
  CT=$(curl -s -o "$WORK/out.$f" -w '%{content_type}' "$BASE/api/jobs/$JOB/export?format=$f")
  case "$CT" in *"$mime"*) ok;; *) fail "export $f: wrong content type $CT";; esac
  [ -s "$WORK/out.$f" ] && ok || fail "export $f was empty"
done
head -c 4 "$WORK/out.pdf" | grep -q '%PDF' && ok || fail "pdf magic missing"
head -c 2 "$WORK/out.xlsx" | grep -q 'PK' && ok || fail "xlsx magic missing"
# The HTML export is the layout the React view mirrors.
grep -q 'href="#languages"' "$WORK/out.html" && ok || fail "html export lost its table of contents"
grep -q 'b-merged' "$WORK/out.html" && ok || fail "html export lost its state badges"

# --- 7. settings the console writes are read back --------------------------
curl -s -X PATCH -H 'Content-Type: application/json' \
  -d '{"sets":{"export.item_limit":3,"default_days":14}}' "$BASE/api/config" > /dev/null
CFG_AFTER=$(curl -s "$BASE/api/config")
echo "$CFG_AFTER" | grep -q '"item_limit":3' && ok || fail "item_limit not persisted"
echo "$CFG_AFTER" | grep -q '"default_days":14' && ok || fail "default_days not persisted"
# The item cap only affects the rendered formats, never json or csv.
grep -q 'Add docs' "$WORK/out.json" && ok || fail "json export dropped rows"

echo
echo "console e2e tests: $PASS passed, $FAIL failed"
[ "$FAIL" -eq 0 ]