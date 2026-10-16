# patches

Fifteen patches against `llvmorg-23.1.1` (commit `6dfe1677` in `SERIES`), the LLVM
Arch ships in `llvm-libs` (decision D1), applied by `scripts/build.sh` before it
configures: 0001 to 0010 scalar, 0011 and 0012 the `knc` CPU (S1), 0013 to
0015 the MVEX MC layer (S2). The first ten were
imported from Intel-Phi-3120A `toolchain/llvm/patches` at cce74b6
(against 22.1.8) and rebased 2026-10-15: every hunk applied unchanged, only
line offsets and index lines moved. Named `NNNN-<summary>.patch`; each is a
plain `git diff` of one step (not `git format-patch` mail), carrying the
Apache-2.0-with-LLVM-exception license of the code it modifies.

Intel-Phi-3120A's `toolchain/llvm/README.md` explains what each one does and why. In short:
0001, 0005 and 0009 remove the assumption that x86-64 implies CMOV; 0002
and 0004 are the knc64-x87 ABI (scalar floats returned in `ST0`, `va_arg`
taking the SSE class from the overflow area); 0003 and 0009 make `-nopl`
reach 64-bit NOP padding; 0006 stops `rep bsf` being emitted without BMI;
0007 builds the compiler-rt x86 builtins without SSE; 0008 turns the
`pause` intrinsic into a `nop`; 0010 returns SSE-class values that need
more than two x87 registers through memory (knc64-x87 ABI).

Applying them by hand is not part of any workflow: `build.sh` does it and
leaves the tree at the patched state, so a rerun after a clone is
idempotent.

## Knights Corner as a CPU (S1, 2026-10-15)

| Patch | Files | Change |
| --- | --- | --- |
| 0011 | `X86.td`, `X86Subtarget.cpp`, `MCTargetDesc/X86MCTargetDesc.{h,cpp}`, `TargetParser/X86TargetParser.{h,cpp}` | `FeatureKNC` ("knc"), outside the `X86SSELevel` ladder. CPU `knc`: x87, cx8, fxsr, 64bit, knc, with x86-64's scheduling model and the tuning that `-march=x86-64` gets (`generic`). `dropTripleSSE2` removes the `+sse2` that `ParseX86Triple` adds for x86_64; otherwise it would override the CPU's list (backend and MC). The target parser gets `CK_KNC`. |
| 0012 | `clang/lib/Basic/Targets/X86.cpp` | `initFeatureMap` does not force SSE2 for `knc`. `-march=knc` predefines `__MIC__` and `__KNC__` as 1, as Intel's compiler did for `-mmic`, plus `__knc`, `__knc__` and `__tune_knc__`. Cache line 64 bytes (SSDG 328207). |

Checked by `scripts/march-check.sh` (byte-identical to `knc-cc`'s flags
on 1,359 sources) and `scripts/hardening-check.sh`.

## The MVEX MC layer (S2, 2026-10-16)

| Patch | Files | Change |
| --- | --- | --- |
| 0013 | `X86InstrKNC.td` (generated), `X86InstrFormats.td`, `X86InstrPredicates.td`, `X86InstrInfo.td`, `MCTargetDesc/X86BaseInfo.h`, `MCTargetDesc/X86KNCInfo.{h,cpp,inc}` (`.inc` generated), `MCTargetDesc/CMakeLists.txt`, `MCTargetDesc/X86MCCodeEmitter.cpp`, `utils/TableGen/X86InstrMappingEmitter.cpp`, `utils/TableGen/X86FoldTablesEmitter.cpp` | 330 KNC defs from `tools/knc-gen.c`, all `isCodeGenOnly`, marked `KNCEnc`; `isMVEX` (TSFlags bit 55, `X86II::MVEX`) and `HasKNC`; the side table of forms, operands and decoration tables; prefix kind `MVEX` (62h, P0 bit 3 and P1 bit 2 clear, P2 = E SSS V' aaa) and disp8*N from the memory conversion and SSS; the trailing E/SSS operand is not emitted as an immediate; the EVEX-compression and fold-table backends skip `KNCEnc` |
| 0014 | `AsmParser/X86AsmParser.cpp`, `MCTargetDesc/X86InstPrinterCommon.{h,cpp}`, `MCTargetDesc/X86ATTInstPrinter.{h,cpp}`, `MCTargetDesc/X86IntelInstPrinter.{h,cpp}` | Under `knc`, KNC mnemonics and pseudo-ops are parsed with the ordinary operand parsers plus a decoration reader, matched form by form from the table, and emitted; the gas-compatible rules in `docs/results/2026-10-16-s2-mc-layer.md`; `.allow_index_reg` accepted; both printers print KNC forms from the table |
| 0015 | `Disassembler/X86Disassembler.cpp` | Under `knc`, 62h with P1 bit 2 clear (MVEX) and the KNC VEX forms are decoded from the table; anything else falls through to the generic decoder |

Checked by `oracle/k1omni-check.sh` (Intel's 6,708 K1OM test
instructions, round trips, text), `oracle/knc-mvex-check.sh` (830
knc-mvex vectors), `scripts/mc-regress.sh` (1,086 other assemblies
unchanged), `scripts/mvex-card-check.sh` (card 0) and
`scripts/check-knc-gen.sh` (the generated files reproduce).
