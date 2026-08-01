#!/usr/bin/env bash
# Offline smoke test for GitSight using GS_FIXTURE_DIR mock responses.
# Usage: bash tests/run_smoke.sh /path/to/gs

set -u

GS="$1"
SRC="${2:-$(cd "$(dirname "$0")/.." && pwd)}"
WORK="$(mktemp -d)"
trap 'rm -rf "$WORK"' EXIT
FIX="$WORK/fixtures"
OUT="$WORK/out"
mkdir -p "$FIX" "$OUT"

D1=$(date -u -d '1 day ago' +%Y-%m-%dT%H:%M:%SZ 2>/dev/null)
if [ -z "$D1" ]; then
    iso_days_ago() { date -u -v-${1}d +%Y-%m-%dT%H:%M:%SZ; }
else
    iso_days_ago() { date -u -d "$1 days ago" +%Y-%m-%dT%H:%M:%SZ; }
fi
D1=$(iso_days_ago 1)
D2=$(iso_days_ago 2)
D3=$(iso_days_ago 3)
D4=$(iso_days_ago 4)
D30=$(iso_days_ago 40)

fail() {
    echo "FAIL: $1" >&2
    exit 1
}

cat > "$FIX/prs.json" <<EOF
[
  {"number": 42, "title": "Fix the widget", "state": "merged", "created_at": "$D4", "updated_at": "$D1", "merged_at": "$D2", "html_url": "https://github.com/acme/gadget/pull/42", "user": {"login": "alice"}, "labels": [{"name": "bug"}, {"name": "enhancement"}]},
  {"number": 41, "title": "Add docs", "state": "closed", "created_at": "$D4", "updated_at": "$D2", "merged_at": null, "html_url": "https://github.com/acme/gadget/pull/41", "user": {"login": "bob"}, "labels": []},
  {"number": 40, "title": "WIP thing", "state": "open", "created_at": "$D3", "updated_at": "$D3", "merged_at": null, "html_url": "https://github.com/acme/gadget/pull/40", "user": {"login": "alice"}, "labels": [{"name": "wip"}]}
]
EOF

cat > "$FIX/commits.json" <<EOF
[
  {"sha": "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa1", "html_url": "https://github.com/acme/gadget/commit/a", "commit": {"message": "first commit\n\nbody line", "author": {"name": "alice", "date": "$D1"}}},
  {"sha": "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa2", "html_url": "https://github.com/acme/gadget/commit/b", "commit": {"message": "second commit", "author": {"name": "bob", "date": "$D2"}}},
  {"sha": "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa3", "html_url": "https://github.com/acme/gadget/commit/c", "commit": {"message": "third commit", "author": {"name": "alice", "date": "$D3"}}}
]
EOF

cat > "$FIX/issues.json" <<EOF
[
  {"number": 99, "title": "Something broke", "state": "closed", "created_at": "$D4", "updated_at": "$D1", "closed_at": "$D2", "html_url": "https://github.com/acme/gadget/issues/99", "user": {"login": "bob"}, "labels": [{"name": "bug"}]},
  {"number": 98, "title": "Feature request", "state": "open", "created_at": "$D3", "updated_at": "$D3", "closed_at": null, "html_url": "https://github.com/acme/gadget/issues/98", "user": {"login": "alice"}, "labels": []}
]
EOF

cat > "$FIX/releases.json" <<EOF
[
  {"tag_name": "v1.2.3", "name": "Release 1.2.3", "published_at": "$D2", "html_url": "https://github.com/acme/gadget/releases/v1.2.3", "author": {"login": "alice"}, "body": "Big release", "draft": false}
]
EOF

cat > "$FIX/contributors.json" <<EOF
[
  {"login": "alice", "contributions": 15},
  {"login": "bob", "contributions": 7}
]
EOF

cat > "$FIX/meta.json" <<EOF
{
  "description": "A test gadget",
  "language": "C",
  "default_branch": "main",
  "stargazers_count": 1234,
  "forks_count": 88,
  "watchers_count": 567,
  "open_issues_count": 2,
  "created_at": "2020-01-01T00:00:00Z",
  "pushed_at": "$D1",
  "license": {"spdx_id": "MIT"}
}
EOF

export GS_FIXTURE_DIR="$FIX"
export GS_CACHE_DIR="$WORK/cache"
export NO_COLOR=1

# Isolated config so no LLM provider or real settings leak in
CFG="$WORK/config.json"
"$GS" config --config "$CFG" --set default_days=7 >/dev/null 2>&1 || fail "seed config failed"
ARGS="--config $CFG"

"$GS" analyze acme/gadget $ARGS --no-cache -q -d 7 -f md -o "$OUT/report.md" || fail "md export failed"
grep -q "Pull Requests" "$OUT/report.md" || fail "md missing PR section"
grep -q "Commits" "$OUT/report.md" || fail "md missing commits section"
grep -q "Issues" "$OUT/report.md" || fail "md missing issues section"
grep -q "Contributors" "$OUT/report.md" || fail "md missing contributors section"
grep -q "v1.2.3" "$OUT/report.md" || fail "md missing release"
grep -q "1234" "$OUT/report.md" || fail "md missing repo meta"
grep -q "bug" "$OUT/report.md" || fail "md missing top labels"

"$GS" analyze acme/gadget $ARGS --no-cache -q -d 7 -f json -o "$OUT/report.json" || fail "json export failed"
grep -q '"items"' "$OUT/report.json" || fail "json missing items"
grep -q '"repository"' "$OUT/report.json" || fail "json missing repository"
grep -q '"pull_requests"' "$OUT/report.json" || fail "json missing pull_requests"
grep -q '"activity"' "$OUT/report.json" || fail "json missing activity"

"$GS" analyze acme/gadget $ARGS --no-cache -q -d 7 -f txt -o "$OUT/report.txt" || fail "txt export failed"
grep -q "Pull Requests" "$OUT/report.txt" || fail "txt missing PR section"
grep -q "Commits" "$OUT/report.txt" || fail "txt missing commits section"

"$GS" analyze acme/gadget $ARGS --no-cache -q -d 7 -f xlsx -o "$OUT/report.xlsx" || fail "xlsx export failed"
head -c 2 "$OUT/report.xlsx" | grep -q "PK" || fail "xlsx not a valid zip"

"$GS" analyze acme/gadget $ARGS --no-cache -q -d 7 -f pdf -o "$OUT/report.pdf" || fail "pdf export failed"
head -c 4 "$OUT/report.pdf" | grep -q "%PDF" || fail "pdf missing header"

"$GS" analyze acme/gadget $ARGS --no-cache -q -d 7 --compare 7:30 -o "$OUT/compare.md" || fail "compare failed"
grep -q "Activity comparison" "$OUT/compare.md" || fail "compare missing header"
grep -q "Pull requests" "$OUT/compare.md" || fail "compare missing metrics"

"$GS" analyze acme/gadget $ARGS --no-cache -q -d 7 --compare 7:30 -f json -o "$OUT/compare.json" || fail "compare json failed"
grep -q '"period_a"' "$OUT/compare.json" || fail "compare json missing period_a"

"$GS" config --config "$CFG" --set default_days=21 || fail "config set failed"
grep -q '"default_days":	21' "$CFG" || grep -q '"default_days":21' "$CFG" || fail "config set not persisted"
"$GS" config --config "$CFG" --set export.item_limit=3 || fail "config set export failed"
"$GS" config --config "$CFG" --show | grep -q "default_days" || fail "config show failed"

# LLM protocol config: auto-detect, list, and set round-trip
cat > "$WORK/proto.json" <<EOF
{
	"llms":	{
		"claude":	{
			"api_key":	"sk-ant-test",
			"api_base":	"https://api.anthropic.com",
			"model":	"claude-sonnet-4-20250514"
		},
		"gemini":	{
			"protocol":	"gemini",
			"api_key":	"AIza-test",
			"api_base":	"https://generativelanguage.googleapis.com/v1beta",
			"model":	"gemini-2.0-flash"
		},
		"local":	{
			"api_key":	"ollama",
			"api_base":	"http://localhost:11434/v1",
			"model":	"llama3.2"
		}
	}
}
EOF
"$GS" config --config "$WORK/proto.json" --list-llms 2>&1 | grep -q "Protocol" || fail "list-llms missing protocol column"
"$GS" config --config "$WORK/proto.json" --list-llms 2>&1 | grep -q "claude" || fail "list-llms missing claude"
"$GS" config --config "$WORK/proto.json" --list-llms 2>&1 | grep -q "gemini" || fail "list-llms missing gemini"
"$GS" config --config "$WORK/proto.json" --set llms.claude.protocol=gemini || fail "config set protocol failed"
grep -q '"protocol":	"gemini"' "$WORK/proto.json" || fail "config set protocol not persisted"
if "$GS" config --config "$WORK/proto.json" --set llms.claude.protocol=bogus >/dev/null 2>&1; then fail "invalid protocol should fail"; fi

# Batch mode: two repos produce two files
"$GS" analyze acme/gadget foo/bar $ARGS --no-cache -d 7 -f md 2>/dev/null | grep -q "Batch summary" || fail "batch summary table missing"
[ -f "acme_gadget_report.md" ] || fail "batch missing first report"
[ -f "foo_bar_report.md" ] || fail "batch missing second report"
rm -f acme_gadget_report.md foo_bar_report.md

# watch (subscriptions) CRUD + analyze
"$GS" watch $ARGS add acme/gadget --alias core --days 7 --format json || fail "watch add failed"
"$GS" watch $ARGS list | grep -q "acme/gadget" || fail "watch list missing repo"
"$GS" watch $ARGS list | grep -q "┌" || fail "watch list missing tui table border"
"$GS" watch $ARGS list | grep -q "│ Repository" || fail "watch list missing tui table header"
"$GS" watch $ARGS list --json | grep -q '"repo": "acme/gadget"' || fail "watch list json missing repo"
if "$GS" watch $ARGS add acme/gadget >/dev/null 2>&1; then fail "watch duplicate add should fail"; fi
"$GS" watch $ARGS update core --days 30 || fail "watch update failed"
"$GS" watch $ARGS list | grep -q "30" || fail "watch update not persisted"
"$GS" watch $ARGS add acme/other --format md || fail "watch add 2nd failed"
"$GS" watch $ARGS analyze --no-cache -q || fail "watch analyze failed"
[ -f acme_gadget_report.json ] || fail "watch analyze missing gadget json"
[ -f acme_other_report.md ] || fail "watch analyze missing other md"
rm -f acme_gadget_report.json acme_other_report.md
"$GS" watch $ARGS remove core || fail "watch remove failed"
"$GS" watch $ARGS remove acme/other || fail "watch remove 2nd failed"
if "$GS" watch $ARGS list | grep -q "acme/"; then fail "watch list not empty after remove"; fi

"$GS" version || fail "version failed"

# completion command: embedded scripts must round-trip byte-for-byte
"$GS" completion bash > "$WORK/gs.bash" || fail "completion bash failed"
cmp -s "$WORK/gs.bash" "$SRC/completions/gs.bash" || fail "completion bash != source"
"$GS" completion zsh > "$WORK/_gs" || fail "completion zsh failed"
cmp -s "$WORK/_gs" "$SRC/completions/_gs" || fail "completion zsh != source"
"$GS" completion fish > "$WORK/gs.fish" || fail "completion fish failed"
cmp -s "$WORK/gs.fish" "$SRC/completions/gs.fish" || fail "completion fish != source"
bash -n "$WORK/gs.bash" || fail "completion bash syntax invalid"
if command -v zsh >/dev/null 2>&1; then
    zsh -n "$WORK/_gs" || fail "completion zsh syntax invalid"
fi
if "$GS" completion >/dev/null 2>&1; then fail "completion without shell should fail"; fi
if "$GS" completion nope >/dev/null 2>&1; then fail "completion with bad shell should fail"; fi

# usage command: LLM usage rendered as a unicode bar chart
USG="$WORK/usage.json"
export GS_USAGE_FILE="$USG"
TS1=$(date -d '1 day ago' +%s 2>/dev/null)
TS2=$(date -d '2 days ago' +%s 2>/dev/null)
if [ -z "$TS1" ]; then TS1=$(date -v-1d +%s); fi
if [ -z "$TS2" ]; then TS2=$(date -v-2d +%s); fi
cat > "$USG" <<EOF
{
  "records": [
    {"ts": $TS1, "llm": "default", "model": "gpt-4o-mini", "prompt": 100, "completion": 50, "total": 150},
    {"ts": $TS1, "llm": "default", "model": "gpt-4o-mini", "prompt": 200, "completion": 50, "total": 250},
    {"ts": $TS2, "llm": "local", "model": "llama3.2", "prompt": 50, "completion": 50, "total": 100}
  ]
}
EOF

USG_JSON="$("$GS" usage --json 2>/dev/null)" || fail "usage json failed"
grep -q '"total_tokens":	500' <<<"$USG_JSON" || grep -q '"total_tokens": 500' <<<"$USG_JSON" || fail "usage json totals wrong"
grep -q '"calls":	3' <<<"$USG_JSON" || grep -q '"calls": 3' <<<"$USG_JSON" || fail "usage json calls wrong"
USG_JSON_LLM="$("$GS" usage --json --by-llm 2>/dev/null)" || fail "usage json by-llm failed"
grep -q '"by_llm"' <<<"$USG_JSON_LLM" || fail "usage json missing by_llm"
USG_LLM="$("$GS" usage --by-llm 2>/dev/null)" || fail "usage by-llm failed"
grep -q "default" <<<"$USG_LLM" || fail "usage by-llm missing default"
grep -q "local" <<<"$USG_LLM" || fail "usage by-llm missing local"
"$GS" usage 2>/dev/null | grep -q "█" || fail "usage chart missing unicode bar"
"$GS" usage --days 3 2>/dev/null | grep -q "last 3 days" || fail "usage days window"
"$GS" usage --clear >/dev/null 2>&1 || fail "usage clear failed"
"$GS" usage 2>&1 | grep -q "No LLM usage recorded" || fail "usage not cleared"
unset GS_USAGE_FILE

echo "SMOKE TEST OK"
exit 0
