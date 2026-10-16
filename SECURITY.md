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
| Intel ISA reference 327364-001 (the instruction table) | SHA-256 in `scripts/isa-extract.sh` | before `pdftotext` reads it; the extraction is committed (`isa/knc-isa.txt`) and the generated table files are checked against it by `make check` |

Two steps use the network:

- `scripts/build.sh fetch`, verified against the pinned commit;
- `scripts/rust-check.sh`, whose `cargo -Zbuild-std` downloads std's
  dependency crates from crates.io. They are pinned by the checksums in
  rust-src's own `Cargo.lock` (Arch package `rust-src`).

Configure, build, install, the runtimes, the card checks and the oracle
all run offline.

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

`config/knc.cfg` carries the defaults. Each one was verified on card 0 by
`scripts/hardening-check.sh` before it went in (2026-10-15):

| Measure | Status on KNC |
| --- | --- |
| `-fstack-protector-strong` | Default. musl's canary at `%fs:0x28`; an overflowing copy is stopped on the card (exit 139) |
| static PIE (`-fPIE -static-pie`) | Default. Two runs on the card load at two different addresses; card `randomize_va_space` 2 |
| RELRO, `-z now`, `-z noexecstack` | Default. `GNU_RELRO`, `BIND_NOW`, `GNU_STACK` RW; the card CPU reports `nx` |
| `-fstack-clash-protection` | Default. Large frames are probed; runs on the card |
| `-fcf-protection` | Off: `endbr64` is not in the KNC instruction list (the stack's `knc-cc` has always passed `-fcf-protection=none`) |
| `_FORTIFY_SOURCE` | Not provided by musl itself; out of scope |

The stack's own `knc-cc` and card `clang.cfg` keep their flags until it
adopts `config/knc.cfg`.

## Reporting

A problem found in this repository is fixed here in the open, and
recorded in `docs/results/` with what was affected.
