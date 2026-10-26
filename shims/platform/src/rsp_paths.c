// File paths: the globals set by SetGameRoot (0x1002a5cf0) and their getters
// (0x1002a5ad0-0x1002a5b30, 0x1002a5d80), the root-copy hooks efd gets
// (0x1002a5de0/0x1002a5e10) and efd's IsRelativePath (0x10130a9e0).
#include "rsp_platform.h"

#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

static char g_root[RSP_PATH_MAX];    // 0x1018ec0c0  "%s/"
static char g_tmp[RSP_PATH_MAX];     // 0x10194f130  "%sTmp/"
static char g_data[RSP_PATH_MAX];    // 0x10194ed30  "%sData/"
static char g_update[RSP_PATH_MAX];  // 0x10194f530  "%sUpdate/"

int rsp_paths_set_root(const char *dir) {
  // The original sprintf()s into 1 KiB globals without a bound; this one
  // truncates instead of overflowing.
  chdir(dir);
  snprintf(g_root, sizeof g_root, "%s/", dir);
  snprintf(g_tmp, sizeof g_tmp, "%sTmp/", g_root);
  snprintf(g_data, sizeof g_data, "%sData/", g_root);
  snprintf(g_update, sizeof g_update, "%sUpdate/", g_root);
  return mkdir(g_tmp, 0777);  // 0x1013132a0: mkdir(path, 0x1ff)
}

const char *rsp_path_root(void) { return g_root; }
const char *rsp_path_data(void) { return g_data; }
const char *rsp_path_tmp(void) { return g_tmp; }
const char *rsp_path_update(void) { return g_update; }

int rsp_path_copy_root(char *buf, int size) {
  // efd::Snprintf (0x10130b200): size 0 returns 0 without touching buf;
  // otherwise buf[0] = 0, vsnprintf, and -1 if it did not fit. The hook
  // returns ret >= 0 && ret < size.
  if (size == 0) return 0;
  int n = snprintf(buf, (size_t)(unsigned)size, "%s", g_root);
  if (n >= 0 && (size_t)n >= (size_t)(unsigned)size) n = -1;
  return n >= 0 && n < size;
}

int rsp_path_is_relative(const char *p) {
  return p && p[0] != '/' && p[0] != '\\';  // "" counts as relative
}

int rsp_path_ini(char *buf, size_t size) {
  int n = snprintf(buf, size, "%sRocksmith.ini", g_root);
  return n >= 0 && (size_t)n < size;
}
