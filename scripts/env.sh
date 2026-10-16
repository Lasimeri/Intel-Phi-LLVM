# env.sh: sourced by every script here. Sets llvm_root (space-free alias
# of this repository), llvm_build (real, space-free build root), the
# family stack root (Intel-Phi-3120A, for knc-cc, the musl sysroot and
# phi-isa-audit) and refuses to run as root. See env.md.
_env_real=$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)
_cache="${XDG_CACHE_HOME:-$HOME/.cache}"
if [ "$(id -u)" = 0 ]; then
    echo "env.sh: refusing to run as root; nothing here needs privileges" >&2
    exit 1
fi
case "$_env_real" in
    *[[:space:]]*)
        _link="$_cache/intel-phi-llvm"
        mkdir -p "$_cache"
        if [ ! -L "$_link" ] || [ "$(readlink "$_link")" != "$_env_real" ]; then
            ln -sfn "$_env_real" "$_link"
        fi
        llvm_root="$_link" ;;
    *) llvm_root="$_env_real" ;;
esac
export llvm_root
export llvm_build="${PHI_LLVM_BUILD_ROOT:-$_cache/intel-phi-llvm-build}"
mkdir -p "$llvm_build"
# The family stack: its space-free alias, created by its own env.sh.
export PHI_STACK_ROOT="${PHI_STACK_ROOT:-$_cache/intel-phi-3120a}"
export PHI_SYSROOT="${PHI_SYSROOT:-$_cache/intel-phi-3120a-build/toolchain/sysroot}"
# Pinned source: the tag and its peeled commit, read from patches/SERIES.
llvm_tag=$(awk '/^# LLVM tag:/ {print $4}' "$_env_real/patches/SERIES")
llvm_commit=$(awk '/^# LLVM commit:/ {print $4}' "$_env_real/patches/SERIES")
export llvm_tag llvm_commit
