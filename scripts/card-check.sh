#!/usr/bin/env bash
# card-check.sh: on card N, verify and unpack the card-package tarball into
# its prefix, compile the C and C++ test programs with the card's own new
# clang, run them, compare with the expected output. Needs the card's
# control socket (phi CLI) and nothing else running a test there.
# See card-check.md.
set -euo pipefail
. "$(cd "$(dirname "$0")" && pwd)/env.sh"
CARD="${1:-0}"; PREFIX="${PHI_CARD_PREFIX:-/data/llvm-knc}"
out="$llvm_build/card-pkg"; t="$llvm_build/rt-test"
sum=$(awk '{print $1}' "$out/llvm-knc-card.tar.gz.sha256")
phi -c "$CARD" put "$out/llvm-knc-card.tar.gz" /data/llvm-knc-card.tar.gz
phi -c "$CARD" put "$t/c.c" /data/llvm-knc-c.c
phi -c "$CARD" put "$t/p.cc" /data/llvm-knc-p.cc
phi -c "$CARD" run sh -c "
set -e
got=\$(sha256sum /data/llvm-knc-card.tar.gz | cut -d' ' -f1)
[ \"\$got\" = $sum ] || { echo 'card-check: tarball checksum mismatch on the card; refusing'; exit 1; }
rm -rf $PREFIX; cd / && tar -xzf /data/llvm-knc-card.tar.gz && rm /data/llvm-knc-card.tar.gz
$PREFIX/bin/clang --version | head -1
s=\$(cut -d' ' -f1 /proc/uptime); $PREFIX/bin/clang -O2 -o /data/llvm-knc-c /data/llvm-knc-c.c -lm -lpthread; e=\$(cut -d' ' -f1 /proc/uptime); echo \"cc on the card: \$(awk -v s=\$s -v e=\$e 'BEGIN {printf \"%.2f\", e - s}') s\"
s=\$(cut -d' ' -f1 /proc/uptime); $PREFIX/bin/clang++ -O2 -o /data/llvm-knc-p /data/llvm-knc-p.cc; e=\$(cut -d' ' -f1 /proc/uptime); echo \"c++ on the card: \$(awk -v s=\$s -v e=\$e 'BEGIN {printf \"%.2f\", e - s}') s\"
/data/llvm-knc-c; echo \"c exit \$?\"
/data/llvm-knc-p; echo \"c++ exit \$?\"
rm -f /data/llvm-knc-c /data/llvm-knc-p /data/llvm-knc-c.c /data/llvm-knc-p.cc
"
