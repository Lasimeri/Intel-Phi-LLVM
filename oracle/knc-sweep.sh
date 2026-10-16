#!/usr/bin/env bash
# knc-sweep.sh: every Knights Corner def against Intel's K1OM gas, with
# registers rotated through every operand role and every decoration of its
# tables (tools/knc-gen.c's sweep output). Lines gas rejects are counted
# and commented out; the rest must match llvm-mc byte for byte. With
# LLVM_MC set. See knc-sweep.md.
set -euo pipefail
. "$(cd "$(dirname "$0")/.." && pwd)/scripts/env.sh"
: "${LLVM_MC:?set LLVM_MC to the llvm-mc to check}"
O="$llvm_build/oracle"; GAS="$O/build/gas/as-new"; OBJDUMP="$O/build/binutils/objdump"
W="$O/sweep"; mkdir -p "$W"
[ -x "$GAS" ] || { echo "knc-sweep.sh: run oracle/build-binutils.sh first" >&2; exit 1; }
"${CC:-cc}" -O2 -Wall -o "$W/knc-gen" "$llvm_root/tools/knc-gen.c"
"$W/knc-gen" "$llvm_root/isa/knc-isa.txt" "$llvm_root/isa/knc-overrides.txt" "$W/td" "$W/inc" "$W/listing" "$W/sweep.s" 2>/dev/null
# Pass 1: the lines gas rejects.
"$GAS" -march=k1om -o "$W/gas1.o" "$W/sweep.s" 2> "$W/gas1.err" || true
awk -F: '/: Error: / {print $2}' "$W/gas1.err" | sort -un > "$W/rejected.lines"
awk 'NR == FNR {bad[$1] = 1; next} (FNR in bad) {print "# gas rejected: " $0; next} {print}' "$W/rejected.lines" "$W/sweep.s" > "$W/sweep2.s"
# Pass 2: gas must take the rest.
"$GAS" -march=k1om -o "$W/gas.o" "$W/sweep2.s"
"$OBJDUMP" -dw "$W/gas.o" | awk -F'\t' '/^ +[0-9a-f]+:/ && NF>=3 {b=$2; gsub(/ +$/,"",b); print b}' > "$W/gas.bytes"
"$LLVM_MC" -triple=x86_64-unknown-linux-musl -mcpu=knc -show-encoding "$W/sweep2.s" > "$W/mc.enc" 2> "$W/mc.err" || true
awk -v MAXSHOW=20 -f "$llvm_root/oracle/k1omni-sections.awk" "$W/sweep2.s" "$W/gas.bytes" "$W/mc.err" "$W/mc.enc" > "$W/sections.txt" 2> "$W/sections.detail"
defs=$(grep -c '^# def ' "$W/sweep.s"); clean=$(awk '$1 == $2 && $1 > 0' "$W/sections.txt" | grep -c ' def ' || true)
echo "knc-sweep: $defs defs; $(wc -l < "$W/rejected.lines") lines rejected by gas (in $W/gas1.err); $(tail -1 "$W/sections.txt" | sed 's/^TOTAL //')"
echo "knc-sweep: $clean of $defs defs with every line identical"
# Acceptance: each line gas rejects, alone, must be rejected by llvm-mc too,
# except %ymm16 to %ymm31 (gas 2.22 has no such names; they are the low
# halves of zmm16 to zmm31 and assemble as such).
acc=0; known=0; : > "$W/accepted.txt"
while read -r n; do
    l=$(sed -n "${n}p" "$W/sweep.s")
    if printf '%s\n' "$l" | "$LLVM_MC" -triple=x86_64-unknown-linux-musl -mcpu=knc -filetype=null 2>/dev/null; then
        case "$l" in *%ymm1[6-9]*|*%ymm2[0-9]*|*%ymm3[01]*) known=$((known + 1)) ;; *) acc=$((acc + 1)); echo "$l" >> "$W/accepted.txt" ;; esac
    fi
done < "$W/rejected.lines"
echo "knc-sweep: of the lines gas rejects, llvm-mc accepts $known (%ymm16-31, known) and $acc others"
head -10 "$W/accepted.txt"
[ "$acc" = 0 ] || exit 1
grep -q ' 0 differ, 0 rejected' "$W/sections.txt"
