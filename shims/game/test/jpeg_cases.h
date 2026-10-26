// libjpeg 8d differential corpus, shared by the x86_64 differential test
// (original game libjpeg) and the arm64 native test (third_party/jpeg-8d).
//
// Inputs are JPEGs written by the rebuilt libjpeg's encoder from vectors.h's
// RNG: grayscale, RGB/YCbCr and CMYK, any quality, baseline, progressive or
// arithmetic coded, optimized Huffman tables, restart intervals, Adobe markers, various
// sampling factors, some damaged or truncated. Each is decoded five ways:
//   0  defaults (JDCT_ISLOW, fancy upsampling)
//   1  JDCT_IFAST, 1/2 scale, no fancy upsampling, no block smoothing
//   2  JDCT_FLOAT, scale n/8 (n = 1..16), grayscale output where allowed
//   3  one-pass colour quantization to 64 colours, Floyd-Steinberg
//   4  jpeg_read_coefficients (what Scaleform's loader at 0x100d04120 calls)
// The outcome is the header, the output geometry, a CRC of all samples or
// coefficients, and the warning and error message codes.
#ifndef RS_GAME_JPEG_CASES_H
#define RS_GAME_JPEG_CASES_H

#include <setjmp.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <zlib.h>

#include "jpeglib.h"
#include "vectors.h"

// The libjpeg calls the decoder makes. Filled with the original image's
// functions in the differential test, with the rebuilt library otherwise.
typedef struct {
  struct jpeg_error_mgr *(*std_error)(struct jpeg_error_mgr *);
  void (*create_decompress)(j_decompress_ptr, int, size_t);
  int (*read_header)(j_decompress_ptr, boolean);
  boolean (*start_decompress)(j_decompress_ptr);
  JDIMENSION (*read_scanlines)(j_decompress_ptr, JSAMPARRAY, JDIMENSION);
  boolean (*finish_decompress)(j_decompress_ptr);
  void (*destroy_decompress)(j_decompress_ptr);
  jvirt_barray_ptr *(*read_coefficients)(j_decompress_ptr);
  boolean (*resync_to_restart)(j_decompress_ptr, int);
} jpeg_api;

#define JPEG_FILES 600
#define JPEG_MODES 5

// --- encoder (always the rebuilt libjpeg) ------------------------------------

static jmp_buf jpeg_enc_jb;
static void jpeg_enc_error(j_common_ptr c) {
  (void)c;
  longjmp(jpeg_enc_jb, 1);
}

// Write file i into *out (malloc'd). Drives the shared RNG, so both tests must
// call it in the same order. Returns the size (0 if the encoder refused).
static inline size_t jpeg_gen_case(int i, uint8_t **out) {
  struct jpeg_compress_struct c;
  struct jpeg_error_mgr err;
  unsigned char *volatile mem = NULL;
  unsigned long size = 0;
  int kind = (int)(vec_next() % 6);  // 0 gray, 1-4 RGB, 5 CMYK
  int comps = kind == 0 ? 1 : kind == 5 ? 4 : 3;
  JDIMENSION w = 1 + vec_next() % (i % 8 == 0 ? 400 : 80);
  JDIMENSION h = 1 + vec_next() % (i % 8 == 0 ? 300 : 80);
  int quality = (int)(vec_next() % 101), mode = (int)(vec_next() % 4);
  int restart = vec_next() % 4 == 0 ? (int)(1 + vec_next() % 8) : 0;
  int optimize = vec_next() % 2, adobe = vec_next() % 4 == 0;
  int hs = (int)(1 + vec_next() % 2), vs = (int)(1 + vec_next() % 2);
  size_t stride = (size_t)w * comps;
  JSAMPLE *pix = malloc(stride * h);
  for (JDIMENSION y = 0; y < h; y++)
    for (size_t x = 0; x < stride; x++)
      pix[y * stride + x] = (JSAMPLE)(x * 2 + y * 3 + (x / comps) * (y % 7) +
                                      (vec_next() % 5 == 0 ? vec_next() % 64 : 0));
  c.err = jpeg_std_error(&err);
  err.error_exit = jpeg_enc_error;
  jpeg_create_compress(&c);
  if (setjmp(jpeg_enc_jb) == 0) {
    jpeg_mem_dest(&c, (unsigned char **)&mem, &size);
    c.image_width = w;
    c.image_height = h;
    c.input_components = comps;
    c.in_color_space = kind == 0 ? JCS_GRAYSCALE : kind == 5 ? JCS_CMYK : JCS_RGB;
    jpeg_set_defaults(&c);
    if (kind == 4) jpeg_set_colorspace(&c, JCS_RGB);  // no YCbCr transform
    jpeg_set_quality(&c, quality, TRUE);
    if (mode == 1) jpeg_simple_progression(&c);
    if (mode == 2) c.arith_code = TRUE;
    if (mode == 3) c.dct_method = JDCT_IFAST;
    c.restart_interval = (unsigned)restart;
    c.optimize_coding = (boolean)(optimize && mode != 2);  // not with arithmetic coding
    if (adobe) c.write_Adobe_marker = TRUE;
    if (comps > 1) {
      c.comp_info[0].h_samp_factor = hs;
      c.comp_info[0].v_samp_factor = vs;
    }
    jpeg_start_compress(&c, TRUE);
    while (c.next_scanline < h) {
      JSAMPROW row = pix + c.next_scanline * stride;
      jpeg_write_scanlines(&c, &row, 1);
    }
    jpeg_finish_compress(&c);
  } else {
    size = 0;
  }
  jpeg_destroy_compress(&c);
  free(pix);
  // Damage some: flip a byte, truncate, or drop a marker byte.
  uint32_t dmg = vec_next() % 10, pos = vec_next();
  if (size > 4) {
    if (dmg == 0) mem[2 + pos % (size - 2)] ^= (uint8_t)(1 + vec_next() % 255);
    else if (dmg == 1) size = 2 + pos % (size - 2);
    else if (dmg == 2) memset(mem + 2 + pos % (size - 2), 0xff, 1);
  }
  *out = malloc(size ? size : 1);
  if (size) memcpy(*out, mem, size);
  free(mem);  // jpeg_mem_dest allocates with malloc
  return size;
}

// --- decoder (driven through jpeg_api) ---------------------------------------

typedef struct {
  int stage;  // 1 header read, 2 started, 3 all data read, 4 finished
  int header;  // read_header's return value
  JDIMENSION w, h, ow, oh;
  int comps, jcs, progressive, arith, jfif, adobe, ocomps, ocs;
  uint32_t crc;
  char msgs[256];  // "W<code>", "E<code>:<text>" joined by '|'
} jpeg_outcome;

static struct {
  const uint8_t *p;
  size_t n;
  jmp_buf jb;
  jpeg_outcome *o;
  struct jpeg_source_mgr src;
} jpeg_dec;

static void jpeg_dec_note(const char *s) {
  size_t l = strlen(jpeg_dec.o->msgs);
  snprintf(jpeg_dec.o->msgs + l, sizeof jpeg_dec.o->msgs - l, "%s%s", l ? "|" : "", s);
}
static void jpeg_dec_error(j_common_ptr c) {
  char text[JMSG_LENGTH_MAX], note[JMSG_LENGTH_MAX + 16];
  (*c->err->format_message)(c, text);  // the library's own formatter
  snprintf(note, sizeof note, "E%d:%s", c->err->msg_code, text);
  jpeg_dec_note(note);
  longjmp(jpeg_dec.jb, 1);
}
static void jpeg_dec_emit(j_common_ptr c, int level) {
  if (level != -1) return;  // trace messages
  char note[16];
  snprintf(note, sizeof note, "W%d", c->err->msg_code);
  if (strlen(jpeg_dec.o->msgs) < 200) jpeg_dec_note(note);
}
static void jpeg_src_init(j_decompress_ptr c) { (void)c; }
static void jpeg_src_term(j_decompress_ptr c) { (void)c; }
static boolean jpeg_src_fill(j_decompress_ptr c) {
  // Out of data: hand libjpeg a fake EOI, as jdatasrc.c does.
  static const JOCTET eoi[2] = {0xff, JPEG_EOI};
  jpeg_dec_note("W<eof>");
  c->src->next_input_byte = eoi;
  c->src->bytes_in_buffer = 2;
  return TRUE;
}
static void jpeg_src_skip(j_decompress_ptr c, long n) {
  struct jpeg_source_mgr *s = c->src;
  if (n <= 0) return;
  while (n > (long)s->bytes_in_buffer) {
    n -= (long)s->bytes_in_buffer;
    jpeg_src_fill(c);
  }
  s->next_input_byte += n;
  s->bytes_in_buffer -= (size_t)n;
}

static inline void jpeg_decode(const jpeg_api *api, const uint8_t *p, size_t n, int mode,
                               int scale, jpeg_outcome *o) {
  struct jpeg_decompress_struct c;
  struct jpeg_error_mgr err;
  memset(o, 0, sizeof *o);
  jpeg_dec.o = o;
  c.err = api->std_error(&err);
  err.error_exit = jpeg_dec_error;
  err.emit_message = jpeg_dec_emit;
  api->create_decompress(&c, JPEG_LIB_VERSION, sizeof c);
  JSAMPLE *volatile row = NULL;
  if (setjmp(jpeg_dec.jb) == 0) {
    jpeg_dec.src.next_input_byte = p;
    jpeg_dec.src.bytes_in_buffer = n;
    jpeg_dec.src.init_source = jpeg_src_init;
    jpeg_dec.src.fill_input_buffer = jpeg_src_fill;
    jpeg_dec.src.skip_input_data = jpeg_src_skip;
    jpeg_dec.src.resync_to_restart = api->resync_to_restart;
    jpeg_dec.src.term_source = jpeg_src_term;
    c.src = &jpeg_dec.src;
    o->header = api->read_header(&c, TRUE);
    o->stage = 1;
    o->w = c.image_width;
    o->h = c.image_height;
    o->comps = c.num_components;
    o->jcs = c.jpeg_color_space;
    o->progressive = c.progressive_mode;
    o->arith = c.arith_code;
    o->jfif = c.saw_JFIF_marker;
    o->adobe = c.saw_Adobe_marker;
    if (mode == 4) {
      jvirt_barray_ptr *coef = api->read_coefficients(&c);
      o->stage = 3;
      for (int ci = 0; ci < c.num_components; ci++) {
        jpeg_component_info *comp = c.comp_info + ci;
        for (JDIMENSION by = 0; by < comp->height_in_blocks; by++) {
          JBLOCKARRAY b = (*c.mem->access_virt_barray)((j_common_ptr)&c, coef[ci], by, 1, FALSE);
          o->crc = (uint32_t)crc32(o->crc, (const uint8_t *)b[0],
                                   (uInt)(comp->width_in_blocks * sizeof(JBLOCK)));
        }
      }
    } else {
      if (mode == 1) {
        c.dct_method = JDCT_IFAST;
        c.scale_num = 1;
        c.scale_denom = 2;
        c.do_fancy_upsampling = FALSE;
        c.do_block_smoothing = FALSE;
      } else if (mode == 2) {
        c.dct_method = JDCT_FLOAT;
        c.scale_num = (unsigned)scale;
        c.scale_denom = 8;
        if (c.jpeg_color_space == JCS_YCbCr || c.jpeg_color_space == JCS_GRAYSCALE)
          c.out_color_space = JCS_GRAYSCALE;
      } else if (mode == 3) {
        c.quantize_colors = TRUE;
        c.desired_number_of_colors = 64;
        c.dither_mode = JDITHER_FS;
      }
      api->start_decompress(&c);
      o->stage = 2;
      o->ow = c.output_width;
      o->oh = c.output_height;
      o->ocomps = c.output_components;
      o->ocs = c.out_color_space;
      size_t stride = (size_t)c.output_width * c.output_components;
      row = malloc(stride ? stride : 1);
      while (c.output_scanline < c.output_height) {
        JSAMPROW r = row;
        if (api->read_scanlines(&c, &r, 1) != 1) break;
        o->crc = (uint32_t)crc32(o->crc, row, (uInt)stride);
      }
      o->stage = 3;
      if (mode == 3) {  // the palette the quantizer chose
        for (int ci = 0; ci < c.out_color_components; ci++)
          o->crc = (uint32_t)crc32(o->crc, c.colormap[ci], (uInt)c.actual_number_of_colors);
      }
    }
    api->finish_decompress(&c);
    o->stage = 4;
  }
  api->destroy_decompress(&c);
  free(row);
}

#endif
