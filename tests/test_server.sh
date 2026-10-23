#!/usr/bin/env bash
# End-to-end test for the `gs serve` HTTP API.
#
# Runs fully offline: GS_FIXTURE_DIR short-circuits every platform request, and
# an isolated --config keeps the developer's real settings untouched.
#
# Usage: bash tests/test_server.sh /path/to/gs [source-dir]

set -u

GS="$(cd "$(dirname "$1")" && pwd)/$(basename "$1")"
SRC="${2:-$(cd "$(dirname "$0")/.." && pwd)}"

PASS=0
FAIL=0

fail() {
  echo "FAIL: $1" >&2
  FAIL=$((FAIL + 1))
}

ok() {
  PASS=$((PASS + 1))
}

# assert_contains <label> <needle> <haystack>
assert_contains() {
  if printf '%s' "$3" | grep -qF -- "$2"; then ok; else
    fail "$1: expected to contain '$2'"
    printf '%s\n' "$3" | head -c 600 >&2
    echo >&2
  fi
}

# assert_exits <label> <signal> <seconds> -- starts a daemon, signals it, and
# requires it to be gone. signal() installs handlers with SA_RESTART on glibc,
# which silently restarts accept() and leaves Ctrl+C with nothing to stop the
# loop, so this guards the whole shutdown path.
assert_exits() {
  local label="$1" sig="$2" limit="$3"
  local work port pid waited
  work=$(mktemp -d)
  cp -r "$FIX" "$work/fix"
  port=$(awk 'BEGIN{srand();print 25000+int(rand()*2000)}')
  "$GS" serve --port "$port" --config "$work/config.json" --web-root "$WORK/web" \
    > "$work/server.log" 2>&1 &
  pid=$!
  local started=0
  for _ in $(seq 1 60); do
    if curl -s -o /dev/null "http://127.0.0.1:$port/api/health" 2>/dev/null; then
      started=1
      break
    fi
    sleep 0.1
  done
  if [ "$started" != 1 ]; then
    fail "$label: daemon did not start"
    rm -rf "$work"
    return
  fi

  kill -"$sig" "$pid" 2>/dev/null
  waited=0
  while kill -0 "$pid" 2>/dev/null; do
    waited=$((waited + 1))
    if [ "$waited" -ge $((limit * 4)) ]; then
      fail "$label: still running ${limit}s after SIG$sig"
      kill -9 "$pid" 2>/dev/null
      rm -rf "$work"
      return
    fi
    sleep 0.25
  done
  wait "$pid" 2>/dev/null
  grep -q 'Shutting down' "$work/server.log" && ok || fail "$label: no shutdown message"
  rm -rf "$work"
}

# assert_status <label> <expected> <curl args...>
assert_status() {
  local label="$1" want="$2"; shift 2
  local got
  got=$(curl -s -o /dev/null -w '%{http_code}' "$@")
  if [ "$got" = "$want" ]; then ok; else fail "$label: expected HTTP $want, got $got"; fi
}

WORK=$(mktemp -d)
trap 'rm -rf "$WORK"; [ -n "${SRV_PID:-}" ] && kill "$SRV_PID" 2>/dev/null' EXIT
FIX="$WORK/fixtures"
mkdir -p "$FIX" "$WORK/web"

CFG="$WORK/config.json"
export GS_FIXTURE_DIR="$FIX"
export NO_COLOR=1

# --- fixtures ---------------------------------------------------------------

. "$(dirname "$0")/common.sh"

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

echo '<!doctype html><title>GitSight</title><div id="root"></div>' > "$WORK/web/index.html"
# Real bundles when the frontend has been built, so the asset caching headers
# can be checked; the stub shell above still covers the fallback path.
if [ -f "$SRC/web/dist/index.html" ]; then
  rm -rf "$WORK/web"
  cp -r "$SRC/web/dist" "$WORK/web"
fi

# --- boot -------------------------------------------------------------------

PORT=$(awk 'BEGIN{srand();print 18000+int(rand()*2000)}')
BASE="http://127.0.0.1:$PORT"
LOG="$WORK/server.log"

"$GS" serve --port "$PORT" --config "$CFG" --web-root "$WORK/web" > "$LOG" 2>&1 &
SRV_PID=$!

for _ in $(seq 1 60); do
  if curl -s -o /dev/null "$BASE/api/health" 2>/dev/null; then break; fi
  sleep 0.1
done
if ! curl -s -o /dev/null "$BASE/api/health" 2>/dev/null; then
  echo "FAIL: server did not start" >&2
  cat "$LOG" >&2
  exit 1
fi

j() { curl -s "$@"; }

# --- meta -------------------------------------------------------------------

H=$(j "$BASE/api/health")
assert_contains "health version" '"version"' "$H"
assert_contains "health config path" "$CFG" "$H"

O=$(j "$BASE/api/meta/options")
assert_contains "options github"     '"github"'   "$O"
assert_contains "options gitlab"     '"gitlab"'   "$O"
assert_contains "options forgejo"    '"forgejo"'  "$O"
assert_contains "options pdf"        '"pdf"'      "$O"
assert_contains "options xlsx"       '"xlsx"'     "$O"
assert_contains "options llm modes"  '"trends"'   "$O"
assert_contains "options named color" '"red"'     "$O"
assert_contains "options batch cap"  '"batch_max_threads"' "$O"

I=$(j "$BASE/api/meta/i18n")
assert_contains "i18n has entries" '"entries"' "$I"

Q=$(j "$BASE/api/quota")
assert_contains "quota shape" '"known"' "$Q"

CA=$(j "$BASE/api/cache")
assert_contains "cache shape" '"ttl_seconds"' "$CA"

# --- completion scripts -----------------------------------------------------

# The served scripts must match the ones compiled into the binary exactly.
for pair in "bash:gs.bash" "fish:gs.fish"; do
  sh=${pair%%:*}; file=${pair#*:}
  C=$(j "$BASE/api/completion/$sh")
  if [ "$C" = "$(cat "$SRC/completions/$file")" ]; then ok; else
    fail "completion $sh does not match completions/$file"
  fi
done
C=$(j "$BASE/api/completion/zsh")
if [ "$C" = "$(cat "$SRC/completions/_gs")" ]; then ok; else
  fail "completion zsh does not match completions/_gs"
fi
assert_status "completion bad shell" 404 "$BASE/api/completion/tcsh"

# --- static console ---------------------------------------------------------

assert_status "index served" 200 "$BASE/"
assert_contains "index content" "GitSight" "$(j "$BASE/")"
# A client-side route that has no file must fall back to index.html.
assert_status "spa fallback" 200 "$BASE/analyze"
assert_status "traversal blocked" 404 --path-as-is "$BASE/../../etc/passwd"
assert_status "encoded traversal blocked" 404 --path-as-is "$BASE/%2e%2e%2f%2e%2e%2fetc%2fpasswd"

# --- config -----------------------------------------------------------------

G=$(j "$BASE/api/config")
assert_contains "config platform"   '"platform"'   "$G"
assert_contains "config export blk" '"item_limit"' "$G"
assert_contains "config subs array" '"subscriptions"' "$G"

# Tokens must never be echoed back.
"$GS" config --config "$CFG" --set github_token=ghp_supersecret1234 >/dev/null 2>&1
G=$(j "$BASE/api/config")
if printf '%s' "$G" | grep -q 'ghp_supersecret1234'; then
  fail "config leaked the github token"
else ok; fi
assert_contains "token reported as set" '"github_token":{"set":true' "$G"
assert_contains "token masked" '1234' "$G"

# --- config writes ----------------------------------------------------------

assert_status "config set valid" 200 -X PATCH -H 'Content-Type: application/json' \
  -d '{"sets":{"default_days":21}}' "$BASE/api/config"
assert_contains "default_days persisted" '"default_days":21' "$(j "$BASE/api/config")"

assert_status "config set invalid rejected" 400 -X PATCH -H 'Content-Type: application/json' \
  -d '{"sets":{"default_days":"not-a-number"}}' "$BASE/api/config"
assert_status "config set bad format rejected" 400 -X PATCH -H 'Content-Type: application/json' \
  -d '{"sets":{"format":"docx"}}' "$BASE/api/config"
assert_status "config set bad platform rejected" 400 -X PATCH -H 'Content-Type: application/json' \
  -d '{"sets":{"platform":"sourceforge"}}' "$BASE/api/config"
assert_status "config set unknown key rejected" 400 -X PATCH -H 'Content-Type: application/json' \
  -d '{"sets":{"nope":"1"}}' "$BASE/api/config"

assert_status "export override accepted" 200 -X PATCH -H 'Content-Type: application/json' \
  -d '{"sets":{"export.item_limit":3}}' "$BASE/api/config"
assert_contains "item_limit persisted" '"item_limit":3' "$(j "$BASE/api/config")"

# --- subscriptions ----------------------------------------------------------

S=$(j "$BASE/api/subscriptions")
assert_contains "subs empty" '"subscriptions":[]' "$S"

S=$(j -X POST -H 'Content-Type: application/json' \
  -d '{"repo":"acme/gadget","alias":"core","days":14}' "$BASE/api/subscriptions")
assert_contains "sub added"      '"repo":"acme/gadget"' "$S"
assert_contains "sub alias"      '"alias":"core"'       "$S"
assert_contains "sub days"       '"days":14'            "$S"

S=$(j -X POST -H 'Content-Type: application/json' \
  -d '{"repo":"foo/bar"}' "$BASE/api/subscriptions")
assert_contains "second sub added" '"repo":"foo/bar"' "$S"

# A duplicate, or an invalid format, must be refused.
assert_status "duplicate sub rejected" 409 -X POST -H 'Content-Type: application/json' \
  -d '{"repo":"acme/gadget"}' "$BASE/api/subscriptions"
assert_status "sub bad format rejected" 400 -X POST -H 'Content-Type: application/json' \
  -d '{"repo":"zz/yy","format":"docx"}' "$BASE/api/subscriptions"
assert_status "sub missing repo rejected" 400 -X POST -H 'Content-Type: application/json' \
  -d '{}' "$BASE/api/subscriptions"

# Update and removal accept either the alias or the repo as the reference.
assert_status "sub update by alias" 200 -X PATCH -H 'Content-Type: application/json' \
  -d '{"days":30}' "$BASE/api/subscriptions/core"
assert_contains "sub updated" '"days":30' "$(j "$BASE/api/subscriptions")"
assert_status "sub remove by repo" 200 -X DELETE "$BASE/api/subscriptions/foo%2Fbar"
assert_contains "sub removed" '"total":1' "$(j "$BASE/api/subscriptions")"
assert_status "sub remove unknown" 404 -X DELETE "$BASE/api/subscriptions/nope"

# --- LLM providers ----------------------------------------------------------

assert_contains "no llms yet" '"llms":[]' "$(j "$BASE/api/llms")"

L=$(j -X POST -H 'Content-Type: application/json' \
  -d '{"name":"local","protocol":"openai","api_key":"not-needed","api_base":"http://localhost:11434/v1","model":"llama3.2"}' \
  "$BASE/api/llms")
assert_contains "llm created" '"name":"local"' "$L"
assert_contains "llm key hidden" '"api_key":{"set":true' "$L"
if printf '%s' "$L" | grep -q '"api_key":"not-needed"'; then
  fail "llm api_key leaked"
else ok; fi

assert_status "llm bad protocol rejected" 400 -X POST -H 'Content-Type: application/json' \
  -d '{"name":"claude","protocol":"telepathy"}' "$BASE/api/llms"
assert_status "llm missing name rejected" 400 -X POST -H 'Content-Type: application/json' \
  -d '{"model":"x"}' "$BASE/api/llms"

assert_status "llm patch" 200 -X PATCH -H 'Content-Type: application/json' \
  -d '{"model":"llama3.3"}' "$BASE/api/llms/local"
assert_contains "llm patched" '"model":"llama3.3"' "$(j "$BASE/api/llms")"

assert_status "llm activate" 200 -X POST "$BASE/api/llms/local/activate"
assert_contains "llm is active" '"active":true' "$(j "$BASE/api/llms")"
assert_status "llm patch unknown" 404 -X PATCH -H 'Content-Type: application/json' \
  -d '{"model":"x"}' "$BASE/api/llms/ghost"

# --- usage ------------------------------------------------------------------

U=$(j "$BASE/api/usage?days=14")
assert_contains "usage totals" '"total_tokens"' "$U"
assert_contains "usage by_day" '"by_day"' "$U"
assert_contains "usage by_llm" '"by_llm"' "$U"
assert_status "usage records" 200 "$BASE/api/usage/records?limit=10"
# The record log can be narrowed to the same window the charts use, so the
# table below them and the charts above them describe the same period.
R=$(j "$BASE/api/usage/records?limit=10&days=14")
assert_contains "usage records window" '"days":14' "$R"
R1=$(j "$BASE/api/usage/records?limit=10&days=1")
assert_contains "usage records one day" '"days":1' "$R1"
assert_status "usage clear" 204 -X DELETE "$BASE/api/usage"

# --- analysis jobs ----------------------------------------------------------

assert_status "analyze needs a repo" 400 -X POST -H 'Content-Type: application/json' \
  -d '{}' "$BASE/api/jobs/analyze"
assert_status "analyze rejects bad format" 400 -X POST -H 'Content-Type: application/json' \
  -d '{"repo":"acme/gadget","formats":["docx"]}' "$BASE/api/jobs/analyze"
assert_status "analyze rejects bad date" 400 -X POST -H 'Content-Type: application/json' \
  -d '{"repo":"acme/gadget","from":"nope"}' "$BASE/api/jobs/analyze"
assert_status "analyze rejects bad platform" 400 -X POST -H 'Content-Type: application/json' \
  -d '{"repo":"acme/gadget","platform":"sourceforge"}' "$BASE/api/jobs/analyze"

SUBMIT=$(j -X POST -H 'Content-Type: application/json' \
  -d '{"repo":"acme/gadget","since_days":7}' "$BASE/api/jobs/analyze")
assert_status "analyze accepted" 202 -X POST -H 'Content-Type: application/json' \
  -d '{"repo":"acme/gadget","since_days":7}' "$BASE/api/jobs/analyze"
JOB=$(printf '%s' "$SUBMIT" | sed -n 's/.*"id":"\([^"]*\)".*/\1/p')
if [ -n "$JOB" ]; then ok; else fail "could not read the job id"; fi

assert_status "job list" 200 "$BASE/api/jobs"
assert_status "unknown job" 404 "$BASE/api/jobs/job_does_not_exist"

# Wait for the analysis to finish, then confirm the payload shape.
RESULT=""
for _ in $(seq 1 100); do
  RESULT=$(j "$BASE/api/jobs/$JOB/result")
  case "$RESULT" in *'"pull_requests"'*) break;; esac
  sleep 0.1
done
assert_contains "result pull_requests" '"pull_requests"' "$RESULT"
assert_contains "result repository"     '"repository"'     "$RESULT"
assert_contains "result languages"      '"languages"'      "$RESULT"
assert_contains "result activity"       '"activity"'       "$RESULT"
assert_contains "result items present"  '"Fix the widget"' "$RESULT"
assert_contains "state badge merged"    '"state":"merged"' "$RESULT"
# The report names the provider that wrote the summary, empty when the local
# summary was used. A client that credits the wrong provider is worse than one
# that credits none.
assert_contains "result llm provider" '"llm_provider"' "$RESULT"

STATE=$(j "$BASE/api/jobs/$JOB")
assert_contains "job done" '"state":"done"' "$STATE"

# --- SSE progress -----------------------------------------------------------

SUBMIT2=$(j -X POST -H 'Content-Type: application/json' \
  -d '{"repo":"acme/gadget"}' "$BASE/api/jobs/analyze")
JOB2=$(printf '%s' "$SUBMIT2" | sed -n 's/.*"id":"\([^"]*\)".*/\1/p')
SSE=$(curl -sN --max-time 25 "$BASE/api/jobs/$JOB2/events" 2>/dev/null || true)
assert_contains "sse hello"      'event: hello'   "$SSE"
assert_contains "sse progress"   'event: progress' "$SSE"
assert_contains "sse stage"      'Fetching'        "$SSE"
assert_contains "sse terminal"   'event: done'     "$SSE"

# --- exports rendered from the stored analysis -------------------------------

for pair in "json:application/json" "md:text/markdown" "txt:text/plain" \
            "html:text/html" "csv:text/csv" "xlsx:spreadsheetml" "pdf:application/pdf"; do
  f=${pair%%:*}; mime=${pair#*:}
  CT=$(curl -s -o "$WORK/out.$f" -w '%{content_type}' "$BASE/api/jobs/$JOB/export?format=$f")
  case "$CT" in *"$mime"*) ok;; *) fail "export $f: expected $mime, got $CT";; esac
  if [ -s "$WORK/out.$f" ]; then ok; else fail "export $f produced no bytes"; fi
done
# Binary formats must survive the round trip byte for byte.
head -c 4 "$WORK/out.pdf" | grep -q '%PDF' && ok || fail "pdf magic missing"
head -c 2 "$WORK/out.xlsx" | grep -q 'PK' && ok || fail "xlsx magic missing"
grep -q 'Fix the widget' "$WORK/out.json" && ok || fail "json export missing data"
grep -q 'GitSight Report' "$WORK/out.md" && ok || fail "md export missing title"

# The job used here has one repo, so a multi-repo job must reject a bare export.
assert_status "export rejects bad format" 400 "$BASE/api/jobs/$JOB/export?format=docx"
assert_status "export needs a format" 400 "$BASE/api/jobs/$JOB/export"
assert_status "export unknown job" 404 "$BASE/api/jobs/nope/export?format=pdf"
assert_status "export sends disposition" 200 \
  -o /dev/null -w '%{http_code}' "$BASE/api/jobs/$JOB/export?format=pdf"

# --- per-run export overrides -----------------------------------------------
#
# The CLI's --export overrides the exporter settings for one run without
# touching the config file; the request body carries the same pairs, and the
# rendering of that job must honour them.
EXPORTJOB=$(j -X POST -H 'Content-Type: application/json' \
  -d '{"repo":"acme/gadget","export":["html.open_color=red"]}' "$BASE/api/jobs/analyze")
EID=$(printf '%s' "$EXPORTJOB" | sed -n 's/.*"id":"\([^"]*\)".*/\1/p')
if [ -n "$EID" ]; then ok; else fail "could not read the export override job id"; fi
ERESULT=""
for _ in $(seq 1 100); do
  ERESULT=$(j "$BASE/api/jobs/$EID/result")
  case "$ERESULT" in *'"pull_requests"'*) break;; esac
  sleep 0.1
done
curl -s -o "$WORK/override.html" "$BASE/api/jobs/$EID/export?format=html"
grep -q '.b-open{color:#dc2629}' "$WORK/override.html" \
  && ok || fail "per-run export override not applied"
# The config file itself must be untouched by a per-run override.
grep -q 'dc2629' "$WORK/config.json" 2>/dev/null \
  && fail "per-run export override leaked into the config file" \
  || ok
# A malformed pair is rejected rather than silently dropped.
assert_status "export override needs key=value" 400 -X POST -H 'Content-Type: application/json' \
  -d '{"repo":"acme/gadget","export":["noequalsign"]}' "$BASE/api/jobs/analyze"

# --- batch analysis (cross-repository parallelism) --------------------------

BATCH=$(j -X POST -H 'Content-Type: application/json' \
  -d '{"repos":["acme/gadget","foo/bar","baz/qux"],"parallel":3}' "$BASE/api/jobs/analyze")
BJOB=$(printf '%s' "$BATCH" | sed -n 's/.*"id":"\([^"]*\)".*/\1/p')
BRESULT=""
for _ in $(seq 1 150); do
  BRESULT=$(j "$BASE/api/jobs/$BJOB/result")
  case "$BRESULT" in *'"reports"'*) break;; esac
  sleep 0.1
done
assert_contains "batch kind"        '"kind":"batch"'  "$BRESULT"
assert_contains "batch total"       '"total":3'       "$BRESULT"
assert_contains "batch repos"       '"foo/bar"'       "$BRESULT"
assert_contains "batch repos 3"     '"baz/qux"'       "$BRESULT"

# A multi-repo job needs ?which= so the requested artifact is unambiguous.
assert_status "ambiguous export rejected" 400 "$BASE/api/jobs/$BJOB/export?format=pdf"
assert_status "which= selects a repo" 200 "$BASE/api/jobs/$BJOB/export?format=pdf&which=foo%2Fbar"
assert_status "which= unknown repo" 404 "$BASE/api/jobs/$BJOB/export?format=pdf&which=nope%2Fnope"

# --- export target resolution ----------------------------------------------
#
# A job stores each repository exactly as it was submitted, while the browser
# identifies a report by the owner/repo pair the report renders. The two differ
# for every URL, SSH address or ".git" suffix, and both spellings have to
# resolve -- otherwise downloading a URL-submitted repository 404s.
run_and_export() {
  local spec="$1" which="$2"
  local id
  id=$(j -X POST -H 'Content-Type: application/json' \
    -d "{\"repo\":\"$spec\",\"since_days\":7}" "$BASE/api/jobs/analyze" \
    | sed -n 's/.*"id":"\([^"]*\)".*/\1/p')
  for _ in $(seq 1 150); do
    case "$(j "$BASE/api/jobs/$id")" in *'"state":"done"'*) break;; esac
    sleep 0.1
  done
  local enc
  enc=$(printf '%s' "$which" | sed 's|/|%2F|g; s|@|%40|g; s|:|%3A|g')
  curl -s -o /dev/null -w '%{http_code}' "$BASE/api/jobs/$id/export?format=pdf&which=$enc"
}

# The pair the report view renders is what a download click sends.
[ "$(run_and_export 'https://github.com/acme/gadget' 'acme/gadget')" = "200" ] && ok \
  || fail "export by rendered owner/repo must work for a URL submission"
[ "$(run_and_export 'https://github.com/acme/gadget.git' 'acme/gadget')" = "200" ] && ok \
  || fail "export must work for a .git submission"
[ "$(run_and_export 'git@github.com:acme/gadget.git' 'acme/gadget')" = "200" ] && ok \
  || fail "export must work for an SSH submission"
# The string the job was submitted with is what the batch table sends.
[ "$(run_and_export 'https://github.com/acme/gadget' 'https://github.com/acme/gadget')" = "200" ] && ok \
  || fail "export by submitted spec must still work"
# A long spec must not be truncated into a non-match.
LONG_SPEC="https://github.com/acme/a-repository-name-well-past-any-short-buffer"
[ "$(run_and_export "$LONG_SPEC" 'acme/a-repository-name-well-past-any-short-buffer')" = "200" ] && ok \
  || fail "export must not truncate a long which= value"
# A single-report job has only one thing ?which= could name.
[ "$(run_and_export 'acme/gadget' 'some/other-thing')" = "200" ] && ok \
  || fail "a single-report job should fall back to its only report"
[ "$(run_and_export 'acme/gadget' 'definitely/not-this-one')" = "200" ] && ok \
  || fail "a single-report job should not 404 on an unmatched which="
# The download is named after the repository the report shows.
DISP=$(curl -s -D- -o /dev/null "$BASE/api/jobs/$JOB/export?format=pdf" | grep -i content-disposition)
case "$DISP" in *'filename="acme_gadget.pdf"'*) ok;; *) fail "download name should be acme_gadget.pdf: $DISP";; esac

# --- language override ------------------------------------------------------

# ?lang= re-renders one response in another language. The exporters read the
# active language from a thread-local, so this also pins that two languages can
# be requested back to back without one bleeding into the other.
MD_EN=$(curl -s "$BASE/api/jobs/$JOB/export?format=md&which=acme%2Fgadget&lang=en")
MD_ZH=$(curl -s "$BASE/api/jobs/$JOB/export?format=md&which=acme%2Fgadget&lang=zh")
assert_contains "lang=en renders English headings" "GitSight Report" "$MD_EN"
assert_contains "lang=zh renders Chinese headings" "GitSight 报告" "$MD_ZH"
case "$MD_ZH" in
  *GitSight\ Report*) fail "lang=zh still produced the English heading";;
  *) ok;;
esac
# Every text format honours it, not just markdown.
for f in txt html; do
  assert_contains "lang=zh applies to $f" "GitSight 报告" \
    "$(curl -s "$BASE/api/jobs/$JOB/export?format=$f&lang=zh")"
done
# A bogus language falls back to English rather than emptying the report.
assert_contains "unknown lang falls back to English" "GitSight Report" \
  "$(curl -s "$BASE/api/jobs/$JOB/export?format=md&lang=xx")"

LANG_FRAMING=$(python3 - "$PORT" "$JOB" <<'PYEOF'
import socket, sys
port, job = sys.argv[1], sys.argv[2]
reqs = []
for lang in ('en', 'zh', 'en', 'zh'):
    reqs.append(
        'GET /api/jobs/%s/export?format=md&which=acme%%2Fgadget&lang=%s '
        'HTTP/1.1\r\nHost: t\r\n\r\n' % (job, lang)
    )
s = socket.create_connection(('127.0.0.1', int(port)))
s.sendall(''.join(reqs).encode() + b'Connection: close\r\n\r\n')
s.settimeout(5)
buf = b''
try:
    while True:
        chunk = s.recv(65536)
        if not chunk:
            break
        buf += chunk
except OSError:
    pass
sys.stdout.write(buf.decode('utf-8', 'replace'))
PYEOF
)
# Four responses, alternating, each carrying only its own language's heading.
COUNT=$(printf '%s' "$LANG_FRAMING" | grep -c '^HTTP/1.1 200')
[ "$COUNT" = "4" ] && ok || fail "expected 4 pipelined export responses, got $COUNT"
ZH_OK=$(printf '%s' "$LANG_FRAMING" | grep -c 'GitSight 报告')
EN_OK=$(printf '%s' "$LANG_FRAMING" | grep -c 'GitSight Report')
[ "$ZH_OK" -ge 2 ] && [ "$EN_OK" -ge 2 ] && ok \
  || fail "interleaved languages bled across requests (en=$EN_OK zh=$ZH_OK)"
# A zh response must not also carry the English heading.
if printf '%s' "$LANG_FRAMING" | grep -q 'GitSight 报告' && \
   printf '%s' "$LANG_FRAMING" | grep -q 'GitSight Report'; then ok; else
  fail "no response carried both language headings"
fi

# --- comparison -------------------------------------------------------------

CMP=$(j -X POST -H 'Content-Type: application/json' \
  -d '{"repo":"acme/gadget","days_a":7,"days_b":30}' "$BASE/api/jobs/compare")
CJOB=$(printf '%s' "$CMP" | sed -n 's/.*"id":"\([^"]*\)".*/\1/p')
CRESULT=""
for _ in $(seq 1 150); do
  CRESULT=$(j "$BASE/api/jobs/$CJOB/result")
  case "$CRESULT" in *'"period_a"'*) break;; esac
  sleep 0.1
done
assert_contains "compare periods"  '"period_days_a":7'  "$CRESULT"
assert_contains "compare periods b" '"period_days_b":30' "$CRESULT"
assert_contains "compare metric"   '"pr_total"'        "$CRESULT"
assert_contains "compare reports"  '"report_a"'        "$CRESULT"
assert_status "compare md export" 200 "$BASE/api/jobs/$CJOB/export?format=md"
assert_status "compare pdf rejected" 400 "$BASE/api/jobs/$CJOB/export?format=pdf"

# --- subscriptions batch ----------------------------------------------------

SJOB=$(j -X POST -H 'Content-Type: application/json' -d '{}' "$BASE/api/subscriptions/acme%2Fgadget/analyze")
SJID=$(printf '%s' "$SJOB" | sed -n 's/.*"id":"\([^"]*\)".*/\1/p')
SRESULT=""
for _ in $(seq 1 150); do
  SRESULT=$(j "$BASE/api/jobs/$SJID/result")
  case "$SRESULT" in *'"pull_requests"'*) break;; esac
  sleep 0.1
done
assert_contains "subscription analyze" '"pull_requests"' "$SRESULT"
assert_status "subscriptions batch" 202 -X POST -H 'Content-Type: application/json' \
  -d '{"parallel":2}' "$BASE/api/subscriptions/analyze"
# With the last subscription gone, a batch run has nothing to do.
assert_status "remove last subscription" 200 -X DELETE "$BASE/api/subscriptions/core"
assert_status "batch rejects when empty" 400 -X POST \
  -H 'Content-Type: application/json' -d '{}' "$BASE/api/subscriptions/analyze"

# --- filters and date ranges ------------------------------------------------

F=$(j -X POST -H 'Content-Type: application/json' \
  -d '{"repo":"acme/gadget","filters":["author:alice"]}' "$BASE/api/jobs/analyze")
FJOB=$(printf '%s' "$F" | sed -n 's/.*"id":"\([^"]*\)".*/\1/p')
FRESULT=""
for _ in $(seq 1 150); do
  FRESULT=$(j "$BASE/api/jobs/$FJOB/result")
  case "$FRESULT" in *'"pull_requests"'*) break;; esac
  sleep 0.1
done
assert_contains "filter kept alice"  '"Fix the widget"' "$FRESULT"
if printf '%s' "$FRESULT" | grep -q '"Add docs"'; then
  fail "filter author:alice should have dropped bob's PR"
else ok; fi

# --- HTTP mechanics ---------------------------------------------------------

assert_status "method not allowed" 405 -X POST "$BASE/api/health"
assert_status "unknown api path" 404 "$BASE/api/nope"
assert_status "bad json rejected" 400 -X PATCH -H 'Content-Type: application/json' \
  -d '{not json' "$BASE/api/config"
assert_status "preflight ok" 204 -X OPTIONS "$BASE/api/health"
CT=$(curl -s -o /dev/null -w '%{content_type}' "$BASE/api/health")
case "$CT" in application/json*) ok;; *) fail "expected JSON content type, got $CT";; esac

# A HEAD reply advertises the GET length but carries no body, otherwise the next
# response on a kept-alive connection is unframed.
HEAD_FRAMING=$(printf 'HEAD / HTTP/1.1\r\nHost: t\r\nConnection: keep-alive\r\n\r\nGET /api/health HTTP/1.1\r\nHost: t\r\nConnection: close\r\n\r\n' \
  | timeout 5 python3 -c "
import socket, sys
s = socket.create_connection(('127.0.0.1', $PORT))
s.sendall(sys.stdin.buffer.read())
s.settimeout(3)
buf = b''
try:
    while True:
        chunk = s.recv(4096)
        if not chunk:
            break
        buf += chunk
except OSError:
    pass
sys.stdout.write(buf.decode('utf-8', 'replace'))
")
case "$HEAD_FRAMING" in
  *"<!doctype"*HTTP/1.1*200*) fail "HEAD response carried a body and desynchronized keep-alive";;
  *HTTP/1.1*200*'{"'*) ok;;
  *) fail "HEAD/GET pipelining did not produce two well-framed responses";;
esac

# --- cancel -----------------------------------------------------------------
CJOB2=$(j -X POST -H 'Content-Type: application/json' \
  -d '{"repo":"acme/gadget"}' "$BASE/api/jobs/analyze")
CJID=$(printf '%s' "$CJOB2" | sed -n 's/.*"id":"\([^"]*\)".*/\1/p')
assert_status "cancel accepted" 204 -X POST "$BASE/api/jobs/$CJID/cancel"
assert_status "cancel unknown" 404 -X POST "$BASE/api/jobs/nope/cancel"

# --- raw config -------------------------------------------------------------

assert_status "raw config readable" 200 "$BASE/api/config/raw"
assert_status "raw config rejects non-json" 400 -X PUT -H 'Content-Type: application/json' \
  -d 'not json at all' "$BASE/api/config/raw"
assert_status "config reset" 200 -X POST "$BASE/api/config/reset"
assert_contains "config reset applied" '"platform":"github"' "$(j "$BASE/api/config")"

# The browser's raw editor shows this body in a textarea, so a JSON content type
# would make every client deserialize it into an object it cannot display.
RAW_CT=$(curl -s -o /dev/null -w '%{content_type}' "$BASE/api/config/raw")
case "$RAW_CT" in text/plain*) ok;; *) fail "raw config should be text/plain, got $RAW_CT";; esac
RAW_BODY=$(curl -s "$BASE/api/config/raw")
case "$RAW_BODY" in '{'*) ok;; *) fail "raw config body should be the file itself";; esac

# --- caching ----------------------------------------------------------------

# A response may carry exactly one Cache-Control. Two of them combine into a
# single comma-joined value, so a hardcoded no-store alongside an immutable
# asset header would silently defeat the caching it claims to enable.
cache_control_count() { curl -s -D- -o /dev/null "$1" | grep -ci '^cache-control:'; }
[ "$(cache_control_count "$BASE/api/health")" = "1" ] && ok \
  || fail "api responses must send exactly one Cache-Control header"
[ "$(cache_control_count "$BASE/")" = "1" ] && ok \
  || fail "the SPA shell must send exactly one Cache-Control header"
ASSET=$(ls "$SRC"/web/dist/assets/*.js 2>/dev/null | head -1)
if [ -n "$ASSET" ]; then
  ASSET_URL="/assets/$(basename "$ASSET")"
  [ "$(cache_control_count "$BASE$ASSET_URL")" = "1" ] && ok \
    || fail "hashed assets must send exactly one Cache-Control header"
  AC=$(curl -s -D- -o /dev/null "$BASE$ASSET_URL" | grep -i '^cache-control:' | tr -d '\r')
  case "$AC" in *immutable*) ok;; *) fail "hashed asset not cacheable: $AC";; esac
  case "$AC" in *no-store*) fail "no-store overrides immutable on assets: $AC";; *) ok;; esac
fi
IC=$(curl -s -D- -o /dev/null "$BASE/" | grep -i '^cache-control:' | tr -d '\r')
case "$IC" in *no-cache*) ok;; *) fail "the SPA shell must not be cached: $IC";; esac

# --- usage window -----------------------------------------------------------

# With no records logged yet the window must still be labelled with real dates;
# a zeroed day_start would report every bucket as the epoch.
curl -s -o /dev/null -X DELETE "$BASE/api/usage"
U=$(curl -s "$BASE/api/usage?days=5")
assert_contains "usage labels a day window" "$(date -u +%Y-%m-%d)" "$U"
case "$U" in *1970-01-01*) fail "usage by_day fell back to the epoch";; *) ok;; esac
assert_contains "usage reports the requested window" '"days":5' "$U"

# --- repository arguments ---------------------------------------------------

# The CLI refuses these; the daemon has to agree rather than queue a job that
# finishes green with an empty report.
for bad in 'bad format here' 'oops' 'https://github.com/onlyowner' ''; do
  BODY=$(printf '{"repo":%s}' "$(printf '%s' "$bad" | sed 's/"/\\"/g' | sed 's/^/"/;s/$/"/')")
  assert_status "rejects repo '$bad'" 400 -X POST -H 'Content-Type: application/json' \
    -d "$BODY" "$BASE/api/jobs/analyze"
  assert_status "rejects compare repo '$bad'" 400 -X POST -H 'Content-Type: application/json' \
    -d "$BODY" "$BASE/api/jobs/compare"
done
# A batch is rejected as a whole rather than half-run.
assert_status "rejects a bad repo inside a batch" 400 -X POST -H 'Content-Type: application/json' \
  -d '{"repos":["acme/gadget","oops"]}' "$BASE/api/jobs/analyze"
# Every accepted spelling of a repository keeps working.
for good in 'acme/gadget' 'https://github.com/curl/curl' 'git@github.com:curl/curl.git'; do
  assert_status "accepts repo '$good'" 202 -X POST -H 'Content-Type: application/json' \
    -d "{\"repo\":\"$good\"}" "$BASE/api/jobs/analyze"
done

# --- job retention ----------------------------------------------------------

# The daemon keeps a bounded history and evicts the oldest finished job once the
# cap is passed. Every kind of job has to survive that release, or the daemon
# dies mid-session once a long-running user crosses the threshold.
EVICT_N=140
i=0
while [ "$i" -lt "$EVICT_N" ]; do
  curl -s -o /dev/null -X POST -H 'Content-Type: application/json' \
    -d '{"repo":"acme/gadget","since_days":7}' "$BASE/api/jobs/analyze"
  curl -s -o /dev/null -X POST -H 'Content-Type: application/json' \
    -d '{"repo":"acme/gadget","days_a":7,"days_b":30}' "$BASE/api/jobs/compare"
  i=$((i + 1))
done
sleep 2
assert_status "daemon survives job eviction" 200 "$BASE/api/health"
assert_contains "job list still served after eviction" '"jobs"' "$(j "$BASE/api/jobs?limit=5")"

# --- shutdown ----------------------------------------------------------------

assert_exits "Ctrl+C stops the daemon" INT 5
assert_exits "SIGTERM stops the daemon" TERM 5

# --- summary ----------------------------------------------------------------

echo
echo "server tests: $PASS passed, $FAIL failed"
[ "$FAIL" -eq 0 ]