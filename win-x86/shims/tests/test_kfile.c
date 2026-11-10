// The file half of kernel32 (win32_kfile.c) on a folder of its own: paths resolved from two threads
// at once stay each thread's own, a find gives its handle back (FindClose, and a find with no match),
// a name with [ matches itself, a folder in both the overlay and the game folder lists both, a host
// path works in a find and only inside the guest's folders, a move out of the game folder leaves the file there, GetFullPathNameW's
// file part.
#include <fcntl.h>
#include <pthread.h>
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include "rc32.h"
#include "win32_handles.h"

#define CHECK(x) do { if (!(x)) { fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #x); exit(1); } } while (0)
#define INVALID 0xffffffffu

extern const w32_dll w32_dll_kernel32_file;
static rc_fn fn(const char *name) {
  for (const w32_dll *d = &w32_dll_kernel32_file; d; d = d->more)
    for (int k = 0; k < d->n; k++)
      if (!strcmp(d->fns[k].name, name)) return d->fns[k].fn;
  fprintf(stderr, "FAIL: no %s\n", name);
  exit(1);
}
// a kernel32 call in the import convention: arguments at esp, no return address
static uint32_t call(rc_ctx *c, const char *f, int n, const uint32_t *a) {
  uint32_t esp0 = (uint32_t)c->r[4];
  c->r[4] -= 4u * (uint32_t)n;
  for (int k = 0; k < n; k++) w32_wr32((uint32_t)c->r[4] + 4 * k, a[k]);
  fn(f)(c);
  c->r[4] = esp0;
  return (uint32_t)c->r[0];
}
static uint32_t gstr(const char *s) {
  uint32_t g = w32_galloc((uint32_t)strlen(s) + 1);
  strcpy((char *)RC32_H(g), s);
  return g;
}
static uint32_t gwstr(const char *s) {
  uint32_t g = w32_galloc(2 * (uint32_t)strlen(s) + 2);
  for (size_t k = 0;; k++) {
    ((uint16_t *)RC32_H(g))[k] = (uint8_t)s[k];
    if (!s[k]) break;
  }
  return g;
}
static void put(const char *path) {
  int fd = open(path, O_WRONLY | O_CREAT | O_TRUNC, 0644);
  CHECK(fd >= 0);
  CHECK(write(fd, "x", 1) == 1);
  close(fd);
}

static char root[1024];
static uint32_t find_data;
// the names a find returns, each between slashes in out
static int find_all(rc_ctx *c, const char *pattern, char *out, size_t n) {
  out[0] = 0;
  uint32_t a[2] = {gstr(pattern), find_data};
  uint32_t h = call(c, "FindFirstFileA", 2, a);
  if (h == INVALID) return 0;
  int k = 0;
  uint32_t nx[2] = {h, find_data};
  do {
    snprintf(out + strlen(out), n - strlen(out), "/%s/", (char *)RC32_H(find_data + 44));
    k++;
  } while (call(c, "FindNextFileA", 2, nx));
  CHECK(call(c, "FindClose", 1, &h));
  return k;
}

// two threads open files through different paths; each must get its own
#define PATHS 4
#define ROUNDS 3000
static atomic_int wrong;
static void *opener(void *arg) {
  rc_ctx *c = rc32_thread_ctx();
  int me = (int)(intptr_t)arg;
  char win[256];
  snprintf(win, sizeof win, "C:\\Rocksmith2014\\t%d\\sub%d\\deep%d\\f%d.txt", me, me, me, me);
  uint32_t a[1] = {gstr(win)};
  for (int k = 0; k < ROUNDS; k++)
    if (call(c, "GetFileAttributesA", 1, a) == INVALID) atomic_fetch_add(&wrong, 1);
  return 0;
}

int main(void) {
  const char *tmp = getenv("TMPDIR");
  snprintf(root, sizeof root, "%s/test_kfile_%d", tmp && *tmp ? tmp : "/tmp", (int)getpid());
  char game[1100], user[1100], p[1200];
  snprintf(game, sizeof game, "%s/game", root);
  snprintf(user, sizeof user, "%s/user", root);
  mkdir(root, 0755);
  mkdir(game, 0755);
  mkdir(user, 0755);
  setenv("LP_GAME_DIR", game, 1);
  setenv("LP_USER_DIR", user, 1);
  CHECK(rc32_mem_init() == 0);
  rc_ctx *c = rc32_thread_ctx();
  find_data = w32_galloc(600);

  // 1. paths from several threads
  for (int k = 0; k < PATHS; k++) {
    snprintf(p, sizeof p, "%s/t%d", game, k); mkdir(p, 0755);
    snprintf(p, sizeof p, "%s/t%d/sub%d", game, k, k); mkdir(p, 0755);
    snprintf(p, sizeof p, "%s/t%d/sub%d/deep%d", game, k, k, k); mkdir(p, 0755);
    snprintf(p, sizeof p, "%s/t%d/sub%d/deep%d/f%d.txt", game, k, k, k, k); put(p);
  }
  pthread_t t[PATHS];
  for (intptr_t k = 0; k < PATHS; k++) pthread_create(&t[k], 0, opener, (void *)k);
  for (int k = 0; k < PATHS; k++) pthread_join(t[k], 0);
  printf("%d lookups on %d threads: %d wrong\n", PATHS * ROUNDS, PATHS, atomic_load(&wrong));
  CHECK(atomic_load(&wrong) == 0);

  // 2. a find gives its handle back, hit or miss: far more of them than there are handles
  snprintf(p, sizeof p, "%s/one.txt", game); put(p);
  for (int k = 0; k < 20000; k++) {
    char out[256];
    CHECK(find_all(c, k & 1 ? "C:\\Rocksmith2014\\one.txt" : "C:\\Rocksmith2014\\nothing.txt", out, sizeof out) == (k & 1));
  }
  w32_obj *o;
  uint32_t h = w32_handle_new(H_EVENT, &o);
  CHECK(h && o);  // the table did not fill
  w32_handle_close(h);

  // 3. [ is a character
  char out[1024];
  snprintf(p, sizeof p, "%s/[Band] Song_p.psarc", game); put(p);
  CHECK(find_all(c, "C:\\Rocksmith2014\\[Band] Song_p.psarc", out, sizeof out) == 1);
  CHECK(find_all(c, "C:\\Rocksmith2014\\*Song_?.psarc", out, sizeof out) == 1);
  CHECK(strstr(out, "/[Band] Song_p.psarc/"));

  // 4. a folder in both places lists both, the overlay's copy once
  snprintf(p, sizeof p, "%s/both", game); mkdir(p, 0755);
  snprintf(p, sizeof p, "%s/both/g.txt", game); put(p);
  snprintf(p, sizeof p, "%s/both/same.txt", game); put(p);
  uint32_t fa[2] = {gstr("C:\\Rocksmith2014\\both\\o.txt"), 0};
  uint32_t cf[7] = {fa[0], 0x40000000u, 0, 0, 2, 0x80, 0};  // GENERIC_WRITE, CREATE_ALWAYS
  uint32_t f = call(c, "CreateFileA", 7, cf);
  CHECK(f != INVALID);
  CHECK(call(c, "CloseHandle", 1, &f));
  cf[0] = gstr("C:\\Rocksmith2014\\both\\same.txt");
  f = call(c, "CreateFileA", 7, cf);
  CHECK(f != INVALID);
  CHECK(call(c, "CloseHandle", 1, &f));
  int n = find_all(c, "C:\\Rocksmith2014\\both\\*.txt", out, sizeof out);
  printf("both: %s\n", out);
  CHECK(n == 3 && strstr(out, "/g.txt/") && strstr(out, "/o.txt/") && strstr(out, "/same.txt/"));
  CHECK(find_all(c, "C:\\Rocksmith2014\\both\\g.txt", out, sizeof out) == 1);  // only in the game folder

  // 5. a host path in a find, as map_path takes it
  snprintf(p, sizeof p, "%s/one.txt", game);
  CHECK(find_all(c, p, out, sizeof out) == 1);

  // 5b. a host path outside the guest's folders is not one: it is the Windows path from the drive's
  // root, in the user folder, and ".." does not lead out of a folder the guest has
  snprintf(p, sizeof p, "%s/outside.txt", root); put(p);
  uint32_t oa[1] = {gstr(p)};
  CHECK(call(c, "GetFileAttributesA", 1, oa) == INVALID);
  CHECK(find_all(c, p, out, sizeof out) == 0);
  snprintf(p, sizeof p, "%s/../outside.txt", game);
  oa[0] = gstr(p);
  CHECK(call(c, "GetFileAttributesA", 1, oa) == INVALID);
  uint32_t ocf[7] = {gstr("/made.txt"), 0x40000000u, 0, 0, 2, 0x80, 0};  // GENERIC_WRITE, CREATE_ALWAYS
  f = call(c, "CreateFileA", 7, ocf);
  CHECK(f != INVALID);
  CHECK(call(c, "CloseHandle", 1, &f));
  struct stat ost;
  snprintf(p, sizeof p, "%s/c/made.txt", user);
  CHECK(!stat(p, &ost) && stat("/made.txt", &ost));
  // a folder the seam handed out (w32_host_root) is the guest's from then on
  char extra[1100];
  snprintf(extra, sizeof extra, "%s/extra", root);
  mkdir(extra, 0755);
  snprintf(p, sizeof p, "%s/in.txt", extra); put(p);
  oa[0] = gstr(p);
  CHECK(call(c, "GetFileAttributesA", 1, oa) == INVALID);
  w32_host_root(extra);
  CHECK(call(c, "GetFileAttributesA", 1, oa) != INVALID);
  snprintf(p, sizeof p, "%s/./sub/../in.txt", extra);
  oa[0] = gstr(p);
  CHECK(call(c, "GetFileAttributesA", 1, oa) != INVALID);

  // 6. a move out of the game folder leaves the game folder as it is
  snprintf(p, sizeof p, "%s/move.txt", game); put(p);
  uint32_t mv[2] = {gstr("C:\\Rocksmith2014\\move.txt"), gstr("C:\\Rocksmith2014\\moved.txt")};
  CHECK(call(c, "MoveFileA", 2, mv));
  struct stat st;
  CHECK(!stat(p, &st));
  snprintf(p, sizeof p, "%s/game/moved.txt", user);
  CHECK(!stat(p, &st));

  // 7. GetFullPathNameW: the file part is the name
  uint32_t buf = w32_galloc(1024), part = w32_galloc(4);
  uint32_t gf[4] = {gwstr("C:\\dir\\file.txt"), 500, buf, part};
  CHECK(call(c, "GetFullPathNameW", 4, gf) == 15);
  CHECK(w32_rd32(part) == buf + 2 * 7);

  char cmd[1200];
  snprintf(cmd, sizeof cmd, "rm -rf '%s'", root);
  CHECK(!system(cmd));
  printf("ok\n");
  return 0;
}
