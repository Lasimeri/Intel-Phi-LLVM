# oracle/knc-mvex-check.sh

Decision D2: knc-mvex, the stack's encoder, stays the sanctioned MVEX
encoder, and LLVM must match it byte for byte.

```
LLVM_MC=~/.cache/intel-phi-llvm-build/llvm/bin/llvm-mc oracle/knc-mvex-check.sh
```

The vectors are every `.byte ... # text` line that `knc-mvex-gen` wrote
into Intel-Phi-3120A's `card/examples/*.S` (the files whose first line
says so): 830 instructions over moves, the float64 and int32 arithmetic,
shifts, masks and compares, `kmov`, unpack and broadcast. Each line's text
(Intel syntax) is assembled by `llvm-mc -mcpu=knc` and compared with its
bytes. knc-mvex always writes a 32-bit displacement (mod = 10, even for
`[rax+0]`), so memory forms get LLVM's `{disp32}` pseudo-prefix.

Measured 2026-10-16: 830 vectors, 0 byte mismatches, 0 errors. Outputs
are in `~/.cache/intel-phi-llvm-build/oracle/knc-mvex/`.
