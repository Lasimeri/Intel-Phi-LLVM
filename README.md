# Intel-Phi-LLVM

LLVM and Clang for the Xeon Phi 3120A (Knights Corner), the whole core.
The scalar knc64-x87 subset works on LLVM 23.1.1, the version Arch ships (S0), and `-march=knc` selects it with hardening defaults in `config/knc.cfg` (S1). This repository adds the
512-bit MVEX vector unit, as a `knc` feature and CPU (`-mcpu=knc`,
`-march=knc`) beside the scalar restrictions.

It is a sibling of [Intel-Phi-3120A](https://github.com/Lasimeri/Intel-Phi-3120A),
the card stack:

- **Scalar compiler:** the stack's `toolchain/llvm` (llvmorg-22.1.8 plus
  ten patches) builds the scalar compiler the card runs today. Those ten
  patches are imported here unchanged as the start of `patches/SERIES`.
- **What lives here:** the vector work, the rebase onto the LLVM that the
  distro rustc uses, and the checks.
- **Switch-over:** the stack keeps its own build until it is pointed here.

| Path | What |
| --- | --- |
| `patches/` | The series against the pinned `llvm-project` tag; `SERIES` names the tag and its commit |
| `scripts/build.sh` | Fetch (verified against the pinned commit), patch, configure, build, install, check; variants `clang`, `dylib` (for rustc), `card` (clang for the card) |
| `scripts/runtimes.sh`, `rust-check.sh`, `card-package.sh`, `card-check.sh` | The S0 gate: runtimes in an isolated sysroot, the distro rustc on the patched dylib, the card clang packaged and tested on a card |
| `scripts/check-distro.sh` | Reports when Arch's LLVM moves past the pinned tag (decision D1) |
| `config/knc.cfg` | `clang --config` defaults: `-march=knc` and the hardening verified on a card |
| `scripts/march-check.sh`, `hardening-check.sh` | The S1 gate: `-march=knc` byte-identical to `knc-cc`, hardening defaults on a card |
| `oracle/` | Intel's MPSS binutils 2.22, built from checksummed sources and run as the MVEX encoding oracle (GPL, never copied) |
| `docs/plan.md` | Stages S0 to S6 and their gates |
| `docs/research/` | Prior art, measurements, constraints |
| `docs/decisions/` | Decision records |
| `SECURITY.md` | Supply chain, isolation, and the hardening policy for compiled output |

Everything builds and runs on the GPU rack. The working copy is
`/mnt/raid5/phi/Intel Phi LLVM`, with the alias `~/.cache/intel-phi-llvm`,
and the build root is `~/.cache/intel-phi-llvm-build`.

```
make check                                  # docs rules, shell syntax, SERIES consistency
scripts/build.sh all                        # host clang for the card
oracle/build-binutils.sh && oracle/k1omni-check.sh
```

Licences: [LICENSE.md](LICENSE.md).
