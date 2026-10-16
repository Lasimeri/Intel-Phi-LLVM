#!/usr/bin/env bash
# build-binutils.sh: build Intel's MPSS binutils 2.22 (K1OM gas and objdump)
# as the MVEX encoding oracle. GPL-3: built and run only, never copied
# into the patches. Inputs verified by SHA256SUMS. See build-binutils.md.
set -euo pipefail
. "$(cd "$(dirname "$0")/.." && pwd)/scripts/env.sh"
TAR="${PHI_MPSS_SRC_TAR:-$PHI_STACK_ROOT/vendor/mpss-3.8.6/mpss-src-3.8.6.tar}"
O="$llvm_build/oracle"
mkdir -p "$O/src"
sum_of() { sha256sum "$1" | awk '{print $1}'; }
want() { awk -v n="$1" '$2 == n {print $1}' "$llvm_root/oracle/SHA256SUMS"; }
[ "$(sum_of "$TAR")" = "$(want mpss-3.8.6/mpss-src-3.8.6.tar)" ] || { echo "build-binutils.sh: $TAR does not match SHA256SUMS; refusing" >&2; exit 1; }
inner=mpss-3.8.6/src/binutils-2.22+mpss3.8.6.tar.bz2
tar -xf "$TAR" -C "$O/src" "$inner"
[ "$(sum_of "$O/src/$inner")" = "$(want "$inner")" ] || { echo "build-binutils.sh: $inner does not match SHA256SUMS; refusing" >&2; exit 1; }
echo "== inputs verified"
rm -rf "$O/src/binutils-2.22+mpss3.8.6" "$O/build"
tar -xjf "$O/src/$inner" -C "$O/src"
mkdir -p "$O/build"; cd "$O/build"
# 2011 sources against a 2026 gcc: old C idioms are warnings, not errors.
"$O/src/binutils-2.22+mpss3.8.6/configure" --target=x86_64-k1om-linux --prefix="$O/inst" \
    --disable-nls --disable-werror --disable-gdb --disable-sim --without-zlib \
    CFLAGS="-O2 -fcommon -std=gnu89 -Wno-error -Wno-implicit-function-declaration -Wno-int-conversion -Wno-incompatible-pointer-types" > configure.log 2>&1
make -j"${PHI_JOBS:-$(nproc)}" MAKEINFO=true all-gas all-binutils > make.log 2>&1
ls -l gas/as-new binutils/objdump
