#!/usr/bin/env bash
# mvex-card-check.sh: tests/mvex-probe.s assembled by this clang (-march=knc
# from config/knc.cfg, LLVM's integrated assembler) and by Intel's K1OM gas
# must give the same .text bytes; linked with tests/mvex-probe.c it must
# then run right on card CARD (default 0). See mvex-card-check.md.
set -uo pipefail
. "$(cd "$(dirname "$0")" && pwd)/env.sh"
CARD="${1:-0}"
B="$llvm_build/llvm/bin"; CFG=(--config="$llvm_root/config/knc.cfg" --sysroot="$llvm_build/sysroot" -O2)
GAS="$llvm_build/oracle/build/gas/as-new"
AUDIT="$PHI_STACK_ROOT/host/target/debug/phi-isa-audit"
w="$llvm_build/mvex-probe"; rm -rf "$w"; mkdir -p "$w"; fail=0
[ -x "$GAS" ] || { echo "mvex-card-check.sh: run oracle/build-binutils.sh first" >&2; exit 1; }
"$B/clang" "${CFG[@]}" -c -o "$w/probe.o" "$llvm_root/tests/mvex-probe.s" || exit 1
"$GAS" -march=k1om -o "$w/probe-gas.o" "$llvm_root/tests/mvex-probe.s" || exit 1
"$B/llvm-objcopy" -O binary -j .text "$w/probe.o" "$w/probe.text"
"$B/llvm-objcopy" -O binary -j .text "$w/probe-gas.o" "$w/probe-gas.text"
if cmp -s "$w/probe.text" "$w/probe-gas.text"; then
    echo "assembly: clang and K1OM gas .text identical ($(stat -c %s "$w/probe.text") bytes, $("$B/llvm-objdump" -d --mcpu=knc "$w/probe.o" | grep -c '^ *[0-9a-f]*:') instructions)"
else
    echo "assembly: clang and K1OM gas .text DIFFER"; cmp "$w/probe.text" "$w/probe-gas.text"; fail=1
fi
"$B/clang" "${CFG[@]}" -o "$w/mvex-probe" "$llvm_root/tests/mvex-probe.c" "$w/probe.o" || exit 1
"$AUDIT" "$w/mvex-probe" | tail -1 | sed 's/^/audit: /'
phi -c "$CARD" put "$w/mvex-probe" /data/llvm-knc-mvex-probe < /dev/null > /dev/null
phi -c "$CARD" run sh -c 'cd /data; chmod +x llvm-knc-mvex-probe; ./llvm-knc-mvex-probe; echo "exit $?"; rm -f llvm-knc-mvex-probe' < /dev/null 2>&1 | tee "$w/card.txt"
grep -q '^mvex-probe: PASS' "$w/card.txt" && grep -q '^exit 0' "$w/card.txt" || fail=1
[ "$fail" = 0 ] && echo "mvex-card-check: PASS (card $CARD)" || { echo "mvex-card-check: FAIL"; exit 1; }
