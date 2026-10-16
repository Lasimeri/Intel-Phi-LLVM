# extract-isa.awk: Intel 327364-001 (pdftotext -layout) -> knc-isa.txt. Run by
# scripts/isa-extract.sh; the format is in isa/README.md.
# Records (pipe separated, source line first):
#   B|line                              the start of an opcode table (a section)
#   I|line|mnemonic|opcode|operands      one row of an opcode table
#   T|line|kind|class|sss:usage:N ...    a conversion table (kind up, down,
#                                        reg0 = MVEX.EH=0 swizzles,
#                                        reg1 = MVEX.EH=1 rounding/SAE)
#   P|line|pseudo|base|predicate         an "Instruction Pseudo-ops" row
#   C|line|predicate|imm                 an "Immediate Format" row
# The PDF drops the fi/fl ligatures ("loat", "v ixupnanpd", "sp lt"); fix().
function fix(s) {
  gsub(/ loat/, "float", s); gsub(/v ixup/, "vfixup", s); gsub(/sp lt/, "spflt", s)
  s = gensub(/([SUD][fi]) ?(32|64) ?\(/, "\\1\\2(", "g", s); s = gensub(/([SUD][fi]) (32|64)/, "\\1\\2", "g", s)
  gsub(/ +\)/, ")", s); gsub(/ +,/, ",", s); gsub(/  +/, " ", s)
  sub(/^ +/, "", s); sub(/ +$/, "", s); return s
}
function flush() {
  if (op != "") {
    ins = fix(ins)
    # keep the operand vocabulary only: stop at the first plain English word
    n = split(ins, w, " "); out = w[1]
    for (i = 2; i <= n; i++) {
      if (w[i] !~ /^(\{?k[0-9]\}?,?|zmm[0-9](\/mt)?,?|\{k1\},?|[SUD][fi](32|64)\((zmm[0-9](\/mt)?|mt|mvt)\),?|mt,?|mvt,?|m,?|m8|imm8|offset|r32|r64|r32,|r64,|rel8|rel32|k[0-9],)$/) break
      out = out " " w[i]
    }
    print "I|" start "|" fix(op) "|" out
  }
  op = ""; ins = ""
}
BEGIN { OFS = "|" }
NR < 3500 || NR > 29600 { next }
/^ *Opcode +Instruction +Description/ { flush(); tflush(); kind = ""; print "B|" NR; on = 1; next }
on && (/^ *(Description|Operation) *$/ || /Reference Number/ || /^CHAPTER/) { flush(); on = 0 }
on {
  if (match($0, /(MVEX|VEX)\.[A-Za-z0-9.]+/) && RSTART < 30) {
    flush(); start = NR; line = $0; oc = RSTART
    rest = substr(line, oc); n = split(rest, t, / +/); p = oc; mc = 0
    for (i = 1; i <= n; i++) {
      q = index(substr(line, p), t[i]) + p - 1
      if (t[i] ~ /^[a-z][a-z0-9]*$/ && t[i] !~ /^(ib|id|iw)$/) { mc = q; break }
      p = q + length(t[i])
    }
    mcol = mc; dcol = 0
    s = substr(line, mc)
    if (match(s, /  +[A-Z][a-z][a-z]/)) dcol = mc + RSTART - 1 + RLENGTH - 3
    op = substr(line, oc, mc - oc)
    ins = dcol ? substr(line, mc, dcol - mc) : substr(line, mc)
    next
  }
  if (op != "" && mcol) {
    a = substr($0, 1, mcol - 1); b = dcol ? substr($0, mcol, dcol - mcol) : substr($0, mcol)
    if (a ~ /[^ ]/) op = op " " a
    if (b ~ /[^ ]/) ins = ins " " b
  }
  next
}
# conversion tables
/Memory Up-conversion:/   { tflush(); kind = "up";   cls = fix(substr($0, index($0, ":") + 1)); tl = NR; next }
/Memory Down-conversion:/ { tflush(); kind = "down"; cls = fix(substr($0, index($0, ":") + 1)); tl = NR; next }
/Register Swizzle/        { tflush(); kind = "reg0"; cls = fix(substr($0, index($0, "Swizzle") + 7)); sub(/^: */, "", cls); if (cls == "") cls = "-"; tl = NR; next }
/MVEX\.EH=0/ && kind ~ /^reg/ { tflush(); kind = "reg0"; tl = NR; next }
/MVEX\.EH=1/ && kind ~ /^reg/ { tflush(); kind = "reg1"; tl = NR; next }
kind != "" && /^ +([01][01][01]|1xx) / {
  r = fix($0); bits = $1; u = ""
  s = r; while (match(s, /\{[^}]*\}/)) { tok = substr(s, RSTART, RLENGTH); gsub(/ /, "", tok); u = u tok; s = substr(s, RSTART + RLENGTH) }
  if (r ~ /N\/A/ && u == "") u = "reserved"
  nn = $NF; if (nn !~ /^[0-9]+$/) nn = "-"
  rows = rows " " bits ":" (u == "" ? "-" : u) ":" (kind ~ /^reg/ ? "-" : nn)
  next
}
kind != "" && /(Intel|Exceptions|Instruction Pseudo|Denormal|Register Swizzle|Memory)/ { tflush() }
function tflush() { if (kind != "" && rows != "") print "T|" tl "|" kind "|" cls "|" substr(rows, 2); rows = ""; if (kind != "reg0" && kind != "reg1") kind = ""; }
# pseudo-ops and immediate formats
/^ +Pseudo-Op +Implementation/ { ps = 1; next }
ps && /^ +v[a-z0-9]+ / { p1 = $1; m = $0; sub(/.*\) +/, "", m); split(m, z, " "); pr = $NF; gsub(/[{}]/, "", pr); print "P|" NR "|" p1 "|" z[1] "|" pr; next }
ps && /^ *$/ { next }
ps { ps = 0 }
/^ +Immediate Format/ { imf = 1; next }
imf && /^ +[a-z]+ +[A-Z].* [01] +[01] +[01] *$/ { print "C|" NR "|" $1 "|" ($(NF-2) * 4 + $(NF-1) * 2 + $NF); next }
imf && /^ +(Operation|Description)/ { imf = 0 }
END { flush(); tflush() }
