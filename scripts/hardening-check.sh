#!/usr/bin/env bash
# hardening-check.sh: each hardening candidate for -march=knc output
# (SECURITY.md) compiled with this clang and sysroot, inspected in the
# binary, and run on card CARD (default 0). See hardening-check.md.
set -uo pipefail
. "$(cd "$(dirname "$0")" && pwd)/env.sh"
CARD="${1:-0}"
# Everything comes from config/knc.cfg: this checks the shipped defaults.
B="$llvm_build/llvm/bin"; CFG=(--config="$llvm_root/config/knc.cfg" --sysroot="$llvm_build/sysroot" -O2)
CC=("$B/clang" "${CFG[@]}"); CXX=("$B/clang++" "${CFG[@]}")
AUDIT="$PHI_STACK_ROOT/host/target/debug/phi-isa-audit"
w="$llvm_build/hardening"; rm -rf "$w"; mkdir -p "$w"; fail=0
cat > "$w/ssp.c" <<'C'
#include <stdio.h>
#include <string.h>
__attribute__((noinline)) static int copy(const char *s) { char b[16]; strcpy(b, s); return b[0]; }
int main(int c, char **v) { printf("ssp %d\n", copy(c > 1 ? v[1] : "short")); return 0; }
C
cat > "$w/clash.c" <<'C'
#include <stdio.h>
__attribute__((noinline)) static int big(int n) { volatile char b[1 << 20]; for (unsigned i = 0; i < sizeof b; i += 4096) b[i] = (char)n; return b[0]; }
int main(void) { printf("clash %d\n", big(7)); return 0; }
C
cat > "$w/pie.c" <<'C'
#include <stdio.h>
static int g;
int main(void) { int l; printf("main %p data %p stack %p\n", (void *)main, (void *)&g, (void *)&l); return 0; }
C
"${CC[@]}" -o "$w/ssp" "$w/ssp.c"
"${CC[@]}" -o "$w/clash" "$w/clash.c"
"${CC[@]}" -o "$w/pie" "$w/pie.c"
"${CXX[@]}" -o "$w/cxx" "$llvm_build/rt-test/p.cc"
echo "== static checks"
"$B/llvm-nm" "$w/ssp" | grep -q ' __stack_chk_fail$' && echo "ssp: __stack_chk_fail linked" || { echo "ssp: no __stack_chk_fail"; fail=1; }
"$B/llvm-objdump" -d --no-show-raw-insn "$w/ssp" | awk '/<copy>:/,/ret/' | grep -q '%fs:0x28' && echo "ssp: canary read from %fs:0x28" || { echo "ssp: no canary load"; fail=1; }
probes=$("$B/llvm-objdump" -d --no-show-raw-insn "$w/clash" | awk '/<big>:/,/ret/' | grep -c 'movq	\$0x0, (%rsp)\|orq	\$0x0, (%rsp)')
[ "$probes" -gt 0 ] && echo "clash: $probes stack probe(s) in big()" || { echo "clash: no probes"; fail=1; }
"$B/llvm-readelf" -h "$w/pie" | grep -q 'DYN (Shared object file)' && echo "pie: ELF type DYN" || { echo "pie: not DYN"; fail=1; }
! "$B/llvm-readelf" -l "$w/pie" | grep -q INTERP && echo "pie: no PT_INTERP (static)" || { echo "pie: has INTERP"; fail=1; }
"$B/llvm-readelf" -l "$w/pie" | grep -q GNU_RELRO && echo "pie: PT_GNU_RELRO" || { echo "pie: no RELRO"; fail=1; }
"$B/llvm-readelf" -d "$w/pie" | grep -q 'BIND_NOW\|FLAGS_1.*NOW' && echo "pie: BIND_NOW" || { echo "pie: no BIND_NOW"; fail=1; }
"$B/llvm-readelf" -l "$w/pie" | awk '/GNU_STACK/ {print "pie: GNU_STACK flags " $(NF-1); if ($(NF-1) ~ /E/) exit 1}' || fail=1
for x in ssp clash pie cxx; do "$AUDIT" "$w/$x" | tail -1 | sed "s/^/$x audit: /"; done
echo "== card $CARD"
for x in ssp clash pie cxx; do phi -c "$CARD" put "$w/$x" "/data/llvm-knc-h-$x" > /dev/null; done
phi -c "$CARD" run sh -c '
cd /data; chmod +x llvm-knc-h-*
echo "card cpu nx flag: $(grep -m1 -o -w nx /proc/cpuinfo || echo absent)"
echo "card randomize_va_space: $(cat /proc/sys/kernel/randomize_va_space)"
./llvm-knc-h-ssp; echo "ssp short: exit $?"
./llvm-knc-h-ssp AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA; echo "ssp overflow: exit $?"
./llvm-knc-h-clash; echo "clash: exit $?"
./llvm-knc-h-pie; ./llvm-knc-h-pie; echo "pie: exit $?"
./llvm-knc-h-cxx; echo "c++: exit $?"
rm -f llvm-knc-h-ssp llvm-knc-h-clash llvm-knc-h-pie llvm-knc-h-cxx' 2>&1 | tee "$w/card.txt"
grep -q 'ssp short: exit 0' "$w/card.txt" || fail=1
grep -q 'ssp overflow: exit 0' "$w/card.txt" && fail=1
grep -q 'clash: exit 0' "$w/card.txt" || fail=1
grep -q 'c++: exit 0' "$w/card.txt" || fail=1
for x in ssp clash cxx; do "$B/llvm-readelf" -h "$w/$x" | grep -q 'DYN (Shared' && echo "$x: static PIE" || { echo "$x: not PIE"; fail=1; }; done
[ "$(grep -c '^main 0x' "$w/card.txt")" = 2 ] && [ "$(grep '^main 0x' "$w/card.txt" | sort -u | wc -l)" = 2 ] && echo "pie: two runs, two different addresses" || { echo "pie: addresses did not change (or no output)"; fail=1; }
[ "$fail" = 0 ] && echo "hardening-check: PASS" || { echo "hardening-check: FAIL"; exit 1; }
