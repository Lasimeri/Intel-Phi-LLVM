# LLVM and Clang with the KNC vector unit

Question: what does it take for the stack's LLVM (Intel-Phi-3120A `toolchain/llvm`, imported here as `patches/`,
llvmorg-22.1.8 plus ten patches) to compile for the whole Knights Corner
core, the 512-bit vector unit included, from clang on the host, clang on
the card, and rustc? Written 2026-10-15 on the GPU rack. Everything
labelled "measured" was run there; everything labelled "estimate" is not.

## Reading of the request

The user: "start on a plan for implementing LLVM and Clang natively into
my Rust written Xeon Phi 3120A software stack".

- Clang already runs natively on the card (P7, `card` variant of
  `toolchain/llvm/build.sh`, 2026-09-14). That is not planned again.
- What the stack's LLVM lacks is the card's vector ISA. Vector code on the
  card today is hand-written `.byte` lines from `knc-mvex-gen`
  (`card/examples/*.S`, `lib/knc`); no compiler emits MVEX.
- So the plan is a real KNC target in the stack's LLVM: the MVEX vector
  instructions behind their own feature and CPU, used by clang (cross and
  on the card) and by rustc.
- Not this plan: LLVM as a library inside the tools (MC or ORC JIT inside
  a translator). That would be a separate plan.

## Prior art

| Source | What it says |
| --- | --- |
| llvm-dev, July 2013, "LLVM x86 backend for Intel MIC" (lists.llvm.org/pipermail/llvm-dev/2013-July/063734.html) | Intel: "We do not plan to support KNC architecture in open source." Mikushin's patch was scalar-only `k1om64`, not merged. |
| llvm-dev, December 2013, "JIT on Intel KNC" (discourse.llvm.org/t/jit-on-intel-knc/30239) | PathScale had a closed KNC LLVM backend. Nothing public. |
| binutils, March 2022, "opcodes: Drop bfd_arch_l1om/bfd_arch_k1om support" (sourceware.org/pipermail/binutils/2022-March/120228.html) | Upstream binutils removed K1OM. Its `.insn` documentation says MVEX encodings "cannot be expressed". |
| Agner Fog, objconv manual, section 8.4 | objconv disassembles KNC; EVEX and MVEX "differ by a single bit in the prefix". |
| Rust reference, inline assembly; `compiler/rustc_target/src/asm/x86.rs` | `zmm_reg` and `kreg` require the `avx512f` target feature. |
| MPSS 3.8.6 sources, `vendor/mpss-3.8.6/mpss-src-3.8.6.tar` | `binutils-2.22+mpss3.8.6` has a full MVEX assembler and disassembler. `gcc-5.1.1+mpss3.8.6` has `linuxk1om.h` and offload code only, no KNC vector codegen. |

Conclusion: nobody has published a compiler that emits MVEX. Intel's
binutils is the only free tool that encodes it.

## Measured on the rack, 2026-10-15

### 1. rustc moved off the patched LLVM's version

```
/usr/bin/rustc -vV        -> release 1.99.0, LLVM version 23.1.1
pacman -Q rust llvm-libs  -> rust 1:1.99.0-1, llvm-libs 23.1.1-3
toolchain/llvm/bin/clang  -> clang version 22.1.8
```

ADR 0007 loads the patched `libLLVM.so.22.1` into the distro rustc, so the
major.minor must match. It no longer does. The card Rust path is stale
until the series is rebased onto llvmorg-23.1.1, or rustc is pinned.

### 2. AVX-512F cannot be reused as a feature

`llvm/lib/Target/X86/X86.td` (22.1.8):

```
def FeatureAVX512 : SubtargetFeature<"avx512f", "X86SSELevel", "AVX512",
                       "Enable AVX-512 instructions",
                       [FeatureAVX2, FeatureFMA, FeatureF16C]>;
```

`X86Subtarget.h`: `enum X86SSEEnum { NoSSE, SSE1, ..., AVX2, AVX512 }`,
`hasSSE1() { return X86SSELevel >= SSE1; }`.

`avx512f` is the top rung of the SSE ladder. Enabling it makes
`hasSSE1()` true, so scalar float moves to XMM, patch 0002's x87 return
stops applying, and SSE and VEX instructions appear everywhere. KNC
executes none of them (ISA reference 327364, appendix B). So KNC needs:

- its own feature outside the ladder;
- its own instruction definitions under a `HasKNC` predicate;
- the register classes VR512 (zmm0 to zmm31) and VK16 (k0 to k7) made
  available without `hasAVX512()`.

The encoding field in `MCTargetDesc/X86BaseInfo.h` is two bits wide, and
all four values are taken: `LEGACY` 0, `VEX` 1, `XOP` 2, `EVEX` 3. MVEX
shares the 62h prefix with EVEX and differs in P1 bit 2: 0 for MVEX
(`host/crates/knc-mvex`, pinned to Intel's `mic_ni.h` bytes), 1 for EVEX.
So MVEX is a flag bit on the EVEX form, not a fifth encoding.

### 3. Intel's assembler builds and agrees with its own tests

```
configure --target=x86_64-k1om-linux --disable-werror \
  CFLAGS="-O2 -fcommon -std=gnu89 -Wno-error -Wno-implicit-function-declaration \
          -Wno-int-conversion -Wno-incompatible-pointer-types"
make -j128 all-gas
make -j128 MAKEINFO=true all-binutils   # first try failed only on binutils.info
as-new -march=k1om -o k1omni.o gas/testsuite/gas/i386/k1omni.s   # rc 0, 0 messages
objdump -dw k1omni.o
```

- `opcodes/i386-opc.tbl` has 319 lines tagged `CpuL1OMNI`/`CpuK1OM` and
  344 distinct mnemonics.
- `k1omni.s`: 6,708 instructions. Comparing the bytes and text from
  `objdump` with the expected `k1omni.d`, 6,704 match. The 4 that differ
  are `vpadcd 0x99(%rip),...` lines, where the expected file's pattern
  does not survive the field split. That is a parsing gap in the
  comparison, not a wrong encoding.
- Without `-march=k1om` every MVEX line fails ("`kand' is not supported
  on `x86_64'").

Build and outputs: now `oracle/build-binutils.sh` and `oracle/k1omni-check.sh` (first run: `/mnt/raid5/phi/bench/llvm-knc/`).

This makes MPSS gas the encoding oracle. It is GPL-3 and is only ever
**run**, as a black box. No table, test or line from it goes into the
Apache-2.0 LLVM patches. The instruction definitions are written from
Intel's ISA reference 327364.

## Constraints the work carries

| Constraint | Consequence |
| --- | --- |
| No Python, ever | LLVM's lit, `ninja check-*` and the `update_*_test_checks.py` scripts are Python. `build.sh` keeps `LLVM_INCLUDE_TESTS=OFF`. Gates are `llvm-mc` and `llc` runs compared byte for byte by awk or a tcc C tool; `FileCheck` may be built and called directly from shell. |
| knc64-x87 ABI (ADR 0002) | Scalars stay x87 with ST0 returns. Passing vector types in zmm is a new decision (open decision D3 in [ADR 0001](../decisions/0001-knc-feature-outside-sse-ladder.md)). Kernel patches 0024 and 0030 already save zmm and k on context switch and around signal handlers. |
| One sanctioned MVEX encoder (`knc-mvex`, enforced by Intel-Phi-AVX512's `make mvex-check`) | An LLVM encoder is a second one. Either knc-mvex stays the truth and LLVM is checked against it inside `mvex-check`, or knc-mvex is retired (open decision D2). |
| GPL oracle | Run MPSS gas and objdump; never copy from them. |
| Cards serve production (llama.phi on :8001, all four) | Every gate that runs on a card takes that card out of production first. Ask before each one. |
| Every build on the rack, `-j128` | `toolchain/llvm/build.sh` runs there already. |

## Stages and gates

Each stage ends at a measured gate. The expensive part, generic vectors
and the vectorizer, comes last and is optional.

| Stage | Work | Gate | Card? |
| --- | --- | --- | --- |
| S0 Rebase | Rebase patches 0001 to 0010 onto llvmorg-23.1.1 (rustc's LLVM), all three variants (default, dylib, card); then the runtimes (compiler-rt, libunwind, libc++), musl, `build-std.sh` | `toolchain/check/run.sh` passes; the Rust `hello` from P2 links with the 23.1 dylib and `phi-isa-audit` reports 0 illegal instructions; the card clang probe compiles on the host | No (the card run of the probe: yes) |
| S1 Feature, CPU, registers | `FeatureKNC` ("knc") outside the SSE ladder; CPU `knc` (`-march=knc`) that implies the scalar restrictions now spelled out in `knc-cc` (no cmov, no nopl, no SSE); VR512 and VK16 under `HasKNC`; an MVEX flag bit on the EVEX form in `X86BaseInfo.h` | `llc -mtriple=x86_64-unknown-linux-musl -mcpu=knc` produces scalar code identical to today's `knc-cc` output for the musl and examples sources (byte compare of `.o` text) | No |
| S2 MC layer | `X86InstrKNC.td` instruction definitions from ISA 327364; asm parser for the K1OM AT&T syntax (swizzles `{cdab}`, conversions `{float16}` `{uint8}`, broadcasts `{1to16}` `{4to16}`, `{eh}`, `{rn-sae}`); MVEX encoder; disassembler selecting MVEX by P1 bit 2 | `llvm-mc -mcpu=knc` on `k1omni.s`: all 6,708 instructions byte-identical to MPSS gas (awk on the two `objdump -dw` listings); `llvm-mc -disassemble` round trip identical; every `knc-mvex-gen vectors` entry identical | No |
| S3 Intrinsics | clang builtins and a header with Intel's KNC `_mm512_*` names; patterns that select the S2 instructions from `llvm.x86.knc.*` intrinsics; no generic vector types yet | `card/examples` vector kernels (mandel-vpu, vpu_memcpy, vpu_int, bitunpack) and `lib/knc` rewritten with intrinsics give bit-identical output to the `.byte` versions, at equal speed within the spread of interleaved runs | Yes |
| S4 Rust | rustc gates `zmm_reg`/`kreg` on `avx512f`, which KNC must never enable: Rust uses MVEX through `global_asm!` or `extern "C"` over the S3 C library, unless rustc itself is patched (decision D1) | `lib/knc-rs` calling the S3 kernels: bit-identical to C on the card | Yes |
| S5 Native | the `card` variant rebuilt with S1 to S3; clang on the card compiles the S3 kernels | `.o` text compiled on the card byte-identical to the host cross build | Yes |
| S6 Generic vectors (optional) | legal `v16f32 v16i32 v8f64 v8i64` and masks under `HasKNC`; lowering of unaligned access through `vloadunpackl/h` and `vpackstorel/h`; expansion of what KNC lacks (byte and word lanes, most permutes, compress and expand, ternary logic); cost model; loop and SLP vectorizer | a C kernel suite auto-vectorized for `-mcpu=knc`, run on a card against the x87 scalar build (results within stated ulp bounds), speed recorded | Yes |

### Rough size

The following are estimates, not measurements:

- S2 covers 344 mnemonics, each with up to several operand forms.
- For comparison, `X86InstrAVX512.td` is the bulk of the 17,668 lines
  counted across `X86InstrAVX512.td` and the X86 TableGen backends.
- S1 to S3 are a small backend inside the X86 one. S6 is the open-ended
  part.

## Open decisions (the user's)

| | Decision | Options |
| --- | --- | --- |
| D1 | How rustc follows the patched LLVM | (a) keep the distro rustc and rebase on every Arch LLVM bump: S0 now, and each rebase grows with every vector patch; (b) build rustc from source pinned to the stack's LLVM (ADR 0007's escape hatch), which also allows patching `asm!` register classes for KNC |
| D2 | Which MVEX encoder is the truth | (a) `knc-mvex` stays sanctioned, and LLVM is checked against it in `mvex-check`; (b) LLVM MC becomes the encoder, and `knc-mvex-gen` turns into C or assembly sources compiled by the S3 compiler |
| D3 | Vector ABI | (a) `__m512` types passed and returned in zmm0 to zmm7, as AVX-512 SysV does, and scalars unchanged; (b) vectors by memory only |

## Next action

S0 (the rebase onto 23.1.1). It is needed regardless of the vector work,
because the card Rust path is broken until then.
