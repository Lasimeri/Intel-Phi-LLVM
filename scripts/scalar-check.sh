#!/usr/bin/env bash
# scalar-check.sh: what S3's changes to the knc CPU do to scalar code on a
# card: the in-order scheduling model (X86ScheduleKNC.td), sahf, and
# -falign-loops=1 in config/knc.cfg. Three of Intel-Phi-3120A's scalar
# programs (card/examples pi-bbp, mandel, membw) are built twice, by the
# compiler before S3 (REF_BIN, with knc.cfg minus -falign-loops) and by this
# one (LLVM_BIN, with knc.cfg), and run on card CARD in alternating order.
# Prints the median time and range of each. See scalar-check.md.
#
#   REF_BIN=.../ref-s2/bin scripts/scalar-check.sh [CARD] [ROUNDS]
set -uo pipefail
. "$(cd "$(dirname "$0")" && pwd)/env.sh"
CARD="${1:-0}"; ROUNDS="${2:-5}"
REF="${REF_BIN:?set REF_BIN to the compiler before the change}"; NEW="${LLVM_BIN:-$llvm_build/llvm/bin}"
EX="$PHI_STACK_ROOT/card/examples"; w="$llvm_build/scalar-check"; rm -rf "$w"; mkdir -p "$w"
grep -v '^-falign-loops' "$llvm_root/config/knc.cfg" > "$w/ref.cfg"
build() { # tag bin cfg
    "$2/clang" --config="$3" --sysroot="$llvm_build/sysroot" -O2 -o "$w/pi-bbp.$1" "$EX/pi-bbp.c" -lpthread &&
    "$2/clang" --config="$3" --sysroot="$llvm_build/sysroot" -O2 -o "$w/mandel.$1" "$EX/mandel.c" -lz -lpthread -lm &&
    "$2/clang" --config="$3" --sysroot="$llvm_build/sysroot" -O2 -o "$w/membw.$1" "$EX/membw.c" -lpthread
}
build ref "$REF" "$w/ref.cfg" && build new "$NEW" "$llvm_root/config/knc.cfg" || exit 1
for p in pi-bbp mandel membw; do
    cmp -s "$w/$p.ref" "$w/$p.new" && echo "$p: the two binaries are identical"
    phi -c "$CARD" put "$w/$p.ref" "/data/sc-$p.ref" < /dev/null > /dev/null
    phi -c "$CARD" put "$w/$p.new" "/data/sc-$p.new" < /dev/null > /dev/null
done
# one round: each program, both builds, the order alternating; the card's
# /proc/uptime has 10 ms steps, so each run is timed by the program itself
cat > "$w/round.sh" <<'EOS'
cd /data; chmod +x sc-*
for t in $ORDER; do
  ./sc-pi-bbp.$t 3000 1 | sed "s/^/pi $t: /"
  ./sc-mandel.$t /dev/null 512 384 512 1 | sed "s/^/mandel $t: /"
  ./sc-membw.$t 64 1 3 | tail -1 | sed "s/^/membw $t: /"
done
EOS
for r in $(seq 1 "$ROUNDS"); do
    order=$([ $((r % 2)) = 1 ] && echo "ref new" || echo "new ref")
    phi -c "$CARD" run sh -c "ORDER='$order'; $(cat "$w/round.sh")" < /dev/null 2>&1
done | tee "$w/card$CARD.txt"
phi -c "$CARD" run sh -c 'rm -f /data/sc-*' < /dev/null > /dev/null 2>&1
# the summary: median and range per program and build
awk '
  /^pi / { t = $2; sub(/:$/, "", t); for (i = 1; i <= NF; i++) if ($(i + 1) == "s,") { v["pi-bbp s", t] = v["pi-bbp s", t] " " $i } }
  /^mandel / { t = $2; sub(/:$/, "", t); for (i = 1; i <= NF; i++) { if ($(i + 1) == "s" && $(i + 2) == "render") v["mandel render s", t] = v["mandel render s", t] " " $i; if ($(i + 1) == "s" && $(i + 2) == "deflate") v["mandel deflate s", t] = v["mandel deflate s", t] " " $i } }
  /^membw / { t = $2; sub(/:$/, "", t); v["membw GB/s", t] = v["membw GB/s", t] " " $(NF - 1) }
  function stat(s,   a, n, i, j, x) { n = split(s, a, " "); for (i = 1; i <= n; i++) for (j = i + 1; j <= n; j++) if (a[j] + 0 < a[i] + 0) { x = a[i]; a[i] = a[j]; a[j] = x }
    return sprintf("median %s [%s..%s] (%d runs)", a[int((n + 1) / 2)], a[1], a[n], n) }
  END { split("pi-bbp s|mandel render s|mandel deflate s|membw GB/s", k, "|")
        for (i = 1; i <= 4; i++) printf "%-17s before S3 %s   S3 %s\n", k[i], stat(v[k[i], "ref"]), stat(v[k[i], "new"]) }' "$w/card$CARD.txt"
