/* knc-gen.c: the Knights Corner instruction table -> LLVM X86 TableGen defs and
 * the C++ side table that the assembler, the printers, the encoder and the
 * disassembler share (patch 0013). See knc-gen.md.
 *
 *   knc-gen knc-isa.txt knc-overrides.txt X86InstrKNC.td X86KNCInfo.inc [listing]
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

int main(int argc, char **argv) {
	if (argc < 5) { fprintf(stderr, "usage: knc-gen knc-isa.txt knc-overrides.txt X86InstrKNC.td X86KNCInfo.inc [listing]\n"); return 2; }
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
	FILE *td = fopen(argv[3], "w"), *inc = fopen(argv[4], "w");
	if (!td || !inc) { perror("output"); return 1; }
	emit_td(td); emit_inc(inc);
	fclose(td); fclose(inc);
	if (argc > 5) { FILE *l = fopen(argv[5], "w"); emit_listing(l); fclose(l); }
	if (argc > 6) { FILE *s = fopen(argv[6], "w"); emit_sweep(s); fclose(s); }
	fprintf(stderr, "knc-gen: %d rows, %d defs, %d decoration tables, %d pseudo-ops\n", nrows, ndefs, ndecos - 1, npseudos);
	return 0;
}
