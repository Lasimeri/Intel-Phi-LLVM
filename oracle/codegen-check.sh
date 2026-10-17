#!/usr/bin/env bash
# codegen-check.sh: every Knights Corner instruction the compiler emitted
# into the given objects, reassembled from its printed text by MPSS gas
# (the oracle), must give the same bytes. Code generation builds MCInsts
# without the assembler's checks, so this is what catches an encoding the
# compiler should never have produced. See codegen-check.md.
#
#   oracle/codegen-check.sh OBJECT...
#   LLVM_OBJDUMP   the llvm-objdump to disassemble with
#                  (default: the series build's)
set -euo pipefail
. "$(cd "$(dirname "$0")/.." && pwd)/scripts/env.sh"
[ $# -gt 0 ] || { echo "usage: oracle/codegen-check.sh OBJECT..." >&2; exit 2; }
OBJDUMP="${LLVM_OBJDUMP:-$llvm_build/llvm/bin/llvm-objdump}"
O="$llvm_build/oracle"
[ -x "$O/build/gas/as-new" ] || { echo "codegen-check.sh: run oracle/build-binutils.sh first" >&2; exit 1; }
W="$llvm_build/oracle/codegen"; mkdir -p "$W"

# One line per KNC instruction (a zmm or mask register operand, or a k*
# mnemonic): object|address|bytes|text.
for obj in "$@"; do
    "$OBJDUMP" -d --no-print-imm-hex --mcpu=knc "$obj" |
      awk -v obj="$(basename "$obj")" -F'\t' '
        /^ *[0-9a-f]+: / && NF >= 2 {
          t = $2; for (i = 3; i <= NF; i++) t = t " " $i; sub(/ *#.*/, "", t)
          if (t !~ /%zmm|%k[0-7]/ && t !~ /^k[a-z0-9]+ /) next
          a = $1; sub(/^ */, "", a); b = a; sub(/:.*/, "", a); sub(/^[0-9a-f]+: /, "", b)
          gsub(/[ \t]+$/, "", b)
          print obj "|" a "|" b "|" t }'
done > "$W/insns.txt"
n=$(wc -l < "$W/insns.txt")
[ "$n" -gt 0 ] || { echo "codegen-check: no KNC instructions in $*"; exit 1; }

# A rip-relative operand disassembles with the relocation's addend folded
# in; gas assembles the same 32-bit field from "(%rip)".
awk -F'|' '{ t = $4; gsub(/[-0-9a-fx]+\(%rip\)/, "(%rip)", t); print t }' "$W/insns.txt" > "$W/insns.s"
"$O/build/gas/as-new" -march=k1om -o "$W/gas.o" "$W/insns.s" 2> "$W/gas.err" || true
errors=$(grep -c 'Error' "$W/gas.err" || true)
"$O/build/binutils/objdump" -dw "$W/gas.o" |
  awk -F'\t' '/^ *[0-9a-f]+:\t/ { b = $2; gsub(/[ \t]+$/, "", b); print b }' > "$W/gas.bytes"
paste -d'|' "$W/insns.txt" "$W/gas.bytes" | awk -F'|' -v errors="$errors" '
  $3 != $5 { n++; if (n <= 10) printf "differ %s+%s: %s\n  clang %s\n  gas   %s\n", $1, $2, $4, $3, $5 }
  END { printf "codegen vs gas: %d KNC instructions, %d byte mismatches, %d gas errors\n", NR, n + 0, errors
        exit (n > 0 || errors > 0) }'
