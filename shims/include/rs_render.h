// RS Re-ARMed render API: game textures for mods (MIT).
//
// Exported by libRSOpenGL, in both apps (RS Re-ARMed, RS Re-ARMed Learn & Play). A mod says which of the game's
// textures it means, by what the game uploads, and gives pixels to draw instead of it, or hides what is drawn with it.
// The game's own texture is left as it is: the swap is made when a draw binds it, so the game can't tell.
//
// A texture is known by the hash of its level 0, the bytes the game gave the renderer, and the size. The hash of a
// texture comes from the texture log (RS_GL_TEX_LOG, docs/mac/METAL.md), or rs_render_hash on bytes the mod has. The same
// file in the game's data can give another hash in the other edition: an uncompressed texture, whose bytes are in another
// order there. A mod lists them per edition (rc_target()).
//
// Detecting the API:
//
//   int (*ver)(void) = (int (*)(void))dlsym(RTLD_DEFAULT, "rs_render_api_version");
//   if (ver && ver() >= 1) { ...dlsym the functions below... }
//
// The API is on the Metal renderer alone: on OpenGL (a Mac without Metal, or the fallback) rs_render_api_version
// returns 0, and rs_render_match NULL.
//
// Version 1: everything in this header.
#ifndef RS_RENDER_H
#define RS_RENDER_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define RS_RENDER_API_VERSION 1

typedef struct rs_tex_rule rs_tex_rule;

// The API version this library implements, 0 on a renderer without it.
int rs_render_api_version(void);

// A rule for the texture of this size and hash, 2D textures only. It applies to what the game uploads from now on, so make
// rules in a mod's constructor, before the game starts. It lasts for the run. The same size and hash give the same rule,
// so two mods share it, and the last to call rs_render_replace wins. NULL when there is no room (64 rules) or the renderer
// has no API. w and h 0: any size, which hashes every upload (for diagnostics).
rs_tex_rule *rs_render_match(int w, int h, uint64_t hash);

// Draws w x h pixels in place of the texture, from the next frame on. rgba is RGBA, 8 bits per channel, R first in memory,
// rows top first with no padding, and is copied. It may be another size than the game's texture: a draw samples it with
// the game's sampler, without mip levels. NULL goes back to the game's texture. Any thread, at any time. 0, or -1 for a
// size out of range (1 to 4096).
int rs_render_replace(rs_tex_rule *rule, int w, int h, const void *rgba);

// 1: draws that bind the texture are skipped, whichever other textures they use. 0: they are drawn. Any thread, at any time.
int rs_render_hide(rs_tex_rule *rule, int on);

// The hash of bytes, as a rule takes it.
uint64_t rs_render_hash(const void *data, size_t bytes);

#ifdef __cplusplus
}
#endif

#endif
