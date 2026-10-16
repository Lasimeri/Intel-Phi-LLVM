#!/usr/bin/env bash
# runtimes.sh: compiler-rt builtins and crt, libunwind, libc++abi and libc++
# built with this repository's clang (through the stack's knc-cc) into an
# isolated copy of the stack's musl sysroot, each library audited. The
# stack's live sysroot is never written. See runtimes.md.
set -euo pipefail
. "$(cd "$(dirname "$0")" && pwd)/env.sh"
SRC="$llvm_build/llvm-project"
export PHI_LLVM="$llvm_build/llvm"
STACK_SYSROOT="$PHI_SYSROOT"
export PHI_SYSROOT="$llvm_build/sysroot"
AUDIT="$PHI_STACK_ROOT/host/target/debug/phi-isa-audit"
CC="$PHI_STACK_ROOT/toolchain/clang/knc-cc"; CXX="$PHI_STACK_ROOT/toolchain/clang/knc-c++"
J="${PHI_JOBS:-$(nproc)}"
[ "$(git -C "$SRC" rev-parse HEAD)" = "$llvm_commit" ] || { echo "runtimes.sh: source is not the pinned commit; run scripts/build.sh patch" >&2; exit 1; }
echo "== sysroot: fresh copy of $STACK_SYSROOT (musl from the stack's build)"
rm -rf "$PHI_SYSROOT"; cp -a "$STACK_SYSROOT" "$PHI_SYSROOT"
# The copy carries the stack's libc++ and libunwind (other LLVM version):
# drop them so no stale header survives under the new install.
rm -rf "$PHI_SYSROOT"/usr/include/c++ "$PHI_SYSROOT"/usr/include/*unwind* "$PHI_SYSROOT"/usr/include/mach-o "$PHI_SYSROOT"/usr/lib/libc++*.a "$PHI_SYSROOT"/usr/lib/libunwind.a
common=(-G Ninja -DCMAKE_BUILD_TYPE=Release
    -DCMAKE_C_COMPILER="$CC" -DCMAKE_CXX_COMPILER="$CXX" -DCMAKE_ASM_COMPILER="$CC"
    -DCMAKE_C_COMPILER_TARGET=x86_64-unknown-linux-musl
    -DCMAKE_CXX_COMPILER_TARGET=x86_64-unknown-linux-musl
    -DCMAKE_ASM_COMPILER_TARGET=x86_64-unknown-linux-musl
    -DCMAKE_SYSROOT="$PHI_SYSROOT" -DCMAKE_TRY_COMPILE_TARGET_TYPE=STATIC_LIBRARY
    -DCMAKE_AR="$PHI_LLVM/bin/llvm-ar" -DCMAKE_RANLIB="$PHI_LLVM/bin/llvm-ranlib"
    -DCMAKE_NM="$PHI_LLVM/bin/llvm-nm")
resdir=$("$PHI_LLVM/bin/clang" -print-resource-dir)
B="$llvm_build/rt-compiler-rt"; rm -rf "$B"
cmake -S "$SRC/compiler-rt" -B "$B" "${common[@]}" \
    -DLLVM_CMAKE_DIR="$PHI_LLVM/lib/cmake/llvm" \
    -DCOMPILER_RT_DEFAULT_TARGET_ONLY=ON -DCOMPILER_RT_BUILD_BUILTINS=ON \
    -DCOMPILER_RT_BUILD_SANITIZERS=OFF -DCOMPILER_RT_BUILD_XRAY=OFF \
    -DCOMPILER_RT_BUILD_LIBFUZZER=OFF -DCOMPILER_RT_BUILD_PROFILE=OFF \
    -DCOMPILER_RT_BUILD_MEMPROF=OFF -DCOMPILER_RT_BUILD_ORC=OFF \
    -DCOMPILER_RT_BUILD_GWP_ASAN=OFF -DCOMPILER_RT_BUILD_CTX_PROFILE=OFF \
    -DCOMPILER_RT_INSTALL_PATH="$resdir" -DCOMPILER_RT_BAREMETAL_BUILD=OFF \
    -DCOMPILER_RT_X86_NO_SSE=ON > "$B.log" 2>&1
cmake --build "$B" -j"$J" >> "$B.log" 2>&1; cmake --install "$B" >> "$B.log" 2>&1
lib=$(find "$resdir/lib" -name 'libclang_rt.builtins*.a' | head -1)
echo "== audit $lib"; "$AUDIT" --allow-suspect "$lib" | tail -1
B="$llvm_build/rt-libcxx"; rm -rf "$B"
cmake -S "$SRC/runtimes" -B "$B" "${common[@]}" \
    -DLLVM_ENABLE_RUNTIMES="libunwind;libcxxabi;libcxx" -DCMAKE_INSTALL_PREFIX="$PHI_SYSROOT/usr" \
    -DLIBUNWIND_ENABLE_SHARED=OFF -DLIBUNWIND_ENABLE_STATIC=ON -DLIBUNWIND_USE_COMPILER_RT=ON \
    -DLIBUNWIND_HERMETIC_STATIC_LIBRARY=ON -DLIBUNWIND_ENABLE_CROSS_UNWINDING=OFF \
    -DLIBUNWIND_INSTALL_LIBRARY_DIR=lib -DLIBUNWIND_INSTALL_INCLUDE_DIR=include \
    -DLIBUNWIND_INCLUDE_TESTS=OFF -DLIBUNWIND_INCLUDE_DOCS=OFF \
    -DLIBCXXABI_HAS_CXA_THREAD_ATEXIT_IMPL=OFF -DLIBCXXABI_ENABLE_SHARED=OFF \
    -DLIBCXXABI_ENABLE_STATIC=ON -DLIBCXXABI_USE_COMPILER_RT=ON -DLIBCXXABI_USE_LLVM_UNWINDER=ON \
    -DLIBCXXABI_ENABLE_STATIC_UNWINDER=ON -DLIBCXXABI_INCLUDE_TESTS=OFF -DLIBCXXABI_INSTALL_LIBRARY_DIR=lib \
    -DLIBCXX_ENABLE_SHARED=OFF -DLIBCXX_ENABLE_STATIC=ON -DLIBCXX_USE_COMPILER_RT=ON \
    -DLIBCXX_HAS_MUSL_LIBC=ON -DLIBCXX_CXX_ABI=libcxxabi -DLIBCXX_ENABLE_STATIC_ABI_LIBRARY=ON \
    -DLIBCXX_STATICALLY_LINK_ABI_IN_STATIC_LIBRARY=ON -DLIBCXX_INCLUDE_TESTS=OFF \
    -DLIBCXX_INCLUDE_BENCHMARKS=OFF -DLIBCXX_INCLUDE_DOCS=OFF -DLIBCXX_INSTALL_LIBRARY_DIR=lib \
    -DLIBCXX_INSTALL_INCLUDE_DIR=include/c++/v1 -DLIBCXX_INSTALL_INCLUDE_TARGET_DIR=include/c++/v1 > "$B.log" 2>&1
cmake --build "$B" -j"$J" >> "$B.log" 2>&1; cmake --install "$B" >> "$B.log" 2>&1
for l in libunwind libc++abi libc++; do echo "== audit $l.a"; "$AUDIT" "$PHI_SYSROOT/usr/lib/$l.a" | tail -1; done
# A linked program: C with libm and threads, and C++ with exceptions.
t="$llvm_build/rt-test"; rm -rf "$t"; mkdir -p "$t"
printf '#include <stdio.h>\n#include <math.h>\n#include <pthread.h>\nstatic double r;\nstatic void *w(void *a){r=sqrt(*(double*)a)*1.5;return 0;}\nint main(void){double x=2.0;pthread_t t;pthread_create(&t,0,w,&x);pthread_join(t,0);printf("c %%.6f\\n",r);return r>2.12&&r<2.13?0:1;}\n' > "$t/c.c"
printf '#include <cstdio>\n#include <stdexcept>\n#include <vector>\nint main(){std::vector<double> v{1.5,2.5};try{throw std::runtime_error("x");}catch(const std::exception&e){std::printf("c++ %%s %%.1f\\n",e.what(),v[0]*v[1]);}return 0;}\n' > "$t/p.cc"
"$CC" -O2 -static -o "$t/c" "$t/c.c" -lm -lpthread
"$CXX" -O2 -static -o "$t/p" "$t/p.cc"
for x in c p; do "$AUDIT" "$t/$x" | tail -1; "$t/$x"; done
echo "runtimes.sh: PASS"
