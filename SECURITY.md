# Security

Scope: a compiler toolchain for hardware the owner controls. The threats
are what a toolchain can pass on: tampered inputs, a build that reaches
beyond its directory, and compiled programs weaker than they need to be.

## Inputs are pinned and verified

| Input | Pin | Where it is checked |
| --- | --- | --- |
| `llvm-project` | tag and peeled commit in `patches/SERIES` | `scripts/build.sh` `verify`, after every fetch and before every patch run. A mismatch stops the build. |
| MPSS sources (oracle) | SHA-256 of the outer tar and of the inner binutils tarball, in `oracle/SHA256SUMS` | `oracle/build-binutils.sh`, before extraction |
| Patches | git history of this repository | `git apply` onto the pristine verified tree; a patch that does not apply exactly stops the build |

`scripts/build.sh fetch` is the only step that uses the network. Configure,
build, install, check and the oracle all run offline.

## Isolation

- Every script refuses to run as root (`scripts/env.sh`). Builds,
  installs and the oracle stay under `~/.cache/intel-phi-llvm-build`.
  Nothing is installed system-wide, and nothing is put on `PATH`.
- The oracle (Intel's MPSS binutils 2.22, a 2011 code base built with
  warnings relaxed) is treated as untrusted code:
  - it runs as the ordinary user, only on Intel's own test file and on
    this repository's outputs;
  - its output is data to compare, never input to the build.
- GPL code from MPSS is never copied into the patches.

## Compiled output

The `knc` CPU will carry hardening defaults. Each one becomes a default
only after it is shown to work on a card (stage S1). Candidates:

| Measure | Status on KNC |
| --- | --- |
| `-fstack-protector-strong` | Candidate: needs musl's TLS canary at `%fs:0x28` and `__stack_chk_fail`. To be verified on a card. |
| static PIE, RELRO, `-z now`, `-z noexecstack` | Candidate: linker-side, ISA-neutral. To be verified with the card kernel's loader. |
| `-fstack-clash-protection` | Candidate: stack probes are plain loads, ISA-neutral. |
| `-fcf-protection` | Off, as in the stack's `knc-cc` today: `endbr64` is not in the KNC instruction list (ISA reference 327364, appendix B is the authority to cite when verified). |
| `_FORTIFY_SOURCE` | Not provided by musl itself; out of scope. |

## Reporting

A problem found in this repository is fixed here in the open, and
recorded in `docs/results/` with what was affected.
