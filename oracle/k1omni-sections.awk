# k1omni-sections.awk: per-section comparison of llvm-mc with the oracle.
# Inputs (in this order): the test source (k1omni.s), the oracle's bytes per
# instruction (gas.bytes, one line each, in order), llvm-mc's stderr (error
# lines "file:LINE:COL: error: ...") and llvm-mc -show-encoding stdout.
{ file = FILENAME == ARGV[1] ? 1 : FILENAME == ARGV[2] ? 2 : FILENAME == ARGV[3] ? 3 : 4 }
file == 1 {
  if ($0 ~ /^[ \t]*#/) { sec = $0; sub(/^[ \t]*#[ \t]*/, "", sec); if (FNR > 3400) sec = "intel: " sec; next }
  if ($0 ~ /^[ \t]*$/ || $0 ~ /^[ \t]*\./ || $0 ~ /:[ \t]*$/) next
  n++; line[n] = FNR; secof[n] = sec; text[n] = $0
  if (!(sec in seen)) { seen[sec] = 1; order[++ns] = sec }
  next
}
file == 2 { xp[++ne] = $0; next }
file == 3 { if (match($0, /:[0-9]+:[0-9]+: error: /)) { split(substr($0, RSTART + 1), a, ":"); err[a[1]] = substr($0, RSTART + RLENGTH) } next }
file == 4 { if (match($0, /# encoding: \[[^]]*\]/)) { e = substr($0, RSTART + 13, RLENGTH - 14); gsub(/0x/, "", e); gsub(/,/, " ", e); enc[++nm] = e } next }
END {
  k = 0
  for (i = 1; i <= n; i++) {
    s = secof[i]; tot[s]++; T++
    if (line[i] in err) { ne2[s]++; NE++; if (shown_e++ < MAXSHOW) printf "ERR  %5d %s  <- %s\n       gas  %s\n", line[i], text[i], err[line[i]], xp[i] > "/dev/stderr"; continue }
    k++; got = enc[k]
    if (got == xp[i]) { ok[s]++; OK++ }
    else { bad[s]++; NB++; if (shown_b++ < MAXSHOW) printf "DIFF %5d %s\n       gas  %s\n       llvm %s\n", line[i], text[i], xp[i], got > "/dev/stderr" }
  }
  for (j = 1; j <= ns; j++) { s = order[j]; printf "%5d %5d %5d %5d  %s\n", tot[s], ok[s] + 0, bad[s] + 0, ne2[s] + 0, s }
  printf "TOTAL %d instructions: %d identical, %d differ, %d rejected (oracle %d, llvm-mc encodings %d)\n", T, OK, NB, NE, ne, nm
}
