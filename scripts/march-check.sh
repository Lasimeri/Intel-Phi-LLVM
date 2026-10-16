#!/usr/bin/env bash
# march-check.sh: the S1 gate. -march=knc must give byte-identical objects
# to knc-cc's explicit feature flags, over musl's sources, the stack's card
# examples and its hello.c; the predefined macros and llc -mcpu=knc are
# checked too. See march-check.md.
set -euo pipefail
. "$(cd "$(dirname "$0")" && pwd)/env.sh"
CLANG="$llvm_build/llvm/bin/clang"; LLC="$llvm_build/llvm/bin/llc"
M="${PHI_MUSL_SRC:-$HOME/.cache/intel-phi-3120a-build/toolchain/musl-1.2.5}"
w="$llvm_build/march-check"; rm -rf "$w"; mkdir -p "$w/ref" "$w/new"
# knc-cc's flag set, minus the paths (Intel-Phi-3120A toolchain/clang/knc-cc).
REF=(--target=x86_64-unknown-linux-musl -march=x86-64
  -mno-mmx -mno-sse -mno-sse2 -mno-sse3 -mno-ssse3 -mno-sse4.1 -mno-sse4.2 -mno-sse4a
  -mno-avx -mno-avx2 -mno-fma -mno-f16c -mno-movbe -mno-popcnt -mno-lzcnt -mno-bmi -mno-bmi2
  -mno-xsave -mno-cx16 -mno-prfchw -mno-clflushopt -mno-rdrnd -mno-rdseed -mno-adx -mno-fsgsbase
  -mno-pclmul -mno-aes -mno-sha -mno-cmov -mno-nopl -fcf-protection=none -fno-jump-tables)
NEW=(--target=x86_64-unknown-linux-musl -march=knc -fcf-protection=none -fno-jump-tables)
MUSL=(-std=c99 -nostdinc -ffreestanding -fexcess-precision=standard -frounding-math
  -fno-strict-aliasing -D_XOPEN_SOURCE=700 -O2 -fomit-frame-pointer -fno-unwind-tables
  -fno-asynchronous-unwind-tables -ffunction-sections -fdata-sections -w
  -I"$M/arch/x86_64" -I"$M/arch/generic" -I"$M/obj/src/internal" -I"$M/src/include"
  -I"$M/src/internal" -I"$M/obj/include" -I"$M/include")
echo "== macros"
"$CLANG" "${NEW[@]}" -dM -E - < /dev/null | grep -E '__MIC__|__KNC__|__knc|__tune_knc|__SSE|__MMX' | sort | tee "$w/macros.txt"
grep -q '^#define __MIC__ 1$' "$w/macros.txt" && grep -q '^#define __KNC__ 1$' "$w/macros.txt" && ! grep -q '__SSE\|__MMX' "$w/macros.txt" || { echo "macros: FAIL"; exit 1; }
echo "macros: PASS"
echo "== llc -mcpu=knc, no -mattr"
printf 'define double @f(double %%a, double %%b) {\n  %%m = fmul double %%a, %%b\n  ret double %%m\n}\ndefine i64 @s(i64 %%a, i64 %%b, i1 %%c) {\n  %%r = select i1 %%c, i64 %%a, i64 %%b\n  ret i64 %%r\n}\n' > "$w/t.ll"
"$LLC" -mtriple=x86_64-unknown-linux-musl -mcpu=knc -O2 -o "$w/t.s" "$w/t.ll"
grep -E '^\s+[a-z]' "$w/t.s" | grep -v '^\s*\.' | tee "$w/t.txt"
if grep -qE 'xmm|cmov' "$w/t.txt" || ! grep -q 'fmul' "$w/t.txt"; then echo "llc: FAIL"; exit 1; fi
echo "llc: PASS (x87, no xmm, no cmov)"
echo "== objects: -march=knc against knc-cc's flags"
list="$w/list"
find "$M/src" -name "*.c" | awk -v m="$M/src/" '{r = substr($0, length(m) + 1); n = split(r, p, "/"); if (n == 2 || p[2] == "x86_64") print}' | sort > "$list"
for f in "$PHI_STACK_ROOT"/card/examples/*.c "$PHI_STACK_ROOT/toolchain/check/hello.c"; do echo "$f" >> "$list"; done
# One compile job per (kind, file); the flag sets are written into the job
# script so xargs can run it in parallel.
{
    echo "#!/usr/bin/env bash"
    printf "REF=(%s)\n" "$(printf "%q " "${REF[@]}")"
    printf "NEW=(%s)\n" "$(printf "%q " "${NEW[@]}")"
    printf "MUSL=(%s)\n" "$(printf "%q " "${MUSL[@]}")"
    printf "CLANG=%q M=%q w=%q SYS=%q\n" "$CLANG" "$M" "$w" "$llvm_build/sysroot"
    cat <<'JOB'
o=$(echo "$2" | md5sum | cut -c1-12)
case "$2" in "$M"/*) extra=("${MUSL[@]}") ;; *) extra=(-O2 --sysroot="$SYS") ;; esac
if [ "$1" = ref ]; then flags=("${REF[@]}"); else flags=("${NEW[@]}"); fi
"$CLANG" "${flags[@]}" "${extra[@]}" -c "$2" -o "$w/$1/$o.o" 2>/dev/null || echo "$2" >> "$w/$1.fail"
JOB
} > "$w/job.sh"; chmod +x "$w/job.sh"
while read -r f; do printf "%s\0%s\0" ref "$f"; printf "%s\0%s\0" new "$f"; done < "$list" | xargs -0 -n2 -P "${PHI_JOBS:-$(nproc)}" "$w/job.sh"
total=$(wc -l < "$list"); same=0; diff=0
for o in "$w"/ref/*.o; do b=$(basename "$o"); [ -f "$w/new/$b" ] || continue; if cmp -s "$o" "$w/new/$b"; then same=$((same+1)); else diff=$((diff+1)); echo "differs: $b" >> "$w/differs"; fi; done
echo "sources $total, compiled by both $((same+diff)), identical $same, different $diff, failed ref $( [ -f "$w/ref.fail" ] && wc -l < "$w/ref.fail" || echo 0), failed new $( [ -f "$w/new.fail" ] && wc -l < "$w/new.fail" || echo 0)"
[ "$diff" = 0 ] && cmp -s <(sort "$w/ref.fail" 2>/dev/null) <(sort "$w/new.fail" 2>/dev/null) && echo "objects: PASS" || { echo "objects: FAIL"; exit 1; }
