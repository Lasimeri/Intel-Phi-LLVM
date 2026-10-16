# patches

Ten patches against `llvmorg-23.1.1` (commit `6dfe1677` in `SERIES`), the LLVM
Arch ships in `llvm-libs` (decision D1), applied by `scripts/build.sh` before it
configures. Imported from Intel-Phi-3120A `toolchain/llvm/patches` at cce74b6
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
