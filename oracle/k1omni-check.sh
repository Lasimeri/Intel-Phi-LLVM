#!/usr/bin/env bash
# k1omni-check.sh: assemble Intel's K1OM vector test (gas testsuite
# k1omni.s) with the oracle and compare bytes and text with k1omni.d.
# With LLVM_MC set, also assemble it with llvm-mc -mcpu=knc and compare
# bytes with the oracle (the S2 gate). See k1omni-check.md.
set -euo pipefail
. "$(cd "$(dirname "$0")/.." && pwd)/scripts/env.sh"
O="$llvm_build/oracle"
T="$O/src/binutils-2.22+mpss3.8.6/gas/testsuite/gas/i386"
W="$O/check"; mkdir -p "$W"
[ -x "$O/build/gas/as-new" ] || { echo "k1omni-check.sh: run oracle/build-binutils.sh first" >&2; exit 1; }
"$O/build/gas/as-new" -march=k1om -o "$W/gas.o" "$T/k1omni.s"
"$O/build/binutils/objdump" -dw "$W/gas.o" > "$W/gas.dis"
# Fields: the .d pattern "[ \t]*" holds a tab, so bytes are $3, text $4;
# objdump output has bytes $2, text $3.
awk -F'\t' '/^\[/ && NF>=4 {b=$3; t=$4; gsub(/ +$/,"",b); gsub(/\\/,"",t); gsub(/ +/," ",t); print b "|" t}' "$T/k1omni.d" > "$W/exp.txt"
awk -F'\t' '/^ +[0-9a-f]+:/ && NF>=3 {b=$2; t=$3; gsub(/ +$/,"",b); gsub(/ +/," ",t); print b "|" t}' "$W/gas.dis" > "$W/gas.txt"
paste -d'#' "$W/exp.txt" "$W/gas.txt" | awk -F'#' '{split($1,a,"|"); split($2,b,"|"); if (a[1]!=b[1]) nb++} END {printf "oracle vs k1omni.d: %d instructions, %d byte mismatches\n", NR, nb+0}'
if [ -n "${LLVM_MC:-}" ]; then
    "$LLVM_MC" -triple=x86_64-unknown-linux-musl -mcpu=knc -filetype=obj -o "$W/mc.o" "$T/k1omni.s"
    "$O/build/binutils/objdump" -dw "$W/mc.o" > "$W/mc.dis"
    awk -F'\t' '/^ +[0-9a-f]+:/ && NF>=3 {b=$2; gsub(/ +$/,"",b); print b}' "$W/mc.dis" > "$W/mc.txt"
    cut -d'|' -f1 "$W/gas.txt" > "$W/gas.bytes"
    paste -d'#' "$W/gas.bytes" "$W/mc.txt" | awk -F'#' '$1!=$2 {n++} END {printf "llvm-mc vs oracle: %d instructions, %d byte mismatches\n", NR, n+0; exit (n>0)}'
fi
