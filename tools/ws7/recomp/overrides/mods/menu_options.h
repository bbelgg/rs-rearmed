// Adv Display Settings' own options: the part that is one source for both editions (menu_options_common.c), the
// options, their switches and files and whether each can be changed now. Each edition's methods in front of its
// game's menu code use it: the Mac's menu_options.c, Learn & Play's win-x86/shims/win32_menu_options.c.
#ifndef RS_MENU_OPTIONS_H
#define RS_MENU_OPTIONS_H

#include <stddef.h>

// A checkbox has two choices, off and on (its switch 0 or 1; Low Latency's none or 2), shown as the game's own Off and
// On; a slider has one notch for each of its choices. Each choice is a value of the switch ("" none) and the text it
// shows. The order of the buttons is cache_overlay.py's MENU_OPTIONS (their ids match).
#define RS_MENU_CHOICES 6
typedef struct {
  const char *id;   // the button's ID in the definition, and the setting's name
  const char *env;  // its switch, or NULL for the renderer's file
  int slider;       // 0: a checkbox
  int only;         // the renderer it's for: RS_MENU_METAL, RS_MENU_OPENGL, or 0 both
  int n, dflt;      // the choices, and which one is the default (without the switch)
  const char *value[RS_MENU_CHOICES], *text[RS_MENU_CHOICES];
  int mac_only;      // not in Learn & Play, which has Metal only (cache_overlay.py MAC_ONLY_OPTIONS)
  const char *path;  // "buttons.<id>.states"
  const char *name;  // its fixed string, what the edited setting's global holds while Edit Setting edits it (the
                     // edition's glue makes it)
  int now;           // the choice this run has (-1: a value of the switch no choice has)
  int session;       // the session sets its switch: locked
} rs_menu_option;
enum { RS_MENU_METAL = 1, RS_MENU_OPENGL };  // the renderer's choices, after 0

extern rs_menu_option rs_menu_opts[];
extern const size_t rs_menu_nopts;

// Once, at the first frame: each option's path, choice and session lock (LP: 1 for Learn & Play, whose list leaves the
// Mac's own out: rs_menu_opt_of_path and _of_id don't find them).
void rs_menu_opts_init(int lp);
// Whether the option is in this edition's list.
int rs_menu_opt_here(const rs_menu_option *o);
rs_menu_option *rs_menu_opt_of_path(const char *path);  // "Buttons.<id>.States", in any case
rs_menu_option *rs_menu_opt_of_id(const char *id);
// The option whose fixed string or id CUR is (the edited setting's global), or NULL.
rs_menu_option *rs_menu_opt_named(const char *cur);
int rs_menu_choice(const rs_menu_option *o);  // its choice, the default for a value no choice has
const char *rs_menu_choice_text(const rs_menu_option *o, int c);
void rs_menu_set_choice(rs_menu_option *o, int c);  // the choice made, written to options.env (or the renderer's file)
// Why an option can't be changed now, as its row says it (NULL: it can); RS_MENU_SESSION for a switch the session sets.
const char *rs_menu_locked(const rs_menu_option *o);
extern const char RS_MENU_SESSION[];
// Whether the renderer's RS_GL_BACKEND came from outside the game (the Mac shell's; NULL: not asked).
extern int (*rs_menu_renderer_env_given)(void);
// A choice handed on as the game runs as well (the Mac shell's rsp_live_switch; NULL: Learn & Play, which reads the
// switches as it starts): 1 when it takes effect from the next frame on.
extern int (*rs_menu_live_switch)(const char *name, const char *value);

// The list's scroll bar: the overlay's edit of SettingsScene puts the game's own scroll bar beside the panel
// (cache_overlay.py: BAR_GAP and THUMB_LEAST there, SettingsScene's BACKGROUND_* in the movie: the same numbers as
// here, a test says). The movie's 1280 x 720.
enum { STAGE_W = 1280, STAGE_H = 720, PANEL_X = 70, PANEL_W = 600, PANEL_Y = 40, PANEL_H = 640 };
enum { BAR_GAP = 8, THUMB_LEAST = 100, BAR_REACH = 24 };  // (BAR_REACH: the bar's width and some, to press it)
// The row (0 .. N-1) whose thumb place is at Y (the movie's), for a list of N rows.
int rs_menu_bar_row(int n, double y);
// Whether a press at (X, Y) (the movie's) is on the bar.
int rs_menu_on_bar(double x, double y);

#endif
