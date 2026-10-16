# scripts/card-package.sh

Packages the card variant (`PHI_LLVM_VARIANT=card scripts/build.sh`) for
the card. It follows the layout of Intel-Phi-3120A's
`card/userland/components/clang.sh`, but installs to its own prefix,
`/data/llvm-knc` (`PHI_CARD_PREFIX`), on the card's persistent disk. The
card's live `/opt/phi` toolchain is never overwritten.

Contents of the tarball:

| Path | Holds |
| --- | --- |
| `bin/` | `clang-<ver>` and its links (`clang clang++ cc c++`), `lld` (`ld.lld`, `ld`), seven binutils-style llvm tools |
| `bin/<name>.cfg` | `config/knc.cfg` (`-march=knc` and the verified hardening defaults) plus `--sysroot` set to the prefix |
| `usr/include`, `usr/lib` | The sysroot from `scripts/runtimes.sh` (musl, libunwind, libc++ of this LLVM) |
| `lib/clang/<ver>/` | The compiler's own headers, the compiler-rt builtins and crt objects |

Before packing, `phi-isa-audit` runs on `clang-<ver>` and `lld`, with
`--ignore XSAVE` as in the stack.

Output: `~/.cache/intel-phi-llvm-build/card-pkg/llvm-knc-card.tar.gz`
and its `.sha256`. `scripts/card-check.sh` checks that sum on the card
before it unpacks the tarball.
