# oracle/codegen-check.sh

Code generation builds MCInsts without the assembler's checks, so a wrong
decoration or operand would be encoded without complaint. This check
closes that gap: every Knights Corner instruction in the given objects
(an operand that is a zmm or mask register, or a `k` mnemonic) is printed
by `llvm-objdump --mcpu=knc`, reassembled from that text by MPSS gas
(`as-new -march=k1om`), and the bytes compared in order.

```
LLVM_OBJDUMP=<llvm-objdump> oracle/codegen-check.sh OBJECT...
```

A rip-relative operand is reassembled as `(%rip)`: the 32-bit field is
the relocation's, the same in both. Prints `codegen vs gas: N KNC
instructions, M byte mismatches, E gas errors` and exits 1 on any
mismatch or error. `scripts/s3-gate.sh` and `s3-libknc-gate.sh` run it on
every kernel they build.
