# 0001: The KNC vector ISA becomes a feature and CPU of the stack's LLVM

Status: accepted 2026-10-15 for D1 and D2 (below); D3 open.

## Context

- The stack's LLVM (Intel-Phi-3120A ADR 0002 and ADR 0007; its `toolchain/llvm` until this repository replaces it) compiles only
  the card's scalar subset. Vector code on the card comes from
  `knc-mvex-gen` as `.byte` lines, with no compiler involved.
- No public compiler has ever emitted MVEX. Upstream LLVM never had KNC,
  and upstream binutils removed K1OM in 2022. Evidence:
  [research/knc-vector-isa.md](../research/knc-vector-isa.md).
- `avx512f` is the top rung of LLVM's `X86SSELevel` ladder. Enabling it
  turns SSE on, and with it scalar float in XMM. So the vector unit cannot
  be exposed as "AVX-512F minus some instructions" (measured by reading
  `X86.td` and `X86Subtarget.h` of 22.1.8).
- The distro rustc is now 1.99.0 on LLVM 23.1.1, while the patched LLVM is
  22.1.8. Intel-Phi-3120A ADR 0007's dylib substitution needs the same major.minor
  (measured on the rack, 2026-10-15).

## Decision (proposed)

- Add a `knc` subtarget feature outside the SSE ladder, and a `knc` CPU
  that also implies today's scalar restrictions (no CMOV, no NOPL, no
  SSE).
- Add MVEX as a flag bit on the EVEX encoding form.
- Write the instruction definitions in their own file, under `HasKNC`,
  from Intel's ISA reference 327364.
- Build it in the stages of [the plan](../plan.md), in order:
  - S0: rebase onto 23.1.1.
  - S1: feature and CPU.
  - S2: MC layer, byte-identical to MPSS gas on its 6,708-instruction
    test.
  - S3: intrinsics.
  - S4: Rust.
  - S5: native on the card.
  - S6: generic vectors and the vectorizer, optional.
- Keep the knc64-x87 scalar ABI.

## Open choices

- **D1:** keep the distro rustc and rebase on every Arch LLVM bump, or
  build rustc from source pinned to the stack's LLVM.
- **D2:** `knc-mvex` stays the sanctioned encoder, with LLVM checked
  against it, or LLVM's encoder replaces it.
- **D3:** `__m512` types in zmm0 to zmm7 for arguments and returns, or by
  memory.

## Consequences

- Each LLVM rebase carries a larger series once the vector patches exist.
- MPSS binutils 2.22 (GPL-3) is built on the rack and only run as an
  oracle. Nothing from it enters the patches.
- Tests stay Python-free: `llvm-mc` and `llc` outputs are compared by awk
  or tcc C tools, with `LLVM_INCLUDE_TESTS=OFF` kept.
- Gates S3 to S6 run on a card, and each takes that card out of
  production first.

## Alternatives rejected

- **Reuse `avx512f` plus an MC rewrite of EVEX into MVEX:** it turns on
  SSE (above).
- **MPSS gcc 5.1.1:** it has no KNC vector codegen.
- **Keep `.byte` generation only:** no intrinsics, no inline asm with
  register allocation, no vectorizer.

## Choices made (2026-10-15)

The person: "the knc-mvex would be the only objectively correct option for
keeping a wrapper around those instructions, and keep the distro LLVM, so
my software stack can remain compatible with Arch Linux and Arch
Linux-Like".

- **D2: (a).** `knc-mvex` stays the one sanctioned MVEX encoder, the
  wrapper around the instructions for the whole family. LLVM's MVEX
  encoder (S2) is a second producer of the same bytes, held to it:
  - S2's gate compares `llvm-mc` with both MPSS gas and
    `knc-mvex-gen vectors`;
  - Intel-Phi-AVX512's `make mvex-check` keeps knc-mvex as the reference.
- **D1: (a).** The series follows the LLVM version Arch ships in
  `llvm-libs`, and the distro rustc loads the patched dylib
  (Intel-Phi-3120A ADR 0007). No rustc fork.
  - Consequences: Rust reaches MVEX through `global_asm!`, `extern "C"`
    or knc-mvex output (rustc's `zmm_reg`/`kreg` classes stay gated on
    `avx512f`), and every Arch LLVM bump means a rebase.
  - `scripts/check-distro.sh` reports when `llvm-libs` and the pinned tag
    differ.
  - A patched LLVM is still required: Arch's unmodified `libLLVM` has no
    KNC. "Distro LLVM" means the same version, so the distro rustc and
    clang setups stay usable.
- **D3:** open.
