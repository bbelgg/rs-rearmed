// Files a mod serves to the game (rc_file.h). Both editions' file layers call these in place of the system's
// functions: the Mac's import thunks (thunks/gen_thunks.py: FILE_FUNCS), Learn & Play's Win32 seam
// (win-x86/shims/win32_kfile.c, win32_ksync.c).
//
// A served file is a real descriptor, opened on /dev/null, and a record beside it: what the system does with a
// descriptor (its number, dup, close, the open-file limit) holds for it, and read, seek and fstat are answered here.
// RS_FILE_LOG=1 logs each served open and each listing a provider changed.
#include "rc_file.h"

#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <pthread.h>
#include <stdarg.h>
#include <stdatomic.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#define MAX_PROVIDERS 8
#define MAX_FD 16384  // above the open-file limit both editions raise to (rc_nofile.h: 10240)

int rc_file_nproviders;
static rc_file_provider providers[MAX_PROVIDERS];
static pthread_mutex_t mu = PTHREAD_MUTEX_INITIALIZER;  // registration, the descriptor table's writes, the folders

// One served file: every descriptor on it (dup) shares the position, as the system's do
typedef struct vfile {
  const rc_file_provider *p;
  void *file;
  rc_file_info info;
  _Atomic int64_t pos;
  int refs;  // descriptors, under mu
} vfile;
static _Atomic(vfile *) vfds[MAX_FD];

static int flog(void) {
  static int on = -1;
  if (on < 0) {
    const char *e = getenv("RS_FILE_LOG");
    on = e && *e && *e != '0';
  }
  return on;
}

int rc_file_provide(const rc_file_provider *p) {
  if (!p || !p->open || !p->read || !p->close) return -1;
  pthread_mutex_lock(&mu);
  int n = rc_file_nproviders, r = -1;
  if (n < MAX_PROVIDERS) {
    providers[n] = *p;
    __atomic_store_n(&rc_file_nproviders, n + 1, __ATOMIC_RELEASE);
    r = 0;
  }
  pthread_mutex_unlock(&mu);
  if (r) fprintf(stderr, "[rc] file provider not registered: %d are\n", MAX_PROVIDERS);
  return r;
}

static int nprov(void) { return __atomic_load_n(&rc_file_nproviders, __ATOMIC_ACQUIRE); }

// path made absolute (not resolved: no link is followed and no "." or ".." removed)
static const char *absolute(const char *path, char *buf, size_t n) {
  if (path[0] == '/') return path;
  char cwd[PATH_MAX];
  if (!getcwd(cwd, sizeof cwd)) return path;
  snprintf(buf, n, "%s/%s", cwd, path);
  return buf;
}

static vfile *vf_of(int fd) { return fd >= 0 && fd < MAX_FD ? atomic_load(&vfds[fd]) : 0; }
int rc_file_served(int fd) { return nprov() && vf_of(fd) != 0; }

// The record of fd is dropped: with its last descriptor the provider closes the file
static void vf_drop(int fd) {
  pthread_mutex_lock(&mu);
  vfile *v = atomic_exchange(&vfds[fd], 0);
  int last = v && --v->refs == 0;
  pthread_mutex_unlock(&mu);
  if (last) {
    v->p->close(v->p->user, v->file);
    free(v);
  }
}

// A descriptor the system just gave out has no record: one left by a close that went past rc_file_close is dropped
static int fresh(int fd) {
  if (fd >= 0 && fd < MAX_FD && atomic_load(&vfds[fd])) vf_drop(fd);
  return fd;
}

static int vf_open(const char *path) {
  char buf[PATH_MAX];
  const char *abs = absolute(path, buf, sizeof buf);
  int n = nprov();
  for (int i = 0; i < n; i++) {
    rc_file_info info = {0, 0};
    void *file = providers[i].open(providers[i].user, abs, &info);
    if (!file) continue;
    int fd = fresh(open("/dev/null", O_RDONLY | O_CLOEXEC));
    vfile *v = fd >= 0 && fd < MAX_FD && info.size >= 0 ? calloc(1, sizeof *v) : 0;
    if (!v) {
      int e = fd < 0 ? errno : fd >= MAX_FD ? EMFILE : info.size < 0 ? EIO : ENOMEM;
      if (fd >= 0) close(fd);
      providers[i].close(providers[i].user, file);
      errno = e;
      return -1;
    }
    v->p = &providers[i];
    v->file = file;
    v->info = info;
    v->refs = 1;
    pthread_mutex_lock(&mu);
    atomic_store(&vfds[fd], v);
    pthread_mutex_unlock(&mu);
    if (flog()) fprintf(stderr, "[rc] file: served \"%s\" (%lld bytes) as %d\n", abs, (long long)info.size, fd);
    return fd;
  }
  return -2;  // no provider's
}

int rc_file_open(const char *path, int flags, ...) {
  va_list ap;
  va_start(ap, flags);
  int mode = va_arg(ap, int);
  va_end(ap);
  if (!nprov()) return open(path, flags, mode);
  if (path && (flags & O_ACCMODE) == O_RDONLY && !(flags & (O_CREAT | O_TRUNC | O_DIRECTORY))) {
    int fd = vf_open(path);
    if (fd != -2) return fd;
  }
  return fresh(open(path, flags, mode));
}

int rc_file_close(int fd) {
  if (nprov() && vf_of(fd)) vf_drop(fd);
  return close(fd);
}

int rc_file_dup(int fd) {
  if (!nprov()) return dup(fd);
  pthread_mutex_lock(&mu);  // the record can't go between the dup and its count
  int d = dup(fd);
  vfile *v = vf_of(fd), *stale = d >= 0 && d < MAX_FD ? atomic_load(&vfds[d]) : 0;
  if (d >= 0 && d < MAX_FD) {
    if (v) v->refs++;
    atomic_store(&vfds[d], v);
  }
  int last = stale && --stale->refs == 0;
  pthread_mutex_unlock(&mu);
  if (last) {
    stale->p->close(stale->p->user, stale->file);
    free(stale);
  }
  if (v && d >= MAX_FD) {
    close(d);
    errno = EMFILE;
    return -1;
  }
  return d;
}

static ssize_t vf_pread(vfile *v, void *buf, size_t n, int64_t off) {
  if (off < 0) { errno = EINVAL; return -1; }
  if (off >= v->info.size || !n) return 0;
  if ((int64_t)n > v->info.size - off) n = (size_t)(v->info.size - off);
  size_t got = 0;
  while (got < n) {
    int64_t r = v->p->read(v->p->user, v->file, (char *)buf + got, n - got, off + (int64_t)got);
    if (r < 0) { errno = EIO; return -1; }
    if (r == 0) break;
    got += (size_t)r;
  }
  return (ssize_t)got;
}

ssize_t rc_file_pread(int fd, void *buf, size_t n, off_t off) {
  vfile *v = nprov() ? vf_of(fd) : 0;
  return v ? vf_pread(v, buf, n, off) : pread(fd, buf, n, off);
}

ssize_t rc_file_read(int fd, void *buf, size_t n) {
  vfile *v = nprov() ? vf_of(fd) : 0;
  if (!v) return read(fd, buf, n);
  ssize_t r = vf_pread(v, buf, n, atomic_load(&v->pos));
  if (r > 0) atomic_fetch_add(&v->pos, r);
  return r;
}

off_t rc_file_lseek(int fd, off_t off, int whence) {
  vfile *v = nprov() ? vf_of(fd) : 0;
  if (!v) return lseek(fd, off, whence);
  int64_t to = whence == SEEK_SET ? off : whence == SEEK_CUR ? atomic_load(&v->pos) + off :
               whence == SEEK_END ? v->info.size + off : -1;
  if (to < 0) { errno = EINVAL; return -1; }
  atomic_store(&v->pos, to);
  return (off_t)to;
}

static void fill_stat(struct stat *st, const rc_file_info *info, uintptr_t id) {
  memset(st, 0, sizeof *st);
  st->st_mode = S_IFREG | 0444;
  st->st_nlink = 1;
  st->st_ino = (ino_t)(id | 1);
  st->st_uid = getuid();
  st->st_gid = getgid();
  st->st_size = (off_t)info->size;
  st->st_blksize = 4096;
  st->st_blocks = (blkcnt_t)((info->size + 511) / 512);
  st->st_atimespec.tv_sec = st->st_mtimespec.tv_sec = st->st_ctimespec.tv_sec = st->st_birthtimespec.tv_sec =
      (time_t)info->mtime;
}

int rc_file_fstat(int fd, struct stat *st) {
  vfile *v = nprov() ? vf_of(fd) : 0;
  if (!v) return fstat(fd, st);
  fill_stat(st, &v->info, (uintptr_t)v);
  return 0;
}

// 1 with *info when a provider serves path
static int vf_stat(const char *abs, rc_file_info *info) {
  int n = nprov();
  for (int i = 0; i < n; i++) {
    const rc_file_provider *p = &providers[i];
    *info = (rc_file_info){0, 0};
    if (p->stat) {
      if (p->stat(p->user, abs, info)) return 1;
      continue;
    }
    void *file = p->open(p->user, abs, info);
    if (!file) continue;
    p->close(p->user, file);
    return 1;
  }
  return 0;
}

int rc_file_stat(const char *path, struct stat *st) {
  if (!nprov() || !path) return stat(path, st);
  char buf[PATH_MAX];
  const char *abs = absolute(path, buf, sizeof buf);
  rc_file_info info;
  if (!vf_stat(abs, &info)) return stat(path, st);
  uintptr_t id = 5381;
  for (const char *s = abs; *s; s++) id = id * 33 + (unsigned char)*s;
  fill_stat(st, &info, id << 1);
  return 0;
}

// --- FILE ---------------------------------------------------------------------------------------------------------
static int f_read(void *cookie, char *buf, int n) { return (int)rc_file_read((int)(intptr_t)cookie, buf, (size_t)n); }
static fpos_t f_seek(void *cookie, fpos_t off, int whence) {
  return (fpos_t)rc_file_lseek((int)(intptr_t)cookie, (off_t)off, whence);
}
static int f_close(void *cookie) { return rc_file_close((int)(intptr_t)cookie); }

FILE *rc_file_fopen(const char *path, const char *mode) {
  if (!nprov() || !path || !mode || mode[0] != 'r' || strchr(mode, '+')) return fopen(path, mode);
  int fd = vf_open(path);
  if (fd == -2) return fopen(path, mode);
  if (fd < 0) return 0;
  FILE *f = fd <= SHRT_MAX ? funopen((void *)(intptr_t)fd, f_read, 0, f_seek, f_close) : 0;
  if (!f) {
    rc_file_close(fd);
    errno = EMFILE;
    return 0;
  }
  f->_file = (short)fd;  // fileno: the game asks fstat for the size of some files it opened with fopen
  return f;
}

// --- folders ------------------------------------------------------------------------------------------------------
typedef struct vdir {
  DIR *d;
  char *path;
  char **add;
  int nadd, cap, next, real_done;
  struct dirent ent;
  struct vdir *link;
} vdir;
static vdir *dirs;

static void dir_add(void *ctx, const char *name) {
  vdir *v = ctx;
  if (!name || !*name || strchr(name, '/') || strlen(name) >= sizeof v->ent.d_name) return;
  for (int i = 0; i < v->nadd; i++)
    if (!strcmp(v->add[i], name)) return;
  if (v->nadd == v->cap) {
    int cap = v->cap ? v->cap * 2 : 16;
    char **a = realloc(v->add, (size_t)cap * sizeof *a);
    if (!a) return;
    v->add = a;
    v->cap = cap;
  }
  if ((v->add[v->nadd] = strdup(name))) v->nadd++;
}

static vdir *dir_of(DIR *d, int take) {
  pthread_mutex_lock(&mu);
  vdir **p = &dirs;
  while (*p && (*p)->d != d) p = &(*p)->link;
  vdir *v = *p;
  if (v && take) *p = v->link;
  pthread_mutex_unlock(&mu);
  return v;
}

DIR *rc_file_opendir(const char *path) {
  DIR *d = opendir(path);
  int n = nprov();
  if (!d || !n) return d;
  vdir *stale = dir_of(d, 1);  // a closedir that went past rc_file_closedir
  if (stale) {
    for (int i = 0; i < stale->nadd; i++) free(stale->add[i]);
    free(stale->add);
    free(stale->path);
    free(stale);
  }
  int any = 0;
  for (int i = 0; i < n; i++) any |= providers[i].list || providers[i].hide;
  if (!any) return d;
  char buf[PATH_MAX];
  vdir *v = calloc(1, sizeof *v);
  if (!v || !(v->path = strdup(absolute(path, buf, sizeof buf)))) {
    free(v);
    return d;  // the folder as it is
  }
  size_t len = strlen(v->path);
  while (len > 1 && v->path[len - 1] == '/') v->path[--len] = 0;
  v->d = d;
  for (int i = 0; i < n; i++)
    if (providers[i].list) providers[i].list(providers[i].user, v->path, dir_add, v);
  if (flog() && v->nadd) fprintf(stderr, "[rc] file: \"%s\": %d names added\n", v->path, v->nadd);
  pthread_mutex_lock(&mu);
  v->link = dirs;
  dirs = v;
  pthread_mutex_unlock(&mu);
  return d;
}

struct dirent *rc_file_readdir(DIR *d) {
  vdir *v = nprov() ? dir_of(d, 0) : 0;
  if (!v) return readdir(d);
  int n = nprov();
  while (!v->real_done) {
    struct dirent *e = readdir(d);
    if (!e) {
      v->real_done = 1;
      break;
    }
    int hidden = 0;
    if (strcmp(e->d_name, ".") && strcmp(e->d_name, ".."))
      for (int i = 0; i < n && !hidden; i++)
        hidden = providers[i].hide && providers[i].hide(providers[i].user, v->path, e->d_name);
    if (hidden) continue;
    for (int i = 0; i < v->nadd; i++)  // a name the folder has is listed once
      if (v->add[i] && !strcmp(v->add[i], e->d_name)) {
        free(v->add[i]);
        v->add[i] = 0;
      }
    return e;
  }
  while (v->next < v->nadd) {
    const char *name = v->add[v->next++];
    if (!name) continue;
    memset(&v->ent, 0, sizeof v->ent);
    v->ent.d_ino = 1;
    v->ent.d_type = DT_REG;
    v->ent.d_namlen = (__uint16_t)strlen(name);
    v->ent.d_reclen = sizeof v->ent;
    memcpy(v->ent.d_name, name, v->ent.d_namlen + 1);
    return &v->ent;
  }
  return 0;
}

int rc_file_closedir(DIR *d) {
  vdir *v = dir_of(d, 1);
  if (v) {
    for (int i = 0; i < v->nadd; i++) free(v->add[i]);
    free(v->add);
    free(v->path);
    free(v);
  }
  return closedir(d);
}
