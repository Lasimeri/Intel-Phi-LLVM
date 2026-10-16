#!/usr/bin/env bash
# check-distro.sh: does the pinned LLVM tag match the LLVM that Arch's
# llvm-libs and rustc use? Decision D1 (docs/decisions/0001) follows the
# distro version. See check-distro.md.
set -uo pipefail
cd "$(dirname "$0")/.."
tag=$(awk '/^# LLVM tag:/ {print $4}' patches/SERIES); pinned=${tag#llvmorg-}
command -v pacman >/dev/null || { echo "check-distro.sh: no pacman here; pinned $pinned, distro unknown"; exit 0; }
distro=$(pacman -Q llvm-libs 2>/dev/null | awk '{print $2}' | sed 's/-[0-9.]*$//')
rv=$(/usr/bin/rustc -vV 2>/dev/null | awk '/^LLVM version:/ {print $3}')
echo "check-distro.sh: pinned $pinned, llvm-libs ${distro:-absent}, rustc's LLVM ${rv:-absent}"
if [ -n "$distro" ] && [ "$distro" != "$pinned" ]; then
    echo "check-distro.sh: DRIFT: Arch moved to LLVM $distro; rebase the series (CONTRIBUTING.md)"
    exit 1
fi
