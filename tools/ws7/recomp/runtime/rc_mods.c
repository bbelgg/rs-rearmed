// The mods folder, the same in both editions (app/rc_main.c, win-x86/app/lp_main.c): every .dylib of
// <home>/mods, in name order, or RS_MODS_PATH (a folder or one library); RS_LOAD_MODS=0 loads none. A mod's
// constructor registers its hooks (rc_hook.h). A library that fails to load is logged and skipped.
#include <dirent.h>
#include <dlfcn.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>

#include "rc_hook.h"
#include "rc_mid.h"

const char *rc_mods_where(const char *dflt) {
  const char *e = getenv("RS_LOAD_MODS");
  if (e && *e == '0') return 0;
  e = getenv("RS_MODS_PATH");
  return e && *e ? e : dflt && *dflt ? dflt : 0;
}

static int dylib_name(const struct dirent *d) {
  size_t n = strlen(d->d_name);
  return d->d_name[0] != '.' && n > 6 && !strcmp(d->d_name + n - 6, ".dylib");
}

int rc_mods_count(const char *where) {
  struct dirent **names;
  int n = scandir(where, &names, dylib_name, alphasort);
  if (n < 0) return access(where, R_OK) == 0;
  for (int i = 0; i < n; i++) free(names[i]);
  free(names);
  return n;
}

#ifdef RC_GUEST32
// A library built without rc_mod.h has the Mac's addresses and offsets compiled in and reads a pointer as 8
// bytes: on this game it would misread memory without a word, so nothing of it runs (its constructor would).
// rc_mod.h defines rc_mod_abi; a symbol's name is in the library's string table, however many slices it has.
static int built_for_every_target(const char *path) {
  static const char mark[] = "_rc_mod_abi";
  int fd = open(path, O_RDONLY), found = 0;
  struct stat st;
  if (fd >= 0 && fstat(fd, &st) == 0 && st.st_size > 0) {
    void *m = mmap(0, (size_t)st.st_size, PROT_READ, MAP_PRIVATE, fd, 0);
    if (m != MAP_FAILED) {
      found = memmem(m, (size_t)st.st_size, mark, sizeof mark) != 0;
      munmap(m, (size_t)st.st_size);
    }
  }
  if (fd >= 0) close(fd);
  return found;
}
#endif

static void load_one(const char *path, const char *shown) {
#ifdef RC_GUEST32
  if (!built_for_every_target(path)) {
    fprintf(stderr, "[rc] mod not loaded: %s: built for the Mac alone (rebuild it with rc_mod.h for %s)\n", shown,
            rc_target());
    return;
  }
#endif
  if (dlopen(path, RTLD_NOW | RTLD_GLOBAL)) fprintf(stderr, "[rc] mod loaded: %s\n", shown);
  else fprintf(stderr, "[rc] dlopen %s: %s\n", path, dlerror());
}

void rc_mods_load(const char *where) {
  struct dirent **names;
  int n = scandir(where, &names, dylib_name, alphasort);
  if (n < 0) {
    if (access(where, R_OK) == 0) load_one(where, where);
    rc_edits_fix();
    return;
  }
  for (int i = 0; i < n; i++) {
    char p[1280];
    snprintf(p, sizeof p, "%s/%s", where, names[i]->d_name);
    load_one(p, names[i]->d_name);
    free(names[i]);
  }
  free(names);
  // the mods' constructors have run: the edit groups they switched on are the game's for this run (rc_mid.h)
  rc_edits_fix();
}
