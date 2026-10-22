#!/usr/bin/env bash
# Times an analysis at a known size, offline.
#
# The fixture mode short-circuits the network, so what is measured is the
# work the engine does: parsing, analysing and exporting. Run it before and
# after a change to see whether the change actually paid off.
#
# Usage: bash tools/bench.sh [items-per-kind] [formats...]

set -eu

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
GS="${GS:-$ROOT/build/gs}"
COUNT="${1:-2000}"
shift || true
FORMATS=("$@")
[ ${#FORMATS[@]} -eq 0 ] && FORMATS=(md json html csv xlsx pdf)

WORK="$(mktemp -d)"
trap 'rm -rf "$WORK"' EXIT

python3 "$ROOT/tools/make_fixture.py" "$WORK/fixtures" "$COUNT" >/dev/null

export GS_FIXTURE_DIR="$WORK/fixtures"
export GS_CACHE_DIR="$WORK/cache"
export XDG_CONFIG_HOME="$WORK/xdg"
export NO_COLOR=1
mkdir -p "$XDG_CONFIG_HOME/gitsight"
cat > "$XDG_CONFIG_HOME/gitsight/config.json" <<'EOF'
{"default_days":7,"platform":"github","lang":"en","export":{"item_limit":10}}
EOF

printf '%-6s %10s %10s %10s %10s %12s\n' \
    format fetch analyze export total "maxrss"
printf '%s\n' "--------------------------------------------------------"

for fmt in "${FORMATS[@]}"; do
    out="$WORK/out.$fmt"
    # The timings go to stderr; the report itself to the file.
    perf=$(GS_PERF=1 "$GS" analyze acme/gadget --no-cache -q -d 7 \
        --max-pages 20 -f "$fmt" -o "$out" 2>&1 >/dev/null || true)

    # The last field is the unit, so the number is the one before it: a mark
    # whose name is two words cannot be mistaken for a value.
    fetch=$(printf '%s' "$perf" | awk '$1 == "fetch" && $NF == "ms" {print $(NF-1)}')
    fetched=$(printf '%s' "$perf" | awk '$1 == "fetched" && $NF == "ms" {print $(NF-1)}')
    analyze=$(printf '%s' "$perf" | awk '$1 == "analyze" && $NF == "ms" {print $(NF-1)}')
    export_ms=$(printf '%s' "$perf" | awk '$1 == "export" && $NF == "ms" {print $(NF-1)}')
    total=$(printf '%s' "$perf" | awk '$1 == "total" && $NF == "ms" {print $(NF-1)}')

    # The fetch phase is reported as two marks: the setup before the workers
    # start and the wall time until the last one is done.
    fetch_total=$(printf '%s' "$perf" | awk '$NF == "ms" {v=$(NF-1)} $1 == "fetch" {f=v} $1 == "fetched" {print f+v}')

    rss=$(/usr/bin/time -f '%M' "$GS" analyze acme/gadget --no-cache -q -d 7 \
        --max-pages 20 -f "$fmt" -o "$out" 2>&1 >/dev/null | tail -1)

    printf '%-6s %9sms %9sms %9sms %9sms %10s kB\n' \
        "$fmt" "${fetch_total:-?}" "${analyze:-?}" "${export_ms:-?}" "${total:-?}" "${rss:-?}"
done

printf '\n%d items per resource kind, offline (no network).\n' "$COUNT"
