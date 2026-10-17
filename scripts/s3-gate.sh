#!/usr/bin/env bash
# s3-gate.sh: the S3 gate for one of the stack's vector kernels. Intel-Phi-
# 3120A's card/examples/NAME.S (knc-mvex .byte lines) is assembled with its
# symbols prefixed ref_; tests/s3/NAME.c (the same kernels from the Knights
# Corner intrinsics) is compiled by this clang; tests/s3/NAME-gate.c runs both
# on card CARD (default 0): output byte for byte, speed interleaved. Every
# KNC instruction the compiler emitted is first checked against Intel's gas
# (oracle/codegen-check.sh) and the binary against the stack's audit. See
# s3-gate.md.
#
#   scripts/s3-gate.sh NAME [CARD]
#   LLVM_BIN   the clang to test (default: the series build's)
#   S3_OPT     the optimization level of the candidate (default -O2; at -O0
#              only the output check means anything)
set -uo pipefail
. "$(cd "$(dirname "$0")" && pwd)/env.sh"
NAME="${1:?usage: scripts/s3-gate.sh NAME [CARD]}"; CARD="${2:-0}"
B="${LLVM_BIN:-$llvm_build/llvm/bin}"; S="$llvm_build/llvm/bin"
T="$llvm_root/tests/s3"; REF="$PHI_STACK_ROOT/card/examples/$NAME.S"
CFG=(--config="$llvm_root/config/knc.cfg" --sysroot="$llvm_build/sysroot" -O2)
AUDIT="$PHI_STACK_ROOT/host/target/debug/phi-isa-audit"
w="$llvm_build/s3/gate-$NAME"; rm -rf "$w"; mkdir -p "$w"
for f in "$T/$NAME.c" "$T/$NAME-gate.c" "$REF"; do [ -f "$f" ] || { echo "s3-gate.sh: $f missing" >&2; exit 1; }; done

"$B/clang" "${CFG[@]}" ${S3_OPT:-} -c -o "$w/$NAME.o" "$T/$NAME.c" || exit 1
LLVM_OBJDUMP="$B/llvm-objdump" "$llvm_root/oracle/codegen-check.sh" "$w/$NAME.o" || exit 1
# the reference: assembled as the stack assembles it, symbols renamed
"$S/clang" "${CFG[@]}" -c -o "$w/ref.o" "$REF" || exit 1
"$S/llvm-objcopy" --prefix-symbols=ref_ "$w/ref.o"
"$S/clang" "${CFG[@]}" -o "$w/$NAME-gate" "$T/$NAME-gate.c" "$w/$NAME.o" "$w/ref.o" || exit 1
"$AUDIT" "$w/$NAME-gate" | tail -1 | sed 's/^/audit: /'
"$AUDIT" "$w/$NAME-gate" | tail -1 | grep -q '^result: 0 illegal' || { echo "s3-gate: audit found illegal instructions"; exit 1; }
phi -c "$CARD" put "$w/$NAME-gate" "/data/s3-$NAME-gate" < /dev/null > /dev/null
phi -c "$CARD" run sh -c "cd /data; chmod +x s3-$NAME-gate; ./s3-$NAME-gate; echo \"exit \$?\"; rm -f s3-$NAME-gate" < /dev/null 2>&1 | tee "$w/card$CARD.txt"
grep -q "^$NAME-gate: PASS" "$w/card$CARD.txt" && grep -q '^exit 0' "$w/card$CARD.txt"
