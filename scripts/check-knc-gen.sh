#!/usr/bin/env bash
# check-knc-gen.sh: the two generated files of patch 0013 (X86InstrKNC.td
# and X86KNCInfo.inc) must be what tools/knc-gen.c makes of isa/knc-isa.txt
# and isa/knc-overrides.txt, byte for byte. Part of make check; needs a C
# compiler (CC, default cc). See check-knc-gen.md.
set -euo pipefail
. "$(cd "$(dirname "$0")" && pwd)/env.sh"
W="$llvm_build/knc-gen"; mkdir -p "$W"
"${CC:-cc}" -O2 -Wall -o "$W/knc-gen" "$llvm_root/tools/knc-gen.c"
"$W/knc-gen" "$llvm_root/isa/knc-isa.txt" "$llvm_root/isa/knc-overrides.txt" "$W/X86InstrKNC.td" "$W/X86KNCInfo.inc" 2> "$W/knc-gen.log"
P="$llvm_root/patches/0013-x86-knc-mvex-instruction-table-and-encoder.patch"
for f in llvm/lib/Target/X86/X86InstrKNC.td llvm/lib/Target/X86/MCTargetDesc/X86KNCInfo.inc; do
    b=$(basename "$f")
    # A new file in a git diff: the "+" lines of its one hunk.
    awk -v f="$f" '/^diff --git / { on = index($0, " b/" f) > 0; body = 0; next }
                   on && /^@@/ { body = 1; next }
                   on && body && /^\+/ { print substr($0, 2) }' "$P" > "$W/patch.$b"
    if ! cmp -s "$W/patch.$b" "$W/$b"; then
        echo "check-knc-gen.sh: $b in patch 0013 is not what tools/knc-gen.c makes of isa/" >&2
        diff "$W/patch.$b" "$W/$b" | head -20 >&2
        exit 1
    fi
done
echo "check-knc-gen.sh: ok ($(tail -1 "$W/knc-gen.log" | sed 's/^knc-gen: //'))"
