/* vpu_memcpy-gate.c: the S3 gate for vpu_memcpy. The intrinsic kernels
 * (vpu_memcpy.c) and the stack's .byte kernels (vpu_memcpy.S, symbols
 * prefixed ref_) run on the same inputs: their output must be identical
 * byte for byte, and their speed is measured interleaved. See README.md. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

void *knc_memcpy64(void *, const void *, size_t);
void *knc_memset64_ord(void *, size_t);
void *knc_memset64(void *, size_t);
void *ref_knc_memcpy64(void *, const void *, size_t);
void *ref_knc_memset64_ord(void *, size_t);
void *ref_knc_memset64(void *, size_t);

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

#define ARENA (8u << 20)
#define ROUNDS 9

int main(void)
{
	unsigned char *a, *b, *c;
	if (posix_memalign((void **)&a, 64, ARENA) || posix_memalign((void **)&b, 64, ARENA) ||
	    posix_memalign((void **)&c, 64, ARENA))
		return 2;
	for (size_t i = 0; i < ARENA; i++) a[i] = (unsigned char)(i * 31u + 7u);

	/* Output: every block count 0..64, and 8 MiB less one block. */
	size_t counts[67], nc = 0, bad = 0;
	for (size_t k = 0; k <= 64; k++) counts[nc++] = k;
	counts[nc++] = ARENA / 64 - 1;
	counts[nc++] = ARENA / 64;
	for (size_t i = 0; i < nc; i++) {
		size_t k = counts[i];
		memset(b, 0xAA, ARENA); memset(c, 0xAA, ARENA);
		void *rb = ref_knc_memcpy64(b, a, k), *rc = knc_memcpy64(c, a, k);
		bad += memcmp(b, c, ARENA) != 0 || (rb == b) != (rc == c);
		memset(b, 0x55, ARENA); memset(c, 0x55, ARENA);
		ref_knc_memset64_ord(b, k); knc_memset64_ord(c, k);
		bad += memcmp(b, c, ARENA) != 0;
		memset(b, 0x55, ARENA); memset(c, 0x55, ARENA);
		ref_knc_memset64(b, k); knc_memset64(c, k);
		bad += memcmp(b, c, ARENA) != 0;
	}
	printf("output: %zu cases (memcpy64, memset64_ord, memset64 at %zu block counts), %zu differ\n",
	       3 * nc, nc, bad);

	/* Speed: per kernel and size, ROUNDS rounds, each timing both versions
	 * (which goes first alternates). */
	static const size_t sizes[] = {64, 256, 4096, 65536, 1u << 20};
	static const char *const names[] = {"memcpy64", "memset64_ord", "memset64"};
	int slow = 0;
	for (int kn = 0; kn < 3; kn++)
	for (size_t s = 0; s < sizeof sizes / sizeof sizes[0]; s++) {
		size_t n = sizes[s], blocks = n / 64, iters = ARENA / n, reps = n < 4096 ? 16 : 4;
		double tr[ROUNDS], ti[ROUNDS];
		for (int r = 0; r < ROUNDS; r++) {
			for (int o = 0; o < 2; o++) {
				int intr = (r + o) & 1;
				double t0 = now();
				for (size_t rep = 0; rep < reps; rep++)
					for (size_t i = 0; i < iters; i++) {
						unsigned char *d = b + i * n;
						if (kn == 0) (intr ? knc_memcpy64 : ref_knc_memcpy64)(d, a + i * n, blocks);
						else if (kn == 1) (intr ? knc_memset64_ord : ref_knc_memset64_ord)(d, blocks);
						else (intr ? knc_memset64 : ref_knc_memset64)(d, blocks);
					}
				double mbs = (double)(iters * reps * n) / 1e6 / (now() - t0);
				(intr ? ti : tr)[r] = mbs;
			}
		}
		qsort(tr, ROUNDS, sizeof tr[0], cmp_d); qsort(ti, ROUNDS, sizeof ti[0], cmp_d);
		double ratio = ti[ROUNDS / 2] / tr[ROUNDS / 2];
		/* equal within the spread: the median is inside the .byte range, or better */
		int within = ti[ROUNDS / 2] >= tr[0];
		slow += !within;
		printf("%-12s %8zu B: .byte median %8.1f MB/s [%8.1f..%8.1f]  intrinsics median %8.1f MB/s [%8.1f..%8.1f]  ratio %.3f %s\n",
		       names[kn], n, tr[ROUNDS / 2], tr[0], tr[ROUNDS - 1], ti[ROUNDS / 2], ti[0], ti[ROUNDS - 1], ratio,
		       within ? "within" : "BELOW");
	}
	printf("vpu_memcpy-gate: %s\n", bad ? "FAIL (output)" : slow ? "SLOWER" : "PASS");
	return bad ? 1 : slow ? 3 : 0;
}
