#!/usr/bin/env bash
# mc-regress.sh: the Knights Corner MC changes must leave every other x86
# encoding alone. compiler-rt's builtins are compiled to assembly by the
# distro clang for several x86 levels (baseline to AVX-512 and APX), then
# assembled by a reference llvm-mc (REF_MC, the series before the change)
# and by the one under test (LLVM_MC); the objects must be byte-identical,
# and the two llvm-objdumps must print the same text for them; the baseline
# set is also assembled with -mcpu=knc, where AVX forms of the mnemonics
# KNC shares are rejected by design, so it carries no AVX. See mc-regress.md.
set -euo pipefail
. "$(cd "$(dirname "$0")/.." && pwd)/scripts/env.sh"
MC=${LLVM_MC:?set LLVM_MC}; OBJDUMP=${LLVM_OBJDUMP:?set LLVM_OBJDUMP}
REF_MC=${REF_MC:?set REF_MC}; REF_OBJDUMP=${REF_OBJDUMP:?set REF_OBJDUMP}
W="$llvm_build/mc-regress"; rm -rf "$W"; mkdir -p "$W"
SRC="$llvm_build/llvm-project/compiler-rt/lib/builtins"
marches="x86-64 x86-64-v3 x86-64-v4 znver4 sapphirerapids"
for m in $marches; do
  mkdir -p "$W/$m"; extra=; [ "$m" = sapphirerapids ] && extra=-mapxf
  ls "$SRC"/*.c | xargs -P 64 -I{} sh -c 'b=$(basename "$1" .c); /usr/bin/clang --target=x86_64-unknown-linux-gnu -march=$2 $3 -O2 -S -I"$4" -o "$5/$b.s" "$1" 2>/dev/null || rm -f "$5/$b.s"' _ {} "$m" "$extra" "$SRC" "$W/$m"
done
n=0; bad=0; dbad=0
for s in "$W"/*/*.s; do
  cpus=""; case "$s" in "$W"/x86-64/*) cpus=knc ;; esac
  for cpu in "" $cpus; do
    o=${s%.s}${cpu:+.$cpu}; flag=${cpu:+-mcpu=$cpu}; dflag=${cpu:+--mcpu=$cpu}
    "$REF_MC" -triple=x86_64-unknown-linux-gnu $flag -filetype=obj -o "$o.ref.o" "$s" 2>/dev/null || continue
    "$MC" -triple=x86_64-unknown-linux-gnu $flag -filetype=obj -o "$o.new.o" "$s"
    n=$((n + 1))
    cmp -s "$o.ref.o" "$o.new.o" || { bad=$((bad + 1)); echo "object differs: $s $flag"; }
    "$REF_OBJDUMP" -d $dflag "$o.ref.o" | tail -n +3 > "$o.ref.dis"
    "$OBJDUMP" -d $dflag "$o.ref.o" | tail -n +3 > "$o.new.dis"
    cmp -s "$o.ref.dis" "$o.new.dis" || { dbad=$((dbad + 1)); echo "disassembly differs: $s $flag"; }
  done
done
echo "mc-regress: $n assemblies ($(ls "$W"/*/*.s | wc -l) files x default, x86-64 also -mcpu=knc; $marches): $bad objects differ, $dbad disassemblies differ"
[ "$bad" = 0 ] && [ "$dbad" = 0 ]
