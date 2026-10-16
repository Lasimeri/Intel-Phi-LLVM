#!/usr/bin/env bash
# check-series.sh: patches/SERIES is consistent: a tag line, a 40-hex
# commit line, every listed patch exists, every patch file is listed once.
# See check-series.md.
set -uo pipefail
cd "$(dirname "$0")/.."
s=patches/SERIES; fail=0
grep -qE '^# LLVM tag: llvmorg-[0-9]+\.[0-9]+\.[0-9]+$' "$s" || { echo "check-series.sh: no tag line in $s"; fail=1; }
grep -qE '^# LLVM commit: [0-9a-f]{40}$' "$s" || { echo "check-series.sh: no 40-hex commit line in $s"; fail=1; }
listed=$(grep -vE '^(#|$)' "$s")
for p in $listed; do [ -f "patches/$p" ] || { echo "check-series.sh: $p listed but missing"; fail=1; }; done
for f in patches/*.patch; do
    n=$(echo "$listed" | grep -cxF "${f#patches/}")
    [ "$n" = 1 ] || { echo "check-series.sh: ${f#patches/} listed $n times"; fail=1; }
done
[ "$fail" = 0 ] && echo "check-series.sh: ok ($(echo "$listed" | wc -l) patches)"
exit "$fail"
