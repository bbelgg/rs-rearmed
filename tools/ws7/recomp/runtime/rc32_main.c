// 32-bit guests: the main image, the tree generated without --module (rc32.h). Apart from
// rc32_image.c so a process made of --module images only (a test of the DLLs alone) links
// without one.
#include "rc32.h"

int rc32_image_load(const char *path) {
  if (!rc32_image_add(&rc32_mod, path)) return -1;
  return rc32_images_bind();
}
