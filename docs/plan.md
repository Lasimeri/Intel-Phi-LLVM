# Plan

Stages in order of dependency. Each ends at a measured gate. The reasons,
the prior art and the measurements behind them are in
[research/knc-vector-isa.md](research/knc-vector-isa.md). The decision
is in [decisions/0001](decisions/0001-knc-feature-outside-sse-ladder.md).

| Stage | Work | Gate | Card | State |
| --- | --- | --- | --- | --- |
| S0 Rebase | the ten scalar patches onto llvmorg-23.1.1 (the distro rustc 1.99.0 is on LLVM 23.1.1); variants clang, dylib, card; the stack's runtimes and `build-std.sh` against it | `scripts/build.sh check`: 0 illegal instructions, x87 float code; the stack's Rust `hello` links with the 23.1 dylib and audits clean | probe run only | Done 2026-10-15 except build-std (rust-src not installed): [results](results/2026-10-15-s0-rebase-23.1.1.md) |
| S1 Feature, CPU | `knc` feature outside the SSE ladder, `knc` CPU with the scalar restrictions of `knc-cc` and the hardening defaults that pass on a card (`SECURITY.md`); VR512, VK16 under `HasKNC`; MVEX flag bit on the EVEX form | `llc -mcpu=knc` scalar output byte-identical to `knc-cc` today on musl and the examples | for hardening defaults | Next |
| S2 MC layer | `X86InstrKNC.td` from ISA 327364; K1OM operand syntax; encoder; disassembler by P1 bit 2 | `LLVM_MC=... oracle/k1omni-check.sh`: 6,708 of 6,708 byte-identical; disassembly round trip identical; every `knc-mvex-gen vectors` entry identical | no | |
| S3 Intrinsics | clang builtins, KNC `_mm512_*` header, intrinsic patterns | the stack's VPU examples and `lib/knc` from intrinsics: bit-identical output, equal speed (interleaved runs) | yes | |
| S4 Rust | `global_asm!` or `extern "C"` over S3, or rustc patched (D1) | `lib/knc-rs` bit-identical to C on a card | yes | |
| S5 Native | card variant with S1 to S3 | `.o` text from clang on the card identical to the host cross build | yes | |
| S6 Generic vectors (optional) | legal 512-bit types, unaligned access by unpack/pack pairs, expansion of what KNC lacks, cost model, vectorizers | auto-vectorized kernel suite within stated ulp of the x87 build, speed recorded | yes | |

Open decisions (the person's): D1 how rustc follows the patched LLVM,
D2 which MVEX encoder is the truth, and D3 the vector ABI. All three are
in [decisions/0001](decisions/0001-knc-feature-outside-sse-ladder.md).
