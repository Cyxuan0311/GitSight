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

. "$(dirname "$0")/common.sh"

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

cat > "$FIX/languages.json" <<EOF
{
  "C": 61000,
  "Python": 30000,
  "Shell": 1000
}
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
grep -q "Languages" "$OUT/report.md" || fail "md missing languages section"
grep -q "66.3%" "$OUT/report.md" || fail "md missing language percentage"

"$GS" analyze acme/gadget $ARGS --no-cache -q -d 7 -f json -o "$OUT/report.json" || fail "json export failed"
grep -q '"items"' "$OUT/report.json" || fail "json missing items"
grep -q '"repository"' "$OUT/report.json" || fail "json missing repository"
grep -q '"pull_requests"' "$OUT/report.json" || fail "json missing pull_requests"
grep -q '"activity"' "$OUT/report.json" || fail "json missing activity"
grep -q '"languages"' "$OUT/report.json" || fail "json missing languages"

"$GS" analyze acme/gadget $ARGS --no-cache -q -d 7 -f txt -o "$OUT/report.txt" || fail "txt export failed"
grep -q "Pull Requests" "$OUT/report.txt" || fail "txt missing PR section"
grep -q "Commits" "$OUT/report.txt" || fail "txt missing commits section"

"$GS" analyze acme/gadget $ARGS --no-cache -q -d 7 -f xlsx -o "$OUT/report.xlsx" || fail "xlsx export failed"
head -c 2 "$OUT/report.xlsx" | grep -q "PK" || fail "xlsx not a valid zip"

"$GS" analyze acme/gadget $ARGS --no-cache -q -d 7 -f pdf -o "$OUT/report.pdf" || fail "pdf export failed"
head -c 4 "$OUT/report.pdf" | grep -q "%PDF" || fail "pdf missing header"

"$GS" analyze acme/gadget $ARGS --no-cache -q -d 7 -f html -o "$OUT/report.html" || fail "html export failed"
grep -qi "<!DOCTYPE html>" "$OUT/report.html" || fail "html missing doctype"
grep -q "badge" "$OUT/report.html" || fail "html missing state badges"
grep -q 'href="#prs"' "$OUT/report.html" || fail "html missing TOC anchor"
grep -q 'class="tfilter"' "$OUT/report.html" || fail "html missing filter inputs"
grep -q 'data-target="tbl-prs"' "$OUT/report.html" || fail "html filter not bound to PR table"
grep -q "<script>" "$OUT/report.html" || fail "html missing inline filter script"
grep -q 'href="#languages"' "$OUT/report.html" || fail "html missing languages anchor"
"$GS" analyze acme/gadget $ARGS --no-cache -q -d 7 -f html --export html.open_color=red -o "$OUT/report_colors.html" || fail "html color export failed"
grep -q '.b-open{color:#dc2629}' "$OUT/report_colors.html" || fail "html --export named color not applied"

# --filter keeps only matching items; --to bounds the window
"$GS" analyze acme/gadget $ARGS --no-cache -q --filter author:bob -f md -o "$OUT/filt.md" || fail "filter analyze failed"
grep -q "Add docs" "$OUT/filt.md" || fail "filter dropped bob's own PR"
grep -q "Fix the widget" "$OUT/filt.md" && fail "filter kept alice's PR"
"$GS" analyze acme/gadget $ARGS --no-cache -q --to "$(iso_date_days_ago 3)" -f md -o "$OUT/win.md" || fail "window analyze failed"
grep -q "first commit" "$OUT/win.md" && fail "--to kept commit after cutoff"
"$GS" analyze acme/gadget $ARGS --no-cache -q --from bad-date -f md -o /dev/null 2>"$OUT/bad.txt" && fail "bad --from accepted"
grep -q "YYYY-MM-DD" "$OUT/bad.txt" || fail "bad --from wrong error"

"$GS" analyze acme/gadget $ARGS --no-cache -q -d 7 -f csv -o "$OUT/report.csv" || fail "csv export failed"
grep -q "^# metrics$" "$OUT/report.csv" || fail "csv missing metrics block"
grep -q "^# pull_requests$" "$OUT/report.csv" || fail "csv missing pull_requests block"
grep -q "^# commits$" "$OUT/report.csv" || fail "csv missing commits block"
grep -q "Fix the widget" "$OUT/report.csv" || fail "csv missing PR row"

"$GS" analyze acme/gadget $ARGS --no-cache -q -d 7 --compare 7:30 -o "$OUT/compare.md" || fail "compare failed"
grep -q "Activity comparison" "$OUT/compare.md" || fail "compare missing header"
grep -q "Pull requests" "$OUT/compare.md" || fail "compare missing metrics"

"$GS" analyze acme/gadget $ARGS --no-cache -q -d 7 --compare 7:30 -f json -o "$OUT/compare.json" || fail "compare json failed"
grep -q '"period_a"' "$OUT/compare.json" || fail "compare json missing period_a"

"$GS" config --config "$CFG" --set default_days=21 || fail "config set failed"
grep -q '"default_days":	21' "$CFG" || grep -q '"default_days":21' "$CFG" || fail "config set not persisted"
"$GS" config --config "$CFG" --set export.item_limit=3 || fail "config set export failed"
"$GS" config --config "$CFG" --show | grep -q "default_days" || fail "config show failed"

# Codeberg / Gitea-family: shared driver parsing gitea-style JSON, URL host
# detection, Link-style pagination, and gitea meta field fallbacks.
FIXCB="$WORK/fixcb"
mkdir -p "$FIXCB"
cat > "$FIXCB/prs.json" <<EOF
[
  {"number": 7, "title": "Fix the flaky test", "state": "closed", "created_at": "$D4", "updated_at": "$D1", "merged": true, "merged_at": "$D2", "html_url": "https://codeberg.org/acme/gadget/pulls/7", "user": {"login": "alice"}, "labels": [{"name": "bug"}]},
  {"number": 6, "title": "Open PR", "state": "open", "created_at": "$D3", "updated_at": "$D2", "merged": false, "merged_at": null, "html_url": "https://codeberg.org/acme/gadget/pulls/6", "user": {"login": "bob"}, "labels": []}
]
EOF
cat > "$FIXCB/prs.2.json" <<EOF
[{"number": 5, "title": "Page two PR", "state": "open", "created_at": "$D3", "updated_at": "$D1", "merged": false, "merged_at": null, "html_url": "https://codeberg.org/acme/gadget/pulls/5", "user": {"login": "alice"}, "labels": []}]
EOF
printf 'https://codeberg.org/api/v1/repos/acme/gadget/pulls?page=2&state=all\n' > "$FIXCB/next_page"
cp "$FIX/commits.json" "$FIXCB/commits.json"
cp "$FIX/issues.json" "$FIXCB/issues.json"
cp "$FIX/releases.json" "$FIXCB/releases.json"
cp "$FIX/contributors.json" "$FIXCB/contributors.json"
cat > "$FIXCB/meta.json" <<EOF
{
  "description": "A codeberg gadget",
  "language": "Go",
  "default_branch": "main",
  "stars_count": 4321,
  "forks_count": 77,
  "watchers_count": 123,
  "open_issues_count": 3,
  "created_at": "2021-05-05T00:00:00Z",
  "updated_at": "$D1",
  "licenses": ["MIT"]
}
EOF
GS_FIXTURE_DIR="$FIXCB" "$GS" analyze https://codeberg.org/acme/gadget $ARGS --no-cache -q -d 7 -f md -o "$OUT/codeberg.md" || fail "codeberg analyze failed"
grep -q "4321" "$OUT/codeberg.md" || fail "codeberg missing gitea star count (stars_count)"
grep -q "MIT" "$OUT/codeberg.md" || fail "codeberg missing license (licenses array)"
grep -q "Page two PR" "$OUT/codeberg.md" || fail "codeberg missing page-2 PR (Link pagination)"
grep -q "| merged | alice |" "$OUT/codeberg.md" || fail "codeberg closed+merged PR not normalized"

# Explicit --platform gitea against the same gitea-style fixtures
GS_FIXTURE_DIR="$FIXCB" "$GS" analyze acme/gadget $ARGS --platform gitea --no-cache -q -d 7 -f txt -o "$OUT/gitea.txt" || fail "gitea analyze failed"
grep -q "4321" "$OUT/gitea.txt" || fail "gitea txt missing star count"

# GitLab X-Next-Page pagination: numeric next_page marker drives the
# page-parameter reconstruction path (fixes GitLab fetches beyond page 1).
FIXGL="$WORK/fixgl"
mkdir -p "$FIXGL"
cat > "$FIXGL/prs.json" <<EOF
[
  {"iid": 11, "title": "MR one", "state": "opened", "created_at": "$D3", "updated_at": "$D1", "merged_at": null, "web_url": "https://gitlab.com/acme/gadget/-/merge_requests/11", "author": {"name": "alice"}}
]
EOF
cat > "$FIXGL/prs.2.json" <<EOF
[{"iid": 10, "title": "MR from page two", "state": "merged", "created_at": "$D3", "updated_at": "$D1", "merged_at": "$D2", "web_url": "https://gitlab.com/acme/gadget/-/merge_requests/10", "author": {"name": "bob"}}]
EOF
printf '2\n' > "$FIXGL/next_page"
GS_FIXTURE_DIR="$FIXGL" "$GS" analyze acme/gadget $ARGS --platform gitlab --no-cache -q -d 7 -f md -o "$OUT/gitlab.md" || fail "gitlab pagination analyze failed"
grep -q "MR from page two" "$OUT/gitlab.md" || fail "gitlab missing page-2 MR (X-Next-Page pagination)"
grep -q "Total  | 2 |" "$OUT/gitlab.md" || fail "gitlab PR count wrong (page-1 duplicate?)"
test "$(grep -c 'MR one' "$OUT/gitlab.md")" -eq 1 || fail "gitlab page-1 MR duplicated"

export GS_FIXTURE_DIR="$FIX"

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

# Batch parallelism: explicit -j 2 (overriding the no-token serial default)
"$GS" analyze acme/gadget foo/bar $ARGS --no-cache -j 2 -d 7 -f md -q >/dev/null 2>&1 || fail "parallel batch failed"
[ -f "acme_gadget_report.md" ] || fail "parallel batch missing first report"
[ -f "foo_bar_report.md" ] || fail "parallel batch missing second report"
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

# Branch: analyze --branch shows Branch row in report
"$GS" analyze acme/gadget $ARGS --branch dev --no-cache -d 7 -f md -o "$OUT/branch.md" || fail "analyze --branch failed"
grep -q "Branch" "$OUT/branch.md" || fail "analyze --branch missing Branch label"
grep -q "dev" "$OUT/branch.md" || fail "analyze --branch missing branch value"

# Branch: watch add --branch + list shows Branch column
"$GS" watch $ARGS add acme/gadget --branch feature/x || fail "watch add --branch failed"
"$GS" watch $ARGS list | grep -q "feature/x" || fail "watch list missing branch"
"$GS" watch $ARGS list --json | grep -q '"branch": "feature/x"' || fail "watch list json missing branch"
"$GS" watch $ARGS remove acme/gadget || fail "watch remove branch"

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
TS1=$(epoch_days_ago 1)
TS2=$(epoch_days_ago 2)
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
