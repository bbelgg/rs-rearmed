// Files a mod serves to the game, the same in both editions: a file provider answers for files the game opens for
// reading, asks the size and date of, and finds in a folder's listing. The game reads such a file as it reads any
// other; where its bytes come from is the provider's business.
//
//   static int my_stat(void *user, const char *path, rc_file_info *info) { ... }       // 1: this path is mine
//   static void *my_open(void *user, const char *path, rc_file_info *info) { ... }     // the file, or 0
//   static int64_t my_read(void *user, void *file, void *buf, uint64_t n, int64_t off) { ... }
//   static void my_close(void *user, void *file) { ... }
//   static void my_list(void *user, const char *dir, rc_file_add add, void *ctx) { add(ctx, "name"); }
//   static int my_hide(void *user, const char *dir, const char *name) { ... }          // 1: leave it out
//   __attribute__((constructor)) static void init(void) {
//     static const rc_file_provider p = {.open = my_open, .read = my_read, .close = my_close, .stat = my_stat,
//                                        .list = my_list, .hide = my_hide};
//     rc_file_provide(&p);
//   }
//
// Paths are the host's, absolute, as the game's open resolved them (Learn & Play: after the Win32 seam's mapping of
// C:\Rocksmith2014 and its case-insensitive lookup; a name no file has keeps the case the game gave). open is asked
// before the system for every file opened for reading alone, so a provider can also answer for a file that exists;
// it returns 0 for every path it does not serve, and quickly: it is asked for every such open while a provider is
// registered. Without a provider nothing here costs more than a test of a counter.
//
// A file has a fixed size and date from its open to its close (*info, filled by open). read is positional and may
// be called from several threads at once, also for one file; it returns the bytes read, 0 at or past the end, and
// less than 0 for an error (the game sees EIO). list adds the names a folder lacks: the folder must exist, and a
// name it really has is listed once. hide leaves a real entry out of the listing (the file itself still opens). The
// first provider that answers wins, in the order they registered.
//
// What the game is not offered through a provider: writing, a folder that does not exist, and (Mac) a FILE opened
// in a mode other than "r" or "rb".
#ifndef RC_FILE_H
#define RC_FILE_H

#include <dirent.h>
#include <stdint.h>
#include <stdio.h>
#include <sys/stat.h>
#include <sys/types.h>

typedef struct rc_file_info {
  int64_t size;   // bytes
  int64_t mtime;  // seconds since 1970
} rc_file_info;

typedef void (*rc_file_add)(void *ctx, const char *name);

typedef struct rc_file_provider {
  void *(*open)(void *user, const char *path, rc_file_info *info);
  int64_t (*read)(void *user, void *file, void *buf, uint64_t n, int64_t off);
  void (*close)(void *user, void *file);
  int (*stat)(void *user, const char *path, rc_file_info *info);                     // optional: open and close serve
  void (*list)(void *user, const char *dir, rc_file_add add, void *ctx);            // optional
  int (*hide)(void *user, const char *dir, const char *name);                       // optional
  void *user;
} rc_file_provider;

// Registers a provider (the structure is copied). 0, or -1 when open, read or close is missing or eight are
// registered already.
int rc_file_provide(const rc_file_provider *p);

// --- what the editions' file layers call in place of the system's functions ---------------------------------------
// Each is the system's function for every file no provider serves. A served file is a descriptor of its own (a
// placeholder the system knows, so dup, close and the descriptor's number behave), and these answer for it.
extern int rc_file_nproviders;
int rc_file_open(const char *path, int flags, ...);
int rc_file_close(int fd);
ssize_t rc_file_read(int fd, void *buf, size_t n);
ssize_t rc_file_pread(int fd, void *buf, size_t n, off_t off);
off_t rc_file_lseek(int fd, off_t off, int whence);
int rc_file_fstat(int fd, struct stat *st);
int rc_file_dup(int fd);
int rc_file_stat(const char *path, struct stat *st);
// 1 when fd is a served file
int rc_file_served(int fd);
// A served file as a FILE (funopen), whose fileno is the placeholder descriptor: fread, fseek, ftell, fstat of its
// fileno and fclose work as on a file.
FILE *rc_file_fopen(const char *path, const char *mode);
// A folder's listing with the providers' names added and hidden ones left out
DIR *rc_file_opendir(const char *path);
struct dirent *rc_file_readdir(DIR *d);
int rc_file_closedir(DIR *d);

#endif
