/* bitunpack-gate.c: the S3 gate for bitunpack. The intrinsic knc_unpack_bB
 * (bitunpack.c) and the stack's .byte ones (bitunpack.S, symbols prefixed
 * ref_) unpack the same random streams: all 1024 values must be identical;
 * speed is measured interleaved. See README.md. */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

void knc_unpack_b5(int *, const unsigned *);
void knc_unpack_b11(int *, const unsigned *);
void knc_unpack_b16(int *, const unsigned *);
void ref_knc_unpack_b5(int *, const unsigned *);
void ref_knc_unpack_b11(int *, const unsigned *);
void ref_knc_unpack_b16(int *, const unsigned *);

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

#define ROUNDS 9

int main(void)
{
	static void (*const fi[3])(int *, const unsigned *) = {knc_unpack_b5, knc_unpack_b11, knc_unpack_b16};
	static void (*const fr[3])(int *, const unsigned *) = {ref_knc_unpack_b5, ref_knc_unpack_b11, ref_knc_unpack_b16};
	static const int bits[3] = {5, 11, 16};
	static _Alignas(64) unsigned packed[512];
	static _Alignas(64) int o1[1024], o2[1024];
	uint32_t s = 88172645u;
	size_t bad = 0, cases = 0;
	for (int t = 0; t < 2000; t++)
		for (int k = 0; k < 3; k++) {
			for (int i = 0; i < 512; i++) { s ^= s << 13; s ^= s >> 17; s ^= s << 5; packed[i] = t < 2 ? (t ? ~0u : 0u) : s; }
			memset(o1, 0xAA, sizeof o1); memset(o2, 0x55, sizeof o2);
			fr[k](o1, packed); fi[k](o2, packed);
			bad += memcmp(o1, o2, sizeof o1) != 0;
			cases++;
		}
	printf("output: %zu unpacks of 1024 values (widths 5, 11, 16; all-zero, all-one and random streams), %zu differ\n", cases, bad);

	int slow = 0;
	for (int k = 0; k < 3; k++) {
		double tr[ROUNDS], ti[ROUNDS];
		for (int r = 0; r < ROUNDS; r++)
			for (int o = 0; o < 2; o++) {
				int intr = (r + o) & 1;
				double t0 = now();
				for (int n = 0; n < 20000; n++) (intr ? fi : fr)[k](o1, packed);
				(intr ? ti : tr)[r] = (now() - t0) * 1e9 / 20000;
			}
		qsort(tr, ROUNDS, sizeof tr[0], cmp_d); qsort(ti, ROUNDS, sizeof ti[0], cmp_d);
		/* equal within the spread: the median is inside the .byte range, or better */
		int within = ti[ROUNDS / 2] <= tr[ROUNDS - 1];
		slow += !within;
		printf("knc_unpack_b%-2d: .byte median %8.1f ns [%8.1f..%8.1f]  intrinsics median %8.1f ns [%8.1f..%8.1f]  ratio %.3f %s\n",
		       bits[k], tr[ROUNDS / 2], tr[0], tr[ROUNDS - 1], ti[ROUNDS / 2], ti[0], ti[ROUNDS - 1],
		       tr[ROUNDS / 2] / ti[ROUNDS / 2], within ? "within" : "SLOWER");
	}
	printf("bitunpack-gate: %s\n", bad ? "FAIL (output)" : slow ? "SLOWER" : "PASS");
	return bad ? 1 : slow ? 3 : 0;
}
