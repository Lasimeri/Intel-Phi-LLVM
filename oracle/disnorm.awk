# disnorm.awk: canonical text of one AT&T instruction (whitespace already
# removed): drop comments, ",1)" scales, "0x0(" displacements, write the
# 64-bit wrapped absolute displacement as negative, put {eh} after the
# conversion, and move a lone rounding decoration to the front.
{
  t = $0
  sub(/#.*/, "", t)
  gsub(/,1\)/, ")", t)
  gsub(/0x0\(/, "(", t)
  if (match(t, /0xffffffffffffff[0-9a-f][0-9a-f]/)) { v = 256 - strtonum("0x" substr(t, RSTART + 16, 2)); t = substr(t, 1, RSTART - 1) sprintf("-0x%x", v) substr(t, RSTART + RLENGTH) }
  while (match(t, /\{eh\}\{[a-z0-9]+\}/)) { u = substr(t, RSTART + 4, RLENGTH - 4); t = substr(t, 1, RSTART - 1) u "{eh}" substr(t, RSTART + RLENGTH) }
  if (match(t, /\$0x[0-9a-f]+,\{r[a-z-]*\},/)) { i = substr(t, RSTART, index(substr(t, RSTART), ",")); r = substr(t, RSTART + length(i)); r = substr(r, 1, index(r, ",")); t = substr(t, 1, RSTART - 1) r i substr(t, RSTART + length(i) + length(r)) }
  print t
}
