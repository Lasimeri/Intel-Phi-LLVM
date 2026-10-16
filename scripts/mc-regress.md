# scripts/mc-regress.sh

Shows that a change to the MC layer moved no other x86 encoding.

```
REF_MC=... REF_OBJDUMP=... LLVM_MC=... LLVM_OBJDUMP=... scripts/mc-regress.sh
```

- **Corpus:** compiler-rt's builtins (`lib/builtins/*.c`, from the pinned
  `llvm-project` clone), compiled to assembly by the distro clang at
  x86-64, x86-64-v3, x86-64-v4, znver4 and sapphirerapids with `-mapxf`.
  That covers legacy, VEX, EVEX and APX encodings: 905 files.
- **Bytes:** each file is assembled by `REF_MC` and by `LLVM_MC`, and the
  objects must be byte-identical. The x86-64 files are assembled again
  with `-mcpu=knc`. AVX forms of the mnemonics KNC shares are rejected
  there by design, so only the baseline set is used.
- **Text:** `REF_OBJDUMP` and `LLVM_OBJDUMP` must print the same
  disassembly for each reference object.

For S2 the reference is the S1 build (`llvm-mc` and `llvm-objdump` of
patches 0001 to 0012), kept at `~/.cache/intel-phi-llvm-build/ref-s1`.
Arch's own `llvm-mc` is not a usable reference without `-mcpu`: patch 0003
pads with one-byte NOPs where it pads with `nopl`. Measured 2026-10-16:
1,086 assemblies, 0 objects differ, 0 disassemblies differ.
