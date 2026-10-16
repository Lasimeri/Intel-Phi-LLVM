#!/usr/bin/env bash
# rust-check.sh: the distro rustc with this repository's libLLVM substituted
# (Intel-Phi-3120A ADR 0007). 1: a no_core probe, no std needed, must give
# the knc64-x87 ABI. 2: the stack's hello_rs with build-std, audited, when
# rust-src is installed. See rust-check.md.
set -euo pipefail
. "$(cd "$(dirname "$0")" && pwd)/env.sh"
RUSTC=/usr/bin/rustc; CARGO=/usr/bin/cargo
DYLIB="$llvm_build/llvm-dylib/lib"
want=$(ldd "$("$RUSTC" --print sysroot)"/lib/librustc_driver-*.so 2>/dev/null | awk '/libLLVM/ {print $1}')
[ -e "$DYLIB/$want" ] || { echo "rust-check.sh: rustc links $want, not in $DYLIB (build the dylib variant)" >&2; exit 1; }
export LD_LIBRARY_PATH="$DYLIB${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}" RUSTC_BOOTSTRAP=1
echo "== rustc $("$RUSTC" -V | cut -d' ' -f2) loads $(ldd "$("$RUSTC" --print sysroot)"/lib/librustc_driver-*.so 2>/dev/null | awk '/libLLVM/ {print $3}')"
spec="$PHI_STACK_ROOT/toolchain/rust/x86_64-knc-linux-musl.json"
w="$llvm_build/rust-check"; rm -rf "$w"; mkdir -p "$w"
cat > "$w/probe.rs" <<'RS'
#![feature(no_core, lang_items)]
#![no_core]
#![no_std]
#[lang = "pointee_sized"] pub trait PointeeSized {}
#[lang = "meta_sized"] pub trait MetaSized: PointeeSized {}
#[lang = "sized"] pub trait Sized: MetaSized {}
#[lang = "copy"] pub trait Copy {}
impl Copy for f64 {}
#[lang = "mul"] pub trait Mul<R = Self> { type Output; fn mul(self, r: R) -> Self::Output; }
impl Mul for f64 { type Output = f64; fn mul(self, r: f64) -> f64 { self * r } }
#[no_mangle] pub extern "C" fn rmul(a: f64, b: f64) -> f64 { a * b }
#[no_mangle] pub extern "C" fn rsel(a: i64, b: i64, c: bool) -> i64 { if c { a } else { b } }
RS
"$RUSTC" -Zunstable-options --target "$spec" --crate-type lib --emit obj -C opt-level=2 -C panic=abort -o "$w/probe.o" "$w/probe.rs" 2>&1 | grep -v 'future\|warning\|^ *|\|^ *=\|^$' || true
"$llvm_build/llvm/bin/llvm-objdump" -d --no-show-raw-insn "$w/probe.o" | awk '/<rmul>:/,/ret/' | tee "$w/rmul.txt"
"$PHI_STACK_ROOT/host/target/debug/phi-isa-audit" "$w/probe.o"
grep -q 'fldl' "$w/rmul.txt" && grep -q 'fmull' "$w/rmul.txt" && echo "probe: x87 ABI (fldl/fmull): PASS" || { echo "probe: FAIL"; exit 1; }
if [ -d "$("$RUSTC" --print sysroot)/lib/rustlib/src/rust/library" ]; then
    cp -r "$PHI_STACK_ROOT/toolchain/check/hello_rs" "$w/hello_rs"; rm -rf "$w/hello_rs/target"
    sed -i "s|\.\./\.\./rust/|$PHI_STACK_ROOT/toolchain/rust/|; s|\.\./\.\./clang/|$PHI_STACK_ROOT/toolchain/clang/|" "$w/hello_rs/.cargo/config.toml"
    (cd "$w/hello_rs" && PHI_LLVM="$llvm_build/llvm" "$CARGO" -Zjson-target-spec build --release)
    "$PHI_STACK_ROOT/host/target/debug/phi-isa-audit" --ignore XSAVE "$w/hello_rs/target/x86_64-knc-linux-musl/release/libhello_rs.a"
else
    echo "build-std: SKIPPED, rust-src not installed (sudo pacman -S --needed rust-src)"
fi
