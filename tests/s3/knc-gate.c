/* knc-gate.c: the S3 gate for libknc. Intel-Phi-3120A's libknc.a (symbols
 * prefixed ref_) and knc.c (knc-c-gen) run on the same inputs: every
 * kernel at every width must give identical output, a width outside the
 * range must leave memory untouched, and every kernel is timed interleaved.
 * See README.md. */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

typedef void (*unpack_fn)(int *, const unsigned *);
typedef void (*pack_fn)(unsigned *, const int *);
typedef void (*base_fn)(int *, const unsigned *, const int *);
typedef void (*fls_fn)(const unsigned *, unsigned *, const unsigned *);
extern const unpack_fn knc_unpack_table[33], ref_knc_unpack_table[33];
extern const pack_fn knc_pack_table[33], ref_knc_pack_table[33];
extern const base_fn knc_unpack_for_table[33], ref_knc_unpack_for_table[33];
extern const base_fn knc_unpack_delta_table[33], ref_knc_unpack_delta_table[33];
extern const fls_fn knc_fls_unffor_table[33], ref_knc_fls_unffor_table[33];
void knc_unpack(int *, const void *, unsigned), ref_knc_unpack(int *, const void *, unsigned);
void knc_pack(void *, const int *, unsigned), ref_knc_pack(void *, const int *, unsigned);
void knc_unpack_for(int *, const void *, unsigned, const int *), ref_knc_unpack_for(int *, const void *, unsigned, const int *);
void knc_unpack_delta(int *, const void *, unsigned, const int *), ref_knc_unpack_delta(int *, const void *, unsigned, const int *);
void knc_encode_for(int *, const int *, const int *), ref_knc_encode_for(int *, const int *, const int *);
void knc_encode_delta(int *, const int *, const int *), ref_knc_encode_delta(int *, const int *, const int *);
void knc_fls_unffor(const unsigned *, unsigned *, unsigned, const unsigned *);
void ref_knc_fls_unffor(const unsigned *, unsigned *, unsigned, const unsigned *);

static double now(void)
{
	struct timespec t;
	clock_gettime(CLOCK_MONOTONIC, &t);
	return t.tv_sec + t.tv_nsec * 1e-9;
}

static int cmp_d(const void *a, const void *b)
{
	double x = *(const double *)a, y = *(const double *)b;
	return x < y ? -1 : x > y;
}

static uint32_t rs = 2463534242u;
static uint32_t rnd(void) { rs ^= rs << 13; rs ^= rs >> 17; rs ^= rs << 5; return rs; }

#define ROUNDS 7
static _Alignas(64) unsigned packed[1024 + 64], in_fls[1024 + 64];
static _Alignas(64) int vals[1024], o1[1024 + 32], o2[1024 + 32], base[16];
static size_t cases, bad;
static int timed, within, faster, slower;

static void fill(int k)
{
	for (int i = 0; i < 1024 + 64; i++) packed[i] = in_fls[i] = k == 0 ? 0 : k == 1 ? ~0u : rnd();
	for (int i = 0; i < 1024; i++) vals[i] = (int)rnd();
	for (int i = 0; i < 16; i++) base[i] = (int)rnd();
}

static void same(const void *a, const void *b, size_t n)
{
	cases++;
	bad += memcmp(a, b, n) != 0;
}

/* one kernel, timed: ns per call, .byte against intrinsics */
static void timeit(const char *name, int b, int which)
{
	double tr[ROUNDS], ti[ROUNDS];
	for (int r = 0; r < ROUNDS; r++)
		for (int o = 0; o < 2; o++) {
			int intr = (r + o) & 1;
			double t0 = now();
			for (int n = 0; n < 2000; n++) {
				switch (which) {
				case 0: (intr ? knc_unpack_table : ref_knc_unpack_table)[b](o1, packed); break;
				case 1: (intr ? knc_pack_table : ref_knc_pack_table)[b]((unsigned *)o1, vals); break;
				case 2: (intr ? knc_unpack_for_table : ref_knc_unpack_for_table)[b](o1, packed, base); break;
				case 3: (intr ? knc_unpack_delta_table : ref_knc_unpack_delta_table)[b](o1, packed, base); break;
				case 4: (intr ? knc_fls_unffor_table : ref_knc_fls_unffor_table)[b](in_fls + 1, (unsigned *)o1, (unsigned *)base); break;
				case 5: (intr ? knc_encode_for : ref_knc_encode_for)(o1, vals, base); break;
				default: (intr ? knc_encode_delta : ref_knc_encode_delta)(o1, vals, base); break;
				}
			}
			(intr ? ti : tr)[r] = (now() - t0) * 1e9 / 2000;
		}
	qsort(tr, ROUNDS, sizeof tr[0], cmp_d); qsort(ti, ROUNDS, sizeof ti[0], cmp_d);
	double ratio = tr[ROUNDS / 2] / ti[ROUNDS / 2];
	int ok = ti[ROUNDS / 2] <= tr[ROUNDS - 1];
	timed++; within += ok; slower += !ok; faster += ti[ROUNDS / 2] < tr[0];
	printf("%-12s b%-2d .byte %7.1f ns [%7.1f..%7.1f]  intrinsics %7.1f ns [%7.1f..%7.1f]  ratio %.3f %s\n", name,
	       b, tr[ROUNDS / 2], tr[0], tr[ROUNDS - 1], ti[ROUNDS / 2], ti[0], ti[ROUNDS - 1], ratio, ok ? "within" : "SLOWER");
}

int main(void)
{
	/* output */
	for (int t = 0; t < 40; t++) {
		fill(t < 2 ? t : 2);
		for (int b = 1; b <= 32; b++) {
			memset(o1, 0xAA, sizeof o1); memset(o2, 0xAA, sizeof o2);
			ref_knc_unpack(o1, packed, b); knc_unpack(o2, packed, b); same(o1, o2, sizeof o1);
			ref_knc_unpack_for(o1, packed, b, base); knc_unpack_for(o2, packed, b, base); same(o1, o2, sizeof o1);
			ref_knc_unpack_delta(o1, packed, b, base); knc_unpack_delta(o2, packed, b, base); same(o1, o2, sizeof o1);
			memset(o1, 0xAA, sizeof o1); memset(o2, 0xAA, sizeof o2);
			ref_knc_pack(o1, vals, b); knc_pack(o2, vals, b); same(o1, o2, sizeof o1);
		}
		for (int b = 0; b <= 32; b++)
			for (int off = 0; off < 16; off += 5) { /* in is only 4-byte aligned */
				memset(o1, 0xAA, sizeof o1); memset(o2, 0xAA, sizeof o2);
				ref_knc_fls_unffor(in_fls + off, (unsigned *)o1, b, (unsigned *)base);
				knc_fls_unffor(in_fls + off, (unsigned *)o2, b, (unsigned *)base);
				same(o1, o2, sizeof o1);
			}
		memset(o1, 0xAA, sizeof o1); memset(o2, 0xAA, sizeof o2);
		ref_knc_encode_for(o1, vals, base); knc_encode_for(o2, vals, base); same(o1, o2, sizeof o1);
		ref_knc_encode_delta(o1, vals, base); knc_encode_delta(o2, vals, base); same(o1, o2, sizeof o1);
	}
	/* widths outside the range touch nothing */
	static const unsigned outside[] = {0, 33, 64, 0xffffffffu};
	for (size_t i = 0; i < 4; i++) {
		memset(o1, 0xAA, sizeof o1); memset(o2, 0xAA, sizeof o2);
		knc_unpack(o2, packed, outside[i]); knc_pack(o2, vals, outside[i]);
		knc_unpack_for(o2, packed, outside[i], base); knc_unpack_delta(o2, packed, outside[i], base);
		if (outside[i]) knc_fls_unffor(in_fls, (unsigned *)o2, outside[i], (unsigned *)base);
		same(o1, o2, sizeof o1);
	}
	printf("output: %zu cases (40 inputs: zeros, ones, random; every width of unpack, for, delta, pack; fls unffor b0..b32 at 4 misalignments; encode; out-of-range widths), %zu differ\n",
	       cases, bad);

	/* speed */
	fill(2);
	static const char *names[7] = {"unpack", "pack", "unpack_for", "unpack_delta", "fls_unffor", "encode_for", "encode_delta"};
	for (int k = 0; k < 5; k++)
		for (int b = k == 4 ? 0 : 1; b <= 32; b++) timeit(names[k], b, k);
	timeit(names[5], 0, 5);
	timeit(names[6], 0, 6);
	printf("speed: %d kernels timed, %d within the .byte range or better (%d faster than all of its runs), %d slower\n", timed,
	       within, faster, slower);
	printf("knc-gate: %s\n", bad ? "FAIL (output)" : slower ? "SLOWER" : "PASS");
	return bad ? 1 : slower ? 3 : 0;
}
