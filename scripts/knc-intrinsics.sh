#!/usr/bin/env bash
# knc-intrinsics.sh: which of Intel's Knights Corner intrinsics the header
# of patch 0017 (kncintrin.h) provides. Intel's list is the Intrinsics
# Guide data 3.5.3, the last version that has KNC (KNCNI), from the Internet
# Archive's copy, SHA-256 pinned: names and prototypes only, nothing of it is
# copied into the patches. Prints the counts and writes the missing names to
# $llvm_build/intrin/missing.txt. See knc-intrinsics.md.
set -euo pipefail
. "$(cd "$(dirname "$0")" && pwd)/env.sh"
URL=https://web.archive.org/web/2020id_/https://software.intel.com/sites/landingpage/IntrinsicsGuide/files/data-3.5.3.xml
SUM=90fe27107115cce5d2813c9fcc5db76f0363fa55209054cfa26935eb9be43b3a
D="$llvm_build/intrin"; mkdir -p "$D"; X="$D/data-3.5.3.xml"
[ -f "$X" ] || curl -sfL --max-time 600 -o "$X" "$URL"
echo "$SUM  $X" | sha256sum -c --quiet - || { echo "knc-intrinsics.sh: $X does not match the pinned SHA-256; refusing" >&2; exit 1; }

# Intel's KNC names: name, then the number of instructions listed (0 = a
# sequence or a library function, not one instruction)
awk '/<intrinsic / { match($0, /name="[^"]*"/); n = substr($0, RSTART + 6, RLENGTH - 7); k = 0; ins = 0 }
     /<CPUID>.*KNCNI/ { k = 1 }
     /<instruction / { ins++ }
     /<\/intrinsic>/ { if (k) print n, ins }' "$X" | sort -u > "$D/intel.txt"

# the header as the series ships it: the new file in patch 0017
P="$llvm_root/patches/0017-clang-knc-builtins-and-kncintrin-header.patch"
awk '/^diff --git / { on = index($0, " b/clang/lib/Headers/kncintrin.h") > 0; body = 0; next }
     on && /^@@/ { body = 1; next }
     on && body && /^\+/ { print substr($0, 2) }' "$P" > "$D/kncintrin.h"
# defined names: functions (_mm512_x( at the start of a declaration line, or
# after the return type), macros, and the __KNC_OP2/__KNC_FMA expansions
{ grep -oE '^(static __inline__ [^(]* )?_mm512_[a-z0-9_]+\(' "$D/kncintrin.h" | grep -oE '_mm512_[a-z0-9_]+'
  grep -oE '^#define _mm512_[a-z0-9_]+' "$D/kncintrin.h" | sed 's/^#define //'
  awk '/^__KNC_OP2\(/ { split(substr($0, 11), a, ","); print "_mm512_" a[1]; print "_mm512_mask_" a[1] }
       /^__KNC_FMA\(/ { split(substr($0, 11), a, ","); print "_mm512_" a[1]; print "_mm512_mask_" a[1]; print "_mm512_mask3_" a[1] }' "$D/kncintrin.h"
} | sort -u > "$D/header.txt"

total=$(wc -l < "$D/intel.txt")
provided=$(awk 'NR == FNR { h[$1] = 1; next } h[$1]' "$D/header.txt" "$D/intel.txt" | wc -l)
awk 'NR == FNR { h[$1] = 1; next } !h[$1]' "$D/header.txt" "$D/intel.txt" > "$D/missing.txt"
one=$(awk '$2 == 1' "$D/missing.txt" | wc -l); seq=$(awk '$2 == 0' "$D/missing.txt" | wc -l); multi=$(awk '$2 > 1' "$D/missing.txt" | wc -l)
extra=$(awk 'NR == FNR { i[$1] = 1; next } !i[$1]' "$D/intel.txt" "$D/header.txt" | wc -l)
echo "Intel's KNC intrinsics (data 3.5.3): $total; kncintrin.h provides $provided"
echo "not provided: $one with one instruction, $multi with several, $seq sequences or library functions (no instruction listed)"
echo "kncintrin.h also defines $extra names that 3.5.3 does not list for KNC (setzero, set1, ... classed AVX-512 there)"
