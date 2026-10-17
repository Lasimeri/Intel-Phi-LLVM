# 2026-10-16: S3, code generation and the Knights Corner intrinsics

Under `-march=knc`, clang now compiles C written with Intel's Knights Corner
intrinsics (`<immintrin.h>`) into MVEX code. The stack's hand-encoded vector
kernels, written again with the intrinsics, give the same output bit for bit
on card 0. In speed, the intrinsic versions match or beat the hand-encoded
ones in all but the short clears of `vpu_memcpy`, listed below. Everything was run on the
rack and on card 0 with production idle.

## Patches

| Patch | Lines | What |
| --- | --- | --- |
| 0016 | 6,778 | LLVM. The four 512-bit types in VR512 for the intrinsics, with every generic operation on them expanded except loads, stores, bitcasts and constants; copies, spills and mask spills; 297 `llvm.x86.knc.*` intrinsics and their selection (generated); an in-order scheduling model for the core; the guards that keep the new legal types out of scalar code; `sahf` for the CPU; the target parser's `knc` feature |
| 0017 | 1,549 | clang. `__builtin_ia32_knc_*` for every intrinsic, with their immediates checked against the manual's tables; `knc` as a target feature; `<kncintrin.h>`, which `<immintrin.h>` includes alone under `__KNC__` |

## How it works

- **Pseudos, not the S2 definitions.** The S2 MC definitions describe bytes.
  Each carries a mask operand (k0 means none) and no tied operands, so as
  MachineInstrs they would hide what a merge-masked or destructive instruction
  reads. Code generation selects pseudos that state their operands. A merge
  pass-through, and a destination the instruction also reads, is tied to the
  result. When the MCInst is built, a generated table turns each pseudo into
  its S2 definition: insert k0, drop the tied operand. The encoder, the
  printers and every S2 gate stay untouched.
- **What reads its destination.** The opcode tables don't say which
  instructions read their destination or write a mask beside it, so
  `isa/extract-ops.awk` reads that from each instruction's Operation section
  (`isa/knc-ops.txt`, 192 records). Three corrections, each with the
  manual's line as evidence, are in `isa/knc-overrides.txt`:
  - `vmov*` reads zmm1 only in its store form;
  - `vloadunpackh*` writes only past the line boundary;
  - the blends write every element.
- **Generated code.** `tools/knc-gen.c --codegen` writes, from the same
  table:
  - the intrinsics, the pseudos, their patterns and the fold patterns
    (register form plus aligned load becomes the memory form);
  - the pseudo-to-MC map, the pass-through and memory-access tables;
  - the clang builtins and their immediate checks.

  It generates 297 of the 330 forms (`OUTDIR/codegen-report.txt`). Not yet
  generated:
  - the 22 VSIB forms (gathers, scatters and their prefetches repeat until
    their mask is clear);
  - the 12 forms that write a mask beside their result (`vaddsetsps`,
    `vpadcd` and the other carry and borrow forms);
  - `kortest`, which is written by hand: its result is flags, so a branch
    on it is `kortest` and `jcc`.
- **Masks.** Masks are i16 at the IR boundary, so v16i1 stays illegal. After
  selection, a web of 16-bit PHIs that only carries masks between k
  registers is rebuilt as k-register PHIs (`finalizeLowering`). An 8-lane
  compare returns `range(i16 0, 256)`, so a `__mmask8` round trip folds away.
  The mandel loop keeps its mask in k1 across iterations, as the
  hand-encoded version does.
- **Memory.** The memory forms carry memory operands
  (`getTgtMemIntrinsic`, `SDNPMemOperand`), so alias analysis can move a
  load above a store to another object. Spills are 64-byte aligned, or the
  `vpackstoreld`/`hd` and `vloadunpackld`/`hd` pair where the stack can no
  longer be realigned. A mask spill goes through a free caller-saved GPR, or
  through %rax kept in a scratch slot.
- **Scheduling.** The core is in order, and one thread issues at most every
  other cycle, so `X86ScheduleKNC.td` models it in order. The vector ops are
  priced at Intel's numbers: 4 cycles, loads 7, conversions and shuffles 6.
  The latency of an op with a folded load is an estimate (7). With
  SandyBridge's out-of-order model, `bitunpack` ran at 0.51 to 0.60 of the
  hand-scheduled version.

## Kernel gates (card 0, series build)

`scripts/s3-gate.sh` and `scripts/s3-libknc-gate.sh` compare each kernel on
the same inputs, then time the two versions in alternating order. Logs are in
`~/.cache/intel-phi-llvm-build/s3/final2/`; the table is in
[tests/s3/README.md](../../tests/s3/README.md).

| Kernel | KNC instructions vs gas | Output | Speed (`.byte` / intrinsics median) |
| --- | --- | --- | --- |
| `vpu_memcpy` | 24, identical | 201 of 201 cases identical | `memcpy64` 1.021 to 1.042; `memset64_ord` 0.944 to 1.005, **256 B 0.965 below the `.byte` range**; **`memset64` 64 B 0.989 below**; the rest within |
| `mandel_vpu` | 21, identical | 30,720 calls identical | 1.124, 1.150 |
| `vpu_int` | 46, identical | 4,128 cases identical | probe 1.551; unaligned 0.942, inside the `.byte` range (27.8 to 29.7 ns) |
| `bitunpack` | 627, identical | 6,000 unpacks identical | 1.218, 1.193, 1.066 |
| libknc (176 symbols) | 51,630, identical | 10,484 cases identical | 163 of 163 kernels faster than every `.byte` run; ratio 1.014 to 2.577, mean 1.246 |

Every gate binary audits with 0 illegal instructions.

**The misses.** The clears of one and four blocks (`memset64` and
`memset64_ord` at 64 B and 256 B) are 1 to 6% slower by median in every
run. Which of them falls outside the `.byte` range moves from run to run.
In three runs of the series build:

| Run | Below the `.byte` range |
| --- | --- |
| 1 | `memset64_ord` 256 B 0.963, `memset64` 64 B 0.989 |
| 2 (9d1eb22) | `memset64` 64 B 0.993, 64 KiB 0.984 |
| 3 (this table) | `memset64_ord` 256 B 0.965, `memset64` 64 B 0.989 |

The cause is one extra instruction on the path of a short call:
- `movq %rax, %rcx`: the greedy allocator puts the cursor next to the return
  value;
- a `jmp` from the block layout.

No source shape removed them. The S3 gate is therefore met for four of the
five kernel sets; `vpu_memcpy` misses on its short clears.

## Beyond the gates: other levels, the machine verifier, mask spills

The kernel gates compile at `-O2`. A review after the first S3 commit
(9d1eb22) asked for the paths they don't reach. Two were broken, and both
are fixed in 0016:

| Check | Found | Fix |
| --- | --- | --- |
| the gate sources and `knc.c` at `-O0` (assertion build) | `-O0` selects through FastISel, which asserted in `X86FastEmitLoad` and `X86FastEmitStore` on the 512-bit types: its paths for them assume AVX-512 | `X86FastISel::isTypeLegal` declines vector types under `knc`, so SelectionDAG selects them |
| the same at `-O1`, `-Os`, `-O2` with `-mllvm -verify-machineinstrs` | `mandel_vpu.c`: "Using a killed virtual register", "PHI operand is not live-out". The mask PHI rewrite pointed a copy at a source whose old use had carried its kill flag. The emitted code was right (the gate output was identical), but a later pass could have trusted the flag | the rewrite clears the kill flags of every register it redirects to |

After the fix:

| Check | Result |
| --- | --- |
| 5 sources (`vpu_memcpy.c`, `mandel_vpu.c`, `vpu_int.c`, `bitunpack.c`, `knc.c`) at `-O0`, `-O1`, `-Os`, `-O2` under the verifier | 20 of 20 compile, verifier clean |
| the `march-check` corpus under the verifier | 1,359 sources, 0 failures |
| all five gates with `S3_OPT=-O0` (card 0) | output identical in every case (201, 30,720, 4,128, 6,000, 10,484); every KNC instruction identical in gas (106, 184, 249, 4,252, 385,548); 0 illegal. Speed at `-O0` is not a gate: libknc 0 of 163 within |
| `tests/s3/maskspill.c` at `-O2`, `-O1`, `-O0` (card 0) | 576 of 576 checks right at each level. At `-O2` the mask in the loop is spilled around the call, and the 12 masks live across a call are 12 16-bit spills and 12 reloads |

## What it did to scalar code

S3 changes the `knc` CPU for scalar code too:
- the scheduling model;
- `sahf`;
- `-falign-loops=1` in `config/knc.cfg`.

It also makes four 512-bit types legal, which leaked into scalar code in
three ways. `march-check` and the stack's programs found all three:

| Found by | What happened | Fix |
| --- | --- | --- |
| `march-check`: 19 musl complex-float sources crashed clang | The type legalizer widened `<2 x float>` into `v16f32`; the cost model then recursed until the stack overflowed | `getPreferredVectorAction`: under `knc` every other vector type is split and scalarized, as before S3 |
| the same sources in the assertion build | `SAHF or FCOMI` assertion in the x87 compare: the `knc` CPU never had `sahf`, though the card runs it (327364 appendix B: CPUID.80000001H:ECX[0] = 1) and release builds already emitted it | `sahf` in the CPU's feature list |
| `scripts/scalar-check.sh`: clang hung on the stack's `mandel.c` (zlib `deflate_worker`) | Store merging built a 512-bit store, legalization split it (not aligned), the combiner merged it again, forever | `canMergeStoresTo` refuses vector types under `knc` |
| `march-check`: 1 of 1,359 objects differed (musl `fstatat`: two 32-bit copies instead of one 64-bit) | DAGCombiner sizes its store-merge search by the widest legal type (now 512 bits): it tried i64 groups that cannot merge and never reached the i32 group | a target hook, `getMaximumMergedStoreInBits` (default: the widest legal store), 64 bits under `knc` |

After them:

| Check | Result |
| --- | --- |
| `scripts/march-check.sh` (`-march=knc` against `knc-cc`'s flags, now with `-msahf -mtune=knc` for the two things S3 added to the CPU; every compile under a 300 s limit) | 1,359 sources, 1,359 objects identical, no compile failure on either side |
| `scripts/scalar-check.sh` against the S2 compiler (5 rounds, card 0) | `pi-bbp 3000 1` 3.474 s to 3.487 s (0.4% slower, the ranges overlap; the run for 9d1eb22 had 3.483 to 3.475, so noise); mandel render 1.221 s to 1.186 s (2.9% faster, the ranges do not overlap, in both runs); deflate 0.126 s both; `membw` 1.1 GB/s both |
| `scripts/mc-regress.sh` against the S2 tools | 1,086 assemblies, 0 objects and 0 disassemblies differ |
| `scripts/asm-regress.sh` | 37 of 37 identical |
| S2 gates | k1omni 6,708 of 6,708, round trips identical (43,862 bytes), knc-mvex 830 of 830, sweep 330 of 330 |
| Card clang (`scripts/card-package.sh`, `card-check.sh 0`) | tarball 94,028,050 bytes (SHA-256 6bf173d6...), `clang-23` and `lld` 0 illegal; on card 0 C 0.49 s and C++ 5.05 s, both run right |
| The card clang on the intrinsics | `tests/s3/mandel_vpu.c`, `vpu_int.c`, `bitunpack.c` compiled on card 0: `.text` identical to the host cross build (159, 324, 4,217 bytes), a first look at the S5 gate |
| Rust (`PHI_RUST_CARD=0 scripts/rust-check.sh`) | the distro rustc on the S3 dylib: `rust_hypot=5`, exit 0 on the host and on card 0 |
| `scripts/hardening-check.sh 0`, `mvex-card-check.sh 0`, `build.sh check`, `make check` | PASS |

## Not done

- **Header coverage.** Intel lists 595 distinct KNC intrinsic names
  (Intrinsics Guide data 3.5.3, `scripts/knc-intrinsics.sh`), and
  `kncintrin.h` provides 119 of them, plus 12 that Intel classes as AVX-512
  (`setzero`, `set1`). Not provided:
  - 333 names that are one instruction, all with builtins already;
  - 58 that are several instructions;
  - 85 sequences or library functions.
- **Gathers, scatters, and forms that write a mask beside their result.**
  See above.
- **D3, the vector ABI.** clang passes `__m512` in memory without AVX-512,
  so that is what `-march=knc` does today. The intrinsics are always
  inlined, and the gate kernels take pointers, so S3 doesn't depend on D3.
  A non-inlined helper that takes `__m512` pays a store and a load per
  argument, and S4's `extern "C"` interface would fix whichever choice is
  made.
- **The two `memset` misses.**

## Enumeration values

Intel's documentation names the KNC enumerations but gives no values.
`kncintrin.h` uses Intel's names with its own values, which are the
hardware's SSS codes, so code that uses the names works.
