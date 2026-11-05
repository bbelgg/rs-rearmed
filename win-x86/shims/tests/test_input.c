// The hook points of the input layer (win32_input.c): with no mod events go straight to the game; with one they
// wait for the game thread, the hooks see the Mac's call, and a pre hook can keep an event from the game.
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "rc_hook.h"
#include "win32_shims.h"

#define CHECK(x) do { if (!(x)) { fprintf(stderr, "FAIL line %d: %s\n", __LINE__, #x); exit(1); } } while (0)

static uint32_t posted[32], nposted, focus = 99, nfocus;
void w32_post_input(uint32_t msg, uint32_t wp, uint32_t lp) { posted[nposted++ % 32] = msg; }
void w32_user_focus(int active) { focus = (uint32_t)active; nfocus++; }

static uint32_t code, a, b, nseen, npost, drop_a = ~0u, fseen, drop_focus, stay_focused, key_up;
static int pre(rc_ctx *c, void *user) {
  code = (uint32_t)c->r[7];
  a = (uint32_t)c->r[6];
  b = (uint32_t)c->r[2];
  nseen++;
  if (code == 2 && a == 0x24) key_up++;
  return a == drop_a;
}
static void post(rc_ctx *c, void *user) { npost++; }
static int pre_focus(rc_ctx *c, void *user) {
  fseen = (uint32_t)c->r[7] + 1;
  if (stay_focused) c->r[7] = 1;
  return drop_focus;
}

int main(void) {
  // no mod: at once
  w32_input_key(0x24, 1, 13);
  CHECK(nposted == 2 && posted[0] == 0x100 && posted[1] == 0x102);
  w32_input_key(0x24, 0, 0);
  CHECK(nposted == 3 && posted[2] == 0x101);
  w32_input_focus(1);
  CHECK(nfocus == 1 && focus == 1);
  w32_input_frame();

  CHECK(!rc_hook_add_seam("input_sink", pre, post, NULL));
  CHECK(!rc_hook_add_seam("set_app_focus", pre_focus, NULL, NULL));
  CHECK(rc_hook_add_seam("no_such_point", pre, NULL, NULL));

  // a key waits for the frame, and goes on with the Mac's code, key code and character
  nposted = 0;
  w32_input_key(0x24, 1, 13);
  CHECK(nposted == 0 && nseen == 0);
  w32_input_frame();
  CHECK(nseen == 1 && code == 1 && a == 0x24 && b == 13 && npost == 1);
  CHECK(nposted == 2 && posted[0] == 0x100);

  // a pre hook keeps one from the game: no message, no key state, no post hook
  drop_a = 0x35;
  nposted = 0;
  w32_input_key(0x35, 1, 27);
  w32_input_frame();
  CHECK(nseen == 2 && code == 1 && a == 0x35 && nposted == 0 && npost == 1);
  CHECK(!(w32_input_vk(0x1b, 0) & 0x8000));
  drop_a = ~0u;

  // the pointer: y from the bottom, as the Mac's view has it; buttons and the wheel
  w32_input_view_height(100);
  w32_input_mouse_move(10, 30, 0, 0);
  w32_input_frame();
  CHECK(code == 3 && a == 10 && b == 70);
  w32_input_mouse_button(1, 1);
  w32_input_frame();
  CHECK(code == 5 && a == 1);
  w32_input_mouse_wheel(-120);
  w32_input_frame();
  CHECK(code == 6 && a == 0 && b == 1);

  // losing the window: what is held goes up through the hooks, in order, then the focus
  nposted = 0;
  w32_input_release_all();
  w32_input_focus(0);
  CHECK(nposted == 0 && fseen == 0);
  w32_input_frame();
  CHECK(key_up == 1 && code == 4 && a == 1);  // the key, then the button
  CHECK(fseen == 1 && focus == 0 && nfocus == 2);

  // a pre hook keeps the focus change from the game too
  drop_focus = 1;
  w32_input_focus(1);
  w32_input_frame();
  CHECK(fseen == 2 && focus == 0 && nfocus == 2);
  drop_focus = 0;

  // or changes it: the game hears the Mac's argument as the hook left it
  stay_focused = 1;
  w32_input_focus(0);
  w32_input_frame();
  CHECK(fseen == 1 && focus == 1 && nfocus == 3);
  stay_focused = 0;

  // a mod's call to input_sink goes the way of a key from the host: through the hooks, then to the game
  uint64_t args[3] = {1, 0x24, 13};
  nposted = nseen = 0;
  CHECK(!rc_call_seam("input_sink", args, 3));
  CHECK(nseen == 0);
  w32_input_frame();
  CHECK(nseen == 1 && code == 1 && a == 0x24 && b == 13 && nposted == 2 && posted[0] == 0x100);
  args[0] = 2;
  CHECK(!rc_call_seam("input_sink", args, 3));
  w32_input_frame();
  CHECK(code == 2 && posted[2] == 0x101);
  args[0] = 3;
  CHECK(rc_call_seam("input_sink", args, 3));
  CHECK(rc_call_seam("set_app_focus", args, 3));
  CHECK(rc_call_seam(NULL, args, 3));

  // more than the queue holds: the rest goes past the hooks, nothing is lost
  nposted = 0;
  for (int i = 0; i < 1100; i++) w32_input_mouse_move(i, 0, 0, 0);
  CHECK(nposted > 0);
  w32_input_frame();
  puts("test_input: ok");
  return 0;
}
