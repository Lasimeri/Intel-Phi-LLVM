# scripts/rust-check.sh

The Rust half of the S0 gate. The distro rustc (`/usr/bin/rustc`, linked
against `libLLVM.so.X.Y` of Arch's `llvm-libs`) loads this repository's
patched dylib through `LD_LIBRARY_PATH`, as Intel-Phi-3120A ADR 0007
describes. The rustup `rustc` on `PATH` is not used, because it ships its
own differently named LLVM.

1. **Name check:** `ldd` reads the libLLVM file name that rustc links, and
   the patched dylib must exist under that name (`llvm-dylib/lib`).
2. **Probe:** a `no_core` probe with no std and no `rust-src` is compiled
   for the stack's `x86_64-knc-linux-musl.json` target.
   - Pass: `extern "C" fn rmul(f64, f64) -> f64` disassembles to x87
     (`fldl`, `fmull`), the same ABI as clang.
   - `phi-isa-audit` must also report no illegal instructions in the
     object.
3. **build-std (optional):** when `rust-src` is installed, the stack's
   `hello_rs` crate is copied to the build root and built with
   `-Zbuild-std`, then audited. This step is skipped, and the skip
   reported, when rust-src is missing; installing it needs sudo.

Linker: the stack's `knc-cc` with `PHI_LLVM` pointing at this repository's
clang install.

4. **Linked program:** after build-std, the stack's
   `toolchain/check/hello.c` is linked with `-DHAVE_RUST`, the Rust static
   library and `-lunwind` (the stack's P2 check), using this clang and
   sysroot. The program is audited and run on the host, and with
   `PHI_RUST_CARD=N` also on card N.

`cargo` is given `RUSTC=/usr/bin/rustc`. Without it, cargo runs the
rustup `rustc` that comes first on the rack's `PATH` (measured
2026-10-15), and that toolchain has neither rust-src nor the patched
LLVM.

Network: build-std downloads std's dependency crates from crates.io
(`getopts`, `object`, `gimli` and others), pinned by the checksums in
rust-src's own `Cargo.lock`. `SECURITY.md` lists this.
