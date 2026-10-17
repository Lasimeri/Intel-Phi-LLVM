#!/usr/bin/env bash
# check-knc-gen.sh: the generated files of the patches must be what
# tools/knc-gen.c makes of isa/, byte for byte: X86InstrKNC.td and
# X86KNCInfo.inc in patch 0013 (from knc-isa.txt and knc-overrides.txt),
# and the code generation files in patches 0016 and 0017 (knc-gen --codegen,
# which also reads knc-ops.txt). Part of make check; needs a C compiler (CC,
# default cc). See check-knc-gen.md.
set -euo pipefail
. "$(cd "$(dirname "$0")" && pwd)/env.sh"
W="$llvm_build/knc-gen"; mkdir -p "$W/cg"
I="$llvm_root/isa"
"${CC:-cc}" -O2 -Wall -o "$W/knc-gen" "$llvm_root/tools/knc-gen.c"
"$W/knc-gen" "$I/knc-isa.txt" "$I/knc-overrides.txt" "$W/X86InstrKNC.td" "$W/X86KNCInfo.inc" 2> "$W/knc-gen.log"
"$W/knc-gen" --codegen "$I/knc-isa.txt" "$I/knc-overrides.txt" "$I/knc-ops.txt" "$W/cg" 2> "$W/knc-gen-cg.log"
P13="$llvm_root/patches/0013-x86-knc-mvex-instruction-table-and-encoder.patch"
P16="$llvm_root/patches/0016-x86-knc-code-generation-and-intrinsics.patch"
P17="$llvm_root/patches/0017-clang-knc-builtins-and-kncintrin-header.patch"
n=0
while read -r patch f gen; do
    b=$(basename "$f")
    # A new file in a git diff: the "+" lines of its one hunk.
    awk -v f="$f" '/^diff --git / { on = index($0, " b/" f) > 0; body = 0; next }
                   on && /^@@/ { body = 1; next }
                   on && body && /^\+/ { print substr($0, 2) }' "$patch" > "$W/patch.$b"
    if ! cmp -s "$W/patch.$b" "$gen"; then
        echo "check-knc-gen.sh: $b in $(basename "$patch") is not what tools/knc-gen.c makes of isa/" >&2
        diff "$W/patch.$b" "$gen" | head -20 >&2
        exit 1
    fi
    n=$((n + 1))
done <<LIST
$P13 llvm/lib/Target/X86/X86InstrKNC.td $W/X86InstrKNC.td
$P13 llvm/lib/Target/X86/MCTargetDesc/X86KNCInfo.inc $W/X86KNCInfo.inc
$P16 llvm/include/llvm/IR/IntrinsicsX86KNC.td $W/cg/IntrinsicsX86KNC.td
$P16 llvm/lib/Target/X86/X86InstrKNCIntrin.td $W/cg/X86InstrKNCIntrin.td
$P16 llvm/lib/Target/X86/X86KNCPseudos.inc $W/cg/X86KNCPseudos.inc
$P16 llvm/lib/Target/X86/X86KNCPassThru.inc $W/cg/X86KNCPassThru.inc
$P16 llvm/lib/Target/X86/X86KNCMemIntrinsics.inc $W/cg/X86KNCMemIntrinsics.inc
$P17 clang/include/clang/Basic/BuiltinsX86KNC.td $W/cg/BuiltinsX86KNC.td
$P17 clang/lib/Sema/SemaX86KNC.inc $W/cg/SemaX86KNC.inc
LIST
echo "check-knc-gen.sh: ok, $n generated files ($(tail -1 "$W/knc-gen.log" | sed 's/^knc-gen: //'); $(head -1 "$W/cg/codegen-report.txt" | sed 's/^knc-gen --codegen: //'))"
