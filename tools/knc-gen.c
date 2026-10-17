/* knc-gen.c: the Knights Corner instruction table -> LLVM X86 TableGen defs and
 * the C++ side table that the assembler, the printers, the encoder and the
 * disassembler share (patch 0013). See knc-gen.md.
 *
 *   knc-gen knc-isa.txt knc-overrides.txt X86InstrKNC.td X86KNCInfo.inc [listing [sweep]]
 *   knc-gen --codegen knc-isa.txt knc-overrides.txt knc-ops.txt OUTDIR
 *
 * Input: isa/knc-isa.txt, extracted from Intel's 327364-001 by
 * isa/extract-isa.awk, and isa/knc-overrides.txt, where Intel's K1OM gas
 * (the oracle) and the manual disagree. Every opcode row becomes one def, or
 * two when its source operand is register or memory; the conversion tables
 * of the row's section give the decorations each form accepts and the
 * disp8*N of every memory conversion. */
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define MAXI 512
#define MAXT 1024
#define MAXD 1024
#define MAXP 64

enum { K_VR, K_KR, K_GR32, K_GR64, K_MEM, K_VSIB, K_IMM, K_M8, K_RM };
static const char *kname[] = {"K_VR", "K_KR", "K_GR32", "K_GR64", "K_MEM", "K_VSIB", "K_IMM", "K_M8", "K_RM"};
enum { R_NONE, R_REG, R_VVVV, R_RM, R_IMM };
static const char *rname[] = {"R_NONE", "R_REG", "R_VVVV", "R_RM", "R_IMM"};

/* operand flags */
#define F_MASK 1 /* carries {k} */
#define F_CONV 2 /* carries the swizzle/conversion decoration */
#define F_EH 4   /* accepts {eh} */
#define F_HALF 8 /* register written as ymm: the low half (vcvtdq2pd and kin) */
#define F_MASKREQ 16 /* the write mask must be written (gathers, scatters, vaddsetsps) */

typedef struct { int kind, flags; char cls[8]; } Opd;
typedef struct {
	int line, block;
	char mnem[32];
	int mvex, nds, ndd, pp, map, w, eh, opc, modrm, imm, vsib;
	int nops; Opd op[5];
} Row;
typedef struct { int block, line; char kind[8], cls[8]; char tok[8][16]; int n[8]; int valid[8]; } Tab;
typedef struct { char tok[8][16]; int n[8]; int valid[8]; char alias[16]; } Deco;
typedef struct { char pseudo[32], base[32]; int imm; } Pseudo;

static Row rows[MAXI]; static int nrows;
static Tab tabs[MAXT]; static int ntabs;
static Deco decos[MAXD]; static int ndecos = 1; /* 0 = none */
static Pseudo pseudos[MAXP]; static int npseudos;
static char rounding_override[16][32]; static int nrounding;
static char nosae_override[16][32]; static int nnosae;
static char maskreq_override[16][32]; static int nmaskreq;

static void die(const char *m, int line) { fprintf(stderr, "knc-gen: %s (isa line %d)\n", m, line); exit(1); }

static char *trim(char *s) {
	while (*s == ' ') s++;
	size_t n = strlen(s);
	while (n && (s[n - 1] == ' ' || s[n - 1] == '\n')) s[--n] = 0;
	return s;
}

static int split(char *s, char sep, char **out, int max) {
	int n = 0; out[n++] = s;
	for (; *s; s++) if (*s == sep) { *s = 0; if (n < max) out[n++] = s + 1; }
	return n;
}

static void parse_opd(Row *r, char *t) {
	Opd *o = &r->op[r->nops++];
	memset(o, 0, sizeof *o);
	t = trim(t);
	char *k = strstr(t, "{k1}");
	if (k) { o->flags |= F_MASK; *k = 0; t = trim(t); }
	if (!strcmp(t, "imm8") || !strcmp(t, "offset")) { o->kind = K_IMM; return; }
	if (!strcmp(t, "r32")) { o->kind = K_GR32; return; }
	if (!strcmp(t, "r64")) { o->kind = K_GR64; return; }
	if (!strcmp(t, "m8")) { o->kind = K_M8; return; }
	if (!strcmp(t, "mt") || !strcmp(t, "m")) { o->kind = K_MEM; o->flags |= F_EH; return; }
	if (!strcmp(t, "mvt")) { o->kind = K_VSIB; o->flags |= F_EH; return; }
	if (t[0] == 'k' && isdigit((unsigned char)t[1]) && !t[2]) { o->kind = K_KR; return; }
	if (!strncmp(t, "zmm", 3) && isdigit((unsigned char)t[3])) {
		o->kind = strstr(t, "/mt") ? K_RM : K_VR;
		if (o->kind == K_RM) o->flags |= F_EH;
		return;
	}
	if ((t[0] == 'S' || t[0] == 'U' || t[0] == 'D') && (t[1] == 'f' || t[1] == 'i') && t[4] == '(') {
		memcpy(o->cls, t, 4); o->cls[4] = 0; o->flags |= F_CONV;
		char *in = t + 5;
		if (!strncmp(in, "zmm", 3)) o->kind = strstr(in, "/mt") ? K_RM : K_VR;
		else if (!strncmp(in, "mvt", 3)) o->kind = K_VSIB;
		else if (!strncmp(in, "mt", 2)) o->kind = K_MEM;
		else die("bad conversion operand", r->line);
		if (o->kind != K_VR) o->flags |= F_EH;
		return;
	}
	fprintf(stderr, "operand '%s'\n", t);
	die("unknown operand", r->line);
}

static int parse_row(Row *r, char *spec, char *ins) {
	char *f[16]; int n = split(spec, ' ', f, 16);
	char *e[12]; int ne = split(f[0], '.', e, 12);
	r->mvex = !strcmp(e[0], "MVEX");
	r->w = 2; r->eh = 2;
	for (int i = 1; i < ne; i++) {
		if (!strcmp(e[i], "NDS")) r->nds = 1;
		else if (!strcmp(e[i], "NDD")) r->ndd = 1;
		else if (!strcmp(e[i], "66")) r->pp = 1;
		else if (!strcmp(e[i], "F3")) r->pp = 2;
		else if (!strcmp(e[i], "F2")) r->pp = 3;
		else if (!strcmp(e[i], "0F")) r->map = 1;
		else if (!strcmp(e[i], "0F38")) r->map = 2;
		else if (!strcmp(e[i], "0F3A")) r->map = 3;
		else if (!strcmp(e[i], "W0")) r->w = 0;
		else if (!strcmp(e[i], "W1")) r->w = 1;
		else if (!strcmp(e[i], "EH0")) r->eh = 0;
		else if (!strcmp(e[i], "EH1")) r->eh = 1;
		else if (strcmp(e[i], "512") && strcmp(e[i], "128")) die("bad opcode field", r->line);
	}
	if (!r->map) return 0; /* jkzd/jknzd rel8: no escape map; not in S2 */
	r->opc = -1; r->modrm = -1;
	for (int i = 1; i < n; i++) {
		if (strlen(f[i]) == 2 && isxdigit((unsigned char)f[i][0]) && isxdigit((unsigned char)f[i][1]) && r->opc < 0)
			r->opc = (int)strtol(f[i], 0, 16);
		else if (!strcmp(f[i], "/r")) r->modrm = 8;
		else if (f[i][0] == '/' && f[i][1] >= '0' && f[i][1] <= '7' && !f[i][2]) r->modrm = f[i][1] - '0';
		else if (!strcmp(f[i], "ib")) r->imm = 1;
		else if (!strcmp(f[i], "id")) return 0; /* rel32 branch: not in S2 */
		else if (!strcmp(f[i], "/vsib")) r->vsib = 1;
		else die("bad opcode token", r->line);
	}
	if (r->opc < 0) die("no opcode", r->line);
	if (r->modrm < 0) r->modrm = 8; /* every KNC vector opcode has a ModRM byte */
	ins = trim(ins);
	char *sp = strchr(ins, ' ');
	if (!sp) die("no operands", r->line);
	*sp = 0; snprintf(r->mnem, sizeof r->mnem, "%s", ins);
	char *o[8]; int no = split(sp + 1, ',', o, 8);
	for (int i = 0; i < no; i++) parse_opd(r, o[i]);
	return 1;
}

static void parse_tab(Tab *t, char *rowsstr) {
	char *c[16]; int n = split(rowsstr, ' ', c, 16);
	for (int i = 0; i < n; i++) {
		char *p[4]; if (split(c[i], ':', p, 4) != 3) die("bad table row", t->line);
		int lo, hi;
		if (!strcmp(p[0], "1xx")) { lo = 4; hi = 7; }
		else { lo = hi = (p[0][0] - '0') * 4 + (p[0][1] - '0') * 2 + (p[0][2] - '0'); }
		for (int s = lo; s <= hi; s++) {
			if (!strcmp(p[1], "reserved")) continue;
			t->valid[s] = 1;
			snprintf(t->tok[s], 16, "%s", strcmp(p[1], "-") ? p[1] : "");
			t->n[s] = strcmp(p[2], "-") ? atoi(p[2]) : 0;
		}
	}
}

static const Tab *find_tab(int block, const char *kind, const char *cls) {
	for (int i = 0; i < ntabs; i++)
		if (tabs[i].block == block && !strcmp(tabs[i].kind, kind) && !strcmp(tabs[i].cls, cls)) return &tabs[i];
	return 0;
}

/* Deco table interning. 000 is the default: printed as nothing; its
 * document spelling ({16to16}, {8to8}, {dcba}) is kept as an alias. */
static int intern(const Deco *d) {
	for (int i = 1; i < ndecos; i++) if (!memcmp(&decos[i], d, sizeof *d)) return i;
	if (ndecos == MAXD) die("too many tables", 0);
	decos[ndecos] = *d;
	return ndecos++;
}

static int deco_from_tab(const Tab *t) {
	Deco d; memset(&d, 0, sizeof d);
	for (int s = 0; s < 8; s++) {
		d.valid[s] = t->valid[s]; d.n[s] = t->n[s];
		if (s == 0 && t->valid[0] && t->tok[0][0] && strcmp(t->tok[0], "{rn}")) {
			snprintf(d.alias, 16, "%s", t->tok[0]); /* {16to16} {8to8} {dcba} */
			d.tok[0][0] = 0;
		} else snprintf(d.tok[s], 16, "%s", t->tok[s]);
	}
	return intern(&d);
}

static int deco_plain(int n) { /* only the default, disp8*N = n */
	Deco d; memset(&d, 0, sizeof d);
	d.valid[0] = 1; d.n[0] = n;
	return intern(&d);
}

static int deco_full_rounding(void) { /* Table 2.14, for the rounding overrides */
	static const char *rc[8] = {"{rn}", "{rd}", "{ru}", "{rz}", "{rn-sae}", "{rd-sae}", "{ru-sae}", "{rz-sae}"};
	Deco d; memset(&d, 0, sizeof d);
	for (int s = 0; s < 8; s++) { d.valid[s] = 1; snprintf(d.tok[s], 16, "%s", rc[s]); }
	return intern(&d);
}

/* one def */
typedef struct {
	char name[80], mnem[32];
	const Row *r;
	int form; /* 0 SrcReg 1 SrcMem 2 DestMem 3 MRMnr 4 MRMnm */
	int nops; Opd op[5]; int role[5];
	int mi[5];  /* MCInst index of each operand (mem = first of 5) */
	int mimask, misss, nmi;
	int memtab, reg0tab, reg1tab;
	int out;    /* operand index in outs, -1 none */
	int mayload, maystore;
} Def;
static Def defs[MAXI * 2]; static int ndefs;

static int is_mem(int k) { return k == K_MEM || k == K_VSIB || k == K_M8; }

static void mkname(Def *d) {
	char s[64]; int j = 0;
	j += snprintf(s + j, sizeof s - j, "KNC_");
	for (const char *p = d->mnem; *p; p++) s[j++] = (char)toupper((unsigned char)*p);
	s[j++] = '_';
	for (int i = 0; i < d->nops; i++) {
		switch (d->op[i].kind) {
		case K_VR: s[j++] = 'z'; break;
		case K_KR: s[j++] = 'k'; break;
		case K_GR32: s[j++] = 'r'; break;
		case K_GR64: s[j++] = 'q'; break;
		case K_MEM: case K_M8: s[j++] = 'm'; break;
		case K_VSIB: s[j++] = 'v'; break;
		case K_IMM: s[j++] = 'i'; break;
		}
	}
	s[j] = 0;
	snprintf(d->name, sizeof d->name, "%s%s", s, d->r->mvex && !strncmp(d->mnem, "vprefetch", 9) ? "_MVEX" : (d->r->mvex && !strncmp(d->mnem, "clevict", 7) ? "_MVEX" : ""));
}

static void build_def(const Row *r, int memform) {
	Def *d = &defs[ndefs++];
	memset(d, 0, sizeof *d);
	d->r = r; snprintf(d->mnem, sizeof d->mnem, "%s", r->mnem);
	d->nops = r->nops; memcpy(d->op, r->op, sizeof d->op);
	d->out = -1; d->mimask = -1; d->misss = -1;
	int rm = -1, mask = -1;
	for (int i = 0; i < d->nops; i++) {
		if (d->op[i].kind == K_RM) d->op[i].kind = memform ? K_MEM : K_VR;
		if (d->op[i].kind == K_VR) d->op[i].flags &= ~F_EH;
		if (d->op[i].flags & F_MASK) mask = i;
	}
	/* roles */
	for (int i = 0; i < d->nops; i++) d->role[i] = d->op[i].kind == K_IMM ? R_IMM : R_NONE;
	int regs[5] = {0}, nr = 0;
	for (int i = 0; i < d->nops; i++) if (d->op[i].kind != K_IMM) regs[nr++] = i;
	if (r->modrm == 8) {
		if (is_mem(d->op[regs[0]].kind)) { /* store / scatter */
			if (nr != 2) die("store shape", r->line);
			d->role[regs[0]] = R_RM; d->role[regs[1]] = R_REG; d->form = 2; rm = regs[0];
		} else {
			d->role[regs[0]] = R_REG;
			if (r->nds) { if (nr != 3) die("NDS shape", r->line); d->role[regs[1]] = R_VVVV; d->role[regs[2]] = R_RM; rm = regs[2]; }
			else { if (nr != 2) die("/r shape", r->line); d->role[regs[1]] = R_RM; rm = regs[1]; }
			d->form = is_mem(d->op[rm].kind) ? 1 : 0;
		}
	} else {
		if (r->ndd) { if (nr != 2) die("NDD shape", r->line); d->role[regs[0]] = R_VVVV; d->role[regs[1]] = R_RM; rm = regs[1]; }
		else { if (nr != 1) die("/digit shape", r->line); d->role[regs[0]] = R_RM; rm = regs[0]; }
		d->form = is_mem(d->op[rm].kind) ? 4 : 3;
	}
	d->mayload = is_mem(d->op[rm].kind) && d->form != 2;
	d->maystore = d->form == 2;
	/* gathers, scatters and their prefetches need a written mask (gas: "no
	 * default mask"), and so do the maskreq overrides */
	if (mask >= 0) {
		int req = 0;
		for (int i = 0; i < d->nops; i++) if (d->op[i].kind == K_VSIB) req = 1;
		for (int k = 0; k < nmaskreq; k++) if (!strcmp(maskreq_override[k], d->mnem)) req = 1;
		if (req) d->op[mask].flags |= F_MASKREQ;
	}
	/* MCInst order (X86MCCodeEmitter conventions):
	 *   SrcReg/SrcMem: reg, [mask], [vvvv], rm, [imm], [sss]
	 *   DestMem:       rm, [mask], reg, [imm], [sss]
	 *   MRMn:          [vvvv], [mask], rm, [imm], [sss] */
	int order[8], no = 0;
	int reg = -1, vvvv = -1, imm = -1;
	for (int i = 0; i < d->nops; i++) {
		if (d->role[i] == R_REG) reg = i;
		if (d->role[i] == R_VVVV) vvvv = i;
		if (d->role[i] == R_IMM) imm = i;
	}
	if (d->form <= 1) { order[no++] = reg; if (mask >= 0) order[no++] = 100; if (vvvv >= 0) order[no++] = vvvv; order[no++] = rm; }
	else if (d->form == 2) { order[no++] = rm; if (mask >= 0) order[no++] = 100; order[no++] = reg; }
	else { if (vvvv >= 0) order[no++] = vvvv; if (mask >= 0) order[no++] = 100; order[no++] = rm; }
	if (imm >= 0) order[no++] = imm;
	/* conversion tables */
	int conv = -1;
	for (int i = 0; i < d->nops; i++) if (d->op[i].flags & F_CONV) conv = i;
	if (r->mvex) {
		const char *cls = conv >= 0 ? d->op[conv].cls : "-";
		int memop = is_mem(d->op[rm].kind);
		if (memop) {
			const Tab *t = 0;
			if (conv >= 0) t = find_tab(r->block, d->form == 2 ? "down" : "up", cls);
			if (conv >= 0 && cls[0] == 'S') t = find_tab(r->block, "up", cls);
			if (!t && conv >= 0) { /* the scatter prefetches name Uf32 but list a Df32 table */
				char alt[8]; snprintf(alt, sizeof alt, "%c%s", d->form == 2 ? 'U' : 'D', cls + 1);
				t = find_tab(r->block, d->form == 2 ? "up" : "down", alt);
				if (t) fprintf(stderr, "knc-gen: %s uses the %s table of its section\n", d->mnem, alt);
			}
			if (t) d->memtab = deco_from_tab(t);
			else if (d->op[rm].kind == K_M8) d->memtab = deco_plain(64); /* vprefetch*, clevict*: disp8*64 */
			else if (conv < 0) d->memtab = deco_plain(64);              /* zmm/mt: no conversion, full vector */
			else { fprintf(stderr, "%s: no %s table for %s\n", d->mnem, d->form == 2 ? "down" : "up", cls); die("missing table", r->line); }
		} else {
			const Tab *t0 = find_tab(r->block, "reg0", cls), *t1 = find_tab(r->block, "reg1", cls);
			if (!t0 && conv >= 0 && cls[0] == 'S') {
				fprintf(stderr, "%s: no reg0 table for %s\n", d->mnem, cls); die("missing table", r->line);
			}
			d->reg0tab = t0 ? deco_from_tab(t0) : deco_plain(0);
			d->reg1tab = t1 ? deco_from_tab(t1) : 0;
			for (int k = 0; k < nrounding; k++)
				if (!strcmp(rounding_override[k], d->mnem)) d->reg1tab = deco_full_rounding();
			for (int k = 0; k < nnosae; k++)
				if (!strcmp(nosae_override[k], d->mnem)) d->reg1tab = 0;
			/* a load-op whose memory form reads half a vector (8 elements of
			 * 32 bits) takes the low half of a register, written as ymm */
			const Tab *tu = conv >= 0 ? find_tab(r->block, "up", cls) : 0;
			if (tu && tu->valid[0] && tu->n[0] == 32 && cls[2] == '3') d->op[conv].flags |= F_HALF;
		}
		int need_sss = (conv >= 0) || memop || d->reg1tab;
		if (d->op[rm].kind == K_M8) need_sss = 0;
		if (need_sss) order[no++] = 101;
	}
	int mi = 0;
	for (int i = 0; i < no; i++) {
		if (order[i] == 100) d->mimask = mi++;
		else if (order[i] == 101) d->misss = mi++;
		else { d->mi[order[i]] = mi; mi += is_mem(d->op[order[i]].kind) ? 5 : 1; }
	}
	d->nmi = mi;
	/* outs: the first MCInst operand when it is a register written by the
	 * instruction (not kortest, which only reads its two masks). */
	int first = order[0];
	if (first < 100 && !is_mem(d->op[first].kind) && d->role[first] != R_RM && strcmp(d->mnem, "kortest")) d->out = first;
	if (d->form == 3 && d->role[first] == R_VVVV) d->out = first;
	mkname(d);
}

static const char *tdop(const Opd *o) {
	switch (o->kind) {
	case K_VR: return "VR512";
	case K_KR: return "VK16";
	case K_GR32: return "GR32";
	case K_GR64: return "GR64";
	case K_MEM: return "i512mem";
	case K_M8: return "i8mem";
	case K_VSIB: return "vz32mem";
	case K_IMM: return "u8imm";
	}
	return "?";
}

/* One "let ... in def" per instruction: the flags differ per def. */
static void emit_td(FILE *f) {
	static const char *forms[] = {"MRMSrcReg", "MRMSrcMem", "MRMDestMem", "MRM%dr", "MRM%dm"};
	static const char *maps[] = {"", "TB", "T8", "TA"};
	static const char *pps[] = {"", "PD", "XS", "XD"};
	fprintf(f, "//===-- X86InstrKNC.td - Knights Corner vector instructions -*- tablegen -*-===//\n"
	           "//\n// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.\n"
	           "// See https://llvm.org/LICENSE.txt for license information.\n"
	           "// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception\n//\n"
	           "//===----------------------------------------------------------------------===//\n//\n"
	           "// Generated by Intel-Phi-LLVM tools/knc-gen.c from isa/knc-isa.txt, which is\n"
	           "// extracted from the Intel Xeon Phi Coprocessor Instruction Set Architecture\n"
	           "// Reference Manual (327364-001). Do not edit by hand.\n//\n"
	           "// The MVEX forms are EVEX-encoded defs with isMVEX set; the trailing\n"
	           "// mvex_sss operand holds MVEX.E << 3 | MVEX.SSS. Syntax, decorations and\n"
	           "// disp8*N come from X86KNCInfo.inc, so these defs are isCodeGenOnly and the\n"
	           "// generic matcher and decoder tables never see them.\n//\n"
	           "//===----------------------------------------------------------------------===//\n\n"
	           "def mvex_sss : Operand<i8> {\n  let OperandType = \"OPERAND_IMMEDIATE\";\n}\n\n"
	           "// Marks every Knights Corner def, so the TableGen backends that pair\n// instructions by encoding (EVEX compression, fold tables) skip them.\nclass KNCEnc;\n"
	           "class KNC_I<bits<8> o, Format f, dag outs, dag ins, string m>\n  : I<o, f, outs, ins, m, []>, KNCEnc;\n"
	           "class KNC_Ii8<bits<8> o, Format f, dag outs, dag ins, string m>\n  : Ii8<o, f, outs, ins, m, []>, KNCEnc;\n\n");
	for (int i = 0; i < ndefs; i++) {
		Def *d = &defs[i]; const Row *r = d->r;
		char form[16]; snprintf(form, sizeof form, forms[d->form], r->modrm);
		int hasimm = 0;
		for (int j = 0; j < d->nops; j++) if (d->op[j].kind == K_IMM) hasimm = 1;
		char outs[64] = "", ins[256] = ""; int k = 0;
		for (int pos = 0; pos < d->nmi; pos++) {
			char one[48] = "";
			int isout = 0;
			if (pos == d->mimask) snprintf(one, sizeof one, "VK16:$mask");
			else if (pos == d->misss) snprintf(one, sizeof one, "mvex_sss:$sss");
			else {
				int j;
				for (j = 0; j < d->nops; j++)
					if (d->mi[j] == pos) break;
				if (j == d->nops) continue; /* inside a memory operand */
				snprintf(one, sizeof one, "%s:$op%d", tdop(&d->op[j]), j);
				isout = j == d->out;
			}
			if (isout) snprintf(outs, sizeof outs, " %s", one);
			else k += snprintf(ins + k, sizeof ins - k, "%s%s", k ? ", " : "", one);
		}
		int fl = !strcmp(d->mnem, "kortest") || strstr(d->mnem, "cnt") != 0;
		int pf = !strncmp(d->mnem, "vprefetch", 9) || !strncmp(d->mnem, "clevict", 7) || strstr(d->mnem, "pf0") || strstr(d->mnem, "pf1") || !strcmp(d->mnem, "delay") || !strcmp(d->mnem, "spflt");
		fprintf(f, "let Predicates = [HasKNC], isCodeGenOnly = 1, hasSideEffects = %d, SchedRW = [WriteFAddZ]%s%s%s%s in\n",
		        pf, d->mayload ? ", mayLoad = 1" : "", d->maystore ? ", mayStore = 1" : "",
		        fl ? ", Defs = [EFLAGS]" : "", r->mvex ? ", isMVEX = 1" : "");
		fprintf(f, "def %s : KNC_%s<0x%02X, %s, (outs%s), (ins %s), \"%s\">, %s, %s%s%s%s%s%s%s;\n",
		        d->name, hasimm ? "Ii8" : "I", r->opc, form, outs, ins, d->mnem,
		        r->mvex ? "EVEX" : "VEX", maps[r->map], r->pp ? ", " : "", pps[r->pp],
		        d->mimask >= 0 ? ", EVEX_K" : "", (r->nds || r->ndd) ? ", VVVV" : "",
		        r->w == 1 ? ", REX_W" : "", r->w == 2 ? ", WIG" : "");
	}
}

static void cstr(FILE *f, const char *s) { fprintf(f, "\"%s\"", s); }

static void emit_inc(FILE *f) {
	fprintf(f, "// Generated by Intel-Phi-LLVM tools/knc-gen.c from isa/knc-isa.txt (Intel\n"
	           "// 327364-001). Do not edit by hand. Included by X86KNCInfo.cpp.\n\n");
	fprintf(f, "static const DecoTable DecoTables[] = {\n  {{\"\", \"\", \"\", \"\", \"\", \"\", \"\", \"\"}, {0, 0, 0, 0, 0, 0, 0, 0}, 0, \"\"},\n");
	for (int i = 1; i < ndecos; i++) {
		Deco *d = &decos[i]; int v = 0;
		for (int s = 0; s < 8; s++) v |= d->valid[s] << s;
		fprintf(f, "  {{");
		for (int s = 0; s < 8; s++) { cstr(f, d->tok[s]); if (s < 7) fprintf(f, ", "); }
		fprintf(f, "}, {");
		for (int s = 0; s < 8; s++) fprintf(f, "%d%s", d->n[s], s < 7 ? ", " : "");
		fprintf(f, "}, 0x%02x, ", v); cstr(f, d->alias); fprintf(f, "},\n");
	}
	fprintf(f, "};\n\nstatic const Insn Insns[] = {\n");
	/* sorted by mnemonic for the parser; the order inside a mnemonic is the
	 * matching order (register forms before memory forms, VEX before MVEX) */
	int idx[MAXI * 2];
	for (int i = 0; i < ndefs; i++) idx[i] = i;
	for (int i = 1; i < ndefs; i++) for (int j = i; j > 0; j--) {
		Def *a = &defs[idx[j - 1]], *b = &defs[idx[j]];
		int c = strcmp(a->mnem, b->mnem);
		if (c > 0 || (c == 0 && a->r->mvex > b->r->mvex)) { int t = idx[j]; idx[j] = idx[j - 1]; idx[j - 1] = t; }
		else break;
	}
	for (int ii = 0; ii < ndefs; ii++) {
		Def *d = &defs[idx[ii]]; const Row *r = d->r;
		fprintf(f, "  {X86::%s, \"%s\", %d, %d, %d, %d, 0x%02X, %d, %d, %d, %d, %d, %d, %d, %d, %d, {", d->name, d->mnem,
		        r->mvex, r->map, r->pp, r->w, r->opc, r->modrm, r->eh, d->mimask, d->misss, d->memtab, d->reg0tab, d->reg1tab,
		        d->form, d->nops);
		for (int j = 0; j < 5; j++) {
			if (j < d->nops) fprintf(f, "{%s, %s, %d, %d}", kname[d->op[j].kind], rname[d->role[j]], d->mi[j], d->op[j].flags);
			else fprintf(f, "{0, 0, 0, 0}");
			if (j < 4) fprintf(f, ", ");
		}
		fprintf(f, "}},\n");
	}
	fprintf(f, "};\n\nstatic const PseudoOp PseudoOps[] = {\n");
	for (int i = 0; i < npseudos; i++) fprintf(f, "  {\"%s\", \"%s\", %d},\n", pseudos[i].pseudo, pseudos[i].base, pseudos[i].imm < 0 ? 0xff : pseudos[i].imm);
	fprintf(f, "};\n");
}

/* A sweep of every def in K1OM AT&T syntax, for oracle/knc-sweep.sh: per
 * def a "# def NAME" line, then six lines rotating zmm 1 7 8 15 16 31 (and
 * k1 to k7, eight general registers, seven memory and four VSIB forms)
 * through the operand roles, then each decoration of the def's tables and
 * {eh} once. Intel's gas assembles the same file, so every encoding field
 * (R R' X B V' vvvv aaa, SIB, disp8*N) is compared in each role. */
static void emit_sweep(FILE *f) {
	static const int zmm[6] = {1, 7, 8, 15, 16, 31};
	static const char *g32[8] = {"%eax", "%ecx", "%ebx", "%esp", "%r8d", "%r9d", "%r13d", "%r15d"};
	static const char *g64[8] = {"%rax", "%rcx", "%rbx", "%rbp", "%r8", "%r9", "%r12", "%r15"};
	static const char *mem[7] = {"(%rax)", "0x40(%rsp)", "-0x40(%rbp)", "0x1000(%r12)", "(%r13)", "0x7f(%r8,%rbx,4)", "-0x80(%r15,%r9,8)"};
	static const char *vsib[4] = {"(%rax,%zmm0,4)", "0x40(%r13,%zmm16,8)", "-0x40(%rsp,%zmm31,1)", "(%r8,%zmm15,2)"};
	for (int i = 0; i < ndefs; i++) {
		Def *d = &defs[i];
		fprintf(f, "# def %s\n", d->name);
		int conv = -1;
		for (int j = 0; j < d->nops; j++) if (d->op[j].flags & F_CONV) conv = j;
		int tab = d->memtab ? d->memtab : d->reg0tab;
		/* lines: 6 register rotations, then one per decoration, {eh}, rounding */
		int nlines = 6, ndeco = 0, nround = 0;
		const char *deco[16], *round[8];
		if (conv >= 0 && tab)
			for (int s = 1; s < 8; s++)
				if (decos[tab].valid[s] && decos[tab].tok[s][0]) deco[ndeco++] = decos[tab].tok[s];
		if (d->reg1tab)
			for (int s = 0; s < 8; s++)
				if (decos[d->reg1tab].valid[s] && (s == 0 || strcmp(decos[d->reg1tab].tok[s], decos[d->reg1tab].tok[s - 1])))
					round[nround++] = decos[d->reg1tab].tok[s];
		int eh = d->r->mvex && d->r->eh == 2 && d->misss >= 0 && (d->form == 1 || d->form == 2 || d->form == 4 || conv >= 0);
		nlines += ndeco + nround + eh;
		for (int l = 0; l < nlines; l++) {
			int v = l < 6 ? l : 0;
			const char *dtok = "", *rtok = "";
			int use_eh = 0;
			if (l >= 6 && l < 6 + ndeco) dtok = deco[l - 6];
			else if (l >= 6 + ndeco && l < 6 + ndeco + nround) rtok = round[l - 6 - ndeco];
			else if (l >= 6) use_eh = 1;
			char ops[5][64];
			for (int j = 0; j < d->nops; j++) {
				const Opd *o = &d->op[j];
				int slot = (v + 2 * d->role[j]) % 6;
				char base[48];
				switch (o->kind) {
				case K_VR: snprintf(base, sizeof base, "%%%s%d", o->flags & F_HALF ? "ymm" : "zmm", zmm[slot]); break;
				case K_KR: snprintf(base, sizeof base, "%%k%d", (v + d->role[j]) % 7 + 1); break;
				case K_GR32: snprintf(base, sizeof base, "%s", g32[(v + 3 * d->role[j]) % 8]); break;
				case K_GR64: snprintf(base, sizeof base, "%s", g64[(v + 3 * d->role[j]) % 8]); break;
				case K_MEM: case K_M8: snprintf(base, sizeof base, "%s", mem[(v + l) % 7]); break;
				case K_VSIB: snprintf(base, sizeof base, "%s", vsib[(v + l) % 4]); break;
				default: snprintf(base, sizeof base, "$3"); break;
				}
				int k = snprintf(ops[j], sizeof ops[j], "%s", base);
				if (j == conv && dtok[0]) k += snprintf(ops[j] + k, sizeof ops[j] - k, "%s", dtok);
				if (use_eh) { /* on the memory operand, else on the register source */
					int memform = d->form == 1 || d->form == 2 || d->form == 4;
					if (memform ? (o->kind == K_MEM || o->kind == K_VSIB) : j == conv)
						k += snprintf(ops[j] + k, sizeof ops[j] - k, "{eh}");
				}
				if ((o->flags & F_MASK) && d->mimask >= 0 && v % 3 != 2) snprintf(ops[j] + k, sizeof ops[j] - k, "{%%k%d}", v % 7 + 1);
			}
			fprintf(f, "\t%s\t", d->mnem);
			/* gas takes static rounding after the immediate where the rounding is
			 * full (vcvtfxpnt(u)dq2ps), before it where it is SAE only */
			int late = rtok[0] && d->reg1tab && decos[d->reg1tab].valid[0] && d->op[d->nops - 1].kind == K_IMM;
			if (late) { fprintf(f, "%s,%s,", ops[d->nops - 1], rtok); for (int j = d->nops - 2; j >= 0; j--) fprintf(f, "%s%s", ops[j], j ? "," : "\n"); continue; }
			if (rtok[0]) fprintf(f, "%s,", rtok);
			for (int j = d->nops - 1; j >= 0; j--) fprintf(f, "%s%s", ops[j], j ? "," : "\n");
		}
	}
}

static void emit_listing(FILE *f) {
	for (int i = 0; i < ndefs; i++) {
		Def *d = &defs[i]; const Row *r = d->r;
		fprintf(f, "%-26s %-16s %s map%d pp%d W%d op%02X /%c form%d mi%d mask%d sss%d tabs %d/%d/%d |", d->name, d->mnem,
		        r->mvex ? "MVEX" : "VEX ", r->map, r->pp, r->w, r->opc, r->modrm == 8 ? 'r' : '0' + r->modrm, d->form, d->nmi,
		        d->mimask, d->misss, d->memtab, d->reg0tab, d->reg1tab);
		for (int j = 0; j < d->nops; j++)
			fprintf(f, " %s:%s@%d%s%s%s", kname[d->op[j].kind], rname[d->role[j]], d->mi[j],
			        d->op[j].flags & F_MASK ? "{k}" : "", d->op[j].flags & F_CONV ? "{c}" : "", d->op[j].flags & F_EH ? "{eh}" : "");
		fprintf(f, "\n");
	}
	for (int i = 1; i < ndecos; i++) {
		fprintf(f, "deco %d alias '%s':", i, decos[i].alias);
		for (int s = 0; s < 8; s++) if (decos[i].valid[s]) fprintf(f, " %d=%s/%d", s, decos[i].tok[s][0] ? decos[i].tok[s] : "-", decos[i].n[s]);
		fprintf(f, "\n");
	}
}

/* ---- Code generation (S3): knc-gen --codegen ----
 *
 * From the same defs: the llvm.x86.knc.* intrinsics, the pseudos that code
 * generation selects (X86InstrKNCCodeGen.td explains why they are not the
 * MC defs), their patterns, the map from each pseudo's operands to its MC
 * def's, the clang builtins and the checks of their immediates. The data
 * flow the opcode tables do not state (a destination that is also read, a
 * mask that is also written) comes from isa/knc-ops.txt and the untied,
 * tied and blend overrides. */

typedef struct { char mnem[32]; int rd; char writes[64]; } OpsRec;
static OpsRec opsrec[256]; static int nopsrec;
static char tied_ov[32][32], untied_ov[32][32], blend_ov[32][32];
static int ntied, nuntied, nblend;

static int in_list(char l[][32], int n, const char *m) {
	for (int i = 0; i < n; i++) if (!strcmp(l[i], m)) return 1;
	return 0;
}

static const OpsRec *find_ops(const char *m) {
	for (int i = 0; i < nopsrec; i++) if (!strcmp(opsrec[i].mnem, m)) return &opsrec[i];
	return 0;
}

enum { T_NONE, T_F32, T_I32, T_F64, T_I64 };
static const char *mvt[] = {"", "v16f32", "v16i32", "v8f64", "v8i64"};
static const char *llty[] = {"", "llvm_v16f32_ty", "llvm_v16i32_ty", "llvm_v8f64_ty", "llvm_v8i64_ty"};
static const char *cty[] = {"", "_Vector<16, float>", "_Vector<16, int>", "_Vector<8, double>", "_Vector<8, long long int>"};

static int vt_of_cls(const char *c) {
	int f = c[1] == 'f', w64 = c[2] == '6';
	return f ? (w64 ? T_F64 : T_F32) : (w64 ? T_I64 : T_I32);
}

/* the element type of the destination, from the mnemonic: the part after a
 * '2' for a conversion, else f32x4 i64x4 ..., else the ps pd d q suffix */
static int vt_of_mnem(const char *m) {
	const char *t = strrchr(m, '2');
	if (t && (!strncmp(m, "vcvt", 4))) {
		t++;
		if (!strcmp(t, "ps")) return T_F32;
		if (!strcmp(t, "pd")) return T_F64;
		return T_I32; /* dq udq */
	}
	if (strstr(m, "f32")) return T_F32;
	if (strstr(m, "f64")) return T_F64;
	if (strstr(m, "i32") || strstr(m, "dqa32")) return T_I32;
	if (strstr(m, "i64") || strstr(m, "dqa64")) return T_I64;
	size_t n = strlen(m);
	/* vloadunpack* and vpackstore* end in the element type (ps pd d q); the
	   other vp* integer forms in d or q (vpcmpd is not a pd) */
	if (!strncmp(m, "vloadunpack", 11) || !strncmp(m, "vpackstore", 10)) {
		if (n > 2 && !strcmp(m + n - 2, "ps")) return T_F32;
		if (n > 2 && !strcmp(m + n - 2, "pd")) return T_F64;
		return m[n - 1] == 'q' ? T_I64 : T_I32;
	}
	if (!strncmp(m, "vp", 2) && (m[n - 1] == 'd' || m[n - 1] == 'q')) return m[n - 1] == 'q' ? T_I64 : T_I32;
	if (n > 2 && !strcmp(m + n - 2, "ps")) return T_F32;
	if (n > 2 && !strcmp(m + n - 2, "pd")) return T_F64;
	if (n > 2 && !strcmp(m + n - 2, "ss")) return T_F32; /* vbroadcastss */
	if (n > 2 && !strcmp(m + n - 2, "sd")) return T_F64;
	if (m[n - 1] == 'q') return T_I64;
	return T_I32;
}

typedef struct {
	int gen; const char *why;
	int tied, blend, kout;
	int vt[5], outvt;
	int pf, fl;
	char lname[96]; /* knc_vaddps_zzz */
} CG;
static CG cgs[MAXI * 2];

/* A pseudo operand: what fills it and where it lands in the MC def. */
enum { PO_OP, PO_TIED, PO_MASK, PO_SSS };
typedef struct { int what, j, mcpos; } PO;

static void cg_classify(int i) {
	Def *d = &defs[i]; CG *c = &cgs[i];
	memset(c, 0, sizeof *c);
	size_t nl = strlen(d->name);
	if (nl >= sizeof c->lname) die("def name too long", d->r->line);
	memcpy(c->lname, d->name, nl + 1);
	for (char *p = c->lname; *p; p++) *p = (char)tolower((unsigned char)*p);
	c->gen = 1;
	for (int j = 0; j < d->nops; j++)
		if (d->op[j].kind == K_VSIB) { c->gen = 0; c->why = "VSIB: a gather or scatter repeats until its mask is clear (not yet)"; return; }
	if (!strcmp(d->mnem, "kortest")) { c->gen = 0; c->why = "sets flags: hand-written (KNC_KORTEST)"; return; }
	const OpsRec *o = find_ops(d->mnem);
	if (!o && !(c->pf = !strncmp(d->mnem, "vprefetch", 9) || !strncmp(d->mnem, "clevict", 7))) { c->gen = 0; c->why = "no Operation section in knc-ops.txt"; return; }
	/* a mask register written that is not the result: the result and the
	 * mask together need custom selection (not yet) */
	char w[80]; snprintf(w, sizeof w, " %s ", o ? o->writes : "");
	for (int k = 1; k < 8 && d->r->mvex; k++) {
		char kn[8]; snprintf(kn, sizeof kn, " k%d ", k);
		if (!strstr(w, kn)) continue;
		int isout = d->out >= 0 && d->op[d->out].kind == K_KR;
		if (!isout) { c->gen = 0; c->why = "writes a mask beside its result (not yet)"; return; }
	}
	c->tied = o ? o->rd : 0;
	if (in_list(untied_ov, nuntied, d->mnem)) c->tied = 0;
	if (in_list(tied_ov, ntied, d->mnem)) c->tied = 1;
	if (d->out < 0 || (d->op[d->out].kind != K_VR && d->op[d->out].kind != K_KR && d->op[d->out].kind != K_GR32 && d->op[d->out].kind != K_GR64)) c->tied = 0;
	c->blend = in_list(blend_ov, nblend, d->mnem);
	int dvt = vt_of_mnem(d->mnem);
	for (int j = 0; j < d->nops; j++) {
		if (d->op[j].kind != K_VR) continue;
		if (d->op[j].flags & F_CONV) c->vt[j] = vt_of_cls(d->op[j].cls);
		else c->vt[j] = dvt;
	}
	/* a conversion's destination type is the mnemonic's, its source the class */
	if (d->out >= 0 && d->op[d->out].kind == K_VR) c->vt[d->out] = dvt;
	c->outvt = d->out >= 0 && d->op[d->out].kind == K_VR ? c->vt[d->out] : T_NONE;
	c->fl = !strcmp(d->mnem, "kortest") || strstr(d->mnem, "cnt") != 0;
	c->pf = !strncmp(d->mnem, "vprefetch", 9) || !strncmp(d->mnem, "clevict", 7) || !strcmp(d->mnem, "delay") || !strcmp(d->mnem, "spflt");
}

static int is_vr_out(const Def *d) { return d->out >= 0 && d->op[d->out].kind == K_VR; }

/* 8 elements: an operand of f64 or i64 */
static int cg_lanes8(const Def *d, const CG *c) {
	for (int j = 0; j < d->nops; j++) if (d->op[j].kind == K_VR && (c->vt[j] == T_F64 || c->vt[j] == T_I64)) return 1;
	return 0;
}

/* The pseudo's operands for a variant: 'U' unmasked (k0), 'M' masked, 'P'
 * a def without a mask. Returns the count; *ntie = index of the tied input
 * or -1. */
static int cg_pseudo_ops(const Def *d, const CG *c, int var, PO *po, int *tie) {
	int n = 0; *tie = -1;
	int pos0 = 0;
	if (d->out >= 0) { po[n++] = (PO){PO_OP, d->out, 0}; pos0 = 1; }
	int pass = var == 'M' && is_vr_out(d) && !c->blend;
	if (c->tied || pass) { *tie = n; po[n++] = (PO){PO_TIED, d->out, -1}; }
	for (int pos = pos0; pos < d->nmi; pos++) {
		if (pos == d->mimask) { if (var == 'M') po[n++] = (PO){PO_MASK, -1, pos}; continue; }
		if (pos == d->misss) { po[n++] = (PO){PO_SSS, -1, pos}; continue; }
		int j;
		for (j = 0; j < d->nops; j++) if (d->mi[j] == pos) break;
		if (j == d->nops) continue; /* inside a memory operand */
		po[n++] = (PO){PO_OP, j, pos};
	}
	return n;
}

static int po_width(const Def *d, const PO *p) {
	return p->what == PO_OP && is_mem(d->op[p->j].kind) ? 5 : 1;
}

static const char *cg_tdop(const Def *d, const PO *p) {
	if (p->what == PO_MASK) return "VK16WM";
	if (p->what == PO_SSS) return "i32u8imm"; /* the intrinsic's immediates are i32 */
	const Opd *o = &d->op[p->j];
	if (o->kind == K_MEM) return "i512mem";
	if (o->kind == K_IMM) return "i32u8imm";
	return tdop(o);
}

static const char *opname(const PO *p, char *buf, size_t n) {
	switch (p->what) {
	case PO_TIED: snprintf(buf, n, "src"); break;
	case PO_MASK: snprintf(buf, n, "mask"); break;
	case PO_SSS: snprintf(buf, n, "sss"); break;
	default: snprintf(buf, n, "op%d", p->j); break;
	}
	return buf;
}

static void kconv(FILE *f, const char *v, const char *rc) {
	fprintf(f, "(COPY_TO_REGCLASS (i32 (INSERT_SUBREG (i32 (IMPLICIT_DEF)), GR16:$%s, sub_16bit)), %s)", v, rc);
}

/* The IR parameters, in order: [pass-through or tied destination], [mask],
 * the other operands in Intel order, [sss]. */
typedef struct { int what, j; } IP;
static int cg_iparams(const Def *d, const CG *c, IP *ip) {
	int n = 0;
	if (c->tied || (is_vr_out(d) && d->mimask >= 0 && !c->blend)) ip[n++] = (IP){PO_TIED, d->out};
	if (d->mimask >= 0) ip[n++] = (IP){PO_MASK, -1};
	for (int j = 0; j < d->nops; j++) if (j != d->out) ip[n++] = (IP){PO_OP, j};
	if (d->misss >= 0) ip[n++] = (IP){PO_SSS, -1};
	return n;
}

static const char *ir_ty(const Def *d, const CG *c, const IP *p) {
	if (p->what == PO_MASK) return "llvm_i16_ty";
	if (p->what == PO_SSS) return "llvm_i32_ty";
	const Opd *o = &d->op[p->j];
	switch (o->kind) {
	case K_VR: return llty[c->vt[p->j]];
	case K_KR: return "llvm_i16_ty";
	case K_GR32: return "llvm_i32_ty";
	case K_GR64: return "llvm_i64_ty";
	case K_IMM: return "llvm_i32_ty";
	default: return "llvm_ptr_ty";
	}
}

static const char *c_ty(const Def *d, const CG *c, const IP *p) {
	if (p->what == PO_MASK) return "unsigned short";
	if (p->what == PO_SSS) return "_Constant int";
	const Opd *o = &d->op[p->j];
	switch (o->kind) {
	case K_VR: return cty[c->vt[p->j]];
	case K_KR: return "unsigned short";
	case K_GR32: return "unsigned int";
	case K_GR64: return "unsigned long long int";
	case K_IMM: return "_Constant int";
	default: return d->maystore ? "void *" : "void const *";
	}
}

static const char *ret_ir(const Def *d, const CG *c) {
	if (d->out < 0) return "";
	switch (d->op[d->out].kind) {
	case K_VR: return llty[c->outvt];
	case K_KR: return "llvm_i16_ty";
	case K_GR32: return "llvm_i32_ty";
	case K_GR64: return "llvm_i64_ty";
	}
	return "";
}

static const char *ret_c(const Def *d, const CG *c) {
	if (d->out < 0) return "void";
	switch (d->op[d->out].kind) {
	case K_VR: return cty[c->outvt];
	case K_KR: return "unsigned short";
	case K_GR32: return "unsigned int";
	case K_GR64: return "unsigned long long int";
	}
	return "void";
}

static const char *banner =
	"//\n// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.\n"
	"// See https://llvm.org/LICENSE.txt for license information.\n"
	"// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception\n//\n"
	"//===----------------------------------------------------------------------===//\n//\n"
	"// Generated by Intel-Phi-LLVM tools/knc-gen.c (--codegen) from isa/knc-isa.txt,\n"
	"// isa/knc-ops.txt and isa/knc-overrides.txt, which come from the Intel Xeon Phi\n"
	"// Coprocessor Instruction Set Architecture Reference Manual (327364-001).\n"
	"// Do not edit by hand.\n//\n"
	"//===----------------------------------------------------------------------===//\n";

static void emit_intrinsics(FILE *f) {
	fprintf(f, "//===-- IntrinsicsX86KNC.td - Knights Corner intrinsics ---*- tablegen -*-===//\n%s\n"
	           "// One intrinsic per Knights Corner instruction form: llvm.x86.knc.<form>.\n"
	           "// Parameters: [pass-through, or the destination when the instruction reads\n"
	           "// it], [i16 write mask, -1 = none], the operands in Intel order, [the\n"
	           "// MVEX.E << 3 | MVEX.SSS decoration].\n\n"
	           "let TargetPrefix = \"x86\" in {\n", banner);
	for (int i = 0; i < ndefs; i++) {
		Def *d = &defs[i]; CG *c = &cgs[i];
		if (!c->gen) continue;
		IP ip[8]; int n = cg_iparams(d, c, ip);
		fprintf(f, "def int_x86_%s : ClangBuiltin<\"__builtin_ia32_%s\">,\n    DefaultAttrsIntrinsic<[%s], [", c->lname, c->lname, ret_ir(d, c));
		for (int k = 0; k < n; k++) fprintf(f, "%s%s", k ? ", " : "", ir_ty(d, c, &ip[k]));
		fprintf(f, "],\n    [");

		if (c->pf) fprintf(f, "IntrHasSideEffects");
		else if (d->maystore) fprintf(f, "IntrWriteMem, IntrArgMemOnly");
		else if (d->mayload) fprintf(f, "IntrReadMem, IntrArgMemOnly");
		else fprintf(f, "IntrNoMem");
		for (int k = 0; k < n; k++)
			if (ip[k].what == PO_SSS || (ip[k].what == PO_OP && d->op[ip[k].j].kind == K_IMM)) fprintf(f, ", ImmArg<ArgIndex<%d>>", k);
		/* an 8-element compare writes k[15:8] = 0 (327364, VCMPPD): its i16 result
		   is below 256, which lets a __mmask8 round trip fold away */
		if (d->out >= 0 && d->op[d->out].kind == K_KR && cg_lanes8(d, c)) fprintf(f, ", Range<RetIndex, 0, 256>");
		/* a memory form: the selected instruction keeps the memory operand that
		   getTgtMemIntrinsic describes, so alias analysis can order it */
		fprintf(f, "]%s>;\n", !c->pf && (d->mayload || d->maystore) ? ", \"\", [SDNPMemOperand]" : "");
	}
	fprintf(f, "}\n");
}

/* The scheduling class of a pseudo, priced by the knc model
   (X86ScheduleKNC.td): stores, loads and load-ops, conversions,
   shuffles, the other vector operations, and the mask and scalar ones. */
static const char *cg_sched(const Def *d) {
	const char *m = d->mnem;
	if (d->maystore) return "WriteFStoreY";
	if (!d->r->mvex) return "WriteALU";
	if (d->mayload) return "WriteFAddZ.Folded";
	if (!strncmp(m, "vcvt", 4) || !strncmp(m, "vrndfxpnt", 9)) return "WriteCvtPS2IZ";
	if (!strncmp(m, "vperm", 5) || !strcmp(m, "vpshufd") || !strcmp(m, "valignd")) return "WriteFShuffleZ";
	return "WriteFAddZ";
}

static void emit_variant(FILE *f, FILE *map, const Def *d, const CG *c, int var) {
	PO po[12]; int tie;
	int n = cg_pseudo_ops(d, c, var, po, &tie);
	char pname[96]; snprintf(pname, sizeof pname, "%s_%c", d->name, var);
	/* the def */
	char outs[64] = "", ins[384] = ""; int k = 0; char nb[16];
	int first_in = d->out >= 0 ? 1 : 0;
	if (d->out >= 0) snprintf(outs, sizeof outs, "%s:$dst", cg_tdop(d, &po[0]));
	for (int p = first_in; p < n; p++) {
		const char *ty = po[p].what == PO_TIED ? (d->op[d->out].kind == K_KR ? "VK16" : tdop(&d->op[d->out])) : cg_tdop(d, &po[p]);
		k += snprintf(ins + k, sizeof ins - k, "%s%s:$%s", k ? ", " : "", ty, opname(&po[p], nb, sizeof nb));
	}
	fprintf(f, "let Predicates = [HasKNC], isPseudo = 1, isCodeGenOnly = 1, hasSideEffects = %d, SchedRW = [%s]%s%s%s%s in\n",
	        c->pf, cg_sched(d), d->mayload ? ", mayLoad = 1" : "", d->maystore ? ", mayStore = 1" : "", c->fl ? ", Defs = [EFLAGS]" : "",
	        tie >= 0 ? ", Constraints = \"$src = $dst\"" : "");
	fprintf(f, "def %s : KNC_P<(outs%s%s), (ins %s), []>;\n", pname, outs[0] ? " " : "", outs, ins);
	/* the pattern */
	IP ip[8]; int ni = cg_iparams(d, c, ip);
	int kout = d->out >= 0 && d->op[d->out].kind == K_KR;
	fprintf(f, "def : Pat<(");
	if (d->out >= 0) fprintf(f, "%s (", kout ? "i16" : d->op[d->out].kind == K_VR ? mvt[c->outvt] : d->op[d->out].kind == K_GR32 ? "i32" : "i64");
	fprintf(f, "int_x86_%s", c->lname);
	for (int q = 0; q < ni; q++) {
		fprintf(f, "%s ", q ? "," : "");
		const IP *p = &ip[q];
		if (p->what == PO_TIED) {
			/* unmasked: the header passes undef, which this form does not read */
			if (d->op[p->j].kind == K_VR) fprintf(f, var == 'U' && !c->tied ? "(%s undef)" : "(%s VR512:$src)", mvt[c->outvt]);
			else if (d->op[p->j].kind == K_KR) fprintf(f, "GR16:$src");
			else fprintf(f, "%s:$src", tdop(&d->op[p->j]));
		} else if (p->what == PO_MASK) fprintf(f, var == 'U' ? "(i16 -1)" : "GR16:$mask");
		else if (p->what == PO_SSS) fprintf(f, "timm:$sss");
		else {
			const Opd *o = &d->op[p->j];
			switch (o->kind) {
			case K_VR: fprintf(f, "(%s VR512:$op%d)", mvt[c->vt[p->j]], p->j); break;
			case K_KR: fprintf(f, "GR16:$op%d", p->j); break;
			case K_GR32: fprintf(f, "GR32:$op%d", p->j); break;
			case K_GR64: fprintf(f, "GR64:$op%d", p->j); break;
			case K_IMM: fprintf(f, "timm:$op%d", p->j); break;
			default: fprintf(f, "addr:$op%d", p->j); break;
			}
		}
	}
	fprintf(f, "%s),\n          ", d->out >= 0 ? ")" : "");
	if (kout) fprintf(f, "(EXTRACT_SUBREG (i32 (COPY_TO_REGCLASS ");
	fprintf(f, "(%s", pname);
	for (int p = first_in; p < n; p++) {
		fprintf(f, "%s ", p > first_in ? "," : "");
		const PO *x = &po[p];
		if (x->what == PO_TIED) {
			if (d->op[d->out].kind == K_KR) { kconv(f, "src", "VK16"); }
			else if (d->op[d->out].kind == K_VR) fprintf(f, "VR512:$src");
			else fprintf(f, "%s:$src", tdop(&d->op[d->out]));
		} else if (x->what == PO_MASK) { kconv(f, "mask", "VK16WM"); }
		else if (x->what == PO_SSS) fprintf(f, "timm:$sss");
		else {
			const Opd *o = &d->op[x->j];
			char v[8]; snprintf(v, sizeof v, "op%d", x->j);
			switch (o->kind) {
			case K_VR: fprintf(f, "VR512:$%s", v); break;
			case K_KR: kconv(f, v, "VK16"); break;
			case K_GR32: fprintf(f, "GR32:$%s", v); break;
			case K_GR64: fprintf(f, "GR64:$%s", v); break;
			case K_IMM: fprintf(f, "timm:$%s", v); break;
			default: fprintf(f, "addr:$%s", v); break;
			}
		}
	}
	fprintf(f, ")%s>;\n", kout ? ", GR32)), sub_16bit)" : "");
	/* the operand map: MC position -> pseudo machine operand, -1 = k0 */
	int at[16]; for (int p = 0; p < 16; p++) at[p] = -9;
	int mo = 0;
	for (int p = 0; p < n; p++) {
		int w = po_width(d, &po[p]);
		if (po[p].mcpos >= 0) for (int q = 0; q < w; q++) at[po[p].mcpos + q] = mo + q;
		mo += w;
	}
	if (var == 'U') at[d->mimask] = -1;
	fprintf(map, "    {X86::%s, X86::%s, %d, {", pname, d->name, d->nmi);
	for (int p = 0; p < d->nmi; p++) {
		if (at[p] == -9) { fprintf(stderr, "knc-gen: %s: MC operand %d unmapped\n", pname, p); exit(1); }
		fprintf(map, "%d%s", at[p], p + 1 < d->nmi ? ", " : "");
	}
	fprintf(map, "}},\n");
}

/* A register form whose rm operand is an aligned 512-bit load, with no
 * decoration: the memory form of the same row, with none either (one
 * instruction, as the hand-written kernels do it). */
static void emit_fold(FILE *f, const Def *rd, const CG *rc, const Def *md, const CG *mc, int var) {
	int j = -1;
	for (int k = 0; k < rd->nops; k++) if (rd->op[k].kind == K_VR && md->op[k].kind == K_MEM) j = k;
	if (j < 0) return;
	IP ip[8]; int ni = cg_iparams(rd, rc, ip);
	int kout = rd->out >= 0 && rd->op[rd->out].kind == K_KR;
	fprintf(f, "def : Pat<(");
	if (rd->out >= 0) fprintf(f, "%s (", kout ? "i16" : rd->op[rd->out].kind == K_VR ? mvt[rc->outvt] : rd->op[rd->out].kind == K_GR32 ? "i32" : "i64");
	fprintf(f, "int_x86_%s", rc->lname);
	for (int q = 0; q < ni; q++) {
		const IP *p = &ip[q];
		fprintf(f, "%s ", q ? "," : "");
		if (p->what == PO_TIED) {
			if (rd->op[p->j].kind == K_VR) fprintf(f, var == 'U' && !rc->tied ? "(%s undef)" : "(%s VR512:$src)", mvt[rc->outvt]);
			else if (rd->op[p->j].kind == K_KR) fprintf(f, "GR16:$src");
			else fprintf(f, "%s:$src", tdop(&rd->op[p->j]));
		} else if (p->what == PO_MASK) fprintf(f, var == 'U' ? "(i16 -1)" : "GR16:$mask");
		else if (p->what == PO_SSS) fprintf(f, "knc_sss0");
		else {
			const Opd *o = &rd->op[p->j];
			if (p->j == j) { fprintf(f, "(%s (alignedload512 addr:$op%d))", mvt[rc->vt[p->j]], p->j); continue; }
			switch (o->kind) {
			case K_VR: fprintf(f, "(%s VR512:$op%d)", mvt[rc->vt[p->j]], p->j); break;
			case K_KR: fprintf(f, "GR16:$op%d", p->j); break;
			case K_GR32: fprintf(f, "GR32:$op%d", p->j); break;
			case K_GR64: fprintf(f, "GR64:$op%d", p->j); break;
			case K_IMM: fprintf(f, "timm:$op%d", p->j); break;
			default: fprintf(f, "addr:$op%d", p->j); break;
			}
		}
	}
	fprintf(f, "%s),\n          ", rd->out >= 0 ? ")" : "");
	PO po[12]; int tie;
	int n = cg_pseudo_ops(md, mc, var, po, &tie);
	int first_in = md->out >= 0 ? 1 : 0;
	if (kout) fprintf(f, "(EXTRACT_SUBREG (i32 (COPY_TO_REGCLASS ");
	fprintf(f, "(%s_%c", md->name, var);
	for (int p = first_in; p < n; p++) {
		const PO *x = &po[p];
		fprintf(f, "%s ", p > first_in ? "," : "");
		if (x->what == PO_TIED) {
			if (md->op[md->out].kind == K_KR) kconv(f, "src", "VK16");
			else if (md->op[md->out].kind == K_VR) fprintf(f, "VR512:$src");
			else fprintf(f, "%s:$src", tdop(&md->op[md->out]));
		} else if (x->what == PO_MASK) kconv(f, "mask", "VK16WM");
		else if (x->what == PO_SSS) fprintf(f, "0");
		else {
			const Opd *o = &md->op[x->j];
			char v[8]; snprintf(v, sizeof v, "op%d", x->j);
			switch (o->kind) {
			case K_VR: fprintf(f, "VR512:$%s", v); break;
			case K_KR: kconv(f, v, "VK16"); break;
			case K_GR32: fprintf(f, "GR32:$%s", v); break;
			case K_GR64: fprintf(f, "GR64:$%s", v); break;
			case K_IMM: fprintf(f, "timm:$%s", v); break;
			default: fprintf(f, "addr:$%s", v); break;
			}
		}
	}
	fprintf(f, ")%s>;\n", kout ? ", GR32)), sub_16bit)" : "");
}

static void emit_pseudos(FILE *f, FILE *map) {
	fprintf(f, "//===-- X86InstrKNCIntrin.td - Knights Corner intrinsic selection -*- tablegen -*-===//\n%s\n"
	           "// Per instruction form: the pseudo for each variant (_U unmasked, _M\n"
	           "// merge-masked or masked, _P without a mask) and the pattern from its\n"
	           "// llvm.x86.knc.* intrinsic. A destination that the instruction also reads,\n"
	           "// and a merge-masked pass-through, are tied to the result. X86KNCPseudos.inc\n"
	           "// maps each pseudo back to its MC def (X86MCInstLower.cpp).\n\n", banner);
	fprintf(map, "// Generated by Intel-Phi-LLVM tools/knc-gen.c (--codegen). Do not edit by\n"
	             "// hand. Included by X86MCInstLower.cpp: pseudo, MC def, MC operand count,\n"
	             "// then for each MC operand the pseudo operand it comes from (-1 = k0).\n");
	for (int i = 0; i < ndefs; i++) {
		Def *d = &defs[i]; CG *c = &cgs[i];
		if (!c->gen) continue;
		fprintf(f, "// %s\n", d->name);
		if (d->mimask < 0) emit_variant(f, map, d, c, 'P');
		else {
			int req = 0;
			for (int j = 0; j < d->nops; j++) if (d->op[j].flags & F_MASKREQ) req = 1;
			if (!req) emit_variant(f, map, d, c, 'U');
			emit_variant(f, map, d, c, 'M');
		}
	}
	fprintf(f, "\n// Loads folded into the memory form (no decoration on either side).\n");
	for (int i = 0; i + 1 < ndefs; i++) {
		Def *rd = &defs[i], *md = &defs[i + 1];
		if (rd->r != md->r || !cgs[i].gen || !cgs[i + 1].gen || (rd->form != 0 && rd->form != 3)) continue;
		if (rd->mimask < 0) { emit_fold(f, rd, &cgs[i], md, &cgs[i + 1], 'P'); continue; }
		int req = 0;
		for (int j = 0; j < rd->nops; j++) if (rd->op[j].flags & F_MASKREQ) req = 1;
		if (!req) emit_fold(f, rd, &cgs[i], md, &cgs[i + 1], 'U');
		emit_fold(f, rd, &cgs[i], md, &cgs[i + 1], 'M');
	}
}

/* the MVEX.E/SSS values an instruction form accepts, as a 16-bit set */
static int sss_allowed(const Def *d) {
	int a = 0, memop = 0;
	for (int j = 0; j < d->nops; j++) if (is_mem(d->op[j].kind)) memop = j + 1;
	if (memop) {
		const Deco *t = &decos[d->memtab];
		for (int s = 0; s < 8; s++) if (t->valid[s]) a |= 1 << s;
		if (d->op[memop - 1].flags & F_EH) a |= a << 8; /* {eh}: the no-temporal hint */
	} else {
		if (d->reg0tab) for (int s = 0; s < 8; s++) if (decos[d->reg0tab].valid[s]) a |= 1 << s;
		if (d->reg1tab) for (int s = 0; s < 8; s++) if (decos[d->reg1tab].valid[s]) a |= 1 << (8 + s);
	}
	return a;
}

static void emit_builtins(FILE *f, FILE *sema) {
	fprintf(f, "//===--- BuiltinsX86KNC.td - Knights Corner builtins ---*- tablegen -*-===//\n%s\n"
	           "// __builtin_ia32_knc_<form>, one per llvm.x86.knc.<form> intrinsic, which\n"
	           "// clang reaches through ClangBuiltin.\n\n", banner);
	fprintf(sema, "// Generated by Intel-Phi-LLVM tools/knc-gen.c (--codegen). Do not edit by\n"
	              "// hand. Included by SemaX86.cpp: builtin, argument, kind (0 imm8, 1 the\n"
	              "// MVEX.E/SSS decoration), the decoration values the form accepts.\n");
	for (int i = 0; i < ndefs; i++) {
		Def *d = &defs[i]; CG *c = &cgs[i];
		if (!c->gen) continue;
		IP ip[8]; int n = cg_iparams(d, c, ip);
		int vec = c->outvt != T_NONE;
		for (int k = 0; k < n; k++) if (ip[k].what == PO_TIED || (ip[k].what == PO_OP && d->op[ip[k].j].kind == K_VR)) vec = 1;
		fprintf(f, "let Features = \"knc\", Attributes = [NoThrow%s%s] in\n", (!d->mayload && !d->maystore && !c->pf) ? ", Const" : "",
		        vec ? ", RequiredVectorWidth<512>" : "");
		fprintf(f, "  def %s : X86Builtin<\"%s(", c->lname, ret_c(d, c));
		for (int k = 0; k < n; k++) {
			const char *t = ip[k].what == PO_TIED ? (d->op[d->out].kind == K_KR ? "unsigned short" : d->op[d->out].kind == K_VR ? cty[c->outvt] : ret_c(d, c)) : c_ty(d, c, &ip[k]);
			fprintf(f, "%s%s", k ? ", " : "", t);
		}
		fprintf(f, ")\">;\n");
		for (int k = 0; k < n; k++) {
			if (ip[k].what == PO_SSS) fprintf(sema, "    {X86::BI__builtin_ia32_%s, %d, 1, 0x%04x},\n", c->lname, k, sss_allowed(d));
			else if (ip[k].what == PO_OP && d->op[ip[k].j].kind == K_IMM) fprintf(sema, "    {X86::BI__builtin_ia32_%s, %d, 0, 0},\n", c->lname, k);
		}
	}
}

static void emit_report(FILE *f) {
	int g = 0;
	for (int i = 0; i < ndefs; i++) g += cgs[i].gen;
	fprintf(f, "knc-gen --codegen: %d of %d defs have an intrinsic\n", g, ndefs);
	for (int i = 0; i < ndefs; i++) if (!cgs[i].gen) fprintf(f, "  not generated: %-28s %s\n", defs[i].name, cgs[i].why);
	for (int i = 0; i < ndefs; i++) if (cgs[i].gen && (cgs[i].tied || cgs[i].blend)) fprintf(f, "  %s: %s\n", cgs[i].tied ? "tied " : "blend", defs[i].name);
}

/* The intrinsics whose first parameter is a merge-masked pass-through: a
   combine makes it undef under an all-ones mask (X86ISelLowering.cpp). */
static void emit_passthru(FILE *f) {
	fprintf(f, "// Generated by Intel-Phi-LLVM tools/knc-gen.c (--codegen). Do not edit by\n"
	           "// hand. Included by X86ISelLowering.cpp: the llvm.x86.knc.* intrinsics whose\n"
	           "// first parameter is a merge-masked pass-through and second the mask.\n");
	for (int i = 0; i < ndefs; i++) {
		Def *d = &defs[i]; CG *c = &cgs[i];
		if (c->gen && is_vr_out(d) && d->mimask >= 0 && !c->tied && !c->blend) fprintf(f, "    Intrinsic::x86_%s,\n", c->lname);
	}
}

/* The memory each memory form touches, for getTgtMemIntrinsic: the pointer
   argument, store or load, and whether the 64 bytes are below the pointer
   (vloadunpackh*, vpackstoreh*: the part of the line before it) or at it.
   A conversion touches less than 64 bytes; 64 covers it. */
static void emit_meminfo(FILE *f) {
	fprintf(f, "// Generated by Intel-Phi-LLVM tools/knc-gen.c (--codegen). Do not edit by\n"
	           "// hand. Included by X86ISelLowering.cpp (getTgtMemIntrinsic): intrinsic,\n"
	           "// pointer argument, 1 for a store, 1 when the 64 bytes are below the pointer.\n");
	for (int i = 0; i < ndefs; i++) {
		Def *d = &defs[i]; CG *c = &cgs[i];
		if (!c->gen || c->pf || (!d->mayload && !d->maystore)) continue;
		IP ip[8]; int n = cg_iparams(d, c, ip), ptr = -1;
		for (int k = 0; k < n; k++) if (ip[k].what == PO_OP && is_mem(d->op[ip[k].j].kind)) ptr = k;
		if (ptr < 0) continue;
		int below = !strncmp(d->mnem, "vloadunpackh", 12) || !strncmp(d->mnem, "vpackstoreh", 11);
		fprintf(f, "    {Intrinsic::x86_%s, %d, %d, %d},\n", c->lname, ptr, d->maystore, below);
	}
}

static int read_ops(const char *path) {
	FILE *f = fopen(path, "r"); if (!f) { perror(path); return 1; }
	char line[512];
	while (fgets(line, sizeof line, f)) {
		char *p[8]; int n = split(trim(line), '|', p, 8);
		if (n < 6 || strcmp(p[0], "O")) continue;
		OpsRec *o = &opsrec[nopsrec++];
		snprintf(o->mnem, sizeof o->mnem, "%s", p[2]); o->rd = atoi(p[4]); snprintf(o->writes, sizeof o->writes, "%s", p[5]);
	}
	fclose(f);
	return 0;
}

static int codegen(const char *opsfile, const char *dir) {
	if (read_ops(opsfile)) return 1;
	for (int i = 0; i < ndefs; i++) cg_classify(i);
	char p[512];
	FILE *fi, *fp, *fm, *fb, *fs, *fr, *ft, *fx;
	snprintf(p, sizeof p, "%s/IntrinsicsX86KNC.td", dir); fi = fopen(p, "w");
	snprintf(p, sizeof p, "%s/X86InstrKNCIntrin.td", dir); fp = fopen(p, "w");
	snprintf(p, sizeof p, "%s/X86KNCPseudos.inc", dir); fm = fopen(p, "w");
	snprintf(p, sizeof p, "%s/BuiltinsX86KNC.td", dir); fb = fopen(p, "w");
	snprintf(p, sizeof p, "%s/SemaX86KNC.inc", dir); fs = fopen(p, "w");
	snprintf(p, sizeof p, "%s/codegen-report.txt", dir); fr = fopen(p, "w");
	snprintf(p, sizeof p, "%s/X86KNCPassThru.inc", dir); ft = fopen(p, "w");
	snprintf(p, sizeof p, "%s/X86KNCMemIntrinsics.inc", dir); fx = fopen(p, "w");
	if (!fi || !fp || !fm || !fb || !fs || !fr || !ft || !fx) { perror("codegen output"); return 1; }
	emit_intrinsics(fi); emit_pseudos(fp, fm); emit_builtins(fb, fs); emit_report(fr); emit_passthru(ft); emit_meminfo(fx);
	fclose(fi); fclose(fp); fclose(fm); fclose(fb); fclose(fs); fclose(fr); fclose(ft); fclose(fx);
	return 0;
}

int main(int argc, char **argv) {
	int cgmode = argc > 1 && !strcmp(argv[1], "--codegen");
	if (cgmode) { argv++; argc--; }
	if (argc < 5) {
		fprintf(stderr, "usage: knc-gen knc-isa.txt knc-overrides.txt X86InstrKNC.td X86KNCInfo.inc [listing [sweep]]\n"
		                "       knc-gen --codegen knc-isa.txt knc-overrides.txt knc-ops.txt OUTDIR\n");
		return 2;
	}
	FILE *in = fopen(argv[1], "r"); if (!in) { perror(argv[1]); return 1; }
	Pseudo opseudo[16]; int nopseudo = 0;
	FILE *ov = fopen(argv[2], "r"); if (!ov) { perror(argv[2]); return 1; }
	char oline[512];
	while (fgets(oline, sizeof oline, ov)) {
		char *h = strchr(oline, '#'); if (h) *h = 0;
		char a[32], b[32], c[32], e[32];
		int n = sscanf(oline, "%31s %31s %31s %31s", a, b, c, e);
		if (n <= 0) continue;
		if (!strcmp(a, "rounding") && n == 2) snprintf(rounding_override[nrounding++], 32, "%s", b);
		else if (!strcmp(a, "nosae") && n == 2) snprintf(nosae_override[nnosae++], 32, "%s", b);
		else if (!strcmp(a, "maskreq") && n == 2) snprintf(maskreq_override[nmaskreq++], 32, "%s", b);
		else if (!strcmp(a, "tied") && n == 2) snprintf(tied_ov[ntied++], 32, "%s", b);
		else if (!strcmp(a, "untied") && n == 2) snprintf(untied_ov[nuntied++], 32, "%s", b);
		else if (!strcmp(a, "blend") && n == 2) snprintf(blend_ov[nblend++], 32, "%s", b);
		else if (!strcmp(a, "pseudo") && n == 4) {
			snprintf(opseudo[nopseudo].pseudo, 32, "%s", b); snprintf(opseudo[nopseudo].base, 32, "%s", c);
			opseudo[nopseudo++].imm = strcmp(e, "-") ? atoi(e) : -1;
		} else { fprintf(stderr, "knc-gen: bad override '%s'\n", oline); return 1; }
	}
	fclose(ov);
	char line[4096]; int block = 0;
	struct { char pred[16]; int imm; } cimm[16]; int ncimm = 0;
	while (fgets(line, sizeof line, in)) {
		char *p[8]; int n = split(trim(line), '|', p, 8);
		int ln = n > 1 ? atoi(p[1]) : 0;
		if (!strcmp(p[0], "B")) { block = ln; ncimm = 0; continue; }
		if (!strcmp(p[0], "I") && n == 4) {
			Row *r = &rows[nrows]; memset(r, 0, sizeof *r); r->line = ln; r->block = block;
			if (parse_row(r, p[2], p[3])) nrows++;
		} else if (!strcmp(p[0], "T") && n == 5) {
			Tab *t = &tabs[ntabs++]; memset(t, 0, sizeof *t); t->block = block; t->line = ln;
			snprintf(t->kind, 8, "%s", p[2]); snprintf(t->cls, 8, "%s", p[3]); parse_tab(t, p[4]);
		} else if (!strcmp(p[0], "C") && n == 4) {
			snprintf(cimm[ncimm].pred, 16, "%s", p[2]); cimm[ncimm++].imm = atoi(p[3]);
		} else if (!strcmp(p[0], "P") && n == 5) {
			Pseudo *ps = &pseudos[npseudos]; int found = -1;
			for (int i = 0; i < ncimm; i++) if (!strcmp(cimm[i].pred, p[4])) found = cimm[i].imm;
			if (found < 0) die("pseudo-op predicate without an immediate", ln);
			snprintf(ps->pseudo, 32, "%s", p[2]);
			/* the document writes the base of vpcmpd/vpcmpud as "vcmpd"/"vcmpud" */
			if (!strcmp(p[3], "vcmpd")) snprintf(ps->base, 32, "vpcmpd");
			else if (!strcmp(p[3], "vcmpud")) snprintf(ps->base, 32, "vpcmpud");
			else snprintf(ps->base, 32, "%s", p[3]);
			ps->imm = found;
			npseudos++;
		}
	}
	fclose(in);
	/* A real instruction of a pseudo-op's name wins (vpcmpeqd, vpcmpltd);
	 * the overrides replace or add pseudo-ops. */
	int np = 0;
	for (int i = 0; i < npseudos; i++) {
		int drop = 0;
		for (int j = 0; j < nrows; j++) if (!strcmp(rows[j].mnem, pseudos[i].pseudo)) drop = 1;
		for (int j = 0; j < nopseudo; j++) if (!strcmp(opseudo[j].pseudo, pseudos[i].pseudo)) drop = 1;
		if (!drop) pseudos[np++] = pseudos[i];
	}
	npseudos = np;
	for (int j = 0; j < nopseudo; j++) pseudos[npseudos++] = opseudo[j];
	for (int i = 0; i < nrows; i++) {
		int hasrm = 0;
		for (int j = 0; j < rows[i].nops; j++) if (rows[i].op[j].kind == K_RM) hasrm = 1;
		build_def(&rows[i], 0);
		if (hasrm) build_def(&rows[i], 1);
	}
	for (int i = 0; i < ndefs; i++) for (int j = i + 1; j < ndefs; j++)
		if (!strcmp(defs[i].name, defs[j].name)) { fprintf(stderr, "duplicate %s\n", defs[i].name); return 1; }
	if (cgmode) return codegen(argv[3], argv[4]);
	FILE *td = fopen(argv[3], "w"), *inc = fopen(argv[4], "w");
	if (!td || !inc) { perror("output"); return 1; }
	emit_td(td); emit_inc(inc);
	fclose(td); fclose(inc);
	if (argc > 5) { FILE *l = fopen(argv[5], "w"); emit_listing(l); fclose(l); }
	if (argc > 6) { FILE *s = fopen(argv[6], "w"); emit_sweep(s); fclose(s); }
	fprintf(stderr, "knc-gen: %d rows, %d defs, %d decoration tables, %d pseudo-ops\n", nrows, ndefs, ndecos - 1, npseudos);
	return 0;
}
