// kernel32: Windows paths onto the host, files, directories, find searches, file mappings
// (win32_shims.h: w32_path).
//
// The guest sees one drive layout: the game folder is "C:\Rocksmith2014" (the starting current
// directory) and lives in LP_GAME_DIR; any other path of a drive X lives under LP_USER_DIR/x/.
// Windows names are case-insensitive, so a path component that does not exist as spelt is looked
// up without case. Every file the guest opens is logged with LP_LOG_FILES=1; failed opens always.
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <fnmatch.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <strings.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <time.h>
#include <unistd.h>

#include "win32_handles.h"

static const char GAME_WIN[] = "C:\\Rocksmith2014";
static char cwd_win[1024] = "C:\\Rocksmith2014";
static pthread_mutex_t cwd_mu = PTHREAD_MUTEX_INITIALIZER;
static int log_files = -1;

const char *w32_game_dir_win(void) { return GAME_WIN; }

static const char *env_dir(const char *var, const char *dflt) {
  const char *e = getenv(var);
  return e && *e ? e : dflt;
}
static const char *game_dir(void) { return env_dir("LP_GAME_DIR", "."); }
static const char *user_dir(void) { return env_dir("LP_USER_DIR", "./userdata"); }

static int flog(void) {
  if (log_files < 0) {
    const char *e = getenv("LP_LOG_FILES");
    log_files = e && *e && *e != '0' ? atoi(e) > 1 ? 2 : 1 : 0;  // 2: every read too
  }
  return log_files;
}

// the full Windows form of a guest path: drive, backslashes, no "." / ".." components
static void full_win(const char *in, char *out, size_t n) {
  char t[2048];
  if (!strncmp(in, "\\\\?\\", 4) || !strncmp(in, "//?/", 4)) in += 4;
  pthread_mutex_lock(&cwd_mu);
  if (((in[0] | 0x20) >= 'a' && (in[0] | 0x20) <= 'z') && in[1] == ':') {
    if (in[2] == '\\' || in[2] == '/') snprintf(t, sizeof t, "%s", in);
    // "C:x": relative to that drive's current directory: the process's own on its drive (the
    // CRT's _getcwd asks for "C:."), the root on any other
    else if ((in[0] | 0x20) == (cwd_win[0] | 0x20)) snprintf(t, sizeof t, "%s\\%s", cwd_win, in + 2);
    else snprintf(t, sizeof t, "%c:\\%s", in[0], in + 2);
  } else if (in[0] == '\\' || in[0] == '/') {
    snprintf(t, sizeof t, "%c:%s", cwd_win[0], in);
  } else {
    snprintf(t, sizeof t, "%s\\%s", cwd_win, in);
  }
  pthread_mutex_unlock(&cwd_mu);
  for (char *p = t; *p; p++)
    if (*p == '/') *p = '\\';
  t[0] = (char)(t[0] & ~0x20);  // drive letter upper case
  // components after "X:"
  char *comp[256];
  int nc = 0;
  char *save;
  for (char *s = strtok_r(t + 2, "\\", &save); s && nc < 256; s = strtok_r(0, "\\", &save)) {
    if (!strcmp(s, ".")) continue;
    if (!strcmp(s, "..")) {
      if (nc) nc--;
      continue;
    }
    comp[nc++] = s;
  }
  size_t o = (size_t)snprintf(out, n, "%c:", t[0]);
  for (int k = 0; k < nc && o < n; k++) o += (size_t)snprintf(out + o, n - o, "\\%s", comp[k]);
  if (!nc && o < n) snprintf(out + o, n - o, "\\");
}

// a component of dir named `name` without case, written over name; 0 when there is none
static int ci_find(const char *dir, char *name) {
  DIR *d = opendir(dir);
  if (!d) return 0;
  struct dirent *e;
  int ok = 0;
  while ((e = readdir(d)))
    if (!strcasecmp(e->d_name, name)) {
      strcpy(name, e->d_name);
      ok = 1;
      break;
    }
  closedir(d);
  return ok;
}

// base + "/" + rest (rest with backslashes), each component found without case when it does not
// exist as spelt
static void join_ci(const char *base, const char *rest, char *host, size_t n) {
  size_t o = (size_t)snprintf(host, n, "%s", base);
  char t[2048];
  snprintf(t, sizeof t, "%s", rest);
  int missing = 0;
  char *save;
  for (char *s = strtok_r(t, "\\", &save); s && o < n; s = strtok_r(0, "\\", &save)) {
    char comp[NAME_MAX + 1];
    snprintf(comp, sizeof comp, "%s", s);
    host[o] = 0;
    if (!missing) {
      char probe[PATH_MAX];
      struct stat st;
      snprintf(probe, sizeof probe, "%s/%s", host, comp);
      if (lstat(probe, &st) && !ci_find(host, comp)) missing = 1;
    }
    o += (size_t)snprintf(host + o, n - o, "/%s", comp);
  }
}

// mkdir -p of the directories above host path p, from `from` (a prefix of p known to exist) on
static void mk_parents(const char *p, size_t from) {
  char t[PATH_MAX];
  snprintf(t, sizeof t, "%s", p);
  for (char *s = t + from + 1; *s; s++)
    if (*s == '/') {
      *s = 0;
      mkdir(t, 0755);
      *s = '/';
    }
}

// The game folder is read-only to the guest's writes: a file the game writes or creates there
// goes to LP_USER_DIR/game/ (copied there first when it exists and is opened to be changed), and
// reads see that copy from then on.
// A host path is one the seam handed the guest, so it is taken only inside a folder the guest has:
// the game's, the user folder, and what w32_host_root adds (the Steam layer's data folder). Any
// other path that starts with a slash is the Windows path it also is, from the current drive's root.
#define HOST_ROOTS 4
static char host_roots[HOST_ROOTS][PATH_MAX];
static int nhost_roots;
static pthread_mutex_t roots_mu = PTHREAD_MUTEX_INITIALIZER;

// p without "." and ".." components and doubled slashes; 0 when it does not fit or leaves the root
static int host_norm(const char *p, char *out, size_t n) {
  size_t o = 0;
  while (*p) {
    while (*p == '/') p++;
    size_t len = strcspn(p, "/");
    if (!len) break;
    if (len == 1 && p[0] == '.') {
    } else if (len == 2 && p[0] == '.' && p[1] == '.') {
      if (!o) return 0;
      while (o && out[--o] != '/') {}
    } else {
      if (o + len + 2 > n) return 0;
      out[o++] = '/';
      memcpy(out + o, p, len);
      o += len;
    }
    p += len;
  }
  if (!o) {
    if (n < 2) return 0;
    out[o++] = '/';
  }
  out[o] = 0;
  return 1;
}
static int under(const char *path, const char *root) {
  char r[PATH_MAX];
  if (root[0] != '/') {  // LP_GAME_DIR and LP_USER_DIR may be relative
    char cwd[PATH_MAX], t[2 * PATH_MAX];
    if (!getcwd(cwd, sizeof cwd)) return 0;
    snprintf(t, sizeof t, "%s/%s", cwd, root);
    if (!host_norm(t, r, sizeof r)) return 0;
  } else if (!host_norm(root, r, sizeof r)) {
    return 0;
  }
  size_t k = strlen(r);
  if (k == 1) return 0;  // "/" is nobody's folder
  return !strncmp(path, r, k) && (path[k] == '/' || !path[k]);
}
void w32_host_root(const char *dir) {
  char r[PATH_MAX];
  if (!dir || dir[0] != '/' || !host_norm(dir, r, sizeof r)) return;
  pthread_mutex_lock(&roots_mu);
  int k = 0;
  while (k < nhost_roots && strcmp(host_roots[k], r)) k++;
  if (k == nhost_roots && nhost_roots < HOST_ROOTS) strcpy(host_roots[nhost_roots++], r);
  pthread_mutex_unlock(&roots_mu);
}
// win as a host path the guest may use, in out; 0 when it is not one
static int host_path(const char *win, char *out, size_t n) {
  if (win[0] != '/' || !strncmp(win, "//?/", 4)) return 0;
  char t[PATH_MAX];
  if (!host_norm(win, t, sizeof t)) return 0;
  int ok = under(t, game_dir()) || under(t, user_dir());
  pthread_mutex_lock(&roots_mu);
  for (int k = 0; k < nhost_roots && !ok; k++) ok = under(t, host_roots[k]);
  pthread_mutex_unlock(&roots_mu);
  if (ok) snprintf(out, n, "%s", t);
  else if (flog()) fprintf(stderr, "w32: file: %s is outside the guest's folders: taken as a Windows path\n", win);
  return ok;
}

static char *map_path(const char *win, char *host, size_t n, int wr) {
  if (host_path(win, host, n)) return host;
  char full[2048];
  full_win(win, full, sizeof full);
  size_t g = sizeof GAME_WIN - 1;
  if (!strncasecmp(full, GAME_WIN, g) && (full[g] == '\\' || !full[g])) {
    char ovl_base[PATH_MAX], ovl[PATH_MAX];
    struct stat st;
    snprintf(ovl_base, sizeof ovl_base, "%s/game", user_dir());
    join_ci(ovl_base, full + g, ovl, sizeof ovl);
    join_ci(game_dir(), full + g, host, n);
    if (!lstat(ovl, &st)) {
      snprintf(host, n, "%s", ovl);
    } else if (wr) {
      mkdir(user_dir(), 0755);
      mkdir(ovl_base, 0755);
      mk_parents(ovl, strlen(ovl_base));
      if (wr > 1 && !stat(host, &st) && S_ISREG(st.st_mode)) {  // keep the bytes it changes
        int in = open(host, O_RDONLY), out = open(ovl, O_WRONLY | O_CREAT | O_TRUNC, 0644);
        char buf[65536];
        ssize_t r;
        while (in >= 0 && out >= 0 && (r = read(in, buf, sizeof buf)) > 0)
          if (write(out, buf, (size_t)r) != r) break;
        if (in >= 0) close(in);
        if (out >= 0) close(out);
      }
      snprintf(host, n, "%s", ovl);
    }
  } else {
    char base[PATH_MAX];
    snprintf(base, sizeof base, "%s/%c", user_dir(), full[0] | 0x20);
    mkdir(user_dir(), 0755);
    mkdir(base, 0755);
    join_ci(base, full + 2, host, n);
  }
  return host;
}

char *w32_path(const char *win, char *host, size_t n) { return map_path(win, host, n, 0); }
// for a write that replaces or creates the file (1), or changes it in place (2)
static char *path_w(const char *win, char *host, size_t n, int how) { return map_path(win, host, n, how); }

static uint32_t err_of(int e) {
  switch (e) {
  case ENOENT: return W32_ERROR_FILE_NOT_FOUND;
  case ENOTDIR: return W32_ERROR_PATH_NOT_FOUND;
  case EACCES: case EPERM: case EISDIR: return W32_ERROR_ACCESS_DENIED;
  case EEXIST: return W32_ERROR_FILE_EXISTS;
  case ENOTEMPTY: return 145;  // ERROR_DIR_NOT_EMPTY
  case ENOSPC: return 112;     // ERROR_DISK_FULL
  }
  return 31;  // ERROR_GEN_FAILURE
}
static void set_errno_err(rc_ctx *c) { w32_set_error(c, err_of(errno)); }
// the path's parent missing reads as ERROR_PATH_NOT_FOUND, as on Windows
static void set_open_err(rc_ctx *c, const char *host) {
  int e = errno;
  if (e == ENOENT) {
    char d[PATH_MAX];
    snprintf(d, sizeof d, "%s", host);
    char *sl = strrchr(d, '/');
    struct stat st;
    if (sl && sl != d) {
      *sl = 0;
      if (stat(d, &st)) { w32_set_error(c, W32_ERROR_PATH_NOT_FOUND); return; }
    }
  }
  w32_set_error(c, err_of(e));
}

static uint64_t filetime(struct timespec t) {
  return ((uint64_t)t.tv_sec + 11644473600ull) * 10000000ull + (uint64_t)t.tv_nsec / 100;
}
static uint32_t attrs(const struct stat *st, const char *host) {
  uint32_t a = S_ISDIR(st->st_mode) ? 0x10 : 0x20;  // DIRECTORY / ARCHIVE
  if (!(st->st_mode & S_IWUSR)) a |= 1;           // READONLY
  const char *b = strrchr(host, '/');
  if (b && b[1] == '.' && b[2] && strcmp(b, "/..")) a |= 2;  // HIDDEN, as dot files read on a Mac
  return a;
}

// --- CreateFile --------------------------------------------------------------------------------
static uint32_t create_file(rc_ctx *c, const char *win, uint32_t access, uint32_t disp, uint32_t flags) {
  char host[PATH_MAX];
  int writes = (access & 0x50000000u) != 0, creates = disp == 1 || disp == 2 || disp == 4;
  if (writes || creates) path_w(win, host, sizeof host, disp == 2 || disp == 5 ? 1 : 2);
  else w32_path(win, host, sizeof host);
  if (!strcasecmp(win, "CONOUT$") || !strcasecmp(win, "CONIN$")) {
    w32_obj *o;
    uint32_t h = w32_handle_new(H_FILE, &o);
    if (!h) return 0xffffffffu;
    o->u.file.fd = win[3] == 'O' || win[3] == 'o' ? 1 : 0;
    o->u.file.console = 1;
    return h;
  }
  int of = (access & 0x40000000u) ? ((access & 0x80000000u) ? O_RDWR : O_WRONLY) : O_RDONLY;
  if (access & 0x10000000u) of = O_RDWR;  // GENERIC_ALL
  switch (disp) {
  case 1: of |= O_CREAT | O_EXCL; break;   // CREATE_NEW
  case 2: of |= O_CREAT | O_TRUNC; break;  // CREATE_ALWAYS
  case 4: of |= O_CREAT; break;            // OPEN_ALWAYS
  case 5: of |= O_TRUNC; break;            // TRUNCATE_EXISTING
  }
  struct stat st;
  int existed = stat(host, &st) == 0;
  if (existed && S_ISDIR(st.st_mode)) {
    if (!(flags & 0x02000000u)) {  // FILE_FLAG_BACKUP_SEMANTICS opens a directory
      w32_set_error(c, W32_ERROR_ACCESS_DENIED);
      if (flog()) fprintf(stderr, "w32: file: open \"%s\": a directory\n", win);
      return 0xffffffffu;
    }
    of = O_RDONLY;
  }
  int fd = open(host, of | O_CLOEXEC, 0644);
  if (fd < 0) {
    int e = errno;
    set_open_err(c, host);
    if (e != ENOENT || flog()) fprintf(stderr, "w32: file: open \"%s\" (%s) failed: %s\n", win, host, strerror(e));
    return 0xffffffffu;
  }
  w32_obj *o;
  uint32_t h = w32_handle_new(H_FILE, &o);
  if (!h) { close(fd); return 0xffffffffu; }
  o->u.file.fd = fd;
  o->u.file.path = strdup(host);
  o->u.file.access = access;
  o->u.file.dir = existed && S_ISDIR(st.st_mode);
  w32_set_error(c, existed && (disp == 2 || disp == 4) ? W32_ERROR_ALREADY_EXISTS : 0);
  if (flog()) fprintf(stderr, "w32: file: open \"%s\" -> 0x%x\n", win, h);
  return h;
}
static void k_CreateFileA(rc_ctx *c) {
  w32_ret(c, create_file(c, w32_cstr(w32_arg(c, 0)), w32_arg(c, 1), w32_arg(c, 4), w32_arg(c, 5)), 7);
}
static void k_CreateFileW(rc_ctx *c) {
  char n[1024];
  w32_ret(c, create_file(c, w32_wstr(w32_arg(c, 0), n, sizeof n), w32_arg(c, 1), w32_arg(c, 4), w32_arg(c, 5)), 7);
}

// --- read / write / seek -----------------------------------------------------------------------
static int64_t ovl_off(uint32_t ovl) { return (int64_t)((uint64_t)w32_rd32(ovl + 8) | (uint64_t)w32_rd32(ovl + 12) << 32); }
static void ovl_done(uint32_t ovl, uint32_t n) {
  w32_wr32(ovl, 0);      // Internal: STATUS_SUCCESS
  w32_wr32(ovl + 4, n);  // InternalHigh: bytes transferred
  uint32_t ev = w32_rd32(ovl + 16);
  if (ev) w32_event_signal(ev);
}
static void k_ReadFile(rc_ctx *c) {
  uint32_t h = w32_arg(c, 0), b = w32_arg(c, 1), n = w32_arg(c, 2), rd = w32_arg(c, 3), ovl = w32_arg(c, 4);
  w32_obj *o = w32_handle_get(h, H_FILE);
  if (!o) { w32_set_error(c, W32_ERROR_INVALID_HANDLE); w32_ret(c, 0, 5); return; }
  uint32_t got = 0;
  uint64_t t0 = LP_FRAME_ON() ? lp_frame_now() : 0;
  if (!(o->u.file.console && o->u.file.fd == 0)) {  // the console's input reads as empty
    int64_t off = ovl ? ovl_off(ovl) : -1;
    while (got < n) {
      ssize_t r = off >= 0 ? pread(o->u.file.fd, RC32_H(b + got), n - got, off + got) : read(o->u.file.fd, RC32_H(b + got), n - got);
      if (r < 0 && errno == EINTR) continue;
      if (r < 0) { set_errno_err(c); if (rd) w32_wr32(rd, got); w32_ret(c, 0, 5); return; }
      if (r == 0) break;
      got += (uint32_t)r;
    }
  }
  if (rd) w32_wr32(rd, got);
  if (t0) lp_frame_event(LPF_FILE_READ, lp_frame_now() - t0, 0);
  if (flog() > 1)
    fprintf(stderr, "w32: file: read 0x%x \"%s\" at %lld: %u of %u\n", h, o->u.file.path ? o->u.file.path : "",
            ovl ? (long long)ovl_off(ovl) : -1ll, got, n);
  if (ovl) {
    ovl_done(ovl, got);
    if (!got && n) { w32_set_error(c, 38); w32_ret(c, 0, 5); return; }  // ERROR_HANDLE_EOF
  }
  w32_ret(c, 1, 5);
}
static void k_WriteFile(rc_ctx *c) {
  uint32_t h = w32_arg(c, 0), b = w32_arg(c, 1), n = w32_arg(c, 2), wr = w32_arg(c, 3), ovl = w32_arg(c, 4);
  w32_obj *o = w32_handle_get(h, H_FILE);
  if (!o) { w32_set_error(c, W32_ERROR_INVALID_HANDLE); w32_ret(c, 0, 5); return; }
  int64_t off = ovl ? ovl_off(ovl) : -1;
  uint32_t put = 0;
  while (put < n) {
    ssize_t r = off >= 0 && !o->u.file.console ? pwrite(o->u.file.fd, RC32_H(b + put), n - put, off + put)
                                               : write(o->u.file.fd, RC32_H(b + put), n - put);
    if (r < 0 && errno == EINTR) continue;
    if (r < 0) { set_errno_err(c); if (wr) w32_wr32(wr, put); w32_ret(c, 0, 5); return; }
    put += (uint32_t)r;
  }
  if (wr) w32_wr32(wr, put);
  if (ovl) ovl_done(ovl, put);
  w32_ret(c, 1, 5);
}
static void k_SetFilePointer(rc_ctx *c) {
  uint32_t h = w32_arg(c, 0), hip = w32_arg(c, 2), how = w32_arg(c, 3);
  int fd = w32_file_fd(h);
  int64_t d = hip ? (int64_t)((uint64_t)w32_arg(c, 1) | (uint64_t)w32_rd32(hip) << 32) : (int64_t)(int32_t)w32_arg(c, 1);
  off_t r = fd < 0 ? -1 : lseek(fd, d, how == 1 ? SEEK_CUR : how == 2 ? SEEK_END : SEEK_SET);
  if (r < 0) {
    w32_set_error(c, fd < 0 ? W32_ERROR_INVALID_HANDLE : 131);  // ERROR_NEGATIVE_SEEK
    w32_ret(c, 0xffffffffu, 4);
    return;
  }
  if (hip) w32_wr32(hip, (uint32_t)((uint64_t)r >> 32));
  w32_set_error(c, 0);
  w32_ret(c, (uint32_t)r, 4);
}
static void k_SetFilePointerEx(rc_ctx *c) {
  int fd = w32_file_fd(w32_arg(c, 0));
  int64_t d = (int64_t)((uint64_t)w32_arg(c, 1) | (uint64_t)w32_arg(c, 2) << 32);
  uint32_t outp = w32_arg(c, 3), how = w32_arg(c, 4);
  off_t r = fd < 0 ? -1 : lseek(fd, d, how == 1 ? SEEK_CUR : how == 2 ? SEEK_END : SEEK_SET);
  if (r < 0) { w32_set_error(c, W32_ERROR_INVALID_PARAMETER); w32_ret(c, 0, 5); return; }
  if (outp) w32_wr64(outp, (uint64_t)r);
  w32_ret(c, 1, 5);
}
static void k_GetFileSize(rc_ctx *c) {
  struct stat st;
  int fd = w32_file_fd(w32_arg(c, 0));
  if (fd < 0 || fstat(fd, &st)) { w32_set_error(c, W32_ERROR_INVALID_HANDLE); w32_ret(c, 0xffffffffu, 2); return; }
  if (w32_arg(c, 1)) w32_wr32(w32_arg(c, 1), (uint32_t)((uint64_t)st.st_size >> 32));
  w32_set_error(c, 0);
  w32_ret(c, (uint32_t)st.st_size, 2);
}
static void k_GetFileSizeEx(rc_ctx *c) {
  struct stat st;
  int fd = w32_file_fd(w32_arg(c, 0));
  if (fd < 0 || fstat(fd, &st)) { w32_set_error(c, W32_ERROR_INVALID_HANDLE); w32_ret(c, 0, 2); return; }
  w32_wr64(w32_arg(c, 1), (uint64_t)st.st_size);
  w32_ret(c, 1, 2);
}
static void k_SetEndOfFile(rc_ctx *c) {
  int fd = w32_file_fd(w32_arg(c, 0));
  w32_ret(c, fd >= 0 && !ftruncate(fd, lseek(fd, 0, SEEK_CUR)), 1);
}
static void k_FlushFileBuffers(rc_ctx *c) { w32_ret(c, w32_file_fd(w32_arg(c, 0)) >= 0, 1); }
static void k_GetFileType(rc_ctx *c) {
  w32_obj *o = w32_handle_get(w32_arg(c, 0), H_FILE);
  uint32_t t = 0;  // FILE_TYPE_UNKNOWN
  if (o && o->u.file.console) t = 2;  // FILE_TYPE_CHAR
  else if (o) {
    struct stat st;
    t = fstat(o->u.file.fd, &st) ? 0 : S_ISFIFO(st.st_mode) || S_ISSOCK(st.st_mode) ? 3 : S_ISCHR(st.st_mode) ? 2 : 1;
  }
  if (!o) w32_set_error(c, W32_ERROR_INVALID_HANDLE);
  w32_ret(c, t, 1);
}
static void k_GetFileTime(rc_ctx *c) {
  struct stat st;
  int fd = w32_file_fd(w32_arg(c, 0));
  if (fd < 0 || fstat(fd, &st)) { w32_ret(c, 0, 4); return; }
  if (w32_arg(c, 1)) w32_wr64(w32_arg(c, 1), filetime(st.st_birthtimespec));
  if (w32_arg(c, 2)) w32_wr64(w32_arg(c, 2), filetime(st.st_atimespec));
  if (w32_arg(c, 3)) w32_wr64(w32_arg(c, 3), filetime(st.st_mtimespec));
  w32_ret(c, 1, 4);
}
static void k_SetFileTime(rc_ctx *c) {
  int fd = w32_file_fd(w32_arg(c, 0));
  uint32_t wt = w32_arg(c, 3);
  if (fd >= 0 && wt) {
    uint64_t ft;
    memcpy(&ft, RC32_H(wt), 8);
    struct timespec ts[2];
    ts[0].tv_nsec = UTIME_OMIT;
    ts[1].tv_sec = (time_t)(ft / 10000000ull - 11644473600ull);
    ts[1].tv_nsec = (long)(ft % 10000000ull) * 100;
    futimens(fd, ts);
  }
  w32_ret(c, fd >= 0, 4);
}
static void k_GetFileInformationByHandle(rc_ctx *c) {
  struct stat st;
  w32_obj *o = w32_handle_get(w32_arg(c, 0), H_FILE);
  uint32_t b = w32_arg(c, 1);
  if (!o || fstat(o->u.file.fd, &st)) { w32_set_error(c, W32_ERROR_INVALID_HANDLE); w32_ret(c, 0, 2); return; }
  memset(RC32_H(b), 0, 52);
  w32_wr32(b, attrs(&st, o->u.file.path ? o->u.file.path : ""));
  w32_wr64(b + 4, filetime(st.st_birthtimespec));
  w32_wr64(b + 12, filetime(st.st_atimespec));
  w32_wr64(b + 20, filetime(st.st_mtimespec));
  w32_wr32(b + 28, (uint32_t)st.st_dev);
  w32_wr32(b + 32, (uint32_t)((uint64_t)st.st_size >> 32));
  w32_wr32(b + 36, (uint32_t)st.st_size);
  w32_wr32(b + 40, (uint32_t)st.st_nlink);
  w32_wr32(b + 44, (uint32_t)((uint64_t)st.st_ino >> 32));
  w32_wr32(b + 48, (uint32_t)st.st_ino);
  w32_ret(c, 1, 2);
}
static void k_LockFile(rc_ctx *c) { w32_ret(c, 1, 5); }
static void k_UnlockFile(rc_ctx *c) { w32_ret(c, 1, 5); }
static void k_DeviceIoControl(rc_ctx *c) {
  if (flog()) fprintf(stderr, "w32: DeviceIoControl(0x%x, 0x%x): not supported\n", w32_arg(c, 0), w32_arg(c, 1));
  w32_set_error(c, W32_ERROR_NOT_SUPPORTED);
  w32_ret(c, 0, 8);
}

// --- attributes, delete, move, directories -----------------------------------------------------
static uint32_t get_attrs(rc_ctx *c, const char *win) {
  char host[PATH_MAX];
  struct stat st;
  w32_path(win, host, sizeof host);
  if (stat(host, &st)) {
    set_open_err(c, host);
    if (flog()) fprintf(stderr, "w32: file: attributes of \"%s\": none\n", win);
    return 0xffffffffu;
  }
  if (flog()) fprintf(stderr, "w32: file: attributes of \"%s\"\n", win);
  return attrs(&st, host);
}
static void k_GetFileAttributesA(rc_ctx *c) { w32_ret(c, get_attrs(c, w32_cstr(w32_arg(c, 0))), 1); }
static void k_GetFileAttributesW(rc_ctx *c) {
  char n[1024];
  w32_ret(c, get_attrs(c, w32_wstr(w32_arg(c, 0), n, sizeof n)), 1);
}
static int attr_data(rc_ctx *c, const char *win, uint32_t b) {
  char host[PATH_MAX];
  struct stat st;
  w32_path(win, host, sizeof host);
  if (stat(host, &st)) {
    set_open_err(c, host);
    if (flog()) fprintf(stderr, "w32: file: attributes of \"%s\": none\n", win);
    return 0;
  }
  w32_wr32(b, attrs(&st, host));
  w32_wr64(b + 4, filetime(st.st_birthtimespec));
  w32_wr64(b + 12, filetime(st.st_atimespec));
  w32_wr64(b + 20, filetime(st.st_mtimespec));
  w32_wr32(b + 28, (uint32_t)((uint64_t)st.st_size >> 32));
  w32_wr32(b + 32, (uint32_t)st.st_size);
  return 1;
}
static void k_GetFileAttributesExA(rc_ctx *c) { w32_ret(c, attr_data(c, w32_cstr(w32_arg(c, 0)), w32_arg(c, 2)), 3); }
static void k_GetFileAttributesExW(rc_ctx *c) {
  char n[1024];
  w32_ret(c, attr_data(c, w32_wstr(w32_arg(c, 0), n, sizeof n), w32_arg(c, 2)), 3);
}
static int set_attrs(const char *win, uint32_t a) {
  char host[PATH_MAX];
  struct stat st;
  path_w(win, host, sizeof host, 2);
  if (stat(host, &st)) return 0;
  chmod(host, (a & 1) ? (st.st_mode & ~0222) : (st.st_mode | S_IWUSR));
  return 1;
}
static void k_SetFileAttributesA(rc_ctx *c) { w32_ret(c, set_attrs(w32_cstr(w32_arg(c, 0)), w32_arg(c, 1)), 2); }
static void k_SetFileAttributesW(rc_ctx *c) {
  char n[1024];
  w32_ret(c, set_attrs(w32_wstr(w32_arg(c, 0), n, sizeof n), w32_arg(c, 1)), 2);
}
typedef int (*path_op)(const char *);
static int do_path(rc_ctx *c, const char *win, path_op op, const char *what) {
  char host[PATH_MAX];
  path_w(win, host, sizeof host, 1);
  int ok = op(host) == 0;
  if (!ok) set_errno_err(c);
  if (flog() || !ok) fprintf(stderr, "w32: file: %s \"%s\"%s\n", what, win, ok ? "" : " failed");
  return ok;
}
static int mkdir1(const char *p) { return mkdir(p, 0755); }
static void k_DeleteFileA(rc_ctx *c) { w32_ret(c, do_path(c, w32_cstr(w32_arg(c, 0)), unlink, "delete"), 1); }
static void k_DeleteFileW(rc_ctx *c) { char n[1024]; w32_ret(c, do_path(c, w32_wstr(w32_arg(c, 0), n, sizeof n), unlink, "delete"), 1); }
static void k_RemoveDirectoryA(rc_ctx *c) { w32_ret(c, do_path(c, w32_cstr(w32_arg(c, 0)), rmdir, "rmdir"), 1); }
static void k_RemoveDirectoryW(rc_ctx *c) { char n[1024]; w32_ret(c, do_path(c, w32_wstr(w32_arg(c, 0), n, sizeof n), rmdir, "rmdir"), 1); }
static void k_CreateDirectoryA(rc_ctx *c) { w32_ret(c, do_path(c, w32_cstr(w32_arg(c, 0)), mkdir1, "mkdir"), 2); }
static void k_CreateDirectoryW(rc_ctx *c) { char n[1024]; w32_ret(c, do_path(c, w32_wstr(w32_arg(c, 0), n, sizeof n), mkdir1, "mkdir"), 2); }
static int move(rc_ctx *c, const char *a, const char *b) {
  char ha[PATH_MAX], hb[PATH_MAX];
  struct stat st;
  // the game folder is read-only here too: a file only there is copied to the overlay and that copy moves
  path_w(a, ha, sizeof ha, 2);
  path_w(b, hb, sizeof hb, 1);
  if (!stat(hb, &st)) { w32_set_error(c, W32_ERROR_ALREADY_EXISTS); return 0; }  // MoveFile does not replace
  int ok = rename(ha, hb) == 0;
  if (!ok) set_errno_err(c);
  return ok;
}
static void k_MoveFileA(rc_ctx *c) { w32_ret(c, move(c, w32_cstr(w32_arg(c, 0)), w32_cstr(w32_arg(c, 1))), 2); }
static void k_MoveFileW(rc_ctx *c) {
  char a[1024], b[1024];
  w32_ret(c, move(c, w32_wstr(w32_arg(c, 0), a, sizeof a), w32_wstr(w32_arg(c, 1), b, sizeof b)), 2);
}

// --- current directory, full paths, temp, drives -----------------------------------------------
static uint32_t put_a(uint32_t buf, uint32_t cap, const char *s) {  // GetCurrentDirectory-style result
  uint32_t n = (uint32_t)strlen(s);
  if (cap <= n) return n + 1;
  memcpy(RC32_H(buf), s, n + 1);
  return n;
}
static uint32_t put_w(uint32_t buf, uint32_t cap, const char *s) {
  uint32_t n = w32_put_wstr(0, 0, s);
  if (cap <= n) return n + 1;
  return w32_put_wstr(buf, cap, s);
}
static void k_GetCurrentDirectoryA(rc_ctx *c) {
  pthread_mutex_lock(&cwd_mu);
  uint32_t r = put_a(w32_arg(c, 1), w32_arg(c, 0), cwd_win);
  pthread_mutex_unlock(&cwd_mu);
  w32_ret(c, r, 2);
}
static void k_GetCurrentDirectoryW(rc_ctx *c) {
  pthread_mutex_lock(&cwd_mu);
  uint32_t r = put_w(w32_arg(c, 1), w32_arg(c, 0), cwd_win);
  pthread_mutex_unlock(&cwd_mu);
  w32_ret(c, r, 2);
}
// (Bink's DllMain asks for both)
static void k_GetWindowsDirectoryA(rc_ctx *c) { w32_ret(c, put_a(w32_arg(c, 0), w32_arg(c, 1), "C:\\Windows"), 2); }
// the 32-bit system folder as a 32-bit process sees it
static void k_GetSystemDirectoryA(rc_ctx *c) { w32_ret(c, put_a(w32_arg(c, 0), w32_arg(c, 1), "C:\\Windows\\system32"), 2); }
static int set_cwd(rc_ctx *c, const char *win) {
  char full[1024], host[PATH_MAX];
  struct stat st;
  full_win(win, full, sizeof full);
  w32_path(full, host, sizeof host);
  if (stat(host, &st) || !S_ISDIR(st.st_mode)) {
    w32_set_error(c, W32_ERROR_PATH_NOT_FOUND);
    fprintf(stderr, "w32: SetCurrentDirectory \"%s\": no such directory\n", win);
    return 0;
  }
  size_t n = strlen(full);
  if (n > 3 && full[n - 1] == '\\') full[n - 1] = 0;
  pthread_mutex_lock(&cwd_mu);
  snprintf(cwd_win, sizeof cwd_win, "%s", full);
  pthread_mutex_unlock(&cwd_mu);
  if (w32_log) fprintf(stderr, "w32: current directory \"%s\"\n", full);
  return 1;
}
static void k_SetCurrentDirectoryA(rc_ctx *c) { w32_ret(c, set_cwd(c, w32_cstr(w32_arg(c, 0))), 1); }
static void k_SetCurrentDirectoryW(rc_ctx *c) { char n[1024]; w32_ret(c, set_cwd(c, w32_wstr(w32_arg(c, 0), n, sizeof n)), 1); }
static void k_GetFullPathNameA(rc_ctx *c) {
  char full[1024];
  uint32_t cap = w32_arg(c, 1), buf = w32_arg(c, 2), part = w32_arg(c, 3);
  full_win(w32_cstr(w32_arg(c, 0)), full, sizeof full);
  if (flog()) fprintf(stderr, "w32: file: full path \"%s\" -> \"%s\"\n", w32_cstr(w32_arg(c, 0)), full);
  uint32_t r = put_a(buf, cap, full);
  if (part && r < cap) {
    char *s = strrchr(full, '\\');
    w32_wr32(part, s && s[1] ? buf + (uint32_t)(s + 1 - full) : 0);
  }
  w32_ret(c, r, 4);
}
static void k_GetFullPathNameW(rc_ctx *c) {
  char in[1024], full[1024];
  uint32_t cap = w32_arg(c, 1), buf = w32_arg(c, 2), part = w32_arg(c, 3);
  full_win(w32_wstr(w32_arg(c, 0), in, sizeof in), full, sizeof full);
  uint32_t r = put_w(buf, cap, full);
  if (part && r < cap) {
    char *s = strrchr(full, '\\');
    w32_wr32(part, s && s[1] ? buf + 2 * w32_put_wstr(0, 0, (s[1] = 0, full)) : 0);
  }
  w32_ret(c, r, 4);
}
static const char TEMP_WIN[] = "C:\\Users\\Player\\AppData\\Local\\Temp\\";
static void mk_temp(void) {
  char host[PATH_MAX];
  const char *parts[] = {"C:\\Users", "C:\\Users\\Player", "C:\\Users\\Player\\AppData", "C:\\Users\\Player\\AppData\\Local", TEMP_WIN};
  for (int k = 0; k < 5; k++) mkdir(w32_path(parts[k], host, sizeof host), 0755);
}
static void k_GetTempPathA(rc_ctx *c) { mk_temp(); w32_ret(c, put_a(w32_arg(c, 1), w32_arg(c, 0), TEMP_WIN), 2); }
static void k_GetTempPathW(rc_ctx *c) { mk_temp(); w32_ret(c, put_w(w32_arg(c, 1), w32_arg(c, 0), TEMP_WIN), 2); }
// out: 1024 bytes; a name longer than a path (MAX_PATH, the caller's buffer) is refused as on Windows
static uint32_t temp_name(rc_ctx *c, const char *dir, const char *pre, uint32_t uniq, char *out) {
  static uint32_t seq;
  for (int tries = 0; tries < 65535; tries++) {
    uint32_t u = uniq ? uniq : (__atomic_add_fetch(&seq, 1, __ATOMIC_RELAXED) + (uint32_t)getpid()) & 0xffff;
    if (!u) continue;
    int len = snprintf(out, 1024, "%s%s%.3s%X.tmp", dir, dir[0] && dir[strlen(dir) - 1] != '\\' ? "\\" : "", pre, u);
    if (len >= 260) {
      w32_set_error(c, 111);  // ERROR_BUFFER_OVERFLOW
      return 0;
    }
    if (uniq) return u;
    char host[PATH_MAX];
    int fd = open(w32_path(out, host, sizeof host), O_CREAT | O_EXCL | O_WRONLY, 0644);
    if (fd >= 0) { close(fd); return u; }
    if (errno != EEXIST) break;
  }
  set_errno_err(c);
  return 0;
}
static void k_GetTempFileNameA(rc_ctx *c) {
  char out[1024];
  uint32_t r = temp_name(c, w32_cstr(w32_arg(c, 0)), w32_cstr(w32_arg(c, 1)), w32_arg(c, 2), out);
  if (r) strcpy((char *)RC32_H(w32_arg(c, 3)), out);
  w32_ret(c, r, 4);
}
static void k_GetTempFileNameW(rc_ctx *c) {
  char d[1024], p[64], out[1024];
  uint32_t r = temp_name(c, w32_wstr(w32_arg(c, 0), d, sizeof d), w32_wstr(w32_arg(c, 1), p, sizeof p), w32_arg(c, 2), out);
  if (r) w32_put_wstr(w32_arg(c, 3), 260, out);
  w32_ret(c, r, 4);
}
static void k_GetDriveTypeA(rc_ctx *c) { w32_ret(c, 3, 1); }  // DRIVE_FIXED
static void k_GetDriveTypeW(rc_ctx *c) { w32_ret(c, 3, 1); }
static void k_GetLogicalDrives(rc_ctx *c) { w32_ret(c, 1u << 2, 0); }  // C:
static void k_GetDiskFreeSpaceA(rc_ctx *c) {  // 4 KB clusters, 100 GB free of 200 GB
  if (w32_arg(c, 1)) w32_wr32(w32_arg(c, 1), 8);
  if (w32_arg(c, 2)) w32_wr32(w32_arg(c, 2), 512);
  if (w32_arg(c, 3)) w32_wr32(w32_arg(c, 3), 26214400);
  if (w32_arg(c, 4)) w32_wr32(w32_arg(c, 4), 52428800);
  w32_ret(c, 1, 5);
}

// --- find --------------------------------------------------------------------------------------
// fills WIN32_FIND_DATAA / W from the next match; 0 at the end. A folder in the overlay and in the game
// folder lists both: the overlay's entries, then the game folder's that the overlay does not have.
static int find_next(w32_obj *o, uint32_t fd, int wide) {
  struct dirent *e;
  for (;;) {
    e = readdir((DIR *)o->u.find.dir);
    if (!e) {
      if (!o->u.find.dir2) break;
      closedir((DIR *)o->u.find.dir);
      o->u.find.dir = o->u.find.dir2;
      o->u.find.dir2 = 0;
      free(o->u.find.over);
      o->u.find.over = o->u.find.hostdir;
      o->u.find.hostdir = o->u.find.hostdir2;
      o->u.find.hostdir2 = 0;
      continue;
    }
    if (fnmatch(o->u.find.pattern, e->d_name, FNM_CASEFOLD | FNM_PERIOD * 0)) continue;
    char host[PATH_MAX];
    struct stat st;
    if (o->u.find.over) {  // the game folder's turn: the overlay's copy was listed
      snprintf(host, sizeof host, "%s/%s", o->u.find.over, e->d_name);
      if (!lstat(host, &st)) continue;
    }
    snprintf(host, sizeof host, "%s/%s", o->u.find.hostdir, e->d_name);
    if (stat(host, &st)) continue;
    memset(RC32_H(fd), 0, wide ? 592 : 320);
    w32_wr32(fd, attrs(&st, host));
    w32_wr64(fd + 4, filetime(st.st_birthtimespec));
    w32_wr64(fd + 12, filetime(st.st_atimespec));
    w32_wr64(fd + 20, filetime(st.st_mtimespec));
    w32_wr32(fd + 28, (uint32_t)((uint64_t)st.st_size >> 32));
    w32_wr32(fd + 32, (uint32_t)st.st_size);
    if (wide) w32_put_wstr(fd + 44, 260, e->d_name);
    else snprintf((char *)RC32_H(fd + 44), 260, "%s", e->d_name);
    return 1;
  }
  return 0;
}
static uint32_t find_first(rc_ctx *c, const char *win, uint32_t fd, int wide) {
  char full[1024], host[PATH_MAX];
  int on_host = host_path(win, full, sizeof full);  // as map_path keeps it
  if (!on_host) full_win(win, full, sizeof full);
  char *sl = strrchr(full, on_host ? '/' : '\\');
  const char *pat = sl ? sl + 1 : full;
  // only * and ? are wild on Windows: fnmatch's [ is a character here, in names like "[Band] Song"
  char pattern[512];
  size_t pn = 0;
  if (!strcmp(pat, "*.*")) pat = "*";
  for (; *pat && pn + 2 < sizeof pattern; pat++) {
    if (*pat == '[' || *pat == '\\') pattern[pn++] = '\\';
    pattern[pn++] = *pat;
  }
  pattern[pn] = 0;
  if (sl) *sl = 0;
  if (on_host) snprintf(host, sizeof host, "%s", sl == full ? "/" : full);  // checked with its last part
  else w32_path(sl == full + 2 ? (strcat(full, "\\"), full) : full, host, sizeof host);
  DIR *d = opendir(host);
  if (!d) {
    set_open_err(c, host);
    if (flog()) fprintf(stderr, "w32: file: find \"%s\": no directory\n", win);
    return 0xffffffffu;
  }
  w32_obj *o;
  uint32_t h = w32_handle_new(H_FIND, &o);
  if (!h) {
    closedir(d);
    w32_set_error(c, W32_ERROR_NOT_ENOUGH_MEMORY);
    return 0xffffffffu;
  }
  o->u.find.dir = d;
  o->u.find.hostdir = strdup(host);
  o->u.find.pattern = strdup(pattern);
  size_t g = sizeof GAME_WIN - 1;
  if (!on_host && !strncasecmp(full, GAME_WIN, g) && (full[g] == '\\' || !full[g])) {
    char game[PATH_MAX];
    join_ci(game_dir(), full + g, game, sizeof game);
    if (strcmp(game, host) && (o->u.find.dir2 = opendir(game))) o->u.find.hostdir2 = strdup(game);
  }
  if (!find_next(o, fd, wide)) {
    w32_handle_close(h);
    if (flog()) fprintf(stderr, "w32: file: find \"%s\": no match\n", win);
    w32_set_error(c, W32_ERROR_FILE_NOT_FOUND);
    return 0xffffffffu;
  }
  if (flog()) fprintf(stderr, "w32: file: find \"%s\"\n", win);
  return h;
}
static void k_FindFirstFileA(rc_ctx *c) { w32_ret(c, find_first(c, w32_cstr(w32_arg(c, 0)), w32_arg(c, 1), 0), 2); }
static void k_FindFirstFileW(rc_ctx *c) { char n[1024]; w32_ret(c, find_first(c, w32_wstr(w32_arg(c, 0), n, sizeof n), w32_arg(c, 1), 1), 2); }
static void k_FindFirstFileExA(rc_ctx *c) { w32_ret(c, find_first(c, w32_cstr(w32_arg(c, 0)), w32_arg(c, 2), 0), 6); }
static void k_FindFirstFileExW(rc_ctx *c) { char n[1024]; w32_ret(c, find_first(c, w32_wstr(w32_arg(c, 0), n, sizeof n), w32_arg(c, 2), 1), 6); }
static void find_nx(rc_ctx *c, int wide) {
  w32_obj *o = w32_handle_get(w32_arg(c, 0), H_FIND);
  int ok = o && o->u.find.dir && find_next(o, w32_arg(c, 1), wide);
  if (!ok) w32_set_error(c, o ? W32_ERROR_NO_MORE_FILES : W32_ERROR_INVALID_HANDLE);
  w32_ret(c, ok, 2);
}
static void k_FindNextFileA(rc_ctx *c) { find_nx(c, 0); }
static void k_FindNextFileW(rc_ctx *c) { find_nx(c, 1); }
static void k_FindClose(rc_ctx *c) {
  uint32_t h = w32_arg(c, 0);
  int ok = w32_handle_get(h, H_FIND) && w32_handle_close(h);
  if (!ok) w32_set_error(c, W32_ERROR_INVALID_HANDLE);
  w32_ret(c, ok, 1);
}

// --- file mappings: a view is a guest copy of the file's bytes ---------------------------------
typedef struct view { uint32_t g, size; int fd; int64_t off; int write; struct view *next; } view;
static view *views;
static pthread_mutex_t view_mu = PTHREAD_MUTEX_INITIALIZER;
static void k_CreateFileMappingA(rc_ctx *c) {
  int fd = w32_file_fd(w32_arg(c, 0));
  uint64_t sz = (uint64_t)w32_arg(c, 3) << 32 | w32_arg(c, 4);
  struct stat st;
  if (fd < 0 || fstat(fd, &st)) { w32_set_error(c, W32_ERROR_INVALID_HANDLE); w32_ret(c, 0, 6); return; }
  if (!sz) sz = (uint64_t)st.st_size;
  w32_obj *o;
  uint32_t h = w32_handle_new(H_MAPPING, &o);
  if (!h) { w32_set_error(c, W32_ERROR_NOT_ENOUGH_MEMORY); w32_ret(c, 0, 6); return; }
  o->u.map.fd = dup(fd);
  o->u.map.size = (uint32_t)sz;
  w32_set_error(c, 0);
  w32_ret(c, h, 6);
}
static void k_MapViewOfFile(rc_ctx *c) {
  w32_obj *o = w32_handle_get(w32_arg(c, 0), H_MAPPING);
  if (!o) { w32_set_error(c, W32_ERROR_INVALID_HANDLE); w32_ret(c, 0, 5); return; }
  int64_t off = (int64_t)((uint64_t)w32_arg(c, 2) << 32 | w32_arg(c, 3));
  uint32_t n = w32_arg(c, 4) ? w32_arg(c, 4) : o->u.map.size - (uint32_t)off;
  uint32_t g = w32_galloc(n);
  if (!g) { w32_set_error(c, W32_ERROR_NOT_ENOUGH_MEMORY); w32_ret(c, 0, 5); return; }
  ssize_t r = pread(o->u.map.fd, RC32_H(g), n, off);
  (void)r;
  view *v = malloc(sizeof *v);
  pthread_mutex_lock(&view_mu);
  *v = (view){g, n, dup(o->u.map.fd), off, (w32_arg(c, 1) & 2) != 0, views};
  views = v;
  pthread_mutex_unlock(&view_mu);
  w32_ret(c, g, 5);
}
static void k_UnmapViewOfFile(rc_ctx *c) {
  uint32_t g = w32_arg(c, 0);
  pthread_mutex_lock(&view_mu);
  view **p = &views;
  while (*p && (*p)->g != g) p = &(*p)->next;
  view *v = *p;
  if (v) *p = v->next;
  pthread_mutex_unlock(&view_mu);
  if (v) {
    if (v->write) {
      ssize_t r = pwrite(v->fd, RC32_H(v->g), v->size, v->off);
      (void)r;
    }
    close(v->fd);
    w32_gfree(v->g, v->size);
    free(v);
  }
  w32_ret(c, v != 0, 1);
}

#define E(n) {#n, k_##n}
static const w32_export fns[] = {
    E(CreateFileA), E(CreateFileW), E(ReadFile), E(WriteFile), E(SetFilePointer), E(SetFilePointerEx),
    E(GetFileSize), E(GetFileSizeEx), E(SetEndOfFile), E(FlushFileBuffers), E(GetFileType), E(GetFileTime),
    E(SetFileTime), E(GetFileInformationByHandle), E(LockFile), E(UnlockFile), E(DeviceIoControl),
    E(GetFileAttributesA), E(GetFileAttributesW), E(GetFileAttributesExA), E(GetFileAttributesExW),
    E(SetFileAttributesA), E(SetFileAttributesW), E(DeleteFileA), E(DeleteFileW), E(RemoveDirectoryA),
    E(RemoveDirectoryW), E(CreateDirectoryA), E(CreateDirectoryW), E(MoveFileA), E(MoveFileW),
    E(GetCurrentDirectoryA), E(GetCurrentDirectoryW), E(SetCurrentDirectoryA), E(SetCurrentDirectoryW),
    E(GetWindowsDirectoryA), E(GetSystemDirectoryA),
    E(GetFullPathNameA), E(GetFullPathNameW), E(GetTempPathA), E(GetTempPathW), E(GetTempFileNameA),
    E(GetTempFileNameW), E(GetDriveTypeA), E(GetDriveTypeW), E(GetLogicalDrives), E(GetDiskFreeSpaceA),
    E(FindFirstFileA), E(FindFirstFileW), E(FindFirstFileExA), E(FindFirstFileExW), E(FindNextFileA),
    E(FindNextFileW), E(FindClose), E(CreateFileMappingA), E(MapViewOfFile), E(UnmapViewOfFile),
};
extern const w32_dll w32_dll_kernel32_sync;
W32_DLL(w32_dll_kernel32_file, "kernel32.dll", fns, &w32_dll_kernel32_sync);
