#!/usr/bin/env sh
set -eu

RAW_LOG="/tmp/termcheck.raw.log"
CLEAN_LOG="/tmp/termcheck.clean.log"

# macOS/BSD script syntax: script -q <logfile> <command ...>
script -q "$RAW_LOG" sudo make test-termcheck-full AUXV6_LOG_USER=1 AUXV6_EXPECT_TIMEOUT=240

# Strip ANSI CSI sequences and normalize CR to NL.
perl -pe 's/\x1b\[[0-9;?]*[ -\/]*[@-~]//g; s/\r/\n/g' "$RAW_LOG" > "$CLEAN_LOG"

if command -v rg >/dev/null 2>&1; then
  rg -n 'FAIL:|PASS:|SKIP:|termcheck:' "$CLEAN_LOG"
else
  grep -nE 'FAIL:|PASS:|SKIP:|termcheck:' "$CLEAN_LOG"
fi

echo ""
echo "Clean log: $CLEAN_LOG"
