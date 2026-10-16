#!/usr/bin/env bash
# isa-extract.sh: regenerate isa/knc-isa.txt from Intel's instruction set
# reference (327364-001, in the family stack's vendor/docs) and compare it
# with the committed copy. The PDF is checked against its pinned SHA-256
# first. Needs pdftotext (poppler). See isa-extract.md.
set -euo pipefail
. "$(cd "$(dirname "$0")" && pwd)/env.sh"
PDF="$PHI_STACK_ROOT/vendor/docs/isa-reference-327364-001.pdf"
SUM=26a2e331c7bd904c774da8f5e397c732a5d4bc5eca1801e555ecba145f65a370
echo "$SUM  $PDF" | sha256sum -c --quiet - || { echo "isa-extract.sh: $PDF does not match the pinned SHA-256; refusing" >&2; exit 1; }
W="$llvm_build/isa"; mkdir -p "$W"
pdftotext -layout "$PDF" "$W/isa.txt"
awk -f "$llvm_root/isa/extract-isa.awk" "$W/isa.txt" > "$W/knc-isa.txt"
if cmp -s "$W/knc-isa.txt" "$llvm_root/isa/knc-isa.txt"; then
    echo "isa-extract.sh: isa/knc-isa.txt reproduced ($(grep -c . "$W/knc-isa.txt") records, pdftotext $(pdftotext -v 2>&1 | awk 'NR==1 {print $3}'))"
else
    echo "isa-extract.sh: the extraction differs from isa/knc-isa.txt:" >&2
    diff "$llvm_root/isa/knc-isa.txt" "$W/knc-isa.txt" | head -20 >&2
    exit 1
fi
