#!/usr/bin/env bash
# build.sh: fetch, verify, patch, build and install the KNC LLVM.
# Usage: scripts/build.sh [fetch|patch|configure|build|install|check|all]
# PHI_LLVM_VARIANT=clang (default) | dylib | card. See build.md.
set -euo pipefail
. "$(cd "$(dirname "$0")" && pwd)/env.sh"
unset CC CXX
HOST_CC="${PHI_HOST_CC:-/usr/bin/clang}"
HOST_CXX="${PHI_HOST_CXX:-/usr/bin/clang++}"
SRC="$llvm_build/llvm-project"
VARIANT="${PHI_LLVM_VARIANT:-clang}"
case "$VARIANT" in
    clang) BUILD="$llvm_build/build-clang"; PREFIX="$llvm_build/llvm" ;;
    dylib) BUILD="$llvm_build/build-dylib"; PREFIX="$llvm_build/llvm-dylib" ;;
    card)  BUILD="$llvm_build/build-card";  PREFIX="$llvm_build/llvm-card"
           HOST_BUILD="$llvm_build/build-clang" ;;
    *) echo "build.sh: unknown PHI_LLVM_VARIANT $VARIANT" >&2; exit 2 ;;
esac
JOBS="${PHI_JOBS:-$(nproc)}"
step="${1:-all}"
[ -n "$llvm_tag" ] && [ -n "$llvm_commit" ] || { echo "build.sh: patches/SERIES lacks the tag or commit line" >&2; exit 1; }

# The only step that uses the network. The clone is accepted only when its
# HEAD is the commit SERIES names: a moved tag or a tampered mirror stops here.
fetch() {
    if [ ! -d "$SRC/.git" ]; then
        echo "== fetching llvm-project $llvm_tag"
        git clone --depth 1 --branch "$llvm_tag" https://github.com/llvm/llvm-project.git "$SRC"
    elif [ "$(git -C "$SRC" rev-parse HEAD)" != "$llvm_commit" ]; then
        echo "== switching the clone to $llvm_tag"
        git -C "$SRC" fetch --depth 1 origin "refs/tags/$llvm_tag:refs/tags/$llvm_tag"
        git -C "$SRC" checkout -q --detach "$llvm_tag"
    fi
    verify
}

verify() {
    local head; head=$(git -C "$SRC" rev-parse HEAD)
    if [ "$head" != "$llvm_commit" ]; then
        echo "build.sh: $SRC is at $head, SERIES pins $llvm_commit ($llvm_tag); refusing" >&2
        exit 1
    fi
    echo "== source verified: $llvm_tag = $llvm_commit"
}

# Every patch onto the pristine pinned tree, in SERIES order; a patch that
# does not apply exactly stops the build.
patch_tree() {
    verify
    git -C "$SRC" reset -q --hard && git -C "$SRC" clean -qfd
    while IFS= read -r p; do
        case "$p" in ''|'#'*) continue ;; esac
        echo "== applying $p"
        git -C "$SRC" apply "$llvm_root/patches/$p"
    done < "$llvm_root/patches/SERIES"
    git -C "$SRC" diff --stat | tail -1
}

common_opts() {
    printf '%s\n' \
        -DCMAKE_BUILD_TYPE=Release \
        -DCMAKE_INSTALL_PREFIX="$PREFIX" \
        -DLLVM_ENABLE_ASSERTIONS=OFF \
        -DLLVM_INCLUDE_TESTS=OFF \
        -DLLVM_INCLUDE_BENCHMARKS=OFF \
        -DLLVM_INCLUDE_EXAMPLES=OFF \
        -DLLVM_INCLUDE_DOCS=OFF \
        -DLLVM_ENABLE_BINDINGS=OFF \
        -DCMAKE_C_COMPILER="$HOST_CC" \
        -DCMAKE_CXX_COMPILER="$HOST_CXX" \
        -DLLVM_USE_LINKER=lld
}

configure() {
    local -a opts; mapfile -t opts < <(common_opts)
    echo "== configuring variant $VARIANT into $BUILD, install to $PREFIX"
    case "$VARIANT" in
    card)
        [ -x "$HOST_BUILD/bin/llvm-tblgen" ] || { echo "card variant needs the clang variant built first (tablegen)" >&2; exit 1; }
        [ -f "$PHI_SYSROOT/usr/lib/libc++.a" ] || { echo "card variant needs libc++ in $PHI_SYSROOT (Intel-Phi-3120A toolchain/libcxx/build.sh)" >&2; exit 1; }
        cmake -S "$SRC/llvm" -B "$BUILD" -G Ninja \
            -DCMAKE_BUILD_TYPE=Release -DCMAKE_INSTALL_PREFIX="$PREFIX" \
            -DCMAKE_SYSTEM_NAME=Linux -DCMAKE_SYSTEM_PROCESSOR=x86_64 \
            -DCMAKE_C_COMPILER="$PHI_STACK_ROOT/toolchain/clang/knc-cc" \
            -DCMAKE_CXX_COMPILER="$PHI_STACK_ROOT/toolchain/clang/knc-c++" \
            -DCMAKE_ASM_COMPILER="$PHI_STACK_ROOT/toolchain/clang/knc-cc" \
            -DCMAKE_SYSROOT="$PHI_SYSROOT" \
            -DCMAKE_AR="$llvm_build/llvm/bin/llvm-ar" \
            -DCMAKE_RANLIB="$llvm_build/llvm/bin/llvm-ranlib" \
            -DCMAKE_NM="$llvm_build/llvm/bin/llvm-nm" \
            -DCMAKE_EXE_LINKER_FLAGS="-static" \
            -DLLVM_HOST_TRIPLE=x86_64-unknown-linux-musl \
            -DLLVM_DEFAULT_TARGET_TRIPLE=x86_64-unknown-linux-musl \
            -DLLVM_NATIVE_TOOL_DIR="$HOST_BUILD/bin" \
            -DLLVM_TABLEGEN="$HOST_BUILD/bin/llvm-tblgen" \
            -DCLANG_TABLEGEN="$HOST_BUILD/bin/clang-tblgen" \
            -DLLVM_ENABLE_PROJECTS="clang;lld" -DLLVM_TARGETS_TO_BUILD=X86 \
            -DLLVM_BUILD_STATIC=ON -DLLVM_ENABLE_PIC=OFF \
            -DLLVM_ENABLE_LIBCXX=ON -DLLVM_STATIC_LINK_CXX_STDLIB=ON \
            -DLLVM_ENABLE_THREADS=ON -DLLVM_ENABLE_ASSERTIONS=OFF \
            -DLLVM_ENABLE_ZLIB=OFF -DLLVM_ENABLE_ZSTD=OFF -DLLVM_ENABLE_LIBXML2=OFF \
            -DLLVM_ENABLE_TERMINFO=OFF -DLLVM_ENABLE_LIBEDIT=OFF -DLLVM_ENABLE_LIBPFM=OFF \
            -DLLVM_ENABLE_BINDINGS=OFF -DLLVM_ENABLE_OCAMLDOC=OFF \
            -DLLVM_INCLUDE_TESTS=OFF -DLLVM_INCLUDE_BENCHMARKS=OFF \
            -DLLVM_INCLUDE_EXAMPLES=OFF -DLLVM_INCLUDE_DOCS=OFF -DLLVM_BUILD_UTILS=OFF \
            -DLLVM_DISABLE_ASSEMBLY_FILES=ON -DLLVM_PARALLEL_LINK_JOBS=2 \
            -DCLANG_ENABLE_ARCMT=OFF -DCLANG_ENABLE_STATIC_ANALYZER=OFF \
            -DCLANG_DEFAULT_LINKER=lld -DCLANG_DEFAULT_RTLIB=compiler-rt \
            -DCLANG_DEFAULT_CXX_STDLIB=libc++ -DCLANG_DEFAULT_UNWINDLIB=libunwind ;;
    dylib)
        cmake -S "$SRC/llvm" -B "$BUILD" -G Ninja "${opts[@]}" \
            -DLLVM_ENABLE_PROJECTS="" -DLLVM_TARGETS_TO_BUILD=all \
            -DLLVM_BUILD_LLVM_DYLIB=ON -DLLVM_LINK_LLVM_DYLIB=ON \
            -DLLVM_ENABLE_RTTI=ON -DLLVM_ENABLE_FFI=ON \
            -DLLVM_STATIC_LINK_CXX_STDLIB=ON -DLLVM_PARALLEL_LINK_JOBS=2 ;;
    clang)
        cmake -S "$SRC/llvm" -B "$BUILD" -G Ninja "${opts[@]}" \
            -DLLVM_ENABLE_PROJECTS="clang;lld" -DLLVM_TARGETS_TO_BUILD=X86 \
            -DLLVM_DEFAULT_TARGET_TRIPLE=x86_64-unknown-linux-musl \
            -DLLVM_ENABLE_ZLIB=ON -DLLVM_ENABLE_ZSTD=OFF -DLLVM_ENABLE_LIBXML2=OFF \
            -DLLVM_ENABLE_TERMINFO=OFF -DLLVM_PARALLEL_LINK_JOBS=4 \
            -DCLANG_DEFAULT_LINKER=lld -DCLANG_DEFAULT_RTLIB=compiler-rt \
            -DCLANG_DEFAULT_CXX_STDLIB=libc++ -DCLANG_DEFAULT_UNWINDLIB=none ;;
    esac
}

build() {
    echo "== building with $JOBS jobs (variant $VARIANT)"
    case "$VARIANT" in
        card)  cmake --build "$BUILD" -j "$JOBS" --target clang lld llvm-ar llvm-ranlib llvm-nm llvm-objdump llvm-strip llvm-readelf llvm-objcopy ;;
        dylib) cmake --build "$BUILD" -j "$JOBS" --target LLVM ;;
        clang) cmake --build "$BUILD" -j "$JOBS" ;;
    esac
}

install() {
    case "$VARIANT" in
        card)  echo "== card variant: not installed; the stack's clang.sh packages $BUILD/bin" ;;
        dylib) mkdir -p "$PREFIX/lib"; cp -a "$BUILD"/lib/libLLVM*.so* "$PREFIX/lib/"; ls -l "$PREFIX/lib" | grep libLLVM ;;
        clang) cmake --install "$BUILD"; "$PREFIX/bin/clang" --version | head -1 ;;
    esac
}

# check: compile the stack's two ABI probes with this clang through knc-cc
# and audit them with phi-isa-audit (0 illegal instructions expected).
check() {
    [ "$VARIANT" = clang ] || { echo "== check covers the clang variant; dylib is checked by the stack's build-std.sh"; return; }
    local audit="$PHI_STACK_ROOT/host/target/debug/phi-isa-audit" tmp; tmp=$(mktemp -d)
    printf 'int f(int a,int b,int c){return c?a:b;}\nlong g(long a,long b,int c){return c?a:b;}\n' > "$tmp/t.c"
    printf 'double m(double a,double b){return a*b+1.5;}\nfloat h(float a){return a/3.0f;}\n' > "$tmp/d.c"
    for f in t d; do
        PHI_LLVM="$PREFIX" "$PHI_STACK_ROOT/toolchain/clang/knc-cc" -O2 -c "$tmp/$f.c" -o "$tmp/$f.o" -nostdinc -nostdlib
        "$audit" "$tmp/$f.o"
    done
    "$PREFIX/bin/llvm-objdump" -d --no-show-raw-insn "$tmp/d.o" | grep -E "fld|fmul|fadd|fstp|ret" | head -8
    rm -rf "$tmp"
}

case "$step" in
    fetch) fetch ;;
    patch) fetch; patch_tree ;;
    configure) fetch; patch_tree; configure ;;
    build) build ;;
    install) install ;;
    check) check ;;
    all) fetch; patch_tree; configure; build; install; check ;;
    *) echo "usage: build.sh [fetch|patch|configure|build|install|check|all]" >&2; exit 2 ;;
esac
