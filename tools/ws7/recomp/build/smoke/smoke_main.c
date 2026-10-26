// Link smoke test for the whole-image build: load the plain image, look up
// zlib 1.2.3 adler32 / crc32 in rc_table and compare with the system zlib.
// usage: smoke PLAIN_IMAGE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <zlib.h>

#include "rc.h"

enum { RAX = 0, RCX = 1, RDX = 2, RSP = 4, RSI = 6, RDI = 7 };
static rc_ctx ctx;
static uint8_t *gstack;
#define GSTACK (1 << 20)

static uint64_t call3(uint64_t vm, uint64_t a, uint64_t b, uint64_t d) {
  rc_fn f = rc_lookup(rc_slide + vm);
  if (!f) { fprintf(stderr, "smoke: 0x%llx not in rc_table\n", (unsigned long long)vm); exit(1); }
  ctx.r[RDI] = a; ctx.r[RSI] = b; ctx.r[RDX] = d;
  uint64_t sp = ((uint64_t)(gstack + GSTACK) & ~15ull) - 8;
  *(uint64_t *)sp = 0xdeadbeefull;
  ctx.r[RSP] = sp;
  f(&ctx);
  if (ctx.r[RSP] != sp + 8) { fprintf(stderr, "smoke: stack imbalance\n"); exit(1); }
  return ctx.r[RAX];
}

int main(int argc, char **argv) {
  if (argc != 2) { fprintf(stderr, "usage: %s PLAIN_IMAGE\n", argv[0]); return 2; }
  if (rc_image_load(argv[1])) { fprintf(stderr, "smoke: cannot map %s\n", argv[1]); return 2; }
  gstack = malloc(GSTACK);
  uint8_t buf[4096];
  unsigned seed = 12345;
  for (int i = 0; i < (int)sizeof buf; i++) { seed = seed * 1103515245 + 12345; buf[i] = seed >> 16; }
  int bad = 0, n = 0;
  for (unsigned len = 0; len <= sizeof buf; len += (len < 64 ? 1 : 97)) {
    uint32_t a = (uint32_t)call3(0x100e071d0, 1, (uint64_t)buf, len);
    uint32_t ea = (uint32_t)adler32(1, buf, len);
    uint32_t c = (uint32_t)call3(0x100e076f0, 0, (uint64_t)buf, len);
    uint32_t ec = (uint32_t)crc32(0, buf, len);
    n += 2;
    if (a != ea) { bad++; printf("adler32 len=%u got %08x want %08x\n", len, a, ea); }
    if (c != ec) { bad++; printf("crc32 len=%u got %08x want %08x\n", len, c, ec); }
  }
  printf("smoke: %d checks, %d bad\n", n, bad);
  return bad != 0;
}
