# config/knc.cfg

The clang defaults for the Xeon Phi 3120A (Knights Corner). Use it as:

```
clang --config=config/knc.cfg --sysroot=<musl sysroot> ...
```

The sysroot stays outside the file because it differs between the host
cross build and the card. `scripts/card-package.sh` writes a copy with the
card's prefix as `--sysroot` next to the card's clang.

| Line | Why | Verified |
| --- | --- | --- |
| `--target=x86_64-unknown-linux-musl`, `-march=knc` | The `knc` CPU (patches 0011, 0012): x87, cx8, fxsr, 64-bit; no CMOV, MMX, SSE or NOPL (ISA reference 327364, appendix B); `__MIC__` and `__KNC__` predefined | `scripts/march-check.sh`: 1,359 objects byte-identical to the stack's `knc-cc` flag list |
| `-fcf-protection=none`, `-fno-jump-tables` | Carried over from `knc-cc` unchanged. `endbr64` is not in the KNC instruction list | same |
| `-fstack-protector-strong` | Stack canary from musl's TLS (`%fs:0x28`) | `scripts/hardening-check.sh`: an overflowing copy is stopped on card 0 (exit 139), normal input exits 0 |
| `-fstack-clash-protection` | Large frames are probed page by page | a 1 MiB frame is probed and runs on card 0 |
| `-fPIE`, `-static-pie` | Static binaries that the kernel loads at a random address | two runs on card 0 print two different addresses (`randomize_va_space` 2) |
| `-Wl,-z,relro,-z,now,-z,noexecstack` | Read-only relocations after start, no lazy binding, non-executable stack | `GNU_RELRO`, `BIND_NOW`, `GNU_STACK` RW in the binary; the card CPU reports `nx` |
| `-fuse-ld=lld`, `-rtlib=compiler-rt`, `-unwindlib=none`, `-stdlib=libc++` | The stack's runtime choices (its `clang.cfg`). libc++abi carries the unwinder statically | a C++ program with exceptions runs on card 0 |

Not here: `_FORTIFY_SOURCE`, because musl provides no fortified headers.
