/* mvex-probe.c: runs the Knights Corner instructions of mvex-probe.s on a
 * card and checks every lane against values computed here in scalar code
 * from the semantics in Intel's 327364-001 (sections 2.2 and 2.3 and the
 * instruction pages). Prints one line per test and "mvex-probe: PASS" when
 * every lane is right. See mvex-probe.md. */
#include <stdint.h>
#include <stdio.h>
#include <string.h>

void knc_swizzle(const int32_t *in, int32_t *out);
void knc_broadcast(const int32_t *in, int32_t *out);
void knc_upconv(const void *in, int32_t *out, uint32_t *half);
void knc_downconv(const int32_t *in, void *out);
void knc_mask(const int32_t *a, const int32_t *b, int32_t *out, unsigned mask);
void knc_gather(const int32_t *table, const int32_t *idx, int32_t *out);
void knc_scatter(int32_t *table, const int32_t *idx, const int32_t *val);
void knc_pack(const int32_t *in, int32_t *packed, int32_t *unpacked, unsigned mask);
void knc_round(const uint32_t *a, const uint32_t *b, uint32_t *out);
unsigned knc_cmplt(const float *a, const float *b);
void knc_bits(unsigned long x, unsigned long *out);

static int checks, wrong;

static void report(const char *name, int right, int total) {
	printf("%-10s %4d of %4d right\n", name, right, total);
	checks += total;
	wrong += total - right;
}

/* binary16 -> binary32 bits, for the normal numbers and zero used here,
 * after the probe's vaddps to +0.0: -0 + +0 is +0 (IEEE 754, round to
 * nearest), which the card returned on 2026-10-16 for 0x8000 */
static uint32_t half_bits(uint16_t h) {
	uint32_t s = (uint32_t)(h >> 15) << 31, e = h >> 10 & 31, m = h & 1023;
	if (!e && !m) return 0;
	return s | (e - 15 + 127) << 23 | m << 13;
}

int main(void) {
	_Alignas(64) int32_t in[16], out[8 * 16];
	for (int i = 0; i < 16; i++) in[i] = 1000 * i + 7;

	/* register swizzles (table 2.2): element i of each 4-element block */
	static const int perm[8][4] = {{0, 1, 2, 3}, {1, 0, 3, 2}, {2, 3, 0, 1}, {1, 2, 0, 3},
	                               {0, 0, 0, 0}, {1, 1, 1, 1}, {2, 2, 2, 2}, {3, 3, 3, 3}};
	knc_swizzle(in, out);
	int ok = 0;
	for (int s = 0; s < 8; s++)
		for (int i = 0; i < 16; i++) ok += out[s * 16 + i] == in[(i & ~3) | perm[s][i & 3]];
	report("swizzle", ok, 128);

	/* broadcasts (table 2.5): {1to16}, {4to16} */
	knc_broadcast(in, out);
	ok = 0;
	for (int i = 0; i < 16; i++) ok += (out[i] == in[0]) + (out[16 + i] == in[i % 4]);
	report("broadcast", ok, 32);

	/* load-op up-conversions (table 2.5) and {float16} (table 2.4) */
	_Alignas(64) unsigned char up[192];
	static const unsigned char bytes[16] = {0, 1, 2, 100, 127, 128, 129, 200, 250, 255, 3, 64, 192, 77, 254, 31};
	static const uint16_t words[16] = {0, 1, 2, 1000, 32767, 32768, 32769, 40000, 65535, 3, 12345, 54321, 255, 256, 65534, 7};
	static const uint16_t halves[16] = {0x3c00, 0xc000, 0x3800, 0x7bff, 0x3e00, 0x3400, 0xba00, 0x4200,
	                                    0x0000, 0x8000, 0x4900, 0xc880, 0x3c01, 0x5640, 0x2e66, 0x4b00};
	memset(up, 0, sizeof up);
	memcpy(up, bytes, 16);
	memcpy(up + 64, words, 32);
	memcpy(up + 128, halves, 32);
	_Alignas(64) uint32_t half[16];
	knc_upconv(up, out, half);
	ok = 0;
	for (int i = 0; i < 16; i++) {
		if (out[i] != (int32_t)bytes[i] || out[16 + i] != (int32_t)(int8_t)bytes[i] || out[32 + i] != (int32_t)words[i] || out[48 + i] != (int32_t)(int16_t)words[i])
			printf("  int lane %d: %d %d %d %d\n", i, out[i], out[16 + i], out[32 + i], out[48 + i]);
		ok += out[i] == (int32_t)bytes[i];
		ok += out[16 + i] == (int32_t)(int8_t)bytes[i];
		ok += out[32 + i] == (int32_t)words[i];
		ok += out[48 + i] == (int32_t)(int16_t)words[i];
		if (half[i] != half_bits(halves[i]))
			printf("  float16 lane %d: 0x%04x -> 0x%08x, expected 0x%08x\n", i, halves[i], half[i], half_bits(halves[i]));
		ok += half[i] == half_bits(halves[i]);
	}
	report("upconv", ok, 80);

	/* store down-conversions (table 2.12): {uint8} writes 16 bytes,
	 * {sint16} 32; the bytes after them keep their fill */
	_Alignas(64) int32_t dsrc[16];
	_Alignas(64) unsigned char down[128];
	for (int i = 0; i < 16; i++) dsrc[i] = (int32_t[]){0, 1, 127, 128, 200, 255, 3, 250, 64, 192, 77, 254, 31, 9, 130, 240}[i];
	memset(down, 0xaa, sizeof down);
	knc_downconv(dsrc, down);
	ok = 0;
	for (int i = 0; i < 16; i++) {
		int16_t w;
		memcpy(&w, down + 64 + 2 * i, 2);
		ok += (down[i] == (unsigned char)dsrc[i]) + (down[16 + i] == 0xaa) + (w == (int16_t)dsrc[i]);
	}
	for (int i = 96; i < 128; i++) ok += down[i] == 0xaa;
	report("downconv", ok, 80);

	/* merge masking */
	_Alignas(64) int32_t a[16], b[16];
	for (int i = 0; i < 16; i++) a[i] = 3 * i + 1, b[i] = 100000 + i;
	knc_mask(a, b, out, 0x5a5a);
	ok = 0;
	for (int i = 0; i < 16; i++) ok += out[i] == (0x5a5a >> i & 1 ? a[i] + b[i] : b[i]);
	report("mask", ok, 16);

	/* gather and scatter */
	_Alignas(64) int32_t table[64], idx[16], val[16];
	for (int i = 0; i < 64; i++) table[i] = 7 * i + 5;
	for (int i = 0; i < 16; i++) idx[i] = (37 * i + 11) % 64, val[i] = 50000 + i;
	knc_gather(table, idx, out);
	ok = 0;
	for (int i = 0; i < 16; i++) ok += out[i] == table[idx[i]];
	report("gather", ok, 16);
	memset(table, 0, sizeof table);
	knc_scatter(table, idx, val);
	ok = 0;
	for (int t = 0; t < 64; t++) {
		int32_t want = 0;
		for (int i = 0; i < 16; i++)
			if (idx[i] == t) want = val[i];
		ok += table[t] == want;
	}
	report("scatter", ok, 64);

	/* vpackstoreld and vloadunpackld under a mask */
	_Alignas(64) int32_t packed[16], unpacked[16];
	unsigned pm = 0x9339;
	for (int i = 0; i < 16; i++) packed[i] = -1;
	knc_pack(in, packed, unpacked, pm);
	ok = 0;
	int k = 0, n = 0;
	for (int i = 0; i < 16; i++)
		if (pm >> i & 1) ok += packed[n++] == in[i];
	for (int i = n; i < 16; i++) ok += packed[i] == -1;
	for (int i = 0; i < 16; i++) ok += unpacked[i] == (pm >> i & 1 ? in[k++] : 0);
	report("pack", ok, 32);

	/* static rounding (table 2.14): 1 + 0.75 ulp, both signs */
	_Alignas(64) uint32_t ra[16], rb[16], rout[64];
	for (int i = 0; i < 16; i++) ra[i] = i & 1 ? 0xbf800000u : 0x3f800000u, rb[i] = i & 1 ? 0xb3c00000u : 0x33c00000u;
	static const uint32_t want_pos[4] = {0x3f800001u, 0x3f800000u, 0x3f800001u, 0x3f800000u};
	static const uint32_t want_neg[4] = {0xbf800001u, 0xbf800001u, 0xbf800000u, 0xbf800000u};
	knc_round(ra, rb, rout);
	ok = 0;
	for (int m = 0; m < 4; m++)
		for (int i = 0; i < 16; i++) ok += rout[16 * m + i] == (i & 1 ? want_neg[m] : want_pos[m]);
	report("round", ok, 64);

	/* vcmpltps into a mask register, read through kmov */
	_Alignas(64) float fa[16], fb[16];
	for (int i = 0; i < 16; i++) fa[i] = (float)i, fb[i] = (float)(15 - i);
	unsigned mask = knc_cmplt(fa, fb), want = 0;
	for (int i = 0; i < 16; i++) want |= (unsigned)(fa[i] < fb[i]) << i;
	report("cmplt", (mask & 0xffff) == want, 1);

	/* the KNC VEX scalar forms */
	unsigned long bits[3], x = 0x00f0000000001000ul;
	knc_bits(x, bits);
	report("bits", (bits[0] == 5) + (bits[1] == 8) + (bits[2] == 12), 3);

	printf("mvex-probe: %s (%d checks, %d wrong)\n", wrong ? "FAIL" : "PASS", checks, wrong);
	return wrong != 0;
}
