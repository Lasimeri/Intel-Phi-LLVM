# scripts/knc-intrinsics.sh

How much of Intel's Knights Corner intrinsic API `kncintrin.h` (patch
0017) provides. Intel's list is the Intel Intrinsics Guide data 3.5.3,
the last version that lists KNC (`KNCNI`); the current one (3.6.9) and the
archived 3.4.x and 3.5.4 copies do not. It is fetched once from the
Internet Archive and checked against its SHA-256; only the names and the
number of instructions per intrinsic are read, and nothing of it goes
into a patch.

```
scripts/knc-intrinsics.sh
```

Measured 2026-10-16: Intel lists 595 distinct names (597 entries);
`kncintrin.h` provides 119, and 12 more that 3.5.3 classes as AVX-512
only (`_mm512_setzero_ps`, `_mm512_set1_epi32`, ...). Not provided: 333
names with one instruction, 58 with several, 85 sequences or library
functions (no instruction listed). The missing names are in
`$llvm_build/intrin/missing.txt`. The builtins exist for 297 of the 330
instruction forms (`knc-gen --codegen`), so a missing one-instruction name
is a header line, not compiler work.
