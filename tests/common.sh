# Helpers shared by the test scripts.
#
# macOS is the awkward platform: its date(1) is the BSD one, which has no -d
# and spells "N days ago" as -v-Nd, and it ships no timeout(1) at all. Both are
# wrapped here so the scripts themselves can stay plain and keep working on
# Linux.

# N days ago, as ISO-8601 UTC, as YYYY-MM-DD, and as epoch seconds.
if date -u -d '1 day ago' +%Y-%m-%dT%H:%M:%SZ >/dev/null 2>&1; then
    iso_days_ago()      { date -u -d "$1 days ago" +%Y-%m-%dT%H:%M:%SZ; }
    iso_date_days_ago() { date -u -d "$1 days ago" +%Y-%m-%d; }
    epoch_days_ago()    { date    -d "$1 days ago" +%s; }
else
    iso_days_ago()      { date -u -v-"$1"d +%Y-%m-%dT%H:%M:%SZ; }
    iso_date_days_ago() { date -u -v-"$1"d +%Y-%m-%d; }
    epoch_days_ago()    { date    -v-"$1"d +%s; }
fi

# A wall-clock limit on a command. macOS has no timeout(1) -- Homebrew ships it
# as gtimeout -- so enforce the limit with a watchdog that kills the child.
if ! command -v timeout >/dev/null 2>&1; then
    timeout() {
        local secs=$1; shift
        "$@" &
        local job=$!
        ( sleep "$secs"; kill "$job" 2>/dev/null ) >/dev/null 2>&1 &
        local guard=$!
        wait "$job" 2>/dev/null
        local rc=$?
        kill "$guard" 2>/dev/null
        return "$rc"
    }
fi
