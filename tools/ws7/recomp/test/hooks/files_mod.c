// A mod for the game runs of the file provider (runtime/rc_file.h; docs/*/TESTING.md): it serves every song and
// game archive (*.psarc) the game opens for reading from the file itself, so all of the game's archive reads go the
// provider's way and a run that reaches the note highway has read its archives, audio and song files through it.
// It logs each archive once and a count now and then:  files_mod: 12 archives, 3400 reads, 512 MB
#include <fcntl.h>
#include <stdatomic.h>
#include <string.h>
#include <strings.h>
#include <unistd.h>

#include "rc_mod.h"

static _Atomic long opens, reads, bytes, next_report = 2000;

static int is_archive(const char *path) {
  size_t n = strlen(path);
  return n > 6 && !strcasecmp(path + n - 6, ".psarc");
}
static void *fm_open(void *user, const char *path, rc_file_info *info) {
  if (!is_archive(path)) return 0;
  struct stat st;
  int fd = open(path, O_RDONLY | O_CLOEXEC);
  if (fd < 0) return 0;  // not there: the system says so
  if (fstat(fd, &st) || !S_ISREG(st.st_mode)) {
    close(fd);
    return 0;
  }
  info->size = st.st_size;
  info->mtime = st.st_mtimespec.tv_sec;
  long n = atomic_fetch_add(&opens, 1) + 1;
  if (n <= 40 || n % 500 == 0) {
    const char *base = strrchr(path, '/');
    fprintf(stderr, "files_mod: serving %s (%lld bytes), open %ld\n", base ? base + 1 : path, (long long)st.st_size, n);
  }
  return (void *)(intptr_t)(fd + 1);
}
static int64_t fm_read(void *user, void *file, void *buf, uint64_t n, int64_t off) {
  ssize_t r = pread((int)(intptr_t)file - 1, buf, n, off);
  if (r > 0) {
    long total = atomic_fetch_add(&bytes, r) + r, k = atomic_fetch_add(&reads, 1) + 1;
    long at = atomic_load(&next_report);
    if (k >= at && atomic_compare_exchange_strong(&next_report, &at, at * 2))
      fprintf(stderr, "files_mod: %ld archives opened, %ld reads, %ld MB\n", atomic_load(&opens), k, total >> 20);
  }
  return r;
}
static void fm_close(void *user, void *file) { close((int)(intptr_t)file - 1); }
static int fm_stat(void *user, const char *path, rc_file_info *info) {
  struct stat st;
  if (!is_archive(path) || stat(path, &st) || !S_ISREG(st.st_mode)) return 0;
  info->size = st.st_size;
  info->mtime = st.st_mtimespec.tv_sec;
  return 1;
}

__attribute__((constructor)) static void init(void) {
  static const rc_file_provider p = {.open = fm_open, .read = fm_read, .close = fm_close, .stat = fm_stat};
  if (!rc_file_provide(&p)) fprintf(stderr, "files_mod: serving every .psarc the game opens (%s)\n", rc_target());
}
