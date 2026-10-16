#!/usr/bin/env bash
# card-package.sh: the card variant's clang, lld and tools with the
# sysroot from runtimes.sh and the compiler's resource directory, in one
# tarball for PREFIX on the card (default /data/llvm-knc, beside the card's
# live /opt/phi toolchain, never over it). See card-package.md.
set -euo pipefail
. "$(cd "$(dirname "$0")" && pwd)/env.sh"
PREFIX="${PHI_CARD_PREFIX:-/data/llvm-knc}"
B="$llvm_build/build-card"; SYSROOT="$llvm_build/sysroot"
RES=$("$llvm_build/llvm/bin/clang" -print-resource-dir); ver=$(basename "$RES")
AUDIT="$PHI_STACK_ROOT/host/target/debug/phi-isa-audit"
out="$llvm_build/card-pkg"; rm -rf "$out"; pkg="$out/root$PREFIX"
mkdir -p "$pkg/bin" "$pkg/lib/clang/$ver/lib/linux"
cp "$B/bin/clang-$ver" "$pkg/bin/"; for n in clang clang++ cc c++; do ln -sfn "clang-$ver" "$pkg/bin/$n"; done
cp "$B/bin/lld" "$pkg/bin/"; for n in ld.lld ld; do ln -sfn lld "$pkg/bin/$n"; done
for t in llvm-ar llvm-ranlib llvm-nm llvm-objdump llvm-strip llvm-readelf llvm-objcopy; do cp "$B/bin/$t" "$pkg/bin/"; ln -sfn "$t" "$pkg/bin/${t#llvm-}"; done
{ cat "$llvm_root/config/knc.cfg"; echo "--sysroot=$PREFIX"; } > "$out/clang.cfg"
for n in clang clang++ cc c++; do cp "$out/clang.cfg" "$pkg/bin/$n.cfg"; done
mkdir -p "$pkg/usr"; cp -a "$SYSROOT/usr/include" "$SYSROOT/usr/lib" "$pkg/usr/"
cp -a "$RES/include" "$pkg/lib/clang/$ver/include"
cp "$RES"/lib/*/libclang_rt.builtins-x86_64.a "$RES"/lib/*/clang_rt.crt*-x86_64.o "$pkg/lib/clang/$ver/lib/linux/"
echo "== audit the card binaries (they run on the host's subset too)"
"$AUDIT" --ignore XSAVE "$pkg/bin/clang-$ver" | tail -1
"$AUDIT" --ignore XSAVE "$pkg/bin/lld" | tail -1
(cd "$out/root" && tar -czf "$out/llvm-knc-card.tar.gz" "${PREFIX#/}")
sha256sum "$out/llvm-knc-card.tar.gz" | tee "$out/llvm-knc-card.tar.gz.sha256"
ls -l "$out/llvm-knc-card.tar.gz"
