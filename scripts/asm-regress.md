# scripts/asm-regress.sh

Under `-march=knc`, every assembly input goes through the Knights Corner
assembler path. That path takes `popcnt`, `lzcnt`, `tzcnt`, `kmov`,
`vmovaps` and the rest of the table, and rejects the AVX forms of those
mnemonics. The check runs the assembly the card's runtimes are built
from:
- every x86_64 `.s` and `.S` of musl (`PHI_MUSL_SRC`, the stack's 1.2.5);
- compiler-rt's builtins;
- libunwind.

Each `.S` is preprocessed once by this clang (`-march=knc`); each `.s`
is used as it is. Every file is assembled with `-mcpu=knc` by `REF_MC`
(the series before the change) and by `LLVM_MC`. The objects must be
byte-identical, and a file accepted by only one of the two fails.

```
REF_MC=~/.cache/intel-phi-llvm-build/ref-s1/llvm-mc LLVM_MC=~/.cache/intel-phi-llvm-build/llvm/bin/llvm-mc scripts/asm-regress.sh
```

Measured 2026-10-16 (S1 against S2): 37 files (musl 31, compiler-rt 4,
libunwind 2), 37 identical. `scripts/march-check.sh` covers the C
sources only.
