// libpng 1.5.13 differential corpus, shared by the x86_64 differential test
// (original game libpng) and the arm64 native test (third_party/libpng-1.5.13).
//
// Inputs are PNGs written by the rebuilt libpng's encoder from vectors.h's RNG:
// every colour type and bit depth, interlaced or not, with and without tRNS,
// gAMA, text and unknown chunks, some of them corrupted or truncated. Each is
// decoded twice:
//   - the game's pipeline (Scaleform's PNG loader at 0x100d04790): strip_16,
//     palette_to_rgb, expand_gray_1_2_4_to_8, tRNS_to_alpha, gray_to_rgb and
//     set_gamma(2.2, file gamma), then png_read_image;
//   - no transforms, row by row with png_read_row (so interlaced images come
//     out pass by pass).
// The outcome is the IHDR before and after the transforms, rowbytes, a CRC of
// every decoded byte, and the error and warning messages.
#ifndef RS_GAME_PNG_CASES_H
#define RS_GAME_PNG_CASES_H

#include <setjmp.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <zlib.h>

#include "png.h"
#include "vectors.h"

// The libpng calls the decoder makes. Filled with the original image's
// functions in the differential test, with the rebuilt library otherwise.
typedef struct {
  png_structp (*create_read_struct)(png_const_charp, png_voidp, png_error_ptr, png_error_ptr);
  png_infop (*create_info_struct)(png_structp);
  void (*destroy_read_struct)(png_structpp, png_infopp, png_infopp);
  void (*set_read_fn)(png_structp, png_voidp, png_rw_ptr);
  void (*set_sig_bytes)(png_structp, int);
  int (*sig_cmp)(png_const_bytep, png_size_t, png_size_t);
  void (*read_info)(png_structp, png_infop);
  png_uint_32 (*get_IHDR)(png_structp, png_infop, png_uint_32 *, png_uint_32 *, int *, int *, int *,
                          int *, int *);
  png_uint_32 (*get_valid)(png_const_structp, png_const_infop, png_uint_32);
  png_uint_32 (*get_gAMA)(png_const_structp, png_const_infop, double *);
  png_size_t (*get_rowbytes)(png_const_structp, png_const_infop);
  void (*set_strip_16)(png_structp);
  void (*set_palette_to_rgb)(png_structp);
  void (*set_expand_gray_1_2_4_to_8)(png_structp);
  void (*set_tRNS_to_alpha)(png_structp);
  void (*set_gray_to_rgb)(png_structp);
  void (*set_gamma)(png_structp, double, double);
  void (*read_update_info)(png_structp, png_infop);
  void (*read_image)(png_structp, png_bytepp);
  void (*read_row)(png_structp, png_bytep, png_bytep);
  void (*read_end)(png_structp, png_infop);
} png_api;

#define PNG_CASES 1500
#define PNG_MAX_DIM 300

// --- encoder (always the rebuilt libpng) -------------------------------------

typedef struct {
  uint8_t *p;
  size_t n, cap;
} png_buf;

static void png_buf_write(png_structp png, png_bytep data, png_size_t len) {
  png_buf *b = png_get_io_ptr(png);
  if (b->n + len > b->cap) {
    b->cap = (b->n + len) * 2;
    b->p = realloc(b->p, b->cap);
  }
  memcpy(b->p + b->n, data, len);
  b->n += len;
}
static void png_buf_flush(png_structp png) { (void)png; }

static void png_enc_error(png_structp png, png_const_charp msg) {
  (void)msg;
  png_longjmp(png, 1);
}

// Write case i into *out (malloc'd). Drives the shared RNG, so both tests must
// call it in the same order. Returns the PNG size.
static inline size_t png_gen_case(int i, uint8_t **out) {
  static const int depths[5][5] = {{1, 2, 4, 8, 16}, {8, 16}, {1, 2, 4, 8}, {8, 16}, {8, 16}};
  static const int ndepths[5] = {5, 2, 4, 2, 2};
  static const int types[5] = {PNG_COLOR_TYPE_GRAY, PNG_COLOR_TYPE_RGB, PNG_COLOR_TYPE_PALETTE,
                               PNG_COLOR_TYPE_GRAY_ALPHA, PNG_COLOR_TYPE_RGB_ALPHA};
  int t = (int)(vec_next() % 5);
  int depth = depths[t][vec_next() % ndepths[t]];
  int type = types[t];
  png_uint_32 w = 1 + vec_next() % (i % 10 == 0 ? PNG_MAX_DIM : 64);
  png_uint_32 h = 1 + vec_next() % (i % 10 == 0 ? PNG_MAX_DIM : 64);
  int interlace = vec_next() % 3 == 0 ? PNG_INTERLACE_ADAM7 : PNG_INTERLACE_NONE;
  int trns = vec_next() % 3 == 0, gama = vec_next() % 3 == 0, text = vec_next() % 4 == 0;
  int level = (int)(vec_next() % 10), filter = (int)(vec_next() % 6);
  png_buf b = {malloc(4096), 0, 4096};
  png_structp png = png_create_write_struct(PNG_LIBPNG_VER_STRING, NULL, png_enc_error, NULL);
  png_infop info = png_create_info_struct(png);
  int channels = type == PNG_COLOR_TYPE_RGB ? 3 : type == PNG_COLOR_TYPE_RGB_ALPHA ? 4
               : type == PNG_COLOR_TYPE_GRAY_ALPHA ? 2 : 1;
  size_t rowbytes = ((size_t)w * channels * depth + 7) / 8;
  uint8_t *pix = malloc(rowbytes * h);
  png_bytep *rows = malloc(h * sizeof *rows);
  // Smooth gradients with noise, so filters and compression both matter.
  for (png_uint_32 y = 0; y < h; y++) {
    rows[y] = pix + y * rowbytes;
    for (size_t x = 0; x < rowbytes; x++)
      rows[y][x] = (uint8_t)(x * 3 + y * 5 + (vec_next() % 4 == 0 ? vec_next() : 0));
  }
  if (setjmp(png_jmpbuf(png)) == 0) {
    png_set_write_fn(png, &b, png_buf_write, png_buf_flush);
    png_set_compression_level(png, level);
    png_set_filter(png, 0, filter == 5 ? PNG_ALL_FILTERS : (PNG_FILTER_NONE << filter));
    png_set_IHDR(png, info, w, h, depth, type, interlace, PNG_COMPRESSION_TYPE_DEFAULT,
                 PNG_FILTER_TYPE_DEFAULT);
    if (type == PNG_COLOR_TYPE_PALETTE) {
      png_color pal[256];
      int n = 1 + (int)(vec_next() % (1 << depth));
      for (int k = 0; k < n; k++) {
        pal[k].red = (png_byte)vec_next();
        pal[k].green = (png_byte)vec_next();
        pal[k].blue = (png_byte)vec_next();
      }
      png_set_PLTE(png, info, pal, n);
      // Indices beyond the palette are an error the decoder must report.
      if (vec_next() % 4) {
        for (png_uint_32 y = 0; y < h; y++) memset(rows[y], 0, rowbytes);
      }
      if (trns) {
        png_byte alpha[256];
        for (int k = 0; k < n; k++) alpha[k] = (png_byte)vec_next();
        png_set_tRNS(png, info, alpha, 1 + (int)(vec_next() % n), NULL);
      }
    } else if (trns && !(type & PNG_COLOR_MASK_ALPHA)) {
      png_color_16 c = {0, (png_uint_16)(vec_next() & ((1 << depth) - 1)),
                        (png_uint_16)(vec_next() & ((1 << depth) - 1)),
                        (png_uint_16)(vec_next() & ((1 << depth) - 1)),
                        (png_uint_16)(vec_next() & ((1 << depth) - 1))};
      png_set_tRNS(png, info, NULL, 0, &c);
    }
    if (gama) png_set_gAMA_fixed(png, info, 10000 + (png_fixed_point)(vec_next() % 200000));
    if (text) {
      png_text tx[2] = {{PNG_TEXT_COMPRESSION_NONE, "Title", "rs_rearmed"},
                        {PNG_TEXT_COMPRESSION_zTXt, "Comment", "differential test corpus"}};
      png_set_text(png, info, tx, 2);
    }
    png_write_info(png, info);
    png_write_image(png, rows);
    png_write_end(png, info);
  }
  png_destroy_write_struct(&png, &info);
  free(rows);
  free(pix);
  // Damage some: flip a byte (CRC or data error), truncate, or break the signature.
  uint32_t dmg = vec_next() % 10, pos = vec_next();
  if (b.n > 8) {
    if (dmg == 0) b.p[8 + pos % (b.n - 8)] ^= (uint8_t)(1 + vec_next() % 255);
    else if (dmg == 1) b.n = 8 + pos % (b.n - 8);
    else if (dmg == 2 && i % 20 == 0) b.p[pos % 8] ^= 0x20;
  }
  *out = b.p;
  return b.n;
}

// --- decoder (driven through png_api) ----------------------------------------

typedef struct {
  int stage;  // how far it got: 0 sig, 1 info, 2 update, 3 image, 4 end
  png_uint_32 w, h, w2, h2;
  int depth, type, interlace, depth2, type2;
  size_t rowbytes;
  uint32_t crc;  // over every decoded row, in order
  char msgs[512];  // "E:..." and "W:..." joined by '|'
} png_outcome;

static struct {
  const uint8_t *p;
  size_t n, off;
  jmp_buf jb;
  png_outcome *o;
} png_dec;

static void png_dec_note(char kind, const char *msg) {
  size_t l = strlen(png_dec.o->msgs);
  snprintf(png_dec.o->msgs + l, sizeof png_dec.o->msgs - l, "%s%c:%s", l ? "|" : "", kind,
           msg ? msg : "(null)");
}
// The game's libpng inflates with its own zlib 1.2.3, the rebuilt one with the
// system zlib (1.2.12). On a damaged IDAT stream the two notice the damage at
// different points, so the message is either zlib's or libpng's complaint
// about the garbage that came out. Those are recorded as one class.
static const char *const png_corrupt_idat[] = {
    "invalid block type", "invalid stored block lengths", "too many length or distance symbols",
    "invalid code lengths set", "invalid bit length repeat", "invalid code -- missing end-of-block",
    "invalid literal/lengths set", "invalid distances set", "invalid literal/length code",
    "invalid distance code", "invalid distance too far back", "incorrect header check",
    "unknown compression method", "invalid window size", "incorrect data check",
    "bad adaptive filter value", "Not enough image data", "Too much image data",
    "Extra compressed data", "Decompression error"};

static void png_dec_error(png_structp png, png_const_charp msg) {
  (void)png;
  if (png_dec.o->stage == 3 && msg)
    for (size_t k = 0; k < sizeof png_corrupt_idat / sizeof *png_corrupt_idat; k++)
      if (strcmp(msg, png_corrupt_idat[k]) == 0) msg = "<damaged IDAT>";
  png_dec_note('E', msg);
  longjmp(png_dec.jb, 1);
}
static void png_dec_warning(png_structp png, png_const_charp msg) {
  (void)png;
  png_dec_note('W', msg);
}
static void png_dec_read(png_structp png, png_bytep data, png_size_t len) {
  (void)png;
  if (len > png_dec.n - png_dec.off) {
    png_dec_note('E', "<eof>");
    longjmp(png_dec.jb, 1);
  }
  memcpy(data, png_dec.p + png_dec.off, len);
  png_dec.off += len;
}

// game_pipeline: 1 = the transforms and png_read_image the game uses,
// 0 = no transforms, png_read_row per row.
static inline void png_decode(const png_api *api, const uint8_t *p, size_t n, int game_pipeline,
                              png_outcome *o) {
  memset(o, 0, sizeof *o);
  png_dec.p = p;
  png_dec.n = n;
  png_dec.off = 0;
  png_dec.o = o;
  if (n < 8 || api->sig_cmp(p, 0, 8) != 0) {
    png_dec_note('E', "<signature>");
    return;
  }
  png_dec.off = 8;
  png_structp png = api->create_read_struct("1.5.13", NULL, png_dec_error, png_dec_warning);
  png_infop info = api->create_info_struct(png);
  // volatile: modified between setjmp and longjmp.
  uint8_t *volatile pix = NULL;
  png_bytep *volatile rows = NULL;
  if (setjmp(png_dec.jb) == 0) {
    api->set_read_fn(png, NULL, png_dec_read);
    api->set_sig_bytes(png, 8);
    o->stage = 1;
    api->read_info(png, info);
    int compression, filter;
    api->get_IHDR(png, info, &o->w, &o->h, &o->depth, &o->type, &o->interlace, &compression,
                  &filter);
    if (game_pipeline) {
      if (o->depth == 16) api->set_strip_16(png);
      if (o->type == PNG_COLOR_TYPE_PALETTE) api->set_palette_to_rgb(png);
      if (o->depth < 8) api->set_expand_gray_1_2_4_to_8(png);
      if (api->get_valid(png, info, PNG_INFO_tRNS)) api->set_tRNS_to_alpha(png);
      if ((o->type | PNG_COLOR_MASK_ALPHA) == PNG_COLOR_TYPE_GRAY_ALPHA) api->set_gray_to_rgb(png);
      double g;
      if (api->get_gAMA(png, info, &g)) api->set_gamma(png, 2.2, g);
    }
    o->stage = 2;
    api->read_update_info(png, info);
    int il2;
    api->get_IHDR(png, info, &o->w2, &o->h2, &o->depth2, &o->type2, &il2, &compression, &filter);
    o->rowbytes = api->get_rowbytes(png, info);
    o->stage = 3;
    pix = calloc(o->h2 ? o->h2 : 1, o->rowbytes ? o->rowbytes : 1);
    rows = malloc((o->h2 ? o->h2 : 1) * sizeof *rows);
    for (png_uint_32 y = 0; y < o->h2; y++) rows[y] = pix + y * o->rowbytes;
    if (game_pipeline) {
      api->read_image(png, rows);
      o->crc = (uint32_t)crc32(0, pix, (uInt)(o->rowbytes * o->h2));
    } else {
      // Without png_set_interlace_handling an interlaced image is read pass by pass.
      png_uint_32 nrows = o->h2;
      if (o->interlace == PNG_INTERLACE_ADAM7) {
        nrows = 0;
        for (int pass = 0; pass < 7; pass++)
          if (PNG_PASS_COLS(o->w2, pass)) nrows += PNG_PASS_ROWS(o->h2, pass);
      }
      for (png_uint_32 y = 0; y < nrows; y++) {
        memset(pix, 0, o->rowbytes);
        api->read_row(png, pix, NULL);
        o->crc = (uint32_t)crc32(o->crc, pix, (uInt)o->rowbytes);
      }
    }
    o->stage = 4;
    api->read_end(png, NULL);
    o->stage = 5;
  }
  api->destroy_read_struct(&png, &info, NULL);
  free(pix);
  free(rows);
}

#endif
