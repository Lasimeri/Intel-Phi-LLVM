#!/usr/bin/env bash
# asm-regress.sh: the assembly the card's runtimes are built from, under
# -mcpu=knc, through the KNC assembler path: every x86_64 .s and .S of
# musl, compiler-rt's builtins and libunwind, preprocessed once, then
# assembled by a reference llvm-mc (REF_MC, the series before the change)
# and by LLVM_MC. The objects must be byte-identical; a difference must be
# a Knights Corner form that can be named. See asm-regress.md.
set -euo pipefail
. "$(cd "$(dirname "$0")" && pwd)/env.sh"
MC=${LLVM_MC:?set LLVM_MC}; REF_MC=${REF_MC:?set REF_MC}
CLANG="$llvm_build/llvm/bin/clang"
MUSL="${PHI_MUSL_SRC:-$HOME/.cache/intel-phi-3120a-build/toolchain/musl-1.2.5}"
LP="$llvm_build/llvm-project"
W="$llvm_build/asm-regress"; rm -rf "$W"; mkdir -p "$W"
{ find "$MUSL/src" "$MUSL/crt" -path '*x86_64*' \( -name '*.s' -o -name '*.S' \)
  find "$LP/compiler-rt/lib/builtins/x86_64" -name '*.S'
  find "$LP/compiler-rt/lib/builtins" -maxdepth 1 -name '*.S'
  find "$LP/libunwind/src" -name '*.S'; } | sort -u > "$W/list"
n=0; same=0; diff=0; fail=0
while read -r f; do
    n=$((n + 1)); o="$W/$n"
    inc=(-I"$MUSL/arch/x86_64" -I"$MUSL/arch/generic" -I"$MUSL/src/include" -I"$MUSL/src/internal" -I"$MUSL/include" -I"$LP/libunwind/include" -I"$LP/compiler-rt/lib/builtins")
    case "$f" in
    *.s) cp "$f" "$o.s" ;; # plain assembly: no preprocessor
    *) if ! "$CLANG" --target=x86_64-unknown-linux-musl -march=knc -E -x assembler-with-cpp "${inc[@]}" -o "$o.s" "$f" 2>/dev/null; then
           echo "preprocess failed: $f"; fail=$((fail + 1)); continue
       fi ;;
    esac
    r=0; "$REF_MC" -triple=x86_64-unknown-linux-musl -mcpu=knc -filetype=obj -o "$o.ref.o" "$o.s" 2> "$o.ref.err" || r=1
    m=0; "$MC" -triple=x86_64-unknown-linux-musl -mcpu=knc -filetype=obj -o "$o.new.o" "$o.s" 2> "$o.new.err" || m=1
    if [ $r != $m ]; then echo "accepted by one only (ref $r, new $m): $f"; head -3 "$o.new.err" "$o.ref.err"; diff=$((diff + 1)); continue; fi
    [ $r = 1 ] && { echo "rejected by both: $f"; fail=$((fail + 1)); continue; }
    if cmp -s "$o.ref.o" "$o.new.o"; then same=$((same + 1)); else diff=$((diff + 1)); echo "object differs: $f"; fi
done < "$W/list"
echo "asm-regress: $n files (musl, compiler-rt builtins, libunwind), $same identical, $diff different, $fail not assembled by either"
[ "$diff" = 0 ]
