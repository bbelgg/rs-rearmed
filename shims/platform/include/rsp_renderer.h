// Renderer choice of a game start: Metal (the default) or OpenGL, and the fallback to OpenGL when
// Metal cannot start or keeps ending abnormally (docs/METAL-DESIGN.md "Defaults and fallback").
//
// libRSOpenGL picks its backend once, when it loads, from RS_GL_BACKEND (unset: Metal). main()
// decides as early as it can (before the image, the engine and the window) and, when the decision
// differs from what libRSOpenGL loaded with, starts the process again with RS_GL_BACKEND set
// (execv of the same executable and arguments: the same app for Metal's shader cache).
//
// Inputs, strongest first:
//   RS_GL_BACKEND=gl|metal  the environment: always wins (metal: also over the crash count)
//   no Metal                no Metal device, or libRSOpenGL without its Metal presentation: OpenGL
//   crash count             the last two Metal sessions ended without a clean exit: OpenGL, and the
//                           setting becomes "opengl" (writing "metal" there again tries Metal again)
//   <home>/renderer         the setting: "metal" or "opengl" (written by install.sh / the installer)
//   default                 Metal
// <home>: RS_NATIVE_HOME, else the install folder when the app is <home>/game/<app> of an install
// (it has <home>/game-dir), else ~/Library/Caches/rs-rearmed (builds run from elsewhere).
//
// Session marker: a Metal game session writes <home>/metal-session ("N": the abnormal endings in a
// row before it) and removes it on a clean exit (rsp_exit, the shell's quit paths, exit(),
// SIGTERM/SIGINT/SIGHUP). Found at the next start: that session ended abnormally (a crash, a hang
// that was force-quit, a kill), N + 1 in a row.
//
// Portable C (no Metal): the caller says what libRSOpenGL loaded with and whether Metal can start.
#ifndef RSP_RENDERER_H
#define RSP_RENDERER_H

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

enum { RSP_GFX_NONE = -1, RSP_GFX_GL = 0, RSP_GFX_METAL = 1 };  // as rs_gfx_backend_t

typedef enum {
  RSP_RENDERER_DEFAULT = 0,  // nothing set: Metal
  RSP_RENDERER_ENV,          // RS_GL_BACKEND
  RSP_RENDERER_SETTING,      // <home>/renderer
  RSP_RENDERER_CRASHES,      // the last two Metal sessions ended abnormally: OpenGL
  RSP_RENDERER_NO_METAL,     // Metal cannot start here: OpenGL
  RSP_RENDERER_NO_RSGL,      // libRSOpenGL is not loaded: the system OpenGL, nothing to choose
} rsp_renderer_reason;

enum { RSP_RENDERER_CRASH_LIMIT = 2 };  // abnormal Metal endings in a row before OpenGL is used

typedef struct {
  const char *env;      // RS_GL_BACKEND, or NULL
  const char *setting;  // the setting file's contents, or NULL (no file)
  int abnormal;         // Metal sessions in a row that ended abnormally, before this start
  int metal_ok;         // Metal can start (a device, libRSOpenGL's Metal presentation)
  int loaded;           // the backend libRSOpenGL loaded with (RSP_GFX_*; NONE: not loaded)
} rsp_renderer_in;

typedef struct {
  int backend;  // RSP_GFX_GL or RSP_GFX_METAL
  int reason;   // rsp_renderer_reason
  int reexec;   // differs from `loaded`: start again with RS_GL_BACKEND=<backend>
} rsp_renderer_out;

// "metal" -> RSP_GFX_METAL, "gl" / "opengl" -> RSP_GFX_GL (any case, surrounding white space and
// a newline allowed), anything else (or NULL) -> RSP_GFX_NONE.
int rsp_renderer_parse(const char *s);
// The decision (pure).
rsp_renderer_out rsp_renderer_decide(const rsp_renderer_in *in);
const char *rsp_renderer_name(int backend);  // "metal" / "opengl"
const char *rsp_renderer_reason_text(int reason);

// <home> (see above) into out; 0 if there is none (no HOME). `bundle`: the app's path or NULL.
int rsp_renderer_home(char *out, size_t n, const char *bundle);
// <home>/renderer: its first line into buf (1), or 0 when there is no file.
int rsp_renderer_read_setting(const char *home, char *buf, size_t n);
int rsp_renderer_write_setting(const char *home, int backend);  // 0 ok
// Abnormal Metal endings in a row before now: the marker's N + 1, or 0 without a marker.
int rsp_metal_abnormal_count(const char *home);
// Writes the marker for this session (N = abnormal) and remembers it for rsp_metal_session_end.
int rsp_metal_session_begin(const char *home, int abnormal);
// A clean exit: removes this process's marker (once; async-signal-safe).
void rsp_metal_session_end(void);
// Removes <home>'s marker whoever wrote it (the count starts again).
void rsp_metal_session_forget(const char *home);

enum {
  RSP_RENDERER_SESSION = 1,  // a game session: a Metal start writes the marker, a fallback notifies
};
// main(): decides, logs it ("[rsp] renderer: ..."), applies a crash fallback (setting -> opengl, the
// count reset, a note in <home>/renderer-fallback.txt, a notification for a session), and when the
// decision differs from `loaded` executes argv[0]'s executable again with RS_GL_BACKEND set (does
// not return then; on a failed exec it returns the backend that is loaded). `metal_ok` is asked only
// when Metal would be chosen (NULL: Metal can start). Keeps argv for rsp_renderer_restart_gl.
rsp_renderer_out rsp_renderer_startup(char **argv, const char *bundle, int loaded, int (*metal_ok)(void),
                                      int flags);
// Late fallback (Metal failed after startup, e.g. no layer context): ends the Metal session as not
// abnormal and executes the process again with RS_GL_BACKEND=gl. Returns -1 when it cannot (no
// argv from rsp_renderer_startup, or the exec failed).
int rsp_renderer_restart_gl(const char *why);

#ifdef __cplusplus
}
#endif
#endif
