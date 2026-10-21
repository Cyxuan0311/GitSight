#!/usr/bin/env bash
# Runs the web console's test suite on a native filesystem.
#
# The suite is I/O-bound: on a WSL /mnt/... mount, reading recharts alone
# takes 42 seconds and the Vitest worker times out mid-transform. This copies
# the workspace to /tmp (native fs) and runs there, which takes seconds.
#
# Usage: bash tools/web_test.sh [vitest args...]

set -eu

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
WS=/tmp/gitsight-webtest

mkdir -p "$WS"
if [ ! -d "$WS/node_modules" ]; then
  cp -r "$ROOT/web/node_modules" "$WS/node_modules"
fi
rm -rf "$WS/src"
cp -r "$ROOT/web/src" "$WS/src"
for f in index.html package.json package-lock.json vite.config.ts vitest.config.ts tsconfig.json eslint.config.js; do
  [ -f "$ROOT/web/$f" ] && cp "$ROOT/web/$f" "$WS/$f"
done

cd "$WS"
exec npx vitest run "$@"
