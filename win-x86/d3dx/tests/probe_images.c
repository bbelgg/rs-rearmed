// Several translated images in one process, on the runtime's registry (rc32_image.c, probe.h).
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

#include "probe.h"
#include "win32_shims.h"

extern rc32_module msvcr100_rc32_mod, d3dx9_42_rc32_mod, d3dcompiler_42_rc32_mod;

probe_image probe_img[IMG_N] = {
    [IMG_CRT] = {"msvcr100.dll", "msvcrt.dll", &msvcr100_rc32_mod},
    [IMG_D3DX] = {"d3dx9_42.dll", 0, &d3dx9_42_rc32_mod},
    [IMG_DCOMP] = {"d3dcompiler_42.dll", 0, &d3dcompiler_42_rc32_mod},
};
int probe_verbose;

// What the D3D9 layer and win32_guestobj.c take from the seam, which the probe does not link
int w32_log;
int lp_frame_on;
void lp_frame_event(int kind, uint64_t ns, uint64_t amount) { (void)kind; (void)ns; (void)amount; }
void lp_frame_present(void) {}
uint64_t lp_frame_now(void) { return 0; }
void w32_display_size(int32_t *w, int32_t *h) { *w = 1920; *h = 1080; }

int probe_image_map(probe_image *m, const char *path) {
  if (!rc32_image_add(m->mod, path)) { fprintf(stderr, "cannot map %s\n", path); return -1; }
  m->base = rc32_image_base(m->name);
  m->entry = m->base + w32_rd32(m->base + w32_rd32(m->base + 0x3c) + 0x28);
  return 0;
}

uint32_t probe_export(const probe_image *m, const char *name) { return rc32_image_export(m->base, name); }

probe_image *probe_image_named(const char *dll) {
  const char *s = strrchr(dll, '\\');
  uint32_t b = rc32_image_base(s ? s + 1 : dll);
  return b ? probe_image_at(b) : 0;
}

probe_image *probe_image_at(uint32_t base) {
  for (int k = 0; k < IMG_N; k++)
    if (probe_img[k].base && probe_img[k].base == base) return &probe_img[k];
  return 0;
}

// An import no image exports, called through its thunk (arguments at esp, no return address).
static void host_import(rc_ctx *c, const rc32_module *m, int k) {
  const char *nm = m->imports[k].name;
  rc_fn f = probe_win32(nm);
  if (!f) {
    fprintf(stderr, "d3dx_probe: %s import missing: %s\n", m->name, nm);
    exit(3);
  }
  f(c);
}

void probe_bind(void) {
  rc32_host_import = host_import;
  for (int k = 0; k < IMG_N; k++)
    if (probe_img[k].alias) rc32_image_alias(probe_img[k].name, probe_img[k].alias);
  if (rc32_images_bind()) { fprintf(stderr, "cannot bind the imports\n"); exit(2); }
}

uint32_t probe_attach(probe_image *m) {
  int r = rc32_image_start(rc32_thread_ctx(), m->base);
  if (probe_verbose || r) fprintf(stderr, "%s: DllMain(PROCESS_ATTACH) -> %s\n", m->name, r ? "failed" : "ok");
  return !r;
}
