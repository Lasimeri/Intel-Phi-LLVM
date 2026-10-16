# scripts/build.sh

Builds this repository's LLVM: the pinned `llvm-project` tag with the
patches of `patches/SERIES` applied in order. It was taken from
Intel-Phi-3120A's `toolchain/llvm/build.sh` at cce74b6. The changes are:

- **Paths:** builds go under this repository's build root
  (`scripts/env.md`).
- **Verified source:** the pinned commit is checked after every fetch and
  before every patch run.
- **No root:** it refuses to run as root.

```
scripts/build.sh all                              # clang variant: host cross clang + lld
PHI_LLVM_VARIANT=dylib scripts/build.sh all       # libLLVM.so for the distro rustc
PHI_LLVM_VARIANT=card  scripts/build.sh all       # clang, lld, tools FOR the card (Canadian cross)
```

| Step | What it does |
| --- | --- |
| `fetch` | The only networked step. It clones `llvm-project` at the `SERIES` tag (depth 1) into `~/.cache/intel-phi-llvm-build/llvm-project`, or moves an existing clone to that tag. It then runs `verify`. |
| `verify` (inside `fetch` and `patch`) | `git rev-parse HEAD` must equal the `# LLVM commit:` line of `SERIES`; otherwise the script exits. A moved tag, a mirror serving other content or a stale clone cannot reach the compiler. |
| `patch` | `git reset --hard` and `git clean` to the pristine commit, then `git apply` each patch. A patch that does not apply stops the build. |
| `configure` | Fixed cmake options per variant. All variants use `LLVM_INCLUDE_TESTS=OFF`, because LLVM's lit is Python. The tests here are `llvm-mc` and `llc` outputs compared by shell and C tools (`oracle/`). |
| `build`, `install` | ninja with `PHI_JOBS` (default `nproc`, 128 on the rack). The clang variant installs to `~/.cache/intel-phi-llvm-build/llvm`, the dylib variant copies `libLLVM*.so*` to `llvm-dylib/lib`, and the card variant is packaged by the stack. |
| `check` | Compiles the stack's two ABI probes with this clang through the stack's `knc-cc` and runs `phi-isa-audit` on them. Expected: 0 illegal instructions, and x87 code for `double` and `float`. |

Variants:

- **clang:** X86 only, with the default triple `x86_64-unknown-linux-musl`.
- **dylib:** mirrors Arch's `llvm-libs` options so that the distro rustc
  loads it (Intel-Phi-3120A ADR 0007).
- **card:** a static clang on musl and libc++ built with `knc-cc`. It
  needs the clang variant's tablegen and the stack's sysroot with libc++.
