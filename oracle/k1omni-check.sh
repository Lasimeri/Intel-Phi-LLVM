#!/usr/bin/env bash
# k1omni-check.sh: assemble Intel's K1OM vector test (gas testsuite
# k1omni.s) with the oracle and compare bytes and text with k1omni.d.
# With LLVM_MC set, the S2 gate on that llvm-mc and the llvm-objdump and
# llvm-objcopy beside it: bytes against the oracle per instruction and per
# section, the disassembly round trip in AT&T and Intel syntax, and the
# disassembly text against the oracle's objdump. See k1omni-check.md.
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
[ -n "${LLVM_MC:-}" ] || exit 0

M=$(dirname "$LLVM_MC"); fail=0
MC=("$LLVM_MC" -triple=x86_64-unknown-linux-musl -mcpu=knc)
# 1. Object level: the whole file, disassembled by the oracle.
"${MC[@]}" -filetype=obj -o "$W/mc.o" "$T/k1omni.s"
"$O/build/binutils/objdump" -dw "$W/mc.o" > "$W/mc.dis"
awk -F'\t' '/^ +[0-9a-f]+:/ && NF>=3 {b=$2; gsub(/ +$/,"",b); print b}' "$W/mc.dis" > "$W/mc.txt"
cut -d'|' -f1 "$W/gas.txt" > "$W/gas.bytes"
paste -d'#' "$W/gas.bytes" "$W/mc.txt" | awk -F'#' '$1!=$2 {n++} END {printf "llvm-mc vs oracle: %d instructions, %d byte mismatches\n", NR, n+0; exit (n>0)}' || fail=1
# 2. Per source line and per "# Tests for" section (-show-encoding).
"${MC[@]}" -show-encoding "$T/k1omni.s" > "$W/mc.enc" 2> "$W/mc.err" || true
awk -v MAXSHOW=20 -f "$llvm_root/oracle/k1omni-sections.awk" "$T/k1omni.s" "$W/gas.bytes" "$W/mc.err" "$W/mc.enc" > "$W/sections.txt" 2> "$W/sections.detail"
echo "sections: $(grep -c . "$W/sections.txt") lines in $W/sections.txt; $(tail -1 "$W/sections.txt")"
grep -q ' 0 differ, 0 rejected' "$W/sections.txt" || fail=1
# 3. Round trip: llvm-objdump --mcpu=knc, assembled again, same .text.
"$M/llvm-objcopy" -O binary -j .text "$W/mc.o" "$W/mc.bin"
for syn in att intel; do
    opt=; hdr=; [ "$syn" = intel ] && { opt="-M intel"; hdr=".intel_syntax noprefix"; }
    { [ -n "$hdr" ] && echo "$hdr"; "$M/llvm-objdump" -d --mcpu=knc $opt --no-show-raw-insn --no-leading-addr "$W/mc.o" | awk '/^ +\t/ {sub(/^ +/, ""); print}'; } > "$W/rt-$syn.s"
    "${MC[@]}" -filetype=obj -o "$W/rt-$syn.o" "$W/rt-$syn.s" 2> "$W/rt-$syn.err" || true
    "$M/llvm-objcopy" -O binary -j .text "$W/rt-$syn.o" "$W/rt-$syn.bin" 2>/dev/null || : > "$W/rt-$syn.bin"
    if cmp -s "$W/mc.bin" "$W/rt-$syn.bin"; then r=identical; else r=DIFFERENT; fail=1; fi
    echo "round trip $syn: $(grep -c . "$W/rt-$syn.s") lines, $(grep -c 'error:' "$W/rt-$syn.err" || true) errors, .text $r ($(stat -c %s "$W/mc.bin") bytes)"
done
# 4. Text: llvm-objdump --mcpu=knc against the oracle's objdump on the same
# bytes, in a canonical form (oracle/disnorm.awk). Display differences
# only, so not a pass condition; the count and the classes are printed.
"$M/llvm-objdump" -d --mcpu=knc --print-imm-hex --no-show-raw-insn "$W/mc.o" | awk '/^ +[0-9a-f]+:/ {sub(/^ +[0-9a-f]+:[ \t]*/, ""); gsub(/[ \t]/, ""); print}' | awk -f "$llvm_root/oracle/disnorm.awk" > "$W/llvm.canon"
awk -F'\t' '/^ +[0-9a-f]+:/ && NF>=3 {t=$3; gsub(/[ \t]/,"",t); print t}' "$W/gas.dis" | awk -f "$llvm_root/oracle/disnorm.awk" > "$W/gas.canon"
paste -d'|' "$W/gas.canon" "$W/llvm.canon" | awk -F'|' '
  $1 == $2 { same++; next }
  { k = ($2 ~ /\{sae\}/ && $1 ~ /\{sae\}/) ? "{sae} with a swizzle (gas does not print the swizzle)" : ($1 ~ /ymm[0-9]+\{eh\}/ ? "half source with MVEX.E=1: gas {eh}, llvm {sae}" : ($1 ~ /^addr32/ ? "gas prints addr32 beside %eiz" : "other")); c[k]++ }
  END { printf "text vs oracle objdump: %d of %d identical in canonical form\n", same, NR; for (k in c) printf "  %d %s\n", c[k], k; exit (("other" in c) ? 1 : 0) }' || fail=1
exit $fail
