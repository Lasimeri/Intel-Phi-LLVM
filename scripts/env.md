# scripts/env.sh

Sourced by every script in this repository. It sets:

| Name | Value |
| --- | --- |
| `llvm_root` | The repository through the space-free alias `~/.cache/intel-phi-llvm`. cmake and ninja record absolute paths, and LLVM's shared-library link passes a version-script path unquoted, so the repository's real name, "Intel Phi LLVM", cannot appear in a build. |
| `llvm_build` | `~/.cache/intel-phi-llvm-build` (`PHI_LLVM_BUILD_ROOT`), the real build root: the source clone, the build trees, the installs and the oracle. |
| `PHI_STACK_ROOT` | `~/.cache/intel-phi-3120a`, the alias of the family stack, Intel-Phi-3120A. The card variant and the checks use its `knc-cc`, its musl sysroot and its `phi-isa-audit`. |
| `PHI_SYSROOT` | `~/.cache/intel-phi-3120a-build/toolchain/sysroot`. |
| `llvm_tag`, `llvm_commit` | The `# LLVM tag:` and `# LLVM commit:` lines of `patches/SERIES`, the one place the pinned source is written. |

It exits when run as root. Nothing here needs privileges: building as an
ordinary user keeps a hostile or broken input from reaching the system
(`SECURITY.md`).
