// Files a mod serves (runtime/rc_file.h), without a game: the functions both editions' file layers call, on a folder
// of its own. Before a provider registers they are the system's; after, a served file is opened, sized, read, seeked,
// duplicated and closed as a descriptor and as a FILE, a folder lists the added name once and not the hidden one, and
// every other file is untouched.
#include <errno.h>
#include <fcntl.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "rc_file.h"

#define CHECK(x) do { if (!(x)) { fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #x); exit(1); } } while (0)
#define SIZE 300000

static char dir[1024], served[1100], real[1100];
static int opens, closes, fail_reads;
static uint8_t pat(int64_t i) { return (uint8_t)(i * 13 + 5); }
static void *p_open(void *user, const char *path, rc_file_info *info) {
  if (strcmp(path, served)) return 0;
  *info = (rc_file_info){SIZE, 1234567890};
  opens++;
  return served;
}
static int64_t p_read(void *user, void *file, void *buf, uint64_t n, int64_t off) {
  CHECK(file == served && off >= 0 && off + (int64_t)n <= SIZE);
  if (fail_reads) return -1;
  if (n > 1000) n = 1000;
  for (uint64_t k = 0; k < n; k++) ((uint8_t *)buf)[k] = pat(off + (int64_t)k);
  return (int64_t)n;
}
static void p_close(void *user, void *file) { closes++; }
static int p_stat(void *user, const char *path, rc_file_info *info) {
  if (strcmp(path, served)) return 0;
  *info = (rc_file_info){SIZE, 1234567890};
  return 1;
}
static void p_list(void *user, const char *d, rc_file_add add, void *ctx) {
  if (strcmp(d, dir)) return;
  add(ctx, "served.bin");
  add(ctx, "served.bin");  // twice: listed once
  add(ctx, "real.bin");    // the folder has it: listed once
  add(ctx, "a/b");         // not a name
}
static int p_hide(void *user, const char *d, const char *name) { return !strcmp(d, dir) && !strcmp(name, "hidden.bin"); }

static int ok_pat(const uint8_t *b, size_t n, int64_t off) {
  for (size_t k = 0; k < n; k++)
    if (b[k] != pat(off + (int64_t)k)) return 0;
  return 1;
}
// the folder's names, each between slashes
static int list(char *out, size_t n) {
  out[0] = 0;
  DIR *d = rc_file_opendir(dir);
  CHECK(d);
  int k = 0;
  for (struct dirent *e; (e = rc_file_readdir(d));) {
    if (e->d_name[0] == '.') continue;
    snprintf(out + strlen(out), n - strlen(out), "/%s/", e->d_name);
    k++;
  }
  CHECK(!rc_file_closedir(d));
  return k;
}

int main(void) {
  const char *tmp = getenv("TMPDIR");
  char p[1200], out[512];
  snprintf(p, sizeof p, "%s/test_files_%d", tmp && *tmp ? tmp : "/tmp", (int)getpid());
  CHECK(!mkdir(p, 0755) && realpath(p, dir));
  snprintf(served, sizeof served, "%s/served.bin", dir);
  snprintf(real, sizeof real, "%s/real.bin", dir);
  snprintf(p, sizeof p, "%s/hidden.bin", dir);
  for (int k = 0; k < 2; k++) {
    int fd = open(k ? p : real, O_WRONLY | O_CREAT, 0644);
    CHECK(fd >= 0 && write(fd, "real", 4) == 4 && !close(fd));
  }
  static uint8_t buf[SIZE];
  struct stat st;

  // without a provider: the system's
  CHECK(rc_file_open(served, O_RDONLY) < 0 && errno == ENOENT && rc_file_stat(served, &st) < 0);
  CHECK(!rc_file_fopen(served, "rb"));
  CHECK(list(out, sizeof out) == 2 && strstr(out, "/hidden.bin/"));
  static const rc_file_provider incomplete = {.open = p_open};
  CHECK(rc_file_provide(&incomplete) < 0 && !rc_file_nproviders);

  static const rc_file_provider prov = {p_open, p_read, p_close, p_stat, p_list, p_hide, 0};
  CHECK(rc_file_provide(&prov) == 0);

  // a descriptor
  CHECK(!rc_file_stat(served, &st) && st.st_size == SIZE && S_ISREG(st.st_mode) && st.st_mtimespec.tv_sec == 1234567890);
  CHECK(opens == 0);  // stat answered
  int fd = rc_file_open(served, O_RDONLY);
  CHECK(fd >= 0 && opens == 1 && rc_file_served(fd));
  CHECK(!rc_file_fstat(fd, &st) && st.st_size == SIZE);
  CHECK(rc_file_read(fd, buf, 5000) == 5000 && ok_pat(buf, 5000, 0));
  CHECK(rc_file_lseek(fd, 0, SEEK_CUR) == 5000);
  CHECK(rc_file_pread(fd, buf, 70, 123456) == 70 && ok_pat(buf, 70, 123456));
  CHECK(rc_file_lseek(fd, 0, SEEK_CUR) == 5000);  // pread leaves the position
  CHECK(rc_file_lseek(fd, -10, SEEK_END) == SIZE - 10);
  CHECK(rc_file_read(fd, buf, 100) == 10 && ok_pat(buf, 10, SIZE - 10) && rc_file_read(fd, buf, 100) == 0);
  CHECK(rc_file_lseek(fd, -1, SEEK_SET) < 0 && errno == EINVAL);
  int d2 = rc_file_dup(fd);
  CHECK(d2 >= 0 && d2 != fd && rc_file_served(d2));
  CHECK(rc_file_lseek(d2, 0, SEEK_CUR) == SIZE && rc_file_lseek(d2, 7, SEEK_SET) == 7 && rc_file_lseek(fd, 0, SEEK_CUR) == 7);
  CHECK(!rc_file_close(fd) && closes == 0 && !rc_file_served(fd));
  CHECK(rc_file_read(d2, buf, 3) == 3 && ok_pat(buf, 3, 7));
  fail_reads = 1;
  CHECK(rc_file_read(d2, buf, 3) < 0 && errno == EIO);
  fail_reads = 0;
  CHECK(!rc_file_close(d2) && closes == 1);
  // the number is the system's again: a file opened on it is not the served one
  int r = rc_file_open(real, O_RDONLY);
  CHECK(r >= 0 && !rc_file_served(r) && rc_file_read(r, buf, 10) == 4 && !memcmp(buf, "real", 4) && !rc_file_close(r));
  // for writing, and other paths: not the provider's
  CHECK(rc_file_open(served, O_WRONLY) < 0 && rc_file_open(served, O_RDWR | O_CREAT, 0644) >= 0 && opens == 1);
  CHECK(!unlink(served));

  // a FILE
  FILE *f = rc_file_fopen(served, "rb");
  CHECK(f && opens == 2 && rc_file_served(fileno(f)));
  CHECK(!rc_file_fstat(fileno(f), &st) && st.st_size == SIZE);
  CHECK(!fseek(f, 0, SEEK_END) && ftell(f) == SIZE && !fseeko(f, 0, SEEK_SET) && ftello(f) == 0);
  CHECK(fread(buf, 1, SIZE, f) == SIZE && ok_pat(buf, SIZE, 0) && fread(buf, 1, 1, f) == 0 && feof(f));
  rewind(f);
  CHECK(!fseek(f, 250000, SEEK_SET) && fread(buf, 1, 4096, f) == 4096 && ok_pat(buf, 4096, 250000) && ftell(f) == 254096);
  CHECK(!fseek(f, -96, SEEK_CUR) && fgetc(f) == pat(254000));
  CHECK(!fclose(f) && closes == 2);
  CHECK(rc_file_fopen(served, "wb") != 0 && opens == 2);  // writing: the system's (it makes the file)
  CHECK(!unlink(served));
  f = rc_file_fopen(real, "rb");
  CHECK(f && !rc_file_served(fileno(f)) && fread(buf, 1, 10, f) == 4 && !fclose(f));

  // the folder
  int n = list(out, sizeof out);
  printf("listing: %s\n", out);
  CHECK(n == 2 && strstr(out, "/real.bin/") && strstr(out, "/served.bin/") && !strstr(out, "hidden"));
  CHECK(strstr(out, "/real.bin/") < strstr(out, "/served.bin/"));
  DIR *other = rc_file_opendir("/usr");  // another folder: as it is
  CHECK(other && rc_file_readdir(other) && !rc_file_closedir(other));
  CHECK(!rc_file_opendir("/nonexistent-folder"));

  // many opens and closes leave nothing behind
  for (int k = 0; k < 5000; k++) {
    fd = rc_file_open(served, O_RDONLY);
    CHECK(fd >= 0 && rc_file_pread(fd, buf, 16, k) == 16 && buf[0] == pat(k) && !rc_file_close(fd));
  }
  CHECK(opens == closes);

  snprintf(p, sizeof p, "rm -rf '%s'", dir);
  CHECK(!system(p));
  printf("ok\n");
  return 0;
}
