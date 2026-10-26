// Stub-engine build of the app (build.sh --stub): the shell runs with the
// platform stub hooks and no image, so the link line and the bundle layout can
// be checked without the translated library. rc_image.c needs an rc_table.
#include "rc.h"
typedef struct { uint64_t vmaddr; rc_fn fn; } rc_entry;
const rc_entry rc_table[1] = {{0, 0}};
const int rc_table_len = 0;
// rc_native.c's weak reference to the generated thunk table (a weak *definition* on Mach-O).
rc_fn rc_import_thunk(uint64_t host) { (void)host; return 0; }
