# tests/s3

The S3 gate: the stack's hand-encoded vector kernels (Intel-Phi-3120A's
`card/examples/*.S` and `card/lib/knc`, `.byte` lines from `knc-mvex-gen`)
written again in C with the Knights Corner intrinsics (`<immintrin.h>`
under `-march=knc`, patch 0017), compiled by this clang, and linked into
one binary with the originals, whose symbols are prefixed `ref_`. On a
card each pair runs on the same inputs: the output must be identical byte
for byte, and the two are timed in alternating order (9 rounds, 7 for
libknc). A kernel is "within" when its median is inside the range of the
`.byte` version's runs, or better.

| Kernel | C | Gate | Driven by |
| --- | --- | --- | --- |
| `vpu_memcpy` | [vpu_memcpy.c](vpu_memcpy.c): `knc_memcpy64`, `knc_memset64_ord`, `knc_memset64` | [vpu_memcpy-gate.c](vpu_memcpy-gate.c) | `scripts/s3-gate.sh vpu_memcpy` |
| `mandel_vpu` | [mandel_vpu.c](mandel_vpu.c): `mandel8_vpu` | [mandel_vpu-gate.c](mandel_vpu-gate.c) | `scripts/s3-gate.sh mandel_vpu` |
| `vpu_int` | [vpu_int.c](vpu_int.c): `vpu_int_probe`, `vpu_int_unaligned` | [vpu_int-gate.c](vpu_int-gate.c) | `scripts/s3-gate.sh vpu_int` |
| `bitunpack` | [bitunpack.c](bitunpack.c): `knc_unpack_b5`, `b11`, `b16` | [bitunpack-gate.c](bitunpack-gate.c) | `scripts/s3-gate.sh bitunpack` |
| libknc | `knc.c`, written by [knc-c-gen.c](knc-c-gen.c): all 176 exported functions and tables | [knc-gate.c](knc-gate.c) | `scripts/s3-libknc-gate.sh` |

Rules the C follows, each because the hand version does the same:

- the pointers do not overlap (`restrict` in the kernels that read and
  write); the `.byte` versions assume it, and without it the compiler may
  not move a load above an earlier store;
- loops are not unrolled further than the `.byte` version unrolls them
  (`#pragma clang loop unroll(disable)` in `vpu_memcpy.c`);
- a source that the instruction overwrites is `_mm512_undefined_epi32()`.

Measured on card 0, 2026-10-16, with the series build (patches 0001 to
0017), the logs in `~/.cache/intel-phi-llvm-build/s3/final/`:

| Kernel | Output | Speed (`.byte` median / intrinsics median) |
| --- | --- | --- |
| `vpu_memcpy` | 201 of 201 cases identical | `memcpy64`: 1.016 to 1.033 at 64 B to 1 MiB; `memset64_ord`: 0.960 to 1.006, within; `memset64`: within except **64 B 0.993 and 64 KiB 0.984 (below)** |
| `mandel_vpu` | 30,720 calls identical (256 x 192 points at maxiter 1, 2, 17, 256, 1000) | 1.123 (maxiter 64), 1.150 (maxiter 1000) |
| `vpu_int` | 4,128 cases identical | probe 1.547; unaligned 0.937, inside the `.byte` range (27.7 to 29.7 ns, median 27.7 against 29.6) |
| `bitunpack` | 6,000 unpacks identical | b5 1.220, b11 1.192, b16 1.068 |
| libknc | 10,484 cases identical (every width of every family, misaligned FastLanes input, out-of-range widths) | 163 of 163 kernels faster than every run of the `.byte` version (ratio 1.015 to 2.642, mean 1.247) |

The clears of one and four blocks are 1 to 4% slower by median in every
run, and which of them falls outside the `.byte` range moves from run to
run (`memset64_ord` 256 B and `memset64` 64 B in the run before). The
cause is one extra instruction on the path of a call of
one or four blocks: a register copy (`movq %rax, %rcx`, the greedy
allocator puts the cursor next to the return value) and a `jmp` from the
block layout. No source shape removed them.
