/* vpu_int-gate.c: the S3 gate for vpu_int. The intrinsic vpu_int_probe and
 * vpu_int_unaligned (vpu_int.c) and the stack's .byte ones (vpu_int.S,
 * symbols prefixed ref_) run on the same inputs: every slot must be
 * identical; speed is measured interleaved. See README.md. */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

void vpu_int_probe(const int *in, int *out);
void vpu_int_unaligned(const int *un, int *out);
void ref_vpu_int_probe(const int *in, int *out);
void ref_vpu_int_unaligned(const int *un, int *out);

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

#define SLOTS 18
#define ROUNDS 9

int main(void)
{
	static _Alignas(64) int in[32], o1[SLOTS * 16], o2[SLOTS * 16], buf[64];
	uint32_t s = 2463534242u;
	size_t bad = 0, cases = 0;
	for (int t = 0; t < 4096; t++) {
		for (int i = 0; i < 32; i++) {
			s ^= s << 13; s ^= s >> 17; s ^= s << 5;
			/* shift counts in b stay meaningful: some in 0..31, some not */
			in[i] = i >= 16 && (t & 1) ? (int)(s & 31) : (int)s;
		}
		memset(o1, 0xAA, sizeof o1); memset(o2, 0xAA, sizeof o2);
		ref_vpu_int_probe(in, o1); vpu_int_probe(in, o2);
		bad += memcmp(o1, o2, sizeof o1) != 0;
		cases++;
	}
	for (int i = 0; i < 64; i++) buf[i] = 1000 + i;
	for (int off = 0; off < 32; off++) {
		memset(o1, 0xAA, 64); memset(o2, 0xAA, 64);
		ref_vpu_int_unaligned(buf + off, o1); vpu_int_unaligned(buf + off, o2);
		bad += memcmp(o1, o2, 64) != 0;
		cases++;
	}
	printf("output: %zu cases (4096 probes of %d slots, 32 unaligned offsets), %zu differ\n", cases, SLOTS, bad);

	int slow = 0;
	for (int f = 0; f < 2; f++) {
		double tr[ROUNDS], ti[ROUNDS];
		for (int r = 0; r < ROUNDS; r++)
			for (int o = 0; o < 2; o++) {
				int intr = (r + o) & 1;
				double t0 = now();
				for (int n = 0; n < 200000; n++) {
					if (f == 0) (intr ? vpu_int_probe : ref_vpu_int_probe)(in, o1);
					else (intr ? vpu_int_unaligned : ref_vpu_int_unaligned)(buf + (n & 15), o1);
				}
				(intr ? ti : tr)[r] = (now() - t0) * 1e9 / 200000;
			}
		qsort(tr, ROUNDS, sizeof tr[0], cmp_d); qsort(ti, ROUNDS, sizeof ti[0], cmp_d);
		/* equal within the spread: the median is inside the .byte range, or better */
		int within = ti[ROUNDS / 2] <= tr[ROUNDS - 1];
		slow += !within;
		printf("%-18s: .byte median %7.1f ns [%7.1f..%7.1f]  intrinsics median %7.1f ns [%7.1f..%7.1f]  ratio %.3f %s\n",
		       f ? "vpu_int_unaligned" : "vpu_int_probe", tr[ROUNDS / 2], tr[0], tr[ROUNDS - 1], ti[ROUNDS / 2], ti[0],
		       ti[ROUNDS - 1], tr[ROUNDS / 2] / ti[ROUNDS / 2], within ? "within" : "SLOWER");
	}
	printf("vpu_int-gate: %s\n", bad ? "FAIL (output)" : slow ? "SLOWER" : "PASS");
	return bad ? 1 : slow ? 3 : 0;
}
