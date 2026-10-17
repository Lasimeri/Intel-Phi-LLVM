# extract-ops.awk: Intel 327364-001 (pdftotext -layout) -> knc-ops.txt, the
# register data flow of each instruction's Operation section, which the
# opcode tables do not state: whether the destination is also a source, and
# which mask registers the instruction writes. Run by scripts/isa-extract.sh
# beside extract-isa.awk; the format is in isa/README.md.
#   O|line|mnemonic|dest|reads-dest|writes   one instruction page
#     dest        the first operand of the page's first opcode row
#     reads-dest  1 when the destination appears on the right of an
#                 assignment or in a condition other than the write mask,
#                 or when the loop stops writing early (break) or a field
#                 "remains unchanged"
#     writes      the registers assigned to, space separated
function fix(s) { gsub(/ loat/, "float", s); gsub(/v ixup/, "vfixup", s); gsub(/sp lt/, "spflt", s); return s }
function flush(   r, w, k) {
  if (mn != "" && seen) {
    w = ""; for (k in lhs) w = w (w == "" ? "" : " ") k
    print "O|" start "|" mn "|" dst "|" (rd ? 1 : 0) "|" w
  }
  mn = ""; dst = ""; rdname = ""; rd = 0; written = 0; seen = 0; delete lhs; inop = 0
}
NR < 3500 || NR > 29600 { next }
/^ *Opcode +Instruction +Description/ { flush(); start = NR; want = 1; next }
want && match($0, /(MVEX|VEX)\.[A-Za-z0-9.]+/) && RSTART < 30 {
  s = fix($0); sub(/^ *(MVEX|VEX)\.[A-Za-z0-9.]+( +[0-9A-F][0-9A-F]| +\/[r0-7]| +ib| +id| +\/vsib)* +/, "", s)
  n = split(s, w, /[ ,]+/); mn = w[1]; dst = w[2]; sub(/\{.*/, "", dst)
  # the scalar pages (appendix A) call the destination DEST
  rdname = dst ~ /^r(32|64)$/ ? "DEST" : dst; want = 0; next }
/^ *Operation *$/ && mn != "" { inop = 1; seen = 1; next }
inop && /^ *(SIMD Floating-Point Exceptions|Denormal Handling|Flags Affected|Memory Up-conversion|Memory Down-conversion|Register Swizzle|Intel. C\/C\+\+ Compiler|Exceptions|Description) *$/ { inop = 0; next }
inop && !/Reference Number|^CHAPTER/ {
  # a loop that stops writing (a "break" line; "break;" ends a switch case)
  # or a field left as it was: the
  # destination keeps lanes or bits it had, so it is read
  if (dst ~ /^(zmm|k)[0-9]$/ && ($0 ~ /^ *break *$/ || $0 ~ /remains unchanged/)) rd = 1
  l = $0; sub(/\/\/.*/, "", l)
  # assignments: NAME[...] = rhs or NAME = rhs
  if (match(l, /^ *[A-Za-z_][A-Za-z0-9_]*(\[[^]]*\])? *= */)) {
    t = substr(l, RSTART, RLENGTH); rhs = substr(l, RSTART + RLENGTH)
    name = t; sub(/^ */, "", name); sub(/[[ =].*/, "", name)
    if (name ~ /^(zmm|k)[0-9]$/) lhs[name] = 1
    if (!written && rdname != "" && rhs ~ ("(^|[^A-Za-z0-9])" rdname "([^0-9]|$)")) rd = 1
    # a whole-register assignment first (DEST = 0): later reads are its own
    if (name == rdname && t !~ /\[/) written = 1
  } else if (!written && rdname != "" && rdname != "k1" && l ~ ("(^|[^A-Za-z0-9])" rdname "\\[")) rd = 1
}
END { flush() }
