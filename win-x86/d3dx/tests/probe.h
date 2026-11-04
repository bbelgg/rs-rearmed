// d3dx_probe: several translated DLLs in one process (probe_images.c, on the runtime's registry of
// images), a minimal kernel32 / gdi32 / advapi32 for them (probe_win32.c), and the checks (d3dx_probe.c).
#ifndef D3DX_PROBE_H
#define D3DX_PROBE_H

#include <stdint.h>

#include "rc32.h"
#include "win32_guestobj.h"

typedef struct {
  const char *name;                 // "msvcr100.dll"
  const char *alias;                // another name its exports answer to ("msvcrt.dll"), or 0
  rc32_module *mod;
  uint32_t base, entry;
} probe_image;

enum { IMG_CRT, IMG_D3DX, IMG_DCOMP, IMG_N };
extern probe_image probe_img[IMG_N];
extern int probe_verbose;

// Map the image file at its preferred base (no imports bound yet). 0 on success.
int probe_image_map(probe_image *m, const char *path);
// Bind every mapped image's imports: to an export of a mapped image, or to the probe's Win32.
void probe_bind(void);
// Run DllMain(DLL_PROCESS_ATTACH) once, after the DLLs it imports from; 1 on success.
uint32_t probe_attach(probe_image *m);
// The image a DLL name (any case, with or without ".dll") or a base address names, or 0.
probe_image *probe_image_named(const char *dll);
probe_image *probe_image_at(uint32_t base);
// Address of a named export, 0 when there is none (or it is a forwarder).
uint32_t probe_export(const probe_image *m, const char *name);

// probe_win32.c: the Win32 import named "dll!fn" (0 when the probe has none).
rc_fn probe_win32(const char *name);

#endif
